// Stub: dbg::core, implemented by package E5.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_core.h"

#include <cstdio>
#include <cstdlib>

// The stub starts no thread, has no handler and builds no tick.
namespace dbg { namespace core {

bool Start(net::Server*) { return false; }
void Stop() { }
net::Handler* NetHandler() { return nullptr; }
void OnRunState(const emu::RunState&) { }

std::string BuildTickText(bool)
{
	fprintf(stderr, "debugger: dbg::core::BuildTickText reached, but dbg::core is a stub\n");
	abort();
}

} }
