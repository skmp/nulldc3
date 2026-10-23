/*
	This file is part of libswirl
        original code by flyinghead
*/
#include "license/bsd"



#include "types.h"


#if FEAT_SHREC == DYNAREC_JIT

#include <unistd.h>
#include <sys/mman.h>
#include <map>
#include "hw/sh4/sh4_mem.h"

#include "deps/vixl/aarch64/macro-assembler-aarch64.h"
using namespace vixl::aarch64;

//#define EXPLODE_SPANS

#include "hw/sh4/sh4_opcode_list.h"

#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_core.h"
#include "hw/sh4/dyna/ngen.h"
#include "hw/sh4/sh4_rom.h"
#include "arm64_regalloc.h"

#undef do_sqw_nommu

extern "C" void ngen_blockcheckfail(u32 pc);
extern "C" void ngen_LinkBlock_Generic_stub();
extern "C" void ngen_LinkBlock_cond_Branch_stub();
extern "C" void ngen_LinkBlock_cond_Next_stub();
extern void vmem_platform_flush_cache(void *icache_start, void *icache_end, void *dcache_start, void *dcache_end);

// The names the assembly below gives C symbols, and the visibility of the stubs it defines. A C name is the symbol of
// the same name on ELF (Linux) and that name behind an underscore on Mach-O (macOS): the compiler's
// __USER_LABEL_PREFIX__, which is empty on ELF and _ on Mach-O. The stubs are global symbols of the program, hidden
// on ELF (.hidden), so that no shared object can take their names; Mach-O's assembly gets no directive for them.
#define A64_STR_(x) #x
#define A64_STR(x) A64_STR_(x)
#define A64_SYM(name) A64_STR(__USER_LABEL_PREFIX__) #name
#if defined(__ELF__)
#define A64_HIDDEN(name) ".hidden " A64_SYM(name) "\n\t"
#else
#define A64_HIDDEN(name) ""
#endif

// The code cache of macOS is MAP_JIT memory, which a thread writes only while it has lifted the write protection
// of its JIT pages (pthread_jit_write_protect_np(false)) and executes only after it put it back. Linux has no such
// call: the code cache is the static array SH4_TCB, writable and executable (vmem_platform_prepare_jit_block)
#if defined(__APPLE__)
#define A64_JIT_WRITE_PROTECT(on) pthread_jit_write_protect_np(on)
#else
#define A64_JIT_WRITE_PROTECT(on) ((void)0)
#endif

struct DynaRBI : RuntimeBlockInfo
{
	virtual u32 Relink() override;

	virtual void Relocate(void* dst) override {
		verify(false);
	}
};

double host_cpu_time;
u64 guest_cpu_cycles;

#ifdef PROFILING
#include <time.h>

static clock_t slice_start;
extern "C"
{
static __attribute((used)) void start_slice()
{
	slice_start = clock();
}
static __attribute((used)) void end_slice()
{
	host_cpu_time += (double)(clock() - slice_start) / CLOCKS_PER_SEC;
}
}
#endif

/*
	The slice counter: the cycles left until the next UpdateSystem. Inside a run of the generated code it is in w27;
	here it is kept when the main loop is left and entered again, so that leaving and entering it is not visible to
	the guest, and a machine reset sets it (ResetSlice). The generated code stores w27 here whenever code other than
	itself may read it: in front of the UpdateSystem_INTC of a slice end, in front of the handler of SLEEP (which calls
	UpdateSystem from inside its block), at the debugger's calls and at every exit of the main loop and of RunBlock;
	it loads it at every entry, and again behind the UpdateSystem_INTC of a slice end. No C code writes it while
	generated code runs: SetSliceCyclesLeft is called by the interpreter fallback at the entry of its loop or of a
	step only (hw/sh4/dyna/driver.cpp, fallback_Enter / fallback_Leave), ResetSlice with the cpu stopped.
*/
static int a64_slice = SH4_TIMESLICE;

// The stubs of the code generator (Arm64NGenBackend::Init), the first thing in the code cache: the main loop's poll and
// dispatch, its end of slice (entered by a branch from the prologue of a block whose test found the slice used up) and
// the dispatcher's entry for a pc that has no block. Their addresses, in the executable view of the cache
static void* a64_poll;
static void* a64_slice_end;
static void* a64_failed_to_find;

__asm__
(
		A64_HIDDEN(ngen_LinkBlock_cond_Branch_stub)
		".globl " A64_SYM(ngen_LinkBlock_cond_Branch_stub) "		\n\t"
	A64_SYM(ngen_LinkBlock_cond_Branch_stub) ":		\n\t"
		"mov w1, #1							\n\t"
		"b " A64_SYM(ngen_LinkBlock_Shared_stub) "		\n"

		A64_HIDDEN(ngen_LinkBlock_cond_Next_stub)
		".globl " A64_SYM(ngen_LinkBlock_cond_Next_stub) "	\n\t"
	A64_SYM(ngen_LinkBlock_cond_Next_stub) ":			\n\t"
		"mov w1, #0							\n\t"
		"b " A64_SYM(ngen_LinkBlock_Shared_stub) "		\n"

		A64_HIDDEN(ngen_LinkBlock_Generic_stub)
		".globl " A64_SYM(ngen_LinkBlock_Generic_stub) "	\n\t"
	A64_SYM(ngen_LinkBlock_Generic_stub) ":			\n\t"
		"mov w1, w29						\n\t"	// djump/pc -> in case we need it ..
		//"b ngen_LinkBlock_Shared_stub		\n"

		A64_HIDDEN(ngen_LinkBlock_Shared_stub)
		".globl " A64_SYM(ngen_LinkBlock_Shared_stub) "	\n\t"
	A64_SYM(ngen_LinkBlock_Shared_stub) ":			\n\t"
		"mov x0, lr							\n\t"
		"sub x0, x0, #4						\n\t"	// go before the call
		"bl " A64_SYM(rdv_LinkBlock) "					\n\t"   // returns an RX addr
		"br x0								\n"

		A64_HIDDEN(ngen_blockcheckfail)
		".globl " A64_SYM(ngen_blockcheckfail) "			\n\t"
	A64_SYM(ngen_blockcheckfail) ":					\n\t"
		"bl " A64_SYM(rdv_BlockCheckFail) "				\n\t"
		"br x0								\n"
);

// The run flag is an atomic of 32 bits (sh4_if.h): the ldr of the poll in the main loop is a relaxed load of it, the
// poll of the emulator thread
static_assert(sizeof(Sh4cntx.CpuRunning) == 4, "the main loop loads the run flag as 32 bits");

// The context is the last member of Sh4RCB, and the table of the dispatcher (fpcb) its first: the main loop gets the
// address behind the context (sh4_dyna_rcb) and reaches both from x28
static_assert(offsetof(Sh4RCB, cntx) + sizeof(Sh4Context) == sizeof(Sh4RCB), "the context ends Sh4RCB");
static_assert(offsetof(Sh4RCB, fpcb) == 0, "the dispatcher's table starts Sh4RCB");

class Arm64Assembler : public MacroAssembler
{
	typedef void (MacroAssembler::*Arm64Op_RRO)(const Register&, const Register&, const Operand&);
	typedef void (MacroAssembler::*Arm64Op_RROF)(const Register&, const Register&, const Operand&, enum FlagsUpdate);

public:
	// The code is written where it will run: capacity is what the place has room for
	Arm64Assembler(void *buffer, size_t capacity = 64 * 1024) : MacroAssembler((u8 *)buffer, capacity), regalloc(this)
	{
		call_regs.push_back(&w0);
		call_regs.push_back(&w1);
		call_regs.push_back(&w2);
		call_regs.push_back(&w3);
		call_regs.push_back(&w4);
		call_regs.push_back(&w5);
		call_regs.push_back(&w6);
		call_regs.push_back(&w7);

		call_regs64.push_back(&x0);
		call_regs64.push_back(&x1);
		call_regs64.push_back(&x2);
		call_regs64.push_back(&x3);
		call_regs64.push_back(&x4);
		call_regs64.push_back(&x5);
		call_regs64.push_back(&x6);
		call_regs64.push_back(&x7);

		call_fregs.push_back(&s0);
		call_fregs.push_back(&s1);
		call_fregs.push_back(&s2);
		call_fregs.push_back(&s3);
		call_fregs.push_back(&s4);
		call_fregs.push_back(&s5);
		call_fregs.push_back(&s6);
		call_fregs.push_back(&s7);
	}

