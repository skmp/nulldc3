/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_regs.h"
#include "dbg_regs_private.h"

#include "dbg_arm7.h"
#include "dbg_emu.h"
#include "dbg_sh4.h"

#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7_context.h"
#include "hw/gdrom/gdromv3.h"
#include "hw/holly/sb.h"
#include "hw/pvr/pvr_regs.h"
#include "hw/sh4/sh4_dbg.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/sh4_sched.h"
#include "version.h"

#include <cstdio>
#include <cstring>

/*
	The register readers. Every value comes from a backing store or from a reader written for the debugger: an
	array of register slots, the SH4 context, a debug accessor of a device. No read function of a register and
	no guest memory handler is called from here, so a read changes nothing and cannot end the process.

	The register writers, behind the readers of their source: the registers of the two processors, and no
	other. A write is a store into the context of its processor together with what the processor itself does
	when it loads the register (dbg_regs.h has the list); no write function of a register is called. The
	registers of the devices have no writer.

	The machine is looked up on every call (sh4_cpu and its devices); nothing is kept between two calls.
*/

// The timer unit keeps its down counters as a base and the scheduler's time (hw/sh4/modules/tmu.cpp)
extern u32 tmu_shift[3];
extern u32 tmu_mask[3];
extern u32 tmu_ch_base[3];

