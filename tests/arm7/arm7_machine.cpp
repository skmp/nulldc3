/*
	hw/arm7/SoundCPU.cpp is compiled as a part of this file: SoundCPU_impl, the Arm7Context in it and the memory
	access functions it gives the context are private to it.
*/
#include "hw/arm7/SoundCPU.cpp"

#include "arm7_machine.h"

#include <cstdlib>
#include <cstring>

#define MCIPD_addr 0x28B8
#define MCIPD_SCPU 0x20

//What SoundCPU.cpp references and no test reaches: the SH4 side of ARMRST and of the interrupt lines, and
//save states
SuperH4* sh4_cpu;

bool rc_serialize(void*, unsigned int, void**, unsigned int*)
{
	die("no save states in the ARM7 tests");
	return false;
}

bool rc_unserialize(void*, unsigned int, void**, unsigned int*)
{
	die("no save states in the ARM7 tests");
	return false;
}

//SoundCPU::Update steps the AICA once per sample
void libAICA_TimeStep() { }

//0x8000 bytes of registers, read and written as the AICA does it: one byte, or 16 bits for everything wider
struct RegisterFile : AICA
{
	u8 regs[0x8000];
	bool scpu_raised;

	void Clear()
	{
		memset(regs, 0, sizeof(regs));
		scpu_raised = false;
	}

	void Update(u32) { }
	void TimeStep() { }

	u32 ReadReg(u32 addr, u32 size)
	{
		addr &= 0x7FFF;

		if (size == 1)
			return regs[addr];

		addr &= ~1;

		return regs[addr] | (regs[addr + 1] << 8);
	}

	void WriteReg(u32 addr, u32 data, u32 size)
	{
		addr &= 0x7FFF;

		if (addr == MCIPD_addr && (data & MCIPD_SCPU))
			scpu_raised = true;

		if (size == 1)
		{
			regs[addr] = (u8)data;
			return;
		}

		addr &= ~1;

		regs[addr] = (u8)data;
		regs[addr + 1] = (u8)(data >> 8);
	}
};

static RegisterFile aica;
static SoundCPU_impl* scpu;
static u8* ram;

void arm7m_init()
{
	//The interpreter fetches 4 bytes from any address below the end of the RAM
	ram = (u8*)calloc(1, ARM7M_RAM_SIZE + 4);
	verify(ram != 0);

	aica.Clear();

	scpu = static_cast<SoundCPU_impl*>(SoundCPU::Create(&aica, ram, ARM7M_RAM_SIZE));

	//as the emulator brings its devices up
	scpu->Init();
	scpu->Reset(false);
}

u8* arm7m_ram()
{
	return ram;
}

Arm7Context* arm7m_context()
{
	return &scpu->ctx;
}

u32 arm7m_cpsr()
{
	ARM7Backend::CPUUpdateCPSR(&scpu->ctx);

	return scpu->ctx.regs[RN_CPSR].I;
}

void arm7m_hold_reset()
{
	scpu->SetResetState(1);
}

void arm7m_release_reset()
{
	scpu->SetResetState(0);
}

void arm7m_clear()
{
	memset(ram, 0, ARM7M_RAM_SIZE);
	aica.Clear();
}

u32 arm7m_step()
{
	return ARM7Backend::Step(&scpu->ctx);
}

u32 arm7m_next_opcode()
{
	u32 opcode;

	memcpy(&opcode, &ram[scpu->ctx.regs[R15_ARM_NEXT].I & scpu->ctx.aram_mask], sizeof(opcode));

	return opcode;
}

bool arm7m_scpu_raised()
{
	return aica.scpu_raised;
}
