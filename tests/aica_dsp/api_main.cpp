/*
	aica.dsp.api: the DSP core's interface on itself. Needs no data.

	  split       a sample run with RunSample from every step, behind that many StepOne, is the sample run with 128
	              StepOne: every register, the wave RAM. On random programs, on programs that end early at every
	              length, with sends, channel fetches and a pending ChannelFetch
	  state       SaveState in front of every step, with a channel in the slot, a sweep begun and sends made by
	              calls, LoadState into a core of all zeros and into one of all ones: the blob is the same again,
	              and both go on to the same state. Blobs that are not a state are refused
	  instances   two cores with different programs, step by step in turns, end where each ends alone
	  registers   what the CPU can write and read back of every register the DSP has, and that it has no others
	  mixs        the sends of a sweep, the two banks, the late sends, the CPU's bank
	  rules       the read latch, the IWT distances and the channel's slot on programs small enough to say by hand
	              what they leave
	  helpers     decode and encode, the float format at its corners, the view of a step, a core without wave RAM
*/
#include "dsp_suite.h"
#include "hw/aica/dsp_core/aica_dsp_core.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#ifndef NULLDC_BASELINE
#define NULLDC_BASELINE "baseline.txt"
#endif

#define RAM_SIZE 0x10000u		//smaller than the largest ring: its addresses wrap in it

static std::string current;
static uint64_t current_total, current_pass;

static void group(const char* name)
{
	if (!current.empty())
		dsp_count(current, current_total, current_pass);

	current = name ? name : "";
	current_total = current_pass = 0;
}

static void check(bool pass, const char* what)
{
	current_total++;
	current_pass += pass;

	if (!pass && (dsp_suite_opt().verbose || current_total - current_pass <= 3))
		dsp_note("  %s: %s", current.c_str(), what);
}

struct Machine
{
	AicaDsp dsp;
	std::vector<uint8_t> ram;

	Machine() : ram(RAM_SIZE, 0) { dsp.Init(ram.data(), RAM_SIZE); }

	Machine(const Machine& other) : dsp(other.dsp), ram(other.ram) { dsp.ram = ram.data(); }

	Machine& operator=(const Machine& other)
	{
		dsp = other.dsp;
		ram = other.ram;
		dsp.ram = ram.data();

		return *this;
	}
};

static std::vector<uint8_t> blob_of(const AicaDsp& dsp)
{
	std::vector<uint8_t> blob(AicaDsp::StateSize());

	dsp.SaveState(blob.data());

	return blob;
}

static bool same(const Machine& a, const Machine& b)
{
	return blob_of(a.dsp) == blob_of(b.dsp) && a.ram == b.ram;
}

static void put(AicaDsp& dsp, int step, const AicaDspInst& inst)
{
	uint16_t w[4];

	AicaDspEncode(&inst, w);

	for (int k = 0; k < 4; k++)
		dsp.RegWrite(0x3400 + 16 * step + 4 * k, w[k]);
}

static AicaDspInst nop()
{
	AicaDspInst inst;

	memset(&inst, 0, sizeof(inst));

	return inst;
}

//A program of `steps` busy rows, the rest empty, and everything a program reads
static void random_machine(Machine& m, DspRandom& rnd, int steps, bool sparse)
{
	AicaDsp& dsp = m.dsp;

	for (uint32_t i = 0; i < RAM_SIZE; i++)
		m.ram[i] = (uint8_t)rnd.Bits(8);

	for (int i = 0; i < 128; i++)
	{
		dsp.RegWrite(0x3000 + 4 * i, rnd.Bits(16));
		dsp.RegWrite(0x4000 + 8 * i, rnd.Bits(8));
		dsp.RegWrite(0x4004 + 8 * i, rnd.Bits(16));
	}

	for (int i = 0; i < 64; i++)
		dsp.RegWrite(0x3200 + 4 * i, rnd.Bits(16));

	for (int i = 0; i < 32; i++)
		dsp.RegWrite(0x4404 + 8 * i, rnd.Bits(16));

	for (int s = 0; s < steps; s++)
	{
		for (int k = 0; k < 4; k++)
		{
			uint32_t word = rnd.Bits(16);

			//a sparse program's steps write and read less: every flag of w2 with a chance of 1 in 4
			if (sparse && k == 2)
				word &= rnd.Bits(16) | 0x0030;

			dsp.RegWrite(0x3400 + 16 * s + 4 * k, word);
		}
	}

	dsp.RegWrite(0x2804, rnd.Bits(15));
	dsp.MDEC_CT = (uint16_t)rnd.Bits(16);
}

static void random_inputs(AicaDspSampleInputs& in, DspRandom& rnd, int fetches)
{
	memset(&in, 0, sizeof(in));

	in.mixs_sent = (uint16_t)rnd.Bits(16);

	for (int i = 0; i < 16; i++)
		in.mixs[i] = (int32_t)(rnd.Bits(24) << 8) >> 8;

	for (int k = 0; k < 64; k++)
	{
		if (fetches == 2 || (fetches == 1 && rnd.Chance(8)))
			in.fetch |= 1ull << k;

		in.fetch_word[k] = (uint16_t)rnd.Bits(16);
	}
}

