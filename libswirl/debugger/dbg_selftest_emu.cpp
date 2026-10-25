/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of dbg::emu: the lifetime of the machine as the debugger tracks it, calls, the slice hook, the
	stop latch, the park loop and its arbitration with the host and the guest.

	    nulldc3 --config config:Headless=yes , audio:backend=none , Debugger:SelfTest=emu tests/debugger/selftest/emu_guest.bin

	The tests are guest tests but for emu.machine: each stands where a client stands and is handed every stop.
	What another thread of the debugger does (a call, a pause request) and what the host does (a stop, a reset
	or a loaded state of a stopped machine, a machine that is taken apart and made again) is done by a thread
	that the test starts: "the other thread" below. In a headless run nothing else plays the host.

	The guest is tests/debugger/selftest/emu_guest.s. A session boots whatever its command line names; a test
	that needs the guest lets the machine boot first and writes the image into main memory if it is not there,
	so the session does not depend on what it was started with.

	Four sets of tests are not part of "emu", because a session of theirs does not end with the summary of
	the driver. tests/debugger/selftest/selftest.py has what the first three have to end with.
	  emuexit.*  the guest returns to the BIOS: a headless run ends there, with its exit line
	  emurun.*   the debugger works while the guest runs to the end that an Exit.* key gives the run: the state
	             hash of the exit line has to be that of a run nobody touched
	  emuhang.*  the emulator thread stays parked: no Exit.* key ends a run whose emulated time stands still
	  emuwin.*   for a run with a window, which a person or tests/debugger/selftest/window_checklist.sh works
	             the keys, the close button and the menu of: the emulator thread is kept parked, or with a
	             pause pending, or left running, and every state that is published is printed. A guest that
	             is stopped while it runs leaves a frame queued for the window, as a program that draws does

	Checks are counted in sums where a test repeats something thousands of times: one line says how often a
	property held.
*/

#include "dbg_selftest.h"
#include "dbg_selftest_nap.h"
#include "dbg_sh4.h"

#include "libswirl.h"
#include "cfg/cfg.h"
#include "gui/gui_renderer.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_dbg.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/aica/dsp.h"

#include "../../tests/debugger/selftest/emu_guest.inc"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#if HOST_CPU == CPU_X64
#include <xmmintrin.h>
#endif

namespace dbg { namespace emu { namespace test {

// dbg_emu.cpp: invoked behind the listener with every state that is published
extern void (*observer)(const RunState& state);

// dbg_emu.cpp: how often dbg::emu has called the window, which it does to put the title of the park loop there
extern u32 pausedTitles;

// dbg_emu.cpp: the threads of the session start without a start park
extern bool plainStart;

// dbg_emu.cpp: called by OnThreadStarting() as its first statement
extern void (*threadStarting)();

// dbg_emu.cpp: how many calls wait in the queue
size_t QueuedCalls();

} } }

// reios/reios.cpp: the hook a guest program returns to
void reios_exit();

// hw/pvr/spg.cpp: when the running machine last wrote its frame rate into the title of the window
extern float last_fps;

namespace {

using namespace dbg;
using selftest::Check;
using selftest::Note;

typedef std::chrono::steady_clock Clock;

const u32 kResetVector = 0xA0000000;
const u32 kSfresAddr   = 0xA05F6890;		// the reset register of the system bus
const u32 kSfresMagic  = 0x7611;
const u32 kMarkerAddr  = 0x8C00E000;		// inside the 64 KiB that a boot of reios fills with FF
const u32 kMarker      = 0x4D41524B;
const u32 kWordAddr    = 0x8C020010;		// a word of main memory that no routine of the guest writes

Sh4Context& Ctx()
{
	return p_sh4rcb->cntx;
}

double MsSince(Clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void Nap(int ms)
{
	selftest::NapUs((s64)ms * 1000);
}

/*
	Everything that is published, in the order it was
*/
struct Seen {
	Lifetime lifetime;
	u32 stopId;
	StopCause cause;
	u32 breakpointId;
	bool onEmu;
};

std::mutex seen_lock;
std::vector<Seen> seen;

// emuwin.*: the states are printed as they are published. SEEN_PRINT_CHANGES leaves the pauses and their
// resumes out, which a session that pauses all the time has thousands of
enum { SEEN_KEEP, SEEN_PRINT_ALL, SEEN_PRINT_CHANGES };
int seen_mode;
bool seen_quiet;		// the last state that was looked at is a pause or its resume

// What a test asks the emulator thread to look at when it publishes Running: the state the guest goes on with
struct {
	bool armed;
	u32 pc, spc;
	bool latched, deferred, sleepCut;
	u32 mxcsr;
} leave;

void Observer(const emu::RunState& state)
{
	bool on_emu = emu::OnEmuThread();

	{
		std::lock_guard<std::mutex> lock(seen_lock);

		if (seen_mode == SEEN_KEEP)
		{
			seen.push_back({ state.lifetime, state.stopId, state.cause, state.breakpointId, on_emu });
		}
		else
		{
			static const char* const lifetimes[] = { "NoMachine", "Stopped", "Running", "Parked" };

			bool pause = state.lifetime == Lifetime::Parked && state.cause == StopCause::Pause;
			bool quiet = pause || (state.lifetime == Lifetime::Running && seen_quiet);

			if (seen_mode == SEEN_PRINT_ALL || !quiet)
			{
				printf("emuwin: state %s %s stop %u\n", lifetimes[(int)state.lifetime], StopCauseName(state.cause), state.stopId);
				fflush(stdout);
			}

			seen_quiet = quiet;
		}
	}

	if (on_emu && state.lifetime == Lifetime::Running && leave.armed)
	{
		leave.armed = false;
		leave.pc = Ctx().pc;
		leave.spc = Ctx().spc;
		leave.latched = g_stop_latched;
		leave.deferred = g_intc_deferred;
		leave.sleepCut = g_sleep_cut;
#if HOST_CPU == CPU_X64
		leave.mxcsr = _mm_getcsr();
#endif
	}
}

std::vector<Seen> TakeSeen()
{
	std::lock_guard<std::mutex> lock(seen_lock);
	std::vector<Seen> rv;

	rv.swap(seen);
	return rv;
}

// "Parked:pause Running ..." for a note
std::string SeenText(const std::vector<Seen>& list)
{
	std::string text;

	for (const Seen& s : list)
	{
		const char* lifetime = s.lifetime == Lifetime::NoMachine ? "NoMachine" : s.lifetime == Lifetime::Stopped ? "Stopped"
			: s.lifetime == Lifetime::Running ? "Running" : "Parked";

		if (!text.empty())
			text += ' ';
		text += lifetime;
		if (s.cause != StopCause::NoCause)
			text += std::string(":") + StopCauseName(s.cause);
		text += s.onEmu ? "(E)" : "(o)";
	}

	return text;
}

bool SeenIs(const std::vector<Seen>& list, std::initializer_list<std::pair<Lifetime, StopCause>> want)
{
	if (list.size() != want.size())
		return false;

	size_t i = 0;

	for (const auto& w : want)
	{
		if (list[i].lifetime != w.first || list[i].cause != w.second)
			return false;
		i++;
	}

	return true;
}

/*
	The probe: a scheduler callback of the test's own that runs in every slice in which emulated time passes.
	It tells what the stop latch has to keep: no callback and no interrupt acceptance between the latch and
	the park, and no emulated time. It is also where a test raises its event and asks for things from inside a
	slice.
*/
enum ProbeMode {
	PROBE_PLAIN,
	PROBE_EVENT_SLEEP_PENDING,		// raises the event once, in a SLEEP wait, in a slice that has an interrupt pending
	PROBE_EVENT_SLEEP_IDLE,			// the same in a slice without one
	PROBE_EVENT_LOOP,				// raises it once outside a SLEEP wait
	PROBE_EVENT_LOOP_PAUSE,			// the same, and asks for a pause in that slice as well
	PROBE_EVENT_OFTEN,				// raises it every 64th slice
	PROBE_PAUSE_WHEN_STOPPING,		// asks for a pause in the slice in which the machine stopped its cpu
	PROBE_EVENT_HOST_STOP,			// raises the event once outside a SLEEP wait, and has the host stop the machine in
									// that slice
	PROBE_FRAME_WHEN_STOPPING,		// queues a frame for the window in the slice in which the machine stopped its cpu
};

// Counts the machines of the session: a scheduler callback belongs to the machine it was registered with
u32 machine_gen;

struct {
	int id = -1;
	u32 machine = 0;					// the machine the callback is registered with

	std::atomic<u64> calls { 0 };
	u64 lastTime = 0;
	u32 pc = 0, spc = 0, ssr = 0, srStatus = 0;
	bool sawLatch = false;				// the callback ran under the stop latch

	// SH4 time at the call (Sh4Time), and what it did from one call to the next inside SLEEP waits: a call there
	// comes SH4_TIMESLICE behind the one before it, and more when guest code was charged in between, at least the
	// block of the SLEEP (on a recompiler, which charges a block in front of its instructions). sh4TimeValid: the
	// call before was made in this run of the guest (ProbeNewRun clears it at a stop)
	u64 sh4Time = 0;
	bool sh4TimeValid = false;
	u64 sleepCalls = 0;					// calls inside a SLEEP wait with a call before them in the same run
	u64 sleepCharged = 0;				// of them, more than SH4_TIMESLICE behind the call before
	u64 sleepShort = 0;					// of them, less than SH4_TIMESLICE behind it
	u32 mxcsr = 0;

	// the first slice that saw a pause asked for, since the test cleared it
	bool pauseSeen = false;
	bool pauseSeenInSleep = false;
	u64 pauseSeenAt = 0;

	ProbeMode mode = PROBE_PLAIN;
	u64 quietUntil = 0;					// no event before this many calls
	u64 raisedAtCalls = 0;
	u64 raisedAtTime = 0;
	u32 raised = 0;
	bool asked = false;

	int slowUs = 0;						// host time a slice takes inside a SLEEP wait

	// PROBE_EVENT_HOST_STOP: what the stop callback found
	std::atomic<int> stopCallbacks { 0 };
	std::atomic<bool> stopSawRunning { false }, stopSawLatch { false };
} probe;

void ProbeRaise()
{
	probe.raised++;
	probe.raisedAtCalls = probe.calls;
	probe.raisedAtTime = sh4_sched_now64();

	DBG_EVENT(EV_DEBUGGER_NOTE, "selftest event %u", probe.raised);
}

// SH4 time, as the debugger reads it (dc.timing.SH4_CYCLES, dbg_regs.cpp): the scheduler's time and what the SH4 has
// used of its slice, the slice counter of the backend that ran the guest last
u64 Sh4Time()
{
	emu::Sh4EngineInfo info = { };

	emu::GetSh4Engine(&info);

	return sh4_sched_now64() + SH4_TIMESLICE - info.sliceCyclesLeft;
}

int ProbeTick(void*, int, int, int)
{
	Sh4Context& c = Ctx();

	probe.calls++;
	probe.lastTime = sh4_sched_now64();
	probe.pc = c.pc;
	probe.spc = c.spc;
	probe.ssr = c.ssr;
	probe.srStatus = c.sr.status;

	const u64 sh4_time = Sh4Time();

	if (g_in_sleep && probe.sh4TimeValid)
	{
		probe.sleepCalls++;
		if (sh4_time > probe.sh4Time + SH4_TIMESLICE)
			probe.sleepCharged++;
		else if (sh4_time < probe.sh4Time + SH4_TIMESLICE)
			probe.sleepShort++;
	}

	probe.sh4Time = sh4_time;
	probe.sh4TimeValid = true;

	if (g_stop_latched)
		probe.sawLatch = true;

	if (!probe.pauseSeen && (g_attn.load(std::memory_order_relaxed) & ATTN_PAUSE))
	{
		probe.pauseSeen = true;
		probe.pauseSeenInSleep = g_in_sleep;
		probe.pauseSeenAt = probe.calls;
	}

#if HOST_CPU == CPU_X64
	probe.mxcsr = _mm_getcsr();
#endif

	if (probe.slowUs != 0 && g_in_sleep)
		selftest::NapUs(probe.slowUs);

	switch (probe.mode)
	{
	case PROBE_EVENT_SLEEP_PENDING:
		if (g_in_sleep && c.interrupt_pend != 0 && probe.calls >= probe.quietUntil && probe.raised == 0)
			ProbeRaise();
		break;

	case PROBE_EVENT_SLEEP_IDLE:
		if (g_in_sleep && c.interrupt_pend == 0 && probe.calls >= probe.quietUntil && probe.raised == 0)
			ProbeRaise();
		break;

	case PROBE_EVENT_LOOP:
	case PROBE_EVENT_LOOP_PAUSE:
		if (!g_in_sleep && probe.calls >= probe.quietUntil && probe.raised == 0)
		{
			ProbeRaise();
			if (probe.mode == PROBE_EVENT_LOOP_PAUSE)
				emu::RequestPause();
		}
		break;

	case PROBE_EVENT_OFTEN:
		if ((probe.calls & 63) == 0)
			ProbeRaise();
		break;

	case PROBE_PAUSE_WHEN_STOPPING:
		if (!c.CpuRunning && !probe.asked)
		{
			probe.asked = true;
			emu::RequestPause();
		}
		break;

	case PROBE_EVENT_HOST_STOP:
		if (!g_in_sleep && probe.calls >= probe.quietUntil && probe.raised == 0)
		{
			// the event while the cpu runs, the stop behind it: the slice hook of this call latches the event's
			// stop for a machine that has been asked to stop
			ProbeRaise();

			virtualDreamcast->Stop([] {
				probe.stopCallbacks++;
				probe.stopSawRunning = sh4_cpu->IsRunning();
				probe.stopSawLatch = g_stop_latched;
			});
		}
		break;

	case PROBE_FRAME_WHEN_STOPPING:
		if (!c.CpuRunning && !probe.asked)
		{
			probe.asked = true;

			// A guest that draws has a frame on its way to the window at most moments; here the thread leaves
			// with one for certain. Behind the frames of the guest, and with nothing to draw or to swap: what
			// counts is that the window has a frame to take while the emulator thread is on its way out
			g_GUIRenderer->WaitQueueEmpty();
			g_GUIRenderer->QueueEmulatorFrame([] { return false; });
		}
		break;

	default:
		break;
	}

	// again in the next slice
	return 1;
}

// On the emulator thread, parked
void ProbeOn()
{
	if (probe.id < 0 || probe.machine != machine_gen)
	{
		probe.id = sh4_sched_register(nullptr, 0, ProbeTick);
		probe.machine = machine_gen;
	}

	probe.mode = PROBE_PLAIN;
	probe.slowUs = 0;
	probe.raised = 0;
	probe.asked = false;
	probe.sawLatch = false;
	probe.sh4TimeValid = false;

	sh4_sched_request(probe.id, 1);
}

void ProbeMode_(ProbeMode mode, u64 quiet_slices)
{
	probe.mode = mode;
	probe.raised = 0;
	probe.asked = false;
	probe.quietUntil = probe.calls + quiet_slices;
}

/*
	The other thread
*/
std::thread* other;

void OtherStart(std::function<void()> body)
{
	verify(other == nullptr);
	other = new std::thread(body);
}

void OtherJoin()
{
	if (other)
	{
		other->join();
		delete other;
		other = nullptr;
	}
}

// A short wait that keeps the processor: for two threads that have to meet
void Spin(u32 n)
{
	static std::atomic<u32> spun;

	for (u32 i = 0; i < n * 64; i++)
		spun.fetch_add(1, std::memory_order_relaxed);
}

bool WaitLifetime(Lifetime want, int timeout_ms = 20000)
{
	Clock::time_point t0 = Clock::now();

	while (emu::GetRunState().lifetime != want)
	{
		if (MsSince(t0) > timeout_ms)
			return false;

		selftest::NapUs(100);
	}

	return true;
}

// What a stopped machine must not show, counted. To be asked once the lifetime is Stopped: the emulator thread
// has published that as its last action and touches nothing any more, and the next one has not been started.
// IsRunning() is one word of the machine and reads neither the run flag of the cpu nor g_parked, so a run flag
// or a g_parked that a thread left set at its end shows in no answer of it: each of the three is looked at
struct StoppedLooks {
	std::atomic<u32> running { 0 }, runFlag { 0 }, parked { 0 };

	void Clear()
	{
		running = 0;
		runFlag = 0;
		parked = 0;
	}

	// One look. True when the machine does not count as running and both words are clear
	bool Look()
	{
		const bool r = sh4_cpu->IsRunning();
		const bool f = p_sh4rcb->cntx.CpuRunning != 0;
		const bool p = g_parked.load();

		if (r)
			running++;

		if (f)
			runFlag++;

		if (p)
			parked++;

		return !r && !f && !p;
	}

	bool Clean() const
	{
		return running == 0 && runFlag == 0 && parked == 0;
	}
};

// Asks for a pause until the emulator thread has parked for a stop behind after_stop
bool PauseAndWait(u32 after_stop, int timeout_ms = 20000)
{
	Clock::time_point t0 = Clock::now();

	for (;;)
	{
		emu::RunState s = emu::GetRunState();

		if (s.lifetime == Lifetime::Parked && s.stopId > after_stop)
			return true;

		if (s.lifetime == Lifetime::Running)
			emu::RequestPause();

		if (MsSince(t0) > timeout_ms)
			return false;

		std::this_thread::yield();
	}
}

// For a test that resumes every stop at once: waits until the guest runs, asks for one pause and waits for the
// stop behind *seen, which becomes *seen. One request: a second one could find the guest running again
bool PauseOnce(u32* seen, int timeout_ms = 20000)
{
	if (!WaitLifetime(Lifetime::Running, timeout_ms))
		return false;

	Clock::time_point t0 = Clock::now();

	emu::RequestPause();

	for (;;)
	{
		u32 id = emu::GetRunState().stopId;

		if (id > *seen)
		{
			*seen = id;
			return true;
		}

		if (MsSince(t0) > timeout_ms)
			return false;

		std::this_thread::yield();
	}
}

// The other thread stops the machine as the host does and waits for the emulator thread to be gone
struct HostStop {
	std::atomic<int> callbacks { 0 };
	std::atomic<bool> onEmu { false };
};

bool StopMachine(HostStop* record)
{
	virtualDreamcast->Stop([record] {
		record->callbacks++;
		record->onEmu = emu::OnEmuThread();
	});

	return WaitLifetime(Lifetime::Stopped);
}

/*
	The guest
*/

bool AtResetVector()
{
	return Ctx().pc == kResetVector;
}

// The image is where a boot of emu_guest.bin puts it
void EnsureImage()
{
	u8* at = GetMemPtr(EMU_GUEST_BASE, sizeof(emu_guest_image));

	if (memcmp(at, emu_guest_image, sizeof(emu_guest_image)) != 0)
	{
		memcpy(at, emu_guest_image, sizeof(emu_guest_image));

		// compiled code of what was there
		sh4_cpu->ResetCache();
	}
}

void Goto(u32 pc)
{
	EnsureImage();

	WriteMem32(EMU_G_count, 0);
	WriteMem32(EMU_G_wakes, 0);
	WriteMem32(EMU_G_irqs, 0);

	Ctx().pc = pc;
	sh4::OnPcWritten();
}

bool Resume()
{
	int rv = emu::Resume();

	if (rv != 0)
		Check(false, "a resume was refused: %d", rv);

	return rv == 0;
}

/*
	The first steps of every guest test: the machine is booted and parked, the probe runs.
	Returns true when that is so. Returns false when it armed something: the test returns false as well and
	comes back here with the next stop.
*/
int ready_phase;
u32 ready_stop;

bool Ready(const emu::RunState& stop)
{
	emu::test::observer = Observer;

	if (ready_phase == 0)
	{
		if (!AtResetVector())
		{
			ProbeOn();
			return true;
		}

		// the boot of reios is the first instruction: run, and stop again somewhere behind it
		ready_stop = stop.stopId;
		ready_phase = 1;

		if (!Resume())
			return true;

		OtherStart([] {
			u32 after = ready_stop;

			WaitLifetime(Lifetime::Running);
			Nap(5);
			PauseAndWait(after);
		});

		return false;
	}

	OtherJoin();
	ready_phase = 0;

	if (stop.cause != StopCause::Pause || AtResetVector())
		Check(false, "the machine boots and is paused behind its boot (\"%s\", pc %08X)", StopCauseName(stop.cause), Ctx().pc);

	ProbeOn();
	TakeSeen();

	return true;
}

u32 ExpectedMxcsr()
{
	u32 v = 0;

	if (Ctx().fpscr.RM & 1)
		v |= 3 << 13;
	if (Ctx().fpscr.DN)
		v |= (1 << 15) | (1 << 6);

	return v;
}

const u32 kMxcsrModeBits = (3 << 13) | (1 << 15) | (1 << 6);

}

/*
	engine.asked: the engine of the session is the one its configuration asks for. No filter selects it: the driver
	runs it in every session of the emulator, before the tests of the filter (dbg_selftest.cpp), and a failure
	fails the session. So a runner that asks for an engine by configuration and looks at nothing but the end of its
	sessions (tests/debugger/sh4bp/selftest.sh) does not pass a session of another engine; selftest.py compares what
	the session says with what it asked for from outside as well.
	  the SH4       the recompiler when the build has one and config:Dynarec.Enabled is set (settings.dynarec.Enable,
	                which dc_run takes for a wish), the interpreter otherwise
	  breakpoints   on the recompiler: sh4-debug-fallback with Debugger:ForceFallback=yes, and when the recompiler has
	  and steps     no breakpoints of its own (no SH4DBG_NATIVE in its DebugCaps); sh4-debug-native otherwise. On the
	                interpreter sh4-debug-native, which the key does not change
	One line: a note when the engine is the one asked for, a failed check when it is not.
*/
DBG_SELFTEST(engine_asked, "engine.asked")
{
	emu::Sh4EngineInfo info = { };

	if (!emu::GetSh4Engine(&info))
	{
		Check(false, "the engine of the session is not known: no backend of the SH4 is selected");
		return;
	}

#if FEAT_SHREC != DYNAREC_NONE
	const bool has_recompiler = true;
#else
	const bool has_recompiler = false;
#endif
	const bool forced = cfgLoadBool("Debugger", "ForceFallback", false);

	const bool recompiler = has_recompiler && settings.dynarec.Enable;
	const bool fallback = recompiler && (forced || !(info.caps & SH4DBG_NATIVE));

	const char* sh4_why = recompiler ? "config:Dynarec.Enabled=yes" :
	                      settings.dynarec.Enable ? "config:Dynarec.Enabled=yes, and the build has no recompiler" : "config:Dynarec.Enabled=no";
	const char* debug_why = !recompiler ? (forced ? "the interpreter's own, Debugger:ForceFallback=yes does not change that" : "the interpreter's own") :
	                        forced ? "Debugger:ForceFallback=yes" :
	                        fallback ? "the recompiler has no breakpoints and steps of its own" : "the recompiler's own, no Debugger:ForceFallback";

	const bool runs_recompiler = strcmp(info.backend, "interpreter") != 0;
	const char* asked_sh4 = recompiler ? "the recompiler" : "the interpreter";
	const char* asked_debug = fallback ? "sh4-debug-fallback" : "sh4-debug-native";
	const char* runs_debug = info.fallback ? "sh4-debug-fallback" : "sh4-debug-native";

	if (runs_recompiler == recompiler && info.fallback == fallback)
		Note("the engine of the session is the one its configuration asks for: the SH4 on %s (%s), its breakpoints and steps on %s (%s)",
			asked_sh4, sh4_why, asked_debug, debug_why);
	else
		Check(false, "the configuration asks for the SH4 on %s (%s) and its breakpoints and steps on %s (%s); the session runs the SH4 on %s%s%s and them on %s",
			asked_sh4, sh4_why, asked_debug, debug_why, runs_recompiler ? "the recompiler (" : "the interpreter",
			runs_recompiler ? info.backend : "", runs_recompiler ? ")" : "", runs_debug);
}

/*
	emu.machine: what holds before the guest has run
*/
DBG_SELFTEST(emu_machine, "emu.machine")
{
	emu::test::observer = Observer;

	emu::RunState s = emu::GetRunState();

	Check(s.lifetime == Lifetime::Running && s.cause == StopCause::NoCause, "the lifetime is Running while the machine tests run");
	Check(emu::OnEmuThread(), "the tests are on the emulator thread");
	Check(!g_parked.load(), "no debugger stop is in force");
	Check(g_stop_latched && !g_intc_deferred && !g_sleep_cut && !g_in_sleep, "the park context holds the stop latch, nothing is owed");

	emu::Sh4EngineInfo info = { };

	if (Check(emu::GetSh4Engine(&info), "the engine is known once a thread has selected its backends"))
		Note("backend %s, caps %u, %s, slice counter %d", info.backend, info.caps, info.fallback ? "breakpoints by the fallback" : "breakpoints native", info.sliceCyclesLeft);

	Check(emu::Resume() == kErrNotPaused, "a resume without a stop in force is refused");
	Check(emu::Step(Target::Sh4, StepKind::Into) == kErrNotPaused, "a step without a stop in force is refused");

	// Run control for a running thread is answered from the state, at once, and is not queued
	static std::atomic<int> rv, ran;
	rv = 1;
	ran = 0;

	std::thread t([] { rv = emu::Call(emu::CC_EXEC, [] { ran++; }); });
	t.join();

	Check(rv == kErrNotPaused && ran == 0, "a CC_EXEC call for a running thread returns -32003 and does not run (%d, ran %d)", rv.load(), ran.load());
	Check((g_attn.load() & ATTN_CALL) == 0, "and leaves nothing queued");
}

/*
	emu.pause: thousands of pause and resume, and what every one of them has to keep
*/
namespace {

enum PauseMode { PM_SPIN, PM_BUSY_TMU, PM_SLEEP_TMU, PM_SLEEP_MASKED, PM_END };

const u32 kPauseCycles[PM_END] = { 2000, 1000, 300, 2 };
const char* const kPauseName[PM_END] = { "a loop", "a loop with interrupts", "SLEEP waits with interrupts", "a SLEEP wait that nothing ends" };

struct {
	int phase;
	PauseMode mode;
	u32 n, last_stop;

