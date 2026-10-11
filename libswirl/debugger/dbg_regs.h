/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include <cstdint>

/*
	dbg::regs: the device tree. The tables (dbg_regs_tables.cpp) are constant and link without the emulator;
	the readers and writers (dbg_regs.cpp) do not. This header uses <cstdint> names so that the tables build
	on their own; uint32_t is the tree's u32. A source that is built without the emulator includes neither
	debugger.h nor dbg_emu.h.

	The tree is a flat list of nodes; each names the node it is shown under. A node carries registers (name,
	width), the events it lists and the panels it offers. Clients take structure and names from
	debugger.describe only; ticks carry values.
*/

namespace dbg { namespace regs {

const uint32_t kNoAddr = 0xFFFFFFFF;

// Register::flags
enum : uint8_t {
	kRegWritable = 1,		// Write() can set it; debugger.describe says so (flags.writable)
	kRegComputed = 2,		// the value comes from a dedicated reader, not from a register array
	kRegNoReader = 4,		// no reader without side effects exists: Read() fails, the value shows as "n/a"
};

// The SH4 on-chip registers behind a read function have no accessor in hw/sh4, and the function is never
// called. What dbg_regs.cpp gives for each of them, as a register and as bytes of dbg::mem:
//   TMU TCNT0..2                  kRegComputed: the down counter as its read function works it out, from
//                                 tmu_ch_base, tmu_shift and tmu_mask (non-static globals of
//                                 hw/sh4/modules/tmu.cpp, which dbg_regs.cpp declares extern itself) and the
//                                 scheduler's time, sh4_sched_now64()
//   CCN CPU_VERSION               kRegComputed: the constant its read function returns, 0x040205C1
//   CCN PRR, INTC IPRD,           kRegComputed: 0, which their read functions return whatever the state
//   SCIF SCFDR2, TMU TCPR2
//   BSC PDTRA                     kRegNoReader, "n/a": the value is put together from PCTRA, the cable setting
//                                 and the last value written to PDTRA, which only the module keeps
//   SCIF SCFSR2, SCIF SCFRDR2     kRegNoReader, "n/a": the read looks at, or takes a byte from, the serial
//                                 input of the host
// The registers of the SCI are kRegNoReader as well, for another reason: the emulator has none of them.

// Node::flags
enum : uint8_t {
	kNodeBase       = 1,	// carried by every running tick; other nodes only by full ticks
	kNodeSh4Context = 2,	// read from the SH4 context: stale in the slice hook on a backend without
							// SH4DBG_CTX_AT_SLICE
};

struct Register {
	const char* name;		// upper case, unique within its node
	uint8_t width;			// bits, 1..64; 0: free text, read-only
	uint8_t flags;
	uint32_t addr;			// guest address of a memory-mapped register (SH4 on-chip: the P4 address; system
							// bus, PVR, AICA: the area 0 address), kNoAddr for any other
	uint32_t key;			// how the reader finds the value; private to the tables and the reader
};

struct Node {
	const char* path;				// lower case, unique: "dc.sh4.cpu"
	const char* parent;				// path of the node it is shown under, null for the root "dc". It is not
									// derived from the dots of path
	const char* label;
	const char* description;		// "" when there is none, never null
	const Register* registers;		// registerCount entries, in the order describe and ticks give them
	uint32_t registerCount;
	const char* const* actions;		// actionCount panel ids: "sh4-disassembly"
	uint32_t actionCount;
	const uint8_t* events;			// eventCount ids (dbg::EventId, dbg_events.h): the events debugger.describe lists
									// for this node. Only events this build raises, each with this node's path as
									// its EventInfo::node
	uint32_t eventCount;
	uint8_t flags;
};

/*
	Tables. Any thread, any lifetime state; everything returned is constant from the first call to the end of
	the process.
*/

// All nodes, a node always behind the node it names as parent, the nodes of one parent in display order.
const Node* Nodes(uint32_t* count);

// The node with this path (exact match), or null.
const Node* Find(const char* path);

// The index of the register with this name in the node, compared without regard to case; -1 when the node
// has none.
int FindRegister(const Node& node, const char* name);

// The wire form of a value of a register with width > 0: "0x" and ceil(width / 4) upper-case hex digits,
// NUL-terminated; at most 19 bytes are written to out. Bits of value above width are ignored.
void FormatValue(uint32_t width, uint64_t value, char* out);

/*
	Values. On (E), or on a thread that holds L while no emulator thread exists; Write() on (E) only. Read()
	never calls a guest register handler: a register whose read has side effects is read from its array or by
	a dedicated reader, and has kRegNoReader when neither exists. Write() applies the effects that belong to
	the register it writes (below) and nothing else of the guest.
*/

// Reads register `index` of the node.
//   width > 0   *value receives the value, bits above width clear; text is not used and may be null
//   width == 0  text receives the text, NUL-terminated and cut to n bytes; value is not used and may be null
// Returns false, with the outputs untouched, when the value cannot be given: no machine exists, index is
// out of range, the register has kRegNoReader, or the device has nothing to read from yet. The caller then
// shows "n/a".
bool Read(const Node& node, uint32_t index, uint64_t* value, char* text, uint32_t n);

// Writes register `index` of the node; bits of value above its width are ignored. (E) in the park context
// only (inside a CC_WRITE call: the park loop, or a service excursion of a running machine). Never blocks.
// Writable are the rows with kRegWritable: in M2 the registers of dc.sh4.cpu and those of dc.aica.arm7 but
// ENABLED. No other row has a writer.
//   dc.sh4.cpu     a store into the SH4 context. PC: a value with bit 0 set is refused (nothing executes at an
//                  odd address, and the block cache is indexed by pc >> 1); an accepted one also tells the
//                  breakpoint state (dbg::sh4::OnPcWritten: the owed remainder goes; the resume exemption is
//                  decided at the leave from the PC as it then is) and, when it differs from the PC it
//                  replaces, empties the code cache of the SH4 (SuperH4::ResetCache()): a block is found by
//                  (pc >> 1) under FPCB_MASK with no address in its key, so the slot of the written PC, or
//                  of an address the code there goes on to, can hold the block of another address
//                  (addresses a multiple of 32 MiB apart share a slot: main memory at 0x0C000000,
//                  0x8C000000 and 0xAC000000 does, and so do the boot ROM and main memory; the mirror
//                  16 MiB up is the same memory in other slots), and a
//                  resume would run that block where a step decodes what is at the PC. SR goes through
//                  sh4_sr_SetFull() and
//                  UpdateSR() (the register banks swap). SR is stored under 0x700083F3, and with MD clear
//                  RB is stored as 0: the register reads back what was stored, not what was written; so do
//                  FPSCR (FPSCR_MASK) and the CPSR of the ARM7 (its flags, I, F and the mode: 0xF00000DF).
//                  When UpdateSR() says an interrupt can be taken (one is pending and the SR as written
//                  lets it in: also when the write changed neither),
//                  UpdateINTC() is called, which under the latch only records the acceptance as owed: it is
//                  made when the park context is left. At a stop that was latched at a slice end the
//                  acceptance is owed already and nothing moves; at a code breakpoint of the SH4 it moves
//                  from the next slice end to the leave, as it does behind an LDC of the guest. FPSCR is
//                  masked with FPSCR_MASK and goes through
//                  UpdateFPSCR() (the FP banks swap, the host FP mode of the emulator thread follows). When
//                  PR, SZ or the rounding mode changed, the code cache of the SH4 is emptied
//                  (SuperH4::ResetCache()): a compiled block has the FP mode of the moment it was compiled
//                  and is found by its address alone. An owed remainder is data and stays
//   dc.aica.arm7   SoundCPU::DebugSetReg (hw/arm7/SoundCPU.h): CPSR through the mode switch (it reads back
//                  as the core composes it, above); PC as the
//                  address of the next instruction, stored with bits 0 and 1 clear, which also tells the
//                  breakpoint state (dbg::arm7::OnPcWritten). An accepted write that stores another
//                  address than the PC had empties the code cache of the ARM7
//                  (SoundCPU::InvalidateJitCache()): a recompiler of the ARM7 finds its code by the
//                  address under the wave-memory mask, and the address written can be another mirror
//                  of one it has compiled. Also while the ARM7 is held in reset and while
//                  a sound burst is suspended (the ARM7 stands between two instructions then)
// Returns 0. Returns kErrNotWritable (-32013), having changed nothing, for a register without kRegWritable,
// for an index out of range, with no machine, and when the device refuses the value (an odd PC of the SH4;
// the CPSR of the ARM7 with mode bits that are no mode).
int Write(const Node& node, uint32_t index, uint64_t value);

} }
