/*
	This file is part of libswirl
*/
#include "license/bsd"

#include <windows.h>

#include "types.h"
#include "win_jit_unwind.h"

/*
	The code region of the recompiler and its unwind data. The region is found near the program, so that generated
	code can address the program's data relative to rip (win_vmem.cpp, vmem_platform_prepare_jit_block). One page
	behind it holds the unwind data of the frames of generated code (UNWIND_INFO, at offsets from the start of the
	region as the system wants them), and a function table callback over the region (RtlInstallFunctionTableCallback,
	installed as soon as the region is there) hands the system the frame of the code at a pc when it walks a stack
	through generated code:
	- the main loop and RunBlock (jit/backend/x64/rec_x64.cpp, mainloop_prologue): eight pushes, the frame, ten xmm
	  registers stored in it; published by win_jit_unwind_frame when the recompiler writes them, each over its own
	  range;
	- everything else, the blocks and the memory trampolines: the 40 bytes of a block's frame (STACK_ALIGN of
	  rec_x64.cpp) below the return address.
	The fatal handler (win_fatal.cpp) also asks whether a fault is in this region.
*/
static u8* code_region = NULL;
static size_t code_region_size = 0;

#define UWOP_PUSH_NONVOL     0
#define UWOP_ALLOC_LARGE     1
#define UWOP_ALLOC_SMALL     2
#define UWOP_SAVE_XMM128     8

#pragma pack(push, 1)
struct WinUnwindInfo
{
	u8 version_flags;	// Version 1, Flags 0
	u8 size_of_prolog;
	u8 count_of_codes;
	u8 frame_register;	// none
	u16 codes[32];
};
#pragma pack(pop)

static_assert(sizeof(WinUnwindInfo) % 4 == 0, "UNWIND_INFO is DWORD aligned");

// In the page behind the region: [0] a block's frame, [1] the main loop's, [2] RunBlock's
static WinUnwindInfo* unwind_infos;
static RUNTIME_FUNCTION unwind_functions[3];

static u16 unwind_code(u8 offset, u8 op, u8 info)
{
	return (u16)(offset | (op << 8) | (info << 12));
}

void win_jit_unwind_frame(int which, const void* begin, size_t size, const u8* prolog, u32 frame)
{
	if (!code_region || (u8*)begin < code_region || (u8*)begin + size > code_region + code_region_size)
		return;
	verify(which == 0 || which == 1);
	verify(frame == 32 + 10 * 16 + 8 && prolog[18] != 0);

	WinUnwindInfo& u = unwind_infos[1 + which];
	memset(&u, 0, sizeof(u));
	u.version_flags = 1;
	u.size_of_prolog = prolog[18];
	int n = 0;
	// the codes in the reverse order of the prologue
	for (int i = 9; i >= 0; i--) {
		// movaps [rsp + 32 + 16 * i], xmm(6 + i)
		u.codes[n++] = unwind_code(prolog[9 + i], UWOP_SAVE_XMM128, (u8)(6 + i));
		u.codes[n++] = (u16)((32 + 16 * i) / 16);
	}
	// sub rsp, frame
	u.codes[n++] = unwind_code(prolog[8], UWOP_ALLOC_LARGE, 0);
	u.codes[n++] = (u16)(frame / 8);
	// push rbx, rbp, rdi, rsi, r12, r13, r14, r15
	static const u8 pushed[8] = { 3, 5, 7, 6, 12, 13, 14, 15 };
	for (int i = 7; i >= 0; i--)
		u.codes[n++] = unwind_code(prolog[i], UWOP_PUSH_NONVOL, pushed[i]);
	u.count_of_codes = (u8)n;

	RUNTIME_FUNCTION& f = unwind_functions[1 + which];
	f.BeginAddress = (DWORD)((u8*)begin - code_region);
	f.EndAddress = (DWORD)((u8*)begin + size - code_region);
	f.UnwindData = (DWORD)((u8*)&u - code_region);
}

static PRUNTIME_FUNCTION CALLBACK code_region_function(DWORD64 pc, PVOID)
{
	DWORD offset = (DWORD)(pc - (DWORD64)code_region);
	for (int i = 1; i <= 2; i++)
		if (unwind_functions[i].EndAddress && offset >= unwind_functions[i].BeginAddress && offset < unwind_functions[i].EndAddress)
			return &unwind_functions[i];
	return &unwind_functions[0];
}

void win_jit_code_region_prepared(void* region, size_t size)
{
	code_region = (u8*)region;
	code_region_size = size;

	// a block: sub rsp, 40 in front of what the frame holds
	unwind_infos = (WinUnwindInfo*)(code_region + size);
	memset(unwind_infos, 0, 3 * sizeof(WinUnwindInfo));
	unwind_infos[0].version_flags = 1;
	unwind_infos[0].count_of_codes = 1;
	unwind_infos[0].codes[0] = unwind_code(0, UWOP_ALLOC_SMALL, (40 - 8) / 8);
	unwind_functions[0].BeginAddress = 0;
	unwind_functions[0].EndAddress = (DWORD)size;
	unwind_functions[0].UnwindData = (DWORD)size;

	verify(RtlInstallFunctionTableCallback((DWORD64)code_region | 3, (DWORD64)code_region, (DWORD)size,
	                                       code_region_function, NULL, NULL));
}

bool win_in_code_region(const void* address)
{
	return code_region && (const u8*)address >= code_region && (const u8*)address < code_region + code_region_size;
}
