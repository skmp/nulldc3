/*
	The known-answer programs of the AICA DSP core: dsp_known.h has what they are and where they run.

	How the programs get at the datapath, with nothing but the CPU's registers:

	  a number into ACC    a row with nothing but TRA leaves TEMP[TRA + MDEC_CT] in ACC: its Y is FRC_REG, which is 0,
	                       and its B is TEMP. The CPU writes TEMP
	  ACC - TEMP           a row with BSEL and YSEL 1 on a COEF of -4096: X * Y / 4096 is -TEMP exactly
	  ACC out              the row behind it shifts it: TWT puts SHIFTED into TEMP, which the CPU reads
	  a word to the RAM    MWT with NOFL writes SHIFTED[23:8]
	  a word from the RAM  MRD with NOFL in an odd step, IWT two steps on: MEMS has the word << 8, the CPU reads it
*/
#include "dsp_known.h"
#include "hw/aica/dsp_core/wasm/aica_dsp_capi.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

static uint32_t cut(uint32_t value, int bits)
{
	return value & ((1u << bits) - 1);
}

static int32_t sign_extended(uint32_t value, int bits)
{
	const uint32_t sign = 1u << (bits - 1);

	return (int32_t)((cut(value, bits) ^ sign) - sign);
}

void dsp_known_set(AicaDsp& dsp, uint32_t index, uint32_t value)
{
	switch (index)
	{
	case AICA_DSP_REG_STEP: dsp.step = (uint8_t)(value % 128); break;
	case AICA_DSP_REG_SAMPLES: dsp.samples = value; break;
	case AICA_DSP_REG_MDEC_CT: dsp.MDEC_CT = (uint16_t)value; break;
	case AICA_DSP_REG_RBPL: dsp.RBPL = (uint16_t)value; break;
	case AICA_DSP_REG_ACC: dsp.ACC = sign_extended(value, 26); break;
	case AICA_DSP_REG_Y_REG: dsp.Y_REG = sign_extended(value, 24); break;
	case AICA_DSP_REG_FRC_REG: dsp.FRC_REG = (uint16_t)(value % 0x2000); break;
	case AICA_DSP_REG_ADRS_REG: dsp.ADRS_REG = (uint16_t)(value % 0x1000); break;
	case AICA_DSP_REG_LATCH: dsp.latch = (uint16_t)value; break;
	case AICA_DSP_REG_LATCH_COPY: dsp.latch_copy = (uint16_t)value; break;
	case AICA_DSP_REG_MEMVAL: dsp.memval = (uint16_t)value; break;
	case AICA_DSP_REG_NOFL_PIPE: dsp.nofl_pipe = (uint8_t)(value % 4); break;
	case AICA_DSP_REG_MIXS_DSP: dsp.mixs_dsp = (uint8_t)(value % 2); break;
	case AICA_DSP_REG_MIXS_FILL: dsp.mixs_fill = (uint8_t)(value % 2); break;
	default: break;
	}
}

uint32_t dsp_known_get(const AicaDsp& dsp, uint32_t index)
{
	switch (index)
	{
	case AICA_DSP_REG_STEP: return dsp.step;
	case AICA_DSP_REG_SAMPLES: return dsp.samples;
	case AICA_DSP_REG_MDEC_CT: return dsp.MDEC_CT;
	case AICA_DSP_REG_RBPL: return dsp.RBPL;
	case AICA_DSP_REG_ACC: return (uint32_t)dsp.ACC;
	case AICA_DSP_REG_Y_REG: return (uint32_t)dsp.Y_REG;
	case AICA_DSP_REG_FRC_REG: return dsp.FRC_REG;
	case AICA_DSP_REG_ADRS_REG: return dsp.ADRS_REG;
	case AICA_DSP_REG_LATCH: return dsp.latch;
	case AICA_DSP_REG_LATCH_COPY: return dsp.latch_copy;
	case AICA_DSP_REG_MEMVAL: return dsp.memval;
	case AICA_DSP_REG_NOFL_PIPE: return dsp.nofl_pipe;
	case AICA_DSP_REG_MIXS_DSP: return dsp.mixs_dsp;
	case AICA_DSP_REG_MIXS_FILL: return dsp.mixs_fill;
	default: return 0;
	}
}


static KnownMachine* m;
static bool single;		//the samples are executed step by step
static uint32_t done;	//the steps of the running sample that are executed

static void create(uint32_t ram_size)
{
	m->Create(ram_size);
	done = 0;
}

static void reset()
{
	m->Reset();
	done = 0;
}

//The running sample up to the front of `step`, by single steps in both ways: for an event in front of that step
static void steps_to(uint32_t step)
{
	if (step > done)
	{
		m->Steps(step - done);
		done = step;
	}
}

//The rest of the running sample
static void sample()
{
	if (single)
		m->Steps(128 - done);
	else
		m->RunSample();

	done = 0;
}

static void check(bool holds, const char* fmt, ...)
{
	char what[256];
	va_list args;

	va_start(args, fmt);
	vsnprintf(what, sizeof(what), fmt, args);
	va_end(args);

	m->Check(holds, what);
}

