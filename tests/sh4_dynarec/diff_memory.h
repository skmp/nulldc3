/*
	Guest memory for the differential: one memory that two engines access in their own ways.

	What a read returns depends on the address and on what is read from it, never on the order or the number
	of accesses, so the interpreter and the dynarec find the same memory.
	  code    the opcodes of the vector (ss_format.h), by address
	  data    the value the vector records for its data read, at that address and with that width. Anywhere
	          else a value made of the address
	Writes are logged and do not change what is read afterwards.

	The data accesses of a run are logged in the order they are made, the instruction fetches as the addresses
	they are made from.
*/
#pragma once
#include "ss_format.h"
#include "ss_memory.h"

//Registers the handlers and maps them over every page. Needs sh4_env_init()
void diff_mem_install();

//Starts a vector. read_size is the width in bytes of its data read (the file's policy)
void diff_mem_begin(const SsTest& test, u32 read_size);

//Starts the run of one engine on the vector, with empty logs. Both engines fetch with 16 bit reads through
//the same handlers their data reads go to. The interpreter fetches the address PC has just moved on from
//(pc_fetches). A block decoder fetches while diff_mem_serve_opcodes is on, and those fetches are not logged
void diff_mem_start(bool pc_fetches);

void diff_mem_serve_opcodes(bool on);

//The opcode the vector has at addr
u32 diff_mem_opcode_at(u32 addr);

//The data accesses of the run. A 64 bit access shows up as two 32 bit ones
const std::vector<SsBusAccess>& diff_mem_log();

//The addresses of the instruction fetches of the run
const std::vector<u32>& diff_mem_fetches();
