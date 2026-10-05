// dbg::net::Server end to end over loopback, through its public interface only, with the test's own
// client on the other side.

#include "client.h"

#include "debugger/dbg_net.h"

#include "check.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace dbg::net;

static const char kAccept[] = "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";	// for the key client.h sends
static const size_t kMaxMessage = 256 * 1024;
static const size_t kBigFile = 3 * 1024 * 1024 + 17;

static std::string Pattern(size_t n) {
	std::string s(n, '\0');
	for (size_t i = 0; i < n; i++)
		s[i] = (char)(i * 7 + i / 255 + 3);
	return s;
}

static bool Provider(const std::string& path, bool acceptGzip, StaticFile* file) {
	file->gzip = false;
	if (path == "index.html") {
		file->body = acceptGzip ? "<gzip page>" : "<plain page>";
		file->gzip = acceptGzip;
	} else if (path == "assets/big.bin") {
		file->body = Pattern(kBigFile);
	} else {
		return false;
	}
	return true;
}

// Records what the server reports and answers a few commands:
//   echo:<text>       the text back
//   broadcast:<text>  the text to every client
//   binary:<n>        n bytes of Pattern as a binary message
//   stop              Server::Stop from inside the handler
// Binary messages are echoed.
struct Recorder : Handler {
	Server* server = nullptr;
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<uint32_t> opened;
	std::vector<uint32_t> closed;
	std::vector<std::pair<uint32_t, std::string>> texts;
	std::vector<std::pair<uint32_t, std::string>> binaries;
	std::thread::id thread;
	bool oneThread = true;

	void Note() {
		if (thread == std::thread::id())
			thread = std::this_thread::get_id();
		else if (thread != std::this_thread::get_id())
			oneThread = false;
	}

	void OnOpen(uint32_t client) override {
		std::lock_guard<std::mutex> lock(mutex);
		Note();
		opened.push_back(client);
		changed.notify_all();
	}

	void OnText(uint32_t client, const std::string& text) override {
		{
			std::lock_guard<std::mutex> lock(mutex);
			Note();
			texts.emplace_back(client, text);
			changed.notify_all();
		}
		if (text.compare(0, 5, "echo:") == 0)
			server->SendText(client, text.substr(5));
		else if (text.compare(0, 10, "broadcast:") == 0)
			server->BroadcastText(text.substr(10));
		else if (text.compare(0, 7, "binary:") == 0)
			server->SendBinary(client, Pattern((size_t)atoll(text.c_str() + 7)));
		else if (text == "stop")
			server->Stop();
	}

	void OnBinary(uint32_t client, const std::string& bytes) override {
		{
			std::lock_guard<std::mutex> lock(mutex);
			Note();
			binaries.emplace_back(client, bytes);
			changed.notify_all();
		}
		server->SendBinary(client, bytes);
	}

	void OnClose(uint32_t client) override {
		std::lock_guard<std::mutex> lock(mutex);
		Note();
		closed.push_back(client);
		changed.notify_all();
	}

	size_t Opened() {
		std::lock_guard<std::mutex> lock(mutex);
		return opened.size();
	}

	size_t Closed() {
		std::lock_guard<std::mutex> lock(mutex);
		return closed.size();
	}

	uint32_t LastOpened() {
		std::lock_guard<std::mutex> lock(mutex);
		return opened.empty() ? 0 : opened.back();
	}

	bool WasClosed(uint32_t client) {
		std::lock_guard<std::mutex> lock(mutex);
		return std::count(closed.begin(), closed.end(), client) == 1;
	}

	bool WaitOpened(size_t count, int ms = 5000) {
		std::unique_lock<std::mutex> lock(mutex);
		return changed.wait_for(lock, std::chrono::milliseconds(ms), [&] { return opened.size() >= count; });
	}

	bool WaitClosed(size_t count, int ms = 5000) {
		std::unique_lock<std::mutex> lock(mutex);
		return changed.wait_for(lock, std::chrono::milliseconds(ms), [&] { return closed.size() >= count; });
	}
};

// Opens a WebSocket client and returns the id the server gave it.
static uint32_t Open(TestClient& ws, Recorder& rec, int port, const std::string& headers = "") {
	size_t before = rec.Opened();
	REQUIRE(ws.Upgrade(port, headers) == 101);
	REQUIRE(rec.WaitOpened(before + 1));
	return rec.LastOpened();
}

static std::string Text(TestClient& ws, int timeoutMs = 5000) {
	WsFrame frame;
	if (!ws.ReadFrame(&frame, timeoutMs))
		return "<no frame>";
	if (frame.opcode != 1 || !frame.fin || frame.masked)
		return "<not a text frame>";
	return frame.payload;
}

static bool ExpectClose(TestClient& ws, int code) {
	WsFrame frame;
	if (!ws.ReadFrame(&frame) || frame.opcode != 8 || frame.CloseCode() != code)
		return false;
	return ws.WaitClosed();
}

