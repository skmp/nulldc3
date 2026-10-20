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

#if defined(_WIN32)
// The unwind data of the frames of the main loop (libswirl/windows/win_jit_unwind.cpp keeps it for the emulator's code
// region, jit/backend/x64/rec_x64.cpp hands it over): this code region has none
void win_jit_unwind_frame(int which, const void* begin, size_t size, const u8* prolog, u32 frame)
{
}
#endif
