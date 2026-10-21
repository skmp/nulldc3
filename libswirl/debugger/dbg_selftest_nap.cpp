/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The naps of the selftests (dbg_selftest_nap.h): NapUs on Windows, and the selftest "nap", which holds NapUs on
	every host.

	    nulldc3 --config config:Headless=yes , audio:backend=none , Debugger:SelfTest=nap <any program>

	nap is a machine test: it runs before the guest has executed anything and does not touch the machine. It
	measures naps on the emulator thread, against the steady clock, and the CPU time of the thread while they last
	(CLOCK_THREAD_CPUTIME_ID; on Windows the thread's cycles, QueryThreadCycleTime, at a rate taken against the
	performance counter, as dbg_selftest_hostcycles.cpp does):
	  - a nap of 50 ms, three times: never less, at most 25 ms more, and at most 5 ms of the thread's CPU time: the
	    thread waits, it does not spin for the nap;
	  - 40 naps each of 100, 300, 700, 1000 and 1500 us: none shorter than asked for, the median of each request
	    at most 400 us longer than asked for (std::this_thread::sleep_for of Windows: about 15 ms longer), and naps
	    of a millisecond or more take at most two thirds of their time on the processor (none is spun whole);
	  - on a POSIX host, where NapUs is std::this_thread::sleep_for: 40 naps each of 100, 300, 700, 1000 and
	    1500 us through NapUs and as many through std::this_thread::sleep_for, one after the other, the medians of
	    each request within 50 us of each other (a nap there lasts what it lasted before NapUs);
	  - on Windows the same for the way without the high-resolution timer, where Windows refuses its flag: the
	    nap of 50 ms without a spin, and naps of 300 us to within 400 us.
*/

#include "dbg_selftest.h"
#include "dbg_selftest_nap.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#if HOST_OS == OS_LINUX || HOST_OS == OS_DARWIN
#include <time.h>
#elif HOST_OS == OS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

#if HOST_OS == OS_WINDOWS
namespace {

// The part of a nap that is spun: the high-resolution timer comes back on a grid of half a millisecond, up to
// 500 us behind the time it is set for
const s64 kSpunUs = 500;

s64 Counter()
{
	LARGE_INTEGER t;
	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

s64 CounterPerSecond()
{
	static const s64 frequency = [] {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);
		return (s64)f.QuadPart;
	}();

	return frequency;
}

// The high-resolution waitable timer of a thread: made at its first nap and closed when the thread ends; none where
// Windows refuses the flag
struct ThreadTimer {
	HANDLE handle = nullptr;
	bool made = false;

	~ThreadTimer()
	{
		if (handle)
			CloseHandle(handle);
	}

	HANDLE Get()
	{
		if (!made)
		{
			made = true;
			handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		}

		return handle;
	}
};

thread_local ThreadTimer threadTimer;

// A nap of us microseconds: with the high-resolution timer of the thread when highResolution is set and Windows has
// one, else without it (dbg_selftest_nap.h)
void Nap(s64 us, bool highResolution)
{
	if (us <= 0)
		return;

	const s64 deadline = Counter() + (us * CounterPerSecond() + 999999) / 1000000;
	HANDLE timer = highResolution ? threadTimer.Get() : nullptr;

	if (timer)
	{
		if (us > kSpunUs)
		{
			LARGE_INTEGER due;
			due.QuadPart = -(us - kSpunUs) * 10;	// relative, in units of 100 ns

			if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
				WaitForSingleObject(timer, INFINITE);
			else
				std::this_thread::sleep_for(std::chrono::microseconds(us));
		}
	}
	else if (us >= 1000)
	{
		// the tick rounds it up; never less than asked for
		std::this_thread::sleep_for(std::chrono::microseconds(us));
	}

	while (Counter() < deadline)
		SwitchToThread();
}

}

namespace dbg { namespace selftest {

void NapUs(s64 us)
{
	Nap(us, true);
}

} }
#endif

namespace {

using namespace dbg;
using selftest::Check;
using selftest::Note;

typedef std::chrono::steady_clock Clock;

#if HOST_OS == OS_WINDOWS
// The rate of the thread's cycles in cycles per microsecond: the largest of five tries of 20 ms on a thread that
// spins, since a try in which the thread lost the processor counts fewer cycles, never more
// (dbg_selftest_hostcycles.cpp has the same)
double ThreadCyclesPerMicro()
{
	static double rate = 0;

	if (rate == 0)
	{
		for (int i = 0; i < 5; i++)
		{
			ULONG64 c0, c1;
			QueryThreadCycleTime(GetCurrentThread(), &c0);
			const s64 t0 = Counter();
			s64 t1;
			do
				t1 = Counter();
			while ((t1 - t0) * 50 < CounterPerSecond());
			QueryThreadCycleTime(GetCurrentThread(), &c1);

			const double us = (double)(t1 - t0) * 1e6 / (double)CounterPerSecond();
			rate = std::max(rate, (double)(c1 - c0) / us);
		}
	}

	return rate;
}
#endif

// The CPU time of the calling thread in microseconds; the wall clock on a host without such a clock
double ThreadMicros()
{
#if HOST_OS == OS_LINUX || HOST_OS == OS_DARWIN
	timespec ts;

	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
		return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
#elif HOST_OS == OS_WINDOWS
	const double rate = ThreadCyclesPerMicro();
	ULONG64 cycles;

	if (QueryThreadCycleTime(GetCurrentThread(), &cycles))
		return (double)cycles / rate;
#endif

	return std::chrono::duration<double, std::micro>(Clock::now().time_since_epoch()).count();
}

// n naps of us microseconds: the wall time of each, and the CPU time and the wall time of all of them
struct Naps {
	std::vector<double> wall;
	double cpu = 0;
	double total = 0;

