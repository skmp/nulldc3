/*
	windows.abi.xmm: the main loop and RunBlock of the x64 backend give xmm6-xmm15 back as they found them, as the
	Windows x64 convention wants of a callee (jit/backend/x64/rec_x64.cpp, mainloop_prologue).

	On Windows the register allocator keeps guest FPU registers in xmm6-xmm15 (x64_regalloc.h). A thunk made with
	Xbyak (MSVC x64 has no inline assembly) loads ten canaries into xmm6-xmm15, calls the code under test, stores
	what the ten registers hold when it returns and gives its own caller its registers back. The machine is the
	least the backend needs (tests/common/sh4_env.cpp, 64 KiB of RAM behind memory handlers), modelled on
	tests/sh4_dynarec/dynarec_runner.cpp. The guest code adds twelve FPU registers to each other in a loop, so that
	its blocks use every one of xmm6-xmm15:
	    RunBlock   one block of the twelve additions, compiled as dynarec_runner.cpp compiles one, run once; then
	               once more with one cycle left in the slice, so that RunBlock ends the slice and calls
	               UpdateSystem_INTC
	    Mainloop   the loop through the block manager until the slice ends: UpdateSystem_INTC of this file stops
	               the CPU there, and the main loop returns
	Each passes when the ten registers came back as the canaries and the guest's registers have the sums.

	The stack walks: the code region of this machine is the emulator's on Windows, a region of its own near the
	program with the emulator's unwind data (libswirl/windows/win_jit_unwind.cpp), so this file has its own
	vmem_platform_prepare_jit_block and the other stubs of tests/common/sh4_stubs_rec.cpp (which is then not linked).
	The thunk also loads canaries into the eight general registers a callee keeps, and notes its rsp at the call.
	UpdateSystem_INTC, called by RunBlock and by Mainloop at the end of the slice, walks the stack from where it is
	with the system's unwinder (RtlLookupFunctionEntry, RtlVirtualUnwind): the walk has to come to the return
	address in the thunk, with the thunk's rsp, xmm6-xmm15 and general registers, as a debugger, a crash dump or
	an exception that nobody takes would walk it. A return address in RunBlock that is a jump out of its range is
	unwound by the system as an epilogue, and that walk leaves the stack (rec_x64.cpp, build_runblock).
*/
#ifndef XBYAK_NO_OP_NAMES
#define XBYAK_NO_OP_NAMES
#endif
#include "deps/xbyak/xbyak.h"

#include <windows.h>

#include "sh4_env.h"
#include "test_guard.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/dyna/decoder.h"
#include "hw/sh4/dyna/ngen.h"
#include "hw/mem/_vmem.h"
#include "hw/sh4/sh4_mem.h"
#include "windows/win_jit_unwind.h"

#include <cstdio>
#include <cstring>

//hw/sh4/dyna/driver.cpp, hw/sh4/dyna/shil.cpp, jit/backend/x64/rec_x64.cpp
extern u32 LastAddr;
extern u32 LastAddr_min;
void AnalyseBlock(RuntimeBlockInfo* blk);
extern "C" int cycle_counter;

//What tests/common/sh4_stubs_rec.cpp has, but the code region: a region of its own near the program, as
//libswirl/windows/win_vmem.cpp makes the emulator's, with the page behind it for the unwind data, which the
//emulator's win_jit_unwind.cpp writes and hands to the system. Inside the program's image (the stubs' region is
//SH4_TCB) the system would never ask for it: it takes the unwind data of a pc in an image from the image.
u8* GetMemPtr(u32 Addr, u32 size) { return 0; }
bool IsOnRam(u32 addr) { return false; }
string get_writable_data_path(const string& filename) { return filename; }

static const size_t unwind_page = 4096;

