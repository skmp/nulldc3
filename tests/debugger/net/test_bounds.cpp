// The two bounds of what a WebSocket client may leave waiting (dbg_net.h, the send functions): 16 MiB of text
// and 64 MiB of binary messages, counted apart, with every message going out in the order it was queued.
// dbg::net::Server end to end over loopback, through its public interface only, with the test's own client.
//
//   - a binary message of the largest size the debugger's protocol gives (4096 frames of an SGC capture: 32 MiB
//     and the 16 bytes of its header) followed by ticks, read slowly: the client stays, and gets every message
//     in order, the binary one whole. Under one bound for both the first tick behind the binary message would
//     find more than 16 MiB waiting and cut the client off
//   - a client that does not read is cut off by more than 16 MiB of text, whatever binary messages wait in
//     front of it, and by more than 64 MiB of binary messages, whatever text waits between them

#include "client.h"

#include "debugger/dbg_net.h"

#include "check.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#ifdef _WIN32
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#endif
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

using namespace dbg::net;

typedef std::chrono::steady_clock Clock;

static const size_t kMiB = 1u << 20;
static const size_t kTextBound = 16 * kMiB;
static const size_t kBinaryBound = 64 * kMiB;
static const size_t kLargestBinary = 4096 * 8192 + 16;		// state.getSgcFrameData of 4096 frames
static const size_t kTick = 160 * 1024;						// a paused tick of a live session

static std::string Pattern(size_t n, unsigned seed) {
	std::string s(n, '\0');
	for (size_t i = 0; i < n; i++)
		s[i] = (char)(i * 7 + i / 255 + seed);
	return s;
}

