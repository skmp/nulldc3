/*
	This file is part of libswirl

	The DSP of the AICA, after the DSP of the caique AICA model (caique-rtl model/sample-model/aica_model.cpp,
	Copyright 2026 Stefanos Kornilios Mitsis Poiitidis, MIT licence: README.md has the notice).
*/

#pragma once
#include <cstdint>

/*
	One implementation of the AICA's DSP for everything that runs it: the emulator's sound chip at every timing
	tier, the debugger's DSP single step and the DSP playground of the web tools (through wasm/). It needs
	nothing but <cstdint>, computes with integers only, and keeps all of its state in one AicaDsp, which has
	no constructor: a copy is a snapshot, and two of them do not know of each other.

	What it computes is what a Dreamcast answers. The rules were measured on a console by the caique project
	(its model/NOTES.md has the measurements, its model/tests/<case>/hw* the captures): "console: <case>" names
	the capture behind a rule below. README.md has the rules in one list, with what is not measured and what is
	not modelled.

	THE HARDWARE, AS THE CORE HAS IT

	A sample is 128 steps. Step s executes the 64 bit instruction MPRO[s]:

	  operands   INPUTS = MEMS[IRA] (24 bit) | MIXS[IRA - 0x20] << 4 | EXTS[IRA - 0x30] << 8 | 0
	             X = XSEL ? INPUTS : TEMP[TRA + MDEC_CT]
	             Y = FRC_REG (13 bit signed) | COEF[s] >> 3 (13 bit signed) | Y_REG[23:11] (signed)
	                 | Y_REG[15:4] (unsigned), by YSEL
	             B = ZERO ? 0 : BSEL ? ACC : TEMP[TRA + MDEC_CT], taken away for NEGB
	  shifter    SHIFTED, from the ACC the step before left: SHIFT 0 saturates it to 24 bit, 1 saturates twice
	             its value, 2 cuts twice its value to 24 bit, 3 cuts it to 24 bit
	  multiplier ACC = floor(X * Y / 4096) + B, cut to 26 bit
	  writes     TWT: TEMP[TWA + MDEC_CT] = SHIFTED          EWT: EFREG[EWA] = SHIFTED[23:8]
	             FRCL: FRC_REG = SHIFTED[23:11], or SHIFTED[11:0] for SHIFT 3
	             ADRL: ADRS_REG = INPUTS[23:16] (12 bit), or SHIFTED[23:12] for SHIFT 3
	             YRL: Y_REG = INPUTS            IWT: MEMS[IWA] = what the wave RAM read latch held (below)
	             MWT: wave RAM = SHIFTED[23:8] (NOFL), or SHIFTED packed into the 16 bit float format
	  reads      MRD: the wave RAM word goes to the read latch

	Everything a step reads it reads before the step writes: INPUTS sees MEMS before IWT, Y sees Y_REG before
	YRL, the memory address sees ADRS_REG before ADRL. ACC, FRC_REG, Y_REG, ADRS_REG and the latches live
	across samples: step 0 shifts the ACC of step 127. (console: dsp_basic, dsp_mem, dsp_mem2)

	Wave RAM. The address of MWT and MRD is MADRS[MASA] + ADRS_REG (ADREB, sign extended from 12 bit) + 1
	(NXADR), in words. TABLE = 0 adds MDEC_CT and wraps it in the ring of 8K << RBL words, TABLE = 1 wraps it
	at 64K words. The ring starts at byte RBP * 2048. MDEC_CT is 16 bit and counts down once a sample.
	(console: dsp_mem, dsp_mem2)

	The read latch. The wave RAM has one access slot per step, and every read through it, whoever reads,
	leaves its 16 bit word in one latch: an MRD of the DSP, the fetch of a sound channel, a read of the ARM7
	or of the SH4. The DSP copies the latch at the end of every odd step, and from the start of the next odd
	step on an IWT stores that copy, as it is << 8 when the step two before the IWT had NOFL, else unpacked
	from the float format. So the word of an MRD in an odd step s is what IWT stores in s + 2 and s + 3, the
	word of an MRD in an even step s in s + 3 and s + 4, unless another read comes in step s + 1. An MRD
	in a step that also has MWT reads nothing. (console: dsp_basic, dsp_mem, dsp_mem3, dsp_wslot, arm_latch,
	sh4_latch)

	Channels. Channel K reads the wave RAM in the slot of step 2K - 14 (mod 128), in every sample in which it
	fetches. A DSP step in a slot a channel has taken loses it: its MWT writes nothing, its MRD reads nothing,
	and the latch has the channel's word. (console: dsp_coll)

	MIXS. There are two banks of the 16 buses. The sound generator fills one in each of its sweeps over the 64
	channels: the first send of a sweep to a bus replaces the bus, the others add to it, in 20 bit and without
	saturation, and a bus no channel sends to keeps its value. The DSP reads the bank the sweep before filled,
	and that is the bank the CPU reads and writes at 0x4500. (console: mixs_write, mixs_rd, sgc_mix, eg_lock)

	HOW IT IS DRIVEN

	Nothing but calls: there is no time in the core. The simplest user loads a program with RegWrite and calls
	RunSample() for every sample. What the rest of the sound chip does to the DSP comes in through

	  SweepBegin, MixsSend, MixsSendLate   the sound generator's sends
	  ChannelFetch                         a channel's wave RAM read in its slot
	  BusRead                              a wave RAM read of the ARM7 or the SH4
	  EXTS[]                               the two CD-DA samples, written into the state

	either call by call, each before the StepOne of the step it belongs to, or, for a user that knows a
	sample's events before the sample runs, as one AicaDspSampleInputs given to StepOne / RunSample.
*/

