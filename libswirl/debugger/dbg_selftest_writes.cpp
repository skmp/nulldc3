/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of the debugger's writes: guest memory (dbg::mem::Write) and the registers of the two
	processors (dbg::regs::Write), with what each of them owes the machine: the compiled code of a page that is
	written, the texture cache, the breakpoint state behind a written PC, the register banks, the interrupt
	acceptance, the FP mode of compiled code.

	    devcast -config config:Headless=yes , audio:backend=none , Debugger:SelfTest=writes tests/debugger/guest/dsptest/dsptest.elf

	The guest is the sounding guest of the tree (tests/debugger/guest/dsptest, README.md there), whose addresses
	come from dsptest_labels.inc. Its SH4 counts in a loop of ten instructions, g_main_loop: one block of the
	recompiler, which goes back to itself by a PC-relative branch. The tests write into that loop. Two of its
	instructions compute r11 from r8 and r10 anew in every pass and nothing reads r11 (kSink0, kSink1), and the
	slot of its branch holds a nop (kSlot): an instruction a test puts there changes nothing of the guest but
	what the instruction itself writes, and a pass behind the restore the guest is what it would have been. The
	guest uses neither r12 to r14 nor an FP register, and its FPSCR is what reios leaves, with PR and SZ clear.

	Every test is a guest test: it stands where a client stands and is handed every stop. It begins by bringing
	the guest to where a session finds it behind its boot (Settle): the SH4 in its loop, the ARM7 clocked and in
	its own, nothing listed, no sound burst suspended, the code cache empty.

	Two kinds of stop are used, and which one a test takes is part of what it shows:

	  - a stop latched at a slice end (the event breakpoint on the timers of the AICA, which overflow in every
	    sound burst of the guest). It owes no remainder: the SH4 stands between two blocks, and the resume
	    enters its next block from the block cache. What compiled code does behind a write is seen from such
	    stops only;
	  - a stop at a code breakpoint of the SH4. On an engine that runs blocks the rest of the block is owed
	    (dbg_sh4_TakeRemainder) and is decoded from memory when the park context is left, in the FPSCR of that
	    moment, outside the block cache; a step decodes its instruction the same way. Such a stop shows what is
	    in memory and says nothing about the block cache.

	writesrun.neutral, at the end of this file, is no test of the session "writes": the run ends it
	(tests/debugger/selftest/README.md). Neither is smcwrites.cached, behind writes.mem.cached: it is the steps
	of that test in the one configuration in which they can fail for a missing discard, and it holds first that
	it runs in that configuration, which the session "writes" does not.
*/

#include "dbg_selftest.h"
#include "dbg_arm7.h"
#include "dbg_core_private.h"
#include "dbg_mem.h"
#include "dbg_regs.h"
#include "dbg_regs_private.h"
#include "dbg_sh4.h"

#include "libswirl.h"
#include "serialize.h"
#include "hw/aica/aica_mmio.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7_context.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"

#if FEAT_SHREC != DYNAREC_NONE
#include "hw/sh4/dyna/blockmanager.h"
#endif

#include "../../tests/debugger/guest/dsptest/dsptest_labels.inc"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace dbg;
using selftest::Check;
using selftest::Note;

typedef std::chrono::steady_clock Clock;

const u32 kResetVector = 0xA0000000;

// The SH4 program of the guest
const u32 kLoop    = DSPTEST_G_MAIN_LOOP;		// add #1, r8: the first instruction of the loop, and of its block
const u32 kMid     = kLoop + 6;					// xor r8, r9: an instruction in the middle of the block
const u32 kSink0   = kLoop + 12;				// mov r10, r11
const u32 kSink1   = kLoop + 14;				// and r8, r11
const u32 kBra     = kLoop + 16;				// bra g_main_loop
const u32 kSlot    = kLoop + 18;				// nop, in the slot of the branch
const u32 kLoopEnd = kLoop + 20;
const u32 kIrq     = DSPTEST_G_IRQ;				// the handler of the AICA's interrupt, at VBR + 0x600

// The ARM7 program of the guest
const u32 kArmLoop = DSPTEST_A_MAIN_LOOP;		// add r3, r3, #1: the first instruction of the loop
const u32 kArmLeaf = DSPTEST_A_LEAF;			// add r7, r7, #1
const u32 kArmNever = 0x000F0000;				// wave memory that the guest neither executes nor reads

// Main memory and video memory that the guest does not use
const u32 kRamScratch = 0x00300000;				// an offset into main memory
const u32 kVramScratch = 0x00100000;			// an offset into video memory, a multiple of 8

// Instructions of the SH4, as the words a test writes
const u16 kNop = 0x0009;
const u16 kFloatFpulFr2 = 0xF22D;				// float FPUL, FR2 (with PR set: float FPUL, DR2)
const u16 kFmovFr4Fr6 = 0xF64C;					// fmov FR4, FR6 (with SZ set: fmov DR4, DR6)

u16 MovImm(u32 n, u32 imm)
{
	return (u16)(0xE000 | (n << 8) | (imm & 0xFF));		// mov #imm, Rn
}

const u32 kMaskAll = (1u << (u32)Category::Events) | (1u << (u32)Category::Sh4) | (1u << (u32)Category::Arm7) | (1u << (u32)Category::Dsp);

// The registers of the ARM7 as the debugger reads them: R0..R14, the PC, the CPSR, the SPSR and the banks
const u32 kArmRegs = SPSR_FIQ + 1;

Sh4Context& Ctx()
{
	return p_sh4rcb->cntx;
}

double MsSince(Clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

/*
	A test as a routine that is left at every stop it waits for and entered again behind it (the switch of a
	protothread): CO_YIELD returns false to the driver, and the next call goes on behind it. What a test keeps
	across a stop is in a static struct, never in a local.
*/
struct Co {
	int line = 0;
};

#define CO_BEGIN(co)  switch ((co).line) { case 0:
#define CO_YIELD(co)  do { (co).line = __LINE__; return false; case __LINE__: ; } while (0)
#define CO_END(co)    } (co).line = 0; return true

// Many comparisons, one line: the number that did not hold and the first of them
struct Tally {
	u32 total = 0;
	u32 bad = 0;
	char first[240] = "";

	void Add(bool ok, const char* fmt, ...) DBG_PRINTF_LIKE(3, 4)
	{
		total++;

		if (ok)
			return;

		if (bad++ == 0)
		{
			va_list args;
			va_start(args, fmt);
			vsnprintf(first, sizeof(first), fmt, args);
			va_end(args);
		}
	}

	bool Report(const char* what)
	{
		if (bad == 0)
			return Check(total != 0, "%s: %u compared", what, total);

		return Check(false, "%s: %u of %u differ, first: %s", what, bad, total, first);
	}
};

/*
	The machine
*/
SoundCPU* Scpu()
{
	return sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
}

// A register of the ARM7 as its device gives it: the second source beside dbg::regs
u32 Arm(u32 index)
{
	u32 value = 0xDEADBEEF;

	Scpu()->DebugGetReg(index, &value);
	return value;
}

u32 ArmPc()
{
	return Arm(15);
}

bool ArmClocked()
{
	return Scpu()->DebugEnabled();
}

// Is a request of the AICA latched for the ARM7 and not answered yet?
bool ArmInService()
{
	u32 level = 0, in_service = 0;

	Scpu()->DebugGetInterrupt(&level, &in_service);
	return in_service != 0;
}

struct ArmRegs {
	u32 r[kArmRegs];
};

ArmRegs ReadArm()
{
	ArmRegs regs;

	for (u32 i = 0; i < kArmRegs; i++)
		regs.r[i] = Arm(i);

	return regs;
}

// The first register that differs, kArmRegs when none does
u32 ArmDiffers(const ArmRegs& a, const ArmRegs& b)
{
	for (u32 i = 0; i < kArmRegs; i++)
	{
		if (a.r[i] != b.r[i])
			return i;
	}

	return kArmRegs;
}

u32 Suspended()
{
	return virtualDreamcast->SoundBurstSuspended();
}

u32 Bursts()
{
	u32 counts[4];

	virtualDreamcast->SoundBurstCounts(counts);
	return counts[0];
}

// What the sound CPU writes into a save state, which is what the state hash folds of it: bool aica_interr, u32
// aica_reg_L, bool e68k_out, u32 e68k_reg_L, u32 e68k_reg_M, the RN_ARM_REG_COUNT words of the context, bool
// armIrqEnable, bool armFiqEnable, int armMode, bool enabled
const size_t kScpuWordsAt = 1 + 4 + 1 + 4 + 4;
const size_t kScpuStateSize = kScpuWordsAt + RN_ARM_REG_COUNT * 4 + 1 + 1 + 4 + 1;

// A word of the ARM7's context as it is stored, not as the debugger reads it
u32 StoredArmWord(u32 index)
{
	void* data = nullptr;
	unsigned int size = 0;

	Scpu()->serialize(&data, &size);

	std::vector<u8> state(size);

	data = state.data();
	size = 0;
	Scpu()->serialize(&data, &size);

	u32 word = 0xDEADBEEF;

	if (state.size() == kScpuStateSize)
		memcpy(&word, &state[kScpuWordsAt + index * 4], 4);

	return word;
}

// The whole machine as a save state has it: every device, the SH4 context and its on-chip modules, the
// scheduler, the three memories
bool MachineState(std::vector<u8>* state)
{
	void* data = nullptr;
	unsigned int size = 0;

	if (!dc_serialize(&data, &size))
		return false;

	state->resize(size);
	data = state->data();
	size = 0;
	return dc_serialize(&data, &size);
}

// What executes the SH4
struct Engine {
	bool known;
	bool blocks;		// the recompiler: a stop in a block owes the rest of it, and blocks are kept in a cache
	bool fallback;		// its breakpoints and steps run on the interpreter fallback
	const char* name;
};

Engine TheEngine()
{
	emu::Sh4EngineInfo info;
	Engine engine = { false, false, false, "unknown" };

	if (emu::GetSh4Engine(&info))
	{
		engine.known = true;
		engine.blocks = strcmp(info.backend, "interpreter") != 0;
		engine.fallback = info.fallback;
		engine.name = !engine.blocks ? "the interpreter" : engine.fallback ? "the recompiler, with the interpreter fallback forced" : "the recompiler";
	}

	return engine;
}

// Is the block that starts at addr in the block cache? Asked of the block manager, which only a recompiler
// that is selected has
bool BlockCached(u32 addr)
{
#if FEAT_SHREC != DYNAREC_NONE
	if (TheEngine().blocks)
		return bm_GetBlock(addr) != nullptr;
#endif
	(void)addr;
	return false;
}

/*
	Registers and memory through the debugger
*/
const regs::Node& Sh4Node()
{
	static const regs::Node* node = regs::Find("dc.sh4.cpu");

	return *node;
}

const regs::Node& Arm7Node()
{
	static const regs::Node* node = regs::Find("dc.aica.arm7");

	return *node;
}

u32 RowOf(const regs::Node& node, const char* name)
{
	const int index = regs::FindRegister(node, name);

	verify(index >= 0);
	return (u32)index;
}

// dbg::regs::Read of a row; a row that cannot be read gives a value no register has here
u64 Get(const regs::Node& node, u32 row)
{
	u64 value = 0xBAD0BAD0BAD0BAD0ull;

	regs::Read(node, row, &value, nullptr, 0);
	return value;
}

u32 GetS(const char* name)
{
	return (u32)Get(Sh4Node(), RowOf(Sh4Node(), name));
}

u32 GetA(const char* name)
{
	return (u32)Get(Arm7Node(), RowOf(Arm7Node(), name));
}

// dbg::regs::Write
int SetS(const char* name, u64 value)
{
	return regs::Write(Sh4Node(), RowOf(Sh4Node(), name), value);
}

int SetA(const char* name, u64 value)
{
	return regs::Write(Arm7Node(), RowOf(Arm7Node(), name), value);
}

// Every row of a node as dbg::regs reads it
struct Rows {
	std::vector<u64> v;
};

Rows ReadRows(const regs::Node& node)
{
	Rows rows;

	for (u32 i = 0; i < node.registerCount; i++)
		rows.v.push_back(node.registers[i].width == 0 ? 0 : Get(node, i));

	return rows;
}

// The first row that differs, the number of rows when none does
u32 RowsDiffer(const Rows& a, const Rows& b)
{
	for (size_t i = 0; i < a.v.size() && i < b.v.size(); i++)
	{
		if (a.v[i] != b.v[i])
			return (u32)i;
	}

	return (u32)a.v.size();
}

const char* RowName(const regs::Node& node, u32 row)
{
	return row < node.registerCount ? node.registers[row].name : "none";
}

bool Peek(Target t, u32 addr, u32 len, u8* out)
{
	return mem::Read(t, addr, len, out, nullptr) == len;
}

u16 Peek16(u32 addr)
{
	u16 word = 0xFFFF;

	mem::Read16(Target::Sh4, addr, &word);
	return word;
}

// dbg::mem::Write of one instruction of the SH4
int Poke16(u32 addr, u16 word)
{
	const u8 bytes[2] = { (u8)word, (u8)(word >> 8) };

	return mem::Write(Target::Sh4, addr, 2, bytes);
}

bool Same(const u8* a, const u8* b, u32 n)
{
	return memcmp(a, b, n) == 0;
}

/*
	The tables
*/
void Sh4Bps(std::initializer_list<std::pair<u32, u32>> list)
{
	u32 ids[16], addrs[16];
	u32 n = 0;

	for (const auto& bp : list)
	{
		ids[n] = bp.first;
		addrs[n] = bp.second;
		n++;
	}

	emu::SetSh4Breakpoints(ids, addrs, n);
}

void ArmBps(std::initializer_list<std::pair<u32, u32>> list)
{
	u32 ids[16], addrs[16];
	u32 n = 0;

	for (const auto& bp : list)
	{
		ids[n] = bp.first;
		addrs[n] = bp.second;
		n++;
	}

	emu::SetArm7Breakpoints(ids, addrs, n);
}

// No client is connected: a test that sets an event breakpoint opens the gate of its event, and closes it
void EventBp(u32 id)
{
	const u32 ids[1] = { id };
	const u8 events[1] = { (u8)EV_AICA_TIMER };

	emu::SetEventBreakpoints(ids, events, 1);
	g_event_on[EV_AICA_TIMER] = 1;
}

void NoEventBp()
{
	emu::SetEventBreakpoints(nullptr, nullptr, 0);
	g_event_on[EV_AICA_TIMER] = 0;
}

void ClearTables()
{
	Sh4Bps({ });
	ArmBps({ });
	NoEventBp();
	emu::SetActiveMask(kMaskAll);
}

/*
	The steps of every test
*/
bool Resume()
{
	int rv = emu::Resume();

	if (rv != 0)
		Check(false, "a resume was refused: %d", rv);

	return rv == 0;
}

bool StepSh4()
{
	const char* why = nullptr;
	int rv = emu::Step(Target::Sh4, StepKind::Into, &why);

	if (rv != 0)
		Check(false, "a step of the SH4 was refused: %d (%s)", rv, why ? why : "no reason");

	return rv == 0;
}

bool StepArm()
{
	int rv = emu::Step(Target::Arm7, StepKind::Into);

	if (rv != 0)
		Check(false, "a step of the ARM7 was refused: %d", rv);

	return rv == 0;
}

// The PC of the SH4 is in the loop, in whichever mirror of main memory
bool InLoop(u32 pc)
{
	const u32 at = pc & 0x00FFFFFF;

	return (pc & 0x1C000000) == 0x0C000000 && at >= (kLoop & 0x00FFFFFF) && at < (kLoopEnd & 0x00FFFFFF);
}

// A step of the SH4 ended behind its instruction, at `next`. A step that used its slice up does the work of a
// slice end behind the instruction: an interrupt that is accepted there leaves the SH4 at its vector, with the
// address behind the instruction as the one it returns to
bool SteppedTo(u32 next)
{
	return Ctx().pc == next || (Ctx().pc == kIrq && Ctx().spc == next);
}

// Not through the writer under test: the pc, the breakpoint state told, and the code cache emptied, so that no
// block of another mirror is found under the new pc
void GotoSh4(u32 pc)
{
	Ctx().pc = pc;
	sh4::OnPcWritten();
	sh4_cpu->ResetCache();
}

/*
	Settle: the guest as a session finds it behind its boot. The SH4 is sent into its loop through g_main, which
	opens the interrupts (through the release of the ARM7 when it is held; through the boot when the machine
	stands at its reset vector), and the run is stopped behind the first sound burst in which a timer of the AICA
	overflows: the ARM7 is in its loop then, and no burst is suspended. Returns false while it has armed something
*/
const u32 kSettleId = 900;

struct {
	Co co;
	u32 tries;
} settle;

bool Settle(const emu::RunState& stop)
{
	CO_BEGIN(settle.co);

	ClearTables();

	if (Ctx().pc != kResetVector)
		GotoSh4(ArmClocked() ? DSPTEST_G_MAIN : DSPTEST_G_RELEASE_ARM7);

	Sh4Bps({ { kSettleId, kLoop } });

	if (!Resume())
		break;
	CO_YIELD(settle.co);

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kSettleId || !ArmClocked())
		Check(false, "the guest gets to its main loop with the ARM7 released (a stop with \"%s\", SH4 pc %08X, the ARM7 %s)", StopCauseName(stop.cause),
			Ctx().pc, ArmClocked() ? "clocked" : "held");

	Sh4Bps({ });

	// the release puts the ARM7 on its reset vector; its program sets the timer up in the first sample
	for (settle.tries = 0; settle.tries < 8; settle.tries++)
	{
		EventBp(kSettleId);

		if (!Resume())
			break;
		CO_YIELD(settle.co);

		NoEventBp();

		if (stop.cause == StopCause::Event && Suspended() == SOUND_BURST_NONE && ArmClocked() && ArmPc() >= kArmLoop && ArmPc() < DSPTEST_A_FIQ_END + 8)
			break;
	}

	if (settle.tries == 8)
		Check(false, "the ARM7 of the guest gets to its loop, and a timer of the AICA overflows in a burst (a stop with \"%s\", the ARM7 at %08X)",
			StopCauseName(stop.cause), ArmPc());

	CO_END(settle.co);
}

