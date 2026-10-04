/*
	The console jobs of a wren7-rtl checkout (model/tests/hw/<suite>): the job file, the images it loads, what
	the console answered, and a job run on the machine (arm7_machine.h).

	A job is a set of images and patched words in wave RAM. The console releases the ARM reset and waits until
	the guest raises MCIPD.SCPU or the job's timeout is over, then it saves the regions the job reads back.
	Every job loads the done stub. A program ends by jumping to it, exceptions arrive there through their
	vectors; the stub saves the registers into the result block and raises the interrupt. In the suites that are
	run here the result block is the first region a job reads back:
	  +0x00 'DONE'    +0x04 exit: 0 the program jumped to the stub, 1..7 it came through the vector at 4 * exit
	  +0x08 r0..r15   +0x48 CPSR    +0x4C SPSR
	A job with a repeat count is run that often and read back once. The console keeps the ARM registers over a
	reset, so the registers a job does not write hold what the run before left in them.

	The console's side of a job is <out>/<job>.bin (the regions) and its line in <out>/results.txt (status and
	the SH4 cycles each repeat took).

	Nothing of wren7-rtl is in this repository: everything is read from the checkout.
*/
#pragma once
#include "types.h"
#include "test_guard.h"

#include <string>
#include <vector>

#define WREN7_RES_SIZE 0x60
#define WREN7_RES_MAGIC 0x00		//'DONE'
#define WREN7_RES_EXIT 0x04
#define WREN7_RES_R0 0x08			//r0..r15, 4 bytes each
#define WREN7_RES_CPSR 0x48
#define WREN7_RES_SPSR 0x4C

#define WREN7_MAGIC_DONE 0x454E4F44

struct Wren7Image
{
	u32 addr;
	std::string file;		//as the job file has it, relative to model/
	std::vector<u8> data;
};

struct Wren7Word
{
	u32 addr;
	u32 value;
};

struct Wren7Region
{
	u32 addr;
	u32 size;
};

struct Wren7Job
{
	std::string name;
	u32 timeout_ms;
	u32 repeat;
	std::vector<Wren7Image> images;
	std::vector<Wren7Word> words;
	std::vector<Wren7Region> reads;
	bool needs_sh4;							//the SH4 sets up or reads AICA registers, or is on the bus during the run

	std::string console_status;				//"done", "timeout", "stuck"
	std::vector<u64> console_sh4_cycles;	//per repeat: reset release to the interrupt
	std::vector<u8> console;				//the read regions, empty for a job that reads nothing
};

struct Wren7Suite
{
	std::string model_dir;		//<checkout>/model, what the paths of the job file are relative to
	std::string out_dir;		//the console's results, relative to model_dir
	std::vector<Wren7Job> jobs;
};

enum Wren7Load
{
	W7L_OK,
	W7L_MISSING,	//a file is not there: no checkout, or its build outputs are missing
	W7L_BAD,		//a file is there and is not what it should be
};

//Reads model/tests/hw/<suite>/jobs.txt with everything it refers to. error says what is wrong otherwise
Wren7Load wren7_load_suite(const std::string& wren7_dir, const std::string& suite, Wren7Suite& out, std::string& error);

//Size of the regions a job reads back
u32 wren7_read_size(const Wren7Job& job);

struct Wren7Run
{
	TestGuardResult guard;		//not TGR_OK: the emulator code crashed or hung, reported with arm7_problem()
	bool done;					//the guest raised MCIPD.SCPU within the job's instruction budget
	u64 steps;					//instructions of the last repeat
	u64 ticks;					//cycles the interpreter counted for them
	std::vector<u8> readback;	//the read regions
};

//Runs the job as the console does: per repeat the ARM is held in reset, wave RAM is set up and the reset is
//released. The timeout is turned into an instruction budget
void wren7_run_job(const Wren7Job& job, Wren7Run& run);

//The job ended the way it did on the console: both done with the same exit in the result block, or both not
//done
bool wren7_same_end(const Wren7Job& job, const Wren7Run& run);
