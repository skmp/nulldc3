/*
	arm7.directed: ADC, SBC and RSC on operands at the edges, with the carry clear and set.

	These take the carry in and make a carry and an overflow of their own. The random instructions of the
	console suites have no case in which the carry out comes from the carry in (0xFFFFFFFF + 0 + C), and the
	vendor programs have one ADC, SBC and RSC each.

	A case is one instruction, executed from the reset state with Rn = r0, operand 2 = r1 (or an immediate),
	Rd = r2 and the flags set by the test. What it leaves in r2 and in N, Z, C and V is written down by hand in the
	tables, from the instruction's definition: Rd = Rn + op2 + C for ADC, Rn + NOT(op2) + C for SBC,
	op2 + NOT(Rn) + C for RSC, C the carry out of that sum and V its signed overflow. Nothing of it comes from an
	emulator. The forms without S leave the flags alone.

	Checks: adc, sbc, rsc. The test needs no data.
*/
#include "arm7_machine.h"
#include "arm7_suite.h"

#include <cstring>

#define FN 8
#define FZ 4
#define FC 2
#define FV 1

//Rd = r2, Rn = r0, operand 2 = r1
#define ADCS 0xE0B02001
#define ADC  0xE0A02001
#define SBCS 0xE0D02001
#define SBC  0xE0C02001
#define RSCS 0xE0F02001
#define RSC  0xE0E02001

//Rd = r2, Rn = r0, operand 2 = #imm
#define ADCS_IMM(imm) (0xE2B02000 | (imm))
#define SBCS_IMM(imm) (0xE2D02000 | (imm))
#define RSCS_IMM(imm) (0xE2F02000 | (imm))

//what r2 holds going in: no case leaves it
#define RD_BEFORE 0xDEADBEEF

struct Case
{
	u32 opcode;
	u32 rn;
	u32 op2;			//r1. Not used by the immediate forms, which have to leave it alone as well
	u32 flags;			//FN, FZ, FC, FV going in
	u32 rd_after;
	u32 flags_after;
};

//The flags going in are the carry of the case and, for the S forms, the other three as the instruction does
//not leave them
static const Case adc[] =
{
	//       Rn          op2         in             Rd after    after
	{ ADCS, 0x00000000, 0x00000000, FN | FV,       0x00000000, FZ },
	{ ADCS, 0x00000000, 0x00000000, FN | FZ | FC | FV, 0x00000001, 0 },
	{ ADCS, 0xFFFFFFFF, 0x00000000, FN | FC | FV,  0x00000000, FZ | FC },		//the carry comes from adding C
	{ ADCS, 0x00000000, 0xFFFFFFFF, FN | FC | FV,  0x00000000, FZ | FC },
	{ ADCS, 0xFFFFFFFF, 0x00000000, FZ | FV,       0xFFFFFFFF, FN },
	{ ADCS, 0xFFFFFFFF, 0x00000001, FN | FV,       0x00000000, FZ | FC },
	{ ADCS, 0x7FFFFFFF, 0x00000000, FZ | FC,       0x80000000, FN | FV },		//the overflow comes from adding C
	{ ADCS, 0x7FFFFFFF, 0x80000000, FN | FC | FV,  0x00000000, FZ | FC },
	{ ADCS, 0x7FFFFFFF, 0x80000000, FZ | FV,       0xFFFFFFFF, FN },
	{ ADCS, 0xFFFFFFFF, 0xFFFFFFFF, FZ | FC | FV,  0xFFFFFFFF, FN | FC },
	{ ADCS, 0x80000000, 0x80000000, FN,            0x00000000, FZ | FC | FV },
	{ ADCS, 0x80000000, 0x80000000, FN | FZ | FC,  0x00000001, FC | FV },
	{ ADCS, 0x80000000, 0xFFFFFFFF, FN | FZ,       0x7FFFFFFF, FC | FV },
	{ ADCS, 0x80000000, 0xFFFFFFFF, FZ | FC | FV,  0x80000000, FN | FC },		//adding C takes the overflow back
	{ ADCS, 0x7FFFFFFF, 0x7FFFFFFF, FZ | FC,       0xFFFFFFFF, FN | FV },
	{ ADCS, 0x3FFFFFFF, 0x40000000, FZ | FC,       0x80000000, FN | FV },
	{ ADCS, 0x00000001, 0xFFFFFFFE, FN | FC | FV,  0x00000000, FZ | FC },
	{ ADCS, 0x7FFFFFFF, 0x00000000, FN | FZ | FV,  0x7FFFFFFF, 0 },

	{ ADCS_IMM(0x00), 0xFFFFFFFF, 0x11111111, FN | FC | FV, 0x00000000, FZ | FC },
	{ ADCS_IMM(0xFF), 0xFFFFFF00, 0x11111111, FN | FC | FV, 0x00000000, FZ | FC },

	{ ADC,  0xFFFFFFFF, 0x00000000, FC,            0x00000000, FC },
	{ ADC,  0x7FFFFFFF, 0x00000000, FN | FC | FV,  0x80000000, FN | FC | FV },
	{ ADC,  0x00000000, 0x00000000, FZ,            0x00000000, FZ },
	{ ADC,  0x80000000, 0x80000000, 0,             0x00000000, 0 },
};

