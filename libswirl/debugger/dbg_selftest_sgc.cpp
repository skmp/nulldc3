/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of the SGC frame capture (dbg_sgc.cpp), of the outputs the sound generator keeps for it
	(hw/aica/sgc_if.cpp), of the binary path of the core and its deferred response of state.recordSgcFrames
	(dbg_core.cpp), and of the two cancels of run control (dbg_emu.cpp).

	    nulldc3 --config config:Headless=yes , audio:backend=none , Debugger:SelfTest=sgc,mem.binary tests/debugger/guest/dsptest/dsptest.elf

	The guest is the sounding guest of the tree (tests/debugger/guest/dsptest, README.md there): an SH4 program
	that counts in a loop, an ARM7 program of its own, two channels keyed on and a DSP program; the addresses come
	from dsptest_labels.inc. Every test is a guest test: it stands where a client stands and is handed every stop.
	A test begins by bringing the guest to where a session finds it after its boot (Settle).

	A frame is one sample of the AICA, taken behind the sweep of that sample and in front of its DSP phase. The
	AICA is clocked whatever ARMRST says: a capture records whether the ARM7 is held or not.

	Two ways in. A test arms a capture as the closures of the core do (dbg::sgc::Arm with emu::Resume at a stop,
	Arm alone in a call while running), or it goes through the core: the session has no server, so the test
	starts the core thread without one (core::StartForTest) and hands it requests as a client would
	(core::TextForTest); what the core sends that client - a response, a deferred response, a binary frame - comes
	to the sink of the test (core::SetSendSinkForTest). The selftest's own requests (client 0,
	core::HandleRequestText) have no connection: their binary frames come to the sink as well.

	What another thread of the debugger does (a call), and what the host does (a stop, a start), is done by a
	thread the test starts: "the other thread". What has to happen at a moment of the emulated machine is done by
	a scheduler callback of the tests ("the hand"), which is registered behind the AICA's and so runs behind it in
	its tick.

	Two tests are not part of "sgc", because a session of theirs does not end with the summary of the driver
	(sgcn.*, at the end of this file): they are what tests/debugger/guest/neutral holds the sound line and the
	state hash of a run against.
*/

// json.hpp comes in front of every tree header
#include "deps/nlohmann/json.hpp"

#include "dbg_selftest.h"
#include "dbg_core.h"
#include "dbg_core_private.h"
#include "dbg_dsp.h"
#include "dbg_dsp_private.h"
#include "dbg_mem.h"
#include "dbg_sgc.h"
#include "dbg_sgc_private.h"
#include "dbg_sh4.h"

#include "libswirl.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/arm7/SoundCPU.h"
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

// hw/aica/sgc_if.cpp: the pair the mix handed to the audio stream last, left and right (in the save state)
extern s16 pl, pr;

namespace dbg { namespace emu { namespace test {

// dbg_emu.cpp: invoked behind the listener with every state that is published
extern void (*observer)(const RunState& state);

// dbg_emu.cpp: the threads of the session start without a start park
extern bool plainStart;

} } }

