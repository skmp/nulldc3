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

//What the dynarec does not pass and the interpreter does. KNOWN_DIFFERENCES.md has the details
static const char known_rte[] =
	"dynarec: RTE loads SR from SSR in front of its delay slot, which then runs on the register bank of the new SR. "
	"The vectors (and the interpreter) run it on the bank of the old SR";
static const char known_sum_order[] =
	"dynarec: the x64 backend adds the 4 products in pairs, (p0 + p1) + (p2 + p3), the interpreter one by one";

static const SsSuiteKnown known[] =
{
	{ "0000000000101011_sz0_pr0", false, 0, known_rte },
	{ "1111nn0111111101_sz0_pr0", false, 0, known_sum_order },
	{ "1111nnmm11101101_sz0_pr0", false, 0, known_sum_order },
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
