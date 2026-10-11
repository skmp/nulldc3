/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include <cstring>

/*
	Debugger events: the ids that emulator code raises with DBG_EVENT (debugger/debugger.h) and the constant
	table that gives each id its wire name.

	An event is not a breakpoint address: the code breakpoints of a processor use the event names
	dc.sh4.cpu.pc, dc.aica.arm7.pc and dc.aica.dsp.step (dbg_emu.h), which are not in this table.

	Rate classes
	- EVR_LOW:  logged whenever a client is connected (and, with none connected, the one note of the grace
	            period: debugger.h, g_event_on)
	- EVR_HIGH: can fire at frame rate or faster; logged only while an event breakpoint on it exists (enabled or
	            not)

	This header includes nothing of the emulator and nothing here needs a lock or a thread: the table is
	constant.
*/

// One row per event:
//   X(id, wire name, subsystem, severity, rate, node)
// - wire name: "event" of a breakpoint, metadata.event of a log entry
// - subsystem: one of sh4, holly, ta, core, aica, dsp, arm7, gdrom, maple, system, debugger
// - severity:  one of trace, info, warn, error
// - node:      path of the device-tree node whose Node::events may list the event (dbg_regs.h).
//              debugger.describe lists what Node::events holds, which is only the events this build raises;
//              the register-table test (tests/debugger/regs) checks every id listed there against this field
// The comment of a row names the place that raises the event and the package that owns that place.
// Ids are the row numbers. They never leave the process: the wire carries the names.
#define DBG_EVENT_LIST(X) \
	X(EV_SH4_INTERRUPT,       "dc.sh4.interrupt",             "sh4",      "info", EVR_HIGH, "dc.sh4")        /* hw/sh4/sh4_interrupts.cpp Do_Interrupt (E4a) */ \
	X(EV_SH4_EXCEPTION,       "dc.sh4.exception",             "sh4",      "warn", EVR_LOW,  "dc.sh4")        /* hw/sh4/sh4_interrupts.cpp Do_Exception (E4a) */ \
	X(EV_DMAC_START,          "dc.holly.dmac.transfer_start", "holly",    "info", EVR_HIGH, "dc.holly.dmac") /* hw/pvr/pvr_sb_regs.cpp: ch2 and sort DMA handlers (E3) */ \
	X(EV_DMAC_END,            "dc.holly.dmac.transfer_end",   "holly",    "info", EVR_HIGH, "dc.holly.dmac") /* hw/pvr/pvr_sb_regs.cpp: ch2 and sort DMA handlers (E3) */ \
	X(EV_TA_LIST_INIT,        "dc.holly.ta.list_init",        "ta",       "info", EVR_HIGH, "dc.holly.ta")   /* hw/pvr/pvr_sb_regs.cpp (E3) */ \
	X(EV_TA_LIST_END,         "dc.holly.ta.list_end",         "ta",       "info", EVR_HIGH, "dc.holly.ta")   /* hw/pvr/ta.cpp (TA_HLE); hw/holly/holly_intc.cpp RaiseAsicNormal (TA_LLE, the default build) (E3) */ \
	X(EV_CORE_RENDER_START,   "dc.holly.core.render_start",   "core",     "info", EVR_HIGH, "dc.holly.core") /* hw/pvr/pvr_sb_regs.cpp (E3) */ \
	X(EV_CORE_RENDER_END,     "dc.holly.core.render_end",     "core",     "info", EVR_HIGH, "dc.holly.core") /* hw/pvr/spg.cpp (E3) */ \
	X(EV_CORE_VBLANK,         "dc.holly.core.vblank",         "core",     "info", EVR_HIGH, "dc.holly.core") /* hw/pvr/spg.cpp (E3) */ \
	X(EV_AICA_INTERRUPT,      "dc.aica.interrupt",            "aica",     "info", EVR_HIGH, "dc.aica")       /* hw/aica/aica_mmio.cpp UpdateSh4Ints (to the SH4), hw/arm7/SoundCPU.cpp update_e68k (to the ARM7) (E3) */ \
	X(EV_AICA_TIMER,          "dc.aica.timer",                "aica",     "info", EVR_HIGH, "dc.aica")       /* hw/aica/aica.h AicaTimer::StepTimer (E3) */ \
	X(EV_AICA_KEY_ON,         "dc.aica.channels.key_on",      "aica",     "info", EVR_LOW,  "dc.aica.sgc")   /* hw/aica/sgc_if.cpp (E3) */ \
	X(EV_AICA_KEY_OFF,        "dc.aica.channels.key_off",     "aica",     "info", EVR_LOW,  "dc.aica.sgc")   /* hw/aica/sgc_if.cpp (E3) */ \
	X(EV_AICA_LOOP,           "dc.aica.channels.loop",        "aica",     "info", EVR_HIGH, "dc.aica.sgc")   /* hw/aica/sgc_if.cpp (E3) */ \
	X(EV_GDROM_COMMAND,       "dc.gdrom.command",             "gdrom",    "info", EVR_LOW,  "dc.gdrom")      /* hw/gdrom/gdromv3.cpp (E3) */ \
	X(EV_MAPLE_DMA,           "dc.maple.dma",                 "maple",    "info", EVR_HIGH, "dc.maple")      /* hw/maple/maple_if.cpp (E3) */ \
	X(EV_SYSTEM_RESET,        "dc.system.reset",              "system",   "info", EVR_LOW,  "dc")            /* libswirl.cpp (E4a) */ \
	X(EV_SYSTEM_STATE,        "dc.system.state",              "system",   "info", EVR_LOW,  "dc")            /* libswirl.cpp: lifetime changes, load state (E4a) */ \
	X(EV_DEBUGGER_ENGINE,     "dc.debugger.engine",           "debugger", "info", EVR_LOW,  "dc")            /* debugger/dbg_emu.cpp: switch to or from the fallback loop of the SH4 (E4a); the ARM7 starts or stops running on its interpreter fallback (M2) */ \
	X(EV_DEBUGGER_DSP_FORCED, "dc.debugger.dsp_forced",       "debugger", "info", EVR_LOW,  "dc")            /* nothing raises it: per-sample forcing is transitional and not built; no node lists it */ \
	X(EV_DEBUGGER_NOTE,       "dc.debugger.note",             "debugger", "info", EVR_LOW,  "dc")            /* debugger/dbg_core.cpp: the grace period expired (appended with the gates closed), a client of another protocol version (E5) */

