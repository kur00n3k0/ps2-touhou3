// PS2 port: display output and VSync timing. See video.hpp.

#include "platform/ps2/video.hpp"
#include "platform/ps2/pc98hw.hpp"
#include "platform/x86real/pc98/font.hpp"
#include "libs/master.lib/master.hpp"
#include <dmaKit.h>
#include <gsKit.h>
#include <kernel.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

extern func_t vsync_Proc;

namespace video {

namespace {

enum {
	TEX_W = 640,
	TEX_H = 400,
	SCREEN_H = 448,
	TEXT_CLUT_BASE = 16, // 8 digital text colors after the 16 graphics colors
	// PC-98 VSync: 24.823 kHz / 440 lines = 56.42 Hz; NTSC: 59.94 Hz.
	PC98_HZ_X100 = 5642,
	TV_HZ_X100 = 5994,
};

GSGLOBAL *gs = nullptr;
GSTEXTURE tex;
uint8_t *pixels = nullptr;  // TEX_W × TEX_H indices
uint32_t *clut = nullptr;   // 256 entries, CSM1-swizzled

int vblank_sema = -1;
void (*volatile pending_func)(void) = nullptr;
uint64_t plane_lut[256];    // byte → 8 one-bit-per-pixel bytes

int vblank_handler(int)
{
	iSignalSema(vblank_sema);
	ExitHandler();
	return 0;
}

// CSM1 stores 256-entry CLUTs with bits 3 and 4 of the index swapped.
inline int clut_slot(int i)
{
	return ((i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1));
}

void update_clut()
{
	for(int i = 0; i < 16; i++) {
		const uint8_t r = (pc98::palette[i][0] * 17);
		const uint8_t g = (pc98::palette[i][1] * 17);
		const uint8_t b = (pc98::palette[i][2] * 17);
		clut[clut_slot(i)] = (r | (g << 8) | (b << 16) | (0x80u << 24));
	}
	// Text colors: attribute bits 5-7 are B, R, G.
	for(int i = 0; i < 8; i++) {
		const uint8_t b = ((i & 1) ? 0xFF : 0);
		const uint8_t r = ((i & 2) ? 0xFF : 0);
		const uint8_t g = ((i & 4) ? 0xFF : 0);
		clut[clut_slot(TEXT_CLUT_BASE + i)] = (r | (g << 8) | (b << 16) | (0x80u << 24));
	}
}

// Planar → one index per pixel, 8 pixels at a time.
void convert_graphics()
{
	const pc98::Page &pg = pc98::vram[pc98::page_shown];
	uint64_t *dst = reinterpret_cast<uint64_t *>(pixels);
	for(int i = 0; i < pc98::PLANE_BYTES; i++) {
		dst[i] = (
			(plane_lut[pg.plane[0][i]] << 0) |
			(plane_lut[pg.plane[1][i]] << 1) |
			(plane_lut[pg.plane[2][i]] << 2) |
			(plane_lut[pg.plane[3][i]] << 3)
		);
	}
}

// Draws 16 rows of 8 dots from [rows] (MSB = left) at text cell (cx, cy).
void put_text_cell(int cx, int cy, const uint8_t rows[16], uint8_t atrb)
{
	const uint8_t col = (TEXT_CLUT_BASE + (atrb >> 5));
	const bool reverse = (atrb & 0x04);
	const bool underline = (atrb & 0x08);
	for(int y = 0; y < 16; y++) {
		uint8_t bits = rows[y];
		if(underline && (y == 15)) {
			bits = 0xFF;
		}
		if(reverse) {
			bits = ~bits;
		}
		if(!bits) {
			continue;
		}
		uint8_t *dst = &pixels[(((cy * 16) + y) * TEX_W) + (cx * 8)];
		for(int x = 0; x < 8; x++) {
			if(bits & (0x80 >> x)) {
				dst[x] = col;
			}
		}
	}
}

void draw_text_layer()
{
	if(!pc98::text_shown) {
		return;
	}
	for(int cy = 0; cy < pc98::TRAM_H; cy++) {
		for(int cx = 0; cx < pc98::TRAM_W; cx++) {
			const int cell = ((cy * pc98::TRAM_W) + cx);
			const uint8_t atrb = pc98::tram_atrb[cell * 2];
			if(!(atrb & 0x01)) {
				continue; // "secret": not displayed
			}
			const uint8_t lo = pc98::tram_code[(cell * 2) + 0];
			const uint8_t hi = pc98::tram_code[(cell * 2) + 1];
			uint8_t rows[16];
			if(hi == 0) {
				if((lo == ' ') && !(atrb & 0x0C)) {
					continue;
				}
				font_glyph_ank_8x16_t g;
				font_read(g, lo);
				for(int y = 0; y < 16; y++) {
					rows[y] = g.dots[y];
				}
			} else {
				// Two-byte code: low byte = JIS row - 0x20, high byte = JIS
				// column, bit 7 set in the right half's cell.
				const bool right = (hi & 0x80);
				const jis_t jis = (((lo + 0x20) << 8) | (hi & 0x7F));
				font_glyph_t g;
				font_read(g, jis);
				for(int y = 0; y < 16; y++) {
					if(g.tag.w == GLYPH_FULL_W) {
						rows[y] = (right ? (g.kanji[y] >> 8) : (g.kanji[y] & 0xFF));
					} else {
						rows[y] = (right ? 0 : g.ank_8x16[y]);
					}
				}
			}
			put_text_cell(cx, cy, rows, atrb);
		}
	}
}

void present()
{
	convert_graphics();
	draw_text_layer();
	if(pc98::palette_dirty) {
		pc98::palette_dirty = false;
		update_clut();
	}
	FlushCache(0);
	gsKit_texture_upload(gs, &tex);
	const float top = ((SCREEN_H - TEX_H) / 2);
	gsKit_prim_sprite_texture(
		gs, &tex,
		0.0f, top, 0.0f, 0.0f,
		float(TEX_W), (top + TEX_H), float(TEX_W), float(TEX_H),
		2, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00)
	);
	gsKit_queue_exec(gs);
}

uint8_t thread_stack[0x8000] __attribute__((aligned(16)));

void video_thread(void *)
{
	unsigned acc = 0;
	for(;;) {
		WaitSema(vblank_sema);
		// Advance the emulated PC-98 VSync at 56.42 Hz.
		acc += PC98_HZ_X100;
		if(acc < TV_HZ_X100) {
			continue;
		}
		acc -= TV_HZ_X100;

		pc98::vsync_count1++;
		vsync_Count1++;
		vsync_Count2++;
		if(vsync_Proc) {
			vsync_Proc();
		}
		if(void (*f)(void) = pending_func) {
			pending_func = nullptr;
			f();
		}
		present();
	}
}

} // namespace

