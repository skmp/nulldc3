/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of the core: the wire protocol, the tables, the tick builder and the event log, against the
	live machine.

	    nulldc3 --config config:Headless=yes , audio:backend=none , Debugger:SelfTest=core 2ndmix.elf

	The session has no server, and so no client and no core thread: Debugger:SelfTest keeps the server off
	unless Debugger:Enabled is set explicitly. With a server running every test fails its first check instead
	of running beside the core thread. The tests reach the core through dbg_core_private.h: they hand request
	texts to the dispatcher and get the response texts, read ticks with core::BuildTickText and the describe
	result as text, and state the number of connected clients themselves.

	  core.events    (machine) the event log
	  core.opcodes   (machine) the disassembler against the opcode table, and the line of the reios trap
	  core.tables    (machine) breakpoints, watches, categories; the active mask and the event gates
	  core.tick      (machine) the JSON of ticks and of describe; strings that are no UTF-8
	  core.guest     (guest)   the booted ELF: symbols, call stacks, the breakpoint of a stop
	  core.lifetime  (guest)   a machine that is stopped, whose guest has exited, that is gone and made again
	  core.reset     (guest)   control.reset: at a stop, which becomes the stop of the reset, and while the machine runs
	  core.writes    (guest)   state.writeMemory, state.setRegister and state.editWatch by parameter, error order,
	                           channel and lifetime; what describe says of them; the tick behind a write

	A test on (E) hands its requests over in place: it stands where a call of every class may stand. What comes
	in as it does on the core thread, through dbg::emu::Call, comes from a thread the test starts: while the
	host has the machine stopped, and while there is none.

	The session boots 2ndmix.elf (tests/debugger/selftest/CMakeLists.txt). core.guest starts at the reset
	vector, where reios has its trap, and resets the machine when it is not there.

	What the writers under the three write methods do to the machine is not this session's: that is the session
	"writes" (dbg_selftest_writes.cpp), on the guest of the tree.
*/

// json.hpp comes in front of every tree header
#include "deps/nlohmann/json.hpp"

#include "dbg_selftest.h"
#include "dbg_arm7.h"
#include "dbg_core_private.h"
#include "dbg_dsp.h"
#include "dbg_disasm.h"
#include "dbg_mem.h"
#include "dbg_regs.h"
#include "dbg_sh4.h"
#include "dbg_symbols.h"

#include "libswirl.h"
#include "cfg/cfg.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_opcode_list.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/sh4/interpr/sh4_opcodes.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dbg { namespace emu { namespace test {

// dbg_emu.cpp: invoked behind the listener with every state that is published
extern void (*observer)(const RunState& state);

} } }

namespace {

using namespace dbg;
using nlohmann::json;
using selftest::Check;
using selftest::Note;

const u32 kResetVector = 0xA0000000;
const u32 kScratchAddr = 0x8C00F000;		// inside the 64 KiB that a boot of reios fills: no program is there
const char* const kHotSymbol = "_stars_one_frame";	// 2ndmix calls it once in every frame

typedef std::chrono::steady_clock Clock;

double MsSince(Clock::time_point t0)
{
	return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

void Nap(int ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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
	A bound is generous: a test says its longest wait in a note, and a session that passes waits about a second at
	most (the boot of 2ndmix on the interpreter, in core.guest) where the bound is a minute. The waits of the
	threads that the tests start have the same bound.
	The machine is the host's, and the host is one thread at a time: bound.restart. The watch holds it for the
	stop and the start it asks for; Restart() holds it; and a thread of a test that takes the machine apart and
	makes it again (core.lifetime, core.writes) holds it from the moment it lets go of the machine until the new
	one has started. So the watch never calls a machine that is being taken apart, nor starts one that such a
	thread starts. Where it had to wait for bound.restart it first waits, a tenth of the bound, for the start park
	of the machine that the thread which held it has started; then it goes on as above. A thread that holds the
	machine for a tenth of the bound beyond the cut has the watch end the process as above.
*/
const int kBoundMs = 60000;

struct Bound {
	std::mutex lock;
	const char* test = nullptr;		// the test that runs, or waits
	bool waiting = false;			// it has returned false: it waits for a stop
	Clock::time_point since;
	double longest = 0;				// ms: the longest wait of the test (emulator thread only)
	const char* cut = nullptr;		// the test whose wait the watch cut
	bool taken = false;				// the stop behind the cut was handed to that test
	const char* how = "";			// what the watch asked the machine for
	std::thread* watch = nullptr;
	std::timed_mutex restart;		// the host: what calls, starts, takes apart or makes the machine (above)
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

// The host starts the stopped machine again: the next thread parks at its start, as every thread of a session.
// False when the machine is not stopped: the watch and a thread of a test can both find it stopped, and only one
// starts it. The caller holds bound.restart
bool RestartHeld()
{
	if (emu::GetRunState().lifetime != Lifetime::Stopped || !virtualDreamcast)
		return false;

	virtualDreamcast->Resume();
	return true;
}

bool Restart()
{
	std::lock_guard<std::timed_mutex> l(bound.restart);

	return RestartHeld();
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

		if (bound.waiting && MsSince(bound.since) > kBoundMs)
			bound.cut = test = bound.test;
	}

	if (emu::GetRunState().lifetime == Lifetime::Running)
	{
		CutAsk("a pause");
		emu::RequestPause();

		if (CutUntil(step, false))
			return;
	}

	// The machine is the host's from here on (bound.restart)
	std::unique_lock<std::timed_mutex> host(bound.restart, std::try_to_lock);

	if (!host.owns_lock())
	{
		// said before the wait: the start park of the machine that thread starts can come before the lock is free
		CutAsk("nothing: a thread of the test that held the machine started it");

		if (!host.try_lock_for(std::chrono::milliseconds(step)))
		{
			printf("SelfTest: %s: FAILED: the stop the test waited for did not come within the bound of the session, %d s, and a thread of the test that "
				"takes the machine apart and makes it again held the machine for a tenth of the bound more: the session ends here, with no summary\n",
				test, kBoundMs / 1000);
			fflush(stdout);
			std::_Exit(1);
		}

		// that thread has started the machine: its thread parks at its start
		if (CutUntil(step, false))
			return;
	}

	if (CutTaken())
		return;

	CutAsk("a start of the host");

	if (emu::GetRunState().lifetime != Lifetime::Stopped && virtualDreamcast)
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

	if (CutTaken())
		return;

	const bool restarted = RestartHeld();

	host.unlock();

	if (restarted && CutUntil(step, false))
		return;

	printf("SelfTest: %s: FAILED: the stop the test waited for did not come within the bound of the session, %d s, and the machine gave none for a pause, "
		"a stop and a start of the host either: the session ends here, with no summary\n", test, kBoundMs / 1000);
	fflush(stdout);
	std::_Exit(1);
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
	Requests and what comes back
*/
u32 request_id = 100;

// The text of a JSON string, "" for anything else
std::string Str(const json& j)
{
	return j.is_string() ? j.get<std::string>() : std::string();
}

// The response of one request, parsed. A response that is no JSON fails a check and gives null
json Send(const std::string& method, const json& params = json::object())
{
	const u32 id = request_id++;
	json request = { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params } };
	const std::string text = core::HandleRequestText(request.dump());
	json response = json::parse(text, nullptr, false);

	if (response.is_discarded() || !response.is_object() || response.value("id", json()) != json(id))
	{
		Check(false, "%s is answered with a JSON-RPC response that echoes the id %u: %.200s", method.c_str(), id, text.c_str());
		return json();
	}

	return response;
}

// {} for a request that succeeded
bool Ok(const json& response)
{
	return response.is_object() && response.contains("result") && response["result"].is_object() && response["result"].empty();
}

// The code of an in-band error, 0 when the response has none
int InBand(const json& response)
{
	if (!response.is_object() || !response.contains("result") || !response["result"].is_object() || !response["result"].contains("error"))
		return 0;

	const json& error = response["result"]["error"];

	if (!error.is_object() || error.size() != 2 || !error.contains("code") || !error.contains("message") || !error["message"].is_string()
		|| Str(error["message"]).empty())
		return 1;

	return error["code"].get<int>();
}

// The code of a JSON-RPC error, 0 when the response has none
int RpcError(const json& response)
{
	if (!response.is_object() || !response.contains("error") || response.contains("result"))
		return 0;

	const json& error = response["error"];

	if (!error.is_object() || error.size() != 2 || !error.contains("code") || !error.contains("message") || !error["message"].is_string()
		|| Str(error["message"]).empty())
		return 1;

	return error["code"].get<int>();
}

// The params of a tick
json Tick(bool full, std::string* raw = nullptr)
{
	const std::string text = core::BuildTickText(full);
	json message = json::parse(text, nullptr, false);

	if (raw != nullptr)
		*raw = text;

	if (message.is_discarded() || !message.is_object() || message.value("jsonrpc", "") != "2.0" || message.value("method", "") != "event.tick"
		|| !message.contains("params") || message.size() != 3)
	{
		Check(false, "a tick is the notification event.tick: %.200s", text.c_str());
		return json::object();
	}

	return message["params"];
}

// A register of a tick: its value, "" when the tick does not carry it
std::string Reg(const json& tick, const char* path, const char* name)
{
	if (!tick.is_object() || !tick.contains("registers") || !tick["registers"].is_object() || !tick["registers"].contains(path))
		return "";

	for (const json& r : tick["registers"][path])
	{
		if (r.is_object() && r.value("name", "") == name)
			return r.value("value", "");
	}

	return "";
}

u32 Hex(const std::string& text)
{
	return text.rfind("0x", 0) == 0 ? (u32)strtoul(text.c_str() + 2, nullptr, 16) : 0xFFFFFFFF;
}

bool HasNull(const json& j)
{
	if (j.is_null())
		return true;

	if (j.is_structured())
	{
		for (const json& item : j)
		{
			if (HasNull(item))
				return true;
		}
	}

	return false;
}

// Nothing but printable ASCII: what the core sends is valid UTF-8 whatever its strings came from
bool Ascii(const std::string& text)
{
	for (unsigned char c : text)
	{
		if (c < 0x20 || c > 0x7E)
			return false;
	}

	return true;
}

// "0x" and ceil(width / 4) upper-case hex digits, or "n/a"
bool WireValue(const std::string& value, u32 width)
{
	if (value == "n/a")
		return true;

	if (width == 0)
		return !value.empty();

	if (value.size() != 2 + (width + 3) / 4 || value[0] != '0' || value[1] != 'x')
		return false;

	for (size_t i = 2; i < value.size(); i++)
	{
		if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'A' && value[i] <= 'F')))
			return false;
	}

	return true;
}

// Every test begins here: not beside the core thread
bool Alone(const char* test)
{
	return Check(!core::Started(), "%s runs in a session without a server: the core thread was not started", test);
}

// The ids of the breakpoints a tick lists, ascending
std::vector<u32> BreakpointIds(const json& tick)
{
	std::vector<u32> ids;

	if (tick.contains("breakpoints") && tick["breakpoints"].is_object())
	{
		for (auto it = tick["breakpoints"].begin(); it != tick["breakpoints"].end(); ++it)
			ids.push_back((u32)strtoul(it.key().c_str(), nullptr, 10));
	}

	std::sort(ids.begin(), ids.end());
	return ids;
}


// The tables as the session found them: nothing listed, nothing muted, no client, no category active
void CleanTables()
{
	json tick = Tick(false);
	json ids = json::array();

	for (u32 id : BreakpointIds(tick))
		Send("breakpoints.remove", { { "id", id } });

	for (json& w : tick["watches"])
		ids.push_back(w["id"]);

	Send("state.unwatch", { { "watchIds", ids } });

	json off = { { "muted", false }, { "soloed", false } };

	Send("breakpoints.setCategoryStates", { { "categories", { { "events", off }, { "sh4", off }, { "arm7", off }, { "dsp", off } } } });

	core::SetClientCountForTest(0, false);

	// A session starts with no category active, and the tests of the other packages set the mask themselves
	// (dbg_selftest.h): the core, which has no client here, hands over nothing behind this
	emu::SetActiveMask(0);
}

// Did a test of this session add a breakpoint or a watch before? Ids are of the process
bool ids_used;

/*
	An ELF32 image with a .symtab, for the strings that come from a guest image
*/
struct FixtureSymbol {
	std::string name;
	u32 value;
	u32 size;
};

void Put32(std::vector<u8>& out, size_t at, u32 v)
{
	for (int i = 0; i < 4; i++)
		out[at + i] = (u8)(v >> (8 * i));
}

std::vector<u8> BuildElf(const std::vector<FixtureSymbol>& symbols)
{
	std::vector<u8> strtab(1, 0);
	std::vector<u32> name_at;

	for (const FixtureSymbol& s : symbols)
	{
		name_at.push_back((u32)strtab.size());
		strtab.insert(strtab.end(), s.name.begin(), s.name.end());
		strtab.push_back(0);
	}

	const size_t symtab = 52;
	const size_t symtab_size = (symbols.size() + 1) * 16;
	const size_t strtab_at = symtab + symtab_size;
	const size_t shdrs = strtab_at + strtab.size();

	std::vector<u8> out(shdrs + 3 * 40, 0);

	memcpy(out.data(), "\x7F" "ELF", 4);
	out[4] = 1;
	out[5] = 1;
	out[6] = 1;
	Put32(out, 32, (u32)shdrs);
	out[46] = 40;
	out[48] = 3;

	for (size_t i = 0; i < symbols.size(); i++)
	{
		const size_t at = symtab + (i + 1) * 16;

		Put32(out, at, name_at[i]);
		Put32(out, at + 4, symbols[i].value);
		Put32(out, at + 8, symbols[i].size);
		out[at + 12] = 0x12;		// a global function
		out[at + 14] = 1;
	}

	memcpy(out.data() + strtab_at, strtab.data(), strtab.size());

	// section 1: .symtab, its names in section 2
	Put32(out, shdrs + 40 + 4, 2);
	Put32(out, shdrs + 40 + 16, (u32)symtab);
	Put32(out, shdrs + 40 + 20, (u32)symtab_size);
	Put32(out, shdrs + 40 + 24, 2);
	Put32(out, shdrs + 40 + 36, 16);

	Put32(out, shdrs + 80 + 4, 3);
	Put32(out, shdrs + 80 + 16, (u32)strtab_at);
	Put32(out, shdrs + 80 + 20, (u32)strtab.size());

	return out;
}

}

/*
	core.events: the ring
*/
DBG_SELFTEST(core_events, "core.events")
{
	if (!Alone("core.events"))
		return;

	events::ClearForTest();
	core::SetClientCountForTest(0, false);

	// with the gates closed nothing is logged, and the arguments are not evaluated
	int evaluated = 0;

	DBG_EVENT(EV_DEBUGGER_NOTE, "closed %d", ++evaluated);
	DBG_EVENT(EV_CORE_VBLANK, "closed %d", ++evaluated);

	json tick = Tick(false);

	Check(evaluated == 0 && tick["eventLog"].empty(), "no client: every gate is closed, an event is not logged and its arguments are not evaluated (%d, %zu entries)",
		evaluated, tick["eventLog"].size());

	// the one entry that is made with the gates closed
	events::Append(EV_DEBUGGER_NOTE, "appended with the gates closed");
	tick = Tick(false);

	Check(tick["eventLog"].size() == 1 && tick["eventLog"][0]["message"] == "appended with the gates closed"
		&& tick["eventLog"][0]["metadata"]["event"] == "dc.debugger.note", "the direct append makes an entry whatever the gates say (%zu entries)",
		tick["eventLog"].size());

	const u64 first_id = strtoull(tick["eventLog"][0].value("eventId", "0").c_str(), nullptr, 10);

	// a client: the low-rate events are logged, a high-rate one without an event breakpoint is not
	core::SetClientCountForTest(1, false);

	DBG_EVENT(EV_CORE_VBLANK, "high rate, no breakpoint");
	DBG_EVENT(EV_SH4_EXCEPTION, "low rate %d", 7);

	tick = Tick(false);

	Check(tick["eventLog"].size() == 2 && tick["eventLog"][1]["message"] == "low rate 7", "a client: a low-rate event is logged, a high-rate one without a breakpoint is not (%zu entries)",
		tick["eventLog"].size());

	if (tick["eventLog"].size() == 2)
	{
		json& entry = tick["eventLog"][1];
		const u64 now_ms = (u64)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		const u64 stamp = entry.value("timestamp", (u64)0);

		Check(entry.size() == 6 && entry["subsystem"] == "sh4" && entry["severity"] == "warn" && entry["metadata"].size() == 2 && entry["metadata"]["event"] == "dc.sh4.exception",
			"an entry has eventId, timestamp, subsystem, severity, message and metadata, from the event table (%s)", entry.dump().c_str());
		Check(entry["metadata"]["cycles"].is_string() && entry["metadata"]["cycles"] == std::to_string(sh4_sched_now64()),
			"metadata.cycles is the time of the scheduler, in decimal (%s, now %llu)", entry["metadata"]["cycles"].dump().c_str(), (unsigned long long)sh4_sched_now64());
		Check(entry["eventId"].is_string() && entry["eventId"] == std::to_string(first_id + 1), "eventId is the sequence number of the process, a decimal string (%s behind %llu)",
			entry["eventId"].dump().c_str(), (unsigned long long)first_id);
		Check(stamp <= now_ms && now_ms - stamp < 60000, "timestamp is the wall clock in milliseconds (%llu, now %llu)", (unsigned long long)stamp, (unsigned long long)now_ms);
	}

	// the ring keeps 200 entries: 300 more
	for (int i = 0; i < 300; i++)
		DBG_EVENT(EV_DEBUGGER_NOTE, "entry %d", i);

	std::string raw;

	tick = Tick(false, &raw);

	json& log = tick["eventLog"];
	bool ascending = log.size() == kEventRingSize;

	for (size_t i = 1; ascending && i < log.size(); i++)
		ascending = strtoull(log[i].value("eventId", "0").c_str(), nullptr, 10) == strtoull(log[i - 1].value("eventId", "0").c_str(), nullptr, 10) + 1;

	Check(log.size() == kEventRingSize, "the log holds %u entries (%zu)", kEventRingSize, log.size());
	Check(ascending, "oldest first, the eventIds ascend by one");
	Check(log.size() == kEventRingSize && log[0]["message"] == "entry 100" && log[kEventRingSize - 1]["message"] == "entry 299",
		"the oldest entries were dropped: the log is entries 100 to 299 (%s .. %s)", log[0]["message"].dump().c_str(), log[log.size() - 1]["message"].dump().c_str());

	// the same id is the same entry in every tick
	json again = Tick(true)["eventLog"];

	Check(again == log, "the next tick has the same entries under the same eventIds");

	DBG_EVENT(EV_DEBUGGER_NOTE, "one more");

	json moved = Tick(false)["eventLog"];

	Check(moved.size() == kEventRingSize && moved[0] == log[1] && moved[kEventRingSize - 2] == log[kEventRingSize - 1] && moved[kEventRingSize - 1]["message"] == "one more",
		"one more event: every entry that is left keeps its eventId, the oldest is gone");

	// a message is cut to its bound
	const std::string big(600, 'x');

	DBG_EVENT(EV_DEBUGGER_NOTE, "%s", big.c_str());

	json cut = Tick(false)["eventLog"];

	Check(Str(cut[cut.size() - 1]["message"]) == std::string(kEventMessageMax - 1, 'x'), "a message of 600 bytes is cut to %u (%zu)", kEventMessageMax - 1,
		Str(cut[cut.size() - 1]["message"]).size());

	CleanTables();
	events::ClearForTest();

	Check(Tick(false)["eventLog"].empty(), "(the log is empty for the tests behind this one)");
}

