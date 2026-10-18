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

	The writable rows are the registers of the two processors (dc.sh4.cpu, and dc.aica.arm7 but ENABLED). Every
	other row is read-only: the registers of the devices have no writer (dbg_regs.h).
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

// A node lists the events this build raises.
static const uint8_t dc_events[] = { EV_SYSTEM_RESET, EV_SYSTEM_STATE, EV_DEBUGGER_ENGINE, EV_DEBUGGER_NOTE };

/*
	dc.sh4.cpu: the registers of the SH4 context. FR and XF are the raw 32 bits of the single-precision
	registers; Rn_BANK is the bank of R0..R7 that is not selected.
	Every row is writable (dbg::regs::Write, dbg_regs.h): a store into the context, with what the processor does
	when it loads PC, SR or FPSCR.
*/
#define SH4(sel) MakeKey(kSrcSh4, sel)
#define SH4_REG(name, sel) { name, 32, kRegWritable, kNoAddr, SH4(sel) }
#define SH4_ROW16(prefix, first) \
	SH4_REG(prefix "0",  first + 0),  SH4_REG(prefix "1",  first + 1),  SH4_REG(prefix "2",  first + 2),  SH4_REG(prefix "3",  first + 3), \
	SH4_REG(prefix "4",  first + 4),  SH4_REG(prefix "5",  first + 5),  SH4_REG(prefix "6",  first + 6),  SH4_REG(prefix "7",  first + 7), \
	SH4_REG(prefix "8",  first + 8),  SH4_REG(prefix "9",  first + 9),  SH4_REG(prefix "10", first + 10), SH4_REG(prefix "11", first + 11), \
	SH4_REG(prefix "12", first + 12), SH4_REG(prefix "13", first + 13), SH4_REG(prefix "14", first + 14), SH4_REG(prefix "15", first + 15)

static const Register sh4_cpu_regs[] = {
	SH4_REG("PC",    kSh4Pc),
	SH4_REG("PR",    kSh4Pr),
	SH4_REG("SR",    kSh4Sr),
	SH4_REG("GBR",   kSh4Gbr),
	SH4_REG("VBR",   kSh4Vbr),
	SH4_REG("MACH",  kSh4Mach),
	SH4_REG("MACL",  kSh4Macl),
	SH4_REG("SSR",   kSh4Ssr),
	SH4_REG("SPC",   kSh4Spc),
	SH4_REG("SGR",   kSh4Sgr),
	SH4_REG("DBR",   kSh4Dbr),
	SH4_REG("FPSCR", kSh4Fpscr),
	SH4_REG("FPUL",  kSh4Fpul),
	SH4_ROW16("R", kSh4R0),
	SH4_REG("R0_BANK", kSh4R0Bank + 0), SH4_REG("R1_BANK", kSh4R0Bank + 1), SH4_REG("R2_BANK", kSh4R0Bank + 2), SH4_REG("R3_BANK", kSh4R0Bank + 3),
	SH4_REG("R4_BANK", kSh4R0Bank + 4), SH4_REG("R5_BANK", kSh4R0Bank + 5), SH4_REG("R6_BANK", kSh4R0Bank + 6), SH4_REG("R7_BANK", kSh4R0Bank + 7),
	SH4_ROW16("FR", kSh4Fr0),
	SH4_ROW16("XF", kSh4Xf0),
};

#undef SH4_ROW16
#undef SH4_REG
#undef SH4

static const uint8_t sh4_events[] = { EV_SH4_INTERRUPT, EV_SH4_EXCEPTION };

static const char* const sh4_cpu_actions[] = { "sh4-disassembly", "sh4-memory", "sh4-registers" };

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
	The system bus: 127 registers, each in one node. addr is the area 0 address; names and addresses are those of
	hw/holly/sb_regs.h. Every one is 32 bits wide and read through SystemBus::DebugRead.

	  dc.holly.intc      the 12 interrupt registers
	  dc.holly.dmac      the DMA channels with their counters: channel 2 of the SH4's DMAC, sort, G1 (the GD-ROM
	                     drive) and the four of G2 (the AICA, external devices 1 and 2, the development device)
	  dc.holly.sb.g1     what is left of the G1 block: the bus timing, which is write-only, and the system mode
	  dc.holly.sb.g2     what is left of the G2 block
	  dc.holly.sb.pvr    the PVR interface, its DMA with it
	  dc.holly.sb.misc   the system control block without the DMA and the interrupt registers
	  dc.maple           the maple block

	A register that is write-only as the emulator has it (the slot refuses a guest read, and SystemBus::DebugRead
	refuses it too) has no reader: it shows as "n/a", whatever the guest wrote. Two registers are behind a read
	function and have a reader of their own in the device: SB_ISTNRM and SB_FFST, whose guest read counts.
	A read-only register that the emulator never assigns reads 0.
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

