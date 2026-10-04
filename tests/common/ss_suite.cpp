#include "ss_suite.h"
#include "ss_memory.h"
#include "ss_policy.h"
#include "ss_state.h"
#include "sh4_env.h"
#include "test_guard.h"

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

//A vector that takes this long is a hang
#define SS_WATCHDOG_MS 2000

enum SsOutcome
{
	SSO_PASS_EXACT,
	SSO_PASS_LENIENT,
	SSO_FAIL,
	SSO_CRASH,
	SSO_HANG,
};

struct SsCounts
{
	u32 total;
	u32 run;
	u32 exact;
	u32 lenient;
	u32 crash;
	u32 hang;
	u32 notified;	//vectors during which the emulator called msgboxf and carried on
	u32 native;		//vectors whose instruction under test was compiled to native code (SsSuite::native)
	u32 filtered[SSF_COUNT];

	SsCounts() { memset(this, 0, sizeof(*this)); }

	u32 failed() const { return run - lenient; }

	void add(const SsCounts& o)
	{
		total += o.total;
		run += o.run;
		exact += o.exact;
		lenient += o.lenient;
		crash += o.crash;
		hang += o.hang;
		notified += o.notified;
		native += o.native;
		for (u32 i = 0; i < SSF_COUNT; i++)
			filtered[i] += o.filtered[i];
	}
};

struct SsBaseline
{
	u32 total;
	u32 run;
	u32 exact;
	u32 lenient;
	u32 native;
	bool has_native;
};

struct SsOptions
{
	std::string vectors_dir;
	std::string baseline;
	std::string file_pattern;
	int vector;
	bool update_baseline;
	bool strict;				//counts above the baseline fail
	bool verbose;
	bool emu_output;
	u32 unfilter;				//mask of the filter reasons (1 << SSF_x) that are not applied
};

static FILE* report;

static const SsSuite* the_suite;

static void run_thunk(void* arg)
{
	the_suite->run(*(const SsTest*)arg);
}

static const SsSuiteKnown* suite_known(const std::string& file)
{
	for (const SsSuiteKnown* k = the_suite->known; k && k->file; k++)
	{
		if (file == k->file)
			return k;
	}

	return 0;
}

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
static bool parse_unfilter(const char* list, SsOptions& opt)
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

static bool load_baseline(const std::string& path, std::map<std::string, SsBaseline>& baseline)
{
	FILE* f = fopen(path.c_str(), "r");

	if (!f)
		return false;

	char line[512];

	while (fgets(line, sizeof(line), f))
	{
		char name[256];
		SsBaseline b;

		if (line[0] == '#')
			continue;

		b.native = 0;

		int fields = sscanf(line, "%255s %u %u %u %u %u", name, &b.total, &b.run, &b.exact, &b.lenient, &b.native);

		b.has_native = fields == 6;

		if (fields >= 5)
			baseline[name] = b;
	}

	fclose(f);

	return true;
}

static bool save_baseline(const std::string& path, const SsSuite& suite, const std::map<std::string, SsCounts>& results)
{
	FILE* f = fopen(path.c_str(), "w");

	if (!f)
		return false;

	fprintf(f, "# %s baseline: what devcast passes of the single-step vectors, per file.\n", suite.name);
	fprintf(f, "# The test fails when a file runs or passes fewer vectors than listed here%s.\n",
		suite.native ? ", or has fewer whose instruction is\n# compiled to native code" : "");
	fprintf(f, "# Written by the test's --update-baseline, see tests/README.md.\n");
	fprintf(f, "#\n");
	fprintf(f, "# %-24s %5s %5s %5s %7s%s\n", "file", "total", "run", "exact", "lenient", suite.native ? " native" : "");

	for (std::map<std::string, SsCounts>::const_iterator it = results.begin(); it != results.end(); ++it)
	{
		fprintf(f, "%-26s %5u %5u %5u %7u", it->first.c_str(), it->second.total, it->second.run, it->second.exact, it->second.lenient);

		if (suite.native)
			fprintf(f, " %6u", it->second.native);

		fprintf(f, "\n");
	}

	fclose(f);

	return true;
}

