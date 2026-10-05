/*
	This file is part of libswirl

	The DSP of the AICA, after the DSP of the caique AICA model (caique-rtl model/sample-model/aica_model.cpp,
	Copyright 2026 Stefanos Kornilios Mitsis Poiitidis, MIT licence: README.md has the notice).
*/

#include "aica_dsp_core.h"

//v as a signed number of `bits` bits
static inline int32_t dsp_sext(int32_t v, int bits)
{
	return (int32_t)((uint32_t)v << (32 - bits)) >> (32 - bits);
}

static inline int32_t dsp_clamp24(int32_t v)
{
	return v < -0x800000 ? -0x800000 : v > 0x7FFFFF ? 0x7FFFFF : v;
}

/*
	The float format. The exponent counts the bits below the sign that repeat it, 12 at most. Up to 11 the mantissa
	is the 11 bits below the first bit that differs from the sign, and that bit is not stored. With 12 it is the low
	11 bits as they are: all that is left below the 12 copies.
*/
uint16_t AicaDspPack(int32_t value24)
{
	const uint32_t v = (uint32_t)value24 & 0xFFFFFF;
	const uint32_t sign = v >> 23;

	//bit i: bit i of v is not bit i - 1. The highest such bit ends the run of copies of the sign
	const uint32_t differs = (v ^ (v << 1)) & 0xFFF000;

	//the position of that bit among the 12 below the sign, found by halving
	uint32_t top = differs >> 12;
	uint32_t exponent = 12;

	if (top)
	{
		exponent = 0;

		if (!(top & 0xFC0)) { exponent += 6; top <<= 6; }
		if (!(top & 0xE00)) { exponent += 3; top <<= 3; }
		if (!(top & 0x800)) { exponent += 1; top <<= 1; }
		if (!(top & 0x800)) { exponent += 1; }
	}

	const uint32_t mantissa = exponent < 12 ? ((v << exponent) >> 11) & 0x7FF : v & 0x7FF;

	return (uint16_t)((sign << 15) | (exponent << 11) | mantissa);
}

/*
	Back from the float format: sign, the bit that is not stored, and the mantissa below them, shifted down by the
	exponent. The bit that is not stored is the opposite of the sign. Exponents above 11 are read as 11, with the
	sign in the place of that bit. Both depend on the top 5 bits of the word only, which are the index here: what
	goes above the mantissa, as bits 23 and 22, and how far all of it is shifted down from the top of 32 bits.
*/
static const uint32_t unpack_top[32] =
{
	0x400000, 0x400000, 0x400000, 0x400000, 0x400000, 0x400000, 0x400000, 0x400000,
	0x400000, 0x400000, 0x400000, 0x400000, 0x000000, 0x000000, 0x000000, 0x000000,
	0x800000, 0x800000, 0x800000, 0x800000, 0x800000, 0x800000, 0x800000, 0x800000,
	0x800000, 0x800000, 0x800000, 0x800000, 0xC00000, 0xC00000, 0xC00000, 0xC00000,
};

static const uint8_t unpack_shift[32] =
{
	8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 19, 19, 19, 19,
	8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 19, 19, 19, 19,
};

static inline int32_t dsp_unpack(uint32_t word)
{
	const uint32_t index = (word >> 11) & 31;
	const uint32_t v = unpack_top[index] | ((word & 0x7FF) << 11);

	return (int32_t)(v << 8) >> unpack_shift[index];
}

int32_t AicaDspUnpack(uint16_t word)
{
	return dsp_unpack(word);
}

void AicaDspDecode(const uint16_t mpro[4], AicaDspInst* inst)
{
	inst->TRA = (mpro[0] >> 9) & 0x7F;
	inst->TWT = (mpro[0] >> 8) & 1;
	inst->TWA = (mpro[0] >> 1) & 0x7F;

	inst->XSEL = (mpro[1] >> 15) & 1;
	inst->YSEL = (mpro[1] >> 13) & 3;
	inst->IRA = (mpro[1] >> 7) & 0x3F;
	inst->IWT = (mpro[1] >> 6) & 1;
	inst->IWA = (mpro[1] >> 1) & 0x1F;

	inst->TABLE = (mpro[2] >> 15) & 1;
	inst->MWT = (mpro[2] >> 14) & 1;
	inst->MRD = (mpro[2] >> 13) & 1;
	inst->EWT = (mpro[2] >> 12) & 1;
	inst->EWA = (mpro[2] >> 8) & 0xF;
	inst->ADRL = (mpro[2] >> 7) & 1;
	inst->FRCL = (mpro[2] >> 6) & 1;
	inst->SHIFT = (mpro[2] >> 4) & 3;
	inst->YRL = (mpro[2] >> 3) & 1;
	inst->NEGB = (mpro[2] >> 2) & 1;
	inst->ZERO = (mpro[2] >> 1) & 1;
	inst->BSEL = mpro[2] & 1;

	inst->NOFL = (mpro[3] >> 15) & 1;
	inst->MASA = (mpro[3] >> 9) & 0x3F;
	inst->ADREB = (mpro[3] >> 8) & 1;
	inst->NXADR = (mpro[3] >> 7) & 1;
}

