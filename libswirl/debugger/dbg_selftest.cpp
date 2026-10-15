/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_selftest.h"

#include "cfg/cfg.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace dbg { namespace selftest {

// Filled by the Test constructors during static initialisation, read by the session afterwards.
static Test* registry;

Test::Test(const char* name, MachineFn machine, GuestFn guest)
	: name(name), machine(machine), guest(guest), next(registry)
{
	registry = this;
}

// Outside a session the driver costs one read of the configuration, at the first Requested(), and the tests
// that AtStart() and Active() begin with. Nothing in this file is destroyed at process exit.
static struct {
	bool started;
	std::vector<Test*>* guests;	// the selected guest tests, in the order they run; made by AtStart
	size_t current;				// the one that runs, or is next
	Test* running;				// the test that Check and Note belong to
	u32 checks;					// of the running test
	u32 failed;
	u32 testsPassed;
	u32 testsFailed;
} session;

// The string is never destroyed: the emulator thread may ask while the process exits.
static const std::string& Filter()
{
	static const std::string& filter = *new std::string(cfgLoadStr("Debugger", "SelfTest", ""));
	return filter;
}

static void Line(const char* kind, const char* fmt, va_list args)
{
	char text[1000];
	vsnprintf(text, sizeof(text), fmt, args);

	printf("SelfTest: %s: %s: %s\n", session.running->name, kind, text);
	fflush(stdout);
}

static void Begin(Test* test)
{
	session.running = test;
	session.checks = 0;
	session.failed = 0;
}

static void End()
{
	if (session.checks != 0 && session.failed == 0)
	{
		printf("SelfTest: %s: PASS, %u checks\n", session.running->name, session.checks);
		session.testsPassed++;
	}
	else
	{
		printf("SelfTest: %s: FAIL, %u of %u checks failed\n", session.running->name, session.failed, session.checks);
		session.testsFailed++;
	}
	fflush(stdout);

	session.running = nullptr;
}

// The process ends here, without the emulator's shutdown: the test may have left the machine in any state,
// and other threads are still running.
[[noreturn]] static void Finish()
{
	printf("SelfTest: %u passed, %u failed\n", session.testsPassed, session.testsFailed);
	fflush(stdout);
	fflush(stderr);

	std::_Exit(session.testsPassed != 0 && session.testsFailed == 0 ? 0 : 1);
}

bool Check(bool ok, const char* fmt, ...)
{
	verify(session.running != nullptr);

	session.checks++;
	if (!ok)
		session.failed++;

	va_list args;
	va_start(args, fmt);
	Line(ok ? "ok" : "FAILED", fmt, args);
	va_end(args);

	return ok;
}

void Note(const char* fmt, ...)
{
	verify(session.running != nullptr);

	va_list args;
	va_start(args, fmt);
	Line("note", fmt, args);
	va_end(args);
}

bool Matches(const char* filter, const char* name)
{
	size_t name_len = strlen(name);

	for (const char* item = filter; *item; )
	{
		const char* end = strchr(item, ',');
		if (end == nullptr)
			end = item + strlen(item);

		const char* first = item;
		const char* last = end;
		while (first < last && (*first == ' ' || *first == '\t'))
			first++;
		while (last > first && (last[-1] == ' ' || last[-1] == '\t'))
			last--;

		size_t len = last - first;

		if (len == 3 && memcmp(first, "all", 3) == 0)
			return true;
		if (len == name_len && memcmp(first, name, len) == 0)
			return true;
		if (len != 0 && len < name_len && name[len] == '.' && memcmp(first, name, len) == 0)
			return true;

		item = *end ? end + 1 : end;
	}

	return false;
}

bool Requested()
{
	return !Filter().empty();
}

static void SelfCheck();

