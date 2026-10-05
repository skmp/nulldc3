/*
	sh4bp_cost <jit|interpreter> [never]: what the breakpoint mechanism costs a guest that uses none of it
	(gate B11). README.md has the criteria; b11_compare.cmake applies them.

	The program is built twice from the same sources, with the debugger (NULLDC_DEBUGGER) and without, and
	counts the host instructions its thread executes in user mode, with the performance counter of the kernel,
	while the workload of the gates runs on a machine like theirs. That machine has no run control: nothing
	stops here.

	  never   (with the debugger) one address is armed that the guest does not reach. It lies on the page of
	          the workload: the decoder's query finds the page of every instruction it decodes marked, and
	          goes on to search the armed set

	A run is two windows of the same number of slices. In the first the recompiler compiles what the workload
	executes; in the second it compiles nothing (the program fails if it does). One line comes out:

	  cost: engine=<e> armed=<armed addresses> slices=<slices of a window> blocks=<blocks in the cache>
	        opcodes=<guest instructions of those blocks> bytes=<host code emitted>
	        first=<host instructions of the first window> steady=<of the second>

	or, where the counter is not there (another host, a kernel that does not let the process count),
	"SKIPPED: <why>" with exit code 0.
*/
#include "types.h"
#include "guest.inc"

#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/dyna/ngen.h"
#include "hw/mem/_vmem.h"
#include "debugger/debugger.h"

#if defined(NULLDC_DEBUGGER)
#include "debugger/dbg_sh4.h"
#include "debugger/dbg_mem.h"
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

//hw/sh4/dyna/driver.cpp, blockmanager.cpp
extern u32 LastAddr;
extern u32 LastAddr_min;
extern std::set<RuntimeBlockInfo*> all_blocks;

#define STACK_TOP 0x8C00F000
#define DEVICE    0x00600000	//low 29 bits of 0xA0600000

//slices per window, and the interrupt request at every n-th slice end
#define WINDOW     20000
#define IRQ_PERIOD 5

static u8* ram;
static bool page_locked[RAM_SIZE / REI_PAGE_SIZE];

static SuperH4Backend* backend;

static u64 slices;
static u64 stop_slice;

static void fatal(const char* what)
{
	printf("sh4bp_cost: cannot go on: %s\n", what);
	fflush(stdout);
	exit(1);
}


/*
	The counter
*/
#if defined(__linux__)

static int counter_fd = -1;

static bool counter_open()
{
	struct perf_event_attr attr;

	memset(&attr, 0, sizeof(attr));
	attr.type = PERF_TYPE_HARDWARE;
	attr.size = sizeof(attr);
	attr.config = PERF_COUNT_HW_INSTRUCTIONS;
	attr.exclude_kernel = 1;
	attr.exclude_hv = 1;

	counter_fd = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);

	if (counter_fd < 0)
	{
		printf("SKIPPED: the kernel does not let this process count its instructions (perf_event_open: %s)\n", strerror(errno));
		return false;
	}

	return true;
}

static u64 counter_read()
{
	long long value = 0;

	if (read(counter_fd, &value, sizeof(value)) != sizeof(value))
		fatal("the counter cannot be read");

	return (u64)value;
}

static void* alloc_pages_zeroed(size_t size)
{
	void* rv = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return rv == MAP_FAILED ? 0 : rv;
}

#else

static bool counter_open()
{
	printf("SKIPPED: host instructions are counted on Linux only\n");
	return false;
}

static u64 counter_read() { return 0; }
static void* alloc_pages_zeroed(size_t size) { return calloc(1, size); }

#endif


/*
	The machine: the memory, the interrupt source and the time base of machine.cpp
*/
struct CostMmr : SuperH4Mmr
{
	void Reset() { }
	void rio_reg(void* context, RegisterStruct* arr, size_t arr_size, u32 addr, RegIO flags, u32 sz, RegReadAddrFP* rp, RegWriteAddrFP* wp) { }
};

struct CostSh4 : SuperH4
{
	void SetA0Handler(Area0Hanlders slot, MMIODevice* dev) { }
	MMIODevice* GetA0Handler(Area0Hanlders slot) { return 0; }

	bool setBackend(SuperH4Backends) { return true; }
	SuperH4Backend* DebugBackend() { return backend; }

	bool Init() { return true; }
	void Reset(bool Manual) { }
	void Term() { }

	void Run() { }
	void Stop() { }
	void Start() { }
	void Step() { }
	void Skip() { }

