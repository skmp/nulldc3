/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of the sound side of the debugger: the sound burst that can be suspended and run on, the
	breakpoints and steps of the ARM7 (dbg_arm7.cpp), what the park loop does with them (dbg_emu.cpp), and the
	register writer of the ARM7 (SoundCPU::DebugSetReg).

	    nulldc3 --config config:Headless=yes , audio:backend=none , Debugger:SelfTest=arm7 tests/debugger/guest/dsptest/dsptest.elf

	The guest is the sounding guest of the tree (tests/debugger/guest/dsptest, README.md there): an SH4 program that
	counts in a loop and an ARM7 program of its own, whose addresses come from dsptest_labels.inc. Every test
	is a guest test: it stands where a client stands and is handed every stop. A test begins by bringing the
	guest to where a session finds it after its boot (Settle), whatever the test before it left: the SH4 in its
	loop, the ARM7 clocked and in its own, no breakpoint listed, no sound burst suspended.

	What another thread of the debugger does (a call), and what the host does (a stop, a start), is done by a
	thread the test starts: "the other thread". What has to happen at a moment of the emulated machine - in the
	tick in which a check suspends a burst, in the first slice of a run - is done by a scheduler callback of
	the tests ("the hand"), which is registered behind the AICA's and so runs behind it in its tick.

	Four tests are not part of "arm7", because a session of theirs does not end with the summary of the driver
	(arm7n.*, at the end of this file): they are what tests/debugger/guest/neutral holds the sound line and the
	state hash of a run against.

	Checks are counted in sums where a test repeats something hundreds of times: one line says how often a
	property held.
*/

#include "dbg_selftest.h"
#include "dbg_arm7.h"
#include "dbg_arm7_private.h"
#include "dbg_sh4.h"
#include "dbg_callstack.h"
#include "dbg_core_private.h"
#include "dbg_mem.h"

#include "libswirl.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7_context.h"
#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp.h"

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
const u32 kMain     = DSPTEST_A_MAIN_LOOP;		// the first instruction of the loop
const u32 kPlain    = DSPTEST_A_FWIN + 4;		// a load inside the F window: F is set in front of it and behind it
const u32 kLastOfWindow = DSPTEST_A_FWIN_END - 4;	// the last plain instruction of the F window
const u32 kUnmask   = DSPTEST_A_FWIN_END;		// the msr that ends the F window
const u32 kBl       = DSPTEST_A_LEAF - 8;		// bl a_leaf
const u32 kBehindBl = DSPTEST_A_LEAF - 4;		// the line behind it
const u32 kLeaf     = DSPTEST_A_LEAF;
const u32 kFiq      = DSPTEST_A_FIQ;
const u32 kRaise    = DSPTEST_A_FIQ_END - 4;	// the store that raises the AICA's interrupt to the SH4, every 64th FIQ
const u32 kVector   = 0x1C;						// the FIQ vector
const u32 kNever    = 0x000F0000;				// wave memory that the guest never executes

const u32 kMaskAll = (1u << (u32)Category::Events) | (1u << (u32)Category::Sh4) | (1u << (u32)Category::Arm7) | (1u << (u32)Category::Dsp);

// The registers of the ARM7 as the debugger reads them: R0..R14, the PC, the CPSR, the SPSR and the banks
const u32 kArmRegs = SPSR_FIQ + 1;

// The period of the AICA's scheduler callback, in cycles of the SH4 (AICA_TICK of libswirl.cpp)
const u64 kAicaPeriod = 145124;

// Registers of the AICA, as offsets into its register file
const u32 kTIMA = 0x2890, kTIMB = 0x2894, kTIMC = 0x2898;

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
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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

u32 ArmMode()
{
	return Arm(RN_CPSR) & 0x1F;
}

// Is a request of the AICA latched for the ARM7 and not answered yet?
bool ArmInService()
{
	u32 level = 0, in_service = 0;

	Scpu()->DebugGetInterrupt(&level, &in_service);
	return in_service != 0;
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

// As the register writer of the debugger will (dbg::regs::Write): the store, and the word to the breakpoint
// state for a PC
bool SetArm(u32 index, u32 value)
{
	bool ok = Scpu()->DebugSetReg(index, value);

	if (ok && index == 15)
		arm7::OnPcWritten();

	return ok;
}

u32 AicaReg16(u32 offset)
{
	u8 bytes[2] = { 0, 0 };

	Aica()->DebugReadRegs(offset, 2, bytes);
	return bytes[0] | (bytes[1] << 8);
}

u32 ArmWord(u32 addr)
{
	u32 word;

	memcpy(&word, &sh4_cpu->aica_ram.data[addr & (sh4_cpu->aica_ram.size - 1)], 4);
	return word;
}

u32 Suspended()
{
	return virtualDreamcast->SoundBurstSuspended();
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

/*
	The tables
*/
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
	Everything that is published, in the order it was, since the test looked last (TakeSeen). The list has an
	end: a test that goes wrong can publish without end - every step of the ARM7 is a stop -, and it has to fail
	by a check of its own, not by the memory of the machine. What a test looks at between two looks is a burst
	of steps at most, fewer than kStepsMax
*/
struct Seen {
	Lifetime lifetime;
	u32 stopId;
	StopCause cause;
	u32 breakpointId;
	u32 notice;
};

// More steps of the ARM7 than a sound burst has instructions (32 samples of 256 ticks): a loop of steps that
// waits for something of its burst ends here at the latest, and fails
const u32 kStepsMax = 100000;

// What the list keeps: a loop of kStepsMax steps and what is published around it
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
	The bound of a wait. A test that has armed something, or that waits for what another thread does, returns
	false and is handed the next stop. Where the machine never gives that stop the session would wait until the
	watchdog of its runner ended the process, with no summary. So every wait has a bound, kBoundMs, the one
	constant of the bound in this file: a thread of the session, the watch, looks at the wait that is in
	progress and cuts one that is older than the bound. It then asks for a stop that the machine can still give,
	each for a tenth of the bound, in this order:
	  - a pause, where the published state is Running;
	  - a stop of the host; where the thread stays parked through that, a call that resumes it (the leave finds
	    the stop of the host pending, and the thread ends);
	  - once the machine is stopped, a start of the host: the next thread parks at its start, as every thread of
	    a session does.
	The test is handed that stop, fails by a check that says so, and ends. Every test behind it in the session
	fails at once with one check, arming nothing: the machine stands where no test begins. The driver then prints
	its summary. A machine that gives no stop through all of that has the watch print the failed check itself and
	end the process.
	A bound is generous: a test says its longest wait in a note, and a session that passes waits less than a tenth
	of a second for any of its stops where the bound is half a minute. The waits of the threads that the tests
	start have the same bound, and the host starts the machine on one thread at a time (Restart). The last wait
	of a test that the run ends (arm7n.*, NEUTRAL_END) has no bound.
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

		std::this_thread::sleep_for(std::chrono::microseconds(100));
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
	is registered behind the callback of the AICA, so in a tick they share it runs behind the sound burst. It
	says what the slices of a run showed, and it is where a test asks for something at a moment of the machine.
*/
enum HandMode {
	HAND_WATCH,					// looks only
	HAND_PAUSE,					// asks for a pause once the bursts started have reached hand.target
	HAND_STOP_FIRST,			// has the host stop the machine in the first slice
	HAND_EXIT_FIRST,			// has the guest end the run there
	HAND_RESET_FIRST,			// has the guest reset the machine there
	HAND_HOLD_FIRST,			// holds the first slice, in host time, until hand.release is set
	HAND_STOP_IN_SLEEP,			// has the host stop the machine in the first slice of a SLEEP wait of the SH4
	HAND_STOP_SUSPENDED,		// has the host stop the machine in the tick in which a burst was suspended
	HAND_EXIT_SUSPENDED,
	HAND_RESET_SUSPENDED,
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
	u32 latched = 0;				// the stop latch was set
	u64 firstTime = 0;				// the scheduler's time of the first slice

	// HAND_HOLD_FIRST
	std::atomic<bool> holding { false };
	std::atomic<bool> release { false };
	std::atomic<bool> sawCall { false };	// a call was queued while the slice was held

	// what the stop callback of HAND_STOP_* found
	std::atomic<int> stopCallbacks { 0 };
	std::atomic<u32> stopSuspended { 0 };
	std::atomic<bool> stopLatched { false };
	Counts stopCounts;
} hand;

void HandStopped()
{
	hand.stopCallbacks++;
	hand.stopSuspended = Suspended();
	hand.stopLatched = g_stop_latched;
	hand.stopCounts = ReadCounts();
}

int HandTick(void*, int, int, int)
{
	if (!hand.on)
		return 0;

	const u32 burst = Suspended();

	if (hand.slices++ == 0)
		hand.firstTime = sh4_sched_now64();

	if (!g_parked.load())
		hand.notParked++;
	if (emu::GetRunState().lifetime != Lifetime::Parked)
		hand.notPublishedParked++;
	if (burst != SOUND_BURST_NONE)
		hand.suspended++;
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

	case HAND_STOP_FIRST:
	case HAND_EXIT_FIRST:
	case HAND_RESET_FIRST:
	case HAND_STOP_SUSPENDED:
	case HAND_EXIT_SUSPENDED:
	case HAND_RESET_SUSPENDED:
		if (!hand.asked && (hand.mode == HAND_STOP_FIRST || hand.mode == HAND_EXIT_FIRST || hand.mode == HAND_RESET_FIRST || burst != SOUND_BURST_NONE))
		{
			hand.asked = true;

			if (hand.mode == HAND_STOP_FIRST || hand.mode == HAND_STOP_SUSPENDED)
				virtualDreamcast->Stop(HandStopped);
			else if (hand.mode == HAND_EXIT_FIRST || hand.mode == HAND_EXIT_SUSPENDED)
				virtualDreamcast->RequestExit();
			else
				virtualDreamcast->RequestReset();
		}
		break;

	case HAND_STOP_IN_SLEEP:
		if (!hand.asked && g_in_sleep)
		{
			hand.asked = true;
			virtualDreamcast->Stop(HandStopped);
		}
		break;

	case HAND_HOLD_FIRST:
		if (!hand.asked)
		{
			hand.asked = true;
			hand.holding = true;

			// until the other threads have queued their calls, and never for long
			Clock::time_point t0 = Clock::now();

			while (!hand.release && MsSince(t0) < kHoldMs)
				std::this_thread::sleep_for(std::chrono::microseconds(200));

			hand.sawCall = (g_attn.load() & ATTN_CALL) != 0;
			hand.holding = false;
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
	hand.latched = 0;
	hand.firstTime = 0;
	hand.holding = false;
	hand.release = false;
	hand.sawCall = false;

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

bool StepArm(StepKind kind = StepKind::Into)
{
	int rv = emu::Step(Target::Arm7, kind);

	if (rv != 0)
		Check(false, "a step of the ARM7 was refused: %d", rv);

	return rv == 0;
}

void ClearTables()
{
	ArmBps({ });
	Sh4Bps({ });
	NoEventBp();
	emu::SetActiveMask(kMaskAll);
}

/*
	Settle: the guest as a session finds it behind its boot. The SH4 is sent to its loop (through the release of
	the ARM7 when a test left that held; through the boot when the machine was reset), and the run is stopped
	behind the first sound burst in which a timer of the AICA overflows: the ARM7 is in its loop then, and no
	burst is suspended. Returns false while it has armed something
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

	if (Ctx().pc != kResetVector)
		GotoSh4(ArmClocked() ? DSPTEST_G_MAIN : DSPTEST_G_RELEASE_ARM7);

	Sh4Bps({ { kSettleId, DSPTEST_G_MAIN_LOOP } });

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

		if (stop.cause == StopCause::Event && Suspended() == SOUND_BURST_NONE && ArmClocked() && ArmPc() >= kMain && ArmPc() < DSPTEST_A_FIQ_END + 8)
			break;
	}

	if (settle.tries == 8)
		Check(false, "the ARM7 of the guest gets to its loop, and a timer of the AICA overflows in a burst (a stop with \"%s\", the ARM7 at %08X)",
			StopCauseName(stop.cause), ArmPc());

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

	CO_END(behind.co);
}

/*
	A stop at a breakpoint of the ARM7, which stays listed: the burst is suspended in front of addr
*/
const u32 kAtId = 902;

struct {
	Co co;
} at;

bool SuspendAt(const emu::RunState& stop, u32 addr)
{
	CO_BEGIN(at.co);

	ArmBps({ { kAtId, addr } });

	if (!Resume())
		break;
	CO_YIELD(at.co);

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kAtId || ArmPc() != addr || Suspended() != SOUND_BURST_ARM7)
		Check(false, "a stop at a breakpoint of the ARM7 on %08X, the burst suspended (a stop with \"%s\" id %u, the ARM7 at %08X, burst %u)", addr,
			StopCauseName(stop.cause), stop.breakpointId, ArmPc(), Suspended());

	CO_END(at.co);
}

/*
	A pause behind `bursts` more sound bursts, asked for by the hand: the stop of a run that nothing of the ARM7
	stopped. It counts bursts, and works whether the ARM7 is clocked or held: with the ARM7 held the timers of the
	AICA count on, but no handler arms timer A again, and no timer overflows in every burst
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

	CO_END(pause_after.co);
}

/*
	Steps of the ARM7 from a suspended burst until the burst is over: returns true at the stop of the step whose
	instruction was the last of its burst, or at the first stop that is not what a step of a suspended burst
	gives, or behind kStepsMax steps with the burst still suspended. The steps are counted in step_end.steps
*/
struct {
	Co co;
	u32 steps;
	u32 bad;				// steps whose stop was not a step's
	u64 time;
} step_end;

bool StepToBurstEnd(const emu::RunState& stop)
{
	CO_BEGIN(step_end.co);

	step_end.steps = 0;
	step_end.bad = 0;
	step_end.time = sh4_sched_now64();

	while (Suspended() != SOUND_BURST_NONE && step_end.steps < kStepsMax)
	{
		if (!StepArm())
			break;
		CO_YIELD(step_end.co);

		step_end.steps++;

		if (stop.cause != StopCause::Step || stop.lifetime != Lifetime::Parked || sh4_sched_now64() != step_end.time)
		{
			step_end.bad++;
			break;
		}
	}

	CO_END(step_end.co);
}

}

/*
	arm7.burst.plain: what a sound burst is made of, by the counts of the machine
*/
namespace {

struct {
	Co co;
	Counts a, b;
} bp_plain;

}

BOUNDED_GUEST_TEST(arm7_burst_plain, "arm7.burst.plain")
{
	Counts c;
	u32 d[4];

	CO_BEGIN(bp_plain.co);

	while (!Settle(stop))
		CO_YIELD(bp_plain.co);

	Check(dbg_arm7_armed == 0, "nothing of the ARM7 is armed: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);

	while (!PauseAfter(stop, 2))
		CO_YIELD(bp_plain.co);
	bp_plain.a = ReadCounts();

	while (!PauseAfter(stop, 5))
		CO_YIELD(bp_plain.co);
	c = ReadCounts();

	for (int i = 0; i < 4; i++)
		d[i] = c.n[i] - bp_plain.a.n[i];

	Check(stop.cause == StopCause::Pause && ArmClocked() && d[0] >= 5, "two pauses with %u sound bursts between them, the ARM7 clocked", d[0]);
	Check(d[1] == 32 * d[0] && d[2] == 32 * d[0] && d[3] == d[0],
		"a burst with the ARM7 clocked is 32 ARM phases, 32 time steps of the AICA and one tail: %u, %u and %u in %u bursts", d[1], d[2], d[3], d[0]);
	Check(Suspended() == SOUND_BURST_NONE, "no burst is suspended at a pause");

	// the guest holds its ARM7
	GotoSh4(DSPTEST_G_HOLD_ARM7);

	while (!PauseAfter(stop, 2))
		CO_YIELD(bp_plain.co);
	bp_plain.a = ReadCounts();

	while (!PauseAfter(stop, 5))
		CO_YIELD(bp_plain.co);
	c = ReadCounts();

	for (int i = 0; i < 4; i++)
		d[i] = c.n[i] - bp_plain.a.n[i];

	Check(stop.cause == StopCause::Pause && !ArmClocked() && d[0] >= 5, "two pauses with %u sound bursts between them, the ARM7 held in reset", d[0]);
	Check(d[1] == 0 && d[2] == 32 * d[0] && d[3] == d[0],
		"a burst with the ARM7 held is no ARM phase, 32 time steps of the AICA and one tail: %u, %u and %u in %u bursts", d[1], d[2], d[3], d[0]);

	CO_END(bp_plain.co);
}

/*
	arm7.held.clocked: the AICA is clocked whatever ARMRST says, and only the ARM7 is held by it. The guest holds
	its ARM7 (g_hold_arm7) across k bursts, k odd:
	  - each burst is one burst, no ARM phase, 32 time steps and one tail (SoundBurstCounts);
	  - nothing of the ARM7 executed: every register of it as the debugger reads it is as at the hold, and so is
	    every word of its stored context but INTR_PEND (which ARM7Backend::UpdateInterrupts sets to e68k_out &&
	    armFiqEnable when the interrupt update latches a request: it moves with the request below), and the four
	    fields behind the words (armIrqEnable, armFiqEnable, armMode, enabled);
	  - the DSP made its samples: MDEC_CT, which counts down once a sample, is 32 k less modulo 2^16;
	  - the channels the guest keyed play on: both are still enabled, and the play position of the second one
	    moved. At OCT -1 and any of the four FNS the ARM7 gives it, it steps 16, 20, 24 or 28 samples a burst,
	    so an odd number of bursts never closes its loop of 32 samples; the first channel, which at FNS 0 makes
	    exactly one loop a burst, is no witness;
	  - the interrupt update ran in the held bursts: the request of the ARM7's timer, which a_reset enabled
	    (timer A, level 2, 13 samples), is raised and latched behind the hold, as the stored interrupt state of
	    the sound CPU shows - aica_interr 1 with aica_reg_L 2, e68k_reg_M 1, e68k_reg_L 2 and e68k_out set; and
	    INTR_PEND set with it where the held ARM7's F is clear, 0 where F is set (inside a_fwin, or at the
	    return of a_fiq).
	The hold is made with no request pending and none in service: the stored aica_interr and e68k_reg_M are 0 at
	the pause in front of it, behind a burst; a pause that finds either is followed by another one burst later,
	32 times at most. The held bursts span more than the 13 samples of the timer (one burst does)
*/
namespace {

const u32 kHeldBursts = 3;
const u32 kHeldSpinId = 50;

// Fields of the stored state of the sound CPU (kScpuStateSize bytes): the interrupt controller in front of the
// context, and the four fields behind it
const size_t kScpuInterr = 0, kScpuRegL = 1, kScpuOut = 5, kScpuE68kL = 6, kScpuE68kM = 10;
const size_t kScpuTailAt = kScpuWordsAt + RN_ARM_REG_COUNT * 4;
const size_t kScpuTailSize = 1 + 1 + 4 + 1;

u32 ScpuField(const std::vector<u8>& state, size_t at, size_t size)
{
	u32 value = 0xDEADBEEF;

	if (state.size() == kScpuStateSize)
	{
		value = 0;
		memcpy(&value, &state[at], size);
	}

	return value;
}

struct {
	Co co;
	u32 tries;
	bool ready;
	Counts c;
	ArmRegs regs;
	std::vector<u8> state;
	DspDebugState dsp;
	AicaChannelDebug ch[2];
} held_cl;

}

BOUNDED_GUEST_TEST(arm7_held_clocked, "arm7.held.clocked")
{
	Counts c;
	u32 d[4];

	CO_BEGIN(held_cl.co);

	while (!Settle(stop))
		CO_YIELD(held_cl.co);

	// a pause behind a burst that left no request of the AICA pending and none in service
	held_cl.ready = false;

	for (held_cl.tries = 0; held_cl.tries < 32 && !held_cl.ready; )
	{
		held_cl.tries++;

		while (!PauseAfter(stop, 1))
			CO_YIELD(held_cl.co);

		held_cl.state = ScpuState();
		held_cl.ready = stop.cause == StopCause::Pause && ArmClocked() && Suspended() == SOUND_BURST_NONE && ScpuField(held_cl.state, kScpuInterr, 1) == 0
			&& ScpuField(held_cl.state, kScpuE68kM, 4) == 0;
	}

	Check(held_cl.ready, "a pause behind a burst with the ARM7 clocked, no request of the AICA pending and none in service (%u pauses)", held_cl.tries);
	if (!held_cl.ready)
		break;

	// the guest holds its ARM7, and the SH4 stops in its spin, long before the next burst
	held_cl.c = ReadCounts();
	GotoSh4(DSPTEST_G_HOLD_ARM7);
	Sh4Bps({ { kHeldSpinId, DSPTEST_G_HOLD_ARM7_SPIN } });

	if (!Resume())
		break;
	CO_YIELD(held_cl.co);

	Sh4Bps({ });
	c = ReadCounts();

	if (stop.cause != StopCause::Breakpoint || stop.breakpointId != kHeldSpinId || ArmClocked() || c.n[0] != held_cl.c.n[0])
	{
		Check(false, "the guest holds its ARM7 in front of the next burst: a stop at g_hold_arm7_spin (\"%s\" id %u), the ARM7 %s, %u bursts since the pause",
			StopCauseName(stop.cause), stop.breakpointId, ArmClocked() ? "clocked" : "held", c.n[0] - held_cl.c.n[0]);
		break;
	}

	// the hold, in front of the held bursts
	held_cl.c = c;
	held_cl.regs = ReadArm();
	held_cl.state = ScpuState();
	held_cl.dsp = { };
	held_cl.ch[0] = { };
	held_cl.ch[1] = { };

	{
		DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
		const bool got = dsp->DebugGetState(&held_cl.dsp) && Aica()->DebugGetChannel(0, &held_cl.ch[0]) && Aica()->DebugGetChannel(1, &held_cl.ch[1]);

		Check(got && held_cl.state.size() == kScpuStateSize && ScpuField(held_cl.state, kScpuInterr, 1) == 0 && ScpuField(held_cl.state, kScpuE68kM, 4) == 0
			&& held_cl.ch[0].enabled && held_cl.ch[1].enabled,
			"the ARM7 is held with no request pending (aica_interr %u) and none in service (e68k_reg_M %u), and the two channels the guest keyed play (%d, %d)",
			ScpuField(held_cl.state, kScpuInterr, 1), ScpuField(held_cl.state, kScpuE68kM, 4), held_cl.ch[0].enabled, held_cl.ch[1].enabled);
		Note("held at the ARM7's PC %08X, mode %02X, F %s; e68k_out %u, INTR_PEND %u; MDEC_CT %04X; the second channel at %u.%03X, %u.%03X a sample; after %u pauses",
			ArmPc(), ArmMode(), (Arm(RN_CPSR) & 0x40) ? "set" : "clear", ScpuField(held_cl.state, kScpuOut, 1), StoredWord(held_cl.state, INTR_PEND),
			held_cl.dsp.mdecCt & 0xFFFF, held_cl.ch[1].ca, held_cl.ch[1].caFraction, held_cl.ch[1].step >> 10, held_cl.ch[1].step & 0x3FF, held_cl.tries);
	}

	// k bursts that find the ARM7 held
	while (!PauseAfter(stop, kHeldBursts))
		CO_YIELD(held_cl.co);

	c = ReadCounts();

	for (int i = 0; i < 4; i++)
		d[i] = c.n[i] - held_cl.c.n[i];

	Check(stop.cause == StopCause::Pause && !ArmClocked() && Suspended() == SOUND_BURST_NONE && d[0] == kHeldBursts,
		"a pause behind %u bursts that found the ARM7 held: %u bursts, a stop with \"%s\", the ARM7 %s", kHeldBursts, d[0], StopCauseName(stop.cause),
		ArmClocked() ? "clocked" : "held");
	Check(d[1] == 0 && d[2] == 32 * d[0] && d[3] == d[0],
		"each burst that finds the ARM7 held is one burst, no ARM phase, 32 time steps of the AICA and one tail: %u, %u and %u in %u bursts", d[1], d[2], d[3], d[0]);

	{
		const ArmRegs regs = ReadArm();
		const std::vector<u8> state = ScpuState();
		const u32 differs = ArmDiffers(regs, held_cl.regs);
		u32 words = 0, first = RN_ARM_REG_COUNT;

		for (u32 i = 0; i < RN_ARM_REG_COUNT; i++)
		{
			if (i == INTR_PEND)
				continue;

			if (StoredWord(state, i) == StoredWord(held_cl.state, i))
				words++;
			else if (first == RN_ARM_REG_COUNT)
				first = i;
		}

		const bool tail = state.size() == kScpuStateSize && held_cl.state.size() == kScpuStateSize
			&& memcmp(&state[kScpuTailAt], &held_cl.state[kScpuTailAt], kScpuTailSize) == 0;

		Check(differs == kArmRegs, "nothing of the ARM7 executed: every register of it reads as at the hold (the first that differs: %s; the PC %08X, at the hold %08X)",
			differs == kArmRegs ? "none" : std::to_string(differs).c_str(), ArmPc(), held_cl.regs.r[15]);
		Check(words == RN_ARM_REG_COUNT - 1 && tail,
			"every word of its stored context but INTR_PEND is as at the hold (%u of %u; the first that differs: %s), and so are armIrqEnable, armFiqEnable, armMode and "
			"enabled (%s)", words, RN_ARM_REG_COUNT - 1, first == RN_ARM_REG_COUNT ? "none" : std::to_string(first).c_str(), tail ? "the same" : "not the same");

		DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
		DspDebugState now = { };
		const bool got = dsp->DebugGetState(&now);

		Check(got && ((held_cl.dsp.mdecCt - 32 * kHeldBursts) & 0xFFFF) == (now.mdecCt & 0xFFFF),
			"the DSP made the samples of the held bursts: MDEC_CT is %04X; it was %04X at the hold, and %u samples count it down to %04X modulo 2^16",
			now.mdecCt & 0xFFFF, held_cl.dsp.mdecCt & 0xFFFF, 32 * kHeldBursts, (held_cl.dsp.mdecCt - 32 * kHeldBursts) & 0xFFFF);

		AicaChannelDebug ch[2] = { };
		const bool gotCh = Aica()->DebugGetChannel(0, &ch[0]) && Aica()->DebugGetChannel(1, &ch[1]);

		Check(gotCh && ch[0].enabled && ch[1].enabled && (ch[1].ca != held_cl.ch[1].ca || ch[1].caFraction != held_cl.ch[1].caFraction),
			"the channels the guest keyed play on: both are enabled (%d, %d), and the second's play position is %u.%03X, at the hold %u.%03X",
			ch[0].enabled, ch[1].enabled, ch[1].ca, ch[1].caFraction, held_cl.ch[1].ca, held_cl.ch[1].caFraction);

		// the interrupt update of the held bursts: the timer's request raised and latched
		const bool f = (Arm(RN_CPSR) & 0x40) != 0;
		const u32 pend = StoredWord(state, INTR_PEND);

		Check(ScpuField(state, kScpuInterr, 1) == 1 && ScpuField(state, kScpuRegL, 4) == 2 && ScpuField(state, kScpuE68kM, 4) == 1
			&& ScpuField(state, kScpuE68kL, 4) == 2 && ScpuField(state, kScpuOut, 1) == 1,
			"the interrupt update ran in the held bursts: the request of timer A is raised and latched for the held ARM7, aica_interr %u with aica_reg_L %u, "
			"e68k_reg_M %u, e68k_reg_L %u, e68k_out %u", ScpuField(state, kScpuInterr, 1), ScpuField(state, kScpuRegL, 4), ScpuField(state, kScpuE68kM, 4),
			ScpuField(state, kScpuE68kL, 4), ScpuField(state, kScpuOut, 1));
		Check(pend == (f ? 0u : 1u), "INTR_PEND is e68k_out && armFiqEnable: %u, the held ARM7's F is %s (mode %02X, the PC %08X)", pend, f ? "set" : "clear",
			ArmMode(), ArmPc());
	}

	CO_END(held_cl.co);
}

/*
	arm7.bp.hit: a breakpoint on the loop of the ARM7 stops in the next burst, in front of its instruction, and
	the SH4 executes nothing behind it
*/
namespace {

struct {
	Co co;
	Counts c;
	u32 stopId;
	u32 passes;
	u32 checks;
	u64 time;
} bp_hit;

}

BOUNDED_GUEST_TEST(arm7_bp_hit, "arm7.bp.hit")
{
	Counts c;
	std::vector<Seen> list;

	CO_BEGIN(bp_hit.co);

	while (!Settle(stop))
		CO_YIELD(bp_hit.co);

	bp_hit.c = ReadCounts();
	bp_hit.stopId = stop.stopId;
	bp_hit.checks = arm7::test::checks;

	ArmBps({ { 7, kMain } });
	Check(dbg_arm7_armed != 0, "an active listed breakpoint of the ARM7 arms the debug loop: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);

	TakeSeen();
	HandOn(HAND_WATCH);

	if (!Resume())
		break;
	CO_YIELD(bp_hit.co);

	HandOff();
	c = ReadCounts();
	list = TakeSeen();

	Check(stop.lifetime == Lifetime::Parked && stop.cause == StopCause::Breakpoint && stop.breakpointId == 7 && stop.stopId == bp_hit.stopId + 1,
		"the breakpoint on a_main_loop stops: \"%s\", id %u, the stop counter advanced by %u", StopCauseName(stop.cause), stop.breakpointId, stop.stopId - bp_hit.stopId);
	Check(ArmPc() == kMain && Suspended() == SOUND_BURST_ARM7, "the burst is suspended in front of the instruction: the ARM7 stands at %08X, burst %u", ArmPc(),
		Suspended());
	Check(c.n[0] == bp_hit.c.n[0] + 1 && c.n[3] == bp_hit.c.n[3], "within one burst of the resume: %u started since, and its tail has not run (%u)",
		c.n[0] - bp_hit.c.n[0], c.n[3] - bp_hit.c.n[3]);
	Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Breakpoint } }), "published: %s", SeenText(list).c_str());
	Check(g_stop_latched && g_parked.load() && !Ctx().CpuRunning, "the stop is latched and the cpu stopped");

	// The hand runs in every slice, behind the AICA's callback: one slice saw the burst suspended, the one of
	// its tick, and no slice came behind it
	Check(hand.suspended == 1 && hand.latched == 0, "no emulated time passed behind the suspension: %u slices saw the burst suspended, %u ran under the latch",
		hand.suspended, hand.latched);
	Check(arm7::test::checks != bp_hit.checks, "the debug loop made its checks (%u)", arm7::test::checks - bp_hit.checks);

	// the resume executes the instruction of the stop, and the breakpoint stops at the next arrival
	bp_hit.passes = Arm(3);
	bp_hit.c = c;
	bp_hit.stopId = stop.stopId;
	bp_hit.time = sh4_sched_now64();

	if (!Resume())
		break;
	CO_YIELD(bp_hit.co);

	c = ReadCounts();
	list = TakeSeen();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 7 && ArmPc() == kMain && Arm(3) == bp_hit.passes + 1,
		"a resume from the breakpoint runs one pass of the loop and stops in front of the next: \"%s\", the ARM7 at %08X, %u passes more", StopCauseName(stop.cause),
		ArmPc(), Arm(3) - bp_hit.passes);
	Check(c.n[0] == bp_hit.c.n[0] && sh4_sched_now64() == bp_hit.time && stop.stopId == bp_hit.stopId + 1,
		"in the same burst, with no emulated time between the two stops; the stop counter advanced by %u", stop.stopId - bp_hit.stopId);
	Check(SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } }), "the resume published no Running, the burst was run on in the park context: %s",
		SeenText(list).c_str());

	ArmBps({ });

	CO_END(bp_hit.co);
}

