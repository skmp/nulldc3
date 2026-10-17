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
	u32 step;       // the step that executes next, 0..127: 0 between two samples; any step while the debugger has
	                // a sound burst suspended in front of a step of the DSP
	u32 mdecCt;     // MDEC_CT, the counter of the ring buffer; its low 16 bits are the register
	u32 rbpl;       // the word the CPU wrote to 0x2804 last, as the DSP uses it: RBP bits 11:0, RBL bits 14:13, the
	                // test bit 15. What a read of 0x2804 by the CPU gives is AICA::DebugReadRegs's
	s32 acc;        // ACC, 26 bits, sign extended
	s32 yReg;       // Y_REG, 24 bits, sign extended
	u32 frcReg;     // FRC_REG, 13 bits
	u32 adrsReg;    // ADRS_REG, 12 bits
	u32 latch;      // the wave RAM read latch, 16 bits
	u32 latchCopy;  // its copy, taken at the end of the last odd step, 16 bits
	u32 memval;     // what an IWT stores, before its conversion, 16 bits
	u32 noflPipe;   // NOFL of the last step (bit 0) and of the step before it (bit 1)
	u32 mixsBank;   // the MIXS bank the DSP and the CPU read, 0 or 1
};

struct DSP : MMIODevice {
	virtual void Step() = 0;
	virtual void WritenMem(u32 addr) = 0;
	virtual bool setBackend(DspBackends backend) = 0;
	virtual dsp_context_t* GetDspContext() = 0;

	/*
		Debugger access. Called on the emulator thread between two steps of the DSP - outside a sound burst, or
		while the debugger has one suspended -, or by a thread that holds the debugger's lifetime lock while no
		emulator thread exists. No side effects. The defaults are those of a device that gives the debugger
		nothing.
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

