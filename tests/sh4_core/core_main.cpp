/*
	sh4.core: the SH4 as the emulator sets it up, on SuperH4_impl (the CPU object of the emulator) instead of the
	stand-in of sh4_env.

	The reset state. What the manual defines for a reset has to be there (SH7091 hardware manual, table 2.1
	"Initial register values": PC, SR, VBR and FPSCR, for a power-on and a manual reset alike), and so has the
	host FP mode that goes with that FPSCR. Everything else is undefined in the manual; the emulator clears it,
	and that is checked as what the tree documents.

	FPSCR.RM and the host rounding mode. The interpreter and the code of the recompilers compute with the host
	FPU in the mode UpdateFPSCR sets, so that mode is what decides how both round.

	The host FP mode and threads. The mode belongs to a thread, and the emulator resets, loads states and runs
	the CPU on more than one: the thread that starts to run the CPU has to end up in the mode of FPSCR, whichever
	thread wrote FPSCR last.

	TRAPA. EXPEVT and TRA are module registers and not part of a vector state, the directed vectors have the
	rest of it.
*/
#include "sh4_env.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_mmio.h"
#include "hw/sh4/sh4_mmr.h"

#include <cstdio>
#include <cstring>
#include <future>
#include <thread>

#if HOST_CPU == CPU_X64 || HOST_CPU == CPU_X86
#include <xmmintrin.h>
#endif

//hw/sh4/sh4_core.h declares these, along with macros for r, sr, pr, ... that are better kept out of here
void UpdateFPSCR();
bool UpdateSR();
void sh4_host_fp_resync();

//hw/sh4/sh4_core_regs.cpp: the FPSCR.RM and DN the host FP mode was last set for. A save state carries them
extern u32 old_rm;
extern u32 old_dn;

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