/*
	arm7.bp.context: the debug loop leaves the context of the ARM7 as the interpreter's own loop leaves it. Every
	word of that context is in the save state, and so in the sound line and in the state hash; two of them are
	scratch that only an instruction boundary inside the FIQ handler shows, and no tick of the guest falls there
	(its bursts end in the loop).

	The CPSR word is stored when the core composes it, and is stale in between. The entry of a FIQ composes it
	in the mode switch and masks I and F behind that: through the handler the stored word has F as the
	interrupted loop had it, clear, while the core has it set, which is what the debugger reads. A debug loop
	that composed the CPSR anywhere would store the word of the debugger's read
*/
namespace {

struct {
	Co co;
} bp_ctx;

}

BOUNDED_GUEST_TEST(arm7_bp_context, "arm7.bp.context")
{
	std::vector<u8> state;

	CO_BEGIN(bp_ctx.co);

	while (!Settle(stop))
		CO_YIELD(bp_ctx.co);

	// the first instruction of the handler: behind the entry and the branch of the vector
	while (!SuspendAt(stop, kFiq))
		CO_YIELD(bp_ctx.co);

	state = ScpuState();

	Check(state.size() == kScpuStateSize, "the save state of the sound CPU has the size of its %u words and its ten fields: %zu bytes",
		(u32)RN_ARM_REG_COUNT, state.size());
	Check(ArmMode() == 0x11 && (Arm(RN_CPSR) & 0xC0) == 0xC0 && StoredWord(state, RN_CPSR) == (Arm(RN_CPSR) & ~0x40u),
		"inside the FIQ handler the stored CPSR word is the one the entry composed, with F as the loop had it: %08X, where the core has %08X. The debug loop "
		"composed none", StoredWord(state, RN_CPSR), Arm(RN_CPSR));
	Check(StoredWord(state, 15) == kFiq + 4, "R15 is what the branch of the vector left: %08X", StoredWord(state, 15));
	Check(StoredWord(state, R15_ARM_NEXT) == kFiq && StoredWord(state, INTR_PEND) == 0 && StoredWord(state, CYCL_CNT) == 0,
		"the address of the next instruction is the PC of the stop; no FIQ is pending and enabled; the tick remainder of the recompiler, which the "
		"interpreter never writes, is %u", StoredWord(state, CYCL_CNT));

	// the suspended burst keeps nothing in the context: where it stands is the machine's
	Check(ScpuState() == state && ReadArm().r[15] == kFiq, "and the debugger's reads leave the save state as it is");

	ArmBps({ });

	CO_END(bp_ctx.co);
}

/*
	arm7.bp.lowestid: of several breakpoints on one instruction the lowest id is published
*/
namespace {

struct {
	Co co;
} bp_lowest;

}

BOUNDED_GUEST_TEST(arm7_bp_lowestid, "arm7.bp.lowestid")
{
	CO_BEGIN(bp_lowest.co);

	while (!Settle(stop))
		CO_YIELD(bp_lowest.co);

	// three on the instruction, one of them through a mirror of the wave memory, and one elsewhere
	ArmBps({ { 9, kMain }, { 4, kMain + sh4_cpu->aica_ram.size }, { 6, kMain }, { 2, kNever } });

	if (!Resume())
		break;
	CO_YIELD(bp_lowest.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 4 && ArmPc() == kMain,
		"breakpoints 9, 4 and 6 are on a_main_loop: the stop has id %u (\"%s\", the ARM7 at %08X)", stop.breakpointId, StopCauseName(stop.cause), ArmPc());

	ArmBps({ });

	CO_END(bp_lowest.co);
}

/*
	arm7.bp.mask: a listed breakpoint whose category is not active stops nothing and arms nothing
*/
namespace {

struct {
	Co co;
	u32 checks;
	Counts c;
} bp_mask;

}

