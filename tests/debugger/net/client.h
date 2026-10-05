// A blocking TCP client with just enough HTTP and WebSocket to talk to dbg::net::Server from a test.
// It is written against the RFCs, not against dbg_net.cpp, so both ends are checked by the other.

#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET client_sock_t;
static const client_sock_t kClientNoSocket = INVALID_SOCKET;
static void ClientSockClose(client_sock_t s) { closesocket(s); }
static int ClientSockPoll(struct pollfd* fd, int timeoutMs) { return WSAPoll(fd, 1, timeoutMs); }
static const int kClientSendFlags = 0;
static const int kClientShutdownSend = SD_SEND;
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
typedef int client_sock_t;
static const client_sock_t kClientNoSocket = -1;
static void ClientSockClose(client_sock_t s) { close(s); }
static int ClientSockPoll(struct pollfd* fd, int timeoutMs) { return poll(fd, 1, timeoutMs); }
#ifdef MSG_NOSIGNAL
static const int kClientSendFlags = MSG_NOSIGNAL;
#else
static const int kClientSendFlags = 0;
#endif
static const int kClientShutdownSend = SHUT_WR;
#endif

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

struct WsFrame {
	bool fin = false;
	int opcode = 0;
	bool masked = false;
	std::string payload;

	int CloseCode() const {
		return payload.size() >= 2 ? (((unsigned char)payload[0] << 8) | (unsigned char)payload[1]) : 0;
	}
};

struct HttpResult {
	int status = 0;		// 0: no parsable response
	std::string head;
	std::string body;

	// Value of a response header, "" when absent.
	std::string Header(const std::string& name) const {
		std::string lowerHead, lowerName;
		for (char c : head)
			lowerHead += (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
		for (char c : name)
			lowerName += (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
		size_t at = lowerHead.find("\r\n" + lowerName + ":");
		if (at == std::string::npos)
			return "";
		at += lowerName.size() + 3;
		size_t end = head.find("\r\n", at);
		std::string value = head.substr(at, end - at);
		while (!value.empty() && value[0] == ' ')
			value.erase(0, 1);
		return value;
	}
};

class TestClient {
public:
	TestClient() {
#ifdef _WIN32
		WSADATA data;
		WSAStartup(MAKEWORD(2, 2), &data);
#endif
	}
	~TestClient() { Close(); }

	// receiveBuffer, when not 0, is the size of the socket's receive buffer: a small one is all the server
	// can fill while this side does not read.
	bool Connect(int port, int receiveBuffer = 0) {
		Close();
		fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (fd == kClientNoSocket)
			return false;
		// before the connection is made, the window is negotiated then
		if (receiveBuffer != 0)
			setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&receiveBuffer, sizeof(receiveBuffer));
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons((unsigned short)port);
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
			Close();
			return false;
		}
		int on = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
		buffer.clear();
		return true;
	}

	bool Connected() const { return fd != kClientNoSocket; }

	void Close() {
		if (fd != kClientNoSocket)
			ClientSockClose(fd);
		fd = kClientNoSocket;
	}

	// Half-close: the server sees the end of the stream, this side can still read.
	void ShutdownSend() { shutdown(fd, kClientShutdownSend); }

	bool Send(const std::string& data) {
		size_t sent = 0;
		while (sent < data.size()) {
			size_t want = data.size() - sent;
			if (want > (1u << 20))
				want = 1u << 20;
			int n = (int)send(fd, data.data() + sent, (int)want, kClientSendFlags);
			if (n <= 0)
				return false;
			sent += (size_t)n;
		}
		return true;
	}

	// Appends what arrives to the buffer. False on timeout, end of stream or error; *closed tells the latter two.
	bool Fill(int timeoutMs, bool* closed = nullptr) {
		if (closed)
			*closed = false;
		struct pollfd p;
		p.fd = fd;
		p.events = POLLIN;
		p.revents = 0;
		if (ClientSockPoll(&p, timeoutMs) <= 0)
			return false;
		char chunk[65536];
		int n = (int)recv(fd, chunk, (int)sizeof(chunk), 0);
		if (n <= 0) {
			if (closed)
				*closed = true;
			return false;
		}
		buffer.append(chunk, (size_t)n);
		return true;
	}

	// Everything up to the end of the stream; *timedOut reports a peer that never closed.
	std::string ReadToEnd(int timeoutMs = 5000, bool* timedOut = nullptr) {
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		bool closed = false;
		while (!closed) {
			int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
			if (left <= 0)
				break;
			Fill(left, &closed);
		}
		if (timedOut)
			*timedOut = !closed;
		std::string all;
		all.swap(buffer);
		return all;
	}

	// True when the peer closes the connection within the timeout; pending input is discarded.
	bool WaitClosed(int timeoutMs = 5000) {
		bool timedOut = false;
		ReadToEnd(timeoutMs, &timedOut);
		return !timedOut;
	}

