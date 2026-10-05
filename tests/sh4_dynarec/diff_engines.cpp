#include "diff_engines.h"
#include "diff_memory.h"
#include "dynarec_runner.h"
#include "ss_state.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_mem.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

const char* const diff_kind_name[DK_COUNT] = { "identical", "hang", "both-crash", "both-notify", "crash", "path", "bus", "state", "notify" };

const char* const diff_kind_description[DK_COUNT] =
{
	"",
	"an engine does not finish",
	"both engines end in the same die()/verify()/signal, there are no results to compare",
	"the engines agree, and both carry on from the same notifications of the emulator code (msgboxf)",
	"one engine ends in die()/verify()/a signal and the other runs through, or they end in different ones",
	"the instructions are run from different addresses",
	"the data accesses differ",
	"the final states differ",
	"the emulator code notifies (msgboxf) and carries on in one engine, not in the other",
};

static const DynarecCode code = { &diff_mem_opcode_at, &diff_mem_serve_opcodes };

static u32 executed[4];

//SH4IInterpreter::Loop, one instruction at a time. A delayed branch fetches and executes its delay slot itself
static void run_interpreter(void*)
{
	while (diff_mem_fetches().size() < 4)
	{
		u32 addr = Sh4cntx.pc;
		Sh4cntx.pc += 2;

		u32 op = IReadMem16(addr);

		ExecuteOpcode(op);
	}
}

static void run_dynarec(void*)
{
	dynarec_run(code, 4, executed);
}

static void run_engine(const SsTest& test, bool interpreter, EngineResult& result)
{
	diff_mem_start(interpreter);
	ss_state_load(test.initial.s);

	result.name = interpreter ? "interpreter" : "dynarec";
	result.guard = test_guard_run(interpreter ? &run_interpreter : &run_dynarec, 0);

	ss_state_save(result.state);

	result.message = test_guard_message();
	result.notifications = result.guard == TGR_OK ? test_guard_msgbox_count() : 0;
	result.notification = result.notifications ? test_guard_last_msgbox() : "";
	result.notification_texts.clear();
	for (u32 n = 0; n < test_guard_msgbox_texts() && result.notifications; n++)
		result.notification_texts.push_back(test_guard_msgbox_text(n));
	result.bus = diff_mem_log();
	result.native = !interpreter && dynarec_native(SS_TEST_INSTRUCTION);

	if (interpreter)
		result.path = diff_mem_fetches();
	else if (result.guard == TGR_OK)
		result.path.assign(executed, executed + 4);
	else
		result.path.clear();
}

void diff_engines_init()
{
	diff_mem_install();
	dynarec_init();
}

void diff_run_interpreter(const SsTest& test, EngineResult& result)
{
	run_engine(test, true, result);
}

void diff_run_dynarec(const SsTest& test, EngineResult& result)
{
	run_engine(test, false, result);
}

std::string diff_format(const char* fmt, ...)
{
	char text[1536];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	return text;
}

std::string diff_bus_text(const SsBusAccess& a)
{
	return diff_format("%s%u @%08X=%0*llX", a.kind == SSB_WRITE ? "write" : "read", a.size * 8, a.addr, a.size * 2, (unsigned long long)a.value);
}

std::string diff_ended_text(const EngineResult& e)
{
	std::string message = e.message;

	//verify() and die() name the source file with the path of the checkout
	size_t dir = message.find("/libswirl/");
	size_t path = dir == std::string::npos ? dir : message.rfind(' ', dir);

	if (path != std::string::npos)
		message.erase(path + 1, dir - path);

	return diff_format("%s (%s)", test_guard_result_name(e.guard), message.c_str());
}

static void append(std::string& what, const std::string& text)
{
	if (!what.empty())
		what += "; ";

	what += text;
}

