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

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

/*
	Selftests of dbg::regs, dbg::mem and the device accessors behind them. All but the last are machine tests:
	they read, and where they compare with what the guest would read they put the machine back as it was. The
	last, regs.enabled, is a guest test: it runs the booted program and reads at stops of it.

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
		DspDebugState state = { 99, 99 };
		u64 step = 99, mdec_ct = 99;
		bool ok = dsp->DebugGetState(&state);

		Check(ok && state.step == 0 && state.mdecCt == dsp->GetDspContext()->regs.MDEC_CT, "DSP: step %u, MDEC_CT %u", state.step, state.mdecCt);

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