/*
	core.opcodes: what the disassembler calls an instruction is what the opcode table executes
*/
DBG_SELFTEST(core_opcodes, "core.opcodes")
{
	if (!Alone("core.opcodes"))
		return;

	u32 differ = 0, first = 0x10000, valid_count = 0;

	for (u32 op = 0; op < 0x10000; op++)
	{
		Sh4InsnInfo info;

		DisasmSh4(0x8C010000, (u16)op, 0, nullptr, &info);

		const bool executes = OpDesc[op]->oph != iNotImplemented;

		if (info.valid)
			valid_count++;

		// the reios trap: the table executes it as an HLE call, and it is no SH4 instruction
		if (op == 0x085B)
			continue;

		if (info.valid != executes)
		{
			differ++;
			if (first == 0x10000)
				first = op;
		}
	}

	Check(differ == 0, "for all 65536 encodings but 085B, dbg_disasm reports an instruction exactly where the opcode table executes one (%u differ, the first is %04X; %u are instructions)",
		differ, first & 0xFFFF, valid_count);

	{
		Sh4InsnInfo info;
		const std::string text = DisasmSh4(0x8C010000, 0x085B, 0, nullptr, &info);

		Check(OpDesc[0x085B]->oph != iNotImplemented && !info.valid && text == ".word 0x085b",
			"085B is the one exception: the table executes it, dbg_disasm prints \"%s\" and does not call it an instruction", text.c_str());
	}

	// the core's line for that word
	u8* at = GetMemPtr(kScratchAddr, 4);
	u8 kept[4];

	memcpy(kept, at, 4);
	at[0] = 0x5B;
	at[1] = 0x08;
	at[2] = 0x09;
	at[3] = 0x00;

	json answer = Send("state.getDisassembly", { { "target", "sh4" }, { "address", kScratchAddr }, { "count", 2 } });

	memcpy(at, kept, 4);

	const bool shaped = answer.contains("result") && answer["result"].contains("lines") && answer["result"]["lines"].size() == 2;

	Check(shaped && answer["result"]["lines"][0]["disassembly"] == ".word 0x085b ! reios trap" && answer["result"]["lines"][0]["bytes"] == "5B 08"
		&& answer["result"]["lines"][0]["address"] == kScratchAddr,
		"state.getDisassembly prints the word as \".word 0x085b ! reios trap\", bytes in memory order (%s)", answer.dump().c_str());
	Check(shaped && answer["result"]["lines"][1]["disassembly"] == "nop" && answer["result"]["lines"][1]["bytes"] == "09 00" && answer["result"]["lines"][1]["address"] == kScratchAddr + 2,
		"and the instruction behind it as dbg_disasm does");
}

