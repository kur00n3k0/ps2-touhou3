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
# Output (default: ../deploy next to the repo): th03.elf, YUME1.DAT, YUME2.DAT,
# PC98FONT.BIN and audio/.
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
GAME="${TH03_GAME:-$REPO/../game}"
OUT="${1:-$REPO/../deploy}"

mkdir -p "$OUT/audio/se"
cp "$REPO/obj-ps2/th03.elf" "$OUT/"
cp "$GAME/installed/夢時空1.DAT" "$OUT/YUME1.DAT"
cp "$GAME/installed/夢時空2.DAT" "$OUT/YUME2.DAT"
cp "$GAME/PC98FONT.BIN" "$OUT/"
cp "$GAME"/audio/*.wav "$GAME"/audio/*.wav.loop "$OUT/audio/"
cp "$GAME"/audio/se/*.wav "$OUT/audio/se/"
echo "deployed to $OUT"
