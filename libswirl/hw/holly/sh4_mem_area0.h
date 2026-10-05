/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

struct SuperH4;

void map_area0_init(SuperH4* sh4);
void map_area0(SuperH4* sh4, u32 base);

// Debugger access to the two memories at the start of area 0: the boot ROM at 0x00000000 and the flash
// memory (battery-backed RAM on arcade boards) at 0x00200000. *data / *size is the backing store, size a
// power of two: the byte at guest address a of the region is data[(a - region start) & (size - 1)].
// Reading it has no side effect. Returns false when the store does not exist.
// Called on the emulator thread, or by a thread that holds the debugger's lifetime lock while no emulator
// thread exists; the pointer is not kept beyond that.
bool area0_DebugRom(const u8** data, u32* size);
bool area0_DebugNvmem(const u8** data, u32* size);
