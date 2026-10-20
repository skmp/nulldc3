/*
	This file is part of libswirl
*/
#include "license/bsd"



#include <windows.h>

#include <algorithm>
#include <vector>

#include "hw/mem/_vmem.h"
#include "hw/sh4/dyna/ngen.h"
#include "win_jit_unwind.h"

// Implementation of the vmem related function for Windows platforms.
// For now this probably does some assumptions on the CPU/platform.

// This implements the VLockedMemory interface, as defined in _vmem.h
// The implementation allows it to be empty (that is, to not lock memory).

void VLockedMemory::LockRegion(unsigned offset, unsigned size) {
	#ifndef TARGET_NO_EXCEPTIONS
	//verify(offset + size < this->size && size != 0);
	DWORD old;
	VirtualProtect(&data[offset], size, PAGE_READONLY, &old);
	#endif
}

void VLockedMemory::UnLockRegion(unsigned offset, unsigned size) {
	#ifndef TARGET_NO_EXCEPTIONS
	//verify(offset + size <= this->size && size != 0);
	DWORD old;
	VirtualProtect(&data[offset], size, PAGE_READWRITE, &old);
	#endif
}

/*
	The guest address space is one range of the process's address space: the SH4 context and the block table
	(Sh4RCB) in front, the 512 MB of the guest behind it, the ARAM's room at its end. It is reserved as a single
	placeholder (VirtualAlloc2, MEM_RESERVE_PLACEHOLDER) and then cut into the pieces of the guest's map, front to
	back, each split off the placeholder that is left (VirtualFree, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) and put in
	its place: a view of the memory file (MapViewOfFile3, MEM_REPLACE_PLACEHOLDER) or plain reserved memory
	(VirtualAlloc2, MEM_REPLACE_PLACEHOLDER). No part of the range is ever free, so no allocation of another thread
	(the debugger's are running by then) can land in it while the map is made. Windows 10 1803 or later: both
	functions are looked up at run time, and without them nvmem is off (the fallback of _vmem.cpp).
*/
typedef PVOID(WINAPI* VirtualAlloc2Fn)(HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);
typedef PVOID(WINAPI* MapViewOfFile3Fn)(HANDLE, HANDLE, PVOID, ULONG64, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);

static VirtualAlloc2Fn virtual_alloc2;
static MapViewOfFile3Fn map_view_of_file3;

static HANDLE mem_handle = INVALID_HANDLE_VALUE, mem_handle2 = INVALID_HANDLE_VALUE;
static char * base_alloc = NULL;
static size_t base_size = 0;

// What the range is made of once the map is made: views and reservations, for vmem_platform_destroy
struct VmemPiece
{
	char* start;
	size_t size;
	bool view;
};
static std::vector<VmemPiece> pieces;
static bool mapped = false;

static bool find_placeholder_functions()
{
	HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
	if (!kernelbase)
		return false;
	virtual_alloc2 = (VirtualAlloc2Fn)GetProcAddress(kernelbase, "VirtualAlloc2");
	map_view_of_file3 = (MapViewOfFile3Fn)GetProcAddress(kernelbase, "MapViewOfFile3");
	return virtual_alloc2 && map_view_of_file3;
}

// Implement vmem initialization for RAM, ARAM, VRAM and SH4 context, fpcb etc.
// The function supports allocating 512MB or 4GB addr spaces.

// Plase read the POSIX implementation for more information. On Windows this is
// rather straightforward.
VMemType vmem_platform_init(void **vmem_base_addr, void **sh4rcb_addr) {
	if (!find_placeholder_functions()) {
		printf("Info: VirtualAlloc2 / MapViewOfFile3 not found (Windows 10 1803 or later has them)\n");
		return MemTypeError;
	}

	// The memory file first
	mem_handle = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, RAM_SIZE_MAX + VRAM_SIZE_MAX + ARAM_SIZE_MAX, 0);
	if (mem_handle == NULL) {
		mem_handle = INVALID_HANDLE_VALUE;
		return MemTypeError;
	}

	// Then the address space, one placeholder (it will be 64KB aligned on windows)
	base_size = 512 * 1024 * 1024 + sizeof(Sh4RCB) + ARAM_SIZE_MAX;
	base_alloc = (char*)virtual_alloc2(GetCurrentProcess(), NULL, base_size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
	                                   PAGE_NOACCESS, NULL, 0);
	if (base_alloc == NULL) {
		CloseHandle(mem_handle);
		mem_handle = INVALID_HANDLE_VALUE;
		return MemTypeError;
	}
	mapped = false;
	pieces.clear();

	// Calculate pointers now
	*sh4rcb_addr = &base_alloc[0];
	*vmem_base_addr = &base_alloc[sizeof(Sh4RCB)];

	return MemType512MB;
}