void AicaDspEncode(const AicaDspInst* inst, uint16_t mpro[4])
{
	mpro[0] = (uint16_t)(((inst->TRA & 0x7F) << 9) | ((inst->TWT & 1) << 8) | ((inst->TWA & 0x7F) << 1));

	mpro[1] = (uint16_t)(((inst->XSEL & 1) << 15) | ((inst->YSEL & 3) << 13) | ((inst->IRA & 0x3F) << 7)
		| ((inst->IWT & 1) << 6) | ((inst->IWA & 0x1F) << 1));

	mpro[2] = (uint16_t)(((inst->TABLE & 1) << 15) | ((inst->MWT & 1) << 14) | ((inst->MRD & 1) << 13)
		| ((inst->EWT & 1) << 12) | ((inst->EWA & 0xF) << 8) | ((inst->ADRL & 1) << 7) | ((inst->FRCL & 1) << 6)
		| ((inst->SHIFT & 3) << 4) | ((inst->YRL & 1) << 3) | ((inst->NEGB & 1) << 2) | ((inst->ZERO & 1) << 1)
		| (inst->BSEL & 1));

	mpro[3] = (uint16_t)(((inst->NOFL & 1) << 15) | ((inst->MASA & 0x3F) << 9) | ((inst->ADREB & 1) << 8)
		| ((inst->NXADR & 1) << 7));
}


void AicaDsp::Init(uint8_t* wave_ram, uint32_t ram_size)
{
	ram = ram_size >= 2 ? wave_ram : nullptr;
	ram_mask = ram ? ram_size - 1 : 0;

	Reset();
}

void AicaDsp::Reset()
{
	for (uint16_t& v : COEF) v = 0;
	for (uint16_t& v : MADRS) v = 0;
	for (auto& row : MPRO) for (uint16_t& v : row) v = 0;
	for (int32_t& v : TEMP) v = 0;
	for (int32_t& v : MEMS) v = 0;
	for (auto& bank : MIXS) for (int32_t& v : bank) v = 0;
	for (int16_t& v : EFREG) v = 0;
	EXTS[0] = EXTS[1] = 0;
	RBPL = 0;
	MDEC_CT = 1;

	ACC = 0;
	Y_REG = 0;
	FRC_REG = 0;
	ADRS_REG = 0;
	latch = 0;
	latch_copy = 0;
	memval = 0;
	nofl_pipe = 0;

	step = 0;
	slot_taken = 0;
	mixs_dsp = 0;
	mixs_fill = 1;
	mixs_swept = 0;
	mixs_sent[0] = mixs_sent[1] = 0;
	samples = 0;

	plan.valid = 0;
}


bool AicaDsp::RegOwned(uint32_t off)
{
	off &= 0x7FFC;

	return off == 0x2804 || (off >= 0x3000 && off < 0x3300) || (off >= 0x3400 && off < 0x3C00)
		|| (off >= 0x4000 && off < 0x45C8);
}

uint32_t AicaDsp::RegRead(uint32_t off) const
{
	off &= 0x7FFC;

	if (off >= 0x3000 && off < 0x3200)
		return COEF[(off - 0x3000) >> 2] & 0xFFF8u;

	if (off >= 0x3200 && off < 0x3300)
		return MADRS[(off - 0x3200) >> 2];

	if (off >= 0x3400 && off < 0x3C00)
		return MPRO[(off - 0x3400) >> 4][(off >> 2) & 3];

	if (off >= 0x4000 && off < 0x4400)
	{
		const int32_t v = TEMP[(off - 0x4000) >> 3];

		return (off & 4) ? (v >> 8) & 0xFFFF : v & 0xFF;
	}

	if (off >= 0x4400 && off < 0x4500)
	{
		const int32_t v = MEMS[(off - 0x4400) >> 3];

		return (off & 4) ? (v >> 8) & 0xFFFF : v & 0xFF;
	}

	if (off >= 0x4500 && off < 0x4580)
	{
		const int32_t v = MIXS[mixs_dsp & 1][(off - 0x4500) >> 3];

		return (off & 4) ? (v >> 4) & 0xFFFF : v & 0xF;
	}

	if (off >= 0x4580 && off < 0x45C0)
		return (uint16_t)EFREG[(off - 0x4580) >> 2];

	return 0;
}

void AicaDsp::RegWrite(uint32_t off, uint32_t value)
{
	off &= 0x7FFC;

	const uint32_t v = value & 0xFFFF;

	if (off == 0x2804)
		RBPL = (uint16_t)v;
	else if (off >= 0x3000 && off < 0x3200)
		COEF[(off - 0x3000) >> 2] = (uint16_t)(v & 0xFFF8);
	else if (off >= 0x3200 && off < 0x3300)
		MADRS[(off - 0x3200) >> 2] = (uint16_t)v;
	else if (off >= 0x3400 && off < 0x3C00)
		MPRO[(off - 0x3400) >> 4][(off >> 2) & 3] = (uint16_t)v;
	else if (off >= 0x4000 && off < 0x4400)
	{
		int32_t& t = TEMP[(off - 0x4000) >> 3];

		if (off & 4)
			t = dsp_sext((int32_t)((v << 8) | ((uint32_t)t & 0xFF)), 24);
		else
			t = (int32_t)(((uint32_t)t & ~0xFFu) | (v & 0xFF));
	}
	else if (off >= 0x4400 && off < 0x4500)
	{
		int32_t& m = MEMS[(off - 0x4400) >> 3];

		if (off & 4)
			m = dsp_sext((int32_t)((v << 8) | ((uint32_t)m & 0xFF)), 24);
	}
	else if (off >= 0x4500 && off < 0x4580)
	{
		int32_t& b = MIXS[mixs_dsp & 1][(off - 0x4500) >> 3];

		if (off & 4)
			b = dsp_sext((int32_t)((v << 4) | ((uint32_t)b & 0xF)), 20);
		else
			b = (int32_t)(((uint32_t)b & ~0xFu) | (v & 0xF));
	}
	else if (off >= 0x4580 && off < 0x45C0)
		EFREG[(off - 0x4580) >> 2] = (int16_t)v;
}


