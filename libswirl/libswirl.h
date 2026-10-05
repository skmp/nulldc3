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
    // when a debugger has it parked), has the user interface draw the frames it queued, runs callback and
    // ends. A second call before the thread has taken its callbacks adds its callback behind the first.
    // Without an emulator thread, or with one that has taken them, the callback runs at once, on the caller.
    // The machine counts as running (SuperH4::IsRunning) until the thread takes its callbacks: what ends the
    // user interface is a callback of Stop for as long as it does, never a call made beside a stop that is
    // on its way.
    virtual void Stop(function<void()> callback) = 0;
    virtual void Reset() = 0;
    virtual void Resume() = 0;
    virtual bool Init() = 0;
    virtual void Term() = 0;
    virtual int StartGame(const string& path) = 0;

    // The thread of the user interface. Stops the machine as Stop() does, with no callback of its own, and
    // returns when the emulator thread has ended; at once when there is none. The emulator thread uses the
    // machine, the global that holds it and the renderer of the user interface until it has ended: whoever
    // takes one of these away calls this first. The caller draws nothing while it waits, so the thread ends
    // without waiting for the frames it queued to be drawn.
    // The body here is that of a machine without an emulator thread
    virtual void StopAndWait() { }

    // Emulator thread: the guest asked for a reset of the machine
    virtual void RequestReset() = 0;

    // Emulator thread: the guest program ended. The emulator thread ends as for Stop, with no callback.
    // Returns whether this call is the one that ends it: false when the thread has been asked to stop or to
    // exit before, and the guest only executes on to the end of its time slice.
    // The body here is that of a machine without an emulator thread
    virtual bool RequestExit() { return false; }

    // Emulator thread, the CPU is stopped: sets it running, at the start of the thread and from then on
    // again, unless a stop, a reset or an exit has been requested since; then the CPU is left stopped. The
    // test and the start are one step against Stop() on another thread: a request is either seen here or
    // stops the CPU afterwards. leavePark also ends the debugger stop the thread is in (dbg::g_parked); a
    // debugger's single step keeps it. Returns whether the CPU runs.
    // The body here is that of a machine without an emulator thread
    virtual bool StartCpu(bool leavePark) { return false; }

    virtual bool HandleFault(unat addr, rei_host_context_t* ctx) = 0;
    virtual ~VirtualDreamcast() { }

    static VirtualDreamcast* Create();
};

// Holds the machine that exists, as a unique_ptr does, but for the order in which a machine goes: its
// emulator thread is ended and waited for first (StopAndWait), and only then does the holder let go of the
// machine and destroy it. The emulator thread, and the fault handler that serves it, find their machine here
// for as long as that thread exists. reset() is for the thread of the user interface.
class MachinePtr {
    VirtualDreamcast* machine = nullptr;

public:
    MachinePtr() { }
    ~MachinePtr() { reset(); }
    MachinePtr(const MachinePtr&) = delete;
    MachinePtr& operator=(const MachinePtr&) = delete;

    void reset(VirtualDreamcast* next = nullptr)
    {
        if (machine)
            machine->StopAndWait();

        VirtualDreamcast* old = machine;
        machine = next;
        delete old;
    }

    VirtualDreamcast* get() const { return machine; }
    VirtualDreamcast* operator->() const { return machine; }
    VirtualDreamcast& operator*() const { return *machine; }
    explicit operator bool() const { return machine != nullptr; }
    bool operator==(std::nullptr_t) const { return machine == nullptr; }
    bool operator!=(std::nullptr_t) const { return machine != nullptr; }
};

extern MachinePtr virtualDreamcast;
extern unique_ptr<GDRomDisc> g_GDRDisc;


// TODO: rename these
int reicast_init(int argc, char* argv[]);
void reicast_ui_loop();
void reicast_term();