/*
	Directed SH4 vectors: the carry, borrow, overflow and compare instructions on operands at the edges (0,
	0xFFFFFFFF, 0x7FFFFFFF, 0x80000000) with T clear and set, and the delayed branches with a delay slot that
	changes what the branch uses: T, the register it jumps through, PR. The single-step vectors are random and
	have none of these: no ADDC whose sum is 0xFFFFFFFF with T set, for one, and add r1,r1 in every delay slot,
	which leaves T and PR as they are. And what the single-step vectors expect another way than an SH4 does it:
	the loads of FPSCR (they store bits that the register does not have), RTE to the other register bank (they
	run the delay slot on the bank of the old SR), TRAPA (they have it as a nop) and FTRC of a NaN and of a
	number above the range (they have the results of their generator's host). And FMAC on operands that show
	whether the sum is rounded once, which no random vector does.

	sh4_directed_gen <dir> writes vector files in the format of the single-step vectors and under their file
	names (ss_format.h), so the suites run them as they run those and the policy table applies as it is. A vector
	is nop / the instruction / nop / nop on a state that has a value of its own in every register, and nop /
	the branch / its delay slot / nop for a branch. Nothing but what the table of the vector names may change.

	What an instruction leaves is written down by hand in the tables, from its definition in the SH-4 software
	manual. Nothing of it comes from an emulator.
*/
#include "ss_format.h"

#include <cstdio>
#include <cstring>
#include <map>

//SR bits a case sets going in and expects coming out
#define FT 0x001
#define FQ 0x100
#define FM 0x200

//MD, BL and the interrupt mask: privileged, bank 0, nothing gets in
#define SR_BASE 0x500000F0

#define PC_BASE 0x8C010000
#define PR_BASE 0x8C0F6000

#define FPSCR_BASE 0x00040001

#define OP_NOP 0x0009
#define OP_CLRT 0x0008
#define OP_SETT 0x0018
#define OP_MOV_R4_R3 0x6343
#define OP_LDS_R4_PR 0x442A
#define OP_STS_PR_R5 0x052A
#define OP_MOV_R0_R8 0x6803
#define OP_MOVT_R8 0x0829

struct Case
{
	u32 rn;			//Rn going in
	u32 rm;			//Rm going in, the immediate of an instruction that has one
	u32 flags;		//FT, FQ, FM going in
	u32 rn_after;
	u32 flags_after;
};

enum Form
{
	FORM_RM_RN,		//op Rm,Rn: Rn = r3, Rm = r4
	FORM_RN_RN,		//op Rn,Rn: both operands are r3, rm of a case is not used
	FORM_RN,		//op Rn: Rn = r3
	FORM_IMM_R0,	//op #imm,R0: Rn = r0, the low byte of rm is the immediate
};

struct Group
{
	const char* file;	//the vector file of the instruction, without SS_FILE_SUFFIX
	u16 opcode;
	Form form;
	const Case* cases;
	u32 count;
};

//// Rn + Rm + T -> Rn, carry -> T

static const Case addc[] =
{
	//Rn          Rm          in  Rn after    after
	{ 0x00000000, 0x00000000, 0,  0x00000000, 0 },
	{ 0x00000000, 0x00000000, FT, 0x00000001, 0 },
	{ 0xFFFFFFFF, 0x00000000, 0,  0xFFFFFFFF, 0 },
	{ 0xFFFFFFFF, 0x00000000, FT, 0x00000000, FT },		//the carry comes from adding T
	{ 0x00000000, 0xFFFFFFFF, FT, 0x00000000, FT },
	{ 0xFFFFFFFF, 0x00000001, 0,  0x00000000, FT },
	{ 0xFFFFFFFF, 0xFFFFFFFF, 0,  0xFFFFFFFE, FT },
	{ 0xFFFFFFFF, 0xFFFFFFFF, FT, 0xFFFFFFFF, FT },
	{ 0x7FFFFFFF, 0x80000000, 0,  0xFFFFFFFF, 0 },
	{ 0x7FFFFFFF, 0x80000000, FT, 0x00000000, FT },
	{ 0x7FFFFFFF, 0x00000000, FT, 0x80000000, 0 },
	{ 0x80000000, 0x80000000, 0,  0x00000000, FT },
	{ 0x80000000, 0x80000000, FT, 0x00000001, FT },
	{ 0x12345678, 0x00000000, 0,  0x12345678, 0 },		//Rm = 0: Rn + Rm is Rn, and that is no carry
	{ 0x00000001, 0xFFFFFFFE, 0,  0xFFFFFFFF, 0 },
	{ 0x00000001, 0xFFFFFFFE, FT, 0x00000000, FT },
};

static const Case addc_same[] =
{
	{ 0x00000000, 0, FT, 0x00000001, 0 },
	{ 0x80000000, 0, 0,  0x00000000, FT },
	{ 0x80000000, 0, FT, 0x00000001, FT },
	{ 0xFFFFFFFF, 0, FT, 0xFFFFFFFF, FT },
	{ 0x7FFFFFFF, 0, FT, 0xFFFFFFFF, 0 },
};

//// Rn - Rm - T -> Rn, borrow -> T

static const Case subc[] =
{
	{ 0x00000000, 0x00000000, 0,  0x00000000, 0 },
	{ 0x00000000, 0x00000000, FT, 0xFFFFFFFF, FT },		//the borrow comes from subtracting T
	{ 0x00000000, 0xFFFFFFFF, 0,  0x00000001, FT },
	{ 0x00000000, 0xFFFFFFFF, FT, 0x00000000, FT },
	{ 0xFFFFFFFF, 0xFFFFFFFF, 0,  0x00000000, 0 },
	{ 0xFFFFFFFF, 0xFFFFFFFF, FT, 0xFFFFFFFF, FT },
	{ 0x80000000, 0x7FFFFFFF, 0,  0x00000001, 0 },
	{ 0x80000000, 0x7FFFFFFF, FT, 0x00000000, 0 },
	{ 0x7FFFFFFF, 0x80000000, 0,  0xFFFFFFFF, FT },
	{ 0x7FFFFFFF, 0x80000000, FT, 0xFFFFFFFE, FT },
	{ 0x00000001, 0x00000000, FT, 0x00000000, 0 },
	{ 0x00000000, 0x00000001, 0,  0xFFFFFFFF, FT },
	{ 0x12345678, 0x00000000, 0,  0x12345678, 0 },
	{ 0x00000005, 0x00000005, FT, 0xFFFFFFFF, FT },
	{ 0xFFFFFFFF, 0x00000000, FT, 0xFFFFFFFE, 0 },
	{ 0x80000000, 0x80000000, FT, 0xFFFFFFFF, FT },
};

static const Case subc_same[] =
{
	{ 0x00000000, 0, 0,  0x00000000, 0 },
	{ 0x12345678, 0, FT, 0xFFFFFFFF, FT },
	{ 0xFFFFFFFF, 0, FT, 0xFFFFFFFF, FT },
	{ 0x80000000, 0, 0,  0x00000000, 0 },
};

//// 0 - Rm - T -> Rn, borrow -> T

