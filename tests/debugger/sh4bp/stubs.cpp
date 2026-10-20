/*
	What the SH4 core and the recompiler reference from the parts of the emulator that are not in this machine:
	no reios, no TA, no store queue targets, no MMU, no host mapping of guest memory. machine.cpp has the parts
	that do something here (time, memory, the page locks).

	On Windows also what the emulator has there: its code region near the program with the unwind data of the
	emulator (libswirl/windows/win_jit_unwind.cpp), so that the system's unwinder walks the stack through the code
	of the recompiler as it does in the emulator (machine.cpp walks it at every stop). The policy for a program that
	dies (no box, no crash report, one line and 128 + the signal's number) is the one of the tests project,
	tests/common/win_policy.cpp, which the programs of this directory link on Windows.
*/
#include "types.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/modules/ccn.h"
#include "hw/sh4/modules/mmu.h"
#include "hw/mem/_vmem.h"
#include "hw/pvr/pvr_mem.h"
#include "reios/reios.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "windows/win_jit_unwind.h"
#else
#include <sys/mman.h>
#endif

settings_t settings;

//reios
void DYNACALL reios_trap(u32 op) { }

//store queues, TA
extern "C" void DYNACALL TAWriteSQ(u32 address, u8* sqb) { }
void TAWrite(u32 address, u32* data, u32 count, u8* vram) { }
void WriteMemBlock_nommu_sq(u32 dst, u32* src) { }

u32 CCN_QACR_TR[2];
u32 sq_remap[64];

//operand cache as ram
u8 OIX_CACHE[256][32];
u32 OIX_ADDR[256];
bool OIX_DIRTY[256];

//mmu
TLB_Entry UTLB[64];
bool UTLB_Sync(u32 entry) { return true; }

//host memory
VMemType vmem_platform_init(void** vmem_base_addr, void** sh4rcb_addr) { return MemTypeError; }
void vmem_platform_reset_mem(void* ptr, unsigned size_bytes) { }
void vmem_platform_ondemand_page(void* address, unsigned size_bytes) { }
void vmem_platform_create_mappings(const vmem_mapping* vmem_maps, unsigned nummaps) { }
void vmem_platform_destroy() { }

//How often the recompiler had the host prepare a code region (the gate threads). Here a call makes the pages
//of the region executable where they are, as the emulator does on Linux. On Windows it maps a region near the
//program with a page behind it for the unwind data, as the emulator does (libswirl/windows/win_vmem.cpp), and
//hands it to the emulator's unwind data (inside the program's image the system would never ask for it: it takes
//the unwind data of a pc in an image from the image)
u32 jit_block_prepares;

bool vmem_platform_prepare_jit_block(void* code_area, unsigned size, void** code_area_rwx)
{
	jit_block_prepares++;

#if defined(_WIN32)
	//within 2 GB of the program, as the code addresses the program's data relative to rip
	uintptr_t base = (uintptr_t)GetModuleHandleW(nullptr) & ~(uintptr_t)0xFFFFF;
	for (uintptr_t step = 8 * 1024 * 1024; step < 1800u * 1024 * 1024; step += 8 * 1024 * 1024)
	{
		for (uintptr_t at : { base + step, base - step })
		{
			void* region = VirtualAlloc((void*)at, size + 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (region)
			{
				win_jit_code_region_prepared(region, size);
				*code_area_rwx = region;
				return true;
			}
		}
	}
	return false;
#else
	if (mprotect(code_area, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
		return false;

	*code_area_rwx = code_area;
	return true;
#endif
}

string get_writable_data_path(const string& filename) { return filename; }

//die() and verify(): the text, then the end of the test run
int msgboxf(const wchar* text, unsigned int type, ...)
{
	va_list args;

	va_start(args, type);
	vprintf(text, args);
	va_end(args);

	printf("\n");
	fflush(stdout);

	return 0;
}

void os_DebugBreak()
{
	printf("sh4bp: the emulator code gave up (die / verify)\n");
	fflush(stdout);
	abort();
}