/*
	A stop latched at a slice end, behind a sound burst and with none suspended: the event breakpoint on the
	timers of the AICA. It owes no remainder: what the resume from it runs first is entered from the block cache.
	Neither the breakpoint nor its gate is a breakpoint of the SH4: setting and clearing them leaves the compiled
	blocks alone. Returns false while it has armed something
*/
const u32 kSliceId = 901;

struct {
	Co co;
} slice;

bool SliceStop(const emu::RunState& stop)
{
	CO_BEGIN(slice.co);

	EventBp(kSliceId);

	if (!Resume())
		break;
	CO_YIELD(slice.co);

	NoEventBp();

	if (stop.cause != StopCause::Event || stop.breakpointId != kSliceId || Suspended() != SOUND_BURST_NONE)
		Check(false, "a stop behind a sound burst, at the event breakpoint on the timers of the AICA (a stop with \"%s\", burst %u)", StopCauseName(stop.cause),
			Suspended());

	CO_END(slice.co);
}

// ... with the PC of the SH4 in the loop. On the recompiler every such stop has it on the first instruction of
// the loop, where its block starts; the interpreter can stand anywhere in the loop, and inside g_irq
struct {
	Co co;
	u32 tries;
} loop_stop;

bool LoopStop(const emu::RunState& stop)
{
	CO_BEGIN(loop_stop.co);

	for (loop_stop.tries = 0; loop_stop.tries < 64; loop_stop.tries++)
	{
		while (!SliceStop(stop))
			CO_YIELD(loop_stop.co);

		if (InLoop(Ctx().pc))
			break;
	}

	if (!InLoop(Ctx().pc))
		Check(false, "a stop at a slice end with the SH4 in its loop (pc %08X)", Ctx().pc);

	CO_END(loop_stop.co);
}

// A stop at the code breakpoint `id` of the SH4 on addr; the set of the SH4's breakpoints is the caller's
struct {
	Co co;
} at_bp;

bool StopAt(const emu::RunState& stop, u32 id, u32 addr)
{
	CO_BEGIN(at_bp.co);

	if (!Resume())
		break;
	CO_YIELD(at_bp.co);

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != id || Ctx().pc != addr)
		Check(false, "a stop at the breakpoint %u of the SH4 on %08X (a stop with \"%s\" id %u, pc %08X)", id, addr, StopCauseName(stop.cause), stop.breakpointId,
			Ctx().pc);

	CO_END(at_bp.co);
}

/*
	A stop at a breakpoint of the ARM7 on the first instruction of its loop, which stays listed: the burst is
	suspended in front of it, the ARM7 is in the mode of its loop, and no request of the AICA waits, so that
	nothing the test writes lets a FIQ in
*/
const u32 kArmId = 902;

struct {
	Co co;
	u32 n;
} arm_stop;

bool ArmStop(const emu::RunState& stop)
{
	CO_BEGIN(arm_stop.co);

	for (arm_stop.n = 0; arm_stop.n < 400; arm_stop.n++)
	{
		ArmBps({ { kArmId, kArmLoop } });

		if (!Resume())
			break;
		CO_YIELD(arm_stop.co);

		if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kArmId || ArmPc() != kArmLoop || Suspended() != SOUND_BURST_ARM7)
		{
			Check(false, "a stop at a breakpoint of the ARM7 on %08X, the burst suspended (a stop with \"%s\" id %u, the ARM7 at %08X, burst %u)", kArmLoop,
				StopCauseName(stop.cause), stop.breakpointId, ArmPc(), Suspended());
			break;
		}

		if (!ArmInService())
			break;
	}

	CO_END(arm_stop.co);
}

/*
	Steps of the ARM7 from a suspended burst until the burst is over: returns true at the stop of the step whose
	instruction was the last of its burst, or at the first stop that is not what a step of a suspended burst
	gives, or behind kStepsMax steps with the burst still suspended
*/
const u32 kStepsMax = 100000;

struct {
	Co co;
	u32 steps;
	bool bad;
} step_end;

bool StepToBurstEnd(const emu::RunState& stop)
{
	CO_BEGIN(step_end.co);

	step_end.steps = 0;
	step_end.bad = false;

	while (Suspended() != SOUND_BURST_NONE && step_end.steps < kStepsMax)
	{
		if (!StepArm())
			break;
		CO_YIELD(step_end.co);

		step_end.steps++;

		if (stop.cause != StopCause::Step || stop.lifetime != Lifetime::Parked)
		{
			step_end.bad = true;
			break;
		}
	}

	CO_END(step_end.co);
}

/*
	The other thread: what another thread of the debugger does (a call)
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

}

/*
	writes.mem.ram: main memory through its mirrors
*/
namespace {

struct {
	Co co;
} m_ram;

}

DBG_SELFTEST_GUEST(writes_mem_ram, "writes.mem.ram")
{
	static const u32 bases[3] = { 0x0C000000, 0x8C000000, 0xAC000000 };
	u8 saved[32], pattern[16], seen[16], low[8], high[8];
	Tally mirrors;

	CO_BEGIN(m_ram.co);

	while (!Settle(stop))
		CO_YIELD(m_ram.co);

	Check(sh4_cpu->mram.size == 0x01000000, "main memory is 16 MiB (%X)", sh4_cpu->mram.size);
	Check(Peek(Target::Sh4, 0x8C000000 + kRamScratch, 32, saved), "(the scratch bytes of the test are read: 32 at %08X)", 0x8C000000 + kRamScratch);

	// each of the three regions that map main memory: written through it, read through the two others, and by
	// the guest's own access
	for (u32 w = 0; w < 3; w++)
	{
		for (u32 i = 0; i < 16; i++)
			pattern[i] = (u8)(0x31 + 0x40 * w + 7 * i);

		mirrors.Add(mem::Write(Target::Sh4, bases[w] + kRamScratch, 16, pattern) == 0, "a write of 16 bytes at %08X is refused", bases[w] + kRamScratch);

		for (u32 r = 0; r < 3; r++)
			mirrors.Add(Peek(Target::Sh4, bases[r] + kRamScratch, 16, seen) && Same(seen, pattern, 16), "written at %08X, read at %08X", bases[w] + kRamScratch,
				bases[r] + kRamScratch);

		mirrors.Add(Same(GetMemPtr(0x8C000000 + kRamScratch, 16), pattern, 16), "written at %08X: the guest reads other bytes at %08X", bases[w] + kRamScratch,
			0x8C000000 + kRamScratch);

		// the store is there four times in each region: 16 MiB up it is the same byte
		for (u32 i = 0; i < 16; i++)
			pattern[i] ^= 0xFF;

		mirrors.Add(mem::Write(Target::Sh4, bases[w] + 0x03000000 + kRamScratch + 16, 16, pattern) == 0 && Peek(Target::Sh4, bases[(w + 1) % 3] + kRamScratch + 16, 16, seen)
			&& Same(seen, pattern, 16), "written at %08X, read at %08X", bases[w] + 0x03000000 + kRamScratch + 16, bases[(w + 1) % 3] + kRamScratch + 16);
	}

	mirrors.Report("main memory is one store behind 0x0C.., 0x8C.. and 0xAC.., four times in each: written through one address and read through the others");

	Check(mem::Write(Target::Sh4, 0x8C000000 + kRamScratch, 32, saved) == 0 && Peek(Target::Sh4, 0x0C000000 + kRamScratch, 16, seen) && Same(seen, saved, 16),
		"the scratch bytes are written back");

	// a length of 0: nothing to refuse, nothing written
	Check(mem::Write(Target::Sh4, 0x8C000000 + kRamScratch, 0, pattern) == 0 && Same(GetMemPtr(0x8C000000 + kRamScratch, 16), saved, 16),
		"a write of no bytes is taken and writes nothing");

	// a range runs from one mirror of the store into the next: the end of main memory and its start
	Check(Peek(Target::Sh4, 0x8CFFFFF8, 8, high) && Peek(Target::Sh4, 0x8C000000, 8, low), "(the last 8 and the first 8 bytes of main memory are read)");

	for (u32 i = 0; i < 16; i++)
		pattern[i] = (u8)(0xC1 + 3 * i);

	Check(mem::Write(Target::Sh4, 0x8CFFFFF8, 16, pattern) == 0 && Peek(Target::Sh4, 0x0CFFFFF8, 8, seen) && Same(seen, pattern, 8) && Peek(Target::Sh4, 0xAC000000, 8, seen)
		&& Same(seen, pattern + 8, 8), "16 bytes at 8CFFFFF8 run from one mirror of the store into the next: 8 at its end, 8 at its start");
	Check(mem::Write(Target::Sh4, 0x8CFFFFF8, 8, high) == 0 && mem::Write(Target::Sh4, 0x8C000000, 8, low) == 0 && Peek(Target::Sh4, 0x8CFFFFF8, 8, seen) && Same(seen, high, 8)
		&& Peek(Target::Sh4, 0x8C000000, 8, seen) && Same(seen, low, 8), "(both are written back)");

	// all of a range or nothing of it: across the end of the region
	for (u32 r = 0; r < 3; r++)
	{
		Check(mem::Write(Target::Sh4, bases[r] + 0x03FFFFF8, 16, pattern) == kErrNotWritable && Peek(Target::Sh4, 0x8CFFFFF8, 8, seen) && Same(seen, high, 8),
			"16 bytes at %08X, across the end of main memory, are refused whole (-32013): the first 8, which are main memory, are unchanged", bases[r] + 0x03FFFFF8);
	}

	// the bound of state.writeMemory, in one write
	{
		std::vector<u8> big(65536), was(65536 + 32), now(65536 + 32);
		const u32 at = 0x8C000000 + kRamScratch + 0x10000;

		for (size_t i = 0; i < big.size(); i++)
			big[i] = (u8)(i * 31 + (i >> 8));

		Peek(Target::Sh4, at - 16, 65536 + 32, was.data());

		const int rv = mem::Write(Target::Sh4, at, 65536, big.data());

		Peek(Target::Sh4, at - 16, 65536 + 32, now.data());

		Check(rv == 0 && memcmp(now.data() + 16, big.data(), 65536) == 0 && memcmp(now.data(), was.data(), 16) == 0
			&& memcmp(now.data() + 16 + 65536, was.data() + 16 + 65536, 16) == 0, "65536 bytes in one write: each of them is stored, and the 16 in front and behind are not touched");
		Check(mem::Write(Target::Sh4, at, 65536, was.data() + 16) == 0, "(they are written back)");
	}

	CO_END(m_ram.co);
}

/*
	writes.mem.code: an instruction that is written executes. By a step, at the PC of the stop and ahead of it;
	and by a resume from a breakpoint stop in the middle of a block, where the rest of the block is owed: it is
	decoded from memory as it is when the park context is left. Neither says anything about the block cache: a
	step and an owed rest are decoded outside it (writes.mem.cached has that)
*/
namespace {

struct {
	Co co;
	u16 w0, w1;
	u32 r12, r13;
} m_code;

}

DBG_SELFTEST_GUEST(writes_mem_code, "writes.mem.code")
{
	u32 end = 0, cycles = 0;

	CO_BEGIN(m_code.co);

	while (!Settle(stop))
		CO_YIELD(m_code.co);

	m_code.w0 = Peek16(kSink0);
	m_code.w1 = Peek16(kSink1);
	m_code.r12 = Ctx().r[12];
	m_code.r13 = Ctx().r[13];

	Check(m_code.w0 == 0x6BA3 && m_code.w1 == 0x2B89, "the two instructions of the loop that compute r11 are mov r10, r11 and and r8, r11 (%04X %04X)", m_code.w0, m_code.w1);

	Sh4Bps({ { 1, kSink0 } });

	while (!StopAt(stop, 1, kSink0))
		CO_YIELD(m_code.co);

	// by a step: the instruction at the pc of the stop, and the one ahead of it
	Check(Poke16(kSink0, MovImm(12, 0x31)) == 0 && Poke16(kSink1, MovImm(13, 0x32)) == 0 && Peek16(kSink0) == MovImm(12, 0x31) && Peek16(kSink1) == MovImm(13, 0x32),
		"mov #0x31, r12 is written at the pc of the stop (%08X) and mov #0x32, r13 behind it", kSink0);

	if (!StepSh4())
		break;
	CO_YIELD(m_code.co);

	Check(stop.cause == StopCause::Step && SteppedTo(kSink1) && Ctx().r[12] == 0x31, "a step executes what was written at the pc: r12 is %08X, the SH4 at %08X", Ctx().r[12],
		Ctx().pc);

	if (Ctx().pc == kSink1)
	{
		if (!StepSh4())
			break;
		CO_YIELD(m_code.co);

		Check(stop.cause == StopCause::Step && SteppedTo(kBra) && Ctx().r[13] == 0x32, "the next step executes what was written ahead of it: r13 is %08X, the SH4 at %08X",
			Ctx().r[13], Ctx().pc);
	}

	Check(Poke16(kSink0, m_code.w0) == 0 && Poke16(kSink1, m_code.w1) == 0, "(the two instructions of the loop are written back)");

	// By a resume, under an owed remainder. The breakpoint on the branch is listed before the write: the rest of
	// the block runs into it
	Sh4Bps({ { 1, kSink0 }, { 2, kBra } });

	while (!StopAt(stop, 1, kSink0))
		CO_YIELD(m_code.co);

	Check(Poke16(kSink0, MovImm(12, 0x41)) == 0 && Poke16(kSink1, MovImm(13, 0x42)) == 0, "at the next stop of the breakpoint, in the middle of the block, the two are written again, "
		"with 0x41 and 0x42");

	Ctx().r[12] = 0;
	Ctx().r[13] = 0;

	while (!StopAt(stop, 2, kBra))
		CO_YIELD(m_code.co);

	Check(Ctx().r[12] == 0x41 && Ctx().r[13] == 0x42, "a resume from there runs what was written, up to the breakpoint on the branch: r12 is %08X and r13 %08X. On %s the rest "
		"of the block %s", Ctx().r[12], Ctx().r[13], TheEngine().name, TheEngine().blocks ? "was owed, and was decoded from memory as it was at the leave" : "is no unit: "
		"every instruction is read when it executes");

	Check(Poke16(kSink0, m_code.w0) == 0 && Poke16(kSink1, m_code.w1) == 0 && Peek16(kSink0) == m_code.w0 && Peek16(kSink1) == m_code.w1,
		"(the two instructions of the loop are written back)");

	// what such a stop owes on this engine, said once
	Sh4Bps({ { 1, kSink0 } });

	while (!StopAt(stop, 1, kSink0))
		CO_YIELD(m_code.co);

	{
		const bool owed = dbg_sh4_TakeRemainder(Ctx().pc, &end, &cycles);

		Check(owed == TheEngine().blocks && (!owed || end == kLoopEnd), "a breakpoint stop in the middle of the block owes the rest of it on an engine that runs blocks, "
			"and nothing on the interpreter: on %s %s (the rest ends at %08X)", TheEngine().name, owed ? "it is owed" : "nothing is owed", end);
	}

	Sh4Bps({ });
	Ctx().r[12] = m_code.r12;
	Ctx().r[13] = m_code.r13;

	CO_END(m_code.co);
}