static const Case negc[] =
{
	{ 0xDEADBEEF, 0x00000000, 0,  0x00000000, 0 },
	{ 0xDEADBEEF, 0x00000000, FT, 0xFFFFFFFF, FT },
	{ 0xDEADBEEF, 0x00000001, 0,  0xFFFFFFFF, FT },
	{ 0xDEADBEEF, 0x00000001, FT, 0xFFFFFFFE, FT },
	{ 0xDEADBEEF, 0xFFFFFFFF, 0,  0x00000001, FT },
	{ 0xDEADBEEF, 0xFFFFFFFF, FT, 0x00000000, FT },
	{ 0xDEADBEEF, 0x80000000, 0,  0x80000000, FT },
	{ 0xDEADBEEF, 0x80000000, FT, 0x7FFFFFFF, FT },
	{ 0xDEADBEEF, 0x7FFFFFFF, 0,  0x80000001, FT },
	{ 0xDEADBEEF, 0x7FFFFFFF, FT, 0x80000000, FT },
};

//// Rn + Rm -> Rn, signed overflow -> T

static const Case addv[] =
{
	{ 0x00000000, 0x00000000, FT, 0x00000000, 0 },
	{ 0x7FFFFFFF, 0x00000001, 0,  0x80000000, FT },
	{ 0x7FFFFFFF, 0x00000000, FT, 0x7FFFFFFF, 0 },
	{ 0x80000000, 0xFFFFFFFF, 0,  0x7FFFFFFF, FT },
	{ 0x80000000, 0x80000000, 0,  0x00000000, FT },
	{ 0x7FFFFFFF, 0x7FFFFFFF, 0,  0xFFFFFFFE, FT },
	{ 0xFFFFFFFF, 0x00000001, FT, 0x00000000, 0 },		//a carry is no overflow
	{ 0x7FFFFFFF, 0x80000000, FT, 0xFFFFFFFF, 0 },
	{ 0xFFFFFFFF, 0xFFFFFFFF, FT, 0xFFFFFFFE, 0 },
	{ 0x80000000, 0x00000000, FT, 0x80000000, 0 },
	{ 0x40000000, 0x40000000, 0,  0x80000000, FT },
	{ 0xC0000000, 0xC0000000, FT, 0x80000000, 0 },
};

//// Rn - Rm -> Rn, signed overflow -> T

static const Case subv[] =
{
	{ 0x00000000, 0x00000000, FT, 0x00000000, 0 },
	{ 0x80000000, 0x00000001, 0,  0x7FFFFFFF, FT },
	{ 0x7FFFFFFF, 0xFFFFFFFF, 0,  0x80000000, FT },
	{ 0x00000000, 0x80000000, 0,  0x80000000, FT },
	{ 0x80000000, 0x80000000, FT, 0x00000000, 0 },
	{ 0x7FFFFFFF, 0x7FFFFFFF, FT, 0x00000000, 0 },
	{ 0x00000000, 0x00000001, FT, 0xFFFFFFFF, 0 },		//a borrow is no overflow
	{ 0xFFFFFFFF, 0x80000000, FT, 0x7FFFFFFF, 0 },
	{ 0x7FFFFFFF, 0x80000000, 0,  0xFFFFFFFF, FT },
	{ 0x80000000, 0x7FFFFFFF, 0,  0x00000001, FT },
	{ 0x00000000, 0x7FFFFFFF, FT, 0x80000001, 0 },
	{ 0xFFFFFFFF, 0x7FFFFFFF, FT, 0x80000000, 0 },
};

//// T <- Rn <- T

static const Case rotcl[] =
{
	{ 0x00000000, 0, 0,  0x00000000, 0 },
	{ 0x00000000, 0, FT, 0x00000001, 0 },
	{ 0xFFFFFFFF, 0, 0,  0xFFFFFFFE, FT },
	{ 0xFFFFFFFF, 0, FT, 0xFFFFFFFF, FT },
	{ 0x80000000, 0, 0,  0x00000000, FT },
	{ 0x80000000, 0, FT, 0x00000001, FT },
	{ 0x7FFFFFFF, 0, 0,  0xFFFFFFFE, 0 },
	{ 0x7FFFFFFF, 0, FT, 0xFFFFFFFF, 0 },
};

//// T -> Rn -> T

static const Case rotcr[] =
{
	{ 0x00000000, 0, 0,  0x00000000, 0 },
	{ 0x00000000, 0, FT, 0x80000000, 0 },
	{ 0xFFFFFFFF, 0, 0,  0x7FFFFFFF, FT },
	{ 0xFFFFFFFF, 0, FT, 0xFFFFFFFF, FT },
	{ 0x00000001, 0, 0,  0x00000000, FT },
	{ 0x00000001, 0, FT, 0x80000000, FT },
	{ 0x80000000, 0, 0,  0x40000000, 0 },
	{ 0x80000000, 0, FT, 0xC0000000, 0 },
	{ 0x7FFFFFFF, 0, FT, 0xBFFFFFFF, FT },
	{ 0xFFFFFFFE, 0, 0,  0x7FFFFFFF, 0 },
};

//// Rn - 1 -> Rn, Rn == 0 -> T

static const Case dt[] =
{
	{ 0x00000001, 0, 0,  0x00000000, FT },
	{ 0x00000001, 0, FT, 0x00000000, FT },
	{ 0x00000000, 0, FT, 0xFFFFFFFF, 0 },
	{ 0x00000002, 0, FT, 0x00000001, 0 },
	{ 0x80000000, 0, FT, 0x7FFFFFFF, 0 },
	{ 0xFFFFFFFF, 0, FT, 0xFFFFFFFE, 0 },
};

//// Compares: Rn stays, the result -> T

static const Case cmp_eq[] =
{
	{ 0x00000000, 0x00000000, 0,  0x00000000, FT },
	{ 0xFFFFFFFF, 0xFFFFFFFF, 0,  0xFFFFFFFF, FT },
	{ 0x80000000, 0x80000000, 0,  0x80000000, FT },
	{ 0x80000000, 0x7FFFFFFF, FT, 0x80000000, 0 },
	{ 0x00000000, 0xFFFFFFFF, FT, 0x00000000, 0 },
	{ 0x00000000, 0x80000000, FT, 0x00000000, 0 },
};

//unsigned Rn >= Rm
static const Case cmp_hs[] =
{
	{ 0x00000000, 0x00000000, 0,  0x00000000, FT },
	{ 0x00000000, 0x00000001, FT, 0x00000000, 0 },
	{ 0xFFFFFFFF, 0x00000000, 0,  0xFFFFFFFF, FT },
	{ 0x00000000, 0xFFFFFFFF, FT, 0x00000000, 0 },
	{ 0x80000000, 0x7FFFFFFF, 0,  0x80000000, FT },
	{ 0x7FFFFFFF, 0x80000000, FT, 0x7FFFFFFF, 0 },
	{ 0xFFFFFFFF, 0xFFFFFFFF, 0,  0xFFFFFFFF, FT },
};

