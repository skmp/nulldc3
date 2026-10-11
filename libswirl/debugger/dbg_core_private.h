/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include <string>
#include <vector>
#include "dbg_core.h"

/*
	What the core's own files share: dbg_core.cpp, dbg_main.cpp, dbg_events.cpp and dbg_selftest_core.cpp. The
	selftests of other packages include it for the parts named below, and nothing else does. Not one of the
	frozen headers; it has no JSON type (json.hpp is for the sources).

	  dbg_main.cpp             hands the core the tick period and the full-tick count it read (Configure)
	  dbg_events.cpp           gives the tick builder the event log (events::Snapshot) and takes the one entry that
	                           is made with every gate closed (events::Append)
	  dbg_selftest_core.cpp    hands request texts to the dispatcher and reads what a client would be sent, in a
	                           session that has no server and no core thread
	  dbg_selftest_arm7.cpp    reads the event log (events::Snapshot): what a sound burst logged while the park
	                           loop ran it on
	  dbg_selftest_writes.cpp  hands request texts to the dispatcher, as dbg_selftest_core.cpp does: the writes
	                           of a client in a run that has to stay the run nobody touched
*/

namespace dbg {

namespace core {

// The defaults of Debugger:TickMs and Debugger:FullTickEvery
const u32 kDefaultTickMs = 200;
const u32 kDefaultFullTickEvery = 5;

// (U), from dbg::Init, before Start(). The period of the running tick in milliseconds and which periodic
// tick is a full one (every n-th); a value below 1 is replaced by its default. A core that was never
// configured has the defaults.
void Configure(u32 tickMs, u32 fullTickEvery);

// Any thread. Is the core thread there: true from a Start() that succeeded until Stop() has returned.
// A selftest session has no server unless Debugger:Enabled is set explicitly; every test of the session
// "core" checks first that this is false, and does not run beside (C).
bool Started();

/*
	For the selftest (dbg_selftest_core.cpp). None of it is called while the core thread is there.
*/

// One request, as the text of a WebSocket message, to the dispatcher; returns the text of the response, and
// the empty string where a client gets no answer (a notification).
//   On (E), from a test: the request is served in place. The test stands where a call of every class may
//   stand (a machine test, a guest test at its stop), and no dbg::emu::Call is made, which a test on (E)
//   must not make.
//   On a thread the test started: the request comes in as it does on (C), through dbg::emu::Call. The caller
//   holds neither L nor a lock that a call takes.
// Nothing is sent anywhere, and no tick is built for nobody: there is no server and no client. A test reads
// ticks with BuildTickText().
std::string HandleRequestText(const std::string& text);

// The result of debugger.describe as its JSON text (the value of "result"), for the state as it is now.
// On (E), or on a thread that holds L while no emulator thread exists.
std::string DescribeText();

// For a session without a server: states the number of connected clients, which the core otherwise counts
// from the OnOpen / OnClose of its handler. On (E), or on a thread that holds L while no emulator thread
// exists: the event gates and the active mask are rewritten for it at once, as the core thread has them
// rewritten in a table call when a client connects or disconnects. `graceExpired` is what the grace period
// says with no client connected: false, the last client left less than kClientGraceMs ago (breakpoints stay
// active); true, it has expired.
void SetClientCountForTest(u32 clients, bool graceExpired);

// The mask of active categories that the core last handed to dbg::emu::SetActiveMask(), which has no getter:
// CategoryBit() of every category that is not muted and is soloed or has no other soloed, and 0 once the grace
// period behind the last client has expired. On (E), or under L while no emulator thread exists.
u32 ActiveMaskForTest();

// For a session without a server: `sink` gets the text of every tick the core would broadcast to its clients
// from inside a call - behind a request that changed the tables, and behind one that wrote guest state - in
// place of them; null, the default: such a tick is not built. It is called where the tick is built: on (E), or
// on the thread that holds L while no emulator thread exists. Set and cleared on (E), or under L while no
// emulator thread exists. (The run-state listener, whose ticks are those of the stops and resumes, is
// installed with the server: a session has none, and a test reads those with BuildTickText().)
void SetTickSinkForTest(void (*sink)(const std::string& tick));

// The text that a string of guest or host origin has on the wire: every byte outside printable ASCII
// (0x20..0x7E) is '?'. Any thread.
std::string PrintableAscii(const char* text, size_t length);

}

namespace events {

// One entry of the event log
struct Entry {
	u64 seq;				// eventId: counts the entries of the process from 1
	u64 timestampMs;		// wall clock, milliseconds since the Unix epoch
	u64 cycles;				// sh4_sched_now64() when it was raised, 0 without a machine
	EventId id;
	std::string message;	// the formatted text, as the caller gave it: not made printable
};

// The log, oldest first: at most kEventRingSize entries. Any thread; takes the log's own mutex for the copy.
void Snapshot(std::vector<Entry>* out);

// Appends an entry whatever the gates say, and tests no event breakpoint: for the note of the grace period,
// which is made while no client is connected (every gate is closed then, and DBG_EVENT would evaluate
// nothing). Any thread; takes the log's own mutex for the append. The text is cut to kEventMessageMax bytes,
// terminator included.
void Append(EventId id, const char* text);

// Empties the log; the sequence numbers go on. For the selftest, which looks at a log of its own entries.
void ClearForTest();

}

}
