// Stub: the register readers and writers, implemented by package E3.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_regs.h"

#include "dbg_emu.h"

// The stub reads nothing: every value is "n/a".
namespace dbg { namespace regs {

bool Read(const Node&, uint32_t, uint64_t*, char*, uint32_t) { return false; }
int Write(const Node&, uint32_t, uint64_t) { return kErrNotSupported; }

} }
