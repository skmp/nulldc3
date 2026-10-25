/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

#include <chrono>
#include <thread>

/*
	The naps of the selftests (dbg_selftest_*.cpp): a thread of a test that waits a little, for another thread or
	for the guest, waits with

	    void dbg::selftest::NapUs(s64 us)       about us microseconds, never less; nothing for 0 or less

	On Linux and the other POSIX hosts it is std::this_thread::sleep_for(std::chrono::microseconds(us)), the call
	the selftests made before it, and nothing else: a nap there lasts what it did.

	On macOS std::this_thread::sleep_for (nanosleep), and every wait of the kernel that takes the timer's default
	leeway (a kevent timer, mach_wait_until), lasts about half as long again as it is asked for, whatever the
	thread's QoS: the kernel coalesces its timers, measured on an Apple M2 under macOS 26 (a nap of 1 ms lasts 1.5 ms,
	one of 5 ms 7.5 ms). NapUs (dbg_selftest_nap.cpp) waits there on a timer of a kqueue of its thread
	(EVFILT_TIMER with NOTE_CRITICAL, which the kernel does not coalesce), again for what is left until the nap's
	end where it came back early, without a spin; where the thread has no kqueue it is std::this_thread::sleep_for.

	On Windows std::this_thread::sleep_for lasts until a tick of the system timer, 15.6 ms (1 to 2 ms where a
	process has asked for a 1 ms timer with timeBeginPeriod), whatever it is asked for, and a test that naps for
	fractions of a millisecond thousands of times outlasts its bounds there. NapUs (dbg_selftest_nap.cpp) asks
	for no resolution of the system timer, which would hold for the whole process and the system: it waits on a
	high-resolution waitable timer of its thread (CreateWaitableTimerExW with
	CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, Windows 10 1803 and later) for all of the nap but its last 500 us, and
	for the rest it spins on the performance counter, giving the processor to any other thread that is ready on it
	(SwitchToThread). That timer comes back on a grid of half a millisecond: by itself it would make a nap of
	100 us one of about 500 us and one of 1 ms one of 1 to 1.5 ms. A nap of 500 us or less is spun whole, and no
	nap of a millisecond or more is: its spin is the last 500 us of it at most. Where Windows refuses the flag, a
	nap of less than a millisecond is spun whole, and a longer one is std::this_thread::sleep_for's, which the
	tick rounds up, without a spin.

	The selftest "nap" (dbg_selftest_nap.cpp, debugger.selftest.nap) holds it on every host: naps of 100 us to
	50 ms are never shorter than asked for and do not spin; how much longer they last is reported, not checked.
*/

namespace dbg { namespace selftest {

#if HOST_OS == OS_WINDOWS || HOST_OS == OS_DARWIN
void NapUs(s64 us);
#else
inline void NapUs(s64 us)
{
	std::this_thread::sleep_for(std::chrono::microseconds(us));
}
#endif

} }
