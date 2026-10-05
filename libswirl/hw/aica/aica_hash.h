/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"

/*
	The sound regression harness: a run that folds what the sound side produces into hashes and ends by itself.

	    -config config:Debug.AicaHash=N

	After N AICA ticks (a tick is 32 samples) the emulator prints

	    AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> ticks=<N> cycles=<scheduler time, decimal>

	and ends the process with exit code 0. The hashes are 64-bit FNV-1a:

	  samples  every pair handed to the audio stream, right then left, two bytes each, low byte first
	  regs     the 0x8000 bytes of the AICA register file, at every tick
	  arm7     what the sound CPU writes into a save state (its registers of every mode, the interrupt latches,
	           the reset state), at every tick
	  sh4      PC, R0..R15, PR, SR and FPSCR of the SH4, four bytes each, low byte first, at every tick; only on a
	           backend with SH4DBG_CTX_AT_SLICE (hw/sh4/sh4_if.h), on any other it stays the FNV offset basis

	Two runs of one binary with the same program and settings print the same line when the run does not depend
	on the host: config:Dreamcast.RTC pins the real-time clock, audio:backend=none keeps the audio device out.

	The AICA has the state and the places where it is folded. What needs the rest of the emulator (the
	configuration, the sound CPU, the SH4, the scheduler) is aica_hash.cpp, which enters its functions below when
	it is linked. A build that takes the AICA without the emulator leaves that file out and has no harness.
*/

struct AicaHash
{
	u32 ticks_wanted;	// 0: the harness is off
	u32 ticks;
	u64 samples;
	u64 regs;
	u64 arm7;
	u64 sh4;

	// From aica_hash.cpp; null without it
	void (*configure)();					// when the AICA is initialised: reads config:Debug.AicaHash
	void (*tick)(const u8* aica_reg);		// at the end of every AICA tick while ticks_wanted is not 0
	u32 (*pinned_rtc)();					// config:Dreamcast.RTC: seconds since 1970, 0 for the clock of the host
};

extern AicaHash aica_hash;

static inline u64 aica_hash_fold(u64 hash, const u8* data, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		hash ^= data[i];
		hash *= 0x100000001b3ull;
	}

	return hash;
}

// At every place that hands a sample pair to the audio stream
static inline void aica_hash_sample(s16 right, s16 left)
{
	if (unlikely(aica_hash.ticks_wanted != 0))
	{
		u8 bytes[4] = { (u8)right, (u8)((u16)right >> 8), (u8)left, (u8)((u16)left >> 8) };

		aica_hash.samples = aica_hash_fold(aica_hash.samples, bytes, sizeof(bytes));
	}
}