	Clock::time_point reqTime;				// when the other thread began to ask, under seen_lock
	std::atomic<bool> otherFailed { false };

	// sums over the cycles of a mode
	u32 bad_cause, bad_id, bad_state, bad_flags, bad_time, bad_accept, bad_latency, bad_leave, bad_seq;
	u32 bad_sh4time, sh4time_compared;
	u32 pended, cuts, progress, waited;
	u64 max_slices;
	double max_ms, min_ms;
	u32 last_count;
	bool park_pended;
	u32 park_pc;
} pz;

void PauseBegin(const emu::RunState& stop)
{
	static const u32 entry[PM_END] = { EMU_G_spin, EMU_G_busy_tmu, EMU_G_sleep_tmu, EMU_G_sleep_masked };

	Goto(entry[pz.mode]);
	ProbeOn();

	if (pz.mode == PM_SLEEP_MASKED)
		probe.slowUs = 400;

	pz.n = 0;
	pz.last_stop = stop.stopId;
	pz.bad_cause = pz.bad_id = pz.bad_state = pz.bad_flags = pz.bad_time = pz.bad_accept = pz.bad_latency = pz.bad_leave = pz.bad_seq = 0;
	pz.bad_sh4time = pz.sh4time_compared = 0;
	probe.sleepCalls = probe.sleepCharged = probe.sleepShort = 0;
	pz.pended = pz.cuts = pz.progress = pz.waited = 0;
	pz.max_slices = 0;
	pz.max_ms = 0;
	pz.min_ms = 1e9;
	pz.last_count = 0;
	pz.park_pended = false;
	pz.otherFailed = false;

	TakeSeen();

	const u32 cycles = kPauseCycles[pz.mode];
	const u32 first = stop.stopId;
	const bool settle = pz.mode != PM_SPIN;

	OtherStart([cycles, first, settle] {
		u32 after = first;

		for (u32 i = 0; i < cycles; i++)
		{
			if (!WaitLifetime(Lifetime::Running))
			{
				pz.otherFailed = true;
				return;
			}

			// with interrupts and SLEEP waits: somewhere in the guest, not at the first slice end behind the resume
			if (settle)
				selftest::NapUs(200 + (i * 37) % 400);

			{
				std::lock_guard<std::mutex> lock(seen_lock);
				pz.reqTime = Clock::now();
			}

			if (!PauseOnce(&after))
			{
				pz.otherFailed = true;
				return;
			}
		}
	});

	probe.pauseSeen = false;
	leave.armed = true;
	Resume();
}

// One stop of a cycle. Returns true when the mode is done
bool PauseStop(const emu::RunState& stop)
{
	Sh4Context& c = Ctx();
	emu::RunState now = emu::GetRunState();
	double ms;

	{
		std::lock_guard<std::mutex> lock(seen_lock);
		ms = MsSince(pz.reqTime);
	}

	// what the resume before this stop went on with
	if (pz.n != 0)
	{
		bool ok = !leave.latched && !leave.deferred && !leave.sleepCut;

		if (pz.park_pended)
			ok = ok && leave.pc == EMU_G_irq && leave.spc == pz.park_pc;		// the acceptance that was owed, made on the way out
		else
			ok = ok && leave.pc == pz.park_pc;

		if (!ok)
			pz.bad_leave++;
	}

	if (stop.cause != StopCause::Pause || stop.breakpointId != 0)
		pz.bad_cause++;
	if (stop.stopId != pz.last_stop + 1)
		pz.bad_id++;
	if (now.lifetime != Lifetime::Parked || now.stopId != stop.stopId || now.cause != stop.cause)
		pz.bad_state++;
	if (!sh4_cpu->IsRunning() || c.CpuRunning || !g_stop_latched || !g_parked)
		pz.bad_flags++;

	// no emulated time, no scheduler callback, no instruction and no interrupt acceptance since the slice end
	// of the latch
	if (sh4_sched_now64() != probe.lastTime || probe.sawLatch)
		pz.bad_time++;
	// (a SLEEP whose wait the latch ended is executed again: the pc is back on it)
	if (c.pc != (g_sleep_cut ? probe.pc - 2 : probe.pc) || c.spc != probe.spc || c.ssr != probe.ssr || c.sr.status != probe.srStatus)
		pz.bad_accept++;

	// SH4 time as well: the slice counter the probe read in the UpdateSystem of the latch is the one the loop was
	// left with (stored in front of that UpdateSystem, and kept). A SLEEP whose wait the latch ended is executed
	// again behind the latch, as often as the slice has room for (its UpdateSystem returns at once, no time passes
	// for the scheduler): there the counter moves on, and the time is not compared
	if (!g_sleep_cut)
	{
		pz.sh4time_compared++;
		if (Sh4Time() != probe.sh4Time)
			pz.bad_sh4time++;
	}

	// the SH4 time of the next run is compared with that run's calls only
	probe.sh4TimeValid = false;

	// slices that went by with the request there to see: none when it is latched in the first of them
	u64 slices = probe.pauseSeen ? probe.calls - probe.pauseSeenAt : 0;

	if (slices > pz.max_slices)
		pz.max_slices = slices;
	if (ms > pz.max_ms)
		pz.max_ms = ms;
	if (ms < pz.min_ms)
		pz.min_ms = ms;

	if (g_sleep_cut)
		pz.cuts++;

	if (pz.mode == PM_SPIN || pz.mode == PM_BUSY_TMU)
	{
		// taken at the first slice end of the backend loop that has the request
		if (slices != 0)
			pz.bad_latency++;
	}
	else if (pz.mode == PM_SLEEP_TMU)
	{
		// taken when the wait has ended and the loop comes to a slice end. A wait is cut only for a request
		// that has been pending for the liveness bound, which a host that is busy enough can make happen
		if (g_sleep_cut && ms < kPauseLivenessMs - 5)
			pz.bad_latency++;

		if (probe.pauseSeenInSleep && slices != 0 && !g_sleep_cut)
			pz.waited++;
	}
	else
	{
		// nothing ends the wait: the request is taken once it has been pending for the liveness bound
		if (!g_sleep_cut || c.pc != EMU_G_sleep_masked_at || ms < kPauseLivenessMs - 5 || ms > kPauseLivenessMs + 1500)
			pz.bad_latency++;
	}

	u32 count = ReadMem32(pz.mode == PM_SLEEP_TMU ? EMU_G_wakes : EMU_G_count);

	if (count != pz.last_count)
		pz.progress++;
	pz.last_count = count;

	pz.park_pended = g_intc_deferred && c.interrupt_pend != 0;
	pz.park_pc = c.pc;

	if (pz.park_pended)
		pz.pended++;

	pz.last_stop = stop.stopId;
	pz.n++;

	if (pz.n < kPauseCycles[pz.mode])
	{
		probe.pauseSeen = false;
		leave.armed = true;
		Resume();
		return false;
	}

	OtherJoin();

	const u32 n = pz.n;
	const char* name = kPauseName[pz.mode];

	// the listener saw each of them, in order, on the emulator thread
	std::vector<Seen> list = TakeSeen();

	if (list.size() != 2 * (size_t)n)
		pz.bad_seq++;

	for (size_t i = 0; i < list.size(); i++)
	{
		const Seen& s = list[i];
		bool want_running = (i & 1) == 0;

		if (!s.onEmu || (want_running ? (s.lifetime != Lifetime::Running || s.cause != StopCause::NoCause)
			: (s.lifetime != Lifetime::Parked || s.cause != StopCause::Pause)))
			pz.bad_seq++;
	}

	Check(!pz.otherFailed, "%s: %u pauses were asked for by another thread and honoured", name, n);
	Check(pz.bad_cause == 0 && pz.bad_id == 0, "%s: every stop is a pause with the next stop id (%u, %u of %u are not)", name, pz.bad_cause, pz.bad_id, n);
	Check(pz.bad_state == 0 && pz.bad_flags == 0, "%s: GetRunState is the stop, the machine counts as running, the cpu is stopped under the latch (%u, %u)", name, pz.bad_state, pz.bad_flags);
	Check(pz.bad_seq == 0, "%s: the listener saw Running and Parked in turns, %zu states, all on the emulator thread", name, list.size());
	Check(pz.bad_time == 0, "%s: emulated time at the park is the time at the latch, and no scheduler callback ran in between (%u of %u)", name, pz.bad_time, n);
	Check(pz.bad_accept == 0, "%s: no interrupt was accepted and nothing executed between the latch and the park (%u of %u)", name, pz.bad_accept, n);

	if (pz.sh4time_compared != 0)
		Check(pz.bad_sh4time == 0, "%s: SH4 time at the park is the time read in the UpdateSystem of the latch (%u of %u differ; %u stops ended a SLEEP wait and are not compared)",
			name, pz.bad_sh4time, pz.sh4time_compared, n - pz.sh4time_compared);
	else
		Note("%s: every stop ended a SLEEP wait: SH4 time is not compared", name);

	if (pz.mode == PM_SLEEP_TMU)
	{
		// The block that holds the SLEEP is charged in front of the call of its handler, which calls UpdateSystem:
		// the first UpdateSystem of a wait comes more than a slice behind the one before it. A recompiler that
		// did not store its counter for the handler would show the time of its last slice end there, exactly a
		// slice behind. The interpreter charges an instruction behind it, and is not held to it
		emu::Sh4EngineInfo info = { };
		emu::GetSh4Engine(&info);

		const bool recompiler = strcmp(info.backend, "interpreter") != 0 && !info.fallbackLoop;

		if (recompiler)
			Check(probe.sleepCharged != 0 && probe.sleepShort == 0,
				"%s: on the recompiler SH4 time inside a SLEEP wait has what the guest was charged: %llu of %llu calls of UpdateSystem in waits come more than a slice behind the call before them, %llu less than a slice",
				name, (unsigned long long)probe.sleepCharged, (unsigned long long)probe.sleepCalls, (unsigned long long)probe.sleepShort);
		else
			Check(probe.sleepShort == 0, "%s: SH4 time inside a SLEEP wait: %llu calls of UpdateSystem in waits, none less than a slice behind the call before them (%llu more)",
				name, (unsigned long long)probe.sleepCalls, (unsigned long long)probe.sleepCharged);
	}
	Check(pz.bad_leave == 0, "%s: a resume goes on at the pc of the stop, or at the handler of the interrupt it owed, with nothing latched (%u of %u)", name, pz.bad_leave, n);

	if (pz.mode == PM_SPIN || pz.mode == PM_BUSY_TMU)
		Check(pz.bad_latency == 0, "%s: a pause is latched at the first slice end that has the request (%u of %u were not)", name, pz.bad_latency, n);
	else if (pz.mode == PM_SLEEP_TMU)
	{
		Check(pz.bad_latency == 0, "%s: no wait is cut for a pause request younger than %u ms (longest from request to park %.1f ms, %u cut)", name, kPauseLivenessMs, pz.max_ms, pz.cuts);
		Check(pz.waited != 0, "%s: %u of the requests were first seen inside a wait and taken behind it, at a slice end of the loop (up to %llu slices later)", name, pz.waited, (unsigned long long)pz.max_slices);
	}
	else
		Check(pz.bad_latency == 0, "%s: a pause is latched in the wait once it is %u ms old, with the pc on the SLEEP (%.0f to %.0f ms)", name, kPauseLivenessMs, pz.min_ms, pz.max_ms);

	if (pz.mode == PM_BUSY_TMU)
		Check(pz.pended != 0, "%s: %u of the stops held an interrupt back, which the resume accepted", name, pz.pended);

	if (pz.mode != PM_SLEEP_MASKED)
		Check(pz.progress > n / 2, "%s: the guest went on between the stops (%u of %u)", name, pz.progress, n);

	return true;
}

}

DBG_SELFTEST_GUEST(emu_pause, "emu.pause")
{
	if (pz.phase == 0)
	{
		if (!Ready(stop))
			return false;

		pz.phase = 1;
		pz.mode = PM_SPIN;
		PauseBegin(stop);
		return false;
	}

	if (!PauseStop(stop))
		return false;

	pz.mode = (PauseMode)(pz.mode + 1);

	if (pz.mode != PM_END)
	{
		PauseBegin(stop);
		return false;
	}

	ProbeOn();
	Goto(EMU_G_spin);

	pz.phase = 0;
	return true;
}

/*
	emu.events: an event breakpoint freezes the scheduler in the call that raised the event
*/
namespace {

struct {
	int phase;
	u32 last_stop;
	u32 raised_before;
} ev;

void EventBreakpoints(bool on)
{
	// two on one event: the lower id is the one that is reported
	static const u32 ids[2] = { 7, 3 };
	static const u8 events[2] = { (u8)EV_DEBUGGER_NOTE, (u8)EV_DEBUGGER_NOTE };

	emu::SetEventBreakpoints(ids, events, on ? 2 : 0);

	// no client is connected: the gate of the event is the test's
	g_event_on[EV_DEBUGGER_NOTE].store(on ? 1 : 0);
}

// No client is connected: the active mask is the test's as well. It is 0 until somebody sets it
bool mask_set;

void ActiveMask(u32 categories)
{
	mask_set = true;
	emu::SetActiveMask(categories);
}

bool EventStop(const emu::RunState& stop, const char* where)
{
	bool ok = Check(stop.cause == StopCause::Event && stop.breakpointId == 3 && stop.stopId == ev.last_stop + 1,
		"%s: the stop is the event's, with the lowest id of its breakpoints (\"%s\", id %u)", where, StopCauseName(stop.cause), stop.breakpointId);

	Check(probe.calls == probe.raisedAtCalls && sh4_sched_now64() == probe.raisedAtTime && !probe.sawLatch,
		"%s: the scheduler stopped in the call that raised the event: no callback behind it, the same emulated time", where);

	ev.last_stop = stop.stopId;
	return ok;
}

}

DBG_SELFTEST_GUEST(emu_events, "emu.events")
{
	Sh4Context& c = Ctx();

	switch (ev.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		ev.last_stop = stop.stopId;

		// Before anybody has set the active mask no category is active: the event is raised and nothing stops.
		// The tests in front of this one set no mask
		if (!mask_set)
		{
			Goto(EMU_G_spin);
			ProbeOn();
			EventBreakpoints(true);

			ev.phase = 8;

			ProbeMode_(PROBE_EVENT_OFTEN, 0);
			Resume();

			OtherStart([] {
				u32 after = ev.last_stop;

				WaitLifetime(Lifetime::Running);
				Nap(30);
				PauseAndWait(after);
			});
			return false;
		}

		[[fallthrough]];

	case 8:
		if (ev.phase == 8)
		{
			OtherJoin();

			Check(stop.cause == StopCause::Pause && stop.stopId == ev.last_stop + 1 && probe.raised > 3,
				"before the active mask was ever set no category is active: the event is raised (%u times) and does not stop", probe.raised);
		}

		Goto(EMU_G_sleep_tmu);
		ProbeOn();
		EventBreakpoints(true);
		ActiveMask(~0u);

		ev.last_stop = stop.stopId;
		ev.phase = 1;

		// in a SLEEP wait, in the slice that raised the interrupt the wait is for
		ProbeMode_(PROBE_EVENT_SLEEP_PENDING, 2000);
		Resume();
		return false;

	case 1:
		if (!EventStop(stop, "in a SLEEP wait, interrupt pending"))
			break;

		Check(g_sleep_cut && c.pc == EMU_G_sleep_tmu_at, "the wait was cut and the pc is on the SLEEP (%08X)", c.pc);
		Check(g_intc_deferred && c.interrupt_pend != 0 && c.spc == probe.spc && c.sr.status == probe.srStatus,
			"the interrupt was not accepted: the acceptance is owed");

		leave.armed = true;
		ev.phase = 2;

		ProbeMode_(PROBE_EVENT_SLEEP_IDLE, 200);
		Resume();
		return false;

	case 2:
		Check(leave.pc == EMU_G_irq && leave.spc == EMU_G_sleep_tmu_woken && !leave.deferred && !leave.sleepCut && !leave.latched,
			"the resume accepted it, and it returns behind the SLEEP (pc %08X, spc %08X)", leave.pc, leave.spc);

		if (!EventStop(stop, "in a SLEEP wait, nothing pending"))
			break;

		Check(g_sleep_cut && c.pc == EMU_G_sleep_tmu_at && c.interrupt_pend == 0, "the wait was cut and the pc is on the SLEEP");

		leave.armed = true;
		ev.phase = 3;

		ProbeMode_(PROBE_EVENT_SLEEP_IDLE, 200);
		Resume();
		return false;

	case 3:
		Check(leave.pc == EMU_G_sleep_tmu_at && !leave.deferred && !leave.sleepCut, "the resume accepted nothing and executes the SLEEP again (pc %08X)", leave.pc);

		if (!EventStop(stop, "in a SLEEP wait, a second time"))
			break;

		// The pc is written during a stop that cut a SLEEP: the resume does not take the new pc for that SLEEP
		Goto(EMU_G_busy_tmu);

		leave.armed = true;
		ev.phase = 4;

		ProbeMode_(PROBE_EVENT_LOOP, 500);
		Resume();
		return false;

	case 4:
		Check(leave.pc == EMU_G_busy_tmu && !leave.sleepCut, "a pc written in a stop that cut a SLEEP is where the resume goes on (pc %08X)", leave.pc);

		if (!EventStop(stop, "in a loop"))
			break;

		Check(!g_sleep_cut, "no wait was cut");

		ev.phase = 5;

		// the event and a pause request in one slice: one stop, the event's
		ProbeMode_(PROBE_EVENT_LOOP_PAUSE, 500);
		Resume();
		return false;

	case 5:
		if (!EventStop(stop, "in a loop, with a pause asked for in the same slice"))
			break;

		Check((g_attn.load() & ATTN_PAUSE) == 0, "the pause request went into that stop");

		// The category is not active (it is muted, or the last client has been gone for the grace period): the
		// event is raised and nothing stops
		ActiveMask(~CategoryBit(Category::Events));

		ev.raised_before = probe.raised;
		ev.phase = 6;

		ProbeMode_(PROBE_EVENT_OFTEN, 0);
		Resume();

		OtherStart([] {
			u32 after = ev.last_stop;

			WaitLifetime(Lifetime::Running);
			Nap(30);
			PauseAndWait(after);
		});
		return false;

	case 6:
		OtherJoin();

		Check(stop.cause == StopCause::Pause && stop.stopId == ev.last_stop + 1 && probe.raised > 3,
			"with the category inactive the event is raised (%u times) and does not stop: the next stop is the pause", probe.raised);

		ev.last_stop = stop.stopId;

		// active again, and the gate of the event closed: the raise does not reach the debugger
		ActiveMask(~0u);
		g_event_on[EV_DEBUGGER_NOTE].store(0);

		ev.phase = 7;

		ProbeMode_(PROBE_EVENT_OFTEN, 0);
		Resume();

		OtherStart([] {
			u32 after = ev.last_stop;

			WaitLifetime(Lifetime::Running);
			Nap(30);
			PauseAndWait(after);
		});
		return false;

	case 7:
		OtherJoin();

		Check(stop.cause == StopCause::Pause && stop.stopId == ev.last_stop + 1 && probe.raised > 3,
			"with the gate of the event closed nothing stops either (%u raises)", probe.raised);
		break;
	}

	EventBreakpoints(false);
	ActiveMask(~0u);
	ProbeOn();
	Goto(EMU_G_spin);
	OtherJoin();

	ev.phase = 0;
	return true;
}

/*
	emu.calls: a call of each class, for a running thread and for a parked one
*/
namespace {

// Where a closure found itself
struct Where {
	std::atomic<int> rv { 99 };
	std::atomic<int> runs { 0 };
	std::atomic<bool> onEmu { false }, latched { false }, cpuRunning { false };
	std::atomic<int> lifetime { -1 };
	std::atomic<u32> stopId { 0 };

