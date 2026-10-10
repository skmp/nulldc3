/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest hostcycles: the host stops the running machine and starts it again, hundreds of times in one
	process, while a program runs to the end that an Exit.* key gives the run.

	    devcast -config config:Headless=yes , audio:backend=none , config:Exit.AfterVblanks=1200 , config:Exit.StateHash=yes ,
	            Debugger:SelfTestHostCycleFrom=300 , Debugger:SelfTest=hostcycles 2ndmix.elf

	Every start of the machine is a new emulator thread, and every emulator thread makes its backends anew. For
	the recompiler that is a block cache without a block and a code cache without code: a thread that kept the
	code of the threads before it below its own left every thread less of the cache, until the guest crawled
	from one cache clear to the next.

	What a cycle is: a thread that the debugger did not stop is asked to stop, as the host asks, when it has run
	for a span of emulated time; another thread, which plays the host, waits for it to be gone and starts the
	machine again. The new thread parks at its start, as every thread of a session does: there the test looks at
	what the thread starts with, and resumes it. The stop is asked for on the emulator thread, between two
	slices, from a call that the other thread makes all the time: it is known there how far the run is, so the
	stop of the test never meets the stop that ends the run. How long a thread runs beyond its span is how long
	the other thread takes from one call to the next, which is some hundred thousand emulated cycles of a guest
	that runs at full speed: the run has to leave the cycles room, and when it does not, the test says how
	many it made.

	The checks are made once, at the start of the thread behind the last cycle:
	  - every thread ended, with its stop callback, and the next one started;
	  - (recompiler) every thread started with the stubs of the code generator at the start of the code cache
	    and nothing behind them, with no block in the tables of the block manager, and with no entry in the
	    lookup table for a block of the thread before it;
	  - (recompiler) the code cache never filled, and the threads together compiled more than it holds: kept
	    from thread to thread, their code would have filled it;
	  - the guest did not get slower with the number of threads: the emulated time of a thread over the CPU time
	    it took from the resume of its start park to its end, in the last fifth of the cycles against the first.
	    That compares like with like where every thread of the test runs the same part of the program: the
	    cycles begin at the vblank of Debugger:SelfTestHostCycleFrom, for a program that is in its main loop
	    by then (2ndmix by its vblank 300; a thread compiles about 160 KiB there).

	The test never finishes when a key ends the run: the run ends with its exit line, whose state hash
	tests/debugger/selftest/selftest.py compares with that of a run nobody touched. A stop of the host and the
	start behind it leave no trace in the guest, on the recompiler as on the interpreter.

	Debugger:SelfTestHostCycles is the number of cycles (600), Debugger:SelfTestHostCycleSpan the emulated
	time of a thread in SH4 cycles (1000000, less than a third of a vblank), Debugger:SelfTestHostCycleFrom the
	vblank in front of which no thread is stopped (0). The last 8 vblanks of the run have no stop either.
*/

#include "dbg_selftest.h"

#include "libswirl.h"
#include "cfg/cfg.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_sched.h"

#if FEAT_SHREC != DYNAREC_NONE
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/dyna/ngen.h"
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if HOST_OS == OS_LINUX || HOST_OS == OS_DARWIN
#include <time.h>
#endif

// hw/pvr/spg.cpp: the vblanks of the machine
extern u32 vblank_count_monotonic;

#if FEAT_SHREC != DYNAREC_NONE
// hw/sh4/dyna/driver.cpp: where the code cache has got to, and the base of what a cache clear recycles
extern u32 LastAddr;
extern u32 LastAddr_min;

// hw/sh4/dyna/blockmanager.cpp: the blocks of the cache, the discarded ones that are not deleted yet, and the
// blocks by the address of their code
extern std::set<RuntimeBlockInfo*> all_blocks;
extern std::set<RuntimeBlockInfo*> del_blocks;
extern std::map<void*, RuntimeBlockInfo*> blkmap;
#endif

