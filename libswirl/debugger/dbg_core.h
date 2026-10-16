/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include <string>
#include "dbg_emu.h"

/*
	dbg::core: the wire protocol (dbg_core.cpp: JSON-RPC dispatch, the breakpoint, watch and category
	tables, the device tree, the tick builder and the event log), and the core thread (C) that serves it.

	(C) takes the requests that the network thread queued, validates them and reaches guest state and the
	tables through dbg::emu::Call; it also owns the timers: the periodic tick, the capture timeout and the
	grace period after the last client left. It never holds L while it waits for (E). The tables themselves
	follow the one rule: they are touched inside calls and in the listener only. A tick is handed to the
	server inside the call or the listener that built it, so ticks reach a client in the order they were
	built; the response to debugger.describe is handed over inside the same call as its tick, directly in
	front of it.
*/

namespace dbg {

namespace net {
class Server;		// dbg_net.h
struct Handler;
}

namespace core {

// (U), from dbg::Init, once; lifetime NoMachine. Starts (C) and installs OnRunState as the run-state
// listener. server is where ticks and responses are sent; it is not owned and must stay valid until Stop()
// returned. It does not have to be started yet: NetHandler() is passed to its Start().
// Returns false, with nothing started or installed, when the thread cannot be created.
bool Start(net::Server* server);

// (U), once after a successful Start(): from dbg::Term, or from dbg::Init when the server could not be
// started behind that Start() (dbg::Term then makes no second call); any lifetime state. Removes the
// listener, ends (C) and joins it; a call (C) is waiting for is given up as it would be after its timeout.
// The caller must not hold L. Afterwards nothing of the core runs and the server is no longer used.
void Stop();

// The handler of the WebSocket traffic, for net::Server::Start. The object lives for the whole process.
// Its methods run on the network thread and only queue for (C): they never block and never touch guest
// state or the tables. Before Start() and after Stop() they drop what they get.
net::Handler* NetHandler();

// The run-state listener (dbg::emu::SetListener): builds the tick for the state just published and hands it
// to the server. Invoked by dbg::emu only, under the listener's contract.
void OnRunState(const emu::RunState& state);

// The text of a tick as the listener would send it now, full or with the paths of a running tick only.
// On (E), or on a thread that holds L while no emulator thread exists; for the selftest. It advances the
// tick counter like any tick that is built.
std::string BuildTickText(bool full);

}

}
