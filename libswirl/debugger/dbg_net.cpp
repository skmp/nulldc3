/*
	This file is part of libswirl
	BSD 3-clause, see license/bsd
*/

// Platform sockets. <winsock2.h> has to come before anything that may include <windows.h>.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET sock_t;
static const sock_t kNoSocket = INVALID_SOCKET;
static const int kSendFlags = 0;
static const int kShutdownSend = SD_SEND;
// SO_REUSEADDR on Windows lets a second process bind the same port.
static const int kListenOption = SO_EXCLUSIVEADDRUSE;

static bool SockStartup() { WSADATA data; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }
static void SockCleanup() { WSACleanup(); }
static void SockClose(sock_t s) { closesocket(s); }
static int SockError() { return WSAGetLastError(); }
static bool SockWouldBlock(int error) { return error == WSAEWOULDBLOCK; }
static bool SockInterrupted(int error) { (void)error; return false; }
static bool SockSetup(sock_t s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on) == 0; }
static int SockPoll(struct pollfd* fds, size_t count, int timeoutMs) { return WSAPoll(fds, (ULONG)count, timeoutMs); }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

typedef int sock_t;
static const sock_t kNoSocket = -1;
// A peer that went away must not raise SIGPIPE: nothing in the emulator ignores it.
#ifdef MSG_NOSIGNAL
static const int kSendFlags = MSG_NOSIGNAL;
#else
static const int kSendFlags = 0;	// SO_NOSIGPIPE, set in SockSetup, covers it
#endif
static const int kShutdownSend = SHUT_WR;
static const int kListenOption = SO_REUSEADDR;

static bool SockStartup() { return true; }
static void SockCleanup() {}
static void SockClose(sock_t s) { close(s); }
static int SockError() { return errno; }
static bool SockWouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK || error == EINTR; }
static bool SockInterrupted(int error) { return error == EINTR; }
static bool SockSetup(sock_t s) {
	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0)
		return false;
	fcntl(s, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
	int on = 1;
	setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
	return true;
}
static int SockPoll(struct pollfd* fds, size_t count, int timeoutMs) { return poll(fds, (nfds_t)count, timeoutMs); }
#endif

#include "dbg_net.h"

#include "deps/crypto/sha1.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace dbg { namespace net {

namespace {

const size_t kMaxRequestLine = 2048;
const size_t kMaxRequestHead = 8192;		// request line + headers + the empty line
const size_t kMaxHeaders = 64;
const size_t kMaxQueuedBytes = 16u << 20;	// per WebSocket client
const size_t kMergeBytes = 16u << 10;		// queued output is merged into chunks of up to this size
const size_t kFlushBytes = 1u << 20;		// most that is written to one connection per pass of the loop
const size_t kMaxStaticFile = 64u << 20;	// files from Options::webRoot are read whole
const size_t kMaxHttpConnections = 32;		// connections that are not WebSocket clients
const int kStallMs = 10000;					// an HTTP connection that makes no progress is closed
const int kLingerMs = 2000;					// wait for the peer's FIN after sending ours
const int kAcceptRetryMs = 100;				// after accept failed for lack of descriptors
const int kIdlePollMs = 250;				// what a lost wake-up may cost, see Server::Impl::Wake

std::atomic<StaticProvider> gStaticProvider{nullptr};

//
// Text helpers. All ASCII only: HTTP field names and tokens are ASCII, and locale-dependent <cctype> must
// not decide what "localhost" compares equal to.
//

char LowerChar(char c) {
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

std::string Lower(std::string s) {
	for (char& c : s)
		c = LowerChar(c);
	return s;
}

bool IsDigit(char c) {
	return c >= '0' && c <= '9';
}

// RFC 7230 tchar
bool IsTokenChar(char c) {
	if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || IsDigit(c))
		return true;
	return c != '\0' && strchr("!#$%&'*+-.^_`|~", c) != nullptr;
}

std::string Trim(const std::string& s) {
	size_t b = 0, e = s.size();
	while (b < e && (s[b] == ' ' || s[b] == '\t'))
		b++;
	while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t'))
		e--;
	return s.substr(b, e - b);
}

// Is `token` an element of the comma separated `list` (case-insensitive)?
bool HasToken(const std::string& list, const char* token) {
	size_t pos = 0;
	while (pos <= list.size()) {
		size_t comma = list.find(',', pos);
		if (comma == std::string::npos)
			comma = list.size();
		if (Lower(Trim(list.substr(pos, comma - pos))) == token)
			return true;
		pos = comma + 1;
	}
	return false;
}

// Accept-Encoding: is gzip acceptable? Absent header (empty list) means no.
bool AcceptsGzip(const std::string& list) {
	bool any = false;
	size_t pos = 0;
	while (pos <= list.size()) {
		size_t comma = list.find(',', pos);
		if (comma == std::string::npos)
			comma = list.size();
		std::string item = Lower(list.substr(pos, comma - pos));
		pos = comma + 1;

		size_t semi = item.find(';');
		std::string coding = Trim(item.substr(0, semi));
		bool refused = false;
		if (semi != std::string::npos) {
			std::string param = Trim(item.substr(semi + 1));
			if (param.compare(0, 2, "q=") == 0)
				refused = param.find_first_not_of("0.", 2) == std::string::npos;	// q=0, q=0.0, ...
		}
		if (coding == "gzip" || coding == "x-gzip")
			return !refused;
		if (coding == "*" && !refused)
			any = true;
	}
	return any;
}

bool ValidUtf8(const char* data, size_t size) {
	const unsigned char* p = (const unsigned char*)data;
	const unsigned char* end = p + size;
	while (p < end) {
		unsigned char c = *p;
		if (c < 0x80) {
			p++;
			continue;
		}
		int extra;
		uint32_t cp;
		if (c >= 0xc2 && c <= 0xdf) {
			extra = 1;
			cp = c & 0x1f;
		} else if (c >= 0xe0 && c <= 0xef) {
			extra = 2;
			cp = c & 0x0f;
		} else if (c >= 0xf0 && c <= 0xf4) {
			extra = 3;
			cp = c & 0x07;
		} else {
			return false;
		}
		if (end - p <= extra)
			return false;
		for (int i = 1; i <= extra; i++) {
			if ((p[i] & 0xc0) != 0x80)
				return false;
			cp = (cp << 6) | (p[i] & 0x3f);
		}
		// overlong forms, UTF-16 surrogates, beyond U+10FFFF
		if (extra == 2 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff)))
			return false;
		if (extra == 3 && (cp < 0x10000 || cp > 0x10ffff))
			return false;
		p += extra + 1;
	}
	return true;
}