/*
	core.tables: breakpoints, watches and categories as the dispatcher's calls use them
*/
DBG_SELFTEST(core_tables, "core.tables")
{
	if (!Alone("core.tables"))
		return;

	const bool first_user = !ids_used;

	ids_used = true;

	CleanTables();
	core::SetClientCountForTest(1, false);

	json none = Tick(false);

	Check(none["breakpoints"].empty() && none["watches"].empty() && none["categories"].size() == 4, "no breakpoints, no watches, four categories to begin with");

	/*
		Breakpoints
	*/
	json vblank = { { "event", "dc.holly.core.vblank" }, { "kind", "event" }, { "enabled", false } };

	Check(Ok(Send("breakpoints.add", vblank)), "an event breakpoint is added, disabled");

	json tick = Tick(false);
	std::vector<u32> ids = BreakpointIds(tick);
	const u32 id0 = ids.size() == 1 ? ids[0] : 0;

	Check(ids.size() == 1 && (!first_user || id0 == 1), "it is listed under a new id; ids start at 1 (%u%s)", id0, first_user ? "" : ", and another test added one before");

	if (ids.size() == 1)
	{
		json& bp = tick["breakpoints"][std::to_string(id0)];

		Check(bp.size() == 4 && bp["id"] == id0 && bp["event"] == "dc.holly.core.vblank" && bp["kind"] == "event" && bp["enabled"] == false,
			"the key of a breakpoint is its id in decimal; an event breakpoint has id, event, kind and enabled (%s)", bp.dump().c_str());
	}

	// the same request again makes a second breakpoint; an address given with an event breakpoint is not kept
	json with_address = vblank;

	with_address["address"] = 0x8C010000u;

	Check(Ok(Send("breakpoints.add", with_address)), "the same request again, with an address");
	tick = Tick(false);
	ids = BreakpointIds(tick);
	Check(ids.size() == 2 && tick["breakpoints"].contains(std::to_string(id0 + 1)) && !tick["breakpoints"][std::to_string(id0 + 1)].contains("address"),
		"it makes a second breakpoint with the next id, and the address of an event breakpoint is not stored");

	// a code breakpoint: kind from the address
	Check(Ok(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", 0x8C010000u } })), "a code breakpoint, without kind and enabled");
	tick = Tick(false);

	{
		const std::string key = std::to_string(id0 + 2);
		const bool listed = tick["breakpoints"].contains(key);

		Check(listed && tick["breakpoints"][key].size() == 5 && tick["breakpoints"][key]["kind"] == "code" && tick["breakpoints"][key]["address"] == 0x8C010000u
			&& tick["breakpoints"][key]["enabled"] == true && tick["breakpoints"][key]["event"] == "dc.sh4.cpu.pc",
			"with an address the kind is code and the breakpoint is enabled: id, event, address, kind, enabled (%s)", listed ? tick["breakpoints"][key].dump().c_str() : "not listed");
	}

	// The category of a breakpoint, and the active mask. An enabled SH4 code breakpoint makes the breakpoint
	// state want its fallback exactly while the category of that breakpoint is active
	const u32 all = CategoryBit(Category::Events) | CategoryBit(Category::Sh4) | CategoryBit(Category::Arm7) | CategoryBit(Category::Dsp);
	json on = { { "muted", true }, { "soloed", false } }, off = { { "muted", false }, { "soloed", false } }, solo = { { "muted", false }, { "soloed", true } };

	Check(core::ActiveMaskForTest() == all && sh4::FallbackWanted(), "nothing muted, a client connected: every category is active (mask %X)", core::ActiveMaskForTest());

	Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "events", on } } } })), "events muted");
	Check(core::ActiveMaskForTest() == (all & ~CategoryBit(Category::Events)) && sh4::FallbackWanted(),
		"the mask lacks events; a code breakpoint of the SH4 is not in that category (mask %X)", core::ActiveMaskForTest());

	Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "sh4", on }, { "unknown", on }, { "events", off } } } })), "sh4 muted, events not, and a key that is no category");
	tick = Tick(false);
	Check(core::ActiveMaskForTest() == (all & ~CategoryBit(Category::Sh4)) && !sh4::FallbackWanted(), "the mask lacks sh4: the code breakpoint of the SH4 is in the category sh4 (mask %X)",
		core::ActiveMaskForTest());
	Check(tick["categories"].size() == 4 && tick["categories"]["sh4"] == on && tick["categories"]["events"] == off && tick["categories"]["arm7"] == off && tick["categories"]["dsp"] == off,
		"the tick has the four categories as they are; an absent key keeps its state, an unknown one is not looked at (%s)", tick["categories"].dump().c_str());

	Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "sh4", off }, { "arm7", solo } } } })), "arm7 soloed");
	Check(core::ActiveMaskForTest() == CategoryBit(Category::Arm7) && !sh4::FallbackWanted(), "a soloed category is the only active one (mask %X)", core::ActiveMaskForTest());

	Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "sh4", solo } } } })), "sh4 soloed as well");
	Check(core::ActiveMaskForTest() == (CategoryBit(Category::Arm7) | CategoryBit(Category::Sh4)) && sh4::FallbackWanted(), "two soloed categories are both active (mask %X)",
		core::ActiveMaskForTest());

	Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "sh4", { { "muted", true }, { "soloed", true } } } } } })), "sh4 soloed and muted");
	Check(core::ActiveMaskForTest() == CategoryBit(Category::Arm7), "a muted category is not active, soloed or not (mask %X)", core::ActiveMaskForTest());

	Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "sh4", off }, { "arm7", off } } } })) && core::ActiveMaskForTest() == all, "all back");

	// no client
	core::SetClientCountForTest(0, false);
	Check(core::ActiveMaskForTest() == all && sh4::FallbackWanted(), "no client, inside the grace period: breakpoints stay active (mask %X)", core::ActiveMaskForTest());

	core::SetClientCountForTest(0, true);
	tick = Tick(false);
	Check(core::ActiveMaskForTest() == 0 && !sh4::FallbackWanted(), "no client and the grace period over: no category is active (mask %X)", core::ActiveMaskForTest());
	Check(tick["breakpoints"].size() == 3 && tick["categories"]["sh4"] == off, "the breakpoints stay listed, and the categories of the tick are not changed by it");

	core::SetClientCountForTest(1, false);
	Check(core::ActiveMaskForTest() == all, "a client again: active again (mask %X)", core::ActiveMaskForTest());

	// toggle, remove
	Check(Ok(Send("breakpoints.toggle", { { "id", id0 + 2 }, { "enabled", false } })) && !sh4::FallbackWanted() && Tick(false)["breakpoints"][std::to_string(id0 + 2)]["enabled"] == false,
		"a disabled breakpoint is listed as disabled and is not armed");
	Check(Ok(Send("breakpoints.toggle", { { "id", id0 + 2 }, { "enabled", true } })) && sh4::FallbackWanted(), "and enabled again");
	Check(InBand(Send("breakpoints.toggle", { { "id", 99999 }, { "enabled", true } })) == kErrInvalidParams, "toggle of an id that no breakpoint has: -32602, in-band");
	Check(Ok(Send("breakpoints.remove", { { "id", id0 + 2 } })) && !sh4::FallbackWanted() && Tick(false)["breakpoints"].size() == 2, "remove");
	Check(InBand(Send("breakpoints.remove", { { "id", id0 + 2 } })) == kErrInvalidParams, "remove of the same id again: -32602, in-band");
	Check(InBand(Send("breakpoints.remove", { { "id", "1" } })) == kErrInvalidParams && InBand(Send("breakpoints.remove", { { "id", -1 } })) == kErrInvalidParams
		&& InBand(Send("breakpoints.remove", { { "id", 1.5 } })) == kErrInvalidParams && InBand(Send("breakpoints.remove")) == kErrInvalidParams,
		"an id that is a string, negative, a fraction or missing: -32602");

	// ids are never used twice
	Check(Ok(Send("breakpoints.add", vblank)) && BreakpointIds(Tick(false)).back() == id0 + 3, "the next breakpoint has the id behind the removed one: ids are not used twice (%u)",
		BreakpointIds(Tick(false)).back());

	// the order of the checks of breakpoints.add
	Check(InBand(Send("breakpoints.add", { { "event", "dc.no.such.event" }, { "kind", "event" } })) == kErrNotSupported, "an event that no node lists: -32004");
	Check(InBand(Send("breakpoints.add", { { "event", "dc.no.such.event" } })) == kErrNotSupported, "the same without kind (no address: an event breakpoint)");
	Check(InBand(Send("breakpoints.add", { { "event", "dc.debugger.dsp_forced" }, { "kind", "event" } })) == kErrNotSupported && FindEvent("dc.debugger.dsp_forced") == EV_ID_COUNT,
		"a name that is in no row of the event table, the one the forcing of the DSP had: -32004");
	Check(InBand(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "kind", "event" } })) == kErrNotSupported, "the event of a code breakpoint is no event: -32004");
	Check(InBand(Send("breakpoints.add", { { "event", "dc.holly.core.vblank" }, { "kind", "code" }, { "address", 0 } })) == kErrNotSupported,
		"a code breakpoint on a name that is no target's: -32004");
	// The ARM7 is what emu::TargetSteps() says: such a target while its run control does not exist, and one
	// whose code breakpoints the core takes and hands over once it does
	if (!emu::TargetSteps(Target::Arm7))
	{
		Check(InBand(Send("breakpoints.add", { { "event", "dc.aica.arm7.pc" }, { "kind", "code" } })) == kErrNotSupported,
			"the ARM7 does not step: a code breakpoint of it is -32004, before its address is looked at");
	}
	else
	{
		auto arm7_at = [](u32 address) {
			return json({ { "event", "dc.aica.arm7.pc" }, { "address", address } });
		};

		// what dbg::arm7 was handed last, as { id, address } pairs in ascending order
		auto handed = [] {
			u32 got_ids[8], got_addrs[8];
			const u32 n = arm7::GetBreakpoints(got_ids, got_addrs, 8);
			std::vector<std::pair<u32, u32>> pairs;

			for (u32 i = 0; i < n && i < 8; i++)
				pairs.push_back({ got_ids[i], got_addrs[i] });

			// more than the test ever lists: no set of the test equals it
			if (n > 8)
				pairs.push_back({ 0, n });

			std::sort(pairs.begin(), pairs.end());
			return pairs;
		};

		typedef std::vector<std::pair<u32, u32>> Pairs;

		Check(InBand(Send("breakpoints.add", { { "event", "dc.aica.arm7.pc" }, { "kind", "code" } })) == kErrInvalidParams,
			"the ARM7 steps: a code breakpoint of it without an address is -32602");
		Check(InBand(Send("breakpoints.add", arm7_at(2))) == kErrInvalidParams, "an address of the ARM7 that is no multiple of 4: -32602");
		Check(InBand(Send("breakpoints.add", arm7_at(0x01000000u))) == kErrInvalidParams, "an address above the ARM7's addressMax, 0x00FFFFFF: -32602");
		Check(Tick(false)["breakpoints"].size() == 3 && handed().empty(), "the refused requests add nothing, and nothing of the ARM7 is handed to dbg::arm7");

		// Three of them: at the highest multiple of 4, low in the wave memory, and at an address that is a
		// mirror on a machine with 2 MiB of it. The addresses are kept as given
		const u32 arm7_addrs[3] = { 0x00FFFFFCu, 0x00000040u, 0x00200040u };
		u32 arm7_ids[3] = { 0, 0, 0 };
		bool added = true, shown = true;

		for (u32 i = 0; i < 3; i++)
		{
			added = added && Ok(Send("breakpoints.add", arm7_at(arm7_addrs[i])));
			arm7_ids[i] = BreakpointIds(Tick(false)).back();
		}

		tick = Tick(false);

		for (u32 i = 0; i < 3; i++)
		{
			const std::string key = std::to_string(arm7_ids[i]);

			shown = shown && tick["breakpoints"].contains(key) && tick["breakpoints"][key].size() == 5 && tick["breakpoints"][key]["id"] == arm7_ids[i]
				&& tick["breakpoints"][key]["event"] == "dc.aica.arm7.pc" && tick["breakpoints"][key]["kind"] == "code"
				&& tick["breakpoints"][key]["address"] == arm7_addrs[i] && tick["breakpoints"][key]["enabled"] == true;
		}

		Check(added && shown && tick["breakpoints"].size() == 6 && arm7_ids[0] < arm7_ids[1] && arm7_ids[1] < arm7_ids[2],
			"code breakpoints of the ARM7 at multiples of 4 up to 0x00FFFFFF are taken and listed: id, event, address as given, kind, enabled (%s)",
			tick["breakpoints"].dump().c_str());
		Check(handed() == Pairs({ { arm7_ids[0], arm7_addrs[0] }, { arm7_ids[1], arm7_addrs[1] }, { arm7_ids[2], arm7_addrs[2] } }),
			"the hand-over: dbg::arm7 has the three, each id at its address (%zu pairs)", handed().size());

		// neither processor is handed the other's: nothing of the SH4 is armed by the three, and an enabled
		// code breakpoint of the SH4 beside them is armed there and is not in the set of the ARM7
		Check(dbg_sh4_armed == 0, "the hand-over: the breakpoints of the ARM7 arm nothing of the SH4 (%u armed)", dbg_sh4_armed);
		{
			const bool added_sh4 = Ok(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", 0x8C010000u } }));

			Check(added_sh4 && dbg_sh4_armed == 1 && handed() == Pairs({ { arm7_ids[0], arm7_addrs[0] }, { arm7_ids[1], arm7_addrs[1] }, { arm7_ids[2], arm7_addrs[2] } }),
				"the hand-over: a code breakpoint of the SH4 beside them is armed for the SH4 alone (%u armed, %zu pairs)", dbg_sh4_armed, handed().size());

			const bool removed_sh4 = Ok(Send("breakpoints.remove", { { "id", BreakpointIds(Tick(false)).back() } }));

			Check(removed_sh4 && dbg_sh4_armed == 0, "(the breakpoint of the SH4 removed: %u armed)", dbg_sh4_armed);
		}

		Check(Ok(Send("breakpoints.toggle", { { "id", arm7_ids[1] }, { "enabled", false } })) && Tick(false)["breakpoints"][std::to_string(arm7_ids[1])]["enabled"] == false,
			"one of them toggled off: it stays listed, disabled");
		Check(handed() == Pairs({ { arm7_ids[0], arm7_addrs[0] }, { arm7_ids[2], arm7_addrs[2] } }),
			"the hand-over: dbg::arm7 has the two enabled ones with their ids at their addresses, and not the disabled one (%zu pairs)", handed().size());

		Check(Ok(Send("breakpoints.remove", { { "id", arm7_ids[0] } })) && handed() == Pairs({ { arm7_ids[2], arm7_addrs[2] } }),
			"one of the two removed: the hand-over has the other");
		Check(Ok(Send("breakpoints.remove", { { "id", arm7_ids[1] } })) && Ok(Send("breakpoints.remove", { { "id", arm7_ids[2] } })) && handed().empty(),
			"all three removed: dbg::arm7 has none");
	}

	// The DSP is what emu::TargetSteps() says, as the ARM7 is: a target without stepping while its run control does
	// not exist, and one whose code breakpoints the core takes and hands over, each at its step, once it does
	if (!emu::TargetSteps(Target::Dsp))
	{
		Check(InBand(Send("breakpoints.add", { { "event", "dc.aica.dsp.step" }, { "address", 3 } })) == kErrNotSupported,
			"a code breakpoint of a target without stepping, the DSP: -32004, before its address is looked at");
	}
	else
	{
		auto dsp_at = [](const json& step) {
			return json({ { "event", "dc.aica.dsp.step" }, { "address", step } });
		};

		typedef std::vector<std::pair<u32, u32>> Pairs;

		// what a processor was handed last, as { id, address or step } pairs in ascending order
		auto pairs_of = [](u32 (*get)(u32*, u32*, u32)) {
			u32 got_ids[8], got_at[8];
			const u32 n = get(got_ids, got_at, 8);
			Pairs pairs;

			for (u32 i = 0; i < n && i < 8; i++)
				pairs.push_back({ got_ids[i], got_at[i] });

			// more than the test ever lists: no set of the test equals it
			if (n > 8)
				pairs.push_back({ 0, n });

			std::sort(pairs.begin(), pairs.end());
			return pairs;
		};
		auto handed = [&] { return pairs_of(dsp::GetBreakpoints); };
		auto handed_arm7 = [&] { return pairs_of(arm7::GetBreakpoints); };

		Check(InBand(Send("breakpoints.add", { { "event", "dc.aica.dsp.step" }, { "kind", "code" } })) == kErrInvalidParams,
			"the DSP steps: a code breakpoint of it without an address is -32602");
		Check(InBand(Send("breakpoints.add", dsp_at(128))) == kErrInvalidParams, "step 128, above the DSP's addressMax, 127: -32602");
		Check(InBand(Send("breakpoints.add", dsp_at(-1))) == kErrInvalidParams, "step -1: -32602");
		Check(Tick(false)["breakpoints"].size() == 3 && handed().empty(), "the refused requests add nothing, and nothing of the DSP is handed to dbg::dsp");

		// Three of them: the last step, the first and one between. The steps are kept as given
		const u32 dsp_steps[3] = { 127, 0, 64 };
		u32 dsp_ids[3] = { 0, 0, 0 };
		bool added = true, shown = true;

		for (u32 i = 0; i < 3; i++)
		{
			added = added && Ok(Send("breakpoints.add", dsp_at(dsp_steps[i])));
			dsp_ids[i] = BreakpointIds(Tick(false)).back();
		}

		tick = Tick(false);

		for (u32 i = 0; i < 3; i++)
		{
			const std::string key = std::to_string(dsp_ids[i]);

			shown = shown && tick["breakpoints"].contains(key) && tick["breakpoints"][key].size() == 5 && tick["breakpoints"][key]["id"] == dsp_ids[i]
				&& tick["breakpoints"][key]["event"] == "dc.aica.dsp.step" && tick["breakpoints"][key]["kind"] == "code"
				&& tick["breakpoints"][key]["address"] == dsp_steps[i] && tick["breakpoints"][key]["enabled"] == true;
		}

		Check(added && shown && tick["breakpoints"].size() == 6 && dsp_ids[0] < dsp_ids[1] && dsp_ids[1] < dsp_ids[2],
			"code breakpoints of the DSP on the steps 127, 0 and 64 are taken and listed: id, event, the step as given, kind, enabled (%s)", tick["breakpoints"].dump().c_str());
		Check(handed() == Pairs({ { dsp_ids[0], dsp_steps[0] }, { dsp_ids[1], dsp_steps[1] }, { dsp_ids[2], dsp_steps[2] } }),
			"the hand-over: dbg::dsp has the three, each id at its step (%zu pairs)", handed().size());

		// no processor is handed another's: the three arm nothing of the SH4 and are not in the set of the ARM7,
		// and a code breakpoint of the SH4 and one of the ARM7 beside them go to their own processor alone
		Check(dbg_sh4_armed == 0 && handed_arm7().empty(), "the hand-over: the breakpoints of the DSP arm nothing of the SH4 and are not the ARM7's (%u armed, %zu pairs of the ARM7)",
			dbg_sh4_armed, handed_arm7().size());
		{
			const bool added_sh4 = Ok(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", 0x8C010000u } }));
			const u32 sh4_id = BreakpointIds(Tick(false)).back();
			const bool added_arm7 = Ok(Send("breakpoints.add", { { "event", "dc.aica.arm7.pc" }, { "address", 0x40u } }));
			const u32 arm7_id = BreakpointIds(Tick(false)).back();

			Check(added_sh4 && added_arm7 && dbg_sh4_armed == 1 && handed_arm7() == Pairs({ { arm7_id, 0x40u } })
				&& handed() == Pairs({ { dsp_ids[0], dsp_steps[0] }, { dsp_ids[1], dsp_steps[1] }, { dsp_ids[2], dsp_steps[2] } }),
				"the hand-over: a code breakpoint of the SH4 and one of the ARM7 beside them are their processors' alone, and dbg::dsp has its three "
				"(%u armed, %zu pairs of the ARM7, %zu of the DSP)", dbg_sh4_armed, handed_arm7().size(), handed().size());

			const bool removed = Ok(Send("breakpoints.remove", { { "id", sh4_id } })) && Ok(Send("breakpoints.remove", { { "id", arm7_id } }));

			Check(removed && dbg_sh4_armed == 0 && handed_arm7().empty() && handed().size() == 3,
				"(the breakpoints of the SH4 and of the ARM7 removed: %u armed, %zu pairs of the ARM7, %zu of the DSP)", dbg_sh4_armed, handed_arm7().size(), handed().size());
		}

		Check(Ok(Send("breakpoints.toggle", { { "id", dsp_ids[1] }, { "enabled", false } })) && Tick(false)["breakpoints"][std::to_string(dsp_ids[1])]["enabled"] == false,
			"one of them toggled off: it stays listed, disabled");
		Check(handed() == Pairs({ { dsp_ids[0], dsp_steps[0] }, { dsp_ids[2], dsp_steps[2] } }),
			"the hand-over: dbg::dsp has the two enabled ones with their ids at their steps, and not the disabled one (%zu pairs)", handed().size());

		Check(Ok(Send("breakpoints.remove", { { "id", dsp_ids[0] } })) && handed() == Pairs({ { dsp_ids[2], dsp_steps[2] } }),
			"one of the two removed: the hand-over has the other");
		Check(Ok(Send("breakpoints.remove", { { "id", dsp_ids[1] } })) && Ok(Send("breakpoints.remove", { { "id", dsp_ids[2] } })) && handed().empty(),
			"all three removed: dbg::dsp has none");
	}

	Check(InBand(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "kind", "code" } })) == kErrInvalidParams, "a code breakpoint of the SH4 without an address: -32602");
	Check(InBand(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", 0x8C010001u } })) == kErrInvalidParams, "an odd address: -32602");
	Check(InBand(Send("breakpoints.add", { { "event", 5 } })) == kErrInvalidParams && InBand(Send("breakpoints.add", { { "event", "dc.no.such.event" }, { "kind", "other" } })) == kErrInvalidParams
		&& InBand(Send("breakpoints.add", { { "event", "dc.no.such.event" }, { "address", "0" } })) == kErrInvalidParams
		&& InBand(Send("breakpoints.add", { { "event", "dc.no.such.event" }, { "enabled", 1 } })) == kErrInvalidParams
		&& InBand(Send("breakpoints.add", { { "event", "dc.no.such.event" }, { "address", 4294967296.0 } })) == kErrInvalidParams,
		"a mistyped event, kind, address or enabled, and an address above 32 bits: -32602, before the event is looked at");

	{
		// A name is compared whole: what stands in front of a NUL is not the name. Sent as a client sends it, with
		// the escape \u0000 in the JSON text: an event that the tree lists, and the event of a target's code
		// breakpoints, each with a NUL behind it
		const std::string vblank_nul("dc.holly.core.vblank\0x", 22), pc_nul("dc.sh4.cpu.pc\0x", 15);
		const json as_event = Send("breakpoints.add", { { "event", vblank_nul }, { "kind", "event" } });
		const json no_kind = Send("breakpoints.add", { { "event", vblank_nul } });
		const json as_code = Send("breakpoints.add", { { "event", pc_nul }, { "address", 0x8C010000u } });

		Check(InBand(as_event) == kErrInvalidParams && InBand(no_kind) == kErrInvalidParams && InBand(as_code) == kErrInvalidParams,
			"an event name with a NUL behind the name of an event, or of the event of a target's code breakpoints (\"dc.holly.core.vblank\\u0000x\", "
			"\"dc.sh4.cpu.pc\\u0000x\"), is none of them: -32602, with kind event, without kind and as a code breakpoint (%s)", as_event.dump().c_str());
	}

	Check(Tick(false)["breakpoints"].size() == 3, "a refused request adds nothing");

	// the event gates
	{
		events::ClearForTest();

		DBG_EVENT(EV_CORE_VBLANK, "high rate, with a disabled breakpoint");
		DBG_EVENT(EV_TA_LIST_END, "high rate, without one");

		json log = Tick(false)["eventLog"];

		Check(log.size() == 1 && log[0]["message"] == "high rate, with a disabled breakpoint" && log[0]["subsystem"] == "core" && log[0]["metadata"]["event"] == "dc.holly.core.vblank",
			"a high-rate event is logged while an event breakpoint on it exists, enabled or not, and no other high-rate event is (%zu entries)", log.size());

		events::ClearForTest();
	}

	Check(g_event_on[EV_CORE_VBLANK].load() == 1 && g_event_on[EV_TA_LIST_END].load() == 0 && g_event_on[EV_SH4_EXCEPTION].load() == 1 && g_event_on[EV_SYSTEM_STATE].load() == 1,
		"a client: the gate of a low-rate event is open, that of a high-rate event only with an event breakpoint on it, enabled or not");

	core::SetClientCountForTest(0, false);
	Check(g_event_on[EV_CORE_VBLANK].load() == 0 && g_event_on[EV_SH4_EXCEPTION].load() == 0 && g_event_on[EV_DEBUGGER_NOTE].load() == 0, "no client: every gate is closed");
	core::SetClientCountForTest(1, false);

	for (u32 id : BreakpointIds(Tick(false)))
		Send("breakpoints.remove", { { "id", id } });

	Check(g_event_on[EV_CORE_VBLANK].load() == 0 && g_event_on[EV_SH4_EXCEPTION].load() == 1, "the last event breakpoint on a high-rate event removed: its gate is closed");

	// the bound: 1024
	{
		u32 refused = 0;

		for (u32 i = 0; i < kMaxBreakpoints; i++)
		{
			if (!Ok(Send("breakpoints.add", vblank)))
				refused++;
		}

		tick = Tick(false);
		ids = BreakpointIds(tick);

		Check(refused == 0 && ids.size() == kMaxBreakpoints, "the table takes %u breakpoints (%zu listed, %u refused)", kMaxBreakpoints, ids.size(), refused);
		Check(InBand(Send("breakpoints.add", vblank)) == kErrInvalidParams && InBand(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", 0x8C010000u } })) == kErrInvalidParams,
			"and refuses the next one, of either kind: -32602, in-band");

		u32 highest = 0;

		for (u32 id : ids)
			highest = id > highest ? id : highest;

		Check(Ok(Send("breakpoints.remove", { { "id", ids[5] } })) && Ok(Send("breakpoints.add", vblank)) && Tick(false)["breakpoints"].contains(std::to_string(highest + 1)),
			"one removed, one added: its id is a new one (%u)", highest + 1);

		for (u32 id : BreakpointIds(Tick(false)))
			Send("breakpoints.remove", { { "id", id } });

		Check(Tick(false)["breakpoints"].empty(), "all removed");
	}

	/*
		Watches
	*/
	Check(Ok(Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.PC" }) } })), "a watch on dc.sh4.cpu.PC");
	tick = Tick(false);

	const u32 w0 = tick["watches"].size() == 1 ? tick["watches"][0].value("id", 0u) : 0;

	Check(tick["watches"].size() == 1 && tick["watches"][0].size() == 3 && tick["watches"][0]["expression"] == "dc.sh4.cpu.pc" && (!first_user || w0 == 1),
		"it is listed with an id, its expression in lower case and a value; ids start at 1 (%s)", tick["watches"].dump().c_str());
	Check(tick["watches"].size() == 1 && tick["watches"][0]["value"] == Reg(tick, "dc.sh4.cpu", "PC") && WireValue(Str(tick["watches"][0]["value"]), 32),
		"its value is the register's value of the same tick");

	// an upper-case path resolves; an expression that is stored, or twice in a call, is not added again
	Check(Ok(Send("state.watch", { { "expressions", json::array({ "DC.SH4.CPU.pc", "dc.sh4.cpu.Pc", "DC.TIMING.SH4_CYCLES", "dc.timing.sh4_cycles" }) } })),
		"the same register in other cases, and another one twice in one call");
	tick = Tick(false);
	Check(tick["watches"].size() == 2 && tick["watches"][1]["id"] == w0 + 1 && tick["watches"][1]["expression"] == "dc.timing.sh4_cycles",
		"one watch more: the path and the name are matched without regard to case, what is stored is not added again, a duplicate inside the call is added once (%s)",
		tick["watches"].dump().c_str());

	// all or nothing
	Check(InBand(Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.pr", "dc.sh4.cpu.nosuch" }) } })) == kErrInvalidParams && Tick(false)["watches"].size() == 2,
		"one expression of a call does not resolve: -32602, and none of the call is added");
	Check(InBand(Send("state.watch", { { "expressions", json::array({ "pc" }) } })) == kErrInvalidParams && InBand(Send("state.watch", { { "expressions", json::array({ "dc.nosuch.pc" }) } })) == kErrInvalidParams
		&& InBand(Send("state.watch", { { "expressions", json::array({ " dc.sh4.cpu.pc" }) } })) == kErrInvalidParams && InBand(Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu." }) } })) == kErrInvalidParams,
		"no dot, an unknown path, a blank in front (nothing is trimmed), no name: -32602");
	Check(InBand(Send("state.watch", { { "expressions", "dc.sh4.cpu.pc" } })) == kErrInvalidParams && InBand(Send("state.watch", { { "expressions", json::array({ 5 }) } })) == kErrInvalidParams
		&& InBand(Send("state.watch")) == kErrInvalidParams, "expressions that is no array of strings, or missing: -32602");
	Check(InBand(Send("state.watch", { { "expressions", json::array({ std::string(kMaxWatchExpressionChars + 1, 'a') }) } })) == kErrInvalidParams, "an expression of 257 characters: -32602");

	{
		json many = json::array();

		for (u32 i = 0; i < kMaxWatchExpressionsPerCall + 1; i++)
			many.push_back("dc.sh4.cpu.pc");

		Check(InBand(Send("state.watch", { { "expressions", many } })) == kErrInvalidParams, "65 expressions in one call: -32602");

		many.erase(many.begin());
		Check(Ok(Send("state.watch", { { "expressions", many } })) && Tick(false)["watches"].size() == 2, "64 are taken (all of them stored already: nothing is added)");
	}

	// modify: the id is kept, another watch's expression is allowed
	Check(Ok(Send("state.modifyWatchExpression", { { "watchId", w0 }, { "newExpression", "DC.SH4.CPU.PR" } })), "the first watch becomes dc.sh4.cpu.PR");
	tick = Tick(false);
	Check(tick["watches"][0]["id"] == w0 && tick["watches"][0]["expression"] == "dc.sh4.cpu.pr" && tick["watches"][0]["value"] == Reg(tick, "dc.sh4.cpu", "PR"),
		"same id, the new expression in lower case, the value of PR");
	Check(Ok(Send("state.modifyWatchExpression", { { "watchId", w0 }, { "newExpression", "dc.timing.sh4_cycles" } })) && Tick(false)["watches"][0]["expression"] == Tick(false)["watches"][1]["expression"],
		"a watch may be given the expression another watch has");
	Check(InBand(Send("state.modifyWatchExpression", { { "watchId", 99999 }, { "newExpression", "dc.sh4.cpu.pc" } })) == kErrInvalidParams, "an unknown watch id: -32602");
	Check(InBand(Send("state.modifyWatchExpression", { { "watchId", w0 }, { "newExpression", "dc.sh4.cpu.nosuch" } })) == kErrInvalidParams
		&& Tick(false)["watches"][0]["expression"] == "dc.timing.sh4_cycles", "an expression that does not resolve: -32602, and the watch is as it was");

	// unwatch: ids nobody has are not an error; ids are not used twice
	Check(Ok(Send("state.unwatch", { { "watchIds", json::array({ w0, 99999 }) } })) && Tick(false)["watches"].size() == 1 && Tick(false)["watches"][0]["id"] == w0 + 1, "unwatch, with an id that no watch has");
	Check(Ok(Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.pc" }) } })) && Tick(false)["watches"][1]["id"] == w0 + 2, "the next watch has a new id: ids are not used twice");
	Check(InBand(Send("state.unwatch", { { "watchIds", json::array({ "1" }) } })) == kErrInvalidParams && InBand(Send("state.unwatch", { { "watchIds", 1 } })) == kErrInvalidParams, "watchIds that are no numbers: -32602");

	// Every register of the tree, 64 expressions a call: the bound of 256 watches counts what a call would add, and
	// the call that would pass it is refused whole. The tree has more registers than the bound: the bound is reached
	{
		u32 count, registers = 0, calls = 0, refused = 0, wrong = 0;
		const regs::Node* nodes = regs::Nodes(&count);
		json batch = json::array();
		std::set<std::string> stored;
		std::string first_wrong;

		// what is stored when the calls begin
		const json listed_first = Tick(false);

		for (const json& w : listed_first["watches"])
			stored.insert(w.value("expression", ""));

		auto flush = [&] {
			if (batch.empty())
				return;

			calls++;

			// what the call would add: an expression that is stored already is not added again
			std::set<std::string> added;

			for (const json& expression : batch)
			{
				std::string lower = expression.get<std::string>();

				for (char& ch : lower)
					ch = (char)tolower((unsigned char)ch);

				if (stored.count(lower) == 0)
					added.insert(lower);
			}

			const bool fits = stored.size() + added.size() <= kMaxWatches;
			const json answer = Send("state.watch", { { "expressions", batch } });
			const size_t listed = Tick(false)["watches"].size();

			if (fits)
				stored.insert(added.begin(), added.end());
			else
				refused++;

			// taken whole, or refused whole with -32602: the list has what the rule gives, whichever it was
			if ((fits ? !Ok(answer) : InBand(answer) != kErrInvalidParams) || listed != stored.size())
			{
				if (wrong++ == 0)
					first_wrong = "call " + std::to_string(calls) + " of " + std::to_string(batch.size()) + " expressions, " + std::to_string(added.size())
						+ " of them new: " + answer.dump() + ", " + std::to_string(listed) + " watches listed, expected " + std::to_string(stored.size());
			}

			batch = json::array();
		};

		for (u32 i = 0; i < count; i++)
		{
			for (u32 r = 0; r < nodes[i].registerCount; r++)
			{
				std::string expression = std::string(nodes[i].path) + "." + nodes[i].registers[r].name;

				registers++;
				batch.push_back(expression);

				if (batch.size() == kMaxWatchExpressionsPerCall)
					flush();
			}
		}

		flush();
		tick = Tick(true);

		Check(wrong == 0 && tick["watches"].size() == stored.size(), "a watch on each of the %u registers of the tree was asked for, in %u calls: a call that fits is taken "
			"whole, one that would make more than %u watches is refused whole with -32602 and adds nothing (%u calls were not as the rule has them; %s)", registers, calls,
			kMaxWatches, wrong, wrong ? first_wrong.c_str() : "none");
		Check(registers > kMaxWatches && refused != 0 && tick["watches"].size() <= kMaxWatches && tick["watches"].size() + kMaxWatchExpressionsPerCall > kMaxWatches,
			"the tree has %u registers, more than the bound: %u of the calls were refused, and %zu watches are listed, which is within a call of the %u", registers, refused,
			tick["watches"].size(), kMaxWatches);

		// in id order, and each one reads what its register reads
		bool ordered = true, equal = true;
		std::string differs;

		for (size_t i = 0; i < tick["watches"].size(); i++)
		{
			json& w = tick["watches"][i];
			const std::string expression = w.value("expression", "");
			const size_t dot = expression.rfind('.');

			if (i != 0 && w.value("id", 0u) <= tick["watches"][i - 1].value("id", 0u))
				ordered = false;

			const std::string path = expression.substr(0, dot);
			std::string name = expression.substr(dot + 1);

			for (char& ch : name)
				ch = (char)toupper((unsigned char)ch);

			if (w["value"] != Reg(tick, path.c_str(), name.c_str()))
			{
				equal = false;
				differs = expression;
			}
		}

		Check(ordered, "the watches of a tick are in id order");
		Check(equal, "every watch reads what its register reads in the same tick (%s)", differs.c_str());
	}

	CleanTables();
	tick = Tick(false);
	Check(tick["watches"].empty() && tick["breakpoints"].empty(), "(the tables are empty for the tests behind this one)");
}

/*
	core.tick: the JSON of a tick and of describe
*/
namespace {

// The registers of the device tree as describe has them: path -> (name, width)
void CollectNodes(const json& nodes, const std::string& parent, std::vector<std::pair<std::string, json>>* out, std::set<std::string>* problems)
{
	for (const json& node : nodes)
	{
		const std::string path = node.value("path", "");

		for (const char* key : { "path", "label", "description" })
		{
			if (!node.contains(key) || !node[key].is_string())
				problems->insert(path + " has no " + key);
		}

		for (const char* key : { "registers", "events", "actions", "children" })
		{
			if (node.contains(key) && (!node[key].is_array() || node[key].empty()))
				problems->insert(path + " has an empty " + key);
		}

		for (auto it = node.begin(); it != node.end(); ++it)
		{
			static const std::set<std::string> known = { "path", "label", "description", "registers", "events", "actions", "children" };

			if (known.count(it.key()) == 0)
				problems->insert(path + " has the key " + it.key());
		}

		(void)parent;
		out->push_back({ path, node.contains("registers") ? node["registers"] : json::array() });

		if (node.contains("children"))
			CollectNodes(node["children"], path, out, problems);
	}
}

}

DBG_SELFTEST(core_tick, "core.tick")
{
	if (!Alone("core.tick"))
		return;

	ids_used = true;

	CleanTables();
	events::ClearForTest();
	core::SetClientCountForTest(1, false);

	/*
		describe
	*/
	const std::string describe_text = core::DescribeText();
	json shape = json::parse(describe_text, nullptr, false);

	Check(shape.is_object() && Ascii(describe_text) && !HasNull(shape), "the describe result is JSON, printable ASCII and has no null");

	std::vector<std::pair<std::string, json>> nodes;
	std::set<std::string> problems;

	if (shape.is_object() && shape.contains("deviceTree"))
		CollectNodes(shape["deviceTree"], "", &nodes, &problems);

	Check(shape.size() == 6 && shape["emulator"] == json({ { "name", "nullDC" }, { "version", "3.0.0" }, { "build", "native" } }) && shape["protocol"] == 3
		&& shape["ui"].is_string() && shape["capabilities"].is_array() && shape["targets"].is_array() && shape["deviceTree"].is_array(),
		"it has emulator, protocol 3, ui, capabilities, targets and deviceTree, and nothing else");

	{
		u32 count, with_registers = 0, total = 0;
		const regs::Node* table = regs::Nodes(&count);

		for (u32 i = 0; i < count; i++)
		{
			with_registers += table[i].registerCount != 0;
			total += table[i].registerCount;
		}

		Check(problems.empty() && nodes.size() == count, "the tree has the %u nodes of the tables, each with path, label and description and without an empty array (%zu nodes; %s)",
			count, nodes.size(), problems.empty() ? "no problem" : problems.begin()->c_str());
		Note("%u nodes, %u of them with registers, %u registers", count, with_registers, total);
	}

	{
		std::set<std::string> caps;

		for (const json& c : shape["capabilities"])
			caps.insert(c.get<std::string>());

		emu::Sh4EngineInfo engine;
		const bool known = emu::GetSh4Engine(&engine);

		Check(caps.count("event-breakpoints") == 1 && caps.count("callstack") == 1 && caps.count("memory-write") == 1 && caps.count("register-write") == 1
			&& caps.count("reset") == 1 && caps.count("sgc-capture") == 1 && caps.count("memory-binary") == 1 && known
			&& caps.count(engine.fallback ? "sh4-debug-fallback" : "sh4-debug-native") == 1 && caps.size() == 8 && shape["capabilities"].size() == 8,
			"the capabilities of M1, M2 and M3, seven, and the one of the engine that is selected (%s)", shape["capabilities"].dump().c_str());

		json& targets = shape["targets"];

		Check(targets.size() == 3 && targets[0]["id"] == "sh4" && targets[0]["stepping"] == true && targets[0]["memory"] == true && targets[0]["addressMax"] == 0xFFFFFFFFu
			&& targets[1]["id"] == "arm7" && targets[1]["stepping"] == emu::TargetSteps(Target::Arm7) && targets[1]["addressMax"] == 0x00FFFFFFu && targets[1]["instructionSize"] == 4
			&& targets[2]["id"] == "dsp" && targets[2]["stepping"] == emu::TargetSteps(Target::Dsp) && targets[2]["memory"] == false && !targets[2].contains("defaultMemoryAddress")
			&& targets[2]["addressRadix"] == 10 && targets[2]["addressDigits"] == 3 && targets[2]["addressMax"] == 127 && targets[0].size() == 13 && targets[2].size() == 12,
			"three targets: the SH4 steps, the ARM7 and the DSP as emu::TargetSteps() has them (%s, %s), the DSP has no memory and no defaultMemoryAddress",
			emu::TargetSteps(Target::Arm7) ? "it steps" : "not yet", emu::TargetSteps(Target::Dsp) ? "it steps" : "not yet");
	}

	/*
		A full tick and a running one. The machine test stands in a running machine: both say "running"
	*/
	std::string full_text, base_text;
	json full = Tick(true, &full_text);
	json base = Tick(false, &base_text);

	static const char* const keys[] = { "tickId", "timestamp", "executionState", "registers", "breakpoints", "eventLog", "watches", "callstacks", "categories" };
	bool has_keys = full.size() == 9 && base.size() == 9;

	for (const char* key : keys)
		has_keys = has_keys && full.contains(key) && base.contains(key);

	Check(has_keys, "a tick has its nine keys and no other");
	Check(Ascii(full_text) && Ascii(base_text) && !HasNull(full) && !HasNull(base), "a tick is printable ASCII and has no null");
	Check(base["tickId"].get<u64>() == full["tickId"].get<u64>() + 1, "tickId advances with every tick that is built (%llu, %llu)", (unsigned long long)full["tickId"].get<u64>(),
		(unsigned long long)base["tickId"].get<u64>());
	Check(full["executionState"] == json({ { "state", "running" }, { "stopId", emu::GetRunState().stopId } }),
		"a running tick has state and stopId, and neither reason nor breakpointId (%s)", full["executionState"].dump().c_str());
	Check(full["callstacks"] == json({ { "sh4", json::array() }, { "arm7", json::array() } }) && base["callstacks"] == full["callstacks"], "a running tick has both call stacks, empty");
	Check(full["categories"].size() == 4 && full["categories"].contains("events") && full["categories"].contains("sh4") && full["categories"].contains("arm7")
		&& full["categories"].contains("dsp"), "categories has its four keys");

	// the registers: every node that has some, with the names and widths of describe, in its order
	{
		u32 carried = 0, expected = 0;
		std::string wrong;

		for (const auto& node : nodes)
		{
			if (node.second.empty())
			{
				if (full["registers"].contains(node.first))
					wrong = node.first + " has no registers and is carried";
				continue;
			}

			expected++;

			if (!full["registers"].contains(node.first))
			{
				wrong = node.first + " is missing";
				continue;
			}

			carried++;

			json& list = full["registers"][node.first];

			if (list.size() != node.second.size())
			{
				wrong = node.first + " has another number of registers";
				continue;
			}

			for (size_t i = 0; i < list.size(); i++)
			{
				if (list[i].size() != 3 || list[i]["name"] != node.second[i]["name"] || list[i]["width"] != node.second[i]["width"])
					wrong = node.first + "." + node.second[i].value("name", "") + " is not the register of describe";
				else if (!list[i]["value"].is_string() || !WireValue(Str(list[i]["value"]), list[i]["width"].get<u32>()))
					wrong = node.first + "." + node.second[i].value("name", "") + " has the value " + list[i]["value"].dump();
				else if (!WireValue(node.second[i].value("value", ""), node.second[i]["width"].get<u32>()))
					wrong = node.first + "." + node.second[i].value("name", "") + " has the value " + node.second[i]["value"].dump() + " in describe";
			}
		}

		Check(wrong.empty() && carried == expected && full["registers"].size() == expected,
			"a full tick carries every node that has registers (%u), each with exactly the names and widths of describe in its order, the values in their wire form (%s)",
			expected, wrong.empty() ? "all" : wrong.c_str());
	}

	Check(base["registers"].size() == 4 && base["registers"].contains("dc.sh4.cpu") && base["registers"].contains("dc.aica.arm7") && base["registers"].contains("dc.aica.dsp")
		&& base["registers"].contains("dc.timing") && !base["executionState"].contains("registersStale"),
		"a running tick that is not full carries the four base paths (%zu paths)", base["registers"].size());
	Check(base["registers"]["dc.sh4.cpu"] == full["registers"]["dc.sh4.cpu"] && Reg(base, "dc.sh4.cpu", "PC") == "0xA0000000" && Reg(base, "dc.aica.arm7", "ENABLED") == "0x0",
		"with the registers a full tick has for them: the SH4 at the reset vector, the ARM7 held in reset");

	// watches are evaluated whatever paths the tick carries
	Check(Ok(Send("state.watch", { { "expressions", json::array({ "dc.holly.intc.sb_istnrm", "dc.sh4.cpu.pc", "dc.sh4.bsc.pdtra", "dc.timing.mode" }) } })), "four watches");

	{
		json part = Tick(false), whole = Tick(true);

		Check(part["watches"].size() == 4 && !part["registers"].contains("dc.holly.intc") && part["watches"][0]["value"] == Reg(whole, "dc.holly.intc", "SB_ISTNRM")
			&& WireValue(Str(part["watches"][0]["value"]), 32) && part["watches"][0]["value"] != "n/a",
			"a watch on a path the tick does not carry shows the value the register has (%s)", part["watches"].dump().c_str());
		Check(part["watches"][1]["value"] == Reg(part, "dc.sh4.cpu", "PC") && part["watches"][2]["value"] == "n/a" && part["watches"][3]["value"] == "fast",
			"a watch reads what its register reads: a value, \"n/a\" for a register without a reader, the text of a text register");
		Check(part["watches"] == whole["watches"], "and the same in a full tick");
	}

	Check(Ok(Send("state.unwatch", { { "watchIds", json::array({ Tick(false)["watches"][0].value("id", 0u) }) } })), "(one watch removed)");

	/*
		Strings of guest and host origin: a symbol name and an event message with bytes that are no ASCII
	*/
	const std::string odd = std::string("odd_") + "\xC3\xA9\xFF\x01\x7F" + "_fn";		// five bytes outside 0x20..0x7E
	const std::string odd_wire = "odd_?????_fn";
	const std::vector<u8> image = BuildElf({ { odd, kScratchAddr + 0x100, 0x20 }, { "plain", kScratchAddr + 0x200, 0x10 } });

	symbols::OnElfLoaded(image.data(), image.size());

	Check(symbols::Count() == 2, "(the fixture image has two symbols: %zu)", symbols::Count());

	// bsr to the symbol, and its delay slot
	u8* at = GetMemPtr(kScratchAddr, 4);
	u8 kept[4];
	const u16 bsr = (u16)(0xB000 | ((0x100 - 4) / 2));

	memcpy(kept, at, 4);
	at[0] = (u8)bsr;
	at[1] = (u8)(bsr >> 8);
	at[2] = 0x09;
	at[3] = 0x00;

	const u32 kept_pr = p_sh4rcb->cntx._pr;

	p_sh4rcb->cntx._pr = kScratchAddr + 0x104;

	DBG_EVENT(EV_DEBUGGER_NOTE, "bytes %s end", "\xC3\xA9 \xFF \x01 \x7F");

	const std::string disasm_text = core::HandleRequestText(R"({"jsonrpc":"2.0","id":1,"method":"state.getDisassembly","params":{"address":2348871680,"count":1}})");
	const std::string stack_text = core::HandleRequestText(R"({"jsonrpc":"2.0","id":2,"method":"state.getCallstack","params":{"target":"sh4","maxFrames":2}})");
	std::string tick_text;
	json tick = Tick(true, &tick_text);

	memcpy(at, kept, 4);
	p_sh4rcb->cntx._pr = kept_pr;
	symbols::OnElfLoaded(nullptr, 0);

	json disasm = json::parse(disasm_text, nullptr, false), stack = json::parse(stack_text, nullptr, false);
	const bool disasm_ok = disasm.is_object() && disasm.contains("result") && disasm["result"]["lines"].size() == 1;
	const bool stack_ok = stack.is_object() && stack.contains("result") && stack["result"]["frames"].size() == 2;

	Check(kScratchAddr == 2348871680u, "(the address in the request text is the scratch address)");
	Check(disasm_ok && Ascii(disasm_text) && disasm["result"]["lines"][0]["disassembly"] == "bsr     0x8c00f100 <" + odd_wire + ">",
		"state.getDisassembly: a symbol name with bytes that are no printable ASCII has a '?' for each of them (%s)", disasm_text.c_str());
	Check(stack_ok && Ascii(stack_text) && stack["result"]["frames"][1]["symbol"] == odd_wire + "+0x4" && stack["result"]["frames"][1]["location"] == "pr"
		&& stack["result"]["frames"][1]["pc"] == kScratchAddr + 0x104 && stack["result"]["target"] == "sh4",
		"state.getCallstack: the same in the symbol of a frame, with its offset in lower-case hex (%s)", stack_text.c_str());
	Check(stack_ok && stack["result"]["frames"][0]["index"] == 0 && stack["result"]["frames"][0]["pc"] == kResetVector && !stack["result"]["frames"][0].contains("location")
		&& stack["result"]["frames"][0].contains("sp") && stack["result"]["frames"][0]["sp"] == stack["result"]["frames"][1]["sp"],
		"frame 0 is the pc and has no location; every frame has the stack pointer of the processor");

	json& log = tick["eventLog"];

	Check(Ascii(tick_text) && log.size() == 1 && log[0]["message"] == "bytes ?? ? ? ? end", "the tick: an event message with such bytes has a '?' for each of them (%s)",
		log.empty() ? "no entry" : log[0]["message"].dump().c_str());

	// success is {}; a notification gets no answer; what is no request is answered with its error and id
	Check(core::HandleRequestText(R"({"jsonrpc":"2.0","id":7,"method":"state.unwatch","params":{"watchIds":[]}})") == R"({"jsonrpc":"2.0","id":7,"result":{}})",
		"the success result of a method whose result type is RpcError is {}, and a response has jsonrpc, id and result");
	Check(core::HandleRequestText(R"({"jsonrpc":"2.0","method":"state.unwatch","params":{"watchIds":[]}})").empty(), "a well-formed message without an id is a notification: no answer");
	Check(core::HandleRequestText("{") == R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"parse error"}})", "text that is no JSON: -32700 with id null");
	Check(core::HandleRequestText(R"([{"jsonrpc":"2.0","id":1,"method":"x"}])") == R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"invalid request"}})", "a batch: -32600 with id null");
	Check(core::HandleRequestText(R"({"id":5,"method":"control.pause"})") == R"({"jsonrpc":"2.0","id":5,"error":{"code":-32600,"message":"invalid request"}})", "no jsonrpc member: -32600 with the id");
	Check(core::HandleRequestText(R"({"jsonrpc":"2.0","id":"5","method":"control.pause"})") == R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"invalid request"}})",
		"an id that is a string: -32600 with id null");
	Check(core::HandleRequestText(R"({"jsonrpc":"2.0","id":5,"method":"control.pause","params":[1]})") == R"({"jsonrpc":"2.0","id":5,"error":{"code":-32600,"message":"invalid request"}})",
		"params that are no object: -32600");
	Check(core::HandleRequestText(R"({"jsonrpc":"2.0","id":5})") == R"({"jsonrpc":"2.0","id":5,"error":{"code":-32600,"message":"invalid request"}})", "no method: -32600");
	Check(RpcError(Send("no.such.method")) == kErrMethodNotFound && RpcError(Send("event.tick")) == kErrMethodNotFound, "a method the protocol does not have: -32601");
	Check(RpcError(Send("state.getTable", { { "id", 5 } })) == kErrNotSupported && RpcError(Send("state.captureHolly", { { "what", 1 } })) == kErrNotSupported
		&& RpcError(Send("state.getTable")) == kErrNotSupported, "a method of a later milestone: -32004 on the channel of the method, whatever its parameters are");

	// The methods of milestone M3, which no longer answer -32004, by what this machine test may ask of them: no
	// capture is armed here (that is the session sgc's, dbg_selftest_sgc.cpp)
	Check(RpcError(Send("state.getMemoryBinary")) == kErrInvalidParams && RpcError(Send("state.getMemoryBinary", { { "address", 0x8C010000u }, { "length", 0 } })) == kErrInvalidParams
		&& RpcError(Send("state.getMemoryBinary", { { "address", 0x8C010000u }, { "length", kMaxMemoryBinaryBytes + 1 } })) == kErrInvalidParams
		&& RpcError(Send("state.getMemoryBinary", { { "target", "dsp" }, { "address", 0 }, { "length", 16 } })) == kErrNotSupported,
		"state.getMemoryBinary: -32602 without an address and outside 1..16777216, -32004 for the target dsp, as JSON-RPC errors");
	Check(Send("state.getMemoryBinary", { { "address", 0x8C010000u }, { "length", 16 } }).contains("result"), "state.getMemoryBinary of 16 bytes has a result");
	Check(RpcError(Send("state.getSgcFrameData")) == kErrNoCapture, "state.getSgcFrameData without a capture: -32011, a JSON-RPC error");
	Check(InBand(Send("state.recordSgcFrames", { { "frames", "x" } })) == kErrInvalidParams && InBand(Send("state.recordSgcFrames", { { "frames", 0 } })) == kErrInvalidParams
		&& InBand(Send("state.recordSgcFrames", { { "frames", kMaxSgcFrames + 1 } })) == kErrInvalidParams,
		"state.recordSgcFrames: -32602 for frames that are no number or outside 1..4096, in-band");
	Check(Ok(json::parse(core::HandleRequestText(R"({"jsonrpc":"2.0","id":9,"method":"state.unwatch","more":1,"params":{"watchIds":[],"other":{"a":null}}})"), nullptr, false)),
		"a member of the request and a key of params that the protocol does not name are not looked at");
	Check(RpcError(Send("state.getMemorySlice", { { "address", "0" }, { "length", 4 } })) == kErrInvalidParams && InBand(Send("control.step", { { "target", 4 } })) == kErrInvalidParams
		&& InBand(Send("control.pause", { { "target", "other" } })) == kErrInvalidParams && RpcError(Send("state.getCallstack", { { "target", "dsp" } })) == kErrInvalidParams,
		"a mistyped parameter: -32602, on the channel of the method");

	// memory: a slice does not wrap, and the DSP has none
	{
		// Across the end of the address space. Nothing can be read in the last 16 bytes of it, and the boot ROM
		// can, at address 0: a slice that wrapped would have 16 readable bytes
		u8 rom[16];
		json top = Send("state.getMemorySlice", { { "target", "sh4" }, { "address", 0xFFFFFFF0u }, { "length", 32 } });
		json flash = Send("state.getMemorySlice", { { "target", "sh4" }, { "address", 0x0021FFF0u }, { "length", 32 } });

		Check(mem::Read(Target::Sh4, 0, 16, rom, nullptr) == 16 && RpcError(top) == kErrNotReadable,
			"a slice does not wrap: 32 bytes from FFFFFFF0 are no bytes of the boot ROM, nothing of them is readable, -32010 (%.120s)", top.dump().c_str());
		Check(flash.contains("result") && flash["result"]["data"].size() == 32 && flash["result"]["baseAddress"] == 0x0021FFF0u && flash["result"]["validity"] == "ok"
			&& flash["result"]["unreadable"] == json::array({ json::array({ 16, 16 }) }) && flash["result"].size() == 4,
			"a slice across the end of the flash memory has its length, and the run that is not readable is listed (%s)",
			flash.contains("result") ? flash["result"]["unreadable"].dump().c_str() : flash.dump().c_str());

		json whole = Send("state.getMemorySlice", { { "address", kScratchAddr }, { "length", 8 } });

		Check(whole.contains("result") && whole["result"].size() == 3 && !whole["result"].contains("unreadable") && whole["result"]["data"].size() == 8,
			"a slice that is readable whole has no unreadable member");
		Check(RpcError(Send("state.getMemorySlice", { { "target", "dsp" }, { "address", 0 }, { "length", 4 } })) == kErrNotSupported, "a slice of the DSP: -32004");
		Check(RpcError(Send("state.getMemorySlice", { { "address", 0x10000000u }, { "length", 16 } })) == kErrNotReadable, "nothing readable in the range: -32010");
		Check(RpcError(Send("state.getMemorySlice", { { "target", "arm7" }, { "address", 0x01000000u }, { "length", 16 } })) == kErrNotReadable,
			"above the last address of the ARM7: -32010");
		Check(RpcError(Send("state.getMemorySlice", { { "address", 0 }, { "length", 0 } })) == kErrInvalidParams
			&& RpcError(Send("state.getMemorySlice", { { "address", 0 }, { "length", kMaxMemorySliceBytes + 1 } })) == kErrInvalidParams, "a length of 0 and of 65537: -32602");

		json dsp = Send("state.getDisassembly", { { "target", "dsp" }, { "address", 126 }, { "count", 8 } });

		Check(dsp.contains("result") && dsp["result"]["lines"].size() == 2 && dsp["result"]["lines"][1]["address"] == 127 && Str(dsp["result"]["lines"][0]["bytes"]).size() == 23,
			"disassembly of the DSP from step 126: two lines, no wrap, eight pairs of hex digits each (%s)", dsp.dump().c_str());
		Check(RpcError(Send("state.getDisassembly", { { "target", "dsp" }, { "address", 128 }, { "count", 1 } })) == kErrInvalidParams
			&& RpcError(Send("state.getDisassembly", { { "address", 0x8C010001u }, { "count", 1 } })) == kErrInvalidParams
			&& RpcError(Send("state.getDisassembly", { { "target", "arm7" }, { "address", 0x00000002 }, { "count", 1 } })) == kErrInvalidParams,
			"an address above the last one and a misaligned address: -32602");
	}

	// run control without a stop in force
	Check(InBand(Send("control.step", { { "target", "sh4" } })) == kErrNotPaused && InBand(Send("control.step", { { "target", "arm7" } })) == kErrNotPaused
		&& InBand(Send("control.stepOut", { { "target", "dsp" } })) == kErrNotPaused, "a step for a running target: -32003, whatever the target can do");
	Check(Ok(Send("control.runUntil")), "control.runUntil for a running target: {}, nothing changes");

	CleanTables();
	events::ClearForTest();
}

/*
	core.guest: the booted ELF
*/
namespace {

struct {
	int phase;
	bool reset_tried;
	u32 stop0;
	u32 hot;				// the address of the hot symbol
	u32 code_id, event_id;
	int tries;
} gs;

std::vector<u32> NewIds(const json& tick, const std::vector<u32>& before)
{
	std::vector<u32> fresh;

	for (u32 id : BreakpointIds(tick))
	{
		bool known = false;

		for (u32 b : before)
			known = known || b == id;

		if (!known)
			fresh.push_back(id);
	}

	return fresh;
}

// The pairing of the ARM7 call stack with the register ENABLED, in one tick and in the answer of the method
void CheckArm7(json tick, bool want_running, const char* where)
{
	const std::string enabled = Reg(tick, "dc.aica.arm7", "ENABLED");
	json& frames = tick["callstacks"]["arm7"];
	json answer = Send("state.getCallstack", { { "target", "arm7" } });
	json none = json::array();
	json& asked = answer.contains("result") ? answer["result"]["frames"] : none;

	if (!want_running)
	{
		Check(enabled == "0x0" && frames.empty() && asked.empty() && answer.contains("result"),
			"%s: the ARM7 is held in reset: ENABLED reads 0x0, callstacks.arm7 is [] and state.getCallstack answers no frames (ENABLED %s, %zu and %zu frames)", where,
			enabled.c_str(), frames.size(), asked.size());
		return;
	}

	const bool two = frames.size() >= 2 && asked.size() >= 2;

	Check(enabled == "0x1" && two, "%s: the ARM7 runs: ENABLED reads 0x1 and its call stack has at least two frames (ENABLED %s, %zu frames in the tick, %zu asked for)", where,
		enabled.c_str(), frames.size(), asked.size());

	if (!two)
		return;

	Check(frames[0]["pc"] == Hex(Reg(tick, "dc.aica.arm7", "PC")) && !frames[0].contains("location") && frames[0]["index"] == 0, "%s: frame 0 is the PC of the ARM7 (%s)", where,
		frames[0].dump().c_str());
	Check(frames[1]["pc"] == Hex(Reg(tick, "dc.aica.arm7", "R14")) && frames[1]["location"] == "lr" && frames[1]["index"] == 1 && frames[1]["sp"] == Hex(Reg(tick, "dc.aica.arm7", "R13")),
		"%s: frame 1 is R14 with the location lr; sp is R13 (%s)", where, frames[1].dump().c_str());
	Check(asked == frames, "%s: state.getCallstack {arm7} answers the frames of the tick", where);
}

void CheckSh4(json tick, const char* where)
{
	json& frames = tick["callstacks"]["sh4"];
	const bool two = frames.size() >= 2;

	Check(two && frames[0]["pc"] == Hex(Reg(tick, "dc.sh4.cpu", "PC")) && frames[0]["index"] == 0 && !frames[0].contains("location")
		&& frames[1]["pc"] == Hex(Reg(tick, "dc.sh4.cpu", "PR")) && frames[1]["location"] == "pr" && frames[0]["sp"] == Hex(Reg(tick, "dc.sh4.cpu", "R15")),
		"%s: the SH4 call stack has frame 0 = PC and frame 1 = PR with the location pr; sp is R15 (%zu frames)", where, frames.size());

	bool later = true;

	for (size_t i = 2; i < frames.size(); i++)
	{
		const std::string location = frames[i].value("location", "");
		char expected[32];

		snprintf(expected, sizeof(expected), "stack+0x%x", (u32)strtoul(location.size() > 8 ? location.c_str() + 8 : "0", nullptr, 16));
		later = later && location == expected && frames[i]["index"] == i && frames[i]["sp"] == frames[0]["sp"];
	}

	Check(later && frames.size() <= kDefaultCallstackFrames, "%s: the frames behind them are stack+0x<offset in lower-case hex>, at most %u frames in a tick", where, kDefaultCallstackFrames);

	json eight = Send("state.getCallstack", { { "target", "sh4" }, { "maxFrames", 1 } });

	Check(eight.contains("result") && eight["result"]["frames"].size() == 1 && eight["result"]["frames"][0] == frames[0], "%s: state.getCallstack {sh4, maxFrames 1} answers frame 0", where);
	Check(RpcError(Send("state.getCallstack", { { "target", "sh4" }, { "maxFrames", kMaxCallstackFrames + 1 } })) == kErrInvalidParams
		&& RpcError(Send("state.getCallstack", { { "target", "sh4" }, { "maxFrames", 0 } })) == kErrInvalidParams, "%s: maxFrames of 0 and of 65: -32602", where);
}

}

BOUNDED_GUEST_TEST(core_guest, "core.guest")
{
	const u32 pc = p_sh4rcb->cntx.pc;

	switch (gs.phase)
	{
	case 0:
	{
		if (!Alone("core.guest"))
			return true;

		ids_used = true;

		// the test begins where the machine does: the trap of reios at the reset vector, nothing booted
		if (pc != kResetVector && !gs.reset_tried)
		{
			gs.reset_tried = true;
			emu::Reset();
			return false;
		}

		CleanTables();
		core::SetClientCountForTest(1, false);

		json tick = Tick(false);

		Check(pc == kResetVector && tick["executionState"]["state"] == "paused" && tick["executionState"]["stopId"] == stop.stopId
			&& tick["executionState"]["reason"] == StopCauseName(stop.cause), "parked at the reset vector, nothing executed: a paused tick with the reason of the stop (%s, pc %08X)",
			tick["executionState"].dump().c_str(), pc);

		{
			u32 count, expected = 0;
			const regs::Node* nodes = regs::Nodes(&count);

			for (u32 i = 0; i < count; i++)
				expected += nodes[i].registerCount != 0;

			Check(tick["registers"].size() == expected && !tick["executionState"].contains("registersStale"), "a paused tick is full, whatever was asked for (%zu of %u paths)",
				tick["registers"].size(), expected);
		}

		Check(symbols::Count() == 0, "no symbols before the boot: reios loads the ELF with its first instruction (%zu)", symbols::Count());
		CheckArm7(tick, false, "before the boot");

		json line = Send("state.getDisassembly", { { "address", kResetVector }, { "count", 1 } });

		Check(line.contains("result") && line["result"]["lines"][0]["disassembly"] == ".word 0x085b ! reios trap", "the word at the reset vector is the trap of reios (%s)",
			line.dump().c_str());

		// a step on it is allowed, and executes the whole HLE call
		gs.stop0 = stop.stopId;
		gs.phase = 1;

		if (!Check(Ok(Send("control.step", { { "target", "sh4" } })), "control.step on the trap of reios is taken"))
		{
			gs.phase = 0;
			CleanTables();
			return true;
		}

		Check(InBand(Send("control.step", { { "target", "sh4" } })) == kErrNotPaused && InBand(Send("control.runUntil")) == kErrNotPaused,
			"run control behind an armed action is refused: -32003, in-band");
		return false;
	}

	case 1:
	{
		json tick = Tick(false);
		u32 entry = 0;

		Check(stop.cause == StopCause::Step && stop.stopId == gs.stop0 + 1 && tick["executionState"]["reason"] == "step" && tick["executionState"]["stopId"] == stop.stopId
			&& !tick["executionState"].contains("breakpointId"), "the step is one stop, reason step, with the next stopId and no breakpointId (%s)", tick["executionState"].dump().c_str());
		Check(pc != kResetVector && mem::Classify(Target::Sh4, pc) == mem::Region::Ram, "the boot ran as one step: the pc is in main memory (%08X)", pc);

		// the symbols of the ELF: the entry resolves
		char symbol[256] = "";
		const bool resolved = symbols::Format(pc, symbol, sizeof(symbol));

		Check(symbols::Count() > 100 && resolved && strchr(symbol, '+') == nullptr, "after the boot the symbol table has the symbols of the ELF (%zu), and the entry is the start of one (%s)",
			symbols::Count(), symbol);
		Check(tick["callstacks"]["sh4"][0].value("symbol", "") == symbol, "frame 0 of the SH4 call stack names it (%s)", tick["callstacks"]["sh4"][0].dump().c_str());
		Check(symbols::Find(symbol, &entry) && entry == pc, "and the name is found at that address");

		CheckSh4(tick, "at the entry");
		CheckArm7(tick, false, "at the entry");

		// on to a function the program calls in every frame: by then it has started the ARM7
		if (!Check(symbols::Find(kHotSymbol, &gs.hot), "the ELF has the symbol %s", kHotSymbol))
		{
			gs.phase = 0;
			CleanTables();
			return true;
		}

		const std::vector<u32> before = BreakpointIds(tick);

		Check(Ok(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", gs.hot }, { "kind", "code" }, { "enabled", true } })), "a code breakpoint on it");

		const std::vector<u32> fresh = NewIds(Tick(false), before);

		gs.code_id = fresh.size() == 1 ? fresh[0] : 0;
		gs.stop0 = stop.stopId;
		gs.phase = 2;

		Check(Ok(Send("control.runUntil")), "resume");
		return false;
	}

	case 2:
	{
		json tick = Tick(false);

		Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == gs.code_id && pc == gs.hot && stop.stopId == gs.stop0 + 1,
			"the breakpoint stops, at its address, with the next stopId (\"%s\", id %u, pc %08X)", StopCauseName(stop.cause), stop.breakpointId, pc);
		Check(tick["executionState"]["reason"] == "breakpoint" && tick["executionState"].value("breakpointId", 0u) == gs.code_id
			&& tick["breakpoints"].contains(std::to_string(gs.code_id)), "the tick of the stop carries reason breakpoint and the breakpointId, which is a key of breakpoints (%s)",
			tick["executionState"].dump().c_str());
		Check(tick["callstacks"]["sh4"][0].value("symbol", "") == kHotSymbol, "frame 0 is the function (%s)", tick["callstacks"]["sh4"][0].dump().c_str());

		CheckSh4(tick, "at the breakpoint");
		CheckArm7(tick, true, "at the breakpoint");

		// a later tick of the same stop repeats it; with the breakpoint removed the id leaves, the reason stays
		Check(Ok(Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.pc" }) } })), "(a watch)");
		tick = Tick(false);
		Check(tick["executionState"]["reason"] == "breakpoint" && tick["executionState"].value("breakpointId", 0u) == gs.code_id && tick["executionState"]["stopId"] == stop.stopId,
			"a later tick of the stop repeats reason, breakpointId and stopId");

		Check(Ok(Send("breakpoints.remove", { { "id", gs.code_id } })), "the breakpoint of the stop is removed");
		tick = Tick(false);
		Check(tick["executionState"]["reason"] == "breakpoint" && !tick["executionState"].contains("breakpointId") && tick["executionState"]["stopId"] == stop.stopId,
			"the ticks of the stop keep the reason and stop carrying the breakpointId (%s)", tick["executionState"].dump().c_str());

		// The category of a breakpoint. An event breakpoint on the vblank and a code breakpoint on the function,
		// one frame apart: with events muted the code breakpoint stops, behind a vblank that did not
		const std::vector<u32> before = BreakpointIds(tick);

		Check(Ok(Send("breakpoints.add", { { "event", "dc.holly.core.vblank" } })), "an event breakpoint on dc.holly.core.vblank, without kind and enabled");

		std::vector<u32> fresh = NewIds(Tick(false), before);

		gs.event_id = fresh.size() == 1 ? fresh[0] : 0;

		Check(Ok(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", gs.hot } })), "and the code breakpoint again");

		fresh = NewIds(Tick(false), before);
		gs.code_id = 0;

		for (u32 id : fresh)
		{
			if (id != gs.event_id)
				gs.code_id = id;
		}

		Check(gs.event_id != 0 && gs.code_id > gs.event_id && Tick(false)["breakpoints"][std::to_string(gs.event_id)] == json({ { "id", gs.event_id },
			{ "event", "dc.holly.core.vblank" }, { "kind", "event" }, { "enabled", true } }), "two new ids; without an address the kind is event, and it is enabled");
		Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "events", { { "muted", true }, { "soloed", false } } } } } })), "events muted");

		gs.stop0 = stop.stopId;
		gs.phase = 3;

		Check(Ok(Send("control.runUntil")), "resume");
		return false;
	}

	case 3:
	{
		json tick = Tick(false);
		u32 vblanks = 0;

		for (const json& entry : tick["eventLog"])
			vblanks += entry["metadata"]["event"] == "dc.holly.core.vblank";

		const bool code_stop = stop.cause == StopCause::Breakpoint && stop.breakpointId == gs.code_id && pc == gs.hot && stop.stopId == gs.stop0 + 1;

		// The program calls the function twice before its first frame is over: on, to a stop that has a vblank
		// behind it
		if (code_stop && vblanks == 0 && gs.tries < 4)
		{
			gs.tries++;
			gs.stop0 = stop.stopId;

			Check(Ok(Send("control.runUntil")), "(no vblank between the last two calls of the function: resume)");
			return false;
		}

		Check(code_stop, "with events muted the code breakpoint stops, at the next call of the function: it is in the category sh4 (\"%s\", id %u)", StopCauseName(stop.cause),
			stop.breakpointId);
		Check(vblanks >= 1, "a vblank was raised on the way, with its enabled event breakpoint muted: the event log has it (%u)", vblanks);

		// the other way round
		Check(Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "events", { { "muted", false }, { "soloed", false } } }, { "sh4", { { "muted", true }, { "soloed", false } } } } } })),
			"events not muted, sh4 muted");

		gs.stop0 = stop.stopId;
		gs.phase = 4;

		Check(Ok(Send("control.runUntil")), "resume");
		return false;
	}

	default:
	{
		json tick = Tick(false);

		Check(stop.cause == StopCause::Event && stop.breakpointId == gs.event_id && stop.stopId == gs.stop0 + 1,
			"with sh4 muted the event breakpoint stops: it is in the category events (\"%s\", id %u)", StopCauseName(stop.cause), stop.breakpointId);
		Check(tick["executionState"]["reason"] == "event" && tick["executionState"].value("breakpointId", 0u) == gs.event_id && tick["executionState"]["state"] == "paused",
			"the tick carries reason event and the id of the event breakpoint (%s)", tick["executionState"].dump().c_str());

		CheckArm7(tick, true, "at the vblank");

		CleanTables();
		events::ClearForTest();

		gs = { };
		return true;
	}
	}
}

