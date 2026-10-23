/*
	The gates. Each one names the gate of the design it stands for (B1 to B14) and runs for every engine it
	applies to: the recompiler of the host (the x64 backend on x86-64, the arm64 backend on arm64), the interpreter,
	and the recompiler with the interpreter fallback forced.
*/
#include "gates.h"
#include "machine.h"
#include "debugger/dbg_selftest_sh4bp_guest.inc"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/dyna/ngen.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

//<windows.h> (behind the headers of the tree on Windows) still defines far and near, empty, for 16-bit code
#ifdef far
#undef far
#endif
#ifdef near
#undef near
#endif

using dbg::StopCause;

//hw/sh4/dyna/blockmanager.cpp: every cached block
extern std::set<RuntimeBlockInfo*> all_blocks;

//hw/sh4/dyna/driver.cpp: where the next block is compiled to in the code cache, and where the cache begins
extern u32 LastAddr;
extern u32 LastAddr_min;

//stubs.cpp: how often the recompiler had the host prepare a code region
extern u32 jit_block_prepares;

//hw/sh4/sh4_core.h, whose names for the registers are not wanted here: applies a written FPSCR
void UpdateFPSCR();

static const Engine all_engines[] = { ENGINE_JIT, ENGINE_INTERP, ENGINE_FALLBACK };

#define SR_MD       0x40000000		//privileged, interrupts open
#define SR_MD_MASK  0x400000F0		//privileged, every interrupt masked

/*
	Helpers
*/

//The registers a guest sees
struct Regs
{
	u32 pc;
	u32 r[16];
	u32 pr, gbr, mach, macl, sr, t;
	u32 fpscr, fpul;
	u32 fp[32];

	bool same_int(const Regs& o) const
	{
		return pc == o.pc && memcmp(r, o.r, sizeof(r)) == 0 && pr == o.pr && gbr == o.gbr && mach == o.mach
			&& macl == o.macl && sr == o.sr && t == o.t;
	}

	bool same_all(const Regs& o) const
	{
		return same_int(o) && fpscr == o.fpscr && fpul == o.fpul && memcmp(fp, o.fp, sizeof(fp)) == 0;
	}
};

static Regs regs()
{
	Regs v;

	v.pc = Sh4cntx.pc;
	memcpy(v.r, Sh4cntx.r, sizeof(v.r));
	v.pr = Sh4cntx._pr;
	v.gbr = Sh4cntx.gbr;
	v.mach = Sh4cntx.mac.h;
	v.macl = Sh4cntx.mac.l;
	v.sr = Sh4cntx.sr.status;
	v.t = Sh4cntx.sr.T;
	v.fpscr = Sh4cntx.fpscr.full;
	v.fpul = Sh4cntx.fpul;
	memcpy(v.fp, Sh4cntx.xffr, sizeof(v.fp));

	return v;
}

static u32& R(u32 n)
{
	return Sh4cntx.r[n];
}

//Breakpoints with the ids 1, 2, ... in the order given
static void bps(std::initializer_list<u32> addrs)
{
	std::vector<u32> ids, a;

	for (u32 addr : addrs)
	{
		ids.push_back((u32)ids.size() + 1);
		a.push_back(addr);
	}

	machine_breakpoints(ids.data(), a.data(), (u32)a.size());
}

static bool is_bp(const MachineStop& s, u32 id, u32 pc)
{
	return s.debugger && s.cause == StopCause::Breakpoint && s.id == id && s.pc == pc;
}

static bool is_step(const MachineStop& s, u32 pc)
{
	return s.debugger && s.cause == StopCause::Step && s.pc == pc;
}

//The block that is cached for this address: the number of debugger checks in it. -1: none is cached
static int block_checks(u32 addr)
{
	RuntimeBlockInfo* blk = bm_GetBlock(addr);

	if (blk == nullptr)
		return -1;

	int n = 0;

	for (const shil_opcode& op : blk->oplist)
		n += op.op == shop_dbg_check;

	return n;
}

//Cached blocks whose epilogue reports a call or a return to the debugger
static u32 flow_blocks()
{
	u32 n = 0;

	for (RuntimeBlockInfo* blk : all_blocks)
		n += blk->dbg_flow != 0;

	return n;
}

static const char* str(const MachineState& s)
{
	static char text[4][160];
	static u32 next;

	char* out = text[next++ & 3];

	snprintf(out, sizeof(text[0]), "sched %llu counter %d pc %08X ctx %016llX ram %016llX", (unsigned long long)s.sched,
		s.counter, s.pc, (unsigned long long)s.ctx, (unsigned long long)s.ram);

	return out;
}


/*
	B1: where a breakpoint stops, and with what registers
*/
static void gate_b1()
{
	struct Case { const char* what; u32 start; u32 bp; u32 pc; };

	static const Case cases[] = {
		{ "the first instruction that runs", G_line, G_line, G_line },
		{ "the middle of a block", G_line, G_line_mid, G_line_mid },
		{ "further on in the same block", G_line, G_line_m2, G_line_m2 },
		{ "the slot of a bra: reported at the branch", G_line, G_line_slot, G_line_br },
		{ "the start of a block that a branch enters", G_line, G_line_b2, G_line_b2 },
		{ "the slot of a bt/s that is not taken: reported at the branch", G_line, G_line_bt_slot, G_line_bt },
	};

	const u32 count = sizeof(cases) / sizeof(cases[0]);
	Regs at[ENGINE_COUNT][count];

	for (Engine e : all_engines)
	{
		for (u32 i = 0; i < count; i++)
		{
			machine_reset(e);
			machine_set_pc(cases[i].start);
			bps({ cases[i].bp });

			MachineStop s = machine_resume();

			check(is_bp(s, 1, cases[i].pc), "%s: %s: stops with breakpoint 1 at %08X (stopped at %08X)", engine_name(e),
				cases[i].what, cases[i].pc, s.pc);

			at[e][i] = regs();
		}

		//nothing of the instruction has run, everything in front of it has
		check(at[e][0].r[0] == 0 && at[e][0].r[1] == 0, "%s: no instruction ran in front of the first one", engine_name(e));
		check(at[e][1].r[0] == 1 && at[e][1].r[1] == 3 && at[e][1].r[2] == 0, "%s: mid-block: r0 = 1, r1 = 3, r2 not yet written",
			engine_name(e));
		check(at[e][3].r[3] == 5 && at[e][3].t == 0, "%s: slot breakpoint: the branch and its slot have not run (r3 = 5), T is the compare's",
			engine_name(e));
		check(at[e][5].r[4] == 15, "%s: slot of the bt/s: r4 = 15, the slot has not run", engine_name(e));
	}

	for (u32 i = 0; i < count; i++)
	{
		check(at[ENGINE_JIT][i].same_all(at[ENGINE_INTERP][i]), "%s: the registers of the jit are the interpreter's", cases[i].what);
		check(at[ENGINE_FALLBACK][i].same_all(at[ENGINE_INTERP][i]), "%s: the registers of the fallback are the interpreter's", cases[i].what);
	}

	//R7: a stop shows the cost of what ran. In fast timing a block costs 1 cycle as a whole, and nothing of it
	//is charged until it completes
	machine_reset(ENGINE_JIT);
	machine_set_pc(G_line);
	bps({ G_line_mid });
	machine_resume();
	check(machine_state().counter == SH4_TIMESLICE && machine_state().sched == 0, "jit: time at a stop inside the first block: nothing charged (counter %d)",
		machine_state().counter);

	machine_reset(ENGINE_INTERP);
	machine_set_pc(G_line);
	bps({ G_line_mid });
	machine_resume();
	check(machine_state().counter == SH4_TIMESLICE - 3 * 8, "interpreter: time at that stop: three instructions charged (counter %d)",
		machine_state().counter);

	//with idleskip off the block of nine instructions costs 13 cycles, and three of the nine have run: 4
	for (Engine e : { ENGINE_JIT, ENGINE_FALLBACK })
	{
		settings.dynarec.idleskip = false;
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		machine_resume();
		int shown = machine_state().counter;
		settings.dynarec.idleskip = true;

		check(shown == SH4_TIMESLICE - 4, "%s, idleskip off: a stop behind three of nine instructions shows their share of the block's 13 cycles (counter %d)",
			engine_name(e), shown);
	}

	//an idle-class block: 90 cycles for three instructions, a stop behind the first shows 30
	for (Engine e : { ENGINE_JIT, ENGINE_FALLBACK })
	{
		machine_reset(e);
		machine_set_pc(G_work);
		bps({ G_work_poll, G_work_poll_mid });

		MachineStop s = machine_resume();	//G_work_poll, in the block that falls into the loop
		s = machine_resume();				//G_work_poll_mid, in that block
		s = machine_resume();				//G_work_poll, the start of the loop block
		int at_start = machine_state().counter;
		s = machine_resume();				//G_work_poll_mid, inside it
		int inside = machine_state().counter;

		check(is_bp(s, 2, G_work_poll_mid) && at_start - inside == 30, "%s: a stop behind one of the three instructions of an idle-class block shows 30 of its 90 cycles (%d)",
			engine_name(e), at_start - inside);
	}
}


/*
	B2: resume executes the instruction once, the next arrival stops again; hit counts; time at the loop exit
*/
static void gate_b2()
{
	const u32 passes = 200;

	struct Case { const char* what; u32 bp; u32 pc; };

	static const Case cases[] = {
		{ "the first instruction of the loop block", G_loop_top, G_loop_top },
		{ "the middle of the loop block", G_loop_mid, G_loop_mid },
		{ "the slot of the loop's bf/s", G_loop_slot, G_loop_br },
	};

	MachineState exit_state[ENGINE_COUNT];

	for (Engine e : all_engines)
	{
		//the loop without a breakpoint in it
		machine_reset(e);
		machine_set_pc(G_loop);
		R(4) = passes;
		R(5) = G_buffer;
		bps({ G_loop_end });

		MachineStop s = machine_resume();

		check(is_bp(s, 1, G_loop_end), "%s: the loop runs to its end", engine_name(e));

		MachineState base = machine_state();
		Regs base_regs = regs();

		exit_state[e] = base;

		for (const Case& c : cases)
		{
			machine_reset(e);
			machine_set_pc(G_loop);
			R(4) = passes;
			R(5) = G_buffer;
			bps({ c.bp, G_loop_end });

			u32 hits = 0;
			bool where = true;

			for (;;)
			{
				s = machine_resume();

				if (!s.debugger || s.id != 1)
					break;

				hits++;
				where = where && s.cause == StopCause::Breakpoint && s.pc == c.pc;
			}

			check(is_bp(s, 2, G_loop_end), "%s: %s: the loop ends", engine_name(e), c.what);
			check(hits == passes && where, "%s: %s: %u hits in %u passes, each at %08X", engine_name(e), c.what, hits, passes, c.pc);
			check(machine_state() == base && regs().same_all(base_regs), "%s: %s: time and state at the loop exit are those without the breakpoint (%s)",
				engine_name(e), c.what, str(machine_state()));
		}
	}

	check(exit_state[ENGINE_FALLBACK] == exit_state[ENGINE_JIT], "the fallback charges the loop as the jit does (%s)", str(exit_state[ENGINE_JIT]));
}


/*
	B3: timing neutrality. State at a later slice end with checks that answer continue, with stops and resumes,
	with pauses and service excursions, is the state without any of it.
*/
struct WorkSetup
{
	Engine engine;
	u64 slices;
	bool fpu;
	bool sleep;
};

static void work_start(const WorkSetup& w)
{
	machine_reset(w.engine);
	machine_set_sr(SR_MD);
	machine_set_pc(G_work);

	R(10) = 0xFFFFFFFF;
	R(12) = w.fpu ? 0 : 1;
	R(14) = w.sleep ? 1 : 0;

	knobs.irq_period = w.sleep ? 5 : 0;
	knobs.host_stop_slice = w.slices;
}

//Runs to the host stop and resumes every debugger stop on the way
static MachineState work_run(u64* stops = nullptr, u64* pauses = nullptr)
{
	for (;;)
	{
		MachineStop s = machine_resume();

		if (!s.debugger)
			break;
	}

	if (stops)
		*stops = counters.stops;
	if (pauses)
		*pauses = counters.pauses;

	return machine_state();
}

static void neutrality(const WorkSetup& w, const char* timing)
{
	const char* en = engine_name(w.engine);

	//The fallback loop runs while a breakpoint is active: its baseline has one that is never reached
	const bool fb = w.engine == ENGINE_FALLBACK;
	const u32 never = G_line;

	work_start(w);
	if (fb)
		bps({ never });
	MachineState base = work_run();

	work_start(w);
	if (fb)
		bps({ never });
	MachineState again = work_run();

	check(base == again, "%s %s: two runs without a stop agree (%s)", en, timing, str(base));

	if (w.engine == ENGINE_JIT)
	{
		RuntimeBlockInfo* cut = bm_GetBlock(G_work_long);

		check(cut != nullptr && cut->sh4_code_size == 448 && bm_GetBlock(G_work_long + 448) != nullptr && G_work_long_mid < G_work_long + 448
			&& G_work_long_2nd > G_work_long + 448, "%s %s: the workload has a block that the size limit ended, behind 224 instructions", en, timing);
	}

	check(counters.stray == 0 && Sh4cntx.r[9] > 8, "%s %s: the workload ran (%u passes, %llu interrupts)", en, timing,
		Sh4cntx.r[9], (unsigned long long)counters.irqs);

	if (fb)
		check(machine_used_fallback(), "%s %s: the guest ran on the fallback loop", en, timing);

	struct Case { const char* what; std::initializer_list<u32> at; };

	const Case stop_cases[] = {
		{ "the middle of an ordinary block", { G_work_long_mid } },
		{ "the middle of an idle-class block", { G_work_poll_mid } },
		{ "the SLEEP", { G_work_sleep } },
		{ "the slot of a jsr", { G_work_jsr_slot } },
		{ "the slot of a bf/s, taken and not taken", { G_work_bfs_slot } },
		{ "the slot of a bsr and the call's target", { G_work_call_slot, G_leaf } },
		{ "two breakpoints in one block", { G_work_long + 2, G_work_long_mid } },
		{ "a block that the size limit ended, and the one behind it", { G_work_long + 4, G_work_long_2nd } },
		{ "the first instruction behind the size limit", { G_work_long + 448 } },
		{ "the interrupt handler and its rte", { G_irq, G_irq_rte } },
		{ "an ldc sr and the instruction behind it", { G_work_ldc, G_work_ldc + 2 } },
	};

	//checks that all answer continue: never reached, and reached with the category muted
	{
		work_start(w);
		bps({ never, G_span });
		MachineState s = work_run();
		check(s == base && counters.stops == 0, "%s %s: armed addresses that are never reached: state = baseline", en, timing);
	}

	if (!fb)
	{
		work_start(w);
		bps({ G_work_long_mid, G_work_poll_mid, G_work_jsr_slot, G_leaf, G_irq_rte, G_work_sleep });
		dbg::sh4::SetActiveMask(0);
		MachineState s = work_run();
		check(s == base && counters.stops == 0, "%s %s: six reached addresses, category muted: state = baseline", en, timing);
	}

	for (const Case& c : stop_cases)
	{
		std::vector<u32> at(c.at);

		if (fb)
			at.push_back(never);

		u64 stops;

		work_start(w);

		std::vector<u32> ids;
		for (u32 i = 0; i < at.size(); i++)
			ids.push_back(i + 1);
		machine_breakpoints(ids.data(), at.data(), (u32)at.size());

		MachineState s = work_run(&stops);

		check(s == base && stops != 0, "%s %s: stop + resume at every arrival at %s: %llu stops, state = baseline (%s)", en, timing, c.what,
			(unsigned long long)stops, str(s));
	}

	//pauses, taken at slice ends
	{
		u64 stops, pauses;

		work_start(w);
		if (fb)
			bps({ never });
		knobs.pause_every = 7;
		MachineState s = work_run(&stops, &pauses);
		check(s == base && pauses != 0 && pauses == stops, "%s %s: pause + resume at every 7th slice end: %llu pauses, state = baseline (%s)", en, timing,
			(unsigned long long)pauses, str(s));

		work_start(w);
		if (fb)
			bps({ G_work_long_mid, never });
		else
			bps({ G_work_long_mid });
		knobs.pause_every = 5;
		s = work_run(&stops, &pauses);
		check(s == base && pauses != 0 && stops > pauses, "%s %s: pauses at every 5th slice end and a breakpoint: %llu pauses, %llu breakpoint stops, state = baseline",
			en, timing, (unsigned long long)pauses, (unsigned long long)(stops - pauses));
	}

	//service excursions: a call that changes the armed set while the guest runs
	{
		static bool on;

		on = false;

		work_start(w);
		if (fb)
			bps({ never });
		else
			dbg::sh4::SetActiveMask(0);

		knobs.excursion_every = 3;
		knobs.excursion = [fb]()
		{
			//arms and disarms an address of a hot block; it never stops: muted, or never reached
			on = !on;

			if (fb)
			{
				if (on)
					bps({ G_line, G_span });
				else
					bps({ G_line });
			}
			else
			{
				if (on)
					bps({ G_work_long_mid });
				else
					bps({ });
			}
		};

		MachineState s = work_run();
		check(s == base && counters.excursions != 0 && counters.stops == 0, "%s %s: an address armed and disarmed in a service excursion at every 3rd slice end: %llu excursions, state = baseline (%s)",
			en, timing, (unsigned long long)counters.excursions, str(s));
	}

	//a step-out that never finishes: the outermost loop does not return
	{
		work_start(w);
		if (fb)
			bps({ never });

		MachineStop s = machine_step_out();
		MachineState st = machine_state();

		check(!s.debugger && st == base, "%s %s: a step-out pending for the whole run: no stop, state = baseline (%s)", en, timing, str(st));
	}
}