//Rd = Rn - op2 - 1 + C. C after: no borrow
static const Case sbc[] =
{
	{ SBCS, 0x00000000, 0x00000000, FN | FC | FV,  0x00000000, FZ | FC },
	{ SBCS, 0x00000000, 0x00000000, FZ | FV,       0xFFFFFFFF, FN },			//the borrow comes from the missing C
	{ SBCS, 0x00000000, 0xFFFFFFFF, FN | FZ | FC | FV, 0x00000001, 0 },
	{ SBCS, 0x00000000, 0xFFFFFFFF, FN | FV,       0x00000000, FZ },
	{ SBCS, 0xFFFFFFFF, 0xFFFFFFFF, FZ | FV,       0xFFFFFFFF, FN },
	{ SBCS, 0xFFFFFFFF, 0xFFFFFFFF, FN | FC | FV,  0x00000000, FZ | FC },
	{ SBCS, 0x80000000, 0x7FFFFFFF, FN,            0x00000000, FZ | FC | FV },
	{ SBCS, 0x80000000, 0x7FFFFFFF, FN | FZ | FC,  0x00000001, FC | FV },
	{ SBCS, 0x7FFFFFFF, 0x80000000, FZ | FC,       0xFFFFFFFF, FN | FV },
	{ SBCS, 0x7FFFFFFF, 0x80000000, FZ,            0xFFFFFFFE, FN | FV },
	{ SBCS, 0x80000000, 0x00000000, FN | FZ,       0x7FFFFFFF, FC | FV },		//the overflow comes from the missing C
	{ SBCS, 0x00000000, 0x80000000, FZ | FC,       0x80000000, FN | FV },
	{ SBCS, 0x00000000, 0x80000000, FN | FZ | FV,  0x7FFFFFFF, 0 },				//the missing C takes the overflow back
	{ SBCS, 0x00000005, 0x00000005, FZ | FV,       0xFFFFFFFF, FN },
	{ SBCS, 0x00000005, 0x00000005, FN | FC | FV,  0x00000000, FZ | FC },
	{ SBCS, 0xFFFFFFFF, 0x00000000, FZ | FV,       0xFFFFFFFE, FN | FC },
	{ SBCS, 0x00000000, 0x00000001, FZ | FC | FV,  0xFFFFFFFF, FN },
	{ SBCS, 0x00000001, 0x00000000, FN | FV,       0x00000000, FZ | FC },

	{ SBCS_IMM(0x00), 0x00000000, 0x11111111, FZ | FV, 0xFFFFFFFF, FN },
	{ SBCS_IMM(0x00), 0x80000000, 0x11111111, FN | FZ, 0x7FFFFFFF, FC | FV },

	{ SBC,  0x00000000, 0x00000000, 0,             0xFFFFFFFF, 0 },
	{ SBC,  0x80000000, 0x7FFFFFFF, FN,            0x00000000, FN },
	{ SBC,  0x00000001, 0x00000000, FC | FV,       0x00000001, FC | FV },
};

