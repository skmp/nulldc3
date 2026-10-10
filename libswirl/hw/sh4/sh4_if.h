/*
	This file is part of libswirl
*/
#include "license/bsd"


#pragma once
#include "types.h"
#include <memory>
#include "sh4_mmr.h"

enum Sh4RegType
{
	//GPRs
	reg_r0,
	reg_r1,
	reg_r2,
	reg_r3,
	reg_r4,
	reg_r5,
	reg_r6,
	reg_r7,
	reg_r8,
	reg_r9,
	reg_r10,
	reg_r11,
	reg_r12,
	reg_r13,
	reg_r14,
	reg_r15,

	//FPU, bank 0
	reg_fr_0,
	reg_fr_1,
	reg_fr_2,
	reg_fr_3,
	reg_fr_4,
	reg_fr_5,
	reg_fr_6,
	reg_fr_7,
	reg_fr_8,
	reg_fr_9,
	reg_fr_10,
	reg_fr_11,
	reg_fr_12,
	reg_fr_13,
	reg_fr_14,
	reg_fr_15,

	//FPU, bank 1
	reg_xf_0,
	reg_xf_1,
	reg_xf_2,
	reg_xf_3,
	reg_xf_4,
	reg_xf_5,
	reg_xf_6,
	reg_xf_7,
	reg_xf_8,
	reg_xf_9,
	reg_xf_10,
	reg_xf_11,
	reg_xf_12,
	reg_xf_13,
	reg_xf_14,
	reg_xf_15,

	//GPR Interrupt bank
	reg_r0_Bank,
	reg_r1_Bank,
	reg_r2_Bank,
	reg_r3_Bank,
	reg_r4_Bank,
	reg_r5_Bank,
	reg_r6_Bank,
	reg_r7_Bank,

	//Misc regs
	reg_gbr,
	reg_ssr,
	reg_spc,
	reg_sgr,
	reg_dbr,
	reg_vbr,
	reg_mach,
	reg_macl,
	reg_pr,
	reg_fpul,
	reg_nextpc,
	reg_sr,            //Includes T (combined on read/separated on write)
	reg_old_sr_status, //Only the status bits
	reg_sr_status,     //Only the status bits
	reg_sr_T,          //Only T
	reg_old_fpscr,
	reg_fpscr,
	
	reg_pc_dyn,        //Write only, for dynarec only (dynamic block exit address)

	sh4_reg_count,

	/*
		These are virtual registers, used by the dynarec decoder
	*/
	regv_dr_0,
	regv_dr_2,
	regv_dr_4,
	regv_dr_6,
	regv_dr_8,
	regv_dr_10,
	regv_dr_12,
	regv_dr_14,

	regv_xd_0,
	regv_xd_2,
	regv_xd_4,
	regv_xd_6,
	regv_xd_8,
	regv_xd_10,
	regv_xd_12,
	regv_xd_14,

	regv_fv_0,
	regv_fv_4,
	regv_fv_8,
	regv_fv_12,

	regv_xmtrx,
	regv_fmtrx,

	NoReg=-1
};

//Varius sh4 registers

union sr_status_t
{
	struct
	{
		u32 T_h     : 1;//<<0
		u32 S       : 1;//<<1
		u32 rsvd0   : 2;//<<2
		u32 IMASK   : 4;//<<4
		u32 Q       : 1;//<<8
		u32 M       : 1;//<<9
		u32 rsvd1   : 5;//<<10
		u32 FD      : 1;//<<15
		u32 rsvd2   : 12;//<<16
		u32 BL      : 1;//<<28
		u32 RB      : 1;//<<29
		u32 MD      : 1;//<<20
		u32 rsvd3   : 1;//<<31
	};
	u32 status;
};

//Status register bitfield
struct sr_t
{
	union
	{
		struct
		{
			u32 T_h     : 1;//<<0
			u32 S       : 1;//<<1
			u32 rsvd0   : 2;//<<2
			u32 IMASK   : 4;//<<4
			u32 Q       : 1;//<<8
			u32 M       : 1;//<<9
			u32 rsvd1   : 5;//<<10
			u32 FD      : 1;//<<15
			u32 rsvd2   : 12;//<<16
			u32 BL      : 1;//<<28
			u32 RB      : 1;//<<29
			u32 MD      : 1;//<<20
			u32 rsvd3   : 1;//<<31
		};
		u32 status;
	};
	u32 T;

};

