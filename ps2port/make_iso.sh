#!/bin/sh
# Packs a deployed TH03 PS2 folder (see deploy.sh) into a bootable ISO for
# Open PS2 Loader (OPL) or a burned DVD.
#
#   ps2port/make_iso.sh [deploy_dir] [output.iso]
#
# Layout: ISO9660 level 1 (upper case 8.3 names), SYSTEM.CNF booting
# cdrom0:\TOHO_030.03;1 (the ELF), plus the game files. The ID "TOHO_030.03"
# is made up so it can't collide with a real game's ID in OPL.
# For OPL, copy the ISO into the DVD/ folder of your USB drive / SMB share /
# HDD as "TOHO_030.03.Touhou 3.iso".
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
DEPLOY="${1:-$REPO/../deploy}"
ISO="${2:-$REPO/../TOHO_030.03.Touhou 3.iso}"
ELF_ID="TOHO_030.03"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

cp "$DEPLOY/TH03.ELF" "$STAGE/$ELF_ID"
cp "$DEPLOY/YUME1.DAT" "$DEPLOY/YUME2.DAT" "$DEPLOY/PC98FONT.BIN" "$STAGE/"
cp -r "$DEPLOY/AUDIO" "$DEPLOY/LOOP" "$STAGE/"
printf 'BOOT2 = cdrom0:\\%s;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' "$ELF_ID" > "$STAGE/SYSTEM.CNF"

# Plain ISO9660 without Rock Ridge/Joliet: the PS2's CD file lookup only
# searches the first 2048-byte sector of each directory, so directory records
# must stay small.
rm -f "$ISO"
xorriso -outdev "$ISO" -rockridge off -joliet off \
	-compliance iso_9660_level=1 -system_id PLAYSTATION -volid TH03 \
	-application_id "TOUHOU 3 PS2 PORT" -map "$STAGE" / 2>&1 | tail -1

# Verify that every directory fits in one sector.
python3 -I - "$ISO" <<'PY'
import struct, sys
f = open(sys.argv[1], 'rb')
def rd(lba, n): f.seek(lba * 2048); return f.read(n)
pvd = rd(16, 2048)
bad = []
def walk(lba, size, path):
    if size > 2048:
        bad.append((path, size))
    d = rd(lba, size); i = 0
    while i < len(d):
        ln = d[i]
        if ln == 0:
            i = ((i // 2048) + 1) * 2048
            continue
        nl = d[i + 32]; name = d[i + 33:i + 33 + nl]
        if (d[i + 25] & 2) and name not in (b'\0', b'\1'):
            walk(struct.unpack_from('<I', d, i + 2)[0], struct.unpack_from('<I', d, i + 10)[0],
                 path + name.decode() + '/')
        i += ln
walk(struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0], '/')
if bad:
    sys.exit('directories larger than one sector (the PS2 won\'t find all files): %s' % bad)
print('all directories fit in one sector')
PY
echo "built $ISO"