BOUNDED_GUEST_TEST(arm7_bp_mask, "arm7.bp.mask")
{
	Counts c;

	CO_BEGIN(bp_mask.co);

	while (!Settle(stop))
		CO_YIELD(bp_mask.co);

	emu::SetActiveMask(kMaskAll & ~CategoryBit(Category::Arm7));
	ArmBps({ { 3, kMain } });

	Check(dbg_arm7_armed == 0, "a breakpoint is listed on a_main_loop and its category is not active: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);

	bp_mask.checks = arm7::test::checks;
	bp_mask.c = ReadCounts();

	while (!PauseAfter(stop, 4))
		CO_YIELD(bp_mask.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - bp_mask.c.n[0] >= 4 && c.n[1] - bp_mask.c.n[1] == 32 * (c.n[0] - bp_mask.c.n[0]),
		"%u bursts ran through it, the next stop is the test's own: \"%s\"", c.n[0] - bp_mask.c.n[0], StopCauseName(stop.cause));
	Check(arm7::test::checks == bp_mask.checks, "and no check was made: the ARM phases ran on the backend (%u checks)", arm7::test::checks - bp_mask.checks);

	// the category becomes active
	emu::SetActiveMask(kMaskAll);
	Check(dbg_arm7_armed != 0, "with the category active the breakpoint arms the debug loop: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);

	bp_mask.c = ReadCounts();

	if (!Resume())
		break;
	CO_YIELD(bp_mask.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 3 && ArmPc() == kMain && c.n[0] == bp_mask.c.n[0] + 1,
		"and stops in the next burst: \"%s\", id %u", StopCauseName(stop.cause), stop.breakpointId);

	// muted at a stop inside a burst: nothing is armed any more
	emu::SetActiveMask(0);
	Check(dbg_arm7_armed == 0, "the mask cleared at the stop: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);
	emu::SetActiveMask(kMaskAll);

	ArmBps({ });
	Check(dbg_arm7_armed == 0, "no breakpoint listed: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);

	CO_END(bp_mask.co);
}

/*
	arm7.bp.mirror: breakpoint addresses are compared under the mask of the wave memory, with bits 0 and 1
	dropped; the PC of a stop is reported as it is
*/
namespace {

struct {
	Co co;
} bp_mirror;

}

BOUNDED_GUEST_TEST(arm7_bp_mirror, "arm7.bp.mirror")
{
	const u32 size = sh4_cpu->aica_ram.size;

	CO_BEGIN(bp_mirror.co);

	while (!Settle(stop))
		CO_YIELD(bp_mirror.co);

	Check(arm7::MaskAddr(kMain + size) == kMain && arm7::MaskAddr(kMain + 3) == kMain && arm7::MaskAddr(0x00FFFFFC) == ((size - 1) & ~3u)
		&& arm7::MaskAddr(kMain) != arm7::MaskAddr(kMain + 4),
		"the address rule: the key of an address is the address under the mask of the wave memory (%08X) with bits 0 and 1 clear", size - 1);

	// the highest mirror a breakpoint address can name
	ArmBps({ { 5, (0x01000000 - size) + kMain } });

	if (!Resume())
		break;
	CO_YIELD(bp_mirror.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 5 && ArmPc() == kMain,
		"a breakpoint on %08X, a mirror of a_main_loop, stops there; the PC of the stop is the PC as it is, %08X", (0x01000000 - size) + kMain, ArmPc());

	ArmBps({ });

	CO_END(bp_mirror.co);
}

/*
	arm7.bp.fiqvector: the check is made behind the entry of a pending FIQ. A breakpoint on the vector is hit,
	and a stop anywhere else is in front of an instruction that executes next, with no handler in between
*/
namespace {

struct {
	Co co;
	u32 n;
	u32 vector, vectorGood;		// stops on the vector, and those of them that are an entry as it has to be
	u32 loop, loopGood;			// stops on a_main_loop, and the steps from them that executed its instruction
	Counts c;
	bool stepping;
} bp_fiq;

}

BOUNDED_GUEST_TEST(arm7_bp_fiqvector, "arm7.bp.fiqvector")
{
	CO_BEGIN(bp_fiq.co);

	while (!Settle(stop))
		CO_YIELD(bp_fiq.co);

	bp_fiq.vector = bp_fiq.vectorGood = bp_fiq.loop = bp_fiq.loopGood = 0;
	bp_fiq.c = ReadCounts();

	ArmBps({ { 1, kVector }, { 2, kMain } });

	for (bp_fiq.n = 0; bp_fiq.n < 2000 && (bp_fiq.vector < 20 || bp_fiq.loop < 20); bp_fiq.n++)
	{
		// (the loop is hit eighty times a burst and the vector twice or three times: once the loop has its
		// stops, the breakpoint on it goes)
		if (bp_fiq.loop == 20)
			ArmBps({ { 1, kVector } });

		if (!Resume())
			break;
		CO_YIELD(bp_fiq.co);

		if (stop.cause != StopCause::Breakpoint)
			break;

		if (stop.breakpointId == 1)
		{
			// The entry: FIQ mode with I and F set, the return address in R14, the interrupted CPSR saved. The
			// guest's loop runs in SVC mode with I set
			bp_fiq.vector++;

			if (ArmPc() == kVector && (Arm(RN_CPSR) & 0xDF) == 0xD1 && (Arm(RN_SPSR) & 0x1F) == 0x13 && Arm(14) >= kMain + 4 && Arm(14) <= kLeaf + 16)
				bp_fiq.vectorGood++;

			continue;
		}

		// On the loop: a request that was pending and let in would have been entered in front of this check. So
		// the step from here executes the instruction of the stop, and is not complete at the vector
		bp_fiq.loop++;

		if (!StepArm())
			break;
		CO_YIELD(bp_fiq.co);

		if (stop.cause == StopCause::Step && ArmPc() == kMain + 4)
			bp_fiq.loopGood++;
	}

	Check(bp_fiq.vector >= 20 && bp_fiq.vectorGood == bp_fiq.vector,
		"a breakpoint on the FIQ vector is hit: %u stops there, %u of them in FIQ mode with I and F set, the return address of the loop in R14 and its CPSR saved",
		bp_fiq.vector, bp_fiq.vectorGood);
	Check(bp_fiq.loop >= 20 && bp_fiq.loopGood == bp_fiq.loop,
		"a stop on a_main_loop is not followed by a detour through the handler: of %u stops there, the step executed the instruction of the stop %u times",
		bp_fiq.loop, bp_fiq.loopGood);
	Note("%u stops in %u bursts", bp_fiq.n, ReadCounts().n[0] - bp_fiq.c.n[0]);

	ArmBps({ });

	CO_END(bp_fiq.co);
}

/*
	arm7.bp.resume: the resume exemption. It lets the instruction of a stop execute once, and it is there only
	for a stop that the ARM7 decided, at the PC of that stop, with the ARM7 clocked and a check to come; the
	first check that is made ends it
*/
namespace {

struct {
	Co co;
	u32 passes;
	u32 tries, n;
	bool done;
	ArmRegs regs;
	Counts c;
	u32 pc;
	std::atomic<bool> calling { false };
	std::atomic<int> rv { 0 };
	std::atomic<bool> onEmu { false };
	u32 sh4pc;
	u32 lr, spsr;
} bp_res;

}

BOUNDED_GUEST_TEST(arm7_bp_resume, "arm7.bp.resume")
{
	ArmRegs regs;
	Counts c;
	u32 differs;

	CO_BEGIN(bp_res.co);

	while (!Settle(stop))
		CO_YIELD(bp_res.co);

	/*
		Once: the instruction of the stop executes, and the next breakpoint stops
	*/
	while (!SuspendAt(stop, kMain))
		CO_YIELD(bp_res.co);

	bp_res.passes = Arm(3);
	ArmBps({ { kAtId, kMain }, { 11, kMain + 4 } });

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 11 && ArmPc() == kMain + 4 && Arm(3) == bp_res.passes + 1,
		"a resume from a breakpoint executes its instruction, and the breakpoint on the next one stops: id %u at %08X, the instruction counted %u",
		stop.breakpointId, ArmPc(), Arm(3) - bp_res.passes);

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && ArmPc() == kMain && Arm(3) == bp_res.passes + 1,
		"and once only: the next arrival on a_main_loop stops, with nothing of it executed (id %u at %08X, %u passes)", stop.breakpointId, ArmPc(),
		Arm(3) - bp_res.passes);

	/*
		The PC written. Onto another breakpoint: it stops there with nothing executed. Away and back onto the
		address of the stop: the resume runs through the breakpoint there
	*/
	bp_res.passes = Arm(3);
	Check(SetArm(15, kMain + 4), "the PC of the ARM7 is written onto the breakpoint behind the one of the stop");

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 11 && ArmPc() == kMain + 4 && Arm(3) == bp_res.passes,
		"a PC written onto another breakpoint stops there, and nothing has executed: id %u at %08X, the count moved by %u", stop.breakpointId, ArmPc(),
		Arm(3) - bp_res.passes);

	Check(SetArm(15, kMain) && SetArm(15, kMain + 4), "the PC is written away and back onto the address of the stop");

	if (!Resume())
		break;
	CO_YIELD(bp_res.co);

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && ArmPc() == kMain && Arm(3) == bp_res.passes,
		"a PC written back onto the address of the stop resumes through the breakpoint there: the next stop is that of the breakpoint in front of the next pass "
		"(id %u at %08X)", stop.breakpointId, ArmPc());

	/*
		A breakpoint added behind the leave and in front of the next burst. The stop is that of a step that was
		complete at the end of its burst: the ARM7 decided it, nothing is listed, no burst is suspended, and the
		ARM7 stands on some instruction P. A thread queues the call that lists a breakpoint on P, and the resume
		is armed while that call waits: the park loop leaves first, the call is served at the first slice end, and
		the next burst stops in front of P with every register as the step left it. An exemption that had been
		armed at the leave, and had outlived the change of dbg_arm7_armed, would run through P
	*/
	ArmBps({ });
	bp_res.done = false;

	for (bp_res.tries = 0; bp_res.tries < 12 && !bp_res.done; bp_res.tries++)
	{
		while (!SuspendAt(stop, kMain))
			CO_YIELD(bp_res.co);

		ArmBps({ });

		while (!StepToBurstEnd(stop))
			CO_YIELD(bp_res.co);

		if (stop.cause != StopCause::Step || Suspended() != SOUND_BURST_NONE || dbg_arm7_armed != 0)
		{
			Check(false, "a step that is complete at the end of its burst (\"%s\", burst %u, dbg_arm7_armed %u)", StopCauseName(stop.cause), Suspended(),
				(u32)dbg_arm7_armed);
			break;
		}

		bp_res.regs = ReadArm();
		bp_res.pc = ArmPc();
		bp_res.c = ReadCounts();
		bp_res.calling = false;

		OtherStart([] {
			u32 pc = bp_res.pc;

			bp_res.calling = true;
			bp_res.rv = emu::Call(emu::CC_TABLE, [pc] {
				bp_res.onEmu = emu::OnEmuThread() && emu::GetRunState().lifetime == Lifetime::Running;
				ArmBps({ { 12, pc } });
			});
		});

		// until the call is in the queue
		for (Clock::time_point t0 = Clock::now(); !(g_attn.load() & ATTN_CALL) && MsSince(t0) < kHoldMs; )
			std::this_thread::yield();

		TakeSeen();

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		OtherJoin();

		regs = ReadArm();
		c = ReadCounts();
		differs = ArmDiffers(regs, bp_res.regs);

		if (stop.cause != StopCause::Breakpoint || stop.breakpointId != 12 || bp_res.rv != 0 || !bp_res.onEmu || c.n[0] != bp_res.c.n[0] + 1)
		{
			Check(false, "the breakpoint that was added behind the leave stops in the next burst (\"%s\" id %u, the call returned %d, %u bursts)",
				StopCauseName(stop.cause), stop.breakpointId, bp_res.rv.load(), c.n[0] - bp_res.c.n[0]);
			break;
		}

		// A FIQ that was entered in front of P has run its handler: the check on the vector ended an exemption
		// unused, and the case shows nothing. It is taken again
		if (differs != kArmRegs && regs.r[15] == bp_res.regs.r[15] && regs.r[R8_FIQ] != bp_res.regs.r[R8_FIQ])
		{
			ArmBps({ });
			continue;
		}

		Check(differs == kArmRegs,
			"a breakpoint that is listed behind the leave, by a call that waited, stops in front of the instruction the ARM7 stood on (%08X): every register is "
			"as the step left it (the first that differs: %u)", bp_res.pc, differs);
		bp_res.done = true;
	}

	Check(bp_res.done, "the case was taken with no FIQ in front of the instruction (%u tries)", bp_res.tries);
	ArmBps({ });

	/*
		The ARM7 held. A step that was complete at a burst with the ARM7 held, while it stood on address 0; then a
		breakpoint on address 0, the SH4 sent to release the ARM7, and a resume: the release stops on 0. An
		exemption armed for the held ARM7 would be for address 0, and would run through.
		The ARM7 comes to stand on 0 by a release that is held again before a burst ran
	*/
	bp_res.done = false;

	for (bp_res.tries = 0; bp_res.tries < 12 && !bp_res.done; bp_res.tries++)
	{
		// held
		GotoSh4(DSPTEST_G_HOLD_ARM7);
		Sh4Bps({ { 20, DSPTEST_G_HOLD_ARM7_SPIN } });

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		// released, onto its reset vector
		GotoSh4(DSPTEST_G_RELEASE_ARM7);
		Sh4Bps({ { 21, DSPTEST_G_MAIN_LOOP } });

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		Sh4Bps({ });

		if (stop.cause != StopCause::Breakpoint || stop.breakpointId != 21 || !ArmClocked() || ArmPc() != 0)
			continue;

		// held again, and a step: the burst that comes finds the ARM7 held
		GotoSh4(DSPTEST_G_HOLD_ARM7);

		if (!StepArm())
			break;
		CO_YIELD(bp_res.co);

		if (stop.cause != StopCause::Step || ArmClocked() || ArmPc() != 0)
			continue;

		Check(arm7::test::GetState().stopByCheck, "a step was complete at a burst with the ARM7 held, the ARM7 standing on address 0: the stop is the ARM7's");

		ArmBps({ { 13, 0 } });
		GotoSh4(DSPTEST_G_RELEASE_ARM7);
		bp_res.c = ReadCounts();

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		c = ReadCounts();

		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 13 && ArmPc() == 0 && ArmClocked() && c.n[1] == bp_res.c.n[1],
			"with a breakpoint on address 0, the release of the ARM7 stops there with nothing executed: \"%s\" id %u, the ARM7 at %08X, %u ARM phases since",
			StopCauseName(stop.cause), stop.breakpointId, ArmPc(), c.n[1] - bp_res.c.n[1]);
		bp_res.done = true;
	}

	Check(bp_res.done, "the case was taken with the ARM7 held on address 0 (%u tries)", bp_res.tries);
	ArmBps({ });
	Sh4Bps({ });

	/*
		A FIQ entered in front of the exempt instruction, with a breakpoint on the vector: it stops at the
		vector, and the instruction of the first stop has not executed when the handler comes back to it.
		The stop is inside the F window of the loop with a request waiting, and the test clears F there
	*/
	while (!Settle(stop))
		CO_YIELD(bp_res.co);

	bp_res.done = false;

	for (bp_res.n = 0; bp_res.n < 4000 && !bp_res.done; bp_res.n++)
	{
		while (!SuspendAt(stop, kLastOfWindow))
			CO_YIELD(bp_res.co);

		if (!ArmInService() || !(Arm(RN_CPSR) & 0x40))
			continue;

		// the request is let in: the entry is made in front of the next instruction
		Check(SetArm(RN_CPSR, Arm(RN_CPSR) & ~0x40u), "inside the F window with a request of the AICA waiting, the debugger clears F");

		bp_res.regs = ReadArm();
		ArmBps({ { kAtId, kLastOfWindow }, { 14, kVector } });

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 14 && ArmPc() == kVector && Arm(14) == kLastOfWindow + 4,
			"the resume enters the FIQ in front of the instruction of the stop, and the breakpoint on the vector stops: \"%s\" id %u at %08X, R14 %08X",
			StopCauseName(stop.cause), stop.breakpointId, ArmPc(), Arm(14));

		ArmBps({ { kAtId, kLastOfWindow } });

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		// the handler has run; the registers of the loop are those of the first stop
		regs = ReadArm();
		differs = kArmRegs;

		for (u32 i = 0; i < 8; i++)
		{
			if (regs.r[i] != bp_res.regs.r[i] && differs == kArmRegs)
				differs = i;
		}

		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && ArmPc() == kLastOfWindow && differs == kArmRegs,
			"the handler returns to the instruction, which has not executed, and its breakpoint stops: the exemption ended unused at the vector (id %u at %08X, "
			"R0 to R7 as they were: %s)", stop.breakpointId, ArmPc(), differs == kArmRegs ? "yes" : "no");
		bp_res.done = true;
	}

	Check(bp_res.done, "a stop inside the F window with a request waiting was found (%u stops)", bp_res.n);
	ArmBps({ });

	/*
		The same with no breakpoint on the vector. The first check behind the resume is the one on the vector: it
		stops nothing, and it ends the exemption all the same, unused. When the handler comes back to the
		instruction of the stop, its breakpoint stops, and the instruction has not executed. An exemption that
		only a check at its own PC ended would wait through the handler and run through the instruction then
	*/
	bp_res.done = false;

	for (bp_res.n = 0; bp_res.n < 4000 && !bp_res.done; bp_res.n++)
	{
		while (!SuspendAt(stop, kLastOfWindow))
			CO_YIELD(bp_res.co);

		if (!ArmInService() || !(Arm(RN_CPSR) & 0x40))
			continue;

		Check(SetArm(RN_CPSR, Arm(RN_CPSR) & ~0x40u), "again inside the F window with a request waiting, the debugger clears F; no breakpoint is on the vector");

		bp_res.regs = ReadArm();
		bp_res.c = ReadCounts();

		if (!Resume())
			break;
		CO_YIELD(bp_res.co);

		regs = ReadArm();
		c = ReadCounts();
		differs = kArmRegs;

		for (u32 i = 0; i < 8; i++)
		{
			if (regs.r[i] != bp_res.regs.r[i] && differs == kArmRegs)
				differs = i;
		}

		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && ArmPc() == kLastOfWindow && differs == kArmRegs
			&& regs.r[R8_FIQ] == bp_res.regs.r[R8_FIQ] + 1 && c.n[0] == bp_res.c.n[0],
			"the FIQ is entered in front of the instruction, its handler runs once (%u), and the breakpoint stops when it comes back to the instruction, which "
			"has not executed: the check on the vector ended the exemption (id %u at %08X, R0 to R7 as they were: %s)", regs.r[R8_FIQ] - bp_res.regs.r[R8_FIQ],
			stop.breakpointId, ArmPc(), differs == kArmRegs ? "yes" : "no");
		bp_res.done = true;
	}

	Check(bp_res.done, "a second stop inside the F window with a request waiting was found (%u stops)", bp_res.n);
	ArmBps({ });

	CO_END(bp_res.co);
}

/*
	arm7.bp.completion: a resume, and a step of the SH4, whose burst hits a second breakpoint of the ARM7: the
	thread stays parked, the stop is published with a new stop counter and no Running, and the command of the
	SH4 is dropped
*/
namespace {

struct {
	Co co;
	u32 stopId;
	u32 sh4pc;
	u64 time;
	u32 r8;
} bp_comp;

}

BOUNDED_GUEST_TEST(arm7_bp_completion, "arm7.bp.completion")
{
	std::vector<Seen> list;

	CO_BEGIN(bp_comp.co);

	while (!Settle(stop))
		CO_YIELD(bp_comp.co);

	while (!SuspendAt(stop, kMain))
		CO_YIELD(bp_comp.co);

	ArmBps({ { kAtId, kMain }, { 31, kMain + 4 }, { 32, kPlain } });

	bp_comp.stopId = stop.stopId;
	bp_comp.sh4pc = Ctx().pc;
	bp_comp.time = sh4_sched_now64();
	bp_comp.r8 = Ctx().r[8];
	TakeSeen();

	if (!Resume())
		break;
	CO_YIELD(bp_comp.co);

	list = TakeSeen();

	Check(stop.lifetime == Lifetime::Parked && stop.cause == StopCause::Breakpoint && stop.breakpointId == 31 && ArmPc() == kMain + 4
		&& Suspended() == SOUND_BURST_ARM7, "a resume whose burst hits a second breakpoint of the ARM7 stays parked on it: id %u at %08X", stop.breakpointId, ArmPc());
	Check(stop.stopId == bp_comp.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } }),
		"a new stop counter (+%u) and no Running: published %s", stop.stopId - bp_comp.stopId, SeenText(list).c_str());
	Check(Ctx().pc == bp_comp.sh4pc && sh4_sched_now64() == bp_comp.time && Ctx().r[8] == bp_comp.r8 && g_stop_latched && g_parked.load(),
		"the SH4 has executed nothing and no time has passed: the latch is still set");

	// a step of the SH4 from here
	bp_comp.stopId = stop.stopId;

	{
		const char* why = "";
		int rv = emu::Step(Target::Sh4, StepKind::Into, &why);

		if (rv != 0)
		{
			Note("this build does not step the SH4 here (%d %s): the half of the SH4 step is not run", rv, why ? why : "");
			ArmBps({ });
			break;
		}
	}
	CO_YIELD(bp_comp.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 32 && ArmPc() == kPlain && stop.stopId == bp_comp.stopId + 1
		&& SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } }),
		"a step of the SH4 runs the burst on first, and the breakpoint of the ARM7 that suspends it again is the stop: \"%s\" id %u, published %s",
		StopCauseName(stop.cause), stop.breakpointId, SeenText(list).c_str());
	Check(Ctx().pc == bp_comp.sh4pc && sh4_sched_now64() == bp_comp.time && Ctx().r[8] == bp_comp.r8,
		"the step of the SH4 is dropped: its PC is %08X as before, and no instruction of it has executed", Ctx().pc);

	ArmBps({ });

	CO_END(bp_comp.co);
}

/*
	arm7.bp.insleep: a breakpoint of the ARM7 while the SH4 waits in SLEEP is a freezing cause there too: the
	stop is latched in the slice of its burst, and the SLEEP is executed again behind the resume
*/
namespace {

struct {
	Co co;
	Counts c;
	u32 sleepAt;
} bp_sleep;

}

BOUNDED_GUEST_TEST(arm7_bp_insleep, "arm7.bp.insleep")
{
	Counts c;

	CO_BEGIN(bp_sleep.co);

	while (!Settle(stop))
		CO_YIELD(bp_sleep.co);

	// the SLEEP of g_sleep_loop: behind the two instructions that open the interrupts
	bp_sleep.sleepAt = DSPTEST_G_SLEEP_LOOP + 4;
	Check(ReadMem16(bp_sleep.sleepAt) == 0x001B, "the guest has its SLEEP at %08X", bp_sleep.sleepAt);

	GotoSh4(DSPTEST_G_SLEEP_LOOP);
	bp_sleep.c = ReadCounts();

	while (!SuspendAt(stop, kMain))
		CO_YIELD(bp_sleep.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && Suspended() == SOUND_BURST_ARM7 && c.n[0] == bp_sleep.c.n[0] + 1,
		"the breakpoint of the ARM7 stops in the burst that comes while the SH4 waits in SLEEP");
	Check(Ctx().pc == bp_sleep.sleepAt, "the SH4 stands on its SLEEP, which the latch cut short: pc %08X", Ctx().pc);

	ArmBps({ });

	while (!PauseAfter(stop, 4))
		CO_YIELD(bp_sleep.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - bp_sleep.c.n[0] >= 5 && c.n[3] - bp_sleep.c.n[3] == c.n[0] - bp_sleep.c.n[0]
		&& Suspended() == SOUND_BURST_NONE, "the resume runs the burst to its end and the machine runs on: %u bursts since, each with its tail",
		c.n[0] - bp_sleep.c.n[0]);
	Check((Ctx().pc >= DSPTEST_G_SLEEP_LOOP && Ctx().pc < DSPTEST_G_HOLD_ARM7) || (Ctx().pc >= DSPTEST_G_IRQ && Ctx().pc < DSPTEST_G_TABLE),
		"the SH4 is in its sleep loop, or in the handler of the interrupt that woke it: pc %08X", Ctx().pc);

	CO_END(bp_sleep.co);
}

/*
	arm7.step.suspended: a step of the ARM7 with a burst suspended is made in the park context
*/
namespace {

struct {
	Co co;
	u32 stopId, sh4pc, checks, n, good;
	u64 time;
	Counts c;
	ArmRegs regs;
} st_susp;

}

BOUNDED_GUEST_TEST(arm7_step_suspended, "arm7.step.suspended")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_susp.co);

	while (!Settle(stop))
		CO_YIELD(st_susp.co);

	// inside the F window: no FIQ is entered in front of this instruction or behind it
	while (!SuspendAt(stop, kPlain))
		CO_YIELD(st_susp.co);

	ArmBps({ });
	Check(dbg_arm7_armed == 0, "nothing of the ARM7 is armed at the stop: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);

	st_susp.stopId = stop.stopId;
	st_susp.sh4pc = Ctx().pc;
	st_susp.time = sh4_sched_now64();
	st_susp.checks = arm7::test::checks;
	st_susp.c = ReadCounts();
	st_susp.regs = ReadArm();
	TakeSeen();

	if (!StepArm())
		break;

	Check(emu::Step(Target::Arm7, StepKind::Into) == kErrNotPaused && emu::Resume() == kErrNotPaused, "with the step armed, a second action is refused");
	CO_YIELD(st_susp.co);

	list = TakeSeen();
	c = ReadCounts();

	Check(stop.lifetime == Lifetime::Parked && stop.cause == StopCause::Step && stop.breakpointId == 0 && ArmPc() == kPlain + 4,
		"a step of the ARM7 from %08X: \"%s\", the ARM7 at %08X", kPlain, StopCauseName(stop.cause), ArmPc());
	Check(stop.stopId == st_susp.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }), "a new stop counter (+%u) and no Running: published %s",
		stop.stopId - st_susp.stopId, SeenText(list).c_str());
	Check(sh4_sched_now64() == st_susp.time && Ctx().pc == st_susp.sh4pc && g_stop_latched && g_parked.load(),
		"the scheduler's time and the PC of the SH4 are unchanged, the latch is set");
	Check(Suspended() == SOUND_BURST_ARM7 && c.n[0] == st_susp.c.n[0] && c.n[1] == st_susp.c.n[1] && c.n[3] == st_susp.c.n[3],
		"the burst is suspended again, in the same ARM phase");
	Check(arm7::test::checks == st_susp.checks + 2, "two checks were made: the one in front of the instruction, which let it execute, and the one behind it (%u)",
		arm7::test::checks - st_susp.checks);
	Check(Arm(0) == AicaReg16(kTIMA) && dbg_arm7_armed == 0 && !arm7::test::GetState().step,
		"the instruction executed (R0 has the timer's count, %04X), and the step is armed no more", Arm(0));

	// many of them, through the loop: each ends in front of the instruction behind it or on a branch target
	st_susp.good = 0;

	for (st_susp.n = 0; st_susp.n < 500; st_susp.n++)
	{
		st_susp.regs = ReadArm();
		st_susp.stopId = stop.stopId;

		if (!StepArm())
			break;
		CO_YIELD(st_susp.co);

		if (stop.cause == StopCause::Step && stop.stopId == st_susp.stopId + 1 && sh4_sched_now64() == st_susp.time && Ctx().pc == st_susp.sh4pc
			&& ArmPc() != st_susp.regs.r[15] && Suspended() == SOUND_BURST_ARM7)
			st_susp.good++;

		if (Suspended() == SOUND_BURST_NONE)
			break;
	}

	list = TakeSeen();

	Check(st_susp.good == st_susp.n && !SeenHas(list, Lifetime::Running, StopCause::NoCause),
		"%u more steps, each a stop with \"step\", a new stop counter and another PC, with no time passed, the SH4 where it was, and no Running (%u)",
		st_susp.n, st_susp.good);

	CO_END(st_susp.co);
}

/*
	arm7.step.sample: a step whose instruction is the last of its sample runs the time step of the AICA behind
	it, and the next phase up to its first check
*/
namespace {

struct {
	Co co;
	u32 n, across, good;
	Counts c;
	u32 timb, timc, pc, sh4pc;
	u64 time;
	u32 sawA;
} st_sample;

}

BOUNDED_GUEST_TEST(arm7_step_sample, "arm7.step.sample")
{
	Counts c;

	CO_BEGIN(st_sample.co);

	while (!Settle(stop))
		CO_YIELD(st_sample.co);

	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_sample.co);

	ArmBps({ });

	st_sample.across = st_sample.good = 0;
	st_sample.time = sh4_sched_now64();
	st_sample.sh4pc = Ctx().pc;

	for (st_sample.n = 0; st_sample.n < 2000 && st_sample.across < 8 && Suspended() == SOUND_BURST_ARM7; st_sample.n++)
	{
		st_sample.c = ReadCounts();
		st_sample.timb = AicaReg16(kTIMB) & 0xFF;
		st_sample.timc = AicaReg16(kTIMC) & 0xFF;

		if (!StepArm())
			break;
		CO_YIELD(st_sample.co);

		c = ReadCounts();

		if (c.n[2] == st_sample.c.n[2])
			continue;

		// The step went across a time step: one ARM phase ended, one time step was made, and the two timers the
		// guest leaves alone counted one sample
		st_sample.across++;

		if (stop.cause == StopCause::Step && c.n[1] == st_sample.c.n[1] + 1 && c.n[2] == st_sample.c.n[2] + 1 && c.n[0] == st_sample.c.n[0]
			&& (AicaReg16(kTIMB) & 0xFF) == ((st_sample.timb + 1) & 0xFF) && (AicaReg16(kTIMC) & 0xFF) == ((st_sample.timc + 1) & 0xFF)
			&& sh4_sched_now64() == st_sample.time && Ctx().pc == st_sample.sh4pc)
			st_sample.good++;
	}

	Check(st_sample.across >= 1 && st_sample.good == st_sample.across,
		"%u steps, %u of them across a time step of the AICA: at each of those one ARM phase ended, one time step was made, timers B and C counted one sample, "
		"and the stop is a step's with the scheduler's time and the SH4 where they were (%u)", st_sample.n, st_sample.across, st_sample.good);

	CO_END(st_sample.co);
}

