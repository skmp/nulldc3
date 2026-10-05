/*
	sh4.differential: the SH4 interpreter against the SH4 dynarec.

	A single-step vector is used for what it starts from: its initial state and its 4 instructions. Both
	engines run them from that state on the same memory (diff_memory.h) and have to agree on
	  the path     the addresses the 4 instructions are run from
	  the bus      every data access in order: read or write, width, address and value
	  the state    every register, bit for bit
	  the outcome  running through, and what the emulator code notifies of on the way (msgboxf)
	What a vector expects plays no part. Only the vectors that devcast cannot run are filtered: unaligned data
	accesses and odd PCs (ss_policy.h).

	Per file the report has: total / run / identical / different / both-crash / both-notify / native / filtered,
	how the different ones differ and the first of them with its opcode, FPSCR and SR, and what differs in it.
	  identical    the engines agree on all of the above, and the emulator code notifies of nothing
	  different    they do not agree. By what differs first: crash (one engine ends in die()/verify()/a signal
	               and the other runs through, or they end in different ones), path, bus, state, notify
	  both-crash   both engines end in the same die()/verify()/signal: there are no results to compare
	  both-notify  the engines agree, and both carry on from the same notifications of the emulator code
	  native       the dynarec turned the instruction under test into native code. For the others it calls the
	               interpreter's handler, and the two engines agree because they are one

	The baseline has the counts of every file. The run fails when
	  - a file has fewer identical vectors than its baseline, runs fewer vectors or has fewer native ones
	  - a file has more vectors than its baseline that differ by crash, path, bus or state, that crash in both
	    engines or that both engines notify on. A difference may go away or become a notification in one engine,
	    it may not become one of another kind
	  - an engine carries on from a notification that the policy does not announce by its text
	    (ss_policy_notification), whatever the baseline says
	  - an engine hangs
	  - a vector file is not in the policy table or not in the baseline, or the other way around
	and lists the vectors of the kinds of difference that the file has more of than its baseline.
	Every other count that is not the baseline's is reported. With --strict, which is how ctest runs the test,
	it fails the run as well: a baseline that is behind lets a regression back to its counts pass.
	--update-baseline writes the counts into the file. KNOWN_DIFFERENCES.md has what is behind the differences
	of the baseline.

	A run that is not compared with the baseline (--vector, --unfilter, --update-baseline) does not end in
	PASS: it fails on what fails whatever the baseline says, and is "not gated" otherwise.
*/
#include "ss_format.h"
#include "ss_policy.h"
#include "ss_state.h"
#include "sh4_env.h"
#include "test_guard.h"
#include "diff_memory.h"
#include "diff_engines.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#if defined(_WIN32)
#include <io.h>
#define NULL_DEVICE "NUL"
#define dup _dup
#define fdopen _fdopen
#define fileno _fileno
#else
#include <unistd.h>
#define NULL_DEVICE "/dev/null"
#endif

#define DIFF_NAME "sh4.differential"

//An engine that takes this long for a vector hangs
#define DIFF_WATCHDOG_MS 2000

//Vectors listed per file and kind when a file fails the baseline
#define DIFF_LIST_MAX 12

//Why the engines are known to differ. KNOWN_DIFFERENCES.md has the long form
struct DiffKnown
{
	const char* file;	//file name without SS_FILE_SUFFIX. 0: every file whose policy has one of flags
	u32 flags;			//SSP_*
	DiffKind kind;
	const char* known;
};

static const char known_fd[] =
	"SR.FD=1: the interpreter notifies \"Full MMU support needed\" where the FPU disable exception would be raised and "
	"executes the instruction, the dynarec executes it without looking at SR.FD";
static const char known_rte[] =
	"RTE: the dynarec loads SR from SSR in front of the delay slot, which then runs on the register bank of the new SR. "
	"The interpreter runs it on the bank of the old SR";
static const char known_sum_order[] =
	"sum of 4 products: the interpreter adds them one by one, ((p0 + p1) + p2) + p3, the x64 backend in pairs, "
	"(p0 + p1) + (p2 + p3). Every sum is rounded, and saturates when rounding to zero";
static const char known_ftrc_nan[] =
	"FTRC of a NaN with the sign bit clear: 7FFFFFFF in the interpreter (it corrects the host's 80000000 by the sign bit "
	"of the operand), 80000000 in the x64 backend (cvttss2si as it is)";

