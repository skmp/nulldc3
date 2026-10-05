// The HTTP side of dbg_net.cpp: request parsing, the Host / Origin rules, methods, static files and
// path sanitising. The translation unit under test is included, not linked: the parsers are internal to it.

#include "../../../libswirl/debugger/dbg_net.cpp"

#include "check.h"

#include <filesystem>
#include <fstream>

using namespace dbg::net;

static std::vector<std::string> gHosts = { "127.0.0.1", "localhost", "[::1]", "devbox.lan", "[fe80::1]" };

// Stands in for the embedded bundle.
static bool TestProvider(const std::string& path, bool acceptGzip, StaticFile* file) {
	if (path != "index.html" && path != "assets/app-1234.js" && path != "logo.png")
		return false;
	file->body = (acceptGzip ? "gzip:" : "plain:") + path;
	file->gzip = acceptGzip;
	return true;
}

struct Result {
	int parse = 0;		// what ParseHttpRequest returned
	Reply reply;
};

static Result Run(const std::string& request, const Options& options = Options(), bool clientsFull = false) {
	Result result;
	HttpRequest req;
	size_t used = 0;
	result.parse = ParseHttpRequest(request.data(), request.size(), &req, &used);
	if (result.parse == 1)
		Respond(req, options, gHosts, clientsFull, &result.reply);
	else if (result.parse != 0)
		SetError(&result.reply, result.parse);
	return result;
}

static int Status(const std::string& request, const Options& options = Options()) {
	return Run(request, options).reply.status;
}

static std::string Get(const std::string& target, const std::string& headers = "Host: localhost\r\n") {
	return "GET " + target + " HTTP/1.1\r\n" + headers + "\r\n";
}

static std::string Upgrade(const std::string& headers, const std::string& target = "/ws") {
	return "GET " + target + " HTTP/1.1\r\nHost: localhost:55543\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n" + headers + "\r\n";
}

static bool HasLine(const Reply& reply, const std::string& line) {
	return reply.head.find("\r\n" + line + "\r\n") != std::string::npos;
}

