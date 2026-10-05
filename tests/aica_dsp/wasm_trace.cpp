/*
	aica_dsp_wasm_trace: what aica.dsp.wasm replays.

	Runs programs on the DSP core through its C interface (hw/aica/dsp_core/wasm/aica_dsp_capi.h), compiled for
	this machine, and writes every call it makes into a trace, with what the core answered: the registers the CPU
	reads, and digests of the state blob, of the wave RAM and of the views of the single steps. wasm_test.mjs
	makes the same calls on aica_dsp_core.wasm through its loader and has to get the same answers.

	The trace is 32 bit little endian words: "ADTR", a version, the size of the wave RAM, then operations, each
	its number and its arguments (the T_ list below, which wasm_test.mjs has as well).
*/
#include "hw/aica/dsp_core/wasm/aica_dsp_capi.h"
#include "hw/aica/dsp_core/aica_dsp_core.h"
#include "dsp_suite.h"

#include <cstdio>
#include <cstring>
#include <vector>

enum
{
	T_REG_WRITE = 1,		//off, value
	T_REG_POKE = 2,			//off, value
	T_RAM_FILL = 3,			//seed: every byte of the wave RAM from a xorshift32 stream
	T_INPUTS = 4,			//on, sent, fetch low, fetch high, 16 sums, 64 words
	T_STEPS = 5,			//count: single steps, the view of each goes into the view digest
	T_RUN_SAMPLES = 6,		//count
	T_RUN = 7,				//count, 4 words of stop bits; expects: steps done
	T_BUS_READ = 8,			//word
	T_CHANNEL_FETCH = 9,	//word
	T_SWEEP_BEGIN = 10,
	T_MIXS_SEND = 11,		//bus, value
	T_MIXS_SEND_LATE = 12,	//bus, value
	T_SAVE_LOAD = 13,		//the state out, a reset, the state back in
	T_SET = 14,				//index, value
	T_EXTS = 15,			//index, value: through the array
	T_CHECK = 16,			//expects: digest of the state, of the wave RAM, of the views since the last check
	T_REG_READ = 17,		//off; expects: what the CPU reads, what a debugger reads
	T_GET = 18,				//index; expects: value
	T_RESET = 19,
	T_END = 20,
};

#define TRACE_RAM_SIZE 0x10000u

static std::vector<uint32_t> trace;
static AicaDspHandle* h;
static uint32_t view_digest, checks;

static uint32_t fnv(uint32_t hash, const uint8_t* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		hash = (hash ^ data[i]) * 16777619u;

	return hash;
}

static void op(uint32_t number) { trace.push_back(number); }
static void arg(uint32_t value) { trace.push_back(value); }

static void reg_write(uint32_t off, uint32_t value)
{
	op(T_REG_WRITE); arg(off); arg(value);
	aica_dsp_reg_write(h, off, value);
}

static void reg_poke(uint32_t off, uint32_t value)
{
	op(T_REG_POKE); arg(off); arg(value);
	aica_dsp_reg_poke(h, off, value);
}

static void ram_fill(uint32_t seed)
{
	op(T_RAM_FILL); arg(seed);

	uint8_t* ram = aica_dsp_ram(h);
	uint32_t x = seed;

	for (uint32_t i = 0; i < TRACE_RAM_SIZE; i++)
	{
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		ram[i] = (uint8_t)x;
	}
}

static void inputs(DspRandom& rnd, bool on, int fetches)
{
	uint32_t sent = rnd.Bits(16);
	uint32_t low = fetches == 2 ? 0xFFFFFFFFu : fetches == 1 ? rnd.Bits(32) & rnd.Bits(32) & rnd.Bits(32) : 0;
	uint32_t high = fetches == 2 ? 0xFFFFFFFFu : fetches == 1 ? rnd.Bits(32) & rnd.Bits(32) & rnd.Bits(32) : 0;

	op(T_INPUTS); arg(on); arg(sent); arg(low); arg(high);

	for (int i = 0; i < 16; i++)
	{
		int32_t sum = (int32_t)(rnd.Bits(24) << 8) >> 8;

		arg((uint32_t)sum);
		aica_dsp_inputs_mixs(h)[i] = sum;
	}

	for (int k = 0; k < 64; k++)
	{
		uint32_t word = rnd.Bits(16);

		arg(word);
		aica_dsp_inputs_fetch_words(h)[k] = (uint16_t)word;
	}

	aica_dsp_set_inputs(h, on, sent, low, high);
}

