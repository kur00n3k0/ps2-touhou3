// PS2 port: software model of the PC-98 display hardware that ZUN's code and
// master.lib draw into.
//
// What is modeled:
// • Graphics VRAM: 2 pages × 4 bitplanes (B, R, G, E) of 640×400 at 1bpp.
//   Bit 7 of each byte is the leftmost pixel, like on the PC-98.
// • Which page is shown and which page the CPU writes to.
// • The GRCG ("graphic charger"), which turns a single write into a
//   tile-colored write to all four planes.
// • The 16-color analog palette, 4 bits per component.
// • Text RAM (80×25 cells of character codes and attributes) and the 256
//   user-definable 16×16 "gaiji" glyphs.
//
// This replaces hardware, not ZUN's logic: the game code above it is the
// original (decompiled) code, and the result of each frame is turned into a
// texture by platform/ps2/video.cpp.

#ifndef PLATFORM_PS2_PC98HW_HPP
#define PLATFORM_PS2_PC98HW_HPP

#include <stdint.h>

// Headers may include this one from inside an extern "C" block.
extern "C++" {

// Unaligned-safe access. The x86 allows 16/32-bit reads and writes at any
// address; the Emotion Engine raises an address error. Every multi-byte access
// to emulated memory goes through these types.
typedef uint16_t __attribute__((aligned(1), may_alias)) u16_unaligned;
typedef uint32_t __attribute__((aligned(1), may_alias)) u32_unaligned;
typedef int16_t __attribute__((aligned(1), may_alias)) s16_unaligned;
typedef int32_t __attribute__((aligned(1), may_alias)) s32_unaligned;

namespace pc98 {

enum {
	ROW_BYTES = 80,
	VRAM_W = 640,
	VRAM_H = 400,
	PLANE_BYTES = (ROW_BYTES * VRAM_H),
	PLANES = 4,
	PAGES = 2,
	TRAM_W = 80,
	TRAM_H = 25,
	GAIJI_COUNT = 256,
};

// Planes are padded at the end, so that 16/32-bit accesses that start inside
// the last bytes of a plane do not run into the next one.
enum { PLANE_ALLOC = (PLANE_BYTES + 64) };

struct Page {
	uint8_t plane[PLANES][PLANE_ALLOC];
};

extern Page vram[PAGES];
extern int page_shown;
extern int page_accessed;

// GRCG
// ----
enum {
	GRCG_OFF = 0x00,
	GRCG_TDW = 0x80, // tile data write: tile replaces masked bits
	GRCG_RMW = 0xC0, // read-modify-write: same visible result for our purposes
};
extern uint8_t grcg_mode;     // GRCG_* | plane disable mask (bits 0-3)
extern uint8_t grcg_tile[PLANES];

// Write [bits] (MSB = leftmost) at [offset] of the accessed page, applying
// the GRCG if active, otherwise writing plane [plane] only.
void vram_write8(int plane, uint32_t offset, uint8_t bits);
// Reads in GRCG tile compare mode: a bit is set where the pixel equals the
// tile color in all enabled planes. With the GRCG off, reads plane [plane].
uint8_t vram_read8(int plane, uint32_t offset);

void grcg_setcolor(int mode, int col);
void grcg_off();
// ----

// Palette (0..15 per component)
// -------
extern uint8_t palette[16][3];
// Set whenever the palette changes, so the video backend re-uploads it.
extern bool palette_dirty;
// -------

// Text RAM
// --------
// Raw PC-98 text RAM layout: at segment A000, 2 bytes per cell of character
// code (JIS or gaiji); at A200, 2 bytes per cell of attributes (low byte).
enum { TRAM_BYTES = 0x2000 };
extern uint8_t tram_code[TRAM_BYTES];
extern uint8_t tram_atrb[TRAM_BYTES];
// 16×16 user glyphs, 2 bytes per row (left 8 dots, right 8 dots), MSB =
// leftmost dot. Same layout as the kanji in PC98FONT.BIN.
extern uint8_t gaiji[GAIJI_COUNT][32];
// Whether the text layer is visible at all.
extern bool text_shown;
// --------

// Maps a real-mode segment:offset address inside the PC-98's video memory
// windows (A000-A3FF text RAM, A800-BFFF and E000-E7FF graphics planes) to
// emulated memory. Graphics planes map to the currently accessed page.
// Any other address is a porting bug, and aborts with a message.
uint8_t* mem(uint16_t seg, uint32_t off);

// Called once per emulated VSync by the video backend.
extern volatile unsigned int vsync_count1, vsync_count2;

} // namespace pc98

// Real-mode pointer helpers used throughout ReC98, mapped onto pc98::mem().
inline void* MK_FP(uint16_t seg, uint32_t off) { return pc98::mem(seg, off); }
inline uint8_t peekb(uint16_t seg, uint32_t off) { return *pc98::mem(seg, off); }
inline void pokeb(uint16_t seg, uint32_t off, uint8_t v) { *pc98::mem(seg, off) = v; }
inline uint16_t peek(uint16_t seg, uint32_t off) {
	return *reinterpret_cast<u16_unaligned *>(pc98::mem(seg, off));
}
inline void poke(uint16_t seg, uint32_t off, uint16_t v) {
	*reinterpret_cast<u16_unaligned *>(pc98::mem(seg, off)) = v;
}
#define _pokeb_ pokeb
#define _peekb_ peekb

} // extern "C++"

#endif /* PLATFORM_PS2_PC98HW_HPP */
