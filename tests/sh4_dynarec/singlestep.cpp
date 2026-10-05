/*
	The single-step vectors through the SH4 dynarec: the 4 instructions of a vector as blocks, see
	dynarec_runner.h. Instruction fetches are not part of the expected bus order, the addresses the blocks were
	decoded from have to be the ones the vector fetches from.
*/
#include "ss_suite.h"
#include "ss_memory.h"
#include "test_guard.h"
#include "dynarec_runner.h"

static const DynarecCode code = { &ss_mem_opcode_at, &ss_mem_serve_opcodes };

static void run(const SsTest& test)
{
	u32 executed[4];

	dynarec_run(code, 4, executed);

	for (u32 i = 0; i < 4; i++)
	{
		if (executed[i] != test.cycles.c[i].fetch_addr)
			test_guard_abort("instruction %u was run from %08X, the vector fetches it from %08X", i, executed[i], test.cycles.c[i].fetch_addr);
	}
}

static bool native()
{
	return dynarec_native(SS_TEST_INSTRUCTION);
}

//What the dynarec does not pass and the interpreter does: nothing today. KNOWN_DIFFERENCES.md has what a suite
//does not pass
static const SsSuiteKnown known[] =
{
	{ 0, false, 0, 0 },
};

int main(int argc, char** argv)
{
	SsSuite suite;

	suite.name = "sh4.singlestep.dynarec";
	suite.vectors_dir = NULLDC_SH4_TESTS_DIR;
	suite.baseline = NULLDC_BASELINE;
	suite.ordered_fetches = false;
	suite.known = known;
	suite.init = &dynarec_init;
	suite.run = &run;
	suite.native = &native;

	return ss_suite_main(argc, argv, suite);
}
