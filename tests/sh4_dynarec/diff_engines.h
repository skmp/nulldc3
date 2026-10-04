/*
	The two SH4 engines on one vector, and how what they make of it compares.

	Both run the 4 instructions of a vector from its initial state, on the memory of diff_memory.h. The
	interpreter is run as SH4IInterpreter::Loop runs it, the dynarec through dynarec_runner.h.
*/
#pragma once
#include "ss_format.h"
#include "ss_memory.h"
#include "test_guard.h"

#include <string>
#include <vector>

//How the engines compare on a vector. When more than one applies it is the first in this order
enum DiffKind
{
	DK_IDENTICAL,
	DK_HANG,		//an engine does not finish
	DK_BOTH_CRASH,	//both engines end in the same die()/verify()/signal
	DK_CRASH,		//one engine ends that way and the other runs through, or they end in different ones
	DK_PATH,		//the instructions are run from different addresses
	DK_BUS,			//the data accesses differ
	DK_STATE,		//the final states differ
	DK_NOTIFY,		//the emulator code notifies (msgboxf) and carries on in one engine, not in the other

	DK_COUNT
};

#define DK_FIRST_DIFFERENT DK_CRASH

extern const char* const diff_kind_name[DK_COUNT];
extern const char* const diff_kind_description[DK_COUNT];

//What an engine made of a vector
struct EngineResult
{
	const char* name;
	TestGuardResult guard;
	std::string message;			//how it ended, if it did not run through
	u32 notifications;				//msgboxf calls it carried on from
	std::string notification;		//the last of them
	SsState state;
	std::vector<SsBusAccess> bus;	//data accesses
	std::vector<u32> path;			//addresses the instructions were run from
};

//Once, when the SH4 environment (sh4_env.h) is up: maps the memory and starts the recompiler
void diff_engines_init();

//Run the vector that diff_mem_begin() was called for. The engine runs guarded (test_guard.h)
void diff_run_interpreter(const SsTest& test, EngineResult& result);
void diff_run_dynarec(const SsTest& test, EngineResult& result);

//How the two compare. Everything they differ in goes into what ("state: ...; notify: ..."), the kind is the
//first of it
DiffKind diff_compare(const EngineResult& interpreter, const EngineResult& dynarec, std::string& what);

//The comparison on results that are made to differ in one thing at a time: it has to tell every one of them
bool diff_compare_selfcheck();

//"write32 @8C001000=12345678"
std::string diff_bus_text(const SsBusAccess& access);

//How an engine ended that did not run through: "die (Verify Failed : ...)"
std::string diff_ended_text(const EngineResult& result);

//printf into a string
std::string diff_format(const char* fmt, ...);