namespace {

using namespace dbg;
using nlohmann::json;
using selftest::Check;
using selftest::Note;

typedef std::chrono::steady_clock Clock;

const u32 kResetVector = 0xA0000000;

// The ARM7 program of the guest
const u32 kMain = DSPTEST_A_MAIN_LOOP;		// the first instruction of its loop

// The DSP program of the guest
const u32 kLast = 127;
const u32 kSite = 5;

const u32 kMaskAll = (1u << (u32)Category::Events) | (1u << (u32)Category::Sh4) | (1u << (u32)Category::Arm7) | (1u << (u32)Category::Dsp);

// The registers of the AICA, as offsets into its register file
const u32 kEFREG0 = 0x2000;		// EFSDL / EFPAN of the DSP's output 0; output 1 at + 4
const u32 kChannel1 = 0x80;		// the second channel
const u32 kDIPAN = 0x24;		// DISDL / DIPAN of a channel

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

DSP* Dsp()
{
	return sh4_cpu->GetA0H<DSP>(A0H_DSP);
}

u32 ArmPc()
{
	u32 value = 0xDEADBEEF;

	Scpu()->DebugGetReg(15, &value);
	return value;
}

bool ArmClocked()
{
	return Scpu()->DebugEnabled();
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

u32 DspStep()
{
	DspDebugState state;

	memset(&state, 0xEE, sizeof(state));

	if (!Dsp()->DebugGetState(&state))
		return 999;

	return state.step;
}

/*
	The sample a burst stands in: 0 between two bursts, and in a burst the sample whose DSP phase or ARM phase it
	stands in, or the next one at the end of a sample (dbg_selftest_dsp.cpp has the same)
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
	Everything that is published, in the order it was, since the test looked last (TakeSeen). The list has an end
*/
struct Seen {
	Lifetime lifetime;
	u32 stopId;
	StopCause cause;
	u32 breakpointId;
};

const size_t kSeenMax = 20000;

std::mutex seen_lock;
std::vector<Seen> seen;

void Observer(const emu::RunState& state)
{
	std::lock_guard<std::mutex> lock(seen_lock);

	if (seen.size() < kSeenMax)
		seen.push_back({ state.lifetime, state.stopId, state.cause, state.breakpointId });
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
	The bound of a wait, as the selftests of the ARM7 and of the DSP have it (dbg_selftest_dsp.cpp): a thread of the
	session, the watch, cuts a wait that is older than kBoundMs and asks the machine for a stop it can still give, a
	pause, a stop and a start of the host; the test is handed that stop, fails by a check that says so, and ends.
	Every test behind it fails at once with one check. A machine that gives no stop through all of that has the
	watch print the failed check itself and end the process. The last wait of a test that the run ends (sgcn.*,
	NEUTRAL_END) has no bound.
*/
const int kBoundMs = 30000;

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
	The hand: a scheduler callback of the tests, in every slice in which emulated time passes while it is on,
	registered behind the callback of the AICA. Here it asks for a pause once the bursts started have reached
	hand.target: the stop of a run that nothing of the sound side stopped, and the backstop of a test that waits for
	a stop of a capture which a mutation of the capture would never give
*/
struct {
	int id = -1;
	bool on = false;
	u32 target = 0;
	bool asked = false;
} hand;

int HandTick(void*, int, int, int)
{
	if (!hand.on)
		return 0;

	if (!hand.asked && (s32)(ReadCounts().n[0] - hand.target) >= 0)
	{
		hand.asked = true;
		emu::RequestPause();
	}

	return 1;
}

// On the emulator thread, parked: a pause once `bursts` more bursts have started
void HandPauseAfter(u32 bursts)
{
	if (hand.id < 0)
		hand.id = sh4_sched_register(nullptr, 0, HandTick);

	hand.on = true;
	hand.target = ReadCounts().n[0] + bursts;
	hand.asked = false;

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
	emu::SetActiveMask(kMaskAll);
}

/*
	Settle: the guest as a session finds it behind its boot (dbg_selftest_dsp.cpp has the same): the SH4 in its loop,
	the ARM7 clocked and in its own, no breakpoint listed, no sound burst suspended, the run stopped behind a burst in
	which a timer of the AICA overflows. Returns false while it has armed something
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

	if (dbg_sgc_active != 0)
		Check(false, "no capture records when a test begins: dbg_sgc_active is %u", (u32)dbg_sgc_active);

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
	A pause behind `bursts` more sound bursts, asked for by the hand. It counts bursts, and works whether the ARM7 is
	clocked or held
*/
struct {
	Co co;
} pause_after;

bool PauseAfter(const emu::RunState& stop, u32 bursts)
{
	CO_BEGIN(pause_after.co);

	HandPauseAfter(bursts);

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
	first sample of the burst when the test stood behind a burst
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
	From a stop at the breakpoint SuspendAtStep() listed, resumes until the stop is in sample `sample` of its burst
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
	The burst suspended at the end of a sample: a step of step 127 from the breakpoint on it, which goes. Returns
	false while it has armed something
*/
struct {
	Co co;
} sample_end;

bool SuspendAtSampleEnd(const emu::RunState& stop)
{
	CO_BEGIN(sample_end.co);

	while (!SuspendAtStep(stop, kLast))
		CO_YIELD(sample_end.co);

	DspBps({ });

	if (!StepDsp())
		break;
	CO_YIELD(sample_end.co);

	if (stop.cause != StopCause::Step || Suspended() != SOUND_BURST_SAMPLE || DspStep() != 0)
		Check(false, "a step of step 127 leaves the burst at the end of its sample (\"%s\", burst %u, the DSP at step %u)", StopCauseName(stop.cause), Suspended(),
			DspStep());

	CO_END(sample_end.co);
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

/*
	The capture, as the closures of the core make it (dbg_core.cpp): at a stop sgc::Arm with stopWhenDone and the
	resume behind it; and what a capture left of itself
*/
bool ArmAtStop(u32 frames)
{
	const int armed = sgc::Arm(frames, true);

	if (armed != 0)
	{
		Check(false, "a capture of %u frames is armed at a stop: sgc::Arm returned %d", frames, armed);
		return false;
	}

	if (emu::Resume() != 0)
	{
		sgc::Cancel(kErrNotPaused);
		Check(false, "the resume behind the arm of a capture was refused");
		return false;
	}

	return true;
}

// The frames of the server's capture
u32 ServerFrames()
{
	u32 frames = 0;

	sgc::GetState(&frames);
	return frames;
}

bool Recording()
{
	u32 frames = 0;

	return sgc::GetState(&frames) == sgc::State::Recording;
}

std::string ServerCapture()
{
	std::string out;

	sgc::Copy(&out);
	return out;
}

/*
	The record of one channel as dbg_sgc.h has its layout, from the accessors of the AICA as they stand now: what a
	frame taken at this moment holds. Written here from the layout, not by dbg_sgc.cpp
*/
std::string ExpectedRecord(u32 channel)
{
	std::string r(sgc::kBytesPerChannel, '\0');
	u8* b = (u8*)&r[0];
	AicaChannelDebug d;

	memset(&d, 0xA5, sizeof(d));
	Aica()->DebugReadRegs(channel * 0x80, 72, b);
	Aica()->DebugGetChannel(channel, &d);

	auto put = [b](u32 at, u32 value, u32 bytes) {
		for (u32 i = 0; i < bytes; i++)
			b[at + i] = (u8)(value >> (8 * i));
	};

	put(72, (u32)d.sampleCurrent, 2);
	put(74, (u32)d.samplePrevious, 2);
	put(84, (u32)d.outLeft, 2);
	put(86, (u32)d.outRight, 2);
	put(88, (u32)d.outDsp, 2);
	put(90, d.caFraction, 2);
	put(92, d.step, 4);
	put(96, d.aegLevel, 4);
	put(100, d.fegValue, 4);
	b[104] = d.lfoState;
	b[105] = d.alfo;
	b[106] = d.plfo;
	put(107, d.ca, 2);
	b[109] = (u8)((d.enabled ? 1 : 0) | ((d.aegState & 3) << 1));

	return r;
}

std::string ExpectedFrame()
{
	std::string frame;

	for (u32 channel = 0; channel < sgc::kChannelsPerFrame; channel++)
		frame += ExpectedRecord(channel);

	return frame;
}

// The first byte in which two frames differ, as "channel c byte b", or "" when they are equal; `skip` leaves the
// outputs (84..89) out
std::string FrameDiffers(const std::string& got, const std::string& want, bool skipOutputs)
{
	if (got.size() != want.size())
		return "a frame of " + std::to_string(got.size()) + " bytes, not " + std::to_string(want.size());

	for (size_t i = 0; i < got.size(); i++)
	{
		const u32 at = (u32)(i % sgc::kBytesPerChannel);

		if (skipOutputs && at >= 84 && at < 90)
			continue;

		if (got[i] != want[i])
		{
			char text[128];

			snprintf(text, sizeof(text), "channel %u byte %u: %02X, the accessors give %02X", (u32)(i / sgc::kBytesPerChannel), at, (u8)got[i], (u8)want[i]);
			return text;
		}
	}

	return "";
}

// A record of a frame of a capture: channel `channel` of frame `frame`
const u8* RecordOf(const std::string& capture, u32 frame, u32 channel)
{
	return (const u8*)capture.data() + (size_t)frame * sgc::kBytesPerFrame + channel * sgc::kBytesPerChannel;
}

u32 Level(const u8* record)
{
	return record[96] | (record[97] << 8) | (record[98] << 16) | ((u32)record[99] << 24);
}

bool Plays(const u8* record)
{
	return (record[109] & 1) != 0 && Level(record) > 0;
}

/*
	The core without a server, as the selftest has it (dbg_core_private.h): what it sends a client comes to Sink.
	Every message is kept with the notices of dbg::sgc that had been given when it came: a response to
	state.recordSgcFrames that comes before its capture's notice was given was sent before the capture was
	complete
*/
struct Sent {
	u32 client;
	bool binary;
	std::string text;			// a response; for a binary frame its 16 bytes of header
	std::string payload;		// a binary frame of client 0: its payload
	size_t bytes;				// of a binary frame, header included
	u32 noticesGiven;
};

std::mutex sent_lock;
std::vector<Sent>& sent = *new std::vector<Sent>();

void Sink(u32 client, const std::string& message, bool binary)
{
	Sent s;

	s.client = client;
	s.binary = binary;
	s.bytes = message.size();
	s.noticesGiven = sgc::test::Notices().given;

	if (binary)
	{
		s.text = message.substr(0, kBinaryHeaderBytes);

		// the payloads of the selftest's own requests are compared; those of the clients, which are captures of up
		// to 32 MiB, are not kept
		if (client == 0)
			s.payload = message.substr(std::min<size_t>(kBinaryHeaderBytes, message.size()));
	}
	else
	{
		s.text = message;
	}

	std::lock_guard<std::mutex> lock(sent_lock);
	sent.push_back(std::move(s));
}

std::vector<Sent> TakeSent()
{
	std::lock_guard<std::mutex> lock(sent_lock);
	std::vector<Sent> rv;

	rv.swap(sent);
	return rv;
}

size_t SentCount()
{
	std::lock_guard<std::mutex> lock(sent_lock);

	return sent.size();
}

// The responses of client `client` to request `id` that came so far
std::vector<Sent> ResponsesTo(u32 client, u32 id)
{
	std::lock_guard<std::mutex> lock(sent_lock);
	std::vector<Sent> rv;

	for (const Sent& s : sent)
	{
		if (s.client != client || s.binary)
			continue;

		json j = json::parse(s.text, nullptr, false);

		if (j.is_object() && j.contains("id") && j["id"].is_number() && j["id"].get<u32>() == id)
			rv.push_back(s);
	}

	return rv;
}

// The first response of client `client` to request `id`, waited for up to ms; "" when none came
std::string WaitResponse(u32 client, u32 id, int ms, Clock::time_point* when = nullptr)
{
	for (Clock::time_point t0 = Clock::now(); ; Nap(1))
	{
		std::vector<Sent> r = ResponsesTo(client, id);

		if (!r.empty())
		{
			if (when)
				*when = Clock::now();
			return r[0].text;
		}

		if (MsSince(t0) > ms)
			return "";
	}
}

// What an in-band response says: 0 for the result {}, the code of an error, 1 for anything else (no response, or
// one that is neither)
int InBandCode(const std::string& text)
{
	json j = json::parse(text, nullptr, false);

	if (!j.is_object() || !j.contains("result"))
		return 1;

	const json& r = j["result"];

	if (r.is_object() && r.empty())
		return 0;

	if (r.is_object() && r.contains("error") && r["error"].contains("code") && r["error"]["code"].is_number())
		return r["error"]["code"].get<int>();

	return 1;
}

// A request of a client of the core, as the network thread hands one over
void Ask(u32 client, u32 id, const char* method, const json& params = json::object())
{
	json request = { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params } };

	core::TextForTest(client, request.dump());
}

// The core thread without a server, for a test that goes through it. False, having failed a check, when it could
// not be started
bool CoreOn()
{
	TakeSent();
	core::SetSendSinkForTest(Sink);
	core::SetCaptureTimeoutForTest(0);
	core::SetBeforeArmForTest(nullptr);
	core::SetBeforeTableCallForTest(nullptr);

	return Check(core::StartForTest(), "the core thread is started without a server (no other core thread is there: the session has no server)");
}

void CoreOff()
{
	core::Stop();
	core::SetCaptureTimeoutForTest(0);
	core::SetBeforeArmForTest(nullptr);
	core::SetBeforeTableCallForTest(nullptr);
}

const u32 kClient = 7;		// the client of the core in the tests that go through it

}

/*
	mem.binary.*: state.getMemoryBinary as the selftest's own requests make it (client 0, core::HandleRequestText,
	in place on the emulator thread), with what the core sends behind the response coming to the sink: the response
	first, the binary frame second, nothing behind an error; the bytes of state.getMemorySlice; and one counter of
	binaryIds for the frames of every kind
*/
namespace {

// A request of the selftest itself, in place: its response, parsed
json Ask0(const char* method, const json& params = json::object())
{
	static u32 id = 1;
	json request = { { "jsonrpc", "2.0" }, { "id", id++ }, { "method", method }, { "params", params } };

	return json::parse(core::HandleRequestText(request.dump()), nullptr, false);
}

int RpcError(const json& response)
{
	if (response.is_object() && response.contains("error") && response["error"].contains("code"))
		return response["error"]["code"].get<int>();

	return 0;
}

// The header of a binary frame: kind, binaryId, payloadBytes; false when it is no NDB3 header
bool Header(const std::string& head, u32* kind, u32* id, u32* bytes)
{
	if (head.size() != kBinaryHeaderBytes || head.compare(0, 4, "NDB3") != 0)
		return false;

	auto word = [&head](u32 at) {
		return (u32)(u8)head[at] | ((u32)(u8)head[at + 1] << 8) | ((u32)(u8)head[at + 2] << 16) | ((u32)(u8)head[at + 3] << 24);
	};

	*kind = word(4);
	*id = word(8);
	*bytes = word(12);
	return true;
}

// state.getMemorySlice of the same range, its bytes
std::string Slice(const char* target, u32 address, u32 length)
{
	json r = Ask0("state.getMemorySlice", { { "target", target }, { "address", address }, { "length", length } });
	std::string bytes;

	if (r.contains("result"))
	{
		for (const json& b : r["result"]["data"])
			bytes += (char)(u8)b.get<u32>();
	}

	return bytes;
}

// A state.getMemoryBinary of the selftest: its result, and what the sink got behind the request, in order
struct BinaryRead {
	json response;
	std::vector<Sent> sent;
	bool ordered;			// exactly the response, then one binary frame
	u32 kind, id, bytes;	// of its header
	std::string payload;
};

BinaryRead ReadBinary(const char* target, u32 address, u32 length)
{
	BinaryRead r;

	TakeSent();
	r.response = Ask0("state.getMemoryBinary", { { "target", target }, { "address", address }, { "length", length } });
	r.sent = TakeSent();
	r.kind = r.id = r.bytes = 0;
	r.ordered = r.sent.size() == 2 && !r.sent[0].binary && r.sent[1].binary && Header(r.sent[1].text, &r.kind, &r.id, &r.bytes)
		&& json::parse(r.sent[0].text, nullptr, false) == r.response;

	if (r.ordered)
		r.payload = r.sent[1].payload;

	return r;
}

struct {
	Co co;
} mb_bytes;

}

BOUNDED_GUEST_TEST(mem_binary_bytes, "mem.binary.bytes")
{
	CO_BEGIN(mb_bytes.co);

	while (!Settle(stop))
		CO_YIELD(mb_bytes.co);

	core::SetSendSinkForTest(Sink);

	{
		BinaryRead r = ReadBinary("sh4", DSPTEST_START, 64);
		const json& result = r.response["result"];

		Check(r.ordered, "state.getMemoryBinary of 64 bytes at %08X: the response, then one binary frame, to the client that asked (%zu messages)", DSPTEST_START,
			r.sent.size());
		Check(r.response.contains("result") && result["baseAddress"] == DSPTEST_START && result["totalBytes"] == 64u && !result.contains("unreadable")
			&& !result.contains("validity") && result["binaryId"].is_number() && result["binaryId"].get<u32>() >= 1,
			"its result: baseAddress, totalBytes, binaryId, no unreadable and no validity (%s)", r.response.dump().c_str());
		Check(r.kind == kBinaryMemory && result["binaryId"] == r.id && r.bytes == 64 && r.payload.size() == 64,
			"the frame: kind 2, the binaryId of the result, payloadBytes 64 (kind %u, id %u, %u bytes)", r.kind, r.id, r.bytes);
		Check(r.payload == Slice("sh4", DSPTEST_START, 64) && r.payload.find_first_not_of('\0') != std::string::npos,
			"its bytes are those of state.getMemorySlice: the guest's code");
	}

	{
		BinaryRead r = ReadBinary("sh4", 0x0021FFF0, 32);
		const json& result = r.response["result"];

		Check(r.ordered && result["unreadable"] == json::parse("[[16,16]]") && r.payload.size() == 32 && r.payload.substr(16) == std::string(16, '\0')
			&& r.payload == Slice("sh4", 0x0021FFF0, 32),
			"across the end of the flash: unreadable [[16,16]], those bytes 0, the bytes of state.getMemorySlice (%s)", r.response.dump().c_str());
	}

	{
		// the bound: main memory, 16 MiB
		BinaryRead r = ReadBinary("sh4", 0x0C000000, kMaxMemoryBinaryBytes);
		std::string whole(kMaxMemoryBinaryBytes, '\0');
		const u32 read = mem::Read(Target::Sh4, 0x0C000000, kMaxMemoryBinaryBytes, (u8*)&whole[0], nullptr);

		Check(r.ordered && r.bytes == kMaxMemoryBinaryBytes && r.response["result"]["totalBytes"] == kMaxMemoryBinaryBytes && !r.response["result"].contains("unreadable"),
			"16 MiB of main memory, the bound: one frame of 16 MiB and its header, nothing unreadable (%u bytes)", r.bytes);
		Check(read == kMaxMemoryBinaryBytes && r.payload == whole, "its bytes are main memory as the debugger reads it, every one of them");
		Check(r.payload.substr(0x10000, 64) == Slice("sh4", 0x0C010000, 64) && r.payload.substr(kMaxMemoryBinaryBytes - 64) == Slice("sh4", 0x0CFFFFC0, 64),
			"and those of state.getMemorySlice past its first 64 KiB and in its last 64 bytes");
	}

	{
		// wave memory behind both targets
		BinaryRead arm7 = ReadBinary("arm7", 0, 0x200000);
		BinaryRead sh4 = ReadBinary("sh4", 0x00800000, 0x200000);

		Check(arm7.ordered && sh4.ordered && arm7.payload.size() == 0x200000 && arm7.payload == sh4.payload
			&& arm7.payload.substr(0, DSPTEST_ARM7_SIZE) == Slice("arm7", 0, DSPTEST_ARM7_SIZE),
			"2 MiB of wave memory through the ARM7 are the bytes at 0x00800000 through the SH4, the ARM7 program of the guest at its start");
	}

	core::SetSendSinkForTest(nullptr);
	TakeSent();

	CO_END(mb_bytes.co);
}

namespace {

struct {
	Co co;
} mb_err;

}

BOUNDED_GUEST_TEST(mem_binary_errors, "mem.binary.errors")
{
	std::vector<Sent> list;
	json r;

	CO_BEGIN(mb_err.co);

	while (!Settle(stop))
		CO_YIELD(mb_err.co);

	core::SetSendSinkForTest(Sink);
	TakeSent();

	r = Ask0("state.getMemoryBinary", { { "target", "sh4" }, { "address", 0x10000000u }, { "length", 16 } });
	list = TakeSent();
	Check(RpcError(r) == kErrNotReadable && list.size() == 1 && !list[0].binary, "nothing readable: -32010, a JSON-RPC error, and no binary frame behind it (%s)",
		r.dump().c_str());

	r = Ask0("state.getMemoryBinary", { { "target", "dsp" }, { "address", 0 }, { "length", 16 } });
	list = TakeSent();
	Check(RpcError(r) == kErrNotSupported && list.size() == 1 && !list[0].binary, "the target dsp, which has no memory: -32004, and no frame (%s)", r.dump().c_str());

	r = Ask0("state.getMemoryBinary", { { "target", "sh4" }, { "address", 0x0C000000u }, { "length", kMaxMemoryBinaryBytes + 1 } });
	list = TakeSent();
	Check(RpcError(r) == kErrInvalidParams && list.size() == 1 && !list[0].binary, "a length above 16 MiB: -32602, and no frame (%s)", r.dump().c_str());

	r = Ask0("state.getMemoryBinary", { { "target", "sh4" }, { "address", 0x0C000000u }, { "length", 0 } });
	list = TakeSent();
	Check(RpcError(r) == kErrInvalidParams && list.size() == 1 && !list[0].binary, "a length of 0: -32602, and no frame (%s)", r.dump().c_str());

	r = Ask0("state.getMemoryBinary", { { "target", "sh4" }, { "length", 16 } });
	list = TakeSent();
	Check(RpcError(r) == kErrInvalidParams && list.size() == 1 && !list[0].binary, "no address: -32602, and no frame (%s)", r.dump().c_str());

	// no wrap across the end of the address space: what state.getMemorySlice answers there, and a frame behind a
	// result only
	r = Ask0("state.getMemoryBinary", { { "target", "sh4" }, { "address", 0xFFFFFFF0u }, { "length", 32 } });
	list = TakeSent();

	{
		json slice = Ask0("state.getMemorySlice", { { "target", "sh4" }, { "address", 0xFFFFFFF0u }, { "length", 32 } });

		Check(RpcError(r) == RpcError(slice) && (RpcError(r) != 0 ? list.size() == 1 && !list[0].binary
			: list.size() == 2 && r["result"]["unreadable"] == slice["result"]["unreadable"]),
			"across the end of the address space, which does not wrap: as state.getMemorySlice answers (%s; %s)", r.dump().c_str(), slice.dump().c_str());
	}

	core::SetSendSinkForTest(nullptr);
	TakeSent();

	CO_END(mb_err.co);
}

namespace {

struct {
	Co co;
	u32 stopId;
} mb_ids;

}

BOUNDED_GUEST_TEST(mem_binary_ids, "mem.binary.ids")
{
	CO_BEGIN(mb_ids.co);

	while (!Settle(stop))
		CO_YIELD(mb_ids.co);

	// a capture to read: two frames, from this stop
	mb_ids.stopId = stop.stopId;

	if (!ArmAtStop(2))
		break;
	CO_YIELD(mb_ids.co);

	Check(stop.cause == StopCause::Capture && ServerFrames() == 2, "a capture of 2 frames armed at a stop ends in a stop of its own (\"%s\")", StopCauseName(stop.cause));

	core::SetSendSinkForTest(Sink);
	TakeSent();

	{
		BinaryRead first = ReadBinary("sh4", DSPTEST_START, 16);

		TakeSent();
		json data = Ask0("state.getSgcFrameData");
		std::vector<Sent> frame = TakeSent();

		BinaryRead error = ReadBinary("sh4", 0x10000000, 16);
		BinaryRead last = ReadBinary("sh4", DSPTEST_START, 16);

		u32 kind = 0, id = 0, bytes = 0;
		const bool ordered = frame.size() == 2 && !frame[0].binary && frame[1].binary && Header(frame[1].text, &kind, &id, &bytes);
		const json& result = data["result"];

		Check(ordered && data.contains("result") && result["frameCount"] == 2u && result["channelsPerFrame"] == 64u && result["bytesPerChannel"] == 128u
			&& result["totalBytes"] == 2u * 8192u && result["binaryId"] == id && kind == kBinarySgc && bytes == 2 * 8192,
			"state.getSgcFrameData: {frameCount 2, channelsPerFrame 64, bytesPerChannel 128, totalBytes, binaryId}, then a frame of kind 1 with that binaryId (%s)",
			data.dump().c_str());
		Check(ordered && frame[1].payload == ServerCapture(), "its payload is the server's capture");
		Check(first.ordered && last.ordered && first.id >= 1 && id == first.id + 1 && last.id == id + 1 && error.sent.size() == 1,
			"one counter of binaryIds for the frames of both kinds, from 1: %u, %u, %u, and an error takes none", first.id, id, last.id);
	}

	core::SetSendSinkForTest(nullptr);
	TakeSent();

	CO_END(mb_ids.co);
}

/*
	sgc.layout: each record of a capture against the accessors of the AICA at its sample. The capture is armed at a
	stop in front of step 0 of the DSP, with the breakpoint on step 0 left listed: so the burst is suspended in
	front of step 0 in every sample behind it, which is behind the sweep and the frame of that sample and in front
	of its DSP phase - where the frame was taken. Its first frame is the next sample's: the sweep of this one came
	before the arm. The last frame completes the capture inside the run-on, and the breakpoint's stop that follows
	in the same sample wins over the capture's
*/
namespace {

const u32 kLayoutFrames = 4;

struct {
	Co co;
	u32 n, given, stopId, outputs;
	std::string frames[kLayoutFrames];
} lay;

// Has the record an output that is not 0?
bool HasOutput(const u8* record)
{
	for (u32 at = 84; at < 90; at++)
	{
		if (record[at] != 0)
			return true;
	}

	return false;
}

}

BOUNDED_GUEST_TEST(sgc_layout, "sgc.layout")
{
	std::string got, want, diff;

	CO_BEGIN(lay.co);

	while (!Settle(stop))
		CO_YIELD(lay.co);

	while (!SuspendAtStep(stop, 0))
		CO_YIELD(lay.co);

	lay.given = sgc::test::Notices().given;
	lay.outputs = 0;

	if (!ArmAtStop(kLayoutFrames))
		break;

	{
		const sgc::test::Recording rec = sgc::test::GetRecording();
		AicaChannelDebug d;

		memset(&d, 0xA5, sizeof(d));
		Aica()->DebugGetChannel(0, &d);

		Check(dbg_sgc_active != 0 && rec.wanted == kLayoutFrames && rec.taken == 0 && rec.stopWhenDone,
			"armed in front of a DSP step: the capture records, and has taken no frame (the sweep of this sample came before the arm)");
		Check(d.outLeft == 0 && d.outRight == 0 && d.outDsp == 0, "the outputs read 0 until a sweep keeps them (%d %d %d)", d.outLeft, d.outRight, d.outDsp);
	}

	for (lay.n = 0; lay.n + 1 < kLayoutFrames; lay.n++)
	{
		CO_YIELD(lay.co);

		got.clear();

		if (!Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == kAtId && DspStep() == 0 && Suspended() == SOUND_BURST_DSP
			&& sgc::test::GetRecording().taken == lay.n + 1 && sgc::test::CopyRecordingFrame(lay.n, &got),
			"sample %u behind the arm: the breakpoint stops in front of step 0, behind the sweep, and the capture has taken the frame of this sample (%u frames)",
			lay.n + 1, sgc::test::GetRecording().taken))
			break;

		want = ExpectedFrame();
		diff = FrameDiffers(got, want, false);

		Check(diff.empty(), "frame %u: every record is what the accessors of the AICA give at its sample (%s)", lay.n, diff.empty() ? "64 records" : diff.c_str());
		Check(Plays(RecordOf(got, 0, 0)) && Plays(RecordOf(got, 0, 1)), "the two channels the guest keyed play in it (flags bit 0, a level above 0)");

		lay.outputs += HasOutput(RecordOf(got, 0, 0)) || HasOutput(RecordOf(got, 0, 1));
		lay.frames[lay.n] = got;

		if (!Resume())
			break;
	}

	CO_YIELD(lay.co);

	// the last frame: taken behind the sweep of this sample, which completed the capture
	Check(stop.cause == StopCause::Breakpoint && DspStep() == 0 && Suspended() == SOUND_BURST_DSP && dbg_sgc_active == 0 && !Recording()
		&& ServerFrames() == kLayoutFrames, "the last frame completes the capture: the server has its %u frames, and the breakpoint's stop wins over the capture's "
		"(\"%s\")", kLayoutFrames, StopCauseName(stop.cause));

	{
		const std::string server = ServerCapture();
		bool same = server.size() == (size_t)kLayoutFrames * sgc::kBytesPerFrame;

		for (u32 i = 0; same && i + 1 < kLayoutFrames; i++)
			same = server.compare((size_t)i * sgc::kBytesPerFrame, sgc::kBytesPerFrame, lay.frames[i]) == 0;

		diff = same ? FrameDiffers(server.substr((size_t)(kLayoutFrames - 1) * sgc::kBytesPerFrame), ExpectedFrame(), true) : "the frames before differ";

		Check(same && diff.empty(), "the server's capture is the capture's own buffer: its frames as they were taken, the last one the accessors at its sample but "
			"the outputs, which read 0 once no capture records (%s)", diff.empty() ? "equal" : diff.c_str());
	}

	Check(lay.outputs != 0, "the outputs of the two channels are in the records: in %u of %u frames they are not 0", lay.outputs, kLayoutFrames - 1);
	Check(sgc::test::Notices().given == lay.given + 1 && sgc::test::Notices().lastAnswer == 0, "one notice, with the answer 0 of a capture that is complete");

	DspBps({ });

	CO_END(lay.co);
}

/*
	sgc.stop: a capture armed at a stop stops at the end of the sample of its last frame, with reason capture and a
	new stopId: from a stop behind a burst (a running tick, then the stop, the burst suspended at the end of a
	sample); from a burst suspended in front of a DSP step with fewer frames than the rest of the burst (no running
	tick: the park loop runs the burst on); with exactly as many frames as the burst has samples left, in front of a
	DSP step and in front of an ARM7 instruction (no running tick, the burst ended with none suspended: the run-on
	decides the stop that was recorded in the burst's last sample); and in the run-on of a step of the SH4, which
	the stop drops. The hand pauses the run a few bursts later where no stop came
*/
namespace {

struct {
	Co co;
	u32 stopId, k, n;
	u32 pc;
	Counts c;
} st_stop;

// The stop of a capture: reason capture, a new stopId, what was published since the arm
bool CaptureStopped(const emu::RunState& stop, const char* where, bool running)
{
	std::vector<Seen> list = TakeSeen();

	HandOff();

	const bool published = running ? SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Capture } })
		: SeenIs(list, { { Lifetime::Parked, StopCause::Capture } });

