/*
	This file is part of libswirl

	The C interface of the AICA DSP core: aica_dsp_capi.h.
*/

//The emulator calls the core itself. Its build takes every .cpp below hw/: for it this file is empty
#if defined(__EMSCRIPTEN__) || defined(AICA_DSP_CAPI)

#include "aica_dsp_capi.h"
#include "../aica_dsp_core.h"

#include <cstdlib>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define CAPI EMSCRIPTEN_KEEPALIVE
#else
#define CAPI
#endif

struct AicaDspHandle
{
	AicaDsp dsp;
	AicaDspSampleInputs inputs;
	AicaDspStepView view;
	uint32_t inputs_on;
	uint8_t* ram;
	uint32_t ram_size;
};

extern "C"
{

CAPI uint32_t aica_dsp_api_version(void)
{
	return AICA_DSP_API_VERSION;
}

CAPI AicaDspHandle* aica_dsp_create(uint32_t ram_size)
{
	//a power of two, or none
	if (ram_size & (ram_size - 1))
		return 0;

	AicaDspHandle* h = (AicaDspHandle*)calloc(1, sizeof(AicaDspHandle));

	if (!h)
		return 0;

	if (ram_size)
	{
		h->ram = (uint8_t*)calloc(ram_size, 1);

		if (!h->ram)
		{
			free(h);
			return 0;
		}
	}

	h->ram_size = ram_size;
	h->inputs_on = 1;
	h->dsp.Init(h->ram, ram_size);

	return h;
}

CAPI void aica_dsp_destroy(AicaDspHandle* h)
{
	if (!h)
		return;

	free(h->ram);
	free(h);
}

CAPI void aica_dsp_reset(AicaDspHandle* h)
{
	h->dsp.Reset();
	memset(&h->view, 0, sizeof(h->view));
}

CAPI uint32_t aica_dsp_reg_read(AicaDspHandle* h, uint32_t off)
{
	return h->dsp.RegRead(off);
}

CAPI void aica_dsp_reg_write(AicaDspHandle* h, uint32_t off, uint32_t value)
{
	h->dsp.RegWrite(off, value);
}

CAPI uint32_t aica_dsp_reg_peek(AicaDspHandle* h, uint32_t off)
{
	off &= 0x7FFC;

	if (off == 0x2804)
		return h->dsp.RBPL;

	if (off >= 0x45C0 && off < 0x45C8)
		return (uint16_t)h->dsp.EXTS[(off - 0x45C0) >> 2];

	return h->dsp.RegRead(off);
}

CAPI void aica_dsp_reg_poke(AicaDspHandle* h, uint32_t off, uint32_t value)
{
	off &= 0x7FFC;

	if (off >= 0x4400 && off < 0x4500 && !(off & 4))
	{
		int32_t& m = h->dsp.MEMS[(off - 0x4400) >> 3];

		m = (int32_t)(((uint32_t)m & ~0xFFu) | (value & 0xFF));
	}
	else if (off >= 0x45C0 && off < 0x45C8)
		h->dsp.EXTS[(off - 0x45C0) >> 2] = (int16_t)value;
	else
		h->dsp.RegWrite(off, value);
}

CAPI void aica_dsp_step(AicaDspHandle* h)
{
	h->dsp.StepOne(h->inputs_on ? &h->inputs : nullptr, &h->view);
}

CAPI void aica_dsp_run_sample(AicaDspHandle* h)
{
	h->dsp.RunSample(h->inputs_on ? &h->inputs : nullptr);
}

CAPI uint32_t aica_dsp_run(AicaDspHandle* h, uint32_t count, const uint8_t* stops)
{
	const AicaDspSampleInputs* in = h->inputs_on ? &h->inputs : nullptr;
	uint32_t done = 0;

	while (done < count)
	{
		h->dsp.StepOne(in, &h->view);
		done++;

		if (stops && stops[h->dsp.step])
			break;
	}

	return done;
}

CAPI void aica_dsp_sweep_begin(AicaDspHandle* h)
{
	h->dsp.SweepBegin();
}

CAPI void aica_dsp_mixs_send(AicaDspHandle* h, uint32_t bus, int32_t value)
{
	h->dsp.MixsSend(bus, value);
}

CAPI void aica_dsp_mixs_send_late(AicaDspHandle* h, uint32_t bus, int32_t value)
{
	h->dsp.MixsSendLate(bus, value);
}

CAPI void aica_dsp_channel_fetch(AicaDspHandle* h, uint32_t word)
{
	h->dsp.ChannelFetch((uint16_t)word);
}

CAPI void aica_dsp_bus_read(AicaDspHandle* h, uint32_t word)
{
	h->dsp.BusRead((uint16_t)word);
}

CAPI int32_t* aica_dsp_inputs_mixs(AicaDspHandle* h)
{
	return h->inputs.mixs;
}

CAPI uint16_t* aica_dsp_inputs_fetch_words(AicaDspHandle* h)
{
	return h->inputs.fetch_word;
}

CAPI void aica_dsp_set_inputs(AicaDspHandle* h, uint32_t on, uint32_t mixs_sent, uint32_t fetch_low, uint32_t fetch_high)
{
	h->inputs_on = on != 0;
	h->inputs.mixs_sent = (uint16_t)mixs_sent;
	h->inputs.fetch = fetch_low | ((uint64_t)fetch_high << 32);
}

CAPI uint8_t* aica_dsp_ram(AicaDspHandle* h) { return h->ram; }
CAPI uint32_t aica_dsp_ram_size(AicaDspHandle* h) { return h->ram_size; }
CAPI uint16_t* aica_dsp_coef(AicaDspHandle* h) { return h->dsp.COEF; }
CAPI uint16_t* aica_dsp_madrs(AicaDspHandle* h) { return h->dsp.MADRS; }
CAPI uint16_t* aica_dsp_mpro(AicaDspHandle* h) { return &h->dsp.MPRO[0][0]; }
CAPI int32_t* aica_dsp_temp(AicaDspHandle* h) { return h->dsp.TEMP; }
CAPI int32_t* aica_dsp_mems(AicaDspHandle* h) { return h->dsp.MEMS; }
CAPI int32_t* aica_dsp_mixs(AicaDspHandle* h) { return &h->dsp.MIXS[0][0]; }
CAPI int16_t* aica_dsp_efreg(AicaDspHandle* h) { return h->dsp.EFREG; }
CAPI int16_t* aica_dsp_exts(AicaDspHandle* h) { return h->dsp.EXTS; }
CAPI int32_t* aica_dsp_view(AicaDspHandle* h) { return &h->view.step; }

CAPI int32_t aica_dsp_get(AicaDspHandle* h, uint32_t index)
{
	const AicaDsp& d = h->dsp;

	switch (index)
	{
	case AICA_DSP_REG_STEP: return d.step;
	case AICA_DSP_REG_SAMPLES: return (int32_t)d.samples;
	case AICA_DSP_REG_MDEC_CT: return d.MDEC_CT;
	case AICA_DSP_REG_RBPL: return d.RBPL;
	case AICA_DSP_REG_ACC: return d.ACC;
	case AICA_DSP_REG_Y_REG: return d.Y_REG;
	case AICA_DSP_REG_FRC_REG: return d.FRC_REG;
	case AICA_DSP_REG_ADRS_REG: return d.ADRS_REG;
	case AICA_DSP_REG_LATCH: return d.latch;
	case AICA_DSP_REG_LATCH_COPY: return d.latch_copy;
	case AICA_DSP_REG_MEMVAL: return d.memval;
	case AICA_DSP_REG_NOFL_PIPE: return d.nofl_pipe;
	case AICA_DSP_REG_MIXS_DSP: return d.mixs_dsp;
	case AICA_DSP_REG_MIXS_FILL: return d.mixs_fill;
	default: return 0;
	}
}

//Each value is cut to the bits its register has
CAPI void aica_dsp_set(AicaDspHandle* h, uint32_t index, int32_t value)
{
	AicaDsp& d = h->dsp;
	const uint32_t v = (uint32_t)value;

	switch (index)
	{
	case AICA_DSP_REG_STEP: d.step = (uint8_t)(v & 127); break;
	case AICA_DSP_REG_SAMPLES: d.samples = v; break;
	case AICA_DSP_REG_MDEC_CT: d.MDEC_CT = (uint16_t)v; break;
	case AICA_DSP_REG_RBPL: d.RBPL = (uint16_t)v; break;
	case AICA_DSP_REG_ACC: d.ACC = (int32_t)(v << 6) >> 6; break;
	case AICA_DSP_REG_Y_REG: d.Y_REG = (int32_t)(v << 8) >> 8; break;
	case AICA_DSP_REG_FRC_REG: d.FRC_REG = (uint16_t)(v & 0x1FFF); break;
	case AICA_DSP_REG_ADRS_REG: d.ADRS_REG = (uint16_t)(v & 0xFFF); break;
	case AICA_DSP_REG_LATCH: d.latch = (uint16_t)v; break;
	case AICA_DSP_REG_LATCH_COPY: d.latch_copy = (uint16_t)v; break;
	case AICA_DSP_REG_MEMVAL: d.memval = (uint16_t)v; break;
	case AICA_DSP_REG_NOFL_PIPE: d.nofl_pipe = (uint8_t)(v & 3); break;
	case AICA_DSP_REG_MIXS_DSP: d.mixs_dsp = (uint8_t)(v & 1); break;
	case AICA_DSP_REG_MIXS_FILL: d.mixs_fill = (uint8_t)(v & 1); break;
	default: break;
	}
}

CAPI uint32_t aica_dsp_state_size(void)
{
	return AicaDsp::StateSize();
}

CAPI void aica_dsp_save_state(AicaDspHandle* h, uint8_t* blob)
{
	h->dsp.SaveState(blob);
}

CAPI uint32_t aica_dsp_load_state(AicaDspHandle* h, const uint8_t* blob, uint32_t size)
{
	return h->dsp.LoadState(blob, size) ? 1 : 0;
}

CAPI void aica_dsp_decode(const uint16_t* mpro, uint8_t* fields)
{
	AicaDspInst inst;

	AicaDspDecode(mpro, &inst);
	memcpy(fields, &inst, sizeof(inst));
}

CAPI void aica_dsp_encode(const uint8_t* fields, uint16_t* mpro)
{
	AicaDspInst inst;

	memcpy(&inst, fields, sizeof(inst));
	AicaDspEncode(&inst, mpro);
}

CAPI uint32_t aica_dsp_pack(int32_t value24)
{
	return AicaDspPack(value24);
}

CAPI int32_t aica_dsp_unpack(uint32_t word)
{
	return AicaDspUnpack((uint16_t)word);
}

CAPI void* aica_dsp_alloc(uint32_t bytes)
{
	return malloc(bytes);
}

CAPI void aica_dsp_free(void* memory)
{
	free(memory);
}

}

#endif
