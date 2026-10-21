/*
	Test driver for libswirl/debugger/dbg_disasm.

	disasm_test golden          golden tables + invariants that need no external tool
	disasm_test sh4 MODE BASE   every 16-bit opcode, laid out as an image at BASE, FPSCR mode MODE (0..3)
	disasm_test arm BASE        opcodes from stdin (one hex word per line), laid out as an image at BASE
	disasm_test dsp             "step w0 w1 w2 w3" (decimal step, hex words) per line from stdin
	disasm_test rows            prints golden table rows for the cases on stdin, to be reviewed and pasted
	                            into golden_*.inc: "sh4 PC OP MODE CTX", "arm PC OP CTX" (hex, CTX 0 or 1),
	                            "dsp STEP W0 W1 W2 W3"

	The dump modes print one line per instruction: opcode, info signature, text, tab separated.
	Literal loads are resolved against the image itself, the way objdump does for a binary file.
*/
#include "dbg_disasm.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace dbg;

namespace
{
	int failures;

	void Fail(const char* what, const std::string& detail)
	{
		if (++failures <= 40)
			fprintf(stderr, "FAIL %s: %s\n", what, detail.c_str());
	}

	std::string Hex(uint32_t v, int digits)
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%0*x", digits, v);
		return buf;
	}

	/*
		Info signature: one letter per flag in a fixed order, '-' when clear, then the addresses.
		v valid, b branch, d delayed, c call, r return, ? conditional, t trap
		" t=<target>" " l=<literal address>/<size>"
	*/
	std::string Signature(bool valid, bool branch, bool delayed, bool call, bool ret, bool cond, bool trap,
		bool has_target, uint32_t target, bool has_literal, uint32_t literal, uint32_t size)
	{
		std::string s;
		s += valid ? 'v' : '-';
		s += branch ? 'b' : '-';
		s += delayed ? 'd' : '-';
		s += call ? 'c' : '-';
		s += ret ? 'r' : '-';
		s += cond ? '?' : '-';
		s += trap ? 't' : '-';
		if (has_target)
			s += " t=" + Hex(target, 8);
		if (has_literal)
			s += " l=" + Hex(literal, 8) + "/" + std::to_string(size);
		return s;
	}

	std::string Signature(const Sh4InsnInfo& i)
	{
		return Signature(i.valid, i.is_branch, i.is_delayed_branch, i.is_call, i.is_return, i.is_conditional,
			i.is_trap, i.has_target, i.target, i.has_literal, i.literal_addr, i.literal_size);
	}

	std::string Signature(const ArmInsnInfo& i)
	{
		return Signature(i.valid, i.is_branch, false, i.is_call, i.is_return, i.is_conditional,
			i.is_trap, i.has_target, i.target, i.has_literal, i.literal_addr, i.literal_size);
	}

	/*
		Golden context: a symbol "_func" covering 0x8c010200..0x8c0102ff, "arm_func" covering
		0x00001200..0x000012ff, and a memory that returns a value derived from the address:
		4 bytes -> 0x8c010200 + (addr & 0xff), 1 byte -> addr & 0xff,
		2 bytes -> 0x1200 | (addr & 0xff), or 0x8000 | (addr & 0xff) when bit 8 of addr is set.
		Addresses with bit 28 set are unreadable.
	*/
	bool GoldenSymbol(void*, uint32_t addr, const char** name, uint32_t* offset)
	{
		if (addr >= 0x8c010200 && addr < 0x8c010300)
		{
			*name = "_func";
			*offset = addr - 0x8c010200;
			return true;
		}
		if (addr >= 0x1200 && addr < 0x1300)
		{
			*name = "arm_func";
			*offset = addr - 0x1200;
			return true;
		}
		return false;
	}

	bool GoldenRead(void*, uint32_t addr, uint32_t size, uint32_t* value)
	{
		if (addr & 0x10000000)
			return false;
		if (size == 4)
			*value = 0x8c010200 + (addr & 0xff);
		else if (size == 2)
			*value = ((addr & 0x100) ? 0x8000 : 0x1200) | (addr & 0xff);
		else
			*value = addr & 0xff;
		return true;
	}

	const DisasmCtx golden_ctx = { GoldenSymbol, GoldenRead, 0 };

	struct Sh4Golden
	{
		uint32_t pc;
		uint16_t op;
		unsigned fp_mode;
		bool ctx;
		const char* text;
		const char* info;
	};

	struct ArmGolden
	{
		uint32_t pc;
		uint32_t op;
		bool ctx;
		const char* text;
		const char* info;
	};

	struct DspGolden
	{
		unsigned step;
		uint16_t mpro[4];
		const char* text;
	};

	const Sh4Golden sh4_golden[] =
	{
#include "golden_sh4.inc"
	};

	const ArmGolden arm_golden[] =
	{
#include "golden_arm7.inc"
	};

	const DspGolden dsp_golden[] =
	{
#include "golden_dsp.inc"
	};

	bool StartsWith(const std::string& s, const char* prefix)
	{
		return s.compare(0, strlen(prefix), prefix) == 0;
	}

	//Collapses runs of spaces; the mnemonic column padding is not part of what is compared
	std::string Squeeze(const std::string& s)
	{
		std::string r;
		for (char c : s)
		{
			if (c == ' ' && !r.empty() && r.back() == ' ')
				continue;
			r += c;
		}
		return r;
	}

	void CheckText(const char* what, const std::string& id, const std::string& text)
	{
		if (text.empty() || text.back() == ' ' || text.front() == ' ')
			Fail(what, id + ": empty text or stray space: '" + text + "'");
		for (char c : text)
		{
			if (c < 0x20 || c > 0x7e)
				Fail(what, id + ": non printable character");
		}
	}

	void GoldenSh4()
	{
		for (const Sh4Golden& g : sh4_golden)
		{
			Sh4InsnInfo info;
			const std::string text = DisasmSh4(g.pc, g.op, g.fp_mode, g.ctx ? &golden_ctx : 0, &info);
			const std::string id = Hex(g.op, 4) + " mode " + std::to_string(g.fp_mode);

			if (text != g.text)
				Fail("sh4 golden text", id + ": got '" + text + "' want '" + g.text + "'");
			if (Signature(info) != g.info)
				Fail("sh4 golden info", id + ": got '" + Signature(info) + "' want '" + g.info + "'");
		}
	}

	void InvariantsSh4()
	{
		unsigned valid_count = 0;

		for (uint32_t op = 0; op < 0x10000; op++)
		{
			Sh4InsnInfo base;
			const std::string base_text = DisasmSh4(0x8c010000, (uint16_t)op, 0, 0, &base);
			const std::string id = Hex(op, 4);

			valid_count += base.valid;

			if (base.valid == StartsWith(base_text, ".word"))
				Fail("sh4 valid vs text", id);
			if (!base.valid && base_text != ".word 0x" + Hex(op, 4))
				Fail("sh4 invalid text", id + ": " + base_text);
			if (base.length != 2)
				Fail("sh4 length", id);
			if (!base.valid && Signature(base) != "-------")
				Fail("sh4 flags on an invalid encoding", id);
			if ((base.is_delayed_branch || base.is_call || base.is_return || base.is_conditional || base.has_target)
				&& !base.is_branch)
				Fail("sh4 branch flags", id);
			if (base.is_trap && base.is_branch)
				Fail("sh4 trap flagged as branch", id);

			for (unsigned mode = 0; mode < 4; mode++)
			{
				Sh4InsnInfo info;
				const std::string text = DisasmSh4(0x8c010000, (uint16_t)op, mode, 0, &info);

				CheckText("sh4 text", id, text);

				//info never depends on the FPU mode; text only for FPU instructions
				if (Signature(info) != Signature(base))
					Fail("sh4 info depends on fp_mode", id);
				if ((op >> 12) != 15 && text != base_text)
					Fail("sh4 non-FPU text depends on fp_mode", id);

				//info is optional, and a context without callbacks is the same as none
				const DisasmCtx empty = { 0, 0, 0 };
				if (DisasmSh4(0x8c010000, (uint16_t)op, mode, 0, 0) != text
					|| DisasmSh4(0x8c010000, (uint16_t)op, mode, &empty, 0) != text)
					Fail("sh4 null info / empty ctx", id);
			}

			//PC-relative operands follow pc; everything else is position independent
			Sh4InsnInfo moved;
			const std::string moved_text = DisasmSh4(0x0c000102, (uint16_t)op, 0, 0, &moved);
			if (!base.has_target && !base.has_literal && moved_text != base_text)
				Fail("sh4 text depends on pc", id);
			if (base.has_target && moved.target - base.target != 0x0c000102u - 0x8c010000u)
				Fail("sh4 target does not follow pc", id);
			if (base.has_literal && base.literal_size == 2 && moved.literal_addr - base.literal_addr != 0x0c000102u - 0x8c010000u)
				Fail("sh4 word literal does not follow pc", id);
			if (base.has_literal && base.literal_size != 2 && moved.literal_addr - base.literal_addr != 0x0c000100u - 0x8c010000u)
				Fail("sh4 long literal is not aligned", id);
		}

		//59050 encodings in the interpreter table, minus the HLE trap
		if (valid_count != 59049)
			Fail("sh4 valid encodings", std::to_string(valid_count));
	}

	void GoldenArm()
	{
		for (const ArmGolden& g : arm_golden)
		{
			ArmInsnInfo info;
			const std::string text = DisasmArm7(g.pc, g.op, g.ctx ? &golden_ctx : 0, &info);
			const std::string id = Hex(g.op, 8);

			if (text != g.text)
				Fail("arm golden text", id + ": got '" + text + "' want '" + g.text + "'");
			if (Signature(info) != g.info)
				Fail("arm golden info", id + ": got '" + Signature(info) + "' want '" + g.info + "'");
		}
	}

	uint32_t rng_state = 0x2545F491;

	uint32_t Rng()
	{
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 17;
		rng_state ^= rng_state << 5;
		return rng_state;
	}

	void InvariantsArm()
	{
		static const char* const cond[16] =
		{
			"eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "", "nv"
		};

		for (unsigned n = 0; n < 1000000; n++)
		{
			const uint32_t op = Rng();
			const uint32_t al = (op & 0x0FFFFFFF) | 0xE0000000;
			const std::string id = Hex(op, 8);
			ArmInsnInfo info, al_info;
			const std::string text = DisasmArm7(0x00001000, op, 0, &info);
			const std::string al_text = DisasmArm7(0x00001000, al, 0, &al_info);

			CheckText("arm text", id, text);

			if (info.valid == StartsWith(text, ".word"))
				Fail("arm valid vs text", id);
			if (!info.valid && text != ".word 0x" + Hex(op, 8))
				Fail("arm invalid text", id + ": " + text);
			if (info.length != 4)
				Fail("arm length", id);
			if (!info.valid && Signature(info) != "-------")
				Fail("arm flags on an invalid encoding", id);
			if ((info.is_call || info.is_return || info.has_target) && !info.is_branch)
				Fail("arm branch flags", id);
			if (info.valid && info.is_conditional != ((op >> 28) != 14))
				Fail("arm conditional flag", id);

			const DisasmCtx empty = { 0, 0, 0 };
			if (DisasmArm7(0x00001000, op, 0, 0) != text || DisasmArm7(0x00001000, op, &empty, 0) != text)
				Fail("arm null info / empty ctx", id);

			//The condition only adds its suffix to the mnemonic: validity, operands and every other flag
			//are those of the same instruction with condition AL
			if (info.valid != al_info.valid)
			{
				Fail("arm validity depends on the condition", id);
				continue;
			}
			if (!info.valid)
				continue;

			al_info.is_conditional = info.is_conditional;
			if (Signature(info) != Signature(al_info))
				Fail("arm info depends on the condition", id);

			const size_t al_split = al_text.find(' ');
			const size_t split = text.find(' ');
			if (al_split == std::string::npos || split == std::string::npos)
			{
				Fail("arm text without operands", id);
				continue;
			}
			if (text.substr(0, split) != al_text.substr(0, al_split) + cond[op >> 28]
				|| Squeeze(text.substr(split)) != Squeeze(al_text.substr(al_split)))
				Fail("arm condition suffix", id + ": '" + text + "' vs '" + al_text + "'");

			//Operands start at column 8 unless the mnemonic is too long
			if (split < 8 && (text.size() < 9 || text[7] != ' ' || text[8] == ' '))
				Fail("arm operand column", id + ": '" + text + "'");
		}
	}

	struct DspFieldDef
	{
		const char* name;
		int word, shift, bits;
	};

	//The playground assembler's field table (dspUtils.ts encodeInst)
	const DspFieldDef dsp_fields[] =
	{
		{ "TRA", 0, 9, 7 }, { "TWT", 0, 8, 1 }, { "TWA", 0, 1, 7 },
		{ "XSEL", 1, 15, 1 }, { "YSEL", 1, 13, 2 }, { "IRA", 1, 7, 6 }, { "IWT", 1, 6, 1 }, { "IWA", 1, 1, 5 },
		{ "TABLE", 2, 15, 1 }, { "MWT", 2, 14, 1 }, { "MRD", 2, 13, 1 }, { "EWT", 2, 12, 1 }, { "EWA", 2, 8, 4 },
		{ "ADRL", 2, 7, 1 }, { "FRCL", 2, 6, 1 }, { "SHIFT", 2, 4, 2 }, { "YRL", 2, 3, 1 }, { "NEGB", 2, 2, 1 },
		{ "ZERO", 2, 1, 1 }, { "BSEL", 2, 0, 1 },
		{ "NOFL", 3, 15, 1 }, { "MASA", 3, 9, 6 }, { "ADREB", 3, 8, 1 }, { "NXADR", 3, 7, 1 },
	};

	//Assembles "MPRO[n] = NAME[:value] ..." the way the playground does. Returns false on a syntax error.
	bool DspAssemble(const std::string& line, unsigned* step, uint16_t mpro[4])
	{
		int consumed = 0;

		mpro[0] = mpro[1] = mpro[2] = mpro[3] = 0;
		if (sscanf(line.c_str(), "MPRO[%u] = %n", step, &consumed) != 1 || consumed == 0)
			return false;

		const char* p = line.c_str() + consumed;
		if (!*p)
			return false;

		while (*p)
		{
			const char* end = p;
			while (*end && *end != ' ')
				end++;

			const std::string token(p, end);
			const size_t colon = token.find(':');
			const std::string name = token.substr(0, colon);
			unsigned value = 1;

			if (colon != std::string::npos)
			{
				char* stop;
				value = (unsigned)strtoul(token.c_str() + colon + 1, &stop, 10);
				if (*stop || stop == token.c_str() + colon + 1)
					return false;
			}

			const DspFieldDef* field = 0;
			for (const DspFieldDef& f : dsp_fields)
			{
				if (name == f.name)
					field = &f;
			}
			if (!field || value >= (1u << field->bits))
				return false;

			mpro[field->word] = (uint16_t)(mpro[field->word] | (value << field->shift));

			p = end;
			while (*p == ' ')
				p++;
		}
		return true;
	}

	void GoldenDsp()
	{
		for (const DspGolden& g : dsp_golden)
		{
			const std::string text = DisasmDsp(g.step, g.mpro);
			if (text != g.text)
				Fail("dsp golden", "got '" + text + "' want '" + g.text + "'");
		}
	}

	void InvariantsDsp()
	{
		//Bits the playground's decodeInst reads
		static const uint16_t field_mask[4] = { 0xFFFE, 0xFFFE, 0xFFFF, 0xFF80 };
		uint16_t mask[4] = { 0, 0, 0, 0 };

		for (const DspFieldDef& f : dsp_fields)
			mask[f.word] = (uint16_t)(mask[f.word] | (((1u << f.bits) - 1) << f.shift));
		for (int w = 0; w < 4; w++)
		{
			if (mask[w] != field_mask[w])
				Fail("dsp field table", "word " + std::to_string(w) + " mask " + Hex(mask[w], 4));
		}

		for (unsigned n = 0; n < 200000; n++)
		{
			uint16_t mpro[4], back[4];
			unsigned step = Rng() & 127, back_step = ~0u;

			for (int w = 0; w < 4; w++)
			{
				//A third of the words sparse, so that short lines and the empty step are exercised
				const uint32_t r = Rng();
				mpro[w] = (uint16_t)(n % 3 == 0 ? r & Rng() & Rng() & Rng() : r);
			}
			if (n < 128)
			{
				step = n;
				if (n < 4)
					mpro[0] = mpro[1] = mpro[2] = mpro[3] = 0;
			}

			const std::string text = DisasmDsp(step, mpro);
			const std::string id = Hex(mpro[0], 4) + " " + Hex(mpro[1], 4) + " " + Hex(mpro[2], 4) + " " + Hex(mpro[3], 4);

			CheckText("dsp text", id, text);
			if (!DspAssemble(text, &back_step, back))
			{
				Fail("dsp line does not assemble", id + ": " + text);
				continue;
			}
			if (back_step != step)
				Fail("dsp step", id + ": " + text);
			for (int w = 0; w < 4; w++)
			{
				if (back[w] != (mpro[w] & mask[w]))
					Fail("dsp round trip", id + ": " + text);
			}
		}
	}

	/*
		Dump modes
	*/
	struct Image
	{
		uint32_t base;
		std::vector<uint8_t> bytes;
	};

	bool ImageRead(void* user, uint32_t addr, uint32_t size, uint32_t* value)
	{
		const Image* image = (const Image*)user;
		const uint32_t offset = addr - image->base;

		if (offset >= image->bytes.size() || size > image->bytes.size() - offset)
			return false;

		*value = 0;
		for (uint32_t b = 0; b < size; b++)
			*value |= (uint32_t)image->bytes[offset + b] << (8 * b);
		return true;
	}

	int DumpSh4(unsigned mode, uint32_t base)
	{
		Image image;
		image.base = base;
		for (uint32_t op = 0; op < 0x10000; op++)
		{
			image.bytes.push_back((uint8_t)op);
			image.bytes.push_back((uint8_t)(op >> 8));
		}

		const DisasmCtx ctx = { 0, ImageRead, &image };
		for (uint32_t op = 0; op < 0x10000; op++)
		{
			Sh4InsnInfo info;
			const std::string text = DisasmSh4(base + op * 2, (uint16_t)op, mode, &ctx, &info);
			printf("%04x\t%s\t%s\n", op, Signature(info).c_str(), text.c_str());
		}
		return 0;
	}

	int DumpArm(uint32_t base)
	{
		Image image;
		std::vector<uint32_t> ops;
		char line[64];

		image.base = base;
		while (fgets(line, sizeof(line), stdin))
		{
			const uint32_t op = (uint32_t)strtoul(line, 0, 16);
			ops.push_back(op);
			for (int b = 0; b < 4; b++)
				image.bytes.push_back((uint8_t)(op >> (8 * b)));
		}

		const DisasmCtx ctx = { 0, ImageRead, &image };
		for (size_t n = 0; n < ops.size(); n++)
		{
			ArmInsnInfo info;
			const std::string text = DisasmArm7(base + (uint32_t)n * 4, ops[n], &ctx, &info);
			printf("%08x\t%s\t%s\n", ops[n], Signature(info).c_str(), text.c_str());
		}
		return 0;
	}

	int DumpDsp()
	{
		char line[128];

		while (fgets(line, sizeof(line), stdin))
		{
			unsigned step, w[4];
			if (sscanf(line, "%u %x %x %x %x", &step, &w[0], &w[1], &w[2], &w[3]) != 5)
				continue;

			const uint16_t mpro[4] = { (uint16_t)w[0], (uint16_t)w[1], (uint16_t)w[2], (uint16_t)w[3] };
			printf("%s\n", DisasmDsp(step, mpro).c_str());
		}
		return 0;
	}

	int Rows()
	{
		char line[128];

		while (fgets(line, sizeof(line), stdin))
		{
			unsigned pc, op, mode, ctx, step, w[4];

			if (sscanf(line, "sh4 %x %x %u %u", &pc, &op, &mode, &ctx) == 4)
			{
				Sh4InsnInfo info;
				const std::string text = DisasmSh4(pc, (uint16_t)op, mode, ctx ? &golden_ctx : 0, &info);
				printf("\t\t{ 0x%08x, 0x%04x, %u, %s, \"%s\", \"%s\" },\n", pc, op, mode, ctx ? "true" : "false",
					text.c_str(), Signature(info).c_str());
			}
			else if (sscanf(line, "arm %x %x %u", &pc, &op, &ctx) == 3)
			{
				ArmInsnInfo info;
				const std::string text = DisasmArm7(pc, op, ctx ? &golden_ctx : 0, &info);
				printf("\t\t{ 0x%08x, 0x%08x, %s, \"%s\", \"%s\" },\n", pc, op, ctx ? "true" : "false",
					text.c_str(), Signature(info).c_str());
			}
			else if (sscanf(line, "dsp %u %x %x %x %x", &step, &w[0], &w[1], &w[2], &w[3]) == 5)
			{
				const uint16_t mpro[4] = { (uint16_t)w[0], (uint16_t)w[1], (uint16_t)w[2], (uint16_t)w[3] };
				printf("\t\t{ %u, { 0x%04x, 0x%04x, 0x%04x, 0x%04x }, \"%s\" },\n", step, w[0], w[1], w[2], w[3],
					DisasmDsp(step, mpro).c_str());
			}
			else
			{
				printf("%s", line);
			}
		}
		return 0;
	}
}

