/*
	aica_dsp_wasm_trace: aica.dsp.capi, and what aica.dsp.wasm replays.

	Runs programs on the DSP core twice, call for call: on an AicaDsp with the calls of aica_dsp_core.h, and through
	the C interface (hw/aica/dsp_core/wasm/aica_dsp_capi.h) compiled for this machine. What the interface answers
	has to be what the core answers when it is called itself: the registers as the CPU and a debugger read them,
	the registers by index, the state blob, the wave RAM, the views of the single steps, the arrays behind the
	pointers, the steps up to a breakpoint. That is aica.dsp.capi.

	The programs are 108 random ones, on a wave RAM of 64 KB (smaller than a ring: the addresses wrap in it), of
	256 KB (larger than what TABLE reaches: TABLE's own wrap shows) and of 2 MB, and behind them the known-answer
	programs of dsp_known.cpp, the set that aica.dsp.api runs on the core alone, on 64 KB to 16 MB.

	Every call goes into a trace, with the answer of the core that was called itself. wasm_test.mjs makes the same
	calls on aica_dsp_core.wasm through its loader and has to get the same answers: aica.dsp.wasm. The C interface
	is in the module, not in what the module is compared with.

	    aica_dsp_wasm_trace                 aica.dsp.capi: fails when an answer of the C interface is not the core's
	    aica_dsp_wasm_trace <trace file>    writes the trace

	The trace is 32 bit little endian words: "ADTR", a version, the size of the first core's wave RAM, then
	operations, each its number and its arguments (the T_ list below, which wasm_test.mjs has as well).
*/
#include "hw/aica/dsp_core/wasm/aica_dsp_capi.h"
#include "hw/aica/dsp_core/aica_dsp_core.h"
#include "dsp_known.h"
#include "dsp_suite.h"

#include <cstdio>
#include <cstdlib>
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
	T_RUN = 7,				//count, 4 words of stop bits, all 0: run without stops; expects: steps done
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
	T_ARRAYS = 21,			//expects: the digest of every array behind a pointer, as numbers (arrays() has the order)
	T_CREATE = 22,			//size: the core goes, another one comes with a wave RAM of that size, all 0
	T_RAM_WRITE = 23,		//byte address, word: 16 bit, little endian
	T_RAM_READ = 24,		//byte address; expects: the word
	T_RAM_USED = 25,		//expects: how many bytes of the wave RAM are not 0
};

#define TRACE_VERSION 3
#define TRACE_RAM_SIZE 0x10000u		//of the first core
#define FNV_START 2166136261u

static std::vector<uint32_t> trace;
static uint32_t operations, answers, differ;

//The C interface
static AicaDspHandle* h;
static uint32_t capi_views;
static uint32_t ram_size = TRACE_RAM_SIZE;	//of the core that is there

//The core that is called itself, and what a handle has around its core: the wave RAM, the inputs of the samples and
//whether they are on, the view of the last single step
static AicaDsp core;
static std::vector<uint8_t> core_ram;
static AicaDspSampleInputs core_inputs;
static bool core_inputs_on = true;
static AicaDspStepView core_view;
static uint32_t core_views;

static uint32_t fnv(uint32_t hash, const uint8_t* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
		hash = (hash ^ data[i]) * 16777619u;

	return hash;
}

//An array as the numbers it holds: of every element its value in 32 bit and whether it is negative. Elements of
//another width or signedness, more or fewer of them, are another digest. wasm_test.mjs has the same
template <typename T> static uint32_t numbers(uint32_t hash, const T* data, size_t count)
{
	for (size_t i = 0; i < count; i++)
	{
		const int64_t value = data[i];
		const uint32_t low = (uint32_t)value;
		const uint8_t bytes[5] = { (uint8_t)low, (uint8_t)(low >> 8), (uint8_t)(low >> 16), (uint8_t)(low >> 24), (uint8_t)(value < 0) };

		hash = fnv(hash, bytes, sizeof(bytes));
	}

	return hash;
}