	return Check(stop.cause == StopCause::Capture && stop.stopId == st_stop.stopId + 1 && published,
		"%s: the stop is the capture's, \"%s\", with a new stopId (+%u); published %s", where, StopCauseName(stop.cause), stop.stopId - st_stop.stopId,
		SeenText(list).c_str());
}

}

BOUNDED_GUEST_TEST(sgc_stop, "sgc.stop")
{
	Counts c;

	CO_BEGIN(st_stop.co);

	/*
		behind a burst
	*/
	while (!Settle(stop))
		CO_YIELD(st_stop.co);

	st_stop.stopId = stop.stopId;
	st_stop.c = ReadCounts();
	TakeSeen();
	HandPauseAfter(3);

	if (!ArmAtStop(10))
		break;
	CO_YIELD(st_stop.co);

	c = ReadCounts();

	if (CaptureStopped(stop, "10 frames armed behind a burst", true))
		Check(Suspended() == SOUND_BURST_SAMPLE && DspStep() == 0 && c.n[2] - st_stop.c.n[2] == 10 && c.n[1] - st_stop.c.n[1] == 10 && c.n[3] == st_stop.c.n[3]
			&& ServerFrames() == 10,
			"the burst is suspended at the end of the sample of the last frame: 10 time steps and 10 ARM phases, not the ARM phase of the next (%u, %u), no tail",
			c.n[2] - st_stop.c.n[2], c.n[1] - st_stop.c.n[1]);

	/*
		in front of a DSP step, fewer frames than the rest of the burst
	*/
	while (!Settle(stop))
		CO_YIELD(st_stop.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_stop.co);

	DspBps({ });
	st_stop.stopId = stop.stopId;
	st_stop.c = ReadCounts();
	TakeSeen();
	HandPauseAfter(3);

	if (!ArmAtStop(5))
		break;
	CO_YIELD(st_stop.co);

	c = ReadCounts();

	if (CaptureStopped(stop, "5 frames armed in front of a DSP step of sample 0", false))
		Check(Suspended() == SOUND_BURST_SAMPLE && c.n[2] - st_stop.c.n[2] == 6 && c.n[0] == st_stop.c.n[0] && ServerFrames() == 5,
			"no running tick: the park loop ran the burst on; suspended at the end of the sample of the last frame, sample 5 (%u time steps, %u bursts)",
			c.n[2] - st_stop.c.n[2], c.n[0] - st_stop.c.n[0]);

	/*
		in front of a DSP step, exactly as many frames as the burst has samples left
	*/
	while (!Settle(stop))
		CO_YIELD(st_stop.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_stop.co);

	while (!ToSample(stop, 20))
		CO_YIELD(st_stop.co);

	DspBps({ });
	st_stop.stopId = stop.stopId;
	st_stop.c = ReadCounts();
	st_stop.n = 31 - Sample();
	TakeSeen();
	HandPauseAfter(3);

	if (!ArmAtStop(st_stop.n))
		break;
	CO_YIELD(st_stop.co);

	c = ReadCounts();

	if (CaptureStopped(stop, "as many frames as the burst has samples left, armed in front of a DSP step", false))
		Check(Suspended() == SOUND_BURST_NONE && c.n[3] == st_stop.c.n[3] + 1 && c.n[0] == st_stop.c.n[0] && c.n[2] - st_stop.c.n[2] == st_stop.n + 1
			&& ServerFrames() == st_stop.n,
			"the burst ended with none suspended, its tail run once, and no running tick (%u frames, %u time steps)", st_stop.n, c.n[2] - st_stop.c.n[2]);

	/*
		in front of an ARM7 instruction, exactly as many frames as the burst has samples left
	*/
	while (!Settle(stop))
		CO_YIELD(st_stop.co);

	while (!SuspendAtArm(stop, kMain))
		CO_YIELD(st_stop.co);

	ArmBps({ });
	st_stop.stopId = stop.stopId;
	st_stop.c = ReadCounts();
	st_stop.n = 32 - Sample();
	TakeSeen();
	HandPauseAfter(3);

	if (!ArmAtStop(st_stop.n))
		break;
	CO_YIELD(st_stop.co);

	c = ReadCounts();

	if (CaptureStopped(stop, "as many frames as the burst has samples left, armed in front of an ARM7 instruction", false))
		Check(Suspended() == SOUND_BURST_NONE && c.n[3] == st_stop.c.n[3] + 1 && c.n[0] == st_stop.c.n[0] && c.n[2] - st_stop.c.n[2] == st_stop.n
			&& ServerFrames() == st_stop.n,
			"the burst ended with none suspended, its tail run once, and no running tick (%u frames, %u time steps)", st_stop.n, c.n[2] - st_stop.c.n[2]);

	/*
		the run-on of a step of the SH4: the capture, held at a breakpoint of the DSP in sample k + 1 with one frame
		taken, completes in the burst's last sample, and the step is dropped
	*/
	while (!Settle(stop))
		CO_YIELD(st_stop.co);

	while (!SuspendAtStep(stop, kSite))
		CO_YIELD(st_stop.co);

	while (!ToSample(stop, 24))
		CO_YIELD(st_stop.co);

	st_stop.n = 31 - Sample();
	HandPauseAfter(3);

	if (!ArmAtStop(st_stop.n))
		break;
	CO_YIELD(st_stop.co);

	if (!Check(stop.cause == StopCause::Breakpoint && Sample() == 25 && sgc::test::GetRecording().taken == 1,
		"the capture of %u frames records at the breakpoint's stop of the next sample, with one frame taken (\"%s\", sample %u)", st_stop.n, StopCauseName(stop.cause),
		Sample()))
		break;

	DspBps({ });
	st_stop.stopId = stop.stopId;
	st_stop.c = ReadCounts();
	st_stop.pc = Ctx().pc;
	TakeSeen();

	if (!StepSh4())
		break;
	CO_YIELD(st_stop.co);

	c = ReadCounts();

	if (CaptureStopped(stop, "the run-on of a step of the SH4, in which the capture completed in the burst's last sample", false))
		Check(Suspended() == SOUND_BURST_NONE && c.n[3] == st_stop.c.n[3] + 1 && Ctx().pc == st_stop.pc && ServerFrames() == st_stop.n,
			"the burst ended with none suspended, and the step of the SH4 was dropped (pc %08X, it was %08X)", Ctx().pc, st_stop.pc);

	HandOff();

	CO_END(st_stop.co);
}

