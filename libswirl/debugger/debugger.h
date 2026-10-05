/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include <atomic>

// See the note on picoTCP below.
#ifdef dbg
#error debugger.h cannot be used in a file that includes a picoTCP header: picoTCP defines dbg as a macro
#endif

#include "dbg_events.h"

/*
	The debugger as the rest of the emulator sees it. Emulator code includes this header and no other file of
	libswirl/debugger. Without NULLDC_DEBUGGER everything below is an empty inline stand-in: call sites are
	written once, without #if, and compile to nothing.

	A file that includes a picoTCP header cannot include this header, in either order, with or without
	NULLDC_DEBUGGER: picoTCP's arch headers (pico_posix.h, pico_msvc.h) define dbg as a macro, which replaces
	the name of the namespace below and every dbg:: that DBG_EVENT expands to.

	Threads
	  (U) main / UI thread
	  (E) emulator thread: Dreamcast_impl::dc_run and everything it calls, generated code included
	  (N) network thread of the debugger server: socket I/O only
	  (C) debugger core thread: parses requests, owns the timers, waits for (E)

	Lifetime of the machine, as the debugger tracks it (dbg::emu, guarded by the recursive lifetime lock L)
	  NoMachine  no VirtualDreamcast exists
	  Stopped    the machine exists, no emulator thread exists
	  Running    the emulator thread exists and runs the guest (or is on its way into or out of SuperH4::Run)
	  Parked     the emulator thread exists and sits in the park loop of dbg::emu

	The one rule: guest state and the debugger's tables (breakpoints, watches, categories) are touched on (E),
	or on another thread that holds L while no emulator thread exists. Nothing in the debugger keeps a pointer
	into guest state across a lifetime change.

	Contexts on (E)
	  slice hook    inside UpdateSystem(), between two slices of guest code; a block may be on the host stack
	                (the SLEEP handler calls UpdateSystem() from inside one)
	  park context  in dc_run after SuperH4::Run() returned and before it is called again: no guest code is on
	                the host stack, the SH4 context in Sh4cntx is complete. The park loop is the part of it
	                that waits while a debugger stop is in force; a service excursion is a visit that only runs
	                queued calls and goes back into Run()
*/

// SH4 code addresses are compared with this mask in builds without MMU: 0x0C.., 0x8C.. and 0xAC.. are one
// address. The complete rule, MMU builds included, is dbg::sh4::MaskAddr (dbg_sh4.h).
#define DBG_SH4_ADDR_MASK 0x1FFFFFFF

// What a block end, or an instruction of an interpreter-type loop, reports to dbg_sh4_CheckFlow.
enum DbgSh4Flow {
	DBG_SH4_FLOW_NONE = 0,
	DBG_SH4_FLOW_CALL = 1,		// BSR, BSRF, JSR
	DBG_SH4_FLOW_RTS  = 2,
	DBG_SH4_FLOW_RTE  = 3,
};

// Lets the compiler check the arguments of a printf-style function against its format. The archetype is
// spelled __printf__ because printf is a macro on some of the tree's platforms (types.h).
#if defined(__GNUC__)
#define DBG_PRINTF_LIKE(fmt, args) __attribute__((format(__printf__, fmt, args)))
#else
#define DBG_PRINTF_LIKE(fmt, args)
#endif

#if defined(NULLDC_DEBUGGER)