static void TestHttp(int port) {
	HttpResult r = TestClient::Get(port, "/");
	CHECK_EQ(r.status, 200);
	CHECK_EQ(r.body, std::string("<plain page>"));
	CHECK_EQ(r.Header("Content-Type"), std::string("text/html; charset=utf-8"));
	CHECK_EQ(r.Header("Content-Length"), std::string("12"));
	CHECK_EQ(r.Header("Cache-Control"), std::string("no-cache"));
	CHECK_EQ(r.Header("X-Content-Type-Options"), std::string("nosniff"));
	CHECK_EQ(r.Header("Connection"), std::string("close"));
	CHECK_EQ(r.Header("Content-Encoding"), std::string(""));

	r = TestClient::Get(port, "/index.html?x=1", "Accept-Encoding: gzip, deflate, br\r\n");
	CHECK_EQ(r.body, std::string("<gzip page>"));
	CHECK_EQ(r.Header("Content-Encoding"), std::string("gzip"));

	r = TestClient::Get(port, "/assets/big.bin");
	CHECK_EQ(r.status, 200);
	CHECK(r.body == Pattern(kBigFile));
	CHECK_EQ(r.Header("Cache-Control"), std::string("max-age=31536000, immutable"));
	CHECK_EQ(r.Header("Content-Type"), std::string("application/octet-stream"));

	CHECK_EQ(TestClient::Get(port, "/missing.js").status, 404);
	CHECK_EQ(TestClient::Get(port, "/../etc/passwd").status, 404);
	CHECK_EQ(TestClient::Request(port, "GET / HTTP/1.1\r\nHost: evil.example\r\n\r\n").status, 403);
	CHECK_EQ(TestClient::Request(port, "GET / HTTP/1.1\r\nHost: localhost.evil.example:" + std::to_string(port) + "\r\n\r\n").status, 403);
	CHECK_EQ(TestClient::Request(port, "GET / HTTP/1.1\r\n\r\n").status, 403);
	CHECK_EQ(TestClient::Request(port, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n").status, 200);
	CHECK_EQ(TestClient::Request(port, "GET / HTTP/1.1\r\nHost: [::1]:" + std::to_string(port) + "\r\n\r\n").status, 200);
	CHECK_EQ(TestClient::Request(port, "HEAD / HTTP/1.1\r\nHost: localhost\r\n\r\n").status, 405);
	const char garbage[] = "\x00\x01\x02 garbage\r\n\r\n";
	CHECK_EQ(TestClient::Request(port, std::string(garbage, sizeof(garbage) - 1)).status, 400);
	CHECK_EQ(TestClient::Request(port, std::string(9000, 'a')).status, 414);
	CHECK_EQ(TestClient::Request(port, "GET / HTTP/1.1\r\nX: " + std::string(9000, 'a')).status, 431);

	// a body the server never reads must not cost the client its response
	std::string post = "POST /upload HTTP/1.1\r\nHost: localhost\r\nContent-Length: 300000\r\n\r\n" + std::string(300000, 'p');
	r = TestClient::Request(port, post);
	CHECK_EQ(r.status, 405);
	CHECK_EQ(r.Header("Allow"), std::string("GET"));

	// the server closes after every response
	TestClient client;
	REQUIRE(client.Connect(port));
	REQUIRE(client.Send("GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
	bool timedOut = false;
	std::string all = client.ReadToEnd(5000, &timedOut);
	CHECK(!timedOut);
	CHECK(all.find("<plain page>") != std::string::npos);
	CHECK(all.find("HTTP/1.1", 10) == std::string::npos);		// one response only

	// a request that arrives in pieces
	REQUIRE(client.Connect(port));
	for (const char* piece : { "GET / HT", "TP/1.1\r\nHo", "st: localhost\r", "\n\r", "\n" }) {
		REQUIRE(client.Send(piece));
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	all = client.ReadToEnd(5000, &timedOut);
	CHECK(!timedOut);
	CHECK(all.find("<plain page>") != std::string::npos);

	// a client that closes its sending side right after the request still gets all of a large response
	REQUIRE(client.Connect(port));
	REQUIRE(client.Send("GET /assets/big.bin HTTP/1.1\r\nHost: localhost\r\n\r\n"));
	client.ShutdownSend();
	all = client.ReadToEnd(10000, &timedOut);
	CHECK(!timedOut);
	size_t body = all.find("\r\n\r\n");
	REQUIRE(body != std::string::npos);
	CHECK(all.substr(body + 4) == Pattern(kBigFile));
}

static void TestWebRoot() {
	namespace fs = std::filesystem;
	fs::path top = fs::temp_directory_path() / ("dbg_net_test_e2e_" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(top / "root" / "assets");
	std::ofstream(top / "root" / "index.html", std::ios::binary) << "<disk page>";
	std::ofstream(top / "root" / "assets" / "big-0123.wasm", std::ios::binary) << Pattern(kBigFile);
	std::ofstream(top / "secret.txt", std::ios::binary) << "secret";
	std::error_code ec;
	fs::create_symlink(top / "secret.txt", top / "root" / "link.txt", ec);

	Recorder rec;
	Server server;
	rec.server = &server;
	Options options;
	options.port = 0;
	options.webRoot = (top / "root").string();
	REQUIRE(server.Start(options, &rec));
	int port = server.Port();

	HttpResult r = TestClient::Get(port, "/", "Accept-Encoding: gzip\r\n");
	CHECK_EQ(r.status, 200);
	CHECK_EQ(r.body, std::string("<disk page>"));		// the provider is out of the picture
	CHECK_EQ(r.Header("Content-Encoding"), std::string(""));
	r = TestClient::Get(port, "/assets/big-0123.wasm");
	CHECK(r.body == Pattern(kBigFile));
	CHECK_EQ(r.Header("Content-Type"), std::string("application/wasm"));
	CHECK_EQ(r.Header("Cache-Control"), std::string("max-age=31536000, immutable"));
	for (const char* target : { "/../secret.txt", "/%2e%2e/secret.txt", "/..%2fsecret.txt", "/link.txt", "/assets/../../secret.txt", "/assets/big.bin", "/nothing" }) {
		r = TestClient::Get(port, target);
		CHECK_EQ(r.status, 404);
		CHECK(r.body.find("secret") == std::string::npos);
	}

	// the WebSocket lives next to the files
	TestClient ws;
	Open(ws, rec, port);
	REQUIRE(ws.SendFrame(1, "echo:from the web root server"));
	CHECK_EQ(Text(ws), std::string("from the web root server"));

	server.Stop();
	fs::remove_all(top, ec);
}

static void TestWebSocket(Server& server, Recorder& rec, int port) {
	TestClient ws;
	std::string head;
	size_t opened = rec.Opened();
	size_t before = rec.Closed();
	REQUIRE(ws.Upgrade(port, "Sec-WebSocket-Protocol: chat\r\nSec-WebSocket-Extensions: permessage-deflate\r\n", &head) == 101);
	CHECK(head.find(kAccept) != std::string::npos);
	CHECK(head.find("Sec-WebSocket-Protocol") == std::string::npos);
	CHECK(head.find("Sec-WebSocket-Extensions") == std::string::npos);
	REQUIRE(rec.WaitOpened(opened + 1));
	uint32_t id = rec.LastOpened();
	CHECK(id >= 1);
	CHECK(server.HasClients());

	REQUIRE(ws.SendFrame(1, "echo:hello"));
	CHECK_EQ(Text(ws), std::string("hello"));
	REQUIRE(ws.SendFrame(1, "echo:"));
	CHECK_EQ(Text(ws), std::string(""));
	REQUIRE(ws.SendFrame(1, "echo:\xe2\x82\xac \xf0\x9f\x98\x80"));
	CHECK_EQ(Text(ws), std::string("\xe2\x82\xac \xf0\x9f\x98\x80"));

	// binary, in every length class, both ways
	for (size_t size : { (size_t)0, (size_t)1, (size_t)125, (size_t)126, (size_t)65535, (size_t)65536, kMaxMessage }) {
		std::string bytes = Pattern(size);
		REQUIRE(ws.SendFrame(2, bytes));
		WsFrame frame;
		REQUIRE(ws.ReadFrame(&frame));
		CHECK_EQ(frame.opcode, 2);
		CHECK(frame.fin && !frame.masked);
		CHECK(frame.payload == bytes);
	}

	// a fragmented message with a ping in the middle: the pong comes first, then the echo of the whole
	REQUIRE(ws.SendFrame(1, "echo:fr", false));
	REQUIRE(ws.SendFrame(9, "ping payload"));
	REQUIRE(ws.SendFrame(0, "ag", false));
	REQUIRE(ws.SendFrame(0, "ment"));
	WsFrame frame;
	REQUIRE(ws.ReadFrame(&frame));
	CHECK_EQ(frame.opcode, 10);
	CHECK_EQ(frame.payload, std::string("ping payload"));
	CHECK_EQ(Text(ws), std::string("fragment"));

	// an unsolicited pong is ignored
	REQUIRE(ws.SendFrame(10, "nobody asked"));
	REQUIRE(ws.SendFrame(1, "echo:still here"));
	CHECK_EQ(Text(ws), std::string("still here"));

	// several frames in one write, one frame in many writes
	REQUIRE(ws.Send(TestClient::Frame(1, "echo:a") + TestClient::Frame(1, "echo:b") + TestClient::Frame(9, "") + TestClient::Frame(1, "echo:c")));
	CHECK_EQ(Text(ws), std::string("a"));
	CHECK_EQ(Text(ws), std::string("b"));
	REQUIRE(ws.ReadFrame(&frame));
	CHECK_EQ(frame.opcode, 10);
	CHECK_EQ(Text(ws), std::string("c"));
	std::string slow = TestClient::Frame(1, "echo:byte by byte");
	for (char c : slow)
		REQUIRE(ws.Send(std::string(1, c)));
	CHECK_EQ(Text(ws), std::string("byte by byte"));

	// 8 MiB from the server in one message
	REQUIRE(ws.SendFrame(1, "binary:8388608"));
	REQUIRE(ws.ReadFrame(&frame, 20000));
	CHECK_EQ(frame.opcode, 2);
	CHECK(frame.payload == Pattern(8388608));

	// A message may be larger than the 16 MiB a client is allowed to leave waiting, also right behind a
	// message that is not written yet: both commands arrive together, so both replies are queued together.
	REQUIRE(ws.Send(TestClient::Frame(1, "echo:first") + TestClient::Frame(1, "binary:16777217")));
	CHECK_EQ(Text(ws), std::string("first"));
	REQUIRE(ws.ReadFrame(&frame, 20000));
	CHECK_EQ(frame.opcode, 2);
	CHECK(frame.payload == Pattern(16777217));

	// A send from another thread interrupts the idle network thread: the message is on the wire at once,
	// not at the thread's next periodic look (250 ms).
	long long quickest = 1000000;
	for (int i = 0; i < 5; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		auto start = std::chrono::steady_clock::now();
		server.SendText(id, "wake up");
		CHECK_EQ(Text(ws), std::string("wake up"));
		quickest = std::min<long long>(quickest, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
	}
	printf("cross-thread send to delivery: %lld us at best\n", quickest);
	CHECK(quickest < 100000);

	// sends from another thread, to this client and to ones that do not exist
	server.SendText(id, "from the test thread");
	server.SendText(id + 1000, "to nobody");
	server.SendBinary(0, "to nobody");
	CHECK_EQ(Text(ws), std::string("from the test thread"));
	CHECK_EQ(server.QueuedBytes(id + 1000), (size_t)0);
	for (int i = 0; i < 100 && server.QueuedBytes(id) != 0; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK_EQ(server.QueuedBytes(id), (size_t)0);

	// closing handshake started by the client: the status comes back, then the connection ends
	REQUIRE(ws.SendFrame(8, std::string("\x03\xe8", 2) + "done"));
	CHECK(ExpectClose(ws, 1000));
	REQUIRE(rec.WaitClosed(before + 1));
	CHECK(rec.WasClosed(id));
	CHECK(!server.HasClients());
	server.SendText(id, "too late");
	CHECK_EQ(server.QueuedBytes(id), (size_t)0);

	// frames sent in the same write as the upgrade request are not lost
	REQUIRE(ws.Connect(port));
	REQUIRE(ws.Send("GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n" + TestClient::Frame(1, "echo:early") + TestClient::Frame(8, "")));
	REQUIRE(ws.ReadHead(&head));
	CHECK_EQ(head.substr(0, 12), std::string("HTTP/1.1 101"));
	CHECK_EQ(Text(ws), std::string("early"));
	REQUIRE(ws.ReadFrame(&frame));
	CHECK_EQ(frame.opcode, 8);
	CHECK(ws.WaitClosed());
	REQUIRE(rec.WaitClosed(before + 2));

	// a close frame without a status is answered without one
	uint32_t second = Open(ws, rec, port);
	CHECK(second > id);
	REQUIRE(ws.SendFrame(8, ""));
	REQUIRE(ws.ReadFrame(&frame));
	CHECK_EQ(frame.opcode, 8);
	CHECK(frame.payload.empty());
	CHECK(ws.WaitClosed());
	REQUIRE(rec.WaitClosed(before + 3));

	// a client that just disappears
	uint32_t third = Open(ws, rec, port);
	ws.Close();
	REQUIRE(rec.WaitClosed(before + 4));
	CHECK(rec.WasClosed(third));
	CHECK(!server.HasClients());
}

static void TestOrigin(Recorder& rec, int port) {
	TestClient ws;
	CHECK_EQ(ws.Upgrade(port, "Origin: null\r\n"), 403);
	CHECK_EQ(ws.Upgrade(port, "Origin: http://evil.example\r\n"), 403);
	CHECK_EQ(ws.Upgrade(port, "Origin: http://evil.example:" + std::to_string(port) + "\r\n"), 403);
	CHECK_EQ(ws.Upgrade(port, "Origin: http://localhost.evil.example\r\n"), 403);
	CHECK_EQ(ws.Upgrade(port, "Origin: file://\r\n"), 403);
	size_t opened = rec.Opened();
	CHECK_EQ(ws.Upgrade(port, "Origin: http://localhost:5173\r\n"), 101);
	CHECK_EQ(ws.Upgrade(port, "Origin: http://127.0.0.1:" + std::to_string(port) + "\r\n"), 101);
	CHECK_EQ(ws.Upgrade(port, "Origin: http://devbox.lan\r\n"), 101);		// in allowedHosts
	CHECK(rec.WaitOpened(opened + 3));

	// the rebinding page: its own name in Host and Origin
	TestClient page;
	REQUIRE(page.Connect(port));
	REQUIRE(page.Send("GET /ws HTTP/1.1\r\nHost: evil.example:" + std::to_string(port) + "\r\nOrigin: http://evil.example:" + std::to_string(port) + "\r\n"
		"Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n"));
	std::string all = page.ReadToEnd();
	CHECK_EQ(all.substr(0, 12), std::string("HTTP/1.1 403"));
	// an allowed Host does not help a refused Origin, and a plain request to /ws is not an upgrade
	CHECK_EQ(TestClient::Get(port, "/ws").status, 426);
	ws.Close();
}

static void TestProtocolErrors(Recorder& rec, int port) {
	TestClient ws;
	size_t closed = rec.Closed();

	// unmasked frame
	uint32_t id = Open(ws, rec, port);
	REQUIRE(ws.SendFrame(1, "echo:x", true, false));
	CHECK(ExpectClose(ws, 1002));
	REQUIRE(rec.WaitClosed(++closed));
	CHECK(rec.WasClosed(id));

	// a message above maxMessage: refused from the frame header, the payload is never sent
	Open(ws, rec, port);
	REQUIRE(ws.Send(TestClient::Frame(2, std::string(kMaxMessage + 1, 'x')).substr(0, 14)));
	CHECK(ExpectClose(ws, 1009));
	REQUIRE(rec.WaitClosed(++closed));

	// the same through fragments
	Open(ws, rec, port);
	REQUIRE(ws.SendFrame(2, std::string(kMaxMessage, 'x'), false));
	REQUIRE(ws.SendFrame(0, "y"));
	CHECK(ExpectClose(ws, 1009));
	REQUIRE(rec.WaitClosed(++closed));

	// text that is not UTF-8
	Open(ws, rec, port);
	REQUIRE(ws.SendFrame(1, "echo:\xff\xfe"));
	CHECK(ExpectClose(ws, 1007));
	REQUIRE(rec.WaitClosed(++closed));

	// reserved opcode; a continuation of nothing; a fragmented ping
	for (const std::string& bad : { TestClient::Frame(3, "x"), TestClient::Frame(0, "x"), TestClient::Frame(9, "x", false) }) {
		Open(ws, rec, port);
		REQUIRE(ws.Send(bad));
		CHECK(ExpectClose(ws, 1002));
		REQUIRE(rec.WaitClosed(++closed));
	}

	// after the failure nothing more is delivered from that client
	size_t texts;
	{
		std::lock_guard<std::mutex> lock(rec.mutex);
		texts = rec.texts.size();
	}
	Open(ws, rec, port);
	REQUIRE(ws.Send(TestClient::Frame(1, "x", true, false) + TestClient::Frame(1, "echo:after the error")));
	CHECK(ExpectClose(ws, 1002));
	REQUIRE(rec.WaitClosed(++closed));
	{
		std::lock_guard<std::mutex> lock(rec.mutex);
		CHECK_EQ(rec.texts.size(), texts);
	}

	// random bytes on fresh connections, with and without an upgrade first
	uint32_t seed = 12345;
	for (int i = 0; i < 200; i++) {
		TestClient junk;
		if (i % 2) {
			// 503 when the clients before this one are not all reaped yet
			int status = junk.Upgrade(port);
			CHECK(status == 101 || status == 503);
		} else {
			REQUIRE(junk.Connect(port));
		}
		std::string bytes(1 + seed % 3000, '\0');
		for (char& c : bytes) {
			seed = seed * 1664525u + 1013904223u;
			c = (char)(seed >> 24);
		}
		junk.Send(bytes);
		junk.Close();
	}
	CHECK_EQ(TestClient::Get(port, "/").status, 200);
}

static void TestBroadcastAndCapacity(Server& server, Recorder& rec, int port, int maxClients) {
	// whatever the earlier tests left open is closed by now
	for (int i = 0; i < 200 && server.HasClients(); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	REQUIRE(!server.HasClients());

	std::vector<std::unique_ptr<TestClient>> clients;
	std::vector<uint32_t> ids;
	for (int i = 0; i < maxClients; i++) {
		clients.push_back(std::make_unique<TestClient>());
		ids.push_back(Open(*clients.back(), rec, port));
		if (i > 0)
			CHECK(ids[i] > ids[i - 1]);
	}
	CHECK(server.HasClients());

	// the 17th
	TestClient extra;
	std::string head;
	CHECK_EQ(extra.Upgrade(port, "", &head), 503);
	CHECK(extra.WaitClosed());
	// plain requests are still served
	CHECK_EQ(TestClient::Get(port, "/").status, 200);

	// broadcast from a handler and from this thread
	REQUIRE(clients[3]->SendFrame(1, "broadcast:to everyone"));
	for (auto& client : clients)
		CHECK_EQ(Text(*client), std::string("to everyone"));
	server.BroadcastText("from outside");
	for (auto& client : clients)
		CHECK_EQ(Text(*client), std::string("from outside"));
	server.BroadcastText("");
	for (auto& client : clients)
		CHECK_EQ(Text(*client), std::string(""));

	// a slot frees up when a client leaves
	size_t closed = rec.Closed();
	REQUIRE(clients[5]->SendFrame(8, std::string("\x03\xe9", 2)));
	CHECK(ExpectClose(*clients[5], 1001));
	REQUIRE(rec.WaitClosed(closed + 1));
	CHECK(rec.WasClosed(ids[5]));
	uint32_t id = Open(extra, rec, port);
	CHECK(id > ids.back());
	server.BroadcastText("again");
	for (size_t i = 0; i < clients.size(); i++) {
		if (i != 5)
			CHECK_EQ(Text(*clients[i]), std::string("again"));
	}
	CHECK_EQ(Text(extra), std::string("again"));
	TestClient another;
	CHECK_EQ(another.Upgrade(port), 503);

	for (auto& client : clients)
		client->Close();
	extra.Close();
	REQUIRE(rec.WaitClosed(closed + (size_t)maxClients + 1));
	CHECK(!server.HasClients());
}

static void TestThreads(Server& server, Recorder& rec, int port) {
	TestClient ws;
	uint32_t id = Open(ws, rec, port);
	const int kThreads = 4, kMessages = 2000;
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; t++) {
		threads.emplace_back([&server, id, t] {
			for (int i = 0; i < kMessages; i++) {
				server.SendText(id, std::to_string(t) + ":" + std::to_string(i));
				if (i % 3 == 0)
					server.QueuedBytes(id);
				if (i % 7 == 0)
					server.HasClients();
			}
		});
	}
	std::vector<int> next(kThreads, 0);
	bool ordered = true;
	for (int i = 0; i < kThreads * kMessages; i++) {
		std::string text = Text(ws);
		size_t colon = text.find(':');
		REQUIRE(colon != std::string::npos);
		int t = atoi(text.c_str());
		REQUIRE(t >= 0 && t < kThreads);
		if (atoi(text.c_str() + colon + 1) != next[t]++)
			ordered = false;
	}
	CHECK(ordered);		// per sender, messages arrive in the order they were sent
	for (auto& thread : threads)
		thread.join();
	size_t closed = rec.Closed();
	ws.Close();
	REQUIRE(rec.WaitClosed(closed + 1));
}

static void TestSlowClient(Server& server, Recorder& rec, int port) {
	TestClient slow, good;
	uint32_t slowId = Open(slow, rec, port);
	uint32_t goodId = Open(good, rec, port);
	size_t closed = rec.Closed();

	// The slow client never reads. Its queue grows to the bound, then the server lets it go.
	std::string chunk = Pattern(1 << 20);
	size_t mostQueued = 0;
	int sent = 0;
	while (sent < 200 && !rec.WasClosed(slowId)) {
		server.SendBinary(slowId, chunk);
		sent++;
		mostQueued = std::max(mostQueued, server.QueuedBytes(slowId));
		// the caller is never held up by the socket
		if (sent % 8 == 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(rec.WaitClosed(closed + 1));
	CHECK(rec.WasClosed(slowId));
	CHECK(sent >= 16 && sent < 200);
	// the bound is on what waits when a message arrives, so the last message taken comes on top of it
	CHECK(mostQueued > (8u << 20) && mostQueued <= (16u << 20) + chunk.size() + 10);
	CHECK_EQ(server.QueuedBytes(slowId), (size_t)0);
	printf("slow client: dropped after %d MiB, at most %zu bytes queued\n", sent, mostQueued);
	CHECK(slow.WaitClosed(10000));

	// everybody else is unaffected
	CHECK(server.HasClients());
	REQUIRE(good.SendFrame(1, "echo:unaffected"));
	CHECK_EQ(Text(good), std::string("unaffected"));
	CHECK_EQ(TestClient::Get(port, "/").status, 200);
	(void)goodId;
	good.Close();
	REQUIRE(rec.WaitClosed(closed + 2));
}

// Sends from several threads race with the network thread picking a wake-up up. Whatever the interleaving,
// the wake-up has to stay armed: afterwards one send still interrupts the idle thread, and so does Stop.
static void TestWakeUps() {
	Recorder rec;
	Server server;
	rec.server = &server;
	Options options;
	options.port = 0;
	REQUIRE(server.Start(options, &rec));
	TestClient ws;
	uint32_t id = Open(ws, rec, server.Port());

	// The short sleeps let the network thread get back to its wait, so nearly every send is a wake-up of
	// its own and many of them arrive while the thread handles the one before.
	const int kThreads = 8, kMessages = 3000;
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; t++) {
		threads.emplace_back([&server, id, t] {
			uint32_t seed = (uint32_t)t * 7919u + 99;
			for (int i = 0; i < kMessages; i++) {
				server.SendText(id, "x");
				seed = seed * 1664525u + 1013904223u;
				std::this_thread::sleep_for(std::chrono::microseconds(1 + (seed >> 16) % 400));
			}
		});
	}
	for (int i = 0; i < kThreads * kMessages; i++)
		REQUIRE(Text(ws) == "x");
	for (auto& thread : threads)
		thread.join();

	// A wake-up that got lost shows as a delivery at the thread's next periodic look (250 ms) instead.
	std::vector<long long> delays;
	for (int i = 0; i < 9; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		auto start = std::chrono::steady_clock::now();
		server.SendText(id, "probe");
		CHECK_EQ(Text(ws), std::string("probe"));
		delays.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
	}
	std::sort(delays.begin(), delays.end());
	printf("send to delivery after %d racing sends: median %lld us, slowest %lld us\n", kThreads * kMessages, delays[delays.size() / 2], delays.back());
	CHECK(delays[delays.size() / 2] < 100000);

	// Stop while the thread waits, most of a period before its next periodic look: only the wake-up ends
	// that wait in time.
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	auto start = std::chrono::steady_clock::now();
	server.Stop();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	printf("Stop after the racing sends: %lld ms\n", (long long)elapsed);
	CHECK(elapsed < 150);
	CHECK_EQ(rec.Closed(), (size_t)1);
}

// One thread feeds a client as fast as that client reads. The network thread still has to look after
// everybody else: a frame from another client reaches the handler, a plain request is answered, and Stop
// returns while the stream is running.
static void TestBusyClient() {
	Recorder rec;
	Server server;
	rec.server = &server;
	Options options;
	options.port = 0;
	REQUIRE(server.Start(options, &rec));
	int port = server.Port();
	TestClient fed, other;
	uint32_t fedId = Open(fed, rec, port);
	Open(other, rec, port);

	std::atomic<bool> quit{false};
	std::atomic<long long> received{0};
	std::thread reader([&] {
		bool closed = false;
		while (!quit && !closed) {
			if (fed.Fill(50, &closed))
				received += (long long)fed.buffer.size();
			fed.buffer.clear();
		}
	});
	// The producer holds back the way the interface offers, so the client stays far below its bound.
	std::thread producer([&] {
		std::string text(200, 'p');
		while (!quit) {
			if (server.QueuedBytes(fedId) > (4u << 20))
				std::this_thread::sleep_for(std::chrono::microseconds(100));
			else
				server.SendText(fedId, text);
		}
	});
	for (int i = 0; i < 1000 && received < (4 << 20); i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	CHECK(received >= (4 << 20));

	long long slowestEcho = 0, slowestPage = 0;
	for (int i = 0; i < 4; i++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		auto start = std::chrono::steady_clock::now();
		REQUIRE(other.SendFrame(1, "echo:probe"));
		CHECK_EQ(Text(other, 2000), std::string("probe"));
		slowestEcho = std::max<long long>(slowestEcho, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
		start = std::chrono::steady_clock::now();
		CHECK_EQ(TestClient::Request(port, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n", 2000).status, 200);
		slowestPage = std::max<long long>(slowestPage, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
	}
	printf("next to a client that is streamed to: echo within %lld ms, page within %lld ms\n", slowestEcho, slowestPage);
	CHECK(slowestEcho < 500);
	CHECK(slowestPage < 500);

	// Stop does not wait for the stream to end. Should it, the watchdog ends the stream and the test goes on.
	std::atomic<bool> stopped{false};
	std::thread watchdog([&] {
		for (int i = 0; i < 300 && !stopped; i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		quit = true;
	});
	auto start = std::chrono::steady_clock::now();
	server.Stop();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	stopped = true;
	watchdog.join();
	producer.join();
	reader.join();
	printf("Stop during the stream: %lld ms, %lld MiB streamed\n", (long long)elapsed, received.load() >> 20);
	CHECK(elapsed < 1000);
	CHECK_EQ(rec.Closed(), (size_t)2);
}

static void TestStop() {
	Recorder rec;
	Options options;
	options.port = 0;
	TestClient a, b, c;
	int port;
	{
		Server server;
		rec.server = &server;
		CHECK_EQ(server.Port(), 0);
		CHECK(!server.HasClients());
		server.Stop();		// not started: nothing to do
		REQUIRE(server.Start(options, &rec));
		port = server.Port();
		uint32_t ida = Open(a, rec, port);
		uint32_t idb = Open(b, rec, port);
		uint32_t idc = Open(c, rec, port);
		// b is in the middle of a fragmented message; one more connection only got as far as the request line
		REQUIRE(b.SendFrame(1, "echo:half", false));
		TestClient pending;
		REQUIRE(pending.Connect(port));
		REQUIRE(pending.Send("GET / HTTP/1.1\r\n"));

		auto start = std::chrono::steady_clock::now();
		server.Stop();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		CHECK(elapsed < 2000);
		// every OnClose has been delivered by the time Stop returns
		CHECK_EQ(rec.Closed(), (size_t)3);
		CHECK(rec.WasClosed(ida) && rec.WasClosed(idb) && rec.WasClosed(idc));
		CHECK(!server.HasClients());
		CHECK_EQ(server.Port(), 0);
		CHECK(ExpectClose(a, 1001));
		CHECK(ExpectClose(b, 1001));
		CHECK(ExpectClose(c, 1001));
		CHECK(pending.WaitClosed());

		// a stopped server takes every call
		server.SendText(ida, "x");
		server.SendBinary(ida, "x");
		server.BroadcastText("x");
		CHECK_EQ(server.QueuedBytes(ida), (size_t)0);
		server.Stop();
		TestClient nobody;
		CHECK(!nobody.Connect(port));

		// and can be started again; ids go on
		REQUIRE(server.Start(options, &rec));
		port = server.Port();
		CHECK_EQ(TestClient::Get(port, "/").status, 200);
		uint32_t idd = Open(a, rec, port);
		CHECK(idd > idc);

		// Stop from inside a handler ends the loop; the Stop outside completes it
		REQUIRE(a.SendFrame(1, "stop"));
		REQUIRE(rec.WaitClosed(4));
		CHECK(rec.WasClosed(idd));
		server.Stop();
		CHECK_EQ(server.Port(), 0);
		a.WaitClosed();

		REQUIRE(server.Start(options, &rec));
		port = server.Port();
		Open(a, rec, port);
		Open(b, rec, port);
		// leaving the scope with clients connected stops the server
	}
	CHECK_EQ(rec.Closed(), (size_t)6);
	CHECK(a.WaitClosed());
	CHECK(b.WaitClosed());
}

static void TestStartFailures(int usedPort) {
	Recorder rec;
	Server server;
	Options options;
	options.port = usedPort;
	printf("(three start failures are reported on stderr next)\n");
	fflush(stdout);
	CHECK(!server.Start(options, &rec));		// the port is taken
	options.port = 0;
	options.bind = "192.0.2.1";					// TEST-NET-1: a valid address, never a local one
	CHECK(!server.Start(options, &rec));
	options.bind = "";
	CHECK(!server.Start(options, &rec));
	options.bind = "127.0.0.1";
	CHECK(!server.Start(options, nullptr));
	options.port = 70000;
	CHECK(!server.Start(options, &rec));
	options.port = -1;
	CHECK(!server.Start(options, &rec));
	CHECK_EQ(server.Port(), 0);

	// the other address family
	options.port = 0;
	options.bind = "::1";
	if (server.Start(options, &rec)) {
		CHECK(server.Port() > 0);
		server.Stop();
	} else {
		printf("no IPv6 loopback here, case skipped\n");
	}
	// localhost by name
	options.bind = "localhost";
	REQUIRE(server.Start(options, &rec));
	CHECK(server.Port() > 0);
	server.Stop();
}

int main() {
	SetStaticProvider(&Provider);

	Recorder rec;
	Server server;
	rec.server = &server;
	Options options;
	options.port = 0;
	options.maxMessage = kMaxMessage;
	options.allowedHosts.push_back("DevBox.LAN");
	REQUIRE(server.Start(options, &rec));
	int port = server.Port();
	REQUIRE(port > 0);
	CHECK(!server.Start(options, &rec));	// already running
	CHECK_EQ(server.Port(), port);
	auto started = std::chrono::steady_clock::now();

	// An idle connection that never completes its request; looked at again at the end.
	TestClient idle;
	REQUIRE(idle.Connect(port));
	REQUIRE(idle.Send("GET / HTTP/1.1\r\nHost: loc"));

	TestHttp(port);
	TestWebRoot();
	TestWebSocket(server, rec, port);
	TestOrigin(rec, port);
	TestProtocolErrors(rec, port);
	TestBroadcastAndCapacity(server, rec, port, options.maxClients);
	TestThreads(server, rec, port);
	TestSlowClient(server, rec, port);
	TestStartFailures(port);

	// the idle connection is closed after 10 s without a request
	auto idleFor = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	CHECK(idle.WaitClosed((int)std::max<long long>(12000 - idleFor, 2000)));

	CHECK(rec.oneThread);
	CHECK(rec.thread != std::this_thread::get_id());
	CHECK_EQ(rec.Opened(), rec.Closed());
	server.Stop();
	CHECK_EQ(rec.Opened(), rec.Closed());

	TestStop();
	TestWakeUps();
	TestBusyClient();
	return Finish("test_e2e");
}
