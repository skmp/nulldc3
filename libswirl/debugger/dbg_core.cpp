/*
	This file is part of libswirl
*/
#include "license/bsd"


// json.hpp comes in front of every tree header (some of them define names it uses)
#include "deps/nlohmann/json.hpp"

#include "dbg_core_private.h"
#include "dbg_callstack.h"
#include "dbg_disasm.h"
#include "dbg_mem.h"
#include "dbg_net.h"
#include "dbg_regs.h"
#include "dbg_sgc.h"
#include "dbg_symbols.h"
#include "dbg_webui.h"

#include "hw/aica/dsp.h"
#include "hw/sh4/sh4_if.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

/*
	The core: protocol v3 on the wire (docs/nulldc3/devtools-integration-design.md, section 3), the
	breakpoint, watch and category tables, the tick builder, and the thread (C) that serves the clients.

	Who does what
	  (N)  the network thread calls the handler, which only puts what it got into the queue of (C)
	  (C)  takes a message, parses and validates it, and reaches guest state and the tables through
	       dbg::emu::Call. It owns the timers: the periodic tick, the capture's deadline, the grace period after
	       the last client left
	  (E)  runs the closures of those calls and the run-state listener; both read and change the tables and
	       build ticks. With no emulator thread the caller of a call runs its closure itself, under L, and so
	       does the thread that changes the lifetime for the listener

	The one rule: the tables below (breakpoints, watches, categories) are touched inside calls and in the
	listener only. What (C) keeps for itself (the open clients, the timers) has a lock of its own, a leaf.

	Order. A tick is handed to the server in the place that built it, a call or the listener, and those run one at a
	time, so ticks reach a client in the order they were built. The response of debugger.describe is handed over inside
	the same call as its tick, directly in front of it. Every other response is sent by (C) when its call has returned,
	and a binary frame behind the response it belongs to, to that client only (a tick may come between the two: a
	client pairs a frame with its response by its binaryId). The response of state.recordSgcFrames is deferred: (C)
	sends it when the capture's notice comes (below, "The capture's deferred response").

	No client is a debugging session: connecting stops nothing. What depends on a client being connected is
	the event gates, the activity of breakpoints with the grace period, and the periodic tick; all of them
	follow the core's own count of open clients, never net::Server::HasClients().

	JSON. Requests are parsed with nlohmann's parser. Everything that is sent is written by JsonOut below: it
	has no null, writes keys in a fixed order and nothing but ASCII, so that building a tick cannot fail on
	the content of a string.
*/

namespace dbg { namespace core {

using nlohmann::json;

namespace {

typedef std::chrono::steady_clock Clock;

const size_t kSlowClientBytes = 1u << 20;		// a periodic tick is not queued behind more than this
const size_t kClientTextMax = 256;				// of the "client" text of a describe, in the line on stdout
const int kRetryMs = 250;						// a table call the emulator thread did not take is made again

/*
	Text
*/

bool Printable(unsigned char c)
{
	return c >= 0x20 && c <= 0x7E;
}

// Writes JSON text. Values of an object are written as Key() and the value; a comma comes by itself.
class JsonOut
{
public:
	std::string text;

	void BeginObject() { Value(); text += '{'; first_.push_back(true); }
	void EndObject()   { text += '}'; first_.pop_back(); }
	void BeginArray()  { Value(); text += '['; first_.push_back(true); }
	void EndArray()    { text += ']'; first_.pop_back(); }

	void Key(const char* key)
	{
		Comma();
		Quote(key, strlen(key));
		text += ':';
		after_key_ = true;
	}

	void String(const std::string& s) { Value(); Quote(s.data(), s.size()); }
	void String(const char* s)        { Value(); Quote(s, strlen(s)); }
	void Number(u64 v)                { Value(); text += std::to_string(v); }
	void Signed(s64 v)                { Value(); text += std::to_string(v); }
	void Bool(bool b)                 { Value(); text += b ? "true" : "false"; }
	void Raw(const std::string& s)    { Value(); text += s; }		// JSON text that is complete already

private:
	void Comma()
	{
		if (first_.empty())
			return;

		if (!first_.back())
			text += ',';

		first_.back() = false;
	}

	void Value()
	{
		if (after_key_)
			after_key_ = false;
		else
			Comma();
	}

	// Nothing but ASCII leaves: a byte above 0x7E is '?'. A control character is escaped; text of guest or
	// host origin has none when it gets here (PrintableAscii)
	void Quote(const char* s, size_t n)
	{
		text += '"';

		for (size_t i = 0; i < n; i++)
		{
			const unsigned char c = (unsigned char)s[i];

			if (c == '"' || c == '\\')
			{
				text += '\\';
				text += (char)c;
			}
			else if (c == '\n')
			{
				text += "\\n";
			}
			else if (c < 0x20)
			{
				char escape[8];
				snprintf(escape, sizeof(escape), "\\u%04x", c);
				text += escape;
			}
			else if (c > 0x7E)
			{
				text += '?';
			}
			else
			{
				text += (char)c;
			}
		}

		text += '"';
	}

