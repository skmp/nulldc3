/*
	Runs the single-step vectors through one way of executing SH4 code and gates the result on a baseline.

	A suite only says how the 4 instructions of a vector are executed. Filtering by the policy, the expected bus
	activity, loading and comparing the state, catching crashes and hangs, the report and the baseline are the same
	for every suite and are done here.

	Per file the report has: total / run / pass-exact / pass-lenient / failed / filtered by reason.
	  pass-exact    every register matches the expected state bit for bit and the bus activity was as expected
	  pass-lenient  as above, FP registers compared with ss_float_lenient (includes the exact passes)
	  failed        run and not even a lenient pass: wrong bus activity, wrong state, crash or hang
	and, counted on its own, whatever the result of the vector is:
	  notified      vectors during which the emulator code raised a notification (msgboxf) and carried on

	A suite that compiles the instructions (SsSuite::native) has one more count per file:
	  native        vectors whose instruction under test was turned into native code. The others were handed
	                to the interpreter's handler, and say nothing about the recompiler

	The baseline has the counts of every file. The run fails when
	  - a file passes fewer vectors than its baseline, exact or lenient, runs fewer vectors, has fewer
	    native ones or more notified ones
	  - a vector crashes in a file for which neither the policy (SSP_MAY_CRASH) nor the suite (SsSuiteKnown)
	    announces it, or a vector hangs
	  - a vector raises a notification that neither the policy (ss_policy_notification) nor the suite
	    (SsSuiteKnown::notifies) announces by its text, whatever the baseline says
	  - a vector file is not in the policy table or not in the baseline, or the other way around
	Counts that are better than the baseline are reported. They fail the run with --strict, which is how ctest
	runs the suites: a baseline that is behind lets a regression back to its counts pass. --update-baseline
	writes the counts into the file.

	--file, --vector, --verbose and --unfilter are for looking at single files and vectors, the option list is
	in the usage text.
*/
#pragma once
#include "ss_format.h"

//What one suite is known not to pass on top of the policy table (ss_policy.cpp has what holds for every way of
//executing the vectors)
struct SsSuiteKnown
{
	const char* file;		//file name without SS_FILE_SUFFIX
	bool may_crash;			//vectors of the file may end in die()/verify() in this suite
	const char* notifies;	//the notification (msgboxf text) vectors of the file may raise in this suite, 0: none
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
	bool (*native)();					//after run: the instruction under test was compiled to native code. 0 for a
										//suite that does not compile
};

//The main() of a suite. Returns the exit code: 0 pass, 1 fail, 2 could not run
int ss_suite_main(int argc, char** argv, const SsSuite& suite);
