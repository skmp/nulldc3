#include "dsp_lockstep.h"

#include <cstdio>
#include <cstring>

LockstepEvents::LockstepEvents()
{
	fetch = 0;
	memset(fetch_word, 0, sizeof(fetch_word));
	memset(bus_read, 0, sizeof(bus_read));
	memset(bus_word, 0, sizeof(bus_word));
}

static void lockstep_hook(void* ctx, int step)
{
	((DspLockstep*)ctx)->OnStep(step);
}

DspLockstep::DspLockstep()
{
	model = caique_new();
	caique_refs(model, &m);
	caique_set_hook(model, lockstep_hook, this);

	core_ram.resize(CAIQUE_RAM_SIZE);
	fast_ram.resize(CAIQUE_RAM_SIZE);
	events = 0;
	steps = 0;
	fast_samples = 0;
	reg_reads = 0;
	memset(&cover, 0, sizeof(cover));

	Reset();
}

DspLockstep::~DspLockstep()
{
	caique_delete(model);
}

void DspLockstep::Reset(bool clear_ram)
{
	caique_reset(model, clear_ram);

	if (clear_ram)
	{
		memset(core_ram.data(), 0, core_ram.size());
		memset(fast_ram.data(), 0, fast_ram.size());
	}

	core.Init(core_ram.data(), (uint32_t)core_ram.size());
	fast.Init(fast_ram.data(), (uint32_t)fast_ram.size());

	feed_inputs = false;
	ok = true;
	first.clear();
}

void DspLockstep::SetMdecCt(uint32_t value)
{
	*m.MDEC_CT = value & 0xFFFF;
	core.MDEC_CT = (uint16_t)value;
	fast.MDEC_CT = (uint16_t)value;
}

void DspLockstep::Differs(int step, const char* what, long long model_value, long long core_value)
{
	if (!ok)
		return;

	char text[256];

	snprintf(text, sizeof(text), "sample %llu step %d: %s is %llx in the model, %llx in the core",
		(unsigned long long)*m.samples, step, what, (unsigned long long)model_value, (unsigned long long)core_value);

	ok = false;
	first = text;
}

void DspLockstep::RegWrite(uint32_t off, uint32_t value)
{
	caique_write(model, off, value);

	if (AicaDsp::RegOwned(off))
	{
		core.RegWrite(off, value);
		fast.RegWrite(off, value);
	}
}

uint32_t DspLockstep::RegRead(uint32_t off)
{
	uint32_t value = caique_read(model, off);

	if (AicaDsp::RegOwned(off))
	{
		uint32_t mine = core.RegRead(off);

		if (ok)
			reg_reads++;

		if (mine != value)
		{
			char what[48];

			snprintf(what, sizeof(what), "the CPU's read of %04x", off & 0x7FFC);
			Differs(-1, what, value, mine);
		}
	}

	return value;
}

void DspLockstep::RamWrite32(uint32_t off, uint32_t value)
{
	off &= CAIQUE_RAM_SIZE - 4;

	for (int i = 0; i < 4; i++)
		RamPoke(off + i, (uint8_t)(value >> (8 * i)));
}

void DspLockstep::RamPoke(uint32_t addr, uint8_t value)
{
	addr &= CAIQUE_RAM_SIZE - 1;

	m.ram[addr] = value;
	core_ram[addr] = value;
	fast_ram[addr] = value;
}

void DspLockstep::RamFill(const uint8_t* image)
{
	memcpy(m.ram, image, CAIQUE_RAM_SIZE);
	memcpy(core_ram.data(), image, CAIQUE_RAM_SIZE);
	memcpy(fast_ram.data(), image, CAIQUE_RAM_SIZE);
}

uint32_t DspLockstep::RamRead32(uint32_t off)
{
	uint32_t value = caique_ram_read32(model, off);

	off &= CAIQUE_RAM_SIZE - 4;

	uint32_t mine = core_ram[off] | (core_ram[off + 1] << 8) | (core_ram[off + 2] << 16) | ((uint32_t)core_ram[off + 3] << 24);

	if (mine != value)
		Differs(-1, "a wave RAM word the CPU reads", value, mine);

	//the upper half is what an SH4 read leaves in the DSP's latch
	core.BusRead((uint16_t)(value >> 16));
	fast.BusRead((uint16_t)(value >> 16));

	return value;
}

void DspLockstep::SetExts(int index, int32_t value)
{
	m.EXTS[index] = value;
	core.EXTS[index] = (int16_t)value;
	fast.EXTS[index] = (int16_t)value;
}

