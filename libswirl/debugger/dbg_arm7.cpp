/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_arm7.h"
#include "dbg_arm7_private.h"
#include "dbg_disasm.h"
#include "dbg_mem.h"

#include "hw/sh4/sh4_if.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7_context.h"

/*
	The state is plain data with static storage, as that of dbg_sh4.cpp: nothing here has a destructor.

	  listed   the enabled listed breakpoints, as the core handed them over: { address as given, id }, in the
	           order they were given. The address rule is applied when a check is made: the mask of the wave
	           memory is the machine's, and the set is also handed over while no machine exists
	  temp     the temporary breakpoint of a step-over or a step-out, at most one
	  exempt   the resume exemption: the first check that is made answers "continue" if its pc has this key,
	           and ends it either way
	  step     a step of one instruction: the first check, if its pc has the key of the step, lets the
	           instruction execute; every other check stops

	No backend of the ARM7 compiles a check, so nothing here touches a code cache: the debug loop of the sound
	burst (SoundCPU::RunSample) asks in front of every instruction while dbg_arm7_armed is not 0.
*/

u32 dbg_arm7_armed;

namespace dbg { namespace arm7 { namespace test {

u32 checks;

} } }

namespace {

u32 listed_ids[dbg::kMaxBreakpoints];
u32 listed_addrs[dbg::kMaxBreakpoints];
u32 listed_count;

u32 active_mask;

enum StepState {
	STEP_NONE,
	STEP_ARMED,			// the instruction of the step has not executed
	STEP_EXECUTED,		// the check in front of it answered "continue": the next check stops
};

struct {
	bool temp;
	u32 temp_addr;

	bool exempt;
	u32 exempt_key;

	StepState step;
	u32 step_key;

	// a check, a burst that found the ARM7 held, or the end of a burst recorded a stop (emu::OnSoundStop)
	// since the last OnStop()
	bool decided;

	// what OnStop() recorded: the PC of the ARM7 at the last published stop, and whether the ARM7 decided it
	bool stop_by_check;
	u32 stop_key;
} st;

SoundCPU* Scpu()
{
	return sh4_cpu ? sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU) : nullptr;
}

bool Clocked()
{
	SoundCPU* scpu = Scpu();

	return scpu != nullptr && scpu->DebugEnabled();
}

// The address of the next instruction of the ARM7, as it is
bool ReadPc(u32* pc)
{
	SoundCPU* scpu = Scpu();

	return scpu != nullptr && scpu->DebugGetReg(15, pc);
}

bool ListedActive()
{
	return listed_count != 0 && (active_mask & dbg::CategoryBit(dbg::Category::Arm7)) != 0;
}

// dbg_arm7_armed follows what can stop. An exemption does not outlive a change of the flag to or from 0: with
// the flag 0 no check is made, and an exemption would wait there for a breakpoint that is added later
void Rearm()
{
	u32 armed = (ListedActive() ? 1 : 0) + (st.temp ? 1 : 0) + (st.step != STEP_NONE ? 1 : 0);

	if ((armed != 0) != (dbg_arm7_armed != 0))
		st.exempt = false;

	dbg_arm7_armed = armed;
}

void ArmExemption(u32 pc)
{
	st.exempt = true;
	st.exempt_key = dbg::arm7::MaskAddr(pc);
}

u32 Stop(dbg::StopCause cause, u32 id)
{
	st.decided = true;

	dbg::emu::OnSoundStop(cause, id);

	return 1;
}

}

u32 dbg_arm7_Check(u32 pc)
{
	dbg::arm7::test::checks++;

	const u32 key = dbg::arm7::MaskAddr(pc);

	// the exemption ends at the first check that is made, used or not
	const bool exempt = st.exempt && st.exempt_key == key;
	st.exempt = false;

	if (st.step != STEP_NONE)
	{
		// The check in front of the instruction of the step: it executes, also with a listed breakpoint on it.
		// A pc that is not the step's (a FIQ was entered in front of the instruction) is the end of the step
		if (st.step == STEP_ARMED && key == st.step_key)
		{
			st.step = STEP_EXECUTED;
			return 0;
		}

		return Stop(dbg::StopCause::Step, 0);
	}

	if (exempt)
		return 0;

	// a listed breakpoint wins over the temporary one; the id is the lowest of those on the address
	if (active_mask & dbg::CategoryBit(dbg::Category::Arm7))
	{
		u32 id = 0;

		for (u32 i = 0; i < listed_count; i++)
		{
			if (dbg::arm7::MaskAddr(listed_addrs[i]) == key && (id == 0 || listed_ids[i] < id))
				id = listed_ids[i];
		}

		if (id != 0)
			return Stop(dbg::StopCause::Breakpoint, id);
	}

	if (st.temp && dbg::arm7::MaskAddr(st.temp_addr) == key)
		return Stop(dbg::StopCause::Step, 0);

	return 0;
}

