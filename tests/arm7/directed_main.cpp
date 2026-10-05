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

	trap: SWI and the undefined instruction trap, with what R14 holds in the trap's mode and where "movs pc, lr" in
	the vector goes on (the tables below have the source).

	ror: the logical operations with a ROR by a register as operand 2, with an amount of 0, of 32 and around them
	(the table has the source).

	mul-rd-rm: MUL and MLA, with and without S, with Rd == Rm (the table has the source). mul-rd-rm-c: C behind
	their S forms, which no console capture has (the table has what it is made of).

	fiq, scilv: the interrupt controller of the AICA in front of nFIQ, driven through its registers as a guest
	drives it, and what SCILV0-2 read back (the comment at fiq_setup() has the source). Two cases of fiq have an
	ARMRST in the middle of a handler, with no capture behind them (the comment at fiq_armrst_in_service()).

	reset: what the release of ARMRST writes and what it leaves, out of four modes (the comment at reset_case() has
	the source).

	power-on: a sound CPU that was created and not reset yet holds what its reset gives it, and a reset of the
	machine leaves nothing of the interrupt controller (the comments at run_power_on() have why).

	Checks: adc, sbc, rsc, ror, mul-rd-rm, mul-rd-rm-c, trap, fiq, scilv, reset, power-on. The test needs no data.
*/
#include "arm7_machine.h"
#include "arm7_suite.h"

#include <cstring>
#include <string>

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

/*
	One instruction, executed from the reset state with r0 to r3 and the flags of the case. Rd is r2 in every case.
	flags_mask: the flags that are compared.
*/
struct Insn
{
	u32 opcode;
	u32 r[4];
	u32 flags;
	u32 rd_after;
	u32 flags_after;
	u32 flags_mask;
};

static void run_insn(const char* check, u32 index, const Insn& in)
{
	Arm7Context* ctx = arm7m_context();

	arm7m_power_on();
	arm7m_clear();
	memcpy(arm7m_ram(), &in.opcode, 4);
	arm7m_release_reset();

	for (u32 r = 0; r < 4; r++)
		ctx->regs[r].I = in.r[r];

	arm7m_set_flags(in.flags);

	char what[64];

	snprintf(what, sizeof(what), "%s case %u (%08X)", check, index, in.opcode);

	TestGuardResult guard = arm7_guarded(what, &step, 0);

	u32 rd = ctx->regs[2].I;
	u32 flags = (arm7m_cpsr() >> 28) & in.flags_mask;

	//the other registers stay and the next instruction is the one behind it
	bool rest = ctx->regs[0].I == in.r[0] && ctx->regs[1].I == in.r[1] && ctx->regs[3].I == in.r[3] && ctx->regs[R15_ARM_NEXT].I == 4;
	bool pass = guard == TGR_OK && rd == in.rd_after && flags == in.flags_after && rest;

	arm7_count(check, pass);

	if (pass || guard != TGR_OK)
		return;

	fprintf(arm7_report, "%s case %u: %08X with r0 %08X, r1 %08X, r2 %08X, r3 %08X, NZCV %X:", check, index, in.opcode, in.r[0], in.r[1],
		in.r[2], in.r[3], in.flags);

	if (rd != in.rd_after)
		fprintf(arm7_report, " r2 is %08X, has to be %08X;", rd, in.rd_after);

	if (flags != in.flags_after)
		fprintf(arm7_report, " NZCV is %X, has to be %X (the flags of %X);", flags, in.flags_after, in.flags_mask);

	if (!rest)
		fprintf(arm7_report, " r0 %08X, r1 %08X, r3 %08X, next instruction at %08X;", ctx->regs[0].I, ctx->regs[1].I, ctx->regs[3].I,
			ctx->regs[R15_ARM_NEXT].I);

	fprintf(arm7_report, "\n");
}

/*
	ROR by a register. Only the low byte of Rs is the amount. With 0 in it operand 2 is Rm and the carry of the
	shifter is C as it comes in, as for every shift type. ROR by 32 gives Rm as well, with bit 31 of Rm as the
	carry, and an amount above 32 counts as the amount less 32 (ARM7DI data sheet 4.4.2). The logical operations
	take C from the shifter when they have S, and leave V.

	The console has the amount of 0 in three of the random instructions of wren7-rtl's qemu_diff
	(model/tests/hw/qemu_diff): case 133 and 820 of s2 and case 389 of s3, a TEQ, a TST and a TEQ with C set going
	in and bit 31 of Rm clear. C is still set behind all three. The table has every logical operation.

	Rd = r2, Rn = r0, Rm = r1, Rs = r3.
*/
#define ANDS_ROR 0xE0102371
#define EORS_ROR 0xE0302371
#define TST_ROR  0xE1100371
#define TEQ_ROR  0xE1300371
#define ORRS_ROR 0xE1902371
#define MOVS_ROR 0xE1B02371
#define MOV_ROR  0xE1A02371
#define BICS_ROR 0xE1D02371
#define MVNS_ROR 0xE1F02371

struct RorCase
{
	u32 opcode;
	u32 rn;
	u32 rm;
	u32 rs;
	u32 flags;
	u32 rd_after;		//RD_BEFORE for TST and TEQ
	u32 flags_after;
};

