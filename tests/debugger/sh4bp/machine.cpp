#include "machine.h"
#include "guest.inc"

#include "hw/sh4/sh4_core.h"
#include "hw/sh4/sh4_dbg.h"
#include "hw/sh4/sh4_interpreter.h"
#include "hw/sh4/sh4_interrupts.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/sh4/dyna/blockmanager.h"
#include "hw/sh4/dyna/ngen.h"
#include "hw/mem/_vmem.h"
#include "debugger/dbg_mem.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

//hw/sh4/dyna/driver.cpp
extern u32 LastAddr;
extern u32 LastAddr_min;

#define STACK_TOP  0x8C00F000
#define DEVICE     0x00600000	//low 29 bits of 0xA0600000
#define HASHED_RAM 0x00100000	//the guest programs stay inside the first megabyte

MachineKnobs knobs;
MachineCounters counters;

static u8* ram;
static bool page_locked[RAM_SIZE / REI_PAGE_SIZE];

static Engine engine;
static SuperH4Backend* backends[2];		//the recompiler, the interpreter
static SuperH4Backend* backend;			//the one the SH4 runs on

static u64 sched_now;
static bool host_stopped;
static u64 host_stop_made;		//the knobs.host_stop_slice that the last host stop was made for

//the slice hook's part of dbg::emu: what waits for a slice end outside of a SLEEP wait
static bool pause_wanted;
static bool excursion_wanted;
static bool in_step;

//what other threads asked for
static std::atomic<bool> ext_pause(false);
static std::atomic<bool> ext_call_pending(false);
static std::mutex ext_mutex;
static std::condition_variable ext_done;
static std::function<void()> ext_call;

//what LatchStop was told since the park context was entered last
static bool latch_has_cause;
static dbg::StopCause latch_cause;
static u32 latch_id;

static bool used_fallback;

const char* engine_name(Engine e)
{
	switch (e)
	{
	case ENGINE_JIT:      return "jit";
	case ENGINE_INTERP:   return "interpreter";
	case ENGINE_FALLBACK: return "fallback";
	default:              return "?";
	}
}

static void fatal(const char* what)
{
	printf("sh4bp: the machine cannot go on: %s\n", what);
	fflush(stdout);
	abort();
}


/*
	The SH4 object
*/
struct MachineMmr : SuperH4Mmr
{
	void Reset() { }
	void rio_reg(void* context, RegisterStruct* arr, size_t arr_size, u32 addr, RegIO flags, u32 sz, RegReadAddrFP* rp, RegWriteAddrFP* wp) { }
};

struct MachineSh4 : SuperH4
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

	void ResetCache() { backends[0]->ClearCache(); }
};


/*
	Memory
*/
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
	if (is_ram(addr))
	{
		u32 value = 0;
		memcpy(&value, &ram[addr & RAM_MASK], size);
		return value;
	}

	counters.stray++;
	return 0;
}

static void bus_write(u32 size, u32 addr, u32 data)
{
	if (is_ram(addr))
	{
		//The emulator write-protects the pages that compiled code was made from, and the write fault discards
		//the blocks of the page. The address handed over is the one inside the emulator's mapping of the guest
		//address space, which starts at virt_ram_base
		if (page_locked[(addr & RAM_MASK) / REI_PAGE_SIZE])
			bm_LockedWrite((u8*)((uintptr_t)virt_ram_base + addr));

		memcpy(&ram[addr & RAM_MASK], &data, size);
		return;
	}

	if ((addr & 0x1FFFFFFC) == DEVICE)
	{
		ResetInterruptPend(sh4_IRL_9);
		return;
	}

	counters.stray++;
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
	printf("sh4bp: unaligned %dbit %s at 0x%08X, pc %08X\n", sz * 8, write ? "write" : "read", addr, next_pc);
	fatal("unaligned guest access");
}