/*
	arm7.step.burstend: a step whose instruction is the last of its burst is complete at the end of the burst:
	the tail has run once and nothing is suspended
*/
namespace {

struct {
	Co co;
	Counts c;
	u32 stopId;
	u64 time;
	u32 sh4pc;
} st_end;

}

BOUNDED_GUEST_TEST(arm7_step_burstend, "arm7.step.burstend")
{
	Counts c;
	std::vector<Seen> list;

	CO_BEGIN(st_end.co);

	while (!Settle(stop))
		CO_YIELD(st_end.co);

	st_end.c = ReadCounts();

	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_end.co);

	ArmBps({ });
	st_end.time = sh4_sched_now64();
	st_end.sh4pc = Ctx().pc;
	TakeSeen();

	while (!StepToBurstEnd(stop))
		CO_YIELD(st_end.co);

	c = ReadCounts();
	list = TakeSeen();

	Check(step_end.bad == 0 && stop.cause == StopCause::Step && Suspended() == SOUND_BURST_NONE,
		"%u steps to the end of the burst, each a stop with \"step\" and no time passed; behind the last one no burst is suspended", step_end.steps);
	Check(c.n[0] == st_end.c.n[0] + 1 && c.n[1] == st_end.c.n[1] + 32 && c.n[2] == st_end.c.n[2] + 32 && c.n[3] == st_end.c.n[3] + 1,
		"the burst that was stepped through is 32 ARM phases, 32 time steps and one tail: %u, %u, %u", c.n[1] - st_end.c.n[1], c.n[2] - st_end.c.n[2],
		c.n[3] - st_end.c.n[3]);
	Check(sh4_sched_now64() == st_end.time && Ctx().pc == st_end.sh4pc && !SeenHas(list, Lifetime::Running, StopCause::NoCause) && g_stop_latched,
		"all of it in the park context: the scheduler's time and the SH4 are where they were, nothing was published but the stops of the steps");
	Check(dbg_arm7_armed == 0 && arm7::test::GetState().stopByCheck, "the stop is the ARM7's, and nothing is armed");

	// the machine goes on from there: the next callback finds no burst suspended
	st_end.c = c;

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_end.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - st_end.c.n[0] >= 3 && c.n[1] - st_end.c.n[1] == 32 * (c.n[0] - st_end.c.n[0])
		&& c.n[3] - st_end.c.n[3] == c.n[0] - st_end.c.n[0], "a resume from there runs whole bursts again: %u, each with 32 ARM phases and its tail",
		c.n[0] - st_end.c.n[0]);

	CO_END(st_end.co);
}

/*
	arm7.step.frompause: a step asked for with no burst suspended. The park loop leaves without publishing, the
	SH4 runs to the next burst as a step that is executing, and the step is the first instruction of that burst
*/
namespace {

struct {
	Co co;
	u32 n, good, vector, quiet;
	u32 stopId, pc;
	u64 time;
	Counts c;
} st_pause;

}

BOUNDED_GUEST_TEST(arm7_step_frompause, "arm7.step.frompause")
{
	std::vector<Seen> list;
	Counts c;
	u64 passed;

	CO_BEGIN(st_pause.co);

	while (!Settle(stop))
		CO_YIELD(st_pause.co);

	st_pause.good = st_pause.vector = st_pause.quiet = 0;

	for (st_pause.n = 0; st_pause.n < 20; st_pause.n++)
	{
		while (!BehindBurst(stop))
			CO_YIELD(st_pause.co);

		st_pause.stopId = stop.stopId;
		st_pause.pc = ArmPc();
		st_pause.time = sh4_sched_now64();
		st_pause.c = ReadCounts();
		TakeSeen();
		HandOn(HAND_WATCH);

		if (!StepArm())
			break;
		CO_YIELD(st_pause.co);

		HandOff();
		list = TakeSeen();
		c = ReadCounts();
		passed = sh4_sched_now64() - st_pause.time;

		// one period of the AICA callback at most, and the rest of a slice
		if (stop.cause == StopCause::Step && stop.stopId == st_pause.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } })
			&& c.n[0] == st_pause.c.n[0] + 1 && Suspended() == SOUND_BURST_ARM7 && passed > 0 && passed <= kAicaPeriod + SH4_TIMESLICE
			&& ArmPc() != st_pause.pc)
			st_pause.good++;

		// complete at the vector with nothing executed: a FIQ was entered in front of the instruction
		if (ArmPc() == kVector && Arm(14) == st_pause.pc + 4)
			st_pause.vector++;

		// the hand ran in every slice of the wait: the stop stayed in force for the host and for a client
		if (hand.slices >= 1 && hand.notParked == 0 && hand.notPublishedParked == 0 && hand.latched == 0)
			st_pause.quiet++;
	}

	Check(st_pause.good == st_pause.n && st_pause.n == 20,
		"%u steps of the ARM7 from a stop with no burst suspended: each one stop with \"step\", a new stop counter, no Running, in the next burst, which is "
		"suspended behind its first instruction, with the SH4 run for one period of the AICA callback at most (%u)", st_pause.n, st_pause.good);
	Check(st_pause.quiet == st_pause.n,
		"while the SH4 ran towards the burst, g_parked stayed set and the published state stayed Parked, in every slice of %u of the waits", st_pause.quiet);
	Note("%u of them were complete at the FIQ vector with nothing executed", st_pause.vector);

	CO_END(st_pause.co);
}

/*
	arm7.step.fiq: a step in front of whose instruction a FIQ is entered is complete at the vector, with
	nothing executed. The stop the step is asked from is behind a burst whose last time step raised the request
*/
namespace {

struct {
	Co co;
	u32 n, found;
	u32 pc, stopId;
	ArmRegs regs;
} st_fiq;

}

BOUNDED_GUEST_TEST(arm7_step_fiq, "arm7.step.fiq")
{
	ArmRegs regs;
	u32 differs;

	CO_BEGIN(st_fiq.co);

	while (!Settle(stop))
		CO_YIELD(st_fiq.co);

	st_fiq.found = 0;

	for (st_fiq.n = 0; st_fiq.n < 400 && st_fiq.found < 3; st_fiq.n++)
	{
		while (!BehindBurst(stop))
			CO_YIELD(st_fiq.co);

		// a request is latched, the loop has F clear: the entry is made in front of the next instruction
		if (!ArmInService() || ArmMode() != 0x13 || (Arm(RN_CPSR) & 0x40))
			continue;

		st_fiq.pc = ArmPc();
		st_fiq.regs = ReadArm();
		st_fiq.stopId = stop.stopId;

		if (!StepArm())
			break;
		CO_YIELD(st_fiq.co);

		regs = ReadArm();
		differs = kArmRegs;

		// nothing of the loop executed: R0 to R7 and its R13 and R14 are as they were
		for (u32 i = 0; i < 8; i++)
		{
			if (regs.r[i] != st_fiq.regs.r[i] && differs == kArmRegs)
				differs = i;
		}

		Check(stop.cause == StopCause::Step && stop.stopId == st_fiq.stopId + 1 && ArmPc() == kVector && Arm(14) == st_fiq.pc + 4 && ArmMode() == 0x11
			&& differs == kArmRegs && regs.r[R13_SVC] == st_fiq.regs.r[13] && regs.r[R14_SVC] == st_fiq.regs.r[14],
			"a step from %08X with a request of the AICA waiting is complete at the FIQ vector: \"%s\", the ARM7 at %08X in mode %02X, R14 %08X, and nothing of "
			"the loop has executed", st_fiq.pc, StopCauseName(stop.cause), ArmPc(), ArmMode(), Arm(14));
		st_fiq.found++;
	}

	Check(st_fiq.found == 3, "three such stops were found in %u bursts", st_fiq.n);

	CO_END(st_fiq.co);
}

/*
	arm7.step.fiqbehind: a FIQ that becomes enterable inside a sample. The loop of the guest masks F over a part
	of each pass; a request that a time step raises while F is set is entered behind the msr that clears F. The
	breakpoint is on the instruction in front of that msr, and at each stop the test steps twice: onto the msr,
	and the msr. A pair counts only when it stayed inside one ARM phase, which the count of the ARM phases says:
	then the msr was stepped in a sample that had executed an instruction already, no time step fell into the
	step, and a FIQ at the end of it was entered by the loop that goes on with a suspended sample. A pair that
	does not count says nothing of that loop (its msr ended a sample, or began one) and is resumed
*/
namespace {

struct {
	Co co;
	u32 n;
	u32 counted, behind, vector, other;		// the counted pairs, and where they ended
	u32 uncounted;
	u32 vectorGood;
	u32 phases0, phases1;
	Counts c;
	u32 firstAt;							// the bursts until the first counted pair that ended on the vector
} st_fb;

}

BOUNDED_GUEST_TEST(arm7_step_fiqbehind, "arm7.step.fiqbehind")
{
	Counts c;

	CO_BEGIN(st_fb.co);

	while (!Settle(stop))
		CO_YIELD(st_fb.co);

	Check(ArmWord(kUnmask) == 0xE321F093 && ArmWord(kLastOfWindow) != 0xE321F093 && (ArmWord(DSPTEST_A_FWIN) & 0xFFFFFF00) == 0xE321F000,
		"the guest has the msr that ends its F window at %08X, a plain instruction in front of it", kUnmask);

	st_fb.counted = st_fb.behind = st_fb.vector = st_fb.other = st_fb.uncounted = st_fb.vectorGood = 0;
	st_fb.firstAt = 0;
	st_fb.c = ReadCounts();

	for (st_fb.n = 0; st_fb.n < 20000; st_fb.n++)
	{
		c = ReadCounts();

		// 64 bursts, and on until the vector was met five times
		if (c.n[0] - st_fb.c.n[0] >= 64 && (st_fb.vector == 0 || st_fb.vector >= 5))
			break;
		if (c.n[0] - st_fb.c.n[0] >= 400)
			break;

		while (!SuspendAt(stop, kLastOfWindow))
			CO_YIELD(st_fb.co);

		st_fb.phases0 = ReadCounts().n[1];

		// onto the msr
		if (!StepArm())
			break;
		CO_YIELD(st_fb.co);

		st_fb.phases1 = ReadCounts().n[1];

		if (stop.cause != StopCause::Step || ArmPc() != kUnmask)
		{
			// (a FIQ cannot be entered in front of the msr: F is set there)
			Check(false, "a step from the last instruction of the F window ends on the msr behind it (\"%s\", the ARM7 at %08X)", StopCauseName(stop.cause),
				ArmPc());
			break;
		}

		// the msr
		if (!StepArm())
			break;
		CO_YIELD(st_fb.co);

		c = ReadCounts();

		if (st_fb.phases0 != st_fb.phases1 || st_fb.phases1 != c.n[1])
		{
			st_fb.uncounted++;
			continue;
		}

		st_fb.counted++;

		if (ArmPc() == kUnmask + 4 && stop.cause == StopCause::Step)
		{
			st_fb.behind++;
		}
		else if (ArmPc() == kVector && stop.cause == StopCause::Step)
		{
			// The msr has executed: the status that the entry saved has F clear. The return address is that of
			// the instruction behind the msr
			st_fb.vector++;

			if (Arm(14) == kUnmask + 8 && (Arm(RN_SPSR) & 0xDF) == 0x93 && ArmMode() == 0x11)
				st_fb.vectorGood++;

			if (st_fb.firstAt == 0)
				st_fb.firstAt = c.n[0] - st_fb.c.n[0] + 1;
		}
		else
		{
			st_fb.other++;
		}
	}

	c = ReadCounts();

	Check(st_fb.counted != 0 && st_fb.other == 0,
		"%u pairs of steps stayed inside one ARM phase: %u ended on the line behind the msr and %u on the FIQ vector, none anywhere else (%u); %u pairs went "
		"across the end of a phase and are not counted", st_fb.counted, st_fb.behind, st_fb.vector, st_fb.other, st_fb.uncounted);
	Check(st_fb.vector >= 1 && st_fb.firstAt <= 64,
		"a step of the msr that ends the F window ended on the FIQ vector inside the sample that was gone on with, within 64 bursts: the first in burst %u, "
		"%u in %u bursts", st_fb.firstAt, st_fb.vector, c.n[0] - st_fb.c.n[0]);
	Check(st_fb.vectorGood == st_fb.vector,
		"at each of them the msr has executed and the FIQ was entered behind it: R14 is %08X, the saved status has F clear, the mode is FIQ (%u of %u)",
		kUnmask + 8, st_fb.vectorGood, st_fb.vector);

	ArmBps({ });

	CO_END(st_fb.co);
}

/*
	arm7.step.unclocked: the guest holds its ARM7 between the request of a step and the burst the step waits
	for. The burst completes the step with nothing executed: otherwise the machine would run on with a stop
	published and no call served
*/
namespace {

struct {
	Co co;
	u32 pc, stopId;
	Counts c;
	ArmRegs regs;
} st_uncl;

}

BOUNDED_GUEST_TEST(arm7_step_unclocked, "arm7.step.unclocked")
{
	Counts c;
	std::vector<Seen> list;
	ArmRegs regs;

	CO_BEGIN(st_uncl.co);

	while (!Settle(stop))
		CO_YIELD(st_uncl.co);

	st_uncl.pc = ArmPc();
	st_uncl.stopId = stop.stopId;
	st_uncl.c = ReadCounts();
	st_uncl.regs = ReadArm();
	TakeSeen();

	// the ARM7 is clocked when the step is asked for, and the SH4 holds it before the next burst
	GotoSh4(DSPTEST_G_HOLD_ARM7);

	if (!StepArm())
		break;
	CO_YIELD(st_uncl.co);

	c = ReadCounts();
	list = TakeSeen();
	regs = ReadArm();

	Check(stop.cause == StopCause::Step && stop.stopId == st_uncl.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
		"the burst that finds the ARM7 held completes the step: \"%s\", a new stop counter, published %s", StopCauseName(stop.cause), SeenText(list).c_str());
	Check(!ArmClocked() && ArmDiffers(regs, st_uncl.regs) == kArmRegs, "nothing executed: the ARM7 is held, and every register of it reads as before (the PC %08X)",
		ArmPc());
	Check(Suspended() == SOUND_BURST_NONE && c.n[0] == st_uncl.c.n[0] + 1 && c.n[1] == st_uncl.c.n[1] && c.n[2] == st_uncl.c.n[2] + 32 && c.n[3] == st_uncl.c.n[3] + 1,
		"the burst is not suspended and went on with its rest: one burst, no ARM phase, 32 time steps, one tail (%u, %u, %u, %u)", c.n[0] - st_uncl.c.n[0],
		c.n[1] - st_uncl.c.n[1], c.n[2] - st_uncl.c.n[2], c.n[3] - st_uncl.c.n[3]);
	Check(dbg_arm7_armed == 0 && !arm7::test::GetState().step, "the step is armed no more");

	CO_END(st_uncl.co);
}

/*
	arm7.step.notclocked: with the ARM7 held in reset the three steps answer -32012, and arm nothing; the DSP's
	step over and step out answer -32004 (and its step too, while it has no run control)
*/
namespace {

struct {
	Co co;
	u32 stopId;
} st_notcl;

}

BOUNDED_GUEST_TEST(arm7_step_notclocked, "arm7.step.notclocked")
{
	CO_BEGIN(st_notcl.co);

	while (!Settle(stop))
		CO_YIELD(st_notcl.co);

	GotoSh4(DSPTEST_G_HOLD_ARM7);

	while (!PauseAfter(stop, 2))
		CO_YIELD(st_notcl.co);

	Check(!ArmClocked() && arm7::StepCheck() == kErrNotClocked, "the guest holds its ARM7: dbg::arm7::StepCheck() answers %d", arm7::StepCheck());
	Check(emu::Step(Target::Arm7, StepKind::Into) == kErrNotClocked && emu::Step(Target::Arm7, StepKind::Over) == kErrNotClocked
		&& emu::Step(Target::Arm7, StepKind::Out) == kErrNotClocked, "a step, a step over and a step out of the ARM7 answer -32012");
	// The DSP as emu::TargetSteps() has it: without run control its three steps are -32004; with it a step over and
	// a step out are -32004 still, and a step is taken whether the ARM7 is held or not (the DSP's own tests hold
	// that step: this one arms nothing)
	if (!emu::TargetSteps(Target::Dsp))
	{
		Check(emu::Step(Target::Dsp, StepKind::Into) == kErrNotSupported && emu::Step(Target::Dsp, StepKind::Over) == kErrNotSupported
			&& emu::Step(Target::Dsp, StepKind::Out) == kErrNotSupported && emu::TargetSteps(Target::Arm7) && emu::TargetSteps(Target::Sh4),
			"the DSP has no steps: -32004; the SH4 and the ARM7 have");
	}
	else
	{
		Check(emu::Step(Target::Dsp, StepKind::Over) == kErrNotSupported && emu::Step(Target::Dsp, StepKind::Out) == kErrNotSupported
			&& emu::TargetSteps(Target::Arm7) && emu::TargetSteps(Target::Sh4), "the DSP steps: a step over and a step out of it are -32004, with the ARM7 held as always");
	}
	Check(dbg_arm7_armed == 0 && !arm7::test::GetState().step, "nothing was armed");

	// nothing was armed in the park loop either: a resume is taken
	st_notcl.stopId = stop.stopId;

	while (!PauseAfter(stop, 2))
		CO_YIELD(st_notcl.co);

	Check(stop.cause == StopCause::Pause && stop.stopId == st_notcl.stopId + 1 && !ArmClocked(), "and a resume from there is a resume: the next stop is the test's pause");

	CO_END(st_notcl.co);
}

/*
	arm7.step.sh4first: a breakpoint of the SH4 that is reached before the burst ends the wait of a step with
	its own cause, and no step is left armed
*/
namespace {

struct {
	Co co;
	u32 stopId, pc;
	Counts c;
} st_sh4;

}

BOUNDED_GUEST_TEST(arm7_step_sh4first, "arm7.step.sh4first")
{
	Counts c;
	std::vector<Seen> list;

	CO_BEGIN(st_sh4.co);

	while (!Settle(stop))
		CO_YIELD(st_sh4.co);

	// the loop of the SH4 comes by its first instruction every ten instructions
	Sh4Bps({ { 40, DSPTEST_G_MAIN_LOOP } });

	st_sh4.stopId = stop.stopId;
	st_sh4.pc = ArmPc();
	st_sh4.c = ReadCounts();
	TakeSeen();

	if (!StepArm())
		break;
	Check(!arm7::test::GetState().step && dbg_arm7_armed == 0, "emu::Step arms the action of the park loop: nothing of the ARM7 is armed before the loop acts on it");
	CO_YIELD(st_sh4.co);

	c = ReadCounts();
	list = TakeSeen();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 40 && stop.stopId == st_sh4.stopId + 1 && Ctx().pc == DSPTEST_G_MAIN_LOOP
		&& SeenIs(list, { { Lifetime::Parked, StopCause::Breakpoint } }),
		"the breakpoint of the SH4 ends the wait of the step with its own cause: \"%s\" id %u, published %s", StopCauseName(stop.cause), stop.breakpointId,
		SeenText(list).c_str());
	Check(c.n[0] == st_sh4.c.n[0] && ArmPc() == st_sh4.pc, "no burst was started and the ARM7 stands where it stood");
	Check(dbg_arm7_armed == 0 && !arm7::test::GetState().step, "no step is left armed: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);

	Sh4Bps({ });

	// and none comes back: a run from here is stopped by the test's pause
	while (!PauseAfter(stop, 3))
		CO_YIELD(st_sh4.co);

	Check(stop.cause == StopCause::Pause, "the run behind it is stopped by the test only (\"%s\")", StopCauseName(stop.cause));

	CO_END(st_sh4.co);
}

/*
	arm7.step.breakpointed: a step executes the instruction it stands on also with a listed breakpoint on it,
	whatever the stop it is asked from
*/
namespace {

struct {
	Co co;
	u32 pc, n;
	bool done;
	Counts c;
} st_bp;

}

