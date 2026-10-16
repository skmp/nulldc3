/*
	This file is part of libswirl
	BSD 3-clause, see license/bsd
*/

#pragma once

// The devtools UI served by the debugger: the bundle embedded at build time (dbg_webui_data.cpp, written
// into the build directory by debugger/cmake/dbg_webui_embed.cmake), or a built-in page that tells how to
// get one. Linking dbg_webui.cpp is enough: it registers itself as the static provider of dbg::net.

#include <cstddef>
#include <string>

namespace dbg {

// Version of the debugger wire protocol, reported by debugger.describe. A UI bundle states the version it
// was built for in bundle-info.json; the build reads the number from this line and refuses any other.
const int kProtocolVersion = 3;

namespace webui {

struct File {
	const char* path;			// relative to the bundle root, '/' separated
	const unsigned char* data;	// gzip stream
	size_t size;				// bytes of data
	size_t rawSize;				// bytes once inflated
};

bool Embedded();				// false: only the built-in page is served
const char* Version();			// version of the embedded bundle, "none" without one
const char* GitRev();			// revision the embedded bundle was built from, "" without one

const File* Files();			// FileCount() entries
size_t FileCount();
const File* Find(const std::string& path);
bool Inflate(const File& file, std::string* out);

// The page served for index.html when no bundle is embedded.
const std::string& BuiltinPage();

// Written by the build into dbg_webui_data.cpp. The table ends with an entry whose path is null; the
// strings are empty when nothing is embedded.
extern const File kFiles[];
extern const char kVersion[];
extern const char kGitRev[];

} }