/*
	writes.mem.cached: the witness of the discard. A write into main memory discards the compiled blocks of
	every page it touches, so that code which the dispatcher enters from the block cache is the code that is in
	memory.

	The stops are latched at slice ends: nothing is owed, and the resume enters the block of the loop from the
	cache. The first write makes the page of the loop a page that holds data: from then on its blocks are not
	write-protected. The run to the next stop compiles the block of the loop again. The second write puts an
	instruction into the slot of the loop's branch, and the next stop reads what it left. Then three ranges of
	more than one page, each with the slot among its bytes and the bytes that are there everywhere else, so that
	every page of a range is witnessed in one of them: one over three pages with the page of the loop in the
	middle, one of two pages that ends in the page of the loop, and one of two pages that begins in it. A writer
	that discarded the blocks of the first page of a range alone, or of its first and its last, leaves the loop's
	block behind the first of them; one that leaves out the last page of a range, behind the second; one that
	leaves out the first page, behind the third.

	What a writer without the discard would do depends on the configuration. With Dynarec.SmcCheckLevel at its
	default every block of a page that holds data compares its code bytes when it is entered and discards itself:
	this test then passes with or without the discard. The witness is the run with Dynarec.SmcCheckLevel=-1, in
	which the block of the loop has no such check and a store without the discard leaves it in the cache: there
	the stale nop executes, and the test fails. That run is the test smcwrites.cached, behind this one: the same
	steps, and in front of them the check that the configuration is that one
*/
namespace {

struct {
	Co co;
	u16 slot;
	u32 r12;
	u32 range;				// of kRanges
} m_cached;

// The ranges of more than one page, each with the slot of the loop's branch among its bytes: where it begins and
// where it ends, by the page of the loop and REI_PAGE_SIZE, and the immediate of the mov that is put into the slot
struct CachedRange {
	const char* what;
	u32 from, to;			// [from, to)
	u32 imm;
};

CachedRange Range(u32 which)
{
	const u32 page = kSlot & ~(u32)(REI_PAGE_SIZE - 1);

	switch (which)
	{
	case 0:
		return { "begins in the page in front of the loop's and ends in the page behind it: the blocks of a page in the middle of the range were discarded",
			page - 16, page + REI_PAGE_SIZE + 16, 0x5D };
	case 1:
		return { "begins in the page in front of the loop's and ends in the loop's, behind the slot: the blocks of the last page of the range were discarded",
			page - 16, kSlot + 2, 0x5E };
	default:
		return { "begins in the loop's page, at the slot, and ends in the page behind it: the blocks of the first page of the range were discarded",
			kSlot, page + REI_PAGE_SIZE + 16, 0x5F };
	}
}

const u32 kRanges = 3;

// One write of a range: the bytes that are there, but for mov #imm, r12 in the slot; r12 cleared in front of it
bool WriteRange(const CachedRange& range)
{
	const u32 n = range.to - range.from;
	std::vector<u8> bytes(n);
	const bool read = Peek(Target::Sh4, range.from, n, bytes.data());

	bytes[kSlot - range.from] = (u8)MovImm(12, range.imm);
	bytes[kSlot - range.from + 1] = (u8)(MovImm(12, range.imm) >> 8);

	return read && SetS("R12", 0) == 0 && mem::Write(Target::Sh4, range.from, n, bytes.data()) == 0 && Peek16(kSlot) == MovImm(12, range.imm);
}

// The steps of writes.mem.cached and of smcwrites.cached
bool CachedWitness(const emu::RunState& stop)
{
	CO_BEGIN(m_cached.co);

	while (!Settle(stop))
		CO_YIELD(m_cached.co);

	while (!LoopStop(stop))
		CO_YIELD(m_cached.co);

	m_cached.slot = Peek16(kSlot);
	m_cached.r12 = Ctx().r[12];

	Check(m_cached.slot == kNop && Peek16(kBra) == 0xAFF6, "the loop ends in bra g_main_loop with a nop in its slot (%04X %04X)", Peek16(kBra), m_cached.slot);
	Note("the SH4 runs on %s; Dynarec.SmcCheckLevel is %d (0, the default: a block of a page that holds data checks its code at entry; -1: it does not)", TheEngine().name,
		(int)settings.dynarec.SmcCheckLevel);

	// the first write: the bytes that are there. The page holds data from here on
	Check(Poke16(kLoop, Peek16(kLoop)) == 0, "at a stop that owes nothing, the first instruction of the loop is written with the word it has: the page of the loop is a "
		"page that was written");

	while (!LoopStop(stop))
		CO_YIELD(m_cached.co);

	if (TheEngine().blocks)
		Check(BlockCached(kLoop), "a sound burst on, the block of the loop is in the block cache again");

	// the second write: the instruction whose effect is the witness
	Check(SetS("R12", 0) == 0 && Poke16(kSlot, MovImm(12, 0x5C)) == 0, "at the next such stop, r12 is cleared and mov #0x5C, r12 is written into the slot of the loop's branch");

	while (!LoopStop(stop))
		CO_YIELD(m_cached.co);

	Check(Ctx().r[12] == 0x5C, "behind a resume that entered the loop from the block cache, r12 is %08X: the code that runs is the code that is in memory", Ctx().r[12]);

	// and back
	Check(Poke16(kSlot, kNop) == 0 && SetS("R12", 0x77) == 0, "the nop is written back, and r12 set to 0x77");

	while (!LoopStop(stop))
		CO_YIELD(m_cached.co);

	Check(Ctx().r[12] == 0x77 && Peek16(kSlot) == kNop, "behind the next resume r12 is %08X: the instruction that was taken out no longer runs", Ctx().r[12]);

	// Three writes of a range of more than one page: over three pages with the loop's in the middle, of two pages
	// that end in the loop's, and of two pages that begin in it. Each holds the bytes that are there, but for the
	// slot
	for (m_cached.range = 0; m_cached.range < kRanges; m_cached.range++)
	{
		{
			const CachedRange range = Range(m_cached.range);

			if (TheEngine().blocks)
				Check(BlockCached(kLoop), "the block of the loop is in the block cache, compiled with the nop in its slot");

			Check(WriteRange(range), "r12 is cleared, and one write of %u bytes from %08X to %08X puts mov #0x%02X, r12 into the slot", range.to - range.from, range.from,
				range.to - 1, range.imm);
		}

		while (!LoopStop(stop))
			CO_YIELD(m_cached.co);

		Check(Ctx().r[12] == Range(m_cached.range).imm, "behind a resume that entered the loop from the block cache, r12 is %08X: the write of the range that %s",
			Ctx().r[12], Range(m_cached.range).what);

		Check(Poke16(kSlot, kNop) == 0 && SetS("R12", 0x77) == 0, "(the nop is written back, and r12 set to 0x77)");

		while (!LoopStop(stop))
			CO_YIELD(m_cached.co);

		Check(Ctx().r[12] == 0x77 && Peek16(kSlot) == kNop, "(behind the next resume r12 is %08X)", Ctx().r[12]);
	}

	SetS("R12", m_cached.r12);

	CO_END(m_cached.co);
}

}

DBG_SELFTEST_GUEST(writes_mem_cached, "writes.mem.cached")
{
	return CachedWitness(stop);
}

/*
	smcwrites.cached: writes.mem.cached where a store without the discard shows (the registration
	debugger.selftest.writes.smc). It is no test of the session "writes", which runs in the default
	configuration: it first holds that this run is not that one. Without the two checks a run that the key
	Dynarec.SmcCheckLevel=-1 did not reach, or one on the interpreter, would be the default run of the steps
	under another name, which passes with or without the discard.

	A build without a recompiler of the SH4 compiles nothing: a write has no block to discard there, and the
	steps show what an interpreter shows. The test says so and asks for the level alone
*/
DBG_SELFTEST_GUEST(smcwrites_cached, "smcwrites.cached")
{
	// in front of the first step
	if (m_cached.co.line == 0)
	{
#if FEAT_SHREC != DYNAREC_NONE
		Check(TheEngine().blocks, "the SH4 runs on a recompiler: compiled blocks are kept in a block cache (%s)", TheEngine().name);
#else
		Note("this build has no recompiler of the SH4: no code is compiled, a write has no block to discard, and the steps show what an interpreter shows");
#endif
		Check(settings.dynarec.SmcCheckLevel == NoCheck, "Dynarec.SmcCheckLevel is %d, and has to be -1: no block of a page that holds data checks its code when it is entered, "
			"so that a block which a write left in the cache would run as it was compiled", (int)settings.dynarec.SmcCheckLevel);
	}

	return CachedWitness(stop);
}

/*
	writes.mem.bppage: a write into a page with an armed breakpoint. No armed address is a patch in memory: the
	blocks of the page are compiled again from the new bytes, with their checks
*/
namespace {

struct {
	Co co;
	u32 r12, r8;
} m_bp;

}

DBG_SELFTEST_GUEST(writes_mem_bppage, "writes.mem.bppage")
{
	CO_BEGIN(m_bp.co);

	while (!Settle(stop))
		CO_YIELD(m_bp.co);

	m_bp.r12 = Ctx().r[12];

	Sh4Bps({ { 5, kMid } });

	while (!StopAt(stop, 5, kMid))
		CO_YIELD(m_bp.co);

	m_bp.r8 = Ctx().r[8];

	Check(dbg_sh4_IsArmed(kMid) && SetS("R12", 0) == 0 && Poke16(kSlot, MovImm(12, 0x5B)) == 0 && Poke16(kMid, Peek16(kMid)) == 0,
		"at the stop of a breakpoint in the loop, the slot of the loop's branch is written with mov #0x5B, r12, and the breakpoint's own instruction with the word it has");

	while (!StopAt(stop, 5, kMid))
		CO_YIELD(m_bp.co);

	Check(Ctx().r[12] == 0x5B && Ctx().r[8] == m_bp.r8 + 1, "the breakpoint still stops, one pass on (%u), and what was written into its page has run: r12 is %08X",
		Ctx().r[8] - m_bp.r8, Ctx().r[12]);

	Check(Poke16(kSlot, kNop) == 0 && SetS("R12", 0x66) == 0, "the nop is written back");

	while (!StopAt(stop, 5, kMid))
		CO_YIELD(m_bp.co);

	Check(Ctx().r[12] == 0x66 && Ctx().r[8] == m_bp.r8 + 2, "and the breakpoint stops again, a pass on, with r12 as it was set (%08X)", Ctx().r[12]);

	Sh4Bps({ });
	SetS("R12", m_bp.r12);

	CO_END(m_bp.co);
}

/*
	writes.mem.vram: video memory through its two views. The 64-bit view is the store; the 32-bit view has the
	two banks of it one behind the other: its word w of bank b is the word 2 w + b of the store
*/
namespace {

struct {
	Co co;
} m_vram;

// Where the byte at offset32 of the 32-bit view lives in the store: written out here, not asked of the emulator
u32 StoreOfView32(u32 offset32)
{
	const u32 bank = (offset32 >> 22) & 1;
	const u32 word = (offset32 & 0x003FFFFC) >> 2;

	return (word * 2 + bank) * 4 + (offset32 & 3);
}

}

DBG_SELFTEST_GUEST(writes_mem_vram, "writes.mem.vram")
{
	u8 saved[64], pattern[16], seen[64], expect[64];
	Tally views;
	const u32 x = kVramScratch;

	CO_BEGIN(m_vram.co);

	while (!Settle(stop))
		CO_YIELD(m_vram.co);

	Check(sh4_cpu->vram.size == 0x00800000, "video memory is 8 MiB (%X)", sh4_cpu->vram.size);
	Check(Peek(Target::Sh4, 0x04000000 + x, 64, saved), "(the scratch bytes of the test are read: 64 at %08X)", 0x04000000 + x);

	// written through the 64-bit view
	for (u32 i = 0; i < 16; i++)
		pattern[i] = (u8)(0x90 + i);

	views.Add(mem::Write(Target::Sh4, 0x04000000 + x, 16, pattern) == 0, "a write of 16 bytes at %08X is refused", 0x04000000 + x);

	for (u32 base : { 0x04000000u, 0x06000000u, 0xA4000000u, 0x84800000u })
		views.Add(Peek(Target::Sh4, base + x, 16, seen) && Same(seen, pattern, 16), "written at %08X, read at %08X", 0x04000000 + x, base + x);

	views.Add(Same(&sh4_cpu->vram.data[x], pattern, 16), "written at %08X: the store has other bytes at %X", 0x04000000 + x, x);

	// each byte of it through the 32-bit view
	for (u32 i = 0; i < 16; i++)
	{
		// byte i of the store is in bank (i / 4) & 1, word (x + i) / 8 of that bank
		const u32 offset32 = (((i >> 2) & 1) << 22) | (((x + i) >> 3) << 2) | (i & 3);
		u8 byte = 0;

		views.Add(StoreOfView32(offset32) == x + i, "(the test's map of the 32-bit view: %X is not %X)", StoreOfView32(offset32), x + i);

		for (u32 base : { 0x05000000u, 0x07000000u, 0xA5000000u })
			views.Add(Peek(Target::Sh4, base + offset32, 1, &byte) && byte == pattern[i], "byte %u written through the 64-bit view reads %02X at %08X of the 32-bit view", i, byte,
				base + offset32);
	}

	// Written through the 32-bit view: 16 bytes of bank 0 behind one another are every other word of the store,
	// and 16 bytes of bank 1 the words between them
	memcpy(expect, saved, 64);
	memcpy(expect, pattern, 16);

	for (u32 bank = 0; bank < 2; bank++)
	{
		const u32 offset32 = (bank << 22) + x / 2 + 8;

		for (u32 i = 0; i < 16; i++)
		{
			pattern[i] = (u8)(0x40 + 0x20 * bank + i);
			expect[StoreOfView32(offset32 + i) - x] = pattern[i];
		}

		views.Add(mem::Write(Target::Sh4, (bank ? 0xA7000000 : 0x05000000) + offset32, 16, pattern) == 0, "a write of 16 bytes at %08X is refused",
			(bank ? 0xA7000000 : 0x05000000) + offset32);
		views.Add(Peek(Target::Sh4, 0x05000000 + offset32, 16, seen) && Same(seen, pattern, 16) && Peek(Target::Sh4, 0x07000000 + offset32, 16, seen) && Same(seen, pattern, 16),
			"written through the 32-bit view at offset %X, read back through it", offset32);
	}

	views.Add(Peek(Target::Sh4, 0x04000000 + x, 64, seen) && Same(seen, expect, 64) && Same(&sh4_cpu->vram.data[x], expect, 64),
		"the 64-bit view has the bytes of the two writes through the 32-bit view word by word in turn, and the bytes between them as they were");

	views.Report("video memory: both views show the bytes, written through either, at every address that maps them");

	// a range from one view into the other is video memory all through
	{
		u8 edge[16], was[16];

		Peek(Target::Sh4, 0x04FFFFF8, 16, was);

		for (u32 i = 0; i < 16; i++)
			edge[i] = (u8)(0xE0 + i);

		Check(mem::Write(Target::Sh4, 0x04FFFFF8, 16, edge) == 0 && Peek(Target::Sh4, 0x04FFFFF8, 16, seen) && Same(seen, edge, 16) && Same(&sh4_cpu->vram.data[0x7FFFF8], edge, 8)
			&& Same(&sh4_cpu->vram.data[0], edge + 8, 4) && Same(&sh4_cpu->vram.data[8], edge + 12, 4), "16 bytes at 04FFFFF8 run from the 64-bit view into the 32-bit view: "
			"8 at the end of the store, and the first two words of bank 0, which are its words 0 and 2");
		Check(mem::Write(Target::Sh4, 0x04FFFFF8, 16, was) == 0, "(they are written back)");
	}

	Check(mem::Write(Target::Sh4, 0x04000000 + x, 64, saved) == 0 && Same(&sh4_cpu->vram.data[x], saved, 64), "the scratch bytes are written back");

	CO_END(m_vram.co);
}