static void print_bus(const char* title, const std::vector<SsBusAccess>& list)
{
	fprintf(report, "    %s", title);

	if (list.empty())
		fprintf(report, " -");

	for (size_t i = 0; i < list.size(); i++)
	{
		const SsBusAccess& a = list[i];
		const char* kind = a.kind == SSB_FETCH ? "fetch" : a.kind == SSB_READ ? "read" : "write";

		fprintf(report, "%s %s%u @%08X=%0*llX", i ? "," : "", kind, a.size * 8, a.addr, a.size * 2, (unsigned long long)a.value);
	}

	fprintf(report, "\n");
}

static void print_vector(const std::string& file, u32 index, const SsFilePolicy& policy, const SsTest& test,
	const SsState& actual, const char* outcome, const char* why)
{
	fprintf(report, "%s %s #%u (%s, op %04X): %s\n", outcome, file.c_str(), index, policy.mnemonic, ss_opcode(test), why);
	fprintf(report, "    opcodes at pc: %04X %04X %04X %04X, elsewhere: %04X\n", test.opcodes.op[0], test.opcodes.op[1],
		test.opcodes.op[2], test.opcodes.op[3], test.opcodes.op[4]);

	print_bus("expected bus:", ss_mem_expected());
	print_bus("actual bus:  ", ss_mem_log());

	if (test_guard_msgbox_count() != 0)
		fprintf(report, "    msgboxf (%u): %s\n", test_guard_msgbox_count(), test_guard_last_msgbox());

	ss_state_print(report, test.initial.s, test.final_.s, actual);
}

static int usage(const SsSuite& suite, const SsOptions& defaults)
{
	fprintf(stderr,
		"%s: runs the SH4 single-step vectors and compares the result with a baseline\n"
		"\n"
		"  --vectors <dir>      vector files (*" SS_FILE_SUFFIX "), default %s\n"
		"  --baseline <file>    baseline file, default %s\n"
		"  --update-baseline    write the counts of this run into the baseline file (all files have to be run)\n"
		"  --strict             counts above the baseline fail as well: the baseline has to be the one of this\n"
		"                       build. ctest runs the test this way\n"
		"  --file <pattern>     only the files whose name or instruction contains <pattern>, or matches it if it\n"
		"                       has * or ?\n"
		"  --vector <N>         only vector N (0 based) of each file, no baseline check\n"
		"  --unfilter <reasons> run the vectors that are filtered for these reasons as well (comma separated, or\n"
		"                       all), no baseline check. To see what the emulator does with them\n"
		"  --verbose            for every vector that is not an exact pass: the state before, expected and actual,\n"
		"                       and the bus activity\n"
		"  --emu-output         do not hide what the emulator code prints\n",
		suite.name, defaults.vectors_dir.c_str(), defaults.baseline.c_str());

	return 2;
}

