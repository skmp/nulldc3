/*
	aica.dsp.diff.*: the DSP core against the DSP of the caique sample model on generated programs, compared
	behind every step (dsp_lockstep.h).

	  random     every bit of the 128 MPRO rows, of COEF, MADRS, TEMP, MEMS and the wave RAM is random
	  sparse     random programs in which a step does little, as a written program's steps do, half of them on
	             operands from the edges of the number ranges
	  directed   one rule at a time, at every place it can apply: a read and the IWT that stores it at every
	             distance, a write in every mode, a bus read in front of every step, a channel's fetch in every
	             slot, the ring at its ends, the test mode of MDEC_CT, the float format through the datapath and,
	             without the DSP around it, on every value there is

	The events a sample gets are generated as well: the sound generator's sends, channel fetches, bus reads, EXTS,
	and what a CPU does between two samples (register writes and reads, wave RAM reads).
	Everything is drawn from fixed seeds: a run compares the same steps every time, and the baseline has their
	number.
*/
#include "dsp_gen.h"
#include "dsp_lockstep.h"
#include "dsp_suite.h"

#include <cstring>
#include <string>
#include <vector>

#ifndef NULLDC_BASELINE
#define NULLDC_BASELINE "baseline.txt"
#endif

static DspLockstep* ls;

//Counts of the family that is running
static uint64_t programs, programs_ok, steps_due, steps_before, reads_before, fast_before;
static std::string first_failure;

static void family_begin()
{
	programs = programs_ok = steps_due = 0;
	steps_before = ls->steps;
	reads_before = ls->reg_reads;
	fast_before = ls->fast_samples;
	first_failure.clear();
}

static void family_end(const char* name)
{
	uint64_t steps = ls->steps - steps_before;

	dsp_count(std::string(name) + ":programs", programs, programs_ok);
	dsp_count(std::string(name) + ":steps", steps_due, steps);
	dsp_count(std::string(name) + ":run-samples", steps_due / 128, ls->fast_samples - fast_before);
	dsp_note("%-10s %8llu programs, %11llu steps compared, %llu CPU reads compared%s%s", name, (unsigned long long)programs,
		(unsigned long long)steps, (unsigned long long)(ls->reg_reads - reads_before),
		first_failure.empty() ? "" : "\n           first: ", first_failure.c_str());
}

//A program starts from power on. The wave RAM stays what the programs before left of it: it is the same in
//the model and in the core, or a program before has failed
static void program_begin()
{
	ls->Reset(false);
}

static void program_end(const char* what, uint64_t index, uint32_t samples)
{
	ls->CompareRam();

	programs++;
	steps_due += (uint64_t)samples * 128;

	if (ls->ok)
		programs_ok++;
	else if (first_failure.empty())
	{
		char text[64];

		snprintf(text, sizeof(text), "%s %llu, ", what, (unsigned long long)index);
		first_failure = text + ls->first;
	}
}

static void put(int step, const AicaDspInst& inst, uint32_t junk = 0)
{
	uint16_t w[4];

	//the bits no field has are register bits like the others
	dsp_gen_encode(inst, junk, w);

	for (int k = 0; k < 4; k++)
		ls->RegWrite(0x3400 + 16 * step + 4 * k, w[k]);
}

static AicaDspInst nop()
{
	AicaDspInst inst;

	memset(&inst, 0, sizeof(inst));

	return inst;
}

static void coef(int step, int value13)
{
	ls->RegWrite(0x3000 + 4 * step, ((uint32_t)value13 & 0x1FFF) << 3);
}

static void madrs(int index, uint32_t value)
{
	ls->RegWrite(0x3200 + 4 * index, value);
}

static void ring(uint32_t rbp, uint32_t rbl, bool test_mode = false)
{
	ls->RegWrite(0x2804, (rbp & 0xFFF) | (rbl << 13) | (test_mode ? 0x8000 : 0));
}

static void temp_set(int index, uint32_t value24)
{
	ls->RegWrite(0x4000 + 8 * index, value24 & 0xFF);
	ls->RegWrite(0x4004 + 8 * index, (value24 >> 8) & 0xFFFF);
}

static void mems_set(int index, uint32_t value24)
{
	ls->RegWrite(0x4404 + 8 * index, (value24 >> 8) & 0xFFFF);
}

static void ram_word(uint32_t byte_addr, uint16_t word)
{
	byte_addr &= (CAIQUE_RAM_SIZE - 1) & ~1u;

	ls->RamPoke(byte_addr, (uint8_t)word);
	ls->RamPoke(byte_addr + 1, (uint8_t)(word >> 8));
}