std::string Base64(const unsigned char* data, size_t size) {
	static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((size + 2) / 3 * 4);
	for (size_t i = 0; i < size; i += 3) {
		uint32_t v = (uint32_t)data[i] << 16;
		if (i + 1 < size)
			v |= (uint32_t)data[i + 1] << 8;
		if (i + 2 < size)
			v |= data[i + 2];
		out += alphabet[(v >> 18) & 63];
		out += alphabet[(v >> 12) & 63];
		out += (i + 1 < size) ? alphabet[(v >> 6) & 63] : '=';
		out += (i + 2 < size) ? alphabet[v & 63] : '=';
	}
	return out;
}

//
// HTTP
//

struct HttpRequest {
	std::string method;
	std::string target;
	int minor = 1;												// HTTP/1.<minor>
	std::vector<std::pair<std::string, std::string>> headers;	// names lower-cased, values trimmed

	// First value of a header; *count receives how many times the header is present.
	const std::string* Get(const char* name, int* count = nullptr) const {
		const std::string* found = nullptr;
		int n = 0;
		for (const auto& h : headers) {
			if (h.first == name) {
				if (!found)
					found = &h.second;
				n++;
			}
		}
		if (count)
			*count = n;
		return found;
	}

	// Every value of a list header, joined the way a single field would carry them.
	std::string GetList(const char* name) const {
		std::string all;
		for (const auto& h : headers) {
			if (h.first == name) {
				if (!all.empty())
					all += ',';
				all += h.second;
			}
		}
		return all;
	}
};

// Parses a request head from data. Returns 0 when the head is not complete yet, 1 when *req is filled and
// *used bytes belong to it, otherwise the HTTP status to refuse the connection with.
int ParseHttpRequest(const char* data, size_t size, HttpRequest* req, size_t* used) {
	static const char crlf[] = "\r\n";
	static const char blank[] = "\r\n\r\n";
	const char* end = data + std::min(size, kMaxRequestHead);

	const char* lineEnd = std::search(data, end, crlf, crlf + 2);
	if (lineEnd == end) {
		if (size > kMaxRequestLine)
			return 414;
		return 0;
	}
	if ((size_t)(lineEnd - data) > kMaxRequestLine)
		return 414;
	const char* headEnd = std::search(data, end, blank, blank + 4);
	if (headEnd == end)
		return size >= kMaxRequestHead ? 431 : 0;

	// METHOD SP request-target SP HTTP/1.x
	const char* sp1 = std::find(data, lineEnd, ' ');
	if (sp1 == data || sp1 == lineEnd)
		return 400;
	const char* sp2 = std::find(sp1 + 1, lineEnd, ' ');
	if (sp2 == sp1 + 1 || sp2 == lineEnd)
		return 400;
	req->method.assign(data, sp1);
	req->target.assign(sp1 + 1, sp2);
	std::string version(sp2 + 1, lineEnd);
	for (char c : req->method) {
		if (!IsTokenChar(c))
			return 400;
	}
	for (char c : req->target) {
		if (c <= 0x20 || c >= 0x7f)
			return 400;
	}
	if (version.size() != 8 || version.compare(0, 5, "HTTP/") != 0 || !IsDigit(version[5]) || version[6] != '.' || !IsDigit(version[7]))
		return 400;
	if (version[5] != '1')
		return 505;
	req->minor = version[7] - '0';

	req->headers.clear();
	const char* line = lineEnd + 2;
	while (line < headEnd + 2) {
		const char* eol = std::search(line, headEnd + 2, crlf, crlf + 2);
		const char* colon = std::find(line, eol, ':');
		if (colon == line || colon == eol)
			return 400;
		if (req->headers.size() == kMaxHeaders)
			return 431;
		std::string name(line, colon);
		for (char c : name) {
			if (!IsTokenChar(c))	// also refuses obsolete line folding and a space before the colon
				return 400;
		}
		std::string value = Trim(std::string(colon + 1, eol));
		for (char c : value) {
			if (((unsigned char)c < 0x20 && c != '\t') || c == 0x7f)
				return 400;
		}
		req->headers.emplace_back(Lower(name), value);
		line = eol + 2;
	}

	*used = (size_t)(headEnd - data) + 4;
	return 1;
}

// "host", "host:port" or "[v6]:port" -> the host part, lower-cased. False when malformed.
bool SplitHostPort(const std::string& value, std::string* host) {
	size_t end;
	if (!value.empty() && value[0] == '[') {
		end = value.find(']');
		if (end == std::string::npos)
			return false;
		end++;
	} else {
		end = value.find(':');
		if (end == std::string::npos)
			end = value.size();
	}
	if (end == 0)
		return false;
	if (end < value.size()) {
		if (value[end] != ':')
			return false;
		size_t digits = value.size() - end - 1;
		if (digits == 0 || digits > 5)
			return false;
		for (size_t i = end + 1; i < value.size(); i++) {
			if (!IsDigit(value[i]))
				return false;
		}
	}
	*host = Lower(value.substr(0, end));
	return true;
}

// The comparison is exact on the whole host: "localhost.evil.example" must not pass as "localhost".
bool HostAllowed(const std::string& hostHeader, const std::vector<std::string>& hosts) {
	std::string host;
	if (!SplitHostPort(hostHeader, &host))
		return false;
	return std::find(hosts.begin(), hosts.end(), host) != hosts.end();
}