static void steps(uint32_t count)
{
	op(T_STEPS); arg(count);

	for (uint32_t i = 0; i < count; i++)
	{
		aica_dsp_step(h);
		view_digest = fnv(view_digest, (const uint8_t*)aica_dsp_view(h), AICA_DSP_STEP_VIEW_WORDS * 4);
	}
}

static void run_samples(uint32_t count)
{
	op(T_RUN_SAMPLES); arg(count);

	for (uint32_t i = 0; i < count; i++)
		aica_dsp_run_sample(h);
}

static void run(uint32_t count, const uint8_t* stops)
{
	op(T_RUN); arg(count);

	for (int w = 0; w < 4; w++)
	{
		uint32_t bits = 0;

		for (int i = 0; i < 32; i++)
			bits |= (uint32_t)(stops[32 * w + i] != 0) << i;

		arg(bits);
	}

	arg(aica_dsp_run(h, count, stops));
}

static void check()
{
	std::vector<uint8_t> blob(aica_dsp_state_size());

	aica_dsp_save_state(h, blob.data());

	op(T_CHECK);
	arg(fnv(2166136261u, blob.data(), blob.size()));
	arg(fnv(2166136261u, aica_dsp_ram(h), TRACE_RAM_SIZE));
	arg(view_digest);

	view_digest = 2166136261u;
	checks++;
}

static void reg_read(uint32_t off)
{
	op(T_REG_READ); arg(off); arg(aica_dsp_reg_read(h, off)); arg(aica_dsp_reg_peek(h, off));
	checks++;
}

static void get(uint32_t index)
{
	op(T_GET); arg(index); arg((uint32_t)aica_dsp_get(h, index));
	checks++;
}