//signed Rn >= Rm
static const Case cmp_ge[] =
{
	{ 0x00000000, 0x00000000, 0,  0x00000000, FT },
	{ 0x00000000, 0x00000001, FT, 0x00000000, 0 },
	{ 0xFFFFFFFF, 0x00000000, FT, 0xFFFFFFFF, 0 },
	{ 0x00000000, 0xFFFFFFFF, 0,  0x00000000, FT },
	{ 0x80000000, 0x7FFFFFFF, FT, 0x80000000, 0 },
	{ 0x7FFFFFFF, 0x80000000, 0,  0x7FFFFFFF, FT },
	{ 0x80000000, 0x80000000, 0,  0x80000000, FT },
	{ 0x7FFFFFFF, 0x7FFFFFFF, 0,  0x7FFFFFFF, FT },
	{ 0x80000000, 0xFFFFFFFF, FT, 0x80000000, 0 },
};

//unsigned Rn > Rm
static const Case cmp_hi[] =
{
	{ 0x00000000, 0x00000000, FT, 0x00000000, 0 },
	{ 0x00000001, 0x00000000, 0,  0x00000001, FT },
	{ 0xFFFFFFFF, 0x00000000, 0,  0xFFFFFFFF, FT },
	{ 0x00000000, 0xFFFFFFFF, FT, 0x00000000, 0 },
	{ 0x80000000, 0x7FFFFFFF, 0,  0x80000000, FT },
	{ 0x7FFFFFFF, 0x80000000, FT, 0x7FFFFFFF, 0 },
	{ 0xFFFFFFFF, 0xFFFFFFFF, FT, 0xFFFFFFFF, 0 },
};

//signed Rn > Rm
static const Case cmp_gt[] =
{
	{ 0x00000000, 0x00000000, FT, 0x00000000, 0 },
	{ 0x00000001, 0x00000000, 0,  0x00000001, FT },
	{ 0xFFFFFFFF, 0x00000000, FT, 0xFFFFFFFF, 0 },
	{ 0x00000000, 0xFFFFFFFF, 0,  0x00000000, FT },
	{ 0x80000000, 0x7FFFFFFF, FT, 0x80000000, 0 },
	{ 0x7FFFFFFF, 0x80000000, 0,  0x7FFFFFFF, FT },
	{ 0x80000000, 0x80000000, FT, 0x80000000, 0 },
	{ 0x7FFFFFFF, 0x7FFFFFFF, FT, 0x7FFFFFFF, 0 },
};

//signed Rn >= 0
static const Case cmp_pz[] =
{
	{ 0x00000000, 0, 0,  0x00000000, FT },
	{ 0x00000001, 0, 0,  0x00000001, FT },
	{ 0x7FFFFFFF, 0, 0,  0x7FFFFFFF, FT },
	{ 0x80000000, 0, FT, 0x80000000, 0 },
	{ 0xFFFFFFFF, 0, FT, 0xFFFFFFFF, 0 },
};

//signed Rn > 0
static const Case cmp_pl[] =
{
	{ 0x00000000, 0, FT, 0x00000000, 0 },
	{ 0x00000001, 0, 0,  0x00000001, FT },
	{ 0x7FFFFFFF, 0, 0,  0x7FFFFFFF, FT },
	{ 0x80000000, 0, FT, 0x80000000, 0 },
	{ 0xFFFFFFFF, 0, FT, 0xFFFFFFFF, 0 },
};

//a byte of Rn is the byte of Rm at its place
static const Case cmp_str[] =
{
	{ 0x00000000, 0x00000000, 0,  0x00000000, FT },
	{ 0x00000000, 0xFFFFFFFF, FT, 0x00000000, 0 },
	{ 0x12345678, 0xFF34FFFF, 0,  0x12345678, FT },
	{ 0x12345678, 0x78563412, FT, 0x12345678, 0 },		//the same bytes at other places
	{ 0x80000000, 0x7FFFFF00, 0,  0x80000000, FT },
	{ 0x7FFFFFFF, 0x80000000, FT, 0x7FFFFFFF, 0 },
	{ 0xFFFFFFFF, 0xFF000000, 0,  0xFFFFFFFF, FT },
};

//R0 == the sign extended immediate
static const Case cmp_eq_imm[] =
{
	{ 0x00000000, 0x00, 0,  0x00000000, FT },
	{ 0xFFFFFFFF, 0xFF, 0,  0xFFFFFFFF, FT },
	{ 0x000000FF, 0xFF, FT, 0x000000FF, 0 },
	{ 0xFFFFFF80, 0x80, 0,  0xFFFFFF80, FT },
	{ 0x00000080, 0x80, FT, 0x00000080, 0 },
	{ 0x0000007F, 0x7F, 0,  0x0000007F, FT },
	{ 0x80000000, 0x00, FT, 0x80000000, 0 },
};

//// One step of the division: Rn:T is shifted left, Rm is added or subtracted as Q and M say, Q and T are the
//// step's quotient bit. M stays

static const Case div1[] =
{
	//Q = 0, M = 0: subtract
	{ 0x00000000, 0x00000000, 0,            0x00000000, FT },
	{ 0x00000000, 0x00000001, FT,           0x00000000, FT },
	{ 0x00000000, 0x00000001, 0,            0xFFFFFFFF, FQ },
	{ 0xFFFFFFFF, 0xFFFFFFFF, FT,           0x00000000, FQ },
	{ 0x80000000, 0x7FFFFFFF, FT,           0x80000002, FT },

	//Q = 1, M = 0: add
	{ 0xFFFFFFFF, 0x00000001, FQ,           0xFFFFFFFF, FQ },
	{ 0x7FFFFFFF, 0x00000001, FQ | FT,      0x00000000, FQ },
	{ 0x80000000, 0x00000000, FQ | FT,      0x00000001, FQ },

	//Q = 0, M = 1: add
	{ 0x80000000, 0xFFFFFFFF, FM,           0xFFFFFFFF, FM },
	{ 0x00000000, 0xFFFFFFFF, FM | FT,      0x00000000, FM },
	{ 0x7FFFFFFF, 0x80000000, FM,           0x7FFFFFFE, FM },
	{ 0xFFFFFFFF, 0x00000001, FM | FT,      0x00000000, FM | FQ | FT },

	//Q = 1, M = 1: subtract
	{ 0x00000000, 0xFFFFFFFF, FQ | FM,      0x00000001, FM },
	{ 0xFFFFFFFF, 0x80000000, FQ | FM | FT, 0x7FFFFFFF, FM },
	{ 0x00000000, 0x00000000, FQ | FM | FT, 0x00000001, FQ | FM | FT },
	{ 0x80000000, 0x00000001, FQ | FM,      0xFFFFFFFF, FQ | FM | FT },
};

#define GROUP(file, opcode, form, cases) { file, opcode, form, cases, sizeof(cases) / sizeof(cases[0]) }