//The host takes a denormal operand as zero: 2^-128 * 2^30 is 0 then, and 2^-98 when it computes with the denormal
static bool host_denormal_operand_is_zero()
{
	volatile f32 denormal;
	volatile f32 factor = 1073741824.0f;

	const u32 pattern = 0x00200000;
	f32 value;

	memcpy(&value, &pattern, sizeof(value));
	denormal = value;

	volatile f32 product = denormal * factor;

	u32 bits;

	value = product;
	memcpy(&bits, &value, sizeof(bits));

	return bits == 0;
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
		CHECK(host_denormal_operand_is_zero());
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

//// FPSCR.RM

//RM = 00 rounds to nearest and 01 to zero. 10 and 11 are reserved: no console was measured with them, they are
//expected to round as their bit 0 says, which is what the RTL and the soft-float reference of shrike4-rtl do.
//DN is flush to zero whatever RM is, of denormal results and of denormal operands
static void test_rounding_mode(SuperH4* cpu)
{
	bool round_to_zero, flush_to_zero;

	if (!host_fp_mode(round_to_zero, flush_to_zero))
		return;

	cpu->Reset(false);

	Sh4Context& ctx = Sh4cntx;

	for (u32 dn = 0; dn < 2; dn++)
	{
		for (u32 rm = 0; rm < 4; rm++)
		{
			u32 value = (dn << 18) | rm;

			//lds r3,fpscr, with the bits FPSCR does not have set as well
			ctx.r[3] = 0xFFC00000 | value;
			ExecuteOpcode(0x436A);

			CHECK_REG("FPSCR", ctx.fpscr.full, value);

			CHECK(host_fp_mode(round_to_zero, flush_to_zero));
			CHECK(round_to_zero == ((rm & 1) != 0));
			CHECK(flush_to_zero == (dn != 0));
			CHECK(host_denormal_operand_is_zero() == (dn != 0));
		}
	}
}

//// Threads

#define FPSCR_NEAREST 0x00000000	//RM = 00, DN = 0
#define FPSCR_ZERO_FLUSH 0x00040001	//RM = 01, DN = 1: the reset value

struct HostMode
{
	bool round_to_zero;
	bool flush_to_zero;
};

static HostMode host_mode()
{
	HostMode mode;

	host_fp_mode(mode.round_to_zero, mode.flush_to_zero);

	return mode;
}

//lds r3,fpscr
static void load_fpscr(u32 value)
{
	Sh4cntx.r[3] = value;
	ExecuteOpcode(0x436A);
}

//What the emulator thread does before it runs the CPU (dc_run), and the mode it is in then
static HostMode start_running()
{
	sh4_host_fp_resync();

	return host_mode();
}

template<typename F>
static HostMode on_new_thread(F fn)
{
	HostMode mode;
	std::thread thread([&mode, &fn] { mode = fn(); });

	thread.join();

	return mode;
}

static void test_host_fp_threads(SuperH4* cpu)
{
	bool round_to_zero, flush_to_zero;

	if (!host_fp_mode(round_to_zero, flush_to_zero))
		return;

	HostMode mode;

	//A reset on one thread, the CPU on another one that did not get its FP mode from the first: here a thread that
	//is there before the reset, in the emulator the emulator thread on a host whose new threads start in the default
	//mode
	cpu->Reset(false);
	load_fpscr(FPSCR_NEAREST);

	{
		std::promise<void> reset_done;
		std::future<void> wait = reset_done.get_future();
		std::thread emulator([&mode, &wait] { wait.wait(); mode = start_running(); });

		cpu->Reset(false);

		reset_done.set_value();
		emulator.join();
	}

	CHECK_REG("FPSCR", Sh4cntx.fpscr.full, FPSCR_ZERO_FLUSH);
	CHECK(mode.round_to_zero);
	CHECK(mode.flush_to_zero);

	//The emulator thread stops (the menu) and a new one is started by a thread that is not in the guest's mode: the
	//thread that reset is in the mode of the reset, the guest has loaded FPSCR since
	cpu->Reset(false);

	on_new_thread([] { start_running(); load_fpscr(FPSCR_NEAREST); return host_mode(); });

	mode = on_new_thread(start_running);

	CHECK_REG("FPSCR", Sh4cntx.fpscr.full, FPSCR_NEAREST);
	CHECK(!mode.round_to_zero);
	CHECK(!mode.flush_to_zero);

	//A state is loaded while the CPU is stopped: FPSCR and what the host mode was last set for come out of the file,
	//no thread's mode changes
	cpu->Reset(false);

	Sh4cntx.fpscr.full = FPSCR_NEAREST;
	Sh4cntx.old_fpscr.full = FPSCR_NEAREST;
	old_rm = 0;
	old_dn = 0;

	mode = on_new_thread(start_running);

	CHECK(!mode.round_to_zero);
	CHECK(!mode.flush_to_zero);

	//and from there on the thread follows the FPSCR the guest loads
	mode = on_new_thread([] { start_running(); load_fpscr(FPSCR_ZERO_FLUSH); return host_mode(); });

	CHECK(mode.round_to_zero);
	CHECK(mode.flush_to_zero);

	mode = on_new_thread([] { start_running(); load_fpscr(FPSCR_ZERO_FLUSH); load_fpscr(FPSCR_NEAREST); return host_mode(); });

	CHECK(!mode.round_to_zero);
	CHECK(!mode.flush_to_zero);
}

//// TRAPA

static void set_sr(u32 value)
{
	sh4_sr_SetFull(value);
	UpdateSR();
}

static void test_trapa(SuperH4* cpu)
{
	SuperH4Mmr* sh4mmr = cpu->sh4mmr.get();		//the CCN_ macros look for it
	Sh4Context& ctx = Sh4cntx;

	//SR.BL = 0, the TRAPA exception: EXPEVT = H'160 and TRA = imm << 2, as a console has them
	cpu->Reset(false);
	set_sr(0x400000F1);

	ctx.vbr = 0x8C0F3000;
	ctx.r[15] = 0x8C00F000;
	ctx.pc = 0x8C010004;	//the address behind TRAPA

	CCN_EXPEVT = 0;
	CCN_TRA = 0;

	ExecuteOpcode(0xC35A);

	CHECK_REG("EXPEVT", CCN_EXPEVT, 0x160);
	CHECK_REG("TRA", CCN_TRA, 0x168);
	CHECK_REG("SSR", ctx.ssr, 0x400000F1);
	CHECK_REG("SPC", ctx.spc, 0x8C010004);
	CHECK_REG("SGR", ctx.sgr, 0x8C00F000);
	CHECK_REG("SR", sh4_sr_GetFull(), 0x700000F1);
	CHECK_REG("PC", ctx.pc, 0x8C0F3100);

	//SR.BL = 1: a general exception is a manual reset of the CPU. EXPEVT = H'020, SR, VBR, FPSCR and PC are the
	//reset's (SH7091 hardware manual 5.5.3, 5.6.1 (2), table 2.1). No console was measured for it: TRA and the
	//registers of the exception that is not taken are expected as they were
	set_sr(0x50008001);
	load_fpscr(0x00140000);

	ctx.vbr = 0x8C0F3000;
	ctx.ssr = 0x11111111;
	ctx.spc = 0x22222222;
	ctx.sgr = 0x33333333;
	ctx.pc = 0x8C010004;

	CCN_EXPEVT = 0x160;
	CCN_TRA = 0x168;

	ExecuteOpcode(0xC3FF);

	CHECK_REG("EXPEVT", CCN_EXPEVT, 0x020);
	CHECK_REG("TRA", CCN_TRA, 0x168);
	CHECK_REG("SR", sh4_sr_GetFull(), 0x700000F0);		//M, Q, S, T: undefined after a reset, here 0 as after Reset
	CHECK_REG("VBR", ctx.vbr, 0);
	CHECK_REG("FPSCR", ctx.fpscr.full, 0x00040001);
	CHECK_REG("PC", ctx.pc, 0xA0000000);
	CHECK_REG("SSR", ctx.ssr, 0x11111111);
	CHECK_REG("SPC", ctx.spc, 0x22222222);
	CHECK_REG("SGR", ctx.sgr, 0x33333333);

	bool round_to_zero, flush_to_zero;

	if (host_fp_mode(round_to_zero, flush_to_zero))
	{
		CHECK(round_to_zero);
		CHECK(flush_to_zero);
	}
}

int main()
{
	sh4_env_init();

	SuperH4* cpu = make_cpu();

	test_reset(cpu, false);
	test_reset(cpu, true);
	test_rounding_mode(cpu);
	test_host_fp_threads(cpu);
	test_trapa(cpu);

	bool round_to_zero, flush_to_zero;

	if (!host_fp_mode(round_to_zero, flush_to_zero))
		printf("sh4.core: the host FP mode is not checked on this host\n");

	printf("sh4.core: %u checks, %u failed\n", checks, failures);

	return failures ? 1 : 0;
}