static void program(DspRandom& rnd, int rows, bool sparse)
{
	op(T_RESET);
	aica_dsp_reset(h);
	ram_fill(rnd.Bits(32) | 1);

	for (int i = 0; i < 128; i++)
	{
		reg_write(0x3000 + 4 * i, rnd.Bits(16));
		reg_write(0x4000 + 8 * i, rnd.Bits(8));
		reg_write(0x4004 + 8 * i, rnd.Bits(16));
	}

	for (int i = 0; i < 64; i++)
		reg_write(0x3200 + 4 * i, rnd.Bits(16));

	for (int i = 0; i < 32; i++)
	{
		reg_write(0x4404 + 8 * i, rnd.Bits(16));
		reg_poke(0x4400 + 8 * i, rnd.Bits(8));
	}

	for (int s = 0; s < rows; s++)
	{
		for (int k = 0; k < 4; k++)
		{
			uint32_t word = rnd.Bits(16);

			if (sparse && k == 2)
				word &= rnd.Bits(16) | 0x0030;

			reg_write(0x3400 + 16 * s + 4 * k, word);
		}
	}

	reg_write(0x2804, rnd.Bits(15) | (rnd.Chance(8) ? 0x8000 : 0));

	op(T_SET); arg(AICA_DSP_REG_MDEC_CT);
	uint32_t ct = rnd.Bits(16);
	arg(ct);
	aica_dsp_set(h, AICA_DSP_REG_MDEC_CT, (int32_t)ct);
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "aica_dsp_wasm_trace <trace file>\n");
		return 2;
	}

	static const int rows[] = { 128, 128, 127, 126, 100, 64, 17, 2, 0 };

	DspRandom rnd(0x7ACE00ull);

	trace.push_back(0x52544441);	//"ADTR"
	trace.push_back(1);
	trace.push_back(TRACE_RAM_SIZE);

	h = aica_dsp_create(TRACE_RAM_SIZE);
	view_digest = 2166136261u;

	int index = 0;

	for (int round = 0; round < 12; round++)
	{
		for (int count : rows)
		{
			program(rnd, count, index % 3 != 0);
			index++;

			for (int n = 0; n < 5; n++)
			{
				bool calls = n == 3;

				inputs(rnd, !calls, (index + n) % 3);

				op(T_EXTS); arg(0); uint32_t e0 = rnd.Bits(16); arg(e0);
				aica_dsp_exts(h)[0] = (int16_t)e0;
				op(T_EXTS); arg(1); uint32_t e1 = rnd.Bits(16); arg(e1);
				aica_dsp_exts(h)[1] = (int16_t)e1;

				if (calls)
				{
					//a sample with its events call by call
					op(T_SWEEP_BEGIN);
					aica_dsp_sweep_begin(h);

					for (int i = 0; i < 4; i++)
					{
						uint32_t bus = rnd.Bits(4), value = rnd.Bits(24);

						op(i == 3 ? T_MIXS_SEND_LATE : T_MIXS_SEND); arg(bus); arg(value);

						if (i == 3)
							aica_dsp_mixs_send_late(h, bus, (int32_t)value);
						else
							aica_dsp_mixs_send(h, bus, (int32_t)value);
					}

					for (int part = 0; part < 4; part++)
					{
						uint32_t word = rnd.Bits(16);

						if (part & 1)
						{
							op(T_CHANNEL_FETCH); arg(word);
							aica_dsp_channel_fetch(h, word);
						}
						else
						{
							op(T_BUS_READ); arg(word);
							aica_dsp_bus_read(h, word);
						}

						steps(32);
					}
				}
				else if (n == 0)
					run_samples(1);
				else if (n == 1)
				{
					//single steps, then the rest in one go, with the state out and in again in between
					steps(1 + rnd.Below(127));
					op(T_SAVE_LOAD);

					std::vector<uint8_t> blob(aica_dsp_state_size());

					aica_dsp_save_state(h, blob.data());
					aica_dsp_reset(h);
					aica_dsp_load_state(h, blob.data(), (uint32_t)blob.size());
					run_samples(1);
				}
				else if (n == 2)
				{
					//up to breakpoints
					uint8_t stops[128];

					for (int i = 0; i < 128; i++)
						stops[i] = rnd.Chance(24);

					for (int k = 0; k < 6; k++)
						run(1 + rnd.Below(200), stops);

					run_samples(1);
				}
				else
					run_samples(3);

				check();

				for (int i = 0; i < 6; i++)
				{
					static const uint32_t bases[] = { 0x3000, 0x3200, 0x3400, 0x4000, 0x4400, 0x4500, 0x4580, 0x45C0, 0x2804, 0x2800 };
					static const uint32_t counts[] = { 128, 64, 512, 256, 64, 32, 16, 2, 1, 1 };
					uint32_t area = rnd.Below(10);

					reg_read(bases[area] + 4 * rnd.Below(counts[area]));
				}

				get(rnd.Below(AICA_DSP_REG_COUNT));
			}
		}
	}

	op(T_END);
	aica_dsp_destroy(h);

	FILE* f = fopen(argv[1], "wb");

	if (!f)
	{
		fprintf(stderr, "cannot write %s\n", argv[1]);
		return 2;
	}

	for (uint32_t word : trace)
	{
		uint8_t bytes[4] = { (uint8_t)word, (uint8_t)(word >> 8), (uint8_t)(word >> 16), (uint8_t)(word >> 24) };

		fwrite(bytes, 1, 4, f);
	}

	fclose(f);

	printf("%s: %zu words, %d programs, %u answers to compare\n", argv[1], trace.size(), index, checks);

	return 0;
}
