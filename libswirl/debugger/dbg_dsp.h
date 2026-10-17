/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::dsp: the state behind DSP code breakpoints and steps (dbg_dsp.cpp): the listed set, the active mask, the
	resume exemption, the step. The sound burst reaches it through dbg_dsp_armed, dbg_dsp_Check and
	dbg_dsp_SampleEnd of debugger.h; this header is what run control (dbg::emu) calls. The DSP is clocked
	whatever ARMRST says: nothing here depends on whether the ARM7 is held.

	Terms
	  listed breakpoint  an enabled code breakpoint of the client's table, on a step 0..127. It stops only while
	                     the active mask has CategoryBit(Category::Dsp), and only then does it arm the debug loop
	  resume exemption   armed for one step (PrepareResume). The first check behind it answers "continue" if it is
	                     in front of that step: the step the DSP stands on executes, also with a breakpoint on it.
	                     It ends at the first check that is made, used or not; whenever dbg_dsp_armed changes to or
	                     from 0; at OnStop() and at OnStateReplaced()
	  step               armed by PrepareStep(): the first check answers "continue", every later check stops; when no
	                     check comes behind the step in its sample (it was step 127), the step is complete at the end
	                     of that sample (dbg_dsp_SampleEnd)

	Unless a function says otherwise it is called on (E) in the park context only, never blocks, and never while a
	burst executes. Nothing here touches a code cache: there is one DSP and it compiles no check. Nothing here keeps
	a pointer into the machine: the step the DSP stands on comes through DSP::DebugGetState, asked for at each use.
*/

namespace dbg { namespace dsp {

// The enabled listed breakpoints, all of them (emu::SetDspBreakpoints): ids[i] is on step steps[i], 0..127, any
// order. n <= kMaxBreakpoints; the arrays are read during the call only. Replaces the listed set and updates
// dbg_dsp_armed. Also callable under L while no emulator thread exists, with or without a machine.
void SetBreakpoints(const u32* ids, const u32* steps, u32 n);

// The listed set as SetBreakpoints() got it last: copies the first max pairs, in the order they were given, and
// returns how many the set has. Changes nothing. Same contexts as SetBreakpoints(). For the selftests: it is how the
// hand-over of the core (PushBreakpoints) is checked without a stop.
u32 GetBreakpoints(u32* ids, u32* steps, u32 max);

// The active mask (emu::SetActiveMask); only CategoryBit(Category::Dsp) matters here. Updates dbg_dsp_armed: a
// listed set whose category is not active arms nothing. Also callable under L while no emulator thread exists.
// Before the first call the mask is 0: no listed breakpoint stops.
void SetActiveMask(u32 categories);

// Park loop, for every leave of the park context that can let the DSP execute and is not a step of the DSP: a
// resume (also the first Park() of a thread that resumes the stop of the thread before it), a step of the SH4, a
// step, step-over or step-out of the ARM7. Called in front of running a suspended burst on, or of leaving for the
// next burst. Arms the exemption for the step the DSP stands in front of iff all of this holds, and arms nothing
// otherwise: the last published stop was decided by the DSP (OnStop() recorded that, with the step); the DSP
// stands in front of that step now (DSP::DebugGetState); dbg_dsp_armed is not 0, so that a check will be made.
void PrepareResume();

// Park loop, for a step of the DSP (StepKind::Into), in front of running a suspended burst on or of leaving for the
// next burst. Arms the step and makes dbg_dsp_armed not 0: the first check answers "continue", wherever it is; every
// later check stops with StopCause::Step; dbg_dsp_SampleEnd() completes it where debugger.h says. There is no
// precondition: a step of the DSP is armed whether the ARM7 is held or not.
void PrepareStep();

// Entering the park context for a stop, once per published stop and before the listener, beside
// dbg::sh4::OnStop() and dbg::arm7::OnStop(); also when temporary state must end without a stop: for a guest reset
// and when the emulator thread exits. Never for a service excursion. Removes a step that is armed, clears the
// exemption, updates dbg_dsp_armed, and records the step the DSP stands in front of together with whether the DSP
// decided this stop (dbg_dsp_Check or dbg_dsp_SampleEnd called emu::OnSoundStop since the OnStop() before). That is
// so also when another cause met it in the same stop and won the publication: the hit counts as taken, and a
// resume runs through it.
void OnStop();

// The machine state was replaced (a reset, a loaded state): ends the exemption. Also callable under L while no
// emulator thread exists.
void OnStateReplaced();

} }
