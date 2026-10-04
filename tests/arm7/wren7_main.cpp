/*
	arm7.wren7.<suite>: console jobs of wren7-rtl that need nothing but wave RAM and the done stub, run on the
	ARM7 interpreter and compared with what the console answered (wren7_job.h).

	  vendor      the programs of arm7di-tests-dreamcast
	                result   the job ends as on the console, with the console's r1 (the checks that failed)
	                cpsr     CPSR at the end is the console's
	  qemu_diff   4 batches of 1000 random instructions. A case leaves r0-r12, r14 and CPSR in a table, some
	              cases also write a 128 byte scratch region
	                exit               the batch ends as on the console
	                <batch>:<class>    the case's registers, CPSR under the case's mask and scratch region are
	                                   the console's
	  mulsem      1024 multiplier inputs (Rm, Rs, Rn, flags), 6 words per record
	                exit, then one check per word: MLA with Rd == Rm, MULS result and flags, MLAS result and
	                flags, MULS with Rd == Rm
	  bringup     a countdown kernel, run 3 times in a row, and one that runs into the job's timeout
	                result        the job ends as on the console, with the console's r1, r12 and r15
	                reset-state   what the reset of the console leaves: r0 as the run before left it, r14 the
	                              address the reset came at, SPSR the CPSR of that moment, CPSR with I and F set

	Only what a job defines is compared: the other registers of the result block hold what earlier jobs left on
	the console.
*/
#include "arm7_suite.h"
#include "wren7_job.h"
#include "arm7_machine.h"

#include <cstring>
#include <map>

struct QemuCase
{
	std::string cls;
	u32 opcode;
	u32 cpsr_mask;
	int region;		//-1: none
};

struct QemuCount
{
	u32 total;
	u32 pass;

	QemuCount() : total(0), pass(0) { }
};

#define QEMU_CASE_SIZE 64
#define QEMU_CASE_WORDS 15		//r0-r12, r14, CPSR
#define QEMU_REGION_SIZE 128

#define MULSEM_INPUTS_ADDR 0x20000
#define MULSEM_INPUT_SIZE 16	//Rm, Rs, Rn, flags
#define MULSEM_RECORD_WORDS 6

static std::map<std::string, std::vector<QemuCase> > qemu_cases;	//per batch
static std::map<std::string, u32> shown;

//Every case that does not pass is printed with --verbose, otherwise the first one of each check
static bool show(const std::string& check)
{
	return shown[check]++ == 0 || arm7_suite_opt().verbose;
}

static std::string describe_end(bool done, const std::vector<u8>& block)
{
	char text[64];

	if (!done)
		return "not done";

	if (arm7_le32(block, WREN7_RES_MAGIC) != WREN7_MAGIC_DONE)
		return "done, no result";

	snprintf(text, sizeof(text), "done, exit %u", arm7_le32(block, WREN7_RES_EXIT));

	return text;
}

static void print_job(const Wren7Job& job, const Wren7Run& run)
{
	fprintf(arm7_report, "%-14s %s", job.name.c_str(), describe_end(run.done, run.readback).c_str());

	if (run.guard != TGR_OK)
		fprintf(arm7_report, " (%s: %s)", test_guard_result_name(run.guard), test_guard_message());

	fprintf(arm7_report, ", %llu instructions; console %s\n", (unsigned long long)run.steps,
		describe_end(job.console_status == "done", job.console).c_str());
}

struct ResultWord
{
	u32 offset;
	const char* name;
};

//Counts one case of <check>: the job ended as on the console and the listed words of its result block are the
//console's
static void check_words(const char* check, const Wren7Job& job, const Wren7Run& run, const ResultWord* words, size_t count)
{
	bool pass = wren7_same_end(job, run);
	bool ended = pass;

	for (size_t i = 0; i < count; i++)
		pass = pass && arm7_le32(run.readback, words[i].offset) == arm7_le32(job.console, words[i].offset);

	arm7_count(check, pass);

	if (pass || !ended || !show(check))
		return;

	fprintf(arm7_report, "    %s %s:", job.name.c_str(), check);

	for (size_t i = 0; i < count; i++)
	{
		u32 console = arm7_le32(job.console, words[i].offset);
		u32 actual = arm7_le32(run.readback, words[i].offset);

		if (console != actual)
			fprintf(arm7_report, " %s is %08X, console %08X;", words[i].name, actual, console);
	}

	fprintf(arm7_report, "\n");
}

static void check_vendor(const Wren7Job& job, const Wren7Run& run)
{
	static const ResultWord result[] = { { WREN7_RES_R0 + 4 * 1, "r1" } };
	static const ResultWord cpsr[] = { { WREN7_RES_CPSR, "CPSR" } };

	check_words("result", job, run, result, 1);
	check_words("cpsr", job, run, cpsr, 1);
}

