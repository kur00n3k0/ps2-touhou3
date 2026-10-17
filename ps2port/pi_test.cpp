// Host test for platform/ps2/pi_decode.cpp: decodes PI files and reports how
// much of each file's bitstream the decoder consumed when the image was full.
#include "platform/ps2/pi_decode.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

// Same BitReader-free check: re-run a decode with a size-limited copy to find
// the smallest prefix that still decodes to the same pixels.
int main(int argc, char **argv)
{
	int ok = 0, bad = 0;
	for(int i = 1; i < argc; i++) {
		FILE *fp = fopen(argv[i], "rb");
		std::vector<uint8_t> d;
		int c;
		while((c = fgetc(fp)) != EOF) d.push_back(uint8_t(c));
		fclose(fp);
		PiImage full;
		if(!pi_decode(d.data(), d.size(), false, full)) {
			printf("%-14s DECODE FAILED\n", argv[i]);
			bad++;
			continue;
		}
		// Binary search the shortest input prefix giving identical pixels:
		// a correct decoder needs (almost) the whole file.
		size_t lo = 0, hi = d.size();
		while(lo < hi) {
			size_t mid = (lo + hi) / 2;
			PiImage t;
			bool same = pi_decode(d.data(), mid, false, t) &&
				(memcmp(t.pixels, full.pixels, size_t(full.w) * full.h) == 0);
			free(t.pixels);
			if(same) hi = mid; else lo = mid + 1;
		}
		int hist[16] = {0};
		for(size_t k = 0; k < size_t(full.w) * full.h; k++) hist[full.pixels[k] & 15]++;
		int used = 0;
		for(int k = 0; k < 16; k++) used += (hist[k] > 0);
		const long unused = long(d.size()) - long(lo);
		printf("%-14s %3dx%-3d bytes needed %6zu of %6zu (unused tail %ld)  colors used %d\n",
			argv[i], full.w, full.h, lo, d.size(), unused, used);
		(unused <= 4 ? ok : bad)++;
		free(full.pixels);
	}
	printf("\n%d files end exactly at the bitstream end, %d don't\n", ok, bad);
}
