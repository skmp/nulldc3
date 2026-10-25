/*
	sh4bp_selftest <jit|interpreter|fallback> [filter] [idleskip=off]: the selftest of the emulator
	(libswirl/debugger/dbg_selftest_sh4bp.cpp, with its driver dbg_selftest.cpp) on the machine of these tests.

	The selftest drives the guest through dbg::emu. Here dbg::emu is this file: a park loop that hands every
	stop to the selftest driver and then does what the test armed, on top of the run control stand-in of
	machine.cpp. The emulator's own park loop takes this place when the selftest runs in the emulator; what
	this run shows is that the tests themselves are right, on all three engines.

	There is no reios: a guest that is resumed or stepped at the reset vector is "booted" by what the boot trap
	leaves behind. The image is loaded again and the pc is put on its entry point, the boot program of guest.s,
	and the devices of the machine start: the interrupt source, and the vblank event of the video timing.
*/
#include "machine.h"
#include "debugger/dbg_selftest_sh4bp_guest.inc"

#include "debugger/dbg_selftest.h"
#include "hw/sh4/sh4_sched.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

using namespace dbg;

//the devices of a booted machine: an interrupt request at every 2nd slice end, a vblank at every 29th
#define BOOT_IRQ_PERIOD    2
#define BOOT_VBLANK_PERIOD 29

static Engine engine;
static const char* filter = "sh4bp";

enum Armed { ARMED_NONE, ARMED_RESUME, ARMED_STEP, ARMED_OVER, ARMED_OUT, ARMED_RESET };

static Armed armed;

//the state last published; other threads ask for it
static emu::RunState state;
static std::mutex state_mutex;

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

void SetEventBreakpoints(const u32* ids, const u8* events, u32 n)
{
	machine_event_breakpoints(ids, events, n);
}

void SetActiveMask(u32 categories)
{
	machine_active_mask(categories);
}

RunState GetRunState()
{
	std::lock_guard<std::mutex> lock(state_mutex);

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

namespace test {

// dbg_emu.cpp's, for the selftest: the calls that wait to be taken
size_t QueuedCalls()
{
	return (size_t)machine_queued_calls();
}

}

bool GetSh4Engine(Sh4EngineInfo* out)
{
#if HOST_CPU == CPU_ARM64
	out->backend = engine == ENGINE_INTERP ? "interpreter" : "arm64";
#else
	out->backend = engine == ENGINE_INTERP ? "interpreter" : "x64";
#endif
	out->caps = machine_backend()->DebugCaps();
	out->fallback = engine == ENGINE_FALLBACK;
	out->fallbackLoop = machine_used_fallback();
	out->sliceCyclesLeft = machine_backend()->SliceCyclesLeft();

	return true;
}

} }

static void publish(StopCause cause, u32 id)
{
	std::lock_guard<std::mutex> lock(state_mutex);

	state.lifetime = Lifetime::Parked;
	state.stopId++;
	state.cause = cause;
	state.breakpointId = id;
	state.notice = engine == ENGINE_FALLBACK && machine_used_fallback() ? (u32)NOTICE_SH4_FALLBACK : 0;
}

//a resume, and a step over or out that goes on as one
static void publish_running()
{
	std::lock_guard<std::mutex> lock(state_mutex);

	state.lifetime = Lifetime::Running;
	state.cause = StopCause::NoCause;
	state.breakpointId = 0;
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

	memcpy(machine_ram(G_BASE), guest_image, sizeof(guest_image));
	machine_set_pc(G_BASE);

	knobs.irq_period = BOOT_IRQ_PERIOD;
	knobs.vblank_period = BOOT_VBLANK_PERIOD;

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
		printf("usage: sh4bp_selftest <jit|interpreter|fallback> [filter] [idleskip=off]\n");
		return 2;
	}

	if (argc > 2)
		filter = argv[2];

	machine_init();

	//config:Dynarec.idleskip=no of the emulator
	if (argc > 3 && strcmp(argv[3], "idleskip=off") == 0)
		settings.dynarec.idleskip = false;
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
			publish_running();
			publish(machine_resume());
			break;

		case ARMED_STEP:
			if (boot())
				publish(StopCause::Step, 0);
			else
				publish(machine_step());
			break;

		case ARMED_OVER:
			publish_running();
			publish(machine_step_over());
			break;

		case ARMED_OUT:
			publish_running();
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
