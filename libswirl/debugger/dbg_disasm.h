/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include <cstdint>
#include <string>

/*
	Disassemblers for the three guest processors: SH7091 (SH-4), ARM7DI (AICA sound CPU), AICA DSP.

	Everything here is a pure function of its arguments: no emulator state is read, nothing is cached,
	and every function may be called from any thread. Guest memory and symbols are reached only through
	the optional callbacks in DisasmCtx.

	Text conventions
	- lower case (except CPSR / SPSR and the DSP field names); the mnemonic is padded with spaces to
	  column 8 when operands follow; no trailing space
	- SH-4 follows GNU objdump: "mov.l   @(8,r1),r2", comments start with '!'
	- ARM follows GNU objdump in unified syntax: "ldrbeq  r0, [r1, #4]!", comments start with '@'
	- addresses are always 0x%08x; an encoding that is not an instruction is ".word 0x<raw>"
*/
namespace dbg
{
	// Symbol lookup. Returns true when addr lies inside a symbol; *name must stay valid until the
	// disassembler call returns, *offset is addr minus the symbol start. Returns false otherwise.
	typedef bool (*SymbolFn)(void* user, uint32_t addr, const char** name, uint32_t* offset);

	// Side-effect-free guest read of size bytes (1, 2 or 4) at addr, little endian, zero extended into
	// *value. Returns false when the address cannot be read without side effects.
	typedef bool (*ReadFn)(void* user, uint32_t addr, uint32_t size, uint32_t* value);

	// Either callback may be null. A null DisasmCtx* is the same as both callbacks null.
	struct DisasmCtx
	{
		SymbolFn symbol;
		ReadFn read;
		void* user;		// passed back as the first argument of both callbacks
	};

	// FPSCR bits that change how SH-4 FPU instructions read. Combine with |.
	enum Sh4FpMode
	{
		SH4_FP_PR = 1,	// FPSCR.PR: arithmetic operates on DRn
		SH4_FP_SZ = 2,	// FPSCR.SZ: fmov transfers DRn / XDn pairs
	};

	// What the debugger core needs to know about an instruction without decoding it again.
	// When valid is false every other flag is false and only length is meaningful.
	struct Sh4InsnInfo
	{
		bool valid;				// an SH7091 instruction; does not depend on fp_mode
		bool is_branch;			// may continue somewhere other than pc + 2 (not set for trapa)
		bool is_delayed_branch;	// is_branch, and the instruction at pc + 2 executes before the transfer
		bool is_call;			// bsr, bsrf, jsr
		bool is_return;			// rts, rte
		bool is_conditional;	// bt, bf, bt.s, bf.s
		bool is_trap;			// trapa
		bool has_target;		// target is the destination of a PC-relative branch
		bool has_literal;		// literal_addr is a PC-relative data address (mov.w / mov.l @(disp,PC), mova)
		uint32_t target;
		uint32_t literal_addr;
		uint32_t literal_size;	// bytes read from literal_addr: 2 or 4; 0 for mova (address only)
		uint32_t length;		// always 2
	};

	struct ArmInsnInfo
	{
		bool valid;				// a documented ARM7DI (ARMv3) instruction
		bool is_branch;			// writes R15: b, bl, and data processing / ldr / ldm with pc as destination
		bool is_call;			// bl
		bool is_return;			// best effort: mov pc, lr; subs pc, lr, #n; ldm with sp as base and pc in
								// the list; ldr pc with sp as base
		bool is_conditional;	// condition field is not AL (NV counts: it never executes)
		bool is_trap;			// swi
		bool has_target;		// target is the destination of b / bl
		bool has_literal;		// literal_addr is a PC-relative data address
		uint32_t target;
		uint32_t literal_addr;
		uint32_t literal_size;	// bytes transferred at literal_addr: 1 or 4 for ldr / str with pc as base
								// and an immediate offset; 0 for add / sub rd, pc, #imm (address only)
		uint32_t length;		// always 4
	};

