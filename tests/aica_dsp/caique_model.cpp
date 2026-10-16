#include "caique_model.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>

//The model is written with the bit builtins of GCC
static inline int caique_msvc_ctz(unsigned v)
{
	unsigned long index = 0;

	_BitScanForward(&index, v);

	return (int)index;
}

#define __builtin_parity(v) ((int)(__popcnt(v) & 1))
#define __builtin_ctz(v) caique_msvc_ctz(v)
#endif

#include "src/dsp_asm.h"
#include "src/dsp_float.h"

namespace caique { struct AicaModel; }

static void caique_step_hook(caique::AicaModel* model);

/*
	The model's DSP runs the 128 steps of a sample in one loop, inside dsp_step(), and decodes one MPRO row at the
	start of every step, nowhere else. That call is taken as the start of a step: the model is compiled with the
	hook in front of it. dsp_step() is private, and the tests that feed the DSP alone call it.
*/
#define dsp_decode(w, in) (caique_step_hook(this), dsp_decode(w, in))
#define private public

#include "sample-model/aica_model.cpp"

#undef private
#undef dsp_decode

struct CaiqueModel
{
	caique::AicaModel model;
	CaiqueStepHook* hook;
	void* hook_ctx;
	int hook_calls;
};

//The model that is inside caique_step() or caique_dsp_sample(): the hook has nothing but the AicaModel
static CaiqueModel* running;

static void caique_step_hook(caique::AicaModel* model)
{
	if (!running || &running->model != model)
		return;

	int step = running->hook_calls++;

	if (running->hook)
		running->hook(running->hook_ctx, step);
}

CaiqueModel* caique_new()
{
	CaiqueModel* model = new CaiqueModel();

	model->hook = 0;
	model->hook_ctx = 0;
	model->hook_calls = 0;

	return model;
}

void caique_delete(CaiqueModel* model)
{
	delete model;
}

void caique_reset(CaiqueModel* model, bool clear_ram)
{
	if (clear_ram)
		memset(model->model.ram, 0, caique::AicaModel::RAM_SIZE);

	model->model.reset();
}

void caique_refs(CaiqueModel* model, CaiqueRefs* refs)
{
	caique::AicaModel& m = model->model;

	refs->reg = m.reg;
	refs->ram = m.ram;
	refs->TEMP = m.TEMP;
	refs->MEMS = m.MEMS;
	refs->MIXS_bank = m.MIXS_bank;
	refs->EXTS = m.EXTS;
	refs->EFREG = m.EFREG;
	refs->MDEC_CT = &m.MDEC_CT;
	refs->ACC = &m.ACC;
	refs->FRC_REG = &m.FRC_REG;
	refs->Y_REG = &m.Y_REG;
	refs->ADRS_REG = &m.ADRS_REG;
	refs->memval = &m.memval;
	refs->latch = &m.latch;
	refs->cp = &m.cp;
	refs->nofl_pipe = m.nofl_pipe;
	refs->samples = &m.samples;
	refs->coll_claim = m.coll_claim;
	refs->coll_word = m.coll_word;
}

void caique_set_hook(CaiqueModel* model, CaiqueStepHook* hook, void* ctx)
{
	model->hook = hook;
	model->hook_ctx = ctx;
}

void caique_write(CaiqueModel* model, uint32_t off, uint32_t value)
{
	model->model.write(off, value);
}

uint32_t caique_read(CaiqueModel* model, uint32_t off)
{
	return model->model.read(off);
}

uint32_t caique_ram_read32(CaiqueModel* model, uint32_t off)
{
	return model->model.ram_read32(off);
}

bool caique_sh4_irq(CaiqueModel* model)
{
	return model->model.sh4_irq();
}

bool caique_step(CaiqueModel* model)
{
	running = model;
	model->hook_calls = 0;
	model->model.step();
	running = 0;

	return model->hook_calls == 128;
}

bool caique_dsp_sample(CaiqueModel* model)
{
	running = model;
	model->hook_calls = 0;
	model->model.dsp_step();
	running = 0;

	model->model.samples++;

	return model->hook_calls == 128;
}

uint16_t caique_pack(int32_t value24)
{
	return dsp_pack(value24);
}

int32_t caique_unpack(uint16_t word)
{
	return dsp_unpack(word);
}
