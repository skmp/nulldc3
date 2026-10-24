/*
	icons.x11: x11_icon_cardinals() (libswirl/linux-dist/x11_icon.cpp), the values the X11 window gets as its
	_NET_WM_ICON (libswirl/linux-dist/x11.cpp passes them to XChangeProperty, format 32), without a display.

	    icons_x11

	The property is, for each icon, its width, its height and its pixels row by row, each A << 24 | R << 16 |
	G << 8 | B. Xlib takes format 32 data as C longs: the values must be the header's uint32_t values zero-extended
	(a sign extension would put ones above bit 31 of every pixel with an alpha of 128 or more). The sizes, the count
	and the hash are written here again rather than taken from the header, so that the header is checked against
	them.
*/
#include "linux-dist/x11_icon.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

static int failed = 0;

#define CHECK(condition) \
	do { if (!(condition)) { printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); failed++; } } while (0)

// 64-bit FNV-1a of the values, each as its four bytes, least significant first (what make_icons.py hashes)
static std::uint64_t fnv1a64(const std::vector<unsigned long>& values)
{
	std::uint64_t h = 0xcbf29ce484222325ull;
	for (unsigned long v : values)
	{
		for (int shift = 0; shift < 32; shift += 8)
		{
			h ^= (std::uint64_t)((v >> shift) & 0xff);
			h *= 0x100000001b3ull;
		}
	}
	return h;
}

static unsigned alpha(unsigned long v)
{
	return (unsigned)((v >> 24) & 0xff);
}

int main()
{
	static const unsigned sizes[] = { 16, 24, 32, 48, 64, 128 };
	const std::size_t nsizes = sizeof(sizes) / sizeof(sizes[0]);
	const std::size_t count = 24652;	// 6 * 2 + 16 * 16 + 24 * 24 + 32 * 32 + 48 * 48 + 64 * 64 + 128 * 128

	const std::vector<unsigned long> values = x11_icon_cardinals();

	printf("%zu values (sizeof(unsigned long) = %zu)\n", values.size(), sizeof(unsigned long));
	CHECK(values.size() == count);
	CHECK(x11_icon::kValueCount == count);
	CHECK(x11_icon::kSizeCount == nsizes);

	// Zero-extended: below 2^32, equal to the header's value
	std::size_t wide = 0, unequal = 0;
	for (std::size_t i = 0; i < values.size(); i++)
	{
		if ((unsigned long long)values[i] > 0xffffffffull)
			wide++;
		if (i < x11_icon::kValueCount && (unsigned long long)values[i] != (unsigned long long)x11_icon::kValues[i])
			unequal++;
	}
	printf("%zu values of 2^32 or more, %zu that are not the header's\n", wide, unequal);
	CHECK(wide == 0);
	CHECK(unequal == 0);

	// The hash of the values against the header's constant
	const std::uint64_t hash = fnv1a64(values);
	printf("FNV-1a 64 of the values %016llx, the header says %016llx\n", (unsigned long long)hash,
		(unsigned long long)x11_icon::kFnv1a64);
	CHECK(hash == x11_icon::kFnv1a64);

	// The blocks: width, height, then width * height pixels; the corners transparent (the 16 px art is antialiased
	// into its corners: an alpha of at most 12 there), the centre opaque
	std::size_t pos = 0;
	for (std::size_t n = 0; n < nsizes; n++)
	{
		const unsigned s = sizes[n];
		if (pos + 2 + (std::size_t)s * s > values.size())
		{
			printf("%u px: the block at %zu runs past the end of the values\n", s, pos);
			failed++;
			break;
		}
		CHECK(values[pos] == s);
		CHECK(values[pos + 1] == s);
		CHECK(n < x11_icon::kSizeCount && x11_icon::kSizes[n] == s && x11_icon::kOffsets[n] == pos);
		const unsigned long* px = &values[pos + 2];
		const unsigned corners[4] = { alpha(px[0]), alpha(px[s - 1]), alpha(px[(s - 1) * s]),
			alpha(px[(s - 1) * s + s - 1]) };
		const unsigned limit = s == 16 ? 12 : 0;
		const unsigned h = s / 2;
		const unsigned centre[4] = { alpha(px[(h - 1) * s + h - 1]), alpha(px[(h - 1) * s + h]),
			alpha(px[h * s + h - 1]), alpha(px[h * s + h]) };
		printf("%u px at %zu: width %lu, height %lu, corner alphas %u %u %u %u (at most %u), centre alphas %u %u %u %u\n",
			s, pos, values[pos], values[pos + 1], corners[0], corners[1], corners[2], corners[3], limit,
			centre[0], centre[1], centre[2], centre[3]);
		for (unsigned c = 0; c < 4; c++)
		{
			CHECK(corners[c] <= limit);
			CHECK(centre[c] == 255);
		}
		pos += 2 + (std::size_t)s * s;
	}
	CHECK(pos == values.size());

	if (failed)
	{
		printf("FAILED: %d checks\n", failed);
		return 1;
	}
	printf("ok\n");
	return 0;
}