// An Origin passes only as http(s)://host[:port] with an allowed host. "null" (sandboxed frames, file:
// and data: pages) and anything with a path, user info or another scheme is refused.
bool OriginAllowed(const std::string& origin, const std::vector<std::string>& hosts) {
	std::string lower = Lower(origin);
	size_t skip;
	if (lower.compare(0, 7, "http://") == 0)
		skip = 7;
	else if (lower.compare(0, 8, "https://") == 0)
		skip = 8;
	else
		return false;
	return HostAllowed(lower.substr(skip), hosts);
}

int HexValue(char c) {
	if (IsDigit(c))
		return c - '0';
	c = LowerChar(c);
	return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

// Device names Windows resolves in every directory, with or without an extension.
bool IsReservedName(const std::string& segment) {
	std::string stem = Lower(segment.substr(0, segment.find('.')));
	static const char* const names[] = { "con", "prn", "aux", "nul", "conin$", "conout$" };
	for (const char* name : names) {
		if (stem == name)
			return true;
	}
	if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && IsDigit(stem[3]))
		return true;
	return false;
}

enum PathResult { kPathOk, kPathMalformed, kPathNotFound };

// Request target -> path below the web root, '/' separated, "index.html" for "/". The query is dropped.
// Only plain names survive: no empty, "." or ".." segment, nothing hidden (leading '.'), no character a
// file system treats specially (separators, drive and stream colons, wildcards) and no name Windows maps
// to something else (trailing dot or space, device names).
PathResult TargetToPath(const std::string& target, std::string* path) {
	size_t cut = target.find_first_of("?#");
	std::string raw = target.substr(0, cut);
	if (raw.empty() || raw[0] != '/')
		return kPathMalformed;

	std::string decoded;
	decoded.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); i++) {
		char c = raw[i];
		if (c == '%') {
			int hi = i + 2 < raw.size() ? HexValue(raw[i + 1]) : -1;
			int lo = i + 2 < raw.size() ? HexValue(raw[i + 2]) : -1;
			if (hi < 0 || lo < 0)
				return kPathMalformed;
			c = (char)(hi * 16 + lo);
			i += 2;
		}
		if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f || strchr("\\:*?\"<>|", c))
			return kPathNotFound;
		decoded += c;
	}

	if (decoded == "/") {
		*path = "index.html";
		return kPathOk;
	}
	size_t pos = 1;
	while (pos <= decoded.size()) {
		size_t slash = decoded.find('/', pos);
		if (slash == std::string::npos)
			slash = decoded.size();
		std::string segment = decoded.substr(pos, slash - pos);
		if (segment.empty() || segment[0] == '.' || segment.back() == '.' || segment.back() == ' ' || IsReservedName(segment))
			return kPathNotFound;
		pos = slash + 1;
	}
	*path = decoded.substr(1);
	return kPathOk;
}

const char* MimeType(const std::string& path) {
	static const struct { const char* ext; const char* type; } table[] = {
		{ "html", "text/html; charset=utf-8" },
		{ "js", "text/javascript; charset=utf-8" },
		{ "css", "text/css; charset=utf-8" },
		{ "json", "application/json" },
		{ "wasm", "application/wasm" },
		{ "png", "image/png" },
		{ "svg", "image/svg+xml" },
		{ "ico", "image/x-icon" },
		{ "ttf", "font/ttf" },
		{ "woff2", "font/woff2" },
		{ "map", "application/json" },
		{ "txt", "text/plain; charset=utf-8" },
	};
	size_t dot = path.rfind('.');
	size_t slash = path.rfind('/');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
		std::string ext = Lower(path.substr(dot + 1));
		for (const auto& entry : table) {
			if (ext == entry.ext)
				return entry.type;
		}
	}
	return "application/octet-stream";
}

// Reads <root>/<path>. The path is already plain (TargetToPath); resolving both ends catches a symbolic
// link that leads out of the root.
bool ReadRootFile(const std::string& root, const std::string& path, std::string* body) {
	namespace fs = std::filesystem;
	std::error_code ec;
	fs::path base = fs::canonical(fs::path(root), ec);
	if (ec)
		return false;
	fs::path full = fs::canonical(base / fs::path(path), ec);
	if (ec)
		return false;
	if (std::mismatch(base.begin(), base.end(), full.begin(), full.end()).first != base.end())
		return false;
	if (!fs::is_regular_file(full, ec) || ec)
		return false;
	std::uintmax_t size = fs::file_size(full, ec);
	if (ec || size > kMaxStaticFile)
		return false;

	std::ifstream file(full, std::ios::binary);
	if (!file)
		return false;
	body->resize((size_t)size);
	if (size != 0)
		file.read(&(*body)[0], (std::streamsize)size);
	return (std::uintmax_t)file.gcount() == size;
}

const char* StatusText(int status) {
	switch (status) {
	case 101: return "Switching Protocols";
	case 200: return "OK";
	case 400: return "Bad Request";
	case 403: return "Forbidden";
	case 404: return "Not Found";
	case 405: return "Method Not Allowed";
	case 414: return "URI Too Long";
	case 426: return "Upgrade Required";
	case 431: return "Request Header Fields Too Large";
	case 503: return "Service Unavailable";
	case 505: return "HTTP Version Not Supported";
	}
	return "Error";
}

struct Reply {
	int status = 0;
	std::string head;		// status line and headers, including the empty line
	std::string body;
	bool upgrade = false;	// 101: the connection carries WebSocket frames from here on
};

// Every plain HTTP response closes its connection: no request body is ever read, so nothing can be left
// in the stream that would be taken for the next request.
void SetReply(Reply* reply, int status, const char* type, std::string body, const std::string& headers = std::string()) {
	reply->status = status;
	reply->body = std::move(body);
	reply->upgrade = false;
	reply->head = "HTTP/1.1 " + std::to_string(status) + " " + StatusText(status) + "\r\n"
		"Content-Type: " + type + "\r\n"
		"Content-Length: " + std::to_string(reply->body.size()) + "\r\n"
		+ headers +
		"X-Content-Type-Options: nosniff\r\n"
		"Connection: close\r\n"
		"\r\n";
}

