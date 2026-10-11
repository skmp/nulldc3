/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_arm7.h"

/*
	The stubs of header revision 6: the declarations of dbg_arm7.h and of the ARM7 block of debugger.h, with the
	bodies of a debugger that has no run control of the ARM7. Nothing is ever armed, no check stops and no step
	is taken (emu::TargetSteps(Target::Arm7) is false, so nothing asks for one). The listed set is kept as the
	core hands it over, for GetBreakpoints().

	The state is plain data with static storage, as that of dbg_sh4.cpp: nothing here has a destructor.
*/

u32 dbg_arm7_armed;

namespace {

u32 listed_ids[dbg::kMaxBreakpoints];
u32 listed_addrs[dbg::kMaxBreakpoints];
u32 listed_count;

}

u32 dbg_arm7_Check(u32)
{
	return 0;
}

void dbg_arm7_OnBurstUnclocked()
{
}

namespace dbg { namespace arm7 {

u32 MaskAddr(u32 addr)
{
	return addr & ~3u;
}

void SetBreakpoints(const u32* ids, const u32* addrs, u32 n)
{
	if (n > kMaxBreakpoints)
		n = kMaxBreakpoints;

	for (u32 i = 0; i < n; i++)
	{
		listed_ids[i] = ids[i];
		listed_addrs[i] = addrs[i];
	}

	listed_count = n;
}

u32 GetBreakpoints(u32* ids, u32* addrs, u32 max)
{
	for (u32 i = 0; i < listed_count && i < max; i++)
	{
		ids[i] = listed_ids[i];
		addrs[i] = listed_addrs[i];
	}

	return listed_count;
}

void SetActiveMask(u32)
{
}

int StepCheck()
{
	return kErrNotSupported;
}

void PrepareResume()
{
}

void PrepareStep()
{
}

bool CompleteStepAtBurstEnd()
{
	return false;
}

bool PrepareStepOver()
{
	return false;
}

void PrepareStepOut()
{
}

void OnStop()
{
}

void OnPcWritten()
{
}

} }