namespace dbg { namespace regs {

// The selectors of the ARM7 rows are the indices of arm7_context.h
#define SAME(selector, index) ((u32)(selector) == (u32)(index))
static_assert(SAME(kArm7Cpsr, RN_CPSR) && SAME(kArm7Spsr, RN_SPSR), "ARM7 selectors");
static_assert(SAME(kArm7R13Irq, R13_IRQ) && SAME(kArm7R14Irq, R14_IRQ) && SAME(kArm7SpsrIrq, SPSR_IRQ), "ARM7 selectors");
static_assert(SAME(kArm7R13Usr, R13_USR) && SAME(kArm7R14Usr, R14_USR), "ARM7 selectors");
static_assert(SAME(kArm7R13Svc, R13_SVC) && SAME(kArm7R14Svc, R14_SVC) && SAME(kArm7SpsrSvc, SPSR_SVC), "ARM7 selectors");
static_assert(SAME(kArm7R13Abt, R13_ABT) && SAME(kArm7R14Abt, R14_ABT) && SAME(kArm7SpsrAbt, SPSR_ABT), "ARM7 selectors");
static_assert(SAME(kArm7R13Und, R13_UND) && SAME(kArm7R14Und, R14_UND) && SAME(kArm7SpsrUnd, SPSR_UND), "ARM7 selectors");
static_assert(SAME(kArm7R8Fiq, R8_FIQ) && SAME(kArm7R8Fiq + 6, R14_FIQ) && SAME(kArm7SpsrFiq, SPSR_FIQ), "ARM7 selectors");
#undef SAME

// ... but for the one that is no register of the processor
static_assert((u32)kArm7Enabled >= (u32)RN_ARM_REG_COUNT, "kArm7Enabled is an index of arm7_context.h");

static u64 WidthMask(u32 width)
{
	return width >= 64 ? ~(u64)0 : ((u64)1 << width) - 1;
}

/*
	SH4 context
*/
static u32 Sh4Raw(f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static bool ReadSh4(u32 selector, u64* value)
{
	if (p_sh4rcb == nullptr)
		return false;

	const Sh4Context& ctx = p_sh4rcb->cntx;

	if (selector >= kSh4R0 && selector < kSh4R0 + 16)
		*value = ctx.r[selector - kSh4R0];
	else if (selector >= kSh4R0Bank && selector < kSh4R0Bank + 8)
		*value = ctx.r_bank[selector - kSh4R0Bank];
	else if (selector >= kSh4Fr0 && selector < kSh4Fr0 + 16)
		*value = Sh4Raw(ctx.xffr[16 + selector - kSh4Fr0]);
	else if (selector >= kSh4Xf0 && selector < kSh4Xf0 + 16)
		*value = Sh4Raw(ctx.xffr[selector - kSh4Xf0]);
	else
	{
		switch (selector)
		{
		case kSh4Pc:    *value = ctx.pc; break;
		case kSh4Pr:    *value = ctx._pr; break;
		case kSh4Sr:    *value = sh4_sr_GetFull(); break;
		case kSh4Gbr:   *value = ctx.gbr; break;
		case kSh4Vbr:   *value = ctx.vbr; break;
		case kSh4Mach:  *value = ctx.mac.h; break;
		case kSh4Macl:  *value = ctx.mac.l; break;
		case kSh4Ssr:   *value = ctx.ssr; break;
		case kSh4Spc:   *value = ctx.spc; break;
		case kSh4Sgr:   *value = ctx.sgr; break;
		case kSh4Dbr:   *value = ctx.dbr; break;
		case kSh4Fpscr: *value = ctx.fpscr.full; break;
		case kSh4Fpul:  *value = ctx.fpul; break;
		default:
			return false;
		}
	}

	return true;
}

// PR, SZ and the rounding mode: what a block of the recompiler has in its code from the FPSCR of the moment it
// was compiled (bits 19, 20 and the field RM)
static u32 Sh4FpMode(u32 fpscr_value)
{
	fpscr_t fpscr;

	fpscr.full = fpscr_value;

	return (fpscr.PR << 3) | (fpscr.SZ << 2) | fpscr.RM;
}

// A store into the SH4 context, with what the SH4 does when it loads the register. Returns 0, or kErrNotWritable,
// having changed nothing, for a value that is refused
static int WriteSh4(u32 selector, u32 value)
{
	if (p_sh4rcb == nullptr)
		return kErrNotWritable;

	Sh4Context& ctx = p_sh4rcb->cntx;

	if (selector >= kSh4R0 && selector < kSh4R0 + 16)
		ctx.r[selector - kSh4R0] = value;
	else if (selector >= kSh4R0Bank && selector < kSh4R0Bank + 8)
		ctx.r_bank[selector - kSh4R0Bank] = value;
	else if (selector >= kSh4Fr0 && selector < kSh4Fr0 + 16)
		memcpy(&ctx.xffr[16 + selector - kSh4Fr0], &value, sizeof(value));
	else if (selector >= kSh4Xf0 && selector < kSh4Xf0 + 16)
		memcpy(&ctx.xffr[selector - kSh4Xf0], &value, sizeof(value));
	else
	{
		switch (selector)
		{
		case kSh4Pc:
		{
			// nothing executes at an odd address
			if (value & 1)
				return kErrNotWritable;

			const u32 replaced = ctx.pc;

			ctx.pc = value;
			sh4::OnPcWritten();

			// A block is found by its address under a mask, with no address in the key: the slot of the new PC,
			// or of an address its code goes on to, can hold the block of another address. With the cache empty
			// every block is compiled at the address the guest reaches it by
			if (value != replaced)
				sh4_cpu->ResetCache();
			break;
		}

		case kSh4Pr:    ctx._pr = value; break;

		case kSh4Sr:
			// the banks of R0..R7 follow MD and RB; with MD clear RB is stored as 0
			sh4_sr_SetFull(value);

			// An interrupt is pending and the SR as it is now lets it in. In the park context the latch is set
			// and nothing is accepted here: the acceptance is owed, and made when the context is left
			if (UpdateSR())
				UpdateINTC();
			break;

		case kSh4Gbr:   ctx.gbr = value; break;
		case kSh4Vbr:   ctx.vbr = value; break;
		case kSh4Mach:  ctx.mac.h = value; break;
		case kSh4Macl:  ctx.mac.l = value; break;
		case kSh4Ssr:   ctx.ssr = value; break;
		case kSh4Spc:   ctx.spc = value; break;
		case kSh4Sgr:   ctx.sgr = value; break;
		case kSh4Dbr:   ctx.dbr = value; break;

		case kSh4Fpscr:
		{
			const u32 replaced = ctx.fpscr.full;

			// the FP banks follow FR, the host FP mode of this thread the rounding and denormal mode
			ctx.fpscr.full = value & FPSCR_MASK;
			UpdateFPSCR();

			// a compiled block has the FP mode of the moment it was compiled, and is found by its address alone
			if (Sh4FpMode(ctx.fpscr.full) != Sh4FpMode(replaced))
				sh4_cpu->ResetCache();
			break;
		}

		case kSh4Fpul:  ctx.fpul = value; break;

		default:
			return kErrNotWritable;
		}
	}

	return 0;
}

/*
	SH4 on-chip modules
*/

// The slot of the register at a P4 or area 7 address, null when the address is in no module's array. The
// decode is the one of the area 7 handler: a module is 64 KiB of address space, of which the first words are
// its registers.
static const RegisterStruct* MmrSlot(u32 addr)
{
	SuperH4Mmr* mmr = sh4_cpu->sh4mmr.get();

	if (mmr == nullptr)
		return nullptr;

	addr &= 0x1FFFFFFF;

	if ((addr & 0xFF03) != 0)
		return nullptr;

	const RegisterStruct* slots;
	size_t count;

	switch (A7_REG_HASH(addr))
	{
	case A7_REG_HASH(CCN_BASE_addr):  slots = mmr->CCN.data();  count = mmr->CCN.size();  break;
	case A7_REG_HASH(UBC_BASE_addr):  slots = mmr->UBC.data();  count = mmr->UBC.size();  break;
	case A7_REG_HASH(BSC_BASE_addr):  slots = mmr->BSC.data();  count = mmr->BSC.size();  break;
	case A7_REG_HASH(DMAC_BASE_addr): slots = mmr->DMAC.data(); count = mmr->DMAC.size(); break;
	case A7_REG_HASH(CPG_BASE_addr):  slots = mmr->CPG.data();  count = mmr->CPG.size();  break;
	case A7_REG_HASH(RTC_BASE_addr):  slots = mmr->RTC.data();  count = mmr->RTC.size();  break;
	case A7_REG_HASH(INTC_BASE_addr): slots = mmr->INTC.data(); count = mmr->INTC.size(); break;
	case A7_REG_HASH(TMU_BASE_addr):  slots = mmr->TMU.data();  count = mmr->TMU.size();  break;
	case A7_REG_HASH(SCI_BASE_addr):  slots = mmr->SCI.data();  count = mmr->SCI.size();  break;
	case A7_REG_HASH(SCIF_BASE_addr): slots = mmr->SCIF.data(); count = mmr->SCIF.size(); break;
	default:
		return nullptr;
	}

	u32 index = (addr & 0xFF) / 4;

	return index < count ? &slots[index] : nullptr;
}

static bool ReadMmr(const Register& reg, u64* value)
{
	u32 selector = KeySelector(reg.key);

	switch (selector)
	{
	case kMmrData:
	{
		const RegisterStruct* slot = MmrSlot(reg.addr);

		// a slot that holds a read function holds no value
		if (slot == nullptr || (slot->flags & REG_RF))
			return false;

		*value = slot->data32;
		return true;
	}

	case kMmrZero:
		*value = 0;
		return true;

	case kMmrCpuVersion:
		*value = 0x040205c1;
		return true;

	case kMmrTmuTcnt0:
	case kMmrTmuTcnt1:
	case kMmrTmuTcnt2:
	{
		u32 ch = selector - kMmrTmuTcnt0;

		*value = (u32)(tmu_ch_base[ch] - ((sh4_sched_now64() >> tmu_shift[ch]) & tmu_mask[ch]));
		return true;
	}
	}

	return false;
}

/*
	Devices
*/
static bool ReadSb(const Register& reg, u64* value)
{
	u32 word;

	if (!ReadSbRegisterAt(reg.addr, &word))
		return false;

	*value = word;
	return true;
}

static bool ReadArm7(u32 selector, u64* value)
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	u32 word;

	if (scpu == nullptr)
		return false;

	// Is the ARM7 clocked, or held in reset? What the unwinder of the ARM7 call stack asks as well. It reads
	// wherever the registers of the processor read: the sound CPU keeps them while it is held in reset too
	if (selector == kArm7Enabled)
	{
		*value = scpu->DebugEnabled() ? 1 : 0;
		return true;
	}

	if (!scpu->DebugGetReg(selector, &word))
		return false;

	*value = word;
	return true;
}

static u32 arm7_cache_resets;

uint32_t Arm7CacheResets()
{
	return arm7_cache_resets;
}

// Through the writer of the sound CPU. Returns 0, or kErrNotWritable, having changed nothing, for ENABLED and for
// a value the device refuses
static int WriteArm7(u32 selector, u32 value)
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);

