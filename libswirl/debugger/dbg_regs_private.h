/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include <cstdint>

/*
	What dbg_regs_tables.cpp, dbg_regs.cpp, dbg_mem.cpp and their tests share and nothing else uses. Like
	dbg_regs.h this header builds without the emulator.

	Register::key (dbg_regs.h) is what the tables tell the readers about a register: a source in its top byte
	and a selector, whose meaning the source gives, in the low 24 bits.
*/

namespace dbg { namespace regs {

enum Source : uint32_t {
	kSrcNone,		// no row has it: a key of 0 is a row that was not filled in
	kSrcText,		// selector: TextReg. The register has width 0
	kSrcSh4,		// selector: Sh4Reg, a register of the SH4 context
	kSrcSh4Mmr,		// an on-chip module register, Register::addr is its P4 address. selector: MmrReader
	kSrcSb,			// a system bus register, Register::addr is its area 0 address. selector: not used
	kSrcArm7,		// selector: Arm7Reg. The index SoundCPU::DebugGetReg takes, or kArm7Enabled
	kSrcDsp,		// selector: DspReg
	kSrcTiming,		// selector: TimingReg
	kSrcPvr,		// a register of the PVR's register file, Register::addr is its area 0 address. selector: not used
	kSrcAica,		// bits of the AICA's register file, Register::addr is the area 0 address of their word.
					// selector: AicaField() or AicaField2()
	kSrcArm7Intc,	// the interrupt controller in front of the ARM7 (SoundCPU::DebugGetInterrupt). selector: Arm7IntcReg
	kSrcRtc,		// the clock of the AICA (libAICA_DebugGetRTC). selector: RtcReg
	kSrcGdrom,		// a member of the snapshot of the GD-ROM drive (gdrom_DebugGetState). selector: GdromReg
	kSrcCount
};

constexpr uint32_t MakeKey(Source source, uint32_t selector = 0) { return ((uint32_t)source << 24) | (selector & 0xFFFFFF); }
constexpr Source KeySource(uint32_t key) { return (Source)(key >> 24); }
constexpr uint32_t KeySelector(uint32_t key) { return key & 0xFFFFFF; }

enum TextReg : uint32_t {
	kTextSysClk,			// dc.SYSCLK
	kTextBuild,				// dc.BUILD
	kTextTimingMode,		// dc.timing.MODE
	kTextSh4Backend,		// dc.timing.SH4_BACKEND
	kTextSh4DebugEngine,	// dc.timing.SH4_DEBUG_ENGINE
	kTextCount
};

// kSh4R0, kSh4R0Bank, kSh4Fr0 and kSh4Xf0 are the first of 16, 8, 16 and 16 consecutive selectors
enum Sh4Reg : uint32_t {
	kSh4Pc, kSh4Pr, kSh4Sr, kSh4Gbr, kSh4Vbr, kSh4Mach, kSh4Macl, kSh4Ssr, kSh4Spc, kSh4Sgr, kSh4Dbr, kSh4Fpscr, kSh4Fpul,
	kSh4R0,
	kSh4R0Bank = kSh4R0 + 16,
	kSh4Fr0 = kSh4R0Bank + 8,
	kSh4Xf0 = kSh4Fr0 + 16,
	kSh4RegCount = kSh4Xf0 + 16
};

// How an on-chip module register is read. Everything but kMmrData has kRegComputed or kRegNoReader in the
// register's flags: its slot of the module's array holds a read function, which the readers never call
enum MmrReader : uint32_t {
	kMmrData,			// the slot holds the value
	kMmrZero,			// the read function gives 0, whatever the state
	kMmrCpuVersion,		// the read function gives the version constant
	kMmrTmuTcnt0,		// the down counter of timer channel 0, 1, 2: worked out from the scheduler's time
	kMmrTmuTcnt1,
	kMmrTmuTcnt2,
	kMmrNoReader,		// the read function has side effects or reads state this module cannot reach
	kMmrNoSlot,			// the emulator has no such register: the slot is not registered
	kMmrCount
};

enum DspReg : uint32_t {
	kDspStep,
	kDspMdecCt,
	kDspRbp,
	kDspRbl,
	kDspRegCount
};

enum TimingReg : uint32_t {
	kTimingSh4Cycles,
	kTimingSchedNow,
	kTimingRegCount
};

/*
	kSrcAica. The register file of the AICA (hw/aica: 0x8000 bytes, at 0x00700000 for the SH4) has a register of
	16 bits in the low half of every four bytes. A row is a field of one such word, or a quantity that the file
	has in two words that follow one another:

	  AicaField(offset, shift)             Register::width bits of the word at `offset`, from bit `shift` up
	  AicaField2(offset, shift, lowShift)  bits 16 and up of the row are the Register::width - 16 bits of the word
	                                       at `offset`, from bit `shift` up; its bits 0..15 are the word at
	                                       `offset + 4`, of which the bits below `lowShift` are no bits of the
	                                       register and read 0. (The start address of a channel, 7 bits and a
	                                       whole word; the address of the AICA's own DMA, 7 bits and the word
	                                       without its bits 0 and 1.)

	`offset` is a multiple of 4 below 0x8000. In the selector: the word (offset / 4) in bits 0..12, `shift` in
	bits 13..16, bit 17 for a row of two words, `lowShift` in bits 18..21.
*/
constexpr uint32_t AicaField(uint32_t offset, uint32_t shift) { return (offset >> 2) | (shift << 13); }
constexpr uint32_t AicaField2(uint32_t offset, uint32_t shift, uint32_t lowShift) { return AicaField(offset, shift) | (1u << 17) | (lowShift << 18); }
constexpr uint32_t AicaFieldOffset(uint32_t selector) { return (selector & 0x1FFF) << 2; }
constexpr uint32_t AicaFieldShift(uint32_t selector) { return (selector >> 13) & 15; }
constexpr bool AicaFieldTwoWords(uint32_t selector) { return ((selector >> 17) & 1) != 0; }
constexpr uint32_t AicaFieldLowShift(uint32_t selector) { return (selector >> 18) & 15; }
constexpr uint32_t kAicaFieldSelectorMask = 0x3FFFFF;	// the bits a selector of kSrcAica can have

enum Arm7IntcReg : uint32_t {
	kArm7IntcLevel,			// L: the level of the request, as the ARM7 reads it at its 0x00802D00
	kArm7IntcService,		// M: 1 while a request is in service
	kArm7IntcRegCount
};

enum RtcReg : uint32_t {
	kRtcSeconds,			// the counter, which the guest reads as two halves at 0x00710000 and 0x00710004
	kRtcWriteEnable,		// the bit the guest writes at 0x00710008, where it reads 0
	kRtcRegCount
};

// The members of GDRomDebugState (hw/gdrom/gdromv3.h), in its order; the twelve bytes of the packet are three rows
enum GdromReg : uint32_t {
	kGdState, kGdDiscType, kGdStatus, kGdError, kGdInterruptReason, kGdFeatures, kGdSectorCount, kGdSectorNumber, kGdDriveSelect,
	kGdAtaCommand, kGdByteCount,
	kGdPacket0, kGdPacket4, kGdPacket8,		// bytes 0..3, 4..7, 8..11: the first of the four is the highest byte of the value
	kGdSenseKey, kGdSenseAsc, kGdSenseAscq,
	kGdCddaPlaying, kGdCddaRepeats, kGdCddaCurrent, kGdCddaStart, kGdCddaEnd,
	kGdReadStartSector, kGdReadRemainingSectors, kGdReadSectorType,
	kGdPioIndex, kGdPioSize,
	kGdromRegCount
};

// The selectors of kSrcArm7 are register indices of hw/arm7/arm7_context.h, which the tables cannot include.
// dbg_regs.cpp checks them against that header. kArm7Enabled is the one that is none: it is above every index
// of that header, and is not handed to SoundCPU::DebugGetReg
enum Arm7Reg : uint32_t {
	kArm7R0 = 0,		// R0..R14 are 0..14
	kArm7Pc = 15,		// the address of the next instruction
	kArm7Cpsr = 16,
	kArm7Spsr = 17,
	kArm7R13Irq = 18, kArm7R14Irq = 19, kArm7SpsrIrq = 20,
	kArm7R13Usr = 26, kArm7R14Usr = 27,
	kArm7R13Svc = 28, kArm7R14Svc = 29, kArm7SpsrSvc = 30,
	kArm7R13Abt = 31, kArm7R14Abt = 32, kArm7SpsrAbt = 33,
	kArm7R13Und = 34, kArm7R14Und = 35, kArm7SpsrUnd = 36,
	kArm7R8Fiq = 37,	// R8_FIQ..R14_FIQ are 37..43
	kArm7SpsrFiq = 44,
	kArm7Enabled = 0x100,	// no register of the processor: 1 while it is clocked, 0 while it is held in reset
							// (SoundCPU::DebugEnabled). One bit wide
};

/*
	Registers by guest address, for the register regions of dbg::mem (dbg_regs.cpp). Same threads as
	dbg::regs::Read, the same rule: no guest handler is called.
*/

// The on-chip register of the SH4 at addr, a multiple of 4 given as P4 or as area 7 address: its value, and
// the number of bytes it has (1, 2 or 4). False when the address holds no register of the tables, when the
// register has no reader, and when no machine exists.
bool ReadSh4RegisterAt(uint32_t addr, uint32_t* value, uint32_t* bytes);

// The system bus register at addr, a multiple of 4 in 0x005F6800 .. 0x005F7CFC: its 32 bits as a guest read
// gives them. False when the slot is no register, is write-only or has no reader, and when no machine exists.
bool ReadSbRegisterAt(uint32_t addr, uint32_t* value);

/*
	For the selftests of the writer (dbg_regs.cpp).
*/

// How often dbg::regs::Write has emptied the code cache of the ARM7: once behind every write of its PC that
// was taken and stored another address than the PC had (SoundCPU::InvalidateJitCache). On a host without a
// recompiler of the ARM7 that call does nothing a test could see, and this count is what shows which writes
// make it. It wraps, is no state of the machine and is in no save state. Emulator thread.
uint32_t Arm7CacheResets();

} }