static const char known_pr_behind_slot[] =
	"BSR, BSRF and JSR with a delay slot that reads PR (sts pr,r5 in the directed vectors): the interpreter writes PR "
	"behind the delay slot, which reads the PR of before the call. The dynarec writes it in front of the delay slot, as "
	"an SH4 does";

static const DiffKnown known[] =
{
	{ 0, SSP_FPU, DK_NOTIFY, known_fd },
	{ "0000000000101011_sz0_pr0", 0, DK_STATE, known_rte },
	{ "0000mmmm00000011_sz0_pr0", 0, DK_STATE, known_pr_behind_slot },
	{ "0100mmmm00001011_sz0_pr0", 0, DK_STATE, known_pr_behind_slot },
	{ "1011dddddddddddd_sz0_pr0", 0, DK_STATE, known_pr_behind_slot },
	{ "1111nn0111111101_sz0_pr0", 0, DK_STATE, known_sum_order },
	{ "1111nnmm11101101_sz0_pr0", 0, DK_STATE, known_sum_order },
	{ "1111mmmm00111101_sz0_pr0", 0, DK_STATE, known_ftrc_nan },
};

struct DiffCounts
{
	u32 total;
	u32 run;
	u32 kind[DK_COUNT];
	u32 native;
	u32 filtered[SSF_COUNT];

	DiffCounts() { memset(this, 0, sizeof(*this)); }

	u32 different() const
	{
		u32 rv = 0;

		for (u32 k = DK_FIRST_DIFFERENT; k < DK_COUNT; k++)
			rv += kind[k];

		return rv;
	}

	void add(const DiffCounts& o)
	{
		total += o.total;
		run += o.run;
		for (u32 k = 0; k < DK_COUNT; k++)
			kind[k] += o.kind[k];
		native += o.native;
		for (u32 r = 0; r < SSF_COUNT; r++)
			filtered[r] += o.filtered[r];
	}
};

struct DiffBaseline
{
	u32 total;
	u32 run;
	u32 kind[DK_COUNT];
	u32 native;
};

//A kind that a file may not have more of than its baseline
static bool kind_is_gated(u32 kind)
{
	return kind == DK_BOTH_CRASH || kind == DK_BOTH_NOTIFY || kind == DK_CRASH || kind == DK_PATH || kind == DK_BUS || kind == DK_STATE;
}

//The first notification an engine carried on from that the policy does not announce for the vector, 0 if there
//is none
static const char* unannounced_notification(const EngineResult& e, const char* announced)
{
	for (size_t n = 0; n < e.notification_texts.size(); n++)
	{
		if (!announced || e.notification_texts[n] != announced)
			return e.notification_texts[n].c_str();
	}

	return 0;
}

struct DiffOptions
{
	std::string name;
	std::string vectors_dir;
	std::string baseline;
	std::string file_pattern;
	int vector;
	bool update_baseline;
	bool strict;				//every count that is not the baseline's fails
	bool verbose;
	bool emu_output;
	u32 unfilter;				//mask of the filter reasons (1 << SSF_x) that are not applied
};

static FILE* report;

//The vector: which instruction, in which FPU and CPU mode
static std::string vector_text(u32 index, const SsTest& test)
{
	u32 fpscr = test.initial.s.FPSCR;
	u32 sr = test.initial.s.SR;

	return diff_format("#%u op %04X PR=%u SZ=%u, FPSCR %08X (FR=%u DN=%u RM=%u), SR %08X (MD=%u RB=%u BL=%u FD=%u)", index, ss_opcode(test),
		(fpscr >> 19) & 1, (fpscr >> 20) & 1, fpscr, (fpscr >> 21) & 1, (fpscr >> 18) & 1, fpscr & 3,
		sr, (sr >> 30) & 1, (sr >> 29) & 1, (sr >> 28) & 1, (sr >> 15) & 1);
}

static void print_bus(const EngineResult& e)
{
	fprintf(report, "    %-12s", e.name);

	if (e.guard != TGR_OK)
		fprintf(report, " ends in %s.", diff_ended_text(e).c_str());

	fprintf(report, " path:");
	for (size_t n = 0; n < e.path.size(); n++)
		fprintf(report, " %08X", e.path[n]);

	fprintf(report, ", data:");
	if (e.bus.empty())
		fprintf(report, " -");
	for (size_t n = 0; n < e.bus.size(); n++)
		fprintf(report, "%s %s", n ? "," : "", diff_bus_text(e.bus[n]).c_str());

	if (e.notifications)
		fprintf(report, ", msgboxf (%u): %s", e.notifications, e.notification.c_str());

	fprintf(report, "\n");
}

