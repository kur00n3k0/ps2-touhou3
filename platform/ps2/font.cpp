// PS2 port: PC-98 character generator (font ROM and gaiji RAM).
//
// Replaces platform/x86real/pc98/font.cpp, which asks the PC-98 BIOS. Glyphs
// come from PC98FONT.BIN in the game folder, converted from a Neko Project
// font bitmap by tools/fontconv.py:
//   0x00000  256 × 16 bytes   ANK 8×16
//   0x01000  128 × 128 × 32   kanji [hi & 0x7F][lo & 0x7F], 16 × (left, right)
// User-defined glyphs (gaiji) live in pc98::gaiji, as on the real hardware.

#include "platform/x86real/pc98/font.hpp"
#include "platform/ps2/fs.hpp"
#include "platform/ps2/pc98hw.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
enum {
	ANK_OFFSET = 0x0000,
	KANJI_OFFSET = 0x1000,
	FONT_SIZE = (KANJI_OFFSET + (128 * 128 * 32)),
};
uint8_t *font = nullptr;

const uint8_t* font_data()
{
	if(!font) {
		fs::IoLock io;
		font = static_cast<uint8_t *>(calloc(1, FONT_SIZE));
		char path[256];
		fs::path(path, sizeof(path), "PC98FONT.BIN");
		FILE *fp = fopen(path, "rb");
		if(!fp || (fread(font, 1, FONT_SIZE, fp) != FONT_SIZE)) {
			printf("font: could not read %s, text will be blank\n", path);
		}
		if(fp) {
			fclose(fp);
		}
	}
	return font;
}

// Gaiji IDs are mapped to JIS rows 0x76 and 0x77 (see font.hpp).
bool jis_to_gaiji(jis_t jis, uint8_t& id)
{
	const uint8_t hi = ((jis >> 8) & 0x7F);
	if((hi != 0x76) && (hi != 0x77)) {
		return false;
	}
	id = ((jis & 0x7F) | ((hi == 0x77) ? 0x80 : 0x00));
	return true;
}
}

// Called at startup, so the video thread never has to touch the disk.
void font_preload(void)
{
	font_data();
}

void font_read(font_glyph_t& glyph, jis_t jis)
{
	const uint8_t *f = font_data();
	if(jis < 0x100) {
		// 8×8 ANK. The Neko Project bitmap has no 8×8 set, so take every
		// other row of the 8×16 glyph.
		glyph.tag.w = GLYPH_HALF_W;
		glyph.tag.h = GLYPH_HALF_H;
		for(int y = 0; y < GLYPH_HALF_H; y++) {
			glyph.ank_8x8[y] = f[ANK_OFFSET + (jis * 16) + (y * 2)];
		}
		return;
	}
	if((jis & 0xFF00) == 0x8000) {
		glyph.tag.w = GLYPH_HALF_W;
		glyph.tag.h = GLYPH_H;
		const uint8_t c = (jis & 0xFF);
		for(int y = 0; y < GLYPH_H; y++) {
			glyph.ank_8x16[y] = f[ANK_OFFSET + (c * 16) + y];
		}
		return;
	}

	const uint8_t *src;
	uint8_t id;
	if(jis_to_gaiji(jis, id)) {
		src = pc98::gaiji[id];
	} else {
		const unsigned cell = ((((jis >> 8) & 0x7F) << 7) | (jis & 0x7F));
		src = &f[KANJI_OFFSET + (cell * 32)];
	}

	// JIS rows 0x29-0x2B hold halfwidth glyphs in the left half of the cell.
	const uint8_t hi = ((jis >> 8) & 0x7F);
	if((hi >= 0x29) && (hi <= 0x2B)) {
		glyph.tag.w = GLYPH_HALF_W;
		glyph.tag.h = GLYPH_H;
		for(int y = 0; y < GLYPH_H; y++) {
			glyph.ank_8x16[y] = src[y * 2];
		}
		return;
	}
	glyph.tag.w = GLYPH_FULL_W;
	glyph.tag.h = GLYPH_H;
	for(int y = 0; y < GLYPH_H; y++) {
		// Same byte order as the BIOS buffer: left byte first, i.e. in the
		// low byte of the little-endian 16-bit row.
		glyph.kanji[y] = (src[y * 2] | (src[(y * 2) + 1] << 8));
	}
}

void font_read(font_glyph_ank_8x8_t& glyph, ank_t ank)
{
	font_read(reinterpret_cast<font_glyph_t &>(glyph), static_cast<jis_t>(ank));
}

void font_read(font_glyph_ank_8x16_t& glyph, ank_t ank)
{
	font_read(reinterpret_cast<font_glyph_t &>(glyph), (0x8000 + ank));
}

void font_gaiji_write(
	const dot_rect_t(GLYPH_FULL_W, GLYPH_H)* gaiji,
	uint16_t count,
	uint8_t first_id
)
{
	for(uint16_t i = 0; i < count; i++) {
		uint8_t *dst = pc98::gaiji[uint8_t(first_id + i)];
		for(int y = 0; y < GLYPH_H; y++) {
			const dots16_t row = gaiji[i][y];
			dst[(y * 2) + 0] = (row & 0xFF);
			dst[(y * 2) + 1] = (row >> 8);
		}
	}
}