template <typename T, size_t N> static uint32_t numbers(uint32_t hash, const T (&data)[N])
{
	return numbers(hash, data, N);
}

static void op(uint32_t number) { trace.push_back(number); operations++; }
static void arg(uint32_t value) { trace.push_back(value); }

//An answer of the C interface against the core's
static void same(const char* what, uint32_t capi, uint32_t value)
{
	answers++;

	if (capi != value && differ++ < 10)
		printf("operation %u: %s is %x through the C interface, %x in the core\n", operations - 1, what, capi, value);
}

//The same, for an answer that the module has to give as well
static void answer(const char* what, uint32_t capi, uint32_t value)
{
	arg(value);
	same(what, capi, value);
}

static const AicaDspSampleInputs* core_in()
{
	return core_inputs_on ? &core_inputs : nullptr;
}

static int32_t sign_extended(uint32_t value, int bits)
{
	const uint32_t sign = 1u << (bits - 1);

	return (int32_t)(((value & (2 * sign - 1)) ^ sign) - sign);
}

static void reg_write(uint32_t off, uint32_t value)
{
	op(T_REG_WRITE); arg(off); arg(value);
	aica_dsp_reg_write(h, off, value);
	core.RegWrite(off, value);
}

//A debugger writes what the CPU writes, and the two things the CPU cannot: the low byte of MEMS, and EXTS
static void reg_poke(uint32_t off, uint32_t value)
{
	op(T_REG_POKE); arg(off); arg(value);
	aica_dsp_reg_poke(h, off, value);

	if (off >= 0x4400 && off < 0x4500 && !(off & 4))
	{
		int32_t& mems = core.MEMS[(off - 0x4400) / 8];

		mems = (mems & ~0xFF) | (int32_t)(value & 0xFF);
	}
	else if (off == 0x45C0 || off == 0x45C4)
		core.EXTS[(off - 0x45C0) / 4] = (int16_t)value;
	else
		core.RegWrite(off, value);
}

static void ram_fill(uint32_t seed)
{
	op(T_RAM_FILL); arg(seed);

	uint8_t* ram = aica_dsp_ram(h);
	uint32_t x = seed;

	for (uint32_t i = 0; i < ram_size; i++)
	{
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		ram[i] = (uint8_t)x;
		core_ram[i] = (uint8_t)x;
	}
}

//The inputs of the samples from here on, and whether the steps get them
static void inputs_put(bool on, const AicaDspSampleInputs& given)
{
	const uint32_t low = (uint32_t)given.fetch, high = (uint32_t)(given.fetch >> 32);

	op(T_INPUTS); arg(on); arg(given.mixs_sent); arg(low); arg(high);

	core_inputs_on = on;
	core_inputs = given;

	for (int i = 0; i < 16; i++)
	{
		arg((uint32_t)given.mixs[i]);
		aica_dsp_inputs_mixs(h)[i] = given.mixs[i];
	}

	for (int k = 0; k < 64; k++)
	{
		arg(given.fetch_word[k]);
		aica_dsp_inputs_fetch_words(h)[k] = given.fetch_word[k];
	}

	aica_dsp_set_inputs(h, on, given.mixs_sent, low, high);
}

static void inputs(DspRandom& rnd, bool on, int fetches)
{
	AicaDspSampleInputs given;

	uint32_t sent = rnd.Bits(16);
	uint32_t low = fetches == 2 ? 0xFFFFFFFFu : fetches == 1 ? rnd.Bits(32) & rnd.Bits(32) & rnd.Bits(32) : 0;
	uint32_t high = fetches == 2 ? 0xFFFFFFFFu : fetches == 1 ? rnd.Bits(32) & rnd.Bits(32) & rnd.Bits(32) : 0;

	given.mixs_sent = (uint16_t)sent;
	given.fetch = ((uint64_t)high << 32) | low;

	for (int i = 0; i < 16; i++)
		given.mixs[i] = (int32_t)(rnd.Bits(24) << 8) >> 8;

	for (int k = 0; k < 64; k++)
		given.fetch_word[k] = (uint16_t)rnd.Bits(16);

	inputs_put(on, given);
}