static void gate_b3()
{
	const WorkSetup jit = { ENGINE_JIT, 3000, true, true };
	const WorkSetup interp = { ENGINE_INTERP, 20000, true, true };
	const WorkSetup fallback = { ENGINE_FALLBACK, 400, true, true };

	neutrality(jit, "fast");
	neutrality(interp, "fast");
	neutrality(fallback, "fast");

	//With idleskip off a block costs one and a half times the sum of its instructions, rounded down, and no more
	//than half a slice: the 224 instructions of the block that the size limit ended cost 224 cycles, not 336.
	//An fp instruction adds nothing to the sum
	settings.dynarec.idleskip = false;
	neutrality(jit, "idleskip off");
	neutrality(fallback, "idleskip off");
	settings.dynarec.idleskip = true;
}


/*
	B4: steps against the interpreter's steps
*/
struct StepTrace
{
	std::vector<u32> pc;
	std::vector<u64> ints;		//hash of the integer registers behind each step
	u64 slices;
};

static u64 int_hash(const Regs& v)
{
	u64 h = 0xCBF29CE484222325ull;

	auto add = [&h](u32 x) { h = (h ^ x) * 0x100000001B3ull; };

	for (u32 x : v.r)
		add(x);
	add(v.pc); add(v.pr); add(v.gbr); add(v.mach); add(v.macl); add(v.sr); add(v.t);

	return h;
}

static StepTrace step_trace(Engine e, u32 start, u32 steps, bool fpu)
{
	StepTrace t;

	machine_reset(e);
	machine_set_pc(start);
	R(10) = 0xFFFFFFFF;
	R(12) = fpu ? 0 : 1;
	R(14) = 0;
	R(4) = 100000;

	for (u32 i = 0; i < steps; i++)
	{
		MachineStop s = machine_step();

		if (!s.debugger || s.cause != StopCause::Step)
			break;

		t.pc.push_back(s.pc);
		t.ints.push_back(int_hash(regs()));
	}

	t.slices = counters.slices;

	return t;
}

static void compare_traces(const char* what, const StepTrace& a, const char* an, const StepTrace& b, const char* bn, u32 steps)
{
	u32 same = 0;

	while (same < a.pc.size() && same < b.pc.size() && a.pc[same] == b.pc[same] && a.ints[same] == b.ints[same])
		same++;

	check(a.pc.size() == steps && b.pc.size() == steps && same == steps,
		"%s: %u steps, pc and integer registers behind every step of the %s are the %s's (%u agree; slice ends crossed: %llu and %llu)",
		what, steps, an, bn, same, (unsigned long long)a.slices, (unsigned long long)b.slices);

	if (same < a.pc.size() && same < b.pc.size())
		note("%s: step %u: %s at %08X, %s at %08X", what, same + 1, an, a.pc[same], bn, b.pc[same]);
}

static void gate_b4()
{
	const u32 steps = 20000;

	StepTrace jit = step_trace(ENGINE_JIT, G_work, steps, true);
	StepTrace interp = step_trace(ENGINE_INTERP, G_work, steps, true);
	StepTrace fallback = step_trace(ENGINE_FALLBACK, G_work, steps, true);

	compare_traces("the workload", jit, "jit", interp, "interpreter", steps);
	compare_traces("the workload", fallback, "fallback", interp, "interpreter", steps);

	//a delayed branch is one step with its slot, taken or not
	for (Engine e : all_engines)
	{
		machine_reset(e);
		machine_set_pc(G_line_br);
		MachineStop s = machine_step();
		check(is_step(s, G_line_b2) && R(3) == 3, "%s: a step on a bra runs its slot and ends at the target", engine_name(e));

		machine_set_pc(G_line_bt);
		R(4) = 1;
		Sh4cntx.sr.T = 0;
		s = machine_step();
		check(is_step(s, G_line_end) && R(4) == 2, "%s: a step on a bt/s that is not taken runs its slot and ends behind it", engine_name(e));

		machine_set_pc(G_line_bt);
		Sh4cntx.sr.T = 1;
		s = machine_step();
		check(is_step(s, G_line_b2) && R(4) == 3, "%s: a step on a bt/s that is taken runs its slot and ends at the target", engine_name(e));
	}

	//a step from a breakpoint executes the instruction; the breakpoint is not in the way and stops the next arrival
	for (Engine e : all_engines)
	{
		machine_reset(e);
		machine_set_pc(G_loop);
		R(4) = 3;
		R(5) = G_buffer;
		bps({ G_loop_mid });

		MachineStop s = machine_resume();
		bool ok = is_bp(s, 1, G_loop_mid);

		s = machine_step();
		ok = ok && is_step(s, G_loop_mid + 2);

		s = machine_resume();
		check(ok && is_bp(s, 1, G_loop_mid) && R(4) == 2, "%s: a step from a breakpoint runs its instruction, a resume then stops at the next arrival",
			engine_name(e));

		//stepping onto an armed instruction is a step, and stepping on from it too
		machine_set_pc(G_loop_top);
		s = machine_step();
		ok = is_step(s, G_loop_top + 2);
		s = machine_step();
		ok = ok && is_step(s, G_loop_mid);
		s = machine_step();
		check(ok && is_step(s, G_loop_mid + 2), "%s: steps onto and off an armed instruction are steps", engine_name(e));
	}
}


/*
	B5: step preconditions, and the step on SLEEP
*/
static void gate_b5()
{
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);

		struct Case { const char* what; u32 pc; u16 op; u16 slot; bool write; const char* why; };

		const Case cases[] = {
			{ "an odd pc", G_buffer + 1, 0, 0, false, "pc is odd" },
			{ "a pc that cannot be read", 0xA0000000, 0, 0, false, "pc not readable" },
			{ "an undefined opcode", G_buffer, 0xFFFD, 0x0009, true, "undefined opcode" },
			{ "a branch in a delay slot", G_buffer, 0xA000, 0xA000, true, "illegal slot instruction" },
			{ "an undefined opcode in a delay slot", G_buffer, 0x000B, 0xFFFD, true, "illegal slot instruction" },
			{ "a trapa in a delay slot", G_buffer, 0x400B, 0xC301, true, "illegal slot instruction" },
			{ "a delay slot that cannot be read", 0x8FFFFFFE, 0xA000, 0, true, "illegal slot instruction" },
		};

		for (const Case& c : cases)
		{
			machine_reset(e);

			if (c.write)
			{
				machine_write16(c.pc, c.op);
				if (c.pc != 0x8FFFFFFE)
					machine_write16(c.pc + 2, c.slot);
			}

			machine_set_pc(c.pc);

			MachineState before = machine_state();
			Regs regs_before = regs();

			bool ok = true;

			for (int kind = 0; kind < 3; kind++)
			{
				int error = 0;
				const char* why = nullptr;

				if (kind == 0)
					machine_step(&error, &why);
				else if (kind == 1)
					machine_step_over(&error, &why);
				else
					machine_step_out(&error, &why);

				ok = ok && error == dbg::kErrCannotStep && why != nullptr && strcmp(why, c.why) == 0;
			}

			check(ok && machine_state() == before && regs().same_all(regs_before) && counters.stops == 0,
				"%s: a step, step over and step out at %s are refused with -32005 \"%s\", nothing ran", en, c.what, c.why);
		}

		//the process is alive, and what is legal steps
		machine_reset(e);
		machine_write16(G_buffer, 0xA000);		//bra to G_buffer + 4
		machine_write16(G_buffer + 2, 0x7001);	//add #1, r0
		machine_set_pc(G_buffer);

		const char* why = "";
		check(dbg::sh4::StepCheck(&why) == 0, "%s: a bra with an add in its slot may be stepped", en);

		MachineStop s = machine_step();
		check(is_step(s, G_buffer + 4) && R(0) == 1, "%s: and the step runs the pair", en);

		//SLEEP without an interrupt: the wait runs to the handler's bound and the pc is back on the SLEEP
		machine_reset(e);
		machine_set_sr(SR_MD);
		machine_set_pc(G_sleep);

		s = machine_step();
		check(is_step(s, G_sleep) && counters.slices >= 1002 && counters.slices <= 1003,
			"%s: a step on SLEEP without an interrupt ends on the SLEEP after %llu slices", en, (unsigned long long)counters.slices);

		//SLEEP with one: the step ends at the handler, which returns behind the SLEEP
		machine_reset(e);
		machine_set_sr(SR_MD);
		machine_set_pc(G_sleep);
		knobs.irq_period = 7;

		s = machine_step();
		check(is_step(s, G_irq) && Sh4cntx.spc == G_sleep_after && counters.slices == 7,
			"%s: a step on SLEEP with an interrupt on the way ends at the handler after %llu slices", en, (unsigned long long)counters.slices);
	}
}


/*
	B6: an address armed in a block that is compiled and hot, while the guest runs; disarmed after some hits
*/
static void gate_b6()
{
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);
		const bool fb = e == ENGINE_FALLBACK;
		const WorkSetup w = { e, fb ? 400u : 2000u, true, true };

		work_start(w);
		if (fb)
			bps({ G_line });
		MachineState base = work_run();

		static int checks_before, checks_armed;
		static bool armed;

		armed = false;
		checks_before = checks_armed = -2;

		work_start(w);
		if (fb)
			bps({ G_line });

		knobs.excursion_every = 50;
		knobs.excursion = [fb]()
		{
			if (armed)
				return;

			//the block of the address has run many times by now
			checks_before = block_checks(G_work_long);

			if (fb)
				bps({ G_work_long_mid, G_line });
			else
				bps({ G_work_long_mid });

			armed = true;
		};

		u32 hits = 0;
		bool where = true;
		MachineStop s;

		for (;;)
		{
			s = machine_resume();

			if (!s.debugger)
				break;

			hits++;
			where = where && is_bp(s, 1, G_work_long_mid);

			if (hits == 1)
				checks_armed = block_checks(G_work_long);

			if (hits == 10)
			{
				//disarmed in the stop
				if (fb)
					bps({ G_line });
				else
					bps({ });
			}
		}

		MachineState st = machine_state();

		check(hits == 10 && where, "%s: an address armed while its block was hot hits: %u stops until it was disarmed", en, hits);
		check(st == base, "%s: and the state at the end is the baseline's (%s)", en, str(st));

		if (e == ENGINE_JIT)
		{
			check(checks_before == 0 && checks_armed == 1 && block_checks(G_work_long) == 0,
				"%s: the cached block had no check, then one, and none again when the address was disarmed (%d, %d, %d)", en,
				checks_before, checks_armed, block_checks(G_work_long));
		}
	}
}


/*
	B7: step over and step out
*/
static void gate_b7()
{
	u32 where[ENGINE_COUNT][12];

	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);
		u32* at = where[e];
		MachineStop s;

		//step over a jsr
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_calls_jsr });
		machine_resume();
		bps({ });
		u32 sp = R(15);
		s = machine_step_over();
		at[0] = s.pc;
		check(is_step(s, G_calls_ret) && R(15) == sp && R(0) == 17, "%s: step over a jsr stops behind the call, on its stack level, with the result (r0 = %u)", en, R(0));

		//step over an ordinary instruction is a step
		s = machine_step_over();
		at[1] = s.pc;
		check(is_step(s, G_calls_ret + 2) && R(8) == 17, "%s: step over an instruction that is no call is a step", en);

		//step over a recursive call: the callee passes the address behind the call on deeper levels first
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_fact_call });
		s = machine_resume();
		bps({ });
		sp = R(15);
		u32 depth_r4 = R(4);
		s = machine_step_over();
		at[2] = s.pc;
		check(is_step(s, G_fact_back) && R(15) == sp && R(0) == 6 && depth_r4 == 3,
			"%s: step over the recursive call of fact(4) stops behind it on the level of the request, with fact(3) = %u", en, R(0));

		//a listed breakpoint inside the callee stops the step over, and the temporary breakpoint is gone with it
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_calls_jsr, G_leaf });
		machine_resume();
		s = machine_step_over();
		bool inner = is_bp(s, 2, G_leaf);
		bps({ G_calls_end });
		s = machine_resume();
		check(inner && is_bp(s, 1, G_calls_end), "%s: a breakpoint inside the call ends the step over; nothing stops behind the call afterwards", en);

		//step over a trapa
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_calls_trapa });
		machine_resume();
		bps({ });
		sp = R(15);
		s = machine_step_over();
		at[3] = s.pc;
		check(is_step(s, G_calls_ret3) && R(15) == sp && machine_read32(G_trapcnt) == 1, "%s: step over a trapa stops behind it, the handler has run", en);

		//a step on a trapa enters the handler
		machine_reset(e);
		machine_set_pc(G_calls_trapa);
		s = machine_step();
		at[4] = s.pc;
		check(is_step(s, G_trap) && Sh4cntx.spc == G_calls_ret3, "%s: a step on a trapa ends at the handler", en);

		//step out of a leaf
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_leaf_mid });
		machine_resume();
		bps({ });
		sp = R(15);
		s = machine_step_out();
		at[5] = s.pc;
		check(is_step(s, G_outer_mid) && R(15) == sp && R(0) == 7, "%s: step out of a leaf stops behind the call in its caller", en);

		//step out of a function that then calls a leaf without a frame: the leaf returns on the level of the request
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_outer_body });
		machine_resume();
		bps({ });
		sp = R(15);
		s = machine_step_out();
		at[6] = s.pc;
		check(is_step(s, G_calls_ret) && R(15) == sp + 4 && R(0) == 17,
			"%s: step out of a function that goes on to call a frameless leaf twice stops in its own caller (at %08X)", en, s.pc);

		//the same, asked for in front of the function's first instruction
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_outer });
		machine_resume();
		bps({ });
		sp = R(15);
		s = machine_step_out();
		at[7] = s.pc;
		check(is_step(s, G_calls_ret) && R(15) == sp, "%s: step out asked for at the entry of that function stops in its caller", en);

		//step out of a call that blocks until three interrupts were served
		machine_reset(e);
		machine_set_sr(SR_MD);
		machine_set_pc(G_waitmain);
		knobs.irq_period = 3;
		bps({ G_wait_loop });
		machine_resume();
		bps({ });
		sp = R(15);
		s = machine_step_out();
		at[8] = s.pc;
		check(is_step(s, G_waitmain_ret) && R(15) == sp + 4 && machine_read32(G_irqcnt) == 3,
			"%s: step out of a call that waits for three interrupts stops in its caller after %u handlers", en, machine_read32(G_irqcnt));

		//step out of an interrupt handler
		machine_reset(e);
		machine_set_sr(SR_MD);
		machine_set_pc(G_waitmain);
		knobs.irq_period = 3;
		bps({ G_irq });
		machine_resume();
		bps({ });
		u32 back = Sh4cntx.spc;
		s = machine_step_out();
		at[9] = s.pc;
		check(is_step(s, back) && (Sh4cntx.sr.status & 0x10000000) == 0, "%s: step out of an interrupt handler stops where the interrupt was taken (%08X)", en, back);

		//a stop for another reason ends a pending step-out
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_outer_body, G_leaf });
		machine_resume();
		s = machine_step_out();
		bool other = is_bp(s, 2, G_leaf);
		bps({ G_calls_end });
		s = machine_resume();
		at[10] = s.pc;
		check(other && is_bp(s, 1, G_calls_end) && dbg_sh4_stepout == 0, "%s: a breakpoint ends a pending step-out; the return it waited for does not stop afterwards", en);

		if (e == ENGINE_JIT)
			check(flow_blocks() == 0 && all_blocks.size() != 0, "%s: and no cached block reports calls and returns any more", en);

		//time: a step out is a run
		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_calls_end });
		machine_resume();
		MachineState calls_plain = machine_state();

		machine_reset(e);
		machine_set_pc(G_calls);
		bps({ G_leaf_mid, G_calls_end });
		machine_resume();
		machine_step_out();
		machine_resume();
		machine_step_out();
		s = machine_resume();
		at[11] = s.pc;
		check(is_bp(s, 2, G_calls_end) && machine_state() == calls_plain, "%s: two step-outs on the way leave time and state as a run has them (%s)", en, str(calls_plain));
	}

	bool same = memcmp(where[ENGINE_JIT], where[ENGINE_INTERP], sizeof(where[0])) == 0;
	bool same_fb = memcmp(where[ENGINE_FALLBACK], where[ENGINE_INTERP], sizeof(where[0])) == 0;

	check(same && same_fb, "the jit, the interpreter and the fallback stop at the same pc in all of these");
}


/*
	B8: an address is its mirrors
*/
static void gate_b8()
{
	static const u32 forms[] = { 0x0C000000, 0x8C000000, 0xAC000000 };

	for (Engine e : all_engines)
	{
		for (u32 given : forms)
		{
			machine_reset(e);
			machine_set_pc(G_line);
			bps({ (G_line_mid & 0x03FFFFFF) | given });

			MachineStop s = machine_resume();

			check(is_bp(s, 1, G_line_mid), "%s: a breakpoint given as %08X stops code that runs at %08X", engine_name(e),
				(G_line_mid & 0x03FFFFFF) | given, G_line_mid);
		}

		//code that runs through another mirror than the breakpoint names
		for (u32 run : forms)
		{
			machine_reset(e);
			machine_set_pc((G_line & 0x03FFFFFF) | run);
			bps({ G_line_slot });

			MachineStop s = machine_resume();

			check(s.debugger && s.cause == StopCause::Breakpoint && ((s.pc ^ G_line_br) & DBG_SH4_ADDR_MASK) == 0 && R(3) == 5,
				"%s: a breakpoint given as %08X stops code that runs at %08X (reported pc %08X)", engine_name(e), G_line_slot,
				(G_line_br & 0x03FFFFFF) | run, s.pc);
		}
	}

	//a block that was compiled through one mirror runs for the others, and reports the address it was compiled for
	machine_reset(ENGINE_JIT);
	machine_set_pc(G_line);
	bps({ G_line_end });
	machine_resume();
	machine_set_pc((G_line & 0x03FFFFFF) | 0xAC000000);
	bps({ G_line_mid });
	MachineStop s = machine_resume();
	check(s.debugger && s.cause == StopCause::Breakpoint && (s.pc & DBG_SH4_ADDR_MASK) == (G_line_mid & DBG_SH4_ADDR_MASK),
		"jit: a jump to another mirror of a cached block stops at the breakpoint (reported pc %08X)", s.pc);
}