u8* machine_ram(u32 addr)
{
	if (!is_ram(addr))
		fatal("machine_ram: not a RAM address");

	return &ram[addr & RAM_MASK];
}

u16 machine_read16(u32 addr) { return (u16)bus_read(2, addr); }
u32 machine_read32(u32 addr) { return bus_read(4, addr); }
void machine_write16(u32 addr, u16 data) { bus_write(2, addr, data); }
void machine_write32(u32 addr, u32 data) { bus_write(4, addr, data); }

bool machine_page_locked(u32 addr)
{
	return page_locked[(addr & RAM_MASK) / REI_PAGE_SIZE];
}

//dbg::mem, as far as dbg::sh4 reads opcodes through it: main memory
namespace dbg { namespace mem {

u32 Read(Target t, u32 addr, u32 len, u8* out, u8* readableBits)
{
	u32 readable = 0;

	if (readableBits)
		memset(readableBits, 0, (len + 7) / 8);

	for (u32 i = 0; i < len; i++)
	{
		u32 a = addr + i;

		out[i] = 0;

		if (t == Target::Sh4 && a < 0xE0000000 && ((a >> 26) & 7) == 3)
		{
			out[i] = ram[a & RAM_MASK];
			readable++;

			if (readableBits)
				readableBits[i >> 3] |= (u8)(1 << (i & 7));
		}
	}

	return readable;
}

} }


/*
	Time, and the stop latch as hw/sh4/SuperH4_impl.cpp has it
*/
extern "C" int UpdateSystem()
{
	if (unlikely(dbg::g_stop_latched))
		return 1;

	//the scheduler tick
	sched_now += SH4_TIMESLICE;
	counters.slices++;

	if (knobs.irq_period && counters.slices % knobs.irq_period == 0)
	{
		SetInterruptPend(sh4_IRL_9);
		counters.irqs++;
	}

	//The host stop is made at a slice end of the backend loop: the loop ends in this call, with nothing of
	//the guest run behind it, which makes the state at the stop a point that two runs can be compared at
	if (knobs.host_stop_slice && counters.slices >= knobs.host_stop_slice && knobs.host_stop_slice != host_stop_made
		&& !dbg::g_in_sleep && !in_step)
	{
		host_stop_made = knobs.host_stop_slice;
		host_stopped = true;
		sh4_int_bCpuRun = false;
	}

	//The slice hook. A pause request and a call for the park context are taken at a slice end of the backend
	//loop, not inside a SLEEP wait, and not while a step executes
	if ((knobs.pause_every && counters.slices % knobs.pause_every == 0) || ext_pause.exchange(false))
		pause_wanted = true;
	if ((knobs.excursion_every && counters.slices % knobs.excursion_every == 0) || ext_call_pending.load())
		excursion_wanted = true;

	if ((pause_wanted || excursion_wanted) && !dbg::g_in_sleep && !in_step && !host_stopped)
	{
		if (pause_wanted)
		{
			dbg::emu::LatchStop(dbg::StopCause::Pause, 0);
		}
		else
		{
			//a latch without a cause: Run() returns for a service excursion
			dbg::g_stop_latched = true;
			dbg::g_parked = true;
			sh4_int_bCpuRun = false;
		}
	}

	return Sh4cntx.interrupt_pend | (sh4_int_bCpuRun == false);
}

extern "C" int UpdateSystem_INTC()
{
	if (UpdateSystem())
		return UpdateINTC();
	else
		return 0;
}

namespace dbg {

std::atomic<u32> g_attn(0);
bool g_stop_latched;
bool g_intc_deferred;
bool g_in_sleep;
bool g_sleep_cut;
std::atomic<bool> g_parked(false);

namespace emu {

static int rank(StopCause cause)
{
	switch (cause)
	{
	case StopCause::Breakpoint: return 4;
	case StopCause::Event:      return 3;
	case StopCause::Step:       return 2;
	default:                    return 1;
	}
}

void LatchStop(StopCause cause, u32 breakpointId)
{
	if (!latch_has_cause || rank(cause) > rank(latch_cause) || (cause == latch_cause && breakpointId < latch_id))
	{
		latch_has_cause = true;
		latch_cause = cause;
		latch_id = breakpointId;
	}

	g_stop_latched = true;
	g_parked = true;
	sh4_int_bCpuRun = false;
}

bool OnEmuThread()
{
	return true;
}

} }


