/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_regs.h"
#include "dbg_regs_private.h"
#include "dbg_events.h"

#include <cstring>

/*
	The device tree: nodes, their registers, events and panels. Constant data, no reference to the emulator:
	this file links into tests without it.

	A node's place in the list is its place on the screen: a node stands behind the node it names as parent, and
	the nodes of one parent stand in display order. The two levels under the root carry no registers, because a
	client shows them unfolded.

	Register names are upper case and unique within their node. A panel id is one the devtools UI knows
	(PanelId of its schema); a node offers none or at least one.
*/

namespace dbg { namespace regs {

#define COUNT(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

/*
	dc
*/
static const Register dc_regs[] = {
	{ "SYSCLK", 0, 0, kNoAddr, MakeKey(kSrcText, kTextSysClk) },
	{ "BUILD",  0, 0, kNoAddr, MakeKey(kSrcText, kTextBuild) },
};

static const uint8_t dc_events[] = { EV_SYSTEM_RESET, EV_SYSTEM_STATE, EV_DEBUGGER_ENGINE, EV_DEBUGGER_DSP_FORCED, EV_DEBUGGER_NOTE };

/*
	dc.sh4.cpu: the registers of the SH4 context. FR and XF are the raw 32 bits of the single-precision
	registers; Rn_BANK is the bank of R0..R7 that is not selected.
*/
#define SH4(sel) MakeKey(kSrcSh4, sel)
#define SH4_ROW16(prefix, first) \
	{ prefix "0",  32, 0, kNoAddr, SH4(first + 0)  }, { prefix "1",  32, 0, kNoAddr, SH4(first + 1)  }, \
	{ prefix "2",  32, 0, kNoAddr, SH4(first + 2)  }, { prefix "3",  32, 0, kNoAddr, SH4(first + 3)  }, \
	{ prefix "4",  32, 0, kNoAddr, SH4(first + 4)  }, { prefix "5",  32, 0, kNoAddr, SH4(first + 5)  }, \
	{ prefix "6",  32, 0, kNoAddr, SH4(first + 6)  }, { prefix "7",  32, 0, kNoAddr, SH4(first + 7)  }, \
	{ prefix "8",  32, 0, kNoAddr, SH4(first + 8)  }, { prefix "9",  32, 0, kNoAddr, SH4(first + 9)  }, \
	{ prefix "10", 32, 0, kNoAddr, SH4(first + 10) }, { prefix "11", 32, 0, kNoAddr, SH4(first + 11) }, \
	{ prefix "12", 32, 0, kNoAddr, SH4(first + 12) }, { prefix "13", 32, 0, kNoAddr, SH4(first + 13) }, \
	{ prefix "14", 32, 0, kNoAddr, SH4(first + 14) }, { prefix "15", 32, 0, kNoAddr, SH4(first + 15) }

static const Register sh4_cpu_regs[] = {
	{ "PC",    32, 0, kNoAddr, SH4(kSh4Pc) },
	{ "PR",    32, 0, kNoAddr, SH4(kSh4Pr) },
	{ "SR",    32, 0, kNoAddr, SH4(kSh4Sr) },
	{ "GBR",   32, 0, kNoAddr, SH4(kSh4Gbr) },
	{ "VBR",   32, 0, kNoAddr, SH4(kSh4Vbr) },
	{ "MACH",  32, 0, kNoAddr, SH4(kSh4Mach) },
	{ "MACL",  32, 0, kNoAddr, SH4(kSh4Macl) },
	{ "SSR",   32, 0, kNoAddr, SH4(kSh4Ssr) },
	{ "SPC",   32, 0, kNoAddr, SH4(kSh4Spc) },
	{ "SGR",   32, 0, kNoAddr, SH4(kSh4Sgr) },
	{ "DBR",   32, 0, kNoAddr, SH4(kSh4Dbr) },
	{ "FPSCR", 32, 0, kNoAddr, SH4(kSh4Fpscr) },
	{ "FPUL",  32, 0, kNoAddr, SH4(kSh4Fpul) },
	SH4_ROW16("R", kSh4R0),
	{ "R0_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 0) }, { "R1_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 1) },
	{ "R2_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 2) }, { "R3_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 3) },
	{ "R4_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 4) }, { "R5_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 5) },
	{ "R6_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 6) }, { "R7_BANK", 32, 0, kNoAddr, SH4(kSh4R0Bank + 7) },
	SH4_ROW16("FR", kSh4Fr0),
	SH4_ROW16("XF", kSh4Xf0),
};