	// ENABLED is no register of the processor, and the debugger does not hold or release the ARM7
	if (scpu == nullptr || selector == kArm7Enabled)
		return kErrNotWritable;

	u32 replaced = 0, stored = 0;
	const bool had = selector == kArm7Pc && scpu->DebugGetReg(kArm7Pc, &replaced);

	// the device refuses a CPSR whose mode bits are no mode, and stores a PC with bits 0 and 1 clear
	if (!scpu->DebugSetReg(selector, value))
		return kErrNotWritable;

	if (selector == kArm7Pc)
	{
		arm7::OnPcWritten();

		// The recompiler of the ARM7 finds a block by its address under the mask of the wave memory, and a block
		// has the address it was compiled at in its code (the PC it leaves, the link of a bl, what R15 reads):
		// behind a PC written into another mirror of the wave memory a resume would run the block of the first
		// mirror and put the PC back there, where a step, which decodes at the PC, goes on in the mirror written.
		// With the cache empty every block is compiled at the address the ARM7 reaches it by. The device stores
		// the PC as it is given, bits 0 and 1 apart; a PC written with the value it has moves nothing
		if (!had || !scpu->DebugGetReg(kArm7Pc, &stored) || stored != replaced)
		{
			scpu->InvalidateJitCache();
			arm7_cache_resets++;
		}
	}

