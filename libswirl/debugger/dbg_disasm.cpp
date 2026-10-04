/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_disasm.h"

#include <cstdarg>
#include <cstdio>

namespace dbg
{
namespace
{
	void Appendf(std::string& s, const char* fmt, ...)
	{
		char buf[64];
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		s += buf;
	}

	//Mnemonic column: operands start at column 8, or one space after a longer mnemonic
	void PadMnemonic(std::string& s)
	{
		if (s.size() < 8)
			s.append(8 - s.size(), ' ');
		else
			s += ' ';
	}

	// " <name>" or " <name+0x10>" when the address resolves, nothing otherwise
	void AppendSymbol(std::string& s, const DisasmCtx* ctx, uint32_t addr)
	{
		const char* name = 0;
		uint32_t offset = 0;

		if (!ctx || !ctx->symbol || !ctx->symbol(ctx->user, addr, &name, &offset) || !name)
			return;

		s += " <";
		s += name;
		if (offset)
			Appendf(s, "+0x%x", offset);
		s += '>';
	}

	bool ReadGuest(const DisasmCtx* ctx, uint32_t addr, uint32_t size, uint32_t* value)
	{
		return ctx && ctx->read && ctx->read(ctx->user, addr, size, value);
	}

	/*
		SH-4
	*/
	enum
	{
		SF_BRANCH	= 1 << 0,
		SF_DELAY	= 1 << 1,
		SF_CALL		= 1 << 2,
		SF_RETURN	= 1 << 3,
		SF_COND		= 1 << 4,
		SF_TRAP		= 1 << 5,
		SF_FMOV_MEM	= 1 << 6,	//fmov to / from memory: "fmov.s" when FPSCR.SZ = 0

		SF_JUMP		= SF_BRANCH | SF_DELAY,
	};

	/*
		args is copied to the output, with these escapes:
		%n %m		Rn (bits 11:8), Rm (bits 7:4)
		%b			Rm_BANK (bits 6:4)
		%i %s		#imm8 unsigned, signed
		%1 %2 %4	disp4 scaled by 1, 2, 4
		%B %W %L	disp8 scaled by 1, 2, 4
		%w %l %a	PC-relative: word literal, long literal, mova address
		%d %D		branch target: disp8, disp12
		%F %f		FRn, FRm
		%X %x		FRn, FRm; DRn, DRm when FPSCR.PR = 1
		%Z %z		FRn, FRm; DRn / XDn, DRm / XDm when FPSCR.SZ = 1
		%P			DRn
		%V %v		FVn (bits 11:10), FVm (bits 9:8)
	*/
	struct Sh4Op
	{
		uint16_t mask;
		uint16_t bits;
		uint16_t flags;
		const char* name;
		const char* args;
	};

