/*
	The sound CPU of the emulator on its own: SoundCPU (hw/arm7/SoundCPU.cpp) with the interpreter backend on
	2 MB of wave RAM.

	The reset state, the memory map and the memory access functions of the ARM (wave RAM mirrors, misaligned
	words) are the ones of the emulator. There is no AICA behind the register window, only a register file that
	knows one bit: MCIPD.SCPU, the interrupt to the SH4 that a guest raises to say it is done.
*/
#pragma once
#include "types.h"
#include "hw/arm7/arm7_context.h"

#define ARM7M_RAM_SIZE (2 * 1024 * 1024)

//Call once
void arm7m_init();

//The wave RAM, ARM7M_RAM_SIZE bytes
u8* arm7m_ram();

//The registers. regs[R15_ARM_NEXT] is the address of the next instruction
Arm7Context* arm7m_context();

//CPSR as MRS reads it: the interpreter keeps the flags, the interrupt masks and the mode apart from the register
u32 arm7m_cpsr();

//Sets N, Z, C and V: bits 3 to 0 of nzcv, as they are in bits 31 to 28 of CPSR
void arm7m_set_flags(u32 nzcv);

//ARMRST, as the SH4 side drives it. The CPU is reset when it is released
void arm7m_hold_reset();
void arm7m_release_reset();

//Zeroes the wave RAM and the register file
void arm7m_clear();

//Executes one instruction, with a pending FIQ taken first. Returns the cycles the interpreter counts for it
u32 arm7m_step();

//The word the next arm7m_step() executes
u32 arm7m_next_opcode();

//The guest has written MCIPD with the SCPU bit set since the last arm7m_clear()
bool arm7m_scpu_raised();