	void ngen_BinaryOp_RRO(shil_opcode* op, Arm64Op_RRO arm_op, Arm64Op_RROF arm_op2)
	{
		Operand op3 = Operand(0);
		if (op->rs2.is_imm())
		{
			op3 = Operand(op->rs2._imm);
		}
		else if (op->rs2.is_r32i())
		{
			op3 = Operand(regalloc.MapRegister(op->rs2));
		}
		if (arm_op != NULL)
			((*this).*arm_op)(regalloc.MapRegister(op->rd), regalloc.MapRegister(op->rs1), op3);
		else
			((*this).*arm_op2)(regalloc.MapRegister(op->rd), regalloc.MapRegister(op->rs1), op3, LeaveFlags);
	}

	const Register& GenMemAddr(const shil_opcode& op, const Register* raddr = NULL)
	{
		const Register* ret_reg = raddr == NULL ? &w0 : raddr;

		if (op.rs3.is_imm())
		{
			Add(*ret_reg, regalloc.MapRegister(op.rs1), op.rs3._imm);
		}
		else if (op.rs3.is_r32i())
		{
			Add(*ret_reg, regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs3));
		}
		else if (!op.rs3.is_null())
		{
			die("invalid rs3");
		}
		else
		{
			if (raddr == NULL)
				ret_reg = &regalloc.MapRegister(op.rs1);
			else
				Mov(*ret_reg, regalloc.MapRegister(op.rs1));
		}

		return *ret_reg;
	}

	// w27 to the slice counter in memory (a64_slice), and back
	void GenStoreSlice()
	{
		Mov(x9, reinterpret_cast<uintptr_t>(&a64_slice));
		Str(w27, MemOperand(x9));
	}

	void GenLoadSlice()
	{
		Mov(x9, reinterpret_cast<uintptr_t>(&a64_slice));
		Ldr(w27, MemOperand(x9));
	}

	// w29 = pc in two instructions whatever its value: block ends behind relink_offset have one size, whatever
	// DynaRBI::Relink() writes there later
	void GenSetNextPc(u32 pc)
	{
		vixl::ExactAssemblyScope scope(this, 2 * kInstructionSize);
		movz(w29, pc & 0xFFFF, 0);
		movk(w29, pc >> 16, 16);
	}

	/*
		The stubs of the code generator, written at the start of the code cache by Init: the main loop (Mainloop),
		its poll and dispatch, its end of slice and its exit, and the dispatcher's entry for a pc that has no block.

		Mainloop has a frame of its own: the registers a caller expects back (x19-x30 and d8-d15, the low 64 bits
		of v8-v15 of the AAPCS64; the blocks keep guest registers in w19-w26 and s8-s15, and a write of sN clears the
		rest of the register). Between two blocks x27 is the slice counter, x28 the context and w29 the pc the guest
		arrives at; the blocks run on the frame's sp, without a frame of their own, and reach the stubs by branches.

		  Mainloop   the frame; x28, w27 from a64_slice, w29 from the context; entered with a counter that is not
		             positive, the end of the slice first (a flow report of the debugger stopped behind the block
		             that used the slice up); then the poll
		  poll       CpuRunning clear: the exit. Otherwise the dispatch of w29: its entry in fpcb
		  slice end  entered from the prologue of a block whose test found the slice used up, before the block's
		             charge: w29 into the context, the counter refilled and stored, UpdateSystem_INTC, which can
		             accept an interrupt (the pc in the context) or latch a stop (CpuRunning); w29 and the counter
		             loaded again, the poll. Nothing is owed when the loop is left there
		  exit       the counter stored, the frame back, the return. It never writes the pc: Sh4cntx.pc is complete
		             wherever the loop is left (the poll's w29 came from the context)
		  failed     the pc has no block: rdv_FailedToFindBlock(w29) compiles it, and the code is entered
	*/
	void GenLoopCode(MainloopFnPtr_t* mainloop)
	{
		const int frame = 160;
		const ptrdiff_t pc_offset = offsetof(Sh4Context, pc);
		Label poll, slice_end, exit;

		// Mainloop(x0 = the address behind the context, sh4_dyna_rcb)
		*mainloop = (MainloopFnPtr_t)CC_RW2RX(GetCursorAddress<void*>());

		Stp(x29, x30, MemOperand(sp, -frame, PreIndex));
		Stp(x19, x20, MemOperand(sp, 16));
		Stp(x21, x22, MemOperand(sp, 32));
		Stp(x23, x24, MemOperand(sp, 48));
		Stp(x25, x26, MemOperand(sp, 64));
		Stp(x27, x28, MemOperand(sp, 80));
		Stp(d8, d9, MemOperand(sp, 96));
		Stp(d10, d11, MemOperand(sp, 112));
		Stp(d12, d13, MemOperand(sp, 128));
		Stp(d14, d15, MemOperand(sp, 144));

		Sub(x28, x0, sizeof(Sh4Context));
		GenLoadSlice();
		Ldr(w29, MemOperand(x28, pc_offset));
		Cmp(w27, 0);
		B(&slice_end, le);

		// the poll and the dispatch: next_pc _MUST_ be on w29
		Bind(&poll);
		a64_poll = CC_RW2RX(GetLabelAddress<void*>(&poll));
		Ldr(w0, MemOperand(x28, offsetof(Sh4Context, CpuRunning)));
		Cbz(w0, &exit);
		Sub(x2, x28, offsetof(Sh4RCB, cntx));
#if RAM_SIZE_MAX == 33554432
		Ubfx(w1, w29, 1, 24);	// 24+1 bits: 32 MB
#elif RAM_SIZE_MAX == 16777216
		Ubfx(w1, w29, 1, 23);	// 23+1 bits: 16 MB
#else
#error "Define RAM_SIZE_MAX"
#endif
		Ldr(x0, MemOperand(x2, x1, LSL, 3));
		Br(x0);

		// the end of the slice
		Bind(&slice_end);
		a64_slice_end = CC_RW2RX(GetLabelAddress<void*>(&slice_end));
		Add(w27, w27, SH4_TIMESLICE);
		Str(w29, MemOperand(x28, pc_offset));
		GenStoreSlice();
		GenCallRuntime(UpdateSystem_INTC);
		Ldr(w29, MemOperand(x28, pc_offset));
		GenLoadSlice();
		B(&poll);

		// the exit
		Bind(&exit);
		GenStoreSlice();
		Ldp(d14, d15, MemOperand(sp, 144));
		Ldp(d12, d13, MemOperand(sp, 128));
		Ldp(d10, d11, MemOperand(sp, 112));
		Ldp(d8, d9, MemOperand(sp, 96));
		Ldp(x27, x28, MemOperand(sp, 80));
		Ldp(x25, x26, MemOperand(sp, 64));
		Ldp(x23, x24, MemOperand(sp, 48));
		Ldp(x21, x22, MemOperand(sp, 32));
		Ldp(x19, x20, MemOperand(sp, 16));
		Ldp(x29, x30, MemOperand(sp, frame, PostIndex));
		Ret();

		// the dispatcher's entry of a pc without a block (fpcb, bm_GetCode): reached by the dispatch, by the
		// dynamic ends of the blocks and by rdv_LinkBlock's stubs, with the pc in w29. In the cache, so that no
		// indirect branch of the generated code has its target in .text (BTI)
		a64_failed_to_find = CC_RW2RX(GetCursorAddress<void*>());
		Mov(w0, w29);
		GenCallRuntime(rdv_FailedToFindBlock);
		Br(x0);
	}

	void ngen_Compile(RuntimeBlockInfo* block, SmcCheckEnum smc_checks, bool reset, bool staging, bool optimise)
	{
		A64_JIT_WRITE_PROTECT(false);
		//printf("REC-ARM64 compiling %08x\n", block->addr);
#ifdef PROFILING
		SaveFramePointer();
#endif
		this->block = block;
		
		CheckBlock(smc_checks, block);

		// run register allocator
		regalloc.DoAlloc(block);

		// The slice is tested in front of the charge, as the main loop of the x64 backend and the interpreter fallback
		// test it behind the block before (hw/sh4/sh4_dbg.h): a block is entered with a positive counter, or the slice
		// ends here, in front of it, and the block is dispatched again behind the end-of-slice work. w29 is the pc the
		// guest arrived at, which the end of the slice stores. A block costs at most SH4_TIMESLICE / 2, so the counter
		// is positive again behind it
		verify(block->guest_cycles <= SH4_TIMESLICE / 2);
		Cmp(w27, 0);
		Label slice_left;
		B(&slice_left, gt);
		GenBranchRuntime((void (*)())a64_slice_end);
		Bind(&slice_left);
		Sub(w27, w27, block->guest_cycles);

#ifdef PROFILING
		Ldr(x11, (uintptr_t)&guest_cpu_cycles);
		Ldr(x0, MemOperand(x11));
		Add(x0, x0, block->guest_cycles);
		Str(x0, MemOperand(x11));
#endif
		for (size_t i = 0; i < block->oplist.size(); i++)
		{
			shil_opcode& op  = block->oplist[i];
			regalloc.OpBegin(&op, i);

			switch (op.op)
			{
			case shop_ifb:	// Interpreter fallback
				if (op.rs1._imm)	// if NeedPC()
				{
					Mov(w10, op.rs2._imm);
					Str(w10, sh4_context_mem_operand(&next_pc));
				}
				// SLEEP calls UpdateSystem from inside the block, where the slice counter is read (SH4 time): it is
				// stored for it
				if (op.rs3._imm == 0x001B)
					GenStoreSlice();
				Mov(*call_regs[0], op.rs3._imm);

				GenCallRuntime(OpDesc[op.rs3._imm]->oph);
				break;

			case shop_jcond:
			case shop_jdyn:
				if (op.rs2.is_imm())
					Add(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), op.rs2._imm);
				else
					Mov(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1));
				// Save it for the branching at the end of the block
				Mov(w29, regalloc.MapRegister(op.rd));
				break;

			case shop_mov32:
				verify(op.rd.is_reg());
				verify(op.rs1.is_reg() || op.rs1.is_imm());

				if (regalloc.IsAllocf(op.rd))
				{
					if (op.rs1.is_imm())
						Fmov(regalloc.MapVRegister(op.rd), (float&)op.rs1._imm);
					else if (regalloc.IsAllocf(op.rs1))
						Fmov(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1));
					else
						Fmov(regalloc.MapVRegister(op.rd), regalloc.MapRegister(op.rs1));
				}
				else
				{
					if (op.rs1.is_imm())
						Mov(regalloc.MapRegister(op.rd), op.rs1._imm);
					else if (regalloc.IsAllocg(op.rs1))
						Mov(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1));
					else
						Fmov(regalloc.MapRegister(op.rd), regalloc.MapVRegister(op.rs1));
				}
				break;

			case shop_mov64:
				verify(op.rd.is_reg());
				verify(op.rs1.is_reg() || op.rs1.is_imm());

