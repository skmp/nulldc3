#pragma once
#include "types.h"

struct Gate
{
	const char* name;
	const char* what;
	void (*run)();
};

extern const Gate gates[];
extern const u32 gate_count;

//One check of the running gate: counts it and prints its line. Returns ok
bool check(bool ok, const char* fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(__printf__, 2, 3)))
#endif
	;

//A line of the running gate that is not a check
void note(const char* fmt, ...)
#if defined(__GNUC__)
	__attribute__((format(__printf__, 1, 2)))
#endif
	;