int main(int argc, char** argv)
{
#ifdef _WIN32
	// the dump modes write their lines as Linux does, each ending in \n alone: a stdout in text mode would end every
	// one in \r\n, which dsp_playground.mts does not take (debugger.disasm.dsp_playground)
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	const std::string mode = argc > 1 ? argv[1] : "golden";

	if (mode == "sh4" && argc == 4)
		return DumpSh4((unsigned)strtoul(argv[2], 0, 0), (uint32_t)strtoul(argv[3], 0, 0));
	if (mode == "arm" && argc == 3)
		return DumpArm((uint32_t)strtoul(argv[2], 0, 0));
	if (mode == "dsp" && argc == 2)
		return DumpDsp();
	if (mode == "rows" && argc == 2)
		return Rows();
	if (mode != "golden" || argc > 2)
	{
		fprintf(stderr, "usage: %s [golden | sh4 MODE BASE | arm BASE | dsp | rows]\n", argv[0]);
		return 2;
	}

	GoldenSh4();
	InvariantsSh4();
	GoldenArm();
	InvariantsArm();
	GoldenDsp();
	InvariantsDsp();

	if (failures)
	{
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}

	printf("golden: %u sh4, %u arm7, %u dsp entries; invariants: 65536 sh4 opcodes x 4 FPU modes, "
		"1000000 arm7 words, 200000 dsp steps\n",
		(unsigned)(sizeof(sh4_golden) / sizeof(sh4_golden[0])),
		(unsigned)(sizeof(arm_golden) / sizeof(arm_golden[0])),
		(unsigned)(sizeof(dsp_golden) / sizeof(dsp_golden[0])));
	return 0;
}