	return 0;
}

// The word of 0x2804 as the DSP uses it (DspDebugState::rbpl): RBP, RBL and TESTB0 are its bits, in the rows of
// dc.aica.dsp and in those of dc.aica.common. What a read of 0x2804 by the CPU gives is not their source
static bool ReadRbpl(u32* word)
{
	DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
	DspDebugState state;

	if (dsp == nullptr || !dsp->DebugGetState(&state))
		return false;

	*word = state.rbpl & 0xFFFF;
	return true;
}

static bool ReadDsp(u32 selector, u64* value)
{
	if (selector == kDspStep || selector == kDspMdecCt)
	{
		DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
		DspDebugState state;

		if (dsp == nullptr || !dsp->DebugGetState(&state))
			return false;

		*value = selector == kDspStep ? state.step : state.mdecCt;
		return true;
	}

	// RBP and RBL are bits 0..11 and 13..14 of the word of 0x2804
	u32 word;

	if (!ReadRbpl(&word))
		return false;

	*value = selector == kDspRbp ? word & 0xFFF : (word >> 13) & 3;
	return true;
}

// The register file of the PVR is an array, and a guest read of it is the array (PVRDevice::Read)
static bool ReadPvr(const Register& reg, u64* value)
{
	u32 word;

	memcpy(&word, &pvr_regs[reg.addr & pvr_RegMask], sizeof(word));

	*value = word;
	return true;
}

// Bits of the register file of the AICA (dbg_regs_private.h has the two forms of a row), from the accessor that
// gives the file as a read by the SH4 delivers it and changes nothing. The guest's own read function is no
// substitute: it rewrites the status words in the file and clears the loop flag of the selected channel.
// The word of 0x2804 (RBP, RBL, TESTB0) is the DSP's, DspDebugState::rbpl: the word the CPU wrote there last
static bool ReadAica(const Register& reg, u64* value)
{
	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	const u32 selector = KeySelector(reg.key);
	const bool two = AicaFieldTwoWords(selector);
	const u32 offset = AicaFieldOffset(selector);
	u8 bytes[6];

	// a word is the low half of four bytes: two words are six bytes
	if (aica == nullptr || !aica->DebugReadRegs(offset, two ? 6 : 2, bytes))
		return false;

	for (u32 at = 0; at < (two ? 6u : 2u); at += 4)
	{
		u32 rbpl;

		if (offset + at != 0x2804)
			continue;

		if (!ReadRbpl(&rbpl))
			return false;

		bytes[at] = (u8)rbpl;
		bytes[at + 1] = (u8)(rbpl >> 8);
	}

	const u32 word = bytes[0] | (bytes[1] << 8);

	if (!two)
	{
		*value = word >> AicaFieldShift(selector);
		return true;
	}

	// the field of the first word above the second word, whose lowest bits are none of the register
	const u32 low = (bytes[4] | (bytes[5] << 8)) & ~((1u << AicaFieldLowShift(selector)) - 1);

	*value = ((u64)(word >> AicaFieldShift(selector)) << 16) | low;
	return true;
}

// The interrupt controller in front of the ARM7 is state of the sound CPU. The words of the AICA's register
// file at 0x2D00 and 0x2D04 are not it
static bool ReadArm7Intc(u32 selector, u64* value)
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	u32 level, in_service;

	if (scpu == nullptr || !scpu->DebugGetInterrupt(&level, &in_service))
		return false;

	switch (selector)
	{
	case kArm7IntcLevel:   *value = level; return true;
	case kArm7IntcService: *value = in_service; return true;
	}

	return false;
}