/*
	sgc.count: a capture armed at a stop holds exactly the frames asked for, and the AICA makes as many time steps
	from the arm to the capture's stop (SoundBurstCounts out[2]): `frames`, and `frames` + 1 where it was armed with
	the burst suspended in front of a DSP step, whose sample's sweep came before the arm and its time step behind
	it. Armed with none suspended, in front of an ARM7 instruction, in front of a DSP step and at the end of a
	sample, with fewer frames than a burst and with more. Through the core: its response comes when the capture is
	complete, with the notice of the capture given, and once
*/
namespace {

enum Where { AT_NONE, AT_ARM7, AT_DSP, AT_SAMPLE, AT_COUNT };

const char* WhereName(u32 where)
{
	switch (where)
	{
	case AT_NONE:   return "with none suspended";
	case AT_ARM7:   return "in front of an ARM7 instruction";
	case AT_DSP:    return "in front of a DSP step";
	case AT_SAMPLE: return "at the end of a sample";
	}

	return "";
}

// Brings the burst to where the round has it. Returns false while it has armed something
struct {
	Co co;
} put;

bool PutBurst(const emu::RunState& stop, u32 where)
{
	CO_BEGIN(put.co);

	// from a stop behind a burst: what follows is in its first sample
	while (!BehindBurst(stop))
		CO_YIELD(put.co);

	if (where == AT_NONE)
	{
	}
	else if (where == AT_ARM7)
	{
		while (!SuspendAtArm(stop, kMain))
			CO_YIELD(put.co);

		ArmBps({ });
	}
	else if (where == AT_DSP)
	{
		while (!SuspendAtStep(stop, kSite))
			CO_YIELD(put.co);

		DspBps({ });
	}
	else
	{
		while (!SuspendAtSampleEnd(stop))
			CO_YIELD(put.co);
	}

	CO_END(put.co);
}

const u32 kCountFrames[2] = { 7, 45 };

struct {
	Co co;
	u32 where, size, id, given;
	Counts c;
} st_count;

}

BOUNDED_GUEST_TEST(sgc_count, "sgc.count")
{
	Counts c;

	CO_BEGIN(st_count.co);

	while (!Settle(stop))
		CO_YIELD(st_count.co);

	if (!CoreOn())
		break;

	st_count.id = 100;

	for (st_count.where = 0; st_count.where < AT_COUNT; st_count.where++)
	{
		for (st_count.size = 0; st_count.size < 2; st_count.size++)
		{
			while (!PutBurst(stop, st_count.where))
				CO_YIELD(st_count.co);

			st_count.c = ReadCounts();
			st_count.given = sgc::test::Notices().given;
			st_count.id++;
			HandPauseAfter(5);

			// the core makes the call that arms the capture and the resume, at this stop
			Ask(kClient, st_count.id, "state.recordSgcFrames", { { "frames", kCountFrames[st_count.size] } });
			CO_YIELD(st_count.co);

			HandOff();
			c = ReadCounts();

			{
				const u32 frames = kCountFrames[st_count.size];
				const u32 steps = c.n[2] - st_count.c.n[2];
				const u32 want = frames + (st_count.where == AT_DSP ? 1 : 0);
				Clock::time_point t0 = Clock::now();
				const std::string response = WaitResponse(kClient, st_count.id, 2000);
				std::vector<Sent> responses;

				Nap(20);
				responses = ResponsesTo(kClient, st_count.id);

				Check(stop.cause == StopCause::Capture && steps == want && ServerFrames() == frames,
					"%u frames armed %s: the capture's stop (\"%s\") is %u time steps of the AICA behind the arm, and the server has %u frames",
					frames, WhereName(st_count.where), StopCauseName(stop.cause), steps, ServerFrames());
				Check(InBandCode(response) == 0 && responses.size() == 1 && responses[0].noticesGiven >= st_count.given + 1,
					"the response is {}, sent once, with the capture's notice given when it was sent: when the capture was complete (%s, %.0f ms behind the stop)",
					response.c_str(), MsSince(t0));
			}
		}
	}

	CoreOff();

	CO_END(st_count.co);
}

/*
	sgc.running: a capture armed while the machine runs, through the core: a CC_TABLE call, in a service excursion,
	arms it with no stop at its end. The response comes when it is complete, state.getSgcFrameData behind it has its
	frames, and the run did not stop for it
*/
namespace {

struct {
	Co co;
	u32 given;
	std::atomic<bool> failed { false };
	std::atomic<int> code { 1 };
	std::atomic<u32> noticesAt { 0 };
	std::string data;
	u32 kind, id, bytes;
	std::atomic<bool> framed { false };
} st_run;

}

BOUNDED_GUEST_TEST(sgc_running, "sgc.running")
{
	std::vector<Seen> list;

	CO_BEGIN(st_run.co);

	while (!Settle(stop))
		CO_YIELD(st_run.co);

	if (!CoreOn())
		break;

	st_run.given = sgc::test::Notices().given;
	st_run.failed = false;
	st_run.framed = false;
	st_run.code = 1;
	TakeSeen();

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Running))
		{
			st_run.failed = true;
			return;
		}

		Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 100 } });

		const std::string response = WaitResponse(kClient, 1, 10000);
		std::vector<Sent> r = ResponsesTo(kClient, 1);

		st_run.code = InBandCode(response);
		st_run.noticesAt = r.empty() ? 0 : r[0].noticesGiven;

		Ask(kClient, 2, "state.getSgcFrameData");
		st_run.data = WaitResponse(kClient, 2, 10000);

		for (Clock::time_point t0 = Clock::now(); MsSince(t0) < 10000 && !st_run.framed; Nap(1))
		{
			std::lock_guard<std::mutex> lock(sent_lock);

			for (const Sent& s : sent)
			{
				if (s.client == kClient && s.binary && Header(s.text, &st_run.kind, &st_run.id, &st_run.bytes))
					st_run.framed = true;
			}
		}

		emu::RequestPause();
	});

	if (!Resume())
		break;
	CO_YIELD(st_run.co);

	OtherJoin();
	list = TakeSeen();

	{
		json data = json::parse(st_run.data, nullptr, false);
		const bool frames = data.is_object() && data.contains("result") && data["result"]["frameCount"] == 100u;

		Check(!st_run.failed && st_run.code == 0 && st_run.noticesAt >= st_run.given + 1, "armed while running: the response {} comes when the capture is complete (%d)",
			st_run.code.load());
		Check(frames && st_run.framed && st_run.kind == kBinarySgc && st_run.bytes == 100 * sgc::kBytesPerFrame && data["result"]["binaryId"] == st_run.id,
			"state.getSgcFrameData behind it has the 100 frames, and their frame of kind 1 behind its response (%s)", st_run.data.substr(0, 200).c_str());
		Check(stop.cause == StopCause::Pause && SeenIs(list, { { Lifetime::Running, StopCause::NoCause }, { Lifetime::Parked, StopCause::Pause } }),
			"nothing stopped for the capture: published %s, the pause the other thread asked for", SeenText(list).c_str());
		Check(sgc::test::Arms().lastRv == 0 && !sgc::test::Arms().lastStopWhenDone && sgc::test::Arms().lastOnEmu,
			"the capture was armed on the emulator thread, without a stop at its end");
	}

	CoreOff();

	CO_END(st_run.co);
}