static const uint8_t sh4_events[] = { EV_SH4_INTERRUPT, EV_SH4_EXCEPTION };

static const char* const sh4_cpu_actions[] = { "sh4-disassembly", "sh4-memory" };

/*
	dc.sh4.<module>: the on-chip registers, one row per NAME_addr definition of hw/sh4/sh4_mmr.h in the order of
	that header, with the width its comment there gives (the read width where it gives two). addr is the P4
	address. A register behind a read function is computed, or cannot be read:

	  TMU.TCNTn         the counter is worked out from the scheduler's time
	  TMU.TCPR2, INTC.IPRD, CCN.PRR, SCIF.SCFDR2, CCN.CPU_VERSION
	                    the read function gives a constant
	  SCIF.SCFSR2, SCIF.SCFRDR2
	                    reading takes bytes from the host's serial input
	  BSC.PDTRA         the value depends on what was last written, which only the module keeps

	The SCI is not emulated: its registers are listed, and have no slot to read.
*/
#define MMR MakeKey(kSrcSh4Mmr, kMmrData)

static const Register bsc_regs[] = {
	{ "BCR1",        32, 0,            0xFF800000, MMR },
	{ "BCR2",        16, 0,            0xFF800004, MMR },
	{ "WCR1",        32, 0,            0xFF800008, MMR },
	{ "WCR2",        32, 0,            0xFF80000C, MMR },
	{ "WCR3",        32, 0,            0xFF800010, MMR },
	{ "MCR",         32, 0,            0xFF800014, MMR },
	{ "PCR",         16, 0,            0xFF800018, MMR },
	{ "RTCSR",       16, 0,            0xFF80001C, MMR },
	{ "RTCNT",       16, 0,            0xFF800020, MMR },
	{ "RTCOR",       16, 0,            0xFF800024, MMR },
	{ "RFCR",        16, 0,            0xFF800028, MMR },
	{ "PCTRA",       32, 0,            0xFF80002C, MMR },
	{ "PDTRA",       16, kRegNoReader, 0xFF800030, MakeKey(kSrcSh4Mmr, kMmrNoReader) },
	{ "PCTRB",       32, 0,            0xFF800040, MMR },
	{ "PDTRB",       16, 0,            0xFF800044, MMR },
	{ "GPIOIC",      16, 0,            0xFF800048, MMR },
};

static const Register ccn_regs[] = {
	{ "PTEH",        32, 0,            0xFF000000, MMR },
	{ "PTEL",        32, 0,            0xFF000004, MMR },
	{ "TTB",         32, 0,            0xFF000008, MMR },
	{ "TEA",         32, 0,            0xFF00000C, MMR },
	{ "MMUCR",       32, 0,            0xFF000010, MMR },
	{ "BASRA",        8, 0,            0xFF000014, MMR },
	{ "BASRB",        8, 0,            0xFF000018, MMR },
	{ "CCR",         32, 0,            0xFF00001C, MMR },
	{ "TRA",         32, 0,            0xFF000020, MMR },
	{ "EXPEVT",      32, 0,            0xFF000024, MMR },
	{ "INTEVT",      32, 0,            0xFF000028, MMR },
	{ "CPU_VERSION", 32, kRegComputed, 0xFF000030, MakeKey(kSrcSh4Mmr, kMmrCpuVersion) },
	{ "PTEA",        32, 0,            0xFF000034, MMR },
	{ "QACR0",       32, 0,            0xFF000038, MMR },
	{ "QACR1",       32, 0,            0xFF00003C, MMR },
	{ "PRR",         32, kRegComputed, 0xFF000044, MakeKey(kSrcSh4Mmr, kMmrZero) },
};

