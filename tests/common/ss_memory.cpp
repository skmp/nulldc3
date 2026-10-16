#include "ss_memory.h"

#include "hw/mem/_vmem.h"

#include <cstdarg>
#include <cstdio>

static std::vector<SsBusAccess> expected;
static std::vector<SsBusAccess> bus_log;

static u32 next_expected;	//index into expected
static u32 half_done;		//the low word of a 64 bit access was made
static u32 fetches;
static bool serve_opcodes;

static u32 base_pc;
static u32 opcodes[5];

static char error[256];

//What the handlers are registered with. A recompiler that resolves a constant address to a handler has to call
//it with this, as _vmem_ReadMem* / _vmem_WriteMem* do
static u32 handler_ctx;

static void fail(const char* fmt, ...)
{
	//only the first one, what follows it is fallout
	if (error[0])
		return;

	va_list args;

	va_start(args, fmt);
	vsnprintf(error, sizeof(error), fmt, args);
	va_end(args);
}

static const char* kind_name(u32 kind)
{
	return kind == SSB_FETCH ? "fetch" : kind == SSB_READ ? "read" : "write";
}

static void log_access(u32 kind, u32 size, u32 addr, u64 value)
{
	//a vector makes 6 accesses, a CPU that keeps going after an error is not worth more
	if (bus_log.size() >= 32)
		return;

	SsBusAccess a;

	a.kind = (u8)kind;
	a.size = (u8)size;
	a.addr = addr;
	a.value = value;

	bus_log.push_back(a);
}

u32 ss_mem_opcode_at(u32 addr)
{
	u32 offset = addr - base_pc;

	if ((offset & 1) == 0 && (offset >> 1) < 4)
		return opcodes[offset >> 1];

	return opcodes[4];
}

//Matches an access of size bytes at addr against the next expected one. Returns it when the access is (a half
//of) that one, with half telling which word of a 64 bit access it is
static const SsBusAccess* match(bool write, u32 size, u32 addr, u32& half)
{
	half = 0;

	if (next_expected >= expected.size())
	{
		fail("%s%u @%08X: no bus access is expected any more", write ? "write" : "read", size * 8, addr);
		return 0;
	}

	const SsBusAccess& e = expected[next_expected];

	bool kind_ok = write ? e.kind == SSB_WRITE : e.kind != SSB_WRITE;

	//64 bit accesses reach the handlers as two 32 bit ones, low word first
	if (kind_ok && e.size == 8 && size == 4)
	{
		if (addr == e.addr + half_done * 4)
		{
			half = half_done;

			if (half_done)
			{
				half_done = 0;
				next_expected++;
			}
			else
			{
				half_done = 1;
			}

			return &e;
		}
	}
	else if (kind_ok && e.size == size && addr == e.addr)
	{
		next_expected++;

		if (e.kind == SSB_FETCH)
			fetches++;

		return &e;
	}

	fail("expected %s%u @%08X, got %s%u @%08X", kind_name(e.kind), e.size * 8, e.addr + half_done * 4,
		write ? "write" : "read", size * 8, addr);

	return 0;
}

static u32 bus_read(u32 size, u32 addr)
{
	if (serve_opcodes && size == 2)
		return ss_mem_opcode_at(addr);

	u32 half;
	const SsBusAccess* e = match(false, size, addr, half);

	u32 value = e ? (u32)(e->value >> (half * 32)) : 0;

	log_access(e && e->kind == SSB_FETCH ? SSB_FETCH : SSB_READ, size, addr, value);

	return value;
}

static void bus_write(u32 size, u32 addr, u32 data)
{
	log_access(SSB_WRITE, size, addr, data);

	u32 half;
	const SsBusAccess* e = match(true, size, addr, half);

	if (!e)
		return;

	u32 mask = size == 1 ? 0xFF : size == 2 ? 0xFFFF : 0xFFFFFFFF;
	u32 want = (u32)(e->value >> (half * 32)) & mask;

	if ((data & mask) != want)
		fail("write%u @%08X: value %0*X, expected %0*X", size * 8, addr, size * 2, data & mask, size * 2, want);
}

static void check_ctx(void* ctx)
{
	if (ctx != &handler_ctx)
		fail("a memory handler was called with a context that is not the one it is registered with");
}

static u8 DYNACALL read8(void* ctx, u32 addr) { check_ctx(ctx); return (u8)bus_read(1, addr); }
static u16 DYNACALL read16(void* ctx, u32 addr) { check_ctx(ctx); return (u16)bus_read(2, addr); }
static u32 DYNACALL read32(void* ctx, u32 addr) { check_ctx(ctx); return bus_read(4, addr); }

static void DYNACALL write8(void* ctx, u32 addr, u8 data) { check_ctx(ctx); bus_write(1, addr, data); }
static void DYNACALL write16(void* ctx, u32 addr, u16 data) { check_ctx(ctx); bus_write(2, addr, data); }
static void DYNACALL write32(void* ctx, u32 addr, u32 data) { check_ctx(ctx); bus_write(4, addr, data); }

void ss_mem_install()
{
	_vmem_handler handler = _vmem_register_handler(&handler_ctx, read8, read16, read32, write8, write16, write32);

	_vmem_map_handler(handler, 0x00, 0xFF);
}

static void expect(u32 kind, u32 size, u32 addr, u64 value)
{
	SsBusAccess a;

	a.kind = (u8)kind;
	a.size = (u8)size;
	a.addr = addr;
	a.value = value;

	expected.push_back(a);
}

void ss_mem_begin(const SsTest& test, u32 read_size, u32 write_size, bool ordered_fetches)
{
	expected.clear();
	bus_log.clear();

	next_expected = 0;
	half_done = 0;
	fetches = 0;
	serve_opcodes = false;
	error[0] = 0;

	base_pc = test.initial.s.PC;
	for (u32 i = 0; i < 5; i++)
		opcodes[i] = test.opcodes.op[i];

	for (u32 i = 0; i < 4; i++)
	{
		const SsCycle& c = test.cycles.c[i];

		if ((c.actions & SS_ACTION_FETCH) && ordered_fetches)
			expect(SSB_FETCH, 2, c.fetch_addr, c.fetch_val);

		if (c.actions & SS_ACTION_READ)
		{
			if (read_size == 0)
				fail("the vector reads @%08X but the file's policy has no read width", c.read_addr);

			expect(SSB_READ, read_size, c.read_addr, c.read_val);
		}

		if (c.actions & SS_ACTION_WRITE)
		{
			if (write_size == 0)
				fail("the vector writes @%08X but the file's policy has no write width", c.write_addr);

			expect(SSB_WRITE, write_size, c.write_addr, c.write_val);
		}
	}
}

void ss_mem_serve_opcodes(bool on)
{
	serve_opcodes = on;
}

bool ss_mem_end()
{
	if (next_expected < expected.size())
	{
		const SsBusAccess& e = expected[next_expected];

		if (half_done)
			fail("expected read/write32 @%08X (high word of a 64 bit access) was not made", e.addr + 4);
		else
			fail("expected %s%u @%08X was not made (%u of %u expected accesses done)", kind_name(e.kind), e.size * 8, e.addr,
				next_expected, (u32)expected.size());
	}

	return error[0] == 0;
}

const char* ss_mem_error()
{
	return error;
}

u32 ss_mem_fetches()
{
	return fetches;
}

const std::vector<SsBusAccess>& ss_mem_expected()
{
	return expected;
}

const std::vector<SsBusAccess>& ss_mem_log()
{
	return bus_log;
}