	void Here()
	{
		runs++;
		onEmu = emu::OnEmuThread();

		// the flags of the emulator thread, when this is it
		if (onEmu)
		{
			latched = g_stop_latched;
			cpuRunning = p_sh4rcb->cntx.CpuRunning != 0;
		}

		emu::RunState s = emu::GetRunState();

		lifetime = (int)s.lifetime;
		stopId = s.stopId;
	}

	// On the emulator thread: in the park context, or in the slice hook with the cpu running
	bool Ran(bool in_park, Lifetime lt) const
	{
		return rv == 0 && runs == 1 && onEmu && latched == in_park && cpuRunning == !in_park && lifetime == (int)lt;
	}

	// By the caller itself
	bool RanOnCaller(Lifetime lt) const
	{
		return rv == 0 && runs == 1 && !onEmu && lifetime == (int)lt;
	}
};

struct {
	int phase;
	u32 stop0;

	Where rd, tb, wr, ex;			// for a running thread
	Where prd, ptb, pwr, pex, late;	// for a parked one
	std::atomic<int> resume1 { 99 }, resume2 { 99 };
	std::atomic<u32> stopAfterRunning { 0 };

	// many at once
	std::atomic<int> inside { 0 }, nested { 0 }, failed { 0 }, done { 0 };
	std::atomic<bool> otherFailed { false };
	std::atomic<bool> inFirst { false };
} cl;

void CallHere(Where* w, emu::CallClass cls)
{
	w->rv = emu::Call(cls, [w] { w->Here(); });
}

}

DBG_SELFTEST_GUEST(emu_calls, "emu.calls")
{
	switch (cl.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);
		WriteMem32(kWordAddr, 0);
		TakeSeen();

		cl.stop0 = stop.stopId;
		cl.phase = 1;
		Resume();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Running))
			{
				cl.otherFailed = true;
				return;
			}

			CallHere(&cl.rd, emu::CC_READ);
			CallHere(&cl.tb, emu::CC_TABLE);

			cl.wr.rv = emu::Call(emu::CC_WRITE, [] {
				cl.wr.Here();
				WriteMem32(kWordAddr, 0x11111111);
			});

			CallHere(&cl.ex, emu::CC_EXEC);

			cl.stopAfterRunning = emu::GetRunState().stopId;

			// four threads at once, reads and calls that need the park context mixed: one closure at a time
			std::vector<std::thread> threads;

			for (int t = 0; t < 4; t++)
			{
				threads.emplace_back([t] {
					for (int i = 0; i < 200; i++)
					{
						int rv = emu::Call(((i + t) & 3) == 0 ? emu::CC_TABLE : emu::CC_READ, [] {
							if (cl.inside.fetch_add(1) != 0)
								cl.nested++;
							cl.done++;
							cl.inside--;
						});

						if (rv != 0)
							cl.failed++;
					}
				});
			}

			for (std::thread& t : threads)
				t.join();

			if (!PauseAndWait(cl.stop0))
				cl.otherFailed = true;
		});
		return false;

	case 1:
		OtherJoin();

		Check(!cl.otherFailed, "the other thread made its calls and its pause");
		Check(cl.rd.Ran(false, Lifetime::Running), "running, CC_READ: runs on the emulator thread in the slice hook (rv %d, runs %d)", cl.rd.rv.load(), cl.rd.runs.load());
		Check(cl.tb.Ran(true, Lifetime::Running), "running, CC_TABLE: runs in the park context, in a service excursion (rv %d)", cl.tb.rv.load());
		Check(cl.wr.Ran(true, Lifetime::Running) && ReadMem32(kWordAddr) == 0x11111111, "running, CC_WRITE: the same, and its write is in guest memory (rv %d)", cl.wr.rv.load());
		Check(cl.ex.rv == kErrNotPaused && cl.ex.runs == 0, "running, CC_EXEC: -32003, not run (rv %d)", cl.ex.rv.load());
		Check(cl.stopAfterRunning == cl.stop0, "a service excursion is no stop: the stop id stayed");
		Check(cl.done == 800 && cl.failed == 0 && cl.nested == 0, "800 calls of four threads ran, one at a time (%d ran, %d failed, %d nested)", cl.done.load(), cl.failed.load(), cl.nested.load());

		{
			std::vector<Seen> list = TakeSeen();

			Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } }) && stop.stopId == cl.stop0 + 1,
				"the listener saw the resume and the pause, and nothing of the calls: %s", SeenText(list).c_str());
		}

		cl.stop0 = stop.stopId;
		cl.phase = 2;

		// parked: nothing is armed, the park loop waits for calls
		OtherStart([] {
			CallHere(&cl.prd, emu::CC_READ);
			CallHere(&cl.ptb, emu::CC_TABLE);

			cl.pwr.rv = emu::Call(emu::CC_WRITE, [] {
				cl.pwr.Here();
				WriteMem32(kWordAddr, 0x22222222);
			});

			// Run control. A second call is queued while the first is running: it is taken when the thread has
			// resumed, and a running thread takes no run control
			cl.inFirst = false;

			std::thread second([] {
				while (!cl.inFirst)
					std::this_thread::yield();

				CallHere(&cl.late, emu::CC_EXEC);
			});

			cl.pex.rv = emu::Call(emu::CC_EXEC, [] {
				cl.pex.Here();
				cl.inFirst = true;

				// the other call is in the queue by then
				Nap(20);

				cl.resume1 = emu::Resume();
				cl.resume2 = emu::Resume();
			});

			second.join();

			if (!WaitLifetime(Lifetime::Running) || !PauseAndWait(cl.stop0))
				cl.otherFailed = true;
		});
		return false;

	case 2:
		OtherJoin();

		Check(!cl.otherFailed, "the other thread made its calls and its pause");
		Check(cl.prd.Ran(true, Lifetime::Parked), "parked, CC_READ: runs on the emulator thread in the park loop (rv %d)", cl.prd.rv.load());
		Check(cl.ptb.Ran(true, Lifetime::Parked), "parked, CC_TABLE: the same (rv %d)", cl.ptb.rv.load());
		Check(cl.pwr.Ran(true, Lifetime::Parked) && ReadMem32(kWordAddr) == 0x22222222, "parked, CC_WRITE: the same, and its write is in guest memory (rv %d)", cl.pwr.rv.load());
		Check(cl.pex.Ran(true, Lifetime::Parked) && cl.resume1 == 0 && cl.resume2 == kErrNotPaused,
			"parked, CC_EXEC: runs, its resume is armed, a second one in the same call is refused (rv %d, %d, %d)", cl.pex.rv.load(), cl.resume1.load(), cl.resume2.load());
		Check(cl.late.rv == kErrNotPaused && cl.late.runs == 0, "a CC_EXEC call that was queued for the parked thread and taken by the running one: -32003, not run (rv %d)", cl.late.rv.load());

		{
			std::vector<Seen> list = TakeSeen();

			Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } }) && stop.stopId == cl.stop0 + 1,
				"the resume of the call was published, then the pause: %s", SeenText(list).c_str());
		}
		break;
	}

	cl.phase = 0;
	return true;
}

/*
	emu.cancel: a call that the emulator thread does not take in time is given up, and never runs
*/
namespace {

struct {
	int phase;
	u32 stop0;
	std::atomic<int> rvLong { 99 }, rvRead { 99 }, rvTable { 99 };
	std::atomic<int> ranRead { 0 }, ranTable { 0 };
	std::atomic<bool> otherFailed { false };
	double msLong, msRead, msTable;
} cc;

// A call whose closure uses memory of the caller, which the caller frees as soon as the call has returned:
// allowed only because a closure that did not run by then never runs
void CallWithBorrowedMemory(emu::CallClass cls, std::atomic<int>* rv, std::atomic<int>* ran, double* ms)
{
	Clock::time_point t0 = Clock::now();
	int* block = new int[256];

	memset(block, 0, 256 * sizeof(int));

	*rv = emu::Call(cls, [block, ran] {
		block[0]++;
		(*ran)++;
	});

	delete[] block;
	*ms = MsSince(t0);
}

}

