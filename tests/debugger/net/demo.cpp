// The debugger transport on its own: dbg::net::Server with the embedded UI and an echo handler where the
// emulator has the debugger. For trying the server from a browser and for interop_ws.js.
//
//   dbg_net_demo [--port N] [--bind ADDR] [--webroot DIR] [--allow HOST]... [--max-message BYTES] [--max-clients N]
//
// Prints "Debugger: http://<address>:<port>/" once it listens, and runs until stdin ends or reads "quit".
//
// Text messages are echoed, except:
//   binary:<n>        answered with n bytes (byte i is i * 7 + i / 255 + 3) as a binary message
//   broadcast:<text>  the text goes to every client
//   queued            answered with Server::QueuedBytes of the client
// Binary messages are echoed.

#include "debugger/dbg_net.h"
#include "debugger/dbg_webui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

struct Echo : dbg::net::Handler {
	dbg::net::Server* server = nullptr;

	void OnOpen(uint32_t client) override {
		printf("client %u: open\n", client);
		fflush(stdout);
	}

	void OnText(uint32_t client, const std::string& text) override {
		if (text.compare(0, 7, "binary:") == 0) {
			std::string bytes((size_t)strtoull(text.c_str() + 7, nullptr, 10), '\0');
			for (size_t i = 0; i < bytes.size(); i++)
				bytes[i] = (char)(i * 7 + i / 255 + 3);
			server->SendBinary(client, std::move(bytes));
		} else if (text.compare(0, 10, "broadcast:") == 0) {
			server->BroadcastText(text.substr(10));
		} else if (text == "queued") {
			server->SendText(client, std::to_string(server->QueuedBytes(client)));
		} else {
			server->SendText(client, text);
		}
	}

	void OnBinary(uint32_t client, const std::string& bytes) override {
		server->SendBinary(client, bytes);
	}

	void OnClose(uint32_t client) override {
		printf("client %u: closed\n", client);
		fflush(stdout);
	}
};

int main(int argc, char** argv) {
	dbg::net::Options options;
	for (int i = 1; i < argc; i += 2) {
		const char* value = i + 1 < argc ? argv[i + 1] : nullptr;
		if (!strcmp(argv[i], "--port") && value) {
			options.port = atoi(value);
		} else if (!strcmp(argv[i], "--bind") && value) {
			options.bind = value;
		} else if (!strcmp(argv[i], "--webroot") && value) {
			options.webRoot = value;
		} else if (!strcmp(argv[i], "--allow") && value) {
			options.allowedHosts.push_back(value);
		} else if (!strcmp(argv[i], "--max-message") && value) {
			options.maxMessage = (size_t)strtoull(value, nullptr, 10);
		} else if (!strcmp(argv[i], "--max-clients") && value) {
			options.maxClients = atoi(value);
		} else {
			fprintf(stderr, "usage: %s [--port N] [--bind ADDR] [--webroot DIR] [--allow HOST]... [--max-message BYTES] [--max-clients N]\n", argv[0]);
			return 2;
		}
	}

	Echo echo;
	dbg::net::Server server;
	echo.server = &server;
	if (!server.Start(options, &echo))
		return 1;
	printf("Debugger: http://%s:%d/\n", options.bind.c_str(), server.Port());
	printf("UI: %s, protocol %d\n", dbg::webui::Version(), dbg::kProtocolVersion);
	fflush(stdout);

	char line[256];
	while (fgets(line, sizeof(line), stdin)) {
		if (!strncmp(line, "quit", 4))
			break;
	}
	server.Stop();
	return 0;
}
