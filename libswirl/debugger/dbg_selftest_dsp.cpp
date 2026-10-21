/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of the DSP's run control: the DSP phase of the sound burst and the end of a sample as places the
	burst is suspended at, the breakpoints, the resume exemption and the steps of the DSP (dbg_dsp.cpp), what the
	park loop does with them (dbg_emu.cpp), and the rows of dc.aica.dsp.

	    nulldc3 --config config:Headless=yes , audio:backend=none , Debugger:SelfTest=dsp tests/debugger/guest/dsptest/dsptest.elf

	The guest is the sounding guest of the tree (tests/debugger/guest/dsptest, README.md there): an SH4 program
	that counts in a loop, an ARM7 program of its own and a DSP program of 18 steps, from step 0, which _start
	loads; the addresses come from dsptest_labels.inc. Every test is a guest test: it stands where a client stands
	and is handed every stop. A test begins by bringing the guest to where a session finds it after its boot
	(Settle), whatever the test before it left: the SH4 in its loop, the ARM7 clocked and in its own, the DSP
	program loaded, no breakpoint listed, no sound burst suspended.

	The DSP is clocked whatever ARMRST says: every one of its 128 steps executes in every sample, also while the
	guest holds its ARM7. So a listed breakpoint of the DSP whose category is active is reached in every sample,
	and a resume from it stops in front of the same step one sample later. A sample is 1/32 of a burst, and the
	sample a stop is in is told by the counts of the machine (Sample()).

	What another thread of the debugger does (a call), and what the host does (a stop, a start), is done by a
	thread the test starts: "the other thread". What has to happen at a moment of the emulated machine - in the
	tick in which a check suspends a burst - is done by a scheduler callback of the tests ("the hand"), which is
	registered behind the AICA's and so runs behind it in its tick.

	Four tests are not part of "dsp", because a session of theirs does not end with the summary of the driver
	(dspn.*, at the end of this file): they are what tests/debugger/guest/neutral holds the sound line and the
	state hash of a run against.
*/

#include "dbg_selftest.h"
#include "dbg_selftest_nap.h"
#include "dbg_dsp.h"
#include "dbg_dsp_private.h"
#include "dbg_arm7.h"
#include "dbg_sh4.h"
#include "dbg_regs.h"

#include "libswirl.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7_context.h"
#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp.h"
#include "hw/aica/dsp_core/aica_dsp_core.h"

#include "../../tests/debugger/guest/dsptest/dsptest_labels.inc"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dbg { namespace emu { namespace test {

// dbg_emu.cpp: invoked behind the listener with every state that is published
extern void (*observer)(const RunState& state);

// dbg_emu.cpp: the threads of the session start without a start park
extern bool plainStart;

} } }

namespace {

using namespace dbg;
using selftest::Check;
using selftest::Note;

typedef std::chrono::steady_clock Clock;

const u32 kResetVector = 0xA0000000;

// The ARM7 program of the guest
const u32 kMain  = DSPTEST_A_MAIN_LOOP;		// the first instruction of its loop
const u32 kNever = 0x000F0000;				// wave memory that the guest never executes

// The DSP program of the guest: steps 0 to 17 (dsptest_dsp.txt), step 127 and the steps behind the program empty
const u32 kSteps    = 128;
const u32 kLast     = 127;
const u32 kSite     = 5;			// an odd step of the program: an IWT, an MWT and a YRL

const u32 kMaskAll = (1u << (u32)Category::Events) | (1u << (u32)Category::Sh4) | (1u << (u32)Category::Arm7) | (1u << (u32)Category::Dsp);

// The registers of the ARM7 as the debugger reads them: R0..R14, the PC, the CPSR, the SPSR and the banks
const u32 kArmRegs = SPSR_FIQ + 1;

// The period of the AICA's scheduler callback, in cycles of the SH4 (AICA_TICK of libswirl.cpp)
const u64 kAicaPeriod = 145124;

// Registers of the AICA, as offsets into its register file
const u32 kTIMB = 0x2894;

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

/*
	The machine
*/
SoundCPU* Scpu()
{
	return sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
}

AICA* Aica()
{
	return sh4_cpu->GetA0H<AICA>(A0H_AICA);
}

DSP* Dsp()
{
	return sh4_cpu->GetA0H<DSP>(A0H_DSP);
}

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

// Every register of the ARM7, as the debugger reads them
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

// What the sound CPU writes into a save state, which is what the sound line and the state hash fold of it: bool
// aica_interr, u32 aica_reg_L, bool e68k_out, u32 e68k_reg_L, u32 e68k_reg_M, the RN_ARM_REG_COUNT words of the
// context, bool armIrqEnable, bool armFiqEnable, int armMode, bool enabled
const size_t kScpuWordsAt = 1 + 4 + 1 + 4 + 4;
const size_t kScpuStateSize = kScpuWordsAt + RN_ARM_REG_COUNT * 4 + 1 + 1 + 4 + 1;

std::vector<u8> ScpuState()
{
	void* data = nullptr;
	unsigned int size = 0;

	Scpu()->serialize(&data, &size);

	std::vector<u8> state(size);

	data = state.data();
	size = 0;
	Scpu()->serialize(&data, &size);

	return state;
}

// A word of the context as it is stored, not as the debugger reads it
u32 StoredWord(const std::vector<u8>& state, u32 index)
{
	u32 word = 0xDEADBEEF;

	if (state.size() == kScpuStateSize)
		memcpy(&word, &state[kScpuWordsAt + index * 4], 4);

	return word;
}

// How many words of the stored context, INTR_PEND left out, differ between two states (the first in *first)
u32 StoredDiffer(const std::vector<u8>& a, const std::vector<u8>& b, u32* first)
{
	u32 n = 0;

	*first = RN_ARM_REG_COUNT;

	for (u32 i = 0; i < RN_ARM_REG_COUNT; i++)
	{
		if (i == INTR_PEND || StoredWord(a, i) == StoredWord(b, i))
			continue;

		if (n++ == 0)
			*first = i;
	}

	return n;
}

u32 AicaReg16(u32 offset)
{
	u8 bytes[2] = { 0, 0 };

	Aica()->DebugReadRegs(offset, 2, bytes);
	return bytes[0] | (bytes[1] << 8);
}

u32 Suspended()
{
	return virtualDreamcast->SoundBurstSuspended();
}

// What the sound bursts of the machine have done: started, ARM phases, time steps, tails
struct Counts {
	u32 n[4];
};

Counts ReadCounts()
{
	Counts c;

	virtualDreamcast->SoundBurstCounts(c.n);
	return c;
}

// What the DSP phases have done: those that ran to the end of their sample, those the debug loop executed in
struct DspCounts {
	u32 n[2];
};

DspCounts ReadDspCounts()
{
	DspCounts c;

	virtualDreamcast->SoundBurstDspCounts(c.n);
	return c;
}

DspDebugState DspState()
{
	DspDebugState state;

	memset(&state, 0xEE, sizeof(state));

	if (!Dsp()->DebugGetState(&state))
		state.step = 999;

	return state;
}

// The step the DSP stands in front of
u32 DspStep()
{
	return DspState().step;
}

// MDEC_CT, which counts down once a sample, at the end of its step 127
u32 Mdec()
{
	return DspState().mdecCt & 0xFFFF;
}

/*
	The sample a burst stands in. A burst makes 32 time steps and one tail, and a reset can discard one in between:
	at a stop behind a burst the test notes what the counts have then (NoteBetweenBursts), and behind it the
	sample of the burst that runs is the time steps made since, less 32 for each tail. 0 between two bursts, and
	in a burst the sample whose DSP phase or ARM phase it stands in, or the next one at the end of a sample
*/
u32 sample_base;

void NoteBetweenBursts()
{
	const Counts c = ReadCounts();

	sample_base = c.n[2] - 32 * c.n[3];
}

u32 Sample()
{
	const Counts c = ReadCounts();

	return c.n[2] - 32 * c.n[3] - sample_base;
}

/*
	The rows of dc.aica.dsp, through the readers of the device tree
*/
const char* const kDspRows[] = { "STEP", "MDEC_CT", "RBP", "RBL", "ACC", "Y_REG", "FRC_REG", "ADRS_REG", "LATCH", "LATCH_COPY", "MEMVAL", "NOFL_PIPE",
	"MIXS_BANK" };
const u32 kDspRowCount = sizeof(kDspRows) / sizeof(kDspRows[0]);
const u32 kDatapathFrom = 4;		// the nine of M3 come behind the four of M1

struct Rows {
	u64 v[kDspRowCount];
	bool ok;
};

Rows ReadRows()
{
	Rows rows;
	const regs::Node* node = regs::Find("dc.aica.dsp");

	rows.ok = node != nullptr && node->registerCount == kDspRowCount;

	for (u32 i = 0; i < kDspRowCount; i++)
	{
		const int index = node ? regs::FindRegister(*node, kDspRows[i]) : -1;

		rows.v[i] = 0xEEEEEEEE;
		rows.ok = rows.ok && index == (int)i && regs::Read(*node, (u32)index, &rows.v[i], nullptr, 0);
	}

	return rows;
}

// The rows the state gives, each under its width
Rows RowsOf(const DspDebugState& s)
{
	Rows rows;

	rows.ok = true;
	rows.v[0] = s.step & 0xFF;
	rows.v[1] = s.mdecCt & 0xFFFF;
	rows.v[2] = s.rbpl & 0xFFF;
	rows.v[3] = (s.rbpl >> 13) & 3;
	rows.v[4] = (u32)s.acc & 0x3FFFFFF;
	rows.v[5] = (u32)s.yReg & 0xFFFFFF;
	rows.v[6] = s.frcReg & 0x1FFF;
	rows.v[7] = s.adrsReg & 0xFFF;
	rows.v[8] = s.latch & 0xFFFF;
	rows.v[9] = s.latchCopy & 0xFFFF;
	rows.v[10] = s.memval & 0xFFFF;
	rows.v[11] = s.noflPipe & 3;
	rows.v[12] = s.mixsBank & 1;

	return rows;
}

// The first row in which two differ, kDspRowCount when none does
u32 RowsDiffer(const Rows& a, const Rows& b)
{
	for (u32 i = 0; i < kDspRowCount; i++)
	{
		if (a.v[i] != b.v[i])
			return i;
	}

	return kDspRowCount;
}

// The state against the core's own fields, which a selftest may read within one call
bool StateIsCore(const DspDebugState& s)
{
	const AicaDsp* core = Dsp()->Core();

	return s.step == core->step && s.mdecCt == core->MDEC_CT && s.rbpl == core->RBPL && s.acc == core->ACC && s.yReg == core->Y_REG
		&& s.frcReg == core->FRC_REG && s.adrsReg == core->ADRS_REG && s.latch == core->latch && s.latchCopy == core->latch_copy && s.memval == core->memval
		&& s.noflPipe == core->nofl_pipe && s.mixsBank == core->mixs_dsp;
}

/*
	The tables
*/
void DspBps(std::initializer_list<std::pair<u32, u32>> list)
{
	u32 ids[16], steps[16];
	u32 n = 0;

	for (const auto& bp : list)
	{
		ids[n] = bp.first;
		steps[n] = bp.second;
		n++;
	}

	emu::SetDspBreakpoints(ids, steps, n);
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

/*
	Everything that is published, in the order it was, since the test looked last (TakeSeen). The list has an end:
	a test that goes wrong can publish without end - every step of the DSP is a stop -, and it has to fail by a
	check of its own, not by the memory of the machine
*/
struct Seen {
	Lifetime lifetime;
	u32 stopId;
	StopCause cause;
	u32 breakpointId;
	u32 notice;
};

// More steps of the DSP than a sound burst has (32 samples of 128 steps): a loop of steps that waits for something
// of its burst ends here at the latest, and fails
const u32 kStepsMax = 32 * kSteps + 64;

const size_t kSeenMax = kStepsMax + 1000;

std::mutex seen_lock;
std::vector<Seen> seen;

void Observer(const emu::RunState& state)
{
	std::lock_guard<std::mutex> lock(seen_lock);

	if (seen.size() < kSeenMax)
		seen.push_back({ state.lifetime, state.stopId, state.cause, state.breakpointId, state.notice });
}

std::vector<Seen> TakeSeen()
{
	std::lock_guard<std::mutex> lock(seen_lock);
	std::vector<Seen> rv;

	rv.swap(seen);
	return rv;
}

// "Parked:pause Running ..." for a line
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
	}