/*
	The park context
*/

//Leaving it, the part every way out shares: the latch goes, and the interrupt acceptance it deferred is made.
//true: that accepted an interrupt, the pc is at the vector
static bool park_leave()
{
	//a machine that the host stopped is started again by the host
	host_stopped = false;

	dbg::g_stop_latched = false;

	bool accepted = false;

	if (dbg::g_intc_deferred)
	{
		dbg::g_intc_deferred = false;

		//only an event or a sound breakpoint cuts a SLEEP wait, and this machine has neither
		if (dbg::g_sleep_cut)
			fatal("a SLEEP wait was cut");

		accepted = UpdateINTC() != 0;
	}

	sh4_host_fp_resync();

	return accepted;
}

//Entering it for a stop
static MachineStop park_enter(bool step)
{
	dbg::sh4::StopInfo info;

	dbg::sh4::OnStop(&info);

	MachineStop stop;

	stop.debugger = true;
	stop.cause = latch_has_cause ? latch_cause : dbg::StopCause::Step;
	stop.id = latch_has_cause ? latch_id : 0;
	stop.pc = next_pc;

	if (!latch_has_cause && !step)
		fatal("a stop without a cause");

	//what dbg::sh4 says stopped is what its helpers told LatchStop
	if (info.cause != dbg::StopCause::NoCause && (info.cause != stop.cause || info.breakpointId != stop.id))
		fatal("dbg::sh4::OnStop does not repeat what was latched");
	if (info.cause == dbg::StopCause::NoCause && (stop.cause == dbg::StopCause::Breakpoint))
		fatal("a breakpoint stop that dbg::sh4::OnStop does not know");

	latch_has_cause = false;

	//a step ends as a stop: latched, the cpu not running
	dbg::g_stop_latched = true;
	dbg::g_parked = true;
	sh4_int_bCpuRun = false;

	counters.stops++;
	if (stop.cause == dbg::StopCause::Pause)
	{
		counters.pauses++;
		pause_wanted = false;
	}

	return stop;
}

//The call of another thread, if one waits
static void run_ext_call()
{
	std::function<void()> fn;

	{
		std::lock_guard<std::mutex> lock(ext_mutex);
		fn.swap(ext_call);
	}

	if (fn)
	{
		fn();

		std::lock_guard<std::mutex> lock(ext_mutex);
		ext_call_pending = false;
		ext_done.notify_all();
	}
}

void machine_request_pause()
{
	ext_pause = true;
}

void machine_call(std::function<void()> fn)
{
	std::unique_lock<std::mutex> lock(ext_mutex);

	ext_call = fn;
	ext_call_pending = true;

	//the machine takes it at its next slice end, or when it is entered next
	ext_done.wait(lock, [] { return !ext_call_pending.load(); });
}

SuperH4Backend* machine_backend()
{
	return backend;
}

//SuperH4_impl::Run(), called again after every service excursion, until a stop
static MachineStop run()
{
	//a call that arrived while the machine was parked
	run_ext_call();

	for (;;)
	{
		used_fallback = engine == ENGINE_FALLBACK && dbg::sh4::FallbackWanted();

		if (used_fallback)
			sh4_fallback_loop();
		else
			backend->Loop();

		if (host_stopped)
		{
			//the host stop wins: the park context is left, the temporary state of the debugger goes
			park_leave();
			host_stopped = true;
			dbg::sh4::OnStop(nullptr);
			latch_has_cause = false;

			MachineStop stop;

			stop.debugger = false;
			stop.cause = dbg::StopCause::HostStopped;
			stop.id = 0;
			stop.pc = next_pc;

			return stop;
		}

		if (!dbg::g_stop_latched)
			fatal("the backend loop returned without a stop");

		if (latch_has_cause)
			return park_enter(false);

		//a service excursion: the call runs, nothing is published, the guest goes on
		counters.excursions++;
		excursion_wanted = false;

		if (knobs.excursion)
			knobs.excursion();

		run_ext_call();

		park_leave();

		sh4_int_bCpuRun = true;
		dbg::g_parked = false;
	}
}