DBG_SELFTEST_GUEST(emu_cancel, "emu.cancel")
{
	if (cc.phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);

		cc.stop0 = stop.stopId;
		cc.phase = 1;
		Resume();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Running))
			{
				cc.otherFailed = true;
				return;
			}

			// a closure that keeps the emulator thread for longer than a call waits
			std::thread long_call([] {
				Clock::time_point t0 = Clock::now();

				cc.rvLong = emu::Call(emu::CC_READ, [] { Nap(kCallTimeoutMs + 600); });
				cc.msLong = MsSince(t0);
			});

			Nap(100);

			std::thread read_call([] { CallWithBorrowedMemory(emu::CC_READ, &cc.rvRead, &cc.ranRead, &cc.msRead); });
			std::thread table_call([] { CallWithBorrowedMemory(emu::CC_TABLE, &cc.rvTable, &cc.ranTable, &cc.msTable); });

			long_call.join();
			read_call.join();
			table_call.join();

			// the emulator thread goes through its queue again
			Nap(50);

			if (!PauseAndWait(cc.stop0))
				cc.otherFailed = true;
		});
		return false;
	}

	OtherJoin();

	Check(!cc.otherFailed, "the other thread made its calls and its pause");
	Check(cc.rvLong == 0 && cc.msLong >= kCallTimeoutMs + 590, "a call that is running is waited for without limit (rv %d after %.0f ms)", cc.rvLong.load(), cc.msLong);
	Check(cc.rvRead == kErrBusy && cc.msRead >= kCallTimeoutMs - 20 && cc.msRead < kCallTimeoutMs + 400,
		"a CC_READ call behind it is given up with -32000 after %u ms (rv %d after %.0f ms)", kCallTimeoutMs, cc.rvRead.load(), cc.msRead);
	Check(cc.rvTable == kErrBusy && cc.msTable >= kCallTimeoutMs - 20 && cc.msTable < kCallTimeoutMs + 400,
		"and so is a CC_TABLE call (rv %d after %.0f ms)", cc.rvTable.load(), cc.msTable);
	Check(cc.ranRead == 0 && cc.ranTable == 0, "neither closure ran, then or later: what they borrowed from their callers is freed");
	Check(stop.cause == StopCause::Pause && stop.stopId == cc.stop0 + 1, "the call that was given up left nothing behind: the next stop is the pause");

	cc.phase = 0;
	return true;
}

/*
	emu.hoststop: the host stops the machine while the emulator thread is parked, while a pause is pending
	and during a call; what a call gets while the machine is stopped; a state loaded into a stopped machine
*/
namespace {

struct {
	int phase;
	u32 stop0;
	std::atomic<bool> otherFailed { false };

	// a stop of a parked thread
	HostStop first;
	std::atomic<int> order { 0 }, firstAt { 0 }, secondAt { 0 };
	emu::RunState stopped, afterLoad;
	std::atomic<bool> runningAfterStop { true };
	Where srd, stb, swr, sex, early;
	std::atomic<u32> wordSaved { 0 }, wordChanged { 0 }, wordLoaded { 0 };
	std::atomic<u32> attnStopped { 99 };
	bool sawStart = false;

	// stops with a pause pending
	std::atomic<bool> cyclesDone { false };
	std::atomic<u32> cycles { 0 }, callbacks { 0 }, hung { 0 };
	u32 parkedFirst, starts;

	// a stop during a call
	std::atomic<bool> inCall { false }, inCallback { false }, exAnswered { false }, queuedInTime { false };
	Where held, drd, dtb, dwr, dex;
	HostStop during;
} hs;

const u32 kStopCycles = 60;

}

DBG_SELFTEST_GUEST(emu_hoststop, "emu.hoststop")
{
	switch (hs.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_busy_tmu);
		WriteMem32(kWordAddr, 0x5A5A0001);
		TakeSeen();

		hs.stop0 = stop.stopId;
		hs.phase = 1;

		// parked, nothing armed: the host stops the machine, twice in a row
		OtherStart([] {
			Nap(10);

			virtualDreamcast->Stop([] {
				hs.first.callbacks++;
				hs.first.onEmu = emu::OnEmuThread();
				hs.firstAt = ++hs.order;
			});
			virtualDreamcast->Stop([] { hs.secondAt = ++hs.order; });

			if (!WaitLifetime(Lifetime::Stopped))
			{
				hs.otherFailed = true;
				return;
			}

			hs.stopped = emu::GetRunState();
			hs.runningAfterStop = sh4_cpu->IsRunning();

			emu::RequestPause();
			hs.attnStopped = g_attn.load();

			// what a call gets without an emulator thread
			CallHere(&hs.srd, emu::CC_READ);
			CallHere(&hs.stb, emu::CC_TABLE);
			CallHere(&hs.swr, emu::CC_WRITE);
			CallHere(&hs.sex, emu::CC_EXEC);

			// a state saved, the guest changed, the state loaded
			emu::Call(emu::CC_READ, [] { hs.wordSaved = ReadMem32(kWordAddr); });
			virtualDreamcast->SaveState();
			emu::Call(emu::CC_TABLE, [] {
				WriteMem32(kWordAddr, 0x5A5A0002);
				hs.wordChanged = ReadMem32(kWordAddr);
			});
			virtualDreamcast->LoadState();
			emu::Call(emu::CC_READ, [] { hs.wordLoaded = ReadMem32(kWordAddr); });

			hs.afterLoad = emu::GetRunState();

			// the host resumes: a call made while the thread starts waits for that thread, which takes it in its
			// park loop once the start park is published. Then the guest runs on, to a pause
			virtualDreamcast->Resume();
			CallHere(&hs.early, emu::CC_TABLE);

			u32 after = emu::GetRunState().stopId;

			emu::Call(emu::CC_EXEC, [] { emu::Resume(); });

			if (!PauseAndWait(after))
				hs.otherFailed = true;
		});
		return false;

	case 1:
		// the start park of the thread the host resumed: the other thread goes on from here
		if (stop.cause == StopCause::Start && !hs.sawStart)
		{
			hs.sawStart = true;
			return false;
		}

		OtherJoin();

		Check(!hs.otherFailed, "a host stop of a parked thread ends the thread");
		Check(hs.sawStart && stop.cause == StopCause::Pause, "the thread the host resumed parked at its start, was resumed by a call and paused (\"%s\")", StopCauseName(stop.cause));
		Check(hs.first.callbacks == 1 && hs.first.onEmu && hs.firstAt == 1 && hs.secondAt == 2,
			"the stop callback ran once, on the emulator thread, and the callback of a second stop behind it (%d, %d, %d)", hs.first.callbacks.load(), hs.firstAt.load(), hs.secondAt.load());
		Check(hs.stopped.lifetime == Lifetime::Stopped && hs.stopped.cause == StopCause::HostStopped && hs.stopped.stopId == hs.stop0 + 1 && !hs.runningAfterStop,
			"the state is Stopped, host-stopped, a new stop; the machine does not count as running");
		Check(hs.attnStopped == 0, "a pause request for a stopped machine does nothing");
		Check(hs.srd.RanOnCaller(Lifetime::Stopped), "stopped, CC_READ: the caller runs it itself (rv %d)", hs.srd.rv.load());
		Check(hs.stb.RanOnCaller(Lifetime::Stopped), "stopped, CC_TABLE: the caller runs it itself (rv %d)", hs.stb.rv.load());
		Check(hs.swr.rv == kErrHostStopped && hs.swr.runs == 0, "stopped, CC_WRITE: -32001, not run (rv %d)", hs.swr.rv.load());
		Check(hs.sex.rv == kErrHostStopped && hs.sex.runs == 0, "stopped, CC_EXEC: -32001, not run (rv %d)", hs.sex.rv.load());
		Check(hs.wordSaved == 0x5A5A0001 && hs.wordChanged == 0x5A5A0002 && hs.wordLoaded == 0x5A5A0001, "a state saved and loaded while stopped puts the guest back (%08X)", hs.wordLoaded.load());
		Check(hs.afterLoad.lifetime == Lifetime::Stopped && hs.afterLoad.cause == StopCause::HostStopped && hs.afterLoad.stopId == hs.stopped.stopId + 1,
			"the loaded state is a new stop of the stopped machine, the saved one is not (stop id %u after %u)", hs.afterLoad.stopId, hs.stopped.stopId);
		Check(hs.early.Ran(true, Lifetime::Parked) || hs.early.Ran(true, Lifetime::Running),
			"a call made while the thread starts is taken by that thread, in its park context (rv %d)", hs.early.rv.load());
		hs.sawStart = false;
		Check(ReadMem32(kWordAddr) == 0x5A5A0001 && Ctx().pc != kResetVector, "the thread goes on with the guest that was loaded");

		{
			std::vector<Seen> list = TakeSeen();

			Check(SeenIs(list, { { Lifetime::Stopped, StopCause::HostStopped }, { Lifetime::Stopped, StopCause::HostStopped },
					{ Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Start },
					{ Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } })
				&& list[0].onEmu && !list[1].onEmu && !list[2].onEmu && list[3].onEmu,
				"the listener saw the stop on the emulator thread, the load and the start on the host's, the start park, the resume and the pause: %s", SeenText(list).c_str());
		}

		// A pause and a stop asked for together, over and over: the stop always wins, whichever comes first
		hs.cyclesDone = false;
		hs.cycles = 0;
		hs.callbacks = 0;
		hs.hung = 0;
		hs.parkedFirst = 0;
		hs.starts = 0;
		hs.phase = 2;

		Resume();

		OtherStart([] {
			for (u32 i = 0; i < kStopCycles; i++)
			{
				if (!WaitLifetime(Lifetime::Running))
				{
					hs.hung++;
					break;
				}

				// somewhere in the guest
				selftest::NapUs(100 + (i * 53) % 900);

				emu::RequestPause();

				// every third time the pause gets the time to park first
				if (i % 3 == 2)
					Nap(2);

				virtualDreamcast->Stop([] { hs.callbacks++; });

				if (!WaitLifetime(Lifetime::Stopped))
				{
					hs.hung++;
					break;
				}

				hs.cycles++;

				if (i + 1 == kStopCycles)
					hs.cyclesDone = true;

				virtualDreamcast->Resume();
			}
		});
		return false;

	case 2:
		// the pause got there first: the thread is parked, and the stop that follows ends it
		if (stop.cause == StopCause::Pause)
		{
			hs.parkedFirst++;
			return false;
		}

		hs.starts++;

		if (!hs.cyclesDone && hs.hung == 0)
		{
			Resume();
			return false;
		}

		OtherJoin();

		Check(hs.hung == 0 && hs.cycles == kStopCycles, "%u times a pause and a host stop together: the thread ended every time", hs.cycles.load());
		Check(hs.callbacks == kStopCycles && hs.starts == kStopCycles, "every stop ran its callback, once (%u), and every resume of the host started a thread (%u)", hs.callbacks.load(), hs.starts);
		Note("the thread was parked for the pause when the stop came in %u of %u", hs.parkedFirst, kStopCycles);
		TakeSeen();

		// A stop while a call runs, and calls that are queued while the stop callback runs
		hs.inCall = false;
		hs.inCallback = false;
		hs.exAnswered = false;
		hs.queuedInTime = false;
		hs.phase = 3;
		hs.stop0 = stop.stopId;

		Resume();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Running))
			{
				hs.otherFailed = true;
				return;
			}

			std::thread holder([] {
				hs.held.rv = emu::Call(emu::CC_TABLE, [] {
					hs.held.Here();
					hs.inCall = true;
					Nap(30);
				});
			});

			while (!hs.inCall)
				std::this_thread::yield();

			virtualDreamcast->Stop([] {
				hs.during.callbacks++;
				hs.during.onEmu = emu::OnEmuThread();
				hs.inCallback = true;
				// the thread stays on its way out until the three calls below wait in its queue and CC_EXEC has
				// its answer: no window of time for them to come in (a nap of 40 ms was one, too short on a
				// hosted virtual machine); 10 s at most, should one never come
				const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while (!(emu::test::QueuedCalls() >= 3 && hs.exAnswered) && std::chrono::steady_clock::now() < until)
					Nap(1);
				hs.queuedInTime = emu::test::QueuedCalls() >= 3 && hs.exAnswered;
			});

			while (!hs.inCallback)
				std::this_thread::yield();

			// the thread is on its way out and has not said so yet: these wait for it in the queue
			std::thread t1([] { CallHere(&hs.drd, emu::CC_READ); });
			std::thread t2([] { CallHere(&hs.dtb, emu::CC_TABLE); });
			std::thread t3([] { CallHere(&hs.dwr, emu::CC_WRITE); });
			std::thread t4([] { CallHere(&hs.dex, emu::CC_EXEC); hs.exAnswered = true; });

			holder.join();
			t1.join();
			t2.join();
			t3.join();
			t4.join();

			if (!WaitLifetime(Lifetime::Stopped))
			{
				hs.otherFailed = true;
				return;
			}

			virtualDreamcast->Resume();
		});
		return false;

	case 3:
		OtherJoin();

		Check(!hs.otherFailed && stop.cause == StopCause::Start, "a host stop during a call ends the thread, and the host starts the next");
		Check(hs.held.Ran(true, Lifetime::Running), "the call that was running when the stop came ran to its end (rv %d)", hs.held.rv.load());
		Check(hs.during.callbacks == 1 && hs.during.onEmu, "the stop callback ran once, on the emulator thread");
		Check(hs.queuedInTime, "the three calls waited in the queue of the thread that leaves, and CC_EXEC had its answer, while the stop callback ran");
		Check(hs.drd.rv == 0 && hs.drd.runs == 1 && hs.drd.onEmu, "queued while the thread leaves, CC_READ: that thread runs it before it is gone (rv %d)", hs.drd.rv.load());
		Check(hs.dtb.rv == 0 && hs.dtb.runs == 1 && hs.dtb.onEmu, "queued while the thread leaves, CC_TABLE: the same (rv %d)", hs.dtb.rv.load());
		Check(hs.dwr.rv == kErrHostStopped && hs.dwr.runs == 0, "queued while the thread leaves, CC_WRITE: -32001, not run (rv %d)", hs.dwr.rv.load());
		Check(hs.dex.rv == kErrNotPaused && hs.dex.runs == 0, "CC_EXEC for the running thread that leaves: -32003, not run (rv %d)", hs.dex.rv.load());
		break;
	}

	TakeSeen();
	hs.phase = 0;
	return true;
}

/*
	emu.lifetime: the machine is reset while stopped, taken apart and made again
*/
namespace {

struct {
	int phase;
	std::atomic<bool> otherFailed { false };
	HostStop stop;
	emu::RunState stopped, reset, none, created;
	Where nrd, ntb, nwr, nex;
	std::atomic<bool> engineInNone { true };
	std::atomic<u32> attnNone { 99 };
	std::atomic<int> startRv { 99 };
	std::atomic<bool> inited { false };
} lt;

}

DBG_SELFTEST_GUEST(emu_lifetime, "emu.lifetime")
{
	if (lt.phase == 0)
	{
		if (!Ready(stop))
			return false;

		TakeSeen();
		lt.phase = 1;

		OtherStart([] {
			string image = cfgLoadStr("config", "image", "");

			if (!StopMachine(&lt.stop))
			{
				lt.otherFailed = true;
				return;
			}

			lt.stopped = emu::GetRunState();

			// the host's Restart: a reset of the stopped machine
			virtualDreamcast->Reset();
			lt.reset = emu::GetRunState();

			// the host's Exit: no machine
			virtualDreamcast.reset();
			lt.none = emu::GetRunState();

			emu::RequestPause();
			lt.attnNone = g_attn.load();

			CallHere(&lt.nrd, emu::CC_READ);

			lt.ntb.rv = emu::Call(emu::CC_TABLE, [] {
				emu::Sh4EngineInfo info;

				lt.ntb.runs++;
				lt.ntb.onEmu = emu::OnEmuThread();
				lt.ntb.lifetime = (int)emu::GetRunState().lifetime;
				lt.engineInNone = emu::GetSh4Engine(&info);
			});

			CallHere(&lt.nwr, emu::CC_WRITE);
			CallHere(&lt.nex, emu::CC_EXEC);

			// the host starts the program again
			machine_gen++;
			virtualDreamcast.reset(VirtualDreamcast::Create());
			lt.inited = virtualDreamcast->Init();

			if (!lt.inited)
			{
				lt.otherFailed = true;
				return;
			}

			lt.created = emu::GetRunState();
			lt.startRv = virtualDreamcast->StartGame(image);
		});
		return false;
	}

	OtherJoin();

	Check(!lt.otherFailed && lt.inited && lt.startRv == 0, "the machine was stopped, reset, taken apart, made again and started (%d)", lt.startRv.load());
	Check(lt.stop.callbacks == 1 && lt.stopped.lifetime == Lifetime::Stopped && lt.stopped.cause == StopCause::HostStopped, "Stopped, host-stopped, when the thread has ended");
	Check(lt.reset.lifetime == Lifetime::Stopped && lt.reset.stopId == lt.stopped.stopId + 1, "a reset of the stopped machine is a new stop of it");
	Check(lt.none.lifetime == Lifetime::NoMachine && lt.none.cause == StopCause::NoGame && lt.none.stopId == lt.reset.stopId + 1, "NoMachine, no-game, a new stop, when the machine is gone");
	Check(lt.attnNone == 0, "a pause request without a machine does nothing");
	Check(lt.nrd.rv == kErrNoGame && lt.nrd.runs == 0, "no machine, CC_READ: -32002, not run (rv %d)", lt.nrd.rv.load());
	Check(lt.ntb.rv == 0 && lt.ntb.runs == 1 && !lt.ntb.onEmu && lt.ntb.lifetime == (int)Lifetime::NoMachine, "no machine, CC_TABLE: the caller runs it itself (rv %d)", lt.ntb.rv.load());
	Check(!lt.engineInNone, "and no engine is known there");
	Check(lt.nwr.rv == kErrNoGame && lt.nwr.runs == 0, "no machine, CC_WRITE: -32002, not run (rv %d)", lt.nwr.rv.load());
	Check(lt.nex.rv == kErrNoGame && lt.nex.runs == 0, "no machine, CC_EXEC: -32002, not run (rv %d)", lt.nex.rv.load());
	Check(lt.created.lifetime == Lifetime::Stopped && lt.created.stopId == lt.none.stopId + 1, "Stopped, a new stop, when a machine is made");
	Check(stop.cause == StopCause::Start && stop.lifetime == Lifetime::Parked && AtResetVector(), "the first thread of the new machine parks at its start, at the reset vector (pc %08X)", Ctx().pc);

	{
		std::vector<Seen> list = TakeSeen();

		Check(SeenIs(list, { { Lifetime::Stopped, StopCause::HostStopped }, { Lifetime::Stopped, StopCause::HostStopped },
				{ Lifetime::NoMachine, StopCause::NoGame }, { Lifetime::Stopped, StopCause::HostStopped }, { Lifetime::Stopped, StopCause::HostStopped },
				{ Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Start } }),
			"the listener saw every one of these changes: %s", SeenText(list).c_str());
	}

	lt.phase = 0;
	return true;
}