static void test_split()
{
	DspRandom rnd(0x5911700ull);

	//how many rows are busy: all, none, and what leaves a run of empty rows of every kind of length at the end
	static const int lengths[] = { 128, 128, 128, 127, 126, 125, 124, 123, 122, 121, 100, 64, 17, 3, 2, 1, 0 };

	group("split");

	int index = 0;

	for (int steps : lengths)
	{
		for (int variant = 0; variant < 3; variant++, index++)
		{
			Machine start;

			random_machine(start, rnd, steps, variant != 0);

			for (int sample = 0; sample < 3; sample++)
			{
				AicaDspSampleInputs in;

				random_inputs(in, rnd, (index + sample) % 3);
				start.dsp.EXTS[0] = (int16_t)rnd.Bits(16);
				start.dsp.EXTS[1] = (int16_t)rnd.Bits(16);

				//a sample in three now has its events made call by call, and a channel that took the slot of step 0
				bool calls = sample == 2;
				const AicaDspSampleInputs* given = calls ? nullptr : &in;

				if (calls)
				{
					start.dsp.SweepBegin();
					start.dsp.MixsSend(3, in.mixs[3]);
					start.dsp.ChannelFetch(in.fetch_word[7]);
				}

				Machine by_steps = start;

				for (int s = 0; s < 128; s++)
					by_steps.dsp.StepOne(given);

				for (int split = 0; split < 128; split++)
				{
					Machine m = start;

					for (int s = 0; s < split; s++)
						m.dsp.StepOne(given);

					m.dsp.RunSample(given);

					char what[96];

					snprintf(what, sizeof(what), "program %d (%d rows) sample %d: RunSample from step %d", index, steps,
						sample, split);
					check(same(m, by_steps), what);
				}

				//the same with a bus read in front of the step RunSample starts at
				for (int split = 1; split < 128; split++)
				{
					Machine m = start, n = start;
					uint16_t word = (uint16_t)rnd.Bits(16);

					for (int s = 0; s < split; s++)
					{
						m.dsp.StepOne(given);
						n.dsp.StepOne(given);
					}

					m.dsp.BusRead(word);
					n.dsp.BusRead(word);
					m.dsp.RunSample(given);

					for (int s = split; s < 128; s++)
						n.dsp.StepOne(given);

					char what[112];

					snprintf(what, sizeof(what), "program %d (%d rows) sample %d: a bus read, then RunSample from step %d",
						index, steps, sample, split);
					check(same(m, n), what);
				}

				start = by_steps;
			}
		}
	}
}

//What a sound generator does in front of step s, call by call and where the hardware has it: the sweep begins 16
//steps before step 0 and its sends go on into the next sample, the last sends of the sweep before land behind
//its start, and a channel that fetches takes the slot of its step
static void events_by_call(AicaDsp& dsp, const AicaDspSampleInputs& in, int s)
{
	if (s == 112)
		dsp.SweepBegin();

	const uint32_t bus = (uint32_t)s & 15;

	if ((in.mixs_sent >> bus) & 1)
	{
		if (s >= 120)
			dsp.MixsSendLate(bus, in.mixs[bus]);
		else if (s >= 112 || s < 64)
			dsp.MixsSend(bus, in.mixs[bus]);
	}

	const uint32_t channel = (((uint32_t)s + 14) >> 1) & 63;

	if (!(s & 1) && ((in.fetch >> channel) & 1))
		dsp.ChannelFetch(in.fetch_word[channel]);
}

//A core with the same byte in all of its state: every field in front of the wave RAM pointer
static void fill_state(AicaDsp& dsp, int byte)
{
	memset(&dsp, byte, offsetof(AicaDsp, ram));
}

