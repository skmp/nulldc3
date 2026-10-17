/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	dbg::emu: the debugger on the emulator thread.

	Locks, in the order they nest
	  L   the lifetime lock: held by Dreamcast_impl across every change of the machine (LifetimeLock), by a
	      caller of Call() while it runs its closure itself, and by the emulator thread while it leaves
	  Q   the queue lock: the call queue, the published state, the requests of other threads. Held for a few
	      statements only: never across a closure, the listener or anything of the machine
	  the listener lock: held across an invocation of the listener

	What only the emulator thread touches needs no lock (the struct e below, the stop latch and its flags).
	The breakpoint tables are touched on the emulator thread, or under L while no emulator thread exists.
*/

#include "dbg_emu.h"
#include "dbg_sh4.h"
#include "dbg_arm7.h"
#include "dbg_dsp.h"
#include "dbg_selftest.h"
#include "dbg_symbols.h"

#include "libswirl.h"
#include "cfg/cfg.h"
#include "oslib/oslib.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_dbg.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/SuperH4_impl.h"
#include "hw/arm7/SoundCPU.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

// hw/pvr/spg.cpp: when the running machine last wrote its frame rate into the title of the window. Written on
// the emulator thread
extern float last_fps;

namespace dbg {

std::atomic<u32> g_attn(0);
bool g_stop_latched;
bool g_intc_deferred;
bool g_in_sleep;
bool g_sleep_cut;
std::atomic<bool> g_parked(false);

namespace emu {

namespace test {

// For the selftest (dbg_selftest_emu.cpp, which declares it as well): invoked behind the listener with every
// state that is published, on the thread that published it. Set and cleared on the emulator thread, or
// under L while no emulator thread exists.
void (*observer)(const RunState& state);

// For the selftest, which has no window to look at: how often dbg::emu has called the window, which it does
// for nothing but the title of the park loop. The emulator thread's own
u32 pausedTitles;

// For the selftest: while it is set a thread of a session starts as a thread without a session does, with no
// start park. Read by the first Park() of a thread; written while no emulator thread exists, or by that thread
bool plainStart;

// For the selftest: called by OnThreadStarting() as its first statement, on the thread that starts the emulator
// thread. The machine takes a stop request for the new thread there already, and Kick() finds no thread yet.
// Written while no emulator thread exists
void (*threadStarting)();

}

namespace {

typedef std::chrono::steady_clock Clock;

const char* const kPausedTitle = "nullDC 3.0.0 [paused by debugger]";

/*
	A call. The caller and the queue share it; everything but fn is guarded by Q. fn is run, and destroyed, by
	the thread that took the call out of the queue, or by the caller when it gave the call up.
*/
struct CallObj {
	enum State { Queued, Running, Done, Cancelled };

	CallClass cls = CC_READ;
	std::function<void()> fn;
	State state = Queued;
	int result = 0;
	std::condition_variable done;
};

std::recursive_mutex lifetime_lock;				// L

std::mutex queue_lock;							// Q
std::condition_variable park_cv;				// the park loop waits on it, with Q

// Under Q
struct {
	RunState state = { Lifetime::NoMachine, 0, StopCause::NoGame, 0, 0 };
	std::deque<std::shared_ptr<CallObj>> calls;

	bool pauseRequested = false;

	// A pause request or a call that needs the park context is waiting; since when. The slice hook leaves
	// them waiting inside a SLEEP wait for kPauseLivenessMs at most
	bool deferrable = false;
	Clock::time_point deferrableSince;

	// Kick(): the park loop goes back to dc_run
	bool kicked = false;
} shared;

std::mutex listener_lock;
void (*listener)(const RunState& state);

// What the park loop does when a call has returned
enum Action {
	ACT_NONE,
	ACT_RESUME,
	ACT_STEP,
	ACT_STEP_OVER,
	ACT_STEP_OUT,
	ACT_ARM7_STEP,
	ACT_ARM7_STEP_OVER,
	ACT_ARM7_STEP_OUT,
};

// What Leave() says when nothing was left: the sound burst it ran on was suspended again, the stop of that is
// decided and the park loop goes on with it. Never returned by Park()
const u32 kParkStay = ~0u;

// The emulator thread's own
struct {
	bool firstPark;				// Park() has not run on this thread yet
	bool inPark;				// in the park context: the guest does not execute
	bool inStep;				// a step is executing: the park loop is inside a step of the SH4, or the SH4 runs
								// towards the sound burst that a step of the ARM7 waits for. The second lasts
								// across a return of Park(), and ends at the Park() that follows
	bool stopInForce;			// a debugger stop is published and in force
	u32 stopPc;					// the pc it was published with
	bool stepped;				// a step has executed since it was published
	bool stepFallback;			// the stop that is decided ends a step the interpreter fallback made
	bool arm7Fallback;			// the stop that comes ends a step, a resume or a run in which the debug loop of the
								// sound burst stood in for the recompiler of the ARM7
	bool titled;				// the park loop of this thread has put its title on the window
	float titledAt;				// last_fps when it last did

	// A stop that is decided and not published yet: the record of LatchStop(), a start park, a pause request
	StopCause want;
	u32 wantId;

	// A freezing cause that the slice hook has not latched yet
	StopCause freeze;
	u32 freezeId;

