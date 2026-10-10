/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once

#include "types.h"
#include <atomic>
#include <memory>

struct SuperH4Backend;
struct MMIODevice;

struct SuperH4_impl final : SuperH4 {
    unique_ptr<MMIODevice> devices[A0H_MAX];
    unique_ptr<SuperH4Backend> sh4_backend;

    // The kind of sh4_backend: what the last setBackend() that found a backend was asked for
    SuperH4Backends backend_kind = SH4BE_INTERPRETER;

    // Reset() ran and no emulator thread has entered Run() since: a backend installed in between starts with
    // a fresh slice counter (SuperH4Backend::ResetSlice)
    bool slice_reset_owed = false;

    // The machine has an emulator thread that has not taken its stop callback: from the moment the host starts
    // the thread (before it exists) until the thread takes the callback, whatever its cpu does in between. It
    // is all that IsRunning() reads. The machine writes it under its callback_lock, beside the thread_live of
    // its own that its stop requests go by, so that any thread can read the same fact without that lock
    std::atomic<bool> thread_live { false };

    // A reset of the machine is on its way: the emulator thread has stopped the cpu for it and starts it again
    // behind it. The machine sets it, before StopForReset() and only while it has not been asked to stop;
    // Stop() clears it, and the emulator thread when the reset is done.
    // Nothing reads it: it was a term of IsRunning(), which is thread_live now. The machine counts as running
    // all through a reset as it did, so that the host stops it as it stops a running one
    std::atomic<bool> resetting { false };

    // The machine has been asked to stop, or its guest has ended, and the emulator thread has not taken its
    // stop callback yet: on its way there it waits for the user interface to draw the frames it queued. The
    // machine sets it before it stops the cpu, and clears it where the thread takes the callback.
    // Nothing reads it: it was a term of IsRunning(), which is thread_live now. The machine counts as running
    // all the way to the callback as it did, so that a host that acts a second time stops it as it stops a
    // running one, behind the stop that is on its way, and does not end its user interface under the thread
    std::atomic<bool> stopping { false };

    void SetA0Handler(Area0Hanlders slot, MMIODevice* dev);
    MMIODevice* GetA0Handler(Area0Hanlders slot);

    bool setBackend(SuperH4Backends backend);

    SuperH4Backend* DebugBackend() { return sh4_backend.get(); }

    SuperH4_impl();

    void Run();

    void Stop();

    // Emulator thread: the cpu leaves its loop as for Stop(), for a reset of the machine: resetting is left
    // as the machine set it
    void StopForReset();

    void Start();

    void Step();

    void Skip();

    void Reset(bool Manual);

    bool IsRunning();

    bool Init();

    void Term();

    void ResetCache();

    void serialize(void** data, unsigned int* total_size);
    void unserialize(void** data, unsigned int* total_size);
};