/*
	The known-answer programs of the AICA DSP core: dsp_known.h has what they are and where they run.
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

//MEMS as the CPU has it: 24 bit in two registers
static uint32_t mems_read(int index)
{
	const uint32_t low = m->RegRead((uint32_t)(0x4400 + 8 * index));

	return low | (m->RegRead((uint32_t)(0x4404 + 8 * index)) << 8);
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

		known_rules();
		known_mixs();
	}

	m = nullptr;
}
