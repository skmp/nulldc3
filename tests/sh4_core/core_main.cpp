/*
	sh4.core: the SH4 as the emulator sets it up, on SuperH4_impl (the CPU object of the emulator) instead of the
	stand-in of sh4_env.

	The reset state. What the manual defines for a reset has to be there (SH7091 hardware manual, table 2.1
	"Initial register values": PC, SR, VBR and FPSCR, for a power-on and a manual reset alike), and so has the
	host FP mode that goes with that FPSCR. Everything else is undefined in the manual; the emulator clears it,
	and that is checked as what the tree documents.
*/
#include "sh4_env.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mmio.h"
#include "hw/sh4/sh4_mmr.h"

#include <cstdio>
#include <cstring>

#if HOST_CPU == CPU_X64 || HOST_CPU == CPU_X86
#include <xmmintrin.h>
#endif

//hw/sh4/sh4_core.h declares these, along with macros for r, sr, pr, ... that are better kept out of here
void UpdateFPSCR();

extern SuperH4* sh4_cpu;

static u32 checks;
static u32 failures;

#define CHECK(cond) \
	do \
	{ \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_REG(what, actual, expected) \
	do \
	{ \
		checks++; \
		if ((u32)(actual) != (u32)(expected)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s is %08X, expected %08X\n", __FILE__, __LINE__, what, (u32)(actual), (u32)(expected)); \
		} \
	} while (0)

//// What SuperH4_impl has around it in the emulator

//the scheduler behind UpdateSystem
void sh4_sched_tick(int cycles) { }
void sh4_sched_cleanup() { }

struct NoModules : SuperH4Mmr
{
	void Reset() { }
	void rio_reg(void* context, RegisterStruct* arr, size_t arr_size, u32 addr, RegIO flags, u32 sz, RegReadAddrFP* rp, RegWriteAddrFP* wp) { }
};

static SuperH4* make_cpu()
{
	SuperH4* cpu = SuperH4::Create();

	cpu->sh4mmr.reset(new NoModules());

	//a reset resets every device of area 0
	for (u32 slot = 0; slot < A0H_MAX; slot++)
		cpu->SetA0Handler((Area0Hanlders)slot, new MMIODevice());

	sh4_cpu = cpu;

	verify(cpu->Init());

	return cpu;
}

//// The host FP mode of the calling thread

//false: not a host this file knows how to ask
static bool host_fp_mode(bool& round_to_zero, bool& flush_to_zero)
{
#if HOST_CPU == CPU_X64 || HOST_CPU == CPU_X86
	u32 mxcsr = _mm_getcsr();

	round_to_zero = ((mxcsr >> 13) & 3) == 3;
	flush_to_zero = ((mxcsr >> 15) & 1) != 0;

	return true;
#elif HOST_CPU == CPU_ARM64
	u64 fpcr;

	asm volatile("mrs %0, fpcr" : "=r"(fpcr));

	round_to_zero = ((fpcr >> 22) & 3) == 3;
	flush_to_zero = ((fpcr >> 24) & 1) != 0;

	return true;
#else
	return false;
#endif
}

//// Reset

//A value in every register that no reset leaves, and the host rounding to nearest without flushing
static void scramble()
{
	Sh4Context& ctx = Sh4cntx;

	ctx.fpscr.full = 0;
	ctx.old_fpscr.full = 0;
	UpdateFPSCR();

	memset(&ctx, 0xA5, sizeof(ctx));

	ctx.CpuRunning = 0;
}

static void test_reset(SuperH4* cpu, bool manual)
{
	scramble();

	cpu->Reset(manual);

	const Sh4Context& ctx = Sh4cntx;

	//table 2.1
	CHECK_REG("PC", ctx.pc, 0xA0000000);
	CHECK_REG("VBR", ctx.vbr, 0x00000000);
	CHECK_REG("FPSCR", ctx.fpscr.full, 0x00040001);		//DN = 1, RM = 01
	CHECK_REG("SR", sh4_sr_GetFull(), 0x700000F0);		//MD, RB and BL = 1, FD = 0, IMASK = 1111. M, Q, S, T: undefined, here 0

	//the banks are where SR and FPSCR say: nothing is left to swap
	CHECK_REG("old SR", ctx.old_sr.status, ctx.sr.status);
	CHECK_REG("old FPSCR", ctx.old_fpscr.full, ctx.fpscr.full);

	//FPSCR.RM = 01 is round to zero, DN = 1 takes denormals as zero
	bool round_to_zero, flush_to_zero;

	if (host_fp_mode(round_to_zero, flush_to_zero))
	{
		CHECK(round_to_zero);
		CHECK(flush_to_zero);
	}

	//undefined in the manual, 0 in the emulator
	char name[16];

	for (u32 i = 0; i < 16; i++)
	{
		snprintf(name, sizeof(name), "R%u", i);
		CHECK_REG(name, ctx.r[i], 0);
	}

	for (u32 i = 0; i < 8; i++)
	{
		snprintf(name, sizeof(name), "R%u_BANK", i);
		CHECK_REG(name, ctx.r_bank[i], 0);
	}

	CHECK_REG("GBR", ctx.gbr, 0);
	CHECK_REG("SSR", ctx.ssr, 0);
	CHECK_REG("SPC", ctx.spc, 0);
	CHECK_REG("SGR", ctx.sgr, 0);
	CHECK_REG("DBR", ctx.dbr, 0);
	CHECK_REG("MACH", ctx.mac.h, 0);
	CHECK_REG("MACL", ctx.mac.l, 0);
	CHECK_REG("PR", ctx._pr, 0);
	CHECK_REG("FPUL", ctx.fpul, 0);
}

int main()
{
	sh4_env_init();

	SuperH4* cpu = make_cpu();

	test_reset(cpu, false);
	test_reset(cpu, true);

	bool round_to_zero, flush_to_zero;

	if (!host_fp_mode(round_to_zero, flush_to_zero))
		printf("sh4.core: the host FP mode is not checked on this host\n");

	printf("sh4.core: %u checks, %u failed\n", checks, failures);

	return failures ? 1 : 0;
}