// One of the four DMA channels of the G2 bus: its eight registers from `at`, its three counters from `counters`
#define SB_ROW(name, addr) { name, 32, 0, addr, SB }
#define G2_DMA(ch, at, counters) \
	SB_ROW("SB_" ch "STAG",  at + 0x00), \
	SB_ROW("SB_" ch "STAR",  at + 0x04), \
	SB_ROW("SB_" ch "LEN",   at + 0x08), \
	SB_ROW("SB_" ch "DIR",   at + 0x0C), \
	SB_ROW("SB_" ch "TSEL",  at + 0x10), \
	SB_ROW("SB_" ch "EN",    at + 0x14), \
	SB_ROW("SB_" ch "ST",    at + 0x18), \
	SB_ROW("SB_" ch "SUSP",  at + 0x1C), \
	SB_ROW("SB_" ch "STAGD", counters + 0x00), \
	SB_ROW("SB_" ch "STARD", counters + 0x04), \
	SB_ROW("SB_" ch "LEND",  counters + 0x08)

static const Register holly_dmac_regs[] = {
	// channel 2 of the SH4's DMAC, to the TA and the texture memory
	{ "SB_C2DSTAT",  32, 0,            0x005F6800, SB },
	{ "SB_C2DLEN",   32, 0,            0x005F6804, SB },
	{ "SB_C2DST",    32, 0,            0x005F6808, SB },

	// sort
	{ "SB_SDSTAW",   32, 0,            0x005F6810, SB },
	{ "SB_SDBAAW",   32, 0,            0x005F6814, SB },
	{ "SB_SDWLT",    32, 0,            0x005F6818, SB },
	{ "SB_SDLAS",    32, 0,            0x005F681C, SB },
	{ "SB_SDST",     32, 0,            0x005F6820, SB },
	{ "SB_SDDIV",    32, 0,            0x005F6860, SB },

	// G1: the GD-ROM drive
	{ "SB_GDSTAR",   32, 0,            0x005F7404, SB },
	{ "SB_GDLEN",    32, 0,            0x005F7408, SB },
	{ "SB_GDDIR",    32, 0,            0x005F740C, SB },
	{ "SB_GDEN",     32, 0,            0x005F7414, SB },
	{ "SB_GDST",     32, 0,            0x005F7418, SB },
	{ "SB_GDSTARD",  32, 0,            0x005F74F4, SB },
	{ "SB_GDLEND",   32, 0,            0x005F74F8, SB },

	// G2: the AICA, the external devices 1 and 2, the development device
	G2_DMA("AD", 0x005F7800, 0x005F78C0),
	G2_DMA("E1", 0x005F7820, 0x005F78D0),
	G2_DMA("E2", 0x005F7840, 0x005F78E0),
	G2_DMA("DD", 0x005F7860, 0x005F78F0),
};

#undef G2_DMA
#undef SB_ROW

static const Register holly_sb_g1_regs[] = {
	{ "SB_G1RRC",    32, kRegNoReader, 0x005F7480, SB },
	{ "SB_G1RWC",    32, kRegNoReader, 0x005F7484, SB },
	{ "SB_G1FRC",    32, kRegNoReader, 0x005F7488, SB },
	{ "SB_G1FWC",    32, kRegNoReader, 0x005F748C, SB },
	{ "SB_G1CRC",    32, kRegNoReader, 0x005F7490, SB },
	{ "SB_G1CWC",    32, kRegNoReader, 0x005F7494, SB },
	{ "SB_G1GDRC",   32, kRegNoReader, 0x005F74A0, SB },
	{ "SB_G1GDWC",   32, kRegNoReader, 0x005F74A4, SB },
	{ "SB_G1SYSM",   32, 0,            0x005F74B0, SB },
	{ "SB_G1CRDYC",  32, kRegNoReader, 0x005F74B4, SB },
	{ "SB_GDAPRO",   32, kRegNoReader, 0x005F74B8, SB },
};

static const Register holly_sb_g2_regs[] = {
	{ "SB_G2ID",     32, 0,            0x005F7880, SB },
	{ "SB_G2DSTO",   32, 0,            0x005F7890, SB },
	{ "SB_G2TRTO",   32, 0,            0x005F7894, SB },
	{ "SB_G2MDMTO",  32, 0,            0x005F7898, SB },
	{ "SB_G2MDMW",   32, 0,            0x005F789C, SB },
	{ "SB_G2APRO",   32, kRegNoReader, 0x005F78BC, SB },
};

static const Register holly_sb_pvr_regs[] = {
	{ "SB_PDSTAP",   32, 0,            0x005F7C00, SB },
	{ "SB_PDSTAR",   32, 0,            0x005F7C04, SB },
	{ "SB_PDLEN",    32, 0,            0x005F7C08, SB },
	{ "SB_PDDIR",    32, 0,            0x005F7C0C, SB },
	{ "SB_PDTSEL",   32, 0,            0x005F7C10, SB },
	{ "SB_PDEN",     32, 0,            0x005F7C14, SB },
	{ "SB_PDST",     32, 0,            0x005F7C18, SB },
	{ "SB_PDAPRO",   32, kRegNoReader, 0x005F7C80, SB },
	{ "SB_PDSTAPD",  32, 0,            0x005F7CF0, SB },
	{ "SB_PDSTARD",  32, 0,            0x005F7CF4, SB },
	{ "SB_PDLEND",   32, 0,            0x005F7CF8, SB },
};