	Action armed;
	bool resetSeen;				// the guest state was replaced on this thread since the park loop last looked
	bool machineAsked;			// a call asked the machine for a reset: dc_run evaluates
	bool sleepNoted;			// sleepPc is that of the SLEEP wait that the latch ended last
	u32 sleepPc;				// the pc Run() returned with when the latch ended a SLEEP wait: the SLEEP
	int depth;					// calls that are running: 0 or 1
} e;

thread_local bool t_is_emu;
thread_local bool t_in_call;	// this thread runs a closure itself, under L

// Under L, or on the emulator thread
struct {
	bool known;					// a thread of this machine has selected its backends
	const char* backend;
	bool jit;
	bool fallback;				// breakpoints and steps of this backend come from the interpreter fallback
	bool fallbackLoop;			// what Run() was last told to run
	bool arm7Recompiler;		// the backend that executes the ARM7 is a recompiler: the debug loop of the sound
								// burst is a fallback with other timing there
	bool arm7Fallback;			// what was last said of it: the debug loop stands in for that recompiler
} engine;

// On the emulator thread, or under L while no emulator thread exists
bool machineFirstPark;			// no thread of this machine has parked yet
bool resumeOwed;				// the last thread of this machine ended in a debugger stop, with the guest as that
								// stop left it: the first Park() of the next thread is the resume of that stop
bool startPaused;				// Debugger:StartPaused
bool forceFallback;				// Debugger:ForceFallback

// On the emulator thread, or under L while no emulator thread exists
u32 eventBp[EV_ID_COUNT];		// the lowest id of the enabled event breakpoints on an event, 0: none
u32 activeMask;					// no category is active before the first SetActiveMask()

const char* JitName()
{
#if HOST_CPU == CPU_X64
	return "x64";
#elif HOST_CPU == CPU_ARM64
	return "arm64";
#else
	return "jit";
#endif
}

// When several causes meet in one stop: the one that is published (the priority of StopCause, dbg_emu.h).
// Capture is the lowest cause: a breakpoint, a step or a pause that falls on the last frame of a capture wins
int Rank(StopCause cause)
{
	switch (cause)
	{
	case StopCause::Reset:      return 7;
	case StopCause::Start:      return 6;
	case StopCause::Breakpoint: return 5;
	case StopCause::Event:      return 4;
	case StopCause::Step:       return 3;
	case StopCause::Pause:      return 2;
	case StopCause::Capture:    return 1;
	default:                    return 0;
	}
}

void Merge(StopCause& into, u32& intoId, StopCause cause, u32 id)
{
	if (Rank(cause) > Rank(into))
	{
		into = cause;
		intoId = id;
	}
	else if (cause == into && id != 0 && (intoId == 0 || id < intoId))
	{
		intoId = id;
	}
}

bool ThreadExists(Lifetime lifetime)
{
	return lifetime == Lifetime::Running || lifetime == Lifetime::Parked;
}

// Does the debug loop of the sound burst stand in for a recompiler of the ARM7 now?
bool Arm7OnFallback()
{
	return engine.arm7Recompiler && dbg_arm7_armed != 0;
}

u32 CurrentNotice()
{
	return (engine.fallbackLoop ? (u32)NOTICE_SH4_FALLBACK : 0) | (Arm7OnFallback() ? (u32)NOTICE_ARM7_FALLBACK : 0);
}

// Q held. The published state changes, all of it at once. stop: the state is a new stop
RunState SetState(Lifetime lifetime, StopCause cause, u32 breakpointId, u32 notice, bool stop)
{
	shared.state.lifetime = lifetime;
	shared.state.cause = cause;
	shared.state.breakpointId = breakpointId;
	shared.state.notice = notice;
	if (stop)
		shared.state.stopId++;

	// A pause that was asked for while the thread was on its way into this stop is this stop: from here on
	// a request finds the thread parked and does nothing
	if (lifetime == Lifetime::Parked)
	{
		shared.pauseRequested = false;
		g_attn.fetch_and(~(u32)ATTN_PAUSE);
	}

	return shared.state;
}

// The listener hears of a state that was published. No lock of this file is held
void Announce(const RunState& state)
{
	{
		std::lock_guard<std::mutex> l(listener_lock);

		if (listener)
			listener(state);
	}

	if (test::observer)
		test::observer(state);
}

// Publishes a state and invokes the listener with it
RunState Publish(Lifetime lifetime, StopCause cause, u32 breakpointId, u32 notice, bool stop)
{
	RunState copy;

	{
		std::lock_guard<std::mutex> q(queue_lock);
		copy = SetState(lifetime, cause, breakpointId, notice, stop);
	}

	Announce(copy);

	return copy;
}

// Q held
void Complete(const std::shared_ptr<CallObj>& call, int result)
{
	call->state = CallObj::Done;
	call->result = result;
	call->done.notify_all();
}

// Runs a call that was taken out of the queue (its state is Running)
void RunCall(const std::shared_ptr<CallObj>& call)
{
	verify(e.depth == 0);
	e.depth++;

	call->fn();
	call->fn = nullptr;

	e.depth--;

	std::lock_guard<std::mutex> q(queue_lock);
	Complete(call, 0);
}

// Q held. Nothing waits in the queue any more
void QueueEmptied()
{
	g_attn.fetch_and(~(u32)ATTN_CALL);
}

/*
	The stop latch
*/

// The latch has its bit in the attention word: the slice end tests the word and nothing else, and finds the
// latch behind it (hw/sh4/SuperH4_impl.cpp)
void SetLatch(bool latched)
{
	g_stop_latched = latched;

	if (latched)
		g_attn.fetch_or(ATTN_LATCHED);
	else
		g_attn.fetch_and(~(u32)ATTN_LATCHED);
}

void Latch()
{
	SetLatch(true);
	g_parked = true;
	p_sh4rcb->cntx.CpuRunning = false;
}

// Run() came back with a SLEEP wait that the latch ended: where that SLEEP is. Asked before anything in the
// park context can write the pc
void NoteSleepCut()
{
	if (g_sleep_cut && !e.sleepNoted)
	{
		e.sleepNoted = true;
		e.sleepPc = p_sh4rcb->cntx.pc;
	}
}

// The interrupt acceptance that the latch held back, in the order of a slice end that no debugger stopped.
// Returns whether an interrupt was accepted
bool AcceptDeferred()
{
	bool accepted = false;

	NoteSleepCut();

	if (g_intc_deferred)
	{
		g_intc_deferred = false;

		// A SLEEP whose wait the latch ended has been executed: an interrupt that is accepted now returns behind
		// it. Not when the pc is no longer on that SLEEP
		if (g_sleep_cut && p_sh4rcb->cntx.pc == e.sleepPc)
		{
			p_sh4rcb->cntx.pc += 2;

			accepted = UpdateINTC() != 0;

			if (!accepted)
				p_sh4rcb->cntx.pc -= 2;
		}
		else
		{
			accepted = UpdateINTC() != 0;
		}
	}

	g_sleep_cut = false;
	e.sleepNoted = false;

	return accepted;
}

// The loop that Run() enters next, and what the notices say about it
void SelectLoop()
{
	bool fallbackLoop = UseFallbackLoop();

	if (fallbackLoop == engine.fallbackLoop)
		return;

	engine.fallbackLoop = fallbackLoop;

	if (fallbackLoop)
		DBG_EVENT(EV_DEBUGGER_ENGINE, "the SH4 runs on the interpreter fallback: backend %s has no native breakpoints", engine.backend);
	else
		DBG_EVENT(EV_DEBUGGER_ENGINE, "the SH4 runs on backend %s again", engine.backend);

	std::lock_guard<std::mutex> q(queue_lock);
	shared.state.notice = CurrentNotice();
}

// Whether the ARM7 runs on its interpreter fallback has changed: the notice of the published state follows at
// once, and the change is logged. On the emulator thread, or under L while no emulator thread exists; Q is not
// held. Where the backend of the ARM7 is its interpreter, which is every x86-64 build, nothing ever changes here
void SelectArm7()
{
	bool fallback = Arm7OnFallback();

	if (fallback == engine.arm7Fallback)
		return;

	engine.arm7Fallback = fallback;

	if (fallback)
		DBG_EVENT(EV_DEBUGGER_ENGINE, "the ARM7 runs on the interpreter fallback: its recompiler has no breakpoints");
	else
		DBG_EVENT(EV_DEBUGGER_ENGINE, "the ARM7 runs on its recompiler again");

	std::lock_guard<std::mutex> q(queue_lock);

	if (fallback)
		shared.state.notice |= (u32)NOTICE_ARM7_FALLBACK;
	else
		shared.state.notice &= ~(u32)NOTICE_ARM7_FALLBACK;
}

// The ARM7 is about to execute, or can, with the debug loop of the sound burst in place of its recompiler: a
// sound burst that the debugger suspended is run on (the rest of a suspended ARM phase is always the debug
// loop's), or the park context is left with dbg_arm7_armed not 0. The stop that follows says so
// (NOTICE_ARM7_FALLBACK), whatever is armed when it is published: a step and a temporary breakpoint are gone by
// then (arm7::OnStop). Asked before the burst is run on, and behind everything that arms for the leave
void NoteArm7Fallback()
{
	if (engine.arm7Recompiler && (dbg_arm7_armed != 0 || virtualDreamcast->SoundBurstSuspended() != SOUND_BURST_NONE))
		e.arm7Fallback = true;
}

// The backends of this thread are selected
void CaptureEngine()
{
	SuperH4_impl* cpu = static_cast<SuperH4_impl*>(sh4_cpu);
	SuperH4Backend* backend = cpu->DebugBackend();
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);