//FPSCR (fpu status and control register) bitfield
struct fpscr_t
{
	union
	{
		u32 full;
		struct
		{
			u32 RM         : 2;
			u32 finexact   : 1;
			u32 funderflow : 1;
			u32 foverflow  : 1;
			u32 fdivbyzero : 1;
			u32 finvalidop : 1;
			u32 einexact   : 1;
			u32 eunderflow : 1;
			u32 eoverflow  : 1;
			u32 edivbyzero : 1;
			u32 einvalidop : 1;
			u32 cinexact   : 1;
			u32 cunderflow : 1;
			u32 coverflow  : 1;
			u32 cdivbyzero : 1;
			u32 cinvalid   : 1;
			u32 cfpuerr    : 1;
			u32 DN         : 1;
			u32 PR         : 1;
			u32 SZ         : 1;
			u32 FR         : 1;
			u32 pad        : 10;
		};
		struct
		{
			u32 _nil   : 2+1+1+1+1+4+8+1;
			u32 PR_SZ : 2;
			u32 nilz  : 11;
		};
	};
};

//SH7091: FPSCR has 22 bits, a console reads 0x003FFFFF back after it loaded 0xFFFFFFFF
#define FPSCR_MASK 0x003FFFFF

//RM = 01 rounds to zero. RM = 10 and 11 are reserved and the chip is not measured for them: they round by bit 0,
//as the RTL (v1_fpu: rz = fpscr[0]) and the soft-float reference (sh4_fpdn.h) of shrike4-rtl do
#define FPSCR_RM_ZERO(fpscr_v) (((fpscr_v).RM & 1) != 0)

//Puts the host FP control register of the calling thread into the mode of FPSCR. That register belongs to a thread:
//a thread that runs the CPU calls this first, FPSCR may have been written on another one (reset, load state)
void sh4_host_fp_resync();


typedef void RunFP();
typedef void StopFP();
typedef void StartFP();
typedef void StepFP();
typedef void SkipFP();
typedef void ResetFP(bool Manual);
typedef void InitFP();
typedef void TermFP();
typedef bool IsCpuRunningFP();

/*
	The interface stuff should be replaced with something nicer
*/
//sh4 interface

struct MMIODevice;
struct SystemBus;
struct SuperH4Mmr;

enum SuperH4Backends {
    SH4BE_INTERPRETER,
    SH4BE_DYNAREC
};


enum Area0Hanlders {
	A0H_BIOS,
	A0H_FLASH,
	A0H_GDROM,
	A0H_SB,
	A0H_PVR,
	A0H_EXTDEV_006,
	A0H_AICA,
	A0H_RTC,
	A0H_EXTDEV_010,

	A0H_MAPLE,
	A0H_ASIC,
	A0H_SPG,
	A0H_SCPU,
	A0H_DSP,

	A0H_MAX
};

struct SuperH4Backend;

struct SuperH4 {
	VLockedMemory mram;
	VLockedMemory vram;
	VLockedMemory aica_ram;
	unique_ptr<SuperH4Mmr> sh4mmr;

	virtual void SetA0Handler(Area0Hanlders slot, MMIODevice* dev) = 0;
	virtual MMIODevice* GetA0Handler(Area0Hanlders slot) = 0;

	template<class T>
	T* GetA0H(Area0Hanlders slot) {
		return dynamic_cast<T*>(GetA0Handler(slot));
	}

    virtual bool setBackend(SuperH4Backends backend) = 0;

    // The backend object Run() drives: the one the last setBackend() installed. Init() installs the interpreter
    // and every emulator thread start installs the configured one, so the pointer is never kept across a thread
    // start. Null before Init() and after Term(), and from a SuperH4 that does not give its backend out (the
    // default): a caller takes null as a backend without capabilities.
    // For the debugger (run control: DebugCaps, Step, SliceCyclesLeft; the SH4 breakpoint state:
    // DebugDiscardPage, DebugDiscardFlow; the memory writer of dbg_mem: DebugDiscardPage) and for the sound
    // regression harness (DebugCaps); it is there in builds without the debugger as well. Emulator thread, or
    // the thread that holds the debugger's lifetime lock while no emulator thread exists.
    virtual SuperH4Backend* DebugBackend() { return nullptr; }

    virtual bool Init() = 0;
    virtual void Reset(bool Manual) = 0;
    virtual void Term() = 0;

    virtual void Run() = 0;
    virtual void Stop() = 0;
    virtual void Start() = 0;
    virtual void Step() = 0;
    virtual void Skip() = 0;

    virtual bool IsRunning() = 0;

    virtual void ResetCache() = 0;

	virtual ~SuperH4() { }

	virtual void serialize(void** data, unsigned int* total_size) { }
	virtual void unserialize(void** data, unsigned int* total_size) { }

	static SuperH4* Create();
};