static void steps(uint32_t count)
{
	op(T_STEPS); arg(count);

	for (uint32_t i = 0; i < count; i++)
	{
		aica_dsp_step(h);
		capi_views = fnv(capi_views, (const uint8_t*)aica_dsp_view(h), AICA_DSP_STEP_VIEW_WORDS * 4);

		core.StepOne(core_in(), &core_view);
		core_views = fnv(core_views, (const uint8_t*)&core_view, sizeof(core_view));
	}
}

static void run_samples(uint32_t count)
{
	op(T_RUN_SAMPLES); arg(count);

	for (uint32_t i = 0; i < count; i++)
	{
		aica_dsp_run_sample(h);
		core.RunSample(core_in());
	}
}

//stops: 128 bytes, or none
static void run(uint32_t count, const uint8_t* stops)
{
	op(T_RUN); arg(count);

	for (int w = 0; w < 4; w++)
	{
		uint32_t bits = 0;

		for (int i = 0; i < 32; i++)
			bits |= (uint32_t)(stops && stops[32 * w + i] != 0) << i;

		arg(bits);
	}

	//where it ends: in front of the first breakpoint behind the step it starts at, or when count steps are done
	uint32_t until = 0;

	while (until < count)
	{
		until++;

		if (stops && stops[(core.step + until) & 127])
			break;
	}

	for (uint32_t i = 0; i < until; i++)
		core.StepOne(core_in(), &core_view);

	answer("the number of steps to the breakpoint", aica_dsp_run(h, count, stops), until);
}

static void bus_read(uint32_t word)
{
	op(T_BUS_READ); arg(word);
	aica_dsp_bus_read(h, word);
	core.BusRead((uint16_t)word);
}

static void channel_fetch(uint32_t word)
{
	op(T_CHANNEL_FETCH); arg(word);
	aica_dsp_channel_fetch(h, word);
	core.ChannelFetch((uint16_t)word);
}

static void sweep_begin()
{
	op(T_SWEEP_BEGIN);
	aica_dsp_sweep_begin(h);
	core.SweepBegin();
}

static void mixs_send(uint32_t bus, uint32_t value, bool late)
{
	op(late ? T_MIXS_SEND_LATE : T_MIXS_SEND); arg(bus); arg(value);

	if (late)
	{
		aica_dsp_mixs_send_late(h, bus, (int32_t)value);
		core.MixsSendLate(bus, (int32_t)value);
	}
	else
	{
		aica_dsp_mixs_send(h, bus, (int32_t)value);
		core.MixsSend(bus, (int32_t)value);
	}
}

//A reset leaves the wave RAM and the inputs, and no view of a step
static void reset()
{
	op(T_RESET);
	aica_dsp_reset(h);
	core.Reset();
	memset(&core_view, 0, sizeof(core_view));
}

static void save_load()
{
	op(T_SAVE_LOAD);

	std::vector<uint8_t> blob(aica_dsp_state_size()), core_blob(AicaDsp::StateSize());

	aica_dsp_save_state(h, blob.data());
	core.SaveState(core_blob.data());
	same("the size of a state", (uint32_t)blob.size(), (uint32_t)core_blob.size());

	aica_dsp_reset(h);
	core.Reset();
	memset(&core_view, 0, sizeof(core_view));

	same("LoadState of its own state", aica_dsp_load_state(h, blob.data(), (uint32_t)blob.size()),
		core.LoadState(core_blob.data(), (uint32_t)core_blob.size()));
	same("LoadState of a cut state", aica_dsp_load_state(h, blob.data(), (uint32_t)blob.size() - 1), 0);
}

