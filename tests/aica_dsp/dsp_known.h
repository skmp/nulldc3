/*
	The known-answer programs of the AICA DSP core.

	Programs small enough to say by hand what they leave. The answers are numbers that are written down in
	dsp_known.cpp, each with the rule of the hardware it follows from: they are not the output of another
	implementation, so they need no data.

	  rules     the read latch, the distances from MRD to IWT, a channel in the slot, NOFL of IWT, EFREG, MDEC_CT
	  mixs      the sends of a sweep, the two banks, the late sends, the CPU's bank

	They are written on KnownMachine, which is nothing but the calls a user of the core has: whatever implements
	it runs the same set. aica.dsp.api runs it on the core called itself.

	Every program runs twice: with its samples executed step by step, and with RunSample.
*/
#pragma once
#include "hw/aica/dsp_core/aica_dsp_core.h"

#include <cstdint>

struct KnownMachine
{
	virtual ~KnownMachine() {}

	//A core after power on, on a wave RAM of ram_size bytes that are all 0. The samples have no inputs: their
	//events are calls
	virtual void Create(uint32_t ram_size) = 0;
	virtual void Reset() = 0;

	//The CPU
	virtual void RegWrite(uint32_t off, uint32_t value) = 0;
	virtual uint32_t RegRead(uint32_t off) = 0;

	//A register that is no array, by its index in the C interface (AICA_DSP_REG_*), as a debugger writes and
	//reads it
	virtual void Set(uint32_t index, uint32_t value) = 0;
	virtual uint32_t Get(uint32_t index) = 0;

	//The wave RAM: a 16 bit word at an even byte address, and how many of its bytes are not 0
	virtual void RamWrite(uint32_t addr, uint32_t word) = 0;
	virtual uint32_t RamRead(uint32_t addr) = 0;
	virtual uint32_t RamUsed() = 0;

	//The events of the samples from here on as one AicaDspSampleInputs, or none
	virtual void Inputs(const AicaDspSampleInputs* in) = 0;

	virtual void Steps(uint32_t count) = 0;		//StepOne
	virtual void RunSample() = 0;
	virtual void SweepBegin() = 0;
	virtual void MixsSend(uint32_t bus, int32_t value) = 0;
	virtual void MixsSendLate(uint32_t bus, int32_t value) = 0;
	virtual void ChannelFetch(uint32_t word) = 0;
	virtual void BusRead(uint32_t word) = 0;

	//The answers that follow belong to `name`
	virtual void Group(const char* name) = 0;

	//An answer: it is the known one, or not
	virtual void Check(bool holds, const char* what) = 0;
};

//All of the programs, in both ways
void dsp_known_answers(KnownMachine& machine);

//A register that is no array by its index in the C interface, on a core: what KnownMachine::Set and Get are on
//an AicaDsp. Set cuts the value to the bits the register has
void dsp_known_set(AicaDsp& dsp, uint32_t index, uint32_t value);
uint32_t dsp_known_get(const AicaDsp& dsp, uint32_t index);
