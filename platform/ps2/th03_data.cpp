// PS2 port: TH03 OP/MAINL globals that ReC98 still defines in assembly
// ([data] / [bss] .asm files), with the same initial values.

#include "th03/common.h"
#include "th03/formats/cdg.h"
#include "th03/formats/scoredat.hpp"
#include "th03/hardware/input.h"

// th03/formats/cfg_lres[data].asm
extern const char CFG_FN[] = "YUME.CFG";

// th03/formats/cdg[bss].asm, cdg[data].asm
CDG cdg_slots[CDG_SLOT_COUNT];
bool cdg_noalpha = false;

// th03/hardware/input[bss].asm
input_t input_mp_p1;
input_t input_mp_p2;
input_t input_sp;

// th03/hiscore/regist[bss].asm
scoredat_section_t hi;

extern "C" {
// th03/snd/se_state[data].asm
unsigned char snd_se_playing = static_cast<unsigned char>(-1);
unsigned char snd_se_frame = 0;

// th03/snd/se_priority[data].asm
unsigned char snd_se_priorities[33] = {
	0, 0, 32, 16, 2, 18, 18, 18, 64, 16, 17, 2, 17, 32, 18, 18, 18, 18, 32, 32,
	18, 18, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
unsigned char snd_se_priority_frames[33] = {
	0, 0, 36, 16, 4, 16, 8, 10, 48, 80, 24, 17, 11, 4, 80, 16, 48, 74, 50, 32,
	24, 68, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};
}
