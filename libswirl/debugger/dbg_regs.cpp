// Stub: the register readers and writers, implemented by package E3.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_regs.h"

#include "dbg_emu.h"

#include "hw/aica/aica_mmio.h"
#include "hw/gdrom/gdromv3.h"
#include "hw/holly/sh4_mem_area0.h"

// The stub reads nothing: every value is "n/a".
namespace dbg { namespace regs {

bool Read(const Node&, uint32_t, uint64_t*, char*, uint32_t) { return false; }
int Write(const Node&, uint32_t, uint64_t) { return kErrNotSupported; }

} }

// The device accessors that are free functions give nothing either. Their place is hw/aica/aica_mmio.cpp,
// hw/gdrom/gdromv3.cpp and hw/holly/sh4_mem_area0.cpp.
bool libAICA_DebugGetRTC(u32*, bool*) { return false; }
bool gdrom_DebugGetState(GDRomDebugState*) { return false; }
bool area0_DebugRom(const u8**, u32*) { return false; }
bool area0_DebugNvmem(const u8**, u32*) { return false; }