static void fill_ram_random(DspRandom& rnd)
{
	std::vector<uint8_t> image(CAIQUE_RAM_SIZE);

	for (uint32_t i = 0; i < CAIQUE_RAM_SIZE; i += 8)
	{
		uint64_t v = rnd.Next();

		memcpy(&image[i], &v, 8);
	}

	ls->RamFill(image.data());
}

//Numbers from the ends of a range of `bits` bits, and around its powers of two (dsp_gen.h)
static uint32_t edge_value(DspRandom& rnd, int bits)
{
	return dsp_gen_edge(rnd, bits);
}


/*
	Random programs
*/

//What a random program is like (dsp_gen.h)
typedef DspGenProfile RandomProfile;

static uint16_t random_word(DspRandom& rnd, const RandomProfile& p, int bits)
{
	return dsp_gen_word(rnd, p, bits);
}

static void random_step(DspRandom& rnd, const RandomProfile& p, int step)
{
	uint16_t w[4];

	dsp_gen_step(rnd, p, w);

	for (int k = 0; k < 4; k++)
		ls->RegWrite(0x3400 + 16 * step + 4 * k, w[k]);
}

static void random_cpu_access(DspRandom& rnd, const RandomProfile& p)
{
	static const struct { uint32_t base, count; } areas[] =
	{
		{ 0x3000, 128 }, { 0x3200, 64 }, { 0x3400, 512 }, { 0x4000, 256 }, { 0x4400, 64 }, { 0x4500, 32 }, { 0x4580, 16 },
		{ 0x45C0, 2 }, { 0x2804, 1 },
	};
	const auto& area = areas[rnd.Below(sizeof(areas) / sizeof(areas[0]))];
	uint32_t off = area.base + 4 * rnd.Below(area.count);

	switch (rnd.Below(6))
	{
	case 0:
	case 1:
		//the test mode of MDEC_CT only where the profile of the program has it
		if (off == 0x2804)
			ls->RegWrite(off, (rnd.Bits(16) & 0x7FFF) | (ls->m.reg[0x2804 >> 2] & 0x8000));
		else
			ls->RegWrite(off, random_word(rnd, p, 16));
		break;

	case 2:
	case 3:
		ls->RegRead(off);
		break;

	case 4:
		//a wave RAM read: its upper half is in the DSP's read latch at the next step 0
		ls->RamRead32(rnd.Bits(21));
		break;

	default:
		ls->RamWrite32(rnd.Bits(21), rnd.Bits(32));
		break;
	}
}

static void random_events(DspRandom& rnd, const RandomProfile& p, LockstepEvents& ev)
{
	uint32_t sends = rnd.Chance(8) ? 16 + rnd.Below(64) : rnd.Below(6);

	for (uint32_t i = 0; i < sends; i++)
	{
		LockstepSend send;

		send.bus = (uint8_t)rnd.Bits(4);

		//up to 24 bit: a sum, and already one send, can leave the 20 bit of a bus
		send.value = (int32_t)((p.edges ? edge_value(rnd, 24) : rnd.Bits(rnd.Chance(2) ? 20 : 24)) << 8) >> 8;
		ev.sends.push_back(send);
	}

	for (int k = 0; k < 64; k++)
	{
		bool fetch = p.fetches == 3 || (p.fetches == 2 && rnd.Chance(2)) || (p.fetches == 1 && rnd.Chance(24));

		if (fetch)
			ev.fetch |= 1ull << k;

		ev.fetch_word[k] = (uint16_t)rnd.Bits(16);
	}

	uint32_t reads = p.bus_reads == 2 ? 1 + rnd.Below(12) : p.bus_reads == 1 ? (rnd.Chance(3) ? 1 : 0) : 0;

	for (uint32_t i = 0; i < reads; i++)
	{
		uint32_t step = rnd.Bits(7);

		ev.bus_read[step] = true;
		ev.bus_word[step] = (uint16_t)rnd.Bits(16);
	}
}

