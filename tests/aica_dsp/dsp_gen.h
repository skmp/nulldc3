/*
	The generator of programs of aica_dsp_diff (diff_main.cpp), for the tests that run generated programs on
	something else than the caique model: aica.dsp.emu.lockstep (emu_main.cpp) runs them through the emulator's
	sound chip. What a function draws from the random stream, and in which order, is what diff_main.cpp drew before
	it was taken out of that file: the baseline of aica.dsp.diff.* holds it.
*/
#pragma once
#include "dsp_suite.h"
#include "hw/aica/dsp_core/aica_dsp_core.h"

#include <cstdint>

//Numbers from the ends of a range of `bits` bits, and around its powers of two
uint32_t dsp_gen_edge(DspRandom& rnd, int bits);

//What a random program is like
struct DspGenProfile
{
	bool sparse;		//a step does little
	bool edges;			//operands from the edges of their ranges
	uint32_t fetches;	//channel fetches: 0 none, 1 few, 2 half of the slots, 3 every slot
	uint32_t bus_reads;	//0 none, 1 one in some samples, 2 several a sample
	uint32_t cpu;		//CPU accesses between two samples, up to
};

//A register value of `bits` bits for a program of that profile
uint16_t dsp_gen_word(DspRandom& rnd, const DspGenProfile& p, int bits);

//The four MPRO words of an instruction, with `junk` in the bits no field has (they are register bits like the
//others): bit 0 into word 0, bit 1 into word 1, bits 8:2 into word 3
void dsp_gen_encode(const AicaDspInst& inst, uint32_t junk, uint16_t w[4]);

//The four MPRO words of a step of a random program: every bit random, or for a sparse one a step that does little
void dsp_gen_step(DspRandom& rnd, const DspGenProfile& p, uint16_t w[4]);