BOUNDED_GUEST_TEST(arm7_step_breakpointed, "arm7.step.breakpointed")
{
	Counts c;

	CO_BEGIN(st_bp.co);

	while (!Settle(stop))
		CO_YIELD(st_bp.co);

	// From a stop the ARM7 did not decide, with no burst suspended: a breakpoint on the instruction the ARM7
	// stands on. (Taken at a stop with no request of the AICA waiting: a FIQ in front of the instruction would
	// end the step at the vector)
	st_bp.done = false;

	for (st_bp.n = 0; st_bp.n < 40 && !st_bp.done; st_bp.n++)
	{
		while (!BehindBurst(stop))
			CO_YIELD(st_bp.co);

		if (ArmInService() || ArmMode() != 0x13 || ArmPc() == kBl || ArmPc() == kBehindBl || ArmPc() == kLeaf + 8)
			continue;

		st_bp.pc = ArmPc();
		st_bp.c = ReadCounts();
		ArmBps({ { 50, st_bp.pc } });

		if (!StepArm())
			break;
		CO_YIELD(st_bp.co);

		c = ReadCounts();

		Check(stop.cause == StopCause::Step && ArmPc() == st_bp.pc + 4 && c.n[0] == st_bp.c.n[0] + 1,
			"a step from a stop behind a burst, with a listed breakpoint on the instruction the ARM7 stands on (%08X): the instruction executes and the step "
			"ends behind it (\"%s\", the ARM7 at %08X)", st_bp.pc, StopCauseName(stop.cause), ArmPc());
		st_bp.done = true;
	}

	Check(st_bp.done, "the case was taken (%u stops)", st_bp.n);

	// In a suspended burst, with the PC written onto a breakpoint: a resume would stop there with nothing
	// executed (arm7.bp.resume); a step executes it
	ArmBps({ });

	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_bp.co);

	ArmBps({ { 51, kPlain + 4 } });
	Check(SetArm(15, kPlain + 4), "the PC is written onto another breakpoint");

	if (!StepArm())
		break;
	CO_YIELD(st_bp.co);

	Check(stop.cause == StopCause::Step && ArmPc() == kPlain + 8 && Arm(1) == AicaReg16(0x28A0),
		"a step from a PC written onto a breakpoint executes the instruction there: \"%s\", the ARM7 at %08X, R1 has what it loaded", StopCauseName(stop.cause),
		ArmPc());

	ArmBps({ });

	CO_END(st_bp.co);
}

/*
	arm7.step.wait.*: the wait of a step, in which the SH4 runs with the stop in force. What ends the thread
	there, or resets the machine, is found by dc_run as behind a step of the SH4: no stop of the step is
	published, the step is armed no more, and the wait counts as a step that has executed, so the next thread
	resumes nothing.

	The last of these is shown on a loop of the test's own, two instructions that count in R8 and whose first
	one the PC of the SH4 is on at every stop: the thread ends where the stop was published. With a breakpoint
	listed on that instruction before the next thread starts, a resume that was owed would run through it once
*/
namespace {

const u32 kSpin = 0x8C020000;		// main memory that the guest does not use

struct {
	Co co;
	u32 stopId;
	u32 r8;
	std::atomic<bool> failed { false };
	std::atomic<u32> armedAtStop { 0 };
	std::atomic<bool> stepAtStop { false };
	std::atomic<u32> r8AtStop { 0 };
	std::atomic<u32> pcAtStop { 0 };
	std::atomic<u32> burstAtStop { 0 };
	emu::RunState stopped;
	Counts c;
} st_wait;

// On the emulator thread, parked: the SH4 is sent into the loop
void SpinSh4()
{
	// bra to itself, with add #1, r8 in its delay slot
	if (ReadMem16(kSpin) != 0xAFFE || ReadMem16(kSpin + 2) != 0x7801)
	{
		WriteMem16(kSpin, 0xAFFE);
		WriteMem16(kSpin + 2, 0x7801);
	}

	GotoSh4(kSpin);
}

// The other thread: the emulator thread has ended in the wait of a step. What it left, then a breakpoint on the
// instruction the SH4 stands on, and a thread that starts as one without a session does: with no start park
void WaitEnded()
{
	if (!WaitLifetime(Lifetime::Stopped))
	{
		st_wait.failed = true;
		return;
	}

	st_wait.stopped = emu::GetRunState();

	int rv = emu::Call(emu::CC_TABLE, [] {
		st_wait.armedAtStop = dbg_arm7_armed;
		st_wait.stepAtStop = arm7::test::GetState().step;
		st_wait.r8AtStop = Ctx().r[8];
		st_wait.pcAtStop = Ctx().pc;
		st_wait.burstAtStop = Suspended();

		Sh4Bps({ { 60, kSpin } });
	});

	if (rv != 0)
		st_wait.failed = true;

	Restart(true);
}

bool WaitEnding(const emu::RunState& stop, HandMode mode, const char* what, StopCause ended)
{
	std::vector<Seen> list;

	CO_BEGIN(st_wait.co);

	while (!Settle(stop))
		CO_YIELD(st_wait.co);

	SpinSh4();

	while (!BehindBurst(stop))
		CO_YIELD(st_wait.co);

	Check(Ctx().pc == kSpin && ArmClocked() && Suspended() == SOUND_BURST_NONE, "the SH4 spins on %08X, the ARM7 is clocked, no burst is suspended", Ctx().pc);

	st_wait.stopId = stop.stopId;
	st_wait.failed = false;
	st_wait.c = ReadCounts();
	TakeSeen();

	// in the first slice of the wait
	HandOn(mode);
	OtherStart(WaitEnded);

	if (!StepArm())
		break;
	CO_YIELD(st_wait.co);

	OtherJoin();
	HandOff();
	emu::test::plainStart = false;
	list = TakeSeen();

	Check(!st_wait.failed && hand.asked && hand.slices >= 1, "%s in the first slice of the wait of a step of the ARM7, and the thread ended", what);
	Check(st_wait.stopped.lifetime == Lifetime::Stopped && st_wait.stopped.cause == ended && !SeenHas(list, Lifetime::Parked, StopCause::Step),
		"no stop of the step is published: %s", SeenText(list).c_str());
	Check(st_wait.armedAtStop == 0 && !st_wait.stepAtStop && st_wait.burstAtStop == SOUND_BURST_NONE,
		"behind the thread the step is armed no more: dbg_arm7_armed is %u", st_wait.armedAtStop.load());
	Check(st_wait.pcAtStop == kSpin, "the thread ended with the SH4 where the stop was published (%08X)", st_wait.pcAtStop.load());

	// The next thread has no start park. Its first Park() resumes the stop of the thread before it when that is
	// owed: the instruction the SH4 stands on would execute, breakpoint or not, and the stop would come one
	// pass of the loop later
	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 60 && Ctx().pc == kSpin && Ctx().r[8] == st_wait.r8AtStop,
		"the first Park() of the next thread resumes nothing, the wait counted as a step: the breakpoint on the instruction of the SH4 stops at once "
		"(\"%s\" id %u, %u passes of the loop)", StopCauseName(stop.cause), stop.breakpointId, Ctx().r[8] - st_wait.r8AtStop);

	Sh4Bps({ });

	CO_END(st_wait.co);
}

}

BOUNDED_GUEST_TEST(arm7_step_wait_hoststop, "arm7.step.wait.hoststop")
{
	if (!WaitEnding(stop, HAND_STOP_FIRST, "the host stopped the machine", StopCause::HostStopped))
		return false;

	Check(hand.stopCallbacks.load() >= 1, "the stop callback ran");
	return true;
}

BOUNDED_GUEST_TEST(arm7_step_wait_exit, "arm7.step.wait.exit")
{
	return WaitEnding(stop, HAND_EXIT_FIRST, "the guest ended the run", StopCause::GuestExit);
}

/*
	arm7.step.wait.reset: a guest reset in the wait of a step: one stop, with reason reset, and no step is left
	armed
*/
namespace {

struct {
	Co co;
	u32 stopId;
} st_wreset;

}

BOUNDED_GUEST_TEST(arm7_step_wait_reset, "arm7.step.wait.reset")
{
	std::vector<Seen> list;

	CO_BEGIN(st_wreset.co);

	while (!Settle(stop))
		CO_YIELD(st_wreset.co);

	st_wreset.stopId = stop.stopId;
	TakeSeen();
	HandOn(HAND_RESET_FIRST);

	if (!StepArm())
		break;
	CO_YIELD(st_wreset.co);

	HandOff();
	list = TakeSeen();

	Check(hand.asked && stop.cause == StopCause::Reset && stop.stopId == st_wreset.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Reset } }),
		"the guest reset the machine in the first slice of the wait of a step: one stop, with reason \"%s\"; published %s", StopCauseName(stop.cause),
		SeenText(list).c_str());
	Check(dbg_arm7_armed == 0 && !arm7::test::GetState().step && !arm7::test::GetState().exempt && Suspended() == SOUND_BURST_NONE,
		"no step is left armed: dbg_arm7_armed is %u", (u32)dbg_arm7_armed);
	Check(Ctx().pc == kResetVector && !ArmClocked(), "the machine is reset: the SH4 at %08X, the ARM7 held", Ctx().pc);

	CO_END(st_wreset.co);
}

/*
	arm7.step.wait.grace: the two calls of the grace expiry, the active mask cleared and a resume, queued by two
	threads during the wait of a step. They are served behind the stop of the step, in their order, and the
	machine then runs
*/
namespace {

struct {
	Co co;
	u32 stopId;
	std::atomic<bool> calledMask { false }, calledResume { false };
	std::atomic<int> rvMask { 1 }, rvResume { 1 }, rvInner { 1 };
	std::atomic<u32> order { 0 }, maskOrder { 0 }, resumeOrder { 0 };
	std::atomic<u32> maskStopId { 0 }, resumeStopId { 0 };
	std::atomic<bool> failed { false };
	std::thread* second;
} st_grace;

}

BOUNDED_GUEST_TEST(arm7_step_wait_grace, "arm7.step.wait.grace")
{
	std::vector<Seen> list;

	CO_BEGIN(st_grace.co);

	while (!Settle(stop))
		CO_YIELD(st_grace.co);

	st_grace.stopId = stop.stopId;
	st_grace.calledMask = st_grace.calledResume = false;
	st_grace.order = 0;
	st_grace.failed = false;
	TakeSeen();

	// the hand holds the first slice of the wait until both calls are on their way into the queue
	HandOn(HAND_HOLD_FIRST);

	OtherStart([] {
		for (Clock::time_point t0 = Clock::now(); !hand.holding; std::this_thread::yield())
		{
			if (MsSince(t0) > kBoundMs)
			{
				st_grace.failed = true;
				return;
			}
		}

		st_grace.calledMask = true;
		st_grace.rvMask = emu::Call(emu::CC_TABLE, [] {
			st_grace.maskOrder = ++st_grace.order;
			st_grace.maskStopId = emu::GetRunState().stopId;
			emu::SetActiveMask(0);
		});
	});

	st_grace.second = new std::thread([] {
		for (Clock::time_point t0 = Clock::now(); !hand.holding || !st_grace.calledMask || !(g_attn.load() & ATTN_CALL); std::this_thread::yield())
		{
			if (MsSince(t0) > kBoundMs)
			{
				st_grace.failed = true;
				hand.release = true;
				return;
			}
		}

		// behind the first in the queue
		Nap(20);
		st_grace.calledResume = true;

		std::thread releaser([] {
			Nap(50);
			hand.release = true;
		});

		st_grace.rvResume = emu::Call(emu::CC_EXEC, [] {
			st_grace.resumeOrder = ++st_grace.order;
			st_grace.resumeStopId = emu::GetRunState().stopId;
			st_grace.rvInner = emu::Resume();
		});

		releaser.join();

		// the machine runs: the test gets its next stop from here
		if (!WaitLifetime(Lifetime::Running))
		{
			st_grace.failed = true;
			return;
		}

		Nap(20);

		for (Clock::time_point t0 = Clock::now(); MsSince(t0) < kBoundMs; )
		{
			emu::RunState s = emu::GetRunState();

			if (s.lifetime == Lifetime::Parked && s.cause == StopCause::Pause)
				return;
			if (s.lifetime == Lifetime::Running)
				emu::RequestPause();

			std::this_thread::yield();
		}

		st_grace.failed = true;
	});

	if (!StepArm())
		break;
	CO_YIELD(st_grace.co);

	// the stop of the step: the calls are in the queue behind it. The test arms nothing
	Check(stop.cause == StopCause::Step && stop.stopId == st_grace.stopId + 1 && Suspended() == SOUND_BURST_ARM7,
		"the stop of the step is published first: \"%s\"", StopCauseName(stop.cause));
	Check(hand.sawCall && st_grace.calledMask && st_grace.calledResume && st_grace.order == 0,
		"both calls were queued during the wait, and neither was served in it");
	CO_YIELD(st_grace.co);

	OtherJoin();
	st_grace.second->join();
	delete st_grace.second;
	st_grace.second = nullptr;
	HandOff();
	list = TakeSeen();

	Check(!st_grace.failed && st_grace.rvMask == 0 && st_grace.rvResume == 0 && st_grace.rvInner == 0 && st_grace.maskOrder == 1 && st_grace.resumeOrder == 2,
		"the two calls were served in their order, and the resume was taken (%d, %d, %d)", st_grace.rvMask.load(), st_grace.rvResume.load(),
		st_grace.rvInner.load());
	Check(st_grace.maskStopId == st_grace.stopId + 1 && st_grace.resumeStopId == st_grace.stopId + 1, "behind the stop of the step, which they found published");
	Check(stop.cause == StopCause::Pause
		&& SeenIs(list, { { Lifetime::Parked, StopCause::Step }, { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } }),
		"and the machine then ran, until the test paused it: published %s", SeenText(list).c_str());

	emu::SetActiveMask(kMaskAll);

	CO_END(st_grace.co);
}

/*
	arm7.step.wait.sh4: for the SH4 the leave of a step that waits is a resume. Two things of a resume that only
	the SH4 can show:

	  - the resume exemption. From a stop at a breakpoint of the SH4 (on the handler of the AICA's interrupt,
	    which the guest enters every twenty-six bursts or so) the step runs the SH4 through that breakpoint to
	    the next burst, and its stop is the step's. A leave that armed no exemption would stop on the
	    breakpoint again, with nothing executed, at every try;
	  - the interrupt acceptance that the latch held back. The burst in which the ARM7 raises the AICA's
	    interrupt to the SH4 is stepped to its end in the park context: no burst is suspended, the interrupt is
	    pending and the SH4 has not executed since. A step from there waits; with a breakpoint on the handler it
	    is the SH4 that stops, come from the PC of the stop with no time passed. A leave that accepted nothing
	    would let the SH4 run a slice first
*/
namespace {

struct {
	Co co;
	u32 sh4pc, r8;
	u64 time;
	Counts c;
} st_wsh4;

}

BOUNDED_GUEST_TEST(arm7_step_wait_sh4, "arm7.step.wait.sh4")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_wsh4.co);

	while (!Settle(stop))
		CO_YIELD(st_wsh4.co);

	// the exemption
	Sh4Bps({ { 61, DSPTEST_G_IRQ } });

	if (!Resume())
		break;
	CO_YIELD(st_wsh4.co);

	if (!Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 61 && Ctx().pc == DSPTEST_G_IRQ && Suspended() == SOUND_BURST_NONE && ArmClocked(),
		"a stop at a breakpoint of the SH4 on the handler of the AICA's interrupt, with no burst suspended (\"%s\" id %u, pc %08X)", StopCauseName(stop.cause),
		stop.breakpointId, Ctx().pc))
	{
		Sh4Bps({ });
		break;
	}

	st_wsh4.c = ReadCounts();
	TakeSeen();

	if (!StepArm())
		break;
	CO_YIELD(st_wsh4.co);

	list = TakeSeen();
	c = ReadCounts();

	Check(stop.cause == StopCause::Step && c.n[0] == st_wsh4.c.n[0] + 1 && Suspended() == SOUND_BURST_ARM7 && Ctx().pc != DSPTEST_G_IRQ
		&& SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
		"a step of the ARM7 from there runs the SH4 through the breakpoint it stands on, to the next burst, and its stop is the step's: \"%s\" id %u, %u burst(s) "
		"started since, the SH4 at %08X, published %s", StopCauseName(stop.cause), stop.breakpointId, c.n[0] - st_wsh4.c.n[0], Ctx().pc, SeenText(list).c_str());

	Sh4Bps({ });

	// the acceptance
	while (!Settle(stop))
		CO_YIELD(st_wsh4.co);

	while (!SuspendAt(stop, kRaise))
		CO_YIELD(st_wsh4.co);

	ArmBps({ });

	while (!StepToBurstEnd(stop))
		CO_YIELD(st_wsh4.co);

	if (!Check(stop.cause == StopCause::Step && Suspended() == SOUND_BURST_NONE && Ctx().sr.BL == 0 && Ctx().pc >= DSPTEST_G_MAIN_LOOP
		&& Ctx().pc < DSPTEST_G_SLEEP_LOOP,
		"the burst in which the ARM7 raises the AICA's interrupt to the SH4 was stepped to its end in the park context: none is suspended, and the SH4 is in its "
		"loop with its interrupts open (\"%s\", burst %u, pc %08X)", StopCauseName(stop.cause), Suspended(), Ctx().pc))
		break;

	st_wsh4.sh4pc = Ctx().pc;
	st_wsh4.r8 = Ctx().r[8];
	st_wsh4.time = sh4_sched_now64();
	st_wsh4.c = ReadCounts();

	Sh4Bps({ { 62, DSPTEST_G_IRQ } });

	if (!StepArm())
		break;
	CO_YIELD(st_wsh4.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 62 && Ctx().pc == DSPTEST_G_IRQ,
		"a step of the ARM7 from there waits, and the SH4 stops on the first instruction of its handler: \"%s\" id %u, pc %08X", StopCauseName(stop.cause),
		stop.breakpointId, Ctx().pc);
	Check(Ctx().spc == st_wsh4.sh4pc && Ctx().r[8] == st_wsh4.r8 && sh4_sched_now64() == st_wsh4.time && c.n[0] == st_wsh4.c.n[0],
		"the leave accepted the interrupt before the first instruction of the SH4: it comes from the PC of the stop (%08X, the stop was at %08X), the loop has not "
		"counted, %llu cycles have passed and %u bursts started", Ctx().spc, st_wsh4.sh4pc, (unsigned long long)(sh4_sched_now64() - st_wsh4.time),
		c.n[0] - st_wsh4.c.n[0]);
	Check(dbg_arm7_armed == 0 && !arm7::test::GetState().step, "and the step that the stop of the SH4 ended is armed no more");

	Sh4Bps({ });

	CO_END(st_wsh4.co);
}

/*
	arm7.stepover.bl, arm7.stepover.plain, arm7.stepout: resumes with one temporary breakpoint. They execute the
	instruction they are asked from, also with a listed breakpoint on it and from a stop the ARM7 did not decide:
	the test puts the ARM7 there by a PC write at a stop behind a burst
*/
namespace {

struct {
	Co co;
	u32 n, calls, stopId;
	bool done;
	Counts c;
} st_over;

// A stop behind a burst at which nothing waits to be entered in front of the next instruction of the loop: the
// ARM7 is in the mode of its loop, and no request of the AICA is latched. Returns false while it has armed
// something; st_over.done says whether such a stop was found
bool QuietBehindBurst(const emu::RunState& stop)
{
	static Co co;

	CO_BEGIN(co);

	st_over.done = false;

	for (st_over.n = 0; st_over.n < 40 && !st_over.done; st_over.n++)
	{
		while (!BehindBurst(stop))
			CO_YIELD(co);

		st_over.done = !ArmInService() && ArmMode() == 0x13;
	}

	CO_END(co);
}

}

BOUNDED_GUEST_TEST(arm7_stepover_bl, "arm7.stepover.bl")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_over.co);

	while (!Settle(stop))
		CO_YIELD(st_over.co);

	Check((ArmWord(kBl) & 0xFF000000) == 0xEB000000 && kBl + 8 + ((s32)(ArmWord(kBl) << 8) >> 6) == kLeaf, "the guest has its bl a_leaf at %08X", kBl);

	while (!QuietBehindBurst(stop))
		CO_YIELD(st_over.co);

	if (!Check(st_over.done, "a stop behind a burst with no request of the AICA waiting (%u stops)", st_over.n))
		break;

	// onto the bl, with a listed breakpoint on it
	Check(SetArm(15, kBl), "the PC of the ARM7 is written onto the bl");
	ArmBps({ { 70, kBl } });

	st_over.calls = Arm(7);
	st_over.stopId = stop.stopId;
	st_over.c = ReadCounts();
	TakeSeen();

	if (!StepArm(StepKind::Over))
		break;
	CO_YIELD(st_over.co);

	list = TakeSeen();
	c = ReadCounts();

	Check(stop.cause == StopCause::Step && stop.breakpointId == 0 && ArmPc() == kBehindBl && Arm(7) == st_over.calls + 1,
		"a step over the bl, asked from a stop the ARM7 did not decide and with a breakpoint on the bl, ends on the line behind it with \"%s\": the ARM7 at "
		"%08X, a_leaf ran %u time(s)", StopCauseName(stop.cause), ArmPc(), Arm(7) - st_over.calls);
	Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Step } }) && c.n[0] == st_over.c.n[0] + 1,
		"it is a resume: published %s, the stop in the next burst", SeenText(list).c_str());
	Check(!arm7::test::GetState().temp && dbg_arm7_armed == 1, "the temporary breakpoint is gone with the stop; the listed one stays (dbg_arm7_armed %u)",
		(u32)dbg_arm7_armed);

	// with the burst suspended the temporary breakpoint is reached in the park context: no Running
	ArmBps({ });

	while (!SuspendAt(stop, kBl))
		CO_YIELD(st_over.co);

	st_over.calls = Arm(7);
	TakeSeen();

	if (!StepArm(StepKind::Over))
		break;
	CO_YIELD(st_over.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Step && ArmPc() == kBehindBl && Arm(7) == st_over.calls + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
		"a step over the bl from the breakpoint on it, in a suspended burst: it ends on the line behind with \"%s\", and nothing was left: published %s",
		StopCauseName(stop.cause), SeenText(list).c_str());

	ArmBps({ });

	CO_END(st_over.co);
}

namespace {

struct {
	Co co;
} st_plain;

}