static void random_program(DspRandom& rnd, const RandomProfile& p, uint32_t samples)
{
	for (int i = 0; i < 128; i++)
		ls->RegWrite(0x3000 + 4 * i, random_word(rnd, p, 16));

	for (int i = 0; i < 64; i++)
		ls->RegWrite(0x3200 + 4 * i, random_word(rnd, p, 16));

	for (int s = 0; s < 128; s++)
		random_step(rnd, p, s);

	for (int i = 0; i < 128; i++)
		temp_set(i, p.edges ? edge_value(rnd, 24) : rnd.Bits(24));

	for (int i = 0; i < 32; i++)
		mems_set(i, p.edges ? edge_value(rnd, 24) : rnd.Bits(24));

	for (int i = 0; i < 16; i++)
	{
		ls->RegWrite(0x4500 + 8 * i, rnd.Bits(4));
		ls->RegWrite(0x4504 + 8 * i, random_word(rnd, p, 16));
		ls->RegWrite(0x4580 + 4 * i, rnd.Bits(16));
	}

	//a ring anywhere, also across the end of the wave RAM; the test mode of MDEC_CT in one program of 16
	ring(rnd.Bits(12), rnd.Bits(2), rnd.Chance(16));
	ls->SetMdecCt(rnd.Chance(4) ? rnd.Below(4) : rnd.Bits(16));

	for (uint32_t n = 0; n < samples; n++)
	{
		LockstepEvents ev;

		random_events(rnd, p, ev);
		ls->SetExts(0, (int16_t)random_word(rnd, p, 16));
		ls->SetExts(1, (int16_t)random_word(rnd, p, 16));

		ls->StepDsp(ev);

		uint32_t accesses = p.cpu ? rnd.Below(p.cpu + 1) : 0;

		for (uint32_t i = 0; i < accesses; i++)
			random_cpu_access(rnd, p);
	}
}

static void run_random(const char* name, bool sparse, uint32_t count, uint32_t samples, uint64_t seed)
{
	DspRandom rnd(seed);

	family_begin();
	fill_ram_random(rnd);

	for (uint32_t i = 0; i < count; i++)
	{
		RandomProfile p;

		p.sparse = sparse;
		p.edges = sparse && (i & 1);
		p.fetches = rnd.Below(4);
		p.bus_reads = rnd.Below(3);
		p.cpu = rnd.Chance(3) ? 0 : 6;

		program_begin();
		random_program(rnd, p, samples);
		program_end("program", i, samples);
	}

	family_end(name);
}


/*
	Directed programs
*/

//ACC = -MEMS[index] in `step`: the step behind it has that in SHIFTED
static void acc_neg_mems(int step, int index)
{
	AicaDspInst inst = nop();

	coef(step, -4096);
	inst.XSEL = 1;
	inst.IRA = (uint8_t)index;
	inst.YSEL = 1;
	inst.ZERO = 1;
	put(step, inst);
}

//A wave RAM read, and the IWT that stores it, at every distance and in every step: with the float conversion
//or without, another read behind it, a write in its step, a channel in its slot, across the end of the sample
static void directed_read_distance(DspRandom& rnd)
{
	family_begin();

	uint32_t index = 0;

	for (int s = 0; s < 128; s++)
	{
		for (int d = 0; d < 6; d++)
		{
			for (uint32_t v = 0; v < 32; v++, index++)
			{
				AicaDspInst prog[128];

				for (int i = 0; i < 128; i++)
					prog[i] = nop();

				int iwt = (s + d) & 127;

				program_begin();
				ring(0x200, 0);

				for (int i = 0; i < 4; i++)
				{
					madrs(i, 0x100 + i);
					ram_word(0x100000 + 2 * (0x100 + i), (uint16_t)rnd.Bits(16));
				}

				prog[s].MRD = 1;
				prog[s].TABLE = 1;
				prog[s].MASA = 0;
				prog[s].NOFL = v & 1;
				prog[(iwt - 2) & 127].NOFL = (v >> 1) & 1;

				if (v & 4)
				{
					prog[(s + 1) & 127].MRD = 1;
					prog[(s + 1) & 127].TABLE = 1;
					prog[(s + 1) & 127].MASA = 1;
				}

				if (v & 8)
				{
					prog[s].MWT = 1;
					prog[s].MASA = 2;
				}

				prog[iwt].IWT = 1;
				prog[iwt].IWA = 5;

				//the value comes back out: MEMS5 to TEMP, three steps on
				prog[(iwt + 1) & 127].XSEL = 1;
				prog[(iwt + 1) & 127].IRA = 5;
				prog[(iwt + 1) & 127].YSEL = 1;
				prog[(iwt + 1) & 127].ZERO = 1;
				coef((iwt + 1) & 127, 4095);
				prog[(iwt + 2) & 127].TWT = 1;
				prog[(iwt + 2) & 127].TWA = 9;

				for (int i = 0; i < 128; i++)
					put(i, prog[i]);

				//what the latch holds when nothing is read: the upper half of a CPU read
				ls->RamWrite32(0x1000, rnd.Bits(32));
				ls->RamRead32(0x1000);

				for (int n = 0; n < 2; n++)
				{
					LockstepEvents ev;

					if (v & 16)
					{
						//the slot of the read if it is a channel's, else the one behind it
						int slot = (s & 1) ? (s + 1) & 127 : s;
						int channel = ((slot + 14) >> 1) & 63;

						ev.fetch = 1ull << channel;
						ev.fetch_word[channel] = (uint16_t)rnd.Bits(16);
					}

					ls->StepDsp(ev);
				}

				program_end("read distance", index, 2);
			}
		}
	}

	family_end("read");
}

