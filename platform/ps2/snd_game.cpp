// PS2 port: the game's sound driver interface (th02/snd/snd.h), implemented on
// top of platform/ps2/audio instead of the PMD driver's interrupt.
//
// Replaces, for the PS2 build: th02/snd/{detmode.c,pmd_res.c,kajaint.cpp,
// load.cpp,delayvol.c}, th03/snd/delaymea.cpp, and the flag variables from
// th02/snd/snd[bss].asm and th02/snd/data.c.

#include "th03/snd/snd.h"
#include "th02/hardware/frmdelay.h"
#include "platform/ps2/audio.hpp"
#include <stdio.h>

extern "C" {

char snd_interrupt_if_midi;
bool snd_midi_possible = false;
bool snd_active = false;
bool snd_midi_active = false;
bool snd_fm_possible = false;

// "PMD is resident": the PS2 port always has its music player.
bool16 snd_pmd_resident(void)
{
	return true;
}

// Same outcome as the original with an OPNA-compatible PMD driver loaded:
// FM is possible, and BGM becomes active.
bool16 snd_determine_mode(void)
{
	snd_fm_possible = true;
	snd_active = true;
	return true;
}

int16_t DEFCONV snd_kaja_interrupt(int16_t ax)
{
	if(!snd_bgm_active()) {
		return ax;
	}
	const uint8_t func = (uint16_t(ax) >> 8);
	const uint8_t param = (ax & 0xFF);
	switch(func) {
	case KAJA_SONG_PLAY:
		audio::song_play();
		break;
	case KAJA_SONG_STOP:
		audio::song_stop();
		break;
	case KAJA_SONG_FADE:
		audio::song_fade(param);
		break;
	case KAJA_GET_VOLUME:
		return audio::song_fade_value();
	case KAJA_GET_SONG_MEASURE:
		return audio::song_measure();
	default:
		printf("snd: unhandled KAJA function %02X\n", func);
		break;
	}
	return 0;
}

void snd_load(const char fn[PF_FN_LEN], snd_load_func_t func)
{
	if(func == SND_LOAD_SONG) {
		audio::song_load(fn);
	}
	// SND_LOAD_SE: the effect bank (YUME.EFC) is pre-rendered instead.
}

void snd_delay_until_volume(uint8_t volume)
{
	// The original busy-waits on the driver. Here, the mixer thread updates
	// the fade value while we sleep a frame at a time.
	while(audio::song_fade_value() != volume) {
		frame_delay(1);
	}
}

void pascal snd_delay_until_measure(int measure, unsigned int frames_if_no_bgm)
{
	if(!snd_bgm_active() || (audio::song_measure() < 0)) {
		// Measure tracking isn't available yet; fall back to the original's
		// no-BGM delay.
		frame_delay(frames_if_no_bgm);
		return;
	}
	while(audio::song_measure() < measure) {
		frame_delay(1);
	}
}

} // extern "C"