#define AICA_DSP_STEPS 128

//The 16 bit float format of the wave RAM: sign, 4 bit exponent (the number of copies of the sign below it, 12
//at most), 11 bit mantissa. All 2^24 values and all 2^16 words are measured (console: dsp_pack, dsp_unpack)
uint16_t AicaDspPack(int32_t value24);
int32_t AicaDspUnpack(uint16_t word);	//24 bit, sign extended

//The fields of a step, MPRO[s][0..3]
struct AicaDspInst
{
	uint8_t TRA;	//[0] 15:9  TEMP read address, relative to MDEC_CT
	uint8_t TWT;	//[0] 8     TEMP write
	uint8_t TWA;	//[0] 7:1   TEMP write address, relative to MDEC_CT
	uint8_t XSEL;	//[1] 15    X: 0 TEMP, 1 INPUTS
	uint8_t YSEL;	//[1] 14:13 Y: 0 FRC_REG, 1 COEF, 2 Y_REG[23:11], 3 Y_REG[15:4]
	uint8_t IRA;	//[1] 12:7  INPUTS: 00-1F MEMS, 20-2F MIXS, 30-31 EXTS, others 0
	uint8_t IWT;	//[1] 6     MEMS write, from the read latch
	uint8_t IWA;	//[1] 5:1   MEMS write address
	uint8_t TABLE;	//[2] 15    memory address: 0 in the ring (+ MDEC_CT), 1 absolute in 64K words
	uint8_t MWT;	//[2] 14    wave RAM write
	uint8_t MRD;	//[2] 13    wave RAM read
	uint8_t EWT;	//[2] 12    EFREG write
	uint8_t EWA;	//[2] 11:8  EFREG write address
	uint8_t ADRL;	//[2] 7     ADRS_REG load
	uint8_t FRCL;	//[2] 6     FRC_REG load
	uint8_t SHIFT;	//[2] 5:4   shifter mode
	uint8_t YRL;	//[2] 3     Y_REG load
	uint8_t NEGB;	//[2] 2     B is taken away
	uint8_t ZERO;	//[2] 1     B is 0
	uint8_t BSEL;	//[2] 0     B: 0 TEMP, 1 ACC
	uint8_t NOFL;	//[3] 15    no float conversion: of this step's MWT, and of the IWT two steps on
	uint8_t MASA;	//[3] 14:9  MADRS index
	uint8_t ADREB;	//[3] 8     the memory address takes ADRS_REG
	uint8_t NXADR;	//[3] 7     the memory address takes 1
};

void AicaDspDecode(const uint16_t mpro[4], AicaDspInst* inst);
void AicaDspEncode(const AicaDspInst* inst, uint16_t mpro[4]);