/*
	emu.reset: a reset of the machine while parked, with a pause pending and by a call
*/
namespace {

struct {
	int phase;
	u32 stop0;
	std::atomic<bool> otherFailed { false };
	std::atomic<int> rvCall { 99 }, rvReset { 99 };
	std::atomic<u32> stopAfterCall { 0 }, marker { 0 };
} rs;

bool ResetStop(const emu::RunState& stop, const char* what)
{
	return Check(stop.cause == StopCause::Reset && stop.lifetime == Lifetime::Parked && stop.stopId == rs.stop0 + 1 && AtResetVector()
		&& sh4_cpu->IsRunning() && !Ctx().CpuRunning,
		"%s: the machine is reset and parked, with a new stop that says reset (\"%s\", pc %08X)", what, StopCauseName(stop.cause), Ctx().pc);
}

}

DBG_SELFTEST_GUEST(emu_reset, "emu.reset")
{
	switch (rs.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);
		TakeSeen();

		rs.stop0 = stop.stopId;
		rs.phase = 1;

		// the reset register, written while parked
		WriteMem32(kSfresAddr, kSfresMagic);
		return false;

	case 1:
		ResetStop(stop, "the reset register written while parked");

		{
			std::vector<Seen> list = TakeSeen();

			Check(SeenIs(list, { { Lifetime::Parked, StopCause::Reset } }), "nothing ran in between: %s", SeenText(list).c_str());
		}

		rs.phase = 2;
		[[fallthrough]];

	case 2:
		// boot again
		if (!Ready(stop))
			return false;

		// the guest writes the register itself, and a pause is asked for in the slice of that write
		Goto(EMU_G_softreset);
		ProbeMode_(PROBE_PAUSE_WHEN_STOPPING, 0);
		TakeSeen();

		rs.stop0 = stop.stopId;
		rs.phase = 3;
		Resume();
		return false;

	case 3:
		Check(probe.asked, "the pause was asked for in the slice in which the guest wrote the reset register");
		ResetStop(stop, "a reset by the guest with a pause pending");

		{
			std::vector<Seen> list = TakeSeen();

			Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Reset } }), "one stop, the reset's: %s", SeenText(list).c_str());
		}

		rs.phase = 4;
		[[fallthrough]];

	case 4:
		if (!Ready(stop))
			return false;

		// a reset by a call while the guest runs: the machine resets and runs on
		Goto(EMU_G_spin);
		WriteMem32(kMarkerAddr, kMarker);
		TakeSeen();

		rs.stop0 = stop.stopId;
		rs.phase = 5;
		Resume();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Running))
			{
				rs.otherFailed = true;
				return;
			}

			Nap(5);

			rs.rvCall = emu::Call(emu::CC_WRITE, [] { rs.rvReset = emu::Reset(); });

			// the reset: main memory is new behind it. A pause that is asked for before the machine has reset
			// would be a pause that the reset finds pending
			Clock::time_point t0 = Clock::now();

			do
			{
				Nap(2);
				emu::Call(emu::CC_READ, [] { rs.marker = ReadMem32(kMarkerAddr); });
			} while (rs.marker == kMarker && MsSince(t0) < 20000);

			rs.stopAfterCall = emu::GetRunState().stopId;

			if (!PauseAndWait(rs.stop0))
				rs.otherFailed = true;
		});
		return false;

	case 5:
		OtherJoin();

		Check(!rs.otherFailed && rs.rvCall == 0 && rs.rvReset == 0, "a reset by a call for the running thread is taken (rv %d, %d)", rs.rvCall.load(), rs.rvReset.load());
		Check(rs.marker != kMarker && ReadMem32(kMarkerAddr) != kMarker, "the machine was reset: main memory is new");
		Check(rs.stopAfterCall == rs.stop0 && stop.cause == StopCause::Pause && stop.stopId == rs.stop0 + 1, "and ran on: nothing was published until the pause");

		{
			std::vector<Seen> list = TakeSeen();

			Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } }), "the listener saw the resume and the pause: %s", SeenText(list).c_str());
		}

		// a reset by a call while parked
		rs.stop0 = stop.stopId;
		rs.phase = 6;

		OtherStart([] {
			rs.rvCall = emu::Call(emu::CC_WRITE, [] { rs.rvReset = emu::Reset(); });
		});
		return false;

	case 6:
		OtherJoin();

		Check(rs.rvCall == 0 && rs.rvReset == 0, "a reset by a call for the parked thread is taken (rv %d, %d)", rs.rvCall.load(), rs.rvReset.load());
		ResetStop(stop, "a reset by a call while parked");

		// and one made from where a call stands
		rs.stop0 = stop.stopId;
		rs.phase = 7;

		Check(emu::Reset() == 0, "a reset from the stop itself is taken");
		return false;

	case 7:
		ResetStop(stop, "a reset from the stop itself");
		break;
	}

	ProbeOn();
	rs.phase = 0;
	return true;
}

/*
	emu.resetruns: a machine that is being reset counts as running. Its cpu is stopped for the reset and its
	emulator thread goes on: a host that took it for stopped would end its user interface under that thread,
	where it stops a running machine first.
*/
namespace {

struct {
	int phase;
	u32 stop0;
	std::atomic<bool> otherFailed { false };
	std::atomic<u32> samples { 0 }, stopped { 0 };
	std::atomic<int> rvCall { 99 };

	// a reset asked for behind the end of the thread
	std::atomic<bool> inCallback { false }, lateOnEmu { false }, lateRunning { true };
	std::atomic<int> rvLate { 99 };
} rr;

// The other thread looks at the machine as a host does, as often as it can, while the reset happens; then it
// pauses the guest
void ResetWatch(bool by_call)
{
	if (!WaitLifetime(Lifetime::Running))
	{
		rr.otherFailed = true;
		return;
	}

	std::thread caller;

	if (by_call)
		caller = std::thread([] { rr.rvCall = emu::Call(emu::CC_WRITE, [] { emu::Reset(); }); });

	Clock::time_point t0 = Clock::now();

	while (MsSince(t0) < 120)
	{
		if (emu::GetRunState().lifetime == Lifetime::Running && !sh4_cpu->IsRunning())
			rr.stopped++;

		rr.samples++;
	}

	if (caller.joinable())
		caller.join();

	if (!PauseAndWait(rr.stop0))
		rr.otherFailed = true;
}

void ResetWatchBegin(const emu::RunState& stop, bool by_call)
{
	Goto(by_call ? EMU_G_spin : EMU_G_softreset);
	WriteMem32(kMarkerAddr, kMarker);

	rr.stop0 = stop.stopId;
	rr.samples = 0;
	rr.stopped = 0;

	Resume();

	OtherStart([by_call] { ResetWatch(by_call); });
}

void ResetWatchEnd(const emu::RunState& stop, const char* what)
{
	OtherJoin();

	Check(!rr.otherFailed && stop.cause == StopCause::Pause && ReadMem32(kMarkerAddr) != kMarker, "%s: the machine was reset and ran on", what);
	Check(rr.stopped == 0 && rr.samples > 1000, "%s: it counted as running every time a host looked (it did not %u times of %u)", what, rr.stopped.load(), rr.samples.load());
}

}

DBG_SELFTEST_GUEST(emu_resetruns, "emu.resetruns")
{
	switch (rr.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		rr.phase = 1;
		ResetWatchBegin(stop, false);
		return false;

	case 1:
		ResetWatchEnd(stop, "a reset by the guest");

		rr.phase = 2;
		ResetWatchBegin(stop, true);
		return false;

	case 2:
		ResetWatchEnd(stop, "a reset by a call");
		Check(rr.rvCall == 0, "the call of the reset returned 0 (%d)", rr.rvCall.load());

		rr.phase = 3;

		// The host stops the machine, and the thread takes a call on its way out, behind its stop callback,
		// that asks for a reset: no thread is left to make it
		OtherStart([] {
			rr.inCallback = false;

			virtualDreamcast->Stop([] {
				rr.inCallback = true;
				Nap(30);
			});

			while (!rr.inCallback)
				std::this_thread::yield();

			rr.rvLate = emu::Call(emu::CC_TABLE, [] {
				rr.lateOnEmu = emu::OnEmuThread();
				virtualDreamcast->RequestReset();
			});

			if (!WaitLifetime(Lifetime::Stopped))
			{
				rr.otherFailed = true;
				return;
			}

			rr.lateRunning = sh4_cpu->IsRunning();

			// put right, for the next thread to start
			if (rr.lateRunning)
				sh4_cpu->Stop();

			virtualDreamcast->Resume();
		});
		return false;

	case 3:
		OtherJoin();

		Check(!rr.otherFailed && rr.rvLate == 0 && rr.lateOnEmu && stop.cause == StopCause::Start, "a reset was asked for by a call that the leaving thread took (rv %d)", rr.rvLate.load());
		Check(!rr.lateRunning, "the stopped machine does not count as running for it");
		break;
	}

	rr.phase = 0;
	return true;
}

/*
	emu.exit: the guest ends the run while the thread is parked. (What a headless run does when the guest
	returns to the BIOS is in emuexit.*: the process ends.)
*/
namespace {

struct {
	int phase;
	std::atomic<bool> otherFailed { false };
	emu::RunState stopped;
	std::atomic<bool> running { true };
} ex;

}

DBG_SELFTEST_GUEST(emu_exit, "emu.exit")
{
	if (ex.phase == 0)
	{
		if (!Ready(stop))
			return false;

		TakeSeen();
		ex.phase = 1;

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Stopped))
			{
				ex.otherFailed = true;
				return;
			}

			ex.stopped = emu::GetRunState();
			ex.running = sh4_cpu->IsRunning();

			virtualDreamcast->Resume();
		});

		// what the exit hook of reios asks the machine for
		virtualDreamcast->RequestExit();
		return false;
	}

	OtherJoin();

	Check(!ex.otherFailed, "a guest exit while parked ends the thread");
	Check(ex.stopped.lifetime == Lifetime::Stopped && ex.stopped.cause == StopCause::GuestExit && !ex.running, "the state is Stopped, guest-exit; the machine does not count as running");
	Check(stop.cause == StopCause::Start, "the host starts the next thread");

	TakeSeen();
	ex.phase = 0;
	return true;
}

/*
	emu.fp: the host FP mode of the emulator thread is FPSCR's when the guest goes on
*/
#if HOST_CPU == CPU_X64
namespace {

struct {
	int phase;
	u32 stop0;
	std::atomic<bool> otherFailed { false };
	u32 inherited;
} fp;

const u32 kWrongMode = (1 << 13) | (1 << 6);		// round down, denormals are zero: no mode FPSCR asks for

}

DBG_SELFTEST_GUEST(emu_fp, "emu.fp")
{
	switch (fp.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);

		// something in the park context left the thread in another mode
		_mm_setcsr((_mm_getcsr() & ~kMxcsrModeBits) | kWrongMode);

		leave.armed = true;
		fp.stop0 = stop.stopId;
		fp.phase = 1;
		Resume();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Running) || !PauseAndWait(fp.stop0))
				fp.otherFailed = true;
		});
		return false;

	case 1:
		OtherJoin();

		Check(!fp.otherFailed && (leave.mxcsr & kMxcsrModeBits) == ExpectedMxcsr() && (probe.mxcsr & kMxcsrModeBits) == ExpectedMxcsr(),
			"leaving the park context puts the host FP mode back to FPSCR's (MXCSR %04X when the guest went on, FPSCR %08X)", leave.mxcsr, Ctx().fpscr.full);

		fp.stop0 = stop.stopId;
		fp.phase = 2;

		// a thread that is started by one with another mode
		OtherStart([] {
			HostStop record;

			if (!StopMachine(&record))
			{
				fp.otherFailed = true;
				return;
			}

			u32 mine = _mm_getcsr();

			_mm_setcsr((mine & ~kMxcsrModeBits) | kWrongMode);
			virtualDreamcast->Resume();
			_mm_setcsr(mine);
		});
		return false;

	case 2:
		OtherJoin();

		fp.inherited = _mm_getcsr();
		Note("the new thread starts with MXCSR %04X, its creator's", fp.inherited);

		fp.stop0 = stop.stopId;
		fp.phase = 3;
		Resume();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Running) || !PauseAndWait(fp.stop0))
				fp.otherFailed = true;
		});
		return false;

	case 3:
		OtherJoin();

		Check(!fp.otherFailed && (probe.mxcsr & kMxcsrModeBits) == ExpectedMxcsr(),
			"a thread that starts runs the guest in the host FP mode of FPSCR, whatever its creator had (MXCSR %04X in the guest)", probe.mxcsr);
		break;
	}

	fp.phase = 0;
	return true;
}
#endif

/*
	emu.startstop: the host stops the machine while its emulator thread starts, from another thread than the one
	that starts it. The threads of this test start as they do without a session: the start park of a session
	would stop the cpu itself.
*/
namespace {

struct {
	int phase;
	std::atomic<u32> cycles { 0 }, callbacks { 0 }, hung { 0 }, early { 0 };
	StoppedLooks stopped;
} st;

const u32 kStartStops = 40;

}

DBG_SELFTEST_GUEST(emu_startstop, "emu.startstop")
{
	if (st.phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);
		TakeSeen();

		st.cycles = 0;
		st.callbacks = 0;
		st.hung = 0;
		st.stopped.Clear();
		st.early = 0;
		st.phase = 1;

		OtherStart([] {
			HostStop first;

			if (!StopMachine(&first))
			{
				st.hung++;
				return;
			}

			emu::test::plainStart = true;

			for (u32 i = 0; i < kStartStops; i++)
			{
				std::thread starter([] { virtualDreamcast->Resume(); });

				// Running is published before the thread exists
				while (emu::GetRunState().lifetime == Lifetime::Stopped)
				{
				}

				Spin((i * 37) % 300);

				// the stop found the starter still waiting for its thread: the cpu of that thread is not
				// started. (The machine counts as running from the start of the thread on, IsRunning())
				if (!p_sh4rcb->cntx.CpuRunning)
					st.early++;

				virtualDreamcast->Stop([] { st.callbacks++; });

				starter.join();

				if (!WaitLifetime(Lifetime::Stopped))
				{
					st.hung++;
					break;
				}

				if (!st.stopped.Look())
				{
					// put right, for the next thread to start
					sh4_cpu->Stop();
				}

				st.cycles++;
			}

			emu::test::plainStart = false;

			virtualDreamcast->Resume();
		});
		return false;
	}

	OtherJoin();

	Check(st.hung == 0 && st.cycles == kStartStops && stop.cause == StopCause::Start, "%u times a host stop while the thread started: the thread ended every time", st.cycles.load());
	Check(st.callbacks == kStartStops, "every stop ran its callback, once (%u)", st.callbacks.load());
	Check(st.stopped.Clean(),
		"the stopped machine never counted as running, and its thread left neither the run flag of the cpu nor g_parked set (of %u times it counted as running %u, the run flag was set %u, g_parked %u)",
		kStartStops, st.stopped.running.load(), st.stopped.runFlag.load(), st.stopped.parked.load());
	Note("the stop came in before the cpu of the new thread was started in %u of %u", st.early.load(), kStartStops);

	TakeSeen();
	st.phase = 0;
	return true;
}

/*
	emu.startpark: the host stops the machine while its emulator thread starts, from another thread than the one
	that starts it, and the new thread has a park to make at its start: the start park of a session. The machine
	takes a stop for the new thread before the debugger has heard of that thread, and a stop that comes in
	between wakes no park loop. The thread finds its cpu left stopped, makes no park and ends.
*/
namespace {

struct {
	int phase;

	// the thread that starts the emulator thread is in OnThreadStarting(), where it waits for the stop
	std::atomic<bool> inGap { false }, stopMade { false };

	std::atomic<u32> gapRounds { 0 }, rounds { 0 }, callbacks { 0 }, direct { 0 }, lost { 0 }, hung { 0 };
	StoppedLooks stopped;

	// start parks of the threads that the other thread stops: counted by the emulator thread
	std::atomic<u32> parks { 0 };

	// the other thread has made its stops: the start park that follows is the next stop of the test
	std::atomic<bool> done { false };

