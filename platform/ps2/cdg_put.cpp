// PS2 port: CDG blitters and the horizontal flip table, replacing
// th03/formats/cdg_put.asm, th03/cdg_p_na.asm and th03/formats/hfliplut.asm.
//
// CDG image data: one alpha plane, then the B, R, G and E planes, each
// [bitplane_size] bytes. Rows are (vram_dword_w * 4) bytes wide and stored
// bottom-up: the first stored row is the bottom line of the image. All three
// blitters draw to (⌊left/8⌋*8, top) on the accessed page.

#include "th03/formats/cdg.h"
#include "platform/ps2/pc98hw.hpp"
#include <stdio.h>

extern "C" {

uint8_t hflip_lut[256];

void hflip_lut_generate(void)
{
	for(int i = 0; i < 256; i++) {
		uint8_t r = 0;
		for(int b = 0; b < 8; b++) {
			if(i & (1 << b)) {
				r |= (0x80 >> b);
			}
		}
		hflip_lut[i] = r;
	}
}

} // extern "C"

namespace {

enum blit_mode_t {
	BLIT_ALPHA,     // clear the alpha shape to color 0, then OR the colors
	BLIT_NOALPHA,   // copy the colors
};

// Destination offset of the first byte of image row [row] (0 = top).
inline int dst_offset(screen_x_t left, vram_y_t top, int row)
{
	return (((top + row) * ROW_SIZE) + (left >> BYTE_BITS));
}

void cdg_blit(screen_x_t left, vram_y_t top, int slot, blit_mode_t mode, bool hflip)
{
	if((slot < 0) || (slot >= CDG_SLOT_COUNT)) {
		printf("cdg: slot %d out of range\n", slot);
		return;
	}
	CDG& cdg = cdg_slots[slot];
	if(!cdg.seg_colors()) {
		return;
	}
	// Anything below the start of our program can't be a heap allocation.
	auto bogus = [](const void *p) {
		return (p && (reinterpret_cast<uintptr_t>(p) < 0x100000));
	};
	if(bogus(cdg.seg_colors()) || bogus(cdg.seg_alpha())) {
		printf(
			"cdg: slot %d has invalid data pointers (alpha %p, colors %p, "
			"%dx%d, bitplane_size %u, count %u)\n",
			slot, (void *)cdg.seg_alpha(), (void *)cdg.seg_colors(),
			cdg.pixel_w, cdg.pixel_h, unsigned(cdg.bitplane_size),
			unsigned(cdg.image_count)
		);
		return;
	}
	const int wbytes = (cdg.vram_dword_w * 4);
	const int h = cdg.pixel_h;
	pc98::Page& pg = pc98::vram[pc98::page_accessed];

	// Source byte [x] of stored row [srow], optionally mirrored.
	auto src_byte = [&](const dots8_t *plane, int srow, int x) -> uint8_t {
		if(hflip) {
			return hflip_lut[plane[(srow * wbytes) + (wbytes - 1 - x)]];
		}
		return plane[(srow * wbytes) + x];
	};

	for(int srow = 0; srow < h; srow++) {
		// Stored bottom-up.
		const int row = ((h - 1) - srow);
		const int vy = (top + row);
		if((vy < 0) || (vy >= RES_Y)) {
			continue; // The original wraps around; we clip instead.
		}
		const int base = dst_offset(left, top, row);
		for(int x = 0; x < wbytes; x++) {
			const int vx = ((left >> BYTE_BITS) + x);
			if((vx < 0) || (vx >= ROW_SIZE)) {
				continue;
			}
			const int off = (base + x);
			if(mode == BLIT_ALPHA) {
				const dots8_t *alpha = cdg.seg_alpha();
				const uint8_t a = (alpha ? src_byte(alpha, srow, x) : 0);
				for(int p = 0; p < PLANE_COUNT; p++) {
					const dots8_t *plane = (cdg.seg_colors() + (p * cdg.bitplane_size));
					uint8_t& v = pg.plane[p][off];
					v = ((v & ~a) | src_byte(plane, srow, x));
				}
			} else {
				for(int p = 0; p < PLANE_COUNT; p++) {
					const dots8_t *plane = (cdg.seg_colors() + (p * cdg.bitplane_size));
					pg.plane[p][off] = src_byte(plane, srow, x);
				}
			}
		}
	}
}

} // namespace

extern "C" {

void pascal cdg_put_8(screen_x_t left, vram_y_t top, int slot)
{
	cdg_blit(left, top, slot, BLIT_ALPHA, false);
}

void pascal cdg_put_hflip_8(screen_x_t left, vram_y_t top, int slot)
{
	cdg_blit(left, top, slot, BLIT_ALPHA, true);
}

void pascal cdg_put_noalpha_8(screen_x_t left, vram_y_t top, int slot)
{
	cdg_blit(left, top, slot, BLIT_NOALPHA, false);
}

} // extern "C"