static bool load_qemu_cases(const Wren7Suite& suite, std::string& error, bool& missing)
{
	for (size_t j = 0; j < suite.jobs.size(); j++)
	{
		const Wren7Job& job = suite.jobs[j];
		std::string path = suite.model_dir + "/tests/qemu_diff/" + job.name + "/cases.tsv";

		FILE* f = fopen(path.c_str(), "r");

		if (!f)
		{
			error = path + " is not there";
			missing = true;
			return false;
		}

		std::vector<QemuCase>& cases = qemu_cases[job.name];
		char line[256];

		while (fgets(line, sizeof(line), f))
		{
			int index;
			char cls[32];
			QemuCase c;

			if (line[0] == '#' || sscanf(line, "%d %31s %x %x %d", &index, cls, &c.opcode, &c.cpsr_mask, &c.region) != 5)
				continue;

			c.cls = cls;
			cases.push_back(c);
		}

		fclose(f);

		//result block, the table of the cases, the scratch regions
		bool fits = job.reads.size() == 3 && job.reads[1].size == cases.size() * QEMU_CASE_SIZE && job.reads[2].size % QEMU_REGION_SIZE == 0;

		for (size_t i = 0; fits && i < cases.size(); i++)
			fits = cases[i].region < (int)(job.reads[2].size / QEMU_REGION_SIZE);

		if (!fits)
		{
			error = path + ": does not fit the regions that job " + job.name + " reads";
			missing = false;
			return false;
		}
	}

	return true;
}

static void check_qemu_diff(const Wren7Job& job, const Wren7Run& run)
{
	static const char* const word_name[QEMU_CASE_WORDS] =
	{
		"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "r14", "CPSR",
	};

	const std::vector<QemuCase>& cases = qemu_cases[job.name];
	bool end = wren7_same_end(job, run);

	arm7_count("exit", end);

	size_t table = job.reads[0].size;
	size_t scratch = table + job.reads[1].size;

	std::map<std::string, QemuCount> counts;	//per check, in the order of their names
	u32 differ = 0;

	for (size_t i = 0; i < cases.size(); i++)
	{
		const QemuCase& c = cases[i];
		std::string check = job.name + ":" + c.cls;
		size_t offset = table + i * QEMU_CASE_SIZE;
		bool pass = end;
		bool print = false;

		for (u32 w = 0; pass && w < QEMU_CASE_WORDS; w++)
		{
			u32 mask = w == QEMU_CASE_WORDS - 1 ? c.cpsr_mask : 0xFFFFFFFF;

			if ((arm7_le32(job.console, offset + 4 * w) & mask) != (arm7_le32(run.readback, offset + 4 * w) & mask))
				pass = false;
		}

		size_t region = scratch + (size_t)(c.region < 0 ? 0 : c.region) * QEMU_REGION_SIZE;

		if (pass && c.region >= 0 && memcmp(&job.console[region], &run.readback[region], QEMU_REGION_SIZE) != 0)
			pass = false;

		counts[check].total++;

		if (pass)
			counts[check].pass++;
		else
			differ++;

		if (!pass && end)
			print = show(check);

		if (!print)
			continue;

		fprintf(arm7_report, "    %s case %u (%s, %08X):", job.name.c_str(), (u32)i, c.cls.c_str(), c.opcode);

		for (u32 w = 0; w < QEMU_CASE_WORDS; w++)
		{
			u32 mask = w == QEMU_CASE_WORDS - 1 ? c.cpsr_mask : 0xFFFFFFFF;
			u32 console = arm7_le32(job.console, offset + 4 * w) & mask;
			u32 actual = arm7_le32(run.readback, offset + 4 * w) & mask;

			if (console != actual)
				fprintf(arm7_report, " %s is %08X, console %08X;", word_name[w], actual, console);
		}

		for (u32 w = 0; c.region >= 0 && w < QEMU_REGION_SIZE / 4; w++)
		{
			u32 console = arm7_le32(job.console, region + 4 * w);
			u32 actual = arm7_le32(run.readback, region + 4 * w);

			if (console != actual)
				fprintf(arm7_report, " scratch[%u] is %08X, console %08X;", w, actual, console);
		}

		fprintf(arm7_report, "\n");
	}

	for (std::map<std::string, QemuCount>::const_iterator it = counts.begin(); it != counts.end(); ++it)
		arm7_count_cases(it->first, it->second.total, it->second.pass);

	fprintf(arm7_report, "    %u cases, %u differ from the console\n", (u32)cases.size(), differ);
}

