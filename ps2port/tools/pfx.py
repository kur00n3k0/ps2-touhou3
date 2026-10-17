#!/usr/bin/env python3
"""Extract TH03/TH04 master.lib "pf" archives (e.g. 夢時空1.DAT).

Format (from ReC98's libs/master.lib/pfint21.asm and th03/formats/pfopen.asm):
  header (16 bytes): u16 entries_size, u16 unk, u16 entries_count, u16 key, 8x0
  entry table (entries_size bytes), decrypted with a rolling key:
      b ^= k; k = (k - b) & 0xFF   (b = decrypted byte)
  entry (32 bytes): u16 type, u8 aux(xor key), char name[13], u16 packsize,
                    u16 orgsize, u32 offset, 8x reserved
  type 0xF388 = stored, 0x9595 = RLE (a repeated byte is followed by a count of
  extra repeats). If aux != 0, every stored byte is XORed with aux first.

usage: pfx.py <archive> [outdir]    # lists; extracts when outdir is given
"""
import os
import struct
import sys

PF_NONE, PF_LEN = 0xF388, 0x9595


def entries(data):
    size, _unk, count, key = struct.unpack_from('<HHHH', data, 0)
    tbl = bytearray(data[16:16 + size])
    k = key & 0xFF
    for i in range(len(tbl)):
        tbl[i] ^= k
        k = (k - tbl[i]) & 0xFF
    for i in range(0, len(tbl), 32):
        typ, aux = struct.unpack_from('<HB', tbl, i)
        if typ == 0:
            break
        name = bytes(tbl[i + 3:i + 16]).split(b'\0')[0].decode('cp932', 'replace')
        psz, osz, ofs = struct.unpack_from('<HHI', tbl, i + 16)
        yield name, typ, aux, psz, osz, ofs


def unpack(data, typ, aux, psz, osz, ofs):
    raw = bytes(b ^ aux for b in data[ofs:ofs + psz]) if aux else data[ofs:ofs + psz]
    if typ == PF_NONE:
        return raw[:osz]
    if typ != PF_LEN:
        raise ValueError(f'unknown pack type {typ:#x}')
    out, ch, i = bytearray(), -1, 0
    while i < len(raw) and len(out) < osz:
        x = raw[i]
        i += 1
        if x != ch:
            ch = x
            out.append(x)
            continue
        out.append(x)  # second byte of the pair
        if i < len(raw):
            out.extend([x] * raw[i])
            i += 1
        # pf_ch stays x, so the same byte right after a run starts a new pair.
    return bytes(out[:osz])


def main():
    data = open(sys.argv[1], 'rb').read()
    out = sys.argv[2] if len(sys.argv) > 2 else None
    for name, typ, aux, psz, osz, ofs in entries(data):
        kind = {PF_NONE: 'raw', PF_LEN: 'rle'}.get(typ, hex(typ))
        print(f'{name:13} {kind:4} key={aux:02x} packed={psz:6} size={osz:6} @ {ofs:#x}')
        if out:
            os.makedirs(out, exist_ok=True)
            with open(os.path.join(out, name), 'wb') as f:
                f.write(unpack(data, typ, aux, psz, osz, ofs))


if __name__ == '__main__':
    main()
