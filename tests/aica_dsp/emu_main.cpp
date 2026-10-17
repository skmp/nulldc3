/*
	aica.dsp.emu.*: the DSP core as the emulator's sound chip drives it (emu_aica.h: the emulator's own AICA
	device, sound generator and DSP device), against what the cut of the M3 round says the wiring is (M3-D5,
	M3-D8; libswirl/hw/aica/aica_mmio.h, the phases of a sample).

	  lockstep   generated programs (the generator of aica_dsp_diff, dsp_gen.h) through the emulator's AICA, with
	             CPU writes of the DSP's registers in every form a CPU has between the samples (16-bit and 32-bit
	             writes of a slot, its bytes one by one, writes of the upper half of a slot with a value other than
	             the low half's, behind a write of the low half and alone, on slots whose value the DSP has moved
	             away from the file's word) and channels keyed so that MIXS carries sends, against a core of the
	             test's own.
	             The test works out what that core is given, never asking the emulator: the CPU's register
	             writes by the rules of the register file (a slot of the DSP gets the file's word of the slot when
	             its low half is written), the sends from what the sample handed to the audio stream (below), the
	             CD-DA pair from the stand-in that gives a pair of its own, not 0, in every sample. The two cores'
	             state blobs are equal behind every sample, and what CPU reads give is what the test's core
	             answers.
	  state      three checks of what the sound chip and its save state do with the core: the mix takes EFREG from
	             the core; the DSP device's Reset is the core's power on; the DSP device's save state is the core's
	             blob, and one the core does not read resets the DSP.

	The sends. The keyed channels have IMXL = DISDL, their pan all to one side, MVOL is 15, and no output of the
	DSP or of the CD-DA is mixed (EFSDL 0): a channel's send is then its direct output on its side, from the same
	sample through the same attenuation (sgc_if.cpp, ChannelEx::Step). The channels on the left send to one bus,
	those on the right to another, so the pair the stream gets is, side by side, the sum of the sends of each bus
	at 16-bit scale; the test's core gets those sums times 16 (the console's scale, MIXS holds the sample times
	16), 0 for every other bus a channel's ISEL names, behind a SweepBegin. Levels are low enough that no sum is
	clipped, and a pair that could have been is a problem of the test.
*/
#include "dsp_gen.h"
#include "dsp_suite.h"
#include "emu_aica.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#ifndef NULLDC_BASELINE
#define NULLDC_BASELINE "baseline.txt"
#endif

static const uint32_t RAM_SIZE = 2 * 1024 * 1024;

static std::vector<uint8_t> blob_of(const AicaDsp& dsp)
{
	std::vector<uint8_t> blob(AicaDsp::StateSize());

	dsp.SaveState(blob.data());

	return blob;
}

//Where two state blobs first differ, as a name of the field
static std::string blob_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
	static const struct { const char* name; uint32_t bytes; } fields[] =
	{
		{ "header", 8 }, { "COEF", 256 }, { "MADRS", 128 }, { "MPRO", 1024 }, { "TEMP", 512 }, { "MEMS", 128 },
		{ "MIXS", 128 }, { "EFREG", 32 }, { "EXTS", 4 }, { "RBPL", 2 }, { "MDEC_CT", 2 }, { "ACC", 4 }, { "Y_REG", 4 },
		{ "FRC_REG", 2 }, { "ADRS_REG", 2 }, { "latch", 2 }, { "latch_copy", 2 }, { "memval", 2 }, { "nofl_pipe", 1 },
		{ "step", 1 }, { "slot_taken", 1 }, { "mixs_dsp", 1 }, { "mixs_fill", 1 }, { "mixs_swept", 1 },
		{ "mixs_sent", 4 }, { "samples", 4 },
	};
	uint32_t at = 0;

	for (const auto& f : fields)
	{
		for (uint32_t i = 0; i < f.bytes; i++, at++)
		{
			if (at < a.size() && at < b.size() && a[at] != b[at])
			{
				char text[96];

				snprintf(text, sizeof(text), "%s, byte %u of it: %02X here, %02X in the test's core", f.name, i, a[at], b[at]);
				return text;
			}
		}
	}

	return "nothing";
}

