/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include "dbg_emu.h"
#include <string>

/*
	dbg::sgc: the SGC frame capture (dbg_sgc.cpp), behind state.recordSgcFrames and state.getSgcFrameData. The
	sound burst reaches it through dbg_sgc_active and dbg_sgc_Frame of debugger.h; the core (dbg_core.cpp) and run
	control (dbg::emu) call what is below.

	A frame is one sample of the AICA: kChannelsPerFrame records of kBytesPerChannel bytes, channel 0 first, taken
	behind the sound generator's sweep of the sample (AICA::SampleSweep) and in front of its DSP phase. A record,
	little endian (design 3.4):
	  0..71     the channel's registers 0x00..0x47, as AICA::DebugReadRegs gives them
	  72  i16   AicaChannelDebug::sampleCurrent          74  i16  samplePrevious
	  76..83    0: this sound generator computes no filtered, post-AEG, post-FEG or post-TL sample
	  84  i16   outLeft          86  i16  outRight          88  i16  outDsp: the channel's outputs of this sample
	  90  u16   caFraction       92  u32  step              96  u32  aegLevel: 0 silent .. 0x3FF loudest
	  100 u32   fegValue         104 u8   lfoState          105 u8   alfo       106 u8  plfo
	  107 u16   ca, its low 16 bits (unaligned)             109 u8   flags: bit 0 enabled, bits 1-2 aegState
	  110..127  0
	A capture records into a buffer of its own and replaces the server's buffer when it is complete. The server's
	buffer stays until the next capture is complete, also across a reset and a thread exit.

	Everything below is (E), or under L while no emulator thread exists, unless it says otherwise. Nothing here
	blocks.
*/

namespace dbg { namespace sgc {

const u32 kChannelsPerFrame = 64;
const u32 kBytesPerChannel  = 128;
const u32 kBytesPerFrame    = kChannelsPerFrame * kBytesPerChannel;	// 8192

// Arms a capture of the next `frames` samples whose sweep comes behind the call, 1..kMaxSgcFrames, allocating its
// buffer. Inside a CC_EXEC call at a stop, with stopWhenDone true: the caller arms the resume behind it
// (emu::Resume; when that is refused the caller ends the capture with Cancel(kErrNotPaused)), and the sample that
// completes the capture records a stop with StopCause::Capture (dbg_sgc_Frame). Inside a CC_TABLE call of a running
// machine, with stopWhenDone false: nothing stops. (E) only: never called without an emulator thread (a CC_TABLE
// closure that emu::Call runs under L, with no thread, arms nothing). A CC_TABLE closure that the exiting thread
// runs in the drain of emu::OnThreadExiting is on (E) and may arm: OnThreadExiting cancels behind its drain.
// The AICA is clocked whatever ARMRST says: a capture records whether the ARM7 is held or not.
// Returns 0 when armed; otherwise nothing is armed and it returns
//   kErrNotPaused     a capture records already: it goes on, and this request is answered with the code
//   kErrNotSupported  this build has no capture (the stub of header revision 7)
int Arm(u32 frames, bool stopWhenDone);

// Ends a capture that records: its frames are dropped and the server's buffer stays as it was. answer is what the
// notice hands on: kErrCaptureTimeout (the core's deadline, inside a CC_TABLE call; a guest reset, from
// dbg::emu::OnGuestStateReplaced), kErrHostStopped (from dbg::emu::OnThreadExiting, behind the drain of its call
// queue) or kErrNotPaused (inside the CC_EXEC call that armed the capture, when emu::Resume() refused to arm the
// resume behind it). Does nothing when no capture records.
void Cancel(int answer);

enum class State : u8 { None, Recording };

// Whether a capture records, and in *frames how many frames the server's buffer holds (0: none). Inside a CC_READ
// call, or under L while no emulator thread exists.
State GetState(u32* frames);

// Appends the server's buffer, frames * kBytesPerFrame bytes, to *out: the payload of a binary frame of kind
// kBinarySgc. Same contexts as GetState(). Returns the number of frames appended (0: there is no buffer).
u32 Copy(std::string* out);

// The core's notice, installed at dbg::Init, removed (null) at dbg::Term. It is given once for every capture that
// Arm() armed: on (E) inside dbg_sgc_Frame() when the capture is complete (answer 0), and wherever Cancel() ends
// one (answer as given). It names no capture: at most one capture is armed whose notice has not been given, since
// Arm() refuses while one records; the core pairs a notice with its request (M3-ROUND.md M3-D28). It must not
// block or call emu::Call: it queues a message for (C).
void SetCompletionNotice(void (*notice)(int answer));

} }
