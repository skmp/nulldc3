/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_symbols.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/*
	The symbol table of the SH4: the named, defined symbols of the .symtab of the ELF that reios loaded.

	The image is a file that a person named: nothing in it is trusted. Every offset is checked against the
	size of the image before a byte is read, in 64 bits, so that no sum wraps; a name is the bytes up to a
	NUL or to the end of its string table, whichever comes first. Names are kept as the bytes the image has:
	what leaves the process is made printable where it is sent (dbg_core.cpp).

	This file links without the emulator (tests/debugger/symbols).
*/

namespace dbg { namespace symbols {

namespace {

// Addresses are compared by their low 29 bits (debugger.h, DBG_SH4_ADDR_MASK, which this file cannot include)
const uint32_t kAddrMask = 0x1FFFFFFF;

// ELF32
const size_t kEhdrSize = 52;
const size_t kShdrSize = 40;
const size_t kSymSize = 16;

const uint32_t kShtSymtab = 2;
const uint32_t kShtStrtab = 3;

const unsigned kSttNotype = 0;
const unsigned kSttObject = 1;
const unsigned kSttFunc = 2;

const uint16_t kShnUndef = 0;
const uint16_t kShnAbs = 0xFFF1;

struct Symbol {
	uint32_t key;			// the address, masked: what is compared
	uint32_t addr;			// as the ELF has it
	uint32_t size;			// 0: the ELF gives none
	uint32_t end;			// key of the first byte behind it, as Lookup() uses it; can be 0x20000000
	uint8_t rank;			// which of several symbols of one address names it: the lower the better
	uint32_t order;			// position in the .symtab
	std::string name;
};

// Sorted by key, and by rank and order within one key
std::vector<Symbol> table;

uint16_t Get16(const uint8_t* p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

uint32_t Get32(const uint8_t* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// Is [offset, offset + length) inside an image of `size` bytes?
bool Inside(uint64_t offset, uint64_t length, size_t size)
{
	return offset <= size && length <= size - offset;
}

struct Section {
	uint32_t type, offset, size, link, entsize;
};

bool ReadSection(const uint8_t* image, size_t size, uint64_t shoff, uint32_t shentsize, uint32_t index, Section* out)
{
	uint64_t at = shoff + (uint64_t)index * shentsize;

	if (!Inside(at, kShdrSize, size))
		return false;

	const uint8_t* p = image + at;

	out->type = Get32(p + 4);
	out->offset = Get32(p + 16);
	out->size = Get32(p + 20);
	out->link = Get32(p + 24);
	out->entsize = Get32(p + 36);
	return true;
}

void Load(const uint8_t* image, size_t size)
{
	if (image == nullptr || size < kEhdrSize)
		return;

	// 32-bit, little-endian
	if (memcmp(image, "\x7F" "ELF", 4) != 0 || image[4] != 1 || image[5] != 1)
		return;

	const uint32_t shoff = Get32(image + 32);
	const uint32_t shentsize = Get16(image + 46);
	const uint32_t shnum = Get16(image + 48);

	if (shoff == 0 || shnum == 0 || shentsize < kShdrSize)
		return;

	for (uint32_t i = 0; i < shnum; i++)
	{
		Section symtab, strtab;

		if (!ReadSection(image, size, shoff, shentsize, i, &symtab))
			return;

		if (symtab.type != kShtSymtab)
			continue;

		// its names: a string table, inside the image
		if (symtab.link >= shnum || !ReadSection(image, size, shoff, shentsize, symtab.link, &strtab))
			continue;

		if (strtab.type != kShtStrtab || !Inside(strtab.offset, strtab.size, size))
			continue;

		if (!Inside(symtab.offset, symtab.size, size))
			continue;

		const uint32_t entsize = symtab.entsize != 0 ? symtab.entsize : (uint32_t)kSymSize;

		if (entsize < kSymSize)
			continue;

		const uint8_t* names = image + strtab.offset;
		const uint32_t count = symtab.size / entsize;

		for (uint32_t n = 0; n < count; n++)
		{
			const uint8_t* p = image + symtab.offset + (size_t)n * entsize;

			const uint32_t name_offset = Get32(p);
			const uint32_t value = Get32(p + 4);
			const uint32_t sym_size = Get32(p + 8);
			const unsigned info = p[12];
			const unsigned type = info & 15;
			const unsigned bind = info >> 4;
			const uint16_t shndx = Get16(p + 14);

			// named and defined: functions, objects and symbols without a type. No section and file symbols
			if (name_offset == 0 || name_offset >= strtab.size || shndx == kShnUndef)
				continue;

			if (type != kSttNotype && type != kSttObject && type != kSttFunc)
				continue;

			// the bytes up to a NUL, or to the end of the string table
			const uint8_t* text = names + name_offset;
			const size_t room = strtab.size - name_offset;
			const void* nul = memchr(text, 0, room);
			const size_t length = nul != nullptr ? (size_t)((const uint8_t*)nul - text) : room;

			if (length == 0)
				continue;

			Symbol s;

			s.key = value & kAddrMask;
			s.addr = value;
			s.size = sym_size;
			s.end = 0;
			// a function names its address before an object does, that before a label; a global one before
			// a local one; and of two that are the same in both, the symbol of a section before an absolute
			// one. An absolute symbol is a number the linker was given: where a program is linked to start
			// (__executable_start of a KallistiOS program has the address of _start), a size, a constant
			s.rank = (uint8_t)((type == kSttFunc ? 0 : type == kSttObject ? 4 : 8) + (bind != 0 ? 0 : 2) + (shndx == kShnAbs ? 1 : 0));
			s.order = (uint32_t)table.size();
			s.name.assign((const char*)text, length);

			table.push_back(std::move(s));
		}
	}

	std::sort(table.begin(), table.end(), [](const Symbol& a, const Symbol& b) {
		if (a.key != b.key)
			return a.key < b.key;
		if (a.rank != b.rank)
			return a.rank < b.rank;
		return a.order < b.order;
	});

	// Where each one ends. A symbol without a size reaches to the next symbol's address: the next address
	// that is another one. From the back, so that a table of one address costs one pass
	bool have_next = false;
	uint32_t next_key = 0;

	for (size_t i = table.size(); i-- > 0; )
	{
		Symbol& s = table[i];

		if (i + 1 < table.size() && table[i + 1].key != s.key)
		{
			have_next = true;
			next_key = table[i + 1].key;
		}

		uint64_t end;

		if (s.size != 0)
		{
			end = (uint64_t)s.key + s.size;
		}
		else
		{
			end = (uint64_t)s.key + kMaxSymbolReach;

			if (have_next && next_key < end)
				end = next_key;
		}

		s.end = end > (uint64_t)kAddrMask + 1 ? kAddrMask + 1 : (uint32_t)end;
	}
}

}

void OnElfLoaded(const void* image, size_t size)
{
	table.clear();

	Load((const uint8_t*)image, size);
}

void Clear()
{
	table.clear();
	table.shrink_to_fit();
}

size_t Count()
{
	return table.size();
}

bool Lookup(uint32_t addr, const char** name, uint32_t* offset)
{
	const uint32_t key = addr & kAddrMask;

	// the first symbol above the address; those of the highest address not above it are in front of it
	auto above = std::upper_bound(table.begin(), table.end(), key, [](uint32_t k, const Symbol& s) { return k < s.key; });

	if (above == table.begin())
		return false;

	const uint32_t at = (above - 1)->key;
	auto first = std::lower_bound(table.begin(), above, at, [](const Symbol& s, uint32_t k) { return s.key < k; });

	for (auto it = first; it != above; ++it)
	{
		if (key < it->end)
		{
			*name = it->name.c_str();
			*offset = key - it->key;
			return true;
		}
	}

	return false;
}

bool Find(const char* name, uint32_t* addr)
{
	const Symbol* found = nullptr;

	// the first definition of the .symtab
	for (const Symbol& s : table)
	{
		if (s.name == name && (found == nullptr || s.order < found->order))
			found = &s;
	}

	if (found == nullptr)
		return false;

	*addr = found->addr;
	return true;
}

bool Format(uint32_t addr, char* out, size_t n)
{
	const char* name;
	uint32_t offset;

	if (!Lookup(addr, &name, &offset))
	{
		if (n != 0)
			out[0] = 0;
		return false;
	}

	if (n != 0)
	{
		if (offset == 0)
			snprintf(out, n, "%s", name);
		else
			snprintf(out, n, "%s+0x%x", name, offset);
	}

	return true;
}

} }