static void print_vector(const std::string& file, const SsFilePolicy& policy, u32 index, const SsTest& test, const EngineResult& i,
	const EngineResult& d, DiffKind kind, const std::string& what)
{
	fprintf(report, "%s %s (%s) %s\n", kind == DK_IDENTICAL ? "IDENTICAL" : kind == DK_BOTH_NOTIFY ? "BOTH-NOTIFY" : "DIFFERENT", file.c_str(),
		policy.mnemonic, vector_text(index, test).c_str());

	if (kind != DK_IDENTICAL)
		fprintf(report, "    %s\n", what.c_str());

	fprintf(report, "    opcodes at pc: %04X %04X %04X %04X, elsewhere: %04X\n", test.opcodes.op[0], test.opcodes.op[1], test.opcodes.op[2],
		test.opcodes.op[3], test.opcodes.op[4]);

	print_bus(i);
	print_bus(d);

	const u32* initial = (const u32*)&test.initial.s;
	const u32* a = (const u32*)&i.state;
	const u32* b = (const u32*)&d.state;

	//after a crash the context is whatever the engine had written back by then
	fprintf(report, "    %-8s %-8s %-11s %-8s\n", "", "initial", "interpreter", "dynarec");

	for (u32 w = 0; w < SS_STATE_WORDS; w++)
		fprintf(report, "    %-8s %08X %08X    %08X%s\n", ss_state_word_name(w), initial[w], a[w], b[w], a[w] != b[w] ? "  <-- differs" : "");
}

//Why a kind of difference is known in a file, 0 if it is not
static const char* known_for(const std::string& file, const SsFilePolicy& policy, u32 kind)
{
	for (size_t n = 0; n < sizeof(known) / sizeof(known[0]); n++)
	{
		const DiffKnown& k = known[n];

		if ((u32)k.kind != kind)
			continue;

		if (k.file ? file == k.file : (policy.flags & k.flags) != 0)
			return k.known;
	}

	return 0;
}

//The vectors that differ in one way for one reason
struct DiffGroup
{
	u32 kind;
	const char* known;
	u32 vectors;
	std::vector<std::string> where;		//"file (instruction): vectors"
};

//* and ? wildcards. A pattern without any matches anywhere in the name
static bool glob(const char* pattern, const char* text)
{
	if (*pattern == 0)
		return *text == 0;

	if (*pattern == '*')
		return glob(pattern + 1, text) || (*text != 0 && glob(pattern, text + 1));

	return *text != 0 && (*pattern == '?' || *pattern == *text) && glob(pattern + 1, text + 1);
}

static bool file_selected(const std::string& pattern, const std::string& name)
{
	if (pattern.empty())
		return true;

	if (pattern.find_first_of("*?") == std::string::npos)
		return name.find(pattern) != std::string::npos;

	return glob(pattern.c_str(), name.c_str());
}

//"reason,reason,..." or "all"
static bool parse_unfilter(const char* list, DiffOptions& opt)
{
	std::string text = list;
	size_t start = 0;

	while (start <= text.size())
	{
		size_t end = text.find(',', start);

		if (end == std::string::npos)
			end = text.size();

		std::string name = text.substr(start, end - start);
		bool found = false;

		for (u32 r = 1; r < SSF_COUNT; r++)
		{
			if (name == "all" || name == ss_filter_name((SsFilterReason)r))
			{
				opt.unfilter |= 1 << r;
				found = true;
			}
		}

		if (!found)
			return false;

		start = end + 1;
	}

	return true;
}

static bool load_baseline(const std::string& path, std::map<std::string, DiffBaseline>& baseline)
{
	FILE* f = fopen(path.c_str(), "r");

	if (!f)
		return false;

	char line[512];

	while (fgets(line, sizeof(line), f))
	{
		char name[256];
		DiffBaseline b;

		if (line[0] == '#')
			continue;

		memset(&b, 0, sizeof(b));

		if (sscanf(line, "%255s %u %u %u %u %u %u %u %u %u %u %u", name, &b.total, &b.run, &b.kind[DK_IDENTICAL], &b.kind[DK_BOTH_CRASH],
			&b.kind[DK_BOTH_NOTIFY], &b.kind[DK_CRASH], &b.kind[DK_PATH], &b.kind[DK_BUS], &b.kind[DK_STATE], &b.kind[DK_NOTIFY], &b.native) == 12)
			baseline[name] = b;
	}

	fclose(f);

	return true;
}