static void test_state()
{
	DspRandom rnd(0x57A7E00ull);

	group("state");

	//The blob is those fields, all of their bits and nothing else: a core of all ones saves all ones
	{
		Machine ones;

		fill_state(ones.dsp, 0xFF);

		std::vector<uint8_t> blob = blob_of(ones.dsp);
		size_t set = 0;

		for (size_t i = 8; i < blob.size(); i++)
			set += blob[i] == 0xFF;

		check(blob.size() > 8 && set == blob.size() - 8, "the blob is the state in front of the wave RAM pointer, bit for bit");
	}

	//what the saved states had: a channel in the slot of their step, a sweep that has begun, sends to both MIXS
	//banks, EXTS, samples behind them
	uint64_t states = 0, taken = 0, swept = 0, sent = 0, used = 0;

	for (int index = 0; index < 6; index++)
	{
		Machine a;

		random_machine(a, rnd, index == 5 ? 40 : 128, index & 1);

		AicaDspSampleInputs in;

		random_inputs(in, rnd, 1);

		//Three programs get the events of a sample with the sample, three call by call: only a call leaves a channel
		//in the slot of the next step, or a sweep that has begun, in the state
		const bool calls = index >= 3;
		const AicaDspSampleInputs* given = calls ? nullptr : &in;

		a.dsp.EXTS[0] = (int16_t)(rnd.Bits(16) | 1);
		a.dsp.EXTS[1] = (int16_t)(rnd.Bits(16) | 1);

		//two samples first: each MIXS bank has had its sweep
		for (int s = 0; s < 2 * 128; s++)
		{
			if (calls)
				events_by_call(a.dsp, in, s & 127);

			a.dsp.StepOne(given);
		}

		for (int s = 0; s < 128; s++)
		{
			if (calls)
				events_by_call(a.dsp, in, s);

			std::vector<uint8_t> blob = blob_of(a.dsp);

			states++;
			taken += a.dsp.slot_taken;
			swept += a.dsp.mixs_swept;
			sent += a.dsp.mixs_sent[0] && a.dsp.mixs_sent[1];
			used += a.dsp.samples && a.dsp.EXTS[0] && a.dsp.EXTS[1];

			//The state goes into a core that holds 0 in every field, and into one that holds all ones: whatever a
			//field of the state is, one of the two had something else in it
			Machine b, c;

			fill_state(b.dsp, 0x00);
			fill_state(c.dsp, 0xFF);

			bool loaded = b.dsp.LoadState(blob.data(), (uint32_t)blob.size()) && c.dsp.LoadState(blob.data(), (uint32_t)blob.size());

			b.ram = a.ram;

			char what[96];

			snprintf(what, sizeof(what), "program %d: state in front of step %d", index, s);
			check(loaded && blob_of(b.dsp) == blob && blob_of(c.dsp) == blob, what);

			//both to the end of this sample and through the next: one by steps, one in one go
			Machine a2 = a;

			for (int k = s; k < 128; k++)
				a2.dsp.StepOne(given);

			a2.dsp.RunSample(given);
			b.dsp.RunSample(given);
			b.dsp.RunSample(given);

			snprintf(what, sizeof(what), "program %d: going on from the state in front of step %d", index, s);
			check(same(a2, b), what);

			a.dsp.StepOne(given);
		}
	}

	if (dsp_suite_opt().verbose)
		dsp_note("  state: %llu states, %llu with a channel in the slot, %llu behind SweepBegin, %llu with sends in both banks, %llu in use",
			(unsigned long long)states, (unsigned long long)taken, (unsigned long long)swept, (unsigned long long)sent,
			(unsigned long long)used);

	check(taken > 0 && taken < states, "some of the states have a channel in the slot of their step");
	check(swept > 0 && swept < states, "some of the states are behind a SweepBegin that step 0 has not taken yet");
	check(sent > states / 2 && used == states, "the states have sends in both MIXS banks, EXTS and samples behind them");

	//what is not a state
	Machine m;

	random_machine(m, rnd, 128, false);
	m.dsp.StepOne();

	std::vector<uint8_t> good = blob_of(m.dsp), before = good;

	std::vector<uint8_t> bad = good;
	bad[0] ^= 0x20;
	check(!m.dsp.LoadState(bad.data(), (uint32_t)bad.size()) && blob_of(m.dsp) == before, "a blob with another magic is refused");

	bad = good;
	bad[4]++;
	check(!m.dsp.LoadState(bad.data(), (uint32_t)bad.size()) && blob_of(m.dsp) == before, "a blob of another version is refused");

	bad = good;
	bad[6]++;
	check(!m.dsp.LoadState(bad.data(), (uint32_t)bad.size()) && blob_of(m.dsp) == before, "a blob that names another size is refused");

	check(!m.dsp.LoadState(good.data(), (uint32_t)good.size() - 1) && blob_of(m.dsp) == before, "a blob that is cut short is refused");

	bad = good;
	bad.push_back(0);
	check(!m.dsp.LoadState(bad.data(), (uint32_t)bad.size()) && blob_of(m.dsp) == before, "a blob with a byte too many is refused");

	check(!m.dsp.LoadState(good.data(), 0) && blob_of(m.dsp) == before, "an empty blob is refused");
	check(m.dsp.LoadState(good.data(), (uint32_t)good.size()) && m.dsp.ram == m.ram.data(), "a state loads, and the wave RAM stays the core's");

	//values no register can hold do not get in
	bad = good;

	for (size_t i = 8; i < bad.size(); i++)
		bad[i] = 0xFF;

	check(m.dsp.LoadState(bad.data(), (uint32_t)bad.size()) && m.dsp.step < 128 && m.dsp.TEMP[5] == -1 && m.dsp.MIXS[1][3] == -1
		&& m.dsp.ACC == -1 && m.dsp.FRC_REG == 0x1FFF && m.dsp.ADRS_REG == 0xFFF && m.dsp.nofl_pipe == 3 && m.dsp.mixs_dsp <= 1
		&& m.dsp.COEF[9] == 0xFFF8, "a blob of all ones loads as what the registers can hold");

	m.dsp.RunSample();
	check(m.dsp.step == 0, "and runs");
}

static void test_instances()
{
	DspRandom rnd(0x12E157ull);

	group("instances");

	for (int index = 0; index < 8; index++)
	{
		Machine a, b;
		AicaDspSampleInputs in_a, in_b;

		random_machine(a, rnd, 128, index & 1);
		random_machine(b, rnd, index & 2 ? 60 : 128, !(index & 1));
		random_inputs(in_a, rnd, 1);
		random_inputs(in_b, rnd, 2);

		Machine a_alone = a, b_alone = b;

		for (int n = 0; n < 4; n++)
		{
			a_alone.dsp.RunSample(&in_a);
			b_alone.dsp.RunSample(&in_b);
		}

		//in turns, a step each, then a sample each
		for (int s = 0; s < 2 * 128; s++)
		{
			a.dsp.StepOne(&in_a);
			b.dsp.StepOne(&in_b);
		}

		for (int n = 0; n < 2; n++)
		{
			a.dsp.RunSample(&in_a);
			b.dsp.RunSample(&in_b);
		}

		check(same(a, a_alone), "the first of two cores is where it is alone");
		check(same(b, b_alone), "the second of two cores is where it is alone");
	}
}