bool vmem_platform_prepare_jit_block(void* code_area, unsigned size, void** code_area_rwx)
{
	//within 2 GB of the program, as the code addresses the program's data relative to rip
	uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr) & ~(uintptr_t)0xFFFFF;
	for (uintptr_t step = 8 * 1024 * 1024; step < 1800u * 1024 * 1024; step += 8 * 1024 * 1024)
	{
		for (uintptr_t at : { base + step, base - step })
		{
			void* region = VirtualAlloc((void*)at, size + unwind_page, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (region)
			{
				win_jit_code_region_prepared(region, size);
				*code_area_rwx = region;
				return true;
			}
		}
	}
	return false;
}

//The guest: 64 KiB of RAM at 0x0C000000 and its mirrors, the code at 0x8C010000
static u8 ram[64 * 1024];
static u32 ram_handler_ctx;

static u8 DYNACALL read8(void*, u32 addr) { return ram[addr & 0xFFFF]; }
static u16 DYNACALL read16(void*, u32 addr) { u16 v; memcpy(&v, &ram[addr & 0xFFFE], 2); return v; }
static u32 DYNACALL read32(void*, u32 addr) { u32 v; memcpy(&v, &ram[addr & 0xFFFC], 4); return v; }
static void DYNACALL write8(void*, u32 addr, u8 data) { ram[addr & 0xFFFF] = data; }
static void DYNACALL write16(void*, u32 addr, u16 data) { memcpy(&ram[addr & 0xFFFE], &data, 2); }
static void DYNACALL write32(void*, u32 addr, u32 data) { memcpy(&ram[addr & 0xFFFC], &data, 4); }

static const u32 code_pc = 0x8C010000;
static const int fp_regs = 12;

//fadd fr((n+1)%12), fr(n) for n = 0..11 (1111nnnnmmmm0000), then bra code_pc with a nop in its delay slot (the
//target is the address of the bra + 4 + disp * 2): a block of the twelve additions that ends in the branch
static void put_code()
{
	u32 at = code_pc & 0xFFFF;
	for (int n = 0; n < fp_regs; n++, at += 2)
	{
		u16 op = (u16)(0xF000 | (n << 8) | (((n + 1) % fp_regs) << 4));
		memcpy(&ram[at], &op, 2);
	}
	u16 bra = (u16)(0xA000 | ((u32)(-(int)(fp_regs * 2 + 4) / 2) & 0xFFF));
	u16 nop = 0x0009;
	memcpy(&ram[at], &bra, 2);
	memcpy(&ram[at + 2], &nop, 2);
}

//The stack walk of UpdateSystem_INTC: what it found
struct Walk
{
	int frames;          //frames unwound
	bool reached;        //it came to the return address in the thunk
	bool faulted;        //the walk faulted (and stopped)
	bool rsp_ok, xmm_ok, gpr_ok;
	char trail[512];     //the frames it went through
};
static Walk walk;
static bool walk_armed;
static DWORD64 thunk_return;   //the return address of the thunk's call of the code under test
static DWORD64 thunk_rsp;      //the thunk's rsp at that call (the thunk writes it)
static u8 canaries[160 + 64];  //xmm6-xmm15, then rbx rbp rdi rsi r12 r13 r14 r15

static void trail_add(const char* where, DWORD64 offset)
{
	size_t n = strlen(walk.trail);
	if (n < sizeof(walk.trail) - 40)
		sprintf_s(walk.trail + n, sizeof(walk.trail) - n, "%s+%llx ", where, (unsigned long long)offset);
}

