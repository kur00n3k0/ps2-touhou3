#!/bin/sh
# Syntax-check ReC98 sources with the PS2 compiler and summarize errors.
# usage: ps2port/check.sh file.cpp... (paths relative to the repository root)
cd "$(dirname "$0")/.." || exit 1
export PATH="$PATH:$HOME/ps2dev/ee/bin"
for f in "$@"; do
  mips64r5900el-ps2-elf-g++ -fsyntax-only -std=gnu++17 -fpermissive -w \
    -D_EE -DPLATFORM_PS2 -DGAME=3 -DMASTER_FAR -include platform.h \
    -I. -isystem platform/ps2/include -isystem $HOME/ps2dev/ps2sdk/ee/include -isystem $HOME/ps2dev/ps2sdk/common/include "$f" 2>&1
done | grep -E 'error' | sed -E 's/^[^:]+:[0-9]+:[0-9]+: //' | sort | uniq -c | sort -rn