static const Register cpg_regs[] = {
	{ "FRQCR",       16, 0,            0xFFC00000, MMR },
	{ "STBCR",        8, 0,            0xFFC00004, MMR },
	{ "WTCNT",        8, 0,            0xFFC00008, MMR },
	{ "WTCSR",        8, 0,            0xFFC0000C, MMR },
	{ "STBCR2",       8, 0,            0xFFC00010, MMR },
};

static const Register dmac_regs[] = {
	{ "SAR0",        32, 0,            0xFFA00000, MMR },
	{ "DAR0",        32, 0,            0xFFA00004, MMR },
	{ "DMATCR0",     32, 0,            0xFFA00008, MMR },
	{ "CHCR0",       32, 0,            0xFFA0000C, MMR },
	{ "SAR1",        32, 0,            0xFFA00010, MMR },
	{ "DAR1",        32, 0,            0xFFA00014, MMR },
	{ "DMATCR1",     32, 0,            0xFFA00018, MMR },
	{ "CHCR1",       32, 0,            0xFFA0001C, MMR },
	{ "SAR2",        32, 0,            0xFFA00020, MMR },
	{ "DAR2",        32, 0,            0xFFA00024, MMR },
	{ "DMATCR2",     32, 0,            0xFFA00028, MMR },
	{ "CHCR2",       32, 0,            0xFFA0002C, MMR },
	{ "SAR3",        32, 0,            0xFFA00030, MMR },
	{ "DAR3",        32, 0,            0xFFA00034, MMR },
	{ "DMATCR3",     32, 0,            0xFFA00038, MMR },
	{ "CHCR3",       32, 0,            0xFFA0003C, MMR },
	{ "DMAOR",       32, 0,            0xFFA00040, MMR },
};

static const Register intc_regs[] = {
	{ "ICR",         16, 0,            0xFFD00000, MMR },
	{ "IPRA",        16, 0,            0xFFD00004, MMR },
	{ "IPRB",        16, 0,            0xFFD00008, MMR },
	{ "IPRC",        16, 0,            0xFFD0000C, MMR },
	{ "IPRD",        16, kRegComputed, 0xFFD00010, MakeKey(kSrcSh4Mmr, kMmrZero) },
};

static const Register rtc_regs[] = {
	{ "R64CNT",       8, 0,            0xFFC80000, MMR },
	{ "RSECCNT",      8, 0,            0xFFC80004, MMR },
	{ "RMINCNT",      8, 0,            0xFFC80008, MMR },
	{ "RHRCNT",       8, 0,            0xFFC8000C, MMR },
	{ "RWKCNT",       8, 0,            0xFFC80010, MMR },
	{ "RDAYCNT",      8, 0,            0xFFC80014, MMR },
	{ "RMONCNT",      8, 0,            0xFFC80018, MMR },
	{ "RYRCNT",      16, 0,            0xFFC8001C, MMR },
	{ "RSECAR",       8, 0,            0xFFC80020, MMR },
	{ "RMINAR",       8, 0,            0xFFC80024, MMR },
	{ "RHRAR",        8, 0,            0xFFC80028, MMR },
	{ "RWKAR",        8, 0,            0xFFC8002C, MMR },
	{ "RDAYAR",       8, 0,            0xFFC80030, MMR },
	{ "RMONAR",       8, 0,            0xFFC80034, MMR },
	{ "RCR1",         8, 0,            0xFFC80038, MMR },
	{ "RCR2",         8, 0,            0xFFC8003C, MMR },
};

