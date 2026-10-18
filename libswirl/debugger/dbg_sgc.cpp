/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_sgc.h"
#include "dbg_sgc_private.h"

#include "hw/sh4/sh4_if.h"
#include "hw/aica/aica_mmio.h"

#include <atomic>
#include <cstring>
#include <new>

/*
	The SGC frame capture (dbg_sgc.h, and the SGC block of debugger.h).

	  recording   the capture that records: a buffer of its own, allocated when it is armed for its `wanted`
	              frames, of which `taken` are in it; dbg_sgc_active is 1 exactly while there is one
	  server      the server's buffer, the last capture that was complete, with its frame count; replaced by the
	              buffer of the next capture when that is complete, and kept across a cancel, a reset and a
	              thread exit

	A frame is taken behind the sweep of its sample (dbg_sgc_Frame, called by the sound burst behind
	AICA::SampleSweep and in front of its DSP phase): the channel's registers through AICA::DebugReadRegs, its state
	and the outputs the sweep kept through AICA::DebugGetChannel. The guest is only read.

	The notice of the core is given once for every capture that Arm() armed: in dbg_sgc_Frame() when the capture
	is complete, or in Cancel(). Everything here runs on (E), or under L while no emulator thread exists, and never
	while a burst executes but for dbg_sgc_Frame() itself; the notice is the one pointer another thread sets
	(SetCompletionNotice, at dbg::Init and dbg::Term), so it is atomic.

	The buffers are on the heap and the rest is plain data with static storage: nothing here has a destructor.
*/

u32 dbg_sgc_active;

namespace {

using namespace dbg;
using namespace dbg::sgc;

std::atomic<void (*)(int answer)> completion_notice { nullptr };

// the capture that records
std::string* recording;
u32 wanted;
u32 taken;
bool stop_when_done;

// the server's buffer
std::string* server;
u32 server_frames;

// dbg_sgc_private.h. The notices are counted where they are given and read on any thread
test::ArmRecord arms;
std::atomic<u32> notices_given { 0 };
std::atomic<int> notices_last { 0 };
std::atomic<u32> failing_allocations { 0 };

void Notice(int answer)
{
	notices_last.store(answer);
	notices_given++;

	if (auto notice = completion_notice.load())
		notice(answer);
}

// The buffer of a capture, zeroed. An allocation that fails throws std::bad_alloc, in new or here where the
// selftest has it fail (dbg_sgc_private.h)
std::string* NewBuffer(size_t bytes)
{
	if (failing_allocations.load() != 0)
	{
		failing_allocations--;
		throw std::bad_alloc();
	}

	return new std::string(bytes, '\0');
}

void Put16(u8* at, u32 value)
{
	at[0] = (u8)value;
	at[1] = (u8)(value >> 8);
}

void Put32(u8* at, u32 value)
{
	Put16(at, value);
	Put16(at + 2, value >> 16);
}

// The record of one channel, dbg_sgc.h's layout. Bytes it does not name stay as the zeroed buffer has them
void TakeRecord(AICA* aica, u32 channel, u8* record)
{
	aica->DebugReadRegs(channel * 0x80, 72, record);

	AicaChannelDebug d;

	memset(&d, 0, sizeof(d));
	aica->DebugGetChannel(channel, &d);

	Put16(record + 72, (u32)d.sampleCurrent);
	Put16(record + 74, (u32)d.samplePrevious);
	// 76..83: no filtered, post-AEG, post-FEG or post-TL sample in this sound generator
	Put16(record + 84, (u32)d.outLeft);
	Put16(record + 86, (u32)d.outRight);
	Put16(record + 88, (u32)d.outDsp);
	Put16(record + 90, d.caFraction);
	Put32(record + 92, d.step);
	Put32(record + 96, d.aegLevel);
	Put32(record + 100, d.fegValue);
	record[104] = d.lfoState;
	record[105] = d.alfo;
	record[106] = d.plfo;
	Put16(record + 107, d.ca);
	record[109] = (u8)((d.enabled ? 1 : 0) | ((d.aegState & 3) << 1));
	// 110..127: 0
}

}

u32 dbg_sgc_Frame(bool checks)
{
	if (recording == nullptr)
		return 0;

	AICA* aica = sh4_cpu->GetA0H<AICA>(A0H_AICA);
	u8* frame = (u8*)&(*recording)[(size_t)taken * kBytesPerFrame];

	for (u32 channel = 0; channel < kChannelsPerFrame; channel++)
		TakeRecord(aica, channel, frame + channel * kBytesPerChannel);

	if (++taken < wanted)
		return 0;

	// complete: the buffer becomes the server's
	delete server;
	server = recording;
	server_frames = wanted;
	recording = nullptr;
	dbg_sgc_active = 0;

	// a capture armed at a stop stops behind its last frame, unless dc_run runs the burst to its end
	const bool stop = stop_when_done && checks;

	if (stop)
		emu::OnSoundStop(StopCause::Capture, 0);

	Notice(0);

	return stop ? 1 : 0;
}

namespace dbg { namespace sgc {

int Arm(u32 frames, bool stopWhenDone)
{
	arms.calls++;
	arms.lastOnEmu = emu::OnEmuThread();
	arms.lastStopWhenDone = stopWhenDone;

	int rv = 0;

	if (recording != nullptr)
		rv = kErrNotPaused;
	else if (frames == 0 || frames > kMaxSgcFrames)
		rv = kErrInvalidParams;

	arms.lastRv = rv;

	if (rv != 0)
		return rv;

	// a buffer of its own, zeroed: the bytes a record does not name are 0. Nothing has changed when its
	// allocation throws: nothing is armed, and the caller's closure answers (dbg_core.cpp)
	recording = NewBuffer((size_t)frames * kBytesPerFrame);
	wanted = frames;
	taken = 0;
	stop_when_done = stopWhenDone;
	dbg_sgc_active = 1;

	arms.armed++;
	return 0;
}

void Cancel(int answer)
{
	if (recording == nullptr)
		return;

	// its frames go; the server's buffer stays as it was
	delete recording;
	recording = nullptr;
	wanted = 0;
	taken = 0;
	stop_when_done = false;
	dbg_sgc_active = 0;

	Notice(answer);
}

State GetState(u32* frames)
{
	*frames = server != nullptr ? server_frames : 0;
	return recording != nullptr ? State::Recording : State::None;
}

u32 Copy(std::string* out)
{
	if (server == nullptr)
		return 0;

	out->append(*server);
	return server_frames;
}

void SetCompletionNotice(void (*notice)(int answer))
{
	completion_notice.store(notice);
}

namespace test {

ArmRecord Arms()
{
	return arms;
}

NoticeRecord Notices()
{
	NoticeRecord r;

	r.given = notices_given.load();
	r.lastAnswer = notices_last.load();
	return r;
}

Recording GetRecording()
{
	Recording r = { 0, 0, false };

	if (recording != nullptr)
	{
		r.wanted = wanted;
		r.taken = taken;
		r.stopWhenDone = stop_when_done;
	}

	return r;
}

bool CopyRecordingFrame(u32 index, std::string* out)
{
	if (recording == nullptr || index >= taken)
		return false;

	out->append(*recording, (size_t)index * kBytesPerFrame, kBytesPerFrame);
	return true;
}

void FailAllocations(u32 count)
{
	failing_allocations.store(count);
}

}

} }