//A wave RAM write in every step and every mode: the four shifters, float or not, ring or table, with
//ADRS_REG and NXADR, MDEC_CT at the ends of its range, and a channel in the slot for one in four
static void directed_write(DspRandom& rnd)
{
	family_begin();

	uint32_t index = 0;

	for (int s = 0; s < 128; s++)
	{
		for (uint32_t v = 0; v < 64; v++, index++)
		{
			program_begin();

			uint32_t rbl = rnd.Bits(2);

			ring(rnd.Chance(4) ? 0x3FF : rnd.Bits(12), rbl);
			ls->SetMdecCt(rnd.Chance(2) ? rnd.Below(3) : rnd.Bits(16));

			//the address register: from INPUTS (8 bit, signed) or from the shifter (12 bit)
			mems_set(2, rnd.Bits(24));
			mems_set(1, rnd.Chance(2) ? edge_value(rnd, 24) : rnd.Bits(24));
			madrs(7, rnd.Chance(2) ? edge_value(rnd, 16) : rnd.Bits(16));

			int a = (s - 3) & 127, b = (s - 2) & 127, c = (s - 1) & 127;
			AicaDspInst inst;

			acc_neg_mems(a, 2);

			inst = nop();
			inst.ADRL = 1;
			inst.IRA = 2;
			inst.SHIFT = (v & 32) ? 3 : 0;
			put(b, inst);

			//ACC = MEMS1 * COEF + TEMP
			inst = nop();
			inst.XSEL = 1;
			inst.IRA = 1;
			inst.YSEL = 1;
			inst.TRA = 3;
			put(c, inst);
			ls->RegWrite(0x3000 + 4 * c, rnd.Bits(16));

			for (int i = 0; i < 128; i++)
				temp_set(i, rnd.Chance(2) ? edge_value(rnd, 24) : rnd.Bits(24));

			inst = nop();
			inst.MWT = 1;
			inst.MASA = 7;
			inst.SHIFT = (uint8_t)(v & 3);
			inst.NOFL = (v >> 2) & 1;
			inst.TABLE = (v >> 3) & 1;
			inst.ADREB = (v >> 4) & 1;
			inst.NXADR = (uint8_t)rnd.Bits(1);
			put(s, inst);

			for (int n = 0; n < 2; n++)
			{
				LockstepEvents ev;

				if (!(s & 1) && rnd.Chance(4))
				{
					int channel = ((s + 14) >> 1) & 63;

					ev.fetch = 1ull << channel;
					ev.fetch_word[channel] = (uint16_t)rnd.Bits(16);
				}

				ls->StepDsp(ev);
			}

			program_end("write", index, 2);
		}
	}

	family_end("write");
}

//A bus read in front of every step, next to a read of the DSP one to four steps around it, and IWTs behind
static void directed_bus_read(DspRandom& rnd)
{
	family_begin();

	uint32_t index = 0;

	for (int p = 0; p < 128; p++)
	{
		for (int rel = -3; rel <= 1; rel++)
		{
			for (uint32_t v = 0; v < 4; v++, index++)
			{
				int s = (p + rel) & 127;
				AicaDspInst prog[128];

				for (int i = 0; i < 128; i++)
				{
					prog[i] = nop();
					prog[i].NOFL = (v >> 1) & 1;
				}

				program_begin();
				ring(0x200, 0);
				madrs(0, 0x40);
				madrs(1, 0x41);
				ram_word(0x100000 + 2 * 0x40, (uint16_t)rnd.Bits(16));

				prog[s].MRD = 1;
				prog[s].TABLE = 1;

				//a write of the DSP in the slot of the bus read: the latch keeps the bus's word
				if (v & 1)
				{
					prog[p].MWT = 1;
					prog[p].TABLE = 1;
					prog[p].MASA = 1;
				}

				for (int k = 1; k <= 5; k++)
				{
					prog[(s + k) & 127].IWT = 1;
					prog[(s + k) & 127].IWA = (uint8_t)k;
				}

				for (int i = 0; i < 128; i++)
					put(i, prog[i]);

				for (int n = 0; n < 2; n++)
				{
					LockstepEvents ev;

					ev.bus_read[p] = true;
					ev.bus_word[p] = (uint16_t)rnd.Bits(16);
					ls->StepDsp(ev);
				}

				program_end("bus read", index, 2);
			}
		}
	}

	family_end("bus-read");
}

