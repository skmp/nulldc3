/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "aica_hash.h"
#include "dsp_core/aica_dsp_core.h"

#include "cfg/cfg.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_mmio.h"
#include "hw/sh4/sh4_sched.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

/*
	The part of the sound regression harness that needs the emulator around the AICA (aica_hash.h).
*/

static void aica_hash_configure()
{
	aica_hash.ticks_wanted = (u32)std::max<s32>(0, cfgLoadInt("config", "Debug.AicaHash", 0));
	aica_hash.ticks = 0;
	aica_hash.samples = aica_hash.regs = aica_hash.arm7 = aica_hash.sh4 = aica_hash.dsp = 0xcbf29ce484222325ull;
}

// The state at the end of an AICA tick, and the end of the run
static void aica_hash_tick(const u8* aica_reg, const AicaDsp* dsp)
{
	aica_hash.regs = aica_hash_fold(aica_hash.regs, aica_reg, 0x8000);

	//the DSP, as its state blob has it
	static std::vector<u8> dsp_state(AicaDsp::StateSize());
	dsp->SaveState(dsp_state.data());
	aica_hash.dsp = aica_hash_fold(aica_hash.dsp, dsp_state.data(), dsp_state.size());

	//the sound CPU, as it goes into a save state
	u8 scpu_state[1024];
	void* data = scpu_state;
	unsigned int size = 0;
	sh4_cpu->GetA0Handler(A0H_SCPU)->serialize(&data, &size);
	verify(size <= sizeof(scpu_state));
	aica_hash.arm7 = aica_hash_fold(aica_hash.arm7, scpu_state, size);

	//the SH4 context is complete inside a scheduler callback only on a backend that says so
	SuperH4Backend* backend = sh4_cpu->DebugBackend();
	if (backend != nullptr && (backend->DebugCaps() & SH4DBG_CTX_AT_SLICE))
	{
		u32 sh4_state[20];
		sh4_state[0] = Sh4cntx.pc;
		for (int i = 0; i < 16; i++)
			sh4_state[1 + i] = Sh4cntx.r[i];
		sh4_state[17] = Sh4cntx._pr;
		sh4_state[18] = sh4_sr_GetFull();
		sh4_state[19] = Sh4cntx.fpscr.full;

		u8 bytes[sizeof(sh4_state)];
		for (size_t i = 0; i < sizeof(bytes); i++)
			bytes[i] = (u8)(sh4_state[i / 4] >> (i % 4 * 8));
		aica_hash.sh4 = aica_hash_fold(aica_hash.sh4, bytes, sizeof(bytes));
	}

	if (++aica_hash.ticks == aica_hash.ticks_wanted)
	{
		printf("AicaHash: samples=%016llx regs=%016llx arm7=%016llx sh4=%016llx dsp=%016llx ticks=%u cycles=%llu\n",
			(unsigned long long)aica_hash.samples, (unsigned long long)aica_hash.regs, (unsigned long long)aica_hash.arm7,
			(unsigned long long)aica_hash.sh4, (unsigned long long)aica_hash.dsp, aica_hash.ticks, (unsigned long long)sh4_sched_now64());
		fflush(stdout);

		//the run ends here, on the emulator thread and without the emulator's shutdown: nothing is saved
		std::_Exit(0);
	}
}

// Two keys name the second the clock is pinned to. Debug.FixedRtc goes first; a key that is 0 or missing pins nothing.
// Each is a setting of section config that the section of a game overrides: the config store returns the key of the
// game's own section, where that section has it, from the moment the id of the game is set (cfgLoadInt)
static u32 aica_hash_pinned_rtc()
{
	u32 pinned = (u32)cfgLoadInt("config", "Debug.FixedRtc", 0);

	if (pinned == 0)
		pinned = (u32)cfgLoadInt("config", "Dreamcast.RTC", 0);

	return pinned;
}

// aica_hash is zero before any constructor runs: this only adds the functions
static struct AicaHashEnter
{
	AicaHashEnter()
	{
		aica_hash.configure = &aica_hash_configure;
		aica_hash.tick = &aica_hash_tick;
		aica_hash.pinned_rtc = &aica_hash_pinned_rtc;
	}
} aica_hash_enter;
