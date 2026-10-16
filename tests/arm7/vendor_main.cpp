/*
	arm7.vendor: the self-checking programs of arm7di-tests-dreamcast (bins/<name>.s.bin).

	A program is loaded at 0 and run from reset until the next instruction is the end marker. It collects the
	checks that failed in r1: bit 4 a wrong destination register, bit 5 a wrong base register, bits 0-3 wrong flags,
	the rest is the test's own. Bits 30 and 31 are set by some tests whatever they find and are no errors.

	The programs are straight lines: one that is not executed at all arrives at its end marker as well, with
	r1 = 0. So "no error bit" is not what a program has to show. The checks are
	  programs      the program gets to its end marker and has no error bit in r1
	  final-state   r0-r14 and CPSR at the end marker are the ones the baseline pins for the program
	  control       the same program on a core that skips every instruction does not pass the two checks above.
	                A program for which it does proves nothing, and that fails the run whatever the baseline says
*/
#include "arm7_machine.h"
#include "arm7_suite.h"

#include <cstring>

#define END_MARKER 0xDEADBEEF
#define ERROR_BITS 0x3FFFFFFF

//The longest program gets to its end marker in about 50 instructions
#define MAX_STEPS 100000

//r0-r14, CPSR
#define STATE_WORDS 16

static const char* const state_name[STATE_WORDS] =
{
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "CPSR",
};

struct ProgramRun
{
	bool skip;		//the control: nothing is executed
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

		if (run->skip)
			arm7m_context()->regs[R15_ARM_NEXT].I += 4;
		else
			arm7m_step();

		run->steps++;
	}
}

//Every program starts on a machine that was just switched on: a reset by itself keeps the registers
static void load_program(const std::vector<u8>& image)
{
	arm7m_power_on();
	arm7m_clear();
	memcpy(arm7m_ram(), &image[0], image.size());
	arm7m_release_reset();
}

static void save_state(std::vector<u32>& state)
{
	state.clear();

	for (u32 i = 0; i < 15; i++)
		state.push_back(arm7m_context()->regs[i].I);

	state.push_back(arm7m_cpsr());
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
		return arm7_suite_skip(test, opt, "no test binaries (*.s.bin) in " + bins +
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

		load_program(image);

		ProgramRun run;

		run.skip = false;
		run.ended = false;
		run.steps = 0;

		TestGuardResult guard = arm7_guarded(name, &run_program, &run);

		std::vector<u32> state, pinned;

		save_state(state);
		arm7_pin(name, state);

		u32 r1 = state[1];
		bool has_pin = arm7_baseline_pin(name, pinned) && pinned.size() == STATE_WORDS;

		//the row that is being written is the one that counts
		if (arm7_suite_opt().update_baseline)
		{
			pinned = state;
			has_pin = true;
		}
		bool ended = guard == TGR_OK && run.ended;
		bool pass = ended && (r1 & ERROR_BITS) == 0;
		bool state_pass = ended && has_pin && state == pinned;

		arm7_count("programs", pass);
		arm7_count("final-state", state_pass);

		fprintf(arm7_report, "%-8s %s  r1 %08X  %u instructions", name.c_str(), pass && state_pass ? "pass" : "FAIL", r1, run.steps);

		if (guard != TGR_OK)
			fprintf(arm7_report, "  %s: %s", test_guard_result_name(guard), test_guard_message());
		else if (!run.ended)
			fprintf(arm7_report, "  no end marker after %u instructions, next is %08X", MAX_STEPS, arm7m_context()->regs[R15_ARM_NEXT].I);
		else if (!pass)
			fprintf(arm7_report, "  wrong:%s%s%s%s", (r1 & 0x0F) ? " flags" : "", (r1 & 0x10) ? " Rd" : "", (r1 & 0x20) ? " Rn" : "",
				(r1 & ERROR_BITS & ~0x3F) ? " other" : "");

		if (ended && !has_pin)
			fprintf(arm7_report, "  final state: not pinned in the baseline");

		for (u32 w = 0; ended && has_pin && w < STATE_WORDS; w++)
		{
			if (state[w] != pinned[w])
				fprintf(arm7_report, "  %s is %08X, pinned %08X;", state_name[w], state[w], pinned[w]);
		}

		fprintf(arm7_report, "\n");

		//the control
		load_program(image);

		ProgramRun skipped;

		skipped.skip = true;
		skipped.ended = false;
		skipped.steps = 0;

		TestGuardResult skip_guard = arm7_guarded(name + " (control)", &run_program, &skipped);

		std::vector<u32> skip_state;

		save_state(skip_state);

		//what it has to be told from: the state that passes, and the one this run would pin
		bool told = skip_guard == TGR_OK && skip_state != state && !(has_pin && skip_state == pinned);

		arm7_count("control", told);

		if (!told)
			arm7_problem("%s: a core that skips every instruction ends with the same r0-r14 and CPSR, the program shows nothing", name.c_str());
	}

	return arm7_suite_end();
}
