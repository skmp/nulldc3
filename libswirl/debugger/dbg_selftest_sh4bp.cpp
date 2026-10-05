/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of SH4 code breakpoints and steps: sh4bp.b1 to sh4bp.b14, the gates of that mechanism on the
	real park loop and the real machine. There is no sh4bp.b11: that gate compares the build with the debugger
	against the build without, which a session cannot (tests/debugger/sh4bp, debugger.sh4bp.b11).

	    devcast -config Debugger:SelfTest=sh4bp , Debugger:StartPaused=yes <elf>

	The tests are guest tests: each stands where a client stands, arms breakpoints and steps through dbg::emu
	and is handed every stop. Most of them run the guest programs of tests/debugger/sh4bp, which they write
	into main memory themselves; the ones that need the booted guest reset the machine first. A session tests
	the SH4 backend that is configured. What two sessions have to agree on (the interpreter against the JIT,
	the forced fallback against the JIT) is printed as "note: cmp <key> <value>" lines, which
	tests/debugger/sh4bp/selftest.sh compares.

	Every test begins with a reset of the machine, so that nothing of the guest that ran before it is left,
	and the guest programs take no interrupt: they run with SR.IMASK = 15.

	Times are compared as differences between two stops of one run. In a session with the forced fallback
	the loop that runs the guest, and with it the slice counter that tells the time, changes with the
	breakpoints: a run that is timed begins with a breakpoint on its first instruction, and its time is taken
	from that stop on.
*/

#include "dbg_selftest.h"
#include "dbg_sh4.h"
#include "dbg_mem.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_dbg.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/sh4_sched.h"

#include "../../tests/debugger/sh4bp/guest.inc"

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
