/*
	Test driver for the device tree tables (libswirl/debugger/dbg_regs_tables.cpp).

	regs_test tables              the tables by themselves
	regs_test sources LIBSWIRL    the on-chip module rows against hw/sh4/sh4_mmr.h, the system bus rows against
	                              hw/holly/sb_regs.h and the PVR rows against hw/pvr/pvr_regs.h, all read as text
	regs_test panels SCHEMA       the panel ids against PANEL_IDS of the devtools UI (src/lib/debuggerSchema.ts)
	regs_test dump                prints the tree: one line per node and per register

	Exit code 0 when every check held.
*/
#include "dbg_regs.h"
#include "dbg_regs_private.h"
#include "dbg_events.h"

#include <algorithm>
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
		"sh4-registers", "bsc-registers", "ccn-registers", "cpg-registers", "dmac-registers", "intc-registers",
		"rtc-registers", "sci-registers", "scif-registers", "tmu-registers", "ubc-registers", "sq-contents",
		"icache-contents", "ocache-contents", "ocram-contents", "tlb-contents", "arm7-disassembly", "arm7-memory",
		"arm7-breakpoints", "arm7-registers", "holly-ta", "holly-core", "sgc", "dsp-disassembly", "dsp-breakpoints",
		"dsp-playground", "device-tree", "watches", "sh4-callstack", "arm7-callstack",
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
		The nodes: the 29 paths of the tree every client was written against, with their labels and panels; the
		five that milestone M1 added; the eight that milestone M2 adds, and the channels 2 to 63 of the sound
		generator, which the test counts out. 104 nodes, and no other.
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
		{ "dc.sh4.cpu",     "dc.sh4",      "Core",           "sh4-disassembly sh4-memory sh4-registers" },
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
		{ "dc.aica.arm7",   "dc.aica",     "ARM7",           "arm7-disassembly arm7-memory arm7-registers" },
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

	const Anchor added_m2[] = {
		{ "dc.holly.sb",      "dc.holly",    "System Bus",   "" },
		{ "dc.holly.sb.g1",   "dc.holly.sb", "G1",           "" },
		{ "dc.holly.sb.g2",   "dc.holly.sb", "G2",           "" },
		{ "dc.holly.sb.pvr",  "dc.holly.sb", "PVR",          "" },
		{ "dc.holly.sb.misc", "dc.holly.sb", "Misc",         "" },
		{ "dc.holly.spg",     "dc.holly",    "SPG",          "" },
		{ "dc.aica.common",   "dc.aica",     "Common",       "" },
		{ "dc.aica.rtc",      "dc.aica",     "RTC",          "" },
	};

	const uint32_t kChannels = 64;

	// The registers of every node that has some (M2-ROUND 3.6.1), the 64 channels apart: 38 each
	const struct { const char* path; uint32_t registers; } node_rows[] = {
		{ "dc", 2 }, { "dc.sh4.cpu", 69 },
		{ "dc.sh4.bsc", 16 }, { "dc.sh4.ccn", 16 }, { "dc.sh4.cpg", 5 }, { "dc.sh4.dmac", 17 }, { "dc.sh4.intc", 5 }, { "dc.sh4.rtc", 16 },
		{ "dc.sh4.sci", 8 }, { "dc.sh4.scif", 10 }, { "dc.sh4.tmu", 12 }, { "dc.sh4.ubc", 9 },
		{ "dc.holly.intc", 12 }, { "dc.holly.dmac", 60 }, { "dc.holly.sb.g1", 11 }, { "dc.holly.sb.g2", 6 }, { "dc.holly.sb.pvr", 11 },
		{ "dc.holly.sb.misc", 15 }, { "dc.holly.ta", 14 }, { "dc.holly.core", 45 }, { "dc.holly.spg", 9 },
		{ "dc.aica.arm7", 41 }, { "dc.aica.dsp", 4 }, { "dc.aica.common", 49 }, { "dc.aica.rtc", 2 },
		{ "dc.gdrom", 27 }, { "dc.maple", 12 }, { "dc.timing", 5 },
	};

	const uint32_t kChannelRows = 38;

	// The twelve register panels: a panel shows the node that offers its id, so each is the action of one node
	const struct { const char* id; const char* path; } register_panels[] = {
		{ "sh4-registers", "dc.sh4.cpu" }, { "arm7-registers", "dc.aica.arm7" },
		{ "bsc-registers", "dc.sh4.bsc" }, { "ccn-registers", "dc.sh4.ccn" }, { "cpg-registers", "dc.sh4.cpg" }, { "dmac-registers", "dc.sh4.dmac" },
		{ "intc-registers", "dc.sh4.intc" }, { "rtc-registers", "dc.sh4.rtc" }, { "sci-registers", "dc.sh4.sci" }, { "scif-registers", "dc.sh4.scif" },
		{ "tmu-registers", "dc.sh4.tmu" }, { "ubc-registers", "dc.sh4.ubc" },
	};

	/*
		The register file of the AICA as the structs of hw/aica have it (CommonData_struct in aica.h,
		ChannelCommonData in sgc_if.cpp), written out again in the notation of a data sheet: for every word the rows
		that have bits in it, NAME<high:low> for the bits of the word. A row that the file has in two words stands in
		both, with the bits of the row in front: SA=22:16<6:0> is the bits 22 to 16 of SA in the bits 6 to 0 of the
		word. '!' marks a row without a reader. The tables are compared with this bit for bit.
	*/
	struct WordSpec {
		uint32_t offset;
		const char* rows;
	};

	const WordSpec common_spec[] = {
		{ 0x2800, "MONO<15:15> MEM8MB<9:9> DAC18B<8:8> VER<7:4> MVOL<3:0>" },
		{ 0x2804, "TESTB0<15:15> RBL<14:13> RBP<11:0>" },
		{ 0x2808, "MOFUL<12:12> MOEMP<11:11> MIOVF<10:10> MIFUL<9:9> MIEMP<8:8> MIBUF<7:0>" },
		{ 0x280C, "AFSET<14:14> MSLC<13:8> MOBUF<7:0>" },
		{ 0x2810, "LP<15:15> SGC<14:13> EG<12:0>" },
		{ 0x2814, "CA<15:0>" },
		{ 0x2880, "DMEA=22:16<15:9> TSCD<7:5> T<4:4> MRWINH<3:0>" },
		{ 0x2884, "DMEA=15:2<15:2>" },
		{ 0x2888, "DGATE<15:15> DRGA<14:2>" },
		{ 0x288C, "DDIR<15:15> DLG<14:2> DEXE<0:0>" },
		{ 0x2890, "TACTL<10:8> TIMA<7:0>" },
		{ 0x2894, "TBCTL<10:8> TIMB<7:0>" },
		{ 0x2898, "TCCTL<10:8> TIMC<7:0>" },
		{ 0x289C, "SCIEB<10:0>" },
		{ 0x28A0, "SCIPD<10:0>" },
		{ 0x28A4, "!SCIRE<10:0>" },
		{ 0x28A8, "!SCILV0<7:0>" },
		{ 0x28AC, "!SCILV1<7:0>" },
		{ 0x28B0, "!SCILV2<7:0>" },
		{ 0x28B4, "MCIEB<10:0>" },
		{ 0x28B8, "MCIPD<10:0>" },
		{ 0x28BC, "!MCIRE<10:0>" },
		{ 0x2C00, "VREG<9:8> AR<0:0>" },
	};

	// offsets from the start of a channel, which is at channel * 0x80 of the file
	const WordSpec channel_spec[] = {
		{ 0x00, "KYONEX<15:15> KYONB<14:14> SSCTL<10:10> LPCTL<9:9> PCMS<8:7> SA=22:16<6:0>" },
		{ 0x04, "SA=15:0<15:0>" },
		{ 0x08, "LSA<15:0>" },
		{ 0x0C, "LEA<15:0>" },
		{ 0x10, "D2R<15:11> D1R<10:6> AR<4:0>" },
		{ 0x14, "LPSLNK<14:14> KRS<13:10> DL<9:5> RR<4:0>" },
		{ 0x18, "OCT<14:11> FNS<9:0>" },
		{ 0x1C, "LFORE<15:15> LFOF<14:10> PLFOWS<9:8> PLFOS<7:5> ALFOWS<4:3> ALFOS<2:0>" },
		{ 0x20, "IMXL<7:4> ISEL<3:0>" },
		{ 0x24, "DISDL<11:8> DIPAN<4:0>" },
		{ 0x28, "TL<15:8> Q<4:0>" },
		{ 0x2C, "FLV0<12:0>" },
		{ 0x30, "FLV1<12:0>" },
		{ 0x34, "FLV2<12:0>" },
		{ 0x38, "FLV3<12:0>" },
		{ 0x3C, "FLV4<12:0>" },
		{ 0x40, "FAR<12:8> FD1R<4:0>" },
		{ 0x44, "FD2R<12:8> FRR<4:0>" },
	};

	// One bit of a row: where it is in the file, and which bit of the row's value it is
	struct Bit {
		uint32_t offset;	// of the word
		uint32_t word_bit;
		uint32_t value_bit;

		bool operator<(const Bit& other) const
		{
			if (offset != other.offset)
				return offset < other.offset;
			if (word_bit != other.word_bit)
				return word_bit < other.word_bit;
			return value_bit < other.value_bit;
		}

		bool operator==(const Bit& other) const { return offset == other.offset && word_bit == other.word_bit && value_bit == other.value_bit; }
	};

	struct SpecRow {
		std::vector<Bit> bits;
		bool no_reader = false;
		uint32_t first_offset = 0xFFFFFFFF;
	};

	// The rows of a list of words, by name, in the order they first occur word by word from the low bits up
	std::map<std::string, SpecRow> ParseSpec(const WordSpec* words, size_t count, uint32_t base, std::vector<std::string>* order)
	{
		std::map<std::string, SpecRow> rows;

		for (size_t w = 0; w < count; w++)
		{
			std::istringstream stream(words[w].rows);
			std::vector<std::string> items;
			std::string item;

			while (stream >> item)
				items.push_back(item);

			// the lists are written from bit 15 down; the rows of a node stand from bit 0 up
			for (size_t i = items.size(); i-- > 0; )
			{
				item = items[i];

				bool no_reader = item[0] == '!';

				if (no_reader)
					item = item.substr(1);

				size_t open = item.find('<'), equals = item.find('=');
				std::string name = item.substr(0, equals != std::string::npos ? equals : open);
				unsigned high = 0, low = 0, value_high = 0, value_low = 0;

				sscanf(item.c_str() + open, "<%u:%u>", &high, &low);

				if (equals != std::string::npos)
					sscanf(item.c_str() + equals, "=%u:%u", &value_high, &value_low);
				else
					value_high = high - low;

				SpecRow& row = rows[name];

				if (row.bits.empty())
					order->push_back(name);

				row.no_reader = row.no_reader || no_reader;
				if (row.first_offset == 0xFFFFFFFF)
					row.first_offset = base + words[w].offset;

				for (uint32_t b = 0; b <= high - low; b++)
					row.bits.push_back({ base + words[w].offset, low + b, value_low + b });

				Check(high >= low && high <= 15 && value_high - value_low == high - low, "the list of the test: %s", items[i].c_str());
			}
		}

		for (auto& entry : rows)
			std::sort(entry.second.bits.begin(), entry.second.bits.end());

		return rows;
	}

	// The bits of a row of the AICA's register file, as its key gives them (dbg_regs_private.h)
	std::vector<Bit> KeyBits(const Register& reg)
	{
		const uint32_t selector = KeySelector(reg.key);
		const uint32_t offset = AicaFieldOffset(selector), shift = AicaFieldShift(selector);
		std::vector<Bit> bits;

		if (!AicaFieldTwoWords(selector))
		{
			for (uint32_t b = 0; b < reg.width; b++)
				bits.push_back({ offset, shift + b, b });
		}
		else
		{
			for (uint32_t b = 16; b < reg.width; b++)
				bits.push_back({ offset, shift + b - 16, b });
			for (uint32_t b = AicaFieldLowShift(selector); b < 16; b++)
				bits.push_back({ offset + 4, b, b });
		}

		std::sort(bits.begin(), bits.end());
		return bits;
	}

	// The rows of a node against the rows of a list of words
	void CheckAicaNode(const char* path, const WordSpec* words, size_t count, uint32_t base, uint32_t others)
	{
		const Node* node = Find(path);
		std::vector<std::string> order;
		std::map<std::string, SpecRow> spec = ParseSpec(words, count, base, &order);

		if (!Check(node != nullptr, "%s is missing", path))
			return;

		Check(node->registerCount == spec.size() + others, "%s has %u registers, the register file has %zu fields there%s", path, node->registerCount, spec.size(),
			others ? " and the node two that are not in the file" : "");

		std::map<uint32_t, uint32_t> used;		// word offset -> bits that a row has
		uint32_t at = 0;

		for (uint32_t r = 0; r < node->registerCount; r++)
		{
			const Register& reg = node->registers[r];

			if (KeySource(reg.key) != kSrcAica)
				continue;

			// in the order of the file
			if (at < order.size())
				Check(order[at] == reg.name, "%s: field %u is %s, the register file has %s there", path, at, reg.name, order[at].c_str());
			at++;

			std::vector<Bit> bits = KeyBits(reg);

			// inside its word, and no bit of a word in two rows
			for (const Bit& bit : bits)
			{
				Check(bit.word_bit <= 15 && bit.value_bit < reg.width, "%s.%s: bit %u of the word at %04X, bit %u of %u", path, reg.name, bit.word_bit, bit.offset,
					bit.value_bit, reg.width);
				Check((used[bit.offset] & (1u << (bit.word_bit & 15))) == 0, "%s.%s: bit %u of the word at %04X is in another row as well", path, reg.name, bit.word_bit,
					bit.offset);
				used[bit.offset] |= 1u << (bit.word_bit & 15);
			}

			auto found = spec.find(reg.name);

			if (!Check(found != spec.end(), "%s.%s: the register file has no such field", path, reg.name))
				continue;

			const SpecRow& want = found->second;

			Check(bits == want.bits, "%s.%s: its bits are not those of the register file (%zu bits from bit %u of the word at %04X; expected %zu from bit %u of %04X)", path,
				reg.name, bits.size(), bits.empty() ? 0 : bits[0].word_bit, bits.empty() ? 0 : bits[0].offset, want.bits.size(), want.bits[0].word_bit, want.bits[0].offset);
			const uint32_t top = std::max_element(want.bits.begin(), want.bits.end(), [](const Bit& a, const Bit& b) { return a.value_bit < b.value_bit; })->value_bit;

			Check(reg.width == top + 1, "%s.%s: width %u, the highest bit of the register is bit %u", path, reg.name, reg.width, top);
			Check(reg.addr == 0x00700000 + want.first_offset, "%s.%s: address %08X, its word is at %08X", path, reg.name, reg.addr, 0x00700000 + want.first_offset);
			Check(((reg.flags & kRegNoReader) != 0) == want.no_reader, "%s.%s: flags %02X, %s", path, reg.name, reg.flags,
				want.no_reader ? "the register has no reader" : "the register reads");
		}

		Check(at == spec.size(), "%s has %u fields of the register file, expected %zu", path, at, spec.size());
	}

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
			Check(selector == 0, "%s.%s: a system bus register has no selector", path, name);
			break;

		case kSrcPvr:
			Check(reg.width == 32 && reg.flags == 0 && selector == 0, "%s.%s: a PVR register is 32 bits wide, plain data and has no selector", path, name);
			Check(reg.addr >= 0x005F8000 && reg.addr <= 0x005F9FFC && (reg.addr & 3) == 0, "%s.%s: address %08X", path, name, reg.addr);
			break;

		case kSrcAica:
		{
			const uint32_t offset = AicaFieldOffset(selector), shift = AicaFieldShift(selector);

			Check((selector & ~kAicaFieldSelectorMask) == 0, "%s.%s: selector %06X", path, name, selector);
			Check((reg.flags & ~kRegNoReader) == 0, "%s.%s: a field of the AICA's register file is plain data, or has no reader (flags %02X)", path, name, reg.flags);
			Check(reg.addr == 0x00700000 + offset, "%s.%s: address %08X, its word is at offset %04X of the file", path, name, reg.addr, offset);

			if (AicaFieldTwoWords(selector))
			{
				Check(reg.width > 16 && shift + (reg.width - 16) <= 16 && offset + 4 < 0x8000, "%s.%s: %u bits of two words, from bit %u of the first", path, name, reg.width,
					shift);
			}
			else
			{
				Check(reg.width >= 1 && shift + reg.width <= 16, "%s.%s: %u bits from bit %u of a word of 16", path, name, reg.width, shift);
				Check(AicaFieldLowShift(selector) == 0, "%s.%s: a field of one word with the selector of a second", path, name);
			}
			break;
		}

		case kSrcArm7Intc:
			Check(selector < kArm7IntcRegCount && strcmp(path, "dc.aica.common") == 0, "%s.%s: a register of the ARM7's interrupt controller (selector %u)", path, name,
				selector);
			Check(reg.width == 8 && reg.addr == kNoAddr && reg.flags == kRegComputed, "%s.%s: 8 bits, no address, computed", path, name);
			break;

		case kSrcRtc:
			Check(selector < kRtcRegCount && strcmp(path, "dc.aica.rtc") == 0, "%s.%s: a register of the clock (selector %u)", path, name, selector);
			Check(reg.flags == kRegComputed && (reg.addr & 0xFFFFFFF3) == 0x00710000, "%s.%s: computed, at 0x00710000", path, name);
			break;

		case kSrcGdrom:
			Check(selector < kGdromRegCount && strcmp(path, "dc.gdrom") == 0, "%s.%s: a member of the drive's snapshot (selector %u)", path, name, selector);
			Check(reg.flags == kRegComputed && reg.addr == kNoAddr && reg.width >= 1 && reg.width <= 32, "%s.%s: computed, no address, up to 32 bits", path, name);
			break;

		case kSrcArm7:
			// one row is no register of the processor: dc.aica.arm7.ENABLED, whether the ARM7 is clocked. Its
			// selector is no register index, it is one bit that a reader of its own gives, and nothing writes it
			if (selector == kArm7Enabled)
			{
				Check(strcmp(path, "dc.aica.arm7") == 0 && strcmp(name, "ENABLED") == 0, "%s.%s: has the selector of dc.aica.arm7.ENABLED", path, name);
				Check(reg.width == 1 && reg.addr == kNoAddr, "%s.%s: whether the ARM7 is clocked is 1 bit wide and has no address", path, name);
				Check((reg.flags & (kRegWritable | kRegComputed | kRegNoReader)) == kRegComputed, "%s.%s: flags %02X, expected computed and read-only", path, name,
					reg.flags);
				break;
			}

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

		// 29 + 5 + 8, and the channels 2 to 63: 0 and 1 are among the 29
		const size_t expected_nodes = sizeof(anchors) / sizeof(anchors[0]) + sizeof(added) / sizeof(added[0]) + sizeof(added_m2) / sizeof(added_m2[0]) + kChannels - 2;

		Check(nodes != nullptr && count == expected_nodes && expected_nodes == 104, "%u nodes, expected %zu, which is 104", count, expected_nodes);
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
		// A node lists what the build raises, and every row of the table is raised (a server accepts a breakpoint
		// on every event it lists)
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

			Check(listed[(size_t)id] == 1, "event %s is listed %d times", EventTable()[id].name, listed[(size_t)id]);
		}

		// the paths clients rely on, and with them every node of the tree: its parent, its label, its actions
		{
			std::vector<Anchor> all(anchors, anchors + sizeof(anchors) / sizeof(anchors[0]));
			std::vector<std::string> channel_paths(kChannels), channel_labels(kChannels);

			all.insert(all.end(), added, added + sizeof(added) / sizeof(added[0]));
			all.insert(all.end(), added_m2, added_m2 + sizeof(added_m2) / sizeof(added_m2[0]));

			for (uint32_t channel = 0; channel < kChannels; channel++)
			{
				channel_paths[channel] = "dc.aica.sgc." + std::to_string(channel);
				channel_labels[channel] = "Channel " + std::to_string(channel);

				// the first two are among the 29
				if (channel >= 2)
					all.push_back({ channel_paths[channel].c_str(), "dc.aica.sgc", channel_labels[channel].c_str(), "" });
			}

			std::set<std::string> known;

			for (const Anchor& anchor : all)
			{
				const Node* node = Find(anchor.path);

				Check(known.insert(anchor.path).second, "the list of the test has %s twice", anchor.path);

				if (!Check(node != nullptr, "%s is missing", anchor.path))
					continue;

				Check(strcmp(node->label, anchor.label) == 0, "%s: label \"%s\", expected \"%s\"", anchor.path, node->label, anchor.label);
				Check(Actions(*node) == anchor.actions, "%s: actions \"%s\", expected \"%s\"", anchor.path, Actions(*node).c_str(), anchor.actions);
				Check((node->parent == nullptr) == (anchor.parent == nullptr) && (anchor.parent == nullptr || strcmp(node->parent, anchor.parent) == 0),
					"%s: parent %s", anchor.path, node->parent ? node->parent : "(none)");
			}

			for (uint32_t i = 0; i < count; i++)
				Check(known.count(nodes[i].path) == 1, "%s is a node the test does not know", nodes[i].path);

			// the channels stand one behind the other, in their order, each with the label and the description of its number
			const Node* first = Find("dc.aica.sgc.0");

			for (uint32_t channel = 0; first != nullptr && channel < kChannels; channel++)
			{
				const bool inside = first + channel < nodes + count;

				Check(inside && channel_paths[channel] == first[channel].path && "SGC " + channel_labels[channel] == first[channel].description,
					"the node %u behind dc.aica.sgc.0 is %s (\"%s\")", channel, inside ? first[channel].path : "none", inside ? first[channel].description : "");
			}
		}

		// how many registers a node has: the table of the round, and no register on any other node
		{
			std::map<std::string, uint32_t> expected;
			uint32_t total = 0;

			for (const auto& row : node_rows)
				expected[row.path] = row.registers;
			for (uint32_t channel = 0; channel < kChannels; channel++)
				expected["dc.aica.sgc." + std::to_string(channel)] = kChannelRows;

			for (uint32_t i = 0; i < count; i++)
			{
				const uint32_t want = expected.count(nodes[i].path) ? expected[nodes[i].path] : 0;

				Check(nodes[i].registerCount == want, "%s has %u registers, expected %u", nodes[i].path, nodes[i].registerCount, want);
				total += nodes[i].registerCount;
			}

			Check(total == 2940, "the tree has %u registers, expected 2940", total);
		}

		// a register panel is the action of one node
		for (const auto& panel : register_panels)
		{
			uint32_t offered = 0;
			std::string by;

			for (uint32_t i = 0; i < count; i++)
			{
				for (uint32_t a = 0; a < nodes[i].actionCount; a++)
				{
					if (strcmp(nodes[i].actions[a], panel.id) == 0)
					{
						offered++;
						by = nodes[i].path;
					}
				}
			}

			Check(offered == 1 && by == panel.path, "the panel %s is offered by %u nodes (%s), expected by %s alone", panel.id, offered, by.c_str(), panel.path);
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
			{ "dc.aica.arm7", "ENABLED", 1 },
			{ "dc.aica.dsp", "STEP", 8 }, { "dc.aica.dsp", "MDEC_CT", 16 }, { "dc.aica.dsp", "RBP", 16 }, { "dc.aica.dsp", "RBL", 8 },
			{ "dc.timing", "MODE", 0 }, { "dc.timing", "SH4_CYCLES", 64 }, { "dc.timing", "SCHED_NOW", 64 },
			{ "dc.timing", "SH4_BACKEND", 0 }, { "dc.timing", "SH4_DEBUG_ENGINE", 0 },
			{ "dc", "SYSCLK", 0 }, { "dc", "BUILD", 0 },
			{ "dc.holly.intc", "SB_ISTNRM", 32 }, { "dc.holly.intc", "SB_IML6ERR", 32 },
			// milestone M2
			{ "dc.holly.dmac", "SB_C2DST", 32 }, { "dc.holly.dmac", "SB_DDLEND", 32 }, { "dc.holly.sb.misc", "SB_FFST", 32 }, { "dc.maple", "SB_MDST", 32 },
			{ "dc.holly.ta", "TA_LIST_INIT", 32 }, { "dc.holly.core", "ID", 32 }, { "dc.holly.core", "STARTRENDER", 32 }, { "dc.holly.spg", "SPG_STATUS", 32 },
			{ "dc.aica.common", "MVOL", 4 }, { "dc.aica.common", "MONO", 1 }, { "dc.aica.common", "T", 1 }, { "dc.aica.common", "TSCD", 3 },
			{ "dc.aica.common", "DMEA", 23 }, { "dc.aica.common", "SCIPD", 11 }, { "dc.aica.common", "SCILV0", 8 }, { "dc.aica.common", "AR", 1 },
			{ "dc.aica.common", "L", 8 }, { "dc.aica.common", "M", 8 }, { "dc.aica.common", "RBP", 12 }, { "dc.aica.common", "RBL", 2 },
			{ "dc.aica.rtc", "SECONDS", 32 }, { "dc.aica.rtc", "WRITE_ENABLE", 1 },
			{ "dc.aica.sgc.0", "SA", 23 }, { "dc.aica.sgc.0", "KYONB", 1 }, { "dc.aica.sgc.63", "AR", 5 }, { "dc.aica.sgc.63", "FD2R", 5 },
			{ "dc.gdrom", "STATUS", 8 }, { "dc.gdrom", "PACKET_8_11", 32 },
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

		// what a client sees of the reset of the ARM7: ENABLED, the register behind the program counter, and the
		// one row of the tree that is read as the clocked flag of the sound CPU. With it the node has 41 registers:
		// PC, ENABLED, R0..R14, CPSR, SPSR, two of the user bank, eight of the FIQ bank and three of each of the
		// four other banks
		{
			const Node* arm7 = Find("dc.aica.arm7");
			const Register* enabled = RegisterOf("dc.aica.arm7", "ENABLED");
			uint32_t clocked_rows = 0;

			for (uint32_t i = 0; i < count; i++)
			{
				for (uint32_t r = 0; r < nodes[i].registerCount; r++)
				{
					if (nodes[i].registers[r].key == MakeKey(kSrcArm7, kArm7Enabled))
						clocked_rows++;
				}
			}

			Check(arm7 && arm7->registerCount == 1 + 1 + 15 + 2 + 2 + 8 + 4 * 3, "dc.aica.arm7 has %u registers, expected 41", arm7 ? arm7->registerCount : 0);
			Check(arm7 && arm7->registerCount > 1 && strcmp(arm7->registers[1].name, "ENABLED") == 0, "the register behind PC of dc.aica.arm7 is ENABLED");
			Check(enabled && enabled->key == MakeKey(kSrcArm7, kArm7Enabled) && clocked_rows == 1,
				"dc.aica.arm7.ENABLED is read as the clocked flag of the sound CPU, and it alone (%u rows are)", clocked_rows);
			Check(enabled && (enabled->flags & kRegWritable) == 0, "dc.aica.arm7.ENABLED is read-only");
		}

		// The rows that have a writer (milestone M2): the registers of the two processors, which are every row of
		// dc.sh4.cpu and every row of dc.aica.arm7 but ENABLED, and no other row of the tree
		{
			uint32_t sh4_rows = 0, arm7_rows = 0, other_rows = 0;
			std::string first_other;

			for (uint32_t i = 0; i < count; i++)
			{
				std::string path = nodes[i].path;

				for (uint32_t r = 0; r < nodes[i].registerCount; r++)
				{
					const Register& reg = nodes[i].registers[r];
					bool processor = path == "dc.sh4.cpu" || (path == "dc.aica.arm7" && strcmp(reg.name, "ENABLED") != 0);

					if (!(reg.flags & kRegWritable))
					{
						Check(!processor, "%s.%s: a register of a processor without kRegWritable", nodes[i].path, reg.name);
						continue;
					}

					if (path == "dc.sh4.cpu")
						sh4_rows++;
					else if (processor)
						arm7_rows++;
					else if (other_rows++ == 0)
						first_other = path + "." + reg.name;

					// what a writer is handed: 32 bits, from the context of its processor
					Check(reg.width == 32 && (KeySource(reg.key) == kSrcSh4 || KeySource(reg.key) == kSrcArm7) && (reg.flags & (kRegComputed | kRegNoReader)) == 0,
						"%s.%s: writable, and not a plain 32-bit register of a processor", nodes[i].path, reg.name);
				}
			}

			Check(sh4_rows == 69 && arm7_rows == 40, "kRegWritable is on the 69 rows of dc.sh4.cpu and on the 40 rows of dc.aica.arm7 that are not ENABLED (%u, %u)", sh4_rows,
				arm7_rows);
			Check(other_rows == 0, "no other row has kRegWritable: no register of a device has a writer (%u have, the first is %s)", other_rows, first_other.c_str());
		}

		// The register file of the AICA: the fields of the common block and of every channel, bit for bit; the two
		// rows of dc.aica.common that are not in the file; and no field of the file on any other node
		{
			CheckAicaNode("dc.aica.common", common_spec, sizeof(common_spec) / sizeof(common_spec[0]), 0, 2);

			for (uint32_t channel = 0; channel < kChannels; channel++)
				CheckAicaNode(("dc.aica.sgc." + std::to_string(channel)).c_str(), channel_spec, sizeof(channel_spec) / sizeof(channel_spec[0]), channel * 0x80, 0);

			const Node* common = Find("dc.aica.common");
			const Register* level = RegisterOf("dc.aica.common", "L");
			const Register* service = RegisterOf("dc.aica.common", "M");

			Check(level && level->key == MakeKey(kSrcArm7Intc, kArm7IntcLevel) && service && service->key == MakeKey(kSrcArm7Intc, kArm7IntcService),
				"L and M of dc.aica.common are the two registers of the ARM7's interrupt controller");
			Check(common && common->registerCount >= 2 && level == &common->registers[common->registerCount - 2] && service == &common->registers[common->registerCount - 1],
				"and stand behind the fields of the register file");

			// what the file has at 0x2D00 and 0x2D04 is no row
			uint32_t elsewhere = 0, controller_words = 0;

			for (uint32_t i = 0; i < count; i++)
			{
				std::string path = nodes[i].path;
				const bool aica_node = path == "dc.aica.common" || path.rfind("dc.aica.sgc.", 0) == 0;

				for (uint32_t r = 0; r < nodes[i].registerCount; r++)
				{
					const Register& reg = nodes[i].registers[r];

					if (KeySource(reg.key) != kSrcAica)
						continue;

					elsewhere += !aica_node;

					for (const Bit& bit : KeyBits(reg))
						controller_words += bit.offset >= 0x2D00;
				}
			}

			Check(elsewhere == 0, "%u fields of the AICA's register file are on other nodes than dc.aica.common and the channels", elsewhere);
			Check(controller_words == 0, "%u bits of rows are in the words of the file at 0x2D00 and above", controller_words);
		}

		// The rows of the snapshots: every member once, in the order of the snapshot
		{
			const struct { const char* path; Source source; uint32_t members; } snapshots[] = {
				{ "dc.gdrom", kSrcGdrom, kGdromRegCount }, { "dc.aica.rtc", kSrcRtc, kRtcRegCount },
			};

			for (const auto& snapshot : snapshots)
			{
				const Node* node = Find(snapshot.path);
				bool in_order = node != nullptr && node->registerCount == snapshot.members;

				for (uint32_t r = 0; in_order && r < node->registerCount; r++)
					in_order = node->registers[r].key == MakeKey(snapshot.source, r);

				Check(in_order, "%s has the %u members of its snapshot, each once and in its order", snapshot.path, snapshot.members);
			}

			const struct { const char* name; uint32_t width; } gdrom[] = {
				{ "STATE", 32 }, { "DISC_TYPE", 32 }, { "STATUS", 8 }, { "ERROR", 8 }, { "INTERRUPT_REASON", 8 }, { "FEATURES", 8 }, { "SECTOR_COUNT", 8 },
				{ "SECTOR_NUMBER", 8 }, { "DRIVE_SELECT", 8 }, { "ATA_COMMAND", 8 }, { "BYTE_COUNT", 16 }, { "PACKET_0_3", 32 }, { "PACKET_4_7", 32 },
				{ "PACKET_8_11", 32 }, { "SENSE_KEY", 8 }, { "SENSE_ASC", 8 }, { "SENSE_ASCQ", 8 }, { "CDDA_PLAYING", 1 }, { "CDDA_REPEATS", 32 },
				{ "CDDA_CURRENT", 32 }, { "CDDA_START", 32 }, { "CDDA_END", 32 }, { "READ_START_SECTOR", 32 }, { "READ_REMAINING_SECTORS", 32 },
				{ "READ_SECTOR_TYPE", 32 }, { "PIO_INDEX", 32 }, { "PIO_SIZE", 32 },
			};
			const Node* drive = Find("dc.gdrom");

			for (uint32_t r = 0; drive != nullptr && r < drive->registerCount && r < sizeof(gdrom) / sizeof(gdrom[0]); r++)
			{
				Check(strcmp(drive->registers[r].name, gdrom[r].name) == 0 && drive->registers[r].width == gdrom[r].width, "dc.gdrom: register %u is %s, %u bits; expected %s, %u",
					r, drive->registers[r].name, drive->registers[r].width, gdrom[r].name, gdrom[r].width);
			}

			const Register* seconds = RegisterOf("dc.aica.rtc", "SECONDS");
			const Register* enable = RegisterOf("dc.aica.rtc", "WRITE_ENABLE");

			Check(seconds && seconds->width == 32 && seconds->addr == 0x00710000 && seconds->key == MakeKey(kSrcRtc, kRtcSeconds), "dc.aica.rtc.SECONDS is the 32 bits of the counter");
			Check(enable && enable->width == 1 && enable->addr == 0x00710008 && enable->key == MakeKey(kSrcRtc, kRtcWriteEnable), "dc.aica.rtc.WRITE_ENABLE is one bit");
		}

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
			{ "dc.holly.intc", "SB_ISTNRM", kRegComputed }, { "dc.holly.sb.misc", "SB_FFST", kRegComputed },
			// the system bus registers that are write-only as the emulator has them
			{ "dc.holly.sb.g1", "SB_G1RRC", kRegNoReader }, { "dc.holly.sb.g1", "SB_G1RWC", kRegNoReader }, { "dc.holly.sb.g1", "SB_G1FRC", kRegNoReader },
			{ "dc.holly.sb.g1", "SB_G1FWC", kRegNoReader }, { "dc.holly.sb.g1", "SB_G1CRC", kRegNoReader }, { "dc.holly.sb.g1", "SB_G1CWC", kRegNoReader },
			{ "dc.holly.sb.g1", "SB_G1GDRC", kRegNoReader }, { "dc.holly.sb.g1", "SB_G1GDWC", kRegNoReader }, { "dc.holly.sb.g1", "SB_G1CRDYC", kRegNoReader },
			{ "dc.holly.sb.g1", "SB_GDAPRO", kRegNoReader }, { "dc.holly.sb.g2", "SB_G2APRO", kRegNoReader }, { "dc.holly.sb.pvr", "SB_PDAPRO", kRegNoReader },
			{ "dc.holly.sb.misc", "SB_SFRES", kRegNoReader }, { "dc.maple", "SB_MDAPRO", kRegNoReader },
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

		in sh4_mmr.h,

		    //0x005F6900    SB_ISTNRM   RW  Normal interrupt status
		    #define SB_ISTNRM_addr 0x005F6900

		in sb_regs.h and

		    #define SPG_STATUS_addr         0x0000010C // R   Sync pulse generator status

		in pvr_regs.h.
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

	// The system bus registers of every node that has some, in its order (M2-ROUND 3.6.1). "G2:<ch>" stands for the
	// eleven registers of a DMA channel of the G2 bus
	const struct { const char* path; const char* registers; } sb_nodes[] = {
		{ "dc.holly.intc", "SB_ISTNRM SB_ISTEXT SB_ISTERR SB_IML2NRM SB_IML2EXT SB_IML2ERR SB_IML4NRM SB_IML4EXT SB_IML4ERR SB_IML6NRM SB_IML6EXT SB_IML6ERR" },
		{ "dc.holly.dmac", "SB_C2DSTAT SB_C2DLEN SB_C2DST SB_SDSTAW SB_SDBAAW SB_SDWLT SB_SDLAS SB_SDST SB_SDDIV "
		                   "SB_GDSTAR SB_GDLEN SB_GDDIR SB_GDEN SB_GDST SB_GDSTARD SB_GDLEND G2:AD G2:E1 G2:E2 G2:DD" },
		{ "dc.holly.sb.g1", "SB_G1RRC SB_G1RWC SB_G1FRC SB_G1FWC SB_G1CRC SB_G1CWC SB_G1GDRC SB_G1GDWC SB_G1SYSM SB_G1CRDYC SB_GDAPRO" },
		{ "dc.holly.sb.g2", "SB_G2ID SB_G2DSTO SB_G2TRTO SB_G2MDMTO SB_G2MDMW SB_G2APRO" },
		{ "dc.holly.sb.pvr", "SB_PDSTAP SB_PDSTAR SB_PDLEN SB_PDDIR SB_PDTSEL SB_PDEN SB_PDST SB_PDAPRO SB_PDSTAPD SB_PDSTARD SB_PDLEND" },
		{ "dc.holly.sb.misc", "SB_DBREQM SB_BAVLWC SB_C2DPRYC SB_C2DMAXL SB_TFREM SB_LMMODE0 SB_LMMODE1 SB_FFST SB_SFRES SB_SBREV SB_RBSPLT SB_PDTNRM SB_PDTEXT "
		                      "SB_G2DTNRM SB_G2DTEXT" },
		{ "dc.maple", "SB_MDSTAR SB_MDTSEL SB_MDEN SB_MDST SB_MSYS SB_MST SB_MSHTCL SB_MDAPRO SB_MMSEL SB_MTXDAD SB_MRXDAD SB_MRXDBD" },
	};

	std::vector<std::string> SbNames(const char* list)
	{
		std::vector<std::string> names;

		for (const std::string& word : Words(list))
		{
			if (word.rfind("G2:", 0) != 0)
			{
				names.push_back(word);
				continue;
			}

			for (const char* part : { "STAG", "STAR", "LEN", "DIR", "TSEL", "EN", "ST", "SUSP", "STAGD", "STARD", "LEND" })
				names.push_back("SB_" + word.substr(3) + part);
		}

		return names;
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

		Check(intc != nullptr && intc->registerCount == 12, "dc.holly.intc has the 12 interrupt registers");

		for (const auto& entry : sb)
		{
			if (entry.first.rfind("SB_IST", 0) == 0 || entry.first.rfind("SB_IML", 0) == 0)
				Check(RegisterOf("dc.holly.intc", entry.first.c_str()) != nullptr, "dc.holly.intc has no %s", entry.first.c_str());
		}

		// Every register of sb_regs.h is a row, of one node: the nodes that have system bus registers have these, in
		// this order, and nothing else; no other node has one
		std::map<std::string, uint32_t> rows_of;
		std::set<std::string> sb_paths;

		for (const auto& group : sb_nodes)
		{
			const Node* node = Find(group.path);
			const std::vector<std::string> names = SbNames(group.registers);

			sb_paths.insert(group.path);

			if (!Check(node != nullptr, "%s is missing", group.path))
				continue;

			Check(node->registerCount == names.size(), "%s has %u registers, expected the %zu system bus registers of its group", group.path, node->registerCount,
				names.size());

			for (uint32_t r = 0; r < node->registerCount; r++)
			{
				const Register& reg = node->registers[r];

				Check(KeySource(reg.key) == kSrcSb, "%s.%s is no system bus register", group.path, reg.name);
				if (r < names.size())
					Check(names[r] == reg.name, "%s: register %u is %s, expected %s", group.path, r, reg.name, names[r].c_str());
				rows_of[reg.name]++;
			}
		}

		for (uint32_t i = 0; i < count; i++)
		{
			for (uint32_t r = 0; r < nodes[i].registerCount && sb_paths.count(nodes[i].path) == 0; r++)
				Check(KeySource(nodes[i].registers[r].key) != kSrcSb, "%s.%s: a system bus register on a node that has none", nodes[i].path, nodes[i].registers[r].name);
		}

		for (const auto& entry : sb)
			Check(rows_of[entry.first] == 1, "%s of sb_regs.h is a row %u times", entry.first.c_str(), rows_of[entry.first]);

		Check(sb.size() == 127 && sb_rows == 127, "sb_regs.h defines %zu registers and the tree has %u system bus rows: 127 each, the 12 interrupt registers and 115 more",
			sb.size(), sb_rows);

		// the PVR's register file: every NAME_addr of pvr_regs.h but the bounds of its three tables is a row, with
		// that name and 0x005F8000 + its offset as address, on the node its prefix gives, in the order of the header
		lines.clear();

		if (!Check(ReadLines(libswirl + "/hw/pvr/pvr_regs.h", &lines), "cannot read %s/hw/pvr/pvr_regs.h", libswirl.c_str()))
			return;

		std::map<std::string, std::vector<std::pair<std::string, uint32_t>>> pvr;		// by node
		std::vector<std::string> tables;
		uint32_t pvr_defs = 0;

		for (const std::string& line : lines)
		{
			std::vector<std::string> define = Words(line);

			if (define.size() < 3 || define[0] != "#define" || define[1].size() < 6 || define[1].compare(define[1].size() - 5, 5, "_addr") != 0)
				continue;

			std::string symbol = define[1].substr(0, define[1].size() - 5);

			// the fog table, the object list pointers and the palette are no rows
			if ((symbol.size() > 6 && symbol.compare(symbol.size() - 6, 6, "_START") == 0) || (symbol.size() > 4 && symbol.compare(symbol.size() - 4, 4, "_END") == 0))
			{
				tables.push_back(symbol);
				continue;
			}

			const char* path = symbol.rfind("TA_", 0) == 0 ? "dc.holly.ta" : symbol.rfind("SPG_", 0) == 0 ? "dc.holly.spg" : "dc.holly.core";

			pvr[path].push_back({ symbol, (uint32_t)strtoul(define[2].c_str(), nullptr, 16) });
			pvr_defs++;
		}

		Check(pvr_defs == 68 && tables.size() == 6, "pvr_regs.h defines %u registers and the bounds of %zu / 2 tables, expected 68 and 3", pvr_defs, tables.size());

		uint32_t pvr_rows = 0;

		for (uint32_t i = 0; i < count; i++)
		{
			for (uint32_t r = 0; r < nodes[i].registerCount; r++)
			{
				if (KeySource(nodes[i].registers[r].key) != kSrcPvr)
					continue;

				pvr_rows++;
				Check(pvr.count(nodes[i].path) == 1, "%s.%s: a PVR register on a node that has none", nodes[i].path, nodes[i].registers[r].name);
			}
		}

		Check(pvr_rows == pvr_defs, "the tree has %u PVR rows, pvr_regs.h defines %u registers", pvr_rows, pvr_defs);

		const struct { const char* path; uint32_t registers; } pvr_nodes[] = { { "dc.holly.ta", 14 }, { "dc.holly.core", 45 }, { "dc.holly.spg", 9 } };

		for (const auto& group : pvr_nodes)
		{
			const Node* node = Find(group.path);
			const std::vector<std::pair<std::string, uint32_t>>& header = pvr[group.path];

			if (!Check(node != nullptr, "%s is missing", group.path))
				continue;

			Check(node->registerCount == header.size() && header.size() == group.registers, "%s has %u registers, pvr_regs.h defines %zu for it, expected %u", group.path,
				node->registerCount, header.size(), group.registers);

			for (uint32_t r = 0; r < node->registerCount && r < header.size(); r++)
			{
				const Register& reg = node->registers[r];

				Check(KeySource(reg.key) == kSrcPvr, "%s.%s is no PVR register", group.path, reg.name);
				Check(header[r].first == reg.name, "%s: register %u is %s, pvr_regs.h has %s there", group.path, r, reg.name, header[r].first.c_str());
				Check(0x005F8000 + header[r].second == reg.addr, "%s.%s: address %08X, pvr_regs.h has the offset %04X", group.path, reg.name, reg.addr, header[r].second);
			}
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