/*
	B9: self-modifying code and an armed page
*/
static void gate_b9()
{
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);

		machine_reset(e);
		machine_set_pc(G_smc);
		R(5) = G_smc_target;
		R(6) = 0xE002;		//mov #2, r0
		bps({ G_smc_target_rts, G_smc_end });

		MachineStop s = machine_resume();
		bool first = is_bp(s, 1, G_smc_target_rts) && R(0) == 1;
		bool locked = machine_page_locked(G_smc_target);

		//the guest writes into the page of the armed address, which holds the block that is running
		s = machine_resume();
		bool second = is_bp(s, 1, G_smc_target_rts) && R(0) == 2;

		s = machine_resume();

		check(first && second && is_bp(s, 2, G_smc_end) && R(8) == 1 && R(9) == 2,
			"%s: a guest write into a page with an armed address: the breakpoint hits again in the code as it was rewritten (r0 = 1, then 2)", en);

		if (e == ENGINE_JIT)
		{
			check(locked && !machine_page_locked(G_smc_target), "%s: the page was locked for its blocks and the write went the way of a write fault", en);
			check(block_checks(G_smc_target) == 1, "%s: the block that was compiled again has its check", en);
		}

		//once more: the page now counts as one that holds data, its blocks are compared with memory at their entry
		machine_set_pc(G_smc);
		R(6) = 0xE003;
		s = machine_resume();
		first = is_bp(s, 1, G_smc_target_rts) && R(0) == 2;
		s = machine_resume();
		second = is_bp(s, 1, G_smc_target_rts) && R(0) == 3;
		s = machine_resume();

		check(first && second && is_bp(s, 2, G_smc_end) && R(8) == 2 && R(9) == 3,
			"%s: a second rewrite, found by the entry check of the block: the breakpoint hits in the new code (r0 = 2, then 3)", en);

		//a stop + resume in the block that then rewrites itself: the rest of the block runs from memory as it is
		machine_reset(e);
		machine_set_pc(G_smc);
		R(5) = G_smc_target;
		R(6) = 0xE005;
		bps({ G_smc_write, G_smc_end });
		s = machine_resume();
		first = is_bp(s, 1, G_smc_write);
		s = machine_resume();
		check(first && is_bp(s, 2, G_smc_end) && R(8) == 1 && R(9) == 5, "%s: a stop in front of the instruction that rewrites code: the resume runs it and the new code", en);
	}
}


/*
	B10: what is owed after a stop inside a block is not run when the resume does not continue there
*/
static void gate_b10()
{
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);
		u32 end_pc, cycles;

		//the reference: the block stopped and resumed in place
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		machine_resume();
		MachineStop s = machine_resume();
		check(is_bp(s, 2, G_line_end) && R(3) == 8 && R(4) == 16, "%s: a stop and resume in place runs the rest of the block (r3 = 8, r4 = 16)", en);

		//the pc is written while stopped inside the block
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		machine_resume();
		machine_set_pc(G_line_b2);
		R(3) = 1;
		s = machine_resume();
		check(is_bp(s, 2, G_line_end) && R(3) == 1 && R(4) == 9, "%s: the pc written during the stop: the run goes on there, the rest of the old block does not run (r4 = %u)",
			en, R(4));

		//written to the address it had: what runs is the block at that address, nothing is owed
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		machine_resume();
		machine_set_pc(G_line_mid);
		bool owed = dbg_sh4_TakeRemainder(G_line_mid, &end_pc, &cycles);
		s = machine_resume();
		check(!owed && is_bp(s, 2, G_line_end) && R(4) == 16, "%s: the pc written to the value it had: nothing is owed, and the breakpoint at it does not stop the resume", en);

		//a step during the stop
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		machine_resume();
		machine_step();
		owed = dbg_sh4_TakeRemainder(Sh4cntx.pc, &end_pc, &cycles) || dbg_sh4_TakeRemainder(G_line_mid, &end_pc, &cycles);
		s = machine_resume();
		check(!owed && is_bp(s, 2, G_line_end) && R(4) == 16, "%s: a step during the stop: nothing is owed afterwards", en);

		//a reset or a loaded state during the stop (dbg::emu::OnGuestStateReplaced: OnPcWritten, OnStop)
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		machine_resume();
		dbg::sh4::OnPcWritten();
		dbg::sh4::OnStop(nullptr);
		owed = dbg_sh4_TakeRemainder(G_line_mid, &end_pc, &cycles);
		check(!owed, "%s: a reset or a loaded state during the stop: nothing is owed afterwards", en);

		//a step that a breakpoint stops in front of its instruction: nothing ran, nothing is owed
		machine_reset(e);
		bps({ G_line_mid, G_line_end });
		machine_set_pc(G_line_mid);
		MachineState before = machine_state();
		s = machine_step();
		owed = dbg_sh4_TakeRemainder(G_line_mid, &end_pc, &cycles);
		bool stopped = is_bp(s, 1, G_line_mid) && R(2) == 0 && machine_state() == before;
		s = machine_step();
		check(stopped && !owed && is_step(s, G_line_mid + 2), "%s: a step onto a pc with a breakpoint that the debugger did not stop at is stopped by it: nothing ran, nothing is owed; the next step runs the instruction", en);

		//what is owed is owed once
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		machine_resume();
		owed = dbg_sh4_TakeRemainder(G_line_mid, &end_pc, &cycles);
		bool twice = dbg_sh4_TakeRemainder(G_line_mid, &end_pc, &cycles);

		if (e == ENGINE_INTERP)
			check(!owed, "%s: the interpreter has no blocks: a stop owes nothing", en);
		else
			check(owed && !twice && end_pc == G_line_slot + 2, "%s: the rest of the block is handed out once, with the end of the block (%08X)", en, end_pc);
	}
}


/*
	B12: the interpreter fallback charges by the decoder's blocks
*/
static void gate_b12()
{
	//integer code: the fallback's state is the jit's
	const WorkSetup jit = { ENGINE_JIT, 600, false, true };
	const WorkSetup fallback = { ENGINE_FALLBACK, 600, false, true };

	work_start(jit);
	MachineState base = work_run();

	work_start(fallback);
	bps({ G_line });
	MachineState s = work_run();
	check(machine_used_fallback() && s == base, "integer workload: the fallback loop with a breakpoint that is never reached has the jit's state (%s)", str(s));

	u64 stops;

	work_start(fallback);
	bps({ G_work_long_mid, G_work_poll_mid });
	s = work_run(&stops);
	check(s == base && stops != 0, "integer workload: the fallback loop with %llu stops and resumes has the jit's state", (unsigned long long)stops);

	work_start(fallback);
	bps({ G_work_jsr_slot, G_work_sleep, G_irq_rte, G_work_long_2nd });
	s = work_run(&stops);
	check(s == base && stops != 0, "integer workload: stops at a slot, the SLEEP, the handler's rte and behind a size limit: %llu stops, the jit's state",
		(unsigned long long)stops);

	settings.dynarec.idleskip = false;

	work_start(jit);
	base = work_run();

	work_start(fallback);
	bps({ G_work_long_mid, G_work_poll_mid, G_work_jsr_slot });
	s = work_run(&stops);
	check(s == base && stops != 0, "integer workload, idleskip off: %llu stops in the fallback loop, the jit's state (%s)", (unsigned long long)stops, str(s));

	settings.dynarec.idleskip = true;

	//the loop is only selected while there is work for it
	work_start(fallback);
	dbg::sh4::SetActiveMask(0);
	bps({ G_work_long_mid });
	work_run();
	check(!machine_used_fallback() && !dbg::sh4::FallbackWanted(), "a breakpoint whose category is muted does not select the fallback loop");

	dbg::sh4::SetActiveMask(~0u);
	check(dbg::sh4::FallbackWanted(), "an active breakpoint does");

	//with FPU code the arithmetic is the interpreter's: reported, not a failure
	const WorkSetup jit_fpu = { ENGINE_JIT, 600, true, true };
	const WorkSetup fallback_fpu = { ENGINE_FALLBACK, 600, true, true };

	work_start(jit_fpu);
	base = work_run();

	work_start(fallback_fpu);
	bps({ G_line });
	s = work_run();
	note("workload with FPU code: jit %s", str(base));
	note("workload with FPU code: fallback %s (%s)", str(s), s == base ? "equal" : "different");
	check(s.sched == base.sched && s.counter == base.counter && s.pc == base.pc, "workload with FPU code: the fallback's time and pc are the jit's");
}


/*
	The forced fallback is selected and dropped while the guest runs: the two loops take turns in the slices of
	one run, and the state at a later slice end is that of a run on either of them alone.
*/
struct Rng
{
	u32 s;

	u32 next()
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}

	u32 below(u32 n) { return next() % n; }
};

static Rng sw_rng;
static u32 sw_count;	//excursions made
static u32 sw_wait;		//excursions until the next switch
static u32 sw_mode;
static u32 sw_arm;		//what an excursion of the modes 4 and 5 arms
static u64 sw_switches;	//changes of the loop that runs the guest

enum
{
	SW_NONE,
	SW_SELECT_ONCE,		//the fallback is selected in the first excursion
	SW_DROP_ONCE,		//dropped in the first excursion
	SW_TOGGLE,			//selected and dropped in turns, in every excursion
	SW_ARM_ONCE,		//the first excursion arms sw_arm, which the guest reaches
	SW_RANDOM,			//selected and dropped at slice ends that sw_rng picks, some of the times with an address that the guest reaches
};

//addresses of the integer workload inside its blocks, at their start and on slots
static const u32 sw_pool[] = {
	G_work_long_mid, G_work_long + 4, G_work_long_2nd, G_work_poll_mid, G_work_jsr_slot, G_leaf, G_work_bfs_slot,
	G_work_ldc + 2, G_work_call_slot, G_irq_rte, G_work_poll, G_work_after_call,
};

static void sw_excursion()
{
	const u32 never = G_line;
	const bool wanted = dbg::sh4::FallbackWanted();

	sw_count++;

	switch (sw_mode)
	{
	case SW_SELECT_ONCE:
		if (sw_count == 1)
			bps({ never });
		break;

	case SW_DROP_ONCE:
		if (sw_count == 1)
			bps({ });
		break;

	case SW_TOGGLE:
		if (wanted)
			bps({ });
		else
			bps({ never });
		break;

	case SW_ARM_ONCE:
		if (sw_count == 1)
			bps({ sw_arm });
		break;

	case SW_RANDOM:
		if (sw_wait != 0)
		{
			sw_wait--;
			break;
		}

		sw_wait = sw_rng.below(3);

		if (wanted)
			bps({ });
		else if (sw_rng.below(3) == 0)
			bps({ never, sw_pool[sw_rng.below(sizeof(sw_pool) / sizeof(sw_pool[0]))] });
		else
			bps({ never });
		break;
	}

	if (wanted != dbg::sh4::FallbackWanted())
		sw_switches++;
}

static void sw_start(const WorkSetup& w, u32 mode, u32 every, u32 seed = 1)
{
	work_start(w);

	sw_mode = mode;
	sw_count = 0;
	sw_wait = 0;
	sw_switches = 0;
	sw_rng.s = seed;

	knobs.excursion_every = every;
	knobs.excursion = sw_excursion;
}

static void gate_switch()
{
	const u32 never = G_line;

	for (bool idleskip : { true, false })
	{
		settings.dynarec.idleskip = idleskip;

		const char* timing = idleskip ? "fast" : "idleskip off";

		//once, and in turns, at slice ends
		for (u64 slices : { 600u, 601u, 607u })
		{
			const WorkSetup jit = { ENGINE_JIT, slices, false, true };
			const WorkSetup fb = { ENGINE_FALLBACK, slices, false, true };

			work_start(jit);
			MachineState base = work_run();

			sw_start(jit, SW_TOGGLE, 100);
			MachineState s = work_run();
			check(s == base && counters.excursions >= 5, "%s, %llu slices: the jit with an address armed and disarmed every 100 slices has the state of its run without (%s)",
				timing, (unsigned long long)slices, str(base));

			sw_start(fb, SW_SELECT_ONCE, 100);
			s = work_run();
			check(s == base && machine_used_fallback() && sw_switches == 1, "%s, %llu slices: the fallback selected once, behind 100 slices of the jit's loop: the jit's state (%s)",
				timing, (unsigned long long)slices, str(s));

			sw_start(fb, SW_DROP_ONCE, 100);
			bps({ never });
			s = work_run();
			check(s == base && !machine_used_fallback() && sw_switches == 1, "%s, %llu slices: the fallback dropped once, behind 100 slices of its own: the jit's state (%s)",
				timing, (unsigned long long)slices, str(s));

			sw_start(fb, SW_TOGGLE, 100);
			s = work_run();
			check(s == base && sw_switches >= 5, "%s, %llu slices: the fallback selected and dropped every 100 slices: the jit's state (%s)", timing,
				(unsigned long long)slices, str(s));
		}

		//The time a stop shows is that of the loop it was made in: a stop inside a block of the fallback, in
		//the first slice it runs, shows what the jit's own check shows there
		for (u32 at : { (u32)G_work_long_mid, (u32)G_work_poll_mid, (u32)G_work_jsr_slot })
		{
			const WorkSetup jit = { ENGINE_JIT, 400, false, true };
			const WorkSetup fb = { ENGINE_FALLBACK, 400, false, true };

			sw_arm = at;

			sw_start(jit, SW_ARM_ONCE, 97);
			MachineStop j = machine_resume();
			MachineState js = machine_state();

			sw_start(fb, SW_ARM_ONCE, 97);
			MachineStop f = machine_resume();
			MachineState fs = machine_state();

			check(is_bp(j, 1, f.pc) && is_bp(f, 1, j.pc) && machine_used_fallback() && fs == js,
				"%s: the first stop of a fallback that was selected behind 97 slices of the jit's loop, at %08X: state and time are those of the jit's stop (%s)",
				timing, f.pc, str(fs));

			//and dropped in that stop: the rest of the block runs in the jit's loop
			bps({ });
			MachineState end = work_run();

			work_start(jit);
			MachineState base = work_run();

			check(end == base && !machine_used_fallback(), "%s: dropped in that stop, inside the block: the jit's state at the end (%s)", timing, str(end));
		}

		//at random points: slice ends both ways, and inside a block from the fallback to the jit's loop
		{
			const u64 slices = 3000;
			const WorkSetup jit = { ENGINE_JIT, slices, false, true };
			const WorkSetup fb = { ENGINE_FALLBACK, slices, false, true };

			work_start(jit);
			MachineState base = work_run();

			work_start(fb);
			bps({ never });
			MachineState alone = work_run();
			check(alone == base && machine_used_fallback(), "%s: %llu slices on the fallback alone: the jit's state (%s)", timing, (unsigned long long)slices,
				str(alone));

			for (u32 seed : { 1u, 2463534242u })
			{
				u64 inside = 0, stops = 0;

				sw_start(fb, SW_RANDOM, 1, seed);

				for (;;)
				{
					MachineStop s = machine_resume();

					if (!s.debugger)
						break;

					//a stop inside a block of the fallback
					stops++;

					switch (sw_rng.below(4))
					{
					case 0:
					case 1:
						//the rest of the block runs in the jit's loop
						bps({ });
						sw_switches++;
						inside++;
						break;

					case 2:
						//and here on the fallback
						bps({ never });
						break;

					default:
						break;
					}
				}

				MachineState s = machine_state();

				check(sw_switches >= 1000 && inside >= 50, "%s, seed %u: the loop changed %llu times, at random slice ends and inside %llu blocks (%llu stops)",
					timing, seed, (unsigned long long)sw_switches, (unsigned long long)inside, (unsigned long long)stops);
				check(s == base, "%s, seed %u: the state behind them is that of a run that never changes its loop (%s)", timing, seed, str(s));
			}
		}
	}

	settings.dynarec.idleskip = true;
}


/*
	A delayed branch whose target is its own slot. The slot runs twice: as the slot, and then as the instruction
	the branch arrived at, which is a block start and has a check of its own.
*/
struct OwnSlot
{
	const char* what;
	u16 branch;		//at +4, to +6
	bool t;			//T in front of it
	bool taken;
	u32 r3;			//relative to the program for jmp and jsr
	bool pr;		//pr = the slot
};

static const OwnSlot own_slots[] = {
	{ "bra", 0xAFFF, true, true, 0, false },
	{ "bsr", 0xBFFF, true, true, 0, false },
	{ "bt/s", 0x8DFF, true, true, 0, false },
	{ "bf/s", 0x8FFF, false, true, 0, false },
	{ "braf", 0x0323, true, true, 0xFFFFFFFE, false },
	{ "bsrf", 0x0303, true, true, 0xFFFFFFFE, false },
	{ "jmp", 0x432B, true, true, 6, false },
	{ "jsr", 0x430B, true, true, 6, false },
	{ "rts", 0x000B, true, true, 0, true },
	{ "bt/s that is not taken", 0x8DFF, false, false, 0, false },
	{ "bf/s that is not taken", 0x8FFF, true, false, 0, false },
};

#define OWN_BASE   G_buffer
#define OWN_BRANCH (OWN_BASE + 4)
#define OWN_SLOT   (OWN_BASE + 6)
#define OWN_NEXT   (OWN_BASE + 8)
#define OWN_END    (OWN_BASE + 14)

static void own_start(Engine e, const OwnSlot& f, u32 passes)
{
	machine_reset(e);

	machine_write16(OWN_BASE + 0, 0x7901);				//add #1, r9
	machine_write16(OWN_BASE + 2, f.t ? 0x0018 : 0x0008);	//sett / clrt
	machine_write16(OWN_BRANCH, f.branch);
	machine_write16(OWN_SLOT, 0x7101);					//add #1, r1
	machine_write16(OWN_NEXT, 0x7201);					//add #1, r2
	machine_write16(OWN_BASE + 10, 0x4810);				//dt r8
	machine_write16(OWN_BASE + 12, 0x8BF8);				//bf OWN_BASE
	machine_write16(OWN_END, 0x0009);					//nop
	machine_write16(OWN_END + 2, 0xAFFE);				//bra to itself
	machine_write16(OWN_END + 4, 0x0009);

	R(8) = passes;
	R(3) = (f.branch & 0xF0DF) == 0x400B ? OWN_BASE + f.r3 : f.r3;
	if (f.pr)
		Sh4cntx._pr = OWN_SLOT;

	machine_set_pc(OWN_BASE);
}

