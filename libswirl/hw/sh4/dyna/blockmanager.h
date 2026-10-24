/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	In case you wonder, the extern "C" stuff are for the assembly code on beagleboard/pandora
*/
#include <map>
#include "types.h"
#include "decoder.h"
#pragma once
#include <set>

typedef void (*DynarecCodeEntryPtr)();

struct RuntimeBlockInfo_Core
{
	u32 addr;
	DynarecCodeEntryPtr code;
	u32 lookups;
};

struct RuntimeBlockInfo: RuntimeBlockInfo_Core
{
	void Setup(u32 pc,fpscr_t fpu_cfg,const DecodeLimits* limits=0);
	const char* hash(bool full=true, bool reloc=false);

	u32 host_code_size;	//in bytes
	u32 sh4_code_size; //in bytes

	u32 runs;
	s32 staging_runs;

	/*
		
	*/
	fpscr_t fpu_cfg;
	u32 guest_cycles;
	u32 guest_opcodes;
	u32 host_opcodes;


	u32 BranchBlock; //if not 0xFFFFFFFF then jump target
	u32 NextBlock;   //if not 0xFFFFFFFF then next block (by position)

	//0 if not available
	RuntimeBlockInfo* pBranchBlock;
	RuntimeBlockInfo* pNextBlock; 

	u32 relink_offset;
	u32 relink_data;
	u32 csc_RetCache; //only for stats for now

	BlockEndType BlockType;
	bool has_jcond;
	u8 dbg_flow; //DbgSh4Flow: the block ends with a call or a return and its epilogue reports it to the debugger (step-out)

	//A block with more debugger checks than its backend compiles into one (ngen_features::DebugCheckBudget): oplist
	//ends in front of the instruction at dbg_rest_pc, and the compiled code hands the rest of the block over there,
	//with dbg_rest_cycles of guest_cycles given back for it (rdv_BlockRest). addr, sh4_code_size, guest_opcodes and
	//guest_cycles are those of the whole block; BlockType is BET_StaticJump to dbg_rest_pc. 0: the block is whole
	u32 dbg_rest_pc;
	u32 dbg_rest_cycles;

	//A block that runs outside the block cache (rdv_RunTempBlock: a single step, the rest of a block), set between
	//Setup and Compile. Nothing links it or links to it, and its backend makes it end without a link: a linking
	//backend (arm64) stores its next pc and returns to RunBlock. Setup clears it
	bool temporary;

	vector<shil_opcode> oplist;

	bool contains_code(u8* ptr)
	{
		return ((unat)(ptr-(u8*)code))<host_code_size;
	}

	virtual ~RuntimeBlockInfo();

	virtual u32 Relink()=0;
	virtual void Relocate(void* dst)=0;
	
	//predecessors references
	set<RuntimeBlockInfo*> pre_refs;

	void AddRef(RuntimeBlockInfo* other);
	void RemRef(RuntimeBlockInfo* other);

	void Discard();
	void UpdateRefs();

	u32 memops;
	u32 linkedmemops;
	// key is a host pc for a load/store in a jit block
	// value contains information for mem-op rewriting purposes.
	struct memop_info {
		uint16_t opid;
		uint8_t  rewrite_offset;
		uint8_t  emitted_bytes;
	};		
	std::map<void*, memop_info> memory_accesses;
};

struct CachedBlockInfo: RuntimeBlockInfo_Core
{
	RuntimeBlockInfo* block;
};

void bm_WriteBlockMap(const string& file);


DynarecCodeEntryPtr DYNACALL bm_GetCode(u32 addr);

extern "C" {
ATTR_USED DynarecCodeEntryPtr DYNACALL bm_GetCode2(u32 addr);
}

RuntimeBlockInfo* bm_GetBlock(void* dynarec_code);
RuntimeBlockInfo* bm_GetStaleBlock(void* dynarec_code);
RuntimeBlockInfo* DYNACALL bm_GetBlock(u32 addr);

void bm_AddBlock(RuntimeBlockInfo* blk, bool lockRam);
void bm_DiscardBlock(RuntimeBlockInfo* blk);

//Discards every block that overlaps the page of the guest address, in any mirror: the blocks that have the address
//as an instruction or as a delay slot are among them. Page protection and the pages marked as holding data stay.
void bm_DiscardAddrPage(u32 addr);
//Discards the blocks that end with a call or a return (false), or of those the ones that report it to the
//debugger (true)
void bm_DiscardFlowBlocks(bool instrumented_only);
//The block that runs outside the block cache, 0: none. It is in no list: bm_GetBlock(void*) knows it, so that a
//memory access of its code can be rewritten
void bm_SetTempBlock(RuntimeBlockInfo* blk);

void bm_Reset();
void bm_Periodical_1s();
void bm_Periodical_14k();
void bm_Sort();

void bm_Init();
void bm_Term();

void bm_vmem_pagefill(void** ptr,u32 PAGE_SZ);

void bm_sh4_jitsym(FILE* out);
bool bm_LockedWrite(u8* addy);
bool bm_RamPageHasData(u32 guest_addr, u32 len);

void bm_CleanupDeletedBlocks();