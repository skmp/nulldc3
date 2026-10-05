/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	Highly inefficient and boring interpreter. Nothing special here
*/

#include "types.h"

#include "../sh4_interpreter.h"
#include "../sh4_interrupts.h"
#include "../sh4_opcode_list.h"
#include "../sh4_core.h"
#include "hw/sh4/sh4_mem.h"
#include "debugger/debugger.h"


#define CPU_RATIO      (8)

//uh uh
#define GetN(str) ((str>>8) & 0xf)
#define GetM(str) ((str>>4) & 0xf)

void ExecuteOpcode(u16 op)
{
	if (sr.FD == 1 && OpDesc[op]->IsFloatingPoint())
		RaiseFPUDisableException();
	OpPtr[op](op);
}

//TODO : Check for valid delayslot instruction
void ExecuteDelayslot()
{
#if !defined(NO_MMU)
    try {
#endif
        u32 addr = next_pc;
        next_pc += 2;
        u32 op = IReadMem16(addr);

        if (op != 0)	// Looney Tunes: Space Race hack
            ExecuteOpcode(op);
#if !defined(NO_MMU)
    }
    catch (SH4ThrownException& ex) {
        ex.epc -= 2;
        if (ex.expEvn == 0x800)	// FPU disable exception
            ex.expEvn = 0x820;	// Slot FPU disable exception
        else if (ex.expEvn == 0x180)	// Illegal instruction exception
            ex.expEvn = 0x1A0;			// Slot illegal instruction exception
        //printf("Delay slot exception\n");
        throw ex;
    }
#endif
}

void ExecuteDelayslot_RTE()
{
#if !defined(NO_MMU)
    try {
#endif
        ExecuteDelayslot();
#if !defined(NO_MMU)
    }
    catch (SH4ThrownException& ex) {
        msgboxf("Exception in RTE delay slot", MBX_ICONERROR);
    }
#endif
}

// The slice counter: cycles left until the next UpdateSystem. It is kept when the loop is left and entered
// again, so that leaving and entering it is not visible to the guest; a machine reset sets it.
static s32 interp_slice = SH4_TIMESLICE;

// The slot of a delayed conditional branch that was not taken runs as an instruction of its own. It belongs to
// its branch: the debugger check of the pair was made in front of the branch, none is made in front of the slot.
// dbg_slot_pc is the address of such a slot from the moment its branch ran, and holds for the instruction that
// runs next only, and only if that is the slot: an interrupt or exception accepted in between returns to the
// slot as to any instruction, and so does whatever else arrives at its address later.
#define DBG_NO_SLOT 0xFFFFFFFF
static u32 dbg_slot_pc = DBG_NO_SLOT;

// bt/s with T clear and bf/s with T set do not branch: their handlers leave the slot to the loop
static inline bool SlotLeftToLoop(u32 op)
{
    return ((op & 0xFF00) == 0x8D00 && sr.T == 0) || ((op & 0xFF00) == 0x8F00 && sr.T != 0);
}

struct SH4IInterpreter : SuperH4Backend {
    ~SH4IInterpreter() { Term(); }

    // The end-of-slice work of Loop(). false: the loop ends
    static bool SliceEnd(s32& l)
    {
        l += SH4_TIMESLICE;

        if (UpdateSystem()) {
            // an interrupt that is accepted here comes in front of a slot that was left to the loop
            if (UpdateINTC())
                dbg_slot_pc = DBG_NO_SLOT;
            if (!sh4_int_bCpuRun)
                return false;
        }

        return true;
    }

    // One instruction of the debug loop, with the debugger check in front of it.
    // false: the check stopped and nothing ran, or the instruction ran and its flow report stopped
    static bool DebugStep(s32& l)
    {
        u32 addr = next_pc;
        next_pc += 2;
        u32 op = IReadMem16(addr);

        const bool is_slot = addr == dbg_slot_pc;

        dbg_slot_pc = DBG_NO_SLOT;

        if (!is_slot && dbg_sh4_WantsCheck(addr, (OpDesc[op]->type & Delayslot) != 0) && dbg_sh4_Check(addr, 0, 0))
            return false;	// the pc is back on the instruction, nothing was charged

        const bool slot_follows = SlotLeftToLoop(op);

        ExecuteOpcode(op);

        if (slot_follows)
            dbg_slot_pc = addr + 2;

        l -= CPU_RATIO;

        if (unlikely(dbg_sh4_stepout != 0))
        {
            // A pending step-out hears of calls and returns when they have run, with their slot. RTE reports
            // itself, in front of its interrupt check.
            if ((op & 0xF000) == 0xB000 || (op & 0xF0FF) == 0x0003 || (op & 0xF0FF) == 0x400B)	// bsr, bsrf, jsr
                dbg_sh4_CheckFlow(DBG_SH4_FLOW_CALL);
            else if (op == 0x000B)	// rts
                dbg_sh4_CheckFlow(DBG_SH4_FLOW_RTS);

            if (dbg_sh4_StopPending())
                return false;
        }

        return true;
    }

