// PS2 port: program entry.
//
// Replaces three DOS-side pieces:
// • GAME.BAT / main_setup(): there are no TSRs to spawn on the PS2.
// • ZUN.COM -5 (th03/res_yume.cpp): creates the resident structure and writes
//   its address into YUME.CFG, where cfg_load() picks it up.
// • execl()-based switching between the OP and MAINL entry points: the entry
//   loop below restarts at the requested entry point instead.

#include "th03/resident.hpp"
#include "th03/formats/cfg.hpp"
#include "th01/rank.h"
#include "th02/snd/snd.h"
#include "th01/pc98/entry.hpp"
#include "libs/master.lib/master.hpp"
#include "libs/master.lib/pc98_gfx.hpp"
#include "platform/vblank.hpp"
#include "platform/ps2/audio.hpp"
#include "platform/ps2/fs.hpp"
#include "platform/ps2/pad.hpp"
#include "platform/ps2/pc98hw.hpp"
#include "platform/ps2/video.hpp"
#include <iopcontrol.h>
#include <kernel.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <setjmp.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE* ps2_game_fopen(const char *fn, const char *mode);
void font_preload(void);

extern "C" {
// Embedded IOP modules (see Makefile.ps2).
extern unsigned char sio2man_irx[];
extern unsigned int size_sio2man_irx;
extern unsigned char padman_irx[];
extern unsigned int size_padman_irx;
extern unsigned char libsd_irx[];
extern unsigned int size_libsd_irx;
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
}

namespace {

void load_iop_modules()
{
	SifInitRpc(0);
	// Start from a clean IOP, then load the SDK's own controller and sound
	// modules, which match the EE-side libraries we link.
	while(!SifIopReset("", 0)) {
	}
	while(!SifIopSync()) {
	}
	SifInitRpc(0);
	sbv_patch_enable_lmb();
	sbv_patch_disable_prefix_check();

	struct Module {
		const char *name;
		unsigned char *data;
		unsigned int size;
	};
	const Module modules[] = {
		{ "sio2man", sio2man_irx, size_sio2man_irx },
		{ "padman", padman_irx, size_padman_irx },
		{ "libsd", libsd_irx, size_libsd_irx },
		{ "audsrv", audsrv_irx, size_audsrv_irx },
	};
	for(const Module& m : modules) {
		int ret = 0;
		const int id = SifExecModuleBuffer(m.data, m.size, 0, nullptr, &ret);
		if(id < 0) {
			printf("main: failed to load %s (%d)\n", m.name, id);
		}
	}
}

// ZUN.COM -5: create the resident structure, then write the default options
// (if YUME.CFG doesn't exist yet), the structure's address and the debug flag
// to YUME.CFG, like th02/formats/cfg_init.c.
void resident_create()
{
	fs::IoLock io;
	resident = static_cast<resident_t *>(calloc(1, sizeof(resident_t)));
	strcpy(resident->id, RES_ID);

	cfg_t cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.opts.bgm_mode = SND_BGM_FM;
	cfg.opts.key_mode = KM_KEY_KEY;
	cfg.opts.rank = RANK_NORMAL;
	if(FILE *fp = ps2_game_fopen(CFG_FN_CAPS, "rb")) {
		fread(&cfg.opts, sizeof(cfg.opts), 1, fp);
		fclose(fp);
	}
	cfg.resident = resident;
	cfg.debug = 0;
	if(FILE *fp = ps2_game_fopen(CFG_FN_CAPS, "wb")) {
		fwrite(&cfg, sizeof(cfg), 1, fp);
		fclose(fp);
	} else {
		printf("main: can't write %s\n", CFG_FN_CAPS);
	}
}

jmp_buf entry_jmp;
volatile entrypoint_t next_entry = EP_OP;

const char *ARGV_OP[] = { "op", nullptr };

} // namespace

// Restarts the "binary" at the given entry point.
void entrypoint_exec(entrypoint_t entrypoint)
{
	next_entry = entrypoint;
	longjmp(entry_jmp, 1);
}

// Waits for any button. Used by error paths in the original code.
int getch(void)
{
	while(pad::held(0)) {
		vsync_wait();
	}
	while(!pad::held(0)) {
		vsync_wait();
	}
	return '\r';
}

// VBLANK helpers (platform/x86real/pc98/vblank.cpp)
bool vblank_in(void)
{
	return false;
}

void vblank_run(func_t func)
{
	video::run_on_next_vsync(func);
}

void vblank_flush_func_queue(void)
{
	video::run_on_next_vsync(nullptr);
}

void vblank_palette_black_and_tram_wipe(void)
{
	palette_settone(0);
	text_clear();
}

int main(int argc, char *argv[])
{
	// The game's main thread runs below the video and audio threads, so that
	// those still get to run while the game busy-waits for VSync.
	ChangeThreadPriority(GetThreadId(), 64);

	load_iop_modules();
	fs::init((argc > 0) ? argv[0] : nullptr);
	font_preload();
	if(!video::init()) {
		printf("main: no video\n");
		SleepThread();
	}
	pad::init();
	audio::init();

	resident_create();

	setjmp(entry_jmp);
	for(;;) {
		switch(next_entry) {
		case EP_OP:
			main_op(1, ARGV_OP);
			// Quitting from the main menu: there's no DOS to return to.
			printf("main: game exited\n");
			SleepThread();
			break;
		default:
			// MAINL (cutscenes, endings, high scores) and MAIN (gameplay) are
			// not ported yet. Go back to the title screen.
			printf("main: entry point %d is not ported yet\n", int(next_entry));
			next_entry = EP_OP;
			break;
		}
	}
	return 0;
}