	u32 gapParks, early;
} sk;

const u32 kGapStops = 20;
const u32 kStartParkStops = 200;

// a stop that has not ended the thread by then is one that was lost
const int kStartParkLostMs = 3000;

// On the thread that starts the emulator thread, in OnThreadStarting(): the other thread stops the machine now
void StartParkGap()
{
	sk.inGap = true;

	while (!sk.stopMade)
		std::this_thread::yield();
}

// The other thread: the emulator thread that it stopped has ended. If not, the stop is made a second time, so
// that the session goes on
bool StartParkEnded()
{
	if (!WaitLifetime(Lifetime::Stopped, kStartParkLostMs))
	{
		sk.lost++;

		virtualDreamcast->Stop(nullptr);

		if (!WaitLifetime(Lifetime::Stopped))
			sk.hung++;

		return false;
	}

	if (!sk.stopped.Look())
	{
		// put right, for the next thread to start
		sh4_cpu->Stop();
	}

	return true;
}

void StartParkStops()
{
	HostStop first;

	if (!StopMachine(&first))
	{
		sk.hung++;
		return;
	}

	// The stop comes in while the starting thread is between the two
	emu::test::threadStarting = StartParkGap;

	for (u32 i = 0; i < kGapStops; i++)
	{
		sk.inGap = false;
		sk.stopMade = false;

		std::thread starter([] { virtualDreamcast->Resume(); });

		while (!sk.inGap)
			std::this_thread::yield();

		virtualDreamcast->Stop([] { sk.callbacks++; });
		sk.stopMade = true;

		starter.join();

		if (!StartParkEnded())
			break;

		sk.gapRounds++;
	}

	emu::test::threadStarting = nullptr;

	sk.gapParks = sk.parks;

	// The stop comes in where the two threads happen to meet
	std::thread::id me = std::this_thread::get_id();

	for (u32 i = 0; i < kStartParkStops && sk.lost == 0 && sk.hung == 0; i++)
	{
		u32 parks = sk.parks;

		std::thread starter([] { virtualDreamcast->Resume(); });

		if (i % 2 == 0)
		{
			// As early as the machine takes a stop for the new thread: one stop after the other, each of which
			// runs its callback here for as long as it finds no thread
			for (;;)
			{
				u32 direct = sk.direct;

				virtualDreamcast->Stop([me] {
					if (std::this_thread::get_id() == me)
						sk.direct++;
					else
						sk.callbacks++;
				});

				if (sk.direct == direct)
					break;
			}
		}
		else
		{
			// Behind the publication of Running, where the cpu of the new thread is about to be started, and
			// behind that of its start park, where the thread is on its way into the wait of the park loop
			bool parked = i % 4 == 3;

			while (parked ? emu::GetRunState().lifetime != Lifetime::Parked : emu::GetRunState().lifetime == Lifetime::Stopped)
			{
			}

			Spin(parked ? i % 32 : (i * 37) % 300);

			virtualDreamcast->Stop([] { sk.callbacks++; });
		}

		starter.join();

		if (!StartParkEnded())
			break;

		if (sk.parks == parks)
			sk.early++;

		sk.rounds++;
	}

	sk.done = true;

	virtualDreamcast->Resume();
}

}

DBG_SELFTEST_GUEST(emu_startpark, "emu.startpark")
{
	if (sk.phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);
		TakeSeen();

		sk.gapRounds = sk.rounds = sk.callbacks = sk.direct = sk.lost = sk.hung = 0;
		sk.stopped.Clear();
		sk.parks = 0;
		sk.done = false;
		sk.gapParks = sk.early = 0;
		sk.phase = 1;

		OtherStart(StartParkStops);
		return false;
	}

	// The start park of a thread that the other thread stops: the stop ends it
	if (!sk.done)
	{
		sk.parks++;
		return false;
	}

	OtherJoin();

	Check(sk.hung == 0 && sk.lost == 0 && sk.gapRounds == kGapStops,
		"%u times a host stop for a thread that parks at its start, made when the machine takes it for that thread and the debugger has not heard of the thread: the thread ended every time (%u; %u stops were not taken within %d ms)",
		kGapStops, sk.gapRounds.load(), sk.lost.load(), kStartParkLostMs);
	Check(sk.gapParks == 0, "a thread whose cpu the machine left stopped at its start makes no park (%u parks)", sk.gapParks);
	Check(sk.hung == 0 && sk.lost == 0 && sk.rounds == kStartParkStops && stop.cause == StopCause::Start,
		"%u times a host stop wherever it meets the start of such a thread: the thread ended every time (%u)", kStartParkStops, sk.rounds.load());
	Check(sk.callbacks == sk.gapRounds + sk.rounds + sk.lost, "every stop that the machine took for a thread ran its callback, once (%u of %u)",
		sk.callbacks.load(), sk.gapRounds + sk.rounds + sk.lost);
	Check(sk.stopped.Clean(),
		"the stopped machine never counted as running, and its thread left neither the run flag of the cpu nor g_parked set (it counted as running %u times, the run flag was set %u times, g_parked %u)",
		sk.stopped.running.load(), sk.stopped.runFlag.load(), sk.stopped.parked.load());
	Note("of those %u the stop came in before the cpu of the new thread was started in %u, and with the thread parked at its start or on its way there in %u; %u stops found no thread",
		sk.rounds.load(), sk.early, sk.rounds - sk.early, sk.direct.load());

	TakeSeen();
	sk.phase = 0;
	return true;
}

/*
	emu.step: steps, as far as run control goes: the stop of a step, and a reset of the guest, a host stop, a
	call and an event breakpoint while a step executes. The step itself is the SH4 backend's; in a build whose
	backend cannot step, what is left of the test is that a step is refused and nothing is armed.
*/
namespace {

struct {
	int phase;
	u32 stop0, pc0, n;
	u32 bad;
	bool can;
	u32 dspStep0;
	std::atomic<bool> otherFailed { false };

	// a host stop during a step
	HostStop host;
	std::atomic<bool> runningInStep { false }, runningAfterStop { false }, runningInCallback { true };
	double msToStopped;
	emu::RunState stopped;

	// a call during a step
	Where read;
	std::atomic<int> rvExec { 99 }, rvStep { 99 };
} sp;

// A step of the SH4, armed
bool StepArm()
{
	const char* why = "";
	int rv = emu::Step(Target::Sh4, StepKind::Into, &why);

	if (rv != 0)
		Check(false, "a step was refused: %d %s, pc %08X", rv, why ? why : "", Ctx().pc);

	return rv == 0;
}

// The five instructions in front of the SLEEP of g_sleep_masked
const u32 kStepsToSleep = 5;

// The step the DSP stands in front of (DSP::DebugGetState); 128, which is no step, when it cannot be given
u32 DspStep()
{
	DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
	DspDebugState state = { };

	return dsp != nullptr && dsp->DebugGetState(&state) ? state.step : 128;
}

}

DBG_SELFTEST_GUEST(emu_step, "emu.step")
{
	Sh4Context& c = Ctx();

	switch (sp.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);

		// The ARM7 answers by its state: the guest of this session never releases it. (A step of a clocked ARM7
		// is armed by the same call: the session "arm7" has those.)
		Check(!sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU)->DebugEnabled() && emu::Step(Target::Arm7, StepKind::Into) == kErrNotClocked
			&& emu::Step(Target::Arm7, StepKind::Over) == kErrNotClocked && emu::Step(Target::Arm7, StepKind::Out) == kErrNotClocked,
			"the ARM7 is held in reset: a step, a step over and a step out of it answer -32012");

		// The DSP as emu::TargetSteps() has it. Without run control its three steps are -32004. With it a step over
		// and a step out are -32004 still, and a step is taken with the ARM7 held, since the AICA is clocked
		// whatever ARMRST says: one step of the DSP, one stop that says step, and no Running
		if (!emu::TargetSteps(Target::Dsp))
		{
			Check(emu::Step(Target::Dsp, StepKind::Into) == kErrNotSupported && emu::Step(Target::Dsp, StepKind::Over) == kErrNotSupported
				&& emu::Step(Target::Dsp, StepKind::Out) == kErrNotSupported, "the DSP has no steps: -32004");
		}
		else
		{
			Check(emu::Step(Target::Dsp, StepKind::Over) == kErrNotSupported && emu::Step(Target::Dsp, StepKind::Out) == kErrNotSupported,
				"the DSP steps: a step over and a step out of it answer -32004");

			TakeSeen();
			sp.stop0 = stop.stopId;
			sp.dspStep0 = DspStep();

			const int rv = emu::Step(Target::Dsp, StepKind::Into);

			Check(rv == 0, "a step of the DSP with the ARM7 held is armed (%d)", rv);

			if (rv == 0)
			{
				sp.phase = 8;
				return false;
			}
		}
		[[fallthrough]];

	case 8:
		if (sp.phase == 8)
		{
			std::vector<Seen> list = TakeSeen();
			const u32 at = DspStep();

			Check(stop.cause == StopCause::Step && stop.stopId == sp.stop0 + 1 && stop.lifetime == Lifetime::Parked && at == (sp.dspStep0 + 1) % 128
				&& SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
				"a step of the DSP with the ARM7 held: one stop that says step (\"%s\"), the DSP in front of step %u after %u, and no Running in between: %s",
				StopCauseName(stop.cause), at, sp.dspStep0, SeenText(list).c_str());

			// the steps of the SH4 below start where they start without it
			Goto(EMU_G_spin);
		}

	{
		const char* why = "";
		int rv = emu::Step(Target::Sh4, StepKind::Into, &why);

		sp.can = rv == 0;

		if (!sp.can)
		{
			Check(rv == kErrNotSupported || rv == kErrCannotStep, "this build does not step the SH4 here: the step is refused (%d %s)", rv, why ? why : "");
			Check(emu::Step(Target::Sh4, StepKind::Over) == rv && emu::Step(Target::Sh4, StepKind::Out) == rv, "and so are a step over and a step out");

			emu::RunState now = emu::GetRunState();

			Check(now.lifetime == Lifetime::Parked && now.stopId == stop.stopId, "nothing was armed: the thread stays parked on its stop");
			Note("the rest of emu.step needs a backend that steps");
			break;
		}

		Check(emu::Step(Target::Sh4, StepKind::Into) == kErrNotPaused && emu::Resume() == kErrNotPaused, "with a step armed, a second action is refused");

		TakeSeen();
		sp.stop0 = stop.stopId;
		sp.pc0 = c.pc;
		sp.n = 0;
		sp.bad = 0;
		sp.phase = 1;
		return false;
	}

	case 1:
		// sixteen steps in a row: each is one stop
		if (stop.cause != StopCause::Step || stop.stopId != sp.stop0 + 1 || stop.lifetime != Lifetime::Parked
			|| !sh4_cpu->IsRunning() || c.CpuRunning || !g_stop_latched)
			sp.bad++;

		if (sp.n == 0)
			Check(c.pc == sp.pc0 + 2, "a step executes one instruction (pc %08X after %08X)", c.pc, sp.pc0);

		sp.stop0 = stop.stopId;

		if (++sp.n < 16)
		{
			if (StepArm())
				return false;
			break;
		}

		{
			std::vector<Seen> list = TakeSeen();
			bool only_steps = list.size() == 16;

			for (const Seen& s : list)
				only_steps = only_steps && s.lifetime == Lifetime::Parked && s.cause == StopCause::Step && s.onEmu;

			Check(sp.bad == 0 && only_steps, "16 steps: each a new stop that says step, parked all through, and no Running in between: %s", SeenText(list).c_str());
		}

		// The guest writes the reset register in a step: the 5th instruction of the routine
		Goto(EMU_G_softreset);
		sp.n = 0;
		sp.phase = 2;

		if (StepArm())
			return false;
		break;

	case 2:
		sp.stop0 = stop.stopId;

		if (++sp.n < 5)
		{
			if (stop.cause != StopCause::Step)
				Check(false, "a step in front of the write stops with \"%s\"", StopCauseName(stop.cause));

			if (sp.n == 4)
				TakeSeen();

			if (StepArm())
				return false;
			break;
		}

		{
			std::vector<Seen> list = TakeSeen();

			Check(stop.cause == StopCause::Reset && AtResetVector() && stop.lifetime == Lifetime::Parked && sh4_cpu->IsRunning(),
				"a reset of the guest in a step: the machine is reset and parked, reason reset (\"%s\", pc %08X)", StopCauseName(stop.cause), c.pc);
			Check(SeenIs(list, { { Lifetime::Parked, StopCause::Reset } }), "the step and the reset are one stop: %s", SeenText(list).c_str());
		}

		sp.phase = 3;
		[[fallthrough]];

	case 3:
		// boot again
		if (!Ready(stop))
			return false;

		// A host stop while a step executes: the step is a SLEEP that nothing ends, with slices that take time
		Goto(EMU_G_sleep_masked);
		sp.n = 0;
		sp.phase = 4;

		if (StepArm())
			return false;
		break;

	case 4:
		if (++sp.n < kStepsToSleep)
		{
			if (StepArm())
				return false;
			break;
		}

		Check(c.pc == EMU_G_sleep_masked_at, "the pc is on the SLEEP (%08X)", c.pc);

		probe.slowUs = 300;
		TakeSeen();
		sp.phase = 5;

		OtherStart([] {
			Nap(60);

			sp.runningInStep = sh4_cpu->IsRunning();

			Clock::time_point t0 = Clock::now();

			virtualDreamcast->Stop([] {
				sp.host.callbacks++;
				sp.host.onEmu = emu::OnEmuThread();
				sp.runningInCallback = sh4_cpu->IsRunning();
			});

			// the step is still in its wait
			sp.runningAfterStop = sh4_cpu->IsRunning();

			if (!WaitLifetime(Lifetime::Stopped))
			{
				sp.otherFailed = true;
				return;
			}

			sp.msToStopped = MsSince(t0);
			sp.stopped = emu::GetRunState();

			virtualDreamcast->Resume();
		});

		if (StepArm())
			return false;
		break;

	case 5:
		OtherJoin();

		Check(!sp.otherFailed && sp.host.callbacks == 1 && sp.host.onEmu && sp.stopped.cause == StopCause::HostStopped,
			"a host stop during a step ends the thread: the callback ran once, the state is Stopped, host-stopped");
		Check(sp.runningInStep && sp.runningAfterStop && !sp.runningInCallback,
			"the machine counts as running while the step executes, and asked to stop until its thread takes the stop callback: not when the callback looks");
		Check(sp.msToStopped > 100, "the stop is taken when the step has returned: the SLEEP waited on (%.0f ms)", sp.msToStopped);

		{
			std::vector<Seen> list = TakeSeen();

			Check(stop.cause == StopCause::Start && SeenIs(list, { { Lifetime::Stopped, StopCause::HostStopped }, { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Start } }),
				"the stop of that step was never published: %s", SeenText(list).c_str());
		}

		// A call that arrives while a step executes waits for the park loop
		Check(c.pc == EMU_G_sleep_masked_at, "the pc is on the SLEEP again: the step ended at the bound of the wait (%08X)", c.pc);

		ProbeOn();
		probe.slowUs = 300;
		TakeSeen();

		sp.stop0 = stop.stopId;
		sp.phase = 6;

		OtherStart([] {
			Nap(60);

			// taken behind the step, which has parked by then
			CallHere(&sp.read, emu::CC_READ);

			// and another step, for the test to get a stop
			sp.rvExec = emu::Call(emu::CC_EXEC, [] { sp.rvStep = emu::Step(Target::Sh4, StepKind::Into); });
		});

		if (StepArm())
			return false;
		break;

	case 6:
		// the stop of the first step: the other thread is still in its calls, which the park loop takes now
		if (stop.stopId == sp.stop0 + 1)
		{
			Check(stop.cause == StopCause::Step && c.pc == EMU_G_sleep_masked_at, "a step on a SLEEP that nothing ends stops on the SLEEP again (\"%s\")", StopCauseName(stop.cause));
			probe.slowUs = 0;
			return false;
		}

		OtherJoin();

		Check(sp.read.Ran(true, Lifetime::Parked), "a call made while a step executed ran in the park loop, behind the step (rv %d)", sp.read.rv.load());
		Check(sp.rvExec == 0 && sp.rvStep == 0 && stop.cause == StopCause::Step && stop.stopId == sp.stop0 + 2, "a step armed by a call is executed and stops");

		// An event breakpoint while a step executes ends the step at that slice
		EventBreakpoints(true);
		ActiveMask(~0u);
		ProbeMode_(PROBE_EVENT_SLEEP_IDLE, 40);

		sp.stop0 = stop.stopId;
		sp.phase = 7;

		if (StepArm())
			return false;
		break;

	case 7:
		Check(stop.cause == StopCause::Event && stop.breakpointId == 3 && stop.stopId == sp.stop0 + 1, "an event breakpoint in a step: the stop is the event's (\"%s\", id %u)", StopCauseName(stop.cause), stop.breakpointId);
		Check(probe.calls == probe.raisedAtCalls && sh4_sched_now64() == probe.raisedAtTime && g_sleep_cut && c.pc == EMU_G_sleep_masked_at,
			"the step ended in the slice that raised the event, with the wait cut and the pc on the SLEEP");
		break;
	}

	EventBreakpoints(false);
	ProbeOn();
	OtherJoin();

	if (!AtResetVector())
		Goto(EMU_G_spin);

	sp.phase = 0;
	return true;
}

/*
	emu.stopevent: the host stops the machine in the slice in which an event breakpoint decides a stop. The
	machine stops; it does not count as running when the stop callback looks, and the stop of the event is
	never published.
*/
namespace {

struct {
	int phase;
	u32 round;
	std::atomic<bool> otherFailed { false };
	emu::RunState stopped;
	std::atomic<bool> runningStopped { true };
} se;

const u32 kStopEventRounds = 3;

void StopEventRound()
{
	Goto(se.round % 2 ? EMU_G_busy_tmu : EMU_G_spin);
	ProbeOn();
	EventBreakpoints(true);
	ActiveMask(~0u);

	probe.stopCallbacks = 0;
	probe.stopSawRunning = false;
	probe.stopSawLatch = false;
	ProbeMode_(PROBE_EVENT_HOST_STOP, 300);
	TakeSeen();

	Resume();

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Stopped))
		{
			se.otherFailed = true;
			return;
		}

		se.stopped = emu::GetRunState();
		se.runningStopped = sh4_cpu->IsRunning();

		virtualDreamcast->Resume();
	});
}

}

