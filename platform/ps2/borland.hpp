// PS2 port: replacements for the Borland C++ runtime functions that ZUN's code
// uses (from <conio.h>, <dos.h>, <mem.h>, <process.h>, <mbstring.h>, ...).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// <mbctype.h> / <mbstring.h>: Shift-JIS helpers
static inline int _ismbblead(unsigned int c) {
	c &= 0xFF;
	return (((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC)));
}
static inline int _ismbbkana(unsigned int c) {
	c &= 0xFF;
	return ((c >= 0xA1) && (c <= 0xDF));
}
// Shift-JIS → JIS X 0208. Returns 0 for invalid input, like Borland's.
static inline unsigned int _mbcjmstojis(unsigned int c) {
	unsigned int hi = ((c >> 8) & 0xFF);
	unsigned int lo = (c & 0xFF);
	if(!_ismbblead(hi) || (lo < 0x40) || (lo == 0x7F) || (lo > 0xFC)) {
		return 0;
	}
	hi -= ((hi <= 0x9F) ? 0x71 : 0xB1);
	hi = ((hi * 2) + 1);
	if(lo > 0x7F) {
		lo--;
	}
	if(lo >= 0x9E) {
		lo -= 0x7D;
		hi++;
	} else {
		lo -= 0x1F;
	}
	return ((hi << 8) | lo);
}

// Borland intrinsics
static inline uint16_t __rotr__(uint16_t v, int n) {
	n &= 15;
	return uint16_t((v >> n) | (v << ((16 - n) & 15)));
}
static inline uint16_t __rotl__(uint16_t v, int n) {
	n &= 15;
	return uint16_t((v << n) | (v >> ((16 - n) & 15)));
}

// <conio.h>
int getch(void);  // waits for a button press
