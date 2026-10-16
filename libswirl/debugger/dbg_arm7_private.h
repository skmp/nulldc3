/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_arm7.h"

/*
	What dbg_arm7.cpp gives the selftests of the debugger (dbg_selftest_arm7.cpp), and nothing else includes.
	Not one of the frozen headers.

	It is the debugger's: emulator code cannot include it (debugger.h is the one debugger header for emulator
	code), and what the sound burst itself counts is VirtualDreamcast::SoundBurstCounts (libswirl.h).

	Everything here is read on (E), or under L while no emulator thread exists. Nothing here changes what
	dbg::arm7 does.
*/

namespace dbg { namespace arm7 { namespace test {

// How many checks dbg_arm7_Check has made since the process started: one for every instruction the debug loop
// stood in front of with dbg_arm7_armed not 0, stopped or not. It wraps.
extern u32 checks;

// The state of dbg_arm7.cpp, as a copy
struct State {
	bool exempt;		// a resume exemption is armed
	u32 exemptKey;		// for the PC with this key
	bool step;			// a step is armed
	bool stepExecuted;	// and its instruction has executed
	bool temp;			// the temporary breakpoint exists
	u32 tempAddr;		// its address, as it was set
	bool stopByCheck;	// what OnStop() recorded: the ARM7 check decided the last published stop
	u32 stopKey;		// and the key of the PC of the ARM7 at that stop
	u32 listed;			// how many breakpoints the listed set has
	u32 mask;			// the active mask as SetActiveMask() got it last
};

State GetState();

} } }
