// WebSocket handshake and framing of dbg_net.cpp against the vectors of RFC 6455, and the output queue of a
// connection. The translation unit under test is included, not linked: the parsers are internal to it.

#include "../../../libswirl/debugger/dbg_net.cpp"

#include "check.h"
#include "client.h"

using namespace dbg::net;

static const size_t kLimit = 1000;	// maxMessage for most cases

struct Decoded {
	std::vector<WsEvent> events;	// kNone (fragments) left out
	int failCode = 0;				// 0: no failure
	size_t leftover = 0;			// bytes of an incomplete frame at the end
};

// Feeds `wire` to a decoder in pieces of `step` bytes, buffering the way the server does.
static Decoded Decode(const std::string& wire, size_t limit = kLimit, size_t step = 0) {
	Decoded result;
	WsDecoder decoder(limit);
	std::string in;
	size_t fed = 0;
	if (step == 0)
		step = wire.size() ? wire.size() : 1;
	while (fed < wire.size() || fed == 0) {
		size_t n = std::min(step, wire.size() - fed);
		in.append(wire, fed, n);
		fed += n;
		for (;;) {
			WsEvent ev;
			size_t used = 0;
			int code = 0;
			WsResult r = decoder.Next(in.data(), in.size(), &used, &ev, &code);
			if (r == kWsNeedMore)
				break;
			if (r == kWsFail) {
				result.failCode = code;
				return result;
			}
			in.erase(0, used);
			if (ev.kind != WsEvent::kNone)
				result.events.push_back(ev);
		}
		if (wire.empty())
			break;
	}
	result.leftover = in.size();
	return result;
}

static std::string Bytes(std::initializer_list<int> list) {
	std::string s;
	for (int b : list)
		s += (char)b;
	return s;
}

static std::string Pattern(size_t n) {
	std::string s(n, '\0');
	for (size_t i = 0; i < n; i++)
		s[i] = (char)('a' + i % 23);
	return s;
}