/*
	aica.dsp.emu.lockstep
*/
namespace lockstep
{
	EmuAica* emu;
	AicaDsp ref;
	std::vector<uint8_t> ref_ram;
	uint8_t file[0x8000];		//the register file as the test's CPU wrote it

	uint64_t reads, reads_ok, byte_writes, upper_writes, full_writes, forwarded, upper_alone;
	uint64_t upper_alone_moved[4];	//TEMP, MEMS, MIXS, EFREG
	std::string first;

	void note_first(const char* fmt, const std::string& what)
	{
		if (first.empty())
		{
			char text[512];

			snprintf(text, sizeof(text), fmt, what.c_str());
			first = text;
		}
	}

	//A write of the CPU through the emulator's AICA, and what the rules of the register file make of it for the
	//test's core: the bytes go into the file; a write in the low half of a slot of the DSP gives the DSP the
	//file's word of the slot
	void write(uint32_t off, uint32_t value, uint32_t size)
	{
		emu->RegWrite(off, value, size);

		uint32_t bytes = size == 1 ? 1 : 2;

		for (uint32_t i = 0; i < bytes; i++)
		{
			uint32_t at = off + i;

			file[at] = (uint8_t)(value >> (8 * i));

			if ((at & 2) == 0 && AicaDsp::RegOwned(at))
			{
				uint32_t slot = at & ~3u;

				ref.RegWrite(slot, file[slot] | (file[slot + 1] << 8));
				forwarded++;
			}
		}
	}

	//A read of the CPU, against the test's core: the DSP's register in the low half of its slot, 0 in the upper
	//half, the file elsewhere
	uint32_t want_byte(uint32_t at)
	{
		if (!AicaDsp::RegOwned(at))
			return file[at];

		uint32_t value = (at & 2) ? 0 : ref.RegRead(at & ~3u);

		return (value >> (8 * (at & 1))) & 0xFF;
	}

	void read(uint32_t off, uint32_t size)
	{
		uint32_t got = emu->RegRead(off, size);
		uint32_t want = want_byte(off);

		if (size != 1)
			want |= want_byte(off + 1) << 8;

		reads++;
		reads_ok += got == want;

		if (got != want)
		{
			char text[96];

			snprintf(text, sizeof(text), "%04X (size %u) reads %04X, the test's core %04X", off, size, got, want);
			note_first("a CPU read: %s", text);
		}
	}

	//A 16-bit register value v into the slot at off, in one of the forms a CPU has
	void put(DspRandom& rnd, uint32_t off, uint16_t v)
	{
		switch (rnd.Below(8))
		{
		case 0:
		case 1:
		case 2:
			write(off, v, 2);
			full_writes++;
			break;

		case 3:
			//a 32-bit write: the AICA takes its low 16 bits
			write(off, v | (rnd.Bits(16) << 16), 4);
			full_writes++;
			break;

		case 4:
			//its two bytes, in either order
			if (rnd.Chance(2))
			{
				write(off, v & 0xFF, 1);
				write(off + 1, v >> 8, 1);
			}
			else
			{
				write(off + 1, v >> 8, 1);
				write(off, v & 0xFF, 1);
			}
			byte_writes += 2;
			break;

		case 5:
			//one of its bytes: the other is what the file has
			if (rnd.Chance(2))
				write(off, v & 0xFF, 1);
			else
				write(off + 1, v >> 8, 1);
			byte_writes++;
			break;

		case 6:
		{
			//the low half, then the upper half of the slot with a value other than the low half's: the DSP keeps
			//the low half's
			uint16_t other = (uint16_t)(v ^ (1 + rnd.Bits(15)));

			write(off, v, 2);
			write(off + 2, other, 2);
			upper_writes++;
			full_writes++;
			break;
		}

		default:
			//the low half, then a byte of the upper half
			write(off, v, 2);
			write(off + 2 + rnd.Below(2), (v ^ 0x5A) & 0xFF, 1);
			upper_writes++;
			full_writes++;
			break;
		}
	}