/*
	writes.mem.aram: wave memory, which the SH4 has at 0x00800000 and the ARM7 at 0
*/
namespace {

struct {
	Co co;
} m_aram;

}

DBG_SELFTEST_GUEST(writes_mem_aram, "writes.mem.aram")
{
	u8 saved[8], pattern[8], seen[8];
	Tally views;
	const u32 x = kArmNever;
	const u32 size = sh4_cpu->aica_ram.size;

	CO_BEGIN(m_aram.co);

	while (!Settle(stop))
		CO_YIELD(m_aram.co);

	Check(size == 0x00200000 || size == 0x00800000, "wave memory is 2 MiB, or 8 MiB (%X)", size);
	Check(Peek(Target::Arm7, x, 8, saved), "(the scratch bytes of the test are read: 8 at %08X of the ARM7)", x);

	// written through the ARM7's address
	for (u32 i = 0; i < 8; i++)
		pattern[i] = (u8)(0xA1 + 5 * i);

	views.Add(mem::Write(Target::Arm7, x, 8, pattern) == 0, "a write of 8 bytes at %08X of the ARM7 is refused", x);
	views.Add(Peek(Target::Arm7, x, 8, seen) && Same(seen, pattern, 8), "written at %08X of the ARM7, read there", x);
	views.Add(Peek(Target::Sh4, 0x00800000 + x, 8, seen) && Same(seen, pattern, 8), "written at %08X of the ARM7, read at %08X of the SH4", x, 0x00800000 + x);
	views.Add(Peek(Target::Sh4, 0xA0800000 + x, 8, seen) && Same(seen, pattern, 8), "written at %08X of the ARM7, read at %08X of the SH4", x, 0xA0800000 + x);
	views.Add(Same(&sh4_cpu->aica_ram.data[x], pattern, 8), "written at %08X of the ARM7: the store has other bytes", x);

	// written through the SH4's, and through a mirror of the store
	for (u32 i = 0; i < 8; i++)
		pattern[i] = (u8)(0x1B + 9 * i);

	views.Add(mem::Write(Target::Sh4, 0xA0800000 + x, 8, pattern) == 0, "a write of 8 bytes at %08X of the SH4 is refused", 0xA0800000 + x);
	views.Add(Peek(Target::Arm7, x, 8, seen) && Same(seen, pattern, 8), "written at %08X of the SH4, read at %08X of the ARM7", 0xA0800000 + x, x);

	if (size == 0x00200000)
	{
		for (u32 i = 0; i < 8; i++)
			pattern[i] ^= 0x55;

		views.Add(mem::Write(Target::Arm7, x + size, 8, pattern) == 0 && Peek(Target::Sh4, 0x00800000 + x + 2 * size, 8, seen) && Same(seen, pattern, 8)
			&& Same(&sh4_cpu->aica_ram.data[x], pattern, 8), "written at %08X of the ARM7, a mirror: read at %08X of the SH4, another", x + size, 0x00800000 + x + 2 * size);
	}

	views.Report("wave memory: both targets show the bytes, written through either");

	Check(mem::Write(Target::Arm7, x, 8, saved) == 0 && Same(&sh4_cpu->aica_ram.data[x], saved, 8), "the scratch bytes are written back");

	CO_END(m_aram.co);
}

/*
	writes.mem.refused: what is no memory of the three kinds is not written, and a range that has such a byte is
	refused whole
*/
namespace {

struct {
	Co co;
} m_refused;

struct Refusal {
	Target target;
	u32 addr;
	u32 len;
	const char* what;
};

const Refusal refusals[] = {
	{ Target::Sh4,  0x00000000, 4,  "the boot ROM" },
	{ Target::Sh4,  0xA0000000, 4,  "the boot ROM, where the SH4 starts" },
	{ Target::Sh4,  0x001FFFFC, 4,  "the end of the boot ROM" },
	{ Target::Sh4,  0x00200000, 4,  "the flash memory" },
	{ Target::Sh4,  0xA021FFFC, 4,  "the end of the flash memory" },
	{ Target::Sh4,  0x005F6800, 4,  "the system bus registers" },
	{ Target::Sh4,  0x005F7018, 4,  "the registers of the GD-ROM" },
	{ Target::Sh4,  0x005F7C00, 4,  "the system bus registers behind them" },
	{ Target::Sh4,  0xA05F8000, 4,  "the PVR registers" },
	{ Target::Sh4,  0x00600000, 4,  "the modem" },
	{ Target::Sh4,  0x00700000, 4,  "the registers of the AICA" },
	{ Target::Sh4,  0xA0702C00, 1,  "ARMRST: the debugger does not hold or release the ARM7" },
	{ Target::Sh4,  0x00710000, 4,  "the RTC" },
	{ Target::Sh4,  0x1F000000, 4,  "the on-chip registers at their area 7 addresses" },
	{ Target::Sh4,  0xFF000010, 4,  "the on-chip registers in P4" },
	{ Target::Sh4,  0xFFD80008, 4,  "a timer register in P4" },
	{ Target::Sh4,  0xE0000000, 4,  "the store queues" },
	{ Target::Sh4,  0xE3FFFFFC, 4,  "the end of the store queues" },
	{ Target::Sh4,  0x08000000, 4,  "area 2, where nothing is" },
	{ Target::Sh4,  0x10000000, 4,  "area 4, where nothing is" },
	{ Target::Sh4,  0xE4000000, 4,  "P4 behind the store queues" },
	{ Target::Sh4,  0xFFFFFFFC, 8,  "a range past the end of the address space" },
	{ Target::Sh4,  0xFFFFFFFF, 2,  "a range past the end of the address space, of two bytes" },
	{ Target::Sh4,  0x00FFFFFC, 8,  "wave memory and what is behind it" },
	{ Target::Sh4,  0x007FFFFC, 8,  "what is in front of the wave memory, and wave memory" },
	{ Target::Sh4,  0x07FFFFFC, 8,  "video memory and what is behind it" },
	{ Target::Sh4,  0x03FFFFFC, 8,  "area 0 and video memory" },
	{ Target::Sh4,  0x0BFFFFFC, 8,  "area 2 and main memory" },
	{ Target::Arm7, 0x00800000, 4,  "the registers of the AICA, as the ARM7 has them" },
	{ Target::Arm7, 0x00802D00, 1,  "the interrupt level of the ARM7" },
	{ Target::Arm7, 0x00808000, 4,  "the ARM7 above its registers" },
	{ Target::Arm7, 0x01000000, 4,  "the ARM7 above its last address" },
	{ Target::Arm7, 0x007FFFFC, 8,  "the ARM7's wave memory and its registers" },
	{ Target::Arm7, 0xFFFFFFFC, 8,  "a range of the ARM7 past the end of the address space" },
	{ Target::Dsp,  0x00000000, 1,  "the DSP, which has no memory" },
	{ Target::Dsp,  0x8C010000, 4,  "the DSP at an address of main memory" },
};

}

DBG_SELFTEST_GUEST(writes_mem_refused, "writes.mem.refused")
{
	std::vector<u8> before, after;
	u8 data[8];
	Tally refused;

	CO_BEGIN(m_refused.co);

	while (!Settle(stop))
		CO_YIELD(m_refused.co);

	// what the bytes would be, were they stored: none that is there now
	Check(MachineState(&before), "(the state of the machine, as a save state has it)");

	for (const Refusal& r : refusals)
	{
		u8 was[8] = { 0 }, now[8] = { 0 };

		mem::Read(r.target, r.addr, r.len, was, nullptr);

		for (u32 i = 0; i < r.len; i++)
			data[i] = (u8)(~was[i] ^ (0x11 * i));

		const int rv = mem::Write(r.target, r.addr, r.len, data);

		mem::Read(r.target, r.addr, r.len, now, nullptr);

		refused.Add(rv == kErrNotWritable && memcmp(was, now, r.len) == 0, "%s: %u bytes at %08X of %s: %d, the bytes read %s", r.what, r.len, r.addr, TargetName(r.target),
			rv, memcmp(was, now, r.len) == 0 ? "as before" : "differently");
	}

	refused.Report("a range with a byte that is no main memory, video memory or wave memory is refused whole, -32013");

	Check(MachineState(&after) && before == after, "and the machine is what it was: its state, as a save state has it, has not changed (%zu bytes)", before.size());

	CO_END(m_refused.co);
}

/*
	writes.reg.sh4: every row of dc.sh4.cpu, written, read back as it is stored, and written back
*/
namespace {

struct {
	Co co;
} r_sh4;

// The register of the SH4 context behind a row of dc.sh4.cpu, read past dbg::regs
bool CtxValue(const char* name, u32* value)
{
	Sh4Context& ctx = Ctx();
	unsigned n = 0;
	u32 raw;

	if (sscanf(name, "R%u_BANK", &n) == 1 && strstr(name, "_BANK") != nullptr && n < 8)
	{
		*value = ctx.r_bank[n];
		return true;
	}

	if (name[0] == 'R' && sscanf(name, "R%u", &n) == 1 && n < 16 && strchr(name, '_') == nullptr)
	{
		*value = ctx.r[n];
		return true;
	}

	if (sscanf(name, "FR%u", &n) == 1 && n < 16)
	{
		memcpy(&raw, &ctx.xffr[16 + n], 4);
		*value = raw;
		return true;
	}

	if (sscanf(name, "XF%u", &n) == 1 && n < 16)
	{
		memcpy(&raw, &ctx.xffr[n], 4);
		*value = raw;
		return true;
	}

	static const struct { const char* name; u32 Sh4Context::* field; } plain[] = {
		{ "PC", &Sh4Context::pc }, { "PR", &Sh4Context::_pr }, { "GBR", &Sh4Context::gbr }, { "VBR", &Sh4Context::vbr }, { "SSR", &Sh4Context::ssr },
		{ "SPC", &Sh4Context::spc }, { "SGR", &Sh4Context::sgr }, { "DBR", &Sh4Context::dbr }, { "FPUL", &Sh4Context::fpul },
	};

	for (const auto& p : plain)
	{
		if (strcmp(name, p.name) == 0)
		{
			*value = ctx.*(p.field);
			return true;
		}
	}

	if (strcmp(name, "MACH") == 0) { *value = ctx.mac.h; return true; }
	if (strcmp(name, "MACL") == 0) { *value = ctx.mac.l; return true; }
	if (strcmp(name, "SR") == 0) { *value = sh4_sr_GetFull(); return true; }
	if (strcmp(name, "FPSCR") == 0) { *value = ctx.fpscr.full; return true; }

	return false;
}

// What a row stores of a value
u32 Sh4Stored(const char* name, u32 value)
{
	if (strcmp(name, "SR") == 0)
	{
		value &= 0x700083F3;

		// with MD clear, RB is stored as 0
		if (!(value & 0x40000000))
			value &= ~0x20000000u;
	}
	else if (strcmp(name, "FPSCR") == 0)
	{
		value &= FPSCR_MASK;
	}

	return value;
}

}

DBG_SELFTEST_GUEST(writes_reg_sh4, "writes.reg.sh4")
{
	const regs::Node& node = Sh4Node();
	Rows start, now;
	Tally stored, others, restored;
	u32 writable = 0;

	CO_BEGIN(r_sh4.co);

	while (!Settle(stop))
		CO_YIELD(r_sh4.co);

	while (!LoopStop(stop))
		CO_YIELD(r_sh4.co);

	start = ReadRows(node);

	for (u32 row = 0; row < node.registerCount; row++)
	{
		const char* name = node.registers[row].name;
		const bool special = strcmp(name, "PC") == 0 || strcmp(name, "SR") == 0 || strcmp(name, "FPSCR") == 0;

		writable += (node.registers[row].flags & regs::kRegWritable) != 0;

		// two values a row: one with every bit that can be set, one that differs from row to row. The PC takes
		// no odd value (writes.reg.pc)
		for (u32 pass = 0; pass < 2; pass++)
		{
			u32 value = pass == 0 ? 0xFFFFFFFF : (0x5A3C0F00u ^ (row * 0x01010101u) ^ (u32)start.v[row]);
			u32 second = 0;

			if (strcmp(name, "PC") == 0)
				value &= ~1u;

			const u32 expect = Sh4Stored(name, value);
			const int rv = regs::Write(node, row, value);

			stored.Add(rv == 0 && (u32)Get(node, row) == expect && CtxValue(name, &second) && second == expect, "%s written with %08X: %d, it reads %08X and the context has "
				"%08X, expected %08X", name, value, rv, (u32)Get(node, row), second, expect);

			// A row that is a store and nothing else leaves every other row alone. SR and FPSCR exchange register
			// banks (writes.reg.sr, writes.reg.fpscr), and the PC is one more store
			if (!special || strcmp(name, "PC") == 0)
			{
				now = ReadRows(node);
				now.v[row] = start.v[row];
				others.Add(RowsDiffer(now, start) == node.registerCount, "%s written: %s changed", name, RowName(node, RowsDiffer(now, start)));
			}

			const int back = regs::Write(node, row, start.v[row]);

			now = ReadRows(node);
			restored.Add(back == 0 && RowsDiffer(now, start) == node.registerCount, "%s written back: %d, %s differs", name, back, RowName(node, RowsDiffer(now, start)));
		}
	}

	Check(node.registerCount == 69 && writable == 69, "dc.sh4.cpu has 69 rows, and every one of them has a writer (%u, %u)", node.registerCount, writable);
	stored.Report("every row of dc.sh4.cpu written with two values: it reads back what is stored, in dbg::regs and in the SH4 context (SR under 700083F3 and without RB "
		"where MD is clear, FPSCR under 003FFFFF, the others whole)");
	others.Report("a row that is a store leaves every other row as it was");
	restored.Report("every row written back: all 69 read as at the stop");

	Check(Ctx().pc == (u32)start.v[RowOf(node, "PC")] && InLoop(Ctx().pc), "the SH4 stands where it stood (%08X)", Ctx().pc);

	CO_END(r_sh4.co);
}