	return text.empty() ? "nothing" : text;
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

bool SeenHas(const std::vector<Seen>& list, Lifetime lifetime, StopCause cause)
{
	for (const Seen& s : list)
	{
		if (s.lifetime == lifetime && s.cause == cause)
			return true;
	}

	return false;
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

/*
	The bound of a wait, as the selftest of the ARM7 has it (dbg_selftest_arm7.cpp). A test that has armed
	something, or that waits for what another thread does, returns false and is handed the next stop. Where the
	machine never gives that stop the session would wait until the watchdog of its runner ended the process, with
	no summary. So every wait has a bound, kBoundMs: a thread of the session, the watch, cuts a wait that is older
	than the bound, and asks for a stop that the machine can still give, each for a tenth of the bound, in this
	order: a pause, where the published state is Running; a stop of the host, and where the thread stays parked
	through that a call that resumes it; once the machine is stopped, a start of the host. The test is handed that
	stop, fails by a check that says so, and ends; every test behind it fails at once with one check. A machine
	that gives no stop through all of that has the watch print the failed check itself and end the process.
	A session that passes waits less than a tenth of a second for any of its stops. The last wait of a test that
	the run ends (dspn.*, NEUTRAL_END) has no bound.
*/
const int kBoundMs = 30000;

// How long a test holds a slice of the machine, or waits on the emulator thread for another thread to queue a call
const int kHoldMs = kBoundMs / 12;

struct Bound {
	std::mutex lock;
	const char* test = nullptr;		// the test that runs, or waits
	bool waiting = false;			// it has returned false: it waits for a stop
	bool unbounded = false;			// that wait is one the run ends
	Clock::time_point since;
	double longest = 0;				// ms: the longest wait of the test (emulator thread only)
	const char* cut = nullptr;		// the test whose wait the watch cut
	bool taken = false;				// the stop behind the cut was handed to that test
	const char* how = "";			// what the watch asked the machine for
	std::thread* watch = nullptr;
	std::mutex restart;				// one start of the host at a time
};

// Never destroyed: the watch runs until the process ends
Bound& bound = *new Bound();

bool WaitLifetime(Lifetime want, int timeout_ms = kBoundMs)
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

// The host starts the stopped machine again; the next thread parks at its start unless plain. False when the
// machine is not stopped: the watch and a thread of a test can both find it stopped, and only one starts it
bool Restart(bool plain)
{
	std::lock_guard<std::mutex> l(bound.restart);

	if (emu::GetRunState().lifetime != Lifetime::Stopped)
		return false;

	emu::test::plainStart = plain;
	virtualDreamcast->Resume();
	return true;
}

bool CutTaken()
{
	std::lock_guard<std::mutex> l(bound.lock);

	return bound.taken;
}

// Until the test whose wait was cut is handed a stop, or the machine is stopped, for ms at most
bool CutUntil(int ms, bool stopped)
{
	for (Clock::time_point t0 = Clock::now(); MsSince(t0) < ms; Nap(2))
	{
		if (CutTaken() || (stopped && emu::GetRunState().lifetime == Lifetime::Stopped))
			return true;
	}

	return false;
}

void CutAsk(const char* how)
{
	std::lock_guard<std::mutex> l(bound.lock);

	bound.how = how;
}

void Watch()
{
	const int step = kBoundMs / 10;
	const char* test = nullptr;

	while (test == nullptr)
	{
		Nap(20);

		std::lock_guard<std::mutex> l(bound.lock);

		if (bound.waiting && !bound.unbounded && MsSince(bound.since) > kBoundMs)
			bound.cut = test = bound.test;
	}

	if (emu::GetRunState().lifetime == Lifetime::Running)
	{
		CutAsk("a pause");
		emu::RequestPause();

		if (CutUntil(step, false))
			return;
	}

	if (emu::GetRunState().lifetime != Lifetime::Stopped)
	{
		CutAsk("a stop and a start of the host");
		virtualDreamcast->Stop([] { });

		if (!CutUntil(step, true))
		{
			CutAsk("a stop of the host, a call that resumes the thread that stayed parked, and a start of the host");
			emu::Call(emu::CC_EXEC, [] { emu::Resume(); });
			CutUntil(step, true);
		}
	}

	if (CutTaken() || (Restart(false) && CutUntil(step, false)))
		return;

	printf("SelfTest: %s: FAILED: the stop the test waited for did not come within the bound of the session, %d s, and the machine gave none for a pause, "
		"a stop and a start of the host either: the session ends here, with no summary\n", test, kBoundMs / 1000);
	fflush(stdout);
	std::_Exit(1);
}

// A test whose last wait the run ends: it has no bound
void Unbounded()
{
	std::lock_guard<std::mutex> l(bound.lock);

	bound.unbounded = true;
}

// A guest test of this file, with the bound of its waits
bool Bounded(const emu::RunState& stop, const char* name, bool (*test)(const emu::RunState& stop))
{
	const char* cut;
	const char* how;

	{
		std::lock_guard<std::mutex> l(bound.lock);

		if (bound.test != name)
		{
			bound.test = name;
			bound.unbounded = false;
			bound.longest = 0;
		}

		if (bound.waiting && bound.cut == nullptr)
			bound.longest = std::max(bound.longest, MsSince(bound.since));

		bound.waiting = false;
		cut = bound.cut;
		how = bound.how;

		if (cut == name)
			bound.taken = true;
	}

	if (cut != nullptr && cut != name)
	{
		Check(false, "not run: a wait of %s did not end within the bound of the session, %d s, and the machine stands where no test begins", cut, kBoundMs / 1000);
		return true;
	}

	if (cut != nullptr)
	{
		Check(false, "the stop the test waited for did not come within the bound of the session, %d s: the session asked the machine for %s, and was handed a stop "
			"with \"%s\"; the test ends here", kBoundMs / 1000, how, StopCauseName(stop.cause));
		return true;
	}

	if (bound.watch == nullptr)
		bound.watch = new std::thread(Watch);

	if (test(stop))
	{
		Note("the longest wait of the test for a stop: %.1f ms (the bound of a wait is %d s)", bound.longest, kBoundMs / 1000);
		return true;
	}

	std::lock_guard<std::mutex> l(bound.lock);

	bound.since = Clock::now();
	bound.waiting = true;
	return false;
}

// DBG_SELFTEST_GUEST with the bound of its waits: every guest test of this file
#define BOUNDED_GUEST_TEST(ident, name) \
	static bool ident(const dbg::emu::RunState& stop); \
	static bool ident##_bounded(const dbg::emu::RunState& stop) { return Bounded(stop, name, ident); } \
	static dbg::selftest::Test ident##_selftest(name, nullptr, ident##_bounded); \
	static bool ident(const dbg::emu::RunState& stop)

/*
	The hand: a scheduler callback of the tests, in every slice in which emulated time passes while it is on. It
	is registered behind the callback of the AICA, so in a tick they share it runs behind the sound burst. It says
	what the slices of a run showed, and it is where a test asks for something at a moment of the machine.
*/
enum HandMode {
	HAND_WATCH,					// looks only
	HAND_PAUSE,					// asks for a pause once the bursts started have reached hand.target
	HAND_STOP_SUSPENDED,		// has the host stop the machine in the tick in which a burst was suspended
	HAND_EXIT_SUSPENDED,		// has the guest end the run there
	HAND_RESET_SUSPENDED,		// has the guest reset the machine there
	HAND_STOP_FIRST,			// has the host stop the machine in the first slice
	HAND_EXIT_FIRST,			// has the guest end the run there
};

struct {
	int id = -1;
	bool on = false;
	HandMode mode = HAND_WATCH;
	u32 target = 0;
	bool asked = false;

	// what the slices showed since HandOn()
	u32 slices = 0;
	u32 notParked = 0;				// g_parked was clear
	u32 notPublishedParked = 0;		// the published lifetime was not Parked
	u32 suspended = 0;				// a sound burst was suspended
	u32 suspendedDsp = 0;			// in front of a DSP step
	u32 latched = 0;				// the stop latch was set

	// what the stop callback of HAND_STOP_SUSPENDED and of the tests found
	std::atomic<int> stopCallbacks { 0 };
	std::atomic<u32> stopSuspended { 0 };
	Counts stopCounts;
	DspCounts stopDspCounts;
} hand;

void HandStopped()
{
	hand.stopCallbacks++;
	hand.stopSuspended = Suspended();
	hand.stopCounts = ReadCounts();
	hand.stopDspCounts = ReadDspCounts();
}

int HandTick(void*, int, int, int)
{
	if (!hand.on)
		return 0;

	const u32 burst = Suspended();

	hand.slices++;

	if (!g_parked.load())
		hand.notParked++;
	if (emu::GetRunState().lifetime != Lifetime::Parked)
		hand.notPublishedParked++;
	if (burst != SOUND_BURST_NONE)
		hand.suspended++;
	if (burst == SOUND_BURST_DSP)
		hand.suspendedDsp++;
	if (g_stop_latched)
		hand.latched++;

	switch (hand.mode)
	{
	case HAND_PAUSE:
		if (!hand.asked && (s32)(ReadCounts().n[0] - hand.target) >= 0)
		{
			hand.asked = true;
			emu::RequestPause();
		}
		break;

	case HAND_STOP_SUSPENDED:
	case HAND_EXIT_SUSPENDED:
	case HAND_RESET_SUSPENDED:
	case HAND_STOP_FIRST:
	case HAND_EXIT_FIRST:
		if (!hand.asked && (hand.mode == HAND_STOP_FIRST || hand.mode == HAND_EXIT_FIRST || burst != SOUND_BURST_NONE))
		{
			hand.asked = true;

			if (hand.mode == HAND_STOP_SUSPENDED || hand.mode == HAND_STOP_FIRST)
				virtualDreamcast->Stop(HandStopped);
			else if (hand.mode == HAND_EXIT_SUSPENDED || hand.mode == HAND_EXIT_FIRST)
				virtualDreamcast->RequestExit();
			else
				virtualDreamcast->RequestReset();
		}
		break;

	default:
		break;
	}

	// again in the next slice
	return 1;
}

// On the emulator thread, parked
void HandOn(HandMode mode, u32 target = 0)
{
	if (hand.id < 0)
		hand.id = sh4_sched_register(nullptr, 0, HandTick);

	hand.on = true;
	hand.mode = mode;
	hand.target = target;
	hand.asked = false;
	hand.slices = 0;
	hand.notParked = 0;
	hand.notPublishedParked = 0;
	hand.suspended = 0;
	hand.suspendedDsp = 0;
	hand.latched = 0;

	sh4_sched_request(hand.id, 1);
}

void HandOff()
{
	hand.on = false;
}

/*
	The steps of every test
*/
void GotoSh4(u32 pc)
{
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

bool StepDsp()
{
	int rv = emu::Step(Target::Dsp, StepKind::Into);

	if (rv != 0)
		Check(false, "a step of the DSP was refused: %d", rv);

	return rv == 0;
}

bool StepArm()
{
	int rv = emu::Step(Target::Arm7, StepKind::Into);

	if (rv != 0)
		Check(false, "a step of the ARM7 was refused: %d", rv);

	return rv == 0;
}

bool StepSh4()
{
	const char* why = "";
	int rv = emu::Step(Target::Sh4, StepKind::Into, &why);

	if (rv != 0)
		Check(false, "a step of the SH4 was refused: %d %s", rv, why ? why : "");

	return rv == 0;
}

void ClearTables()
{
	DspBps({ });
	ArmBps({ });
	Sh4Bps({ });
	NoEventBp();
	dsp::test::SetLoopForced(false);
	emu::SetActiveMask(kMaskAll);
}

/*
	Settle: the guest as a session finds it behind its boot. The SH4 is sent to its loop (through the release of
	the ARM7 when a test left that held), or, where it is not in the guest's code behind _start (the machine was
	reset, and a stop of the DSP in the first burst of the boot came in front of _start or inside it), goes there
	through the boot, which loads the DSP program; and the run is stopped behind the first sound burst in which a
	timer of the AICA overflows: the ARM7 is in its loop then, and no burst is suspended. Returns false while it
	has armed something
*/
const u32 kSettleId = 900;

struct {
	Co co;
	u32 tries;
} settle;

bool Settle(const emu::RunState& stop)
{
	CO_BEGIN(settle.co);

	emu::test::observer = Observer;
	emu::test::plainStart = false;

	HandOff();
	ClearTables();

	if (Ctx().pc >= DSPTEST_G_MAIN && Ctx().pc < DSPTEST_G_TABLE)
		GotoSh4(ArmClocked() ? DSPTEST_G_MAIN : DSPTEST_G_RELEASE_ARM7);

	Sh4Bps({ { kSettleId, DSPTEST_G_MAIN_LOOP } });

	if (!Resume())
		break;
	CO_YIELD(settle.co);

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kSettleId || !ArmClocked())
		Check(false, "the guest gets to its main loop with the ARM7 released (a stop with \"%s\", SH4 pc %08X, the ARM7 %s)", StopCauseName(stop.cause),
			Ctx().pc, ArmClocked() ? "clocked" : "held");

	Sh4Bps({ });

	for (settle.tries = 0; settle.tries < 8; settle.tries++)
	{
		EventBp(kSettleId);

		if (!Resume())
			break;
		CO_YIELD(settle.co);

		NoEventBp();

		if (stop.cause == StopCause::Event && Suspended() == SOUND_BURST_NONE && ArmClocked() && ArmPc() >= kMain && ArmPc() < DSPTEST_A_FIQ_END + 8)
			break;
	}

	if (settle.tries == 8)
		Check(false, "the ARM7 of the guest gets to its loop, and a timer of the AICA overflows in a burst (a stop with \"%s\", the ARM7 at %08X)",
			StopCauseName(stop.cause), ArmPc());

	{
		u16 words[4] = { 0, 0, 0, 0 };

		if (!Dsp()->DebugGetProgramStep(0, words) || (words[0] | words[1] | words[2] | words[3]) == 0)
			Check(false, "the guest has loaded its DSP program: step 0 is %04X %04X %04X %04X", words[0], words[1], words[2], words[3]);
	}

	NoteBetweenBursts();
	TakeSeen();

	CO_END(settle.co);
}

/*
	A stop behind a sound burst, with none suspended: the event breakpoint on the timers of the AICA, which
	overflow in every burst of the guest while its ARM7 is clocked. Returns false while it has armed something
*/
const u32 kBehindId = 901;

struct {
	Co co;
} behind;

bool BehindBurst(const emu::RunState& stop)
{
	CO_BEGIN(behind.co);

	EventBp(kBehindId);

	if (!Resume())
		break;
	CO_YIELD(behind.co);

	NoEventBp();

	if (stop.cause != StopCause::Event || stop.breakpointId != kBehindId || Suspended() != SOUND_BURST_NONE)
		Check(false, "a stop behind a sound burst, at the event breakpoint on the timers of the AICA (a stop with \"%s\", burst %u)", StopCauseName(stop.cause),
			Suspended());
	else
		NoteBetweenBursts();

	CO_END(behind.co);
}

/*
	A pause behind `bursts` more sound bursts, asked for by the hand: the stop of a run that nothing of the sound
	side stopped. It counts bursts, and works whether the ARM7 is clocked or held
*/
struct {
	Co co;
} pause_after;

bool PauseAfter(const emu::RunState& stop, u32 bursts)
{
	CO_BEGIN(pause_after.co);

	HandOn(HAND_PAUSE, ReadCounts().n[0] + bursts);

	if (!Resume())
		break;
	CO_YIELD(pause_after.co);

	HandOff();

	if (stop.cause == StopCause::Pause && Suspended() == SOUND_BURST_NONE)
		NoteBetweenBursts();

	CO_END(pause_after.co);
}

/*
	A stop at a breakpoint of the DSP, which stays listed: the burst is suspended in front of `step`. It is in the
	first sample of the burst when the test stood behind a burst, as it does behind Settle
*/
const u32 kAtId = 902;

struct {
	Co co;
} at;

bool SuspendAtStep(const emu::RunState& stop, u32 step)
{
	CO_BEGIN(at.co);

	DspBps({ { kAtId, step } });

	if (!Resume())
		break;
	CO_YIELD(at.co);

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kAtId || DspStep() != step || Suspended() != SOUND_BURST_DSP)
		Check(false, "a stop at a breakpoint of the DSP on step %u, the burst suspended in front of it (a stop with \"%s\" id %u, the DSP at step %u, burst %u)",
			step, StopCauseName(stop.cause), stop.breakpointId, DspStep(), Suspended());

	CO_END(at.co);
}

/*
	From a stop at the breakpoint SuspendAtStep() listed, resumes until the stop is in sample `sample` of its burst:
	one sample a resume. Returns false while it has armed something
*/
struct {
	Co co;
	u32 n;
} to_sample;

bool ToSample(const emu::RunState& stop, u32 sample)
{
	CO_BEGIN(to_sample.co);

	for (to_sample.n = 0; to_sample.n < 70 && Sample() != sample; to_sample.n++)
	{
		if (!Resume())
			break;
		CO_YIELD(to_sample.co);

		if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kAtId || Suspended() != SOUND_BURST_DSP)
		{
			Check(false, "on the way to sample %u: a stop at the breakpoint of the DSP in every sample (a stop with \"%s\" id %u, burst %u)", sample,
				StopCauseName(stop.cause), stop.breakpointId, Suspended());
			break;
		}
	}

	if (Sample() != sample)
		Check(false, "the stop is in sample %u of its burst, not in sample %u (%u resumes)", Sample(), sample, to_sample.n);

	CO_END(to_sample.co);
}

/*
	A stop at a breakpoint of the ARM7, which stays listed: the burst is suspended in front of addr
*/
const u32 kArmAtId = 903;

struct {
	Co co;
} arm_at;

bool SuspendAtArm(const emu::RunState& stop, u32 addr)
{
	CO_BEGIN(arm_at.co);

	ArmBps({ { kArmAtId, addr } });

	if (!Resume())
		break;
	CO_YIELD(arm_at.co);

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kArmAtId || ArmPc() != addr || Suspended() != SOUND_BURST_ARM7)
		Check(false, "a stop at a breakpoint of the ARM7 on %08X, the burst suspended (a stop with \"%s\" id %u, the ARM7 at %08X, burst %u)", addr,
			StopCauseName(stop.cause), stop.breakpointId, ArmPc(), Suspended());

	CO_END(arm_at.co);
}

/*
	The guest holds its ARM7, and the SH4 stops in its spin, in front of the next burst. Returns false while it has
	armed something
*/
const u32 kHeldSpinId = 904;

struct {
	Co co;
	Counts c;
} hold;

bool HoldArm7(const emu::RunState& stop)
{
	CO_BEGIN(hold.co);

	hold.c = ReadCounts();
	GotoSh4(DSPTEST_G_HOLD_ARM7);
	Sh4Bps({ { kHeldSpinId, DSPTEST_G_HOLD_ARM7_SPIN } });

	if (!Resume())
		break;
	CO_YIELD(hold.co);

	Sh4Bps({ });

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kHeldSpinId || ArmClocked() || ReadCounts().n[0] != hold.c.n[0])
		Check(false, "the guest holds its ARM7 in front of the next burst: a stop at g_hold_arm7_spin (\"%s\" id %u), the ARM7 %s, %u bursts since",
			StopCauseName(stop.cause), stop.breakpointId, ArmClocked() ? "clocked" : "held", ReadCounts().n[0] - hold.c.n[0]);

	CO_END(hold.co);
}

}

/*
	dsp.burst.plain: what the DSP phases of the bursts are, by the counts of the machine, with nothing armed: one
	phase a sample, which runs to the end of its sample, and none by the debug loop; with the ARM7 clocked and with
	it held
*/
namespace {

struct {
	Co co;
	Counts a;
	DspCounts da;
	u32 checks;
} b_plain;

}

BOUNDED_GUEST_TEST(dsp_burst_plain, "dsp.burst.plain")
{
	Counts c;
	DspCounts dc;
	u32 bursts;

	CO_BEGIN(b_plain.co);

	while (!Settle(stop))
		CO_YIELD(b_plain.co);

	Check(dbg_dsp_armed == 0, "nothing of the DSP is armed: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);

	while (!PauseAfter(stop, 2))
		CO_YIELD(b_plain.co);

	b_plain.a = ReadCounts();
	b_plain.da = ReadDspCounts();
	b_plain.checks = dsp::test::checks;

	while (!PauseAfter(stop, 5))
		CO_YIELD(b_plain.co);

	c = ReadCounts();
	dc = ReadDspCounts();
	bursts = c.n[0] - b_plain.a.n[0];

	Check(stop.cause == StopCause::Pause && ArmClocked() && bursts >= 5 && c.n[3] - b_plain.a.n[3] == bursts && c.n[2] - b_plain.a.n[2] == 32 * bursts,
		"two pauses with %u whole sound bursts between them, the ARM7 clocked", bursts);
	Check(dc.n[0] - b_plain.da.n[0] == 32 * bursts && dc.n[1] == b_plain.da.n[1],
		"with nothing armed every sample has one DSP phase, which runs to the end of its sample, and none goes through the debug loop: %u and %u in %u bursts",
		dc.n[0] - b_plain.da.n[0], dc.n[1] - b_plain.da.n[1], bursts);
	Check(dsp::test::checks == b_plain.checks, "and no check of the DSP was made (%u)", dsp::test::checks - b_plain.checks);

	// the guest holds its ARM7: the DSP is clocked all the same
	GotoSh4(DSPTEST_G_HOLD_ARM7);

	while (!PauseAfter(stop, 2))
		CO_YIELD(b_plain.co);

	b_plain.a = ReadCounts();
	b_plain.da = ReadDspCounts();
	b_plain.checks = dsp::test::checks;

	while (!PauseAfter(stop, 5))
		CO_YIELD(b_plain.co);

	c = ReadCounts();
	dc = ReadDspCounts();
	bursts = c.n[0] - b_plain.a.n[0];

	Check(stop.cause == StopCause::Pause && !ArmClocked() && bursts >= 5 && c.n[1] == b_plain.a.n[1] && c.n[2] - b_plain.a.n[2] == 32 * bursts,
		"two pauses with %u sound bursts between them, the ARM7 held in reset: no ARM phase, 32 time steps a burst", bursts);
	Check(dc.n[0] - b_plain.da.n[0] == 32 * bursts && dc.n[1] == b_plain.da.n[1] && dsp::test::checks == b_plain.checks,
		"and one DSP phase a sample, none by the debug loop, no check: %u and %u in %u bursts", dc.n[0] - b_plain.da.n[0], dc.n[1] - b_plain.da.n[1], bursts);

	CO_END(b_plain.co);
}

/*
	dsp.bp.hit: a breakpoint of the DSP stops in the next burst, in front of its step in the first sample, and the
	SH4 executes nothing behind it; a resume from it stops in front of the same step one sample later, in the same
	burst and with no emulated time in between
*/
namespace {

struct {
	Co co;
	Counts c;
	DspCounts dc;
	u32 stopId, checks, mdec;
	u64 time;
} bp_hit;

}

BOUNDED_GUEST_TEST(dsp_bp_hit, "dsp.bp.hit")
{
	Counts c;
	DspCounts dc;
	std::vector<Seen> list;

	CO_BEGIN(bp_hit.co);

	while (!Settle(stop))
		CO_YIELD(bp_hit.co);

	bp_hit.c = ReadCounts();
	bp_hit.dc = ReadDspCounts();
	bp_hit.stopId = stop.stopId;
	bp_hit.checks = dsp::test::checks;

	DspBps({ { 7, kSite } });
	Check(dbg_dsp_armed != 0, "an active listed breakpoint of the DSP arms the debug loop: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);

	TakeSeen();
	HandOn(HAND_WATCH);

	if (!Resume())
		break;
	CO_YIELD(bp_hit.co);

	HandOff();
	c = ReadCounts();
	dc = ReadDspCounts();
	list = TakeSeen();

	Check(stop.lifetime == Lifetime::Parked && stop.cause == StopCause::Breakpoint && stop.breakpointId == 7 && stop.stopId == bp_hit.stopId + 1,
		"the breakpoint on step %u stops: \"%s\", id %u, the stop counter advanced by %u", kSite, StopCauseName(stop.cause), stop.breakpointId,
		stop.stopId - bp_hit.stopId);
	Check(DspStep() == kSite && Suspended() == SOUND_BURST_DSP, "the burst is suspended in front of the step: the DSP stands at step %u, burst %u", DspStep(),
		Suspended());
	Check(c.n[0] == bp_hit.c.n[0] + 1 && c.n[3] == bp_hit.c.n[3] && Sample() == 0 && c.n[1] == bp_hit.c.n[1] + 1,
		"in the first sample of the next burst: %u bursts started since, its tail has not run (%u), sample %u, its ARM phase ran (%u)", c.n[0] - bp_hit.c.n[0],
		c.n[3] - bp_hit.c.n[3], Sample(), c.n[1] - bp_hit.c.n[1]);
	Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Breakpoint } }), "published: %s", SeenText(list).c_str());
	Check(g_stop_latched && g_parked.load() && !Ctx().CpuRunning, "the stop is latched and the cpu stopped");
	Check(hand.suspended == 1 && hand.suspendedDsp == 1 && hand.latched == 0,
		"no emulated time passed behind the suspension: %u slices saw the burst suspended in front of a DSP step, %u ran under the latch", hand.suspendedDsp,
		hand.latched);
	Check(dsp::test::checks - bp_hit.checks == kSite + 1 && dc.n[0] == bp_hit.dc.n[0] && dc.n[1] == bp_hit.dc.n[1],
		"the debug loop made a check in front of each step up to the breakpoint (%u), and the phase has not ended", dsp::test::checks - bp_hit.checks);

	// the resume executes the step of the stop, and the breakpoint stops in front of it in the next sample
	bp_hit.c = c;
	bp_hit.dc = dc;
	bp_hit.stopId = stop.stopId;
	bp_hit.time = sh4_sched_now64();
	bp_hit.checks = dsp::test::checks;
	bp_hit.mdec = Mdec();

	if (!Resume())
		break;
	CO_YIELD(bp_hit.co);

	c = ReadCounts();
	dc = ReadDspCounts();
	list = TakeSeen();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 7 && DspStep() == kSite && Mdec() == ((bp_hit.mdec - 1) & 0xFFFF) && Sample() == 1,
		"a resume from the breakpoint runs one sample and stops in front of the step in the next: \"%s\", the DSP at step %u in sample %u, MDEC_CT %04X (it was "
		"%04X)", StopCauseName(stop.cause), DspStep(), Sample(), Mdec(), bp_hit.mdec);
	Check(c.n[0] == bp_hit.c.n[0] && sh4_sched_now64() == bp_hit.time && stop.stopId == bp_hit.stopId + 1 && c.n[2] == bp_hit.c.n[2] + 1
		&& dc.n[0] == bp_hit.dc.n[0] + 1 && dc.n[1] == bp_hit.dc.n[1] + 1,
		"in the same burst, with no emulated time between the two stops; one time step and one DSP phase, by the debug loop, ended; the stop counter advanced by %u",
		stop.stopId - bp_hit.stopId);
	Check(dsp::test::checks - bp_hit.checks == kSteps + 1, "a check in front of every step from the stop to the stop: %u", dsp::test::checks - bp_hit.checks);
	Check(SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } }), "the resume published no Running, the burst was run on in the park context: %s",
		SeenText(list).c_str());
	Check(stop.notice == 0, "a stop of the DSP has no notice (%u)", stop.notice);

	DspBps({ });

	CO_END(bp_hit.co);
}

