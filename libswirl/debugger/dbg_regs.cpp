/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_regs.h"
#include "dbg_regs_private.h"

#include "dbg_emu.h"

#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7_context.h"
#include "hw/gdrom/gdromv3.h"
#include "hw/holly/sb.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/sh4_sched.h"
#include "version.h"

#include <cstdio>
#include <cstring>

/*
	The register readers. Every value comes from a backing store or from a reader written for the debugger: an
	array of register slots, the SH4 context, a debug accessor of a device. No read function of a register and
	no guest memory handler is called from here, so a read changes nothing and cannot end the process.

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

	if (scpu == nullptr || !scpu->DebugGetReg(selector, &word))
		return false;

	*value = word;
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

	// RBP and RBL are bits 0..11 and 13..14 of the common register 0x2804
	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	u8 bytes[2];

	if (aica == nullptr || !aica->DebugReadRegs(0x2804, 2, bytes))
		return false;

	u32 word = bytes[0] | (bytes[1] << 8);

	*value = selector == kDspRbp ? word & 0xFFF : (word >> 13) & 3;
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
	default: break;
	}

	if (!ok)
		return false;

	if (value != nullptr)
		*value = result & WidthMask(reg.width);
	return true;
}

// No register can be written yet.
int Write(const Node&, uint32_t, uint64_t) { return kErrNotSupported; }

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

// The device accessors that are free functions give nothing yet. Their place is hw/gdrom/gdromv3.cpp.
bool gdrom_DebugGetState(GDRomDebugState*) { return false; }
