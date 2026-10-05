/*
	The DSP core next to the caique sample model, step for step.

	Both get the same CPU accesses and the same events, the model runs a sample, and behind every one of its 128
	steps the core runs that step and everything the DSP has is compared: TEMP, MEMS, both MIXS banks, EFREG,
	MDEC_CT, ACC, FRC_REG, Y_REG, ADRS_REG, the read latch, its copy, what IWT stores, the NOFL of the last two
	steps, and the wave RAM the step can have written. The core has its own wave RAM, so a write that only one of
	the two makes shows.

	The view the core gives of a step (AicaDspStepView) is compared too, all of its words. What a step computes
	and no register keeps, INPUTS, SHIFTED, X, Y, B and what it did to the wave RAM, where and with which word,
	is worked out from the model's state in front of the step; the rest of the view is the model's state behind
	it.

	Two ways to run a sample:
	  StepChip   the whole model: its sound generator makes the sends and the channel fetches, the core gets them
	             as the model has them when its DSP starts
	  StepDsp    the model's DSP alone, on events the test makes up: sends, channel fetches in any slot, and bus
	             reads in front of any step. The sample model has bus reads between samples only: one in front of
	             a later step is put into the model's latch at that step, which is where the clocked model of
	             caique has it

	The core runs a sample in two ways, StepOne 128 times and RunSample, which is other code. A second core gets
	everything the first gets and runs each sample with RunSample, from step 0 or, behind some steps of StepOne,
	from a step in the middle; behind every sample it has to be where the first is, in every register and in its
	own wave RAM. So RunSample is compared with the model as well, sample for sample.
*/
#pragma once
#include "caique_model.h"
#include "hw/aica/dsp_core/aica_dsp_core.h"

#include <cstdint>
#include <string>
#include <vector>

struct LockstepSend
{
	uint8_t bus;
	int32_t value;
};

//The events of a sample for StepDsp
struct LockstepEvents
{
	std::vector<LockstepSend> sends;	//of the sweep, in order
	uint64_t fetch;						//per channel, as AicaDspSampleInputs
	uint16_t fetch_word[64];
	bool bus_read[128];					//a bus read in the slot of that step
	uint16_t bus_word[128];

	LockstepEvents();
};

struct DspLockstep
{
	CaiqueModel* model;
	CaiqueRefs m;

	AicaDsp core;
	std::vector<uint8_t> core_ram;

	AicaDsp fast;			//the core that runs its samples with RunSample
	std::vector<uint8_t> fast_ram;

	//How the core is fed: a sample's events as one AicaDspSampleInputs, or call by call. Alternates per sample
	bool feed_inputs;

	//What the compared steps did, counted like `steps`: a run that compares the same number of steps on programs
	//that no longer read or write would show here
	struct Coverage
	{
		uint64_t ram_reads;		//MRD that read
		uint64_t ram_writes;	//MWT that wrote
		uint64_t no_reads;		//of those: with an MRD in the same step, which reads nothing
		uint64_t slots_lost;	//MRD or MWT in a slot a channel had
		uint64_t iwt_raw;		//IWT that stored the latch as it is
		uint64_t iwt_float;		//IWT that unpacked it
		uint64_t fetches;		//channel fetches
		uint64_t bus_reads;		//bus reads inside a sample
		uint64_t sends;			//sends of the sound generator
	} cover;

	uint64_t fast_samples;	//samples of RunSample compared, likewise
	uint64_t steps;			//compared since the object is there. Counting stops at a difference, until the next Reset
	uint64_t reg_reads;		//CPU reads of DSP registers compared, likewise
	bool ok;				//since the last Reset
	std::string first;		//the first difference, or what is wrong with the wiring

	DspLockstep();
	~DspLockstep();
	DspLockstep(const DspLockstep&) = delete;
	DspLockstep& operator=(const DspLockstep&) = delete;

	//Both to power on. The wave RAM is cleared, or stays as it is in both
	void Reset(bool clear_ram = true);

	//MDEC_CT is nothing a program can set: the tests put it where they want the ring
	void SetMdecCt(uint32_t value);

	//The CPU. Reads give what the model answers; where the DSP has the register the core has to answer the same
	void RegWrite(uint32_t off, uint32_t value);
	uint32_t RegRead(uint32_t off);
	void RamWrite32(uint32_t off, uint32_t value);
	uint32_t RamRead32(uint32_t off);
	void RamPoke(uint32_t addr, uint8_t value);			//a byte into every wave RAM, at no time
	void RamFill(const uint8_t* image);					//CAIQUE_RAM_SIZE bytes into every wave RAM
	void SetExts(int index, int32_t value);

	void StepChip();
	void StepDsp(const LockstepEvents& events);

	//Wave RAM of the model and of the core, all of it
	void CompareRam();

	//for the hook
	void OnStep(int step);

private:
	const LockstepEvents* events;	//StepDsp: the running sample's
	AicaDspSampleInputs inputs;		//the running sample's, as a user with an AicaDspSampleInputs has them

	bool sample_wrote;				//a step of the running sample wrote wave RAM

	AicaDspStepView due;			//what the view of the next step has to say, as far as the model has it in front of the step

	void BeginSample();
	void ViewDue(int step);
	void FastSample();
	void CoreStep(int step);
	void Compare(int step, const AicaDspStepView& view);
	void Differs(int step, const char* what, long long model_value, long long core_value);
};
