/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "types.h"

#include "../sh4_interpreter.h"
#include "../sh4_opcode_list.h"
#include "../sh4_core.h"
#include "../sh4_if.h"
#include "hw/sh4/sh4_interrupts.h"

#include "hw/mem/_vmem.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/gdrom/gdrom_if.h"

#include <time.h>
#include <float.h>

#include "blockmanager.h"
#include "ngen.h"
#include "decoder.h"

#include "debugger/debugger.h"
#include "hw/sh4/sh4_dbg.h"

#define bm_printf(...)

#if FEAT_SHREC != DYNAREC_NONE

u8 SH4_TCB[CODE_SIZE+16384]
#if HOST_OS == OS_WINDOWS
	;
#elif HOST_OS == OS_LINUX
	__attribute__((section(".text")));
#elif HOST_OS==OS_DARWIN
	__attribute__((section("__TEXT,.text")));
#else
	#error SH4_TCB ALLOC
#endif

u8* CodeCache;
uintptr_t cc_rx_offset;

u32 LastAddr;
u32 LastAddr_min;
u32* emit_ptr=0;
NGenBackend* rdv_ngen;

void* emit_GetCCPtr() { return emit_ptr==0?(void*)&CodeCache[LastAddr]:(void*)emit_ptr; }
void emit_SetBaseAddr() { LastAddr_min = LastAddr; }
void emit_WriteCodeCache()
{
	wchar path[512];
	sprintf(path,"/code_cache_%8p.bin",CodeCache);
	string pt2=get_writable_data_path(path);
	printf("Writing code cache to %s\n",pt2.c_str());
	FILE*f=fopen(pt2.c_str(),"wb");
	if (f)
	{
		fwrite(CodeCache,LastAddr,1,f);
		fclose(f);
		printf("Written!\n");
	}

	bm_WriteBlockMap(pt2+".map");
}

void RASDASD()
{
	LastAddr=LastAddr_min;
	memset(emit_GetCCPtr(),0xCC,emit_FreeSpace());
}

void emit_Write32(u32 data)
{
	if (emit_ptr)
	{
		*emit_ptr=data;
		emit_ptr++;
	}
	else
	{
		*(u32*)&CodeCache[LastAddr]=data;
		LastAddr+=4;
	}
}

void emit_Skip(u32 sz)
{
	LastAddr+=sz;
}
u32 emit_FreeSpace()
{
	return CODE_SIZE-LastAddr;
}


SmcCheckEnum DoCheck(u32 pc, u32 len)
{

	// is on bios or such
	if (!GetMemPtr(pc, len))
	{
		return NoCheck;
	}
	
	// no need for checks if fault based discard is used for this block
	if (!bm_RamPageHasData(pc, len))
	{
		// printf("FAST CHECK %08X, %d\n", pc, len);

		return NoCheck; //FaultCheck; //ValidationCheck;
	}
	else
	{
		// printf("SLOW CHECK %08X, %d\n", pc, len);		
	}

	// if no fault based discards, use whatever options
	switch (settings.dynarec.SmcCheckLevel) {

		// Heuristic-elimintaed FastChecks
		case NoCheck: {
			if (IsOnRam(pc))
			{
				pc&=0xFFFFFF;
				switch(pc)
				{
					//DOA2LE
					case 0x3DAFC6:
					case 0x3C83F8:

					//Shenmue 2
					case 0x348000:
						
					//Shenmue
					case 0x41860e:
					

						return FastCheck;

					default:
						return NoCheck;
				}
			}
			return NoCheck;
		}
		break;

		// Fast Check everything
		case FastCheck:
			return FastCheck;

		// Full Check everything
		case FullCheck:
			return FullCheck;

		default:
			die("Unhandled settings.dynarec.SmcCheckLevel");
			return FullCheck;
	}
}

void AnalyseBlock(RuntimeBlockInfo* blk);

char block_hash[1024];

#include "deps/crypto/sha1.h"