//A channel in every slot, with what the DSP step of that slot can want: nothing, a read, a write, both. A read of
//the DSP one and two steps in front of the slot, and IWTs behind
static void directed_fetch(DspRandom& rnd)
{
	family_begin();

	uint32_t index = 0;

	for (int channel = 0; channel < 64; channel++)
	{
		for (uint32_t op = 0; op < 4; op++)
		{
			for (int before = 0; before < 3; before++, index++)
			{
				int s = (int)AicaDsp::ChannelStep(channel);
				AicaDspInst prog[128];

				for (int i = 0; i < 128; i++)
				{
					prog[i] = nop();
					prog[i].NOFL = 1;
				}

				program_begin();
				ring(0x200, 0);

				for (int i = 0; i < 4; i++)
				{
					madrs(i, 0x80 + i);
					ram_word(0x100000 + 2 * (0x80 + i), (uint16_t)rnd.Bits(16));
				}

				//the step in front of the slot leaves -MEMS16 in ACC: what a write in the slot writes
				int c = (s - 1) & 127;

				mems_set(16, rnd.Bits(24));
				coef(c, -4096);
				prog[c].XSEL = 1;
				prog[c].IRA = 16;
				prog[c].YSEL = 1;
				prog[c].ZERO = 1;

				prog[s].TABLE = 1;
				prog[s].MRD = op & 1;
				prog[s].MWT = (op >> 1) & 1;
				prog[s].MASA = (op & 2) ? 2 : 0;

				if (before)
				{
					prog[(s - before) & 127].MRD = 1;
					prog[(s - before) & 127].TABLE = 1;
					prog[(s - before) & 127].MASA = 1;
				}

				for (int k = 1; k <= 4; k++)
				{
					prog[(s + k) & 127].IWT = 1;
					prog[(s + k) & 127].IWA = (uint8_t)k;
				}

				for (int i = 0; i < 128; i++)
					put(i, prog[i]);

				for (int n = 0; n < 3; n++)
				{
					LockstepEvents ev;

					//the channel fetches in two samples of three
					if (n != 1)
					{
						ev.fetch = 1ull << channel;
						ev.fetch_word[channel] = (uint16_t)rnd.Bits(16);
					}

					ls->StepDsp(ev);
				}

				program_end("fetch", index, 3);
			}
		}
	}

	family_end("fetch");
}

//The ring: a counter that is written every sample at MDEC_CT, for every ring size, with MDEC_CT going through 0
//and through the size of the ring, and the ring at places where its end is behind the end of the wave RAM
static void directed_ring(DspRandom& rnd)
{
	static const uint32_t rbps[] = { 0x000, 0x200, 0x3F0, 0x3FF, 0xFFF, 0x7E1 };

	family_begin();

	uint32_t index = 0;

	for (uint32_t rbl = 0; rbl < 4; rbl++)
	{
		for (uint32_t rbp : rbps)
		{
			for (int start = 0; start < 3; start++)
			{
				for (int table = 0; table < 2; table++, index++)
				{
					AicaDspInst inst;

					program_begin();
					ring(rbp, rbl);
					ls->SetMdecCt(start == 0 ? 20 : start == 1 ? (8192u << rbl) + 20 : 0x8000 + 20);
					madrs(0, rnd.Chance(2) ? 0 : rnd.Bits(16));
					mems_set(31, 0x000100);

					//steps 0, 1: ACC = TEMP[1 + MDEC_CT] - 0x100; step 2: TEMP[0 + MDEC_CT] = it; step 3: to the ring
					for (int s = 0; s < 2; s++)
					{
						coef(s, -4096);
						inst = nop();
						inst.XSEL = 1;
						inst.IRA = 31;
						inst.YSEL = 1;
						inst.TRA = 1;
						inst.SHIFT = 3;
						put(s, inst);
					}

					coef(2, 0);
					inst = nop();
					inst.SHIFT = 3;
					inst.TWT = 1;
					inst.BSEL = 1;
					inst.YSEL = 1;
					put(2, inst);

					inst = nop();
					inst.SHIFT = 3;
					inst.MWT = 1;
					inst.NOFL = 1;
					inst.TABLE = (uint8_t)table;
					inst.ZERO = 1;
					put(3, inst);

					//and read back a sample later, float, one word up
					inst = nop();
					inst.MRD = 1;
					inst.NXADR = 1;
					inst.TABLE = (uint8_t)table;
					put(5, inst);
					inst = nop();
					inst.IWT = 1;
					inst.IWA = 3;
					put(7, inst);

					for (int n = 0; n < 40; n++)
					{
						LockstepEvents ev;

						ls->StepDsp(ev);
					}

					program_end("ring", index, 40);
				}
			}
		}
	}

	family_end("ring");
}