void AicaDsp::SweepBegin()
{
	mixs_fill = (uint8_t)((mixs_fill & 1) ^ 1);
	mixs_sent[mixs_fill] = 0;
	mixs_swept = 1;
}

static inline void mixs_send(AicaDsp& d, uint32_t bank, uint32_t bus, int32_t value)
{
	bus &= 15;

	int32_t& b = d.MIXS[bank][bus];
	const uint32_t base = (d.mixs_sent[bank] >> bus) & 1 ? (uint32_t)b : 0;

	b = dsp_sext((int32_t)(base + (uint32_t)value), 20);
	d.mixs_sent[bank] |= (uint16_t)(1u << bus);
}

void AicaDsp::MixsSend(uint32_t bus, int32_t value)
{
	mixs_send(*this, mixs_fill & 1u, bus, value);
}

void AicaDsp::MixsSendLate(uint32_t bus, int32_t value)
{
	mixs_send(*this, (mixs_fill & 1u) ^ 1u, bus, value);
}

void AicaDsp::ChannelFetch(uint16_t word)
{
	latch = word;
	slot_taken = 1;
}

void AicaDsp::BusRead(uint16_t word)
{
	latch = word;
}

bool AicaDsp::StepUsesRam(uint32_t s) const
{
	return (MPRO[s & 127][2] & 0x6000) != 0;
}


/*
	Running, one step at a time: StepOne. The step as the hardware description in the header has it, on the state
	as it is.
*/

//What step 0 does before it executes: the sample's sweep if the caller has it per sample, or the one nobody
//began, and the change of the MIXS banks
static inline void sample_begin(AicaDsp& d, const AicaDspSampleInputs* in)
{
	if (in)
	{
		d.SweepBegin();

		//the sums of the sweep: each is the first send to its bus
		int32_t* bank = d.MIXS[d.mixs_fill];

		for (uint32_t bus = 0; bus < 16; bus++)
			bank[bus] = (in->mixs_sent >> bus) & 1 ? dsp_sext(in->mixs[bus], 20) : bank[bus];

		d.mixs_sent[d.mixs_fill] = in->mixs_sent;
	}
	else if (!(d.mixs_swept & 1))
		d.SweepBegin();

	d.mixs_swept = 0;
	d.mixs_dsp = (uint8_t)((d.mixs_fill & 1) ^ 1);
}

//What step 127 does behind itself
static inline void sample_end(AicaDsp& d)
{
	d.step = 0;
	d.MDEC_CT = (uint16_t)((d.RBPL & 0x8000) ? d.MDEC_CT ^ 0x8080 : d.MDEC_CT - 1);
	d.samples++;
}

//Byte address of the wave RAM word of an MWT or MRD
static inline uint32_t dsp_mem_addr(const AicaDsp& d, uint32_t masa, bool adreb, bool nxadr, bool table, uint32_t adrs,
	uint32_t ct)
{
	uint32_t addr = d.MADRS[masa];

	if (adreb)
		addr += (uint32_t)dsp_sext((int32_t)adrs, 12);

	if (nxadr)
		addr++;

	if (table)
		addr &= 0xFFFF;
	else
		addr = (addr + ct) & ((8192u << ((d.RBPL >> 13) & 3)) - 1);

	return ((addr << 1) + (d.RBPL & 0xFFFu) * 2048u) & d.ram_mask & ~1u;
}

static inline uint32_t dsp_ram_read(const AicaDsp& d, uint32_t addr)
{
	return d.ram ? (uint32_t)(d.ram[addr] | (d.ram[addr + 1] << 8)) : 0;
}

static inline void dsp_ram_write(AicaDsp& d, uint32_t addr, uint32_t word)
{
	if (d.ram)
	{
		d.ram[addr] = (uint8_t)word;
		d.ram[addr + 1] = (uint8_t)(word >> 8);
	}
}

//The shifter, on the ACC the step before left
static inline int32_t dsp_shifter(int32_t acc, uint32_t shift)
{
	const int32_t scaled = (shift == 1 || shift == 2) ? acc * 2 : acc;

	return (shift & 2) ? dsp_sext(scaled, 24) : dsp_clamp24(scaled);
}