static void test_registers()
{
	group("registers");

	Machine m;
	AicaDsp& dsp = m.dsp;
	uint32_t owned = 0;

	for (uint32_t off = 0; off < 0x8000; off += 4)
		owned += AicaDsp::RegOwned(off);

	check(owned == 1 + 128 + 64 + 512 + 256 + 64 + 32 + 16 + 2, "the DSP has 1075 registers");
	check(AicaDsp::RegOwned(0x2804) && AicaDsp::RegOwned(0x3000) && AicaDsp::RegOwned(0x32FC) && !AicaDsp::RegOwned(0x3300)
		&& AicaDsp::RegOwned(0x3400) && AicaDsp::RegOwned(0x3BFC) && !AicaDsp::RegOwned(0x3C00) && AicaDsp::RegOwned(0x4000)
		&& AicaDsp::RegOwned(0x45C4) && !AicaDsp::RegOwned(0x45C8) && !AicaDsp::RegOwned(0x2800) && !AicaDsp::RegOwned(0),
		"from COEF to EXTS, and RBP / RBL");
	check(AicaDsp::RegOwned(0x3001) && AicaDsp::RegOwned(0x703000) && dsp.RegRead(0x3003) == 0, "bits 1:0 and what is above the register area are ignored");

	static const uint32_t patterns[4] = { 0xFFFFFFFF, 0, 0x55555555, 0xAAAAAAAA };

	//what a console reads back of each pattern (caique tests/probe), per kind of register
	for (uint32_t p : patterns)
	{
		dsp.RegWrite(0x3000 + 4 * 5, p);
		check(dsp.RegRead(0x3000 + 4 * 5) == (p & 0xFFF8), "COEF keeps bits 15:3");

		dsp.RegWrite(0x3200 + 4 * 63, p);
		check(dsp.RegRead(0x3200 + 4 * 63) == (p & 0xFFFF), "MADRS keeps 16 bits");

		dsp.RegWrite(0x3400 + 16 * 127 + 12, p);
		check(dsp.RegRead(0x3BFC) == (p & 0xFFFF) && dsp.MPRO[127][3] == (p & 0xFFFF), "MPRO keeps 16 bits");

		dsp.RegWrite(0x4000, p);
		dsp.RegWrite(0x4004, p);
		check(dsp.RegRead(0x4000) == (p & 0xFF) && dsp.RegRead(0x4004) == (p & 0xFFFF), "TEMP keeps 24 bits");
		check(dsp.TEMP[0] == (int32_t)((p & 0xFFFFFF) << 8) >> 8, "TEMP is sign extended in the state");

		dsp.MEMS[31] = 0x123456;
		dsp.RegWrite(0x4400 + 8 * 31, p);
		check(dsp.RegRead(0x4400 + 8 * 31) == 0x56, "the low byte of MEMS is not the CPU's to write");
		dsp.RegWrite(0x4404 + 8 * 31, p);
		check(dsp.RegRead(0x4404 + 8 * 31) == (p & 0xFFFF) && dsp.RegRead(0x4400 + 8 * 31) == 0x56, "the upper 16 bits of MEMS are");

		dsp.RegWrite(0x4500, p);
		dsp.RegWrite(0x4504, p);
		check(dsp.RegRead(0x4500) == (p & 0xF) && dsp.RegRead(0x4504) == (p & 0xFFFF), "MIXS keeps 20 bits");

		dsp.RegWrite(0x4580 + 4 * 15, p);
		check(dsp.RegRead(0x45BC) == (p & 0xFFFF) && dsp.EFREG[15] == (int16_t)p, "EFREG keeps 16 bits");

		dsp.EXTS[0] = 0x1234;
		dsp.RegWrite(0x45C0, p);
		check(dsp.RegRead(0x45C0) == 0 && dsp.EXTS[0] == 0x1234, "EXTS is not written and reads 0");

		dsp.RegWrite(0x2804, p);
		check(dsp.RegRead(0x2804) == 0 && dsp.RBPL == (p & 0xFFFF), "RBP / RBL are stored and read 0");

		dsp.RegWrite(0x3300, p);
		dsp.RegWrite(0x3C00, p);
		dsp.RegWrite(0x45C8, p);
		check(dsp.RegRead(0x3300) == 0 && dsp.RegRead(0x3C00) == 0 && dsp.RegRead(0x45C8) == 0, "offsets between the register files are nothing of the DSP's");
	}

	//A slot is one register of 16 bit or fewer, whichever of its four bytes the offset names: nothing of a write
	//is kept above the register's bits, and there is no byte of a slot to write alone
	dsp.RegWrite(0x3000, 0x12345678);
	dsp.RegWrite(0x3200, 0x12345678);
	dsp.RegWrite(0x3400, 0x12345678);
	dsp.RegWrite(0x4004, 0x12345678);
	check(dsp.RegRead(0x3000) == 0x5678 && dsp.RegRead(0x3200) == 0x5678 && dsp.RegRead(0x3400) == 0x5678 && dsp.RegRead(0x4004) == 0x5678,
		"0x12345678 written to COEF, MADRS, MPRO and the upper part of TEMP reads back 5678");

	dsp.RegWrite(0x4000, 0x1FF);
	dsp.RegWrite(0x4500, 0x1F);
	check(dsp.RegRead(0x4000) == 0xFF && dsp.TEMP[0] == 0x5678FF && dsp.RegRead(0x4500) == 0xF && dsp.RegRead(0x4504) == 0xAAAA,
		"the low part of TEMP is 8 bit and that of MIXS 4 bit: a bit above them is not stored, and is not in the value the DSP has");

	dsp.RegWrite(0x3001, 0xAB);
	dsp.RegWrite(0x3406, 0x1234);
	dsp.RegWrite(0x4007, 0x4321);
	check(dsp.RegRead(0x3000) == 0xA8 && dsp.RegRead(0x3003) == 0xA8 && dsp.MPRO[0][0] == 0x5678 && dsp.MPRO[0][1] == 0x1234
		&& dsp.RegRead(0x3405) == 0x1234 && dsp.TEMP[0] == 0x4321FF,
		"an offset inside a slot is the slot's register: a write to 0x3001 writes COEF0, a read of 0x3003 reads it");
}

