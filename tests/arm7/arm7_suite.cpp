#include "arm7_suite.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <dirent.h>

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

//A guarded call that takes this long is a hang. The longest job of the suites runs for a fraction of a second
#define ARM7_WATCHDOG_MS 30000

struct Arm7Check
{
	std::string name;
	u32 total;
	u32 pass;
};

struct Arm7BaselineRow
{
	std::string test;
	std::string check;
	u32 total;
	u32 pass;
};

FILE* arm7_report;

static std::string test_name;
static Arm7Options options;
static std::vector<Arm7Check> checks;
static std::vector<std::string> problems;
static std::vector<Arm7BaselineRow> baseline;
static std::chrono::steady_clock::time_point started;

//* and ? wildcards
static bool glob(const char* pattern, const char* text)
{
	if (*pattern == 0)
		return *text == 0;

	if (*pattern == '*')
		return glob(pattern + 1, text) || (*text != 0 && glob(pattern, text + 1));

	return *text != 0 && (*pattern == '?' || *pattern == *text) && glob(pattern + 1, text + 1);
}

static bool load_baseline(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "r");

	if (!f)
		return false;

	char line[512];

	while (fgets(line, sizeof(line), f))
	{
		char test[128];
		char check[128];
		Arm7BaselineRow row;

		if (line[0] == '#')
			continue;

		if (sscanf(line, "%127s %127s %u %u", test, check, &row.total, &row.pass) == 4)
		{
			row.test = test;
			row.check = check;
			baseline.push_back(row);
		}
	}

	fclose(f);

	return true;
}

//The rows of this test are replaced where they were, the ones of the other tests stay
static bool save_baseline(const std::string& path)
{
	std::vector<Arm7BaselineRow> rows;
	bool placed = false;

	for (size_t i = 0; i <= baseline.size(); i++)
	{
		bool mine = i < baseline.size() && baseline[i].test == test_name;

		if (!placed && (mine || i == baseline.size()))
		{
			for (size_t c = 0; c < checks.size(); c++)
			{
				Arm7BaselineRow row;

				row.test = test_name;
				row.check = checks[c].name;
				row.total = checks[c].total;
				row.pass = checks[c].pass;
				rows.push_back(row);
			}

			placed = true;
		}

		if (i < baseline.size() && !mine)
			rows.push_back(baseline[i]);
	}

	FILE* f = fopen(path.c_str(), "w");

	if (!f)
		return false;

	fprintf(f, "# What devcast's ARM7 interpreter passes of the ARM7 tests, per test and check.\n");
	fprintf(f, "# A test fails when one of its checks passes fewer cases than listed here. KNOWN_FAILURES.md has the\n");
	fprintf(f, "# reason for every check that is below its total.\n");
	fprintf(f, "# Written by --update-baseline of the tests, see README.md.\n");
	fprintf(f, "#\n");
	fprintf(f, "# %-22s %-20s %5s %5s\n", "test", "check", "total", "pass");

	for (size_t i = 0; i < rows.size(); i++)
		fprintf(f, "%-24s %-20s %5u %5u\n", rows[i].test.c_str(), rows[i].check.c_str(), rows[i].total, rows[i].pass);

	fclose(f);

	return true;
}