struct OwnHit
{
	u32 pc, id;
	Regs regs;
};

static std::vector<OwnHit> own_hits(Engine e, const OwnSlot& f, u32 passes, const std::vector<u32>& at)
{
	std::vector<OwnHit> hits;
	std::vector<u32> ids;

	own_start(e, f, passes);

	for (u32 i = 0; i < at.size(); i++)
		ids.push_back(i + 1);
	machine_breakpoints(ids.data(), at.data(), (u32)at.size());

	knobs.host_stop_slice = 2000;

	for (u32 i = 0; i < passes * 4 + 4; i++)
	{
		MachineStop s = machine_resume();

		if (!s.debugger || s.cause != StopCause::Breakpoint)
			break;

		hits.push_back({ s.pc, s.id, regs() });

		if (s.pc == OWN_END)
			break;
	}

	return hits;
}

static void gate_ownslot()
{
	const u32 passes = 40;

	for (const OwnSlot& f : own_slots)
	{
		//what the rules give per pass, with the last stop at the end
		struct { const char* what; std::vector<u32> at; u32 per_pass; } cases[] = {
			{ "nothing but the end", { OWN_END }, 0 },
			{ "the slot", { OWN_SLOT, OWN_END }, f.taken ? 2u : 1u },
			{ "the branch", { OWN_BRANCH, OWN_END }, 1 },
			{ "the instruction behind the slot", { OWN_NEXT, OWN_END }, 1 },
			{ "the branch, the slot and the instruction behind it", { OWN_BRANCH, OWN_SLOT, OWN_NEXT, OWN_END }, f.taken ? 3u : 2u },
		};

		for (auto& c : cases)
		{
			std::vector<OwnHit> want = own_hits(ENGINE_INTERP, f, passes, c.at);

			//the interpreter's stops are those of the rules: the pair stops at its branch, with the slot's
			//breakpoint as well, and the arrival at the slot's address stops there
			bool rules = want.size() == c.per_pass * passes + 1 && want.back().pc == OWN_END
				&& want.back().regs.r[1] == (f.taken ? 2 : 1) * passes && want.back().regs.r[2] == passes && want.back().regs.r[9] == passes;

			if (c.at.size() == 2 && c.at[0] == OWN_SLOT && want.size() > 2)
			{
				rules = rules && want[0].pc == OWN_BRANCH && want[0].id == 1 && want[0].regs.r[1] == 0;

				if (f.taken)
					rules = rules && want[1].pc == OWN_SLOT && want[1].id == 1 && want[1].regs.r[1] == 1 && want[2].pc == OWN_BRANCH && want[2].regs.r[1] == 2;
				else
					rules = rules && want[1].pc == OWN_BRANCH && want[1].regs.r[1] == 1;
			}

			check(rules, "%s to its own slot, breakpoints on %s: the interpreter stops %u times per pass (%zu stops in %u passes), the slot ran %u times",
				f.what, c.what, c.per_pass, want.size(), passes, want.empty() ? 0 : want.back().regs.r[1]);

			for (Engine e : { ENGINE_JIT, ENGINE_FALLBACK })
			{
				std::vector<OwnHit> got = own_hits(e, f, passes, c.at);

				size_t same = 0;

				while (same < got.size() && same < want.size() && got[same].pc == want[same].pc && got[same].id == want[same].id
					&& got[same].regs.same_all(want[same].regs))
					same++;

				check(got.size() == want.size() && same == want.size(),
					"%s to its own slot, breakpoints on %s: the stops of the %s are the interpreter's, with their registers (%zu stops, %zu agree%s)", f.what,
					c.what, engine_name(e), got.size(), same, e == ENGINE_FALLBACK && !machine_used_fallback() ? ", NOT on the fallback loop" : "");

				if (same < got.size() && same < want.size())
					note("%s, %s: stop %zu at %08X id %u r1 %u, the interpreter's at %08X id %u r1 %u", f.what, engine_name(e), same, got[same].pc, got[same].id,
						got[same].regs.r[1], want[same].pc, want[same].id, want[same].regs.r[1]);
			}
		}

		//a step on the branch runs the pair once and ends on the slot's address, which has run once
		{
			Regs behind[ENGINE_COUNT];
			bool ok = true;

			for (Engine e : all_engines)
			{
				own_start(e, f, passes);
				machine_set_pc(OWN_BRANCH);
				Sh4cntx.sr.T = f.t ? 1 : 0;

				MachineStop s = machine_step();
				behind[e] = regs();

				ok = ok && is_step(s, f.taken ? OWN_SLOT : OWN_NEXT) && R(1) == 1 && R(2) == 0;

				//the next step is the instruction at the slot's address
				s = machine_step();
				ok = ok && (f.taken ? is_step(s, OWN_NEXT) && R(1) == 2 && R(2) == 0 : is_step(s, OWN_NEXT + 2) && R(1) == 1 && R(2) == 1);
			}

			check(ok && behind[ENGINE_JIT].same_all(behind[ENGINE_INTERP]) && behind[ENGINE_FALLBACK].same_all(behind[ENGINE_INTERP]),
				"%s to its own slot: a step on the branch runs its slot once and ends %s, on every engine (jit %08X r1 %u, interpreter %08X r1 %u, fallback %08X r1 %u)",
				f.what, f.taken ? "on the slot's address" : "behind the slot", behind[ENGINE_JIT].pc, behind[ENGINE_JIT].r[1], behind[ENGINE_INTERP].pc,
				behind[ENGINE_INTERP].r[1], behind[ENGINE_FALLBACK].pc, behind[ENGINE_FALLBACK].r[1]);
		}

		//time: the fallback charges the block that starts on the slot's address as the jit does
		for (bool idleskip : { true, false })
		{
			MachineState st[2];

			settings.dynarec.idleskip = idleskip;

			for (int i = 0; i < 2; i++)
			{
				own_start(i == 0 ? ENGINE_JIT : ENGINE_FALLBACK, f, 0);		//2^32 passes
				machine_set_sr(SR_MD);
				knobs.irq_period = 3;
				knobs.host_stop_slice = 200;

				if (i == 1)
					bps({ G_line });

				MachineStop s = machine_resume();

				st[i] = machine_state();

				if (s.debugger || (i == 1 && !machine_used_fallback()))
					st[i].pc = 0;
			}

			settings.dynarec.idleskip = true;

			check(st[0] == st[1] && st[0].pc != 0, "%s to its own slot, %s: the state of the fallback loop behind 200 slices with interrupts is the jit's (%s)", f.what,
				idleskip ? "fast" : "idleskip off", str(st[1]));
		}
	}
}


/*
	Dense arming: every instruction of long straight lines armed, up to the 1024 addresses a session can list. The
	recompiler holds a bounded number of checks per compiled block (ngen_features::DebugCheckBudget): of a block
	with more it compiles the instructions in front of the check that is one too many, and the rest of the block
	runs behind them outside the block cache. Nothing of that is visible to the guest: the block is charged once
	and the slice counter tested behind its last instruction, as without a check.

	The program, written into RAM by the test:

	   top:   n instructions of a line, integer or floating point
	          jsr @r12 ; nop          the leaf of the image
	          mov #3, r1
	   poll:  mov.l @r13, r2          a loop block of the decoder's idle class
	          dt r1
	          bf poll
	          dt r8
	          bt end
	          jmp @r14 ; nop          r14 = top
	   end:   bra end ; nop
*/
struct Dense
{
	u32 top;
	u32 n;
	u32 call;		//the jsr behind the line
	u32 end;
};

static Dense dense_write(u32 top, u32 n, bool fp)
{
	static const u16 int_ops[] = {
		0x7101,		//add #1, r1
		0x221A,		//xor r1, r2
		0x4204,		//rotl r2
		0x332C,		//add r2, r3
		0x3318,		//sub r1, r3
		0x643B,		//neg r3, r4
		0x6549,		//swap.w r4, r5
		0x365E,		//addc r5, r6
		0x1D12,		//mov.l r1, @(8, r13)
		0x676C,		//extu.b r6, r7
		0x52D2,		//mov.l @(8, r13), r2
		0x6077,		//not r7, r0
	};
	static const u16 fp_ops[] = {
		0xF09D,		//fldi1 fr0
		0xF100,		//fadd fr0, fr1
		0xF21C,		//fmov fr1, fr2
		0xF202,		//fmul fr0, fr2
		0xF301,		//fsub fr0, fr3
		0xF44D,		//fneg fr4
		0xF55D,		//fabs fr5
		0xF61C,		//fmov fr1, fr6
		0xF760,		//fadd fr6, fr7
		0xF13D,		//ftrc fr1, fpul
		0xF82D,		//float fpul, fr8
		0xF214,		//fcmp/eq fr1, fr2
	};
	static const u16 tail[] = {
		0x4C0B,		//jsr @r12
		0x0009,
		0xE103,		//mov #3, r1
		0x62D2,		//mov.l @r13, r2
		0x4110,		//dt r1
		0x8BFC,		//bf poll
		0x4810,		//dt r8
		0x8901,		//bt end
		0x4E2B,		//jmp @r14
		0x0009,
		0xAFFE,		//end: bra end
		0x0009,
	};

	for (u32 i = 0; i < n; i++)
		machine_write16(top + i * 2, fp ? fp_ops[i % 12] : int_ops[i % 12]);

	for (u32 i = 0; i < 12; i++)
		machine_write16(top + n * 2 + i * 2, tail[i]);

	Dense d = { top, n, top + n * 2, top + n * 2 + 20 };

	return d;
}

static Dense dense_start(Engine e, u32 top, u32 n, bool fp, u32 passes)
{
	machine_reset(e);

	Dense d = dense_write(top, n, fp);

	R(8) = passes;
	R(12) = G_leaf;
	R(13) = G_data;
	R(14) = d.top;

	machine_set_pc(d.top);

	return d;
}

//A line whose 36 instructions write r0 to r7 and fr0 to fr9, every one of them twice, 18 instructions apart, with
//the tail of dense_write behind it: a check on the 19th instruction is a check in front of which every host register
//holds a guest register it has written, and behind which every one is read again
static Dense flush_line_write(u32 top)
{
	static const u16 group[18] = {
		0x7001, 0x7101, 0x7201, 0x7301, 0x7401, 0x7501, 0x7601, 0x7701,	//add #1, r0 ... add #1, r7
		0xF000, 0xF110, 0xF220, 0xF330, 0xF440, 0xF550, 0xF660, 0xF770,	//fadd fr0, fr0 ... fadd fr7, fr7
		0xF880, 0xF990,													//fadd fr8, fr8, fadd fr9, fr9
	};
	static const u16 tail[] = {
		0x4C0B,		//jsr @r12
		0x0009,
		0xE103,		//mov #3, r1
		0x62D2,		//mov.l @r13, r2
		0x4110,		//dt r1
		0x8BFC,		//bf poll
		0x4810,		//dt r8
		0x8901,		//bt end
		0x4E2B,		//jmp @r14
		0x0009,
		0xAFFE,		//end: bra end
		0x0009,
	};
	const u32 n = 36;

	for (u32 i = 0; i < n; i++)
		machine_write16(top + i * 2, group[i % 18]);

	for (u32 i = 0; i < 12; i++)
		machine_write16(top + n * 2 + i * 2, tail[i]);

	Dense d = { top, n, top + n * 2, top + n * 2 + 20 };

	return d;
}

//The first armed instructions of the line, and the end
static void dense_arm(const Dense& d, u32 armed)
{
	std::vector<u32> ids, at;

	for (u32 i = 0; i < armed; i++)
		at.push_back(d.top + i * 2);
	at.push_back(d.end);

	for (u32 i = 0; i < at.size(); i++)
		ids.push_back(i + 1);

	machine_breakpoints(ids.data(), at.data(), (u32)at.size());
}

struct DenseBlocks
{
	u32 blocks;
	u32 most_checks;	//in one block
	u32 with_rest;		//blocks whose code hands a rest over
	u32 most_bytes;		//host code of one block
};

static DenseBlocks dense_blocks()
{
	DenseBlocks b = { 0, 0, 0, 0 };

	for (RuntimeBlockInfo* blk : all_blocks)
	{
		u32 checks = 0;

		for (const shil_opcode& op : blk->oplist)
			checks += op.op == shop_dbg_check;

		b.blocks++;
		b.most_checks = std::max(b.most_checks, checks);
		b.with_rest += blk->dbg_rest_pc != 0;
		b.most_bytes = std::max(b.most_bytes, blk->host_code_size);
	}

	return b;
}

//The time of a run with interrupts to a slice end, every debugger stop resumed.
//cache_left: not 0, the run begins with that many bytes of the code cache free
static MachineState dense_timed(Engine e, u32 top, u32 n, bool fp, u32 armed, bool muted, u64 slices, u64* stops, DenseBlocks* blocks = nullptr,
	u32 cache_left = 0)
{
	Dense d = dense_start(e, top, n, fp, 0);		//2^32 passes

	if (cache_left)
		LastAddr = CODE_SIZE - cache_left;

	machine_set_sr(SR_MD);
	knobs.irq_period = 3;
	knobs.host_stop_slice = slices;

	if (armed)
		dense_arm(d, armed);
	if (muted)
		dbg::sh4::SetActiveMask(0);

	MachineState s = work_run(stops);

	if (blocks)
		*blocks = dense_blocks();

	return s;
}

//The heap of the process: the bytes in use, and the bytes the C library holds for them (not HeapSize, a function
//of Windows)
struct HeapUse
{
	s64 in_use;
	s64 held;
};

//false: this C library does not tell (nor does it under a sanitizer that has its own allocator)
static bool heap_size(HeapUse* h)
{
	h->in_use = h->held = 0;

#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 33)
	struct mallinfo2 m = mallinfo2();

	h->in_use = (s64)m.uordblks;
	h->held = (s64)m.arena + (s64)m.hblkhd;
#endif
#endif

	return h->held != 0;
}

