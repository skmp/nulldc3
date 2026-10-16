/*
	The sample model of the caique project (caique-rtl model/sample-model), the reference of the AICA DSP tests.

	The model is compiled from the checkout as it is on disk. This is the only place that sees its class: the
	tests get pointers into its state, a call per sample, and a hook that the model's DSP calls at the start of
	every step, which is what lets a test look at the model after each of the 128 steps of a sample and not only
	after the sample.
*/
#pragma once
#include <cstdint>

struct CaiqueModel;

//Into the model's state. What the DSP reads and writes, and what the rest of the model feeds it
struct CaiqueRefs
{
	uint16_t* reg;				//the register file: reg[off >> 2]
	uint8_t* ram;				//wave RAM, CAIQUE_RAM_SIZE bytes

	int32_t* TEMP;				//[128]
	int32_t* MEMS;				//[32]
	int32_t (*MIXS_bank)[16];	//[2]: the sound generator fills [samples & 1], the DSP reads the other
	int32_t* EXTS;				//[2]
	int32_t* EFREG;				//[16]
	uint32_t* MDEC_CT;
	int32_t* ACC;
	int32_t* FRC_REG;
	int32_t* Y_REG;
	uint32_t* ADRS_REG;
	uint16_t* memval;
	uint16_t* latch;
	uint16_t* cp;
	uint8_t* nofl_pipe;			//[2]: NOFL of the last step, of the one before

	uint64_t* samples;
	bool* coll_claim;			//[64], per channel: it fetches in its wave RAM slot of this DSP sample
	uint16_t* coll_word;		//[64]: the word that fetch leaves
};

#define CAIQUE_RAM_SIZE (2u << 20)

//Called when the model's DSP is about to execute step `step` of a sample: steps 0 to step - 1 are done
typedef void CaiqueStepHook(void* ctx, int step);

CaiqueModel* caique_new();
void caique_delete(CaiqueModel* model);

void caique_reset(CaiqueModel* model, bool clear_ram);
void caique_refs(CaiqueModel* model, CaiqueRefs* refs);
void caique_set_hook(CaiqueModel* model, CaiqueStepHook* hook, void* ctx);

void caique_write(CaiqueModel* model, uint32_t off, uint32_t value);
uint32_t caique_read(CaiqueModel* model, uint32_t off);
uint32_t caique_ram_read32(CaiqueModel* model, uint32_t off);	//leaves its upper half in the DSP's read latch
bool caique_sh4_irq(CaiqueModel* model);

//One sample of the whole chip: sound generator, then DSP. False: the DSP did not call the hook once per step,
//so the model in the checkout is not the one this wrapper was written for
bool caique_step(CaiqueModel* model);

//The DSP's part of a sample alone, and the sample count behind it. The caller is the sound generator: it puts
//the MIXS bank, coll_claim and coll_word in place first. False as for caique_step
bool caique_dsp_sample(CaiqueModel* model);

//The float format as the model's shared header has it (caique-rtl model/src/dsp_float.h)
uint16_t caique_pack(int32_t value24);
int32_t caique_unpack(uint16_t word);
