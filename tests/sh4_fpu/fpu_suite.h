/*
	What the SH4 FPU tests share: one FPU instruction through the SH4 interpreter and through the SH4 dynarec,
	what each of them leaves compared with what the hardware leaves, counted by group, the report and the
	baseline.

	A case is an opcode, the FP registers, FPUL, FPSCR and T in front of it, the same behind it on the hardware,
	and whether the hardware raises the FPU exception. Both engines run it from the same state:
	  interpreter   the instruction loop of SH4IInterpreter::Loop
	  dynarec       decoder -> shil -> x64 backend, entered as the other dynarec tests enter it
	                (sh4_dynarec/dynarec_runner.h)
	FPSCR is written the way LDS writes it: the value, then UpdateFPSCR(), which loads the host's rounding and
	denormal modes. The host's control word is read back around every case and has to be the one the emulator's
	SetFloatStatusReg() loads for that FPSCR.

	Per group the report has
	  total     cases
	  trap      cases in which the hardware raises the FPU exception (EXPEVT 0x120) and writes no result
	  value     per engine: FR0-FR15, XF0-XF15, FPUL and T are the hardware's, bit for bit, and the engine went on
	            to the next instruction. For a trap case: and the engine did not go on to the next instruction
	  flags     per engine: FPSCR behind the instruction is the hardware's. The cause and flag fields are what an
	            instruction changes in it
	  agree     the interpreter and the dynarec leave the same registers, FPSCR and PC, or end in the same die()
	  native    the dynarec compiled the instruction to native code. Otherwise its block calls the interpreter's
	            handler, and the dynarec's counts say nothing about the recompiler
	  crash     cases in which an engine ends in die(), verify() or a signal

	The baseline has these counts for every group. The run fails when
	  - a group has fewer cases with the hardware's value or flags than its baseline, in either engine, fewer
	    cases in which the engines agree, fewer native ones or more crashes
	  - a group has another number of cases or of trap cases than its baseline: the data is not the baseline's
	  - a group is not in the baseline, or a group of the baseline has no cases
	and, whatever the baseline says, when
	  - a case hangs, or the emulator code raises a notification (msgboxf) and carries on
	  - the host's control word is not the emulator's in front of or behind a case
	  - a test reports a problem of its own (fpu_problem)
	Counts that are better than the baseline are reported. They fail the run with --strict, which is how ctest
	runs the tests: a baseline that is behind lets a regression back to its counts pass. --update-baseline writes
	the counts into the file. KNOWN_FAILURES.md has the reason for every count that is below its total.

	A run of a part of the groups (--group) or one that writes the baseline is not compared with the baseline
	and does not end in PASS. --except leaves groups out of a run that is compared: the report says how many, and
	a pattern that leaves out nothing fails.

	--why counts, per group and over all of them, why the cases that are not as on the hardware are not. The suite
	itself tells a crash, an FPU exception that is not raised and an FPSCR that is not written. For a wrong value
	it asks the test (FpuWhyFP), which looks at the operands and at what the engine left, and may run the engine
	again on other operands to see what makes the difference (fpu_try).
*/
#pragma once
#include "types.h"

#include <cstdio>
#include <string>
#include <vector>

//Exit codes of a test
#define FPU_EXIT_PASS 0
#define FPU_EXIT_FAIL 1
#define FPU_EXIT_ERROR 2	//could not run
#define FPU_EXIT_SKIPPED 77	//the test data is not there, SKIP_RETURN_CODE of the tests. FPU_EXIT_ERROR with --require-data

//FPSCR
#define FPU_FPSCR_RM   0x00000003
#define FPU_FPSCR_DN   0x00040000
#define FPU_FPSCR_PR   0x00080000
#define FPU_FPSCR_FR   0x00200000

//The FP side of the machine, as bit patterns
struct FpuRegs
{
	u32 fr[16];
	u32 xf[16];
	u32 fpul;
	u32 fpscr;
	u32 t;
};

struct FpuCase
{
	u32 opcode;
	FpuRegs before;
	FpuRegs after;	//what the hardware leaves
	bool trap;		//the hardware raises the FPU exception
};

enum
{
	FPU_INTERPRETER,
	FPU_DYNAREC,

	FPU_ENGINES
};

struct FpuOptions
{
	std::string data_dir;		//the shrike4-rtl checkout
	std::string baseline;
	std::string name;			//what the report calls the test, empty: its own name
	std::string select;			//only the groups that match, no baseline check
	std::string except;			//not the groups that match. The others are compared with the baseline
	bool update_baseline;
	bool strict;				//counts above the baseline fail
	bool require_data;			//test data that is not there is an error, not a reason to skip
	bool verbose;				//every case that is not as on the hardware
	bool first;					//the first such case of every group
	bool why;					//the reasons such cases have, counted
	bool emu_output;
};

//The report. stdout is where the emulator code prints, and that is hidden unless --emu-output is given
extern FILE* fpu_report;

//Reads the command line from argv[first] on, opt comes in with the defaults. False: bad command line (also:
//first is beyond argc), the usage was printed
bool fpu_suite_options(int argc, char** argv, int first, const char* usage_head, FpuOptions& opt);

//Starts the report of the test <name>, brings up the SH4 with both engines and checks the host's control word
//against FPSCR. False: could not, a message was printed
bool fpu_suite_begin(const char* name, const FpuOptions& opt);

//The index of the group <name>. Groups are reported and written in the order they are first asked for
u32 fpu_group(const std::string& name);

//The group is run: there is no selection, or it matches, and no exception matches. A pattern without * or ? matches
//anywhere in the name
bool fpu_group_selected(u32 group);

//Describes a case for the report: where it comes from and its operands
typedef void FpuLabelFP(const void* arg, const FpuCase& c, char* text, size_t size);

//Runs the case through both engines and counts it into the group. Registers a case does not name should hold
//something an instruction that writes them by mistake does not leave (fpu_fill)
void fpu_run(u32 group, const FpuCase& c, FpuLabelFP* label, const void* label_arg);

//Sentinels in every FP register and in FPUL, T = 1, FPSCR = fpscr
void fpu_fill(FpuRegs& regs, u32 fpscr);

//Why an engine went on behind the instruction of a case with values that are not the hardware's: a short text
//that stands for every case with that reason, the report counts them (--why). left is what the engine left. 0:
//the test cannot tell
typedef const char* FpuWhyFP(const FpuCase& c, const FpuRegs& left, u32 engine);

void fpu_suite_why(FpuWhyFP* why);

//Runs c.opcode on c.before through an engine, outside of the counts. False: the engine does not run through
bool fpu_try(const FpuCase& c, u32 engine, FpuRegs& left);

//A line of the report that is not a group: where the data comes from, what a test checked on its own
void fpu_note(const char* fmt, ...);

//Fails the run whatever the counts are
void fpu_problem(const char* fmt, ...);

//Prints the counts, compares them with the baseline or writes it. Returns the exit code
int fpu_suite_end();

//The test data is not there: prints why the test is skipped or, with opt.require_data, why it cannot run.
//Returns the exit code
int fpu_suite_skip(const char* name, const FpuOptions& opt, const std::string& reason);

//Helpers
bool fpu_read_file(const std::string& path, std::vector<u8>& data);
u32 fpu_le32(const std::vector<u8>& data, size_t offset);

//Deterministic 64 bit mixer (splitmix64): the operands of generated cases come from their indices
u64 fpu_mix(u64 x);