//From here to the thunk with the system's unwinder: a frame of this program or of the code region at a time
__declspec(noinline) static void walk_to_thunk()
{
	CONTEXT ctx;
	RtlCaptureContext(&ctx);
	ULONG_PTR low = 0, high = 0;
	GetCurrentThreadStackLimits(&low, &high);
	const DWORD64 image = (DWORD64)GetModuleHandleW(nullptr);
	memset(&walk, 0, sizeof(walk));
	__try
	{
		for (int depth = 0; depth < 12; depth++)
		{
			if (ctx.Rip == thunk_return)
			{
				walk.reached = true;
				break;
			}
			if (ctx.Rsp < low || ctx.Rsp + 512 > high)
			{
				trail_add("rsp-off-the-stack", ctx.Rsp);
				break;
			}
			DWORD64 base = 0;
			PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
			bool in_region = ctx.Rip >= (DWORD64)CodeCache && ctx.Rip < (DWORD64)CodeCache + CODE_SIZE;
			bool in_image = f && base == image;
			if (!in_region && !in_image)
			{
				trail_add("elsewhere", ctx.Rip);	//no frame of this program
				break;
			}
			trail_add(in_region ? "region" : "program", ctx.Rip - (in_region ? (DWORD64)CodeCache : image));
			if (f)
			{
				void* handler_data = nullptr;
				DWORD64 establisher = 0;
				RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, f, &ctx, &handler_data, &establisher, nullptr);
			}
			else
			{
				ctx.Rip = *(DWORD64*)ctx.Rsp;	//a leaf
				ctx.Rsp += 8;
			}
			walk.frames++;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		walk.faulted = true;
	}
	if (!walk.reached)
		return;
	walk.rsp_ok = ctx.Rsp == thunk_rsp;
	walk.xmm_ok = memcmp(&ctx.Xmm6, canaries, 160) == 0;
	DWORD64 gpr[8] = { ctx.Rbx, ctx.Rbp, ctx.Rdi, ctx.Rsi, ctx.R12, ctx.R13, ctx.R14, ctx.R15 };
	walk.gpr_ok = memcmp(gpr, canaries + 160, 64) == 0;
}

//The main loop ends at the end of its first slice: this file's UpdateSystem, in place of tests/common's
static u32 slice_ends;
extern "C" int UpdateSystem()
{
	slice_ends++;
	sh4_int_bCpuRun = false;
	return 0;
}
extern "C" int UpdateSystem_INTC()
{
	if (walk_armed)
	{
		walk_armed = false;
		walk_to_thunk();
	}
	return UpdateSystem();
}

//void thunk(void (*code)(void*), void* arg, const u8 canaries[160 + 64], u8 out[160])
typedef void ThunkFP(void* code, void* arg, const u8* canaries, u8* out);

struct CanaryThunk : Xbyak::CodeGenerator
{
	CanaryThunk(void* buffer) : Xbyak::CodeGenerator(1024, buffer)
	{
		//rcx code, rdx arg, r8 canaries, r9 out. Frame: 32 bytes for the callee, out, the caller's ten xmm
		//registers, and what keeps the stack 16 byte aligned
		const int frame = 32 + 16 + 10 * 16 + 8;
		push(rbx);
		push(rbp);
		push(rdi);
		push(rsi);
		push(r12);
		push(r13);
		push(r14);
		push(r15);
		sub(rsp, frame);
		for (int i = 0; i < 10; i++)
			movaps(xword[rsp + 48 + i * 16], Xbyak::Xmm(6 + i));
		mov(qword[rsp + 32], r9);

		for (int i = 0; i < 10; i++)
			movups(Xbyak::Xmm(6 + i), xword[r8 + i * 16]);
		const Xbyak::Reg64 kept[8] = { rbx, rbp, rdi, rsi, r12, r13, r14, r15 };
		for (int i = 0; i < 8; i++)
			mov(kept[i], qword[r8 + 160 + i * 8]);
		mov(rax, (size_t)&thunk_rsp);
		mov(qword[rax], rsp);
		mov(rax, rcx);
		mov(rcx, rdx);
		call(rax);
		thunk_return = (DWORD64)CC_RW2RX(getCurr());

		mov(r9, qword[rsp + 32]);
		for (int i = 0; i < 10; i++)
			movups(xword[r9 + i * 16], Xbyak::Xmm(6 + i));

		for (int i = 0; i < 10; i++)
			movaps(Xbyak::Xmm(6 + i), xword[rsp + 48 + i * 16]);
		add(rsp, frame);
		pop(r15);
		pop(r14);
		pop(r13);
		pop(r12);
		pop(rsi);
		pop(rdi);
		pop(rbp);
		pop(rbx);
		ret();
		ready();
	}
};

