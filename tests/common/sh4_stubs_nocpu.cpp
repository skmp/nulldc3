/*
	See sh4_stubs.cpp. The scheduler's part of the instruction loop, for builds without hw/sh4/SuperH4_impl.cpp
	(with it, that file has the real one and this file is not linked).
*/
#include "types.h"

#include "hw/sh4/sh4_interpreter.h"

extern "C" int UpdateSystem() { return 0; }
extern "C" int UpdateSystem_INTC() { return 0; }
