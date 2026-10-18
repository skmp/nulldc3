/*
	This file is part of libswirl
	BSD 3-clause, see license/bsd
*/

#pragma once

// Debugger transport: an HTTP/1.1 static file server and an RFC 6455 WebSocket server on one listening
// socket, served by one network thread.
//
// This header and dbg_net.cpp stay free of tree headers (types.h pulls <Windows.h>, which must not come
// before <winsock2.h>), so they also build outside the emulator.

#include <cstdint>
#include <string>
#include <vector>

namespace dbg { namespace net {

// Receives the WebSocket traffic. Every method is called on the network thread, never concurrently.
// A handler may call the Send, Broadcast and query methods of the Server. Stop from a handler only asks the
// network thread to end; a Stop from another thread completes it.
struct Handler {
	virtual void OnOpen(uint32_t client) = 0;
	virtual void OnText(uint32_t client, const std::string& text) = 0;
	virtual void OnClose(uint32_t client) = 0;
	// Binary messages have no consumer in the debugger protocol; they are dropped unless overridden.
	virtual void OnBinary(uint32_t client, const std::string& bytes) { (void)client; (void)bytes; }
	virtual ~Handler() {}
};

struct Options {
	std::string bind = "127.0.0.1";			// numeric address or host name; the first resolved address is used
	int port = 55543;						// 0 = OS-assigned, see Server::Port
	std::string webRoot;					// serve static files from this directory instead of the embedded UI
	std::vector<std::string> allowedHosts;	// extra Host / Origin hosts, IPv6 literals in brackets
	size_t maxMessage = 1u << 20;			// largest inbound WebSocket message
	int maxClients = 16;					// WebSocket clients; further upgrades get 503
};

// Requests are served only when the Host header (port stripped, case-insensitive) is 127.0.0.1, localhost,
// [::1] or an entry of allowedHosts; a WebSocket upgrade at /ws also needs Origin to be absent or to name one
// of those hosts. Everything else is answered 403, which is what stops a web page that rebinds its own
// name to this address.
//
// Every GET other than /ws is a static file, from Options::webRoot or else from the static provider below;
// the query string is ignored. A path that is not found is answered with index.html when its last segment
// has no extension (a route of the page), and with 404 when it has one.
//
// webRoot is a narrow file name as std::filesystem::path takes one: the bytes as they are on POSIX systems; on
// Windows text that the C++ library converts, from the active code page with the Microsoft library (what
// fopen takes there). A root that is not valid in that encoding is like one that does not exist: it has no
// files, every path is 404.
//
// Client ids start at 1 and are not reused while the Server object lives. OnOpen is followed by exactly one
// OnClose, also when the server stops.
class Server {
public:
	Server();
	~Server();

	// False when the address cannot be resolved or bound, when the port is not in 0..65535, without a handler,
	// on a server that is already started, and when the socket library, the wake-up socket or the network
	// thread cannot be set up; one line on stderr says which. Start and Stop are for one controlling
	// thread; everything else may be called from any thread at any time.
	bool Start(const Options& options, Handler* handler);
	void Stop();

	int Port() const;			// the bound port, 0 while stopped
	bool HasClients() const;	// lock-free: at least one WebSocket client is open

	// Queue a message and return; nothing here waits for the socket. Unknown or closed clients are ignored.
	// Text and binary messages are counted apart and go out in the order they were queued. A client that lets
	// more than 16 MiB of text, or more than 64 MiB of binary messages, pile up is disconnected: a text message
	// that finds more than 16 MiB of text waiting for the client, or a binary message that finds more than
	// 64 MiB of binary messages waiting, is not queued and the client is dropped. The bounds are not on the
	// message, one of any size is taken: a binary message of the largest size the protocol gives (32 MiB and its
	// header) leaves room for every text message behind it.
	void SendText(uint32_t client, std::string text);
	void SendBinary(uint32_t client, std::string bytes);
	void BroadcastText(const std::string& text);
	size_t QueuedBytes(uint32_t client) const;	// bytes not yet handed to the socket, text and binary together

private:
	Server(const Server&);
	Server& operator=(const Server&);

	struct Impl;
	Impl* impl;
};

// Static content for requests when Options::webRoot is empty. `path` is relative and already sanitised
// ("index.html", "assets/app.js"). dbg_webui.cpp installs the embedded UI bundle from a static
// initialiser; without a provider every path is 404.
struct StaticFile {
	std::string body;
	bool gzip = false;		// body is a gzip stream, sent with Content-Encoding: gzip
};
typedef bool (*StaticProvider)(const std::string& path, bool acceptGzip, StaticFile* file);
void SetStaticProvider(StaticProvider provider);

} }