//A debugger's write of a register that is no array: the value, cut to the bits the register has. dsp_known.cpp has
//which field an index is and how it is cut, written out for the tests: it is not taken from the interface
static void set(uint32_t index, uint32_t value)
{
	op(T_SET); arg(index); arg(value);
	aica_dsp_set(h, index, (int32_t)value);
	dsp_known_set(core, index, value);
}

static uint32_t get(uint32_t index)
{
	const uint32_t value = dsp_known_get(core, index);

	op(T_GET); arg(index);
	answer("a register by its index", (uint32_t)aica_dsp_get(h, index), value);

	return value;
}

static void exts(uint32_t index, uint32_t value)
{
	op(T_EXTS); arg(index); arg(value);
	aica_dsp_exts(h)[index] = (int16_t)value;
	core.EXTS[index] = (int16_t)value;
}

//Every pointer of the interface, over as many elements as aica_dsp_capi.h says it has
static void arrays()
{
	int32_t view[AICA_DSP_STEP_VIEW_WORDS];

	memcpy(view, &core_view, sizeof(view));

	uint32_t mpro = FNV_START;

	for (const auto& row : core.MPRO)
		mpro = numbers(mpro, row);

	op(T_ARRAYS);
	answer("the array of COEF", numbers(FNV_START, aica_dsp_coef(h), 128), numbers(FNV_START, core.COEF));
	answer("the array of MADRS", numbers(FNV_START, aica_dsp_madrs(h), 64), numbers(FNV_START, core.MADRS));
	answer("the array of MPRO", numbers(FNV_START, aica_dsp_mpro(h), 128 * 4), mpro);
	answer("the array of TEMP", numbers(FNV_START, aica_dsp_temp(h), 128), numbers(FNV_START, core.TEMP));
	answer("the array of MEMS", numbers(FNV_START, aica_dsp_mems(h), 32), numbers(FNV_START, core.MEMS));
	answer("the array of MIXS", numbers(FNV_START, aica_dsp_mixs(h), 2 * 16), numbers(numbers(FNV_START, core.MIXS[0]), core.MIXS[1]));
	answer("the array of EFREG", numbers(FNV_START, aica_dsp_efreg(h), 16), numbers(FNV_START, core.EFREG));
	answer("the array of EXTS", numbers(FNV_START, aica_dsp_exts(h), 2), numbers(FNV_START, core.EXTS));
	answer("the array of the view", numbers(FNV_START, aica_dsp_view(h), AICA_DSP_STEP_VIEW_WORDS), numbers(FNV_START, view));
	answer("the array of the inputs' sums", numbers(FNV_START, aica_dsp_inputs_mixs(h), 16), numbers(FNV_START, core_inputs.mixs));
	answer("the array of the inputs' fetch words", numbers(FNV_START, aica_dsp_inputs_fetch_words(h), 64), numbers(FNV_START, core_inputs.fetch_word));
}

static void check()
{
	std::vector<uint8_t> blob(aica_dsp_state_size()), core_blob(AicaDsp::StateSize());

	aica_dsp_save_state(h, blob.data());
	core.SaveState(core_blob.data());

	op(T_CHECK);
	answer("the digest of the state", fnv(FNV_START, blob.data(), blob.size()), fnv(FNV_START, core_blob.data(), core_blob.size()));
	answer("the digest of the wave RAM", fnv(FNV_START, aica_dsp_ram(h), aica_dsp_ram_size(h)), fnv(FNV_START, core_ram.data(), core_ram.size()));
	answer("the digest of the step views", capi_views, core_views);

	capi_views = core_views = FNV_START;

	arrays();
}