static const RorCase ror[] =
{
	//           Rn          Rm          Rs          in                  Rd after    after
	//amount 0, C set, bit 31 of Rm clear: C stays
	{ MOVS_ROR, 0x11111111, 0x7FFFFFFF, 0x00000100, FN | FZ | FC | FV, 0x7FFFFFFF, FC | FV },
	{ MVNS_ROR, 0x11111111, 0x7FFFFFFF, 0x00000000, FZ | FC,           0x80000000, FN | FC },
	{ ANDS_ROR, 0xFFFFFFFF, 0x7FFFFFFF, 0x00000100, FN | FZ | FC,      0x7FFFFFFF, FC },
	{ EORS_ROR, 0x7FFFFFFF, 0x7FFFFFFF, 0xFFFFFF00, FN | FC | FV,      0x00000000, FZ | FC | FV },
	{ ORRS_ROR, 0x00000000, 0x7FFFFFFF, 0x00000100, FN | FZ | FC,      0x7FFFFFFF, FC },
	{ BICS_ROR, 0xFFFFFFFF, 0x7FFFFFFF, 0x00000000, FZ | FC,           0x80000000, FN | FC },
	{ TST_ROR,  0xFFFFFFFF, 0x7FFFFFFF, 0x00000100, FN | FZ | FC,      RD_BEFORE,  FC },
	{ TEQ_ROR,  0x7FFFFFFF, 0x7FFFFFFF, 0x00000000, FN | FC,           RD_BEFORE,  FZ | FC },

	//amount 0, C clear, bit 31 of Rm set: C stays
	{ MOVS_ROR, 0x11111111, 0x80000000, 0x00000100, FZ | FV,           0x80000000, FN | FV },
	{ MVNS_ROR, 0x11111111, 0x80000000, 0x00000000, FN | FZ,           0x7FFFFFFF, 0 },
	{ ANDS_ROR, 0xFFFFFFFF, 0x80000000, 0x00000100, FZ,                0x80000000, FN },
	{ EORS_ROR, 0x00000000, 0x80000000, 0x00000000, FZ | FV,           0x80000000, FN | FV },
	{ ORRS_ROR, 0x00000001, 0x80000000, 0x00000100, FZ,                0x80000001, FN },
	{ BICS_ROR, 0x80000000, 0x80000000, 0x00000000, FN,                0x00000000, FZ },
	{ TST_ROR,  0x80000000, 0x80000000, 0x00000100, FZ,                RD_BEFORE,  FN },
	{ TEQ_ROR,  0x80000000, 0x80000000, 0x00000000, FN | FV,           RD_BEFORE,  FZ | FV },

	//amount 32, and 64: Rm, C is bit 31 of Rm
	{ MOVS_ROR, 0x11111111, 0x7FFFFFFF, 0x00000020, FN | FZ | FC,      0x7FFFFFFF, 0 },
	{ MOVS_ROR, 0x11111111, 0x80000000, 0x00000120, FZ,                0x80000000, FN | FC },
	{ ORRS_ROR, 0x00000000, 0x7FFFFFFF, 0x00000020, FN | FC,           0x7FFFFFFF, 0 },
	{ TST_ROR,  0xFFFFFFFF, 0x80000000, 0x00000040, FZ,                RD_BEFORE,  FN | FC },

	//amount 1 and 4, and 33 and 36: C is the last bit that is rotated out
	{ MOVS_ROR, 0x11111111, 0x00000001, 0x00000101, FZ,                0x80000000, FN | FC },
	{ ORRS_ROR, 0x00000000, 0x00000002, 0x00000021, FN | FC,           0x00000001, 0 },
	{ EORS_ROR, 0x00000000, 0x80000001, 0x00000004, FV,                0x18000000, FV },
	{ BICS_ROR, 0xFFFFFFFF, 0x0000000F, 0x00000024, FN | FZ,           0x0FFFFFFF, FC },

	//without S the flags stay
	{ MOV_ROR,  0x11111111, 0x80000000, 0x00000100, FZ | FV,           0x80000000, FZ | FV },
	{ MOV_ROR,  0x11111111, 0x7FFFFFFF, 0x00000000, FN | FC,           0x7FFFFFFF, FN | FC },
};

static void run_ror()
{
	for (u32 i = 0; i < sizeof(ror) / sizeof(ror[0]); i++)
	{
		const RorCase& c = ror[i];
		Insn in = { c.opcode, { c.rn, c.rm, RD_BEFORE, c.rs }, c.flags, c.rd_after, c.flags_after, FN | FZ | FC | FV };

		run_insn("ror", i, in);
	}
}

/*
	MUL and MLA with Rd == Rm. The multiplier writes Rd before its first step, with 0 or with Rn for MLA, and
	reads Rm again in every step: MUL gives 0 whatever the operands are, and MLA is not the product (ARM7DI data
	sheet 4.6.1: "zero" and "meaningless").

	The console has both in the 1024 records of wren7-rtl's multiplier probe (model/tests/hw/mulsem, the kernel is
	model/hw/arm/k_mulsem.s): "muls r9, r9, r5" leaves 0 in every record, and the MLA rows are records of it with
	the word "mla r0, r0, r5, r6" leaves. In record 37, with Rs = 1, that is twice Rn: the one step adds Rm, which
	is Rd, and Rd is Rn by then.

	The probe has MUL only with S and MLA only without. They are one multiplier: MUL has to leave the 0 of MULS,
	MLAS the word of MLA, and the S forms N and Z of what they leave. V stays (the probe's MULS and MLAS with
	Rd != Rm). No capture has C behind the S forms with Rd == Rm, and these rows do not compare it; the check
	mul-rd-rm-c below is about it.

	Rd = Rm = r2, Rs = r1, Rn = r0.
*/
#define MUL_RD_RM  0xE0020192
#define MULS_RD_RM 0xE0120192
#define MLA_RD_RM  0xE0220192
#define MLAS_RD_RM 0xE0320192

struct MulCase
{
	u32 opcode;
	u32 rm;
	u32 rs;
	u32 rn;				//MUL does not use it and has to leave it alone
	u32 flags;
	u32 rd_after;
	u32 flags_after;
	u32 flags_mask;
};

//the flags that are compared
#define ALL_FLAGS (FN | FZ | FC | FV)
#define NZV_FLAGS (FN | FZ | FV)

