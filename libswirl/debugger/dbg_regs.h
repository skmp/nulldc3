/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include <cstdint>

/*
	dbg::regs: the device tree. The tables (dbg_regs_tables.cpp) are constant and link without the emulator;
	the readers and writers (dbg_regs.cpp) do not. This header uses <cstdint> names so that the tables build
	on their own; uint32_t is the tree's u32.

	The tree is a flat list of nodes; each names the node it is shown under. A node carries registers (name,
	width), the events it lists and the panels it offers. Clients take structure and names from
	debugger.describe only; ticks carry values.
*/

namespace dbg { namespace regs {

const uint32_t kNoAddr = 0xFFFFFFFF;

// Register::flags
enum : uint8_t {
	kRegWritable = 1,		// Write() can set it
	kRegComputed = 2,		// the value comes from a dedicated reader, not from a register array
	kRegNoReader = 4,		// no reader without side effects exists: Read() fails, the value shows as "n/a"
};

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
	Values. On (E), or on a thread that holds L while no emulator thread exists. No guest register handler is
	ever called: a register whose read has side effects is read from its array or by a dedicated reader, and
	has kRegNoReader when neither exists.
*/

// Reads register `index` of the node.
//   width > 0   *value receives the value, bits above width clear; text is not used and may be null
//   width == 0  text receives the text, NUL-terminated and cut to n bytes; value is not used and may be null
// Returns false, with the outputs untouched, when the value cannot be given: no machine exists, index is
// out of range, the register has kRegNoReader, or the device has nothing to read from yet. The caller then
// shows "n/a".
bool Read(const Node& node, uint32_t index, uint64_t* value, char* text, uint32_t n);

// Writes register `index` of the node; bits of value above its width are ignored. (E) in the park context
// only (inside a CC_WRITE call). A write of the SH4 PC also tells the breakpoint state
// (dbg::sh4::OnPcWritten); a write of SR or FPSCR applies its effects (register banks, host FP mode).
// Returns 0, kErrNotWritable (-32013) for a register without kRegWritable or an index out of range, or
// kErrNotSupported (-32004) while no writer exists at all.
int Write(const Node& node, uint32_t index, uint64_t value);

} }