/*
	sgc.second: a second state.recordSgcFrames while a capture records is -32003, and the first goes on. The first is
	held open at a breakpoint of the DSP on step 0, a stop that keeps it recording
*/
namespace {

struct {
	Co co;
	u32 taken;
} st_second;

}

BOUNDED_GUEST_TEST(sgc_second, "sgc.second")
{
	CO_BEGIN(st_second.co);

	while (!Settle(stop))
		CO_YIELD(st_second.co);

	if (!CoreOn())
		break;

	DspBps({ { kAtId, 0 } });
	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 64 } });
	CO_YIELD(st_second.co);

	st_second.taken = sgc::test::GetRecording().taken;

	Check(stop.cause == StopCause::Breakpoint && Recording() && st_second.taken == 1 && ResponsesTo(kClient, 1).empty(),
		"the capture records at the breakpoint's stop of the next burst, one frame taken, and its request is not answered");

	Ask(kClient, 2, "state.recordSgcFrames", { { "frames", 16 } });

	{
		const std::string second = WaitResponse(kClient, 2, 2000);
		const int direct = sgc::Arm(16, true);
		const sgc::test::Recording rec = sgc::test::GetRecording();

		Check(InBandCode(second) == kErrNotPaused, "a second state.recordSgcFrames is -32003 (%s)", second.c_str());
		Check(direct == kErrNotPaused && rec.wanted == 64 && rec.taken == st_second.taken && ResponsesTo(kClient, 1).empty(),
			"so is a second sgc::Arm, and the first capture goes on: 64 frames asked, %u taken", rec.taken);
	}

	DspBps({ });

	if (!Resume())
		break;
	CO_YIELD(st_second.co);

	{
		const std::string first = WaitResponse(kClient, 1, 2000);

		Check(stop.cause == StopCause::Capture && ServerFrames() == 64 && InBandCode(first) == 0 && ResponsesTo(kClient, 1).size() == 1
			&& ResponsesTo(kClient, 2).size() == 1, "resumed, the first capture completes with its 64 frames and its response {}; each request has one answer");
	}

	CoreOff();

	CO_END(st_second.co);
}

/*
	sgc.previous: while a capture records the server's capture is the one before; when it is complete, the new one
*/
namespace {

struct {
	Co co;
	std::string before;
} st_prev;

}

BOUNDED_GUEST_TEST(sgc_previous, "sgc.previous")
{
	u32 frames;

	CO_BEGIN(st_prev.co);

	while (!Settle(stop))
		CO_YIELD(st_prev.co);

	if (!ArmAtStop(20))
		break;
	CO_YIELD(st_prev.co);

	st_prev.before = ServerCapture();

	Check(stop.cause == StopCause::Capture && ServerFrames() == 20 && st_prev.before.size() == 20 * sgc::kBytesPerFrame, "a capture of 20 frames is the server's");

	// held open at a breakpoint of the DSP
	DspBps({ { kAtId, 0 } });

	if (!ArmAtStop(64))
		break;
	CO_YIELD(st_prev.co);

	frames = 0;

	Check(stop.cause == StopCause::Breakpoint && sgc::GetState(&frames) == sgc::State::Recording && frames == 20 && ServerCapture() == st_prev.before,
		"while a capture of 64 frames records the server's capture is the one before, of %u frames, as it was", frames);

	DspBps({ });

	if (!Resume())
		break;
	CO_YIELD(st_prev.co);

	frames = 0;

	Check(stop.cause == StopCause::Capture && sgc::GetState(&frames) == sgc::State::None && frames == 64 && ServerCapture().size() == 64 * sgc::kBytesPerFrame,
		"complete, the new capture is the server's: %u frames", frames);

	CO_END(st_prev.co);
}

/*
	sgc.reset: a reset cancels a capture that records and answers its request -32014 at once, and leaves the server's
	capture as it was: control.reset at a stop (emu::Reset, which the method calls), with the capture held open at a
	breakpoint of the DSP; and a reset of the guest while the machine runs. A capture that the reset did not cancel
	would go on recording in the machine that boots, and stop behind its answered request
*/
namespace {

struct {
	Co co;
	u32 before, arms;
	Clock::time_point t0;
	std::atomic<bool> failed { false };
	std::atomic<int> code { 1 };
	std::atomic<double> ms { 0 };
} st_reset;

}

BOUNDED_GUEST_TEST(sgc_reset, "sgc.reset")
{
	std::vector<Seen> list;

	CO_BEGIN(st_reset.co);

	while (!Settle(stop))
		CO_YIELD(st_reset.co);

	if (!CoreOn())
		break;

	// a capture before it, which the reset leaves
	if (!ArmAtStop(12))
		break;
	CO_YIELD(st_reset.co);

	st_reset.before = ServerFrames();

	DspBps({ { kAtId, 0 } });
	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 4096 } });
	CO_YIELD(st_reset.co);

	Check(stop.cause == StopCause::Breakpoint && Recording(), "the capture records at the breakpoint's stop");

	DspBps({ });
	st_reset.arms = sgc::test::Arms().armed;
	TakeSeen();

	st_reset.t0 = Clock::now();
	Check(emu::Reset() == 0, "control.reset at the stop: the machine is reset");

	{
		const std::string response = WaitResponse(kClient, 1, 2000);

		Check(InBandCode(response) == kErrCaptureTimeout && MsSince(st_reset.t0) < 1000, "its request is answered -32014 at once (%s, %.0f ms behind the reset)",
			response.c_str(), MsSince(st_reset.t0));
		Check(!Recording() && dbg_sgc_active == 0 && ServerFrames() == st_reset.before, "no capture records, and the server's capture is the one before (%u frames)",
			ServerFrames());
	}

	CO_YIELD(st_reset.co);

	Check(stop.cause == StopCause::Reset, "the stop of the reset is published (\"%s\")", StopCauseName(stop.cause));

	// the machine boots again: nothing of the capture is left to stop it
	while (!PauseAfter(stop, 4))
		CO_YIELD(st_reset.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Pause && !SeenHas(list, Lifetime::Parked, StopCause::Capture) && sgc::test::Arms().armed == st_reset.arms
		&& ResponsesTo(kClient, 1).size() == 1, "the machine runs on with no stop of the capture behind its answered request (%s)", SeenText(list).c_str());

	/*
		a reset of the guest while the machine runs
	*/
	while (!Settle(stop))
		CO_YIELD(st_reset.co);

	st_reset.before = ServerFrames();
	st_reset.failed = false;
	st_reset.code = 1;
	TakeSeen();

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Running))
		{
			st_reset.failed = true;
			return;
		}

		Ask(kClient, 2, "state.recordSgcFrames", { { "frames", 4096 } });

		// armed: the request is pending
		for (Clock::time_point t0 = Clock::now(); !core::CaptureCountsForTest().pending; Nap(1))
		{
			if (MsSince(t0) > 5000)
			{
				st_reset.failed = true;
				emu::RequestPause();
				return;
			}
		}

		const Clock::time_point t0 = Clock::now();

		virtualDreamcast->RequestReset();

		const std::string response = WaitResponse(kClient, 2, 3000);

		st_reset.code = InBandCode(response);
		st_reset.ms = MsSince(t0);

		Nap(100);
		emu::RequestPause();
	});

	if (!Resume())
		break;
	CO_YIELD(st_reset.co);

	OtherJoin();
	list = TakeSeen();

	Check(!st_reset.failed && st_reset.code == kErrCaptureTimeout && st_reset.ms < 1000,
		"a reset of the guest while a capture armed while running records: its request is answered -32014 at once (%d, %.0f ms)", st_reset.code.load(),
		st_reset.ms.load());
	Check(stop.cause == StopCause::Pause && !Recording() && ServerFrames() == st_reset.before && !SeenHas(list, Lifetime::Parked, StopCause::Capture),
		"the machine reset and ran on to the pause, no capture records and the server's capture is the one before (%s)", SeenText(list).c_str());

	CoreOff();

	CO_END(st_reset.co);
}

/*
	sgc.hoststop: a host stop and a guest exit cancel a capture that records and answer its request -32001 at once:
	the thread runs the burst it was parked in to its end in front of its stop callback (dc_run), with no check and
	no stop, and its exit cancels the capture. The thread the host starts next records nothing of it
*/
namespace {

struct {
	Co co;
	u32 round, arms;
	Clock::time_point t0;
	std::atomic<bool> failed { false };
	std::atomic<int> code { 1 };
	std::atomic<double> ms { 0 };
	std::atomic<u32> active { 9 };
	std::atomic<bool> recording { true };
	emu::RunState stopped;
} st_host;

}

BOUNDED_GUEST_TEST(sgc_hoststop, "sgc.hoststop")
{
	std::vector<Seen> list;

	CO_BEGIN(st_host.co);

	for (st_host.round = 0; st_host.round < 2; st_host.round++)
	{
		while (!Settle(stop))
			CO_YIELD(st_host.co);

		if (!CoreOn())
			break;

		DspBps({ { kAtId, 0 } });
		Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 4096 } });
		CO_YIELD(st_host.co);

		if (!Check(stop.cause == StopCause::Breakpoint && Recording(), "the capture records at the breakpoint's stop"))
			break;

		st_host.arms = sgc::test::Arms().armed;
		st_host.failed = false;
		st_host.code = 1;
		st_host.active = 9;
		st_host.recording = true;
		st_host.t0 = Clock::now();

		OtherStart([] {
			if (!WaitLifetime(Lifetime::Stopped))
			{
				st_host.failed = true;
				return;
			}

			st_host.stopped = emu::GetRunState();

			const std::string response = WaitResponse(kClient, 1, 3000);

			st_host.code = InBandCode(response);
			st_host.ms = MsSince(st_host.t0);

			// with no emulator thread the caller runs it itself
			emu::Call(emu::CC_TABLE, [] {
				st_host.active = dbg_sgc_active;
				st_host.recording = Recording();
			});

			Restart(false);
		});

		if (st_host.round == 0)
			virtualDreamcast->Stop([] { });
		else
			virtualDreamcast->RequestExit();
		CO_YIELD(st_host.co);

		OtherJoin();

		Check(!st_host.failed && st_host.stopped.cause == (st_host.round == 0 ? StopCause::HostStopped : StopCause::GuestExit),
			"%s while the thread is parked at the breakpoint's stop, with the capture recording", st_host.round == 0 ? "the host stops the machine" : "the guest ends the run");
		Check(st_host.code == kErrHostStopped && st_host.ms < 2000, "its request is answered -32001 at once (%d, %.0f ms behind the stop asked for)",
			st_host.code.load(), st_host.ms.load());
		Check(st_host.active == 0 && !st_host.recording, "behind the thread no capture records: dbg_sgc_active is %u", st_host.active.load());
		Check(stop.cause == StopCause::Start, "the host starts the next thread (\"%s\")", StopCauseName(stop.cause));

		DspBps({ });
		TakeSeen();

		while (!PauseAfter(stop, 3))
			CO_YIELD(st_host.co);

		list = TakeSeen();

		Check(stop.cause == StopCause::Pause && !SeenHas(list, Lifetime::Parked, StopCause::Capture) && sgc::test::Arms().armed == st_host.arms
			&& ResponsesTo(kClient, 1).size() == 1 && core::CaptureCountsForTest().owed == 0,
			"the next thread records nothing of it and stops for nothing of it; the request had one answer (%s)", SeenText(list).c_str());

		CoreOff();
	}

	CO_END(st_host.co);
}

/*
	sgc.held: a capture armed while the guest holds its ARM7, for every burst of the capture: the AICA is clocked
	whatever ARMRST says, so it records its frames in as many time steps, with no ARM phase, and completes; the two
	channels the guest keyed play in every frame of it
*/
namespace {

struct {
	Co co;
	u32 stopId;
	Counts c;
} st_held;

const u32 kHeldFrames = 100;

}

