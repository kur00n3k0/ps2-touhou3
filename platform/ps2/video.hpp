// PS2 port: display output and VSync timing.
//
// A VBlank interrupt wakes a video thread, which runs at a higher priority
// than the game. On each emulated PC-98 VSync (56.42 Hz, derived from the
// TV's 59.94 Hz), it increments master.lib's VSync counters, runs the game's
// VSync procedure, and draws the shown VRAM page plus the text layer.

#ifndef PLATFORM_PS2_VIDEO_HPP
#define PLATFORM_PS2_VIDEO_HPP

namespace video {

bool init();

// Runs [func] once on the next emulated VSync, on the video thread.
void run_on_next_vsync(void (*func)(void));

} // namespace video

#endif /* PLATFORM_PS2_VIDEO_HPP */
