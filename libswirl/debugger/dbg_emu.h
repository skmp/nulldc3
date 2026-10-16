/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include <functional>
#include "debugger.h"

#if !defined(NULLDC_DEBUGGER)
#error dbg_emu.h is for debugger sources: NULLDC_DEBUGGER is not defined
#endif

/*
	dbg::emu: the debugger on the emulator thread (lifetime, the call queue, the slice hook, the stop latch,
	the park loop and the sequencing of run control: dbg_emu.cpp), and the types every debugger module
	shares. Debugger sources include this header; emulator code includes debugger.h, which has the threads,
	the lifetime states and the contexts this header refers to.

	This header and those that include it (dbg_sh4.h, dbg_arm7.h, dbg_core.h, dbg_mem.h, dbg_callstack.h,
	dbg_selftest.h) exist only in builds with NULLDC_DEBUGGER: debugger.h has stand-ins for a build without it,
	these have none.
*/

namespace dbg {

/*
	Shared types and wire constants.
*/

// The processors of the wire protocol ("target").
enum class Target : u8 {
	Sh4  = 0,		// "sh4"
	Arm7 = 1,		// "arm7"
	Dsp  = 2,		// "dsp"
};
const u32 kTargetCount = 3;

inline const char* TargetName(Target t) {
	switch (t) {
	case Target::Sh4:  return "sh4";
	case Target::Arm7: return "arm7";
	case Target::Dsp:  return "dsp";
	}
	return "";
}

// The "event" of a code breakpoint, one name per target. These names are not in the event table
// (dbg_events.h): an event breakpoint cannot be set on them.
const char* const kEventSh4Pc   = "dc.sh4.cpu.pc";
const char* const kEventArm7Pc  = "dc.aica.arm7.pc";
const char* const kEventDspStep = "dc.aica.dsp.step";

// Breakpoint categories: the keys of "categories" in a tick. A breakpoint with kind "event" is in Events;
// a code breakpoint is in the category of its target.
enum class Category : u8 {
	Events = 0,		// "events"
	Sh4    = 1,		// "sh4"
	Arm7   = 2,		// "arm7"
	Dsp    = 3,		// "dsp"
};
const u32 kCategoryCount = 4;

inline const char* CategoryName(Category c) {
	switch (c) {
	case Category::Events: return "events";
	case Category::Sh4:    return "sh4";
	case Category::Arm7:   return "arm7";
	case Category::Dsp:    return "dsp";
	}
	return "";
}

// The bit of a category in an active mask (emu::SetActiveMask).
inline u32 CategoryBit(Category c) {
	return 1u << (u32)c;
}

enum class StepKind : u8 {
	Into,		// control.step: one instruction, a delayed branch together with its slot
	Over,		// control.stepOver
	Out,		// control.stepOut
};

// See debugger.h for what the states mean.
enum class Lifetime : u8 {
	NoMachine,
	Stopped,
	Running,
	Parked,
};

// Why the state is "paused": the wire value of executionState.reason. When several causes meet in one stop
// the one published is the first of Reset, Start, Breakpoint, Event, Step, Pause.
enum class StopCause : u8 {
	NoCause,		// not paused: lifetime Running. Nothing on the wire
	Pause,			// "pause"         control.pause honoured
	Step,			// "step"          a step completed: a single step, the stop of a temporary breakpoint, the
					//                 return a step-out waited for
	Breakpoint,		// "breakpoint"    a code breakpoint hit; breakpointId is set
	Event,			// "event"         an event breakpoint hit; breakpointId is set
	Reset,			// "reset"         a reset made while a debugger stop was in force or a pause was wanted
	Start,			// "start"         the start park: Debugger:StartPaused, or a thread start of a selftest session
	Capture,		// "capture"       an SGC capture started while paused finished and paused again
	HostStopped,	// "host-stopped"  lifetime Stopped: the host stopped the machine, or the machine was created
					//                 and no thread has started yet
	GuestExit,		// "guest-exit"    lifetime Stopped: the guest ended the run
	NoGame,			// "no-game"       lifetime NoMachine
};

inline const char* StopCauseName(StopCause c) {
	switch (c) {
	case StopCause::NoCause:     return "";
	case StopCause::Pause:       return "pause";
	case StopCause::Step:        return "step";
	case StopCause::Breakpoint:  return "breakpoint";
	case StopCause::Event:       return "event";
	case StopCause::Reset:       return "reset";
	case StopCause::Start:       return "start";
	case StopCause::Capture:     return "capture";
	case StopCause::HostStopped: return "host-stopped";
	case StopCause::GuestExit:   return "guest-exit";
	case StopCause::NoGame:      return "no-game";
	}
	return "";
}

// Statements of executionState.notice, one line each.
enum : u32 {
	NOTICE_SH4_FALLBACK        = 1,		// the SH4 runs on the interpreter while breakpoints are armed: FPU results
										// can differ from a run without breakpoints. Set while the fallback loop is
										// selected (UseFallbackLoop()) and in the stop of a fallback step
	NOTICE_SH4_FALLBACK_TIMING = 2,		// with NOTICE_SH4_FALLBACK: that loop does not charge time by the decoder's
										// blocks, so timing differs as well. Reserved: never set while
										// sh4_fallback_loop() charges by the decoder's blocks, as hw/sh4/sh4_dbg.h
										// requires
	NOTICE_ARM7_FALLBACK       = 4,		// the ARM7 runs on the interpreter fallback: timing differs. Set while the
										// backend that executes the ARM7 is a recompiler
										// (SoundCPU::DebugIsRecompiler) and dbg_arm7_armed is not 0: the debug loop
										// then replaces the recompiler. Also set in the stop that ends a step, a
										// resume or a run in which the debug loop executed in place of the
										// recompiler, whatever is armed at that stop (a step and a temporary
										// breakpoint end before the stop is published). Never set where that
										// backend is the interpreter, which is every x86-64 build
	NOTICE_DSP_FORCED          = 8,		// the per-sample DSP path is forced on by the debugger. Transitional: it can
										// be set only while the AICA has a path without the DSP, and goes with it
};
const u32 kNoticeLineMax = 200;			// characters of one notice line

// Error codes of the wire protocol. Debugger functions that return int return 0 or one of these.
enum ErrorCode : int {
	kErrParse          = -32700,		// "parse error"
	kErrInvalidRequest = -32600,		// "invalid request"
	kErrMethodNotFound = -32601,		// "method not found: <name>"
	kErrInvalidParams  = -32602,		// "invalid params: <what>"
	kErrInternal       = -32603,		// "internal error"
	kErrBusy           = -32000,		// "emulator busy"
	kErrHostStopped    = -32001,		// "emulator stopped by host UI"
	kErrNoGame         = -32002,		// "no game loaded"
	kErrNotPaused      = -32003,		// "not paused"
	kErrNotSupported   = -32004,		// "not supported: <what>"
	kErrCannotStep     = -32005,		// "cannot step: <why>"
	kErrNotReadable    = -32010,		// "address not readable"
	kErrNoCapture      = -32011,		// "no capture available"
	kErrNotClocked     = -32012,		// "arm7 held in reset" / "aica not clocked (arm7 in reset)"
	kErrNotWritable    = -32013,		// "not writable: <what>"
	kErrCaptureTimeout = -32014,		// "capture timed out"
};

// A binary WebSocket frame starts with a header of kBinaryHeaderBytes: the magic "NDB3" (4E 44 42 33), then
// kind, binaryId and payloadBytes, each a little-endian u32. The JSON result the frame belongs to carries the
// same binaryId, and totalBytes equal to payloadBytes.
enum BinaryKind : u32 {
	kBinarySgc    = 1,		// SGC frames
	kBinaryMemory = 2,		// memory
	kBinaryCore   = 3,		// CORE capture
	kBinaryTa     = 4,		// TA capture
};
const u32 kBinaryHeaderBytes = 16;

// Bounds of request parameters; a request outside them is answered with kErrInvalidParams.
const u32 kMaxMemorySliceBytes  = 65536;		// state.getMemorySlice.length, 1..
const u32 kMaxMemoryBinaryBytes = 16777216;		// state.getMemoryBinary.length, 1..
const u32 kMaxDisassemblyLines  = 1024;			// state.getDisassembly.count, 1..
const u32 kMaxCallstackFrames   = 64;			// state.getCallstack.maxFrames, 1..
const u32 kDefaultCallstackFrames = 32;			// state.getCallstack.maxFrames when absent; frames per target in a tick
const u32 kMaxSgcFrames         = 4096;			// state.recordSgcFrames.frames, 1..
const u32 kDefaultSgcFrames     = 1024;
const u32 kMaxWriteMemoryBytes  = 65536;		// state.writeMemory.data.length, 1..
const u32 kMaxWatchExpressionChars = 256;
const u32 kMaxWatchExpressionsPerCall = 64;
const u32 kMaxWatches           = 256;
const u32 kMaxBreakpoints       = 1024;
const u32 kMaxMessageBytes      = 1u << 20;		// inbound message; the transport closes the connection above it

// Times, in milliseconds of host time.
const u32 kCallTimeoutMs        = 2000;			// emu::Call waits this long for a call that has not started
const u32 kPauseLivenessMs      = 250;			// a pause request or queued call met inside a SLEEP wait is latched
												// after this long
const u32 kClientGraceMs        = 10000;		// after the last client left, breakpoints stay active this long.
												// When it expires with no client connected, the core (dbg_core, on (C))
												// clears the active mask (a CC_TABLE call with SetActiveMask(0)) and
												// releases a parked thread (a CC_EXEC call with Resume()). dbg::emu has no
												// timer for this. The time starts at a disconnect only, so a start park
												// waits for its first client without limit
const u32 kCaptureTimeoutMs     = 5000;			// state.recordSgcFrames

namespace emu {

struct RunState {
	Lifetime lifetime;
	u32 stopId;				// counts stops: advances on every transition into a state reported as paused (a park
							// for any cause, Stopped, NoMachine), on every completed step, on a stop that the ARM7
							// check makes while the park loop runs a suspended sound burst on (no park is entered
							// for it), on a reset made while paused and on a reset or state load while Stopped. A
							// service excursion leaves it alone
	StopCause cause;		// NoCause exactly when lifetime is Running
	u32 breakpointId;		// for Breakpoint and Event: the id of the breakpoint, the lowest when several match;
							// otherwise 0 (ids start at 1)
	u32 notice;				// NOTICE_* bits that apply now
};

/*
	Calls: work handed to the emulator thread.
*/
enum CallClass {
	CC_READ,		// reads guest state and debugger tables
	CC_TABLE,		// changes debugger tables (breakpoints, watches, categories); may need code-cache maintenance
	CC_WRITE,		// writes guest state: memory, registers, reset
	CC_EXEC,		// run control: resume, step
};

// Runs fn where the one rule allows it and waits for it.
//
// Caller: any thread except (E). Not from inside a call, the listener or a selftest: those are on (E) or
// hold L and do the work directly. The caller holds neither L nor a lock that fn or the listener takes.
// Calling it on (E) is a programming error and ends the process.
//
// Where fn runs, by the lifetime at the moment the call is taken:
//
//              NoMachine          Stopped            Running                         Parked
//   CC_READ    not run, -32002    caller, under L    (E), slice hook                 (E), park loop
//   CC_TABLE   caller, under L    caller, under L    (E), park context (excursion)   (E), park loop
//   CC_WRITE   not run, -32002    not run, -32001    (E), park context (excursion)   (E), park loop
//   CC_EXEC    not run, -32002    not run, -32001    not run, -32003                 (E), park loop
//
// "caller, under L": the calling thread takes L, sees that no emulator thread exists and runs fn itself.
// A call that waits in the queue when the emulator thread exits is taken by that thread, under L, with the
// outcome of the Stopped column. A call queued for a parked thread that is taken after the thread resumed
// follows the Running column.
// CC_READ does not run in NoMachine. A caller that needs only the debugger's tables there (the tick that
// follows debugger.describe) makes a CC_TABLE call, which the caller runs itself under L.
//
// Returns 0 when fn ran; fn has returned.
// Returns kErrNoGame (-32002), kErrHostStopped (-32001) or kErrNotPaused (-32003) per the table; fn did not
// run. (control.runUntil while running answers success: its caller maps kErrNotPaused.)
// Returns kErrBusy (-32000) when (E) did not take the call within kCallTimeoutMs: the call is cancelled, fn
// will not run. A call that (E) has started is waited for without limit. Where the caller runs fn itself,
// the wait for L has the same limit and the same result.
//
// fn
// - captures by value or by shared_ptr only, never a reference or pointer into the caller's stack. A call
//   that is given up has its closure destroyed by the caller at that moment. Its results live in a captured
//   object.
// - runs alone: (E) takes one call at a time and never inside another call. It must not call Call.
// - must not let an exception escape, and must not wait for another thread: every other call waits behind it.
// - does what its class says and no more. CC_READ: read guest state (dbg_regs, dbg_mem, dbg_callstack,
//   dbg_symbols), read the tables, build a tick and hand it to dbg::net. In the slice hook the SH4 context
//   is complete only with SH4DBG_CTX_AT_SLICE (GetSh4Engine). CC_TABLE: also SetSh4Breakpoints,
//   SetArm7Breakpoints, SetEventBreakpoints, SetActiveMask. CC_WRITE: also dbg::mem::Write,
//   dbg::regs::Write, Reset. CC_EXEC: also Resume, Step.
int Call(CallClass cls, std::function<void()> fn);

/*
	Emulator -> core.
*/

// Installs the run-state listener; null removes it. One listener exists (the core's). (U), at dbg::Init and
// dbg::Term. It returns when no invocation of the previous listener is in progress, so the caller must not
// hold a lock that the listener takes.
//
// The listener is invoked with the state just published
//   on (E)  for every stop (park for any cause, completed step, reset while paused, a stop that the ARM7
//           check makes while the park loop runs a suspended sound burst on) and every resume;
//   on the thread that made a lifetime change, with L held, for Stopped, NoMachine, and Running at a
//           thread start.
// A service excursion publishes nothing. At every invocation guest state is quiescent and may be read
// directly unless the lifetime is NoMachine. The listener builds the tick and hands it to dbg::net, whose
// sends never block. It must not call Call or SetListener, must not wait for another thread, and must not
// let an exception escape.
void SetListener(void (*listener)(const RunState& state));

/*
	Core -> emulator.
*/

// Any thread, never blocks. Asks for a pause: while the lifetime is Running it records a pause request and
// sets ATTN_PAUSE; the thread parks at the next slice end of the backend loop (not inside a SLEEP wait,
// unless the request has been pending for kPauseLivenessMs) and publishes StopCause::Pause. In every other
// state it does nothing. A request that is still pending when the emulator thread exits is dropped.
void RequestPause();

// Inside a CC_EXEC call. Arms a resume: after the call returns, the park loop leaves the park context and
// publishes Running. With a sound burst suspended it runs the burst on first (debugger.h, Park()): if a
// breakpoint of the ARM7 suspends it again, the thread stays parked, that stop is published and Running is
// not. Returns 0; kErrNotPaused, with nothing armed, if the thread is not parked or an action is already
// armed.
int Resume();

// Inside a CC_EXEC call. Validates a step and arms it; the park loop acts on it after the call returns, and
// a new stop is published when it is complete (StopCause::Step unless a breakpoint or an event ended it).
//
// Target::Sh4. The park loop executes the step. With a sound burst suspended it runs the burst on first: a
// breakpoint of the ARM7 that suspends it again is published as the stop, and the step is dropped. A step
// over or out that turns into a resume publishes Running first. A step of any kind is complete, without
// executing anything, when the interrupt acceptance that the latch deferred accepts an interrupt on the way
// out of the park context: the stop is at the vector (dbg::sh4::PrepareStep).
//
// Target::Arm7. Into: one instruction of the ARM7 (dbg::arm7::PrepareStep). With a burst suspended the park
// loop runs it on in the park context until the check behind the instruction stops it: nothing is published
// in between, no emulated time passes, the SH4 executes nothing; when the instruction was the last of its
// burst, the step is complete at the end of the burst, with none suspended. With no burst suspended the park
// loop leaves without publishing (debugger.h, Park()): the SH4 runs to the next burst, one period of the
// AICA callback at most, and the step is the first instruction of that burst. The step is complete without
// executing anything when a FIQ is entered in front of its instruction (the stop is at the vector) and when
// that burst finds the ARM7 held in reset. A stop of the SH4 that comes first is published with its own
// cause. Over: on a BL the temporary breakpoint behind it and a resume, else as Into. Out: the temporary
// breakpoint at R14 and a resume (dbg::arm7::PrepareStepOver, PrepareStepOut). That resume is Resume()'s: it
// publishes Running when it leaves. Like Into, both execute the instruction they stand on, also with a
// listed breakpoint on it and whatever the stop before them was.
//
// Returns 0 when armed. Otherwise nothing is armed and it returns
//   kErrNotPaused     the thread is not parked, or an action is already armed
//   kErrNotSupported  the target cannot be stepped: TargetSteps(t) is false; Over and Out on Dsp always
//   kErrCannotStep    a precondition of an SH4 step failed (dbg::sh4::StepCheck); *why, if why is not null,
//                     receives the reason as a constant string
//   kErrNotClocked    Arm7 while the ARM7 is held in reset (dbg::arm7::StepCheck); Dsp likewise, from M3
int Step(Target t, StepKind k, const char** why = nullptr);

// Any thread, any lifetime state, never blocks. Whether run control and code breakpoints of the target exist
// in this build: what debugger.describe gives as targets[].stepping, and what the core asks before it accepts
// a code breakpoint or forwards a step. Constant for the process. The SH4: true. The ARM7: true once its run
// control exists (package E6b of the M2 round; false before it). The DSP: false until M3.
bool TargetSteps(Target t);

// Inside a CC_WRITE call. Parked: resets the machine through Dreamcast_impl::Reset at once, which discards a
// suspended sound burst; the thread stays parked and publishes a new stop with StopCause::Reset. Running (the
// call runs in a service excursion): asks the machine for a guest reset; it resets and keeps running, nothing
// is published.
// Returns 0. Called anywhere but on (E) in the park context it changes nothing and returns kErrHostStopped;
// a CC_WRITE call is never made there.
int Reset();

// Inside a CC_TABLE call. The enabled SH4 code breakpoints, all of them: ids[i] stops at addrs[i]
// (addresses as the client gave them; the address rule is applied by dbg::sh4). n <= kMaxBreakpoints; the
// arrays are read during the call only. Forwarded to dbg::sh4::SetBreakpoints. If the change makes
// UseFallbackLoop() change while running, the excursion ends by entering Run() on the other loop.
void SetSh4Breakpoints(const u32* ids, const u32* addrs, u32 n);

// Inside a CC_TABLE call. The enabled ARM7 code breakpoints, all of them: ids[i] stops at addrs[i] (addresses
// as the client gave them, multiples of 4 up to 0x00FFFFFF; the address rule is applied by dbg::arm7 when a
// check is made). n <= kMaxBreakpoints; the arrays are read during the call only. Forwarded to
// dbg::arm7::SetBreakpoints. It never causes code-cache work and never changes the loop of the SH4. It can
// change dbg_arm7_armed (debugger.h), and with it NOTICE_ARM7_FALLBACK, which GetRunState() then has at once.
// Like the other setters it also runs under L while no emulator thread exists, with or without a machine.
void SetArm7Breakpoints(const u32* ids, const u32* addrs, u32 n);

// Inside a CC_TABLE call. The enabled event breakpoints, all of them: ids[i] is on the event events[i]
// (an EventId). Same array rules.
void SetEventBreakpoints(const u32* ids, const u8* events, u32 n);

// Inside a CC_TABLE call. CategoryBit() of every category whose breakpoints can stop now: not muted, soloed
// or no category soloed, and a client connected or within kClientGraceMs of the last one. Read at every
// arrival at a breakpoint; never causes code-cache work. Forwarded to dbg::sh4::SetActiveMask and
// dbg::arm7::SetActiveMask; it can make UseFallbackLoop() change as SetSh4Breakpoints can, and
// dbg_arm7_armed as SetArm7Breakpoints can.
// Before the first call the mask is 0: no listed breakpoint and no event breakpoint stops.
void SetActiveMask(u32 categories);

// Any thread, never blocks. The state last published, as one consistent copy. notice is current even when
// nothing was published since: dbg::emu updates it when a service excursion switches loops and when a table
// call changes whether the ARM7 runs on its fallback.
RunState GetRunState();

// Any thread, never blocks. True on the emulator thread, from OnThreadEntered() until OnThreadExiting()
// has published Stopped.
bool OnEmuThread();

/*
	Breakpoint state and events -> run control.
*/

// (E) only, while the guest executes (in a backend loop or a step): from the SH4 check and flow helpers
// (dbg_sh4.cpp) and from the slice hook. Decides a debugger stop: records the cause, sets g_stop_latched
// and g_parked and clears Sh4cntx.CpuRunning. Never blocks. The sound side does not call it: a breakpoint
// or a step of the ARM7 is recorded through OnSoundStop, below, and latched by the slice hook.
// It does not leave the backend loop itself. The caller returns into the loop in the way that ends it at
// once: a helper returns non-zero to the backend; the slice hook returns into UpdateSystem(), whose result
// ends the loop.
//   Breakpoint, id   a listed code breakpoint stopped
//   Event, id        an event breakpoint stopped
//   Step, 0          a temporary breakpoint stopped, or the return a step-out waited for was executed
//   Pause, 0         a pause request
// When it is called more than once before the park context is entered, the published cause follows the
// priority of StopCause, and among breakpoints of the winning cause the lowest id.
void LatchStop(StopCause cause, u32 breakpointId);

// (E) only, from the ARM7 check (dbg_arm7_Check, dbg_arm7_OnBurstUnclocked: debugger.h) and from
// dbg::arm7::CompleteStepAtBurstEnd, when a breakpoint of the ARM7 stops or a step of the ARM7 is complete.
// Never blocks. It records the stop; it latches nothing and leaves nothing. It does not look at the run flag:
// a check that hits has suspended its burst whatever the machine is about to do, and dc_run deals with a
// burst it finds suspended.
// While the guest executes (the burst runs in the AICA scheduler callback, in a run or during a step):
// records the cause as a freezing cause and sets ATTN_PAUSE; the slice hook latches it behind the scheduler
// tick of the UpdateSystem() call that is running, also inside a SLEEP wait and during a step. The SH4
// executes nothing behind it. When the machine leaves Run() in the same slice for a reason of its own, dc_run
// acts on that first, with no Park() in between. A host stop or a guest exit: the burst runs to its end
// without checks in front of the stop callback, and this stop is not published. A guest reset: the burst is
// discarded, and the stop that the next Park() publishes is the reset's (StopCause::Reset), as for every
// stop that was wanted when a reset came.
// In the park context (the park loop runs a suspended burst on): records the cause as the stop that the park
// loop publishes next. The thread stays parked.
//   Breakpoint, id   an active listed breakpoint of the ARM7 stopped; the lowest id when several are on the
//                    address
//   Step, 0          the temporary breakpoint of the ARM7 stopped, or a step of the ARM7 is complete
// When other causes meet it in the same stop the published cause follows the priority of StopCause, and among
// breakpoints of the winning cause the lowest id, as for LatchStop.
void OnSoundStop(StopCause cause, u32 breakpointId);

// (E) only, from dbg::EventSlow for an event raised on the emulator thread. While the guest executes
// (Running, or a step) and an enabled event breakpoint on id exists whose category is active: records it
// as a freezing cause and sets ATTN_PAUSE; the slice hook latches the stop in the UpdateSystem() call that
// is running, or in the next one. In the park context it does nothing, also while the park loop runs a
// suspended sound burst on: the events of that burst are logged and do not stop. Never blocks.
void OnEvent(EventId id);

/*
	The SH4 engine, for dc.timing, describe.capabilities and the notices. The active backend is
	sh4_cpu->DebugBackend() (hw/sh4/sh4_if.h).
*/
struct Sh4EngineInfo {
	const char* backend;	// "interpreter", "x64", "arm64": constant string
	u32 caps;				// SuperH4Backend::DebugCaps() of the active backend
	bool fallback;			// breakpoints and steps use the interpreter fallback on this backend: it is a JIT
							// without SH4DBG_NATIVE, or a JIT with Debugger:ForceFallback set (the key has no
							// effect on the interpreter). False: they are native
	bool fallbackLoop;		// UseFallbackLoop() as SuperH4_impl::Run() last saw it
	int sliceCyclesLeft;	// SuperH4Backend::SliceCyclesLeft() of the active backend.
							// SH4 time is sh4_sched_now64() + SH4_TIMESLICE - sliceCyclesLeft
};

// (E), or under L while no emulator thread exists. Returns false, leaving *out alone, before the first
// thread of a machine selected a backend and when no machine exists. No side effects.
bool GetSh4Engine(Sh4EngineInfo* out);

}

}