	//The upper half of the slot at off alone, with no write of its low half in front of it: a 16-bit write at off + 2
	//or one of its two bytes. The file takes it and the DSP nothing (M3-D5). Between two samples the DSP's value of
	//a TEMP, MEMS, MIXS or EFREG slot is often no longer the file's word (the program and the sweep wrote it since):
	//counted are the writes on such a slot, where the DSP given the file's word again would differ
	void put_upper(DspRandom& rnd, uint32_t off, uint16_t v)
	{
		if (off >= 0x4000 && off < 0x45C0)
		{
			AicaDsp given = ref;

			given.RegWrite(off, file[off] | (file[off + 1] << 8));
			upper_alone_moved[off < 0x4400 ? 0 : off < 0x4500 ? 1 : off < 0x4580 ? 2 : 3] += blob_of(given) != blob_of(ref);
		}

		if (rnd.Chance(2))
			write(off + 2, v, 2);
		else
			write(off + 2 + rnd.Below(2), v & 0xFF, 1);

		upper_alone++;
	}

	//Between two samples: writes of DSP registers in every form, reads of them, and of what is next to them
	void cpu_access(DspRandom& rnd, const DspGenProfile& p)
	{
		static const struct { uint32_t base, count; } areas[] =
		{
			{ 0x3000, 128 }, { 0x3200, 64 }, { 0x3400, 512 }, { 0x4000, 256 }, { 0x4400, 64 }, { 0x4500, 32 }, { 0x4580, 16 },
			{ 0x45C0, 2 }, { 0x2804, 1 }, { 0x3300, 64 }, { 0x3C00, 256 }, { 0x45C8, 14 },
		};
		const auto& area = areas[rnd.Below(sizeof(areas) / sizeof(areas[0]))];
		uint32_t off = area.base + 4 * rnd.Below(area.count);

		switch (rnd.Below(6))
		{
		case 0:
		case 1:
			//the test mode of MDEC_CT only where the profile of the program has it
			if (off == 0x2804)
				put(rnd, off, (uint16_t)((rnd.Bits(16) & 0x7FFF) | (ref.RBPL & 0x8000)));
			else
				put(rnd, off, dsp_gen_word(rnd, p, 16));
			break;

		case 2:
			put_upper(rnd, off, (uint16_t)rnd.Bits(16));
			break;

		default:
		{
			static const uint32_t forms[][2] = { { 0, 2 }, { 0, 4 }, { 0, 1 }, { 1, 1 }, { 2, 2 }, { 2, 1 }, { 3, 1 } };
			const auto& form = forms[rnd.Below(7)];

			read(off + form[0], form[1]);
			break;
		}
		}
	}

	struct Counts
	{
		uint64_t programs = 0, programs_ok = 0, samples = 0, samples_ok = 0, sounding = 0, cdda = 0;
	};