void DspLockstep::CompareRam()
{
	if (ok && memcmp(core_ram.data(), fast_ram.data(), CAIQUE_RAM_SIZE) != 0)
	{
		uint32_t at = 0;

		while (core_ram[at] == fast_ram[at])
			at++;

		char what[64];

		snprintf(what, sizeof(what), "RunSample: wave RAM byte %06x (model: the steps)", at);
		Differs(-1, what, core_ram[at], fast_ram[at]);
	}

	if (!ok || memcmp(m.ram, core_ram.data(), CAIQUE_RAM_SIZE) == 0)
		return;

	uint32_t at = 0;

	while (m.ram[at] == core_ram[at])
		at++;

	char what[48];

	snprintf(what, sizeof(what), "wave RAM byte %06x", at);
	Differs(-1, what, m.ram[at], core_ram[at]);
}

//The sound generator's part of the sample is done in the model, or made up by the test: the core gets it
void DspLockstep::BeginSample()
{
	memset(&inputs, 0, sizeof(inputs));

	feed_inputs = !feed_inputs;
	sample_wrote = false;

	if (events)
	{
		//the sums of the sends per bus are what a user with an AicaDspSampleInputs has
		int64_t sum[16] = { 0 };

		for (size_t i = 0; i < events->sends.size(); i++)
		{
			sum[events->sends[i].bus & 15] += events->sends[i].value;
			inputs.mixs_sent |= (uint16_t)(1u << (events->sends[i].bus & 15));
		}

		for (int bus = 0; bus < 16; bus++)
			inputs.mixs[bus] = (int32_t)sum[bus];

		for (int k = 0; k < 64; k++)
			inputs.fetch_word[k] = events->fetch_word[k];

		inputs.fetch = events->fetch;
	}
	else
	{
		//every channel sends to the bus of its ISEL, and the model has the sums in the bank it fills
		for (int ch = 0; ch < 64; ch++)
		{
			uint32_t bus = m.reg[(0x80 * ch + 0x20) >> 2] & 0xF;

			inputs.mixs_sent |= (uint16_t)(1u << bus);
			inputs.mixs[bus] = m.MIXS_bank[*m.samples & 1][bus];
		}

		for (int k = 0; k < 64; k++)
		{
			if (m.coll_claim[k])
				inputs.fetch |= 1ull << k;

			inputs.fetch_word[k] = m.coll_word[k];
		}

		core.EXTS[0] = fast.EXTS[0] = (int16_t)m.EXTS[0];
		core.EXTS[1] = fast.EXTS[1] = (int16_t)m.EXTS[1];
	}

	if (feed_inputs)
		return;

	//Call by call. The test's sends go one by one, several of a sweep to one bus. A sweep without a send is left
	//to the SweepBegin the core does itself at step 0, every other time
	if (events)
	{
		if (!events->sends.empty() || (*m.samples & 2))
			core.SweepBegin();

		for (size_t i = 0; i < events->sends.size(); i++)
			core.MixsSend(events->sends[i].bus, events->sends[i].value);
	}
	else
	{
		core.SweepBegin();

		for (uint32_t bus = 0; bus < 16; bus++)
		{
			if ((inputs.mixs_sent >> bus) & 1)
				core.MixsSend(bus, inputs.mixs[bus]);
		}
	}
}

void DspLockstep::CoreStep(int step)
{
	AicaDspStepView view;

	//a bus read comes in front of the channel's fetch of the same slot, as the model has the two
	if (events && events->bus_read[step])
		core.BusRead(events->bus_word[step]);

	if (!feed_inputs && !(step & 1))
	{
		int channel = ((step + 14) >> 1) & 63;

		if ((inputs.fetch >> channel) & 1)
			core.ChannelFetch(inputs.fetch_word[channel]);
	}

	core.StepOne(feed_inputs ? &inputs : nullptr, &view);

	sample_wrote |= view.mem_op == AICA_DSP_MEM_WRITE || view.mem_op == AICA_DSP_MEM_WRITE_NO_READ;

	Compare(step, view);

	if (step == 127)
		FastSample();
}