//Rd = op2 - Rn - 1 + C
static const Case rsc[] =
{
	{ RSCS, 0x00000000, 0x00000000, FN | FC | FV,  0x00000000, FZ | FC },
	{ RSCS, 0x00000000, 0x00000000, FZ | FV,       0xFFFFFFFF, FN },
	{ RSCS, 0xFFFFFFFF, 0x00000000, FN | FV,       0x00000000, FZ },
	{ RSCS, 0xFFFFFFFF, 0xFFFFFFFF, FZ | FV,       0xFFFFFFFF, FN },
	{ RSCS, 0x7FFFFFFF, 0x80000000, FN,            0x00000000, FZ | FC | FV },
	{ RSCS, 0x80000000, 0x7FFFFFFF, FZ | FC,       0xFFFFFFFF, FN | FV },
	{ RSCS, 0x00000000, 0x80000000, FN | FZ,       0x7FFFFFFF, FC | FV },
	{ RSCS, 0x80000000, 0x00000000, FN | FZ | FV,  0x7FFFFFFF, 0 },
	{ RSCS, 0x00000000, 0xFFFFFFFF, FZ | FV,       0xFFFFFFFE, FN | FC },
	{ RSCS, 0x00000000, 0x00000001, FN | FV,       0x00000000, FZ | FC },
	{ RSCS, 0x00000001, 0x00000000, FZ | FC | FV,  0xFFFFFFFF, FN },
	{ RSCS, 0x00000005, 0x00000005, FZ | FV,       0xFFFFFFFF, FN },

	{ RSCS_IMM(0x00), 0xFFFFFFFF, 0x11111111, FN | FV, 0x00000000, FZ },
	{ RSCS_IMM(0x00), 0x00000000, 0x11111111, FZ | FV, 0xFFFFFFFF, FN },

	{ RSC,  0xFFFFFFFF, 0x00000000, 0,             0x00000000, 0 },
	{ RSC,  0x00000000, 0x00000000, FN | FC,       0x00000000, FN | FC },
};

struct Group
{
	const char* check;
	const Case* cases;
	u32 count;
};

static const Group groups[] =
{
	{ "adc", adc, sizeof(adc) / sizeof(adc[0]) },
	{ "sbc", sbc, sizeof(sbc) / sizeof(sbc[0]) },
	{ "rsc", rsc, sizeof(rsc) / sizeof(rsc[0]) },
};

static void step(void*)
{
	arm7m_step();
}

int main(int argc, char** argv)
{
	static const char test[] = "arm7.directed";

	Arm7Options opt;

	opt.baseline = NULLDC_BASELINE;

	if (!arm7_suite_options(argc, argv, 1, "arm7_directed: runs single instructions with hand-written results on the ARM7 interpreter",
		0, "--check", opt))
		return ARM7_EXIT_ERROR;

	if (!arm7_suite_begin(test, opt))
		return ARM7_EXIT_ERROR;

	arm7m_init();

	for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++)
	{
		const Group& group = groups[g];

		if (!arm7_selected(group.check))
			continue;

		for (u32 i = 0; i < group.count; i++)
		{
			const Case& c = group.cases[i];
			Arm7Context* ctx = arm7m_context();

			arm7m_hold_reset();
			arm7m_clear();
			memcpy(arm7m_ram(), &c.opcode, 4);
			arm7m_release_reset();

			ctx->regs[0].I = c.rn;
			ctx->regs[1].I = c.op2;
			ctx->regs[2].I = RD_BEFORE;
			arm7m_set_flags(c.flags);

			char what[64];

			snprintf(what, sizeof(what), "%s case %u (%08X)", group.check, i, c.opcode);

			TestGuardResult guard = arm7_guarded(what, &step, 0);

			u32 rd = ctx->regs[2].I;
			u32 flags = arm7m_cpsr() >> 28;

			//the operands stay and the next instruction is the one behind it
			bool rest = ctx->regs[0].I == c.rn && ctx->regs[1].I == c.op2 && ctx->regs[R15_ARM_NEXT].I == 4;
			bool pass = guard == TGR_OK && rd == c.rd_after && flags == c.flags_after && rest;

			arm7_count(group.check, pass);

			if (pass || guard != TGR_OK)
				continue;

			fprintf(arm7_report, "%s case %u: %08X with Rn %08X, operand 2 %08X, NZCV %X:", group.check, i, c.opcode, c.rn, c.op2, c.flags);

			if (rd != c.rd_after)
				fprintf(arm7_report, " Rd is %08X, has to be %08X;", rd, c.rd_after);

			if (flags != c.flags_after)
				fprintf(arm7_report, " NZCV is %X, has to be %X;", flags, c.flags_after);

			if (!rest)
				fprintf(arm7_report, " r0 %08X, r1 %08X, next instruction at %08X;", ctx->regs[0].I, ctx->regs[1].I, ctx->regs[R15_ARM_NEXT].I);

			fprintf(arm7_report, "\n");
		}
	}

	return arm7_suite_end();
}