/*
	dsp.bp.lowestid: of several breakpoints on one step the lowest id is published
*/
namespace {

struct {
	Co co;
} bp_lowest;

}

BOUNDED_GUEST_TEST(dsp_bp_lowestid, "dsp.bp.lowestid")
{
	CO_BEGIN(bp_lowest.co);

	while (!Settle(stop))
		CO_YIELD(bp_lowest.co);

	// three on the step, and one on a later step
	DspBps({ { 9, kSite }, { 4, kSite }, { 6, kSite }, { 2, kSite + 40 } });

	if (!Resume())
		break;
	CO_YIELD(bp_lowest.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 4 && DspStep() == kSite,
		"breakpoints 9, 4 and 6 are on step %u: the stop has id %u (\"%s\", the DSP at step %u)", kSite, stop.breakpointId, StopCauseName(stop.cause), DspStep());

	// and the one on the later step, behind it in the sample
	if (!Resume())
		break;
	CO_YIELD(bp_lowest.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 2 && DspStep() == kSite + 40 && Sample() == 0,
		"the resume stops at the one on step %u of the same sample: id %u at step %u", kSite + 40, stop.breakpointId, DspStep());

	DspBps({ });

	CO_END(bp_lowest.co);
}

/*
	dsp.bp.mask: a listed breakpoint whose category is not active stops nothing and arms nothing
*/
namespace {

struct {
	Co co;
	u32 checks;
	Counts c;
	DspCounts dc;
} bp_mask;

}

BOUNDED_GUEST_TEST(dsp_bp_mask, "dsp.bp.mask")
{
	Counts c;
	DspCounts dc;

	CO_BEGIN(bp_mask.co);

	while (!Settle(stop))
		CO_YIELD(bp_mask.co);

	emu::SetActiveMask(kMaskAll & ~CategoryBit(Category::Dsp));
	DspBps({ { 3, kSite } });

	Check(dbg_dsp_armed == 0, "a breakpoint is listed on step %u and its category is not active: dbg_dsp_armed is %u", kSite, (u32)dbg_dsp_armed);

	bp_mask.checks = dsp::test::checks;
	bp_mask.c = ReadCounts();
	bp_mask.dc = ReadDspCounts();

	while (!PauseAfter(stop, 4))
		CO_YIELD(bp_mask.co);

	c = ReadCounts();
	dc = ReadDspCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - bp_mask.c.n[0] >= 4 && dc.n[0] - bp_mask.dc.n[0] == 32 * (c.n[0] - bp_mask.c.n[0]),
		"%u bursts ran through it, the next stop is the test's own: \"%s\"", c.n[0] - bp_mask.c.n[0], StopCauseName(stop.cause));
	Check(dsp::test::checks == bp_mask.checks && dc.n[1] == bp_mask.dc.n[1], "and no check was made: no DSP phase went through the debug loop (%u checks)",
		dsp::test::checks - bp_mask.checks);

	// the category becomes active
	emu::SetActiveMask(kMaskAll);
	Check(dbg_dsp_armed != 0, "with the category active the breakpoint arms the debug loop: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);

	bp_mask.c = ReadCounts();

	if (!Resume())
		break;
	CO_YIELD(bp_mask.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 3 && DspStep() == kSite && c.n[0] == bp_mask.c.n[0] + 1,
		"and stops in the next burst: \"%s\", id %u", StopCauseName(stop.cause), stop.breakpointId);

	// muted at a stop inside a burst: nothing is armed any more
	emu::SetActiveMask(kMaskAll & ~CategoryBit(Category::Dsp));
	Check(dbg_dsp_armed == 0, "the category muted at the stop: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);
	emu::SetActiveMask(0);
	Check(dbg_dsp_armed == 0, "the mask cleared: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);
	emu::SetActiveMask(kMaskAll);

	DspBps({ });
	Check(dbg_dsp_armed == 0, "no breakpoint listed: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);

	CO_END(bp_mask.co);
}

/*
	dsp.bp.resume: the resume exemption. It lets the step of a stop execute once, and it is there only for a stop
	the DSP decided, with the DSP in front of the step of that stop and a check to come; the first check that is
	made ends it, and so does a change of dbg_dsp_armed to or from 0; a burst that finds the ARM7 held does not
*/
namespace {

struct {
	Co co;
	u32 mdec, step, round;
	Counts c;
	std::atomic<bool> calling { false };
	std::atomic<int> rv { 0 };
	std::atomic<bool> onEmu { false };

	// what the call of the fourth block found: the exemption armed by the leave in front of the change of
	// dbg_dsp_armed, and the flag between its two changes and behind them, with what was left of the exemption
	std::atomic<bool> exemptBefore { false };
	std::atomic<u32> armedBetween { 0 };
	std::atomic<u32> armedAfter { 0 };
	std::atomic<bool> exemptAfter { false };
} bp_res;

}

BOUNDED_GUEST_TEST(dsp_bp_resume, "dsp.bp.resume")
{
	Counts c;

	CO_BEGIN(bp_res.co);

	while (!Settle(stop))
		CO_YIELD(bp_res.co);

	/*
		Once: the step of the stop executes, and the breakpoint on the next step stops in the same sample
	*/
	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(bp_res.co);

	bp_res.mdec = Mdec();
	DspBps({ { kAtId, kSite }, { 11, kSite + 1 } });

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 11 && DspStep() == kSite + 1 && Mdec() == bp_res.mdec && Sample() == 0,
		"a resume from a breakpoint executes its step, and the breakpoint on the next one stops: id %u at step %u, in the same sample (MDEC_CT %04X, sample %u)",
		stop.breakpointId, DspStep(), Mdec(), Sample());

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == kSite && Mdec() == ((bp_res.mdec - 1) & 0xFFFF) && Sample() == 1,
		"and once only: the breakpoint on step %u stops in the next sample (id %u at step %u, MDEC_CT %04X, sample %u)", kSite, stop.breakpointId, DspStep(),
		Mdec(), Sample());

	/*
		A stop the DSP did not decide: behind a burst, the DSP in front of step 0, with a breakpoint listed on step 0.
		The next burst stops in front of step 0 of its first sample, with nothing of it executed
	*/
	DspBps({ });

	while (!BehindBurst(stop))
		CO_YIELD(bp_res.co);

	DspBps({ { 12, 0 } });
	bp_res.mdec = Mdec();
	bp_res.c = ReadCounts();

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 12 && DspStep() == 0 && Sample() == 0 && Mdec() == bp_res.mdec && c.n[0] == bp_res.c.n[0] + 1,
		"from a stop behind a burst, which the DSP did not decide, a breakpoint on step 0 stops in front of step 0 of the next burst with nothing of it executed: "
		"id %u at step %u, sample %u, MDEC_CT %04X (it was %04X)", stop.breakpointId, DspStep(), Sample(), Mdec(), bp_res.mdec);

	/*
		A breakpoint added behind the leave and in front of the next burst. The stop is that of a step of step 127
		in the last sample of a burst: the DSP decided it, nothing is listed, no burst is suspended, and the DSP
		stands in front of step 0. A thread queues the call that lists a breakpoint on step 0, and the resume is
		armed while that call waits: the park loop leaves first, the call is served at the first slice end, and the
		next burst stops in front of step 0 with nothing of it executed. dbg_dsp_armed is 0 at the leave, so the
		resume arms no exemption (no check would be made): this block holds that rule together with the end of an
		exemption at the change of the flag from 0, which would end one that had been armed. The next block arms
		one at the leave, and ends it by a change of the flag alone
	*/
	DspBps({ });

	while (!SuspendAtStep(stop, kLast))
		CO_YIELD(bp_res.co);

	while (!ToSample(stop, 31))
		CO_YIELD(bp_res.co);

	DspBps({ });

	if (!StepDsp())
		break;
	CO_YIELD(bp_res.co);

	if (stop.cause != StopCause::Step || Suspended() != SOUND_BURST_NONE || DspStep() != 0 || dbg_dsp_armed != 0 || !dsp::test::GetState().stopByCheck)
	{
		Check(false, "a step of step 127 in the last sample of a burst: the stop is the DSP's, at step 0, behind the burst (\"%s\", burst %u, step %u, dbg_dsp_armed "
			"%u)", StopCauseName(stop.cause), Suspended(), DspStep(), (u32)dbg_dsp_armed);
		break;
	}

	NoteBetweenBursts();
	bp_res.mdec = Mdec();
	bp_res.c = ReadCounts();
	bp_res.calling = false;
	bp_res.onEmu = false;

	OtherStart([] {
		bp_res.calling = true;
		bp_res.rv = emu::Call(emu::CC_TABLE, [] {
			bp_res.onEmu = emu::OnEmuThread() && emu::GetRunState().lifetime == Lifetime::Running;
			DspBps({ { 13, 0 } });
		});
	});

	// until the call is in the queue
	for (Clock::time_point t0 = Clock::now(); !(g_attn.load() & ATTN_CALL) && MsSince(t0) < kHoldMs; )
		std::this_thread::yield();

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	OtherJoin();
	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 13 && bp_res.rv == 0 && bp_res.onEmu && c.n[0] == bp_res.c.n[0] + 1 && DspStep() == 0
		&& Sample() == 0 && Mdec() == bp_res.mdec,
		"a breakpoint listed behind the leave, by a call that waited and was served while the machine ran, stops in front of step 0 of the next burst, which "
		"has not executed: \"%s\" id %u, the call returned %d, %u bursts, sample %u, MDEC_CT %04X (it was %04X)", StopCauseName(stop.cause), stop.breakpointId,
		bp_res.rv.load(), c.n[0] - bp_res.c.n[0], Sample(), Mdec(), bp_res.mdec);

	/*
		An exemption armed at the leave ends at a change of dbg_dsp_armed to 0 and back, in two rounds. The stop is
		again that of a step of step 127 in the last sample of a burst, and a breakpoint on step 0 is listed at the
		stop: dbg_dsp_armed is not 0, and the resume arms the exemption for step 0. A thread queues one call, which
		is served at the first slice end while the SH4 runs to the next burst: in the first round it removes the
		breakpoint and lists it again, in the second it mutes the category of the DSP and makes it active again;
		either takes dbg_dsp_armed to 0 and back, as a client's two requests would. The exemption ends there, and
		the breakpoint stops in front of step 0 of the next burst with nothing of it executed. An exemption that
		outlived the change would run through step 0 and stop in front of it one sample later, MDEC_CT one less
	*/
	for (bp_res.round = 0; bp_res.round < 2; bp_res.round++)
	{
		DspBps({ });

		while (!SuspendAtStep(stop, kLast))
			CO_YIELD(bp_res.co);

		while (!ToSample(stop, 31))
			CO_YIELD(bp_res.co);

		DspBps({ });

		if (!StepDsp())
			break;
		CO_YIELD(bp_res.co);

		if (stop.cause != StopCause::Step || Suspended() != SOUND_BURST_NONE || DspStep() != 0 || !dsp::test::GetState().stopByCheck)
		{
			Check(false, "a step of step 127 in the last sample of a burst, for the change of dbg_dsp_armed: the stop is the DSP's, at step 0, behind the burst "
				"(\"%s\", burst %u, step %u)", StopCauseName(stop.cause), Suspended(), DspStep());
			break;
		}

		NoteBetweenBursts();
		DspBps({ { 16, 0 } });

		bp_res.mdec = Mdec();
		bp_res.c = ReadCounts();
		bp_res.rv = -1;
		bp_res.onEmu = false;
		bp_res.exemptBefore = false;
		bp_res.armedBetween = ~0u;
		bp_res.armedAfter = 0;
		bp_res.exemptAfter = true;

		OtherStart([] {
			bp_res.rv = emu::Call(emu::CC_TABLE, [] {
				const dsp::test::State s = dsp::test::GetState();

				bp_res.onEmu = emu::OnEmuThread() && emu::GetRunState().lifetime == Lifetime::Running;
				bp_res.exemptBefore = s.exempt && s.exemptStep == 0;

				if (bp_res.round == 0)
				{
					DspBps({ });
					bp_res.armedBetween = dbg_dsp_armed;
					DspBps({ { 16, 0 } });
				}
				else
				{
					emu::SetActiveMask(kMaskAll & ~CategoryBit(Category::Dsp));
					bp_res.armedBetween = dbg_dsp_armed;
					emu::SetActiveMask(kMaskAll);
				}

				bp_res.armedAfter = dbg_dsp_armed;
				bp_res.exemptAfter = dsp::test::GetState().exempt;
			});
		});

		// until the call is in the queue
		for (Clock::time_point t0 = Clock::now(); !(g_attn.load() & ATTN_CALL) && MsSince(t0) < kHoldMs; )
			std::this_thread::yield();

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		OtherJoin();
		c = ReadCounts();

		Check(bp_res.rv == 0 && bp_res.onEmu && bp_res.exemptBefore && bp_res.armedBetween == 0 && bp_res.armedAfter != 0 && !bp_res.exemptAfter,
			"%s: the call was served on the emulator thread while the machine ran, with the exemption for step 0 that the leave armed; it took dbg_dsp_armed to "
			"0 and back, and that ended the exemption (the call returned %d, the exemption %s in front of it and %s behind it, dbg_dsp_armed %u between the "
			"changes and %u behind them)", bp_res.round ? "the category muted and active again" : "the breakpoint removed and listed again", bp_res.rv.load(),
			bp_res.exemptBefore ? "armed" : "not armed", bp_res.exemptAfter ? "still armed" : "ended", bp_res.armedBetween.load(), bp_res.armedAfter.load());
		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 16 && DspStep() == 0 && Sample() == 0 && Mdec() == bp_res.mdec
			&& c.n[0] == bp_res.c.n[0] + 1,
			"%s: the breakpoint on step 0 stops in front of it in the next burst, which has not executed it: \"%s\" id %u at step %u, %u bursts, sample %u, "
			"MDEC_CT %04X (it was %04X)", bp_res.round ? "the category muted and active again" : "the breakpoint removed and listed again",
			StopCauseName(stop.cause), stop.breakpointId, DspStep(), c.n[0] - bp_res.c.n[0], Sample(), Mdec(), bp_res.mdec);
	}

	// a round that could not go on has failed a check
	if (bp_res.round < 2)
		break;

	/*
		A burst that finds the ARM7 held does not end the exemption: the DSP is clocked whatever ARMRST says. The
		stop is again that of a step of step 127 in the last sample of a burst, and a breakpoint on step 0 is listed
		at the stop: the resume arms the exemption for step 0, which the first check of the next burst uses. The
		guest is sent to hold its ARM7 in front of that burst, and a breakpoint of the ARM7 is listed, so that the
		burst that finds the ARM7 held has the ARM7's run control look at it (dbg_arm7_OnBurstUnclocked). The resume
		runs through step 0 in the first sample of that burst and stops in front of it in the second
	*/
	DspBps({ });

	while (!SuspendAtStep(stop, kLast))
		CO_YIELD(bp_res.co);

	while (!ToSample(stop, 31))
		CO_YIELD(bp_res.co);

	DspBps({ });

	if (!StepDsp())
		break;
	CO_YIELD(bp_res.co);

	if (stop.cause != StopCause::Step || Suspended() != SOUND_BURST_NONE || DspStep() != 0)
	{
		Check(false, "a step of step 127 in the last sample of a burst, again (\"%s\", burst %u, step %u)", StopCauseName(stop.cause), Suspended(), DspStep());
		break;
	}

	NoteBetweenBursts();
	DspBps({ { 15, 0 } });
	ArmBps({ { 14, kNever } });
	GotoSh4(DSPTEST_G_HOLD_ARM7);

	bp_res.mdec = Mdec();
	bp_res.c = ReadCounts();

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 15 && DspStep() == 0 && !ArmClocked() && c.n[0] == bp_res.c.n[0] + 1
		&& c.n[1] == bp_res.c.n[1] && Sample() == 1 && Mdec() == ((bp_res.mdec - 1) & 0xFFFF),
		"from the stop of that step, with a breakpoint on step 0 listed there, a resume into a burst that finds the ARM7 held runs through step 0 once and stops "
		"in front of it one sample later: \"%s\" id %u at step %u in sample %u, the ARM7 %s, %u bursts, %u ARM phases, MDEC_CT %04X (it was %04X)",
		StopCauseName(stop.cause), stop.breakpointId, DspStep(), Sample(), ArmClocked() ? "clocked" : "held", c.n[0] - bp_res.c.n[0], c.n[1] - bp_res.c.n[1],
		Mdec(), bp_res.mdec);

	ArmBps({ });
	DspBps({ });

	CO_END(bp_res.co);
}

