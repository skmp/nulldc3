/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of SH4 code breakpoints and steps: sh4bp.b1 to sh4bp.b14, the gates of that mechanism on the
	real park loop and the real machine. There is no sh4bp.b11: that gate compares the build with the debugger
	against the build without, which a session cannot (tests/debugger/sh4bp, debugger.sh4bp.b11).

	    nulldc3 --config Debugger:SelfTest=sh4bp , Debugger:StartPaused=yes <elf>

	The tests are guest tests: each stands where a client stands, arms breakpoints and steps through dbg::emu
	and is handed every stop. Most of them run the guest programs of tests/debugger/sh4bp, which they write
	into main memory themselves; the ones that need the booted guest reset the machine first. A session tests
	the SH4 backend that is configured. What two sessions have to agree on (the interpreter against the JIT,
	the forced fallback against the JIT) is printed as "note: cmp <key> <value>" lines, which
	tests/debugger/sh4bp/selftest.sh compares.

	Every test begins with a reset of the machine, so that nothing of the guest that ran before it is left,
	and the guest programs take no interrupt: they run with SR.IMASK = 15. The tests sh4bpboot.* are B3, B6 and
	B7 on the booted guest with its interrupts live, with the state at a fixed vblank compared between two
	sessions ("The booted guest" below). The timing mode of a session is the configured one too: a session with
	config:Dynarec.idleskip=no is these gates with idleskip off.

	Times are compared as differences between two stops of one run. In a session with the forced fallback
	the loop that runs the guest changes with the breakpoints, and behind a recompiler whose loop starts a
	slice of its own at every entry the slice counter that tells the time changes with it: a run that is
	timed begins with a breakpoint on its first instruction, and its time is taken from that stop on.
*/

#include "dbg_selftest.h"
#include "dbg_sh4.h"
#include "dbg_mem.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_dbg.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/sh4_sched.h"

#if FEAT_SHREC != DYNAREC_NONE
#include "hw/sh4/dyna/blockmanager.h"
#endif

#include "dbg_selftest_sh4bp_guest.inc"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <thread>

