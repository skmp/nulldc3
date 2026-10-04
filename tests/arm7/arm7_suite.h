/*
	What the ARM7 tests share besides the machine: the options, the report and the checks with their baseline.

	A test counts its cases into named checks. baseline.txt has, per test and check, how many cases there are and
	how many of them pass. The run fails when
	  - a check passes fewer cases than its baseline, or has another number of cases
	  - a check is not in the baseline, or a check of the baseline was not counted
	  - guest code crashes the emulator code (die(), verify(), a signal) or hangs it
	Passes above the baseline are reported and do not fail, --update-baseline writes them into the file.
	KNOWN_FAILURES.md has the reason for every check that is below its number of cases.
*/
#pragma once
#include "types.h"
#include "test_guard.h"

#include <cstdio>
#include <string>
#include <vector>

//Exit codes of a test
#define ARM7_EXIT_PASS 0
#define ARM7_EXIT_FAIL 1
#define ARM7_EXIT_ERROR 2		//could not run
#define ARM7_EXIT_SKIPPED 77	//the test data is not there, SKIP_RETURN_CODE of the tests

struct Arm7Options
{
	std::string data_dir;		//where the test data is
	std::string baseline;
	std::string select;			//only the programs / jobs that match, no baseline check
	bool update_baseline;
	bool verbose;
	bool emu_output;
};

//The report. stdout is where the emulator code prints, and that is hidden unless --emu-output is given
extern FILE* arm7_report;

//Reads the command line from argv[first] on. data_option and select_option are the names of the options that
//set data_dir and select, opt comes in with the defaults. False: bad command line (also: first is beyond argc),
//the usage was printed
bool arm7_suite_options(int argc, char** argv, int first, const char* usage_head, const char* data_option,
	const char* select_option, Arm7Options& opt);

//Starts the report of the test <name> and installs the guard. False: could not, a message was printed
bool arm7_suite_begin(const char* name, const Arm7Options& opt);

const Arm7Options& arm7_suite_opt();

//<name> is run: there is no selection, or it matches. A selection without * or ? matches anywhere in the name
bool arm7_selected(const std::string& name);

//One case of a check, or a number of them
void arm7_count(const std::string& check, bool pass);
void arm7_count_cases(const std::string& check, u32 total, u32 pass);

//Fails the run whatever the counts are
void arm7_problem(const char* fmt, ...);

//Runs fn(arg) guarded (test_guard.h). A crash or a hang is reported as a problem of <what> and returned
TestGuardResult arm7_guarded(const std::string& what, TestGuardFP* fn, void* arg);

//Prints the counts, compares them with the baseline or writes it. Returns the exit code
int arm7_suite_end();

//Prints why the test is skipped. Returns the exit code
int arm7_suite_skip(const char* name, const std::string& reason);

//Helpers
bool arm7_read_file(const std::string& path, std::vector<u8>& data);
bool arm7_list_files(const std::string& dir, const char* suffix, std::vector<std::string>& names);	//sorted, without suffix
u32 arm7_le32(const std::vector<u8>& data, size_t offset);