void dbg_arm7_OnBurstUnclocked()
{
	// The ARM7 takes its reset exception when it is released: its first instruction then is not the one an
	// exemption was armed for
	st.exempt = false;

	// no instruction executes in this burst, and none may come: the step is complete with nothing executed
	if (st.step != STEP_NONE)
		Stop(dbg::StopCause::Step, 0);
}

namespace dbg { namespace arm7 {

namespace test {

State GetState()
{
	State s;

	s.exempt = st.exempt;
	s.exemptKey = st.exempt_key;
	s.step = st.step != STEP_NONE;
	s.stepExecuted = st.step == STEP_EXECUTED;
	s.temp = st.temp;
	s.tempAddr = st.temp_addr;
	s.stopByCheck = st.stop_by_check;
	s.stopKey = st.stop_key;
	s.listed = listed_count;
	s.mask = active_mask;

	return s;
}

}

u32 MaskAddr(u32 addr)
{
	if (sh4_cpu != nullptr && sh4_cpu->aica_ram.size != 0)
		addr &= sh4_cpu->aica_ram.size - 1;

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

	Rearm();
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

void SetActiveMask(u32 categories)
{
	active_mask = categories;

	Rearm();
}

int StepCheck()
{
	return Clocked() ? 0 : kErrNotClocked;
}

void PrepareResume()
{
	u32 pc;

	st.exempt = false;

	// Only behind a stop that the ARM7 decided, with the ARM7 still in front of that instruction; and only
	// when a check will be made and the ARM7 executes: an exemption that is armed waits for its check
	if (!st.stop_by_check || dbg_arm7_armed == 0 || !Clocked() || !ReadPc(&pc) || MaskAddr(pc) != st.stop_key)
		return;

	ArmExemption(pc);
}

void PrepareStep()
{
	u32 pc = 0;

	ReadPc(&pc);

	st.step = STEP_ARMED;
	st.step_key = MaskAddr(pc);

	// the step has its own rule for the first check
	st.exempt = false;

	Rearm();
}

bool CompleteStepAtBurstEnd()
{
	if (st.step != STEP_EXECUTED)
		return false;

	Stop(StopCause::Step, 0);

	return true;
}

bool PrepareStepOver()
{
	u32 pc, op;

	// the word the ARM7 fetches next: the fetch reads under the mask of the wave memory, whatever the pc is
	if (!ReadPc(&pc) || !mem::Read32(Target::Arm7, MaskAddr(pc), &op))
		return false;

	ArmInsnInfo info;

	DisasmArm7(pc, op, nullptr, &info);

	if (!info.is_call)
		return false;

	st.temp = true;
	st.temp_addr = pc + 4;

	Rearm();

	// the instruction the request stands on executes, whatever decided the last stop
	ArmExemption(pc);

	return true;
}

void PrepareStepOut()
{
	SoundCPU* scpu = Scpu();
	u32 pc = 0, lr = 0;

	ReadPc(&pc);

	if (scpu != nullptr)
		scpu->DebugGetReg(14, &lr);

	st.temp = true;
	st.temp_addr = lr;

	Rearm();

	ArmExemption(pc);
}

void OnStop()
{
	u32 pc = 0;

	st.temp = false;
	st.step = STEP_NONE;
	st.exempt = false;

	Rearm();

	ReadPc(&pc);

	st.stop_by_check = st.decided;
	st.stop_key = MaskAddr(pc);
	st.decided = false;
}

void OnPcWritten()
{
	st.exempt = false;
}

} }