static const MulCase mul_rd_rm[] =
{
	//             Rm          Rs          Rn          in                  Rd after    after         compared
	//MUL: 0, the flags stay. Operands of the records 37, 0 and 2
	{ MUL_RD_RM,  0x4D5D160D, 0x00000001, 0x11111111, FN | FC | FV,      0x00000000, FN | FC | FV, ALL_FLAGS },
	{ MUL_RD_RM,  0x00004000, 0x050C368D, 0x11111111, FZ,                0x00000000, FZ,           ALL_FLAGS },
	{ MUL_RD_RM,  0x5079FC5D, 0xDABF5585, 0x11111111, 0,                 0x00000000, 0,            ALL_FLAGS },
	{ MUL_RD_RM,  0xFFFFFFFF, 0xFFFFFFFF, 0x11111111, FN | FV,           0x00000000, FN | FV,      ALL_FLAGS },

	//MULS: 0 and Z. Records 37 and 2
	{ MULS_RD_RM, 0x4D5D160D, 0x00000001, 0x11111111, FN | FV,           0x00000000, FZ | FV,      NZV_FLAGS },
	{ MULS_RD_RM, 0x5079FC5D, 0xDABF5585, 0x11111111, FN | FC,           0x00000000, FZ,           NZV_FLAGS },

	//MLA: the console's word, the flags stay. Records 37, 101, 19, 0, 2 and 4
	{ MLA_RD_RM,  0x4D5D160D, 0x00000001, 0x00000084, FN | FZ,           0x00000108, FN | FZ,      ALL_FLAGS },
	{ MLA_RD_RM,  0xFCA904E7, 0x00000002, 0xAAAAAAAA, FC,                0xAAAAAAAE, FC,           ALL_FLAGS },
	{ MLA_RD_RM,  0xB64CFDF7, 0x0000005D, 0x000000DC, 0,                 0xFFD4CBD8, 0,            ALL_FLAGS },
	{ MLA_RD_RM,  0x00004000, 0x050C368D, 0x1DCE4E7B, FZ | FV,           0xA2AACDFE, FZ | FV,      ALL_FLAGS },
	{ MLA_RD_RM,  0x5079FC5D, 0xDABF5585, 0x2DCFC8AE, FN,                0x3A2AF8CC, FN,           ALL_FLAGS },
	{ MLA_RD_RM,  0x20000000, 0x7FFFFFFF, 0x91E47F62, FN | FC | FV,      0x00000000, FN | FC | FV, ALL_FLAGS },

	//MLAS: the same words, with their N and Z
	{ MLAS_RD_RM, 0x4D5D160D, 0x00000001, 0x00000084, FN | FZ | FV,      0x00000108, FV,           NZV_FLAGS },
	{ MLAS_RD_RM, 0xFCA904E7, 0x00000002, 0xAAAAAAAA, FZ | FC,           0xAAAAAAAE, FN,           NZV_FLAGS },
	{ MLAS_RD_RM, 0xB64CFDF7, 0x0000005D, 0x000000DC, FZ | FV,           0xFFD4CBD8, FN | FV,      NZV_FLAGS },
	{ MLAS_RD_RM, 0x00004000, 0x050C368D, 0x1DCE4E7B, 0,                 0xA2AACDFE, FN,           NZV_FLAGS },
	{ MLAS_RD_RM, 0x5079FC5D, 0xDABF5585, 0x2DCFC8AE, FN | FZ | FC,      0x3A2AF8CC, 0,            NZV_FLAGS },
	{ MLAS_RD_RM, 0x20000000, 0x7FFFFFFF, 0x91E47F62, FN | FV,           0x00000000, FZ | FV,      NZV_FLAGS },
};

static void run_mul_cases(const char* check, const MulCase* cases, u32 count)
{
	for (u32 i = 0; i < count; i++)
	{
		const MulCase& c = cases[i];
		Insn in = { c.opcode, { c.rn, c.rs, c.rm, 0x33333333 }, c.flags, c.rd_after, c.flags_after, c.flags_mask };

		run_insn(check, i, in);
	}
}

static void run_mul_rd_rm()
{
	run_mul_cases("mul-rd-rm", mul_rd_rm, sizeof(mul_rd_rm) / sizeof(mul_rd_rm[0]));
}

/*
	C behind MULS and MLAS with Rd == Rm. This is NOT measured on a console: the probe has "mla r0, r0, r5, r6"
	without S, and behind "muls r9, r9, r5" it stores the registers and goes on with a SUBS, the CPSR of that
	instruction is never read (model/hw/arm/k_mulsem.s). The rows pin what the emulator does, so that a change of
	it shows.

	It is two things the console has, put together. C behind MULS and MLAS with Rd != Rm is the bit the barrel
	shifter drops in the last step of the multiplier: the step shifts its operand left by 2k, or by 2k + 1 for a
	Booth digit of 2 or -2, and a shift by 0 passes the old C on (all 2048 flag words of the probe). And with
	Rd == Rm the operand of every step is Rd as the steps before left it (all 1024 MLA words of the probe). So C is
	bit 32 - shift of Rd as the last step finds it.

	MULS: Rd is 0 in every step, C is clear behind every multiply whose last step shifts, whatever Rm is. MLAS:
	Rd going into the last step, from the steps written out apart from the emulator's code (Rd = Rn, then
	Rd +- Rd << shift per step; the same steps give the console's MLA word in all 1024 records):

	  record 37   Rs 00000001   1 step, LSL #0                                    C stays
	  record 101  Rs 00000002   2 steps, the last LSL #2,   Rd 55555556, bit 30   C set
	  record 0    Rs 050C368D   14 steps, the last LSL #26, Rd AAAACDFE, bit 6    C set (bit 6 of Rm is clear)
	  record 19   Rs 0000005D   4 steps, the last LSL #6,   Rd FFFF55D8, bit 26   C set
	  record 4    Rs 7FFFFFFF   16 steps, the last LSL #31, Rd 00000000, bit 1    C clear

	Rd behind the instruction is the console's word in every row.
*/
static const MulCase mul_rd_rm_c[] =
{
	//             Rm          Rs          Rn          in                  Rd after    after         compared
	//MULS. Records 37, twice, 101 (bit 30 of Rm is set) and 2
	{ MULS_RD_RM, 0x4D5D160D, 0x00000001, 0x11111111, FC,                0x00000000, FZ | FC,      ALL_FLAGS },
	{ MULS_RD_RM, 0x4D5D160D, 0x00000001, 0x11111111, FN,                0x00000000, FZ,           ALL_FLAGS },
	{ MULS_RD_RM, 0xFCA904E7, 0x00000002, 0x11111111, FC,                0x00000000, FZ,           ALL_FLAGS },
	{ MULS_RD_RM, 0x5079FC5D, 0xDABF5585, 0x11111111, FN | FC | FV,      0x00000000, FZ | FV,      ALL_FLAGS },

	//MLAS. Records 37, twice, 101, 0, 19 and 4
	{ MLAS_RD_RM, 0x4D5D160D, 0x00000001, 0x00000084, FC,                0x00000108, FC,           ALL_FLAGS },
	{ MLAS_RD_RM, 0x4D5D160D, 0x00000001, 0x00000084, FN | FZ,           0x00000108, 0,            ALL_FLAGS },
	{ MLAS_RD_RM, 0xFCA904E7, 0x00000002, 0xAAAAAAAA, 0,                 0xAAAAAAAE, FN | FC,      ALL_FLAGS },
	{ MLAS_RD_RM, 0x00004000, 0x050C368D, 0x1DCE4E7B, FV,                0xA2AACDFE, FN | FC | FV, ALL_FLAGS },
	{ MLAS_RD_RM, 0xB64CFDF7, 0x0000005D, 0x000000DC, FZ,                0xFFD4CBD8, FN | FC,      ALL_FLAGS },
	{ MLAS_RD_RM, 0x20000000, 0x7FFFFFFF, 0x91E47F62, FN | FC,           0x00000000, FZ,           ALL_FLAGS },
};