static void test_mixs()
{
	group("mixs");

	Machine m;
	AicaDsp& dsp = m.dsp;

	//step 0: ACC = MIXS3 << 4 (X * 4095 / 4096 is not exact: read through Y_REG instead); YRL takes INPUTS as it is
	AicaDspInst inst = nop();

	inst.YRL = 1;
	inst.IRA = 0x23;
	put(dsp, 0, inst);

	auto dsp_reads = [&]() { dsp.RunSample(); return dsp.Y_REG; };

	dsp.SweepBegin();
	dsp.MixsSend(3, 0x100);
	check(dsp_reads() != 0x1000, "a send is not in the bank the DSP reads in the sample of its sweep");
	check(dsp_reads() == 0x1000, "the DSP reads it in the next sample, << 4");
	check(dsp_reads() != 0x1000, "and the bank of the sweep before that in the one after");

	dsp.Reset();
	put(dsp, 0, inst);
	dsp.SweepBegin();
	dsp.MixsSend(3, 0x100);
	dsp.MixsSend(3, 0x23);
	dsp.MixsSend(3, -0x3);
	dsp.RunSample();
	check(dsp_reads() == 0x1200, "the sends of a sweep to a bus add up");

	dsp.SweepBegin();
	dsp.MixsSend(3, 0x7FFFF);
	dsp.MixsSend(3, 1);
	dsp.RunSample();
	check(dsp_reads() == -0x800000, "past 20 bit the sum wraps, it does not saturate");

	dsp.SweepBegin();
	dsp.MixsSend(3, 0x12345678);
	dsp.RunSample();
	check(dsp_reads() == 0x456780, "a single send is cut to 20 bit as well");

	//a bus without a send keeps its value: bank after bank
	dsp.Reset();
	put(dsp, 0, inst);
	dsp.SweepBegin();
	dsp.MixsSend(3, 0x55);
	dsp.RunSample();
	dsp.SweepBegin();
	dsp.MixsSend(3, 0x66);
	dsp.RunSample();

	int32_t seen[4];

	for (int n = 0; n < 4; n++)
	{
		dsp.SweepBegin();
		dsp.MixsSend(4, 1);
		seen[n] = dsp_reads();
	}

	check(seen[0] == 0x660 && seen[1] == 0x550 && seen[2] == 0x660 && seen[3] == 0x550, "a bus no channel sends to keeps its value in both banks");

	//the first send of a sweep replaces what the bank had
	dsp.SweepBegin();
	dsp.MixsSend(3, 0);
	dsp.RunSample();
	check(dsp_reads() == 0, "a send of 0 replaces the value");

	//the last channels of a sweep send behind the start of the next one
	dsp.Reset();
	put(dsp, 0, inst);
	dsp.SweepBegin();
	dsp.MixsSend(3, 0x10);
	dsp.SweepBegin();
	dsp.MixsSendLate(3, 0x20);
	dsp.MixsSend(3, 0x700);
	check(dsp_reads() == 0x300, "a late send adds to the sweep before the running one");
	check(dsp_reads() == 0x7000, "and the running sweep is the next sample's");

	//The sample in which the DSP reads a send that is made for sample 0, the samples behind it sending 0. A send is
	//in the bank the DSP starts to read at the first step 0 behind the next SweepBegin: with the sample's inputs
	//and behind a called SweepBegin that is a sample on, without one it is the step 0 that follows the send
	auto read_in = [&](int form)
	{
		int at = -1, times = 0;

		dsp.Reset();
		put(dsp, 0, inst);

		for (int n = 0; n < 4; n++)
		{
			AicaDspSampleInputs in;

			memset(&in, 0, sizeof(in));
			in.mixs_sent = 1 << 3;
			in.mixs[3] = n == 0 ? 0x100 : 0;

			if (form == 0)
				dsp.RunSample(&in);
			else
			{
				if (form == 1)
					dsp.SweepBegin();

				dsp.MixsSend(3, in.mixs[3]);
				dsp.RunSample();
			}

			if (dsp.Y_REG == 0x1000)
			{
				at = n;
				times++;
			}
		}

		return times == 1 ? at : -1;
	};

	check(read_in(0) == 1, "a send of a sample's inputs is read in the sample after, and in no other");
	check(read_in(1) == 1, "so is a send between SweepBegin and RunSample");
	check(read_in(2) == 0, "a send in front of a RunSample without SweepBegin is read by that RunSample");

	//the CPU has the bank the DSP reads
	dsp.Reset();
	put(dsp, 0, inst);
	dsp.RunSample();
	dsp.RegWrite(0x4500 + 8 * 3, 0x5);
	dsp.RegWrite(0x4504 + 8 * 3, 0x1234);
	check(dsp.RegRead(0x4504 + 8 * 3) == 0x1234 && dsp.RegRead(0x4500 + 8 * 3) == 5, "the CPU reads back what it wrote to MIXS");

	int32_t alt[4];

	for (int n = 0; n < 4; n++)
		alt[n] = dsp_reads();

	check(alt[0] == 0 && alt[1] == 0x123450 && alt[2] == 0 && alt[3] == 0x123450, "the DSP has a CPU write to MIXS in every other sample");

	dsp.RegWrite(0x4504 + 8 * 3, 0x0777);
	dsp.SweepBegin();
	dsp.MixsSend(3, 0x11);
	dsp.RunSample();
	check(dsp_reads() == 0x110, "a channel's send replaces what the CPU wrote to its bus");

	//a core nobody sweeps changes its banks all the same
	dsp.Reset();
	check(dsp.mixs_dsp == 0 && dsp.mixs_fill == 1, "after power on the CPU has bank 0");
	dsp.RunSample();
	check(dsp.mixs_dsp == 1 && dsp.mixs_fill == 0, "a sample without SweepBegin changes the banks over");
	dsp.SweepBegin();
	dsp.RunSample();
	check(dsp.mixs_dsp == 0 && dsp.mixs_fill == 1, "a sample with SweepBegin does it once");
}