/*
	writes.reg.pc: the PC of the SH4
*/
namespace {

struct {
	Co co;
	u32 pc, r8, r9, r10, r11;
	u32 tries;
	bool cachedBefore;
} r_pc;

}

DBG_SELFTEST_GUEST(writes_reg_pc, "writes.reg.pc")
{
	const Engine engine = TheEngine();
	u32 end = 0, cycles = 0;
	int rv, rv2, rv3;

	CO_BEGIN(r_pc.co);

	while (!Settle(stop))
		CO_YIELD(r_pc.co);

	/*
		An odd PC is refused, and nothing changes: the rest of the block that the stop owes is still owed
	*/
	Sh4Bps({ { 1, kMid }, { 2, kSink0 } });

	while (!StopAt(stop, 1, kMid))
		CO_YIELD(r_pc.co);

	rv = SetS("PC", kMid | 1);
	rv2 = SetS("PC", 0x8C010001);
	rv3 = SetS("PC", 0xFFFFFFFF);
	Check(rv == kErrNotWritable && rv2 == kErrNotWritable && rv3 == kErrNotWritable && Ctx().pc == kMid && GetS("PC") == kMid,
		"a PC with bit 0 set is refused, -32013, and the PC is what it was (%d, %d and %d; %08X)", rv, rv2, rv3, Ctx().pc);

	{
		const bool owed = dbg_sh4_TakeRemainder(kMid, &end, &cycles);

		Check(owed == engine.blocks, "behind the refused writes the stop owes what it owed: on %s, %s", engine.name, owed ? "the rest of its block" : "nothing");
	}

	/*
		The PC written with the value it has: the owed remainder goes. (The probe above took the remainder of that
		stop: this is the stop of the next pass, which owes its own.)
	*/
	Sh4Bps({ { 1, kMid } });

	while (!StopAt(stop, 1, kMid))
		CO_YIELD(r_pc.co);

	r_pc.cachedBefore = BlockCached(kLoop);

	rv = SetS("PC", kMid);
	Check(rv == 0 && Ctx().pc == kMid, "at the next stop of the breakpoint, in the middle of the block, the PC is written with the value it has");
	Check(!dbg_sh4_TakeRemainder(kMid, &end, &cycles), "nothing is owed behind it: the rest of the block went with the write%s", engine.blocks ? "" :
		" (the interpreter owes none at any stop: the recompiler shows this)");

	if (engine.blocks && !engine.fallback)
		Check(r_pc.cachedBefore && BlockCached(kLoop), "and the code cache was left alone: the block of the loop is in it as before the write");

	/*
		Onto the address of another breakpoint: the resume stops there with nothing executed. (The breakpoint is
		listed behind the look at the code cache: a change of the armed set discards the blocks of its page by
		itself.)
	*/
	r_pc.r8 = Ctx().r[8];
	r_pc.r9 = Ctx().r[9];
	r_pc.r10 = Ctx().r[10];
	r_pc.r11 = Ctx().r[11];

	rv = SetS("PC", kSink0);
	Check(rv == 0 && Ctx().pc == kSink0, "the PC is written three instructions on");

	if (engine.blocks && !engine.fallback)
		Check(!BlockCached(kLoop), "a PC that moves empties the code cache: the block of the loop is gone");

	Sh4Bps({ { 1, kMid }, { 2, kSink0 } });

	if (!Resume())
		break;
	CO_YIELD(r_pc.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 2 && Ctx().pc == kSink0 && Ctx().r[8] == r_pc.r8 && Ctx().r[9] == r_pc.r9 && Ctx().r[10] == r_pc.r10
		&& Ctx().r[11] == r_pc.r11, "the resume stops at that breakpoint, and nothing has executed: \"%s\" id %u at %08X, r8 to r11 as they were",
		StopCauseName(stop.cause), stop.breakpointId, Ctx().pc);

	/*
		Away and back onto the address of the stop: the resume runs through the breakpoint there
	*/
	rv = SetS("PC", kMid);
	rv2 = SetS("PC", kSink0);
	Check(rv == 0 && rv2 == 0 && Ctx().pc == kSink0, "the PC is written away, and back onto the address of the stop");

	if (!Resume())
		break;
	CO_YIELD(r_pc.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 1 && Ctx().pc == kMid && Ctx().r[8] == r_pc.r8 + 1,
		"the resume runs through the breakpoint of the stop: the next stop is that of the other one, in the next pass (\"%s\" id %u at %08X, %u passes on)",
		StopCauseName(stop.cause), stop.breakpointId, Ctx().pc, Ctx().r[8] - r_pc.r8);

	Sh4Bps({ });

	/*
		The mirror. The block cache finds a block by its address under a mask of 32 MiB, with no address in the key:
		0x0C.., 0x8C.. and 0xAC.. share their slots. At a stop that owes nothing, with the SH4 in its loop, the PC
		is written into the 0x0C.. mirror of the address it has. The loop goes back by a branch relative to itself:
		with the cache emptied by the write its block is compiled at the address the PC has, and the loop stays in
		that mirror; a block that was compiled at 0x8C.. and is found in the slot goes back to 0x8C...
		(0x8D.. is the same memory in other slots: a PC written there finds no block, and shows nothing.)
		The witness is the recompiler's: an interpreter reads every instruction at the PC
	*/
	while (!LoopStop(stop))
		CO_YIELD(r_pc.co);

	r_pc.pc = Ctx().pc;

	Check((r_pc.pc >> 24) == 0x8C && (!engine.blocks || r_pc.pc == kLoop), "a stop at a slice end with the SH4 in its loop, at %08X%s", r_pc.pc,
		engine.blocks ? ": the first instruction of its block" : "");
	rv = SetS("PC", r_pc.pc & 0x1FFFFFFF);
	Check(rv == 0 && Ctx().pc == (r_pc.pc & 0x1FFFFFFF), "the PC is written into the 0x0C.. mirror of that address (%08X)", r_pc.pc & 0x1FFFFFFF);

	for (r_pc.tries = 0; r_pc.tries < 3; r_pc.tries++)
	{
		while (!LoopStop(stop))
			CO_YIELD(r_pc.co);

		if ((Ctx().pc >> 24) != 0x0C)
			break;
	}

	Check((Ctx().pc >> 24) == 0x0C && InLoop(Ctx().pc) && r_pc.tries == 3, "behind three resumes, each entered from the block cache, the loop is still in the mirror the PC was "
		"written into: the SH4 at %08X (on %s)", Ctx().pc, engine.name);

	// and back, for the tests behind this one
	rv = SetS("PC", Ctx().pc | 0x80000000);
	Check(rv == 0 && (Ctx().pc >> 24) == 0x8C, "the PC is written back into the 0x8C.. mirror");

	while (!LoopStop(stop))
		CO_YIELD(r_pc.co);

	Check((Ctx().pc >> 24) == 0x8C && InLoop(Ctx().pc), "where the loop goes on (%08X)", Ctx().pc);

	CO_END(r_pc.co);
}

/*
	writes.reg.sr: SR with what the SH4 does when it loads it
*/
namespace {

struct {
	Co co;
	u32 sr, open, pc, spc, ssr;
	u32 tries;
	u32 bank[2][8];
} r_sr;

// Is the interrupt of the AICA raised at the SH4? Bit 1 of SB_ISTEXT
bool AicaAsksSh4()
{
	const regs::Node* intc = regs::Find("dc.holly.intc");
	u64 value = 0;

	return intc != nullptr && regs::Read(*intc, RowOf(*intc, "SB_ISTEXT"), &value, nullptr, 0) && (value & 2) != 0;
}

}

DBG_SELFTEST_GUEST(writes_reg_sr, "writes.reg.sr")
{
	bool swapped, back;
	int rv;
	u32 got;

	CO_BEGIN(r_sr.co);

	while (!Settle(stop))
		CO_YIELD(r_sr.co);

	while (!LoopStop(stop))
		CO_YIELD(r_sr.co);

	r_sr.sr = GetS("SR");

	Check((r_sr.sr & 0x700000F0) == 0x40000000, "in its loop the guest is privileged, on register bank 0, with its interrupts open: SR is %08X", r_sr.sr);

	/*
		The register banks
	*/
	for (u32 i = 0; i < 8; i++)
	{
		r_sr.bank[0][i] = Ctx().r[i];
		r_sr.bank[1][i] = Ctx().r_bank[i];

		// the two banks apart
		Ctx().r[i] = 0xB0000000 + i;
		Ctx().r_bank[i] = 0xB1000000 + i;
	}

	rv = SetS("SR", r_sr.sr | 0x20000000);
	Check(rv == 0 && GetS("SR") == (r_sr.sr | 0x20000000), "SR is written with RB set");

	swapped = true;

	for (u32 i = 0; i < 8; i++)
		swapped = swapped && Ctx().r[i] == 0xB1000000 + i && Ctx().r_bank[i] == 0xB0000000 + i && GetS("R0_BANK") == 0xB0000000;

	Check(swapped, "R0 to R7 and R0_BANK to R7_BANK have changed places");

	// MD written clear: RB is stored as 0, and the banks are those of RB 0
	rv = SetS("SR", 0x200000F0);
	got = GetS("SR");
	Check(rv == 0 && got == 0x000000F0, "SR written with 200000F0, MD clear and RB set, reads back %08X: RB is stored as 0", got);

	back = true;

	for (u32 i = 0; i < 8; i++)
		back = back && Ctx().r[i] == 0xB0000000 + i && Ctx().r_bank[i] == 0xB1000000 + i;

	Check(back, "and the banks are back in their places");

	rv = SetS("SR", 0xFFFFFFFF);
	got = GetS("SR");
	Check(rv == 0 && got == 0x700083F3 && sh4_sr_GetFull() == 0x700083F3, "SR written with FFFFFFFF reads back %08X: the value under 700083F3", got);

	rv = SetS("SR", r_sr.sr);
	Check(rv == 0 && GetS("SR") == r_sr.sr, "SR is written back");

	back = true;

	for (u32 i = 0; i < 8; i++)
	{
		back = back && Ctx().r[i] == 0xB0000000 + i && Ctx().r_bank[i] == 0xB1000000 + i;

		Ctx().r[i] = r_sr.bank[0][i];
		Ctx().r_bank[i] = r_sr.bank[1][i];
	}

	Check(back, "with the banks where they were at the stop");

	/*
		An interrupt that the written SR lets in. The guest's interrupts are masked by a write of SR, and the run
		goes on until the AICA has raised its interrupt, which then waits. At the stop of a code breakpoint,
		where no acceptance is owed, SR is written back: the interrupt is let in, and it is accepted when the
		park context is left, not in it
	*/
	rv = SetS("SR", r_sr.sr | 0xF0);
	Check(rv == 0 && (GetS("SR") & 0xF0) == 0xF0, "SR is written with the interrupt mask at 15");

	for (r_sr.tries = 0; r_sr.tries < 200 && !AicaAsksSh4(); r_sr.tries++)
	{
		while (!SliceStop(stop))
			CO_YIELD(r_sr.co);
	}

	// (T is the guest's: its loop sets and clears it)
	Check(AicaAsksSh4() && InLoop(Ctx().pc) && (GetS("SR") | 1) == (r_sr.sr | 0xF1), "the AICA raises its interrupt at the SH4 (%u sound bursts on), and it is not accepted: "
		"the SH4 is in its loop, at %08X, with SR %08X", r_sr.tries, Ctx().pc, GetS("SR"));

	Sh4Bps({ { 3, kMid } });

	while (!StopAt(stop, 3, kMid))
		CO_YIELD(r_sr.co);

	Sh4Bps({ });

	r_sr.pc = Ctx().pc;
	r_sr.spc = Ctx().spc;
	r_sr.ssr = Ctx().ssr;
	r_sr.open = GetS("SR") & ~0xF0u;

	Check(!g_intc_deferred && g_stop_latched && AicaAsksSh4(), "at the stop of a code breakpoint in the loop no interrupt acceptance is owed, and the interrupt still waits");

	rv = SetS("SR", r_sr.open);
	Check(rv == 0 && GetS("SR") == r_sr.open, "SR is written with the value it has and the interrupt mask at 0 (%08X)", r_sr.open);
	Check(Ctx().pc == r_sr.pc && Ctx().spc == r_sr.spc && Ctx().ssr == r_sr.ssr && GetS("SR") == r_sr.open,
		"nothing is accepted in the park context: the PC, SPC and SSR are what they were, and SR is what was written (pc %08X)", Ctx().pc);
	Check(g_intc_deferred, "the acceptance is owed");

	if (!StepSh4())
		break;
	CO_YIELD(r_sr.co);

	Check(stop.cause == StopCause::Step && Ctx().pc == kIrq && Ctx().spc == r_sr.pc && Ctx().ssr == r_sr.open && (GetS("SR") & 0x70000000) == 0x70000000,
		"it is made on the way out: the step is complete at the vector with nothing executed, the SH4 at %08X (g_irq), SPC %08X and SSR %08X the PC of the stop and the SR "
		"that was written", Ctx().pc, Ctx().spc, Ctx().ssr);

	// the handler runs and returns
	while (!LoopStop(stop))
		CO_YIELD(r_sr.co);

	Check((GetS("SR") | 1) == (r_sr.sr | 1) && !AicaAsksSh4(), "the handler has answered the interrupt and returned into the loop (SR %08X)", GetS("SR"));

	CO_END(r_sr.co);
}

/*
	writes.reg.fpscr: FPSCR with what the SH4 does when it loads it, and the FP mode of compiled code.

	Two instructions of the loop are replaced by float FPUL, FR2 and fmov FR4, FR6. What they do depends on PR
	and on SZ: with PR clear the first makes a single of FPUL in FR2, with PR set a double in FR2 and FR3; with
	SZ clear the second copies FR4 to FR6, with SZ set FR4 and FR5 to FR6 and FR7.

	The recompiler has the FP mode in the code of a block only when the block was compiled with PR clear: with
	PR set it emits every FP instruction as a call of the interpreter's handler, which reads FPSCR when it runs.
	So the block of the loop is compiled with PR and SZ clear, the guest's FPSCR, and the witness is taken behind
	a write that sets PR; then, with PR clear again and the block compiled again, behind a write that sets SZ.
	The other direction shows nothing. Each witness is taken twice: by a resume from a stop that owes nothing,
	which enters the block from the cache, and by a step, which decodes its instruction in the FPSCR of the
	moment whatever the cache holds.
	The rounding mode has no witness on an x86-64 host: the one operation the decoder selects by it is the same
	host instruction either way
*/
namespace {

struct {
	Co co;
	u32 fpscr, fpul;
	u32 fr[8];
	u16 w0, w1;
	u32 steps;
	u32 fr0, xf0, fr15, xf15;
} r_fp;

const u32 kSingleOne = 0x3F800000;		// 1.0f
const u32 kDoubleOneHigh = 0x3FF00000;	// the high word of 1.0
const u32 kMark = 0xDEADBEEF;

// FR2, FR3 and FR6, FR7 as a pass of the loop has not left them; FR4 and FR5 are what the move copies
void FpArm()
{
	SetS("FPUL", 1);
	SetS("FR2", 0);
	SetS("FR3", kMark);
	SetS("FR4", 0x11111111);
	SetS("FR5", 0x22222222);
	SetS("FR6", 0);
	SetS("FR7", kMark);
}

bool FpSingle()
{
	return GetS("FR2") == kSingleOne && GetS("FR3") == kMark;
}

bool FpDouble()
{
	return GetS("FR2") == kDoubleOneHigh && GetS("FR3") == 0;
}

bool FpMove32()
{
	return GetS("FR6") == 0x11111111 && GetS("FR7") == kMark;
}

bool FpMove64()
{
	return GetS("FR6") == 0x11111111 && GetS("FR7") == 0x22222222;
}

// Steps of the SH4 until the instruction at `at` has executed: through the rest of the pass, and through the
// handler of an interrupt that is accepted on the way
struct {
	Co co;
	u32 steps;
	bool done;
} step_over;

bool StepPast(const emu::RunState& stop, u32 at)
{
	CO_BEGIN(step_over.co);

	step_over.done = false;

	for (step_over.steps = 0; step_over.steps < 64 && !step_over.done; step_over.steps++)
	{
		step_over.done = Ctx().pc == at;

		if (!StepSh4())
			break;
		CO_YIELD(step_over.co);

		// complete at the vector with nothing executed: the instruction is still to come
		if (step_over.done && Ctx().pc == kIrq && Ctx().spc == at)
			step_over.done = false;

		if (stop.cause != StopCause::Step)
			break;
	}

	CO_END(step_over.co);
}

}

