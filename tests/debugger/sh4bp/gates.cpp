/*
	The gates. Each one names the gate of the design it stands for (B1 to B14) and runs for every engine it
	applies to: the x64 backend, the interpreter, and the x64 backend with the interpreter fallback forced.
*/
#include "gates.h"
#include "machine.h"
#include "guest.inc"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/dyna/ngen.h"

#include <cstring>
#include <set>
#include <vector>

using dbg::StopCause;

//hw/sh4/dyna/blockmanager.cpp: every cached block
extern std::set<RuntimeBlockInfo*> all_blocks;

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

	//with idleskip off a block costs one and a half times the sum of its instructions
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
	note("jit, idleskip off: time of %u steps %llu, of the run %llu: a block costs one and a half times its instructions, rounded down", n,
		(unsigned long long)a.time(), (unsigned long long)b.time());

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
	{ "b13", "B13 interpreter: n steps equal a run of n instructions", gate_b13 },
	{ "b14", "B14 jit: n steps equal a run to the n-th instruction", gate_b14 },
	{ "arm", "page discards on arming, blocks over two pages, category mask, breakpoints added in a stop", gate_arm },
};

const u32 gate_count = sizeof(gates) / sizeof(gates[0]);