namespace {

using namespace dbg;
using selftest::Check;
using selftest::Note;

const u32 kStackTop = 0x8C00F000;
const u32 kEntry = 0x8C010000;		// where reios starts an ELF

Sh4Context& Ctx()
{
	return p_sh4rcb->cntx;
}

u32& R(u32 n)
{
	return Ctx().r[n];
}

u8* Ram(u32 addr)
{
	return &sh4_cpu->mram[addr & RAM_MASK];
}

u32 Ram32(u32 addr)
{
	u32 value;
	memcpy(&value, Ram(addr), 4);
	return value;
}

u64 Fnv(u64 h, const void* data, size_t bytes)
{
	const u8* p = (const u8*)data;

	for (size_t i = 0; i < bytes; i++)
		h = (h ^ p[i]) * 0x100000001B3ull;

	return h;
}

const u64 kFnvBasis = 0xCBF29CE484222325ull;

// The integer registers a guest sees
u64 IntHash(u64 h = kFnvBasis)
{
	Sh4Context& c = Ctx();
	u32 misc[] = { c.pc, c._pr, c.gbr, c.mac.l, c.mac.h, c.sr.status, c.sr.T };

	h = Fnv(h, c.r, sizeof(c.r));
	return Fnv(h, misc, sizeof(misc));
}

// Every register, the FP registers bit for bit
u64 AllHash()
{
	Sh4Context& c = Ctx();
	u32 misc[] = { c.fpscr.full, c.fpul };

	return Fnv(Fnv(IntHash(), c.xffr, sizeof(c.xffr)), misc, sizeof(misc));
}

// The memory the guest programs write
u64 DataHash()
{
	return Fnv(Fnv(kFnvBasis, Ram(G_data), 32), Ram(G_buffer), 256);
}

// SH4 time: the scheduler's, and what the running slice has used
u64 Time()
{
	emu::Sh4EngineInfo info = { };

	emu::GetSh4Engine(&info);

	return sh4_sched_now64() + SH4_TIMESLICE - info.sliceCyclesLeft;
}

const char* Backend()
{
	emu::Sh4EngineInfo info = { };

	if (!emu::GetSh4Engine(&info))
		return "";

	return info.fallback ? "fallback" : info.backend;
}

bool IsInterpreter()
{
	return strcmp(Backend(), "interpreter") == 0;
}

void Bps(std::initializer_list<u32> addrs)
{
	u32 ids[16], a[16];
	u32 n = 0;

	for (u32 addr : addrs)
	{
		ids[n] = n + 1;
		a[n] = addr;
		n++;
	}

	emu::SetSh4Breakpoints(ids, a, n);
}

// The guest programs in memory, the registers as a reset leaves them but for SR (MD only, every interrupt
// masked), the vectors and the stack; everything active, no breakpoint
void LoadGuest()
{
	Sh4Context& c = Ctx();
	SuperH4Backend* backend = sh4_cpu->DebugBackend();

	emu::SetSh4Breakpoints(nullptr, nullptr, 0);
	emu::SetActiveMask(~0u);

	// compiled code of what was there
	if (backend)
	{
		for (u32 addr = G_BASE; addr < G_BASE + sizeof(guest_image) + REI_PAGE_SIZE; addr += REI_PAGE_SIZE)
			backend->DebugDiscardPage(addr);
	}

	memcpy(Ram(G_BASE), guest_image, sizeof(guest_image));

	memset(c.r, 0, sizeof(c.r));
	memset(c.r_bank, 0, sizeof(c.r_bank));
	memset(c.xffr, 0, sizeof(c.xffr));

	c.gbr = c.ssr = c.spc = c.sgr = c.dbr = 0;
	c.mac.full = 0;
	c._pr = c.fpul = 0;

	sh4_sr_SetFull(0x400000F0);
	UpdateSR();

	c.fpscr.full = 0x00040001;
	UpdateFPSCR();

	c.vbr = G_vbr;
	c.r[15] = kStackTop;
}

void Start(u32 pc)
{
	Ctx().pc = pc;
	sh4::OnPcWritten();
}

// Arms a resume. true, as every function that arms: the next step gets the next stop
bool Go()
{
	int rv = emu::Resume();

	if (rv != 0)
		Check(false, "a resume was refused: %d", rv);

	return true;
}

bool Step(StepKind kind)
{
	const char* why = "";
	int rv = emu::Step(Target::Sh4, kind, &why);

	if (rv != 0)
		Check(false, "a step was refused: %d %s, pc %08X", rv, why ? why : "", Ctx().pc);

	return true;
}

bool IsBp(const emu::RunState& s, u32 id, u32 pc)
{
	return s.lifetime == Lifetime::Parked && s.cause == StopCause::Breakpoint && s.breakpointId == id && Ctx().pc == pc;
}

bool IsStep(const emu::RunState& s, u32 pc)
{
	return s.lifetime == Lifetime::Parked && s.cause == StopCause::Step && Ctx().pc == pc;
}

/*
	A test is a list of steps. A step looks at the stop it is handed and either arms what runs next (returns
	true: the step behind it gets the next stop) or goes straight on (returns false).
*/
typedef bool (*StepFn)(const emu::RunState& stop);

bool RunScript(const StepFn* steps, u32 count, u32& at, const emu::RunState& stop)
{
	while (at < count)
	{
		if (steps[at++](stop))
			return false;
	}

	at = 0;
	return true;
}

#define SCRIPT(name) \
	static u32 name##_at; \
	static const StepFn name##_steps[]

#define RUN_SCRIPT(name) \
	return RunScript(name##_steps, sizeof(name##_steps) / sizeof(name##_steps[0]), name##_at, stop)

#define STEP [](const dbg::emu::RunState& stop) -> bool

// what a step leaves for the ones behind it
u64 t0, base_time, base_ints, base_data;
u32 sp0, hits, count, end_pc, arrivals, case_at;
u64 trace;
std::thread* helper;

// The first step of every test: the machine as a reset leaves it, parked at the reset vector
bool ResetMachine(const emu::RunState&)
{
	emu::SetSh4Breakpoints(nullptr, nullptr, 0);
	emu::Reset();
	return true;
}

// A timed run: the breakpoints of the test, and one more on the first instruction, with the id behind theirs.
// The stop at it is where the time of the run starts
u32 entry_pc;
bool entered;

void TimedBps(u32 entry, std::initializer_list<u32> addrs)
{
	u32 ids[16], a[16];
	u32 n = 0;

	for (u32 addr : addrs)
	{
		ids[n] = n + 1;
		a[n] = addr;
		n++;
	}

	ids[n] = n + 1;
	a[n] = entry;
	n++;

	emu::SetSh4Breakpoints(ids, a, n);

	entry_pc = entry;
	entered = false;
}

// true: this stop was the one at the first instruction; the run goes on and the step is called again
bool Entered(u32& at)
{
	if (entered || Ctx().pc != entry_pc)
		return false;

	entered = true;
	t0 = Time();
	at--;

	return Go();
}


/*
	The booted guest: sh4bpboot.b3, sh4bpboot.b6, sh4bpboot.b7 and their twins sh4bpbase.*

	B3, B6 and B7 are also made on the guest of the session as reios boots it, with its interrupts live. What
	they compare there is the state of the machine at a fixed vblank: scheduler time, the slice counter, the
	pc, a hash of the SH4 context and a hash of main memory.

	A run is a reset, main memory cleared (the boot loads the guest into it again) and the guest resumed up to
	its vblank kBootVblanks. An event breakpoint on dc.holly.core.vblank counts the vblanks. It stops the guest
	at every one of them, where the slice of the vblank ends. Time counts from the reset. What a case tests it
	arms in the stop of vblank kBootWindow, so that all runs are one run up to there. A test is a row of runs,
	one for each of its cases, and prints the state that each ended in as "note: cmp boot.<gate>.<case> <state>".

	A reset does not put the whole machine back: the scheduler keeps its time, the devices their place in it and
	some of their registers. A run therefore has no baseline in its own session. Its baseline is the run at the
	same place of a second session, which makes the same row of runs with nothing of the cases in them: the
	tests sh4bpbase.*, to be selected in place of sh4bpboot.*. Both sessions come to every reset with the same
	machine, as long as the runs in front of it changed nothing for the guest, and what they print for a run
	has to be equal. tests/debugger/sh4bp/selftest.sh runs the sessions and compares them: two sessions of
	baselines have to agree, and a session of cases that differs from them is run twice more and fails when it
	differs again, because runs of one guest can also differ for reasons that are not the debugger's.
	These tests are not under the name sh4bp: a session of theirs holds nothing but the runs, which no thread
	of the host has a say in.

	What a case stops at are addresses of the guest's own path: the instructions of kBootSteps single steps
	from the stop of vblank kBootWindow, taken in a run of their own that every session begins with, sorted
	into what the gate asks for with the opcodes and, on a recompiler, with the blocks that are cached for them.

	A vblank that finds the guest waiting in a SLEEP cuts the wait short, and how the backend loop goes on from
	there to the park depends on whether the SLEEP has a breakpoint: with one it does not run the rest of its
	slice out. That is a skew of the stop of the event breakpoint, not of the breakpoint on the SLEEP, and it
	makes the two runs differ. The case that stops at SLEEPs therefore counts no vblanks behind kBootWindow.
	Its run, and the one of its baseline, end at the kBootInterrupts-th interrupt that the guest accepts from
	there on: a breakpoint on the interrupt vector, armed in the stop of kBootWindow, stops at every one of
	them, behind the wait that the interrupt ended.

	In a session with the forced fallback the interpreter fallback runs the guest while a breakpoint is active,
	and its FPU arithmetic is not the JIT's. Every run there has a breakpoint on the entry point of the guest
	from the stop of kBootWindow on, where the guest does not come back to: from there to its end the run is
	the fallback loop's, the baselines included. selftest.sh prints how those compare with the JIT's (B12).
*/
const u32 kBootVblanks = 400;		// the vblank the state is compared at
const u32 kBootWindow = 300;		// the vblank a case arms at
const u32 kBootInterrupts = 200;	// the accepted interrupt behind it that the case with the SLEEPs is compared at
const u32 kBootSteps = 12000;		// the steps the path is sampled with
const u32 kStopBudget = 20000;		// a case that stops at breakpoints, or pauses, disarms behind this many stops
const u32 kCallBudget = 2000;		// the calls of the case with service excursions
const u32 kOutChain = 4;			// step-outs asked for between two vblanks
const u32 kVoidRuns = 20;			// runs of a case that are made again because its other thread came too late
const u32 kVblankId = 1000;			// the id of the event breakpoint
const u32 kResetVector = 0xA0000000;	// where a run starts
const u16 kSleepOp = 0x001B;

struct BootState
{
	u64 time;		// scheduler time since the reset
	int slice;		// cycles left in the running slice
	u32 pc;
	u64 ctx;		// every register a guest sees, and the pending interrupts
	u64 ram;		// main memory
};

const char* Text(const BootState& s)
{
	static char text[4][128];
	static u32 next;

	char* out = text[next++ & 3];

	snprintf(out, sizeof(text[0]), "time %llu slice %d pc %08X context %016llX memory %016llX", (unsigned long long)s.time, s.slice, s.pc,
		(unsigned long long)s.ctx, (unsigned long long)s.ram);

	return out;
}

struct AddrSet
{
	u32 addr[24];
	u32 n;
	bool none;		// a session of this kind has no such address

	void Add(u32 a)
	{
		for (u32 i = 0; i < n; i++)
		{
			if (addr[i] == a)
				return;
		}

		if (n < sizeof(addr) / sizeof(addr[0]))
			addr[n++] = a;
	}

	void Add(const AddrSet& o)
	{
		for (u32 i = 0; i < o.n; i++)
			Add(o.addr[i]);
	}

	void Remove(u32 a)
	{
		for (u32 i = 0; i < n; i++)
		{
			if (addr[i] == a)
				addr[i--] = addr[--n];
		}
	}

	// up to `want` of the `count` candidates, spread over them
	void Spread(const u32* from, u32 count, u32 want)
	{
		for (u32 k = 0; k < want && k < count; k++)
			Add(from[count <= want ? k : (2 * k + 1) * count / (2 * want)]);
	}
};

/*
	What a case does in its run:
	  BOOT_WINDOW  parked at the vblank kBootWindow: it arms what it tests
	  BOOT_STOP    a stop behind that one and in front of the end of the run, whatever its cause. The guest is
	               resumed behind it unless the case arms a step (BootStep) or ends the run (boot_abort)
	  BOOT_END     parked where the run ends, the state is taken. A case whose other thread did not get to act
	               in this run says so (boot_void): the run is made again
	  BOOT_RESULT  the check of the case, on what it counted
*/
enum BootAt { BOOT_WINDOW, BOOT_STOP, BOOT_END, BOOT_RESULT };

typedef void (*BootFn)(BootAt at, const emu::RunState& stop);

// Where a run ends: at the vblank kBootVblanks, or at the interrupt kBootInterrupts behind the vblank kBootWindow
enum BootMode { BOOT_VBLANKS, BOOT_INTERRUPTS, BOOT_MODES };

struct BootCase
{
	const char* what;
	BootFn fn;
	const AddrSet* set;		// what the case arms: it is left out when there is nothing in it
	const char* missing;	// the name of the set, when the path of a guest can lack it
	bool native;			// not in a session with the forced fallback
	BootMode mode;
};

// a run
u32 boot_phase;				// 0: none, 1: the reset is made, 2: the guest runs
BootMode boot_mode;
u32 boot_vblanks;
u32 boot_vector;			// the interrupt vector with its breakpoint, 0: none
u32 boot_interrupts;		// stops at it
u64 boot_sched0;
bool boot_bare;				// a run that never keeps the fallback loop: the one that takes the path
bool boot_fallback;			// the run keeps the fallback loop from here on, with a breakpoint on the entry point
bool boot_armed, boot_abort, boot_void;
bool boot_ok;				// the run got to its end, boot_state is the state there
bool boot_loop;				// and the fallback loop ran the guest
BootState boot_state;

// the case of a run, and what it counts
const BootCase* boot_case;
u32 boot_stops, boot_pauses, boot_calls, boot_outs, boot_ended, boot_chain;

// the path
u32 prof_pc[kBootSteps];
u16 prof_op[kBootSteps];
u32 prof_n;
bool prof_known;
u32 boot_hot;
AddrSet set_never, set_plain, set_pair, set_slot, set_sleep, set_idle, set_cut, set_all;
AddrSet seen_sleep;

/*
	A helper thread stands for another thread of the debugger: it asks for pauses or makes calls while the guest
	runs. It works for one generation, the one it was started in, and is not waited for: a call of its that
	is still queued when the generation ends does nothing when it is taken.
*/
std::atomic<u32> helper_gen;
std::atomic<bool> helper_ready;

// Returns when the thread runs: the stretch of the guest that it is for can be short
void HelperStart(void (*body)(u32 gen))
{
	helper_ready = false;
	std::thread(body, helper_gen.load()).detach();

	while (!helper_ready)
		std::this_thread::yield();
}

void HelperEnd()
{
	helper_gen++;
}

bool HelperLive(u32 gen)
{
	return helper_gen.load() == gen;
}

bool SessionFallback()
{
	emu::Sh4EngineInfo info = { };

	return emu::GetSh4Engine(&info) && info.fallback;
}

// The event breakpoint on the vblank. No client is connected: the gate of the event is the test's to open
void Vblank(bool on)
{
	u32 id = kVblankId;
	u8 event = (u8)EV_CORE_VBLANK;

	emu::SetEventBreakpoints(&id, &event, on ? 1 : 0);
	g_event_on[EV_CORE_VBLANK].store(on ? 1 : 0);
}

// The breakpoints of a run: these addresses with the ids 1, 2, ..., and behind them the ones on the interrupt
// vector and on the entry point when the run has them
void BootArm(const u32* addrs, u32 n)
{
	u32 ids[sizeof(AddrSet::addr) / sizeof(u32) + 2], a[sizeof(AddrSet::addr) / sizeof(u32) + 2];
	u32 count = 0;

	for (; count < n; count++)
	{
		ids[count] = count + 1;
		a[count] = addrs[count];
	}

	if (boot_vector)
	{
		ids[count] = count + 1;
		a[count] = boot_vector;
		count++;
	}

	if (boot_fallback)
	{
		ids[count] = count + 1;
		a[count] = kEntry;
		count++;
	}

	emu::SetSh4Breakpoints(ids, a, count);
}

void BootArm(const AddrSet& set)
{
	BootArm(set.addr, set.n);
}

// A step that a case arms. false: it is refused, and the guest is resumed
bool BootStep(StepKind kind)
{
	if (emu::Step(Target::Sh4, kind, nullptr) != 0)
		return false;

	boot_armed = true;
	return true;
}

BootState BootCapture()
{
	Sh4Context& c = Ctx();
	emu::Sh4EngineInfo info = { };
	BootState s;

	emu::GetSh4Engine(&info);

	s.time = sh4_sched_now64() - boot_sched0;
	s.slice = info.sliceCyclesLeft;
	s.pc = c.pc;

	u32 misc[] = { c.gbr, c.ssr, c.spc, c.sgr, c.dbr, c.vbr, c._pr, c.fpul, c.pc, c.sr.status, c.sr.T, c.fpscr.full, c.interrupt_pend };
	u64 h = Fnv(kFnvBasis, c.xffr, sizeof(c.xffr));

	h = Fnv(h, c.r, sizeof(c.r));
	h = Fnv(h, &c.mac, sizeof(c.mac));
	h = Fnv(h, c.r_bank, sizeof(c.r_bank));
	s.ctx = Fnv(h, misc, sizeof(misc));

	// main memory, eight bytes at a time
	const u8* ram = Ram(G_BASE & ~RAM_MASK);

	h = kFnvBasis;

	for (u32 i = 0; i < RAM_SIZE; i += 8)
	{
		u64 v;

		memcpy(&v, ram + i, 8);
		h = (h ^ v) * 0x100000001B3ull;
		h ^= h >> 32;
	}

	s.ram = h;

	return s;
}

// Where the runs of a mode end, for the text of a check
const char* Where(BootMode mode)
{
	static char text[BOOT_MODES][64];

	if (mode == BOOT_VBLANKS)
		snprintf(text[mode], sizeof(text[mode]), "vblank %u", kBootVblanks);
	else
		snprintf(text[mode], sizeof(text[mode]), "interrupt %u behind vblank %u", kBootInterrupts, kBootWindow);

	return text[mode];
}

void BootEnd(bool ok)
{
	HelperEnd();
	Vblank(false);
	emu::SetSh4Breakpoints(nullptr, nullptr, 0);
	emu::SetActiveMask(~0u);

	boot_ok = ok;
	boot_abort = false;
	boot_phase = 0;
}

// One run of the booted guest. false: it armed what goes on and wants the next stop. true: the run is over,
// the guest is parked, nothing is armed. The first call of a run does not look at its stop
bool BootRun(const emu::RunState& stop, BootFn fn, BootMode mode)
{
	if (boot_phase == 0)
	{
		emu::SetSh4Breakpoints(nullptr, nullptr, 0);
		emu::SetActiveMask(~0u);
		Vblank(false);

		boot_mode = mode;
		boot_vector = 0;
		boot_void = false;
		boot_phase = 1;
		emu::Reset();
		return false;
	}

	if (boot_phase == 1)
	{
		if (stop.cause != StopCause::Reset || Ctx().pc != kResetVector)
		{
			Check(false, "a run of the booted guest starts parked at the reset vector (\"%s\", pc %08X)", StopCauseName(stop.cause), Ctx().pc);
			BootEnd(false);
			return true;
		}

		memset(Ram(G_BASE & ~RAM_MASK), 0, RAM_SIZE);

		boot_sched0 = sh4_sched_now64();
		boot_vblanks = 0;
		boot_fallback = false;

		Vblank(true);

		boot_phase = 2;
		Go();
		return false;
	}

	const bool vblank = stop.cause == StopCause::Event && stop.breakpointId == kVblankId;

	if (stop.lifetime != Lifetime::Parked
		|| !(vblank || stop.cause == StopCause::Breakpoint || stop.cause == StopCause::Pause || stop.cause == StopCause::Step))
	{
		Check(false, "the booted guest runs to %s (a stop with \"%s\" behind vblank %u, pc %08X)", Where(boot_mode),
			StopCauseName(stop.cause), boot_vblanks, Ctx().pc);
		BootEnd(false);
		return true;
	}

	// a stop of the breakpoint on the interrupt vector
	const bool interrupt = boot_vector != 0 && stop.cause == StopCause::Breakpoint && sh4::MaskAddr(Ctx().pc) == sh4::MaskAddr(boot_vector);
	const bool window = vblank && boot_vblanks + 1 == kBootWindow;

	if (vblank)
	{
		u16 op;

		boot_vblanks++;

		// an event breakpoint cuts a SLEEP wait short and leaves the pc on the SLEEP
		if (mem::Read16(Target::Sh4, Ctx().pc, &op) && op == kSleepOp)
			seen_sleep.Add(Ctx().pc);
	}

	if (interrupt)
		boot_interrupts++;

	if (boot_mode == BOOT_VBLANKS ? (vblank && boot_vblanks == kBootVblanks) : (interrupt && boot_interrupts == kBootInterrupts))
	{
		emu::Sh4EngineInfo info = { };

		boot_state = BootCapture();
		boot_loop = emu::GetSh4Engine(&info) && info.fallbackLoop;

		if (fn)
			fn(BOOT_END, stop);

		BootEnd(true);
		return true;
	}

	boot_armed = false;

	if (window)
	{
		// from here on a run to an interrupt has its stops at the interrupt vector, and none at a vblank
		if (boot_mode == BOOT_INTERRUPTS)
		{
			Vblank(false);

			boot_vector = Ctx().vbr + 0x600;
			boot_interrupts = 0;
		}

		boot_fallback = !boot_bare && SessionFallback();

		BootArm(nullptr, 0);
	}

	if (fn && boot_vblanks >= kBootWindow && !interrupt)
		fn(window ? BOOT_WINDOW : BOOT_STOP, stop);

	if (boot_abort)
	{
		BootEnd(false);
		return true;
	}

	if (!boot_armed)
		Go();

	return false;
}

// The path of the guest: single steps from the stop of the window vblank
void BootProfileFn(BootAt at, const emu::RunState&)
{
	u16 op;

	if (at == BOOT_END || at == BOOT_RESULT)
		return;

	if (at == BOOT_WINDOW)
		prof_n = 0;

	if (prof_n < kBootSteps && mem::Read16(Target::Sh4, Ctx().pc, &op) && BootStep(StepKind::Into))
	{
		prof_pc[prof_n] = Ctx().pc;
		prof_op[prof_n] = op;
		prof_n++;
		return;
	}

	boot_abort = true;
}

bool IsPlainOp(u16 op)
{
	return op != kSleepOp && (OpDesc[op]->type & (WritesPC | WritesSR | WritesFPSCR | Invalid)) == 0;
}

// Sorts the path into the sets of the cases
void BootSets()
{
	static u32 addr[kBootSteps], times[kBootSteps], list[kBootSteps];
	static u16 ops[kBootSteps];
	u32 n = 0, count;

	// the addresses of the path in the order they came up, and how often each came up
	for (u32 i = 0; i < prof_n; i++)
	{
		u32 j = 0;

		while (j < n && addr[j] != prof_pc[i])
			j++;

		if (j == n)
		{
			addr[n] = prof_pc[i];
			ops[n] = prof_op[i];
			times[n++] = 0;
		}

		times[j]++;
	}

	// boot code: the guest does not come back to its entry point
	set_never.Add(kEntry);

	// instructions that end no block, and the one of them that came up most often
	count = 0;
	boot_hot = 0;

	for (u32 j = 0, most = 0; j < n; j++)
	{
		if (!IsPlainOp(ops[j]))
			continue;

		list[count++] = addr[j];

		if (times[j] > most)
		{
			most = times[j];
			boot_hot = addr[j];
		}
	}

	set_plain.Spread(list, count, 4);

	// two instructions in a row, which one block holds: the pair in the middle of the path
	count = 0;

	for (u32 i = 0; i + 1 < prof_n; i++)
	{
		if (prof_pc[i + 1] == prof_pc[i] + 2 && IsPlainOp(prof_op[i]) && IsPlainOp(prof_op[i + 1]))
			list[count++] = prof_pc[i];
	}

	if (count != 0)
	{
		set_pair.Add(list[count / 2]);
		set_pair.Add(list[count / 2] + 2);
	}

	// delay slots: a breakpoint on one stops in front of its branch
	count = 0;

	for (u32 j = 0; j < n; j++)
	{
		if ((OpDesc[ops[j]]->type & Delayslot) != 0)
			list[count++] = addr[j] + 2;
	}

	set_slot.Spread(list, count, 4);

	// SLEEPs: on the path, and where a vblank found the guest waiting
	count = 0;

	for (u32 j = 0; j < n; j++)
	{
		if (ops[j] == kSleepOp)
			list[count++] = addr[j];
	}

	set_sleep.Spread(list, count, 2);
	set_sleep.Spread(seen_sleep.addr, seen_sleep.n, 2);

	// what only the blocks tell: the idle class of the decoder's cost, and the blocks that a size limit ended
	set_idle.none = set_cut.none = true;

#if FEAT_SHREC != DYNAREC_NONE
	if (!IsInterpreter())
	{
		u32 cuts = 0;

		set_cut.none = false;
		set_idle.none = !settings.dynarec.idleskip;
		count = 0;

		for (u32 j = 0; j < n; j++)
		{
			// the cache is looked up for code in main memory
			RuntimeBlockInfo* blk = addr[j] < 0xE0000000 && ((addr[j] >> 26) & 7) == 3 ? bm_GetBlock(addr[j]) : nullptr;
			u16 last, before;

			if (blk == nullptr || sh4::MaskAddr(blk->addr) != sh4::MaskAddr(addr[j]))
				continue;

			// with idleskip every block costs one cycle but the ones of the idle class
			if (settings.dynarec.idleskip && blk->guest_cycles > 1)
				list[count++] = blk->addr + (blk->guest_opcodes > 2 ? 2 : 0);

			// a block that goes on into the next one behind an instruction that ends no block
			if (cuts < 2 && blk->BlockType == BET_StaticJump && blk->BranchBlock == blk->addr + blk->sh4_code_size && blk->guest_opcodes >= 64
				&& mem::Read16(Target::Sh4, blk->addr + blk->sh4_code_size - 2, &last) && IsPlainOp(last)
				&& mem::Read16(Target::Sh4, blk->addr + blk->sh4_code_size - 4, &before) && IsPlainOp(before))
			{
				set_cut.Add(blk->addr + 4);
				set_cut.Add(blk->addr + blk->sh4_code_size);
				set_cut.Add(blk->addr + blk->sh4_code_size + 4);
				cuts++;
			}
		}

		set_idle.Spread(list, count, 4);
	}
#endif

	// the interrupt vector is where the runs that end at an interrupt have their own breakpoint
	for (AddrSet* set : { &set_plain, &set_pair, &set_slot, &set_sleep, &set_idle, &set_cut })
	{
		for (u32 j = 0; j < n; j++)
		{
			if (sh4::MaskAddr(addr[j]) == sh4::MaskAddr(Ctx().vbr + 0x600))
				set->Remove(addr[j]);
		}
	}

	set_all.Add(set_sleep);
	set_all.Add(set_idle);
	set_all.Add(set_slot);
	set_all.Add(set_plain);
	set_all.Add(set_pair);
	set_all.Add(set_cut);

	Note("the path of the booted guest behind vblank %u: %u steps, %u addresses. Taken from it: %u instructions that end no block, %u in a row, %u delay slots, %u SLEEPs, %u in blocks of the idle class, %u in and behind blocks that a size limit ended",
		kBootWindow, prof_n, n, set_plain.n, set_pair.n, set_slot.n, set_sleep.n, set_idle.n, set_cut.n);
}

// The path, taken once in a session, on the backend's own loop. false: armed
bool BootProfile(const emu::RunState& stop)
{
	if (prof_known)
		return true;

	boot_bare = true;

	if (!BootRun(stop, BootProfileFn, BOOT_VBLANKS))
		return false;

	boot_bare = false;
	prof_known = true;

	BootSets();

	return true;
}

// A test: the runs of its cases, one after the other, or with `baseline` the same runs with nothing of the
// cases in them. Every session begins with the run that takes the path. false: armed. true: the test is over
bool BootSuite(const emu::RunState& stop, const char* gate, const BootCase* cases, u32 count, bool baseline)
{
	static u32 index, voids;
	static bool begun;

	if (!BootProfile(stop))
		return false;

	for (; index < count; index++)
	{
		const BootCase& c = cases[index];

		if (!begun)
		{
			if (c.native && SessionFallback())
				continue;

			if (c.set && c.set->n == 0)
			{
				if (!c.set->none)
					Note("the path of the booted guest has no %s", c.missing);

				continue;
			}

			boot_case = &c;
			begun = true;
		}

		if (!BootRun(stop, baseline ? nullptr : c.fn, c.mode))
			return false;

		// The other thread of the case came too late to do anything in this run: the run is made again. Where a
		// reset is not exact that moves the runs behind it away from their baselines, and the session is one
		// that differs
		if (boot_ok && boot_void && voids < kVoidRuns)
		{
			voids++;
			index--;
			continue;
		}

		begun = false;
		voids = 0;

		if (!boot_ok)
			continue;

		if (SessionFallback())
			Check(boot_loop, "booted guest, %s: the fallback loop ran the guest to %s", c.what, Where(c.mode));
		else
			Check(true, "booted guest, %s: the run got to %s", c.what, Where(c.mode));

		if (boot_void)
			Note("booted guest, %s: the other thread came too late in %u runs, the case has no state", c.what, kVoidRuns + 1);
		else
			Note("cmp boot.%s.%u %s", gate, index, Text(boot_state));

		if (!baseline && !boot_void)
			c.fn(BOOT_RESULT, stop);
	}

	index = 0;
	return true;
}

/*
	The cases
*/

// Armed addresses that answer continue: not reached, or reached with the category muted
void BootQuietFn(BootAt at, const emu::RunState& stop)
{
	switch (at)
	{
	case BOOT_WINDOW:
		boot_stops = 0;
		BootArm(*boot_case->set);

		if (boot_case->set != &set_never)
			emu::SetActiveMask(~CategoryBit(Category::Sh4));
		break;

	case BOOT_STOP:
		boot_stops += stop.cause == StopCause::Breakpoint;
		break;

	case BOOT_END:
		break;

	case BOOT_RESULT:
		Check(boot_stops == 0, "booted guest, %s: no stop", boot_case->what);
		break;
	}
}

// A stop and a resume at every arrival; behind kStopBudget of them the addresses are disarmed, in the stop
void BootStopFn(BootAt at, const emu::RunState& stop)
{
	switch (at)
	{
	case BOOT_WINDOW:
		boot_stops = 0;
		BootArm(*boot_case->set);
		break;

	case BOOT_STOP:
		if (stop.cause == StopCause::Breakpoint && ++boot_stops == kStopBudget)
			BootArm(nullptr, 0);
		break;

	case BOOT_END:
		break;

	case BOOT_RESULT:
		Check(boot_stops != 0, "booted guest, stop + resume at %s: %u stops at %u addresses", boot_case->what, boot_stops, boot_case->set->n);
		break;
	}
}

// Pauses that another thread asks for, as fast as they are taken: at the slice ends of the backend loop
void PauseHelper(u32 gen)
{
	while (HelperLive(gen))
	{
		emu::RequestPause();
		helper_ready = true;
	}
}

void BootPauseFn(BootAt at, const emu::RunState& stop)
{
	switch (at)
	{
	case BOOT_WINDOW:
		boot_pauses = 0;
		HelperStart(PauseHelper);
		break;

	case BOOT_STOP:
		if (stop.cause == StopCause::Pause && ++boot_pauses == kStopBudget)
			HelperEnd();
		break;

	case BOOT_END:
		boot_void = boot_pauses == 0;
		break;

	case BOOT_RESULT:
		Check(boot_pauses != 0, "booted guest, pause + resume at slice ends: %u pauses", boot_pauses);
		break;
	}
}

// Service excursions: calls of another thread that arm and disarm addresses while the guest runs. None of
// them stops: the category is muted, and in a session with the fallback, where a muted category would end the
// fallback loop, the address is one that is not reached. On (E), in the park context
void BootToggle()
{
	const bool on = (++boot_calls & 1) != 0 && boot_calls < kCallBudget;

	if (boot_calls == kCallBudget)
		HelperEnd();

	if (on)
		BootArm(SessionFallback() ? set_never : set_all);
	else
		BootArm(nullptr, 0);
}

void CallHelper(u32 gen)
{
	while (HelperLive(gen))
	{
		helper_ready = true;

		emu::Call(emu::CC_TABLE, [gen] {
			if (HelperLive(gen))
				BootToggle();
		});
	}
}

void BootCallFn(BootAt at, const emu::RunState& stop)
{
	switch (at)
	{
	case BOOT_WINDOW:
		boot_calls = boot_stops = 0;

		if (!SessionFallback())
			emu::SetActiveMask(~CategoryBit(Category::Sh4));

		HelperStart(CallHelper);
		break;

	case BOOT_STOP:
		boot_stops += stop.cause == StopCause::Breakpoint;
		break;

	case BOOT_END:
		boot_void = boot_calls == 0;
		break;

	case BOOT_RESULT:
		Check(boot_calls != 0 && boot_stops == 0, "booted guest, addresses armed and disarmed in service excursions: %u calls, no stop", boot_calls);
		break;
	}
}

// B6: an address of a hot block, armed by a call of another thread while the guest runs, disarmed in its tenth stop
void ArmHelper(u32 gen)
{
	const u32 hot = boot_hot;

	// the call is made as soon as the guest runs: this loop does not give the processor away
	while (HelperLive(gen) && emu::GetRunState().lifetime != Lifetime::Running)
		helper_ready = true;

	while (HelperLive(gen))
	{
		int rv = emu::Call(emu::CC_TABLE, [gen, hot] {
			if (HelperLive(gen))
			{
				boot_calls++;
				BootArm(&hot, 1);
			}
		});

		if (rv == 0)
			break;
	}
}

void BootHotFn(BootAt at, const emu::RunState& stop)
{
	switch (at)
	{
	case BOOT_WINDOW:
		boot_calls = boot_stops = 0;
		HelperStart(ArmHelper);
		break;

	case BOOT_STOP:
		if (stop.cause == StopCause::Breakpoint && ++boot_stops == 10)
			BootArm(nullptr, 0);
		break;

	case BOOT_END:
		boot_void = boot_stops < 10;
		break;

	case BOOT_RESULT:
		Check(boot_calls == 1 && boot_stops == 10, "booted guest, %s: armed at %08X by %u call, %u stops", boot_case->what, boot_hot, boot_calls, boot_stops);
		break;
	}
}

// B7: a step-out is asked for in the stop of every vblank, and again where one ended, kOutChain times between two
// vblanks. Each is pending until the function it was asked in returns, a handler included, or until the next vblank
void BootOutFn(BootAt at, const emu::RunState& stop)
{
	switch (at)
	{
	case BOOT_WINDOW:
		boot_outs = boot_ended = boot_chain = 0;
		// no break here by design

	case BOOT_STOP:
		boot_ended += stop.cause == StopCause::Step;

		if (stop.cause == StopCause::Event)
			boot_chain = 0;

		if (boot_chain < kOutChain && BootStep(StepKind::Out))
		{
			boot_chain++;
			boot_outs++;
		}
		break;

	case BOOT_END:
		break;

	case BOOT_RESULT:
		Check(boot_outs != 0, "booted guest, %s: %u asked for, %u ended at a return", boot_case->what, boot_outs, boot_ended);
		break;
	}
}

const BootCase b3_boot[] = {
	{ "an armed address that is not reached", BootQuietFn, &set_never, nullptr, false, BOOT_VBLANKS },
	{ "the addresses of every case below, the category muted", BootQuietFn, &set_all, "instruction", true, BOOT_VBLANKS },
	{ "instructions that end no block", BootStopFn, &set_plain, "instruction that ends no block", false, BOOT_VBLANKS },
	{ "two instructions in a row, in one block", BootStopFn, &set_pair, "two such instructions in a row", false, BOOT_VBLANKS },
	{ "delay slots", BootStopFn, &set_slot, "delayed branch", false, BOOT_VBLANKS },
	{ "SLEEPs", BootStopFn, &set_sleep, "SLEEP", false, BOOT_INTERRUPTS },
	{ "instructions in blocks of the idle class", BootStopFn, &set_idle, "block of the idle class", false, BOOT_VBLANKS },
	{ "blocks that a size limit ended, and the instructions behind them", BootStopFn, &set_cut, "block that a size limit ended", false, BOOT_VBLANKS },
	{ "pauses", BootPauseFn, nullptr, nullptr, false, BOOT_VBLANKS },
	{ "service excursions", BootCallFn, nullptr, nullptr, false, BOOT_VBLANKS },
};

const BootCase b6_boot[] = {
	{ "an address armed by a call while its block is hot, disarmed in its tenth stop", BootHotFn, &set_plain, "instruction that ends no block", false, BOOT_VBLANKS },
};

const BootCase b7_boot[] = {
	{ "step-outs pending from every vblank on", BootOutFn, nullptr, nullptr, false, BOOT_VBLANKS },
};

#define BOOT_TESTS(gate) \
	DBG_SELFTEST_GUEST(sh4bpboot_##gate, "sh4bpboot." #gate) { return BootSuite(stop, #gate, gate##_boot, sizeof(gate##_boot) / sizeof(gate##_boot[0]), false); } \
	DBG_SELFTEST_GUEST(sh4bpbase_##gate, "sh4bpbase." #gate) { return BootSuite(stop, #gate, gate##_boot, sizeof(gate##_boot) / sizeof(gate##_boot[0]), true); }

BOOT_TESTS(b3)
BOOT_TESTS(b6)
BOOT_TESTS(b7)


/*
	B1: where a breakpoint stops, and with what registers
*/
struct B1Case { const char* what; u32 bp; u32 pc; u32 reg; u32 value; };

const B1Case b1_cases[] = {
	{ "the first instruction that runs", G_line, G_line, 0, 0 },
	{ "the middle of a block", G_line_mid, G_line_mid, 1, 3 },
	{ "further on in the same block", G_line_m2, G_line_m2, 2, 12 },
	{ "the slot of a bra: reported at the branch", G_line_slot, G_line_br, 3, 5 },
	{ "the start of a block that a branch enters", G_line_b2, G_line_b2, 3, 8 },
	{ "the slot of a bt/s that is not taken: reported at the branch", G_line_bt_slot, G_line_bt, 4, 15 },
};

SCRIPT(b1) = {
	ResetMachine,
	STEP { case_at = 0; return false; },

	// one case per stop
	STEP {
		if (case_at != 0)
		{
			const B1Case& c = b1_cases[case_at - 1];

			Check(IsBp(stop, 1, c.pc) && R(c.reg) == c.value, "%s: stops with breakpoint 1 at %08X, r%u = %u (pc %08X)", c.what, c.pc,
				c.reg, c.value, Ctx().pc);
			Note("cmp b1.%u %016llX", case_at, (unsigned long long)AllHash());
		}

		if (case_at == sizeof(b1_cases) / sizeof(b1_cases[0]))
			return false;

		LoadGuest();
		Start(G_line);
		Bps({ b1_cases[case_at].bp });
		case_at++;
		b1_at--;		// this step again, at the next stop

		return Go();
	},

	// the first instruction of the booted guest
	ResetMachine,
	STEP {
		Check(stop.cause == StopCause::Reset && Ctx().pc == 0xA0000000, "a reset while parked stays parked, at the reset vector (pc %08X)", Ctx().pc);
		Bps({ kEntry });
		return Go();
	},
	STEP {
		Check(IsBp(stop, 1, kEntry), "a breakpoint on the first instruction of the booted guest stops there (pc %08X)", Ctx().pc);
		Note("cmp b1.boot %016llX", (unsigned long long)IntHash());
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b1, "sh4bp.b1") { RUN_SCRIPT(b1); }


/*
	B2: resume runs the instruction once, the next arrival stops again; hit counts; time at the loop exit
*/
const u32 kPasses = 200;

void StartLoop(std::initializer_list<u32> at)
{
	LoadGuest();
	Start(G_loop);
	R(4) = kPasses;
	R(5) = G_buffer;
	TimedBps(G_loop, at);
	hits = 0;
}

struct B2Case { const char* what; u32 bp; u32 pc; };

const B2Case b2_cases[] = {
	{ "the first instruction of the loop block", G_loop_top, G_loop_top },
	{ "the middle of the loop block", G_loop_mid, G_loop_mid },
	{ "the slot of the loop's bf/s", G_loop_slot, G_loop_br },
};

SCRIPT(b2) = {
	ResetMachine,
	STEP { StartLoop({ G_loop_end }); return Go(); },
	STEP {
		if (Entered(b2_at))
			return true;

		Check(IsBp(stop, 1, G_loop_end), "the loop runs to its end");
		base_time = Time() - t0;
		base_ints = AllHash();
		base_data = DataHash();
		case_at = 0;
		StartLoop({ b2_cases[0].bp, G_loop_end });
		return Go();
	},
	STEP {
		const B2Case& c = b2_cases[case_at];

		if (Entered(b2_at))
			return true;

		if (stop.cause == StopCause::Breakpoint && stop.breakpointId == 1 && Ctx().pc == c.pc)
		{
			hits++;
			b2_at--;
			return Go();
		}

		Check(IsBp(stop, 2, G_loop_end) && hits == kPasses, "%s: %u hits in %u passes, each at %08X", c.what, hits, kPasses, c.pc);
		Check(Time() - t0 == base_time && AllHash() == base_ints && DataHash() == base_data,
			"%s: time and state at the loop exit are those without the breakpoint (%llu cycles)", c.what, (unsigned long long)base_time);

		if (++case_at == sizeof(b2_cases) / sizeof(b2_cases[0]))
			return false;

		StartLoop({ b2_cases[case_at].bp, G_loop_end });
		b2_at--;
		return Go();
	},
	STEP { Note("cmp b2.time.%s %llu", IsInterpreter() ? "interpreter" : "blocks", (unsigned long long)base_time); return false; },
};

DBG_SELFTEST_GUEST(sh4bp_b2, "sh4bp.b2") { RUN_SCRIPT(b2); }


/*
	B3: timing neutrality on the workload, bounded to a number of passes
*/
const u32 kWorkPasses = 400;

void StartWork(bool fpu, std::initializer_list<u32> at)
{
	LoadGuest();
	Start(G_work);
	R(10) = kWorkPasses;
	R(12) = fpu ? 0 : 1;
	R(14) = 0;
	TimedBps(G_work, at);
	hits = 0;
}

bool WorkIsBase(const char* what)
{
	return Check(Ctx().pc == G_work_done && Time() - t0 == base_time && AllHash() == base_ints && DataHash() == base_data,
		"%s: %u stops, time and state at the end are the baseline's", what, hits);
}

SCRIPT(b3) = {
	ResetMachine,
	STEP { StartWork(true, { G_work_done }); return Go(); },
	STEP {
		if (Entered(b3_at))
			return true;

		Check(IsBp(stop, 1, G_work_done) && R(9) == kWorkPasses, "the workload runs its %u passes", kWorkPasses);
		base_time = Time() - t0;
		base_ints = AllHash();
		base_data = DataHash();

		// checks that answer continue: addresses that are never reached
		StartWork(true, { G_work_done, G_line, G_span });
		return Go();
	},
	STEP {
		if (Entered(b3_at))
			return true;

		WorkIsBase("armed addresses that are never reached");

		// Reached addresses with the category muted. Nothing stops this run: when its passes are done the
		// guest spins at g_work_done, where the pause that another thread asks for finds it
		StartWork(true, { G_work_long_mid, G_work_poll_mid, G_work_jsr_slot, G_leaf });
		emu::SetActiveMask(0);

		helper = new std::thread([] {
			std::this_thread::sleep_for(std::chrono::milliseconds(1500));
			emu::RequestPause();
		});

		return Go();
	},
	STEP {
		helper->join();
		delete helper;
		helper = nullptr;

		Check(stop.cause == StopCause::Pause && R(9) == kWorkPasses && AllHash() == base_ints && DataHash() == base_data,
			"reached addresses with the category muted: no stop until the pause, the workload ended as the baseline (pc %08X)", Ctx().pc);
		emu::SetActiveMask(~0u);

		// a stop and a resume at every arrival
		StartWork(true, { G_work_done, G_work_long_mid, G_work_poll_mid, G_work_jsr_slot, G_work_bfs_slot, G_work_long + 448,
			G_work_long_2nd, G_work_ldc });
		return Go();
	},
	STEP {
		if (Entered(b3_at))
			return true;

		if (stop.cause == StopCause::Breakpoint && stop.breakpointId != 1)
		{
			hits++;
			b3_at--;
			return Go();
		}

		WorkIsBase("a stop and a resume at every arrival at seven addresses of the loop");
		Check(hits >= 7 * kWorkPasses, "every one of them stopped in every pass (%u stops)", hits);

		// a step-out that the outermost loop never satisfies
		StartWork(true, { G_work_done });
		return Go();
	},
	STEP {
		// at the first instruction: the time starts, and the step-out is asked for
		entered = true;
		t0 = Time();
		return Step(StepKind::Out);
	},
	STEP {
		Check(IsBp(stop, 1, G_work_done), "a pending step-out does not stop on the calls and returns of the loop");
		WorkIsBase("a step-out pending for the whole run");
		Note("cmp b3.time.%s %llu", IsInterpreter() ? "interpreter" : "blocks", (unsigned long long)base_time);
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b3, "sh4bp.b3") { RUN_SCRIPT(b3); }


/*
	B4: single steps. The trace of a session is compared with the interpreter's by selftest.sh
*/
const u32 kSteps = 20000;

SCRIPT(b4) = {
	ResetMachine,
	STEP {
		LoadGuest();
		Start(G_work);
		R(10) = 0xFFFFFFFF;
		count = 0;
		trace = kFnvBasis;
		return Step(StepKind::Into);
	},
	STEP {
		if (stop.cause == StopCause::Step && ++count < kSteps)
		{
			trace = IntHash(trace);
			b4_at--;
			return Step(StepKind::Into);
		}

		Check(count == kSteps, "%u steps through the workload, each one a stop with the reason step (%u)", kSteps, count);
		Note("cmp b4.work %016llX", (unsigned long long)trace);

		// a delayed branch is one step with its slot
		LoadGuest();
		Start(G_line_br);
		return Step(StepKind::Into);
	},
	STEP {
		Check(IsStep(stop, G_line_b2) && R(3) == 3, "a step on a bra runs its slot and ends at the target");
		Start(G_line_bt);
		R(4) = 1;
		Ctx().sr.T = 0;
		return Step(StepKind::Into);
	},
	STEP {
		Check(IsStep(stop, G_line_end) && R(4) == 2, "a step on a bt/s that is not taken runs its slot and ends behind it");

		// from the entry point of the booted guest
		emu::SetSh4Breakpoints(nullptr, nullptr, 0);
		emu::Reset();
		return true;
	},
	STEP { return Step(StepKind::Into); },
	STEP {
		Check(IsStep(stop, kEntry), "a step on the reios boot trap runs the whole boot and ends at the entry point (pc %08X)", Ctx().pc);
		count = 0;
		trace = kFnvBasis;
		return Step(StepKind::Into);
	},
	STEP {
		if (stop.cause == StopCause::Step && ++count < kSteps)
		{
			trace = IntHash(trace);
			b4_at--;
			return Step(StepKind::Into);
		}

		Check(count == kSteps, "%u steps from the entry point of the booted guest (%u)", kSteps, count);
		Note("cmp b4.boot %016llX", (unsigned long long)trace);
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b4, "sh4bp.b4") { RUN_SCRIPT(b4); }


/*
	B5: step preconditions, the step on SLEEP
*/
struct B5Case { const char* what; u32 pc; u32 op; u32 slot; bool write; const char* why; };

const B5Case b5_cases[] = {
	{ "an odd pc", G_buffer + 1, 0, 0, false, "pc is odd" },
	{ "a pc that cannot be read", 0x10000000, 0, 0, false, "pc not readable" },
	{ "an undefined opcode", G_buffer, 0xFFFD, 0x0009, true, "undefined opcode" },
	{ "a branch in a delay slot", G_buffer, 0xA000, 0xA000, true, "illegal slot instruction" },
	{ "an undefined opcode in a delay slot", G_buffer, 0x000B, 0xFFFD, true, "illegal slot instruction" },
	{ "a trapa in a delay slot", G_buffer, 0x400B, 0xC301, true, "illegal slot instruction" },
};

SCRIPT(b5) = {
	ResetMachine,
	STEP {
		for (const B5Case& c : b5_cases)
		{
			LoadGuest();

			if (c.write)
			{
				u16 op = (u16)c.op, slot = (u16)c.slot;

				memcpy(Ram(c.pc), &op, 2);
				memcpy(Ram(c.pc + 2), &slot, 2);
			}

			Start(c.pc);

			u64 before = AllHash();
			bool ok = true;

			for (StepKind kind : { StepKind::Into, StepKind::Over, StepKind::Out })
			{
				const char* why = nullptr;
				int rv = emu::Step(Target::Sh4, kind, &why);

				ok = ok && rv == kErrCannotStep && why != nullptr && strcmp(why, c.why) == 0;
			}

			Check(ok && AllHash() == before, "a step, step over and step out at %s are refused with -32005 \"%s\", nothing ran", c.what, c.why);
		}

		// SLEEP without an interrupt on the way: the wait runs to the handler's bound
		LoadGuest();
		Start(G_sleep);
		t0 = Time();
		return Step(StepKind::Into);
	},
	STEP {
		u64 slices = (Time() - t0) / SH4_TIMESLICE;

		Check(IsStep(stop, G_sleep) && slices == 1002, "a step on SLEEP with every interrupt masked ends on the SLEEP after %llu slices",
			(unsigned long long)slices);
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b5, "sh4bp.b5") { RUN_SCRIPT(b5); }


/*
	B6: an address armed while the guest runs, by a call that another thread makes, in a block that is hot
*/
SCRIPT(b6) = {
	ResetMachine,
	STEP {
		LoadGuest();
		Start(G_work);
		R(10) = 0xFFFFFFFF;
		hits = 0;

		helper = new std::thread([] {
			std::this_thread::sleep_for(std::chrono::milliseconds(300));

			emu::Call(emu::CC_TABLE, [] {
				u32 id = 1, addr = G_work_long_mid;

				emu::SetSh4Breakpoints(&id, &addr, 1);
			});
		});

		return Go();
	},
	STEP {
		if (helper)
		{
			helper->join();
			delete helper;
			helper = nullptr;

			Check(IsBp(stop, 1, G_work_long_mid) && R(9) > 100, "an address armed while its block was hot stops the guest (pass %u)", R(9));
		}

		if (IsBp(stop, 1, G_work_long_mid) && ++hits < 10)
		{
			b6_at--;
			return Go();
		}

		Check(hits == 10, "it stops at every arrival (%u)", hits);

		// disarmed: the loop runs on to its end, which the debugger moves near
		R(10) = R(9) + 50;
		Bps({ G_line, G_work_done });
		count = R(9);
		return Go();
	},
	STEP {
		Check(IsBp(stop, 2, G_work_done) && R(9) == count + 50, "disarmed, it stops no more: the loop runs its last 50 passes");
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b6, "sh4bp.b6") { RUN_SCRIPT(b6); }


/*
	B7: step over and step out
*/
void StartCalls(std::initializer_list<u32> at)
{
	LoadGuest();
	Start(G_calls);
	Bps(at);
}

SCRIPT(b7) = {
	ResetMachine,

	// step over a jsr
	STEP { StartCalls({ G_calls_jsr }); return Go(); },
	STEP { Bps({ }); sp0 = R(15); return Step(StepKind::Over); },
	STEP {
		Check(IsStep(stop, G_calls_ret) && R(15) == sp0 && R(0) == 17, "step over a jsr stops behind the call, on its stack level, with the result (r0 = %u)", R(0));
		return Step(StepKind::Over);
	},
	STEP {
		Check(IsStep(stop, G_calls_ret + 2) && R(8) == 17, "step over an instruction that is no call is a step");

		// step over a recursive call
		StartCalls({ G_fact_call });
		return Go();
	},
	STEP { Bps({ }); sp0 = R(15); return Step(StepKind::Over); },
	STEP {
		Check(IsStep(stop, G_fact_back) && R(15) == sp0 && R(0) == 6, "step over the recursive call of fact(4) stops behind it on the level of the request, with fact(3) = %u", R(0));

		// step over a trapa
		StartCalls({ G_calls_trapa });
		return Go();
	},
	STEP { Bps({ }); sp0 = R(15); return Step(StepKind::Over); },
	STEP {
		Check(IsStep(stop, G_calls_ret3) && R(15) == sp0 && Ram32(G_trapcnt) == 1, "step over a trapa stops behind it, the handler has run");

		// step out of a leaf
		StartCalls({ G_leaf_mid });
		return Go();
	},
	STEP { Bps({ }); sp0 = R(15); return Step(StepKind::Out); },
	STEP {
		Check(IsStep(stop, G_outer_mid) && R(15) == sp0 && R(0) == 7, "step out of a leaf stops behind the call in its caller");

		// step out of a function that goes on to call a frameless leaf
		StartCalls({ G_outer_body });
		return Go();
	},
	STEP { Bps({ }); sp0 = R(15); return Step(StepKind::Out); },
	STEP {
		Check(IsStep(stop, G_calls_ret) && R(15) == sp0 + 4 && R(0) == 17, "step out of a function that goes on to call a frameless leaf twice stops in its own caller (pc %08X)",
			Ctx().pc);

		// a breakpoint ends a pending step-out
		StartCalls({ G_outer_body, G_leaf });
		return Go();
	},
	STEP { return Step(StepKind::Out); },
	STEP {
		Check(IsBp(stop, 2, G_leaf) && dbg_sh4_stepout == 0, "a breakpoint ends a pending step-out");
		Bps({ G_calls_end });
		return Go();
	},
	STEP {
		Check(IsBp(stop, 1, G_calls_end), "and the return it waited for does not stop afterwards");
		Note("cmp b7.end %016llX", (unsigned long long)IntHash());
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b7, "sh4bp.b7") { RUN_SCRIPT(b7); }


/*
	B8: an address is its mirrors
*/
const u32 b8_forms[] = { 0x0C000000, 0x8C000000, 0xAC000000 };

SCRIPT(b8) = {
	ResetMachine,
	STEP { case_at = 0; return false; },
	STEP {
		if (case_at != 0)
			Check(IsBp(stop, 1, G_line_mid), "a breakpoint given as %08X stops code that runs at %08X", (G_line_mid & 0x03FFFFFF) | b8_forms[case_at - 1], G_line_mid);

		if (case_at == 3)
			return false;

		LoadGuest();
		Start(G_line);
		Bps({ (G_line_mid & 0x03FFFFFF) | b8_forms[case_at] });
		case_at++;
		b8_at--;
		return Go();
	},
	STEP {
		LoadGuest();
		Start((G_line & 0x03FFFFFF) | 0xAC000000);
		Bps({ G_line_slot });
		return Go();
	},
	STEP {
		Check(stop.cause == StopCause::Breakpoint && sh4::MaskAddr(Ctx().pc) == sh4::MaskAddr(G_line_br) && R(3) == 5,
			"a breakpoint given as %08X stops code that runs through the 0xAC mirror (pc %08X)", G_line_slot, Ctx().pc);
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b8, "sh4bp.b8") { RUN_SCRIPT(b8); }


/*
	B9: self-modifying code and an armed page, on the emulator's own write protection
*/
SCRIPT(b9) = {
	ResetMachine,
	STEP {
		LoadGuest();
		Start(G_smc);
		R(5) = G_smc_target;
		R(6) = 0xE002;		// mov #2, r0
		Bps({ G_smc_target_rts, G_smc_end });
		return Go();
	},
	STEP { Check(IsBp(stop, 1, G_smc_target_rts) && R(0) == 1, "the breakpoint hits in the code as it was loaded (r0 = %u)", R(0)); return Go(); },
	STEP { Check(IsBp(stop, 1, G_smc_target_rts) && R(0) == 2, "the guest rewrote the page of the armed address: the breakpoint hits again, in the new code (r0 = %u)", R(0)); return Go(); },
	STEP {
		Check(IsBp(stop, 2, G_smc_end) && R(8) == 1 && R(9) == 2, "and the program ends with both results");

		// once more: the page is now one that holds data
		Start(G_smc);
		R(6) = 0xE003;
		return Go();
	},
	STEP { Check(IsBp(stop, 1, G_smc_target_rts) && R(0) == 2, "a second round starts in the code of the first"); return Go(); },
	STEP { Check(IsBp(stop, 1, G_smc_target_rts) && R(0) == 3, "a second rewrite: the breakpoint hits in the new code (r0 = %u)", R(0)); return Go(); },
	STEP { Check(IsBp(stop, 2, G_smc_end) && R(8) == 2 && R(9) == 3, "and the program ends with both results"); return false; },
};

DBG_SELFTEST_GUEST(sh4bp_b9, "sh4bp.b9") { RUN_SCRIPT(b9); }


/*
	B10: what is owed after a stop inside a block is not run when the resume does not continue there
*/
SCRIPT(b10) = {
	ResetMachine,
	STEP { LoadGuest(); Start(G_line); Bps({ G_line_mid, G_line_end }); return Go(); },
	STEP { Check(IsBp(stop, 1, G_line_mid), "a stop inside a block"); return Go(); },
	STEP {
		Check(IsBp(stop, 2, G_line_end) && R(3) == 8 && R(4) == 16, "a resume in place runs the rest of the block (r3 = %u, r4 = %u)", R(3), R(4));

		LoadGuest();
		Start(G_line);
		Bps({ G_line_mid, G_line_end });
		return Go();
	},
	STEP {
		// the debugger writes the pc
		Start(G_line_b2);
		R(3) = 1;
		return Go();
	},
	STEP {
		Check(IsBp(stop, 2, G_line_end) && R(3) == 1 && R(4) == 9, "the pc written during the stop: the run goes on there, the rest of the old block does not run (r4 = %u)", R(4));

		LoadGuest();
		Start(G_line);
		Bps({ G_line_mid, G_line_end });
		return Go();
	},
	STEP { return Step(StepKind::Into); },
	STEP {
		u32 end, cycles;

		Check(IsStep(stop, G_line_mid + 2) && !dbg_sh4_TakeRemainder(Ctx().pc, &end, &cycles), "a step during the stop: nothing is owed afterwards");

		LoadGuest();
		Start(G_line);
		Bps({ G_line_mid, G_line_end });
		return Go();
	},
	// a reset during the stop
	ResetMachine,
	STEP {
		u32 end, cycles;

		Check(stop.cause == StopCause::Reset && !dbg_sh4_TakeRemainder(G_line_mid, &end, &cycles) && !dbg_sh4_TakeRemainder(Ctx().pc, &end, &cycles),
			"a reset during the stop: nothing is owed afterwards");
		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b10, "sh4bp.b10") { RUN_SCRIPT(b10); }


/*
	B12: the engine a session debugs with. The state of the integer workload under the forced fallback is
	compared with the JIT's by selftest.sh
*/
SCRIPT(b12) = {
	ResetMachine,
	STEP {
		emu::Sh4EngineInfo info = { };

		Check(emu::GetSh4Engine(&info), "the engine is known");
		Note("backend %s, caps %u, fallback %d", info.backend ? info.backend : "", info.caps, info.fallback);

		StartWork(false, { G_work_done, G_work_long_mid });
		return Go();
	},
	STEP {
		if (Entered(b12_at))
			return true;

		if (IsBp(stop, 2, G_work_long_mid))
		{
			emu::Sh4EngineInfo info = { };

			emu::GetSh4Engine(&info);

			if (hits++ == 0)
			{
				if (info.fallback)
					Check((stop.notice & NOTICE_SH4_FALLBACK) != 0 && info.fallbackLoop, "the forced fallback: the stop carries the notice, the fallback loop ran");
				else
					Check((stop.notice & NOTICE_SH4_FALLBACK) == 0 && (info.caps & SH4DBG_NATIVE) != 0 && !info.fallbackLoop, "a native backend: no notice, its own loop ran");
			}

			b12_at--;
			return Go();
		}

		Check(IsBp(stop, 1, G_work_done) && hits == kWorkPasses, "the integer workload with a stop in every pass (%u)", hits);

		if (!IsInterpreter())
			Note("cmp b12.blocks %llu %016llX %016llX", (unsigned long long)(Time() - t0), (unsigned long long)AllHash(), (unsigned long long)DataHash());

		return false;
	},
};

DBG_SELFTEST_GUEST(sh4bp_b12, "sh4bp.b12") { RUN_SCRIPT(b12); }


/*
	B13 and B14: n instructions by stepping against the same n by running
*/
const u32 kCompareSteps = 3000;

bool IsSlotOfBranch(u32 pc)
{
	u16 op;

	return mem::Read16(Target::Sh4, pc - 2, &op) && (OpDesc[op]->type & Delayslot) != 0;
}

void StartCompare(u32 program)
{
	LoadGuest();
	Start(program);
	R(4) = 100000;
	R(10) = 0xFFFFFFFF;
	t0 = Time();
}

// steps: counts the steps and the arrivals at the address the last one ended at
u32 pcs[kCompareSteps + 16];

bool CompareStep(u32& at, u32 program)
{
	pcs[count++] = Ctx().pc;

	// a breakpoint on the slot of a delayed branch stops at the branch: a few more steps, until the end is an
	// address that a breakpoint stops at as itself
	if (count < kCompareSteps || (IsSlotOfBranch(Ctx().pc) && count < kCompareSteps + 16))
	{
		at--;
		return Step(StepKind::Into);
	}

	end_pc = Ctx().pc;
	arrivals = 0;

	for (u32 i = 0; i < count; i++)
		arrivals += pcs[i] == end_pc;

	base_time = Time() - t0;
	base_ints = AllHash();
	base_data = DataHash();

	StartCompare(program);
	Bps({ end_pc });
	hits = 0;
	return Go();
}

bool CompareRun(const emu::RunState& stop, u32& at, const char* what)
{
	if (IsBp(stop, 1, end_pc) && ++hits < arrivals)
	{
		at--;
		return Go();
	}

	Check(hits == arrivals && AllHash() == base_ints && DataHash() == base_data,
		"%s: %u steps and a run to the same instruction (arrival %u at %08X): every register, the FPU registers bit for bit", what, count, arrivals, end_pc);

	u64 run_time = Time() - t0;

	// the interpreter charges every instruction the same, stepped or run
	if (IsInterpreter())
		Check(run_time == base_time, "%s: and the same time (%llu cycles)", what, (unsigned long long)base_time);
	else
		Note("%s: time of the steps %llu, of the run %llu: a step is charged as a block of one instruction", what,
			(unsigned long long)base_time, (unsigned long long)run_time);

	return false;
}

SCRIPT(b13) = {
	ResetMachine,
	STEP { StartCompare(G_work); count = 0; return Step(StepKind::Into); },
	STEP { return CompareStep(b13_at, G_work); },
	STEP { return CompareRun(stop, b13_at, "the workload"); },
	STEP { StartCompare(G_istep); count = 0; return Step(StepKind::Into); },
	STEP { return CompareStep(b13_at, G_istep); },
	STEP { return CompareRun(stop, b13_at, "integer code"); },
};

DBG_SELFTEST_GUEST(sh4bp_b13, "sh4bp.b13") { RUN_SCRIPT(b13); }

SCRIPT(b14) = {
	ResetMachine,
	STEP { StartCompare(G_fstep); count = 0; return Step(StepKind::Into); },
	STEP { return CompareStep(b14_at, G_fstep); },
	STEP { return CompareRun(stop, b14_at, "FPU code"); },
};

DBG_SELFTEST_GUEST(sh4bp_b14, "sh4bp.b14") { RUN_SCRIPT(b14); }

}
