/*
	This file is part of libswirl
*/
#pragma once

#include <stddef.h>

#include "types.h"

// The unwind data of the recompiler's code region on Windows (win_jit_unwind.cpp).

// The region is there: its unwind data goes into the page behind it (4096 bytes, readable and writable), and the
// system asks this file for the frame of a pc in it from then on
void win_jit_code_region_prepared(void* region, size_t size);

// The frame of the main loop (which 0) or of RunBlock (which 1), as rec_x64.cpp writes it: its code, the end of
// every instruction of its prologue, its size below the eight pushes
void win_jit_unwind_frame(int which, const void* begin, size_t size, const u8* prolog, u32 frame);

// Whether an address is in the code region
bool win_in_code_region(const void* address);
