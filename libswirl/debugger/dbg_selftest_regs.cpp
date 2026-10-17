/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_selftest.h"

#include "dbg_core.h"
#include "dbg_mem.h"
#include "dbg_regs.h"
#include "dbg_regs_private.h"

#include "hw/aica/aica.h"
#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp.h"
#include "hw/aica/dsp_backend.h"
#include "hw/arm7/SoundCPU.h"
#include "hw/arm7/arm7.h"
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

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

/*
	Selftests of dbg::regs, dbg::mem and the device accessors behind them. All but the last are machine tests:
	they read, and where they compare with what the guest would read they put the machine back as it was. The
	last, regs.enabled, is a guest test: it runs the booted program and reads at stops of it.

	The rows of the devices (the system bus, the PVR's register file, the AICA's register file by field, its
	clock, the GD-ROM drive) are read at the machine's own state, which is that of a reset and mostly zero, and
	at states the tests make up: a value of its own in every word a row reads, so that a row that reads its
	neighbour's word, another channel's, or another bit of its own shows. A made-up state is put into a device
	through its save state or its register array, never by running the guest, and the device is put back.

	Where a value has a second source that is not the debugger's (the SH4 context, a save state of a device, a
	guest read that is known to have no side effect) the two are compared. Guest reads are made by this file
	only, never by the modules under test (but for the two bytes of the ARM7 that dbg_mem.cpp names), and only
	of registers whose read function changes nothing; the two reads that do change something (the FIFO status of
	the system bus, the status of an AICA channel) are followed by putting the device back from a save state of
	it.

	The register window of the AICA for the ARM7 has the ARM7 itself as its second source: Arm7Loads executes a
	loop of the test's own on the sound CPU, and puts back what that changed.
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

// The settings the flash chip of a Dreamcast puts in place of stored bytes when a guest reads them: region at
// 0x1A002 and 0x1A0A2, language at 0x1A003 and 0x1A0A3, broadcast at 0x1A004 and 0x1A0A4, each while it names a
// value. The last row is every setting left at its default: the stored bytes are read.
struct FlashSettings {
	u32 region, language, broadcast;
};

const FlashSettings flash_settings[] = { { 1, 3, 2 }, { 2, 5, 0 }, { 0, 0, 3 }, { 3, 6, 4 } };

FlashSettings SetFlashSettings(const FlashSettings& now)
{
	FlashSettings before = { settings.dreamcast.region, settings.dreamcast.language, settings.dreamcast.broadcast };

	settings.dreamcast.region = now.region;
	settings.dreamcast.language = now.language;
	settings.dreamcast.broadcast = now.broadcast;

	return before;
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
	RegisterStruct* slots;		// not const: regs.mem.read gives the slots values of its own, and puts them back
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

// The rows of the tables for the on-chip registers by address (P4), from one walk over every row of every node.
// Which nodes have such rows is not asked: dbg::regs answers that for the walk of its own (the nodes of the
// modules, known by their first row), and this is what that walk is compared with
const std::map<u32, const regs::Register*>& MmrRows()
{
	static const std::map<u32, const regs::Register*> rows = [] {
		std::map<u32, const regs::Register*> by_address;
		u32 count;
		const regs::Node* nodes = regs::Nodes(&count);

		for (u32 i = 0; i < count; i++)
		{
			for (u32 r = 0; r < nodes[i].registerCount; r++)
			{
				const regs::Register& reg = nodes[i].registers[r];

				// the first row of an address, in the order of the tables
				if (regs::KeySource(reg.key) == regs::kSrcSh4Mmr)
					by_address.emplace(reg.addr, &reg);
			}
		}

		return by_address;
	}();

	return rows;
}

// The row of the tables for an on-chip register address (P4), null when there is none.
const regs::Register* MmrRow(u32 p4)
{
	const auto row = MmrRows().find(p4);

	return row == MmrRows().end() ? nullptr : row->second;
}

// The slot that holds the value of an on-chip register: of a row that is plain data, when the slot is registered
// and not behind a read function (a slot behind one holds the address of that function). Null for any other row.
RegisterStruct* MmrDataSlot(const regs::Register& reg)
{
	if (regs::KeySource(reg.key) != regs::kSrcSh4Mmr || regs::KeySelector(reg.key) != regs::kMmrData)
		return nullptr;

	for (const ModuleSlots& module : Modules())
	{
		const u32 index = (reg.addr - module.base) / 4;

		if (reg.addr < module.base || index >= module.count)
			continue;

		RegisterStruct* slot = &module.slots[index];

		return Registered(*slot) && !(slot->flags & REG_RF) ? slot : nullptr;
	}

	return nullptr;
}

// What the on-chip modules write into a save state: the on-chip RAM, every slot of every module, and what each
// module keeps beside its slots.
std::vector<u8> OnChipState()
{
	SuperH4Mmr* mmr = sh4_cpu->sh4mmr.get();
	void* data = nullptr;
	unsigned int size = 0;
	mmr->serialize(&data, &size);

	std::vector<u8> state(size);
	data = state.data();
	size = 0;
	mmr->serialize(&data, &size);

	return state;
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

// What the sound CPU writes into a save state: bool aica_interr, u32 aica_reg_L, bool e68k_out, u32 e68k_reg_L,
// u32 e68k_reg_M, the registers, bool armIrqEnable, bool armFiqEnable, int armMode, bool enabled
const size_t scpu_out_at = 1 + 4;
const size_t scpu_level_at = scpu_out_at + 1;
const size_t scpu_service_at = scpu_level_at + 4;
const size_t scpu_regs_at = scpu_service_at + 4;
const size_t scpu_tail_at = scpu_regs_at + RN_ARM_REG_COUNT * 4;
const size_t scpu_state_size = scpu_tail_at + 1 + 1 + 4 + 1;

// A loop of the ARM7: it copies the bytes from R0 up to R3 to R1, one by one, and then stays where it is
const u32 arm7_loads[] = {
	0xE4D02001,		// ldrb r2, [r0], #1
	0xE4C12001,		// strb r2, [r1], #1
	0xE1500003,		// cmp  r0, r3
	0x1AFFFFFB,		// bne  the ldrb
	0xEAFFFFFE,		// b    .
};

/*
	Byte loads by the ARM7 itself: what the bytes [first, first + count) are for the sound CPU, count up to 0x8000.

	The sound CPU is given a state that executes the loop above out of the wave memory: one LDRB for every byte,
	stored in the wave memory behind the loop. Then the bytes of the wave memory, the sound CPU, the AICA and the
	interrupt line of the AICA to the SH4 are put back as they were.

	SoundCPU::Update() is what executes code of the ARM7. It runs the core in 32 slices and steps the AICA by a
	sample behind each of them: the timers count, the sample interval is raised, the interrupt lines follow. A
	slice is 64 cycles for every instruction of the loop, of which the core charges 10 at most, so the loop ends
	within the first slice: every load finds the registers as they were when this was called. The state it runs
	on masks FIQ, so that a request in front of nFIQ does not take the core out of the loop. What the 32 steps
	and the loads themselves change (the status of the selected channel and its loop flag, the acknowledge of a
	request) is in the two save states that are put back; the sound generator is kept from producing samples in
	the steps, which no save state of the two would undo.

	*loads are the bytes. Returns false when the loop did not come to its end or something is not as it was.
*/
bool Arm7Loads(u32 first, u32 count, std::vector<u8>* loads)
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	ASIC* asic = sh4_cpu->GetA0H<ASIC>(A0H_ASIC);
	SystemBus* sb = sh4_cpu->GetA0H<SystemBus>(A0H_SB);
	u8* ram = sh4_cpu->aica_ram.data;

	// the loop and what it stores are in the last 64 KiB of the wave memory
	const u32 loop_at = sh4_cpu->aica_ram.size - 0x10000;
	const u32 store_at = loop_at + sizeof(arm7_loads);

	std::vector<u8> cpu = DeviceState(scpu);
	std::vector<u8> device = DeviceState(aica);

	if (count == 0 || count > 0x8000 || cpu.size() != scpu_state_size)
		return false;

	std::vector<u8> wave(ram + loop_at, ram + store_at + count);
	const u32 line_bit = 1 << (u8)holly_SPU_IRQ;
	u32 line = SB_ISTEXT & line_bit;
	bool no_batch = settings.aica.NoBatch;

	std::vector<u8> made = cpu;
	auto set = [&](u32 reg, u32 value) { memcpy(&made[scpu_regs_at + reg * 4], &value, 4); };

	set(0, first);
	set(1, store_at);
	set(3, first + count);
	set(R15_ARM_NEXT, loop_at);
	set(INTR_PEND, 0);
	made[scpu_tail_at] = 0;			// IRQ and FIQ masked
	made[scpu_tail_at + 1] = 0;
	made[scpu_tail_at + 6] = 1;		// not held in reset

	memcpy(ram + loop_at, arm7_loads, sizeof(arm7_loads));
	memset(ram + store_at, 0, count);
	RestoreDevice(scpu, made);
	scpu->InvalidateJitCache();

	settings.aica.NoBatch = false;
	scpu->Update(count * 4 * 64 * 32 * arm_sh4_bias);
	settings.aica.NoBatch = no_batch;

	// where the core stands, and how far it came
	std::vector<u8> behind = DeviceState(scpu);
	u32 r0, next;

	memcpy(&r0, &behind[scpu_regs_at], 4);
	memcpy(&next, &behind[scpu_regs_at + R15_ARM_NEXT * 4], 4);
	loads->assign(ram + store_at, ram + store_at + count);

	memcpy(ram + loop_at, wave.data(), wave.size());
	RestoreDevice(aica, device);
	RestoreDevice(scpu, cpu);
	scpu->InvalidateJitCache();

	// a timer that ran out in the steps has raised the interrupt of the AICA
	if ((SB_ISTEXT & line_bit) != line)
	{
		if (line)
			asic->RaiseInterrupt(holly_SPU_IRQ);
		else
			asic->CancelInterrupt(holly_SPU_IRQ);
	}

	return r0 == first + count && next == loop_at + 16 && DeviceState(scpu) == cpu && DeviceState(aica) == device;
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

	// The system bus: a row is a registered slot, and a slot has one row at most. A slot that refuses a read (it
	// is write-only) is a row without a reader, and only such a slot is; of the others a slot behind a read
	// function is a computed row, and only such a slot is
	SystemBus* sb = sh4_cpu->GetA0H<SystemBus>(A0H_SB);
	Tally sb_rows;
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);
	u32 with_row = 0, write_only_rows = 0;
	std::vector<u32> rows_of(sb->sb_regs.size(), 0);

	for (u32 i = 0; i < count; i++)
	{
		for (u32 r = 0; r < nodes[i].registerCount; r++)
		{
			const regs::Register& reg = nodes[i].registers[r];

			if (regs::KeySource(reg.key) != regs::kSrcSb)
				continue;

			const u32 index = (reg.addr - SB_BASE) / 4;
			const RegisterStruct& slot = sb->sb_regs[index];
			const bool write_only = (slot.flags & REG_WO) != 0;

			with_row++;
			write_only_rows += write_only;
			rows_of[index]++;

			sb_rows.Add(Registered(slot), "%s.%s: slot flags %02X: no register", nodes[i].path, reg.name, slot.flags);
			sb_rows.Add(write_only == ((reg.flags & regs::kRegNoReader) != 0), "%s.%s: slot flags %02X, register flags %02X: write-only and no reader disagree",
				nodes[i].path, reg.name, slot.flags, reg.flags);
			sb_rows.Add((!write_only && (slot.flags & REG_RF) != 0) == ((reg.flags & regs::kRegComputed) != 0), "%s.%s: slot flags %02X, register flags %02X",
				nodes[i].path, reg.name, slot.flags, reg.flags);
		}
	}

	sb_rows.Report("system bus rows that agree with their slot: registered, without a reader exactly when write-only, computed exactly when behind a read function");

	// the registered slots without a row are the ten that have no name in sb_regs.h, all of them write-only
	u32 registered = 0, twice = 0, no_row = 0, no_row_readable = 0;

	for (u32 index = 0; index < sb->sb_regs.size(); index++)
	{
		const RegisterStruct& slot = sb->sb_regs[index];

		twice += rows_of[index] > 1;

		if (!Registered(slot))
			continue;

		registered++;

		if (rows_of[index] == 0)
		{
			no_row++;
			no_row_readable += !(slot.flags & REG_WO);
		}
	}

	Check(twice == 0 && with_row == 127 && registered == with_row + no_row && no_row_readable == 0,
		"%u of the %u registered system bus registers have a row, none has two (%u); the %u without one are write-only (%u are not)", with_row, registered, twice, no_row,
		no_row_readable);
	Note("%u system bus rows are on write-only slots and read as n/a", write_only_rows);
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

			// a machine without the GD-ROM drive has no snapshot of it
			GDRomDebugState drive;

			if (regs::KeySource(reg.key) == regs::kSrcGdrom && !gdrom_DebugGetState(&drive))
				want = false;

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

		// A write that the writer refuses, having changed nothing: behind the last register of the node, and to
		// its first register without a writer. (What a write that is taken does is the session "writes".)
		edges.Add(regs::Write(node, node.registerCount, 0) == kErrNotWritable && regs::Write(node, 0xFFFFFFFF, 0) == kErrNotWritable, "%s: Write() behind the last register",
			node.path);

		for (u32 r = 0; r < node.registerCount; r++)
		{
			if (node.registers[r].flags & regs::kRegWritable)
				continue;

			u64 before = 0, after = 0;
			const bool readable = node.registers[r].width != 0 && regs::Read(node, r, &before, nullptr, 0);

			edges.Add(regs::Write(node, r, ~before) == kErrNotWritable && (!readable || (regs::Read(node, r, &after, nullptr, 0) && after == before)),
				"%s.%s: Write() to a register without a writer", node.path, node.registers[r].name);
			break;
		}
	}

	reads.Report("registers that read as their flags say");
	values.Report("values inside their width, texts not empty, outputs untouched on failure");
	edges.Report("reads without outputs and outside a node, and writes that are refused");

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

		if (Check(state.size() == scpu_state_size, "the save state of the sound CPU has %zu bytes", state.size()))
		{
			const regs::Node* arm7 = regs::Find("dc.aica.arm7");

			// every row of dc.aica.arm7 against what the bytes of a save state say it is
			auto compare = [&](const std::vector<u8>& bytes, Tally* tally) {
				u32 raw[RN_ARM_REG_COUNT];
				memcpy(raw, &bytes[scpu_regs_at], sizeof(raw));

				bool irq = bytes[scpu_tail_at] != 0;
				bool fiq = bytes[scpu_tail_at + 1] != 0;
				u32 mode;
				memcpy(&mode, &bytes[scpu_tail_at + 2], 4);

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
					u64 value = ~(u64)0;

					regs::Read(*arm7, r, &value, nullptr, 0);

					// the one row that is no register of the core: whether it is clocked, the last byte of the state
					if (index == regs::kArm7Enabled)
					{
						u32 clocked = bytes[scpu_tail_at + 6] != 0;

						tally->Add(value == clocked, "dc.aica.arm7.%s in mode %02X: %llX, the save state gives %X", reg.name, mode, (unsigned long long)value, clocked);
						continue;
					}

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
			memcpy(&next, &state[scpu_regs_at + R15_ARM_NEXT * 4], 4);
			memcpy(&r15, &state[scpu_regs_at + 15 * 4], 4);
			memcpy(&mode, &state[scpu_tail_at + 2], 4);

			u64 pc = 0;
			bool ok = ReadRegister("dc.aica.arm7", "PC", &pc);
			bool enabled = state[scpu_tail_at + 6] != 0;

			Check(ok && pc == next, "dc.aica.arm7.PC is the next instruction, %08llX (R15 is %08X), mode %02X", (unsigned long long)pc, r15, mode);
			Check(scpu->DebugEnabled() == enabled, "the ARM7 is %s, and DebugEnabled() says so", enabled ? "running" : "held in reset");

			// ENABLED is that flag, one bit of it: what a client sees of the reset of the ARM7. No guest has run
			// yet, so the machine's own reset holds the ARM7
			int flag = arm7 != nullptr ? regs::FindRegister(*arm7, "ENABLED") : -1;
			u32 width = flag >= 0 ? arm7->registers[flag].width : 0;
			u64 clocked = ~(u64)0;

			ok = ReadRegister("dc.aica.arm7", "ENABLED", &clocked);

			Check(ok && width == 1 && clocked == 0 && !enabled,
				"dc.aica.arm7.ENABLED has %u bit and reads %llX before the guest has executed anything: the ARM7 is held in reset", width,
				(unsigned long long)clocked);

			// the same in every mode: a state with a value of its own in every register is put into the sound
			// CPU, read, and taken out again. Two of the states are of a core that is clocked
			Tally modes;

			for (u32 other : { 0x10u, 0x11u, 0x12u, 0x13u, 0x17u, 0x1Bu, 0x1Fu })
			{
				std::vector<u8> made = state;

				for (u32 i = 0; i < RN_ARM_REG_COUNT; i++)
				{
					u32 word = 0xA5000000 + other * 0x10000 + i * 0x0101;
					memcpy(&made[scpu_regs_at + i * 4], &word, 4);
				}

				made[scpu_tail_at] = (other & 1) != 0;
				made[scpu_tail_at + 1] = (other & 2) != 0;
				memcpy(&made[scpu_tail_at + 2], &other, 4);
				made[scpu_tail_at + 6] = (other & 8) != 0;

				RestoreDevice(scpu, made);
				compare(made, &modes);
			}

			RestoreDevice(scpu, state);
			modes.Report("dc.aica.arm7 in each of the seven modes");

			// ENABLED in both of its states, with every other byte of the sound CPU as the machine has it: the
			// register is the flag of the save state, and DebugEnabled(), which is what the unwinder of the ARM7
			// call stack asks (dbg_callstack.h), says the same
			for (bool made_clocked : { true, false })
			{
				std::vector<u8> made = state;

				made[scpu_tail_at + 6] = made_clocked;
				RestoreDevice(scpu, made);

				clocked = ~(u64)0;
				ok = ReadRegister("dc.aica.arm7", "ENABLED", &clocked);

				Check(ok && clocked == (made_clocked ? 1u : 0u) && scpu->DebugEnabled() == made_clocked,
					"a sound CPU that is %s: dc.aica.arm7.ENABLED reads %llX, and DebugEnabled() is %d", made_clocked ? "clocked" : "held in reset",
					(unsigned long long)clocked, (int)scpu->DebugEnabled());
			}

			RestoreDevice(scpu, state);
		}

		// the interrupt controller in front of nFIQ: L and the in-service flag against the save state, and the
		// ARM7's view of the two registers through memory. Looking is no acknowledge: the state stands, with
		// its request line
		if (state.size() >= scpu_regs_at)
		{
			Tally controller;

			auto compare = [&](const std::vector<u8>& bytes) {
				u32 want_level, want_service;
				memcpy(&want_level, &bytes[scpu_level_at], 4);
				memcpy(&want_service, &bytes[scpu_service_at], 4);

				u32 level = ~want_level, in_service = ~want_service;
				bool ok = scpu->DebugGetInterrupt(&level, &in_service);

				controller.Add(ok && level == want_level && in_service == want_service, "DebugGetInterrupt gives L %X and in service %X, the save state has %X and %X",
					level, in_service, want_level, want_service);

				AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
				u8 file[8] = { }, view[8] = { }, view_bits[1] = { };

				aica->DebugReadRegs(0x2D00, 8, file);

				u32 n = mem::Read(Target::Arm7, 0x00802D00, 8, view, view_bits);

				file[0] = (u8)want_level;
				file[4] = 0;
				controller.Add(n == 8 && view_bits[0] == 0xFF && memcmp(view, file, 8) == 0, "the ARM7 at 0x00802D00: %u of 8 bytes, L reads %02X and M reads %02X, with L %X in the save state",
					n, view[0], view[4], want_level);

				controller.Add(DeviceState(scpu) == bytes, "reading L and M changed the sound CPU");
			};

			compare(state);

			// a request latched and nFIQ pulled, one that the ARM7 has acknowledged, one whose service has ended
			// with nFIQ still pulled, and nothing at all
			const struct { u32 level, service; bool out; } made_up[] = { { 5, 1, true }, { 7, 1, false }, { 1, 0, true }, { 0, 0, false } };

			for (const auto& c : made_up)
			{
				std::vector<u8> made = state;

				made[scpu_out_at] = c.out;
				memcpy(&made[scpu_level_at], &c.level, 4);
				memcpy(&made[scpu_service_at], &c.service, 4);

				RestoreDevice(scpu, made);
				compare(made);
			}

			RestoreDevice(scpu, state);
			controller.Report("the interrupt controller of the sound CPU against its save state");
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
		DspDebugState state;
		u64 step = 99, mdec_ct = 99;

		memset(&state, 0xEE, sizeof(state));

		bool ok = dsp->DebugGetState(&state);

		Check(ok && state.step == 0 && state.mdecCt == dsp->GetDspContext()->regs.MDEC_CT, "DSP: step %u, MDEC_CT %u", state.step, state.mdecCt);

		// The word of 0x2804 as the DSP uses it, which the rows of RBP, RBL and TESTB0 read: the register file's word,
		// from which this DSP decodes RBP and RBL. Its datapath lives in locals of a sample: every field of it is 0
		{
			u8 bytes[2] = { 0xEE, 0xEE };
			const bool read = aica->DebugReadRegs(0x2804, 2, bytes);
			const u32 file = bytes[0] | (bytes[1] << 8);

			Check(ok && read && state.rbpl == file && state.acc == 0 && state.yReg == 0 && state.frcReg == 0 && state.adrsReg == 0 && state.latch == 0
				&& state.latchCopy == 0 && state.memval == 0 && state.noflPipe == 0 && state.mixsBank == 0,
				"DSP: rbpl %04X, the register file has %04X at 0x2804; the datapath reads 0 (ACC %X, Y_REG %X, FRC_REG %X, ADRS_REG %X, the latch %X and its copy %X, "
				"MEMVAL %X, NOFL %X, the MIXS bank %X)", state.rbpl, file, state.acc, state.yReg, state.frcReg, state.adrsReg, state.latch, state.latchCopy,
				state.memval, state.noflPipe, state.mixsBank);
		}

		// MDEC_CT has the 16 bits of the register; the emulator keeps the counter in 32
		ok = ReadRegister("dc.aica.dsp", "MDEC_CT", &mdec_ct) && ReadRegister("dc.aica.dsp", "STEP", &step);
		Check(ok && mdec_ct == (state.mdecCt & 0xFFFF) && step == 0, "dc.aica.dsp shows them");

		// the widths of the node: those of the registers on the hardware
		{
			const regs::Node* node = regs::Find("dc.aica.dsp");
			const struct { const char* name; u32 width; } widths[] = { { "STEP", 8 }, { "MDEC_CT", 16 }, { "RBP", 16 }, { "RBL", 8 } };
			Tally tally;

			for (const auto& want : widths)
			{
				int index = node ? regs::FindRegister(*node, want.name) : -1;

				tally.Add(index >= 0 && node->registers[index].width == want.width, "dc.aica.dsp.%s: width %u, expected %u", want.name,
					index >= 0 ? node->registers[index].width : 0, want.width);
			}

			tally.Add(node != nullptr && node->registerCount == 4, "dc.aica.dsp has other registers than these four");
			tally.Report("the widths of dc.aica.dsp");
		}

		// The counter is reloaded with the ring buffer length, which is 0x10000 for the longest ring: the register
		// shows the counter modulo 2^16, so 0x0000 for that sample, and its text has four digits
		{
			u32 kept = dsp->GetDspContext()->regs.MDEC_CT;
			const u32 counters[] = { 0x10000, 0xFFFF, 0x2000, 1 };
			Tally tally;

			for (u32 counter : counters)
			{
				u64 shown = 99;
				char text[24];

				dsp->GetDspContext()->regs.MDEC_CT = counter;
				bool read = ReadRegister("dc.aica.dsp", "MDEC_CT", &shown);
				regs::FormatValue(16, counter, text);

				tally.Add(read && shown == (counter & 0xFFFF), "MDEC_CT %X shows as %llX", counter, (unsigned long long)shown);
				tally.Add(strlen(text) == 6 && strtoul(text, nullptr, 16) == (counter & 0xFFFF), "MDEC_CT %X has the text %s", counter, text);
			}

			dsp->GetDspContext()->regs.MDEC_CT = kept;
			tally.Report("MDEC_CT modulo 2^16");
		}

		// RBP and RBL are fields of the common register 0x2804, whose read has no side effect
		u32 common = aica->ReadReg(0x2804, 2);
		u64 rbp = 0, rbl = 0;

		ok = ReadRegister("dc.aica.dsp", "RBP", &rbp) && ReadRegister("dc.aica.dsp", "RBL", &rbl);
		Check(ok && rbp == (common & 0xFFF) && rbl == ((common >> 13) & 3), "dc.aica.dsp.RBP %04llX and RBL %02llX are the fields of %04X",
			(unsigned long long)rbp, (unsigned long long)rbl, common);

		// A word the SH4 writes to 0x2804, with every field not 0: the five rows of its bits, of dc.aica.dsp and of
		// dc.aica.common, read it as the DSP has it. The AICA and the DSP are put back as they were
		{
			std::vector<u8> aica_kept = DeviceState(aica), dsp_kept = DeviceState(dsp);
			const u32 made = 0xC5A3;	// TESTB0 1, RBL 2, RBP 0x5A3
			DspDebugState written;
			u64 d_rbp = 99, d_rbl = 99, c_rbp = 99, c_rbl = 99, c_test = 99;

			aica->WriteReg(0x2804, made, 2);

			const bool read = dsp->DebugGetState(&written) && ReadRegister("dc.aica.dsp", "RBP", &d_rbp) && ReadRegister("dc.aica.dsp", "RBL", &d_rbl)
				&& ReadRegister("dc.aica.common", "RBP", &c_rbp) && ReadRegister("dc.aica.common", "RBL", &c_rbl) && ReadRegister("dc.aica.common", "TESTB0", &c_test);

			Check(read && written.rbpl == made && d_rbp == 0x5A3 && d_rbl == 2 && c_rbp == 0x5A3 && c_rbl == 2 && c_test == 1,
				"0x2804 written with %04X: rbpl %04X; dc.aica.dsp.RBP %llX and RBL %llX, dc.aica.common.RBP %llX, RBL %llX and TESTB0 %llX", made, written.rbpl,
				(unsigned long long)d_rbp, (unsigned long long)d_rbl, (unsigned long long)c_rbp, (unsigned long long)c_rbl, (unsigned long long)c_test);

			RestoreDevice(aica, aica_kept);
			RestoreDevice(dsp, dsp_kept);
			Check(DeviceState(aica) == aica_kept && DeviceState(dsp) == dsp_kept, "the AICA and the DSP are as they were");
		}

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

	// the flash memory byte by byte as a guest read delivers it, which is not the store where the chip puts a
	// setting in its place
	{
		const u8* data = nullptr;
		u32 size = 0;
		u8 byte = 0xEE;

		if (Check(area0_DebugNvmem(&data, &size) && area0_DebugNvmemByte(0, &byte), "the flash memory gives its bytes one by one"))
		{
			MMIODevice* flash = sh4_cpu->GetA0Handler(A0H_FLASH);
			Tally bytes, substituted;

			auto compare_at = [&](u32 offset) {
				byte = 0xEE;
				bool ok = area0_DebugNvmemByte(offset, &byte);
				u32 guest = flash->Read(offset, 1);

				bytes.Add(ok && byte == guest, "flash byte %05X: %02X, a guest read delivers %02X", offset, byte, guest);
			};

			auto compare = [&]() {
				// the whole memory in steps, and every byte around the settings
				for (u32 offset = 0; offset < size; offset += 257)
					compare_at(offset);

				for (u32 offset = 0x1A000; offset < 0x1A100 && offset < size; offset++)
					compare_at(offset);

				// an offset is taken modulo the size of the store
				u8 mirrored = 0xEE;
				bool ok = area0_DebugNvmemByte(size + 0x1A002, &mirrored) && area0_DebugNvmemByte(0x1A002 & (size - 1), &byte);

				bytes.Add(ok && mirrored == byte, "flash byte %05X behind the end of the store: %02X, at its place %02X", 0x1A002, mirrored, byte);
			};

			// as the run has its settings
			compare();

#if DC_PLATFORM == DC_PLATFORM_DREAMCAST
			for (const FlashSettings& now : flash_settings)
			{
				FlashSettings before = SetFlashSettings(now);

				compare();

				for (u32 base : { 0x1A000u, 0x1A0A0u })
				{
					const u8 want[3] = {
						(u8)(now.region <= 2 ? '0' + now.region : data[base + 2]),
						(u8)(now.language <= 5 ? '0' + now.language : data[base + 3]),
						(u8)(now.broadcast <= 3 ? '0' + now.broadcast : data[base + 4]),
					};

					for (u32 i = 0; i < 3; i++)
					{
						byte = 0xEE;
						area0_DebugNvmemByte(base + 2 + i, &byte);
						substituted.Add(byte == want[i], "flash byte %05X with region %u, language %u, broadcast %u: %02X, has to be %02X",
							base + 2 + i, now.region, now.language, now.broadcast, byte, want[i]);
					}
				}

				SetFlashSettings(before);
			}

			substituted.Report("the six flash bytes that deliver the region, the language and the broadcast, by their settings");
#endif

			bytes.Report("flash bytes against guest reads");
		}
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

		// where the chip delivers settings in place of the stored bytes, memory is what the guest reads
		{
			MMIODevice* chip = sh4_cpu->GetA0Handler(A0H_FLASH);
			Tally guest;

			auto compare = [&](u32 addr) {
				n = mem::Read(Target::Sh4, addr, 0x100, buffer, nullptr);

				guest.Add(n == 0x100, "%08X: %u of 256 bytes readable", addr, n);
				for (u32 i = 0; i < 0x100; i++)
					guest.Add(buffer[i] == chip->Read(0x1A000 + i, 1), "%08X: %02X, a guest read delivers %02X", addr + i, buffer[i], chip->Read(0x1A000 + i, 1));
			};

			compare(0xA021A000);

#if DC_PLATFORM == DC_PLATFORM_DREAMCAST
			for (const FlashSettings& now : flash_settings)
			{
				FlashSettings before = SetFlashSettings(now);

				compare(0xA021A000);
				compare(0x0221A000);

				if (now.region <= 2 && now.language <= 5 && now.broadcast <= 3)
				{
					n = mem::Read(Target::Sh4, 0x8021A0A2, 3, buffer, nullptr);
					guest.Add(n == 3 && buffer[0] == '0' + now.region && buffer[1] == '0' + now.language && buffer[2] == '0' + now.broadcast,
						"0x8021A0A2 with region %u, language %u, broadcast %u: %02X %02X %02X", now.region, now.language, now.broadcast, buffer[0], buffer[1], buffer[2]);
				}

				SetFlashSettings(before);
			}
#endif

			guest.Report("the flash memory around its settings against guest reads");
		}

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

		Check(n_sh4 == 0x8000 && sh4_view == file && DeviceState(aica) == before, "the AICA register file through memory for the SH4, and the AICA stands");

		// The ARM7 reads the file as the SH4 does, but for four bytes: it has no ARMRST and VREG at 0x2C00, where it
		// reads the two bytes of the file behind them, and at 0x2D00 and 0x2D04 it reads its interrupt controller.
		// What these four are is the business of regs.mem.arm7
		Tally others;

		for (u32 offset = 0; offset < 0x8000; offset++)
		{
			if (offset != 0x2C00 && offset != 0x2C01 && offset != 0x2D00 && offset != 0x2D04)
				others.Add(arm_view[offset] == file[offset], "register file byte %04X: %02X for the ARM7, %02X for the SH4", offset, arm_view[offset], file[offset]);
		}

		others.Report("the AICA register file through memory for the ARM7, but for the four bytes the SH4 reads otherwise");
		Check(n_arm == 0x8000 && DeviceState(aica) == before, "every byte of it is readable for the ARM7, %u of 32768, and the AICA stands", n_arm);

		memset(bits, 0xEE, sizeof(bits));
		u32 n_window = mem::Read(Target::Arm7, 0x00802BFE, 8, buffer, bits);

		Check(n_window == 8 && bits[0] == 0xFF && memcmp(buffer, &arm_view[0x2BFE], 8) == 0, "the ARM7 at 0x00802BFE: %u of 8 bytes readable, bits %02X", n_window, bits[0]);

		n_window = mem::Read(Target::Sh4, 0x00702BFE, 8, buffer, bits);
		Check(n_window == 8 && bits[0] == 0xFF && memcmp(buffer, &file[0x2BFE], 8) == 0, "the SH4 at 0x00702BFE: %u of 8 bytes readable", n_window);

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

	// Every on-chip register, at its P4 address and at its address in area 7: as many bytes as its width has, and
	// no byte of a row without a reader. The rows are those of a walk over every row of the tree; the memory view
	// finds them by a walk of its own, over the nodes of the modules alone, and a module it does not walk reads as
	// nothing. As the machine stands the bytes are what the row's reader gives, which behind a reset is 0 for
	// most rows; then every slot that holds the value of a row is given a value of its own, and the bytes are that
	// value: a view that takes the slot of another register shows
	{
		const std::vector<u8> before = OnChipState();
		std::vector<std::pair<RegisterStruct*, u32>> kept;
		u32 count;
		const regs::Node* nodes = regs::Nodes(&count);

		// different addresses, different values, and different low bytes for neighbours: the factor is odd
		auto own = [](u32 p4) { return ((p4 >> 2) + 1) * 0x9E3779B1u + 0x7F4A7C15u; };

		for (u32 pass = 0; pass < 2; pass++)
		{
			Tally view, given;
			u32 rows = 0, with_rows = 0, no_reader = 0;

			if (pass != 0)
			{
				for (u32 i = 0; i < count; i++)
				{
					for (u32 r = 0; r < nodes[i].registerCount; r++)
					{
						const regs::Register& reg = nodes[i].registers[r];
						RegisterStruct* slot = MmrDataSlot(reg);

						if (slot == nullptr)
							continue;

						kept.push_back({ slot, slot->data32 });
						slot->data32 = own(reg.addr);
					}
				}
			}

			for (u32 i = 0; i < count; i++)
			{
				u32 of_node = 0;

				for (u32 r = 0; r < nodes[i].registerCount; r++)
				{
					const regs::Register& reg = nodes[i].registers[r];

					if (regs::KeySource(reg.key) != regs::kSrcSh4Mmr)
						continue;

					of_node++;

					const bool reader = !(reg.flags & regs::kRegNoReader);
					const u32 want = reader ? (reg.width + 7) / 8 : 0;
					u64 value = 0;
					const bool reads = regs::Read(nodes[i], r, &value, nullptr, 0);

					no_reader += !reader;
					view.Add(reads == reader, "%s.%s: Read() gives %d, the row has %s reader", nodes[i].path, reg.name, reads, reader ? "a" : "no");

					if (pass != 0 && regs::KeySelector(reg.key) == regs::kMmrData)
					{
						const u64 mine = own(reg.addr) & WidthMask(reg.width);

						given.Add(MmrDataSlot(reg) != nullptr && reads && value == mine, "%s.%s: %llX, its slot was given %08X", nodes[i].path, reg.name,
							(unsigned long long)value, own(reg.addr));
						value = mine;
					}

					for (u32 addr : { reg.addr, reg.addr & 0x1FFFFFFF })
					{
						u8 bytes[4] = { 0xEE, 0xEE, 0xEE, 0xEE };
						u8 readable = 0xEE;
						const u32 n = mem::Read(Target::Sh4, addr, 4, bytes, &readable);
						const u32 word = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((u32)bytes[3] << 24);

						view.Add(n == want && readable == (1u << want) - 1 && word == (reader ? value : 0),
							"%s.%s at %08X: %u of 4 bytes readable (bits %02X) holding %08X, expected %u holding %08llX", nodes[i].path, reg.name, addr, n, readable, word,
							want, (unsigned long long)(reader ? value : 0));
					}
				}

				rows += of_node;
				with_rows += of_node != 0;
			}

			if (pass == 0)
			{
				view.Report("every on-chip register through memory, at its P4 address and in area 7: the bytes of its width holding what its reader gives, none without a reader");
				Check(with_rows == Modules().size() && no_reader != 0 && no_reader < rows, "%u on-chip registers on %u nodes, the %zu modules; %u of them have no reader", rows,
					with_rows, Modules().size(), no_reader);
			}
			else
			{
				view.Report("the same with a value of its own in the slot of every register that is plain data");
				given.Report("the row of each of these reads the value its own slot was given");
			}
		}

		for (const auto& slot : kept)
			slot.first->data32 = slot.second;

		Check(!kept.empty() && OnChipState() == before, "the %zu slots hold what they held, and the on-chip modules are as they were", kept.size());
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

		// what is no memory of a kind that is written is refused (the session "writes" has the writer)
		Check(mem::Write(Target::Sh4, 0x00000000, 1, buffer) == kErrNotWritable && mem::Write(Target::Sh4, 0xFFFFFFFF, 2, buffer) == kErrNotWritable
			&& mem::Write(Target::Dsp, 0x8C010000, 1, buffer) == kErrNotWritable, "Write() refuses the boot ROM, a range past the end of the address space and the DSP: -32013");
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
	regs.mem.arm7: the register window of the AICA for the ARM7 target against loads by the ARM7 itself, at the
	moment the test runs and with bytes in the register file that the two processors read differently: the two
	behind ARMRST and VREG, and the ones where the ARM7 has its interrupt controller.
*/
DBG_SELFTEST(regs_mem_arm7, "regs.mem.arm7")
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	std::vector<u8> machine;
	bool quiescent = WaitQuiescent(&machine);

	if (!Check(quiescent, "the machine stands still: two save states of %zu bytes are equal", machine.size()))
		return;

	const std::vector<u8> cpu = DeviceState(scpu);
	const std::vector<u8> device = DeviceState(aica);

	if (!Check(cpu.size() == scpu_state_size, "the save state of the sound CPU has %zu bytes", cpu.size()))
		return;

	// The view of [0x00800000 + offset, + count) and then the loads of the same bytes. The view is taken first:
	// it has to leave the two devices alone, and a load of L is the acknowledge of a request
	auto compare = [&](const char* when, u32 offset, u32 count, std::vector<u8>* view) {
		const std::vector<u8> cpu_now = DeviceState(scpu);
		const std::vector<u8> device_now = DeviceState(aica);
		std::vector<u8> loads, bits((count + 7) / 8, 0);

		view->assign(count, 0xEE);

		u32 n = mem::Read(Target::Arm7, 0x00800000 + offset, count, view->data(), bits.data());
		bool stands = DeviceState(scpu) == cpu_now && DeviceState(aica) == device_now;
		bool loaded = Arm7Loads(0x00800000 + offset, count, &loads);
		Tally bytes;

		for (u32 i = 0; loaded && i < count; i++)
		{
			bool bit = (bits[i >> 3] >> (i & 7)) & 1;

			bytes.Add(bit && (*view)[i] == loads[i], "0x%08X: %s, %02X for the debugger, the ARM7 loads %02X", 0x00800000 + offset + i,
				bit ? "readable" : "not readable", (*view)[i], loads[i]);
		}

		Check(loaded, "%s: the ARM7 loaded %u bytes from 0x%08X, and the machine is as it was in front of that", when, count, 0x00800000 + offset);
		Check(n == count && stands, "%s: %u of %u bytes are readable for the debugger, and reading them changed neither the sound CPU nor the AICA", when, n, count);

		bytes.Report(when);
	};

	std::vector<u8> view;
	u32 level, service;

	memcpy(&level, &cpu[scpu_level_at], 4);
	memcpy(&service, &cpu[scpu_service_at], 4);
	Note("the ARM7 is %s, mode %02X; L is %u, a request is %s, nFIQ is %s", cpu[scpu_tail_at + 6] ? "running" : "held in reset", cpu[scpu_tail_at + 2] & 0x1F,
		level, service ? "in service" : "not in service", cpu[scpu_out_at] ? "pulled" : "released");

	// the whole window as the machine stands
	compare("the register window for the ARM7 against its own loads", 0, 0x8000, &view);

	// ARMRST and VREG are registers of the SH4. The ARM7 has the two bytes of the register file behind them
	// there, which only its own writes change (AICA::WriteReg is what such a write calls)
	{
		u8 sh4_before[2] = { }, sh4_after[2] = { };

		aica->DebugReadRegs(0x2C00, 2, sh4_before);
		aica->WriteReg(0x2C00, 0x5A, 1);
		aica->WriteReg(0x2C01, 0xA5, 1);
		aica->DebugReadRegs(0x2C00, 2, sh4_after);

		compare("0x00802BFE..0x00802C05 behind writes of 5A and A5 to 0x00802C00 and 0x00802C01 by the ARM7", 0x2BFE, 8, &view);
		Check(view[2] == 0x5A && view[3] == 0xA5, "the debugger shows the ARM7 %02X %02X there", view[2], view[3]);
		Check(memcmp(sh4_before, sh4_after, 2) == 0, "ARMRST and VREG are %02X %02X for the SH4, as in front of the writes (%02X %02X)", sh4_after[0], sh4_after[1],
			sh4_before[0], sh4_before[1]);

		std::vector<u8> state = device;
		RestoreDevice(aica, state);
	}

	// L and M are registers of the interrupt controller in front of nFIQ, which the sound CPU keeps. The register
	// file has bytes at 0x2D00 and 0x2D04 all the same, and a write by the SH4 changes them: the ARM7 reads L and,
	// for M, 0 whatever they are. With 71..78 in the file from 0x2D00 on, as the controller stands and in states
	// of it that are made up: a request latched and nFIQ pulled, one that the ARM7 has acknowledged, one whose
	// service has ended with nFIQ still pulled, and nothing at all
	{
		u8 file[8] = { };

		for (u32 i = 0; i < 8; i++)
			aica->Write(0x00702D00 + i, 0x71 + i, 1);

		aica->DebugReadRegs(0x2D00, 8, file);
		Check(memcmp(file, "\x71\x72\x73\x74\x75\x76\x77\x78", 8) == 0, "the SH4 wrote 71..78 to 0x00702D00..0x00702D07 and reads %02X %02X %02X %02X %02X %02X %02X %02X there",
			file[0], file[1], file[2], file[3], file[4], file[5], file[6], file[7]);

		auto controller = [&](const char* when, u32 want_level) {
			compare(when, 0x2D00, 8, &view);
			Check(view[0] == (u8)want_level && view[4] == 0 && memcmp(&view[1], &file[1], 3) == 0 && memcmp(&view[5], &file[5], 3) == 0,
				"%s: the debugger shows the ARM7 %02X for L (%X) and %02X for M, with %02X and %02X in the register file there", when, view[0], want_level,
				view[4], file[0], file[4]);
		};

		controller("0x00802D00..0x00802D07 with 71..78 in the register file", level);

		const struct { u32 level, service; bool out; } made_up[] = { { 5, 1, true }, { 7, 1, false }, { 1, 0, true }, { 0, 0, false } };

		for (const auto& c : made_up)
		{
			std::vector<u8> made = cpu;
			char when[120];

			made[scpu_out_at] = c.out;
			memcpy(&made[scpu_level_at], &c.level, 4);
			memcpy(&made[scpu_service_at], &c.service, 4);
			RestoreDevice(scpu, made);

			snprintf(when, sizeof(when), "the same with L %u, %s and nFIQ %s", c.level, c.service ? "a request in service" : "no request in service",
				c.out ? "pulled" : "released");
			controller(when, c.level);
		}

		std::vector<u8> state = cpu;
		RestoreDevice(scpu, state);
		state = device;
		RestoreDevice(aica, state);
	}

	Check(DeviceState(scpu) == cpu && DeviceState(aica) == device, "the sound CPU and the AICA are as they were");

	std::vector<u8> after;

	if (Check(MachineState(&after), "a second save state"))
	{
		size_t at = FirstDifference(machine, after);

		if (machine == after)
			Check(true, "the save state of the machine is the same %zu bytes", machine.size());
		else
			Check(false, "the save state of the machine differs at byte %zu of %zu (%02X, then %02X)", at, machine.size(), at < machine.size() ? machine[at] : 0,
				at < after.size() ? after[at] : 0);
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

/*
	The rows of milestone M2, at states the tests make up.
*/
namespace {

// A 32-bit value of its own for every index, and a 16-bit one: an odd factor maps different indices to
// different values
u32 Word32(u32 salt, u32 index)
{
	return (index + 1) * 0x9E3779B1u + salt * 0x7F4A7C15u;
}

u32 Word16(u32 salt, u32 index)
{
	return ((index + 1) * 0x9E37u + salt * 0x6A09u) & 0xFFFF;
}

// xorshift, for the bytes of a made-up snapshot
u32 NextRandom(u32* state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return *state;
}

// The devices of the machine, with what each writes into a save state
struct Devices {
	std::vector<MMIODevice*> devices;
	std::vector<std::vector<u8>> states;
	std::vector<u8> pvr;		// the PVR's register file, which no device has in its state
};

Devices AllDevices()
{
	Devices all;

	for (u32 slot = 0; slot < A0H_MAX; slot++)
	{
		MMIODevice* device = sh4_cpu->GetA0Handler((Area0Hanlders)slot);

		if (device == nullptr)
			continue;

		all.devices.push_back(device);
		all.states.push_back(DeviceState(device));
	}

	all.pvr.assign(pvr_regs, pvr_regs + pvr_RegSize);
	return all;
}

// The first device whose state is not the one of `before`, as its index, and -1 when every one is; -2 for the
// PVR's register file
int FirstChanged(const Devices& before)
{
	for (size_t i = 0; i < before.devices.size(); i++)
	{
		if (DeviceState(before.devices[i]) != before.states[i])
			return (int)i;
	}

	return memcmp(pvr_regs, before.pvr.data(), pvr_RegSize) == 0 ? -1 : -2;
}

// The value of a row of the AICA's register file in an image of the file, worked out bit by bit from the two
// forms of a row as dbg_regs_private.h words them
u64 FieldOf(const regs::Register& reg, const u8* file)
{
	const u32 selector = regs::KeySelector(reg.key);
	const u32 offset = regs::AicaFieldOffset(selector), shift = regs::AicaFieldShift(selector);
	u64 value = 0;

	auto bit = [file](u32 word_at, u32 n) { return (u64)((file[word_at + n / 8] >> (n % 8)) & 1); };

	if (!regs::AicaFieldTwoWords(selector))
	{
		for (u32 b = 0; b < reg.width; b++)
			value |= bit(offset, shift + b) << b;
	}
	else
	{
		for (u32 b = 16; b < reg.width; b++)
			value |= bit(offset, shift + b - 16) << b;
		for (u32 b = regs::AicaFieldLowShift(selector); b < 16; b++)
			value |= bit(offset + 4, b) << b;
	}

	return value;
}

// The save state of the AICA: three timers of two words, VREG, ARMRST, the 0x8000 bytes of the register file,
// the sound generator
const size_t aica_vreg_at = 3 * 8;
const size_t aica_armrst_at = aica_vreg_at + 4;
const size_t aica_file_at = aica_armrst_at + 4;

// The words of the register file that a row reads and that the accessor delivers as the file has them: the 18
// of every channel, and the common block without the three status words, the three level registers and the
// word of ARMRST and VREG
bool StoredWord(u32 offset)
{
	if (offset < 0x2000)
		return (offset & 0x7F) < 0x48;

	if (offset == 0x2808 || offset == 0x2810 || offset == 0x2814 || offset == 0x2C00)
		return false;
	if (offset >= SCILV0_addr && offset < SCILV2_addr + 4)
		return false;

	return (offset >= 0x2800 && offset <= 0x2814) || (offset >= 0x2880 && offset <= 0x28BC);
}

/*
	A made-up state of the AICA: every word that a row reads has a value of its own, the two words behind the
	common block where the file has what is not the ARM7's interrupt controller hold values that controller never
	has, ARMRST and VREG are other than the word of the file at 0x2C00 says, and the channel that MSLC selects
	has passed its loop end.

	  0x2808   MIEMP and MOEMP are clear in the file: a read has them set
	  0x280C   AFSET is clear: the guest's read of the status verifies that
	  0x2810   LP is clear in the file: the status is worked out from the channel, whose loop flag is set

	The loop flag is a byte of the save state behind the two loop points of its channel, which are found by
	writing two values of their own to LSA and LEA, as the guest does; the device is put back behind that.
*/
struct AicaMade {
	std::vector<u8> state;		// what RestoreDevice() takes
	const u8* file = nullptr;	// the register file in it
	u32 armrst = 0, vreg = 0;
	u32 selected = 0;			// the channel MSLC selects
	size_t loop_flag_at = 0;
};

bool MakeAicaState(AICA* aica, const std::vector<u8>& real, u32 salt, AicaMade* made)
{
	if (real.size() < aica_file_at + 0x8000)
		return false;

	made->state = real;

	u8* file = &made->state[aica_file_at];

	auto set = [file](u32 offset, u32 word) {
		file[offset] = (u8)word;
		file[offset + 1] = (u8)(word >> 8);
	};

	for (u32 offset = 0; offset < 0x2D08; offset += 4)
	{
		if (StoredWord(offset) || offset == 0x2808 || offset == 0x2810 || offset == 0x2814 || (offset >= SCILV0_addr && offset < SCILV2_addr + 4)
			|| offset == 0x2C00 || offset == 0x2D00 || offset == 0x2D04)
			set(offset, Word16(salt, offset / 4));
	}

	set(0x2808, Word16(salt, 0x2808 / 4) & ~((1u << 8) | (1u << 11)));
	set(0x280C, Word16(salt, 0x280C / 4) & ~(1u << 14));
	set(0x2810, Word16(salt, 0x2810 / 4) & 0x7FFF);

	// the level registers hold something, whatever the accessor says of them
	for (u32 offset = SCILV0_addr; offset < SCILV2_addr + 4; offset += 4)
		set(offset, Word16(salt, offset / 4) | 0x11);

	// what the file has where the ARM7 reads its interrupt controller: no level and no flag of that controller
	set(0x2D00, 0xC3A6);
	set(0x2D04, 0x3C5B);

	made->selected = (file[0x280D] & 0x3F);

	// ARMRST and VREG, which the SH4 reads at 0x2C00, are not the word of the file there
	made->armrst = (file[0x2C00] & 1) ^ 1;
	made->vreg = ((file[0x2C01] & 3) + 1) & 3;
	memcpy(&made->state[aica_vreg_at], &made->vreg, 4);
	memcpy(&made->state[aica_armrst_at], &made->armrst, 4);

	// where the loop flag of the selected channel is
	const u32 loop_points[2] = { 0xA55A, 0x5AA5 };

	aica->WriteReg(made->selected * 0x80 + 0x08, loop_points[0], 2);
	aica->WriteReg(made->selected * 0x80 + 0x0C, loop_points[1], 2);

	std::vector<u8> probe = DeviceState(aica);
	u32 found = 0;

	for (size_t i = aica_file_at + 0x8000; i + sizeof(loop_points) < probe.size(); i++)
	{
		if (memcmp(&probe[i], loop_points, sizeof(loop_points)) == 0)
		{
			made->loop_flag_at = i + sizeof(loop_points);
			found++;
		}
	}

	std::vector<u8> back = real;

	RestoreDevice(aica, back);

	if (found != 1 || DeviceState(aica) != real)
		return false;

	made->state[made->loop_flag_at] = 1;
	made->file = &made->state[aica_file_at];
	return true;
}

// Every field of the register file on the nodes of the tree: the rows of dc.aica.common and of the 64 channels
template <typename Fn>
void ForEachAicaRow(Fn fn)
{
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);

	for (u32 i = 0; i < count; i++)
	{
		for (u32 r = 0; r < nodes[i].registerCount; r++)
		{
			if (regs::KeySource(nodes[i].registers[r].key) == regs::kSrcAica)
				fn(nodes[i], r);
		}
	}
}

/*
	A made-up snapshot of the GD-ROM drive: every byte of the device's save state is 0 or 1, by a generator. (Not
	any byte: the state has flags the device keeps as bool.) No member of the snapshot then has a value another
	member cannot have, so one state tells little; the tests use many, and two rows that are equal in every one
	of them would be two rows of one member.

	But for two members, which the device keeps in an enum: the state of its command state machine (gd_states) and
	the type of the disc (DiscType). A word of bytes that are 0 or 1 is no value of either, and the snapshot loads
	them as what they are; they get enumerators of their own, by the seed. They are the two words in front of the
	nine members that the device's save state ends with, which are of the types of gdromv3.h; where the drive has
	them is held by the snapshot, which has to read what was put there (GdromEnumsAt, GdromEnumsMade)
*/
const gd_states kGdStates[] = { gds_waitcmd, gds_procata, gds_waitpacket, gds_procpacket, gds_pio_send_data, gds_pio_get_data, gds_pio_end, gds_procpacketdone,
	gds_readsector_pio, gds_readsector_dma, gds_process_set_mode };
const DiscType kDiscTypes[] = { CdDA, CdRom, CdRom_XA, CdRom_Extra, CdRom_CDI, GdRom, NoDisk, Open, Busy };

// Where the state and the disc type are in the save state of the drive: behind them data_write_mode and DriveSel
// (u32 each), then Error, IntReason, Features, SecCount, SecNumber, GDStatus and ByteCount. False when the state is
// too short for that
bool GdromEnumsAt(size_t size, size_t* state_at, size_t* disc_at)
{
	const size_t tail = 4 + 4 + sizeof(GD_ErrRegT) + sizeof(GD_InterruptReasonT) + sizeof(GD_FeaturesT) + sizeof(GD_SecCountT) + sizeof(GD_SecNumbT)
		+ sizeof(GD_StatusT) + sizeof(ByteCount_t);

	if (size < tail + sizeof(DiscType) + sizeof(gd_states))
		return false;

	*disc_at = size - tail - sizeof(DiscType);
	*state_at = *disc_at - sizeof(gd_states);
	return true;
}

// The enumerators that the made-up state of a seed has
void GdromEnumsMade(u32 seed, gd_states* state, DiscType* disc)
{
	*state = kGdStates[seed % (sizeof(kGdStates) / sizeof(kGdStates[0]))];
	*disc = kDiscTypes[(seed * 5 + 3) % (sizeof(kDiscTypes) / sizeof(kDiscTypes[0]))];
}

std::vector<u8> MakeGdromState(const std::vector<u8>& real, u32 seed)
{
	std::vector<u8> made(real.size());
	u32 state = seed * 0x9E3779B1u + 0x1234567u;

	for (u8& byte : made)
		byte = (NextRandom(&state) >> 16) & 1;

	size_t state_at, disc_at;
	gd_states gd_state;
	DiscType disc;

	if (GdromEnumsAt(made.size(), &state_at, &disc_at))
	{
		GdromEnumsMade(seed, &gd_state, &disc);
		memcpy(&made[state_at], &gd_state, sizeof(gd_state));
		memcpy(&made[disc_at], &disc, sizeof(disc));
	}

	return made;
}

// A row of the drive's node as the snapshot has its member, by the name of the row
bool GdromMember(const GDRomDebugState& g, const char* name, u64* value)
{
	auto packet = [&g](u32 first) { return (u64)g.packet[first] * 0x1000000u + g.packet[first + 1] * 0x10000u + g.packet[first + 2] * 0x100u + g.packet[first + 3]; };

	const struct { const char* name; u64 value; } members[] = {
		{ "STATE", g.state }, { "DISC_TYPE", g.discType }, { "STATUS", g.status }, { "ERROR", g.error }, { "INTERRUPT_REASON", g.interruptReason },
		{ "FEATURES", g.features }, { "SECTOR_COUNT", g.sectorCount }, { "SECTOR_NUMBER", g.sectorNumber }, { "DRIVE_SELECT", g.driveSelect },
		{ "ATA_COMMAND", g.ataCommand }, { "BYTE_COUNT", g.byteCount }, { "PACKET_0_3", packet(0) }, { "PACKET_4_7", packet(4) }, { "PACKET_8_11", packet(8) },
		{ "SENSE_KEY", g.senseKey }, { "SENSE_ASC", g.senseAsc }, { "SENSE_ASCQ", g.senseAscq }, { "CDDA_PLAYING", g.cddaPlaying ? 1u : 0u },
		{ "CDDA_REPEATS", g.cddaRepeats }, { "CDDA_CURRENT", g.cddaCurrent }, { "CDDA_START", g.cddaStart }, { "CDDA_END", g.cddaEnd },
		{ "READ_START_SECTOR", g.readStartSector }, { "READ_REMAINING_SECTORS", g.readRemainingSectors }, { "READ_SECTOR_TYPE", g.readSectorType },
		{ "PIO_INDEX", g.pioIndex }, { "PIO_SIZE", g.pioSize },
	};

	for (const auto& member : members)
	{
		if (strcmp(member.name, name) == 0)
		{
			*value = member.value;
			return true;
		}
	}

	return false;
}

// The system bus slots that hold a value the guest reads: registered, readable, not behind a read function
bool DataSlot(const RegisterStruct& slot)
{
	return Registered(slot) && !(slot.flags & (REG_WO | REG_RF));
}

// The registers of the PVR's register file that have a row, as offsets into the file
std::vector<u32> PvrRowOffsets()
{
	std::vector<u32> offsets;
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);

	for (u32 i = 0; i < count; i++)
	{
		for (u32 r = 0; r < nodes[i].registerCount; r++)
		{
			if (regs::KeySource(nodes[i].registers[r].key) == regs::kSrcPvr)
				offsets.push_back(nodes[i].registers[r].addr & pvr_RegMask);
		}
	}

	return offsets;
}

}

/*
	regs.rows.sb: the 115 system bus rows of M2 beside the twelve interrupt registers of dc.holly.intc: 100 on
	slots that hold a value, 14 on write-only slots, and SB_FFST behind a read function. Against a guest read,
	for the 111 rows on slots that hold a value, those 100 and eleven of dc.holly.intc (a guest read of such a
	slot is the value and changes nothing); with a value of its own in every slot, where a row that reads the slot
	of its neighbour shows; and a write-only slot, which reads "n/a" whatever it holds. The two rows behind a
	read function are SB_FFST and SB_ISTNRM of dc.holly.intc: 111 + 14 + 2 = 127 rows of the system bus.
*/
DBG_SELFTEST(regs_rows_sb, "regs.rows.sb")
{
	SystemBus* sb = sh4_cpu->GetA0H<SystemBus>(A0H_SB);
	MMIODevice* sb_device = sh4_cpu->GetA0Handler(A0H_SB);
	std::vector<u8> before = DeviceState(sb_device);
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);

	// the slots as they are, and with a value of its own in each that holds one
	std::vector<u32> kept(sb->sb_regs.size());

	for (size_t i = 0; i < sb->sb_regs.size(); i++)
		kept[i] = sb->sb_regs[i].data32;

	for (u32 pass = 0; pass < 3; pass++)
	{
		Tally guest, values, refused;
		u32 data_rows = 0, write_only = 0;

		if (pass != 0)
		{
			for (u32 i = 0; i < sb->sb_regs.size(); i++)
			{
				RegisterStruct& slot = sb->sb_regs[i];

				// a write-only slot holds what the guest wrote last: its storage is its own as well
				if (DataSlot(slot) || (Registered(slot) && (slot.flags & REG_WO) && !(slot.flags & REG_RO)))
					slot.data32 = Word32(pass, i);
			}
		}

		for (u32 i = 0; i < count; i++)
		{
			for (u32 r = 0; r < nodes[i].registerCount; r++)
			{
				const regs::Register& reg = nodes[i].registers[r];

				if (regs::KeySource(reg.key) != regs::kSrcSb)
					continue;

				const u32 index = (reg.addr - SB_BASE) / 4;
				const RegisterStruct& slot = sb->sb_regs[index];
				u64 value = ~(u64)0;
				const bool ok = regs::Read(nodes[i], r, &value, nullptr, 0);

				if (slot.flags & REG_WO)
				{
					u32 word = 0x5EED;

					write_only++;
					refused.Add(!ok && value == ~(u64)0 && !regs::ReadSbRegisterAt(reg.addr, &word) && word == 0x5EED && (pass == 0 || slot.data32 == Word32(pass, index)),
						"%s.%s: a write-only register that holds %08X reads %llX", nodes[i].path, reg.name, slot.data32, (unsigned long long)value);
					continue;
				}

				if (!DataSlot(slot))
					continue;

				data_rows++;

				// the guest's own view
				const u32 read = sb_device->Read(reg.addr, 4);

				guest.Add(ok && value == read, "%s.%s: %llX, a guest read of %08X gives %X", nodes[i].path, reg.name, (unsigned long long)value, reg.addr, read);

				if (pass != 0)
					values.Add(ok && value == Word32(pass, index), "%s.%s: %llX, its slot was given %08X", nodes[i].path, reg.name, (unsigned long long)value,
						Word32(pass, index));
			}
		}

		if (pass == 0)
		{
			guest.Report("system bus rows on slots that hold a value, against a guest read, as the machine stands");
			refused.Report("system bus rows on write-only slots that read as n/a, as a read by address does");
			Check(data_rows + write_only + 2 == 127, "%u rows on slots that hold a value, %u on write-only slots, and the two behind a read function: 127", data_rows,
				write_only);
		}
		else
		{
			guest.Report("the same with a value of its own in every slot");
			values.Report("each row reads the value its own slot was given");
			refused.Report("a write-only register reads as n/a whatever its slot holds");
		}
	}

	for (size_t i = 0; i < sb->sb_regs.size(); i++)
	{
		if (DataSlot(sb->sb_regs[i]) || (Registered(sb->sb_regs[i]) && (sb->sb_regs[i].flags & REG_WO) && !(sb->sb_regs[i].flags & REG_RO)))
			sb->sb_regs[i].data32 = kept[i];
	}

	Check(DeviceState(sb_device) == before, "the system bus is as it was");

	// a read-only register that the emulator never assigns reads 0, not what the slot was made with
	{
		u64 value = ~(u64)0;
		const bool ok = ReadRegister("dc.maple", "SB_MST", &value);
		const RegisterStruct& slot = sb->sb_regs[(SB_MST_addr - SB_BASE) / 4];

		Check(ok && value == 0 && slot.data32 == 0 && slot.readFunctionAddr == nullptr, "dc.maple.SB_MST, which nothing assigns, reads %llX: its slot holds no address of the host",
			(unsigned long long)value);
	}
}

/*
	regs.rows.pvr: the rows of the PVR's register file against a guest read, which is the file and changes
	nothing; as the machine stands, and with a value of its own in every register that has a row.
*/
DBG_SELFTEST(regs_rows_pvr, "regs.rows.pvr")
{
	MMIODevice* pvr = sh4_cpu->GetA0Handler(A0H_PVR);
	std::vector<u8> machine;

	if (!Check(WaitQuiescent(&machine), "the machine stands still: no frame is being drawn from the register file"))
		return;

	std::vector<u8> kept(pvr_regs, pvr_regs + pvr_RegSize);
	const std::vector<u32> offsets = PvrRowOffsets();
	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);

	for (u32 pass = 0; pass < 3; pass++)
	{
		Tally guest, values;

		if (pass != 0)
		{
			for (u32 offset : offsets)
			{
				const u32 word = Word32(pass + 8, offset / 4);

				memcpy(&pvr_regs[offset], &word, 4);
			}
		}

		for (u32 i = 0; i < count; i++)
		{
			for (u32 r = 0; r < nodes[i].registerCount; r++)
			{
				const regs::Register& reg = nodes[i].registers[r];

				if (regs::KeySource(reg.key) != regs::kSrcPvr)
					continue;

				u64 value = ~(u64)0;
				const bool ok = regs::Read(nodes[i], r, &value, nullptr, 0);
				const u32 read = pvr->Read(reg.addr, 4);

				guest.Add(ok && value == read, "%s.%s: %llX, a guest read of %08X gives %X", nodes[i].path, reg.name, (unsigned long long)value, reg.addr, read);

				if (pass != 0)
					values.Add(ok && value == Word32(pass + 8, (reg.addr & pvr_RegMask) / 4), "%s.%s: %llX, its register was given %08X", nodes[i].path, reg.name,
						(unsigned long long)value, Word32(pass + 8, (reg.addr & pvr_RegMask) / 4));
			}
		}

		if (pass == 0)
			guest.Report("PVR rows against a guest read, as the machine stands");
		else
		{
			guest.Report("PVR rows against a guest read, with a value of its own in every register");
			values.Report("each row reads the value its own register was given");
		}
	}

	memcpy(pvr_regs, kept.data(), pvr_RegSize);

	std::vector<u8> after;

	Check(offsets.size() == 68 && MachineState(&after) && after == machine, "the %zu registers are as they were, and the machine with them", offsets.size());
}

/*
	regs.rows.aica: the fields of the AICA's register file. Every row of dc.aica.common and of the 64 channels
	against the file as the accessor delivers it, and against the word a guest read gives where that read changes
	nothing; in two made-up states, where every word has a value of its own: a row with another shift, another
	width, another word or another channel reads another value. The rows that the accessor works out, the five
	without a reader, and L and M, which are the sound CPU's.
*/
DBG_SELFTEST(regs_rows_aica, "regs.rows.aica")
{
	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	std::vector<u8> real = DeviceState(aica);
	std::vector<u8> cpu = DeviceState(scpu);
	std::vector<u8> delivered(0x8000);

	bool layout = real.size() > aica_file_at + 0x8000 && aica->DebugReadRegs(0, 0x8000, delivered.data());

	// where the accessor delivers the file as it is, the bytes of the save state are those bytes
	for (u32 offset = 0; layout && offset < 0x2D08; offset += 4)
	{
		if (StoredWord(offset))
			layout = memcmp(&real[aica_file_at + offset], &delivered[offset], 2) == 0;
	}

	if (!Check(layout && cpu.size() == scpu_state_size, "the save state of the AICA has the register file at byte %zu, behind the timers, VREG and ARMRST", aica_file_at))
		return;

	// The rows of the common block against the struct the emulator itself reads the block through
	// (CommonData_struct, hw/aica/aica.h): a member set to all ones in a block of zeroes shows the bits the
	// compiler gives it, and those are the bits of the row of its name. The two halves of DMEA are one row
	{
		const regs::Node* common = regs::Find("dc.aica.common");
		Tally layout_of;
		u32 members = 0;

		auto compare = [&](const char* name, const CommonData_struct& ones) {
			std::vector<u8> block(0x8000, 0), mine(0x8000, 0);
			const int index = common != nullptr ? regs::FindRegister(*common, name) : -1;

			members++;
			memcpy(&block[0x2800], &ones, sizeof(ones));

			if (index < 0)
			{
				layout_of.Add(false, "dc.aica.common has no row %s", name);
				return;
			}

			// the bits of the row, by its key
			const regs::Register& reg = common->registers[index];
			const u32 selector = regs::KeySelector(reg.key);
			const u32 offset = regs::AicaFieldOffset(selector), shift = regs::AicaFieldShift(selector);

			auto set = [&mine](u32 word_at, u32 n) { mine[word_at + n / 8] |= (u8)(1 << (n % 8)); };

			if (!regs::AicaFieldTwoWords(selector))
			{
				for (u32 b = 0; b < reg.width; b++)
					set(offset, shift + b);
			}
			else
			{
				for (u32 b = 16; b < reg.width; b++)
					set(offset, shift + b - 16);
				for (u32 b = regs::AicaFieldLowShift(selector); b < 16; b++)
					set(offset + 4, b);
			}

			const size_t at = FirstDifference(block, mine);

			layout_of.Add(block == mine && regs::KeySource(reg.key) == regs::kSrcAica, "dc.aica.common.%s: its bits differ from the member's at byte %04zX of the file (%02X, the struct has %02X)",
				name, at, at < mine.size() ? mine[at] : 0, at < block.size() ? block[at] : 0);
		};

// (a member that is 0 and is counted down by one has every one of its bits set)
#define MEMBER(row, member) { CommonData_struct ones; memset(&ones, 0, sizeof(ones)); ones.member--; compare(row, ones); }
#define MEMBER2(row, high, low) { CommonData_struct ones; memset(&ones, 0, sizeof(ones)); ones.high--; ones.low--; compare(row, ones); }
		MEMBER("MVOL", MVOL) MEMBER("VER", VER) MEMBER("DAC18B", DAC18B) MEMBER("MEM8MB", MEM8MB) MEMBER("MONO", Mono)
		MEMBER("RBP", RBP) MEMBER("RBL", RBL) MEMBER("TESTB0", TESTB0)
		MEMBER("MIBUF", MIBUF) MEMBER("MIEMP", MIEMP) MEMBER("MIFUL", MIFUL) MEMBER("MIOVF", MIOVF) MEMBER("MOEMP", MOEMP) MEMBER("MOFUL", MOFUL)
		MEMBER("MOBUF", MOBUF) MEMBER("MSLC", MSLC) MEMBER("AFSET", AFSET)
		MEMBER("EG", EG) MEMBER("SGC", SGC) MEMBER("LP", LP) MEMBER("CA", CA)
		MEMBER("MRWINH", MRWINH) MEMBER("T", $T) MEMBER("TSCD", $TSCD) MEMBER2("DMEA", DMEA_hi, DMEA_lo)
		MEMBER("DRGA", DRGA) MEMBER("DGATE", DGATE) MEMBER("DEXE", DEXE) MEMBER("DLG", DLG) MEMBER("DDIR", DDIR)
		MEMBER("TIMA", TIMA) MEMBER("TACTL", TACTL) MEMBER("TIMB", TIMB) MEMBER("TBCTL", TBCTL) MEMBER("TIMC", TIMC) MEMBER("TCCTL", TCCTL)
		MEMBER("SCIEB", SCIEB) MEMBER("SCIPD", SCIPD) MEMBER("SCIRE", SCIRE) MEMBER("SCILV0", SCILV0) MEMBER("SCILV1", SCILV1) MEMBER("SCILV2", SCILV2)
		MEMBER("MCIEB", MCIEB) MEMBER("MCIPD", MCIPD) MEMBER("MCIRE", MCIRE)
		MEMBER("AR", AR) MEMBER("VREG", VREG)
#undef MEMBER2
#undef MEMBER

		layout_of.Report("the fields of dc.aica.common against the members of the emulator's struct of the common block");
		Check(common != nullptr && members + 2 == common->registerCount, "every field of the node was compared: %u members, and L and M, which are no members of the file",
			members);
	}

	u32 rows = 0, no_reader = 0;

	ForEachAicaRow([&](const regs::Node& node, u32 r) {
		rows++;
		no_reader += (node.registers[r].flags & regs::kRegNoReader) != 0;
	});

	Check(rows == 47 + 64 * 38 && no_reader == 5, "%u fields of the register file are rows, %u of them without a reader (expected 47 and 38 for each channel, and 5)", rows,
		no_reader);

	for (u32 salt = 1; salt <= 2; salt++)
	{
		AicaMade made;
		const bool is_made = MakeAicaState(aica, real, salt, &made);

		if (!Check(is_made, "made-up state %u: the loop flag of channel %u is found in the save state, once", salt, made.selected))
			break;

		std::vector<u8> state = made.state;

		RestoreDevice(aica, state);
		aica->DebugReadRegs(0, 0x8000, delivered.data());

		// the accessor: the stored words as the state has them, and the others as the device has them
		{
			bool stored = true;

			for (u32 offset = 0; offset < 0x2D08; offset += 4)
			{
				if (StoredWord(offset))
					stored = stored && memcmp(&delivered[offset], &made.file[offset], 2) == 0;
			}

			AicaChannelDebug channel;
			const u32 status = delivered[0x2810] | (delivered[0x2811] << 8);
			const u32 in_file = made.file[0x2810] | (made.file[0x2811] << 8);

			Check(stored && aica->DebugGetChannel(made.selected, &channel) && channel.looped && (status & 0x8000) != 0 && (in_file & 0x8000) == 0,
				"made-up state %u is in the device: its words read back, and channel %u, which MSLC selects, has passed its loop end (status %04X, the file has %04X)", salt,
				made.selected, status, in_file);
		}

		Tally fields, guest, refused;
		std::vector<u32> unlike_next(64, 0);

		ForEachAicaRow([&](const regs::Node& node, u32 r) {
			const regs::Register& reg = node.registers[r];
			const u32 offset = regs::AicaFieldOffset(regs::KeySelector(reg.key));
			u64 value = ~(u64)0;
			const bool ok = regs::Read(node, r, &value, nullptr, 0);

			if (reg.flags & regs::kRegNoReader)
			{
				// what the file holds there, and what the accessor delivers for it
				const u64 held = FieldOf(reg, made.file), given = FieldOf(reg, delivered.data());

				refused.Add(!ok && value == ~(u64)0, "%s.%s: reads %llX, the file holds %llX and the accessor delivers %llX", node.path, reg.name, (unsigned long long)value,
					(unsigned long long)held, (unsigned long long)given);
				return;
			}

			// as the accessor delivers the file
			const u64 want = FieldOf(reg, delivered.data());

			fields.Add(ok && value == want, "%s.%s: %llX, the register file has %llX in those bits", node.path, reg.name, (unsigned long long)value, (unsigned long long)want);

			// a stored word is the made-up one: the value does not depend on the accessor
			const bool two = regs::AicaFieldTwoWords(regs::KeySelector(reg.key));

			if (StoredWord(offset) && (!two || StoredWord(offset + 4)))
			{
				fields.Add(ok && value == FieldOf(reg, made.file), "%s.%s: %llX, the made-up state has %llX in those bits", node.path, reg.name, (unsigned long long)value,
					(unsigned long long)FieldOf(reg, made.file));

				// the guest's own view of a channel: a read of its words is the file and changes nothing
				if (offset < 0x2000)
				{
					u8 words[8] = { };

					for (u32 w = 0; w <= (two ? 4u : 0u); w += 4)
					{
						const u32 word = aica->ReadReg(offset + w, 2);

						words[w] = (u8)word;
						words[w + 1] = (u8)(word >> 8);
					}

					regs::Register at_zero = reg;

					at_zero.key = regs::MakeKey(regs::kSrcAica, regs::KeySelector(reg.key) & ~0x1FFFu);
					guest.Add(ok && value == FieldOf(at_zero, words), "%s.%s: %llX, guest reads of its words give %llX", node.path, reg.name, (unsigned long long)value,
						(unsigned long long)FieldOf(at_zero, words));

					// the same field of the channel behind it: another value, but where few bits happen to agree
					if (offset + 0x80 < 0x2000)
					{
						regs::Register next = reg;

						next.key = regs::MakeKey(regs::kSrcAica, regs::KeySelector(reg.key) + 0x80 / 4);
						unlike_next[offset / 0x80] += FieldOf(next, made.file) != value;
					}
				}
			}
		});

		fields.Report("fields of the register file in a made-up state: as the accessor delivers them, and as the state was made");
		guest.Report("fields of the channels against guest reads of their words");
		refused.Report("the five fields without a reader read as n/a whatever the file holds");
		{
			u32 fewest = 38;

			for (u32 channel = 0; channel < 63; channel++)
				fewest = unlike_next[channel] < fewest ? unlike_next[channel] : fewest;

			Check(fewest >= 10, "every channel has at least %u of its 38 fields unlike the same fields of the channel behind it: a row that reads another channel shows",
				fewest);
		}

		// what the accessor works out, by name
		{
			u64 ar = 9, vreg = 9, miemp = 9, moemp = 9, lp = 9, mslc = 99;
			const bool ok = ReadRegister("dc.aica.common", "AR", &ar) && ReadRegister("dc.aica.common", "VREG", &vreg) && ReadRegister("dc.aica.common", "MIEMP", &miemp)
				&& ReadRegister("dc.aica.common", "MOEMP", &moemp) && ReadRegister("dc.aica.common", "LP", &lp) && ReadRegister("dc.aica.common", "MSLC", &mslc);

			Check(ok && ar == made.armrst && vreg == made.vreg && (made.file[0x2C00] & 1u) != made.armrst && (made.file[0x2C01] & 3u) != made.vreg,
				"AR is %llX and VREG is %llX: the reset of the ARM7 and the video register as the device has them (%X, %X), not the word of the file at 0x2C00 (%02X%02X)",
				(unsigned long long)ar, (unsigned long long)vreg, made.armrst, made.vreg, made.file[0x2C01], made.file[0x2C00]);
			Check(ok && miemp == 1 && moemp == 1 && lp == 1 && mslc == made.selected,
				"MIEMP and MOEMP read 1 and LP reads 1 for channel %llu, with all three clear in the file: as a read delivers them", (unsigned long long)mslc);
		}

		// The level registers: the file holds a value, the accessor delivers 0, and the rows say that they do not
		// know. SCIRE and MCIRE store nothing a guest could read back
		{
			u64 value = 0;
			u32 held = 0, given = 0, read = 0;

			for (u32 offset = SCILV0_addr; offset < SCILV2_addr + 4; offset += 4)
			{
				held += made.file[offset] != 0;
				given += delivered[offset] != 0;
			}

			for (const char* name : { "SCILV0", "SCILV1", "SCILV2", "SCIRE", "MCIRE" })
				read += ReadRegister("dc.aica.common", name, &value);

			Check(held == 3 && given == 0 && read == 0, "SCILV0, SCILV1 and SCILV2 hold a value in the file (%u of 3), the accessor delivers 0 for them (%u are not 0), and the "
				"rows read as n/a, with SCIRE and MCIRE (%u of the five read)", held, given, read);
		}

		// L and M are the interrupt controller of the sound CPU, in each of four states of it. The words of the file
		// at 0x2D00 and 0x2D04 hold other values
		{
			const struct { u32 level, service; bool out; } controller[] = { { 5, 1, true }, { 7, 1, false }, { 1, 0, true }, { 0, 0, false } };
			Tally interrupts;

			for (const auto& c : controller)
			{
				std::vector<u8> made_cpu = cpu;

				made_cpu[scpu_out_at] = c.out;
				memcpy(&made_cpu[scpu_level_at], &c.level, 4);
				memcpy(&made_cpu[scpu_service_at], &c.service, 4);
				RestoreDevice(scpu, made_cpu);

				u64 level = 99, service = 99;
				u32 want_level = 98, want_service = 98;
				const bool ok = ReadRegister("dc.aica.common", "L", &level) && ReadRegister("dc.aica.common", "M", &service)
					&& scpu->DebugGetInterrupt(&want_level, &want_service);
				const u32 file_level = delivered[0x2D00], file_service = delivered[0x2D04];

				interrupts.Add(ok && level == c.level && service == c.service && want_level == c.level && want_service == c.service,
					"L %llX and M %llX, the sound CPU has %X and %X", (unsigned long long)level, (unsigned long long)service, c.level, c.service);
				interrupts.Add(file_level != c.level && file_service != c.service, "the file has %02X and %02X at 0x2D00 and 0x2D04, which the test meant to differ from %X and %X",
					file_level, file_service, c.level, c.service);
			}

			RestoreDevice(scpu, cpu);
			interrupts.Report("L and M of dc.aica.common are what SoundCPU::DebugGetInterrupt says, in four states of the controller, and not the words of the file");
		}

		Check(DeviceState(aica) == made.state, "the reads left made-up state %u as it was: no status word was rewritten, the loop flag stands", salt);
	}

	RestoreDevice(aica, real);
	Check(DeviceState(aica) == real && DeviceState(scpu) == cpu, "the AICA and the sound CPU are as they were");
}

/*
	regs.rows.gdrom: the rows of dc.gdrom are the members of the drive's snapshot, and the two rows of dc.aica.rtc
	are the clock. As the machine stands, and for the drive in 24 made-up states.
*/
DBG_SELFTEST(regs_rows_gdrom, "regs.rows.gdrom")
{
	// the clock
	{
		MMIODevice* rtc = sh4_cpu->GetA0Handler(A0H_RTC);
		std::vector<u8> before = DeviceState(rtc);
		u32 seconds = 0;
		bool write_enable = true;
		u64 row_seconds = 1, row_enable = 9;

		bool ok = libAICA_DebugGetRTC(&seconds, &write_enable) && ReadRegister("dc.aica.rtc", "SECONDS", &row_seconds) && ReadRegister("dc.aica.rtc", "WRITE_ENABLE", &row_enable);

		Check(ok && row_seconds == seconds && row_seconds == ((u64)rtc->Read(0x00710000, 4) << 16 | rtc->Read(0x00710004, 4)) && seconds != 0,
			"dc.aica.rtc.SECONDS is %llu, the counter the guest reads as two halves", (unsigned long long)row_seconds);
		Check(ok && row_enable == (write_enable ? 1u : 0u), "dc.aica.rtc.WRITE_ENABLE is %llX, as the accessor has it", (unsigned long long)row_enable);

		// the bit set: the save state of the clock is that bit. The guest reads 0 where it writes it
		if (Check(before.size() == 4, "the save state of the clock is its write enable, %zu bytes", before.size()))
		{
			std::vector<u8> made = before;
			const u32 set = !write_enable;

			memcpy(made.data(), &set, 4);
			RestoreDevice(rtc, made);

			ok = ReadRegister("dc.aica.rtc", "WRITE_ENABLE", &row_enable) && ReadRegister("dc.aica.rtc", "SECONDS", &row_seconds);
			Check(ok && row_enable == set && row_seconds == seconds && rtc->Read(0x00710008, 4) == 0, "with the bit %s: WRITE_ENABLE is %llX, a guest read of 0x00710008 gives 0",
				set ? "set" : "clear", (unsigned long long)row_enable);

			RestoreDevice(rtc, before);
		}

		Check(DeviceState(rtc) == before, "the clock is as it was");
	}

	// the drive
	MMIODevice* gd = sh4_cpu->GetA0Handler(A0H_GDROM);
	const regs::Node* node = regs::Find("dc.gdrom");
	GDRomDebugState snapshot;

	memset(&snapshot, 0, sizeof(snapshot));

	if (node == nullptr || !gdrom_DebugGetState(&snapshot))
	{
		Note("this machine has no GD-ROM drive: the rows of dc.gdrom read as n/a");
		return;
	}

	std::vector<u8> before = DeviceState(gd);
	const u32 kStates = 24;
	std::vector<std::vector<u64>> seen(node->registerCount);
	Tally members, enums;

	for (u32 n = 0; n <= kStates; n++)
	{
		// the first round is the drive as the machine has it
		if (n != 0)
		{
			std::vector<u8> made = MakeGdromState(before, n);

			RestoreDevice(gd, made);
		}

		memset(&snapshot, 0xEE, sizeof(snapshot));
		gdrom_DebugGetState(&snapshot);

		// the two members the device keeps in an enum hold the enumerators the made-up state put there
		if (n != 0)
		{
			gd_states state;
			DiscType disc;

			GdromEnumsMade(n, &state, &disc);
			enums.Add(snapshot.state == (u32)state && snapshot.discType == (u32)disc, "made-up state %u: the drive's state is %u and its disc type %X, the state put %u and %X there",
				n, snapshot.state, snapshot.discType, (u32)state, (u32)disc);
		}

		for (u32 r = 0; r < node->registerCount; r++)
		{
			const regs::Register& reg = node->registers[r];
			u64 value = ~(u64)0, want = 0;
			const bool ok = regs::Read(*node, r, &value, nullptr, 0);
			const bool known = GdromMember(snapshot, reg.name, &want);

			members.Add(ok && known && value == (want & WidthMask(reg.width)), "dc.gdrom.%s in state %u: %llX, the snapshot has %llX", reg.name, n, (unsigned long long)value,
				(unsigned long long)want);

			if (n != 0)
				seen[r].push_back(value);
		}
	}

	RestoreDevice(gd, before);
	enums.Report("in every made-up state the state of the drive's command state machine and the type of its disc are enumerators of their enums, the ones the state was "
		"made with (no other value is loaded from them)");
	members.Report("the rows of dc.gdrom against the members of the snapshot, as the machine stands and in 24 made-up states of the drive");

	// no two rows had the same values in all of the states: each is a member of its own
	u32 alike = 0;
	std::string pair;

	for (u32 a = 0; a < node->registerCount; a++)
	{
		for (u32 b = a + 1; b < node->registerCount; b++)
		{
			if (seen[a] == seen[b])
			{
				alike++;
				pair = std::string(node->registers[a].name) + " and " + node->registers[b].name;
			}
		}
	}

	Check(alike == 0 && node->registerCount == 27, "the %u rows differ from one another over the made-up states (%u pairs do not: %s)", node->registerCount, alike, pair.c_str());
	Check(DeviceState(gd) == before, "the GD-ROM drive is as it was");
}

/*
	regs.rows.pure: reading the rows changes nothing. Every register of the tree is read a hundred times, and the
	save state of every device and the PVR's register file are what they were; as the machine stands, and with the
	made-up states in the devices at once, where a read of the AICA through the guest's read function shows: the
	status words of the file are not what a read works out, and the selected channel has its loop flag set, which
	that read clears. (The FIFO status of the system bus counts its guest reads in any state: regs.values.)
*/
DBG_SELFTEST(regs_rows_pure, "regs.rows.pure")
{
	std::vector<u8> machine;
	const bool quiescent = WaitQuiescent(&machine);

	if (!Check(quiescent, "the machine stands still: two save states of %zu bytes are equal", machine.size()))
		return;

	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);

	auto read_all = [&](u32 times) {
		u32 reads = 0;

		for (u32 n = 0; n < times; n++)
		{
			for (u32 i = 0; i < count; i++)
			{
				for (u32 r = 0; r < nodes[i].registerCount; r++)
				{
					u64 value;
					char text[64];

					regs::Read(nodes[i], r, &value, text, sizeof(text));
					reads++;
				}
			}
		}

		return reads;
	};

	auto name_of = [](const Devices& devices, int changed) -> std::string {
		if (changed == -2)
			return "the PVR's register file";
		if (changed < 0)
			return "none";

		for (u32 slot = 0; slot < A0H_MAX; slot++)
		{
			if (sh4_cpu->GetA0Handler((Area0Hanlders)slot) == devices.devices[changed])
				return "the device in slot " + std::to_string(slot);
		}

		return "a device";
	};

	// as the machine stands
	{
		Devices before = AllDevices();
		const u32 reads = read_all(100);
		const int changed = FirstChanged(before);
		std::vector<u8> after;

		Check(changed == -1 && MachineState(&after) && after == machine, "%u reads, every register a hundred times: the save states of the %zu devices, the PVR's register "
			"file and the machine are as they were (changed: %s)", reads, before.devices.size(), name_of(before, changed).c_str());
	}

	// with the made-up states
	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	SystemBus* sb = sh4_cpu->GetA0H<SystemBus>(A0H_SB);
	MMIODevice* gd = sh4_cpu->GetA0Handler(A0H_GDROM);
	MMIODevice* sb_device = sh4_cpu->GetA0Handler(A0H_SB);

	std::vector<u8> real_aica = DeviceState(aica), real_cpu = DeviceState(scpu), real_gd = DeviceState(gd), real_sb = DeviceState(sb_device);
	std::vector<u8> real_pvr(pvr_regs, pvr_regs + pvr_RegSize);
	AicaMade made;
	const bool is_made = MakeAicaState(aica, real_aica, 3, &made);

	if (!Check(is_made && real_cpu.size() == scpu_state_size, "(a made-up state of the AICA, with the loop flag of channel %u set)", made.selected))
		return;

	{
		std::vector<u8> state = made.state;

		RestoreDevice(aica, state);

		// a request latched and in service
		std::vector<u8> cpu = real_cpu;
		const u32 level = 5, service = 1;

		cpu[scpu_out_at] = 1;
		memcpy(&cpu[scpu_level_at], &level, 4);
		memcpy(&cpu[scpu_service_at], &service, 4);
		RestoreDevice(scpu, cpu);

		GDRomDebugState snapshot;

		if (gdrom_DebugGetState(&snapshot))
		{
			std::vector<u8> drive = MakeGdromState(real_gd, 99);

			RestoreDevice(gd, drive);
		}

		for (u32 i = 0; i < sb->sb_regs.size(); i++)
		{
			if (DataSlot(sb->sb_regs[i]))
				sb->sb_regs[i].data32 = Word32(5, i);
		}

		for (u32 offset : PvrRowOffsets())
		{
			const u32 word = Word32(6, offset / 4);

			memcpy(&pvr_regs[offset], &word, 4);
		}
	}

	{
		Devices before = AllDevices();
		const u32 reads = read_all(100);
		const int changed = FirstChanged(before);
		AicaChannelDebug channel;

		Check(changed == -1, "%u reads with a made-up state in the AICA, the sound CPU, the drive, the system bus and the PVR's register file: the save states of the %zu "
			"devices and the register file are as they were (changed: %s)", reads, before.devices.size(), name_of(before, changed).c_str());
		Check(aica->DebugGetChannel(made.selected, &channel) && channel.looped, "the loop flag of channel %u, which a guest read of the status clears, stands", made.selected);
	}

	// A guest read does show there: the test can tell. The status of the selected channel, read as the guest reads it
	{
		std::vector<u8> before = DeviceState(aica);

		aica->ReadReg(0x2810, 2);
		Check(DeviceState(aica) != before, "(a guest read of the channel status does change that state of the AICA)");
	}

	// back
	RestoreDevice(aica, real_aica);
	RestoreDevice(scpu, real_cpu);
	RestoreDevice(gd, real_gd);
	RestoreDevice(sb_device, real_sb);
	memcpy(pvr_regs, real_pvr.data(), pvr_RegSize);

	std::vector<u8> after;

	Check(MachineState(&after) && after == machine, "the machine is as it was");
}