#ifdef EXPLODE_SPANS
				Fmov(regalloc.MapVRegister(op.rd, 0), regalloc.MapVRegister(op.rs1, 0));
				Fmov(regalloc.MapVRegister(op.rd, 1), regalloc.MapVRegister(op.rs1, 1));
#else
				shil_param_to_host_reg(op.rs1, x15);
				host_reg_to_shil_param(op.rd, x15);
#endif
				break;

			case shop_readm:
				GenReadMemory(op, i);
				break;

			case shop_writem:
				GenWriteMemory(op, i);
				break;
#define CANONICAL_FALLBACK 0

#if CANONICAL_FALLBACK == 0
			case shop_sync_sr:
				GenCallRuntime(UpdateSR);
				break;
			case shop_sync_fpscr:
				GenCallRuntime(UpdateFPSCR);
				break;

			case shop_swaplb:
				Mov(w9, Operand(regalloc.MapRegister(op.rs1), LSR, 16));
				Rev16(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1));
				Bfi(regalloc.MapRegister(op.rd), w9, 16, 16);
				break;

			case shop_neg:
				Neg(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1));
				break;
			case shop_not:
				Mvn(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1));
				break;

			case shop_and:
				ngen_BinaryOp_RRO(&op, &MacroAssembler::And, NULL);
				break;
			case shop_or:
				ngen_BinaryOp_RRO(&op, &MacroAssembler::Orr, NULL);
				break;
			case shop_xor:
				ngen_BinaryOp_RRO(&op, &MacroAssembler::Eor, NULL);
				break;
			case shop_add:
				ngen_BinaryOp_RRO(&op, NULL, &MacroAssembler::Add);
				break;
			case shop_sub:
				ngen_BinaryOp_RRO(&op, NULL, &MacroAssembler::Sub);
				break;
			case shop_shl:
				if (op.rs2.is_imm())
					Lsl(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), op.rs2._imm);
				else if (op.rs2.is_reg())
					Lsl(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
				break;
			case shop_shr:
				if (op.rs2.is_imm())
					Lsr(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), op.rs2._imm);
				else if (op.rs2.is_reg())
					Lsr(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
				break;
			case shop_sar:
				if (op.rs2.is_imm())
					Asr(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), op.rs2._imm);
				else if (op.rs2.is_reg())
					Asr(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
				break;
			case shop_ror:
				if (op.rs2.is_imm())
					Ror(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), op.rs2._imm);
				else if (op.rs2.is_reg())
					Ror(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
				break;

			case shop_adc:
				Cmp(regalloc.MapRegister(op.rs3), 1);	// C = rs3
				Adcs(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2)); // (C,rd)=rs1+rs2+rs3(C)
				Cset(regalloc.MapRegister(op.rd2), cs);	// rd2 = C
				break;
			case shop_sbc:
				Cmp(wzr, regalloc.MapRegister(op.rs3));	// C = ~rs3
				Sbcs(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2)); // (C,rd) = rs1 - rs2 - ~rs3(C)
				Cset(regalloc.MapRegister(op.rd2), cc);	// rd2 = ~C
				break;

			case shop_rocr:
				Ubfx(w0, regalloc.MapRegister(op.rs1), 0, 1);										// w0 = rs1[0] (new C)
				Mov(regalloc.MapRegister(op.rd), Operand(regalloc.MapRegister(op.rs1), LSR, 1));	// rd = rs1 >> 1
				Bfi(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs2), 31, 1);				// rd |= C << 31
				Mov(regalloc.MapRegister(op.rd2), w0);												// rd2 = w0 (new C)
				break;
			case shop_rocl:
				Tst(regalloc.MapRegister(op.rs1), 0x80000000);	// Z = ~rs1[31]
				Orr(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs2), Operand(regalloc.MapRegister(op.rs1), LSL, 1)); // rd = rs1 << 1 | rs2(C)
				Cset(regalloc.MapRegister(op.rd2), ne);			// rd2 = ~Z(C)
				break;

			case shop_shld:
			case shop_shad:
				// TODO optimize
				Cmp(regalloc.MapRegister(op.rs2), 0);
				Csel(w1, regalloc.MapRegister(op.rs2), wzr, ge);	// if shift >= 0 then w1 = shift else w1 = 0
				Mov(w0, wzr);	// wzr not supported by csneg
				Csneg(w2, w0, regalloc.MapRegister(op.rs2), ge);	// if shift < 0 then w2 = -shift else w2 = 0
				Cmp(w2, 32);
				Csel(w2, 31, w2, eq);								// if shift == -32 then w2 = 31
				Lsl(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), w1);		// Left shift by w1
				if (op.op == shop_shld)													// Right shift by w2
					// Logical shift
					Lsr(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rd), w2);
				else
					// Arithmetic shift
					Asr(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rd), w2);
				break;

			case shop_test:
			case shop_seteq:
			case shop_setge:
			case shop_setgt:
			case shop_setae:
			case shop_setab:
				{
					if (op.op == shop_test)
					{
						if (op.rs2.is_imm())
							Tst(regalloc.MapRegister(op.rs1), op.rs2._imm);
						else
							Tst(regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
					}
					else
					{
						if (op.rs2.is_imm())
							Cmp(regalloc.MapRegister(op.rs1), op.rs2._imm);
						else
							Cmp(regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
					}

					static const Condition shop_conditions[] = { eq, eq, ge, gt, hs, hi };

					Cset(regalloc.MapRegister(op.rd), shop_conditions[op.op - shop_test]);
				}
				break;
			case shop_setpeq:
				Eor(w1, regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));

				Mov(regalloc.MapRegister(op.rd), wzr);
				Mov(w2, wzr);	// wzr not supported by csinc (?!)
				Tst(w1, 0xFF000000);
				Csinc(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rd), w2, ne);
				Tst(w1, 0x00FF0000);
				Csinc(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rd), w2, ne);
				Tst(w1, 0x0000FF00);
				Csinc(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rd), w2, ne);
				Tst(w1, 0x000000FF);
				Csinc(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rd), w2, ne);
				break;

			case shop_mul_u16:
				Uxth(w10, regalloc.MapRegister(op.rs1));
				Uxth(w11, regalloc.MapRegister(op.rs2));
				Mul(regalloc.MapRegister(op.rd), w10, w11);
				break;
			case shop_mul_s16:
				Sxth(w10, regalloc.MapRegister(op.rs1));
				Sxth(w11, regalloc.MapRegister(op.rs2));
				Mul(regalloc.MapRegister(op.rd), w10, w11);
				break;
			case shop_mul_i32:
				Mul(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
				break;
			case shop_mul_u64:
			case shop_mul_s64:
				{
					const Register& rd_xreg = Register::GetXRegFromCode(regalloc.MapRegister(op.rd).GetCode());
					if (op.op == shop_mul_u64)
						Umull(rd_xreg, regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
					else
						Smull(rd_xreg, regalloc.MapRegister(op.rs1), regalloc.MapRegister(op.rs2));
					const Register& rd2_xreg = Register::GetXRegFromCode(regalloc.MapRegister(op.rd2).GetCode());
					Lsr(rd2_xreg, rd_xreg, 32);
				}
				break;

			case shop_pref:
				{
					Lsr(w1, regalloc.MapRegister(op.rs1), 26);
					Cmp(w1, 0x38);
					Label not_sqw;
					B(&not_sqw, ne);
					Mov(w0, regalloc.MapRegister(op.rs1));

					auto sh4mmr = sh4_cpu->sh4mmr.get();
					
					if (CCN_MMUCR.AT)
					{
						Ldr(x9, reinterpret_cast<uintptr_t>(&do_sqw_mmu));
					}
					else
					{
						Sub(x9, x28, offsetof(Sh4RCB, cntx) - offsetof(Sh4RCB, do_sqw_nommu));
						Ldr(x9, MemOperand(x9));
						Sub(x1, x28, offsetof(Sh4RCB, cntx) - offsetof(Sh4RCB, sq_buffer));
					}
					Blr(x9);
					Bind(&not_sqw);
				}
				break;

			case shop_ext_s8:
				Sxtb(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1));
				break;
			case shop_ext_s16:
				Sxth(regalloc.MapRegister(op.rd), regalloc.MapRegister(op.rs1));
				break;

			//
			// FPU
			//

			case shop_fadd:
				Fadd(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1), regalloc.MapVRegister(op.rs2));
				break;
			case shop_fsub:
				Fsub(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1), regalloc.MapVRegister(op.rs2));
				break;
			case shop_fmul:
				Fmul(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1), regalloc.MapVRegister(op.rs2));
				break;
			case shop_fdiv:
				Fdiv(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1), regalloc.MapVRegister(op.rs2));
				break;

			case shop_fabs:
				Fabs(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1));
				break;
			case shop_fneg:
				Fneg(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1));
				break;
			case shop_fsqrt:
				Fsqrt(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs1));
				break;

			case shop_fmac:
				Fmadd(regalloc.MapVRegister(op.rd), regalloc.MapVRegister(op.rs3), regalloc.MapVRegister(op.rs2), regalloc.MapVRegister(op.rs1));
				break;

			case shop_fsrra:
				Fsqrt(s0, regalloc.MapVRegister(op.rs1));
				Fmov(s1, 1.f);
				Fdiv(regalloc.MapVRegister(op.rd), s1, s0);
				break;

			case shop_fsetgt:
			case shop_fseteq:
				Fcmp(regalloc.MapVRegister(op.rs1), regalloc.MapVRegister(op.rs2));
				Cset(regalloc.MapRegister(op.rd), op.op == shop_fsetgt ? gt : eq);
				break;

			case shop_fsca:
				Mov(x1, reinterpret_cast<uintptr_t>(&sin_table));
				Add(x1, x1, Operand(regalloc.MapRegister(op.rs1), UXTH, 3));