DBG_SELFTEST_GUEST(writes_reg_fpscr, "writes.reg.fpscr")
{
	const Engine engine = TheEngine();
	bool swapped;
	int rv;
	u32 got;

	CO_BEGIN(r_fp.co);

	while (!Settle(stop))
		CO_YIELD(r_fp.co);

	while (!LoopStop(stop))
		CO_YIELD(r_fp.co);

	r_fp.fpscr = GetS("FPSCR");
	r_fp.fpul = GetS("FPUL");

	for (u32 i = 0; i < 8; i++)
		memcpy(&r_fp.fr[i], &Ctx().xffr[16 + i], 4);

	r_fp.w0 = Peek16(kSink0);
	r_fp.w1 = Peek16(kSink1);

	Check(r_fp.fpscr == 0x00040001, "the FPSCR of the guest is what reios leaves, 00040001: PR 0 and SZ 0 (%08X)", r_fp.fpscr);

	/*
		The FP banks, and the mask
	*/
	memcpy(&r_fp.fr0, &Ctx().xffr[16], 4);
	memcpy(&r_fp.xf0, &Ctx().xffr[0], 4);
	memcpy(&r_fp.fr15, &Ctx().xffr[31], 4);
	memcpy(&r_fp.xf15, &Ctx().xffr[15], 4);

	Check(SetS("FR0", 0xF0F0F0F0) == 0 && SetS("XF0", 0x0A0A0A0A) == 0 && SetS("FR15", 0xF1F1F1F1) == 0 && SetS("XF15", 0x1A1A1A1A) == 0, "(FR0, FR15, XF0 and XF15 apart)");
	rv = SetS("FPSCR", r_fp.fpscr | 0x00200000);
	Check(rv == 0 && GetS("FPSCR") == (r_fp.fpscr | 0x00200000), "FPSCR is written with FR set");

	swapped = GetS("FR0") == 0x0A0A0A0A && GetS("XF0") == 0xF0F0F0F0 && GetS("FR15") == 0x1A1A1A1A && GetS("XF15") == 0xF1F1F1F1;
	Check(swapped, "FR0 to FR15 and XF0 to XF15 have changed places");

	rv = SetS("FPSCR", 0xFFFFFFFF);
	got = GetS("FPSCR");
	Check(rv == 0 && got == 0x003FFFFF && Ctx().fpscr.full == 0x003FFFFF, "FPSCR written with FFFFFFFF reads back %08X: the value under 003FFFFF", got);

	rv = SetS("FPSCR", r_fp.fpscr);
	Check(rv == 0 && GetS("FPSCR") == r_fp.fpscr && GetS("FR0") == 0xF0F0F0F0 && GetS("XF0") == 0x0A0A0A0A && GetS("FR15") == 0xF1F1F1F1 && GetS("XF15") == 0x1A1A1A1A,
		"FPSCR is written back, and the banks are in their places");

	SetS("FR0", r_fp.fr0);
	SetS("XF0", r_fp.xf0);
	SetS("FR15", r_fp.fr15);
	SetS("XF15", r_fp.xf15);

	/*
		The FP mode of compiled code: PR
	*/
	Check(Poke16(kSink0, kFloatFpulFr2) == 0 && Poke16(kSink1, kFmovFr4Fr6) == 0, "float FPUL, FR2 and fmov FR4, FR6 are written into the loop, in place of the two "
		"instructions that compute r11");
	FpArm();
	Check((GetS("FPSCR") & 0x00180000) == 0, "PR is 0 and SZ is 0: the block of the loop is compiled in that mode");

	while (!LoopStop(stop))
		CO_YIELD(r_fp.co);

	Check(FpSingle() && FpMove32(), "a run with PR 0 and SZ 0: FR2 is %08X with FR3 untouched (a single), FR6 has FR4 and FR7 is untouched (a move of 32 bits)", GetS("FR2"));

	// PR set: by a resume from a stop that owes nothing
	FpArm();
	rv = SetS("FPSCR", r_fp.fpscr | 0x00080000);
	Check(rv == 0 && GetS("FPSCR") == (r_fp.fpscr | 0x00080000), "at a stop that owes nothing, FPSCR is written with PR set");

	if (engine.blocks && !engine.fallback)
		Check(!BlockCached(kLoop), "the write empties the code cache: the block of the loop is gone");

	while (!LoopStop(stop))
		CO_YIELD(r_fp.co);

	Check(FpDouble(), "behind a resume that entered the loop from the block cache, FR2 and FR3 are %08X %08X: the instruction ran in the precision that was written, "
		"a double (on %s)", GetS("FR2"), GetS("FR3"), engine.name);

	// by a step
	FpArm();

	while (!StepPast(stop, kSink0))
		CO_YIELD(r_fp.co);

	Check(step_over.done && FpDouble(), "by steps, to behind the instruction (%u of them): FR2 and FR3 are %08X %08X, a double", step_over.steps, GetS("FR2"), GetS("FR3"));

	/*
		PR back at 0, the block compiled again; then SZ
	*/
	rv = SetS("FPSCR", r_fp.fpscr);
	Check(rv == 0 && (GetS("FPSCR") & 0x00180000) == 0, "FPSCR is written back: PR is 0 and SZ is 0, and the block of the loop is compiled in that mode again");
	FpArm();

	while (!LoopStop(stop))
		CO_YIELD(r_fp.co);

	Check(FpSingle() && FpMove32(), "a run with PR 0 and SZ 0: a single again (FR2 %08X), and a move of 32 bits", GetS("FR2"));

	FpArm();
	rv = SetS("FPSCR", r_fp.fpscr | 0x00100000);
	Check(rv == 0 && GetS("FPSCR") == (r_fp.fpscr | 0x00100000), "at a stop that owes nothing, FPSCR is written with SZ set");

	while (!LoopStop(stop))
		CO_YIELD(r_fp.co);

	Check(FpMove64() && FpSingle(), "behind a resume that entered the loop from the block cache, FR6 and FR7 are %08X %08X: the move ran in the size that was written, "
		"64 bits (on %s)", GetS("FR6"), GetS("FR7"), engine.name);

	FpArm();

	while (!StepPast(stop, kSink1))
		CO_YIELD(r_fp.co);

	Check(step_over.done && FpMove64(), "by steps, to behind the instruction (%u of them): FR6 and FR7 are %08X %08X, a move of 64 bits", step_over.steps, GetS("FR6"),
		GetS("FR7"));

	/*
		Everything back
	*/
	Check(SetS("FPSCR", r_fp.fpscr) == 0 && Poke16(kSink0, r_fp.w0) == 0 && Poke16(kSink1, r_fp.w1) == 0 && Peek16(kSink0) == r_fp.w0 && Peek16(kSink1) == r_fp.w1,
		"FPSCR and the two instructions of the loop are written back");

	for (u32 i = 0; i < 8; i++)
	{
		char name[8];

		snprintf(name, sizeof(name), "FR%u", i);
		SetS(name, r_fp.fr[i]);
	}

	SetS("FPUL", r_fp.fpul);

	while (!LoopStop(stop))
		CO_YIELD(r_fp.co);

	Check(GetS("FPSCR") == 0x00040001 && InLoop(Ctx().pc), "the guest runs on in its loop, with its FPSCR");

	CO_END(r_fp.co);
}

/*
	writes.reg.arm7: the rows of dc.aica.arm7
*/
namespace {

struct {
	Co co;
	ArmRegs saved, regs;
	Rows rows;
	u32 r3, r7;
	u32 pc, bursts, tries, resets;
	bool done;
	std::atomic<int> rv { 99 };
	std::atomic<bool> onEmu { false };
} r_arm;

bool HasSpsr(u32 mode)
{
	return mode == 0x11 || mode == 0x12 || mode == 0x13 || mode == 0x17 || mode == 0x1B;
}

const char* SpsrRow(u32 mode)
{
	switch (mode)
	{
	case 0x11: return "SPSR_FIQ";
	case 0x12: return "SPSR_IRQ";
	case 0x13: return "SPSR_SVC";
	case 0x17: return "SPSR_ABT";
	case 0x1B: return "SPSR_UND";
	}

	return "SPSR";
}

// The rows that show the live R13 and R14 of a mode
const char* BankRow(u32 mode, u32 reg)
{
	static const char* const names[6][2] = { { "R13_USR", "R14_USR" }, { "R13_FIQ", "R14_FIQ" }, { "R13_IRQ", "R14_IRQ" }, { "R13_SVC", "R14_SVC" }, { "R13_ABT", "R14_ABT" },
		{ "R13_UND", "R14_UND" } };
	u32 bank = 0;

	switch (mode)
	{
	case 0x11: bank = 1; break;
	case 0x12: bank = 2; break;
	case 0x13: bank = 3; break;
	case 0x17: bank = 4; break;
	case 0x1B: bank = 5; break;
	}

	return names[bank][reg - 13];
}

// Do two rows of dc.aica.arm7 show one register while the ARM7 is in `mode`? A row of a bank shows the live
// register while its mode is the active one
bool SameRegister(const char* a, const char* b, u32 mode)
{
	if (strcmp(a, b) == 0)
		return true;

	auto live = [mode](const char* name) -> std::string {
		if (strcmp(name, BankRow(mode, 13)) == 0)
			return "R13";
		if (strcmp(name, BankRow(mode, 14)) == 0)
			return "R14";
		if (HasSpsr(mode) && strcmp(name, SpsrRow(mode)) == 0)
			return "SPSR";

		// R8 to R12 are banked in FIQ mode only
		if (mode == 0x11 && strlen(name) >= 6 && strcmp(name + strlen(name) - 4, "_FIQ") == 0 && name[0] == 'R')
			return std::string(name, strlen(name) - 4);

		return name;
	};

	return live(a) == live(b);
}

/*
	A write of the CPSR that is taken, held to what the mode switch of the core does (hw/arm7/SoundCPU.h,
	DebugSetReg): the register reads back under F00000DF, in the row and in the word that is stored; the live
	R13 and R14 are those of the mode written; the row SPSR reads, by value, the CPSR as it was when the write
	is made in user or system mode with one of those two modes, and otherwise what the row SPSR_<mode> of the
	mode written read in front of the write (the SPSR itself where the mode written has none); and every other
	row reads as before. Returns how many of these did not hold, with the first in *why
*/
u32 CpsrWrite(u32 value, std::string* why)
{
	const regs::Node& node = Arm7Node();
	const Rows pre = ReadRows(node);
	const u32 c0 = (u32)pre.v[RowOf(node, "CPSR")];
	const u32 m0 = c0 & 0x1F, mv = value & 0x1F;
	u32 bad = 0;
	char text[200];

	auto fail = [&](const char* what, u32 got, u32 expected) {
		if (bad++ == 0)
		{
			snprintf(text, sizeof(text), "CPSR %08X written with %08X: %s is %08X, expected %08X", c0, value, what, got, expected);
			*why = text;
		}
	};

	const int rv = SetA("CPSR", value);

	if (rv != 0)
	{
		fail("the answer", (u32)rv, 0);
		return bad;
	}

	const Rows post = ReadRows(node);
	const u32 stored = value & 0xF00000DF;

	if ((u32)post.v[RowOf(node, "CPSR")] != stored)
		fail("the row CPSR", (u32)post.v[RowOf(node, "CPSR")], stored);

	if (StoredArmWord(RN_CPSR) != stored)
		fail("the word of the CPSR in the save state", StoredArmWord(RN_CPSR), stored);

	// the row SPSR, by value
	u32 spsr;

	if (!HasSpsr(mv))
		spsr = HasSpsr(m0) ? (u32)pre.v[RowOf(node, "SPSR")] : c0;
	else
		spsr = (u32)pre.v[RowOf(node, SpsrRow(mv))];

	if ((u32)post.v[RowOf(node, "SPSR")] != spsr)
		fail("the row SPSR", (u32)post.v[RowOf(node, "SPSR")], spsr);

	for (u32 row = 0; row < node.registerCount; row++)
	{
		const char* name = node.registers[row].name;
		u32 expected = (u32)pre.v[row];

		if (strcmp(name, "CPSR") == 0 || strcmp(name, "SPSR") == 0)
			continue;

		// the live R13 and R14 are the bank of the mode written
		if (strcmp(name, "R13") == 0)
			expected = (u32)pre.v[RowOf(node, BankRow(mv, 13))];
		else if (strcmp(name, "R14") == 0)
			expected = (u32)pre.v[RowOf(node, BankRow(mv, 14))];
		else if (name[0] == 'R' && strlen(name) <= 3 && atoi(name + 1) >= 8 && atoi(name + 1) <= 12 && (m0 == 0x11) != (mv == 0x11))
		{
			// R8 to R12: into FIQ mode they are the rows of its bank; out of it they are the registers of the
			// other modes, which no row showed
			if (mv != 0x11)
				continue;

			char banked[12];

			snprintf(banked, sizeof(banked), "%s_FIQ", name);
			expected = (u32)pre.v[RowOf(node, banked)];
		}

		if ((u32)post.v[row] != expected)
			fail(name, (u32)post.v[row], expected);
	}

	return bad;
}

}