/*
	regs.tick: what the tree costs a tick. A full tick carries every register of the tree, and every stop, step and
	write sends one: with no breakpoint and no watch listed it has at most kFullTickBytesMax bytes. The time the
	emulator thread takes to build one is printed, with the time of the reads alone.
*/
namespace {

// M2-ROUND, M2-D27: the budget of a full tick with empty tables
const size_t kFullTickBytesMax = 192 * 1024;

// The part of a tick's text from `"key":` up to, and without, `"next":`
size_t PartBytes(const std::string& tick, const char* key, const char* next)
{
	const size_t from = tick.find(std::string("\"") + key + "\":");
	const size_t to = tick.find(std::string(",\"") + next + "\":");

	return from != std::string::npos && to != std::string::npos && to > from ? to - from : 0;
}

}

DBG_SELFTEST(regs_tick, "regs.tick")
{
	typedef std::chrono::steady_clock Clock;

	u32 count;
	const regs::Node* nodes = regs::Nodes(&count);
	u32 registers = 0, with_registers = 0;

	for (u32 i = 0; i < count; i++)
	{
		registers += nodes[i].registerCount;
		with_registers += nodes[i].registerCount != 0;
	}

	const std::string full = core::BuildTickText(true);
	const std::string base = core::BuildTickText(false);

	const bool empty_tables = full.find("\"breakpoints\":{}") != std::string::npos && full.find("\"watches\":[]") != std::string::npos;

	// every node that has registers is a key of the tick's registers
	u32 carried = 0;

	for (u32 i = 0; i < count; i++)
	{
		if (nodes[i].registerCount != 0 && full.find(std::string("\"") + nodes[i].path + "\":[{\"name\":\"") != std::string::npos)
			carried++;
	}

	Check(empty_tables && carried == with_registers, "a full tick with no breakpoint and no watch listed carries the %u nodes that have registers (%u found)",
		with_registers, carried);
	Check(full.size() <= kFullTickBytesMax, "it has %zu bytes for %u nodes and %u registers: at most %zu (192 KiB)", full.size(), count, registers,
		kFullTickBytesMax);
	Note("of them the registers are %zu bytes and the event log is %zu; a tick with the base paths only has %zu bytes", PartBytes(full, "registers", "breakpoints"),
		PartBytes(full, "eventLog", "watches"), base.size());

	// the time of a build, on this thread: the fastest and the median of 41
	{
		const u32 kRounds = 41;
		std::vector<double> build_us, read_us;
		size_t bytes = 0;

		for (u32 round = 0; round < kRounds; round++)
		{
			Clock::time_point t0 = Clock::now();
			const std::string tick = core::BuildTickText(true);
			Clock::time_point t1 = Clock::now();

			bytes += tick.size();

			// the reads alone, without their text
			for (u32 i = 0; i < count; i++)
			{
				for (u32 r = 0; r < nodes[i].registerCount; r++)
				{
					u64 value;
					char text[128];

					regs::Read(nodes[i], r, &value, text, sizeof(text));
				}
			}

			Clock::time_point t2 = Clock::now();

			build_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
			read_us.push_back(std::chrono::duration<double, std::micro>(t2 - t1).count());
		}

		std::sort(build_us.begin(), build_us.end());
		std::sort(read_us.begin(), read_us.end());

		Check(bytes != 0, "%u full ticks were built", kRounds);
		Note("the build of a full tick takes %.0f us at its fastest and %.0f us in the median of %u; reading the %u registers without making their text %.0f us and %.0f us",
			build_us.front(), build_us[kRounds / 2], kRounds, registers, read_us.front(), read_us[kRounds / 2]);
	}
}