#ifdef EXPLODE_SPANS
				Ldr(regalloc.MapVRegister(op.rd, 0), MemOperand(x1, 4, PostIndex));
				Ldr(regalloc.MapVRegister(op.rd, 1), MemOperand(x1));
#else
				Ldr(x2, MemOperand(x1));
				Str(x2, sh4_context_mem_operand(op.rd.reg_ptr()));
#endif
				break;

			case shop_fipr:
				Add(x9, x28, sh4_context_mem_operand(op.rs1.reg_ptr()).GetOffset());
				Ld1(v0.V4S(), MemOperand(x9));
				if (op.rs1._reg != op.rs2._reg)
				{
					Add(x9, x28, sh4_context_mem_operand(op.rs2.reg_ptr()).GetOffset());
					Ld1(v1.V4S(), MemOperand(x9));
					Fmul(v0.V4S(), v0.V4S(), v1.V4S());
				}
				else
					Fmul(v0.V4S(), v0.V4S(), v0.V4S());
				Faddp(v1.V4S(), v0.V4S(), v0.V4S());
				Faddp(regalloc.MapVRegister(op.rd), v1.V2S());
				break;

			case shop_ftrv:
				Add(x9, x28, sh4_context_mem_operand(op.rs1.reg_ptr()).GetOffset());
				Ld1(v0.V4S(), MemOperand(x9));
				Add(x9, x28, sh4_context_mem_operand(op.rs2.reg_ptr()).GetOffset());
				Ld1(v1.V4S(), MemOperand(x9, 16, PostIndex));
				Ld1(v2.V4S(), MemOperand(x9, 16, PostIndex));
				Ld1(v3.V4S(), MemOperand(x9, 16, PostIndex));
				Ld1(v4.V4S(), MemOperand(x9, 16, PostIndex));
				Fmul(v5.V4S(), v1.V4S(), s0, 0);
				Fmla(v5.V4S(), v2.V4S(), s0, 1);
				Fmla(v5.V4S(), v3.V4S(), s0, 2);
				Fmla(v5.V4S(), v4.V4S(), s0, 3);
				Add(x9, x28, sh4_context_mem_operand(op.rd.reg_ptr()).GetOffset());
				St1(v5.V4S(), MemOperand(x9));
				break;

			case shop_frswap:
				Add(x9, x28, sh4_context_mem_operand(op.rs1.reg_ptr()).GetOffset());
				Add(x10, x28, sh4_context_mem_operand(op.rd.reg_ptr()).GetOffset());
				Ld4(v0.V2D(), v1.V2D(), v2.V2D(), v3.V2D(), MemOperand(x9));
				Ld4(v4.V2D(), v5.V2D(), v6.V2D(), v7.V2D(), MemOperand(x10));
				St4(v4.V2D(), v5.V2D(), v6.V2D(), v7.V2D(), MemOperand(x9));
				St4(v0.V2D(), v1.V2D(), v2.V2D(), v3.V2D(), MemOperand(x10));
				break;

			case shop_cvt_f2i_t:
				Fcvtzs(regalloc.MapRegister(op.rd), regalloc.MapVRegister(op.rs1));
				break;
			case shop_cvt_i2f_n:
			case shop_cvt_i2f_z:
				Scvtf(regalloc.MapVRegister(op.rd), regalloc.MapRegister(op.rs1));
				break;