	//No two entries match the same opcode
	const Sh4Op sh4_ops[] =
	{
		{ 0xF0FF, 0x0002, 0, "stc", "sr,%n" },
		{ 0xF0FF, 0x0012, 0, "stc", "gbr,%n" },
		{ 0xF0FF, 0x0022, 0, "stc", "vbr,%n" },
		{ 0xF0FF, 0x0032, 0, "stc", "ssr,%n" },
		{ 0xF0FF, 0x0042, 0, "stc", "spc,%n" },
		{ 0xF08F, 0x0082, 0, "stc", "%b,%n" },
		{ 0xF0FF, 0x0003, SF_JUMP | SF_CALL, "bsrf", "%n" },
		{ 0xF0FF, 0x0023, SF_JUMP, "braf", "%n" },
		{ 0xF0FF, 0x0083, 0, "pref", "@%n" },
		{ 0xF0FF, 0x0093, 0, "ocbi", "@%n" },
		{ 0xF0FF, 0x00A3, 0, "ocbp", "@%n" },
		{ 0xF0FF, 0x00B3, 0, "ocbwb", "@%n" },
		{ 0xF0FF, 0x00C3, 0, "movca.l", "r0,@%n" },
		{ 0xF00F, 0x0004, 0, "mov.b", "%m,@(r0,%n)" },
		{ 0xF00F, 0x0005, 0, "mov.w", "%m,@(r0,%n)" },
		{ 0xF00F, 0x0006, 0, "mov.l", "%m,@(r0,%n)" },
		{ 0xF00F, 0x0007, 0, "mul.l", "%m,%n" },
		{ 0xFFFF, 0x0008, 0, "clrt", "" },
		{ 0xFFFF, 0x0018, 0, "sett", "" },
		{ 0xFFFF, 0x0028, 0, "clrmac", "" },
		{ 0xFFFF, 0x0038, 0, "ldtlb", "" },
		{ 0xFFFF, 0x0048, 0, "clrs", "" },
		{ 0xFFFF, 0x0058, 0, "sets", "" },
		{ 0xFFFF, 0x0009, 0, "nop", "" },
		{ 0xFFFF, 0x0019, 0, "div0u", "" },
		{ 0xF0FF, 0x0029, 0, "movt", "%n" },
		{ 0xF0FF, 0x000A, 0, "sts", "mach,%n" },
		{ 0xF0FF, 0x001A, 0, "sts", "macl,%n" },
		{ 0xF0FF, 0x002A, 0, "sts", "pr,%n" },
		{ 0xF0FF, 0x003A, 0, "stc", "sgr,%n" },
		{ 0xF0FF, 0x005A, 0, "sts", "fpul,%n" },
		{ 0xF0FF, 0x006A, 0, "sts", "fpscr,%n" },
		{ 0xF0FF, 0x00FA, 0, "stc", "dbr,%n" },
		{ 0xFFFF, 0x000B, SF_JUMP | SF_RETURN, "rts", "" },
		{ 0xFFFF, 0x001B, 0, "sleep", "" },
		{ 0xFFFF, 0x002B, SF_JUMP | SF_RETURN, "rte", "" },
		{ 0xF00F, 0x000C, 0, "mov.b", "@(r0,%m),%n" },
		{ 0xF00F, 0x000D, 0, "mov.w", "@(r0,%m),%n" },
		{ 0xF00F, 0x000E, 0, "mov.l", "@(r0,%m),%n" },
		{ 0xF00F, 0x000F, 0, "mac.l", "@%m+,@%n+" },

		{ 0xF000, 0x1000, 0, "mov.l", "%m,@(%4,%n)" },

		{ 0xF00F, 0x2000, 0, "mov.b", "%m,@%n" },
		{ 0xF00F, 0x2001, 0, "mov.w", "%m,@%n" },
		{ 0xF00F, 0x2002, 0, "mov.l", "%m,@%n" },
		{ 0xF00F, 0x2004, 0, "mov.b", "%m,@-%n" },
		{ 0xF00F, 0x2005, 0, "mov.w", "%m,@-%n" },
		{ 0xF00F, 0x2006, 0, "mov.l", "%m,@-%n" },
		{ 0xF00F, 0x2007, 0, "div0s", "%m,%n" },
		{ 0xF00F, 0x2008, 0, "tst", "%m,%n" },
		{ 0xF00F, 0x2009, 0, "and", "%m,%n" },
		{ 0xF00F, 0x200A, 0, "xor", "%m,%n" },
		{ 0xF00F, 0x200B, 0, "or", "%m,%n" },
		{ 0xF00F, 0x200C, 0, "cmp/str", "%m,%n" },
		{ 0xF00F, 0x200D, 0, "xtrct", "%m,%n" },
		{ 0xF00F, 0x200E, 0, "mulu.w", "%m,%n" },
		{ 0xF00F, 0x200F, 0, "muls.w", "%m,%n" },

		{ 0xF00F, 0x3000, 0, "cmp/eq", "%m,%n" },
		{ 0xF00F, 0x3002, 0, "cmp/hs", "%m,%n" },
		{ 0xF00F, 0x3003, 0, "cmp/ge", "%m,%n" },
		{ 0xF00F, 0x3004, 0, "div1", "%m,%n" },
		{ 0xF00F, 0x3005, 0, "dmulu.l", "%m,%n" },
		{ 0xF00F, 0x3006, 0, "cmp/hi", "%m,%n" },
		{ 0xF00F, 0x3007, 0, "cmp/gt", "%m,%n" },
		{ 0xF00F, 0x3008, 0, "sub", "%m,%n" },
		{ 0xF00F, 0x300A, 0, "subc", "%m,%n" },
		{ 0xF00F, 0x300B, 0, "subv", "%m,%n" },
		{ 0xF00F, 0x300C, 0, "add", "%m,%n" },
		{ 0xF00F, 0x300D, 0, "dmuls.l", "%m,%n" },
		{ 0xF00F, 0x300E, 0, "addc", "%m,%n" },
		{ 0xF00F, 0x300F, 0, "addv", "%m,%n" },

		{ 0xF0FF, 0x4000, 0, "shll", "%n" },
		{ 0xF0FF, 0x4010, 0, "dt", "%n" },
		{ 0xF0FF, 0x4020, 0, "shal", "%n" },
		{ 0xF0FF, 0x4001, 0, "shlr", "%n" },
		{ 0xF0FF, 0x4011, 0, "cmp/pz", "%n" },
		{ 0xF0FF, 0x4021, 0, "shar", "%n" },
		{ 0xF0FF, 0x4002, 0, "sts.l", "mach,@-%n" },
		{ 0xF0FF, 0x4012, 0, "sts.l", "macl,@-%n" },
		{ 0xF0FF, 0x4022, 0, "sts.l", "pr,@-%n" },
		{ 0xF0FF, 0x4032, 0, "stc.l", "sgr,@-%n" },
		{ 0xF0FF, 0x4052, 0, "sts.l", "fpul,@-%n" },
		{ 0xF0FF, 0x4062, 0, "sts.l", "fpscr,@-%n" },
		{ 0xF0FF, 0x40F2, 0, "stc.l", "dbr,@-%n" },
		{ 0xF0FF, 0x4003, 0, "stc.l", "sr,@-%n" },
		{ 0xF0FF, 0x4013, 0, "stc.l", "gbr,@-%n" },
		{ 0xF0FF, 0x4023, 0, "stc.l", "vbr,@-%n" },
		{ 0xF0FF, 0x4033, 0, "stc.l", "ssr,@-%n" },
		{ 0xF0FF, 0x4043, 0, "stc.l", "spc,@-%n" },
		{ 0xF08F, 0x4083, 0, "stc.l", "%b,@-%n" },
		{ 0xF0FF, 0x4004, 0, "rotl", "%n" },
		{ 0xF0FF, 0x4024, 0, "rotcl", "%n" },
		{ 0xF0FF, 0x4005, 0, "rotr", "%n" },
		{ 0xF0FF, 0x4015, 0, "cmp/pl", "%n" },
		{ 0xF0FF, 0x4025, 0, "rotcr", "%n" },
		{ 0xF0FF, 0x4006, 0, "lds.l", "@%n+,mach" },
		{ 0xF0FF, 0x4016, 0, "lds.l", "@%n+,macl" },
		{ 0xF0FF, 0x4026, 0, "lds.l", "@%n+,pr" },
		{ 0xF0FF, 0x4036, 0, "ldc.l", "@%n+,sgr" },
		{ 0xF0FF, 0x4056, 0, "lds.l", "@%n+,fpul" },
		{ 0xF0FF, 0x4066, 0, "lds.l", "@%n+,fpscr" },
		{ 0xF0FF, 0x40F6, 0, "ldc.l", "@%n+,dbr" },
		{ 0xF0FF, 0x4007, 0, "ldc.l", "@%n+,sr" },
		{ 0xF0FF, 0x4017, 0, "ldc.l", "@%n+,gbr" },
		{ 0xF0FF, 0x4027, 0, "ldc.l", "@%n+,vbr" },
		{ 0xF0FF, 0x4037, 0, "ldc.l", "@%n+,ssr" },
		{ 0xF0FF, 0x4047, 0, "ldc.l", "@%n+,spc" },
		{ 0xF08F, 0x4087, 0, "ldc.l", "@%n+,%b" },
		{ 0xF0FF, 0x4008, 0, "shll2", "%n" },
		{ 0xF0FF, 0x4018, 0, "shll8", "%n" },
		{ 0xF0FF, 0x4028, 0, "shll16", "%n" },
		{ 0xF0FF, 0x4009, 0, "shlr2", "%n" },
		{ 0xF0FF, 0x4019, 0, "shlr8", "%n" },
		{ 0xF0FF, 0x4029, 0, "shlr16", "%n" },
		{ 0xF0FF, 0x400A, 0, "lds", "%n,mach" },
		{ 0xF0FF, 0x401A, 0, "lds", "%n,macl" },
		{ 0xF0FF, 0x402A, 0, "lds", "%n,pr" },
		{ 0xF0FF, 0x403A, 0, "ldc", "%n,sgr" },
		{ 0xF0FF, 0x405A, 0, "lds", "%n,fpul" },
		{ 0xF0FF, 0x406A, 0, "lds", "%n,fpscr" },
		{ 0xF0FF, 0x40FA, 0, "ldc", "%n,dbr" },
		{ 0xF0FF, 0x400B, SF_JUMP | SF_CALL, "jsr", "@%n" },
		{ 0xF0FF, 0x401B, 0, "tas.b", "@%n" },
		{ 0xF0FF, 0x402B, SF_JUMP, "jmp", "@%n" },
		{ 0xF00F, 0x400C, 0, "shad", "%m,%n" },
		{ 0xF00F, 0x400D, 0, "shld", "%m,%n" },
		{ 0xF0FF, 0x400E, 0, "ldc", "%n,sr" },
		{ 0xF0FF, 0x401E, 0, "ldc", "%n,gbr" },
		{ 0xF0FF, 0x402E, 0, "ldc", "%n,vbr" },
		{ 0xF0FF, 0x403E, 0, "ldc", "%n,ssr" },
		{ 0xF0FF, 0x404E, 0, "ldc", "%n,spc" },
		{ 0xF08F, 0x408E, 0, "ldc", "%n,%b" },
		{ 0xF00F, 0x400F, 0, "mac.w", "@%m+,@%n+" },

		{ 0xF000, 0x5000, 0, "mov.l", "@(%4,%m),%n" },

		{ 0xF00F, 0x6000, 0, "mov.b", "@%m,%n" },
		{ 0xF00F, 0x6001, 0, "mov.w", "@%m,%n" },
		{ 0xF00F, 0x6002, 0, "mov.l", "@%m,%n" },
		{ 0xF00F, 0x6003, 0, "mov", "%m,%n" },
		{ 0xF00F, 0x6004, 0, "mov.b", "@%m+,%n" },
		{ 0xF00F, 0x6005, 0, "mov.w", "@%m+,%n" },
		{ 0xF00F, 0x6006, 0, "mov.l", "@%m+,%n" },
		{ 0xF00F, 0x6007, 0, "not", "%m,%n" },
		{ 0xF00F, 0x6008, 0, "swap.b", "%m,%n" },
		{ 0xF00F, 0x6009, 0, "swap.w", "%m,%n" },
		{ 0xF00F, 0x600A, 0, "negc", "%m,%n" },
		{ 0xF00F, 0x600B, 0, "neg", "%m,%n" },
		{ 0xF00F, 0x600C, 0, "extu.b", "%m,%n" },
		{ 0xF00F, 0x600D, 0, "extu.w", "%m,%n" },
		{ 0xF00F, 0x600E, 0, "exts.b", "%m,%n" },
		{ 0xF00F, 0x600F, 0, "exts.w", "%m,%n" },

		{ 0xF000, 0x7000, 0, "add", "%s,%n" },

		{ 0xFF00, 0x8000, 0, "mov.b", "r0,@(%1,%m)" },
		{ 0xFF00, 0x8100, 0, "mov.w", "r0,@(%2,%m)" },
		{ 0xFF00, 0x8400, 0, "mov.b", "@(%1,%m),r0" },
		{ 0xFF00, 0x8500, 0, "mov.w", "@(%2,%m),r0" },
		{ 0xFF00, 0x8800, 0, "cmp/eq", "%s,r0" },
		{ 0xFF00, 0x8900, SF_BRANCH | SF_COND, "bt", "%d" },
		{ 0xFF00, 0x8B00, SF_BRANCH | SF_COND, "bf", "%d" },
		{ 0xFF00, 0x8D00, SF_JUMP | SF_COND, "bt.s", "%d" },
		{ 0xFF00, 0x8F00, SF_JUMP | SF_COND, "bf.s", "%d" },

		{ 0xF000, 0x9000, 0, "mov.w", "%w,%n" },
		{ 0xF000, 0xA000, SF_JUMP, "bra", "%D" },
		{ 0xF000, 0xB000, SF_JUMP | SF_CALL, "bsr", "%D" },

		{ 0xFF00, 0xC000, 0, "mov.b", "r0,@(%B,gbr)" },
		{ 0xFF00, 0xC100, 0, "mov.w", "r0,@(%W,gbr)" },
		{ 0xFF00, 0xC200, 0, "mov.l", "r0,@(%L,gbr)" },
		{ 0xFF00, 0xC300, SF_TRAP, "trapa", "%i" },
		{ 0xFF00, 0xC400, 0, "mov.b", "@(%B,gbr),r0" },
		{ 0xFF00, 0xC500, 0, "mov.w", "@(%W,gbr),r0" },
		{ 0xFF00, 0xC600, 0, "mov.l", "@(%L,gbr),r0" },
		{ 0xFF00, 0xC700, 0, "mova", "%a,r0" },
		{ 0xFF00, 0xC800, 0, "tst", "%i,r0" },
		{ 0xFF00, 0xC900, 0, "and", "%i,r0" },
		{ 0xFF00, 0xCA00, 0, "xor", "%i,r0" },
		{ 0xFF00, 0xCB00, 0, "or", "%i,r0" },
		{ 0xFF00, 0xCC00, 0, "tst.b", "%i,@(r0,gbr)" },
		{ 0xFF00, 0xCD00, 0, "and.b", "%i,@(r0,gbr)" },
		{ 0xFF00, 0xCE00, 0, "xor.b", "%i,@(r0,gbr)" },
		{ 0xFF00, 0xCF00, 0, "or.b", "%i,@(r0,gbr)" },

		{ 0xF000, 0xD000, 0, "mov.l", "%l,%n" },
		{ 0xF000, 0xE000, 0, "mov", "%s,%n" },

		{ 0xF00F, 0xF000, 0, "fadd", "%x,%X" },
		{ 0xF00F, 0xF001, 0, "fsub", "%x,%X" },
		{ 0xF00F, 0xF002, 0, "fmul", "%x,%X" },
		{ 0xF00F, 0xF003, 0, "fdiv", "%x,%X" },
		{ 0xF00F, 0xF004, 0, "fcmp/eq", "%x,%X" },
		{ 0xF00F, 0xF005, 0, "fcmp/gt", "%x,%X" },
		{ 0xF00F, 0xF006, SF_FMOV_MEM, "fmov", "@(r0,%m),%Z" },
		{ 0xF00F, 0xF007, SF_FMOV_MEM, "fmov", "%z,@(r0,%n)" },
		{ 0xF00F, 0xF008, SF_FMOV_MEM, "fmov", "@%m,%Z" },
		{ 0xF00F, 0xF009, SF_FMOV_MEM, "fmov", "@%m+,%Z" },
		{ 0xF00F, 0xF00A, SF_FMOV_MEM, "fmov", "%z,@%n" },
		{ 0xF00F, 0xF00B, SF_FMOV_MEM, "fmov", "%z,@-%n" },
		{ 0xF00F, 0xF00C, 0, "fmov", "%z,%Z" },
		{ 0xF0FF, 0xF00D, 0, "fsts", "fpul,%F" },
		{ 0xF0FF, 0xF01D, 0, "flds", "%F,fpul" },
		{ 0xF0FF, 0xF02D, 0, "float", "fpul,%X" },
		{ 0xF0FF, 0xF03D, 0, "ftrc", "%X,fpul" },
		{ 0xF0FF, 0xF04D, 0, "fneg", "%X" },
		{ 0xF0FF, 0xF05D, 0, "fabs", "%X" },
		{ 0xF0FF, 0xF06D, 0, "fsqrt", "%X" },
		{ 0xF0FF, 0xF07D, 0, "fsrra", "%F" },
		{ 0xF0FF, 0xF08D, 0, "fldi0", "%F" },
		{ 0xF0FF, 0xF09D, 0, "fldi1", "%F" },
		{ 0xF0FF, 0xF0AD, 0, "fcnvsd", "fpul,%P" },
		{ 0xF0FF, 0xF0BD, 0, "fcnvds", "%P,fpul" },
		{ 0xF0FF, 0xF0ED, 0, "fipr", "%v,%V" },
		{ 0xF1FF, 0xF0FD, 0, "fsca", "fpul,%P" },
		{ 0xF3FF, 0xF1FD, 0, "ftrv", "xmtrx,%V" },
		{ 0xFFFF, 0xF3FD, 0, "fschg", "" },
		{ 0xFFFF, 0xFBFD, 0, "frchg", "" },
		{ 0xF00F, 0xF00E, 0, "fmac", "fr0,%f,%F" },
	};