namespace dbg {

enum EventRate {
	EVR_LOW,
	EVR_HIGH,
};

// EV_ID_COUNT is the number of events, not an event.
enum EventId {
#define DBG_EVENT_ID(id, name, subsystem, severity, rate, node) id,
	DBG_EVENT_LIST(DBG_EVENT_ID)
#undef DBG_EVENT_ID
	EV_ID_COUNT
};

struct EventInfo {
	const char* name;
	const char* subsystem;
	const char* severity;
	const char* node;
	EventRate rate;
};

// The log keeps this many entries; every tick carries all of them, oldest first.
const unsigned kEventRingSize = 200;

// A formatted event message is cut to this many bytes, terminator included.
const unsigned kEventMessageMax = 256;

// The table, EV_ID_COUNT rows indexed by EventId. Any thread; the rows and their strings live for the whole
// process.
inline const EventInfo* EventTable() {
	static const EventInfo table[EV_ID_COUNT] = {
#define DBG_EVENT_ROW(id, name, subsystem, severity, rate, node) { name, subsystem, severity, node, rate },
		DBG_EVENT_LIST(DBG_EVENT_ROW)
#undef DBG_EVENT_ROW
	};
	return table;
}

// The id of a wire name (exact, case-sensitive match), EV_ID_COUNT when the name is not in the table.
// Any thread.
inline EventId FindEvent(const char* name) {
	const EventInfo* table = EventTable();
	for (int id = 0; id < EV_ID_COUNT; id++) {
		if (strcmp(table[id].name, name) == 0)
			return (EventId)id;
	}
	return EV_ID_COUNT;
}

}