/*
	The sample again, on the second core, with RunSample. A bus read belongs in front of its step: the steps up to
	the last one are single steps, and so are the steps up to a point that moves from sample to sample, so that
	RunSample starts at step 0 in half of the samples and anywhere else in the others.
*/
void DspLockstep::FastSample()
{
	int singles = 0;

	if (events)
	{
		for (int s = 0; s < 128; s++)
		{
			if (events->bus_read[s])
				singles = s;
		}
	}

	uint32_t pick = (uint32_t)((fast.samples + 1) * 2654435761u) >> 24;

	if (pick < 128 && (int)pick > singles)
		singles = (int)pick;

	for (int s = 0; s < singles; s++)
	{
		if (events && events->bus_read[s])
			fast.BusRead(events->bus_word[s]);

		fast.StepOne(&inputs);
	}

	//the last bus read can be in front of the step RunSample starts at
	if (events && events->bus_read[singles])
		fast.BusRead(events->bus_word[singles]);

	fast.RunSample(&inputs);

	if (!ok)
		return;

	fast_samples++;

	const char* what = 0;
	long long a = 0, b = 0;

	#define SAME(field) if (!what && core.field != fast.field) { what = #field; a = core.field; b = fast.field; }
	#define SAME_ARRAY(field) if (!what && memcmp(core.field, fast.field, sizeof(core.field)) != 0) { what = #field; }

	SAME_ARRAY(TEMP) SAME_ARRAY(MEMS) SAME_ARRAY(MIXS) SAME_ARRAY(EFREG) SAME_ARRAY(EXTS)
	SAME_ARRAY(COEF) SAME_ARRAY(MADRS) SAME_ARRAY(MPRO) SAME_ARRAY(mixs_sent)
	SAME(RBPL) SAME(MDEC_CT) SAME(ACC) SAME(Y_REG) SAME(FRC_REG) SAME(ADRS_REG) SAME(latch) SAME(latch_copy)
	SAME(memval) SAME(nofl_pipe) SAME(step) SAME(slot_taken) SAME(mixs_dsp) SAME(mixs_fill) SAME(mixs_swept)
	SAME(samples)

	#undef SAME
	#undef SAME_ARRAY

	if (what)
	{
		char text[96];

		snprintf(text, sizeof(text), "RunSample from step %d: %s (model: the steps)", singles, what);
		Differs(127, text, a, b);
		return;
	}

	//what the sample can have written: 64K words from RBP on, as for the steps
	if (sample_wrote)
	{
		uint32_t base = ((core.RBPL & 0xFFFu) * 2048u) & (CAIQUE_RAM_SIZE - 1);
		uint32_t length = 2 * 65536;
		uint32_t part = length < CAIQUE_RAM_SIZE - base ? length : CAIQUE_RAM_SIZE - base;

		if (memcmp(core_ram.data() + base, fast_ram.data() + base, part) != 0
			|| (part < length && memcmp(core_ram.data(), fast_ram.data(), length - part) != 0))
			Differs(127, "RunSample: wave RAM (model: the steps)", 0, 1);
	}
}