static void check_mulsem(const Wren7Job& job, const Wren7Run& run)
{
	static const char* const word_name[MULSEM_RECORD_WORDS] =
	{
		"mla-rd-rm", "muls-result", "muls-flags", "mlas-result", "mlas-flags", "muls-rd-rm",
	};

	bool end = wren7_same_end(job, run);

	arm7_count("exit", end);

	const std::vector<u8>* inputs = 0;

	for (size_t i = 0; i < job.images.size(); i++)
	{
		if (job.images[i].addr == MULSEM_INPUTS_ADDR)
			inputs = &job.images[i].data;
	}

	size_t table = job.reads[0].size;
	u32 records = job.reads.size() == 2 ? job.reads[1].size / (4 * MULSEM_RECORD_WORDS) : 0;

	if (records == 0 || !inputs || inputs->size() < records * MULSEM_INPUT_SIZE)
	{
		arm7_problem("%s: the job does not look like the multiplier probe", job.name.c_str());
		return;
	}

	for (u32 i = 0; i < records; i++)
	{
		for (u32 w = 0; w < MULSEM_RECORD_WORDS; w++)
		{
			//the two CPSR words: only the flags are the multiplier's
			u32 mask = (w == 2 || w == 4) ? 0xF0000000 : 0xFFFFFFFF;
			size_t offset = table + (i * MULSEM_RECORD_WORDS + w) * 4;
			u32 console = arm7_le32(job.console, offset) & mask;
			u32 actual = arm7_le32(run.readback, offset) & mask;
			bool pass = end && console == actual;

			arm7_count(word_name[w], pass);

			if (!pass && end && show(word_name[w]))
			{
				size_t in = i * MULSEM_INPUT_SIZE;

				fprintf(arm7_report, "    record %u %s: Rm %08X Rs %08X Rn %08X NZCV %X: is %08X, console %08X\n", i, word_name[w],
					arm7_le32(*inputs, in), arm7_le32(*inputs, in + 4), arm7_le32(*inputs, in + 8), arm7_le32(*inputs, in + 12) >> 28,
					actual, console);
			}
		}
	}
}

static void check_bringup(const Wren7Job& job, const Wren7Run& run)
{
	static const ResultWord result[] =
	{
		{ WREN7_RES_R0 + 4 * 1, "r1" }, { WREN7_RES_R0 + 4 * 12, "r12" }, { WREN7_RES_R0 + 4 * 15, "r15" },
	};

	static const ResultWord reset_state[] =
	{
		{ WREN7_RES_R0 + 4 * 0, "r0" }, { WREN7_RES_R0 + 4 * 14, "r14" }, { WREN7_RES_CPSR, "CPSR" }, { WREN7_RES_SPSR, "SPSR" },
	};

	if (job.console_status != "done")
	{
		//the stub was not reached: nothing is in the result block
		arm7_count("result", wren7_same_end(job, run) && run.readback == job.console);
		return;
	}

	check_words("result", job, run, result, 3);

	if (job.repeat >= 2)
		check_words("reset-state", job, run, reset_state, 4);
}

struct SuiteDef
{
	const char* name;
	void (*check)(const Wren7Job& job, const Wren7Run& run);
};

static const SuiteDef suites[] =
{
	{ "vendor", &check_vendor },
	{ "qemu_diff", &check_qemu_diff },
	{ "mulsem", &check_mulsem },
	{ "bringup", &check_bringup },
};

int main(int argc, char** argv)
{
	const SuiteDef* def = 0;

	for (size_t i = 0; argc > 1 && i < sizeof(suites) / sizeof(suites[0]); i++)
	{
		if (strcmp(argv[1], suites[i].name) == 0)
			def = &suites[i];
	}

	Arm7Options opt;

	opt.data_dir = NULLDC_WREN7_DIR;
	opt.baseline = NULLDC_BASELINE;

	//an unknown suite gets the usage
	if (!arm7_suite_options(argc, argv, def ? 2 : argc + 1,
		"arm7_wren7 vendor|qemu_diff|mulsem|bringup: runs console jobs of wren7-rtl on the ARM7 interpreter",
		"--wren7", "--job", opt))
		return ARM7_EXIT_ERROR;

	std::string test = std::string("arm7.wren7.") + def->name;

	if (opt.data_dir.empty())
		return arm7_suite_skip(test.c_str(), "no wren7-rtl checkout, configure with -DNULLDC_WREN7_DIR=<path to wren7-rtl> or pass --wren7 <dir>");

	Wren7Suite suite;
	std::string error;
	bool missing = false;

	Wren7Load loaded = wren7_load_suite(opt.data_dir, def->name, suite, error);

	if (loaded == W7L_OK && def->check == &check_qemu_diff && !load_qemu_cases(suite, error, missing))
		loaded = missing ? W7L_MISSING : W7L_BAD;

	if (loaded == W7L_MISSING)
		return arm7_suite_skip(test.c_str(), error);

	if (loaded != W7L_OK)
	{
		fprintf(stderr, "%s: %s\n", test.c_str(), error.c_str());
		return ARM7_EXIT_ERROR;
	}

	if (!arm7_suite_begin(test.c_str(), opt))
		return ARM7_EXIT_ERROR;

	arm7m_init();

	fprintf(arm7_report, "%s: jobs %s/tests/hw/%s, console results %s/%s\n\n", test.c_str(), suite.model_dir.c_str(), def->name,
		suite.model_dir.c_str(), suite.out_dir.c_str());

	for (size_t j = 0; j < suite.jobs.size(); j++)
	{
		const Wren7Job& job = suite.jobs[j];

		if (!arm7_selected(job.name))
			continue;

		if (job.needs_sh4 || job.reads.empty() || job.reads[0].size != WREN7_RES_SIZE)
		{
			arm7_problem("%s: not a job that only needs wave RAM and the done stub", job.name.c_str());
			continue;
		}

		Wren7Run run;

		wren7_run_job(job, run);
		print_job(job, run);
		def->check(job, run);
	}

	return arm7_suite_end();
}