//The test mode of MDEC_CT: bit 15 of 0x2804 goes on and off between samples under a program that writes at
//MDEC_CT and reads TEMP relative to it
static void directed_test_mode(DspRandom& rnd)
{
	family_begin();

	for (uint32_t index = 0; index < 64; index++)
	{
		AicaDspInst inst;
		uint32_t rbl = rnd.Bits(2), rbp = rnd.Bits(12);

		program_begin();
		ring(rbp, rbl);
		ls->SetMdecCt(rnd.Bits(16));
		mems_set(31, 0x000100);

		for (int i = 0; i < 128; i++)
			temp_set(i, rnd.Bits(24));

		coef(0, -4096);
		inst = nop();
		inst.XSEL = 1;
		inst.IRA = 31;
		inst.YSEL = 1;
		inst.TRA = 1;
		inst.SHIFT = 3;
		put(0, inst);

		inst = nop();
		inst.SHIFT = 3;
		inst.TWT = 1;
		inst.MWT = 1;
		inst.NOFL = 1;
		inst.ZERO = 1;
		put(1, inst);

		inst.TWT = 0;
		inst.TABLE = 1;
		put(3, inst);

		bool on = false;

		for (int n = 0; n < 32; n++)
		{
			LockstepEvents ev;

			if (rnd.Chance(3))
			{
				on = !on;
				ring(rbp, rbl, on);
			}

			ls->StepDsp(ev);
		}

		program_end("test mode", index, 32);
	}

	family_end("test-mode");
}

//The float format through the datapath: 30 values a program from TEMP (all 24 bit of it are the CPU's) to the
//wave RAM, packed, and back through the read latch, unpacked, into MEMS
static void directed_float(DspRandom& rnd)
{
	family_begin();

	for (uint32_t index = 0; index < 400; index++)
	{
		program_begin();
		ring(0x200, 0);

		for (int i = 0; i < 128; i++)
			temp_set(i, (index & 1) ? rnd.Bits(24) : edge_value(rnd, 24));

		for (int j = 0; j < 30; j++)
		{
			AicaDspInst inst;

			madrs(j, 0x10 + j);

			//ACC = TEMP[j + MDEC_CT]: X * 0 + B
			coef(2 * j, 0);
			inst = nop();
			inst.YSEL = 1;
			inst.TRA = (uint8_t)j;
			put(2 * j, inst);

			inst = nop();
			inst.SHIFT = 3;
			inst.MWT = 1;
			inst.TABLE = 1;
			inst.MASA = (uint8_t)j;
			inst.ZERO = 1;
			inst.YSEL = 1;
			put(2 * j + 1, inst);
		}

		//and back: word j is read in step 61 + 2 j and stored two steps on
		for (int j = 0; j <= 30; j++)
		{
			AicaDspInst inst = nop();

			if (j < 30)
			{
				inst.MRD = 1;
				inst.TABLE = 1;
				inst.MASA = (uint8_t)j;
			}

			if (j > 0)
			{
				inst.IWT = 1;
				inst.IWA = (uint8_t)(j - 1);
			}

			put(61 + 2 * j, inst);
		}

		for (int n = 0; n < 4; n++)
		{
			LockstepEvents ev;

			ls->StepDsp(ev);
		}

		program_end("float", index, 4);
	}

	family_end("float");
}

