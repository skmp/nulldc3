// Stub: dbg::symbols, implemented by package E5.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_symbols.h"

// The stub keeps no symbols.
namespace dbg { namespace symbols {

void OnElfLoaded(const void*, size_t) { }
void Clear() { }
size_t Count() { return 0; }
bool Lookup(uint32_t, const char**, uint32_t*) { return false; }
bool Find(const char*, uint32_t*) { return false; }

bool Format(uint32_t, char* out, size_t n)
{
	if (n != 0)
		out[0] = 0;
	return false;
}

} }
