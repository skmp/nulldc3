/*
	This file is part of libswirl
*/
#include "license/bsd"


// nullAICA.cpp : Defines the entry point for the DLL application.
//

#include "hw/aica/aica_mmio.h"
#include "SoundCPU.h"
#include "arm7.h"
#include "arm7_context.h"

#include <memory>

#define REG_L (0x2D00)
#define REG_M (0x2D04)

#define N_FLAG regs[RN_PSR_FLAGS].FLG.N
#define Z_FLAG regs[RN_PSR_FLAGS].FLG.Z
#define C_FLAG regs[RN_PSR_FLAGS].FLG.C
#define V_FLAG regs[RN_PSR_FLAGS].FLG.V


/*
	--Seems like aica has 3 interrupt controllers actualy (damn lazy sega ..)
	The "normal" one (the one that exists on scsp) , one to emulate the 68k intc , and ,
	of course , the arm7 one

	The output of the sci* bits is input to the e68k , and the output of e68k is inputed into the FIQ
	pin on arm7
*/

/*
	The handshake, as on the console (wren7-rtl model/tests/hw/fiqdiag, model/NOTES.md "Interrupts"):
	a request is latched with its level and pulls nFIQ, reading L releases nFIQ, writing bit 0 of M ends the
	service and lets the next request in
*/
static void update_e68k(Arm7Context* ctx)
{
	if (!ctx->e68k_reg_M && ctx->aica_interr)
	{
		ctx->e68k_reg_L = ctx->aica_reg_L;

		//a request of level 0 does not interrupt, and L reads 0 behind it (console: fiqdiag d1.bin, d4.bin, d5.bin)
		if (ctx->aica_reg_L != 0)
		{
			//Set the pending signal
			ctx->e68k_reg_M = 1;
			ctx->e68k_out = true;

			ctx->backend->UpdateInterrupts();
		}
	}
}

static void e68k_AcknowledgeInterrupt(Arm7Context* ctx)
{
	if (ctx->e68k_out)
	{
		ctx->e68k_out = false;
		ctx->backend->UpdateInterrupts();
	}
}

static void e68k_AcceptInterrupt(Arm7Context* ctx)
{
	ctx->e68k_reg_M = 0;
	update_e68k(ctx);
	
}

//Reg reads from arm side ..
template <u32 sz, class T>
static T arm_ReadReg(Arm7Context* ctx, u32 addr)
{
	addr &= 0x7FFF;
	if (addr == REG_L)
	{
		e68k_AcknowledgeInterrupt(ctx);
		return ctx->e68k_reg_L;
	}
	else if (addr == REG_M)
		return 0;	//shouldn't really happen
	else
		return ctx->aica->ReadReg(addr, sz);
}

template <u32 sz, class T>
static void arm_WriteReg(Arm7Context* ctx, u32 addr, T data)
{
	addr &= 0x7FFF;
	if (addr == REG_L)
	{
		return; // Shouldn't really happen (read only)
	}
	else if (addr == REG_M)
	{
		//accept interrupts
		if (data & 1)
			e68k_AcceptInterrupt(ctx);
	}
	else
	{
		return ctx->aica->WriteReg(addr, data, sz);
	}
}

//00000000~007FFFFF @DRAM_AREA* 
//00800000~008027FF @CHANNEL_DATA 
//00802800~00802FFF @COMMON_DATA 
//00803000~00807FFF @DSP_DATA 


template<int sz, typename T>
static T DYNACALL scpu_ReadMemArm(u32 addr, Arm7Context* ctx)
{
	T rv;

	addr &= 0x00FFFFFF;
	if (addr < 0x800000)
	{
		rv = *(T*)&ctx->aica_ram[addr & (ctx->aram_mask - (sz - 1))];
	}
	else
	{
		rv = arm_ReadReg<sz, T>(ctx, addr);
	}

	if (unlikely(sz == 4 && addr & 3))
	{
		u32 sf = (addr & 3) * 8;
		return (rv >> sf) | (rv << (32 - sf));
	}
	else
		return rv;
}

template<int sz, typename T>
static void DYNACALL scpu_WriteMemArm(u32 addr, T data, Arm7Context* ctx)
{
	addr &= 0x00FFFFFF;
	if (addr < 0x800000)
	{
		*(T*)&ctx->aica_ram[addr & (ctx->aram_mask - (sz - 1))] = data;
	}
	else
	{
		arm_WriteReg<sz, T>(ctx, addr, data);
	}
}

struct SoundCPU_impl : SoundCPU {
	Arm7Context ctx;
	unique_ptr<ARM7Backend> arm;

	SoundCPU_impl(AICA* aica, u8* aica_ram, u32 aram_size) {
		ctx.aica_ram = aica_ram;
		ctx.aram_mask = aram_size - 1;
		ctx.aica = aica;

		ctx.read8 = &scpu_ReadMemArm<1, u8>;
		ctx.read32 = &scpu_ReadMemArm<4, u32>;

		ctx.write8 = &scpu_WriteMemArm<1, u8>;
		ctx.write32 = &scpu_WriteMemArm<4, u32>;

		setBackend(ARM7BE_INTERPRETER);

		//The reset of the AICA releases ARMRST before the first Reset() of this device: the reset state from the start
		SoundCPU_impl::Reset(false);
	}

	//called when plugin is used by emu (you should do first time init here)
	bool Init()
	{
		return true;
	}

	//called when plugin is unloaded by emu, only if dcInit is called (eg, not called to enumerate plugins)
	void Term()
	{
		//arm7_Term ?
	}