static AicaDspInst nop()
{
	AicaDspInst inst;

	memset(&inst, 0, sizeof(inst));

	return inst;
}

static void put(int step, const AicaDspInst& inst)
{
	uint16_t w[4];

	AicaDspEncode(&inst, w);

	for (int k = 0; k < 4; k++)
		m->RegWrite((uint32_t)(0x3400 + 16 * step + 4 * k), w[k]);
}

//TEMP and MEMS as the CPU has them: 24 bit in two registers
static void temp_write(int index, int32_t value)
{
	m->RegWrite((uint32_t)(0x4000 + 8 * index), (uint32_t)value & 0xFF);
	m->RegWrite((uint32_t)(0x4004 + 8 * index), ((uint32_t)value >> 8) & 0xFFFF);
}

static uint32_t temp_read(int index)
{
	const uint32_t low = m->RegRead((uint32_t)(0x4000 + 8 * index));

	return low | (m->RegRead((uint32_t)(0x4004 + 8 * index)) << 8);
}

static uint32_t mems_read(int index)
{
	const uint32_t low = m->RegRead((uint32_t)(0x4400 + 8 * index));

	return low | (m->RegRead((uint32_t)(0x4404 + 8 * index)) << 8);
}


/*
	The shifter. ACC is t[0] - t[1] - t[2] - t[3] - t[4] in 26 bit, and SHIFTED is

	  SHIFT 0   ACC, saturated to 24 bit: 0x7FFFFF above, -0x800000 below
	  SHIFT 1   twice ACC, saturated the same way
	  SHIFT 2   twice ACC, its low 24 bit
	  SHIFT 3   ACC, its low 24 bit
*/
static const struct
{
	int32_t t[5];
	int32_t acc;
	int32_t shifted[4];
}
shifts[] =
{
	//inside 24 bit, twice the value too
	{ { 0x123456, 0, 0, 0, 0 }, 0x123456, { 0x123456, 0x2468AC, 0x2468AC, 0x123456 } },
	{ { -0x123456, 0, 0, 0, 0 }, -0x123456, { -0x123456, -0x2468AC, -0x2468AC, -0x123456 } },
	{ { 1, 0, 0, 0, 0 }, 1, { 1, 2, 2, 1 } },
	{ { -1, 0, 0, 0, 0 }, -1, { -1, -2, -2, -1 } },
	{ { 0x3FFFFF, 0, 0, 0, 0 }, 0x3FFFFF, { 0x3FFFFF, 0x7FFFFE, 0x7FFFFE, 0x3FFFFF } },
	{ { -0x400000, 0, 0, 0, 0 }, -0x400000, { -0x400000, -0x800000, -0x800000, -0x400000 } },

	//twice the value is beyond 24 bit: SHIFT 1 saturates, SHIFT 2 cuts
	{ { 0x400000, 0, 0, 0, 0 }, 0x400000, { 0x400000, 0x7FFFFF, -0x800000, 0x400000 } },
	{ { -0x400001, 0, 0, 0, 0 }, -0x400001, { -0x400001, -0x800000, 0x7FFFFE, -0x400001 } },
	{ { 0x7FFFFF, 0, 0, 0, 0 }, 0x7FFFFF, { 0x7FFFFF, 0x7FFFFF, -2, 0x7FFFFF } },
	{ { -0x800000, 0, 0, 0, 0 }, -0x800000, { -0x800000, -0x800000, 0, -0x800000 } },

	//the value is beyond 24 bit: SHIFT 0 saturates, SHIFT 3 cuts
	{ { 0x7FFFFF, -1, 0, 0, 0 }, 0x800000, { 0x7FFFFF, 0x7FFFFF, 0, -0x800000 } },
	{ { -0x800000, 1, 0, 0, 0 }, -0x800001, { -0x800000, -0x800000, -2, 0x7FFFFF } },
	{ { 0x7FFFFF, -0x100001, 0, 0, 0 }, 0x900000, { 0x7FFFFF, 0x7FFFFF, 0x200000, -0x700000 } },
	{ { -0x800000, 0x100000, 0, 0, 0 }, -0x900000, { -0x800000, -0x800000, -0x200000, 0x700000 } },

	//the ends of 26 bit
	{ { 0x7FFFFF, -0x800000, -0x800000, -0x800000, 0 }, 0x1FFFFFF, { 0x7FFFFF, 0x7FFFFF, -2, -1 } },
	{ { -0x800000, 0x7FFFFF, 0x7FFFFF, 0x7FFFFF, 3 }, -0x2000000, { -0x800000, -0x800000, 0, 0 } },

	//and beyond them: ACC wraps
	{ { 0x7FFFFF, -0x800000, -0x800000, -0x800000, -0x800000 }, -0x1800001, { -0x800000, -0x800000, -2, 0x7FFFFF } },
	{ { -0x800000, 0x7FFFFF, 0x7FFFFF, 0x7FFFFF, 0x7FFFFF }, 0x1800004, { 0x7FFFFF, 0x7FFFFF, 8, -0x7FFFFC } },
};