static void TestHandshake() {
	// RFC 6455 section 1.3
	CHECK_EQ(WsAccept("dGhlIHNhbXBsZSBub25jZQ=="), std::string("s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

	// RFC 4648 section 10
	const char* plain[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
	const char* coded[] = { "", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy" };
	for (int i = 0; i < 7; i++)
		CHECK_EQ(Base64((const unsigned char*)plain[i], strlen(plain[i])), std::string(coded[i]));

	// The opening handshake of RFC 6455 section 1.2, with a Host this server answers to.
	std::string request =
		"GET /ws HTTP/1.1\r\n"
		"Host: localhost:55543\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
		"Origin: http://localhost:55543\r\n"
		"Sec-WebSocket-Protocol: chat, superchat\r\n"
		"Sec-WebSocket-Extensions: permessage-deflate\r\n"
		"Sec-WebSocket-Version: 13\r\n"
		"\r\n";
	HttpRequest req;
	size_t used = 0;
	REQUIRE(ParseHttpRequest(request.data(), request.size(), &req, &used) == 1);
	CHECK_EQ(used, request.size());
	std::vector<std::string> hosts = { "127.0.0.1", "localhost", "[::1]" };
	Reply reply;
	Respond(req, Options(), hosts, false, &reply);
	CHECK_EQ(reply.status, 101);
	CHECK(reply.upgrade);
	CHECK_EQ(reply.head, std::string(
		"HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
		"\r\n"));
	// Neither an extension nor a subprotocol is selected.
	CHECK(reply.head.find("Sec-WebSocket-Protocol") == std::string::npos);
	CHECK(reply.head.find("Sec-WebSocket-Extensions") == std::string::npos);
	CHECK(reply.body.empty());

	CHECK(ValidWsKey("dGhlIHNhbXBsZSBub25jZQ=="));
	CHECK(!ValidWsKey(""));
	CHECK(!ValidWsKey("dGhlIHNhbXBsZSBub25jZQ="));
	CHECK(!ValidWsKey("dGhlIHNhbXBsZSBub25jZQ==="));
	CHECK(!ValidWsKey("dGhlIHNhbXBsZSBub25jZ==="));
	CHECK(!ValidWsKey("dGhlIHNhbXBsZSBub25j!Q=="));
}

static void TestEncode() {
	// RFC 6455 section 5.7: single-frame unmasked text message
	CHECK_EQ(FrameHeader(kOpText, 5) + "Hello", Bytes({ 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f }));
	// 256 bytes binary message in a single unmasked frame
	CHECK_EQ(FrameHeader(kOpBinary, 256), Bytes({ 0x82, 0x7e, 0x01, 0x00 }));
	// 64 KiB binary message in a single unmasked frame
	CHECK_EQ(FrameHeader(kOpBinary, 65536), Bytes({ 0x82, 0x7f, 0, 0, 0, 0, 0, 1, 0, 0 }));

	CHECK_EQ(FrameHeader(kOpText, 0), Bytes({ 0x81, 0x00 }));
	CHECK_EQ(FrameHeader(kOpText, 125), Bytes({ 0x81, 0x7d }));
	CHECK_EQ(FrameHeader(kOpText, 126), Bytes({ 0x81, 0x7e, 0x00, 0x7e }));
	CHECK_EQ(FrameHeader(kOpText, 65535), Bytes({ 0x81, 0x7e, 0xff, 0xff }));
	if (sizeof(size_t) > 4)
		CHECK_EQ(FrameHeader(kOpBinary, (size_t)0x100000005ull), Bytes({ 0x82, 0x7f, 0, 0, 0, 1, 0, 0, 0, 5 }));
	CHECK_EQ(FrameHeader(kOpPong, 5), Bytes({ 0x8a, 0x05 }));

	CHECK_EQ(CloseFrame(1000), Bytes({ 0x88, 0x02, 0x03, 0xe8 }));
	CHECK_EQ(CloseFrame(1009), Bytes({ 0x88, 0x02, 0x03, 0xf1 }));
	CHECK_EQ(CloseFrame(0), Bytes({ 0x88, 0x00 }));
}

static void TestDecodeRfcExamples() {
	// single-frame masked text message
	Decoded d = Decode(Bytes({ 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 }));
	CHECK_EQ(d.failCode, 0);
	REQUIRE(d.events.size() == 1);
	CHECK_EQ(d.events[0].kind, WsEvent::kText);
	CHECK_EQ(d.events[0].payload, std::string("Hello"));

	// the same message unmasked: a client must mask
	d = Decode(Bytes({ 0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f }));
	CHECK_EQ(d.failCode, 1002);
	CHECK(d.events.empty());

	// masked Pong ("unmasked Ping request and masked Ping response")
	d = Decode(Bytes({ 0x8a, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 }));
	REQUIRE(d.events.size() == 1);
	CHECK_EQ(d.events[0].kind, WsEvent::kPong);
	CHECK_EQ(d.events[0].payload, std::string("Hello"));

	// fragmented text message "Hel" + "lo", masked as a client sends it
	d = Decode(TestClient::Frame(kOpText, "Hel", false) + TestClient::Frame(kOpContinuation, "lo", true));
	REQUIRE(d.events.size() == 1);
	CHECK_EQ(d.events[0].kind, WsEvent::kText);
	CHECK_EQ(d.events[0].payload, std::string("Hello"));
}

static void TestDecodeLengths() {
	// Every length class, each also fed one byte at a time and in odd pieces.
	const size_t sizes[] = { 0, 1, 2, 125, 126, 127, 128, 65535, 65536, 65537, 70000 };
	for (size_t size : sizes) {
		std::string payload = Pattern(size);
		for (int opcode : { (int)kOpText, (int)kOpBinary }) {
			std::string wire = TestClient::Frame(opcode, payload);
			for (size_t step : { (size_t)0, (size_t)1, (size_t)7, (size_t)4096 }) {
				if (step == 1 && size > 1000)
					continue;
				Decoded d = Decode(wire, 100000, step);
				CHECK_EQ(d.failCode, 0);
				CHECK_EQ(d.leftover, (size_t)0);
				REQUIRE(d.events.size() == 1);
				CHECK_EQ(d.events[0].kind, opcode == kOpText ? WsEvent::kText : WsEvent::kBinary);
				CHECK(d.events[0].payload == payload);
			}
		}
	}

	// The length must use the shortest form.
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(5), true, true, 2)).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(125), true, true, 2)).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(5), true, true, 8)).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(65535), true, true, 8), 100000).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(126), true, true, 2)).failCode, 0);
	// 64-bit length with the top bit set; and one far beyond any limit: refused from the header alone.
	CHECK_EQ(Decode(Bytes({ 0x82, 0xff, 0x80, 0, 0, 0, 0, 0, 0, 0 })).failCode, 1002);
	CHECK_EQ(Decode(Bytes({ 0x82, 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff })).failCode, 1009);

	// An incomplete frame is not an error, at any cut.
	std::string wire = TestClient::Frame(kOpText, Pattern(300));
	for (size_t cut = 0; cut < wire.size(); cut++) {
		Decoded d = Decode(wire.substr(0, cut));
		CHECK_EQ(d.failCode, 0);
		CHECK(d.events.empty());
		CHECK_EQ(d.leftover, cut);
	}
}

