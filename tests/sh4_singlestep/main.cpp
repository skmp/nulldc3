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

int main(int argc, char** argv)
{
	SsSuite suite;

	suite.name = "sh4.singlestep.interp";
	suite.vectors_dir = NULLDC_SH4_TESTS_DIR;
	suite.baseline = NULLDC_BASELINE;
	suite.ordered_fetches = true;
	suite.known = 0;
	suite.init = 0;
	suite.run = &run;
	suite.native = 0;

	return ss_suite_main(argc, argv, suite);
}