static void run_mul_rd_rm_c()
{
	run_mul_cases("mul-rd-rm-c", mul_rd_rm_c, sizeof(mul_rd_rm_c) / sizeof(mul_rd_rm_c[0]));
}

/*
	The traps. SWI and the undefined instruction trap put the address of the instruction behind them into R14 of
	the mode they enter, SVC and UND, and go on at 0x08 and 0x04 (ARM7DI data sheet 3.4, 4.10). "movs pc, lr" in the
	vector comes back to the instruction behind the trap.

	The console has it in the timing kernels swi and und of wren7-rtl (model/tests/hw/timing/src/swi.s, und.s):
	"movs pc, lr" in the vector and 8 traps in a row per iteration. An iteration takes 415.99 and 451.04 clocks
	(model/tests/hw/timing/SUMMARY.txt), the time of 8 traps and 8 returns; a trap that came back one instruction
	further would run 4 of them. The cases are that program: one trap, and 8 in a row.
*/
#define MOVS_PC_LR 0xE1B0F00E
#define MODE_SVC 0x13
#define MODE_UND 0x1B

#define TRAP_ADDR 0x40

struct Trap
{
	const char* name;
	u32 opcode;
	u32 vector;
	u32 mode;
	u32 count;			//traps in a row
};

static const Trap traps[] =
{
	{ "swi 0", 0xEF000000, 0x08, MODE_SVC, 1 },
	{ "swi 0", 0xEF000000, 0x08, MODE_SVC, 8 },
	{ "undefined", 0xE7F000F0, 0x04, MODE_UND, 1 },
	{ "undefined", 0xE7F000F0, 0x04, MODE_UND, 8 },
};

static void run_traps()
{
	for (size_t i = 0; i < sizeof(traps) / sizeof(traps[0]); i++)
	{
		const Trap& t = traps[i];
		Arm7Context* ctx = arm7m_context();
		u32 handler = MOVS_PC_LR;

		arm7m_power_on();
		arm7m_clear();
		memcpy(arm7m_ram() + t.vector, &handler, 4);

		for (u32 n = 0; n < t.count; n++)
			memcpy(arm7m_ram() + TRAP_ADDR + 4 * n, &t.opcode, 4);

		arm7m_release_reset();
		ctx->regs[R15_ARM_NEXT].I = TRAP_ADDR;

		char what[64];

		snprintf(what, sizeof(what), "trap case %u (%s x %u)", (u32)i, t.name, t.count);

		bool pass = true;
		std::string wrong;

		for (u32 n = 0; pass && n < t.count; n++)
		{
			u32 behind = TRAP_ADDR + 4 * n + 4;
			char text[128];

			//the trap
			if (arm7_guarded(what, &step, 0) != TGR_OK)
			{
				pass = false;
				break;
			}

			u32 next = ctx->regs[R15_ARM_NEXT].I;
			u32 mode = arm7m_cpsr() & 0x1F;
			u32 r14 = ctx->regs[14].I;

			if (next != t.vector || mode != t.mode || r14 != behind)
			{
				snprintf(text, sizeof(text), " trap %u at %08X: next instruction at %08X, mode %02X, R14 %08X, has to be %08X, %02X, %08X;",
					n, behind - 4, next, mode, r14, t.vector, t.mode, behind);
				wrong = text;
				pass = false;
				break;
			}

			//movs pc, lr
			if (arm7_guarded(what, &step, 0) != TGR_OK)
			{
				pass = false;
				break;
			}

			next = ctx->regs[R15_ARM_NEXT].I;
			mode = arm7m_cpsr() & 0x1F;

			if (next != behind || mode != MODE_SVC)
			{
				snprintf(text, sizeof(text), " return of trap %u: next instruction at %08X, mode %02X, has to be %08X, %02X;",
					n, next, mode, behind, MODE_SVC);
				wrong = text;
				pass = false;
			}
		}

		arm7_count("trap", pass);

		if (!wrong.empty())
			fprintf(arm7_report, "trap case %u: %s x %u:%s\n", (u32)i, t.name, t.count, wrong.c_str());
	}
}