DBG_SELFTEST_GUEST(emu_stopevent, "emu.stopevent")
{
	if (se.phase == 0)
	{
		if (!Ready(stop))
			return false;

		se.round = 0;
		se.phase = 1;

		StopEventRound();
		return false;
	}

	OtherJoin();

	Check(!se.otherFailed && probe.raised == 1 && probe.stopCallbacks == 1 && probe.stopSawLatch,
		"round %u: the event was raised and the host stop asked for in one slice, which latched the stop of the event (callbacks %d)", se.round, probe.stopCallbacks.load());
	Check(!probe.stopSawRunning && !se.runningStopped, "round %u: the machine did not count as running when the stop callback ran, nor once it was stopped", se.round);

	{
		std::vector<Seen> list = TakeSeen();

		Check(se.stopped.cause == StopCause::HostStopped && stop.cause == StopCause::Start
			&& SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Stopped, StopCause::HostStopped }, { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Start } }),
			"round %u: the host stop won, the stop of the event was never published: %s", se.round, SeenText(list).c_str());
	}

	EventBreakpoints(false);
	ProbeOn();

	if (++se.round < kStopEventRounds)
	{
		StopEventRound();
		return false;
	}

	se.phase = 0;
	return true;
}

/*
	emu.stopresume: the host stops the machine while its emulator thread is parked in a debugger stop, and starts
	it again. The first Park() of the next thread is the resume of that stop: the instruction the stop is in
	front of executes, and a breakpoint on it stops at its next arrival, not again at once. The threads of the
	first two rounds start as they do without a session: the start park of a session is a stop of its own, whose
	resume is a resume.
*/
namespace {

struct {
	int phase;
	u32 count;
	HostStop host;
	std::atomic<bool> otherFailed { false };
} rsm;

const u32 kStopResumeLoop = EMU_G_spin_loop;			// add #1, r1
const u32 kStopResumeStore = EMU_G_spin_loop + 2;		// mov.l r1, @r2
const u32 kStopResumeMark = 0x00C0FFEE;

void StopResumeBreakpoints(u32 n)
{
	static const u32 ids[2] = { 5, 6 };
	static const u32 addrs[2] = { kStopResumeLoop, kStopResumeStore };

	emu::SetSh4Breakpoints(ids, addrs, n);
}

// The other thread is the host: it stops the machine, whose thread is parked, and starts it again
void StopResumeRestart(bool plain)
{
	OtherStart([plain] {
		if (!StopMachine(&rsm.host))
		{
			rsm.otherFailed = true;
			return;
		}

		emu::test::plainStart = plain;

		virtualDreamcast->Resume();
	});
}

}

DBG_SELFTEST_GUEST(emu_stopresume, "emu.stopresume")
{
	Sh4Context& c = Ctx();

	switch (rsm.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);
		StopResumeBreakpoints(1);
		ActiveMask(~0u);

		rsm.host.callbacks = 0;
		rsm.phase = 1;

		Resume();
		return false;

	case 1:
		if (!Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 5 && c.pc == kStopResumeLoop,
				"the loop stops at its breakpoint (\"%s\", id %u, pc %08X)", StopCauseName(stop.cause), stop.breakpointId, c.pc))
			break;

		rsm.count = c.r[1];
		rsm.phase = 2;

		TakeSeen();
		StopResumeRestart(true);
		return false;

	case 2:
		OtherJoin();

		{
			std::vector<Seen> list = TakeSeen();

			Check(!rsm.otherFailed && rsm.host.callbacks == 1
				&& SeenIs(list, { { Lifetime::Stopped, StopCause::HostStopped }, { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Breakpoint } }),
				"the host stopped the machine in the stop of a breakpoint and started it again: the stop is not applied again, the guest runs (%s)", SeenText(list).c_str());
		}

		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 5 && c.pc == kStopResumeLoop && c.r[1] == rsm.count + 1,
			"the next thread resumed that stop: the instruction of the breakpoint executed, and the breakpoint stopped at its next arrival (count %u behind %u)", c.r[1], rsm.count);

		// The pc is written in the stop, onto another breakpoint: that instruction is not the one of the stop
		StopResumeBreakpoints(2);

		rsm.count = ReadMem32(EMU_G_count);
		c.r[1] = kStopResumeMark;
		c.pc = kStopResumeStore;
		sh4::OnPcWritten();

		rsm.phase = 3;

		TakeSeen();
		StopResumeRestart(true);
		return false;

	case 3:
		OtherJoin();

		Check(!rsm.otherFailed && stop.cause == StopCause::Breakpoint && stop.breakpointId == 6 && c.pc == kStopResumeStore && ReadMem32(EMU_G_count) == rsm.count,
			"a pc that was written in the stop is not the instruction of the stop: its breakpoint stops the next thread at once, as it stops a resume (\"%s\", id %u, pc %08X, count %u, was %u)",
			StopCauseName(stop.cause), stop.breakpointId, c.pc, ReadMem32(EMU_G_count), rsm.count);

		// With the start park of a session in between
		StopResumeBreakpoints(1);

		rsm.phase = 4;

		TakeSeen();
		StopResumeRestart(false);
		return false;

	case 4:
		OtherJoin();

		if (!Check(!rsm.otherFailed && stop.cause == StopCause::Start && c.pc == kStopResumeStore, "a thread of the session parks at its start, where the thread before was stopped (\"%s\", pc %08X)", StopCauseName(stop.cause), c.pc))
			break;

		rsm.phase = 5;

		Resume();
		return false;

	case 5:
		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 5 && c.pc == kStopResumeLoop && c.r[1] == kStopResumeMark && ReadMem32(EMU_G_count) == kStopResumeMark,
			"and goes on from there when the start park is resumed (\"%s\", id %u, pc %08X, count %08X)", StopCauseName(stop.cause), stop.breakpointId, c.pc, ReadMem32(EMU_G_count));
		break;
	}

	OtherJoin();

	// a thread that a failed round left without its start park: the session goes on with the stops it gets
	emu::test::plainStart = false;

	StopResumeBreakpoints(0);
	TakeSeen();

	if (!AtResetVector())
		Goto(EMU_G_spin);

	rsm.phase = 0;
	return true;
}

/*
	emu.stopruns: a machine that has been asked to stop counts as running until its emulator thread has taken
	its stop callback. A host that acts twice in a row (the close button of the window clicked twice, the escape
	key and then the close button) looks at IsRunning() for its second action. Where it finds the machine
	stopped it ends its user interface on the spot; an emulator thread that is still on its way out waits for
	that user interface to draw the frames it queued, and never ends.
*/
namespace {

// The host stops the machine and acts again at once: what it found, and what the callbacks found
struct Twice {
	std::atomic<bool> runningBehind { false };		// IsRunning() behind the first stop
	std::atomic<int> direct { 0 };					// the second action found the machine stopped
	std::atomic<int> order { 0 }, firstAt { 0 }, secondAt { 0 };
	std::atomic<bool> runningInCallback { true };
};

struct {
	int phase;
	std::atomic<bool> otherFailed { false };
	std::atomic<bool> runningStopped { true };		// stopped, the machine was not as it has to be (StoppedLooks)
	std::atomic<int> rvCall { 99 };
	emu::RunState stopped;

	Twice parked, running;

	// the exit of the guest, and the close button behind it
	bool exitFirst, exitAgain, exitRunning;
	int exitDirect;
	std::atomic<int> exitCloses { 0 };

	// the rounds of the other thread
	std::atomic<u32> rounds { 0 }, firsts { 0 }, chained { 0 }, seconds { 0 }, direct { 0 }, bypassed { 0 }, hung { 0 };
	StoppedLooks looks, roundLooks;		// in the three single stops, and in the rounds
	std::atomic<bool> roundsDone { false };
	u32 parkedFirst, starts;
} sr;

const u32 kStopRunsRounds = 240;

// On the emulator thread, which is here and so has not taken a stop callback: the host stops the machine and
// then does what the window does for its close button (linux-dist/x11.cpp), with a count where the window ends
// its user interface
void TwiceHere(Twice* t)
{
	virtualDreamcast->Stop([t] {
		t->firstAt = ++t->order;
		t->runningInCallback = sh4_cpu->IsRunning();
	});

	t->runningBehind = sh4_cpu->IsRunning();

	if (virtualDreamcast && sh4_cpu->IsRunning())
		virtualDreamcast->Stop([t] { t->secondAt = ++t->order; });
	else
		t->direct++;
}

void TwiceCheck(const Twice& t, const char* when)
{
	Check(t.runningBehind, "%s: asked to stop, the machine counts as running while its thread has not taken the stop callback", when);
	Check(t.direct == 0 && t.firstAt == 1 && t.secondAt == 2,
		"%s: a second action of the host stops the machine as well, and its callback runs behind the first (%d, %d; found stopped %d times)",
		when, t.firstAt.load(), t.secondAt.load(), t.direct.load());
	Check(!t.runningInCallback && !sr.runningStopped && !sr.otherFailed,
		"%s: the machine does not count as running when the stop callback looks, nor once it is stopped, where the run flag of the cpu and g_parked are clear as well (counted as running %u, the run flag %u, g_parked %u)",
		when, sr.looks.running.load(), sr.looks.runFlag.load(), sr.looks.parked.load());
}

// The other thread waits for the emulator thread to be gone, and starts the next
void StopRunsNext()
{
	if (!WaitLifetime(Lifetime::Stopped))
	{
		sr.otherFailed = true;
		return;
	}

	sr.stopped = emu::GetRunState();
	sr.runningStopped = !sr.looks.Look();

	virtualDreamcast->Resume();
}

// The other thread is the host: a stop, and a second action with nothing in between, at any moment of a
// running guest, with a pause asked for and with the thread parked
void StopRunsRounds()
{
	for (u32 i = 0; i < kStopRunsRounds; i++)
	{
		if (!WaitLifetime(Lifetime::Running))
		{
			sr.hung++;
			break;
		}

		// somewhere in the guest
		selftest::NapUs(100 + (i * 53) % 900);

		if (i % 3 != 0)
			emu::RequestPause();

		// every third time the pause gets the time to park first
		if (i % 3 == 2)
			Nap(2);

		virtualDreamcast->Stop([] { sr.firsts++; });

		if (virtualDreamcast && sh4_cpu->IsRunning())
		{
			sr.chained++;
			virtualDreamcast->Stop([] { sr.seconds++; });
		}
		else
		{
			// The window ends its user interface here. It may when the emulator thread has taken its stop
			// callback: a stop then finds no thread and runs its callback at once, on this thread. A callback
			// that the emulator thread runs was taken by a thread that was still on its way out
			sr.direct++;

			virtualDreamcast->Stop([] {
				if (emu::OnEmuThread())
					sr.bypassed++;
			});
		}

		if (!WaitLifetime(Lifetime::Stopped))
		{
			sr.hung++;
			break;
		}

		sr.roundLooks.Look();

		sr.rounds++;

		if (i + 1 == kStopRunsRounds)
			sr.roundsDone = true;

		virtualDreamcast->Resume();
	}
}

}

DBG_SELFTEST_GUEST(emu_stopruns, "emu.stopruns")
{
	switch (sr.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_busy_tmu);
		TakeSeen();

		sr.otherFailed = false;
		sr.runningStopped = true;
		sr.looks.Clear();
		sr.phase = 1;

		// parked
		OtherStart(StopRunsNext);
		TwiceHere(&sr.parked);
		return false;

	case 1:
		OtherJoin();

		TwiceCheck(sr.parked, "parked");
		Check(sr.stopped.cause == StopCause::HostStopped && stop.cause == StopCause::Start, "parked: the thread ended for the host stop, and the host started the next");

		sr.runningStopped = true;
		sr.phase = 2;

		// running: the host acts from a call that the slice hook runs
		Resume();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Running))
			{
				sr.otherFailed = true;
				return;
			}

			Nap(2);

			sr.rvCall = emu::Call(emu::CC_READ, [] { TwiceHere(&sr.running); });

			StopRunsNext();
		});
		return false;

	case 2:
		OtherJoin();

		TwiceCheck(sr.running, "running");
		Check(sr.rvCall == 0 && sr.stopped.cause == StopCause::HostStopped && stop.cause == StopCause::Start, "running: the call returned 0 (%d), the thread ended for the host stop", sr.rvCall.load());

		sr.runningStopped = true;
		sr.exitCloses = 0;
		sr.exitDirect = 0;
		sr.phase = 3;

		// The guest has ended, which ends the thread as a stop does, and the close button comes behind it
		OtherStart(StopRunsNext);

		sr.exitFirst = virtualDreamcast->RequestExit();
		sr.exitRunning = sh4_cpu->IsRunning();
		sr.exitAgain = virtualDreamcast->RequestExit();

		if (virtualDreamcast && sh4_cpu->IsRunning())
			virtualDreamcast->Stop([] { sr.exitCloses++; });
		else
			sr.exitDirect++;

		return false;

	case 3:
		OtherJoin();

		Check(sr.exitFirst && !sr.exitAgain, "an exit of the guest is asked for once: the second request says that it changed nothing");
		Check(sr.exitRunning && sr.exitDirect == 0 && sr.exitCloses == 1, "a machine whose guest has ended counts as running until its thread has stopped: the close button behind the exit goes through the machine (callback ran %d times)", sr.exitCloses.load());
		Check(!sr.otherFailed && !sr.runningStopped && sr.stopped.cause == StopCause::GuestExit && stop.cause == StopCause::Start,
			"the thread ended for the guest exit; stopped, the machine does not count as running, and the run flag of the cpu and g_parked are clear (counted as running %u, the run flag %u, g_parked %u)",
			sr.looks.running.load(), sr.looks.runFlag.load(), sr.looks.parked.load());

		sr.rounds = sr.firsts = sr.chained = sr.seconds = sr.direct = sr.bypassed = sr.hung = 0;
		sr.roundLooks.Clear();
		sr.roundsDone = false;
		sr.parkedFirst = sr.starts = 0;
		sr.phase = 4;

		Resume();

		OtherStart(StopRunsRounds);
		return false;

	case 4:
		// the pause got there first: the thread is parked, and the stop that follows ends it
		if (stop.cause == StopCause::Pause)
		{
			sr.parkedFirst++;
			return false;
		}

		sr.starts++;

		if (!sr.roundsDone && sr.hung == 0)
		{
			Resume();
			return false;
		}

		OtherJoin();

		Check(sr.hung == 0 && sr.rounds == kStopRunsRounds && sr.starts == kStopRunsRounds, "%u times the host stopped the machine and acted again at once: the thread ended every time", sr.rounds.load());
		Check(sr.bypassed == 0, "the host never found a machine stopped whose thread had its stop callback still to take (it did %u times of %u)", sr.bypassed.load(), kStopRunsRounds);
		Check(sr.firsts == kStopRunsRounds && sr.seconds == sr.chained && sr.chained + sr.direct == kStopRunsRounds,
			"every callback ran once (%u, and %u of the %u second actions that went through the machine)",
			sr.firsts.load(), sr.seconds.load(), sr.chained.load());
		Check(sr.roundLooks.Clean(),
			"stopped, the machine never counted as running, and its thread left neither the run flag of the cpu nor g_parked set (of %u times it counted as running %u, the run flag was set %u, g_parked %u)",
			kStopRunsRounds, sr.roundLooks.running.load(), sr.roundLooks.runFlag.load(), sr.roundLooks.parked.load());
		Note("the second action found the thread gone %u times of %u; the thread was parked for a pause when the stop came in %u times", sr.direct.load(), kStopRunsRounds, sr.parkedFirst);
		break;
	}

	TakeSeen();
	sr.phase = 0;
	return true;
}

/*
	emu.term: the machine is taken apart under its emulator thread, whatever that thread is doing, and made
	again. The thread is ended and waited for first: until it is gone it finds its machine, also through the
	global that holds it, and the fault handler of a recompiler finds it too.
*/
namespace {

enum TermCase {
	TC_RUNNING,			// the guest runs: the boot, then the loop
	TC_PAUSE_PENDING,	// a pause is asked for and the machine goes in the same moment
	TC_EXCURSION,		// a call runs in the park context of the running thread
	TC_CALL_RESET,		// a reset that a call asked the running machine for is on its way
	TC_GUEST_RESET,		// the guest wrote the reset register
	TC_RESUME,			// the thread is parked, and a call resumes it in the same moment
	TC_END
};

const char* const kTermName[TC_END] = {
	"while the guest runs", "with a pause asked for", "during a service excursion", "with a reset by a call on its way",
	"with a reset by the guest on its way", "while a call resumes the parked thread",
};

const u32 kTermRounds = 8;

struct {
	int phase;
	u32 n;
	string image;
	std::atomic<u32> done[TC_END], notGone { 0 }, notMade { 0 }, hung { 0 };
	std::atomic<bool> inCall { false };
	u32 strays;
} tx;

// The other thread: what the host does when its user interface ends, or its menu says Exit, with the emulator
// thread still there, and a start of the program behind it
void TermRound(TermCase which, u32 round)
{
	std::thread aux;
	std::chrono::microseconds jitter((round * 7919) % 2000);

	if (which != TC_RESUME && !WaitLifetime(Lifetime::Running))
		tx.hung++;

	switch (which)
	{
	case TC_RUNNING:
	case TC_GUEST_RESET:
		selftest::NapUs(jitter.count());
		break;

	case TC_PAUSE_PENDING:
		selftest::NapUs((jitter / 8).count());
		emu::RequestPause();
		break;

	case TC_EXCURSION:
	{
		tx.inCall = false;
		aux = std::thread([] {
			emu::Call(emu::CC_TABLE, [] {
				tx.inCall = true;
				Nap(10);
			});
		});

		Clock::time_point t0 = Clock::now();

		while (!tx.inCall && MsSince(t0) < 5000)
			std::this_thread::yield();

		if (!tx.inCall)
			tx.hung++;
		break;
	}

	case TC_CALL_RESET:
		aux = std::thread([] { emu::Call(emu::CC_WRITE, [] { emu::Reset(); }); });
		selftest::NapUs(jitter.count());
		break;

	case TC_RESUME:
		aux = std::thread([round] {
			Spin((round * 13) % 60);
			emu::Call(emu::CC_EXEC, [] { emu::Resume(); });
		});
		Spin((round * 29) % 60);
		break;

	default:
		break;
	}

	virtualDreamcast.reset();

	if (emu::GetRunState().lifetime != Lifetime::NoMachine)
		tx.notGone++;

	if (aux.joinable())
		aux.join();

	machine_gen++;
	virtualDreamcast.reset(VirtualDreamcast::Create());

	if (!virtualDreamcast->Init() || virtualDreamcast->StartGame(tx.image) != 0)
	{
		tx.notMade++;
		return;
	}

	tx.done[which]++;
}

}