	const Sh4Op* Sh4Find(uint16_t op)
	{
		for (const Sh4Op& d : sh4_ops)
		{
			if ((op & d.mask) == d.bits)
				return &d;
		}
		return 0;
	}

	//FPSCR.SZ = 1 register pair: bit 0 of the field selects the XD bank
	void Sh4PairReg(std::string& s, uint32_t r)
	{
		Appendf(s, "%s%u", (r & 1) ? "xd" : "dr", r & 14);
	}

	void Sh4Operands(std::string& s, const Sh4Op& d, uint32_t pc, uint16_t op, unsigned fp_mode,
		const DisasmCtx* ctx, Sh4InsnInfo& info)
	{
		const uint32_t n = (op >> 8) & 15;
		const uint32_t m = (op >> 4) & 15;
		const uint32_t disp4 = op & 15;
		const uint32_t disp8 = op & 0xFF;
		const bool pr = (fp_mode & SH4_FP_PR) != 0;
		const bool sz = (fp_mode & SH4_FP_SZ) != 0;
		std::string comment;

		for (const char* p = d.args; *p; p++)
		{
			if (*p != '%')
			{
				s += *p;
				continue;
			}

			switch (*++p)
			{
			case 'n': Appendf(s, "r%u", n); break;
			case 'm': Appendf(s, "r%u", m); break;
			case 'b': Appendf(s, "r%u_bank", m & 7); break;

			case 'i': Appendf(s, "#%u", disp8); break;
			case 's': Appendf(s, "#%d", (int)(int8_t)disp8); break;

			case '1': Appendf(s, "%u", disp4); break;
			case '2': Appendf(s, "%u", disp4 * 2); break;
			case '4': Appendf(s, "%u", disp4 * 4); break;
			case 'B': Appendf(s, "%u", disp8); break;
			case 'W': Appendf(s, "%u", disp8 * 2); break;
			case 'L': Appendf(s, "%u", disp8 * 4); break;

			case 'w':
				{
					uint32_t v = 0;
					info.has_literal = true;
					info.literal_addr = pc + 4 + disp8 * 2;
					info.literal_size = 2;
					Appendf(s, "0x%08x", info.literal_addr);
					if (ReadGuest(ctx, info.literal_addr, 2, &v))
						Appendf(comment, "0x%08x", (uint32_t)(int32_t)(int16_t)v);
				}
				break;

			case 'l':
				{
					uint32_t v = 0;
					info.has_literal = true;
					info.literal_addr = (pc & ~3u) + 4 + disp8 * 4;
					info.literal_size = 4;
					Appendf(s, "0x%08x", info.literal_addr);
					if (ReadGuest(ctx, info.literal_addr, 4, &v))
					{
						Appendf(comment, "0x%08x", v);
						AppendSymbol(comment, ctx, v);
					}
				}
				break;

			case 'a':
				info.has_literal = true;
				info.literal_addr = (pc & ~3u) + 4 + disp8 * 4;
				info.literal_size = 0;
				Appendf(s, "0x%08x", info.literal_addr);
				break;

			case 'd':
			case 'D':
				{
					//sign extend 8 or 12 bits
					const uint32_t sign = *p == 'd' ? 0x80 : 0x800;
					const uint32_t disp = ((op & (sign * 2 - 1)) ^ sign) - sign;

					info.has_target = true;
					info.target = pc + 4 + disp * 2;
					Appendf(s, "0x%08x", info.target);
					AppendSymbol(s, ctx, info.target);
				}
				break;

			case 'F': Appendf(s, "fr%u", n); break;
			case 'f': Appendf(s, "fr%u", m); break;

			case 'X': Appendf(s, pr ? "dr%u" : "fr%u", pr ? (n & 14) : n); break;
			case 'x': Appendf(s, pr ? "dr%u" : "fr%u", pr ? (m & 14) : m); break;

			case 'Z':
				if (sz)
					Sh4PairReg(s, n);
				else
					Appendf(s, "fr%u", n);
				break;

			case 'z':
				if (sz)
					Sh4PairReg(s, m);
				else
					Appendf(s, "fr%u", m);
				break;

			case 'P': Appendf(s, "dr%u", n & 14); break;
			case 'V': Appendf(s, "fv%u", n & 12); break;
			case 'v': Appendf(s, "fv%u", (n & 3) * 4); break;
			}
		}

		if (!comment.empty())
		{
			s += " ! ";
			s += comment;
		}
	}