BOUNDED_GUEST_TEST(arm7_stepover_plain, "arm7.stepover.plain")
{
	std::vector<Seen> list;

	CO_BEGIN(st_plain.co);

	while (!Settle(stop))
		CO_YIELD(st_plain.co);

	while (!SuspendAt(stop, kPlain))
		CO_YIELD(st_plain.co);

	TakeSeen();

	if (!StepArm(StepKind::Over))
		break;
	CO_YIELD(st_plain.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Step && ArmPc() == kPlain + 4 && SeenIs(list, { { Lifetime::Parked, StopCause::Step } })
		&& !arm7::test::GetState().temp, "a step over an instruction that is no bl is a step: \"%s\", the ARM7 at %08X, no temporary breakpoint, published %s",
		StopCauseName(stop.cause), ArmPc(), SeenText(list).c_str());

	ArmBps({ });

	CO_END(st_plain.co);
}

namespace {

struct {
	Co co;
	u32 calls, stopId;
	Counts c;
} st_out;

}

BOUNDED_GUEST_TEST(arm7_stepout, "arm7.stepout")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_out.co);

	while (!Settle(stop))
		CO_YIELD(st_out.co);

	while (!QuietBehindBurst(stop))
		CO_YIELD(st_out.co);

	if (!Check(st_over.done && Arm(14) == kBehindBl, "a stop behind a burst with no request of the AICA waiting; R14 of the loop is the line behind its bl (%08X)",
		Arm(14)))
		break;

	// onto the first instruction of a_leaf, with a listed breakpoint on it
	Check(SetArm(15, kLeaf), "the PC of the ARM7 is written onto a_leaf");
	ArmBps({ { 71, kLeaf } });

	st_out.calls = Arm(7);
	st_out.c = ReadCounts();
	TakeSeen();

	if (!StepArm(StepKind::Out))
		break;
	CO_YIELD(st_out.co);

	list = TakeSeen();
	c = ReadCounts();

	Check(stop.cause == StopCause::Step && stop.breakpointId == 0 && ArmPc() == kBehindBl && Arm(7) == st_out.calls + 1,
		"a step out of a_leaf, asked from its first line with a breakpoint on it, ends where R14 pointed with \"%s\": the ARM7 at %08X, the leaf ran",
		StopCauseName(stop.cause), ArmPc());
	Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Step } }) && c.n[0] == st_out.c.n[0] + 1,
		"it is a resume: published %s", SeenText(list).c_str());
	Check(!arm7::test::GetState().temp, "the temporary breakpoint is gone with the stop");

	// from a stop inside the leaf that the ARM7 decided, in the suspended burst
	ArmBps({ });

	while (!SuspendAt(stop, kLeaf + 4))
		CO_YIELD(st_out.co);

	TakeSeen();

	if (!StepArm(StepKind::Out))
		break;
	CO_YIELD(st_out.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Step && ArmPc() == kBehindBl && SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
		"a step out from the breakpoint inside the leaf, in a suspended burst: \"%s\" on the line behind the bl, nothing was left: published %s",
		StopCauseName(stop.cause), SeenText(list).c_str());

	ArmBps({ });

	CO_END(st_out.co);
}

/*
	arm7.reset.suspended, arm7.hoststop.suspended, arm7.exit.suspended: the machine is reset, stopped by the host
	or ended by the guest while the thread is parked on a suspended burst. A reset discards the burst. The
	others run it to its end in front of the stop callback, without checks: the breakpoint that suspended it
	is still listed and active, and stops nothing
*/
namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
	std::atomic<bool> failed { false };
	std::atomic<u32> burstAtStop { 0 };
	Counts atStop;
	emu::RunState stopped;
} st_susp_end;

bool SuspendedEnding(const emu::RunState& stop, bool exit)
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_susp_end.co);

	while (!Settle(stop))
		CO_YIELD(st_susp_end.co);

	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_susp_end.co);

	st_susp_end.c = ReadCounts();
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
		&& st_susp_end.stopped.cause == (exit ? StopCause::GuestExit : StopCause::HostStopped), "the thread ended while it was parked on a suspended burst");

	if (!exit)
		Check(hand.stopCallbacks == 1 && hand.stopSuspended == SOUND_BURST_NONE && hand.stopCounts.n[3] == st_susp_end.c.n[3] + 1,
			"the burst is complete in front of the stop callback: the callback found none suspended, and the tail run");

	Check(st_susp_end.burstAtStop == SOUND_BURST_NONE && st_susp_end.atStop.n[0] == st_susp_end.c.n[0]
		&& st_susp_end.atStop.n[1] - st_susp_end.c.n[1] <= 32 && st_susp_end.atStop.n[1] - st_susp_end.c.n[1] >= 1
		&& st_susp_end.atStop.n[2] - st_susp_end.c.n[2] == st_susp_end.atStop.n[1] - st_susp_end.c.n[1] && st_susp_end.atStop.n[3] == st_susp_end.c.n[3] + 1,
		"behind the thread no burst is suspended: the rest of its ARM phases ran (%u), each with its time step, and its tail once",
		st_susp_end.atStop.n[1] - st_susp_end.c.n[1]);
	Check(dbg_arm7_armed != 0 && !SeenHas(list, Lifetime::Parked, StopCause::Breakpoint),
		"nothing stopped it: the breakpoint is still listed and active, and no stop of it was published (%s)", SeenText(list).c_str());
	Check(stop.cause == StopCause::Start, "the host starts the next thread");

	// the next callback of the AICA finds no burst suspended
	ArmBps({ });
	st_susp_end.c = ReadCounts();

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_susp_end.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - st_susp_end.c.n[0] >= 3 && c.n[3] - st_susp_end.c.n[3] == c.n[0] - st_susp_end.c.n[0],
		"and the machine runs on: %u bursts", c.n[0] - st_susp_end.c.n[0]);

	CO_END(st_susp_end.co);
}

}

BOUNDED_GUEST_TEST(arm7_hoststop_suspended, "arm7.hoststop.suspended")
{
	return SuspendedEnding(stop, false);
}

/*
	arm7.hoststop.insleep: the check of the ARM7 does not look at the run flag. The host stops the machine while
	the SH4 waits in SLEEP: the cpu is stopped, and the wait runs on to its end, with the bursts that fall into
	it. A breakpoint of the ARM7 that is hit in one of them suspends its burst as in a run, and the stop is
	latched in that slice, which ends the wait: the thread goes to its stop callback, in front of which the
	burst is run to its end. (A check that recorded nothing for a stopped cpu would leave the burst suspended
	with the wait running on, into the next callback of the AICA.)
*/
namespace {

struct {
	Co co;
	Counts c;
	std::atomic<bool> failed { false };
	std::atomic<u32> burstAtStop { 0 };
	Counts atStop;
	emu::RunState stopped;
} st_sleep_stop;

}

BOUNDED_GUEST_TEST(arm7_hoststop_insleep, "arm7.hoststop.insleep")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_sleep_stop.co);

	while (!Settle(stop))
		CO_YIELD(st_sleep_stop.co);

	// the SH4 sleeps from its first slice on; the next burst is a period of the AICA callback away
	GotoSh4(DSPTEST_G_SLEEP_LOOP);
	ArmBps({ { 85, kMain } });

	st_sleep_stop.c = ReadCounts();
	st_sleep_stop.failed = false;
	hand.stopCallbacks = 0;
	hand.stopSuspended = ~0u;
	TakeSeen();
	HandOn(HAND_STOP_IN_SLEEP);

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Stopped))
		{
			st_sleep_stop.failed = true;
			return;
		}

		st_sleep_stop.stopped = emu::GetRunState();

		emu::Call(emu::CC_TABLE, [] {
			st_sleep_stop.burstAtStop = Suspended();
			st_sleep_stop.atStop = ReadCounts();
		});

		Restart(false);
	});

	if (!Resume())
		break;
	CO_YIELD(st_sleep_stop.co);

	OtherJoin();
	HandOff();
	list = TakeSeen();

	Check(!st_sleep_stop.failed && hand.asked && hand.stopCallbacks == 1 && st_sleep_stop.stopped.cause == StopCause::HostStopped,
		"the host stopped the machine in the first slice of a SLEEP wait of the SH4, and the thread ended");
	Check(hand.suspended == 1 && st_sleep_stop.atStop.n[0] == st_sleep_stop.c.n[0] + 1,
		"the wait ran on to the next burst, in which the breakpoint of the ARM7 suspended the burst with the cpu stopped; that slice ended the wait (%u slices "
		"saw a burst suspended, %u bursts)", hand.suspended, st_sleep_stop.atStop.n[0] - st_sleep_stop.c.n[0]);
	Check(hand.stopSuspended == SOUND_BURST_NONE && st_sleep_stop.burstAtStop == SOUND_BURST_NONE && st_sleep_stop.atStop.n[1] == st_sleep_stop.c.n[1] + 32
		&& st_sleep_stop.atStop.n[3] == st_sleep_stop.c.n[3] + 1, "the burst is whole in front of the stop callback: 32 ARM phases and its tail");
	Check(!SeenHas(list, Lifetime::Parked, StopCause::Breakpoint), "and the stop of the breakpoint is not published: %s", SeenText(list).c_str());

	// (out of the sleep loop: a pause that is asked for during a SLEEP wait is taken when it is 250 ms old)
	ArmBps({ });
	GotoSh4(DSPTEST_G_MAIN);
	st_sleep_stop.c = ReadCounts();

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_sleep_stop.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - st_sleep_stop.c.n[0] >= 3 && c.n[3] - st_sleep_stop.c.n[3] == c.n[0] - st_sleep_stop.c.n[0],
		"the next thread runs on: %u bursts", c.n[0] - st_sleep_stop.c.n[0]);

	CO_END(st_sleep_stop.co);
}

BOUNDED_GUEST_TEST(arm7_exit_suspended, "arm7.exit.suspended")
{
	return SuspendedEnding(stop, true);
}

namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
} st_reset;

}

BOUNDED_GUEST_TEST(arm7_reset_suspended, "arm7.reset.suspended")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_reset.co);

	while (!Settle(stop))
		CO_YIELD(st_reset.co);

	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_reset.co);

	st_reset.stopId = stop.stopId;
	st_reset.c = ReadCounts();
	TakeSeen();

	Check(emu::Reset() == 0, "the debugger resets the machine at a stop inside a burst");
	Check(Suspended() == SOUND_BURST_NONE, "the reset discards the suspended burst");
	CO_YIELD(st_reset.co);

	list = TakeSeen();
	c = ReadCounts();

	Check(stop.cause == StopCause::Reset && stop.stopId == st_reset.stopId + 1 && SeenIs(list, { { Lifetime::Parked, StopCause::Reset } }),
		"the stop of the reset is published: \"%s\"", StopCauseName(stop.cause));
	Check(c.n[1] == st_reset.c.n[1] && c.n[2] == st_reset.c.n[2] && c.n[3] == st_reset.c.n[3] && Suspended() == SOUND_BURST_NONE,
		"nothing of the discarded burst ran: no ARM phase, no time step, no tail");
	Check(!ArmClocked() && ArmPc() == 0 && !arm7::test::GetState().exempt && !arm7::test::GetState().stopByCheck,
		"the ARM7 is held on its reset state, and the breakpoint state has no exemption and no stop of the ARM7 to resume");

	// The breakpoint is still listed. The machine boots again, and the next callback of the AICA finds no burst
	// suspended: the breakpoint stops the program when its ARM7 gets there again
	st_reset.c = c;

	if (!Resume())
		break;
	CO_YIELD(st_reset.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && ArmPc() == kMain && c.n[0] - st_reset.c.n[0] >= 1,
		"the machine boots again and runs its bursts (%u), until the breakpoint of the ARM7 stops the program anew", c.n[0] - st_reset.c.n[0]);

	ArmBps({ });

	CO_END(st_reset.co);
}

/*
	arm7.sameslice.*: the machine is asked to stop, to exit or to reset in the tick in which a check suspends
	its burst, by the hand, which runs behind the AICA's callback in that tick. dc_run meets the suspended burst
	with no Park() in between
*/
namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
	std::atomic<bool> failed { false };
	std::atomic<u32> burstAtStop { 0 };
	std::atomic<u32> armPcAtStop { 0 };
	Counts atStop;
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
	st_same.stopId = stop.stopId;
	st_same.failed = false;
	hand.stopCallbacks = 0;
	hand.stopSuspended = ~0u;
	TakeSeen();

	ArmBps({ { 80, kMain } });
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
		});

		Restart(false);
	});

	if (!Resume())
		break;
	CO_YIELD(st_same.co);

	OtherJoin();
	HandOff();
	list = TakeSeen();

	Check(!st_same.failed && hand.asked && hand.suspended == 1,
		"%s in the tick in which the breakpoint of the ARM7 suspended the burst, and the thread ended", exit ? "the guest ended the run" : "the host stopped the machine");
	Check(st_same.stopped.lifetime == Lifetime::Stopped && st_same.stopped.cause == (exit ? StopCause::GuestExit : StopCause::HostStopped)
		&& !SeenHas(list, Lifetime::Parked, StopCause::Breakpoint) && st_same.stopped.stopId == st_same.stopId + 1,
		"the stop of the breakpoint is not published: %s", SeenText(list).c_str());

	if (!exit)
		Check(hand.stopCallbacks == 1 && hand.stopSuspended == SOUND_BURST_NONE && hand.stopCounts.n[3] == st_same.c.n[3] + 1,
			"the burst is complete in front of the stop callback: the callback found none suspended, and the tail run");

	Check(st_same.burstAtStop == SOUND_BURST_NONE && st_same.atStop.n[0] == st_same.c.n[0] + 1 && st_same.atStop.n[1] == st_same.c.n[1] + 32
		&& st_same.atStop.n[2] == st_same.c.n[2] + 32 && st_same.atStop.n[3] == st_same.c.n[3] + 1,
		"behind the thread the burst is whole: 32 ARM phases, 32 time steps, one tail, and none is suspended");
	Check(stop.cause == StopCause::Start, "the host starts the next thread");

	ArmBps({ });
	st_same.c = ReadCounts();

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_same.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && c.n[0] - st_same.c.n[0] >= 3 && c.n[3] - st_same.c.n[3] == c.n[0] - st_same.c.n[0],
		"the next callback of the AICA finds no burst suspended, and the machine runs on: %u bursts", c.n[0] - st_same.c.n[0]);

	CO_END(st_same.co);
}

}

BOUNDED_GUEST_TEST(arm7_sameslice_hoststop, "arm7.sameslice.hoststop")
{
	return SameSliceEnding(stop, false);
}

BOUNDED_GUEST_TEST(arm7_sameslice_exit, "arm7.sameslice.exit")
{
	return SameSliceEnding(stop, true);
}

namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
} st_same_reset;

}

BOUNDED_GUEST_TEST(arm7_sameslice_reset, "arm7.sameslice.reset")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_same_reset.co);

	while (!Settle(stop))
		CO_YIELD(st_same_reset.co);

	st_same_reset.c = ReadCounts();
	st_same_reset.stopId = stop.stopId;
	TakeSeen();

	ArmBps({ { 81, kMain } });
	HandOn(HAND_RESET_SUSPENDED);

	if (!Resume())
		break;
	CO_YIELD(st_same_reset.co);

	HandOff();
	list = TakeSeen();
	c = ReadCounts();

	Check(hand.asked && hand.suspended == 1, "the guest reset the machine in the tick in which the breakpoint of the ARM7 suspended the burst");
	Check(stop.cause == StopCause::Reset && stop.stopId == st_same_reset.stopId + 1
		&& SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Reset } }),
		"the stop that is published is the reset's: \"%s\"; published %s", StopCauseName(stop.cause), SeenText(list).c_str());
	Check(Suspended() == SOUND_BURST_NONE && c.n[0] == st_same_reset.c.n[0] + 1 && c.n[3] == st_same_reset.c.n[3] && c.n[1] == st_same_reset.c.n[1],
		"the burst is discarded: none is suspended, and its tail never ran");
	Check(Ctx().pc == kResetVector && !ArmClocked() && !arm7::test::GetState().stopByCheck, "the machine is reset, and no stop of the ARM7 is there to resume");

	// the breakpoint is still listed: the program is stopped again when its ARM7 gets there
	st_same_reset.c = c;

	if (!Resume())
		break;
	CO_YIELD(st_same_reset.co);

	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 81 && ArmPc() == kMain && c.n[0] - st_same_reset.c.n[0] >= 1,
		"the next callback of the AICA finds no burst suspended: the machine boots and runs its bursts (%u) until the breakpoint stops the program anew",
		c.n[0] - st_same_reset.c.n[0]);

	ArmBps({ });

	CO_END(st_same_reset.co);
}

/*
	arm7.intc.order: an interrupt of the SH4 that a burst raises while it is run on in the park context is
	accepted on the way out, before the first instruction of the SH4. The burst is suspended in front of the
	store with which the ARM7 of the guest raises the AICA's interrupt to the SH4
*/
namespace {

struct {
	Co co;
	u32 sh4pc, r8;
	u64 time;
	Counts c;
} st_intc;

}

BOUNDED_GUEST_TEST(arm7_intc_order, "arm7.intc.order")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_intc.co);

	while (!Settle(stop))
		CO_YIELD(st_intc.co);

	Check(ArmWord(kRaise) == 0xE585A0B8, "the guest has the store to MCIPD of its FIQ handler at %08X", kRaise);

	// every 64th interrupt of the timer: some twenty-six bursts
	while (!SuspendAt(stop, kRaise))
		CO_YIELD(st_intc.co);

	Check(Ctx().pc >= DSPTEST_G_MAIN_LOOP && Ctx().pc < DSPTEST_G_SLEEP_LOOP && Ctx().sr.BL == 0,
		"the burst is suspended in front of the store; the SH4 is in its loop with its interrupts open (pc %08X)", Ctx().pc);

	st_intc.sh4pc = Ctx().pc;
	st_intc.r8 = Ctx().r[8];
	st_intc.time = sh4_sched_now64();
	st_intc.c = ReadCounts();

	ArmBps({ });
	Sh4Bps({ { 90, DSPTEST_G_IRQ } });
	TakeSeen();

	if (!Resume())
		break;
	CO_YIELD(st_intc.co);

	list = TakeSeen();
	c = ReadCounts();

	Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == 90 && Ctx().pc == DSPTEST_G_IRQ,
		"the resume runs the burst on, and the next stop is on the first instruction of the SH4's handler: \"%s\" id %u, pc %08X", StopCauseName(stop.cause),
		stop.breakpointId, Ctx().pc);
	Check(Ctx().spc == st_intc.sh4pc && Ctx().r[8] == st_intc.r8 && sh4_sched_now64() == st_intc.time,
		"the interrupt was accepted before the first instruction of the SH4: it comes from the PC of the stop (%08X), the loop has not counted, no time has passed",
		Ctx().spc);
	Check(Suspended() == SOUND_BURST_NONE && c.n[3] == st_intc.c.n[3] + 1 && c.n[0] == st_intc.c.n[0],
		"the burst that raised it ran to its end in the park context, with its tail");
	Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Breakpoint } }), "published %s", SeenText(list).c_str());

	Sh4Bps({ });

	CO_END(st_intc.co);
}

/*
	arm7.events.park: events of the AICA that a suspended burst raises while the park loop runs it on are
	logged and do not stop; the same event stops in a run
*/
namespace {

struct {
	Co co;
	u64 seq;
	u64 time;
	Counts c;
} st_events;

u64 LastEventSeq()
{
	std::vector<events::Entry> log;

	events::Snapshot(&log);
	return log.empty() ? 0 : log.back().seq;
}

}

BOUNDED_GUEST_TEST(arm7_events_park, "arm7.events.park")
{
	std::vector<Seen> list;
	std::vector<events::Entry> log;
	Counts c;
	u32 parked, later;

	CO_BEGIN(st_events.co);

	while (!Settle(stop))
		CO_YIELD(st_events.co);

	// in front of the first instruction of the loop that a burst executes: its 32 time steps are still to come
	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_events.co);

	c = ReadCounts();

	ArmBps({ });
	EventBp(91);

	st_events.seq = LastEventSeq();
	st_events.time = sh4_sched_now64();
	st_events.c = c;
	TakeSeen();

	if (!Resume())
		break;
	CO_YIELD(st_events.co);

	NoEventBp();
	list = TakeSeen();
	c = ReadCounts();
	events::Snapshot(&log);

	// the entries of the timer since: those of the burst that was run on carry its time
	parked = later = 0;

	for (const events::Entry& entry : log)
	{
		if (entry.seq <= st_events.seq || entry.id != EV_AICA_TIMER)
			continue;

		if (entry.cycles == st_events.time)
			parked++;
		else
			later++;
	}

	Check(parked >= 1, "with an event breakpoint on the timers of the AICA, the rest of the suspended burst raised the event %u time(s) while the park loop ran "
		"it on: they are in the log", parked);
	Check(SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Event } }) && stop.breakpointId == 91,
		"and did not stop: the resume left, published %s", SeenText(list).c_str());
	Check(stop.cause == StopCause::Event && c.n[0] == st_events.c.n[0] + 1 && later >= 1 && sh4_sched_now64() > st_events.time && Suspended() == SOUND_BURST_NONE,
		"the same event stops in the run behind it: in the next burst, which is not suspended for it");

	CO_END(st_events.co);
}

/*
	arm7.reads.suspended: at a stop inside a burst the ARM7 stands between two instructions and the sound
	generator between two samples: every accessor of the debugger reads the same twice, and frame 0 of the
	callstack of the ARM7 is its PC
*/
namespace {

struct {
	Co co;
	u32 phases;			// the ARM phases that had run to their end at the stop behind the burst before
	u32 steps;
} st_reads;

bool SameChannel(const AicaChannelDebug& a, const AicaChannelDebug& b)
{
	return a.enabled == b.enabled && a.looped == b.looped && a.aegState == b.aegState && a.fegState == b.fegState && a.aegLevel == b.aegLevel
		&& a.fegValue == b.fegValue && a.ca == b.ca && a.caFraction == b.caFraction && a.step == b.step && a.sampleCurrent == b.sampleCurrent
		&& a.samplePrevious == b.samplePrevious && a.lfoState == b.lfoState && a.alfo == b.alfo && a.plfo == b.plfo;
}

}

