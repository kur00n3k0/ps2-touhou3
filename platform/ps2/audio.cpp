// PS2 port: music and sound effect playback. See audio.hpp.

#include "platform/ps2/audio.hpp"
#include "platform/ps2/fs.hpp"
#include <audsrv.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

namespace audio {

enum {
	RATE = 44100,
	CHUNK_FRAMES = 512,           // ~11.6 ms per mixer pass
	FADE_STEP_US = (9216 * 8),    // PMD: fout() every 8 timer A ticks
};

// Mixer state. Commands from the game thread are posted under [lock] and
// picked up by the mixer thread on its next pass.
namespace {

struct Song {
	FILE *fp = nullptr;
	long data_offset = 0;   // byte offset of PCM data in the file
	long total = 0;         // frames
	long loop_start = -1;   // frame, -1 = no loop
	long pos = 0;           // frame
	bool playing = false;
	int fade_speed = 0;
	int fade_value = 0;
	long fade_us = 0;       // time accumulated towards the next fade step
	long frames_played = 0;
};

Song song;
char song_name[32] = "";
bool song_name_changed = false;

enum cmd_t { CMD_NONE, CMD_PLAY, CMD_STOP, CMD_FADE };
volatile cmd_t pending = CMD_NONE;
volatile int pending_fade = 0;
volatile uint8_t fade_value_shared = 0;

int lock = -1;
bool ready = false;
int16_t mixbuf[CHUNK_FRAMES * 2];

// Sound effects: pre-rendered from YUME.EFC (ps2port/tools/efc2m.py +
// pmd2wav) and kept in memory. Like PMD's single FM effect channel, a new
// effect replaces the one that is playing.
enum { SE_SLOTS = 64 };
struct Effect {
	int16_t *pcm = nullptr; // stereo frames
	long frames = 0;
};
Effect effects[SE_SLOTS];
volatile int se_request = -1;
int se_cur = -1;
long se_pos = 0;

// Seeks [fp] to the PCM data of a RIFF WAV file and returns its size in
// frames (16-bit stereo), or 0 if there is no data chunk.
long wav_find_data(FILE *fp)
{
	uint8_t hdr[12];
	if(fread(hdr, 1, 12, fp) != 12) {
		return 0;
	}
	for(;;) {
		uint8_t ck[8];
		if(fread(ck, 1, 8, fp) != 8) {
			return 0;
		}
		const uint32_t size = (ck[4] | (ck[5] << 8) | (ck[6] << 16) | (ck[7] << 24));
		if(memcmp(ck, "data", 4) == 0) {
			return (size / 4);
		}
		fseek(fp, size, SEEK_CUR);
	}
}

void effects_load()
{
	int loaded = 0;
	for(int n = 0; n < SE_SLOTS; n++) {
		char path[256];
		fs::path(path, sizeof(path), "audio/se/SE%02d.wav", n);
		FILE *fp = fopen(path, "rb");
		if(!fp) {
			continue;
		}
		const long frames = wav_find_data(fp);
		if(frames > 0) {
			effects[n].pcm = static_cast<int16_t *>(malloc(frames * 4));
			if(effects[n].pcm) {
				effects[n].frames = fread(effects[n].pcm, 4, frames, fp);
				loaded++;
			}
		}
		fclose(fp);
	}
	printf("audio: %d sound effects loaded\n", loaded);
}

// Opens audio/<NAME>.wav and its .loop sidecar for [name] ("OP.M" → "OP").
bool song_open(Song& s, const char *name)
{
	if(s.fp) {
		fclose(s.fp);
		s.fp = nullptr;
	}
	char base[16];
	int i = 0;
	for(; name[i] && (name[i] != '.') && (i < 15); i++) {
		base[i] = toupper(uint8_t(name[i]));
	}
	base[i] = '\0';

	char path[256];
	fs::path(path, sizeof(path), "audio/%s.wav", base);
	s.fp = fopen(path, "rb");
	if(!s.fp) {
		printf("audio: missing %s\n", path);
		return false;
	}
	s.total = wav_find_data(s.fp);
	s.data_offset = ftell(s.fp);
	s.loop_start = -1;
	fs::path(path, sizeof(path), "audio/%s.wav.loop", base);
	if(FILE *lp = fopen(path, "r")) {
		long ls = -1, tot = 0;
		if(fscanf(lp, "%ld %ld", &ls, &tot) >= 1) {
			s.loop_start = ls;
		}
		fclose(lp);
	}
	s.pos = 0;
	return (s.total > 0);
}

// Reads up to [frames] frames of the song into [dst], handling the loop.
// Returns the number of frames written; fewer means the song ended.
int song_read(Song& s, int16_t *dst, int frames)
{
	int done = 0;
	while(done < frames) {
		if(s.pos >= s.total) {
			if(s.loop_start < 0) {
				break;
			}
			s.pos = s.loop_start;
		}
		long n = (frames - done);
		if(n > (s.total - s.pos)) {
			n = (s.total - s.pos);
		}
		fseek(s.fp, (s.data_offset + (s.pos * 4)), SEEK_SET);
		const size_t got = fread(dst + (done * 2), 4, n, s.fp);
		if(got == 0) {
			break;
		}
		s.pos += got;
		done += got;
	}
	return done;
}

void mixer_pass()
{
	memset(mixbuf, 0, sizeof(mixbuf));

	WaitSema(lock);
	if(song_name_changed) {
		song_name_changed = false;
		song.playing = false;
		song_open(song, song_name);
	}
	switch(pending) {
	case CMD_PLAY:
		if(song.fp) {
			song.pos = 0;
			song.playing = true;
			song.fade_speed = 0;
			song.fade_value = 0;
			song.fade_us = 0;
			song.frames_played = 0;
		}
		break;
	case CMD_STOP:
		song.playing = false;
		break;
	case CMD_FADE:
		song.fade_speed = pending_fade;
		break;
	default:
		break;
	}
	pending = CMD_NONE;
	SignalSema(lock);

	if(song.playing) {
		const int got = song_read(song, mixbuf, CHUNK_FRAMES);
		if(got < CHUNK_FRAMES) {
			song.playing = false;
		}
		song.frames_played += got;

		// PMD fade, applied as a linear gain on the mix.
		if(song.fade_speed) {
			song.fade_us += ((CHUNK_FRAMES * 1000000LL) / RATE);
			while(song.fade_us >= FADE_STEP_US) {
				song.fade_us -= FADE_STEP_US;
				if((song.fade_value + song.fade_speed) < 256) {
					song.fade_value += song.fade_speed;
				} else {
					song.fade_value = 255;
					song.fade_speed = 0;
					song.playing = false;
				}
			}
		}
		if(song.fade_value) {
			const int gain = (256 - song.fade_value);
			for(int i = 0; i < (CHUNK_FRAMES * 2); i++) {
				mixbuf[i] = int16_t((mixbuf[i] * gain) >> 8);
			}
		}
	}
	fade_value_shared = uint8_t(song.fade_value);

	if(se_request >= 0) {
		se_cur = se_request;
		se_request = -1;
		se_pos = 0;
	}
	if((se_cur >= 0) && effects[se_cur].pcm) {
		const Effect &e = effects[se_cur];
		long n = (e.frames - se_pos);
		if(n > CHUNK_FRAMES) {
			n = CHUNK_FRAMES;
		}
		const int16_t *src = (e.pcm + (se_pos * 2));
		for(long i = 0; i < (n * 2); i++) {
			int v = (mixbuf[i] + src[i]);
			mixbuf[i] = int16_t((v > 32767) ? 32767 : ((v < -32768) ? -32768 : v));
		}
		se_pos += n;
		if(se_pos >= e.frames) {
			se_cur = -1;
		}
	}

	audsrv_wait_audio(sizeof(mixbuf));
	audsrv_play_audio(reinterpret_cast<const char *>(mixbuf), sizeof(mixbuf));
}

uint8_t thread_stack[0x4000] __attribute__((aligned(16)));

void mixer_thread(void *)
{
	for(;;) {
		mixer_pass();
	}
}

} // namespace

bool init()
{
	if(audsrv_init() != 0) {
		printf("audio: audsrv_init failed: %s\n", audsrv_get_error_string());
		return false;
	}
	audsrv_fmt_t fmt;
	fmt.freq = RATE;
	fmt.bits = 16;
	fmt.channels = 2;
	if(audsrv_set_format(&fmt) != 0) {
		printf("audio: unsupported format\n");
		return false;
	}
	audsrv_set_volume(MAX_VOLUME);
	effects_load();

	ee_sema_t sema = {};
	sema.init_count = 1;
	sema.max_count = 1;
	lock = CreateSema(&sema);

	ee_thread_t th = {};
	th.func = reinterpret_cast<void *>(mixer_thread);
	th.stack = thread_stack;
	th.stack_size = sizeof(thread_stack);
	th.gp_reg = &_gp;
	th.initial_priority = 16; // above the game's main thread
	const int id = CreateThread(&th);
	if(id < 0) {
		printf("audio: CreateThread failed\n");
		return false;
	}
	StartThread(id, nullptr);
	ready = true;
	return true;
}

void song_load(const char *pmd_fn)
{
	if(!ready) {
		return;
	}
	WaitSema(lock);
	strncpy(song_name, pmd_fn, (sizeof(song_name) - 1));
	song_name_changed = true;
	SignalSema(lock);
}

static void post(cmd_t cmd, int fade = 0)
{
	if(!ready) {
		return;
	}
	WaitSema(lock);
	pending = cmd;
	pending_fade = fade;
	SignalSema(lock);
}

void song_play() { post(CMD_PLAY); }
void song_stop() { post(CMD_STOP); }
void song_fade(uint8_t speed) { post(CMD_FADE, speed); }
uint8_t song_fade_value() { return fade_value_shared; }

int song_measure()
{
	// TODO: per-song measure tables from pmd2wav (only the endings need them).
	return -1;
}

void se_play(int se)
{
	if(!ready || (se < 0) || (se >= SE_SLOTS) || !effects[se].pcm) {
		return;
	}
	se_request = se;
}

} // namespace audio
