// PS2 port: decoder for the PI image format (Yanagisawa's 16-color PI), used
// for TH03's backgrounds and pictures. Portable C++ with no PS2 dependencies,
// so it can also be tested on the host (port/pi_test.cpp).
//
// Follows piloadm.asm (piloadc by 電脳空間 / Mr. Sakai, as included in ReC98)
// rather than the format documentation, to decode exactly like the original.

#ifndef PLATFORM_PS2_PI_DECODE_HPP
#define PLATFORM_PS2_PI_DECODE_HPP

#include <stddef.h>
#include <stdint.h>

struct PiImage {
	int w = 0;
	int h = 0;               // after line skipping
	uint8_t palette[16][3];  // 8-bit R, G, B
	bool has_palette = false;
	uint8_t *pixels = nullptr; // w × h color indices, malloc'd
};

// Decodes [data] (a whole .PI file). With [lineskip], only every other line
// is kept, halving the height (piloadc's PILOAD_OPT_LINESKIP).
// Returns false on malformed input.
bool pi_decode(const uint8_t *data, size_t size, bool lineskip, PiImage &out);

#endif /* PLATFORM_PS2_PI_DECODE_HPP */