BOUNDED_GUEST_TEST(arm7_reads_suspended, "arm7.reads.suspended")
{
	CO_BEGIN(st_reads.co);

	while (!Settle(stop))
		CO_YIELD(st_reads.co);

	// in the middle of the burst: some samples in
	st_reads.phases = ReadCounts().n[1];

	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_reads.co);

	ArmBps({ });

	// (seven samples are some hundreds of steps. Where a step ends no ARM phase the loop has an end all the same,
	// and the test fails here)
	for (st_reads.steps = 0; st_reads.steps < kStepsMax && ReadCounts().n[1] - st_reads.phases < 7 && Suspended() == SOUND_BURST_ARM7; st_reads.steps++)
	{
		if (!StepArm())
			break;
		CO_YIELD(st_reads.co);
	}

	Check(Suspended() == SOUND_BURST_ARM7 && ReadCounts().n[1] - st_reads.phases >= 7 && ReadCounts().n[1] - st_reads.phases < 32,
		"the stop is inside a burst, behind %u of its samples (%u steps)", ReadCounts().n[1] - st_reads.phases, st_reads.steps);

	{
		ArmRegs a = ReadArm(), b = ReadArm();
		u32 la = 1, ma = 2, lb = 3, mb = 4;
		bool int_a = Scpu()->DebugGetInterrupt(&la, &ma), int_b = Scpu()->DebugGetInterrupt(&lb, &mb);

		Check(ArmDiffers(a, b) == kArmRegs && int_a && int_b && la == lb && ma == mb && Scpu()->DebugEnabled() && Scpu()->DebugEnabled(),
			"the registers of the ARM7, its interrupt controller and its clock read the same twice");
	}

	{
		static u8 a[0x8000], b[0x8000];
		bool ok = Aica()->DebugReadRegs(0, 0x8000, a) && Aica()->DebugReadRegs(0, 0x8000, b);
		u32 channels = 0, playing = 0;

		for (u32 ch = 0; ch < 64; ch++)
		{
			AicaChannelDebug ca = { }, cb = { };

			if (Aica()->DebugGetChannel(ch, &ca) && Aica()->DebugGetChannel(ch, &cb) && SameChannel(ca, cb))
				channels++;
			if (ca.enabled)
				playing++;
		}

		Check(ok && memcmp(a, b, sizeof(a)) == 0, "the register file of the AICA reads the same twice");
		Check(channels == 64 && playing >= 1, "and so does the state of its 64 channels, %u of which play", playing);
	}

	{
		DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
		DspDebugState a = { }, b = { };
		bool ok = dsp->DebugGetState(&a) && dsp->DebugGetState(&b) && a.step == b.step && a.mdecCt == b.mdecCt;
		u32 steps = 0;

		for (u32 step = 0; step < 128; step++)
		{
			u16 pa[4] = { 1, 2, 3, 4 }, pb[4] = { 5, 6, 7, 8 };

			if (dsp->DebugGetProgramStep(step, pa) && dsp->DebugGetProgramStep(step, pb) && memcmp(pa, pb, sizeof(pa)) == 0)
				steps++;
		}

		u32 sa = 1, sb = 2;
		bool wa = false, wb = true;
		bool rtc = libAICA_DebugGetRTC(&sa, &wa) && libAICA_DebugGetRTC(&sb, &wb) && sa == sb && wa == wb;

		Check(ok && a.step == 0 && steps == 128 && rtc, "the DSP stands between two samples (step %u), and it, its program and the RTC read the same twice", a.step);
	}

	{
		callstack::Frame a[8], b[8];
		u32 na = callstack::Unwind(Target::Arm7, a, 8), nb = callstack::Unwind(Target::Arm7, b, 8);
		bool same = na == nb;

		for (u32 i = 0; i < na && same; i++)
			same = a[i].pc == b[i].pc && a[i].sp == b[i].sp && a[i].kind == b[i].kind && a[i].stackOffset == b[i].stackOffset;

		Check(na >= 2 && same && a[0].pc == ArmPc() && a[0].kind == callstack::FrameKind::Pc && a[1].pc == Arm(14) && a[1].kind == callstack::FrameKind::Link,
			"frame 0 of the callstack of the ARM7 is its PC (%08X), frame 1 its R14 (%08X); the callstack reads the same twice", na ? a[0].pc : 0,
			na > 1 ? a[1].pc : 0);
	}

	{
		u32 word = 0;

		Check(mem::Read32(Target::Arm7, ArmPc(), &word) && word == ArmWord(ArmPc()), "and the instruction at the PC is read from the wave memory (%08X)", word);
	}

	CO_END(st_reads.co);
}

/*
	arm7.setreg.*: the register writer of the ARM7 (SoundCPU::DebugSetReg)
*/
namespace {

// The register that holds index now, by the rules of the core: R8 to R14 and the SPSR of the active mode are
// live, the banks of the other modes are slots, and 21..25 are R8 to R12 of the modes other than FIQ
u32 Storage(u32 index, u32 mode)
{
	if (index >= 21 && index <= 25)
		return (mode == 0x11 ? (u32)R8_FIQ : 8) + (index - 21);

	switch (mode)
	{
	case 0x10:
	case 0x1F:
		if (index == R13_USR || index == R14_USR)
			return index - R13_USR + 13;
		break;
	case 0x11:
		if (index >= R8_FIQ && index <= R14_FIQ)
			return index - R8_FIQ + 8;
		if (index == SPSR_FIQ)
			return RN_SPSR;
		break;
	case 0x12:
		if (index == R13_IRQ || index == R14_IRQ)
			return index - R13_IRQ + 13;
		if (index == SPSR_IRQ)
			return RN_SPSR;
		break;
	case 0x13:
		if (index == R13_SVC || index == R14_SVC)
			return index - R13_SVC + 13;
		if (index == SPSR_SVC)
			return RN_SPSR;
		break;
	case 0x17:
		if (index == R13_ABT || index == R14_ABT)
			return index - R13_ABT + 13;
		if (index == SPSR_ABT)
			return RN_SPSR;
		break;
	case 0x1B:
		if (index == R13_UND || index == R14_UND)
			return index - R13_UND + 13;
		if (index == SPSR_UND)
			return RN_SPSR;
		break;
	}

	return index;
}

// Writes every register but the PC and the CPSR, one at a time, and puts it back. Returns how many of the
// writes read back in every row that shows the register, and left every other row alone
u32 WriteEach(u32* count)
{
	const u32 mode = ArmMode();
	u32 good = 0;

	*count = 0;

	for (u32 index = 0; index < kArmRegs; index++)
	{
		if (index == 15 || index == RN_CPSR)
			continue;

		const ArmRegs before = ReadArm();
		const u32 value = 0xA5C30000 ^ (index * 0x01010101) ^ before.r[index];

		(*count)++;

		if (!Scpu()->DebugSetReg(index, value))
			continue;

		const ArmRegs after = ReadArm();
		bool ok = true;

		for (u32 row = 0; row < kArmRegs; row++)
		{
			const bool same = Storage(row, mode) == Storage(index, mode);

			if (after.r[row] != (same ? value : before.r[row]))
				ok = false;
		}

		if (!Scpu()->DebugSetReg(index, before.r[index]) || ArmDiffers(ReadArm(), before) != kArmRegs)
			ok = false;

		if (ok)
			good++;
	}

	return good;
}

struct {
	Co co;
	ArmRegs saved;
	u32 n;
} st_set;

// A stop inside a burst, in front of the first instruction of the loop, with no request of the AICA waiting:
// what the test writes lets no FIQ in
bool QuietSuspended(const emu::RunState& stop)
{
	static Co co;
	static u32 n;

	CO_BEGIN(co);

	for (n = 0; n < 400; n++)
	{
		while (!SuspendAt(stop, kMain))
			CO_YIELD(co);

		if (!ArmInService())
			break;
	}

	ArmBps({ });

	CO_END(co);
}

}

BOUNDED_GUEST_TEST(arm7_setreg_classes, "arm7.setreg.classes")
{
	u32 count, good;

	CO_BEGIN(st_set.co);

	while (!Settle(stop))
		CO_YIELD(st_set.co);

	while (!QuietSuspended(stop))
		CO_YIELD(st_set.co);

	st_set.saved = ReadArm();

	Check(ArmMode() == 0x13 && !ArmInService(), "the loop of the guest runs in SVC mode; the stop is inside a burst");

	good = WriteEach(&count);
	Check(good == count && count == kArmRegs - 2,
		"each of the %u registers but the PC and the CPSR is written and put back: %u of them read back in every row that shows the register (the live one for "
		"the bank of SVC mode), with every other row as it was", count, good);

	// the same with the core in FIQ mode, where R8 to R12 are banked as well
	Check(SetArm(RN_CPSR, (st_set.saved.r[RN_CPSR] & ~0x1Fu) | 0x11) && ArmMode() == 0x11, "the CPSR is written with the mode of the FIQ");

	good = WriteEach(&count);
	Check(good == count, "in FIQ mode: %u of %u", good, count);

	Check(SetArm(RN_CPSR, st_set.saved.r[RN_CPSR]) && ArmDiffers(ReadArm(), st_set.saved) == kArmRegs,
		"the CPSR written back: every register reads as at the stop");

	// what is no register
	Check(!Scpu()->DebugSetReg(kArmRegs, 1) && !Scpu()->DebugSetReg(RN_PSR_FLAGS, 1) && !Scpu()->DebugSetReg(R15_ARM_NEXT, 1) && !Scpu()->DebugSetReg(INTR_PEND, 1)
		&& !Scpu()->DebugSetReg(CYCL_CNT, 1) && !Scpu()->DebugSetReg(1000, 1) && ArmDiffers(ReadArm(), st_set.saved) == kArmRegs,
		"an index that is no register of the debugger is refused, and nothing changes");

	CO_END(st_set.co);
}

BOUNDED_GUEST_TEST(arm7_setreg_cpsr, "arm7.setreg.cpsr")
{
	ArmRegs regs, pre;
	u32 refused, differs, c1;
	const u32 mine = 0x600000D2;

	CO_BEGIN(st_set.co);

	while (!Settle(stop))
		CO_YIELD(st_set.co);

	while (!QuietSuspended(stop))
		CO_YIELD(st_set.co);

	st_set.saved = ReadArm();
	Check(ArmMode() == 0x13, "the loop of the guest runs in SVC mode, a mode with an SPSR");

	// a CPSR whose mode bits are no mode
	refused = 0;

	for (u32 mode = 0; mode < 32; mode++)
	{
		if (mode == 0x10 || mode == 0x11 || mode == 0x12 || mode == 0x13 || mode == 0x17 || mode == 0x1B || mode == 0x1F)
			continue;

		if (!Scpu()->DebugSetReg(RN_CPSR, (st_set.saved.r[RN_CPSR] & ~0x1Fu) | mode) && ArmDiffers(ReadArm(), st_set.saved) == kArmRegs)
			refused++;
	}

	Check(refused == 25, "a CPSR with mode bits that are no mode is refused, and nothing changes: %u of the 25 other values of the five bits", refused);

	// what is stored: N Z C V, I, F and the mode
	Check(SetArm(RN_CPSR, 0xFFFFFFF3) && Arm(RN_CPSR) == 0xF00000D3, "the CPSR written with FFFFFFF3 reads back %08X: the value under F00000DF", Arm(RN_CPSR));
	Check(SetArm(RN_CPSR, 0x0FFFFF33 & ~0xC0u) && Arm(RN_CPSR) == 0x00000013, "written with 0FFFFF33 it reads back %08X", Arm(RN_CPSR));
	Check(SetArm(RN_CPSR, st_set.saved.r[RN_CPSR]) && ArmDiffers(ReadArm(), st_set.saved) == kArmRegs,
		"written back, every register reads as at the stop: from a mode with an SPSR a CPSR written and written back leaves every row as it read");

	// The mode switch, from a mode with an SPSR into another: the banks swap, and the SPSR is that of the mode
	// that is entered
	Check(SetArm(RN_CPSR, (st_set.saved.r[RN_CPSR] & ~0x1Fu) | 0x12), "the CPSR is written with IRQ mode");
	regs = ReadArm();

	Check(ArmMode() == 0x12 && regs.r[13] == st_set.saved.r[R13_IRQ] && regs.r[14] == st_set.saved.r[R14_IRQ] && regs.r[R13_SVC] == st_set.saved.r[13]
		&& regs.r[R14_SVC] == st_set.saved.r[14], "R13 and R14 are those of the IRQ bank, and the bank of SVC mode has what was live");
	Check(regs.r[RN_SPSR] == st_set.saved.r[SPSR_IRQ] && regs.r[SPSR_SVC] == st_set.saved.r[RN_SPSR],
		"the row SPSR reads what the row SPSR_irq read in front of the write (%08X), and SPSR_svc has what was live", regs.r[RN_SPSR]);

	differs = kArmRegs;

	for (u32 i = 0; i < 13; i++)
	{
		if (regs.r[i] != st_set.saved.r[i] && differs == kArmRegs)
			differs = i;
	}

	Check(differs == kArmRegs && regs.r[15] == st_set.saved.r[15], "R0 to R12 and the PC are not banked there");

	Check(SetArm(RN_CPSR, (st_set.saved.r[RN_CPSR] & ~0x1Fu) | 0x11), "the CPSR is written with FIQ mode");
	regs = ReadArm();
	differs = kArmRegs;

	for (u32 i = 0; i < 5; i++)
	{
		if ((regs.r[8 + i] != st_set.saved.r[R8_FIQ + i] || regs.r[21 + i] != st_set.saved.r[8 + i]) && differs == kArmRegs)
			differs = i;
	}

	Check(ArmMode() == 0x11 && differs == kArmRegs && regs.r[13] == st_set.saved.r[R13_FIQ] && regs.r[RN_SPSR] == st_set.saved.r[SPSR_FIQ],
		"R8 to R14 and the SPSR are those of the FIQ bank, and the rows of the other modes' R8 to R12 have what was live");

	Check(SetArm(RN_CPSR, st_set.saved.r[RN_CPSR]) && ArmDiffers(ReadArm(), st_set.saved) == kArmRegs,
		"written back to SVC mode, every register reads as at the stop (the first that differs: %u)", ArmDiffers(ReadArm(), st_set.saved));

	/*
		System mode, which has no SPSR. Leaving it, the mode switch copies the CPSR as it was into the live SPSR;
		entering a mode that has an SPSR, it loads that mode's over it. Both writes are made from system mode
	*/
	Check(SetArm(RN_CPSR, (st_set.saved.r[RN_CPSR] & ~0x1Fu) | 0x1F) && ArmMode() == 0x1F, "the CPSR is written with system mode");
	Check(Arm(RN_SPSR) == st_set.saved.r[RN_SPSR], "from a mode with an SPSR into system mode: the row SPSR reads what it read in front of the write (%08X)",
		Arm(RN_SPSR));

	c1 = Arm(RN_CPSR);
	Check(SetArm(RN_CPSR, c1) && Arm(RN_CPSR) == c1 && Arm(RN_SPSR) == c1,
		"written again with the value it has (%08X): the row SPSR reads the CPSR as it was before that write (%08X)", c1, Arm(RN_SPSR));

	Check(SetArm(SPSR_IRQ, mine) && Arm(SPSR_IRQ) == mine, "SPSR_irq is written with a value of the test's (%08X)", mine);
	pre = ReadArm();

	Check(SetArm(RN_CPSR, (c1 & ~0x1Fu) | 0x12) && ArmMode() == 0x12, "the CPSR is written with IRQ mode, from system mode");
	Check(Arm(RN_SPSR) == mine && Arm(SPSR_IRQ) == mine, "the row SPSR reads the value that was put into SPSR_irq (%08X), and so does the row SPSR_irq",
		Arm(RN_SPSR));

	Check(SetArm(RN_CPSR, c1) && ArmMode() == 0x1F, "and back to system mode");
	regs = ReadArm();
	differs = kArmRegs;

	for (u32 i = 0; i < kArmRegs; i++)
	{
		if (i != RN_SPSR && regs.r[i] != pre.r[i] && differs == kArmRegs)
			differs = i;
	}

	Check(regs.r[RN_SPSR] == mine && regs.r[SPSR_IRQ] == mine, "the row SPSR reads that value still (%08X), and SPSR_irq has it", regs.r[RN_SPSR]);
	Check(differs == kArmRegs, "every other row reads behind the pair as in front of it (the first that differs: %u)", differs);

	// the restore
	Check(SetArm(SPSR_IRQ, st_set.saved.r[SPSR_IRQ]) && SetArm(RN_CPSR, st_set.saved.r[RN_CPSR]), "SPSR_irq and the CPSR are put back");

	for (u32 i = 0; i < kArmRegs; i++)
	{
		if (i != RN_CPSR && Arm(i) != st_set.saved.r[i])
			SetArm(i, st_set.saved.r[i]);
	}

	Check(ArmDiffers(ReadArm(), st_set.saved) == kArmRegs, "every register reads as at the stop");

	CO_END(st_set.co);
}

BOUNDED_GUEST_TEST(arm7_setreg_pc, "arm7.setreg.pc")
{
	CO_BEGIN(st_set.co);

	while (!Settle(stop))
		CO_YIELD(st_set.co);

	while (!QuietSuspended(stop))
		CO_YIELD(st_set.co);

	st_set.saved = ReadArm();

	// stored aligned, as every write of R15 by the core is
	Check(SetArm(15, kPlain + 3) && ArmPc() == kPlain, "the PC written with %08X reads back %08X: bits 0 and 1 are not stored", kPlain + 3, ArmPc());
	Check(!arm7::test::GetState().exempt, "the breakpoint state heard of it: no exemption is armed");

	if (!StepArm())
		break;
	CO_YIELD(st_set.co);

	Check(stop.cause == StopCause::Step && ArmPc() == kPlain + 4 && Arm(0) == AicaReg16(kTIMA) && Arm(3) == st_set.saved.r[3],
		"a step from the written PC executes the instruction there, and no other: the ARM7 at %08X, R0 has the timer's count, the loop has not counted",
		ArmPc());

	// the top of the address space: the fetch reads under the mask of the wave memory
	Check(SetArm(15, 0xFFFFFFFF) && ArmPc() == 0xFFFFFFFC, "the PC written with FFFFFFFF reads back %08X", ArmPc());

	if (!StepArm())
		break;
	CO_YIELD(st_set.co);

	Check(stop.cause == StopCause::Step && (ArmPc() == 0 || ArmPc() == 4 || ArmPc() == kVector),
		"a step from there does not end the process: it executes the word at the top of the wave memory (%08X), and the ARM7 stands at %08X", ArmWord(0xFFFFFFFC),
		ArmPc());

	// back into the loop
	for (u32 i = 0; i < kArmRegs; i++)
	{
		if (i != RN_CPSR && Arm(i) != st_set.saved.r[i])
			SetArm(i, st_set.saved.r[i]);
	}

	Check(ArmPc() == kMain, "the registers are put back");

	CO_END(st_set.co);
}

/*
	arm7.notice: NOTICE_ARM7_FALLBACK follows the backend that executes the ARM7. Where that is the interpreter,
	which is every x86-64 build, the notice is never set, armed or not. (Where it is a recompiler, the notice is
	set while dbg_arm7_armed is not 0 and in the stop that ends a step, a resume or a run on the debug loop:
	the test is written for both, and has run on the interpreter only)
*/
namespace {

struct {
	Co co;
	u32 any;
} st_notice;

u32 Arm7Notice(u32 notice)
{
	return notice & NOTICE_ARM7_FALLBACK;
}

}

BOUNDED_GUEST_TEST(arm7_notice, "arm7.notice")
{
	const bool recompiler = Scpu()->DebugIsRecompiler();
	const u32 want = recompiler ? (u32)NOTICE_ARM7_FALLBACK : 0;
	std::vector<Seen> list;

	CO_BEGIN(st_notice.co);

	while (!Settle(stop))
		CO_YIELD(st_notice.co);

#if HOST_CPU == CPU_X64
	Check(!recompiler, "the backend that executes the ARM7 is its interpreter on this host");
#endif
	Note("the ARM7 runs on its %s", recompiler ? "recompiler" : "interpreter");

	Check(Arm7Notice(emu::GetRunState().notice) == 0, "nothing of the ARM7 is armed: no notice");

	ArmBps({ { 95, kMain } });
	Check(dbg_arm7_armed != 0 && Arm7Notice(emu::GetRunState().notice) == want,
		"a listed breakpoint arms the debug loop, and the published state has the notice at once where the backend is a recompiler (%u)",
		Arm7Notice(emu::GetRunState().notice));

	TakeSeen();

	if (!Resume())
		break;
	CO_YIELD(st_notice.co);

	Check(stop.cause == StopCause::Breakpoint && Arm7Notice(stop.notice) == want, "the stop of the breakpoint: notice %u", Arm7Notice(stop.notice));

	// a step: nothing is armed when its stop is published, and the notice is the flag's
	ArmBps({ });
	Check(dbg_arm7_armed == 0 && Arm7Notice(emu::GetRunState().notice) == 0, "the breakpoint removed: nothing is armed, no notice");

	if (!StepArm())
		break;
	CO_YIELD(st_notice.co);

	Check(stop.cause == StopCause::Step && dbg_arm7_armed == 0 && Arm7Notice(stop.notice) == want,
		"the stop of a step, with nothing armed any more: notice %u", Arm7Notice(stop.notice));

	// a run with nothing armed: the burst is run on by the debug loop, the run behind it is the backend's
	while (!PauseAfter(stop, 3))
		CO_YIELD(st_notice.co);

	list = TakeSeen();
	st_notice.any = 0;

	for (const Seen& s : list)
		st_notice.any |= Arm7Notice(s.notice);

	Check(stop.cause == StopCause::Pause && Arm7Notice(stop.notice) == want && st_notice.any == want,
		"the stop of the run that ran the suspended burst on: notice %u; of all that was published in this test: %u", Arm7Notice(stop.notice), st_notice.any);

	while (!PauseAfter(stop, 2))
		CO_YIELD(st_notice.co);

	Check(stop.cause == StopCause::Pause && Arm7Notice(stop.notice) == 0, "the stop of a run in which the debug loop never executed: no notice");

	CO_END(st_notice.co);
}