/*
	core.lifetime: Stopped, guest-exit, NoMachine, and the machine made again. A thread of the test plays the
	host, and sends what a client sends.
*/
namespace {

std::thread* host;

void HostStart(std::function<void()> body)
{
	verify(host == nullptr);
	host = new std::thread(body);
}

void HostJoin()
{
	if (host != nullptr)
	{
		host->join();
		delete host;
		host = nullptr;
	}
}

// What the host thread found in one state. Texts: it does not check, the test does, on the emulator thread
struct Found {
	bool reached = false;
	emu::RunState state;
	std::string tick, describe;
	std::string add, watch, slice, stack, disasm, run, step, pause, record, binary, frames, later_rpc, remove, unwatch;
};

struct {
	int phase;
	u32 stop0;
	std::atomic<bool> failed { false };
	std::atomic<int> start_rv { 99 };
	Found stopped, exited, none;
	emu::RunState created;
} lt;

u32 lifetime_request = 5000;

std::string Ask(const std::string& method, const json& params = json::object())
{
	json request = { { "jsonrpc", "2.0" }, { "id", lifetime_request++ }, { "method", method }, { "params", params } };

	return core::HandleRequestText(request.dump());
}

// On the host thread, with no emulator thread: what a client is answered, and the tick it would be sent
void Look(Found* found, bool with_tables)
{
	found->reached = true;
	found->state = emu::GetRunState();

	{
		// a tick is built under L where no emulator thread exists
		emu::LifetimeLock lock;

		found->tick = core::BuildTickText(false);
		found->describe = core::DescribeText();
	}

	// the requests come in as on the core thread: through dbg::emu::Call, with no lock held
	if (with_tables)
	{
		found->add = Ask("breakpoints.add", { { "event", "dc.holly.core.vblank" }, { "kind", "event" }, { "enabled", false } });
		found->watch = Ask("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.pc" }) } });
	}

	found->slice = Ask("state.getMemorySlice", { { "address", 0x8C010000u }, { "length", 16 } });
	found->stack = Ask("state.getCallstack", { { "target", "sh4" } });
	found->disasm = Ask("state.getDisassembly", { { "address", 0x8C010000u }, { "count", 4 } });
	found->run = Ask("control.runUntil");
	found->step = Ask("control.step", { { "target", "sh4" } });
	found->pause = Ask("control.pause", { { "target", "sh4" } });
	found->record = Ask("state.recordSgcFrames", { { "frames", 64 } });
	found->binary = Ask("state.getMemoryBinary", { { "address", 0x8C010000u }, { "length", 16 } });
	found->frames = Ask("state.getSgcFrameData");
	found->later_rpc = Ask("state.getTable", { { "id", "sq" } });
}