#endif
			default:
				shil_chf[op.op](&op);
				break;
			}
			regalloc.OpEnd(&op);
		}

		block->relink_offset = (u32)GetBuffer()->GetCursorOffset();
		block->relink_data = 0;

		RelinkBlock(block);

		Finalize();
		A64_JIT_WRITE_PROTECT(true);
	}

	void ngen_CC_Start(shil_opcode* op)
	{
		CC_pars.clear();
	}

	void ngen_CC_Param(shil_opcode& op, shil_param& prm, CanonicalParamType tp)
	{
		switch (tp)
		{

		case CPT_u32:
		case CPT_ptr:
		case CPT_f32:
		{
			CC_PS t = { tp, &prm };
			CC_pars.push_back(t);
		}
		break;

		case CPT_u64rvL:
		case CPT_u32rv:
			host_reg_to_shil_param(prm, w0);
			break;

		case CPT_u64rvH:
			Lsr(x10, x0, 32);
			host_reg_to_shil_param(prm, w10);
			break;

		case CPT_f32rv:
			host_reg_to_shil_param(prm, s0);
			break;
		}
	}

	void ngen_CC_Call(shil_opcode*op, void* function)
	{
		int regused = 0;
		int fregused = 0;

		// Args are pushed in reverse order by shil_canonical
		for (int i = CC_pars.size(); i-- > 0;)
		{
			verify(fregused < call_fregs.size() && regused < call_regs.size());
			shil_param& prm = *CC_pars[i].prm;
			switch (CC_pars[i].type)
			{
			// push the params

			case CPT_u32:
				shil_param_to_host_reg(prm, *call_regs[regused++]);

				break;

			case CPT_f32:
				if (prm.is_reg()) {
					Fmov(*call_fregs[fregused], regalloc.MapVRegister(prm));
				}
				else {
					verify(prm.is_null());
				}
				fregused++;
				break;

			case CPT_ptr:
				verify(prm.is_reg());
				// push the ptr itself
				Mov(*call_regs64[regused++], reinterpret_cast<uintptr_t>(prm.reg_ptr()));

				break;
			case CPT_u32rv:
			case CPT_u64rvL:
			case CPT_u64rvH:
			case CPT_f32rv:
				// return values are handled in ngen_CC_param()
				break;
			}
		}
		GenCallRuntime((void (*)())function);
	}

	MemOperand sh4_context_mem_operand(void *p)
	{
		u32 offset = (u8*)p - (u8*)&p_sh4rcb->cntx;
		verify((offset & 3) == 0 && offset <= 16380);	// FIXME 64-bit regs need multiple of 8 up to 32760
		return MemOperand(x28, offset);
	}

	void GenReadMemorySlow(const shil_opcode& op)
	{
		Instruction *start_instruction = GetCursorAddress<Instruction *>();
		u32 size = op.flags & 0x7f;

		switch (size)
		{
		case 1:
			GenCallRuntime(ReadMem8);
			Sxtb(w0, w0);
			break;

		case 2:
			GenCallRuntime(ReadMem16);
			Sxth(w0, w0);
			break;

		case 4:
			GenCallRuntime(ReadMem32);
			break;

		case 8:
			GenCallRuntime(ReadMem64);
			break;

		default:
			die("1..8 bytes");
			break;
		}

		if (size != 8)
			host_reg_to_shil_param(op.rd, w0);
		else
		{
#ifdef EXPLODE_SPANS
			verify(op.rd.count() == 2 && regalloc.IsAllocf(op.rd, 0) && regalloc.IsAllocf(op.rd, 1));
			Fmov(regalloc.MapVRegister(op.rd, 0), w0);
			Lsr(x0, x0, 32);
			Fmov(regalloc.MapVRegister(op.rd, 1), w0);
#else
			host_reg_to_shil_param(op.rd, x0);
#endif
		}
		EnsureCodeSize(start_instruction, read_memory_rewrite_size);
	}

	void GenWriteMemorySlow(const shil_opcode& op)
	{
		Instruction *start_instruction = GetCursorAddress<Instruction *>();
		u32 size = op.flags & 0x7f;
		switch (size)
		{
		case 1:
			GenCallRuntime(WriteMem8);
			break;

		case 2:
			GenCallRuntime(WriteMem16);
			break;

		case 4:
			GenCallRuntime(WriteMem32);
			break;

		case 8:
			GenCallRuntime(WriteMem64);
			break;

		default:
			die("1..8 bytes");
			break;
		}
		EnsureCodeSize(start_instruction, write_memory_rewrite_size);
	}

	void InitializeRewrite(RuntimeBlockInfo *block, size_t opid)
	{
		regalloc.DoAlloc(block);
		regalloc.current_opid = opid;
	}

	u32 RelinkBlock(RuntimeBlockInfo *block)
	{
		ptrdiff_t start_offset = GetBuffer()->GetCursorOffset();

		switch (block->BlockType)
		{

		case BET_StaticJump:
		case BET_StaticCall:
			// next_pc = block->BranchBlock, in w29: the pc the guest arrives at, which a slice end in the prologue of
			// the next block stores
			GenSetNextPc(block->BranchBlock);
			if (block->pBranchBlock == NULL)
				GenCallRuntime(ngen_LinkBlock_Generic_stub);
			else
				GenBranch(block->pBranchBlock->code);
			break;

		case BET_Cond_0:
		case BET_Cond_1:
			{
				// next_pc = next_pc_value;
				// if (*jdyn == 0)
				//   next_pc = branch_pc_value;

				if (block->has_jcond)
					Ldr(w11, sh4_context_mem_operand(&Sh4cntx.jdyn));
				else
					Ldr(w11, sh4_context_mem_operand(&sr.T));

				Cmp(w11, block->BlockType & 1);

				Label branch_not_taken;

				B(ne, &branch_not_taken);
				GenSetNextPc(block->BranchBlock);
				if (block->pBranchBlock != NULL)
					GenBranch(block->pBranchBlock->code);
				else
					GenCallRuntime(ngen_LinkBlock_cond_Branch_stub);

				Bind(&branch_not_taken);

				GenSetNextPc(block->NextBlock);
				if (block->pNextBlock != NULL)
					GenBranch(block->pNextBlock->code);
				else
					GenCallRuntime(ngen_LinkBlock_cond_Next_stub);
			}
			break;

		case BET_DynamicJump:
		case BET_DynamicCall:
		case BET_DynamicRet:
			// next_pc = *jdyn;

			Str(w29, sh4_context_mem_operand(&next_pc));
			// TODO Branch to the poll of the main loop instead (and check CpuRunning less frequently?)
			Mov(x2, sizeof(Sh4RCB));
			Sub(x2, x28, x2);
			Add(x2, x2, sizeof(Sh4Context));		// x2 now points to FPCB
#if RAM_SIZE_MAX == 33554432
			Ubfx(w1, w29, 1, 24);
#else
			Ubfx(w1, w29, 1, 23);
#endif
			Ldr(x15, MemOperand(x2, x1, LSL, 3));	// Get block entry point
			Br(x15);

			break;

		case BET_DynamicIntr:
		case BET_StaticIntr:
			if (block->BlockType == BET_StaticIntr)
				// next_pc = next_pc_value;
				Mov(w29, block->NextBlock);
			// else next_pc = *jdyn (already in w29)

			Str(w29, sh4_context_mem_operand(&next_pc));

			GenCallRuntime(UpdateINTC);

			Ldr(w29, sh4_context_mem_operand(&next_pc));

			GenBranchRuntime((void (*)())a64_poll);
			break;

		default:
			die("Invalid block end type");
		}

		return GetBuffer()->GetCursorOffset() - start_offset;
	}

	void Finalize(bool rewrite = false)
	{
		Label code_end;
		Bind(&code_end);

		FinalizeCode();

		if (!rewrite)
		{
			block->code = GetBuffer()->GetStartAddress<DynarecCodeEntryPtr>();
			block->host_code_size = GetBuffer()->GetSizeInBytes();
			block->host_opcodes = GetLabelAddress<u32*>(&code_end) - GetBuffer()->GetStartAddress<u32*>();

			emit_Skip(block->host_code_size);
		}

		// Flush and invalidate caches
		vmem_platform_flush_cache(
			CC_RW2RX(GetBuffer()->GetStartAddress<void*>()), CC_RW2RX(GetBuffer()->GetEndAddress<void*>()),
			GetBuffer()->GetStartAddress<void*>(), GetBuffer()->GetEndAddress<void*>());

#if 0
//		if (rewrite)
		{
			Instruction* instr_start = (Instruction*)block->code;
//			Instruction* instr_end = GetLabelAddress<Instruction*>(&code_end);
			Instruction* instr_end = (Instruction*)((u8 *)block->code + block->host_code_size);
			Decoder decoder;
			Disassembler disasm;
			decoder.AppendVisitor(&disasm);
			Instruction* instr;
			for (instr = instr_start; instr < instr_end; instr += kInstructionSize) {
				decoder.Decode(instr);
				printf("VIXL\t %p:\t%s\n",
						   reinterpret_cast<void*>(instr),
						   disasm.GetOutput());
			}
		}
#endif
	}