	/*
		ARM7DI
	*/
	const char* const arm_cond[16] =
	{
		"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "nv"
	};

	const char* const arm_reg[16] =
	{
		"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc"
	};

	const char* const arm_dp[16] =
	{
		"and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc", "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn"
	};

	const char* const arm_shift[4] = { "lsl", "lsr", "asr", "ror" };

	enum
	{
		ARM_SUB = 2,
		ARM_ADD = 4,
		ARM_MOV = 13,
		ARM_MVN = 15,
	};

	const uint32_t ARM_SP = 13;
	const uint32_t ARM_LR = 14;
	const uint32_t ARM_PC = 15;

	//Numbers below 10 in decimal, the rest in hex
	void ArmNum(std::string& s, uint32_t v)
	{
		Appendf(s, v < 10 ? "%u" : "0x%x", v);
	}

	//Shift by immediate, bits 11:5. Amount 0 means: no shift (lsl), 32 (lsr, asr), rrx (ror)
	void ArmShiftImm(std::string& s, uint32_t op)
	{
		const uint32_t type = (op >> 5) & 3;
		const uint32_t amount = (op >> 7) & 31;

		if (amount)
			Appendf(s, ", %s #%u", arm_shift[type], amount);
		else if (type == 3)
			s += ", rrx";
		else if (type != 0)
			Appendf(s, ", %s #32", arm_shift[type]);
	}