static void known_shifter()
{
	m->Group("shifter");
	create(0x10000);

	//The last rows of the sample, so that ACC is still the sum behind it. Row 119: ACC = TEMP[0]. Rows 120 to 123:
	//ACC = ACC - TEMP[1 to 4]. Rows 124 to 127 keep ACC and put SHIFTED of mode 0 to 3 into TEMP[8 to 11] and into
	//EFREG[0 to 3]
	for (int k = 1; k <= 4; k++)
	{
		AicaDspInst inst = nop();

		inst.TRA = (uint8_t)k;
		inst.YSEL = 1;
		inst.BSEL = 1;
		put(119 + k, inst);
		m->RegWrite((uint32_t)(0x3000 + 4 * (119 + k)), 0x8000);
	}

	for (int mode = 0; mode < 4; mode++)
	{
		AicaDspInst inst = nop();

		inst.BSEL = 1;
		inst.SHIFT = (uint8_t)mode;
		inst.TWT = 1;
		inst.TWA = (uint8_t)(8 + mode);
		inst.EWT = 1;
		inst.EWA = (uint8_t)mode;
		put(124 + mode, inst);
	}

	for (const auto& s : shifts)
	{
		for (int k = 0; k < 5; k++)
			temp_write(k, s.t[k]);

		m->Set(AICA_DSP_REG_MDEC_CT, 0);
		sample();

		check(m->Get(AICA_DSP_REG_ACC) == (uint32_t)s.acc, "%x - %x - %x - %x - %x is ACC %x", (uint32_t)s.t[0],
			(uint32_t)s.t[1], (uint32_t)s.t[2], (uint32_t)s.t[3], (uint32_t)s.t[4], (uint32_t)s.acc);

		for (int mode = 0; mode < 4; mode++)
		{
			const uint32_t shifted = cut((uint32_t)s.shifted[mode], 24);

			check(temp_read(8 + mode) == shifted, "SHIFT %d of ACC %x is %06x", mode, (uint32_t)s.acc, shifted);
			check(m->RegRead((uint32_t)(0x4580 + 4 * mode)) == shifted >> 8, "EWT of it is %04x", shifted >> 8);
		}
	}

	check(m->Get(AICA_DSP_REG_MDEC_CT) == 0xFFFF, "MDEC_CT counts down through 0");
}


/*
	The byte of the wave RAM that a memory step has. In words:

	  TABLE 1   (MADRS + ADRS_REG + NXADR) mod 64K, MDEC_CT and RBL do not count
	  TABLE 0   (MADRS + ADRS_REG + NXADR + MDEC_CT) mod the ring's 8K << RBL

	ADRS_REG is a signed 12 bit number. The byte is RBP * 2048 + 2 * the word, in the wave RAM as large as it is:
	RBP is 12 bit and reaches 8 MB, a ring near the top goes on behind it. `at` is that byte in a wave RAM of 2, 4, 8
	and 16 MB.
*/
enum
{
	ADRS_NONE = 0,		//the row has no ADREB
	ADRS_SHIFTED = 1,	//ADRL with SHIFT 3: ADRS_REG = SHIFTED[23:12]
	ADRS_INPUTS = 2,	//ADRL: ADRS_REG = INPUTS[23:16], 8 bit sign extended
};

