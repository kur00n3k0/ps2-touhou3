#!/bin/sh
# Assembles a runnable TH03 PS2 folder from the build and the user's game files.
#   port/deploy.sh [target_dir]      (default: deploy/ next to port/)
# Contents: th03.elf, YUME1.DAT/YUME2.DAT (the original 夢時空 archives under
# ASCII names), PC98FONT.BIN, and audio/*.wav(.loop).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/deploy}"
GAME="$ROOT/game"

mkdir -p "$OUT/audio"
cp "$ROOT/rec98-ps2/obj-ps2/th03.elf" "$OUT/"
cp "$GAME/installed/夢時空1.DAT" "$OUT/YUME1.DAT"
cp "$GAME/installed/夢時空2.DAT" "$OUT/YUME2.DAT"
cp "$GAME/PC98FONT.BIN" "$OUT/"
cp "$GAME"/audio/*.wav "$GAME"/audio/*.wav.loop "$OUT/audio/"
echo "deployed to $OUT"