void SetError(Reply* reply, int status, const std::string& headers = std::string()) {
	SetReply(reply, status, "text/plain; charset=utf-8", std::to_string(status) + " " + StatusText(status) + "\n", headers);
}

std::string WsAccept(const std::string& key) {
	std::string input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	sha1_ctx ctx;
	unsigned char digest[SHA1_DIGEST_SIZE];
	sha1_init(&ctx);
	sha1_update(&ctx, (unsigned)input.size(), (const unsigned char*)input.data());
	sha1_final(&ctx);
	sha1_digest(&ctx, SHA1_DIGEST_SIZE, digest);
	return Base64(digest, sizeof(digest));
}

// Sec-WebSocket-Key is the base64 form of 16 bytes: 22 characters and "==".
bool ValidWsKey(const std::string& key) {
	if (key.size() != 24 || key[22] != '=' || key[23] != '=')
		return false;
	for (size_t i = 0; i < 22; i++) {
		char c = key[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || IsDigit(c) || c == '+' || c == '/'))
			return false;
	}
	return true;
}

void RespondUpgrade(const HttpRequest& req, const std::vector<std::string>& hosts, bool clientsFull, Reply* reply) {
	int count = 0;
	const std::string* origin = req.Get("origin", &count);
	if (count > 1 || (origin && !OriginAllowed(*origin, hosts)))
		return SetError(reply, 403);

	static const char upgradeHeaders[] = "Upgrade: websocket\r\nSec-WebSocket-Version: 13\r\n";
	if (req.minor < 1 || !HasToken(req.GetList("upgrade"), "websocket") || !HasToken(req.GetList("connection"), "upgrade"))
		return SetError(reply, 426, upgradeHeaders);
	const std::string* version = req.Get("sec-websocket-version", &count);
	if (!version || count != 1 || *version != "13")
		return SetError(reply, 426, upgradeHeaders);
	const std::string* key = req.Get("sec-websocket-key", &count);
	if (!key || count != 1 || !ValidWsKey(*key))
		return SetError(reply, 400);
	if (clientsFull)
		return SetError(reply, 503);

	// No extension and no subprotocol is ever selected, whatever the client offers.
	reply->status = 101;
	reply->upgrade = true;
	reply->body.clear();
	reply->head = "HTTP/1.1 101 Switching Protocols\r\n"
		"Upgrade: websocket\r\n"
		"Connection: Upgrade\r\n"
		"Sec-WebSocket-Accept: " + WsAccept(*key) + "\r\n"
		"\r\n";
}

// Decides the response to a parsed request. Order matters: the Host check comes before anything that
// could tell a rebinding page what exists here.
void Respond(const HttpRequest& req, const Options& options, const std::vector<std::string>& hosts, bool clientsFull, Reply* reply) {
	int count = 0;
	const std::string* host = req.Get("host", &count);
	if (count > 1)
		return SetError(reply, 400);
	if (!host || !HostAllowed(*host, hosts))
		return SetError(reply, 403);
	if (req.method != "GET")
		return SetError(reply, 405, "Allow: GET\r\n");
	const std::string* length = req.Get("content-length");
	if (req.Get("transfer-encoding") || (length && length->find_first_not_of('0') != std::string::npos))
		return SetError(reply, 400);

	if (req.target.substr(0, req.target.find_first_of("?#")) == "/ws")
		return RespondUpgrade(req, hosts, clientsFull, reply);

	std::string path;
	switch (TargetToPath(req.target, &path)) {
	case kPathMalformed: return SetError(reply, 400);
	case kPathNotFound: return SetError(reply, 404);
	case kPathOk: break;
	}

	StaticFile file;
	std::string headers;
	if (!options.webRoot.empty()) {
		if (!ReadRootFile(options.webRoot, path, &file.body))
			return SetError(reply, 404);
	} else {
		StaticProvider provider = gStaticProvider.load();
		if (!provider || !provider(path, AcceptsGzip(req.GetList("accept-encoding")), &file))
			return SetError(reply, 404);
		headers += "Vary: Accept-Encoding\r\n";
		if (file.gzip)
			headers += "Content-Encoding: gzip\r\n";
	}
	// Bundles name everything under assets/ by content hash; the page that refers to them must be fresh.
	if (path.compare(0, 7, "assets/") == 0)
		headers += "Cache-Control: max-age=31536000, immutable\r\n";
	else
		headers += "Cache-Control: no-cache\r\n";
	SetReply(reply, 200, MimeType(path), std::move(file.body), headers);
}

//
// WebSocket framing (RFC 6455)
//

enum {
	kOpContinuation = 0,
	kOpText = 1,
	kOpBinary = 2,
	kOpClose = 8,
	kOpPing = 9,
	kOpPong = 10,
};

enum {
	kCloseGoingAway = 1001,
	kCloseProtocol = 1002,
	kCloseNoStatus = 1005,
	kCloseBadData = 1007,
	kCloseTooBig = 1009,
};

// Header of a server frame: FIN set, never masked.
std::string FrameHeader(int opcode, size_t length) {
	std::string h;
	h += (char)(0x80 | opcode);
	if (length < 126) {
		h += (char)length;
	} else if (length < 65536) {
		h += (char)126;
		h += (char)(length >> 8);
		h += (char)length;
	} else {
		h += (char)127;
		for (int shift = 56; shift >= 0; shift -= 8)
			h += (char)((uint64_t)length >> shift);
	}
	return h;
}

std::string CloseFrame(int code) {
	std::string frame = FrameHeader(kOpClose, code ? 2 : 0);
	if (code) {
		frame += (char)(code >> 8);
		frame += (char)code;
	}
	return frame;
}

bool ValidCloseCode(int code) {
	return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
}