static const Group groups[] =
{
	GROUP("0011nnnnmmmm1110_sz0_pr0", 0x334E, FORM_RM_RN, addc),		//addc r4,r3
	GROUP("0011nnnnmmmm1110_sz0_pr0", 0x333E, FORM_RN_RN, addc_same),	//addc r3,r3
	GROUP("0011nnnnmmmm1010_sz0_pr0", 0x334A, FORM_RM_RN, subc),		//subc r4,r3
	GROUP("0011nnnnmmmm1010_sz0_pr0", 0x333A, FORM_RN_RN, subc_same),	//subc r3,r3
	GROUP("0110nnnnmmmm1010_sz0_pr0", 0x634A, FORM_RM_RN, negc),		//negc r4,r3
	GROUP("0011nnnnmmmm1111_sz0_pr0", 0x334F, FORM_RM_RN, addv),		//addv r4,r3
	GROUP("0011nnnnmmmm1011_sz0_pr0", 0x334B, FORM_RM_RN, subv),		//subv r4,r3
	GROUP("0100nnnn00100100_sz0_pr0", 0x4324, FORM_RN, rotcl),			//rotcl r3
	GROUP("0100nnnn00100101_sz0_pr0", 0x4325, FORM_RN, rotcr),			//rotcr r3
	GROUP("0100nnnn00010000_sz0_pr0", 0x4310, FORM_RN, dt),				//dt r3
	GROUP("0011nnnnmmmm0000_sz0_pr0", 0x3340, FORM_RM_RN, cmp_eq),		//cmp/eq r4,r3
	GROUP("0011nnnnmmmm0010_sz0_pr0", 0x3342, FORM_RM_RN, cmp_hs),		//cmp/hs r4,r3
	GROUP("0011nnnnmmmm0011_sz0_pr0", 0x3343, FORM_RM_RN, cmp_ge),		//cmp/ge r4,r3
	GROUP("0011nnnnmmmm0110_sz0_pr0", 0x3346, FORM_RM_RN, cmp_hi),		//cmp/hi r4,r3
	GROUP("0011nnnnmmmm0111_sz0_pr0", 0x3347, FORM_RM_RN, cmp_gt),		//cmp/gt r4,r3
	GROUP("0100nnnn00010001_sz0_pr0", 0x4311, FORM_RN, cmp_pz),			//cmp/pz r3
	GROUP("0100nnnn00010101_sz0_pr0", 0x4315, FORM_RN, cmp_pl),			//cmp/pl r3
	GROUP("0010nnnnmmmm1100_sz0_pr0", 0x234C, FORM_RM_RN, cmp_str),		//cmp/str r4,r3
	GROUP("10001000iiiiiiii_sz0_pr0", 0x8800, FORM_IMM_R0, cmp_eq_imm),	//cmp/eq #imm,r0
	GROUP("0011nnnnmmmm0100_sz0_pr0", 0x3344, FORM_RM_RN, div1),		//div1 r4,r3
};

//// Delayed conditional branches: T is tested in front of the delay slot, what the delay slot leaves in T
//// does not decide. The branch is the second instruction, at PC_BASE + 2

struct Branch
{
	const char* file;
	u16 opcode;			//with the displacement
	u16 slot;			//the delay slot: OP_CLRT or OP_SETT
	u32 flags;			//FT going in
	u32 next;			//where the instruction behind the delay slot is run from
	u32 flags_after;
};

#define BRANCH_TARGET (PC_BASE + 2 + 4 + 0x10 * 2)		//displacement 0x10: taken
#define BRANCH_BACK (PC_BASE + 2 + 4 - 0x10 * 2)		//displacement 0xF0: taken, backwards
#define BRANCH_NONE (PC_BASE + 6)						//not taken

static const Branch branches[] =
{
	//bt/s: taken with T set
	//file                        opcode  slot     in  next           after
	{ "10001101dddddddd_sz0_pr0", 0x8D10, OP_CLRT, FT, BRANCH_TARGET, 0 },
	{ "10001101dddddddd_sz0_pr0", 0x8D10, OP_SETT, 0,  BRANCH_NONE,   FT },
	{ "10001101dddddddd_sz0_pr0", 0x8D10, OP_SETT, FT, BRANCH_TARGET, FT },
	{ "10001101dddddddd_sz0_pr0", 0x8D10, OP_CLRT, 0,  BRANCH_NONE,   0 },
	{ "10001101dddddddd_sz0_pr0", 0x8DF0, OP_CLRT, FT, BRANCH_BACK,   0 },
	{ "10001101dddddddd_sz0_pr0", 0x8DF0, OP_SETT, 0,  BRANCH_NONE,   FT },

	//bf/s: taken with T clear
	{ "10001111dddddddd_sz0_pr0", 0x8F10, OP_SETT, 0,  BRANCH_TARGET, FT },
	{ "10001111dddddddd_sz0_pr0", 0x8F10, OP_CLRT, FT, BRANCH_NONE,   0 },
	{ "10001111dddddddd_sz0_pr0", 0x8F10, OP_CLRT, 0,  BRANCH_TARGET, 0 },
	{ "10001111dddddddd_sz0_pr0", 0x8F10, OP_SETT, FT, BRANCH_NONE,   FT },
	{ "10001111dddddddd_sz0_pr0", 0x8FF0, OP_SETT, 0,  BRANCH_BACK,   FT },
	{ "10001111dddddddd_sz0_pr0", 0x8FF0, OP_CLRT, FT, BRANCH_NONE,   0 },
};

//// Delayed jumps and calls: where it goes is taken, and PR is written, in front of the delay slot (PR = PC + 4,
//// PC = ... and then Delay_Slot() in the manual's descriptions). A delay slot that loads the register the
//// jump goes through, or PR, does not move it, one that stores PR behind a call stores the return address, and
//// one that loads PR behind a call leaves what it loads.
//// The instruction is at PC_BASE + 2 and goes through r3, the delay slot loads from r4 or stores to r5

struct Jump
{
	const char* file;
	u16 opcode;
	u16 slot;			//the delay slot
	u32 r3;				//r3 going in
	u32 next;			//where the instruction behind the delay slot is run from
	u32 r3_after;
	u32 r5_after;
	u32 pr_after;
};

#define JUMP_R3 0x8C020000		//jmp and jsr go there
#define JUMP_OFFSET 0x00000100	//braf and bsrf go that far
#define JUMP_BACK 0xFFFFFF00
#define JUMP_R4 0x8C0E0000		//what the delay slot loads
#define JUMP_R5 0xA0A50A0F		//r5 of base_state()
#define JUMP_RETURN (PC_BASE + 6)	//PR behind a call