    // The loop while the debugger has an address armed or a step-out pending: Loop() with a check in front of
    // every instruction. Slices end where they end in Loop().
    void LoopDebug()
    {
        s32& l = interp_slice;

        do
        {
#if !defined(NO_MMU)
            try {
#endif
                do
                {
                    if (!DebugStep(l))
                        return;
                } while (l > 0);

                if (!SliceEnd(l))
                    break;

#if !defined(NO_MMU)
            }
            catch (SH4ThrownException& ex) {
                Do_Exception(ex.epc, ex.expEvn, ex.callVect);
                l -= CPU_RATIO * 5;	// an exception requires the instruction pipeline to drain, so approx 5 cycles
                dbg_slot_pc = DBG_NO_SLOT;
            }
#endif
        } while (true);
    }

    void Loop()
    {
        s32& l = interp_slice;

        // The loop can have been left behind the instruction that used the slice up, in front of the test of the
        // counter (a flow report of the debugger stopped there): the end-of-slice work comes first then
        if (unlikely(l <= 0))
        {
            if (!SliceEnd(l))
                return;
        }

        if (dbg_sh4_armed || dbg_sh4_stepout)
        {
            LoopDebug();
            return;
        }

        do
        {
#if !defined(NO_MMU)
            try {
#endif
                do
                {
                    u32 addr = next_pc;
                    next_pc += 2;
                    u32 op = IReadMem16(addr);

                    ExecuteOpcode(op);

                    l -= CPU_RATIO;
                } while (l > 0);

                l += SH4_TIMESLICE;

                // model how the jit works
                // only check for sh4_int_bCpuRun on interrupts
                if (UpdateSystem()) {
                    UpdateINTC();
                    if (!sh4_int_bCpuRun)
                        break;
                }

#if !defined(NO_MMU)
            }
            catch (SH4ThrownException& ex) {
                Do_Exception(ex.epc, ex.expEvn, ex.callVect);
                l -= CPU_RATIO * 5;	// an exception requires the instruction pipeline to drain, so approx 5 cycles
            }
#endif
        } while (true);
    }

    u32 DebugCaps()
    {
        // Every register lives in the context: it is complete wherever UpdateSystem is called from
        return SH4DBG_NATIVE | SH4DBG_CTX_AT_SLICE;
    }

    // One instruction with the end-of-slice work if the slice ran out. A delayed branch runs together with its
    // slot, also when it is not taken: a step is the same unit as in a jit. The step goes on into a slot only
    // behind the branch that it ran itself, and not when the slice ended between the two and an interrupt was
    // accepted there: the step then ends at the handler, as the loop gets there.
    bool Step()
    {
        s32& l = interp_slice;

#if !defined(NO_MMU)
        try {
#endif
            do
            {
                if (!DebugStep(l))
                    return true;

                if (l <= 0)
                    SliceEnd(l);
            } while (dbg_slot_pc != DBG_NO_SLOT);
#if !defined(NO_MMU)
        }
        catch (SH4ThrownException& ex) {
            Do_Exception(ex.epc, ex.expEvn, ex.callVect);
            l -= CPU_RATIO * 5;
            dbg_slot_pc = DBG_NO_SLOT;
        }
#endif

        return true;
    }

    int SliceCyclesLeft()
    {
        return interp_slice;
    }

    void ResetSlice()
    {
        interp_slice = SH4_TIMESLICE;
        dbg_slot_pc = DBG_NO_SLOT;
    }

    bool Init() { return true; }
    void Term() { }
    void ClearCache() { }

};

SuperH4Backend* Get_Sh4Interpreter()
{
    return new SH4IInterpreter();
}