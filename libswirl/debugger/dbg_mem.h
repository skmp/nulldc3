/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::mem: guest memory for the debugger (dbg_mem.cpp). Byte granular, any address, any length. It reads
	the backing stores and register arrays directly. It never calls a guest memory handler, and it calls the
	read function of a register for two bytes only (0x00802C00 and 0x00802C01 of the ARM7 target), where that
	function copies a byte and changes nothing: a read changes nothing and cannot spin or end the process,
	whatever the address. There is no address translation: MMU builds are read through the same fixed map.

	Read is called on (E) or by a thread that holds L while no emulator thread exists, Write on (E) only. The
	backing stores are looked up from the machine on every call; no pointer is kept. Video memory and the PVR
	registers are also written by the renderer's threads while a frame is queued: a read can see a frame in
	progress, and a write lands under one as a write by the guest does.

	Target::Sh4. An address below 0xE0000000 is first reduced to its low 29 bits.

	  0x00000000-0x03FFFFFF  area 0, reduced again to its low 25 bits (0x02000000 mirrors 0x00000000)
	    0x00000000-0x001FFFFF  Bios       boot ROM
	    0x00200000-0x0021FFFF  Flash      flash memory
	    0x005F6800-0x005F7CFF  SbRegs     system bus registers, except
	    0x005F7000-0x005F70FF  Unreadable GD-ROM: its reads have side effects
	    0x005F8000-0x005F9FFF  PvrRegs    PVR registers
	    0x00600000-0x006007FF  Unreadable modem
	    0x00700000-0x00707FFF  AicaRegs   AICA registers, as a guest read delivers them
	    0x00710000-0x0071000B  AicaRtc    RTC
	    0x00800000-0x00FFFFFF  AicaRam    wave memory, mirrored by its size
	    everything else        Unreadable
	  0x04000000-0x04FFFFFF, 0x06000000-0x06FFFFFF  Vram64  video memory, 64-bit view, mirrored by its size
	  0x05000000-0x05FFFFFF, 0x07000000-0x07FFFFFF  Vram32  video memory, 32-bit view (byte lanes kept)
	  0x0C000000-0x0FFFFFFF  Ram          main memory, mirrored by its size
	  0x1F000000-0x1FFFFFFF  Sh4Regs      on-chip registers at their area 7 addresses
	  0xE0000000-0xE3FFFFFF  StoreQueue   the two store queues, 64 bytes, mirrored
	  0xFF000000-0xFFFFFFFF  Sh4Regs      on-chip registers at their P4 addresses
	  everything else        Unreadable

	Target::Arm7

	  0x00000000-0x007FFFFF  AicaRam      wave memory, mirrored by its size
	  0x00800000-0x00807FFF  AicaRegs     as a read by the ARM7 delivers them: the register file, with the
	                                      interrupt level L at 0x2D00 (without the acknowledge of that read),
	                                      0 at 0x2D04, and at 0x2C00 / 0x2C01 the two bytes of the register
	                                      file where the SH4 has ARMRST and VREG
	  everything else        Unreadable

	Target::Dsp has no memory: every address is Unreadable.

	A register region delivers the bytes of the register values in little-endian order; a byte that belongs
	to no register, or to one without a side-effect-free reader, is not readable.
*/

namespace dbg { namespace mem {

enum class Region : u8 {
	Unreadable,		// nothing a debugger can read: not mapped, or a device whose reads have side effects
	Bios,
	Flash,
	SbRegs,
	PvrRegs,
	AicaRegs,
	AicaRtc,
	AicaRam,		// writable
	Vram64,			// writable
	Vram32,			// writable
	Ram,			// writable
	StoreQueue,
	Sh4Regs,
};

// The region of one address by the map above. Depends on the address and the build only: any thread, any
// lifetime state. A byte of a register region can still be unreadable.
Region Classify(Target t, u32 addr);

// Reads len bytes at addr into out; addr + i wraps at 2^32. Every byte of out is written: an unreadable
// byte reads as 0. If readableBits is not null it receives one bit per byte, bit (i & 7) of
// readableBits[i >> 3], set when byte i is readable; its (len + 7) / 8 bytes are all written.
// Returns the number of readable bytes; 0 when nothing in the range is readable and when no machine exists.
// len 0 returns 0 and writes nothing.
u32 Read(Target t, u32 addr, u32 len, u8* out, u8* readableBits);

// Writes len bytes at addr. All or nothing: the whole range is checked before the first byte is stored.
// (E) in the park context only (inside a CC_WRITE call: the park loop, or a service excursion). It waits for
// nothing but the lock of the texture cache, which a write to video memory takes as a faulting write by the
// guest does.
// Writable are main memory, video memory (both views) and wave memory (both targets), every mirror of them;
// a range may run from one mirror of a store into the next. No other region is writable, and neither is a
// range that would pass the end of the address space (addr + len > 2^32): a write does not wrap.
//   main memory    in a build with an SH4 recompiler the compiled blocks of every page the range touches are
//                  discarded first. Where the virtual memory layout is in use: bm_LockedWrite per page of
//                  REI_PAGE_SIZE, which also marks the page as data and removes its protection. Where it is
//                  not (virt_ram_base is null): SuperH4Backend::DebugDiscardPage of the active backend per
//                  4 KiB page, and the writer removes the protection of the pages itself
//                  (VLockedMemory::UnLockRegion). A build without a recompiler stores the bytes
//   video memory   VramLockedWrite first, for every page of REI_PAGE_SIZE of the store that the range
//                  touches (for the 32-bit view: the pages of pvr_map32 of each byte): textures locked on the
//                  page are invalidated and its protection goes
//   wave memory    the bytes are stored, then SoundCPU::InvalidateJitCache() once: the ARM7 recompiler has no
//                  detection of self-modifying code
// Breakpoints need nothing: no armed address is a patch in memory. An owed remainder of the SH4 is decoded
// from memory as it is when the park context is left.
// Returns 0. Returns kErrNotWritable (-32013), having stored nothing, when any byte of the range is not
// writable and when no machine exists, or kErrNotSupported (-32004) while the writer does not exist.
int Write(Target t, u32 addr, u32 len, const u8* data);

// One little-endian word; false when any of its bytes is not readable. Same threads as Read().
inline bool Read16(Target t, u32 addr, u16* value) {
	u8 b[2];
	if (Read(t, addr, 2, b, nullptr) != 2)
		return false;
	*value = (u16)(b[0] | (b[1] << 8));
	return true;
}

inline bool Read32(Target t, u32 addr, u32* value) {
	u8 b[4];
	if (Read(t, addr, 4, b, nullptr) != 4)
		return false;
	*value = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
	return true;
}

} }
