// PS2 port: software model of the PC-98 display hardware. See pc98hw.hpp.

#include "platform/ps2/pc98hw.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace pc98 {

Page vram[PAGES] __attribute__((aligned(64)));
int page_shown = 0;
int page_accessed = 0;

uint8_t grcg_mode = GRCG_OFF;
uint8_t grcg_tile[PLANES];

uint8_t palette[16][3];
bool palette_dirty = true;

uint8_t tram_code[TRAM_BYTES];
uint8_t tram_atrb[TRAM_BYTES];
uint8_t gaiji[GAIJI_COUNT][32];
bool text_shown = true;

volatile unsigned int vsync_count1 = 0, vsync_count2 = 0;

// Real-mode segment bases of the video memory windows.
enum : uint32_t {
	LIN_TRAM_CODE = (0xA000u << 4),
	LIN_TRAM_ATRB = (0xA200u << 4),
	LIN_PLANE_B = (0xA800u << 4),
	LIN_PLANE_R = (0xB000u << 4),
	LIN_PLANE_G = (0xB800u << 4),
	LIN_PLANE_E = (0xE000u << 4),
	WINDOW = 0x8000u, // each graphics window is 32 KiB
};

uint8_t* mem(uint16_t seg, uint32_t off)
{
	const uint32_t lin = ((uint32_t(seg) << 4) + off);
	if((lin >= LIN_TRAM_CODE) && (lin < (LIN_TRAM_CODE + TRAM_BYTES))) {
		return &tram_code[lin - LIN_TRAM_CODE];
	}
	if((lin >= LIN_TRAM_ATRB) && (lin < (LIN_TRAM_ATRB + TRAM_BYTES))) {
		return &tram_atrb[lin - LIN_TRAM_ATRB];
	}
	static const uint32_t bases[PLANES] = {
		LIN_PLANE_B, LIN_PLANE_R, LIN_PLANE_G, LIN_PLANE_E
	};
	for(int p = 0; p < PLANES; p++) {
		if((lin >= bases[p]) && (lin < (bases[p] + WINDOW))) {
			const uint32_t o = (lin - bases[p]);
			// The window is 32 KiB, but only the first 32,000 bytes are
			// displayed. Accesses past that land in the padding.
			return &vram[page_accessed].plane[p][(o < PLANE_ALLOC) ? o : (PLANE_ALLOC - 1)];
		}
	}
	printf("pc98::mem: unmapped real-mode address %04X:%04X\n", seg, unsigned(off));
	abort();
}

void vram_write8(int plane, uint32_t offset, uint8_t bits)
{
	if(offset >= PLANE_BYTES) {
		return;
	}
	Page& pg = vram[page_accessed];
	if(grcg_mode & 0x80) {
		// TDW and RMW: every enabled plane gets the tile's bits where [bits]
		// is set and keeps its own bits elsewhere. (In TDW mode, the real
		// hardware ignores the written value and writes the full tile, but
		// master.lib only ever writes 0xFF in that mode.)
		const uint8_t mask = ((grcg_mode & 0x40) ? bits : 0xFF);
		for(int p = 0; p < PLANES; p++) {
			if(grcg_mode & (1 << p)) {
				continue; // plane disabled
			}
			uint8_t& v = pg.plane[p][offset];
			v = ((v & ~mask) | (grcg_tile[p] & mask));
		}
		return;
	}
	pg.plane[plane][offset] = bits;
}

uint8_t vram_read8(int plane, uint32_t offset)
{
	if(offset >= PLANE_BYTES) {
		return 0;
	}
	const Page& pg = vram[page_accessed];
	if((grcg_mode & 0xC0) == 0x80) {
		// TCR: tile compare read
		uint8_t ret = 0xFF;
		for(int p = 0; p < PLANES; p++) {
			if(grcg_mode & (1 << p)) {
				continue;
			}
			ret &= ~(pg.plane[p][offset] ^ grcg_tile[p]);
		}
		return ret;
	}
	return pg.plane[plane][offset];
}

void grcg_setcolor(int mode, int col)
{
	grcg_mode = uint8_t(mode);
	for(int p = 0; p < PLANES; p++) {
		grcg_tile[p] = ((col & (1 << p)) ? 0xFF : 0x00);
	}
}

void grcg_off()
{
	grcg_mode = GRCG_OFF;
}

} // namespace pc98