static MachineStop single_step()
{
	in_step = true;

	if (engine == ENGINE_FALLBACK)
	{
		used_fallback = true;
		sh4_fallback_step();
	}
	else
	{
		used_fallback = false;

		if (!backend->Step())
			fatal("the backend cannot step");
	}

	in_step = false;

	return park_enter(true);
}

static bool step_refused(int* error, const char** why)
{
	const char* reason = nullptr;
	int rv = dbg::sh4::StepCheck(&reason);

	if (error)
		*error = rv;
	if (why)
		*why = reason;

	if (rv != 0 && error == nullptr)
		fatal("a step was refused and the test does not expect it");

	return rv != 0;
}

static MachineStop refused()
{
	MachineStop stop;

	stop.debugger = true;
	stop.cause = dbg::StopCause::NoCause;
	stop.id = 0;
	stop.pc = next_pc;

	return stop;
}

MachineStop machine_resume()
{
	park_leave();

	dbg::sh4::PrepareResume();

	sh4_int_bCpuRun = true;
	dbg::g_parked = false;

	return run();
}

MachineStop machine_step(int* error, const char** why)
{
	if (step_refused(error, why))
		return refused();

	bool accepted = park_leave();

	dbg::sh4::PrepareStep();

	sh4_int_bCpuRun = true;

	//a step whose deferred interrupt acceptance accepted is complete at the vector
	if (accepted)
		return park_enter(true);

	return single_step();
}

MachineStop machine_step_over(int* error, const char** why)
{
	if (step_refused(error, why))
		return refused();

	bool accepted = park_leave();

	if (accepted)
	{
		dbg::sh4::PrepareStep();
		return park_enter(true);
	}

	if (dbg::sh4::PrepareStepOver())
	{
		dbg::sh4::PrepareResume();

		sh4_int_bCpuRun = true;
		dbg::g_parked = false;

		return run();
	}

	dbg::sh4::PrepareStep();

	sh4_int_bCpuRun = true;

	return single_step();
}

MachineStop machine_step_out(int* error, const char** why)
{
	if (step_refused(error, why))
		return refused();

	bool accepted = park_leave();

	if (accepted)
	{
		dbg::sh4::PrepareStep();
		return park_enter(true);
	}

	dbg::sh4::PrepareStepOut();
	dbg::sh4::PrepareResume();

	sh4_int_bCpuRun = true;
	dbg::g_parked = false;

	return run();
}

bool machine_used_fallback()
{
	return used_fallback;
}

void machine_breakpoints(const u32* ids, const u32* addrs, u32 n)
{
	dbg::sh4::SetBreakpoints(ids, addrs, n);
}


/*
	Setup
*/
static void* alloc_pages_zeroed(size_t size)
{
#if defined(_WIN32)
	return VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	void* rv = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return rv == MAP_FAILED ? 0 : rv;
#endif
}

void machine_init()
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

	static MachineSh4 cpu;

	cpu.sh4mmr.reset(new MachineMmr());
	cpu.mram.SetRegion(ram, RAM_SIZE);
	sh4_cpu = &cpu;

	_vmem_reset();
	_vmem_unaligned_handler = &unaligned_access;
	_vmem_map_handler(_vmem_register_handler(&handler_ctx, read8, read16, read32, write8, write16, write32), 0x00, 0xFF);

	interrupts_init(cpu.sh4mmr.get());

	backends[0] = Get_Sh4Recompiler();
	backends[1] = Get_Sh4Interpreter();

	if (!backends[0]->Init() || !backends[1]->Init())
		fatal("backend init");

	backend = backends[0];
}