static ThunkFP* thunk;
static SuperH4Backend* recompiler;
static int failures;

static void check(bool ok, const char* what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		failures++;
}

static void reset_guest()
{
	memset(r, 0, sizeof(r));
	memset(Sh4cntx.xffr, 0, sizeof(Sh4cntx.xffr));
	for (int n = 0; n < fp_regs; n++)
		fr[n] = (float)(n + 1);
	sh4_sr_SetFull(0x700000F0);
	old_sr.status = sr.status;
	UpdateSR();
	fpscr.full = 0x00040001;
	old_fpscr = fpscr;
	UpdateFPSCR();
	Sh4cntx.pc = code_pc;
}

//The sums of one pass of the twelve additions, from fr[n] = n + 1
static void expected_pass(float* f, int passes)
{
	for (int n = 0; n < fp_regs; n++)
		f[n] = (float)(n + 1);
	for (int p = 0; p < passes; p++)
		for (int n = 0; n < fp_regs; n++)
			f[n] = f[n] + f[(n + 1) % fp_regs];
}

static bool canaries_back(const u8* canaries, const u8* out, const char* what)
{
	bool same = memcmp(canaries, out, 160) == 0;
	for (int i = 0; i < 10; i++)
	{
		if (memcmp(canaries + i * 16, out + i * 16, 16) != 0)
		{
			u32 got[4];
			memcpy(got, out + i * 16, 16);
			printf("  %s: xmm%d came back as %08x %08x %08x %08x\n", what, 6 + i, got[0], got[1], got[2], got[3]);
		}
	}
	return same;
}

static void make_canaries(u8* canaries, u32 seed)
{
	for (int i = 0; i < 56; i++)
	{
		u32 v = 0xC0DE0000u ^ (seed << 8) ^ (u32)(i * 0x9E3779B9u);
		memcpy(canaries + i * 4, &v, 4);
	}
}

static void check_walk(const char* what)
{
	printf("%s: the walk from UpdateSystem_INTC: %d frame(s): %s%s\n", what, walk.frames, walk.trail,
	       walk.faulted ? "(it faulted)" : "");
	char text[160];
	sprintf_s(text, "%s: a stack walk from UpdateSystem_INTC comes to the return address in the thunk", what);
	check(walk.reached, text);
	sprintf_s(text, "%s: ... with the thunk's rsp, xmm6-xmm15 and general registers", what);
	check(walk.reached && walk.rsp_ok && walk.xmm_ok && walk.gpr_ok, text);
}

