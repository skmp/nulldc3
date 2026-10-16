/*
	FIPR, FTRV, FSCA and FSRRA as cases of the FPU tests: what the hardware leaves, from shrike4-rtl's model, and
	generated operands.

	The model (shrike4-rtl model/sh4_fpu_approx.h) is compiled in from the checkout. It reproduces every capture
	of the four instructions that was taken on a console: values, FPSCR and exceptions. How it wants its
	arguments, which vector_reference() is the one place for:
	  FIPR FVm,FVn     sh4_fipr_ex(fvm, fvn, ..): the two are not interchangeable, the hardware recodes FVn for
	                   its multipliers. The result is FR[n+3]
	  FTRV XMTRX,FVn   sh4_ftrv_ex(xmtrx, fv, ..): xmtrx[0..15] is XF0..XF15, column major, result i is the sum
	                   over j of XF[4j+i] * FV[j]. The matrix element is the recoded operand
	  FSCA FPUL,DRn    sh4_fsca_ex(fpul, ..): the angle is FPUL[15:0], sine to FR[n], cosine to FR[n+1]
	  FSRRA FRn        sh4_fsrra_ex(x, ..)
	All of them read FPSCR (RM, the enables, DN, PR), rewrite the cause field, set flags and say whether the
	instruction raises the FPU exception, in which case no register is written.
*/
#pragma once
#include "fpu_suite.h"

enum VectorInsn
{
	VI_FIPR,
	VI_FTRV,
	VI_FSCA,
	VI_FSRRA,

	VI_COUNT
};

extern const char* const vector_insn_name[VI_COUNT];

//The opcodes. m and n are the numbers in the mnemonics: FV0-FV12 in steps of 4, DR0-DR14 in steps of 2, FR0-FR15
u32 vector_fipr(u32 m, u32 n);
u32 vector_ftrv(u32 n);
u32 vector_fsca(u32 n);
u32 vector_fsrra(u32 n);

//Which of the four the opcode is. False: none
bool vector_decode(u32 opcode, VectorInsn& insn);

//Fills c.after and c.trap with what the hardware makes of c.before, by the model
void vector_reference(FpuCase& c);

//The registers the instruction of the case reads, for the report
void vector_describe(const FpuCase& c, char* text, size_t size);

//Generated cases: an instruction has classes of operands, a class has a number of cases
u32 vector_classes(VectorInsn insn);
const char* vector_class_name(VectorInsn insn, u32 cls);
u32 vector_class_cases(VectorInsn insn, u32 cls);

//Case <index> of the class: the opcode and the registers in front of it, FPSCR = fpscr. The operands depend on
//the instruction, the class and the index only
void vector_generate(VectorInsn insn, u32 cls, u32 index, u32 fpscr, FpuCase& c);

//FSCA: the class with every angle once, in FPUL as it is
#define VECTOR_FSCA_EVERY_ANGLE 0
