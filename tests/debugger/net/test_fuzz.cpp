// Robustness of the HTTP and WebSocket parsers of dbg_net.cpp: random and mutated input must never crash,
// hang, ask for unbounded buffering or serve a byte from outside the web root. Valid WebSocket streams must
// come out exactly as they went in, however they are cut into reads.
//
//   test_fuzz [iterations per parser, default 100000] [seed]

#include "../../../libswirl/debugger/dbg_net.cpp"

#include "check.h"
#include "client.h"

#include <filesystem>
#include <fstream>

using namespace dbg::net;

struct Rng {
	uint64_t state;

	uint32_t Next() {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		return (uint32_t)((state * 0x2545f4914f6cdd1dull) >> 32);
	}
	size_t Below(size_t n) { return n ? Next() % n : 0; }
	bool Chance(unsigned percent) { return Next() % 100 < percent; }
};

static Rng gRng = { 0x9e3779b97f4a7c15ull };

static std::string RandomBytes(size_t n) {
	std::string s(n, '\0');
	for (char& c : s)
		c = (char)gRng.Next();
	return s;
}

static const char* const kTokens[] = {
	"\r\n", "\r\n\r\n", "\n", "\r", " ", ":", "/", "//", "..", "../", "/..", "%2e", "%2f", "%5c", "%00", "%", "\\", "?", "#",
	"Host: ", "Origin: ", "localhost", "127.0.0.1", "[::1]", "null", "evil.example", "http://", "HTTP/1.1", "HTTP/1.0", "GET",
	"Upgrade: websocket\r\n", "Connection: Upgrade\r\n", "Sec-WebSocket-Version: 13\r\n", "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n",
	"Content-Length: 1\r\n", "Transfer-Encoding: chunked\r\n", "Accept-Encoding: gzip\r\n", "/ws", "/index.html", "/assets/app.js",
	"secret.txt", "link-out.txt", "con", "nul", ".", "@", "[", "]", "\t", "=", ";q=0", ",",
};

static std::string Mutate(std::string s) {
	size_t rounds = 1 + gRng.Below(6);
	for (size_t r = 0; r < rounds; r++) {
		size_t at = gRng.Below(s.size() + 1);
		switch (gRng.Below(8)) {
		case 0:		// flip bits
			if (!s.empty())
				s[gRng.Below(s.size())] ^= (char)(1 << gRng.Below(8));
			break;
		case 1:		// random byte in
			s.insert(at, 1, (char)gRng.Next());
			break;
		case 2:		// a piece out
			if (!s.empty())
				s.erase(gRng.Below(s.size()), 1 + gRng.Below(8));
			break;
		case 3: {	// a piece again
			size_t from = gRng.Below(s.size() + 1);
			s.insert(at, s.substr(from, gRng.Below(32)));
			break;
		}
		case 4:		// something the grammar cares about
		case 5:
			s.insert(at, kTokens[gRng.Below(sizeof(kTokens) / sizeof(kTokens[0]))]);
			break;
		case 6:		// cut short
			s.resize(at);
			break;
		case 7:		// a run of one byte
			s.insert(at, 1 + gRng.Below(gRng.Chance(5) ? 9000 : 64), (char)gRng.Next());
			break;
		}
	}
	return s;
}

//
// HTTP
//

static const char kSecret[] = "TOP-SECRET-OUTSIDE-THE-ROOT";
static const size_t kLargestBody = 4096;	// no file of the fixtures is larger

static bool FuzzProvider(const std::string& path, bool acceptGzip, StaticFile* file) {
	if (path != "index.html" && path != "assets/app.js")
		return false;
	file->body = "provided " + path;
	file->gzip = acceptGzip;
	return true;
}

