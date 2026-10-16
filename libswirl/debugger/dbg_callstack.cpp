/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_callstack.h"
#include "dbg_mem.h"

#include "hw/arm7/SoundCPU.h"
#include "hw/sh4/sh4_if.h"

#include <vector>

/*
	The unwinders. Both are guesses beyond the link register (dbg_callstack.h) and both only read: the
	registers from the SH4 context and through the debug accessors of the sound CPU, memory through dbg::mem,
	which calls no guest handler. Nothing is kept between two calls.
*/

namespace dbg { namespace callstack {

namespace {

// One read of the words above the stack pointer, with what of it is readable
struct StackWords {
	std::vector<u8> bytes;
	std::vector<u8> readable;

	StackWords(Target t, u32 sp) : bytes(kScanWords * 4), readable((kScanWords * 4 + 7) / 8)
	{
		mem::Read(t, sp, kScanWords * 4, bytes.data(), readable.data());
	}

	// The word at index i, false when one of its bytes cannot be read
	bool Word(u32 i, u32* value) const
	{
		const u32 at = i * 4;

		for (u32 b = at; b < at + 4; b++)
		{
			if (!(readable[b >> 3] & (1 << (b & 7))))
				return false;
		}

		*value = (u32)bytes[at] | ((u32)bytes[at + 1] << 8) | ((u32)bytes[at + 2] << 16) | ((u32)bytes[at + 3] << 24);
		return true;
	}
};

// How many words the scan looks at. A stack ends where its memory ends: above a stack pointer in main memory
// (the SH4) or in wave memory (the ARM7) the words up to the end of that memory are looked at, not the start
// of the memory, which its mirror has behind the end
u32 ScanWords(Target t, u32 sp)
{
	const bool sh4 = t == Target::Sh4;
	const mem::Region home = sh4 ? mem::Region::Ram : mem::Region::AicaRam;
	const u32 size = sh4 ? sh4_cpu->mram.size : sh4_cpu->aica_ram.size;

	if ((sh4 && sp >= 0xE0000000) || mem::Classify(t, sp) != home || size == 0 || (size & (size - 1)) != 0)
		return kScanWords;

	const u32 left = (size - (sp & (size - 1))) / 4;

	return left < kScanWords ? left : kScanWords;
}

// Does the SH4 execute from there? Main memory or the boot ROM, in any of their mirrors
bool Sh4Executable(u32 addr)
{
	if (addr >= 0xE0000000)
		return false;

	mem::Region region = mem::Classify(Target::Sh4, addr);

	return region == mem::Region::Ram || region == mem::Region::Bios;
}

// BSR, BSRF, JSR
bool Sh4IsCall(u16 op)
{
	return (op & 0xF000) == 0xB000 || (op & 0xF0FF) == 0x0003 || (op & 0xF0FF) == 0x400B;
}

u32 UnwindSh4(Frame* out, u32 maxFrames)
{
	if (p_sh4rcb == nullptr)
		return 0;

	const Sh4Context& ctx = p_sh4rcb->cntx;
	const u32 sp = ctx.r[15];
	u32 n = 0;

	out[n++] = { ctx.pc, sp, 0, FrameKind::Pc };

	if (n < maxFrames)
		out[n++] = { ctx._pr, sp, 0, FrameKind::Link };

	if (n >= maxFrames)
		return n;

	StackWords stack(Target::Sh4, sp);
	const u32 words = ScanWords(Target::Sh4, sp);

	for (u32 i = 0; i < words && n < maxFrames; i++)
	{
		u32 value;
		u16 op;

		if (!stack.Word(i, &value))
			continue;

		// a return address: even, in memory the SH4 executes from, behind a call (which has a delay slot)
		if ((value & 1) != 0 || !Sh4Executable(value) || !Sh4Executable(value - 4))
			continue;

		if (!mem::Read16(Target::Sh4, value - 4, &op) || !Sh4IsCall(op))
			continue;

		out[n++] = { value, sp, i * 4, FrameKind::Stack };
	}

	return n;
}

// BL, on any condition but "never"
bool Arm7IsCall(u32 op)
{
	return (op & 0x0F000000) == 0x0B000000 && (op >> 28) != 0xF;
}

u32 UnwindArm7(Frame* out, u32 maxFrames)
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);

	// held in reset: nothing executes, there is no stack to speak of
	if (scpu == nullptr || !scpu->DebugEnabled())
		return 0;

	u32 pc, sp, lr;

	if (!scpu->DebugGetReg(15, &pc) || !scpu->DebugGetReg(13, &sp) || !scpu->DebugGetReg(14, &lr))
		return 0;

	u32 n = 0;

	out[n++] = { pc, sp, 0, FrameKind::Pc };

	if (n < maxFrames)
		out[n++] = { lr, sp, 0, FrameKind::Link };

	if (n >= maxFrames)
		return n;

	StackWords stack(Target::Arm7, sp);
	const u32 words = ScanWords(Target::Arm7, sp);

	for (u32 i = 0; i < words && n < maxFrames; i++)
	{
		u32 value, op;

		if (!stack.Word(i, &value))
			continue;

		// a return address: a multiple of 4 in wave memory, behind a BL
		if ((value & 3) != 0 || value < 4 || mem::Classify(Target::Arm7, value) != mem::Region::AicaRam)
			continue;

		if (!mem::Read32(Target::Arm7, value - 4, &op) || !Arm7IsCall(op))
			continue;

		out[n++] = { value, sp, i * 4, FrameKind::Stack };
	}

	return n;
}

}

u32 Unwind(Target t, Frame* out, u32 maxFrames)
{
	if (maxFrames == 0 || out == nullptr)
		return 0;

	if (maxFrames > kMaxCallstackFrames)
		maxFrames = kMaxCallstackFrames;

	// no machine: the published state says so before the machine is taken apart
	if (sh4_cpu == nullptr || emu::GetRunState().lifetime == Lifetime::NoMachine)
		return 0;

	switch (t)
	{
	case Target::Sh4:  return UnwindSh4(out, maxFrames);
	case Target::Arm7: return UnwindArm7(out, maxFrames);
	case Target::Dsp:  break;
	}

	return 0;
}

} }