/*
	The interrupt controller in front of nFIQ, as wren7-rtl measured it on a Dreamcast with the one sample
	interval (SCIPD bit 10) as the only request (model/NOTES.md, "Interrupts"; the kernel is
	model/hw/arm/k_blkphase.s, the captures are in model/tests/hw/fiqdiag/hw):

	  - a request reaches nFIQ only with a level other than 0 in SCILV0-2. With all three 0 the enabled, pending
	    request never interrupts (d1.bin, d4.bin: FIQ enabled over 2048 adds, r0 still has all its samples);
	    SCILV0 = 0x80 is level 1 and interrupts, as 0xFF in all three, level 7, does
	  - reading L (0x802D00) is the acknowledge: it releases nFIQ, and it returns the level of the request
	    (a1.bin: r3 = 1). A handler that reads a RAM word in its place, clears the request with SCIRE and writes
	    M is entered again as soon as it returns, with one M write (a2.bin) and with four (a4.bin): 256 entries
	    with 45 and 17 adds in all, fewer than a single sample has room for
	  - writing bit 0 of M (0x802D04) ends the service: the next request is let in. With the L read and no M
	    write the first interrupt is the last (a5.bin: one sample taken off r0, the 2048 adds run out). One M
	    write is enough (a1.bin: all 256 samples)
	  - SCILV0-2 read back as 0. k_blkphase.s stores the registers as it finds them at 0x1FF100, the second
	    region of d*.bin: d3.bin and d4.bin have 0 in all three, and an L of 1 and 7, the level the job before
	    them set up with SCILV0 = 0x80 and with 0xFF in all three
	  - L does not keep the level of the last request that interrupted. d4.bin finds 7 in it, runs with a level
	    of 0 and takes no interrupt, and d5.bin, the job behind it, finds 0

	The job file of d1 to d5 is not kept, model/NOTES.md has what they ran with. The scenarios do what the
	kernel does, with the bus accesses of its prologue and of its handler made by the test: the vector has a
	"mov r0, r0" for the entry and "subs pc, lr, #4" for the return.
*/
#define AICA_SCIEB 0x80289C
#define AICA_SCIRE 0x8028A4
#define AICA_SCILV0 0x8028A8
#define AICA_L 0x802D00
#define AICA_M 0x802D04

#define SAMPLE_INTERVAL 0x400

#define MODE_FIQ 0x11

#define FIQ_VECTOR 0x1C
#define FIQ_MAIN 0x100
#define FIQ_SLED (FIQ_MAIN + 12)

struct Scenario
{
	const char* name;
	//Leaves what is wrong in text, or nothing
	void (*run)(std::string& text);
};

struct ScenarioRun
{
	const Scenario* scenario;
	std::string text;
};

static bool in_fiq()
{
	return (arm7m_cpsr() & 0x1F) == MODE_FIQ;
}

//"mrs r1, cpsr", "bic r1, r1, #0x40" and "msr cpsr_c, r1" at FIQ_MAIN: FIQs on. The ARM is left in front of a sled
//of "add r7, r7, #1"
static void fiq_on()
{
	arm7m_context()->regs[R15_ARM_NEXT].I = FIQ_MAIN;

	for (u32 i = 0; i < 3; i++)
		arm7m_step();
}

//The prologue of k_blkphase.s: the levels, nothing pending, nothing in service, only the one sample interval
//enabled, and FIQs on. Returns L as the prologue finds it
static u32 fiq_prologue(u32 scilv0, u32 scilv1, u32 scilv2)
{
	u32 l = arm7m_read32(AICA_L);

	arm7m_write32(AICA_SCILV0, scilv0);
	arm7m_write32(AICA_SCILV0 + 4, scilv1);
	arm7m_write32(AICA_SCILV0 + 8, scilv2);
	arm7m_write32(AICA_SCIRE, 0x7FF);
	arm7m_write32(AICA_M, 1);
	arm7m_write32(AICA_SCIEB, SAMPLE_INTERVAL);
	fiq_on();

	return l;
}

//The machine is reset, and ARMRST released with the program in wave RAM
static void fiq_load()
{
	static const u32 vector[] =
	{
		0xE1A00000,		//mov r0, r0
		0xE25EF004,		//subs pc, lr, #4
	};

	static const u32 main_code[] =
	{
		0xE10F1000,		//mrs r1, cpsr
		0xE3C11040,		//bic r1, r1, #0x40
		0xE121F001,		//msr cpsr_c, r1
	};

	static const u32 add_r7 = 0xE2877001;

	arm7m_power_on();
	arm7m_clear();
	memcpy(arm7m_ram() + FIQ_VECTOR, vector, sizeof(vector));
	memcpy(arm7m_ram() + FIQ_MAIN, main_code, sizeof(main_code));

	for (u32 i = 0; i < 64; i++)
		memcpy(arm7m_ram() + FIQ_SLED + 4 * i, &add_r7, 4);

	arm7m_release_reset();
}

//The machine is reset, and the program runs its prologue
static void fiq_setup(u32 scilv0, u32 scilv1, u32 scilv2)
{
	fiq_load();
	fiq_prologue(scilv0, scilv1, scilv2);
}

//A sample passes and the ARM gets to its next instruction. True: that was the entry of the FIQ, the ARM is behind
//the first instruction of the vector with the return address in R14_fiq
static bool sample_interrupts(std::string& text)
{
	Arm7Context* ctx = arm7m_context();
	u32 at = ctx->regs[R15_ARM_NEXT].I;

	arm7m_sample();
	arm7m_step();

	if (!in_fiq())
		return false;

	if (ctx->regs[R15_ARM_NEXT].I != FIQ_VECTOR + 4 || ctx->regs[14].I != at + 4)
		text += " the FIQ is entered with a wrong PC or R14;";

	return true;
}

//"subs pc, lr, #4" of the vector
static void fiq_return(std::string& text)
{
	arm7m_step();

	if (in_fiq())
		text += " the return from the FIQ does not leave FIQ mode;";
}

static void fiq_level_0(std::string& text)
{
	fiq_setup(0, 0, 0);

	for (u32 i = 0; i < 4; i++)
	{
		if (sample_interrupts(text))
		{
			text += " a request of level 0 interrupts;";
			return;
		}

		arm7m_step();
	}
}

static void fiq_level(std::string& text, u32 scilv0, u32 scilv1, u32 scilv2, u32 level)
{
	char number[64];

	fiq_setup(scilv0, scilv1, scilv2);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	u32 l = arm7m_read32(AICA_L);

	if (l != level)
	{
		snprintf(number, sizeof(number), " L reads %X, has to be %X;", l, level);
		text += number;
	}
}

static void fiq_level_1(std::string& text)
{
	fiq_level(text, 0x80, 0, 0, 1);
}

static void fiq_level_7(std::string& text)
{
	fiq_level(text, 0xFF, 0xFF, 0xFF, 7);
}

