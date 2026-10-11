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
		Debugger access. Called on the emulator thread between two instructions of the ARM7 - outside a sound
		burst, or while the debugger has one suspended (in the slice hook and in the park context) -, or by a
		thread that holds the debugger's lifetime lock while no emulator thread exists; the setter on the
		emulator thread only. The defaults are those of a device that gives the debugger nothing.
	*/

	// False while the ARM7 is held in reset (ARMRST set): it is not clocked and a sound burst executes none
	// of its instructions.
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

	// Writes a register, same indices as DebugGetReg. Emulator thread only, in the debugger's park context;
	// also while the ARM7 is held in reset and while a sound burst is suspended.
	//   0..14, RN_SPSR, the banked registers   stored; for the bank of the active mode into the live register
	//   15                                     the address of the next instruction (R15_ARM_NEXT), stored with
	//                                          bits 0 and 1 cleared, as every write of R15 by the core is: the
	//                                          fetch reads four bytes at the address under the mask of the wave
	//                                          memory and does not align it
	//   RN_CPSR                                the mode bits have to be one of 0x10, 0x11, 0x12, 0x13, 0x17,
	//                                          0x1B, 0x1F. The write goes through the mode switch (the banks
	//                                          swap); the flags and the interrupt enables follow, and the
	//                                          pending FIQ is worked out again. The register reads back as
	//                                          the core composes it: N Z C V, I, F and the mode (the value
	//                                          under 0xF00000DF); no other bit is stored. The switch stores
	//                                          the live R13 and R14, and the live SPSR, into the slots of
	//                                          the mode it leaves, as the core's own switch does (an MSR of
	//                                          the guest makes it also when the mode stays). Leaving user or
	//                                          system mode, which have no SPSR, it copies the CPSR as it
	//                                          was before the write into the live SPSR instead, and a mode
	//                                          that has an SPSR loads its own over that when it is entered,
	//                                          as behind an MSR of the guest. So behind a write from one of
	//                                          those two modes RN_SPSR reads the CPSR as it was before the
	//                                          write when the mode written is one of the two as well (also
	//                                          behind a write of the value the CPSR has), and the SPSR of
	//                                          the mode written otherwise, which it goes on reading when
	//                                          the CPSR is written back. A CPSR written and written back
	//                                          leaves every register as it read, but RN_SPSR when the mode
	//                                          it is written from is one of those two, and can leave the
	//                                          save state different in those slots
	// No compiled code is dropped: the code cache of the ARM7 is found by address. Returns false, having
	// changed nothing, for any other index and for a CPSR with other mode bits.
	virtual bool DebugSetReg(u32 reg, u32 value) { return false; }

	// The interrupt controller in front of nFIQ: *level is the interrupt level register L at 0x2D00 as a read
	// by the ARM7 delivers it, without the acknowledge of the request that goes with that read; *inService is
	// the in-service flag M at 0x2D04, 1 while a request is in service. No side effects. Returns false while
	// the values cannot be given.
	virtual bool DebugGetInterrupt(u32* level, u32* inService) { return false; }

	// Is the backend that executes the ARM7 a recompiler. The debugger's breakpoints and steps of the ARM7 then
	// run the interpreter on the same context, with other timing (NOTICE_ARM7_FALLBACK). The answer changes
	// when setBackend() does, which is at the start of an emulator thread. No side effects. Same threads as
	// DebugEnabled().
	virtual bool DebugIsRecompiler() { return false; }

	virtual ~SoundCPU() { }

	static SoundCPU* Create(AICA* aica, u8* aica_ram, u32 aram_size);
};