// PS2 port: music and sound effect playback. See audio.hpp.
//
// Threads:
// • The reader thread streams the current song from disk into a ring buffer
//   (about 2 seconds), in large reads, handling the loop point. Slow media
//   (a disc, or OPL's USB emulation) only ever block this thread.
// • The mixer thread takes song frames from the ring, applies PMD's fade,
//   mixes in the current sound effect, and feeds audsrv.
// The game thread only posts commands.

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
	RING_FRAMES = (RATE * 2),     // 2 seconds of read-ahead
	READ_FRAMES = 8192,           // 32 KiB per disk read
	SE_SLOTS = 64,
};

namespace {

/// Shared state, guarded by [lock]
/// --------------------------------
int lock = -1;
bool ready = false;

// Requests from the game thread.
char req_name[32] = "";
bool req_load = false;     // reader: open [req_name]
bool req_restart = false;  // reader: flush the ring, start at frame 0
bool req_stop = false;     // mixer: stop playback
int req_fade = -1;         // mixer: new fade speed
bool req_fade_reset = false; // mixer: reset the fade (on play)

// Ring buffer between reader (producer) and mixer (consumer).
int16_t ring[RING_FRAMES * 2];
volatile long ring_write = 0;   // frames written in total
volatile long ring_read = 0;    // frames consumed in total
volatile bool ring_eof = false; // a non-looping song has been fully read
volatile bool playing = false;
/// --------------------------------

volatile uint8_t fade_value_shared = 0;

int reader_wake = -1; // semaphore: something for the reader to do

/// Reader thread state (only touched by the reader)
/// ------------------------------------------------
FILE *song_fp = nullptr;
long song_data_offset = 0;
long song_total = 0;       // frames
long song_loop_start = -1; // frame, -1 = no loop
long song_pos = 0;         // frame to read next
/// ------------------------------------------------

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

// Opens audio/<NAME>.WAV and its loop point file loop/<NAME>.LOP
// ("<loop start frame> <total frames>") for [name] ("op.m" → "OP").
bool song_open(const char *name)
{
	fs::IoLock io;
	if(song_fp) {
		fclose(song_fp);
		song_fp = nullptr;
	}
	song_total = 0;
	char base[16];
	int i = 0;
	for(; name[i] && (name[i] != '.') && (i < 15); i++) {
		base[i] = toupper(uint8_t(name[i]));
	}
	base[i] = '\0';

	char path[256];
	fs::path(path, sizeof(path), "audio/%s.WAV", base);
	song_fp = fopen(path, "rb");
	if(!song_fp) {
		printf("audio: missing %s\n", path);
		return false;
	}
	song_total = wav_find_data(song_fp);
	song_data_offset = ftell(song_fp);
	song_loop_start = -1;
	fs::path(path, sizeof(path), "loop/%s.LOP", base);
	if(FILE *lp = fopen(path, "r")) {
		long ls = -1, tot = 0;
		if(fscanf(lp, "%ld %ld", &ls, &tot) >= 1) {
			song_loop_start = ls;
		}
		fclose(lp);
	}
	song_pos = 0;
	return (song_total > 0);
}

// Fills the ring as far as possible.
void reader_fill()
{
	while(song_fp && !ring_eof) {
		const long space = (RING_FRAMES - (ring_write - ring_read));
		if(space < READ_FRAMES) {
			return;
		}
		if(song_pos >= song_total) {
			if(song_loop_start < 0) {
				ring_eof = true;
				return;
			}
			song_pos = song_loop_start;
		}
		long n = READ_FRAMES;
		if(n > (song_total - song_pos)) {
			n = (song_total - song_pos);
		}
		// Don't cross the end of the ring in one read.
		const long at = (ring_write % RING_FRAMES);
		if(n > (RING_FRAMES - at)) {
			n = (RING_FRAMES - at);
		}
		size_t got;
		{
			fs::IoLock io;
			fseek(song_fp, (song_data_offset + (song_pos * 4)), SEEK_SET);
			got = fread(&ring[at * 2], 4, n, song_fp);
		}
		if(got == 0) {
			ring_eof = true;
			return;
		}
		song_pos += got;
		ring_write += got;
	}
}

uint8_t reader_stack[0x4000] __attribute__((aligned(16)));

void reader_thread(void *)
{
	for(;;) {
		WaitSema(reader_wake);

		WaitSema(lock);
		const bool load = req_load;
		const bool restart = req_restart;
		char name[32];
		strcpy(name, req_name);
		req_load = false;
		req_restart = false;
		SignalSema(lock);

		if(load) {
			song_open(name);
		}
		if(load || restart) {
			// Flush: the mixer sees an empty ring until new data arrives.
			ring_eof = false;
			ring_read = ring_write;
			song_pos = 0;
		}
		reader_fill();
	}
}

/// Sound effects
/// -------------
// Pre-rendered from YUME.EFC (ps2port/tools/efc2m.py + pmd2wav) and kept in
// memory. Like PMD's single FM effect channel, a new effect replaces the one
// that is playing.
struct Effect {
	int16_t *pcm = nullptr; // stereo frames
	long frames = 0;
};
Effect effects[SE_SLOTS];
volatile int se_request = -1;
int se_cur = -1;
long se_pos = 0;

void effects_load()
{
	fs::IoLock io;
	int loaded = 0;
	for(int n = 0; n < SE_SLOTS; n++) {
		char path[256];
		fs::path(path, sizeof(path), "audio/se/SE%02d.WAV", n);
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
/// -------------

/// Mixer thread
/// ------------
int16_t mixbuf[CHUNK_FRAMES * 2];
int fade_speed = 0;
int fade_value = 0;
long fade_us = 0;

void mixer_pass()
{
	memset(mixbuf, 0, sizeof(mixbuf));

	WaitSema(lock);
	if(req_stop) {
		req_stop = false;
		playing = false;
	}
	if(req_fade_reset) {
		req_fade_reset = false;
		fade_speed = 0;
		fade_value = 0;
		fade_us = 0;
	}
	if(req_fade >= 0) {
		fade_speed = req_fade;
		req_fade = -1;
	}
	SignalSema(lock);

	if(playing) {
		const long avail = (ring_write - ring_read);
		const long n = ((avail < CHUNK_FRAMES) ? avail : CHUNK_FRAMES);
		for(long i = 0; i < n; i++) {
			const long at = ((ring_read + i) % RING_FRAMES);
			mixbuf[(i * 2) + 0] = ring[(at * 2) + 0];
			mixbuf[(i * 2) + 1] = ring[(at * 2) + 1];
		}
		ring_read += n;
		if((n < CHUNK_FRAMES) && ring_eof && (ring_read == ring_write)) {
			playing = false; // non-looping song finished
		}

		// PMD fade, applied as a linear gain on the music.
		if(fade_speed) {
			fade_us += ((CHUNK_FRAMES * 1000000LL) / RATE);
			while(fade_us >= FADE_STEP_US) {
				fade_us -= FADE_STEP_US;
				if((fade_value + fade_speed) < 256) {
					fade_value += fade_speed;
				} else {
					fade_value = 255;
					fade_speed = 0;
					playing = false;
				}
			}
		}
		if(fade_value) {
			const int gain = (256 - fade_value);
			for(int i = 0; i < (CHUNK_FRAMES * 2); i++) {
				mixbuf[i] = int16_t((mixbuf[i] * gain) >> 8);
			}
		}
	}
	// Wake the reader whenever there's room for another read.
	if((RING_FRAMES - (ring_write - ring_read)) >= READ_FRAMES) {
		SignalSema(reader_wake);
	}
	fade_value_shared = uint8_t(fade_value);

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
			const int v = (mixbuf[i] + src[i]);
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

uint8_t mixer_stack[0x4000] __attribute__((aligned(16)));

void mixer_thread(void *)
{
	for(;;) {
		mixer_pass();
	}
}
/// ------------

bool start_thread(void (*func)(void *), uint8_t *stack, int stack_size, int prio)
{
	ee_thread_t th = {};
	th.func = reinterpret_cast<void *>(func);
	th.stack = stack;
	th.stack_size = stack_size;
	th.gp_reg = &_gp;
	th.initial_priority = prio;
	const int id = CreateThread(&th);
	if(id < 0) {
		return false;
	}
	StartThread(id, nullptr);
	return true;
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
	sema.init_count = 0;
	sema.max_count = 1;
	reader_wake = CreateSema(&sema);

	// Mixer above video (20) and the game (64); the reader below video, since
	// it mostly waits for the disk.
	if(!start_thread(mixer_thread, mixer_stack, sizeof(mixer_stack), 16) ||
	   !start_thread(reader_thread, reader_stack, sizeof(reader_stack), 24)) {
		printf("audio: CreateThread failed\n");
		return false;
	}
	ready = true;
	return true;
}

void song_load(const char *pmd_fn)
{
	if(!ready) {
		return;
	}
	WaitSema(lock);
	strncpy(req_name, pmd_fn, (sizeof(req_name) - 1));
	req_load = true;
	playing = false;
	SignalSema(lock);
	SignalSema(reader_wake);
}

void song_play()
{
	if(!ready) {
		return;
	}
	WaitSema(lock);
	req_restart = true;
	req_fade_reset = true;
	playing = true;
	SignalSema(lock);
	SignalSema(reader_wake);
}

void song_stop()
{
	if(!ready) {
		return;
	}
	WaitSema(lock);
	req_stop = true;
	SignalSema(lock);
}

void song_fade(uint8_t speed)
{
	if(!ready) {
		return;
	}
	WaitSema(lock);
	req_fade = speed;
	SignalSema(lock);
}

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