	//A program from power on: a new machine
	void run_program(DspRandom& rnd, const DspGenProfile& p, uint32_t samples, Counts& c)
	{
		emu = EmuAica::Create(RAM_SIZE);
		memset(file, 0, sizeof(file));

		//the wave memory: random, the same for the test's core
		for (uint32_t i = 0; i < RAM_SIZE; i += 8)
		{
			uint64_t v = rnd.Next();

			memcpy(emu->Ram() + i, &v, 8);
		}

		ref_ram.assign(emu->Ram(), emu->Ram() + RAM_SIZE);
		ref.Init(ref_ram.data(), RAM_SIZE);

		//MVOL 15: the pair is the mix
		write(0x2800, 0x000F, 2);

		//The channels. Keyed: one to three on the left, sending to bus A, one to three on the right, bus B; IMXL =
		//DISDL, the pan to the side; 16-bit PCM on a loop of random words at TL 0x20 and up, a quarter of full
		//scale at most. The others are not keyed, and send 0 to the bus of their ISEL
		uint32_t bus_a = rnd.Below(16), bus_b = (bus_a + 1 + rnd.Below(15)) & 15;
		uint32_t left = 1 + rnd.Below(3), right = 1 + rnd.Below(3);
		uint32_t mask = 0;
		std::vector<uint32_t> keyed;

		for (uint32_t ch = 0; ch < 64; ch++)
		{
			uint32_t base = 0x80 * ch;
			bool is_left = ch < left, is_right = ch >= 32 && ch < 32 + right;

			if (is_left || is_right)
			{
				uint32_t level = 10 + rnd.Below(6);
				uint32_t sa = 0x20000 + 0x2000 * ch;
				uint32_t isel = is_left ? bus_a : bus_b;

				write(base + 0x04, sa & 0xFFFF, 2);							// SA, low part
				write(base + 0x08, 0, 2);									// LSA
				write(base + 0x0C, 64 + rnd.Below(1024), 2);				// LEA
				write(base + 0x10, 0x001F, 2);								// AR 31, no decay
				write(base + 0x14, 0x001F, 2);								// RR 31
				write(base + 0x18, rnd.Bits(10), 2);						// OCT 0, FNS
				write(base + 0x1C, 0, 2);									// no LFO
				write(base + 0x20, (level << 4) | isel, 2);					// IMXL, ISEL
				write(base + 0x24, (level << 8) | (is_left ? 0x1F : 0x0F), 2);	// DISDL, DIPAN: one side only
				write(base + 0x28, (0x20 + rnd.Below(0x20)) << 8, 2);		// TL
				mask |= 1u << isel;
				keyed.push_back(ch);
				// KYONB, LPCTL, 16-bit PCM, SA high part; keyed on below
				write(base + 0x00, 0x4200 | (sa >> 16), 2);
			}
			else
			{
				uint32_t isel = rnd.Bits(4);

				write(base + 0x20, (rnd.Bits(4) << 4) | isel, 2);
				mask |= 1u << isel;
			}
		}

		//KYONEX: every channel with KYONB starts
		write(0x00, (file[0x00] | (file[0x01] << 8)) | 0x8000, 2);

		//The program, as aica_dsp_diff's random_program makes it, every write in a form of its own
		for (int i = 0; i < 128; i++)
			put(rnd, 0x3000 + 4 * i, dsp_gen_word(rnd, p, 16));

		for (int i = 0; i < 64; i++)
			put(rnd, 0x3200 + 4 * i, dsp_gen_word(rnd, p, 16));

		for (int s = 0; s < 128; s++)
		{
			uint16_t w[4];

			dsp_gen_step(rnd, p, w);

			for (int k = 0; k < 4; k++)
				put(rnd, 0x3400 + 16 * s + 4 * k, w[k]);
		}

		for (int i = 0; i < 128; i++)
		{
			uint32_t v = p.edges ? dsp_gen_edge(rnd, 24) : rnd.Bits(24);

			put(rnd, 0x4000 + 8 * i, v & 0xFF);
			put(rnd, 0x4004 + 8 * i, (v >> 8) & 0xFFFF);
		}

		for (int i = 0; i < 32; i++)
			put(rnd, 0x4404 + 8 * i, ((p.edges ? dsp_gen_edge(rnd, 24) : rnd.Bits(24)) >> 8) & 0xFFFF);

		for (int i = 0; i < 16; i++)
		{
			put(rnd, 0x4500 + 8 * i, rnd.Bits(4));
			put(rnd, 0x4504 + 8 * i, dsp_gen_word(rnd, p, 16));
			put(rnd, 0x4580 + 4 * i, rnd.Bits(16));
		}

		//a ring anywhere, also across the end of the wave memory; the test mode of MDEC_CT in one program of 16
		put(rnd, 0x2804, (uint16_t)(rnd.Bits(12) | (rnd.Bits(2) << 13) | (rnd.Chance(16) ? 0x8000 : 0)));

		bool ok = true;

		for (uint32_t n = 0; n < samples; n++)
		{
			uint64_t pairs = emu->Pairs();

			emu->Sample();

			int16_t l = emu->LastLeft(), r = emu->LastRight();

			if (emu->Pairs() != pairs + 1)
				dsp_problem("a sample handed %llu pairs to the audio stream", (unsigned long long)(emu->Pairs() - pairs));

			if (l == 32767 || l == -32768 || r == 32767 || r == -32768)
				dsp_problem("the pair %d, %d may have been clipped: the sends are not the pair", l, r);

			//the test's core: the sweep's sends, the CD-DA pair, the sample
			int16_t cl, cr;

			EmuCddaPair(n, &cl, &cr);
			ref.SweepBegin();

			for (uint32_t bus = 0; bus < 16; bus++)
			{
				if (mask & (1u << bus))
					ref.MixsSend(bus, bus == bus_a ? l * 16 : bus == bus_b ? r * 16 : 0);
			}

			ref.EXTS[0] = cl;
			ref.EXTS[1] = cr;
			ref.RunSample();

			std::vector<uint8_t> mine = blob_of(*emu->Core()), theirs = blob_of(ref);
			bool equal = mine == theirs;

			c.samples++;
			c.samples_ok += equal;
			c.sounding += l != 0 && r != 0;
			c.cdda += cl != 0 && cr != 0;

			if (!equal)
			{
				char text[64];

				snprintf(text, sizeof(text), "sample %u: ", n);
				note_first("%s", text + blob_difference(mine, theirs));
				ok = false;
			}

			uint32_t accesses = p.cpu ? rnd.Below(p.cpu + 1) : 0;

			for (uint32_t i = 0; i < accesses; i++)
				cpu_access(rnd, p);
		}

		//what the two DSPs wrote into their wave memories
		if (memcmp(emu->Ram(), ref_ram.data(), RAM_SIZE) != 0)
		{
			note_first("%s", "the wave memories differ behind the program");
			ok = false;
		}

		c.programs++;
		c.programs_ok += ok;

		delete emu;
		emu = nullptr;
	}

