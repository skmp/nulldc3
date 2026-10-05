/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "hw/sh4/sh4_mmio.h"

enum DspBackends {
	DSPBE_INTERPRETER,
	DSPBE_DYNAREC,
};

struct dsp_context_t;
struct AicaContext;

// What the debugger shows of the DSP beyond the registers in the AICA register file (DSP::DebugGetState).
struct DspDebugState {
	u32 step;    // the program step that executes next, 0..127; 0 between two samples
	u32 mdecCt;  // MDEC_CT, the counter that moves the ring buffer down once per sample
};

struct DSP : MMIODevice {
	virtual void Step() = 0;
	virtual void WritenMem(u32 addr) = 0;
	virtual bool setBackend(DspBackends backend) = 0;
	virtual dsp_context_t* GetDspContext() = 0;

	/*
		Debugger access, independent of the layout of dsp_context_t. Called on the emulator thread outside
		Step(), or by a thread that holds the debugger's lifetime lock while no emulator thread exists.
		No side effects. The defaults are those of a device that gives the debugger nothing.
	*/

	virtual bool DebugGetState(DspDebugState* out) { return false; }

	// The four 16-bit words of program step 0..127, in register order, into mpro[0..3]: the program as the
	// DSP executes it now. Returns false for any other step and while the program cannot be given.
	virtual bool DebugGetProgramStep(u32 step, u16* mpro) { return false; }

	static DSP* Create(AicaContext* aica_ctx, u8* aica_ram, u32 aram_size);
};

static inline void libDSP_Step() {
	sh4_cpu->GetA0H<DSP>(A0H_DSP)->Step();
}