/*
	dsp.bp.everysample: a breakpoint of the DSP is reached in every sample, and a resume from it stops in front of
	the same step one sample later: in the same burst, with no emulated time and no Running, when the sample was not
	the last of its burst, and in the next burst when it was
*/
namespace {

struct {
	Co co;
	u32 n, good, within, across;
	u32 mdec, sample, stopId, checks;
	u64 time;
	Counts c;
} bp_every;

}

BOUNDED_GUEST_TEST(dsp_bp_everysample, "dsp.bp.everysample")
{
	Counts c;
	std::vector<Seen> list;
	bool ok;

	CO_BEGIN(bp_every.co);

	while (!Settle(stop))
		CO_YIELD(bp_every.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(bp_every.co);

	bp_every.good = bp_every.within = bp_every.across = 0;

	for (bp_every.n = 0; bp_every.n < 70; bp_every.n++)
	{
		bp_every.mdec = Mdec();
		bp_every.sample = Sample();
		bp_every.stopId = stop.stopId;
		bp_every.checks = dsp::test::checks;
		bp_every.time = sh4_sched_now64();
		bp_every.c = ReadCounts();
		TakeSeen();

		if (!Resume())
			break;
		CO_YIELD(bp_every.co);

		c = ReadCounts();
		list = TakeSeen();

		ok = stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == kSite && Suspended() == SOUND_BURST_DSP
			&& Mdec() == ((bp_every.mdec - 1) & 0xFFFF) && Sample() == (bp_every.sample + 1) % 32 && stop.stopId == bp_every.stopId + 1;

		if (bp_every.sample != 31)
		{
			bp_every.within++;
			ok = ok && c.n[0] == bp_every.c.n[0] && sh4_sched_now64() == bp_every.time && SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } })
				&& dsp::test::checks - bp_every.checks == kSteps + 1;
		}
		else
		{
			bp_every.across++;
			ok = ok && c.n[0] == bp_every.c.n[0] + 1 && c.n[3] == bp_every.c.n[3] + 1 && sh4_sched_now64() > bp_every.time
				&& sh4_sched_now64() - bp_every.time <= kAicaPeriod + SH4_TIMESLICE
				&& SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Breakpoint } });
		}

		if (ok)
			bp_every.good++;
	}

	Check(bp_every.good == bp_every.n && bp_every.n == 70 && bp_every.across >= 2,
		"%u resumes from the breakpoint on step %u, each a stop in front of it one sample later with MDEC_CT one less (%u good): %u within a burst, each with no "
		"emulated time, no Running and a check in front of every step in between; %u from the last sample of a burst, each in the next burst, one period of the "
		"AICA at most later", bp_every.n, kSite, bp_every.good, bp_every.within, bp_every.across);

	DspBps({ });

	CO_END(bp_every.co);
}

/*
	dsp.bp.held: with the guest's ARM7 held across the bursts (g_hold_arm7) the DSP is clocked as ever: a breakpoint
	of it is reached in every sample, a resume from it stops in front of the same step one sample later with MDEC_CT
	one less, and nothing of the ARM7 executes - its registers as the debugger reads them, and every word of its
	stored context but INTR_PEND, which the interrupt update of a held sample sets when the timer's request latches
	(ARM7Backend::UpdateInterrupts)
*/
namespace {

struct {
	Co co;
	u32 n, good, across, mdec, sample;
	ArmRegs regs;
	std::vector<u8> state;
	Counts c, held;
} bp_held;

}

BOUNDED_GUEST_TEST(dsp_bp_held, "dsp.bp.held")
{
	Counts c;
	bool ok;

	CO_BEGIN(bp_held.co);

	while (!Settle(stop))
		CO_YIELD(bp_held.co);

	while (!HoldArm7(stop))
		CO_YIELD(bp_held.co);

	// the hold, in front of the held bursts
	bp_held.regs = ReadArm();
	bp_held.state = ScpuState();
	bp_held.held = ReadCounts();
	NoteBetweenBursts();

	DspBps({ { kAtId, kSite } });

	// the stop the test asks for is in the next burst; a pause two bursts on is there in case none comes
	HandOn(HAND_PAUSE, ReadCounts().n[0] + 2);

	if (!Resume())
		break;
	CO_YIELD(bp_held.co);

	HandOff();

	if (!Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == kSite && Suspended() == SOUND_BURST_DSP && !ArmClocked()
		&& ReadCounts().n[0] == bp_held.held.n[0] + 1 && Sample() == 0,
		"in the first burst that finds the ARM7 held, the breakpoint on step %u stops in its first sample: \"%s\" id %u, the DSP at step %u, burst %u, the ARM7 %s",
		kSite, StopCauseName(stop.cause), stop.breakpointId, DspStep(), Suspended(), ArmClocked() ? "clocked" : "held"))
	{
		DspBps({ });
		break;
	}

	bp_held.good = bp_held.across = 0;

	for (bp_held.n = 0; bp_held.n < 40; bp_held.n++)
	{
		bp_held.mdec = Mdec();
		bp_held.sample = Sample();
		bp_held.c = ReadCounts();

		HandOn(HAND_PAUSE, ReadCounts().n[0] + 2);

		if (!Resume())
			break;
		CO_YIELD(bp_held.co);

		HandOff();
		c = ReadCounts();

		ok = stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == kSite && Mdec() == ((bp_held.mdec - 1) & 0xFFFF)
			&& Sample() == (bp_held.sample + 1) % 32 && c.n[1] == bp_held.held.n[1] && !ArmClocked();

		if (bp_held.sample == 31)
		{
			bp_held.across++;
			ok = ok && c.n[0] == bp_held.c.n[0] + 1;
		}

		if (ok)
			bp_held.good++;
		else
			break;
	}

	Check(bp_held.good == bp_held.n && bp_held.n == 40 && bp_held.across >= 1,
		"%u resumes with the ARM7 held, each a stop in front of step %u one sample later with MDEC_CT one less and no ARM phase (%u good; %u into the next burst)",
		bp_held.n, kSite, bp_held.good, bp_held.across);

	{
		const ArmRegs regs = ReadArm();
		const std::vector<u8> state = ScpuState();
		const u32 differs = ArmDiffers(regs, bp_held.regs);
		u32 first;
		const u32 words = StoredDiffer(state, bp_held.state, &first);

		Check(differs == kArmRegs, "nothing of the ARM7 executed: every register of it reads as at the hold (the first that differs: %s; the PC %08X, at the hold %08X)",
			differs == kArmRegs ? "none" : std::to_string(differs).c_str(), ArmPc(), bp_held.regs.r[15]);
		Check(state.size() == kScpuStateSize && words == 0, "every word of its stored context but INTR_PEND is as at the hold (%u differ; the first: %s)", words,
			first == RN_ARM_REG_COUNT ? "none" : std::to_string(first).c_str());
	}

	DspBps({ });

	CO_END(bp_held.co);
}

/*
	dsp.step.suspended: a step of the DSP with a burst suspended in front of a DSP step is made in the park
	context: the step executes, the check behind it stops
*/
namespace {

struct {
	Co co;
	u32 stopId, sh4pc, checks, n, good, step;
	u64 time;
	Counts c;
	DspCounts dc;
} st_susp;

}

BOUNDED_GUEST_TEST(dsp_step_suspended, "dsp.step.suspended")
{
	std::vector<Seen> list;
	Counts c;
	DspCounts dc;

	CO_BEGIN(st_susp.co);

	while (!Settle(stop))
		CO_YIELD(st_susp.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_susp.co);

	DspBps({ });
	Check(dbg_dsp_armed == 0, "nothing of the DSP is armed at the stop: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);

	st_susp.stopId = stop.stopId;
	st_susp.sh4pc = Ctx().pc;
	st_susp.time = sh4_sched_now64();
	st_susp.checks = dsp::test::checks;
	st_susp.c = ReadCounts();
	st_susp.dc = ReadDspCounts();
	TakeSeen();

	if (!StepDsp())
		break;

	Check(emu::Step(Target::Dsp, StepKind::Into) == kErrNotPaused && emu::Resume() == kErrNotPaused, "with the step armed, a second action is refused");
	CO_YIELD(st_susp.co);

	list = TakeSeen();
	c = ReadCounts();
	dc = ReadDspCounts();

	Check(stop.lifetime == Lifetime::Parked && stop.cause == StopCause::Step && stop.breakpointId == 0 && DspStep() == kSite + 1,
		"a step of the DSP from step %u: \"%s\", the DSP in front of step %u", kSite, StopCauseName(stop.cause), DspStep());
	Check(stop.stopId == st_susp.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }), "a new stop counter (+%u) and no Running: published %s",
		stop.stopId - st_susp.stopId, SeenText(list).c_str());
	Check(sh4_sched_now64() == st_susp.time && Ctx().pc == st_susp.sh4pc && g_stop_latched && g_parked.load(),
		"the scheduler's time and the PC of the SH4 are unchanged, the latch is set");
	Check(Suspended() == SOUND_BURST_DSP && c.n[0] == st_susp.c.n[0] && c.n[1] == st_susp.c.n[1] && c.n[2] == st_susp.c.n[2] && dc.n[0] == st_susp.dc.n[0],
		"the burst is suspended again, in the same DSP phase");
	Check(dsp::test::checks == st_susp.checks + 2, "two checks were made: the one in front of the step, which let it execute, and the one behind it (%u)",
		dsp::test::checks - st_susp.checks);
	Check(dbg_dsp_armed == 0 && !dsp::test::GetState().step, "the step is armed no more");

	// many of them, through the sample: each ends in front of the step behind it
	st_susp.good = 0;

	for (st_susp.n = 0; st_susp.n < 100; st_susp.n++)
	{
		st_susp.step = DspStep();
		st_susp.stopId = stop.stopId;

		if (!StepDsp())
			break;
		CO_YIELD(st_susp.co);

		if (stop.cause == StopCause::Step && stop.stopId == st_susp.stopId + 1 && sh4_sched_now64() == st_susp.time && Ctx().pc == st_susp.sh4pc
			&& DspStep() == st_susp.step + 1 && Suspended() == SOUND_BURST_DSP)
			st_susp.good++;
	}

	list = TakeSeen();

	Check(st_susp.good == st_susp.n && st_susp.n == 100 && !SeenHas(list, Lifetime::Running, StopCause::NoCause),
		"%u more steps, each a stop with \"step\", a new stop counter and the next step, with no time passed, the SH4 where it was, and no Running (%u)",
		st_susp.n, st_susp.good);

	CO_END(st_susp.co);
}

/*
	dsp.step.wrap: a step of step 127 is complete at the end of its sample: the sample's mix and interrupt update
	run, and the burst is suspended there, in front of the ARM phase of the next sample, nothing of which has run
*/
namespace {

struct {
	Co co;
	u32 stopId, mdec, timb, sample;
	u64 time;
	Counts c;
	DspCounts dc;
} st_wrap;

}

BOUNDED_GUEST_TEST(dsp_step_wrap, "dsp.step.wrap")
{
	std::vector<Seen> list;
	Counts c;
	DspCounts dc;

	CO_BEGIN(st_wrap.co);

	while (!Settle(stop))
		CO_YIELD(st_wrap.co);

	while (!SuspendAtStep(stop, kLast))
		CO_YIELD(st_wrap.co);

	DspBps({ });

	st_wrap.stopId = stop.stopId;
	st_wrap.mdec = Mdec();
	st_wrap.timb = AicaReg16(kTIMB) & 0xFF;
	st_wrap.sample = Sample();
	st_wrap.time = sh4_sched_now64();
	st_wrap.c = ReadCounts();
	st_wrap.dc = ReadDspCounts();
	TakeSeen();

	if (!StepDsp())
		break;
	CO_YIELD(st_wrap.co);

	list = TakeSeen();
	c = ReadCounts();
	dc = ReadDspCounts();

	Check(stop.cause == StopCause::Step && stop.stopId == st_wrap.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }) && DspStep() == 0,
		"a step of step 127: one stop with \"%s\", a new stop counter, no Running (%s); the DSP in front of step 0", StopCauseName(stop.cause),
		SeenText(list).c_str());
	Check(Suspended() == SOUND_BURST_SAMPLE && Sample() == st_wrap.sample + 1 && Mdec() == ((st_wrap.mdec - 1) & 0xFFFF),
		"the step is complete at the end of its sample: the burst is suspended there (%u), in front of sample %u, MDEC_CT %04X (it was %04X)", Suspended(),
		Sample(), Mdec(), st_wrap.mdec);
	Check(c.n[2] == st_wrap.c.n[2] + 1 && dc.n[0] == st_wrap.dc.n[0] + 1 && dc.n[1] == st_wrap.dc.n[1] + 1,
		"the DSP phase of the sample ended, and its mix and interrupt update ran: the time step is counted (%u), the phase too, by the debug loop (%u, %u)",
		c.n[2] - st_wrap.c.n[2], dc.n[0] - st_wrap.dc.n[0], dc.n[1] - st_wrap.dc.n[1]);
	Check(c.n[1] == st_wrap.c.n[1] && (AicaReg16(kTIMB) & 0xFF) == st_wrap.timb && c.n[0] == st_wrap.c.n[0] && sh4_sched_now64() == st_wrap.time,
		"nothing of the next sample ran: no ARM phase (%u), the timers did not count (TIMB %02X, it was %02X); in the same burst, no emulated time",
		c.n[1] - st_wrap.c.n[1], AicaReg16(kTIMB) & 0xFF, st_wrap.timb);
	Check(dbg_dsp_armed == 0 && !dsp::test::GetState().step && dsp::test::GetState().stopByCheck, "the stop is the DSP's, and nothing is armed");

	CO_END(st_wrap.co);
}

/*
	dsp.step.burstend: a step of step 127 in the last sample of its burst, the burst suspended in that sample, is
	complete at the end of the burst: the tail ran once, nothing is suspended, and the stop of the step is published
	with a new stop counter and no Running
*/
namespace {

struct {
	Co co;
	Counts c;
	u32 stopId, sh4pc;
	u64 time;
} st_end;

}

BOUNDED_GUEST_TEST(dsp_step_burstend, "dsp.step.burstend")
{
	Counts c;
	std::vector<Seen> list;

	CO_BEGIN(st_end.co);

	while (!Settle(stop))
		CO_YIELD(st_end.co);

	while (!SuspendAtStep(stop, kLast))
		CO_YIELD(st_end.co);

	while (!ToSample(stop, 31))
		CO_YIELD(st_end.co);

	DspBps({ });

	st_end.c = ReadCounts();
	st_end.stopId = stop.stopId;
	st_end.time = sh4_sched_now64();
	st_end.sh4pc = Ctx().pc;
	TakeSeen();

	if (!StepDsp())
		break;
	CO_YIELD(st_end.co);

	c = ReadCounts();
	list = TakeSeen();

	Check(stop.cause == StopCause::Step && stop.stopId == st_end.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
		"a step of step 127 in the last sample of a burst: one stop with \"%s\", a new stop counter, no Running (%s)", StopCauseName(stop.cause),
		SeenText(list).c_str());
	Check(Suspended() == SOUND_BURST_NONE && DspStep() == 0 && c.n[3] == st_end.c.n[3] + 1 && c.n[2] == st_end.c.n[2] + 1 && c.n[0] == st_end.c.n[0],
		"behind it no burst is suspended: the sample ended, and the tail ran once (%u, %u); the DSP in front of step 0", c.n[2] - st_end.c.n[2],
		c.n[3] - st_end.c.n[3]);
	Check(sh4_sched_now64() == st_end.time && Ctx().pc == st_end.sh4pc && g_stop_latched,
		"all of it in the park context: the scheduler's time and the SH4 are where they were");
	Check(dbg_dsp_armed == 0 && dsp::test::GetState().stopByCheck, "the stop is the DSP's, and nothing is armed");

	// the machine goes on from there: the next callback finds no burst suspended
	NoteBetweenBursts();
	st_end.c = c;

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_end.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - st_end.c.n[0] >= 3 && c.n[2] - st_end.c.n[2] == 32 * (c.n[0] - st_end.c.n[0])
		&& c.n[3] - st_end.c.n[3] == c.n[0] - st_end.c.n[0], "a resume from there runs whole bursts again: %u, each with 32 time steps and its tail",
		c.n[0] - st_end.c.n[0]);

	CO_END(st_end.co);
}

/*
	dsp.step.frompause: a step of the DSP asked for with no burst suspended. The park loop leaves without
	publishing, the SH4 runs to the next burst as a step that is executing, and the step is step 0 of the first
	sample of that burst
*/
namespace {

struct {
	Co co;
	u32 n, good, quiet;
	u32 stopId;
	u64 time;
	Counts c;
} st_pause;

}

BOUNDED_GUEST_TEST(dsp_step_frompause, "dsp.step.frompause")
{
	std::vector<Seen> list;
	Counts c;
	u64 passed;

	CO_BEGIN(st_pause.co);

	while (!Settle(stop))
		CO_YIELD(st_pause.co);

	st_pause.good = st_pause.quiet = 0;

	for (st_pause.n = 0; st_pause.n < 20; st_pause.n++)
	{
		// behind a burst, or at a pause; every other one at a pause
		if (st_pause.n & 1)
		{
			while (!PauseAfter(stop, 1))
				CO_YIELD(st_pause.co);
		}
		else
		{
			while (!BehindBurst(stop))
				CO_YIELD(st_pause.co);
		}

		st_pause.stopId = stop.stopId;
		st_pause.time = sh4_sched_now64();
		st_pause.c = ReadCounts();
		TakeSeen();
		HandOn(HAND_WATCH);

		if (!StepDsp())
			break;
		CO_YIELD(st_pause.co);

		HandOff();
		list = TakeSeen();
		c = ReadCounts();
		passed = sh4_sched_now64() - st_pause.time;

		// one period of the AICA callback at most, and the rest of a slice
		if (stop.cause == StopCause::Step && stop.stopId == st_pause.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } })
			&& c.n[0] == st_pause.c.n[0] + 1 && Suspended() == SOUND_BURST_DSP && DspStep() == 1 && Sample() == 0 && passed > 0
			&& passed <= kAicaPeriod + SH4_TIMESLICE)
			st_pause.good++;

		// the hand ran in every slice of the wait: the stop stayed in force for the host and for a client
		if (hand.slices >= 1 && hand.notParked == 0 && hand.notPublishedParked == 0 && hand.latched == 0)
			st_pause.quiet++;

		DspBps({ });
	}

	Check(st_pause.good == st_pause.n && st_pause.n == 20,
		"%u steps of the DSP from a stop with no burst suspended: each one stop with \"step\", a new stop counter, no Running, in the next burst, which is "
		"suspended in front of step 1 of its first sample, with the SH4 run for one period of the AICA callback at most (%u)", st_pause.n, st_pause.good);
	Check(st_pause.quiet == st_pause.n,
		"while the SH4 ran towards the burst, g_parked stayed set and the published state stayed Parked, in every slice of %u of the waits", st_pause.quiet);

	CO_END(st_pause.co);
}