static void gate_dense()
{
	ngen_features features = { };
	rdv_ngen->GetFeatures(&features);

	check(features.DebugChecks && features.DebugCheckBudget != 0 && features.DebugCheckBudget < 224, "the recompiler compiles checks, %u of them into one block",
		features.DebugCheckBudget);

	//a backend without a bound is tested as one that has this one
	const u32 budget = features.DebugCheckBudget ? features.DebugCheckBudget : 56;

	//what a check adds to the host code of a block
	{
		u32 bytes[3];
		const u32 armed[3] = { 0, 1, budget };

		for (bool fp : { false, true })
		{
			for (u32 i = 0; i < 3; i++)
			{
				Dense d = dense_start(ENGINE_JIT, 0x8C020000, 200, fp, 1);

				dense_arm(d, armed[i]);
				dbg::sh4::SetActiveMask(0);
				knobs.host_stop_slice = 3;
				machine_resume();

				RuntimeBlockInfo* blk = bm_GetBlock(d.top);

				bytes[i] = blk ? blk->host_code_size : 0;

				check(blk != nullptr && blk->dbg_rest_pc == 0 && block_checks(d.top) == (int)armed[i], "%s line: the block with %u armed instructions is whole and has %u checks",
					fp ? "fp" : "integer", armed[i], armed[i]);
			}

			note("%s line, host code of a block of 200 instructions: %u bytes; with one check %u more; with %u checks on its first instructions %u more, %u a check",
				fp ? "fp" : "integer", bytes[0], bytes[1] - bytes[0], budget, bytes[2] - bytes[0], (bytes[2] - bytes[0]) / budget);

			check(bytes[2] > bytes[0] && (bytes[2] - bytes[0]) <= 16 * 1024, "%s line: %u checks add %u bytes of host code to their block, within the 16 KiB that the backend states",
				fp ? "fp" : "integer", budget, bytes[2] - bytes[0]);
		}
	}

	//The flush of a check at its most: every host register written to the context in front of it and loaded again
	//behind it (flush_line_write). One such check adds the call, both halves of the flush and the tail of the
	//checks to its block; the budget's worth of them, with one tail, has to stay within the 16 KiB that the backend
	//states. The register set is the calling convention's: 6 general and 4 xmm registers with System V, 8 and 10
	//with Windows' (x64_regalloc.h), so the budget of one is no bound for the other; on arm64 8 general and 8 FP
	//registers (arm64_regalloc.h)
	{
		u32 bytes[2];

		for (u32 i = 0; i < 2; i++)
		{
			machine_reset(ENGINE_JIT);

			Dense d = flush_line_write(0x8C020000);

			R(8) = 1;
			R(12) = G_leaf;
			R(13) = G_data;
			R(14) = d.top;
			machine_set_pc(d.top);

			std::vector<u32> ids, at;
			if (i)
				at.push_back(d.top + 18 * 2);
			at.push_back(d.end);
			for (u32 k = 0; k < at.size(); k++)
				ids.push_back(k + 1);
			machine_breakpoints(ids.data(), at.data(), (u32)at.size());

			dbg::sh4::SetActiveMask(0);
			knobs.host_stop_slice = 3;
			machine_resume();

			RuntimeBlockInfo* blk = bm_GetBlock(d.top);

			bytes[i] = blk ? blk->host_code_size : 0;

			check(blk != nullptr && blk->dbg_rest_pc == 0 && block_checks(d.top) == (int)i, "a line that writes every register twice: the block with %u armed instructions is whole and has %u checks",
				i, i);
		}

		const u32 one = bytes[1] - bytes[0];
#if defined(_WIN32)
		const u32 tail = 11;	//the third argument is in r8d: its mov has a REX prefix
#elif HOST_CPU == CPU_ARM64
		const u32 tail = 12;	//the end of the block into w2 (movz, movk) and the branch to the check stub
#else
		const u32 tail = 10;
#endif

		note("a check between two writes of every register adds %u bytes to its block: the call, the flush both ways and the tail of %u bytes", one, tail);

		check(one > tail && budget * (one - tail) + tail <= 16 * 1024, "%u such checks add %u bytes of host code to their block, within the 16 KiB that the backend states",
			budget, budget * (one - tail) + tail);
	}

	struct Layout { const char* what; u32 top; bool fp; };
	const Layout layouts[] = {
		{ "integer line in one page", 0x8C020000, false },
		{ "integer line across pages", 0x8C020C00, false },
		{ "fp line in one page", 0x8C020000, true },
		{ "fp line across pages", 0x8C020C00, true },
	};

	const u32 n = dbg::kMaxBreakpoints - 1;		//with the end: every address a session can list

	for (const Layout& l : layouts)
	{
		//every arrival stops: the stops are the interpreter's, with their registers
		{
			const u32 passes = 2;
			std::vector<OwnHit> hits[ENGINE_COUNT];
			DenseBlocks blocks = { 0, 0, 0, 0 };

			for (Engine e : { ENGINE_INTERP, ENGINE_JIT, ENGINE_FALLBACK })
			{
				Dense d = dense_start(e, l.top, n, l.fp, passes);

				dense_arm(d, n);
				knobs.host_stop_slice = 100000;

				for (u32 i = 0; i < passes * n + 8; i++)
				{
					MachineStop s = machine_resume();

					if (!s.debugger || s.cause != StopCause::Breakpoint)
						break;

					hits[e].push_back({ s.pc, s.id, regs() });

					if (s.pc == d.end)
						break;
				}

				if (e == ENGINE_JIT)
					blocks = dense_blocks();
			}

			const std::vector<OwnHit>& want = hits[ENGINE_INTERP];

			check(want.size() == passes * n + 1 && want.back().id == n + 1, "%s, %u addresses armed: the interpreter stops at every instruction of the line in %u passes, and at the end (%zu stops)",
				l.what, n + 1, passes, want.size());

			for (Engine e : { ENGINE_JIT, ENGINE_FALLBACK })
			{
				size_t same = 0;

				while (same < hits[e].size() && same < want.size() && hits[e][same].pc == want[same].pc && hits[e][same].id == want[same].id
					&& hits[e][same].regs.same_all(want[same].regs))
					same++;

				check(hits[e].size() == want.size() && same == want.size(), "%s, %u addresses armed: the stops of the %s are the interpreter's, with their registers (%zu stops, %zu agree)",
					l.what, n + 1, engine_name(e), hits[e].size(), same);
			}

			check(blocks.most_checks == budget && blocks.with_rest != 0, "%s: no cached block has more than %u checks (%u), and the code of %u blocks ends in front of their rest",
				l.what, budget, blocks.most_checks, blocks.with_rest);
		}

		//every check answers continue: the time is that of the run with nothing armed
		for (bool idleskip : { true, false })
		{
			settings.dynarec.idleskip = idleskip;

			const char* timing = idleskip ? "fast" : "idleskip off";
			const u64 slices = 45;
			u64 stops;
			DenseBlocks blocks;

			MachineState base = dense_timed(ENGINE_JIT, l.top, n, l.fp, 0, false, slices, &stops);
			u32 passes = 0 - R(8);

			MachineState s = dense_timed(ENGINE_JIT, l.top, n, l.fp, n, true, slices, &stops, &blocks);

			check(s == base && stops == 0 && blocks.with_rest != 0 && passes >= 8,
				"%s, %s: %u addresses armed and muted, %u passes with interrupts: the state behind %llu slices is that of the run with nothing armed (%s)", l.what, timing,
				n + 1, passes, (unsigned long long)slices, str(s));

			if (idleskip)
			{
				base = dense_timed(ENGINE_INTERP, l.top, n, l.fp, 0, false, 400, &stops);
				s = dense_timed(ENGINE_INTERP, l.top, n, l.fp, n, true, 400, &stops);

				check(s == base && stops == 0, "%s: the same on the interpreter, behind 400 slices (%s)", l.what, str(s));
			}
		}

		settings.dynarec.idleskip = true;
	}

	//The rest of a block is compiled every time the block runs. A compile gives back what it takes from the heap:
	//the run that repeats one leaves the heap as it found it
	{
		const u64 slices = 45;
		u64 stops;
		HeapUse before, after;

		settings.dynarec.idleskip = false;

		dense_timed(ENGINE_JIT, 0x8C020000, n, true, n, true, slices, &stops);

		const bool told = heap_size(&before);

		dense_timed(ENGINE_JIT, 0x8C020000, n, true, n, true, slices, &stops);

		const u32 passes = 0 - R(8);

		heap_size(&after);

		settings.dynarec.idleskip = true;

		if (told)
			check(passes >= 8 && after.in_use - before.in_use <= 64 * 1024 && after.held - before.held <= 1024 * 1024,
				"fp line in one page, idleskip off: %u addresses armed and muted, the run of %u passes made again: the heap is as large behind it as in front of it (in use %+lld bytes, held %+lld bytes)",
				n + 1, passes, (long long)(after.in_use - before.in_use), (long long)(after.held - before.held));
		else
			note("the size of the heap is not known here: the run that is made again is not measured");
	}

	//A stop and a resume at every arrival, in a block with more armed instructions than its code has checks: the
	//stops are in the code of the block and in every part of its rest
	for (bool fp : { false, true })
	{
		const u32 top = 0x8C020000;
		const u32 line = 3 * budget - 18;
		const char* kind = fp ? "fp" : "integer";

		for (bool idleskip : { true, false })
		{
			settings.dynarec.idleskip = idleskip;

			//fp instructions cost nothing: with idleskip off a pass of the fp line is a few cycles
			const u64 slices = fp && !idleskip ? 3 : 16;
			const char* timing = idleskip ? "fast" : "idleskip off";
			u64 stops;
			DenseBlocks blocks;

			MachineState base = dense_timed(ENGINE_JIT, top, line, fp, 0, false, slices, &stops);
			u32 passes = 0 - R(8);

			MachineState s = dense_timed(ENGINE_JIT, top, line, fp, line, false, slices, &stops, &blocks);

			check(s == base && stops >= (u64)line * (passes - 1) && blocks.with_rest != 0 && blocks.most_checks == budget,
				"%s line of %u armed instructions in one block, %s: %llu stops and resumes in %u passes with interrupts, the state behind %llu slices is that of the run with nothing armed (%s)",
				kind, line, timing, (unsigned long long)stops, passes, (unsigned long long)slices, str(s));

			s = dense_timed(ENGINE_FALLBACK, top, line, fp, line, false, slices, &stops);

			check(s == base && machine_used_fallback() && stops >= (u64)line * (passes - 1), "%s line, %s: the same stops on the fallback loop, the jit's state (%s)", kind, timing, str(s));
		}

		settings.dynarec.idleskip = true;

		//The block ends with a call. Its code does not hold the end: the rest reports the call to a step-out
		//that is pending, and the leaf's return is then the return of something the level of the request
		//called. A report that was lost would end the step-out behind the call
		for (Engine e : all_engines)
		{
			Dense d = dense_start(e, top, line, fp, 3);

			dense_arm(d, line);

			MachineStop s;

			for (u32 i = 0; i < line; i++)
				s = machine_resume();

			bool ok = is_bp(s, line, d.call - 2);

			//asked for in front of the last instruction of the line, inside the rest of the block
			s = machine_step_out();

			check(ok && is_bp(s, 1, d.top) && R(8) == 2, "%s line, %s: a step-out asked for in the rest of the block that ends with a call is not ended by the callee's return (%s at %08X)",
				kind, engine_name(e), s.debugger && s.cause == StopCause::Step ? "a step stop" : "a stop", s.pc);
		}

		//a step-out that stays pending, with every check answering continue
		{
			const u64 slices = 16;
			u64 stops;

			MachineState base = dense_timed(ENGINE_JIT, top, line, fp, 0, false, slices, &stops);

			Dense d = dense_start(ENGINE_JIT, top, line, fp, 0);

			machine_set_sr(SR_MD);
			knobs.irq_period = 3;
			knobs.host_stop_slice = slices;
			dense_arm(d, line);
			dbg::sh4::SetActiveMask(0);

			MachineStop s = machine_step_out();
			MachineState st = machine_state();

			check(!s.debugger && st == base, "%s line: a step-out pending for the whole run, %u addresses armed and muted: the state of the run with nothing armed (%s)",
				kind, line + 1, str(st));
		}
	}

	//The code cache. A block is compiled when 16 KiB of the cache are free, and its code can be more than
	//that: the backend gives it up, the cache is emptied and the block compiled again
	{
		//a line of double precision arithmetic: every instruction is a call, around which the System V backend
		//saves xmm8-xmm11. On Windows the callee keeps xmm6-xmm15 and such a call is a few bytes: there the line is
		//of ftrv, about a hundred bytes of code each. On arm64 the callee keeps the registers of the blocks too, and
		//the most code an instruction that costs no cycles compiles to is ftrv's, 52 bytes: there the line is of ftrv
		const u32 top = 0x8C020000;
		const u32 n = 500;
		const u64 slices = 12;

		auto doubles = [&](u32 cache_left, u32 armed, u32* most_bytes)
		{
			machine_reset(ENGINE_JIT);

#if defined(_WIN32)
			static const u16 ops[] = { 0xF1FD /*ftrv xmtrx, fv0*/, 0xF5FD /*ftrv xmtrx, fv4*/, 0xF9FD /*ftrv xmtrx, fv8*/, 0xFDFD /*ftrv xmtrx, fv12*/ };
			const u32 fpscr = 0x00040001;	//single precision
#elif HOST_CPU == CPU_ARM64
			static const u16 ops[] = { 0xF1FD /*ftrv xmtrx, fv0*/, 0xF5FD /*ftrv xmtrx, fv4*/, 0xF9FD /*ftrv xmtrx, fv8*/, 0xFDFD /*ftrv xmtrx, fv12*/ };
			const u32 fpscr = 0x00040001;	//single precision
#else
			static const u16 ops[] = { 0xF200 /*fadd dr0, dr2*/, 0xF422 /*fmul dr2, dr4*/, 0xF601 /*fsub dr0, dr6*/, 0xF843 /*fdiv dr4, dr8*/ };
			const u32 fpscr = 0x000C0001;	//double precision, pairs
#endif

			for (u32 i = 0; i < n; i++)
				machine_write16(top + i * 2, ops[i % 4]);

			Dense d = dense_write(top + n * 2, 8, false);

			d.top = top;
			d.n = n;

			R(12) = G_leaf;
			R(13) = G_data;
			R(14) = top;

			Sh4cntx.fpscr.full = fpscr;
			Sh4cntx.old_fpscr = Sh4cntx.fpscr;
			UpdateFPSCR();

			machine_set_pc(top);
			machine_set_sr(SR_MD);
			knobs.irq_period = 3;
			knobs.host_stop_slice = slices;

			if (armed)
			{
				dense_arm(d, armed);
				dbg::sh4::SetActiveMask(0);
			}

			if (cache_left)
				LastAddr = CODE_SIZE - cache_left;

			MachineStop s = machine_resume();
			MachineState st = machine_state();

			*most_bytes = dense_blocks().most_bytes;

			if (s.debugger)
				st.pc = 0;

			return st;
		};

		u32 bytes, bytes_left, bytes_armed;

		MachineState base = doubles(0, 0, &bytes);

		check(bytes > 16 * 1024 + 2048, "a block of %u %s instructions has %u bytes of host code, more than the 16 KiB that a compile waits for", n,
#if defined(_WIN32) || HOST_CPU == CPU_ARM64
			"ftrv",
#else
			"double precision",
#endif
			bytes);

		//room for the compile to begin, not for the code
		MachineState s = doubles(16 * 1024 + 2048, 0, &bytes_left);

		check(s == base && bytes_left == bytes, "compiled with %u bytes of the code cache left: the cache is emptied for it, the state behind %llu slices is that of the run in an empty cache (%s)",
			16 * 1024 + 2048, (unsigned long long)slices, str(s));

		s = doubles(16 * 1024 + 2048, budget + 40, &bytes_armed);

		check(s == base, "the same with %u of its instructions armed and muted (%s)", budget + 40, str(s));

		//Room for the code of the block's first instructions and for a compile to begin behind it, not for the
		//code of the rest, which is compiled outside the block cache
		const u32 first_part = bytes / n * (budget + 8);

		s = doubles(16 * 1024 + 2048 + first_part, budget + 40, &bytes_armed);

		check(s == base && first_part < 14 * 1024 && bytes - first_part > 18 * 1024 + 2048,
			"and with %u bytes left, which the rest of the block does not fit into when it is compiled (%u bytes of the block's %u are behind its first %u instructions) (%s)",
			16 * 1024 + 2048 + first_part, bytes - first_part, bytes, budget + 8, str(s));
	}

	//A code cache that has no room for the code of a block when it is empty: the block is made of fewer
	//instructions. That moves block ends, and time with them; what the guest computes stays.
	{
		const u32 top = 0x8C020000;
		const u32 line = 600;
		const u32 passes = 2;
		const u32 room = 2600;

		for (bool fp : { false, true })
		{
			std::vector<OwnHit> hits[2];
			u32 most_bytes = 0;

			for (int i = 0; i < 2; i++)
			{
				Dense d = dense_start(i == 0 ? ENGINE_INTERP : ENGINE_JIT, top, line, fp, passes);

				const u32 cache_begin = LastAddr_min;

				if (i == 1)
					LastAddr_min = LastAddr = CODE_SIZE - room;

				//every eighth instruction of the line, and the end
				std::vector<u32> ids, at;

				for (u32 k = 0; k < line; k += 8)
					at.push_back(d.top + k * 2);
				at.push_back(d.end);

				for (u32 k = 0; k < at.size(); k++)
					ids.push_back(k + 1);

				machine_breakpoints(ids.data(), at.data(), (u32)at.size());

				knobs.host_stop_slice = 100000;

				for (u32 k = 0; k < passes * line; k++)
				{
					MachineStop s = machine_resume();

					if (!s.debugger || s.cause != StopCause::Breakpoint)
						break;

					hits[i].push_back({ s.pc, s.id, regs() });

					if (i == 1)
						most_bytes = std::max(most_bytes, dense_blocks().most_bytes);

					if (s.pc == d.end)
						break;
				}

				LastAddr_min = cache_begin;
			}

			size_t same = 0;

			while (same < hits[0].size() && same < hits[1].size() && hits[1][same].pc == hits[0][same].pc && hits[1][same].id == hits[0][same].id
				&& hits[1][same].regs.same_all(hits[0][same].regs))
				same++;

			check(hits[0].size() == passes * (line / 8) + 1 && hits[1].size() == hits[0].size() && same == hits[0].size() && most_bytes != 0 && most_bytes <= room,
				"%s line of %u instructions in a code cache of %u bytes: the jit runs it in blocks of at most %u bytes of host code, its stops are the interpreter's with their registers (%zu stops, %zu agree)",
				fp ? "fp" : "integer", line, room, most_bytes, hits[1].size(), same);
		}
	}
}


/*
	B13 and B14: n instructions by stepping against the same n by running
*/

//Steps n times from the start; says where that ends and how many of the steps ended there.
//A breakpoint on the slot of a delayed branch stops at the branch: a few more steps are made until the end is
//an address that a breakpoint stops at as itself
static MachineState stepped(u32 n, u32* end_pc, u32* arrivals, Regs* end_regs)
{
	u32 at = 0, count = 0;
	std::vector<u32> pcs;

	for (u32 i = 0; i < n; i++)
		pcs.push_back(machine_step().pc);

	while (OpDesc[machine_read16(pcs.back() - 2)]->type & Delayslot)
		pcs.push_back(machine_step().pc);

	at = pcs.back();

	for (u32 pc : pcs)
		count += pc == at;

	*end_pc = at;
	*arrivals = count;
	*end_regs = regs();

	return machine_state();
}

//Runs to the given arrival at a breakpoint
static MachineState ran(u32 end_pc, u32 arrivals, Regs* end_regs)
{
	bps({ end_pc });

	for (u32 i = 0; i < arrivals; i++)
		machine_resume();

	*end_regs = regs();

	return machine_state();
}

static void gate_b13()
{
	const u32 n = 6000;
	const WorkSetup w = { ENGINE_INTERP, 0, true, true };

	u32 end_pc, arrivals;
	Regs by_step, by_run;

	work_start(w);
	MachineState a = stepped(n, &end_pc, &arrivals, &by_step);
	u64 irqs = counters.irqs;

	work_start(w);
	MachineState b = ran(end_pc, arrivals, &by_run);

	check(a == b && by_step.same_all(by_run) && irqs != 0,
		"interpreter: %u steps through the workload with interrupts, and a run to the same instruction (arrival %u at %08X): the same context, memory and time (%s)",
		n, arrivals, end_pc, str(a));
}