namespace dbg {

/*
	Process start and end (dbg_main.cpp).

	Section Debugger of the configuration (on the command line: -config Debugger:Port=0), keys and defaults:
	  read by Init()          Enabled yes, Bind 127.0.0.1, Port 55543, Required no, AllowedHosts (empty),
	                          WebRoot (empty), TickMs 200, FullTickEvery 5
	  read by dbg::emu        StartPaused no, ForceFallback no
	  read by dbg::selftest   SelfTest (empty)
*/

// (U), once, at the end of reicast_init(); lifetime NoMachine. With Debugger:Enabled it starts the server
// and the core thread and prints exactly "Debugger: http://<bind>:<port>/" with the bound port; it blocks
// for the bind only. A bind that fails ends the process with exit code 2 when Debugger:Required is set;
// otherwise it is logged and the emulator runs without a server. Without Debugger:Enabled nothing is
// started; the hooks and helpers below work all the same. Does not change signal dispositions.
void Init();

// (U), once, at the start of reicast_term(), before the GUI and with it the machine are destroyed; any
// lifetime state: an emulator thread may be running or parked. The caller must not hold L. Stops the server
// (every client is closed), ends the core thread and joins both; a call the core thread is waiting for is
// given up. Afterwards no listener is installed and no debugger thread exists; the hooks and helpers below
// stay callable until the process ends, and a parked emulator thread is released by the stop request that
// Dreamcast_impl::Term makes for any live thread.
void Term();

/*
	Attention word, stop latch.
*/
enum : u32 {
	ATTN_CALL  = 1,		// at least one call is queued for (E)
	ATTN_PAUSE = 2,		// a pause is wanted: a client asked, or an event or sound breakpoint fired
};

// UpdateSystem() tests it after the scheduler tick of every slice and calls emu::SliceHook() when it is not
// zero; memory_order_relaxed is enough there (a bit that is missed is seen one slice later).
// Bits are set by any thread and cleared by (E), only ever with fetch_or / fetch_and.
extern std::atomic<u32> g_attn;

// The stop latch: a debugger stop has been decided and (E) is on its way into the park context, or in it.
// (E) only. Set by emu::LatchStop() and at the end of a step, cleared when the park context is left.
// While it is set, emulator code obeys three rules:
//   UpdateSystem()  returns 1 as its first statement: no emulated time passes, no scheduler callback runs,
//                   the slice hook is not called
//   UpdateINTC()    accepts nothing: sets g_intc_deferred and returns 0
//   SLEEP handler   tests it before each UpdateSystem_INTC() of its wait; when set it sets g_sleep_cut,
//                   leaves the wait and re-executes SLEEP (next_pc -= 2)
extern bool g_stop_latched;

// (E) only. UpdateINTC() was called under the latch: one interrupt acceptance is owed, and is made when the
// park context is left. Set by UpdateINTC(), cleared by dbg::emu.
extern bool g_intc_deferred;

// (E) only. The SLEEP handler sets it before its wait loop and clears it after: an UpdateSystem() made
// while it is set is not a slice end of the backend loop, and the slice hook then leaves a pause request or
// a service excursion pending (for at most 250 ms of host time).
extern bool g_in_sleep;

// (E) only. The latch ended a SLEEP wait; set by the SLEEP handler together with next_pc -= 2. dbg::emu
// reads and clears it when the park context is left: the owed interrupt acceptance is then made with the PC
// behind the SLEEP, so that an accepted interrupt returns behind it.
extern bool g_sleep_cut;

// (E) is in a debugger stop: latched, parked or executing a step. SuperH4_impl::IsRunning() is
// CpuRunning || g_parked, so that a parked machine counts as running for the host UI.
// Set by (E) before it clears CpuRunning for a debugger stop; cleared by (E) when it resumes, and by
// SuperH4_impl::Stop() on any thread (a stop request ends the debugger stop).
extern std::atomic<bool> g_parked;

namespace emu {

// (E) only, from UpdateSystem() when g_attn is not zero and g_stop_latched is clear, after the scheduler
// tick of the slice. Never called from a signal handler. Does not wait for another thread.
//
// Lifetime Running:
//   1. runs the queued CC_READ calls (dbg_emu.h), one at a time
//   2. latches the stop (emu::LatchStop) if a freezing cause is recorded (event breakpoint, sound
//      breakpoint), or if g_in_sleep is clear and a pause was requested or a call of another class is queued.
//      A pause request or queued call met while g_in_sleep is set stays pending; once it has been pending
//      for 250 ms of host time it is latched wherever the hook is called from.
// Lifetime Parked (a step is executing): runs no call; latches the stop if a freezing cause is recorded,
// which ends the step at this slice boundary.
//
// It never changes the armed breakpoint set and never touches the code cache: a block may be executing.
// Guest state is only read, and Sh4cntx is complete here only on a backend with SH4DBG_CTX_AT_SLICE.
void SliceHook();

// (E) only: SuperH4_impl::Run() asks it once per entry and runs sh4_fallback_loop() (hw/sh4/sh4_dbg.h)
// instead of the backend's Loop() when it is true. True while the active backend is a JIT without
// SH4DBG_NATIVE, or a JIT with Debugger:ForceFallback set (the key has no effect on the interpreter), and an
// SH4 breakpoint or temporary breakpoint is active or a step-out is pending. The value changes only in the
// park context. No side effects.
bool UseFallbackLoop();

// Any thread, any lifetime state; returns at once (it waits for nothing but the short mutex of the call
// queue). Wakes the park loop, which then returns to dc_run for the evaluation of the machine's stop
// reasons. SuperH4_impl::Stop() calls it after it cleared CpuRunning and g_parked. Without a parked thread
// it does nothing.
void Kick();

/*
	Lifetime hooks, called by Dreamcast_impl. Each one that publishes a lifetime change invokes the run-state
	listener (dbg_emu.h) on the calling thread before it returns.

	  Init        LifetimeLock; create the machine; OnMachineCreated()                NoMachine -> Stopped
	  StartGame   LifetimeLock; ...; Reset(); Resume()
	  Reset       LifetimeLock; reset the machine; OnGuestStateReplaced()
	  LoadState   LifetimeLock; load; OnGuestStateReplaced()
	  SaveState   LifetimeLock; save
	  Resume      join a previous thread; LifetimeLock; OnThreadStarting();           Stopped -> Running
	              start the thread; wait for emu_started
	  dc_run      OnThreadEntered(); select the backends; Start(); set emu_started;
	              the loop of Park() below; stop callback; OnThreadExiting()          Running -> Stopped
	  Term        stop and join a live thread; LifetimeLock; OnMachineDestroying();   Stopped -> NoMachine
	              destroy the machine
*/

// RAII over the lifetime lock L. L is recursive: StartGame holds it across Reset() and Resume().
// (U), and (E) inside Dreamcast_impl::Reset (a guest reset, a reset by the debugger). Blocks while another
// thread holds L: (C) holds it for the calls it runs itself while no emulator thread exists, bounded by
// those calls.
// The holder must not join the emulator thread (OnThreadExiting takes L on (E)): Resume and Term join
// first, then lock. Resume may wait for emu_started while holding L, because dc_run takes no L before it
// set emu_started.
struct LifetimeLock {
	LifetimeLock();
	~LifetimeLock();
	LifetimeLock(const LifetimeLock&) = delete;
	LifetimeLock& operator=(const LifetimeLock&) = delete;
};

// (U), at the end of Dreamcast_impl::Init, L held, lifetime NoMachine. Publishes Stopped. From here on
// the debugger reads the machine (sh4_cpu, its devices) under the one rule.
void OnMachineCreated();

// (U), at the start of Dreamcast_impl::Term once no emulator thread exists, L held, lifetime Stopped.
// Publishes NoMachine before anything of the machine is destroyed, and empties the symbol table. When it
// returns, no debugger thread is inside a call that touches the machine and none starts one: the debugger
// answers from its constant tables.
void OnMachineDestroying();

// (U), in Dreamcast_impl::Resume before the thread is started, L held, lifetime Stopped. Publishes Running.
// Until OnThreadEntered() has run on the new thread, calls queue up (dbg::emu::Call).
void OnThreadStarting();

// (E), first statement of dc_run, no lock held, lifetime Running. Makes the calling thread the emulator
// thread (emu::OnEmuThread) and the next Park() the first one of this thread. Does not block.
void OnThreadEntered();

// (E), last action of dc_run, after the stop callback ran; no lock held by the caller; lifetime Running
// or Parked. guestExit: the thread ends because the guest asked for it (reios_exit), not because the host
// stopped it. Takes L (blocks while (U) holds it, which (U) never does while it waits for this thread);
// drains the call queue under it: CC_READ and CC_TABLE calls run, CC_WRITE and CC_EXEC calls complete with
// -32001; drops a pause request that was not honoured; publishes Stopped (reason host-stopped, or
// guest-exit). After it returns the thread touches no guest state.
void OnThreadExiting(bool guestExit);

// In Dreamcast_impl::Reset and LoadState, L held, after the new guest state is in place; any lifetime
// state with a machine: (U) while Stopped, (E) for a guest reset or a reset by the debugger (park context).
// The debugger drops what belonged to the old state: the owed remainder and resume exemption of the SH4
// breakpoint mechanism, g_sleep_cut, temporary breakpoints, a pending step-out.
// While Stopped it publishes a new stop (the stop counter advances) so that clients fetch again; on (E) the
// stop, if one is in force, is published by Park().
void OnGuestStateReplaced();

/*
	The park context, called by dc_run on (E):

	    OnThreadEntered(); ...; sh4_cpu->Start(); emu_started.Set();
	    for (;;) {
	        if (Park() == PARK_RUN)
	            sh4_cpu->Run();
	        // the machine's own stop reasons, in this order:
	        //   guest reset pending:              save ROM files, Reset(), sh4_host_fp_resync(), sh4_cpu->Start()
	        //   host stop or guest exit pending:  break
	    }
	    ...; stop callback; OnThreadExiting(guestExit);

	dc_run sets CpuRunning (SuperH4::Start) at thread start and after a guest reset; everywhere else Park()
	owns CpuRunning, g_parked and g_stop_latched.
*/
enum : u32 {
	PARK_RUN      = 0,		// the park context has been left; CpuRunning is set: call SuperH4::Run()
	PARK_EVALUATE = 1,		// evaluate the machine's stop reasons, then call Park() again
};

// (E), dc_run only; lifetime Running or Parked; the caller holds no lock. Called before the first
// SuperH4::Run() of a thread, after emu_started is set, and after every evaluation of the machine's stop
// reasons that did not end the thread.
//
// - Runs the queued calls of every class, one at a time. With no debugger stop in force this is a service
//   excursion: no stop is published.
// - Parks while a debugger stop is in force: the stop that was latched before Run() returned; a guest
//   reset made while a debugger stop was in force or a pause was wanted (reason reset); a start park (reason
//   start): at the first Park() after OnMachineCreated() when Debugger:StartPaused is set, and at the first
//   Park() of every emulator thread while a selftest session is active (dbg::selftest::Active()).
//   Entering a stop: g_parked is set, CpuRunning cleared, the SH4 breakpoint state drops its temporary
//   breakpoints and a pending step-out, the listener is invoked, the window title becomes
//   "nullDC 3.0.0 [paused by debugger]". The park loop then waits, without limit, for a call, for a
//   run-control action that a call armed, or for Kick(); armed steps are executed by the loop itself.
// - Leaves the park context for a resume and at the end of a service excursion, in this order: clears
//   g_stop_latched; makes the interrupt acceptance that the latch deferred; calls sh4_host_fp_resync(); for a
//   resume arms the SH4 resume exemption; sets CpuRunning and clears g_parked, unless a host stop or a guest
//   exit is pending; for a resume invokes the listener with Running.
//
// Returns PARK_RUN after leaving, with CpuRunning set and g_stop_latched clear.
// Returns PARK_EVALUATE, with CpuRunning clear, whenever a stop reason of the machine may be pending: after
// Kick(), after a call that asked for a reset, after a step, and in place of PARK_RUN when a host stop or a
// guest exit is pending. A debugger stop that was in force stays in force across the evaluation: dc_run
// does not save ROM files and does not run the stop callback for it.
u32 Park();

}

/*
	Events. Ids and their table: dbg_events.h.
*/

// One gate per event: 1 while a client is connected (net::Server::HasClients) and the event is EVR_LOW or an
// event breakpoint on it exists, else 0. Written by the debugger core; read by any thread through DBG_EVENT.
// A selftest session has no client: a guest test that sets an event breakpoint (emu::SetEventBreakpoints)
// stores 1 in the gate of that event itself, and 0 when it removes the breakpoint. The core rewrites the
// gates only when a client connects or disconnects or the event breakpoints change through it.
extern std::atomic<u8> g_event_on[EV_ID_COUNT];

// Raise an event: DBG_EVENT(dbg::EV_GDROM_COMMAND, "command %02X", cmd). The format is required. With the
// gate closed the cost is one byte load and the arguments are not evaluated, so they must have no side
// effect. Any thread, any lifetime state, with or without L; not from a signal handler.
#define DBG_EVENT(id, ...) do { if (unlikely(dbg::g_event_on[id].load(std::memory_order_relaxed))) dbg::EventSlow(id, __VA_ARGS__); } while (0)

// Behind DBG_EVENT; not called directly. printf-style; the message is cut to kEventMessageMax bytes.
// Appends an entry to the event log under the log's own mutex, which is held for the append only: it
// never takes L, never waits for (E) and may be called with any emulator lock held.
// On (E) while the guest executes (Running, or a step) it also tests the event breakpoints (emu::OnEvent):
// a hit asks for a pause, which the slice hook latches in the UpdateSystem() call that is running or the
// next one. Events raised on other threads, or on (E) in the park context, never stop.
// The cycles of the entry (metadata.cycles) are sh4_sched_now64() while a machine exists (the lifetime of
// emu::GetRunState() is not NoMachine) and 0 otherwise: the SH4 context block goes away with the machine.
void EventSlow(EventId id, const char* fmt, ...) DBG_PRINTF_LIKE(2, 3);

namespace symbols {

// (E), from reios when it has loaded an ELF (every boot and reset that loads one): image / size are the
// file as read, valid only during the call. Replaces the symbol table with the .symtab of the image; an
// image without one, or one that is not an ELF, empties the table. reios also calls it with (nullptr, 0) on
// a boot that loads no ELF (a disc, a .bin): that empties the table. Does not block.
void OnElfLoaded(const void* image, size_t size);

}

}

