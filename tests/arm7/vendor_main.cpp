/*
	arm7.vendor: the self-checking programs of arm7di-tests-dreamcast (bins/<name>.s.bin).

	A program is loaded at 0 and run from reset until the next instruction is the end marker. It collects the
	checks that failed in r1: bit 4 a wrong destination register, bit 5 a wrong base register, bits 0-3 wrong flags,
	the rest is the test's own. Bits 30 and 31 are set by some tests whatever they find and are no errors.
*/
#include "arm7_machine.h"
#include "arm7_suite.h"

#include <cstring>

#define END_MARKER 0xDEADBEEF
#define ERROR_BITS 0x3FFFFFFF

//The longest program gets to its end marker in about 50 instructions
#define MAX_STEPS 100000

struct ProgramRun
{
	bool ended;
	u32 steps;
};

static void run_program(void* arg)
{
	ProgramRun* run = (ProgramRun*)arg;

	while (run->steps < MAX_STEPS)
	{
		if (arm7m_next_opcode() == END_MARKER)
		{
			run->ended = true;
			break;
		}

		arm7m_step();
		run->steps++;
	}
}

int main(int argc, char** argv)
{
	static const char test[] = "arm7.vendor";

	Arm7Options opt;

	opt.data_dir = NULLDC_ARM7_TESTS_DIR;
	opt.baseline = NULLDC_BASELINE;

	if (!arm7_suite_options(argc, argv, 1, "arm7_vendor: runs the programs of arm7di-tests-dreamcast on the ARM7 interpreter",
		"--tests", "--program", opt))
		return ARM7_EXIT_ERROR;

	std::string bins = opt.data_dir + "/bins";
	std::vector<std::string> programs;

	if (!arm7_list_files(bins, ".s.bin", programs) || programs.empty())
	{
		return arm7_suite_skip(test, "no test binaries (*.s.bin) in " + bins +
			", run: git submodule update --init tests/vendor/arm7di-tests-dreamcast");
	}

	if (!arm7_suite_begin(test, opt))
		return ARM7_EXIT_ERROR;

	arm7m_init();

	fprintf(arm7_report, "%s: programs %s\n\n", test, bins.c_str());

	for (size_t i = 0; i < programs.size(); i++)
	{
		const std::string& name = programs[i];

		if (!arm7_selected(name))
			continue;

		std::vector<u8> image;

		if (!arm7_read_file(bins + "/" + name + ".s.bin", image) || image.empty() || image.size() > ARM7M_RAM_SIZE)
		{
			arm7_problem("%s: cannot read the program", name.c_str());
			continue;
		}

		arm7m_hold_reset();
		arm7m_clear();
		memcpy(arm7m_ram(), &image[0], image.size());
		arm7m_release_reset();

		ProgramRun run;

		run.ended = false;
		run.steps = 0;

		TestGuardResult guard = arm7_guarded(name, &run_program, &run);

		u32 r1 = arm7m_context()->regs[1].I;
		bool pass = guard == TGR_OK && run.ended && (r1 & ERROR_BITS) == 0;

		arm7_count("programs", pass);

		fprintf(arm7_report, "%-8s %s  r1 %08X  %u instructions", name.c_str(), pass ? "pass" : "FAIL", r1, run.steps);

		if (guard != TGR_OK)
			fprintf(arm7_report, "  %s: %s", test_guard_result_name(guard), test_guard_message());
		else if (!run.ended)
			fprintf(arm7_report, "  no end marker after %u instructions, next is %08X", MAX_STEPS, arm7m_context()->regs[R15_ARM_NEXT].I);
		else if (!pass)
			fprintf(arm7_report, "  wrong:%s%s%s%s", (r1 & 0x0F) ? " flags" : "", (r1 & 0x10) ? " Rd" : "", (r1 & 0x20) ? " Rn" : "",
				(r1 & ERROR_BITS & ~0x3F) ? " other" : "");

		fprintf(arm7_report, "\n");
	}

	return arm7_suite_end();
}
