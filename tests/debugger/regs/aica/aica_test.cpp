/*
	Test driver for the sound generator of the AICA (libswirl/hw/aica/sgc_if.cpp), linked without the rest of
	the emulator: what it references from there is at the end of this file.

	aica_test channels    the loop flag of a channel: clear until the channel passes a loop end, whatever the
	                      memory the generator was created in held, and the same bit for the guest and for
	                      the debugger

	Exit code 0 when every check held.
*/
#include "hw/aica/aica_hash.h"
#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp_backend.h"
#include "hw/aica/sgc_if.h"
#include "serialize.h"
#include "stdclass.h"
#include "types.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace
{
	// operator new of this program fills what it hands out with this byte, so that a member no constructor
	// wrote reads as it and not as the zero a fresh heap happens to hold. The fill is a store in front of a
	// constructor: it lasts because the constructors are in other translation units and nothing is optimised
	// across them
	unsigned char alloc_fill = 0xFF;
	size_t alloc_bytes;
}

void* operator new(std::size_t size)
{
	void* memory = std::malloc(size != 0 ? size : 1);

	if (memory == nullptr)
		throw std::bad_alloc();

	std::memset(memory, alloc_fill, size);
	alloc_bytes += size;
	return memory;
}

void operator delete(void* memory) noexcept
{
	std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
	std::free(memory);
}

namespace
{
	int failures;
	int checks;

	bool Check(bool ok, const char* fmt, ...)
	{
		checks++;

		if (!ok)
		{
			char text[512];
			va_list args;
			va_start(args, fmt);
			vsnprintf(text, sizeof(text), fmt, args);
			va_end(args);

			if (++failures <= 60)
				fprintf(stderr, "FAIL %s\n", text);
		}

		return ok;
	}

	u8 aica_reg[0x8000];
	u8 aica_ram[2 * 1024 * 1024];
	dsp_context_t dsp;

	// MSLC, the channel whose status the common registers 0x2810 and 0x2814 show: bits 8..13 of 0x280C
	void SelectChannel(u32 channel)
	{
		aica_reg[0x280D] = (u8)channel;
	}

	// The loop flag of the selected channel as a guest reads it: LP, bit 15 of 0x2810. The read clears the flag
	bool GuestReadsLoopFlag(SGC* sgc)
	{
		sgc->ReadCommonReg(0x2810, false);

		return (aica_reg[0x2811] & 0x80) != 0;
	}

	std::vector<u8> SaveState(SGC* sgc)
	{
		void* data = nullptr;
		unsigned int size = 0;
		sgc->channel_serialize(&data, &size);

		std::vector<u8> state(size);
		data = state.data();
		size = 0;
		sgc->channel_serialize(&data, &size);

		return state;
	}

	void LoadState(SGC* sgc, std::vector<u8>& state)
	{
		void* data = state.data();
		unsigned int size = 0;
		sgc->channel_unserialize(&data, &size);
	}