DBG_SELFTEST_GUEST(writes_reg_arm7, "writes.reg.arm7")
{
	const regs::Node& node = Arm7Node();
	Rows now;
	Tally stored, shown, restored, refused, cpsr, emptied;
	std::string why;
	u32 writable = 0, c0, m0, other, plain, differs, word0, got, mirror;
	int rv, rv2;

	CO_BEGIN(r_arm.co);

	while (!Settle(stop))
		CO_YIELD(r_arm.co);

	while (!ArmStop(stop))
		CO_YIELD(r_arm.co);

	ArmBps({ });

	r_arm.saved = ReadArm();
	r_arm.rows = ReadRows(node);

	c0 = GetA("CPSR");
	m0 = c0 & 0x1F;

	Check(m0 == 0x1F || HasSpsr(m0), "the loop of the guest's ARM7 runs in system mode, or in a mode with an SPSR: its CPSR is %08X (%s); the stop is inside a sound burst, "
		"with no request of the AICA waiting", c0, HasSpsr(m0) ? "a mode with an SPSR" : "system mode");

	/*
		Every row with a writer: written, read back as it is stored, written back
	*/
	for (u32 row = 0; row < node.registerCount; row++)
	{
		const char* name = node.registers[row].name;

		if (!(node.registers[row].flags & regs::kRegWritable))
			continue;

		writable++;

		// the CPSR has its own part below
		if (strcmp(name, "CPSR") == 0)
			continue;

		const u32 value = 0xA5C30003 ^ (row * 0x01010100u) ^ (u32)r_arm.rows.v[row];
		const u32 expect = strcmp(name, "PC") == 0 ? value & ~3u : value;
		const u32 resets = regs::Arm7CacheResets();
		const int rv = regs::Write(node, row, value);
		u32 second = 0xDEADBEEF;

		Scpu()->DebugGetReg(regs::KeySelector(node.registers[row].key), &second);

		stored.Add(rv == 0 && (u32)Get(node, row) == expect && second == expect, "%s written with %08X: %d, it reads %08X and the device has %08X, expected %08X", name, value, rv,
			(u32)Get(node, row), second, expect);

		// every row that shows the register shows the value, and every other row is as it was
		now = ReadRows(node);

		for (u32 r = 0; r < node.registerCount; r++)
		{
			const bool same = SameRegister(node.registers[r].name, name, m0);

			shown.Add((u32)now.v[r] == (same ? expect : (u32)r_arm.rows.v[r]), "%s written: %s reads %08X, expected %08X", name, node.registers[r].name, (u32)now.v[r],
				same ? expect : (u32)r_arm.rows.v[r]);
		}

		const int back = regs::Write(node, row, r_arm.rows.v[row]);

		now = ReadRows(node);
		restored.Add(back == 0 && RowsDiffer(now, r_arm.rows) == node.registerCount, "%s written back: %d, %s differs", name, back, RowName(node, RowsDiffer(now, r_arm.rows)));

		// the PC was moved twice; no other row is one that compiled code is found by
		emptied.Add(regs::Arm7CacheResets() - resets == (strcmp(name, "PC") == 0 ? 2u : 0u), "%s written and written back: the code cache of the ARM7 was emptied %u times",
			name, regs::Arm7CacheResets() - resets);
	}

	Check(node.registerCount == 41 && writable == 40, "dc.aica.arm7 has 41 rows, and every one of them but ENABLED has a writer (%u, %u)", node.registerCount, writable);
	stored.Report("every row of dc.aica.arm7 but ENABLED and the CPSR written: it reads back what is stored, in dbg::regs and in the device (the PC with bits 0 and 1 clear, "
		"the others whole)");
	shown.Report("the value is in every row that shows the register (the live one for the bank of the active mode), and every other row is as it was");
	restored.Report("every row written back: all 41 read as at the stop");
	emptied.Report("the write that moves the PC, and the one that moves it back, each empty the code cache of the ARM7 (SoundCPU::InvalidateJitCache; on this host the count of "
		"the writer is what shows it); the write of no other row does");

	/*
		What is refused
	*/
	word0 = StoredArmWord(RN_CPSR);
	r_arm.resets = regs::Arm7CacheResets();

	for (u32 mode = 0; mode < 32; mode++)
	{
		if (mode == 0x10 || HasSpsr(mode) || mode == 0x1F)
			continue;

		refused.Add(SetA("CPSR", (c0 & ~0x1Fu) | mode) == kErrNotWritable && RowsDiffer(ReadRows(node), r_arm.rows) == node.registerCount && StoredArmWord(RN_CPSR) == word0,
			"a CPSR with the mode bits %02X: taken, or something changed", mode);
	}

	refused.Report("a CPSR whose mode bits are no mode is refused, -32013, and nothing changes: the 25 other values of the five bits");

	rv = SetA("ENABLED", 0);
	rv2 = SetA("ENABLED", 1);
	Check(rv == kErrNotWritable && rv2 == kErrNotWritable && GetA("ENABLED") == 1 && ArmClocked(),
		"ENABLED is refused, -32013: the debugger does not hold or release the ARM7, which is clocked as before (%d and %d)", rv, rv2);

	/*
		The CPSR, through the mode switch. `other` is a mode with an SPSR that is not the mode of the loop, `plain`
		one of the two modes without. Every write is held to the table of the four cases (CpsrWrite); the row SPSR
		is compared by value
	*/
	other = m0 == 0x12 ? 0x17 : 0x12;
	plain = m0 == 0x1F ? 0x10 : 0x1F;

	// what is stored: N Z C V, I, F and the mode, and no other bit
	cpsr.Add(CpsrWrite(c0 | 0xFFFFFF20, &why) == 0 && GetA("CPSR") == ((c0 & 0xDF) | 0xF0000000), "%s", why.c_str());
	cpsr.Add(CpsrWrite(c0 & 0x0FFFFFFF, &why) == 0, "%s", why.c_str());
	cpsr.Add(CpsrWrite(c0, &why) == 0, "%s", why.c_str());

	now = ReadRows(node);
	now.v[RowOf(node, "SPSR")] = r_arm.rows.v[RowOf(node, "SPSR")];
	cpsr.Add(RowsDiffer(now, r_arm.rows) == node.registerCount && (HasSpsr(m0) ? GetA("SPSR") == (u32)r_arm.rows.v[RowOf(node, "SPSR")] : GetA("SPSR") == (c0 & 0x0FFFFFFF)),
		"the CPSR written three times in its mode and back at its value: %s differs, SPSR is %08X", RowName(node, RowsDiffer(now, r_arm.rows)), GetA("SPSR"));

	// into another mode with an SPSR, and back
	cpsr.Add(CpsrWrite((c0 & ~0x1Fu) | other, &why) == 0 && (GetA("CPSR") & 0x1F) == other, "%s", why.c_str());
	cpsr.Add(CpsrWrite(c0, &why) == 0, "%s", why.c_str());

	// Into a mode without one, a second time with the value it then has, from there into the mode with an SPSR,
	// back, and back into the mode of the loop
	cpsr.Add(CpsrWrite((c0 & ~0x1Fu) | plain, &why) == 0 && (GetA("CPSR") & 0x1F) == plain, "%s", why.c_str());
	cpsr.Add(CpsrWrite(GetA("CPSR"), &why) == 0, "%s", why.c_str());
	cpsr.Add(CpsrWrite((c0 & ~0x1Fu) | other, &why) == 0, "%s", why.c_str());
	cpsr.Add(CpsrWrite((c0 & ~0x1Fu) | plain, &why) == 0, "%s", why.c_str());
	cpsr.Add(CpsrWrite(c0, &why) == 0, "%s", why.c_str());

	// into FIQ mode, where R8 to R12 are banked as well, and back
	if (m0 != 0x11)
	{
		cpsr.Add(CpsrWrite((c0 & ~0x1Fu) | 0x11, &why) == 0 && (GetA("CPSR") & 0x1F) == 0x11, "%s", why.c_str());
		cpsr.Add(CpsrWrite(c0, &why) == 0, "%s", why.c_str());
	}

	cpsr.Report("the CPSR written through the mode switch: it reads back under F00000DF, in its row and in the word a save state has; R13 and R14 are those of the mode "
		"written; the row SPSR reads, by value, what the mode switch of the core gives it; every other row is as it was");

	// From a mode with an SPSR every pair of a write and its write back leaves every row as it read. From system
	// mode the row SPSR is the exception: it reads what the last pair left in it
	now = ReadRows(node);
	differs = RowsDiffer(now, r_arm.rows);

	if (HasSpsr(m0))
	{
		Check(differs == node.registerCount && ArmDiffers(ReadArm(), r_arm.saved) == kArmRegs, "behind all of them, with the CPSR back at its value: every row reads as at the "
			"stop (the loop runs in a mode with an SPSR; the first that differs: %s)", RowName(node, differs));
	}
	else
	{
		now.v[RowOf(node, "SPSR")] = r_arm.rows.v[RowOf(node, "SPSR")];
		Check(RowsDiffer(now, r_arm.rows) == node.registerCount, "behind all of them, with the CPSR back at its value: every row but SPSR reads as at the stop (the loop runs in "
			"system mode, where a write of the CPSR leaves the CPSR as it was in the SPSR; the first that differs: %s)", RowName(node, RowsDiffer(now, r_arm.rows)));
		SetA("SPSR", r_arm.rows.v[RowOf(node, "SPSR")]);
	}

	/*
		The PC: stored aligned, and where the next instruction is fetched
	*/
	r_arm.r7 = Arm(7);
	r_arm.r3 = Arm(3);

	Check(regs::Arm7CacheResets() == r_arm.resets, "no write of the CPSR, taken or refused, and no refused write of ENABLED emptied the code cache of the ARM7");

	rv = SetA("PC", kArmLeaf + 3);
	got = GetA("PC");
	Check(rv == 0 && got == kArmLeaf && ArmPc() == kArmLeaf, "the PC written with %08X reads back %08X: bits 0 and 1 are not stored", kArmLeaf + 3, got);

	// A block of the ARM7's recompiler is found by its address under the mask of the wave memory and has the
	// address it was compiled at in its code: a write that moves the PC empties the code cache, and a write of
	// the address the PC has moves nothing
	rv = SetA("PC", kArmLeaf);
	rv2 = SetA("PC", kArmLeaf + 2);
	Check(rv == 0 && rv2 == 0 && ArmPc() == kArmLeaf && regs::Arm7CacheResets() - r_arm.resets == 1, "the write moved the PC and emptied the code cache of the ARM7, once; two "
		"more writes, which store the address the PC has, did not (%u times)", regs::Arm7CacheResets() - r_arm.resets);

	if (!StepArm())
		break;
	CO_YIELD(r_arm.co);

	Check(stop.cause == StopCause::Step && (ArmPc() == kArmLeaf + 4 || ArmPc() == 0x1C) && (ArmPc() == 0x1C || Arm(7) == r_arm.r7 + 1) && Arm(3) == r_arm.r3,
		"a step from the written PC executes the instruction there, the first of a_leaf, and no other: the ARM7 at %08X, r7 counted %u, the loop has not counted", ArmPc(),
		Arm(7) - r_arm.r7);

	// the top of the address space, and back to where the step ended: the ARM7 goes on from there, through
	// a_leaf back into its loop
	r_arm.pc = ArmPc();
	r_arm.resets = regs::Arm7CacheResets();

	rv = SetA("PC", 0xFFFFFFFF);
	got = GetA("PC");
	Check(rv == 0 && got == 0xFFFFFFFC && ArmPc() == 0xFFFFFFFC, "the PC written with FFFFFFFF reads back %08X", got);

	// The same instruction in the next mirror of the wave memory. The address is stored whole: a step goes on in
	// the mirror, and so does a resume, which finds no block of the other mirror in the cache
	mirror = r_arm.pc + sh4_cpu->aica_ram.size;

	rv = SetA("PC", mirror);
	got = GetA("PC");
	Check(rv == 0 && got == mirror && ArmPc() == mirror, "the PC written into the next mirror of the wave memory, with %08X, reads back %08X: the address is stored under no "
		"mask", mirror, got);

	rv = SetA("PC", r_arm.pc);
	Check(rv == 0 && ArmPc() == r_arm.pc && regs::Arm7CacheResets() - r_arm.resets == 3, "(and is written back: %08X.) Each of the three writes moved the PC and emptied the code "
		"cache of the ARM7 (%u times)", r_arm.pc, regs::Arm7CacheResets() - r_arm.resets);

	/*
		The rule of the SH4 for a written PC and the breakpoint state: onto another breakpoint it stops there with
		nothing executed, back onto the address of the stop it resumes through
	*/
	while (!ArmStop(stop))
		CO_YIELD(r_arm.co);

	ArmBps({ { kArmId, kArmLoop }, { 11, kArmLoop + 4 } });
	r_arm.r3 = Arm(3);

	rv = SetA("PC", kArmLoop + 4);
	Check(rv == 0 && ArmPc() == kArmLoop + 4, "at the stop of a breakpoint of the ARM7, its PC is written onto the breakpoint behind it");

	if (!Resume())
		break;
	CO_YIELD(r_arm.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 11 && ArmPc() == kArmLoop + 4 && Arm(3) == r_arm.r3,
		"the resume stops there, and nothing has executed: id %u at %08X, the loop has counted %u", stop.breakpointId, ArmPc(), Arm(3) - r_arm.r3);

	rv = SetA("PC", kArmLoop);
	rv2 = SetA("PC", kArmLoop + 4);
	Check(rv == 0 && rv2 == 0 && ArmPc() == kArmLoop + 4, "the PC is written away and back onto the address of the stop");

	if (!Resume())
		break;
	CO_YIELD(r_arm.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kArmId && ArmPc() == kArmLoop && Arm(3) == r_arm.r3,
		"the resume runs through the breakpoint there: the next stop is that of the breakpoint in front of the next pass (id %u at %08X)", stop.breakpointId, ArmPc());

	ArmBps({ });

	/*
		The PC written in a service excursion of a running machine. The stop is that of a step that was complete at
		the end of its burst: the ARM7 decided it, no burst is suspended, and the ARM7 stands on some instruction P.
		A breakpoint is listed on P and the resume armed: the instruction the ARM7 stands on is exempt from the
		first check, which is in the next burst. A thread has queued a call that writes the PC with P: it is
		served at the first slice end, in front of that burst. A written PC ends the exemption, also when it is
		written with the value it has: the next burst stops in front of P, with every register as the step left
		it. (A writer that did not tell the breakpoint state would run through P.)
	*/
	r_arm.done = false;

	for (r_arm.tries = 0; r_arm.tries < 12 && !r_arm.done; r_arm.tries++)
	{
		while (!ArmStop(stop))
			CO_YIELD(r_arm.co);

		ArmBps({ });

		while (!StepToBurstEnd(stop))
			CO_YIELD(r_arm.co);

		if (stop.cause != StopCause::Step || step_end.bad || Suspended() != SOUND_BURST_NONE || dbg_arm7_armed != 0)
		{
			Check(false, "a step that is complete at the end of its burst (\"%s\", %u steps, burst %u, dbg_arm7_armed %u)", StopCauseName(stop.cause), step_end.steps,
				Suspended(), (u32)dbg_arm7_armed);
			break;
		}

		r_arm.regs = ReadArm();
		r_arm.pc = ArmPc();
		r_arm.bursts = Bursts();
		r_arm.rv = 99;
		r_arm.onEmu = false;

		ArmBps({ { 12, r_arm.pc } });

		OtherStart([] {
			const u32 pc = r_arm.pc;

			emu::Call(emu::CC_WRITE, [pc] {
				r_arm.onEmu = emu::OnEmuThread() && emu::GetRunState().lifetime == Lifetime::Running;
				r_arm.rv = regs::Write(Arm7Node(), RowOf(Arm7Node(), "PC"), pc | 1);
			});
		});

		// until the call is in the queue
		for (Clock::time_point t0 = Clock::now(); !(g_attn.load() & ATTN_CALL) && MsSince(t0) < 5000; )
			std::this_thread::yield();

		if (!Resume())
			break;
		CO_YIELD(r_arm.co);

		OtherJoin();

		differs = ArmDiffers(ReadArm(), r_arm.regs);

		if (stop.cause != StopCause::Breakpoint || stop.breakpointId != 12 || r_arm.rv != 0 || !r_arm.onEmu || Bursts() != r_arm.bursts + 1)
		{
			Check(false, "the write of the PC was served while the machine ran, and the breakpoint on the PC stops in the next burst (\"%s\" id %u, the write returned %d, %s, "
				"%u bursts)", StopCauseName(stop.cause), stop.breakpointId, r_arm.rv.load(), r_arm.onEmu ? "in a service excursion" : "not in a service excursion",
				Bursts() - r_arm.bursts);
			break;
		}

		// A FIQ that was entered in front of P has run its handler: the check on the vector ended the exemption,
		// and the case shows nothing. It is taken again
		if (differs != kArmRegs && Arm(15) == r_arm.regs.r[15] && Arm(R8_FIQ) != r_arm.regs.r[R8_FIQ])
		{
			ArmBps({ });
			continue;
		}

		Check(differs == kArmRegs, "a PC of the ARM7 that is written with the value it has, in a service excursion of the running machine, ends the resume exemption: the "
			"breakpoint on it stops in the next burst with nothing executed, every register as the step left it (at %08X; %u of %u registers read as before)", r_arm.pc,
			differs, kArmRegs);
		r_arm.done = true;
	}

	Check(r_arm.done, "the case was taken with no FIQ in front of the instruction (%u tries)", r_arm.tries);
	ArmBps({ });

	// The guest holds its ARM7: the test behind this one has it released, and its program starts anew whatever
	// this test left of it
	GotoSh4(DSPTEST_G_HOLD_ARM7);
	Sh4Bps({ { 20, DSPTEST_G_HOLD_ARM7_SPIN } });

	while (!StopAt(stop, 20, DSPTEST_G_HOLD_ARM7_SPIN))
		CO_YIELD(r_arm.co);

	Sh4Bps({ });
	Check(!ArmClocked() && GetA("ENABLED") == 0, "(the guest holds its ARM7 for the test behind this one, which has it released: ENABLED reads 0)");

	// while it is held its registers are written as well
	r_arm.r3 = GetA("R1");
	rv = SetA("R1", r_arm.r3 ^ 0x00FF00FF);
	got = GetA("R1");
	rv2 = SetA("R1", r_arm.r3);
	Check(rv == 0 && got == (r_arm.r3 ^ 0x00FF00FF) && rv2 == 0 && GetA("R1") == r_arm.r3, "a register of the held ARM7 is written, and written back");

	CO_END(r_arm.co);
}

