/*
	This file is part of libswirl
	BSD 3-clause, see license/bsd
*/

#include "dbg_webui.h"

#include "dbg_net.h"

#include "nulldc_version.h"

#include "deps/zlib/zlib.h"

#include <cstring>

namespace dbg { namespace webui {

bool Embedded() {
	return kFiles[0].path != nullptr;
}

const char* Version() {
	return Embedded() ? kVersion : "none";
}

const char* GitRev() {
	return kGitRev;
}

const File* Files() {
	return kFiles;
}

size_t FileCount() {
	size_t count = 0;
	while (kFiles[count].path)
		count++;
	return count;
}

const File* Find(const std::string& path) {
	for (const File* file = kFiles; file->path; file++) {
		if (path == file->path)
			return file;
	}
	return nullptr;
}

bool Inflate(const File& file, std::string* out) {
	z_stream z;
	memset(&z, 0, sizeof(z));
	if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK)	// 16: the stream has a gzip header
		return false;
	out->resize(file.rawSize);
	z.next_in = (Bytef*)file.data;
	z.avail_in = (uInt)file.size;
	z.next_out = (Bytef*)&(*out)[0];
	z.avail_out = (uInt)out->size();
	bool ok = inflate(&z, Z_FINISH) == Z_STREAM_END && z.total_out == file.rawSize;
	inflateEnd(&z);
	return ok;
}

const std::string& BuiltinPage() {
	static const std::string page =
		"<!doctype html>\n"
		"<html lang=\"en\">\n"
		"<head><meta charset=\"utf-8\"><title>nullDC " + std::string(nulldc_version()) + " debugger</title></head>\n"
		"<body>\n"
		"<h1>nullDC " + std::string(nulldc_version()) + " debugger</h1>\n"
		"<p>The debugger server is running, but this build of the emulator has no Dreamcast DevTools embedded.</p>\n"
		"<p>To get Dreamcast DevTools, build it in <code>devtools/</code> of the emulator tree (the submodule\n"
		"dreamcast-devtools: <code>git submodule update --init devtools</code>) with\n"
		"<code>npm ci &amp;&amp; npm run build:native</code>, which writes <code>dist-native/</code>. Then either</p>\n"
		"<ul>\n"
		"<li>build the emulator again: it embeds <code>devtools/dist-native</code> when it is there, or one named with\n"
		"<code>-DNULLDC_DEVTOOLS_DIST=/path/to/dist-native</code>, or</li>\n"
		"<li>start the emulator with <code>--config Debugger:WebRoot=/path/to/dist-native</code> to serve it from disk.</li>\n"
		"</ul>\n"
		"<p>The debugger itself (protocol " + std::to_string(kProtocolVersion) + ") is a WebSocket at <code>/ws</code> on this address.</p>\n"
		"</body>\n"
		"</html>\n";
	return page;
}

namespace {

// Browsers take the stored gzip stream as it is. Anything that does not announce gzip (curl, scripts)
// gets the file inflated rather than a 406.
bool Provide(const std::string& path, bool acceptGzip, net::StaticFile* out) {
	if (!Embedded()) {
		if (path != "index.html")
			return false;
		out->body = BuiltinPage();
		out->gzip = false;
		return true;
	}
	const File* file = Find(path);
	if (!file)
		return false;
	out->gzip = acceptGzip;
	if (!acceptGzip)
		return Inflate(*file, &out->body);
	out->body.assign((const char*)file->data, file->size);
	return true;
}

const struct Install {
	Install() { net::SetStaticProvider(&Provide); }
} install;

}

} }