BOUNDED_GUEST_TEST(sgc_held, "sgc.held")
{
	Counts c;

	CO_BEGIN(st_held.co);

	while (!Settle(stop))
		CO_YIELD(st_held.co);

	while (!HoldArm7(stop))
		CO_YIELD(st_held.co);

	if (!CoreOn())
		break;

	st_held.c = ReadCounts();
	st_held.stopId = stop.stopId;
	HandPauseAfter(10);

	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", kHeldFrames } });
	CO_YIELD(st_held.co);

	HandOff();
	c = ReadCounts();

	{
		const std::string response = WaitResponse(kClient, 1, 2000);

		Check(stop.cause == StopCause::Capture && !ArmClocked() && c.n[2] - st_held.c.n[2] == kHeldFrames && c.n[1] == st_held.c.n[1],
			"armed with the ARM7 held: the capture's stop (\"%s\") is %u time steps behind the arm, with no ARM phase (%u), the ARM7 held all along",
			StopCauseName(stop.cause), c.n[2] - st_held.c.n[2], c.n[1] - st_held.c.n[1]);
		Check(InBandCode(response) == 0 && ServerFrames() == kHeldFrames, "its response is {} and the server has its %u frames (%s)", kHeldFrames, response.c_str());
	}

	{
		const std::string capture = ServerCapture();
		u32 playing = 0;

		for (u32 f = 0; f < kHeldFrames && capture.size() == kHeldFrames * sgc::kBytesPerFrame; f++)
			playing += Plays(RecordOf(capture, f, 0)) && Plays(RecordOf(capture, f, 1));

		Check(playing == kHeldFrames, "the two channels the guest keyed play in every frame: flags bit 0 set and a level above 0 (%u of %u frames)", playing, kHeldFrames);
	}

	CoreOff();

	CO_END(st_held.co);
}

/*
	sgc.timeout: a capture whose machine does not run is answered by its deadline, 5 s of host time behind the
	request: the core cancels it in a call (sgc::Cancel(kErrCaptureTimeout)), whose notice answers -32014. The
	capture is held at a breakpoint of the DSP and the test lets the park loop take calls; the other thread waits
	for the answer and then steps the DSP, which gives the test its next stop. A later resume records no stop
*/
namespace {

struct {
	Co co;
	u32 before;
	Clock::time_point t0;
	core::CaptureCounts counts;
	std::atomic<int> code { 1 };
	std::atomic<double> ms { 0 };
} st_time;

}

BOUNDED_GUEST_TEST(sgc_timeout, "sgc.timeout")
{
	std::vector<Seen> list;

	CO_BEGIN(st_time.co);

	while (!Settle(stop))
		CO_YIELD(st_time.co);

	if (!CoreOn())
		break;

	st_time.before = ServerFrames();
	st_time.counts = core::CaptureCountsForTest();

	DspBps({ { kAtId, 0 } });
	st_time.t0 = Clock::now();
	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 4096 } });
	CO_YIELD(st_time.co);

	if (!Check(stop.cause == StopCause::Breakpoint && Recording(), "the capture records at the breakpoint's stop, and the machine stands"))
		break;

	st_time.code = 1;

	OtherStart([] {
		Clock::time_point when = Clock::now();
		const std::string response = WaitResponse(kClient, 1, 9000, &when);

		st_time.code = InBandCode(response);
		st_time.ms = std::chrono::duration<double, std::milli>(when - st_time.t0).count();

		// the next stop of the test
		emu::Call(emu::CC_EXEC, [] { emu::Step(Target::Dsp, StepKind::Into); });
	});

	// nothing armed: the park loop takes the calls, the deadline's and the step's
	CO_YIELD(st_time.co);

	OtherJoin();

	{
		const core::CaptureCounts counts = core::CaptureCountsForTest();

		Check(st_time.code == kErrCaptureTimeout && st_time.ms >= kCaptureTimeoutMs && st_time.ms < kCaptureTimeoutMs + 1500,
			"the request is answered -32014 at its deadline, %.0f ms behind it (%d)", st_time.ms.load(), st_time.code.load());
		Check(counts.deadlinesTaken == st_time.counts.deadlinesTaken + 1 && counts.answeredByCore == st_time.counts.answeredByCore && counts.owed == 0 && !counts.pending,
			"through the notice of the cancel the deadline's call made, which the park loop took: the core answered nothing itself");
		Check(stop.cause == StopCause::Step && !Recording() && dbg_sgc_active == 0 && ServerFrames() == st_time.before,
			"the capture is cancelled, and the server's capture is the one before (\"%s\")", StopCauseName(stop.cause));
	}

	DspBps({ });
	TakeSeen();

	while (!PauseAfter(stop, 5))
		CO_YIELD(st_time.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Pause && !SeenHas(list, Lifetime::Parked, StopCause::Capture) && ServerFrames() == st_time.before
		&& ResponsesTo(kClient, 1).size() == 1, "a later resume records no stop of it and no frame (%s)", SeenText(list).c_str());

	CoreOff();

	CO_END(st_time.co);
}

/*
	sgc.late: the deadline met by a completion that its cancel call comes behind. The capture of two frames is held
	at a breakpoint of the DSP with one frame taken; there the deadline is made now (core::ExpireCaptureDeadlineForTest)
	and the test waits on the emulator thread until the core has made its cancel call, which waits in the queue; then
	the resume completes the capture in the run-on, and its notice comes in front of the cancel. The call is taken,
	finds nothing to cancel, and the notice answers: one answer, the completion's. A request behind it is answered by
	its own capture
*/
namespace {

struct {
	Co co;
	core::CaptureCounts counts;
	std::atomic<bool> failed { false };
	std::atomic<int> first { 1 }, second { 1 };
	std::atomic<u32> firstAnswers { 0 };
} st_late;

}

BOUNDED_GUEST_TEST(sgc_late, "sgc.late")
{
	CO_BEGIN(st_late.co);

	while (!Settle(stop))
		CO_YIELD(st_late.co);

	if (!CoreOn())
		break;

	DspBps({ { kAtId, 0 } });
	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 2 } });
	CO_YIELD(st_late.co);

	if (!Check(stop.cause == StopCause::Breakpoint && sgc::test::GetRecording().taken == 1, "the capture of 2 frames is held at the breakpoint's stop with one frame taken"))
		break;

	DspBps({ });
	st_late.counts = core::CaptureCountsForTest();

	core::ExpireCaptureDeadlineForTest();

	for (Clock::time_point t0 = Clock::now(); core::CaptureCountsForTest().deadlinesMade == st_late.counts.deadlinesMade && MsSince(t0) < 2000; Nap(1))
		;

	// the call is in the queue of the emulator thread, behind the count
	Nap(20);

	Check(core::CaptureCountsForTest().deadlinesMade == st_late.counts.deadlinesMade + 1 && ResponsesTo(kClient, 1).empty() && Recording(),
		"the deadline came, and the core made its cancel call, which waits: the request is not answered and the capture records");

	if (!Resume())
		break;
	CO_YIELD(st_late.co);

	Check(stop.cause == StopCause::Capture && ServerFrames() == 2 && !Recording(), "the resume completes the capture in front of the cancel call (\"%s\")",
		StopCauseName(stop.cause));

	st_late.failed = false;

	OtherStart([] {
		st_late.first = InBandCode(WaitResponse(kClient, 1, 3000));

		// a second answer would come behind the first
		Nap(300);
		st_late.firstAnswers = (u32)ResponsesTo(kClient, 1).size();

		Ask(kClient, 2, "state.recordSgcFrames", { { "frames", 3 } });
		st_late.second = InBandCode(WaitResponse(kClient, 2, 5000));
	});

	// nothing armed: the park loop takes the cancel call, and then the call of the second request
	CO_YIELD(st_late.co);

	OtherJoin();

	{
		const core::CaptureCounts counts = core::CaptureCountsForTest();

		Check(st_late.first == 0 && st_late.firstAnswers == 1, "the first request has one answer, the completion's: {} (%d, %u answers)", st_late.first.load(),
			st_late.firstAnswers.load());
		Check(counts.deadlinesTaken == st_late.counts.deadlinesTaken + 1 && counts.answeredByCore == st_late.counts.answeredByCore,
			"the cancel call was taken, and the core answered nothing itself");
		Check(stop.cause == StopCause::Capture && st_late.second == 0 && ServerFrames() == 3 && ResponsesTo(kClient, 2).size() == 1,
			"a request behind it is answered by its own capture, of 3 frames (%d)", st_late.second.load());
	}

	CoreOff();

	CO_END(st_late.co);
}

/*
	sgc.untaken: the deadline's cancel call is not taken - the test holds the emulator thread at the stop beyond the
	2 s a call waits - so the core answers -32014 itself; the notice stays owed, a new request is -32003 until it has
	come, the cancel is made again, and that notice answers nothing. A request behind it is answered by its own
	capture
*/
namespace {

struct {
	Co co;
	core::CaptureCounts counts;
	std::atomic<bool> failed { false };
	std::atomic<u32> answers { 0 }, unpaired { 0 };
	std::atomic<int> third { 1 };
} st_untaken;

}

BOUNDED_GUEST_TEST(sgc_untaken, "sgc.untaken")
{
	core::CaptureCounts counts;

	CO_BEGIN(st_untaken.co);

	while (!Settle(stop))
		CO_YIELD(st_untaken.co);

	if (!CoreOn())
		break;

	DspBps({ { kAtId, 0 } });
	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 4096 } });
	CO_YIELD(st_untaken.co);

	if (!Check(stop.cause == StopCause::Breakpoint && Recording(), "the capture is held at the breakpoint's stop"))
		break;

	DspBps({ });
	st_untaken.counts = core::CaptureCountsForTest();

	// the deadline, and the emulator thread held here while its cancel call waits
	core::ExpireCaptureDeadlineForTest();

	for (Clock::time_point t0 = Clock::now(); core::CaptureCountsForTest().deadlinesUntaken == st_untaken.counts.deadlinesUntaken && MsSince(t0) < 5000; Nap(2))
		;

	{
		const std::string first = WaitResponse(kClient, 1, 1000);

		counts = core::CaptureCountsForTest();

		Check(counts.deadlinesUntaken == st_untaken.counts.deadlinesUntaken + 1 && counts.answeredByCore == st_untaken.counts.answeredByCore + 1
			&& InBandCode(first) == kErrCaptureTimeout && !counts.pending && counts.owed == 1,
			"the cancel call was not taken: the core answers -32014 itself, and the notice stays owed (%s, %u owed)", first.c_str(), counts.owed);
		Check(Recording(), "the capture still records: nothing cancelled it");
	}

	Ask(kClient, 2, "state.recordSgcFrames", { { "frames", 16 } });

	{
		const std::string second = WaitResponse(kClient, 2, 1000);

		Check(InBandCode(second) == kErrNotPaused, "a new request is -32003 while the notice is owed (%s)", second.c_str());
	}

	st_untaken.failed = false;

	OtherStart([] {
		// the cancel made again is taken once the test lets the park loop take calls, and its notice comes
		for (Clock::time_point t0 = Clock::now(); core::CaptureCountsForTest().owed != 0; Nap(2))
		{
			if (MsSince(t0) > 8000)
			{
				st_untaken.failed = true;
				emu::Call(emu::CC_EXEC, [] { emu::Step(Target::Dsp, StepKind::Into); });
				return;
			}
		}

		Nap(200);
		st_untaken.answers = (u32)ResponsesTo(kClient, 1).size();
		st_untaken.unpaired = core::CaptureCountsForTest().unpaired;

		Ask(kClient, 3, "state.recordSgcFrames", { { "frames", 2 } });
		st_untaken.third = InBandCode(WaitResponse(kClient, 3, 5000));
	});

	CO_YIELD(st_untaken.co);

	OtherJoin();
	counts = core::CaptureCountsForTest();

	Check(!st_untaken.failed && counts.cancelsAgain >= st_untaken.counts.cancelsAgain + 1 && st_untaken.unpaired == st_untaken.counts.unpaired + 1
		&& st_untaken.answers == 1, "the cancel made again cancelled the capture, and its notice answered nothing: the first request has its one answer");
	Check(stop.cause == StopCause::Capture && st_untaken.third == 0 && ServerFrames() == 2 && !Recording(),
		"a request behind the notice is answered by its own capture, of 2 frames (%d)", st_untaken.third.load());

	CoreOff();

	CO_END(st_untaken.co);
}