//A program of reads and IWTs with NOFL everywhere: MEMS[k] is the word IWT stored, << 8
struct Rules
{
	Machine m;
	AicaDspInst prog[128];

	Rules()
	{
		for (int s = 0; s < 128; s++)
		{
			prog[s] = nop();
			prog[s].NOFL = 1;
		}

		m.dsp.RegWrite(0x2804, 0);

		//word w of the table holds 0x1000 + w
		for (uint32_t w = 0; w < 64; w++)
		{
			m.ram[2 * w] = (uint8_t)w;
			m.ram[2 * w + 1] = 0x10;
			m.dsp.RegWrite(0x3200 + 4 * w, w);
		}
	}

	void Read(int step, int word)
	{
		prog[step].MRD = 1;
		prog[step].TABLE = 1;
		prog[step].MASA = (uint8_t)word;
	}

	void Store(int step, int mems) { prog[step].IWT = 1; prog[step].IWA = (uint8_t)mems; }

	void Load()
	{
		for (int s = 0; s < 128; s++)
			put(m.dsp, s, prog[s]);
	}

	uint32_t Stored(int mems) const { return (uint32_t)(m.dsp.MEMS[mems] >> 8) & 0xFFFF; }
};

static void test_rules()
{
	group("rules");

	{
		//an odd read is stored 2 and 3 steps on, an even one 3 and 4 steps on
		Rules r;

		r.Read(11, 5);
		r.Store(12, 0); r.Store(13, 1); r.Store(14, 2);
		r.Read(30, 6);
		r.Store(31, 3); r.Store(32, 4); r.Store(33, 5); r.Store(34, 6);
		r.Load();
		r.m.dsp.BusRead(0xAAAA);
		r.m.dsp.RunSample();
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

		for (int s = 0; s < 128; s++)
		{
			if (s == 51 || s == 73)
				r.m.dsp.BusRead(0xBEEF);

			r.m.dsp.StepOne();
		}

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
		r.m.dsp.BusRead(0x4321);
		r.m.dsp.RunSample();
		check(r.Stored(0) == 0x4321, "MRD with MWT in one step reads nothing");
		check(r.m.ram[18] == 0 && r.m.ram[19] == 0, "and its MWT writes: SHIFTED, which is 0 here");
	}

	{
		//a channel in the slot: the DSP's write is dropped, its read returns the channel's word
		Rules r;
		int slot = (int)AicaDsp::ChannelStep(20);

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
		r.m.dsp.RunSample(&in);
		check(slot == 26 && AicaDsp::ChannelStep(7) == 0 && AicaDsp::ChannelStep(6) == 126 && AicaDsp::ChannelStep(63) == 112, "channel K has the slot of step 2K - 14");
		check(r.Stored(0) == 0x7777, "a read in a channel's slot returns the channel's word");
		check(r.m.ram[24] == 12 && r.m.ram[25] == 0x10, "a write in a channel's slot is dropped");

		in.fetch = 0;
		r.m.dsp.RunSample(&in);
		check(r.Stored(0) == 0x100A && r.m.ram[24] == 0 && r.m.ram[25] == 0, "without the channels both go through");
		check(r.m.dsp.StepUsesRam(slot) && r.m.dsp.StepUsesRam(slot + 2) && !r.m.dsp.StepUsesRam(slot + 1), "StepUsesRam has the steps with MRD or MWT");
	}

	{
		//the conversion of IWT is chosen by NOFL two steps before it
		Rules r;

		r.m.ram[2 * 20] = 0xFF;
		r.m.ram[2 * 20 + 1] = 0xFF;
		r.Read(41, 20);
		r.Store(43, 0);
		r.Store(44, 1);
		r.prog[42].NOFL = 0;
		r.Load();
		r.m.dsp.RunSample();
		check(r.m.dsp.MEMS[0] == (int32_t)0xFFFFFF00 && r.m.dsp.MEMS[1] == -1, "IWT in step 43 stores the word as it is (NOFL in 41), in step 44 unpacked (none in 42)");
	}

	{
		//ACC of step 127 is what step 0 shifts, EFREG is replaced, MDEC_CT counts down
		Machine m;
		AicaDspInst inst = nop();

		m.dsp.RegWrite(0x4404, 0x0123);
		m.dsp.RegWrite(0x3000 + 4 * 127, 0x1000 << 3);
		inst.XSEL = 1;
		inst.YSEL = 1;
		inst.ZERO = 1;
		put(m.dsp, 127, inst);

		inst = nop();
		inst.SHIFT = 3;
		inst.EWT = 1;
		inst.EWA = 2;
		put(m.dsp, 0, inst);
		inst.NEGB = 0;
		put(m.dsp, 1, inst);

		uint16_t ct = m.dsp.MDEC_CT;

		m.dsp.RunSample();
		check(m.dsp.EFREG[2] == 0, "step 1 writes EFREG after step 0: the later write is what stays");
		m.dsp.RunSample();
		check(m.dsp.ACC != 0 && (uint16_t)(ct - 2) == m.dsp.MDEC_CT && m.dsp.samples == 2, "MDEC_CT counts down once a sample");

		//only step 0 now: it has the ACC that step 127 of the sample before left, -MEMS0
		put(m.dsp, 1, nop());
		m.dsp.RunSample();
		check(m.dsp.EFREG[2] == (int16_t)-0x0123, "step 0 shifts the ACC of step 127");
		check(m.dsp.RegRead(0x4580 + 4 * 2) == 0xFEDD, "a negative output is 16 bit to the CPU: nothing reads above bit 15");

		m.dsp.RegWrite(0x2804, 0x8000);
		ct = m.dsp.MDEC_CT;
		m.dsp.RunSample();
		check(m.dsp.MDEC_CT == (uint16_t)(ct ^ 0x8080), "with bit 15 of 0x2804 a sample leaves MDEC_CT XOR 0x8080");
	}
}