DiffKind diff_compare(const EngineResult& i, const EngineResult& d, std::string& what)
{
	what.clear();

	if (i.guard == TGR_TIMEOUT || d.guard == TGR_TIMEOUT)
	{
		what = diff_format("hang: the %s does not finish", i.guard == TGR_TIMEOUT ? i.name : d.name);
		return DK_HANG;
	}

	if (i.guard != TGR_OK && d.guard != TGR_OK)
	{
		if (i.guard == d.guard && i.message == d.message)
		{
			what = "both-crash: " + diff_ended_text(i);
			return DK_BOTH_CRASH;
		}

		what = "crash: the interpreter ends in " + diff_ended_text(i) + ", the dynarec in " + diff_ended_text(d);
		return DK_CRASH;
	}

	if (i.guard != TGR_OK || d.guard != TGR_OK)
	{
		const EngineResult& e = i.guard != TGR_OK ? i : d;
		const EngineResult& o = i.guard != TGR_OK ? d : i;

		what = diff_format("crash: the %s ends in %s, the %s runs through", e.name, diff_ended_text(e).c_str(), o.name);
		return DK_CRASH;
	}

	DiffKind kind = DK_IDENTICAL;

	if (i.path != d.path)
	{
		size_t n = 0;

		while (n < i.path.size() && n < d.path.size() && i.path[n] == d.path[n])
			n++;

		if (n < i.path.size() && n < d.path.size())
			append(what, diff_format("path: instruction %u is run from %08X by the interpreter, from %08X by the dynarec", (u32)n, i.path[n], d.path[n]));
		else
			append(what, diff_format("path: the interpreter runs %u instructions, the dynarec %u", (u32)i.path.size(), (u32)d.path.size()));

		kind = DK_PATH;
	}

	bool bus_differs = false;

	for (size_t n = 0; n < i.bus.size() && n < d.bus.size() && !bus_differs; n++)
	{
		const SsBusAccess& a = i.bus[n];
		const SsBusAccess& b = d.bus[n];

		if (a.kind != b.kind || a.size != b.size || a.addr != b.addr || a.value != b.value)
		{
			append(what, diff_format("bus: data access %u is %s in the interpreter, %s in the dynarec", (u32)n, diff_bus_text(a).c_str(), diff_bus_text(b).c_str()));
			bus_differs = true;
		}
	}

	if (!bus_differs && i.bus.size() != d.bus.size())
	{
		const EngineResult& more = i.bus.size() > d.bus.size() ? i : d;
		const EngineResult& less = i.bus.size() > d.bus.size() ? d : i;

		append(what, diff_format("bus: the %s makes %u data accesses, the %s %u and not %s", more.name, (u32)more.bus.size(), less.name,
			(u32)less.bus.size(), diff_bus_text(more.bus[less.bus.size()]).c_str()));
		bus_differs = true;
	}

	if (bus_differs && kind == DK_IDENTICAL)
		kind = DK_BUS;

	const u32* a = (const u32*)&i.state;
	const u32* b = (const u32*)&d.state;
	u32 words = 0;
	std::string state;

	for (u32 w = 0; w < SS_STATE_WORDS; w++)
	{
		if (a[w] == b[w])
			continue;

		if (words < 4)
			state += diff_format("%s%s interpreter %08X dynarec %08X", words ? ", " : "", ss_state_word_name(w), a[w], b[w]);

		words++;
	}

	if (words != 0)
	{
		if (words > 4)
			state += diff_format(" and %u more registers", words - 4);

		append(what, "state: " + state);

		if (kind == DK_IDENTICAL)
			kind = DK_STATE;
	}

	if (i.notifications != d.notifications || i.notification != d.notification || i.notification_texts != d.notification_texts)
	{
		std::string notify;

		if (i.notifications && d.notifications)
			notify = diff_format("the interpreter notifies %u times, last \"%s\", the dynarec %u times, last \"%s\"", i.notifications,
				i.notification.c_str(), d.notifications, d.notification.c_str());
		else
			notify = diff_format("only the %s notifies: \"%s\"", i.notifications ? i.name : d.name,
				i.notifications ? i.notification.c_str() : d.notification.c_str());

		append(what, "notify: " + notify);

		if (kind == DK_IDENTICAL)
			kind = DK_NOTIFY;
	}
	else if (kind == DK_IDENTICAL && i.notifications)
	{
		what = diff_format("both-notify: both engines notify %u times, last \"%s\"", i.notifications, i.notification.c_str());
		kind = DK_BOTH_NOTIFY;
	}

	return kind;
}

bool diff_compare_selfcheck()
{
	static const SsBusAccess write = { SSB_WRITE, 4, 0x8C001000, 0x12345678 };

	EngineResult a;
	std::string what;

	a.name = "interpreter";
	a.guard = TGR_OK;
	a.notifications = 0;
	a.native = false;
	memset(&a.state, 0, sizeof(a.state));
	a.bus.push_back(write);
	for (u32 n = 0; n < 4; n++)
		a.path.push_back(0x8C010000 + n * 2);

	EngineResult b = a;
	b.name = "dynarec";

	const EngineResult same = b;
	bool ok = diff_compare(a, b, what) == DK_IDENTICAL;

	b.state.FPUL ^= 1;
	ok = ok && diff_compare(a, b, what) == DK_STATE;

	b = same;
	b.bus[0].value ^= 1;
	ok = ok && diff_compare(a, b, what) == DK_BUS;

	b = same;
	b.bus[0].kind = SSB_READ;
	ok = ok && diff_compare(a, b, what) == DK_BUS;

	b = same;
	b.bus.clear();
	ok = ok && diff_compare(a, b, what) == DK_BUS;

	b = same;
	b.path[3] += 2;
	ok = ok && diff_compare(a, b, what) == DK_PATH;

	b = same;
	b.path.pop_back();
	ok = ok && diff_compare(a, b, what) == DK_PATH;

	b = same;
	b.notifications = 1;
	b.notification = "notification";
	b.notification_texts.push_back(b.notification);
	ok = ok && diff_compare(a, b, what) == DK_NOTIFY;

	//the same notification in both is not identical, another one or another number of them is a difference
	EngineResult both = a;
	both.notifications = 1;
	both.notification = "notification";
	both.notification_texts.push_back(both.notification);
	ok = ok && diff_compare(both, b, what) == DK_BOTH_NOTIFY;

	b.notifications = 2;
	ok = ok && diff_compare(both, b, what) == DK_NOTIFY;

	b.notifications = 1;
	b.notification = "another notification";
	b.notification_texts[0] = b.notification;
	ok = ok && diff_compare(both, b, what) == DK_NOTIFY;

	b.state.FPUL ^= 1;
	ok = ok && diff_compare(both, b, what) == DK_STATE;

	b.notification = both.notification;
	b.notification_texts = both.notification_texts;
	ok = ok && diff_compare(both, b, what) == DK_STATE;

	b = same;
	b.guard = TGR_DIE;
	b.message = "verify";
	ok = ok && diff_compare(a, b, what) == DK_CRASH;

	EngineResult c = a;
	c.guard = TGR_DIE;
	c.message = "verify";
	ok = ok && diff_compare(c, b, what) == DK_BOTH_CRASH;

	c.message = "another verify";
	ok = ok && diff_compare(c, b, what) == DK_CRASH;

	b.guard = TGR_TIMEOUT;
	ok = ok && diff_compare(a, b, what) == DK_HANG;

	return ok;
}