json Parsed(const std::string& text)
{
	return json::parse(text, nullptr, false);
}

json TickOf(const std::string& text)
{
	json message = Parsed(text);

	return message.is_object() && message.contains("params") ? message["params"] : json::object();
}

}

BOUNDED_GUEST_TEST(core_lifetime, "core.lifetime")
{
	switch (lt.phase)
	{
	case 0:
		if (!Alone("core.lifetime"))
			return true;

		ids_used = true;

		CleanTables();
		core::SetClientCountForTest(1, false);

		lt.stop0 = stop.stopId;
		lt.phase = 1;

		HostStart([] {
			// the host stops the machine: its emulator thread ends
			virtualDreamcast->Stop([] { });

			if (!WaitLifetime(Lifetime::Stopped))
			{
				lt.failed = true;
				return;
			}

			Look(&lt.stopped, true);

			// and starts it again
			Restart();
		});
		return false;

	case 1:
	{
		HostJoin();

		const Found& f = lt.stopped;
		json tick = TickOf(f.tick), shape = Parsed(f.describe);

		Check(!lt.failed && f.reached && f.state.lifetime == Lifetime::Stopped && f.state.cause == StopCause::HostStopped && f.state.stopId == lt.stop0 + 1,
			"the host stopped the machine: Stopped, host-stopped, a new stop (%u behind %u)", f.state.stopId, lt.stop0);
		Check(tick["executionState"] == json({ { "state", "paused" }, { "stopId", f.state.stopId }, { "reason", "host-stopped" } }), "its tick: paused, reason host-stopped (%s)",
			tick["executionState"].dump().c_str());

		{
			u32 count, expected = 0;
			const regs::Node* nodes = regs::Nodes(&count);

			for (u32 i = 0; i < count; i++)
				expected += nodes[i].registerCount != 0;

			Check(tick["registers"].size() == expected && WireValue(Reg(tick, "dc.sh4.cpu", "PC"), 32) && Reg(tick, "dc.sh4.cpu", "PC") != "n/a"
				&& tick["callstacks"]["sh4"].size() >= 2 && tick["callstacks"]["sh4"][0]["pc"] == Hex(Reg(tick, "dc.sh4.cpu", "PC")),
				"it is full, built under L, with the registers and the call stack of the stopped machine (%zu paths, %zu frames)", tick["registers"].size(),
				tick["callstacks"]["sh4"].size());
		}

		Check(shape.is_object() && shape["deviceTree"][0]["registers"][0]["value"] == "200MHz" && shape["capabilities"].size() == 8, "describe: the tree with its values, and the engine capability (%s)",
			shape.is_object() ? shape["capabilities"].dump().c_str() : "no JSON");
		Check(Ok(Parsed(f.add)) && Ok(Parsed(f.watch)), "bookkeeping works: breakpoints.add and state.watch are answered {} (%s, %s)", f.add.c_str(), f.watch.c_str());
		Check(Parsed(f.slice).contains("result") && Parsed(f.stack).contains("result") && Parsed(f.disasm).contains("result")
			&& Parsed(f.stack)["result"]["frames"] == tick["callstacks"]["sh4"], "reads work: memory, disassembly and the call stack of the stopped machine");
		Check(InBand(Parsed(f.run)) == kErrHostStopped && InBand(Parsed(f.step)) == kErrHostStopped && InBand(Parsed(f.pause)) == kErrHostStopped,
			"run control is refused: -32001, in-band (%s)", f.run.c_str());
		Check(RpcError(Parsed(f.later_rpc)) == kErrNotSupported, "a method of a later milestone is -32004 there as well");
		Check(InBand(Parsed(f.record)) == kErrHostStopped && Parsed(f.binary).contains("result") && RpcError(Parsed(f.frames)) == kErrNoCapture,
			"state.recordSgcFrames is refused by the lifetime, -32001 in-band; state.getMemoryBinary is a read, under L; state.getSgcFrameData too: -32011, no "
			"capture (%s, %s)", f.record.c_str(), f.frames.c_str());
		Check(stop.cause == StopCause::Start && stop.stopId == f.state.stopId + 1, "the thread the host starts parks at its start, as every thread of a session");

		{
			json now = Tick(false);

			Check(now["breakpoints"].size() == 1 && now["watches"].size() == 1 && now["watches"][0]["expression"] == "dc.sh4.cpu.pc",
				"the breakpoint and the watch that were added while the machine was stopped are in the tables (%zu, %zu)", now["breakpoints"].size(), now["watches"].size());
		}

		lt.stop0 = stop.stopId;
		lt.phase = 2;

		HostStart([] {
			const std::string image = cfgLoadStr("config", "image", "");

			// the guest has exited: the thread ends by itself
			if (!WaitLifetime(Lifetime::Stopped))
			{
				lt.failed = true;
				return;
			}

			Look(&lt.exited, false);

			// the host takes the machine apart, and has it until the new one has started (the watch of the bound)
			std::lock_guard<std::timed_mutex> owns(bound.restart);

			virtualDreamcast.reset();

			Look(&lt.none, false);

			lt.none.remove = Ask("breakpoints.remove", { { "id", 99999 } });
			lt.none.add = Ask("breakpoints.add", { { "event", "dc.gdrom.command" }, { "kind", "event" }, { "enabled", false } });
			lt.none.watch = Ask("state.watch", { { "expressions", json::array({ "dc.timing.sh4_cycles" }) } });
			lt.none.unwatch = Ask("state.unwatch", { { "watchIds", json::array({ 99999 }) } });

			{
				emu::LifetimeLock lock;

				// with the tables as they are now
				lt.none.tick = core::BuildTickText(true);
			}

			// and makes it again, with the program it had
			virtualDreamcast.reset(VirtualDreamcast::Create());

			if (!virtualDreamcast->Init())
			{
				lt.failed = true;
				return;
			}

			lt.created = emu::GetRunState();
			lt.start_rv = virtualDreamcast->StartGame(image);
		});

		// what the exit hook of reios asks the machine for
		virtualDreamcast->RequestExit();
		return false;
	}

	default:
	{
		HostJoin();

		Check(!lt.failed && lt.start_rv == 0, "the guest exited, the machine was taken apart, made again and started (%d)", lt.start_rv.load());

		// guest-exit
		{
			const Found& f = lt.exited;
			json tick = TickOf(f.tick);

			Check(f.reached && f.state.lifetime == Lifetime::Stopped && f.state.cause == StopCause::GuestExit && f.state.stopId == lt.stop0 + 1,
				"the guest exited: Stopped, guest-exit, a new stop (%u behind %u)", f.state.stopId, lt.stop0);
			Check(tick["executionState"] == json({ { "state", "paused" }, { "stopId", f.state.stopId }, { "reason", "guest-exit" } }) && tick["registers"].size() > 4,
				"its tick: paused, reason guest-exit, full (%s)", tick["executionState"].dump().c_str());
			Check(InBand(Parsed(f.run)) == kErrHostStopped && Parsed(f.slice).contains("result"), "run control -32001, reads work");
		}

		// no machine
		{
			const Found& f = lt.none;
			json tick = TickOf(f.tick), shape = Parsed(f.describe);

			Check(f.reached && f.state.lifetime == Lifetime::NoMachine && f.state.cause == StopCause::NoGame && f.state.stopId == lt.exited.state.stopId + 1,
				"the machine is gone: NoMachine, no-game, a new stop (%u)", f.state.stopId);
			Check(tick["executionState"] == json({ { "state", "paused" }, { "stopId", f.state.stopId }, { "reason", "no-game" } }) && tick["registers"].is_object()
				&& tick["registers"].empty() && tick["callstacks"] == json({ { "sh4", json::array() }, { "arm7", json::array() } }) && !HasNull(tick),
				"its tick: paused, reason no-game, registers {} and no frames (%s, %zu paths)", tick["executionState"].dump().c_str(), tick["registers"].size());

			// the static tree, every value "n/a"
			std::vector<std::pair<std::string, json>> nodes;
			std::set<std::string> problems;
			u32 values = 0, not_na = 0;

			if (shape.is_object())
				CollectNodes(shape["deviceTree"], "", &nodes, &problems);

			for (const auto& node : nodes)
			{
				for (const json& r : node.second)
				{
					values++;
					not_na += r["value"] != "n/a";
				}
			}

			u32 count;

			regs::Nodes(&count);

			Check(shape.is_object() && nodes.size() == count && problems.empty() && values > 200 && not_na == 0,
				"describe: the static tree, %u nodes, with every one of its %u values \"n/a\" (%u are not)", count, values, not_na);
			Check(shape.is_object() && shape["capabilities"] == json::array({ "event-breakpoints", "callstack", "memory-write", "register-write", "reset", "sgc-capture", "memory-binary" })
				&& shape["targets"].size() == 3, "no engine capability, and the three targets (%s)", shape.is_object() ? shape["capabilities"].dump().c_str() : "no JSON");

			Check(RpcError(Parsed(f.slice)) == kErrNoGame && RpcError(Parsed(f.stack)) == kErrNoGame && RpcError(Parsed(f.disasm)) == kErrNoGame,
				"reads of guest state: -32002, as JSON-RPC errors (%s)", f.slice.c_str());
			Check(InBand(Parsed(f.run)) == kErrNoGame && InBand(Parsed(f.step)) == kErrNoGame && InBand(Parsed(f.pause)) == kErrNoGame, "run control: -32002, in-band (%s)", f.run.c_str());
			Check(RpcError(Parsed(f.later_rpc)) == kErrNotSupported, "a method of a later milestone: -32004, not -32002");
			Check(InBand(Parsed(f.record)) == kErrNoGame && RpcError(Parsed(f.binary)) == kErrNoGame && RpcError(Parsed(f.frames)) == kErrNoGame,
				"the methods of M3: -32002, state.recordSgcFrames in-band and the two reads as JSON-RPC errors (%s)", f.record.c_str());
			Check(Ok(Parsed(f.add)) && Ok(Parsed(f.watch)) && Ok(Parsed(f.unwatch)) && InBand(Parsed(f.remove)) == kErrInvalidParams,
				"bookkeeping works without a machine: a breakpoint and a watch are added, an unknown id is -32602 (%s, %s)", f.add.c_str(), f.watch.c_str());
			Check(tick["breakpoints"].size() == 2 && tick["watches"].size() == 2 && tick["watches"][1]["expression"] == "dc.timing.sh4_cycles" && tick["watches"][1]["value"] == "n/a"
				&& tick["watches"][0]["value"] == "n/a", "they are in the tick, and a watch reads \"n/a\" (%zu breakpoints, %s)", tick["breakpoints"].size(), tick["watches"].dump().c_str());
		}

		Check(lt.created.lifetime == Lifetime::Stopped && lt.created.cause == StopCause::HostStopped && lt.created.stopId == lt.none.state.stopId + 1,
			"a machine is made: Stopped, host-stopped, a new stop (%u)", lt.created.stopId);
		Check(stop.cause == StopCause::Start && stop.lifetime == Lifetime::Parked && stop.stopId > lt.created.stopId && p_sh4rcb->cntx.pc == kResetVector,
			"the machine made again parks at its start, at the reset vector, with a new stop (\"%s\", stop %u, pc %08X)", StopCauseName(stop.cause), stop.stopId, p_sh4rcb->cntx.pc);

		{
			json tick = Tick(false);

			Check(tick["executionState"]["reason"] == "start" && tick["breakpoints"].size() == 2 && tick["watches"].size() == 2 && tick["watches"][0]["value"] == "0xA0000000",
				"its tick: reason start, with the tables that outlived the machine, and the watches read again (%s)", tick["watches"].dump().c_str());
		}

		CleanTables();
		events::ClearForTest();

		lt.phase = 0;
		return true;
	}
	}
}

