/*
	hw/arm7/SoundCPU.cpp is compiled as a part of this file: SoundCPU_impl, the Arm7Context in it and the memory
	access functions it gives the context are private to it.
*/
#include "hw/arm7/SoundCPU.cpp"

#include "arm7_machine.h"

#include "hw/aica/aica.h"
#include "hw/aica/dsp.h"
#include "hw/aica/sgc_if.h"
#include "hw/holly/holly_intc.h"
#include "hw/holly/sb.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/sh4_sched.h"

#include <cstdlib>
#include <cstring>

#define MCIPD_SCPU 0x20

/*
	What SoundCPU.cpp and aica_mmio.cpp reference and no test reaches: save states, the scheduler and the G2 DMA
	of the SH4 side, the settings.
*/
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

int sh4_sched_register(void*, int, sh4_sched_callback*) { return 0; }
void sh4_sched_request(int, int) { }
void WriteMemBlock_nommu_dma(u32, u32, u32) { }

settings_t settings;

//The channels: they keep the bytes of the registers they own
struct NoChannels : SGC
{
	u8* aica_reg;

	NoChannels(u8* aica_reg) : aica_reg(aica_reg) { }

	void AICA_Sample() { }
	void AICA_Sample32() { }
	void WriteChannelReg8(u32, u32) { }
	void ReadCommonReg(u32, bool) { }
	void WriteCommonReg8(u32 reg, u32 data) { aica_reg[reg] = (u8)data; }
	bool channel_serialize(void**, unsigned int*) { return true; }
	bool channel_unserialize(void**, unsigned int*) { return true; }
};

SGC* SGC::Create(AudioStream*, u8* aica_reg, dsp_context_t*, u8*, u32)
{
	return new NoChannels(aica_reg);
}

struct NoDsp : DSP
{
	void Step() { }
	void WritenMem(u32) { }
	bool setBackend(DspBackends) { return true; }
	dsp_context_t* GetDspContext() { return 0; }
};

//The system bus registers the AICA looks at: SB_ISTEXT, where its interrupt to the SH4 shows
struct NoSystemBus : SystemBus
{
	void RegisterRIO(void*, u32, RegIO, RegReadAddrFP*, RegWriteAddrFP*) { }
};

struct NoAsic : ASIC
{
	void RaiseInterrupt(HollyInterruptID) { }
	void CancelInterrupt(HollyInterruptID) { }
};

//The SH4 as the two devices find each other through it
struct NoSh4 : SuperH4
{
	MMIODevice* devices[A0H_MAX];

	void SetA0Handler(Area0Hanlders slot, MMIODevice* dev) { devices[slot] = dev; }
	MMIODevice* GetA0Handler(Area0Hanlders slot) { return devices[slot]; }

	bool setBackend(SuperH4Backends) { return true; }

	bool Init() { return true; }
	void Reset(bool) { }
	void Term() { }

	void Run() { }
	void Stop() { }
	void Start() { }
	void Step() { }
	void Skip() { }

	bool IsRunning() { return false; }

	void ResetCache() { }
};

SuperH4* sh4_cpu;

static AicaContext aica_ctx;
static AICA* aica;
static SoundCPU_impl* scpu;
static u8* ram;

//What the reset of the sound CPU gives a value. Bytes: a context that was only created may hold anything
struct CpuState
{
	u8 regs[sizeof(arm7_reg) * RN_ARM_REG_COUNT];
	u8 mode[sizeof(int)];
	u8 irq_enable[sizeof(bool)];
	u8 fiq_enable[sizeof(bool)];
	u8 enabled[sizeof(bool)];
};

static CpuState state_created;
static CpuState state_reset;

static void cpu_state(const Arm7Context& ctx, CpuState& state)
{
	memcpy(state.regs, ctx.regs, sizeof(state.regs));
	memcpy(state.mode, &ctx.armMode, sizeof(state.mode));
	memcpy(state.irq_enable, &ctx.armIrqEnable, sizeof(state.irq_enable));
	memcpy(state.fiq_enable, &ctx.armFiqEnable, sizeof(state.fiq_enable));
	memcpy(state.enabled, &ctx.enabled, sizeof(state.enabled));
}

void arm7m_init()
{
	static NoSh4 sh4;
	static NoSystemBus sb;
	static NoAsic asic;
	static NoDsp dsp;

	//The interpreter fetches 4 bytes from any address below the end of the RAM
	ram = (u8*)calloc(1, ARM7M_RAM_SIZE + 4);
	verify(ram != 0);

	memset(sh4.devices, 0, sizeof(sh4.devices));
	sh4_cpu = &sh4;

	aica = AICA::Create(0, &sb, &asic, &dsp, &aica_ctx, ram, ARM7M_RAM_SIZE);
	scpu = static_cast<SoundCPU_impl*>(SoundCPU::Create(aica, ram, ARM7M_RAM_SIZE));
	cpu_state(scpu->ctx, state_created);

	sh4.SetA0Handler(A0H_AICA, aica);
	sh4.SetA0Handler(A0H_SCPU, scpu);

	//as the emulator brings its devices up: the AICA releases ARMRST when it is reset, the ARM is stopped when
	//it is reset itself
	aica->Init();
	scpu->Init();
	aica->Reset(false);
	scpu->Reset(false);
	cpu_state(scpu->ctx, state_reset);
}

bool arm7m_created_reset()
{
	return memcmp(&state_created, &state_reset, sizeof(CpuState)) == 0;
}

void arm7m_power_on()
{
	scpu->Reset(false);
}

bool arm7m_controller_idle()
{
	const Arm7Context& ctx = scpu->ctx;

	return !ctx.aica_interr && ctx.aica_reg_L == 0 && !ctx.e68k_out && ctx.e68k_reg_L == 0 && ctx.e68k_reg_M == 0;
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

void arm7m_set_flags(u32 nzcv)
{
	scpu->ctx.regs[RN_PSR_FLAGS].FLG.NZCV = nzcv;
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
	memset(aica_ctx.regs, 0, sizeof(aica_ctx.regs));
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
	return (aica_ctx.regs[MCIPD_addr] & MCIPD_SCPU) != 0;
}

void arm7m_sample()
{
	libAICA_TimeStep();
}

u32 arm7m_read32(u32 addr)
{
	return scpu->ctx.read32(addr, &scpu->ctx);
}

void arm7m_write32(u32 addr, u32 data)
{
	scpu->ctx.write32(addr, data, &scpu->ctx);
}
