/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_selftest.h"

#include "dbg_mem.h"
#include "dbg_regs.h"
#include "dbg_regs_private.h"

#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp.h"
#include "hw/aica/dsp_backend.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7_context.h"
#include "hw/gdrom/gdromv3.h"
#include "hw/holly/holly_intc.h"
#include "hw/holly/sb.h"
#include "hw/holly/sh4_mem_area0.h"
#include "hw/mem/_vmem.h"
#include "hw/pvr/pvr_mem.h"
#include "hw/pvr/pvr_regs.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/sh4_sched.h"
#include "serialize.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

/*
	Selftests of dbg::regs, dbg::mem and the device accessors behind them. All of them are machine tests: they
	read, and where they compare with what the guest would read they put the machine back as it was.

	Where a value has a second source that is not the debugger's (the SH4 context, a save state of a device, a
	guest read that is known to have no side effect) the two are compared. Guest reads are made by this file
	only, never by the modules under test, and only of registers whose read function changes nothing; the two
	reads that do change something (the FIFO status of the system bus, the status of an AICA channel) are
	followed by putting the device back from a save state of it.
*/

using namespace dbg;
using namespace dbg::selftest;

namespace {

// Many comparisons, one line: the number that did not hold and the first of them.
struct Tally {
	u32 total = 0;
	u32 bad = 0;
	char first[200] = "";

	void Add(bool ok, const char* fmt, ...) DBG_PRINTF_LIKE(3, 4)
	{
		total++;

		if (ok)
			return;

		if (bad++ == 0)
		{
			va_list args;
			va_start(args, fmt);
			vsnprintf(first, sizeof(first), fmt, args);
			va_end(args);
		}
	}

	bool Report(const char* what)
	{
		if (bad == 0)
			return Check(total != 0, "%s: %u compared", what, total);

		return Check(false, "%s: %u of %u differ, first: %s", what, bad, total, first);
	}
};

u64 WidthMask(u32 width)
{
	return width >= 64 ? ~(u64)0 : ((u64)1 << width) - 1;
}

// What a device writes into a save state.
std::vector<u8> DeviceState(MMIODevice* device)
{
	void* data = nullptr;
	unsigned int size = 0;
	device->serialize(&data, &size);

	std::vector<u8> state(size);
	data = state.data();
	size = 0;
	device->serialize(&data, &size);

	return state;
}

void RestoreDevice(MMIODevice* device, std::vector<u8>& state)
{
	void* data = state.data();
	unsigned int size = 0;
	device->unserialize(&data, &size);
}

// The whole machine as a save state has it: every device, the SH4 context and its on-chip modules, the
// scheduler, the three memories.
bool MachineState(std::vector<u8>* state)
{
	void* data = nullptr;
	unsigned int size = 0;

	if (!dc_serialize(&data, &size))
		return false;

	state->resize(size);
	data = state->data();
	size = 0;
	return dc_serialize(&data, &size);
}

// The renderer writes video memory and the PVR registers from its own threads while a frame is queued: two
// states taken one behind the other are equal only when it is idle.
bool WaitQuiescent(std::vector<u8>* state)
{
	std::vector<u8> again;

	for (int tries = 0; tries < 200; tries++)
	{
		if (!MachineState(state) || !MachineState(&again))
			return false;
		if (*state == again)
			return true;

		// a frame takes milliseconds
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	return false;
}

size_t FirstDifference(const std::vector<u8>& a, const std::vector<u8>& b)
{
	size_t n = a.size() < b.size() ? a.size() : b.size();

	for (size_t i = 0; i < n; i++)
	{
		if (a[i] != b[i])
			return i;
	}

	return n;
}

/*
	The on-chip modules as the emulator has them.
*/
struct ModuleSlots {
	const char* path;
	u32 base;					// P4
	const RegisterStruct* slots;
	size_t count;
};

std::vector<ModuleSlots> Modules()
{
	SuperH4Mmr* mmr = sh4_cpu->sh4mmr.get();

	return {
		{ "dc.sh4.bsc",  BSC_BASE_addr | 0xE0000000,  mmr->BSC.data(),  mmr->BSC.size() },
		{ "dc.sh4.ccn",  CCN_BASE_addr | 0xE0000000,  mmr->CCN.data(),  mmr->CCN.size() },
		{ "dc.sh4.cpg",  CPG_BASE_addr | 0xE0000000,  mmr->CPG.data(),  mmr->CPG.size() },
		{ "dc.sh4.dmac", DMAC_BASE_addr | 0xE0000000, mmr->DMAC.data(), mmr->DMAC.size() },
		{ "dc.sh4.intc", INTC_BASE_addr | 0xE0000000, mmr->INTC.data(), mmr->INTC.size() },
		{ "dc.sh4.rtc",  RTC_BASE_addr | 0xE0000000,  mmr->RTC.data(),  mmr->RTC.size() },
		{ "dc.sh4.sci",  SCI_BASE_addr | 0xE0000000,  mmr->SCI.data(),  mmr->SCI.size() },
		{ "dc.sh4.scif", SCIF_BASE_addr | 0xE0000000, mmr->SCIF.data(), mmr->SCIF.size() },
		{ "dc.sh4.tmu",  TMU_BASE_addr | 0xE0000000,  mmr->TMU.data(),  mmr->TMU.size() },
		{ "dc.sh4.ubc",  UBC_BASE_addr | 0xE0000000,  mmr->UBC.data(),  mmr->UBC.size() },
	};
}

// A slot that a module or a device registered: every slot starts out as one without access.
bool Registered(const RegisterStruct& slot)
{
	const u32 access = REG_RF | REG_WF | REG_RO | REG_WO | REG_CONST;

	return (slot.flags & access) != (u32)RIO_NO_ACCESS;
}

// The row of the tables for an on-chip register address (P4), null when there is none.
const regs::Register* MmrRow(u32 p4)
{
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);

	for (u32 i = 0; i < count; i++)
	{
		for (u32 r = 0; r < nodes[i].registerCount; r++)
		{
			const regs::Register& reg = nodes[i].registers[r];

			if (regs::KeySource(reg.key) == regs::kSrcSh4Mmr && reg.addr == p4)
				return &reg;
		}
	}

	return nullptr;
}

bool ReadRegister(const char* path, const char* name, u64* value)
{
	const regs::Node* node = regs::Find(path);

	if (node == nullptr)
		return false;

	int index = regs::FindRegister(*node, name);

	return index >= 0 && regs::Read(*node, (u32)index, value, nullptr, 0);
}

// A guest read of an on-chip register, at the width the register has. Only for registers whose read has no
// side effect.
u32 GuestReadMmr(const regs::Register& reg)
{
	switch (reg.width)
	{
	case 8:  return _vmem_ReadMem8(reg.addr);
	case 16: return _vmem_ReadMem16(reg.addr);
	default: return _vmem_ReadMem32(reg.addr);
	}
}

/*
	The memory map of dbg_mem.h, written out again: the low 29 bits of an SH4 address.
*/
struct MapRow {
	u32 first;
	u32 last;
	mem::Region region;
};

const MapRow area0_map[] = {
	{ 0x00000000, 0x001FFFFF, mem::Region::Bios },
	{ 0x00200000, 0x0021FFFF, mem::Region::Flash },
	{ 0x00220000, 0x005F67FF, mem::Region::Unreadable },
	{ 0x005F6800, 0x005F6FFF, mem::Region::SbRegs },
	{ 0x005F7000, 0x005F70FF, mem::Region::Unreadable },
	{ 0x005F7100, 0x005F7CFF, mem::Region::SbRegs },
	{ 0x005F7D00, 0x005F7FFF, mem::Region::Unreadable },
	{ 0x005F8000, 0x005F9FFF, mem::Region::PvrRegs },
	{ 0x005FA000, 0x006FFFFF, mem::Region::Unreadable },
	{ 0x00700000, 0x00707FFF, mem::Region::AicaRegs },
	{ 0x00708000, 0x0070FFFF, mem::Region::Unreadable },
	{ 0x00710000, 0x0071000B, mem::Region::AicaRtc },
	{ 0x0071000C, 0x007FFFFF, mem::Region::Unreadable },
	{ 0x00800000, 0x00FFFFFF, mem::Region::AicaRam },
	{ 0x01000000, 0x01FFFFFF, mem::Region::Unreadable },
};

const MapRow upper_map[] = {
	{ 0x04000000, 0x04FFFFFF, mem::Region::Vram64 },
	{ 0x05000000, 0x05FFFFFF, mem::Region::Vram32 },
	{ 0x06000000, 0x06FFFFFF, mem::Region::Vram64 },
	{ 0x07000000, 0x07FFFFFF, mem::Region::Vram32 },
	{ 0x08000000, 0x0BFFFFFF, mem::Region::Unreadable },
	{ 0x0C000000, 0x0FFFFFFF, mem::Region::Ram },
	{ 0x10000000, 0x1EFFFFFF, mem::Region::Unreadable },
	{ 0x1F000000, 0x1FFFFFFF, mem::Region::Sh4Regs },
};

const MapRow p4_map[] = {
	{ 0xE0000000, 0xE3FFFFFF, mem::Region::StoreQueue },
	{ 0xE4000000, 0xFEFFFFFF, mem::Region::Unreadable },
	{ 0xFF000000, 0xFFFFFFFF, mem::Region::Sh4Regs },
};

const MapRow arm7_map[] = {
	{ 0x00000000, 0x007FFFFF, mem::Region::AicaRam },
	{ 0x00800000, 0x00807FFF, mem::Region::AicaRegs },
	{ 0x00808000, 0xFFFFFFFF, mem::Region::Unreadable },
};

// Every run of one region in the 4 GiB of the SH4, mirrors included.
std::vector<MapRow> Sh4Map()
{
	std::vector<MapRow> map;

	for (u32 mirror = 0; mirror < 0xE0000000; mirror += 0x20000000)
	{
		for (u32 half = 0; half <= 0x02000000; half += 0x02000000)
		{
			for (const MapRow& row : area0_map)
				map.push_back({ mirror + half + row.first, mirror + half + row.last, row.region });
		}

		for (const MapRow& row : upper_map)
			map.push_back({ mirror + row.first, mirror + row.last, row.region });
	}

	for (const MapRow& row : p4_map)
		map.push_back(row);

	return map;
}

const char* RegionName(mem::Region region)
{
	switch (region)
	{
	case mem::Region::Unreadable: return "Unreadable";
	case mem::Region::Bios:       return "Bios";
	case mem::Region::Flash:      return "Flash";
	case mem::Region::SbRegs:     return "SbRegs";
	case mem::Region::PvrRegs:    return "PvrRegs";
	case mem::Region::AicaRegs:   return "AicaRegs";
	case mem::Region::AicaRtc:    return "AicaRtc";
	case mem::Region::AicaRam:    return "AicaRam";
	case mem::Region::Vram64:     return "Vram64";
	case mem::Region::Vram32:     return "Vram32";
	case mem::Region::Ram:        return "Ram";
	case mem::Region::StoreQueue: return "StoreQueue";
	case mem::Region::Sh4Regs:    return "Sh4Regs";
	}

	return "?";
}

// Is the byte at an SH4 address readable, by the rules of dbg_mem.h and the registers the machine has? Worked
// out from the slots and the tables, not from dbg::mem.
bool ExpectReadable(Target t, u32 addr, mem::Region region)
{
	switch (region)
	{
	case mem::Region::Unreadable:
		return false;

	case mem::Region::SbRegs:
	{
		SystemBus* sb = sh4_cpu->GetA0H<SystemBus>(A0H_SB);
		u32 reg_addr = addr & 0x01FFFFFC;
		const RegisterStruct& slot = sb->sb_regs[(reg_addr - SB_BASE) / 4];

		if (slot.flags & REG_WO)
			return false;

		// the two registers behind a read function that have a reader
		return !(slot.flags & REG_RF) || reg_addr == SB_FFST_addr || reg_addr == SB_ISTNRM_addr;
	}

	case mem::Region::Sh4Regs:
	{
		const regs::Register* row = MmrRow((addr & ~3u) | 0xE0000000);

		return row != nullptr && !(row->flags & regs::kRegNoReader) && (addr & 3) < row->width / 8u;
	}

	default:
		(void)t;
		return true;
	}
}

// Reads a window and compares every byte's readable bit with ExpectReadable, the returned count with the
// bits, and the unreadable bytes with 0.
void CheckWindow(Target t, u32 addr, u32 len, Tally* tally)
{
	std::vector<u8> data(len, 0xEE);
	std::vector<u8> bits((len + 7) / 8, 0xEE);

	u32 readable = mem::Read(t, addr, len, data.data(), bits.data());
	u32 counted = 0;

	for (u32 i = 0; i < len; i++)
	{
		u32 at = addr + i;
		bool bit = (bits[i >> 3] >> (i & 7)) & 1;
		bool want = ExpectReadable(t, at, mem::Classify(t, at));

		tally->Add(bit == want, "%s %08X: readable %d, expected %d (%s)", TargetName(t), at, bit, want, RegionName(mem::Classify(t, at)));
		if (!bit)
			tally->Add(data[i] == 0, "%s %08X: unreadable byte reads %02X", TargetName(t), at, data[i]);

		counted += bit;
	}

	// the bits behind the last byte of the window are clear
	for (u32 i = len; i < bits.size() * 8; i++)
		tally->Add(((bits[i >> 3] >> (i & 7)) & 1) == 0, "%s %08X+%X: bit %u behind the window is set", TargetName(t), addr, len, i);

	tally->Add(readable == counted, "%s %08X+%X: returns %u readable bytes, the bits say %u", TargetName(t), addr, len, readable, counted);
}

// The windows of 64 KiB around every place where the region changes, for every target.
template <typename Fn>
void ForEachBoundaryWindow(Fn fn)
{
	const u32 half = 0x8000;

	for (const MapRow& row : Sh4Map())
		fn(Target::Sh4, row.first - half, 2 * half);

	// the end of the address space wraps to its start
	fn(Target::Sh4, 0xFFFF8000, 2 * half);

	for (const MapRow& row : arm7_map)
		fn(Target::Arm7, row.first - half, 2 * half);

	fn(Target::Dsp, 0, 2 * half);
	fn(Target::Dsp, 0x00800000 - half, 2 * half);
}

}