static const Jump jumps[] =
{
	//rts returns to the PR it is entered with
	//file                        opcode  slot          r3 in        next                       r3 after     r5 after     PR after
	{ "0000000000001011_sz0_pr0", 0x000B, OP_LDS_R4_PR, JUMP_R3,     PR_BASE,                   JUMP_R3,     JUMP_R5,     JUMP_R4 },

	//jmp @r3, braf r3 and bsrf r3 go by the r3 they are entered with
	{ "0100mmmm00101011_sz0_pr0", 0x432B, OP_MOV_R4_R3, JUMP_R3,     JUMP_R3,                   JUMP_R4,     JUMP_R5,     PR_BASE },
	{ "0000mmmm00100011_sz0_pr0", 0x0323, OP_MOV_R4_R3, JUMP_OFFSET, JUMP_RETURN + JUMP_OFFSET, JUMP_R4,     JUMP_R5,     PR_BASE },
	{ "0000mmmm00100011_sz0_pr0", 0x0323, OP_MOV_R4_R3, JUMP_BACK,   JUMP_RETURN + JUMP_BACK,   JUMP_R4,     JUMP_R5,     PR_BASE },
	{ "0000mmmm00000011_sz0_pr0", 0x0303, OP_MOV_R4_R3, JUMP_OFFSET, JUMP_RETURN + JUMP_OFFSET, JUMP_R4,     JUMP_R5,     JUMP_RETURN },
	{ "0000mmmm00000011_sz0_pr0", 0x0303, OP_MOV_R4_R3, JUMP_BACK,   JUMP_RETURN + JUMP_BACK,   JUMP_R4,     JUMP_R5,     JUMP_RETURN },

	//jsr @r3, bsr and bsrf r3: the delay slot sees the PR of the call
	{ "0100mmmm00001011_sz0_pr0", 0x430B, OP_STS_PR_R5, JUMP_R3,     JUMP_R3,                   JUMP_R3,     JUMP_RETURN, JUMP_RETURN },
	{ "1011dddddddddddd_sz0_pr0", 0xB010, OP_STS_PR_R5, JUMP_R3,     JUMP_RETURN + 0x10 * 2,    JUMP_R3,     JUMP_RETURN, JUMP_RETURN },
	{ "0000mmmm00000011_sz0_pr0", 0x0303, OP_STS_PR_R5, JUMP_OFFSET, JUMP_RETURN + JUMP_OFFSET, JUMP_OFFSET, JUMP_RETURN, JUMP_RETURN },

	//and a delay slot that loads PR is the later write of the two
	{ "0100mmmm00001011_sz0_pr0", 0x430B, OP_LDS_R4_PR, JUMP_R3,     JUMP_R3,                   JUMP_R3,     JUMP_R5,     JUMP_R4 },
	{ "1011dddddddddddd_sz0_pr0", 0xB010, OP_LDS_R4_PR, JUMP_R3,     JUMP_RETURN + 0x10 * 2,    JUMP_R3,     JUMP_R5,     JUMP_R4 },
	{ "0000mmmm00000011_sz0_pr0", 0x0303, OP_LDS_R4_PR, JUMP_OFFSET, JUMP_RETURN + JUMP_OFFSET, JUMP_OFFSET, JUMP_R5,     JUMP_R4 },
};

//// RTE: SR = SSR and PC = SPC, then the delay slot. The delay slot runs with the SR that is restored: S, T, M, Q,
//// FD, BL and RB are used after modification, only its instruction fetch uses the MD of before (SH7091 hardware
//// manual 7.1, Delayed Branches; "SR = SSR; PC = SPC; Delay_Slot(temp + 2);" in the programming manual).
//// A console's delay slot reads R0 of the new bank and the new T (shrike4-rtl,
//// tools/hw/exceptions-intc/results_run1.txt, rte_slot_rb and rte_slot_t). The instruction is at PC_BASE + 2, its
//// delay slot copies R0 or T into r8

struct Rte
{
	u32 sr;				//SR going in
	u32 ssr;
	u32 spc;
	u16 slot;			//OP_MOV_R0_R8 or OP_MOVT_R8
	u32 r8_after;
};

#define SR_RB 0x20000000

#define RTE_TARGET 0x8C0F2000
#define RTE_USER_TARGET 0x0C0F2000	//user mode cannot fetch from P1
#define RTE_R0 0xA0A00000			//R0 of the bank RTE is entered with (base_state())
#define RTE_R0_BANK 0xB0B00000		//R0 of the other bank

static const Rte rtes[] =
{
	//R0 in the delay slot is R0 of the bank SSR selects
	//SR in            SSR         SPC              slot          r8 after
	{ SR_BASE,         0x700000F0, RTE_TARGET,      OP_MOV_R0_R8, RTE_R0_BANK },	//bank 0 -> 1
	{ SR_BASE | SR_RB, 0x500000F0, RTE_TARGET,      OP_MOV_R0_R8, RTE_R0_BANK },	//bank 1 -> 0
	{ SR_BASE | SR_RB, 0x700000F0, RTE_TARGET,      OP_MOV_R0_R8, RTE_R0 },			//stays
	{ SR_BASE,         0x500000F0, RTE_TARGET,      OP_MOV_R0_R8, RTE_R0 },
	{ SR_BASE | SR_RB, 0x000000F0, RTE_USER_TARGET, OP_MOV_R0_R8, RTE_R0_BANK },	//to user mode: bank 0

	//T in the delay slot is the T of SSR
	{ SR_BASE,         0x500000F1, RTE_TARGET,      OP_MOVT_R8,   1 },
	{ SR_BASE | FT,    0x500000F0, RTE_TARGET,      OP_MOVT_R8,   0 },
	{ SR_BASE,         0x700000F1, RTE_TARGET,      OP_MOVT_R8,   1 },				//with the bank
	{ SR_BASE | SR_RB | FT, 0x500000F0, RTE_TARGET, OP_MOVT_R8,   0 },
};

//// TRAPA #imm with SR.BL = 0: SSR = SR, SPC = the address behind TRAPA, SGR = R15, SR.MD = RB = BL = 1 and the
//// rest of SR as it is, PC = VBR + H'100 (TRA and EXPEVT are not part of a vector). A console does that
//// (shrike4-rtl, tools/hw/exceptions-intc/results_run1.txt, trapa and fd_*: SPC = TRAPA + 2, SGR = R15, the entry
//// SR is SSR with MD, RB and BL set and FD kept).
//// With SR.BL = 1 a general exception is a manual reset of the CPU: SR = MD RB BL set, FD clear, IMASK = 1111,
//// VBR = 0, FPSCR = H'00040001, PC = H'A0000000 (SH7091 hardware manual 5.5.3, 5.6.1 (2) and table 2.1). No
//// console was measured for it. What the manual leaves undefined after a reset (the other registers, and M, Q, S
//// and T of SR, which a vector has clear going in) is expected as it was, in the bank that SR.RB = 1 selects.
//// TRAPA is at PC_BASE + 2, the two instructions behind it are run from where it goes

struct Trapa
{
	u32 sr;				//SR going in
	u32 fpscr;			//FPSCR going in
	bool reset;			//SR.BL going in
};

#define OP_TRAPA_5A 0xC35A

#define SR_AFTER_ENTRY 0x70000000	//MD, RB and BL are set, the rest stays
#define SR_AFTER_RESET 0x700000F0
#define RESET_PC 0xA0000000

static const Trapa trapas[] =
{
	//SR in      FPSCR in    reset
	{ 0x400000F0, FPSCR_BASE, false },		//bank 0: the handler is entered on bank 1
	{ 0x600000F1, FPSCR_BASE, false },		//bank 1 and T: T stays
	{ 0x400080F0, FPSCR_BASE, false },		//FD stays
	{ 0x40000000, FPSCR_BASE, false },		//IMASK stays
	{ 0x500000F0, FPSCR_BASE, true },		//bank 0: the reset selects bank 1
	{ 0x70008000, FPSCR_BASE, true },		//FD and IMASK are the reset's
	{ 0x500000F0, 0x00140000, true },		//FPSCR is the reset's
};