//The datapath: every combination of the operand selects and the shifter on operands from the edges of their
//ranges, behind a chain that fills all 26 bit of ACC, with every register load and two writes to one EFREG
static void directed_datapath(DspRandom& rnd)
{
	family_begin();

	uint32_t index = 0;

	for (uint32_t combo = 0; combo < 256; combo++)
	{
		for (int rep = 0; rep < 12; rep++, index++)
		{
			AicaDspInst inst;

			program_begin();
			ring(0x200, 0);

			for (int i = 0; i < 128; i++)
			{
				temp_set(i, edge_value(rnd, 24));
				ls->RegWrite(0x3000 + 4 * i, edge_value(rnd, 13) << 3);
			}

			for (int i = 0; i < 32; i++)
				mems_set(i, edge_value(rnd, 24));

			madrs(0, 0x20);

			//Y_REG from any input
			inst = nop();
			inst.YRL = 1;
			inst.IRA = (uint8_t)rnd.Below(0x34);
			put(0, inst);

			//FRC_REG from a product, through any shifter mode
			inst = nop();
			inst.XSEL = 1;
			inst.IRA = (uint8_t)rnd.Below(0x32);
			inst.YSEL = 1;
			inst.ZERO = 1;
			put(1, inst);

			inst = nop();
			inst.FRCL = 1;
			inst.SHIFT = (uint8_t)rnd.Bits(2);
			inst.XSEL = 1;
			inst.IRA = (uint8_t)rnd.Below(0x32);
			inst.YSEL = 1;
			inst.ZERO = 1;
			put(2, inst);

			//a chain on ACC: products are added to it until 26 bit are too few
			for (int s = 3; s < 8; s++)
			{
				inst = nop();
				inst.XSEL = 1;
				inst.IRA = (uint8_t)rnd.Below(0x32);
				inst.YSEL = 1;
				inst.BSEL = 1;
				inst.NEGB = rnd.Chance(4);
				put(s, inst);
			}

			//the combination, three steps of it: each shifts the ACC of the one before
			for (int s = 8; s < 11; s++)
			{
				inst = nop();
				inst.XSEL = combo & 1;
				inst.YSEL = (combo >> 1) & 3;
				inst.BSEL = (combo >> 3) & 1;
				inst.ZERO = (combo >> 4) & 1;
				inst.NEGB = (combo >> 5) & 1;
				inst.SHIFT = (combo >> 6) & 3;
				inst.IRA = (uint8_t)rnd.Below(0x34);
				inst.TRA = (uint8_t)rnd.Bits(7);
				inst.TWT = 1;
				inst.TWA = (uint8_t)(100 + s);
				inst.EWT = 1;
				inst.EWA = (uint8_t)(s == 10 ? 4 : 3);
				inst.FRCL = s == 9;
				inst.ADRL = s == 9;
				inst.YRL = s == 9;
				put(s, inst);
			}

			//what the combination left, into the wave RAM in both formats
			inst = nop();
			inst.SHIFT = (combo >> 6) & 3;
			inst.MWT = 1;
			inst.TABLE = 1;
			inst.ADREB = 1;
			put(11, inst);
			inst.NOFL = 1;
			inst.NXADR = 1;
			put(12, inst);

			for (int n = 0; n < 2; n++)
			{
				LockstepEvents ev;

				for (int bus = 0; bus < 16; bus++)
				{
					LockstepSend send;

					send.bus = (uint8_t)bus;
					send.value = (int32_t)(edge_value(rnd, 20) << 12) >> 12;
					ev.sends.push_back(send);
				}

				ls->SetExts(0, (int16_t)edge_value(rnd, 16));
				ls->SetExts(1, (int16_t)edge_value(rnd, 16));
				ls->StepDsp(ev);
			}

			program_end("datapath", index, 2);
		}
	}

	family_end("datapath");
}

//MIXS: sweeps that send several times to a bus, past the 20 bit of it, or not at all, under a program that reads
//all 16 buses, with a CPU that writes buses between the samples and reads all of them
static void directed_mixs(DspRandom& rnd)
{
	family_begin();

	for (uint32_t index = 0; index < 256; index++)
	{
		program_begin();

		for (int bus = 0; bus < 16; bus++)
		{
			AicaDspInst inst = nop();

			coef(2 * bus, 4095);
			inst.XSEL = 1;
			inst.IRA = (uint8_t)(0x20 + bus);
			inst.YSEL = 1;
			inst.ZERO = 1;
			put(2 * bus, inst);

			inst = nop();
			inst.SHIFT = 3;
			inst.TWT = 1;
			inst.TWA = (uint8_t)bus;
			inst.EWT = 1;
			inst.EWA = (uint8_t)bus;
			put(2 * bus + 1, inst);
		}

		//the buses that have a channel: the others keep what they have
		uint32_t with_sender = index & 1 ? rnd.Bits(16) : 0xFFFF;

		for (int n = 0; n < 8; n++)
		{
			LockstepEvents ev;

			for (int bus = 0; bus < 16; bus++)
			{
				if (!((with_sender >> bus) & 1))
					continue;

				uint32_t count = rnd.Chance(4) ? 64 : 1 + rnd.Below(4);

				for (uint32_t i = 0; i < count; i++)
				{
					LockstepSend send;

					send.bus = (uint8_t)bus;
					send.value = rnd.Chance(3) ? 0 : (int32_t)(edge_value(rnd, 20) << 12) >> 12;
					ev.sends.push_back(send);
				}
			}

			ls->StepDsp(ev);

			for (int i = 0; i < 4; i++)
			{
				uint32_t bus = rnd.Bits(4);

				if (rnd.Chance(2))
					ls->RegWrite(0x4500 + 8 * bus, rnd.Bits(16));

				if (rnd.Chance(2))
					ls->RegWrite(0x4504 + 8 * bus, rnd.Bits(16));
			}

			for (uint32_t off = 0x4500; off < 0x4580; off += 4)
				ls->RegRead(off);
		}

		program_end("mixs", index, 8);
	}

	family_end("mixs");
}