	//8-bit immediate rotated right by twice bits 11:8
	uint32_t ArmRotValue(uint32_t op)
	{
		const uint32_t rot = (op >> 7) & 30;
		const uint32_t imm8 = op & 0xFF;

		return rot ? (imm8 >> rot) | (imm8 << (32 - rot)) : imm8;
	}

	void ArmRotImm(std::string& s, uint32_t op)
	{
		const uint32_t rot = (op >> 7) & 30;
		const uint32_t v = ArmRotValue(op);
		uint32_t smallest;

		//The rotation also selects the shifter carry out, so an encoding that does not use the smallest
		//rotation for its value is shown as the pair it is
		for (smallest = 0; smallest < 32; smallest += 2)
		{
			if ((smallest ? (v << smallest) | (v >> (32 - smallest)) : v) <= 0xFF)
				break;
		}

		s += '#';
		if (smallest == rot)
		{
			ArmNum(s, v);
		}
		else
		{
			ArmNum(s, op & 0xFF);
			Appendf(s, ", %u", rot);
		}
	}

	//mrs / msr: the data processing space with opcode 10xx and S clear
	bool ArmPsr(uint32_t op, std::string& mn, std::string& args)
	{
		const char* psr = (op & (1 << 22)) ? "SPSR" : "CPSR";

		if (!(op & (1 << 21)))
		{
			if ((op & 0x0FBF0FFF) != 0x010F0000)
				return false;

			mn = "mrs";
			args = arm_reg[(op >> 12) & 15];
			args += ", ";
			args += psr;
			return true;
		}

		const bool imm = (op & (1 << 25)) != 0;

		if (imm ? (op & 0x0FB0F000) != 0x0320F000 : (op & 0x0FB0FFF0) != 0x0120F000)
			return false;

		mn = "msr";
		args = psr;
		args += '_';
		if (op & (1 << 19)) args += 'f';
		if (op & (1 << 18)) args += 's';
		if (op & (1 << 17)) args += 'x';
		if (op & (1 << 16)) args += 'c';
		args += ", ";
		if (imm)
			ArmRotImm(args, op);
		else
			args += arm_reg[op & 15];
		return true;
	}

