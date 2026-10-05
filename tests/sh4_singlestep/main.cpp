/*
	The single-step vectors through the SH4 interpreter: the instruction loop of SH4IInterpreter::Loop, one
	instruction at a time.
*/
#include "ss_suite.h"
#include "ss_memory.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_mem.h"

static void run(const SsTest& test)
{
	//An instruction makes one fetch, a delayed branch two: its delay slot is executed by the branch handler
	while (ss_mem_fetches() < 4)
	{
		u32 addr = Sh4cntx.pc;
		Sh4cntx.pc += 2;

		u32 op = IReadMem16(addr);

		if (ss_mem_error()[0])
			break;

		ExecuteOpcode(op);
	}
}

//What the interpreter does not pass and the dynarec does. sh4_dynarec/KNOWN_DIFFERENCES.md has the details
static const char known_pr_behind_slot[] =
	"interpreter: BSR, BSRF and JSR write PR behind their delay slot, an SH4 in front of it (PR = PC + 4, then Delay_Slot() "
	"in the manual). A delay slot that reads PR reads the one of before the call. Only the directed vectors have such a "
	"delay slot (sts pr,r5)";

static const SsSuiteKnown known[] =
{
	{ "0000mmmm00000011_sz0_pr0", false, 0, known_pr_behind_slot },
	{ "0100mmmm00001011_sz0_pr0", false, 0, known_pr_behind_slot },
	{ "1011dddddddddddd_sz0_pr0", false, 0, known_pr_behind_slot },
	{ 0, false, 0, 0 },
};

int main(int argc, char** argv)
{
	SsSuite suite;

	suite.name = "sh4.singlestep.interp";
	suite.vectors_dir = NULLDC_SH4_TESTS_DIR;
	suite.baseline = NULLDC_BASELINE;
	suite.ordered_fetches = true;
	suite.known = known;
	suite.init = 0;
	suite.run = &run;
	suite.native = 0;

	return ss_suite_main(argc, argv, suite);
}
