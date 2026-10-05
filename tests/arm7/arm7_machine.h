/*
	The sound CPU of the emulator on its own: SoundCPU (hw/arm7/SoundCPU.cpp) with the interpreter backend on
	2 MB of wave RAM, and the register block of the AICA (hw/aica/aica_mmio.cpp) behind the register window.

	The reset state, the memory map and the memory access functions of the ARM (wave RAM mirrors, misaligned
	words), the interrupt and timer registers and the interrupt controller in front of nFIQ are the ones of the
	emulator. The channels and the DSP are not there: their registers keep what is written to them. Nothing
	lets time pass but arm7m_sample().
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

//The machine is reset, as the emulator resets its devices: the registers get their power on values and the ARM
//is held in reset
void arm7m_power_on();

//ARMRST, as the SH4 side drives it. The CPU is reset when it is released, and keeps its registers over it
void arm7m_hold_reset();
void arm7m_release_reset();

//Zeroes the wave RAM and the registers of the AICA
void arm7m_clear();

//Executes one instruction, with a pending FIQ taken first. Returns the cycles the interpreter counts for it
u32 arm7m_step();

//The word the next arm7m_step() executes
u32 arm7m_next_opcode();

//The guest has written MCIPD with the SCPU bit set since the last arm7m_clear()
bool arm7m_scpu_raised();

//One sample passes, as the emulator steps the AICA behind every sample's worth of ARM code: the timers count and
//the one sample interval is raised (SCIPD bit 10)
void arm7m_sample();

//A word access as the ARM makes it
u32 arm7m_read32(u32 addr);
void arm7m_write32(u32 addr, u32 data);