/*
	SH4 code breakpoints and steps: what the SH4 backends call (dbg_sh4.cpp). Everything is (E) only.

	A check is code in front of one guest instruction that calls dbg_sh4_Check and can stop there. A JIT
	decoder asks dbg_sh4_WantsCheck for every instruction it decodes that is not a delay slot and emits a
	check where it says so; an interpreter-type loop asks in front of every instruction it executes.
	The armed set changes only in the park context, or under L while no emulator thread exists: never while a
	block is decoded or executing. Each change made on (E) discards the compiled blocks of the address's page
	through SuperH4Backend::DebugDiscardPage of the active backend, sh4_cpu->DebugBackend() (hw/sh4/sh4_if.h).
*/

// Number of armed addresses. Zero keeps the query to one load and a branch.
extern u32 dbg_sh4_armed;

// Is the address armed? Compared under the address rule (dbg::sh4::MaskAddr). No side effects.
bool dbg_sh4_IsArmed(u32 pc);

// Does the instruction at pc get a check in front of it? has_slot: its opcode has the Delayslot type bit.
// A breakpoint on a delay slot is checked in front of its branch; this is never asked for an instruction
// that runs as the delay slot of the instruction in front of it.
static inline bool dbg_sh4_WantsCheck(u32 pc, bool has_slot)
{
	if (likely(dbg_sh4_armed == 0))
		return false;

	return dbg_sh4_IsArmed(pc) || (has_slot && dbg_sh4_IsArmed(pc + 2));
}

