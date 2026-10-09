/*
	Test driver for the device tree tables (libswirl/debugger/dbg_regs_tables.cpp).

	regs_test tables              the tables by themselves
	regs_test sources LIBSWIRL    the on-chip module rows against hw/sh4/sh4_mmr.h and the system bus rows
	                              against hw/holly/sb_regs.h, both read as text
	regs_test panels SCHEMA       the panel ids against PANEL_IDS of the devtools UI (src/lib/debuggerSchema.ts)
	regs_test dump                prints the tree: one line per node and per register

	Exit code 0 when every check held.
*/
#include "dbg_regs.h"
#include "dbg_regs_private.h"
#include "dbg_events.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace dbg;
using namespace dbg::regs;

namespace
{
	int failures;
	int checks;

	bool Check(bool ok, const char* fmt, ...)
	{
		checks++;

		if (!ok)
		{
			char text[512];
			va_list args;
			va_start(args, fmt);
			vsnprintf(text, sizeof(text), fmt, args);
			va_end(args);

			if (++failures <= 60)
				fprintf(stderr, "FAIL %s\n", text);
		}

		return ok;
	}

	// The panel ids of the devtools UI (PanelId of its schema)
	const char* const panel_ids[] = {
		"documentation", "sh4-sim", "events", "events-breakpoints", "sh4-disassembly", "sh4-memory", "sh4-breakpoints",
		"bsc-registers", "ccn-registers", "cpg-registers", "dmac-registers", "intc-registers", "rtc-registers",
		"sci-registers", "scif-registers", "tmu-registers", "ubc-registers", "sq-contents", "icache-contents",
		"ocache-contents", "ocram-contents", "tlb-contents", "arm7-disassembly", "arm7-memory", "arm7-breakpoints",
		"holly-ta", "holly-core", "sgc", "dsp-disassembly", "dsp-breakpoints", "dsp-playground", "device-tree", "watches",
		"sh4-callstack", "arm7-callstack",
	};

	bool IsPanelId(const std::string& id)
	{
		for (const char* known : panel_ids)
		{
			if (id == known)
				return true;
		}

		return false;
	}

	/*
		The paths clients rely on: the 29 of the tree every client was written against, with their labels and
		panels, and the nodes this tree adds.
	*/
	struct Anchor {
		const char* path;
		const char* parent;
		const char* label;
		const char* actions;		// separated by blanks
	};

	const Anchor anchors[] = {
		{ "dc",             nullptr,       "Dreamcast",      "" },
		{ "dc.sh4",         "dc",          "SH4",            "" },
		{ "dc.sh4.cpu",     "dc.sh4",      "Core",           "sh4-disassembly sh4-memory" },
		{ "dc.sh4.pbus",    "dc.sh4",      "PBUS",           "" },
		{ "dc.sh4.bsc",     "dc.sh4.pbus", "BSC",            "bsc-registers" },
		{ "dc.sh4.ccn",     "dc.sh4.pbus", "CCN",            "ccn-registers" },
		{ "dc.sh4.cpg",     "dc.sh4.pbus", "CPG",            "cpg-registers" },
		{ "dc.sh4.dmac",    "dc.sh4.pbus", "DMAC",           "dmac-registers" },
		{ "dc.sh4.intc",    "dc.sh4.pbus", "INTC",           "intc-registers" },
		{ "dc.sh4.rtc",     "dc.sh4.pbus", "RTC",            "rtc-registers" },
		{ "dc.sh4.sci",     "dc.sh4.pbus", "SCI",            "sci-registers" },
		{ "dc.sh4.scif",    "dc.sh4.pbus", "SCIF",           "scif-registers" },
		{ "dc.sh4.tmu",     "dc.sh4.pbus", "TMU",            "tmu-registers" },
		{ "dc.sh4.ubc",     "dc.sh4.pbus", "UBC",            "ubc-registers" },
		{ "dc.sh4.sq",      "dc.sh4.pbus", "SQ",             "sq-contents" },
		{ "dc.sh4.icache",  "dc.sh4.pbus", "ICACHE",         "icache-contents" },
		{ "dc.sh4.ocache",  "dc.sh4.pbus", "OCACHE",         "ocache-contents" },
		{ "dc.sh4.ocram",   "dc.sh4.pbus", "OCRAM",          "ocram-contents" },
		{ "dc.sh4.tlb",     "dc.sh4.pbus", "TLB",            "tlb-contents" },
		{ "dc.holly",       "dc",          "Holly",          "" },
		{ "dc.holly.dmac",  "dc.holly",    "DMA Controller", "" },
		{ "dc.holly.ta",    "dc.holly",    "TA",             "holly-ta" },
		{ "dc.holly.core",  "dc.holly",    "CORE",           "holly-core" },
		{ "dc.aica",        "dc",          "AICA",           "" },
		{ "dc.aica.arm7",   "dc.aica",     "ARM7",           "arm7-disassembly arm7-memory" },
		{ "dc.aica.sgc",    "dc.aica",     "SGC",            "sgc" },
		{ "dc.aica.sgc.0",  "dc.aica.sgc", "Channel 0",      "" },
		{ "dc.aica.sgc.1",  "dc.aica.sgc", "Channel 1",      "" },
		{ "dc.aica.dsp",    "dc.aica",     "DSP",            "dsp-disassembly" },
	};