static const struct Address
{
	const char* what;
	uint32_t rbp, rbl, table, madrs;
	int how, adrs;
	uint32_t nxadr, ct;
	uint32_t at[4];
}
addresses[] =
{
	//TABLE, with the ring at 0x020000 and 8K words long
	{ "TABLE: word 0", 0x040, 0, 1, 0x0000, ADRS_NONE, 0, 0, 0x1234, { 0x020000, 0x020000, 0x020000, 0x020000 } },
	{ "TABLE: word 0xFFFF", 0x040, 0, 1, 0xFFFF, ADRS_NONE, 0, 0, 0x1234, { 0x03FFFE, 0x03FFFE, 0x03FFFE, 0x03FFFE } },
	{ "TABLE: 0xFFFF + NXADR is word 0", 0x040, 0, 1, 0xFFFF, ADRS_NONE, 0, 1, 0x1234, { 0x020000, 0x020000, 0x020000, 0x020000 } },
	{ "TABLE: 0xFFC0 + ADRS_REG 0x7F is word 0x003F", 0x040, 0, 1, 0xFFC0, ADRS_INPUTS, 0x7F, 0, 0x1234, { 0x02007E, 0x02007E, 0x02007E, 0x02007E } },
	{ "TABLE: 0xF801 + ADRS_REG 0x7FF is word 0", 0x040, 0, 1, 0xF801, ADRS_SHIFTED, 0x7FF, 0, 0x1234, { 0x020000, 0x020000, 0x020000, 0x020000 } },
	{ "TABLE: 1 + ADRS_REG -2 is word 0xFFFF", 0x040, 0, 1, 0x0001, ADRS_INPUTS, -2, 0, 0x1234, { 0x03FFFE, 0x03FFFE, 0x03FFFE, 0x03FFFE } },
	{ "TABLE: 0x07FF + ADRS_REG -0x800 is word 0xFFFF", 0x040, 0, 1, 0x07FF, ADRS_SHIFTED, -0x800, 0, 0x1234, { 0x03FFFE, 0x03FFFE, 0x03FFFE, 0x03FFFE } },
	{ "TABLE: 0x10 + ADRS_REG -2 + NXADR is word 0x000F", 0x040, 0, 1, 0x0010, ADRS_INPUTS, -2, 1, 0x1234, { 0x02001E, 0x02001E, 0x02001E, 0x02001E } },
	{ "TABLE: word 0x8000 with a ring of 8K", 0x000, 0, 1, 0x8000, ADRS_NONE, 0, 0, 0x1234, { 0x010000, 0x010000, 0x010000, 0x010000 } },

	//the ring, at 0x000800
	{ "ring of 8K: its last word", 0x001, 0, 0, 0x1FFF, ADRS_NONE, 0, 0, 0x0000, { 0x0047FE, 0x0047FE, 0x0047FE, 0x0047FE } },
	{ "ring of 8K: behind its last word is word 0", 0x001, 0, 0, 0x1FFF, ADRS_NONE, 0, 1, 0x0000, { 0x000800, 0x000800, 0x000800, 0x000800 } },
	{ "ring of 8K: MDEC_CT 0xFFFF is its last word", 0x001, 0, 0, 0x0000, ADRS_NONE, 0, 0, 0xFFFF, { 0x0047FE, 0x0047FE, 0x0047FE, 0x0047FE } },
	{ "ring of 16K: MDEC_CT 0xFFFF is its last word", 0x001, 1, 0, 0x0000, ADRS_NONE, 0, 0, 0xFFFF, { 0x0087FE, 0x0087FE, 0x0087FE, 0x0087FE } },
	{ "ring of 32K: MDEC_CT 0xFFFF is its last word", 0x001, 2, 0, 0x0000, ADRS_NONE, 0, 0, 0xFFFF, { 0x0107FE, 0x0107FE, 0x0107FE, 0x0107FE } },
	{ "ring of 64K: MDEC_CT 0xFFFF is its last word", 0x001, 3, 0, 0x0000, ADRS_NONE, 0, 0, 0xFFFF, { 0x0207FE, 0x0207FE, 0x0207FE, 0x0207FE } },
	{ "ring of 64K: 0x9000 + MDEC_CT 0x9000 is word 0x2000", 0x001, 3, 0, 0x9000, ADRS_NONE, 0, 0, 0x9000, { 0x004800, 0x004800, 0x004800, 0x004800 } },
	{ "ring of 16K: 0x4005 + ADRS_REG -2 + MDEC_CT 3 is word 6", 0x001, 1, 0, 0x4005, ADRS_INPUTS, -2, 0, 0x0003, { 0x00080C, 0x00080C, 0x00080C, 0x00080C } },

	//RBP, and the end of the wave RAM
	{ "RBP 0x3FF: the last word of 2 MB", 0x3FF, 0, 0, 0x03FF, ADRS_NONE, 0, 0, 0x0000, { 0x1FFFFE, 0x1FFFFE, 0x1FFFFE, 0x1FFFFE } },
	{ "RBP 0x3FF: the ring goes on behind 2 MB", 0x3FF, 0, 0, 0x03FF, ADRS_NONE, 0, 1, 0x0000, { 0x000000, 0x200000, 0x200000, 0x200000 } },
	{ "RBP 0x7FF: the ring goes on behind 4 MB", 0x7FF, 2, 0, 0x7FFF, ADRS_NONE, 0, 1, 0x0400, { 0x000000, 0x000000, 0x400000, 0x400000 } },
	{ "RBP 0x800: the ring at 4 MB", 0x800, 3, 0, 0x8000, ADRS_NONE, 0, 0, 0x8001, { 0x000002, 0x000002, 0x400002, 0x400002 } },
	{ "RBP 0xBFF: a ring of 16K at 0x5FF800", 0xBFF, 1, 0, 0x0000, ADRS_NONE, 0, 0, 0xFFFF, { 0x0077FE, 0x2077FE, 0x6077FE, 0x6077FE } },
	{ "RBP 0xC80: TABLE at 0x640000", 0xC80, 0, 1, 0x0123, ADRS_NONE, 0, 0, 0x4321, { 0x040246, 0x240246, 0x640246, 0x640246 } },
	{ "RBP 0xFFF: the last word of 8 MB", 0xFFF, 3, 0, 0x03FF, ADRS_NONE, 0, 0, 0x0000, { 0x1FFFFE, 0x3FFFFE, 0x7FFFFE, 0x7FFFFE } },
	{ "RBP 0xFFF: TABLE 0x400 + ADRS_REG -1, the same word", 0xFFF, 0, 1, 0x0400, ADRS_INPUTS, -1, 0, 0x0000, { 0x1FFFFE, 0x3FFFFE, 0x7FFFFE, 0x7FFFFE } },
	{ "RBP 0xFFF: the ring goes on behind 8 MB", 0xFFF, 3, 0, 0x03FF, ADRS_NONE, 0, 1, 0x0000, { 0x000000, 0x000000, 0x000000, 0x800000 } },
	{ "RBP 0xFFF: the last word of a ring of 64K", 0xFFF, 3, 0, 0xFFFE, ADRS_NONE, 0, 0, 0x0001, { 0x01F7FE, 0x01F7FE, 0x01F7FE, 0x81F7FE } },
	{ "RBP 0xFFF: the last word of TABLE", 0xFFF, 0, 1, 0xFFFF, ADRS_NONE, 0, 0, 0x0000, { 0x01F7FE, 0x01F7FE, 0x01F7FE, 0x81F7FE } },
};