namespace {

using namespace dbg;
using selftest::Check;
using selftest::Note;

// No stop is asked for in the last vblanks of the run: the thread that is asked ends within a slice, or
// within the SLEEP wait it is in, which is a fraction of a vblank
const u32 kQuietVblanks = 8;

// The threads of the last fifth of the cycles may be this much slower than those of the first, by the median.
// They do the same work, and their speed is taken by the CPU time of the thread: a guest that goes from one
// cache clear to the next is slower by far more
const double kSlowest = 2.0;

// One emulator thread, from the resume of its start park to its stop callback
struct ThreadRun {
	double us;			// CPU time of the thread
	u64 cycles;			// emulated time
	u32 code;			// host code it compiled, in bytes
};

struct {
	// Emulator thread only
	bool started = false;
	bool summed = false;
	bool jit = false;
	bool endless = false;			// no key ends the run: the test finishes behind its checks
	u32 total = 0;					// cycles to make
	u64 span = 0;					// emulated time a thread runs before it is asked to stop
	u32 firstVblank = 0;			// no stop is asked for in front of this vblank
	u32 lastVblank = 0;				// and none from this one on

	bool asked = true;				// this thread has been asked to stop, or has not been resumed yet
	u64 startTime = 0;				// scheduler time at the resume of the start park of this thread
	double startUs = 0;				// its CPU time there

	std::vector<ThreadRun> runs;
	u32 offThread = 0;				// stop callbacks that did not run on the emulator thread

	// what the threads started with (recompiler)
	u32 starts = 0;
	u32 base = 0;					// the base of the code cache of the first thread: the size of the stubs
	const void* mainloop = nullptr;	// where the first thread had the main loop
	u32 notEmpty = 0, baseMoved = 0, tablesLeft = 0;
	u32 lookups = 0, staleLookups = 0;
	std::vector<u32> lastBlocks;	// the guest addresses of the blocks of the thread before

	u32 maxCode = 0;
	u32 minFree = ~0u;
	u64 allCode = 0;

