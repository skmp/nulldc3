/*
	See sh4_stubs.cpp. What the recompiler (hw/sh4/dyna and a jit backend) needs on top of it. Guest code is never
	backed by host memory here: blocks are decoded through the memory handlers and are not checked for
	modification.
*/
#include "types.h"

#include "hw/sh4/sh4_mem.h"
#include "hw/mem/_vmem.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

u8* GetMemPtr(u32 Addr, u32 size) { return 0; }
bool IsOnRam(u32 addr) { return false; }

string get_writable_data_path(const string& filename) { return filename; }

bool vmem_platform_prepare_jit_block(void* code_area, unsigned size, void** code_area_rwx)
{
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
