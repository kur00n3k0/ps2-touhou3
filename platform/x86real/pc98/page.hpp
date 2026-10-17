// VRAM page flipping
// ------------------

#if defined(PLATFORM_PS2)
#include "platform/ps2/pc98hw.hpp"
#define page_show(page)  	(pc98::page_shown = ((page) & 1))
#define page_access(page)	(pc98::page_accessed = ((page) & 1))
#else
#include "x86real.h"

#define page_show(page)  	_outportb_(0xA4, page)
#define page_access(page)	_outportb_(0xA6, page)
#endif
