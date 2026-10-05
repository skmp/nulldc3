// Stub: the event gates and dbg::EventSlow, implemented by package E5.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "debugger.h"

#include <cstdio>
#include <cstdlib>

namespace dbg {

// Every gate stays closed: DBG_EVENT never gets past its test.
std::atomic<u8> g_event_on[EV_ID_COUNT];

void EventSlow(EventId, const char*, ...)
{
	fprintf(stderr, "debugger: dbg::EventSlow reached, but the event log is a stub\n");
	abort();
}

}