	const Anchor added[] = {
		{ "dc.holly.intc",  "dc.holly",    "INTC",           "" },
		{ "dc.system",      "dc",          "System",         "" },
		{ "dc.gdrom",       "dc.system",   "GD-ROM",         "" },
		{ "dc.maple",       "dc.system",   "Maple",          "" },
		{ "dc.timing",      "dc.system",   "Timing",         "" },
	};

	std::string Actions(const Node& node)
	{
		std::string all;

		for (uint32_t i = 0; i < node.actionCount; i++)
		{
			if (i)
				all += ' ';
			all += node.actions[i];
		}

		return all;
	}

	std::string Upper(std::string s)
	{
		for (char& c : s)
			c = (char)toupper((unsigned char)c);
		return s;
	}

	std::string Lower(std::string s)
	{
		for (char& c : s)
			c = (char)tolower((unsigned char)c);
		return s;
	}

	const Register* RegisterOf(const char* path, const char* name)
	{
		const Node* node = Find(path);

		if (node == nullptr)
			return nullptr;

		int index = FindRegister(*node, name);

		return index < 0 ? nullptr : &node->registers[index];
	}

	// The address range the registers of an on-chip module live in, by the last component of its path
	struct Module {
		const char* name;
		uint32_t base;		// P4
		uint32_t slots;		// of the module's array in the emulator
	};

	const Module modules[] = {
		{ "bsc",  0xFF800000, 19 }, { "ccn",  0xFF000000, 18 }, { "cpg", 0xFFC00000, 5 },  { "dmac", 0xFFA00000, 17 },
		{ "intc", 0xFFD00000, 5 },  { "rtc",  0xFFC80000, 16 }, { "sci", 0xFFE00000, 8 },  { "scif", 0xFFE80000, 10 },
		{ "tmu",  0xFFD80000, 12 }, { "ubc",  0xFF200000, 9 },
	};

