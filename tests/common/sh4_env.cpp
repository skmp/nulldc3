#include "sh4_env.h"
#include "test_guard.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mmr.h"
#include "hw/mem/_vmem.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

extern SuperH4* sh4_cpu;

struct TestSh4Mmr : SuperH4Mmr
{
	void Reset() { }
	void rio_reg(void* context, RegisterStruct* arr, size_t arr_size, u32 addr, RegIO flags, u32 sz, RegReadAddrFP* rp, RegWriteAddrFP* wp) { }
};

struct TestSh4 : SuperH4
{
	void SetA0Handler(Area0Hanlders slot, MMIODevice* dev) { }
	MMIODevice* GetA0Handler(Area0Hanlders slot) { return 0; }

	bool setBackend(SuperH4Backends backend) { return true; }

	bool Init() { return true; }
	void Reset(bool Manual) { }
	void Term() { }

	void Run() { }
	void Stop() { }
	void Start() { }
	void Step() { }
	void Skip() { }

	bool IsRunning() { return false; }

	void ResetCache() { }
};

//Zeroed and page aligned, as the emulator's own allocation: the recompilers use aligned moves on the context
static void* alloc_pages_zeroed(size_t size)
{
#if defined(_WIN32)
	return VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
	void* rv = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	return rv == MAP_FAILED ? 0 : rv;
#endif
}

static void unaligned_access(u32 addr, u32 sz, bool write)
{
	test_guard_abort("Unaligned %dbit %s at 0x%08X", sz * 8, write ? "write" : "read", addr);
}

void sh4_env_init()
{
	p_sh4rcb = (Sh4RCB*)alloc_pages_zeroed(sizeof(Sh4RCB));
	verify(p_sh4rcb != 0);

	static TestSh4 cpu;
	cpu.sh4mmr.reset(new TestSh4Mmr());
	sh4_cpu = &cpu;

	_vmem_reset();
	_vmem_unaligned_handler = &unaligned_access;
}
