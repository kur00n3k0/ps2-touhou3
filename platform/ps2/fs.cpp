// PS2 port: game file locations. See fs.hpp.

#include "platform/ps2/fs.hpp"
#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace fs {

static char base[256] = "host:";

void init(const char *argv0)
{
	if(!argv0 || !argv0[0]) {
		return;
	}
	// Keep everything up to and including the last path separator.
	strncpy(base, argv0, (sizeof(base) - 1));
	char *cut = nullptr;
	for(char *p = base; *p; p++) {
		if((*p == '/') || (*p == '\\') || (*p == ':')) {
			cut = p;
		}
	}
	if(cut) {
		cut[1] = '\0';
	}
	// Some loaders pass "host:th03.elf" without a slash, which leaves
	// "host:" – also fine.
	printf("fs: game folder is %s\n", base);
}

void path(char *buf, size_t size, const char *fmt, ...)
{
	const size_t n = snprintf(buf, size, "%s", base);
	va_list ap;
	va_start(ap, fmt);
	if(n < size) {
		vsnprintf((buf + n), (size - n), fmt, ap);
	}
	va_end(ap);
}

// The data archives are named in Shift-JIS on the original disk. PS2 file
// systems don't reliably support that, so deploy.sh copies them to ASCII
// names. Everything else in the game uses ASCII 8.3 names already.
struct Rename {
	const char *sjis;
	const char *ascii;
};
static const Rename RENAMES[] = {
	{ "\x96\xB2\x8E\x9E\x8B\xF3" "1.DAT", "YUME1.DAT" }, // 夢時空1.DAT
	{ "\x96\xB2\x8E\x9E\x8B\xF3" "2.DAT", "YUME2.DAT" }, // 夢時空2.DAT
};

static bool name_eq(const char *a, const char *b)
{
	for(; *a && *b; a++, b++) {
		if(toupper(uint8_t(*a)) != toupper(uint8_t(*b))) {
			return false;
		}
	}
	return (*a == *b);
}

const char *map_name(const char *game_fn)
{
	for(const Rename& r : RENAMES) {
		if(name_eq(game_fn, r.sjis)) {
			return r.ascii;
		}
	}
	return game_fn;
}

} // namespace fs
