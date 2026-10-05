// Stub: the event gates and dbg::EventSlow, implemented by package E5.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_emu.h"

namespace dbg {

// Nothing here opens a gate: DBG_EVENT gets past its test only for an event whose gate a selftest opened.
std::atomic<u8> g_event_on[EV_ID_COUNT];

// The log is a stub and records nothing; the breakpoint half of the contract works. With the stub of dbg::emu
// no thread is the emulator thread and nothing happens at all.
void EventSlow(EventId id, const char*, ...)
{
	if (emu::OnEmuThread())
		emu::OnEvent(id);
}

}