//a5: the handler clears the request and reads L, M is not written
static void fiq_no_accept(std::string& text)
{
	fiq_setup(0x80, 0, 0);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);
	arm7m_read32(AICA_L);
	fiq_return(text);

	for (u32 i = 0; i < 4; i++)
	{
		arm7m_step();

		if (in_fiq() || sample_interrupts(text))
		{
			text += " there is another interrupt though M was not written;";
			return;
		}
	}
}

//a2, a4: the handler clears the request and writes M, L is not read
static void fiq_no_acknowledge(std::string& text, u32 m_writes)
{
	fiq_setup(0x80, 0, 0);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	for (u32 entry = 0; entry < 3; entry++)
	{
		arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);

		for (u32 i = 0; i < m_writes; i++)
			arm7m_write32(AICA_M, 1);

		fiq_return(text);

		//no sample passes
		arm7m_step();

		if (!in_fiq())
		{
			text += " nFIQ is released though L was not read;";
			return;
		}
	}
}

static void fiq_no_acknowledge_1(std::string& text)
{
	fiq_no_acknowledge(text, 1);
}

static void fiq_no_acknowledge_4(std::string& text)
{
	fiq_no_acknowledge(text, 4);
}

//a1: the handler clears the request, reads L and writes M once
static void fiq_serviced(std::string& text)
{
	fiq_setup(0x80, 0, 0);

	for (u32 sample = 0; sample < 3; sample++)
	{
		if (!sample_interrupts(text))
		{
			text += " a sample does not interrupt;";
			return;
		}

		arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);
		arm7m_read32(AICA_L);
		arm7m_write32(AICA_M, 1);
		fiq_return(text);

		//until the next sample
		arm7m_step();

		if (in_fiq())
		{
			text += " the handler is entered again without a new request;";
			return;
		}
	}
}

//d3, d4, d5: a job with level 7, a job with level 0, and L as that one and the job behind it find it. A job of
//k_blkphase.s ends in its handler in front of the L read, and the next one starts with the release of ARMRST
static void fiq_level_0_l(std::string& text)
{
	char number[96];

	fiq_setup(0xFF, 0xFF, 0xFF);

	if (!sample_interrupts(text))
	{
		text += " the request of level 7 does not interrupt;";
		return;
	}

	arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);
	arm7m_read32(AICA_L);
	arm7m_write32(AICA_M, 1);
	fiq_return(text);

	if (!sample_interrupts(text))
	{
		text += " the second request of level 7 does not interrupt;";
		return;
	}

	arm7m_hold_reset();
	arm7m_release_reset();

	u32 l = fiq_prologue(0, 0, 0);

	if (l != 7)
	{
		snprintf(number, sizeof(number), " the job behind the one with level 7 finds %X in L, has to be 7;", l);
		text += number;
	}

	for (u32 i = 0; i < 4; i++)
	{
		if (sample_interrupts(text))
		{
			text += " a request of level 0 interrupts;";
			return;
		}

		arm7m_step();
	}

	arm7m_hold_reset();
	arm7m_release_reset();

	l = arm7m_read32(AICA_L);

	if (l != 0)
	{
		snprintf(number, sizeof(number), " the job behind the one with level 0 finds %X in L, has to be 0;", l);
		text += number;
	}
}

/*
	ARMRST with a request in service. Nothing of the console is in these two: every FIQ kernel of wren7-rtl reads L
	and writes M in its prologue, before it turns FIQs on, and so no job shows what the reset left of the service
	of the job before (KNOWN_FAILURES.md, D5).

	The release of ARMRST ends the service, as a write of bit 0 of M does. That is chosen, because software depends
	on it: a program that ARMRST starts has to get its interrupts whatever the program in front of it was in the
	middle of. The ARM crt0 of KallistiOS writes M in its FIQ handler only; with the service kept it never gets
	there when the reset comes between the L read and the M write of the program in front of it, and the sound
	program of the BIOS is at that point in about 2 of 100 resets.

	nFIQ is not released by it: that is what the emulator did before the handshake was the console's, and what
	the captures say of everything but a read of L (a2.bin, a4.bin). It is not measured over ARMRST either.
*/
//ARMRST between a handler's read of L and its write of M. The program behind it sets its level and its enable,
//turns FIQs on and writes M in its handler only
static void fiq_armrst_in_service(std::string& text)
{
	char number[64];

	fiq_setup(0xFF, 0xFF, 0xFF);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);
	arm7m_read32(AICA_L);

	arm7m_hold_reset();
	arm7m_release_reset();

	arm7m_write32(AICA_SCILV0, 0x80);
	arm7m_write32(AICA_SCILV0 + 4, 0);
	arm7m_write32(AICA_SCILV0 + 8, 0);
	arm7m_write32(AICA_SCIEB, SAMPLE_INTERVAL);
	fiq_on();

	for (u32 sample = 0; sample < 3; sample++)
	{
		if (!sample_interrupts(text))
		{
			snprintf(number, sizeof(number), " sample %u does not interrupt the program behind ARMRST;", sample);
			text += number;
			return;
		}

		arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);

		u32 l = arm7m_read32(AICA_L);

		if (l != 1)
		{
			snprintf(number, sizeof(number), " L reads %X in its handler, has to be 1;", l);
			text += number;
		}

		arm7m_write32(AICA_M, 1);
		fiq_return(text);

		//until the next sample
		arm7m_step();

		if (in_fiq())
		{
			text += " the handler is entered again without a new request;";
			return;
		}
	}
}

//ARMRST in front of the handler's read of L, with the request cleared: nFIQ is still pulled. The program behind it
//turns FIQs on and is interrupted without a sample, L has the level of the request that pulled it (as d4.bin finds
//the 7 of the job before), and its handler ends that interrupt
static void fiq_armrst_nfiq(std::string& text)
{
	char number[64];

	fiq_setup(0xFF, 0xFF, 0xFF);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);

	arm7m_hold_reset();
	arm7m_release_reset();
	fiq_on();
	arm7m_step();

	if (!in_fiq())
	{
		text += " nFIQ is released by ARMRST: the program behind it is not interrupted;";
		return;
	}

	u32 l = arm7m_read32(AICA_L);

	if (l != 7)
	{
		snprintf(number, sizeof(number), " L reads %X behind ARMRST, has to be 7;", l);
		text += number;
	}

	arm7m_write32(AICA_M, 1);
	fiq_return(text);
	arm7m_step();

	if (in_fiq())
		text += " the handler is entered again though it has read L;";
}