static void TestParser() {
	std::string request = "GET /a/b?x=1 HTTP/1.1\r\nHost: localhost:55543\r\nX-Empty:\r\nAccept-Encoding:  gzip, br \r\nACCEPT-encoding: deflate\r\n\r\nrest";
	HttpRequest req;
	size_t used = 0;
	REQUIRE(ParseHttpRequest(request.data(), request.size(), &req, &used) == 1);
	CHECK_EQ(used, request.size() - 4);
	CHECK_EQ(req.method, std::string("GET"));
	CHECK_EQ(req.target, std::string("/a/b?x=1"));
	CHECK_EQ(req.minor, 1);
	CHECK_EQ(req.headers.size(), (size_t)4);
	int count = 0;
	REQUIRE(req.Get("host", &count) != nullptr);
	CHECK_EQ(*req.Get("host"), std::string("localhost:55543"));
	CHECK_EQ(count, 1);
	CHECK_EQ(*req.Get("x-empty"), std::string(""));
	CHECK(req.Get("missing", &count) == nullptr);
	CHECK_EQ(count, 0);
	CHECK_EQ(req.GetList("accept-encoding"), std::string("gzip, br,deflate"));
	req.Get("accept-encoding", &count);
	CHECK_EQ(count, 2);

	// Every proper prefix is "not complete yet", never an error and never a request.
	for (size_t cut = 0; cut < used; cut++)
		CHECK_EQ(ParseHttpRequest(request.data(), cut, &req, &used), 0);

	CHECK_EQ(Run("GET / HTTP/1.0\r\nHost: localhost\r\n\r\n").parse, 1);
	CHECK_EQ(Run("GET / HTTP/1.1\r\n\r\n").parse, 1);

	// malformed request lines
	CHECK_EQ(Run("GET /\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET  / HTTP/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run(" GET / HTTP/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.1 \r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / extra HTTP/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run("G@T / HTTP/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET /\x01 HTTP/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET /caf\xc3\xa9 HTTP/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.1\nHost: localhost\n\n\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / http/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.x\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/11\r\n\r\n").parse, 400);
	CHECK_EQ(Run("\r\nGET / HTTP/1.1\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/2.0\r\n\r\n").parse, 505);
	CHECK_EQ(Run("GET / HTTP/0.9\r\n\r\n").parse, 505);
	CHECK_EQ(Run("GET / HTTP/2.0\r\n\r\n").reply.status, 505);

	// malformed header lines
	CHECK_EQ(Run("GET / HTTP/1.1\r\nHost localhost\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.1\r\n: value\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.1\r\nHost : localhost\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.1\r\nHost: a\r\n folded\r\n\r\n").parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.1\r\nHost: a\rb\r\n\r\n").parse, 400);
	const char nul[] = "GET / HTTP/1.1\r\nHost: a\0b\r\n\r\n";
	CHECK_EQ(Run(std::string(nul, sizeof(nul) - 1)).parse, 400);
	CHECK_EQ(Run("GET / HTTP/1.1\r\nX: tab\tinside is fine\r\n\r\n").parse, 1);

	// size bounds: answered as soon as the bound is passed, with or without the end of the head
	std::string longTarget(kMaxRequestLine, 'a');
	CHECK_EQ(Run("GET /" + longTarget + " HTTP/1.1\r\nHost: localhost\r\n\r\n").parse, 414);
	CHECK_EQ(Run("GET /" + longTarget).parse, 414);
	CHECK_EQ(Run("GET /" + longTarget.substr(0, 1000)).parse, 0);
	CHECK_EQ(Run(std::string(kMaxRequestHead + 1, 'x')).parse, 414);
	std::string filler = "GET / HTTP/1.1\r\n";
	while (filler.size() < kMaxRequestHead - 100)
		filler += "X-Filler: " + std::string(80, 'f') + "\r\n";
	CHECK_EQ(Run(filler + "\r\n").parse, 431);		// more than kMaxHeaders
	std::string big = "GET / HTTP/1.1\r\nX-Big: " + std::string(kMaxRequestHead, 'b');
	CHECK_EQ(Run(big).parse, 431);
	CHECK_EQ(Run(big + "\r\n\r\n").parse, 431);
	CHECK_EQ(Run(big.substr(0, kMaxRequestHead - 1)).parse, 0);
	std::string many = "GET / HTTP/1.1\r\n";
	for (size_t i = 0; i < kMaxHeaders; i++)
		many += "X-" + std::to_string(i) + ": v\r\n";
	CHECK_EQ(Run(many + "\r\n").parse, 1);
	CHECK_EQ(Run(many + "X-More: v\r\n\r\n").parse, 431);
	CHECK_EQ(Run(many + "X-More: v\r\n\r\n").reply.status, 431);
}

static void TestHost() {
	const char* allowed[] = {
		"127.0.0.1", "127.0.0.1:55543", "localhost", "localhost:1", "LOCALHOST:55543", "LocalHost",
		"[::1]", "[::1]:55543", "devbox.lan", "DevBox.LAN:8080", "[fe80::1]:99", "[FE80::1]", "  localhost  ",
	};
	for (const char* host : allowed)
		CHECK_EQ(Status(Get("/", std::string("Host: ") + host + "\r\n")), 200);

	const char* refused[] = {
		"", "evil.example", "evil.example:55543", "localhost.evil.example", "localhost.evil.example:55543",
		"127.0.0.1.evil.example", "evil.example.localhost", "xlocalhost", "localhost.", "localhostx:80",
		"localhost:", "localhost:abc", "localhost:80x", "localhost:123456", "localhost:-1", "127.0.0.1:80:80",
		"::1", "[::1", "::1]", "[::1]x", "[::1]:", "[::1]:x", "[::2]", "[::1]]", "[[::1]", "[]",
		"127.0.0.2", "0.0.0.0", "127.1", "2130706433", "0x7f.0.0.1", "127.0.0.01",
		"user@localhost", "localhost@evil.example", "localhost/", "localhost/evil", "localhost evil.example",
		"localhost,evil.example", "http://localhost", "null", "*",
	};
	for (const char* host : refused)
		CHECK_EQ(Status(Get("/", std::string("Host: ") + host + "\r\n")), 403);

	// no Host at all, and more than one
	CHECK_EQ(Status(Get("/", "")), 403);
	CHECK_EQ(Status("GET / HTTP/1.0\r\n\r\n"), 403);
	CHECK_EQ(Status(Get("/", "Host: localhost\r\nHost: localhost\r\n")), 400);
	CHECK_EQ(Status(Get("/", "Host: evil.example\r\nHost: localhost\r\n")), 400);

	// The Host rule covers everything: the upgrade, other methods, files that do not exist.
	CHECK_EQ(Status("GET /ws HTTP/1.1\r\nHost: evil.example:55543\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nOrigin: http://evil.example:55543\r\n\r\n"), 403);
	CHECK_EQ(Status("POST / HTTP/1.1\r\nHost: evil.example\r\n\r\n"), 403);
	CHECK_EQ(Status(Get("/nothing-here", "Host: evil.example\r\n")), 403);

	// allowedHosts entries are what Server::Start adds to the list; nothing else is derived from them
	std::vector<std::string> only = { "127.0.0.1", "localhost", "[::1]" };
	CHECK(!HostAllowed("devbox.lan", only));
	CHECK(HostAllowed("devbox.lan", gHosts));
}

static void TestOrigin() {
	CHECK_EQ(Status(Upgrade("")), 101);		// no Origin: not a browser
	const char* allowed[] = {
		"http://localhost", "http://localhost:5173", "http://127.0.0.1:55543", "https://127.0.0.1:55543",
		"HTTP://LOCALHOST:5173", "https://LocalHost", "http://[::1]:5173", "http://[::1]", "http://devbox.lan:8080",
	};
	for (const char* origin : allowed)
		CHECK_EQ(Status(Upgrade(std::string("Origin: ") + origin + "\r\n")), 101);

	const char* refused[] = {
		"null", "", "http://evil.example", "http://evil.example:55543", "https://evil.example",
		"http://localhost.evil.example", "http://localhost.evil.example:55543", "http://127.0.0.1.evil.example",
		"http://localhost:5173/", "http://localhost/", "http://localhost/path", "http://localhost:5173/path",
		"http://localhost?x", "http://localhost#x", "http://user@localhost", "http://user:pw@localhost:5173",
		"http://127.0.0.1@evil.example", "http://localhost:", "http://localhost:x", "http://localhost:123456",
		"http://", "https://", "http:/localhost", "http:localhost", "//localhost", "localhost", "localhost:5173",
		"ftp://localhost", "ws://localhost", "file://", "file:///home/user/page.html", "chrome-extension://abcdef",
		"http://[::2]", "http://::1", "http://localhost http://evil.example", "http://127.0.0.2",
	};
	for (const char* origin : refused)
		CHECK_EQ(Status(Upgrade(std::string("Origin: ") + origin + "\r\n")), 403);

	// two Origin headers, even if both are fine
	CHECK_EQ(Status(Upgrade("Origin: http://localhost\r\nOrigin: http://localhost\r\n")), 403);

	// Origin plays no part in plain requests: the page is public, the socket is not
	CHECK_EQ(Status(Get("/", "Host: localhost\r\nOrigin: http://evil.example\r\n")), 200);
}

static void TestUpgrade() {
	Result r = Run(Upgrade(""));
	CHECK_EQ(r.reply.status, 101);
	CHECK(r.reply.upgrade);
	CHECK(HasLine(r.reply, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

	// the query is ignored, the path is exact: any other target is a request for a static file
	CHECK_EQ(Status(Upgrade("", "/ws?token=1")), 101);
	CHECK_EQ(Status(Upgrade("", "/ws/")), 404);
	CHECK_EQ(Status(Upgrade("", "/ws.txt")), 404);
	for (const char* target : { "/WS", "/%77s", "/ws2", "/" }) {
		r = Run(Upgrade("", target));
		CHECK_EQ(r.reply.status, 200);
		CHECK(!r.reply.upgrade);
		CHECK_EQ(r.reply.body, std::string("plain:index.html"));
	}

	const char* base = "GET /ws HTTP/1.1\r\nHost: localhost\r\n";
	const char* key = "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
	std::string b = base;
	// a browser that navigates to /ws
	r = Run(b + "\r\n");
	CHECK_EQ(r.reply.status, 426);
	CHECK(HasLine(r.reply, "Upgrade: websocket"));
	CHECK(HasLine(r.reply, "Sec-WebSocket-Version: 13"));
	CHECK(!r.reply.upgrade);
	// header variants real clients send
	CHECK_EQ(Status(b + "Upgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\n" + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 101);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\nConnection: keep-alive\r\nConnection: upgrade\r\n" + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 101);
	// incomplete or wrong handshakes
	CHECK_EQ(Status(b + "Connection: Upgrade\r\n" + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 426);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\n" + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 426);
	CHECK_EQ(Status(b + "Upgrade: h2c\r\nConnection: Upgrade\r\n" + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 426);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\nConnection: Upgrade\r\n" + key + "\r\n"), 426);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\nConnection: Upgrade\r\n" + key + "Sec-WebSocket-Version: 8\r\n\r\n"), 426);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\nConnection: Upgrade\r\n" + key + "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Version: 13\r\n\r\n"), 426);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\n\r\n"), 400);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: short\r\nSec-WebSocket-Version: 13\r\n\r\n"), 400);
	CHECK_EQ(Status(b + "Upgrade: websocket\r\nConnection: Upgrade\r\n" + key + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 400);
	CHECK_EQ(Status(std::string("GET /ws HTTP/1.0\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n") + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 426);
	CHECK_EQ(Status(std::string("POST /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n") + key + "Sec-WebSocket-Version: 13\r\n\r\n"), 405);

	// all client slots taken
	r = Run(Upgrade(""), Options(), true);
	CHECK_EQ(r.reply.status, 503);
	CHECK(!r.reply.upgrade);
	// a refused Origin is reported as such even then
	CHECK_EQ(Run(Upgrade("Origin: null\r\n"), Options(), true).reply.status, 403);
}

static void TestMethods() {
	for (const char* method : { "POST", "PUT", "DELETE", "HEAD", "OPTIONS", "PATCH", "TRACE", "CONNECT", "get", "Get", "PROPFIND" }) {
		Result r = Run(std::string(method) + " / HTTP/1.1\r\nHost: localhost\r\n\r\n");
		CHECK_EQ(r.reply.status, 405);
		CHECK(HasLine(r.reply, "Allow: GET"));
		CHECK(HasLine(r.reply, "X-Content-Type-Options: nosniff"));
		CHECK(HasLine(r.reply, "Connection: close"));
	}
	CHECK_EQ(Status("POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello"), 405);

	// GET carries no body here
	CHECK_EQ(Status(Get("/", "Host: localhost\r\nContent-Length: 0\r\n")), 200);
	CHECK_EQ(Status(Get("/", "Host: localhost\r\nContent-Length: 5\r\n")), 400);
	CHECK_EQ(Status(Get("/", "Host: localhost\r\nContent-Length: x\r\n")), 400);
	CHECK_EQ(Status(Get("/", "Host: localhost\r\nTransfer-Encoding: chunked\r\n")), 400);
	// absolute-form and asterisk-form targets are for proxies
	CHECK_EQ(Status(Get("http://localhost/")), 400);
	CHECK_EQ(Status(Get("*")), 400);
	CHECK_EQ(Status(Get("index.html")), 400);
}

static void TestProviderFiles() {
	Result r = Run(Get("/"));
	CHECK_EQ(r.reply.status, 200);
	CHECK_EQ(r.reply.body, std::string("plain:index.html"));
	CHECK(HasLine(r.reply, "Content-Type: text/html; charset=utf-8"));
	CHECK(HasLine(r.reply, "Content-Length: 16"));
	CHECK(HasLine(r.reply, "Cache-Control: no-cache"));
	CHECK(HasLine(r.reply, "X-Content-Type-Options: nosniff"));
	CHECK(HasLine(r.reply, "Connection: close"));
	CHECK(HasLine(r.reply, "Vary: Accept-Encoding"));
	CHECK(r.reply.head.find("Content-Encoding") == std::string::npos);
	CHECK_EQ(r.reply.head.substr(0, 17), std::string("HTTP/1.1 200 OK\r\n"));
	CHECK_EQ(r.reply.head.substr(r.reply.head.size() - 4), std::string("\r\n\r\n"));

	CHECK_EQ(Run(Get("/index.html")).reply.body, std::string("plain:index.html"));
	CHECK_EQ(Run(Get("/?ws=ws://localhost:1/ws")).reply.body, std::string("plain:index.html"));
	CHECK_EQ(Run(Get("/index.html?a=b#frag")).reply.body, std::string("plain:index.html"));
	CHECK_EQ(Run(Get("/#/workspace")).reply.body, std::string("plain:index.html"));
	CHECK_EQ(Run(Get("/%69ndex.html")).reply.body, std::string("plain:index.html"));

	r = Run(Get("/assets/app-1234.js", "Host: localhost\r\nAccept-Encoding: gzip, deflate, br\r\n"));
	CHECK_EQ(r.reply.status, 200);
	CHECK_EQ(r.reply.body, std::string("gzip:assets/app-1234.js"));
	CHECK(HasLine(r.reply, "Content-Type: text/javascript; charset=utf-8"));
	CHECK(HasLine(r.reply, "Content-Encoding: gzip"));
	CHECK(HasLine(r.reply, "Cache-Control: max-age=31536000, immutable"));
	CHECK(HasLine(r.reply, "X-Content-Type-Options: nosniff"));

	r = Run(Get("/logo.png"));
	CHECK(HasLine(r.reply, "Content-Type: image/png"));
	CHECK(HasLine(r.reply, "Cache-Control: no-cache"));

	// A name without an extension that is no file is a route of the page: it gets the page, as fresh as "/".
	for (const char* target : { "/missing", "/debugger", "/tool/sh4-sim", "/some/deep/route", "/assets", "/assets/missing", "/a.b/route", "/missing?file=x.js" }) {
		r = Run(Get(target, "Host: localhost\r\nAccept-Encoding: gzip\r\n"));
		CHECK_EQ(r.reply.status, 200);
		CHECK_EQ(r.reply.body, std::string("gzip:index.html"));
		CHECK(HasLine(r.reply, "Content-Type: text/html; charset=utf-8"));
		CHECK(HasLine(r.reply, "Content-Encoding: gzip"));
		CHECK(HasLine(r.reply, "Vary: Accept-Encoding"));
		CHECK(HasLine(r.reply, "Cache-Control: no-cache"));
		CHECK(HasLine(r.reply, "X-Content-Type-Options: nosniff"));
	}
	CHECK_EQ(Run(Get("/debugger")).reply.body, std::string("plain:index.html"));

	// A name with an extension that is no file is 404, never the page; so is all that the path rules refuse.
	for (const char* target : { "/missing.js", "/assets/missing-1234.js", "/Index.html", "/favicon.ico", "/route.v2", "/some/deep/route.map",
			"/assets/", "/index.html/", "/workspace/", "/../missing", "/.missing", "/missing.", "/con" }) {
		r = Run(Get(target));
		CHECK_EQ(r.reply.status, 404);
		CHECK(HasLine(r.reply, "X-Content-Type-Options: nosniff"));
		CHECK(HasLine(r.reply, "Content-Type: text/plain; charset=utf-8"));
	}

	CHECK(HasExtension("index.html"));
	CHECK(HasExtension("assets/app-1234.js"));
	CHECK(HasExtension("a/b.tar.gz"));
	CHECK(!HasExtension("debugger"));
	CHECK(!HasExtension("tool/sh4-sim"));
	CHECK(!HasExtension("a.b/route"));

	// without a provider everything is 404
	SetStaticProvider(nullptr);
	CHECK_EQ(Status(Get("/")), 404);
	CHECK_EQ(Status(Get("/debugger")), 404);
	SetStaticProvider(&TestProvider);
}

static void TestAcceptEncoding() {
	CHECK(AcceptsGzip("gzip"));
	CHECK(AcceptsGzip("gzip, deflate, br"));
	CHECK(AcceptsGzip("deflate,GZIP"));
	CHECK(AcceptsGzip("br;q=1.0, gzip;q=0.8, *;q=0.1"));
	CHECK(AcceptsGzip("x-gzip"));
	CHECK(AcceptsGzip("*"));
	CHECK(AcceptsGzip("gzip;q=0.001"));
	CHECK(AcceptsGzip("gzip ; q=1"));
	CHECK(!AcceptsGzip(""));
	CHECK(!AcceptsGzip("identity"));
	CHECK(!AcceptsGzip("deflate, br"));
	CHECK(!AcceptsGzip("gzip;q=0"));
	CHECK(!AcceptsGzip("gzip;q=0.0, deflate"));
	CHECK(!AcceptsGzip("*;q=0"));
	CHECK(!AcceptsGzip("*, gzip;q=0"));
	CHECK(!AcceptsGzip("gzipx"));
}

static void TestMime() {
	CHECK_EQ(std::string(MimeType("index.html")), std::string("text/html; charset=utf-8"));
	CHECK_EQ(std::string(MimeType("assets/a.js")), std::string("text/javascript; charset=utf-8"));
	CHECK_EQ(std::string(MimeType("a.css")), std::string("text/css; charset=utf-8"));
	CHECK_EQ(std::string(MimeType("a.json")), std::string("application/json"));
	CHECK_EQ(std::string(MimeType("assets/aica_dsp_bg-CjwAqFkL.wasm")), std::string("application/wasm"));
	CHECK_EQ(std::string(MimeType("a.png")), std::string("image/png"));
	CHECK_EQ(std::string(MimeType("a.svg")), std::string("image/svg+xml"));
	CHECK_EQ(std::string(MimeType("a.ico")), std::string("image/x-icon"));
	CHECK_EQ(std::string(MimeType("a.ttf")), std::string("font/ttf"));
	CHECK_EQ(std::string(MimeType("a.woff2")), std::string("font/woff2"));
	CHECK_EQ(std::string(MimeType("a.js.map")), std::string("application/json"));
	CHECK_EQ(std::string(MimeType("a.txt")), std::string("text/plain; charset=utf-8"));
	CHECK_EQ(std::string(MimeType("A.HTML")), std::string("text/html; charset=utf-8"));
	CHECK_EQ(std::string(MimeType("a.exe")), std::string("application/octet-stream"));
	CHECK_EQ(std::string(MimeType("noextension")), std::string("application/octet-stream"));
	CHECK_EQ(std::string(MimeType("dir.js/file")), std::string("application/octet-stream"));
}

static void TestPaths() {
	std::string path;
	CHECK_EQ(TargetToPath("/", &path), kPathOk);
	CHECK_EQ(path, std::string("index.html"));
	CHECK_EQ(TargetToPath("/a/b/c.txt?q#f", &path), kPathOk);
	CHECK_EQ(path, std::string("a/b/c.txt"));
	CHECK_EQ(TargetToPath("/a%20b/c%2Bd", &path), kPathOk);
	CHECK_EQ(path, std::string("a b/c+d"));
	CHECK_EQ(TargetToPath("/a%2fb", &path), kPathOk);		// an encoded separator is a separator
	CHECK_EQ(path, std::string("a/b"));

	for (const char* target : { "", "a", "?x", "/%", "/%2", "/%zz", "/a%", "/a%f", "/%g0" })
		CHECK_EQ(TargetToPath(target, &path), kPathMalformed);

	const char* refused[] = {
		"//", "//a", "/a//b", "/a/", "/.", "/..", "/../a", "/a/..", "/a/../b", "/a/./b", "/./a",
		"/%2e%2e/a", "/%2E%2E/a", "/..%2fa", "/a/%2e%2e", "/.%2e/a", "/%2e/a", "/a%2f..%2fb", "/..%5ca",
		"/.hidden", "/a/.git/config", "/.env", "/...", "/a.", "/a./b", "/a%20", "/a /b",
		"/a\\b", "/a%5cb", "/C:", "/c:/windows/win.ini", "/a:stream", "/a*", "/a%3f", "/a\"b", "/a<b", "/a>b", "/a|b",
		"/%00", "/a%00.png", "/a%0d%0ab", "/a%7f", "/caf%c3%a9", "/%ff",
		"/con", "/CON", "/con.txt", "/a/NUL", "/nul.tar.gz", "/prn", "/aux.js", "/com1", "/COM9.txt", "/lpt1", "/conin$", "/CONOUT$",
		"/nul%20.txt", "/CON%20%20.js", "/a/aux .h", "/com1%20.txt", "/LPT9%20.tar.gz", "/conout$%20.log",
	};
	for (const char* target : refused)
		CHECK_EQ(TargetToPath(target, &path), kPathNotFound);

	// names that only look special
	for (const char* target : { "/console.js", "/com.js", "/com10", "/lpt", "/a..b", "/a.b.c", "/-", "/~a", "/a%25b", "/nulls", "/a;b", "/a,b", "/a'b", "/(a)", "/a&b=c",
			"/nul%20l.txt", "/com1%20a.txt", "/%20con.txt", "/a.nul%20.txt" })
		CHECK_EQ(TargetToPath(target, &path), kPathOk);
}

static void WriteFile(const std::filesystem::path& path, const std::string& content) {
	std::filesystem::create_directories(path.parent_path());
	std::ofstream file(path, std::ios::binary);
	file.write(content.data(), (std::streamsize)content.size());
}

static void TestWebRoot() {
	namespace fs = std::filesystem;
	fs::path top = fs::temp_directory_path() / ("dbg_net_test_http_" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::path root = top / "root";
	WriteFile(root / "index.html", "<html>root page</html>");
	WriteFile(root / "assets" / "app-abcd.js", "console.log(1)");
	WriteFile(root / "assets" / "deep" / "er" / "file.txt", "deep");
	WriteFile(root / "sub" / "index.html", "sub page");
	WriteFile(root / "with space.txt", "space");
	WriteFile(root / ".hidden", "hidden");
	WriteFile(root / "empty.css", "");
	WriteFile(top / "secret.txt", "secret");
	WriteFile(top / "outside" / "secret.txt", "secret");
	std::string blob(300000, '\0');
	for (size_t i = 0; i < blob.size(); i++)
		blob[i] = (char)(i * 31 + i / 251);
	WriteFile(root / "assets" / "blob.wasm", blob);

	Options options;
	options.webRoot = root.string();
	// With a web root the provider is not consulted at all.
	Result r = Run(Get("/"), options);
	CHECK_EQ(r.reply.status, 200);
	CHECK_EQ(r.reply.body, std::string("<html>root page</html>"));
	CHECK(HasLine(r.reply, "Cache-Control: no-cache"));
	CHECK(HasLine(r.reply, "Content-Type: text/html; charset=utf-8"));
	CHECK(r.reply.head.find("Content-Encoding") == std::string::npos);
	CHECK_EQ(Run(Get("/index.html"), options).reply.body, std::string("<html>root page</html>"));
	r = Run(Get("/assets/app-abcd.js?v=2"), options);
	CHECK_EQ(r.reply.body, std::string("console.log(1)"));
	CHECK(HasLine(r.reply, "Cache-Control: max-age=31536000, immutable"));
	CHECK_EQ(Run(Get("/assets/deep/er/file.txt"), options).reply.body, std::string("deep"));
	CHECK_EQ(Run(Get("/with%20space.txt"), options).reply.body, std::string("space"));
	CHECK_EQ(Run(Get("/sub/index.html"), options).reply.body, std::string("sub page"));
	r = Run(Get("/empty.css"), options);
	CHECK_EQ(r.reply.status, 200);
	CHECK(HasLine(r.reply, "Content-Length: 0"));
	r = Run(Get("/assets/blob.wasm"), options);
	CHECK(r.reply.body == blob);
	CHECK(HasLine(r.reply, "Content-Type: application/wasm"));
	CHECK(HasLine(r.reply, "Content-Length: 300000"));

	const char* missing[] = {
		"/logo.png",						// only the provider has it
		"/sub/", "/missing.txt",
		"/../secret.txt", "/..%2fsecret.txt", "/%2e%2e/secret.txt", "/%2e%2e%2fsecret.txt", "/assets/../../secret.txt",
		"/assets/..%2f..%2fsecret.txt", "/..\\secret.txt", "/..%5csecret.txt", "/assets/../index.html",
		"/../outside/secret.txt", "/./index.html", "//index.html", "/.hidden", "/index.html.", "/index.html%20",
		"/index.html%00", "/index.html%00.png", "/index.html/", "/index.html/.", "/INDEX.HTML::$DATA",
	};
	for (const char* target : missing)
		CHECK_EQ(Run(Get(target), options).reply.status, 404);
	// an absolute path in the target is still looked up below the root
	CHECK_EQ(Run(Get("/" + (top / "secret.txt").generic_string()), options).reply.status, 404);
	CHECK_EQ(Run(Get("/etc/passwd.txt"), options).reply.status, 404);
	CHECK_EQ(Run(Get("//etc/passwd"), options).reply.status, 404);

	// A name without an extension that is no file gets the page of the root: routes, and directories too.
	for (const char* target : { "/debugger", "/tool/sh4-sim", "/sub", "/assets", "/assets/deep/er", "/assets/missing", "/etc/passwd" }) {
		r = Run(Get(target), options);
		CHECK_EQ(r.reply.status, 200);
		CHECK_EQ(r.reply.body, std::string("<html>root page</html>"));
		CHECK(HasLine(r.reply, "Content-Type: text/html; charset=utf-8"));
		CHECK(HasLine(r.reply, "Cache-Control: no-cache"));
		CHECK(r.reply.head.find("Content-Encoding") == std::string::npos);
	}

	// symbolic links: fine inside the root, refused when they lead out of it
	std::error_code ec;
	fs::create_symlink(root / "index.html", root / "link-in.html", ec);
	if (!ec) {
		fs::create_symlink(top / "secret.txt", root / "link-out.txt", ec);
		fs::create_directory_symlink(top / "outside", root / "dir-out", ec);
		fs::create_directory_symlink(root / "assets", root / "dir-in", ec);
		fs::create_symlink("../secret.txt", root / "relative-out.txt", ec);
		fs::create_symlink(root / "nowhere", root / "dangling.txt", ec);
		fs::create_directory_symlink(top, root / "assets" / "up", ec);
		fs::create_symlink(top / "secret.txt", root / "link-out", ec);
		CHECK_EQ(Run(Get("/link-in.html"), options).reply.body, std::string("<html>root page</html>"));
		CHECK_EQ(Run(Get("/dir-in/app-abcd.js"), options).reply.body, std::string("console.log(1)"));
		CHECK_EQ(Run(Get("/link-out.txt"), options).reply.status, 404);
		CHECK_EQ(Run(Get("/dir-out/secret.txt"), options).reply.status, 404);
		CHECK_EQ(Run(Get("/relative-out.txt"), options).reply.status, 404);
		CHECK_EQ(Run(Get("/dangling.txt"), options).reply.status, 404);
		CHECK_EQ(Run(Get("/assets/up/secret.txt"), options).reply.status, 404);
		// without an extension the answer is the page, not what the link leads to
		CHECK_EQ(Run(Get("/link-out"), options).reply.body, std::string("<html>root page</html>"));
		CHECK_EQ(Run(Get("/dir-out"), options).reply.body, std::string("<html>root page</html>"));
		CHECK_EQ(Run(Get("/assets/up/root/index.html"), options).reply.body, std::string("<html>root page</html>"));

		// the root itself may be reached through a link
		fs::create_directory_symlink(root, top / "root-link", ec);
		Options linked;
		linked.webRoot = (top / "root-link").string();
		CHECK_EQ(Run(Get("/"), linked).reply.body, std::string("<html>root page</html>"));
		CHECK_EQ(Run(Get("/link-out.txt"), linked).reply.status, 404);
	} else {
		printf("symbolic links not available here, cases skipped\n");
	}

	// a root with a trailing separator, a root that does not exist, a root that is a file
	Options variant;
	variant.webRoot = root.string() + "/";
	CHECK_EQ(Run(Get("/"), variant).reply.status, 200);
	variant.webRoot = (top / "nowhere").string();
	CHECK_EQ(Run(Get("/"), variant).reply.status, 404);
	CHECK_EQ(Run(Get("/debugger"), variant).reply.status, 404);		// no page to fall back to
	variant.webRoot = (root / "index.html").string();
	CHECK_EQ(Run(Get("/"), variant).reply.status, 404);
	// a root that is not UTF-8, which a C++ library that converts narrow file names refuses to make a path of
	variant.webRoot = root.string() + "\xe9\xff";
	CHECK_EQ(Run(Get("/"), variant).reply.status, 404);
	CHECK_EQ(Run(Get("/index.html"), variant).reply.status, 404);
	CHECK_EQ(Run(Get("/debugger"), variant).reply.status, 404);

	// the Host rule comes first here too
	CHECK_EQ(Run(Get("/", "Host: evil.example\r\n"), options).reply.status, 403);

	fs::remove_all(top, ec);
}

int main() {
	SetStaticProvider(&TestProvider);
	TestParser();
	TestHost();
	TestOrigin();
	TestUpgrade();
	TestMethods();
	TestProviderFiles();
	TestAcceptEncoding();
	TestMime();
	TestPaths();
	TestWebRoot();
	return Finish("test_http");
}
