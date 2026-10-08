/*
	This file is part of libswirl
*/
#include "license/bsd"


#include "SuperH4_impl.h"
#include "sh4_interpreter.h"
#include "sh4_core.h"
#include "sh4_mmio.h"
#include "sh4_mem.h"
#include "sh4_sched.h"
#include "sh4_interrupts.h"
#include "sh4_dbg.h"
#include "debugger/debugger.h"

// The slice end is in both of the functions below, with nothing between them and it: one of them is every
// slice end of the recompilers
#if BUILD_COMPILER == COMPILER_VC
#define SLICE_INLINE __forceinline
#else
#define SLICE_INLINE inline __attribute__((always_inline))
#endif

// The slice end of a machine whose debugger wants something of it
[[maybe_unused]] static NOINLINE int slice_end_attention()
{
    // A debugger stop is decided: no emulated time passes and no scheduler callback runs until the
    // emulator thread has left the park context again
    if (dbg::g_stop_latched)
        return 1;

    Sh4cntx.sh4_sched_next -= SH4_TIMESLICE;
    if (Sh4cntx.sh4_sched_next < 0)
        sh4_sched_tick(SH4_TIMESLICE);

    // Behind the scheduler tick: what a callback of this slice asked the debugger for is seen in this call
    if (dbg::g_attn.load(std::memory_order_relaxed))
        dbg::emu::SliceHook();

    return Sh4cntx.interrupt_pend | (sh4_int_bCpuRun == false);
}

// every SH4_TIMESLICE cycles
static SLICE_INLINE int slice_end()
{
    // The one test a slice pays for the debugger. The attention word is not zero while a call or a pause
    // waits for the slice hook, and for as long as the stop latch is set (ATTN_LATCHED), so the latch is
    // looked at only behind it
    if (unlikely(dbg::g_attn.load(std::memory_order_relaxed)))
        return slice_end_attention();

    //this is an optimisation (mostly for ARM)
    //makes scheduling easier !
    //update_fp* tmu=pUpdateTMU;

    Sh4cntx.sh4_sched_next -= SH4_TIMESLICE;
    if (Sh4cntx.sh4_sched_next < 0)
    {
        sh4_sched_tick(SH4_TIMESLICE);

        // what a callback of this tick asked the debugger for is seen in this call
        if (unlikely(dbg::g_attn.load(std::memory_order_relaxed)))
            dbg::emu::SliceHook();
    }

    // Force an interrupt check if the cpu has been stopped
    // ngen is required to only check the bCpuRun on interrupt processing
    return Sh4cntx.interrupt_pend | (sh4_int_bCpuRun == false);
}

int UpdateSystem()
{
    return slice_end();
}

int UpdateSystem_INTC()
{
    if (slice_end())
        return UpdateINTC();
    else
        return 0;
}


/// SuperH4_impl



void SuperH4_impl::SetA0Handler(Area0Hanlders slot, MMIODevice* dev) {
    devices[slot].reset(dev);
}

MMIODevice* SuperH4_impl::GetA0Handler(Area0Hanlders slot) {
    return devices[slot].get();
}

bool SuperH4_impl::setBackend(SuperH4Backends backend) {
    switch (backend)
    {
    case SH4BE_INTERPRETER:
        sh4_backend.reset(Get_Sh4Interpreter());
        break;
#if FEAT_SHREC != DYNAREC_NONE
    case SH4BE_DYNAREC:
        sh4_backend.reset(Get_Sh4Recompiler());
        break;
#endif

    default:
        return false;
    }

    backend_kind = backend;

    // The slice counters outlive a backend object: the reset that this backend has not seen is made on it
    if (slice_reset_owed)
        sh4_backend->ResetSlice();

    return sh4_backend->Init();
}

void SuperH4_impl::Run() {
    slice_reset_owed = false;

    // the host FP mode is per thread: FPSCR was last written by another one (reset, load state, the thread before)
    sh4_host_fp_resync();

    // A JIT without breakpoint checks of its own runs the guest through the interpreter fallback while the
    // debugger has a breakpoint that can stop. Asked once per entry: the answer changes only between two
    // calls of Run()
    if (dbg::emu::UseFallbackLoop())
        sh4_fallback_loop();
    else
        sh4_backend->Loop();
}