static const Register holly_sb_misc_regs[] = {
	{ "SB_DBREQM",   32, 0,            0x005F6840, SB },
	{ "SB_BAVLWC",   32, 0,            0x005F6844, SB },
	{ "SB_C2DPRYC",  32, 0,            0x005F6848, SB },
	{ "SB_C2DMAXL",  32, 0,            0x005F684C, SB },
	{ "SB_TFREM",    32, 0,            0x005F6880, SB },
	{ "SB_LMMODE0",  32, 0,            0x005F6884, SB },
	{ "SB_LMMODE1",  32, 0,            0x005F6888, SB },
	{ "SB_FFST",     32, kRegComputed, 0x005F688C, SB },
	{ "SB_SFRES",    32, kRegNoReader, 0x005F6890, SB },
	{ "SB_SBREV",    32, 0,            0x005F689C, SB },
	{ "SB_RBSPLT",   32, 0,            0x005F68A0, SB },
	{ "SB_PDTNRM",   32, 0,            0x005F6940, SB },
	{ "SB_PDTEXT",   32, 0,            0x005F6944, SB },
	{ "SB_G2DTNRM",  32, 0,            0x005F6950, SB },
	{ "SB_G2DTEXT",  32, 0,            0x005F6954, SB },
};

static const Register maple_regs[] = {
	{ "SB_MDSTAR",   32, 0,            0x005F6C04, SB },
	{ "SB_MDTSEL",   32, 0,            0x005F6C10, SB },
	{ "SB_MDEN",     32, 0,            0x005F6C14, SB },
	{ "SB_MDST",     32, 0,            0x005F6C18, SB },
	{ "SB_MSYS",     32, 0,            0x005F6C80, SB },
	{ "SB_MST",      32, 0,            0x005F6C84, SB },
	{ "SB_MSHTCL",   32, 0,            0x005F6C88, SB },
	{ "SB_MDAPRO",   32, kRegNoReader, 0x005F6C8C, SB },
	{ "SB_MMSEL",    32, 0,            0x005F6CE8, SB },
	{ "SB_MTXDAD",   32, 0,            0x005F6CF4, SB },
	{ "SB_MRXDAD",   32, 0,            0x005F6CF8, SB },
	{ "SB_MRXDBD",   32, 0,            0x005F6CFC, SB },
};

#undef SB

/*
	The PVR's register file: the 68 registers of hw/pvr/pvr_regs.h that are no table, by the prefix of their
	name. dc.holly.ta has the 14 named TA_, dc.holly.spg the 9 named SPG_, dc.holly.core the others. addr is the
	area 0 address, 0x005F8000 and the offset of that header; every one is 32 bits wide. A guest read of the file
	is the array, and so is the reader's. The fog table, the object list pointers and the palette are in the
	same file and are no rows: the memory view shows them.
*/
#define PVR(name, offset) { name, 32, 0, 0x005F8000 + offset, MakeKey(kSrcPvr) }

static const Register holly_ta_regs[] = {
	PVR("TA_OL_BASE",        0x124),
	PVR("TA_ISP_BASE",       0x128),
	PVR("TA_OL_LIMIT",       0x12C),
	PVR("TA_ISP_LIMIT",      0x130),
	PVR("TA_NEXT_OPB",       0x134),
	PVR("TA_ISP_CURRENT",    0x138),
	PVR("TA_GLOB_TILE_CLIP", 0x13C),
	PVR("TA_ALLOC_CTRL",     0x140),
	PVR("TA_LIST_INIT",      0x144),
	PVR("TA_YUV_TEX_BASE",   0x148),
	PVR("TA_YUV_TEX_CTRL",   0x14C),
	PVR("TA_YUV_TEX_CNT",    0x150),
	PVR("TA_LIST_CONT",      0x160),
	PVR("TA_NEXT_OPB_INIT",  0x164),
};

static const Register holly_core_regs[] = {
	PVR("ID",                0x000),
	PVR("REVISION",          0x004),
	PVR("SOFTRESET",         0x008),
	PVR("STARTRENDER",       0x014),
	PVR("TEST_SELECT",       0x018),
	PVR("PARAM_BASE",        0x020),
	PVR("REGION_BASE",       0x02C),
	PVR("SPAN_SORT_CFG",     0x030),
	PVR("VO_BORDER_COL",     0x040),
	PVR("FB_R_CTRL",         0x044),
	PVR("FB_W_CTRL",         0x048),
	PVR("FB_W_LINESTRIDE",   0x04C),
	PVR("FB_R_SOF1",         0x050),
	PVR("FB_R_SOF2",         0x054),
	PVR("FB_R_SIZE",         0x05C),
	PVR("FB_W_SOF1",         0x060),
	PVR("FB_W_SOF2",         0x064),
	PVR("FB_X_CLIP",         0x068),
	PVR("FB_Y_CLIP",         0x06C),
	PVR("FPU_SHAD_SCALE",    0x074),
	PVR("FPU_CULL_VAL",      0x078),
	PVR("FPU_PARAM_CFG",     0x07C),
	PVR("HALF_OFFSET",       0x080),
	PVR("FPU_PERP_VAL",      0x084),
	PVR("ISP_BACKGND_D",     0x088),
	PVR("ISP_BACKGND_T",     0x08C),
	PVR("ISP_FEED_CFG",      0x098),
	PVR("SDRAM_REFRESH",     0x0A0),
	PVR("SDRAM_ARB_CFG",     0x0A4),
	PVR("SDRAM_CFG",         0x0A8),
	PVR("FOG_COL_RAM",       0x0B0),
	PVR("FOG_COL_VERT",      0x0B4),
	PVR("FOG_DENSITY",       0x0B8),
	PVR("FOG_CLAMP_MAX",     0x0BC),
	PVR("FOG_CLAMP_MIN",     0x0C0),
	PVR("TEXT_CONTROL",      0x0E4),
	PVR("VO_CONTROL",        0x0E8),
	PVR("VO_STARTX",         0x0EC),
	PVR("VO_STARTY",         0x0F0),
	PVR("SCALER_CTL",        0x0F4),
	PVR("PAL_RAM_CTRL",      0x108),
	PVR("FB_BURSTCTRL",      0x110),
	PVR("FB_C_SOF",          0x114),
	PVR("Y_COEFF",           0x118),
	PVR("PT_ALPHA_REF",      0x11C),
};