void AicaDsp::StepOne(const AicaDspSampleInputs* in, AicaDspStepView* view)
{
	const uint32_t s = step & 127;

	if (s == 0)
		sample_begin(*this, in);

	if (in && !(s & 1))
	{
		const uint32_t channel = ((s + 14) >> 1) & 63;

		if ((in->fetch >> channel) & 1)
			ChannelFetch(in->fetch_word[channel]);
	}

	const bool taken = (slot_taken & 1) != 0;

	slot_taken = 0;

	AicaDspInst i;

	AicaDspDecode(MPRO[s], &i);

	//IWT stores the copy the latch had at the start of the last odd step
	if (s & 1)
		memval = latch_copy;

	int32_t inputs;

	if (i.IRA <= 0x1F)
		inputs = MEMS[i.IRA];
	else if (i.IRA <= 0x2F)
		inputs = (int32_t)((uint32_t)MIXS[mixs_dsp & 1][i.IRA - 0x20] << 4);
	else if (i.IRA <= 0x31)
		inputs = EXTS[i.IRA - 0x30] * 256;
	else
		inputs = 0;

	inputs = dsp_sext(inputs, 24);

	const uint32_t stored_raw = (nofl_pipe >> 1) & 1;

	if (i.IWT)
		MEMS[i.IWA] = stored_raw ? dsp_sext((int32_t)((uint32_t)memval << 8), 24) : dsp_unpack(memval);

	//ACC and TEMP as the numbers their 26 and 24 bit are: the sums below then stay inside 32 bit
	const int32_t acc = dsp_sext(ACC, 26);
	const int32_t shifted = dsp_shifter(acc, i.SHIFT);
	const uint32_t ct = MDEC_CT;
	const int32_t temp = dsp_sext(TEMP[(i.TRA + ct) & 0x7F], 24);
	int32_t b = i.ZERO ? 0 : i.BSEL ? acc : temp;

	if (i.NEGB)
		b = -b;

	const int32_t x = i.XSEL ? inputs : temp;
	int32_t y;

	switch (i.YSEL)
	{
	case 0: y = dsp_sext(FRC_REG, 13); break;
	case 1: y = dsp_sext(COEF[s] >> 3, 13); break;
	case 2: y = dsp_sext(Y_REG >> 11, 13); break;
	default: y = (Y_REG >> 4) & 0xFFF; break;
	}

	if (i.YRL)
		Y_REG = inputs;

	ACC = dsp_sext((int32_t)(((int64_t)x * y) >> 12) + b, 26);

	if (i.TWT)
		TEMP[(i.TWA + ct) & 0x7F] = shifted;

	if (i.FRCL)
		FRC_REG = (uint16_t)(i.SHIFT == 3 ? shifted & 0xFFF : (shifted >> 11) & 0x1FFF);

	int32_t mem_op = AICA_DSP_MEM_NONE;
	uint32_t mem_addr = 0;
	uint32_t mem_data = 0;

	if (i.MWT || i.MRD)
	{
		mem_addr = dsp_mem_addr(*this, i.MASA, i.ADREB, i.NXADR, i.TABLE, ADRS_REG, ct);

		if (taken)
			mem_op = AICA_DSP_MEM_LOST;
		else if (i.MWT)
		{
			mem_data = i.NOFL ? (uint16_t)(shifted >> 8) : AicaDspPack(shifted);
			mem_op = i.MRD ? AICA_DSP_MEM_WRITE_NO_READ : AICA_DSP_MEM_WRITE;
			dsp_ram_write(*this, mem_addr, mem_data);
		}
		else
		{
			mem_data = dsp_ram_read(*this, mem_addr);
			mem_op = AICA_DSP_MEM_READ;
			latch = (uint16_t)mem_data;
		}
	}

	if (s & 1)
		latch_copy = latch;

	if (i.ADRL)
		ADRS_REG = (uint16_t)(i.SHIFT == 3 ? (shifted >> 12) & 0xFFF : (inputs >> 16) & 0xFFF);

	if (i.EWT)
		EFREG[i.EWA] = (int16_t)(shifted >> 8);

	const uint8_t stored_nofl = (uint8_t)stored_raw;

	nofl_pipe = (uint8_t)(((nofl_pipe << 1) | i.NOFL) & 3);

	if (view)
	{
		view->step = (int32_t)s;
		view->MDEC_CT = (int32_t)ct;
		view->INPUTS = inputs;
		view->SHIFTED = shifted;
		view->X = x;
		view->Y = y;
		view->B = b;
		view->ACC = ACC;
		view->FRC_REG = FRC_REG & 0x1FFF;
		view->Y_REG = dsp_sext(Y_REG, 24);
		view->ADRS_REG = ADRS_REG & 0xFFF;
		view->memval = memval;
		view->memval_nofl = stored_nofl;
		view->latch = latch;
		view->latch_copy = latch_copy;
		view->mem_op = mem_op;
		view->mem_addr = (int32_t)mem_addr;
		view->mem_data = (int32_t)mem_data;
		view->slot_taken = taken ? 1 : 0;
		view->mixs_bank = mixs_dsp & 1;
	}

	if (s == AICA_DSP_STEPS - 1)
		sample_end(*this);
	else
		step = (uint8_t)(s + 1);
}


/*
	Running a sample in one go: RunSample. The same steps, laid out for speed.

	A step of StepOne spends most of its time finding out what its row asks for. Here that is done once per
	program: the plan has, per step, where X, Y and B come from as indices into one file of operands, and the rest
	of what the step does as a set of flags that is empty for most steps. The file is filled when the sample starts:

	  TEMP    as the steps address it: entry i is TEMP[i + MDEC_CT]
	  MEMS    as it is
	  MIXS    << 4, of the bank the DSP reads, and EXTS << 8: INPUTS, with a 0 for the IRA values that have none
	  FRC_REG and the two halves of Y_REG the multiplier takes, each as the 13 bit number Y is
	  COEF    as the 13 bit numbers Y is

	and TEMP and MEMS go back into the state behind the last step. Nothing else of the state is in it: nothing
	writes the DSP's bank of MIXS, EXTS or COEF while a sample runs in one go.

	Every entry is the number that the bits of its field are (the header has which bits a field has), whatever
	else the field holds: no sum of a step can then leave 32 bit.
*/