const char* RuntimeBlockInfo::hash(bool full, bool relocable)
{
	sha1_ctx ctx;
	sha1_init(&ctx);

	u8* ptr = GetMemPtr(this->addr,this->guest_opcodes*2);

	if (ptr)
	{
		if (relocable)
		{
			for (u32 i=0; i<this->guest_opcodes; i++)
			{
				u16 data=ptr[i];
				//Do not count PC relative loads (relocated code)
				if ((ptr[i]>>12)==0xD)
					data=0xD000;

				sha1_update(&ctx,2,(u8*)&data);
			}
		}
		else
		{
			sha1_update(&ctx,this->guest_opcodes*2,ptr);
		}
	}

	sha1_final(&ctx);

	if (full)
		sprintf(block_hash,">:%d:%08X:%02X:%08X:%08X:%08X:%08X:%08X",relocable,this->addr,this->guest_opcodes,ctx.digest[0],ctx.digest[1],ctx.digest[2],ctx.digest[3],ctx.digest[4]);
	else
		sprintf(block_hash,">:%d:%02X:%08X:%08X:%08X:%08X:%08X",relocable,this->guest_opcodes,ctx.digest[0],ctx.digest[1],ctx.digest[2],ctx.digest[3],ctx.digest[4]);

	//return ctx
	return block_hash;
}

void RuntimeBlockInfo::Setup(u32 rpc,fpscr_t rfpu_cfg,const DecodeLimits* limits)
{
	staging_runs=addr=lookups=runs=host_code_size=0;
	guest_cycles=guest_opcodes=host_opcodes=0;
	pBranchBlock=pNextBlock=0;
	code=0;
	has_jcond=false;
	BranchBlock=NextBlock=csc_RetCache=0xFFFFFFFF;
	BlockType=BET_SCL_Intr;
	
	addr=rpc;
	fpu_cfg=rfpu_cfg;
	
	oplist.clear();

	dec_DecodeBlock(this,SH4_TIMESLICE/2,limits);
	AnalyseBlock(this);
}

//The code cache is cleared when the guest enters one of these addresses and no block is there: compiled code of
//the boot rom aliases with ram
static bool rdv_IsSoftResetPc(u32 pc)
{
	return pc==0x8c0000e0 || pc==0xac010000 || pc==0xac008300;
}

//Compiles the block at next_pc into the block cache. 0: the code cache has to be emptied first. That is so at the
//addresses of a soft reset and when little of the cache is left (soft_resets: the caller has not emptied it
//yet), and whenever the backend finds no room for the code of the block.
//max_opcodes: not 0, the block ends behind that many instructions at the latest
DynarecCodeEntryPtr rdv_CompilePC_OrFail(bool soft_resets, u32 max_opcodes=0)
{
	u32 pc=next_pc;

	bm_printf("rdv_CompilePC: %08X\n", next_pc);


	if (soft_resets)
	{
		if (rdv_IsSoftResetPc(pc))
		{
			bm_printf("rdv_CompilePC: failed (soft jit reset) %08X\n", next_pc);	 	
			return nullptr;
		}

		if (emit_FreeSpace()<16*1024)
		{
			bm_printf("rdv_CompilePC: failed out of mem %08X\n", next_pc);
			return nullptr;
		}
	}

	const DecodeLimits limits = { max_opcodes, 0, -1, false };

	RuntimeBlockInfo* rv=0;
	do
	{
		RuntimeBlockInfo* rbi = rdv_ngen->AllocateBlock();
		if (rv==0) rv=rbi;

		rbi->Setup(pc,fpscr,max_opcodes ? &limits : 0);

		
		bool do_opts=((rbi->addr&0x3FFFFFFF)>0x0C010100);
		rbi->staging_runs=do_opts?100:-100;

		rdv_ngen->Compile(rbi,DoCheck(rbi->addr, rbi->sh4_code_size),(pc&0xFFFFFF)==0x08300 || (pc&0xFFFFFF)==0x10000,false,do_opts);

		if (rbi->code==0)
		{
			bm_printf("rdv_CompilePC: failed, no room for the code %08X\n", next_pc);
			delete rbi;
			return nullptr;
		}

		bool doLock = !bm_RamPageHasData(rbi->addr, rbi->sh4_code_size); // && maybe some setting?

		bm_AddBlock(rbi, doLock);

		if (rbi->BlockType==BET_Cond_0 || rbi->BlockType==BET_Cond_1)
			pc=rbi->NextBlock;
		else
			pc=0;
    #if BUILD_COMPILER==COMPILER_CLANG
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunreachable-code"
    #endif
	} while(false && pc);
    #if BUILD_COMPILER==COMPILER_CLANG
    #pragma clang diagnostic pop
    #endif

	bm_printf("rdv_CompilePC: end %08X\n", next_pc);

	return rv->code;
}