// Is any address in [pc, pc + bytes) armed? For idioms that fold several instructions into one unit: they
// are not folded when it is true. No side effects.
bool dbg_sh4_AnyArmed(u32 pc, u32 bytes);

// The check. Called from generated code and from interpreter-type loops, in front of the instruction at pc
// and never between a delayed branch and its slot, with the SH4 context complete in Sh4cntx except for the
// pc (every register the backend caches is written back; nothing is cached across the call).
//   refund     cycles of the running block that belong to the instructions from pc on
//   block_end  address behind the last instruction of the running block; 0, with refund 0, when the caller
//              charges per instruction and has no blocks
// Returns 0 to continue: guest state was only read; registers are reloaded from the context after the call.
// Returns non-zero to stop in front of the instruction: it has written Sh4cntx.pc = pc, recorded
// { pc, block_end, refund } as the owed remainder (none when block_end is 0) and latched the stop
// (dbg::emu::LatchStop: g_stop_latched and g_parked set, CpuRunning cleared). The caller adds refund to
// its slice counter and leaves its loop at once: no further guest instruction, no UpdateSystem().
extern "C" u32 DYNACALL dbg_sh4_Check(u32 pc, u32 refund, u32 block_end);

// Non-zero while a step-out is pending. A JIT decoder then marks the blocks that end with a call or a
// return and their epilogue reports to dbg_sh4_CheckFlow; an interpreter-type loop reports after each call
// and RTS it executed, and its RTE handler reports itself. Changes like the armed set, with a discard
// through SuperH4Backend::DebugDiscardFlow.
extern u32 dbg_sh4_stepout;