enum
{
	F_TEMP = 0,			//128
	F_MEMS = 128,		//32
	F_MIXS = 160,		//16
	F_EXTS = 176,		//2
	F_ZERO = 178,
	F_FRC = 179,
	F_YHIGH = 180,
	F_YLOW = 181,
	F_COEF = 182,		//128
	F_SIZE = 310,
};

//What a step does besides multiplying, as far as its kind (below) does not say it
enum
{
	P_YRL = 0x0001,
	P_TWT = 0x0002,
	P_FRCL = 0x0004,
	P_MWT = 0x0008,
	P_ADRL = 0x0010,
	P_EWT = 0x0020,
	P_ADREB = 0x0040,	//of the step's MRD or MWT
	P_NOFL = 0x0080,	//of the step's MWT
};

/*
	The kind of a step: what of it is not the same in every step. There is one copy of the step's code per kind,
	each with only what its kind needs, and a step is one jump to the copy of its row's kind.
*/
enum
{
	K_ODD = 1,		//an odd step: it copies the read latch
	K_BACC = 2,		//B is ACC
	K_NEG = 4,		//B is taken away
	K_IWT = 8,
	K_READ = 16,	//MRD without MWT: a wave RAM read
	K_OTHER = 32,	//any of MWT, YRL, TWT, FRCL, ADRL, EWT
	K_COUNT = 64,
};

//The step is written once and compiled once per kind into the loop over the sample, which only is a fast loop if
//the compiler does put it there
#if defined(_MSC_VER)
#define DSP_STEP_INLINE __forceinline
#elif defined(__GNUC__)
#define DSP_STEP_INLINE inline __attribute__((always_inline))
#else
#define DSP_STEP_INLINE inline
#endif

namespace
{
	//What the steps of a sample run on: the registers of the datapath, out of the state while the loop runs
	struct Run
	{
		AicaDsp* d;
		uint8_t* ram;
		int32_t* file;
		const uint16_t* fetch_word;
		uint64_t fetch;			//the even steps from the next one on, a bit each: the channel with its slot fetches
		int32_t acc;
		uint32_t adrs;
		uint32_t latch, copy, stored;
		const AicaDspPlan::Row* settled;	//from this row on NOFL of the step two before is the plan's
		uint32_t nofl_before;	//for the two rows in front of it: NOFL of the two steps in front of the loop
		uint32_t add_ct[2];		//by TABLE: what the address takes of MDEC_CT ...
		uint32_t wrap[2];		//... and where it wraps
		uint32_t ring_base, ram_even;
	};
}

static DSP_STEP_INLINE uint32_t run_mem_addr(const Run& c, const AicaDspPlan::Row& r)
{
	uint32_t addr = c.d->MADRS[r.masa] + r.nxadr;

	if (r.flags & P_ADREB)
		addr += (uint32_t)dsp_sext((int32_t)c.adrs, 12);

	addr = (addr + c.add_ct[r.table]) & c.wrap[r.table];

	return ((addr << 1) + c.ring_base) & c.ram_even;
}

template <uint32_t K>
static DSP_STEP_INLINE void run_step(Run& c, const AicaDspPlan::Row& r)
{
	bool taken = false;

	if (K & K_ODD)
		c.stored = c.copy;
	else
	{
		if (c.fetch & 1)
		{
			c.latch = c.fetch_word[r.channel];
			taken = true;
		}

		c.fetch >>= 1;
	}

	//the multiplier: what every step does
	const int32_t x = c.file[r.x];
	const int32_t y = c.file[r.y];
	const int32_t b_from = (K & K_BACC) ? c.acc : c.file[r.b];
	const int32_t b = (K & K_NEG) ? -b_from : b_from;
	const int32_t acc_next = dsp_sext((int32_t)(((int64_t)x * y) >> 12) + b, 26);

	//INPUTS is read before IWT writes MEMS
	const int32_t inputs = (K & K_OTHER) ? c.file[r.in] : 0;

	if (K & K_IWT)
	{
		//NOFL of the step two before: of its row, or for the first two steps of the loop what the state had
		const uint32_t raw = &r >= c.settled ? r.raw : (c.nofl_before >> (c.settled - &r - 1)) & 1;

		c.file[F_MEMS + r.iwa] = raw ? dsp_sext((int32_t)(c.stored << 8), 24) : dsp_unpack(c.stored);
	}

	if (K & K_READ)
	{
		const uint32_t addr = run_mem_addr(c, r);

		if (!taken)
			c.latch = (uint32_t)(c.ram[addr] | (c.ram[addr + 1] << 8));
	}

	if (K & K_OTHER)
	{
		const uint32_t flags = r.flags;
		const int32_t shifted = dsp_shifter(c.acc, r.shift);

		if (flags & P_YRL)
		{
			c.file[F_YHIGH] = dsp_sext(inputs >> 11, 13);
			c.file[F_YLOW] = (inputs >> 4) & 0xFFF;
			c.d->Y_REG = inputs;
		}

		if (flags & P_TWT)
			c.file[F_TEMP + r.twa] = shifted;

		if (flags & P_FRCL)
		{
			c.d->FRC_REG = (uint16_t)(r.shift == 3 ? shifted & 0xFFF : (shifted >> 11) & 0x1FFF);
			c.file[F_FRC] = dsp_sext(c.d->FRC_REG, 13);
		}

		if (flags & P_MWT)
		{
			const uint32_t addr = run_mem_addr(c, r);

			if (!taken)
			{
				const uint32_t word = (flags & P_NOFL) ? (uint16_t)(shifted >> 8) : AicaDspPack(shifted);

				c.ram[addr] = (uint8_t)word;
				c.ram[addr + 1] = (uint8_t)(word >> 8);
			}
		}

		if (flags & P_ADRL)
			c.adrs = (uint32_t)(r.shift == 3 ? (shifted >> 12) & 0xFFF : (inputs >> 16) & 0xFFF);

		if (flags & P_EWT)
			c.d->EFREG[r.ewa] = (int16_t)(shifted >> 8);
	}

	if (K & K_ODD)
		c.copy = c.latch;

	c.acc = acc_next;
}

