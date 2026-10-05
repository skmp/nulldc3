/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "shil.h"
#include "../sh4_if.h"

#define mkbet(c,s,v) ((c<<3)|(s<<1)|v)
#define BET_GET_CLS(x) (x>>3)

enum BlockEndType
{
	BET_CLS_Static=0,
	BET_CLS_Dynamic=1,
	BET_CLS_COND=2,

	BET_SCL_Jump=0,
	BET_SCL_Call=1,
	BET_SCL_Ret=2,
	BET_SCL_Intr=3,
	

	BET_StaticJump=mkbet(BET_CLS_Static,BET_SCL_Jump,0),    //BranchBlock is jump target
	BET_StaticCall=mkbet(BET_CLS_Static,BET_SCL_Call,0),    //BranchBlock is jump target, NextBlock is ret hint
	BET_StaticIntr=mkbet(BET_CLS_Static,BET_SCL_Intr,0),    //(pending inttr!=0) -> Intr else NextBlock

	BET_DynamicJump=mkbet(BET_CLS_Dynamic,BET_SCL_Jump,0),  //pc+2 is jump target
	BET_DynamicCall=mkbet(BET_CLS_Dynamic,BET_SCL_Call,0),  //pc+2 is jump target, NextBlock is ret hint
	BET_DynamicRet=mkbet(BET_CLS_Dynamic,BET_SCL_Ret,0),    //pr is jump target
	BET_DynamicIntr=mkbet(BET_CLS_Dynamic,BET_SCL_Intr,0),  //(pending inttr!=0) -> Intr else Dynamic

	BET_Cond_0=mkbet(BET_CLS_COND,BET_SCL_Jump,0),          //sr.T==0 -> BranchBlock else NextBlock
	BET_Cond_1=mkbet(BET_CLS_COND,BET_SCL_Jump,1),          //sr.T==1 -> BranchBlock else NextBlock
};

enum NextDecoderOperation
{
	NDO_NextOp,     //pc+=2
	NDO_End,        //End the block, Type = BlockEndType
	NDO_Delayslot,  //pc+=2, NextOp=DelayOp
	NDO_Jump,       //pc=JumpAddr,NextOp=JumpOp
};
//ngen features
struct ngen_features
{
	bool OnlyDynamicEnds;     //if set the block endings aren't handled natively and only Dynamic block end type is used
	bool InterpreterFallback; //if set all the non-branch opcodes are handled with the ifb opcode
	bool DebugChecks;         //if set the backend implements everything SH4DBG_NATIVE stands for (hw/sh4/sh4_if.h):
	                          //only then do the blocks it compiles get debugger checks and flow reports (debugger/debugger.h)
	bool CtxAtSlice;          //if set Sh4cntx is complete, pc included, whenever the generated code calls UpdateSystem (SH4DBG_CTX_AT_SLICE)
	//A backend whose GetFeatures does not set the last two leaves them clear, because what it fills is the decoder's
	//state, which has static storage. An ngen_features of any other storage is zeroed in front of GetFeatures.
};

//Limits of a decode that is not the one of a cached block: a single step, the rest of a block that a debugger
//check interrupted (both run once, outside the block cache) and the blocks of the interpreter fallback
//(hw/sh4/sh4_dbg.h), which are decoded for their boundaries, cost and refunds and never compiled.
//An idiom that folds several instructions into one unit is not matched under max_opcodes, nor across end_pc.
struct DecodeLimits
{
	u32 max_opcodes;  //end the block behind this many instructions, a delay slot goes with its branch. 0: no limit
	u32 end_pc;       //end the block in front of this address. 0: none
	s32 cycles;       //the cost of the block, replacing the computed one. negative: computed as for any block
	bool no_compile;  //the block is not compiled: it gets the debugger checks whatever ngen_features::DebugChecks says
};

//DbgSh4Flow (debugger/debugger.h) of a block end: a call, a return or DBG_SH4_FLOW_NONE
u32 dec_BlockFlow(BlockEndType type);

struct RuntimeBlockInfo;
void dec_DecodeBlock(RuntimeBlockInfo* rbi,u32 max_cycles,const DecodeLimits* limits=0);

struct state_t
{
	NextDecoderOperation NextOp;
	NextDecoderOperation DelayOp;
	NextDecoderOperation JumpOp;
	u32 JumpAddr;
	u32 NextAddr;
	BlockEndType BlockType;

	struct
	{
		bool FPR64; //64 bit FPU opcodes
		bool FSZ64; //64 bit FPU moves
		bool RoundToZero; //false -> Round to nearest.
		u32 rpc;
		bool is_delayslot;
	} cpu;

	ngen_features ngen;

	struct
	{
		bool has_readm;
		bool has_writem;
		bool has_fpu;
	} info;

	u32 dbg_checks; //shop_dbg_check ops in the block. They are not counted for the block size limits

} ;