#define KNOWN_WRITTEN 0xA5C3u		//the word the MWT writes
#define KNOWN_PLANTED 0x5A17u		//the word that is where the MRD has to read

static void known_address(int size_index)
{
	const uint32_t ram_size = (2u << 20) << size_index;
	const uint32_t megabytes = ram_size >> 20;

	create(ram_size);

	//rows 1 to 3 load ADRS_REG and leave the word to write in ACC, row 4 writes, row 5 reads, row 7 stores what was
	//read in MEMS[2]. TEMP[10] is what ADRS_REG is shifted out of, TEMP[11] the word to write, MEMS[1] the input
	//with ADRS_REG in bits 23:16
	temp_write(11, (int32_t)(KNOWN_WRITTEN << 8));

	for (const Address& a : addresses)
	{
		const uint32_t at = a.at[size_index];
		const uint8_t to_adrs = (uint8_t)((10 - a.ct) & 127), to_word = (uint8_t)((11 - a.ct) & 127);
		AicaDspInst inst;

		m->RegWrite(0x2804, a.rbp | (a.rbl << 13));
		m->RegWrite(0x3200 + 4 * 3, a.madrs);
		temp_write(10, a.how == ADRS_SHIFTED ? a.adrs * 4096 : 0);
		m->RegWrite(0x4404 + 8 * 1, a.how == ADRS_INPUTS ? ((uint32_t)a.adrs & 0xFF) << 8 : 0);

		inst = nop();
		inst.TRA = to_adrs;
		put(1, inst);

		inst = nop();
		inst.TRA = to_word;
		inst.IRA = 1;
		inst.ADRL = a.how != ADRS_NONE;
		inst.SHIFT = a.how == ADRS_SHIFTED ? 3 : 0;
		put(2, inst);

		inst.ADRL = 0;
		inst.SHIFT = 0;
		put(3, inst);

		AicaDspInst access = nop();

		access.TRA = to_word;
		access.NOFL = 1;
		access.TABLE = (uint8_t)a.table;
		access.MASA = 3;
		access.ADREB = a.how != ADRS_NONE;
		access.NXADR = (uint8_t)a.nxadr;

		inst = access;
		inst.MWT = 1;
		put(4, inst);
		put(5, nop());
		put(7, nop());

		m->Set(AICA_DSP_REG_MDEC_CT, a.ct);
		sample();

		check(m->RamRead(at) == KNOWN_WRITTEN && m->RamUsed() == 2, "%u MB, %s: MWT writes byte %06x, and no other", megabytes, a.what, at);
		m->RamWrite(at, 0);

		//the word to read, and other words where an address that is cut short, or not cut, would read
		const uint32_t others[5] = { at ^ 0x200000, at ^ 0x400000, at ^ 0x800000, (at + 0x20000) & (ram_size - 1), (at - 0x20000) & (ram_size - 1) };

		for (uint32_t other : others)
		{
			if (other < ram_size)
				m->RamWrite(other, 0xD0C0);
		}

		m->RamWrite(at, KNOWN_PLANTED);

		put(4, nop());
		inst = access;
		inst.MRD = 1;
		put(5, inst);

		inst = nop();
		inst.IWT = 1;
		inst.IWA = 2;
		put(7, inst);

		m->Set(AICA_DSP_REG_MDEC_CT, a.ct);
		sample();

		check(mems_read(2) == KNOWN_PLANTED << 8, "%u MB, %s: MRD reads byte %06x", megabytes, a.what, at);

		for (uint32_t other : others)
		{
			if (other < ram_size)
				m->RamWrite(other, 0);
		}

		m->RamWrite(at, 0);
	}

	check(m->RamUsed() == 0, "%u MB: no step has written anything else", megabytes);
}