#define RUN_KIND(k) case k: run_step<k>(c, *r); break;
#define RUN_KINDS_4(k) RUN_KIND(k) RUN_KIND(k + 1) RUN_KIND(k + 2) RUN_KIND(k + 3)
#define RUN_KINDS_16(k) RUN_KINDS_4(k) RUN_KINDS_4(k + 4) RUN_KINDS_4(k + 8) RUN_KINDS_4(k + 12)

static void plan_build(AicaDsp& d)
{
	AicaDspPlan& plan = d.plan;
	uint32_t empty_from = 0;

	for (uint32_t s = 0; s < AICA_DSP_STEPS; s++)
	{
		AicaDspPlan::Row& r = plan.row[s];
		AicaDspInst i;
		uint32_t any = 0;

		for (uint32_t k = 0; k < 4; k++)
		{
			plan.mpro[s][k] = d.MPRO[s][k];
			any |= d.MPRO[s][k];
		}

		if (any)
			empty_from = s + 1;

		AicaDspDecode(d.MPRO[s], &i);

		const uint32_t in = i.IRA <= 0x1F ? F_MEMS + i.IRA : i.IRA <= 0x2F ? F_MIXS + (i.IRA - 0x20)
			: i.IRA <= 0x31 ? F_EXTS + (i.IRA - 0x30) : F_ZERO;

		r.in = (uint16_t)in;
		r.x = (uint16_t)(i.XSEL ? in : F_TEMP + i.TRA);
		r.y = (uint16_t)(i.YSEL == 0 ? (uint32_t)F_FRC : i.YSEL == 1 ? F_COEF + s : i.YSEL == 2 ? (uint32_t)F_YHIGH : (uint32_t)F_YLOW);
		r.b = (uint16_t)(i.ZERO || i.BSEL ? F_ZERO : F_TEMP + i.TRA);

		r.flags = (uint16_t)((i.YRL ? P_YRL : 0) | (i.TWT ? P_TWT : 0) | (i.FRCL ? P_FRCL : 0) | (i.MWT ? P_MWT : 0)
			| (i.ADRL ? P_ADRL : 0) | (i.EWT ? P_EWT : 0) | (i.ADREB ? P_ADREB : 0) | (i.NOFL ? P_NOFL : 0));

		r.twa = i.TWA;
		r.iwa = i.IWA;
		r.ewa = i.EWA;
		r.masa = i.MASA;
		r.shift = i.SHIFT;
		r.nofl = i.NOFL;
		r.raw = s >= 2 ? (uint8_t)((d.MPRO[s - 2][3] >> 15) & 1) : 0;
		r.table = i.TABLE;
		r.nxadr = i.NXADR;
		r.channel = (uint8_t)(((s + 14) >> 1) & 63);

		r.kind = (uint8_t)((s & 1 ? K_ODD : 0) | (!i.ZERO && i.BSEL ? K_BACC : 0) | (i.NEGB && !i.ZERO ? K_NEG : 0)
			| (i.IWT ? K_IWT : 0) | (i.MRD && !i.MWT ? K_READ : 0)
			| (i.MWT || i.YRL || i.TWT || i.FRCL || i.ADRL || i.EWT ? K_OTHER : 0));
	}

	plan.empty_from = (uint8_t)empty_from;
	plan.valid = 1;
}

//The plan is the one of the program that is in MPRO now. Looked at before every sample: MPRO is a field anybody
//can write. One flat loop without an early exit, which compilers turn into a few vector compares
static inline bool plan_current(const AicaDsp& d)
{
	const uint16_t* was = &d.plan.mpro[0][0];
	const uint16_t* is = &d.MPRO[0][0];
	uint16_t differs = 0;

	for (uint32_t i = 0; i < AICA_DSP_STEPS * 4; i++)
		differs |= (uint16_t)(was[i] ^ is[i]);

	return d.plan.valid && differs == 0;
}