static void TestDecodeInvalid() {
	// reserved bits: no extension is negotiated
	for (int reserved : { 1, 2, 4, 7 })
		CHECK_EQ(Decode(TestClient::Frame(kOpText, "x", true, true, 0, reserved)).failCode, 1002);
	// reserved opcodes
	for (int opcode : { 3, 4, 5, 6, 7, 11, 12, 13, 14, 15 })
		CHECK_EQ(Decode(TestClient::Frame(opcode, "x")).failCode, 1002);
	// unmasked frames of every kind
	for (int opcode : { (int)kOpText, (int)kOpBinary, (int)kOpPing, (int)kOpPong, (int)kOpClose })
		CHECK_EQ(Decode(TestClient::Frame(opcode, "", true, false)).failCode, 1002);

	// control frames: never fragmented, at most 125 bytes
	CHECK_EQ(Decode(TestClient::Frame(kOpPing, "x", false)).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpClose, "", false)).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpPing, Pattern(125))).failCode, 0);
	CHECK_EQ(Decode(TestClient::Frame(kOpPing, Pattern(126))).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpPong, Pattern(200))).failCode, 1002);

	// fragmentation order
	CHECK_EQ(Decode(TestClient::Frame(kOpContinuation, "x")).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpContinuation, "x", false)).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpText, "a", false) + TestClient::Frame(kOpText, "b")).failCode, 1002);
	CHECK_EQ(Decode(TestClient::Frame(kOpText, "a", false) + TestClient::Frame(kOpBinary, "b", false)).failCode, 1002);
	// after a complete message a continuation is out of place again
	Decoded d = Decode(TestClient::Frame(kOpText, "a", false) + TestClient::Frame(kOpContinuation, "b") + TestClient::Frame(kOpContinuation, "c"));
	CHECK_EQ(d.failCode, 1002);

	// text must be UTF-8, checked on the whole message
	CHECK_EQ(Decode(TestClient::Frame(kOpText, "\xff")).failCode, 1007);
	CHECK_EQ(Decode(TestClient::Frame(kOpText, "\xc3\x28")).failCode, 1007);
	CHECK_EQ(Decode(TestClient::Frame(kOpText, "\xe2\x82", false) + TestClient::Frame(kOpContinuation, "\xac")).failCode, 0);
	CHECK_EQ(Decode(TestClient::Frame(kOpText, "\xe2\x82", false) + TestClient::Frame(kOpContinuation, "a")).failCode, 1007);
	CHECK_EQ(Decode(TestClient::Frame(kOpText, "\xe2\x82")).failCode, 1007);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, "\xff\xfe")).failCode, 0);
}