bool init()
{
	for(int b = 0; b < 256; b++) {
		uint64_t v = 0;
		for(int x = 0; x < 8; x++) {
			if(b & (0x80 >> x)) {
				v |= (uint64_t(1) << (x * 8));
			}
		}
		plane_lut[b] = v;
	}

	gs = gsKit_init_global();
	gs->Mode = GS_MODE_NTSC;
	gs->Interlace = GS_INTERLACED;
	gs->Field = GS_FIELD;
	gs->Width = TEX_W;
	gs->Height = SCREEN_H;
	gs->PSM = GS_PSM_CT32;
	gs->ZBuffering = GS_SETTING_OFF;
	gs->DoubleBuffering = GS_SETTING_OFF;
	gs->PrimAlphaEnable = GS_SETTING_OFF;

	dmaKit_init(
		D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF,
		D_CTRL_RCYC_8, (1 << DMA_CHANNEL_GIF)
	);
	dmaKit_chan_init(DMA_CHANNEL_GIF);
	gsKit_init_screen(gs);
	gsKit_mode_switch(gs, GS_ONESHOT);
	gsKit_clear(gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));

	memset(&tex, 0, sizeof(tex));
	tex.Width = TEX_W;
	tex.Height = TEX_H;
	tex.PSM = GS_PSM_T8;
	tex.ClutPSM = GS_PSM_CT32;
	tex.Filter = GS_FILTER_NEAREST;
	tex.Mem = static_cast<u32 *>(memalign(128, gsKit_texture_size_ee(TEX_W, TEX_H, GS_PSM_T8)));
	tex.Clut = static_cast<u32 *>(memalign(128, (256 * 4)));
	tex.Vram = gsKit_vram_alloc(gs, gsKit_texture_size(TEX_W, TEX_H, GS_PSM_T8), GSKIT_ALLOC_USERBUFFER);
	tex.VramClut = gsKit_vram_alloc(gs, gsKit_texture_size(16, 16, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
	pixels = reinterpret_cast<uint8_t *>(tex.Mem);
	clut = reinterpret_cast<uint32_t *>(tex.Clut);
	memset(pixels, 0, (TEX_W * TEX_H));
	memset(clut, 0, (256 * 4));
	update_clut();

	ee_sema_t sema = {};
	sema.init_count = 0;
	sema.max_count = 1;
	vblank_sema = CreateSema(&sema);

	ee_thread_t th = {};
	th.func = reinterpret_cast<void *>(video_thread);
	th.stack = thread_stack;
	th.stack_size = sizeof(thread_stack);
	th.gp_reg = &_gp;
	th.initial_priority = 20; // above the game, below audio
	const int id = CreateThread(&th);
	if(id < 0) {
		printf("video: CreateThread failed\n");
		return false;
	}
	StartThread(id, nullptr);

	AddIntcHandler(INTC_VBLANK_S, vblank_handler, 0);
	EnableIntc(INTC_VBLANK_S);
	return true;
}

void run_on_next_vsync(void (*func)(void))
{
	pending_func = func;
}

} // namespace video