// Just tries to wipe as much as possible in the relevant area.
void vmem_platform_destroy() {
	if (!mapped) {
		if (base_alloc)
			VirtualFree(base_alloc, 0, MEM_RELEASE);
	}
	else {
		for (const VmemPiece& p : pieces) {
			if (p.view)
				UnmapViewOfFile(p.start);
			else
				VirtualFree(p.start, 0, MEM_RELEASE);
		}
	}
	pieces.clear();
	mapped = false;
	base_alloc = NULL;
	if (mem_handle != INVALID_HANDLE_VALUE)
		CloseHandle(mem_handle);
	mem_handle = INVALID_HANDLE_VALUE;
}

// Resets a chunk of memory by deleting its data and setting its protection back.
void vmem_platform_reset_mem(void *ptr, unsigned size_bytes) {
	VirtualFree(ptr, size_bytes, MEM_DECOMMIT);
}

// Allocates a bunch of memory (page aligned and page-sized)
void vmem_platform_ondemand_page(void *address, unsigned size_bytes) {
	verify(NULL != VirtualAlloc(address, size_bytes, MEM_COMMIT, PAGE_READWRITE));
}

/// Creates mappings to the underlying file including mirroring sections
void vmem_platform_create_mappings(const vmem_mapping *vmem_maps, unsigned nummaps) {
	verify(!mapped && base_alloc != NULL);

	// What each piece of the range becomes: a view of the file at an offset, or reserved memory
	struct Want
	{
		char* start;
		size_t size;
		bool view;
		u64 offset;
		bool writes;
	};
	std::vector<Want> want;

	// The SH4 context block: reserved, the context part committed (the block table is paged in on demand)
	want.push_back({ base_alloc, sizeof(Sh4RCB), false, 0, false });

	for (unsigned i = 0; i < nummaps; i++) {
		unsigned address_range_size = vmem_maps[i].end_address - vmem_maps[i].start_address;

		if (!vmem_maps[i].memsize) {
			// Unmapped stuff goes with a protected area or memory. Prevent anything from allocating here
			want.push_back({ (char*)&virt_ram_base[vmem_maps[i].start_address], address_range_size, false, 0, false });
		}
		else {
			// Calculate the number of mirrors
			unsigned num_mirrors = (address_range_size) / vmem_maps[i].memsize;
			verify((address_range_size % vmem_maps[i].memsize) == 0 && num_mirrors >= 1);

			// The views one by one
			for (unsigned j = 0; j < num_mirrors; j++) {
				unsigned offset = vmem_maps[i].start_address + j * vmem_maps[i].memsize;
				want.push_back({ (char*)&virt_ram_base[offset], vmem_maps[i].memsize, true, vmem_maps[i].memoffset,
				                 vmem_maps[i].allow_writes });
			}
		}
	}

	// Front to back; what the map leaves out (the room of the ARAM behind its size) stays reserved
	std::sort(want.begin(), want.end(), [](const Want& a, const Want& b) { return a.start < b.start; });
	std::vector<Want> all;
	char* at = base_alloc;
	for (const Want& w : want) {
		verify(w.start >= at);
		if (w.start > at)
			all.push_back({ at, (size_t)(w.start - at), false, 0, false });
		all.push_back(w);
		at = w.start + w.size;
	}
	verify(at <= base_alloc + base_size);
	if (at < base_alloc + base_size)
		all.push_back({ at, (size_t)(base_alloc + base_size - at), false, 0, false });

	for (size_t k = 0; k < all.size(); k++) {
		const Want& w = all[k];
		// split the piece off the front of the placeholder that is left, unless it is all of it
		if (k + 1 < all.size())
			verify(VirtualFree(w.start, w.size, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER));

		void* ptr;
		if (w.view) {
			ptr = map_view_of_file3(mem_handle, GetCurrentProcess(), w.start, w.offset, w.size, MEM_REPLACE_PLACEHOLDER,
			                        w.writes ? PAGE_READWRITE : PAGE_READONLY, NULL, 0);
		}
		else {
			ptr = virtual_alloc2(GetCurrentProcess(), w.start, w.size, MEM_RESERVE | MEM_REPLACE_PLACEHOLDER, PAGE_NOACCESS,
			                     NULL, 0);
		}
		verify(ptr == w.start);
		pieces.push_back({ w.start, w.size, w.view });
	}
	mapped = true;

	void *cntx_ptr = VirtualAlloc((u8*)p_sh4rcb + sizeof(p_sh4rcb->fpcb), sizeof(Sh4RCB) - sizeof(p_sh4rcb->fpcb), MEM_COMMIT, PAGE_READWRITE);
	verify(cntx_ptr == (u8*)p_sh4rcb + sizeof(p_sh4rcb->fpcb));
}