/*
	arm7.grace: the grace expiry on a suspended burst. The core clears the active mask first and resumes second:
	with the mask cleared the resume runs through the breakpoint that suspended the burst, and through every
	later arrival
*/
namespace {

struct {
	Co co;
	Counts c;
	u32 checks;
	u32 phases;			// the ARM phases that had run to their end at the stop behind a burst
} st_gr;

}

BOUNDED_GUEST_TEST(arm7_grace, "arm7.grace")
{
	std::vector<Seen> list;
	Counts c;

	CO_BEGIN(st_gr.co);

	while (!Settle(stop))
		CO_YIELD(st_gr.co);

	st_gr.phases = ReadCounts().n[1];

	while (!SuspendAt(stop, kMain))
		CO_YIELD(st_gr.co);

	// a few instructions into the sample, so that the suspended ARM phase has used ticks
	if (!StepArm())
		break;
	CO_YIELD(st_gr.co);

	emu::SetActiveMask(0);
	Check(dbg_arm7_armed == 0 && arm7::test::GetState().listed == 1, "the mask is cleared while the burst is suspended: the breakpoint is listed, nothing is armed");

	st_gr.c = ReadCounts();
	st_gr.checks = arm7::test::checks;
	TakeSeen();

	while (!PauseAfter(stop, 3))
		CO_YIELD(st_gr.co);

	list = TakeSeen();
	c = ReadCounts();

	Check(stop.cause == StopCause::Pause && SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } }),
		"the resume runs through: published %s", SeenText(list).c_str());
	Check(c.n[0] - st_gr.c.n[0] >= 3 && c.n[3] - st_gr.c.n[3] == c.n[0] - st_gr.c.n[0] + 1 && c.n[1] - st_gr.phases == 32 * (c.n[0] - st_gr.c.n[0] + 1)
		&& arm7::test::checks == st_gr.checks,
		"the suspended burst ran to its end with its tail, %u more bursts ran, and no check was made (%u)", c.n[0] - st_gr.c.n[0],
		arm7::test::checks - st_gr.checks);

	emu::SetActiveMask(kMaskAll);
	ArmBps({ });

	CO_END(st_gr.co);
}

/*
	arm7n.*: the debugger at work on the ARM7 in a run that the sound harness, or an Exit.* key, ends. The sound
	line and the state hash of such a run have to be those of a run nobody touched
	(tests/debugger/guest/neutral, tests/debugger/guest/dsptest/soundlines.py --mode neutral). A test of these
	never returns true: a session of it does not end by itself. Each says what it did in lines its target
	requires, so that a test that armed nothing is not taken for one that changed nothing.

	Their stops behind a burst are those of an event breakpoint on the timers of the AICA, which is a stop
	latched at a slice end: outside a SLEEP wait (the guest does not sleep) a run that goes on from it keeps no
	trace of it. The latch holds back the interrupt acceptance of that slice end, and the leave of the park
	context makes it, in front of the first instruction of the SH4.

	A run that ends in the tick of a stop is another matter, and no test of these takes one there. The thread
	then goes to its stop callback, which prints the exit line, with the latch set and that acceptance owed: it
	is made behind the callback (dc_run of libswirl.cpp, emu::OnThreadExiting). So it is with a check of the
	ARM7 that suspends the burst in the tick of the exit: the burst is whole in front of the callback, and an
	interrupt of the SH4 that is pending there, one that the rest of the burst raised among them, is not
	accepted yet. The `sh4` part of such an exit line is not that of the run nobody touched, whose slice end
	accepted the interrupt before the cpu left its loop. Each test has done its work early: within some hundreds
	of bursts, idlebp with its one stop some 900 bursts in, where the sound line is taken at 2000 and the state
	hash, at 90 vblanks, some 2070 bursts in.
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
	std::atomic<u32> bursts { 0 };
	std::atomic<bool> listed { false };
} n_idle;

// How far the run has to be before the test takes its one stop, in sound bursts: late, and with room for the
// stop in front of the 2000 ticks that end the run of the sound line
const u32 kIdleLate = 900;

}

/*
	arm7n.idlebp: an active breakpoint of the ARM7 on an address that the guest never executes. The debug loop
	runs every ARM phase of the run in place of the backend's Run(), and stops nowhere
*/
BOUNDED_GUEST_TEST(arm7n_idlebp, "arm7n.idlebp")
{
	Counts c;
	u32 bursts, checks;

	CO_BEGIN(n_idle.co);

	emu::SetActiveMask(kMaskAll);
	ArmBps({ { 1, kNever } });

	n_idle.checks = arm7::test::checks;
	n_idle.c = ReadCounts();

	Check(stop.cause == StopCause::Start && dbg_arm7_armed != 0, "at the start of the run a breakpoint of the ARM7 is listed on %08X and active: dbg_arm7_armed is %u",
		kNever, (u32)dbg_arm7_armed);

	// A thread looks at the run, with calls that only read, and lists the event breakpoint when the run is late
	OtherStart([] {
		for (Clock::time_point t0 = Clock::now(); ; )
		{
			emu::Call(emu::CC_READ, [] { n_idle.bursts = ReadCounts().n[0]; });

			if (n_idle.bursts - n_idle.c.n[0] >= kIdleLate)
				break;

			// a run that does not get that far: the breakpoint is not listed, and the wait of the test is cut
			if (MsSince(t0) > kBoundMs)
				return;

			std::this_thread::sleep_for(std::chrono::microseconds(500));
		}

		emu::Call(emu::CC_TABLE, [] {
			EventBp(kNeutralEvent);
			n_idle.listed = true;
		});
	});

	if (!Resume())
		break;
	CO_YIELD(n_idle.co);

	OtherJoin();
	NoEventBp();

	c = ReadCounts();
	bursts = c.n[0] - n_idle.c.n[0];
	checks = arm7::test::checks - n_idle.checks;

	Check(stop.cause == StopCause::Event && stop.breakpointId == kNeutralEvent && n_idle.listed && Suspended() == SOUND_BURST_NONE,
		"late in the run, one stop behind a burst: \"%s\"", StopCauseName(stop.cause));
	Check(dbg_arm7_armed != 0 && bursts >= kIdleLate && checks >= 32 * bursts && c.n[1] - n_idle.c.n[1] == 32 * bursts,
		"dbg_arm7_armed is %u there, and %u checks were made in the %u bursts the scheduler started since the breakpoint was set, at least 32 a burst: "
		"the debug loop ran every sample", (u32)dbg_arm7_armed, checks, bursts);

	NEUTRAL_END(n_idle.co);

	CO_END(n_idle.co);
}

/*
	arm7n.hitresume: breakpoints of the ARM7 that are hit, each resumed. A breakpoint on the loop is reached
	eighty times a burst, so the test removes it at each stop and sets it again, on the loop and on the FIQ
	handler in turn, at a stop behind a later burst
*/
namespace {

struct {
	Co co;
	u32 loop, fiq, bursts, other;
	u32 lastBurst;
	bool next;
} n_hit;

}

BOUNDED_GUEST_TEST(arm7n_hitresume, "arm7n.hitresume")
{
	u32 burst;

	CO_BEGIN(n_hit.co);

	emu::SetActiveMask(kMaskAll);
	EventBp(kNeutralEvent);

	n_hit.loop = n_hit.fiq = n_hit.bursts = n_hit.other = 0;
	n_hit.lastBurst = ~0u;
	n_hit.next = false;

	for (;;)
	{
		if (!Resume())
			return false;
		CO_YIELD(n_hit.co);

		if (stop.cause == StopCause::Breakpoint && Suspended() == SOUND_BURST_ARM7 && (stop.breakpointId == 1 || stop.breakpointId == 2)
			&& ArmPc() == (stop.breakpointId == 1 ? kMain : kFiq))
		{
			// a hit: the breakpoint goes, and the resume runs the burst on
			burst = ReadCounts().n[0];

			(stop.breakpointId == 1 ? n_hit.loop : n_hit.fiq)++;

			if (burst != n_hit.lastBurst)
				n_hit.bursts++;
			n_hit.lastBurst = burst;

			ArmBps({ });
			continue;
		}

		if (stop.cause != StopCause::Event || Suspended() != SOUND_BURST_NONE)
		{
			n_hit.other++;
			continue;
		}

		// behind a burst
		if (n_hit.loop + n_hit.fiq >= 200)
			break;

		n_hit.next = !n_hit.next;
		ArmBps({ { n_hit.next ? 1u : 2u, n_hit.next ? kMain : kFiq } });
	}

	NoEventBp();
	ArmBps({ });

	Check(n_hit.loop + n_hit.fiq == 200 && n_hit.loop >= 50 && n_hit.fiq >= 50 && n_hit.bursts >= 100 && n_hit.other == 0,
		"%u stops at breakpoints of the ARM7, each resumed: %u on a_main_loop and %u on a_fiq, in %u different bursts (%u stops of another kind)",
		n_hit.loop + n_hit.fiq, n_hit.loop, n_hit.fiq, n_hit.bursts, n_hit.other);

	NEUTRAL_END(n_hit.co);

	CO_END(n_hit.co);
}

/*
	arm7n.steps: single steps of the ARM7, in runs that begin at a stop with no burst suspended, go through the
	next burst instruction by instruction to its end, and are followed by a resume. Four kinds of step are
	counted, and the test goes on until each was taken often enough:

	  - from a stop with no burst suspended: the SH4 runs to the next burst, whose first instruction is the step;
	  - across a time step of the AICA: the instruction was the last of its sample;
	  - across the end of a burst: the instruction was the last of its burst, and the tail ran;
	  - of the msr that ends the F window of the loop, ending on the FIQ vector, inside one ARM phase: the count
	    of the ARM phases reads the same at the stop in front of the step that arrived on the msr, at the stop on
	    the msr and at the stop of the step. Then the request that waited was entered by the loop that goes on
	    with a suspended sample. (A step of that msr that reaches the vector through the end of a phase is the
	    entry of a new phase, which says nothing of that loop, and is not counted.)
*/
namespace {

struct {
	Co co;
	u32 fromStop, acrossSample, acrossEnd, fiqBehind;
	u32 steps, runs, bad, other;
	u32 inRun;				// steps of this run so far
	bool endless;			// a run of kStepsMax steps did not get to the end of its burst
	u32 phases[2];			// the ARM phases that had run to their end at the stop before the last step, and at this one
	u32 pc;
	u32 wasSuspended;
	Counts c;
} n_steps;

bool StepsDone()
{
	return n_steps.fromStop >= 20 && n_steps.acrossSample >= 20 && n_steps.acrossEnd >= 20 && n_steps.fiqBehind >= 5;
}

}

BOUNDED_GUEST_TEST(arm7n_steps, "arm7n.steps")
{
	Counts c;

	CO_BEGIN(n_steps.co);

	emu::SetActiveMask(kMaskAll);

	n_steps.fromStop = n_steps.acrossSample = n_steps.acrossEnd = n_steps.fiqBehind = 0;
	n_steps.steps = n_steps.runs = n_steps.bad = n_steps.other = 0;
	n_steps.endless = false;

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

		if (StepsDone())
			break;

		// a run of steps, to the end of the next burst
		n_steps.runs++;
		n_steps.inRun = 0;
		n_steps.phases[0] = n_steps.phases[1] = 0;

		do
		{
			n_steps.c = ReadCounts();
			n_steps.pc = ArmPc();
			n_steps.wasSuspended = Suspended();
			n_steps.phases[0] = n_steps.phases[1];
			n_steps.phases[1] = n_steps.c.n[1];

			if (!StepArm())
				return false;
			CO_YIELD(n_steps.co);

			c = ReadCounts();
			n_steps.steps++;

			if (stop.cause != StopCause::Step)
			{
				n_steps.bad++;
				break;
			}

			if (n_steps.wasSuspended == SOUND_BURST_NONE)
				n_steps.fromStop++;
			else if (Suspended() == SOUND_BURST_NONE)
				n_steps.acrossEnd++;
			else if (c.n[2] != n_steps.c.n[2])
				n_steps.acrossSample++;

			if (n_steps.pc == kUnmask && ArmPc() == kVector && n_steps.inRun >= 1 && n_steps.phases[0] == n_steps.phases[1] && n_steps.phases[1] == c.n[1])
				n_steps.fiqBehind++;

			n_steps.inRun++;
		} while (Suspended() != SOUND_BURST_NONE && n_steps.inRun < kStepsMax);

		// Steps that do not get to the end of their burst end no ARM phase: the test has failed, and leaves the
		// run to itself (a loop of steps in which no emulated time passes would be ended by nothing but the
		// watchdog of the runner)
		if (Suspended() != SOUND_BURST_NONE && n_steps.inRun >= kStepsMax)
		{
			n_steps.endless = true;
			break;
		}
	}

	Check(!n_steps.endless, "each of the %u runs of steps got to the end of its burst (the last one in %u steps)", n_steps.runs, n_steps.inRun);
	Check(n_steps.bad == 0 && n_steps.other == 0, "%u single steps of the ARM7 in %u runs, each run followed by a resume: every stop was a step's", n_steps.steps,
		n_steps.runs);
	Check(n_steps.fromStop >= 20, "%u steps from a stop with no burst suspended", n_steps.fromStop);
	Check(n_steps.acrossSample >= 20, "%u steps across a time step of the AICA", n_steps.acrossSample);
	Check(n_steps.acrossEnd >= 20, "%u steps across the end of a burst", n_steps.acrossEnd);
	Check(n_steps.fiqBehind >= 5, "%u steps of the msr that ends the F window which ended on the FIQ vector inside one ARM phase", n_steps.fiqBehind);

	NEUTRAL_END(n_steps.co);

	CO_END(n_steps.co);
}

/*
	arm7n.hoststop: the completion of a suspended burst in dc_run, held to the run nobody touched. Breakpoints of
	the ARM7 are hit, on thirteen addresses in turn, and at each of sixty hits the host stops the machine while
	the thread is parked on the suspended burst, and starts it again. The thread that ends runs the rest of the
	burst in front of its stop callback, without checks, from where the breakpoint left it: inside a sample,
	with some of its ticks used. The next thread, which has no start park, runs on.

	The tests of the session hold this path to its counts (arm7.hoststop.suspended, arm7.exit.suspended,
	arm7.sameslice.*): that the rest of the ARM phases ran, each with its time step, and the tail once. What
	those phases executed is held here, by the sound line and the state hash: a completion that started the
	suspended sample again, or dropped the rest of it, moves both.

	And what it does with a FIQ that becomes enterable in the rest of its sample. On two of the addresses, the last
	plain instruction of the F window of the loop and the msr that ends it, a hit is taken only where a request of
	the AICA is latched and F is set: the request waits behind the window, and the completion executes the msr and
	enters the FIQ in the loop that goes on with the suspended sample, where a plain run enters it in its own
	sample. (Unless the msr is the last instruction of its sample: then the entry is that of a new phase.) A loop
	that entered such a FIQ one executed instruction late moves the line, through the handler's fold of lr. Other
	hits on those two addresses are resumed with the breakpoint kept; the test takes at least five such stops of
	the host, and a hit of its own when a cycle has seen kFiqTries of them without the request.

	The sixty are over within some hundreds of bursts. The stops of the host are taken with the thread parked,
	not in the tick of the exit key
*/
namespace {

// in the loop, in its F window, on the msr that ends it, around the bl and in the leaf, in the FIQ handler, on
// the vector, and on the store that raises the AICA's interrupt to the SH4
const u32 kHostAddrs[] = { kMain, kMain + 4, kPlain, kLastOfWindow, kUnmask, kBl, kBehindBl, kLeaf, kLeaf + 4, kFiq, kFiq + 4, kVector, kRaise };
const u32 kHostAddrCount = sizeof(kHostAddrs) / sizeof(kHostAddrs[0]);
const u32 kHostStops = 60;
const u32 kHostBpId = 100;

// The stops of the host where the rest of the sample has a FIQ to enter, at least; and the hits of one cycle on the
// window that are resumed for it, at most
const u32 kHostFiqStops = 5;
const u32 kFiqTries = 4000;

struct {
	Co co;
	u32 hits, cycles, other, bad;
	u32 fiq;				// stops of the host with a FIQ to enter in the rest of the sample
	u32 passed;				// hits on the window resumed for that, in this cycle
	u32 passedAll;
	u32 at[kHostAddrCount];
	bool counted;			// whole has the counts of a stop behind a burst
	Counts whole;
	std::atomic<u32> callbacks { 0 }, callbacksSuspended { 0 };
	std::atomic<bool> failed { false };
} n_host;

// A hit on the last plain instruction of the F window, or on the msr that ends it, with a request of the AICA latched
// and F set: the rest of the sample has the FIQ to enter behind the msr
bool NeutralHostFiqWaits()
{
	return (ArmPc() == kLastOfWindow || ArmPc() == kUnmask) && ArmInService() && (Arm(RN_CPSR) & 0x40) != 0;
}

// the stop callback of the host: the burst is whole in front of it
void NeutralHostStopped()
{
	n_host.callbacks++;

	if (Suspended() != SOUND_BURST_NONE)
		n_host.callbacksSuspended++;
}

void NeutralHostNext()
{
	const u32 which = n_host.cycles % kHostAddrCount;

	ArmBps({ { kHostBpId + which, kHostAddrs[which] } });
}

// At a stop behind a burst: was every burst since the stop behind a burst before this one whole, 32 ARM phases with
// their time steps and one tail, whichever thread ran it to its end?
bool NeutralHostWhole()
{
	const Counts c = ReadCounts();
	const u32 bursts = c.n[0] - n_host.whole.n[0];
	const bool ok = !n_host.counted
		|| (c.n[1] - n_host.whole.n[1] == 32 * bursts && c.n[2] - n_host.whole.n[2] == 32 * bursts && c.n[3] - n_host.whole.n[3] == bursts);

	n_host.whole = c;
	n_host.counted = true;

	return ok;
}

}

BOUNDED_GUEST_TEST(arm7n_hoststop, "arm7n.hoststop")
{
	CO_BEGIN(n_host.co);

	emu::SetActiveMask(kMaskAll);
	EventBp(kNeutralEvent);

	n_host.hits = n_host.cycles = n_host.other = n_host.bad = 0;
	n_host.fiq = n_host.passed = n_host.passedAll = 0;
	n_host.counted = false;
	memset(n_host.at, 0, sizeof(n_host.at));

	for (;;)
	{
		if (!Resume())
			return false;
		CO_YIELD(n_host.co);

		if (stop.cause == StopCause::Breakpoint && Suspended() == SOUND_BURST_ARM7 && stop.breakpointId >= kHostBpId
			&& stop.breakpointId < kHostBpId + kHostAddrCount && n_host.cycles < kHostStops)
		{
			// on the window, a hit with no FIQ waiting behind it is resumed, with the breakpoint kept (the test holds
			// what it keeps across a stop in n_host only)
			if ((kHostAddrs[stop.breakpointId - kHostBpId] == kLastOfWindow || kHostAddrs[stop.breakpointId - kHostBpId] == kUnmask) && !NeutralHostFiqWaits()
				&& n_host.passed < kFiqTries)
			{
				n_host.passed++;
				n_host.passedAll++;
				continue;
			}

			if (NeutralHostFiqWaits())
				n_host.fiq++;

			// a hit: the breakpoint goes, and the host stops the machine while this thread is parked on the burst
			n_host.hits++;
			n_host.passed = 0;
			n_host.at[stop.breakpointId - kHostBpId]++;

			ArmBps({ });

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

			if (n_host.cycles < kHostStops)
				NeutralHostNext();

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

		NeutralHostNext();
	}

	NoEventBp();
	ArmBps({ });

	Check(n_host.cycles == kHostStops && n_host.hits == kHostStops && n_host.bad == 0 && n_host.other == 0 && !n_host.failed,
		"the host stopped the machine %u times while the thread was parked on a burst that a breakpoint of the ARM7 had suspended, and started it again: the next "
		"thread ran on to a stop behind a burst, with every burst whole (%u did not; %u stops of another kind)", n_host.cycles, n_host.bad, n_host.other);
	Check(n_host.callbacks == kHostStops && n_host.callbacksSuspended == 0, "each of the %u stop callbacks found the burst whole (%u did not)",
		n_host.callbacks.load(), n_host.callbacksSuspended.load());
	Check(n_host.fiq >= kHostFiqStops, "%u of the stops of the host were taken where the rest of the sample had a FIQ to enter: on the last plain instruction of the "
		"F window or on the msr that ends it, with a request of the AICA latched and F set (at least %u; %u hits there were resumed for them)", n_host.fiq, kHostFiqStops,
		n_host.passedAll);
	Note("stops by address: %u %u %u %u %u %u %u %u %u %u %u %u %u; the last one in burst %u of the run", n_host.at[0], n_host.at[1], n_host.at[2], n_host.at[3],
		n_host.at[4], n_host.at[5], n_host.at[6], n_host.at[7], n_host.at[8], n_host.at[9], n_host.at[10], n_host.at[11], n_host.at[12], ReadCounts().n[0]);

	NEUTRAL_END(n_host.co);

	CO_END(n_host.co);
}