/*
	regs.slots: the tables against the register slots of the live machine.
*/
DBG_SELFTEST(regs_slots, "regs.slots")
{
	Tally rows, slots, kinds;
	u32 unemulated = 0;

	for (const ModuleSlots& module : Modules())
	{
		const regs::Node* node = regs::Find(module.path);

		if (!Check(node != nullptr, "%s is in the tree", module.path))
			continue;

		// every row is a registered slot
		for (u32 r = 0; r < node->registerCount; r++)
		{
			const regs::Register& reg = node->registers[r];
			u32 index = (reg.addr - module.base) / 4;

			if (reg.addr < module.base || index >= module.count)
			{
				rows.Add(false, "%s.%s: address %08X is outside the module's %zu slots", module.path, reg.name, reg.addr, module.count);
				continue;
			}

			const RegisterStruct& slot = module.slots[index];

			// a row is a registered slot, but for the rows that say the emulator has no such register
			bool no_slot = regs::KeySelector(reg.key) == regs::kMmrNoSlot;

			rows.Add(Registered(slot) != no_slot, "%s.%s: slot %u is %sregistered", module.path, reg.name, index, Registered(slot) ? "" : "not ");
			unemulated += no_slot;

			// a value in the slot is read from it; a read function in the slot needs a reader, or there is none
			bool function = (slot.flags & REG_RF) != 0;
			bool special = (reg.flags & (regs::kRegComputed | regs::kRegNoReader)) != 0;

			kinds.Add(function == special, "%s.%s: slot flags %02X, register flags %02X", module.path, reg.name, slot.flags, reg.flags);
		}

		// every registered slot has a row
		for (u32 index = 0; index < module.count; index++)
		{
			if (Registered(module.slots[index]))
				slots.Add(MmrRow(module.base + index * 4) != nullptr, "%s: slot %u at %08X has no row", module.path, index, module.base + index * 4);
		}
	}

	rows.Report("on-chip register rows that are registered slots");
	slots.Report("registered on-chip slots that have a row");
	kinds.Report("rows that agree with their slot about a read function");
	Note("%u on-chip registers of the tables are not emulated: no slot is registered for them", unemulated);

	// the system bus: the rows are registers that can be read; the slots without a row are the tables to come
	SystemBus* sb = sh4_cpu->GetA0H<SystemBus>(A0H_SB);
	Tally sb_rows;
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);
	u32 with_row = 0;

	for (u32 i = 0; i < count; i++)
	{
		for (u32 r = 0; r < nodes[i].registerCount; r++)
		{
			const regs::Register& reg = nodes[i].registers[r];

			if (regs::KeySource(reg.key) != regs::kSrcSb)
				continue;

			const RegisterStruct& slot = sb->sb_regs[(reg.addr - SB_BASE) / 4];

			with_row++;
			sb_rows.Add(Registered(slot) && !(slot.flags & REG_WO), "%s.%s: slot flags %02X", nodes[i].path, reg.name, slot.flags);
			sb_rows.Add(((slot.flags & REG_RF) != 0) == ((reg.flags & regs::kRegComputed) != 0), "%s.%s: slot flags %02X, register flags %02X",
				nodes[i].path, reg.name, slot.flags, reg.flags);
		}
	}

	sb_rows.Report("system bus rows that are readable registers");

	u32 registered = 0;

	for (const RegisterStruct& slot : sb->sb_regs)
		registered += Registered(slot);

	Note("%u of the %u registered system bus registers have a row", with_row, registered);
}

