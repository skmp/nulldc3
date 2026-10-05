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
	  fields      a state with bits in its fields that their registers do not have: the CPU's reads, the sends, the
	              steps and RunSample do what they do on the state with every field cut to its bits
	  helpers     decode and encode, the float format at its corners, the view of a step, a core without wave RAM
	  shifter, rules, mixs
	              the known-answer programs (dsp_known.cpp)
*/
#include "dsp_known.h"
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

//Every field of the state with bits that its register does not have: any bits, all of them, or the largest and
//the smallest number of the field's type
static void beyond_the_bits(AicaDsp& dsp, DspRandom& rnd, int kind)
{
	auto bits = [&](int width)
	{
		const uint32_t all = 0xFFFFFFFFu >> (32 - width);

		return kind == 0 ? rnd.Bits(width) : kind == 1 ? all : rnd.Chance(2) ? all >> 1 : (all >> 1) + 1;
	};

	for (int32_t& v : dsp.TEMP) v = (int32_t)bits(32);
	for (int32_t& v : dsp.MEMS) v = (int32_t)bits(32);
	for (auto& bank : dsp.MIXS) for (int32_t& v : bank) v = (int32_t)bits(32);
	for (uint16_t& v : dsp.COEF) v = (uint16_t)bits(16);

	dsp.ACC = (int32_t)bits(32);
	dsp.Y_REG = (int32_t)bits(32);
	dsp.FRC_REG = (uint16_t)bits(16);
	dsp.ADRS_REG = (uint16_t)bits(16);
	dsp.nofl_pipe = (uint8_t)bits(8);
	dsp.step = (uint8_t)bits(8);
	dsp.slot_taken = (uint8_t)bits(8);
	dsp.mixs_dsp = (uint8_t)bits(8);
	dsp.mixs_fill = (uint8_t)bits(8);
	dsp.mixs_swept = (uint8_t)bits(8);
}

//The machine with every field cut to the bits its register has: what LoadState makes of the state
static Machine cut_of(const Machine& m)
{
	Machine cut = m;
	std::vector<uint8_t> blob = blob_of(m.dsp);

	cut.dsp.LoadState(blob.data(), (uint32_t)blob.size());

	return cut;
}

static void test_fields()
{
	DspRandom rnd(0xF1E1D5ull);

	group("fields");

	uint64_t beyond = 0;

	for (int index = 0; index < 96; index++)
	{
		Machine any;
		AicaDspSampleInputs in;

		random_machine(any, rnd, index % 4 == 3 ? 60 : 128, index & 1);
		random_inputs(in, rnd, index % 3);
		beyond_the_bits(any.dsp, rnd, index % 3);

		//a sample in two gets no inputs: the sweep is then the state's to begin, and the slot of the first step
		//the state's to give away
		const AicaDspSampleInputs* given = index & 2 ? &in : nullptr;

		Machine cut = cut_of(any);

		beyond += blob_of(cut.dsp) != blob_of(any.dsp);

		bool reads = true;

		for (uint32_t off = 0; off < 0x8000; off += 4)
			reads &= any.dsp.RegRead(off) == cut.dsp.RegRead(off);

		char what[128];

		snprintf(what, sizeof(what), "state %d: the CPU reads what it reads of the state cut to its bits", index);
		check(reads, what);

		//a send to each bank
		for (Machine* m : { &any, &cut })
		{
			m->dsp.MixsSend((uint32_t)index & 15, in.mixs[0]);
			m->dsp.MixsSendLate((uint32_t)(index >> 2) & 15, in.mixs[1]);
		}

		//to the end of the running sample and through the next one: step by step with the views, and in one go
		Machine in_one_go = any;
		const int left = 128 - cut.dsp.step;
		bool views = true;

		for (int s = 0; s < left + 128; s++)
		{
			AicaDspStepView of_any, of_cut;

			any.dsp.StepOne(given, &of_any);
			cut.dsp.StepOne(given, &of_cut);
			views &= memcmp(&of_any, &of_cut, sizeof(of_any)) == 0;
		}

		in_one_go.dsp.RunSample(given);
		in_one_go.dsp.RunSample(given);

		snprintf(what, sizeof(what), "state %d: %d steps compute what they compute on the state cut to its bits", index, left + 128);
		check(views && same(cut_of(any), cut), what);

		snprintf(what, sizeof(what), "state %d: RunSample leaves what it leaves on the state cut to its bits", index);
		check(same(cut_of(in_one_go), cut), what);
	}

	check(beyond == 96, "every one of the states has bits that its registers do not have");
}

//The known-answer programs on the core called itself
struct NativeMachine : KnownMachine
{
	AicaDsp dsp;
	std::vector<uint8_t> ram;
	AicaDspSampleInputs inputs;
	bool inputs_on;

	const AicaDspSampleInputs* Given() const { return inputs_on ? &inputs : nullptr; }

	void Create(uint32_t ram_size) override
	{
		ram.assign(ram_size, 0);
		dsp.Init(ram.data(), ram_size);
		inputs_on = false;
	}

	void Reset() override { dsp.Reset(); }
	void RegWrite(uint32_t off, uint32_t value) override { dsp.RegWrite(off, value); }
	uint32_t RegRead(uint32_t off) override { return dsp.RegRead(off); }
	void Set(uint32_t index, uint32_t value) override { dsp_known_set(dsp, index, value); }
	uint32_t Get(uint32_t index) override { return dsp_known_get(dsp, index); }

	void RamWrite(uint32_t addr, uint32_t word) override
	{
		ram[addr] = (uint8_t)word;
		ram[addr + 1] = (uint8_t)(word >> 8);
	}

	uint32_t RamRead(uint32_t addr) override { return (uint32_t)(ram[addr] | (ram[addr + 1] << 8)); }

	uint32_t RamUsed() override
	{
		uint32_t used = 0;

		for (uint8_t byte : ram)
			used += byte != 0;

		return used;
	}

	void Inputs(const AicaDspSampleInputs* in) override
	{
		inputs_on = in != nullptr;

		if (in)
			inputs = *in;
	}

	void Steps(uint32_t count) override
	{
		for (uint32_t i = 0; i < count; i++)
			dsp.StepOne(Given());
	}

	void RunSample() override { dsp.RunSample(Given()); }
	void SweepBegin() override { dsp.SweepBegin(); }
	void MixsSend(uint32_t bus, int32_t value) override { dsp.MixsSend(bus, value); }
	void MixsSendLate(uint32_t bus, int32_t value) override { dsp.MixsSendLate(bus, value); }
	void ChannelFetch(uint32_t word) override { dsp.ChannelFetch((uint16_t)word); }
	void BusRead(uint32_t word) override { dsp.BusRead((uint16_t)word); }
	void Group(const char* name) override { group(name); }
	void Check(bool holds, const char* what) override { check(holds, what); }
};

static void test_known()
{
	NativeMachine machine;

	dsp_known_answers(machine);
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

	//which steps take the wave RAM slot
	inst = nop();
	inst.MRD = 1;
	put(ring, 8, inst);
	inst.MRD = 0;
	inst.MWT = 1;
	put(ring, 10, inst);
	check(ring.StepUsesRam(0) && ring.StepUsesRam(8) && !ring.StepUsesRam(9) && ring.StepUsesRam(10) && ring.StepUsesRam(128 + 10),
		"StepUsesRam has the steps with MRD or MWT");

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
	test_fields();
	test_helpers();
	test_known();
	group(0);

	return dsp_suite_end();
}
