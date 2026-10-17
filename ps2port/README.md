# TH03 PS2 port

A native PlayStation 2 port of Touhou 3 (東方夢時空 ~ Phantasmagoria of Dimensional Dream), built from [ReC98](https://github.com/nmlgc/ReC98)'s decompiled source (`debloated` branch). The game code is ZUN's original logic as reconstructed by ReC98. The PS2 side replaces the PC-98 hardware and libraries it ran on.

**No game data is included.** You need your own copy of TH03. All graphics, music and text are read from your files.

## Status

| Part | State |
|---|---|
| Title screen and menus (OP) | Working in PCSX2 |
| Music | Working: pre-rendered from the original PMD songs |
| Sound effects | Silent (not rendered yet) |
| Cutscenes, endings, high scores (MAINL) | Not ported yet; choosing them returns to the title |
| Gameplay (MAIN) | Not ported; ReC98 has only part of MAIN.EXE decompiled |

## How it works

- `platform/ps2/` replaces everything PC-98 specific:
  - `pc98hw`: software model of the display hardware (2 pages × 4 bitplanes, the GRCG, 16-color palette, text RAM, gaiji). The game draws into it unchanged.
  - `video`: shows the current page and the text layer through gsKit. A VBlank interrupt drives the game's VSync counters at the PC-98's 56.42 Hz.
  - `masterlib`: the parts of master.lib the game uses (palette, text, gaiji, GRCG fills, BFNT sprites, files with pack-archive lookup, …).
  - `cdg_put`, `grp_surf`, `pi_decode`: CDG sprite blitters, image surfaces, and a PI decoder that follows piloadc.
  - `audio`, `snd_game`: streams the pre-rendered songs with their loop points and PMD-style fades, behind the game's PMD driver interface.
  - `pad`: DualShock input. Cross = shot/confirm, Circle = bomb, Start = OK, Triangle = cancel, Select = Q; controller 2 = player 2.
  - `main`: IOP modules, the resident structure and YUME.CFG (what ZUN.COM -5 did), and the loop that replaces `execl()` entry point switching.
- Elsewhere in the tree, changes are wrapped in `#if defined(PLATFORM_PS2)`.

## Building

Requirements: [ps2dev](https://github.com/ps2dev/ps2dev) (ps2sdk + gsKit) with `PS2DEV`, `PS2SDK` and `GSKIT` set.

```sh
make -f Makefile.ps2        # → obj-ps2/th03.elf
```

`ps2port/check.sh <file.cpp>` syntax-checks a single source file with the PS2 compiler.

## Preparing your game files

Put these in a folder (default: `../game` next to this repository, or set `TH03_GAME`):

1. `installed/夢時空1.DAT` and `installed/夢時空2.DAT` from your TH03 installation.
   - If you only have a `.hdi` disk image: strip its 4096-byte header, cut out the partition, and use `ps2port/tools/fatx.py <partition.img> <outdir>` to extract the files. That tool reads PC-98 FAT partitions with 1024-byte sectors.
2. `PC98FONT.BIN`, from a Neko Project font bitmap (`font.bmp` / `font.tmp`):
   `python3 ps2port/tools/fontconv.py font.tmp PC98FONT.BIN`
3. `audio/*.wav` + `audio/*.wav.loop`: the songs, rendered from the `.M` files inside `夢時空1.DAT`:
   - Unpack the archive: `python3 ps2port/tools/pfx.py 夢時空1.DAT data/`
   - Build [pmdmini](https://github.com/mistydemeo/pmdmini) with `ps2port/tools/pmd2wav/pmdwin-loop-tracking.patch` applied (it reports per-channel loops), then build `pmd2wav.c` against it.
   - For each song: `pmd2wav data/XX.M audio/XX.wav 44100 <folder with 2608_*.WAV>`.
     The YM2608 rhythm samples (`2608_BD.WAV`, `2608_SD.WAV`, …) are needed for drums.

Then:

```sh
ps2port/deploy.sh           # → ../deploy: th03.elf, YUME1.DAT, YUME2.DAT, PC98FONT.BIN, audio/
```

## Running

- **PCSX2:** enable *Settings → Emulation → Enable Host Filesystem*, then boot `deploy/th03.elf`. The game reads its files from the ELF's folder.
- **Real PS2:** copy the deploy folder to USB and start `th03.elf` from a loader such as uLaunchELF. Untested so far.

## Other tools

- `ps2port/pi_test.cpp`: host-side check of the PI decoder against `.PI` files.
- `ps2port/tools/ghidra/DecompileAll.java`: Ghidra headless script that decompiles every function of an executable and reports decompiler quality metrics.