/*
	regs.read: every register of every node reads, or says that it cannot.
*/
DBG_SELFTEST(regs_read, "regs.read")
{
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);
	Tally reads, values, edges;
	u32 total = 0, unreadable = 0, by_engine = 0;

	emu::Sh4EngineInfo engine;
	bool engine_known = emu::GetSh4Engine(&engine);

	for (u32 i = 0; i < count; i++)
	{
		const regs::Node& node = nodes[i];

		for (u32 r = 0; r < node.registerCount; r++)
		{
			const regs::Register& reg = node.registers[r];
			const u64 untouched = 0xA5A5A5A5A5A5A5A5ull;
			u64 value = untouched;
			char text[128] = "\x7F";

			bool ok = regs::Read(node, r, &value, text, sizeof(text));
			bool want = !(reg.flags & regs::kRegNoReader);

			total++;

			// the engine of the SH4 is known once a thread has selected a backend
			bool engine_text = regs::KeySource(reg.key) == regs::kSrcText &&
				(regs::KeySelector(reg.key) == regs::kTextSh4Backend || regs::KeySelector(reg.key) == regs::kTextSh4DebugEngine);

			if (engine_text && !engine_known)
			{
				want = false;
				by_engine++;
			}

			reads.Add(ok == want, "%s.%s: Read() gives %d, expected %d", node.path, reg.name, ok, want);

			if (!ok)
			{
				unreadable++;
				values.Add(value == untouched && text[0] == 0x7F, "%s.%s: a failed Read() wrote its outputs", node.path, reg.name);
				continue;
			}

			if (reg.width == 0)
			{
				values.Add(text[0] != 0 && text[0] != 0x7F && value == untouched, "%s.%s: text \"%s\"", node.path, reg.name, text);

				// a text is cut to the buffer
				char cut[4] = "###";
				values.Add(regs::Read(node, r, nullptr, cut, sizeof(cut)) && strlen(cut) <= 3 && strncmp(cut, text, strlen(cut)) == 0,
					"%s.%s: text cut to 4 bytes is \"%.4s\"", node.path, reg.name, cut);
			}
			else
			{
				values.Add((value & ~WidthMask(reg.width)) == 0, "%s.%s: value %llX has bits above its %u", node.path, reg.name,
					(unsigned long long)value, reg.width);
				values.Add(text[0] == 0x7F, "%s.%s: a value wrote the text", node.path, reg.name);

				// a second read gives the same: nothing moves while a test runs
				u64 again = untouched;
				values.Add(regs::Read(node, r, &again, nullptr, 0) && again == value, "%s.%s: %llX, then %llX", node.path, reg.name,
					(unsigned long long)value, (unsigned long long)again);
			}

			// the outputs may be absent
			edges.Add(regs::Read(node, r, nullptr, nullptr, 0), "%s.%s: Read() without outputs fails", node.path, reg.name);
		}

		u64 value = 0x1234;
		char text[8] = "x";

		edges.Add(!regs::Read(node, node.registerCount, &value, text, sizeof(text)) && value == 0x1234 && strcmp(text, "x") == 0,
			"%s: Read() behind the last register", node.path);
		edges.Add(!regs::Read(node, 0xFFFFFFFF, &value, text, sizeof(text)), "%s: Read() of index -1", node.path);
		edges.Add(regs::Write(node, 0, 0) == kErrNotSupported, "%s: Write() does not say that no writer exists", node.path);
	}

	reads.Report("registers that read as their flags say");
	values.Report("values inside their width, texts not empty, outputs untouched on failure");
	edges.Report("reads without outputs and outside a node");

	Note("%u registers, %u read as n/a (%u of them because no SH4 engine is known%s)", total, unreadable, by_engine,
		engine_known ? "" : ": dbg::emu does not give one");
}