//The CPU's side: every register of the DSP written with patterns and read back, a sample of a running program
//in between, and read again
static void directed_cpu(DspRandom& rnd)
{
	static const uint32_t patterns[] = { 0xFFFF, 0x0000, 0x5555, 0xAAAA };

	family_begin();

	for (uint32_t index = 0; index < 16; index++)
	{
		RandomProfile profile = { true, false, 0, 0, 0 };

		program_begin();

		for (int s = 0; s < 128; s++)
			random_step(rnd, profile, s);

		for (int pass = 0; pass < 2; pass++)
		{
			for (uint32_t off = 0x2800; off < 0x4600; off += 4)
			{
				if (!AicaDsp::RegOwned(off))
					continue;

				//the program stays what it is in the second pass, and the test mode of MDEC_CT is not this family's
				bool keep = off == 0x2804 || (pass == 1 && off >= 0x3400 && off < 0x3C00);

				if (!keep)
					ls->RegWrite(off, index < 4 ? patterns[index] : rnd.Bits(32));

				ls->RegRead(off);
			}

			LockstepEvents ev;

			ls->StepDsp(ev);

			for (uint32_t off = 0x2800; off < 0x4600; off += 4)
			{
				if (AicaDsp::RegOwned(off))
					ls->RegRead(off);
			}
		}

		program_end("cpu", index, 2);
	}

	family_end("cpu");
}

//The float format alone: the core's functions against the model's shared header on every value
static void float_format()
{
	uint64_t pack_ok = 0, unpack_ok = 0;

	for (uint32_t v = 0; v < (1u << 24); v++)
	{
		int32_t value = (int32_t)(v << 8) >> 8;

		pack_ok += AicaDspPack(value) == caique_pack(value);
	}

	for (uint32_t w = 0; w < 65536; w++)
		unpack_ok += AicaDspUnpack((uint16_t)w) == caique_unpack((uint16_t)w);

	dsp_count("pack:values", 1u << 24, pack_ok);
	dsp_count("unpack:words", 65536, unpack_ok);
	dsp_note("%-10s pack %llu of 16777216 values, unpack %llu of 65536 words as the model's header", "format",
		(unsigned long long)pack_ok, (unsigned long long)unpack_ok);
}

int main(int argc, char** argv)
{
	static const char* usage =
		"aica_dsp_diff <random|sparse|directed> [options]\n"
		"The AICA DSP core against the DSP of the caique sample model on generated programs, step for step";

	DspOptions opt;

	opt.baseline = NULLDC_BASELINE;

	std::string suite = argc > 1 ? argv[1] : "";

	if (suite != "random" && suite != "sparse" && suite != "directed")
	{
		fprintf(stderr, "%s\n", usage);
		return DSP_EXIT_ERROR;
	}

	if (!dsp_suite_options(argc, argv, 2, usage, opt))
		return DSP_EXIT_ERROR;

	std::string name = "aica.dsp.diff." + suite;

	if (!dsp_suite_begin(name.c_str(), opt))
		return DSP_EXIT_ERROR;

	ls = new DspLockstep();

	if (suite == "random")
		run_random("random", false, 1500, 16, 0xA1CAD5B0001ull);
	else if (suite == "sparse")
		run_random("sparse", true, 4000, 24, 0xA1CAD5B0002ull);
	else
	{
		DspRandom rnd(0xA1CAD5B0003ull);

		fill_ram_random(rnd);
		directed_read_distance(rnd);
		directed_write(rnd);
		directed_bus_read(rnd);
		directed_fetch(rnd);
		directed_ring(rnd);
		directed_test_mode(rnd);
		directed_float(rnd);
		directed_datapath(rnd);
		directed_mixs(rnd);
		directed_cpu(rnd);
		float_format();
	}

	//what the compared steps did
	const DspLockstep::Coverage& c = ls->cover;

	dsp_count("did:ram-read", c.ram_reads, c.ram_reads);
	dsp_count("did:ram-write", c.ram_writes, c.ram_writes);
	dsp_count("did:write-without-read", c.no_reads, c.no_reads);
	dsp_count("did:slot-lost", c.slots_lost, c.slots_lost);
	dsp_count("did:iwt-raw", c.iwt_raw, c.iwt_raw);
	dsp_count("did:iwt-float", c.iwt_float, c.iwt_float);
	dsp_count("did:channel-fetch", c.fetches, c.fetches);
	dsp_count("did:bus-read", c.bus_reads, c.bus_reads);
	dsp_count("did:send", c.sends, c.sends);
	dsp_count("did:cpu-read", ls->reg_reads, ls->reg_reads);

	if (!ls->ok && ls->first.find("announce") != std::string::npos)
		dsp_problem("%s", ls->first.c_str());

	delete ls;

	return dsp_suite_end();
}
