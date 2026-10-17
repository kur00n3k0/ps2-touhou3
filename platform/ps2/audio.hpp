// PS2 port: music and sound effect playback.
//
// The PC-98 games drive the PMD sound driver, which synthesizes music on the
// YM2608 FM chip in real time. This port plays pre-rendered recordings of the
// same songs instead (see tools/pmd2wav): one 44.1 kHz stereo WAV per .M file,
// with a loop point, streamed from disk by a mixer thread.

#ifndef PLATFORM_PS2_AUDIO_HPP
#define PLATFORM_PS2_AUDIO_HPP

#include <stdint.h>

namespace audio {

// Starts the IOP audio modules and the mixer thread. Returns false if audio
// is unavailable; all other functions are then no-ops.
bool init();

// Selects the song that [play] starts, by its original file name ("op.m").
// The recording is looked up as audio/<NAME>.wav next to the game data.
void song_load(const char *pmd_fn);
void song_play();
void song_stop();
// PMD-style fade: [speed] is added to the fade value every 8 ticks of the
// 9.216 ms OPN timer A; playback stops once the value reaches 255.
void song_fade(uint8_t speed);
// Current PMD fade value (0 = full volume, 255 = silent).
uint8_t song_fade_value();
// Number of full measures played since the song started, or -1 if unknown.
int song_measure();

// Sound effects: [se] is the PMD effect number from YUME.EFC.
void se_play(int se);

} // namespace audio

#endif /* PLATFORM_PS2_AUDIO_HPP */
