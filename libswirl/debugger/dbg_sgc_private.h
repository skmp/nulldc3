/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include <string>

/*
	What dbg_sgc.cpp shows its selftest (dbg_selftest_sgc.cpp) beyond dbg_sgc.h. Not one of the frozen headers;
	nothing but the selftest includes it. Same contexts as dbg_sgc.h: (E), or under L while no emulator thread
	exists. Nothing here changes what a capture does.
*/

namespace dbg { namespace sgc { namespace test {

// What Arm() was asked and did, counted from the start of the process
struct ArmRecord {
	u32 calls;			// Arm() calls
	u32 armed;			// of them, those that armed (returned 0)
	int lastRv;			// what the last call returned
	bool lastOnEmu;		// the last call was made on the emulator thread (dbg::emu::OnEmuThread)
	bool lastStopWhenDone;
};

ArmRecord Arms();

// The notices given, counted from the start of the process, and the answer of the last one. Any thread: the count
// has gone up when the notice is handed to the core
struct NoticeRecord {
	u32 given;
	int lastAnswer;
};

NoticeRecord Notices();		// any thread

// The capture that records: the frames it was armed for, the frames it has taken, and whether it was armed at a
// stop (stopWhenDone). All 0 / false while none records.
struct Recording {
	u32 wanted;
	u32 taken;
	bool stopWhenDone;
};

Recording GetRecording();

// Appends frame `index` (0 .. taken - 1) of the capture that records to *out, kBytesPerFrame bytes. Returns false,
// appending nothing, when no capture records or it has not taken that frame.
bool CopyRecordingFrame(u32 index, std::string* out);

} } }