/*
	regs.values: register values against a second source.
*/
DBG_SELFTEST(regs_values, "regs.values")
{
	// the SH4 context
	const regs::Node* cpu = regs::Find("dc.sh4.cpu");
	const Sh4Context& ctx = p_sh4rcb->cntx;
	Tally sh4;

	for (u32 r = 0; cpu != nullptr && r < cpu->registerCount; r++)
	{
		const char* name = cpu->registers[r].name;
		u64 value = ~(u64)0;
		u32 want = 0;
		unsigned n = 0;
		char tail[8] = "";
		bool known = true;

		regs::Read(*cpu, r, &value, nullptr, 0);

		if (sscanf(name, "R%u%7s", &n, tail) == 2 && strcmp(tail, "_BANK") == 0 && n < 8)
			want = ctx.r_bank[n];
		else if (sscanf(name, "R%u%7s", &n, tail) == 1 && n < 16)
			want = ctx.r[n];
		else if (sscanf(name, "FR%u%7s", &n, tail) == 1 && n < 16)
			memcpy(&want, &ctx.xffr[16 + n], 4);
		else if (sscanf(name, "XF%u%7s", &n, tail) == 1 && n < 16)
			memcpy(&want, &ctx.xffr[n], 4);
		else if (strcmp(name, "PC") == 0)    want = ctx.pc;
		else if (strcmp(name, "PR") == 0)    want = ctx._pr;
		else if (strcmp(name, "SR") == 0)    want = (ctx.sr.status & 0x700083F2) | ctx.sr.T;
		else if (strcmp(name, "GBR") == 0)   want = ctx.gbr;
		else if (strcmp(name, "VBR") == 0)   want = ctx.vbr;
		else if (strcmp(name, "MACH") == 0)  want = (u32)(ctx.mac.full >> 32);
		else if (strcmp(name, "MACL") == 0)  want = (u32)ctx.mac.full;
		else if (strcmp(name, "SSR") == 0)   want = ctx.ssr;
		else if (strcmp(name, "SPC") == 0)   want = ctx.spc;
		else if (strcmp(name, "SGR") == 0)   want = ctx.sgr;
		else if (strcmp(name, "DBR") == 0)   want = ctx.dbr;
		else if (strcmp(name, "FPSCR") == 0) want = ctx.fpscr.full;
		else if (strcmp(name, "FPUL") == 0)  want = ctx.fpul;
		else
			known = false;

		sh4.Add(known && value == want, "dc.sh4.cpu.%s: %llX, the context has %X", name, (unsigned long long)value, want);
	}

	sh4.Report("dc.sh4.cpu against the SH4 context");

	// the on-chip registers against a guest read, for those whose guest read changes nothing
	Tally mmr;
	u32 skipped = 0;

	for (const ModuleSlots& module : Modules())
	{
		const regs::Node* node = regs::Find(module.path);

		for (u32 r = 0; node != nullptr && r < node->registerCount; r++)
		{
			const regs::Register& reg = node->registers[r];
			u64 value = ~(u64)0;

			if (reg.flags & regs::kRegNoReader)
			{
				skipped++;
				continue;
			}

			regs::Read(*node, r, &value, nullptr, 0);

			u32 guest = GuestReadMmr(reg);

			mmr.Add(value == guest, "%s.%s: %llX, a guest read of %08X gives %X", module.path, reg.name, (unsigned long long)value, reg.addr, guest);
		}
	}

	mmr.Report("on-chip registers against a guest read");
	Note("%u on-chip registers have no reader and were not read by the guest path either", skipped);

	u64 value = 0;

	bool ok = ReadRegister("dc.sh4.ccn", "CPU_VERSION", &value);

	Check(ok && value == 0x040205c1, "dc.sh4.ccn.CPU_VERSION is %08llX", (unsigned long long)value);
	Check(!ReadRegister("dc.sh4.scif", "SCFRDR2", &value) && !ReadRegister("dc.sh4.scif", "SCFSR2", &value) && !ReadRegister("dc.sh4.bsc", "PDTRA", &value),
		"SCFRDR2, SCFSR2 and PDTRA have no value");

	// the timer counters follow the scheduler's time, which stands still here: a channel that counts was read
	// at this very time by the guest path above
	u64 tstr = 0;
	ok = ReadRegister("dc.sh4.tmu", "TSTR", &tstr);

	Check(ok, "dc.sh4.tmu.TSTR reads (%02llX: the channels that count)", (unsigned long long)tstr);

	// the interrupt registers of the system bus against a guest read
	MMIODevice* sb_device = sh4_cpu->GetA0Handler(A0H_SB);
	const regs::Node* intc = regs::Find("dc.holly.intc");
	Tally holly;

	for (u32 r = 0; intc != nullptr && r < intc->registerCount; r++)
	{
		const regs::Register& reg = intc->registers[r];
		u64 mine = ~(u64)0;

		regs::Read(*intc, r, &mine, nullptr, 0);

		u32 guest = sb_device->Read(reg.addr, 4);

		holly.Add(mine == guest, "dc.holly.intc.%s: %llX, a guest read gives %X", reg.name, (unsigned long long)mine, guest);
	}

	holly.Report("dc.holly.intc against a guest read");

	// SB_FFST: the reader gives what the next guest read gives, and does not count as a read
	{
		std::vector<u8> before = DeviceState(sb_device);
		u32 first = 0, second = 0;

		ok = regs::ReadSbRegisterAt(SB_FFST_addr, &first) && regs::ReadSbRegisterAt(SB_FFST_addr, &second);

		Check(ok && first == second && DeviceState(sb_device) == before, "SB_FFST reads %X twice and the poll counter stands", first);

		u32 guest = sb_device->Read(SB_FFST_addr, 4);

		Check(guest == first, "the guest read behind it gives %X", guest);
		Check(DeviceState(sb_device) != before, "that guest read counted");

		RestoreDevice(sb_device, before);
		Check(DeviceState(sb_device) == before, "the system bus is as it was");
	}

	// time
	u64 now = 0, sh4_time = 0;

	ok = ReadRegister("dc.timing", "SCHED_NOW", &now) && ReadRegister("dc.timing", "SH4_CYCLES", &sh4_time);

	Check(ok && now == sh4_sched_now64(), "dc.timing.SCHED_NOW is the scheduler's time, %llu", (unsigned long long)now);
	Check(ok && sh4_time >= now && sh4_time - now <= SH4_TIMESLICE, "dc.timing.SH4_CYCLES is %llu, at most a slice ahead of it", (unsigned long long)sh4_time);

	// texts
	const regs::Node* root = regs::Find("dc");
	const regs::Node* timing = regs::Find("dc.timing");
	char text[128] = "";

	ok = root && regs::Read(*root, (u32)regs::FindRegister(*root, "SYSCLK"), nullptr, text, sizeof(text));
	Check(ok && strcmp(text, "200MHz") == 0, "dc.SYSCLK is \"%s\"", text);

	ok = root && regs::Read(*root, (u32)regs::FindRegister(*root, "BUILD"), nullptr, text, sizeof(text));
	Check(ok && strncmp(text, "nullDC 3.0.0", 12) == 0, "dc.BUILD is \"%s\"", text);

	ok = timing && regs::Read(*timing, (u32)regs::FindRegister(*timing, "MODE"), nullptr, text, sizeof(text));
	Check(ok && strcmp(text, "fast") == 0, "dc.timing.MODE is \"%s\"", text);

	emu::Sh4EngineInfo engine;

	if (emu::GetSh4Engine(&engine))
	{
		ok = timing && regs::Read(*timing, (u32)regs::FindRegister(*timing, "SH4_BACKEND"), nullptr, text, sizeof(text));
		Check(ok && strcmp(text, engine.backend) == 0, "dc.timing.SH4_BACKEND is \"%s\"", text);

		ok = timing && regs::Read(*timing, (u32)regs::FindRegister(*timing, "SH4_DEBUG_ENGINE"), nullptr, text, sizeof(text));
		Check(ok && strcmp(text, engine.fallback ? "interpreter-fallback" : "native") == 0, "dc.timing.SH4_DEBUG_ENGINE is \"%s\"", text);
	}
	else
		Note("no SH4 engine is known: dc.timing.SH4_BACKEND and SH4_DEBUG_ENGINE read as n/a");
}

