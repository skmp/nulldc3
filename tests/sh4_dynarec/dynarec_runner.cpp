//before types.h: xbyak has members that are called like its macros
#ifndef XBYAK_NO_OP_NAMES
#define XBYAK_NO_OP_NAMES
#endif
#include "deps/xbyak/xbyak.h"

#include "dynarec_runner.h"
#include "test_guard.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/dyna/decoder.h"
#include "hw/sh4/dyna/ngen.h"

//hw/sh4/dyna/driver.cpp, hw/sh4/dyna/shil.cpp
extern u32 LastAddr;
extern u32 LastAddr_min;
void AnalyseBlock(RuntimeBlockInfo* blk);

typedef void BlockEntryFP(DynarecCodeEntryPtr code);

/*
	Blocks keep guest registers in host registers that the caller of a function expects to find unchanged, and
	leave saving them to the main loop the backend generates (BlockCompiler::build_mainloop). This is that
	loop's prologue, call and epilogue: void entry(DynarecCodeEntryPtr code)
*/
struct BlockEntry : Xbyak::CodeGenerator
{
	BlockEntry(void* buffer) : Xbyak::CodeGenerator(256, buffer)
	{
#ifdef _WIN32
		//32 bytes for the callee, 10 xmm registers and what keeps the stack 16 byte aligned
		const u32 frame = 32 + 10 * 16 + 8;
		const Xbyak::Reg64& code = rcx;
#else
		const u32 frame = 8;
		const Xbyak::Reg64& code = rdi;
#endif

		push(rbx);
		push(rbp);
#ifdef _WIN32
		push(rdi);
		push(rsi);
#endif
		push(r12);
		push(r13);
		push(r14);
		push(r15);
		sub(rsp, frame);

#ifdef _WIN32
		for (int i = 0; i < 10; i++)
			movaps(xword[rsp + 32 + i * 16], Xbyak::Xmm(6 + i));
#endif

		call(code);

#ifdef _WIN32
		for (int i = 0; i < 10; i++)
			movaps(Xbyak::Xmm(6 + i), xword[rsp + 32 + i * 16]);
#endif

		add(rsp, frame);
		pop(r15);
		pop(r14);
		pop(r13);
		pop(r12);
#ifdef _WIN32
		pop(rsi);
		pop(rdi);
#endif
		pop(rbp);
		pop(rbx);
		ret();

		ready();
	}
};

static SuperH4Backend* recompiler;
static BlockEntryFP* block_entry;
static RuntimeBlockInfo* block;

void dynarec_init()
{
	//as the emulator starts up (LoadSettings)
	settings.dynarec.Enable = true;
	settings.dynarec.idleskip = true;
	settings.dynarec.unstable_opt = false;
	settings.dynarec.safemode = true;
	settings.dynarec.SmcCheckLevel = FullCheck;

	//guest memory is the handlers alone, there is no host mapping of it for blocks to access
	settings.dynarec.disable_nvmem = true;

	recompiler = Get_Sh4Recompiler();
	verify(recompiler->Init());

	//the entry goes into the code cache, below what is recycled
	BlockEntry entry(emit_GetCCPtr());

	block_entry = (BlockEntryFP*)CC_RW2RX(entry.getCode());

	emit_Skip((u32)entry.getSize());
	emit_SetBaseAddr();
}

void dynarec_run(const DynarecCode& code, u32 count, u32* executed)
{
	//what a run that was ended from the inside left behind
	delete block;
	block = 0;

	LastAddr = LastAddr_min;

	u32 done = 0;

	while (done < count)
	{
		u32 pc = Sh4cntx.pc;
		u32 left = count - done;

		//The decoder ends a block in front of the instruction at which the block's cycles have reached
		//max_cycles. An instruction counts CPU_RATIO cycles, an FPU instruction none
		u32 max_cycles = 0;

		for (u32 i = 0; i < left; i++)
		{
			if (code.opcode_at(pc + i * 2) < 0xF000)
				max_cycles += CPU_RATIO;
		}

		block = rdv_ngen->AllocateBlock();

		//RuntimeBlockInfo::Setup, which decodes with a fixed max_cycles
		block->staging_runs = block->addr = block->lookups = block->runs = block->host_code_size = 0;
		block->guest_cycles = block->guest_opcodes = block->host_opcodes = 0;
		block->pBranchBlock = block->pNextBlock = 0;
		block->code = 0;
		block->has_jcond = false;
		block->BranchBlock = block->NextBlock = block->csc_RetCache = 0xFFFFFFFF;
		block->BlockType = BET_SCL_Intr;

		block->addr = pc;
		block->fpu_cfg = Sh4cntx.fpscr;

		block->oplist.clear();

		code.serve_opcodes(true);
		dec_DecodeBlock(block, max_cycles);
		code.serve_opcodes(false);

		AnalyseBlock(block);

		if (block->guest_opcodes == 0 || block->guest_opcodes > left)
			test_guard_abort("the block at %08X has %u instructions, %u are left to run", pc, block->guest_opcodes, left);

		//rdv_CompilePC_OrFail. Guest code is not backed by host memory, so there is nothing to check a block against
		bool do_opts = ((block->addr & 0x3FFFFFFF) > 0x0C010100);
		block->staging_runs = do_opts ? 100 : -100;

		rdv_ngen->Compile(block, NoCheck, false, false, do_opts);
		verify(block->code != 0);

		block_entry((DynarecCodeEntryPtr)CC_RW2RX(block->code));

		for (u32 i = 0; i < block->guest_opcodes; i++)
			executed[done + i] = pc + i * 2;

		done += block->guest_opcodes;

		delete block;
		block = 0;
	}
}
