/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::arm7: the state behind ARM7 code breakpoints and steps (dbg_arm7.cpp): the listed set, the active
	mask, the temporary breakpoint, the resume exemption, the step. The sound burst reaches it through
	dbg_arm7_armed, dbg_arm7_Check and dbg_arm7_OnBurstUnclocked of debugger.h; this header is what run control
	(dbg::emu) and the register writer (dbg::regs) call.

	Terms
	  listed breakpoint     an enabled code breakpoint of the client's table. It stops only while the active
	                        mask has CategoryBit(Category::Arm7), and only then does it arm the debug loop
	  temporary breakpoint  made by a step-over or a step-out; never listed, always active; at most one exists
	  resume exemption      armed for one PC (PrepareResume, PrepareStepOver, PrepareStepOut). The first check
	                        behind it answers "continue" if its pc has the key of that PC: the instruction the
	                        ARM7 stands on executes, also with a breakpoint on it. It ends at the first check
	                        that is made, used or not (a FIQ entered in front of the instruction ends it unused:
	                        a breakpoint on the vector stops); whenever dbg_arm7_armed changes to or from 0;
	                        at a burst that finds the ARM7 held in reset (dbg_arm7_OnBurstUnclocked); at
	                        OnStop() and at OnPcWritten(). So it never waits for a breakpoint that is added
	                        later, or for an ARM7 that is released later
	  step                  armed by PrepareStep(): the instruction at the PC executes, also with a listed
	                        breakpoint on it, and the next check stops

	Unless a function says otherwise it is called on (E) in the park context only, never blocks, and never
	while a burst executes. Nothing here touches a code cache: no ARM7 backend compiles a check. Nothing here
	keeps a pointer into the machine: the mask of the wave memory is the machine's (sh4_cpu->aica_ram.size - 1,
	as dbg::mem reads it) and the registers come through SoundCPU::Debug* (hw/arm7/SoundCPU.h), both asked for
	at each use.
*/