static const Scenario fiq_scenarios[] =
{
	{ "level 0", &fiq_level_0 },
	{ "level 1", &fiq_level_1 },
	{ "level 7", &fiq_level_7 },
	{ "L read, M not written", &fiq_no_accept },
	{ "L not read, M written", &fiq_no_acknowledge_1 },
	{ "L not read, M written 4 times", &fiq_no_acknowledge_4 },
	{ "L read, M written", &fiq_serviced },
	{ "L behind level 7 and behind level 0", &fiq_level_0_l },
	{ "ARMRST between the L read and the M write", &fiq_armrst_in_service },
	{ "ARMRST in front of the L read", &fiq_armrst_nfiq },
};

static void scilv_reads_0(std::string& text, u32 index)
{
	char number[64];

	fiq_setup(0, 0, 0);
	arm7m_write32(AICA_SCILV0 + 4 * index, 0xFF);

	u32 value = arm7m_read32(AICA_SCILV0 + 4 * index);

	if (value != 0)
	{
		snprintf(number, sizeof(number), " SCILV%u reads %X behind a write of FF, has to be 0;", index, value);
		text += number;
	}

	//it is the register that was written: the request has its level
	if (!sample_interrupts(text) || arm7m_read32(AICA_L) != (1u << index))
		text += " the level that was written is not the level of the request;";
}

static void scilv0_reads_0(std::string& text) { scilv_reads_0(text, 0); }
static void scilv1_reads_0(std::string& text) { scilv_reads_0(text, 1); }
static void scilv2_reads_0(std::string& text) { scilv_reads_0(text, 2); }

static const Scenario scilv_scenarios[] =
{
	{ "SCILV0", &scilv0_reads_0 },
	{ "SCILV1", &scilv1_reads_0 },
	{ "SCILV2", &scilv2_reads_0 },
};

/*
	The reset: ARMRST is held and released. R14_svc and SPSR_svc take the PC and the CPSR, the CPSR gets SVC mode
	with I and F set, and the PC is 0 (ARM7DI data sheet 3.5). Nothing else is written: N, Z, C and V stay, and so
	does every other register, the banks of the other modes among them.

	The console has it out of FIQ mode in wren7-rtl's fiqdiag (model/tests/hw/fiqdiag/hw). d3.bin ends in its FIQ
	handler with CPSR = 0x600000D1. d4.bin, the job behind it, has that word in SPSR_svc and the address the ARM
	was spinning at in R14_svc, and the CPSR it ends with, 0x60000093, still has Z and C and no N or V. The
	reset out of SVC mode is in model/tests/hw/bringup (arm7.wren7.bringup).

	A case gets to its mode and flags with "msr cpsr, r0" at RESET_AT and is reset behind that instruction. It
	looks at the registers in SVC mode, and then at the bank of its mode again, to which the same instruction at
	0 takes it back.
*/
#define MSR_CPSR_R0 0xE129F000
#define MODE_USR 0x10
#define RESET_AT 0x40

//what the registers hold: r0 to r12 and the bank of SVC, and the bank of the mode of the case
#define SVC_VALUE(r) (0x10000000 + (r))
#define MODE_VALUE(r) (0x20000000 + (r))
#define SVC_SPSR_BEFORE 0x00000010

static void reset_expect(std::string& text, const std::string& what, u32 value, u32 expected)
{
	char number[64];

	if (value == expected)
		return;

	snprintf(number, sizeof(number), " is %08X, has to be %08X;", value, expected);
	text += " " + what + number;
}

//cpsr: the CPSR the reset finds. spsr: the SPSR of that mode, for the modes that have one and keep it (the reset
//writes the one of SVC)
static void reset_case(std::string& text, u32 cpsr, u32 spsr)
{
	Arm7Context* ctx = arm7m_context();
	const u32 msr = MSR_CPSR_R0;
	const u32 mode = cpsr & 0x1F;
	const bool own_bank = mode != MODE_SVC;
	const bool own_spsr = own_bank && mode != MODE_USR;
	const u32 bank = mode == MODE_FIQ ? 8 : 13;		//the first register of the mode's bank

	arm7m_power_on();
	arm7m_clear();
	memcpy(arm7m_ram(), &msr, 4);
	memcpy(arm7m_ram() + RESET_AT, &msr, 4);
	arm7m_release_reset();

	ctx->regs[0].I = cpsr;

	for (u32 r = 1; r <= 14; r++)
		ctx->regs[r].I = SVC_VALUE(r);

	ctx->regs[RN_SPSR].I = SVC_SPSR_BEFORE;
	ctx->regs[R15_ARM_NEXT].I = RESET_AT;
	arm7m_step();

	if (arm7m_cpsr() != cpsr)
	{
		text += " msr cpsr, r0 does not get to the mode of the case;";
		return;
	}

	if (own_bank)
	{
		for (u32 r = bank; r <= 14; r++)
			ctx->regs[r].I = MODE_VALUE(r);
	}

	if (own_spsr)
		ctx->regs[RN_SPSR].I = spsr;

	arm7m_hold_reset();
	arm7m_release_reset();

	reset_expect(text, "the address of the next instruction", ctx->regs[R15_ARM_NEXT].I, 0);
	reset_expect(text, "CPSR", arm7m_cpsr(), (cpsr & 0xF0000000) | 0xC0 | MODE_SVC);
	reset_expect(text, "SPSR_svc", ctx->regs[RN_SPSR].I, cpsr);
	reset_expect(text, "r0", ctx->regs[0].I, cpsr);

	//r14: the address of the instruction the ARM was at, as in arm7.wren7.bringup
	for (u32 r = 1; r <= 14; r++)
		reset_expect(text, "r" + std::to_string(r) + " in SVC mode", ctx->regs[r].I, r == 14 ? RESET_AT + 4 : SVC_VALUE(r));

	//msr cpsr, r0
	arm7m_step();
	reset_expect(text, "CPSR back in the mode", arm7m_cpsr(), cpsr);

	for (u32 r = 1; r <= 14; r++)
	{
		u32 expected = (own_bank && r >= bank) ? MODE_VALUE(r) : r == 14 ? RESET_AT + 4 : SVC_VALUE(r);

		reset_expect(text, "r" + std::to_string(r) + " back in the mode", ctx->regs[r].I, expected);
	}

	if (mode != MODE_USR)
		reset_expect(text, "SPSR back in the mode", ctx->regs[RN_SPSR].I, own_spsr ? spsr : cpsr);
}

