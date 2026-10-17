// PC-98 single-color drawing
// --------------------------

#include "platform/x86real/pc98/grcg.hpp"

struct GrpMono : public GRCG {
	GrpMono() : GRCG(GC_RMW) {
	}

#if defined(PLATFORM_PS2)
	void draw_point_noclip(const screen_x_t& x, const vram_y_t& y) {
		pc98::vram_write8(
			0, ((y * ROW_SIZE) + (x >> BYTE_BITS)), (0x80 >> (x & BYTE_MASK))
		);
	}
#else
	void __fastcall draw_point_noclip(const screen_x_t& x, const vram_y_t& y) {
		// Neat trick from master.lib: Since all VRAM rows are segment-aligned,
		// we can calculate [y] into the segment rather than the offset, which
		// only requires a multiplication with 5 (([y] << 2) + [y])) rather
		// than 80.
		_pokeb_(
			(SEG_PLANE_B + (y << 2) + y),
			(x >> BYTE_BITS),
			(0x80 >> (x & BYTE_MASK))
		);
	}
#endif
};