static void test_runblock()
{
	reset_guest();
	put_code();

	RuntimeBlockInfo* block = rdv_ngen->AllocateBlock();
	block->staging_runs = block->addr = block->lookups = block->runs = block->host_code_size = 0;
	block->guest_cycles = block->guest_opcodes = block->host_opcodes = 0;
	block->pBranchBlock = block->pNextBlock = 0;
	block->code = 0;
	block->has_jcond = false;
	block->BranchBlock = block->NextBlock = block->csc_RetCache = 0xFFFFFFFF;
	block->BlockType = BET_SCL_Intr;
	block->addr = code_pc;
	block->fpu_cfg = fpscr;
	block->oplist.clear();

	//the block ends in the branch
	dec_DecodeBlock(block, SH4_TIMESLICE);
	AnalyseBlock(block);
	block->staging_runs = 100;
	rdv_ngen->Compile(block, NoCheck, false, false, true);
	verify(block->code != 0);

	u8 out[160];
	make_canaries(canaries, 1);
	sh4_int_bCpuRun = true;
	cycle_counter = SH4_TIMESLICE;
	slice_ends = 0;
	thunk((void*)rdv_ngen->RunBlock, (void*)CC_RW2RX(block->code), canaries, out);

	float want[fp_regs];
	expected_pass(want, 1);
	bool sums = true;
	for (int n = 0; n < fp_regs; n++)
		sums = sums && fr[n] == want[n];

	printf("RunBlock: a block of %u instructions, %u host bytes; the pc after it %08x\n", block->guest_opcodes,
	       block->host_code_size, Sh4cntx.pc);
	check(block->guest_opcodes == (u32)fp_regs + 2 && Sh4cntx.pc == code_pc, "RunBlock ran the block of the twelve additions and the branch");
	check(sums, "RunBlock: the guest's FPU registers have the sums of one pass");
	check(canaries_back(canaries, out, "RunBlock"), "RunBlock gives xmm6-xmm15 back as they were");
	check(slice_ends == 0, "RunBlock: the slice did not run out");

	//once more with one cycle left: the block uses the slice up, and RunBlock calls UpdateSystem_INTC
	make_canaries(canaries, 3);
	cycle_counter = 1;
	slice_ends = 0;
	walk_armed = true;
	sh4_int_bCpuRun = true;
	thunk((void*)rdv_ngen->RunBlock, (void*)CC_RW2RX(block->code), canaries, out);
	check(slice_ends == 1, "RunBlock with the slice used up: it ended the slice (UpdateSystem_INTC)");
	check(canaries_back(canaries, out, "RunBlock at the end of the slice"), "RunBlock at the end of the slice gives xmm6-xmm15 back as they were");
	check_walk("RunBlock");

	delete block;
}

static void test_mainloop()
{
	reset_guest();
	put_code();
	recompiler->ClearCache();

	u8 out[160];
	make_canaries(canaries, 2);
	cycle_counter = SH4_TIMESLICE;
	slice_ends = 0;
	walk_armed = true;
	sh4_int_bCpuRun = true;
	thunk((void*)rdv_ngen->Mainloop, sh4_dyna_rcb, canaries, out);

	printf("Mainloop: %u slice end(s), the pc %08x, fr0 %g\n", slice_ends, Sh4cntx.pc, (double)fr[0]);
	check(slice_ends == 1, "Mainloop ran its slice and returned at its end");
	bool changed = false;
	for (int n = 0; n < fp_regs; n++)
		changed = changed || fr[n] != (float)(n + 1);
	check(changed, "Mainloop: the guest's FPU registers have changed");
	check(canaries_back(canaries, out, "Mainloop"), "Mainloop gives xmm6-xmm15 back as they were");
	check_walk("Mainloop");
}

int main(int argc, char** argv)
{
	sh4_env_init();
	_vmem_map_handler(_vmem_register_handler(&ram_handler_ctx, read8, read16, read32, write8, write16, write32), 0x00, 0xFF);

	//as the emulator starts up (LoadSettings), as tests/sh4_dynarec does
	settings.dynarec.Enable = true;
	settings.dynarec.idleskip = false;
	settings.dynarec.unstable_opt = false;
	settings.dynarec.safemode = true;
	settings.dynarec.SmcCheckLevel = NoCheck;
	settings.dynarec.disable_nvmem = true;

	recompiler = Get_Sh4Recompiler();
	verify(recompiler->Init());
	sh4_dyna_rcb = (u8*)&Sh4cntx + sizeof(Sh4cntx);

	//the thunk goes into the code cache, below what is recycled
	CanaryThunk entry(emit_GetCCPtr());
	thunk = (ThunkFP*)CC_RW2RX(entry.getCode());
	emit_Skip((u32)entry.getSize());
	emit_SetBaseAddr();

	test_runblock();
	test_mainloop();

	printf("windows.abi.xmm: %s\n", failures ? "FAILED" : "passed");
	return failures ? 1 : 0;
}