static const Register holly_spg_regs[] = {
	PVR("SPG_TRIGGER_POS",   0x0C4),
	PVR("SPG_HBLANK_INT",    0x0C8),
	PVR("SPG_VBLANK_INT",    0x0CC),
	PVR("SPG_CONTROL",       0x0D0),
	PVR("SPG_HBLANK",        0x0D4),
	PVR("SPG_LOAD",          0x0D8),
	PVR("SPG_VBLANK",        0x0DC),
	PVR("SPG_WIDTH",         0x0E0),
	PVR("SPG_STATUS",        0x10C),
};

#undef PVR

static const uint8_t holly_dmac_events[] = { EV_DMAC_START, EV_DMAC_END };
static const uint8_t holly_ta_events[]   = { EV_TA_LIST_INIT, EV_TA_LIST_END };
static const uint8_t holly_core_events[] = { EV_CORE_RENDER_START, EV_CORE_RENDER_END, EV_CORE_VBLANK };

static const char* const holly_ta_actions[]   = { "holly-ta" };
static const char* const holly_core_actions[] = { "holly-core" };

/*
	dc.aica.arm7. PC is the address of the next instruction. Rn_<mode> and SPSR_<mode> are the banked registers
	of a mode; for the mode the processor is in they show what R8..R14 and SPSR show.
	ENABLED is no register of the processor: 1 while the ARM7 is clocked, 0 while it is held in reset, as it is
	from a machine reset until the guest releases it and whenever the guest holds it again. It is what a client
	sees of the reset (the ARM7 call stack is empty exactly while it reads 0), and nothing writes it. It stands
	behind PC: the program counter is the first register of the node of every processor.
	Every row but ENABLED is writable (dbg::regs::Write, through SoundCPU::DebugSetReg): the guest holds and
	releases its ARM7, the debugger does not.
*/
#define ARM7(sel) MakeKey(kSrcArm7, sel)

static const Register arm7_regs[] = {
	{ "PC",       32, kRegWritable, kNoAddr, ARM7(kArm7Pc) },
	{ "ENABLED",  1,  kRegComputed, kNoAddr, ARM7(kArm7Enabled) },
	{ "R0",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 0) },
	{ "R1",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 1) },
	{ "R2",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 2) },
	{ "R3",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 3) },
	{ "R4",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 4) },
	{ "R5",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 5) },
	{ "R6",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 6) },
	{ "R7",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 7) },
	{ "R8",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 8) },
	{ "R9",       32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 9) },
	{ "R10",      32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 10) },
	{ "R11",      32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 11) },
	{ "R12",      32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 12) },
	{ "R13",      32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 13) },
	{ "R14",      32, kRegWritable, kNoAddr, ARM7(kArm7R0 + 14) },
	{ "CPSR",     32, kRegWritable, kNoAddr, ARM7(kArm7Cpsr) },
	{ "SPSR",     32, kRegWritable, kNoAddr, ARM7(kArm7Spsr) },
	{ "R13_USR",  32, kRegWritable, kNoAddr, ARM7(kArm7R13Usr) },
	{ "R14_USR",  32, kRegWritable, kNoAddr, ARM7(kArm7R14Usr) },
	{ "R8_FIQ",   32, kRegWritable, kNoAddr, ARM7(kArm7R8Fiq + 0) },
	{ "R9_FIQ",   32, kRegWritable, kNoAddr, ARM7(kArm7R8Fiq + 1) },
	{ "R10_FIQ",  32, kRegWritable, kNoAddr, ARM7(kArm7R8Fiq + 2) },
	{ "R11_FIQ",  32, kRegWritable, kNoAddr, ARM7(kArm7R8Fiq + 3) },
	{ "R12_FIQ",  32, kRegWritable, kNoAddr, ARM7(kArm7R8Fiq + 4) },
	{ "R13_FIQ",  32, kRegWritable, kNoAddr, ARM7(kArm7R8Fiq + 5) },
	{ "R14_FIQ",  32, kRegWritable, kNoAddr, ARM7(kArm7R8Fiq + 6) },
	{ "SPSR_FIQ", 32, kRegWritable, kNoAddr, ARM7(kArm7SpsrFiq) },
	{ "R13_IRQ",  32, kRegWritable, kNoAddr, ARM7(kArm7R13Irq) },
	{ "R14_IRQ",  32, kRegWritable, kNoAddr, ARM7(kArm7R14Irq) },
	{ "SPSR_IRQ", 32, kRegWritable, kNoAddr, ARM7(kArm7SpsrIrq) },
	{ "R13_SVC",  32, kRegWritable, kNoAddr, ARM7(kArm7R13Svc) },
	{ "R14_SVC",  32, kRegWritable, kNoAddr, ARM7(kArm7R14Svc) },
	{ "SPSR_SVC", 32, kRegWritable, kNoAddr, ARM7(kArm7SpsrSvc) },
	{ "R13_ABT",  32, kRegWritable, kNoAddr, ARM7(kArm7R13Abt) },
	{ "R14_ABT",  32, kRegWritable, kNoAddr, ARM7(kArm7R14Abt) },
	{ "SPSR_ABT", 32, kRegWritable, kNoAddr, ARM7(kArm7SpsrAbt) },
	{ "R13_UND",  32, kRegWritable, kNoAddr, ARM7(kArm7R13Und) },
	{ "R14_UND",  32, kRegWritable, kNoAddr, ARM7(kArm7R14Und) },
	{ "SPSR_UND", 32, kRegWritable, kNoAddr, ARM7(kArm7SpsrUnd) },
};