//// LDS Rm,FPSCR and LDS.L @Rm+,FPSCR: FPSCR has 22 bits (FR SZ PR DN, cause, enable, flag, RM), the 10 above them
//// read 0 and are not written. A console reads 003FFFFF back after it loaded FFFFFFFF (shrike4-rtl,
//// tools/hw/exceptions-intc/results_run1.txt, "A masks"). FR selects the FP bank: loading another FR than the
//// one going in swaps FR0-FR15 and XF0-XF15. The instruction is at PC_BASE + 2 and loads from r3, or from the
//// longword r3 points at

struct FpscrLoad
{
	const char* file;
	u16 opcode;
	u32 value;			//what is loaded
	u32 fpscr_after;
};

#define OP_LDS_R3_FPSCR 0x436A
#define OP_LDSL_R3_FPSCR 0x4366

#define FPSCR_FR 0x00200000
#define FPSCR_ADDR 0x8C0F7000	//r3 of lds.l

static const FpscrLoad fpscr_loads[] =
{
	//file                        opcode            loaded      FPSCR after
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0xFFFFFFFF, 0x003FFFFF },
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0xFFC00000, 0x00000000 },		//only bits that are not there
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0x00400000, 0x00000000 },		//the lowest of them
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0x80040001, 0x00040001 },
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0x00140001, 0x00140001 },		//nothing to drop
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0x003FFFFF, 0x003FFFFF },
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0xFFC40002, 0x00040002 },		//RM = 10 and 11, reserved, are stored
	{ "0100mmmm01101010_sz0_pr0", OP_LDS_R3_FPSCR,  0xFFC40003, 0x00040003 },
	{ "0100mmmm01100110_sz0_pr0", OP_LDSL_R3_FPSCR, 0xFFFFFFFF, 0x003FFFFF },
	{ "0100mmmm01100110_sz0_pr0", OP_LDSL_R3_FPSCR, 0xFFC00000, 0x00000000 },
	{ "0100mmmm01100110_sz0_pr0", OP_LDSL_R3_FPSCR, 0x80040001, 0x00040001 },
	{ "0100mmmm01100110_sz0_pr0", OP_LDSL_R3_FPSCR, 0x7FC40002, 0x00040002 },
	{ "0100mmmm01100110_sz0_pr0", OP_LDSL_R3_FPSCR, 0x00440003, 0x00040003 },
};

//// FTRC FRm,FPUL and FTRC DRm,FPUL: the number cut to an integer, toward zero. 2^31 and everything above it,
//// +inf too, gives H'7FFFFFFF, everything below -2^31 and -inf H'80000000, and so does every NaN, of either sign
//// (SH7091 programming manual 10.46: the ranges are told by the bits, a single above H'4EFFFFFF and a double
//// with a high word above H'41DFFFFF are out of range). A console gives 7FFFFFFF for +max and +inf, 80000000
//// for -inf and for quiet and signalling NaNs, and -3 for -3.0 (shrike4-rtl, tools/hw/fpdn, FTRC and FTRC.D);
//// the edges of the range and a NaN with the sign bit set in single precision are the manual's. The single is in
//// FR3, the double in DR2 (FR2 the high word, FR3 the low one)

struct Ftrc
{
	bool pr;			//FTRC DR2,FPUL with FPSCR.PR = 1
	u32 high;			//the single, or the high word of the double
	u32 low;
	u32 fpul_after;
};

#define OP_FTRC_FR3 0xF33D
#define OP_FTRC_DR2 0xF23D

#define FPSCR_PR 0x00080000

static const Ftrc ftrcs[] =
{
	//double  high        low         FPUL after
	{ false,  0x3FC00000, 0,          0x00000001 },		//1.5
	{ false,  0xBFC00000, 0,          0xFFFFFFFF },		//-1.5
	{ false,  0xC0400000, 0,          0xFFFFFFFD },		//-3
	{ false,  0x4EFFFFFF, 0,          0x7FFFFF80 },		//the largest single below 2^31
	{ false,  0x4F000000, 0,          0x7FFFFFFF },		//2^31
	{ false,  0x7F7FFFFF, 0,          0x7FFFFFFF },		//the largest number
	{ false,  0x7F800000, 0,          0x7FFFFFFF },		//+inf
	{ false,  0xCF000000, 0,          0x80000000 },		//-2^31
	{ false,  0xCF000001, 0,          0x80000000 },		//the next one below it
	{ false,  0xFF800000, 0,          0x80000000 },		//-inf
	{ false,  0x7F800001, 0,          0x80000000 },		//NaNs
	{ false,  0x7FBFFFFF, 0,          0x80000000 },
	{ false,  0x7FC00000, 0,          0x80000000 },
	{ false,  0xFFC00000, 0,          0x80000000 },
	{ true,   0x3FF80000, 0x00000000, 0x00000001 },		//1.5
	{ true,   0xC0080000, 0x00000000, 0xFFFFFFFD },		//-3
	{ true,   0x41DFFFFF, 0xFFFFFFFF, 0x7FFFFFFF },		//the largest double below 2^31
	{ true,   0x41E00000, 0x00000000, 0x7FFFFFFF },		//2^31
	{ true,   0x7FEFFFFF, 0xFFFFFFFF, 0x7FFFFFFF },		//the largest number
	{ true,   0x7FF00000, 0x00000000, 0x7FFFFFFF },		//+inf
	{ true,   0xC1DFFFFF, 0xFFE00000, 0x80000001 },		//-(2^31 - 0.5)
	{ true,   0xC1E00000, 0x00000000, 0x80000000 },		//-2^31
	{ true,   0xC1E00001, 0x00000000, 0x80000000 },		//a high word below the range
	{ true,   0xFFF00000, 0x00000000, 0x80000000 },		//-inf
	{ true,   0x7FF00000, 0x00000001, 0x80000000 },		//NaNs
	{ true,   0x7FF80000, 0x00000000, 0x80000000 },
	{ true,   0xFFF00000, 0x80000000, 0x80000000 },
};

//// FMAC FR0,FRm,FRn: FR0 * FRm + FRn, the exact sum rounded once. A console's FMAC is that in every record
//// (shrike4-rtl, tools/hw/fpdn: the 12,288 FMAC records of fpdn_p2.bin have results in and next to the
//// denormal range and cancellations, and a product that is rounded before it is added is not the console's
//// in any record in which it makes a difference). The operands here are ones for which rounding the product
//// first, or rounding the sum to double precision first, gives another single:
////   (8 + 2^-20) * (8 - 2^-20) = 64 - 2^-40. Around 2^30 the singles are 128 apart. Added to 2^30 + 128 the
////     sum is 2^-40 below the middle of two of them, and the lower one is nearest. Added to -(2^30 + 384) it
////     is 2^-40 beyond the middle of -(2^30 + 256) and -(2^30 + 384) in size, and the latter is nearest. A
////     product rounded to single precision (64), or a sum rounded to double precision, is the middle itself,
////     which goes to the even one of the two: the other one both times
////   (1 + 2^-23) * (1 + 2^-23) = 1 + 2^-22 + 2^-46. Added to -(1 + 2^-22) it leaves 2^-46, a product that was
////     rounded leaves 0
//// Rounding to zero cuts the exact sum. FR0 and FR1 are multiplied, FR2 is added and gets the result

struct Fmac
{
	u32 fpscr;
	u32 fr0;
	u32 fr1;
	u32 fr2;
	u32 fr2_after;
};