/*
	dsp.step.fromarm7: a step of the DSP from a stop of the ARM7 runs the rest of that ARM phase, with the ARM7's
	exemption and its checks, to the DSP phase of the sample, whose step 0 is the step. With the breakpoint of the
	ARM7 still listed its loop comes back to it in the rest of the phase: that stop comes first, with its own cause,
	one pass of the loop on, and the step of the DSP ends
*/
namespace {

struct {
	Co co;
	u32 n, armFirst, dspFirst, bad, passes, sample, stopId;
	Counts c;
	u64 time;
} st_arm;

}

BOUNDED_GUEST_TEST(dsp_step_fromarm7, "dsp.step.fromarm7")
{
	Counts c;
	bool armStop, dspStop;

	CO_BEGIN(st_arm.co);

	while (!Settle(stop))
		CO_YIELD(st_arm.co);

	st_arm.armFirst = st_arm.dspFirst = st_arm.bad = 0;

	for (st_arm.n = 0; st_arm.n < 40 && st_arm.armFirst < 10; st_arm.n++)
	{
		while (!SuspendAtArm(stop, kMain))
			CO_YIELD(st_arm.co);

		// the loop counts its passes in R3
		st_arm.passes = Arm(3);
		st_arm.sample = Sample();
		st_arm.c = ReadCounts();
		st_arm.stopId = stop.stopId;
		st_arm.time = sh4_sched_now64();

		if (!StepDsp())
			break;
		CO_YIELD(st_arm.co);

		c = ReadCounts();

		armStop = stop.cause == StopCause::Breakpoint && stop.breakpointId == kArmAtId && ArmPc() == kMain && Arm(3) == st_arm.passes + 1
			&& Suspended() == SOUND_BURST_ARM7 && c.n[1] == st_arm.c.n[1] && !dsp::test::GetState().step && dbg_dsp_armed == 0;
		dspStop = stop.cause == StopCause::Step && DspStep() == 1 && Suspended() == SOUND_BURST_DSP && c.n[1] == st_arm.c.n[1] + 1 && Sample() == st_arm.sample;

		if (armStop)
			st_arm.armFirst++;
		else if (dspStop)
			st_arm.dspFirst++;
		else
			st_arm.bad++;

		if (stop.stopId != st_arm.stopId + 1 || sh4_sched_now64() != st_arm.time)
			st_arm.bad++;

		// the burst is suspended still: the next case comes from the next burst
		ArmBps({ });

		while (!PauseAfter(stop, 1))
			CO_YIELD(st_arm.co);
	}

	Check(st_arm.bad == 0 && st_arm.armFirst >= 10,
		"%u steps of the DSP from a stop at a breakpoint of the ARM7 that stays listed: %u stopped first at that breakpoint, one pass of the ARM7's loop on, "
		"with its own cause and the step of the DSP armed no more; %u got to the DSP phase first (%u of neither kind); each in the park context",
		st_arm.n, st_arm.armFirst, st_arm.dspFirst, st_arm.bad);

	// the breakpoint of the ARM7 removed at its stop: the rest of the ARM phase runs, and the step is step 0 of the
	// DSP phase of that sample
	while (!SuspendAtArm(stop, kMain))
		CO_YIELD(st_arm.co);

	ArmBps({ });

	st_arm.passes = Arm(3);
	st_arm.sample = Sample();
	st_arm.c = ReadCounts();
	st_arm.stopId = stop.stopId;
	st_arm.time = sh4_sched_now64();
	TakeSeen();

	if (!StepDsp())
		break;
	CO_YIELD(st_arm.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Step && stop.stopId == st_arm.stopId + 1 && DspStep() == 1 && Suspended() == SOUND_BURST_DSP && Sample() == st_arm.sample
		&& c.n[1] == st_arm.c.n[1] + 1 && c.n[0] == st_arm.c.n[0] && sh4_sched_now64() == st_arm.time,
		"a step of the DSP from a stop of the ARM7 with nothing listed: the rest of the ARM phase ran (%u), and the step is step 0 of the DSP phase of the same "
		"sample, %u: \"%s\", the DSP in front of step %u, burst %u", c.n[1] - st_arm.c.n[1], Sample(), StopCauseName(stop.cause), DspStep(), Suspended());
	{
		const std::vector<Seen> list = TakeSeen();

		Check(SeenIs(list, { { Lifetime::Parked, StopCause::Step } }), "and no Running was published: %s", SeenText(list).c_str());
	}

	CO_END(st_arm.co);
}

/*
	dsp.step.fromsampleend: a step of the DSP from the end of a sample, where a step of step 127 left the burst:
	the next sample's ARM phase, timers and sweep run, and the step is its step 0
*/
namespace {

struct {
	Co co;
	u32 stopId, sample, timb;
	u64 time;
	Counts c;
} st_se;

}

BOUNDED_GUEST_TEST(dsp_step_fromsampleend, "dsp.step.fromsampleend")
{
	Counts c;
	std::vector<Seen> list;

	CO_BEGIN(st_se.co);

	while (!Settle(stop))
		CO_YIELD(st_se.co);

	while (!SuspendAtStep(stop, kLast))
		CO_YIELD(st_se.co);

	DspBps({ });

	if (!StepDsp())
		break;
	CO_YIELD(st_se.co);

	if (!Check(stop.cause == StopCause::Step && Suspended() == SOUND_BURST_SAMPLE && DspStep() == 0, "a step of step 127 leaves the burst at the end of the sample"))
		break;

	st_se.stopId = stop.stopId;
	st_se.sample = Sample();
	st_se.timb = AicaReg16(kTIMB) & 0xFF;
	st_se.time = sh4_sched_now64();
	st_se.c = ReadCounts();
	TakeSeen();

	if (!StepDsp())
		break;
	CO_YIELD(st_se.co);

	c = ReadCounts();
	list = TakeSeen();

	Check(stop.cause == StopCause::Step && stop.stopId == st_se.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }) && DspStep() == 1
		&& Suspended() == SOUND_BURST_DSP && Sample() == st_se.sample,
		"a step from the end of the sample: \"%s\", the DSP in front of step 1 of sample %u (the burst stood in front of sample %u), burst %u, published %s",
		StopCauseName(stop.cause), Sample(), st_se.sample, Suspended(), SeenText(list).c_str());
	Check(c.n[1] == st_se.c.n[1] + 1 && (AicaReg16(kTIMB) & 0xFF) == ((st_se.timb + 1) & 0xFF) && c.n[0] == st_se.c.n[0] && sh4_sched_now64() == st_se.time,
		"the ARM phase of that sample ran (%u) and its timers counted (TIMB %02X, it was %02X), in the same burst, with no emulated time", c.n[1] - st_se.c.n[1],
		AicaReg16(kTIMB) & 0xFF, st_se.timb);

	CO_END(st_se.co);
}

/*
	dsp.step.held: the guest holds its ARM7 between the request of a step of the DSP and the burst the step waits
	for. The DSP is clocked all the same: the step is step 0 of that burst's first sample, nothing of the ARM7
	executes. Once with nothing of the ARM7 armed, once with a breakpoint of it listed, so that the burst that finds
	it held has the ARM7's run control look at it (dbg_arm7_OnBurstUnclocked). Then a step with the ARM7 held and
	the burst suspended
*/
namespace {

struct {
	Co co;
	u32 round, stopId;
	Counts c;
	ArmRegs regs;
	std::vector<u8> state;
} st_held;

}

BOUNDED_GUEST_TEST(dsp_step_held, "dsp.step.held")
{
	Counts c;
	std::vector<Seen> list;

	CO_BEGIN(st_held.co);

	for (st_held.round = 0; st_held.round < 2; st_held.round++)
	{
		while (!Settle(stop))
			CO_YIELD(st_held.co);

		if (st_held.round == 1)
			ArmBps({ { 30, kNever } });

		st_held.stopId = stop.stopId;
		st_held.c = ReadCounts();
		st_held.regs = ReadArm();
		st_held.state = ScpuState();
		TakeSeen();

		// the ARM7 is clocked when the step is asked for, and the SH4 holds it before the next burst
		GotoSh4(DSPTEST_G_HOLD_ARM7);

		if (!StepDsp())
			break;
		CO_YIELD(st_held.co);

		c = ReadCounts();
		list = TakeSeen();

		{
			u32 first;
			const u32 words = StoredDiffer(ScpuState(), st_held.state, &first);

			Check(stop.cause == StopCause::Step && stop.stopId == st_held.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
				"%s: the step of the DSP is complete in the burst that finds the ARM7 held: \"%s\", a new stop counter, published %s",
				st_held.round ? "a breakpoint of the ARM7 listed" : "nothing of the ARM7 armed", StopCauseName(stop.cause), SeenText(list).c_str());
			Check(!ArmClocked() && DspStep() == 1 && Suspended() == SOUND_BURST_DSP && Sample() == 0 && c.n[0] == st_held.c.n[0] + 1 && c.n[1] == st_held.c.n[1],
				"the step is step 0 of the first sample of that burst: the DSP in front of step %u, sample %u, the ARM7 %s, no ARM phase (%u)", DspStep(), Sample(),
				ArmClocked() ? "clocked" : "held", c.n[1] - st_held.c.n[1]);
			Check(ArmDiffers(ReadArm(), st_held.regs) == kArmRegs && words == 0,
				"nothing of the ARM7 executed: its registers read as before, and every word of its stored context but INTR_PEND is as it was (%u differ)", words);
		}

		// with the ARM7 held and the burst suspended: a step in the park context
		st_held.stopId = stop.stopId;

		if (!StepDsp())
			break;
		CO_YIELD(st_held.co);

		Check(stop.cause == StopCause::Step && stop.stopId == st_held.stopId + 1 && DspStep() == 2 && Suspended() == SOUND_BURST_DSP && !ArmClocked(),
			"and a step from there, the ARM7 held: \"%s\", the DSP in front of step %u", StopCauseName(stop.cause), DspStep());

		ArmBps({ });
	}

	CO_END(st_held.co);
}

/*
	dsp.step.sh4first: a breakpoint of the SH4 that is reached before the burst ends the wait of a step of the DSP
	with its own cause, and no step is left armed
*/
namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
} st_sh4;

}

BOUNDED_GUEST_TEST(dsp_step_sh4first, "dsp.step.sh4first")
{
	Counts c;
	std::vector<Seen> list;

	CO_BEGIN(st_sh4.co);

	while (!Settle(stop))
		CO_YIELD(st_sh4.co);

	// the loop of the SH4 comes by its first instruction every ten instructions
	Sh4Bps({ { 40, DSPTEST_G_MAIN_LOOP } });

	st_sh4.stopId = stop.stopId;
	st_sh4.c = ReadCounts();
	TakeSeen();

	if (!StepDsp())
		break;
	Check(!dsp::test::GetState().step && dbg_dsp_armed == 0, "emu::Step arms the action of the park loop: nothing of the DSP is armed before the loop acts on it");
	CO_YIELD(st_sh4.co);

	c = ReadCounts();
	list = TakeSeen();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 40 && stop.stopId == st_sh4.stopId + 1 && Ctx().pc == DSPTEST_G_MAIN_LOOP
		&& SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } }),
		"the breakpoint of the SH4 ends the wait of the step with its own cause: \"%s\" id %u, published %s", StopCauseName(stop.cause), stop.breakpointId,
		SeenText(list).c_str());
	Check(c.n[0] == st_sh4.c.n[0] && DspStep() == 0, "no burst was started and the DSP stands where it stood");
	Check(dbg_dsp_armed == 0 && !dsp::test::GetState().step, "no step is left armed: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);

	Sh4Bps({ });

	// and none comes back: a run from here is stopped by the test's pause
	while (!PauseAfter(stop, 3))
		CO_YIELD(st_sh4.co);

	Check(stop.cause == StopCause::Pause, "the run behind it is stopped by the test only (\"%s\")", StopCauseName(stop.cause));

	CO_END(st_sh4.co);
}

/*
	dsp.step.breakpointed: a step executes the step it stands on also with a listed breakpoint on it, whatever the
	stop it is asked from
*/
namespace {

struct {
	Co co;
	Counts c;
} st_bp;

}

BOUNDED_GUEST_TEST(dsp_step_breakpointed, "dsp.step.breakpointed")
{
	CO_BEGIN(st_bp.co);

	while (!Settle(stop))
		CO_YIELD(st_bp.co);

	// at the stop of the breakpoint, which stays listed
	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_bp.co);

	if (!StepDsp())
		break;
	CO_YIELD(st_bp.co);

	Check(stop.cause == StopCause::Step && DspStep() == kSite + 1 && Suspended() == SOUND_BURST_DSP,
		"a step from the stop of a breakpoint on step %u that stays listed executes the step: \"%s\", the DSP in front of step %u", kSite, StopCauseName(stop.cause),
		DspStep());

	// from a stop the DSP did not decide, with no burst suspended: a breakpoint on step 0, which the step is
	DspBps({ });

	while (!BehindBurst(stop))
		CO_YIELD(st_bp.co);

	DspBps({ { 50, 0 } });
	st_bp.c = ReadCounts();

	if (!StepDsp())
		break;
	CO_YIELD(st_bp.co);

	Check(stop.cause == StopCause::Step && DspStep() == 1 && Sample() == 0 && ReadCounts().n[0] == st_bp.c.n[0] + 1,
		"a step from a stop behind a burst, with a listed breakpoint on step 0: step 0 of the next burst executes and the step ends behind it (\"%s\", the DSP in "
		"front of step %u)", StopCauseName(stop.cause), DspStep());

	DspBps({ });

	CO_END(st_bp.co);
}

/*
	dsp.step.overout: there is nothing to step over or out of in a DSP program: -32004, and nothing is armed
*/
namespace {

struct {
	Co co;
	u32 stopId;
} st_oo;

}

BOUNDED_GUEST_TEST(dsp_step_overout, "dsp.step.overout")
{
	CO_BEGIN(st_oo.co);

	while (!Settle(stop))
		CO_YIELD(st_oo.co);

	Check(emu::TargetSteps(Target::Dsp), "the DSP steps: emu::TargetSteps(Target::Dsp) is true");
	Check(emu::Step(Target::Dsp, StepKind::Over) == kErrNotSupported && emu::Step(Target::Dsp, StepKind::Out) == kErrNotSupported,
		"at a stop behind a burst, a step over and a step out of the DSP answer -32004");

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_oo.co);

	Check(emu::Step(Target::Dsp, StepKind::Over) == kErrNotSupported && emu::Step(Target::Dsp, StepKind::Out) == kErrNotSupported,
		"and at a stop in front of a DSP step");
	Check(!dsp::test::GetState().step, "nothing was armed");

	DspBps({ });
	st_oo.stopId = stop.stopId;

	while (!PauseAfter(stop, 2))
		CO_YIELD(st_oo.co);

	Check(stop.cause == StopCause::Pause && stop.stopId == st_oo.stopId + 1, "and a resume from there is a resume: the next stop is the test's pause");

	CO_END(st_oo.co);
}

/*
	dsp.sh4step.dropped: a step of the SH4 from a stop of a DSP breakpoint that stays active runs the suspended burst
	on first, with the DSP's exemption: the breakpoint stops it again one sample later, and the step of the SH4 is
	dropped
*/
namespace {

struct {
	Co co;
	u32 stopId, sh4pc, mdec, r8;
	u64 time;
} sh4_drop;

}

BOUNDED_GUEST_TEST(dsp_sh4step_dropped, "dsp.sh4step.dropped")
{
	std::vector<Seen> list;

	CO_BEGIN(sh4_drop.co);

	while (!Settle(stop))
		CO_YIELD(sh4_drop.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(sh4_drop.co);

	sh4_drop.stopId = stop.stopId;
	sh4_drop.sh4pc = Ctx().pc;
	sh4_drop.r8 = Ctx().r[8];
	sh4_drop.mdec = Mdec();
	sh4_drop.time = sh4_sched_now64();
	TakeSeen();

	if (!StepSh4())
		break;
	CO_YIELD(sh4_drop.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == kSite && Sample() == 1 && Mdec() == ((sh4_drop.mdec - 1) & 0xFFFF),
		"a step of the SH4 from the stop of the breakpoint runs the burst on, and the breakpoint stops in front of the step one sample later: \"%s\" id %u, the "
		"DSP at step %u in sample %u, MDEC_CT %04X (it was %04X)", StopCauseName(stop.cause), stop.breakpointId, DspStep(), Sample(), Mdec(), sh4_drop.mdec);
	Check(Ctx().pc == sh4_drop.sh4pc && Ctx().r[8] == sh4_drop.r8 && sh4_sched_now64() == sh4_drop.time && stop.stopId == sh4_drop.stopId + 1
		&& SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } }),
		"the step of the SH4 is dropped: its PC and its counter are where they were, no time passed, one stop and no Running (%s)", SeenText(list).c_str());

	DspBps({ });

	CO_END(sh4_drop.co);
}

/*
	dsp.arm7step: a step of the ARM7 from a stop of a DSP breakpoint that stays active runs the suspended burst on
	with the DSP's exemption: the rest of the DSP phase runs, and the step executes its instruction in the ARM phase
	of the next sample; the DSP's breakpoint does not drop it
*/
namespace {

struct {
	Co co;
	u32 stopId, pc, mdec;
	Counts c;
} arm_step;

}

