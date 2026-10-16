/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	Nice helper #defines
*/

#pragma once
#include "types.h"
#include "hw/RegisterStruct.h"
#include "hw/StaticForward.h"
#include "hw/sh4/sh4_mmio.h"
#include <array>

struct SystemBus : MMIODevice {
	array<RegisterStruct, 0x540> sb_regs;

    virtual void RegisterRIO(void* context, u32 reg_addr, RegIO flags, RegReadAddrFP* rf = nullptr, RegWriteAddrFP* wf = nullptr) = 0;

    // Debugger access: the 32-bit register at reg_addr (a multiple of 4 in SB_BASE .. SB_BASE + 0x14FC) as a
    // guest read gives it, read without side effects. A register that holds data comes from sb_regs; one
    // behind a read function comes from a reader that changes nothing (SB_FFST without advancing its poll
    // counter, SB_ISTNRM as the interrupt controller composes it).
    // Returns false for a slot that is no register, for a write-only register and for a function-backed
    // register that has no such reader. The default is that of a device that gives the debugger nothing.
    // Called on the emulator thread outside a register access, or by a thread that holds the debugger's
    // lifetime lock while no emulator thread exists.
    virtual bool DebugRead(u32 reg_addr, u32* value) { return false; }
};

#include "sb_regs.h"