	engine.jit = cpu->backend_kind == SH4BE_DYNAREC;
	engine.backend = engine.jit ? JitName() : "interpreter";
	engine.fallback = engine.jit && (forceFallback || backend == nullptr || !(backend->DebugCaps() & SH4DBG_NATIVE));
	engine.arm7Recompiler = scpu != nullptr && scpu->DebugIsRecompiler();
	engine.known = true;

	// a breakpoint that was listed while no thread existed arms the debug loop from the first burst on
	SelectArm7();
}

/*
	The park context
*/

// What other threads asked for, and what the guest raised, becomes the stop that is decided
void FoldRequests()
{
	{
		std::lock_guard<std::mutex> q(queue_lock);

		if (shared.pauseRequested)
		{
			shared.pauseRequested = false;
			Merge(e.want, e.wantId, StopCause::Pause, 0);
		}

		shared.deferrable = false;
		g_attn.fetch_and(~(u32)ATTN_PAUSE);
	}

	if (e.freeze != StopCause::NoCause)
	{
		Merge(e.want, e.wantId, e.freeze, e.freezeId);
		e.freeze = StopCause::NoCause;
		e.freezeId = 0;
	}
}

// Enters the stop that is decided: publishes it, and hands it to a selftest session
void EnterStop()
{
	StopCause cause = e.want;
	u32 id = (cause == StopCause::Breakpoint || cause == StopCause::Event) ? e.wantId : 0;

	e.want = StopCause::NoCause;
	e.wantId = 0;

	g_parked = true;
	p_sh4rcb->cntx.CpuRunning = false;

	// what the breakpoint state reports is what its helpers gave to LatchStop(), whose record is published
	sh4::StopInfo info;
	sh4::OnStop(&info);

	// the temporary breakpoint and a step of the ARM7 end here, also when the stop is the SH4's
	arm7::OnStop();
	SelectArm7();

	// The notices of the stop: what holds now, and what held for the step, the resume or the run it ends. A
	// step and a temporary breakpoint of the ARM7 have just been disarmed: their half is the flag's
	u32 notice = CurrentNotice() | (e.stepFallback ? (u32)NOTICE_SH4_FALLBACK : 0) | (e.arm7Fallback ? (u32)NOTICE_ARM7_FALLBACK : 0);

	e.stepFallback = false;
	e.arm7Fallback = false;

	e.stopInForce = true;
	e.stopPc = p_sh4rcb->cntx.pc;
	e.stepped = false;
	e.armed = ACT_NONE;

	// the stop of the thread before is this stop now
	resumeOwed = false;

	RunState state = Publish(Lifetime::Parked, cause, id, notice, true);

	// The title says that the debugger paused, at every stop. It is put where it is not on the window already:
	// at the first stop of a thread, and when the running machine has taken the title back with its frame
	// rate since. A title call waits for the thread of the user interface on some hosts
	if (!e.titled || e.titledAt != last_fps)
	{
		e.titled = true;
		e.titledAt = last_fps;

		os_SetWindowText(kPausedTitle);
		test::pausedTitles++;
	}

	// A guest test stands where a call stands: it reads and writes directly and arms what happens next
	if (selftest::Active())
	{
		verify(e.depth == 0);
		e.depth++;
		selftest::OnStop(state);
		e.depth--;
	}
}

// The first step of every leave: a sound burst that the debugger suspended is run on before the SH4 executes
// anything, with the checks of the ARM7. No emulated time passes: it is the rest of the AICA tick that was due.
// Returns false when a check suspended the burst again: the stop is decided (emu::OnSoundStop has recorded it),
// nothing is left, and the caller drops what it was leaving for
bool RunBurstOn()
{
	if (virtualDreamcast->SoundBurstSuspended() == SOUND_BURST_NONE)
		return true;

	NoteArm7Fallback();

	return virtualDreamcast->RunSoundBurst(true);
}

// Leaves the park context for Run(). resume: the stop in force ends with it. arm7Decided: the resume exemption
// of the ARM7 has been armed by the caller, for a step-over or a step-out of the ARM7.
// Returns kParkStay, with nothing left and the thread parked, when a breakpoint of the ARM7 suspended the sound
// burst again that was run on: the park loop publishes that stop
u32 Leave(bool resume, bool arm7Decided = false)
{
	// Also for the stop that the thread before this one ended in, which nothing has resumed: the instruction
	// of that stop executes, and its breakpoint does not stop again at once
	const bool resumes = resume || resumeOwed;

	if (resumes && !arm7Decided)
		arm7::PrepareResume();

	if (!RunBurstOn())
		return kParkStay;

	SetLatch(false);

	AcceptDeferred();

	sh4_host_fp_resync();

	if (resumes)
		sh4::PrepareResume();

	SelectLoop();
	SelectArm7();
	NoteArm7Fallback();

	// not when the machine is asked to stop: dc_run finds the reason
	if (!virtualDreamcast->StartCpu(true))
		return PARK_EVALUATE;

	resumeOwed = false;
	e.inPark = false;

	if (e.stopInForce)
	{
		e.stopInForce = false;
		Publish(Lifetime::Running, StopCause::NoCause, 0, CurrentNotice(), false);
	}

	return PARK_RUN;
}

// Executes the step a call armed. Returns true with *rv when Park() returns; false when the stop of the
// step is decided and the park loop goes on with it
bool DoStep(Action action, u32* rv)
{
	// The step can let the ARM7 execute: its slice end can start a sound burst, and one that is suspended is
	// run on first. A breakpoint of the ARM7 that suspends it again is the stop, and the step is dropped
	arm7::PrepareResume();

	if (!RunBurstOn())
		return false;

	SelectArm7();
	NoteArm7Fallback();

	SetLatch(false);

	bool accepted = AcceptDeferred();

	sh4_host_fp_resync();

	if (accepted)
	{
		// complete at the vector: nothing executes
		sh4::PrepareStep();

		SetLatch(true);
		Merge(e.want, e.wantId, StopCause::Step, 0);
		return false;
	}

	bool resume = false;

	if (action == ACT_STEP_OVER)
	{
		resume = sh4::PrepareStepOver();
	}
	else if (action == ACT_STEP_OUT)
	{
		sh4::PrepareStepOut();
		resume = true;
	}

	if (resume)
	{
		sh4::PrepareResume();

		SelectLoop();

		if (!virtualDreamcast->StartCpu(true))
		{
			*rv = PARK_EVALUATE;
			return true;
		}

		e.inPark = false;
		e.stopInForce = false;
		Publish(Lifetime::Running, StopCause::NoCause, 0, CurrentNotice(), false);

		*rv = PARK_RUN;
		return true;
	}

	sh4::PrepareStep();

	// the debugger stop stays in force: the machine counts as running for the host all through the step
	if (!virtualDreamcast->StartCpu(false))
	{
		*rv = PARK_EVALUATE;
		return true;
	}

	e.inPark = false;
	e.inStep = true;
	e.stepped = true;

	if (engine.fallback)
		sh4_fallback_step();
	else
		sh4_cpu->DebugBackend()->Step();

	e.inStep = false;
	e.inPark = true;

	p_sh4rcb->cntx.CpuRunning = false;
	SetLatch(true);

	e.stepFallback = engine.fallback;
	Merge(e.want, e.wantId, StopCause::Step, 0);

	// the machine may have been asked to stop or to reset while the step ran
	*rv = PARK_EVALUATE;
	return true;
}

// Executes the step of the ARM7 a call armed. Returns true with *rv when Park() returns; false when the park
// loop goes on with a stop that is decided: the stop of the step, or that of a breakpoint of the ARM7 in the
// burst that a step-over or a step-out ran on
bool DoArm7Step(Action action, u32* rv)
{
	bool resume = false;

	if (action == ACT_ARM7_STEP_OVER)
	{
		resume = arm7::PrepareStepOver();
	}
	else if (action == ACT_ARM7_STEP_OUT)
	{
		arm7::PrepareStepOut();
		resume = true;
	}

	if (resume)
	{
		// a resume with one temporary breakpoint: it publishes Running when it leaves
		*rv = Leave(true, true);
		return *rv != kParkStay;
	}

	arm7::PrepareStep();

	if (virtualDreamcast->SoundBurstSuspended() != SOUND_BURST_NONE)
	{
		// In the park context: the burst is run on until the check behind the instruction suspends it again,
		// or the check in front of it does (a FIQ was entered there). No emulated time passes, the SH4 executes
		// nothing, the latch stays set, nothing is published in between
		NoteArm7Fallback();

		if (!virtualDreamcast->RunSoundBurst(true))
			return false;

		// the instruction was the last of its burst: the step is complete at the end of the burst
		if (arm7::CompleteStepAtBurstEnd())
			return false;

		// (a step that is armed is the first check of the burst that is run on, so its instruction has executed
		// when that burst is over. Were it not so, the instruction would be in the next burst, as below)
	}

	// With no burst suspended the instruction is in the next burst, which only the scheduler starts. The loop
	// leaves as for a resume and the stop stays in force: nothing is published, g_parked stays set, and the slice
	// hook treats the run of the SH4 as a step that is executing. The check of that burst, or the burst itself
	// when it finds the ARM7 held, records the stop of the step, the hook latches it, and the next Park()
	// publishes it
	SelectArm7();
	NoteArm7Fallback();

	SetLatch(false);

	AcceptDeferred();

	sh4_host_fp_resync();

	// for the SH4 this is a resume
	sh4::PrepareResume();

	SelectLoop();

	if (!virtualDreamcast->StartCpu(false))
	{
		*rv = PARK_EVALUATE;
		return true;
	}

	e.inPark = false;
	e.inStep = true;

	// The wait counts as a step that has executed: a thread that ends in it leaves no resume owed
	e.stepped = true;

	*rv = PARK_RUN;
	return true;
}

enum Taken {
	TAKEN_NONE,			// the queue is empty
	TAKEN_RAN,
};

// Takes the first call of the queue and runs it, in the park context
Taken RunOneCall()
{
	std::shared_ptr<CallObj> call;

	{
		std::lock_guard<std::mutex> q(queue_lock);

		for (;;)
		{
			if (shared.calls.empty())
			{
				QueueEmptied();
				return TAKEN_NONE;
			}

			call = shared.calls.front();
			shared.calls.pop_front();

			if (call->state == CallObj::Cancelled)
				continue;

			// run control is for a parked thread
			if (call->cls == CC_EXEC && !e.stopInForce)
			{
				Complete(call, kErrNotPaused);
				continue;
			}

			call->state = CallObj::Running;
			break;
		}
	}

	RunCall(call);
	return TAKEN_RAN;
}

}

/*
	Calls
*/

int Call(CallClass cls, std::function<void()> fn)
{
	if (OnEmuThread() || t_in_call)
		die("dbg::emu::Call on the emulator thread, or from inside a call");

	const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(kCallTimeoutMs);

	for (;;)
	{
		{
			std::unique_lock<std::mutex> q(queue_lock);

			if (ThreadExists(shared.state.lifetime))
			{
				// answered from the state: a running thread takes no run control
				if (cls == CC_EXEC && shared.state.lifetime == Lifetime::Running)
					return kErrNotPaused;

				std::shared_ptr<CallObj> call = std::make_shared<CallObj>();

				call->cls = cls;
				call->fn = std::move(fn);

				shared.calls.push_back(call);

				if (cls != CC_READ && !shared.deferrable)
				{
					shared.deferrable = true;
					shared.deferrableSince = Clock::now();
				}

				g_attn.fetch_or(ATTN_CALL);
				park_cv.notify_all();

				if (!call->done.wait_until(q, deadline, [&call] { return call->state != CallObj::Queued; }))
				{
					// given up: the emulator thread skips it, and the closure goes with the caller
					call->state = CallObj::Cancelled;

					std::function<void()> dropped = std::move(call->fn);
					call->fn = nullptr;

					q.unlock();
					return kErrBusy;
				}

				call->done.wait(q, [&call] { return call->state == CallObj::Done; });

				return call->result;
			}
		}

		// No emulator thread: the caller runs it, under L. The wait for L has the limit of every call: L is
		// tried until it is free
		std::unique_lock<std::recursive_mutex> l(lifetime_lock, std::defer_lock);

		while (!l.try_lock())
		{
			if (Clock::now() >= deadline)
				return kErrBusy;

			std::this_thread::sleep_for(std::chrono::microseconds(200));
		}

		Lifetime lifetime;

		{
			std::lock_guard<std::mutex> q(queue_lock);
			lifetime = shared.state.lifetime;
		}

		// a thread was started in between: it takes the call
		if (ThreadExists(lifetime))
			continue;

		if (lifetime == Lifetime::NoMachine)
		{
			if (cls != CC_TABLE)
				return kErrNoGame;
		}
		else if (cls == CC_WRITE || cls == CC_EXEC)
		{
			return kErrHostStopped;
		}

		t_in_call = true;
		fn();
		t_in_call = false;

		return 0;
	}
}

void SetListener(void (*fn)(const RunState& state))
{
	std::lock_guard<std::mutex> l(listener_lock);

	listener = fn;
}

/*
	Core -> emulator
*/

void RequestPause()
{
	std::lock_guard<std::mutex> q(queue_lock);

	if (shared.state.lifetime != Lifetime::Running)
		return;

	shared.pauseRequested = true;

	if (!shared.deferrable)
	{
		shared.deferrable = true;
		shared.deferrableSince = Clock::now();
	}

	g_attn.fetch_or(ATTN_PAUSE);
}

int Resume()
{
	if (!OnEmuThread() || !e.inPark || !e.stopInForce || e.armed != ACT_NONE)
		return kErrNotPaused;

	e.armed = ACT_RESUME;
	return 0;
}

int Step(Target t, StepKind k, const char** why)
{
	if (!OnEmuThread() || !e.inPark || !e.stopInForce || e.armed != ACT_NONE)
		return kErrNotPaused;

	// the DSP has no run control yet (the stubs of header revision 7, dbg_dsp.cpp): its steps are refused, also
	// where TargetSteps(Target::Dsp) is made true
	if (!TargetSteps(t) || t == Target::Dsp)
		return kErrNotSupported;

	if (t == Target::Arm7)
	{
		int rv = arm7::StepCheck();

		if (rv != 0)
			return rv;

		e.armed = k == StepKind::Into ? ACT_ARM7_STEP : k == StepKind::Over ? ACT_ARM7_STEP_OVER : ACT_ARM7_STEP_OUT;
		return 0;
	}

	int rv = sh4::StepCheck(why);

	if (rv != 0)
		return rv;

	// a backend that neither steps nor has the fallback behind it
	if (!engine.fallback)
	{
		SuperH4Backend* backend = sh4_cpu->DebugBackend();

		if (backend == nullptr || !(backend->DebugCaps() & SH4DBG_NATIVE))
			return kErrNotSupported;
	}

	e.armed = k == StepKind::Into ? ACT_STEP : k == StepKind::Over ? ACT_STEP_OVER : ACT_STEP_OUT;
	return 0;
}

bool TargetSteps(Target t)
{
	return t == Target::Sh4 || t == Target::Arm7;
}

int Reset()
{
	if (!OnEmuThread() || !e.inPark)
		return kErrHostStopped;

	if (e.stopInForce)
	{
		// at once: the park loop publishes the stop of the reset when the call has returned
		virtualDreamcast->Reset();
	}
	else
	{
		// a service excursion: the machine resets as for a reset of the guest, and runs on
		virtualDreamcast->RequestReset();
		e.machineAsked = true;
	}

	return 0;
}

void SetSh4Breakpoints(const u32* ids, const u32* addrs, u32 n)
{
	sh4::SetBreakpoints(ids, addrs, n);
}

void SetArm7Breakpoints(const u32* ids, const u32* addrs, u32 n)
{
	arm7::SetBreakpoints(ids, addrs, n);

	SelectArm7();
}

void SetDspBreakpoints(const u32* ids, const u32* steps, u32 n)
{
	dsp::SetBreakpoints(ids, steps, n);
}

void SetEventBreakpoints(const u32* ids, const u8* events, u32 n)
{
	memset(eventBp, 0, sizeof(eventBp));

	for (u32 i = 0; i < n; i++)
	{
		if (events[i] >= EV_ID_COUNT || ids[i] == 0)
			continue;

		u32& slot = eventBp[events[i]];

		if (slot == 0 || ids[i] < slot)
			slot = ids[i];
	}
}

void SetActiveMask(u32 categories)
{
	activeMask = categories;

	sh4::SetActiveMask(categories);
	arm7::SetActiveMask(categories);
	dsp::SetActiveMask(categories);

	SelectArm7();
}

RunState GetRunState()
{
	std::lock_guard<std::mutex> q(queue_lock);

	return shared.state;
}

bool OnEmuThread()
{
	return t_is_emu;
}

/*
	Breakpoint state and events -> run control
*/

void LatchStop(StopCause cause, u32 breakpointId)
{
	Merge(e.want, e.wantId, cause, breakpointId);

	Latch();
}

void OnSoundStop(StopCause cause, u32 breakpointId)
{
	// The park loop runs a suspended burst on: this is the stop it publishes next
	if (e.inPark)
	{
		Merge(e.want, e.wantId, cause, breakpointId);
		return;
	}

	// The burst runs in its scheduler callback. The run flag is not looked at: the burst is suspended whatever
	// the machine is about to do, and the hook latches a freezing cause also for a machine that leaves Run()
	Merge(e.freeze, e.freezeId, cause, breakpointId);

	g_attn.fetch_or(ATTN_PAUSE);
}

void OnEvent(EventId id)
{
	// only while the guest executes
	if (e.inPark || !p_sh4rcb->cntx.CpuRunning)
		return;

	u32 bp = eventBp[id];

	if (bp == 0 || !(activeMask & CategoryBit(Category::Events)))
		return;

	Merge(e.freeze, e.freezeId, StopCause::Event, bp);

	g_attn.fetch_or(ATTN_PAUSE);
}

bool GetSh4Engine(Sh4EngineInfo* out)
{
	if (!engine.known)
		return false;

	SuperH4Backend* backend = sh4_cpu->DebugBackend();

	out->backend = engine.backend;
	out->caps = backend ? backend->DebugCaps() : 0;
	out->fallback = engine.fallback;
	out->fallbackLoop = engine.fallbackLoop;
	out->sliceCyclesLeft = backend ? backend->SliceCyclesLeft() : 0;

	return true;
}

/*
	The slice hook
*/

void SliceHook()
{
	// A step: calls wait for the park loop. Only what freezes the scheduler ends the step here
	if (e.inStep)
	{
		if (e.freeze != StopCause::NoCause)
		{
			LatchStop(e.freeze, e.freezeId);
			e.freeze = StopCause::NoCause;
			e.freezeId = 0;
		}
		return;
	}

	bool service = false;
	bool pause = false;
	bool overdue = false;

	// The reads run here; a call of another class needs the park context
	for (;;)
	{
		std::shared_ptr<CallObj> call;

		{
			std::lock_guard<std::mutex> q(queue_lock);

			service = false;

			for (auto it = shared.calls.begin(); it != shared.calls.end(); )
			{
				CallObj& c = **it;

				if (c.state == CallObj::Cancelled)
				{
					it = shared.calls.erase(it);
				}
				else if (c.cls == CC_READ)
				{
					call = *it;
					call->state = CallObj::Running;
					shared.calls.erase(it);
					break;
				}
				else if (c.cls == CC_EXEC)
				{
					// queued for a parked thread, taken by a running one
					Complete(*it, kErrNotPaused);
					it = shared.calls.erase(it);
				}
				else
				{
					service = true;
					++it;
				}
			}

			if (!call)
			{
				if (shared.calls.empty())
					QueueEmptied();

				pause = shared.pauseRequested;

				if (!service && !pause)
					shared.deferrable = false;
				else if (g_in_sleep && shared.deferrable)
					overdue = Clock::now() - shared.deferrableSince >= std::chrono::milliseconds(kPauseLivenessMs);

				break;
			}
		}

		RunCall(call);
	}

	if (e.freeze != StopCause::NoCause)
	{
		// the scheduler stops in this very call
		LatchStop(e.freeze, e.freezeId);
		e.freeze = StopCause::NoCause;
		e.freezeId = 0;
		return;
	}

	if (!service && !pause)
		return;

	// A pause and a service excursion are taken where leaving and entering Run() again is exact: at a slice
	// end of the backend loop. They wait while the cpu is in a SLEEP wait, and when the machine itself is
	// about to leave Run(): the park context that follows takes them
	if (!p_sh4rcb->cntx.CpuRunning)
		return;

	if (g_in_sleep && !overdue)
		return;

	if (pause)
		LatchStop(StopCause::Pause, 0);
	else
		Latch();
}

bool UseFallbackLoop()
{
	return engine.known && engine.fallback && sh4::FallbackWanted();
}

void Kick()
{
	std::lock_guard<std::mutex> q(queue_lock);

	if (!ThreadExists(shared.state.lifetime))
		return;

	shared.kicked = true;
	park_cv.notify_all();
}

/*
	Lifetime
*/

LifetimeLock::LifetimeLock()
{
	lifetime_lock.lock();
}

LifetimeLock::~LifetimeLock()
{
	lifetime_lock.unlock();
}

void OnMachineCreated()
{
	engine = { };

	machineFirstPark = true;
	resumeOwed = false;
	startPaused = cfgLoadBool("Debugger", "StartPaused", false);
	forceFallback = cfgLoadBool("Debugger", "ForceFallback", false);

	Publish(Lifetime::Stopped, StopCause::HostStopped, 0, 0, true);
}

void OnMachineDestroying()
{
	{
		std::lock_guard<std::mutex> q(queue_lock);

		// a machine that was never created whole
		if (shared.state.lifetime == Lifetime::NoMachine)
			return;
	}

	symbols::Clear();

	engine = { };

	Publish(Lifetime::NoMachine, StopCause::NoGame, 0, 0, true);
}

void OnThreadStarting()
{
	if (test::threadStarting)
		test::threadStarting();

	{
		std::lock_guard<std::mutex> q(queue_lock);

		shared.kicked = false;
		shared.pauseRequested = false;
		shared.deferrable = false;
	}

	Publish(Lifetime::Running, StopCause::NoCause, 0, CurrentNotice(), false);
}

void OnThreadEntered()
{
	t_is_emu = true;

	e = { };
	e.firstPark = true;

	SetLatch(false);
	g_intc_deferred = false;
	g_in_sleep = false;
	g_sleep_cut = false;
}

void OnThreadExiting(bool guestExit)
{
	std::lock_guard<std::recursive_mutex> l(lifetime_lock);

	// The park context is left for good: what the latch held back is made, as at the slice end of a machine
	// that no debugger stopped; what belonged to a stop goes
	SetLatch(false);
	AcceptDeferred();

	sh4::OnStop(nullptr);
	arm7::OnStop();
	SelectArm7();

	// A thread that ends in a debugger stop leaves the resume of that stop to the next one, when the guest is
	// where the stop was published: no step has executed behind it and nothing has written the pc
	if (e.stopInForce && !e.stepped && p_sh4rcb->cntx.pc == e.stopPc)
		resumeOwed = true;

	e.inPark = true;
	e.stopInForce = false;

	// The queue: what only looks runs, what would change the guest is answered. The state becomes Stopped
	// with the queue empty, under Q: from then on a caller runs its call itself, and waits for L to do so
	RunState stopped;

	for (;;)
	{
		std::shared_ptr<CallObj> call;

		{
			std::lock_guard<std::mutex> q(queue_lock);

			while (!shared.calls.empty())
			{
				std::shared_ptr<CallObj> front = shared.calls.front();
				shared.calls.pop_front();

				if (front->state == CallObj::Cancelled)
					continue;

				if (front->cls == CC_WRITE || front->cls == CC_EXEC)
				{
					Complete(front, kErrHostStopped);
					continue;
				}

				front->state = CallObj::Running;
				call = front;
				break;
			}

			if (!call)
			{
				shared.pauseRequested = false;
				shared.deferrable = false;
				shared.kicked = false;
				g_attn.store(0);

				// whoever sees the new state sees a machine that does not count as running
				g_parked = false;
				g_intc_deferred = false;
				g_sleep_cut = false;

				stopped = SetState(Lifetime::Stopped, guestExit ? StopCause::GuestExit : StopCause::HostStopped, 0, 0, true);
				break;
			}
		}

		RunCall(call);
	}

	Announce(stopped);

	// No call of the window from here: this is behind the stop callback, and the thread of the user interface
	// may have ended its loop in it and be waiting for this thread. The title of the park loop stays until a
	// running machine writes its frame rate
	e = { };
	t_is_emu = false;
}

void OnGuestStateReplaced()
{
	// what belonged to the state that is gone
	g_intc_deferred = false;
	g_sleep_cut = false;
	resumeOwed = false;

	sh4::OnPcWritten();
	arm7::OnPcWritten();

	if (OnEmuThread())
	{
		// temporary breakpoints, a step-out and a step of the ARM7 end with the state they were made for.
		// Without an emulator thread there are none: the thread ended them on its way out
		sh4::OnStop(nullptr);
		arm7::OnStop();
		SelectArm7();

		// the park loop publishes the stop, if one is in force
		e.resetSeen = true;
		return;
	}

	RunState state = GetRunState();

	// clients fetch again
	if (state.lifetime == Lifetime::Stopped)
		Publish(Lifetime::Stopped, state.cause, 0, 0, true);
}

/*
	The park loop
*/

u32 Park()
{
	e.inPark = true;

	// The run of the SH4 towards the sound burst that a step of the ARM7 waited for is over, whatever ended it
	e.inStep = false;

	// In the park context nothing is accepted and no time passes, whatever a call does: an interrupt that a
	// write lets in is accepted on the way out
	SetLatch(true);

	NoteSleepCut();

	if (e.firstPark)
	{
		e.firstPark = false;

		CaptureEngine();

		// ends the process when the session has no guest test
		selftest::AtStart();

		bool start = selftest::Active() && !test::plainStart;

		if (machineFirstPark)
		{
			machineFirstPark = false;
			start = start || startPaused;
		}

		if (start)
			Merge(e.want, e.wantId, StopCause::Start, 0);
	}

	for (;;)
	{
		FoldRequests();

		if (e.resetSeen)
		{
			e.resetSeen = false;

			// a reset under a debugger stop, or with one wanted: the stop is the reset's
			if (e.stopInForce || e.want != StopCause::NoCause)
			{
				e.want = StopCause::Reset;
				e.wantId = 0;
			}
		}

		if (e.want != StopCause::NoCause)
			EnterStop();

		if (e.resetSeen)
			continue;

		if (e.stopInForce && e.armed != ACT_NONE)
		{
			Action action = e.armed;
			e.armed = ACT_NONE;

			u32 rv;

			if (action == ACT_RESUME)
			{
				rv = Leave(true);

				// a breakpoint of the ARM7 stopped the burst that was run on: that stop is published
				if (rv == kParkStay)
					continue;

				return rv;
			}

			if (action >= ACT_ARM7_STEP ? DoArm7Step(action, &rv) : DoStep(action, &rv))
				return rv;

			continue;
		}

		if (RunOneCall() == TAKEN_RAN)
		{
			if (e.machineAsked)
			{
				e.machineAsked = false;
				return PARK_EVALUATE;
			}

			continue;
		}

		if (!e.stopInForce)
		{
			std::unique_lock<std::mutex> q(queue_lock);

			// asked for while the excursion ran: this is a slice end, it is taken here
			if (shared.pauseRequested || !shared.calls.empty())
				continue;

			q.unlock();

			u32 rv = Leave(false);

			if (rv == kParkStay)
				continue;

			return rv;
		}

		std::unique_lock<std::mutex> q(queue_lock);

		park_cv.wait(q, [] { return shared.kicked || !shared.calls.empty(); });

		if (shared.kicked)
		{
			shared.kicked = false;
			return PARK_EVALUATE;
		}
	}
}

}

}