BOUNDED_GUEST_TEST(dsp_arm7step, "dsp.arm7step")
{
	Counts c;

	CO_BEGIN(arm_step.co);

	while (!Settle(stop))
		CO_YIELD(arm_step.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(arm_step.co);

	arm_step.stopId = stop.stopId;
	arm_step.pc = ArmPc();
	arm_step.mdec = Mdec();
	arm_step.c = ReadCounts();

	if (!StepArm())
		break;
	CO_YIELD(arm_step.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Step && stop.stopId == arm_step.stopId + 1 && ArmPc() != arm_step.pc && Suspended() == SOUND_BURST_ARM7,
		"a step of the ARM7 from the stop of the DSP breakpoint executes its instruction: \"%s\", the ARM7 from %08X to %08X, burst %u", StopCauseName(stop.cause),
		arm_step.pc, ArmPc(), Suspended());
	Check(c.n[2] == arm_step.c.n[2] + 1 && Sample() == 1 && DspStep() == 0 && Mdec() == ((arm_step.mdec - 1) & 0xFFFF) && c.n[0] == arm_step.c.n[0],
		"the rest of the DSP phase ran through the breakpoint, the sample ended, and the instruction is in the ARM phase of the next sample: %u time step, sample "
		"%u, the DSP in front of step %u", c.n[2] - arm_step.c.n[2], Sample(), DspStep());

	DspBps({ });

	CO_END(arm_step.co);
}

/*
	dsp.reset.suspended, dsp.hoststop.suspended, dsp.exit.suspended: the machine is reset, stopped by the host or
	ended by the guest while the thread is parked on a burst suspended in front of a DSP step, and on one suspended
	at the end of a sample. A reset discards the burst. The others run it to its end in front of the stop callback,
	without checks: the breakpoint of the DSP that is listed stops nothing
*/
namespace {

struct {
	Co co;
	u32 round, stopId, rest, arm, at;
	Counts c;
	DspCounts dc;
	std::atomic<bool> failed { false };
	std::atomic<u32> burstAtStop { 0 };
	Counts atStop;
	DspCounts dspAtStop;
	emu::RunState stopped;
} st_susp_end;

// Brings the burst to where the round has it: in front of step kSite, or at the end of a sample (a step of step 127
// with the breakpoint on it still listed). Returns false while it has armed something
struct {
	Co co;
} where;

bool SuspendFor(const emu::RunState& stop, u32 round)
{
	CO_BEGIN(where.co);

	if (round == 0)
	{
		while (!SuspendAtStep(stop, kSite))
			CO_YIELD(where.co);
	}
	else
	{
		while (!SuspendAtStep(stop, kLast))
			CO_YIELD(where.co);

		if (!StepDsp())
			break;
		CO_YIELD(where.co);

		if (stop.cause != StopCause::Step || Suspended() != SOUND_BURST_SAMPLE)
			Check(false, "a step of step 127 leaves the burst at the end of its sample (\"%s\", burst %u)", StopCauseName(stop.cause), Suspended());
	}

	CO_END(where.co);
}

const char* RoundName(u32 round)
{
	return round == 0 ? "in front of a DSP step" : "at the end of a sample";
}

bool SuspendedEnding(const emu::RunState& stop, bool exit)
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_susp_end.co);

	for (st_susp_end.round = 0; st_susp_end.round < 2; st_susp_end.round++)
	{
		while (!Settle(stop))
			CO_YIELD(st_susp_end.co);

		while (!SuspendFor(stop, st_susp_end.round))
			CO_YIELD(st_susp_end.co);

		st_susp_end.c = ReadCounts();
		st_susp_end.dc = ReadDspCounts();
		st_susp_end.at = Sample();
		// the rest of the burst: the samples from this one on (from its DSP phase, or from its ARM phase)
		st_susp_end.rest = 32 - st_susp_end.at;
		st_susp_end.arm = st_susp_end.round == 0 ? st_susp_end.rest - 1 : st_susp_end.rest;
		st_susp_end.failed = false;
		hand.stopCallbacks = 0;
		hand.stopSuspended = ~0u;
		TakeSeen();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Stopped))
			{
				st_susp_end.failed = true;
				return;
			}

			st_susp_end.stopped = emu::GetRunState();

			// with no emulator thread the caller runs it itself
			emu::Call(emu::CC_TABLE, [] {
				st_susp_end.burstAtStop = Suspended();
				st_susp_end.atStop = ReadCounts();
				st_susp_end.dspAtStop = ReadDspCounts();
			});

			Restart(false);
		});

		// as the host, and as the exit hook of reios, ask the machine
		if (exit)
			virtualDreamcast->RequestExit();
		else
			virtualDreamcast->Stop(HandStopped);
		CO_YIELD(st_susp_end.co);

		OtherJoin();
		list = TakeSeen();

		Check(!st_susp_end.failed && st_susp_end.stopped.lifetime == Lifetime::Stopped
			&& st_susp_end.stopped.cause == (exit ? StopCause::GuestExit : StopCause::HostStopped),
			"%s: the thread ended while it was parked on the burst, in sample %u", RoundName(st_susp_end.round), st_susp_end.at);

		if (!exit)
			Check(hand.stopCallbacks == 1 && hand.stopSuspended == SOUND_BURST_NONE && hand.stopCounts.n[3] == st_susp_end.c.n[3] + 1,
				"the burst is complete in front of the stop callback: the callback found none suspended, and the tail run");

		Check(st_susp_end.burstAtStop == SOUND_BURST_NONE && st_susp_end.atStop.n[0] == st_susp_end.c.n[0]
			&& st_susp_end.atStop.n[2] - st_susp_end.c.n[2] == st_susp_end.rest && st_susp_end.atStop.n[1] - st_susp_end.c.n[1] == st_susp_end.arm
			&& st_susp_end.atStop.n[3] == st_susp_end.c.n[3] + 1 && st_susp_end.dspAtStop.n[0] - st_susp_end.dc.n[0] == st_susp_end.rest,
			"behind the thread no burst is suspended: the rest of it ran once, %u time steps and DSP phases (%u, %u), %u ARM phases (%u), and its tail",
			st_susp_end.rest, st_susp_end.atStop.n[2] - st_susp_end.c.n[2], st_susp_end.dspAtStop.n[0] - st_susp_end.dc.n[0], st_susp_end.arm,
			st_susp_end.atStop.n[1] - st_susp_end.c.n[1]);
		Check(dbg_dsp_armed != 0 && !SeenHas(list, Lifetime::Parked, StopCause::Breakpoint) && !SeenHas(list, Lifetime::Parked, StopCause::Step),
			"nothing stopped it: the breakpoint of the DSP is still listed and active, and no stop was published (%s)", SeenText(list).c_str());
		Check(stop.cause == StopCause::Start, "the host starts the next thread");

		// the next callback of the AICA finds no burst suspended
		DspBps({ });
		st_susp_end.c = ReadCounts();

		while (!PauseAfter(stop, 3))
			CO_YIELD(st_susp_end.co);

		c = ReadCounts();

		Check(stop.cause == StopCause::Pause && c.n[0] - st_susp_end.c.n[0] >= 3 && c.n[3] - st_susp_end.c.n[3] == c.n[0] - st_susp_end.c.n[0],
			"and the machine runs on: %u bursts", c.n[0] - st_susp_end.c.n[0]);
	}

	CO_END(st_susp_end.co);
}

}

BOUNDED_GUEST_TEST(dsp_hoststop_suspended, "dsp.hoststop.suspended")
{
	return SuspendedEnding(stop, false);
}

BOUNDED_GUEST_TEST(dsp_exit_suspended, "dsp.exit.suspended")
{
	return SuspendedEnding(stop, true);
}

namespace {

struct {
	Co co;
	u32 round, stopId;
	Counts c;
	DspCounts dc;
} st_reset;

}

BOUNDED_GUEST_TEST(dsp_reset_suspended, "dsp.reset.suspended")
{
	std::vector<Seen> list;
	Counts c;
	DspCounts dc;

	CO_BEGIN(st_reset.co);

	for (st_reset.round = 0; st_reset.round < 2; st_reset.round++)
	{
		while (!Settle(stop))
			CO_YIELD(st_reset.co);

		while (!SuspendFor(stop, st_reset.round))
			CO_YIELD(st_reset.co);

		st_reset.stopId = stop.stopId;
		st_reset.c = ReadCounts();
		st_reset.dc = ReadDspCounts();
		TakeSeen();

		Check(emu::Reset() == 0, "%s: the debugger resets the machine at a stop inside a burst", RoundName(st_reset.round));
		Check(Suspended() == SOUND_BURST_NONE && DspStep() == 0, "the reset discards the suspended burst, and puts the DSP on step 0");
		CO_YIELD(st_reset.co);

		list = TakeSeen();
		c = ReadCounts();
		dc = ReadDspCounts();

		Check(stop.cause == StopCause::Reset && stop.stopId == st_reset.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Reset } }),
			"the stop of the reset is published: \"%s\"", StopCauseName(stop.cause));
		Check(c.n[1] == st_reset.c.n[1] && c.n[2] == st_reset.c.n[2] && c.n[3] == st_reset.c.n[3] && dc.n[0] == st_reset.dc.n[0] && Suspended() == SOUND_BURST_NONE,
			"nothing of the discarded burst ran: no ARM phase, no time step, no DSP phase, no tail");
		Check(!dsp::test::GetState().exempt && !dsp::test::GetState().stopByCheck && !dsp::test::GetState().step,
			"the run control of the DSP has no exemption, no step and no stop of the DSP to resume");

		// The breakpoint is still listed in the first round. The machine boots again, and the next callback of the
		// AICA finds no burst suspended: the breakpoint stops in its first sample
		st_reset.c = c;

		if (!Resume())
			break;
		CO_YIELD(st_reset.co);

		c = ReadCounts();

		if (st_reset.round == 0)
			Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == kSite && c.n[0] == st_reset.c.n[0] + 1,
				"the machine boots again, and the breakpoint of the DSP stops in the first burst (\"%s\" id %u, %u bursts)", StopCauseName(stop.cause),
				stop.breakpointId, c.n[0] - st_reset.c.n[0]);
		else
			Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == kLast && c.n[0] == st_reset.c.n[0] + 1,
				"the machine boots again, and the breakpoint on step 127 stops in the first burst (\"%s\" id %u, %u bursts)", StopCauseName(stop.cause),
				stop.breakpointId, c.n[0] - st_reset.c.n[0]);

		DspBps({ });
	}

	CO_END(st_reset.co);
}

/*
	dsp.sameslice.*: the machine is asked to stop, to exit or to reset in the tick in which a check of the DSP
	suspends its burst, by the hand, which runs behind the AICA's callback in that tick. dc_run meets the suspended
	burst with no Park() in between. (A burst run in its scheduler tick is suspended at the end of a sample only for
	the last frame of an SGC capture armed at a stop, which E8b adds: a step of the DSP that waits for its burst is
	step 0 of it and stops in front of step 1)
*/
namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
	DspCounts dc;
	std::atomic<bool> failed { false };
	std::atomic<u32> burstAtStop { 0 };
	Counts atStop;
	DspCounts dspAtStop;
	emu::RunState stopped;
} st_same;

bool SameSliceEnding(const emu::RunState& stop, bool exit)
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_same.co);

	while (!Settle(stop))
		CO_YIELD(st_same.co);

	st_same.c = ReadCounts();
	st_same.dc = ReadDspCounts();
	st_same.stopId = stop.stopId;
	st_same.failed = false;
	hand.stopCallbacks = 0;
	hand.stopSuspended = ~0u;
	TakeSeen();

	DspBps({ { 80, kSite } });
	HandOn(exit ? HAND_EXIT_SUSPENDED : HAND_STOP_SUSPENDED);

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Stopped))
		{
			st_same.failed = true;
			return;
		}

		st_same.stopped = emu::GetRunState();

		emu::Call(emu::CC_TABLE, [] {
			st_same.burstAtStop = Suspended();
			st_same.atStop = ReadCounts();
			st_same.dspAtStop = ReadDspCounts();
		});

		Restart(false);
	});

	if (!Resume())
		break;
	CO_YIELD(st_same.co);

	OtherJoin();
	HandOff();
	list = TakeSeen();

	Check(!st_same.failed && hand.asked && hand.suspended == 1 && hand.suspendedDsp == 1,
		"%s in the tick in which the breakpoint of the DSP suspended the burst, and the thread ended", exit ? "the guest ended the run" : "the host stopped the machine");
	Check(st_same.stopped.lifetime == Lifetime::Stopped && st_same.stopped.cause == (exit ? StopCause::GuestExit : StopCause::HostStopped)
		&& !SeenHas(list, Lifetime::Parked, StopCause::Breakpoint) && st_same.stopped.stopId == st_same.stopId + 1,
		"the stop of the breakpoint is not published: %s", SeenText(list).c_str());

	if (!exit)
		Check(hand.stopCallbacks == 1 && hand.stopSuspended == SOUND_BURST_NONE && hand.stopCounts.n[3] == st_same.c.n[3] + 1,
			"the burst is complete in front of the stop callback: the callback found none suspended, and the tail run");

	Check(st_same.burstAtStop == SOUND_BURST_NONE && st_same.atStop.n[0] == st_same.c.n[0] + 1 && st_same.atStop.n[1] == st_same.c.n[1] + 32
		&& st_same.atStop.n[2] == st_same.c.n[2] + 32 && st_same.atStop.n[3] == st_same.c.n[3] + 1 && st_same.dspAtStop.n[0] == st_same.dc.n[0] + 32,
		"behind the thread the burst is whole: 32 ARM phases, 32 time steps, 32 DSP phases, one tail, and none is suspended");
	Check(stop.cause == StopCause::Start, "the host starts the next thread");

	DspBps({ });
	st_same.c = ReadCounts();

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_same.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - st_same.c.n[0] >= 3 && c.n[3] - st_same.c.n[3] == c.n[0] - st_same.c.n[0],
		"the next callback of the AICA finds no burst suspended, and the machine runs on: %u bursts", c.n[0] - st_same.c.n[0]);

	CO_END(st_same.co);
}

}

BOUNDED_GUEST_TEST(dsp_sameslice_hoststop, "dsp.sameslice.hoststop")
{
	return SameSliceEnding(stop, false);
}

BOUNDED_GUEST_TEST(dsp_sameslice_exit, "dsp.sameslice.exit")
{
	return SameSliceEnding(stop, true);
}

namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
	DspCounts dc;
} st_same_reset;

}

BOUNDED_GUEST_TEST(dsp_sameslice_reset, "dsp.sameslice.reset")
{
	std::vector<Seen> list;
	Counts c;
	DspCounts dc;

	CO_BEGIN(st_same_reset.co);

	while (!Settle(stop))
		CO_YIELD(st_same_reset.co);

	st_same_reset.c = ReadCounts();
	st_same_reset.dc = ReadDspCounts();
	st_same_reset.stopId = stop.stopId;
	TakeSeen();

	DspBps({ { 81, kSite } });
	HandOn(HAND_RESET_SUSPENDED);

	if (!Resume())
		break;
	CO_YIELD(st_same_reset.co);

	HandOff();
	list = TakeSeen();
	c = ReadCounts();
	dc = ReadDspCounts();

	Check(hand.asked && hand.suspended == 1 && hand.suspendedDsp == 1, "the guest reset the machine in the tick in which the breakpoint of the DSP suspended the burst");
	Check(stop.cause == StopCause::Reset && stop.stopId == st_same_reset.stopId + 1
		&& SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Reset } }),
		"the stop that is published is the reset's: \"%s\"; published %s", StopCauseName(stop.cause), SeenText(list).c_str());
	Check(Suspended() == SOUND_BURST_NONE && c.n[0] == st_same_reset.c.n[0] + 1 && c.n[3] == st_same_reset.c.n[3] && c.n[2] == st_same_reset.c.n[2]
		&& dc.n[0] == st_same_reset.dc.n[0] && DspStep() == 0,
		"the burst is discarded: none is suspended, its DSP phase and its tail never ran, and the DSP is on step 0");
	Check(Ctx().pc == kResetVector && !ArmClocked() && !dsp::test::GetState().stopByCheck, "the machine is reset, and no stop of the DSP is there to resume");

	// the breakpoint is still listed: it stops in the first burst of the boot
	st_same_reset.c = c;

	if (!Resume())
		break;
	CO_YIELD(st_same_reset.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 81 && DspStep() == kSite && c.n[0] == st_same_reset.c.n[0] + 1,
		"the next callback of the AICA finds no burst suspended: the machine boots, and the breakpoint stops in its first burst (%u)", c.n[0] - st_same_reset.c.n[0]);

	DspBps({ });

	CO_END(st_same_reset.co);
}

/*
	dsp.step.wait.hoststop, dsp.step.wait.exit: the wait of a step of the DSP asked for with no burst suspended, in
	which the SH4 runs to the next burst with the stop in force, ended in its first slice by the host or by the
	guest, as arm7.step.wait.* end the wait of a step of the ARM7. No stop of the step is published. The exit of the
	thread ends the step (dsp::OnStop of emu::OnThreadExiting): behind the thread no step of the DSP is armed and
	dbg_dsp_armed is 0. The wait counts as a step that has executed, so the next thread, which starts as one
	without a session does (no start park), resumes nothing; and it does not stop for the step of the thread before:
	the event breakpoint on the timers of the AICA, listed behind the thread, stops it behind its first burst, no
	check of the DSP made. A step left armed would execute step 0 of that burst and stop in front of step 1
*/
namespace {

struct {
	Co co;
	u32 stopId;
	u32 checks;
	Counts c;
	DspCounts dc;
	std::atomic<bool> failed { false };
	std::atomic<u32> armedAtStop { 0 };
	std::atomic<bool> stepAtStop { false };
	std::atomic<bool> exemptAtStop { false };
	std::atomic<u32> burstAtStop { 0 };
	emu::RunState stopped;
	Counts atStop;
	DspCounts dspAtStop;
	u32 checksAtStop;
} st_wait;

// The other thread: the emulator thread has ended in the wait of the step. What it left, then the event
// breakpoint, and a thread with no start park
void WaitEnded()
{
	if (!WaitLifetime(Lifetime::Stopped))
	{
		st_wait.failed = true;
		return;
	}

	st_wait.stopped = emu::GetRunState();

	// with no emulator thread the caller runs it itself
	int rv = emu::Call(emu::CC_TABLE, [] {
		const dsp::test::State s = dsp::test::GetState();

		st_wait.armedAtStop = dbg_dsp_armed;
		st_wait.stepAtStop = s.step;
		st_wait.exemptAtStop = s.exempt;
		st_wait.burstAtStop = Suspended();
		st_wait.atStop = ReadCounts();
		st_wait.dspAtStop = ReadDspCounts();
		st_wait.checksAtStop = dsp::test::checks;

		EventBp(kBehindId);
	});

	if (rv != 0)
		st_wait.failed = true;

	Restart(true);
}

bool WaitEnding(const emu::RunState& stop, HandMode mode, const char* what, StopCause ended)
{
	std::vector<Seen> list;
	Counts c;
	DspCounts dc;

	CO_BEGIN(st_wait.co);

	while (!Settle(stop))
		CO_YIELD(st_wait.co);

	Check(stop.cause == StopCause::Event && Suspended() == SOUND_BURST_NONE && ArmClocked(), "a stop behind a burst, none suspended, the ARM7 clocked (\"%s\", burst %u)",
		StopCauseName(stop.cause), Suspended());

	st_wait.stopId = stop.stopId;
	st_wait.failed = false;
	st_wait.c = ReadCounts();
	hand.stopCallbacks = 0;
	TakeSeen();

	// in the first slice of the wait
	HandOn(mode);

	if (!StepDsp())
	{
		HandOff();
		break;
	}

	OtherStart(WaitEnded);
	CO_YIELD(st_wait.co);

	OtherJoin();
	HandOff();
	emu::test::plainStart = false;
	NoEventBp();
	list = TakeSeen();
	c = ReadCounts();
	dc = ReadDspCounts();

	Check(!st_wait.failed && hand.asked && hand.slices >= 1 && st_wait.atStop.n[0] == st_wait.c.n[0],
		"%s in the first slice of the wait of a step of the DSP, in front of the burst it waited for, and the thread ended (%u bursts)", what,
		st_wait.atStop.n[0] - st_wait.c.n[0]);
	Check(st_wait.stopped.lifetime == Lifetime::Stopped && st_wait.stopped.cause == ended && !SeenHas(list, Lifetime::Parked, StopCause::Step),
		"no stop of the step is published: %s", SeenText(list).c_str());
	Check(st_wait.armedAtStop == 0 && !st_wait.stepAtStop && !st_wait.exemptAtStop && st_wait.burstAtStop == SOUND_BURST_NONE,
		"behind the thread no step of the DSP is armed: dbg_dsp_armed %u, a step %s, an exemption %s, burst %u", st_wait.armedAtStop.load(),
		st_wait.stepAtStop ? "armed" : "not armed", st_wait.exemptAtStop ? "armed" : "not armed", st_wait.burstAtStop.load());
	Check(stop.cause == StopCause::Event && stop.breakpointId == kBehindId && Suspended() == SOUND_BURST_NONE && DspStep() == 0 && c.n[0] > st_wait.atStop.n[0]
		&& c.n[3] - st_wait.atStop.n[3] == c.n[0] - st_wait.atStop.n[0] && dc.n[1] == st_wait.dspAtStop.n[1] && dsp::test::checks == st_wait.checksAtStop,
		"the next thread, with no start park, does not stop for the step of the thread before: the event breakpoint stops it behind its first burst, no check "
		"of the DSP made (\"%s\" id %u, burst %u, the DSP at step %u, %u bursts and %u tails, %u checks of the DSP, %u DSP phases through the debug loop)",
		StopCauseName(stop.cause), stop.breakpointId, Suspended(), DspStep(), c.n[0] - st_wait.atStop.n[0], c.n[3] - st_wait.atStop.n[3],
		dsp::test::checks - st_wait.checksAtStop, dc.n[1] - st_wait.dspAtStop.n[1]);

	CO_END(st_wait.co);
}

}