#undef ARM7

static const uint8_t aica_events[]     = { EV_AICA_INTERRUPT, EV_AICA_TIMER };
static const uint8_t aica_sgc_events[] = { EV_AICA_KEY_ON, EV_AICA_KEY_OFF, EV_AICA_LOOP };

static const char* const arm7_actions[] = { "arm7-disassembly", "arm7-memory", "arm7-registers" };
static const char* const sgc_actions[]  = { "sgc" };

/*
	dc.aica.dsp. STEP is the program step that executes next; MDEC_CT is the counter of the ring buffer, which
	counts down once a sample; RBP and RBL are fields of the word of 0x2804 as the DSP uses it
	(DspDebugState::rbpl), 12 and 2 bits of it.
	The widths are those of the registers: MDEC_CT has 16 bits on the hardware, and the DSP core keeps it in 16
	bits, free running: it wraps from 0 to 0xFFFF and is never reloaded with the length of the ring.
	Behind them the datapath of the DSP core, each a field of DspDebugState in the width of its register: ACC
	and Y_REG are signed, and a value is given as its bits under the width (FormatValue); the read latch and its
	copy, what an IWT stores (MEMVAL), the NOFL of the last two steps and the MIXS bank the DSP reads. They are
	what a continuation of the DSP from a stop needs, and they are read-only, as every row of a device is.
	Between two samples STEP reads 0; at a stop inside the DSP phase of a sample it is the step in front of
	which the debugger suspended it.
*/
static const Register dsp_regs[] = {
	{ "STEP",        8, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspStep) },
	{ "MDEC_CT",    16, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspMdecCt) },
	{ "RBP",        16, 0,            0x00702804, MakeKey(kSrcDsp, kDspRbp) },
	{ "RBL",         8, 0,            0x00702804, MakeKey(kSrcDsp, kDspRbl) },
	{ "ACC",        26, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspAcc) },
	{ "Y_REG",      24, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspYReg) },
	{ "FRC_REG",    13, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspFrcReg) },
	{ "ADRS_REG",   12, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspAdrsReg) },
	{ "LATCH",      16, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspLatch) },
	{ "LATCH_COPY", 16, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspLatchCopy) },
	{ "MEMVAL",     16, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspMemval) },
	{ "NOFL_PIPE",   2, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspNoflPipe) },
	{ "MIXS_BANK",   1, kRegComputed, kNoAddr,    MakeKey(kSrcDsp, kDspMixsBank) },
};

static const char* const dsp_actions[] = { "dsp-disassembly" };

/*
	The register file of the AICA, by field. A register of the file is a word of 16 bits in the low half of four
	bytes; addr is the area 0 address of the word a row is in (of its first word, for a row of two). The rows are
	read through AICA::DebugReadRegs, which gives the file as a read by the SH4 delivers it and changes nothing,
	but for the word of 0x2804 (below).

	Names are those of the data sheet, which are the members of the structs of hw/aica (CommonData_struct in
	aica.h, ChannelCommonData in sgc_if.cpp) in upper case, without the '$' of two of them. A quantity that the
	file has in two words is one row: SA, the start address of a channel, and DMEA, the wave memory address of the
	AICA's own DMA, whose bits 0 and 1 are no bits of the register and read 0.
*/
#define AICA_ROW(name, offset, shift, width) \
	{ name, width, 0, 0x00700000 + (offset), MakeKey(kSrcAica, AicaField(offset, shift)) }