static const Register sci_regs[] = {
	{ "SCSMR1",       8, kRegNoReader, 0xFFE00000, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
	{ "SCBRR1",       8, kRegNoReader, 0xFFE00004, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
	{ "SCSCR1",       8, kRegNoReader, 0xFFE00008, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
	{ "SCTDR1",       8, kRegNoReader, 0xFFE0000C, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
	{ "SCSSR1",       8, kRegNoReader, 0xFFE00010, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
	{ "SCRDR1",       8, kRegNoReader, 0xFFE00014, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
	{ "SCSCMR1",      8, kRegNoReader, 0xFFE00018, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
	{ "SCSPTR1",      8, kRegNoReader, 0xFFE0001C, MakeKey(kSrcSh4Mmr, kMmrNoSlot) },
};

static const Register scif_regs[] = {
	{ "SCSMR2",      16, 0,            0xFFE80000, MMR },
	{ "SCBRR2",       8, 0,            0xFFE80004, MMR },
	{ "SCSCR2",      16, 0,            0xFFE80008, MMR },
	{ "SCFTDR2",      8, 0,            0xFFE8000C, MMR },
	{ "SCFSR2",      16, kRegNoReader, 0xFFE80010, MakeKey(kSrcSh4Mmr, kMmrNoReader) },
	{ "SCFRDR2",      8, kRegNoReader, 0xFFE80014, MakeKey(kSrcSh4Mmr, kMmrNoReader) },
	{ "SCFCR2",      16, 0,            0xFFE80018, MMR },
	{ "SCFDR2",      16, kRegComputed, 0xFFE8001C, MakeKey(kSrcSh4Mmr, kMmrZero) },
	{ "SCSPTR2",     16, 0,            0xFFE80020, MMR },
	{ "SCLSR2",      16, 0,            0xFFE80024, MMR },
};

static const Register tmu_regs[] = {
	{ "TOCR",         8, 0,            0xFFD80000, MMR },
	{ "TSTR",         8, 0,            0xFFD80004, MMR },
	{ "TCOR0",       32, 0,            0xFFD80008, MMR },
	{ "TCNT0",       32, kRegComputed, 0xFFD8000C, MakeKey(kSrcSh4Mmr, kMmrTmuTcnt0) },
	{ "TCR0",        16, 0,            0xFFD80010, MMR },
	{ "TCOR1",       32, 0,            0xFFD80014, MMR },
	{ "TCNT1",       32, kRegComputed, 0xFFD80018, MakeKey(kSrcSh4Mmr, kMmrTmuTcnt1) },
	{ "TCR1",        16, 0,            0xFFD8001C, MMR },
	{ "TCOR2",       32, 0,            0xFFD80020, MMR },
	{ "TCNT2",       32, kRegComputed, 0xFFD80024, MakeKey(kSrcSh4Mmr, kMmrTmuTcnt2) },
	{ "TCR2",        16, 0,            0xFFD80028, MMR },
	{ "TCPR2",       32, kRegComputed, 0xFFD8002C, MakeKey(kSrcSh4Mmr, kMmrZero) },
};

static const Register ubc_regs[] = {
	{ "BARA",        32, 0,            0xFF200000, MMR },
	{ "BAMRA",        8, 0,            0xFF200004, MMR },
	{ "BBRA",        16, 0,            0xFF200008, MMR },
	{ "BARB",        32, 0,            0xFF20000C, MMR },
	{ "BAMRB",        8, 0,            0xFF200010, MMR },
	{ "BBRB",        16, 0,            0xFF200014, MMR },
	{ "BDRB",        32, 0,            0xFF200018, MMR },
	{ "BDMRB",       32, 0,            0xFF20001C, MMR },
	{ "BRCR",        16, 0,            0xFF200020, MMR },
};

#undef MMR

static const char* const bsc_actions[]  = { "bsc-registers" };
static const char* const ccn_actions[]  = { "ccn-registers" };
static const char* const cpg_actions[]  = { "cpg-registers" };
static const char* const dmac_actions[] = { "dmac-registers" };
static const char* const intc_actions[] = { "intc-registers" };
static const char* const rtc_actions[]  = { "rtc-registers" };
static const char* const sci_actions[]  = { "sci-registers" };
static const char* const scif_actions[] = { "scif-registers" };
static const char* const tmu_actions[]  = { "tmu-registers" };
static const char* const ubc_actions[]  = { "ubc-registers" };

static const char* const sq_actions[]     = { "sq-contents" };
static const char* const icache_actions[] = { "icache-contents" };
static const char* const ocache_actions[] = { "ocache-contents" };
static const char* const ocram_actions[]  = { "ocram-contents" };
static const char* const tlb_actions[]    = { "tlb-contents" };

/*
	dc.holly.intc: the interrupt registers of the system bus. addr is the area 0 address.
*/
#define SB MakeKey(kSrcSb)

static const Register holly_intc_regs[] = {
	{ "SB_ISTNRM",  32, kRegComputed, 0x005F6900, SB },
	{ "SB_ISTEXT",  32, 0,            0x005F6904, SB },
	{ "SB_ISTERR",  32, 0,            0x005F6908, SB },
	{ "SB_IML2NRM", 32, 0,            0x005F6910, SB },
	{ "SB_IML2EXT", 32, 0,            0x005F6914, SB },
	{ "SB_IML2ERR", 32, 0,            0x005F6918, SB },
	{ "SB_IML4NRM", 32, 0,            0x005F6920, SB },
	{ "SB_IML4EXT", 32, 0,            0x005F6924, SB },
	{ "SB_IML4ERR", 32, 0,            0x005F6928, SB },
	{ "SB_IML6NRM", 32, 0,            0x005F6930, SB },
	{ "SB_IML6EXT", 32, 0,            0x005F6934, SB },
	{ "SB_IML6ERR", 32, 0,            0x005F6938, SB },
};

#undef SB

static const uint8_t holly_dmac_events[] = { EV_DMAC_START, EV_DMAC_END };
static const uint8_t holly_ta_events[]   = { EV_TA_LIST_INIT, EV_TA_LIST_END };
static const uint8_t holly_core_events[] = { EV_CORE_RENDER_START, EV_CORE_RENDER_END, EV_CORE_VBLANK };

static const char* const holly_ta_actions[]   = { "holly-ta" };
static const char* const holly_core_actions[] = { "holly-core" };

/*
	dc.aica.arm7. PC is the address of the next instruction. Rn_<mode> and SPSR_<mode> are the banked registers
	of a mode; for the mode the processor is in they show what R8..R14 and SPSR show.
*/
#define ARM7(sel) MakeKey(kSrcArm7, sel)

static const Register arm7_regs[] = {
	{ "PC",       32, 0, kNoAddr, ARM7(kArm7Pc) },
	{ "R0",       32, 0, kNoAddr, ARM7(kArm7R0 + 0) },
	{ "R1",       32, 0, kNoAddr, ARM7(kArm7R0 + 1) },
	{ "R2",       32, 0, kNoAddr, ARM7(kArm7R0 + 2) },
	{ "R3",       32, 0, kNoAddr, ARM7(kArm7R0 + 3) },
	{ "R4",       32, 0, kNoAddr, ARM7(kArm7R0 + 4) },
	{ "R5",       32, 0, kNoAddr, ARM7(kArm7R0 + 5) },
	{ "R6",       32, 0, kNoAddr, ARM7(kArm7R0 + 6) },
	{ "R7",       32, 0, kNoAddr, ARM7(kArm7R0 + 7) },
	{ "R8",       32, 0, kNoAddr, ARM7(kArm7R0 + 8) },
	{ "R9",       32, 0, kNoAddr, ARM7(kArm7R0 + 9) },
	{ "R10",      32, 0, kNoAddr, ARM7(kArm7R0 + 10) },
	{ "R11",      32, 0, kNoAddr, ARM7(kArm7R0 + 11) },
	{ "R12",      32, 0, kNoAddr, ARM7(kArm7R0 + 12) },
	{ "R13",      32, 0, kNoAddr, ARM7(kArm7R0 + 13) },
	{ "R14",      32, 0, kNoAddr, ARM7(kArm7R0 + 14) },
	{ "CPSR",     32, 0, kNoAddr, ARM7(kArm7Cpsr) },
	{ "SPSR",     32, 0, kNoAddr, ARM7(kArm7Spsr) },
	{ "R13_USR",  32, 0, kNoAddr, ARM7(kArm7R13Usr) },
	{ "R14_USR",  32, 0, kNoAddr, ARM7(kArm7R14Usr) },
	{ "R8_FIQ",   32, 0, kNoAddr, ARM7(kArm7R8Fiq + 0) },
	{ "R9_FIQ",   32, 0, kNoAddr, ARM7(kArm7R8Fiq + 1) },
	{ "R10_FIQ",  32, 0, kNoAddr, ARM7(kArm7R8Fiq + 2) },
	{ "R11_FIQ",  32, 0, kNoAddr, ARM7(kArm7R8Fiq + 3) },
	{ "R12_FIQ",  32, 0, kNoAddr, ARM7(kArm7R8Fiq + 4) },
	{ "R13_FIQ",  32, 0, kNoAddr, ARM7(kArm7R8Fiq + 5) },
	{ "R14_FIQ",  32, 0, kNoAddr, ARM7(kArm7R8Fiq + 6) },
	{ "SPSR_FIQ", 32, 0, kNoAddr, ARM7(kArm7SpsrFiq) },
	{ "R13_IRQ",  32, 0, kNoAddr, ARM7(kArm7R13Irq) },
	{ "R14_IRQ",  32, 0, kNoAddr, ARM7(kArm7R14Irq) },
	{ "SPSR_IRQ", 32, 0, kNoAddr, ARM7(kArm7SpsrIrq) },
	{ "R13_SVC",  32, 0, kNoAddr, ARM7(kArm7R13Svc) },
	{ "R14_SVC",  32, 0, kNoAddr, ARM7(kArm7R14Svc) },
	{ "SPSR_SVC", 32, 0, kNoAddr, ARM7(kArm7SpsrSvc) },
	{ "R13_ABT",  32, 0, kNoAddr, ARM7(kArm7R13Abt) },
	{ "R14_ABT",  32, 0, kNoAddr, ARM7(kArm7R14Abt) },
	{ "SPSR_ABT", 32, 0, kNoAddr, ARM7(kArm7SpsrAbt) },
	{ "R13_UND",  32, 0, kNoAddr, ARM7(kArm7R13Und) },
	{ "R14_UND",  32, 0, kNoAddr, ARM7(kArm7R14Und) },
	{ "SPSR_UND", 32, 0, kNoAddr, ARM7(kArm7SpsrUnd) },
};

#undef ARM7

static const uint8_t aica_events[]     = { EV_AICA_INTERRUPT, EV_AICA_TIMER };
static const uint8_t aica_sgc_events[] = { EV_AICA_KEY_ON, EV_AICA_KEY_OFF, EV_AICA_LOOP };

static const char* const arm7_actions[] = { "arm7-disassembly", "arm7-memory" };
static const char* const sgc_actions[]  = { "sgc" };

/*
	dc.aica.dsp. STEP is the program step that executes next; MDEC_CT is the counter that counts down from the
	ring buffer length; RBP and RBL are the fields of the common register 0x2804 (12 and 2 bits of it).
	The widths are those of the registers: MDEC_CT has 16 bits on the hardware. The emulator keeps the counter
	as a 32-bit value and reloads it with the ring buffer length, which can be 0x10000, so the register shows
	the counter modulo 2^16 and can read 0x0000 for one sample.
*/
static const Register dsp_regs[] = {
	{ "STEP",     8, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspStep) },
	{ "MDEC_CT", 16, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspMdecCt) },
	{ "RBP",     16, 0,            0x00702804, MakeKey(kSrcDsp, kDspRbp) },
	{ "RBL",      8, 0,            0x00702804, MakeKey(kSrcDsp, kDspRbl) },
};

static const char* const dsp_actions[] = { "dsp-disassembly" };

static const uint8_t gdrom_events[] = { EV_GDROM_COMMAND };
static const uint8_t maple_events[] = { EV_MAPLE_DMA };

/*
	dc.timing. SH4_CYCLES is the time of the SH4: the scheduler's time plus what the SH4 has used of its slice.
*/
static const Register timing_regs[] = {
	{ "MODE",             0,  0,            kNoAddr, MakeKey(kSrcText, kTextTimingMode) },
	{ "SH4_CYCLES",       64, kRegComputed, kNoAddr, MakeKey(kSrcTiming, kTimingSh4Cycles) },
	{ "SCHED_NOW",        64, kRegComputed, kNoAddr, MakeKey(kSrcTiming, kTimingSchedNow) },
	{ "SH4_BACKEND",      0,  0,            kNoAddr, MakeKey(kSrcText, kTextSh4Backend) },
	{ "SH4_DEBUG_ENGINE", 0,  0,            kNoAddr, MakeKey(kSrcText, kTextSh4DebugEngine) },
};

/*
	The nodes.
*/
#define REGS(a)    a, COUNT(a)
#define NO_REGS    nullptr, 0
#define ACTIONS(a) a, COUNT(a)
#define NO_ACTIONS nullptr, 0
#define EVENTS(a)  a, COUNT(a)
#define NO_EVENTS  nullptr, 0

#define MODULE(name, label, description) \
	{ "dc.sh4." #name, "dc.sh4.pbus", label, description, REGS(name##_regs), ACTIONS(name##_actions), NO_EVENTS, 0 }
#define CONTENTS(name, label, description) \
	{ "dc.sh4." #name, "dc.sh4.pbus", label, description, NO_REGS, ACTIONS(name##_actions), NO_EVENTS, 0 }

static const Node nodes[] = {
	{ "dc", nullptr, "Dreamcast", "beloved console", REGS(dc_regs), NO_ACTIONS, EVENTS(dc_events), 0 },

	{ "dc.sh4", "dc", "SH4", "SH7750-alike SoC", NO_REGS, NO_ACTIONS, EVENTS(sh4_events), 0 },
	{ "dc.sh4.cpu", "dc.sh4", "Core", "SuperH4 CPU Core", REGS(sh4_cpu_regs), ACTIONS(sh4_cpu_actions), NO_EVENTS, kNodeBase | kNodeSh4Context },
	{ "dc.sh4.pbus", "dc.sh4", "PBUS", "Peripherals Bus", NO_REGS, NO_ACTIONS, NO_EVENTS, 0 },
	MODULE(bsc,  "BSC",  "Bus State Controller"),
	MODULE(ccn,  "CCN",  "Cache Controller"),
	MODULE(cpg,  "CPG",  "Clock Pulse Generator"),
	MODULE(dmac, "DMAC", "Direct Memory Access Controller"),
	MODULE(intc, "INTC", "Interrupt Controller"),
	MODULE(rtc,  "RTC",  "Real Time Clock"),
	MODULE(sci,  "SCI",  "Serial Communications Interface"),
	MODULE(scif, "SCIF", "Serial Communications Interface w/ FIFO"),
	MODULE(tmu,  "TMU",  "Timer Unit"),
	MODULE(ubc,  "UBC",  "User Break Controller"),
	CONTENTS(sq,     "SQ",     "Store Queues"),
	CONTENTS(icache, "ICACHE", "Instruction Cache"),
	CONTENTS(ocache, "OCACHE", "Operand Cache"),
	CONTENTS(ocram,  "OCRAM",  "Operand RAM"),
	CONTENTS(tlb,    "TLB",    "Translation Lookaside Buffer"),

	{ "dc.holly", "dc", "Holly", "System ASIC", NO_REGS, NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.holly.intc", "dc.holly", "INTC", "Interrupt Controller", REGS(holly_intc_regs), NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.holly.dmac", "dc.holly", "DMA Controller", "peripheral", NO_REGS, NO_ACTIONS, EVENTS(holly_dmac_events), 0 },
	{ "dc.holly.ta", "dc.holly", "TA", "Tile Accelerator", NO_REGS, ACTIONS(holly_ta_actions), EVENTS(holly_ta_events), 0 },
	{ "dc.holly.core", "dc.holly", "CORE", "Depth and Shading Engine", NO_REGS, ACTIONS(holly_core_actions), EVENTS(holly_core_events), 0 },

	{ "dc.aica", "dc", "AICA", "Sound SoC", NO_REGS, NO_ACTIONS, EVENTS(aica_events), 0 },
	{ "dc.aica.arm7", "dc.aica", "ARM7", "ARM7DI CPU Core", REGS(arm7_regs), ACTIONS(arm7_actions), NO_EVENTS, kNodeBase },
	{ "dc.aica.sgc", "dc.aica", "SGC", "Sound Generation Core", NO_REGS, ACTIONS(sgc_actions), EVENTS(aica_sgc_events), 0 },
	{ "dc.aica.sgc.0", "dc.aica.sgc", "Channel 0", "SGC Channel 0", NO_REGS, NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.aica.sgc.1", "dc.aica.sgc", "Channel 1", "SGC Channel 1", NO_REGS, NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.aica.dsp", "dc.aica", "DSP", "DSP VLIW Core", REGS(dsp_regs), ACTIONS(dsp_actions), NO_EVENTS, kNodeBase },

	{ "dc.system", "dc", "System", "Peripherals and timing", NO_REGS, NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.gdrom", "dc.system", "GD-ROM", "GD-ROM drive", NO_REGS, NO_ACTIONS, EVENTS(gdrom_events), 0 },
	{ "dc.maple", "dc.system", "Maple", "Maple bus", NO_REGS, NO_ACTIONS, EVENTS(maple_events), 0 },
	{ "dc.timing", "dc.system", "Timing", "Scheduler time and timing mode", REGS(timing_regs), NO_ACTIONS, NO_EVENTS, kNodeBase },
};

const Node* Nodes(uint32_t* count)
{
	*count = COUNT(nodes);
	return nodes;
}

const Node* Find(const char* path)
{
	for (const Node& node : nodes)
	{
		if (strcmp(node.path, path) == 0)
			return &node;
	}

	return nullptr;
}

// Register names are ASCII: only the letters a-z are folded
static bool SameName(const char* a, const char* b)
{
	for (;; a++, b++)
	{
		char ca = *a, cb = *b;

		if (ca >= 'a' && ca <= 'z')
			ca = (char)(ca - 'a' + 'A');
		if (cb >= 'a' && cb <= 'z')
			cb = (char)(cb - 'a' + 'A');

		if (ca != cb)
			return false;
		if (ca == 0)
			return true;
	}
}

int FindRegister(const Node& node, const char* name)
{
	for (uint32_t i = 0; i < node.registerCount; i++)
	{
		if (SameName(node.registers[i].name, name))
			return (int)i;
	}

	return -1;
}

void FormatValue(uint32_t width, uint64_t value, char* out)
{
	static const char digits[] = "0123456789ABCDEF";

	if (width > 64)
		width = 64;
	if (width < 64)
		value &= ((uint64_t)1 << width) - 1;

	uint32_t count = (width + 3) / 4;

	*out++ = '0';
	*out++ = 'x';
	for (uint32_t i = count; i-- > 0; )
		*out++ = digits[(value >> (i * 4)) & 15];
	*out = 0;
}

} }