/*
	writes.reg.readonly: a row without a writer
*/
namespace {

struct {
	Co co;
} r_ro;

}

DBG_SELFTEST_GUEST(writes_reg_readonly, "writes.reg.readonly")
{
	std::vector<u8> before, after;
	Tally refused, edges;
	u32 count, rows = 0, with_writer = 0;
	const regs::Node* nodes = regs::Nodes(&count);

	CO_BEGIN(r_ro.co);

	while (!Settle(stop))
		CO_YIELD(r_ro.co);

	Check(MachineState(&before), "(the state of the machine, as a save state has it)");

	for (u32 n = 0; n < count; n++)
	{
		const regs::Node& node = nodes[n];

		for (u32 row = 0; row < node.registerCount; row++)
		{
			const regs::Register& reg = node.registers[row];

			if (reg.flags & regs::kRegWritable)
			{
				with_writer++;
				continue;
			}

			u64 was = 0, now = 0;
			const bool readable = reg.width != 0 && regs::Read(node, row, &was, nullptr, 0);

			rows++;

			// two values, of which one is not the value it has
			const int rv0 = regs::Write(node, row, ~was);
			const int rv1 = regs::Write(node, row, 0);

			refused.Add(rv0 == kErrNotWritable && rv1 == kErrNotWritable && (!readable || (regs::Read(node, row, &now, nullptr, 0) && now == was)),
				"%s.%s: %d and %d, it read %llX and reads %llX", node.path, reg.name, rv0, rv1, (unsigned long long)was, (unsigned long long)now);
		}

		edges.Add(regs::Write(node, node.registerCount, 0) == kErrNotWritable && regs::Write(node, 0xFFFFFFFF, 0) == kErrNotWritable, "%s: a write behind its last row is taken",
			node.path);
	}

	refused.Report("every row without a writer refuses a write, -32013");
	edges.Report("and so does every node for a row it does not have");

	Check(with_writer == 69 + 40, "109 rows have a writer: the 69 of dc.sh4.cpu and the 40 of dc.aica.arm7 that are not ENABLED (%u); %u rows have none", with_writer, rows);
	Check(MachineState(&after) && before == after, "the machine is what it was: its state, as a save state has it, has not changed (%zu bytes)", before.size());

	CO_END(r_ro.co);
}

/*
	writes.reg.width: bits of a value above the width of its row
*/
namespace {

struct {
	Co co;
} r_width;

}

DBG_SELFTEST_GUEST(writes_reg_width, "writes.reg.width")
{
	u32 r12, pc, r1, got;
	int rv, rv2;

	CO_BEGIN(r_width.co);

	while (!Settle(stop))
		CO_YIELD(r_width.co);

	while (!LoopStop(stop))
		CO_YIELD(r_width.co);

	r12 = GetS("R12");
	pc = GetS("PC");
	r1 = GetA("R1");

	rv = SetS("R12", 0x123456789ABCDEF0ull);
	got = GetS("R12");
	Check(rv == 0 && got == 0x9ABCDEF0 && Ctx().r[12] == 0x9ABCDEF0, "R12 of the SH4 written with 123456789ABCDEF0 reads %08X: the low 32 bits", got);

	rv = SetS("R12", 0xFFFFFFFF00000000ull | r12);
	Check(rv == 0 && GetS("R12") == r12, "(and is written back, with bits set above its width)");

	// the bits above the width do not decide what the device takes: an even PC under an odd high half, an odd
	// one under an even high half
	rv = SetS("PC", 0x0000000100000000ull | pc);
	rv2 = SetS("PC", 0xFFFFFFFE00000001ull | pc);
	Check(rv == 0 && rv2 == kErrNotWritable && GetS("PC") == pc, "the PC of the SH4 written with the value it has and bit 32 set is taken; with bit 0 set under a high half "
		"that is even it is refused (%d, %d; it reads %08X)", rv, rv2, GetS("PC"));

	rv = SetA("R1", 0xFEDCBA9876543210ull);
	got = GetA("R1");
	Check(rv == 0 && got == 0x76543210 && Arm(1) == 0x76543210, "R1 of the ARM7 written with FEDCBA9876543210 reads %08X", got);

	rv = SetA("R1", 0x100000000ull | r1);
	Check(rv == 0 && GetA("R1") == r1, "(and is written back)");

	got = GetA("CPSR");
	rv = SetA("CPSR", 0x1F00000000ull | got);
	rv2 = SetA("CPSR", 0x9300000000ull);
	Check(rv == 0 && rv2 == kErrNotWritable && GetA("CPSR") == got, "the CPSR of the ARM7: a mode in the bits above its width is not looked at (%d, %d)", rv, rv2);

	CO_END(r_width.co);
}

/*
	writesrun.neutral: the debugger's writes in a run that an Exit.* key ends. The state hash of such a run has
	to be that of a run nobody touched (tests/debugger/selftest/selftest.py --mode same-hash). The test never
	returns true: a session of it does not end by itself, and it is not named under the prefix of the session
	"writes", whose filter would select it.

	Every write is made at a stop that was latched at a slice end, the event breakpoint on the timers of the
	AICA, and is written back at the same stop: main memory, the page of the guest's code, a general register,
	SR, FPSCR with PR set, which empties the code cache, and R0 to R12 of the ARM7. At such a stop the interrupt
	acceptance of the slice end is owed already, and a write of SR moves nothing.
	Not written, because a correct writer is not neutral there: the PC of either processor (the owed remainder of
	the SH4 goes, which is no guest state but moves a block boundary); SR at a code breakpoint of the SH4 (the
	acceptance of a pending interrupt moves from the next slice end to the leave, as behind an LDC of the guest);
	the CPSR of the ARM7 (the mode switch rewrites bank slots that only the save state shows).

	Half of the stops write through dbg::mem and dbg::regs, the other half through the requests of a client
	(state.writeMemory, state.setRegister), handed to the dispatcher in place.

	The test has done its work early: within kNeutralStops sound bursts of the first one in which a timer
	overflows, of the some 6900 of a run of 600 vblanks. A run that ends in the tick of a stop is another matter
	(dbg_selftest_arm7.cpp, arm7n.*)
*/
namespace {

const u32 kNeutralEvent = 950;
const u32 kNeutralStops = 240;

struct {
	Co co;
	u32 stops, other, notOwed;
	u32 ram, code, gpr, sr, fpscr, arm, requests;
	u32 bad;
	char first[240];
} neutral;

void NeutralBad(const char* fmt, ...) DBG_PRINTF_LIKE(1, 2);

void NeutralBad(const char* fmt, ...)
{
	if (neutral.bad++ == 0)
	{
		va_list args;
		va_start(args, fmt);
		vsnprintf(neutral.first, sizeof(neutral.first), fmt, args);
		va_end(args);
	}
}

u32 neutral_request = 1;

// A request of a client, handed to the dispatcher in place; true for the answer {}
bool Request(const char* method, const std::string& params)
{
	char head[96];

	snprintf(head, sizeof(head), "{\"jsonrpc\":\"2.0\",\"id\":%u,\"method\":\"%s\",\"params\":", neutral_request++, method);

	const std::string answer = core::HandleRequestText(std::string(head) + params + "}");

	return answer.find("\"result\":{}") != std::string::npos;
}

bool RequestRegister(const char* path, const char* name, u32 value)
{
	char params[160];

	snprintf(params, sizeof(params), "{\"path\":\"%s\",\"name\":\"%s\",\"value\":\"0x%08X\"}", path, name, value);
	return Request("state.setRegister", params);
}

bool RequestMemory(u32 addr, const u8* bytes, u32 n)
{
	std::string params = "{\"target\":\"sh4\",\"address\":" + std::to_string(addr) + ",\"data\":[";

	for (u32 i = 0; i < n; i++)
		params += (i ? "," : "") + std::to_string(bytes[i]);

	return Request("state.writeMemory", params + "]}");
}

// One register of a processor written with another value and written back, by the writer or by a request
void NeutralRegister(bool by_request, const regs::Node& node, const char* name, u32 flip)
{
	const u32 row = RowOf(node, name);
	const u32 was = (u32)Get(node, row);
	const u32 other = was ^ flip;
	bool ok;

	if (by_request)
		ok = RequestRegister(node.path, name, other);
	else
		ok = regs::Write(node, row, other) == 0;

	if (!ok || (u32)Get(node, row) != other)
		NeutralBad("%s.%s written with %08X reads %08X", node.path, name, other, (u32)Get(node, row));

	if (by_request)
		ok = RequestRegister(node.path, name, was);
	else
		ok = regs::Write(node, row, was) == 0;

	if (!ok || (u32)Get(node, row) != was)
		NeutralBad("%s.%s written back with %08X reads %08X", node.path, name, was, (u32)Get(node, row));
}

void NeutralMemory(bool by_request, u32 addr, const u8* other, u32 n)
{
	u8 was[16], seen[16];
	bool ok;

	Peek(Target::Sh4, addr, n, was);

	if (by_request)
		ok = RequestMemory(addr, other, n);
	else
		ok = mem::Write(Target::Sh4, addr, n, other) == 0;

	if (!ok || !Peek(Target::Sh4, addr, n, seen) || !Same(seen, other, n))
		NeutralBad("%u bytes at %08X were not written", n, addr);

	if (by_request)
		ok = RequestMemory(addr, was, n);
	else
		ok = mem::Write(Target::Sh4, addr, n, was) == 0;

	if (!ok || !Peek(Target::Sh4, addr, n, seen) || !Same(seen, was, n))
		NeutralBad("%u bytes at %08X were not written back", n, addr);
}

}

DBG_SELFTEST_GUEST(writesrun_neutral, "writesrun.neutral")
{
	bool by_request;
	u8 bytes[16];

	CO_BEGIN(neutral.co);

	emu::SetActiveMask(kMaskAll);
	EventBp(kNeutralEvent);

	neutral.stops = neutral.other = neutral.notOwed = neutral.bad = 0;
	neutral.ram = neutral.code = neutral.gpr = neutral.sr = neutral.fpscr = neutral.arm = neutral.requests = 0;

	Check(stop.cause == StopCause::Start && !core::Started(), "at the start of the run an event breakpoint is listed on the timers of the AICA; the session has no server");

	for (;;)
	{
		if (!Resume())
			return false;
		CO_YIELD(neutral.co);

		if (stop.cause != StopCause::Event || stop.breakpointId != kNeutralEvent || Suspended() != SOUND_BURST_NONE)
		{
			neutral.other++;
			continue;
		}

		// the stop was latched at a slice end: the interrupt acceptance of that slice end is owed, which is what
		// makes a write of SR one that moves nothing
		if (!g_intc_deferred)
		{
			neutral.notOwed++;
			continue;
		}

		by_request = (neutral.stops & 1) != 0;

		// main memory that the guest does not use
		for (u32 i = 0; i < 16; i++)
			bytes[i] = (u8)(neutral.stops * 7 + i * 13 + 1);

		NeutralMemory(by_request, 0x8C000000 + kRamScratch, bytes, 16);
		neutral.ram++;

		// the page of the guest's code: its loop, with another instruction in the slot of the branch
		bytes[0] = (u8)MovImm(12, neutral.stops);
		bytes[1] = (u8)(MovImm(12, neutral.stops) >> 8);

		NeutralMemory(by_request, kSlot, bytes, 2);
		neutral.code++;

		// a general register: the counter of the loop
		NeutralRegister(by_request, Sh4Node(), "R8", 0x00FF00FF);
		neutral.gpr++;

		// SR: the other register bank, and another interrupt mask
		NeutralRegister(by_request, Sh4Node(), "SR", 0x200000F0);
		neutral.sr++;

		// FPSCR with PR set: the code cache is emptied, and again when it is written back
		NeutralRegister(by_request, Sh4Node(), "FPSCR", 0x00080000);
		neutral.fpscr++;

		// R0 to R12 of the ARM7, which stands between two sound bursts
		for (u32 i = 0; i <= 12; i++)
		{
			char name[8];

			snprintf(name, sizeof(name), "R%u", i);
			NeutralRegister(by_request, Arm7Node(), name, 0x0F0F0F0F << (i & 3));
			neutral.arm++;
		}

		neutral.requests += by_request ? 2 * (2 + 3 + 13) : 0;

		if (++neutral.stops == kNeutralStops)
			break;
	}

	NoEventBp();

	Check(neutral.stops == kNeutralStops && neutral.other == 0 && neutral.notOwed == 0, "%u stops latched at a slice end, each with its writes and their restores, in %u "
		"sound bursts (%u stops of another kind, %u at which no interrupt acceptance was owed)", neutral.stops, Bursts(), neutral.other, neutral.notOwed);
	Check(neutral.bad == 0, "every write read back, and every restore: %u did not%s%s", neutral.bad, neutral.bad ? ", the first: " : "", neutral.bad ? neutral.first : "");
	Check(neutral.ram == kNeutralStops && neutral.code == kNeutralStops, "%u writes of main memory and %u writes into the page of the guest's code, each with its restore",
		neutral.ram, neutral.code);
	Check(neutral.gpr == kNeutralStops && neutral.sr == kNeutralStops && neutral.fpscr == kNeutralStops, "%u writes of R8, %u of SR and %u of FPSCR that empty the code cache, "
		"each with its restore", neutral.gpr, neutral.sr, neutral.fpscr);
	Check(neutral.arm == 13 * kNeutralStops, "%u writes of R0 to R12 of the ARM7, each with its restore", neutral.arm);
	Check(neutral.requests == kNeutralStops / 2 * 2 * 18, "%u of the writes and restores were requests of a client, state.writeMemory and state.setRegister", neutral.requests);

	// The run has gone on behind what the test had to do. Nothing stops it any more; a stop that comes all the
	// same is a failure, and is resumed
	for (;;)
	{
		if (!Resume())
			return false;
		CO_YIELD(neutral.co);

		Check(false, "a stop that the test did not ask for: \"%s\" id %u", StopCauseName(stop.cause), stop.breakpointId);
	}

	CO_END(neutral.co);
}
