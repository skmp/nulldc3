/*
	Runs the single-step vectors through one way of executing SH4 code and gates the result on a baseline.

	A suite only says how the 4 instructions of a vector are executed. Filtering by the policy, the expected bus
	activity, loading and comparing the state, catching crashes and hangs, the report and the baseline are the same
	for every suite and are done here.

	Per file the report has: total / run / pass-exact / pass-lenient / failed / filtered by reason.
	  pass-exact    every register matches the expected state bit for bit and the bus activity was as expected
	  pass-lenient  as above, FP registers compared with ss_float_lenient (includes the exact passes)
	  failed        run and not even a lenient pass: wrong bus activity, wrong state, crash or hang

	The baseline has the counts of every file. The run fails when
	  - a file passes fewer vectors than its baseline, exact or lenient, or runs fewer vectors
	  - a vector crashes in a file for which neither the policy (SSP_MAY_CRASH) nor the suite (SsSuiteKnown)
	    announces it, or a vector hangs
	  - a vector file is not in the policy table or not in the baseline, or the other way around
	Counts above the baseline are reported and do not fail, --update-baseline writes them into the file.
*/
#pragma once
#include "ss_format.h"

//What one suite is known not to pass on top of the policy table (ss_policy.cpp has what holds for every way of
//executing the vectors)
struct SsSuiteKnown
{
	const char* file;		//file name without SS_FILE_SUFFIX
	bool may_crash;			//vectors of the file may end in die()/verify() in this suite
	const char* known;		//why vectors of the file do not pass
};

struct SsSuite
{
	const char* name;					//name of the test
	const char* vectors_dir;			//where the vector files are, unless --vectors says otherwise
	const char* baseline;				//baseline file, unless --baseline says otherwise
	bool ordered_fetches;				//instruction fetches are part of the expected bus order, see ss_memory.h
	const SsSuiteKnown* known;			//ends with an entry that has no file. May be 0

	void (*init)();						//once, when the SH4 environment and the guest memory are up. May be 0
	void (*run)(const SsTest& test);	//executes the 4 instructions of the vector that is loaded. Runs guarded
										//(test_guard.h): test_guard_abort() is how it reports a failure of its own
};

//The main() of a suite. Returns the exit code: 0 pass, 1 fail, 2 could not run
int ss_suite_main(int argc, char** argv, const SsSuite& suite);