//What a step did to the wave RAM, AicaDspStepView::mem_op
enum AicaDspMemOp
{
	AICA_DSP_MEM_NONE = 0,			//the step has neither MRD nor MWT
	AICA_DSP_MEM_READ = 1,			//MRD: mem_data went to the read latch
	AICA_DSP_MEM_WRITE = 2,			//MWT: mem_data was written
	AICA_DSP_MEM_WRITE_NO_READ = 3,	//MWT and MRD: mem_data was written, nothing was read
	AICA_DSP_MEM_LOST = 4,			//MRD or MWT in a slot a channel has taken: nothing was read or written
};

//What a step computed, for a debugger: the values on the datapath that the state does not keep, next to those it
//does. All of it is int32_t, in this order, so a host that cannot see the struct can take it as an array of
//AICA_DSP_STEP_VIEW_WORDS words
struct AicaDspStepView
{
	int32_t step;		//the step that was executed
	int32_t MDEC_CT;	//as the step used it
	int32_t INPUTS;		//24 bit
	int32_t SHIFTED;	//24 bit, from the ACC the step before left
	int32_t X;			//24 bit
	int32_t Y;			//13 bit
	int32_t B;			//26 bit
	int32_t ACC;		//26 bit, what the step leaves
	int32_t FRC_REG;	//after the step
	int32_t Y_REG;
	int32_t ADRS_REG;
	int32_t memval;		//what an IWT of this step stores, before its conversion
	int32_t memval_nofl;//1: that conversion is << 8, 0: it is the float format's (NOFL of the step two before)
	int32_t latch;		//the read latch after the step
	int32_t latch_copy;	//its copy, taken at the end of the last odd step
	int32_t mem_op;		//AicaDspMemOp
	int32_t mem_addr;	//byte address in the wave RAM, when mem_op is not AICA_DSP_MEM_NONE
	int32_t mem_data;	//the word that was read or written
	int32_t slot_taken;	//1: a channel had the wave RAM slot of this step
	int32_t mixs_bank;	//the MIXS bank the DSP reads in this sample
};

#define AICA_DSP_STEP_VIEW_WORDS 20

//The events of one sample, for a user that has them before the sample runs. A core without a sound generator
//around it needs none: StepOne() and RunSample() take a null pointer
struct AicaDspSampleInputs
{
	//The sweep of the sound generator that goes with this sample: bit i of mixs_sent says that a channel sends to
	//bus i (every channel sends to the bus of its ISEL in every sample, also when it is silent or its IMXL is 0),
	//mixs[i] is the sum of those sends. The DSP reads them in the next sample
	uint16_t mixs_sent;
	int32_t mixs[16];

	//The channels that read the wave RAM in their slot of this sample: bit K of fetch for channel K, fetch_word[K]
	//the word its fetch leaves, which is the last one it reads: the word that holds its sample CA + 1. Channels 0
	//to 6 have their slots (steps 114 to 126) behind the start of the sound generator's next sweep: what goes
	//here for them is the fetch of that sweep (console: dsp_coll)
	uint64_t fetch;
	uint16_t fetch_word[64];
};

//The program in MPRO as RunSample executes it. It is made from MPRO and from nothing else, and made again when MPRO
//is another program: it is no state. Nothing outside the core reads it
struct AicaDspPlan
{
	struct Row
	{
		uint16_t x, y, b;	//where the operands of the multiplier come from, B when it is not ACC
		uint16_t in;		//where INPUTS comes from
		uint16_t flags;		//what the step does besides multiplying ...
		uint8_t kind;		//... and what of that decides which code executes the step
		uint8_t twa, iwa, ewa, masa, shift;
		uint8_t nofl;		//NOFL of the step
		uint8_t raw;		//NOFL of the step two before: an IWT stores the read latch as it is
		uint8_t table, nxadr;
		uint8_t channel;	//the channel with the wave RAM slot of the step, for the even steps
	};

	uint16_t mpro[128][4];	//the program it is the plan of
	Row row[128];
	uint8_t empty_from;		//from this step on the rows are empty
	uint8_t valid;
};