/*
	Compiles a block at next_pc that does not enter the block cache, runs it once and does the end-of-slice work
	if the slice ran out, as the main loop does behind a block. The space its code took is given back.

	This is a single step (limits: one instruction) and the rest of a block that a debugger check interrupted
	(limits: the end and the cost of that block). A block that is cached at the same address stays, and the
	dispatcher never finds this one: it would change the block boundaries and costs of the run.

	Only with ngen_features::DebugChecks.
*/
void rdv_RunTempBlock(const DecodeLimits* limits)
{
	u32 pc=next_pc;

	//what the dispatcher does when it has to compile at this pc
	if ((rdv_IsSoftResetPc(pc) && bm_GetCode(pc)==rdv_ngen->FailedToFindBlock) || emit_FreeSpace()<16*1024)
		sh4_cpu->ResetCache();

	DecodeLimits fewer=*limits;
	RuntimeBlockInfo* rbi;
	u32 cc_start;

	for (;;)
	{
		cc_start=LastAddr;

		rbi = rdv_ngen->AllocateBlock();

		rbi->Setup(pc,fpscr,&fewer);

		bool do_opts=((rbi->addr&0x3FFFFFFF)>0x0C010100);
		rbi->staging_runs=do_opts?100:-100;

		//The code was decoded this instant and runs right away: there is nothing to check at its entry, and the
		//failure path of that check would look the block up in the cache
		rdv_ngen->Compile(rbi,NoCheck,false,false,do_opts);

		if (rbi->code!=0)
			break;

		//No room for the code in what is left of the cache: the cache is emptied, and code that the empty
		//cache has no room for is made of fewer instructions, down to one
		delete rbi;

		if (LastAddr!=LastAddr_min)
		{
			sh4_cpu->ResetCache();
		}
		else
		{
			fewer.max_opcodes=fewer.max_opcodes ? fewer.max_opcodes/2 : 256;
			verify(fewer.max_opcodes!=0);
		}
	}

	u32 cc_end=LastAddr;

	bm_SetTempBlock(rbi);
	rdv_ngen->RunBlock((void*)CC_RW2RX(rbi->code));
	bm_SetTempBlock(0);

	//Nothing else is compiled while a block runs. A cache clear from inside the block moves LastAddr, and wins
	if (LastAddr==cc_end)
		LastAddr=cc_start;

	delete rbi;
}

//The rest of a block whose code ended in front of its last instruction: where the block ends, and the cycles of
//the rest
static bool rest_pending;
static u32 rest_end;
static u32 rest_cycles;

void DYNACALL rdv_BlockRest(u32 pc, u32 cycles, u32 end_pc)
{
	next_pc=pc;

	rest_pending=true;
	rest_end=end_pc;
	rest_cycles=cycles;
}

//Takes it, as the limits of the block that runs it. false: none is owed
static bool rdv_TakeBlockRest(DecodeLimits* limits)
{
	if (!rest_pending)
		return false;

	rest_pending=false;

	limits->max_opcodes=0;
	limits->end_pc=rest_end;
	limits->cycles=(s32)rest_cycles;
	limits->no_compile=false;

	return true;
}

DynarecCodeEntryPtr rdv_FindOrCompile_OrFail()
{
        DynarecCodeEntryPtr rv = bm_GetCode(next_pc);  // Returns exec addr
        if (rv == rdv_ngen->FailedToFindBlock)
        {
        	rv = rdv_CompilePC_OrFail(true);
        	
        	if (rv == nullptr)
        		return nullptr;

            rv = (DynarecCodeEntryPtr)CC_RW2RX(rv);  // Returns rw addr
        }
       
        return rv;
}