/*
	regs.devices: the debug accessors of the devices against a second source.
*/
DBG_SELFTEST(regs_devices, "regs.devices")
{
	// the sound CPU: its registers as the debugger shows them, worked out again from its save state
	{
		SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
		std::vector<u8> state = DeviceState(scpu);

		// bool aica_interr, u32 aica_reg_L, bool e68k_out, u32 e68k_reg_L, u32 e68k_reg_M, the registers,
		// bool armIrqEnable, bool armFiqEnable, int armMode, bool enabled
		const size_t regs_at = 1 + 4 + 1 + 4 + 4;
		const size_t tail_at = regs_at + RN_ARM_REG_COUNT * 4;

		if (Check(state.size() == tail_at + 1 + 1 + 4 + 1, "the save state of the sound CPU has %zu bytes", state.size()))
		{
			const regs::Node* arm7 = regs::Find("dc.aica.arm7");

			// every row of dc.aica.arm7 against what the bytes of a save state say it is
			auto compare = [&](const std::vector<u8>& bytes, Tally* tally) {
				u32 raw[RN_ARM_REG_COUNT];
				memcpy(raw, &bytes[regs_at], sizeof(raw));

				bool irq = bytes[tail_at] != 0;
				bool fiq = bytes[tail_at + 1] != 0;
				u32 mode;
				memcpy(&mode, &bytes[tail_at + 2], 4);

				// the bank of the mode the core is in is live in R8..R14 and SPSR
				const struct { u32 mode, first, last, live; } banks[] = {
					{ 0x10, R13_USR, R14_USR, 13 }, { 0x1F, R13_USR, R14_USR, 13 },
					{ 0x11, R8_FIQ, R14_FIQ, 8 },   { 0x11, SPSR_FIQ, SPSR_FIQ, RN_SPSR },
					{ 0x12, R13_IRQ, R14_IRQ, 13 }, { 0x12, SPSR_IRQ, SPSR_IRQ, RN_SPSR },
					{ 0x13, R13_SVC, R14_SVC, 13 }, { 0x13, SPSR_SVC, SPSR_SVC, RN_SPSR },
					{ 0x17, R13_ABT, R14_ABT, 13 }, { 0x17, SPSR_ABT, SPSR_ABT, RN_SPSR },
					{ 0x1B, R13_UND, R14_UND, 13 }, { 0x1B, SPSR_UND, SPSR_UND, RN_SPSR },
				};

				for (u32 r = 0; arm7 != nullptr && r < arm7->registerCount; r++)
				{
					const regs::Register& reg = arm7->registers[r];
					u32 index = regs::KeySelector(reg.key);
					u32 want = raw[index];

					if (index == 15)
						want = raw[R15_ARM_NEXT];
					else if (index == RN_CPSR)
						want = (raw[RN_PSR_FLAGS] & 0xF0000000) | (raw[RN_CPSR] & 0x40) | (fiq ? 0 : 0x40) | (irq ? 0 : 0x80) | (mode & 0x1F);

					for (const auto& bank : banks)
					{
						if (bank.mode == mode && index >= bank.first && index <= bank.last)
							want = raw[bank.live + index - bank.first];
					}

					u64 value = ~(u64)0;
					regs::Read(*arm7, r, &value, nullptr, 0);

					tally->Add(value == want, "dc.aica.arm7.%s in mode %02X: %llX, the save state gives %X", reg.name, mode, (unsigned long long)value, want);
				}

				// R8..R12 of the modes other than FIQ, which no row has: live but in FIQ mode, where the slots of
				// the FIQ bank hold them
				for (u32 i = 0; i < 5; i++)
				{
					u32 want = raw[(mode == 0x11 ? (u32)R8_FIQ : 8u) + i];
					u32 value = ~want;
					bool ok = scpu->DebugGetReg(21 + i, &value);

					tally->Add(ok && value == want, "DebugGetReg(%u) in mode %02X: %X, the save state gives %X", 21 + i, mode, value, want);
				}
			};

			Tally live;
			compare(state, &live);
			live.Report("dc.aica.arm7 against the save state of the sound CPU");

			u32 next, r15, mode;
			memcpy(&next, &state[regs_at + R15_ARM_NEXT * 4], 4);
			memcpy(&r15, &state[regs_at + 15 * 4], 4);
			memcpy(&mode, &state[tail_at + 2], 4);

			u64 pc = 0;
			bool ok = ReadRegister("dc.aica.arm7", "PC", &pc);
			bool enabled = state[tail_at + 6] != 0;

			Check(ok && pc == next, "dc.aica.arm7.PC is the next instruction, %08llX (R15 is %08X), mode %02X", (unsigned long long)pc, r15, mode);
			Check(scpu->DebugEnabled() == enabled, "the ARM7 is %s, and DebugEnabled() says so", enabled ? "running" : "held in reset");

			// the same in every mode: a state with a value of its own in every register is put into the sound
			// CPU, read, and taken out again
			Tally modes;

			for (u32 other : { 0x10u, 0x11u, 0x12u, 0x13u, 0x17u, 0x1Bu, 0x1Fu })
			{
				std::vector<u8> made = state;

				for (u32 i = 0; i < RN_ARM_REG_COUNT; i++)
				{
					u32 word = 0xA5000000 + other * 0x10000 + i * 0x0101;
					memcpy(&made[regs_at + i * 4], &word, 4);
				}

				made[tail_at] = (other & 1) != 0;
				made[tail_at + 1] = (other & 2) != 0;
				memcpy(&made[tail_at + 2], &other, 4);

				RestoreDevice(scpu, made);
				compare(made, &modes);
			}

			RestoreDevice(scpu, state);
			modes.Report("dc.aica.arm7 in each of the seven modes");
		}

		u32 value = 0x55AA;
		bool none = true;

		for (u32 reg : { (u32)RN_PSR_FLAGS, (u32)R15_ARM_NEXT, (u32)INTR_PEND, (u32)CYCL_CNT, (u32)RN_ARM_REG_COUNT, 1000u, 0xFFFFFFFFu })
			none = none && !scpu->DebugGetReg(reg, &value) && value == 0x55AA;

		Check(none, "DebugGetReg() gives nothing for an index that is no register of the debugger");
		Check(DeviceState(scpu) == state, "the sound CPU is as it was");
	}

	// the AICA register file: what the accessor delivers against byte reads by the SH4, which rewrite the
	// status of the selected channel and clear its loop flag
	{
		AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
		std::vector<u8> before = DeviceState(aica);
		std::vector<u8> mine(0x8000, 0xEE), again(0x8000, 0xDD);

		bool ok = aica->DebugReadRegs(0, 0x8000, mine.data()) && aica->DebugReadRegs(0, 0x8000, again.data());

		Check(ok && mine == again && DeviceState(aica) == before, "the register file reads twice the same and the AICA stands");

		// windows of the file are the bytes of the whole
		Tally windows;

		for (u32 offset : { 0u, 0x2808u, 0x2809u, 0x2810u, 0x2811u, 0x2814u, 0x2815u, 0x28A7u, 0x28B3u, 0x2C00u, 0x2C01u, 0x7FF0u })
		{
			for (u32 len : { 1u, 2u, 3u, 16u })
			{
				u8 part[16];

				if (offset + len > 0x8000)
					continue;

				windows.Add(aica->DebugReadRegs(offset, len, part) && memcmp(part, &mine[offset], len) == 0, "window %04X+%u differs from the whole", offset, len);
			}
		}

		windows.Report("windows of the register file");

		u8 byte = 0x77;
		Check(!aica->DebugReadRegs(0x7FFF, 2, &byte) && !aica->DebugReadRegs(0x8001, 0, &byte) && !aica->DebugReadRegs(0, 0x8001, mine.data()) &&
			aica->DebugReadRegs(0x8000, 0, &byte) && byte == 0x77, "a window that leaves the file is refused");

		Tally bytes;

		for (u32 offset = 0; offset < 0x8000; offset++)
		{
			u32 guest = aica->Read(0x00700000 + offset, 1) & 0xFF;

			bytes.Add(guest == again[offset], "register file byte %04X: %02X, a guest read gives %02X", offset, again[offset], guest);
		}

		bytes.Report("the register file against byte reads by the SH4");

		RestoreDevice(aica, before);
		Check(DeviceState(aica) == before, "the AICA is as it was");

		// channels
		Tally channels;
		AicaChannelDebug channel;

		for (u32 i = 0; i < 64; i++)
		{
			memset(&channel, 0xFF, sizeof(channel));
			channels.Add(aica->DebugGetChannel(i, &channel) && channel.aegLevel <= 0x3FF && channel.aegState <= 3 && channel.fegState <= 3 &&
				channel.caFraction < 1024, "channel %u: level %X state %u", i, channel.aegLevel, channel.aegState);
		}

		channels.Add(!aica->DebugGetChannel(64, &channel) && !aica->DebugGetChannel(0xFFFFFFFF, &channel), "a channel behind the last");
		channels.Report("the state of the 64 channels");
		Check(DeviceState(aica) == before, "the AICA is as it was after its channels were read");
	}

	// RTC
	{
		MMIODevice* rtc = sh4_cpu->GetA0Handler(A0H_RTC);
		u32 seconds = 0;
		bool write_enable = true;
		bool ok = libAICA_DebugGetRTC(&seconds, &write_enable);

		Check(ok && (seconds >> 16) == rtc->Read(0x00710000, 4) && (seconds & 0xFFFF) == rtc->Read(0x00710004, 4),
			"the RTC is at %u seconds, as its two registers", seconds);
	}

	// DSP
	{
		DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
		AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
		DspDebugState state = { 99, 99 };
		u64 step = 99, mdec_ct = 99;
		bool ok = dsp->DebugGetState(&state);

		Check(ok && state.step == 0 && state.mdecCt == dsp->GetDspContext()->regs.MDEC_CT, "DSP: step %u, MDEC_CT %u", state.step, state.mdecCt);

		ok = ReadRegister("dc.aica.dsp", "MDEC_CT", &mdec_ct) && ReadRegister("dc.aica.dsp", "STEP", &step);
		Check(ok && mdec_ct == state.mdecCt && step == 0, "dc.aica.dsp shows them");

		// RBP and RBL are fields of the common register 0x2804, whose read has no side effect
		u32 common = aica->ReadReg(0x2804, 2);
		u64 rbp = 0, rbl = 0;

		ok = ReadRegister("dc.aica.dsp", "RBP", &rbp) && ReadRegister("dc.aica.dsp", "RBL", &rbl);
		Check(ok && rbp == (common & 0xFFF) && rbl == ((common >> 13) & 3), "dc.aica.dsp.RBP %03llX and RBL %llX are the fields of %04X",
			(unsigned long long)rbp, (unsigned long long)rbl, common);

		Tally program;

		for (u32 n = 0; n < 128; n++)
		{
			u16 words[4] = { 0xEEEE, 0xEEEE, 0xEEEE, 0xEEEE };
			bool given = dsp->DebugGetProgramStep(n, words);

			for (u32 i = 0; i < 4; i++)
			{
				u32 guest = aica->ReadReg(0x3400 + n * 16 + i * 4, 2);

				program.Add(given && words[i] == guest, "program step %u word %u: %04X, the register reads %04X", n, i, words[i], guest);
			}
		}

		u16 words[4];
		program.Add(!dsp->DebugGetProgramStep(128, words), "a step behind the last");
		program.Report("the DSP program against its registers");
	}

	// the interrupt controller
	{
		ASIC* asic = sh4_cpu->GetA0H<ASIC>(A0H_ASIC);
		u32 value = 0;
		bool ok = asic->DebugReadISTNRM(&value);

		Check(ok && value == sh4_cpu->GetA0Handler(A0H_SB)->Read(SB_ISTNRM_addr, 4), "SB_ISTNRM is %08X, as a guest read", value);
	}

	// GD-ROM: the registers whose guest read changes nothing
	{
		MMIODevice* gd = sh4_cpu->GetA0Handler(A0H_GDROM);
		GDRomDebugState state;

		memset(&state, 0xEE, sizeof(state));

		if (gdrom_DebugGetState(&state))
		{
			std::vector<u8> before = DeviceState(gd);

			Check(state.status == gd->Read(GD_ALTSTAT_Read, 1) && state.interruptReason == gd->Read(GD_IREASON_Read, 1) &&
				state.sectorNumber == gd->Read(GD_SECTNUM, 1) && state.driveSelect == (u8)gd->Read(GD_DRVSEL, 1) &&
				state.byteCount == (gd->Read(GD_BYCTLLO, 1) | (gd->Read(GD_BYCTLHI, 1) << 8)),
				"GD-ROM: status %02X, interrupt reason %02X, sector number %02X, byte count %04X are what the registers read", state.status,
				state.interruptReason, state.sectorNumber, state.byteCount);
			Check((state.error >> 4) == (state.senseKey & 15) && state.pioIndex <= state.pioSize, "GD-ROM: error %02X carries sense key %X", state.error, state.senseKey);
			Check(DeviceState(gd) == before, "the GD-ROM drive is as it was");
		}
		else
			Note("this machine has no GD-ROM drive");
	}

	// boot ROM and flash: the stores against reads of their devices
	{
		const u8* data = nullptr;
		u32 size = 0;
		Tally stores;

		bool ok = area0_DebugRom(&data, &size);

		if (Check(ok && size != 0 && (size & (size - 1)) == 0, "the boot ROM has %u bytes", size))
		{
			MMIODevice* bios = sh4_cpu->GetA0Handler(A0H_BIOS);

			for (u32 offset = 0; offset < size; offset += 4099)
				stores.Add(data[offset] == bios->Read(offset, 1), "boot ROM byte %06X", offset);
		}

		ok = area0_DebugNvmem(&data, &size);

		if (Check(ok && size != 0 && (size & (size - 1)) == 0, "the flash memory has %u bytes", size))
		{
			MMIODevice* flash = sh4_cpu->GetA0Handler(A0H_FLASH);

			// the chip puts the region of the console in place of a few bytes at 0x1A000; the store has the file
			for (u32 offset = 0; offset < size; offset += 257)
			{
				if (offset < 0x1A000 || offset >= 0x1A100)
					stores.Add(data[offset] == flash->Read(offset, 1), "flash byte %05X", offset);
			}
		}

		stores.Report("boot ROM and flash stores against their devices");
	}
}

