/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "hw/holly/sb.h"
#include "hw/sh4/sh4_mmio.h"

struct AicaContext {
	u8 regs[0x8000];
};

struct AudioStream;
struct SystemBus;
struct ASIC;
struct DSP;

// One channel of the sound generator as the debugger shows it (AICA::DebugGetChannel): the state that is
// not in the channel's registers.
struct AicaChannelDebug {
	bool enabled;        // the channel is playing: keyed on and not run to its end
	bool looped;         // the loop end was passed since the flag was last cleared by a read of the channel status
	u8   aegState;       // amplitude envelope: 0 attack, 1 decay 1, 2 decay 2, 3 release
	u8   fegState;       // filter envelope, same states
	u32  aegLevel;       // amplitude envelope as a level: 0 silent .. 0x3FF loudest
	u32  fegValue;       // filter envelope value
	u32  ca;             // play position: samples from the start address
	u32  caFraction;     // fraction of the play position, 10 bits
	u32  step;           // play position increment per output sample, 22.10 fixed point
	s32  sampleCurrent;  // the two decoded samples the output is interpolated between
	s32  samplePrevious;
	u8   lfoState;       // phase of the low-frequency oscillator
	u8   alfo;           // amplitude modulation it gives now
	u8   plfo;           // pitch modulation it gives now
	s32  outLeft;        // the channel's outputs of the sample of the last sweep: its direct left and right signal
	s32  outRight;       // and its send to the DSP, at 16-bit scale, while an SGC capture records (dbg_sgc_active,
	s32  outDsp;         // debugger/debugger.h); 0 at any other time
};

struct AICA : MMIODevice {
	//Mainloop
	virtual void Update(u32 Samples) = 0;

	//Aica reads (both sh4&arm)
	virtual u32 ReadReg(u32 addr, u32 size) = 0;
	virtual void WriteReg(u32 addr, u32 data, u32 size) = 0;

	virtual void TimeStep() = 0;

	/*
		Debugger access. Called on the emulator thread outside Update() and the time step of a sample (TimeStep,
		or its phases) - between two sound bursts, or while the debugger has one suspended: in front of an ARM7
		instruction or at the end of a sample, where the sound generator stands between two samples, or in front
		of a DSP step, where the sweep of the sample is made (its channels have stepped, its timers have counted)
		and its mix and its interrupt update are to come -, or by a thread that holds the debugger's lifetime lock
		while no emulator thread exists; DebugWriteReg on the emulator thread only. Update() and the time step
		themselves also run in the debugger's park context, when a suspended burst is run on. The defaults are
		those of a device that gives the debugger nothing.
	*/

	// Copies len bytes of the register file from offset (offset + len <= 0x8000) as guest reads deliver
	// them, and changes nothing: the channel status and play position of the common block (0x2810..0x2815)
	// are worked out for the selected channel without clearing its loop flag; 0x2C00 is ARMRST, 0x2C01 VREG.
	// The registers of the DSP (0x2804, 0x3000..0x45C7) are what the DSP answers a read of the CPU.
	// ReadReg is not a substitute: it rewrites those fields and clears the flag.
	virtual bool DebugReadRegs(u32 offset, u32 len, u8* out) { return false; }

	// Writes the 16-bit register at offset (even, below 0x8000) as a write by the SH4 does: the sound
	// generator, the timers, the interrupt lines and the ARM7 reset follow it. Returns false, having
	// changed nothing, when the register cannot be written. No device has a body for it and nothing calls
	// it: in M2 and M3 the debugger writes no register of the AICA, ARMRST among them (the debugger does not
	// hold or release the ARM7).
	virtual bool DebugWriteReg(u32 offset, u32 value) { return false; }

	// The state of channel 0..63 at this moment. No side effects. Returns false for any other channel
	// and while the state cannot be given.
	virtual bool DebugGetChannel(u32 channel, AicaChannelDebug* out) { return false; }

	static AicaContext* CreateContext() {
		return new AicaContext();
	}

	static AICA* Create(AudioStream* audio_stream, SystemBus* sb, ASIC* asic, DSP* dsp, AicaContext* aica_ctx, u8* aica_ram, u32 aram_size);

	static MMIODevice* CreateRTC();
};

struct ASIC;
struct DSP;




void libAICA_TimeStep();

u32 libAICA_GetRTC_now();	// what the clock is set to at a reset: the host's time, or config:Dreamcast.RTC; config:Debug.FixedRtc pins the same value and wins
u32 libAICA_GetRTC();		// the clock of the console, in seconds since 1/1/1950

// Debugger access to the RTC behind 0x00710000, same threads as AICA::DebugReadRegs: the seconds counter
// (the guest reads its high half at +0 and its low half at +4) and the write-enable bit (written at +8,
// which reads as 0). No side effects. Returns false while there is no RTC device.
bool libAICA_DebugGetRTC(u32* seconds, bool* writeEnable);
