/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::sh4: the state behind SH4 code breakpoints and steps (dbg_sh4.cpp) — the armed set and its page bitmap,
	the resume exemption, the owed remainder of an interrupted block, temporary breakpoints, step-over and
	step-out. The SH4 backends reach it through the dbg_sh4_* functions of debugger.h; this header is what
	run control (dbg::emu) and register writes call.

	Terms
	  armed address        an address with at least one enabled listed breakpoint, or with a temporary breakpoint.
	                       Backends put a check in front of armed addresses
	  active               a listed breakpoint stops only while its category is in the active mask; a temporary
	                       breakpoint is always active. Activity is tested at each arrival, in the check
	  temporary breakpoint an armed address with a stack condition, made by a step-over; never listed
	  resume exemption     the first check at the pc of a stop answers "continue" once, so that a resume or a
	                       step from a breakpoint executes the instruction it stopped in front of
	  owed remainder       { pc, end, cycles } of the block that a check interrupted; if the resume continues at
	                       that pc, the backend runs the rest of the block for exactly those cycles
	                       (dbg_sh4_TakeRemainder), which makes stop + resume invisible to the guest

	Unless a function says otherwise it is called on (E) in the park context only (park loop or service
	excursion: no block is executing, none is on the host stack), never blocks, and never from the slice hook.
	Changes of the armed set made on (E) discard compiled blocks through sh4_cpu->DebugBackend()
	(SuperH4Backend::DebugDiscardPage / DebugDiscardFlow, hw/sh4/sh4_if.h); under L without an emulator thread
	only the set changes, because every thread start builds a fresh code cache.
*/

