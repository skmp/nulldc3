/*
	This file is part of libswirl
*/
#include "license/bsd"


/*
	The selftest of the store queues with the MMU off: what the guest writes into SQ0 and SQ1 and sends with a pref
	is in main memory, those bytes and no others. The write of a pref there is the store queue function of the
	context (Sh4RCB::do_sqw_nommu, which a write of QACR0 or QACR1 selects: do_sqw_nommu_area_3 for area 3 with the
	host's layout of guest memory), called by the blocks of the recompiler for their pref; the interpreter's pref
	writes through the memory map. No guest program of the other tests writes main memory through the store queues.

	    nulldc3 --config config:Headless=yes , audio:backend=none , Debugger:SelfTest=sqw tests/debugger/selftest/emu_guest.bin

	One guest test, sqw.nommu. At the start stop it writes its program into main memory and sets the pc on it; the
	boot of the session's program does not run. The program (below) sets QACR0 and QACR1 to area 3, writes sixteen
	longs into the store queues, 32 bytes into SQ0 and 32 into SQ1, and sends each with a pref, to 0x8C040000 and
	0x8C040020; then it writes 1 to a flag and spins. A scheduler callback of the test asks for a pause once the flag
	is set. At the pause the test compares the 64 bytes with the values written, never with another engine's run,
	and the 32 bytes in front of them and the 96 behind them with what it put there. Then it calls the store queue
	function of the context itself, as the blocks of the recompiler call it (x0 the address, x1 the store queues),
	for SQ0 and SQ1 with values of its own, to 0x8C040040 and 0x8C040060: the same compare. So on the interpreter as
	well a function that writes nothing, or writes elsewhere, fails the test.

	On the recompiler the test also requires that the guest ran on the recompiler's own loop (no SH4 breakpoint is
	armed, so nothing selects the interpreter fallback).

	The program, at 0x8C030000 (sh-elf-as --little --isa=sh4):

	    _start: mov.l   qacr0, r0       ! 0xFF000038, QACR0; QACR1 is behind it
	            mov     #0x0C, r1       ! area 3 (bits 4:2)
	            mov.l   r1, @r0
	            mov.l   r1, @(4,r0)
	            mov.l   sqdst, r2       ! 0xE0040000: SQ0, for 0x0C040000
	            mov.l   pattern, r3
	            mov.l   step, r4
	            mov     #16, r5
	            mov     r2, r6
	    1:      mov.l   r3, @r6         ! the sixteen longs of SQ0 and SQ1
	            add     r4, r3
	            dt      r5
	            bf/s    1b
	            add     #4, r6
	            pref    @r2             ! SQ0 to 0x0C040000
	            mov     r2, r7
	            add     #32, r7
	            pref    @r7             ! SQ1 to 0x0C040020
	            mov.l   done, r8
	            mov     #1, r9
	            mov.l   r9, @r8         ! the flag
	    2:      bra     2b
	            nop
	            .align 2
	    qacr0:  .long   0xFF000038
	    sqdst:  .long   0xE0040000
	    pattern: .long  0x5A010203
	    step:   .long   0x01010101
	    done:   .long   0x8C03F000
*/

#include "dbg_selftest.h"
#include "dbg_sh4.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_sched.h"

#include <cstring>

namespace {

using namespace dbg;
using selftest::Check;
using selftest::Note;

const u32 kCode = 0x8C030000;
const u32 kFlag = 0x8C03F000;
const u32 kTarget = 0x8C040000;		// the 64 bytes of the prefs
const u32 kDirect = 0x8C040040;		// the 64 bytes of the test's own calls
const u32 kBefore = 32;				// bytes in front of kTarget that nothing writes
const u32 kAfter = 64;				// bytes behind kDirect + 64 that nothing writes
const u8 kFill = 0xA5;

const u32 kPattern = 0x5A010203;
const u32 kStep = 0x01010101;
const u32 kOwnPattern = 0xC3D4E5F6;	// the test's own values for the calls, with this step
const u32 kOwnStep = 0x11111111;

const u16 kProgram[] = {
	0xd00b, 0xe10c, 0x2012, 0x1011, 0xd20a, 0xd30b, 0xd40b, 0xe510,
	0x6623, 0x2632, 0x334c, 0x4510, 0x8ffb, 0x7604, 0x0283, 0x6723,
	0x7720, 0x0783, 0xd806, 0xe901, 0x2892, 0xaffe, 0x0009, 0x0009,
	0x0038, 0xff00, 0x0000, 0xe004, 0x0203, 0x5a01, 0x0101, 0x0101,
	0xf000, 0x8c03,
};

struct {
	int phase = 0;
	int callback = -1;
	bool asked = false;
} sq;

// Every slice: once the guest has set its flag, a pause
int FlagTick(void*, int, int, int)
{
	if (!sq.asked && ReadMem32(kFlag) == 1)
	{
		sq.asked = true;
		emu::RequestPause();
	}

	return 1;
}

// How many of n bytes at addr are not fill
u32 Touched(u32 addr, u32 n)
{
	u32 count = 0;

	for (u32 i = 0; i < n; i++)
		count += ReadMem8(addr + i) != kFill;

	return count;
}

// How many of the 16 longs at addr are not first, first + step, ...
u32 Differ(u32 addr, u32 first, u32 step)
{
	u32 count = 0;

	for (u32 i = 0; i < 16; i++)
		count += ReadMem32(addr + i * 4) != first + i * step;

	return count;
}

}

