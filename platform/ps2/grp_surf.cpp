// PS2 port: sprite/image surfaces and PI loading, replacing
// platform/x86real/pc98/grp_surf.cpp, blitter.cpp and the piloadc library.
//
// A surface stores its bitplanes one after another, each [w] bytes (= pixel
// width / 8) wide and [h] rows tall; [plane_paras] is the size of one plane
// in bytes on the PS2.

#include "platform/x86real/pc98/grp_surf.hpp"
#include "platform/ps2/pc98hw.hpp"
#include "platform/ps2/pi_decode.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE* ps2_game_fopen(const char *fn, const char *mode);

namespace {

// Source rectangle inside a surface (or VRAM), in bytes and rows.
struct SrcRect {
	int x, y, w, h;
};

SrcRect source_rect(int surf_w, int surf_h, const LTWH<upixel_t> *region)
{
	if(!region) {
		return { 0, 0, surf_w, surf_h };
	}
	const int left = (region->left / BYTE_DOTS);
	return {
		left,
		int(region->top),
		int(((region->left + region->w + BYTE_MASK) / BYTE_DOTS) - left),
		int(region->h),
	};
}

// Copies [r] of [src] (with [stride] bytes per row) to VRAM byte column
// [vx], row [vy] of [plane] on the accessed page, clipped to the screen.
void blit_plane_to_vram(
	int plane, int vx, int vy, const uint8_t *src, int stride, const SrcRect &r
)
{
	uint8_t *dst = pc98::vram[pc98::page_accessed].plane[plane];
	for(int row = 0; row < r.h; row++) {
		const int y = (vy + row);
		if((y < 0) || (y >= RES_Y)) {
			continue;
		}
		for(int col = 0; col < r.w; col++) {
			const int x = (vx + col);
			if((x < 0) || (x >= ROW_SIZE)) {
				continue;
			}
			dst[(y * ROW_SIZE) + x] = src[((r.y + row) * stride) + r.x + col];
		}
	}
}

// Copies the VRAM rectangle [r] of [plane] into [dst] at byte column
// [dx], row [dy], with [stride] bytes per row. No clipping, like the
// original's blitter_init_noclip().
void snap_plane_from_vram(
	int plane, uint8_t *dst, int stride, int dx, int dy, const SrcRect &r
)
{
	const uint8_t *src = pc98::vram[pc98::page_accessed].plane[plane];
	for(int row = 0; row < r.h; row++) {
		for(int col = 0; col < r.w; col++) {
			dst[((dy + row) * stride) + dx + col] = src[
				((r.y + row) * ROW_SIZE) + r.x + col
			];
		}
	}
}

bool surface_alloc(GrpSurface_M &surf, upixel_t w, upixel_t h, int planes)
{
	surf.free();
	const unsigned vram_w = ((w + BYTE_MASK) / BYTE_DOTS);
	const uint32_t plane_size = (vram_w * h);
	surf.plane_B = static_cast<uint8_t *>(calloc(planes, (plane_size ? plane_size : 1)));
	if(!surf.plane_B) {
		return true;
	}
	surf.plane_paras = plane_size;
	surf.w = vram_w;
	surf.h = h;
	return false;
}

} // namespace

void GrpSurface_M::free(void)
{
	::free(plane_B);
	plane_B = nullptr;
	plane_paras = 0;
	w = 0;
	h = 0;
}

bool GrpSurface_M1::alloc(upixel_t w, upixel_t h)
{
	return surface_alloc(*this, w, h, 1);
}

void GrpSurface_M1::snap(
	uscreen_x_t dst_left,
	uscreen_y_t dst_top,
	vram_plane_t plane,
	const LTWH<upixel_t> *region
)
{
	const SrcRect r = source_rect(ROW_SIZE, h, region);
	snap_plane_from_vram(plane, plane_B, w, (dst_left / BYTE_DOTS), dst_top, r);
}

void GrpSurface_M1::write(
	vram_plane_t plane,
	screen_x_t left,
	screen_y_t top,
	const LTWH<upixel_t> *region
)
{
	const SrcRect r = source_rect(w, h, region);
	blit_plane_to_vram(plane, (left >> BYTE_BITS), top, plane_B, w, r);
}

bool GrpSurface_M4::alloc(upixel_t w, upixel_t h)
{
	return surface_alloc(*this, w, h, 4);
}

void GrpSurface_M4::snap(
	uscreen_x_t dst_left, uscreen_y_t dst_top, const LTWH<upixel_t> *region
)
{
	const SrcRect r = source_rect(ROW_SIZE, h, region);
	for(int p = 0; p < PLANE_COUNT; p++) {
		snap_plane_from_vram(
			p, (plane_B + (p * plane_paras)), w, (dst_left / BYTE_DOTS), dst_top, r
		);
	}
}

void GrpSurface_M4::write(
	screen_x_t left, screen_y_t top, const LTWH<upixel_t> *region
)
{
	if(!plane_B) {
		return;
	}
	const SrcRect r = source_rect(w, h, region);
	for(int p = 0; p < PLANE_COUNT; p++) {
		blit_plane_to_vram(
			p, (left >> BYTE_BITS), top, (plane_B + (p * plane_paras)), w, r
		);
	}
}