	void CheckRegister(const Node& node, const Register& reg)
	{
		const char* path = node.path;
		const char* name = reg.name;

		Check(reg.name != nullptr && reg.name[0] != 0, "%s: a register without a name", path);
		if (reg.name == nullptr)
			return;

		for (const char* c = name; *c; c++)
			Check((*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_', "%s.%s: name is not upper case", path, name);

		Check(reg.width <= 64, "%s.%s: width %u", path, name, reg.width);
		Check((reg.flags & ~(kRegWritable | kRegComputed | kRegNoReader)) == 0, "%s.%s: unknown flag bits %02X", path, name, reg.flags);
		Check(!((reg.flags & kRegComputed) && (reg.flags & kRegNoReader)), "%s.%s: computed and without a reader", path, name);
		Check(!((reg.flags & kRegWritable) && (reg.flags & kRegNoReader)), "%s.%s: writable and without a reader", path, name);

		Source source = KeySource(reg.key);
		uint32_t selector = KeySelector(reg.key);

		Check(source > kSrcNone && source < kSrcCount, "%s.%s: key %08X has no source", path, name, reg.key);

		// free text is read-only and comes from the text reader, and only it does
		Check((reg.width == 0) == (source == kSrcText), "%s.%s: width %u with source %u", path, name, reg.width, (unsigned)source);
		if (reg.width == 0)
			Check((reg.flags & kRegWritable) == 0, "%s.%s: writable text", path, name);

		switch (source)
		{
		case kSrcText:
			Check(selector < kTextCount, "%s.%s: text selector %u", path, name, selector);
			Check(reg.addr == kNoAddr, "%s.%s: text with an address", path, name);
			break;

		case kSrcSh4:
			Check(selector < kSh4RegCount, "%s.%s: SH4 selector %u", path, name, selector);
			Check(reg.width == 32 && reg.addr == kNoAddr, "%s.%s: an SH4 register is 32 bits wide and has no address", path, name);
			Check((node.flags & kNodeSh4Context) != 0, "%s.%s: read from the SH4 context, but the node does not say so", path, name);
			break;

		case kSrcSh4Mmr:
			Check(selector < kMmrCount, "%s.%s: reader %u", path, name, selector);
			Check(reg.width == 8 || reg.width == 16 || reg.width == 32, "%s.%s: width %u", path, name, reg.width);
			Check((selector == kMmrNoReader || selector == kMmrNoSlot) == ((reg.flags & kRegNoReader) != 0), "%s.%s: reader and kRegNoReader disagree", path, name);
			Check((selector != kMmrData && selector != kMmrNoReader && selector != kMmrNoSlot) == ((reg.flags & kRegComputed) != 0),
				"%s.%s: reader and kRegComputed disagree", path, name);
			break;

		case kSrcSb:
			Check(reg.width == 32, "%s.%s: a system bus register is 32 bits wide", path, name);
			Check(reg.addr >= 0x005F6800 && reg.addr <= 0x005F7CFC && (reg.addr & 3) == 0, "%s.%s: address %08X", path, name, reg.addr);
			Check(reg.addr < 0x005F7000 || reg.addr > 0x005F70FF, "%s.%s: address %08X is in the GD-ROM block", path, name, reg.addr);
			break;

		case kSrcArm7:
			Check(selector <= 20 || (selector >= 26 && selector <= 44), "%s.%s: ARM7 register index %u", path, name, selector);
			Check(reg.width == 32 && reg.addr == kNoAddr, "%s.%s: an ARM7 register is 32 bits wide and has no address", path, name);
			break;

		case kSrcDsp:
			Check(selector < kDspRegCount, "%s.%s: DSP selector %u", path, name, selector);
			break;

		case kSrcTiming:
			Check(selector < kTimingRegCount, "%s.%s: timing selector %u", path, name, selector);
			Check(reg.width == 64 && reg.addr == kNoAddr, "%s.%s: a time is 64 bits wide and has no address", path, name);
			break;

		default:
			break;
		}
	}

	void TestTables()
	{
		uint32_t count = 0;
		const Node* nodes = Nodes(&count);

		Check(nodes != nullptr && count == sizeof(anchors) / sizeof(anchors[0]) + sizeof(added) / sizeof(added[0]),
			"%u nodes, expected %zu", count, sizeof(anchors) / sizeof(anchors[0]) + sizeof(added) / sizeof(added[0]));
		if (nodes == nullptr)
			return;

		uint32_t count2 = 0;
		Check(Nodes(&count2) == nodes && count2 == count, "Nodes() gives the same table every time");

		// structure
		std::map<std::string, uint32_t> index_of;
		std::map<std::string, uint32_t> last_child;

		for (uint32_t i = 0; i < count; i++)
		{
			const Node& node = nodes[i];
			std::string path = node.path ? node.path : "";

			Check(node.path != nullptr && path.rfind("dc", 0) == 0, "node %u: path \"%s\"", i, path.c_str());
			Check(path == Lower(path), "%s: path is not lower case", path.c_str());
			Check(index_of.count(path) == 0, "%s: path is there twice", path.c_str());
			Check(node.label != nullptr && node.label[0] != 0, "%s: no label", path.c_str());
			Check(node.description != nullptr, "%s: description is null", path.c_str());
			Check((node.flags & ~(kNodeBase | kNodeSh4Context)) == 0, "%s: unknown flag bits", path.c_str());

			if (i == 0)
				Check(path == "dc" && node.parent == nullptr, "the first node is the root dc");
			else if (Check(node.parent != nullptr, "%s: a second root", path.c_str()))
			{
				Check(index_of.count(node.parent) == 1, "%s: parent %s is not in front of it", path.c_str(), node.parent);

				// what stands between a node and the child of it that came last belongs to that child: the
				// list is the tree, written depth first
				if (last_child.count(node.parent))
				{
					for (uint32_t j = last_child[node.parent] + 1; j < i; j++)
					{
						std::string up = nodes[j].parent ? nodes[j].parent : "";
						bool below = false;

						while (!up.empty())
						{
							if (up == nodes[last_child[node.parent]].path)
							{
								below = true;
								break;
							}
							const Node* parent = Find(up.c_str());
							up = parent && parent->parent ? parent->parent : "";
						}

						Check(below, "%s stands between the children of %s", nodes[j].path, node.parent);
					}
				}

				last_child[node.parent] = i;
			}

			index_of[path] = i;

			Check(Find(node.path) == &node, "%s: Find() gives another node", path.c_str());

			// registers
			Check((node.registerCount == 0) == (node.registers == nullptr), "%s: registers and registerCount disagree", path.c_str());

			std::set<std::string> names;

			for (uint32_t r = 0; r < node.registerCount; r++)
			{
				const Register& reg = node.registers[r];

				CheckRegister(node, reg);

				std::string name = reg.name ? reg.name : "";

				Check(names.insert(Upper(name)).second, "%s.%s: name is there twice", path.c_str(), name.c_str());
				Check(FindRegister(node, name.c_str()) == (int)r, "%s.%s: FindRegister() gives another index", path.c_str(), name.c_str());
				Check(FindRegister(node, Lower(name).c_str()) == (int)r, "%s.%s: FindRegister() is not case-insensitive", path.c_str(), name.c_str());
			}

			Check(FindRegister(node, "NO_SUCH_REGISTER") == -1 && FindRegister(node, "") == -1, "%s: FindRegister() finds what is not there", path.c_str());

			// panels
			Check((node.actionCount == 0) == (node.actions == nullptr), "%s: actions and actionCount disagree", path.c_str());

			for (uint32_t a = 0; a < node.actionCount; a++)
			{
				Check(node.actions[a] != nullptr && IsPanelId(node.actions[a]), "%s: action \"%s\" is no panel id", path.c_str(),
					node.actions[a] ? node.actions[a] : "(null)");
				for (uint32_t b = 0; b < a; b++)
					Check(strcmp(node.actions[a], node.actions[b]) != 0, "%s: action \"%s\" is there twice", path.c_str(), node.actions[a]);
			}

			// events
			Check((node.eventCount == 0) == (node.events == nullptr), "%s: events and eventCount disagree", path.c_str());
		}

		Check(Find("dc.nothing") == nullptr && Find("") == nullptr && Find("DC") == nullptr, "Find() finds what is not there");

		// events: an id is listed by the node the event table names for it, by that node only and exactly once.
		// A node lists what the build raises: the one row that nothing raises is listed by none (a server accepts
		// a breakpoint on every event it lists)
		std::vector<int> listed(EV_ID_COUNT, 0);

		for (uint32_t i = 0; i < count; i++)
		{
			const Node& node = nodes[i];

			for (uint32_t e = 0; e < node.eventCount; e++)
			{
				uint8_t id = node.events[e];

				if (!Check(id < EV_ID_COUNT, "%s: event id %u", node.path, id))
					continue;

				listed[id]++;
				Check(strcmp(EventTable()[id].node, node.path) == 0, "%s lists %s, which belongs to %s", node.path,
					EventTable()[id].name, EventTable()[id].node);
			}
		}

		for (int id = 0; id < EV_ID_COUNT; id++)
		{
			Check(Find(EventTable()[id].node) != nullptr, "event %s names the node %s, which is not in the tree", EventTable()[id].name, EventTable()[id].node);

			if (id == EV_DEBUGGER_DSP_FORCED)
				Check(listed[(size_t)id] == 0, "event %s, which nothing raises, is listed %d times", EventTable()[id].name, listed[(size_t)id]);
			else
				Check(listed[(size_t)id] == 1, "event %s is listed %d times", EventTable()[id].name, listed[(size_t)id]);
		}

		// the paths clients rely on
		for (const Anchor* list : { anchors, added })
		{
			size_t n = list == anchors ? sizeof(anchors) / sizeof(anchors[0]) : sizeof(added) / sizeof(added[0]);

			for (size_t i = 0; i < n; i++)
			{
				const Anchor& anchor = list[i];
				const Node* node = Find(anchor.path);

				if (!Check(node != nullptr, "%s is missing", anchor.path))
					continue;

				Check(strcmp(node->label, anchor.label) == 0, "%s: label \"%s\", expected \"%s\"", anchor.path, node->label, anchor.label);
				Check(Actions(*node) == anchor.actions, "%s: actions \"%s\", expected \"%s\"", anchor.path, Actions(*node).c_str(), anchor.actions);
				Check((node->parent == nullptr) == (anchor.parent == nullptr) && (anchor.parent == nullptr || strcmp(node->parent, anchor.parent) == 0),
					"%s: parent %s", anchor.path, node->parent ? node->parent : "(none)");
			}
		}

		// the two levels a client shows unfolded carry no registers, except for the root
		for (uint32_t i = 1; i < count; i++)
		{
			if (strcmp(nodes[i].parent, "dc") == 0)
				Check(nodes[i].registerCount == 0, "%s is on the second level and has registers", nodes[i].path);
		}

		// the registers clients name: the program counter of every target, what the running tick carries, the
		// time of the contract tests
		struct { const char* path; const char* name; uint32_t width; } named[] = {
			{ "dc.sh4.cpu", "PC", 32 }, { "dc.sh4.cpu", "PR", 32 }, { "dc.sh4.cpu", "SR", 32 }, { "dc.sh4.cpu", "R15", 32 },
			{ "dc.sh4.cpu", "FPSCR", 32 }, { "dc.sh4.cpu", "R7_BANK", 32 }, { "dc.sh4.cpu", "FR15", 32 }, { "dc.sh4.cpu", "XF15", 32 },
			{ "dc.aica.arm7", "PC", 32 }, { "dc.aica.arm7", "R14", 32 }, { "dc.aica.arm7", "CPSR", 32 }, { "dc.aica.arm7", "SPSR_FIQ", 32 },
			{ "dc.aica.dsp", "STEP", 8 }, { "dc.aica.dsp", "MDEC_CT", 16 }, { "dc.aica.dsp", "RBP", 16 }, { "dc.aica.dsp", "RBL", 8 },
			{ "dc.timing", "MODE", 0 }, { "dc.timing", "SH4_CYCLES", 64 }, { "dc.timing", "SCHED_NOW", 64 },
			{ "dc.timing", "SH4_BACKEND", 0 }, { "dc.timing", "SH4_DEBUG_ENGINE", 0 },
			{ "dc", "SYSCLK", 0 }, { "dc", "BUILD", 0 },
			{ "dc.holly.intc", "SB_ISTNRM", 32 }, { "dc.holly.intc", "SB_IML6ERR", 32 },
		};

		for (const auto& want : named)
		{
			const Register* reg = RegisterOf(want.path, want.name);

			if (Check(reg != nullptr, "%s.%s is missing", want.path, want.name))
				Check(reg->width == want.width, "%s.%s: width %u, expected %u", want.path, want.name, reg->width, want.width);
		}

		Check(Find("dc.sh4.cpu") && Find("dc.sh4.cpu")->registerCount == 13 + 16 + 8 + 16 + 16, "dc.sh4.cpu has 69 registers");
		Check(Find("dc.sh4.cpu") && strcmp(Find("dc.sh4.cpu")->registers[0].name, "PC") == 0, "the first register of dc.sh4.cpu is PC");
		Check(Find("dc.aica.arm7") && strcmp(Find("dc.aica.arm7")->registers[0].name, "PC") == 0, "the first register of dc.aica.arm7 is PC");
		Check(Find("dc.aica.dsp") && strcmp(Find("dc.aica.dsp")->registers[0].name, "STEP") == 0, "the first register of dc.aica.dsp is STEP");
		Check(RegisterOf("dc.aica.arm7", "R15") == nullptr, "dc.aica.arm7 shows the next instruction as PC, not R15");

		// the nodes of a running tick
		for (uint32_t i = 0; i < count; i++)
		{
			std::string path = nodes[i].path;
			bool base = path == "dc.sh4.cpu" || path == "dc.aica.arm7" || path == "dc.aica.dsp" || path == "dc.timing";

			Check(((nodes[i].flags & kNodeBase) != 0) == base, "%s: kNodeBase", nodes[i].path);
			Check(((nodes[i].flags & kNodeSh4Context) != 0) == (path == "dc.sh4.cpu"), "%s: kNodeSh4Context", nodes[i].path);
		}

		// the on-chip modules: addresses inside the module, in rising order, every slot at most once
		for (const Module& module : modules)
		{
			std::string path = std::string("dc.sh4.") + module.name;
			const Node* node = Find(path.c_str());

			if (!Check(node != nullptr && node->registerCount != 0, "%s has no registers", path.c_str()))
				continue;

			Check(Actions(*node) == std::string(module.name) + "-registers", "%s: actions \"%s\"", path.c_str(), Actions(*node).c_str());

			uint32_t previous = 0;

			for (uint32_t r = 0; r < node->registerCount; r++)
			{
				const Register& reg = node->registers[r];

				Check(KeySource(reg.key) == kSrcSh4Mmr, "%s.%s: not an on-chip register", path.c_str(), reg.name);
				Check(reg.addr >= module.base && reg.addr < module.base + module.slots * 4 && (reg.addr & 3) == 0,
					"%s.%s: address %08X is outside %08X..%08X", path.c_str(), reg.name, reg.addr, module.base, module.base + module.slots * 4 - 4);
				Check(r == 0 || reg.addr > previous, "%s.%s: address %08X is not above the register in front of it", path.c_str(), reg.name, reg.addr);
				previous = reg.addr;
			}
		}

		// nothing but the modules has on-chip registers
		for (uint32_t i = 0; i < count; i++)
		{
			bool is_module = false;

			for (const Module& module : modules)
				is_module = is_module || std::string("dc.sh4.") + module.name == nodes[i].path;

			for (uint32_t r = 0; r < nodes[i].registerCount && !is_module; r++)
				Check(KeySource(nodes[i].registers[r].key) != kSrcSh4Mmr, "%s.%s: an on-chip register outside the modules", nodes[i].path, nodes[i].registers[r].name);
		}

		// the registers that are not plain data, by name
		struct { const char* path; const char* name; uint8_t flag; } special[] = {
			{ "dc.sh4.tmu", "TCNT0", kRegComputed }, { "dc.sh4.tmu", "TCNT1", kRegComputed }, { "dc.sh4.tmu", "TCNT2", kRegComputed },
			{ "dc.sh4.tmu", "TCPR2", kRegComputed }, { "dc.sh4.intc", "IPRD", kRegComputed }, { "dc.sh4.ccn", "CPU_VERSION", kRegComputed },
			{ "dc.sh4.ccn", "PRR", kRegComputed }, { "dc.sh4.scif", "SCFDR2", kRegComputed },
			{ "dc.sh4.scif", "SCFSR2", kRegNoReader }, { "dc.sh4.scif", "SCFRDR2", kRegNoReader }, { "dc.sh4.bsc", "PDTRA", kRegNoReader },
			{ "dc.holly.intc", "SB_ISTNRM", kRegComputed },
			// the SCI is not emulated
			{ "dc.sh4.sci", "SCSMR1", kRegNoReader }, { "dc.sh4.sci", "SCBRR1", kRegNoReader }, { "dc.sh4.sci", "SCSCR1", kRegNoReader },
			{ "dc.sh4.sci", "SCTDR1", kRegNoReader }, { "dc.sh4.sci", "SCSSR1", kRegNoReader }, { "dc.sh4.sci", "SCRDR1", kRegNoReader },
			{ "dc.sh4.sci", "SCSCMR1", kRegNoReader }, { "dc.sh4.sci", "SCSPTR1", kRegNoReader },
		};

		uint32_t special_count = 0;

		for (uint32_t i = 0; i < count; i++)
		{
			for (uint32_t r = 0; r < nodes[i].registerCount; r++)
			{
				Source source = KeySource(nodes[i].registers[r].key);

				if ((source == kSrcSh4Mmr || source == kSrcSb) && (nodes[i].registers[r].flags & (kRegComputed | kRegNoReader)))
					special_count++;
			}
		}

		Check(special_count == sizeof(special) / sizeof(special[0]), "%u registers are computed or without a reader, expected %zu",
			special_count, sizeof(special) / sizeof(special[0]));

		for (const auto& want : special)
		{
			const Register* reg = RegisterOf(want.path, want.name);

			if (Check(reg != nullptr, "%s.%s is missing", want.path, want.name))
				Check((reg->flags & (kRegComputed | kRegNoReader)) == want.flag, "%s.%s: flags %02X", want.path, want.name, reg->flags);
		}

		// the wire form of a value
		struct { uint32_t width; uint64_t value; const char* text; } formats[] = {
			{ 32, 0x8C010000, "0x8C010000" }, { 32, 0, "0x00000000" }, { 32, 0x1FFFFFFFFull, "0xFFFFFFFF" },
			{ 16, 0xBEEF, "0xBEEF" }, { 8, 0x1A5, "0xA5" }, { 1, 1, "0x1" }, { 1, 2, "0x0" }, { 2, 3, "0x3" },
			{ 12, 0xABC, "0xABC" }, { 12, 0xFABC, "0xABC" }, { 13, 0x1FFF, "0x1FFF" }, { 23, 0x7FFFFF, "0x7FFFFF" },
			{ 23, 0xFFFFFF, "0x7FFFFF" }, { 64, 0x0123456789ABCDEFull, "0x0123456789ABCDEF" }, { 64, ~0ull, "0xFFFFFFFFFFFFFFFF" },
			{ 33, 0x1FFFFFFFFull, "0x1FFFFFFFF" }, { 4, 0xA, "0xA" }, { 5, 0x1F, "0x1F" },
		};

		for (const auto& f : formats)
		{
			char out[32];
			memset(out, '#', sizeof(out));
			FormatValue(f.width, f.value, out);

			Check(strcmp(out, f.text) == 0, "FormatValue(%u, %llX) gives \"%.20s\", expected \"%s\"", f.width, (unsigned long long)f.value, out, f.text);
			Check(out[strlen(f.text) + 1] == '#', "FormatValue(%u) writes behind its terminator", f.width);
		}
	}

	/*
		The tables against the header files they were written from. A definition is the pair

		    //CCN PTEH 0xFF000000 0x1F000000 32 ...
		    #define CCN_PTEH_addr 0x1F000000

		in sh4_mmr.h and

		    //0x005F6900    SB_ISTNRM   RW  Normal interrupt status
		    #define SB_ISTNRM_addr 0x005F6900

		in sb_regs.h.
	*/
	bool ReadLines(const std::string& path, std::vector<std::string>* lines)
	{
		std::ifstream file(path);

		if (!file)
			return false;

		std::string line;
		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			lines->push_back(line);
		}

		return true;
	}

	std::vector<std::string> Words(const std::string& line)
	{
		std::istringstream stream(line);
		std::vector<std::string> words;
		std::string word;

		while (stream >> word)
			words.push_back(word);

		return words;
	}

	void TestSources(const std::string& libswirl)
	{
		// on-chip modules
		std::vector<std::string> lines;

		if (!Check(ReadLines(libswirl + "/hw/sh4/sh4_mmr.h", &lines), "cannot read %s/hw/sh4/sh4_mmr.h", libswirl.c_str()))
			return;

		struct Def { std::string name; uint32_t addr; uint32_t width; };
		std::map<std::string, std::vector<Def>> defs;		// by module, lower case

		for (size_t i = 1; i < lines.size(); i++)
		{
			std::vector<std::string> define = Words(lines[i]);

			if (define.size() != 3 || define[0] != "#define" || define[1].size() < 6 || define[1].compare(define[1].size() - 5, 5, "_addr") != 0)
				continue;

			std::string symbol = define[1].substr(0, define[1].size() - 5);
			uint32_t addr = (uint32_t)strtoul(define[2].c_str(), nullptr, 16);

			if (symbol.size() > 5 && symbol.compare(symbol.size() - 5, 5, "_BASE") == 0)
				continue;

			// the two rows without a width in their comment are the 32-bit registers the manual does not have
			if (symbol == "CPU_VERSION" || symbol == "CCN_PRR")
			{
				defs["ccn"].push_back({ symbol == "CPU_VERSION" ? "CPU_VERSION" : "PRR", addr, 32 });
				continue;
			}

			std::vector<std::string> comment = Words(lines[i - 1]);

			if (comment.size() < 5 || comment[0].rfind("//", 0) != 0)
				continue;

			std::string module = comment[0].substr(2);
			std::string name = comment[1];

			if (!Check(symbol == module + "_" + name, "sh4_mmr.h: %s does not follow the comment of %s %s", symbol.c_str(), module.c_str(), name.c_str()))
				continue;

			// the write-only SDRAM mode registers have a page of their own; the debug interface is not on the chip's bus
			if (name == "SDMR2" || name == "SDMR3" || module == "UDI")
				continue;

			defs[Lower(module)].push_back({ name, addr, (uint32_t)atoi(comment[4].c_str()) });
		}

		uint32_t total = 0;

		for (const Module& module : modules)
		{
			std::string path = std::string("dc.sh4.") + module.name;
			const Node* node = Find(path.c_str());
			const std::vector<Def>& list = defs[module.name];

			if (!Check(node != nullptr, "%s is missing", path.c_str()))
				continue;

			Check(node->registerCount == list.size(), "%s has %u registers, sh4_mmr.h defines %zu", path.c_str(), node->registerCount, list.size());

			for (uint32_t r = 0; r < node->registerCount && r < list.size(); r++)
			{
				const Register& reg = node->registers[r];
				const Def& def = list[r];

				Check(def.name == reg.name, "%s: register %u is %s, sh4_mmr.h has %s there", path.c_str(), r, reg.name, def.name.c_str());
				Check((def.addr | 0xE0000000) == reg.addr, "%s.%s: address %08X, sh4_mmr.h has %08X", path.c_str(), reg.name, reg.addr, def.addr);
				Check(def.width == reg.width, "%s.%s: width %u, sh4_mmr.h has %u", path.c_str(), reg.name, reg.width, def.width);
			}

			total += node->registerCount;
		}

		Check(total == 114, "%u on-chip registers, expected 114", total);
		Check(defs.size() == sizeof(modules) / sizeof(modules[0]), "sh4_mmr.h defines registers of %zu modules, the tree has %zu", defs.size(),
			sizeof(modules) / sizeof(modules[0]));

		// system bus
		lines.clear();

		if (!Check(ReadLines(libswirl + "/hw/holly/sb_regs.h", &lines), "cannot read %s/hw/holly/sb_regs.h", libswirl.c_str()))
			return;

		std::map<std::string, uint32_t> sb;

		for (const std::string& line : lines)
		{
			std::vector<std::string> define = Words(line);

			if (define.size() == 3 && define[0] == "#define" && define[1].rfind("SB_", 0) == 0 && define[1].size() > 5 &&
				define[1].compare(define[1].size() - 5, 5, "_addr") == 0)
				sb[define[1].substr(0, define[1].size() - 5)] = (uint32_t)strtoul(define[2].c_str(), nullptr, 16);
		}

		Check(sb.size() > 100, "sb_regs.h defines %zu registers", sb.size());

		uint32_t count = 0;
		const Node* nodes = Nodes(&count);
		uint32_t sb_rows = 0;

		for (uint32_t i = 0; i < count; i++)
		{
			for (uint32_t r = 0; r < nodes[i].registerCount; r++)
			{
				const Register& reg = nodes[i].registers[r];

				if (KeySource(reg.key) != kSrcSb)
					continue;

				sb_rows++;

				if (Check(sb.count(reg.name) == 1, "%s.%s: sb_regs.h has no such register", nodes[i].path, reg.name))
					Check(sb[reg.name] == reg.addr, "%s.%s: address %08X, sb_regs.h has %08X", nodes[i].path, reg.name, reg.addr, sb[reg.name]);
			}
		}

		// the interrupt registers of the system bus: three status registers and three masks for each of three levels
		const Node* intc = Find("dc.holly.intc");

		Check(intc != nullptr && intc->registerCount == 12 && sb_rows == 12, "dc.holly.intc has the 12 interrupt registers, and no other node has system bus registers");

		for (const auto& entry : sb)
		{
			if (entry.first.rfind("SB_IST", 0) == 0 || entry.first.rfind("SB_IML", 0) == 0)
				Check(RegisterOf("dc.holly.intc", entry.first.c_str()) != nullptr, "dc.holly.intc has no %s", entry.first.c_str());
		}
	}

	// PANEL_IDS = { NAME: "id", ... } of the schema
	void TestPanels(const std::string& schema)
	{
		std::vector<std::string> lines;

		if (!Check(ReadLines(schema, &lines), "cannot read %s", schema.c_str()))
			return;

		std::set<std::string> ids;
		bool inside = false;

		for (const std::string& line : lines)
		{
			if (line.find("export const PANEL_IDS") != std::string::npos)
			{
				inside = true;
				continue;
			}

			if (!inside)
				continue;

			if (line.find('}') != std::string::npos)
				break;

			size_t first = line.find('"');
			size_t last = line.rfind('"');

			if (first != std::string::npos && last > first)
				ids.insert(line.substr(first + 1, last - first - 1));
		}

		Check(!ids.empty(), "%s has no PANEL_IDS", schema.c_str());

		for (const char* id : panel_ids)
			Check(ids.count(id) == 1, "panel id \"%s\" is not one of the UI", id);
		Check(ids.size() == sizeof(panel_ids) / sizeof(panel_ids[0]), "the UI has %zu panel ids, this test knows %zu", ids.size(),
			sizeof(panel_ids) / sizeof(panel_ids[0]));

		uint32_t count = 0;
		const Node* nodes = Nodes(&count);

		for (uint32_t i = 0; i < count; i++)
		{
			for (uint32_t a = 0; a < nodes[i].actionCount; a++)
				Check(ids.count(nodes[i].actions[a]) == 1, "%s: action \"%s\" is not a panel id of the UI", nodes[i].path, nodes[i].actions[a]);
		}
	}

	void Dump()
	{
		uint32_t count = 0;
		const Node* nodes = Nodes(&count);
		uint32_t registers = 0;

		for (uint32_t i = 0; i < count; i++)
		{
			const Node& node = nodes[i];

			printf("%-16s parent=%-12s label=\"%s\" registers=%u actions=[%s] events=[", node.path, node.parent ? node.parent : "-", node.label,
				node.registerCount, Actions(node).c_str());
			for (uint32_t e = 0; e < node.eventCount; e++)
				printf("%s%s", e ? " " : "", EventTable()[node.events[e]].name);
			printf("]%s%s\n", node.flags & kNodeBase ? " base" : "", node.flags & kNodeSh4Context ? " sh4-context" : "");

			for (uint32_t r = 0; r < node.registerCount; r++)
			{
				const Register& reg = node.registers[r];

				printf("    %-12s width=%-2u", reg.name, reg.width);
				if (reg.addr != kNoAddr)
					printf(" addr=%08X", reg.addr);
				printf("%s%s%s\n", reg.flags & kRegComputed ? " computed" : "", reg.flags & kRegNoReader ? " no-reader" : "",
					reg.flags & kRegWritable ? " writable" : "");
			}

			registers += node.registerCount;
		}

		printf("%u nodes, %u registers\n", count, registers);
	}
}

int main(int argc, char** argv)
{
	std::string mode = argc > 1 ? argv[1] : "";

	if (mode == "tables" && argc == 2)
		TestTables();
	else if (mode == "sources" && argc == 3)
		TestSources(argv[2]);
	else if (mode == "panels" && argc == 3)
		TestPanels(argv[2]);
	else if (mode == "dump" && argc == 2)
	{
		Dump();
		return 0;
	}
	else
	{
		fprintf(stderr, "usage: regs_test tables | sources <libswirl dir> | panels <debuggerSchema.ts> | dump\n");
		return 2;
	}

	printf("%s: %d checks, %d failed\n", mode.c_str(), checks, failures);
	return failures == 0 && checks != 0 ? 0 : 1;
}
