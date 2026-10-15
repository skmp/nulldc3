/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::selftest: tests that run inside the emulator, on the emulator thread, against the live machine
	(driver: dbg_selftest.cpp; tests: one dbg_selftest_<package>.cpp per package).

	    devcast -config Debugger:SelfTest=<filter> <game>

	runs the tests the filter selects and ends the process with exit code 0 when every one of them passed and
	1 otherwise, also when the filter selects nothing. The filter is a comma-separated list; an item selects
	the test of that name and every test whose name starts with the item and a dot ("sh4bp" selects
	"sh4bp.b1"); "all" selects every test.

	Output, on stdout, each line flushed as it is written:

	    SelfTest: engine: <text>                               first line: the SH4's engine and its debug engine
	    SelfTest: <test>: ok: <text>                           a check that held
	    SelfTest: <test>: FAILED: <text>                       a check that did not
	    SelfTest: <test>: note: <text>
	    SelfTest: <test>: PASS, <n> checks                     once per test
	    SelfTest: <test>: FAIL, <f> of <n> checks failed       a test without any check fails as well
	    SelfTest: no test matches "<filter>"
	    SelfTest: <p> passed, <f> failed                       last line; the process ends behind it

	Two kinds of test, both on (E):

	  machine test   runs once, to completion, before the guest has executed anything: the machine exists, the
	                 SH4 backend is selected, the lifetime is Running. It reads guest state and the constant
	                 tables directly. It does not run the guest.
	  guest test     drives the guest from debugger stops, in place of a client. The park loop hands it every
	                 stop; in that call it stands where a call of every class may stand: it reads and writes
	                 guest state and the tables directly and uses dbg::emu (SetSh4Breakpoints,
	                 SetArm7Breakpoints, SetEventBreakpoints, SetActiveMask, Resume, Step, Reset, ...) to arm
	                 what happens next.
	                 No client is connected, so a test that sets an event breakpoint also opens the gate of
	                 its event, and closes it again (g_event_on, debugger.h).
	                 No client is connected, so the test also sets the active mask (emu::SetActiveMask); it
	                 starts as 0.

	The machine tests run first, then the guest tests one after the other, each kind in the order of the
	names. A test starts with the machine as the test before it left it.

	Tests are on (E): they never call dbg::emu::Call, and Check / Note are called from inside a test only.

	What the emulator thread does for a session (dbg_emu.cpp):
	  - the first Park() of every emulator thread calls AtStart() before it decides whether to park;
	  - that Park() makes the start park (reason start) when Active() is true, which is at every thread start
	    during a session, or when it is the first Park() after OnMachineCreated() and Debugger:StartPaused is set.
	    It is not made for the threads that the selftest of dbg::emu starts itself;
	  - the park loop calls OnStop() for every stop it publishes, after the listener, while Active() is true,
	    and then executes the action the test armed, or waits as it does for a client.
*/

namespace dbg { namespace selftest {

typedef void (*MachineFn)();

// stop: the state the park loop just published; the lifetime is Parked. The first call of a test gets the
// stop that is current when the test begins. A test that ends the emulator thread (host stop, guest exit) is
// called again with the start stop of the next thread.
// Returns false to be called again at the next stop: the test armed an action (emu::Resume, emu::Step), or
// waits for a stop that another thread causes. Returns true when the test is finished; it then has armed
// nothing in this call, so that the next test begins parked on the same stop.
// The driver starts no thread. A test that ends the emulator thread arranges the restart itself: a thread
// it created waits for the lifetime Stopped (emu::GetRunState) and calls virtualDreamcast->Resume().
// A test cannot arm a pause from a stop (emu::RequestPause does nothing while Parked): to stop a running
// guest it sets an event breakpoint, or calls RequestPause() from a scheduler callback it registered or from
// another thread. A service excursion while running is asked for by another thread through emu::Call; only
// the test function itself, which runs on (E), must not call it.
typedef bool (*GuestFn)(const emu::RunState& stop);

// One registered test. Objects of it are defined by the two macros below, at namespace scope: the
// constructor enters the test in the registry during static initialisation. name and the functions must
// stay valid for the whole process. Names are unique; two tests of one name fail the session.
struct Test {
	Test(const char* name, MachineFn machine, GuestFn guest);

	const char* name;
	MachineFn machine;		// exactly one of the two is set
	GuestFn guest;
	Test* next;
};

// DBG_SELFTEST(regs_slots, "regs.slots") { ... Check(...); }
#define DBG_SELFTEST(ident, name) \
	static void ident(); \
	static dbg::selftest::Test ident##_selftest(name, ident, nullptr); \
	static void ident()

// DBG_SELFTEST_GUEST(sh4bp_b1, "sh4bp.b1") { ... return done; }   with the parameter `stop`
#define DBG_SELFTEST_GUEST(ident, name) \
	static bool ident(const dbg::emu::RunState& stop); \
	static dbg::selftest::Test ident##_selftest(name, nullptr, ident); \
	static bool ident(const dbg::emu::RunState& stop)

// A check of the running test: counts it and prints its line. printf-style; the text is cut to 1000
// bytes. Returns ok.
bool Check(bool ok, const char* fmt, ...) DBG_PRINTF_LIKE(2, 3);

// A line of the running test that is not a check.
void Note(const char* fmt, ...) DBG_PRINTF_LIKE(1, 2);

// Does the filter select a test of this name? Any thread.
bool Matches(const char* filter, const char* name);

// Is Debugger:SelfTest set? Any thread, from dbg::Init on; the key is read once.
bool Requested();

// (E). Starts the session if one is requested and none was started in this process; otherwise it does
// nothing. Runs every selected machine test. When no guest test is selected the session ends here: the
// last line is printed and the process ends, AtStart() does not return. Called by the first Park() of every
// emulator thread, before that Park() decides whether to park, with emu_started set and no lock held.
void AtStart();

// (E). True from AtStart() on while guest tests remain.
bool Active();

// (E), park loop only, lifetime Parked, while Active(): hands the stop to the current guest test, and to
// the ones behind it as tests finish. When the last one finished it prints the last line and ends the
// process; it does not return then.
void OnStop(const emu::RunState& stop);

} }