	void ArmDataProc(uint32_t pc, uint32_t op, std::string& mn, std::string& args, std::string& comment, ArmInsnInfo& info)
	{
		const uint32_t opcode = (op >> 21) & 15;
		const bool imm = (op & (1 << 25)) != 0;
		const bool s = (op & (1 << 20)) != 0;
		const uint32_t rn = (op >> 16) & 15;
		const uint32_t rd = (op >> 12) & 15;
		const bool compare = (opcode & 12) == 8;	//tst teq cmp cmn

		mn = arm_dp[opcode];
		if (compare)
		{
			//Rd = 15 is the 26-bit "P" form: in a 32-bit mode it copies SPSR to CPSR
			if (rd == ARM_PC)
				mn += 'p';
			args = arm_reg[rn];
		}
		else
		{
			if (s)
				mn += 's';
			args = arm_reg[rd];
			if (opcode != ARM_MOV && opcode != ARM_MVN)
			{
				args += ", ";
				args += arm_reg[rn];
			}
		}

		args += ", ";
		if (imm)
		{
			ArmRotImm(args, op);
			if (rn == ARM_PC && (opcode == ARM_ADD || opcode == ARM_SUB))
			{
				const uint32_t v = ArmRotValue(op);

				info.has_literal = true;
				info.literal_addr = opcode == ARM_ADD ? pc + 8 + v : pc + 8 - v;
				info.literal_size = 0;
				Appendf(comment, "0x%08x", info.literal_addr);
			}
		}
		else
		{
			args += arm_reg[op & 15];
			if (op & (1 << 4))
				Appendf(args, ", %s %s", arm_shift[(op >> 5) & 3], arm_reg[(op >> 8) & 15]);
			else
				ArmShiftImm(args, op);
		}

		if (!compare && rd == ARM_PC)
		{
			info.is_branch = true;
			if (opcode == ARM_MOV && !imm && (op & 0xFFF) == ARM_LR)
				info.is_return = true;
			if (opcode == ARM_SUB && s && imm && rn == ARM_LR)
				info.is_return = true;
		}
	}

