/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

/*
	What the debugger needs from hw/sh4 beyond sh4_if.h. Debugger sources include this header and never
	sh4_core.h, whose register macros (r, pr, sr, fr, ...) rename members of other headers.
	Registers are reached as p_sh4rcb->cntx.*, SR as sh4_sr_GetFull() / sh4_sr_SetFull() (sh4_if.h).

	The debugger calls everything here on the emulator thread only.
*/

// After sr.status and sr.T were written (sh4_sr_SetFull): swaps r0..r7 with the other bank when the effective
// RB changed and forces RB = 0 while MD = 0. Returns true when an interrupt can be accepted now; the caller
// then calls UpdateINTC().
bool UpdateSR();

// After fpscr.full was written: swaps the FP register banks when FR changed and puts the host FP control
// register of the calling thread into the rounding and denormal mode of FPSCR.
void UpdateFPSCR();

// Puts the host FP control register of the calling thread into the mode of FPSCR, whatever was applied last.
// That register belongs to a thread: the thread that runs the CPU calls this before it runs guest code that
// another thread, or the park context, may have prepared (thread start, reset, leaving the park context).
void sh4_host_fp_resync();

/*
	The interpreter fallback for SH4 breakpoints and steps, used with a JIT backend that has no
	SH4DBG_NATIVE. It executes through the interpreter and charges time as that backend's decoder does: at
	each block start it decodes the block at pc, charges its cycles, and interprets up to the block's end
	before it tests its slice counter, so block boundaries and costs are the backend's. A stop in a block
	refunds the cost of the instructions that did not run: that decode carries the debugger checks with their
	refunds (debugger/debugger.h) whether or not the backend sets ngen_features::DebugChecks, and nothing is
	compiled from it. Its slice counter is its own and survives leaving and entering the loop; the JIT
	backend's ResetSlice() resets it. Entered with a counter that is not positive and no remainder to run, the
	loop does the end-of-slice work before its first block, as a backend's Loop() does (sh4_if.h). The backend
	object and its code cache are not touched.
*/
#if defined(NULLDC_DEBUGGER) && FEAT_SHREC != DYNAREC_NONE

// From SuperH4_impl::Run(), in place of the backend's Loop(), while dbg::emu::UseFallbackLoop() is true.
// Makes the debugger check (dbg_sh4_Check) in front of every armed instruction and the flow reports of a
// pending step-out. Returns as Loop() does: at a slice end with CpuRunning clear, or at once when a check
// or a flow report stopped.
void sh4_fallback_loop();

// From the park loop, in place of SuperH4Backend::Step(): executes the instruction at pc (a delayed branch
// together with its slot), charged as a block of that one instruction, with the end-of-slice work if the
// slice ran out. The caller has checked the step preconditions and set CpuRunning for its duration.
// Like Step(), it does not apply the loop's rule for a counter that is not positive: entered with one it
// executes its instruction first and does the end-of-slice work behind it. After a flow report that stopped
// behind the block that used the slice up, that is one instruction ahead of an undebugged run (a documented
// skew of a debugger stop).
void sh4_fallback_step();

#else

// Builds without the debugger, and builds without an SH4 JIT, never select the fallback.
static inline void sh4_fallback_loop() { die("sh4_fallback_loop: not in this build"); }
static inline void sh4_fallback_step() { die("sh4_fallback_step: not in this build"); }

#endif
