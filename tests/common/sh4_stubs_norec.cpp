/*
	See sh4_stubs.cpp. The block manager's part of the memory map code, for builds without the recompiler
	(with it, hw/sh4/dyna/blockmanager.cpp has the real one and this file is not linked).
*/
#include "types.h"

void bm_vmem_pagefill(void** ptr, u32 size_bytes) { }