	std::vector<bool> first_;
	bool after_key_ = false;
};

u64 WallClockMs()
{
	return (u64)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

const char* ErrorText(int code)
{
	switch (code)
	{
	case kErrParse:          return "parse error";
	case kErrInvalidRequest: return "invalid request";
	case kErrInternal:       return "internal error";
	case kErrBusy:           return "emulator busy";
	case kErrHostStopped:    return "emulator stopped by host UI";
	case kErrNoGame:         return "no game loaded";
	case kErrNotPaused:      return "not paused";
	case kErrNotSupported:   return "not supported";
	case kErrCannotStep:     return "cannot step";
	case kErrNotReadable:    return "address not readable";
	case kErrNoCapture:      return "no capture available";
	case kErrNotClocked:     return "arm7 held in reset";
	case kErrNotWritable:    return "not writable";
	case kErrCaptureTimeout: return "capture timed out";
	}

	return "error";
}

/*
	The targets (design 3.1). The table is normative but for label and the default addresses
*/
struct TargetInfo {
	Target target;
	const char* id;
	const char* label;
	const char* pcPath;
	const char* pcRegister;
	const char* breakpointEvent;
	u32 instructionSize;
	u32 addressMax;
	u32 addressRadix;
	u32 addressDigits;
	u32 defaultDisassemblyAddress;
	u32 defaultMemoryAddress;
	bool memory;
};

// Whether a target steps and takes code breakpoints is not in the table: emu::TargetSteps() says it
const TargetInfo kTargets[kTargetCount] = {
	{ Target::Sh4,  "sh4",  "SH4",  "dc.sh4.cpu",   "PC",   kEventSh4Pc,   2, 0xFFFFFFFF, 16, 8, 0x8C010000, 0x8C010000, true  },
	{ Target::Arm7, "arm7", "ARM7", "dc.aica.arm7", "PC",   kEventArm7Pc,  4, 0x00FFFFFF, 16, 8, 0x00000000, 0x00000000, true  },
	{ Target::Dsp,  "dsp",  "DSP",  "dc.aica.dsp",  "STEP", kEventDspStep, 1, 127,        10, 3, 0,          0,          false },
};

const TargetInfo& InfoOf(Target t)
{
	return kTargets[(u32)t];
}

const TargetInfo* FindTarget(const std::string& id)
{
	for (const TargetInfo& info : kTargets)
	{
		if (id == info.id)
			return &info;
	}

	return nullptr;
}

/*
	The tables. Touched inside calls and in the listener only (the one rule).
*/
struct Breakpoint {
	u32 id;
	bool code;				// kind "code"; otherwise "event"
	Target target;			// code
	u32 address;			// code: as the client gave it
	EventId event;			// event
	bool enabled;
};

struct Watch {
	u32 id;
	std::string expression;			// lower case
	const regs::Node* node;			// the constant tables: valid for the whole process
	u32 index;
};

struct CategoryState {
	bool muted;
	bool soloed;
};

// Nothing of the core is destroyed when the process ends: a thread of the debugger may still be at work in
// an exit that did not go through dbg::Term. What has a destructor is made once and stays
std::map<u32, Breakpoint>& breakpoints = *new std::map<u32, Breakpoint>();		// by id
u32 next_breakpoint_id = 1;					// ids are never used twice in a process
std::vector<Watch>& watches = *new std::vector<Watch>();						// in id order
u32 next_watch_id = 1;
CategoryState categories[kCategoryCount];

// Is a client connected, and has the grace period behind the last one expired: written by (C), or by the
// selftest in place of it; read where the gates and the active mask are worked out
std::atomic<u32> client_count { 0 };
std::atomic<bool> grace_expired { false };

u32 last_active_mask;		// what emu::SetActiveMask() was last given; with the tables

// The events debugger.describe lists: a breakpoint is taken on each of them, and on no other name
struct ListedEvents {
	bool listed[EV_ID_COUNT];

	ListedEvents()
	{
		memset(listed, 0, sizeof(listed));

		u32 count;
		const regs::Node* nodes = regs::Nodes(&count);

		for (u32 i = 0; i < count; i++)
		{
			for (u32 e = 0; e < nodes[i].eventCount; e++)
			{
				if (nodes[i].events[e] < EV_ID_COUNT)
					listed[nodes[i].events[e]] = true;
			}
		}
	}
};

bool EventListed(EventId id)
{
	static const ListedEvents events;

	return (unsigned)id < EV_ID_COUNT && events.listed[id];
}

// The category of a breakpoint is Events for kind "event" and that of its target for a code breakpoint. The
// core does not work it out: dbg::emu and dbg::sh4 test the bit of their own category at each arrival, in the
// mask below.

// The categories whose breakpoints can stop now: not muted, soloed or none soloed, and a client connected
// or the grace period behind the last one not over
u32 ActiveMask()
{
	if (client_count.load() == 0 && grace_expired.load())
		return 0;

	bool any_soloed = false;

	for (const CategoryState& c : categories)
		any_soloed = any_soloed || c.soloed;

	u32 mask = 0;

	for (u32 i = 0; i < kCategoryCount; i++)
	{
		if (!categories[i].muted && (!any_soloed || categories[i].soloed))
			mask |= CategoryBit((Category)i);
	}

	return mask;
}

// Inside a table call. The gates and the active mask follow the tables and the count of clients
void PushPresence()
{
	bool has_breakpoint[EV_ID_COUNT] = { };

	for (const auto& entry : breakpoints)
	{
		if (!entry.second.code)
			has_breakpoint[entry.second.event] = true;
	}

	const bool connected = client_count.load() != 0;
	const EventInfo* table = EventTable();

	// 1 while a client is connected and the event is low-rate or has an event breakpoint, enabled or not
	for (u32 id = 0; id < EV_ID_COUNT; id++)
		g_event_on[id].store(connected && (table[id].rate == EVR_LOW || has_breakpoint[id]) ? 1 : 0, std::memory_order_relaxed);

	last_active_mask = ActiveMask();
	emu::SetActiveMask(last_active_mask);
}

// Inside a table call that changed a breakpoint: the enabled sets, all of them, and what follows them
void PushBreakpoints()
{
	std::vector<u32> sh4_ids, sh4_addrs, arm7_ids, arm7_addrs, dsp_ids, dsp_steps, event_ids;
	std::vector<u8> events;

	for (const auto& entry : breakpoints)
	{
		const Breakpoint& bp = entry.second;

		if (!bp.enabled)
			continue;

		if (!bp.code)
		{
			event_ids.push_back(bp.id);
			events.push_back((u8)bp.event);
		}
		else if (bp.target == Target::Sh4)
		{
			sh4_ids.push_back(bp.id);
			sh4_addrs.push_back(bp.address);
		}
		else if (bp.target == Target::Arm7)
		{
			arm7_ids.push_back(bp.id);
			arm7_addrs.push_back(bp.address);
		}
		else if (bp.target == Target::Dsp)
		{
			// the address of a code breakpoint of the DSP is its step, 0..127
			dsp_ids.push_back(bp.id);
			dsp_steps.push_back(bp.address);
		}
	}

	emu::SetSh4Breakpoints(sh4_ids.data(), sh4_addrs.data(), (u32)sh4_ids.size());
	emu::SetArm7Breakpoints(arm7_ids.data(), arm7_addrs.data(), (u32)arm7_ids.size());
	emu::SetDspBreakpoints(dsp_ids.data(), dsp_steps.data(), (u32)dsp_ids.size());
	emu::SetEventBreakpoints(event_ids.data(), events.data(), (u32)event_ids.size());

	PushPresence();
}

/*
	The server and its clients
*/
std::atomic<net::Server*> server { nullptr };

struct ClientEntry {
	u32 id;
	bool needsFull;		// a periodic tick was not queued for it: the next tick it is sent is a full one
	u32 binaryId;		// the binaryId of the last binary frame it was sent: per connection from 1, one counter for
						// the frames of every kind (design 3.4)
};

std::mutex& clients_lock = *new std::mutex();									// a leaf
std::vector<ClientEntry>& clients = *new std::vector<ClientEntry>();			// the open clients, as (C) has heard of them

std::atomic<u32> tick_id { 0 };

// For the selftest, whose session has no server: what takes the ticks that would be broadcast (SetTickSinkForTest)
std::atomic<void (*)(const std::string& tick)> tick_sink { nullptr };

// For the selftest, whose session has no server: what takes what would be sent to one client, a response or a
// binary frame (SetSendSinkForTest)
std::atomic<void (*)(u32 client, const std::string& message, bool binary)> send_sink { nullptr };

// The selftest's own requests (client 0, HandleRequestText) have no connection: the counter of their binaryIds
u32 selftest_binary_id;

u32 tick_ms = kDefaultTickMs;
u32 full_tick_every = kDefaultFullTickEvery;

void SendTo(u32 client, const std::string& text)
{
	net::Server* s = server.load();

	if (s != nullptr)
	{
		if (client != 0)
			s->SendText(client, text);
	}
	else if (auto sink = send_sink.load())
	{
		sink(client, text, false);
	}
}

void SendBinaryTo(u32 client, std::string bytes)
{
	net::Server* s = server.load();

	if (s != nullptr)
	{
		if (client != 0)
			s->SendBinary(client, std::move(bytes));
	}
	else if (auto sink = send_sink.load())
	{
		sink(client, bytes, true);
	}
}

// The binaryId of the next binary frame to a client: per connection from 1, one counter for the frames of every
// kind. A client that is gone gets 0, the id of nobody: what it is sent is dropped
u32 NextBinaryId(u32 client)
{
	std::lock_guard<std::mutex> lock(clients_lock);

	for (ClientEntry& c : clients)
	{
		if (c.id == client)
			return ++c.binaryId;
	}

	return client == 0 ? ++selftest_binary_id : 0;
}

// The 16 bytes in front of the payload of a binary frame (design 3.4): "NDB3", then kind, binaryId and
// payloadBytes, each a little-endian u32. frame has room for them at its start
void PutBinaryHeader(std::string& frame, u32 kind, u32 binaryId, u32 payloadBytes)
{
	const u32 words[3] = { kind, binaryId, payloadBytes };

	frame[0] = 'N';
	frame[1] = 'D';
	frame[2] = 'B';
	frame[3] = '3';

	for (u32 w = 0; w < 3; w++)
	{
		for (u32 b = 0; b < 4; b++)
			frame[4 + w * 4 + b] = (char)(u8)(words[w] >> (8 * b));
	}
}

/*
	Values
*/
void WriteRegisterValue(JsonOut& w, const regs::Node& node, u32 index, bool readable)
{
	const regs::Register& reg = node.registers[index];

	if (readable)
	{
		if (reg.width == 0)
		{
			char text[128];

			if (regs::Read(node, index, nullptr, text, sizeof(text)))
			{
				w.String(PrintableAscii(text, strlen(text)));
				return;
			}
		}
		else
		{
			u64 value;

			if (regs::Read(node, index, &value, nullptr, 0))
			{
				char text[24];

				regs::FormatValue(reg.width, value, text);
				w.String(text);
				return;
			}
		}
	}

	w.String("n/a");
}

// The registers of a node. describe: as debugger.describe has them, where a register that state.setRegister can
// write says so; a tick carries values, and no flags
void WriteRegisters(JsonOut& w, const regs::Node& node, bool readable, bool describe)
{
	w.BeginArray();

	for (u32 i = 0; i < node.registerCount; i++)
	{
		w.BeginObject();
		w.Key("name");
		w.String(node.registers[i].name);
		w.Key("value");
		WriteRegisterValue(w, node, i, readable);
		w.Key("width");
		w.Number(node.registers[i].width);

		if (describe && (node.registers[i].flags & regs::kRegWritable))
		{
			w.Key("flags");
			w.BeginObject();
			w.Key("writable");
			w.Bool(true);
			w.EndObject();
		}

		w.EndObject();
	}

	w.EndArray();
}

// The frames of a call stack, as a tick and state.getCallstack have them
void WriteFrames(JsonOut& w, Target t, u32 maxFrames)
{
	callstack::Frame frames[kMaxCallstackFrames];
	const u32 n = callstack::Unwind(t, frames, maxFrames);

	w.BeginArray();

	for (u32 i = 0; i < n; i++)
	{
		const callstack::Frame& f = frames[i];

		w.BeginObject();
		w.Key("index");
		w.Number(i);
		w.Key("pc");
		w.Number(f.pc);
		w.Key("sp");
		w.Number(f.sp);

		// the one symbol table is the SH4's
		if (t == Target::Sh4)
		{
			char symbol[512];

			if (symbols::Format(f.pc, symbol, sizeof(symbol)))
			{
				w.Key("symbol");
				w.String(PrintableAscii(symbol, strlen(symbol)));
			}
		}

		if (f.kind == callstack::FrameKind::Link)
		{
			w.Key("location");
			w.String(t == Target::Sh4 ? "pr" : "lr");
		}
		else if (f.kind == callstack::FrameKind::Stack)
		{
			char location[32];

			snprintf(location, sizeof(location), "stack+0x%x", f.stackOffset);
			w.Key("location");
			w.String(location);
		}

		w.EndObject();
	}

	w.EndArray();
}

// Is the SH4 context incomplete where a running machine is read (the slice hook)?
bool Sh4ContextStale(const emu::RunState& state)
{
	if (state.lifetime != Lifetime::Running)
		return false;

	emu::Sh4EngineInfo engine;

	return emu::GetSh4Engine(&engine) && !(engine.caps & SH4DBG_CTX_AT_SLICE);
}

std::string NoticeText(u32 notice)
{
	std::string text;

	auto line = [&text](const std::string& statement) {
		if (!text.empty())
			text += '\n';
		text += statement.substr(0, kNoticeLineMax);
	};

	if (notice & NOTICE_SH4_FALLBACK)
	{
		emu::Sh4EngineInfo engine;
		const char* backend = emu::GetSh4Engine(&engine) ? engine.backend : "jit";

		line(std::string("SH4 runs on the interpreter while breakpoints are armed (JIT backend ") + backend
			+ " has no native breakpoints): FPU results can differ from a run without breakpoints");
	}

	if (notice & NOTICE_SH4_FALLBACK_TIMING)
		line("SH4 runs on the interpreter fallback: timing differs");

	if (notice & NOTICE_ARM7_FALLBACK)
		line("ARM7 runs on the interpreter fallback: timing differs");

	return text;
}

/*
	The tick (design 3.2, 2.8, 2.9). On (E), or under L while no emulator thread exists: everything in it
	belongs to one instant.
*/
std::string BuildTick(const emu::RunState& state, bool full)
{
	const bool machine = state.lifetime != Lifetime::NoMachine;
	const bool running = state.lifetime == Lifetime::Running;
	const bool stale = Sh4ContextStale(state);

	// a paused tick is full
	if (!running)
		full = true;

	JsonOut w;

	// the 2949 registers of the tree are 124 KiB of a full tick, the four base paths 6 KiB
	w.text.reserve(full ? 144 * 1024 : 12 * 1024);

	w.BeginObject();
	w.Key("jsonrpc");
	w.String("2.0");
	w.Key("method");
	w.String("event.tick");
	w.Key("params");
	w.BeginObject();

	w.Key("tickId");
	w.Number(tick_id.fetch_add(1));
	w.Key("timestamp");
	w.Number(WallClockMs());

	w.Key("executionState");
	w.BeginObject();
	w.Key("state");
	w.String(running ? "running" : "paused");
	w.Key("stopId");
	w.Number(state.stopId);

	if (!running)
	{
		w.Key("reason");
		w.String(StopCauseName(state.cause));

		// for as long as the breakpoint of the stop exists
		if ((state.cause == StopCause::Breakpoint || state.cause == StopCause::Event) && breakpoints.count(state.breakpointId) != 0)
		{
			w.Key("breakpointId");
			w.Number(state.breakpointId);
		}
	}

	if (stale)
	{
		w.Key("registersStale");
		w.Bool(true);
	}

	if (machine && state.notice != 0)
	{
		const std::string notice = NoticeText(state.notice);

		if (!notice.empty())
		{
			w.Key("notice");
			w.String(notice);
		}
	}

	w.EndObject();

	// no game: no registers, and the client drops what it holds
	w.Key("registers");
	w.BeginObject();

	if (machine)
	{
		u32 count;
		const regs::Node* nodes = regs::Nodes(&count);

		for (u32 i = 0; i < count; i++)
		{
			const regs::Node& node = nodes[i];

			if (node.registerCount == 0)
				continue;

			if (!full && !(node.flags & regs::kNodeBase))
				continue;

			if (stale && (node.flags & regs::kNodeSh4Context))
				continue;

			w.Key(node.path);
			WriteRegisters(w, node, true, false);
		}
	}

	w.EndObject();

	w.Key("breakpoints");
	w.BeginObject();

	for (const auto& entry : breakpoints)
	{
		const Breakpoint& bp = entry.second;

		w.Key(std::to_string(bp.id).c_str());
		w.BeginObject();
		w.Key("id");
		w.Number(bp.id);
		w.Key("event");
		w.String(bp.code ? InfoOf(bp.target).breakpointEvent : EventTable()[bp.event].name);

		if (bp.code)
		{
			w.Key("address");
			w.Number(bp.address);
		}

		w.Key("kind");
		w.String(bp.code ? "code" : "event");
		w.Key("enabled");
		w.Bool(bp.enabled);
		w.EndObject();
	}

	w.EndObject();

	w.Key("eventLog");
	w.BeginArray();

	{
		std::vector<events::Entry> log;
		const EventInfo* table = EventTable();

		events::Snapshot(&log);

		for (const events::Entry& entry : log)
		{
			w.BeginObject();
			w.Key("eventId");
			w.String(std::to_string(entry.seq));
			w.Key("timestamp");
			w.Number(entry.timestampMs);
			w.Key("subsystem");
			w.String(table[entry.id].subsystem);
			w.Key("severity");
			w.String(table[entry.id].severity);
			w.Key("message");
			w.String(PrintableAscii(entry.message.data(), entry.message.size()));
			w.Key("metadata");
			w.BeginObject();
			w.Key("event");
			w.String(table[entry.id].name);
			w.Key("cycles");
			w.String(std::to_string(entry.cycles));
			w.EndObject();
			w.EndObject();
		}
	}

	w.EndArray();

	// evaluated in every tick, whatever paths it carries
	w.Key("watches");
	w.BeginArray();

	for (const Watch& watch : watches)
	{
		w.BeginObject();
		w.Key("id");
		w.Number(watch.id);
		w.Key("expression");
		w.String(watch.expression);
		w.Key("value");
		WriteRegisterValue(w, *watch.node, watch.index, machine && !(stale && (watch.node->flags & regs::kNodeSh4Context)));
		w.EndObject();
	}

	w.EndArray();

	w.Key("callstacks");
	w.BeginObject();
	w.Key("sh4");

	if (machine && !running)
		WriteFrames(w, Target::Sh4, kDefaultCallstackFrames);
	else
		w.Raw("[]");

	w.Key("arm7");

	if (machine && !running)
		WriteFrames(w, Target::Arm7, kDefaultCallstackFrames);
	else
		w.Raw("[]");

	w.EndObject();

	w.Key("categories");
	w.BeginObject();

	for (u32 i = 0; i < kCategoryCount; i++)
	{
		w.Key(CategoryName((Category)i));
		w.BeginObject();
		w.Key("muted");
		w.Bool(categories[i].muted);
		w.Key("soloed");
		w.Bool(categories[i].soloed);
		w.EndObject();
	}

	w.EndObject();

	w.EndObject();
	w.EndObject();

	return std::move(w.text);
}

// A tick to every client, from the place that built it: after a state change and after a request that
// changed the tables. Never dropped. A running tick has the base paths, unless a client is owed a full one
void BroadcastTick(const emu::RunState& state)
{
	net::Server* s = server.load();

	if (s == nullptr)
	{
		// a session of the selftest: the tick every client would get, to the test that asked for it
		if (auto sink = tick_sink.load())
			sink(BuildTick(state, false));

		return;
	}

	bool full = false;

	{
		std::lock_guard<std::mutex> lock(clients_lock);

		if (clients.empty())
			return;

		for (ClientEntry& c : clients)
		{
			full = full || c.needsFull;
			c.needsFull = false;
		}
	}

	s->BroadcastText(BuildTick(state, full));
}

/*
	debugger.describe (design 3.1)
*/
void WriteNode(JsonOut& w, const regs::Node* nodes, u32 count, u32 at, bool machine)
{
	const regs::Node& node = nodes[at];

	w.BeginObject();
	w.Key("path");
	w.String(node.path);
	w.Key("label");
	w.String(node.label);
	w.Key("description");
	w.String(node.description);

	// an empty array is left out
	if (node.registerCount != 0)
	{
		w.Key("registers");
		WriteRegisters(w, node, machine, true);
	}

	if (node.eventCount != 0)
	{
		const EventInfo* table = EventTable();

		w.Key("events");
		w.BeginArray();
		for (u32 i = 0; i < node.eventCount; i++)
			w.String(table[node.events[i]].name);
		w.EndArray();
	}

	if (node.actionCount != 0)
	{
		w.Key("actions");
		w.BeginArray();
		for (u32 i = 0; i < node.actionCount; i++)
			w.String(node.actions[i]);
		w.EndArray();
	}

	// the nodes that name this one as their parent, in the order of the table
	bool any = false;

	for (u32 i = at + 1; i < count; i++)
	{
		if (nodes[i].parent == nullptr || strcmp(nodes[i].parent, node.path) != 0)
			continue;

		if (!any)
		{
			any = true;
			w.Key("children");
			w.BeginArray();
		}

		WriteNode(w, nodes, count, i, machine);
	}

	if (any)
		w.EndArray();

	w.EndObject();
}

std::string BuildDescribe(const emu::RunState& state)
{
	const bool machine = state.lifetime != Lifetime::NoMachine;

	JsonOut w;

	// the tree is 136 KiB
	w.text.reserve(160 * 1024);

	w.BeginObject();

	w.Key("emulator");
	w.BeginObject();
	w.Key("name");
	w.String("nullDC");
	w.Key("version");
	w.String("3.0.0");
	w.Key("build");
	w.String("native");
	w.EndObject();

	w.Key("protocol");
	w.Number((u64)kProtocolVersion);

	w.Key("ui");
	{
		const char* version = webui::Version();

		w.String(PrintableAscii(version, strlen(version)));
	}

	w.Key("capabilities");
	w.BeginArray();
	w.String("event-breakpoints");
	w.String("callstack");

	// milestone M2: each with its method (state.writeMemory; state.setRegister and state.editWatch; control.reset)
	w.String("memory-write");
	w.String("register-write");
	w.String("reset");

	// milestone M3: each with its method (state.recordSgcFrames and state.getSgcFrameData; state.getMemoryBinary)
	w.String("sgc-capture");
	w.String("memory-binary");

	// what executes SH4 breakpoints and steps, once a backend is selected
	if (machine)
	{
		emu::Sh4EngineInfo engine;

		if (emu::GetSh4Engine(&engine))
			w.String(engine.fallback ? "sh4-debug-fallback" : "sh4-debug-native");
	}

	w.EndArray();

	w.Key("targets");
	w.BeginArray();

	for (const TargetInfo& t : kTargets)
	{
		w.BeginObject();
		w.Key("id");
		w.String(t.id);
		w.Key("label");
		w.String(t.label);
		w.Key("pcPath");
		w.String(t.pcPath);
		w.Key("pcRegister");
		w.String(t.pcRegister);
		w.Key("breakpointEvent");
		w.String(t.breakpointEvent);
		w.Key("instructionSize");
		w.Number(t.instructionSize);
		w.Key("addressMax");
		w.Number(t.addressMax);
		w.Key("addressRadix");
		w.Number(t.addressRadix);
		w.Key("addressDigits");
		w.Number(t.addressDigits);
		w.Key("defaultDisassemblyAddress");
		w.Number(t.defaultDisassemblyAddress);

		// absent exactly when the target has no memory
		if (t.memory)
		{
			w.Key("defaultMemoryAddress");
			w.Number(t.defaultMemoryAddress);
		}

		w.Key("memory");
		w.Bool(t.memory);
		w.Key("stepping");
		w.Bool(emu::TargetSteps(t.target));
		w.EndObject();
	}

	w.EndArray();

	w.Key("deviceTree");
	w.BeginArray();

	{
		u32 count;
		const regs::Node* nodes = regs::Nodes(&count);

		for (u32 i = 0; i < count; i++)
		{
			if (nodes[i].parent == nullptr)
				WriteNode(w, nodes, count, i, machine);
		}
	}

	w.EndArray();
	w.EndObject();

	return std::move(w.text);
}

/*
	Requests
*/
struct Request {
	u32 client;				// 0: nobody (the selftest)
	std::string id;			// the id as JSON text, echoed as it came
	const json* params;		// an object
};

// What a method answers. code 0: `result`, the JSON text of the result, and `binary`, a binary frame (header and
// payload) that goes to the client directly behind the response; otherwise the error, which the dispatcher puts on
// the channel of the method, and nothing behind it. deferred: no response now, the method answers later
// (state.recordSgcFrames)
struct Outcome {
	int code = 0;
	std::string message;
	std::string result = "{}";
	std::string response;		// sent by the method itself (debugger.describe): the text
	bool sent = false;
	std::string binary;
	bool deferred = false;
};

void Fail(Outcome& out, int code, const std::string& message)
{
	out.code = code;
	out.message = message;
}

void Fail(Outcome& out, int code)
{
	Fail(out, code, ErrorText(code));
}

void BadParam(Outcome& out, const std::string& what)
{
	Fail(out, kErrInvalidParams, "invalid params: " + what);
}

void Unsupported(Outcome& out, const std::string& what)
{
	Fail(out, kErrNotSupported, "not supported: " + what);
}

std::string ErrorResponse(const std::string& id, int code, const std::string& message)
{
	JsonOut w;

	w.BeginObject();
	w.Key("jsonrpc");
	w.String("2.0");
	w.Key("id");
	w.Raw(id);
	w.Key("error");
	w.BeginObject();
	w.Key("code");
	w.Signed(code);
	w.Key("message");
	w.String(message.empty() ? std::string(ErrorText(code)) : message);
	w.EndObject();
	w.EndObject();

	return std::move(w.text);
}

std::string ResultResponse(const std::string& id, const std::string& result)
{
	JsonOut w;

	w.text.reserve(result.size() + 64);

	w.BeginObject();
	w.Key("jsonrpc");
	w.String("2.0");
	w.Key("id");
	w.Raw(id);
	w.Key("result");
	w.Raw(result);
	w.EndObject();

	return std::move(w.text);
}

std::string InBandError(int code, const std::string& message)
{
	JsonOut w;

	w.BeginObject();
	w.Key("error");
	w.BeginObject();
	w.Key("code");
	w.Signed(code);
	w.Key("message");
	w.String(message.empty() ? std::string(ErrorText(code)) : message);
	w.EndObject();
	w.EndObject();

	return std::move(w.text);
}

// A call as (C) makes it. On the emulator thread (a selftest stands there, where a call of every class may
// stand, and must not make a call) the closure runs in place, with what a call answers from the published
// state: run control for a thread that is not parked is refused.
int RunCall(emu::CallClass cls, std::function<void()> fn)
{
	if (emu::OnEmuThread())
	{
		if (cls == emu::CC_EXEC && emu::GetRunState().lifetime != Lifetime::Parked)
			return kErrNotPaused;

		fn();
		return 0;
	}

	return emu::Call(cls, std::move(fn));
}

/*
	The capture's deferred response (M3-ROUND, M3-D28). The response of state.recordSgcFrames is sent when the
	capture is complete, and (C) answers one message at a time: the method makes its call and leaves a record, and
	the notice of dbg::sgc (dbg_sgc.h, SetCompletionNotice) queues a message for (C). The notice names no capture;
	what pairs it with its request:
	  - a request whose call armed a capture (sgc::Arm returned 0) leaves the pending record {client, id,
	    deadline} before (C) takes its next message, and one notice owed;
	  - a notice answers the pending record, if one exists, and clears it; one that finds none answers nothing;
	  - while a notice is owed a new state.recordSgcFrames is answered -32003, also when no capture records any
	    more (its notice is queued);
	  - the core does not answer a capture it armed: at the deadline it cancels the capture in a CC_TABLE call
	    (sgc::Cancel(kErrCaptureTimeout)), and the notice answers: -32014, or the result of a capture that was
	    complete in front of the call. Only when that call is not taken (kErrBusy) does (C) answer -32014 itself
	    and clear the record; the notice stays owed, and the cancel is made again kRetryMs later while it is
	    owed. No capture can be armed meanwhile, so it cancels no other.
	All of it is (C)'s, or the dispatching thread's in a session without the core thread (where nothing takes a
	notice: a selftest that arms a capture through the core starts the core thread, StartForTest). What a
	selftest reads of it from another thread is atomic.
*/
struct CaptureRequests {
	bool pending = false;				// the pending record
	u32 client = 0;
	std::string id;
	Clock::time_point deadline;
	bool deadlineArmed = false;			// the deadline of the pending record has not come

	bool cancelOwed = false;			// the cancel of the deadline was not taken: it is made again at cancelAt
	Clock::time_point cancelAt;
};

CaptureRequests& capture = *new CaptureRequests();

std::atomic<u32> notices_owed { 0 };

// For the selftest (dbg_core_private.h)
std::atomic<u32> capture_timeout_ms { kCaptureTimeoutMs };
std::atomic<void (*)()> before_arm { nullptr };
std::atomic<void (*)()> before_table_call { nullptr };
std::atomic<bool> pending_seen { false };
std::atomic<u32> notices_taken { 0 }, notices_unpaired { 0 }, deadlines_made { 0 }, deadlines_taken { 0 }, deadlines_untaken { 0 },
	answered_by_core { 0 }, cancels_again { 0 };

// Answers the pending record, which goes
void AnswerCapture(int answer)
{
	const std::string text = ResultResponse(capture.id, answer == 0 ? std::string("{}") : InBandError(answer, ""));
	const u32 client = capture.client;

	capture.pending = false;
	capture.deadlineArmed = false;
	capture.id.clear();
	pending_seen.store(false);

	SendTo(client, text);
}

// A notice of dbg::sgc, as (C) takes it from its queue
void TakeNotice(int answer)
{
	notices_taken++;

	if (notices_owed.load() != 0)
		notices_owed--;

	if (notices_owed.load() == 0)
		capture.cancelOwed = false;

	if (capture.pending)
		AnswerCapture(answer);
	else
		notices_unpaired++;
}

// The deadline of the pending record: the capture is cancelled, and its notice answers
void CaptureDeadline()
{
	capture.deadlineArmed = false;

	deadlines_made++;

	const int rv = RunCall(emu::CC_TABLE, [] { sgc::Cancel(kErrCaptureTimeout); });

	if (rv == 0)
	{
		deadlines_taken++;
		return;
	}

	// not taken: the core answers, and the cancel is made again while the notice is owed
	deadlines_untaken++;

	if (capture.pending)
	{
		answered_by_core++;
		AnswerCapture(kErrCaptureTimeout);
	}

	if (notices_owed.load() != 0)
	{
		capture.cancelOwed = true;
		capture.cancelAt = Clock::now() + std::chrono::milliseconds(kRetryMs);
	}
}

void CancelAgain()
{
	capture.cancelOwed = false;

	if (notices_owed.load() == 0)
		return;

	cancels_again++;

	if (RunCall(emu::CC_TABLE, [] { sgc::Cancel(kErrCaptureTimeout); }) != 0)
	{
		capture.cancelOwed = true;
		capture.cancelAt = Clock::now() + std::chrono::milliseconds(kRetryMs);
	}
}

/*
	Parameters. A named parameter that is missing or has another type, or is outside a bound, is -32602;
	keys this file does not name are not looked at.
*/
const json* Param(const Request& req, const char* key)
{
	auto it = req.params->find(key);

	return it == req.params->end() ? nullptr : &*it;
}

// A JSON number that is an integer and not negative. A client written in JavaScript has one kind of number:
// 16.0 is 16
bool ToUnsigned(const json& j, u64* value)
{
	if (j.is_number_unsigned())
	{
		*value = j.get<u64>();
		return true;
	}

	if (j.is_number_integer())
	{
		const s64 v = j.get<s64>();

		if (v < 0)
			return false;

		*value = (u64)v;
		return true;
	}

	if (j.is_number_float())
	{
		const double d = j.get<double>();

		if (!(d >= 0) || d > 18446744073709549568.0 || d != std::floor(d))
			return false;

		*value = (u64)d;
		return true;
	}

	return false;
}

// An unsigned integer in [min, max]. Returns false with the outcome set; *present says whether the key was
// there, for an optional parameter (present == nullptr: the parameter is required)
bool ParamUnsigned(const Request& req, Outcome& out, const char* key, u64 min, u64 max, u64* value, bool* present = nullptr)
{
	const json* j = Param(req, key);

	if (present != nullptr)
		*present = j != nullptr;

	if (j == nullptr)
	{
		if (present != nullptr)
			return true;

		BadParam(out, std::string(key) + " is missing");
		return false;
	}

	u64 v;

	if (!ToUnsigned(*j, &v))
	{
		BadParam(out, std::string(key) + " is not an unsigned integer");
		return false;
	}

	if (v < min || v > max)
	{
		BadParam(out, std::string(key) + " is outside " + std::to_string(min) + ".." + std::to_string(max));
		return false;
	}

	*value = v;
	return true;
}

bool ParamAddress(const Request& req, Outcome& out, u32* address, bool* present = nullptr)
{
	u64 v = 0;

	if (!ParamUnsigned(req, out, "address", 0, 0xFFFFFFFFu, &v, present))
		return false;

	*address = (u32)v;
	return true;
}

bool ParamString(const Request& req, Outcome& out, const char* key, std::string* value, bool* present = nullptr)
{
	const json* j = Param(req, key);

	if (present != nullptr)
		*present = j != nullptr;

	if (j == nullptr)
	{
		if (present != nullptr)
			return true;

		BadParam(out, std::string(key) + " is missing");
		return false;
	}

	if (!j->is_string())
	{
		BadParam(out, std::string(key) + " is not a string");
		return false;
	}

	*value = j->get_ref<const std::string&>();
	return true;
}

bool ParamBool(const Request& req, Outcome& out, const char* key, bool* value, bool* present = nullptr)
{
	const json* j = Param(req, key);

	if (present != nullptr)
		*present = j != nullptr;

	if (j == nullptr)
	{
		if (present != nullptr)
			return true;

		BadParam(out, std::string(key) + " is missing");
		return false;
	}

	if (!j->is_boolean())
	{
		BadParam(out, std::string(key) + " is not a boolean");
		return false;
	}

	*value = j->get<bool>();
	return true;
}

// "target": one of the three ids. With a default it may be absent
bool ParamTarget(const Request& req, Outcome& out, const TargetInfo* fallback, const TargetInfo** target)
{
	std::string id;
	bool present;

	if (!ParamString(req, out, "target", &id, fallback != nullptr ? &present : nullptr))
		return false;

	if (fallback != nullptr && !present)
	{
		*target = fallback;
		return true;
	}

	*target = FindTarget(id);

	if (*target == nullptr)
	{
		BadParam(out, "target is not one of sh4, arm7, dsp");
		return false;
	}

	return true;
}

/*
	debugger.describe: answered from inside the call, with its full tick directly behind it
*/
struct DescribeJob {
	u32 client;
	std::string id;
	bool otherProtocol;
	double protocol;
	std::string response;
	int code = 0;
};

void MethodDescribe(const Request& req, Outcome& out)
{
	auto job = std::make_shared<DescribeJob>();

	job->client = req.client;
	job->id = req.id;
	job->otherProtocol = false;
	job->protocol = 0;

	if (const json* protocol = Param(req, "protocol"))
	{
		if (!protocol->is_number())
			return BadParam(out, "protocol is not a number");

		job->protocol = protocol->get<double>();
		job->otherProtocol = job->protocol != (double)kProtocolVersion;
	}

	if (const json* client = Param(req, "client"))
	{
		if (!client->is_string())
			return BadParam(out, "client is not a string");
	}

	std::function<void()> fn = [job] {
		try
		{
			// a client of another protocol version is answered like any other, and noted
			if (job->otherProtocol)
				DBG_EVENT(EV_DEBUGGER_NOTE, "a client describes itself with protocol %g, this server speaks protocol %d", job->protocol, kProtocolVersion);

			const emu::RunState state = emu::GetRunState();

			job->response = ResultResponse(job->id, BuildDescribe(state));

			net::Server* s = server.load();

			if (s != nullptr && job->client != 0)
			{
				// the response, and the full tick directly behind it: no other tick comes between the two
				s->SendText(job->client, job->response);
				s->SendText(job->client, BuildTick(state, true));

				std::lock_guard<std::mutex> lock(clients_lock);

				for (ClientEntry& c : clients)
				{
					if (c.id == job->client)
						c.needsFull = false;
				}
			}
		}
		catch (...)
		{
			job->code = kErrInternal;
		}
	};

	// a read; without a machine it is answered from the constant tables, and its tick is built in a table call
	int rv = RunCall(emu::CC_READ, fn);

	if (rv == kErrNoGame)
		rv = RunCall(emu::CC_TABLE, fn);

	if (rv != 0)
		return Fail(out, rv);

	if (job->code != 0)
		return Fail(out, job->code);

	out.response = std::move(job->response);
	out.sent = true;
}

/*
	Memory and disassembly
*/
struct MemoryJob {
	const TargetInfo* target;
	u32 address;
	u32 length;
	bool binary = false;			// state.getMemoryBinary: the bytes go into `frame`, behind its header
	std::vector<u8> data;			// state.getMemorySlice: the bytes
	std::string frame;				// state.getMemoryBinary: kBinaryHeaderBytes, then the bytes
	std::vector<u8> readable;		// one bit per byte
	u32 covered = 0;				// bytes of the range that lie at or below the target's last address
	int code = 0;
};

// The read of a slice or of a memory binary: one CC_READ call. Unreadable bytes are 0. Fails the outcome as
// both methods do: by the lifetime, -32004 for a target without memory, -32010 when nothing is readable
bool ReadMemory(const std::shared_ptr<MemoryJob>& job, Outcome& out)
{
	int rv = RunCall(emu::CC_READ, [job] {
		try
		{
			const TargetInfo& t = *job->target;

			if (!t.memory)
			{
				job->code = kErrNotSupported;
				return;
			}

			u8* into;

			if (job->binary)
			{
				job->frame.assign((size_t)kBinaryHeaderBytes + job->length, '\0');
				into = (u8*)&job->frame[kBinaryHeaderBytes];
			}
			else
			{
				job->data.assign(job->length, 0);
				into = job->data.data();
			}

			job->readable.assign((job->length + 7) / 8, 0);

			// A slice does not wrap: a byte above the last address of the target is not readable. The reader
			// wraps at 2^32, so it gets the part below only
			if (job->address <= t.addressMax)
			{
				const u64 room = (u64)t.addressMax - job->address + 1;

				job->covered = room < job->length ? (u32)room : job->length;
			}

			u32 readable = 0;

			if (job->covered != 0)
				readable = mem::Read(t.target, job->address, job->covered, into, job->readable.data());

			if (readable == 0)
				job->code = kErrNotReadable;
		}
		catch (...)
		{
			job->code = kErrInternal;
		}
	});

	if (rv != 0)
	{
		Fail(out, rv);
		return false;
	}

	if (job->code == kErrNotSupported)
	{
		Unsupported(out, std::string("target ") + job->target->id + " has no memory");
		return false;
	}

	if (job->code != 0)
	{
		Fail(out, job->code);
		return false;
	}

	return true;
}

// "unreadable": the runs of bytes that could not be read, as [offset, length]; left out when there are none
void WriteUnreadable(JsonOut& w, const MemoryJob& job)
{
	bool any = false;

	for (u32 i = 0; i < job.length; )
	{
		auto unreadable = [&job](u32 at) { return at >= job.covered || !(job.readable[at >> 3] & (1 << (at & 7))); };

		if (!unreadable(i))
		{
			i++;
			continue;
		}

		u32 end = i + 1;

		while (end < job.length && unreadable(end))
			end++;

		if (!any)
		{
			any = true;
			w.Key("unreadable");
			w.BeginArray();
		}

		w.BeginArray();
		w.Number(i);
		w.Number(end - i);
		w.EndArray();

		i = end;
	}

	if (any)
		w.EndArray();
}

void MethodGetMemorySlice(const Request& req, Outcome& out)
{
	auto job = std::make_shared<MemoryJob>();
	u64 length;

	if (!ParamTarget(req, out, &kTargets[0], &job->target) || !ParamAddress(req, out, &job->address)
		|| !ParamUnsigned(req, out, "length", 1, kMaxMemorySliceBytes, &length))
		return;

	job->length = (u32)length;

	if (!ReadMemory(job, out))
		return;

	JsonOut w;

	w.text.reserve((size_t)job->length * 4 + 128);

	w.BeginObject();
	w.Key("baseAddress");
	w.Number(job->address);
	w.Key("data");
	w.BeginArray();
	for (u32 i = 0; i < job->length; i++)
		w.Number(job->data[i]);
	w.EndArray();
	w.Key("validity");
	w.String("ok");
	WriteUnreadable(w, *job);
	w.EndObject();

	out.result = std::move(w.text);
}

// state.getMemorySlice with the bound of a memory binary, and the bytes in a binary frame of kind 2 behind the
// response (M3-ROUND, M3-D36): {baseAddress, totalBytes, unreadable?, binaryId}, no validity
void MethodGetMemoryBinary(const Request& req, Outcome& out)
{
	auto job = std::make_shared<MemoryJob>();
	u64 length;

	if (!ParamTarget(req, out, &kTargets[0], &job->target) || !ParamAddress(req, out, &job->address)
		|| !ParamUnsigned(req, out, "length", 1, kMaxMemoryBinaryBytes, &length))
		return;

	job->length = (u32)length;
	job->binary = true;

	if (!ReadMemory(job, out))
		return;

	const u32 id = NextBinaryId(req.client);

	PutBinaryHeader(job->frame, kBinaryMemory, id, job->length);

	JsonOut w;

	w.BeginObject();
	w.Key("baseAddress");
	w.Number(job->address);
	w.Key("totalBytes");
	w.Number(job->length);
	WriteUnreadable(w, *job);
	w.Key("binaryId");
	w.Number(id);
	w.EndObject();

	out.result = std::move(w.text);
	out.binary = std::move(job->frame);
}

bool SymbolCallback(void*, uint32_t addr, const char** name, uint32_t* offset)
{
	return symbols::Lookup(addr, name, offset);
}

bool ReadCallback(void* user, uint32_t addr, uint32_t size, uint32_t* value)
{
	const Target t = *(const Target*)user;
	u8 bytes[4];

	if (size != 1 && size != 2 && size != 4)
		return false;

	if (mem::Read(t, addr, size, bytes, nullptr) != size)
		return false;

	u32 v = 0;

	for (u32 i = 0; i < size; i++)
		v |= (u32)bytes[i] << (8 * i);

	*value = v;
	return true;
}

void HexPairs(std::string& out, const u8* bytes, u32 n)
{
	static const char digits[] = "0123456789ABCDEF";

	for (u32 i = 0; i < n; i++)
	{
		if (i != 0)
			out += ' ';
		out += digits[bytes[i] >> 4];
		out += digits[bytes[i] & 15];
	}
}

struct DisasmLine {
	u32 address;
	std::string bytes;
	std::string text;
};

struct DisasmJob {
	const TargetInfo* target;
	u32 address;
	u32 count;
	std::vector<DisasmLine> lines;
	int code = 0;
	std::string message;
};

const u16 kReiosTrap = 0x085B;		// executed by the opcode table as an HLE call; no SH4 instruction

// One line, on (E) or under L. The text is what dbg_disasm prints
void DisassembleLine(Target t, u32 address, DisasmLine* line)
{
	Target user = t;
	u8 bytes[8];

	line->address = address;

	switch (t)
	{
	case Target::Sh4:
	{
		if (mem::Read(t, address, 2, bytes, nullptr) != 2)
			break;

		const u16 op = (u16)(bytes[0] | (bytes[1] << 8));

		HexPairs(line->bytes, bytes, 2);

		if (op == kReiosTrap)
		{
			line->text = ".word 0x085b ! reios trap";
			return;
		}

		// how FPU instructions read depends on FPSCR
		unsigned fp_mode = 0;

		if (p_sh4rcb != nullptr)
		{
			if (p_sh4rcb->cntx.fpscr.PR)
				fp_mode |= SH4_FP_PR;
			if (p_sh4rcb->cntx.fpscr.SZ)
				fp_mode |= SH4_FP_SZ;
		}

		const DisasmCtx ctx = { SymbolCallback, ReadCallback, &user };
		const std::string text = DisasmSh4(address, op, fp_mode, &ctx, nullptr);

		// a symbol name is bytes of the guest image
		line->text = PrintableAscii(text.data(), text.size());
		return;
	}

	case Target::Arm7:
	{
		if (mem::Read(t, address, 4, bytes, nullptr) != 4)
			break;

		const u32 op = (u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24);

		HexPairs(line->bytes, bytes, 4);

		const DisasmCtx ctx = { nullptr, ReadCallback, &user };
		const std::string text = DisasmArm7(address, op, &ctx, nullptr);

		line->text = PrintableAscii(text.data(), text.size());
		return;
	}

	case Target::Dsp:
	{
		DSP* dsp = sh4_cpu != nullptr ? sh4_cpu->GetA0H<DSP>(A0H_DSP) : nullptr;
		u16 mpro[4];

		if (dsp == nullptr || !dsp->DebugGetProgramStep(address, mpro))
			break;

		// the four words in register order, each with its high byte first
		for (u32 i = 0; i < 4; i++)
		{
			bytes[i * 2] = (u8)(mpro[i] >> 8);
			bytes[i * 2 + 1] = (u8)mpro[i];
		}

		HexPairs(line->bytes, bytes, 8);
		line->text = DisasmDsp(address, mpro);
		return;
	}
	}

	line->bytes = "??";
	line->text = "<unreadable>";
}

void MethodGetDisassembly(const Request& req, Outcome& out)
{
	auto job = std::make_shared<DisasmJob>();
	u64 count;

	if (!ParamTarget(req, out, &kTargets[0], &job->target) || !ParamAddress(req, out, &job->address)
		|| !ParamUnsigned(req, out, "count", 1, kMaxDisassemblyLines, &count))
		return;

	// ignored, but a number where it is given
	if (const json* context = Param(req, "context"))
	{
		if (!context->is_number())
			return BadParam(out, "context is not a number");
	}

	job->count = (u32)count;

	int rv = RunCall(emu::CC_READ, [job] {
		try
		{
			const TargetInfo& t = *job->target;

			if (job->address % t.instructionSize != 0)
			{
				job->code = kErrInvalidParams;
				job->message = std::string("invalid params: address is not a multiple of ") + std::to_string(t.instructionSize);
				return;
			}

			if (job->address > t.addressMax)
			{
				job->code = kErrInvalidParams;
				job->message = std::string("invalid params: address is above the last address of ") + t.id;
				return;
			}

			job->lines.reserve(job->count);

			// `count` lines, fewer only at the last address: no wrap
			for (u32 i = 0; i < job->count; i++)
			{
				const u64 address = (u64)job->address + (u64)i * t.instructionSize;

				if (address + t.instructionSize - 1 > t.addressMax)
					break;

				job->lines.emplace_back();
				DisassembleLine(t.target, (u32)address, &job->lines.back());
			}
		}
		catch (...)
		{
			job->code = kErrInternal;
		}
	});

	if (rv != 0)
		return Fail(out, rv);

	if (job->code != 0)
		return Fail(out, job->code, job->message);

	JsonOut w;

	w.text.reserve(job->lines.size() * 96 + 32);

	w.BeginObject();
	w.Key("lines");
	w.BeginArray();

	for (const DisasmLine& line : job->lines)
	{
		w.BeginObject();
		w.Key("address");
		w.Number(line.address);
		w.Key("bytes");
		w.String(line.bytes);
		w.Key("disassembly");
		w.String(line.text);
		w.EndObject();
	}

	w.EndArray();
	w.EndObject();

	out.result = std::move(w.text);
}

struct TextJob {
	std::string text;
	int code = 0;
	std::string message;
};

void MethodGetCallstack(const Request& req, Outcome& out)
{
	const TargetInfo* target;
	u64 max_frames = kDefaultCallstackFrames;
	bool present;

	if (!ParamTarget(req, out, nullptr, &target))
		return;

	if (target->target == Target::Dsp)
		return BadParam(out, "target is not one of sh4, arm7");

	if (!ParamUnsigned(req, out, "maxFrames", 1, kMaxCallstackFrames, &max_frames, &present))
		return;

	auto job = std::make_shared<TextJob>();
	const Target t = target->target;
	const u32 frames = (u32)max_frames;

	int rv = RunCall(emu::CC_READ, [job, t, frames] {
		try
		{
			JsonOut w;

			// in the slice hook the SH4 is unwound only where its context is complete there
			if (t == Target::Sh4 && Sh4ContextStale(emu::GetRunState()))
				w.Raw("[]");
			else
				WriteFrames(w, t, frames);

			job->text = std::move(w.text);
		}
		catch (...)
		{
			job->code = kErrInternal;
		}
	});

	if (rv != 0)
		return Fail(out, rv);

	if (job->code != 0)
		return Fail(out, job->code);

	JsonOut w;

	w.BeginObject();
	w.Key("target");
	w.String(target->id);
	w.Key("frames");
	w.Raw(job->text);
	w.EndObject();

	out.result = std::move(w.text);
}

/*
	SGC capture (design 3.3, 3.4; M3-ROUND, M3-D27, M3-D28)
*/
struct FrameDataJob {
	std::string frame;		// kBinaryHeaderBytes, then the server's capture
	u32 frames = 0;
	int code = 0;
};

// A read: the server's capture, which is the last one that was complete, also while another records; -32011
// without one. The frames in a binary frame of kind 1 behind the response
void MethodGetSgcFrameData(const Request& req, Outcome& out)
{
	auto job = std::make_shared<FrameDataJob>();

	int rv = RunCall(emu::CC_READ, [job] {
		try
		{
			u32 frames = 0;

			sgc::GetState(&frames);

			if (frames == 0)
			{
				job->code = kErrNoCapture;
				return;
			}

			job->frame.reserve((size_t)kBinaryHeaderBytes + (size_t)frames * sgc::kBytesPerFrame);
			job->frame.assign(kBinaryHeaderBytes, '\0');
			job->frames = sgc::Copy(&job->frame);

			if (job->frames == 0)
				job->code = kErrNoCapture;
		}
		catch (...)
		{
			job->code = kErrInternal;
		}
	});

	if (rv != 0)
		return Fail(out, rv);

	if (job->code != 0)
		return Fail(out, job->code);

	const u32 total = job->frames * sgc::kBytesPerFrame;
	const u32 id = NextBinaryId(req.client);

	PutBinaryHeader(job->frame, kBinarySgc, id, total);

	JsonOut w;

	w.BeginObject();
	w.Key("frameCount");
	w.Number(job->frames);
	w.Key("channelsPerFrame");
	w.Number(sgc::kChannelsPerFrame);
	w.Key("bytesPerChannel");
	w.Number(sgc::kBytesPerChannel);
	w.Key("totalBytes");
	w.Number(total);
	w.Key("binaryId");
	w.Number(id);
	w.EndObject();

	out.result = std::move(w.text);
	out.binary = std::move(job->frame);
}

struct CaptureJob {
	u32 frames;
	int armed = kErrInternal;	// what sgc::Arm returned; kErrInternal while it was not called, and where it threw
	bool refused = false;		// emu::Resume() refused behind the arm, which was cancelled again
	bool parked = false;		// a table closure found a stop in force: the exec call is made
	bool noThread = false;		// a table closure ran on its caller: no emulator thread exists
};

// How often a request that finds the other state than its call was made for is made again
const int kCaptureAttempts = 8;

// Armed at a stop by a CC_EXEC call, which arms the capture and the resume behind it; while running by a
// CC_TABLE call, and nothing stops. The response is deferred: the capture's notice answers it (the core's
// deferred response, above)
void MethodRecordSgcFrames(const Request& req, Outcome& out)
{
	u64 frames = kDefaultSgcFrames;
	bool present;

	if (!ParamUnsigned(req, out, "frames", 1, kMaxSgcFrames, &frames, &present))
		return;

	// the capture before is not answered yet, or its notice has not come
	if (notices_owed.load() != 0)
		return Fail(out, kErrNotPaused);

	for (int attempt = 0; attempt < kCaptureAttempts; attempt++)
	{
		// by the published lifetime, without a call
		const Lifetime lifetime = emu::GetRunState().lifetime;

		if (lifetime == Lifetime::NoMachine)
			return Fail(out, kErrNoGame);

		if (lifetime == Lifetime::Stopped)
			return Fail(out, kErrHostStopped);

		auto job = std::make_shared<CaptureJob>();
		int rv;

		job->frames = (u32)frames;

		if (lifetime == Lifetime::Parked)
		{
			rv = RunCall(emu::CC_EXEC, [job] {
				// Nothing leaves a call (dbg_emu.h, Call()): an allocation of the capture's buffer that fails
				// throws in sgc::Arm, which has armed nothing then, and the resume is not armed either. -32603,
				// with no notice owed
				try
				{
					if (auto hook = before_arm.load())
						hook();

					// the capture first: a refused arm leaves no resume armed
					job->armed = sgc::Arm(job->frames, true);

					if (job->armed != 0)
						return;

					if (emu::Resume() != 0)
					{
						// the notice answers -32003
						sgc::Cancel(kErrNotPaused);
						job->refused = true;
					}
				}
				catch (...)
				{
					job->armed = kErrInternal;
				}
			});

			// the thread runs: made again as a table call
			if (rv == kErrNotPaused)
				continue;
		}
		else
		{
			if (auto hook = before_table_call.load())
				hook();

			rv = RunCall(emu::CC_TABLE, [job] {
				// as the exec call: what sgc::Arm throws is -32603, with nothing armed. On the emulator thread this
				// closure is in a service excursion or in the drain of the thread's exit
				try
				{
					// run on its caller under L: no emulator thread exists, and nothing is armed
					if (!emu::OnEmuThread())
					{
						job->noThread = true;
						return;
					}

					// a stop is in force (the park loop runs this call): the exec call arms the capture and the
					// resume
					if (emu::GetRunState().lifetime == Lifetime::Parked)
					{
						job->parked = true;
						return;
					}

					job->armed = sgc::Arm(job->frames, false);
				}
				catch (...)
				{
					job->armed = kErrInternal;
				}
			});

			if (rv == 0 && job->parked)
				continue;

			if (rv == 0 && job->noThread)
			{
				// answered by the lifetime; a thread that started in between takes the request again
				const Lifetime now = emu::GetRunState().lifetime;

				if (now == Lifetime::NoMachine)
					return Fail(out, kErrNoGame);

				if (now == Lifetime::Stopped)
					return Fail(out, kErrHostStopped);

				continue;
			}
		}

		if (rv != 0)
			return Fail(out, rv);

		if (job->armed == kErrNotSupported)
			return Unsupported(out, "state.recordSgcFrames: this build has no SGC capture");

		// a capture records already (-32003): it goes on; or sgc::Arm threw (-32603): nothing is armed
		if (job->armed != 0)
			return Fail(out, job->armed);

		// Armed, also where the resume behind it was refused: the notice of the capture answers, and none may be
		// armed until it has come
		notices_owed++;

		capture.pending = true;
		capture.client = req.client;
		capture.id = req.id;
		capture.deadline = Clock::now() + std::chrono::milliseconds(capture_timeout_ms.load());
		capture.deadlineArmed = true;
		pending_seen.store(true);

		out.deferred = true;
		return;
	}

	// the state changed under every call that was made
	Fail(out, kErrBusy);
}

/*
	The tables: breakpoints, watches, categories. Each request is one table call; the tick that shows the
	tables as they are now is built and broadcast inside it.
*/

// Runs a closure that returns 0 or an error code with its text in *message, as one call of the class, and
// answers. The lifetime answers are the call's; the method's own errors are the closure's
template <typename Fn>
void TickCall(Outcome& out, emu::CallClass cls, Fn fn)
{
	auto job = std::make_shared<TextJob>();

	int rv = RunCall(cls, [job, fn]() mutable {
		try
		{
			job->code = fn(&job->message);

			// after every request that succeeded, whether or not it changed anything
			if (job->code == 0)
				BroadcastTick(emu::GetRunState());
		}
		catch (...)
		{
			job->code = kErrInternal;
			job->message.clear();
		}
	});

	if (rv != 0)
		return Fail(out, rv);

	if (job->code != 0)
		return Fail(out, job->code, job->message);
}

// A request that changes the tables: served in every lifetime state
template <typename Fn>
void TableCall(Outcome& out, Fn fn)
{
	TickCall(out, emu::CC_TABLE, fn);
}

// A request that writes guest state: served while the thread is parked, and in a service excursion while it
// runs; -32002 without a machine and -32001 while the host has it stopped
template <typename Fn>
void WriteCall(Outcome& out, Fn fn)
{
	TickCall(out, emu::CC_WRITE, fn);
}

void MethodBreakpointsAdd(const Request& req, Outcome& out)
{
	std::string event, kind;
	u32 address = 0;
	bool has_address, has_kind, has_enabled, enabled = true;

	// what the request alone shows
	if (!ParamString(req, out, "event", &event) || !ParamAddress(req, out, &address, &has_address) || !ParamString(req, out, "kind", &kind, &has_kind)
		|| !ParamBool(req, out, "enabled", &enabled, &has_enabled))
		return;

	if (has_kind && kind != "code" && kind != "event")
		return BadParam(out, "kind is not one of code, event");

	// No name of an event or of a target has a NUL in it, and the look-up of an event compares C strings, which
	// end at the first one: a name with one is none of them, whatever stands in front of the NUL
	if (event.find('\0') != std::string::npos)
		return BadParam(out, "event " + PrintableAscii(event.data(), std::min<size_t>(event.size(), 96)) + " has a NUL in it: no event or target is named so");

	if (!has_enabled)
		enabled = true;

	Breakpoint bp = { };

	bp.enabled = enabled;
	bp.code = has_kind ? kind == "code" : has_address;

	if (!bp.code)
	{
		// an event the device tree lists. The names of the code breakpoints are no events, and neither is a
		// row of the event table that no node lists. An address is not looked at and not kept
		bp.event = FindEvent(event.c_str());

		if (!EventListed(bp.event))
			return Unsupported(out, "event " + PrintableAscii(event.data(), std::min<size_t>(event.size(), 96)));
	}
	else
	{
		// the event of the code breakpoints of a listed target, which steps
		const TargetInfo* target = nullptr;

		for (const TargetInfo& t : kTargets)
		{
			if (event == t.breakpointEvent)
				target = &t;
		}

		if (target == nullptr)
			return Unsupported(out, "code breakpoints on " + PrintableAscii(event.data(), std::min<size_t>(event.size(), 96)));

		if (!emu::TargetSteps(target->target))
			return Unsupported(out, std::string("code breakpoints of target ") + target->id);

		if (!has_address)
			return BadParam(out, "address is missing: a code breakpoint has one");

		if (address % target->instructionSize != 0)
			return BadParam(out, std::string("address is not a multiple of ") + std::to_string(target->instructionSize));

		// no instruction of the target is above its addressMax
		if (address > target->addressMax)
			return BadParam(out, std::string("address is above the highest address of target ") + target->id);

		bp.target = target->target;
		bp.address = address;
	}

	TableCall(out, [bp](std::string* message) mutable {
		if (breakpoints.size() >= kMaxBreakpoints)
		{
			*message = "invalid params: the table has " + std::to_string(kMaxBreakpoints) + " breakpoints";
			return (int)kErrInvalidParams;
		}

		// every add makes a breakpoint: no two are one
		bp.id = next_breakpoint_id++;
		breakpoints[bp.id] = bp;

		PushBreakpoints();
		return 0;
	});
}

void MethodBreakpointsRemove(const Request& req, Outcome& out)
{
	u64 id;

	if (!ParamUnsigned(req, out, "id", 0, ~(u64)0, &id))
		return;

	TableCall(out, [id](std::string* message) {
		auto it = id <= 0xFFFFFFFFu ? breakpoints.find((u32)id) : breakpoints.end();

		if (it == breakpoints.end())
		{
			*message = "invalid params: no breakpoint has id " + std::to_string(id);
			return (int)kErrInvalidParams;
		}

		breakpoints.erase(it);

		PushBreakpoints();
		return 0;
	});
}

void MethodBreakpointsToggle(const Request& req, Outcome& out)
{
	u64 id;
	bool enabled;

	if (!ParamUnsigned(req, out, "id", 0, ~(u64)0, &id) || !ParamBool(req, out, "enabled", &enabled))
		return;

	TableCall(out, [id, enabled](std::string* message) {
		auto it = id <= 0xFFFFFFFFu ? breakpoints.find((u32)id) : breakpoints.end();

		if (it == breakpoints.end())
		{
			*message = "invalid params: no breakpoint has id " + std::to_string(id);
			return (int)kErrInvalidParams;
		}

		if (it->second.enabled != enabled)
		{
			it->second.enabled = enabled;
			PushBreakpoints();
		}

		return 0;
	});
}

void MethodSetCategoryStates(const Request& req, Outcome& out)
{
	const json* given = Param(req, "categories");

	if (given == nullptr)
		return BadParam(out, "categories is missing");

	if (!given->is_object())
		return BadParam(out, "categories is not an object");

	// keys that are no category are not looked at; a category that is absent keeps its state
	struct Change {
		bool present;
		CategoryState state;
	};

	Change changes[kCategoryCount] = { };

	for (u32 i = 0; i < kCategoryCount; i++)
	{
		const char* name = CategoryName((Category)i);
		auto it = given->find(name);

		if (it == given->end())
			continue;

		if (!it->is_object())
			return BadParam(out, std::string("categories.") + name + " is not an object");

		auto muted = it->find("muted");
		auto soloed = it->find("soloed");

		if (muted == it->end() || !muted->is_boolean() || soloed == it->end() || !soloed->is_boolean())
			return BadParam(out, std::string("categories.") + name + " has no boolean muted and soloed");

		changes[i].present = true;
		changes[i].state.muted = muted->get<bool>();
		changes[i].state.soloed = soloed->get<bool>();
	}

	std::vector<Change> list(changes, changes + kCategoryCount);

	TableCall(out, [list](std::string*) {
		for (u32 i = 0; i < kCategoryCount; i++)
		{
			if (list[i].present)
				categories[i] = list[i].state;
		}

		// read at every arrival: never a recompile
		PushPresence();
		return 0;
	});
}

// "<node path>.<register name>": lower-cased as a whole, then split at its last dot
bool ResolveWatch(const std::string& expression, Watch* watch)
{
	// No name of the tree has a NUL in it, and the look-ups below compare C strings, which end at the first one:
	// a text with one is no register of the tree, whatever stands in front of the NUL
	if (expression.find('\0') != std::string::npos)
		return false;

	std::string lower = expression;

	for (char& c : lower)
	{
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	}

	const size_t dot = lower.rfind('.');

	if (dot == std::string::npos || dot == 0 || dot + 1 >= lower.size())
		return false;

	const regs::Node* node = regs::Find(lower.substr(0, dot).c_str());

	if (node == nullptr)
		return false;

	const int index = regs::FindRegister(*node, lower.c_str() + dot + 1);

	if (index < 0)
		return false;

	watch->id = 0;
	watch->expression = lower;
	watch->node = node;
	watch->index = (u32)index;
	return true;
}

std::string Unresolvable(const std::string& expression)
{
	return "invalid params: \"" + PrintableAscii(expression.data(), std::min<size_t>(expression.size(), 96)) + "\" is not a register of the device tree";
}

void MethodWatch(const Request& req, Outcome& out)
{
	const json* given = Param(req, "expressions");

	if (given == nullptr)
		return BadParam(out, "expressions is missing");

	if (!given->is_array())
		return BadParam(out, "expressions is not an array");

	if (given->size() > kMaxWatchExpressionsPerCall)
		return BadParam(out, "more than " + std::to_string(kMaxWatchExpressionsPerCall) + " expressions");

	std::vector<std::string> expressions;

	for (const json& item : *given)
	{
		if (!item.is_string())
			return BadParam(out, "an expression is not a string");

		const std::string& text = item.get_ref<const std::string&>();

		if (text.size() > kMaxWatchExpressionChars)
			return BadParam(out, "an expression has more than " + std::to_string(kMaxWatchExpressionChars) + " characters");

		expressions.push_back(text);
	}

	TableCall(out, [expressions](std::string* message) {
		// all or nothing: every expression is resolved before the first one is stored
		std::vector<Watch> added;

		for (const std::string& expression : expressions)
		{
			Watch watch;

			if (!ResolveWatch(expression, &watch))
			{
				*message = Unresolvable(expression);
				return (int)kErrInvalidParams;
			}

			// one that is stored already, or that this call has already, is not added again
			bool known = false;

			for (const Watch& w : watches)
				known = known || w.expression == watch.expression;
			for (const Watch& w : added)
				known = known || w.expression == watch.expression;

			if (!known)
				added.push_back(watch);
		}

		// the bound counts what would be added
		if (watches.size() + added.size() > kMaxWatches)
		{
			*message = "invalid params: more than " + std::to_string(kMaxWatches) + " watches";
			return (int)kErrInvalidParams;
		}

		for (Watch& watch : added)
		{
			watch.id = next_watch_id++;
			watches.push_back(watch);
		}

		return 0;
	});
}

void MethodUnwatch(const Request& req, Outcome& out)
{
	const json* given = Param(req, "watchIds");

	if (given == nullptr)
		return BadParam(out, "watchIds is missing");

	if (!given->is_array())
		return BadParam(out, "watchIds is not an array");

	std::vector<u64> ids;

	for (const json& item : *given)
	{
		u64 id;

		if (!ToUnsigned(item, &id))
			return BadParam(out, "a watch id is not an unsigned integer");

		ids.push_back(id);
	}

	TableCall(out, [ids](std::string*) {
		// an id that no watch has is not an error
		std::set<u64> gone(ids.begin(), ids.end());
		std::vector<Watch> kept;

		for (const Watch& w : watches)
		{
			if (gone.count(w.id) == 0)
				kept.push_back(w);
		}

		watches.swap(kept);
		return 0;
	});
}

void MethodModifyWatchExpression(const Request& req, Outcome& out)
{
	u64 id;
	std::string expression;

	if (!ParamUnsigned(req, out, "watchId", 0, ~(u64)0, &id) || !ParamString(req, out, "newExpression", &expression))
		return;

	if (expression.size() > kMaxWatchExpressionChars)
		return BadParam(out, "newExpression has more than " + std::to_string(kMaxWatchExpressionChars) + " characters");

	TableCall(out, [id, expression](std::string* message) {
		Watch* found = nullptr;

		for (Watch& w : watches)
		{
			if (w.id == id)
				found = &w;
		}

		if (found == nullptr)
		{
			*message = "invalid params: no watch has id " + std::to_string(id);
			return (int)kErrInvalidParams;
		}

		Watch watch;

		if (!ResolveWatch(expression, &watch))
		{
			*message = Unresolvable(expression);
			return (int)kErrInvalidParams;
		}

		// the id is kept; another watch may have the expression already
		found->expression = watch.expression;
		found->node = watch.node;
		found->index = watch.index;
		return 0;
	});
}

struct ExecJob {
	int rv = 0;
	const char* why = nullptr;		// a constant string
};

/*
	Writes (milestone M2). Each request is one write call: what the request alone shows is checked in front of
	it (-32602), the lifetime answers are the call's, and the errors of the method itself are decided inside the
	closure, in the order the method states. The tick that shows what was written is built and broadcast inside
	the call; a write is no stop, so it has the stop of the moment.
*/
void MethodWriteMemory(const Request& req, Outcome& out)
{
	const TargetInfo* target;
	u32 address;

	// the target is required: a read has a default, a write has none
	if (!ParamTarget(req, out, nullptr, &target) || !ParamAddress(req, out, &address))
		return;

	const json* given = Param(req, "data");

	if (given == nullptr)
		return BadParam(out, "data is missing");

	if (!given->is_array())
		return BadParam(out, "data is not an array");

	if (given->empty() || given->size() > kMaxWriteMemoryBytes)
		return BadParam(out, "data has " + std::to_string(given->size()) + " entries, outside 1.." + std::to_string(kMaxWriteMemoryBytes));

	auto bytes = std::make_shared<std::vector<u8>>();

	bytes->reserve(given->size());

	for (const json& item : *given)
	{
		u64 byte;

		if (!ToUnsigned(item, &byte) || byte > 255)
			return BadParam(out, "an entry of data is not an integer 0..255");

		bytes->push_back((u8)byte);
	}

	WriteCall(out, [target, address, bytes](std::string* message) {
		// one of the method's own errors, as for a read of that target
		if (!target->memory)
		{
			*message = std::string("not supported: target ") + target->id + " has no memory";
			return (int)kErrNotSupported;
		}

		// all of the range or nothing of it; a range does not wrap
		const int rv = mem::Write(target->target, address, (u32)bytes->size(), bytes->data());

		if (rv == kErrNotWritable)
		{
			char where[96];

			snprintf(where, sizeof(where), "%zu byte%s at 0x%08X of %s", bytes->size(), bytes->size() == 1 ? "" : "s", address, target->id);
			*message = std::string("not writable: ") + where;
		}

		return rv;
	});
}

// The value of state.setRegister and of state.editWatch: ASCII blanks around it are dropped; then 0x or 0X and
// one or more hex digits of either case, or one or more decimal digits, and nothing else: no sign, no empty
// text. Of a text of any length the low 64 bits are kept
bool ParseValue(const std::string& text, u64* value)
{
	size_t first = 0, last = text.size();

	while (first < last && (text[first] == ' ' || text[first] == '\t'))
		first++;
	while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t'))
		last--;

	u64 v = 0;

	if (last - first >= 2 && text[first] == '0' && (text[first + 1] == 'x' || text[first + 1] == 'X'))
	{
		first += 2;

		if (first == last)
			return false;

		for (size_t i = first; i < last; i++)
		{
			const char c = text[i];
			u32 digit;

			if (c >= '0' && c <= '9')
				digit = (u32)(c - '0');
			else if (c >= 'a' && c <= 'f')
				digit = (u32)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				digit = (u32)(c - 'A' + 10);
			else
				return false;

			v = (v << 4) | digit;
		}
	}
	else
	{
		if (first == last)
			return false;

		for (size_t i = first; i < last; i++)
		{
			const char c = text[i];

			if (c < '0' || c > '9')
				return false;

			// modulo 2^64
			v = v * 10 + (u32)(c - '0');
		}
	}

	*value = v;
	return true;
}

// Inside a write call: `text` into a register of the tree. The answers behind "the register exists", in their
// order: a register of width 0 or without a writer, a text that is no number, a value the device refuses
int WriteRegisterText(const regs::Node& node, u32 index, const std::string& text, std::string* message)
{
	const regs::Register& reg = node.registers[index];
	const std::string name = std::string(node.path) + "." + reg.name;

	if (reg.width == 0 || !(reg.flags & regs::kRegWritable))
	{
		*message = "not writable: " + name + (reg.width == 0 ? " is text" : " has no writer");
		return (int)kErrNotWritable;
	}

	u64 value;

	if (!ParseValue(text, &value))
	{
		*message = "invalid params: value is neither 0x and hex digits nor decimal digits";
		return (int)kErrInvalidParams;
	}

	const int rv = regs::Write(node, index, value);

	if (rv == kErrNotWritable)
	{
		char given[24];

		regs::FormatValue(reg.width, value, given);
		*message = "not writable: " + name + " does not take the value " + given;
	}

	return rv;
}

void MethodSetRegister(const Request& req, Outcome& out)
{
	std::string path, name, value;

	if (!ParamString(req, out, "path", &path) || !ParamString(req, out, "name", &name) || !ParamString(req, out, "value", &value))
		return;

	WriteCall(out, [path, name, value](std::string* message) {
		// the path without regard to case, as the path of a watch expression; FindRegister ignores case by itself
		std::string lower = path;

		for (char& c : lower)
		{
			if (c >= 'A' && c <= 'Z')
				c = (char)(c - 'A' + 'a');
		}

		// No name of the tree has a NUL in it, and the two look-ups compare C strings, which end at the first one:
		// a path or a name with one is not found, whatever stands in front of the NUL
		const bool whole = path.find('\0') == std::string::npos && name.find('\0') == std::string::npos;
		const regs::Node* node = whole ? regs::Find(lower.c_str()) : nullptr;
		const int index = node != nullptr ? regs::FindRegister(*node, name.c_str()) : -1;

		if (index < 0)
		{
			*message = "invalid params: " + PrintableAscii(path.data(), std::min<size_t>(path.size(), 96)) + " has no register "
				+ PrintableAscii(name.data(), std::min<size_t>(name.size(), 96));
			return (int)kErrInvalidParams;
		}

		return WriteRegisterText(*node, (u32)index, value, message);
	});
}

// state.setRegister on the register of a watch
void MethodEditWatch(const Request& req, Outcome& out)
{
	u64 id;
	std::string value;

	if (!ParamUnsigned(req, out, "watchId", 0, ~(u64)0, &id) || !ParamString(req, out, "value", &value))
		return;

	WriteCall(out, [id, value](std::string* message) {
		const Watch* found = nullptr;

		for (const Watch& w : watches)
		{
			if (w.id == id)
				found = &w;
		}

		if (found == nullptr)
		{
			*message = "invalid params: no watch has id " + std::to_string(id);
			return (int)kErrInvalidParams;
		}

		return WriteRegisterText(*found->node, found->index, value, message);
	});
}

// No tick of its own: while a stop is in force the stop of the reset is published when the call has returned,
// with its tick; a machine that runs resets and runs on, and nothing is published
void MethodReset(const Request&, Outcome& out)
{
	auto job = std::make_shared<ExecJob>();

	int rv = RunCall(emu::CC_WRITE, [job] { job->rv = emu::Reset(); });

	if (rv != 0)
		return Fail(out, rv);

	if (job->rv != 0)
		return Fail(out, job->rv);
}

/*
	Run control (design 2.3.2). A request is answered when it is validated and armed; a tick says what
	became of it.
*/
void MethodPause(const Request& req, Outcome& out)
{
	// there is one run state: the target is not looked at, when it is one
	if (const json* target = Param(req, "target"))
	{
		if (!target->is_string() || FindTarget(target->get_ref<const std::string&>()) == nullptr)
			return BadParam(out, "target is not one of sh4, arm7, dsp");
	}

	switch (emu::GetRunState().lifetime)
	{
	case Lifetime::NoMachine:
		return Fail(out, kErrNoGame);

	case Lifetime::Stopped:
		return Fail(out, kErrHostStopped);

	case Lifetime::Running:
		emu::RequestPause();
		return;

	case Lifetime::Parked:
		// nothing changes
		return;
	}
}

void MethodRunUntil(const Request&, Outcome& out)
{
	auto job = std::make_shared<ExecJob>();

	int rv = RunCall(emu::CC_EXEC, [job] { job->rv = emu::Resume(); });

	// the thread runs already: nothing changes
	if (rv == kErrNotPaused)
		return;

	if (rv != 0)
		return Fail(out, rv);

	// an action is armed for this stop already
	if (job->rv != 0)
		return Fail(out, job->rv);
}

void Step(const Request& req, Outcome& out, StepKind kind)
{
	const TargetInfo* target;

	if (!ParamTarget(req, out, nullptr, &target))
		return;

	if (!emu::TargetSteps(target->target))
	{
		// the lifetime first, then a step while running, then what the target cannot do
		switch (emu::GetRunState().lifetime)
		{
		case Lifetime::NoMachine: return Fail(out, kErrNoGame);
		case Lifetime::Stopped:   return Fail(out, kErrHostStopped);
		case Lifetime::Running:   return Fail(out, kErrNotPaused);
		case Lifetime::Parked:    break;
		}

		return Unsupported(out, std::string("stepping target ") + target->id);
	}

	auto job = std::make_shared<ExecJob>();
	const Target t = target->target;

	int rv = RunCall(emu::CC_EXEC, [job, t, kind] { job->rv = emu::Step(t, kind, &job->why); });

	if (rv != 0)
		return Fail(out, rv);

	switch (job->rv)
	{
	case 0:
		return;

	case kErrCannotStep:
		return Fail(out, kErrCannotStep, std::string("cannot step: ") + (job->why != nullptr ? job->why : "a precondition failed"));

	case kErrNotSupported:
		// the DSP steps into only, on every backend (M3-ROUND, M3-D30)
		if (t == Target::Dsp && kind != StepKind::Into)
			return Unsupported(out, std::string("step over and step out of target ") + target->id);

		return Unsupported(out, std::string("stepping target ") + target->id + " on this backend");

	default:
		return Fail(out, job->rv);
	}
}

void MethodStep(const Request& req, Outcome& out)     { Step(req, out, StepKind::Into); }
void MethodStepOver(const Request& req, Outcome& out) { Step(req, out, StepKind::Over); }
void MethodStepOut(const Request& req, Outcome& out)  { Step(req, out, StepKind::Out); }

/*
	The methods of section 3.3. One without a function belongs to a later milestone: it is answered -32004
	on its channel, before its parameters and the lifetime are looked at.
*/
typedef void (*MethodFn)(const Request& req, Outcome& out);

struct Method {
	const char* name;
	bool inBand;		// its result type is RpcError: every failure of a well-formed request is in-band
	MethodFn run;
};

const Method kMethods[] = {
	{ "debugger.describe",             false, MethodDescribe },
	{ "state.getMemorySlice",          false, MethodGetMemorySlice },
	{ "state.getDisassembly",          false, MethodGetDisassembly },
	{ "state.getCallstack",            false, MethodGetCallstack },
	{ "state.watch",                   true,  MethodWatch },
	{ "state.unwatch",                 true,  MethodUnwatch },
	{ "state.modifyWatchExpression",   true,  MethodModifyWatchExpression },
	{ "control.pause",                 true,  MethodPause },
	{ "control.runUntil",              true,  MethodRunUntil },
	{ "control.step",                  true,  MethodStep },
	{ "control.stepOver",              true,  MethodStepOver },
	{ "control.stepOut",               true,  MethodStepOut },
	{ "breakpoints.add",               true,  MethodBreakpointsAdd },
	{ "breakpoints.remove",            true,  MethodBreakpointsRemove },
	{ "breakpoints.toggle",            true,  MethodBreakpointsToggle },
	{ "breakpoints.setCategoryStates", true,  MethodSetCategoryStates },
	// milestone M2
	{ "state.editWatch",               true,  MethodEditWatch },
	{ "state.writeMemory",             true,  MethodWriteMemory },
	{ "state.setRegister",             true,  MethodSetRegister },
	{ "control.reset",                 true,  MethodReset },
	// milestone M3
	{ "state.recordSgcFrames",         true,  MethodRecordSgcFrames },
	{ "state.getSgcFrameData",         false, MethodGetSgcFrameData },
	{ "state.getMemoryBinary",         false, MethodGetMemoryBinary },
	// milestone M4
	{ "state.getTable",                false, nullptr },
	{ "state.captureHolly",            false, nullptr },
};

struct Reply {
	std::string text;		// empty: no answer (a notification, or a response the method defers)
	bool sent = false;		// the method handed it to the server itself
	std::string binary;		// a binary frame that goes directly behind the response
};

// One message of a client. Runs on (C); in a selftest on the thread that plays it, or in place on (E)
Reply Dispatch(u32 client, const std::string& text)
{
	Reply reply;
	json message;

	try
	{
		message = json::parse(text);
	}
	catch (...)
	{
		reply.text = ErrorResponse("null", kErrParse, "");
		return reply;
	}

	// The envelope: an object with "jsonrpc": "2.0", a method string, params that are an object where they
	// are there, and an id that is a number where it is there. Members this does not name are not looked at
	std::string id = "null";
	bool has_id = false, valid = message.is_object();

	if (valid)
	{
		auto it = message.find("id");

		if (it != message.end())
		{
			has_id = true;

			if (it->is_number())
				id = it->dump();
			else
				valid = false;
		}

		auto version = message.find("jsonrpc");
		auto method = message.find("method");
		auto params = message.find("params");

		if (version == message.end() || !version->is_string() || version->get_ref<const std::string&>() != "2.0")
			valid = false;

		if (method == message.end() || !method->is_string())
			valid = false;

		if (params != message.end() && !params->is_object())
			valid = false;
	}

	if (!valid)
	{
		reply.text = ErrorResponse(id, kErrInvalidRequest, "");
		return reply;
	}

	// a notification of the client: the server has none to take, and none is answered
	if (!has_id)
		return reply;

	const std::string& name = message.find("method")->get_ref<const std::string&>();
	const Method* method = nullptr;

	for (const Method& m : kMethods)
	{
		if (name == m.name)
			method = &m;
	}

	if (method == nullptr)
	{
		reply.text = ErrorResponse(id, kErrMethodNotFound, "method not found: " + PrintableAscii(name.data(), std::min<size_t>(name.size(), 96)));
		return reply;
	}

	static const json no_params = json::object();
	auto params = message.find("params");

	Request req;

	req.client = client;
	req.id = id;
	req.params = params != message.end() ? &*params : &no_params;

	Outcome out;

	if (method->run == nullptr)
	{
		Unsupported(out, method->name);
	}
	else
	{
		try
		{
			method->run(req, out);
		}
		catch (...)
		{
			out = Outcome();
			Fail(out, kErrInternal);
		}
	}

	if (out.sent)
	{
		reply.text = std::move(out.response);
		reply.sent = true;
	}
	else if (out.deferred)
	{
		// answered when the capture's notice comes
	}
	else if (out.code == 0)
	{
		reply.text = ResultResponse(id, out.result);

		// the frame of a result, and of nothing else
		reply.binary = std::move(out.binary);
	}
	else if (method->inBand)
	{
		reply.text = ResultResponse(id, InBandError(out.code, out.message));
	}
	else
	{
		reply.text = ErrorResponse(id, out.code, out.message);
	}

	return reply;
}

/*
	The core thread
*/
struct Message {
	enum Kind { Open, Close, Text, Notice, Expire } kind;
	u32 client;
	std::string text;
	int answer = 0;			// Notice: what the capture's notice hands on (dbg_sgc.h)
};

struct Queue {
	std::mutex lock;
	std::condition_variable wake;
	std::deque<Message> messages;
	bool accepting = false;		// between Start() and Stop()
	bool quit = false;
};

Queue& queue = *new Queue();

std::thread* thread;					// (C), between Start() and Stop()
std::atomic<bool> started { false };

// (C)'s own
struct Own {
	std::set<u32> named;				// the clients whose "client" text was printed
	u32 periodic = 0;					// periodic ticks so far
	Clock::time_point nextTick;

	bool graceArmed = false;			// the last client left, and nothing was done about it yet
	Clock::time_point graceAt;

	bool syncOwed = false;				// the emulator thread did not take the table call of a connect or
	Clock::time_point syncAt;			// disconnect: it is made again

	bool releaseOwed = false;			// ... or the release of the stop when the grace period expired
	Clock::time_point releaseAt;
};

Own& c = *new Own();

void Enqueue(Message&& message)
{
	{
		std::lock_guard<std::mutex> lock(queue.lock);

		if (!queue.accepting)
			return;

		queue.messages.push_back(std::move(message));
	}

	queue.wake.notify_one();
}

// The notice of dbg::sgc (dbg_sgc.h, SetCompletionNotice): on (E), or under L while no emulator thread exists. It
// only queues, for (C), which pairs it with its request (the capture's deferred response)
void CaptureNotice(int answer)
{
	Enqueue({ Message::Notice, 0, std::string(), answer });
}

// The handler of the transport: on the network thread, it only queues
struct CoreHandler : net::Handler {
	void OnOpen(uint32_t client) override { Enqueue({ Message::Open, client, std::string() }); }
	void OnClose(uint32_t client) override { Enqueue({ Message::Close, client, std::string() }); }
	void OnText(uint32_t client, const std::string& text) override { Enqueue({ Message::Text, client, text }); }
};

// The gates and the active mask for the count of clients as it is now
void SyncPresence()
{
	const int rv = RunCall(emu::CC_TABLE, [] {
		try
		{
			PushPresence();
		}
		catch (...)
		{
		}
	});

	c.syncOwed = rv != 0;

	if (c.syncOwed)
		c.syncAt = Clock::now() + std::chrono::milliseconds(kRetryMs);
}

void ClientOpened(u32 client)
{
	{
		std::lock_guard<std::mutex> lock(clients_lock);

		clients.push_back({ client, false });
		client_count.store((u32)clients.size());
	}

	// breakpoints are active again, and nothing is released behind a client's back
	c.graceArmed = false;
	c.releaseOwed = false;
	grace_expired.store(false);

	SyncPresence();
}

void ClientClosed(u32 client)
{
	bool none;

	{
		std::lock_guard<std::mutex> lock(clients_lock);

		for (size_t i = 0; i < clients.size(); i++)
		{
			if (clients[i].id == client)
			{
				clients.erase(clients.begin() + (std::ptrdiff_t)i);
				break;
			}
		}

		client_count.store((u32)clients.size());
		none = clients.empty();
	}

	c.named.erase(client);

	// the grace period starts at a disconnect only: a start park waits for its first client without limit
	if (none)
	{
		c.graceArmed = true;
		c.graceAt = Clock::now() + std::chrono::milliseconds(kClientGraceMs);
	}

	SyncPresence();
}

// The last client has been gone for kClientGraceMs: no breakpoint stops any more, and a stop is released
void GraceExpired()
{
	c.graceArmed = false;

	if (client_count.load() != 0)
		return;

	grace_expired.store(true);

	// Breakpoints stay listed and armed; the categories of the tick are not changed. With no client connected
	// every gate is closed: the note is appended to the log itself, where the scheduler's time can be read,
	// and the next client finds it in its first tick
	const int table = RunCall(emu::CC_TABLE, [] {
		try
		{
			PushPresence();

			events::Append(EV_DEBUGGER_NOTE, "no client for 10 s: breakpoints are inactive until a client connects, and a stop that is in force is released");
		}
		catch (...)
		{
		}
	});

	// the emulator thread did not take it within its time: once more, a little later
	if (table != 0)
	{
		c.graceArmed = true;
		c.graceAt = Clock::now() + std::chrono::milliseconds(1000);
		return;
	}

	// a parked thread goes on; one that runs, and a machine without a thread, has nothing to release
	auto job = std::make_shared<ExecJob>();

	if (RunCall(emu::CC_EXEC, [job] { job->rv = emu::Resume(); }) == kErrBusy)
	{
		c.releaseOwed = true;
		c.releaseAt = Clock::now() + std::chrono::milliseconds(1000);
	}
}

// The release of a stop that the emulator thread did not take in time, for as long as no client is connected
void ReleaseAgain()
{
	c.releaseOwed = false;

	if (client_count.load() != 0 || !grace_expired.load())
		return;

	auto job = std::make_shared<ExecJob>();

	if (RunCall(emu::CC_EXEC, [job] { job->rv = emu::Resume(); }) == kErrBusy)
	{
		c.releaseOwed = true;
		c.releaseAt = Clock::now() + std::chrono::milliseconds(1000);
	}
}

// Every Debugger:TickMs while the machine runs and a client is connected. A read: (E) builds it in the slice
// hook, and when (E) does not take it in time it is skipped
void PeriodicTick()
{
	if (client_count.load() == 0 || emu::GetRunState().lifetime != Lifetime::Running)
		return;

	const bool nth = ++c.periodic % full_tick_every == 0;

	RunCall(emu::CC_READ, [nth] {
		try
		{
			const emu::RunState state = emu::GetRunState();
			net::Server* s = server.load();

			// no periodic tick while paused
			if (s == nullptr || state.lifetime != Lifetime::Running)
				return;

			std::vector<u32> recipients;
			bool full = nth;

			{
				std::lock_guard<std::mutex> lock(clients_lock);

				for (ClientEntry& client : clients)
				{
					// not behind a megabyte that a slow client has not read; it gets a full tick next
					if (s->QueuedBytes(client.id) > kSlowClientBytes)
					{
						client.needsFull = true;
						continue;
					}

					full = full || client.needsFull;
					client.needsFull = false;
					recipients.push_back(client.id);
				}
			}

			if (recipients.empty())
				return;

			const std::string tick = BuildTick(state, full);

			for (u32 id : recipients)
				s->SendText(id, tick);
		}
		catch (...)
		{
		}
	});
}

// What a request is answered with, to the client that made it: the response, then the binary frame behind it
void Deliver(u32 client, Reply& reply)
{
	if (!reply.text.empty() && !reply.sent)
		SendTo(client, reply.text);

	if (!reply.binary.empty())
		SendBinaryTo(client, std::move(reply.binary));
}

void HandleText(u32 client, const std::string& text)
{
	Reply reply = Dispatch(client, text);

	Deliver(client, reply);
}

// The "client" text of a describe, once per connection: one line on stdout
void NoteClientText(u32 client, const std::string& text)
{
	if (c.named.count(client) != 0)
		return;

	// only for a request that will be answered as a describe: cheap to find out, the text is parsed again later
	json message = json::parse(text, nullptr, false);

	if (!message.is_object())
		return;

	auto version = message.find("jsonrpc");
	auto id = message.find("id");
	auto method = message.find("method");
	auto params = message.find("params");

	if (version == message.end() || !version->is_string() || version->get_ref<const std::string&>() != "2.0" || id == message.end() || !id->is_number())
		return;

	if (method == message.end() || !method->is_string() || method->get_ref<const std::string&>() != "debugger.describe")
		return;

	if (params == message.end() || !params->is_object())
		return;

	auto name = params->find("client");

	if (name == params->end() || !name->is_string())
		return;

	const std::string& given = name->get_ref<const std::string&>();

	c.named.insert(client);

	printf("Debugger: client %u: %s\n", client, PrintableAscii(given.data(), std::min<size_t>(given.size(), kClientTextMax)).c_str());
	fflush(stdout);
}

void ThreadMain()
{
	c.nextTick = Clock::now() + std::chrono::milliseconds(tick_ms);

	for (;;)
	{
		Message message;
		bool have = false;

		{
			std::unique_lock<std::mutex> lock(queue.lock);

			Clock::time_point until = c.nextTick;

			if (c.graceArmed && c.graceAt < until)
				until = c.graceAt;
			if (c.syncOwed && c.syncAt < until)
				until = c.syncAt;
			if (c.releaseOwed && c.releaseAt < until)
				until = c.releaseAt;
			if (capture.deadlineArmed && capture.deadline < until)
				until = capture.deadline;
			if (capture.cancelOwed && capture.cancelAt < until)
				until = capture.cancelAt;

			queue.wake.wait_until(lock, until, [] { return queue.quit || !queue.messages.empty(); });

			if (queue.quit)
				return;

			if (!queue.messages.empty())
			{
				message = std::move(queue.messages.front());
				queue.messages.pop_front();
				have = true;
			}
		}

		if (have)
		{
			switch (message.kind)
			{
			case Message::Open:
				ClientOpened(message.client);
				break;

			case Message::Close:
				ClientClosed(message.client);
				break;

			case Message::Text:
				try
				{
					// cheap for anything but a describe that names its client
					if (message.text.find("\"client\"") != std::string::npos)
						NoteClientText(message.client, message.text);
				}
				catch (...)
				{
				}

				HandleText(message.client, message.text);
				break;

			case Message::Notice:
				TakeNotice(message.answer);
				break;

			case Message::Expire:
				// the selftest's: the deadline of the pending record is now
				if (capture.deadlineArmed)
					capture.deadline = Clock::now();
				break;
			}
		}

		const Clock::time_point now = Clock::now();

		if (now >= c.nextTick)
		{
			PeriodicTick();

			c.nextTick += std::chrono::milliseconds(tick_ms);

			// not a burst of ticks behind a call that took long
			if (c.nextTick < Clock::now())
				c.nextTick = Clock::now() + std::chrono::milliseconds(tick_ms);
		}

		if (c.graceArmed && Clock::now() >= c.graceAt)
			GraceExpired();

		if (c.syncOwed && Clock::now() >= c.syncAt)
			SyncPresence();

		if (c.releaseOwed && Clock::now() >= c.releaseAt)
			ReleaseAgain();

		if (capture.deadlineArmed && Clock::now() >= capture.deadline)
			CaptureDeadline();

		if (capture.cancelOwed && Clock::now() >= capture.cancelAt)
			CancelAgain();
	}
}

// The core thread, with the server or (s null) without one, for the selftest
bool StartThread(net::Server* s)
{
	{
		std::lock_guard<std::mutex> lock(queue.lock);

		queue.messages.clear();
		queue.quit = false;
		queue.accepting = true;
	}

	c = Own();
	capture = CaptureRequests();
	notices_owed.store(0);
	pending_seen.store(false);
	server.store(s);

	try
	{
		thread = new std::thread(ThreadMain);
	}
	catch (...)
	{
		std::lock_guard<std::mutex> lock(queue.lock);

		queue.accepting = false;
		server.store(nullptr);
		return false;
	}

	started.store(true);

	// the notice of a capture, which (C) takes
	sgc::SetCompletionNotice(CaptureNotice);

	emu::SetListener(OnRunState);
	return true;
}

}

/*
	The entry points
*/

void Configure(u32 tickMs, u32 fullTickEvery)
{
	tick_ms = tickMs >= 1 ? tickMs : kDefaultTickMs;
	full_tick_every = fullTickEvery >= 1 ? fullTickEvery : kDefaultFullTickEvery;
}

bool Started()
{
	return started.load();
}

bool Start(net::Server* s)
{
	if (started.load() || s == nullptr)
		return false;

	return StartThread(s);
}

void Stop()
{
	if (!started.load())
		return;

	// no invocation of the listener is in progress when this returns. A notice that comes from here on finds the
	// queue closed
	emu::SetListener(nullptr);
	sgc::SetCompletionNotice(nullptr);

	{
		std::lock_guard<std::mutex> lock(queue.lock);

		queue.accepting = false;
		queue.quit = true;
		queue.messages.clear();
	}

	queue.wake.notify_all();

	// A call the thread waits for ends, or is given up by its timeout: the thread comes back from it and finds
	// the flag
	thread->join();

	delete thread;
	thread = nullptr;

	server.store(nullptr);

	{
		std::lock_guard<std::mutex> lock(clients_lock);

		clients.clear();
		client_count.store(0);
	}

	started.store(false);
}

net::Handler* NetHandler()
{
	// lives for the whole process
	static CoreHandler* handler = new CoreHandler();

	return handler;
}

void OnRunState(const emu::RunState& state)
{
	try
	{
		BroadcastTick(state);
	}
	catch (...)
	{
	}
}

std::string BuildTickText(bool full)
{
	return BuildTick(emu::GetRunState(), full);
}

std::string HandleRequestText(const std::string& text)
{
	Reply reply = Dispatch(0, text);
	std::string answer = reply.text;

	// to the sink of the selftest, where it has one: the response, then the binary frame
	Deliver(0, reply);

	return answer;
}

std::string DescribeText()
{
	return BuildDescribe(emu::GetRunState());
}

void SetClientCountForTest(u32 count, bool graceExpired)
{
	client_count.store(count);
	grace_expired.store(graceExpired);

	PushPresence();
}

u32 ActiveMaskForTest()
{
	return last_active_mask;
}

void SetTickSinkForTest(void (*sink)(const std::string& tick))
{
	tick_sink.store(sink);
}

void SetSendSinkForTest(void (*sink)(u32 client, const std::string& message, bool binary))
{
	send_sink.store(sink);
}

bool StartForTest()
{
	if (started.load())
		return false;

	return StartThread(nullptr);
}

void TextForTest(u32 client, const std::string& text)
{
	// a connection of its own, for its binaryIds; it counts as no client (client_count)
	{
		std::lock_guard<std::mutex> lock(clients_lock);
		bool known = false;

		for (const ClientEntry& entry : clients)
			known = known || entry.id == client;

		if (!known)
			clients.push_back({ client, false, 0 });
	}

	Enqueue({ Message::Text, client, text });
}

void SetCaptureTimeoutForTest(u32 ms)
{
	capture_timeout_ms.store(ms != 0 ? ms : kCaptureTimeoutMs);
}

void ExpireCaptureDeadlineForTest()
{
	Enqueue({ Message::Expire, 0, std::string() });
}

void SetBeforeArmForTest(void (*hook)())
{
	before_arm.store(hook);
}

void SetBeforeTableCallForTest(void (*hook)())
{
	before_table_call.store(hook);
}

CaptureCounts CaptureCountsForTest()
{
	CaptureCounts counts;

	counts.pending = pending_seen.load();
	counts.owed = notices_owed.load();
	counts.notices = notices_taken.load();
	counts.unpaired = notices_unpaired.load();
	counts.deadlinesMade = deadlines_made.load();
	counts.deadlinesTaken = deadlines_taken.load();
	counts.deadlinesUntaken = deadlines_untaken.load();
	counts.answeredByCore = answered_by_core.load();
	counts.cancelsAgain = cancels_again.load();

	return counts;
}

std::string PrintableAscii(const char* text, size_t length)
{
	std::string out(text, length);

	for (char& ch : out)
	{
		if (!Printable((unsigned char)ch))
			ch = '?';
	}

	return out;
}

} }