void GrpSurface_M4::write_bg_region(
	upixel_t left, upixel_t top, upixel_t w, upixel_t h
)
{
	LTWH<upixel_t> region;
	region.left = left;
	region.top = top;
	region.w = w;
	region.h = h;
	write(left, top, &region);
}

// The original uses the EGC to OR each row through a 16-dot pattern from
// [masks], cycling through them row by row (with [left] as the pattern's
// starting bit). Same result in software.
void GrpSurface_M4::or_masked(
	screen_x_t left,
	screen_y_t top,
	const dots16_t *masks,
	unsigned int mask_count,
	const LTWH<upixel_t> *region
)
{
	if(!plane_B || !mask_count) {
		return;
	}
	const SrcRect r = source_rect(w, h, region);
	const int vx = (left >> BYTE_BITS);
	for(int row = 0; row < r.h; row++) {
		const int y = (top + row);
		if((y < 0) || (y >= RES_Y)) {
			continue;
		}
		const dots16_t m = masks[row % mask_count];
		// dots16_t in x86 order: low byte = left 8 dots.
		const uint8_t mask_bytes[2] = { uint8_t(m & 0xFF), uint8_t(m >> 8) };
		for(int col = 0; col < r.w; col++) {
			const int x = (vx + col);
			if((x < 0) || (x >= ROW_SIZE)) {
				continue;
			}
			const uint8_t mb = mask_bytes[(x & 1)];
			for(int p = 0; p < PLANE_COUNT; p++) {
				const uint8_t *src = (plane_B + (p * plane_paras));
				pc98::vram[pc98::page_accessed].plane[p][(y * ROW_SIZE) + x] |= (
					src[((r.y + row) * w) + r.x + col] & mb
				);
			}
		}
	}
}

// PI loading
// ----------

namespace {

// Reads a whole game file (from the pack archive or the game folder).
uint8_t* read_whole_file(const char *fn, size_t &size)
{
	FILE *fp = ps2_game_fopen(fn, "rb");
	if(!fp) {
		return nullptr;
	}
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t *buf = static_cast<uint8_t *>(malloc(size ? size : 1));
	if(buf && (fread(buf, 1, size, fp) != size)) {
		::free(buf);
		buf = nullptr;
	}
	fclose(fp);
	return buf;
}

bool load_pi(const char *fn, bool lineskip, PiImage &img)
{
	size_t size;
	uint8_t *data = read_whole_file(fn, size);
	if(!data) {
		printf("pi: can't open %s\n", fn);
		return false;
	}
	const bool ok = pi_decode(data, size, lineskip, img);
	::free(data);
	if(!ok) {
		printf("pi: can't decode %s\n", fn);
	}
	return ok;
}

void extract_palette(const PiImage &img, Palette8 *palette)
{
	if(!palette || !img.has_palette) {
		return;
	}
	for(int i = 0; i < COLOR_COUNT; i++) {
		for(int c = 0; c < COMPONENT_COUNT; c++) {
			(*palette)[i].v[c] = img.palette[i][c];
		}
	}
}

// Converts 8 color indices into one byte per bitplane.
void chunky_to_planar(const uint8_t *px, uint8_t out[PLANE_COUNT])
{
	for(int p = 0; p < PLANE_COUNT; p++) {
		uint8_t b = 0;
		for(int i = 0; i < BYTE_DOTS; i++) {
			b = uint8_t((b << 1) | ((px[i] >> p) & 1));
		}
		out[p] = b;
	}
}

} // namespace

int GrpSurface_LoadPI(
	GrpSurface_M4 &surf, Palette8 *palette, const char *fn, pi_flag_t flags
)
{
	PiImage img;
	if(!load_pi(fn, (flags & PIF_LINESKIP), img)) {
		return -2; // FileNotFound
	}
	if(surface_alloc(surf, img.w, img.h, PLANE_COUNT)) {
		::free(img.pixels);
		return -8; // InsufficientMemory
	}
	for(int y = 0; y < img.h; y++) {
		for(unsigned bx = 0; bx < surf.w; bx++) {
			uint8_t planes[PLANE_COUNT];
			chunky_to_planar(&img.pixels[(y * img.w) + (bx * BYTE_DOTS)], planes);
			for(int p = 0; p < PLANE_COUNT; p++) {
				surf.plane_B[(p * surf.plane_paras) + (y * surf.w) + bx] = planes[p];
			}
		}
	}
	extract_palette(img, palette);
	::free(img.pixels);
	return 0;
}

int GrpSurface_BlitBackgroundPI(Palette8 *palette, const char *fn)
{
	PiImage img;
	if(!load_pi(fn, false, img)) {
		return -2;
	}
	pc98::Page &pg = pc98::vram[pc98::page_accessed];
	const int rows = ((img.h < RES_Y) ? img.h : RES_Y);
	const int cols = (((img.w / BYTE_DOTS) < ROW_SIZE) ? (img.w / BYTE_DOTS) : ROW_SIZE);
	for(int y = 0; y < rows; y++) {
		for(int bx = 0; bx < cols; bx++) {
			uint8_t planes[PLANE_COUNT];
			chunky_to_planar(&img.pixels[(y * img.w) + (bx * BYTE_DOTS)], planes);
			for(int p = 0; p < PLANE_COUNT; p++) {
				pg.plane[p][(y * ROW_SIZE) + bx] = planes[p];
			}
		}
	}
	extract_palette(img, palette);
	::free(img.pixels);
	return 0;
}
