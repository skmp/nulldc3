#include "dsp_suite.h"

#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

struct DspCheck
{
	std::string name;
	uint64_t total;
	uint64_t pass;
};

struct DspBaselineRow
{
	std::string test;
	std::string check;
	uint64_t total;
	uint64_t pass;
};

static std::string test_name;
static DspOptions options;
static std::vector<DspCheck> checks;
static std::vector<std::string> problems;
static std::vector<DspBaselineRow> baseline;
static std::chrono::steady_clock::time_point started;

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
		unsigned long long total, pass;

		if (line[0] == '#')
			continue;

		if (sscanf(line, "%127s %127s %llu %llu", test, check, &total, &pass) == 4)
		{
			DspBaselineRow row;

			row.test = test;
			row.check = check;
			row.total = total;
			row.pass = pass;
			baseline.push_back(row);
		}
	}

	fclose(f);

	return true;
}

//The rows of this test are replaced where they were, the ones of the other tests stay
static bool save_baseline(const std::string& path)
{
	std::vector<DspBaselineRow> rows;
	bool placed = false;

	for (size_t i = 0; i <= baseline.size(); i++)
	{
		bool mine = i < baseline.size() && baseline[i].test == test_name;

		if (!placed && (mine || i == baseline.size()))
		{
			for (size_t c = 0; c < checks.size(); c++)
			{
				DspBaselineRow row;

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

	fprintf(f, "# What the AICA DSP core passes of its tests, per test and check.\n");
	fprintf(f, "# A test fails when one of its checks passes fewer cases than listed here, or has another number of\n");
	fprintf(f, "# cases. KNOWN_FAILURES.md has the reason for every check that is below its total.\n");
	fprintf(f, "# Written by --update-baseline of the tests, see README.md.\n");
	fprintf(f, "#\n");
	fprintf(f, "# %-26s %-28s %12s %12s\n", "test", "check", "total", "pass");

	for (size_t i = 0; i < rows.size(); i++)
	{
		fprintf(f, "%-28s %-28s %12llu %12llu\n", rows[i].test.c_str(), rows[i].check.c_str(),
			(unsigned long long)rows[i].total, (unsigned long long)rows[i].pass);
	}

	fclose(f);

	return true;
}

bool dsp_suite_options(int argc, char** argv, int first, const char* usage_head, DspOptions& opt)
{
	const DspOptions defaults = opt;

	opt.update_baseline = false;
	opt.strict = false;
	opt.require_data = false;
	opt.verbose = false;

	bool ok = first <= argc;

	for (int i = first; i < argc && ok; i++)
	{
		std::string arg = argv[i];
		bool has_value = i + 1 < argc;

		if (arg == "--caique" && has_value)
			opt.data_dir = argv[++i];
		else if (arg == "--baseline" && has_value)
			opt.baseline = argv[++i];
		else if (arg == "--update-baseline")
			opt.update_baseline = true;
		else if (arg == "--strict")
			opt.strict = true;
		else if (arg == "--require-data")
			opt.require_data = true;
		else if (arg == "--verbose")
			opt.verbose = true;
		else
			ok = false;
	}

	if (!ok)
	{
		fprintf(stderr, "%s\n\n", usage_head);
		fprintf(stderr,
			"  --caique <dir>       the caique-rtl checkout with the console captures, default %s\n"
			"  --baseline <file>    baseline file, default %s\n"
			"  --update-baseline    write the counts of this run into the baseline file\n"
			"  --strict             passes above the baseline fail as well: the baseline has to be the one of this\n"
			"                       build. ctest runs the test this way\n"
			"  --require-data       test data that is not there is an error. Without it the test says that it is\n"
			"                       skipped and exits with %d\n"
			"  --verbose            more of what does not pass\n",
			defaults.data_dir.empty() ? "(none)" : defaults.data_dir.c_str(), defaults.baseline.c_str(), DSP_EXIT_SKIPPED);
	}

	return ok;
}

bool dsp_suite_begin(const char* name, const DspOptions& opt)
{
	test_name = name;
	options = opt;

	if (!load_baseline(opt.baseline) && !opt.update_baseline)
	{
		fprintf(stderr, "cannot read the baseline %s\n", opt.baseline.c_str());
		return false;
	}

	started = std::chrono::steady_clock::now();

	return true;
}

const DspOptions& dsp_suite_opt()
{
	return options;
}

void dsp_count(const std::string& check, uint64_t total, uint64_t pass)
{
	size_t i = 0;

	while (i < checks.size() && checks[i].name != check)
		i++;

	if (i == checks.size())
	{
		DspCheck c;

		c.name = check;
		c.total = 0;
		c.pass = 0;
		checks.push_back(c);
	}

	checks[i].total += total;
	checks[i].pass += pass;
}

void dsp_problem(const char* fmt, ...)
{
	char text[1536];
	va_list args;

	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);

	problems.push_back(text);
}

void dsp_note(const char* fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);

	printf("\n");
}

static const DspBaselineRow* baseline_find(const std::string& check)
{
	for (size_t i = 0; i < baseline.size(); i++)
	{
		if (baseline[i].test == test_name && baseline[i].check == check)
			return &baseline[i];
	}

	return 0;
}

int dsp_suite_end()
{
	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	bool failed = false;
	bool above = false;

	printf("\n%-28s %12s %12s   baseline\n", "check", "total", "pass");

	for (size_t i = 0; i < checks.size(); i++)
	{
		const DspBaselineRow* row = baseline_find(checks[i].name);
		const char* verdict = "";

		if (options.update_baseline)
			verdict = "";
		else if (!row)
		{
			verdict = "  NOT IN THE BASELINE";
			failed = true;
		}
		else if (row->total != checks[i].total)
		{
			verdict = "  ANOTHER NUMBER OF CASES";
			failed = true;
		}
		else if (checks[i].pass < row->pass)
		{
			verdict = "  WORSE";
			failed = true;
		}
		else if (checks[i].pass > row->pass)
		{
			verdict = "  better";
			above = true;
		}

		if (row && !options.update_baseline)
		{
			printf("%-28s %12llu %12llu   %llu / %llu%s\n", checks[i].name.c_str(), (unsigned long long)checks[i].total,
				(unsigned long long)checks[i].pass, (unsigned long long)row->pass, (unsigned long long)row->total, verdict);
		}
		else
		{
			printf("%-28s %12llu %12llu%s\n", checks[i].name.c_str(), (unsigned long long)checks[i].total,
				(unsigned long long)checks[i].pass, verdict);
		}
	}

	if (!options.update_baseline)
	{
		for (size_t i = 0; i < baseline.size(); i++)
		{
			if (baseline[i].test != test_name)
				continue;

			bool counted = false;

			for (size_t c = 0; c < checks.size(); c++)
				counted |= checks[c].name == baseline[i].check;

			if (!counted)
			{
				printf("%-28s was not counted, the baseline has it\n", baseline[i].check.c_str());
				failed = true;
			}
		}
	}

	for (size_t i = 0; i < problems.size(); i++)
		printf("PROBLEM: %s\n", problems[i].c_str());

	printf("\n%.1f s\n", seconds);

	if (!problems.empty())
	{
		printf("%s: FAIL\n", test_name.c_str());
		return DSP_EXIT_FAIL;
	}

	if (options.update_baseline)
	{
		if (!save_baseline(options.baseline))
		{
			printf("cannot write the baseline %s\n", options.baseline.c_str());
			return DSP_EXIT_ERROR;
		}

		printf("%s: baseline written to %s\n", test_name.c_str(), options.baseline.c_str());
		return DSP_EXIT_PASS;
	}

	if (failed)
	{
		printf("%s: FAIL\n", test_name.c_str());
		return DSP_EXIT_FAIL;
	}

	if (above)
	{
		if (options.strict)
		{
			printf("%s: FAIL (better than the baseline: update it with --update-baseline and commit it)\n", test_name.c_str());
			return DSP_EXIT_FAIL;
		}

		printf("%s: PASS (better than the baseline: update it with --update-baseline)\n", test_name.c_str());
		return DSP_EXIT_PASS;
	}

	printf("%s: PASS\n", test_name.c_str());

	return DSP_EXIT_PASS;
}

int dsp_suite_skip(const char* name, const DspOptions& opt, const std::string& reason)
{
	if (opt.require_data)
	{
		printf("%s: cannot run, %s\n", name, reason.c_str());
		return DSP_EXIT_ERROR;
	}

	printf("%s: SKIPPED, %s\n", name, reason.c_str());

	return DSP_EXIT_SKIPPED;
}

bool dsp_read_file(const std::string& path, std::vector<uint8_t>& data)
{
	FILE* f = fopen(path.c_str(), "rb");

	if (!f)
		return false;

	data.clear();

	uint8_t chunk[65536];
	size_t got;

	while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
		data.insert(data.end(), chunk, chunk + got);

	fclose(f);

	return true;
}

bool dsp_read_lines(const std::string& path, std::vector<std::string>& lines)
{
	std::vector<uint8_t> data;

	if (!dsp_read_file(path, data))
		return false;

	lines.clear();

	std::string line;

	for (size_t i = 0; i < data.size(); i++)
	{
		if (data[i] == '\n')
		{
			lines.push_back(line);
			line.clear();
		}
		else if (data[i] != '\r')
			line += (char)data[i];
	}

	if (!line.empty())
		lines.push_back(line);

	return true;
}