static void FuzzHttp(size_t iterations, const std::string& root) {
	const std::string seeds[] = {
		"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
		"GET /index.html HTTP/1.1\r\nHost: 127.0.0.1:55543\r\nAccept-Encoding: gzip, deflate\r\nUser-Agent: fuzz\r\n\r\n",
		"GET /assets/app.js?v=1 HTTP/1.1\r\nHost: [::1]:55543\r\nAccept: */*\r\n\r\n",
		"GET /ws HTTP/1.1\r\nHost: localhost:55543\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n",
		"GET /ws HTTP/1.1\r\nHost: localhost:55543\r\nUpgrade: websocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nOrigin: http://localhost:5173\r\n\r\n",
		"GET /../secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n",
		"GET /%2e%2e/secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n",
		"GET /link-out.txt HTTP/1.1\r\nHost: localhost\r\n\r\n",
		"GET /sub/file.txt HTTP/1.1\r\nHost: localhost\r\n\r\n",
		"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 3\r\n\r\nabc",
		"GET / HTTP/1.0\r\n\r\n",
		"OPTIONS * HTTP/1.1\r\nHost: evil.example\r\n\r\n",
	};
	const size_t seedCount = sizeof(seeds) / sizeof(seeds[0]);
	std::vector<std::string> hosts = { "127.0.0.1", "localhost", "[::1]" };
	Options plain;
	Options rooted;
	rooted.webRoot = root;

	size_t parsed = 0, incomplete = 0, refused = 0, served = 0, upgraded = 0;
	for (size_t i = 0; i < iterations; i++) {
		std::string input;
		switch (gRng.Below(10)) {
		case 0:
			input = RandomBytes(gRng.Below(gRng.Chance(10) ? 10000 : 300));
			break;
		case 1:
			input = seeds[gRng.Below(seedCount)];
			break;
		default:
			input = Mutate(seeds[gRng.Below(seedCount)]);
			break;
		}

		HttpRequest req;
		size_t used = 0;
		int rc = ParseHttpRequest(input.data(), input.size(), &req, &used);
		if (rc == 0) {
			incomplete++;
			// a connection never has to buffer more than the bound to learn its fate
			CHECK(input.size() < kMaxRequestHead);
			continue;
		}
		if (rc != 1) {
			refused++;
			CHECK(rc == 400 || rc == 414 || rc == 431 || rc == 505);
			continue;
		}
		parsed++;
		CHECK(used <= input.size() && used <= kMaxRequestHead && used >= 16);
		CHECK(req.headers.size() <= kMaxHeaders);
		CHECK(!req.method.empty() && !req.target.empty());
		if (i % 16 == 0) {
			HttpRequest again;
			size_t usedAgain = 0;
			CHECK_EQ(ParseHttpRequest(input.data(), gRng.Below(used), &again, &usedAgain), 0);
		}

		Reply reply;
		Respond(req, gRng.Chance(50) ? rooted : plain, hosts, gRng.Chance(10), &reply);
		switch (reply.status) {
		case 101:
			upgraded++;
			CHECK(reply.upgrade && reply.body.empty());
			break;
		case 200:
			served++;
			[[fallthrough]];
		case 400: case 403: case 404: case 405: case 426: case 503:
			CHECK(!reply.upgrade);
			CHECK(reply.body.size() <= kLargestBody);
			CHECK(reply.head.find("\r\nContent-Length: " + std::to_string(reply.body.size()) + "\r\n") != std::string::npos);
			CHECK(reply.head.find("\r\nX-Content-Type-Options: nosniff\r\n") != std::string::npos);
			break;
		default:
			CHECK(false);
			printf("unexpected status %d\n", reply.status);
			break;
		}
		CHECK(reply.head.compare(0, 9, "HTTP/1.1 ") == 0);
		CHECK(reply.head.size() >= 4 && reply.head.compare(reply.head.size() - 4, 4, "\r\n\r\n") == 0);
		CHECK(reply.head.find("\r\n\r\n") == reply.head.size() - 4);
		CHECK(reply.body.find(kSecret) == std::string::npos);
		// the Host rule, checked from the outside
		const std::string* host = req.Get("host");
		if (reply.status == 200 || reply.status == 101) {
			REQUIRE(host != nullptr);
			std::string name = Lower(*host);
			size_t colon = name.rfind(':');
			if (colon != std::string::npos && name.back() != ']')
				name.resize(colon);
			CHECK(name == "localhost" || name == "127.0.0.1" || name == "[::1]");
		}
	}
	printf("http: %zu inputs: %zu parsed (%zu served, %zu upgraded), %zu incomplete, %zu refused by the parser\n",
		iterations, parsed, served, upgraded, incomplete, refused);
	// the corpus must reach every outcome, or the run proves nothing
	CHECK(parsed > iterations / 20 && incomplete > iterations / 20 && refused > iterations / 20 && served > 100 && upgraded > 100);
}

//
// WebSocket
//

struct Expected {
	WsEvent::Kind kind;
	std::string payload;
	int code;
};

static std::string RandomText(size_t n) {
	static const char* const pieces[] = { "a", "Z", "0", " ", "{", "\"", "\n", "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80" };
	std::string s;
	while (s.size() < n) {
		const char* piece = pieces[gRng.Below(sizeof(pieces) / sizeof(pieces[0]))];
		if (s.size() + strlen(piece) <= n)
			s += piece;
		else
			s += 'x';
	}
	return s;
}

// A valid client stream and what it must decode to.
static std::string ValidStream(size_t limit, std::vector<Expected>* expected) {
	std::string wire;
	size_t messages = gRng.Below(6);
	for (size_t m = 0; m < messages; m++) {
		bool text = gRng.Chance(60);
		size_t size = gRng.Chance(10) ? gRng.Below(limit + 1) : gRng.Below(std::min<size_t>(limit, 300) + 1);
		std::string payload = text ? RandomText(size) : RandomBytes(size);
		size_t at = 0;
		bool first = true;
		for (;;) {
			size_t piece = gRng.Chance(50) ? payload.size() - at : gRng.Below(payload.size() - at + 1);
			bool fin = at + piece == payload.size() && gRng.Chance(80);
			wire += TestClient::Frame(first ? (text ? 1 : 2) : 0, payload.substr(at, piece), fin);
			at += piece;
			first = false;
			if (fin)
				break;
			if (gRng.Chance(20)) {
				bool ping = gRng.Chance(50);
				std::string data = RandomBytes(gRng.Below(126));
				wire += TestClient::Frame(ping ? 9 : 10, data);
				expected->push_back({ ping ? WsEvent::kPing : WsEvent::kPong, data, 0 });
			}
		}
		expected->push_back({ text ? WsEvent::kText : WsEvent::kBinary, payload, 0 });
	}
	if (gRng.Chance(30)) {
		if (gRng.Chance(50)) {
			wire += TestClient::Frame(8, "");
			expected->push_back({ WsEvent::kClose, "", 1005 });
		} else {
			std::string payload = std::string("\x03\xe9", 2) + RandomText(gRng.Below(100));
			wire += TestClient::Frame(8, payload);
			expected->push_back({ WsEvent::kClose, payload, 1001 });
		}
	}
	return wire;
}