static bool ReadRtc(u32 selector, u64* value)
{
	u32 seconds;
	bool write_enable;

	if (!libAICA_DebugGetRTC(&seconds, &write_enable))
		return false;

	switch (selector)
	{
	case kRtcSeconds:     *value = seconds; return true;
	case kRtcWriteEnable: *value = write_enable ? 1 : 0; return true;
	}

	return false;
}

// A member of the snapshot of the GD-ROM drive. A machine without the drive has no snapshot
static bool ReadGdrom(u32 selector, u64* value)
{
	GDRomDebugState state;

	if (!gdrom_DebugGetState(&state))
		return false;

	auto packet = [&state](u32 first) {
		return ((u32)state.packet[first] << 24) | ((u32)state.packet[first + 1] << 16) | ((u32)state.packet[first + 2] << 8) | state.packet[first + 3];
	};

	switch (selector)
	{
	case kGdState:                *value = state.state; break;
	case kGdDiscType:             *value = state.discType; break;
	case kGdStatus:               *value = state.status; break;
	case kGdError:                *value = state.error; break;
	case kGdInterruptReason:      *value = state.interruptReason; break;
	case kGdFeatures:             *value = state.features; break;
	case kGdSectorCount:          *value = state.sectorCount; break;
	case kGdSectorNumber:         *value = state.sectorNumber; break;
	case kGdDriveSelect:          *value = state.driveSelect; break;
	case kGdAtaCommand:           *value = state.ataCommand; break;
	case kGdByteCount:            *value = state.byteCount; break;
	case kGdPacket0:              *value = packet(0); break;
	case kGdPacket4:              *value = packet(4); break;
	case kGdPacket8:              *value = packet(8); break;
	case kGdSenseKey:             *value = state.senseKey; break;
	case kGdSenseAsc:             *value = state.senseAsc; break;
	case kGdSenseAscq:            *value = state.senseAscq; break;
	case kGdCddaPlaying:          *value = state.cddaPlaying ? 1 : 0; break;
	case kGdCddaRepeats:          *value = state.cddaRepeats; break;
	case kGdCddaCurrent:          *value = state.cddaCurrent; break;
	case kGdCddaStart:            *value = state.cddaStart; break;
	case kGdCddaEnd:              *value = state.cddaEnd; break;
	case kGdReadStartSector:      *value = state.readStartSector; break;
	case kGdReadRemainingSectors: *value = state.readRemainingSectors; break;
	case kGdReadSectorType:       *value = state.readSectorType; break;
	case kGdPioIndex:             *value = state.pioIndex; break;
	case kGdPioSize:              *value = state.pioSize; break;
	default:
		return false;
	}

	return true;
}

static bool ReadTiming(u32 selector, u64* value)
{
	if (p_sh4rcb == nullptr)
		return false;

	u64 now = sh4_sched_now64();

	if (selector == kTimingSh4Cycles)
	{
		// the SH4 is ahead of the scheduler by what it has used of its slice; before a backend is selected
		// it has used nothing
		emu::Sh4EngineInfo engine;

		if (emu::GetSh4Engine(&engine))
			now += SH4_TIMESLICE - engine.sliceCyclesLeft;
	}

	*value = now;
	return true;
}

static bool ReadText(u32 selector, char* text, u32 n)
{
	emu::Sh4EngineInfo engine;

	switch (selector)
	{
	case kTextSysClk:
		snprintf(text, n, "%uMHz", SH4_MAIN_CLOCK / 1000000);
		return true;

	case kTextBuild:
	{
		// the revision the build system found; it finds none outside a plain checkout
		const char* revision = REICAST_VERSION;
		size_t len = strlen(revision);
		const char notfound[] = "-NOTFOUND";

		if (len == 0 || (len >= sizeof(notfound) - 1 && strcmp(revision + len - (sizeof(notfound) - 1), notfound) == 0))
			snprintf(text, n, "nullDC 3.0.0");
		else
			snprintf(text, n, "nullDC 3.0.0 %s", revision);
		return true;
	}

	case kTextTimingMode:
		snprintf(text, n, "fast");
		return true;

	case kTextSh4Backend:
		if (!emu::GetSh4Engine(&engine))
			return false;
		snprintf(text, n, "%s", engine.backend);
		return true;

	case kTextSh4DebugEngine:
		if (!emu::GetSh4Engine(&engine))
			return false;
		snprintf(text, n, "%s", engine.fallback ? "interpreter-fallback" : "native");
		return true;
	}

	return false;
}