#define AICA_ROW2(name, offset, shift, width, low_shift) \
	{ name, width, 0, 0x00700000 + (offset), MakeKey(kSrcAica, AicaField2(offset, shift, low_shift)) }
#define AICA_NO_READER(name, offset, shift, width) \
	{ name, width, kRegNoReader, 0x00700000 + (offset), MakeKey(kSrcAica, AicaField(offset, shift)) }

/*
	dc.aica.common: the fields of the 23 words of the common block, 0x2800 to 0x2C00.

	  RBP, RBL, TESTB0      the word of 0x2804 as the DSP uses it (DspDebugState::rbpl, DSP::DebugGetState), not
	                        what a read by the SH4 gives: the two are equal while the old DSP runs
	  MIEMP, MOEMP          set, as every read has them
	  EG, SGC, LP, CA       of the channel MSLC selects, worked out at the read; the read of the debugger does not
	                        clear the loop flag LP, as a read by the guest does
	  AR, VREG              the reset of the ARM7 and the video register as the SH4 reads them at 0x2C00: the
	                        device keeps them outside the file
	  SCILV0, SCILV1, SCILV2   write-only on the console, and the accessor gives 0 for them whatever is stored:
	                        no reader, "n/a"
	  SCIRE, MCIRE          a write clears bits of SCIPD and MCIPD and nothing is stored: no reader, "n/a"

	The words at 0x2D00 and 0x2D04 of the file are no rows: they are not the interrupt controller in front of the
	ARM7. That controller is state of the sound CPU, and its two registers are the last two rows, read through
	SoundCPU::DebugGetInterrupt: L, the level of the request as the ARM7 reads it at its 0x00802D00, and M, 1
	while a request is in service.
*/
static const Register aica_common_regs[] = {
	AICA_ROW("MVOL",    0x2800, 0,  4),
	AICA_ROW("VER",     0x2800, 4,  4),
	AICA_ROW("DAC18B",  0x2800, 8,  1),
	AICA_ROW("MEM8MB",  0x2800, 9,  1),
	AICA_ROW("MONO",    0x2800, 15, 1),

	AICA_ROW("RBP",     0x2804, 0,  12),
	AICA_ROW("RBL",     0x2804, 13, 2),
	AICA_ROW("TESTB0",  0x2804, 15, 1),

	AICA_ROW("MIBUF",   0x2808, 0,  8),
	AICA_ROW("MIEMP",   0x2808, 8,  1),
	AICA_ROW("MIFUL",   0x2808, 9,  1),
	AICA_ROW("MIOVF",   0x2808, 10, 1),
	AICA_ROW("MOEMP",   0x2808, 11, 1),
	AICA_ROW("MOFUL",   0x2808, 12, 1),

	AICA_ROW("MOBUF",   0x280C, 0,  8),
	AICA_ROW("MSLC",    0x280C, 8,  6),
	AICA_ROW("AFSET",   0x280C, 14, 1),

	AICA_ROW("EG",      0x2810, 0,  13),
	AICA_ROW("SGC",     0x2810, 13, 2),
	AICA_ROW("LP",      0x2810, 15, 1),

	AICA_ROW("CA",      0x2814, 0,  16),

	AICA_ROW("MRWINH",  0x2880, 0,  4),
	AICA_ROW("T",       0x2880, 4,  1),
	AICA_ROW("TSCD",    0x2880, 5,  3),
	AICA_ROW2("DMEA",   0x2880, 9,  23, 2),

	AICA_ROW("DRGA",    0x2888, 2,  13),
	AICA_ROW("DGATE",   0x2888, 15, 1),

	AICA_ROW("DEXE",    0x288C, 0,  1),
	AICA_ROW("DLG",     0x288C, 2,  13),
	AICA_ROW("DDIR",    0x288C, 15, 1),

	AICA_ROW("TIMA",    0x2890, 0,  8),
	AICA_ROW("TACTL",   0x2890, 8,  3),
	AICA_ROW("TIMB",    0x2894, 0,  8),
	AICA_ROW("TBCTL",   0x2894, 8,  3),
	AICA_ROW("TIMC",    0x2898, 0,  8),
	AICA_ROW("TCCTL",   0x2898, 8,  3),

	AICA_ROW("SCIEB",   0x289C, 0,  11),
	AICA_ROW("SCIPD",   0x28A0, 0,  11),
	AICA_NO_READER("SCIRE",  0x28A4, 0, 11),
	AICA_NO_READER("SCILV0", 0x28A8, 0, 8),
	AICA_NO_READER("SCILV1", 0x28AC, 0, 8),
	AICA_NO_READER("SCILV2", 0x28B0, 0, 8),

	AICA_ROW("MCIEB",   0x28B4, 0,  11),
	AICA_ROW("MCIPD",   0x28B8, 0,  11),
	AICA_NO_READER("MCIRE",  0x28BC, 0, 11),

	AICA_ROW("AR",      0x2C00, 0,  1),
	AICA_ROW("VREG",    0x2C00, 8,  2),

	{ "L", 8, kRegComputed, kNoAddr, MakeKey(kSrcArm7Intc, kArm7IntcLevel) },
	{ "M", 8, kRegComputed, kNoAddr, MakeKey(kSrcArm7Intc, kArm7IntcService) },
};