	double Min() const { return *std::min_element(wall.begin(), wall.end()); }
	double Max() const { return *std::max_element(wall.begin(), wall.end()); }
	double Median() const
	{
		std::vector<double> sorted = wall;
		std::sort(sorted.begin(), sorted.end());
		return sorted[sorted.size() / 2];
	}
};

template <typename NapFn>
Naps Measure(NapFn nap, s64 us, int n)
{
	Naps naps;
	ThreadMicros();		// on Windows the first call takes the rate, by spinning

	const double cpu0 = ThreadMicros();
	const Clock::time_point all0 = Clock::now();

	for (int i = 0; i < n; i++)
	{
		const Clock::time_point t0 = Clock::now();
		nap(us);
		naps.wall.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t0).count());
	}

	naps.total = std::chrono::duration<double, std::micro>(Clock::now() - all0).count();
	naps.cpu = ThreadMicros() - cpu0;
	return naps;
}

// A nap of 50 ms, three times: never shorter, at most 25 ms longer, and no spin
template <typename NapFn>
void Long(NapFn nap, const char* way)
{
	const s64 us = 50000;
	double cpuMax = 0;
	Naps all;

	for (int i = 0; i < 3; i++)
	{
		Naps one = Measure(nap, us, 1);
		cpuMax = std::max(cpuMax, one.cpu);
		all.wall.push_back(one.wall[0]);
	}

	Check(all.Min() >= us && all.Max() <= us + 25000, "%s: three naps of 50 ms lasted %.0f to %.0f us: never less, at most 25 ms more",
		way, all.Min(), all.Max());
	Check(cpuMax <= 5000, "%s: a nap of 50 ms took at most %.0f us of the thread's CPU time (5000 at most): the thread waits, it does not spin",
		way, cpuMax);
}

// n naps of each request: none shorter, the median at most 400 us longer; those of a millisecond or more not spun whole
template <typename NapFn>
void Short(NapFn nap, const char* way, const std::vector<s64>& requests, int n)
{
	for (s64 us : requests)
	{
		Naps naps = Measure(nap, us, n);

		Check(naps.Min() >= (double)us && naps.Median() - (double)us <= 400,
			"%s: %d naps of %d us lasted %.0f to %.0f us, the median %.0f: none shorter, the median at most 400 us longer",
			way, n, (int)us, naps.Min(), naps.Max(), naps.Median());

		if (us >= 1000)
			Check(naps.cpu <= naps.total * 2 / 3, "%s: %d naps of %d us took %.0f us of the thread's CPU time in %.0f us: two thirds at most, none spun whole",
				way, n, (int)us, naps.cpu, naps.total);
	}
}

#if HOST_OS != OS_WINDOWS
// A POSIX host: NapUs is std::this_thread::sleep_for (dbg_selftest_nap.h), and a nap through it lasts what a
// std::this_thread::sleep_for of the same request lasts. n naps of each request through each, one after the other,
// so that both see the same host: the medians of the two within 50 us of each other (they agree to a few us). The
// bounds of Short let a nap through that lasts some 300 us more than asked for; this does not
void AsSleepFor(const std::vector<s64>& requests, int n)
{
	for (s64 us : requests)
	{
		Naps naps, sleeps;

		for (int i = 0; i < n; i++)
		{
			const Clock::time_point t0 = Clock::now();
			selftest::NapUs(us);
			const Clock::time_point t1 = Clock::now();
			std::this_thread::sleep_for(std::chrono::microseconds(us));
			const Clock::time_point t2 = Clock::now();

			naps.wall.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
			sleeps.wall.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
		}

		const double apart = naps.Median() - sleeps.Median();

		Check(apart <= 50 && apart >= -50,
			"NapUs against std::this_thread::sleep_for: %d naps of %d us through each, one after the other: the medians %.0f and %.0f us, within 50 us of each other",
			n, (int)us, naps.Median(), sleeps.Median());
	}
}
#endif

DBG_SELFTEST(nap_selftest, "nap")
{
	Long([](s64 us) { selftest::NapUs(us); }, "NapUs");
	Short([](s64 us) { selftest::NapUs(us); }, "NapUs", { 100, 300, 700, 1000, 1500 }, 40);

#if HOST_OS != OS_WINDOWS
	AsSleepFor({ 100, 300, 700, 1000, 1500 }, 40);
#endif

#if HOST_OS == OS_WINDOWS
	Note("the high-resolution waitable timer of this thread: %s", threadTimer.Get() ? "made" : "refused by Windows: every nap without it");

	Long([](s64 us) { Nap(us, false); }, "without the high-resolution timer");
	Short([](s64 us) { Nap(us, false); }, "without the high-resolution timer", { 300 }, 20);
#endif
}

}