int ss_suite_main(int argc, char** argv, const SsSuite& suite)
{
	SsOptions opt;

	opt.vectors_dir = suite.vectors_dir;
	opt.baseline = suite.baseline;
	opt.vector = -1;
	opt.update_baseline = false;
	opt.strict = false;
	opt.verbose = false;
	opt.emu_output = false;
	opt.unfilter = 0;

	const SsOptions defaults = opt;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		bool has_value = i + 1 < argc;

		if (arg == "--vectors" && has_value)
			opt.vectors_dir = argv[++i];
		else if (arg == "--baseline" && has_value)
			opt.baseline = argv[++i];
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
			return usage(suite, defaults);
	}

	bool all_files = opt.file_pattern.empty() && opt.vector < 0;
	bool gated = opt.vector < 0 && opt.unfilter == 0;	//the counts can be compared with the baseline

	if (opt.update_baseline && (!all_files || !gated))
	{
		fprintf(stderr, "--update-baseline needs a run of all files as the policy filters them\n");
		return 2;
	}

	std::vector<std::string> files;

	if (!ss_list_files(opt.vectors_dir, files) || files.empty())
	{
		fprintf(stderr, "no vector files (*" SS_FILE_SUFFIX ") in %s\n", opt.vectors_dir.c_str());
		fprintf(stderr, "run: git submodule update --init tests/vendor/sh4-tests\n");
		return 2;
	}

	std::map<std::string, SsBaseline> baseline;
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

	the_suite = &suite;

	sh4_env_init();
	ss_mem_install();

	if (suite.init)
		suite.init();

	test_guard_init(SS_WATCHDOG_MS);

	std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

	fprintf(report, "%s: vectors %s\n\n", suite.name, opt.vectors_dir.c_str());
	fprintf(report, "%-26s %-24s %5s %5s %5s %7s %6s%s  %s\n", "file", "instruction", "total", "run", "exact", "lenient", "failed",
		suite.native ? " native" : "", "filtered");

	std::map<std::string, SsCounts> results;
	SsCounts sum;
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

		const SsSuiteKnown* known = suite_known(file);
		bool may_crash = (policy->flags & SSP_MAY_CRASH) || (known && known->may_crash);

		SsCounts counts;
		std::string first;
		u32 unannounced_crashes = 0;

		for (size_t ti = 0; ti < tests.size(); ti++)
		{
			if (opt.vector >= 0 && (size_t)opt.vector != ti)
				continue;

			const SsTest& test = tests[ti];

			counts.total++;

			SsFilterReason reason = ss_policy_filter(*policy, test, opt.unfilter);

			if (reason != SSF_NONE)
			{
				counts.filtered[reason]++;

				if (opt.vector >= 0)
					fprintf(report, "%s #%u is filtered: %s (%s)\n", file.c_str(), (u32)ti, ss_filter_name(reason), ss_filter_description(reason));

				continue;
			}

			counts.run++;

			ss_mem_begin(test, policy->read_size, policy->write_size, suite.ordered_fetches);
			ss_state_load(test.initial.s);

			TestGuardResult guard = test_guard_run(&run_thunk, (void*)&test);

			SsState actual;
			ss_state_save(actual);

			if (guard == TGR_OK && test_guard_msgbox_count() != 0)
				counts.notified++;

			if (suite.native && suite.native())
				counts.native++;

			bool bus_ok = ss_mem_end();

			SsOutcome outcome;
			char why[1536];

			why[0] = 0;

			if (guard == TGR_TIMEOUT)
			{
				outcome = SSO_HANG;
				snprintf(why, sizeof(why), "hang: %s", test_guard_message());
			}
			else if (guard != TGR_OK)
			{
				outcome = SSO_CRASH;
				snprintf(why, sizeof(why), "crash (%s): %s", test_guard_result_name(guard), test_guard_message());
			}
			else if (!bus_ok)
			{
				outcome = SSO_FAIL;
				snprintf(why, sizeof(why), "bus: %s", ss_mem_error());
			}
			else
			{
				SsCompare cmp = ss_state_compare(test.final_.s, actual);
				int word = cmp.lenient ? cmp.first_exact : cmp.first_lenient;

				outcome = cmp.exact ? SSO_PASS_EXACT : cmp.lenient ? SSO_PASS_LENIENT : SSO_FAIL;

				if (word >= 0)
				{
					snprintf(why, sizeof(why), "state: %s is %08X, expected %08X%s", ss_state_word_name(word), ((const u32*)&actual)[word],
						((const u32*)&test.final_.s)[word], cmp.lenient ? " (lenient: ok)" : "");
				}
			}

			switch (outcome)
			{
			case SSO_PASS_EXACT:
				counts.exact++;
				counts.lenient++;
				break;

			case SSO_PASS_LENIENT:
				counts.lenient++;
				break;

			case SSO_CRASH:
				counts.crash++;
				if (!may_crash)
					unannounced_crashes++;
				break;

			case SSO_HANG:
				counts.hang++;
				break;

			case SSO_FAIL:
				break;
			}

			if (outcome != SSO_PASS_EXACT)
			{
				const char* tag = outcome == SSO_PASS_LENIENT ? "LENIENT" : outcome == SSO_FAIL ? "FAIL" : outcome == SSO_CRASH ? "CRASH" : "HANG";

				if (first.empty() || (first.compare(0, 7, "LENIENT") == 0 && outcome != SSO_PASS_LENIENT))
				{
					char text[1700];
					snprintf(text, sizeof(text), "%s #%u op %04X: %s", tag, (u32)ti, ss_opcode(test), why);
					first = text;
				}

				if (opt.verbose)
					print_vector(file, (u32)ti, *policy, test, actual, tag, why);
			}
			else if (opt.vector >= 0)
			{
				fprintf(report, "%s #%u (%s, op %04X): pass\n", file.c_str(), (u32)ti, policy->mnemonic, ss_opcode(test));
			}
		}

		//the file's line
		std::string filtered;

		for (u32 r = 1; r < SSF_COUNT; r++)
		{
			if (counts.filtered[r] == 0)
				continue;

			char text[64];
			snprintf(text, sizeof(text), "%s%s=%u", filtered.empty() ? "" : " ", ss_filter_name((SsFilterReason)r), counts.filtered[r]);
			filtered += text;
		}

		char native[16];

		native[0] = 0;

		if (suite.native)
			snprintf(native, sizeof(native), " %6u", counts.native);

		fprintf(report, "%-26s %-24s %5u %5u %5u %7u %6u%s  %s\n", file.c_str(), policy->mnemonic, counts.total, counts.run, counts.exact,
			counts.lenient, counts.failed(), native, filtered.empty() ? "-" : filtered.c_str());

		if (!first.empty() && !opt.verbose)
			fprintf(report, "    first: %s\n", first.c_str());

		if (counts.crash || counts.hang)
			fprintf(report, "    crashed: %u, hung: %u\n", counts.crash, counts.hang);

		results[file] = counts;
		sum.add(counts);

		//the gate
		char text[512];

		if (unannounced_crashes)
		{
			snprintf(text, sizeof(text), "FAIL %s: %u vectors crash and neither the file's policy nor the suite says they may", file.c_str(), unannounced_crashes);
			notes.push_back(text);
			problems++;
		}

		if (counts.hang)
		{
			snprintf(text, sizeof(text), "FAIL %s: %u vectors hang", file.c_str(), counts.hang);
			notes.push_back(text);
			problems++;
		}

		if (opt.vector >= 0)
		{
			if (counts.failed())
				problems++;

			continue;
		}

		if (opt.update_baseline || !gated)
			continue;

		std::map<std::string, SsBaseline>::const_iterator base = baseline.find(file);

		if (base == baseline.end())
		{
			snprintf(text, sizeof(text), "FAIL %s: not in the baseline", file.c_str());
			notes.push_back(text);
			problems++;
			continue;
		}

		const SsBaseline& b = base->second;

		if (counts.total != b.total)
		{
			snprintf(text, sizeof(text), "FAIL %s: has %u vectors, the baseline was made with %u", file.c_str(), counts.total, b.total);
			notes.push_back(text);
			problems++;
			continue;
		}

