/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

struct rei_host_context_t {
#if HOST_CPU != CPU_GENERIC
	unat pc;
#endif
};