private:
	// Runtime branches/calls need to be adjusted if rx space is different to rw space.
	// Therefore can't mix GenBranch with GenBranchRuntime!

	template <typename R, typename... P>
	void GenCallRuntime(R (*function)(P...))
	{
		ptrdiff_t offset = reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(CC_RW2RX(GetBuffer()->GetStartAddress<void*>()));
		verify(offset >= -128 * 1024 * 1024 && offset <= 128 * 1024 * 1024);
		verify((offset & 3) == 0);
		Label function_label;
		BindToOffset(&function_label, offset);
		Bl(&function_label);
	}

   template <typename R, typename... P>
	void GenBranchRuntime(R (*target)(P...))
	{
		ptrdiff_t offset = reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(CC_RW2RX(GetBuffer()->GetStartAddress<void*>()));
		verify(offset >= -128 * 1024 * 1024 && offset <= 128 * 1024 * 1024);
		verify((offset & 3) == 0);
		Label target_label;
		BindToOffset(&target_label, offset);
		B(&target_label);
	}

	template <typename R, typename... P>
	void GenBranch(R (*code)(P...), Condition cond = al)
	{
		ptrdiff_t offset = reinterpret_cast<uintptr_t>(code) - GetBuffer()->GetStartAddress<uintptr_t>();
		verify(offset >= -128 * 1024 * 1024 && offset < 128 * 1024 * 1024);
		verify((offset & 3) == 0);
		Label code_label;
		BindToOffset(&code_label, offset);
		if (cond == al)
			B(&code_label);
		else
			B(&code_label, cond);
	}

	void GenReadMemory(const shil_opcode& op, size_t opid)
	{
		u32 size = op.flags & 0x7f;

		if (GenReadMemoryImmediate(op))
			return;

		GenMemAddr(op, call_regs[0]);

		if (GenReadMemoryFast(op, opid))
			return;

		GenReadMemorySlow(op);
	}

	bool GenReadMemoryImmediate(const shil_opcode& op)
	{
		if (!op.rs1.is_imm())
			return false;

		u32 size = op.flags & 0x7f;
		bool isram = false;
		void* ctx;
		void* ptr = _vmem_read_const(op.rs1._imm, isram, size, ctx);

		if (isram)
		{
			Ldr(x1, reinterpret_cast<uintptr_t>(ptr));
			switch (size)
			{
			case 2:
				Ldrsh(regalloc.MapRegister(op.rd), MemOperand(x1, xzr, SXTW));
				break;

			case 4:
				if (op.rd.is_r32f())
					Ldr(regalloc.MapVRegister(op.rd), MemOperand(x1));
				else
					Ldr(regalloc.MapRegister(op.rd), MemOperand(x1));
				break;

			default:
				die("Invalid size");
				break;
			}
		}
		else
		{
			// Not RAM
			Mov(x0, (uintptr_t)ctx);
			Mov(w1, op.rs1._imm);

			switch(size)
			{
			case 1:
				GenCallRuntime((void (*)())ptr);
				Sxtb(w0, w0);
				break;

			case 2:
				GenCallRuntime((void (*)())ptr);
				Sxth(w0, w0);
				break;

			case 4:
				GenCallRuntime((void (*)())ptr);
				break;

			case 8:
				die("SZ_64F not supported");
				break;
			}

			if (regalloc.IsAllocg(op.rd))
				Mov(regalloc.MapRegister(op.rd), w0);
			else
				Fmov(regalloc.MapVRegister(op.rd), w0);
		}

		return true;
	}

	bool GenReadMemoryFast(const shil_opcode& op, size_t opid)
	{
		// Direct memory access. Need to handle SIGSEGV and rewrite block as needed. See ngen_Rewrite()
		if (!_nvmem_enabled())
			return false;

		// WARNING: the rewrite code relies on having two ops before the memory access
		// Update ngen_Rewrite (and perhaps read_memory_rewrite_size) if adding or removing code
		Instruction *start_instruction = GetCursorAddress<Instruction *>();
		Add(w1, *call_regs[0], sizeof(Sh4Context), LeaveFlags);
		Bfc(w1, 29, 3);		// addr &= ~0xE0000000
		Instruction *mem_instruction = GetCursorAddress<Instruction *>();
		unsigned prologue_size = (char*)mem_instruction - (char*)start_instruction;

		u32 size = op.flags & 0x7f;
		switch(size)
		{
		case 1:
			Ldrsb(regalloc.MapRegister(op.rd), MemOperand(x28, x1, SXTW));
			break;

		case 2:
			Ldrsh(regalloc.MapRegister(op.rd), MemOperand(x28, x1, SXTW));
			break;

		case 4:
			if (!op.rd.is_r32f())
				Ldr(regalloc.MapRegister(op.rd), MemOperand(x28, x1));
			else
				Ldr(regalloc.MapVRegister(op.rd), MemOperand(x28, x1));
			break;

		case 8:
			Ldr(x1, MemOperand(x28, x1));
			break;
		}

		if (size == 8)
		{
#ifdef EXPLODE_SPANS
			verify(op.rd.count() == 2 && regalloc.IsAllocf(op.rd, 0) && regalloc.IsAllocf(op.rd, 1));
			Fmov(regalloc.MapVRegister(op.rd, 0), w1);
			Lsr(x1, x1, 32);
			Fmov(regalloc.MapVRegister(op.rd, 1), w1);
#else
			Str(x1, sh4_context_mem_operand(op.rd.reg_ptr()));
#endif
		}
		unsigned code_size = (unsigned)(GetCursorAddress<char*>() - (char*)start_instruction);
		EnsureCodeSize(start_instruction, read_memory_rewrite_size);
		verify(code_size < 256 && prologue_size < 256);
		this->block->memory_accesses[mem_instruction] = { (uint16_t)opid, (uint8_t)prologue_size, (uint8_t)code_size };

		return true;
	}

	void GenWriteMemory(const shil_opcode& op, size_t opid)
	{
		GenMemAddr(op, call_regs[0]);

		u32 size = op.flags & 0x7f;
		if (size != 8)
			shil_param_to_host_reg(op.rs2, *call_regs[1]);
		else
		{
#ifdef EXPLODE_SPANS
			verify(op.rs2.count() == 2 && regalloc.IsAllocf(op.rs2, 0) && regalloc.IsAllocf(op.rs2, 1));
			Fmov(*call_regs[1], regalloc.MapVRegister(op.rs2, 1));
			Lsl(*call_regs64[1], *call_regs64[1], 32);
			Fmov(w2, regalloc.MapVRegister(op.rs2, 0));
			Orr(*call_regs64[1], *call_regs64[1], x2);
#else
			shil_param_to_host_reg(op.rs2, *call_regs64[1]);
#endif
		}
		if (GenWriteMemoryFast(op, opid))
			return;

		GenWriteMemorySlow(op);
	}

	bool GenWriteMemoryFast(const shil_opcode& op, size_t opid)
	{
		// Direct memory access. Need to handle SIGSEGV and rewrite block as needed. See ngen_Rewrite()
		if (!_nvmem_enabled())
			return false;

		// WARNING: the rewrite code relies on having two ops before the memory access
		// Update ngen_Rewrite (and perhaps write_memory_rewrite_size) if adding or removing code
		Instruction *start_instruction = GetCursorAddress<Instruction *>();
		Add(w7, *call_regs[0], sizeof(Sh4Context), LeaveFlags);
		Bfc(w7, 29, 3);		// addr &= ~0xE0000000
		Instruction *mem_instruction = GetCursorAddress<Instruction *>();
		unsigned prologue_size = (char*)mem_instruction - (char*)start_instruction;

		u32 size = op.flags & 0x7f;
		switch(size)
		{
		case 1:
			Strb(w1, MemOperand(x28, x7, SXTW));
			break;

		case 2:
			Strh(w1, MemOperand(x28, x7, SXTW));
			break;

		case 4:
			Str(w1, MemOperand(x28, x7));
			break;

		case 8:
			Str(x1, MemOperand(x28, x7));
			break;
		}

		unsigned code_size = (unsigned)(GetCursorAddress<char*>() - (char*)start_instruction);
		EnsureCodeSize(start_instruction, write_memory_rewrite_size);
		verify(code_size < 256 && prologue_size < 256);
		this->block->memory_accesses[mem_instruction] = { (uint16_t)opid, (uint8_t)prologue_size, (uint8_t)code_size };

		return true;
	}

	void EnsureCodeSize(Instruction *start_instruction, int code_size)
	{
		while (GetCursorAddress<Instruction *>() - start_instruction < code_size * kInstructionSize)
			Nop();
		verify (GetCursorAddress<Instruction *>() - start_instruction == code_size * kInstructionSize);
	}

	void CheckBlock(SmcCheckEnum smc_checks, RuntimeBlockInfo* block)
	{

		Label blockcheck_fail;
		Label blockcheck_success;

		switch (smc_checks) {
			case NoCheck:
				return;

			case FastCheck: {
				u8* ptr = GetMemPtr(block->addr, 4);
				if (ptr == NULL)
					return;
				Ldr(x9, reinterpret_cast<uintptr_t>(ptr));
				Ldr(w10, MemOperand(x9));
				Ldr(w11, *(u32*)ptr);
				Cmp(w10, w11);
				B(eq, &blockcheck_success);
			}
			break;

			case FullCheck: {
				s32 sz = block->sh4_code_size;

				u8* ptr = GetMemPtr(block->addr, sz);
				if (ptr == NULL)
					return;

				Ldr(x9, reinterpret_cast<uintptr_t>(ptr));

				while (sz > 0)
				{
					if (sz >= 8)
					{
						Ldr(x10, MemOperand(x9, 8, PostIndex));
						Ldr(x11, *(u64*)ptr);
						Cmp(x10, x11);
						sz -= 8;
						ptr += 8;
					}
					else if (sz >= 4)
					{
						Ldr(w10, MemOperand(x9, 4, PostIndex));
						Ldr(w11, *(u32*)ptr);
						Cmp(w10, w11);
						sz -= 4;
						ptr += 4;
					}
					else
					{
						Ldrh(w10, MemOperand(x9, 2, PostIndex));
						Mov(w11, *(u16*)ptr);
						Cmp(w10, w11);
						sz -= 2;
						ptr += 2;
					}
					B(ne, &blockcheck_fail);
				}
				B(&blockcheck_success);
			}
			break;

			default:
				die("unhandled smc_checks");
		}

		Bind(&blockcheck_fail);
		Ldr(w0, block->addr);
		// a direct branch: the stub is in .text, where an indirect one would need a landing pad (BTI)
		GenBranchRuntime(ngen_blockcheckfail);

		Bind(&blockcheck_success);
	}

	void shil_param_to_host_reg(const shil_param& param, const Register& reg)
	{
		if (param.is_imm())
		{
			Mov(reg, param._imm);
		}
		else if (param.is_reg())
		{
			if (param.is_r64f())
				Ldr(reg, sh4_context_mem_operand(param.reg_ptr()));
			else if (param.is_r32f())
				Fmov(reg, regalloc.MapVRegister(param));
			else
				Mov(reg, regalloc.MapRegister(param));
		}
		else
		{
			verify(param.is_null());
		}
	}

	void host_reg_to_shil_param(const shil_param& param, const CPURegister& reg)
	{
		if (reg.Is64Bits())
		{
			Str((const Register&)reg, sh4_context_mem_operand(param.reg_ptr()));
		}
		else if (regalloc.IsAllocg(param))
		{
			if (reg.IsRegister())
				Mov(regalloc.MapRegister(param), (const Register&)reg);
			else
				Fmov(regalloc.MapRegister(param), (const VRegister&)reg);
		}
		else
		{
			if (reg.IsVRegister())
				Fmov(regalloc.MapVRegister(param), (const VRegister&)reg);
			else
				Fmov(regalloc.MapVRegister(param), (const Register&)reg);
		}
	}

	struct CC_PS
	{
		CanonicalParamType type;
		shil_param* prm;
	};
	vector<CC_PS> CC_pars;
	std::vector<const WRegister*> call_regs;
	std::vector<const XRegister*> call_regs64;
	std::vector<const VRegister*> call_fregs;
	Arm64RegAlloc regalloc;
	RuntimeBlockInfo* block;
	const int write_memory_rewrite_size = 3;  // same size (fast write) for any size: add, bfc, str
	#ifdef EXPLODE_SPANS
	const int read_memory_rewrite_size = 6;	// worst case for u64: add, bfc, ldr, fmov, lsr, fmov
	#else
	const int read_memory_rewrite_size = 4;	// worst case for u64: add, bfc, ldr, str
	#endif
};