	void TestChannels()
	{
		size_t allocated = alloc_bytes;
		SGC* sgc = SGC::Create(nullptr, aica_reg, &dsp, aica_ram, sizeof(aica_ram));

		allocated = alloc_bytes - allocated;
		Check(allocated >= 64 * 64, "the generator and its 64 channels come from the operator new that fills: %zu bytes", allocated);

		// no channel was keyed on: none has passed a loop end. The debugger's reads come first, a guest read
		// clears the flag
		for (u32 channel = 0; channel < 64; channel++)
		{
			AicaChannelDebug shown = {};
			u32 status = 0xFFFFFFFF;

			shown.looped = true;
			SelectChannel(channel);

			bool ok = sgc->DebugGetChannel(channel, &shown) && sgc->DebugReadCommonReg(0x2810, &status);

			Check(ok && !shown.looped && (status & 0x8000) == 0, "channel %u was never keyed on: the debugger shows looped %d, status %04X",
				channel, shown.looped, status);
			Check(!GuestReadsLoopFlag(sgc), "channel %u was never keyed on: the guest reads LP set", channel);
		}

		// The flag is a byte of the save state and one bit to the guest. The byte stands behind the loop
		// points of its channel, which are found by their values
		const u32 channel = 5;
		const u32 loop_points[2] = { 0xA55A, 0x5AA5 };
		u8* regs = &aica_reg[channel * 0x80];

		regs[0x08] = (u8)loop_points[0];
		regs[0x09] = (u8)(loop_points[0] >> 8);
		regs[0x0C] = (u8)loop_points[1];
		regs[0x0D] = (u8)(loop_points[1] >> 8);
		sgc->WriteChannelReg8(channel, 0x08);
		sgc->WriteChannelReg8(channel, 0x0C);

		std::vector<u8> state = SaveState(sgc);
		size_t flag_at = 0;
		u32 found = 0;

		for (size_t i = 0; i + sizeof(loop_points) < state.size(); i++)
		{
			if (memcmp(&state[i], loop_points, sizeof(loop_points)) == 0)
			{
				flag_at = i + sizeof(loop_points);
				found++;
			}
		}

		if (Check(found == 1, "the loop points of channel %u are in the save state once: %u times in %zu bytes", channel, found, state.size()))
		{
			SelectChannel(channel);

			for (u32 value : { 0u, 1u, 2u, 3u, 0x80u, 0xFEu, 0xFFu })
			{
				AicaChannelDebug shown = {};
				u32 status = 0;

				state[flag_at] = (u8)value;
				LoadState(sgc, state);

				bool ok = sgc->DebugGetChannel(channel, &shown) && sgc->DebugReadCommonReg(0x2810, &status);
				bool status_bit = (status & 0x8000) != 0;
				bool guest_bit = GuestReadsLoopFlag(sgc);

				Check(ok && guest_bit == ((value & 1) != 0) && shown.looped == guest_bit && status_bit == guest_bit,
					"loop flag byte %02X: the guest reads LP %d, the debugger shows looped %d and status %04X", value, guest_bit, shown.looped, status);

				ok = sgc->DebugGetChannel(channel, &shown);
				Check(ok && !shown.looped, "loop flag byte %02X: the guest's read cleared the flag, the debugger shows looped %d", value, shown.looped);
			}
		}

		delete sgc;
	}
}

int main(int argc, char** argv)
{
	std::string mode = argc > 1 ? argv[1] : "";

	if (mode == "channels" && argc == 2)
		TestChannels();
	else
	{
		fprintf(stderr, "usage: aica_test channels\n");
		return 2;
	}

	printf("%s: %d checks, %d failed\n", mode.c_str(), checks, failures);
	return failures == 0 && checks != 0 ? 0 : 1;
}


// What sgc_if.cpp references from the rest of the emulator

settings_t settings;
SuperH4* sh4_cpu;
AicaHash aica_hash;

bool rc_serialize(void* src, unsigned int src_size, void** dest, unsigned int* total_size)
{
	if (*dest != nullptr)
	{
		memcpy(*dest, src, src_size);
		*dest = (u8*)*dest + src_size;
	}

	*total_size += src_size;
	return true;
}

bool rc_unserialize(void* src, unsigned int src_size, void** dest, unsigned int* total_size)
{
	if (*dest != nullptr)
	{
		memcpy(src, *dest, src_size);
		*dest = (u8*)*dest + src_size;
	}

	*total_size += src_size;
	return true;
}

// verify() and die() end the test
int msgboxf(const wchar* text, unsigned int type, ...)
{
	(void)type;

	va_list args;
	va_start(args, type);
	vfprintf(stderr, text, args);
	va_end(args);

	return 0;
}

void os_DebugBreak()
{
	abort();
}

void libCore_CDDA_Sector(s16* sector)
{
	memset(sector, 0, 2352);
}