static long long MsSince(Clock::time_point t0) {
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

// What the server reports; the client's messages are not answered
struct Recorder : Handler {
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<uint32_t> opened;
	std::vector<uint32_t> closed;

	void OnOpen(uint32_t client) override {
		std::lock_guard<std::mutex> lock(mutex);
		opened.push_back(client);
		changed.notify_all();
	}

	void OnText(uint32_t, const std::string&) override {}

	void OnClose(uint32_t client) override {
		std::lock_guard<std::mutex> lock(mutex);
		closed.push_back(client);
		changed.notify_all();
	}

	bool WasClosed(uint32_t client) {
		std::lock_guard<std::mutex> lock(mutex);
		return std::count(closed.begin(), closed.end(), client) == 1;
	}

	bool WaitClosed(uint32_t client, int ms) {
		std::unique_lock<std::mutex> lock(mutex);
		return changed.wait_for(lock, std::chrono::milliseconds(ms), [&] { return std::count(closed.begin(), closed.end(), client) == 1; });
	}

	uint32_t WaitOpened(size_t count) {
		std::unique_lock<std::mutex> lock(mutex);
		if (!changed.wait_for(lock, std::chrono::milliseconds(5000), [&] { return opened.size() >= count; }))
			return 0;
		return opened.back();
	}
};

// A client with a small receive buffer: what the server can hand to the socket while this side does not read is
// small against the bounds
static uint32_t Open(TestClient& ws, Recorder& rec, int port) {
	size_t before;
	{
		std::lock_guard<std::mutex> lock(rec.mutex);
		before = rec.opened.size();
	}
	REQUIRE(ws.Connect(port, 64 * 1024));
	std::string request = "GET /ws HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n"
		"Upgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
	std::string head;
	REQUIRE(ws.Send(request) && ws.ReadHead(&head) && head.compare(0, 12, "HTTP/1.1 101") == 0);
	uint32_t id = rec.WaitOpened(before + 1);
	REQUIRE(id != 0);
	return id;
}

// One frame, read slowly: at most 64 KiB at a time, with a pause behind each
static bool ReadSlowly(TestClient& ws, WsFrame* frame, int pauseUs, int timeoutMs) {
	auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
	auto need = [&](size_t n) {
		while (ws.buffer.size() < n) {
			int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
			if (left <= 0)
				return false;
			bool closed = false;
			if (!ws.Fill(std::min(left, 1000), &closed) && closed)
				return false;
			std::this_thread::sleep_for(std::chrono::microseconds(pauseUs));
		}
		return true;
	};
	if (!need(2))
		return false;
	frame->fin = (ws.buffer[0] & 0x80) != 0;
	frame->opcode = ws.buffer[0] & 0x0f;
	frame->masked = (ws.buffer[1] & 0x80) != 0;
	uint64_t length = ws.buffer[1] & 0x7f;
	size_t at = 2;
	if (length == 126) {
		if (!need(4))
			return false;
		length = ((uint64_t)(unsigned char)ws.buffer[2] << 8) | (unsigned char)ws.buffer[3];
		at = 4;
	} else if (length == 127) {
		if (!need(10))
			return false;
		length = 0;
		for (int i = 0; i < 8; i++)
			length = (length << 8) | (unsigned char)ws.buffer[2 + i];
		at = 10;
	}
	if (frame->masked || !need(at + (size_t)length))
		return false;
	frame->payload.assign(ws.buffer, at, (size_t)length);
	ws.buffer.erase(0, at + (size_t)length);
	return true;
}

// The largest binary message, and ticks behind it while the client reads it slowly: the client stays
static void TestLargestBinaryThenTicks(Server& server, Recorder& rec, int port) {
	TestClient ws;
	uint32_t id = Open(ws, rec, port);

	const std::string binary = Pattern(kLargestBinary, 3);
	server.SendBinary(id, binary);

	// The ticks come while the binary message waits nearly whole: a tick at once, then one every 40 ms until the
	// client has read the binary message
	std::atomic<bool> read{false};
	std::atomic<int> ticks{0};
	size_t mostQueued = 0;
	std::mutex queuedLock;

	server.BroadcastText(Pattern(kTick, 0) + "#0");
	ticks = 1;
	{
		std::lock_guard<std::mutex> lock(queuedLock);
		mostQueued = server.QueuedBytes(id);
	}
	CHECK(mostQueued > kTextBound);

	std::thread ticker([&] {
		while (!read && ticks < 200) {
			std::this_thread::sleep_for(std::chrono::milliseconds(40));
			int n = ticks.load();
			server.BroadcastText(Pattern(kTick, (unsigned)n) + "#" + std::to_string(n));
			ticks = n + 1;
			std::lock_guard<std::mutex> lock(queuedLock);
			mostQueued = std::max(mostQueued, server.QueuedBytes(id));
		}
	});

	auto t0 = Clock::now();
	WsFrame frame;
	bool got = ReadSlowly(ws, &frame, 1000, 60000);
	read = true;
	ticker.join();
	long long readMs = MsSince(t0);

	REQUIRE(got);
	CHECK_EQ(frame.opcode, 2);
	CHECK(frame.fin);
	CHECK(frame.payload.size() == binary.size() && frame.payload == binary);

	// every tick, in order, behind it
	int seen = 0;
	bool inOrder = true;
	for (int i = 0; i < ticks.load(); i++) {
		WsFrame tick;
		if (!ws.ReadFrame(&tick, 10000))
			break;
		std::string want = Pattern(kTick, (unsigned)i) + "#" + std::to_string(i);
		inOrder = inOrder && tick.opcode == 1 && tick.payload == want;
		seen++;
	}
	CHECK_EQ(seen, ticks.load());
	CHECK(inOrder);
	CHECK(!rec.WasClosed(id));
	CHECK(ticks.load() >= 2);
	printf("largest binary message: %zu bytes read in %lld ms with %d ticks behind it, at most %zu bytes queued, the client stays\n", binary.size(), readMs,
		ticks.load(), mostQueued);

	ws.Close();
	REQUIRE(rec.WaitClosed(id, 5000));
}

// A client that does not read: more than 16 MiB of text cuts it off, with a binary message of 32 MiB waiting in
// front of the text, which does not count for it
static void TestTextBound(Server& server, Recorder& rec, int port) {
	TestClient ws;
	uint32_t id = Open(ws, rec, port);

	server.SendBinary(id, Pattern(kLargestBinary, 5));

	const std::string text = Pattern(kMiB, 1);
	int sent = 0;
	size_t mostQueued = 0;
	while (sent < 100 && !rec.WasClosed(id)) {
		server.SendText(id, text);
		sent++;
		mostQueued = std::max(mostQueued, server.QueuedBytes(id));
		if (sent % 8 == 0)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	REQUIRE(rec.WaitClosed(id, 10000));

	// The bound is on the text that waits when a text message arrives: with their frame headers 16 messages are a
	// little more than 16 MiB, and the 17th is not taken; the binary message in front, which the socket takes a
	// part of, changes nothing of that. The sends behind the refused one, until the network thread has dropped the
	// client, are ignored
	CHECK(sent >= 17 && sent <= 40);
	CHECK(mostQueued > kTextBound + 16 * kMiB && mostQueued <= kLargestBinary + kTextBound + kMiB);
	CHECK_EQ(server.QueuedBytes(id), (size_t)0);
	CHECK(ws.WaitClosed(10000));
	printf("text bound: dropped at text message %d (1 MiB each) behind a binary message of %zu bytes, at most %zu bytes queued\n", sent, kLargestBinary, mostQueued);
}

// A client that does not read: more than 64 MiB of binary messages cut it off, with text between them, which does
// not count for them
static void TestBinaryBound(Server& server, Recorder& rec, int port) {
	TestClient ws;
	uint32_t id = Open(ws, rec, port);

	const std::string binary = Pattern(kMiB, 2);
	const std::string text = Pattern(kMiB, 4);
	int sent = 0, texts = 0;
	size_t mostQueued = 0;
	while (sent < 200 && !rec.WasClosed(id)) {
		server.SendBinary(id, binary);
		sent++;
		// a text message behind every eighth: 8 MiB of text in all, half its bound
		if (sent % 8 == 0) {
			server.SendText(id, text);
			texts++;
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		mostQueued = std::max(mostQueued, server.QueuedBytes(id));
	}
	REQUIRE(rec.WaitClosed(id, 10000));

	// With their frame headers 64 binary messages are a little more than 64 MiB, and the next is not taken, or
	// a few more where the socket has taken the first of them (its buffers are a few MiB at most); the text
	// between them changes nothing of that
	CHECK(sent >= 65 && sent <= 90);
	CHECK(texts >= 8 && texts <= 11);
	CHECK(mostQueued > kBinaryBound && mostQueued <= kBinaryBound + (size_t)texts * kMiB + 2 * kMiB);
	CHECK_EQ(server.QueuedBytes(id), (size_t)0);
	CHECK(ws.WaitClosed(10000));
	printf("binary bound: dropped at binary message %d (1 MiB each, %d text messages of 1 MiB between them), at most %zu bytes queued\n", sent, texts,
		mostQueued);
}

int main() {
#ifdef _WIN32
	// The slow reader and the ticker sleep 1 ms and 40 ms: with Windows' default timer of 15.6 ms the client read
	// fifteen times slower than it means to, and the ticks overran the bound of text behind the binary message
	timeBeginPeriod(1);
#endif
	Recorder rec;
	Server server;
	Options options;
	options.port = 0;
	REQUIRE(server.Start(options, &rec));
	int port = server.Port();
	REQUIRE(port > 0);

	TestLargestBinaryThenTicks(server, rec, port);
	TestTextBound(server, rec, port);
	TestBinaryBound(server, rec, port);

	server.Stop();
	return Finish("test_bounds");
}