bool arm7_suite_options(int argc, char** argv, int first, const char* usage_head, const char* data_option,
	const char* select_option, Arm7Options& opt)
{
	const Arm7Options defaults = opt;

	opt.update_baseline = false;
	opt.verbose = false;
	opt.emu_output = false;

	bool ok = first <= argc;

	for (int i = first; i < argc && ok; i++)
	{
		std::string arg = argv[i];
		bool has_value = i + 1 < argc;

		if (arg == data_option && has_value)
			opt.data_dir = argv[++i];
		else if (arg == select_option && has_value)
			opt.select = argv[++i];
		else if (arg == "--baseline" && has_value)
			opt.baseline = argv[++i];
		else if (arg == "--update-baseline")
			opt.update_baseline = true;
		else if (arg == "--verbose")
			opt.verbose = true;
		else if (arg == "--emu-output")
			opt.emu_output = true;
		else
			ok = false;
	}

	if (ok && opt.update_baseline && !opt.select.empty())
	{
		fprintf(stderr, "--update-baseline needs a run of everything\n\n");
		ok = false;
	}

	if (!ok)
	{
		fprintf(stderr,
			"%s\n"
			"\n"
			"  %s <dir>%*s the test data, default %s\n"
			"  %s <pattern>%*s only what has <pattern> in its name, or matches it if it has * or ?,\n"
			"                       no baseline check\n"
			"  --baseline <file>    baseline file, default %s\n"
			"  --update-baseline    write the counts of this run into the baseline file\n"
			"  --verbose            every case that does not pass, not only the first of each check\n"
			"  --emu-output         do not hide what the emulator code prints\n",
			usage_head,
			data_option, (int)(14 - strlen(data_option)), "", defaults.data_dir.empty() ? "(none)" : defaults.data_dir.c_str(),
			select_option, (int)(10 - strlen(select_option)), "",
			defaults.baseline.c_str());
	}

	return ok;
}

bool arm7_suite_begin(const char* name, const Arm7Options& opt)
{
	test_name = name;
	options = opt;

	if (!load_baseline(opt.baseline) && !opt.update_baseline && opt.select.empty())
	{
		fprintf(stderr, "cannot read the baseline %s\n", opt.baseline.c_str());
		return false;
	}

	arm7_report = stdout;

	if (!opt.emu_output)
	{
		fflush(stdout);
		arm7_report = fdopen(dup(fileno(stdout)), "w");

		if (!arm7_report || !freopen(NULL_DEVICE, "w", stdout))
		{
			fprintf(stderr, "cannot redirect the emulator output\n");
			return false;
		}
	}

	test_guard_echo_msgbox(opt.emu_output);
	test_guard_init(ARM7_WATCHDOG_MS);

	started = std::chrono::steady_clock::now();

	return true;
}

const Arm7Options& arm7_suite_opt()
{
	return options;
}

bool arm7_selected(const std::string& name)
{
	if (options.select.empty())
		return true;

	if (options.select.find_first_of("*?") == std::string::npos)
		return name.find(options.select) != std::string::npos;

	return glob(options.select.c_str(), name.c_str());
}

void arm7_count_cases(const std::string& check, u32 total, u32 pass)
{
	size_t i = 0;

	while (i < checks.size() && checks[i].name != check)
		i++;

	if (i == checks.size())
	{
		Arm7Check c;

		c.name = check;
		c.total = 0;
		c.pass = 0;
		checks.push_back(c);
	}

	checks[i].total += total;
	checks[i].pass += pass;
}

void arm7_count(const std::string& check, bool pass)
{
	arm7_count_cases(check, 1, pass ? 1 : 0);
}

void arm7_problem(const char* fmt, ...)
{
	char text[1536];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	problems.push_back(text);
}

TestGuardResult arm7_guarded(const std::string& what, TestGuardFP* fn, void* arg)
{
	TestGuardResult result = test_guard_run(fn, arg);

	if (result == TGR_TIMEOUT)
		arm7_problem("%s hangs: %s", what.c_str(), test_guard_message());
	else if (result != TGR_OK)
		arm7_problem("%s crashes (%s): %s", what.c_str(), test_guard_result_name(result), test_guard_message());

	return result;
}

static const Arm7BaselineRow* baseline_find(const std::string& check)
{
	for (size_t i = 0; i < baseline.size(); i++)
	{
		if (baseline[i].test == test_name && baseline[i].check == check)
			return &baseline[i];
	}

	return 0;
}