static void FuzzWs(size_t iterations) {
	const size_t limits[] = { 0, 1, 125, 126, 1000, 70000 };
	size_t valid = 0, failed = 0, events = 0, bytes = 0;
	for (size_t i = 0; i < iterations; i++) {
		size_t limit = limits[gRng.Below(sizeof(limits) / sizeof(limits[0]))];
		std::vector<Expected> expected;
		std::string wire = ValidStream(limit, &expected);
		bool exact = true;
		switch (gRng.Below(4)) {
		case 0:
			break;
		case 1:
			wire = RandomBytes(gRng.Below(gRng.Chance(10) ? 5000 : 64));
			exact = false;
			break;
		default:
			wire = Mutate(wire);
			exact = false;
			break;
		}
		bytes += wire.size();

		// what a connection does: append what was read, take every complete frame
		WsDecoder decoder(limit);
		std::string in;
		std::vector<WsEvent> got;
		int failCode = 0;
		size_t fed = 0;
		while (fed < wire.size() && !failCode) {
			size_t n = std::min(wire.size() - fed, 1 + gRng.Below(gRng.Chance(50) ? 16 : 4096));
			in.append(wire, fed, n);
			fed += n;
			for (;;) {
				WsEvent ev;
				size_t used = 0;
				WsResult r = decoder.Next(in.data(), in.size(), &used, &ev, &failCode);
				if (r == kWsNeedMore) {
					// one frame at most is ever waited for
					CHECK(in.size() < std::max<size_t>(limit, 125) + 14);
					break;
				}
				if (r == kWsFail) {
					CHECK(failCode == 1002 || failCode == 1007 || failCode == 1009);
					break;
				}
				CHECK(used >= 6 && used <= in.size());
				in.erase(0, used);
				if (ev.kind == WsEvent::kText || ev.kind == WsEvent::kBinary)
					CHECK(ev.payload.size() <= limit);
				else if (ev.kind != WsEvent::kNone)
					CHECK(ev.payload.size() <= 125);
				if (ev.kind == WsEvent::kText)
					CHECK(ValidUtf8(ev.payload.data(), ev.payload.size()));
				if (ev.kind != WsEvent::kNone)
					got.push_back(ev);
			}
		}
		events += got.size();
		if (failCode)
			failed++;

		if (exact) {
			valid++;
			CHECK_EQ(failCode, 0);
			CHECK(in.empty());
			CHECK_EQ(got.size(), expected.size());
			for (size_t k = 0; k < got.size() && k < expected.size(); k++) {
				CHECK_EQ(got[k].kind, expected[k].kind);
				CHECK(got[k].payload == expected[k].payload);
				if (got[k].kind == WsEvent::kClose)
					CHECK_EQ(got[k].code, expected[k].code);
			}
		}
	}
	printf("ws: %zu streams, %zu bytes: %zu valid ones decoded exactly, %zu failed the connection, %zu events\n",
		iterations, bytes, valid, failed, events);
	CHECK(valid > iterations / 10 && failed > iterations / 10 && events > iterations);
}

int main(int argc, char** argv) {
	size_t iterations = argc > 1 ? (size_t)strtoull(argv[1], nullptr, 10) : 100000;
	if (argc > 2)
		gRng.state = strtoull(argv[2], nullptr, 10) | 1;

	namespace fs = std::filesystem;
	fs::path top = fs::temp_directory_path() / ("dbg_net_test_fuzz_" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(top / "root" / "assets");
	fs::create_directories(top / "root" / "sub");
	std::ofstream(top / "secret.txt") << kSecret;
	std::ofstream(top / "root" / "index.html") << "<html>page</html>";
	std::ofstream(top / "root" / "assets" / "app.js") << "app();";
	std::ofstream(top / "root" / "sub" / "file.txt") << std::string(kLargestBody, 'f');
	std::error_code ec;
	fs::create_symlink(top / "secret.txt", top / "root" / "link-out.txt", ec);
	fs::create_directory_symlink(top, top / "root" / "sub" / "up", ec);

	SetStaticProvider(&FuzzProvider);
	FuzzHttp(iterations, (top / "root").string());
	FuzzWs(iterations);

	fs::remove_all(top, ec);
	return Finish("test_fuzz");
}