/*
	dc.aica.rtc: the clock behind 0x00710000. SECONDS is the counter, which the guest reads as two halves at +0
	and +4; WRITE_ENABLE is the bit the guest writes at +8, where it reads 0.
*/
static const Register aica_rtc_regs[] = {
	{ "SECONDS",      32, kRegComputed, 0x00710000, MakeKey(kSrcRtc, kRtcSeconds) },
	{ "WRITE_ENABLE", 1,  kRegComputed, 0x00710008, MakeKey(kSrcRtc, kRtcWriteEnable) },
};

/*
	dc.aica.sgc.0 .. dc.aica.sgc.63: the fields of the 18 words of a channel, which stands at channel * 0x80 of
	the file. 38 rows a channel, the same for every one: the tables are made by the two lists below.
*/
#define SGC_ROWS(at) \
	AICA_ROW2("SA",    (at) + 0x00, 0,  23, 0), \
	AICA_ROW("PCMS",   (at) + 0x00, 7,  2), \
	AICA_ROW("LPCTL",  (at) + 0x00, 9,  1), \
	AICA_ROW("SSCTL",  (at) + 0x00, 10, 1), \
	AICA_ROW("KYONB",  (at) + 0x00, 14, 1), \
	AICA_ROW("KYONEX", (at) + 0x00, 15, 1), \
	AICA_ROW("LSA",    (at) + 0x08, 0,  16), \
	AICA_ROW("LEA",    (at) + 0x0C, 0,  16), \
	AICA_ROW("AR",     (at) + 0x10, 0,  5), \
	AICA_ROW("D1R",    (at) + 0x10, 6,  5), \
	AICA_ROW("D2R",    (at) + 0x10, 11, 5), \
	AICA_ROW("RR",     (at) + 0x14, 0,  5), \
	AICA_ROW("DL",     (at) + 0x14, 5,  5), \
	AICA_ROW("KRS",    (at) + 0x14, 10, 4), \
	AICA_ROW("LPSLNK", (at) + 0x14, 14, 1), \
	AICA_ROW("FNS",    (at) + 0x18, 0,  10), \
	AICA_ROW("OCT",    (at) + 0x18, 11, 4), \
	AICA_ROW("ALFOS",  (at) + 0x1C, 0,  3), \
	AICA_ROW("ALFOWS", (at) + 0x1C, 3,  2), \
	AICA_ROW("PLFOS",  (at) + 0x1C, 5,  3), \
	AICA_ROW("PLFOWS", (at) + 0x1C, 8,  2), \
	AICA_ROW("LFOF",   (at) + 0x1C, 10, 5), \
	AICA_ROW("LFORE",  (at) + 0x1C, 15, 1), \
	AICA_ROW("ISEL",   (at) + 0x20, 0,  4), \
	AICA_ROW("IMXL",   (at) + 0x20, 4,  4), \
	AICA_ROW("DIPAN",  (at) + 0x24, 0,  5), \
	AICA_ROW("DISDL",  (at) + 0x24, 8,  4), \
	AICA_ROW("Q",      (at) + 0x28, 0,  5), \
	AICA_ROW("TL",     (at) + 0x28, 8,  8), \
	AICA_ROW("FLV0",   (at) + 0x2C, 0,  13), \
	AICA_ROW("FLV1",   (at) + 0x30, 0,  13), \
	AICA_ROW("FLV2",   (at) + 0x34, 0,  13), \
	AICA_ROW("FLV3",   (at) + 0x38, 0,  13), \
	AICA_ROW("FLV4",   (at) + 0x3C, 0,  13), \
	AICA_ROW("FD1R",   (at) + 0x40, 0,  5), \
	AICA_ROW("FAR",    (at) + 0x40, 8,  5), \
	AICA_ROW("FRR",    (at) + 0x44, 0,  5), \
	AICA_ROW("FD2R",   (at) + 0x44, 8,  5)

// X(n) for every channel, n as a decimal number
#define SGC_CHANNELS(X) \
	X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15) \
	X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) \
	X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39) X(40) X(41) X(42) X(43) X(44) X(45) X(46) X(47) \
	X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) X(56) X(57) X(58) X(59) X(60) X(61) X(62) X(63)

#define SGC_TABLE(n) static const Register sgc_##n##_regs[] = { SGC_ROWS((n) * 0x80) };
SGC_CHANNELS(SGC_TABLE)
#undef SGC_TABLE

#undef SGC_ROWS
#undef AICA_NO_READER
#undef AICA_ROW2
#undef AICA_ROW

/*
	dc.gdrom: the drive as its snapshot has it (GDRomDebugState, hw/gdrom/gdromv3.h), a row for every member. The
	register block at 0x005F7000 is not read: a read of it acknowledges the interrupt and takes data out of the
	buffer. The twelve bytes of the last packet command are three rows of four bytes, the first byte of each the
	highest of its value, so that the digits stand in the order of the packet. Every row shows "n/a" on a machine
	without the drive (the arcade boards).
*/
#define GD(name, width, member) { name, width, kRegComputed, kNoAddr, MakeKey(kSrcGdrom, member) }

