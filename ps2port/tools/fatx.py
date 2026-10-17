#!/usr/bin/env python3
"""Minimal FAT12/16 reader for PC-98 partitions (handles 1024-byte sectors).

usage: fatx.py <partition.img> [outdir]   # lists files; extracts if outdir given
"""
import os
import struct
import sys


def main():
    img = open(sys.argv[1], 'rb').read()
    out = sys.argv[2] if len(sys.argv) > 2 else None
    bps, spc, rsv, nfat, nroot, tot16, media, spf = struct.unpack_from('<HBHBHHBH', img, 11)
    tot = tot16 or struct.unpack_from('<I', img, 32)[0]
    root_off = (rsv + nfat * spf) * bps
    data_off = root_off + ((nroot * 32 + bps - 1) // bps) * bps
    nclus = (tot * bps - data_off) // (spc * bps)
    fat16 = nclus >= 4085
    fat = img[rsv * bps:(rsv + spf) * bps]
    csize = spc * bps
    print(f'bps={bps} spc={spc} rsv={rsv} fats={nfat} root={nroot} spf={spf} '
          f'clusters={nclus} {"FAT16" if fat16 else "FAT12"}', file=sys.stderr)

    def next_clus(c):
        if fat16:
            return struct.unpack_from('<H', fat, c * 2)[0]
        v = struct.unpack_from('<H', fat, c * 3 // 2)[0]
        return v >> 4 if c & 1 else v & 0xFFF

    end = 0xFFF8 if fat16 else 0xFF8

    def chain(c):
        while 2 <= c < end:
            yield c
            c = next_clus(c)

    def read(c, size=None):
        b = b''.join(img[data_off + (x - 2) * csize:data_off + (x - 1) * csize] for x in chain(c))
        return b[:size] if size is not None else b

    def walk(raw, path):
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if len(e) < 32 or e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue
            name = e[:8].decode('cp932', 'replace').rstrip()
            ext = e[8:11].decode('cp932', 'replace').rstrip()
            if name in ('.', '..'):
                continue
            full = os.path.join(path, name + ('.' + ext if ext else ''))
            clus, size = struct.unpack_from('<HI', e, 26)
            if e[11] & 0x10:
                print(f'{"<DIR>":>10}  {full}/')
                walk(read(clus), full)
            else:
                print(f'{size:>10}  {full}')
                if out:
                    dst = os.path.join(out, full)
                    os.makedirs(os.path.dirname(dst) or '.', exist_ok=True)
                    with open(dst, 'wb') as f:
                        f.write(read(clus, size))

    walk(img[root_off:data_off], '')


if __name__ == '__main__':
    main()