// The flow report: flow is a DbgSh4Flow other than DBG_SH4_FLOW_NONE. Called after the call or return and
// its delay slot ran and next_pc was written, with the complete SH4 context, pc included; an RTE reports
// before its interrupt check. With no step-out pending it returns 0.
// Returns non-zero to stop: it has latched the stop as dbg_sh4_Check does; nothing is owed, the block was
// charged in full. The caller leaves its loop at once; for an RTE it first makes the UpdateINTC() call it
// would have made (under the latch that call accepts nothing and leaves the acceptance owed).
// The report can stop behind the block that used the slice up, before the loop tested its counter: a Loop()
// entered with a slice counter that is not positive and no remainder to run does the end-of-slice work
// before its first block (hw/sh4/sh4_if.h).
extern "C" u32 DYNACALL dbg_sh4_CheckFlow(u32 flow);

// From Do_Interrupt and Do_Exception, on every call: an interrupt or exception is being accepted (TRAPA
// included). Counts handler levels for a pending step-out; no other effect, does not stop.
void dbg_sh4_OnException();

// Did a check or flow report stop? True from a non-zero return of dbg_sh4_Check / dbg_sh4_CheckFlow until
// the park context is entered (dbg::sh4::OnStop). A backend asks where the return value is not at hand:
// after it ran a block outside the block cache, after a report in an interpreter-type loop.
bool dbg_sh4_StopPending();