DBG_SELFTEST_GUEST(sqw_nommu, "sqw.nommu")
{
	if (sq.phase == 0)
	{
		// the program, the flag, and fill around and in the bytes the store queues write
		for (u32 i = 0; i < sizeof(kProgram) / sizeof(kProgram[0]); i++)
			WriteMem16(kCode + i * 2, kProgram[i]);

		WriteMem32(kFlag, 0);

		for (u32 a = kTarget - kBefore; a < kDirect + 64 + kAfter; a++)
			WriteMem8(a, kFill);

		// compiled code of what was there
		sh4_cpu->ResetCache();

		if (sq.callback < 0)
			sq.callback = sh4_sched_register(nullptr, 0, FlagTick);
		sh4_sched_request(sq.callback, 1);

		Sh4cntx.pc = kCode;
		sh4::OnPcWritten();

		Note("the guest at %08X: QACR0 and QACR1 to area 3, 32 bytes through SQ0 and 32 through SQ1, a pref for each (the stop was \"%s\")",
			kCode, StopCauseName(stop.cause));

		sq.phase = 1;

		int rv = emu::Resume();

		if (rv != 0)
		{
			Check(false, "a resume was refused: %d", rv);
			return true;
		}

		return false;
	}

	emu::Sh4EngineInfo info = { };
	emu::GetSh4Engine(&info);

	const bool recompiler = strcmp(info.backend, "interpreter") != 0;

	Check(stop.cause == StopCause::Pause && ReadMem32(kFlag) == 1, "the guest ran its program to the end and was paused there (\"%s\", the flag %u, pc %08X)",
		StopCauseName(stop.cause), ReadMem32(kFlag), Sh4cntx.pc);

	if (recompiler)
		Check(!info.fallbackLoop, "the guest ran on the recompiler (%s) and its own loop, not on the interpreter fallback", info.backend);

	// the guest's prefs
	Check(Differ(kTarget, kPattern, kStep) == 0, "the pref of SQ0 and the pref of SQ1 wrote the 64 bytes the guest put into them at %08X (%u of 16 longs differ: %08X %08X ... %08X)",
		kTarget, Differ(kTarget, kPattern, kStep), ReadMem32(kTarget), ReadMem32(kTarget + 4), ReadMem32(kTarget + 60));

	Check(Touched(kTarget - kBefore, kBefore) == 0 && Touched(kDirect, 64 + kAfter) == 0,
		"and nothing else: the %u bytes in front of them and the %u behind them are as they were (%u and %u bytes were written)",
		kBefore, 64 + kAfter, Touched(kTarget - kBefore, kBefore), Touched(kDirect, 64 + kAfter));

	// the store queue function of the context, called as the blocks call it (do_sqw_nommu is sh4rcb's, sh4_if.h)
	sqw_fp* fn = do_sqw_nommu;
	const char* which = fn == &do_sqw_nommu_area_3 ? "do_sqw_nommu_area_3" : fn == &do_sqw_nommu_area_3_nonvmem ? "do_sqw_nommu_area_3_nonvmem" : "another function";

	u32* sqb = (u32*)p_sh4rcb->sq_buffer;

	for (u32 i = 0; i < 16; i++)
		sqb[i] = kOwnPattern + i * kOwnStep;

	fn(0xE0000000 | (kDirect & 0x03FFFFE0), (u8*)p_sh4rcb->sq_buffer);
	fn(0xE0000000 | ((kDirect + 32) & 0x03FFFFE0), (u8*)p_sh4rcb->sq_buffer);

	Check(fn == &do_sqw_nommu_area_3 || fn == &do_sqw_nommu_area_3_nonvmem, "QACR0 and QACR1 of area 3 select the store queue function of area 3 (%s)", which);

	Check(Differ(kDirect, kOwnPattern, kOwnStep) == 0, "%s, called for SQ0 and SQ1 as the recompiler's blocks call it, wrote their 64 bytes at %08X (%u of 16 longs differ: %08X ... %08X)",
		which, kDirect, Differ(kDirect, kOwnPattern, kOwnStep), ReadMem32(kDirect), ReadMem32(kDirect + 60));

	Check(Differ(kTarget, kPattern, kStep) == 0 && Touched(kTarget - kBefore, kBefore) == 0 && Touched(kDirect + 64, kAfter) == 0,
		"and nothing else: the guest's 64 bytes, the %u in front of them and the %u behind are as they were", kBefore, kAfter);

	return true;
}