/*
	regs.mem.map: Classify() against the map of dbg_mem.h, in every mirror.
*/
DBG_SELFTEST(regs_mem_map, "regs.mem.map")
{
	Tally sh4;
	std::vector<MapRow> map = Sh4Map();

	for (size_t i = 0; i < map.size(); i++)
	{
		const MapRow& row = map[i];

		// the runs follow one another without a gap, from 0 to the end
		sh4.Add(row.first == (i == 0 ? 0 : map[i - 1].last + 1), "the map has a gap in front of %08X", row.first);

		for (u32 addr : { row.first, row.first + 1, row.first + (row.last - row.first) / 2, row.last - 1, row.last })
		{
			if (addr < row.first || addr > row.last)
				continue;

			mem::Region region = mem::Classify(Target::Sh4, addr);

			sh4.Add(region == row.region, "sh4 %08X is %s, expected %s", addr, RegionName(region), RegionName(row.region));
		}
	}

	sh4.Add(map.back().last == 0xFFFFFFFF, "the map ends at %08X", map.back().last);
	sh4.Report("the regions of the SH4, both ends and the middle of every run in every mirror");

	Tally others;

	for (const MapRow& row : arm7_map)
	{
		for (u32 addr : { row.first, row.first + (row.last - row.first) / 2, row.last })
			others.Add(mem::Classify(Target::Arm7, addr) == row.region, "arm7 %08X is %s", addr, RegionName(mem::Classify(Target::Arm7, addr)));
	}

	for (u32 addr : { 0u, 0x00800000u, 0x0C000000u, 0x8C010000u, 0xFFFFFFFFu })
		others.Add(mem::Classify(Target::Dsp, addr) == mem::Region::Unreadable, "dsp %08X is not Unreadable", addr);

	others.Report("the regions of the ARM7 and the DSP");
}