/*
	regs.enabled: dc.aica.arm7.ENABLED where the booted program holds and releases the ARM7. The register is
	1 while the ARM7 is clocked and 0 while it is held in reset: from a machine reset on, and whenever the guest
	holds it with ARMRST.

	The guest test of this file. It runs the booted program from its start for kEnabledCycles and stops it
	wherever the program has written another value to ARMRST: a scheduler callback of the test looks at that
	byte, as the SH4 reads it, in every slice and asks for a pause when it is no longer what it was at the last
	stop. A stop is therefore the slice end behind such a write, a slice of the SH4 behind the store at most;
	where the guest goes into a SLEEP wait in that slice, it is the end of the wait.

	At the stop of the start, with nothing executed, the register has to read 0. At every stop behind a write it
	has to be what the guest wrote: 0 behind a write of 1 to ARMRST, 1 behind a write of 0. At every stop it has
	to be what two other sources say as well: the flag in the save state of the sound CPU, and
	SoundCPU::DebugEnabled(), which is what the unwinder of the ARM7 call stack and an ARM7 step ask
	(dbg_callstack.h, dbg_emu.h). And the program has to release the ARM7 on the way, with the register reading
	1 behind it.

	ARMRST is the witness of what the guest did and no more. It is not what the debugger shows of the reset:
	from a machine reset until the guest first writes it, it reads 0 with the ARM7 held, as the start of every
	run has it.

	The program is 2ndmix of the KallistiOS examples, which writes ARMRST four times with a value that is not
	the one before: spu_init() holds the ARM7, clears the wave memory and releases it; play_s3m() holds it,
	loads the player and releases it.
*/
namespace {

const u64 kEnabledCycles = 200 * 1000 * 1000;		// of the scheduler: a second of the guest

// How the ARM7 stands, by every source
struct Arm7Clock {
	bool read;			// dc.aica.arm7.ENABLED gave a value
	u64 enabled;		// that value
	bool accessor;		// SoundCPU::DebugEnabled()
	bool state;			// the flag in the save state of the sound CPU
	u8 armrst;			// the byte at 0x00702C00 as the SH4 reads it
	bool agree;			// the register is the flag of the save state, and the accessor says the same
};

u8 ReadArmrst()
{
	u8 byte = 0xEE;

	sh4_cpu->GetA0H<AICA>(A0H_AICA)->DebugReadRegs(0x2C00, 1, &byte);
	return byte;
}

Arm7Clock ReadArm7Clock()
{
	SoundCPU* scpu = sh4_cpu->GetA0H<SoundCPU>(A0H_SCPU);
	std::vector<u8> state = DeviceState(scpu);
	Arm7Clock c = { };

	c.enabled = ~(u64)0;
	c.read = ReadRegister("dc.aica.arm7", "ENABLED", &c.enabled);
	c.accessor = scpu->DebugEnabled();
	c.state = state.size() == scpu_state_size && state[scpu_tail_at + 6] != 0;
	c.armrst = ReadArmrst();
	c.agree = c.read && state.size() == scpu_state_size && c.enabled == (c.state ? 1u : 0u) && c.accessor == c.state;

	return c;
}

struct {
	int id = -1;		// of the scheduler callback
	bool on;
	bool asked;			// a pause, for the stop that comes
	bool at_end;		// that pause is for the end of the walk
	u8 armrst;			// ARMRST at the last stop
	u64 end;			// the scheduler's time the walk ends at
	u32 phase;
	u32 held, released;	// the stops behind a write of 1, and of 0
	u32 zeros, ones;	// those of them at which the register read 0, and 1, with the other sources
} en;

// In every slice of the walk, on the emulator thread: a pause is asked for in the slice in which the guest wrote
// another value to ARMRST, and at the end of the walk
int EnabledTick(void*, int, int, int)
{
	if (!en.on)
		return 0;

	if (!en.asked)
	{
		en.at_end = sh4_sched_now64() >= en.end;

		if (en.at_end || ReadArmrst() != en.armrst)
		{
			en.asked = true;
			emu::RequestPause();
		}
	}

	// again in the next slice
	return 1;
}

}