//What the CPU reads, and what a debugger reads: the same, and RBP / RBL and EXTS where the CPU reads 0
static uint32_t reg_read(uint32_t off)
{
	uint32_t shown = core.RegRead(off);

	if (off == 0x2804)
		shown = core.RBPL;
	else if (off == 0x45C0 || off == 0x45C4)
		shown = (uint16_t)core.EXTS[(off - 0x45C0) / 4];

	op(T_REG_READ); arg(off);
	answer("what the CPU reads", aica_dsp_reg_read(h, off), core.RegRead(off));
	answer("what a debugger reads", aica_dsp_reg_peek(h, off), shown);

	return core.RegRead(off);
}

//Another core: a wave RAM of that size, all 0, and a handle as it starts, with its inputs on and nothing in them
static void create(uint32_t size)
{
	op(T_CREATE); arg(size);

	aica_dsp_destroy(h);
	h = aica_dsp_create(size);

	if (!h)
	{
		fprintf(stderr, "no core with %u bytes of wave RAM through the C interface\n", size);
		exit(2);
	}

	ram_size = size;
	core_ram.assign(size, 0);
	core.Init(core_ram.data(), size);
	memset(&core_inputs, 0, sizeof(core_inputs));
	core_inputs_on = true;
	memset(&core_view, 0, sizeof(core_view));
}

static void ram_write(uint32_t addr, uint32_t word)
{
	op(T_RAM_WRITE); arg(addr); arg(word);

	uint8_t* ram = aica_dsp_ram(h);

	ram[addr] = (uint8_t)word;
	ram[addr + 1] = (uint8_t)(word >> 8);
	core_ram[addr] = (uint8_t)word;
	core_ram[addr + 1] = (uint8_t)(word >> 8);
}

static uint32_t ram_read(uint32_t addr)
{
	const uint8_t* ram = aica_dsp_ram(h);
	const uint32_t word = (uint32_t)(core_ram[addr] | (core_ram[addr + 1] << 8));

	op(T_RAM_READ); arg(addr);
	answer("a word of the wave RAM", (uint32_t)(ram[addr] | (ram[addr + 1] << 8)), word);

	return word;
}

static uint32_t ram_used()
{
	const uint8_t* ram = aica_dsp_ram(h);
	uint32_t used = 0, core_used = 0;

	for (uint32_t i = 0; i < ram_size; i++)
	{
		used += ram[i] != 0;
		core_used += core_ram[i] != 0;
	}

	op(T_RAM_USED);
	answer("the number of bytes of the wave RAM that are not 0", used, core_used);

	return core_used;
}

//The known-answer programs (dsp_known.cpp), call for call on both and into the trace. What the programs get to
//see is the core's answer: a known answer that is not given is the core's defect, and aica.dsp.api says so too
struct TraceMachine : KnownMachine
{
	const char* group = "";
	uint32_t known = 0;		//the answers that were looked at

	void Create(uint32_t size) override
	{
		static const AicaDspSampleInputs none = {};

		create(size);
		inputs_put(false, none);
	}

	void Reset() override { reset(); }
	void RegWrite(uint32_t off, uint32_t value) override { reg_write(off, value); }
	uint32_t RegRead(uint32_t off) override { return reg_read(off); }
	void Set(uint32_t index, uint32_t value) override { set(index, value); }
	uint32_t Get(uint32_t index) override { return get(index); }
	void RamWrite(uint32_t addr, uint32_t word) override { ram_write(addr, word); }
	uint32_t RamRead(uint32_t addr) override { return ram_read(addr); }
	uint32_t RamUsed() override { return ram_used(); }

	void Inputs(const AicaDspSampleInputs* in) override
	{
		static const AicaDspSampleInputs none = {};

		inputs_put(in != nullptr, in ? *in : none);
	}