/*
	sgc.refused: the resume behind the arm is refused (a test hook arms a step of the DSP in front of the arm, in the
	call of the core): the capture is cancelled again, and the notice answers -32003. Nothing records, and the step
	that was armed makes the next stop
*/
namespace {

struct {
	Co co;
	u32 before;
	sgc::test::ArmRecord arms;
	sgc::test::NoticeRecord notices;
} st_ref;

void ArmAStep()
{
	emu::Step(Target::Dsp, StepKind::Into);
}

}

BOUNDED_GUEST_TEST(sgc_refused, "sgc.refused")
{
	std::vector<Seen> list;

	CO_BEGIN(st_ref.co);

	while (!Settle(stop))
		CO_YIELD(st_ref.co);

	if (!CoreOn())
		break;

	st_ref.before = ServerFrames();
	st_ref.arms = sgc::test::Arms();
	st_ref.notices = sgc::test::Notices();
	core::SetBeforeArmForTest(ArmAStep);

	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 50 } });
	CO_YIELD(st_ref.co);

	core::SetBeforeArmForTest(nullptr);

	{
		const std::string response = WaitResponse(kClient, 1, 2000);
		const sgc::test::ArmRecord arms = sgc::test::Arms();
		const sgc::test::NoticeRecord notices = sgc::test::Notices();

		Check(stop.cause == StopCause::Step, "the step armed in front of the arm makes the stop (\"%s\")", StopCauseName(stop.cause));
		Check(arms.armed == st_ref.arms.armed + 1 && notices.given == st_ref.notices.given + 1 && notices.lastAnswer == kErrNotPaused,
			"the capture was armed, and cancelled with -32003 when the resume behind it was refused");
		Check(InBandCode(response) == kErrNotPaused && !Recording() && dbg_sgc_active == 0 && core::CaptureCountsForTest().owed == 0,
			"the notice answered the request -32003, and no capture records (%s)", response.c_str());
	}

	TakeSeen();

	while (!PauseAfter(stop, 4))
		CO_YIELD(st_ref.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Pause && !SeenHas(list, Lifetime::Parked, StopCause::Capture) && ServerFrames() == st_ref.before,
		"a resume records nothing: no stop of a capture, the server's capture the one before (%s)", SeenText(list).c_str());

	CoreOff();

	CO_END(st_ref.co);
}

/*
	sgc.nothread: the table call of a request finds no emulator thread - the thread ended between the method's look at
	the lifetime (Running) and the call (a test hook stops the machine there) - and runs on the core thread under L.
	Its closure arms nothing, and the method answers by the lifetime: -32001. The thread the host starts next records
	nothing for it
*/
namespace {

struct {
	Co co;
	u32 calls, before;
	std::atomic<bool> failed { false };
	std::atomic<int> code { 1 };
	std::atomic<u32> callsAtStop { 0 }, active { 9 };
	std::atomic<bool> recording { true };
} st_nothread;

// On the core thread, between its look at the lifetime and its call: the host stops the machine, and the thread ends
void EndTheThread()
{
	virtualDreamcast->Stop([] { });

	if (!WaitLifetime(Lifetime::Stopped, 10000))
		st_nothread.failed = true;
}

}

BOUNDED_GUEST_TEST(sgc_nothread, "sgc.nothread")
{
	std::vector<Seen> list;

	CO_BEGIN(st_nothread.co);

	while (!Settle(stop))
		CO_YIELD(st_nothread.co);

	if (!CoreOn())
		break;

	st_nothread.calls = sgc::test::Arms().calls;
	st_nothread.before = ServerFrames();
	st_nothread.failed = false;
	st_nothread.code = 1;

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Running))
		{
			st_nothread.failed = true;
			return;
		}

		core::SetBeforeTableCallForTest(EndTheThread);
		Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 50 } });
		st_nothread.code = InBandCode(WaitResponse(kClient, 1, 10000));
		core::SetBeforeTableCallForTest(nullptr);

		// with no emulator thread the caller runs it itself
		emu::Call(emu::CC_TABLE, [] {
			st_nothread.callsAtStop = sgc::test::Arms().calls;
			st_nothread.active = dbg_sgc_active;
			st_nothread.recording = Recording();
		});

		Restart(false);
	});

	if (!Resume())
		break;
	CO_YIELD(st_nothread.co);

	OtherJoin();

	Check(!st_nothread.failed && st_nothread.code == kErrHostStopped, "the closure found no emulator thread: the request is answered by the lifetime, -32001 (%d)",
		st_nothread.code.load());
	Check(st_nothread.callsAtStop == st_nothread.calls && st_nothread.active == 0 && !st_nothread.recording,
		"it armed nothing: sgc::Arm was not called (%u calls), dbg_sgc_active is %u", st_nothread.callsAtStop - st_nothread.calls, st_nothread.active.load());
	Check(stop.cause == StopCause::Start, "the host starts the next thread (\"%s\")", StopCauseName(stop.cause));

	TakeSeen();

	while (!PauseAfter(stop, 4))
		CO_YIELD(st_nothread.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Pause && !SeenHas(list, Lifetime::Parked, StopCause::Capture) && sgc::test::Arms().calls == st_nothread.calls
		&& ServerFrames() == st_nothread.before && core::CaptureCountsForTest().owed == 0 && ResponsesTo(kClient, 1).size() == 1,
		"the next thread records nothing for it, and the request has its one answer (%s)", SeenText(list).c_str());

	CoreOff();

	CO_END(st_nothread.co);
}

/*
	sgc.drain: the request is made while the emulator thread naps in the callback of a host stop, where the published
	state is still Running: its table call waits in the queue, and the drain of the thread's exit (emu::OnThreadExiting)
	takes it, on the emulator thread, where it arms the capture - and the cancel behind the drain answers it -32001,
	well within the deadline. Behind the thread no capture records, and the thread the host starts next records no
	frame of it and gives no second answer
*/
namespace {

struct {
	Co co;
	sgc::test::ArmRecord arms;
	u32 before;
	std::atomic<bool> failed { false };
	std::atomic<bool> inCallback { false };
	std::atomic<int> code { 1 };
	std::atomic<double> ms { 0 };
	std::atomic<u32> active { 9 };
	std::atomic<bool> recording { true };
	sgc::test::ArmRecord atStop;
} st_drain;

}

BOUNDED_GUEST_TEST(sgc_drain, "sgc.drain")
{
	std::vector<Seen> list;

	CO_BEGIN(st_drain.co);

	while (!Settle(stop))
		CO_YIELD(st_drain.co);

	if (!CoreOn())
		break;

	st_drain.arms = sgc::test::Arms();
	st_drain.before = ServerFrames();
	st_drain.failed = false;
	st_drain.inCallback = false;
	st_drain.code = 1;

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Running))
		{
			st_drain.failed = true;
			return;
		}

		virtualDreamcast->Stop([] {
			st_drain.inCallback = true;
			Nap(300);
		});

		for (Clock::time_point t0 = Clock::now(); !st_drain.inCallback; Nap(1))
		{
			if (MsSince(t0) > 10000)
			{
				st_drain.failed = true;
				return;
			}
		}

		// the thread is on its way out and has not said so yet
		const Clock::time_point t0 = Clock::now();

		Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 4096 } });
		st_drain.code = InBandCode(WaitResponse(kClient, 1, 8000));
		st_drain.ms = MsSince(t0);

		if (!WaitLifetime(Lifetime::Stopped))
			st_drain.failed = true;

		// with no emulator thread the caller runs it itself
		emu::Call(emu::CC_TABLE, [] {
			st_drain.atStop = sgc::test::Arms();
			st_drain.active = dbg_sgc_active;
			st_drain.recording = Recording();
		});

		Restart(false);
	});

	if (!Resume())
		break;
	CO_YIELD(st_drain.co);

	OtherJoin();

	Check(!st_drain.failed && st_drain.atStop.calls == st_drain.arms.calls + 1 && st_drain.atStop.armed == st_drain.arms.armed + 1 && st_drain.atStop.lastRv == 0
		&& st_drain.atStop.lastOnEmu && !st_drain.atStop.lastStopWhenDone,
		"the drain of the thread's exit took the table call: its closure ran on the emulator thread, where sgc::Arm armed the capture (returned %d)",
		st_drain.atStop.lastRv);
	Check(st_drain.code == kErrHostStopped && st_drain.ms < 2000, "the cancel behind the drain answered the request -32001, %.0f ms behind it (%d)", st_drain.ms.load(),
		st_drain.code.load());
	Check(st_drain.active == 0 && !st_drain.recording, "behind the thread no capture records: dbg_sgc_active is %u", st_drain.active.load());
	Check(stop.cause == StopCause::Start, "the host starts the next thread (\"%s\")", StopCauseName(stop.cause));

	TakeSeen();

	while (!PauseAfter(stop, 4))
		CO_YIELD(st_drain.co);

	list = TakeSeen();

	Check(stop.cause == StopCause::Pause && !SeenHas(list, Lifetime::Parked, StopCause::Capture) && ServerFrames() == st_drain.before
		&& sgc::test::Arms().calls == st_drain.arms.calls + 1 && ResponsesTo(kClient, 1).size() == 1 && core::CaptureCountsForTest().owed == 0,
		"the thread the host started records no frame of it and gives no second answer (%s)", SeenText(list).c_str());

	CoreOff();

	CO_END(st_drain.co);
}

/*
	sgc.merge: the last frame of a capture in the sample of a DSP step's stop. The capture of two frames is held at a
	breakpoint on step 127 with one frame taken; a step of step 127 leaves the burst at the end of that sample, and the
	next step goes through the next sample's sweep - its frame, the capture's last - to step 0 and stops in front of
	step 1. The two stops meet: the step's is published, and the response of the capture is sent
*/
namespace {

struct {
	Co co;
	u32 stopId;
} st_merge;

}

BOUNDED_GUEST_TEST(sgc_merge, "sgc.merge")
{
	std::vector<Seen> list;

	CO_BEGIN(st_merge.co);

	while (!Settle(stop))
		CO_YIELD(st_merge.co);

	if (!CoreOn())
		break;

	while (!SuspendAtStep(stop, kLast))
		CO_YIELD(st_merge.co);

	Ask(kClient, 1, "state.recordSgcFrames", { { "frames", 2 } });
	CO_YIELD(st_merge.co);

	if (!Check(stop.cause == StopCause::Breakpoint && DspStep() == kLast && sgc::test::GetRecording().taken == 1,
		"the capture of 2 frames is held at the breakpoint on step 127 of the next sample, one frame taken (\"%s\")", StopCauseName(stop.cause)))
		break;

	DspBps({ });

	if (!StepDsp())
		break;
	CO_YIELD(st_merge.co);

	Check(stop.cause == StopCause::Step && Suspended() == SOUND_BURST_SAMPLE && sgc::test::GetRecording().taken == 1,
		"a step of step 127 leaves the burst at the end of its sample, and no frame more: no sweep came");

	st_merge.stopId = stop.stopId;
	TakeSeen();

	if (!StepDsp())
		break;
	CO_YIELD(st_merge.co);

	list = TakeSeen();

	{
		const std::string response = WaitResponse(kClient, 1, 2000);

		Check(stop.cause == StopCause::Step && stop.stopId == st_merge.stopId + 1 && DspStep() == 1 && Suspended() == SOUND_BURST_DSP
			&& SeenIs(list, { { Lifetime::Parked, StopCause::Step } }),
			"the next step: the sweep of the next sample, its frame, step 0, and the stop in front of step 1 is the step's (published %s)", SeenText(list).c_str());
		Check(!Recording() && ServerFrames() == 2 && InBandCode(response) == 0, "the frame was the capture's last: complete, and its response {} is sent (%s)",
			response.c_str());
	}

	CoreOff();

	CO_END(st_merge.co);
}