	// With the other thread
	std::atomic<u32> stopped { 0 };		// stop callbacks that ran
	std::atomic<bool> done { false };	// every cycle is made
	std::atomic<bool> over { false };	// the end of the run is near: no more stops
	std::atomic<bool> busy { false };	// the emulator thread did not take a call
} hc;

// The CPU time of the calling thread in microseconds: what the thread did, whatever else the machine does.
// The wall clock on a host without such a clock
double ThreadMicros()
{
#if HOST_OS == OS_LINUX || HOST_OS == OS_DARWIN
	timespec ts;

	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
		return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
#endif

	return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The stop callback of a cycle: on the emulator thread, at its end
void ThreadEnded()
{
	ThreadRun run;

	run.us = ThreadMicros() - hc.startUs;
	run.cycles = sh4_sched_now64() - hc.startTime;
	run.code = 0;

	if (!emu::OnEmuThread())
		hc.offThread++;

#if FEAT_SHREC != DYNAREC_NONE
	if (hc.jit)
	{
		run.code = LastAddr - LastAddr_min;

		hc.maxCode = (std::max)(hc.maxCode, run.code);
		hc.minFree = (std::min)(hc.minFree, emit_FreeSpace());
		hc.allCode += run.code;

		hc.lastBlocks.clear();
		for (RuntimeBlockInfo* blk : all_blocks)
			hc.lastBlocks.push_back(blk->addr);
	}
#endif

	hc.runs.push_back(run);
	hc.stopped++;
}

// A thread at its start park: what it starts with
void ThreadStarted()
{
#if FEAT_SHREC != DYNAREC_NONE
	if (!hc.jit)
		return;

	if (hc.starts++ == 0)
	{
		hc.base = LastAddr_min;
		hc.mainloop = (const void*)rdv_ngen->Mainloop;
	}

	// the stubs of the code generator, and nothing behind them
	if (LastAddr != LastAddr_min)
		hc.notEmpty++;

	// where the first thread had them
	if (LastAddr_min != hc.base || (const void*)rdv_ngen->Mainloop != hc.mainloop)
		hc.baseMoved++;

	if (!all_blocks.empty() || !del_blocks.empty() || !blkmap.empty())
		hc.tablesLeft++;

	// the dispatcher finds no code for a block of the thread before
	for (u32 addr : hc.lastBlocks)
	{
		hc.lookups++;

		if ((void*)bm_GetCode(addr) != (void*)rdv_ngen->FailedToFindBlock)
			hc.staleLookups++;
	}
#endif
}

// A call of the other thread. On the emulator thread it runs between two slices of the guest, or in the park
// loop: when the thread has run its span, the machine is stopped from here as the host stops it
void Poll()
{
	if (!emu::OnEmuThread() || hc.asked || hc.done || hc.over)
		return;

	if (emu::GetRunState().lifetime != Lifetime::Running)
		return;

	if (vblank_count_monotonic < hc.firstVblank)
		return;

	if (vblank_count_monotonic >= hc.lastVblank)
	{
		// The run had no room for the cycles: the test is handed a stop to say so
		hc.over = true;
		emu::RequestPause();
		return;
	}

	if (sh4_sched_now64() - hc.startTime < hc.span)
		return;

	hc.asked = true;

	virtualDreamcast->Stop([] { ThreadEnded(); });
}

// The other thread plays the host: it starts the machine whenever the test has stopped it
void Host()
{
	u32 started = 0;

	while (!hc.done && !hc.over)
	{
		Lifetime lifetime = emu::GetRunState().lifetime;

		if (lifetime == Lifetime::NoMachine)
			return;

		if (lifetime == Lifetime::Stopped)
		{
			// The stop callback of a cycle has run by now. Without one the run has ended
			if (hc.stopped.load() == started)
				return;

			started++;
			virtualDreamcast->Resume();
			continue;
		}

		// One call behind the other: the emulator thread takes one at every slice end, so a thread is asked
		// to stop within a slice of the end of its span, however fast the guest runs
		if (lifetime == Lifetime::Running && emu::Call(emu::CC_READ, [] { Poll(); }) == kErrBusy)
		{
			hc.busy = true;
			return;
		}

		std::this_thread::yield();
	}
}

double Median(std::vector<double> values)
{
	if (values.empty())
		return 0;

	std::sort(values.begin(), values.end());

	return values[values.size() / 2];
}

// Emulated cycles per microsecond of CPU time, the median of the threads [from, to)
double Speed(size_t from, size_t to)
{
	std::vector<double> rates;

	for (size_t i = from; i < to && i < hc.runs.size(); i++)
		rates.push_back(hc.runs[i].us > 0 ? (double)hc.runs[i].cycles / hc.runs[i].us : 0);

	return Median(rates);
}

void Summary()
{
	const u32 n = (u32)hc.runs.size();

	Check(n == hc.total && hc.stopped.load() == n && hc.offThread == 0 && !hc.busy,
		"%u times the host stopped the running machine and started it again: every thread ended with its stop callback, on the emulator thread, and the next one started (%u cycles were to be made by vblank %u, this is vblank %u)",
		n, hc.total, hc.lastVblank, vblank_count_monotonic);

#if FEAT_SHREC != DYNAREC_NONE
	if (hc.jit)
	{
		Check(hc.notEmpty == 0 && hc.baseMoved == 0,
			"every one of the %u threads started with an empty code cache: the stubs of the code generator where the first thread had them, %u bytes, and nothing behind them (%u threads found code there, %u found the stubs elsewhere)",
			hc.starts, hc.base, hc.notEmpty, hc.baseMoved);

		Check(hc.tablesLeft == 0 && hc.staleLookups == 0 && hc.lookups != 0,
			"nothing of a thread's blocks was left for the next: no block in the tables, and no entry in the lookup table for any block of the thread before (%u lookups, %u found code; %u threads found a block in a table)",
			hc.lookups, hc.staleLookups, hc.tablesLeft);

		Check(hc.minFree >= CODE_SIZE / 2,
			"the code cache never filled: a thread compiled %u bytes at most, and %u of the %u bytes of the cache were free at least",
			hc.maxCode, hc.minFree, (u32)CODE_SIZE);

		Check(hc.allCode > CODE_SIZE,
			"the threads compiled %llu bytes in all, %.1f times what the code cache holds: kept from thread to thread, it would have filled",
			(unsigned long long)hc.allCode, (double)hc.allCode / CODE_SIZE);
	}
	else
	{
		Note("the SH4 runs on the interpreter: no code cache to look at");
	}
#else
	Note("built without a recompiler: no code cache to look at");
#endif

	const size_t fifth = n / 5;
	const double first = Speed(0, fifth);
	const double last = Speed(n - fifth, n);

	Check(fifth != 0 && first > 0 && last * kSlowest >= first,
		"the guest did not get slower with the number of threads: the median thread of the last fifth ran %.0f emulated cycles per microsecond of its CPU time, of the first fifth %.0f",
		last, first);

	// The median thread of each tenth of the cycles: its speed, and how much code it compiled
	std::string speeds, codes;
	std::vector<double> us, code;
	u64 cycles = 0;

	for (u32 i = 0; i < 10; i++)
	{
		const size_t from = (size_t)n * i / 10, to = (size_t)n * (i + 1) / 10;
		std::vector<double> tenth;
		char text[32];

		for (size_t k = from; k < to; k++)
			tenth.push_back(hc.runs[k].code);

		snprintf(text, sizeof(text), "%s%.0f", i ? " " : "", Speed(from, to));
		speeds += text;

		snprintf(text, sizeof(text), "%s%.0f", i ? " " : "", Median(tenth));
		codes += text;
	}

	for (const ThreadRun& run : hc.runs)
	{
		us.push_back(run.us);
		code.push_back(run.code);
		cycles += run.cycles;
	}

	Note("emulated cycles per microsecond, the median thread of each tenth of the cycles: %s", speeds.c_str());

	if (hc.jit)
		Note("bytes of code compiled, the median thread of each tenth of the cycles: %s", codes.c_str());

	Note("a thread, by the median: %.0f us of CPU time from its resume to its end, %.0f bytes of code compiled; all of them ran %llu emulated cycles, from vblank %u to vblank %u",
		Median(us), Median(code), (unsigned long long)cycles, hc.firstVblank, vblank_count_monotonic);
}

}

DBG_SELFTEST_GUEST(hostcycles, "hostcycles")
{
	// The end of the run is near and cycles are missing: the stop that Poll() asked for
	if (stop.cause == StopCause::Pause && hc.over && !hc.summed)
	{
		hc.summed = true;

		Summary();

		emu::Resume();
		return false;
	}

	if (stop.cause != StopCause::Start || stop.lifetime != Lifetime::Parked)
	{
		Check(false, "a stop that is no start of a thread (\"%s\")", StopCauseName(stop.cause));
		emu::Resume();
		return false;
	}

	if (!hc.started)
	{
		hc.started = true;

		const s32 exit_vblanks = cfgLoadInt("config", "Exit.AfterVblanks", 0);

		hc.total = (u32)cfgLoadInt("Debugger", "SelfTestHostCycles", 600);
		hc.span = (u64)cfgLoadInt("Debugger", "SelfTestHostCycleSpan", 1000000);
		hc.firstVblank = (u32)cfgLoadInt("Debugger", "SelfTestHostCycleFrom", 0);
		hc.endless = exit_vblanks <= 0;
		hc.lastVblank = hc.endless ? ~0u : (exit_vblanks > (s32)kQuietVblanks ? (u32)exit_vblanks - kQuietVblanks : 0);

#if FEAT_SHREC != DYNAREC_NONE
		emu::Sh4EngineInfo engine;

		hc.jit = emu::GetSh4Engine(&engine) && strcmp(engine.backend, "interpreter") != 0;
#endif

		Check(hc.total != 0 && hc.span != 0 && hc.firstVblank < hc.lastVblank,
			"the run starts parked: from vblank %u on the host will stop the running machine and start it again %u times, each thread after %llu emulated cycles, on the %s",
			hc.firstVblank, hc.total, (unsigned long long)hc.span, hc.jit ? "recompiler" : "interpreter");

		ThreadStarted();

		std::thread(Host).detach();
	}
	else
	{
		ThreadStarted();
	}

	if (!hc.summed && hc.runs.size() >= hc.total)
	{
		hc.summed = true;
		hc.done = true;

		Summary();

		// nothing ends the run: the session ends here
		if (hc.endless)
			return true;
	}

	hc.startTime = sh4_sched_now64();
	hc.startUs = ThreadMicros();
	hc.asked = false;

	int rv = emu::Resume();

	if (rv != 0)
		Check(false, "a resume was refused: %d", rv);

	return false;
}
