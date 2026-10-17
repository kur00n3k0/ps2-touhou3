#!/usr/bin/env python3
"""Convert a Neko Project font bitmap (font.bmp / font.tmp, 2048×2048 1bpp)
into PC98FONT.BIN for the TH03 PS2 port.

Layout of the source bitmap (from NP2kai font/fontpc98.c), top-down, with
1 = background (bits are inverted):
  ANK 8×16 glyph c:     rows 0..15, byte column c
  Kanji cell (hi, lo):  rows lo*16 .. +15, byte columns (hi-0x20)*2, +1
                        (hi, lo = JIS bytes 0x21..0x7F). This is the layout of
                        Neko Project 21's generated font.tmp, verified by
                        locating glyphs; NP2kai's fontpc98.c reads rows from
                        (lo-0x20)*16 instead, for its own font.bmp files.

Output (PC98FONT.BIN), 1 = foreground:
  0x00000  256 × 16 bytes   ANK 8×16, one byte per row
  0x01000  128 × 128 × 32   kanji, indexed [hi & 0x7F][lo & 0x7F],
                            each 16 rows × (left byte, right byte)

usage: fontconv.py <font.tmp|font.bmp> <PC98FONT.BIN>
"""
import struct
import sys

W = H = 2048
LINE = W // 8


def main():
    d = open(sys.argv[1], 'rb').read()
    if d[:2] != b'BM':
        sys.exit('not a BMP file')
    off = struct.unpack_from('<I', d, 10)[0]
    w, h, _planes, bpp = struct.unpack_from('<iiHH', d, 18)
    if (w, abs(h), bpp) != (W, H, 1):
        sys.exit(f'expected a {W}x{H} 1bpp bitmap, got {w}x{h} {bpp}bpp')
    bottom_up = h > 0
    px = d[off:off + LINE * H]

    def row(y):  # top-down row y, inverted to 1 = foreground
        r = (H - 1 - y) if bottom_up else y
        return bytes(b ^ 0xFF for b in px[r * LINE:(r + 1) * LINE])

    rows = [row(y) for y in range(H)]
    out = bytearray(0x1000 + 128 * 128 * 32)
    for c in range(256):
        for y in range(16):
            out[c * 16 + y] = rows[y][c]
    for hi in range(0x21, 0x80):
        for lo in range(0x21, 0x80):
            base = 0x1000 + ((hi << 7) | lo) * 32
            x = (hi - 0x20) * 2
            for y in range(16):
                r = rows[lo * 16 + y]
                out[base + y * 2] = r[x]
                out[base + y * 2 + 1] = r[x + 1]
    open(sys.argv[2], 'wb').write(out)
    print(f'wrote {sys.argv[2]} ({len(out)} bytes)')


if __name__ == '__main__':
    main()