/*
	The ring as what it is for, a delay. The program writes a word at MADRS[0] and reads at MADRS[1] = MADRS[0] + 1 and
	at MADRS[2] = MADRS[0] + 2. MDEC_CT counts down once a sample, so the word a sample writes is the one the next
	sample reads at MADRS[1] and the one after that at MADRS[2].
*/
static void known_ring()
{
	m->Group("ring");
	create(2u << 20);

	//a ring of 8K words at byte 0x8000
	m->RegWrite(0x2804, 0x010);
	m->RegWrite(0x3200 + 4 * 0, 0x0100);
	m->RegWrite(0x3200 + 4 * 1, 0x0101);
	m->RegWrite(0x3200 + 4 * 2, 0x0102);

	//the row in front of the write leaves TEMP[MDEC_CT] in ACC: the four samples write these, with MDEC_CT 2, 1, 0
	//and 0xFFFF
	static const uint32_t words[4] = { 0x1110, 0x2220, 0x3330, 0x4440 };

	temp_write(2, (int32_t)(words[0] << 8));
	temp_write(1, (int32_t)(words[1] << 8));
	temp_write(0, (int32_t)(words[2] << 8));
	temp_write(127, (int32_t)(words[3] << 8));

	AicaDspInst inst = nop();

	inst.MWT = 1;
	inst.NOFL = 1;
	inst.MASA = 0;
	put(4, inst);

	inst = nop();
	inst.MRD = 1;
	inst.NOFL = 1;
	inst.MASA = 1;
	put(5, inst);
	inst.MASA = 2;
	put(9, inst);

	inst = nop();
	inst.IWT = 1;
	inst.IWA = 2;
	put(7, inst);
	inst.IWA = 3;
	put(11, inst);

	m->Set(AICA_DSP_REG_MDEC_CT, 2);

	//what the two reads of each sample return: the words of the sample before and of the one before that
	static const uint32_t one_back[4] = { 0, 0x1110, 0x2220, 0x3330 }, two_back[4] = { 0, 0, 0x1110, 0x2220 };

	for (int n = 0; n < 4; n++)
	{
		sample();
		check(mems_read(2) == one_back[n] << 8, "sample %d reads at MADRS + 1 what the sample before wrote at MADRS", n);
		check(mems_read(3) == two_back[n] << 8, "sample %d reads at MADRS + 2 what the sample two before wrote", n);
	}

	//with MDEC_CT 2, 1, 0 and 0xFFFF the writes went to words 0x102, 0x101, 0x100 and 0x0FF of the ring
	check(m->RamRead(0x8204) == 0x1110 && m->RamRead(0x8202) == 0x2220 && m->RamRead(0x8200) == 0x3330 && m->RamRead(0x81FE) == 0x4440
		&& m->RamUsed() == 8, "the four words are one word apart, downwards, and nothing else is written");
	check(m->Get(AICA_DSP_REG_MDEC_CT) == 0xFFFE, "MDEC_CT is 16 bit and goes on below 0");
}


/*
	The read latch. A program of reads and IWTs with NOFL in every row: the word IWT stored is bits 23:8 of MEMS.
	Word w of the table holds 0x1000 + w.
*/
struct Rules
{
	AicaDspInst prog[128];

	Rules()
	{
		for (AicaDspInst& inst : prog)
		{
			inst = nop();
			inst.NOFL = 1;
		}

		create(0x10000);
		m->RegWrite(0x2804, 0);

		for (uint32_t w = 0; w < 64; w++)
		{
			m->RamWrite(2 * w, 0x1000 + w);
			m->RegWrite(0x3200 + 4 * w, w);
		}
	}

	void Read(int step, int word)
	{
		prog[step].MRD = 1;
		prog[step].TABLE = 1;
		prog[step].MASA = (uint8_t)word;
	}

	void Store(int step, int mems)
	{
		prog[step].IWT = 1;
		prog[step].IWA = (uint8_t)mems;
	}

	void Load()
	{
		for (int s = 0; s < 128; s++)
			put(s, prog[s]);
	}

	uint32_t Stored(int mems) const { return m->RegRead((uint32_t)(0x4404 + 8 * mems)); }
};