static bool save_baseline(const std::string& path, const char* name, const std::map<std::string, DiffCounts>& results)
{
	FILE* f = fopen(path.c_str(), "w");

	if (!f)
		return false;

	fprintf(f, "# %s baseline: on how many vectors the SH4 interpreter and the dynarec agree, per file, by what the\n", name);
	fprintf(f, "# others differ, and for how many the dynarec compiles the instruction to native code.\n");
	fprintf(f, "# The test fails when a file runs fewer vectors than listed here, has fewer identical or native ones, or more\n");
	fprintf(f, "# of both-crash, both-notify, crash, path, bus or state.\n");
	fprintf(f, "# Written by the test's --update-baseline, tests/sh4_dynarec/KNOWN_DIFFERENCES.md has the causes.\n");
	fprintf(f, "#\n");
	fprintf(f, "# %-24s %5s %5s %9s %10s %11s %5s %5s %5s %5s %6s %6s\n", "file", "total", "run", diff_kind_name[DK_IDENTICAL],
		diff_kind_name[DK_BOTH_CRASH], diff_kind_name[DK_BOTH_NOTIFY], diff_kind_name[DK_CRASH], diff_kind_name[DK_PATH], diff_kind_name[DK_BUS],
		diff_kind_name[DK_STATE], diff_kind_name[DK_NOTIFY], "native");

	for (std::map<std::string, DiffCounts>::const_iterator it = results.begin(); it != results.end(); ++it)
	{
		const DiffCounts& c = it->second;

		fprintf(f, "%-26s %5u %5u %9u %10u %11u %5u %5u %5u %5u %6u %6u\n", it->first.c_str(), c.total, c.run, c.kind[DK_IDENTICAL],
			c.kind[DK_BOTH_CRASH], c.kind[DK_BOTH_NOTIFY], c.kind[DK_CRASH], c.kind[DK_PATH], c.kind[DK_BUS], c.kind[DK_STATE], c.kind[DK_NOTIFY],
			c.native);
	}

	fclose(f);

	return true;
}

//"state=12 notify=239"
static std::string kinds_text(const u32* kind)
{
	std::string rv;

	for (u32 k = DK_FIRST_DIFFERENT; k < DK_COUNT; k++)
	{
		if (kind[k])
			rv += diff_format("%s%s=%u", rv.empty() ? "" : " ", diff_kind_name[k], kind[k]);
	}

	return rv;
}

static int usage(const DiffOptions& defaults)
{
	fprintf(stderr,
		DIFF_NAME ": runs the SH4 interpreter and the dynarec from the initial states of the single-step vectors,\n"
		"compares what they make of them and compares the result with a baseline\n"
		"\n"
		"  --vectors <dir>      vector files (*" SS_FILE_SUFFIX "), default %s\n"
		"  --baseline <file>    baseline file, default %s\n"
		"  --name <name>        what the report and the baseline call the test, for another set of vectors\n"
		"  --update-baseline    write the counts of this run into the baseline file (all files have to be run)\n"
		"  --strict             every count that is not the baseline's fails: the baseline has to be the one of\n"
		"                       this build. ctest runs the test this way\n"
		"  --file <pattern>     only the files whose name or instruction contains <pattern>, or matches it if it\n"
		"                       has * or ?\n"
		"  --vector <N>         only vector N (0 based) of each file, no baseline check\n"
		"  --unfilter <reasons> run the vectors that are filtered for these reasons as well (unaligned, odd-pc, or\n"
		"                       all), no baseline check\n"
		"  --verbose            for every vector the engines differ on (with --vector: for that vector): what both\n"
		"                       made of it, path, data accesses and all registers\n"
		"  --emu-output         do not hide what the emulator code prints\n",
		defaults.vectors_dir.c_str(), defaults.baseline.c_str());

	return 2;
}