	int run()
	{
		DspRandom rnd(0x5eed00e1);
		Counts c;

		EmuCddaOn(true);

		for (uint32_t i = 0; i < 1200; i++)
		{
			DspGenProfile p;

			p.sparse = (i & 1) != 0;
			p.edges = p.sparse && (i & 2);
			p.fetches = 0;
			p.bus_reads = 0;
			p.cpu = rnd.Chance(4) ? 0 : 8;

			run_program(rnd, p, 48, c);
		}

		dsp_count("programs", c.programs, c.programs_ok);
		dsp_count("samples", c.samples, c.samples_ok);
		dsp_count("cpu-reads", reads, reads_ok);
		dsp_count("did:sends-both-buses", c.sounding, c.sounding);
		dsp_count("did:cdda", c.cdda, c.cdda);
		dsp_count("did:write-16-32", full_writes, full_writes);
		dsp_count("did:write-byte", byte_writes, byte_writes);
		dsp_count("did:write-upper-half", upper_writes, upper_writes);
		dsp_count("did:write-upper-alone", upper_alone, upper_alone);
		dsp_count("did:upper-alone-moved:TEMP", upper_alone_moved[0], upper_alone_moved[0]);
		dsp_count("did:upper-alone-moved:MEMS", upper_alone_moved[1], upper_alone_moved[1]);
		dsp_count("did:upper-alone-moved:MIXS", upper_alone_moved[2], upper_alone_moved[2]);
		dsp_count("did:upper-alone-moved:EFREG", upper_alone_moved[3], upper_alone_moved[3]);
		dsp_count("did:forwarded", forwarded, forwarded);
		dsp_note("%llu programs, %llu samples compared, %llu CPU reads; %llu samples with sends on both buses%s%s", (unsigned long long)c.programs,
			(unsigned long long)c.samples, (unsigned long long)reads, (unsigned long long)c.sounding, first.empty() ? "" : "\nfirst: ", first.c_str());

		return dsp_suite_end();
	}
}

/*
	aica.dsp.emu.state
*/
namespace state
{
	//What the sound chip's VOLPAN gives a DSP output (sgc_if.cpp): the level of EFSDL, the pan of EFPAN
	void volpan(int32_t value, uint32_t efsdl, uint32_t efpan, int32_t* left, int32_t* right)
	{
		auto lut = [](uint32_t i) { return i == 0 ? 0 : (int32_t)((1 << 15) / pow(2.0, (15 - (int)i) / 2.0)); };

		int32_t temp = (value * lut(efsdl)) >> 15;
		int32_t other = (temp * lut(0xF - (efpan & 0xF))) >> 15;

		if (efpan & 0x10)
		{
			*left = temp;
			*right = other;
		}
		else
		{
			*left = other;
			*right = temp;
		}
	}

	void write_inst(EmuAica* emu, int step, const AicaDspInst& inst)
	{
		uint16_t w[4];

		AicaDspEncode(&inst, w);

		for (int k = 0; k < 4; k++)
			emu->RegWrite(0x3400 + 16 * step + 4 * k, w[k], 2);
	}

