/*
	What the SH4 core and the recompiler reference from the parts of the emulator that are not in this machine:
	no reios, no TA, no store queue targets, no MMU, no host mapping of guest memory. machine.cpp has the parts
	that do something here (time, memory, the page locks).
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
#include <windows.h>
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
//of the region executable where they are, as the emulator does on Linux; the emulator on Windows maps a new
//region near the program for every call and frees none
u32 jit_block_prepares;

bool vmem_platform_prepare_jit_block(void* code_area, unsigned size, void** code_area_rwx)
{
	jit_block_prepares++;

#if defined(_WIN32)
	DWORD old;
	if (!VirtualProtect(code_area, size, PAGE_EXECUTE_READWRITE, &old))
		return false;
#else
	if (mprotect(code_area, size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
		return false;
#endif

	*code_area_rwx = code_area;
	return true;
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