/*
	Programs end early: behind their last step the rows of MPRO are 0 up to step 127. An empty row writes nothing
	and reads nothing but its two operands: ACC = TEMP[MDEC_CT] * FRC_REG / 4096 + TEMP[MDEC_CT], whatever ACC
	was. So of a run of empty rows up to the end of the sample only the last one leaves anything in ACC, the NOFL
	of the last two steps is 0, and the read latch goes on being copied at the odd steps. That is all such a run
	is executed as.
*/
void AicaDsp::RunSample(const AicaDspSampleInputs* in)
{
	//Without wave RAM, and with a channel that took the slot of the next step by a call: the way StepOne has it
	if (!ram || (slot_taken & 1))
	{
		do
			StepOne(in);
		while (step != 0 && (!ram || (slot_taken & 1)));

		if (step == 0)
			return;
	}

	uint32_t s = step & 127;

	if (s == 0)
		sample_begin(*this, in);

	if (!plan_current(*this))
		plan_build(*this);

	//the run of empty rows at the end is taken as one only when it has the two steps that empty the NOFL history
	uint32_t stop = plan.empty_from > s ? plan.empty_from : s;

	if (stop > AICA_DSP_STEPS - 2)
		stop = AICA_DSP_STEPS;

	const uint32_t turn = MDEC_CT & 0x7F;
	int32_t file[F_SIZE];

	//TEMP is cut to its 24 bit where it is, in a loop of a fixed length: all of it comes back from the file anyway
	for (int32_t& v : TEMP)
		v = dsp_sext(v, 24);

	for (uint32_t i = 0; i < 128 - turn; i++)
		file[F_TEMP + i] = TEMP[turn + i];

	for (uint32_t i = 0; i < turn; i++)
		file[F_TEMP + 128 - turn + i] = TEMP[i];

	for (uint32_t i = 0; i < 32; i++)
		file[F_MEMS + i] = dsp_sext(MEMS[i], 24);

	for (uint32_t i = 0; i < 16; i++)
		file[F_MIXS + i] = dsp_sext((int32_t)((uint32_t)MIXS[mixs_dsp & 1][i] << 4), 24);

	file[F_EXTS] = EXTS[0] * 256;
	file[F_EXTS + 1] = EXTS[1] * 256;
	file[F_ZERO] = 0;
	file[F_FRC] = dsp_sext(FRC_REG, 13);
	file[F_YHIGH] = dsp_sext(Y_REG >> 11, 13);
	file[F_YLOW] = (Y_REG >> 4) & 0xFFF;

	for (uint32_t i = 0; i < 128; i++)
		file[F_COEF + i] = dsp_sext(COEF[i] >> 3, 13);

	//the channels by the step of their slot: channel K has step 2K - 14, so step s has channel s / 2 + 7. Then
	//the even steps that are done already are taken off
	const uint64_t by_channel = in ? in->fetch : 0;
	const uint64_t by_step = (by_channel >> 7) | (by_channel << 57);
	const uint32_t ring_mask = (8192u << ((RBPL >> 13) & 3)) - 1;

	Run c;

	c.d = this;
	c.ram = ram;
	c.file = file;
	c.fetch_word = in ? in->fetch_word : nullptr;
	c.fetch = s < AICA_DSP_STEPS - 1 ? by_step >> ((s + 1) >> 1) : 0;
	c.acc = dsp_sext(ACC, 26);
	c.adrs = ADRS_REG;
	c.latch = latch;
	c.copy = latch_copy;
	c.stored = memval;
	c.settled = &plan.row[s] + 2;
	c.nofl_before = nofl_pipe;
	c.add_ct[0] = MDEC_CT;
	c.add_ct[1] = 0;
	c.wrap[0] = ring_mask;
	c.wrap[1] = 0xFFFF;
	c.ring_base = (RBPL & 0xFFFu) * 2048u;
	c.ram_even = ram_mask & ~1u;

	const uint32_t first = s;
	const AicaDspPlan::Row* const end = &plan.row[stop];

	for (const AicaDspPlan::Row* r = &plan.row[s]; r != end; r++)
	{
		switch (r->kind & (K_COUNT - 1))
		{
		RUN_KINDS_16(0)
		RUN_KINDS_16(16)
		RUN_KINDS_16(32)
		RUN_KINDS_16(48)
		}
	}

	if (stop < AICA_DSP_STEPS)
	{
		//The empty rows up to step 127, two or more. The latch takes the fetches of their slots, and each odd step
		//passes it on
		for (s = stop; s < AICA_DSP_STEPS; s++)
		{
			//no channel fetches from here on, and the last two odd steps are still to come: they leave the latch
			//in its copy and in what IWT stores
			if (s <= AICA_DSP_STEPS - 3 && c.fetch == 0)
			{
				c.stored = c.copy = c.latch;
				break;
			}

			if (s & 1)
			{
				c.stored = c.copy;
				c.copy = c.latch;
			}
			else
			{
				if (c.fetch & 1)
					c.latch = c.fetch_word[((s + 14) >> 1) & 63];

				c.fetch >>= 1;
			}
		}

		const int32_t temp = file[F_TEMP];

		c.acc = dsp_sext((int32_t)(((int64_t)temp * file[F_FRC]) >> 12) + temp, 26);
		nofl_pipe = 0;
	}
	else if (first <= AICA_DSP_STEPS - 2)
		nofl_pipe = (uint8_t)((plan.row[AICA_DSP_STEPS - 2].nofl << 1) | plan.row[AICA_DSP_STEPS - 1].nofl);
	else
		nofl_pipe = (uint8_t)(((nofl_pipe << 1) | plan.row[AICA_DSP_STEPS - 1].nofl) & 3);

	for (uint32_t i = 0; i < 128 - turn; i++)
		TEMP[turn + i] = file[F_TEMP + i];

	for (uint32_t i = 0; i < turn; i++)
		TEMP[i] = file[F_TEMP + 128 - turn + i];

	for (uint32_t i = 0; i < 32; i++)
		MEMS[i] = file[F_MEMS + i];

	ACC = c.acc;
	ADRS_REG = (uint16_t)c.adrs;
	latch = (uint16_t)c.latch;
	latch_copy = (uint16_t)c.copy;
	memval = (uint16_t)c.stored;

	sample_end(*this);
}