//d4.bin
static void reset_fiq(std::string& text) { reset_case(text, 0x600000D1, 0x90000010); }
static void reset_svc(std::string& text) { reset_case(text, 0xF0000013, 0); }
static void reset_irq(std::string& text) { reset_case(text, 0x00000092, 0xF0000010); }
static void reset_usr(std::string& text) { reset_case(text, 0x90000010, 0); }

static const Scenario reset_scenarios[] =
{
	{ "out of FIQ mode, Z and C set", &reset_fiq },
	{ "out of SVC mode, all flags set", &reset_svc },
	{ "out of IRQ mode, no flag set", &reset_irq },
	{ "out of user mode, N and V set", &reset_usr },
};

/*
	The emulator resets the AICA in front of the sound CPU, and the reset of the AICA releases ARMRST: the reset
	of the ARM runs on what the sound CPU was created with. That has to be what its own reset gives it, the
	registers, the mode and the interrupt masks, with the ARM held in reset.
*/
static void run_power_on()
{
	bool pass = arm7m_created_reset();

	arm7_count("power-on", pass);

	if (!pass)
		fprintf(arm7_report, "power-on: the sound CPU that was created and not reset yet does not hold the registers, the mode and the "
			"interrupt masks of its reset, or is not held in reset\n");
}

/*
	The reset of the machine comes at any time: a guest asks for it (SB_SFRES) and the emulator resets its devices
	where they are. It has to leave nothing of the interrupt controller in front of nFIQ: no request and no level
	from the AICA, whose registers it clears, nFIQ released, nothing in service and 0 in L. The program that is
	started behind it gets the interrupts it asks for and no other, whatever the program in front of it was in
	the middle of.

	Nothing of the console is in this: its jobs start behind an ARMRST, and none behind a reset of the machine.
*/
//The reset finds a request latched and nFIQ pulled: what the controller holds behind it
static void power_on_controller(std::string& text)
{
	fiq_setup(0x80, 0, 0);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	arm7m_power_on();

	if (!arm7m_controller_idle())
		text += " the interrupt controller holds a request, a level, nFIQ or a service;";
}

//The reset finds the handler between its read of L and its write of M. The next program sets the level and the
//enable and writes M in its handler only, as the ARM crt0 of KallistiOS does
static void power_on_in_service(std::string& text)
{
	fiq_setup(0x80, 0, 0);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	arm7m_write32(AICA_SCIRE, SAMPLE_INTERVAL);
	arm7m_read32(AICA_L);

	fiq_load();
	arm7m_write32(AICA_SCILV0, 0x80);
	arm7m_write32(AICA_SCIEB, SAMPLE_INTERVAL);
	fiq_on();

	if (!sample_interrupts(text))
		text += " the program behind the reset does not get its interrupt;";
}

//The reset finds the handler in front of its read of L, with the request still pending. The next program enables
//nothing
static void power_on_nfiq(std::string& text)
{
	char number[64];

	fiq_setup(0xFF, 0xFF, 0xFF);

	if (!sample_interrupts(text))
	{
		text += " the request does not interrupt;";
		return;
	}

	fiq_load();
	fiq_on();
	arm7m_step();

	if (in_fiq())
	{
		text += " the program behind the reset gets an interrupt it has no request for;";
		return;
	}

	u32 l = arm7m_read32(AICA_L);

	if (l != 0)
	{
		snprintf(number, sizeof(number), " L reads %X, has to be 0;", l);
		text += number;
	}

	arm7m_write32(AICA_M, 1);
	arm7m_step();

	if (in_fiq())
		text += " the program behind the reset gets an interrupt behind its write of M and has no request for it;";
}

static const Scenario power_on_scenarios[] =
{
	{ "reset of the machine with a request latched: the controller", &power_on_controller },
	{ "reset of the machine between the L read and the M write", &power_on_in_service },
	{ "reset of the machine with nFIQ pulled", &power_on_nfiq },
};

static void run_scenario(void* arg)
{
	ScenarioRun* run = (ScenarioRun*)arg;

	run->scenario->run(run->text);
}

static void run_scenarios(const char* check, const Scenario* scenarios, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		ScenarioRun run;

		run.scenario = &scenarios[i];

		std::string what = std::string(check) + " case " + scenarios[i].name;
		TestGuardResult guard = arm7_guarded(what, &run_scenario, &run);

		arm7_count(check, guard == TGR_OK && run.text.empty());

		if (guard == TGR_OK && !run.text.empty())
			fprintf(arm7_report, "%s case %u: %s:%s\n", check, (u32)i, scenarios[i].name, run.text.c_str());
	}
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

			arm7m_power_on();
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

	if (arm7_selected("ror"))
		run_ror();

	if (arm7_selected("mul-rd-rm"))
		run_mul_rd_rm();

	if (arm7_selected("mul-rd-rm-c"))
		run_mul_rd_rm_c();

	if (arm7_selected("trap"))
		run_traps();

	if (arm7_selected("fiq"))
		run_scenarios("fiq", fiq_scenarios, sizeof(fiq_scenarios) / sizeof(fiq_scenarios[0]));

	if (arm7_selected("scilv"))
		run_scenarios("scilv", scilv_scenarios, sizeof(scilv_scenarios) / sizeof(scilv_scenarios[0]));

	if (arm7_selected("reset"))
		run_scenarios("reset", reset_scenarios, sizeof(reset_scenarios) / sizeof(reset_scenarios[0]));

	if (arm7_selected("power-on"))
	{
		run_power_on();
		run_scenarios("power-on", power_on_scenarios, sizeof(power_on_scenarios) / sizeof(power_on_scenarios[0]));
	}

	return arm7_suite_end();
}