	void Steps(uint32_t count) override { steps(count); }
	void RunSample() override { run_samples(1); }
	void SweepBegin() override { sweep_begin(); }
	void MixsSend(uint32_t bus, int32_t value) override { mixs_send(bus, (uint32_t)value, false); }
	void MixsSendLate(uint32_t bus, int32_t value) override { mixs_send(bus, (uint32_t)value, true); }
	void ChannelFetch(uint32_t word) override { channel_fetch(word); }
	void BusRead(uint32_t word) override { bus_read(word); }
	void Group(const char* name) override { group = name; }

	void Check(bool holds, const char* what) override
	{
		answers++;
		known++;

		if (!holds && differ++ < 10)
			printf("operation %u: %s, %s: the core does not give the known answer\n", operations - 1, group, what);
	}
};

static void program(DspRandom& rnd, int rows, bool sparse)
{
	reset();
	arrays();
	ram_fill(rnd.Bits(32) | 1);

	for (int i = 0; i < 128; i++)
	{
		reg_write(0x3000 + 4 * i, rnd.Bits(16));
		reg_write(0x4000 + 8 * i, rnd.Bits(8));
		reg_write(0x4004 + 8 * i, rnd.Bits(16));
	}

	//a debugger's write of a register the CPU writes is the CPU's write
	for (int i = 0; i < 64; i++)
	{
		if (i & 1)
			reg_poke(0x3200 + 4 * i, rnd.Bits(16));
		else
			reg_write(0x3200 + 4 * i, rnd.Bits(16));
	}

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

	if (rnd.Chance(2))
		reg_poke(0x2804, rnd.Bits(15) | (rnd.Chance(8) ? 0x8000 : 0));
	else
		reg_write(0x2804, rnd.Bits(15) | (rnd.Chance(8) ? 0x8000 : 0));

	set(AICA_DSP_REG_MDEC_CT, rnd.Bits(16));
}

//What no handle of the trace shows: the functions without a handle, a wave RAM size that is none, a core without
//wave RAM
static void without_trace(DspRandom& rnd)
{
	same("the version of the interface", aica_dsp_api_version(), AICA_DSP_API_VERSION);
	same("the size of the wave RAM", aica_dsp_ram_size(h), ram_size);
	same("the fields of a step, in bytes", (uint32_t)sizeof(AicaDspInst), 24);

	AicaDspHandle* odd = aica_dsp_create(3 * TRACE_RAM_SIZE);

	same("a core with a wave RAM that is no power of two", odd != nullptr, 0);
	aica_dsp_destroy(odd);

	AicaDspHandle* bare = aica_dsp_create(0);

	same("a core without wave RAM", bare && !aica_dsp_ram(bare) && !aica_dsp_ram_size(bare), 1);

	if (bare)
	{
		aica_dsp_run_sample(bare);
		same("its samples", (uint32_t)aica_dsp_get(bare, AICA_DSP_REG_SAMPLES), 1);
		aica_dsp_destroy(bare);
	}

	for (int i = 0; i < 1000; i++)
	{
		uint16_t words[4], back[4], core_back[4];
		uint8_t fields[sizeof(AicaDspInst)];
		AicaDspInst inst;

		for (uint16_t& word : words)
			word = (uint16_t)rnd.Bits(16);

		aica_dsp_decode(words, fields);
		AicaDspDecode(words, &inst);
		same("a decoded step", memcmp(fields, &inst, sizeof(inst)) == 0, 1);

		aica_dsp_encode(fields, back);
		AicaDspEncode(&inst, core_back);
		same("an encoded step", memcmp(back, core_back, sizeof(back)) == 0, 1);

		const int32_t value = sign_extended(rnd.Bits(24), 24);

		same("a packed value", aica_dsp_pack(value), AicaDspPack(value));
		same("an unpacked word", (uint32_t)aica_dsp_unpack(words[0]), (uint32_t)AicaDspUnpack(words[0]));
	}

	void* memory = aica_dsp_alloc(128);

	same("memory for the host", memory != nullptr, 1);
	aica_dsp_free(memory);
}