static void TestDecodeFragments() {
	// control frames between the fragments of a message are delivered where they arrive
	std::string wire =
		TestClient::Frame(kOpText, "one ", false) +
		TestClient::Frame(kOpPing, "p1") +
		TestClient::Frame(kOpContinuation, "two ", false) +
		TestClient::Frame(kOpPong, "") +
		TestClient::Frame(kOpContinuation, "", false) +
		TestClient::Frame(kOpPing, "p2") +
		TestClient::Frame(kOpContinuation, "three") +
		TestClient::Frame(kOpBinary, Bytes({ 0, 1, 2, 255 }));
	for (size_t step : { (size_t)0, (size_t)1, (size_t)3 }) {
		Decoded d = Decode(wire, kLimit, step);
		CHECK_EQ(d.failCode, 0);
		REQUIRE(d.events.size() == 5);
		CHECK_EQ(d.events[0].kind, WsEvent::kPing);
		CHECK_EQ(d.events[0].payload, std::string("p1"));
		CHECK_EQ(d.events[1].kind, WsEvent::kPong);
		CHECK_EQ(d.events[2].kind, WsEvent::kPing);
		CHECK_EQ(d.events[2].payload, std::string("p2"));
		CHECK_EQ(d.events[3].kind, WsEvent::kText);
		CHECK_EQ(d.events[3].payload, std::string("one two three"));
		CHECK_EQ(d.events[4].kind, WsEvent::kBinary);
		CHECK_EQ(d.events[4].payload, Bytes({ 0, 1, 2, 255 }));
	}

	// a fragmented binary message keeps its type
	Decoded d = Decode(TestClient::Frame(kOpBinary, "ab", false) + TestClient::Frame(kOpContinuation, "cd"));
	REQUIRE(d.events.size() == 1);
	CHECK_EQ(d.events[0].kind, WsEvent::kBinary);
	CHECK_EQ(d.events[0].payload, std::string("abcd"));
}

static void TestDecodeOversize() {
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(kLimit))).failCode, 0);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(kLimit + 1))).failCode, 1009);
	// refused as soon as the header is in, without waiting for the payload
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(kLimit + 1)).substr(0, 4)).failCode, 1009);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, Pattern(70000)).substr(0, 10)).failCode, 1009);

	// fragments that add up
	std::string half = Pattern(kLimit / 2);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, half, false) + TestClient::Frame(kOpContinuation, half)).failCode, 0);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, half, false) + TestClient::Frame(kOpContinuation, half + "x")).failCode, 1009);
	CHECK_EQ(Decode(TestClient::Frame(kOpBinary, half, false) + TestClient::Frame(kOpContinuation, half, false) + TestClient::Frame(kOpContinuation, "x")).failCode, 1009);

	// the limit is per message: the next one starts from zero, and control frames do not count
	Decoded d = Decode(TestClient::Frame(kOpBinary, Pattern(kLimit)) + TestClient::Frame(kOpPing, Pattern(125)) + TestClient::Frame(kOpBinary, Pattern(kLimit)));
	CHECK_EQ(d.failCode, 0);
	CHECK_EQ(d.events.size(), (size_t)3);

	// a limit below 126 still lets control frames through
	d = Decode(TestClient::Frame(kOpPing, Pattern(125)), 10);
	CHECK_EQ(d.failCode, 0);
	CHECK_EQ(d.events.size(), (size_t)1);
}

