/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_core_private.h"

#include "hw/sh4/sh4_sched.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

/*
	The event gates, the event log and what stands behind DBG_EVENT.

	The gates are the core's to write (dbg_core.cpp: a client connected or left, the event breakpoints
	changed); this file defines them. The log is a ring of kEventRingSize entries behind a mutex of its own,
	which is held for an append or a copy and for nothing else: EventSlow() is called from any thread, with
	any lock of the emulator held, and takes no other lock but the queue lock of dbg::emu, a leaf, for the
	published state.
*/

namespace dbg {

std::atomic<u8> g_event_on[EV_ID_COUNT];

namespace {

struct Slot {
	u64 seq;
	u64 timestampMs;
	u64 cycles;
	EventId id;
	char message[kEventMessageMax];
};

std::mutex ring_lock;

// Under ring_lock
Slot ring[kEventRingSize];
u32 ring_first;				// index of the oldest entry
u32 ring_count;
u64 ring_seq;				// the sequence number of the entry made last

u64 WallClockMs()
{
	return (u64)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// The time of the machine, while there is one: the SH4 context block goes away with it
u64 MachineCycles()
{
	if (emu::GetRunState().lifetime == Lifetime::NoMachine)
		return 0;

	return sh4_sched_now64();
}

void AppendEntry(EventId id, const char* message)
{
	const u64 now = WallClockMs();
	const u64 cycles = MachineCycles();

	std::lock_guard<std::mutex> lock(ring_lock);

	u32 at;

	if (ring_count < kEventRingSize)
	{
		at = (ring_first + ring_count) % kEventRingSize;
		ring_count++;
	}
	else
	{
		// the oldest goes
		at = ring_first;
		ring_first = (ring_first + 1) % kEventRingSize;
	}

	Slot& slot = ring[at];

	slot.seq = ++ring_seq;
	slot.timestampMs = now;
	slot.cycles = cycles;
	slot.id = id;
	snprintf(slot.message, sizeof(slot.message), "%s", message);
}

}

void EventSlow(EventId id, const char* fmt, ...)
{
	if ((unsigned)id >= EV_ID_COUNT)
		return;

	char message[kEventMessageMax];

	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);

	AppendEntry(id, message);

	// an event breakpoint stops only for what the guest raises while it executes
	if (emu::OnEmuThread())
		emu::OnEvent(id);
}

namespace events {

void Snapshot(std::vector<Entry>* out)
{
	out->clear();

	std::lock_guard<std::mutex> lock(ring_lock);

	out->reserve(ring_count);

	for (u32 i = 0; i < ring_count; i++)
	{
		const Slot& slot = ring[(ring_first + i) % kEventRingSize];

		out->push_back({ slot.seq, slot.timestampMs, slot.cycles, slot.id, slot.message });
	}
}

void Append(EventId id, const char* text)
{
	if ((unsigned)id >= EV_ID_COUNT)
		return;

	AppendEntry(id, text);
}

void ClearForTest()
{
	std::lock_guard<std::mutex> lock(ring_lock);

	ring_first = 0;
	ring_count = 0;
}

}

}
