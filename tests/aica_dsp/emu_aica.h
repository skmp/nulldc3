/*
	The emulator's own sound chip on a machine of its own, for the tests of the wiring of the DSP core into it
	(aica.dsp.case.<case>.emu, aica.dsp.emu.*): the AICA device of libswirl/hw/aica/aica_mmio.cpp with the sound
	generator of sgc_if.cpp and the DSP device of dsp.cpp, which holds the core. Around them only what they reach
	of the rest of the emulator, as stand-ins (emu_aica.cpp, after the stubs of tests/arm7 and
	tests/debugger/regs/aica): no SH4, no ARM7, no scheduler.

	The register accesses are those of a CPU: AICA::ReadReg / AICA::WriteReg, with the AICA's byte and slot
	handling. A sample is libAICA_TimeStep(), the emulator's time step of the AICA. The wave memory is the
	device's. What the sample hands to the audio stream is kept, and the CD-DA pairs come from a stand-in that,
	when it is on, gives a pair of its own in every sample, none of its halves 0.

	This header needs nothing of the emulator: a test that includes it is compiled as the core's tests are.
*/
#pragma once
#include "hw/aica/dsp_core/aica_dsp_core.h"

#include <cstdint>
#include <vector>

struct EmuAica
{
	// A machine as the emulator brings its sound chip up: the devices created, Init, Reset(false). The wave
	// memory is ram_size bytes of 0. One machine at a time
	static EmuAica* Create(uint32_t ram_size);

	virtual ~EmuAica() { }

	// AICA::ReadReg and AICA::WriteReg at offset off of the register area (0x00700000 for the SH4), size 1, 2 or 4
	virtual uint32_t RegRead(uint32_t off, uint32_t size) = 0;
	virtual void RegWrite(uint32_t off, uint32_t value, uint32_t size) = 0;

	// the register file as the CPUs wrote it (the AICA's 0x8000 bytes)
	virtual const uint8_t* File() = 0;

	virtual uint8_t* Ram() = 0;
	virtual uint32_t RamSize() = 0;

	// one sample: libAICA_TimeStep()
	virtual void Sample() = 0;

	// the SH4's interrupt line of the AICA, as the AICA raised and cancelled it
	virtual bool Sh4Irq() = 0;

	// the DSP device: DSP::Core(), Reset(false), and its save state (serialize / unserialize)
	virtual AicaDsp* Core() = 0;
	virtual void DspReset() = 0;
	virtual std::vector<uint8_t> DspSave() = 0;
	virtual void DspLoad(const std::vector<uint8_t>& state) = 0;

	// the AICA device's save state (serialize / unserialize): its timers, the register file and the sound
	// generator's channels
	virtual std::vector<uint8_t> AicaSave() = 0;
	virtual void AicaLoad(const std::vector<uint8_t>& state) = 0;

	// what the audio stream was handed: the pairs, and the last one
	virtual uint64_t Pairs() = 0;
	virtual int16_t LastLeft() = 0;
	virtual int16_t LastRight() = 0;
};

// The CD-DA stand-in: off, every sector is silence (no disc, as in the emulator's runs of the tree's programs). On,
// pair n of the run (n counted from 0 for every machine) is EmuCddaPair(n)
void EmuCddaOn(bool on);
void EmuCddaPair(uint64_t n, int16_t* left, int16_t* right);

// "Disable Sound", the emulator's config:aica.NoSound (settings.aica.NoSound): off unless a test turns it on, for every
// machine from then on
void EmuNoSound(bool on);

// The sound regression harness (hw/aica/aica_hash.h) as far as a sample reaches it: its fold of the pairs, which the
// sound generator makes where it hands a pair to the audio stream (aica_hash_sample). On, the fold starts again from
// the FNV offset basis; EmuHarnessSamples is what it folded since. The harness's ticks are never made here (they
// are the tail of a sound burst, and no burst runs). One harness for every machine
void EmuHarnessOn(bool on);
uint64_t EmuHarnessSamples();