DynarecCodeEntryPtr rdv_CompilePC_OrClearCache()
{
	auto rv = rdv_CompilePC_OrFail(true);

	if (!rv)
	{
        sh4_cpu->ResetCache();

		rv = rdv_CompilePC_OrFail(false);

		//code that the empty cache has no room for: a block of fewer instructions, down to one
		for (u32 max_opcodes = 256; !rv && max_opcodes != 0; max_opcodes /= 2)
			rv = rdv_CompilePC_OrFail(false, max_opcodes);

		verify(rv != nullptr);
	}

	return rv;
}

DynarecCodeEntryPtr DYNACALL rdv_FailedToFindBlock(u32 pc)
{
	//printf("rdv_FailedToFindBlock ~ %08X\n",pc);
	next_pc=pc;

	return (DynarecCodeEntryPtr)CC_RW2RX(rdv_CompilePC_OrClearCache());
}

u32 DYNACALL rdv_DoInterrupts_pc(u32 pc) {
	next_pc = pc;
	UpdateINTC();

	//We can only safely relocate/etc stuff here, as in other generic update cases
	//There's a RET, meaning the code can't move around
	//Interrupts happen at least 50 times/second, so its not a problem ..

#if 0 // bm-extras not working anymore
	if (rebuild_counter == 0)
	{
		// TODO: Why is this commented, etc.
		//bm_Rebuild();
	}
#endif

	return next_pc;
}

void bm_Rebuild();
u32 DYNACALL rdv_DoInterrupts(void* block_cpde)
{
	RuntimeBlockInfo* rbi = bm_GetBlock(block_cpde);
	return rdv_DoInterrupts_pc(rbi->addr);
}

DynarecCodeEntryPtr DYNACALL rdv_BlockCheckFail(u32 pc)
{
	next_pc=pc;
	auto block = bm_GetBlock(pc);

	printf("Discard: %08X, %p\n", pc, block);

	bm_DiscardBlock(block);
	return (DynarecCodeEntryPtr)CC_RW2RX(rdv_CompilePC_OrClearCache());
}

void* DYNACALL rdv_LinkBlock(u8* code,u32 dpc)
{
	bm_printf("rdv_LinkBlock()\n");
	// code is the RX addr to return after, however bm_GetBlock returns RW
	RuntimeBlockInfo* rbi = bm_GetBlock(code);


	bool do_link = true;

	if (!rbi)
	{
		printf("Stale block ..\n");
		rbi = bm_GetStaleBlock(code);
		do_link = false;
	}
	
	verify(rbi != NULL);

	u32 bcls=BET_GET_CLS(rbi->BlockType);

	if (bcls==BET_CLS_Static)
	{
		next_pc=rbi->BranchBlock;
	}
	else if (bcls==BET_CLS_Dynamic)
	{
		next_pc=dpc;
	}
	else if (bcls==BET_CLS_COND)
	{
		if (dpc)
			next_pc=rbi->BranchBlock;
		else
			next_pc=rbi->NextBlock;
	}

	DynarecCodeEntryPtr rv = rdv_FindOrCompile_OrFail();  // Returns rx ptr or NULL

	if (rv != nullptr)
	{
		RuntimeBlockInfo* nxt=bm_GetBlock(next_pc);

		if (nxt)
		{
			if (do_link)
			{
				if (bcls==BET_CLS_Dynamic)
				{
					// this is broken for now, I don't think it is used
					verify(rbi->relink_data==0 || rbi->pBranchBlock==0);

					if (rbi->pBranchBlock!=0)
					{
						rbi->pBranchBlock->RemRef(rbi);
						rbi->pBranchBlock=0;
						rbi->relink_data=1;
					}
					else if (rbi->relink_data==0)
					{
						rbi->pBranchBlock=nxt;
						nxt->AddRef(rbi);
					}
				}
				else
				{
				
					if (rbi->BranchBlock==next_pc)
						rbi->pBranchBlock=nxt;
					if (rbi->NextBlock==next_pc)
						rbi->pNextBlock=nxt;

					nxt->AddRef(rbi);
				}
				u32 ncs=rbi->relink_offset+rbi->Relink();
				verify(rbi->host_code_size>=ncs);
				rbi->host_code_size=ncs;
			}
			else
			{
				printf(" .. null RBI: %08X -- unlinked stale block\n",next_pc);
			}
		}
	}
	else
	{
		rv = rdv_CompilePC_OrClearCache();
	}

	bm_printf("rdv_LinkBlock() 2\n");
	bm_CleanupDeletedBlocks();	// get rid of the old RBI, it is not needed any more
	
	bm_printf("rdv_LinkBlock() end\n");
	return (void*)rv;
}
#if defined(NULLDC_DEBUGGER)

