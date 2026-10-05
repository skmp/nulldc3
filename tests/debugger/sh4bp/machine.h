/*
	The machine of the sh4bp gates: an SH4 with RAM, a time base and one interrupt source, and a stand-in for
	the part of the debugger that the breakpoint mechanism is not, run control (dbg::emu).

	What is real here is what the gates are about: the interpreter, the decoder, the x64 backend with its main
	loop and block manager, the interpreter fallback, and dbg::sh4 with the dbg_sh4_* helpers. What stands in:

	  time          UpdateSystem() counts slices; the scheduler time is 448 cycles per slice
	  interrupts    the real interrupt controller of the SH4; the machine requests IRL9 at every n-th slice end,
	                a guest write to 0xA0600000 takes the request back
	  memory        16 MiB of RAM at 0x0C000000 with its mirrors, behind memory handlers. A page that the block
	                manager locks stays locked until a guest write hits it: the write then goes the way a
	                write fault goes in the emulator (bm_LockedWrite)
	  run control   LatchStop records the stop; the park context is machine_resume() and the machine_step*()
	                functions, which leave and enter it in the order dbg::emu::Park() does, and the test that
	                calls them stands where a client of the debugger stands
*/
#pragma once
#include "types.h"

#include "hw/sh4/sh4_if.h"
#include "debugger/dbg_sh4.h"

#include <functional>

enum Engine
{
	ENGINE_JIT,			//the x64 backend, checks and steps native to it
	ENGINE_INTERP,		//the interpreter
	ENGINE_FALLBACK,	//the x64 backend with the debugger told to use the interpreter fallback (Debugger:ForceFallback)
	ENGINE_COUNT,
};

const char* engine_name(Engine engine);

//What the machine does on its own while the guest runs. machine_reset() clears it
struct MachineKnobs
{
	u32 irq_period;			//request the interrupt at every n-th slice end. 0: never
	u64 host_stop_slice;	//the host stops the machine at the first slice end of the backend loop that is this one
							//or a later one, counted from the reset: not inside a SLEEP wait. 0: never
	u32 pause_every;		//a client asks for a pause at every n-th slice end
	u32 excursion_every;	//a call that needs the park context arrives at every n-th slice end
	std::function<void()> excursion;	//what that call does
};

extern MachineKnobs knobs;

//State, as the gates compare it
struct MachineState
{
	u64 sched;		//scheduler time
	int counter;	//cycles left in the running slice
	u32 pc;
	u64 ctx;		//hash of the SH4 context: every register a guest can see
	u64 ram;		//hash of the RAM the guest programs use

	u64 time() const { return sched + 448 - counter; }

	bool operator==(const MachineState& o) const
	{
		return sched == o.sched && counter == o.counter && pc == o.pc && ctx == o.ctx && ram == o.ram;
	}
	bool operator!=(const MachineState& o) const { return !(*this == o); }
};

//How a run or a step ended
struct MachineStop
{
	bool debugger;			//a debugger stop; false: the host stopped the machine (knobs.host_stop_slice)
	dbg::StopCause cause;	//of a debugger stop
	u32 id;					//breakpoint id, for StopCause::Breakpoint
	u32 pc;
};

struct MachineCounters
{
	u64 slices;			//UpdateSystem() calls that passed time
	u64 stops;			//debugger stops, steps included
	u64 pauses;			//of them, pauses
	u64 excursions;
	u64 irqs;			//interrupt requests made
	u64 stray;			//guest accesses outside of RAM and the device
};

extern MachineCounters counters;

//Once
void machine_init();

//The machine as after a reset, with the guest image loaded, on this engine: reset state of the SH4 (pc
//0xA0000000) but SR = MD only, vbr = G_vbr and r15 = the stack; slice counters full; code cache empty; no
//breakpoint, everything active; knobs and counters cleared. The machine is parked
void machine_reset(Engine engine);

Engine machine_engine();

//The debugger writes the pc
void machine_set_pc(u32 pc);

//The debugger writes SR
void machine_set_sr(u32 sr);

MachineState machine_state();

//SH4 time: the scheduler's, and what the running slice has used
u64 machine_time();

//The enabled listed SHcode breakpoints: ids[i] at addrs[i]. In the park context
void machine_breakpoints(const u32* ids, const u32* addrs, u32 n);

//control.runUntil: leaves the park context and runs until the next stop
MachineStop machine_resume();

//control.step and friends. An error of dbg::sh4::StepCheck comes back in *error (with *why) and nothing runs
MachineStop machine_step(int* error = nullptr, const char** why = nullptr);
MachineStop machine_step_over(int* error = nullptr, const char** why = nullptr);
MachineStop machine_step_out(int* error = nullptr, const char** why = nullptr);

//The reference for a step of the interpreter: the interpreter's plain loop, made by a loop of the machine's own
//over the opcode handlers, for one unit. A unit is an instruction; a conditional branch with a delay slot that is
//not taken goes on into its slot, unless the slice ended between the two and an interrupt was accepted there.
//The slice counter of this loop is its own: from the first call until the next machine_reset() it is the one
//that machine_state() and machine_time() tell. Interpreter engine only
MachineStop machine_reference_step();

//Did the last machine_resume() or step use the interpreter fallback?
bool machine_used_fallback();

//What other threads ask of a running guest, as a client does. Both are taken at a slice end of the backend loop.
//machine_request_pause: dbg::emu::RequestPause, the machine stops with StopCause::Pause.
//machine_call: dbg::emu::Call with a call that needs the park context; it runs in a service excursion (or
//at once, on the calling thread's behalf, when the machine is parked) and the function returns behind it.
void machine_request_pause();
void machine_call(std::function<void()> fn);

//The SH4 backend that runs the guest
SuperH4Backend* machine_backend();

//Guest memory, any mirror of RAM
u8* machine_ram(u32 addr);
u16 machine_read16(u32 addr);
u32 machine_read32(u32 addr);
void machine_write16(u32 addr, u16 data);
void machine_write32(u32 addr, u32 data);

//Is the RAM page of this address locked by the block manager?
bool machine_page_locked(u32 addr);