/*
	What the two tests below share: the ticks the core broadcasts from inside a call, which a session without a
	server hands to a test (core::SetTickSinkForTest), and what dbg::emu publishes.
*/
namespace {

std::mutex& sink_lock = *new std::mutex();
std::vector<std::string>& sink_ticks = *new std::vector<std::string>();

void TickSink(const std::string& tick)
{
	std::lock_guard<std::mutex> lock(sink_lock);

	sink_ticks.push_back(tick);
}

// The ticks since the last look, as their params
std::vector<json> TakeTicks()
{
	std::vector<std::string> texts;

	{
		std::lock_guard<std::mutex> lock(sink_lock);

		texts.swap(sink_ticks);
	}

	std::vector<json> ticks;

	for (const std::string& text : texts)
	{
		json message = json::parse(text, nullptr, false);

		ticks.push_back(message.is_object() && message.contains("params") ? message["params"] : json::object());
	}

	return ticks;
}

std::mutex& published_lock = *new std::mutex();
std::vector<emu::RunState>& published = *new std::vector<emu::RunState>();

void Published(const emu::RunState& state)
{
	std::lock_guard<std::mutex> lock(published_lock);

	if (published.size() < 1000)
		published.push_back(state);
}

std::vector<emu::RunState> TakePublished()
{
	std::lock_guard<std::mutex> lock(published_lock);
	std::vector<emu::RunState> list;

	list.swap(published);
	return list;
}

// "Parked:reset Running ..." for a line
std::string PublishedText(const std::vector<emu::RunState>& list)
{
	std::string text;

	for (const emu::RunState& s : list)
	{
		if (!text.empty())
			text += ' ';

		text += s.lifetime == Lifetime::NoMachine ? "NoMachine" : s.lifetime == Lifetime::Stopped ? "Stopped" : s.lifetime == Lifetime::Running ? "Running" : "Parked";

		if (s.cause != StopCause::NoCause)
			text += std::string(":") + StopCauseName(s.cause);
	}

	return text.empty() ? "nothing" : text;
}

// A paused tick of one stop
bool PausedTickOf(const json& tick, u32 stopId)
{
	return tick.is_object() && tick.contains("executionState") && tick["executionState"].value("state", "") == "paused" && tick["executionState"].value("stopId", 0u) == stopId;
}

// A running tick behind that stop: no reason, no breakpoint
bool RunningTickOf(const json& tick, u32 stopId)
{
	return tick.is_object() && tick.contains("executionState") && tick["executionState"].value("state", "") == "running" && tick["executionState"].value("stopId", 0u) == stopId
		&& !tick["executionState"].contains("reason") && !tick["executionState"].contains("breakpointId");
}

const u32 kEntry = 0x8C010000;				// where reios enters a program

}

/*
	core.reset: control.reset. At a stop it is one more stop, with reason reset, and the machine is the one a
	reset leaves; the tables are kept. While the machine runs it resets and runs on, and nothing is published
*/
namespace {

struct {
	int phase;
	u32 stop0, entry_id, off_id, watch_id;
	std::string tables;				// breakpoints, watches and categories of the tick in front of the reset
	std::atomic<bool> asked { false }, timed_out { false };
	std::string answer;
} rs;

// What a reset has to keep, as one text: the breakpoints, the watches and the categories of a tick
std::string TablesOf(const json& tick)
{
	json watches = json::array();

	// (the value of a watch is its register's: it changes with the machine)
	for (const json& w : tick["watches"])
		watches.push_back({ { "id", w["id"] }, { "expression", w["expression"] } });

	return tick["breakpoints"].dump() + watches.dump() + tick["categories"].dump();
}

}