	bool IsRunning() { return sh4_int_bCpuRun; }

	void ResetCache() { fatal("the code cache was reset"); }
};

static bool is_ram(u32 addr)
{
	return ((addr >> 26) & 7) == 3 && (addr >> 29) != 7;
}

bool IsOnRam(u32 addr)
{
	return is_ram(addr) && (addr >> 29) != 3;
}

u8* GetMemPtr(u32 addr, u32 size)
{
	return is_ram(addr) ? &ram[addr & RAM_MASK] : 0;
}

void VLockedMemory::LockRegion(unsigned offset, unsigned size_bytes)
{
	if (this != &sh4_cpu->mram)
		return;

	for (unsigned page = offset / REI_PAGE_SIZE; page < (offset + size_bytes + REI_PAGE_MASK) / REI_PAGE_SIZE; page++)
		page_locked[page] = true;
}

void VLockedMemory::UnLockRegion(unsigned offset, unsigned size_bytes)
{
	if (this != &sh4_cpu->mram)
		return;

	for (unsigned page = offset / REI_PAGE_SIZE; page < (offset + size_bytes + REI_PAGE_MASK) / REI_PAGE_SIZE; page++)
		page_locked[page] = false;
}

static u32 bus_read(u32 size, u32 addr)
{
	u32 value = 0;

	if (!is_ram(addr))
		fatal("a guest read outside of RAM");

	memcpy(&value, &ram[addr & RAM_MASK], size);
	return value;
}

static void bus_write(u32 size, u32 addr, u32 data)
{
	if (is_ram(addr))
	{
		//a write to a page that compiled code was made from goes the way a write fault goes in the emulator
		if (page_locked[(addr & RAM_MASK) / REI_PAGE_SIZE])
			bm_LockedWrite((u8*)((uintptr_t)virt_ram_base + addr));

		memcpy(&ram[addr & RAM_MASK], &data, size);
		return;
	}

	if ((addr & 0x1FFFFFFC) != DEVICE)
		fatal("a guest write outside of RAM and the device");

	ResetInterruptPend(sh4_IRL_9);
}

static u32 handler_ctx;

static u8 DYNACALL read8(void* ctx, u32 addr) { return (u8)bus_read(1, addr); }
static u16 DYNACALL read16(void* ctx, u32 addr) { return (u16)bus_read(2, addr); }
static u32 DYNACALL read32(void* ctx, u32 addr) { return bus_read(4, addr); }

static void DYNACALL write8(void* ctx, u32 addr, u8 data) { bus_write(1, addr, data); }
static void DYNACALL write16(void* ctx, u32 addr, u16 data) { bus_write(2, addr, data); }
static void DYNACALL write32(void* ctx, u32 addr, u32 data) { bus_write(4, addr, data); }

static void unaligned_access(u32 addr, u32 sz, bool write)
{
	fatal("unaligned guest access");
}

//Time, with the tests of the debugger that the emulator's UpdateSystem() makes in every slice
extern "C" int UpdateSystem()
{
	if (unlikely(dbg::g_stop_latched))
		return 1;

	slices++;

	if (slices % IRQ_PERIOD == 0)
		SetInterruptPend(sh4_IRL_9);

	if (unlikely(dbg::g_attn.load(std::memory_order_relaxed)))
		dbg::emu::SliceHook();

	if (slices == stop_slice)
		sh4_int_bCpuRun = false;

	return Sh4cntx.interrupt_pend | (sh4_int_bCpuRun == false);
}

extern "C" int UpdateSystem_INTC()
{
	if (UpdateSystem())
		return UpdateINTC();
	else
		return 0;
}

#if defined(NULLDC_DEBUGGER)

//The debugger around dbg::sh4: no run control, and memory for the opcodes it reads
namespace dbg {

std::atomic<u32> g_attn(0);
bool g_stop_latched;
bool g_intc_deferred;
bool g_in_sleep;
bool g_sleep_cut;
std::atomic<bool> g_parked(false);

namespace emu {

void SliceHook() { }
void LatchStop(StopCause cause, u32 breakpointId) { fatal("a debugger stop"); }
bool OnEmuThread() { return true; }

}

namespace mem {

u32 Read(Target t, u32 addr, u32 len, u8* out, u8* readableBits)
{
	if (readableBits)
		memset(readableBits, 0xFF, (len + 7) / 8);

	for (u32 i = 0; i < len; i++)
		out[i] = (u8)bus_read(1, addr + i);

	return len;
}

}

}

