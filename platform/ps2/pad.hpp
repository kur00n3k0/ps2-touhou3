// PS2 port: DualShock controller input.

#ifndef PLATFORM_PS2_PAD_HPP
#define PLATFORM_PS2_PAD_HPP

#include <stdint.h>

namespace pad {

enum : uint16_t {
	UP = 0x0001,
	DOWN = 0x0002,
	LEFT = 0x0004,
	RIGHT = 0x0008,
	CROSS = 0x0010,
	CIRCLE = 0x0020,
	SQUARE = 0x0040,
	TRIANGLE = 0x0080,
	START = 0x0100,
	SELECT = 0x0200,
	L1 = 0x0400,
	R1 = 0x0800,
};

// Loads the IOP controller modules and opens both ports.
bool init();

// Currently held buttons on controller [port] (0 or 1), as a mask of the
// values above. The left analog stick also counts as the D-pad.
uint16_t held(int port);

} // namespace pad

#endif /* PLATFORM_PS2_PAD_HPP */
