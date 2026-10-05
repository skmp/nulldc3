/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

/*
	Runs that nobody watches: the conditions that end a run, and the line a run prints when it ends.
	docs/nulldc3/automated-runs.md has the keys and the format of the line.

*/

// Reads the keys. Once, when the settings are loaded and before anything saves one
void runctl_configure();

// Emulator thread, once per vblank, after the frame of the vblank is queued
void runctl_vblank();

// Emulator thread: the guest program returned to the BIOS.
// True: the run ends here, and the CPU is stopped for the caller
bool runctl_guest_exit();