static void gate_b14()
{
	const u32 n = 3000;
	u32 end_pc, arrivals;
	Regs by_step, by_run;

	//FPU code, interrupts masked: the registers, bit for bit
	machine_reset(ENGINE_JIT);
	machine_set_pc(G_fstep);
	R(4) = 100000;
	MachineState a = stepped(n, &end_pc, &arrivals, &by_step);

	machine_reset(ENGINE_JIT);
	machine_set_pc(G_fstep);
	R(4) = 100000;
	MachineState b = ran(end_pc, arrivals, &by_run);

	check(by_step.same_all(by_run) && a.ctx == b.ctx && a.ram == b.ram,
		"jit: %u steps through FPU code and a run to the same instruction: every register, the FPU registers bit for bit", n);
	note("jit, fast timing: time of %u steps %llu, of the run %llu: a step is charged as a block of one instruction", n,
		(unsigned long long)a.time(), (unsigned long long)b.time());

	//the workload, interrupts masked
	const WorkSetup w = { ENGINE_JIT, 0, true, false };

	work_start(w);
	machine_set_sr(SR_MD_MASK);
	a = stepped(n, &end_pc, &arrivals, &by_step);

	work_start(w);
	machine_set_sr(SR_MD_MASK);
	b = ran(end_pc, arrivals, &by_run);

	check(by_step.same_all(by_run) && a.ctx == b.ctx && a.ram == b.ram, "jit: %u steps through the workload and a run to the same instruction: the same registers and memory", n);

	//No cost function of the tree is a sum over instructions: the times of the two are reported, not compared
	settings.dynarec.idleskip = false;

	machine_reset(ENGINE_JIT);
	machine_set_pc(G_istep);
	R(4) = 100000;
	a = stepped(n, &end_pc, &arrivals, &by_step);

	machine_reset(ENGINE_JIT);
	machine_set_pc(G_istep);
	R(4) = 100000;
	b = ran(end_pc, arrivals, &by_run);

	settings.dynarec.idleskip = true;

	check(by_step.same_all(by_run), "jit, idleskip off: %u steps through integer code and a run to the same instruction: the same registers", n);
	note("jit, idleskip off: time of %u steps %llu, of the run %llu: a block costs one and a half times its instructions, rounded down, and at most half a slice",
		n, (unsigned long long)a.time(), (unsigned long long)b.time());

	//the interpreter fallback steps as the interpreter computes, at the cost of the jit's one-instruction blocks
	machine_reset(ENGINE_FALLBACK);
	machine_set_pc(G_istep);
	R(4) = 100000;
	MachineState f = stepped(n, &end_pc, &arrivals, &by_run);

	machine_reset(ENGINE_JIT);
	machine_set_pc(G_istep);
	R(4) = 100000;
	a = stepped(n, &end_pc, &arrivals, &by_step);

	check(f == a && by_step.same_all(by_run), "fallback: %u steps cost what the jit's steps cost and end in the same state", n);
}


/*
	The slot of a delayed conditional branch that is not taken. The interpreter runs it as an instruction of its
	own, so its loop can be left, and an interrupt accepted, between the two. A block of the recompiler holds
	both.
*/
static void pair_start(Engine e, u32 irq_period)
{
	machine_reset(e);
	machine_set_sr(SR_MD);
	machine_set_pc(G_pair);

	R(4) = 0x7FFFFFFF;

	knobs.irq_period = irq_period;
}

//Resumes every debugger stop up to the host stop. at_slot: the stops that were at the address of a slot
static MachineState pair_run(u64* stops, u64* at_slot)
{
	*at_slot = 0;

	for (;;)
	{
		MachineStop s = machine_resume();

		if (!s.debugger)
			break;

		*at_slot += s.pc == G_pair_bt_slot || s.pc == G_pair_bf_slot;
	}

	*stops = counters.stops;

	return machine_state();
}

static void gate_slot()
{
	MachineStop s;

	//An interrupt that is accepted between a branch and its slot returns to the slot. The step on the handler's
	//rte ends there: the slot is the instruction of the step behind it
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);

		for (u32 slot : { (u32)G_pair_bt_slot, (u32)G_pair_bf_slot })
		{
			const char* what = slot == G_pair_bt_slot ? "bt/s" : "bf/s";
			const u32 reg = slot == G_pair_bt_slot ? 1 : 2;

			pair_start(e, 3);

			bool found = false;

			for (u32 i = 0; i < 20000 && !found; i++)
			{
				s = machine_step();
				found = s.pc == G_irq && Sh4cntx.spc == slot;
			}

			if (e != ENGINE_INTERP)
			{
				check(!found && counters.irqs != 0, "%s: no interrupt comes between a %s that is not taken and its slot in 20000 steps (%llu interrupts)",
					en, what, (unsigned long long)counters.irqs);
				continue;
			}

			if (!check(found, "%s: a step on a %s that is not taken ends at the handler of an interrupt, the slot still to run", en, what))
				continue;

			//the handler works on the other register bank
			const u32 ran = Sh4cntx.r_bank[reg];

			for (u32 i = 0; i < 20 && Sh4cntx.pc != G_irq_rte; i++)
				machine_step();

			s = machine_step();
			check(is_step(s, slot) && R(reg) == ran, "%s: the step on the handler's rte ends at the slot of the %s, and the slot has not run (pc %08X, slot runs %u, %u before)",
				en, what, s.pc, R(reg), ran);

			s = machine_step();
			check(is_step(s, slot + 2) && R(reg) == ran + 1, "%s: the next step runs the slot of the %s and nothing else (pc %08X)", en, what, s.pc);
		}
	}

	//Steps against the plain loop of the interpreter cut into units, with interrupts
	{
		struct Unit
		{
			u32 pc;
			u64 ints;
			u64 time;

			bool operator==(const Unit& o) const { return pc == o.pc && ints == o.ints && time == o.time; }
		};

		const u32 steps = 30000;
		std::vector<Unit> by_step, by_loop;
		u32 between = 0;

		pair_start(ENGINE_INTERP, 3);
		for (u32 i = 0; i < steps; i++)
		{
			s = machine_step();
			by_step.push_back({ s.pc, int_hash(regs()), machine_time() });
			between += s.pc == G_irq && (Sh4cntx.spc == G_pair_bt_slot || Sh4cntx.spc == G_pair_bf_slot);
		}
		MachineState a = machine_state();

		pair_start(ENGINE_INTERP, 3);
		for (u32 i = 0; i < steps; i++)
		{
			s = machine_reference_step();
			by_loop.push_back({ s.pc, int_hash(regs()), machine_time() });
		}
		MachineState b = machine_state();

		u32 same = 0;

		while (same < steps && by_step[same] == by_loop[same])
			same++;

		check(same == steps && a == b && between != 0,
			"interpreter: %u steps with interrupts: pc, integer registers and time behind every step are those of the plain loop cut into units (%u agree; %u interrupts between a branch and its slot)",
			steps, same, between);

		if (same < steps)
			note("step %u: the step ends at %08X, time %llu; the plain loop at %08X, time %llu", same + 1, by_step[same].pc,
				(unsigned long long)by_step[same].time, by_loop[same].pc, (unsigned long long)by_loop[same].time);

		//and the plain loop itself, at a slice end that the steps reach with nothing run behind it
		pair_start(ENGINE_INTERP, 3);
		for (u32 i = 0; i < steps || machine_state().counter != SH4_TIMESLICE; i++)
			machine_step();
		a = machine_state();

		const u64 slices = counters.slices;

		pair_start(ENGINE_INTERP, 3);
		knobs.host_stop_slice = slices;
		s = machine_resume();
		b = machine_state();

		check(!s.debugger && a == b, "interpreter: the state behind the steps that end with slice %llu is the state of a run to the end of that slice (%s)",
			(unsigned long long)slices, str(a));
	}

	//The loop is left at a slice end between a branch and its slot, and the slot is armed in that visit of the
	//park context. The pair that is under way has had its check: the slot runs, and the next pair stops, in front
	//of its branch. With nothing armed before, and with the loop that checks already running
	for (Engine e : all_engines)
	{
		for (bool checking : { false, true })
		{
			const char* en = engine_name(e);
			const char* loop = checking ? "an address armed before" : "nothing armed before";
			bool armed = false;
			u32 ran = 0;

			pair_start(e, 0);
			if (checking)
				bps({ G_line });

			knobs.host_stop_slice = 200;
			knobs.excursion_every = 1;
			knobs.excursion = [&]()
			{
				if (armed || Sh4cntx.pc != G_pair_bt_slot)
					return;

				armed = true;
				ran = R(1);

				if (checking)
					bps({ G_line, G_pair_bt_slot });
				else
					bps({ G_pair_bt_slot });
			};

			s = machine_resume();

			if (e == ENGINE_INTERP)
				check(armed && is_bp(s, checking ? 2 : 1, G_pair_bt) && R(1) == ran + 1,
					"%s, %s: a slot armed while the loop is left between its branch and it: the slot runs, the next pair stops at its branch (pc %08X, slot runs %u, %u before)",
					en, loop, s.pc, R(1), ran);
			else
				check(!armed && !s.debugger, "%s, %s: no slice ends between a branch and its slot", en, loop);
		}
	}

	//The address of a slot that a jump arrives at is an instruction like any other: armed in the visit of the park
	//context that follows the jump, it stops at once, as itself
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);
		bool armed = false;
		u32 ran = 0;

		pair_start(e, 0);

		knobs.host_stop_slice = 200;
		knobs.excursion_every = 1;
		knobs.excursion = [&]()
		{
			if (armed || Sh4cntx.pc != G_pair_in)
				return;

			armed = true;
			ran = R(3);
			bps({ G_pair_in });
		};

		s = machine_resume();

		if (armed)
			check(is_bp(s, 1, G_pair_in) && R(3) == ran && Sh4cntx.sr.T == 1,
				"%s: armed while the loop is left behind a jump to an address that follows a bf/s in memory: stops there, nothing of it run (pc %08X)", en, s.pc);
		else
			check(e != ENGINE_INTERP && !s.debugger, "%s: no slice ends behind the jump", en);
	}

	//In a run the same holds: the rte of a handler that came between a branch and its slot arrives at the slot as
	//at any instruction, and a breakpoint on it stops there
	{
		pair_start(ENGINE_INTERP, 3);
		bps({ G_irq });

		bool found = false;

		for (u32 i = 0; i < 200 && !found; i++)
		{
			s = machine_resume();
			found = is_bp(s, 1, G_irq) && Sh4cntx.spc == G_pair_bt_slot;
		}

		const u32 ran = Sh4cntx.r_bank[1];

		bps({ G_irq, G_pair_bt_slot });
		s = machine_resume();

		bool at_slot = found && is_bp(s, 2, G_pair_bt_slot) && R(1) == ran;

		s = machine_resume();

		check(at_slot && is_bp(s, 2, G_pair_bt) && R(1) == ran + 1,
			"interpreter: a breakpoint on a slot stops the handler's return to it at the slot, nothing of it run; the next pair stops at its branch");
	}

	//An event breakpoint that fires in a slice that ends between a branch and its slot stops there: a run, and a
	//step as well, which then ends without the slot
	{
		u64 slice = 0;

		pair_start(ENGINE_INTERP, 0);

		for (u32 i = 0; i < 5000 && slice == 0; i++)
		{
			const bool on_branch = Sh4cntx.pc == G_pair_bt;
			const u64 before = counters.slices;

			machine_step();

			//the step crossed a slice end and charged its slot to the new slice
			if (on_branch && counters.slices != before && machine_backend()->SliceCyclesLeft() == SH4_TIMESLICE - 8)
				slice = counters.slices;
		}

		MachineState at[2];

		for (int step = 0; step < 2; step++)
		{
			const char* how = step ? "a step" : "a run";

			pair_start(ENGINE_INTERP, 0);
			knobs.event_slice = slice;

			if (step)
			{
				for (u32 i = 0; i < 100000; i++)
				{
					s = machine_step();

					if (s.cause != StopCause::Step)
						break;
				}
			}
			else
			{
				s = machine_resume();
			}

			at[step] = machine_state();

			//the slot has run once in every pass before this one, as the last instruction of a pass has
			check(slice != 0 && s.debugger && s.cause == StopCause::Event && s.pc == G_pair_bt_slot && R(1) == R(3) && at[step].counter == SH4_TIMESLICE,
				"interpreter: an event in the slice that ends between a bt/s and its slot ends %s there, the slot not run (pc %08X, slice %llu)", how,
				s.pc, (unsigned long long)slice);

			const u32 ran = R(1);

			bps({ G_pair_bt_slot });
			s = machine_resume();

			check(is_bp(s, 1, G_pair_bt) && R(1) == ran + 1, "interpreter: the slot armed in that stop of %s: the slot runs, the next pair stops at its branch (pc %08X)",
				how, s.pc);
		}

		check(at[0] == at[1], "interpreter: the step ends in the state the run ends in (%s)", str(at[1]));
	}

	//Stops and pauses between a branch and its slot leave no trace
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);
		const bool fb = e == ENGINE_FALLBACK;
		const u64 slices = e == ENGINE_INTERP ? 400 : 40;
		u64 stops, at_slot;

		//the fallback loop runs while a breakpoint is active: its baseline has one that is never reached
		pair_start(e, 3);
		if (fb)
			bps({ G_line });
		knobs.host_stop_slice = slices;
		MachineState base = pair_run(&stops, &at_slot);

		pair_start(e, 3);
		bps({ G_pair_bt_slot, G_pair_bf_slot, G_line });
		knobs.host_stop_slice = slices;
		MachineState st = pair_run(&stops, &at_slot);

		check(st == base && stops != 0 && (at_slot != 0) == (e == ENGINE_INTERP),
			"%s: stop + resume at every arrival at two slots, with interrupts: %llu stops, %llu of them at a slot that a handler returned to, state = baseline (%s)",
			en, (unsigned long long)stops, (unsigned long long)at_slot, str(st));

		pair_start(e, 3);
		bps({ G_pair_bt_slot, G_pair_bf_slot, G_line });
		knobs.host_stop_slice = slices;
		knobs.pause_every = 1;
		st = pair_run(&stops, &at_slot);

		check(st == base && counters.pauses != 0, "%s: the same with a pause at every slice end: %llu stops, %llu pauses, state = baseline", en,
			(unsigned long long)stops, (unsigned long long)counters.pauses);
	}
}


/*
	A stop between a bt/s that was not taken and its slot, with the slot's address armed during the stop, and the
	same address arrived at as a branch target afterwards. The resume exemption is for the instruction that runs
	first: the slot runs without a check call, and the arrival behind it stops.

	The program, a pass of 16 instructions of the interpreter:

	   1  top:  add #1, r9
	   2        mov #0, r3
	   3        clrt
	   4  B:    bt/s end        not taken
	   5  S:    add #1, r1      its slot
	   6        add #1, r2
	   7        tst r3, r3
	   8        add #1, r3
	   9        bt S            taken in the first round of a pass
	  10  S:    add #1, r1      arrived at as a branch target
	  11        add #1, r2
	  12        tst r3, r3
	  13        add #1, r3
	  14        bt S            not taken in the second
	  15        dt r8
	  16        bf top

	A breakpoint on S is checked in front of 4 (the pair stops at its branch) and of 10; one on the instruction
	behind S in front of 6 and 11. A resume from a stop in front of place q runs q, and the stops that follow
	are the checked places behind q, in the order of the program.
*/
#define MID_NOPS   16									//room for the instructions in front of the program
#define MID_TOP    (G_buffer + 2 * MID_NOPS)
#define MID_B      (MID_TOP + 6)
#define MID_S      (MID_TOP + 8)
#define MID_END    (MID_TOP + 22)

//Writes the program with a run of instructions in front of it, and points the pc at the first of them: nops
//single instructions, hops blocks of their own (a bra to the next instruction and its slot)
static void mid_start(Engine e, u32 nops, u32 hops)
{
	machine_reset(e);

	static const u16 code[] = {
		0x7901,		//add #1, r9
		0xE300,		//mov #0, r3
		0x0008,		//clrt
		0x8D06,		//bt/s MID_END
		0x7101,		//add #1, r1
		0x7201,		//add #1, r2
		0x2338,		//tst r3, r3
		0x7301,		//add #1, r3
		0x89FA,		//bt MID_S
		0x4810,		//dt r8
		0x8BF4,		//bf MID_TOP
		0x0009,		//nop: MID_END
		0xAFFE,		//bra to itself
		0x0009,
	};

	for (u32 i = 0; i < sizeof(code) / sizeof(code[0]); i++)
		machine_write16(MID_TOP + i * 2, code[i]);

	u32 pc = MID_TOP;

	for (u32 i = 0; i < hops; i++)
	{
		pc -= 4;
		machine_write16(pc, 0xA000);		//bra pc + 4
		machine_write16(pc + 2, 0x0009);
	}

	for (u32 i = 0; i < nops; i++)
	{
		pc -= 2;
		machine_write16(pc, 0x0009);
	}

	R(8) = 1000;

	machine_set_pc(pc);
}

//The place of the instruction at the pc in its pass, 1 to 16; 0: not in the program
static u32 mid_place()
{
	const u32 pc = Sh4cntx.pc;

	if (pc < MID_TOP || pc >= MID_END)
		return 0;

	const u32 i = (pc - MID_TOP) / 2;	//0 to 10

	if (i < 4)
		return i + 1;
	if (i < 9)
		return i + 1 + (i == 8 ? R(3) == 2 : R(3) != 0) * 5;	//the two rounds of 5 to 9: r3 counts them from place 8 on

	return i + 6;
}

static void gate_midpair()
{
	const u32 never = G_line;

	struct Variant { const char* what; bool behind; };
	const Variant variants[] = { { "the slot's address", false }, { "the slot's address and the instruction behind it", true } };

	bool between_seen = false;

	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);
		const u32 runs = e == ENGINE_INTERP ? 16 : 4;

		u32 places = 0;

		for (u32 run = 0; run < runs; run++)
		{
			for (const Variant& v : variants)
			{
				//the first slice end is a pause: for the interpreter at each place of the pass in turn
				mid_start(e, e == ENGINE_INTERP ? run : 0, e == ENGINE_INTERP ? 0 : run);

				if (e == ENGINE_FALLBACK)
					bps({ never });

				knobs.pause_every = 1;
				MachineStop s = machine_resume();
				knobs.pause_every = 0;

				u32 q = mid_place();

				if (!check(s.debugger && s.cause == StopCause::Pause && q != 0 && (e != ENGINE_FALLBACK || machine_used_fallback()),
					"%s: the pause at the first slice end is in the program (%08X)", en, s.pc))
					continue;

				places |= 1u << q;

				const u32 paused = q;
				const bool between = q == 5;

				between_seen = between_seen || between;

				//the slot's address is armed in the stop
				if (v.behind)
					bps({ MID_S, MID_S + 2, never });
				else
					bps({ MID_S, never });

				//what the registers count in front of place q: passes begun, and runs of the slot's address
				u32 pass = R(9);
				u32 s_runs = R(1);

				bool ok = true;
				char seen[160] = "";

				for (u32 i = 0; i < 6 && ok; i++)
				{
					//place q runs, up to the next checked place
					for (;;)
					{
						if (q == 1)
							pass++;
						if (q == 5 || q == 10)
							s_runs++;

						q = q == 16 ? 1 : q + 1;

						if (q == 4 || q == 10 || (v.behind && (q == 6 || q == 11)))
							break;
					}

					const u32 want_pc = q == 4 ? MID_B : (q == 10 ? MID_S : MID_S + 2);
					const u32 want_id = q == 6 || q == 11 ? 2 : 1;

					s = machine_resume();

					snprintf(seen + strlen(seen), sizeof(seen) - strlen(seen), " %08X", s.pc);

					ok = is_bp(s, want_id, want_pc) && mid_place() == q && R(9) == pass && R(1) == s_runs;

					if (!ok)
						note("%s, paused in front of place %u, %s armed: stop %u is at %08X (id %u, place %u, pass %u, %u runs of the slot's address), the rules have %08X (id %u, place %u, pass %u, %u runs)",
							en, paused, v.what, i + 1, s.pc, s.id, mid_place(), R(9), R(1), want_pc, want_id, q, pass, s_runs);
				}

				check(ok, "%s: paused in front of place %u%s, %s armed in the stop: the six stops that follow are the checked places behind the first instruction that runs (%s)",
					en, paused, between ? ", between the bt/s and its slot" : "", v.what, seen);
			}
		}

		u32 count = 0;
		for (u32 q = 1; q <= 16; q++)
			count += (places >> q) & 1;

		check(e == ENGINE_INTERP ? count == 16 : count >= 2, "%s: the pauses were in front of %u places of the pass", en, count);
	}

	check(between_seen, "the interpreter was paused between the bt/s and its slot");
}