struct WsEvent {
	enum Kind { kNone, kText, kBinary, kPing, kPong, kClose };
	Kind kind = kNone;
	std::string payload;
	int code = 0;	// kClose: the peer's status, kCloseNoStatus when it sent none
};

enum WsResult { kWsNeedMore, kWsOk, kWsFail };

// Turns client frames into messages. It never asks for more input than one frame of at most maxMessage
// payload bytes, which bounds what a connection has to buffer.
class WsDecoder {
public:
	explicit WsDecoder(size_t limit) : maxMessage(limit) {}

	// Takes one frame from the start of data.
	//   kWsNeedMore  the frame is not complete
	//   kWsOk        *used bytes were consumed; *ev tells what the frame completed (kNone: a fragment)
	//   kWsFail      *code is the status to close the connection with
	WsResult Next(const char* data, size_t size, size_t* used, WsEvent* ev, int* code) {
		const unsigned char* p = (const unsigned char*)data;
		if (size < 2)
			return kWsNeedMore;
		bool fin = (p[0] & 0x80) != 0;
		int opcode = p[0] & 0x0f;
		bool control = (opcode & 8) != 0;
		size_t header = 2;
		uint64_t length = p[1] & 0x7f;

		// No extension is negotiated, so the reserved bits stay clear; clients mask every frame.
		if ((p[0] & 0x70) || !(p[1] & 0x80))
			return Fail(code, kCloseProtocol);
		if (opcode != kOpContinuation && opcode != kOpText && opcode != kOpBinary && opcode != kOpClose && opcode != kOpPing && opcode != kOpPong)
			return Fail(code, kCloseProtocol);
		if (control && (!fin || length > 125))
			return Fail(code, kCloseProtocol);

		// The length has one valid encoding: the shortest.
		if (length == 126) {
			if (size < 4)
				return kWsNeedMore;
			length = ((uint64_t)p[2] << 8) | p[3];
			if (length < 126)
				return Fail(code, kCloseProtocol);
			header = 4;
		} else if (length == 127) {
			if (size < 10)
				return kWsNeedMore;
			length = 0;
			for (int i = 2; i < 10; i++)
				length = (length << 8) | p[i];
			if ((length >> 63) || length < 65536)
				return Fail(code, kCloseProtocol);
			header = 10;
		}

		if (!control) {
			if ((opcode == kOpContinuation) != (messageOp != 0))
				return Fail(code, kCloseProtocol);
			if (length > maxMessage - message.size())
				return Fail(code, kCloseTooBig);
		}

		const unsigned char* mask = p + header;
		header += 4;
		if (size < header || size - header < length)
			return kWsNeedMore;
		const char* payload = data + header;
		size_t n = (size_t)length;
		*used = header + n;
		ev->kind = WsEvent::kNone;
		ev->payload.clear();
		ev->code = 0;

		if (control) {
			ev->payload.assign(payload, n);
			Unmask(&ev->payload, 0, mask);
			if (opcode == kOpPing) {
				ev->kind = WsEvent::kPing;
			} else if (opcode == kOpPong) {
				ev->kind = WsEvent::kPong;
			} else {
				ev->kind = WsEvent::kClose;
				ev->code = kCloseNoStatus;
				if (n == 1)
					return Fail(code, kCloseProtocol);
				if (n >= 2) {
					ev->code = ((unsigned char)ev->payload[0] << 8) | (unsigned char)ev->payload[1];
					if (!ValidCloseCode(ev->code))
						return Fail(code, kCloseProtocol);
					if (!ValidUtf8(ev->payload.data() + 2, n - 2))
						return Fail(code, kCloseBadData);
				}
			}
			return kWsOk;
		}

		size_t at = message.size();
		message.append(payload, n);
		Unmask(&message, at, mask);
		if (opcode != kOpContinuation)
			messageOp = opcode;
		if (fin) {
			if (messageOp == kOpText && !ValidUtf8(message.data(), message.size()))
				return Fail(code, kCloseBadData);
			ev->kind = messageOp == kOpText ? WsEvent::kText : WsEvent::kBinary;
			ev->payload.swap(message);
			message.clear();
			messageOp = 0;
		}
		return kWsOk;
	}

private:
	static WsResult Fail(int* code, int value) {
		*code = value;
		return kWsFail;
	}

	static void Unmask(std::string* s, size_t from, const unsigned char* mask) {
		char* d = &(*s)[0];
		for (size_t i = from; i < s->size(); i++)
			d[i] ^= (char)mask[(i - from) & 3];
	}

	size_t maxMessage;
	std::string message;	// payload of the fragmented message so far, never above maxMessage
	int messageOp = 0;		// its opcode, 0 between messages
};

typedef std::chrono::steady_clock Clock;

struct Conn {
	enum State { kHttp, kWs, kClosing };

	Conn(sock_t socket, size_t maxMessage) : fd(socket), decoder(maxMessage) {}

	// network thread only
	sock_t fd;
	State state = kHttp;
	uint32_t id = 0;			// WebSocket client id, 0 before the upgrade
	std::string in;
	WsDecoder decoder;
	std::deque<std::string> out;	// chunks taken from queue, in order
	std::string sending;		// chunk being written, empty between chunks
	size_t sent = 0;
	bool finSent = false;
	bool eof = false;			// kClosing: the peer has closed its side
	bool dead = false;
	Clock::time_point deadline;	// kHttp and kClosing

	// guarded by Server::Impl::mutex
	std::deque<std::string> queue;

	// Changed with the mutex held, except that the network thread lowers queued as it writes; read anywhere.
	std::atomic<size_t> queued{0};		// bytes in queue and out plus the unsent rest of sending
	std::atomic<bool> overflow{false};
};

