// PS2 port: DualShock controller input. See pad.hpp.

#include "platform/ps2/pad.hpp"
#include <libpad.h>
#include <stdio.h>

namespace pad {

namespace {
// libpad requires a 256-byte, 64-byte aligned DMA buffer per port.
char pad_buf[2][256] __attribute__((aligned(64)));
bool opened[2];

// Analog stick dead zone around the 0x80 center.
enum { STICK_DEAD = 0x40 };
}

bool init()
{
	if(padInit(0) != 1) {
		printf("pad: padInit failed\n");
		return false;
	}
	for(int port = 0; port < 2; port++) {
		opened[port] = (padPortOpen(port, 0, pad_buf[port]) != 0);
	}
	return opened[0];
}

uint16_t held(int port)
{
	if(!opened[port]) {
		return 0;
	}
	const int state = padGetState(port, 0);
	if((state != PAD_STATE_STABLE) && (state != PAD_STATE_FINDCTP1)) {
		return 0;
	}
	padButtonStatus st;
	if(padRead(port, 0, &st) == 0) {
		return 0;
	}
	// libpad reports buttons active-low.
	const uint16_t b = (0xFFFF ^ st.btns);
	uint16_t ret = 0;
	if(b & PAD_UP)       { ret |= UP; }
	if(b & PAD_DOWN)     { ret |= DOWN; }
	if(b & PAD_LEFT)     { ret |= LEFT; }
	if(b & PAD_RIGHT)    { ret |= RIGHT; }
	if(b & PAD_CROSS)    { ret |= CROSS; }
	if(b & PAD_CIRCLE)   { ret |= CIRCLE; }
	if(b & PAD_SQUARE)   { ret |= SQUARE; }
	if(b & PAD_TRIANGLE) { ret |= TRIANGLE; }
	if(b & PAD_START)    { ret |= START; }
	if(b & PAD_SELECT)   { ret |= SELECT; }
	if(b & PAD_L1)       { ret |= L1; }
	if(b & PAD_R1)       { ret |= R1; }

	// Analog mode: left stick. In digital mode these bytes read 0x00/0xFF
	// garbage, so only use them when the controller reports analog mode.
	if((st.mode >> 4) == 0x7) {
		if(st.ljoy_h < (0x80 - STICK_DEAD)) { ret |= LEFT; }
		if(st.ljoy_h > (0x80 + STICK_DEAD)) { ret |= RIGHT; }
		if(st.ljoy_v < (0x80 - STICK_DEAD)) { ret |= UP; }
		if(st.ljoy_v > (0x80 + STICK_DEAD)) { ret |= DOWN; }
	}
	return ret;
}

} // namespace pad
