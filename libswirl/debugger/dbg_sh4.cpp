// Stub: dbg::sh4 and the dbg_sh4_* helpers, implemented by package E4b.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_sh4.h"

#include <cstdio>
#include <cstdlib>

// The stub arms nothing: no backend ever makes a check, steps are not supported. What only an armed address
// or an armed step leads to ends the process.
[[noreturn]] static void StubReached(const char* what)
{
	fprintf(stderr, "debugger: %s reached, but dbg::sh4 is a stub\n", what);
	abort();
}

u32 dbg_sh4_armed;
u32 dbg_sh4_stepout;

bool dbg_sh4_IsArmed(u32) { return false; }
bool dbg_sh4_AnyArmed(u32, u32) { return false; }
extern "C" u32 DYNACALL dbg_sh4_Check(u32, u32, u32) { StubReached("dbg_sh4_Check"); }
extern "C" u32 DYNACALL dbg_sh4_CheckFlow(u32) { StubReached("dbg_sh4_CheckFlow"); }
void dbg_sh4_OnException() { }
bool dbg_sh4_StopPending() { return false; }
bool dbg_sh4_TakeRemainder(u32, u32*, u32*) { return false; }

namespace dbg { namespace sh4 {

void SetBreakpoints(const u32*, const u32*, u32 n)
{
	if (n != 0)
		StubReached("dbg::sh4::SetBreakpoints with a breakpoint");
}

void SetActiveMask(u32) { }

int StepCheck(const char**) { return kErrNotSupported; }

void PrepareResume() { }
void PrepareStep() { StubReached("dbg::sh4::PrepareStep"); }
bool PrepareStepOver() { StubReached("dbg::sh4::PrepareStepOver"); }
void PrepareStepOut() { StubReached("dbg::sh4::PrepareStepOut"); }

void OnStop(StopInfo* out)
{
	if (out)
	{
		out->cause = StopCause::NoCause;
		out->breakpointId = 0;
	}
}

void OnPcWritten() { }
bool FallbackWanted() { return false; }

} }
