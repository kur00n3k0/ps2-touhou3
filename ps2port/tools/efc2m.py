#!/usr/bin/env python3
"""Wrap each FM sound effect of a PMD .EFC bank into a one-part PMD song (.M),
so it can be rendered to WAV with pmd2wav like the music.

.EFC layout (TH03's YUME.EFC): 128 little-endian offsets. Entries 0-126
point to compiled effect sequences (unused ones to a single 0x80 "end" byte),
entry 127 points to the FM instrument table (26-byte entries: instrument
number + 25 bytes).

PMD plays an FM effect on FM channel 6 (PMD86/PMDB2), temporarily masking the
song's part F there. The generated song is a copy of the .EFC with its first
26 bytes replaced by a song header:
  words 0-10  part A-K offsets: the effect for part F, an empty 0x80 elsewhere
  word 11     rhythm table offset (unused)
  word 12     instrument table offset
Keeping the sequences at their original offsets keeps the loop offsets inside
them valid (PMD resolves them relative to the start of the data either way).

usage: efc2m.py <bank.efc> <outdir>     → <outdir>/SE00.M, SE01.M, ...
"""
import os
import struct
import sys

PART_F = 5


def main():
    efc = bytearray(open(sys.argv[1], 'rb').read())
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    table = [struct.unpack_from('<H', efc, i)[0] for i in range(0, 256, 2)]
    offs, voices = table[:127], table[127]
    # Unused parts point to an 0x80 "end" byte placed right after the header
    # (offset 26). Players check that part A starts there to recognize a .M
    # file, and PMDWin then reads word 12 as the instrument table pointer.
    empty = 26
    if min(offs) < 27:
        sys.exit('effect data overlaps the song header')

    count = 0
    for n, start in enumerate(offs):
        if efc[start] == 0x80:
            continue
        song = bytearray(efc)
        song[empty] = 0x80
        header = [empty] * 11 + [empty, voices]
        header[PART_F] = start
        struct.pack_into('<13H', song, 0, *header)
        # PMD .M files start with one byte (0 = OPN/OPNA) before the data.
        with open(os.path.join(out, f'SE{n:02d}.M'), 'wb') as f:
            f.write(b'\x00' + song)
        count += 1
    print(f'wrote {count} effect songs to {out}')


if __name__ == '__main__':
    main()
