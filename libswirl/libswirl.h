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
// The phases a sound burst can be suspended in (VirtualDreamcast::SoundBurstSuspended)
enum : u32 { SOUND_BURST_NONE = 0, SOUND_BURST_ARM7 = 1 };

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

    // Emulator thread. Whether a sound burst is suspended, and in which phase: 0 none; SOUND_BURST_ARM7: the
    // AICA scheduler callback returned in front of an ARM7 instruction because the debugger asked
    // (dbg_arm7_Check), and the rest of that burst has not run. Not 0 only between such a return and the
    // moment the park loop, dc_run or Reset() has dealt with it; the stop latch is set for all of that time
    // but for the slice end in which it happened.
    // The body here is that of a machine that never suspends
    virtual u32 SoundBurstSuspended() { return 0; }

    // Emulator thread, the CPU is stopped. Runs a suspended sound burst on, outside the scheduler: no emulated
    // time passes (it is the rest of the AICA tick that was due), and an interrupt it raises waits for the next
    // acceptance of the SH4. checks true: the ARM phases use the debugger's check while dbg_arm7_armed is not 0
    // (the park loop). checks false: nothing can stop it (dc_run, in front of the stop callback of a host stop
    // or a guest exit). Returns true when the burst ran to its end, the rest of the callback included (the tick
    // of the sound regression harness: once per burst, behind the last sample), and at once when none is
    // suspended. Returns false when it is suspended again, which it never is with checks false.
    // The body here is that of a machine that never suspends
    virtual bool RunSoundBurst(bool checks) { return true; }

    // Emulator thread. What the sound bursts of this machine have done since it was created: out[0] the
    // bursts the scheduler started (calls of the AICA callback), out[1] the ARM phases that ran to their end
    // (SoundCPU::RunSample returned true), out[2] the time steps of the AICA that a burst made, out[3] the
    // tails (the rest of the callback behind the last sample: once per burst, wherever the burst was run on).
    // The counts wrap, are in no save state and in no hash, and nothing of the guest reads them. For the
    // selftests of the debugger.
    // The body here is that of a machine that counts nothing
    virtual void SoundBurstCounts(u32 out[4]) { out[0] = out[1] = out[2] = out[3] = 0; }

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