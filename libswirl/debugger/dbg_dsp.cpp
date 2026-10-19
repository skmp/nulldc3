/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_dsp.h"
#include "dbg_dsp_private.h"

#include "hw/sh4/sh4_if.h"
#include "hw/aica/dsp.h"

/*
	The state is plain data with static storage, as that of dbg_arm7.cpp: nothing here has a destructor.

	  listed   the enabled listed breakpoints, as the core handed them over: { step, id }, in the order they were
	           given
	  exempt   the resume exemption: the first check that is made answers "continue" if it is in front of this
	           step, and ends it either way
	  step     a step of the DSP: the first check lets its step execute, wherever it is; every later check stops,
	           and so does the end of a sample that no check came behind (dbg_dsp_SampleEnd)

	The DSP compiles no check, so nothing here touches a code cache: the debug loop of the DSP phase
	(DSP::RunPhase) asks in front of every step while dbg_dsp_armed is not 0. Nothing here depends on whether the
	ARM7 is held: the AICA is clocked whatever ARMRST says.
*/

u32 dbg_dsp_armed;

namespace dbg { namespace dsp { namespace test {

u32 checks;

} } }

namespace {

u32 listed_ids[dbg::kMaxBreakpoints];
u32 listed_steps[dbg::kMaxBreakpoints];
u32 listed_count;

u32 active_mask;

// dbg::dsp::test::SetLoopForced
bool forced;

enum StepState {
	STEP_NONE,
	STEP_ARMED,			// the first check behind PrepareStep() has not come yet
	STEP_EXECUTED,		// it answered "continue": the next check stops, or the end of the sample
};

struct {
	bool exempt;
	u32 exempt_step;

	StepState step;

	// a check or the end of a sample recorded a stop (emu::OnSoundStop) since the last OnStop()
	bool decided;

	// what OnStop() recorded: the step the DSP stood in front of at the last published stop, and whether the DSP
	// decided it
	bool stop_by_check;
	u32 stop_step;
} st;

// The step the DSP stands in front of, as the DSP gives it
bool ReadStep(u32* step)
{
	DSP* dsp = sh4_cpu ? sh4_cpu->GetA0H<DSP>(A0H_DSP) : nullptr;
	DspDebugState state;

	if (dsp == nullptr || !dsp->DebugGetState(&state))
		return false;

	*step = state.step;
	return true;
}

bool ListedActive()
{
	return listed_count != 0 && (active_mask & dbg::CategoryBit(dbg::Category::Dsp)) != 0;
}

// dbg_dsp_armed follows what can stop. An exemption does not outlive a change of the flag to or from 0: with the
// flag 0 no check is made, and an exemption would wait there for a breakpoint that is added later
void Rearm()
{
	u32 armed = (ListedActive() ? 1 : 0) + (st.step != STEP_NONE ? 1 : 0) + (forced ? 1 : 0);

	if ((armed != 0) != (dbg_dsp_armed != 0))
		st.exempt = false;

	dbg_dsp_armed = armed;
}

u32 Stop(dbg::StopCause cause, u32 id)
{
	st.decided = true;

	dbg::emu::OnSoundStop(cause, id);

	return 1;
}

}

u32 dbg_dsp_Check(u32 step)
{
	dbg::dsp::test::checks++;

	// the exemption ends at the first check that is made, used or not
	const bool exempt = st.exempt && st.exempt_step == step;
	st.exempt = false;

	if (st.step != STEP_NONE)
	{
		// the check in front of the step's own step, wherever it is: it executes, also with a listed breakpoint
		// on it. Every check behind it is the end of the step
		if (st.step == STEP_ARMED)
		{
			st.step = STEP_EXECUTED;
			return 0;
		}

		return Stop(dbg::StopCause::Step, 0);
	}

	if (exempt)
		return 0;

	// the id is the lowest of those on the step
	if (active_mask & dbg::CategoryBit(dbg::Category::Dsp))
	{
		u32 id = 0;

		for (u32 i = 0; i < listed_count; i++)
		{
			if (listed_steps[i] == step && (id == 0 || listed_ids[i] < id))
				id = listed_ids[i];
		}

		if (id != 0)
			return Stop(dbg::StopCause::Breakpoint, id);
	}

	return 0;
}

u32 dbg_dsp_SampleEnd()
{
	// the step executed the last step of its sample, and no check came behind it
	if (st.step == STEP_EXECUTED)
		return Stop(dbg::StopCause::Step, 0);

	return 0;
}

namespace dbg { namespace dsp {

namespace test {

State GetState()
{
	State s;

	s.exempt = st.exempt;
	s.exemptStep = st.exempt_step;
	s.step = st.step != STEP_NONE;
	s.stepExecuted = st.step == STEP_EXECUTED;
	s.stopByCheck = st.stop_by_check;
	s.stopStep = st.stop_step;
	s.listed = listed_count;
	s.mask = active_mask;
	s.forced = forced;

	return s;
}

void SetLoopForced(bool on)
{
	forced = on;

	Rearm();
}

}

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

	Rearm();
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

	Rearm();
}

void PrepareResume()
{
	u32 step;

	st.exempt = false;

	// Only behind a stop that the DSP decided, with the DSP still in front of that step; and only when a check
	// will be made: an exemption that is armed waits for its check
	if (!st.stop_by_check || dbg_dsp_armed == 0 || !ReadStep(&step) || step != st.stop_step)
		return;

	st.exempt = true;
	st.exempt_step = step;
}

void PrepareStep()
{
	st.step = STEP_ARMED;

	// the step has its own rule for the first check
	st.exempt = false;

	Rearm();
}

void OnStop()
{
	u32 step = 0;

	st.step = STEP_NONE;
	st.exempt = false;

	Rearm();

	ReadStep(&step);

	st.stop_by_check = st.decided;
	st.stop_step = step;
	st.decided = false;
}

void OnStateReplaced()
{
	st.exempt = false;
}

} }