/*
	sgc.outputs: the outputs the sound generator keeps for the capture are those of the sample. The guest holds its
	ARM7, so nothing changes the channels' registers, and the test leaves one channel alone in the mix: the first
	channel's direct level and the levels of the DSP's outputs at 0 (as the SH4 writes them, AICA::WriteReg). At the
	end of a sample - a step of step 127 of the DSP, from its breakpoint, puts the burst there, behind the sample's
	mix - the second channel's kept outputs are then the pair the mix handed to the audio stream, in sample after
	sample. While no capture records, every channel's outputs read 0, the sounding ones included
*/
namespace {

const u32 kOutputRounds = 5;

struct {
	Co co;
	u32 round, efreg0, efreg1, pan0, moved, nonzero;
	s32 last;
} st_out;

bool NoOutputs(u32* sounding)
{
	bool none = true;

	*sounding = 0;

	for (u32 channel = 0; channel < sgc::kChannelsPerFrame; channel++)
	{
		AicaChannelDebug d;

		memset(&d, 0xA5, sizeof(d));
		Aica()->DebugGetChannel(channel, &d);

		none = none && d.outLeft == 0 && d.outRight == 0 && d.outDsp == 0;
		*sounding += d.enabled && d.aegLevel > 0;
	}

	return none;
}

}

BOUNDED_GUEST_TEST(sgc_outputs, "sgc.outputs")
{
	u32 sounding;

	CO_BEGIN(st_out.co);

	while (!Settle(stop))
		CO_YIELD(st_out.co);

	while (!HoldArm7(stop))
		CO_YIELD(st_out.co);

	while (!PauseAfter(stop, 2))
		CO_YIELD(st_out.co);

	Check(NoOutputs(&sounding) && sounding >= 2, "no capture records: every channel's outputs read 0, also those of the %u that sound", sounding);

	// one channel alone in the mix
	st_out.efreg0 = AicaReg16(kEFREG0);
	st_out.efreg1 = AicaReg16(kEFREG0 + 4);
	st_out.pan0 = AicaReg16(kDIPAN);

	Aica()->WriteReg(kEFREG0, st_out.efreg0 & ~0x0F00u, 2);
	Aica()->WriteReg(kEFREG0 + 4, st_out.efreg1 & ~0x0F00u, 2);
	Aica()->WriteReg(kDIPAN, st_out.pan0 & ~0x0F00u, 2);

	Check(sgc::Arm(sgc::kChannelsPerFrame * 64, false) == 0, "a capture records, armed with no stop at its end");

	st_out.moved = 0;
	st_out.nonzero = 0;
	st_out.last = 0;

	for (st_out.round = 0; st_out.round < kOutputRounds; st_out.round++)
	{
		while (!SuspendAtStep(stop, kLast))
			CO_YIELD(st_out.co);

		DspBps({ });

		if (!StepDsp())
			break;
		CO_YIELD(st_out.co);

		{
			AicaChannelDebug alone, silent, other;

			Aica()->DebugGetChannel(1, &alone);
			Aica()->DebugGetChannel(0, &silent);
			Aica()->DebugGetChannel(5, &other);

			Check(stop.cause == StopCause::Step && DspStep() == 0 && Recording() && alone.outLeft == pl && alone.outRight == pr,
				"at the end of a sample: the channel alone has the outputs %d and %d, the pair the mix handed to the audio stream (%d, %d)",
				alone.outLeft, alone.outRight, pl, pr);
			Check(silent.outLeft == 0 && silent.outRight == 0 && silent.outDsp != 0 && other.outLeft == 0 && other.outRight == 0 && other.outDsp == 0,
				"the first channel, at direct level 0, has no direct outputs and its send to the DSP (%d); a channel that does not play has none", silent.outDsp);

			st_out.moved += st_out.round != 0 && alone.outRight != st_out.last;
			st_out.nonzero += alone.outRight != 0;
			st_out.last = alone.outRight;
		}
	}

	Check(st_out.moved != 0 && st_out.nonzero != 0, "the outputs are those of each sample: on the side the channel is panned to they move %u times in %u samples, "
		"%u of them not 0", st_out.moved, kOutputRounds, st_out.nonzero);

	sgc::Cancel(kErrCaptureTimeout);

	Check(NoOutputs(&sounding) && sounding >= 1, "once no capture records the outputs read 0 again, the channel alone's included");

	Aica()->WriteReg(kEFREG0, st_out.efreg0, 2);
	Aica()->WriteReg(kEFREG0 + 4, st_out.efreg1, 2);
	Aica()->WriteReg(kDIPAN, st_out.pan0, 2);

	CO_END(st_out.co);
}

/*
	sgcn.*: the capture at work in a run that the sound harness, or an Exit.* key, ends. The sound line and the
	state hash of such a run have to be those of a run nobody touched (tests/debugger/guest/neutral,
	tests/debugger/guest/dsptest/soundlines.py --mode neutral): a capture only reads the guest, and its stop at the
	end of a sample is a stop like every other. A test of these never returns true: a session of it does not end by
	itself. Each says what it did in lines its target requires, so that a test that armed nothing is not taken for one
	that changed nothing. Each has done its work early, some hundreds of bursts into the run: no stop falls into the
	tick in which the run ends
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

const u32 kRunningSizes[] = { 1, 2, 3, 31, 32, 33, 64, 100, 511, 512, 513, 1000, 2048, 4096 };
const u32 kRunningCount = sizeof(kRunningSizes) / sizeof(kRunningSizes[0]);

struct {
	Co co;
	std::atomic<u32> done { 0 }, bad { 0 }, bursts { 0 };
	std::atomic<bool> listed { false };
	u32 c0;
} n_run;

}

/*
	sgcn.running: captures of 1 to 4096 frames while the machine runs, one behind the other: each armed in a table
	call (a service excursion), as the core arms one, and waited for by its notice; each the server's capture with its
	frames when it is complete. Then the one stop of the test, behind a burst
*/
BOUNDED_GUEST_TEST(sgcn_running, "sgcn.running")
{
	CO_BEGIN(n_run.co);

	emu::test::observer = Observer;
	emu::SetActiveMask(kMaskAll);

	n_run.done = 0;
	n_run.bad = 0;
	n_run.listed = false;
	n_run.c0 = ReadCounts().n[0];

	Check(stop.cause == StopCause::Start && !Recording(), "at the start of the run no capture records");

	OtherStart([] {
		if (!WaitLifetime(Lifetime::Running))
			return;

		for (u32 i = 0; i < kRunningCount; i++)
		{
			const u32 frames = kRunningSizes[i];
			const u32 given = sgc::test::Notices().given;
			std::atomic<int> armed { 1 };

			emu::Call(emu::CC_TABLE, [frames, &armed] { armed = emu::OnEmuThread() ? sgc::Arm(frames, false) : kErrHostStopped; });

			if (armed != 0)
			{
				n_run.bad++;
				continue;
			}

			for (Clock::time_point t0 = Clock::now(); sgc::test::Notices().given == given; std::this_thread::sleep_for(std::chrono::microseconds(500)))
			{
				if (MsSince(t0) > kBoundMs)
					return;
			}

			std::atomic<u32> have { 0 };

			emu::Call(emu::CC_READ, [&have] { have = ServerFrames(); });

			if (have == frames && sgc::test::Notices().lastAnswer == 0)
				n_run.done++;
			else
				n_run.bad++;
		}

		// the one stop of the test, behind a burst
		emu::Call(emu::CC_TABLE, [] {
			EventBp(kNeutralEvent);
			n_run.listed = true;
		});
	});

	if (!Resume())
		break;
	CO_YIELD(n_run.co);

	OtherJoin();
	NoEventBp();

	Check(stop.cause == StopCause::Event && stop.breakpointId == kNeutralEvent && n_run.listed && Suspended() == SOUND_BURST_NONE,
		"one stop behind a burst, some %u bursts into the run: \"%s\"", ReadCounts().n[0] - n_run.c0, StopCauseName(stop.cause));
	Check(n_run.done == kRunningCount && n_run.bad == 0 && !SeenHas(TakeSeen(), Lifetime::Parked, StopCause::Capture),
		"%u captures while running, of 1 to 4096 frames, each complete with its frames, and none stopped the run (%u were not)", n_run.done.load(), n_run.bad.load());

	NEUTRAL_END(n_run.co);

	CO_END(n_run.co);
}

/*
	sgcn.paused: captures armed at stops, each resumed, until each kind was taken often enough: behind a burst (the
	event breakpoint on the timers of the AICA), at the end of a sample (the stop of the capture before), in front of a
	DSP step and in front of an ARM7 instruction (breakpoints that go at their stop). Each stops at the end of the
	sample of its last frame, or ends its burst, with its frames and as many time steps of the AICA as it has frames
	(one more in front of a DSP step)
*/
namespace {

const u32 kPausedSizes[] = { 1, 3, 7, 31, 32, 33, 64, 100 };
const u32 kPausedSizeCount = sizeof(kPausedSizes) / sizeof(kPausedSizes[0]);
const u32 kPausedEach = 10;

struct {
	Co co;
	u32 at[AT_COUNT];
	u32 bad, other, n, where, frames;
	Counts c;
} n_paused;

bool PausedDone()
{
	for (u32 i = 0; i < AT_COUNT; i++)
	{
		if (n_paused.at[i] < kPausedEach)
			return false;
	}

	return true;
}

}

BOUNDED_GUEST_TEST(sgcn_paused, "sgcn.paused")
{
	Counts c;

	CO_BEGIN(n_paused.co);

	emu::test::observer = Observer;
	emu::SetActiveMask(kMaskAll);

	memset(n_paused.at, 0, sizeof(n_paused.at));
	n_paused.bad = n_paused.other = n_paused.n = 0;

	while (!PausedDone() && n_paused.bad == 0 && n_paused.other < 20)
	{
		// behind a burst
		EventBp(kNeutralEvent);

		if (!Resume())
			return false;
		CO_YIELD(n_paused.co);

		NoEventBp();

		if (stop.cause != StopCause::Event || Suspended() != SOUND_BURST_NONE)
		{
			n_paused.other++;
			continue;
		}

		for (n_paused.where = 0; n_paused.where < AT_COUNT; n_paused.where++)
		{
			if (n_paused.where == AT_ARM7)
			{
				ArmBps({ { 2, kMain } });

				if (!Resume())
					return false;
				CO_YIELD(n_paused.co);

				ArmBps({ });

				if (stop.cause != StopCause::Breakpoint || Suspended() != SOUND_BURST_ARM7)
				{
					n_paused.other++;
					break;
				}
			}
			else if (n_paused.where == AT_DSP)
			{
				DspBps({ { 3, kSite } });

				if (!Resume())
					return false;
				CO_YIELD(n_paused.co);

				DspBps({ });

				if (stop.cause != StopCause::Breakpoint || Suspended() != SOUND_BURST_DSP)
				{
					n_paused.other++;
					break;
				}
			}
			else if (n_paused.where == AT_SAMPLE && Suspended() != SOUND_BURST_SAMPLE)
			{
				// the capture before ended its burst: no end of a sample to arm at in this round
				continue;
			}

			n_paused.frames = kPausedSizes[n_paused.n++ % kPausedSizeCount];
			n_paused.c = ReadCounts();

			if (!ArmAtStop(n_paused.frames))
				return false;
			CO_YIELD(n_paused.co);

			c = ReadCounts();

			if (stop.cause == StopCause::Capture && ServerFrames() == n_paused.frames
				&& c.n[2] - n_paused.c.n[2] == n_paused.frames + (n_paused.where == AT_DSP ? 1 : 0))
				n_paused.at[n_paused.where]++;
			else
				n_paused.bad++;
		}
	}

	ArmBps({ });
	DspBps({ });
	NoEventBp();

	Check(PausedDone() && n_paused.bad == 0 && n_paused.other == 0,
		"captures armed at stops, each resumed: %u behind a burst, %u in front of an ARM7 instruction, %u in front of a DSP step, %u at the end of a sample; "
		"each stopped behind its last frame with its frames and its time steps (%u did not; %u stops of another kind)", n_paused.at[AT_NONE], n_paused.at[AT_ARM7],
		n_paused.at[AT_DSP], n_paused.at[AT_SAMPLE], n_paused.bad, n_paused.other);

	NEUTRAL_END(n_paused.co);

	CO_END(n_paused.co);
}
