/*
	The SH4 context in the layout of the single-step vectors: loading a state, reading it back, comparing.
*/
#pragma once
#include "ss_format.h"

#include <cstdio>

//Puts the machine into the state. R / FP0 are the active banks and go where the CPU works on them, nothing is
//swapped. Everything of the machine that is not part of a vector state is reset, so vectors do not leak into
//each other
void ss_state_load(const SsState& state);

//Reads the machine state back
void ss_state_save(SsState& state);

struct SsCompare
{
	bool exact;			//all words are equal
	bool lenient;		//everything but the FP registers is equal and those pass ss_float_lenient
	int first_exact;	//index of the first word that is not equal, -1 if none
	int first_lenient;	//index of the first word that fails the lenient comparison, -1 if none
};

SsCompare ss_state_compare(const SsState& expected, const SsState& actual);

//The comparison the vectors' README proposes for FP registers, on the bits of one 32 bit register: equal as
//floats, both NaN, at most a few units in the last place apart or less than 1e-7 apart. Two deviations from the
//README: the difference is taken as an absolute value (as written it accepts any result above the expected one)
//and its four value pairs that excuse one particular emulator are left out
bool ss_float_lenient(u32 mine, u32 theirs);

//Prints the registers of a vector, one per line, marking what differs from expected
void ss_state_print(FILE* out, const SsState& initial, const SsState& expected, const SsState& actual);