#define OP_FMAC_FR0_FR1_FR2 0xF21E

#define FPSCR_RN 0x00040000		//DN = 1, round to nearest
#define FPSCR_RZ 0x00040001		//DN = 1, round to zero

static const Fmac fmacs[] =
{
	//FPSCR     FR0         FR1         FR2         FR2 after
	{ FPSCR_RN, 0x40000000, 0x40400000, 0x3F800000, 0x40E00000 },		//2 * 3 + 1
	{ FPSCR_RN, 0x41000001, 0x40FFFFFE, 0x4E800001, 0x4E800001 },		//2^30 + 192 - 2^-40: 2^30 + 128
	{ FPSCR_RN, 0xC1000001, 0x40FFFFFE, 0xCE800001, 0xCE800001 },		//the same with the other signs
	{ FPSCR_RN, 0x41000001, 0x40FFFFFE, 0xCE800003, 0xCE800003 },		//-(2^30 + 320 + 2^-40): -(2^30 + 384)
	{ FPSCR_RN, 0x3F800001, 0x3F800001, 0xBF800002, 0x28800000 },		//2^-46
	{ FPSCR_RZ, 0x3F800001, 0x3F800001, 0xBF800002, 0x28800000 },
	{ FPSCR_RZ, 0x41000001, 0x40FFFFFE, 0x4E800001, 0x4E800001 },		//cut to 2^30 + 128
	{ FPSCR_RZ, 0x41000001, 0x40FFFFFE, 0xCE800003, 0xCE800002 },		//cut to -(2^30 + 256)
};

//Every register has a value that no other one has, so an instruction that touches the wrong one shows
static void base_state(SsState& s)
{
	for (u32 i = 0; i < 16; i++)
	{
		s.R[i] = 0xA0A00000 + i * 0x00010203;
		s.FP0[i] = 0x3F800000 + i;		//1.0 and its neighbours
		s.FP1[i] = 0x40000000 + i;		//2.0 and its neighbours
	}

	for (u32 i = 0; i < 8; i++)
		s.R_[i] = 0xB0B00000 + i * 0x00030201;

	s.PC = PC_BASE;
	s.GBR = 0x8C0F1000;
	s.SR = SR_BASE;
	s.SSR = 0x40000001;
	s.SPC = 0x8C0F2000;
	s.VBR = 0x8C0F3000;
	s.SGR = 0x8C0F4000;
	s.DBR = 0x8C0F5000;
	s.MACL = 0x11112222;
	s.MACH = 0x33334444;
	s.PR = PR_BASE;
	s.FPSCR = FPSCR_BASE;
	s.FPUL = 0x55556666;
}

//The framing, the state going in and nops
static void begin_vector(SsTest& t)
{
	memset(&t, 0, sizeof(t));

	t.size = sizeof(SsTest);
	t.initial.size = sizeof(SsStateBlock);
	t.initial.tag = 1;
	t.final_.size = sizeof(SsStateBlock);
	t.final_.tag = 2;
	t.cycles.size = sizeof(SsCyclesBlock);
	t.cycles.tag = 3;
	t.cycles.count = 4;
	t.opcodes.size = sizeof(SsOpcodesBlock);
	t.opcodes.tag = 4;

	base_state(t.initial.s);

	for (u32 i = 0; i < 5; i++)
		t.opcodes.op[i] = OP_NOP;
}

//The fetches: three instructions in a row and the 4th from last, PC ends behind that one
static void end_vector(SsTest& t, u32 last)
{
	t.final_.s.PC = last + 2;

	for (u32 i = 0; i < 4; i++)
	{
		u32 addr = i < 3 ? PC_BASE + i * 2 : last;
		u32 offset = addr - PC_BASE;

		t.cycles.c[i].actions = SS_ACTION_FETCH;
		t.cycles.c[i].fetch_addr = addr;
		t.cycles.c[i].fetch_val = t.opcodes.op[(offset & 1) == 0 && offset < 8 ? offset / 2 : 4];
	}
}

static void make_vector(const Group& g, const Case& c, SsTest& t)
{
	begin_vector(t);

	u32 n = g.form == FORM_IMM_R0 ? 0 : 3;
	u32 opcode = g.opcode;

	t.initial.s.R[n] = c.rn;
	t.initial.s.SR = SR_BASE | c.flags;

	if (g.form == FORM_RM_RN)
		t.initial.s.R[4] = c.rm;
	else if (g.form == FORM_IMM_R0)
		opcode |= c.rm & 0xFF;

	t.final_.s = t.initial.s;

	t.final_.s.R[n] = c.rn_after;
	t.final_.s.SR = SR_BASE | c.flags_after;

	t.opcodes.op[SS_TEST_INSTRUCTION] = opcode;

	end_vector(t, PC_BASE + 6);
}

static void make_branch_vector(const Branch& b, SsTest& t)
{
	begin_vector(t);

	t.initial.s.SR = SR_BASE | b.flags;

	t.final_.s = t.initial.s;

	t.final_.s.SR = SR_BASE | b.flags_after;

	t.opcodes.op[SS_TEST_INSTRUCTION] = b.opcode;
	t.opcodes.op[SS_TEST_INSTRUCTION + 1] = b.slot;

	end_vector(t, b.next);
}

static void make_jump_vector(const Jump& j, SsTest& t)
{
	begin_vector(t);

	t.initial.s.R[3] = j.r3;
	t.initial.s.R[4] = JUMP_R4;

	t.final_.s = t.initial.s;

	t.final_.s.R[3] = j.r3_after;
	t.final_.s.R[5] = j.r5_after;
	t.final_.s.PR = j.pr_after;

	t.opcodes.op[SS_TEST_INSTRUCTION] = j.opcode;
	t.opcodes.op[SS_TEST_INSTRUCTION + 1] = j.slot;

	end_vector(t, j.next);
}

//R0-R7 are banked by SR.RB in privileged mode, user mode has bank 0
static bool sr_bank(u32 sr)
{
	return (sr & 0x60000000) == 0x60000000;
}

static void make_rte_vector(const Rte& r, SsTest& t)
{
	begin_vector(t);

	t.initial.s.SR = r.sr;
	t.initial.s.SSR = r.ssr;
	t.initial.s.SPC = r.spc;

	t.final_.s = t.initial.s;

	t.final_.s.SR = r.ssr;

	//R is the bank that is active, R_ the other one
	if (sr_bank(r.sr) != sr_bank(r.ssr))
	{
		for (u32 i = 0; i < 8; i++)
		{
			t.final_.s.R[i] = t.initial.s.R_[i];
			t.final_.s.R_[i] = t.initial.s.R[i];
		}
	}

	t.final_.s.R[8] = r.r8_after;

	t.opcodes.op[SS_TEST_INSTRUCTION] = 0x002B;
	t.opcodes.op[SS_TEST_INSTRUCTION + 1] = r.slot;

	end_vector(t, r.spc);
}

