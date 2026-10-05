/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once

#include "types.h"
#include "oslib/context.h"
#include <memory>
#include <functional>

/*
    The machine. It runs on a thread of its own, the emulator thread, which Resume() starts and which ends
    when the machine is stopped: a machine that does not run has no emulator thread.

    The emulator thread ends for a host stop (Stop) and for a guest exit (RequestExit), and resets the machine
    and runs on for a guest reset (RequestReset). When several are pending the reset is made first.
    LoadState, SaveState, Reset and Resume are for a machine without an emulator thread; Reset is also what the
    emulator thread itself calls for a guest reset.
*/
struct VirtualDreamcast {
    virtual void LoadState() = 0;
    virtual void SaveState() = 0;

    // Any thread. Stops the machine: the emulator thread leaves the CPU at the end of its time slice (at once
    // when a debugger has it parked), runs callback and ends. A second call before the thread has ended adds
    // its callback behind the first. Without an emulator thread the callback runs at once, on the caller.
    virtual void Stop(function<void()> callback) = 0;
    virtual void Reset() = 0;
    virtual void Resume() = 0;
    virtual bool Init() = 0;
    virtual void Term() = 0;
    virtual int StartGame(const string& path) = 0;

    // Emulator thread: the guest asked for a reset of the machine
    virtual void RequestReset() = 0;

    // Emulator thread: the guest program ended. The emulator thread ends as for Stop, with no callback.
    // The body here is that of a machine without an emulator thread
    virtual void RequestExit() { }

    // Emulator thread, the CPU is stopped: sets it running again unless a stop, a reset or an exit has been
    // requested since. The test and the start are one step against Stop() on another thread: a request is
    // either seen here or stops the CPU afterwards. leavePark also ends the debugger stop the thread is in
    // (dbg::g_parked); a debugger's single step keeps it. Returns whether the CPU runs.
    // The body here is that of a machine without an emulator thread
    virtual bool StartCpu(bool leavePark) { return false; }

    virtual bool HandleFault(unat addr, rei_host_context_t* ctx) = 0;
    virtual ~VirtualDreamcast() { }

    static VirtualDreamcast* Create();
};

extern unique_ptr<VirtualDreamcast> virtualDreamcast;
extern unique_ptr<GDRomDisc> g_GDRDisc;


// TODO: rename these
int reicast_init(int argc, char* argv[]);
void reicast_ui_loop();
void reicast_term();