	//ldr / str
	void ArmSingleTransfer(uint32_t pc, uint32_t op, const DisasmCtx* ctx, std::string& mn, std::string& args,
		std::string& comment, ArmInsnInfo& info)
	{
		const bool reg = (op & (1 << 25)) != 0;
		const bool pre = (op & (1 << 24)) != 0;
		const bool up = (op & (1 << 23)) != 0;
		const bool byte = (op & (1 << 22)) != 0;
		const bool wb = (op & (1 << 21)) != 0;
		const bool load = (op & (1 << 20)) != 0;
		const uint32_t rn = (op >> 16) & 15;
		const uint32_t rd = (op >> 12) & 15;
		std::string offset;

		mn = load ? "ldr" : "str";
		if (byte)
			mn += 'b';
		if (!pre && wb)
			mn += 't';

		if (reg)
		{
			if (!up)
				offset += '-';
			offset += arm_reg[op & 15];
			ArmShiftImm(offset, op);
		}
		else if ((op & 0xFFF) || !up || !pre || wb)
		{
			offset += up ? "#" : "#-";
			ArmNum(offset, op & 0xFFF);
		}

		args = arm_reg[rd];
		args += ", [";
		args += arm_reg[rn];
		if (pre)
		{
			if (!offset.empty())
			{
				args += ", ";
				args += offset;
			}
			args += wb ? "]!" : "]";
		}
		else
		{
			args += "], ";
			args += offset;
		}

		if (rn == ARM_PC && pre && !reg)
		{
			uint32_t v = 0;

			info.has_literal = true;
			info.literal_addr = up ? pc + 8 + (op & 0xFFF) : pc + 8 - (op & 0xFFF);
			info.literal_size = byte ? 1 : 4;
			Appendf(comment, "0x%08x", info.literal_addr);
			if (load && ReadGuest(ctx, info.literal_addr, info.literal_size, &v))
			{
				if (byte)
				{
					Appendf(comment, " = 0x%02x", v & 0xFF);
				}
				else
				{
					Appendf(comment, " = 0x%08x", v);
					AppendSymbol(comment, ctx, v);
				}
			}
		}

		if (load && rd == ARM_PC)
		{
			info.is_branch = true;
			info.is_return = rn == ARM_SP;
		}
	}

	//ldm / stm
	void ArmBlockTransfer(uint32_t op, std::string& mn, std::string& args, ArmInsnInfo& info)
	{
		static const char* const mode[4] = { "da", "ia", "db", "ib" };
		const bool load = (op & (1 << 20)) != 0;
		const uint32_t rn = (op >> 16) & 15;
		bool first = true;

		mn = load ? "ldm" : "stm";
		mn += mode[(op >> 23) & 3];

		args = arm_reg[rn];
		if (op & (1 << 21))
			args += '!';
		args += ", {";
		for (uint32_t r = 0; r < 16; r++)
		{
			if (!(op & (1u << r)))
				continue;
			if (!first)
				args += ", ";
			args += arm_reg[r];
			first = false;
		}
		args += '}';
		if (op & (1 << 22))
			args += '^';

		if (load && (op & (1 << ARM_PC)))
		{
			info.is_branch = true;
			info.is_return = rn == ARM_SP;
		}
	}

	//ldc / stc
	void ArmCoprocTransfer(uint32_t op, std::string& mn, std::string& args)
	{
		const bool pre = (op & (1 << 24)) != 0;
		const bool up = (op & (1 << 23)) != 0;
		const bool wb = (op & (1 << 21)) != 0;
		const uint32_t rn = (op >> 16) & 15;
		const uint32_t imm8 = op & 0xFF;

		mn = (op & (1 << 20)) ? "ldc" : "stc";
		if (op & (1 << 22))
			mn += 'l';

		Appendf(args, "p%u, c%u, [%s", (op >> 8) & 15, (op >> 12) & 15, arm_reg[rn]);
		if (pre)
		{
			if (imm8 || !up || wb)
			{
				args += up ? ", #" : ", #-";
				ArmNum(args, imm8 * 4);
			}
			args += wb ? "]!" : "]";
		}
		else if (wb)
		{
			args += up ? "], #" : "], #-";
			ArmNum(args, imm8 * 4);
		}
		else
		{
			//unindexed: the low byte is an option passed to the coprocessor
			Appendf(args, "], {%u}", imm8);
		}
	}

	bool ArmDecode(uint32_t pc, uint32_t op, const DisasmCtx* ctx, std::string& mn, std::string& args,
		std::string& comment, ArmInsnInfo& info)
	{
		switch ((op >> 25) & 7)
		{
		case 0:
			if ((op & 0x90) == 0x90)
			{
				//Bits 7 and 4 set: multiply / swap space. Only mul, mla and swp exist on the ARM7DI
				if ((op & 0x0FC000F0) == 0x00000090)
				{
					const bool acc = (op & (1 << 21)) != 0;

					mn = acc ? "mla" : "mul";
					if (op & (1 << 20))
						mn += 's';
					Appendf(args, "%s, %s, %s", arm_reg[(op >> 16) & 15], arm_reg[op & 15], arm_reg[(op >> 8) & 15]);
					if (acc)
					{
						args += ", ";
						args += arm_reg[(op >> 12) & 15];
					}
					return true;
				}

				if ((op & 0x0FB00FF0) == 0x01000090)
				{
					mn = (op & (1 << 22)) ? "swpb" : "swp";
					Appendf(args, "%s, %s, [%s]", arm_reg[(op >> 12) & 15], arm_reg[op & 15], arm_reg[(op >> 16) & 15]);
					return true;
				}

				return false;
			}
			[[fallthrough]];
		case 1:
			if ((op & 0x01900000) == 0x01000000)
				return ArmPsr(op, mn, args);

			ArmDataProc(pc, op, mn, args, comment, info);
			return true;

		case 3:
			if (op & (1 << 4))
				return false;	//undefined instruction trap
			[[fallthrough]];
		case 2:
			ArmSingleTransfer(pc, op, ctx, mn, args, comment, info);
			return true;

		case 4:
			ArmBlockTransfer(op, mn, args, info);
			return true;

		case 5:
			{
				//sign extend 24 bits, in words
				const uint32_t disp = ((op & 0x00FFFFFF) ^ 0x00800000) - 0x00800000;

				mn = (op & (1 << 24)) ? "bl" : "b";
				info.is_branch = true;
				info.is_call = (op & (1 << 24)) != 0;
				info.has_target = true;
				info.target = pc + 8 + disp * 4;
				Appendf(args, "0x%08x", info.target);
				AppendSymbol(args, ctx, info.target);
			}
			return true;

		case 6:
			ArmCoprocTransfer(op, mn, args);
			return true;

		default:
			if (op & (1 << 24))
			{
				mn = "swi";
				info.is_trap = true;
				Appendf(args, "0x%06x", op & 0x00FFFFFF);
			}
			else if (op & (1 << 4))
			{
				mn = (op & (1 << 20)) ? "mrc" : "mcr";
				Appendf(args, "p%u, %u, %s, c%u, c%u, %u", (op >> 8) & 15, (op >> 21) & 7, arm_reg[(op >> 12) & 15],
					(op >> 16) & 15, op & 15, (op >> 5) & 7);
			}
			else
			{
				mn = "cdp";
				Appendf(args, "p%u, %u, c%u, c%u, c%u, %u", (op >> 8) & 15, (op >> 20) & 15, (op >> 12) & 15,
					(op >> 16) & 15, op & 15, (op >> 5) & 7);
			}
			return true;
		}
	}