DBG_SELFTEST_GUEST(regs_enabled, "regs.enabled")
{
	const Arm7Clock now = ReadArm7Clock();
	const u32 pc = p_sh4rcb->cntx.pc;
	const unsigned long long time = sh4_sched_now64();

	if (en.phase == 0)
	{
		en.phase = 1;

		Check(stop.cause == StopCause::Start && now.agree && now.enabled == 0,
			"the start of the booted program, nothing executed (SH4 pc %08X, time %llu): the ARM7 is held in reset, dc.aica.arm7.ENABLED reads %llX, "
			"DebugEnabled() is %d, the save state has %d", pc, time, (unsigned long long)now.enabled, now.accessor, now.state);
		Note("ARMRST reads %02X for the SH4 there: the guest has not written it", now.armrst);

		en.armrst = now.armrst;
		en.end = time + kEnabledCycles;
		en.asked = false;
		en.on = true;

		if (en.id < 0)
			en.id = sh4_sched_register(nullptr, 0, EnabledTick);
		sh4_sched_request(en.id, 1);

		emu::Resume();
		return false;
	}

	const bool wrote = now.armrst != en.armrst;

	if (stop.cause != StopCause::Pause || stop.lifetime != Lifetime::Parked || !en.asked || (!wrote && !en.at_end))
	{
		Check(false, "the booted program runs for %llu cycles and is stopped by the test only (a stop with \"%s\" at SH4 pc %08X, time %llu)",
			(unsigned long long)kEnabledCycles, StopCauseName(stop.cause), pc, time);
		en.on = false;
		return true;
	}

	if (wrote)
	{
		const bool hold = (now.armrst & 1) != 0;

		(hold ? en.held : en.released)++;

		if (now.agree && now.enabled == (hold ? 0u : 1u))
			(hold ? en.zeros : en.ones)++;

		Check(now.agree && now.enabled == (hold ? 0u : 1u),
			"the guest wrote %02X to ARMRST and %s the ARM7 (the stop behind it: SH4 pc %08X, time %llu): dc.aica.arm7.ENABLED reads %llX, "
			"DebugEnabled() is %d, the save state has %d", now.armrst, hold ? "holds" : "released", pc, time, (unsigned long long)now.enabled, now.accessor,
			now.state);

		en.armrst = now.armrst;
		en.asked = false;

		emu::Resume();
		return false;
	}

	en.on = false;

	Check(now.agree && now.enabled == ((now.armrst & 1) ? 0u : 1u),
		"the end of the walk (SH4 pc %08X, time %llu), with ARMRST at %02X: dc.aica.arm7.ENABLED reads %llX, DebugEnabled() is %d, the save state has %d", pc,
		time, now.armrst, (unsigned long long)now.enabled, now.accessor, now.state);
	Check(en.ones != 0 && en.ones == en.released && en.zeros == en.held,
		"the guest released the ARM7 on the way: dc.aica.arm7.ENABLED read 1 at %u of %u stops behind a write that released it, and 0 at %u of %u stops "
		"behind a write that holds it", en.ones, en.released, en.zeros, en.held);

	return true;
}