struct AicaDsp
{
	/*
		Registers, as the CPU has them from 0x3000 on. A debugger reads and writes them here, the CPU through
		RegRead and RegWrite.
	*/
	uint16_t COEF[128];		//0x3000 + 4 i: bits 15:3 are the 13 bit coefficient of step i, bits 2:0 read 0
	uint16_t MADRS[64];		//0x3200 + 4 i
	uint16_t MPRO[128][4];	//0x3400 + 16 s + 4 k
	int32_t TEMP[128];		//0x4000 + 8 i: bits 7:0, + 4: bits 23:8. 24 bit, sign extended here
	int32_t MEMS[32];		//0x4400 + 8 i: bits 7:0 (the CPU cannot write them), + 4: bits 23:8. 24 bit, sign extended
	int32_t MIXS[2][16];	//0x4500 + 8 i: bits 3:0, + 4: bits 19:4, of the bank mixs_dsp. 20 bit, sign extended
	int16_t EFREG[16];		//0x4580 + 4 i
	int16_t EXTS[2];		//0x45C0 + 4 i: the CD-DA input, written here by whatever has it. The CPU reads 0
	uint16_t RBPL;			//0x2804: RBP 11:0, RBL 14:13, bit 15 is a test mode of MDEC_CT (StepOne). The CPU reads 0
	uint16_t MDEC_CT;		//not a register of the CPU

	//The datapath
	int32_t ACC;			//26 bit, sign extended
	int32_t Y_REG;			//24 bit, sign extended
	uint16_t FRC_REG;		//13 bit
	uint16_t ADRS_REG;		//12 bit
	uint16_t latch;			//the wave RAM read latch
	uint16_t latch_copy;	//its copy at the end of the last odd step
	uint16_t memval;		//what IWT stores: latch_copy as it was at the start of the last odd step
	uint8_t nofl_pipe;		//bit 0: NOFL of the last step, bit 1: of the one before

	//Where it is
	uint8_t step;			//the step StepOne executes next
	uint8_t slot_taken;		//ChannelFetch: a channel has the wave RAM slot of that step
	uint8_t mixs_dsp;		//the MIXS bank the DSP and the CPU have
	uint8_t mixs_fill;		//the MIXS bank the sound generator's sweep fills
	uint8_t mixs_swept;		//SweepBegin came since step 0 was executed
	uint16_t mixs_sent[2];	//per bank: the buses its sweep has sent to
	uint32_t samples;		//samples completed since Reset, for a user interface

	//Wave RAM: not owned. ram_mask is its size - 1, the size a power of two
	uint8_t* ram;
	uint32_t ram_mask;

	AicaDspPlan plan;

	/*
		Setting up
	*/

	//The state after power on, on a wave RAM of ram_size bytes (a power of two, 2 MB or 8 MB on the hardware).
	//Without wave RAM (0, 0) an MRD reads 0 and an MWT writes nothing
	void Init(uint8_t* wave_ram, uint32_t ram_size);

	//Everything but the wave RAM pointer goes to its power on value: 0, MDEC_CT 1. What a console has in MDEC_CT
	//after power on is not known, a program cannot read it
	void Reset();

	/*
		The CPU's side. off is the offset of a 32 bit slot in the AICA's register area, bits 1:0 and everything above
		0x7FFF are ignored. Each slot holds a 16 bit register.
	*/

	//The DSP has the register: 0x2804, COEF, MADRS, MPRO, TEMP, MEMS, MIXS, EFREG and EXTS. For any other offset
	//RegRead gives 0 and RegWrite does nothing
	static bool RegOwned(uint32_t off);

	//What the CPU reads. COEF, MADRS, MPRO and EFREG as they are, both halves of TEMP and MEMS, MIXS of the bank
	//the DSP reads. 0x2804 and EXTS read 0 (console: probe)
	uint32_t RegRead(uint32_t off) const;

	//What a CPU write does. COEF keeps bits 15:3. The low byte of MEMS is not written, a write of bits 23:8 keeps
	//it. MIXS is written in the bank the DSP reads: the DSP has the value in every other sample, and the sound
	//generator replaces it with its next sweep over that bank if a channel sends to the bus. EXTS is not written
	//(console: probe, mixs_rd, dsp_basic)
	void RegWrite(uint32_t off, uint32_t value);