struct Sh4Context
{
	union
	{
		struct
		{
			f32 xffr[32];
			u32 r[16];

			union
			{
				u64 full;
				struct
				{
					u32 l;
					u32 h;
				};
			} mac;

			u32 r_bank[8];

			u32 gbr,ssr,spc,sgr,dbr,vbr;
			u32 _pr,fpul;
			u32 pc;

			u32 jdyn;

			sr_t sr;
			fpscr_t fpscr;
			sr_status_t old_sr;
			fpscr_t old_fpscr;

			// The run flag: not 0 while the loop of the backend goes on. The generated code of both recompilers
			// loads its 32 bits at this offset and leaves its loop on 0: the word keeps its place, its size
			// and that meaning. An AtomicU32 (oslib/atomic_u32.h) is those 32 bits, accessed atomically.
			// Written by the emulator thread (set where it enters its loop, cleared for a debugger stop, behind
			// a step and at its end) and by any thread that asks the machine to stop (SuperH4_impl::Stop).
			// Every store is sequentially consistent: none is on a path where its cost counts.
			// Whether the machine counts as running is not read from this word: it is 0 for a machine that
			// is parked, being reset or on its way to its stop, and SuperH4_impl::IsRunning() has a word of
			// the machine's own for that.
			// The emulator thread polls the word where its loop decides whether it goes on, with a relaxed
			// load, which is the load the generated code makes (mov, ldr): what it takes from the value is
			// whether to leave the loop, at this poll or the next; what it does behind the loop it reads under
			// a lock or from state of its own. Every other read is sequentially consistent.
			AtomicU32 CpuRunning;

			int sh4_sched_next;
			u32 interrupt_pend;
		};
		u64 raw[64-8];
	};

};

u32 sh4context_offset_u32(u32 sh4_reg);
u32 sh4context_offset_regtype(Sh4RegType sh4_reg);

void DYNACALL do_sqw_mmu(u32 dst);
extern "C" void DYNACALL do_sqw_nommu_area_3(u32 dst, u8* sqb);
extern "C" void DYNACALL do_sqw_nommu_area_3_nonvmem(u32 dst, u8* sqb);
void DYNACALL do_sqw_nommu_full(u32 dst, u8* sqb);

typedef void DYNACALL sqw_fp(u32 dst,u8* sqb);
typedef void DYNACALL TaListVoidFP(void* data);

#define FPCB_SIZE (RAM_SIZE_MAX/2)
#define FPCB_MASK (FPCB_SIZE -1)
//#defeine FPCB_PAD 0x40000
#define FPCB_PAD 0x100000
#define FPCB_OFFSET (-(FPCB_SIZE*sizeof(void*) + FPCB_PAD)) 
struct Sh4RCB
{
	void* fpcb[FPCB_SIZE];
	u64 _pad[(FPCB_PAD-sizeof(Sh4Context)-64-sizeof(void*)*2)/8];
	TaListVoidFP* tacmd_voud; //*TODO* remove (not used)
	sqw_fp* do_sqw_nommu;
	u64 sq_buffer[64/8];
	Sh4Context cntx;
};

extern "C" Sh4RCB* p_sh4rcb;
extern u8* sh4_dyna_rcb;

INLINE u32 sh4_sr_GetFull()
{
	return (p_sh4rcb->cntx.sr.status & 0x700083F2) | p_sh4rcb->cntx.sr.T;
}

INLINE void sh4_sr_SetFull(u32 value)
{
	p_sh4rcb->cntx.sr.status=value & 0x700083F2;
	p_sh4rcb->cntx.sr.T=value&1;
}

#define do_sqw_nommu sh4rcb.do_sqw_nommu

template<typename T>
s32 rcb_noffs(T* ptr)  
{ 
	s32 rv= (s32)((u8*)ptr - (u8*)p_sh4rcb-sizeof(Sh4RCB)); 
	verify(rv<0);

	return rv;
}

template<typename T>
s32 rcb_poffs(T* ptr)  
{ 
	s32 rv= (u8*)ptr - (u8*)p_sh4rcb-sizeof(Sh4RCB); 
	verify(rv>=0);

	return rv;
}

#define sh4rcb (*p_sh4rcb)
#define Sh4cntx (sh4rcb.cntx)

// What a backend does for the debugger: the bits of SuperH4Backend::DebugCaps()
enum {
	// Breakpoint checks, Step() and the flow reports of a step-out are implemented: the debugger checks
	// (debugger/debugger.h) are made by this backend's own code, in front of the instructions the debugger
	// armed. A JIT without it gets its breakpoints and steps from the interpreter fallback (sh4_dbg.h).
	SH4DBG_NATIVE = 1,

