/*
	This file is part of libswirl
*/
#include "license/bsd"

#include "pvr_coredump.h"

#include <mutex>
#include <vector>

#include "deps/zlib/zlib.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/pvr/pvr_regs.h"

namespace {

std::mutex pending_lock;
string pending;                 // the path of the dump asked for; empty: none

// One gzip member of data appended to f
bool write_member(FILE* f, const u8* data, size_t size)
{
	z_stream z;
	memset(&z, 0, sizeof(z));
	if (deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return false;

	std::vector<u8> out(1 << 16);
	z.next_in = (Bytef*)data;
	z.avail_in = (uInt)size;
	int status;
	do {
		z.next_out = out.data();
		z.avail_out = (uInt)out.size();
		status = deflate(&z, Z_FINISH);
		size_t made = out.size() - z.avail_out;
		if (status == Z_STREAM_ERROR || fwrite(out.data(), 1, made, f) != made) {
			deflateEnd(&z);
			return false;
		}
	} while (status != Z_STREAM_END);

	deflateEnd(&z);
	return true;
}

}

bool pvr_coredump_write(const string& path, u8* vram)
{
	// the 32 bit area's order, as the refsw2 dump key writes vram.bin
	std::vector<u8> linear(VRAM_SIZE);
	for (u32 i = 0; i < VRAM_SIZE; i += 4)
		memcpy(&linear[i], vrp(vram, i), 4);

	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return false;

	static const char cookie[] = "pvrcoredump";
	bool ok = fwrite(cookie, 1, sizeof(cookie) - 1, f) == sizeof(cookie) - 1 &&
		write_member(f, linear.data(), linear.size()) &&
		write_member(f, pvr_regs, pvr_RegSize);

	ok = fclose(f) == 0 && ok;
	if (!ok)
		remove(path.c_str());
	return ok;
}

void pvr_coredump_request(const string& path)
{
	std::lock_guard<std::mutex> hold(pending_lock);
	pending = path;
}

void pvr_coredump_poll(u8* vram, bool rtt)
{
	string path;
	{
		std::lock_guard<std::mutex> hold(pending_lock);
		if (pending.empty() || rtt)
			return;
		path.swap(pending);
	}

	if (pvr_coredump_write(path, vram))
		printf("pvrcoredump: %s\n", path.c_str());
	else
		printf("pvrcoredump: cannot write %s\n", path.c_str());
}
