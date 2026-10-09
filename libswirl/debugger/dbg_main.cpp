/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_core_private.h"
#include "dbg_net.h"
#include "dbg_selftest.h"

#include "cfg/cfg.h"
#include "utils/run_control.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

/*
	dbg::Init and dbg::Term: the configuration of the server (section Debugger), the server object, and the
	wiring of the transport, the core and the emulator side.

	The server is the control surface of the emulator, not only a debugger: it is started whether or not a game
	is loaded, and what it is started with says nothing about clients. Nothing here writes a configuration key.

	What dbg::Init decides, in this order (design 2.11):
	  1. Enabled. Its default is yes, and no in an automated run and in a selftest session: there the server is
	     off unless the key is set explicitly.
	  2. Not enabled: a key that needs the server (Required; StartPaused outside a selftest session) is a
	     configuration error, one line on stderr and exit code 2. Otherwise nothing is started.
	  3. Enabled: the other keys, the rule for Bind, then the core and the transport. A server that cannot be
	     started is one line on stderr; with a key that needs the server it ends the process with exit code 2.
	No machine and no emulator thread exist yet where the process ends here, and no nulldc-exit line is
	printed: that line is for a run that had started.
*/

namespace dbg {

namespace {

// What Init() left running
net::Server* server;

std::string Lower(std::string text)
{
	for (char& c : text)
	{
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	}

	return text;
}

// One of the three texts the Host rule of the transport takes without AllowedHosts. Text is compared and
// nothing is resolved: 127.0.0.2, or a name that resolves to the loopback interface, is no loopback address
bool IsLoopback(const std::string& bind)
{
	const std::string text = Lower(bind);

	return text == "127.0.0.1" || text == "localhost" || text == "::1";
}

// "a, b ,c": the entries without the blanks around them
std::vector<std::string> SplitHosts(const std::string& list)
{
	std::vector<std::string> hosts;
	size_t at = 0;

	while (at <= list.size())
	{
		size_t end = list.find(',', at);

		if (end == std::string::npos)
			end = list.size();

		size_t first = at, last = end;

		while (first < last && (list[first] == ' ' || list[first] == '\t'))
			first++;
		while (last > first && (list[last - 1] == ' ' || list[last - 1] == '\t'))
			last--;

		if (last > first)
			hosts.push_back(list.substr(first, last - first));

		at = end + 1;
	}

	return hosts;
}

// The configuration cannot be run: nothing has been started, and nothing is
[[noreturn]] void Refuse(const std::string& line)
{
	fprintf(stderr, "%s\n", line.c_str());
	fflush(stderr);
	exit(2);
}

}

void Init()
{
	// The default of Enabled, and the key. A test run opens no listening socket unasked; the tests of a
	// selftest session stand in place of a client, and a client beside them would rewrite what they set
	const bool session = selftest::Requested();
	const bool enabled = cfgLoadBool("Debugger", "Enabled", !(runctl_automated() || session));

	const bool required = cfgLoadBool("Debugger", "Required", false);

	// dbg::emu's key; read here for the check alone. A selftest session resumes its start park itself
	const bool start_paused = cfgLoadBool("Debugger", "StartPaused", false) && !session;

	if (!enabled)
	{
		// a key that needs the server never silently does nothing
		if (required)
			Refuse("Debugger: Debugger:Required=yes needs the server, which is not enabled: Debugger:Enabled=yes is missing "
				"(the server is off by default in a headless run, in a run that an Exit.* key ends and in a selftest session)");

		if (start_paused)
			Refuse("Debugger: Debugger:StartPaused=yes needs the server, which is not enabled: Debugger:Enabled=yes is missing "
				"(the server is off by default in a headless run, in a run that an Exit.* key ends and in a selftest session); "
				"nothing could resume the start park");

		return;
	}

	// With one of the two a server that cannot be started ends the process: a start park without a server has
	// nothing to resume it, whichever way the server came to be missing
	const char* const needed_by = required ? "Debugger:Required=yes" : start_paused ? "Debugger:StartPaused=yes" : nullptr;

	auto failed = [needed_by](const std::string& why) {
		if (needed_by != nullptr)
			Refuse("Debugger: " + why + ", and " + needed_by + " needs the server: the emulator does not start");

		fprintf(stderr, "Debugger: %s: the emulator runs without the server\n", why.c_str());
		fflush(stderr);
	};

	net::Options options;

	options.bind = cfgLoadStr("Debugger", "Bind", "127.0.0.1");
	options.port = cfgLoadInt("Debugger", "Port", 55543);
	options.webRoot = cfgLoadStr("Debugger", "WebRoot", "");
	options.allowedHosts = SplitHosts(cfgLoadStr("Debugger", "AllowedHosts", ""));
	options.maxMessage = kMaxMessageBytes;

	s32 tick_ms = cfgLoadInt("Debugger", "TickMs", (s32)core::kDefaultTickMs);
	s32 full_tick_every = cfgLoadInt("Debugger", "FullTickEvery", (s32)core::kDefaultFullTickEvery);

	if (tick_ms < 1)
		tick_ms = (s32)core::kDefaultTickMs;
	if (full_tick_every < 1)
		full_tick_every = (s32)core::kDefaultFullTickEvery;

	// Decided before anything is started: the server has no authentication, and off the loopback interface
	// the names it answers to have to be said
	if (!IsLoopback(options.bind))
	{
		if (options.allowedHosts.empty())
		{
			failed("the server is not started: Debugger:Bind=" + options.bind + " is not a loopback address (127.0.0.1, localhost, ::1) and "
				"Debugger:AllowedHosts is empty");
			return;
		}

		fprintf(stderr, "Debugger: warning: Debugger:Bind=%s is not a loopback address: the server can be reached from other machines, "
			"and it has no authentication\n", options.bind.c_str());
		fflush(stderr);
	}

	core::Configure((u32)tick_ms, (u32)full_tick_every);

	net::Server* s = new net::Server();

	if (!core::Start(s))
	{
		delete s;
		failed("the server could not be started (the core thread)");
		return;
	}

	// says on stderr what it could not do: the address, the port, the socket library, its thread
	if (!s->Start(options, core::NetHandler()))
	{
		// no core thread and no run-state listener stay behind
		core::Stop();
		delete s;
		failed("the server could not be started on " + options.bind + " port " + std::to_string(options.port));
		return;
	}

	server = s;

	// Test harnesses read this line for the port. A Bind with a colon is an IPv6 literal: in brackets, so that
	// the line is a URL
	if (options.bind.find(':') != std::string::npos)
		printf("Debugger: http://[%s]:%d/\n", options.bind.c_str(), s->Port());
	else
		printf("Debugger: http://%s:%d/\n", options.bind.c_str(), s->Port());

	fflush(stdout);
}

void Term()
{
	// nothing, where Init() left nothing running
	if (server == nullptr)
		return;

	// The listener goes and the core thread ends first: a call it waits for is given up as by its timeout.
	// Then every client is closed and the network thread ends; the handler drops what it still hears
	core::Stop();
	server->Stop();

	delete server;
	server = nullptr;
}

}