BOUNDED_GUEST_TEST(core_reset, "core.reset")
{
	const u32 pc = p_sh4rcb->cntx.pc;

	switch (rs.phase)
	{
	case 0:
	{
		if (!Alone("core.reset"))
			return true;

		ids_used = true;

		CleanTables();
		core::SetClientCountForTest(1, false);

		// a breakpoint on the entry of the program: every boot runs into it
		const std::vector<u32> before = BreakpointIds(Tick(false));

		Check(Ok(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", kEntry } })), "a code breakpoint on the entry of the program, %08X", kEntry);

		const std::vector<u32> fresh = NewIds(Tick(false), before);

		rs.entry_id = fresh.size() == 1 ? fresh[0] : 0;
		rs.phase = 1;

		// from the entry itself the resume would run through it: the boot is made again
		if (pc == kEntry)
			emu::Reset();
		else
			Check(Ok(Send("control.runUntil")), "resume");

		return false;
	}

	case 1:
	{
		// the stop of the reset this test made to get away from the entry
		if (stop.cause == StopCause::Reset)
		{
			Check(Ok(Send("control.runUntil")), "(the machine was reset to boot again: resume)");
			return false;
		}

		if (!Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == rs.entry_id && pc == kEntry, "the boot stops at the breakpoint on the entry (\"%s\" id %u, pc %08X)",
			StopCauseName(stop.cause), stop.breakpointId, pc))
		{
			rs.phase = 0;
			CleanTables();
			return true;
		}

		// something for the reset to keep: a disabled breakpoint, a watch, a muted category
		std::vector<u32> before = BreakpointIds(Tick(false));

		Check(Ok(Send("breakpoints.add", { { "event", "dc.sh4.cpu.pc" }, { "address", kEntry + 0x100 }, { "enabled", false } }))
			&& Ok(Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.vbr" }) } }))
			&& Ok(Send("breakpoints.setCategoryStates", { { "categories", { { "dsp", { { "muted", true }, { "soloed", false } } } } } })),
			"a disabled breakpoint, a watch and the category dsp muted");

		json tick = Tick(false);
		std::vector<u32> fresh = NewIds(tick, before);

		rs.off_id = fresh.size() == 1 ? fresh[0] : 0;
		rs.watch_id = tick["watches"].size() == 1 ? tick["watches"][0].value("id", 0u) : 0;
		rs.tables = TablesOf(tick);

		Check(tick["executionState"].value("breakpointId", 0u) == rs.entry_id && tick["breakpoints"].size() == 2 && tick["watches"].size() == 1 && tick["categories"]["dsp"]["muted"] == true,
			"the tick of the stop names its breakpoint, and lists two breakpoints, the watch and the category (%s)", tick["executionState"].dump().c_str());

		// the reset, asked at this stop
		emu::test::observer = Published;
		core::SetTickSinkForTest(TickSink);
		TakePublished();
		TakeTicks();

		rs.stop0 = stop.stopId;
		rs.phase = 2;

		// (it has no parameters: a key it does not name is not looked at)
		json answer = Send("control.reset", { { "target", 5 } });

		if (!Check(Ok(answer), "control.reset at the stop of a breakpoint is answered {} (%s)", answer.dump().c_str()))
		{
			// no reset was made, and no stop comes for it: the test ends here
			emu::test::observer = nullptr;
			core::SetTickSinkForTest(nullptr);
			rs.phase = 0;
			CleanTables();
			return true;
		}

		Check(TakeTicks().empty(), "and broadcasts no tick of its own: the stop of the reset is its tick");
		return false;
	}

	case 2:
	{
		const std::vector<emu::RunState> list = TakePublished();
		json tick = Tick(false);
		emu::RunState now = emu::GetRunState();

		Check(stop.cause == StopCause::Reset && stop.lifetime == Lifetime::Parked && stop.stopId == rs.stop0 + 1 && stop.breakpointId == 0,
			"a reset at a stop is one more stop: reason reset, the next stopId, no breakpoint (\"%s\", stop %u behind %u, id %u)", StopCauseName(stop.cause), stop.stopId,
			rs.stop0, stop.breakpointId);
		Check(list.size() == 1 && list[0].lifetime == Lifetime::Parked && list[0].cause == StopCause::Reset && now.lifetime == Lifetime::Parked,
			"that stop is all that was published: no Running in between (%s)", PublishedText(list).c_str());
		Check(pc == kResetVector && Hex(Reg(tick, "dc.sh4.cpu", "PC")) == kResetVector, "the machine is reset: the PC of the SH4 is the one a reset leaves, %08X, not the "
			"PC of the stop, %08X", pc, kEntry);
		Check(PausedTickOf(tick, stop.stopId) && tick["executionState"].value("reason", "") == "reset" && !tick["executionState"].contains("breakpointId"),
			"its tick: paused, reason reset, and no breakpointId, though the breakpoint of the stop before is listed (%s)", tick["executionState"].dump().c_str());
		Check(TablesOf(tick) == rs.tables && tick["breakpoints"].contains(std::to_string(rs.entry_id)) && tick["breakpoints"][std::to_string(rs.entry_id)]["enabled"] == true
			&& tick["breakpoints"].contains(std::to_string(rs.off_id)) && tick["breakpoints"][std::to_string(rs.off_id)]["enabled"] == false
			&& tick["watches"].size() == 1 && tick["watches"][0]["id"] == rs.watch_id && tick["categories"]["dsp"]["muted"] == true,
			"the tables are what they were: the breakpoint that had stopped, the disabled one, the watch and the muted category (%s)", tick["breakpoints"].dump().c_str());

		CheckArm7(tick, false, "behind the reset");

		// the machine runs behind its reset: the boot stops at the entry again
		rs.stop0 = stop.stopId;
		rs.phase = 3;

		Check(Ok(Send("control.runUntil")), "resume");
		return false;
	}

	case 3:
	{
		TakePublished();

		if (!Check(stop.cause == StopCause::Breakpoint && stop.breakpointId == rs.entry_id && pc == kEntry && stop.stopId == rs.stop0 + 1,
			"the machine runs behind its reset: the boot stops at the breakpoint on the entry (\"%s\" id %u, pc %08X)", StopCauseName(stop.cause), stop.breakpointId, pc))
		{
			emu::test::observer = nullptr;
			core::SetTickSinkForTest(nullptr);
			rs.phase = 0;
			CleanTables();
			return true;
		}

		// A reset while the machine runs. The program goes on from its entry; a thread asks for the reset when
		// the machine has run for a while, as a client does. The breakpoint on the entry says that it was made
		events::ClearForTest();
		TakeTicks();

		rs.asked = false;
		rs.timed_out = false;
		rs.stop0 = stop.stopId;
		rs.phase = 4;

		HostStart([] {
			if (!WaitLifetime(Lifetime::Running))
				return;

			// far from its entry
			std::this_thread::sleep_for(std::chrono::milliseconds(300));

			rs.answer = Ask("control.reset");
			rs.asked = true;

			// a machine that is not reset never comes back to its entry: the test is given a stop all the same, in
			// half the bound of a wait
			if (!WaitLifetime(Lifetime::Parked, kBoundMs / 2))
			{
				rs.timed_out = true;
				emu::RequestPause();
			}
		});

		Check(Ok(Send("control.runUntil")), "resume, and a thread that asks for control.reset while the machine runs");
		return false;
	}

	default:
	{
		HostJoin();

		const std::vector<emu::RunState> list = TakePublished();
		json tick = Tick(false);
		u32 resets = 0;

		for (const json& entry : tick["eventLog"])
			resets += entry["metadata"]["event"] == "dc.system.reset";

		Check(rs.asked && Ok(Parsed(rs.answer)), "control.reset while the machine runs is answered {} (%s)", rs.answer.c_str());
		Check(!rs.timed_out && stop.cause == StopCause::Breakpoint && stop.breakpointId == rs.entry_id && pc == kEntry,
			"and the machine is reset: it boots again and stops at the breakpoint on the entry of the program (\"%s\" id %u, pc %08X%s)", StopCauseName(stop.cause),
			stop.breakpointId, pc, rs.timed_out ? "; it never came there, and was paused" : "");
		Check(list.size() == 2 && list[0].lifetime == Lifetime::Running && list[1].lifetime == Lifetime::Parked && list[1].cause == StopCause::Breakpoint
			&& stop.stopId == rs.stop0 + 1, "nothing is published for it: the resume, and the stop of the breakpoint, which is the next stop (%s; stop %u behind %u)",
			PublishedText(list).c_str(), stop.stopId, rs.stop0);
		Check(TakeTicks().empty(), "and no tick is broadcast for it");
		Check(resets == 1, "the event log has the reset: one entry dc.system.reset (%u)", resets);

		emu::test::observer = nullptr;
		core::SetTickSinkForTest(nullptr);

		CleanTables();
		events::ClearForTest();

		rs.phase = 0;
		return true;
	}
	}
}

/*
	core.writes: the three write methods as a client meets them. By parameter (-32602, in front of everything
	else), by lifetime, by the errors of each method in their order, all of them in-band; what describe says:
	seven capabilities besides the engine's, and flags.writable on the registers that have a writer and nowhere
	else; the tick behind an accepted write, which is of the stop the write was made in; and a write made while
	the machine runs, which is served, with a running tick behind it, and never answered -32003.

	The register the test writes while the program runs is DBR, which the program does not use, and its memory
	is in the middle of main memory, far from the program and from its stack.
*/
namespace {

const u32 kQuietAddr = 0x8CA00000;

struct RunningAnswers {
	bool ran = false;
	std::string refused_register, refused_memory, bad_register, bad_memory, memory, read, reg, watch, unknown;
	size_t ticks_memory = 0, ticks_register = 0, ticks_refused = 0;
	json tick_memory, tick_register;
};

struct StoppedAnswers {
	bool reached = false;
	std::string memory, reg, watch, reset, bad_memory, bad_register, bad_watch, unknown_register, unknown_watch, dsp_memory, rom_memory, slice;
};

struct {
	int phase;
	u32 stop0, pc0, r0, dbr, watch_r1, watch_enabled, watch_text, watch_dbr;
	u8 quiet[4];
	std::atomic<bool> failed { false };
	std::atomic<int> start_rv { 99 };
	RunningAnswers running;
	StoppedAnswers stopped, none;
} wr;

json WriteMemory(const char* target, u32 address, std::initializer_list<int> bytes)
{
	return Send("state.writeMemory", { { "target", target }, { "address", address }, { "data", json(std::vector<int>(bytes)) } });
}

json SetRegister(const char* path, const char* name, const char* value)
{
	return Send("state.setRegister", { { "path", path }, { "name", name }, { "value", value } });
}

// R0 of the SH4 written with a text: the value it then reads, 0xFFFFFFFF behind a write that was not taken
u32 R0Written(const char* text)
{
	if (!Ok(SetRegister("dc.sh4.cpu", "R0", text)))
		return 0xFFFFFFFF;

	return p_sh4rcb->cntx.r[0];
}

void AskStopped(StoppedAnswers* a, u32 watch)
{
	a->reached = true;

	// what the request alone shows comes in front of the lifetime
	a->bad_memory = Ask("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array() } });
	a->bad_register = Ask("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", "R0" } });
	a->bad_watch = Ask("state.editWatch", { { "watchId", "1" }, { "value", "0" } });

	// the lifetime in front of what a method finds out itself
	a->memory = Ask("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array({ 0xEE, 0xEE, 0xEE, 0xEE }) } });
	a->reg = Ask("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", "DBR" }, { "value", "0xEEEEEEEE" } });
	a->watch = Ask("state.editWatch", { { "watchId", watch }, { "value", "0xEEEEEEEE" } });
	a->reset = Ask("control.reset");
	a->unknown_register = Ask("state.setRegister", { { "path", "dc.no.such.node" }, { "name", "R0" }, { "value", "0" } });
	a->unknown_watch = Ask("state.editWatch", { { "watchId", 99999 }, { "value", "0" } });
	a->dsp_memory = Ask("state.writeMemory", { { "target", "dsp" }, { "address", 0 }, { "data", json::array({ 0 }) } });
	a->rom_memory = Ask("state.writeMemory", { { "target", "sh4" }, { "address", 0 }, { "data", json::array({ 0 }) } });

	a->slice = Ask("state.getMemorySlice", { { "address", kQuietAddr }, { "length", 4 } });
}

}