u32 DynaRBI::Relink()
{
	//printf("DynaRBI::Relink %08x\n", this->addr);

	A64_JIT_WRITE_PROTECT(false);
	Arm64Assembler *compiler = new Arm64Assembler((u8 *)this->code + this->relink_offset);

	u32 code_size = compiler->RelinkBlock(this);
	compiler->Finalize(true);
	delete compiler;

	A64_JIT_WRITE_PROTECT(true);
	return code_size;
}

void Arm64RegAlloc::Preload(u32 reg, eReg nreg)
{
	assembler->Ldr(Register(nreg, 32), assembler->sh4_context_mem_operand(GetRegPtr(reg)));
}
void Arm64RegAlloc::Writeback(u32 reg, eReg nreg)
{
	assembler->Str(Register(nreg, 32), assembler->sh4_context_mem_operand(GetRegPtr(reg)));
}
void Arm64RegAlloc::Preload_FPU(u32 reg, eFReg nreg)
{
	assembler->Ldr(VRegister(nreg, 32), assembler->sh4_context_mem_operand(GetRegPtr(reg)));
}
void Arm64RegAlloc::Writeback_FPU(u32 reg, eFReg nreg)
{
	assembler->Str(VRegister(nreg, 32), assembler->sh4_context_mem_operand(GetRegPtr(reg)));
}


