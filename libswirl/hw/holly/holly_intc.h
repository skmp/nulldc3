/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_mmio.h"

void asic_RaiseInterrupt(HollyInterruptID inter);
void asic_CancelInterrupt(HollyInterruptID inter);

struct ASIC: MMIODevice {
    virtual void RaiseInterrupt(HollyInterruptID inter) = 0;
    virtual void CancelInterrupt(HollyInterruptID inter) = 0;

    // Debugger access: SB_ISTNRM as a guest read gives it (bits 30 and 31 tell whether SB_ISTEXT / SB_ISTERR
    // have a bit set), read without side effects. SB_ISTEXT, SB_ISTERR and the SB_IML* masks are data of
    // the system bus (SystemBus::DebugRead). The default is that of a device that gives the debugger nothing.
    // Called on the emulator thread outside a register access, or by a thread that holds the debugger's
    // lifetime lock while no emulator thread exists.
    virtual bool DebugReadISTNRM(u32* value) { return false; }
};

ASIC* Create_ASIC(SystemBus* sb);