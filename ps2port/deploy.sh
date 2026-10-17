#!/bin/sh
# Assembles a runnable TH03 PS2 folder from the build and your own game files.
#
#   ps2port/deploy.sh [target_dir]
#
# Environment:
#   TH03_GAME  folder with your game files (default: ../game next to the repo):
#                installed/夢時空1.DAT, installed/夢時空2.DAT  (from your TH03 install)
#                PC98FONT.BIN                                   (tools/fontconv.py)
#                audio/*.wav, audio/*.wav.loop                  (tools/pmd2wav)
#                audio/se/SE*.wav                               (tools/efc2m.py + pmd2wav)
# Output (default: ../deploy next to the repo), all upper case to match ISO9660:
# TH03.ELF, YUME1.DAT, YUME2.DAT, PC98FONT.BIN, AUDIO/*.WAV, AUDIO/SE/SE*.WAV,
# LOOP/*.LOP (loop points).
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
GAME="${TH03_GAME:-$REPO/../game}"
OUT="${1:-$REPO/../deploy}"

rm -rf "$OUT/audio" "$OUT/AUDIO" "$OUT/LOOP"
mkdir -p "$OUT/AUDIO/SE" "$OUT/LOOP"
cp "$REPO/obj-ps2/th03.elf" "$OUT/TH03.ELF"
cp "$GAME/installed/夢時空1.DAT" "$OUT/YUME1.DAT"
cp "$GAME/installed/夢時空2.DAT" "$OUT/YUME2.DAT"
cp "$GAME/PC98FONT.BIN" "$OUT/"
for f in "$GAME"/audio/*.wav; do
	b="$(basename "$f" .wav | tr a-z A-Z)"
	cp "$f" "$OUT/AUDIO/$b.WAV"
	[ -f "$f.loop" ] && cp "$f.loop" "$OUT/LOOP/$b.LOP"
done
for f in "$GAME"/audio/se/*.wav; do
	cp "$f" "$OUT/AUDIO/SE/$(basename "$f" .wav | tr a-z A-Z).WAV"
done
echo "deployed to $OUT"
