// PS2 port: PI image decoder. See pi_decode.hpp.

#include "platform/ps2/pi_decode.hpp"
#include <stdlib.h>
#include <string.h>

namespace {

struct BitReader {
	const uint8_t *p;
	const uint8_t *end;
	uint8_t cur = 0;
	int left = 0;

	int bit() {
		if(left == 0) {
			cur = ((p < end) ? *p++ : 0);
			left = 8;
		}
		left--;
		return ((cur >> left) & 1);
	}
	unsigned bits(int n) {
		unsigned v = 0;
		while(n-- > 0) {
			v = ((v << 1) | bit());
		}
		return v;
	}
};

// Move-to-front color tables, one row per previous color.
struct ColorTable {
	uint8_t t[16][16];

	ColorTable() {
		for(int r = 0; r < 16; r++) {
			for(int i = 0; i < 16; i++) {
				t[r][i] = ((r - i) & 0x0F);
			}
		}
	}

	// piloadm.asm `color`: 10 → 0, 11 → 1, 00x → 2+x, 010xx → 4+xx,
	// 011xxx → 8+xxx.
	uint8_t read(BitReader &br, uint8_t prev) {
		int idx;
		if(br.bit()) {
			idx = br.bit();
		} else if(!br.bit()) {
			idx = (2 + br.bit());
		} else if(!br.bit()) {
			idx = (4 + br.bits(2));
		} else {
			idx = (8 + br.bits(3));
		}
		uint8_t *row = t[prev & 0x0F];
		const uint8_t c = row[idx];
		memmove(&row[1], &row[0], idx);
		row[0] = c;
		return c;
	}
};

// Length of a copy, in pixel pairs.
unsigned read_length(BitReader &br)
{
	if(!br.bit()) {
		return 1;
	}
	int n = 1;
	while(br.bit()) {
		n++;
	}
	return ((1u << n) + br.bits(n));
}

} // namespace

bool pi_decode(const uint8_t *data, size_t size, bool lineskip, PiImage &out)
{
	const uint8_t *p = data;
	const uint8_t *end = (data + size);
	if((size < 2) || (p[0] != 'P') || (p[1] != 'i')) {
		return false;
	}
	p += 2;
	while((p < end) && (*p != 0x1A)) { p++; } // comment
	if(p < end) { p++; }
	while((p < end) && (*p != 0x00)) { p++; } // dummy
	if(p < end) { p++; }
	if((end - p) < 10) {
		return false;
	}
	const uint8_t mode = *p++;
	p += 2; // aspect ratio
	const uint8_t planes = *p++;
	p += 4; // machine
	const unsigned ext = ((p[0] << 8) | p[1]);
	p += (2 + ext);
	if((end - p) < 4) {
		return false;
	}
	const int w = ((p[0] << 8) | p[1]);
	const int h_full = ((p[2] << 8) | p[3]);
	p += 4;
	if((planes != 4) || (w <= 0) || (h_full <= 0)) {
		return false;
	}
	out.has_palette = !(mode & 0x80);
	if(out.has_palette) {
		if((end - p) < 48) {
			return false;
		}
		memcpy(out.palette, p, 48);
		p += 48;
	}

	// Two virtual lines above the image, then the image.
	const size_t total = (size_t(w) * (h_full + 2));
	uint8_t *buf = static_cast<uint8_t *>(malloc(total + 4));
	if(!buf) {
		return false;
	}
	BitReader br{ p, end };
	ColorTable tbl;

	uint8_t c1 = tbl.read(br, 0);
	uint8_t c2 = tbl.read(br, c1);
	for(size_t i = 0; i < (size_t(w) * 2); i += 2) {
		buf[i] = c1;
		buf[i + 1] = c2;
	}

	size_t pos = (size_t(w) * 2);
	long prev_src = 0; // 0 = none
	while(pos < total) {
		long src; // relative offset, or position identity for type 0
		bool type0_equal = false;
		if(br.bit()) {
			if(!br.bit()) {
				src = -(2L * w);           // 10: two lines up
			} else if(!br.bit()) {
				src = (-long(w) + 1);      // 110: one line up, +1
			} else {
				src = (-long(w) - 1);      // 111: one line up, -1
			}
		} else if(br.bit()) {
			src = -long(w);                // 01: one line up
		} else {
			src = -4;                      // 00
			type0_equal = (buf[pos - 1] == buf[pos - 2]);
		}

		if(src == prev_src) {
			// Literal color pairs.
			uint8_t prev = buf[pos - 1];
			do {
				const uint8_t a = tbl.read(br, prev);
				const uint8_t b = tbl.read(br, a);
				if(pos < total) { buf[pos] = a; }
				if((pos + 1) < total) { buf[pos + 1] = b; }
				pos += 2;
				prev = b;
			} while((pos < total) && br.bit());
			prev_src = 0;
			continue;
		}
		prev_src = src;

		const long from = (type0_equal ? -2 : src);
		size_t n = (size_t(read_length(br)) * 2);
		if(n > (total - pos)) {
			n = (total - pos);
		}
		// Overlapping copies are intended (byte by byte).
		for(size_t i = 0; i < n; i++, pos++) {
			const long s = (long(pos) + from);
			buf[pos] = ((s >= 0) ? buf[s] : 0);
		}
	}

	const int h = (lineskip ? (h_full / 2) : h_full);
	out.w = w;
	out.h = h;
	out.pixels = static_cast<uint8_t *>(malloc(size_t(w) * h));
	if(!out.pixels) {
		free(buf);
		return false;
	}
	for(int y = 0; y < h; y++) {
		const int sy = (lineskip ? (y * 2) : y);
		memcpy(&out.pixels[size_t(y) * w], &buf[size_t(sy + 2) * w], w);
	}
	free(buf);
	return true;
}