namespace dbg { namespace arm7 {

// The address rule. Two ARM7 code addresses are the same instruction when their keys are equal: the address
// under the mask of the wave memory (its size - 1) with bits 0 and 1 clear. The ARM7 fetches every
// instruction from wave memory under that mask. The key is for comparing, not an address to read from.
// Needs a machine for the mask: (E), or under L while no emulator thread exists; with no machine it returns
// addr with bits 0 and 1 clear.
u32 MaskAddr(u32 addr);

// The enabled listed breakpoints, all of them (emu::SetArm7Breakpoints): ids[i] is at addrs[i], any order,
// addresses as the client gave them. n <= kMaxBreakpoints; the arrays are read during the call only.
// Replaces the listed set and updates dbg_arm7_armed. The temporary breakpoint stays. The addresses are kept
// as given and compared under the address rule at each check: the call does not touch the machine. Also
// callable under L while no emulator thread exists, with or without a machine.
void SetBreakpoints(const u32* ids, const u32* addrs, u32 n);

// The listed set as SetBreakpoints() got it last: copies the first max pairs, in the order they were given,
// and returns how many the set has. Changes nothing. Same contexts as SetBreakpoints(). For the selftests: it
// is how the hand-over of the core (PushBreakpoints) is checked without a stop.
u32 GetBreakpoints(u32* ids, u32* addrs, u32 max);

// The active mask (emu::SetActiveMask); only CategoryBit(Category::Arm7) matters here. Updates
// dbg_arm7_armed: a listed set whose category is not active arms nothing. Also callable under L while no
// emulator thread exists. Before the first call the mask is 0: no listed breakpoint stops.
void SetActiveMask(u32 categories);

// Inside a CC_EXEC call (emu::Step calls it for every kind of ARM7 step). Changes nothing.
// Returns 0, or kErrNotClocked while the ARM7 is held in reset: SoundCPU::DebugEnabled() is false, the
// source that the register ENABLED of dc.aica.arm7 and the unwinder read. There is no kErrCannotStep for the
// ARM7: a word that is no instruction takes the undefined-instruction exception, as in a run.
int StepCheck();

// Park loop, for every leave of the park context that can let the ARM7 execute and is not a step of the
// ARM7: a resume; a step of the SH4, with a sound burst suspended or not (its slice end can start one); the
// first Park() of a thread that resumes the stop of the thread before it. With a burst suspended it is called
// in front of running the burst on. Not called for a step-over on a BL or a step-out of the ARM7:
// PrepareStepOver() and PrepareStepOut() arm the exemption themselves.
// Arms the exemption for the PC of the ARM7 iff all of this holds, and arms nothing otherwise: the last
// published stop was decided by the ARM7 check (OnStop() recorded that, with the PC); the PC has the key of
// that PC now (a PC the debugger wrote away and back counts as that PC: the rule of dbg::sh4); the ARM7 is
// clocked (SoundCPU::DebugEnabled()); dbg_arm7_armed is not 0, so that a check will be made.
void PrepareResume();

// Park loop, at the same point, for a step of one instruction. Arms the step for the PC of the ARM7 as it is
// now and makes dbg_arm7_armed not 0. From then on the first check, if its pc has the key of that PC, answers
// "continue": the instruction executes, also with a listed breakpoint on it. Every other check stops with
// StopCause::Step: the check behind the instruction, wherever it is (the next instruction, a branch target,
// the vector of an exception the instruction took or of a FIQ entered behind it), and the first check itself
// when its pc is not that PC (a FIQ was entered in front of the instruction: the step is complete at the
// vector with nothing executed). A burst that starts with the ARM7 held in reset completes the step
// (dbg_arm7_OnBurstUnclocked). A burst that ends behind the instruction without another check leaves the step
// to CompleteStepAtBurstEnd().
void PrepareStep();

// Park loop, behind a burst that ran to its end (VirtualDreamcast::RunSoundBurst returned true) while a step
// was armed. If the instruction of the step has executed, the step is complete at the end of the burst, with
// none suspended: it records the stop as the check does (emu::OnSoundStop with StopCause::Step, which in the
// park context is the stop the park loop publishes next) and returns true. Otherwise it changes nothing and
// returns false.
bool CompleteStepAtBurstEnd();

// Park loop, for a step-over. If the instruction at the PC is BL (read through dbg::mem, decoded by
// DisasmArm7: ArmInsnInfo::is_call) it sets the temporary breakpoint at PC + 4, then arms the exemption for
// the PC as it is, whatever decided the last stop (the instruction the request stands on executes, also with
// a listed breakpoint on it, as for a step of one instruction), and returns true: the caller goes on as for
// a resume, without PrepareResume(). Otherwise it changes nothing and returns false: the caller makes a step
// (PrepareStep()).
bool PrepareStepOver();

// Park loop, for a step-out. Sets the temporary breakpoint at R14 of the active mode, then arms the exemption
// for the PC as it is, whatever decided the last stop; the caller goes on as for a resume, without
// PrepareResume(). A heuristic: right for leaf code and before R14 is saved or overwritten.
void PrepareStepOut();

// Entering the park context for a stop, once per published stop and before the listener, beside
// dbg::sh4::OnStop(); also when temporary state must end without a stop: for a guest reset and when the
// emulator thread exits. Never for a service excursion, which publishes nothing. Never called without an
// emulator thread. Removes the temporary breakpoint and a step that is armed, clears the exemption, updates
// dbg_arm7_armed, and records the PC of the ARM7 together with whether the ARM7 check decided this stop (it
// called emu::OnSoundStop since the OnStop() before). That is so also when a cause of the SH4 met it in the
// same stop and won the publication: the hit of the ARM7 counts as taken, and a resume runs through it.
void OnStop();

// The debugger wrote the PC of the ARM7 (dbg::regs::Write, also in a service excursion of a running machine),
// or the machine state was replaced (a reset, a loaded state): ends the exemption. What OnStop() recorded
// stays: PrepareResume() decides from the PC as it is at the leave, which is the rule of
// dbg::sh4::OnPcWritten. A PC written onto another breakpoint stops there with nothing executed; a PC written
// back onto the address of the stop resumes through a breakpoint there. A replaced state needs no more: on (E)
// OnStop() follows and records anew, and while Stopped the owed resume goes with the state, so the next
// PrepareResume() comes behind a new OnStop(). Also callable under L while no emulator thread exists.
void OnPcWritten();

} }