// Any thread. A stop request also ends a debugger stop and a reset as reasons for the machine to count as
// running (IsRunning): from here on it does so by the request alone, which the machine has recorded
// (stopping). The park loop is woken so that the emulator thread takes the request
void SuperH4_impl::Stop()
{
    resetting = false;

    StopForReset();
}

void SuperH4_impl::StopForReset()
{
    sh4_int_bCpuRun = false;
    dbg::g_parked = false;

    dbg::emu::Kick();
}

void SuperH4_impl::Start()
{
    sh4_int_bCpuRun = true;
}

// One instruction, executed and charged by the backend as its Loop() would
void SuperH4_impl::Step()
{
    sh4_backend->Step();
}

void SuperH4_impl::Skip()
{
    if (sh4_int_bCpuRun)
    {
        printf("Sh4 Is running, can't Skip\n");
    }
    else
    {
        next_pc += 2;
    }
}

void SuperH4_impl::Reset(bool Manual)
{
    if (sh4_int_bCpuRun)
    {
        printf("Sh4 Is running, can't Reset\n");
    }
    else
    {
        next_pc = 0xA0000000;

        memset(r, 0, sizeof(r));
        memset(r_bank, 0, sizeof(r_bank));

        gbr = ssr = spc = sgr = dbr = vbr = 0;
        mac.full = pr = fpul = 0;

        sh4_sr_SetFull(0x700000F0);
        old_sr.status = sr.status;
        UpdateSR();

        // SH7091 hardware manual, table 2.1: FPSCR is H'00040001 after a reset (DN = 1, RM = 01)
        fpscr.full = 0x00040001;
        old_fpscr = fpscr;
        UpdateFPSCR();

        //Any more registers have default value ?
        printf("Sh4 Reset\n");

        //Clear cache
        sh4_backend->ClearCache();

        // the slice in progress belongs to the machine before the reset
        sh4_backend->ResetSlice();
        slice_reset_owed = true;

        // reset mmrs/modules
        sh4mmr->Reset();

        // reset devices
        for (const auto& dev : devices)
            dev->Reset(Manual);
    }
}

// A machine counts as running for as long as its emulator thread has not taken its stop callback: also one
// that the debugger has stopped (latched, parked, in a step), one that is being reset, and one that has been
// asked to stop and is on its way there. The host UI stops each of them like a running one
//
// The first three hand over to one another on the emulator thread, the next one set before the one before it
// is cleared. Read in this order, the cpu's a second time behind them, no hand-over is read as a machine that
// does not run. A stop request sets stopping before it clears the others, on any thread: it is read last
bool SuperH4_impl::IsRunning()
{
    return sh4_int_bCpuRun || dbg::g_parked || resetting || sh4_int_bCpuRun || stopping;
}

SuperH4_impl::SuperH4_impl() {

}

bool SuperH4_impl::Init()
{
    verify(sizeof(Sh4cntx) == 448);

    memset(&p_sh4rcb->cntx, 0, sizeof(p_sh4rcb->cntx));

    setBackend(SH4BE_INTERPRETER);

    // init devices
    for (const auto& dev : devices)
        if (!dev->Init())
            return false;

    return true;
}

void SuperH4_impl::Term()
{
    verify(!sh4_cpu->IsRunning());

    for (const auto& dev : devices)
        dev->Term();
    
    for (auto& dev : devices)
        dev.reset();
    
    sh4mmr.reset();

    sh4_sched_cleanup();

    sh4_backend.reset();

    printf("Sh4 Term\n");
}

void SuperH4_impl::ResetCache() {
    sh4_backend->ClearCache();
}

void SuperH4_impl::serialize(void** data, unsigned int* total_size) {
    for (int i = 0; i < A0H_MAX; i++) {
        sh4_cpu->GetA0Handler((Area0Hanlders)i)->serialize(data, total_size);
    }

    sh4mmr->serialize(data, total_size);
}

void SuperH4_impl::unserialize(void** data, unsigned int* total_size) {
    for (int i = 0; i < A0H_MAX; i++) {
        sh4_cpu->GetA0Handler((Area0Hanlders)i)->unserialize(data, total_size);
    }

    sh4mmr->unserialize(data, total_size);
}

SuperH4* SuperH4::Create() {

    auto rv = new SuperH4_impl();

    return rv;
}