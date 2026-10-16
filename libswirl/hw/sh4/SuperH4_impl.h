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

    void SetA0Handler(Area0Hanlders slot, MMIODevice* dev);
    MMIODevice* GetA0Handler(Area0Hanlders slot);

    bool setBackend(SuperH4Backends backend);

    SuperH4Backend* DebugBackend() { return sh4_backend.get(); }

    SuperH4_impl();

    void Run();

    void Stop();

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