static void test_helpers()
{
	group("helpers");

	//every field through encode and decode, at both ends of its range
	static const struct { size_t offset; uint8_t max; } fields[] =
	{
		{ offsetof(AicaDspInst, TRA), 0x7F }, { offsetof(AicaDspInst, TWT), 1 }, { offsetof(AicaDspInst, TWA), 0x7F },
		{ offsetof(AicaDspInst, XSEL), 1 }, { offsetof(AicaDspInst, YSEL), 3 }, { offsetof(AicaDspInst, IRA), 0x3F },
		{ offsetof(AicaDspInst, IWT), 1 }, { offsetof(AicaDspInst, IWA), 0x1F }, { offsetof(AicaDspInst, TABLE), 1 },
		{ offsetof(AicaDspInst, MWT), 1 }, { offsetof(AicaDspInst, MRD), 1 }, { offsetof(AicaDspInst, EWT), 1 },
		{ offsetof(AicaDspInst, EWA), 0xF }, { offsetof(AicaDspInst, ADRL), 1 }, { offsetof(AicaDspInst, FRCL), 1 },
		{ offsetof(AicaDspInst, SHIFT), 3 }, { offsetof(AicaDspInst, YRL), 1 }, { offsetof(AicaDspInst, NEGB), 1 },
		{ offsetof(AicaDspInst, ZERO), 1 }, { offsetof(AicaDspInst, BSEL), 1 }, { offsetof(AicaDspInst, NOFL), 1 },
		{ offsetof(AicaDspInst, MASA), 0x3F }, { offsetof(AicaDspInst, ADREB), 1 }, { offsetof(AicaDspInst, NXADR), 1 },
	};

	uint16_t all[4] = { 0, 0, 0, 0 };

	for (const auto& f : fields)
	{
		AicaDspInst inst = nop(), back;
		uint16_t w[4];

		((uint8_t*)&inst)[f.offset] = f.max;
		AicaDspEncode(&inst, w);
		AicaDspDecode(w, &back);
		check(memcmp(&inst, &back, sizeof(inst)) == 0 && (w[0] | w[1] | w[2] | w[3]) != 0, "a field is alone in its bits");

		//no two fields share a bit
		check(!(all[0] & w[0]) && !(all[1] & w[1]) && !(all[2] & w[2]) && !(all[3] & w[3]), "a field's bits are no other field's");

		for (int k = 0; k < 4; k++)
			all[k] |= w[k];
	}

	check(all[0] == 0xFFFE && all[1] == 0xFFFE && all[2] == 0xFFFF && all[3] == 0xFF80, "the fields have 55 of the 64 bits");
	check(sizeof(AicaDspStepView) == AICA_DSP_STEP_VIEW_WORDS * sizeof(int32_t), "the view is an array of words");

	//the float format where it is not a plain shift
	check(AicaDspPack(0) == 0x6000 && AicaDspUnpack(0x6000) == 0, "0 is exponent 12, mantissa 0");
	check(AicaDspPack(-1) == 0xE7FF && AicaDspUnpack(0xE7FF) == -1, "-1 is exponent 12, mantissa 7FF");
	check(AicaDspUnpack(0xFFFF) == -1 && AicaDspUnpack(0xF7FF) == -1, "exponents above 12 are read as -1 too");
	check(AicaDspPack(0x7FFFFF) == 0x07FF && AicaDspUnpack(0x07FF) == 0x7FF800, "the largest number loses its low 11 bits");
	check(AicaDspPack(-0x800000) == 0x8000 && AicaDspUnpack(0x8000) == -0x800000, "the smallest is exact");
	check(AicaDspPack(0x7FF) == 0x67FF && AicaDspUnpack(0x67FF) == 0x7FF, "11 bit numbers are exact");
	check(AicaDspPack(0x800) == 0x5800 && AicaDspUnpack(0x5800) == 0x800, "12 bit numbers have a bit that is not stored");
	check(AicaDspUnpack(0x7800) == 0 && AicaDspUnpack(0x7FFF) == 0x7FF, "positive words with exponents above 11 have no such bit");

	uint32_t exact = 0;

	for (int32_t v = -0x1000; v < 0x1000; v++)
		exact += AicaDspUnpack(AicaDspPack(v)) == v;

	check(exact == 0x2000, "13 bit numbers come back as they went in");

	//what a step tells of itself
	Machine m;
	AicaDspInst inst = nop();
	AicaDspStepView view;

	m.dsp.RegWrite(0x4404 + 8 * 2, 0x0400);
	m.dsp.RegWrite(0x3000 + 4 * 3, (uint32_t)(0x800 << 3));
	m.dsp.RegWrite(0x3200 + 4 * 1, 0x30);
	m.dsp.MDEC_CT = 0x1234;

	inst.XSEL = 1;
	inst.IRA = 2;
	inst.YSEL = 1;
	inst.ZERO = 1;
	put(m.dsp, 3, inst);

	inst = nop();
	inst.SHIFT = 3;
	inst.MWT = 1;
	inst.NOFL = 1;
	inst.TABLE = 1;
	inst.MASA = 1;
	put(m.dsp, 4, inst);

	for (int s = 0; s < 3; s++)
		m.dsp.StepOne(nullptr, &view);

	check(view.step == 2 && view.mem_op == AICA_DSP_MEM_NONE && view.MDEC_CT == 0x1234, "the view of an empty step");

	m.dsp.StepOne(nullptr, &view);
	check(view.step == 3 && view.INPUTS == 0x040000 && view.X == 0x040000 && view.Y == 0x800 && view.B == 0 && view.ACC == 0x020000
		&& view.ACC == m.dsp.ACC, "the view of a multiply: INPUTS, X, Y, B, ACC");

	m.dsp.StepOne(nullptr, &view);
	check(view.step == 4 && view.SHIFTED == 0x020000 && view.mem_op == AICA_DSP_MEM_WRITE && view.mem_addr == 0x60
		&& view.mem_data == 0x0200 && m.ram[0x60] == 0x00 && m.ram[0x61] == 0x02, "the view of a write: SHIFTED, the address, the word");

	m.dsp.ChannelFetch(0x5555);
	put(m.dsp, 5, inst);
	m.dsp.StepOne(nullptr, &view);
	check(view.mem_op == AICA_DSP_MEM_LOST && view.slot_taken == 1 && view.latch == 0x5555 && view.latch_copy == 0x5555, "the view of a step in a channel's slot");

	//B is what the multiplier adds: with NEGB, the ACC of the step before taken away
	m.dsp.RegWrite(0x3000 + 4 * 6, (uint32_t)(0x800 << 3));
	inst = nop();
	inst.XSEL = 1;
	inst.IRA = 2;
	inst.YSEL = 1;
	inst.ZERO = 1;
	put(m.dsp, 6, inst);

	inst = nop();
	inst.BSEL = 1;
	inst.NEGB = 1;
	put(m.dsp, 7, inst);

	m.dsp.StepOne(nullptr, &view);
	m.dsp.StepOne(nullptr, &view);
	check(view.step == 7 && view.B == -0x020000 && view.ACC == -0x020000, "the view of a step with NEGB: B is ACC taken away");

	//the address of the view is the byte in the wave RAM: the ring's base is in it, above 64K too
	std::vector<uint8_t> wave(0x200000, 0);
	AicaDsp ring;

	ring.Init(wave.data(), (uint32_t)wave.size());
	ring.RegWrite(0x2804, 0x123);
	ring.RegWrite(0x3200 + 4 * 1, 0x30);
	ring.MDEC_CT = 0x1234;
	wave[0x123 * 2048 + 2 * 0x1264] = 0xCD;
	wave[0x123 * 2048 + 2 * 0x1264 + 1] = 0xAB;

	inst = nop();
	inst.MRD = 1;
	inst.MASA = 1;
	put(ring, 0, inst);
	ring.StepOne(nullptr, &view);
	check(view.mem_op == AICA_DSP_MEM_READ && view.mem_addr == 0x123 * 2048 + 2 * 0x1264 && view.mem_data == 0xABCD
		&& view.latch == 0xABCD, "the view of a read in a ring with RBP 0x123: word 0x30 + MDEC_CT of the ring, as a byte of the wave RAM");

	//a core without wave RAM
	AicaDsp bare;

	bare.Init(nullptr, 0);
	inst = nop();
	inst.MRD = 1;
	inst.TABLE = 1;
	put(bare, 1, inst);
	inst = nop();
	inst.MWT = 1;
	inst.TABLE = 1;
	put(bare, 2, inst);
	bare.BusRead(0x1111);
	bare.RunSample();
	check(bare.latch == 0 && bare.step == 0 && bare.samples == 1, "without wave RAM a read gives 0 and a write is nothing");
	bare.StepOne();
	bare.StepOne();
	bare.StepOne(nullptr, &view);
	check(view.mem_op == AICA_DSP_MEM_WRITE && bare.step == 3, "and the steps run");
}

int main(int argc, char** argv)
{
	static const char* usage =
		"aica_dsp_api [options]\n"
		"The interface of the AICA DSP core: what its calls promise each other";

	DspOptions opt;

	opt.baseline = NULLDC_BASELINE;

	if (!dsp_suite_options(argc, argv, 1, usage, opt) || !dsp_suite_begin("aica.dsp.api", opt))
		return DSP_EXIT_ERROR;

	test_split();
	test_state();
	test_instances();
	test_registers();
	test_mixs();
	test_rules();
	test_helpers();
	group(0);

	return dsp_suite_end();
}
