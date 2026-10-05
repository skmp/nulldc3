/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "hw/sh4/sh4_mmio.h"

enum Arm7Backends {
	ARM7BE_INTERPRETER,
	ARM7BE_DYNAREC
};

struct AICA;

struct SoundCPU : MMIODevice {
	
	virtual bool setBackend(Arm7Backends backend) = 0;

	virtual void SetResetState(u32 State) = 0;
	virtual void Update(u32 cycles) = 0;
	virtual void InterruptChange(u32 bits, u32 L) = 0;
	virtual void InvalidateJitCache() = 0;

	/*
		Debugger access. Called on the emulator thread outside Update(), or by a thread that holds the
		debugger's lifetime lock while no emulator thread exists; the setter on the emulator thread only.
		The defaults are those of a device that gives the debugger nothing.
	*/

	// False while the ARM7 is held in reset (ARMRST set): it is not clocked and Update() executes nothing.
	virtual bool DebugEnabled() { return false; }

	// A register as the debugger shows it. reg is an index of hw/arm7/arm7_context.h:
	//   0..14                 R0..R14 of the active mode
	//   15                    the address of the next instruction (R15_ARM_NEXT; R15 itself runs ahead of it
	//                         during an instruction and is stale between two)
	//   RN_CPSR               put together from the flags, the interrupt enables and the mode, which the
	//                         core keeps apart
	//   RN_SPSR               SPSR of the active mode
	//   R13_IRQ .. SPSR_FIQ   the banked registers; for the bank of the active mode the live values
	//   21..25                R8..R12 of the modes other than FIQ (the live values unless the core is in FIQ mode)
	// No side effects. Returns false for any other index and while the value cannot be given.
	virtual bool DebugGetReg(u32 reg, u32* value) { return false; }

	// Writes a register, same indices. RN_CPSR goes through the mode switch (banks swap, flags and interrupt
	// enables follow); 15 sets the address of the next instruction; code already compiled for the ARM7 is
	// dropped where the write needs it. Returns false, having changed nothing, when the register cannot be
	// written.
	virtual bool DebugSetReg(u32 reg, u32 value) { return false; }

	virtual ~SoundCPU() { }

	static SoundCPU* Create(AICA* aica, u8* aica_ram, u32 aram_size);
};