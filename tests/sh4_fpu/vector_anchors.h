/*
	What a Dreamcast answered for FIPR, FTRV, FSCA and FSRRA, as far as it is on record in text files of the
	shrike4-rtl checkout. A line of them names its operands, or the operand set of a header that is compiled in from
	the checkout:
	  model/fipr/hw_specials/fipr_specials.txt    FIPR FV0,FV4 on 50 operand sets (exact, inexact, overflow,
	                                              underflow, cancellation, infinities, NaNs, denormals) under
	                                              34 FPSCR settings: FR7, FPSCR, exception or not
	  model/fipr/hw_specials/ftrv_specials.txt    FTRV XMTRX,FV0 on the 17 operand sets of
	                                              ftrv_specials_vectors.h under 23 settings: FV0, FPSCR, exception
	  model/fsrra/hw_specials/fpu_specials.txt    FSRRA FR0 on 31 operands under 26 settings and FSCA FPUL,DR0 on 7
	                                              angles under 13: the registers, FPSCR, exception
	  model/fsrra/hw_specials/fpu_specials2.txt   6 more settings, on 8 of the operands and on one angle
	  model/fipr/kos_fipr_test/fipr_test.out      results alone, in both rounding modes: FIPR FV4,FV0 on 5,000
	                                              operand sets, FSRRA on 272 operands, FSCA on 1,028 angles
	The settings are DN, RM, the exception enables one at a time and together, cause and flag fields that are set
	in front of the instruction, PR = 1 and SZ = 1.

	The tests use them to prove that the model is called the way the hardware works (the model has to give every
	one of these answers), and run them as cases of their own.
*/
#pragma once
#include "vector_cases.h"

struct VectorAnchor
{
	u32 file;			//vector_anchor_file
	u32 line;
	VectorInsn insn;
	FpuCase c;			//the registers in front of the instruction as the console program set them up, and what the
						//console read back behind it. Registers it did not read back are as in front of it
	bool has_fpscr;		//the record has FPSCR behind the instruction and whether the exception was raised. Without
						//it c.after.fpscr and c.trap say nothing
};

//The files, by their paths in the checkout, and how many records of each instruction a file has
u32 vector_anchor_files();
const char* vector_anchor_file(u32 file);
u32 vector_anchor_records(u32 file, VectorInsn insn);

//Reads the files. False: one of them is not there, error has which. What a file has that the tests cannot
//use goes into complaints
bool vector_anchors_load(const std::string& shrike4_dir, std::vector<VectorAnchor>& anchors, std::string& error,
	std::vector<std::string>& complaints);