#define GATE(what, now, was) \
		if ((now) < (was)) \
		{ \
			snprintf(text, sizeof(text), "FAIL %s: " what " %u, baseline %u", file.c_str(), (now), (was)); \
			notes.push_back(text); \
			problems++; \
		} \
		else if ((now) > (was)) \
		{ \
			snprintf(text, sizeof(text), "%s: %s " what " %u, baseline %u", opt.strict ? "FAIL above the baseline" : "above the baseline", \
				file.c_str(), (now), (was)); \
			notes.push_back(text); \
			improvements++; \
		}

		GATE("run", counts.run, b.run);
		GATE("pass-exact", counts.exact, b.exact);
		GATE("pass-lenient", counts.lenient, b.lenient);

		if (suite.native && !b.has_native)
		{
			snprintf(text, sizeof(text), "FAIL %s: the baseline has no native count", file.c_str());
			notes.push_back(text);
			problems++;
		}
		else if (suite.native)
		{
			GATE("native", counts.native, b.native);
		}

#undef GATE
	}

	if (results.empty() && problems == 0)
	{
		fprintf(report, "no vector file matches %s\n", opt.file_pattern.c_str());
		return 2;
	}

	if (all_files && gated && !opt.update_baseline)
	{
		for (std::map<std::string, SsBaseline>::const_iterator it = baseline.begin(); it != baseline.end(); ++it)
		{
			if (results.find(it->first) == results.end())
			{
				notes.push_back("FAIL " + it->first + ": in the baseline, but there is no such vector file");
				problems++;
			}
		}
	}

	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

	u32 filtered_total = sum.total - sum.run;

	fprintf(report, "\n%u files, %u vectors in %.2f s\n", (u32)results.size(), sum.total, seconds);
	fprintf(report, "  run          %6u\n", sum.run);
	fprintf(report, "  pass-exact   %6u\n", sum.exact);
	fprintf(report, "  pass-lenient %6u\n", sum.lenient);
	fprintf(report, "  failed       %6u  (crashed %u, hung %u)\n", sum.failed(), sum.crash, sum.hang);
	if (suite.native)
		fprintf(report, "  native       %6u  vectors whose instruction is compiled to native code, the others are handed to the interpreter\n", sum.native);
	if (sum.notified)
		fprintf(report, "  msgboxf      %6u  vectors during which the emulator code raised a notification and carried on\n", sum.notified);
	fprintf(report, "  filtered     %6u\n", filtered_total);

	for (u32 r = 1; r < SSF_COUNT; r++)
	{
		if (sum.filtered[r])
			fprintf(report, "    %-12s %6u  %s\n", ss_filter_name((SsFilterReason)r), sum.filtered[r], ss_filter_description((SsFilterReason)r));
	}

	//what is known about the files that have failures
	bool header = false;

	for (std::map<std::string, SsCounts>::const_iterator it = results.begin(); it != results.end(); ++it)
	{
		const SsCounts& c = it->second;

		if (c.run == c.exact)
			continue;

		const SsFilePolicy* policy = ss_policy_find(it->first);
		const SsSuiteKnown* known = suite_known(it->first);

		if (!header)
		{
			fprintf(report, "\nfiles with vectors that are not an exact pass:\n");
			header = true;
		}

		fprintf(report, "  %s (%s): %u failed, %u lenient only\n", it->first.c_str(), policy->mnemonic, c.failed(), c.lenient - c.exact);

		if (policy->known)
			fprintf(report, "    %s\n", policy->known);

		if (known && known->known)
			fprintf(report, "    %s\n", known->known);

		if (!policy->known && !(known && known->known))
			fprintf(report, "    NOT EXPLAINED by the file's policy or by the suite\n");
	}

	if (!notes.empty())
	{
		fprintf(report, "\n");

		for (size_t i = 0; i < notes.size(); i++)
			fprintf(report, "%s\n", notes[i].c_str());
	}

	if (opt.update_baseline)
	{
		if (!save_baseline(opt.baseline, suite, results))
		{
			fprintf(report, "\ncannot write %s\n", opt.baseline.c_str());
			return 2;
		}

		fprintf(report, "\nbaseline written to %s\n", opt.baseline.c_str());
	}
	else if (improvements)
	{
		fprintf(report, "\n%u counts are above the baseline%s: run with --update-baseline and commit %s\n", improvements,
			opt.strict ? ", which fails with --strict" : "", opt.baseline.c_str());

		if (opt.strict)
			problems++;
	}

	if (!gated)
		fprintf(report, "\nnot compared with the baseline\n");

	fprintf(report, "\n%s: %s\n", suite.name, problems ? "FAIL" : "PASS");
	fflush(report);

	return problems ? 1 : 0;
}