/*
	The interpreter fallback (hw/sh4/sh4_dbg.h): the guest runs through the interpreter's handlers and is charged
	by the decoder's blocks. At each block start the block at pc is decoded, without compiling it, for its end,
	its cost and the refunds of its debugger checks; the cost goes off the slice counter, and instructions are
	interpreted up to the end of the block before the counter is tested.
*/

// Cycles left in the slice of the fallback loop. Kept when the loop is left and entered again.
static int fallback_slice = SH4_TIMESLICE;

// The guest last ran on the fallback loop: its counter is the one that tells the time
static bool fallback_ran;

// The two loops take turns in one slice where the backend's loop keeps its counter: whichever runs goes on from
// the counter of the one that ran before it, so that the slice ends where it would have without the switch.
// Behind a backend that starts a slice of its own at every entry the fallback keeps to its own counter.
static void fallback_Enter()
{
	if (!fallback_ran && rdv_ngen->KeepsSlice())
		fallback_slice = rdv_ngen->SliceCyclesLeft();

	fallback_ran = true;
}

static void fallback_Leave()
{
	if (fallback_ran && rdv_ngen->KeepsSlice())
		rdv_ngen->SetSliceCyclesLeft(fallback_slice);

	fallback_ran = false;
}

// The decoded block, reused from block to block
static RuntimeBlockInfo* fallback_blk;

// Decodes the block at next_pc, charges it and interprets it to its end.
// false: a debugger check or a flow report stopped inside; the refund is back on the counter
static bool fallback_RunBlock(u32 max_opcodes, u32 end_pc, s32 cycles)
{
	const DecodeLimits limits = { max_opcodes, end_pc, cycles, true };

	if (!fallback_blk)
		fallback_blk = rdv_ngen->AllocateBlock();

	RuntimeBlockInfo* rbi = fallback_blk;

	rbi->Setup(next_pc,fpscr,&limits);

	const u32 block_end = rbi->addr + rbi->sh4_code_size;

	fallback_slice -= rbi->guest_cycles;

	// the checks are in the order of their instructions
	size_t check = 0;

	// the instruction that runs next is the slot of a bt/s or bf/s that was not taken
	bool slot_left = false;

	for (;;)
	{
		u32 addr = next_pc;

		while (check < rbi->oplist.size() && rbi->oplist[check].op != shop_dbg_check)
			check++;

		// The decoder makes no check for a delay slot: the slot of a delayed branch that is not taken runs
		// here as an instruction of its own, without one
		if (slot_left)
		{
			dbg_sh4_SlotRuns();
		}
		else if (check < rbi->oplist.size() && rbi->oplist[check].rs1._imm == addr)
		{
			u32 refund = rbi->oplist[check].rs2._imm;
			check++;

			if (dbg_sh4_Check(addr, refund, block_end))
			{
				fallback_slice += refund;
				return false;
			}
		}

		u32 op = IReadMem16(addr);

		// A delayed branch that is taken is a control transfer wherever it goes, to its own slot as well: the
		// address is then arrived at as a branch target, at the start of a block. bt/s and bf/s that are not
		// taken leave their slot to this loop.
		const bool has_slot = (OpDesc[op]->type & Delayslot) != 0;

		slot_left = has_slot && (((op & 0xFF00) == 0x8D00 && sr.T == 0) || ((op & 0xFF00) == 0x8F00 && sr.T != 0));

		const bool taken_delayed = has_slot && !slot_left;

		next_pc = addr + 2;

		ExecuteOpcode(op);

		if (unlikely(dbg_sh4_stepout != 0))
		{
			// calls and returns report when they have run. RTE does it itself, in front of its interrupt check
			if ((op & 0xF000) == 0xB000 || (op & 0xF0FF) == 0x0003 || (op & 0xF0FF) == 0x400B)
				dbg_sh4_CheckFlow(DBG_SH4_FLOW_CALL);
			else if (op == 0x000B)
				dbg_sh4_CheckFlow(DBG_SH4_FLOW_RTS);

			if (dbg_sh4_StopPending())
				return false;
		}

		// the block ends with a control transfer, or behind its last instruction
		if (taken_delayed || next_pc != addr + 2 || next_pc == block_end)
			return true;
	}
}