// The engine of the session, before its tests: what the SH4 runs on, and what executes its breakpoints and steps,
// by the name debugger.describe gives it among its capabilities. The session runs on it whatever was asked for: a
// runner compares it with the engine it asked for (tests/debugger/selftest/selftest.py), so that a key which did not
// reach the emulator, or which the emulator did not take, does not pass for a session of that engine. The emulator
// compares it with its configuration as well (kEngineAsked).
static void SayEngine()
{
	emu::Sh4EngineInfo engine = { };

	if (emu::GetSh4Engine(&engine))
	{
		bool interpreter = strcmp(engine.backend, "interpreter") == 0;

		printf("SelfTest: engine: the SH4 runs on %s%s%s, its breakpoints and steps on %s\n",
			interpreter ? "the interpreter" : "the recompiler (", interpreter ? "" : engine.backend, interpreter ? "" : ")",
			engine.fallback ? "sh4-debug-fallback" : "sh4-debug-native");
	}
	else
		printf("SelfTest: engine: not known: no backend of the SH4 is selected\n");

	fflush(stdout);
}

// The check that the engine of the session is the one its configuration asks for: a machine test of this name,
// which the emulator registers (dbg_selftest_emu.cpp). A program that runs sessions on a machine of its own, whose
// engine is no matter of the configuration (tests/debugger/sh4bp/selftest_shim.cpp), registers none. No filter
// selects it: every session runs it, behind the engine line and before the tests of its filter, so that a runner
// which asks for an engine by configuration and looks only at the end of a session fails a session of another
// engine. Its lines are those of a test ("SelfTest: engine.asked: note: ..." or "...: FAILED: ..."); it prints no
// PASS or FAIL line, and the summary counts it only when it fails.
static const char kEngineAsked[] = "engine.asked";

void AtStart()
{
	if (session.started || !Requested())
		return;

	session.started = true;
	session.guests = new std::vector<Test*>();

	SayEngine();

	// the driver's own test enters the registry here, the tests of the packages during static initialisation
	static Test self("selftest", SelfCheck, nullptr);

	std::vector<Test*> selected;
	Test* asked = nullptr;

	for (Test* test = registry; test; test = test->next)
	{
		if (test->machine && strcmp(test->name, kEngineAsked) == 0)
			asked = test;
		else if (Matches(Filter().c_str(), test->name))
			selected.push_back(test);
	}

	std::sort(selected.begin(), selected.end(), [](const Test* a, const Test* b) { return strcmp(a->name, b->name) < 0; });

	if (selected.empty())
	{
		printf("SelfTest: no test matches \"%s\"\n", Filter().c_str());
		Finish();
	}

	for (size_t i = 1; i < selected.size(); i++)
	{
		if (strcmp(selected[i - 1]->name, selected[i]->name) == 0)
		{
			printf("SelfTest: %s: FAILED: two tests have this name\n", selected[i]->name);
			session.testsFailed++;
		}
	}

	if (asked)
	{
		Begin(asked);
		asked->machine();

		if (session.failed != 0)
			session.testsFailed++;

		session.running = nullptr;
	}

	for (Test* test : selected)
	{
		if (test->machine)
		{
			Begin(test);
			test->machine();
			End();
		}
	}

	for (Test* test : selected)
	{
		if (test->guest)
			session.guests->push_back(test);
	}

	if (session.guests->empty())
		Finish();
}

bool Active()
{
	return session.started && session.current < session.guests->size();
}

void OnStop(const emu::RunState& stop)
{
	if (!Active())
		return;

	while (session.current < session.guests->size())
	{
		Test* test = (*session.guests)[session.current];

		if (session.running != test)
			Begin(test);

		if (!test->guest(stop))
			return;

		End();
		session.current++;
	}

	Finish();
}

// The driver's check of itself: the filter rules and the registration.
static void SelfCheck()
{
	Check(Matches("all", "regs.slots"), "\"all\" selects every test");
	Check(Matches("regs", "regs.slots") && Matches("regs.slots", "regs.slots"), "a name selects itself and the names below it");
	Check(!Matches("reg", "regs.slots") && !Matches("regs.slots", "regs") && !Matches("regs.", "regs.slots"),
		"an item that does not end where a dot follows selects nothing");
	Check(Matches("emu, regs", "regs.slots") && !Matches("", "regs") && !Matches(" ,, ", "regs"), "lists, blanks and empty items");

	u32 count = 0;
	bool found = false;

	for (Test* test = registry; test; test = test->next)
	{
		count++;
		found = found || test->machine == SelfCheck;
	}

	Check(found, "the registry holds this test, %u registered", count);
}

} }
