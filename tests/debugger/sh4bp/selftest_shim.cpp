/*
	sh4bp_selftest <jit|interpreter|fallback> [filter]: the selftest of the emulator
	(libswirl/debugger/dbg_selftest_sh4bp.cpp, with its driver dbg_selftest.cpp) on the machine of these tests.

	The selftest drives the guest through dbg::emu. Here dbg::emu is this file: a park loop that hands every
	stop to the selftest driver and then does what the test armed, on top of the run control stand-in of
	machine.cpp. The emulator's own park loop takes this place when the selftest runs in the emulator; what
	this run shows is that the tests themselves are right, on all three engines.

	There is no reios: a guest that is resumed or stepped at the reset vector is "booted" by putting its pc on
	the entry point of the image, which is what the boot trap leaves behind.
*/
#include "machine.h"
#include "guest.inc"

#include "debugger/dbg_selftest.h"
#include "hw/sh4/sh4_sched.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace dbg;

static Engine engine;
static const char* filter = "sh4bp";

enum Armed { ARMED_NONE, ARMED_RESUME, ARMED_STEP, ARMED_OVER, ARMED_OUT, ARMED_RESET };

static Armed armed;
static emu::RunState state;

//cfg/cfg.h: the driver reads its filter from the configuration
string cfgLoadStr(const wchar* section, const wchar* key, const wchar* def)
{
	return strcmp(key, "SelfTest") == 0 ? filter : def;
}

u64 sh4_sched_now64()
{
	return machine_state().sched;
}

namespace dbg { namespace emu {

int Resume()
{
	if (armed != ARMED_NONE)
		return kErrNotPaused;

	armed = ARMED_RESUME;
	return 0;
}

int Step(Target t, StepKind k, const char** why)
{
	if (armed != ARMED_NONE)
		return kErrNotPaused;

	if (t != Target::Sh4)
		return kErrNotSupported;

	//the boot trap of reios is at the reset vector, which this machine has no memory at
	if (Sh4cntx.pc != 0xA0000000)
	{
		int rv = sh4::StepCheck(why);

		if (rv != 0)
			return rv;
	}

	armed = k == StepKind::Into ? ARMED_STEP : k == StepKind::Over ? ARMED_OVER : ARMED_OUT;
	return 0;
}

int Reset()
{
	armed = ARMED_RESET;
	return 0;
}

void SetSh4Breakpoints(const u32* ids, const u32* addrs, u32 n)
{
	sh4::SetBreakpoints(ids, addrs, n);
}

void SetActiveMask(u32 categories)
{
	sh4::SetActiveMask(categories);
}

RunState GetRunState()
{
	return state;
}

void RequestPause()
{
	machine_request_pause();
}

int Call(CallClass cls, std::function<void()> fn)
{
	machine_call(fn);
	return 0;
}

bool GetSh4Engine(Sh4EngineInfo* out)
{
	out->backend = engine == ENGINE_INTERP ? "interpreter" : "x64";
	out->caps = machine_backend()->DebugCaps();
	out->fallback = engine == ENGINE_FALLBACK;
	out->fallbackLoop = machine_used_fallback();
	out->sliceCyclesLeft = machine_backend()->SliceCyclesLeft();

	return true;
}

} }

static void publish(StopCause cause, u32 id)
{
	state.lifetime = Lifetime::Parked;
	state.stopId++;
	state.cause = cause;
	state.breakpointId = id;
	state.notice = engine == ENGINE_FALLBACK && machine_used_fallback() ? (u32)NOTICE_SH4_FALLBACK : 0;
}

static void publish(const MachineStop& stop)
{
	publish(stop.cause, stop.id);
}

//what the boot trap of reios does, as far as these tests look
static bool boot()
{
	if (Sh4cntx.pc != 0xA0000000)
		return false;

	machine_set_pc(G_BASE);
	return true;
}

int main(int argc, char** argv)
{
	const char* name = argc > 1 ? argv[1] : "jit";

	if (strcmp(name, "jit") == 0)
		engine = ENGINE_JIT;
	else if (strcmp(name, "interpreter") == 0)
		engine = ENGINE_INTERP;
	else if (strcmp(name, "fallback") == 0)
		engine = ENGINE_FALLBACK;
	else
	{
		printf("usage: sh4bp_selftest <jit|interpreter|fallback> [filter]\n");
		return 2;
	}

	if (argc > 2)
		filter = argv[2];

	machine_init();
	machine_reset(engine);

	selftest::AtStart();

	if (!selftest::Active())
	{
		printf("the selftest session did not start\n");
		return 1;
	}

	publish(StopCause::Start, 0);

	//the park loop
	for (;;)
	{
		armed = ARMED_NONE;

		//ends the process when the last test is done
		selftest::OnStop(state);

		switch (armed)
		{
		case ARMED_RESUME:
			boot();
			publish(machine_resume());
			break;

		case ARMED_STEP:
			if (boot())
				publish(StopCause::Step, 0);
			else
				publish(machine_step());
			break;

		case ARMED_OVER:
			publish(machine_step_over());
			break;

		case ARMED_OUT:
			publish(machine_step_out());
			break;

		case ARMED_RESET:
			machine_reset(engine);
			publish(StopCause::Reset, 0);
			break;

		default:
			printf("a test waits for a stop without having armed anything\n");
			return 1;
		}
	}
}
