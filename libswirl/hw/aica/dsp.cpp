/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "aica.h"
#include "aica_mmio.h"
#include "dsp.h"
#include "dsp_core/aica_dsp_core.h"
#include "aica_mem.h"
#include "oslib/oslib.h"
#include <vector>

/*
	The DSP of the AICA is the T5.0 core (dsp_core/aica_dsp_core.h): one AicaDsp, on the wave memory. The sound
	generator sends to it and mixes its EFREG, the AICA routes the CPUs' accesses of its registers to it
	(aica_mmio.cpp), and the sample's DSP phase is RunPhase.
*/
struct DSP_impl final : DSP {
	u8* aica_reg;
	u8* aica_ram;
	u32 aram_size;

	AicaDsp core = {};

	DSP_impl(u8* aica_reg, u8* aica_ram, u32 aram_size) : aica_reg(aica_reg), aica_ram(aica_ram), aram_size(aram_size) { }

	bool Init() {
		// the DSP's part of the register file, as the CPUs wrote it: nothing yet
		memset(&aica_reg[0x3000], 0, sizeof(DSPData_struct));

		core.Init(aica_ram, aram_size);

		return true;
	}

	// a machine reset is the DSP's power on: MDEC_CT 1, everything else 0
	void Reset(bool manual) {
		core.Reset();
	}

	bool RunPhase(bool checks) {
		core.RunSample();

		return true;
	}

	AicaDsp* Core() {
		return &core;
	}

	//The core keeps every field inside the bits of its register; the masks say which bits those are
	bool DebugGetState(DspDebugState* out) {
		out->step = core.step & 127;
		out->mdecCt = core.MDEC_CT;
		out->rbpl = core.RBPL;
		out->acc = (s32)((u32)core.ACC << 6) >> 6;
		out->yReg = (s32)((u32)core.Y_REG << 8) >> 8;
		out->frcReg = core.FRC_REG & 0x1FFF;
		out->adrsReg = core.ADRS_REG & 0xFFF;
		out->latch = core.latch;
		out->latchCopy = core.latch_copy;
		out->memval = core.memval;
		out->noflPipe = core.nofl_pipe & 3;
		out->mixsBank = core.mixs_dsp & 1;
		return true;
	}

	bool DebugGetProgramStep(u32 step, u16* mpro) {
		if (step >= 128)
			return false;

		for (int i = 0; i < 4; i++)
			mpro[i] = core.MPRO[step][i];

		return true;
	}

	//The core's state blob, AicaDsp::StateSize() bytes
	void serialize(void** data, unsigned int* total_size) {
		std::vector<u8> blob(AicaDsp::StateSize());

		core.SaveState(blob.data());
		rc_serialize(blob.data(), (unsigned int)blob.size(), data, total_size);
	}

	//A blob the core refuses (a save state this build did not write) resets the DSP, and the load goes on
	void unserialize(void** data, unsigned int* total_size) {
		std::vector<u8> blob(AicaDsp::StateSize());
		const bool given = *data != nullptr;

		rc_unserialize(blob.data(), (unsigned int)blob.size(), data, total_size);

		if (given && !core.LoadState(blob.data(), (u32)blob.size()))
		{
			printf("DSP: the save state has no DSP state this build reads; the DSP is reset\n");
			core.Reset();
		}
	}
};

DSP* DSP::Create(AicaContext* aica_ctx, u8* aica_ram, u32 aram_size) {
	return new DSP_impl(aica_ctx->regs, aica_ram, aram_size);
}