/*
	regs.mem.read: what Read() delivers, against the stores and the register readers.
*/
DBG_SELFTEST(regs_mem_read, "regs.mem.read")
{
	u8 buffer[0x100];
	u8 bits[0x20];

	// main memory and its mirrors
	{
		Tally ram;
		const u8* mram = sh4_cpu->mram.data;
		u32 mask = sh4_cpu->mram.size - 1;

		for (u32 addr : { 0x0C000000u, 0x8C010000u, 0xAC010000u, 0x0D010000u, 0x8CFFFFE0u, 0x8FFFFFC0u, 0xCC000100u, 0x6C000100u })
		{
			u32 n = mem::Read(Target::Sh4, addr, 0x40, buffer, bits);

			ram.Add(n == 0x40, "%08X: %u of 64 bytes readable", addr, n);
			for (u32 i = 0; i < 0x40; i++)
				ram.Add(buffer[i] == mram[(addr + i) & mask], "%08X+%X: %02X, main memory has %02X", addr, i, buffer[i], mram[(addr + i) & mask]);
		}

		// area 3 ends at 0x0FFFFFFF, and nothing readable follows it
		ram.Add(mem::Read(Target::Sh4, 0x8FFFFFF0, 0x40, buffer, bits) == 16 && bits[1] == 0xFF && bits[2] == 0, "8FFFFFF0: the end of area 3");

		ram.Report("main memory in its mirrors, across the end of a mirror and to the end of area 3");
	}

	// video memory: the 64-bit view from the store, the 32-bit view as the guest's handler maps it
	{
		Tally vram;
		const u8* store = sh4_cpu->vram.data;
		u32 mask = sh4_cpu->vram.size - 1;

		// the last one crosses the end of the store into its mirror
		for (u32 addr : { 0x04000000u, 0xA4123450u, 0x06000000u, 0x04000000u + sh4_cpu->vram.size - 0x20 })
		{
			u32 n = mem::Read(Target::Sh4, addr, 0x40, buffer, nullptr);

			vram.Add(n == 0x40, "%08X: %u of 64 bytes readable", addr, n);
			for (u32 i = 0; i < 0x40; i++)
				vram.Add(buffer[i] == store[(addr + i) & mask], "%08X+%X differs from video memory", addr, i);
		}

		for (u32 addr : { 0x05000000u, 0xA5000004u, 0xA5400000u, 0x07123454u, 0x053FFFF0u, 0xA57FFFE0u })
		{
			u32 n = mem::Read(Target::Sh4, addr, 0x40, buffer, nullptr);

			vram.Add(n == 0x40, "%08X: %u of 64 bytes readable", addr, n);
			for (u32 i = 0; i < 0x40; i += 4)
			{
				u32 mine = buffer[i] | (buffer[i + 1] << 8) | (buffer[i + 2] << 16) | ((u32)buffer[i + 3] << 24);
				u32 guest = pvr_read_area1_32((void*)store, addr + i);

				vram.Add(mine == guest, "%08X: %08X, the 32-bit view has %08X", addr + i, mine, guest);
			}

			// a byte keeps its lane: an odd start is the same bytes
			u8 odd[8];
			mem::Read(Target::Sh4, addr + 1, 7, odd, nullptr);
			vram.Add(memcmp(odd, buffer + 1, 7) == 0, "%08X: an unaligned read differs", addr + 1);
		}

		vram.Report("video memory in both views");
	}

	// wave memory, for both processors
	{
		Tally aram;
		const u8* store = sh4_cpu->aica_ram.data;
		u32 size = sh4_cpu->aica_ram.size;

		for (u32 addr : { 0x00800000u, 0xA0800100u, 0x00800000u + size - 0x20, 0x00FFFFC0u, 0x02800000u })
		{
			u32 n = mem::Read(Target::Sh4, addr, 0x40, buffer, nullptr);

			aram.Add(n == 0x40, "sh4 %08X: %u of 64 bytes readable", addr, n);
			for (u32 i = 0; i < 0x40; i++)
				aram.Add(buffer[i] == store[(addr + i) & (size - 1)], "sh4 %08X+%X differs from wave memory", addr, i);
		}

		for (u32 addr : { 0u, 0x100u, size - 0x20, 0x007FFFC0u })
		{
			u32 n = mem::Read(Target::Arm7, addr, 0x40, buffer, nullptr);

			aram.Add(n == 0x40, "arm7 %08X: %u of 64 bytes readable", addr, n);
			for (u32 i = 0; i < 0x40; i++)
				aram.Add(buffer[i] == store[(addr + i) & (size - 1)], "arm7 %08X+%X differs from wave memory", addr, i);
		}

		aram.Report("wave memory for the SH4 and the ARM7, with its mirrors");
	}

	// boot ROM and flash, and the end of the flash
	{
		const u8* rom = nullptr;
		const u8* flash = nullptr;
		u32 rom_size = 0, flash_size = 0;

		area0_DebugRom(&rom, &rom_size);
		area0_DebugNvmem(&flash, &flash_size);

		u32 n = mem::Read(Target::Sh4, 0xA0000000, 0x100, buffer, nullptr);
		Check(n == 0x100 && rom != nullptr && memcmp(buffer, rom, 0x100) == 0, "the boot ROM at 0xA0000000");

		n = mem::Read(Target::Sh4, 0x00000000, 0x100, buffer, nullptr);
		Check(n == 0x100 && rom != nullptr && memcmp(buffer, rom, 0x100) == 0, "the boot ROM at 0, where a guest read never returns");

		n = mem::Read(Target::Sh4, 0x02000000, 0x100, buffer, nullptr);
		Check(n == 0x100 && rom != nullptr && memcmp(buffer, rom, 0x100) == 0, "the boot ROM in the mirror of area 0");

		n = mem::Read(Target::Sh4, 0xA0200000, 0x100, buffer, nullptr);
		Check(n == 0x100 && flash != nullptr && memcmp(buffer, flash, 0x100) == 0, "the flash memory at 0xA0200000");

		memset(bits, 0xEE, sizeof(bits));
		n = mem::Read(Target::Sh4, 0x0021FFF0, 32, buffer, bits);

		bool zeros = true;
		for (u32 i = 16; i < 32; i++)
			zeros = zeros && buffer[i] == 0;

		Check(n == 16 && bits[0] == 0xFF && bits[1] == 0xFF && bits[2] == 0 && bits[3] == 0 && zeros && flash != nullptr &&
			memcmp(buffer, flash + ((0x1FFF0) & (flash_size - 1)), 16) == 0,
			"32 bytes across 0x0021FFF0: %u readable, bits %02X %02X %02X %02X", n, bits[0], bits[1], bits[2], bits[3]);
	}

	// what cannot be read
	{
		Tally holes;

		for (u32 addr : { 0x10000000u, 0x005F7000u, 0xA05F7080u, 0x005F70F0u, 0x00600000u, 0xA06007F0u, 0x00400000u, 0x08000000u, 0x14000000u,
			0xE4000000u, 0xF0000000u, 0xF6000000u, 0x7C000000u, 0x01000000u, 0x00708000u, 0x0071000Cu })
		{
			memset(buffer, 0xEE, 16);
			memset(bits, 0xEE, 2);

			u32 n = mem::Read(Target::Sh4, addr, 16, buffer, bits);
			bool zeros = bits[0] == 0 && bits[1] == 0;

			for (u32 i = 0; i < 16; i++)
				zeros = zeros && buffer[i] == 0;

			holes.Add(n == 0 && zeros, "sh4 %08X: %u bytes readable", addr, n);
		}

		for (u32 addr : { 0x00808000u, 0x01000000u, 0x80000000u, 0xFFFFFFF0u })
			holes.Add(mem::Read(Target::Arm7, addr, 16, buffer, bits) == 0, "arm7 %08X is readable", addr);

		for (u32 addr : { 0u, 0x00800000u, 0x8C010000u })
			holes.Add(mem::Read(Target::Dsp, addr, 16, buffer, bits) == 0, "dsp %08X is readable", addr);

		holes.Report("the GD-ROM block, the modem and what is not mapped read as nothing");

		// the GD-ROM drive was not touched
		MMIODevice* gd = sh4_cpu->GetA0Handler(A0H_GDROM);
		std::vector<u8> before = DeviceState(gd);

		mem::Read(Target::Sh4, 0x005F7000, 0x100, buffer, nullptr);
		Check(DeviceState(gd) == before, "reading the GD-ROM block leaves the drive alone");
	}

	// system bus registers
	{
		u32 word = 0, direct = 0;
		bool ok = mem::Read32(Target::Sh4, 0xA05F6900, &word) && regs::ReadSbRegisterAt(0x005F6900, &direct);

		Check(ok && word == direct, "SB_ISTNRM through memory is %08X", word);

		u32 n = mem::Read(Target::Sh4, 0x005F68FE, 8, buffer, bits);
		Check(n == 6 && bits[0] == 0xFC && buffer[0] == 0 && buffer[1] == 0, "a slot that is no register in front of SB_ISTNRM: %u of 8 readable, bits %02X", n, bits[0]);

		// SB_SFRES is write-only
		Check(mem::Read(Target::Sh4, 0x005F6890, 4, buffer, bits) == 0 && !mem::Read32(Target::Sh4, 0x005F6890, &word), "the write-only SB_SFRES is not readable");

		// SB_FFST does not count the read
		MMIODevice* sb = sh4_cpu->GetA0Handler(A0H_SB);
		std::vector<u8> before = DeviceState(sb);

		ok = mem::Read32(Target::Sh4, 0xA05F688C, &word) && mem::Read32(Target::Sh4, 0xA05F688C, &direct);

		Check(ok && word == direct && DeviceState(sb) == before, "SB_FFST through memory reads %08X twice and the poll counter stands", word);
	}

	// PVR registers
	{
		u32 n = mem::Read(Target::Sh4, 0xA05F8000, 0x100, buffer, nullptr);

		Check(n == 0x100 && memcmp(buffer, pvr_regs, 0x100) == 0 && mem::Read(Target::Sh4, 0x005F9FF0, 0x20, buffer, nullptr) == 0x10 &&
			memcmp(buffer, pvr_regs + 0x1FF0, 0x10) == 0, "the PVR registers, to the end of their block");
	}

	// AICA registers and RTC
	{
		AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
		std::vector<u8> before = DeviceState(aica);
		std::vector<u8> file(0x8000), sh4_view(0x8000), arm_view(0x8000);

		aica->DebugReadRegs(0, 0x8000, file.data());

		u32 n_sh4 = mem::Read(Target::Sh4, 0xA0700000, 0x8000, sh4_view.data(), nullptr);
		u32 n_arm = mem::Read(Target::Arm7, 0x00800000, 0x8000, arm_view.data(), nullptr);

		Check(n_sh4 == 0x8000 && n_arm == 0x8000 && sh4_view == file && arm_view == file && DeviceState(aica) == before,
			"the AICA register file through memory, for both processors, and the AICA stands");

		u32 seconds = 0;
		bool write_enable = false;
		libAICA_DebugGetRTC(&seconds, &write_enable);

		memset(bits, 0xEE, sizeof(bits));
		u32 n = mem::Read(Target::Sh4, 0x00710000, 16, buffer, bits);
		u32 high = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | ((u32)buffer[3] << 24);
		u32 low = buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | ((u32)buffer[7] << 24);

		Check(n == 12 && bits[0] == 0xFF && bits[1] == 0x0F && high == seconds >> 16 && low == (seconds & 0xFFFF) && buffer[8] == 0,
			"the RTC through memory: %u of 16 bytes, %04X %04X", n, high, low);
	}

	// store queues
	{
		const u8* sq = (const u8*)p_sh4rcb->sq_buffer;
		u32 n = mem::Read(Target::Sh4, 0xE0000000, 64, buffer, nullptr);
		bool ok = n == 64 && memcmp(buffer, sq, 64) == 0;

		n = mem::Read(Target::Sh4, 0xE3FFFFE0, 64, buffer, nullptr);
		ok = ok && n == 32 && memcmp(buffer, sq + 32, 32) == 0;

		n = mem::Read(Target::Sh4, 0xE1234570, 32, buffer, nullptr);
		ok = ok && n == 32 && memcmp(buffer, sq + 0x30, 16) == 0 && memcmp(buffer + 16, sq, 16) == 0;

		Check(ok, "the store queues, mirrored every 64 bytes up to 0xE3FFFFFF");
	}

	// on-chip registers, at both of their addresses
	{
		u32 p4 = 0, area7 = 0;
		bool ok = mem::Read32(Target::Sh4, 0xFF000030, &p4) && mem::Read32(Target::Sh4, 0x1F000030, &area7);

		Check(ok && p4 == 0x040205c1 && area7 == p4, "the CPU version register reads %08X at 0xFF000030 and at 0x1F000030", p4);

		// an 8-bit register is one byte; the three behind it belong to no register
		u64 tstr = 0;
		ReadRegister("dc.sh4.tmu", "TSTR", &tstr);

		memset(bits, 0xEE, sizeof(bits));
		u32 n = mem::Read(Target::Sh4, 0xFFD80004, 4, buffer, bits);
		Check(n == 1 && bits[0] == 0x01 && buffer[0] == tstr, "TSTR, 8 bits wide: %u of 4 bytes readable, %02X", n, buffer[0]);

		// 16 bits: TCR0
		u64 tcr0 = 0;
		ReadRegister("dc.sh4.tmu", "TCR0", &tcr0);

		u16 word = 0;
		ok = mem::Read16(Target::Sh4, 0xFFD80010, &word);

		Check(ok && word == tcr0 && mem::Read(Target::Sh4, 0xFFD80012, 2, buffer, nullptr) == 0, "TCR0, 16 bits wide, reads %04X and nothing behind it", word);

		Tally none;

		for (u32 addr : { 0xFFE80014u, 0xFFE80010u, 0xFF800030u, 0xFF00002Cu, 0xFF000100u, 0xFF010000u, 0xFF900000u, 0xFFF00000u, 0x1F000048u })
			none.Add(mem::Read(Target::Sh4, addr, 4, buffer, nullptr) == 0, "%08X is readable", addr);

		none.Report("the FIFO registers of the SCIF, PDTRA and addresses without a register are not readable");
	}

	// ends: nothing, the end of the address space, no bitmap
	{
		u8 sentinel[4] = { 0x11, 0x22, 0x33, 0x44 };

		Check(mem::Read(Target::Sh4, 0x8C010000, 0, sentinel, sentinel) == 0 && sentinel[0] == 0x11, "a read of no bytes writes nothing");

		const u8* rom = nullptr;
		u32 rom_size = 0;
		area0_DebugRom(&rom, &rom_size);

		memset(bits, 0xEE, sizeof(bits));
		u32 n = mem::Read(Target::Sh4, 0xFFFFFFF0, 32, buffer, bits);
		Check(n == 16 && bits[0] == 0 && bits[1] == 0 && bits[2] == 0xFF && bits[3] == 0xFF && rom != nullptr && memcmp(buffer + 16, rom, 16) == 0,
			"a read across 0xFFFFFFFF goes on at 0: %u of 32 readable", n);

		u16 word = 0;
		u32 dword = 0;
		Check(mem::Read16(Target::Sh4, 0x8C010000, &word) && mem::Read32(Target::Sh4, 0x8C010000, &dword) && word == (dword & 0xFFFF) &&
			!mem::Read16(Target::Sh4, 0x0021FFFF, &word) && !mem::Read32(Target::Sh4, 0x0021FFFE, &dword), "Read16 and Read32 need every byte");

		Check(mem::Write(Target::Sh4, 0x8C010000, 1, buffer) == kErrNotSupported, "Write() says that no writer exists");
	}

	// the 64 KiB around every boundary: every byte readable or not as the map and the registers say
	{
		Tally windows;
		u32 count = 0;

		ForEachBoundaryWindow([&](Target t, u32 addr, u32 len) {
			CheckWindow(t, addr, len, &windows);
			count++;
		});

		windows.Report("readable bits of the 64 KiB windows around every region boundary");
		Note("%u windows", count);
	}
}