BOUNDED_GUEST_TEST(dsp_step_wait_hoststop, "dsp.step.wait.hoststop")
{
	if (!WaitEnding(stop, HAND_STOP_FIRST, "the host stopped the machine", StopCause::HostStopped))
		return false;

	Check(hand.stopCallbacks.load() == 1, "the stop callback ran once (%d)", hand.stopCallbacks.load());
	return true;
}

BOUNDED_GUEST_TEST(dsp_step_wait_exit, "dsp.step.wait.exit")
{
	return WaitEnding(stop, HAND_EXIT_FIRST, "the guest ended the run", StopCause::GuestExit);
}

/*
	dsp.hoststop.owed: the stop of a breakpoint of the DSP on step 0, in the first sample of a burst, which stays
	listed; the host stops the machine there. The burst runs to its end in front of the stop callback, and the DSP
	stands in front of step 0 again, that of the next burst. The thread ends in the stop with the SH4 where the stop
	was published, so the next thread owes it its resume; the exit of the thread ends what the stop of the DSP left
	for that resume (dsp::OnStop of emu::OnThreadExiting): the DSP decided no stop since. The next thread starts
	with no start park, and its first Park() resumes the stop that was owed, which arms nothing for the DSP: the
	breakpoint stops in front of step 0 of the first sample of the next burst, with nothing of it executed. An
	exemption armed by that resume would run through step 0 and stop in front of it one sample later, MDEC_CT one
	less
*/
namespace {

struct {
	Co co;
	u32 stopId, pc;
	Counts c;
	std::atomic<bool> failed { false };
	emu::RunState stopped;
	std::atomic<bool> stopByCheckAtStop { true };
	std::atomic<bool> exemptAtStop { true };
	std::atomic<u32> burstAtStop { 0 };
	std::atomic<u32> stepAtStop { 0 };
	std::atomic<u32> armedAtStop { 0 };
	std::atomic<u32> pcAtStop { 0 };
	std::atomic<u32> mdecAtStop { 0 };
	Counts atStop;
} st_owed;

}

BOUNDED_GUEST_TEST(dsp_hoststop_owed, "dsp.hoststop.owed")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_owed.co);

	while (!Settle(stop))
		CO_YIELD(st_owed.co);

	while (!SuspendAtStep(stop, 0))
		CO_YIELD(st_owed.co);

	if (!Check(Sample() == 0 && dsp::test::GetState().stopByCheck && dsp::test::GetState().stopStep == 0,
		"the stop of the breakpoint on step 0 is in the first sample of its burst, and the DSP decided it (sample %u)", Sample()))
		break;

	st_owed.stopId = stop.stopId;
	st_owed.pc = Ctx().pc;
	st_owed.c = ReadCounts();
	st_owed.failed = false;
	hand.stopCallbacks = 0;
	hand.stopSuspended = ~0u;
	TakeSeen();

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Stopped))
		{
			st_owed.failed = true;
			return;
		}

		st_owed.stopped = emu::GetRunState();

		// with no emulator thread the caller runs it itself
		int rv = emu::Call(emu::CC_TABLE, [] {
			const dsp::test::State s = dsp::test::GetState();

			st_owed.stopByCheckAtStop = s.stopByCheck;
			st_owed.exemptAtStop = s.exempt;
			st_owed.burstAtStop = Suspended();
			st_owed.stepAtStop = DspStep();
			st_owed.armedAtStop = dbg_dsp_armed;
			st_owed.pcAtStop = Ctx().pc;
			st_owed.mdecAtStop = Mdec();
			st_owed.atStop = ReadCounts();
		});

		if (rv != 0)
			st_owed.failed = true;

		// as a thread without a session starts: with no start park
		Restart(true);
	});

	virtualDreamcast->Stop(HandStopped);
	CO_YIELD(st_owed.co);

	OtherJoin();
	emu::test::plainStart = false;
	list = TakeSeen();
	c = ReadCounts();

	Check(!st_owed.failed && st_owed.stopped.lifetime == Lifetime::Stopped && st_owed.stopped.cause == StopCause::HostStopped && hand.stopCallbacks == 1
		&& hand.stopSuspended == SOUND_BURST_NONE,
		"the host stopped the machine at the stop of the breakpoint, and the burst was complete in front of the stop callback");
	Check(st_owed.burstAtStop == SOUND_BURST_NONE && st_owed.stepAtStop == 0 && st_owed.armedAtStop != 0 && st_owed.atStop.n[0] == st_owed.c.n[0]
		&& st_owed.atStop.n[3] == st_owed.c.n[3] + 1 && st_owed.pcAtStop == st_owed.pc,
		"behind the thread the burst is whole, the DSP stands in front of step 0 of the next one with the breakpoint listed and active (dbg_dsp_armed %u), "
		"and the SH4 is where the stop was published (%08X, it was %08X): the next thread owes the stop its resume", st_owed.armedAtStop.load(),
		st_owed.pcAtStop.load(), st_owed.pc);
	Check(!st_owed.stopByCheckAtStop && !st_owed.exemptAtStop,
		"the exit of the thread ended what the stop of the DSP left for a resume: no stop of the DSP is recorded, no exemption is armed (a stop of the DSP %s)",
		st_owed.stopByCheckAtStop ? "still recorded" : "not recorded");
	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == 0 && Suspended() == SOUND_BURST_DSP && Sample() == 0
		&& c.n[0] == st_owed.atStop.n[0] + 1 && c.n[2] == st_owed.atStop.n[2] && Mdec() == st_owed.mdecAtStop && stop.stopId == st_owed.stopped.stopId + 1
		&& SeenIs(list, { { Lifetime::Stopped, StopCause::HostStopped }, { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Breakpoint } }),
		"the owed resume of the next thread arms nothing for the DSP: the breakpoint on step 0 stops in front of it in the first sample of the next burst, "
		"which has not executed it (\"%s\" id %u, step %u, burst %u, %u bursts, %u time steps, MDEC_CT %04X, it was %04X; published %s)",
		StopCauseName(stop.cause), stop.breakpointId, DspStep(), Suspended(), c.n[0] - st_owed.atStop.n[0], c.n[2] - st_owed.atStop.n[2], Mdec(),
		st_owed.mdecAtStop.load(), SeenText(list).c_str());

	DspBps({ });

	CO_END(st_owed.co);
}

/*
	dsp.reads.suspended: at a stop in front of a DSP step every accessor of the debugger reads the same twice: the
	DSP's state is the core's fields, its program the core's MPRO, the registers of the DSP in the AICA's register
	file what the core answers a read of the CPU, the rows of dc.aica.dsp the state's fields; and the burst stands
	where it stood
*/
namespace {

struct {
	Co co;
} st_reads;

}

BOUNDED_GUEST_TEST(dsp_reads_suspended, "dsp.reads.suspended")
{
	CO_BEGIN(st_reads.co);

	while (!Settle(stop))
		CO_YIELD(st_reads.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_reads.co);

	{
		DspDebugState a, b;

		memset(&a, 0x11, sizeof(a));
		memset(&b, 0x22, sizeof(b));

		const bool ok = Dsp()->DebugGetState(&a) && Dsp()->DebugGetState(&b);

		Check(ok && memcmp(&a, &b, sizeof(a)) == 0 && a.step == kSite, "DSP::DebugGetState reads the same twice, the DSP in front of step %u", a.step);
		Check(StateIsCore(a), "and gives the core's fields: step %u, MDEC_CT %04X, ACC %X, Y_REG %X, FRC_REG %X, ADRS_REG %X, the latch %X and its copy %X, MEMVAL %X, "
			"NOFL %X, the MIXS bank %X", a.step, a.mdecCt, (u32)a.acc, (u32)a.yReg, a.frcReg, a.adrsReg, a.latch, a.latchCopy, a.memval, a.noflPipe, a.mixsBank);

		const Rows r1 = ReadRows(), r2 = ReadRows(), want = RowsOf(a);

		Check(r1.ok && r2.ok && RowsDiffer(r1, r2) == kDspRowCount && RowsDiffer(r1, want) == kDspRowCount,
			"the thirteen rows of dc.aica.dsp read the same twice, and are the fields of the state under their widths (the first that differs: %s)",
			RowsDiffer(r1, want) == kDspRowCount ? "none" : kDspRows[RowsDiffer(r1, want)]);
	}

	{
		const AicaDsp* core = Dsp()->Core();
		u32 steps = 0;

		for (u32 step = 0; step < kSteps; step++)
		{
			u16 pa[4] = { 1, 2, 3, 4 }, pb[4] = { 5, 6, 7, 8 };

			if (Dsp()->DebugGetProgramStep(step, pa) && Dsp()->DebugGetProgramStep(step, pb) && memcmp(pa, pb, sizeof(pa)) == 0 && pa[0] == core->MPRO[step][0]
				&& pa[1] == core->MPRO[step][1] && pa[2] == core->MPRO[step][2] && pa[3] == core->MPRO[step][3])
				steps++;
		}

		Check(steps == kSteps, "the program reads the same twice, the core's MPRO, in %u of the 128 steps", steps);
	}

	{
		// the DSP's part of the register file: 0x2804 and 0x3000 .. 0x45C7
		static u8 a[0x8000], b[0x8000];
		const AicaDsp* core = Dsp()->Core();
		const bool ok = Aica()->DebugReadRegs(0, 0x8000, a) && Aica()->DebugReadRegs(0, 0x8000, b);
		u32 slots = 0, right = 0;

		for (u32 off = 0x2804; off < 0x45C8; off += 4)
		{
			if (!AicaDsp::RegOwned(off))
				continue;

			const u32 got = a[off] | (a[off + 1] << 8) | (a[off + 2] << 16) | (a[off + 3] << 24);

			slots++;

			if (got == (core->RegRead(off) & 0xFFFF))
				right++;
		}

		Check(ok && memcmp(a, b, sizeof(a)) == 0, "the register file of the AICA reads the same twice");
		Check(slots > 0 && right == slots, "and in its %u slots of the DSP it is what the core answers a read of the CPU, the upper half 0 (%u right)", slots, right);
	}

	{
		const ArmRegs a = ReadArm(), b = ReadArm();
		u32 channels = 0;

		for (u32 ch = 0; ch < 64; ch++)
		{
			AicaChannelDebug ca, cb;

			memset(&ca, 0, sizeof(ca));
			memset(&cb, 0, sizeof(cb));

			if (Aica()->DebugGetChannel(ch, &ca) && Aica()->DebugGetChannel(ch, &cb) && memcmp(&ca, &cb, sizeof(ca)) == 0)
				channels++;
		}

		Check(ArmDiffers(a, b) == kArmRegs && channels == 64, "the registers of the ARM7 and the state of the 64 channels read the same twice (%u)", channels);
	}

	Check(Suspended() == SOUND_BURST_DSP && DspStep() == kSite, "and the burst is suspended where it was, in front of step %u", DspStep());

	DspBps({ });

	CO_END(st_reads.co);
}

/*
	dsp.regs.move: the nine datapath rows of dc.aica.dsp follow the DSP through the steps of one sample of the
	guest's program: from the stop in front of step 0 (a breakpoint there) through the 128 steps of the sample to
	the end of it. Each of the nine takes at least two values, and at every stop the rows are the state's fields and
	the state the core's
*/
namespace {

struct {
	Co co;
	u32 n, good, inOrder;
	u64 values[kSteps + 1][kDspRowCount];
} st_move;

}

BOUNDED_GUEST_TEST(dsp_regs_move, "dsp.regs.move")
{
	CO_BEGIN(st_move.co);

	while (!Settle(stop))
		CO_YIELD(st_move.co);

	while (!SuspendAtStep(stop, 0))
		CO_YIELD(st_move.co);

	DspBps({ });

	st_move.good = st_move.inOrder = 0;

	for (st_move.n = 0; st_move.n <= kSteps; st_move.n++)
	{
		{
			const DspDebugState s = DspState();
			const Rows rows = ReadRows();

			if (rows.ok && RowsDiffer(rows, RowsOf(s)) == kDspRowCount && StateIsCore(s))
				st_move.good++;

			if (s.step == (st_move.n & 127))
				st_move.inOrder++;

			for (u32 i = 0; i < kDspRowCount; i++)
				st_move.values[st_move.n][i] = rows.v[i];
		}

		if (st_move.n == kSteps)
			break;

		if (!StepDsp())
			break;
		CO_YIELD(st_move.co);

		if (stop.cause != StopCause::Step)
			break;
	}

	Check(st_move.n == kSteps && st_move.good == kSteps + 1 && st_move.inOrder == kSteps + 1,
		"%u stops, from the one in front of step 0 through the 128 steps of the sample: the rows are the state's fields and the state the core's at %u, STEP is "
		"the step at %u", st_move.n + 1, st_move.good, st_move.inOrder);

	{
		u32 moved = 0;
		std::string still;

		for (u32 i = kDatapathFrom; i < kDspRowCount; i++)
		{
			std::set<u64> distinct;

			for (u32 n = 0; n <= kSteps; n++)
				distinct.insert(st_move.values[n][i]);

			if (distinct.size() >= 2)
				moved++;
			else
				still += std::string(still.empty() ? "" : " ") + kDspRows[i];

			Note("%s: %zu values over the sample", kDspRows[i], distinct.size());
		}

		Check(moved == kDspRowCount - kDatapathFrom, "each of the nine datapath rows takes at least two values over the steps of one sample (%u do; the rows that "
			"do not: %s)", moved, still.empty() ? "none" : still.c_str());
	}

	CO_END(st_move.co);
}

/*
	dsp.grace: the grace expiry on a burst suspended in front of a DSP step. The core clears the active mask first
	and resumes second: with the mask cleared the resume runs through the breakpoint that suspended the burst, and
	through every later sample
*/
namespace {

struct {
	Co co;
	Counts c;
	DspCounts dc;
	u32 checks, at;
} st_gr;

}

BOUNDED_GUEST_TEST(dsp_grace, "dsp.grace")
{
	std::vector<Seen> list;
	Counts c;
	DspCounts dc;
	u32 bursts;

	CO_BEGIN(st_gr.co);

	while (!Settle(stop))
		CO_YIELD(st_gr.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_gr.co);

	emu::SetActiveMask(0);
	Check(dbg_dsp_armed == 0 && dsp::test::GetState().listed == 1, "the mask is cleared while the burst is suspended: the breakpoint is listed, nothing is armed");

	st_gr.c = ReadCounts();
	st_gr.dc = ReadDspCounts();
	st_gr.checks = dsp::test::checks;
	st_gr.at = Sample();
	TakeSeen();

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_gr.co);

	list = TakeSeen();
	c = ReadCounts();
	dc = ReadDspCounts();
	bursts = c.n[0] - st_gr.c.n[0];

	Check(stop.cause == StopCause::Pause && SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } }),
		"the resume runs through: published %s", SeenText(list).c_str());
	Check(bursts >= 3 && c.n[3] - st_gr.c.n[3] == bursts + 1 && dc.n[0] - st_gr.dc.n[0] == (32 - st_gr.at) + 32 * bursts && dsp::test::checks == st_gr.checks,
		"the suspended burst ran to its end with its tail, %u more bursts ran with every DSP phase, and no check was made (%u)", bursts,
		dsp::test::checks - st_gr.checks);
	Check(dc.n[1] - st_gr.dc.n[1] == 1, "of those DSP phases one had steps of the debug loop: the one that was suspended (%u)", dc.n[1] - st_gr.dc.n[1]);

	emu::SetActiveMask(kMaskAll);
	DspBps({ });

	CO_END(st_gr.co);
}