	/*
		One SH-4 instruction. pc is the address of the instruction, op the 16-bit opcode, fp_mode a
		combination of Sh4FpMode. ctx and info may be null.

		- valid is true for exactly the encodings the interpreter table (hw/sh4/sh4_opcode_list.cpp)
		  executes, except 0x085b, the HLE BIOS trap, which is not an SH-4 instruction
		- PC-relative operands are printed as the absolute address they resolve to:
		    "bsr     0x8c010200 <_start+0x40>"	branch target, symbol appended when ctx->symbol resolves it
		    "mov.l   0x8c010320,r1 ! 0x8c0f0000 <_stack>"
		    "mov.w   0x8c010324,r2 ! 0xffff8000"
		  the comment is the value the register receives (mov.w sign extended) and is present only when
		  ctx->read succeeds; the symbol after a mov.l value is present when ctx->symbol resolves the value
		- FPSCR.SZ = 0: "fmov fr1,fr2", "fmov.s @r1,fr2"; SZ = 1: "fmov dr2,xd4", "fmov @r1,dr2"
		  (bit 0 of a register field selects XDn)
		- FPSCR.PR = 1: fadd fsub fmul fdiv fcmp/eq fcmp/gt fabs fneg fsqrt float ftrc name DRn; bit 0 of
		  a register field is ignored, as the interpreter does. fcnvsd, fcnvds and fsca always name DRn,
		  fldi0 fldi1 flds fsts fmac fsrra always FRn, fipr and ftrv FVn
		- no register values are shown
	*/
	std::string DisasmSh4(uint32_t pc, uint16_t op, unsigned fp_mode, const DisasmCtx* ctx, Sh4InsnInfo* info);

	/*
		One ARM7DI instruction (32-bit ARM state; there is no Thumb). pc is the address of the
		instruction, op the 32-bit opcode. ctx and info may be null.

		- valid is true for: data processing, mrs / msr, mul / mla, swp / swpb, ldr / str, ldm / stm,
		  b / bl, swi, and the coprocessor classes cdp, mrc / mcr, ldc / stc (which trap on the AICA, where
		  no coprocessor answers). It is false for the undefined-instruction space (bits 27:25 = 011 with
		  bit 4 set), for every other encoding with bits 27:25 = 000 and bits 7, 4 set (the ARMv4 long
		  multiplies and halfword transfers), and for the PSR transfer space (tst / teq / cmp / cmn
		  opcodes with S clear) outside the exact mrs / msr encodings
		- fields the ARM7DI ignores are not shown: Rn of mov / mvn, Rd of tst / teq / cmp / cmn (Rd = 15
		  is the 26-bit form and prints as tstp / teqp / cmpp / cmnp), Rn of mul
		- canonical forms only, no aliases: "mov r0, r1, lsl #2", "stmdb sp!, {r4, lr}", "swi 0x123456";
		  an immediate whose rotation is not the smallest one for its value is "#imm8, rotation"
		- condition NV is printed as the suffix "nv"
		- numbers below 10 are decimal, larger ones hexadecimal; shift amounts are decimal
		- "b       0x00001234 <sym+0x8>"		branch target, symbol appended when ctx->symbol resolves it
		- "ldr     r0, [pc, #0x1c] @ 0x00001048 = 0x00800000"
		  the comment carries the address; "= value" (4 or 1 bytes) is present only for loads and only when
		  ctx->read succeeds, and a 4-byte value is followed by " <symbol>" when ctx->symbol resolves it.
		  add / sub rd, pc, #imm carry "@ address"
	*/
	std::string DisasmArm7(uint32_t pc, uint32_t op, const DisasmCtx* ctx, ArmInsnInfo* info);

	/*
		One AICA DSP program step: step is the index (0..127), mpro the four 16-bit MPRO words of that
		step in register order. The result is one line of the DSP playground assembler,
		    "MPRO[12] = TRA:3 TWT TWA:5 YSEL:1 SHIFT:2"
		fields in the fixed order TRA TWT TWA XSEL YSEL IRA IWT IWA TABLE MWT MRD EWT EWA ADRL FRCL SHIFT
		YRL NEGB ZERO BSEL NOFL MASA ADREB NXADR, zero fields omitted, one-bit fields by name only,
		the others as NAME:decimal. A step with every field zero is "MPRO[12] = TRA:0", which assembles
		back to four zero words. Bits outside the listed fields are not shown.
	*/
	std::string DisasmDsp(unsigned step, const uint16_t mpro[4]);
}