// From the backend's Loop(), before it enters its main loop. If the rest of a block is owed for exactly
// this pc (the full address, as dbg_sh4_Check got it), writes *end_pc and *cycles and returns true: the
// backend first runs [pc, end_pc) as one block outside the block cache that costs *cycles. Otherwise
// returns false and leaves both alone. Nothing is owed after the call, whatever it returned.
bool dbg_sh4_TakeRemainder(u32 pc, u32* end_pc, u32* cycles);

#else	// !NULLDC_DEBUGGER

namespace dbg {

// Stand-ins for the variables: they read as zero and writes to them vanish.
struct NoFlag {
	constexpr operator bool() const { return false; }
	constexpr bool load(std::memory_order = std::memory_order_seq_cst) const { return false; }
	const NoFlag& operator=(bool) const { return *this; }
	void store(bool, std::memory_order = std::memory_order_seq_cst) const { }
};

struct NoWord {
	constexpr u32 load(std::memory_order = std::memory_order_seq_cst) const { return 0; }
};

struct NoCount {
	constexpr operator u32() const { return 0; }
	constexpr explicit operator bool() const { return false; }
};

static inline void Init() { }
static inline void Term() { }

constexpr NoWord g_attn{};
constexpr NoFlag g_stop_latched{};
constexpr NoFlag g_intc_deferred{};
constexpr NoFlag g_in_sleep{};
constexpr NoFlag g_sleep_cut{};
constexpr NoFlag g_parked{};

namespace emu {

static inline void SliceHook() { }
static inline bool UseFallbackLoop() { return false; }
static inline void Kick() { }

struct LifetimeLock {
	LifetimeLock() { }
	~LifetimeLock() { }
	LifetimeLock(const LifetimeLock&) = delete;
	LifetimeLock& operator=(const LifetimeLock&) = delete;
};

static inline void OnMachineCreated() { }
static inline void OnMachineDestroying() { }
static inline void OnThreadStarting() { }
static inline void OnThreadEntered() { }
static inline void OnThreadExiting(bool) { }
static inline void OnGuestStateReplaced() { }

enum : u32 {
	PARK_RUN      = 0,
	PARK_EVALUATE = 1,
};

static inline u32 Park() { return PARK_RUN; }

}

#define DBG_EVENT(id, ...) do { } while (0)

namespace symbols {

static inline void OnElfLoaded(const void*, size_t) { }

}

}

constexpr dbg::NoCount dbg_sh4_armed{};
constexpr dbg::NoCount dbg_sh4_stepout{};

static inline bool dbg_sh4_IsArmed(u32) { return false; }
static inline bool dbg_sh4_WantsCheck(u32, bool) { return false; }
static inline bool dbg_sh4_AnyArmed(u32, u32) { return false; }
static inline u32 DYNACALL dbg_sh4_Check(u32, u32, u32) { return 0; }
static inline u32 DYNACALL dbg_sh4_CheckFlow(u32) { return 0; }
static inline void dbg_sh4_OnException() { }
static inline bool dbg_sh4_StopPending() { return false; }
static inline bool dbg_sh4_TakeRemainder(u32, u32*, u32*) { return false; }

#endif