/*
	dspn.*: the debugger at work on the DSP in a run that the sound harness, or an Exit.* key, ends. The sound line
	and the state hash of such a run have to be those of a run nobody touched (tests/debugger/guest/neutral,
	tests/debugger/guest/dsptest/soundlines.py --mode neutral). A test of these never returns true: a session of it
	does not end by itself. Each says what it did in lines its target requires, so that a test that armed nothing is
	not taken for one that changed nothing.

	Their stops behind a burst are those of an event breakpoint on the timers of the AICA, latched at a slice end;
	their stops of the DSP are latched behind the tick of their burst, as every sound stop. Outside a SLEEP wait (the
	guest does not sleep) a run that goes on from them keeps no trace of them. Each test has done its work early, as
	those of the ARM7 (dbg_selftest_arm7.cpp, arm7n.*): no stop falls into the tick in which the run ends.
*/
namespace {

const u32 kNeutralEvent = 950;

// The run has gone on behind what the test had to do. Nothing stops it any more; a stop that comes all the same
// is a failure, and is resumed. The run ends the wait: it has no bound
#define NEUTRAL_END(co) \
	Unbounded(); \
	for (;;) \
	{ \
		if (!Resume()) \
			return false; \
		CO_YIELD(co); \
		Check(false, "a stop that the test did not ask for: \"%s\" id %u", StopCauseName(stop.cause), stop.breakpointId); \
	}

struct {
	Co co;
	u32 checks;
	Counts c;
	DspCounts dc;
	std::atomic<u32> bursts { 0 };
	std::atomic<bool> listed { false };
} n_loop;

// How far the run has to be before the test takes its one stop, in sound bursts: late, and with room for the stop in
// front of the 2000 ticks that end the run of the sound line
const u32 kLoopLate = 900;

}

/*
	dspn.loop: the debug loop of the DSP forced for the whole run, with nothing listed (the test hook of
	dbg_dsp_private.h): every DSP phase of the run goes through it, and its checks stop nowhere
*/
BOUNDED_GUEST_TEST(dspn_loop, "dspn.loop")
{
	Counts c;
	DspCounts dc;
	u32 bursts, phases, looped, checks;

	CO_BEGIN(n_loop.co);

	emu::SetActiveMask(kMaskAll);
	dsp::test::SetLoopForced(true);

	n_loop.checks = dsp::test::checks;
	n_loop.c = ReadCounts();
	n_loop.dc = ReadDspCounts();

	Check(stop.cause == StopCause::Start && dbg_dsp_armed != 0 && dsp::test::GetState().listed == 0,
		"at the start of the run the debug loop of the DSP is forced with nothing listed: dbg_dsp_armed is %u", (u32)dbg_dsp_armed);

	// A thread looks at the run, with calls that only read, and lists the event breakpoint when the run is late
	OtherStart([] {
		for (Clock::time_point t0 = Clock::now(); ; )
		{
			emu::Call(emu::CC_READ, [] { n_loop.bursts = ReadCounts().n[0]; });

			if (n_loop.bursts - n_loop.c.n[0] >= kLoopLate)
				break;

			// a run that does not get that far: the breakpoint is not listed, and the wait of the test is cut
			if (MsSince(t0) > kBoundMs)
				return;

			selftest::NapUs(500);
		}

		emu::Call(emu::CC_TABLE, [] {
			EventBp(kNeutralEvent);
			n_loop.listed = true;
		});
	});

	if (!Resume())
		break;
	CO_YIELD(n_loop.co);

	OtherJoin();
	NoEventBp();

	c = ReadCounts();
	dc = ReadDspCounts();
	bursts = c.n[0] - n_loop.c.n[0];
	phases = dc.n[0] - n_loop.dc.n[0];
	looped = dc.n[1] - n_loop.dc.n[1];
	checks = dsp::test::checks - n_loop.checks;

	Check(stop.cause == StopCause::Event && stop.breakpointId == kNeutralEvent && n_loop.listed && Suspended() == SOUND_BURST_NONE,
		"late in the run, one stop behind a burst: \"%s\"", StopCauseName(stop.cause));
	Check(dbg_dsp_armed != 0 && bursts >= kLoopLate && phases == 32 * bursts && looped == phases && checks == kSteps * phases,
		"dbg_dsp_armed is %u there; in the %u bursts since the start every DSP phase of the run went through the debug loop: %u phases, %u of them by the "
		"loop, %u checks, one in front of every step", (u32)dbg_dsp_armed, bursts, phases, looped, checks);

	NEUTRAL_END(n_loop.co);

	CO_END(n_loop.co);
}

/*
	dspn.hitresume: breakpoints of the DSP that are hit, each resumed. At a stop behind a burst one is listed on a
	step of a list of ten, in turn; it stops in the first sample of the next burst, and a resume from there stops in
	front of it again one sample later, through the exemption; it is removed there, and the run goes on to the next
	stop behind a burst
*/
namespace {

const u32 kHitSteps[] = { 0, 1, 3, 5, 9, 11, 13, 17, 64, 127 };
const u32 kHitStepCount = sizeof(kHitSteps) / sizeof(kHitSteps[0]);

struct {
	Co co;
	u32 hits, bursts, other, cycle, lastBurst;
	u32 at[kHitStepCount];
	bool listed;
} n_hit;

}

BOUNDED_GUEST_TEST(dspn_hitresume, "dspn.hitresume")
{
	u32 burst, steps;

	CO_BEGIN(n_hit.co);

	emu::SetActiveMask(kMaskAll);
	EventBp(kNeutralEvent);

	n_hit.hits = n_hit.bursts = n_hit.other = n_hit.cycle = 0;
	n_hit.lastBurst = ~0u;
	n_hit.listed = false;
	memset(n_hit.at, 0, sizeof(n_hit.at));

	for (;;)
	{
		if (!Resume())
			return false;
		CO_YIELD(n_hit.co);

		if (stop.cause == StopCause::Breakpoint && Suspended() == SOUND_BURST_DSP && stop.breakpointId >= 1 && stop.breakpointId <= kHitStepCount
			&& DspStep() == kHitSteps[stop.breakpointId - 1])
		{
			burst = ReadCounts().n[0];

			if (burst != n_hit.lastBurst)
				n_hit.bursts++;

			n_hit.lastBurst = burst;
			n_hit.hits++;
			n_hit.at[stop.breakpointId - 1]++;

			// the second hit of a breakpoint goes, and the resume runs the burst on
			if (!n_hit.listed)
				DspBps({ });

			n_hit.listed = false;
			continue;
		}

		if (stop.cause != StopCause::Event || Suspended() != SOUND_BURST_NONE)
		{
			n_hit.other++;
			continue;
		}

		// behind a burst
		if (n_hit.hits >= 200)
			break;

		{
			const u32 which = n_hit.cycle++ % kHitStepCount;

			DspBps({ { which + 1, kHitSteps[which] } });
			n_hit.listed = true;
		}
	}

	NoEventBp();
	DspBps({ });

	steps = 0;

	for (u32 i = 0; i < kHitStepCount; i++)
		steps += n_hit.at[i] != 0;

	Check(n_hit.hits >= 200 && n_hit.bursts >= 100 && steps >= 8 && n_hit.other == 0,
		"%u stops at breakpoints of the DSP, each resumed, in %u different bursts, at %u different steps (%u stops of another kind)", n_hit.hits, n_hit.bursts,
		steps, n_hit.other);

	NEUTRAL_END(n_hit.co);

	CO_END(n_hit.co);
}

/*
	dspn.steps: single steps of the DSP, until each kind was taken at least 20 times:
	  - from a stop with no burst suspended: the SH4 runs to the next burst, whose step 0 of its first sample is the
	    step;
	  - inside a DSP phase: from one step of a sample to the next;
	  - of step 127 across the end of a sample: the burst is suspended there;
	  - of step 127 of the last sample across the end of a burst: the tail runs, and none is suspended;
	  - from a stop of the ARM7: the rest of its ARM phase runs, and step 0 of the sample is the step.
	A cycle: a stop behind a burst; a step from there and steps inside the phase; the breakpoint on step 127,
	resumed sample by sample, with a step across the end of a sample on the way and one across the end of the burst
	at its last sample; then a breakpoint of the ARM7 in the next burst and a step of the DSP from its stop; a resume
*/
namespace {

struct {
	Co co;
	u32 fromStop, inside, acrossSample, acrossEnd, fromArm7;
	u32 steps, cycles, bad, other, n, step;
	Counts c;
} n_steps;

bool NeutralStepsDone()
{
	return n_steps.fromStop >= 20 && n_steps.inside >= 20 && n_steps.acrossSample >= 20 && n_steps.acrossEnd >= 20 && n_steps.fromArm7 >= 20;
}

}

BOUNDED_GUEST_TEST(dspn_steps, "dspn.steps")
{
	Counts c;

	CO_BEGIN(n_steps.co);

	emu::SetActiveMask(kMaskAll);

	n_steps.fromStop = n_steps.inside = n_steps.acrossSample = n_steps.acrossEnd = n_steps.fromArm7 = 0;
	n_steps.steps = n_steps.cycles = n_steps.bad = n_steps.other = 0;

	for (;;)
	{
		// a stop behind a burst
		EventBp(kNeutralEvent);

		if (!Resume())
			return false;
		CO_YIELD(n_steps.co);

		NoEventBp();

		if (stop.cause != StopCause::Event || Suspended() != SOUND_BURST_NONE)
		{
			n_steps.other++;
			continue;
		}

		if (NeutralStepsDone())
			break;

		n_steps.cycles++;
		NoteBetweenBursts();

		// from a stop with no burst suspended
		n_steps.c = ReadCounts();

		if (!StepDsp())
			return false;
		CO_YIELD(n_steps.co);

		n_steps.steps++;

		if (stop.cause == StopCause::Step && Suspended() == SOUND_BURST_DSP && DspStep() == 1 && ReadCounts().n[0] == n_steps.c.n[0] + 1)
			n_steps.fromStop++;
		else
		{
			n_steps.bad++;
			continue;
		}

		// inside the phase
		for (n_steps.n = 0; n_steps.n < 3; n_steps.n++)
		{
			n_steps.step = DspStep();

			if (!StepDsp())
				return false;
			CO_YIELD(n_steps.co);

			n_steps.steps++;

			if (stop.cause == StopCause::Step && Suspended() == SOUND_BURST_DSP && DspStep() == n_steps.step + 1)
				n_steps.inside++;
			else
				n_steps.bad++;
		}

		// the breakpoint on step 127, to the last sample; a step across the end of the first sample on the way
		DspBps({ { 1, kLast } });

		for (n_steps.n = 0; n_steps.n < 40; n_steps.n++)
		{
			if (!Resume())
				return false;
			CO_YIELD(n_steps.co);

			if (stop.cause != StopCause::Breakpoint || stop.breakpointId != 1 || DspStep() != kLast || Suspended() != SOUND_BURST_DSP)
			{
				n_steps.bad++;
				break;
			}

			if (Sample() == 0 || Sample() == 31)
			{
				DspBps({ });
				n_steps.c = ReadCounts();

				if (!StepDsp())
					return false;
				CO_YIELD(n_steps.co);

				n_steps.steps++;
				c = ReadCounts();

				if (stop.cause != StopCause::Step || DspStep() != 0)
					n_steps.bad++;
				else if (Suspended() == SOUND_BURST_SAMPLE && c.n[3] == n_steps.c.n[3])
					n_steps.acrossSample++;
				else if (Suspended() == SOUND_BURST_NONE && c.n[3] == n_steps.c.n[3] + 1)
					n_steps.acrossEnd++;
				else
					n_steps.bad++;

				if (Suspended() == SOUND_BURST_NONE)
					break;

				DspBps({ { 1, kLast } });
			}
		}

		DspBps({ });

		if (Suspended() != SOUND_BURST_NONE)
		{
			n_steps.bad++;
			continue;
		}

		// a stop of the ARM7 in the next burst, and a step of the DSP from it
		ArmBps({ { 2, kMain } });

		if (!Resume())
			return false;
		CO_YIELD(n_steps.co);

		ArmBps({ });

		if (stop.cause != StopCause::Breakpoint || stop.breakpointId != 2 || Suspended() != SOUND_BURST_ARM7)
		{
			n_steps.other++;
			continue;
		}

		n_steps.c = ReadCounts();

		if (!StepDsp())
			return false;
		CO_YIELD(n_steps.co);

		n_steps.steps++;
		c = ReadCounts();

		if (stop.cause == StopCause::Step && Suspended() == SOUND_BURST_DSP && DspStep() == 1 && c.n[1] == n_steps.c.n[1] + 1)
			n_steps.fromArm7++;
		else
			n_steps.bad++;
	}

	Check(n_steps.bad == 0 && n_steps.other == 0, "%u single steps of the DSP in %u cycles, each cycle followed by a resume: every stop was the one the cycle asked "
		"for (%u were not; %u stops of another kind)", n_steps.steps, n_steps.cycles, n_steps.bad, n_steps.other);
	Check(n_steps.fromStop >= 20, "%u steps from a stop with no burst suspended", n_steps.fromStop);
	Check(n_steps.inside >= 20, "%u steps inside a DSP phase", n_steps.inside);
	Check(n_steps.acrossSample >= 20, "%u steps of step 127 across the end of a sample", n_steps.acrossSample);
	Check(n_steps.acrossEnd >= 20, "%u steps of step 127 across the end of a burst", n_steps.acrossEnd);
	Check(n_steps.fromArm7 >= 20, "%u steps from a stop of the ARM7", n_steps.fromArm7);

	NEUTRAL_END(n_steps.co);

	CO_END(n_steps.co);
}

/*
	dspn.hoststop: the completion in dc_run of a burst suspended in front of a DSP step, and of one suspended at the
	end of a sample, held to the run nobody touched. Sixty times the host stops the machine while the thread is
	parked on such a burst, and starts it again: the thread that ends runs the rest of the burst in front of its stop
	callback, without checks, from where the DSP left it (inside its DSP phase, or in front of the next sample). The
	next thread, which has no start park, runs on. The tests of the session hold this path to its counts
	(dsp.hoststop.suspended, dsp.exit.suspended); what the rest of the burst computed is held here, by the sound
	line and the state hash: a completion that made the sweep of the sample again, or dropped the rest of a phase,
	moves both. The breakpoints are on steps of a list in turn, in samples that move from cycle to cycle
*/
namespace {

const u32 kHostSteps[] = { 1, 4, 5, 9, 11, 16, 17, 64 };
const u32 kHostStepCount = sizeof(kHostSteps) / sizeof(kHostSteps[0]);
const u32 kHostStops = 60;
const u32 kHostBpId = 100;

struct {
	Co co;
	u32 cycles, bad, other, inDsp, atEnd, resumes;
	bool counted;
	Counts whole;
	DspCounts wholeDsp;
	std::atomic<u32> callbacks { 0 }, callbacksSuspended { 0 };
	std::atomic<bool> failed { false };
} n_host;

// the stop callback of the host: the burst is whole in front of it
void NeutralHostStopped()
{
	n_host.callbacks++;

	if (Suspended() != SOUND_BURST_NONE)
		n_host.callbacksSuspended++;
}

// At a stop behind a burst: was every burst since the stop behind a burst before this one whole, whichever thread
// ran it to its end?
bool NeutralHostWhole()
{
	const Counts c = ReadCounts();
	const DspCounts dc = ReadDspCounts();
	const u32 bursts = c.n[0] - n_host.whole.n[0];
	const bool ok = !n_host.counted
		|| (c.n[1] - n_host.whole.n[1] == 32 * bursts && c.n[2] - n_host.whole.n[2] == 32 * bursts && c.n[3] - n_host.whole.n[3] == bursts
			&& dc.n[0] - n_host.wholeDsp.n[0] == 32 * bursts);

	n_host.whole = c;
	n_host.wholeDsp = dc;
	n_host.counted = true;

	return ok;
}

}

BOUNDED_GUEST_TEST(dspn_hoststop, "dspn.hoststop")
{
	CO_BEGIN(n_host.co);

	emu::SetActiveMask(kMaskAll);
	EventBp(kNeutralEvent);

	n_host.cycles = n_host.bad = n_host.other = n_host.inDsp = n_host.atEnd = n_host.resumes = 0;
	n_host.counted = false;

	for (;;)
	{
		if (!Resume())
			return false;
		CO_YIELD(n_host.co);

		if (stop.cause == StopCause::Breakpoint && Suspended() == SOUND_BURST_DSP && stop.breakpointId >= kHostBpId
			&& stop.breakpointId < kHostBpId + kHostStepCount + 1 && n_host.cycles < kHostStops)
		{
			// further into the burst: a resume or more, one sample each
			if (n_host.resumes > 0)
			{
				n_host.resumes--;
				continue;
			}

			DspBps({ });

			// every third cycle on step 127, stepped to the end of its sample
			if (stop.breakpointId == kHostBpId + kHostStepCount)
			{
				if (!StepDsp())
					return false;
				CO_YIELD(n_host.co);

				if (stop.cause != StopCause::Step || Suspended() != SOUND_BURST_SAMPLE)
				{
					n_host.bad++;
					continue;
				}

				n_host.atEnd++;
			}
			else
			{
				n_host.inDsp++;
			}

			OtherStart([] {
				if (!WaitLifetime(Lifetime::Stopped))
				{
					n_host.failed = true;
					return;
				}

				// as a thread without a session starts: with no start park
				Restart(true);
			});

			virtualDreamcast->Stop(NeutralHostStopped);
			CO_YIELD(n_host.co);

			// the first stop of the next thread: behind a burst, at the event breakpoint, which is still listed
			OtherJoin();
			n_host.cycles++;

			if (stop.cause != StopCause::Event || Suspended() != SOUND_BURST_NONE || !NeutralHostWhole())
				n_host.bad++;

			continue;
		}

		if (stop.cause != StopCause::Event || Suspended() != SOUND_BURST_NONE)
		{
			n_host.other++;
			continue;
		}

		// behind a burst
		if (!NeutralHostWhole())
			n_host.bad++;

		if (n_host.cycles >= kHostStops)
			break;

		{
			const u32 which = n_host.cycles % (kHostStepCount + 1);

			if (which == kHostStepCount)
				DspBps({ { kHostBpId + kHostStepCount, kLast } });
			else
				DspBps({ { kHostBpId + which, kHostSteps[which] } });

			// the sample moves from cycle to cycle: 0 to 30
			n_host.resumes = (n_host.cycles * 7) % 31;
		}
	}

	NoEventBp();
	DspBps({ });

	Check(n_host.cycles == kHostStops && n_host.bad == 0 && n_host.other == 0 && !n_host.failed && n_host.inDsp >= 20 && n_host.atEnd >= 5,
		"the host stopped the machine %u times while the thread was parked on a suspended burst, %u times in front of a DSP step and %u times at the end of a "
		"sample, and started it again: the next thread ran on to a stop behind a burst, with every burst whole (%u did not; %u stops of another kind)",
		n_host.cycles, n_host.inDsp, n_host.atEnd, n_host.bad, n_host.other);
	Check(n_host.callbacks == kHostStops && n_host.callbacksSuspended == 0, "each of the %u stop callbacks found the burst whole (%u did not)",
		n_host.callbacks.load(), n_host.callbacksSuspended.load());

	NEUTRAL_END(n_host.co);

	CO_END(n_host.co);
}