/*
	regs.pure: reading changes nothing. The machine, as a save state has it, before and after everything the
	debugger can read was read.
*/
DBG_SELFTEST(regs_pure, "regs.pure")
{
	std::vector<u8> before, after;

	bool quiescent = WaitQuiescent(&before);

	if (!Check(quiescent, "the machine stands still: two save states of %zu bytes are equal", before.size()))
		return;

	// every register
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);
	u32 registers = 0;

	for (u32 i = 0; i < count; i++)
	{
		for (u32 r = 0; r < nodes[i].registerCount; r++)
		{
			u64 value;
			char text[64];

			regs::Read(nodes[i], r, &value, text, sizeof(text));
			registers++;
		}
	}

	// every device accessor
	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	DSP* dsp = sh4_cpu->GetA0H<DSP>(A0H_DSP);
	std::vector<u8> file(0x8000);
	AicaChannelDebug channel;
	DspDebugState dsp_state;
	GDRomDebugState gd_state;
	u16 words[4];
	u32 word;
	bool flag;
	const u8* data;
	u32 size;

	aica->DebugReadRegs(0, 0x8000, file.data());
	for (u32 i = 0; i < 64; i++)
		aica->DebugGetChannel(i, &channel);
	for (u32 reg = 0; reg < RN_ARM_REG_COUNT; reg++)
		scpu->DebugGetReg(reg, &word);
	scpu->DebugEnabled();
	dsp->DebugGetState(&dsp_state);
	for (u32 step = 0; step < 128; step++)
		dsp->DebugGetProgramStep(step, words);
	libAICA_DebugGetRTC(&word, &flag);
	gdrom_DebugGetState(&gd_state);
	area0_DebugRom(&data, &size);
	area0_DebugNvmem(&data, &size);
	sh4_cpu->GetA0H<ASIC>(A0H_ASIC)->DebugReadISTNRM(&word);
	for (u32 addr = SB_BASE; addr < SB_BASE + 0x1500; addr += 4)
		sh4_cpu->GetA0H<SystemBus>(A0H_SB)->DebugRead(addr, &word);

	// memory: the 64 KiB around every boundary, and the register regions as a whole
	std::vector<u8> window(0x10000), bits(0x2000);
	u32 windows = 0;
	u64 bytes = 0;

	ForEachBoundaryWindow([&](Target t, u32 addr, u32 len) {
		mem::Read(t, addr, len, window.data(), bits.data());
		windows++;
		bytes += len;
	});

	for (u32 addr : { 0xA05F6800u, 0xA05F8000u, 0xA0700000u, 0xFF000000u, 0xFFD80000u, 0xFFE80000u, 0x1F000000u, 0xE0000000u })
	{
		mem::Read(Target::Sh4, addr, 0x10000, window.data(), bits.data());
		bytes += 0x10000;
	}

	if (!Check(MachineState(&after), "a second save state"))
		return;

	size_t at = FirstDifference(before, after);

	if (before == after)
		Check(true, "%u registers, the device accessors and %u windows (%llu bytes) read: the save state of the machine is the same %zu bytes", registers,
			windows, (unsigned long long)bytes, before.size());
	else
	{
		// the renderer may have started a frame of its own: it shows in a state taken without any read
		std::vector<u8> control;
		MachineState(&control);

		Check(false, "the save state differs at byte %zu of %zu (%02X, then %02X)%s", at, before.size(), at < before.size() ? before[at] : 0,
			at < after.size() ? after[at] : 0, control == after ? "" : "; it moves without reads as well: run again");
	}
}
