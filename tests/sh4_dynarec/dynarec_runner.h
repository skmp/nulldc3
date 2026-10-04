/*
	Runs SH4 code through the recompiler the way the emulator does (decoder -> shil -> x64 backend), a few
	instructions at a time and without the block manager and the main loop.

	A block is decoded from the current PC, capped to the instructions that are left to run, compiled, entered
	once and thrown away, until the instructions are done. The decoder reads a whole block before any of it
	runs, so guest code is served by address while a block is decoded and the block itself only makes its data
	accesses.

	Blocks are entered directly, which needs to know how the backend's main loop calls them. That is done for
	the x64 backend only.
*/
#pragma once
#include "types.h"

//Where the guest code comes from
struct DynarecCode
{
	u32 (*opcode_at)(u32 addr);			//the opcode at addr. For the cap, the decoder reads through the memory handlers
	void (*serve_opcodes)(bool on);		//on while a block is decoded: 16 bit reads are instruction fetches
};

//Once, when the SH4 environment (sh4_env.h) is up and the guest memory handlers are mapped
void dynarec_init();

//Runs count instructions from Sh4cntx.pc. executed gets the address of each of them, in the order they ran.
//Has to run guarded (test_guard.h): it ends in test_guard_abort() when the blocks do not add up to count
//instructions, and whatever the recompiler dies of ends it as well
void dynarec_run(const DynarecCode& code, u32 count, u32* executed);

//What the last dynarec_run compiled, also when it did not come back: the number of instructions whose blocks
//were compiled, and which of them are not native code. Bit i is set when the block of instruction i calls the
//interpreter's handler for it (shop_ifb, the decoder's fallback)
u32 dynarec_compiled();
u32 dynarec_interpreted();

//Instruction index of the last dynarec_run was compiled to native code
static inline bool dynarec_native(u32 index)
{
	return index < dynarec_compiled() && (dynarec_interpreted() & (1u << index)) == 0;
}