// Server::Impl::mutex held. Queues a chunk of whole messages for a connection.
//
// A small chunk is appended to the last one instead of getting an entry of its own: an entry costs far more
// memory than a 2 byte pong counts for, and merged chunks go out in one write. Chunks still end where a
// message ends.
//
// bounded: a WebSocket client that does not read is cut off at kMaxQueuedBytes; the network thread sees the
// flag and drops it.
void Push(Conn& c, std::string chunk, bool bounded) {
	if (c.overflow || chunk.empty())
		return;
	if (bounded && c.queued + chunk.size() > kMaxQueuedBytes) {
		c.overflow = true;
		return;
	}
	c.queued += chunk.size();
	if (!c.queue.empty() && c.queue.back().size() + chunk.size() <= kMergeBytes)
		c.queue.back() += chunk;
	else
		c.queue.push_back(std::move(chunk));
}

void SetOption(sock_t s, int level, int name, int value) {
	setsockopt(s, level, name, (const char*)&value, sizeof(value));
}

}	// namespace

void SetStaticProvider(StaticProvider provider) {
	gStaticProvider.store(provider);
}

struct Server::Impl {
	Options options;
	Handler* handler = nullptr;
	std::vector<std::string> hosts;		// lower-cased Host / Origin allow-list

	sock_t listenFd = kNoSocket;
	sock_t wakeFd = kNoSocket;			// UDP socket connected to itself: a datagram interrupts poll
	std::thread thread;
	std::atomic<std::thread::id> loopThread;
	std::atomic<bool> stop{false};
	std::atomic<bool> wakePending{false};
	std::atomic<bool> hasClients{false};
	std::atomic<int> port{0};

	mutable std::mutex mutex;						// guards clients, nextId and the queue of every Conn
	std::unordered_map<uint32_t, Conn*> clients;	// modified by the network thread only
	uint32_t nextId = 1;

	// network thread only
	std::vector<std::unique_ptr<Conn>> conns;
	size_t wsCount = 0;
	Clock::time_point acceptAfter;

	bool Listen() {
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		struct addrinfo* list = nullptr;
		if (options.bind.empty() || getaddrinfo(options.bind.c_str(), std::to_string(options.port).c_str(), &hints, &list) != 0 || !list) {
			fprintf(stderr, "dbg::net: cannot resolve bind address '%s'\n", options.bind.c_str());
			return false;
		}

		const char* step = "socket";
		listenFd = socket(list->ai_family, list->ai_socktype, list->ai_protocol);
		bool ok = listenFd != kNoSocket && SockSetup(listenFd);
		if (ok) {
			SetOption(listenFd, SOL_SOCKET, kListenOption, 1);
			if (list->ai_family == AF_INET6)
				SetOption(listenFd, IPPROTO_IPV6, IPV6_V6ONLY, 1);
			step = "bind";
			ok = ::bind(listenFd, list->ai_addr, (socklen_t)list->ai_addrlen) == 0;
		}
		if (ok) {
			step = "listen";
			ok = listen(listenFd, 16) == 0;
		}
		struct sockaddr_storage bound;
		socklen_t boundSize = sizeof(bound);
		if (ok) {
			step = "getsockname";
			ok = getsockname(listenFd, (struct sockaddr*)&bound, &boundSize) == 0;
		}
		int error = ok ? 0 : SockError();
		freeaddrinfo(list);
		if (!ok) {
			fprintf(stderr, "dbg::net: %s failed for %s port %d (error %d)\n", step, options.bind.c_str(), options.port, error);
			return false;
		}
		if (bound.ss_family == AF_INET6)
			port = ntohs(((struct sockaddr_in6*)&bound)->sin6_port);
		else
			port = ntohs(((struct sockaddr_in*)&bound)->sin_port);
		return true;
	}

	bool OpenWake() {
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		socklen_t size = sizeof(addr);
		wakeFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		bool ok = wakeFd != kNoSocket && SockSetup(wakeFd)
			&& ::bind(wakeFd, (struct sockaddr*)&addr, sizeof(addr)) == 0
			&& getsockname(wakeFd, (struct sockaddr*)&addr, &size) == 0
			&& connect(wakeFd, (struct sockaddr*)&addr, sizeof(addr)) == 0;
		if (!ok)
			fprintf(stderr, "dbg::net: cannot create the wake-up socket (error %d)\n", SockError());
		return ok;
	}

	void CloseSockets() {
		if (listenFd != kNoSocket)
			SockClose(listenFd);
		if (wakeFd != kNoSocket)
			SockClose(wakeFd);
		listenFd = wakeFd = kNoSocket;
		port = 0;
	}

	// Makes the network thread run its loop once more. One datagram is enough for any number of callers
	// until the thread has picked it up. Nothing depends on the datagram arriving (a local firewall may
	// eat it): the loop never sleeps longer than kIdlePollMs.
	void Wake() {
		if (wakePending.exchange(true))
			return;
		char byte = 0;
		if (send(wakeFd, &byte, 1, kSendFlags) != 1)
			wakePending = false;
	}

	// The wake-up is sent with the mutex held: a client found in the map means the network thread has
	// not finished, so wakeFd is still open.
	void Send(uint32_t client, int opcode, std::string payload) {
		payload.insert(0, FrameHeader(opcode, payload.size()));
		std::lock_guard<std::mutex> lock(mutex);
		auto it = clients.find(client);
		if (it == clients.end())
			return;
		Push(*it->second, std::move(payload), true);
		Wake();
	}

	//
	// Everything below runs on the network thread.
	//

	void Enqueue(Conn& c, std::string chunk, bool bounded) {
		std::lock_guard<std::mutex> lock(mutex);
		Push(c, std::move(chunk), bounded);
	}

