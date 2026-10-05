/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

/*
	Runs that nobody watches: headless mode, the conditions that end a run, and the line a run prints when it
	ends. docs/nulldc3/automated-runs.md has the keys and the format of the line.

*/

// Reads the keys. Once, when the settings are loaded and before anything saves one
void runctl_configure();

// config:Headless. No window, no GL context, no host input
bool runctl_headless();

// Emulator thread, once per vblank, after the frame of the vblank is queued
void runctl_vblank();

// Emulator thread: the guest program returned to the BIOS.
// True: the run ends here, and the CPU is stopped for the caller. So in headless mode and with any of the keys
// that end a run; false leaves the machine to the caller, for a person at the window
bool runctl_guest_exit();

// A frame on its way to the screen: width x height pixels, 4 bytes each, B G R A in memory.
// Called on the thread that presents: the thread of the user interface with a window, the emulator thread
// headless. The frame is kept for the exit line, which the emulator thread reads only after WaitQueueEmpty().
// True: there is no GL context, and the caller is done with the frame
bool runctl_present(const u32* pixels, int width, int height);

// The run cannot go on (nothing to run, no renderer). Thread of the user interface
void runctl_fail(const char* reason);

// What main returns
int runctl_exit_code();
