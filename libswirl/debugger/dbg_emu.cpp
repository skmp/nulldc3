// Stub: dbg::emu, implemented by package E4a.
/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "dbg_emu.h"

#include <cstdio>
#include <cstdlib>

// The stub tracks no lifetime and never raises attention: the hooks do nothing, Park() says run, the state is
// always NoMachine. A call, and everything only a call or a decided stop leads to, ends the process.
[[noreturn]] static void StubReached(const char* what)
{
	fprintf(stderr, "debugger: %s reached, but dbg::emu is a stub\n", what);
	abort();
}

namespace dbg {

std::atomic<u32> g_attn(0);
bool g_stop_latched;
bool g_intc_deferred;
bool g_in_sleep;
bool g_sleep_cut;
std::atomic<bool> g_parked(false);

namespace emu {

void SliceHook() { StubReached("dbg::emu::SliceHook"); }
bool UseFallbackLoop() { return false; }
void Kick() { }

LifetimeLock::LifetimeLock() { }
LifetimeLock::~LifetimeLock() { }

void OnMachineCreated() { }
void OnMachineDestroying() { }
void OnThreadStarting() { }
void OnThreadEntered() { }
void OnThreadExiting(bool) { }
void OnGuestStateReplaced() { }

u32 Park() { return PARK_RUN; }

int Call(CallClass, std::function<void()>) { StubReached("dbg::emu::Call"); }
void SetListener(void (*)(const RunState&)) { }
void RequestPause() { }
int Resume() { StubReached("dbg::emu::Resume"); }
int Step(Target, StepKind, const char**) { StubReached("dbg::emu::Step"); }
int Reset() { StubReached("dbg::emu::Reset"); }
void SetSh4Breakpoints(const u32*, const u32*, u32) { StubReached("dbg::emu::SetSh4Breakpoints"); }
void SetEventBreakpoints(const u32*, const u8*, u32) { StubReached("dbg::emu::SetEventBreakpoints"); }
void SetActiveMask(u32) { StubReached("dbg::emu::SetActiveMask"); }

RunState GetRunState()
{
	RunState state = { Lifetime::NoMachine, 0, StopCause::NoGame, 0, 0 };
	return state;
}

bool OnEmuThread() { return false; }

void LatchStop(StopCause, u32) { StubReached("dbg::emu::LatchStop"); }
void OnEvent(EventId) { StubReached("dbg::emu::OnEvent"); }

bool GetSh4Engine(Sh4EngineInfo*) { return false; }

}

}