int main(int argc, char** argv)
{
	if (argc > 2 || (argc == 2 && argv[1][0] == '-'))
	{
		fprintf(stderr, "aica_dsp_wasm_trace [<trace file>]\n");
		return 2;
	}

	static const int rows[] = { 128, 128, 127, 126, 100, 64, 17, 2, 0 };

	DspRandom rnd(0x7ACE00ull);

	trace.push_back(0x52544441);	//"ADTR"
	trace.push_back(TRACE_VERSION);
	trace.push_back(TRACE_RAM_SIZE);

	h = aica_dsp_create(TRACE_RAM_SIZE);

	if (!h)
	{
		fprintf(stderr, "no core through the C interface\n");
		return 2;
	}

	core_ram.assign(TRACE_RAM_SIZE, 0);
	core.Init(core_ram.data(), TRACE_RAM_SIZE);
	capi_views = core_views = FNV_START;

	//A handle starts with its inputs on and nothing in them: the sample begins a sweep of its own behind the one
	//that was called
	sweep_begin();
	mixs_send(5, 0x12345, false);
	run_samples(1);
	check();

	//the wave RAM of the random programs, in turns
	static const uint32_t ram_sizes[] = { TRACE_RAM_SIZE, 2u << 20, TRACE_RAM_SIZE, 0x40000 };

	int index = 0;

	for (int round = 0; round < 12; round++)
	{
		for (int count : rows)
		{
			if (ram_sizes[index % 4] != ram_size)
				create(ram_sizes[index % 4]);

			program(rnd, count, index % 3 != 0);
			index++;

			for (int n = 0; n < 5; n++)
			{
				bool calls = n == 3;

				inputs(rnd, !calls, (index + n) % 3);

				//EXTS through its array, and as a debugger's write
				for (uint32_t i = 0; i < 2; i++)
				{
					if (n & 1)
						reg_poke(0x45C0 + 4 * i, rnd.Bits(16));
					else
						exts(i, rnd.Bits(16));
				}

				if (calls)
				{
					//a sample with its events call by call
					sweep_begin();

					for (int i = 0; i < 4; i++)
					{
						uint32_t bus = rnd.Bits(4), value = rnd.Bits(24);

						mixs_send(bus, value, i == 3);
					}

					for (int part = 0; part < 4; part++)
					{
						uint32_t word = rnd.Bits(16);

						if (part & 1)
							channel_fetch(word);
						else
							bus_read(word);

						steps(32);
					}
				}
				else if (n == 0)
					run_samples(1);
				else if (n == 1)
				{
					//single steps, then the rest in one go, with the state out and in again in between
					steps(1 + rnd.Below(127));
					save_load();
					run_samples(1);
				}
				else if (n == 2)
				{
					//up to breakpoints, and once without any
					uint8_t stops[128];

					for (int i = 0; i < 128; i++)
						stops[i] = rnd.Chance(24);

					for (int k = 0; k < 6; k++)
						run(1 + rnd.Below(200), k == 4 ? nullptr : stops);

					run_samples(1);
				}
				else
				{
					//every register that is no array, written as a debugger does, with more bits than it has
					for (uint32_t r = 0; r < AICA_DSP_REG_COUNT; r++)
						set(r, rnd.Bits(32));

					for (uint32_t r = 0; r < AICA_DSP_REG_COUNT; r++)
						get(r);

					run_samples(3);
				}

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

	TraceMachine machine;

	dsp_known_answers(machine);

	op(T_END);
	without_trace(rnd);
	aica_dsp_destroy(h);

	printf("%d programs and %u known answers, %u operations, %u answers of the C interface compared with the core's, %u differ\n",
		index, machine.known, operations, answers, differ);

	if (argc < 2)
	{
		printf(differ ? "aica.dsp.capi: FAIL\n" : "aica.dsp.capi: PASS\n");
		return differ ? 1 : 0;
	}

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

	printf("%s: %zu words\n", argv[1], trace.size());

	return 0;
}