static void make_trapa_vector(const Trapa& c, SsTest& t)
{
	begin_vector(t);

	t.initial.s.SR = c.sr;
	t.initial.s.FPSCR = c.fpscr;

	t.final_.s = t.initial.s;

	u32 next;

	if (c.reset)
	{
		t.final_.s.SR = SR_AFTER_RESET;
		t.final_.s.VBR = 0;
		t.final_.s.FPSCR = FPSCR_BASE;

		next = RESET_PC;
	}
	else
	{
		t.final_.s.SR = c.sr | SR_AFTER_ENTRY;
		t.final_.s.SSR = c.sr;
		t.final_.s.SPC = PC_BASE + 4;
		t.final_.s.SGR = t.initial.s.R[15];

		next = t.initial.s.VBR + 0x100;
	}

	//R is the bank that is active, R_ the other one
	if (sr_bank(c.sr) != sr_bank(t.final_.s.SR))
	{
		for (u32 i = 0; i < 8; i++)
		{
			t.final_.s.R[i] = t.initial.s.R_[i];
			t.final_.s.R_[i] = t.initial.s.R[i];
		}
	}

	t.opcodes.op[SS_TEST_INSTRUCTION] = OP_TRAPA_5A;

	//nop and TRAPA, then two instructions from where it goes
	end_vector(t, next + 2);

	t.cycles.c[2].fetch_addr = next;
	t.cycles.c[2].fetch_val = t.opcodes.op[4];
}

static void make_fpscr_vector(const FpscrLoad& l, SsTest& t)
{
	begin_vector(t);

	bool from_memory = l.opcode == OP_LDSL_R3_FPSCR;

	t.initial.s.R[3] = from_memory ? FPSCR_ADDR : l.value;

	t.final_.s = t.initial.s;

	t.final_.s.FPSCR = l.fpscr_after;

	if (from_memory)
		t.final_.s.R[3] = FPSCR_ADDR + 4;

	if ((l.fpscr_after ^ FPSCR_BASE) & FPSCR_FR)
	{
		for (u32 i = 0; i < 16; i++)
		{
			t.final_.s.FP0[i] = t.initial.s.FP1[i];
			t.final_.s.FP1[i] = t.initial.s.FP0[i];
		}
	}

	t.opcodes.op[SS_TEST_INSTRUCTION] = l.opcode;

	end_vector(t, PC_BASE + 6);

	if (from_memory)
	{
		SsCycle& c = t.cycles.c[SS_TEST_INSTRUCTION];

		c.actions |= SS_ACTION_READ;
		c.read_addr = FPSCR_ADDR;
		c.read_val = l.value;
	}
}

static void make_ftrc_vector(const Ftrc& f, SsTest& t)
{
	begin_vector(t);

	if (f.pr)
	{
		t.initial.s.FPSCR = FPSCR_BASE | FPSCR_PR;
		t.initial.s.FP0[2] = f.high;
		t.initial.s.FP0[3] = f.low;
	}
	else
	{
		t.initial.s.FP0[3] = f.high;
	}

	t.final_.s = t.initial.s;

	t.final_.s.FPUL = f.fpul_after;

	t.opcodes.op[SS_TEST_INSTRUCTION] = f.pr ? OP_FTRC_DR2 : OP_FTRC_FR3;

	end_vector(t, PC_BASE + 6);
}

static void make_fmac_vector(const Fmac& f, SsTest& t)
{
	begin_vector(t);

	t.initial.s.FPSCR = f.fpscr;
	t.initial.s.FP0[0] = f.fr0;
	t.initial.s.FP0[1] = f.fr1;
	t.initial.s.FP0[2] = f.fr2;

	t.final_.s = t.initial.s;

	t.final_.s.FP0[2] = f.fr2_after;

	t.opcodes.op[SS_TEST_INSTRUCTION] = OP_FMAC_FR0_FR1_FR2;

	end_vector(t, PC_BASE + 6);
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		fprintf(stderr, "sh4_directed_gen <dir>: writes the directed SH4 vectors as *" SS_FILE_SUFFIX " files into <dir>\n");
		return 2;
	}

	std::string dir = argv[1];
	std::map<std::string, std::vector<SsTest> > files;
	u32 vectors = 0;

	for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++)
	{
		for (u32 c = 0; c < groups[g].count; c++)
		{
			SsTest t;

			make_vector(groups[g], groups[g].cases[c], t);
			files[groups[g].file].push_back(t);
			vectors++;
		}
	}

	for (size_t b = 0; b < sizeof(branches) / sizeof(branches[0]); b++)
	{
		SsTest t;

		make_branch_vector(branches[b], t);
		files[branches[b].file].push_back(t);
		vectors++;
	}

	for (size_t j = 0; j < sizeof(jumps) / sizeof(jumps[0]); j++)
	{
		SsTest t;

		make_jump_vector(jumps[j], t);
		files[jumps[j].file].push_back(t);
		vectors++;
	}

	for (size_t r = 0; r < sizeof(rtes) / sizeof(rtes[0]); r++)
	{
		SsTest t;

		make_rte_vector(rtes[r], t);
		files["0000000000101011_sz0_pr0"].push_back(t);
		vectors++;
	}

	for (size_t c = 0; c < sizeof(trapas) / sizeof(trapas[0]); c++)
	{
		SsTest t;

		make_trapa_vector(trapas[c], t);
		files["11000011iiiiiiii_sz0_pr0"].push_back(t);
		vectors++;
	}

	for (size_t l = 0; l < sizeof(fpscr_loads) / sizeof(fpscr_loads[0]); l++)
	{
		SsTest t;

		make_fpscr_vector(fpscr_loads[l], t);
		files[fpscr_loads[l].file].push_back(t);
		vectors++;
	}

	for (size_t f = 0; f < sizeof(ftrcs) / sizeof(ftrcs[0]); f++)
	{
		SsTest t;

		make_ftrc_vector(ftrcs[f], t);
		files[ftrcs[f].pr ? "1111mmm000111101_sz0_pr1" : "1111mmmm00111101_sz0_pr0"].push_back(t);
		vectors++;
	}

	for (size_t f = 0; f < sizeof(fmacs) / sizeof(fmacs[0]); f++)
	{
		SsTest t;

		make_fmac_vector(fmacs[f], t);
		files["1111nnnnmmmm1110_sz0_pr0"].push_back(t);
		vectors++;
	}

	//what an earlier table left behind
	std::vector<std::string> present;

	if (!ss_list_files(dir, present))
	{
		fprintf(stderr, "cannot read the directory %s\n", dir.c_str());
		return 2;
	}

	for (size_t i = 0; i < present.size(); i++)
	{
		if (files.find(present[i]) == files.end())
			remove((dir + "/" + present[i] + SS_FILE_SUFFIX).c_str());
	}

	for (std::map<std::string, std::vector<SsTest> >::const_iterator it = files.begin(); it != files.end(); ++it)
	{
		std::string path = dir + "/" + it->first + SS_FILE_SUFFIX;
		FILE* f = fopen(path.c_str(), "wb");

		if (!f || fwrite(&it->second[0], sizeof(SsTest), it->second.size(), f) != it->second.size() || fclose(f) != 0)
		{
			fprintf(stderr, "cannot write %s\n", path.c_str());
			return 2;
		}
	}

	printf("%u directed vectors in %u files in %s\n", vectors, (u32)files.size(), dir.c_str());

	return 0;
}