	void Unregister(Conn& c) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			clients.erase(c.id);
		}
		wsCount--;
		hasClients = wsCount != 0;
		handler->OnClose(c.id);
	}

	// The connection is gone as far as the handler is concerned; its socket is closed when the loop reaps it.
	void Drop(Conn& c) {
		if (c.state == Conn::kWs)
			Unregister(c);
		c.state = Conn::kClosing;
		c.dead = true;
	}

	// Orderly end: what is queued still goes out, then FIN, then the peer gets kLingerMs to close its side.
	// Closing at once could reset the connection under a response the peer has not read yet.
	void BeginClose(Conn& c) {
		if (c.state == Conn::kWs)
			Unregister(c);
		c.state = Conn::kClosing;
		std::string().swap(c.in);
		c.deadline = Clock::now() + std::chrono::milliseconds(kStallMs);
	}

	void CloseWs(Conn& c, int code) {
		Enqueue(c, CloseFrame(code), true);
		BeginClose(c);
	}

	void ProcessHttp(Conn& c) {
		HttpRequest req;
		size_t used = 0;
		int rc = ParseHttpRequest(c.in.data(), c.in.size(), &req, &used);
		if (rc == 0)
			return;
		Reply reply;
		if (rc == 1)
			Respond(req, options, hosts, wsCount >= (size_t)std::max(options.maxClients, 0), &reply);
		else
			SetError(&reply, rc);
		Enqueue(c, std::move(reply.head), false);
		Enqueue(c, std::move(reply.body), false);
		if (!reply.upgrade) {
			BeginClose(c);
			return;
		}

		c.in.erase(0, used);
		c.state = Conn::kWs;
		{
			std::lock_guard<std::mutex> lock(mutex);
			c.id = nextId++;
			clients[c.id] = &c;
		}
		wsCount++;
		hasClients = true;
		handler->OnOpen(c.id);
		ProcessWs(c);
	}

	void ProcessWs(Conn& c) {
		size_t pos = 0;
		while (c.state == Conn::kWs) {
			WsEvent ev;
			size_t used = 0;
			int code = 0;
			WsResult result = c.decoder.Next(c.in.data() + pos, c.in.size() - pos, &used, &ev, &code);
			if (result == kWsNeedMore)
				break;
			if (result == kWsFail) {
				CloseWs(c, code);
				break;
			}
			pos += used;
			switch (ev.kind) {
			case WsEvent::kText:
				handler->OnText(c.id, ev.payload);
				break;
			case WsEvent::kBinary:
				handler->OnBinary(c.id, ev.payload);
				break;
			case WsEvent::kPing:
				Enqueue(c, FrameHeader(kOpPong, ev.payload.size()) + ev.payload, true);
				break;
			case WsEvent::kClose:
				CloseWs(c, ev.code == kCloseNoStatus ? 0 : ev.code);
				break;
			case WsEvent::kPong:
			case WsEvent::kNone:
				break;
			}
		}
		if (c.state == Conn::kWs)
			c.in.erase(0, pos);
	}

	void Receive(Conn& c) {
		char buffer[65536];
		// Bounded rounds: one busy connection must not keep the loop from the others.
		for (int round = 0; round < 16 && !c.dead; round++) {
			int n = (int)recv(c.fd, buffer, (int)sizeof(buffer), 0);
			if (n > 0) {
				if (c.state == Conn::kHttp) {
					c.in.append(buffer, (size_t)n);
					ProcessHttp(c);
				} else if (c.state == Conn::kWs) {
					c.in.append(buffer, (size_t)n);
					ProcessWs(c);
				}
				// kClosing: input is discarded, only the end of the stream matters
				if (n < (int)sizeof(buffer))
					break;
			} else if (n == 0 && c.state == Conn::kClosing) {
				c.eof = true;	// what is queued can still be delivered
				break;
			} else {
				if (n == 0 || !SockWouldBlock(SockError()))
					Drop(c);
				break;
			}
		}
	}

	// Writes at most kFlushBytes and leaves the rest to the next pass: a client that is fed as fast as it
	// reads must not keep the loop from the other connections, from its own input and from the stop flag.
	// POLLOUT is asked for while anything is queued, so that pass follows at once. The queue is taken over
	// whole, one lock per batch instead of one per chunk: a thread that sends without a pause holds the
	// mutex most of the time, and while the loop waits for it nobody is served.
	void Flush(Conn& c) {
		size_t budget = kFlushBytes;
		while (budget != 0) {
			if (c.sending.empty()) {
				if (c.out.empty()) {
					std::lock_guard<std::mutex> lock(mutex);
					c.out.swap(c.queue);
				}
				if (c.out.empty())
					return;
				c.sending = std::move(c.out.front());
				c.out.pop_front();
			}
			size_t want = std::min(c.sending.size() - c.sent, budget);
			int n = (int)send(c.fd, c.sending.data() + c.sent, (int)want, kSendFlags);
			if (n <= 0) {
				if (n == 0 || !SockWouldBlock(SockError()))
					Drop(c);
				return;
			}
			c.sent += (size_t)n;
			budget -= (size_t)n;
			c.queued -= (size_t)n;
			if (c.sent == c.sending.size()) {
				c.sent = 0;
				std::string().swap(c.sending);
			}
			if (c.state == Conn::kClosing)
				c.deadline = Clock::now() + std::chrono::milliseconds(kStallMs);
		}
	}

	// Output, close progress and timeouts of one connection.
	void Service(Conn& c) {
		if (c.dead)
			return;
		if (c.overflow) {
			Drop(c);
			return;
		}
		if (c.queued != 0)
			Flush(c);
		if (c.dead || c.state == Conn::kWs)
			return;

		Clock::time_point now = Clock::now();
		if (c.state == Conn::kClosing && c.queued == 0) {
			if (c.eof) {
				c.dead = true;
				return;
			}
			if (!c.finSent) {
				shutdown(c.fd, kShutdownSend);
				c.finSent = true;
				c.deadline = now + std::chrono::milliseconds(kLingerMs);
			}
		}
		if (now >= c.deadline)
			c.dead = true;
	}

	bool CanAccept() const {
		return conns.size() < (size_t)std::max(options.maxClients, 0) + kMaxHttpConnections;
	}

	void Accept() {
		while (CanAccept()) {
			sock_t fd = accept(listenFd, nullptr, nullptr);
			if (fd == kNoSocket) {
				// Out of descriptors leaves the listener readable; polling it again at once would spin.
				if (!SockWouldBlock(SockError()))
					acceptAfter = Clock::now() + std::chrono::milliseconds(kAcceptRetryMs);
				return;
			}
			if (!SockSetup(fd)) {
				SockClose(fd);
				continue;
			}
			SetOption(fd, IPPROTO_TCP, TCP_NODELAY, 1);
			conns.push_back(std::make_unique<Conn>(fd, options.maxMessage));
			conns.back()->deadline = Clock::now() + std::chrono::milliseconds(kStallMs);
		}
	}

	void Run() {
		loopThread = std::this_thread::get_id();
		std::vector<struct pollfd> fds;
		while (!stop) {
			Clock::time_point now = Clock::now();
			Clock::time_point wakeAt = Clock::time_point::max();
			size_t count = conns.size();
			bool accepting = CanAccept();
			if (accepting && now < acceptAfter) {
				accepting = false;
				wakeAt = acceptAfter;
			}

			fds.clear();
			struct pollfd entry;
			entry.revents = 0;
			entry.fd = wakeFd;
			entry.events = POLLIN;
			fds.push_back(entry);
			for (const auto& c : conns) {
				entry.fd = c->fd;
				entry.events = c->eof ? 0 : POLLIN;
				if (c->queued != 0)
					entry.events |= POLLOUT;
				fds.push_back(entry);
				if (c->state != Conn::kWs)
					wakeAt = std::min(wakeAt, c->deadline);
			}
			if (accepting) {
				entry.fd = listenFd;
				entry.events = POLLIN;
				fds.push_back(entry);
			}

			int timeout = kIdlePollMs;
			if (wakeAt != Clock::time_point::max()) {
				auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(wakeAt - now).count();
				timeout = ms < 0 ? 0 : (int)std::min<long long>(ms + 1, kIdlePollMs);
			}
			if (SockPoll(fds.data(), fds.size(), timeout) < 0) {
				if (!SockInterrupted(SockError()))
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				continue;
			}

			if (fds[0].revents) {
				char drain[64];
				while (recv(wakeFd, drain, (int)sizeof(drain), 0) > 0) {
				}
			}
			// The flag falls only after the socket is empty. Cleared first, a Wake could set it again and
			// have its datagram taken by the drain: flag set, nothing to read, and no later Wake would
			// send. What was queued before this point is seen by the pass below; clearing on every pass
			// also covers a datagram that never arrived.
			wakePending = false;
			// Connections accepted during this pass sit behind `count` and wait for the next poll.
			for (size_t i = 0; i < count; i++) {
				if (fds[i + 1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))
					Receive(*conns[i]);
			}
			if (accepting && fds.back().revents)
				Accept();
			for (size_t i = 0; i < conns.size(); i++)
				Service(*conns[i]);

			for (size_t i = 0; i < conns.size();) {
				if (conns[i]->dead) {
					SockClose(conns[i]->fd);
					conns.erase(conns.begin() + (std::ptrdiff_t)i);
				} else {
					i++;
				}
			}
		}

		for (const auto& c : conns) {
			if (c->state == Conn::kWs) {
				// Best effort, and only on a frame boundary: a close frame in the middle of another frame
				// would corrupt the stream.
				if (c->sent == c->sending.size()) {
					std::string frame = CloseFrame(kCloseGoingAway);
					send(c->fd, frame.data(), (int)frame.size(), kSendFlags);
				}
				Unregister(*c);
			}
			SockClose(c->fd);
		}
		conns.clear();
	}
};