Engine machine_engine()
{
	return engine;
}

void machine_reset(Engine e)
{
	engine = e;
	backend = e == ENGINE_INTERP ? backends[1] : backends[0];

	//SuperH4_impl::Reset
	next_pc = 0xA0000000;

	memset(r, 0, sizeof(r));
	memset(r_bank, 0, sizeof(r_bank));
	memset(Sh4cntx.xffr, 0, sizeof(Sh4cntx.xffr));

	gbr = ssr = spc = sgr = dbr = vbr = 0;
	mac.full = pr = fpul = 0;
	Sh4cntx.jdyn = 0;

	sh4_sr_SetFull(0x700000F0);
	old_sr.status = sr.status;
	UpdateSR();

	fpscr.full = 0x00040001;
	old_fpscr = fpscr;
	UpdateFPSCR();

	sh4_int_bCpuRun = false;
	Sh4cntx.sh4_sched_next = 0;

	interrupts_reset();

	//the code cache of a new emulator thread
	LastAddr = LastAddr_min;
	bm_Reset();
	memset(page_locked, 0, sizeof(page_locked));

	backends[0]->ResetSlice();
	backends[1]->ResetSlice();

	//the guest
	memset(ram, 0, HASHED_RAM);
	memcpy(machine_ram(G_BASE), guest_image, sizeof(guest_image));

	sh4_sr_SetFull(0x400000F0);
	UpdateSR();

	vbr = G_vbr;
	r[15] = STACK_TOP;

	//the machine, and the debugger as a new session has it
	knobs = MachineKnobs();
	counters = MachineCounters();

	sched_now = 0;
	host_stopped = false;
	host_stop_made = 0;
	pause_wanted = excursion_wanted = in_step = false;
	latch_has_cause = false;
	used_fallback = false;

	dbg::g_stop_latched = true;
	dbg::g_intc_deferred = false;
	dbg::g_in_sleep = false;
	dbg::g_sleep_cut = false;
	dbg::g_parked = true;

	dbg::sh4::SetBreakpoints(nullptr, nullptr, 0);
	dbg::sh4::SetActiveMask(~0u);
	dbg::sh4::OnPcWritten();
	dbg::sh4::OnStop(nullptr);
}

void machine_set_pc(u32 pc)
{
	next_pc = pc;
	dbg::sh4::OnPcWritten();
}

void machine_set_sr(u32 value)
{
	sh4_sr_SetFull(value);

	if (UpdateSR())
		UpdateINTC();
}

static u64 fnv(u64 h, const void* data, size_t bytes)
{
	const u8* p = (const u8*)data;

	for (size_t i = 0; i < bytes; i++)
		h = (h ^ p[i]) * 0x100000001B3ull;

	return h;
}

MachineState machine_state()
{
	MachineState s;

	s.sched = sched_now;
	s.counter = backend->SliceCyclesLeft();
	s.pc = next_pc;

	u64 h = 0xCBF29CE484222325ull;

	h = fnv(h, Sh4cntx.xffr, sizeof(Sh4cntx.xffr));
	h = fnv(h, r, sizeof(r));
	h = fnv(h, &mac, sizeof(mac));
	h = fnv(h, r_bank, sizeof(r_bank));

	u32 misc[] = { gbr, ssr, spc, sgr, dbr, vbr, pr, fpul, next_pc, sr.status, sr.T, fpscr.full, Sh4cntx.interrupt_pend };

	s.ctx = fnv(h, misc, sizeof(misc));
	s.ram = fnv(0xCBF29CE484222325ull, ram, HASHED_RAM);

	return s;
}
