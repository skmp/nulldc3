/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include <cstddef>
#include <cstdint>

/*
	dbg::symbols: the symbols of the ELF that reios loaded (dbg_symbols.cpp). It reads the .symtab of the
	image itself and links without the emulator; this header uses <cstdint> names for that reason.

	There is one table for the SH4. Addresses are compared by their low 29 bits, as SH4 code addresses are in
	builds without MMU. The table has no lock: OnElfLoaded() is called on (E), everything else on (E) or on a
	thread that holds L while no emulator thread exists (inside calls and the listener; Clear() when the
	machine is destroyed). Without an ELF there are no symbols.
*/

namespace dbg { namespace symbols {

// A symbol without a size reaches to the next symbol, but no further than this many bytes.
const uint32_t kMaxSymbolReach = 0x10000;

// image / size: an ELF file as read into memory, valid during the call only. Replaces the table with the
// named, defined symbols of its .symtab (functions, objects and symbols without a type; no section and file
// symbols). An image that is not a 32-bit little-endian ELF or has no .symtab leaves the table empty, and so
// does size 0, with which image may be null (a boot that loads no ELF).
// Every offset of the image is checked against size: a damaged image cannot make it read outside.
void OnElfLoaded(const void* image, size_t size);

// Empties the table. On (E), or on a thread that holds L while no emulator thread exists; dbg::emu calls it
// in OnMachineDestroying().
void Clear();

// Number of symbols in the table.
size_t Count();

// The symbol addr lies in: the one with the highest address not above addr, provided addr is below its
// end (address + size; for a symbol without a size the next symbol's address, at most kMaxSymbolReach
// further). *name is its name, valid until the next OnElfLoaded() or Clear(); *offset is addr minus its
// address. Returns false, leaving both alone, when addr lies in no symbol.
bool Lookup(uint32_t addr, const char** name, uint32_t* offset);

// The address of the symbol with exactly this name, as the ELF has it. Returns false when there is none.
bool Find(const char* name, uint32_t* addr);

// The text form of an address: "name" at the start of a symbol, "name+0x<offset in lower-case hex>" inside
// it, NUL-terminated and cut to n bytes. Returns false, with out[0] = 0 (when n > 0), when addr lies in no
// symbol.
bool Format(uint32_t addr, char* out, size_t n);

} }