/*
	The state blob: "ADSP", version (u16), size (u16), then every field of the state in the order of the struct,
	each little endian in its own width.
*/
#define AICA_DSP_STATE_VERSION 1

namespace
{
	struct BlobWriter
	{
		uint8_t* p;

		void u8(uint32_t v) { *p++ = (uint8_t)v; }
		void u16(uint32_t v) { u8(v); u8(v >> 8); }
		void u32(uint32_t v) { u16(v); u16(v >> 16); }
	};

	struct BlobReader
	{
		const uint8_t* p;

		uint32_t u8() { return *p++; }
		uint32_t u16() { const uint32_t lo = u8(); return lo | (u8() << 8); }
		uint32_t u32() { const uint32_t lo = u16(); return lo | (u16() << 16); }
	};
}

uint32_t AicaDsp::StateSize()
{
	return 8							//header
		+ 2 * (128 + 64 + 128 * 4)		//COEF, MADRS, MPRO
		+ 4 * (128 + 32 + 2 * 16)		//TEMP, MEMS, MIXS
		+ 2 * (16 + 2)					//EFREG, EXTS
		+ 2 + 2							//RBPL, MDEC_CT
		+ 4 + 4 + 2 + 2					//ACC, Y_REG, FRC_REG, ADRS_REG
		+ 2 + 2 + 2 + 1					//latch, latch_copy, memval, nofl_pipe
		+ 1 + 1 + 1 + 1 + 1				//step, slot_taken, mixs_dsp, mixs_fill, mixs_swept
		+ 2 * 2 + 4;					//mixs_sent, samples
}

void AicaDsp::SaveState(uint8_t* blob) const
{
	BlobWriter w = { blob };

	w.u8('A'); w.u8('D'); w.u8('S'); w.u8('P');
	w.u16(AICA_DSP_STATE_VERSION);
	w.u16(StateSize());

	for (uint16_t v : COEF) w.u16(v);
	for (uint16_t v : MADRS) w.u16(v);
	for (const auto& row : MPRO) for (uint16_t v : row) w.u16(v);
	for (int32_t v : TEMP) w.u32((uint32_t)v);
	for (int32_t v : MEMS) w.u32((uint32_t)v);
	for (const auto& bank : MIXS) for (int32_t v : bank) w.u32((uint32_t)v);
	for (int16_t v : EFREG) w.u16((uint16_t)v);
	for (int16_t v : EXTS) w.u16((uint16_t)v);
	w.u16(RBPL);
	w.u16(MDEC_CT);

	w.u32((uint32_t)ACC);
	w.u32((uint32_t)Y_REG);
	w.u16(FRC_REG);
	w.u16(ADRS_REG);
	w.u16(latch);
	w.u16(latch_copy);
	w.u16(memval);
	w.u8(nofl_pipe);

	w.u8(step);
	w.u8(slot_taken);
	w.u8(mixs_dsp);
	w.u8(mixs_fill);
	w.u8(mixs_swept);
	w.u16(mixs_sent[0]);
	w.u16(mixs_sent[1]);
	w.u32(samples);
}

bool AicaDsp::LoadState(const uint8_t* blob, uint32_t size)
{
	if (size < 8 || blob[0] != 'A' || blob[1] != 'D' || blob[2] != 'S' || blob[3] != 'P')
		return false;

	BlobReader r = { blob + 4 };

	if (r.u16() != AICA_DSP_STATE_VERSION || r.u16() != StateSize() || size != StateSize())
		return false;

	//Every field is cut to the bits the hardware has, so no blob can leave a value the datapath cannot hold
	for (uint16_t& v : COEF) v = (uint16_t)(r.u16() & 0xFFF8);
	for (uint16_t& v : MADRS) v = (uint16_t)r.u16();
	for (auto& row : MPRO) for (uint16_t& v : row) v = (uint16_t)r.u16();
	for (int32_t& v : TEMP) v = dsp_sext((int32_t)r.u32(), 24);
	for (int32_t& v : MEMS) v = dsp_sext((int32_t)r.u32(), 24);
	for (auto& bank : MIXS) for (int32_t& v : bank) v = dsp_sext((int32_t)r.u32(), 20);
	for (int16_t& v : EFREG) v = (int16_t)r.u16();
	for (int16_t& v : EXTS) v = (int16_t)r.u16();
	RBPL = (uint16_t)r.u16();
	MDEC_CT = (uint16_t)r.u16();

	ACC = dsp_sext((int32_t)r.u32(), 26);
	Y_REG = dsp_sext((int32_t)r.u32(), 24);
	FRC_REG = (uint16_t)(r.u16() & 0x1FFF);
	ADRS_REG = (uint16_t)(r.u16() & 0xFFF);
	latch = (uint16_t)r.u16();
	latch_copy = (uint16_t)r.u16();
	memval = (uint16_t)r.u16();
	nofl_pipe = (uint8_t)(r.u8() & 3);

	step = (uint8_t)(r.u8() & 127);
	slot_taken = (uint8_t)(r.u8() & 1);
	mixs_dsp = (uint8_t)(r.u8() & 1);
	mixs_fill = (uint8_t)(r.u8() & 1);
	mixs_swept = (uint8_t)(r.u8() & 1);
	mixs_sent[0] = (uint16_t)r.u16();
	mixs_sent[1] = (uint16_t)r.u16();
	samples = r.u32();

	plan.valid = 0;

	return true;
}
