/*
	Guest memory for the single-step vectors: there is no memory, only the bus activity a vector expects.

	The whole 4GB address space is mapped to one set of memory handlers. Every access the CPU makes has to be the
	next expected one: same kind, same address, same width and, for a write, the same value. Reads return the
	expected value. The first access that does not fit is the error of the vector and so is every expected access
	that was not made by the end.

	The instruction fetches are either part of that order (an interpreter fetches as it executes), or opcodes are
	served by address and only the data accesses are ordered (a recompiler fetches a whole block when it decodes).
*/
#pragma once
#include "ss_format.h"

enum SsBusKind
{
	SSB_FETCH,
	SSB_READ,
	SSB_WRITE,
};

struct SsBusAccess
{
	u8 kind;		//SsBusKind
	u8 size;		//bytes: 1, 2, 4, 8
	u32 addr;
	u64 value;
};

//Registers the handlers and maps them over every page. Needs sh4_env_init()
void ss_mem_install();

//Starts a vector. read_size / write_size are the widths in bytes of its data accesses (the file's policy).
//With ordered_fetches the expected order is F0 F1 [R] [W] F2 F3, without it [R] [W] and fetches are not expected
void ss_mem_begin(const SsTest& test, u32 read_size, u32 write_size, bool ordered_fetches);

//While on, 16 bit reads return the test's opcode for that address and are not checked. For block decoders
void ss_mem_serve_opcodes(bool on);

//The opcode the test serves at addr
u32 ss_mem_opcode_at(u32 addr);

//Ends the vector: every expected access must have been made. Returns false if the vector has an error
bool ss_mem_end();

//The first error of the vector, an empty string if there is none
const char* ss_mem_error();

//Fetches consumed from the expected order so far
u32 ss_mem_fetches();

//What was expected and what the CPU did, for reports. A 64 bit access shows up as two 32 bit ones in the log
const std::vector<SsBusAccess>& ss_mem_expected();
const std::vector<SsBusAccess>& ss_mem_log();