bool Read(const Node& node, uint32_t index, uint64_t* value, char* text, uint32_t n)
{
	if (sh4_cpu == nullptr || index >= node.registerCount)
		return false;

	const Register& reg = node.registers[index];

	if (reg.flags & kRegNoReader)
		return false;

	u32 selector = KeySelector(reg.key);

	if (reg.width == 0)
	{
		char buffer[128];

		if (KeySource(reg.key) != kSrcText || !ReadText(selector, buffer, sizeof(buffer)))
			return false;

		if (text != nullptr && n != 0)
			snprintf(text, n, "%s", buffer);
		return true;
	}

	u64 result = 0;
	bool ok = false;

	switch (KeySource(reg.key))
	{
	case kSrcSh4:    ok = ReadSh4(selector, &result); break;
	case kSrcSh4Mmr: ok = ReadMmr(reg, &result); break;
	case kSrcSb:     ok = ReadSb(reg, &result); break;
	case kSrcArm7:   ok = ReadArm7(selector, &result); break;
	case kSrcDsp:    ok = ReadDsp(selector, &result); break;
	case kSrcTiming: ok = ReadTiming(selector, &result); break;
	case kSrcPvr:    ok = ReadPvr(reg, &result); break;
	case kSrcAica:   ok = ReadAica(reg, &result); break;
	case kSrcArm7Intc: ok = ReadArm7Intc(selector, &result); break;
	case kSrcRtc:    ok = ReadRtc(selector, &result); break;
	case kSrcGdrom:  ok = ReadGdrom(selector, &result); break;
	default: break;
	}

	if (!ok)
		return false;

	if (value != nullptr)
		*value = result & WidthMask(reg.width);
	return true;
}

int Write(const Node& node, uint32_t index, uint64_t value)
{
	if (sh4_cpu == nullptr || index >= node.registerCount)
		return kErrNotWritable;

	const Register& reg = node.registers[index];

	if (!(reg.flags & kRegWritable) || reg.width == 0)
		return kErrNotWritable;

	// the rows that have the flag are 32 bits wide
	const u32 stored = (u32)(value & WidthMask(reg.width));
	const u32 selector = KeySelector(reg.key);

	switch (KeySource(reg.key))
	{
	case kSrcSh4:  return WriteSh4(selector, stored);
	case kSrcArm7: return WriteArm7(selector, stored);
	default:       break;
	}

	// a row that has the flag and no writer
	return kErrNotWritable;
}

/*
	Registers by address
*/
bool ReadSh4RegisterAt(uint32_t addr, uint32_t* value, uint32_t* bytes)
{
	if (sh4_cpu == nullptr || (addr & 3) != 0)
		return false;

	// the tables have the P4 address
	u32 p4 = addr | 0xE0000000;

	if (MmrSlot(p4) == nullptr)
		return false;

	u32 count;
	const Node* nodes = Nodes(&count);

	for (u32 i = 0; i < count; i++)
	{
		const Node& node = nodes[i];

		// the on-chip registers are the rows of the ten module nodes, which have no other rows
		if (node.registerCount == 0 || KeySource(node.registers[0].key) != kSrcSh4Mmr)
			continue;

		for (u32 j = 0; j < node.registerCount; j++)
		{
			const Register& reg = node.registers[j];

			if (KeySource(reg.key) != kSrcSh4Mmr || reg.addr != p4)
				continue;

			u64 result;

			if ((reg.flags & kRegNoReader) || !ReadMmr(reg, &result))
				return false;

			*value = (u32)(result & WidthMask(reg.width));
			*bytes = (reg.width + 7u) / 8;
			return true;
		}
	}

	return false;
}

bool ReadSbRegisterAt(uint32_t addr, uint32_t* value)
{
	if (sh4_cpu == nullptr)
		return false;

	SystemBus* sb = sh4_cpu->GetA0H<SystemBus>(A0H_SB);

	return sb != nullptr && sb->DebugRead(addr, value);
}

} }
