/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_dsp.h"

/*
	The stubs of header revision 7: the declarations of dbg_dsp.h and of the DSP block of debugger.h, with the
	bodies of a debugger that has no run control of the DSP. Nothing is ever armed, no check stops and no step
	is taken (emu::TargetSteps(Target::Dsp) is false, so nothing asks for one). The listed set is kept as the
	core hands it over, for GetBreakpoints(), and the active mask as it is set.

	The state is plain data with static storage, as that of dbg_arm7.cpp: nothing here has a destructor.
*/

u32 dbg_dsp_armed;

namespace {

u32 listed_ids[dbg::kMaxBreakpoints];
u32 listed_steps[dbg::kMaxBreakpoints];
u32 listed_count;
u32 active_mask;

}

u32 dbg_dsp_Check(u32)
{
	return 0;
}

u32 dbg_dsp_SampleEnd()
{
	return 0;
}

namespace dbg { namespace dsp {

void SetBreakpoints(const u32* ids, const u32* steps, u32 n)
{
	if (n > kMaxBreakpoints)
		n = kMaxBreakpoints;

	for (u32 i = 0; i < n; i++)
	{
		listed_ids[i] = ids[i];
		listed_steps[i] = steps[i];
	}

	listed_count = n;
}

u32 GetBreakpoints(u32* ids, u32* steps, u32 max)
{
	for (u32 i = 0; i < listed_count && i < max; i++)
	{
		ids[i] = listed_ids[i];
		steps[i] = listed_steps[i];
	}

	return listed_count;
}

void SetActiveMask(u32 categories)
{
	active_mask = categories;
}

void PrepareResume()
{
}

void PrepareStep()
{
}

void OnStop()
{
}

void OnStateReplaced()
{
}

} }