	//The program of check 1: every TEMP slot 0x123456; step 0 takes it into ACC (B from TEMP, X * COEF0 = 0),
	//step 1 writes SHIFTED[23:8] into EFREG3: 0x1234
	const uint16_t known = 0x1234;

	void load_known(EmuAica* emu)
	{
		for (int i = 0; i < 128; i++)
		{
			emu->RegWrite(0x4000 + 8 * i, 0x56, 2);
			emu->RegWrite(0x4004 + 8 * i, 0x1234, 2);
		}

		AicaDspInst inst;

		memset(&inst, 0, sizeof(inst));
		inst.YSEL = 1;
		write_inst(emu, 0, inst);

		memset(&inst, 0, sizeof(inst));
		inst.EWT = 1;
		inst.EWA = 3;
		inst.ZERO = 1;
		write_inst(emu, 1, inst);
	}

	//A program that leaves something in every part of the state: random, from the generator
	void load_random(EmuAica* emu, DspRandom& rnd)
	{
		DspGenProfile p = { false, false, 0, 0, 0 };

		for (int i = 0; i < 128; i++)
			emu->RegWrite(0x3000 + 4 * i, rnd.Bits(16), 2);

		for (int i = 0; i < 64; i++)
			emu->RegWrite(0x3200 + 4 * i, rnd.Bits(16), 2);

		for (int s = 0; s < 128; s++)
		{
			uint16_t w[4];

			dsp_gen_step(rnd, p, w);

			for (int k = 0; k < 4; k++)
				emu->RegWrite(0x3400 + 16 * s + 4 * k, w[k], 2);
		}

		for (int i = 0; i < 128; i++)
		{
			emu->RegWrite(0x4000 + 8 * i, rnd.Bits(8), 2);
			emu->RegWrite(0x4004 + 8 * i, rnd.Bits(16), 2);
		}

		emu->RegWrite(0x2804, rnd.Bits(12) | (rnd.Bits(2) << 13), 2);
	}