int arm7_suite_end()
{
	bool gated = options.select.empty() && !options.update_baseline;
	u32 improvements = 0;
	u32 failing = 0;

	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

	fprintf(arm7_report, "\n%s, %.2f s\n", test_name.c_str(), seconds);
	fprintf(arm7_report, "  %-20s %5s %5s %8s\n", "check", "cases", "pass", "baseline");

	for (size_t i = 0; i < checks.size(); i++)
	{
		const Arm7Check& c = checks[i];
		const Arm7BaselineRow* b = baseline_find(c.name);

		fprintf(arm7_report, "  %-20s %5u %5u ", c.name.c_str(), c.total, c.pass);

		if (b && options.select.empty())
			fprintf(arm7_report, "%8u\n", b->pass);
		else
			fprintf(arm7_report, "%8s\n", "-");

		if (c.pass != c.total)
			failing++;

		if (!gated)
			continue;

		if (!b)
			arm7_problem("%s: not in the baseline", c.name.c_str());
		else if (c.total != b->total)
			arm7_problem("%s: has %u cases, the baseline was made with %u", c.name.c_str(), c.total, b->total);
		else if (c.pass < b->pass)
			arm7_problem("%s: %u pass, baseline %u", c.name.c_str(), c.pass, b->pass);
		else if (c.pass > b->pass)
			improvements++;
	}

	if (gated)
	{
		for (size_t i = 0; i < baseline.size(); i++)
		{
			const Arm7BaselineRow& b = baseline[i];
			bool counted = false;

			for (size_t c = 0; c < checks.size(); c++)
				counted = counted || checks[c].name == b.check;

			if (b.test == test_name && !counted)
				arm7_problem("%s: in the baseline, but the run has no such check", b.check.c_str());
		}
	}

	if (checks.empty() && problems.empty())
	{
		fprintf(arm7_report, "\nnothing matches %s\n", options.select.c_str());
		return ARM7_EXIT_ERROR;
	}

	//a selection has no baseline to hold it against: it passes when everything in it does
	if (!options.select.empty() && failing)
		arm7_problem("%u checks have cases that do not pass", failing);

	if (!problems.empty())
		fprintf(arm7_report, "\n");

	for (size_t i = 0; i < problems.size(); i++)
		fprintf(arm7_report, "FAIL %s\n", problems[i].c_str());

	if (options.update_baseline)
	{
		if (!save_baseline(options.baseline))
		{
			fprintf(arm7_report, "\ncannot write %s\n", options.baseline.c_str());
			return ARM7_EXIT_ERROR;
		}

		fprintf(arm7_report, "\nbaseline written to %s\n", options.baseline.c_str());
	}
	else if (improvements)
	{
		fprintf(arm7_report, "\n%u checks are above the baseline: run with --update-baseline and commit %s\n", improvements,
			options.baseline.c_str());
	}

	fprintf(arm7_report, "\n%s\n", problems.empty() ? "PASS" : "FAIL");
	fflush(arm7_report);

	return problems.empty() ? ARM7_EXIT_PASS : ARM7_EXIT_FAIL;
}

int arm7_suite_skip(const char* name, const std::string& reason)
{
	printf("SKIPPED: %s: %s\n", name, reason.c_str());

	return ARM7_EXIT_SKIPPED;
}

bool arm7_read_file(const std::string& path, std::vector<u8>& data)
{
	data.clear();

	FILE* f = fopen(path.c_str(), "rb");

	if (!f)
		return false;

	u8 buffer[65536];
	size_t count;

	while ((count = fread(buffer, 1, sizeof(buffer), f)) > 0)
		data.insert(data.end(), buffer, buffer + count);

	bool ok = !ferror(f);

	fclose(f);

	return ok;
}

bool arm7_list_files(const std::string& dir, const char* suffix, std::vector<std::string>& names)
{
	names.clear();

	DIR* d = opendir(dir.c_str());

	if (!d)
		return false;

	const size_t length = strlen(suffix);

	while (dirent* e = readdir(d))
	{
		std::string name = e->d_name;

		if (name.size() > length && name.compare(name.size() - length, length, suffix) == 0)
			names.push_back(name.substr(0, name.size() - length));
	}

	closedir(d);

	std::sort(names.begin(), names.end());

	return true;
}

u32 arm7_le32(const std::vector<u8>& data, size_t offset)
{
	if (offset + 4 > data.size())
		return 0;

	return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | ((u32)data[offset + 3] << 24);
}