static void TestDecodeClose() {
	Decoded d = Decode(TestClient::Frame(kOpClose, ""));
	REQUIRE(d.events.size() == 1);
	CHECK_EQ(d.events[0].kind, WsEvent::kClose);
	CHECK_EQ(d.events[0].code, 1005);

	d = Decode(TestClient::Frame(kOpClose, Bytes({ 0x03, 0xe8 }) + "bye"));
	REQUIRE(d.events.size() == 1);
	CHECK_EQ(d.events[0].code, 1000);

	CHECK_EQ(Decode(TestClient::Frame(kOpClose, "x")).failCode, 1002);
	for (int code : { 0, 999, 1004, 1005, 1006, 1015, 1016, 2999, 5000, 65535 })
		CHECK_EQ(Decode(TestClient::Frame(kOpClose, Bytes({ code >> 8, code & 255 }))).failCode, 1002);
	for (int code : { 1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 3000, 4999 })
		CHECK_EQ(Decode(TestClient::Frame(kOpClose, Bytes({ code >> 8, code & 255 }))).failCode, 0);
	CHECK_EQ(Decode(TestClient::Frame(kOpClose, Bytes({ 0x03, 0xe8, 0xff }))).failCode, 1007);
}

static void TestUtf8() {
	const char* valid[] = { "", "plain", "\xc2\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\xef\xbf\xbf", "\xf4\x8f\xbf\xbf", "\xed\x9f\xbf", "\xee\x80\x80" };
	for (const char* s : valid)
		CHECK(ValidUtf8(s, strlen(s)));
	const char* invalid[] = {
		"\x80", "\xbf", "\xc0\xaf", "\xc1\xbf",			// stray continuation, overlong 2-byte forms
		"\xe0\x80\xaf", "\xe0\x9f\xbf",					// overlong 3-byte forms
		"\xf0\x80\x80\xaf", "\xf0\x8f\xbf\xbf",			// overlong 4-byte forms
		"\xed\xa0\x80", "\xed\xbf\xbf",					// surrogates
		"\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff",	// beyond U+10FFFF
		"\xc2", "\xe2\x82", "\xf0\x9f\x98",				// cut short
		"\xc2\x41", "\xe2\x28\xa1",						// continuation byte missing
	};
	for (const char* s : invalid)
		CHECK(!ValidUtf8(s, strlen(s)));
	CHECK(ValidUtf8("a\0b", 3));
}

// Memory a connection's queue holds, as far as the queue can tell.
static size_t Held(const Conn& c) {
	size_t bytes = 0;
	for (const std::string& chunk : c.queue)
		bytes += sizeof(std::string) + chunk.capacity();
	return bytes;
}

static std::string Joined(const Conn& c) {
	std::string all;
	for (const std::string& chunk : c.queue)
		all += chunk;
	return all;
}