DBG_SELFTEST_GUEST(emu_term, "emu.term")
{
	if (tx.phase == 0)
	{
		if (!Ready(stop))
			return false;

		tx.image = cfgLoadStr("config", "image", "");
		tx.n = 0;
		tx.strays = 0;
		tx.notGone = 0;
		tx.notMade = 0;
		tx.hung = 0;
		for (auto& d : tx.done)
			d = 0;

		tx.phase = 1;
	}
	else
	{
		// a stop of the thread that is about to go (the pause got there first): nothing is armed for it
		if (stop.cause != StopCause::Start)
		{
			tx.strays++;
			return false;
		}

		// the first thread of the machine that the round before made
		OtherJoin();
	}

	if (tx.n == TC_END * kTermRounds || tx.notMade != 0)
	{
		for (u32 i = 0; i < TC_END; i++)
			Check(tx.done[i] == kTermRounds, "%u times the machine was taken apart %s, and made again", tx.done[i].load(), kTermName[i]);

		Check(tx.notGone == 0 && tx.notMade == 0 && tx.hung == 0, "every time the state was NoMachine behind it, and the next machine started (%u, %u, %u)", tx.notGone.load(), tx.notMade.load(), tx.hung.load());
		Check(stop.cause == StopCause::Start && stop.lifetime == Lifetime::Parked && AtResetVector(), "the first thread of the last machine is parked at its start");
		Note("%u stops of threads that were about to go", tx.strays);

		TakeSeen();
		tx.phase = 0;
		return true;
	}

	TermCase which = (TermCase)(tx.n % TC_END);
	u32 round = tx.n / TC_END;

	tx.n++;

	if (which == TC_GUEST_RESET)
		Goto(EMU_G_softreset);

	if (which != TC_RESUME)
		Resume();

	OtherStart([which, round] { TermRound(which, round); });
	return false;
}

/*
	emu.title: the window says that the debugger paused, at every stop. The park loop puts its title there when
	it is not on the window already, and the emulator thread calls the window for nothing else: not on its way
	out, behind the stop callback, where the thread of the user interface may be waiting for it.
*/
namespace {

struct {
	int phase;
	u32 stop0, titles, n;
	float fps;
	u32 bad, puts, taken;
	std::atomic<u32> inCallback { 0 };
	std::atomic<bool> otherFailed { false };
} ti;

const u32 kTitleStops = 30;
const int kTitleRunMs = 3000;		// the running machine writes its frame rate every 2 s

// The guest runs for run_ms and is paused again
void TitleRound(const emu::RunState& stop, int run_ms)
{
	ti.stop0 = stop.stopId;
	ti.titles = emu::test::pausedTitles;
	ti.fps = last_fps;

	Resume();

	OtherStart([run_ms] {
		if (!WaitLifetime(Lifetime::Running))
		{
			ti.otherFailed = true;
			return;
		}

		Nap(run_ms);

		if (!PauseAndWait(ti.stop0))
			ti.otherFailed = true;
	});
}

}

DBG_SELFTEST_GUEST(emu_title, "emu.title")
{
	switch (ti.phase)
	{
	case 0:
		if (!Ready(stop))
			return false;

		Goto(EMU_G_spin);
		ti.n = ti.bad = ti.puts = ti.taken = 0;
		ti.otherFailed = false;
		ti.phase = 1;

		// this thread has stopped before, and its title is on the window
		TitleRound(stop, 0);
		return false;

	case 1:
	{
		OtherJoin();

		// the title is put exactly where the running machine has taken it back since the last stop
		bool taken = last_fps != ti.fps;
		u32 put = emu::test::pausedTitles - ti.titles;

		if (stop.cause != StopCause::Pause || put != (taken ? 1u : 0u))
			ti.bad++;

		ti.puts += put;
		ti.taken += taken;

		if (++ti.n < kTitleStops && !ti.otherFailed)
		{
			TitleRound(stop, 0);
			return false;
		}

		Check(!ti.otherFailed && ti.bad == 0 && ti.puts < kTitleStops,
			"%u stops of one thread, one behind the other: the park loop put its title on the window where the running machine had taken it back, %u times, and nowhere else (%u times; %u stops were not as they have to be)",
			ti.n, ti.taken, ti.puts, ti.bad);

		ti.phase = 2;
		TitleRound(stop, kTitleRunMs);
		return false;
	}

	case 2:
		OtherJoin();

		Check(!ti.otherFailed && stop.cause == StopCause::Pause && last_fps != ti.fps && emu::test::pausedTitles == ti.titles + 1,
			"a stop behind %d ms of a running guest, which wrote its frame rate into the title: the park loop put its title there again, once (%u times)",
			kTitleRunMs, emu::test::pausedTitles - ti.titles);

		ti.phase = 3;

		// The host stops the parked machine. The thread that leaves has its title on the window
		OtherStart([] {
			virtualDreamcast->Stop([] { ti.inCallback = emu::test::pausedTitles; });

			if (!WaitLifetime(Lifetime::Stopped))
			{
				ti.otherFailed = true;
				return;
			}

			// waits for the thread that leaves to be gone
			virtualDreamcast->Resume();
		});
		return false;

	case 3:
		OtherJoin();

		Check(!ti.otherFailed && stop.cause == StopCause::Start && emu::test::pausedTitles == ti.inCallback + 1,
			"a thread that the host stops does not call the window behind its stop callback; the next thread puts the title at its first stop (%u calls since the callback)",
			emu::test::pausedTitles - ti.inCallback.load());
		break;
	}

	ti.phase = 0;
	return true;
}

/*
	emuexit.*: the guest returns to the BIOS. A headless run ends there: the process ends with the exit line of
	the run, reason guest-exit, and not with the summary of this driver.
*/
namespace {

int xe_phase;

bool ExitRun(const emu::RunState& stop, bool pending)
{
	if (xe_phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_exit);

		if (pending)
			ProbeMode_(PROBE_PAUSE_WHEN_STOPPING, 0);

		xe_phase = 1;

		Check(stop.lifetime == Lifetime::Parked, "the guest is about to return to the BIOS%s: the run ends there", pending ? ", and a pause will be pending when it does" : "");
		Resume();
		return false;
	}

	// not reached in a run that ends at the exit
	Check(false, "the emulator thread parked (\"%s\") where the guest exit should have ended the run", StopCauseName(stop.cause));
	xe_phase = 0;
	return true;
}

}

DBG_SELFTEST_GUEST(emuexit_running, "emuexit.running")
{
	return ExitRun(stop, false);
}

DBG_SELFTEST_GUEST(emuexit_pending, "emuexit.pending")
{
	return ExitRun(stop, true);
}

// The guest exit is asked for while the thread is parked
DBG_SELFTEST_GUEST(emuexit_parked, "emuexit.parked")
{
	if (xe_phase == 0)
	{
		if (!Ready(stop))
			return false;

		xe_phase = 1;

		Check(stop.lifetime == Lifetime::Parked && sh4_cpu->IsRunning(), "the thread is parked, and the guest exit is asked for: the run ends");

		// the exit hook of reios, where a step onto it would call it
		reios_exit();
		return false;
	}

	Check(false, "the emulator thread parked (\"%s\") where the guest exit should have ended the run", StopCauseName(stop.cause));
	xe_phase = 0;
	return true;
}

/*
	emurun.*: the debugger works while the guest runs to the end an Exit.* key gives the run. The tests never
	finish: the run ends with its exit line, whose state hash is compared with that of a run nobody touched.
*/
namespace {

int xr_phase;
std::atomic<u32> xr_pauses, xr_reads;

u32 RunCount(const char* key, u32 def)
{
	return (u32)cfgLoadInt("Debugger", key, (s32)def);
}

}

// Reads of guest state, as fast as the emulator thread takes them, for the whole run
DBG_SELFTEST_GUEST(emurun_reads, "emurun.reads")
{
	if (xr_phase == 0)
	{
		xr_phase = 1;

		Check(stop.cause == StopCause::Start, "the run starts parked, and is resumed with another thread reading guest state all through it");

		std::thread([] {
			static std::atomic<u64> sum;

			for (;;)
			{
				Lifetime lifetime = emu::GetRunState().lifetime;

				if (lifetime != Lifetime::Running && lifetime != Lifetime::Parked)
					break;

				emu::Call(emu::CC_READ, [] {
					Sh4Context& c = p_sh4rcb->cntx;
					const u8* ram = GetMemPtr(0x8C010000, 4096);
					u64 v = c.pc + c.r[15] + sh4_sched_now64();

					for (u32 i = 0; i < 4096; i += 64)
						v += ram[i];

					sum += v;
					xr_reads++;
				});
			}
		}).detach();

		Resume();
		return false;
	}

	Check(false, "the emulator thread parked (\"%s\") in a run that only had reads", StopCauseName(stop.cause));
	return false;
}

// Pauses, each resumed at once, for the first part of the run. Debugger:SelfTestPauses is how many
DBG_SELFTEST_GUEST(emurun_pauses, "emurun.pauses")
{
	static u32 total, last_stop, taken;

	if (xr_phase == 0)
	{
		xr_phase = 1;
		total = RunCount("SelfTestPauses", 150);
		last_stop = stop.stopId;

		Check(stop.cause == StopCause::Start, "the run starts parked, and is paused and resumed %u times by another thread on its way", total);

		std::thread([] {
			u32 after = last_stop;

			for (u32 i = 0; i < total; i++)
			{
				if (!WaitLifetime(Lifetime::Running, 5000))
					return;

				selftest::NapUs(100 + (i * 97) % 500);

				if (!PauseOnce(&after, 5000))
					return;

				xr_pauses++;
			}
		}).detach();

		Resume();
		return false;
	}

	if (stop.cause != StopCause::Pause)
		Check(false, "a stop that is no pause (\"%s\")", StopCauseName(stop.cause));

	if (++taken == total)
		Note("all %u pauses were taken and resumed", total);

	Resume();
	return false;
}

// A pause is asked for in the very slice in which the run is ended: the run ends, with the state of a run
// that had no pause
DBG_SELFTEST_GUEST(emurun_pending, "emurun.pending")
{
	if (xr_phase == 0)
	{
		xr_phase = 1;

		Check(stop.cause == StopCause::Start, "the run starts parked; a pause will be asked for in the slice that ends the run");

		ProbeOn();
		ProbeMode_(PROBE_PAUSE_WHEN_STOPPING, 0);

		Resume();
		return false;
	}

	Check(false, "the emulator thread parked (\"%s\") where the run should have ended", StopCauseName(stop.cause));
	return false;
}

/*
	emuhang.parked: the thread is parked and nothing resumes it. Emulated time stands still, so no Exit.* key
	ends the run: the process is still there when its watcher looks, with this test's line as its last.
*/
DBG_SELFTEST_GUEST(emuhang_parked, "emuhang.parked")
{
	Check(stop.cause == StopCause::Start && stop.lifetime == Lifetime::Parked, "the run is parked at its start and stays so");
	Note("parked: nothing resumes this run");
	return false;
}

/*
	emuwin.*: a run with a window. The tests never finish; what happens to the run is done at its window.
*/

// The emulator thread is parked whenever it exists
DBG_SELFTEST_GUEST(emuwin_parked, "emuwin.parked")
{
	static int phase;

	seen_mode = SEEN_PRINT_ALL;

	if (phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_busy_tmu);
		phase = 1;
	}

	Check(stop.lifetime == Lifetime::Parked, "parked: \"%s\", pc %08X, stop %u", StopCauseName(stop.cause), Ctx().pc, stop.stopId);
	return false;
}

// The guest runs, and another thread asks for a pause as soon as the last one was resumed: at any moment a
// pause is pending, or the thread is parked for one and about to go on
DBG_SELFTEST_GUEST(emuwin_pending, "emuwin.pending")
{
	static int phase;
	static std::atomic<u64> pauses;

	if (phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_busy_tmu);
		ProbeMode_(PROBE_FRAME_WHEN_STOPPING, 0);
		phase = 1;
		seen_mode = SEEN_PRINT_CHANGES;

		Check(stop.lifetime == Lifetime::Parked, "the guest runs from here, with a pause asked for all the time");

		std::thread([] {
			for (;;)
			{
				emu::RequestPause();
				std::this_thread::yield();
			}
		}).detach();
	}

	// one line now and then
	if ((++pauses & 0x3FFF) == 0 || stop.cause != StopCause::Pause)
	{
		printf("emuwin: pending, %llu stops so far, this one \"%s\", pc %08X\n", (unsigned long long)pauses.load(), StopCauseName(stop.cause), Ctx().pc);
		fflush(stdout);
	}

	Resume();
	return false;
}

// The guest runs, and the debugger leaves it alone
DBG_SELFTEST_GUEST(emuwin_running, "emuwin.running")
{
	static int phase;

	if (phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_busy_tmu);
		ProbeMode_(PROBE_FRAME_WHEN_STOPPING, 0);
		phase = 1;
		seen_mode = SEEN_PRINT_ALL;

		Check(stop.lifetime == Lifetime::Parked, "the guest runs from here, and nothing stops it");
		Resume();
		return false;
	}

	Check(false, "the emulator thread parked (\"%s\") where nothing should have stopped the guest", StopCauseName(stop.cause));
	return false;
}

// The guest returns to the BIOS in a run with a window: the emulator thread ends and the window stays, for
// the person in front of it. pending: with a pause asked for in the slice of the return
namespace {

bool WindowExit(const emu::RunState& stop, bool pending)
{
	static int phase;

	if (phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_exit);

		if (pending)
			ProbeMode_(PROBE_PAUSE_WHEN_STOPPING, 0);

		phase = 1;
		seen_mode = SEEN_PRINT_ALL;

		Check(stop.lifetime == Lifetime::Parked, "the guest is about to return to the BIOS: the thread ends there, the window stays");
		Resume();
		return false;
	}

	Check(false, "the emulator thread parked (\"%s\") where the guest exit should have ended it", StopCauseName(stop.cause));
	return false;
}

}

DBG_SELFTEST_GUEST(emuwin_exit, "emuwin.exit")
{
	return WindowExit(stop, false);
}

DBG_SELFTEST_GUEST(emuwin_exitpending, "emuwin.exitpending")
{
	return WindowExit(stop, true);
}

// Two stops of one thread with the guest running in between, long enough for the running machine to write its
// frame rate into the title of the window
DBG_SELFTEST_GUEST(emuwin_twice, "emuwin.twice")
{
	static int phase;
	static u32 stop0;

	if (phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_busy_tmu);
		phase = 1;
		stop0 = stop.stopId;

		Check(stop.lifetime == Lifetime::Parked, "the first stop; the guest runs on in a while, and is paused again");

		std::thread([] {
			Nap(2000);

			emu::Call(emu::CC_EXEC, [] { emu::Resume(); });

			printf("emuwin: twice: running\n");
			fflush(stdout);

			Nap(6000);
			PauseAndWait(stop0);
		}).detach();

		return false;
	}

	printf("emuwin: twice: the second stop, \"%s\"\n", StopCauseName(stop.cause));
	fflush(stdout);
	return false;
}

// The close button of the window while the guest resets the machine. Another thread does what the window does
// for its close button (linux-dist/x11.cpp), in the moment in which it finds the cpu stopped for the reset, or
// when it has looked for that in vain: the process ends either way
DBG_SELFTEST_GUEST(emuwin_closereset, "emuwin.closereset")
{
	static int phase;

	if (phase == 0)
	{
		if (!Ready(stop))
			return false;

		Goto(EMU_G_softreset);
		phase = 1;
		seen_mode = SEEN_PRINT_ALL;

		Check(stop.lifetime == Lifetime::Parked, "the guest is about to reset the machine, and the window is closed while it does");

		std::thread([] {
			Clock::time_point t0 = Clock::now();
			bool stopped = false;

			while (!stopped && MsSince(t0) < 500)
				stopped = emu::GetRunState().lifetime == Lifetime::Running && !sh4_cpu->IsRunning();

			printf("emuwin: close: %s\n", stopped ? "the cpu was found stopped for the reset" : "the machine counted as running all through the reset");
			fflush(stdout);

			if (virtualDreamcast && sh4_cpu->IsRunning())
				virtualDreamcast->Stop([] { g_GUIRenderer->Stop(); });
			else
				g_GUIRenderer->Stop();
		}).detach();

		Resume();
		return false;
	}

	// not reached: nothing stops the guest again
	Check(false, "the emulator thread parked (\"%s\") where the close button should have ended the run", StopCauseName(stop.cause));
	return false;
}
