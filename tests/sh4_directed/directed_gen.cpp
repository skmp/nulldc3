/*
	Directed SH4 vectors: the carry, borrow, overflow and compare instructions on operands at the edges (0,
	0xFFFFFFFF, 0x7FFFFFFF, 0x80000000) with T clear and set, and the delayed conditional branches with a delay
	slot that changes T. The single-step vectors are random and have none of these: no ADDC whose sum is
	0xFFFFFFFF with T set, for one, and add r1,r1 in every delay slot, which leaves T as it is.

	sh4_directed_gen <dir> writes vector files in the format of the single-step vectors and under their file
	names (ss_format.h), so the suites run them as they run those and the policy table applies as it is. A vector
	is nop / the instruction / nop / nop on a state that has a value of its own in every register, and nop /
	the branch / clrt or sett / nop for a branch. Nothing but Rn, T, Q and PC may change.

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

#define OP_NOP 0x0009
#define OP_CLRT 0x0008
#define OP_SETT 0x0018

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
	s.PR = 0x8C0F6000;
	s.FPSCR = 0x00040001;
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