static void TestQueue() {
	// One pong per ping for a client that never reads: the bound on bytes has to bound the memory as well.
	{
		Conn c(kNoSocket, kLimit);
		std::string pong = FrameHeader(kOpPong, 0);
		size_t pushed = 0;
		while (!c.overflow && pushed < 100000000) {
			Push(c, pong, true);
			pushed++;
		}
		CHECK(c.overflow);
		CHECK(c.queued >= kMaxQueuedBytes - pong.size() && c.queued <= kMaxQueuedBytes + pong.size());
		CHECK_EQ(Joined(c).size(), (size_t)c.queued);
		CHECK(c.queue.size() <= 2 * c.queued / kMergeBytes + 1);
		CHECK(Held(c) <= 2 * c.queued + (1u << 20));
		printf("%zu pongs: %zu bytes queued in %zu chunks that hold %zu bytes\n", pushed - 1, (size_t)c.queued, c.queue.size(), Held(c));

		// cut off: nothing more is taken
		size_t queued = c.queued;
		Push(c, pong, true);
		Push(c, pong, false);
		CHECK_EQ((size_t)c.queued, queued);
	}

	// Messages of mixed sizes: every byte in order, and no chunk ends inside a message.
	{
		Conn c(kNoSocket, kLimit);
		std::string all;
		std::vector<size_t> ends;
		uint32_t seed = 1;
		for (int i = 0; i < 3000; i++) {
			seed = seed * 1664525u + 1013904223u;
			size_t size = (seed >> 8) % 7 == 0 ? (seed >> 12) % 30000 : 1 + (seed >> 12) % 300;
			std::string message(size, (char)('a' + i % 26));
			Push(c, message, i % 2 == 0);
			all += message;
			ends.push_back(all.size());
		}
		CHECK(!c.overflow);
		CHECK_EQ((size_t)c.queued, all.size());
		CHECK(Joined(c) == all);
		size_t at = 0;
		bool whole = true, merged = true;
		for (size_t i = 0; i < c.queue.size(); i++) {
			at += c.queue[i].size();
			if (!std::binary_search(ends.begin(), ends.end(), at))
				whole = false;
			if (i > 0 && c.queue[i - 1].size() + c.queue[i].size() <= kMergeBytes)
				merged = false;
		}
		CHECK(whole);
		CHECK(merged);		// two neighbours that would have fitted one chunk
		CHECK(c.queue.size() <= 2 * c.queued / kMergeBytes + 1);
	}

	// A chunk is closed when the next message would take it past kMergeBytes; larger messages stand alone.
	{
		Conn c(kNoSocket, kLimit);
		Push(c, std::string(kMergeBytes - 1, 'a'), true);
		Push(c, "b", true);
		Push(c, "c", true);
		Push(c, std::string(kMergeBytes, 'd'), true);
		Push(c, std::string(3 * kMergeBytes, 'e'), true);
		Push(c, "", true);
		Push(c, "f", true);
		REQUIRE(c.queue.size() == 5);
		CHECK_EQ(c.queue[0], std::string(kMergeBytes - 1, 'a') + "b");
		CHECK_EQ(c.queue[1], std::string("c"));
		CHECK_EQ(c.queue[2].size(), kMergeBytes);
		CHECK_EQ(c.queue[3].size(), 3 * kMergeBytes);
		CHECK_EQ(c.queue[4], std::string("f"));
		CHECK_EQ((size_t)c.queued, 5 * kMergeBytes + 2);
	}

	// The bound is on what already waits. A message of any size is taken while no more than kMaxQueuedBytes
	// are ahead of it; the first message after that cuts the client off.
	{
		Conn c(kNoSocket, kLimit);
		Push(c, "reply", true);
		Push(c, std::string(kMaxQueuedBytes + 100, 'x'), true);
		CHECK(!c.overflow);
		CHECK_EQ((size_t)c.queued, kMaxQueuedBytes + 105);
		Push(c, "tick", true);
		CHECK(c.overflow);
		CHECK_EQ((size_t)c.queued, kMaxQueuedBytes + 105);
	}
	{
		Conn c(kNoSocket, kLimit);
		Push(c, std::string(kMaxQueuedBytes, 'x'), true);
		Push(c, "y", true);		// exactly the bound was waiting
		CHECK(!c.overflow);
		Push(c, "z", true);
		CHECK(c.overflow);
		CHECK_EQ((size_t)c.queued, kMaxQueuedBytes + 1);
	}

	// What a plain response queues is not bounded here: files are bounded where they are read.
	{
		Conn c(kNoSocket, kLimit);
		Push(c, "HTTP/1.1 200 OK\r\n\r\n", false);
		Push(c, std::string(kMaxQueuedBytes + 1, 'x'), false);
		CHECK(!c.overflow);
		CHECK_EQ(c.queue.size(), (size_t)2);
		CHECK_EQ((size_t)c.queued, kMaxQueuedBytes + 20);
	}
}

int main() {
	TestHandshake();
	TestEncode();
	TestDecodeRfcExamples();
	TestDecodeLengths();
	TestDecodeInvalid();
	TestDecodeFragments();
	TestDecodeOversize();
	TestDecodeClose();
	TestUtf8();
	TestQueue();
	return Finish("test_ws");
}