static void known_rules()
{
	m->Group("rules");

	{
		//an odd read is stored 2 and 3 steps on, an even one 3 and 4 steps on
		Rules r;

		r.Read(11, 5);
		r.Store(12, 0); r.Store(13, 1); r.Store(14, 2);
		r.Read(30, 6);
		r.Store(31, 3); r.Store(32, 4); r.Store(33, 5); r.Store(34, 6);
		r.Load();
		m->BusRead(0xAAAA);
		sample();
		check(r.Stored(0) == 0xAAAA && r.Stored(1) == 0x1005 && r.Stored(2) == 0x1005, "the word of a read in step 11 is what IWT stores in steps 13 and 14, not in 12");
		check(r.Stored(3) == 0x1005 && r.Stored(4) == 0x1005 && r.Stored(5) == 0x1006 && r.Stored(6) == 0x1006, "the word of a read in step 30 is what IWT stores in steps 33 and 34, not in 31 and 32");
	}

	{
		//a read in the odd step behind an even read takes its place, whoever reads
		Rules r;

		r.Read(30, 6);
		r.Read(31, 7);
		r.Store(33, 0);
		r.Read(50, 8);
		r.Store(53, 1);
		r.Read(71, 9);
		r.Store(74, 2);
		r.Load();
		steps_to(51);
		m->BusRead(0xBEEF);
		steps_to(73);
		m->BusRead(0xBEEF);
		sample();
		check(r.Stored(0) == 0x1007, "a read of the DSP in step 31 replaces the word of its read in step 30");
		check(r.Stored(1) == 0xBEEF, "a bus read in step 51 replaces the word of the read in step 50");
		check(r.Stored(2) == 0x1009, "a bus read in step 73 is too late for the read in step 71 and its IWT in step 74");
	}

	{
		//a read with a write in its step reads nothing: IWT gets what the latch held
		Rules r;

		r.Read(21, 9);
		r.prog[21].MWT = 1;
		r.Store(23, 0);
		r.Load();
		m->BusRead(0x4321);
		sample();
		check(r.Stored(0) == 0x4321, "MRD with MWT in one step reads nothing");
		check(m->RamRead(18) == 0, "and its MWT writes: SHIFTED, which is 0 here");
	}

	{
		//a channel in the slot: the DSP's write is dropped, its read returns the channel's word
		Rules r;
		const int slot = (int)AicaDsp::ChannelStep(20);

		r.Read(slot, 10);
		r.Store(slot + 3, 0);
		r.prog[slot + 2].MWT = 1;
		r.prog[slot + 2].TABLE = 1;
		r.prog[slot + 2].MASA = 12;
		r.Load();

		AicaDspSampleInputs in;

		memset(&in, 0, sizeof(in));
		in.fetch = (1ull << 20) | (1ull << 21);
		in.fetch_word[20] = 0x7777;
		in.fetch_word[21] = 0x8888;
		m->Inputs(&in);
		sample();
		check(slot == 26 && AicaDsp::ChannelStep(7) == 0 && AicaDsp::ChannelStep(6) == 126 && AicaDsp::ChannelStep(63) == 112, "channel K has the slot of step 2K - 14");
		check(r.Stored(0) == 0x7777, "a read in a channel's slot returns the channel's word");
		check(m->RamRead(24) == 0x100C, "a write in a channel's slot is dropped");

		in.fetch = 0;
		m->Inputs(&in);
		sample();
		check(r.Stored(0) == 0x100A && m->RamRead(24) == 0, "without the channels both go through");
	}

	{
		//the conversion of IWT is chosen by NOFL two steps before it
		Rules r;

		m->RamWrite(2 * 20, 0xFFFF);
		r.Read(41, 20);
		r.Store(43, 0);
		r.Store(44, 1);
		r.prog[42].NOFL = 0;
		r.Load();
		sample();
		check(mems_read(0) == 0xFFFF00 && mems_read(1) == 0xFFFFFF, "IWT in step 43 stores the word as it is (NOFL in 41), in step 44 unpacked (none in 42)");
	}

	{
		//ACC of step 127 is what step 0 shifts, EFREG is replaced, MDEC_CT counts down
		create(0x10000);

		AicaDspInst inst = nop();

		m->RegWrite(0x4404, 0x0123);
		m->RegWrite(0x3000 + 4 * 127, 0x1000 << 3);
		inst.XSEL = 1;
		inst.YSEL = 1;
		inst.ZERO = 1;
		put(127, inst);

		inst = nop();
		inst.SHIFT = 3;
		inst.EWT = 1;
		inst.EWA = 2;
		put(0, inst);
		put(1, inst);

		uint32_t ct = m->Get(AICA_DSP_REG_MDEC_CT);

		sample();
		check(m->RegRead(0x4580 + 4 * 2) == 0, "step 1 writes EFREG after step 0: the later write is what stays");
		sample();
		check(m->Get(AICA_DSP_REG_ACC) == (uint32_t)-0x012300 && ((ct - 2) & 0xFFFF) == m->Get(AICA_DSP_REG_MDEC_CT)
			&& m->Get(AICA_DSP_REG_SAMPLES) == 2, "MDEC_CT counts down once a sample");

		//only step 0 now: it has the ACC that step 127 of the sample before left, -MEMS0
		put(1, nop());
		sample();
		check(m->RegRead(0x4580 + 4 * 2) == 0xFEDD, "step 0 shifts the ACC of step 127, and a negative output is 16 bit to the CPU");

		m->RegWrite(0x2804, 0x8000);
		ct = m->Get(AICA_DSP_REG_MDEC_CT);
		sample();
		check(m->Get(AICA_DSP_REG_MDEC_CT) == (ct ^ 0x8080), "with bit 15 of 0x2804 a sample leaves MDEC_CT XOR 0x8080");
	}
}


