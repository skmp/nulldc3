/*
	This file is part of libswirl

	The AICA DSP core for hosts that cannot use a C++ struct: a WebAssembly module in a browser, anything that
	binds C. Plain C functions over a handle, and pointers into the handle's memory that such a host turns into
	typed arrays.

	A handle is a core (aica_dsp_core.h) with a wave RAM of its own, the inputs of the running sample, and what
	the last single step computed. What a function does to the DSP is what the method of AicaDsp it names does:
	the rules are in aica_dsp_core.h and README.md.

	wasm/build.sh makes aica_dsp_core.wasm of this, wasm/aica_dsp_core.mjs loads it.
*/
#pragma once
#include <cstdint>

extern "C"
{
	struct AicaDspHandle;

	//The version of this interface: it changes when a function, the layout behind a pointer or the state blob does
	uint32_t aica_dsp_api_version(void);

	//A core after power on, with ram_size bytes of wave RAM, cleared (a power of two; the hardware has 2 MB or
	//8 MB). 0: a core without wave RAM. Returns 0 when there is no memory
	AicaDspHandle* aica_dsp_create(uint32_t ram_size);
	void aica_dsp_destroy(AicaDspHandle* h);

	//AicaDsp::Reset. The wave RAM and the inputs stay
	void aica_dsp_reset(AicaDspHandle* h);

	//The CPU's side: AicaDsp::RegRead and RegWrite
	uint32_t aica_dsp_reg_read(AicaDspHandle* h, uint32_t off);
	void aica_dsp_reg_write(AicaDspHandle* h, uint32_t off, uint32_t value);

	//A debugger's side: the same offsets, but what the hardware has there and not what its CPU gets. Other than
	//aica_dsp_reg_read it shows RBP / RBL and EXTS, other than aica_dsp_reg_write it writes the low byte of MEMS
	//and EXTS. Offsets the DSP has no register at read 0
	uint32_t aica_dsp_reg_peek(AicaDspHandle* h, uint32_t off);
	void aica_dsp_reg_poke(AicaDspHandle* h, uint32_t off, uint32_t value);

	//One step, AicaDsp::StepOne, with the handle's inputs. aica_dsp_view has what it computed
	void aica_dsp_step(AicaDspHandle* h);

	//The rest of the sample, AicaDsp::RunSample, with the handle's inputs
	void aica_dsp_run_sample(AicaDspHandle* h);

	//Steps until `count` are done or the step that comes next has a non zero byte in stops[128], a breakpoint.
	//The step it starts at is executed whatever its byte is. Returns how many were executed. The view is the last
	//step's
	uint32_t aica_dsp_run(AicaDspHandle* h, uint32_t count, const uint8_t* stops);

	//Events call by call: AicaDsp::SweepBegin, MixsSend, MixsSendLate, ChannelFetch, BusRead
	void aica_dsp_sweep_begin(AicaDspHandle* h);
	void aica_dsp_mixs_send(AicaDspHandle* h, uint32_t bus, int32_t value);
	void aica_dsp_mixs_send_late(AicaDspHandle* h, uint32_t bus, int32_t value);
	void aica_dsp_channel_fetch(AicaDspHandle* h, uint32_t word);
	void aica_dsp_bus_read(AicaDspHandle* h, uint32_t word);

	/*
		The inputs of a sample, an AicaDspSampleInputs that aica_dsp_step and aica_dsp_run_sample pass on: the sums of
		the sound generator's sends, 16 x int32 at aica_dsp_inputs_mixs, and which buses have one; the channel fetches,
		64 x uint16 words at aica_dsp_inputs_fetch_words, and which channels fetch. They stay until they are changed.
		aica_dsp_set_inputs(h, 0, ...) turns them off: the steps then run without inputs, as AicaDsp::StepOne(nullptr),
		and the events are the caller's to make with the calls above.
		A handle starts with inputs on, no bus sent to, no channel fetching.
	*/
	int32_t* aica_dsp_inputs_mixs(AicaDspHandle* h);
	uint16_t* aica_dsp_inputs_fetch_words(AicaDspHandle* h);
	void aica_dsp_set_inputs(AicaDspHandle* h, uint32_t on, uint32_t mixs_sent, uint32_t fetch_low, uint32_t fetch_high);

	//Memory of the handle. The pointers are good until aica_dsp_destroy; a host with typed arrays makes them anew
	//when the module's memory has grown
	uint8_t* aica_dsp_ram(AicaDspHandle* h);			//ram_size bytes
	uint32_t aica_dsp_ram_size(AicaDspHandle* h);
	uint16_t* aica_dsp_coef(AicaDspHandle* h);			//128
	uint16_t* aica_dsp_madrs(AicaDspHandle* h);			//64
	uint16_t* aica_dsp_mpro(AicaDspHandle* h);			//128 x 4
	int32_t* aica_dsp_temp(AicaDspHandle* h);			//128, sign extended 24 bit
	int32_t* aica_dsp_mems(AicaDspHandle* h);			//32, sign extended 24 bit
	int32_t* aica_dsp_mixs(AicaDspHandle* h);			//2 banks x 16, sign extended 20 bit
	int16_t* aica_dsp_efreg(AicaDspHandle* h);			//16
	int16_t* aica_dsp_exts(AicaDspHandle* h);			//2: the CD-DA input, written here
	int32_t* aica_dsp_view(AicaDspHandle* h);			//AICA_DSP_STEP_VIEW_WORDS words: AicaDspStepView of the last aica_dsp_step

	//The registers that are no arrays, by index: AICA_DSP_REG_*. aica_dsp_set is a debugger's write
	int32_t aica_dsp_get(AicaDspHandle* h, uint32_t index);
	void aica_dsp_set(AicaDspHandle* h, uint32_t index, int32_t value);

	//State: AicaDsp::StateSize, SaveState, LoadState. The wave RAM is not in it
	uint32_t aica_dsp_state_size(void);
	void aica_dsp_save_state(AicaDspHandle* h, uint8_t* blob);
	uint32_t aica_dsp_load_state(AicaDspHandle* h, const uint8_t* blob, uint32_t size);	//1: loaded

	//AicaDspDecode into 24 bytes, the fields in the order of AicaDspInst; AicaDspEncode back; the float format
	void aica_dsp_decode(const uint16_t* mpro, uint8_t* fields);
	void aica_dsp_encode(const uint8_t* fields, uint16_t* mpro);
	uint32_t aica_dsp_pack(int32_t value24);
	int32_t aica_dsp_unpack(uint32_t word);

	//Memory for what the host hands in: a state blob, a row to decode, the stops of aica_dsp_run
	void* aica_dsp_alloc(uint32_t bytes);
	void aica_dsp_free(void* memory);
}

#define AICA_DSP_API_VERSION 1

enum
{
	AICA_DSP_REG_STEP = 0,			//the step that is executed next
	AICA_DSP_REG_SAMPLES = 1,		//samples completed since the reset
	AICA_DSP_REG_MDEC_CT = 2,
	AICA_DSP_REG_RBPL = 3,			//register 0x2804 as it was written
	AICA_DSP_REG_ACC = 4,
	AICA_DSP_REG_Y_REG = 5,
	AICA_DSP_REG_FRC_REG = 6,
	AICA_DSP_REG_ADRS_REG = 7,
	AICA_DSP_REG_LATCH = 8,			//the wave RAM read latch
	AICA_DSP_REG_LATCH_COPY = 9,
	AICA_DSP_REG_MEMVAL = 10,		//what IWT stores
	AICA_DSP_REG_NOFL_PIPE = 11,
	AICA_DSP_REG_MIXS_DSP = 12,		//the MIXS bank the DSP and the CPU have
	AICA_DSP_REG_MIXS_FILL = 13,	//the MIXS bank the sound generator fills
	AICA_DSP_REG_COUNT = 14,
};