void DspLockstep::Compare(int step, const AicaDspStepView& view)
{
	if (!ok)
		return;

	steps++;

	cover.ram_reads += view.mem_op == AICA_DSP_MEM_READ;
	cover.ram_writes += view.mem_op == AICA_DSP_MEM_WRITE || view.mem_op == AICA_DSP_MEM_WRITE_NO_READ;
	cover.no_reads += view.mem_op == AICA_DSP_MEM_WRITE_NO_READ;
	cover.slots_lost += view.mem_op == AICA_DSP_MEM_LOST;
	cover.fetches += view.slot_taken;

	if (m.reg[(0x3404 + 16 * step) >> 2] & 0x40)
	{
		cover.iwt_raw += view.memval_nofl;
		cover.iwt_float += !view.memval_nofl;
	}

	if (events)
	{
		cover.bus_reads += events->bus_read[step];

		if (step == 0)
			cover.sends += events->sends.size();
	}
	else if (step == 0)
	{
		for (uint32_t bus = 0; bus < 16; bus++)
			cover.sends += (inputs.mixs_sent >> bus) & 1;
	}

	if (memcmp(m.TEMP, core.TEMP, sizeof(core.TEMP)) != 0)
	{
		for (int i = 0; i < 128; i++)
		{
			if (m.TEMP[i] != core.TEMP[i])
			{
				char what[24];

				snprintf(what, sizeof(what), "TEMP[%d]", i);
				Differs(step, what, m.TEMP[i], core.TEMP[i]);
				return;
			}
		}
	}

	if (memcmp(m.MEMS, core.MEMS, sizeof(core.MEMS)) != 0)
	{
		for (int i = 0; i < 32; i++)
		{
			if (m.MEMS[i] != core.MEMS[i])
			{
				char what[24];

				snprintf(what, sizeof(what), "MEMS[%d]", i);
				Differs(step, what, m.MEMS[i], core.MEMS[i]);
				return;
			}
		}
	}

	for (int bank = 0; bank < 2; bank++)
	{
		for (int i = 0; i < 16; i++)
		{
			if (m.MIXS_bank[bank][i] != core.MIXS[bank][i])
			{
				char what[24];

				snprintf(what, sizeof(what), "MIXS[%d][%d]", bank, i);
				Differs(step, what, m.MIXS_bank[bank][i], core.MIXS[bank][i]);
				return;
			}
		}
	}

	for (int i = 0; i < 16; i++)
	{
		if (m.EFREG[i] != core.EFREG[i])
		{
			char what[24];

			snprintf(what, sizeof(what), "EFREG[%d]", i);
			Differs(step, what, m.EFREG[i], core.EFREG[i]);
			return;
		}
	}

	if (*m.MDEC_CT != core.MDEC_CT) { Differs(step, "MDEC_CT", *m.MDEC_CT, core.MDEC_CT); return; }
	if (*m.ACC != core.ACC) { Differs(step, "ACC", *m.ACC, core.ACC); return; }
	if (*m.FRC_REG != core.FRC_REG) { Differs(step, "FRC_REG", *m.FRC_REG, core.FRC_REG); return; }
	if (*m.Y_REG != core.Y_REG) { Differs(step, "Y_REG", *m.Y_REG, core.Y_REG); return; }
	if (*m.ADRS_REG != core.ADRS_REG) { Differs(step, "ADRS_REG", *m.ADRS_REG, core.ADRS_REG); return; }
	if (*m.memval != core.memval) { Differs(step, "what IWT stores", *m.memval, core.memval); return; }
	if (*m.latch != core.latch) { Differs(step, "the read latch", *m.latch, core.latch); return; }
	if (*m.cp != core.latch_copy) { Differs(step, "the copy of the read latch", *m.cp, core.latch_copy); return; }

	uint32_t nofl = (uint32_t)(m.nofl_pipe[0] | (m.nofl_pipe[1] << 1));

	if (nofl != core.nofl_pipe) { Differs(step, "NOFL of the last two steps", nofl, core.nofl_pipe); return; }

	//Wave RAM: what a step with MWT can reach is 64K words from RBP on
	uint32_t w2 = m.reg[(0x3408 + 16 * step) >> 2];

	if ((w2 & 0x4000) || view.mem_op == AICA_DSP_MEM_WRITE || view.mem_op == AICA_DSP_MEM_WRITE_NO_READ)
	{
		uint32_t base = ((m.reg[0x2804 >> 2] & 0xFFFu) * 2048u) & (CAIQUE_RAM_SIZE - 1);
		uint32_t length = 2 * 65536;
		uint32_t part = length < CAIQUE_RAM_SIZE - base ? length : CAIQUE_RAM_SIZE - base;

		if (memcmp(m.ram + base, core_ram.data() + base, part) != 0
			|| (part < length && memcmp(m.ram, core_ram.data(), length - part) != 0))
		{
			uint32_t at = base;

			while (m.ram[at] == core_ram[at])
				at = (at + 1) & (CAIQUE_RAM_SIZE - 1);

			char what[48];

			snprintf(what, sizeof(what), "wave RAM byte %06x", at);
			Differs(step, what, m.ram[at], core_ram[at]);
			return;
		}
	}
}

void DspLockstep::OnStep(int step)
{
	if (step == 0)
		BeginSample();
	else
		CoreStep(step - 1);

	//a bus read in the slot of this step: in the model's latch before the step runs
	if (events && events->bus_read[step])
		*m.latch = events->bus_word[step];
}

void DspLockstep::StepChip()
{
	events = 0;

	if (!caique_step(model))
	{
		if (ok)
		{
			ok = false;
			first = "the model's DSP did not announce 128 steps for a sample";
		}

		return;
	}

	//step 127 with what ends the sample: both have counted MDEC_CT
	CoreStep(127);
}

void DspLockstep::StepDsp(const LockstepEvents& ev)
{
	//What the model's sound generator does with the sends of a sweep: a bus that got one has their sum, in 20 bit
	int64_t sum[16] = { 0 };
	uint32_t sent = 0;

	for (size_t i = 0; i < ev.sends.size(); i++)
	{
		sum[ev.sends[i].bus & 15] += ev.sends[i].value;
		sent |= 1u << (ev.sends[i].bus & 15);
	}

	for (int bus = 0; bus < 16; bus++)
	{
		if ((sent >> bus) & 1)
			m.MIXS_bank[*m.samples & 1][bus] = (int32_t)((uint32_t)(sum[bus] & 0xFFFFF) << 12) >> 12;
	}

	for (int k = 0; k < 64; k++)
	{
		m.coll_claim[k] = ((ev.fetch >> k) & 1) != 0;
		m.coll_word[k] = ev.fetch_word[k];
	}

	events = &ev;

	bool announced = caique_dsp_sample(model);

	if (!announced)
	{
		if (ok)
		{
			ok = false;
			first = "the model's DSP did not announce 128 steps for a sample";
		}

		events = 0;
		return;
	}

	CoreStep(127);

	events = 0;
}
