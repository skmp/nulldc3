/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::callstack: call stacks of the SH4 and the ARM7 (dbg_callstack.cpp). Neither processor has a frame
	pointer convention the guest must follow, so beyond the link register the stack is a guess, and is shown
	as one:

	  frame 0   the program counter
	  frame 1   the link register (PR, LR). In code that made a call it points into the current function, not
	            into its caller
	  further   words on the stack that look like return addresses: scanning upward from the stack pointer,
	            at most kScanWords words, a value that is even (SH4) or a multiple of 4 (ARM7), lies in
	            executable memory (SH4: main memory or the boot ROM; ARM7: wave memory) and whose preceding
	            instruction, at value - 4, is a call (SH4: BSR, BSRF, JSR; ARM7: BL)

	Memory is read through dbg::mem.
*/

namespace dbg { namespace callstack {

const u32 kScanWords = 1024;

enum class FrameKind : u8 {
	Pc,			// frame 0. No wire location
	Link,		// wire location "pr" (SH4), "lr" (ARM7)
	Stack,		// wire location "stack+0x<stackOffset in hex>"
};

struct Frame {
	u32 pc;
	u32 sp;				// the stack pointer of the processor (R15, R13): the same in every frame
	u32 stackOffset;	// Stack: bytes above sp where the return address was found; otherwise 0
	FrameKind kind;
};

// Writes at most maxFrames frames, innermost first, and returns their number: at least 1 for Sh4 and
// Arm7 when maxFrames is not 0, and 0 for Dsp and when no machine exists. maxFrames <= kMaxCallstackFrames.
// On (E), or on a thread that holds L while no emulator thread exists. In the slice hook the SH4 context
// must be complete (SH4DBG_CTX_AT_SLICE); callers do not unwind the SH4 there otherwise. No side effects.
u32 Unwind(Target t, Frame* out, u32 maxFrames);

} }