	//It's supposed to reset anything
	void Reset(bool Manual)
	{
		ctx.enabled = false;
		// clean registers
		memset(&ctx.regs[0], 0, sizeof(ctx.regs));

		ctx.armMode = 0x13;

		ctx.regs[13].I = 0x03007F00;
		ctx.regs[15].I = 0x0000000;
		ctx.regs[16].I = 0x00000000;
		ctx.regs[R13_IRQ].I = 0x03007FA0;
		ctx.regs[R13_SVC].I = 0x03007FE0;
		ctx.armIrqEnable = true;
		ctx.armFiqEnable = false;

		//armState = true;
		ctx.C_FLAG = ctx.V_FLAG = ctx.N_FLAG = ctx.Z_FLAG = false;

		// disable FIQ
		ctx.regs[16].I |= 0x40;

		//The AICA's requests go with its registers: nothing is pending, pulls nFIQ or is in service, L is 0
		ctx.aica_interr = false;
		ctx.aica_reg_L = 0;
		ctx.e68k_out = false;
		ctx.e68k_reg_L = 0;
		ctx.e68k_reg_M = 0;

		ARM7Backend::CPUUpdateCPSR(&ctx);

		ctx.regs[R15_ARM_NEXT].I = ctx.regs[15].I;
		ctx.regs[15].I += 4;

		arm->UpdateInterrupts();
		arm->InvalidateJitCache();
	}

	//ARMRST is released: the reset of the ARM7DI (data sheet 3.5, console: wren7-rtl model/tests/hw/bringup).
	//R14_svc and SPSR_svc take the PC and the CPSR, the CPSR gets SVC mode with I and F set and the PC is 0.
	//Every other register keeps its value
	void ResetException()
	{
		u32 pc = ctx.regs[R15_ARM_NEXT].I;

		ARM7Backend::CPUSwitchMode(&ctx, 0x13, true);
		ctx.regs[14].I = pc;

		ctx.regs[RN_PSR_FLAGS].FLG.NZCV = ctx.regs[RN_SPSR].PSR.NZCV;
		ctx.armIrqEnable = false;
		ctx.armFiqEnable = false;
		ARM7Backend::CPUUpdateCPSR(&ctx);

		ctx.regs[R15_ARM_NEXT].I = 0;
		ctx.regs[15].I = 4;
		ctx.regs[CYCL_CNT].I = 0;

		arm->UpdateInterrupts();
		arm->InvalidateJitCache();
	}

	void SetResetState(u32 state)
	{
		bool enabled = state == 0;

		if (!ctx.enabled && enabled)
			ResetException();

		ctx.enabled = enabled;
	}

	//Mainloop
	void Update(u32 Cycles)
	{
		if (ctx.enabled) {
			for (int i = 0; i < 32; i++)
			{
				arm->Run(Cycles / 32 / arm_sh4_bias);
				libAICA_TimeStep();
			}
		}
	}

	bool setBackend(Arm7Backends backend) {

		if (backend == ARM7BE_INTERPRETER) {
			arm.reset(ARM7Backend::CreateInterpreter(&ctx));
			ctx.backend = arm.get();

			return true;
		}
#if FEAT_AREC != DYNAREC_NONE
		else if (backend == ARM7BE_DYNAREC) {
			arm.reset(ARM7Backend::CreateJit(&ctx));
			ctx.backend = arm.get();

			return true;
		}
#endif

		return false;
	}

	void InterruptChange(u32 bits, u32 L)
	{
		ctx.aica_interr = bits != 0;
		if (ctx.aica_interr)
			ctx.aica_reg_L = L;
		update_e68k(&ctx);
	}

	void InvalidateJitCache() {
		arm->InvalidateJitCache();
	}

	void serialize(void** data, unsigned int* total_size)
	{
		REICAST_S(ctx.aica_interr);
		REICAST_S(ctx.aica_reg_L);
		REICAST_S(ctx.e68k_out);
		REICAST_S(ctx.e68k_reg_L);
		REICAST_S(ctx.e68k_reg_M);

		REICAST_SA(ctx.regs, RN_ARM_REG_COUNT);
		REICAST_S(ctx.armIrqEnable);
		REICAST_S(ctx.armFiqEnable);
		REICAST_S(ctx.armMode);
		REICAST_S(ctx.enabled);
	}

	void unserialize(void** data, unsigned int* total_size)
	{
		REICAST_US(ctx.aica_interr);
		REICAST_US(ctx.aica_reg_L);
		REICAST_US(ctx.e68k_out);
		REICAST_US(ctx.e68k_reg_L);
		REICAST_US(ctx.e68k_reg_M);

		REICAST_USA(ctx.regs, RN_ARM_REG_COUNT);
		REICAST_US(ctx.armIrqEnable);
		REICAST_US(ctx.armFiqEnable);
		REICAST_US(ctx.armMode);
		REICAST_US(ctx.enabled);
	}
};

SoundCPU* SoundCPU::Create(AICA* aica, u8* aica_ram, u32 aram_size) {
	return new SoundCPU_impl(aica, aica_ram, aram_size);
}

void libARM_SetResetState(bool Reset) {
	sh4_cpu->GetA0H<SoundCPU_impl>(A0H_SCPU)->SetResetState(Reset);
}
void libARM_InterruptChange(u32 bits, u32 L) {
	sh4_cpu->GetA0H<SoundCPU_impl>(A0H_SCPU)->InterruptChange(bits, L);
}