int main(int argc, char** argv)
{
	DiffOptions opt;

	opt.name = DIFF_NAME;
	opt.vectors_dir = NULLDC_SH4_TESTS_DIR;
	opt.baseline = NULLDC_BASELINE;
	opt.vector = -1;
	opt.update_baseline = false;
	opt.strict = false;
	opt.verbose = false;
	opt.emu_output = false;
	opt.unfilter = 0;

	const DiffOptions defaults = opt;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		bool has_value = i + 1 < argc;

		if (arg == "--vectors" && has_value)
			opt.vectors_dir = argv[++i];
		else if (arg == "--baseline" && has_value)
			opt.baseline = argv[++i];
		else if (arg == "--name" && has_value)
			opt.name = argv[++i];
		else if (arg == "--file" && has_value)
			opt.file_pattern = argv[++i];
		else if (arg == "--vector" && has_value)
			opt.vector = atoi(argv[++i]);
		else if (arg == "--unfilter" && has_value && parse_unfilter(argv[i + 1], opt))
			i++;
		else if (arg == "--update-baseline")
			opt.update_baseline = true;
		else if (arg == "--strict")
			opt.strict = true;
		else if (arg == "--verbose")
			opt.verbose = true;
		else if (arg == "--emu-output")
			opt.emu_output = true;
		else
			return usage(defaults);
	}

	bool all_files = opt.file_pattern.empty() && opt.vector < 0;
	bool gated = opt.vector < 0 && opt.unfilter == 0;	//the counts can be compared with the baseline

	if (opt.update_baseline && (!all_files || !gated))
	{
		fprintf(stderr, "--update-baseline needs a run of all files with the filter as it is\n");
		return 2;
	}

	if (!diff_compare_selfcheck())
	{
		fprintf(stderr, "the comparison of the two engines does not see what it has to (diff_compare_selfcheck)\n");
		return 2;
	}

	//What the vectors expect does not matter here, what devcast cannot run does
	u32 ignore = ~((1u << SSF_UNALIGNED) | (1u << SSF_ODD_PC)) | opt.unfilter;

	std::vector<std::string> files;

	if (!ss_list_files(opt.vectors_dir, files) || files.empty())
	{
		fprintf(stderr, "no vector files (*" SS_FILE_SUFFIX ") in %s\n", opt.vectors_dir.c_str());
		fprintf(stderr, "run: git submodule update --init tests/vendor/sh4-tests\n");
		return 2;
	}

	std::map<std::string, DiffBaseline> baseline;
	bool have_baseline = load_baseline(opt.baseline, baseline);

	if (!have_baseline && !opt.update_baseline && gated)
	{
		fprintf(stderr, "cannot read the baseline %s\n", opt.baseline.c_str());
		return 2;
	}

	//The emulator code prints as it goes. The report keeps the real stdout
	report = stdout;

	if (!opt.emu_output)
	{
		fflush(stdout);
		report = fdopen(dup(fileno(stdout)), "w");

		if (!report || !freopen(NULL_DEVICE, "w", stdout))
		{
			fprintf(stderr, "cannot redirect the emulator output\n");
			return 2;
		}
	}

	test_guard_echo_msgbox(opt.emu_output);

	sh4_env_init();
	diff_engines_init();

	test_guard_init(DIFF_WATCHDOG_MS);

	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

	fprintf(report, "%s: vectors %s\n\n", opt.name.c_str(), opt.vectors_dir.c_str());
	fprintf(report, "%-26s %-24s %5s %5s %9s %9s %10s %11s %6s  %s\n", "file", "instruction", "total", "run", "identical", "different", "both-crash",
		"both-notify", "native", "filtered");

	std::map<std::string, DiffCounts> results;
	DiffCounts sum;
	u32 problems = 0;		//what fails the run
	u32 improvements = 0;
	std::vector<std::string> notes;

	for (size_t fi = 0; fi < files.size(); fi++)
	{
		const std::string& file = files[fi];

		const SsFilePolicy* policy = ss_policy_find(file);

		if (!file_selected(opt.file_pattern, file) && !(policy && file_selected(opt.file_pattern, policy->mnemonic)))
			continue;

		if (!policy)
		{
			fprintf(report, "%-26s FAIL: the file is not in the policy table (tests/common/ss_policy.cpp)\n", file.c_str());
			problems++;
			continue;
		}

		std::vector<SsTest> tests;
		std::string error;

		if (!ss_load_file(opt.vectors_dir + "/" + file + SS_FILE_SUFFIX, tests, error))
		{
			fprintf(report, "%-26s FAIL: %s\n", file.c_str(), error.c_str());
			problems++;
			continue;
		}

		DiffCounts counts;
		std::vector<std::string> listed[DK_COUNT];	//the first DIFF_LIST_MAX vectors of each kind
		std::vector<std::string> unannounced;		//the first DIFF_LIST_MAX vectors with a notification nobody announces
		u32 unannounced_notifications = 0;

		for (size_t ti = 0; ti < tests.size(); ti++)
		{
			if (opt.vector >= 0 && (size_t)opt.vector != ti)
				continue;

			const SsTest& test = tests[ti];

			counts.total++;

			SsFilterReason reason = ss_policy_filter(*policy, test, ignore);

			if (reason != SSF_NONE)
			{
				counts.filtered[reason]++;

				if (opt.vector >= 0)
					fprintf(report, "%s #%u is filtered: %s (%s)\n", file.c_str(), (u32)ti, ss_filter_name(reason), ss_filter_description(reason));

				continue;
			}

			counts.run++;

			diff_mem_begin(test, policy->read_size);

			EngineResult interpreter, dynarec;

			diff_run_interpreter(test, interpreter);
			diff_run_dynarec(test, dynarec);

			std::string what;
			DiffKind kind = diff_compare(interpreter, dynarec, what);

			counts.kind[kind]++;

			if (dynarec.native)
				counts.native++;

			if (kind != DK_IDENTICAL)
			{
				if (listed[kind].size() < DIFF_LIST_MAX)
					listed[kind].push_back(vector_text((u32)ti, test) + ": " + what);
			}

			//a notification an engine carries on from has to be one the policy knows by its text
			const char* announced = ss_policy_notification(*policy, test);
			const char* raised = unannounced_notification(interpreter, announced);
			const EngineResult& by = raised ? interpreter : dynarec;

			if (!raised)
				raised = unannounced_notification(dynarec, announced);

			if (raised)
			{
				unannounced_notifications++;

				if (unannounced.size() < DIFF_LIST_MAX)
					unannounced.push_back(vector_text((u32)ti, test) + diff_format(": the %s notifies \"%s\"", by.name, raised));
			}

			if (opt.verbose && (kind != DK_IDENTICAL || opt.vector >= 0))
				print_vector(file, *policy, (u32)ti, test, interpreter, dynarec, kind, what);
			else if (opt.vector >= 0)
				fprintf(report, "%s (%s) %s: %s\n", file.c_str(), policy->mnemonic, vector_text((u32)ti, test).c_str(),
					kind == DK_IDENTICAL ? "identical" : what.c_str());
		}

		//the file's line
		std::string filtered;

		for (u32 r = 1; r < SSF_COUNT; r++)
		{
			if (counts.filtered[r])
				filtered += diff_format("%s%s=%u", filtered.empty() ? "" : " ", ss_filter_name((SsFilterReason)r), counts.filtered[r]);
		}

		fprintf(report, "%-26s %-24s %5u %5u %9u %9u %10u %11u %6u  %s\n", file.c_str(), policy->mnemonic, counts.total, counts.run,
			counts.kind[DK_IDENTICAL], counts.different(), counts.kind[DK_BOTH_CRASH], counts.kind[DK_BOTH_NOTIFY], counts.native,
			filtered.empty() ? "-" : filtered.c_str());

		//the first vector of every kind of difference
		for (u32 k = DK_HANG; k < DK_COUNT && !opt.verbose && opt.vector < 0; k++)
		{
			if (counts.kind[k])
				fprintf(report, "    %s %u, first: %s\n", diff_kind_name[k], counts.kind[k], listed[k][0].c_str());
		}

		results[file] = counts;
		sum.add(counts);

		//the gate
		if (counts.kind[DK_HANG])
		{
			notes.push_back(diff_format("FAIL %s: an engine hangs on %u vectors", file.c_str(), counts.kind[DK_HANG]));
			for (size_t n = 0; n < listed[DK_HANG].size(); n++)
				notes.push_back("    " + listed[DK_HANG][n]);
			problems++;
		}

		if (unannounced_notifications)
		{
			notes.push_back(diff_format("FAIL %s (%s): an engine carries on from a notification that the policy does not announce on %u vectors",
				file.c_str(), policy->mnemonic, unannounced_notifications));
			for (size_t n = 0; n < unannounced.size(); n++)
				notes.push_back("    " + unannounced[n]);
			if (unannounced_notifications > unannounced.size())
				notes.push_back(diff_format("    and %u more", unannounced_notifications - (u32)unannounced.size()));
			problems++;
		}

		if (opt.vector >= 0)
		{
			if (counts.different())
				problems++;

			continue;
		}

		if (opt.update_baseline || !gated)
			continue;

		std::map<std::string, DiffBaseline>::const_iterator base = baseline.find(file);

		if (base == baseline.end())
		{
			notes.push_back(diff_format("FAIL %s: not in the baseline", file.c_str()));
			problems++;
			continue;
		}

		const DiffBaseline& b = base->second;

		if (counts.total != b.total)
		{
			notes.push_back(diff_format("FAIL %s: has %u vectors, the baseline was made with %u", file.c_str(), counts.total, b.total));
			problems++;
			continue;
		}

		//a count that is not the baseline's and does not fail by itself
		const char* moved = opt.strict ? "FAIL not as in the baseline" : "not as in the baseline";

		if (counts.run < b.run)
		{
			notes.push_back(diff_format("FAIL %s: run %u, baseline %u", file.c_str(), counts.run, b.run));
			problems++;
		}
		else if (counts.run > b.run)
		{
			notes.push_back(diff_format("%s: %s run %u, baseline %u", moved, file.c_str(), counts.run, b.run));
			improvements++;
		}

		if (counts.native < b.native)
		{
			notes.push_back(diff_format("FAIL %s (%s): native %u, baseline %u: the dynarec hands the instruction to the interpreter", file.c_str(),
				policy->mnemonic, counts.native, b.native));
			problems++;
		}
		else if (counts.native > b.native)
		{
			notes.push_back(diff_format("%s: %s native %u, baseline %u", moved, file.c_str(), counts.native, b.native));
			improvements++;
		}

		//fewer identical vectors, or more of a kind a file may not have more of
		std::string worse;

		if (counts.kind[DK_IDENTICAL] < b.kind[DK_IDENTICAL])
			worse = diff_format("identical %u, baseline %u", counts.kind[DK_IDENTICAL], b.kind[DK_IDENTICAL]);

		for (u32 k = DK_BOTH_CRASH; k < DK_COUNT; k++)
		{
			if (kind_is_gated(k) && counts.kind[k] > b.kind[k])
				worse += diff_format("%s%s %u, baseline %u", worse.empty() ? "" : "; ", diff_kind_name[k], counts.kind[k], b.kind[k]);
		}

		if (!worse.empty())
		{
			notes.push_back(diff_format("FAIL %s (%s): %s", file.c_str(), policy->mnemonic, worse.c_str()));
			problems++;

			//which vectors: the ones of the kinds there are more of now
			for (u32 k = DK_BOTH_CRASH; k < DK_COUNT; k++)
			{
				if (counts.kind[k] <= b.kind[k])
					continue;

				notes.push_back(diff_format("  %s %u, baseline %u (%s):", diff_kind_name[k], counts.kind[k], b.kind[k], diff_kind_description[k]));

				for (size_t n = 0; n < listed[k].size(); n++)
					notes.push_back("    " + listed[k][n]);

				if (counts.kind[k] > listed[k].size())
					notes.push_back(diff_format("    and %u more, all of them: --file %s --verbose", counts.kind[k] - (u32)listed[k].size(), file.c_str()));
			}

			continue;
		}

		//what is left: more identical vectors, more or fewer notifications of one engine, fewer of another kind
		for (u32 k = DK_IDENTICAL; k < DK_COUNT; k++)
		{
			if (k != DK_HANG && counts.kind[k] != b.kind[k])
			{
				notes.push_back(diff_format("%s: %s %s %u, baseline %u", moved, file.c_str(), diff_kind_name[k], counts.kind[k], b.kind[k]));
				improvements++;
			}
		}
	}

	if (results.empty() && problems == 0)
	{
		fprintf(report, "no vector file matches %s\n", opt.file_pattern.c_str());
		return 2;
	}

	if (all_files && gated && !opt.update_baseline)
	{
		for (std::map<std::string, DiffBaseline>::const_iterator it = baseline.begin(); it != baseline.end(); ++it)
		{
			if (results.find(it->first) == results.end())
			{
				notes.push_back("FAIL " + it->first + ": in the baseline, but there is no such vector file");
				problems++;
			}
		}
	}

	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

	fprintf(report, "\n%u files, %u vectors in %.2f s\n", (u32)results.size(), sum.total, seconds);
	fprintf(report, "  run          %6u\n", sum.run);
	fprintf(report, "  identical    %6u\n", sum.kind[DK_IDENTICAL]);
	fprintf(report, "  different    %6u\n", sum.different());

	for (u32 k = DK_FIRST_DIFFERENT; k < DK_COUNT; k++)
	{
		if (sum.kind[k])
			fprintf(report, "    %-12s %6u  %s\n", diff_kind_name[k], sum.kind[k], diff_kind_description[k]);
	}

	fprintf(report, "  both-crash   %6u  %s\n", sum.kind[DK_BOTH_CRASH], diff_kind_description[DK_BOTH_CRASH]);
	fprintf(report, "  both-notify  %6u  %s\n", sum.kind[DK_BOTH_NOTIFY], diff_kind_description[DK_BOTH_NOTIFY]);
	if (sum.kind[DK_HANG])
		fprintf(report, "  hang         %6u  %s\n", sum.kind[DK_HANG], diff_kind_description[DK_HANG]);
	fprintf(report, "  native       %6u  vectors whose instruction the dynarec compiles to native code, the others it hands to the interpreter\n", sum.native);
	fprintf(report, "  filtered     %6u\n", sum.total - sum.run);

	for (u32 r = 1; r < SSF_COUNT; r++)
	{
		if (sum.filtered[r])
			fprintf(report, "    %-12s %6u  %s\n", ss_filter_name((SsFilterReason)r), sum.filtered[r], ss_filter_description((SsFilterReason)r));
	}

	//what is behind the differences
	std::vector<DiffGroup> groups;

	for (std::map<std::string, DiffCounts>::const_iterator it = results.begin(); it != results.end(); ++it)
	{
		const SsFilePolicy* policy = ss_policy_find(it->first);

		for (u32 k = DK_FIRST_DIFFERENT; k < DK_COUNT; k++)
		{
			u32 count = it->second.kind[k];

			if (count == 0)
				continue;

			const char* why = known_for(it->first, *policy, k);
			size_t g = 0;

			while (g < groups.size() && !(groups[g].kind == k && groups[g].known == why))
				g++;

			if (g == groups.size())
			{
				DiffGroup group;

				group.kind = k;
				group.known = why;
				group.vectors = 0;

				groups.push_back(group);
			}

			groups[g].vectors += count;
			groups[g].where.push_back(diff_format("%s (%s): %u", it->first.c_str(), policy->mnemonic, count));
		}
	}

	if (!groups.empty())
		fprintf(report, "\nwhat the engines differ in:\n");

	for (size_t g = 0; g < groups.size(); g++)
	{
		const DiffGroup& group = groups[g];

		fprintf(report, "  %s, %u vector%s in %u file%s: %s\n", diff_kind_name[group.kind], group.vectors, group.vectors == 1 ? "" : "s",
			(u32)group.where.size(), group.where.size() == 1 ? "" : "s", group.known ? group.known : "NOT EXPLAINED, KNOWN_DIFFERENCES.md has what is");

		//the files are in the list above, they are named again when they are few or when they are news
		for (size_t w = 0; w < group.where.size() && (group.where.size() <= 4 || !group.known); w++)
			fprintf(report, "    %s\n", group.where[w].c_str());
	}

	if (!notes.empty())
	{
		fprintf(report, "\n");

		for (size_t i = 0; i < notes.size(); i++)
			fprintf(report, "%s\n", notes[i].c_str());
	}

	if (opt.update_baseline)
	{
		if (!save_baseline(opt.baseline, opt.name.c_str(), results))
		{
			fprintf(report, "\ncannot write %s\n", opt.baseline.c_str());
			return 2;
		}

		fprintf(report, "\nbaseline written to %s\n", opt.baseline.c_str());
	}
	else if (improvements)
	{
		fprintf(report, "\n%u counts are not the baseline's%s: run with --update-baseline and commit %s\n", improvements,
			opt.strict ? ", which fails with --strict" : " and do not fail", opt.baseline.c_str());

		if (opt.strict)
			problems++;
	}

	//Only a run that was compared with the baseline passes
	const char* verdict = "PASS";

	if (problems)
		verdict = "FAIL";
	else if (opt.update_baseline)
		verdict = "not gated (the baseline was written)";
	else if (!gated)
		verdict = "not gated (not compared with the baseline)";

	fprintf(report, "\n%s: %s\n", opt.name.c_str(), verdict);
	fflush(report);

	return problems ? 1 : 0;
}