/*
	The resume exemption when an interrupt is accepted in front of the instruction of the stop. A step-out can
	stop behind the block (the interpreter: the instruction) that used the slice up: the loop that is entered
	for the resume then does the end-of-slice work first, and an interrupt that this accepts has its handler
	run in front of the instruction. When the handler returns to it, it has not executed: it stops again.
*/
static void gate_vector()
{
	for (bool idleskip : { true, false })
	{
		settings.dynarec.idleskip = idleskip;

		for (Engine e : all_engines)
		{
			const char* en = engine_name(e);
			const char* timing = idleskip ? "fast" : "idleskip off";

			machine_reset(e);
			machine_set_sr(SR_MD);
			machine_set_pc(G_work);

			R(10) = 0xFFFFFFFF;
			R(12) = 1;
			R(14) = 0;

			//a step-out of the leaf at every arrival, until one ends with the slice used up
			bool found = false;
			MachineStop s;

			for (u32 i = 0; i < 20000 && !found; i++)
			{
				bps({ G_leaf_mid });
				s = machine_resume();

				if (!is_bp(s, 1, G_leaf_mid))
					break;

				bps({ });
				s = machine_step_out();

				if (!s.debugger || s.cause != StopCause::Step)
					break;

				found = machine_state().counter <= 0;
			}

			if (!check(found, "%s, %s: a step-out of the leaf ends behind the block that used its slice up (%08X, counter %d)", en, timing, s.pc,
				machine_state().counter))
				continue;

			//the instruction of the stop is armed, and the slice end that the resume begins with requests the
			//interrupt
			const u32 at = s.pc;
			const Regs before = regs();
			const u32 handled = machine_read32(G_irqcnt);

			bps({ at });
			knobs.irq_period = 1;

			s = machine_resume();

			check(is_bp(s, 1, at) && regs().same_int(before) && machine_read32(G_irqcnt) == handled + 1,
				"%s, %s: the handler of an interrupt that the resume accepted in front of the instruction of the stop returns to it, and it stops again, not executed (stop at %08X, %u interrupts handled, pass %u, at the first stop %08X pass %u)",
				en, timing, s.pc, machine_read32(G_irqcnt) - handled, R(9), at, before.r[9]);

			//the resume of that stop runs it
			knobs.irq_period = 0;
			bps({ at, G_leaf_mid });
			s = machine_resume();

			check(is_bp(s, 2, G_leaf_mid) || (is_bp(s, 1, at) && R(9) != before.r[9]), "%s, %s: and the resume of that stop runs it (%08X)", en, timing, s.pc);
		}
	}

	settings.dynarec.idleskip = true;
}


/*
	An event breakpoint on the machine's vblank. The stop is where the slice of the event ends; a SLEEP wait is
	cut short by it, and an interrupt that is accepted on the way out of the stop returns behind the SLEEP.
*/
static void event_breakpoint(bool on)
{
	const u32 id = 9;
	const u8 event = (u8)dbg::EV_CORE_VBLANK;

	machine_event_breakpoints(&id, &event, on ? 1 : 0);
	dbg::g_event_on[dbg::EV_CORE_VBLANK] = on ? 1 : 0;
}

static void gate_event()
{
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);

		//The fallback loop runs while a breakpoint is active: every run has one that is never reached
		const bool fb = e == ENGINE_FALLBACK;

		//in running code: at the slice end, and the resume goes on as a run does
		const WorkSetup w = { e, 600, true, false };

		work_start(w);
		if (fb)
			bps({ G_line });
		MachineState base = work_run();

		work_start(w);
		if (fb)
			bps({ G_line });
		knobs.vblank_period = 11;
		event_breakpoint(true);

		u64 stops;
		MachineState s = work_run(&stops);

		check(s == base && stops == 600 / 11, "%s: an event breakpoint on the vblank stops at every vblank, %llu times, and leaves the state of a run (%s)", en,
			(unsigned long long)stops, str(s));

		work_start(w);
		knobs.vblank_period = 11;
		event_breakpoint(true);
		MachineStop stop = machine_resume();

		check(stop.debugger && stop.cause == StopCause::Event && stop.id == 9 && counters.slices == 11, "%s: the stop has the cause event and the id of the breakpoint, in the slice of the vblank (%llu)",
			en, (unsigned long long)counters.slices);

		//the gate of the event closed, the category of the events muted: no stop
		work_start(w);
		if (fb)
			bps({ G_line });
		knobs.vblank_period = 11;
		event_breakpoint(true);
		dbg::g_event_on[dbg::EV_CORE_VBLANK] = 0;
		s = work_run(&stops);
		check(s == base && stops == 0, "%s: with the gate of the event closed nothing stops", en);

		work_start(w);
		if (fb)
			bps({ G_line });
		knobs.vblank_period = 11;
		event_breakpoint(true);
		machine_active_mask(~dbg::CategoryBit(dbg::Category::Events));
		s = work_run(&stops);
		check(s == base && stops == 0, "%s: with the category of the events muted nothing stops", en);

		//in a SLEEP wait, no interrupt pending: the pc is on the SLEEP, which a resume executes again
		machine_reset(e);
		machine_set_sr(SR_MD);
		machine_set_pc(G_sleep);
		knobs.vblank_period = 3;
		knobs.irq_period = 7;
		event_breakpoint(true);

		stop = machine_resume();
		check(stop.debugger && stop.cause == StopCause::Event && stop.pc == G_sleep && counters.slices == 3 && counters.irqs == 0,
			"%s: a vblank in a SLEEP wait stops in its slice, the pc on the SLEEP (slice %llu, pc %08X)", en, (unsigned long long)counters.slices, stop.pc);

		event_breakpoint(false);
		stop = machine_step();
		check(is_step(stop, G_irq) && Sh4cntx.spc == G_sleep_after && counters.slices == 7, "%s: the SLEEP is executed again and waits for its interrupt (slice %llu)", en,
			(unsigned long long)counters.slices);

		//in a SLEEP wait, the interrupt request in the same slice: the acceptance that the stop deferred is made
		//with the pc behind the SLEEP
		machine_reset(e);
		machine_set_sr(SR_MD);
		machine_set_pc(G_sleep);
		knobs.vblank_period = 3;
		knobs.irq_period = 3;
		event_breakpoint(true);

		stop = machine_resume();
		check(stop.debugger && stop.cause == StopCause::Event && stop.pc == G_sleep && counters.slices == 3 && counters.irqs == 1,
			"%s: a vblank and an interrupt request in one slice of a SLEEP wait: the stop comes first, the pc on the SLEEP", en);

		event_breakpoint(false);
		stop = machine_step();
		check(is_step(stop, G_irq) && Sh4cntx.spc == G_sleep_after && counters.slices == 3,
			"%s: leaving the stop accepts the interrupt, which returns behind the SLEEP (spc %08X); a step is complete there", en, Sh4cntx.spc);
	}

	event_breakpoint(false);
}


/*
	Arming: what is discarded, and what is not
*/
static void gate_arm()
{
	MachineStop s;

	//a block that starts on one page and has the armed address on the next
	machine_reset(ENGINE_JIT);
	machine_set_pc(G_span);
	bps({ G_span_end });
	machine_resume();
	machine_resume();

	RuntimeBlockInfo* blk = bm_GetBlock(G_span);

	check(blk != nullptr && blk->addr + blk->sh4_code_size > G_span_p2 && (G_span >> 12) != (G_span_p2 >> 12),
		"a cached block starts on one page and ends on the next");

	RuntimeBlockInfo* other = bm_GetBlock(G_span_end);

	bps({ G_span_p2 });
	check(bm_GetBlock(G_span) == nullptr, "arming an address on its second page discards it");
	check(other != nullptr && bm_GetBlock(G_span_end) == nullptr, "and the other blocks of that page");

	machine_set_pc(G_span);
	s = machine_resume();
	check(is_bp(s, 1, G_span_p2) && R(0) == 7 && block_checks(G_span) == 1, "the block is compiled again with the check, and the breakpoint hits (r0 = %u)", R(0));

	bps({ });
	check(bm_GetBlock(G_span) == nullptr, "disarming discards it again");
	machine_set_pc(G_span);
	knobs.host_stop_slice = counters.slices + 2;
	s = machine_resume();
	check(!s.debugger && block_checks(G_span) == 0, "and it is compiled without a check");

	//a delayed branch at the end of a page with its slot on the next
	machine_reset(ENGINE_JIT);
	machine_set_pc(G_span2);
	bps({ G_span2_end });
	machine_resume();

	blk = bm_GetBlock(G_span2);
	check(blk != nullptr && (G_span2_br >> 12) != (G_span2_slot >> 12), "a cached block has its branch on one page and the slot on the next");

	bps({ G_span2_slot });
	check(bm_GetBlock(G_span2) == nullptr, "arming the slot discards the block of the branch");

	machine_set_pc(G_span2);
	s = machine_resume();
	check(is_bp(s, 1, G_span2_br) && R(0) == 2, "and the breakpoint on the slot stops at the branch");

	//only the page of the address is discarded
	machine_reset(ENGINE_JIT);
	machine_set_pc(G_span);
	bps({ G_line_end, G_span_end });
	machine_resume();
	machine_set_pc(G_line);
	machine_resume();

	RuntimeBlockInfo* far = bm_GetBlock(G_line);
	RuntimeBlockInfo* far2 = bm_GetBlock(G_line_b2);

	bps({ G_line_end, G_span_end, G_span_p2 });
	check(far != nullptr && far2 != nullptr && bm_GetBlock(G_line) == far && bm_GetBlock(G_line_b2) == far2 && bm_GetBlock(G_span) == nullptr,
		"arming an address leaves the blocks of other pages cached");

	//a change that moves no address in or out of the armed set discards nothing
	machine_set_pc(G_span);
	s = machine_resume();
	blk = bm_GetBlock(G_span);

	u32 ids2[] = { 7, 8, 9, 10 };
	u32 addrs2[] = { G_span_p2, G_line_end, G_span_p2 | 0x20000000, G_span_end };
	machine_breakpoints(ids2, addrs2, 4);
	check(is_bp(s, 3, G_span_p2) && blk != nullptr && bm_GetBlock(G_span) == blk,
		"a second breakpoint on an armed address, named through a mirror, discards nothing");

	//two breakpoints on one address: the lowest id is reported
	machine_set_pc(G_span);
	s = machine_resume();
	check(is_bp(s, 7, G_span_p2), "of two breakpoints on one address the lowest id is reported (id %u)", s.id);

	//removing one of them keeps the address armed
	u32 ids3[] = { 8, 9, 10 };
	u32 addrs3[] = { G_line_end, G_span_p2 | 0x20000000, G_span_end };
	machine_breakpoints(ids3, addrs3, 3);
	check(bm_GetBlock(G_span) == blk, "removing one of the two discards nothing");
	machine_set_pc(G_span);
	s = machine_resume();
	check(is_bp(s, 9, G_span_p2), "and the other one stops (id %u)", s.id);

	//the category mask is looked at when a check is made: no block is compiled for it
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);

		machine_reset(e);
		machine_set_pc(G_loop);
		R(4) = 100;
		R(5) = G_buffer;
		knobs.host_stop_slice = 20;
		bps({ G_loop_mid, G_loop_end });

		u32 hits = 0;

		for (int i = 0; i < 5; i++)
			hits += is_bp(machine_resume(), 1, G_loop_mid);

		blk = bm_GetBlock(G_loop_top);

		dbg::sh4::SetActiveMask(0);

		bool kept = bm_GetBlock(G_loop_top) == blk;

		s = machine_resume();

		bool silent = !s.debugger;

		dbg::sh4::SetActiveMask(~0u);
		kept = kept && bm_GetBlock(G_loop_top) == blk;

		knobs.host_stop_slice = 0;
		s = machine_resume();

		check(hits == 5 && silent && is_bp(s, 2, G_loop_end), "%s: a muted category stops nothing, unmuted it stops again", en);

		if (e == ENGINE_JIT)
			check(blk != nullptr && kept && block_checks(G_loop_top) == 1, "%s: and the blocks with the checks stayed as they were", en);
	}

	//a breakpoint added in a stop, in the block that is stopped
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);

		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_end });
		machine_resume();
		MachineState base = machine_state();

		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid, G_line_end });
		s = machine_resume();
		bool first = is_bp(s, 1, G_line_mid);

		bps({ G_line_mid, G_line_end, G_line_m2, G_line });
		s = machine_resume();
		bool second = is_bp(s, 3, G_line_m2) && R(2) == 12;

		s = machine_resume();

		check(first && second && is_bp(s, 2, G_line_end) && machine_state() == base,
			"%s: a breakpoint added during a stop, further on in the stopped block, stops the resume there; time and state at the end are a run's", en);
	}

	//breakpoints on neighbours: on two instructions in a row, and on a delayed branch and its slot
	for (Engine e : all_engines)
	{
		const char* en = engine_name(e);

		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_mid + 2, G_line_mid, G_line_end });

		s = machine_resume();
		bool first = is_bp(s, 2, G_line_mid);
		s = machine_resume();
		bool second = is_bp(s, 1, G_line_mid + 2);
		s = machine_resume();

		check(first && second && is_bp(s, 3, G_line_end), "%s: breakpoints on two instructions in a row stop at each, once", en);

		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_slot, G_line_br, G_line_end });

		s = machine_resume();
		first = is_bp(s, 2, G_line_br) && R(3) == 5;
		s = machine_resume();

		check(first && is_bp(s, 3, G_line_end) && R(3) == 8, "%s: breakpoints on a delayed branch and on its slot stop once, at the branch, with the branch's id", en);

		//the same with the branch's own breakpoint muted away: the slot's still stops there
		machine_reset(e);
		machine_set_pc(G_line);
		bps({ G_line_slot, G_line_end });
		s = machine_resume();
		check(is_bp(s, 1, G_line_br), "%s: the breakpoint on the slot alone stops at the branch", en);
	}

	//the queries of the decoders
	machine_reset(ENGINE_JIT);
	machine_set_pc(G_line);
	bps({ G_line_mid });
	machine_resume();
	check(dbg_sh4_armed == 1 && dbg_sh4_IsArmed(G_line_mid | 0x20000000) && !dbg_sh4_IsArmed(G_line_mid + 2)
		&& dbg_sh4_AnyArmed(G_line, 16) && !dbg_sh4_AnyArmed(G_line_mid + 2, 0x1000),
		"the armed set answers for mirrors and ranges");
}


/*
	threads: what the recompiler of a new emulator thread starts with. Every emulator thread makes its backend
	objects anew, and a process has as many emulator threads as its host stops and starts the machine. The code
	region is the process's: the host is asked for it once. A recompiler that asked at every start of a thread
	got the same pages again on Linux; on Windows it got a new region of 10 MB near the program every time, none
	of them freed, and the process ended when the search for the next one found no room, some two hundred
	threads on.
*/
static void gate_threads()
{
	const u32 threads = 256;
	const WorkSetup w = { ENGINE_JIT, 40, true, true };

	//the object that machine_init() made
	work_start(w);

	const MachineState first = work_run();

	const u8* const region = CodeCache;
	const void* const mainloop = (const void*)rdv_ngen->Mainloop;
	const u32 base = LastAddr_min;
	const u32 code = LastAddr - LastAddr_min;
	const u32 blocks = (u32)all_blocks.size();
	const u32 prepares = jit_block_prepares;

	u32 moved = 0, stubs_moved = 0, not_empty = 0, in_tables = 0, differ = 0, other_code = 0;

	for (u32 i = 0; i < threads; i++)
	{
		machine_thread_start();

		moved += CodeCache != region;
		stubs_moved += LastAddr_min != base || (const void*)rdv_ngen->Mainloop != mainloop;
		not_empty += LastAddr != LastAddr_min;
		in_tables += !all_blocks.empty();

		work_start(w);

		differ += work_run() != first;
		other_code += LastAddr - LastAddr_min != code || all_blocks.size() != blocks;
	}

	check(prepares == 1 && jit_block_prepares == 1, "the host prepared the code region once, for the first of %u backend objects of the recompiler (%u calls before the %u that followed it, %u behind them)",
		threads + 1, prepares, threads, jit_block_prepares);
	check(moved == 0 && stubs_moved == 0, "every object has the code cache where the first had it, and the stubs of the code generator at its start, %u bytes (%u found another region, %u the stubs elsewhere)",
		base, moved, stubs_moved);
	check(not_empty == 0 && in_tables == 0, "every object starts with nothing behind the stubs and no block in the tables (%u had code there, %u a block)",
		not_empty, in_tables);
	check(differ == 0 && other_code == 0 && code != 0 && blocks != 0, "the workload ends in the state of the first object's run on every one, with the same %u blocks in %u bytes of code (%u differ in state, %u in code)",
		blocks, code, differ, other_code);
}