static void known_mixs()
{
	m->Group("mixs");
	create(0x10000);

	//step 0: YRL takes INPUTS as it is, MIXS3 << 4
	AicaDspInst inst = nop();

	inst.YRL = 1;
	inst.IRA = 0x23;
	put(0, inst);

	auto dsp_reads = [&]() { sample(); return (int32_t)m->Get(AICA_DSP_REG_Y_REG); };

	m->SweepBegin();
	m->MixsSend(3, 0x100);
	check(dsp_reads() != 0x1000, "a send is not in the bank the DSP reads in the sample of its sweep");
	check(dsp_reads() == 0x1000, "the DSP reads it in the next sample, << 4");
	check(dsp_reads() != 0x1000, "and the bank of the sweep before that in the one after");

	reset();
	put(0, inst);
	m->SweepBegin();
	m->MixsSend(3, 0x100);
	m->MixsSend(3, 0x23);
	m->MixsSend(3, -0x3);
	sample();
	check(dsp_reads() == 0x1200, "the sends of a sweep to a bus add up");

	m->SweepBegin();
	m->MixsSend(3, 0x7FFFF);
	m->MixsSend(3, 1);
	sample();
	check(dsp_reads() == -0x800000, "past 20 bit the sum wraps, it does not saturate");

	m->SweepBegin();
	m->MixsSend(3, 0x12345678);
	sample();
	check(dsp_reads() == 0x456780, "a single send is cut to 20 bit as well");

	//a bus without a send keeps its value: bank after bank
	reset();
	put(0, inst);
	m->SweepBegin();
	m->MixsSend(3, 0x55);
	sample();
	m->SweepBegin();
	m->MixsSend(3, 0x66);
	sample();

	int32_t seen[4];

	for (int n = 0; n < 4; n++)
	{
		m->SweepBegin();
		m->MixsSend(4, 1);
		seen[n] = dsp_reads();
	}

	check(seen[0] == 0x660 && seen[1] == 0x550 && seen[2] == 0x660 && seen[3] == 0x550, "a bus no channel sends to keeps its value in both banks");

	//the first send of a sweep replaces what the bank had
	m->SweepBegin();
	m->MixsSend(3, 0);
	sample();
	check(dsp_reads() == 0, "a send of 0 replaces the value");

	//the last channels of a sweep send behind the start of the next one
	reset();
	put(0, inst);
	m->SweepBegin();
	m->MixsSend(3, 0x10);
	m->SweepBegin();
	m->MixsSendLate(3, 0x20);
	m->MixsSend(3, 0x700);
	check(dsp_reads() == 0x300, "a late send adds to the sweep before the running one");
	check(dsp_reads() == 0x7000, "and the running sweep is the next sample's");

	//The sample in which the DSP reads a send that is made for sample 0, the samples behind it sending 0. A send is
	//in the bank the DSP starts to read at the first step 0 behind the next SweepBegin: with the sample's inputs
	//and behind a called SweepBegin that is a sample on, without one it is the step 0 that follows the send
	auto read_in = [&](int form)
	{
		int at = -1, times = 0;

		reset();
		put(0, inst);

		for (int n = 0; n < 4; n++)
		{
			AicaDspSampleInputs in;

			memset(&in, 0, sizeof(in));
			in.mixs_sent = 1 << 3;
			in.mixs[3] = n == 0 ? 0x100 : 0;

			if (form == 0)
				m->Inputs(&in);
			else
			{
				if (form == 1)
					m->SweepBegin();

				m->MixsSend(3, in.mixs[3]);
			}

			if (dsp_reads() == 0x1000)
			{
				at = n;
				times++;
			}
		}

		m->Inputs(nullptr);

		return times == 1 ? at : -1;
	};

	check(read_in(0) == 1, "a send of a sample's inputs is read in the sample after, and in no other");
	check(read_in(1) == 1, "so is a send between SweepBegin and RunSample");
	check(read_in(2) == 0, "a send in front of a RunSample without SweepBegin is read by that RunSample");

	//the CPU has the bank the DSP reads
	reset();
	put(0, inst);
	sample();
	m->RegWrite(0x4500 + 8 * 3, 0x5);
	m->RegWrite(0x4504 + 8 * 3, 0x1234);
	check(m->RegRead(0x4504 + 8 * 3) == 0x1234 && m->RegRead(0x4500 + 8 * 3) == 5, "the CPU reads back what it wrote to MIXS");

	int32_t alt[4];

	for (int n = 0; n < 4; n++)
		alt[n] = dsp_reads();

	check(alt[0] == 0 && alt[1] == 0x123450 && alt[2] == 0 && alt[3] == 0x123450, "the DSP has a CPU write to MIXS in every other sample");

	m->RegWrite(0x4504 + 8 * 3, 0x0777);
	m->SweepBegin();
	m->MixsSend(3, 0x11);
	sample();
	check(dsp_reads() == 0x110, "a channel's send replaces what the CPU wrote to its bus");

	//a core nobody sweeps changes its banks all the same
	reset();
	check(m->Get(AICA_DSP_REG_MIXS_DSP) == 0 && m->Get(AICA_DSP_REG_MIXS_FILL) == 1, "after power on the CPU has bank 0");
	sample();
	check(m->Get(AICA_DSP_REG_MIXS_DSP) == 1 && m->Get(AICA_DSP_REG_MIXS_FILL) == 0, "a sample without SweepBegin changes the banks over");
	m->SweepBegin();
	sample();
	check(m->Get(AICA_DSP_REG_MIXS_DSP) == 0 && m->Get(AICA_DSP_REG_MIXS_FILL) == 1, "a sample with SweepBegin does it once");
}

void dsp_known_answers(KnownMachine& machine)
{
	m = &machine;

	for (int way = 0; way < 2; way++)
	{
		single = way == 0;

		known_shifter();

		m->Group("address");

		for (int size_index = 0; size_index < 4; size_index++)
			known_address(size_index);

		known_ring();
		known_rules();
		known_mixs();
	}

	m = nullptr;
}