// The end-of-slice work, when the slice ran out. false: the loop ends
static bool fallback_SliceEnd()
{
	if (fallback_slice > 0)
		return true;

	fallback_slice += SH4_TIMESLICE;

	// as the main loop of a jit: the cpu state is only looked at behind UpdateSystem
	// (the poll of the emulator thread: a relaxed load, sh4_if.h)
	UpdateSystem_INTC();

	return sh4_int_bCpuRun.load(std::memory_order_relaxed) != 0;
}

void sh4_fallback_loop()
{
	u32 end_pc, cycles;

	fallback_Enter();

	if (dbg_sh4_TakeRemainder(next_pc, &end_pc, &cycles))
	{
		// the rest of the block that a check interrupted, for the cycles that were given back
		if (!fallback_RunBlock(0, end_pc, (s32)cycles))
			return;
	}

	// Left behind the block that used the slice up (a flow report stopped there), or entered with the cpu
	// stopped: the end-of-slice work comes first
	if (!fallback_SliceEnd() || !sh4_int_bCpuRun.load(std::memory_order_relaxed))
		return;

	for (;;)
	{
		if (!fallback_RunBlock(0, 0, -1))
			return;

		if (!fallback_SliceEnd())
			return;
	}
}

void sh4_fallback_step()
{
	fallback_Enter();

	if (fallback_RunBlock(1, 0, -1))
		fallback_SliceEnd();
}

#endif

struct recSH4 : SuperH4Backend {
	~recSH4() { Term(); }

    // of the backend, constant: read once at Init
    ngen_features features = { };

    void ClearCache()
    {
        LastAddr = LastAddr_min;
        bm_Reset();

        printf("recSh4:Dynarec Cache clear at %08X\n", curr_pc);
    }

    void Loop()
    {
        sh4_dyna_rcb = (u8*)& Sh4cntx + sizeof(Sh4cntx);

        verify(rcb_noffs(&next_pc) == -184);

#if defined(NULLDC_DEBUGGER)
        fallback_Leave();
#endif

        // Resuming in the block that a debugger check interrupted: its rest runs first, as one block that ends
        // where the interrupted block ends and costs the cycles that were given back. A backend that cannot run
        // a block outside the block cache lets the rest go.
        DecodeLimits rest = { 0, 0, -1, false };
        u32 owed_cycles;
        bool have_rest = false;

        if (dbg_sh4_TakeRemainder(next_pc, &rest.end_pc, &owed_cycles) && features.DebugChecks)
        {
            rest.cycles = (s32)owed_cycles;
            have_rest = true;
        }

        for (;;)
        {
            if (have_rest)
            {
                rdv_RunTempBlock(&rest);

                if (dbg_sh4_StopPending())
                    return;

                // the code of that block can end in front of its last instruction as well
                have_rest = rdv_TakeBlockRest(&rest);
                continue;
            }

            rdv_ngen->Mainloop(sh4_dyna_rcb);

            // The main loop returns when the cpu was stopped, and when the code of a block handed the rest of
            // the block over (rdv_BlockRest): that runs, and the loop is entered again
            have_rest = rdv_TakeBlockRest(&rest);

            if (!have_rest)
                return;
        }
    }