// The page behind the code region that holds its unwind data
#define UNWIND_PAGE 4096

typedef void* (*mapper_fn) (void *addr, unsigned size);

// This is a tempalted function since it's used twice
static void* vmem_platform_prepare_jit_block_template(void *code_area, unsigned size, mapper_fn mapper) {
	// Several issues on Windows: can't protect arbitrary pages due to (I guess) the way
	// kernel tracks mappings, so only stuff that has been allocated with VirtualAlloc can be
	// protected (the entire allocation IIUC).

	// Strategy: ignore code_area and allocate a new one. Protect it properly.
	// More issues: the area should be "close" to the .text stuff so that code gen works.
	// Remember that on x64 we have 4 byte jump/load offset immediates, no issues on x86 :D

	// Take this function addr as reference.
	uintptr_t base_addr = reinterpret_cast<uintptr_t>(&vmem_platform_init) & ~0xFFFFF;

	// Probably safe to assume the emulator's code is <200MB (today seems to be <16MB on every platform I've seen).
	for (unsigned i = 0; i < 1800*1024*1024; i += 8*1024*1024) {  // Some arbitrary step size.
		uintptr_t try_addr_above = base_addr + i;
		uintptr_t try_addr_below = base_addr - i;

		// We need to make sure there's no address wrap around the end of the addrspace (meaning: int overflow).
		if (try_addr_above > base_addr) {
			void *ptr = mapper((void*)try_addr_above, size);
			if (ptr)
				return ptr;
		}
		if (try_addr_below < base_addr) {
			void *ptr = mapper((void*)try_addr_below, size);
			if (ptr)
				return ptr;
		}
	}
	return NULL;
}

static void* mem_alloc(void *addr, unsigned size) {
	return VirtualAlloc(addr, size + UNWIND_PAGE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

// Prepares the code region for JIT operations, thus marking it as RWX
bool vmem_platform_prepare_jit_block(void *code_area, unsigned size, void **code_area_rwx) {
	// Get the RWX page close to the code_area
	void *ptr = vmem_platform_prepare_jit_block_template(code_area, size, &mem_alloc);
	if (!ptr)
		return false;

	*code_area_rwx = ptr;
	printf("Found code area at %p, not too far away from %p\n", *code_area_rwx, &vmem_platform_init);
	win_jit_code_region_prepared(ptr, size);

	// We should have found some area in the addrspace, after all size is ~tens of megabytes.
	// Pages are already RWX, all done
	return true;
}


static void* mem_file_map(void *addr, unsigned size) {
	// Maps the entire file at the specified addr.
	void *ptr = VirtualAlloc(addr, size, MEM_RESERVE, PAGE_NOACCESS);
	if (!ptr)
		return NULL;
	VirtualFree(ptr, 0, MEM_RELEASE);
	if (ptr != addr)
		return NULL;

	return MapViewOfFileEx(mem_handle2, FILE_MAP_READ | FILE_MAP_EXECUTE, 0, 0, size, addr);
}

// Use two addr spaces: need to remap something twice, therefore use CreateFileMapping()
bool vmem_platform_prepare_jit_block(void *code_area, unsigned size, void **code_area_rw, uintptr_t *rx_offset) {
	mem_handle2 = CreateFileMapping(INVALID_HANDLE_VALUE, 0, PAGE_EXECUTE_READWRITE, 0, size, 0);

	// Get the RX page close to the code_area
	void *ptr_rx = vmem_platform_prepare_jit_block_template(code_area, size, &mem_file_map);
	if (!ptr_rx)
		return false;

	// Ok now we just remap the RW segment at any position (dont' care).
	void *ptr_rw = MapViewOfFileEx(mem_handle2, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size, NULL);

	*code_area_rw = ptr_rw;
	*rx_offset = (char*)ptr_rx - (char*)ptr_rw;
	printf("Info: Using NO_RWX mode, rx ptr: %p, rw ptr: %p, offset: %lu\n", ptr_rx, ptr_rw, (unsigned long)*rx_offset);

	return (ptr_rw != NULL);
}
