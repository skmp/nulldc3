/*
	What the AICA DSP tests share: the options, the report and the checks with their baseline.

	A test counts its cases into named checks. baseline.txt has, per test and check, how many cases there are and
	how many of them pass. The run fails when
	  - a check passes fewer cases than its baseline, or has another number of cases
	  - a check is not in the baseline, or a check of the baseline was not counted
	  - the test reports a problem (dsp_problem): something that is wrong whatever the counts are
	Passes above the baseline are reported. They fail the run with --strict, which is how ctest runs the tests: a
	baseline that is behind lets a regression back to its counts pass. --update-baseline writes the counts into
	the file. KNOWN_FAILURES.md has the reason for every check that is below its number of cases.
*/
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

//Exit codes of a test
#define DSP_EXIT_PASS 0
#define DSP_EXIT_FAIL 1
#define DSP_EXIT_ERROR 2		//could not run
#define DSP_EXIT_SKIPPED 77		//the test data is not there, SKIP_RETURN_CODE of the tests. DSP_EXIT_ERROR with --require-data

struct DspOptions
{
	std::string data_dir;		//the caique-rtl checkout, for the tests that read its console captures
	std::string baseline;
	bool update_baseline = false;
	bool strict = false;		//passes above the baseline fail
	bool require_data = false;	//test data that is not there is an error, not a reason to skip
	bool verbose = false;
};

//Reads the command line from argv[first] on, opt comes in with the defaults. False: bad command line, the usage
//was printed
bool dsp_suite_options(int argc, char** argv, int first, const char* usage_head, DspOptions& opt);

//Starts the report of the test <name>. False: could not, a message was printed
bool dsp_suite_begin(const char* name, const DspOptions& opt);

const DspOptions& dsp_suite_opt();

//Cases of a check
void dsp_count(const std::string& check, uint64_t total, uint64_t pass);

//Fails the run whatever the counts are
void dsp_problem(const char* fmt, ...);

//A line of the report
void dsp_note(const char* fmt, ...);

//Prints the counts, compares them with the baseline or writes it. Returns the exit code
int dsp_suite_end();

//The test data is not there: prints why the test is skipped or, with opt.require_data, why it cannot run.
//Returns the exit code
int dsp_suite_skip(const char* name, const DspOptions& opt, const std::string& reason);

//Helpers
bool dsp_read_file(const std::string& path, std::vector<uint8_t>& data);
bool dsp_read_lines(const std::string& path, std::vector<std::string>& lines);

//A reproducible stream of numbers: xorshift64*
struct DspRandom
{
	uint64_t state;

	explicit DspRandom(uint64_t seed) : state(seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) { if (!state) state = 1; }

	uint64_t Next()
	{
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;

		return state * 0x2545F4914F6CDD1Dull;
	}

	uint32_t Bits(int n) { return (uint32_t)(Next() >> (64 - n)); }			//n of 1 to 32
	uint32_t Below(uint32_t n) { return (uint32_t)((Next() >> 32) * n >> 32); }	//0 to n - 1
	bool Chance(uint32_t one_in) { return Below(one_in) == 0; }
};
