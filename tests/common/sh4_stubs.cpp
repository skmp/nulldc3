/*
	What the SH4 core translation units (NULLDC_SH4_INTERP_SOURCES and the recompiler on top of them) reference
	from the parts of the emulator that the tests leave out. There is no system behind the CPU: no scheduler, no
	interrupts, no TA, no reios, no MMU and no host memory mapping.

	Symbols that a recompiler build takes from its own translation units are in sh4_stubs_norec.cpp, the ones only
	a recompiler build needs in sh4_stubs_rec.cpp, the ones hw/sh4/SuperH4_impl.cpp has in sh4_stubs_nocpu.cpp.
*/
#include "types.h"

#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mem.h"
#include "hw/sh4/modules/ccn.h"
#include "hw/sh4/modules/mmu.h"
#include "hw/mem/_vmem.h"
#include "hw/pvr/pvr_mem.h"
#include "reios/reios.h"

settings_t settings;

//reios
void DYNACALL reios_trap(u32 op) { }

//store queues, TA
extern "C" void DYNACALL TAWriteSQ(u32 address, u8* sqb) { }
void TAWrite(u32 address, u32* data, u32 count, u8* vram) { }
void WriteMemBlock_nommu_sq(u32 dst, u32* src) { }

u32 CCN_QACR_TR[2];
u32 sq_remap[64];

//operand cache as ram (see sh4_mem.cpp)
u8 OIX_CACHE[256][32];
u32 OIX_ADDR[256];
bool OIX_DIRTY[256];

//mmu
TLB_Entry UTLB[64];
bool UTLB_Sync(u32 entry) { return true; }

//host memory
void VLockedMemory::LockRegion(unsigned offset, unsigned size_bytes) { }
void VLockedMemory::UnLockRegion(unsigned offset, unsigned size_bytes) { }

VMemType vmem_platform_init(void** vmem_base_addr, void** sh4rcb_addr) { return MemTypeError; }
void vmem_platform_reset_mem(void* ptr, unsigned size_bytes) { }
void vmem_platform_ondemand_page(void* address, unsigned size_bytes) { }
void vmem_platform_create_mappings(const vmem_mapping* vmem_maps, unsigned nummaps) { }
void vmem_platform_destroy() { }