	/*
		AICA DSP
	*/
	struct DspField
	{
		const char* name;
		uint8_t word;
		uint8_t shift;
		uint8_t bits;
	};

	//In the order the playground's disassembler lists them
	const DspField dsp_fields[] =
	{
		{ "TRA", 0, 9, 7 },
		{ "TWT", 0, 8, 1 },
		{ "TWA", 0, 1, 7 },

		{ "XSEL", 1, 15, 1 },
		{ "YSEL", 1, 13, 2 },
		{ "IRA", 1, 7, 6 },
		{ "IWT", 1, 6, 1 },
		{ "IWA", 1, 1, 5 },

		{ "TABLE", 2, 15, 1 },
		{ "MWT", 2, 14, 1 },
		{ "MRD", 2, 13, 1 },
		{ "EWT", 2, 12, 1 },
		{ "EWA", 2, 8, 4 },
		{ "ADRL", 2, 7, 1 },
		{ "FRCL", 2, 6, 1 },
		{ "SHIFT", 2, 4, 2 },
		{ "YRL", 2, 3, 1 },
		{ "NEGB", 2, 2, 1 },
		{ "ZERO", 2, 1, 1 },
		{ "BSEL", 2, 0, 1 },

		{ "NOFL", 3, 15, 1 },
		{ "MASA", 3, 9, 6 },
		{ "ADREB", 3, 8, 1 },
		{ "NXADR", 3, 7, 1 },
	};
}

	std::string DisasmSh4(uint32_t pc, uint16_t op, unsigned fp_mode, const DisasmCtx* ctx, Sh4InsnInfo* info)
	{
		Sh4InsnInfo local;
		Sh4InsnInfo& i = info ? *info : local;
		const Sh4Op* d = Sh4Find(op);
		std::string s;

		i = Sh4InsnInfo();
		i.length = 2;

		if (!d)
		{
			Appendf(s, ".word 0x%04x", op);
			return s;
		}

		i.valid = true;
		i.is_branch = (d->flags & SF_BRANCH) != 0;
		i.is_delayed_branch = (d->flags & SF_DELAY) != 0;
		i.is_call = (d->flags & SF_CALL) != 0;
		i.is_return = (d->flags & SF_RETURN) != 0;
		i.is_conditional = (d->flags & SF_COND) != 0;
		i.is_trap = (d->flags & SF_TRAP) != 0;

		s = d->name;
		if ((d->flags & SF_FMOV_MEM) && !(fp_mode & SH4_FP_SZ))
			s += ".s";

		if (d->args[0])
		{
			PadMnemonic(s);
			Sh4Operands(s, *d, pc, op, fp_mode, ctx, i);
		}

		return s;
	}

	std::string DisasmArm7(uint32_t pc, uint32_t op, const DisasmCtx* ctx, ArmInsnInfo* info)
	{
		ArmInsnInfo local;
		ArmInsnInfo& i = info ? *info : local;
		std::string s, args, comment;

		i = ArmInsnInfo();
		i.length = 4;

		if (!ArmDecode(pc, op, ctx, s, args, comment, i))
		{
			i = ArmInsnInfo();
			i.length = 4;
			s.clear();
			Appendf(s, ".word 0x%08x", op);
			return s;
		}

		i.valid = true;
		i.is_conditional = (op >> 28) != 14;

		s += arm_cond[op >> 28];
		PadMnemonic(s);
		s += args;
		if (!comment.empty())
		{
			s += " @ ";
			s += comment;
		}

		return s;
	}

	std::string DisasmDsp(unsigned step, const uint16_t mpro[4])
	{
		std::string s;
		bool any = false;

		Appendf(s, "MPRO[%u] =", step);
		for (const DspField& f : dsp_fields)
		{
			const unsigned v = (mpro[f.word] >> f.shift) & ((1u << f.bits) - 1);

			if (!v)
				continue;

			s += ' ';
			s += f.name;
			if (f.bits != 1)
				Appendf(s, ":%u", v);
			any = true;
		}

		//The assembler rejects a line without a field
		if (!any)
			s += " TRA:0";

		return s;
	}
}
