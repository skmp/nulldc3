/*
	Test driver for the sound generator of the AICA (libswirl/hw/aica/sgc_if.cpp) and for the settings of the
	sound regression harness (libswirl/hw/aica/aica_hash.cpp), linked without the rest of the emulator: what
	they reference from there is at the end of this file.

	aica_test channels    the state of a channel that no register write sets: the same whatever the memory the
	                      generator was created in held, for the guest, for the debugger and in what is played;
	                      the loop flag is the same bit for the guest and for the debugger
	aica_test rtc         the keys of the configuration that pin the real-time clock

	Exit code 0 when every check held.
*/
#include "cfg/cfg.h"
#include "hw/aica/aica_hash.h"
#include "hw/aica/aica_mmio.h"
#include "hw/aica/dsp_backend.h"
#include "hw/aica/sgc_if.h"
#include "hw/sh4/sh4_if.h"
#include "hw/sh4/sh4_sched.h"
#include "oslib/audiostream.h"
#include "serialize.h"
#include "stdclass.h"
#include "types.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

	// The play position of the selected channel as a guest reads it: CA, 0x2814
	u32 GuestReadsPlayPosition(SGC* sgc)
	{
		sgc->ReadCommonReg(0x2814, false);

		return aica_reg[0x2814] | (aica_reg[0x2815] << 8);
	}

	// A 16-bit write to a register of a channel, as the AICA hands it to the generator
	void WriteChannelReg(SGC* sgc, u32 channel, u32 reg, u32 value)
	{
		aica_reg[channel * 0x80 + reg] = (u8)value;
		aica_reg[channel * 0x80 + reg + 1] = (u8)(value >> 8);
		sgc->WriteChannelReg8(channel, reg);
		sgc->WriteChannelReg8(channel, reg + 1);
	}

	// The audio stream of a generator that plays
	struct SampleSink : AudioStream
	{
		std::vector<s16> samples;

		void InitAudio() override { }
		void TermAudio() override { }

		void WriteSample(s16 right, s16 left) override
		{
			samples.push_back(right);
			samples.push_back(left);
		}
	};

	// What one channel plays
	enum Voice
	{
		VOICE_WAVE,		// a loop of 16 samples
		VOICE_LFO,		// the same loop with the amplitude modulation of the oscillator, which the guest does not reset
		VOICE_NOISE,	// the noise generator
	};

	const u32 played_channel = 7;
	const u32 silent_channel = 40;
	const u32 played_ticks = 64;	// of 32 samples: the oscillator of VOICE_LFO goes around 8 times

	// What a run gives to the outside
	struct Played
	{
		// to the audio stream
		std::vector<s16> samples;

		// the status and the play position (0x2810..0x2815) as the guest reads them after every tick, of the
		// channel that plays and of one that was never keyed on; then the register file
		std::vector<u8> regs;

		// the oscillator of the channel that plays, after every tick
		std::vector<u8> phases;

		// what the debugger shows of every channel at the end, shown_fields of each
		std::vector<u32> shown;
	};

	const char* const shown_fields[] = { "enabled", "looped", "aegState", "fegState", "aegLevel", "fegValue", "ca", "caFraction", "step",
		"sampleCurrent", "samplePrevious", "lfoState", "alfo", "plfo" };
	const size_t shown_field_count = sizeof(shown_fields) / sizeof(shown_fields[0]);

	// Creates a generator in memory filled with one byte, on a machine that is empty otherwise, starts one
	// channel and plays
	Played Play(u8 fill, Voice voice)
	{
		Played played;
		SampleSink sink;

		memset(aica_reg, 0, sizeof(aica_reg));
		memset(aica_ram, 0, sizeof(aica_ram));

		alloc_fill = fill;
		SGC* sgc = SGC::Create(&sink, aica_reg, &dsp, aica_ram, sizeof(aica_ram));
		alloc_fill = 0xFF;

		const u32 wave_at = 0x10000;

		for (u32 i = 0; i < 16; i++)
		{
			u32 sample = 0x1000 + i * 0x300;

			aica_ram[wave_at + i * 2] = (u8)sample;
			aica_ram[wave_at + i * 2 + 1] = (u8)(sample >> 8);
		}

		aica_reg[0x2800] = 0x0F;										// MVOL: full volume
		WriteChannelReg(sgc, played_channel, 0x04, wave_at & 0xFFFF);	// SA, low part
		WriteChannelReg(sgc, played_channel, 0x08, 0);					// LSA
		WriteChannelReg(sgc, played_channel, 0x0C, 16);					// LEA
		WriteChannelReg(sgc, played_channel, 0x10, 0x001F);				// AR: at full level with the first sample, and held
		WriteChannelReg(sgc, played_channel, 0x24, 0x0F00);				// DISDL: to both outputs, not attenuated

		if (voice == VOICE_LFO)
			WriteChannelReg(sgc, played_channel, 0x1C, 0x7C07);			// LFOF 31: a step per sample; ALFOWS sawtooth, ALFOS 7; LFORE stays 0

		// KYONEX, KYONB, LPCTL, 16-bit PCM or the noise generator (SSCTL), SA high part
		WriteChannelReg(sgc, played_channel, 0x00, (voice == VOICE_NOISE ? 0xC600 : 0xC200) | (wave_at >> 16));

		for (u32 tick = 0; tick < played_ticks; tick++)
		{
			sgc->AICA_Sample32();

			for (u32 channel : { played_channel, silent_channel })
			{
				SelectChannel(channel);
				sgc->ReadCommonReg(0x2810, false);
				sgc->ReadCommonReg(0x2814, false);
				played.regs.insert(played.regs.end(), &aica_reg[0x2810], &aica_reg[0x2816]);
			}

			AicaChannelDebug shown = {};
			sgc->DebugGetChannel(played_channel, &shown);
			played.phases.push_back(shown.lfoState);
		}

		played.regs.insert(played.regs.end(), aica_reg, aica_reg + sizeof(aica_reg));
		played.samples = sink.samples;

		for (u32 channel = 0; channel < 64; channel++)
		{
			AicaChannelDebug shown = {};
			sgc->DebugGetChannel(channel, &shown);

			const u32 fields[] = { shown.enabled, shown.looped, shown.aegState, shown.fegState, shown.aegLevel, shown.fegValue, shown.ca, shown.caFraction,
				shown.step, (u32)shown.sampleCurrent, (u32)shown.samplePrevious, shown.lfoState, shown.alfo, shown.plfo };
			static_assert(sizeof(fields) / sizeof(fields[0]) == shown_field_count, "one name for every field");

			played.shown.insert(played.shown.end(), fields, fields + shown_field_count);
		}

		delete sgc;
		return played;
	}

	// The first place where two runs differ, or the size of the shorter
	template<typename T>
	size_t FirstDifference(const std::vector<T>& a, const std::vector<T>& b)
	{
		size_t i = 0;

		while (i < a.size() && i < b.size() && a[i] == b[i])
			i++;

		return i;
	}

	// A generator plays the same, and shows the guest the same, whatever the memory it was created in held
	void TestPlaysFromAnyMemory(Voice voice, const char* name)
	{
		Played reference = Play(0x00, voice);

		size_t sounding = 0;
		for (s16 sample : reference.samples)
			sounding += sample != 0;

		Check(reference.samples.size() == played_ticks * 32 * 2 && sounding > reference.samples.size() / 2, "%s: %zu samples of %u ticks, %zu of them not 0",
			name, reference.samples.size(), played_ticks, sounding);

		for (u8 fill : { (u8)0xFF, (u8)0x5A })
		{
			Played played = Play(fill, voice);

			size_t at = FirstDifference(played.samples, reference.samples);
			Check(played.samples == reference.samples, "%s: a generator created in memory of %02X plays as one created in memory of 00: sample %zu is %d, not %d",
				name, fill, at, at < played.samples.size() ? played.samples[at] : 0, at < reference.samples.size() ? reference.samples[at] : 0);

			at = FirstDifference(played.regs, reference.regs);
			Check(played.regs == reference.regs, "%s: the guest reads the same from a generator created in memory of %02X: byte %zu is %02X, not %02X",
				name, fill, at, at < played.regs.size() ? played.regs[at] : 0, at < reference.regs.size() ? reference.regs[at] : 0);

			at = FirstDifference(played.phases, reference.phases);
			Check(played.phases == reference.phases, "%s: the oscillator of a generator created in memory of %02X runs as that of one created in memory of 00: after tick %zu it is at %02X, not %02X",
				name, fill, at, at < played.phases.size() ? played.phases[at] : 0, at < reference.phases.size() ? reference.phases[at] : 0);

			at = FirstDifference(played.shown, reference.shown);
			Check(played.shown == reference.shown, "%s: the debugger is shown the same channels of a generator created in memory of %02X: %s of channel %zu is %08X, not %08X",
				name, fill, shown_fields[at % shown_field_count], at / shown_field_count,
				at < played.shown.size() ? played.shown[at] : 0, at < reference.shown.size() ? reference.shown[at] : 0);
		}
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

		// no channel was keyed on: none has passed a loop end, none has a play position or a sample, and the
		// oscillator and the filter envelope of each stand at 0. The debugger's reads come first, a guest read
		// clears the loop flag
		for (u32 channel = 0; channel < 64; channel++)
		{
			AicaChannelDebug shown = {};
			u32 status = 0xFFFFFFFF;
			u32 position = 0xFFFFFFFF;

			shown.looped = true;
			shown.lfoState = 0xFF;
			shown.fegValue = 0xFFFFFFFF;
			shown.ca = 0xFFFFFFFF;
			shown.caFraction = 0xFFFFFFFF;
			shown.sampleCurrent = -1;
			shown.samplePrevious = -1;
			SelectChannel(channel);

			bool ok = sgc->DebugGetChannel(channel, &shown) && sgc->DebugReadCommonReg(0x2810, &status) && sgc->DebugReadCommonReg(0x2814, &position);

			Check(ok && !shown.looped && (status & 0x8000) == 0, "channel %u was never keyed on: the debugger shows looped %d, status %04X",
				channel, shown.looped, status);
			Check(!GuestReadsLoopFlag(sgc), "channel %u was never keyed on: the guest reads LP set", channel);

			Check(ok && shown.lfoState == 0 && shown.alfo == 0 && shown.plfo == 0, "channel %u was never keyed on: the debugger shows the oscillator at %02X, giving %02X and %02X",
				channel, shown.lfoState, shown.alfo, shown.plfo);
			Check(ok && shown.fegValue == 0, "channel %u was never keyed on: the debugger shows the filter envelope at %08X", channel, shown.fegValue);
			Check(ok && shown.ca == 0 && position == 0 && GuestReadsPlayPosition(sgc) == 0,
				"channel %u was never keyed on: the debugger shows the play position %08X and CA %04X, the guest reads CA %04X",
				channel, shown.ca, position, GuestReadsPlayPosition(sgc));
			Check(ok && shown.caFraction == 0 && shown.sampleCurrent == 0 && shown.samplePrevious == 0,
				"channel %u was never keyed on: the debugger shows the fraction %X of its play position and the samples %d and %d",
				channel, shown.caFraction, shown.samplePrevious, shown.sampleCurrent);
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

		// What is played does not come from the memory of the generator either. The oscillator is the phase
		// the amplitude modulation is worked out from, the noise generator has no other start than its last
		// value
		TestPlaysFromAnyMemory(VOICE_WAVE, "a loop");
		TestPlaysFromAnyMemory(VOICE_LFO, "a loop with amplitude modulation");
		TestPlaysFromAnyMemory(VOICE_NOISE, "noise");

		// the three are different sounds, and the oscillator that was never reset started at 0: it steps once per sample
		Played wave = Play(0xFF, VOICE_WAVE);
		Played lfo = Play(0xFF, VOICE_LFO);
		Played noise = Play(0xFF, VOICE_NOISE);

		Check(lfo.samples != wave.samples && noise.samples != wave.samples && noise.samples != lfo.samples,
			"the amplitude modulation and the noise generator are heard: first differences at samples %zu, %zu and %zu",
			FirstDifference(lfo.samples, wave.samples), FirstDifference(noise.samples, wave.samples), FirstDifference(noise.samples, lfo.samples));

		for (u32 tick = 0; tick < 8 && tick < lfo.phases.size(); tick++)
			Check(lfo.phases[tick] == (u8)((tick + 1) * 32), "the oscillator of a channel started at 0: after %u samples it is at %02X", (tick + 1) * 32, lfo.phases[tick]);
	}

	// The configuration the emulator's units read: "section:key" and its number
	std::map<std::string, s32> config;

	void TestRtc()
	{
		// 2000-01-01 00:00:00, in seconds since 1970
		const u32 second = 946684800;

		if (!Check(aica_hash.pinned_rtc != nullptr, "the harness entered its functions"))
			return;

		Check(aica_hash.pinned_rtc() == 0, "no key: the clock is the host's, not pinned to %u", aica_hash.pinned_rtc());

		config = { { "config:Debug.FixedRtc", (s32)second } };
		Check(aica_hash.pinned_rtc() == second, "config:Debug.FixedRtc pins the clock: %u", aica_hash.pinned_rtc());

		config = { { "config:Dreamcast.RTC", (s32)second } };
		Check(aica_hash.pinned_rtc() == second, "config:Dreamcast.RTC pins the clock: %u", aica_hash.pinned_rtc());

		config = { { "config:Debug.FixedRtc", (s32)second }, { "config:Dreamcast.RTC", (s32)second + 60 } };
		Check(aica_hash.pinned_rtc() == second, "both keys: config:Debug.FixedRtc goes first: %u", aica_hash.pinned_rtc());

		config = { { "config:Debug.FixedRtc", 0 }, { "config:Dreamcast.RTC", (s32)second } };
		Check(aica_hash.pinned_rtc() == second, "config:Debug.FixedRtc is 0: config:Dreamcast.RTC pins the clock: %u", aica_hash.pinned_rtc());

		// the keys belong to the section config
		config = { { "Debug:FixedRtc", (s32)second }, { "Dreamcast:RTC", (s32)second }, { "Debug:Debug.FixedRtc", (s32)second } };
		Check(aica_hash.pinned_rtc() == 0, "the keys in another section pin nothing: %u", aica_hash.pinned_rtc());
	}
}

int main(int argc, char** argv)
{
	std::string mode = argc > 1 ? argv[1] : "";

	if (mode == "channels" && argc == 2)
		TestChannels();
	else if (mode == "rtc" && argc == 2)
		TestRtc();
	else
	{
		fprintf(stderr, "usage: aica_test channels | rtc\n");
		return 2;
	}

	printf("%s: %d checks, %d failed\n", mode.c_str(), checks, failures);
	return failures == 0 && checks != 0 ? 0 : 1;
}


// What sgc_if.cpp and aica_hash.cpp reference from the rest of the emulator

settings_t settings;
SuperH4* sh4_cpu;
Sh4RCB* p_sh4rcb;
AicaHash aica_hash;

s32 cfgLoadInt(const wchar* section, const wchar* key, s32 def)
{
	auto found = config.find(std::string(section) + ":" + key);

	return found != config.end() ? found->second : def;
}

u64 sh4_sched_now64()
{
	return 0;
}

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
