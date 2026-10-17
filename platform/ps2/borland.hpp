// PS2 port: replacements for the Borland C++ runtime functions that ZUN's code
// uses (from <conio.h>, <dos.h>, <mem.h>, <process.h>, <mbstring.h>, ...).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// <conio.h>
int getch(void);  // waits for a button press