#if defined(_WIN32)
/*
	unwind (Windows): a stop that a check of the recompiler made, walked by the system's unwinder from the debugger's
	latch through dbg_sh4_Check, the check stub, the block and the main loop back to the machine (machine.cpp,
	walk_from_stop). The stub calls dbg_sh4_Check with the callee's home space below the refund, so that its frame at
	the call is 40 bytes below the return address into the block: the frame of a block, which is what the unwind
	data of the code region describes for every pc that is not the main loop's or RunBlock's
	(libswirl/windows/win_jit_unwind.cpp, jit/backend/x64/rec_x64.cpp). sh4bp_test holds the walks of every stop of
	every gate as well; this gate makes sure that some of them came from checks
*/
static void gate_unwind()
{
	struct Case { const char* what; u32 bp; };
	static const Case cases[] = {
		{ "the first instruction of a block", G_line },
		{ "the middle of a block", G_line_mid },
		{ "the slot of a bra", G_line_slot },
	};

	for (const Case& c : cases)
	{
		machine_reset(ENGINE_JIT);
		machine_set_pc(G_line);
		bps({ c.bp });

		const MachineWalks before = walks;
		MachineStop s = machine_resume();

		check(s.debugger && s.cause == StopCause::Breakpoint && s.id == 1, "jit: %s: the check stops there (at %08X)", c.what, s.pc);
		check(walks.made > before.made && walks.through_region > before.through_region && walks.failed == before.failed,
			"jit: %s: the system's unwinder walked the stop from the debugger's latch through the check stub, the block and the main "
			"loop back to the machine (%u walks, %u of them through 3 frames of the code region or more, %u went wrong%s%s)",
			c.what, walks.made - before.made, walks.through_region - before.through_region, walks.failed - before.failed,
			walks.failed != before.failed ? ": " : "", walks.failed != before.failed ? walks.first_failure : "");
	}

	note("the most frames of the code region one walk came through: %d", walks.region_frames);
}
#endif


#if HOST_CPU == CPU_ARM64
/*
	abi (arm64): the main loop and RunBlock of the arm64 recompiler give a caller back what the AAPCS64 makes a callee
	keep: x19-x29, sp and the low 64 bits of v8-v15 (d8-d15), through every way they end (jit/backend/arm64/
	rec_arm64.cpp: one frame and one exit for both). The blocks keep guest registers in w19-w26 and s8-s15 (a write of
	sN clears the rest of v<N>), the slice counter in w27, the context in x28 and the pc in w29: a frame that saved
	s8-s15 as 32-bit values, or an exit that missed one of them, gives the caller other values back. As windows.abi.xmm
	for Windows' xmm6-xmm15: a thunk loads canaries into the nineteen registers, calls the code under test, and writes
	what they hold when it returns, and sp. The gate puts a wrapper of the thunk in the place of Mainloop and of
	RunBlock (rdv_ngen), so that the machine's runs and steps go through it, on a line whose instructions write r0 to
	r7 and fr0 to fr9 (flush_line_write: every host register of the blocks holds a guest register), and checks every
	call, case by case:
	    the loop entered with the cpu stopped (the poll), a host stop at a slice end, a pause latched at a slice end,
	    the loop entered with the slice used up (the end of the slice first);
	    with native debugger checks also a check that stops in a block, a flow report that stops, the code of a block
	    that hands its rest over, a step (RunBlock), a step that ends the slice (RunBlock's end of slice)
*/
extern "C" void sh4bp_abi_thunk(void (*fn)(void*), void* arg, const u64* in, u64* out);

#define ABI_STR_(x) #x
#define ABI_STR(x) ABI_STR_(x)
#define ABI_SYM(name) ABI_STR(__USER_LABEL_PREFIX__) #name
#if defined(__ELF__)
#define ABI_TEXT ".pushsection .text\n\t"
#define ABI_TEXT_END ".popsection\n"
#else
#define ABI_TEXT ".text\n\t"
#define ABI_TEXT_END ""
#endif

//in: x19-x29 (11), d8-d15 (8); out: the same nineteen behind the call, and sp in front of it and behind it
__asm__
(
	ABI_TEXT
	".p2align 2\n\t"
	".globl " ABI_SYM(sh4bp_abi_thunk) "\n"
	ABI_SYM(sh4bp_abi_thunk) ":\n\t"
	"hint #34\n\t"						// bti c
	"stp x29, x30, [sp, #-176]!\n\t"
	"stp x19, x20, [sp, #16]\n\t"
	"stp x21, x22, [sp, #32]\n\t"
	"stp x23, x24, [sp, #48]\n\t"
	"stp x25, x26, [sp, #64]\n\t"
	"stp x27, x28, [sp, #80]\n\t"
	"stp d8, d9, [sp, #96]\n\t"
	"stp d10, d11, [sp, #112]\n\t"
	"stp d12, d13, [sp, #128]\n\t"
	"stp d14, d15, [sp, #144]\n\t"
	"str x3, [sp, #160]\n\t"
	"mov x9, sp\n\t"
	"str x9, [x3, #152]\n\t"
	"mov x9, x0\n\t"
	"mov x0, x1\n\t"
	"ldp x19, x20, [x2, #0]\n\t"
	"ldp x21, x22, [x2, #16]\n\t"
	"ldp x23, x24, [x2, #32]\n\t"
	"ldp x25, x26, [x2, #48]\n\t"
	"ldp x27, x28, [x2, #64]\n\t"
	"ldr x29, [x2, #80]\n\t"
	"ldp d8, d9, [x2, #88]\n\t"
	"ldp d10, d11, [x2, #104]\n\t"
	"ldp d12, d13, [x2, #120]\n\t"
	"ldp d14, d15, [x2, #136]\n\t"
	"blr x9\n\t"
	"ldr x9, [sp, #160]\n\t"
	"stp x19, x20, [x9, #0]\n\t"
	"stp x21, x22, [x9, #16]\n\t"
	"stp x23, x24, [x9, #32]\n\t"
	"stp x25, x26, [x9, #48]\n\t"
	"stp x27, x28, [x9, #64]\n\t"
	"str x29, [x9, #80]\n\t"
	"stp d8, d9, [x9, #88]\n\t"
	"stp d10, d11, [x9, #104]\n\t"
	"stp d12, d13, [x9, #120]\n\t"
	"stp d14, d15, [x9, #136]\n\t"
	"mov x10, sp\n\t"
	"str x10, [x9, #160]\n\t"
	"ldp d14, d15, [sp, #144]\n\t"
	"ldp d12, d13, [sp, #128]\n\t"
	"ldp d10, d11, [sp, #112]\n\t"
	"ldp d8, d9, [sp, #96]\n\t"
	"ldp x27, x28, [sp, #80]\n\t"
	"ldp x25, x26, [sp, #64]\n\t"
	"ldp x23, x24, [sp, #48]\n\t"
	"ldp x21, x22, [sp, #32]\n\t"
	"ldp x19, x20, [sp, #16]\n\t"
	"ldp x29, x30, [sp], #176\n\t"
	"ret\n\t"
	ABI_TEXT_END
);

static const char* const abi_names[19] = {
	"x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29",
	"d8", "d9", "d10", "d11", "d12", "d13", "d14", "d15",
};

//What the wrappers found, for the case that runs
static struct
{
	MainloopFnPtr_t mainloop;
	RunBlockFnPtr_t runblock;
	u64 seed;
	u32 mainloop_calls, runblock_calls;
	u32 bad_calls;
	char first_bad[200];
} abi;

static void abi_call(void (*fn)(void*), void* arg, const char* what)
{
	u64 in[19];
	u64 out[21];

	//values a block does not make: every bit pattern of the canaries differs in both halves from call to call
	for (u32 i = 0; i < 19; i++)
		in[i] = (abi.seed += 0x9E3779B97F4A7C15ull) | 0x8000000100000001ull;

	memset(out, 0, sizeof(out));
	sh4bp_abi_thunk(fn, arg, in, out);

	int wrong = -1;
	for (u32 i = 0; i < 19 && wrong < 0; i++)
		if (out[i] != in[i])
			wrong = (int)i;

	if (wrong >= 0 || out[20] != out[19])
	{
		if (abi.bad_calls++ == 0)
		{
			if (wrong >= 0)
				snprintf(abi.first_bad, sizeof(abi.first_bad), "%s gave %s back as %016llX, not %016llX", what, abi_names[wrong],
					(unsigned long long)out[wrong], (unsigned long long)in[wrong]);
			else
				snprintf(abi.first_bad, sizeof(abi.first_bad), "%s gave sp back as %016llX, not %016llX", what,
					(unsigned long long)out[20], (unsigned long long)out[19]);
		}
	}
}

static void abi_mainloop(void* rcb)
{
	abi.mainloop_calls++;
	abi_call(abi.mainloop, rcb, "Mainloop");
}

static void abi_runblock(void* code)
{
	abi.runblock_calls++;
	abi_call((void (*)(void*))abi.runblock, code, "RunBlock");
}

//The wrappers in the place of the backend's two entries, from here until abi_off()
static void abi_on()
{
	abi.mainloop = rdv_ngen->Mainloop;
	abi.runblock = rdv_ngen->RunBlock;

	rdv_ngen->Mainloop = abi_mainloop;
	if (abi.runblock)
		rdv_ngen->RunBlock = abi_runblock;

	abi.mainloop_calls = abi.runblock_calls = abi.bad_calls = 0;
	abi.first_bad[0] = 0;
}

static void abi_off()
{
	rdv_ngen->Mainloop = abi.mainloop;
	rdv_ngen->RunBlock = abi.runblock;
}

//The line of flush_line_write at its top, run until the host stops it at the given slice
static Dense abi_line(u32 host_stop_slice)
{
	machine_reset(ENGINE_JIT);

	Dense d = flush_line_write(0x8C020000);

	R(8) = 0;		//2^32 passes
	R(12) = G_leaf;
	R(13) = G_data;
	R(14) = d.top;
	machine_set_pc(d.top);
	machine_set_sr(SR_MD);

	knobs.host_stop_slice = host_stop_slice;

	return d;
}

//One case: what the wrappers counted, and that every call gave the registers back
static void abi_case(const char* what, bool ran, u32 mainloop_calls, u32 runblock_calls)
{
	check(ran && abi.mainloop_calls >= mainloop_calls && abi.runblock_calls >= runblock_calls && abi.bad_calls == 0,
		"%s: x19-x29, d8-d15 and sp came back as they were from %u calls of Mainloop and %u of RunBlock (%u did not%s%s)",
		what, abi.mainloop_calls, abi.runblock_calls, abi.bad_calls, abi.bad_calls ? ": " : "", abi.first_bad);
}

static void gate_abi()
{
	ngen_features features = { };
	rdv_ngen->GetFeatures(&features);

	//the loop entered with the cpu stopped: the poll leaves it at once
	{
		abi_line(0);
		abi_on();
		Sh4cntx.CpuRunning = 0;
		rdv_ngen->Mainloop(sh4_dyna_rcb);
		abi_off();

		abi_case("Mainloop entered with the cpu stopped", true, 1, 0);
	}

	//a host stop at a slice end: the loop ends at its poll behind the slice end
	{
		abi_line(6);
		abi_on();
		MachineStop s = machine_resume();
		abi_off();

		abi_case("the blocks of a line that writes r0-r7 and fr0-fr9, to a host stop", !s.debugger && counters.slices >= 6 && R(0) != 0, 1, 0);
	}

	//a pause latched at a slice end, and the resumes behind it
	{
		abi_line(30);
		knobs.pause_every = 4;
		abi_on();

		u32 pauses = 0;
		for (;;)
		{
			MachineStop s = machine_resume();

			if (!s.debugger)
				break;
			pauses += s.cause == StopCause::Pause;
		}
		abi_off();

		abi_case("pauses latched at slice ends, each resumed, to a host stop", pauses >= 5, pauses + 1, 0);
	}

	//the loop entered with the slice used up: the end of the slice comes first
	{
		abi_line(5);
		rdv_ngen->SetSliceCyclesLeft(-7);
		abi_on();
		MachineStop s = machine_resume();
		abi_off();

		abi_case("Mainloop entered with the slice used up (-7 cycles left), to a host stop", !s.debugger && counters.slices >= 5, 1, 0);
	}

	if (!features.DebugChecks)
	{
		note("the recompiler has no native debugger checks: no case of a check, a flow report, a block's rest or RunBlock");
		return;
	}

	//a check that stops, in a block behind writes of every register: the check stub's exit
	{
		Dense d = abi_line(0);
		bps({ d.top + 18 * 2 });
		abi_on();
		MachineStop s = machine_resume();
		abi_off();

		abi_case("a check that stops in a block (the check stub's exit)", is_bp(s, 1, d.top + 18 * 2), 1, 0);
	}

	//steps: RunBlock, its exit behind the block; and a step that uses the slice up, RunBlock's end of the slice
	{
		Dense d = abi_line(0);
		abi_on();
		MachineStop s1 = machine_step();
		const u64 slices = counters.slices;
		rdv_ngen->SetSliceCyclesLeft(1);
		MachineStop s2 = machine_step();
		abi_off();

		abi_case("two steps, the second of which ends the slice (RunBlock, its exit and its end of the slice)",
			is_step(s1, d.top + 2) && is_step(s2, d.top + 4) && counters.slices == slices + 1, 0, 2);
	}

	//A step-out from the leaf the line calls: the flow report of its return stops (that exit). From the stop at the
	//leaf's entry the rest of its block is owed and runs outside the block cache, so the report is made in RunBlock;
	//with the pc written in that stop nothing is owed (b10), and the report is made in a block of the main loop
	for (bool written : { false, true })
	{
		Dense d = abi_line(0);
		bps({ G_leaf });
		abi_on();
		MachineStop a = machine_resume();
		bps({});
		if (written)
			machine_set_pc(G_leaf);
		const u32 mainloop_calls = abi.mainloop_calls, runblock_calls = abi.runblock_calls;
		MachineStop b = machine_step_out();
		abi_off();

		const bool ok = is_bp(a, 1, G_leaf) && b.debugger && b.cause == StopCause::Step && b.pc == d.call + 4
			&& (written ? abi.mainloop_calls == mainloop_calls + 1 && abi.runblock_calls == runblock_calls
			            : abi.runblock_calls == runblock_calls + 1);

		abi_case(written ? "a step-out from the leaf the line calls, the pc written at the stop: the flow report of its return stops in "
		                   "a block of the main loop (that exit)"
		                 : "a step-out from the leaf the line calls: the flow report of its return stops in the rest of the leaf's block, "
		                   "which RunBlock runs (that exit)",
			ok, written ? 2 : 1, written ? 0 : 1);
	}

	//the code of a block that ends in front of its rest (more armed and muted instructions than the budget): that
	//exit, and RunBlock for the rest
	{
		Dense d = dense_start(ENGINE_JIT, 0x8C020000, features.DebugCheckBudget + 40, true, 0);

		machine_set_sr(SR_MD);
		dense_arm(d, features.DebugCheckBudget + 40);
		dbg::sh4::SetActiveMask(0);
		knobs.host_stop_slice = 12;

		abi_on();
		MachineStop s = machine_resume();
		abi_off();

		abi_case("a block whose code hands its rest over, to a host stop (that exit, and RunBlock for the rest)",
			!s.debugger && dense_blocks().with_rest != 0, 2, 1);
	}
}
#endif


const Gate gates[] = {
	{ "b1", "B1 exact pc and registers at a hit: block start, mid-block, delay slot", gate_b1 },
	{ "b2", "B2 resume exempts once; hit counts in a loop; time at the loop exit", gate_b2 },
	{ "b3", "B3 timing neutrality: continue answers, stop + resume, pauses, excursions, pending step-out", gate_b3 },
	{ "b4", "B4 20000 steps equal the interpreter's steps in pc and registers", gate_b4 },
	{ "b5", "B5 step preconditions (-32005), step on SLEEP", gate_b5 },
	{ "b6", "B6 arming in a hot compiled block while running, disarming", gate_b6 },
	{ "b7", "B7 step over and step out", gate_b7 },
	{ "b8", "B8 mirrors 0x0C / 0x8C / 0xAC", gate_b8 },
	{ "b9", "B9 self-modifying code under an armed page", gate_b9 },
	{ "b10", "B10 no stale remainder after a written pc, a step, a reset", gate_b10 },
	{ "b12", "B12 the interpreter fallback charges by decoder blocks", gate_b12 },
	{ "switch", "the forced fallback selected and dropped while the guest runs: no slice end moves", gate_switch },
	{ "ownslot", "a delayed branch to its own slot: the arrival at the slot's address is a block start with its check", gate_ownslot },
	{ "dense", "1024 addresses armed in straight lines: a bounded number of checks per compiled block, the stops and the time unchanged", gate_dense },
	{ "b13", "B13 interpreter: n steps equal a run of n instructions", gate_b13 },
	{ "b14", "B14 jit: n steps equal a run to the n-th instruction", gate_b14 },
	{ "slot", "the interpreter between a conditional branch that is not taken and its slot: steps, arming, stops", gate_slot },
	{ "midpair", "a slot armed in a stop between its branch and it: the slot runs, the arrival at its address behind it stops", gate_midpair },
	{ "vector", "an interrupt accepted in front of the instruction of a stop: it stops again when the handler returns to it", gate_vector },
	{ "arm", "page discards on arming, blocks over two pages, category mask, breakpoints added in a stop", gate_arm },
	{ "event", "an event breakpoint on the vblank of the machine: at a slice end, in a SLEEP wait, with an interrupt request", gate_event },
	{ "threads", "the recompiler of a new emulator thread: the code region of the process, prepared once, and an empty code cache in it", gate_threads },
#if HOST_CPU == CPU_ARM64
	{ "abi", "arm64: x19-x29, d8-d15 and sp given back by the main loop and RunBlock, through each of their exits", gate_abi },
#endif
#if defined(_WIN32)
	{ "unwind", "Windows: a stop of a check walked by the system's unwinder through the check stub, the block and the main loop", gate_unwind },
#endif
};

const u32 gate_count = sizeof(gates) / sizeof(gates[0]);
