/*
	The least of an emulator the SH4 core needs around it: the context block, the SuperH4 object the opcode
	handlers reach their module registers through, and an empty memory map.
*/
#pragma once
#include "types.h"

//Call once, before anything touches the SH4 context. Unaligned accesses end the guarded call (test_guard.h)
void sh4_env_init();
