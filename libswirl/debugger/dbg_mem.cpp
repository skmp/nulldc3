// Stub: dbg::mem, implemented by package E3.
// dbg::sh4::StepCheck and the resume exemption read opcodes through here: SH4 steps work only with the real module.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_mem.h"

#include <cstring>

// The stub has no readable byte.
namespace dbg { namespace mem {

Region Classify(Target, u32) { return Region::Unreadable; }

u32 Read(Target, u32, u32 len, u8* out, u8* readableBits)
{
	memset(out, 0, len);
	if (readableBits)
		memset(readableBits, 0, (len + 7) / 8);
	return 0;
}

int Write(Target, u32, u32, const u8*) { return kErrNotSupported; }

} }
