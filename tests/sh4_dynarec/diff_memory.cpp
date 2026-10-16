#include "diff_memory.h"

#include "test_guard.h"

#include "hw/sh4/sh4_if.h"
#include "hw/mem/_vmem.h"

//An engine that makes more accesses than this is not going to end well, a vector makes 2
#define DIFF_LOG_MAX 32

static std::vector<SsBusAccess> bus_log;
static std::vector<u32> fetches;

static bool pc_fetches;
static bool serve_opcodes;

static u32 base_pc;
static u32 opcodes[5];

//What the handlers are registered with, and have to be called with
static u32 handler_ctx;

static bool has_read;
static u32 read_addr;
static u32 read_size;
static u64 read_value;

u32 diff_mem_opcode_at(u32 addr)
{
	u32 offset = addr - base_pc;

	if ((offset & 1) == 0 && (offset >> 1) < 4)
		return opcodes[offset >> 1];

	return opcodes[4];
}

static u32 data_at(u32 size, u32 addr)
{
	if (has_read)
	{
		//64 bit accesses reach the handlers as two 32 bit ones
		if (read_size == 8 && size == 4 && (addr == read_addr || addr == read_addr + 4))
			return (u32)(read_value >> (addr == read_addr ? 0 : 32));

		if (read_size == size && addr == read_addr)
			return (u32)read_value;
	}

	u32 value = addr * 0x9E3779B1u;

	return value ^ (value >> 15);
}

static void log_access(u32 kind, u32 size, u32 addr, u32 value)
{
	if (bus_log.size() >= DIFF_LOG_MAX)
		return;

	SsBusAccess a;

	a.kind = (u8)kind;
	a.size = (u8)size;
	a.addr = addr;
	a.value = value;

	bus_log.push_back(a);
}

static u32 bus_read(u32 size, u32 addr)
{
	if (size == 2)
	{
		if (serve_opcodes)
			return diff_mem_opcode_at(addr);

		if (pc_fetches && addr == Sh4cntx.pc - 2)
		{
			if (fetches.size() < DIFF_LOG_MAX)
				fetches.push_back(addr);

			return diff_mem_opcode_at(addr);
		}
	}

	u32 mask = size == 1 ? 0xFF : size == 2 ? 0xFFFF : 0xFFFFFFFF;
	u32 value = data_at(size, addr) & mask;

	log_access(SSB_READ, size, addr, value);

	return value;
}

static void bus_write(u32 size, u32 addr, u32 data)
{
	u32 mask = size == 1 ? 0xFF : size == 2 ? 0xFFFF : 0xFFFFFFFF;

	log_access(SSB_WRITE, size, addr, data & mask);
}

//Ends the engine that is running: the blocks of a recompiler call the handlers of constant addresses directly
static void check_ctx(void* ctx)
{
	if (ctx != &handler_ctx)
		test_guard_abort("a memory handler was called with a context that is not the one it is registered with");
}

static u8 DYNACALL read8(void* ctx, u32 addr) { check_ctx(ctx); return (u8)bus_read(1, addr); }
static u16 DYNACALL read16(void* ctx, u32 addr) { check_ctx(ctx); return (u16)bus_read(2, addr); }
static u32 DYNACALL read32(void* ctx, u32 addr) { check_ctx(ctx); return bus_read(4, addr); }

static void DYNACALL write8(void* ctx, u32 addr, u8 data) { check_ctx(ctx); bus_write(1, addr, data); }
static void DYNACALL write16(void* ctx, u32 addr, u16 data) { check_ctx(ctx); bus_write(2, addr, data); }
static void DYNACALL write32(void* ctx, u32 addr, u32 data) { check_ctx(ctx); bus_write(4, addr, data); }

void diff_mem_install()
{
	_vmem_handler handler = _vmem_register_handler(&handler_ctx, read8, read16, read32, write8, write16, write32);

	_vmem_map_handler(handler, 0x00, 0xFF);
}

void diff_mem_begin(const SsTest& test, u32 size)
{
	base_pc = test.initial.s.PC;
	for (u32 i = 0; i < 5; i++)
		opcodes[i] = test.opcodes.op[i];

	has_read = false;
	read_size = size;

	//only the instruction under test accesses data, a vector has one read at most
	for (u32 i = 0; i < 4 && !has_read; i++)
	{
		const SsCycle& c = test.cycles.c[i];

		if ((c.actions & SS_ACTION_READ) && size != 0)
		{
			has_read = true;
			read_addr = c.read_addr;
			read_value = c.read_val;
		}
	}

	diff_mem_start(false);
}

void diff_mem_start(bool by_pc)
{
	bus_log.clear();
	fetches.clear();

	pc_fetches = by_pc;
	serve_opcodes = false;
}

void diff_mem_serve_opcodes(bool on)
{
	serve_opcodes = on;
}

const std::vector<SsBusAccess>& diff_mem_log()
{
	return bus_log;
}

const std::vector<u32>& diff_mem_fetches()
{
	return fetches;
}