Server::Server() : impl(new Impl) {
}

Server::~Server() {
	Stop();
	delete impl;
}

bool Server::Start(const Options& options, Handler* handler) {
	Impl& s = *impl;
	if (s.thread.joinable() || !handler || options.port < 0 || options.port > 65535)
		return false;
	if (!SockStartup()) {
		fprintf(stderr, "dbg::net: socket library startup failed\n");
		return false;
	}

	s.options = options;
	s.handler = handler;
	s.hosts.clear();
	s.hosts.push_back("127.0.0.1");
	s.hosts.push_back("localhost");
	s.hosts.push_back("[::1]");
	for (const std::string& host : options.allowedHosts)
		s.hosts.push_back(Lower(host));
	s.stop = false;
	s.wakePending = false;
	s.loopThread = std::thread::id();
	s.acceptAfter = Clock::time_point();

	bool ok = s.Listen() && s.OpenWake();
	if (ok) {
		try {
			s.thread = std::thread(&Impl::Run, &s);
		} catch (const std::system_error&) {
			fprintf(stderr, "dbg::net: cannot start the network thread\n");
			ok = false;
		}
	}
	if (!ok) {
		s.CloseSockets();
		SockCleanup();
	}
	return ok;
}

void Server::Stop() {
	Impl& s = *impl;
	if (!s.thread.joinable())
		return;
	s.stop = true;
	// From a handler the thread cannot join itself; the loop ends and a later Stop finishes the job.
	if (std::this_thread::get_id() == s.loopThread.load())
		return;
	s.Wake();
	s.thread.join();
	s.CloseSockets();
	SockCleanup();
}

int Server::Port() const {
	return impl->port;
}

bool Server::HasClients() const {
	return impl->hasClients.load(std::memory_order_relaxed);
}

void Server::SendText(uint32_t client, std::string text) {
	impl->Send(client, kOpText, std::move(text));
}

void Server::SendBinary(uint32_t client, std::string bytes) {
	impl->Send(client, kOpBinary, std::move(bytes));
}

void Server::BroadcastText(const std::string& text) {
	Impl& s = *impl;
	std::string frame = FrameHeader(kOpText, text.size()) + text;
	std::lock_guard<std::mutex> lock(s.mutex);
	for (const auto& client : s.clients)
		Push(*client.second, frame, true);
	if (!s.clients.empty())
		s.Wake();
}

size_t Server::QueuedBytes(uint32_t client) const {
	const Impl& s = *impl;
	std::lock_guard<std::mutex> lock(s.mutex);
	auto it = s.clients.find(client);
	return it == s.clients.end() ? 0 : it->second->queued.load();
}

} }