	/*
		The sound generator's side
	*/

	//The sound generator starts a sweep over its 64 channels. Its sends go to the other MIXS bank from here on,
	//where the first send to a bus replaces it, and the bank that was filled up to here is the one the DSP reads
	//from its next step 0 on. On the hardware this is 16 steps before the DSP's step 0. Step 0 does it itself when
	//it was not called since the step 0 before: the banks change over once a sample either way. Which of the two
	//it was decides in which sample the DSP reads a send: MixsSend has the rule
	void SweepBegin();

	//A channel's send to its ISEL bus in the running sweep: the 20 bit bus takes the value, or adds it if the
	//sweep already sent to the bus, without saturation.
	//The send is in the bank that the DSP starts to read at the first step 0 behind the next SweepBegin, the one
	//that is called or the one that step 0 does itself. For a user without time inside the sample that is
	//  SweepBegin(); sends; RunSample()   the sends are read by the RunSample after this one: a sample of delay,
	//                                     as on the console and as the sends of an AicaDspSampleInputs
	//  sends; RunSample()                 the next SweepBegin is the one of this RunSample's step 0: the sends
	//                                     are read by this RunSample, a sample earlier than on the console
	void MixsSend(uint32_t bus, int32_t value);

	//The same for a send of the sweep before the running one. On the hardware the sends of the last 8 channels
	//land behind the start of the next sweep, and still in front of the DSP's step 0
	void MixsSendLate(uint32_t bus, int32_t value);

	//A channel reads the wave RAM in the slot of the step that StepOne executes next, and leaves word in the read
	//latch: the DSP step loses its MWT and its MRD
	void ChannelFetch(uint16_t word);

	//A read of the wave RAM by the ARM7 or the SH4 in the slot of the step that StepOne executes next: word stays
	//in the read latch. The ARM7 leaves the low half of what it reads. The SH4 reads in two slots, the low half
	//first and the upper half at least two steps later (console: arm_latch, sh4_latch). The bus gives such a read
	//no slot that a channel or an MRD / MWT of the DSP has: StepUsesRam says so for the DSP's part. A user without
	//time inside the sample calls it between two samples with the upper half of an SH4 read (console: dsp_mem3)
	void BusRead(uint16_t word);

	//Step s has an MRD or an MWT: the DSP takes the wave RAM slot of that step if no channel does
	bool StepUsesRam(uint32_t s) const;

	/*
		Running
	*/

	//Executes the step `step`. Step 0 starts a sample: the MIXS bank the sound generator filled last becomes the
	//DSP's. Step 127 ends it: MDEC_CT counts down, or, while bit 15 of RBPL is set, is XORed with 0x8080, which is
	//what 128 steps of that test mode leave of it (console: mdec_hold, per sample only).
	//in: the sample's events, for a user that has them per sample. Step 0 then begins the sweep and makes its
	//sends, and a step in the slot of a channel that fetches does its ChannelFetch. Such a user calls SweepBegin,
	//MixsSend and ChannelFetch for nothing that is in there.
	//view: gets what the step computed
	void StepOne(const AicaDspSampleInputs* in = nullptr, AicaDspStepView* view = nullptr);

	//The steps up to the end of the sample: all 128 from step 0, the rest of the sample from any other step. It
	//leaves what StepOne leaves, step for step, and is several times faster: the steps are executed from a plan of
	//the program (AicaDspPlan), and the empty rows a program ends with are not executed one by one
	void RunSample(const AicaDspSampleInputs* in = nullptr);

	//The step of channel K's wave RAM slot, 2K - 14 mod 128
	static uint32_t ChannelStep(uint32_t channel) { return (2 * channel + 114) & 127; }

	/*
		State as a flat blob: little endian, versioned, the same on every host. The wave RAM and its pointer are not
		part of it.
	*/
	static uint32_t StateSize();

	//Writes StateSize() bytes
	void SaveState(uint8_t* blob) const;

	//False, and the state is left as it was: blob is not a state of a version this core reads
	bool LoadState(const uint8_t* blob, uint32_t size);
};
