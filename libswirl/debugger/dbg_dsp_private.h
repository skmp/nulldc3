/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_dsp.h"

/*
	What dbg_dsp.cpp gives the selftests of the debugger (dbg_selftest_dsp.cpp), and nothing else includes. Not
	one of the frozen headers.

	It is the debugger's: emulator code cannot include it (debugger.h is the one debugger header for emulator
	code), and what the sound burst itself counts is VirtualDreamcast::SoundBurstDspCounts (libswirl.h).

	Everything here is read and written on (E), or under L while no emulator thread exists. The state is only
	read: the one thing here that changes what dbg::dsp does is SetLoopForced().
*/

namespace dbg { namespace dsp { namespace test {

// How many checks dbg_dsp_Check has made since the process started: one for every step the debug loop stood in
// front of with dbg_dsp_armed not 0, stopped or not. It wraps.
extern u32 checks;

// The state of dbg_dsp.cpp, as a copy
struct State {
	bool exempt;		// a resume exemption is armed
	u32 exemptStep;		// for this step
	bool step;			// a step of the DSP is armed
	bool stepExecuted;	// and its first check let it execute
	bool stopByCheck;	// what OnStop() recorded: the DSP decided the last published stop
	u32 stopStep;		// and the step the DSP stood in front of at that stop
	u32 listed;			// how many breakpoints the listed set has
	u32 mask;			// the active mask as SetActiveMask() got it last
	bool forced;		// SetLoopForced(true) is in force
};

State GetState();

// For the neutrality target dspn.loop: while forced is true, dbg_dsp_armed is not 0 with nothing listed and no
// step armed, so that every DSP phase goes through the debug loop, whose checks then answer "continue". It is
// what a listed breakpoint that is never reached would be, which the DSP cannot have: every step executes in
// every sample. In the park context, or under L while no emulator thread exists, as the setters of dbg_dsp.h.
void SetLoopForced(bool forced);

} } }
