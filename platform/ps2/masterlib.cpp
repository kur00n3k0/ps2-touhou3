// PS2 port: the subset of master.lib (by 恋塚昭彦 and contributors) that the
// TH03 OP/MAINL binary uses, reimplemented on top of the software PC-98 model
// in pc98hw. Behavior follows the original assembly in libs/master.lib/; the
// notes next to each function say which details were kept.

#include "libs/master.lib/master.hpp"
#include "libs/master.lib/pc98_gfx.hpp"
#include "platform/ps2/pc98hw.hpp"
#include "platform/ps2/fs.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

using namespace pc98;

// Declared with C++ linkage by master.hpp's vsync_proc_set() macro.
func_t vsync_Proc = nullptr;

extern "C" {

/// Globals
/// -------
volatile unsigned int vsync_Count1 = 0;
volatile unsigned int vsync_Count2 = 0;

Palette8 Palettes;
unsigned int PaletteTone = 100;

long random_seed = 0;

int js_bexist = 0;
unsigned js_stat[2] = { 0, 0 };

unsigned graph_VramZoom = 0;
/// -------

/// Memory
/// ------
int mem_assign_dos(unsigned) { return 0; } // 0 = success
int mem_unassign(void) { return 0; }

void __seg* hmem_allocbyte(unsigned bytesize)
{
	return calloc(1, (bytesize ? bytesize : 1));
}

void __seg* hmem_alloc(unsigned parasize)
{
	return hmem_allocbyte(parasize * 16);
}

void hmem_free(void __seg* memseg)
{
	free(memseg);
}

void __seg* smem_wget(unsigned bytesize) { return hmem_allocbyte(bytesize); }
void smem_release(void __seg* memseg) { free(memseg); }
/// ------

/// Misc. system
/// ------------
void dos_puts2(const char *string)
{
	printf("%s", string);
}

void key_beep_on(void) {}
void key_beep_off(void) {}
void text_systemline_show(void) {}
void text_systemline_hide(void) {}
void text_cursor_show(void) {}
void text_cursor_hide(void) {}
void egc_start(void) {}
void vsync_start(void) {}
void vsync_end(void) {}

void vsync_wait(void)
{
	const unsigned int start = vsync_Count1;
	while(vsync_Count1 == start) {
	}
}

int js_start(void) { js_bexist = 0; return 0; }
void js_end(void) {}
int js_sense(void) { return 0; }

// The resident palette lets DOS programs restore the palette on exit. There's
// nothing to restore to on the PS2.
int respal_exist(void) { return 1; }
int respal_create(void) { return 1; }
void respal_get_palettes(void) {}
void respal_set_palettes(void) {}
void respal_free(void) {}
/// ------------

/// Math
/// ----
extern const uint8_t AtanTable8[256];

// Same LCG and output bits as random.asm.
int irand(void)
{
	random_seed = (uint32_t(random_seed) * 0x015A4E35u) + 1u;
	return ((uint32_t(random_seed) >> 16) & 0x7FFF);
}

// iatan2.asm: 8-bit angle (256 = full circle), 0 for (0, 0).
int iatan2(int y, int x)
{
	y = int16_t(y);
	x = int16_t(x);
	if((x | y) == 0) {
		return 0;
	}
	const unsigned ay = ((y < 0) ? -y : y);
	const unsigned ax = ((x < 0) ? -x : x);
	unsigned a;
	if(ax == ay) {
		a = 32;
	} else if(ax > ay) {
		a = AtanTable8[((ay << 8) / ax) & 0xFF];
	} else {
		a = (64 - AtanTable8[((ax << 8) / ay) & 0xFF]);
	}
	if(x < 0) {
		a = (128 - a);
	}
	a &= 0xFF;
	if(y < 0) {
		a = ((-a) & 0xFF);
	}
	return a;
}
/// ----

/// Palette
/// -------
// palette_show.asm: tone 0-100 scales toward black, 100-200 toward white.
void palette_show(void)
{
	int tone = int(PaletteTone);
	if(tone < 0) {
		tone = 0;
	} else if(tone > 200) {
		tone = 200;
	}
	const bool to_white = (tone > 100);
	const int mul = (to_white ? (200 - tone) : tone);
	const uint8_t x = (to_white ? 0x0F : 0x00);
	for(int i = 0; i < 16; i++) {
		for(int c = 0; c < 3; c++) {
			const uint8_t v = ((Palettes[i].v[c] >> 4) ^ x);
			palette[i][c] = (((v * mul) / 100) ^ x);
		}
	}
	palette_dirty = true;
}

void palette_black_out(unsigned speed)
{
	PaletteTone = 100;
	vsync_wait();
	for(;;) {
		palette_show();
		for(unsigned i = 0; i < speed; i++) {
			vsync_wait();
		}
		if(int(PaletteTone -= 6) <= 0) {
			break;
		}
	}
	PaletteTone = 0;
	palette_show();
}

void palette_black_in(unsigned speed)
{
	PaletteTone = 0;
	vsync_wait();
	for(;;) {
		palette_show();
		for(unsigned i = 0; i < speed; i++) {
			vsync_wait();
		}
		if((PaletteTone += 6) >= 100) {
			break;
		}
	}
	PaletteTone = 100;
	palette_show();
}

static FILE* game_fopen(const char *fn, const char *mode);

// .RGB files: 16 × (r, g, b), 4 bits each; expanded to 8 bits like the asm.
int palette_entry_rgb(const char *fn)
{
	fs::IoLock io;
	FILE *fp = game_fopen(fn, "rb");
	if(!fp) {
		return -2;
	}
	uint8_t buf[48];
	const size_t got = fread(buf, 1, sizeof(buf), fp);
	fclose(fp);
	for(int i = 0; i < 48; i++) {
		buf[i] |= (buf[i] << 4);
	}
	memcpy(&Palettes, buf, sizeof(buf));
	return ((got == sizeof(buf)) ? 0 : -13);
}
/// -------

/// Graphics
/// --------
void graph_start(void)
{
	graph_clear();
	text_shown = true;
}

void graph_400line(void) {}
void graph_show(void) {}
void graph_hide(void) {}

void graph_clear(void)
{
	for(int p = 0; p < PLANES; p++) {
		memset(vram[page_accessed].plane[p], 0, PLANE_BYTES);
	}
}

// graph_copy_page.asm: copies the other page onto [to_page], which stays
// accessed afterwards.
int graph_copy_page(int to_page)
{
	to_page &= 1;
	vram[to_page] = vram[to_page ^ 1];
	page_accessed = to_page;
	grcg_mode = GRCG_OFF;
	return 1;
}

// egc_shift_left_all.asm: shifts every line of the accessed page left by
// [dots]. The EGC's bit length is (639 - dots), so the rightmost [dots]
// pixels of each line keep their old contents.
void egc_shift_left_all(pixel_t dots)
{
	if(dots <= 0) {
		return;
	}
	if(dots >= VRAM_W) {
		return;
	}
	Page& pg = vram[page_accessed];
	for(int p = 0; p < PLANES; p++) {
		for(int y = 0; y < VRAM_H; y++) {
			uint8_t *row = &pg.plane[p][y * ROW_BYTES];
			uint8_t src[ROW_BYTES];
			memcpy(src, row, ROW_BYTES);
			for(int x = 0; x < (VRAM_W - dots); x++) {
				const int sx = (x + dots);
				const uint8_t bit = ((src[sx >> 3] >> (7 - (sx & 7))) & 1);
				const uint8_t m = (0x80 >> (x & 7));
				row[x >> 3] = (bit ? (row[x >> 3] | m) : (row[x >> 3] & ~m));
			}
		}
	}
}
/// --------

/// GRCG
/// ----
void grcg_setcolor(int mode, vc2 col)
{
	pc98::grcg_setcolor(mode, col);
}

void grcg_off(void)
{
	pc98::grcg_off();
}

// Writes the horizontal span [x1, x2] of line [y] through the GRCG.
static void grcg_hspan(int x1, int x2, int y)
{
	if((y < 0) || (y >= VRAM_H)) {
		return;
	}
	if(x1 < 0) {
		x1 = 0;
	}
	if(x2 >= VRAM_W) {
		x2 = (VRAM_W - 1);
	}
	if(x1 > x2) {
		return;
	}
	const int row = (y * ROW_BYTES);
	for(int bx = (x1 >> 3); bx <= (x2 >> 3); bx++) {
		uint8_t mask = 0xFF;
		if(bx == (x1 >> 3)) {
			mask &= (0xFF >> (x1 & 7));
		}
		if(bx == (x2 >> 3)) {
			mask &= (0xFF << (7 - (x2 & 7)));
		}
		vram_write8(0, (row + bx), mask);
	}
}

// Inclusive corners in any order, clipped to the screen.
void grcg_boxfill(int x1, int y1, int x2, int y2)
{
	if(x1 > x2) { int t = x1; x1 = x2; x2 = t; }
	if(y1 > y2) { int t = y1; y1 = y2; y2 = t; }
	for(int y = y1; y <= y2; y++) {
		grcg_hspan(x1, x2, y);
	}
}

void grcg_byteboxfill_x(int x1, int y1, int x2, int y2)
{
	grcg_boxfill((x1 * 8), y1, ((x2 * 8) + 7), y2);
}

// Filled convex polygon: per scanline, the span between the leftmost and
// rightmost edge crossings.
void grcg_polygon_c(const screen_point_t *pts, int npoint)
{
	if(npoint < 2) {
		return;
	}
	int ymin = pts[0].y, ymax = pts[0].y;
	for(int i = 1; i < npoint; i++) {
		if(pts[i].y < ymin) { ymin = pts[i].y; }
		if(pts[i].y > ymax) { ymax = pts[i].y; }
	}
	for(int y = ymin; y <= ymax; y++) {
		int xl = 0x7FFF, xr = -0x8000;
		for(int i = 0; i < npoint; i++) {
			const screen_point_t& a = pts[i];
			const screen_point_t& b = pts[(i + 1) % npoint];
			if(((y < a.y) && (y < b.y)) || ((y > a.y) && (y > b.y))) {
				continue;
			}
			int x;
			if(a.y == b.y) {
				if(a.x < xl) { xl = a.x; }
				if(a.x > xr) { xr = a.x; }
				x = b.x;
			} else {
				x = (a.x + (((b.x - a.x) * (y - a.y)) / (b.y - a.y)));
			}
			if(x < xl) { xl = x; }
			if(x > xr) { xr = x; }
		}
		if(xl <= xr) {
			grcg_hspan(xl, xr, y);
		}
	}
}
/// ----

/// Text RAM
/// --------
static inline void tram_put(int cell, uint16_t code, uint8_t atrb)
{
	if((cell < 0) || (cell >= (TRAM_W * TRAM_H))) {
		return;
	}
	tram_code[(cell * 2) + 0] = (code & 0xFF);
	tram_code[(cell * 2) + 1] = (code >> 8);
	tram_atrb[(cell * 2) + 0] = atrb;
	tram_atrb[(cell * 2) + 1] = 0;
}

// ESC[2J on the PC-98 console: spaces with the default (white) attribute.
void text_clear(void)
{
	for(int i = 0; i < (TRAM_W * TRAM_H); i++) {
		tram_put(i, 0x0020, 0xE1);
	}
}

void text_fillca(unsigned ch, unsigned atrb)
{
	for(int i = 0; i < (TRAM_W * TRAM_H); i++) {
		tram_put(i, ch, atrb);
	}
}

// gaiji_putsa.asm: each gaiji takes two cells, with code (0x56 | 0x57) in the
// low byte and the glyph number's low 7 bits in the high byte; the right
// cell additionally has bit 7 set.
void gaiji_putsa(unsigned x, unsigned y, const char *str, unsigned atrb)
{
	int cell = ((y * TRAM_W) + x);
	for(; *str; str++) {
		const uint8_t c = *str;
		const uint16_t code = (((c & 0x7F) << 8) | (0x56 + (c >> 7)));
		tram_put(cell++, code, atrb);
		tram_put(cell++, (code | 0x8000), atrb);
	}
}

void gaiji_putca(unsigned x, unsigned y, unsigned c, unsigned atrb)
{
	const char str[2] = { char(c), '\0' };
	gaiji_putsa(x, y, str, atrb);
}

void text_putsa(unsigned x, unsigned y, const char *str, unsigned atrb)
{
	int cell = ((y * TRAM_W) + x);
	for(; *str; str++) {
		tram_put(cell++, uint8_t(*str), atrb);
	}
}
/// --------

/// Gaiji
/// -----
int gaiji_backup(void) { return 1; }
int gaiji_restore(void) { return 1; }

struct bfnt_header_t {
	char id[5];
	uint8_t col;
	uint8_t ver;
	uint8_t x00;
	uint16_t xdots;
	uint16_t ydots;
	uint16_t start;
	uint16_t end;
	char font_name[8];
	uint32_t time;
	uint16_t ext_size;
	uint16_t hdr_size;
} __attribute__((packed));
static_assert(sizeof(bfnt_header_t) == 32);

static bool bfnt_open(const char *fn, FILE *&fp, bfnt_header_t& h)
{
	fp = game_fopen(fn, "rb");
	if(!fp) {
		printf("bfnt: can't open %s\n", fn);
		return false;
	}
	if((fread(&h, sizeof(h), 1, fp) != 1) || memcmp(h.id, "BFNT\x1A", 5)) {
		printf("bfnt: %s is not a BFNT file\n", fn);
		fclose(fp);
		return false;
	}
	fseek(fp, h.ext_size, SEEK_CUR);
	return true;
}

// gaiji_entry_bfnt.asm: a monochrome 16×16 font with 256 glyphs, 2 bytes per
// row, written to all 256 gaiji slots.
int gaiji_entry_bfnt(const char *fn)
{
	fs::IoLock io;
	FILE *fp;
	bfnt_header_t h;
	if(!bfnt_open(fn, fp, h)) {
		return 0;
	}
	int ret = 0;
	if((h.col == 0) && (h.xdots == 16) && (h.ydots == 16) && (h.start == 0) && (h.end == 255)) {
		ret = (fread(gaiji, sizeof(gaiji), 1, fp) == 1);
	}
	fclose(fp);
	return ret;
}

// graph_gaiji_puts.asm: draws gaiji into graphics VRAM in color [col] via the
// GRCG, [step] pixels apart.
void graph_gaiji_puts(
	screen_x_t left, vram_y_t top, pixel_t step, const char *str, vc2 col
)
{
	pc98::grcg_setcolor(GRCG_RMW, col);
	for(; *str; str++, left += step) {
		const uint8_t *g = gaiji[uint8_t(*str)];
		const int sh = (left & 7);
		for(int y = 0; y < 16; y++) {
			const int vy = (top + y);
			if((vy < 0) || (vy >= VRAM_H)) {
				continue;
			}
			const uint32_t row = (((g[y * 2] << 8) | g[(y * 2) + 1]) << (8 - sh));
			const int base = ((vy * ROW_BYTES) + (left >> 3));
			for(int b = 0; b < 3; b++) {
				const uint8_t bits = (row >> (16 - (b * 8)));
				const int bx = ((left >> 3) + b);
				if(bits && (bx >= 0) && (bx < ROW_BYTES)) {
					vram_write8(0, (base + b), bits);
				}
			}
		}
	}
	pc98::grcg_off();
}

void graph_gaiji_putc(screen_x_t left, vram_y_t top, int c, vc2 col)
{
	const char str[2] = { char(c), '\0' };
	graph_gaiji_puts(left, top, 16, str, col);
}
/// -----

/// "super" sprites (from BFNT files)
/// ---------------------------------
enum { SUPER_MAX = 512 };

struct SuperPat {
	int wbytes = 0;
	int h = 0;
	uint8_t *planes = nullptr; // [4][h][wbytes]
	uint8_t *mask = nullptr;   // [h][wbytes], 1 = opaque
};

static SuperPat super_pats[SUPER_MAX];
static int super_count = 0;

// bfnt_entry_pat.asm + super_entry_pat.asm: 4bpp packed patterns (left pixel
// in the high nibble); pixels equal to the clear color (0 without an
// extended header) are transparent. A palette, if present, is loaded into
// Palettes with each entry's bytes rotated as in bfnt_palette_set.asm.
int super_entry_bfnt(const char *fn)
{
	fs::IoLock io;
	FILE *fp;
	bfnt_header_t h;
	if(!bfnt_open(fn, fp, h)) {
		return -2;
	}
	if((h.col & 0x7F) != 3) {
		fclose(fp);
		return -13;
	}
	if(h.col & 0x80) {
		uint8_t pal[48];
		fread(pal, 1, sizeof(pal), fp);
		for(int i = 0; i < 16; i++) {
			Palettes[i].v[0] = pal[(i * 3) + 1];
			Palettes[i].v[1] = pal[(i * 3) + 2];
			Palettes[i].v[2] = pal[(i * 3) + 0];
		}
	}
	const uint8_t clear_color = 0;
	const int wbytes = (h.xdots / 8);
	const int count = ((h.end - h.start) + 1);
	const size_t packed_size = (size_t(wbytes) * h.ydots * 4);
	uint8_t *packed = static_cast<uint8_t *>(malloc(packed_size));
	int loaded = 0;
	for(int n = 0; n < count; n++) {
		if(super_count >= SUPER_MAX) {
			break;
		}
		if(fread(packed, 1, packed_size, fp) != packed_size) {
			break;
		}
		SuperPat& sp = super_pats[super_count++];
		sp.wbytes = wbytes;
		sp.h = h.ydots;
		const size_t plane_size = (size_t(wbytes) * h.ydots);
		sp.planes = static_cast<uint8_t *>(calloc(4, plane_size));
		sp.mask = static_cast<uint8_t *>(calloc(1, plane_size));
		for(size_t i = 0; i < plane_size; i++) {
			// 8 pixels = 4 packed bytes
			for(int px = 0; px < 8; px++) {
				const uint8_t byte = packed[(i * 4) + (px >> 1)];
				const uint8_t c = ((px & 1) ? (byte & 0x0F) : (byte >> 4));
				const uint8_t bit = (0x80 >> px);
				for(int p = 0; p < 4; p++) {
					if(c & (1 << p)) {
						sp.planes[(p * plane_size) + i] |= bit;
					}
				}
				if(c != clear_color) {
					sp.mask[i] |= bit;
				}
			}
		}
		loaded++;
	}
	free(packed);
	fclose(fp);
	return loaded;
}

void super_free(void)
{
	for(int i = 0; i < super_count; i++) {
		free(super_pats[i].planes);
		free(super_pats[i].mask);
		super_pats[i] = SuperPat();
	}
	super_count = 0;
}

// super_put.asm: opaque pixels replace VRAM in all four planes, transparent
// ones keep it. No clipping beyond the edges of VRAM.
void super_put(int x, int y, int num)
{
	if((num < 0) || (num >= super_count)) {
		return;
	}
	const SuperPat& sp = super_pats[num];
	const size_t plane_size = (size_t(sp.wbytes) * sp.h);
	Page& pg = vram[page_accessed];
	const int sh = (x & 7);
	for(int row = 0; row < sp.h; row++) {
		const int vy = (y + row);
		if((vy < 0) || (vy >= VRAM_H)) {
			continue;
		}
		for(int bx = 0; bx <= sp.wbytes; bx++) {
			// Combine the bytes overlapping VRAM byte (x >> 3) + bx.
			auto shifted = [&](const uint8_t *src) -> uint8_t {
				const uint8_t prev = ((bx > 0) ? src[(row * sp.wbytes) + bx - 1] : 0);
				const uint8_t cur = ((bx < sp.wbytes) ? src[(row * sp.wbytes) + bx] : 0);
				return uint8_t((cur >> sh) | (prev << (8 - sh)));
			};
			const int vx = ((x >> 3) + bx);
			if((vx < 0) || (vx >= ROW_BYTES)) {
				continue;
			}
			const uint8_t m = (sh ? shifted(sp.mask) : ((bx < sp.wbytes) ? sp.mask[(row * sp.wbytes) + bx] : 0));
			if(!m) {
				continue;
			}
			const int off = ((vy * ROW_BYTES) + vx);
			for(int p = 0; p < 4; p++) {
				const uint8_t *src = (sp.planes + (p * plane_size));
				const uint8_t d = (sh ? shifted(src) : ((bx < sp.wbytes) ? src[(row * sp.wbytes) + bx] : 0));
				uint8_t& v = pg.plane[p][off];
				v = ((v & ~m) | (d & m));
			}
		}
	}
}
/// ---------------------------------

/// Files and pack archives
/// -----------------------
// TH03's pfstart() hooks DOS's file functions so that files inside the
// archive are found by plain open calls (pfint21.asm). The same is done here
// for master.lib's file_* functions and for game_fopen().

namespace {
struct PfEntry {
	char name[14];
	uint16_t type;
	uint8_t key;
	uint16_t packsize;
	uint16_t orgsize;
	uint32_t offset;
};
PfEntry *pf_entries = nullptr;
int pf_count = 0;
FILE *pf_fp = nullptr;

bool name_ieq(const char *a, const char *b)
{
	for(; *a && *b; a++, b++) {
		if(toupper(uint8_t(*a)) != toupper(uint8_t(*b))) {
			return false;
		}
	}
	return (*a == *b);
}

// Returns a malloc'd buffer with the unpacked file, or nullptr.
uint8_t* pf_load(const char *fn, uint32_t& size)
{
	for(int i = 0; i < pf_count; i++) {
		const PfEntry& e = pf_entries[i];
		if(!name_ieq(e.name, fn)) {
			continue;
		}
		uint8_t *raw = static_cast<uint8_t *>(malloc(e.packsize + 1));
		fseek(pf_fp, e.offset, SEEK_SET);
		fread(raw, 1, e.packsize, pf_fp);
		if(e.key) {
			for(unsigned j = 0; j < e.packsize; j++) {
				raw[j] ^= e.key;
			}
		}
		uint8_t *out = static_cast<uint8_t *>(malloc(e.orgsize + 1));
		if(e.type == 0xF388) {
			memcpy(out, raw, e.orgsize);
		} else {
			// pfgetc.asm's RLE: a byte equal to the previous one is followed
			// by a count of extra repeats.
			unsigned o = 0, j = 0;
			int prev = -1;
			while((j < e.packsize) && (o < e.orgsize)) {
				const uint8_t c = raw[j++];
				out[o++] = c;
				if(c == prev) {
					unsigned n = ((j < e.packsize) ? raw[j++] : 0);
					while(n-- && (o < e.orgsize)) {
						out[o++] = c;
					}
				}
				prev = c;
			}
		}
		free(raw);
		size = e.orgsize;
		return out;
	}
	return nullptr;
}
}

void pfstart(const unsigned char *parfile)
{
	fs::IoLock io;
	pfend();
	char path[256];
	fs::path(path, sizeof(path), "%s", fs::map_name(reinterpret_cast<const char *>(parfile)));
	pf_fp = fopen(path, "rb");
	if(!pf_fp) {
		printf("pf: can't open archive %s\n", path);
		return;
	}
	uint8_t hdr[16];
	fread(hdr, 1, sizeof(hdr), pf_fp);
	const uint16_t entries_size = (hdr[0] | (hdr[1] << 8));
	uint8_t key = hdr[6];
	uint8_t *tbl = static_cast<uint8_t *>(malloc(entries_size));
	fread(tbl, 1, entries_size, pf_fp);
	for(unsigned i = 0; i < entries_size; i++) { // pfint21.asm PFSTART_decrypt
		tbl[i] ^= key;
		key -= tbl[i];
	}
	pf_count = 0;
	pf_entries = static_cast<PfEntry *>(calloc((entries_size / 32) + 1, sizeof(PfEntry)));
	for(unsigned i = 0; (i + 32) <= entries_size; i += 32) {
		const uint8_t *e = &tbl[i];
		const uint16_t type = (e[0] | (e[1] << 8));
		if(type == 0) {
			break;
		}
		PfEntry& pe = pf_entries[pf_count++];
		pe.type = type;
		pe.key = e[2];
		memcpy(pe.name, &e[3], 13);
		pe.name[13] = '\0';
		pe.packsize = (e[16] | (e[17] << 8));
		pe.orgsize = (e[18] | (e[19] << 8));
		pe.offset = (e[20] | (e[21] << 8) | (e[22] << 16) | (uint32_t(e[23]) << 24));
	}
	free(tbl);
	printf("pf: %s has %d files\n", path, pf_count);
}

void pfend(void)
{
	fs::IoLock io;
	if(pf_fp) {
		fclose(pf_fp);
		pf_fp = nullptr;
	}
	free(pf_entries);
	pf_entries = nullptr;
	pf_count = 0;
}

// Opens a game file: from the started archive if it's in there (served from
// memory), otherwise from the game folder.
// Files written while running from a read-only disc (YUME.CFG, the high
// score file) live in memory for the rest of the session.
namespace {
struct RamFile {
	char name[32];
	uint8_t *data;
	size_t size;
	size_t cap;
};
enum { RAMFILES_MAX = 16 };
RamFile ramfiles[RAMFILES_MAX];

struct RamCursor {
	RamFile *f;
	size_t pos;
};

RamFile* ramfile_find(const char *fn, bool create)
{
	for(RamFile &f : ramfiles) {
		if(f.name[0] && name_ieq(f.name, fn)) {
			return &f;
		}
	}
	if(!create) {
		return nullptr;
	}
	for(RamFile &f : ramfiles) {
		if(!f.name[0]) {
			strncpy(f.name, fn, (sizeof(f.name) - 1));
			f.data = nullptr;
			f.size = f.cap = 0;
			return &f;
		}
	}
	return nullptr;
}

int ram_read(void *c, char *buf, _READ_WRITE_BUFSIZE_TYPE n)
{
	RamCursor *rc = static_cast<RamCursor *>(c);
	size_t avail = ((rc->pos < rc->f->size) ? (rc->f->size - rc->pos) : 0);
	size_t len = ((size_t(n) < avail) ? size_t(n) : avail);
	memcpy(buf, (rc->f->data + rc->pos), len);
	rc->pos += len;
	return int(len);
}

int ram_write(void *c, const char *buf, _READ_WRITE_BUFSIZE_TYPE n)
{
	RamCursor *rc = static_cast<RamCursor *>(c);
	RamFile *f = rc->f;
	const size_t end = (rc->pos + n);
	if(end > f->cap) {
		size_t cap = ((f->cap ? f->cap : 256));
		while(cap < end) {
			cap *= 2;
		}
		uint8_t *d = static_cast<uint8_t *>(realloc(f->data, cap));
		if(!d) {
			return -1;
		}
		memset((d + f->cap), 0, (cap - f->cap));
		f->data = d;
		f->cap = cap;
	}
	memcpy((f->data + rc->pos), buf, n);
	rc->pos = end;
	if(end > f->size) {
		f->size = end;
	}
	return int(n);
}

fpos_t ram_seek(void *c, fpos_t off, int whence)
{
	RamCursor *rc = static_cast<RamCursor *>(c);
	long base = ((whence == SEEK_SET) ? 0 : ((whence == SEEK_CUR) ? long(rc->pos) : long(rc->f->size)));
	long pos = (base + long(off));
	if(pos < 0) {
		return -1;
	}
	rc->pos = size_t(pos);
	return pos;
}

int ram_close(void *c)
{
	delete static_cast<RamCursor *>(c);
	return 0;
}

FILE* ramfile_open(RamFile *f, const char *mode)
{
	if((mode[0] == 'w')) {
		f->size = 0;
	}
	RamCursor *rc = new RamCursor{ f, ((mode[0] == 'a') ? f->size : 0) };
	return funopen(rc, ram_read, ram_write, ram_seek, ram_close);
}
}

// Opens a game file: files written earlier in a read-only session, then the
// started archive (served from memory), then the game folder.
static FILE* game_fopen(const char *fn, const char *mode)
{
	fs::IoLock io;
	if(fs::read_only()) {
		const bool writing = ((mode[0] != 'r') || strchr(mode, '+'));
		if(RamFile *f = ramfile_find(fn, false)) {
			return ramfile_open(f, mode);
		}
		if(writing) {
			RamFile *f = ramfile_find(fn, true);
			return (f ? ramfile_open(f, mode) : nullptr);
		}
	}
	if(mode[0] == 'r') {
		uint32_t size;
		if(uint8_t *data = pf_load(fn, size)) {
			FILE *mf = fmemopen(nullptr, (size + 1), "w+b");
			if(mf) {
				fwrite(data, 1, size, mf);
				rewind(mf);
			}
			free(data);
			if(mf) {
				return mf;
			}
		}
	}
	char path[256];
	fs::path(path, sizeof(path), "%s", fs::map_name(fn));
	return fopen(path, mode);
}

// master.lib keeps a single file open at a time.
static FILE *file_fp = nullptr;

int file_ropen(const char *filename)
{
	fs::IoLock io;
	file_close();
	file_fp = game_fopen(filename, "rb");
	return (file_fp != nullptr);
}

int file_create(const char *filename)
{
	fs::IoLock io;
	file_close();
	file_fp = game_fopen(filename, "wb");
	return (file_fp != nullptr);
}

// Opens for writing without truncating, creating the file if needed.
int file_append(const char *filename)
{
	fs::IoLock io;
	file_close();
	file_fp = game_fopen(filename, "r+b");
	if(!file_fp) {
		file_fp = game_fopen(filename, "w+b");
	}
	if(file_fp) {
		fseek(file_fp, 0, SEEK_END);
	}
	return (file_fp != nullptr);
}

int file_read(void far *buf, unsigned wsize)
{
	fs::IoLock io;
	return (file_fp ? int(fread(buf, 1, wsize, file_fp)) : 0);
}

int file_write(const void far *buf, unsigned wsize)
{
	fs::IoLock io;
	return (file_fp ? (fwrite(buf, 1, wsize, file_fp) == wsize) : 0);
}

void file_seek(long pos, int dir)
{
	fs::IoLock io;
	if(file_fp) {
		fseek(file_fp, pos, dir);
	}
}

void file_close(void)
{
	fs::IoLock io;
	if(file_fp) {
		fclose(file_fp);
		file_fp = nullptr;
	}
}

int file_exist(const char *filename)
{
	fs::IoLock io;
	FILE *fp = game_fopen(filename, "rb");
	if(fp) {
		fclose(fp);
		return 1;
	}
	return 0;
}

long file_size(void)
{
	fs::IoLock io;
	if(!file_fp) {
		return 0;
	}
	const long pos = ftell(file_fp);
	fseek(file_fp, 0, SEEK_END);
	const long ret = ftell(file_fp);
	fseek(file_fp, pos, SEEK_SET);
	return ret;
}
/// -----------------------

} // extern "C"

// For the other PS2 modules that need to read game files.
FILE* ps2_game_fopen(const char *fn, const char *mode)
{
	return game_fopen(fn, mode);
}