	bool ReadExact(size_t n, std::string* out, int timeoutMs = 5000) {
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		while (buffer.size() < n) {
			int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
			if (left <= 0 || !Fill(left))
				return false;
		}
		out->assign(buffer, 0, n);
		buffer.erase(0, n);
		return true;
	}

	// Reads a response head, through the empty line.
	bool ReadHead(std::string* head, int timeoutMs = 5000) {
		auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
		size_t end;
		while ((end = buffer.find("\r\n\r\n")) == std::string::npos) {
			int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
			if (left <= 0 || !Fill(left))
				return false;
		}
		head->assign(buffer, 0, end + 4);
		buffer.erase(0, end + 4);
		return true;
	}

	// One request on a fresh connection; the server closes after every plain response.
	static HttpResult Request(int port, const std::string& request, int timeoutMs = 5000) {
		HttpResult result;
		TestClient client;
		if (!client.Connect(port) || !client.Send(request))
			return result;
		std::string all = client.ReadToEnd(timeoutMs);
		size_t end = all.find("\r\n\r\n");
		if (end == std::string::npos || all.compare(0, 9, "HTTP/1.1 ") != 0)
			return result;
		result.status = atoi(all.c_str() + 9);
		result.head = all.substr(0, end + 4);
		result.body = all.substr(end + 4);
		return result;
	}

	static HttpResult Get(int port, const std::string& target, const std::string& headers = "") {
		return Request(port, "GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n" + headers + "\r\n");
	}

	// Connects and asks for the WebSocket at /ws. Returns the HTTP status; 101 leaves the connection open
	// for frames. `headers` are extra request header lines, each ending in CRLF.
	int Upgrade(int port, const std::string& headers = "", std::string* head = nullptr) {
		if (!Connect(port))
			return 0;
		std::string request = "GET /ws HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) + "\r\n"
			"Upgrade: websocket\r\nConnection: Upgrade\r\n"
			"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n" + headers + "\r\n";
		std::string response;
		if (!Send(request) || !ReadHead(&response) || response.compare(0, 9, "HTTP/1.1 ") != 0)
			return 0;
		if (head)
			*head = response;
		return atoi(response.c_str() + 9);
	}

	// A client frame as it goes on the wire. `lengthBytes` forces the 16-bit (2) or 64-bit (8) length
	// form for payloads that do not need it.
	static std::string Frame(int opcode, const std::string& payload, bool fin = true, bool masked = true, int lengthBytes = 0, int reserved = 0) {
		static uint32_t seed = 0x1234abcd;
		std::string f;
		f += (char)((fin ? 0x80 : 0) | (reserved << 4) | opcode);
		size_t n = payload.size();
		int maskBit = masked ? 0x80 : 0;
		if (lengthBytes == 0)
			lengthBytes = n < 126 ? 0 : n < 65536 ? 2 : 8;
		if (lengthBytes == 0) {
			f += (char)(maskBit | (int)n);
		} else if (lengthBytes == 2) {
			f += (char)(maskBit | 126);
			f += (char)(n >> 8);
			f += (char)n;
		} else {
			f += (char)(maskBit | 127);
			for (int shift = 56; shift >= 0; shift -= 8)
				f += (char)((uint64_t)n >> shift);
		}
		if (!masked)
			return f + payload;
		unsigned char mask[4];
		for (unsigned char& m : mask) {
			seed = seed * 1664525u + 1013904223u;
			m = (unsigned char)(seed >> 24);
		}
		f.append((const char*)mask, 4);
		size_t at = f.size();
		f += payload;
		for (size_t i = 0; i < n; i++)
			f[at + i] = (char)(f[at + i] ^ mask[i & 3]);
		return f;
	}

	bool SendFrame(int opcode, const std::string& payload, bool fin = true, bool masked = true) {
		return Send(Frame(opcode, payload, fin, masked));
	}

	bool ReadFrame(WsFrame* frame, int timeoutMs = 5000) {
		std::string h;
		if (!ReadExact(2, &h, timeoutMs))
			return false;
		frame->fin = (h[0] & 0x80) != 0;
		frame->opcode = h[0] & 0x0f;
		frame->masked = (h[1] & 0x80) != 0;
		uint64_t length = h[1] & 0x7f;
		if (length == 126) {
			if (!ReadExact(2, &h, timeoutMs))
				return false;
			length = ((uint64_t)(unsigned char)h[0] << 8) | (unsigned char)h[1];
		} else if (length == 127) {
			if (!ReadExact(8, &h, timeoutMs))
				return false;
			length = 0;
			for (int i = 0; i < 8; i++)
				length = (length << 8) | (unsigned char)h[i];
		}
		std::string mask;
		if (frame->masked && !ReadExact(4, &mask, timeoutMs))
			return false;
		if (!ReadExact((size_t)length, &frame->payload, timeoutMs))
			return false;
		for (size_t i = 0; frame->masked && i < frame->payload.size(); i++)
			frame->payload[i] = (char)(frame->payload[i] ^ mask[i & 3]);
		return true;
	}

	client_sock_t fd = kClientNoSocket;
	std::string buffer;		// received, not yet consumed
};