BOUNDED_GUEST_TEST(core_writes, "core.writes")
{
	Sh4Context& ctx = p_sh4rcb->cntx;

	switch (wr.phase)
	{
	case 0:
	{
		if (!Alone("core.writes"))
			return true;

		ids_used = true;

		CleanTables();
		events::ClearForTest();
		core::SetClientCountForTest(1, false);
		core::SetTickSinkForTest(TickSink);
		TakeTicks();

		wr.stop0 = stop.stopId;
		wr.pc0 = ctx.pc;
		wr.r0 = ctx.r[0];
		wr.dbr = ctx.dbr;

		/*
			What describe says
		*/
		json shape = json::parse(core::DescribeText(), nullptr, false);
		std::vector<std::pair<std::string, json>> nodes;
		std::set<std::string> problems;

		if (shape.is_object())
			CollectNodes(shape["deviceTree"], "", &nodes, &problems);

		{
			emu::Sh4EngineInfo engine;
			json caps = shape.is_object() ? shape["capabilities"] : json::array();
			json want = json::array({ "event-breakpoints", "callstack", "memory-write", "register-write", "reset", "sgc-capture", "memory-binary" });

			if (emu::GetSh4Engine(&engine))
				want.push_back(engine.fallback ? "sh4-debug-fallback" : "sh4-debug-native");

			Check(caps == want && caps.size() == 8, "describe has seven capabilities besides the engine's: the two of M1, memory-write, register-write and reset of "
				"M2, sgc-capture and memory-binary of M3 (%s)", caps.dump().c_str());
		}

		{
			u32 count, flagged = 0, wrong = 0, registers = 0;
			const regs::Node* table = regs::Nodes(&count);
			std::map<std::string, json> described;
			std::string first;

			for (const auto& node : nodes)
				described[node.first] = node.second;

			for (u32 n = 0; n < count; n++)
			{
				const json& list = described[table[n].path];

				for (u32 r = 0; r < table[n].registerCount; r++)
				{
					const json reg = r < list.size() ? list[r] : json::object();
					const bool writable = (table[n].registers[r].flags & regs::kRegWritable) != 0;
					const bool says = reg.contains("flags");

					registers++;
					flagged += says;

					if (says != writable || reg.size() != (says ? 4u : 3u) || (says && reg["flags"] != json({ { "writable", true } })) || reg.value("name", "") != table[n].registers[r].name)
					{
						if (wrong++ == 0)
							first = std::string(table[n].path) + "." + table[n].registers[r].name + ": " + reg.dump();
					}
				}
			}

			Check(nodes.size() == count && wrong == 0 && flagged == 69 + 40, "in describe a register has flags: {\"writable\": true} exactly when its row has a writer, and no other "
				"key beside name, value and width: %u of %u registers, the 69 of dc.sh4.cpu and the 40 of dc.aica.arm7 that are not ENABLED (%u are wrong; %s)", flagged,
				registers, wrong, first.empty() ? "none" : first.c_str());

			json enabled, pc_sh4, pc_arm7, build;

			for (const auto& node : nodes)
			{
				for (const json& reg : node.second)
				{
					if (node.first == "dc.aica.arm7" && reg["name"] == "ENABLED")
						enabled = reg;
					if (node.first == "dc.aica.arm7" && reg["name"] == "PC")
						pc_arm7 = reg;
					if (node.first == "dc.sh4.cpu" && reg["name"] == "PC")
						pc_sh4 = reg;
					if (node.first == "dc" && reg["name"] == "BUILD")
						build = reg;
				}
			}

			Check(enabled.is_object() && !enabled.contains("flags") && build.is_object() && !build.contains("flags") && pc_sh4.contains("flags") && pc_arm7.contains("flags"),
				"ENABLED of the ARM7 and a text register have no flags; the PC of either processor has");
		}

		{
			json tick = Tick(true);
			u32 with_flags = 0, registers = 0;

			for (auto it = tick["registers"].begin(); it != tick["registers"].end(); ++it)
			{
				for (const json& reg : it.value())
				{
					registers++;
					with_flags += reg.contains("flags") || reg.size() != 3;
				}
			}

			Check(registers > 200 && with_flags == 0 && PausedTickOf(tick, stop.stopId), "no register of a tick has flags: %u registers of a full paused tick, each with name, "
				"value and width (%u have more)", registers, with_flags);
		}

		/*
			state.writeMemory
		*/
		u8 kept[8], top[2];

		memcpy(kept, GetMemPtr(kQuietAddr, 8), 8);
		memcpy(top, GetMemPtr(0x8CFFFFFE, 2), 2);

		json data4 = json::array({ 1, 2, 3, 4 });

		Check(InBand(Send("state.writeMemory", { { "address", kQuietAddr }, { "data", data4 } })) == kErrInvalidParams
			&& InBand(Send("state.writeMemory", { { "target", 5 }, { "address", kQuietAddr }, { "data", data4 } })) == kErrInvalidParams
			&& InBand(Send("state.writeMemory", { { "target", "cpu" }, { "address", kQuietAddr }, { "data", data4 } })) == kErrInvalidParams,
			"state.writeMemory without a target, or with one that is no target: -32602, in-band (a read has a default target, a write has none)");
		Check(InBand(Send("state.writeMemory", { { "target", "sh4" }, { "data", data4 } })) == kErrInvalidParams
			&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", "0x8CA00000" }, { "data", data4 } })) == kErrInvalidParams
			&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", -4 }, { "data", data4 } })) == kErrInvalidParams
			&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", 4294967296.0 }, { "data", data4 } })) == kErrInvalidParams,
			"without an address, or with one that is a string, negative or above 32 bits: -32602");

		{
			json big = json::array(), most = json::array();

			for (u32 i = 0; i < kMaxWriteMemoryBytes + 1; i++)
				big.push_back(0xEE);

			Check(InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr } })) == kErrInvalidParams
				&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", "AQID" } })) == kErrInvalidParams
				&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array() } })) == kErrInvalidParams
				&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", big } })) == kErrInvalidParams,
				"without data, with data that is no array, with none and with 65537 bytes: -32602");
			Check(InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array({ 0xEE, 256 }) } })) == kErrInvalidParams
				&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array({ 0xEE, -1 }) } })) == kErrInvalidParams
				&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array({ 0xEE, 1.5 }) } })) == kErrInvalidParams
				&& InBand(Send("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array({ 0xEE, "1" }) } })) == kErrInvalidParams,
				"with a byte that is 256, negative, a fraction or a string: -32602");

			// the errors of the method itself
			Check(InBand(WriteMemory("dsp", 0, { 0xEE })) == kErrNotSupported, "target dsp: -32004, in-band; the DSP has no memory");
			Check(InBand(WriteMemory("sh4", 0x00000000, { 0xEE })) == kErrNotWritable && InBand(WriteMemory("sh4", 0x005F8000, { 0xEE })) == kErrNotWritable
				&& InBand(WriteMemory("arm7", 0x00800000, { 0xEE })) == kErrNotWritable && InBand(WriteMemory("sh4", 0xFFFFFFFF, { 0xEE, 0xEE })) == kErrNotWritable,
				"the boot ROM, a register window, the ARM7 above its wave memory and a range past the end of the address space: -32013, in-band");
			Check(InBand(WriteMemory("sh4", 0x8FFFFFFE, { top[0] ^ 0xFF, top[1] ^ 0xFF, 0xEE, 0xEE })) == kErrNotWritable
				&& InBand(WriteMemory("sh4", 0x0FFFFFFE, { top[0] ^ 0xFF, top[1] ^ 0xFF, 0xEE, 0xEE })) == kErrNotWritable && memcmp(top, GetMemPtr(0x8CFFFFFE, 2), 2) == 0,
				"a range across the end of main memory: -32013, and its first half is not written");
			Check(memcmp(kept, GetMemPtr(kQuietAddr, 8), 8) == 0 && TakeTicks().empty(), "nothing was stored by any of them, and none broadcast a tick");

			// taken
			json answer = WriteMemory("sh4", kQuietAddr, { 0x11, 0x22, 0x33, 0x44 });
			std::vector<json> ticks = TakeTicks();
			json slice = Send("state.getMemorySlice", { { "address", kQuietAddr & 0x1FFFFFFF }, { "length", 4 } });

			Check(Ok(answer) && GetMemPtr(kQuietAddr, 4)[0] == 0x11 && GetMemPtr(kQuietAddr, 4)[3] == 0x44 && slice.contains("result")
				&& slice["result"]["data"] == json::array({ 0x11, 0x22, 0x33, 0x44 }), "four bytes of main memory are written: {}, and they read back through another mirror (%s)",
				answer.dump().c_str());
			Check(ticks.size() == 1 && PausedTickOf(ticks[0], stop.stopId) && ticks[0]["executionState"].value("reason", "") == StopCauseName(stop.cause),
				"one tick is broadcast behind the write: paused, with the stopId and the reason of the stop it was made in (%zu ticks; %s)", ticks.size(),
				ticks.empty() ? "none" : ticks[0]["executionState"].dump().c_str());

			// the bound: 65536 bytes, written back as they read
			json block = Send("state.getMemorySlice", { { "address", kEntry }, { "length", kMaxWriteMemoryBytes } });

			if (block.contains("result"))
				most = block["result"]["data"];

			answer = Send("state.writeMemory", { { "target", "sh4" }, { "address", kEntry }, { "data", most } });
			ticks = TakeTicks();

			Check(most.size() == kMaxWriteMemoryBytes && Ok(answer) && ticks.size() == 1 && PausedTickOf(ticks[0], stop.stopId)
				&& Send("state.getMemorySlice", { { "address", kEntry }, { "length", kMaxWriteMemoryBytes } })["result"]["data"] == most,
				"65536 bytes, the program's own, written back as they read: {}, a tick, and they read as before");

			// wave memory through the ARM7
			u8& wave = sh4_cpu->aica_ram.data[0x001F0000 & (sh4_cpu->aica_ram.size - 1)];
			const u8 wave_was = wave;

			answer = WriteMemory("arm7", 0x001F0000, { wave_was ^ 0x5A });
			ticks = TakeTicks();
			Check(Ok(answer) && ticks.size() == 1 && wave == (wave_was ^ 0x5A), "a byte of wave memory through the target arm7: {}, a tick");
			wave = wave_was;
		}

		memcpy(GetMemPtr(kQuietAddr, 8), kept, 8);
		TakeTicks();

		/*
			state.setRegister
		*/
		Check(InBand(Send("state.setRegister", { { "name", "R0" }, { "value", "0" } })) == kErrInvalidParams
			&& InBand(Send("state.setRegister", { { "path", "dc.sh4.cpu" }, { "value", "0" } })) == kErrInvalidParams
			&& InBand(Send("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", "R0" } })) == kErrInvalidParams
			&& InBand(Send("state.setRegister", { { "path", 5 }, { "name", "R0" }, { "value", "0" } })) == kErrInvalidParams
			&& InBand(Send("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", 0 }, { "value", "0" } })) == kErrInvalidParams
			&& InBand(Send("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", "R0" }, { "value", 0 } })) == kErrInvalidParams,
			"state.setRegister without a path, a name or a value, or with one that is no string (a value that is a number among them): -32602, in-band");

		// the errors of the method itself, in their order: the register, its writer, the text, the device
		Check(InBand(SetRegister("dc.no.such.node", "R0", "0")) == kErrInvalidParams && InBand(SetRegister("dc.sh4.cpu", "NO_SUCH", "0")) == kErrInvalidParams
			&& InBand(SetRegister("dc.sh4", "R0", "0")) == kErrInvalidParams && InBand(SetRegister("dc.no.such.node", "R0", "twelve")) == kErrInvalidParams,
			"a path or a name that the tree does not have: -32602");
		{
			// A name is compared whole: what stands in front of a NUL is not the name. Sent as a client sends it,
			// with the escape \u0000 in the JSON text
			const std::string path_nul("dc.sh4.cpu\0x", 12), name_nul("R0\0y", 4);
			const json by_path = Send("state.setRegister", { { "path", path_nul }, { "name", "R0" }, { "value", "0x5A5A0001" } });
			const json by_name = Send("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", name_nul }, { "value", "0x5A5A0002" } });
			const json by_both = Send("state.setRegister", { { "path", path_nul }, { "name", name_nul }, { "value", "0x5A5A0003" } });

			Check(InBand(by_path) == kErrInvalidParams && InBand(by_name) == kErrInvalidParams && InBand(by_both) == kErrInvalidParams && ctx.r[0] == wr.r0 && TakeTicks().empty(),
				"a path or a name with a NUL behind the name of a node or of a register (\"dc.sh4.cpu\\u0000x\", \"R0\\u0000y\") is none of the tree: -32602, R0 is what it was "
				"(%08X), and no tick was broadcast (%s)", ctx.r[0], by_both.dump().c_str());
		}

		Check(InBand(SetRegister("dc.aica.arm7", "ENABLED", "0")) == kErrNotWritable && InBand(SetRegister("dc", "BUILD", "0")) == kErrNotWritable
			&& InBand(SetRegister("dc.timing", "SH4_CYCLES", "0")) == kErrNotWritable && InBand(SetRegister("dc.sh4.ccn", "MMUCR", "0")) == kErrNotWritable
			&& InBand(SetRegister("dc.holly.intc", "SB_ISTNRM", "0")) == kErrNotWritable && InBand(SetRegister("dc.aica.dsp", "STEP", "0")) == kErrNotWritable,
			"a register without a writer, and one of width 0: -32013");
		Check(InBand(SetRegister("dc.aica.arm7", "ENABLED", "twelve")) == kErrNotWritable && InBand(SetRegister("dc", "BUILD", "")) == kErrNotWritable,
			"no writer comes in front of the text: a value that is no number is -32013 there as well");

		{
			u32 refused = 0;

			for (const char* text : { "twelve", "-1", "+1", "", " ", "1.5", "0x", "0X", "x10", "0x1G", "1 2", "0x 1", "1e3", "0b1", "$10", "10h" })
				refused += InBand(SetRegister("dc.sh4.cpu", "R0", text)) == kErrInvalidParams;

			Check(refused == 16 && ctx.r[0] == wr.r0 && TakeTicks().empty(), "a value that is neither 0x and hex digits nor decimal digits: -32602, for each of 16 texts (%u); "
				"R0 is what it was, and no tick was broadcast", refused);
		}

		Check(InBand(SetRegister("dc.sh4.cpu", "PC", "0x8C010001")) == kErrNotWritable && InBand(SetRegister("dc.aica.arm7", "CPSR", "0")) == kErrNotWritable
			&& ctx.pc == wr.pc0, "a value the device refuses, an odd PC of the SH4 and a CPSR of the ARM7 that is no mode: -32013, not -32602");

		// taken: the value, in its forms
		{
			json answer = SetRegister("dc.sh4.cpu", "R0", "0x12345678");
			std::vector<json> ticks = TakeTicks();

			Check(Ok(answer) && ctx.r[0] == 0x12345678 && ticks.size() == 1 && PausedTickOf(ticks[0], stop.stopId) && Reg(ticks[0], "dc.sh4.cpu", "R0") == "0x12345678",
				"R0 of the SH4 is written: {}, and one tick behind it, paused, of the same stop, with the value (%zu ticks)", ticks.size());

			const u32 dec = R0Written("255"), blanks = R0Written(" 0x1f "), tabs = R0Written("\t17\t "), upper = R0Written("0XaBcDeF"), zeros = R0Written("000000000000000000000000000019");

			Check(dec == 0xFF && blanks == 0x1F && tabs == 17 && upper == 0xABCDEF && zeros == 19, "decimal digits, blanks and tabs around the text, 0X and digits of either case, "
				"leading zeros: %X, %X, %X, %X, %X", dec, blanks, tabs, upper, zeros);

			// of a text of any length the low bits count
			const u32 hex33 = R0Written("0x1FFFFFFFF"), hex76 = R0Written("0xABC123456789ABCDEF0"), dec64 = R0Written("18446744073709551621"),
				dec70 = R0Written("1180591620717411303429"), dec99 = R0Written("340282366920938463463374607431768211461");

			Check(hex33 == 0xFFFFFFFF && hex76 == 0x9ABCDEF0 && dec64 == 5 && dec70 == 5 && dec99 == 5, "of a text of any length the low bits are kept: 33 and 76 bits of hex "
				"(%08X, %08X), and in decimal 2^64 + 5, 2^70 + 5 and 2^128 + 5 (%u, %u, %u)", hex33, hex76, dec64, dec70, dec99);

			// the path and the name without regard to case
			const std::string arm_r1 = Reg(Tick(false), "dc.aica.arm7", "R1");

			answer = SetRegister("DC.SH4.CPU", "r0", "0x77");
			Check(Ok(answer) && ctx.r[0] == 0x77 && Ok(SetRegister("Dc.Aica.Arm7", "r1", arm_r1.c_str())) && Reg(Tick(false), "dc.aica.arm7", "R1") == arm_r1,
				"the path and the name are matched without regard to case (R0 of the SH4; R1 of the ARM7, written with the value it has, %s)", arm_r1.c_str());

			TakeTicks();

			// a register reads back what is stored
			const u32 sr = sh4_sr_GetFull();

			answer = SetRegister("dc.sh4.cpu", "SR", "0xFFFFFFFF");
			ticks = TakeTicks();
			Check(Ok(answer) && ticks.size() == 1 && Reg(ticks[0], "dc.sh4.cpu", "SR") == "0x700083F3", "SR written with 0xFFFFFFFF reads 0x700083F3 in the tick behind the "
				"write: what is stored (%s)", ticks.empty() ? "no tick" : Reg(ticks[0], "dc.sh4.cpu", "SR").c_str());

			char back[16];

			snprintf(back, sizeof(back), "0x%08X", sr);
			Check(Ok(SetRegister("dc.sh4.cpu", "SR", back)) && sh4_sr_GetFull() == sr, "(SR is written back)");
			TakeTicks();
		}

		/*
			state.editWatch: state.setRegister on the register of a watch
		*/
		{
			// the same for a watch, which state.editWatch would then write through: no watch is made of such a text
			const std::string name_nul("dc.sh4.cpu.r1\0y", 15), path_nul("dc.sh4.cpu\0x.r1", 15);
			const json by_name = Send("state.watch", { { "expressions", json::array({ name_nul }) } });
			const json by_path = Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.r2", path_nul }) } });

			Check(InBand(by_name) == kErrInvalidParams && InBand(by_path) == kErrInvalidParams && Tick(false)["watches"].empty(),
				"a watch expression with a NUL behind the name of a register or of a node is no register of the tree: -32602, and no watch is listed (%s)",
				by_name.dump().c_str());
			TakeTicks();
		}

		Check(Ok(Send("state.watch", { { "expressions", json::array({ "dc.sh4.cpu.r1", "dc.aica.arm7.enabled", "dc.build", "dc.sh4.cpu.dbr" }) } })), "(four watches)");

		{
			json tick = Tick(false);

			wr.watch_r1 = wr.watch_enabled = wr.watch_text = wr.watch_dbr = 0;

			for (const json& w : tick["watches"])
			{
				if (w["expression"] == "dc.sh4.cpu.r1")
					wr.watch_r1 = w.value("id", 0u);
				if (w["expression"] == "dc.aica.arm7.enabled")
					wr.watch_enabled = w.value("id", 0u);
				if (w["expression"] == "dc.build")
					wr.watch_text = w.value("id", 0u);
				if (w["expression"] == "dc.sh4.cpu.dbr")
					wr.watch_dbr = w.value("id", 0u);
			}

			TakeTicks();

			const u32 r1 = ctx.r[1];

			Check(InBand(Send("state.editWatch", { { "value", "0" } })) == kErrInvalidParams && InBand(Send("state.editWatch", { { "watchId", wr.watch_r1 } })) == kErrInvalidParams
				&& InBand(Send("state.editWatch", { { "watchId", "1" }, { "value", "0" } })) == kErrInvalidParams
				&& InBand(Send("state.editWatch", { { "watchId", -1 }, { "value", "0" } })) == kErrInvalidParams
				&& InBand(Send("state.editWatch", { { "watchId", wr.watch_r1 }, { "value", 0 } })) == kErrInvalidParams,
				"state.editWatch without a watchId or a value, or with one of another type: -32602, in-band");
			Check(InBand(Send("state.editWatch", { { "watchId", 99999 }, { "value", "0" } })) == kErrInvalidParams
				&& InBand(Send("state.editWatch", { { "watchId", 99999 }, { "value", "twelve" } })) == kErrInvalidParams, "a watchId that no watch has: -32602");
			Check(InBand(Send("state.editWatch", { { "watchId", wr.watch_enabled }, { "value", "1" } })) == kErrNotWritable
				&& InBand(Send("state.editWatch", { { "watchId", wr.watch_enabled }, { "value", "twelve" } })) == kErrNotWritable
				&& InBand(Send("state.editWatch", { { "watchId", wr.watch_text }, { "value", "1" } })) == kErrNotWritable,
				"a watch on a register without a writer, or on a text: -32013, also with a value that is no number");
			Check(InBand(Send("state.editWatch", { { "watchId", wr.watch_r1 }, { "value", "twelve" } })) == kErrInvalidParams && ctx.r[1] == r1 && TakeTicks().empty(),
				"a watch on R1 and a value that is no number: -32602, and nothing is written or broadcast");

			json answer = Send("state.editWatch", { { "watchId", wr.watch_r1 }, { "value", " 0x00C0FFEE " } });
			std::vector<json> ticks = TakeTicks();

			Check(Ok(answer) && ctx.r[1] == 0x00C0FFEE && ticks.size() == 1 && PausedTickOf(ticks[0], stop.stopId) && Reg(ticks[0], "dc.sh4.cpu", "R1") == "0x00C0FFEE",
				"a watch on R1 is edited: {}, R1 has the value, and one tick behind it shows it in the register and in the watch (%s)",
				ticks.empty() ? "no tick" : ticks[0]["watches"].dump().c_str());

			ctx.r[1] = r1;
		}

		ctx.r[0] = wr.r0;

		Check(emu::GetRunState().stopId == stop.stopId && emu::GetRunState().lifetime == Lifetime::Parked, "no write was a stop: the stop is the one the test began at (%u)",
			emu::GetRunState().stopId);

		/*
			While the machine runs. A thread sends what a client sends
		*/
		memcpy(wr.quiet, GetMemPtr(kQuietAddr, 4), 4);
		TakeTicks();

		wr.running = RunningAnswers();
		wr.phase = 1;

		HostStart([] {
			RunningAnswers& a = wr.running;

			if (!WaitLifetime(Lifetime::Running))
				return;

			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			a.ran = true;

			// refused, and not with "not paused"
			a.refused_register = Ask("state.setRegister", { { "path", "dc.aica.arm7" }, { "name", "ENABLED" }, { "value", "0" } });
			a.refused_memory = Ask("state.writeMemory", { { "target", "sh4" }, { "address", 0 }, { "data", json::array({ 0 }) } });
			a.bad_register = Ask("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", "DBR" }, { "value", "twelve" } });
			a.bad_memory = Ask("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array({ 256 }) } });
			a.unknown = Ask("state.editWatch", { { "watchId", 99999 }, { "value", "0" } });
			a.ticks_refused = TakeTicks().size();

			// taken
			a.memory = Ask("state.writeMemory", { { "target", "sh4" }, { "address", kQuietAddr }, { "data", json::array({ 0xA1, 0xB2, 0xC3, 0xD4 }) } });

			{
				std::vector<json> ticks = TakeTicks();

				a.ticks_memory = ticks.size();
				a.tick_memory = ticks.empty() ? json::object() : ticks[0];
			}

			a.read = Ask("state.getMemorySlice", { { "address", kQuietAddr }, { "length", 4 } });
			a.reg = Ask("state.setRegister", { { "path", "dc.sh4.cpu" }, { "name", "DBR" }, { "value", "0x0DB00DB0" } });

			{
				std::vector<json> ticks = TakeTicks();

				a.ticks_register = ticks.size();
				a.tick_register = ticks.empty() ? json::object() : ticks[0];
			}

			a.watch = Ask("state.editWatch", { { "watchId", wr.watch_dbr }, { "value", "0x0DB11DB1" } });

			emu::RequestPause();
		});

		Check(Ok(Send("control.runUntil")), "resume, and a thread that writes while the machine runs");
		return false;
	}

	case 1:
	{
		HostJoin();

		const RunningAnswers& a = wr.running;
		json read = Parsed(a.read);

		Check(a.ran && stop.cause == StopCause::Pause && stop.stopId == wr.stop0 + 1, "the machine ran, and was paused (\"%s\")", StopCauseName(stop.cause));
		Check(InBand(Parsed(a.refused_register)) == kErrNotWritable && InBand(Parsed(a.refused_memory)) == kErrNotWritable,
			"a write to a running target is served: a register without a writer and the boot ROM are -32013 there, not -32003 (%s)", a.refused_register.c_str());
		Check(InBand(Parsed(a.bad_register)) == kErrInvalidParams && InBand(Parsed(a.bad_memory)) == kErrInvalidParams && InBand(Parsed(a.unknown)) == kErrInvalidParams
			&& a.ticks_refused == 0, "a text that is no number, a byte of 256 and an unknown watchId are -32602 there, and nothing is broadcast (%zu ticks)", a.ticks_refused);
		Check(Ok(Parsed(a.memory)) && read.contains("result") && read["result"]["data"] == json::array({ 0xA1, 0xB2, 0xC3, 0xD4 }),
			"four bytes of main memory are written while the machine runs: {}, and a read made while it runs has them (%s)", a.memory.c_str());
		Check(a.ticks_memory == 1 && RunningTickOf(a.tick_memory, wr.stop0), "with one tick behind the write, a running tick of the stop the machine left: no reason, the same "
			"stopId (%zu ticks; %s)", a.ticks_memory, a.tick_memory.value("executionState", json::object()).dump().c_str());
		Check(Ok(Parsed(a.reg)) && a.ticks_register == 1 && RunningTickOf(a.tick_register, wr.stop0) && Reg(a.tick_register, "dc.sh4.cpu", "DBR") == "0x0DB00DB0",
			"a register is written while the machine runs, DBR: {}, and a running tick that has its value (%s; %zu ticks)", a.reg.c_str(), a.ticks_register);
		Check(Ok(Parsed(a.watch)) && ctx.dbr == 0x0DB11DB1, "and a watch on it is edited: DBR is %08X at the stop of the pause", ctx.dbr);

		ctx.dbr = wr.dbr;
		memcpy(GetMemPtr(kQuietAddr, 4), wr.quiet, 4);
		TakeTicks();

		/*
			By lifetime. A thread plays the host: it stops the machine, takes it apart and makes it again
		*/
		wr.stopped = StoppedAnswers();
		wr.none = StoppedAnswers();
		wr.failed = false;
		wr.start_rv = 99;
		wr.phase = 2;

		HostStart([] {
			const std::string image = cfgLoadStr("config", "image", "");

			virtualDreamcast->Stop([] { });

			if (!WaitLifetime(Lifetime::Stopped))
			{
				wr.failed = true;
				return;
			}

			AskStopped(&wr.stopped, wr.watch_dbr);

			// the host takes the machine apart, and has it until the new one has started (the watch of the bound)
			std::lock_guard<std::timed_mutex> owns(bound.restart);

			virtualDreamcast.reset();

			AskStopped(&wr.none, wr.watch_dbr);

			virtualDreamcast.reset(VirtualDreamcast::Create());

			if (!virtualDreamcast->Init())
			{
				wr.failed = true;
				return;
			}

			wr.start_rv = virtualDreamcast->StartGame(image);
		});
		return false;
	}

	default:
	{
		HostJoin();

		Check(!wr.failed && wr.start_rv == 0 && wr.stopped.reached && wr.none.reached, "the host stopped the machine, took it apart, made it again and started it (%d)",
			wr.start_rv.load());

		for (int which = 0; which < 2; which++)
		{
			const StoppedAnswers& a = which == 0 ? wr.stopped : wr.none;
			const int lifetime = which == 0 ? kErrHostStopped : kErrNoGame;
			const char* where = which == 0 ? "while the host has the machine stopped" : "with no machine";

			Check(InBand(Parsed(a.bad_memory)) == kErrInvalidParams && InBand(Parsed(a.bad_register)) == kErrInvalidParams && InBand(Parsed(a.bad_watch)) == kErrInvalidParams,
				"%s: what the request alone shows is -32602, in front of the lifetime (%s)", where, a.bad_memory.c_str());
			Check(InBand(Parsed(a.memory)) == lifetime && InBand(Parsed(a.reg)) == lifetime && InBand(Parsed(a.watch)) == lifetime && InBand(Parsed(a.reset)) == lifetime,
				"%s: state.writeMemory, state.setRegister, state.editWatch and control.reset are %d, in-band (%s)", where, lifetime, a.memory.c_str());
			Check(InBand(Parsed(a.unknown_register)) == lifetime && InBand(Parsed(a.unknown_watch)) == lifetime,
				"%s: the lifetime comes in front of what a method finds out itself: a path and a watchId that do not exist are %d as well", where, lifetime);
			Check(InBand(Parsed(a.dsp_memory)) == lifetime && InBand(Parsed(a.rom_memory)) == lifetime,
				"%s: and so are state.writeMemory to the target dsp and into the boot ROM, %d: not the -32004 and the -32013 they are when the call is served (%s; %s)", where,
				lifetime, a.dsp_memory.c_str(), a.rom_memory.c_str());
		}

		{
			json slice = Parsed(wr.stopped.slice);

			Check(slice.contains("result") && slice["result"]["data"] == json::array({ wr.quiet[0], wr.quiet[1], wr.quiet[2], wr.quiet[3] }),
				"nothing was written while the machine was stopped: the bytes read as before (%s)", wr.stopped.slice.c_str());
			Check(RpcError(Parsed(wr.none.slice)) == kErrNoGame, "(and with no machine there is nothing to read)");
		}

		Check(TakeTicks().empty(), "no tick was broadcast for a request that was refused by the lifetime");
		Check(stop.cause == StopCause::Start && p_sh4rcb->cntx.pc == kResetVector, "the machine made again parks at its start (\"%s\", pc %08X)", StopCauseName(stop.cause),
			p_sh4rcb->cntx.pc);

		core::SetTickSinkForTest(nullptr);

		CleanTables();
		events::ClearForTest();

		wr.phase = 0;
		return true;
	}
	}
}
