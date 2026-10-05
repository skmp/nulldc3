// Minimal checks for the dbg_net tests: a failed CHECK is printed and counted, Finish() turns the count
// into the exit code ctest looks at.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

static int gChecks = 0;
static int gFailures = 0;

static std::string Printable(const std::string& s) {
	std::string out;
	for (size_t i = 0; i < s.size() && i < 200; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c >= 0x20 && c < 0x7f && c != '\\') {
			out += (char)c;
		} else {
			char hex[8];
			snprintf(hex, sizeof(hex), "\\x%02x", c);
			out += hex;
		}
	}
	if (s.size() > 200)
		out += "... (" + std::to_string(s.size()) + " bytes)";
	return out;
}

template <class T>
static std::string Shown(const T& value) {
	std::ostringstream os;
	os << value;
	return Printable(os.str());
}

#define CHECK(cond) \
	do { \
		gChecks++; \
		if (!(cond)) { \
			gFailures++; \
			printf("%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ(a, b) \
	do { \
		gChecks++; \
		const auto& va_ = (a); \
		const auto& vb_ = (b); \
		if (!(va_ == vb_)) { \
			gFailures++; \
			printf("%s:%d: CHECK_EQ(%s, %s) failed: '%s' vs '%s'\n", __FILE__, __LINE__, #a, #b, Shown(va_).c_str(), Shown(vb_).c_str()); \
		} \
	} while (0)

// For conditions the rest of a test depends on.
#define REQUIRE(cond) \
	do { \
		gChecks++; \
		if (!(cond)) { \
			printf("%s:%d: REQUIRE(%s) failed, giving up\n", __FILE__, __LINE__, #cond); \
			fflush(stdout); \
			exit(1); \
		} \
	} while (0)

static int Finish(const char* name) {
	printf("%s: %d checks, %d failed\n", name, gChecks, gFailures);
	fflush(stdout);
	return gFailures ? 1 : 0;
}