	// Sh4cntx is complete, pc included, whenever the backend calls UpdateSystem(): at a slice end, and from the
	// SLEEP handler inside a block (a JIT that sets the bit closes every span, the FP registers included, in
	// front of the SLEEP fallback). Without it the context is complete only after Loop() returned.
	// A backend states the bit itself when it meets all of this; none does by default. The interpreter and the
	// x64 JIT do; the arm64 JIT does not. Whatever reads SH4 state inside
	// UpdateSystem() reads it only under the bit (the debugger's slice hook, the sound regression harness), so
	// its output for a backend changes at the point where that backend starts to report the bit.
	SH4DBG_CTX_AT_SLICE = 2,
};

struct SuperH4Backend {
    virtual bool Init() = 0;
    virtual void Loop() = 0;
    virtual void ClearCache() = 0;

    /*
        Debugger support. It is called on the emulator thread between two calls of Loop(), when no guest
        code of this backend is on the host stack. DebugCaps() and SliceCyclesLeft() are also called from
        inside UpdateSystem(). DebugCaps(), SliceCyclesLeft() and ResetSlice() are also called, while no
        emulator thread exists, by the thread that holds the debugger's lifetime lock (ResetSlice: a reset
        from the host UI). The defaults are those of a backend that implements none of it.

        The slice counter is the number of cycles left until the next UpdateSystem(). A backend with
        SH4DBG_NATIVE keeps it when Loop() returns and continues from it when Loop() or Step() is entered again.
        A Loop() entered with a counter that is not positive and no remainder to run (dbg_sh4_TakeRemainder,
        debugger/debugger.h: a flow report stopped behind the block that used the slice up) does the
        end-of-slice work before its first block. After a remainder it tests the counter as after any block.
        Step() does not apply this rule: entered with a counter that is not positive it executes its
        instruction first and does the end-of-slice work behind it. After a flow report that stopped behind
        the block that used the slice up, that is one instruction ahead of an undebugged run (a documented
        skew of a debugger stop).
    */

    // SH4DBG_* bits. Constant for the life of the backend.
    virtual u32  DebugCaps()                              { return 0; }

    // Executes the instruction at pc, a delayed branch together with its slot whether or not it is taken,
    // through the same decoder, code generator and cost function as Loop(); charges it to the slice counter
    // as a block of that one instruction and, if the slice ran out, does the end-of-slice work Loop() does
    // (UpdateSystem, interrupt acceptance). A debugger check on the instruction is still made. Nothing it
    // compiles enters the block cache; blocks already cached stay, except where Loop() itself would clear
    // the cache at this pc (the boot addresses, a code cache that is nearly full).
    // The caller has verified that pc is even and that the instruction, and the slot of a delayed branch,
    // can be read and executed; CpuRunning is set for the duration.
    // Returns false, having done nothing, when the backend cannot step.
    virtual bool Step()                                   { return false; }

    // The slice counter that the guest code executed last on this backend was charged to: Loop()'s, or the
    // interpreter fallback's from sh4_fallback_loop() / sh4_fallback_step() until Loop() runs again. SH4 time
    // is sh4_sched_now64() + SH4_TIMESLICE - SliceCyclesLeft(). 0 when the backend does not expose it.
    virtual int  SliceCyclesLeft()                        { return 0; }

    // Sets the slice counter(s) of this backend to SH4_TIMESLICE; a JIT also the one of the interpreter
    // fallback (sh4_dbg.h). Called by SuperH4_impl for a machine reset: in Reset() on the installed backend,
    // and after a setBackend() that follows a Reset() which no emulator thread has run since (StartGame resets
    // while Init()'s interpreter is installed; dc_run installs the configured backend afterwards).
    virtual void ResetSlice()                             { }

    // Discards every compiled block that overlaps the 4 KiB page of the guest address, in every mirror of
    // that page, and nothing else: page protection and the marks of pages that hold data stay as they are.
    // Called when the debugger arms or disarms the address, and when the debugger writes main memory on a
    // host without the virtual memory layout.
    virtual void DebugDiscardPage(u32 addr)               { }

    // Step-out: false discards every compiled block that ends with a call or a return, so that they are
    // compiled again with a flow report; true discards the blocks that have one.
    virtual void DebugDiscardFlow(bool instrumented_only) { }

	virtual ~SuperH4Backend() {  }
};

//Get an interface to sh4 interpreter
SuperH4Backend* Get_Sh4Interpreter();
SuperH4Backend* Get_Sh4Recompiler();

u32* GetRegPtr(u32 reg);