static const Register gdrom_regs[] = {
	GD("STATE",                  32, kGdState),
	GD("DISC_TYPE",              32, kGdDiscType),
	GD("STATUS",                 8,  kGdStatus),
	GD("ERROR",                  8,  kGdError),
	GD("INTERRUPT_REASON",       8,  kGdInterruptReason),
	GD("FEATURES",               8,  kGdFeatures),
	GD("SECTOR_COUNT",           8,  kGdSectorCount),
	GD("SECTOR_NUMBER",          8,  kGdSectorNumber),
	GD("DRIVE_SELECT",           8,  kGdDriveSelect),
	GD("ATA_COMMAND",            8,  kGdAtaCommand),
	GD("BYTE_COUNT",             16, kGdByteCount),
	GD("PACKET_0_3",             32, kGdPacket0),
	GD("PACKET_4_7",             32, kGdPacket4),
	GD("PACKET_8_11",            32, kGdPacket8),
	GD("SENSE_KEY",              8,  kGdSenseKey),
	GD("SENSE_ASC",              8,  kGdSenseAsc),
	GD("SENSE_ASCQ",             8,  kGdSenseAscq),
	GD("CDDA_PLAYING",           1,  kGdCddaPlaying),
	GD("CDDA_REPEATS",           32, kGdCddaRepeats),
	GD("CDDA_CURRENT",           32, kGdCddaCurrent),
	GD("CDDA_START",             32, kGdCddaStart),
	GD("CDDA_END",               32, kGdCddaEnd),
	GD("READ_START_SECTOR",      32, kGdReadStartSector),
	GD("READ_REMAINING_SECTORS", 32, kGdReadRemainingSectors),
	GD("READ_SECTOR_TYPE",       32, kGdReadSectorType),
	GD("PIO_INDEX",              32, kGdPioIndex),
	GD("PIO_SIZE",               32, kGdPioSize),
};

#undef GD

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
#define SGC_NODE(n) \
	{ "dc.aica.sgc." #n, "dc.aica.sgc", "Channel " #n, "SGC Channel " #n, REGS(sgc_##n##_regs), NO_ACTIONS, NO_EVENTS, 0 },

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
	{ "dc.holly.dmac", "dc.holly", "DMA Controller", "peripheral", REGS(holly_dmac_regs), NO_ACTIONS, EVENTS(holly_dmac_events), 0 },
	{ "dc.holly.ta", "dc.holly", "TA", "Tile Accelerator", REGS(holly_ta_regs), ACTIONS(holly_ta_actions), EVENTS(holly_ta_events), 0 },
	{ "dc.holly.core", "dc.holly", "CORE", "Depth and Shading Engine", REGS(holly_core_regs), ACTIONS(holly_core_actions), EVENTS(holly_core_events), 0 },
	{ "dc.holly.sb", "dc.holly", "System Bus", "System bus registers", NO_REGS, NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.holly.sb.g1", "dc.holly.sb", "G1", "G1 bus", REGS(holly_sb_g1_regs), NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.holly.sb.g2", "dc.holly.sb", "G2", "G2 bus", REGS(holly_sb_g2_regs), NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.holly.sb.pvr", "dc.holly.sb", "PVR", "PVR interface", REGS(holly_sb_pvr_regs), NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.holly.sb.misc", "dc.holly.sb", "Misc", "Other system bus registers", REGS(holly_sb_misc_regs), NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.holly.spg", "dc.holly", "SPG", "Sync Pulse Generator", REGS(holly_spg_regs), NO_ACTIONS, NO_EVENTS, 0 },

	{ "dc.aica", "dc", "AICA", "Sound SoC", NO_REGS, NO_ACTIONS, EVENTS(aica_events), 0 },
	{ "dc.aica.arm7", "dc.aica", "ARM7", "ARM7DI CPU Core", REGS(arm7_regs), ACTIONS(arm7_actions), NO_EVENTS, kNodeBase },
	{ "dc.aica.sgc", "dc.aica", "SGC", "Sound Generation Core", NO_REGS, ACTIONS(sgc_actions), EVENTS(aica_sgc_events), 0 },
	SGC_CHANNELS(SGC_NODE)
	{ "dc.aica.dsp", "dc.aica", "DSP", "DSP VLIW Core", REGS(dsp_regs), ACTIONS(dsp_actions), NO_EVENTS, kNodeBase },
	{ "dc.aica.common", "dc.aica", "Common", "Common registers", REGS(aica_common_regs), NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.aica.rtc", "dc.aica", "RTC", "Real Time Clock", REGS(aica_rtc_regs), NO_ACTIONS, NO_EVENTS, 0 },

	{ "dc.system", "dc", "System", "Peripherals and timing", NO_REGS, NO_ACTIONS, NO_EVENTS, 0 },
	{ "dc.gdrom", "dc.system", "GD-ROM", "GD-ROM drive", REGS(gdrom_regs), NO_ACTIONS, EVENTS(gdrom_events), 0 },
	{ "dc.maple", "dc.system", "Maple", "Maple bus", REGS(maple_regs), NO_ACTIONS, EVENTS(maple_events), 0 },
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
