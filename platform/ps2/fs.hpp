// PS2 port: game file locations.
//
// All game files live in one folder next to the executable, e.g.
//   host:/…/th03/   when started from PCSX2 with host filesystem access
//   mass:/TH03/     on a USB drive on real hardware
// That folder holds the original data archives (renamed to ASCII, see
// map_name()), the YUME.CFG settings file, and the converted audio in audio/.

#ifndef PLATFORM_PS2_FS_HPP
#define PLATFORM_PS2_FS_HPP

#include <stddef.h>

namespace fs {

// Derives the game folder from the path the ELF was started with.
void init(const char *argv0);

// Formats a path relative to the game folder into [buf].
void path(char *buf, size_t size, const char *fmt, ...)
	__attribute__((format(printf, 3, 4)));

// Maps a file name as used by the original game (Shift-JIS, any case) to the
// name of the file in the game folder.
const char *map_name(const char *game_fn);

} // namespace fs

#endif /* PLATFORM_PS2_FS_HPP */