namespace dbg { namespace sh4 {

// The address rule. Two SH4 code addresses are the same instruction when their keys are equal. The key is
// for comparing, not an address to read from.
// Without MMU every address is its low 29 bits: 0x0C.., 0x8C.. and 0xAC.. are one. With MMU the fixed
// mappings P1 and P2 are one address and every other address stands for itself.
inline u32 MaskAddr(u32 addr) {
#if defined(NO_MMU)
	return addr & DBG_SH4_ADDR_MASK;
#else
	if (addr >= 0x80000000 && addr < 0xC0000000)
		return 0x80000000 | (addr & DBG_SH4_ADDR_MASK);
	return addr;
#endif
}

// Stack test of step-over and step-out: sp is on the level of the request (sp0) or above it, on the same
// stack, when sp >= sp0 && sp - sp0 < kStackWindow. Below sp0 is a deeper activation; kStackWindow or more
// above it is taken to be another guest thread's stack.
const u32 kStackWindow = 0x100000;

// What the breakpoint state says about a stop: the cause and id its helpers gave to emu::LatchStop, which
// holds the record that is published.
struct StopInfo {
	StopCause cause;		// Breakpoint: a listed breakpoint stopped. Step: a temporary breakpoint stopped, or the
							// return a step-out waited for was executed. NoCause: no check or flow report stopped
	u32 breakpointId;		// for Breakpoint: its id, the lowest when several are on the address; otherwise 0
};

// The enabled listed breakpoints, all of them: ids[i] is at addrs[i], any order, addresses as the client
// gave them. n <= kMaxBreakpoints; the arrays are read during the call only.
// Diffs against the armed set: for every key that becomes armed or stops being armed it updates the set,
// the page bitmap and dbg_sh4_armed and, on (E), discards the compiled blocks of that address's page.
// Temporary breakpoints stay. A change that moves no address in or out of the armed set discards nothing.
// Also callable under L while no emulator thread exists.
void SetBreakpoints(const u32* ids, const u32* addrs, u32 n);

// The active mask (emu::SetActiveMask); only CategoryBit(Category::Sh4) matters here. Read by the check;
// never changes the armed set and never discards. Also callable under L while no emulator thread exists.
void SetActiveMask(u32 categories);

// Park loop, inside a CC_EXEC call (emu::Step calls it for every kind of SH4 step). The preconditions of
// executing one instruction at Sh4cntx.pc, with the opcodes read through dbg::mem. Changes nothing.
// Returns 0, or kErrCannotStep with *why (if why is not null) set to one of these constant strings:
//   "pc is odd"
//   "pc not readable"           the word at pc cannot be read
//   "undefined opcode"          the opcode at pc is not an instruction the interpreter table executes
//   "illegal slot instruction"  pc holds a delayed branch whose slot word cannot be read, is undefined or
//                               is an instruction that writes pc
int StepCheck(const char** why);

// Park loop, when the park context is left for a resume, after the deferred interrupt acceptance and
// sh4_host_fp_resync(). Arms the resume exemption iff Sh4cntx.pc has the key of the pc that the last
// OnStop() recorded and dbg_sh4_WantsCheck is true for it (opcode read through dbg::mem). Keeps the owed
// remainder if Sh4cntx.pc is exactly its pc, drops it otherwise.
void PrepareResume();

// Park loop, at the same point, for a step of one instruction. Arms the exemption as PrepareResume() does
// and drops the owed remainder.
// A step of any kind whose deferred interrupt acceptance accepted an interrupt is complete at the vector
// without executing anything: StepCheck() looked at the instruction of the stop, not at the one at the
// vector. This is then the only Prepare function called, and no instruction is executed behind it.
void PrepareStep();

// Park loop, at the same point, for a step-over, before PrepareResume() / PrepareStep().
// If Sh4cntx.pc holds BSR, BSRF or JSR it adds a temporary breakpoint at pc + 4, if it holds TRAPA at
// pc + 2, with sp0 = R15, and returns true: the caller goes on as for a resume (PrepareResume). The
// temporary breakpoint stops, with StopCause::Step, only when R15 passes the stack test against sp0.
// For any other instruction it changes nothing and returns false: the caller makes a step (PrepareStep).
// Not called when the deferred interrupt acceptance accepted an interrupt: the step is complete (PrepareStep),
// and the instruction at the vector is not the one the step was asked for.
bool PrepareStepOver();

// Park loop, at the same point, for a step-out, before PrepareResume(). Records sp0 = R15, sets
// dbg_sh4_stepout and discards the cached blocks that end with a call or a return; the caller goes on as
// for a resume. The stop comes, with StopCause::Step, right after the RTS or RTE (and its slot) that leaves
// the activation of the request: calls made on the level of the request are counted, handlers entered
// after the request are skipped whole.
// Not called when the deferred interrupt acceptance accepted an interrupt: the step is complete (PrepareStep).
void PrepareStepOut();

// Entering the park context for a stop, once per published stop and before the listener: after
// SuperH4::Run() returned with a debugger stop decided, after every step (executed, or complete at a vector),
// for the start park and for a reset made while paused. Never for a service excursion, which publishes
// nothing: temporary breakpoints and a pending step-out survive it. Also called when temporary state must end
// without a stop: for a guest reset and when the emulator thread exits.
// Writes to *out (out may be null) what stopped. *out repeats what the helpers passed to emu::LatchStop for
// this stop; dbg::emu publishes LatchStop's record and may check it against *out. Then removes every
// temporary breakpoint, cancels a pending step-out (dbg_sh4_stepout = 0, instrumented blocks discarded),
// clears the exemption, makes dbg_sh4_StopPending() false and records Sh4cntx.pc as the pc of the stop. The
// owed remainder stays.
void OnStop(StopInfo* out);

// The debugger wrote the SH4 PC, the machine was reset or a state was loaded: drops the owed remainder and
// clears the exemption. Also callable under L while no emulator thread exists.
void OnPcWritten();

// Does the interpreter fallback have work on a backend without native breakpoints? True while an enabled
// listed breakpoint exists and its category is active, a temporary breakpoint exists, or a step-out is
// pending. No side effects. Also callable under L while no emulator thread exists.
bool FallbackWanted();

} }