// The store queue's write to main memory with the MMU off (the do_sqw_nommu of the context, called by the blocks and
// by the interpreter): x0 the address, x1 the store queues
// It is reached by indirect calls (blr from the blocks, a function pointer of C++): its first instruction is the landing
// pad of such a call (bti c, a hint: no operation on a core without BTI)
#define A64_SQW_NOMMU_AREA_3 \
		"hint #34					\n\t"	/* bti c */ \
		"and x12, x0, #0x20			\n\t"	/* SQ# selection, isolate */ \
		"add x12, x12, x1			\n\t"	/* SQ# selection, add to SQ ptr */ \
		"ld2 { v0.2D, v1.2D }, [x12]\n\t" \
		"movz x11, #0x0C00, lsl #16 \n\t" \
		"add x11, x1, x11			\n\t"	/* get ram ptr from x1, part 1 */ \
		"ubfx x0, x0, #5, #20		\n\t"	/* get ram offset */ \
		"add x11, x11, #512			\n\t"	/* get ram ptr from x1, part 2 */ \
		"add x11, x11, x0, lsl #5	\n\t"	/* ram + offset */ \
		"st2 { v0.2D, v1.2D }, [x11] \n\t" \
		"ret						\n"

#if defined(__ELF__) && !defined(__clang__)
// GCC has no naked functions on arm64: it ignores the attribute, with a warning, and a build without optimisation
// gives the function a frame, which the ret of the assembly would leave on the stack. With GCC the function is the
// assembly itself, in .text, as the stubs at the top of this file are
__asm__
(
		".pushsection .text			\n\t"
		".p2align 2					\n\t"
		A64_HIDDEN(do_sqw_nommu_area_3)
		".globl " A64_SYM(do_sqw_nommu_area_3) "	\n\t"
		".type " A64_SYM(do_sqw_nommu_area_3) ", %function	\n\t"
	A64_SYM(do_sqw_nommu_area_3) ":		\n\t"
		A64_SQW_NOMMU_AREA_3
		".size " A64_SYM(do_sqw_nommu_area_3) ", . - " A64_SYM(do_sqw_nommu_area_3) "	\n\t"
		".popsection				\n"
);
#else
extern "C" naked void do_sqw_nommu_area_3(u32 dst, u8* sqb)
{
	__asm__
	(
		A64_SQW_NOMMU_AREA_3

		: : : "memory"
	);
}
#endif

static Arm64Assembler* compiler;

struct Arm64NGenBackend: NGenBackend
{
	bool Init()
	{
		printf("Initializing the ARM64 dynarec\n");

		// What is written here is the first thing in the code cache, with the base of what a cache clear
		// recycles behind it: Init() is entered with an empty cache every time (recSH4::Init), also by the
		// backend object of a later emulator thread, which writes the same stubs to the same place
		verify(emit_FreeSpace() == CODE_SIZE);

		A64_JIT_WRITE_PROTECT(false);
		{
			Arm64Assembler stubs(emit_GetCCPtr(), 4096);

			stubs.GenLoopCode(&this->Mainloop);
			stubs.FinalizeCode();

			u8* begin = stubs.GetBuffer()->GetStartAddress<u8*>();
			u8* end = stubs.GetBuffer()->GetEndAddress<u8*>();

			vmem_platform_flush_cache(CC_RW2RX(begin), CC_RW2RX(end), begin, end);
			emit_Skip((u32)(end - begin));
		}
		A64_JIT_WRITE_PROTECT(true);

		// To prevent code cache reset from wiping these out
		emit_SetBaseAddr();

		this->FailedToFindBlock = (void (*)())a64_failed_to_find;

		return true;
	}

	void OnResetBlocks()
	{
	}

	void GetFeatures(ngen_features* dst)
	{
		dst->InterpreterFallback = false;
		dst->OnlyDynamicEnds = false;

		// Between two blocks every register is in the context, and an end of slice stores the pc (w29) in front
		// of UpdateSystem_INTC. Inside a block UpdateSystem is only called by SLEEP, in front of which the register
		// allocator flushes and the block stores the pc
		dst->CtxAtSlice = true;
	}

	int SliceCyclesLeft()
	{
		return a64_slice;
	}

	void ResetSlice()
	{
		a64_slice = SH4_TIMESLICE;
	}

	bool KeepsSlice()
	{
		return true;
	}

	void SetSliceCyclesLeft(int cycles)
	{
		a64_slice = cycles;
	}

	RuntimeBlockInfo* AllocateBlock()
	{
		return new DynaRBI();
	}


	void Compile(RuntimeBlockInfo* block, SmcCheckEnum smc_checks, bool reset, bool staging, bool optimise)
	{
		verify(emit_FreeSpace() >= 16 * 1024);

		compiler = new Arm64Assembler(emit_GetCCPtr());

		compiler->ngen_Compile(block, smc_checks, reset, staging, optimise);

		delete compiler;
		compiler = NULL;
	}

	void CC_Start(shil_opcode* op)
	{
		compiler->ngen_CC_Start(op);
	}

	void CC_Param(shil_opcode* op, shil_param* par, CanonicalParamType tp)
	{
		compiler->ngen_CC_Param(*op, *par, tp);
	}

	void CC_Call(shil_opcode*op, void* function)
	{
		compiler->ngen_CC_Call(op, function);
	}

	void CC_Finish(shil_opcode* op)
	{

	}

	bool Rewrite(rei_host_context_t* ctx)
	{
		A64_JIT_WRITE_PROTECT(false);
		//printf("ngen_Rewrite pc %p\n", host_pc);
		void *host_pc_rw = (void*)CC_RX2RW(ctx->pc);
		RuntimeBlockInfo *block = bm_GetBlock((void*)ctx->pc);
		
		if (block == NULL)
		{
			printf("ngen_Rewrite: trying stale block for %p \n", (void*)ctx->pc);
			block = bm_GetStaleBlock((void*)ctx->pc);
		}

		if (block == NULL)
		{
			printf("ngen_Rewrite: Block at %p not found\n", (void *)ctx->pc);
			A64_JIT_WRITE_PROTECT(true);
			return false;
		}
		u32 *code_ptr = (u32*)host_pc_rw;
		auto it = block->memory_accesses.find(code_ptr);
		if (it == block->memory_accesses.end())
		{
			printf("ngen_Rewrite: memory access at %p not found (%lu entries)\n", code_ptr, block->memory_accesses.size());
			A64_JIT_WRITE_PROTECT(true);
			return false;
		}
		u32 opid = it->second.opid;
		verify(opid < block->oplist.size());
		const shil_opcode& op = block->oplist[opid];
		Arm64Assembler *assembler = new Arm64Assembler(code_ptr - 2);	// Skip the 2 preceding ops (bic, add)
		assembler->InitializeRewrite(block, opid);
		if (op.op == shop_readm)
			assembler->GenReadMemorySlow(op);
		else
			assembler->GenWriteMemorySlow(op);
		assembler->Finalize(true);
		delete assembler;
		ctx->pc = (unat)CC_RW2RX(code_ptr - 2);

		A64_JIT_WRITE_PROTECT(true);
		return true;
	}
};

static NGenBackend* create_ngen_arm64() {
	return new Arm64NGenBackend();
}


static auto rec_cpp = rdv_RegisterShilBackend(ngen_backend_t{"arm64", "Slow arm64 jit", &create_ngen_arm64 });

#endif	// FEAT_SHREC == DYNAREC_JIT