#endif

static void machine_start(bool interpreter)
{
	//as the emulator starts up (LoadSettings), but for the host mapping of guest memory: there is none
	settings.dynarec.Enable = true;
	settings.dynarec.idleskip = true;
	settings.dynarec.unstable_opt = false;
	settings.dynarec.safemode = true;
	settings.dynarec.SmcCheckLevel = FullCheck;
	settings.dynarec.disable_nvmem = true;

	p_sh4rcb = (Sh4RCB*)alloc_pages_zeroed(sizeof(Sh4RCB));
	ram = (u8*)alloc_pages_zeroed(RAM_SIZE);

	if (p_sh4rcb == 0 || ram == 0)
		fatal("no memory");

	static CostSh4 cpu;

	cpu.sh4mmr.reset(new CostMmr());
	cpu.mram.SetRegion(ram, RAM_SIZE);
	sh4_cpu = &cpu;

	_vmem_reset();
	_vmem_unaligned_handler = &unaligned_access;
	_vmem_map_handler(_vmem_register_handler(&handler_ctx, read8, read16, read32, write8, write16, write32), 0x00, 0xFF);

	interrupts_init(cpu.sh4mmr.get());

	backend = interpreter ? Get_Sh4Interpreter() : Get_Sh4Recompiler();

	if (!backend->Init())
		fatal("backend init");

	//SuperH4_impl::Reset
	next_pc = 0xA0000000;

	sh4_sr_SetFull(0x700000F0);
	old_sr.status = sr.status;
	UpdateSR();

	fpscr.full = 0x00040001;
	old_fpscr = fpscr;
	UpdateFPSCR();

	interrupts_reset();
	bm_Reset();
	backend->ResetSlice();

	//the workload, as the gates run it: FPU instructions in, no SLEEP, interrupts open
	memcpy(&ram[G_BASE & RAM_MASK], guest_image, sizeof(guest_image));

	sh4_sr_SetFull(0x40000000);
	UpdateSR();

	vbr = G_vbr;
	r[15] = STACK_TOP;
	r[10] = 0xFFFFFFFF;
	r[12] = 0;
	r[14] = 0;

	next_pc = G_work;
}

//The next window of slices: the host instructions it took
static u64 window()
{
	stop_slice = slices + WINDOW;
	sh4_int_bCpuRun = true;

	u64 before = counter_read();

	backend->Loop();

	u64 after = counter_read();

	if (slices != stop_slice)
		fatal("the loop did not end with its window");

	return after - before;
}

int main(int argc, char** argv)
{
	const bool interpreter = argc > 1 && strcmp(argv[1], "interpreter") == 0;
	const bool never = argc > 2 && strcmp(argv[2], "never") == 0;

	if (argc < 2 || argc > 3 || (!interpreter && strcmp(argv[1], "jit") != 0) || (argc == 3 && !never))
	{
		printf("usage: sh4bp_cost <jit|interpreter> [never]\n");
		return 2;
	}

	if (!counter_open())
		return 0;

	machine_start(interpreter);

	u32 armed = 0;

	if (never)
	{
#if defined(NULLDC_DEBUGGER)
		const u32 id = 1, addr = G_line;

		dbg::sh4::SetBreakpoints(&id, &addr, 1);
		dbg::sh4::SetActiveMask(~0u);
		armed = dbg_sh4_armed;
#else
		fatal("built without the debugger: nothing can be armed");
#endif
	}

	const u64 first = window();
	const size_t blocks = all_blocks.size();
	const u32 bytes = LastAddr - LastAddr_min;

	u32 opcodes = 0;

	for (RuntimeBlockInfo* blk : all_blocks)
		opcodes += blk->guest_opcodes;

	const u64 steady = window();

	if (all_blocks.size() != blocks || LastAddr - LastAddr_min != bytes)
		fatal("the second window compiled");
	if (!interpreter && blocks == 0)
		fatal("nothing was compiled");
	if (r[9] < 100)
		fatal("the workload did not run");

	printf("cost: engine=%s armed=%u slices=%u blocks=%u opcodes=%u bytes=%u first=%llu steady=%llu\n", interpreter ? "interpreter" : "jit", armed,
		WINDOW, (u32)blocks, opcodes, bytes, (unsigned long long)first, (unsigned long long)steady);

	return 0;
}