    u32 DebugCaps()
    {
        return (features.DebugChecks ? SH4DBG_NATIVE : 0) | (features.CtxAtSlice ? SH4DBG_CTX_AT_SLICE : 0);
    }

    bool Step()
    {
        if (!features.DebugChecks)
            return false;

        sh4_dyna_rcb = (u8*)& Sh4cntx + sizeof(Sh4cntx);

#if defined(NULLDC_DEBUGGER)
        fallback_Leave();
#endif

        DecodeLimits one = { 1, 0, -1, false };
        rdv_RunTempBlock(&one);

        return true;
    }

    int SliceCyclesLeft()
    {
#if defined(NULLDC_DEBUGGER)
        if (fallback_ran)
            return fallback_slice;
#endif
        return rdv_ngen->SliceCyclesLeft();
    }

    void ResetSlice()
    {
        rdv_ngen->ResetSlice();
#if defined(NULLDC_DEBUGGER)
        fallback_slice = SH4_TIMESLICE;
#endif
    }

    void DebugDiscardPage(u32 addr)
    {
        bm_DiscardAddrPage(addr);
    }

    void DebugDiscardFlow(bool instrumented_only)
    {
        bm_DiscardFlowBlocks(instrumented_only);
    }

    bool Init()
    {
        printf("recSh4 Init\n");
        
        bm_Init();
        bm_Reset();

        verify(rcb_noffs(p_sh4rcb->fpcb) == FPCB_OFFSET);

        verify(rcb_noffs(p_sh4rcb->sq_buffer) == -512);

        verify(rcb_noffs(&p_sh4rcb->cntx.sh4_sched_next) == -152);
        verify(rcb_noffs(&p_sh4rcb->cntx.interrupt_pend) == -148);

        if (_nvmem_enabled()) {
            verify(sh4_cpu->mram.data == ((u8*)p_sh4rcb->sq_buffer + 512 + 0x0C000000));
        }

        // Prepare some pointer to the pre-allocated code cache:
        void* candidate_ptr = (void*)(((unat)SH4_TCB + REI_PAGE_MASK) & ~REI_PAGE_MASK);

        // Call the platform-specific magic to make the pages RWX
        CodeCache = NULL;
#ifdef FEAT_NO_RWX_PAGES
        verify(vmem_platform_prepare_jit_block(candidate_ptr, CODE_SIZE, (void**)& CodeCache, &cc_rx_offset));
#else
        verify(vmem_platform_prepare_jit_block(candidate_ptr, CODE_SIZE, (void**)& CodeCache));
#endif
        // Ensure the pointer returned is non-null
        verify(CodeCache != NULL);

       // memset(CodeCache, 0xFF, CODE_SIZE);
        verify(rdv_ngen->Init());

        bm_Reset();

        rdv_ngen->GetFeatures(&features);

        // Loop() is entered again after every debugger stop: what is said once per backend is said here
        sh4_dyna_rcb = (u8*)& Sh4cntx + sizeof(Sh4cntx);
        printf("cntx // fpcb offset: %td // pc offset: %td // pc %08X\n", (u8*)& sh4rcb.fpcb - sh4_dyna_rcb, (u8*)& sh4rcb.cntx.pc - sh4_dyna_rcb, sh4rcb.cntx.pc);

        if (!settings.dynarec.safemode)
            printf("Warning: Dynarec safe mode is off\n");

        if (settings.dynarec.unstable_opt)
            printf("Warning: Unstable optimizations is on\n");

        if (settings.dynarec.SmcCheckLevel != FullCheck)
            printf("Warning: SMC check mode is %d\n", settings.dynarec.SmcCheckLevel);

        return true;
    }

    void Term()
    {
        printf("recSh4 Term\n");
        bm_Term();
    }
};

SuperH4Backend*  Get_Sh4Recompiler()
{
    return new recSH4();
}

bool rdv_RegisterShilBackend(const ngen_backend_t& backend)
{
	verify(rdv_ngen == nullptr);

	rdv_ngen = backend.Create();
	
	return true;
};

#endif  // FEAT_SHREC != DYNAREC_NONE
