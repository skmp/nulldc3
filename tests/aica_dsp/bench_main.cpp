/*
	aica_dsp_bench: how many samples a second the DSP core runs, on three programs.

	  reverb   the shape of a reverb as games load them: 96 steps, an even step multiplies and accumulates, an odd
	           step reads a delay tap from the ring in the float format and stores the tap read before, with a
	           write to the ring, to TEMP or to EFREG where the taps of a comb or an all pass meet
	  full     all 128 steps busy: the reverb's steps, repeated
	  idle     no program: 128 empty steps, which still compute

	The sound generator's part of a sample is in the loop as well: a sweep with 16 sends.
	Not a test: the numbers depend on the machine. The core's README.md has the ones of the machine it was
	written on.
*/
#include "hw/aica/dsp_core/aica_dsp_core.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void put(AicaDsp& dsp, int step, const AicaDspInst& inst)
{
	uint16_t w[4];

	AicaDspEncode(&inst, w);

	for (int k = 0; k < 4; k++)
		dsp.RegWrite(0x3400 + 16 * step + 4 * k, w[k]);
}

static void load_reverb(AicaDsp& dsp, int steps)
{
	for (int i = 0; i < 64; i++)
		dsp.RegWrite(0x3200 + 4 * i, 0x0800 + 0x3F1 * i);

	for (int s = 0; s < steps; s++)
	{
		AicaDspInst inst;
		int tap = (s / 2) % 24;

		memset(&inst, 0, sizeof(inst));
		dsp.RegWrite(0x3000 + 4 * s, (uint32_t)(((s * 2731) & 0x1FFF) << 3));

		inst.YSEL = 1;
		inst.BSEL = 1;

		if (!(s & 1))
		{
			//a product of a tap or of an input, added to ACC or starting it
			inst.XSEL = 1;
			inst.IRA = (uint8_t)((s % 16) == 0 ? 0x20 + (s / 16) % 2 : tap);
			inst.ZERO = (s % 8) == 0;
			inst.NEGB = (s % 16) == 8;
		}
		else
		{
			//a tap in, the one before it into MEMS, and what the block computed out
			inst.MRD = (s % 16) != 15;
			inst.MWT = (s % 16) == 15;
			inst.MASA = (uint8_t)tap;
			inst.NXADR = (s % 4) == 1;
			inst.IWT = (s % 16) != 1;
			inst.IWA = (uint8_t)((tap + 23) % 24);
			inst.TWT = (s % 16) == 11;
			inst.TWA = (uint8_t)(s / 16);
			inst.TRA = (uint8_t)(s / 16);
			inst.EWT = (s % 48) == 47;
			inst.EWA = (uint8_t)(s / 48);
			inst.SHIFT = (uint8_t)((s % 48) == 47 ? 1 : 0);
		}

		put(dsp, s, inst);
	}
}

static void run(const char* name, int steps, uint32_t samples)
{
	std::vector<uint8_t> ram(2u << 20);
	AicaDsp dsp;

	dsp.Init(ram.data(), (uint32_t)ram.size());
	dsp.RegWrite(0x2804, (0x100000 >> 11) | (3 << 13));
	load_reverb(dsp, steps);

	AicaDspSampleInputs in;

	memset(&in, 0, sizeof(in));
	in.mixs_sent = 0xFFFF;

	uint32_t noise = 0x12345;
	int64_t sum = 0;
	auto start = std::chrono::steady_clock::now();

	for (uint32_t n = 0; n < samples; n++)
	{
		for (int bus = 0; bus < 16; bus++)
		{
			noise = noise * 1664525u + 1013904223u;
			in.mixs[bus] = (int32_t)(noise >> 12) - 0x80000;
		}

		dsp.RunSample(&in);
		sum += dsp.EFREG[0] + dsp.EFREG[1];
	}

	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	double rate = samples / seconds;

	printf("%-7s %3d steps: %10.0f samples/s, %6.1f x 44100 Hz, %5.2f ns a step   (check %lld)\n", name, steps, rate,
		rate / 44100.0, seconds * 1e9 / ((double)samples * 128), (long long)sum);
}

//aica_dsp_bench [samples] [program]
int main(int argc, char** argv)
{
	uint32_t samples = argc > 1 ? (uint32_t)strtoul(argv[1], 0, 0) : 2000000;
	const char* only = argc > 2 ? argv[2] : "";

	static const struct { const char* name; int steps; } programs[] = { { "reverb", 96 }, { "full", 128 }, { "idle", 0 } };

	for (const auto& program : programs)
	{
		if (!*only || strcmp(only, program.name) == 0)
			run(program.name, program.steps, samples);
	}

	return 0;
}