	int run()
	{
		DspRandom rnd(0x57a7e);

		EmuCddaOn(true);

		//(1) The mix takes EFREG from the core: the pair of a sample with EFSDL 15 differs from the pair with EFSDL 0
		//by the VOLPAN of the core's EFREG, and that EFREG is the one the program wrote
		{
			const uint32_t samples = 8;
			std::vector<int32_t> pairs[2];
			std::vector<int16_t> efreg;
			uint64_t total = 0, same = 0;

			for (int run = 0; run < 2; run++)
			{
				const uint32_t efsdl = run == 0 ? 15 : 0;
				EmuAica* emu = EmuAica::Create(RAM_SIZE);

				emu->RegWrite(0x2800, 0x000F, 2);				// MVOL 15
				emu->RegWrite(0x2000 + 4 * 3, efsdl << 8, 2);	// EFSDL of output 3, EFPAN 0
				load_known(emu);

				for (uint32_t n = 0; n < samples; n++)
				{
					emu->Sample();
					pairs[run].push_back(emu->LastLeft());
					pairs[run].push_back(emu->LastRight());

					if (run == 0)
						efreg.push_back(emu->Core()->EFREG[3]);
				}

				delete emu;
			}

			for (uint32_t n = 0; n < samples; n++)
			{
				int32_t l, r;

				volpan(efreg[n], 15, 0, &l, &r);
				total++;

				bool ok = efreg[n] == (int16_t)known && l != 0 && pairs[0][2 * n] - pairs[1][2 * n] == l && pairs[0][2 * n + 1] - pairs[1][2 * n + 1] == r;

				same += ok;

				if (!ok)
					dsp_note("  sample %u: EFREG3 %04X (the program writes %04X); the pair with EFSDL 15 is %d, %d, with EFSDL 0 %d, %d: the VOLPAN of the core's EFREG is %d, %d",
						n, (uint16_t)efreg[n], known, pairs[0][2 * n], pairs[0][2 * n + 1], pairs[1][2 * n], pairs[1][2 * n + 1], l, r);
			}

			dsp_count("efreg-mix", total, same);
		}

		//(2) A machine reset is the DSP's power on: the DSP device's Reset(false), behind a program that ran, leaves
		//the core's state blob that of a core after Reset() (MDEC_CT 1, TEMP 0, step 0 among it)
		{
			EmuAica* emu = EmuAica::Create(RAM_SIZE);
			AicaDsp fresh = {};

			fresh.Init(emu->Ram(), emu->RamSize());
			load_random(emu, rnd);

			for (int n = 0; n < 5; n++)
				emu->Sample();

			const bool ran = blob_of(*emu->Core()) != blob_of(fresh) && emu->Core()->MDEC_CT != 1;

			emu->DspReset();

			const AicaDsp* core = emu->Core();
			bool temp_zero = true;

			for (int i = 0; i < 128; i++)
				temp_zero = temp_zero && core->TEMP[i] == 0;

			const bool reset = blob_of(*core) == blob_of(fresh) && core->MDEC_CT == 1 && temp_zero && core->step == 0;

			if (!ran || !reset)
				dsp_note("  the program %s; behind the reset: %s; MDEC_CT %u, TEMP %s, step %u", ran ? "ran" : "left the core at its power on",
					blob_difference(blob_of(*core), blob_of(fresh)).c_str(), core->MDEC_CT, temp_zero ? "0" : "not 0", core->step);

			dsp_count("reset", 2, (uint64_t)ran + reset);
			delete emu;
		}

		//(3) The save state of the DSP device is the core's blob: serialize, then a CPU write of a DSP register and
		//a sample, then unserialize gives the core's state of before
		{
			EmuAica* emu = EmuAica::Create(RAM_SIZE);

			load_random(emu, rnd);

			for (int n = 0; n < 5; n++)
				emu->Sample();

			const std::vector<uint8_t> before = blob_of(*emu->Core());
			const std::vector<uint8_t> saved = emu->DspSave();

			emu->RegWrite(0x3000 + 4 * 7, emu->Core()->COEF[7] ^ 0x5558, 2);
			emu->Sample();

			const bool moved = blob_of(*emu->Core()) != before;

			emu->DspLoad(saved);

			const std::vector<uint8_t> after = blob_of(*emu->Core());
			const bool back = after == before;

			if (!moved || !back || saved.size() != AicaDsp::StateSize())
				dsp_note("  the save state has %zu bytes (the core's blob %u); the write and the sample %s the core; behind the load: %s", saved.size(),
					AicaDsp::StateSize(), moved ? "changed" : "did not change", blob_difference(after, before).c_str());

			dsp_count("save-load", 3, (uint64_t)moved + back + (saved.size() == AicaDsp::StateSize()));

			//a save state whose DSP part the core does not read (another header): the DSP is reset, and the load
			//goes on behind it
			std::vector<uint8_t> refused = saved;
			AicaDsp fresh = {};

			fresh.Init(emu->Ram(), emu->RamSize());
			refused[0] ^= 0xFF;
			emu->DspLoad(refused);

			const bool reset = blob_of(*emu->Core()) == blob_of(fresh);

			if (!reset)
				dsp_note("  behind a load of a DSP state with another header the core is not at its power on: %s",
					blob_difference(blob_of(*emu->Core()), blob_of(fresh)).c_str());

			dsp_count("refused-blob", 1, reset);
			delete emu;
		}

		return dsp_suite_end();
	}
}

int main(int argc, char** argv)
{
	static const char* usage =
		"aica_dsp_emu lockstep|state [options]\n"
		"The DSP core as the emulator's own sound chip drives it: generated programs and CPU writes against a core\n"
		"of the test's own (lockstep), the mix, the reset and the save state of the DSP device (state)";

	DspOptions opt;

	opt.baseline = NULLDC_BASELINE;

	std::string mode = argc > 1 ? argv[1] : "";

	if ((mode != "lockstep" && mode != "state") || !dsp_suite_options(argc, argv, 2, usage, opt))
	{
		fprintf(stderr, "%s\n", usage);
		return DSP_EXIT_ERROR;
	}

	std::string name = "aica.dsp.emu." + mode;

	if (!dsp_suite_begin(name.c_str(), opt))
		return DSP_EXIT_ERROR;

	return mode == "lockstep" ? lockstep::run() : state::run();
}
