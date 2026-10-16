# M2 round — the cut

> **This copy.** `docs/nulldc3/devtools-integration-m2-round.md` is the repository's copy of the working document
> `M2-ROUND.md` as it stands at the end of the M2 round (package M2G, 2026-10-15): the cut with the project owner's
> answers of 2026-10-11 (6.2) and the integrator's notes on every package of the round (Appendix I, I.1 to I.11). It
> replaces the copy that package R6 committed with header revision 6. The text below names its companion documents by
> their working names; in `docs/nulldc3/` they are:
>
> | Named below | In `docs/nulldc3/` |
> | --- | --- |
> | `M2-ROUND.md`, "this file", "this document" | `devtools-integration-m2-round.md` (this file) |
> | `TRACK1-DESIGN.md`, "the design" | `devtools-integration-design.md` |
> | `DESIGN-V3-OPEN.md` | `devtools-integration-open.md` |
> | `DESIGN-V3-DECISIONS.md` | `devtools-integration-decisions.md` |
> | `PLAN.md` | `PLAN.md` |
>
> "Beside this file" is the working area: the reader reports of `understand-m2/` and every path of the form
> `agents/nulldc3/...` point outside this repository (`docs/nulldc3/README.md`). The design in `docs/nulldc3/` is at
> this cut: section 7 was applied at the end of the round (package M2G). The design, the decisions and the frozen
> headers cite this file for header revision 6 (section 2), for the M2 tree (3.6.1) and for the guest's contract (5.2).

Status: cut, 2026-10-10; revised the same day after five adversarial reviews (four lenses each: sound, writes,
frontend, integration; the fourth and the fifth closing rounds, each on what the round before it had left;
Appendix R has every finding of the five rounds and what became of it). Documents only:
nothing was built or run for it. It is the document that design 6.3 asks for ("a document `M2-ROUND.md` beside this design,
which has header revision 6 and the packages of M2 with their ownership and gates"). Packages start from here;
the design text is brought to it by the list of section 7, at the end of the round. **Until then, where this
file and the design differ, this file is the contract** (M2-D64).

State it starts from. Emulator: repository `devcast`, integration branch `skmp/nulldc3`, head `d546aa76` (a
documentation commit on the merge of J1, `c0c81436`; 190 ctest targets). UI: repository `dreamcast-devtools`,
branch `skmp/nulldc3`, head `22b358e`. Design: `TRACK1-DESIGN.md` beside this file, status v3. **The design is
cited by section, never by line**: it was rewritten while the readers worked.

Inputs: the six reader reports in `understand-m2/` (`sound-burst.md`, `headers-m2.md`, `core-m2.md`,
`devices-m2.md`, `devtools-m2.md`, `guest-harness.md`), the design sections 2.2, 2.3, 2.5, 2.7, 2.8, 3.3, 4.4, 5.5,
5.6, 6.1 to 6.3 and Appendix B, `PLAN.md` (ground rules; tracks 1, 5, 6, 10) and `DESIGN-V3-DECISIONS.md`. The
readers' file and line references are not repeated here; a statement of this document that rests on one of them
says which report it is from the first time.

The method is that of round 1 and is not restated per package: one worktree and one build directory per
emulator package (`agents/nulldc3/wt/<pkg>`, branch `skmp/nulldc3-<pkg>`), a package edits only what it owns, the
shared headers are frozen and change only by a numbered revision of the integrator, every package has a gate and
is checked by a verifier who did not write it, with mutations, and the integrator merges. One workflow at a time
on this machine: `ninja -j8`, `ctest -j4`, at most four emulator processes, every emulator run headless (design
D10). **Time neutrality is the rule every package is held to: a debugger that is not used changes no
guest-visible state, and costs a run nothing but what design 5.7 names.** Its witnesses are the state hashes of
design 5.7, the sound lines of design 5.6 and, where a package touches a path every run takes, the compiled
code of that path and instruction counts (5.7; 3.4, "Host cost"). A gate is green when its tests **ran**: every gate of this cut is "none skipped" (A.0 has the configure
line that makes missing data a failure).

Names used below: decisions of this cut are **M2-D1 ...** (section 6.1), questions for the project owner
**M2-Q1 ...** (6.2), items of the header revision **H6-...** (section 2), packages **F3a, R6, E6a, E6b, E7a, E7b,
F3b, F3c, F3d, M2G** (section 3).

Contents: 1 scope · 2 header revision 6 · 3 packages · 4 order · 5 the sounding guest · 6 decisions and
questions · 7 changes to the design text · Appendix A briefs · Appendix R the five reviews.

---

## 1. Scope of M2

### 1.1 In

| What | Design | Package | As cut |
| --- | --- | --- | --- |
| The sounding guest and a runner for the sound lines | 5.6, 5.5 (the `dsptest` paragraph), 6.3 E6 "first" | E6a | a bare-metal program, not a KOS program (M2-D31); the runner and its ctest targets are in the tree |
| The resumable sound burst | 2.5.2, 2.2.3 rule 4, 2.2.4 a | E6b | the ARM phase only: state `{suspended, i, ticks_done, arm_on}`; suspension in front of an ARM7 instruction and nowhere else (M2-D1, M2-D2) |
| ARM7 code breakpoints | 2.5.3, 2.3.3 | E6b (run control), R6 (the hand-over from the core) | compared under the mask of the wave memory with bits 0 and 1 dropped; active by the category mask (M2-D7, M2-D12) |
| ARM7 step, step over, step out | 2.5.3, 2.3.2 (the `arm7` row), 3.3 | E6b | a step never publishes `Running`; from a stop outside a burst the SH4 runs to the next burst (M2-D4 to M2-D6, M2-D9) |
| `-32012` | 2.5.3, 3.3, 3.5 | E6b | for the three steps; **not** for `breakpoints.add` (M2-D11) |
| `NOTICE_ARM7_FALLBACK` | 2.5.3 | E6b | from the backend that is selected; cannot be shown on an x86-64 host (M2-D8) |
| The ARM7 register writer (`SoundCPU::DebugSetReg`) | 2.8, 6.3 E6 | E6b (body), E7a (caller, table flags) | all rows of `dc.aica.arm7` but `ENABLED`; a written PC is stored aligned (M2-D12) |
| `state.writeMemory` | 2.7, 3.3, 3.6 | E7a | main memory, video memory (both views), wave memory (both targets); everything else `-32013` (M2-D21) |
| `state.setRegister`, `state.editWatch` | 2.8, 3.3 | E7a | writable are the registers of `dc.sh4.cpu` and of `dc.aica.arm7` but `ENABLED`; a register says on the wire whether it has a writer (M2-D18 to M2-D20); an odd PC of the SH4 is refused; an FPSCR write that changes the FP mode, and a write that moves the PC of the SH4, empty the SH4's code cache (M2-D41, M2-D42) |
| `control.reset` | 2.3.2, 3.3 | E7a (wiring; `emu::Reset()` exists) | as the design has it |
| The capabilities `memory-write`, `register-write`, `reset`; `targets[arm7].stepping: true` | 3.1 | E7a; E6b through R6's query | each capability in the commit that has its method and its writer |
| The M2 rows of the device tree, and the actions `sh4-registers`, `arm7-registers` | 2.8 | E7b | 70 new nodes; **read-only**; channel registers by field (M2-D24 to M2-D30) |
| The register panel, register and memory editing, toolbar run control with reset and hotkeys, the end of protocol v2, the narrow rule of the memory view | 4.4 | F3b, F3c, F3d | 4.4, with the points it left open decided in M2-D39 |
| The contract of M2: schema ids, the mock as an M2 server, S16 with five parts, the two M1-level runner rules of 4.4 | 4.4, 5.5 | F3a | the mock passes the M2 gate and `npm run check` runs it, also as a server that can do what the M2 emulator can and no more (M2-D36 to M2-D38, M2-D45); the mock has stores and a call in its ARM7 program, and the runner the options the emulator's targets pass through (M2-D52, M2-D53) |
| The leftovers of J1: `resetting`, `stopping`, `StopForReset()` removed; three selftests read the run flag and `g_parked` | 6.1.2 items 1 and 2 | R6 | moved from E6 to the integrator's step (M2-D17) |

### 1.2 Not in M2

| What | Design row | Goes to | Why |
| --- | --- | --- | --- |
| The DSP phase of the burst machine: `phase`, `mixl`, `mixr`, the `per_sample` latch, the split of `TimeStep` into `TS_TIMERS` / `SGC` / `DSP` / `MIX` / `TS_IRQ` | 2.5.2 (state and sequence) | M3, E8 | no suspension point of M2 depends on them: today's DSP runs its 128 steps in one call with its datapath in locals and cannot be suspended inside a sample (`sound-burst.md` 2.4 C); the batch path and its setting go when the T5.0 core is wired in |
| DSP steps and breakpoints, the datapath registers of `dc.aica.dsp`, SGC capture, `state.getMemoryBinary`, the removal of the three forcing names | 2.5.4, 2.5.5, 2.8 (`dc.aica.dsp`), 3.3 | M3, E8 | the design's own milestone |
| The DSP program part of the guest | 5.5, 6.3 E8 | M3, E8 | E6a reserves its place; E8 changes the ELF and retakes the sound lines as its first step (M2-D35) |
| Writes of the AICA register file (`dc.aica.common`, `dc.aica.sgc.N`), of the RTC; the body of `AICA::DebugWriteReg` | 2.8 (silent), `aica_mmio.h` | M3, with the AICA round | the accessor's contract includes the ARM7 reset, which 2.8 says the debugger does not drive; a write inside a suspended burst has no rule; nothing of the M2 gate needs it (M2-D18, M2-Q1) |
| The state of a channel that no register holds (`AicaChannelDebug`: envelope levels, play position) as rows | 2.8 (silent) | M3 | the SGC capture of M3 carries the same quantities |
| Writes of system bus, PVR and SH4 on-chip module registers | 2.8 (silent), 4.4 ("edit through `state.setRegister`") | M4, E9 | each register needs a stated write semantics (the guest's effects, a plain store, or refused): `devices-m2.md` section 5 lists writes that start a DMA in the call, hang the emulator thread or end the process. That table, and the accessors it needs, are a design step of their own (M2-D18, M2-Q1) |
| A snapshot of the devices on the maple bus; the scan-line counters of the SPG; the three PVR tables as rows; the stored value of a write-only system bus register | 2.8 ("device snapshots") | M4, E9 | each needs a new accessor in an `hw/` header; the M2 rows are served by what exists (M2-D26, M2-D28, M2-D30) |
| The target a stop belongs to, as a field of the tick | — | not planned | a client knows it from `breakpointId` or from the step it asked for (`core-m2.md` section 5); S16 does not need it |
| Telling other clients, or the other views of one client, that memory was written | 2.9, 4.4 | not in M2 beyond the client's own refetch (M2-D39) | ticks carry no memory |
| A run of the ARM7 fallback and of its notice | 2.5.3 | A64 | no arm64 host; kept source-consistent by reading |
| A guard against a write that takes the guest to what is no instruction | — | PLAN's backlog (undefined instructions and odd PCs as guest exceptions) | in the default build such a guest ends the emulator with or without a debugger; M2 refuses an odd PC of the SH4, stores the ARM7's PC aligned, and says the rest in the help (M2-D42, M2-Q5) |
| Blocks of the SH4 recompiler keyed by the FP mode | 2.3.7 item 2 | PLAN T3.4 | M2 only keeps the debugger from making it worse: an FPSCR write that changes the FP mode empties the code cache (M2-D41, M2-Q4) |
| Blocks of the SH4 recompiler found with the address in their key (the block cache is indexed by `(pc >> 1)` under a mask of 32 MiB: addresses a multiple of 32 MiB apart share a slot — main memory at `0x0C…`, `0x8C…` and `0xAC…` does, and so do the boot ROM and main memory) | 2.3.7 item 6 | PLAN's backlog | a guest that changes mirror by itself runs the blocks of the other one, with or without a debugger. M2 only keeps the debugger from doing it to a guest: a write that moves the PC of the SH4 empties the code cache (M2-D42, M2-Q4) |
| Recorded for other tracks, not work of this round | — | PLAN tracks 5, 6, 7 | reios leaves `SCIEB` and `SCILV0-2` at 0, so the ARM7 of 2ndmix and the stock KallistiOS driver never get their timer FIQ and stay silent (`guest-harness.md` 2.3, inferred; M2-Q3); a machine reset does not reset the GD-ROM drive and leaves most system bus slots as they were (`devices-m2.md` 4.13, 8 item 22); the boot ROM workload of the sound harness is silent for an unknown cause; the build with an SH4 recompiler and without the virtual memory layout cannot resolve a write fault in a page of compiled code, so a guest that stores into such a page ends the process there (by reading; E7a's verifier makes that build once: M2-Q6) |

---

## 2. Header revision 6

Made by the integrator as package R6, before any other emulator package of the round starts. It has every
declaration that crosses a package boundary or stands in a frozen header, and every comment of a frozen header
that M2 makes false. The texts below are the texts as they stand in the headers after the revision
(indentation as the file has it). A comment is given whole where more than a sentence of it changes; "..." inside
a quoted comment stands for lines that stay as they are.

Rules of the revision:

- Appendix B of the design gains `libswirl/debugger/dbg_arm7.h` in its list of frozen headers: that is row 51
  of section 7, applied with the other rows at M2G. R6 changes no text of the design (it adds the repository's
  copy of this file: 2.4 item 6). From R6's merge on the header is frozen by this file (M2-D64).
- No existing signature changes. No enumerator is added to `ErrorCode`, `StopCause`, `Target`, `Category`,
  `CallClass`, the notices, the bounds or the event table (`headers-m2.md` 1 and 2).
- The revision is delivered with **stub bodies** and with the **wiring of the core** (2.4): the tree builds and
  behaves as before it, bit for bit.
- Two comment clauses describe a stub and can only go with the body. R6 leaves them in place and the package
  named in 2.5 removes them; that is the one edit of a frozen header a package of this round makes.
- `hw/aica/dsp.h` is not changed: its "outside Step()" stays true in M2, and the revision of M3 changes that
  block anyway (2.5.5 of the design).

### 2.1 New declarations in frozen headers

#### H6-1 `libswirl/debugger/debugger.h` — the ARM7 block

Behind the SH4 block (`dbg_sh4_TakeRemainder`), inside `#if defined(NULLDC_DEBUGGER)`. The names are global, as
those of the SH4 block are (M2-D15).

```cpp
/*
	ARM7 code breakpoints and steps: what the sound burst calls (dbg_arm7.cpp). Everything is (E) only.

	A sound burst is the work of one AICA scheduler callback (Dreamcast_impl::AicaUpdate): if the ARM7 is
	clocked, 32 times { the ARM phase of a sample: the ARM7 for its ticks; one time step of the AICA }, then
	the rest of the callback (the batch render while it exists, the tick of the sound regression harness).
	The ARM phase runs through the backend's Run() while dbg_arm7_armed is 0, and through the debug loop while
	it is not. The debug loop is, per instruction: the entry of a pending FIQ (ARM7Backend::StepIrq), the
	check below, the instruction (ARM7Backend::StepExec); it tests the ticks of the sample behind every
	instruction, as ARM7Backend::StepMany does, and while every check answers 0 it executes the sequence
	StepMany executes. It runs the interpreter's functions on the context the backend runs on: native where
	the backend is the interpreter (every x86-64 build), a fallback with other timing where it is a recompiler
	(NOTICE_ARM7_FALLBACK, dbg_emu.h).
	A burst is suspended in front of an ARM7 instruction and nowhere else. A suspended burst is state of the
	machine (VirtualDreamcast::SoundBurstSuspended, libswirl.h). It is run on by the park loop or by dc_run,
	never by the scheduler: while a burst is suspended the stop latch is set and the AICA callback cannot run.
*/

// Not 0 while the ARM phase has to use the debug loop: an enabled listed breakpoint of the ARM7 exists and
// the active mask has CategoryBit(Category::Arm7), or a temporary breakpoint exists, or a step is armed.
// Tested once per sample (dbg_arm7_armed != 0); zero keeps the ARM phase to one load and a branch. It changes
// in the park context, or under L while no emulator thread exists: never while a burst executes.
extern u32 dbg_arm7_armed;

// The check. From the debug loop only, behind the entry of a pending FIQ and in front of the instruction at
// pc (the address of the next instruction, regs[R15_ARM_NEXT], as it is: not masked). The ARM7 context is
// complete and nothing of the instruction has executed. Never blocks.
// Returns 0 to continue: nothing of the guest was changed.
// Returns non-zero to suspend the burst in front of the instruction. It has recorded the stop
// (dbg::emu::OnSoundStop). The caller keeps where the burst is (sample, ticks used) and returns to whoever
// runs the burst without executing anything more of the ARM7 or the AICA: the scheduler callback returns its
// period as usual, VirtualDreamcast::RunSoundBurst returns false.
// What stops: an active listed breakpoint, or the temporary breakpoint, on pc under the address rule
// (dbg::arm7::MaskAddr), unless the resume exemption covers this check; and, while a step is armed, every
// check but the one in front of its instruction (dbg::arm7::PrepareStep).
u32 dbg_arm7_Check(u32 pc);

// From the AICA scheduler callback only, at the start of a burst whose ARM7 is held in reset, while
// dbg_arm7_armed is not 0: no instruction executes in this burst and no check is made. A step that is armed
// is complete here with nothing executed: the stop is recorded (dbg::emu::OnSoundStop with StopCause::Step)
// and the slice hook latches it behind this scheduler tick. The burst is not suspended: the callback goes on
// with its rest. Breakpoints are left alone. A resume exemption that is armed ends here: a held ARM7 takes its
// reset exception when it is released, and its first instruction then is not the one the exemption was for.
// Never blocks.
void dbg_arm7_OnBurstUnclocked();
```

In the `#else` half, beside the stand-ins of the SH4:

```cpp
constexpr dbg::NoCount dbg_arm7_armed{};

static inline u32 dbg_arm7_Check(u32) { return 0; }
static inline void dbg_arm7_OnBurstUnclocked() { }
```

#### H6-2 `libswirl/debugger/dbg_emu.h` — `SetArm7Breakpoints`, behind `SetSh4Breakpoints`

```cpp
// Inside a CC_TABLE call. The enabled ARM7 code breakpoints, all of them: ids[i] stops at addrs[i] (addresses
// as the client gave them, multiples of 4 up to 0x00FFFFFF; the address rule is applied by dbg::arm7 when a
// check is made). n <= kMaxBreakpoints; the arrays are read during the call only. Forwarded to
// dbg::arm7::SetBreakpoints. It never causes code-cache work and never changes the loop of the SH4. It can
// change dbg_arm7_armed (debugger.h), and with it NOTICE_ARM7_FALLBACK, which GetRunState() then has at once.
// Like the other setters it also runs under L while no emulator thread exists, with or without a machine.
void SetArm7Breakpoints(const u32* ids, const u32* addrs, u32 n);
```

#### H6-3 `libswirl/debugger/dbg_emu.h` — `OnSoundStop`, behind `LatchStop`

```cpp
// (E) only, from the ARM7 check (dbg_arm7_Check, dbg_arm7_OnBurstUnclocked: debugger.h) and from
// dbg::arm7::CompleteStepAtBurstEnd, when a breakpoint of the ARM7 stops or a step of the ARM7 is complete.
// Never blocks. It records the stop; it latches nothing and leaves nothing. It does not look at the run flag:
// a check that hits has suspended its burst whatever the machine is about to do, and dc_run deals with a
// burst it finds suspended.
// While the guest executes (the burst runs in the AICA scheduler callback, in a run or during a step):
// records the cause as a freezing cause and sets ATTN_PAUSE; the slice hook latches it behind the scheduler
// tick of the UpdateSystem() call that is running, also inside a SLEEP wait and during a step. The SH4
// executes nothing behind it. When the machine leaves Run() in the same slice for a reason of its own, dc_run
// acts on that first, with no Park() in between. A host stop or a guest exit: the burst runs to its end
// without checks in front of the stop callback, and this stop is not published. A guest reset: the burst is
// discarded, and the stop that the next Park() publishes is the reset's (StopCause::Reset), as for every
// stop that was wanted when a reset came.
// In the park context (the park loop runs a suspended burst on): records the cause as the stop that the park
// loop publishes next. The thread stays parked.
//   Breakpoint, id   an active listed breakpoint of the ARM7 stopped; the lowest id when several are on the
//                    address
//   Step, 0          the temporary breakpoint of the ARM7 stopped, or a step of the ARM7 is complete
// When other causes meet it in the same stop the published cause follows the priority of StopCause, and among
// breakpoints of the winning cause the lowest id, as for LatchStop.
void OnSoundStop(StopCause cause, u32 breakpointId);
```

#### H6-4 `libswirl/debugger/dbg_emu.h` — `TargetSteps`, behind `Step`

```cpp
// Any thread, any lifetime state, never blocks. Whether run control and code breakpoints of the target exist
// in this build: what debugger.describe gives as targets[].stepping, and what the core asks before it accepts
// a code breakpoint or forwards a step. Constant for the process. The SH4: true. The ARM7: true once its run
// control exists (package E6b of the M2 round; false before it). The DSP: false until M3.
bool TargetSteps(Target t);
```

#### H6-5 `libswirl/debugger/dbg_arm7.h` — new, frozen from this revision

```cpp
#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::arm7: the state behind ARM7 code breakpoints and steps (dbg_arm7.cpp): the listed set, the active
	mask, the temporary breakpoint, the resume exemption, the step. The sound burst reaches it through
	dbg_arm7_armed, dbg_arm7_Check and dbg_arm7_OnBurstUnclocked of debugger.h; this header is what run control
	(dbg::emu) and the register writer (dbg::regs) call.

	Terms
	  listed breakpoint     an enabled code breakpoint of the client's table. It stops only while the active
	                        mask has CategoryBit(Category::Arm7), and only then does it arm the debug loop
	  temporary breakpoint  made by a step-over or a step-out; never listed, always active; at most one exists
	  resume exemption      armed for one PC (PrepareResume, PrepareStepOver, PrepareStepOut). The first check
	                        behind it answers "continue" if its pc has the key of that PC: the instruction the
	                        ARM7 stands on executes, also with a breakpoint on it. It ends at the first check
	                        that is made, used or not (a FIQ entered in front of the instruction ends it unused:
	                        a breakpoint on the vector stops); whenever dbg_arm7_armed changes to or from 0;
	                        at a burst that finds the ARM7 held in reset (dbg_arm7_OnBurstUnclocked); at
	                        OnStop() and at OnPcWritten(). So it never waits for a breakpoint that is added
	                        later, or for an ARM7 that is released later
	  step                  armed by PrepareStep(): the instruction at the PC executes, also with a listed
	                        breakpoint on it, and the next check stops

	Unless a function says otherwise it is called on (E) in the park context only, never blocks, and never
	while a burst executes. Nothing here touches a code cache: no ARM7 backend compiles a check. Nothing here
	keeps a pointer into the machine: the mask of the wave memory is the machine's (sh4_cpu->aica_ram.size - 1,
	as dbg::mem reads it) and the registers come through SoundCPU::Debug* (hw/arm7/SoundCPU.h), both asked for
	at each use.
*/

namespace dbg { namespace arm7 {

// The address rule. Two ARM7 code addresses are the same instruction when their keys are equal: the address
// under the mask of the wave memory (its size - 1) with bits 0 and 1 clear. The ARM7 fetches every
// instruction from wave memory under that mask. The key is for comparing, not an address to read from.
// Needs a machine for the mask: (E), or under L while no emulator thread exists; with no machine it returns
// addr with bits 0 and 1 clear.
u32 MaskAddr(u32 addr);

// The enabled listed breakpoints, all of them (emu::SetArm7Breakpoints): ids[i] is at addrs[i], any order,
// addresses as the client gave them. n <= kMaxBreakpoints; the arrays are read during the call only.
// Replaces the listed set and updates dbg_arm7_armed. The temporary breakpoint stays. The addresses are kept
// as given and compared under the address rule at each check: the call does not touch the machine. Also
// callable under L while no emulator thread exists, with or without a machine.
void SetBreakpoints(const u32* ids, const u32* addrs, u32 n);

// The listed set as SetBreakpoints() got it last: copies the first max pairs, in the order they were given,
// and returns how many the set has. Changes nothing. Same contexts as SetBreakpoints(). For the selftests: it
// is how the hand-over of the core (PushBreakpoints) is checked without a stop.
u32 GetBreakpoints(u32* ids, u32* addrs, u32 max);

// The active mask (emu::SetActiveMask); only CategoryBit(Category::Arm7) matters here. Updates
// dbg_arm7_armed: a listed set whose category is not active arms nothing. Also callable under L while no
// emulator thread exists. Before the first call the mask is 0: no listed breakpoint stops.
void SetActiveMask(u32 categories);

// Inside a CC_EXEC call (emu::Step calls it for every kind of ARM7 step). Changes nothing.
// Returns 0, or kErrNotClocked while the ARM7 is held in reset: SoundCPU::DebugEnabled() is false, the
// source that the register ENABLED of dc.aica.arm7 and the unwinder read. There is no kErrCannotStep for the
// ARM7: a word that is no instruction takes the undefined-instruction exception, as in a run.
int StepCheck();

// Park loop, for every leave of the park context that can let the ARM7 execute and is not a step of the
// ARM7: a resume; a step of the SH4, with a sound burst suspended or not (its slice end can start one); the
// first Park() of a thread that resumes the stop of the thread before it. With a burst suspended it is called
// in front of running the burst on. Not called for a step-over on a BL or a step-out of the ARM7:
// PrepareStepOver() and PrepareStepOut() arm the exemption themselves.
// Arms the exemption for the PC of the ARM7 iff all of this holds, and arms nothing otherwise: the last
// published stop was decided by the ARM7 check (OnStop() recorded that, with the PC); the PC has the key of
// that PC now (a PC the debugger wrote away and back counts as that PC: the rule of dbg::sh4); the ARM7 is
// clocked (SoundCPU::DebugEnabled()); dbg_arm7_armed is not 0, so that a check will be made.
void PrepareResume();

// Park loop, at the same point, for a step of one instruction. Arms the step for the PC of the ARM7 as it is
// now and makes dbg_arm7_armed not 0. From then on the first check, if its pc has the key of that PC, answers
// "continue": the instruction executes, also with a listed breakpoint on it. Every other check stops with
// StopCause::Step: the check behind the instruction, wherever it is (the next instruction, a branch target,
// the vector of an exception the instruction took or of a FIQ entered behind it), and the first check itself
// when its pc is not that PC (a FIQ was entered in front of the instruction: the step is complete at the
// vector with nothing executed). A burst that starts with the ARM7 held in reset completes the step
// (dbg_arm7_OnBurstUnclocked). A burst that ends behind the instruction without another check leaves the step
// to CompleteStepAtBurstEnd().
void PrepareStep();

// Park loop, behind a burst that ran to its end (VirtualDreamcast::RunSoundBurst returned true) while a step
// was armed. If the instruction of the step has executed, the step is complete at the end of the burst, with
// none suspended: it records the stop as the check does (emu::OnSoundStop with StopCause::Step, which in the
// park context is the stop the park loop publishes next) and returns true. Otherwise it changes nothing and
// returns false.
bool CompleteStepAtBurstEnd();

// Park loop, for a step-over. If the instruction at the PC is BL (read through dbg::mem, decoded by
// DisasmArm7: ArmInsnInfo::is_call) it sets the temporary breakpoint at PC + 4, then arms the exemption for
// the PC as it is, whatever decided the last stop (the instruction the request stands on executes, also with
// a listed breakpoint on it, as for a step of one instruction), and returns true: the caller goes on as for
// a resume, without PrepareResume(). Otherwise it changes nothing and returns false: the caller makes a step
// (PrepareStep()).
bool PrepareStepOver();

// Park loop, for a step-out. Sets the temporary breakpoint at R14 of the active mode, then arms the exemption
// for the PC as it is, whatever decided the last stop; the caller goes on as for a resume, without
// PrepareResume(). A heuristic: right for leaf code and before R14 is saved or overwritten.
void PrepareStepOut();

// Entering the park context for a stop, once per published stop and before the listener, beside
// dbg::sh4::OnStop(); also when temporary state must end without a stop: for a guest reset and when the
// emulator thread exits. Never for a service excursion, which publishes nothing. Never called without an
// emulator thread. Removes the temporary breakpoint and a step that is armed, clears the exemption, updates
// dbg_arm7_armed, and records the PC of the ARM7 together with whether the ARM7 check decided this stop (it
// called emu::OnSoundStop since the OnStop() before). That is so also when a cause of the SH4 met it in the
// same stop and won the publication: the hit of the ARM7 counts as taken, and a resume runs through it.
void OnStop();

// The debugger wrote the PC of the ARM7 (dbg::regs::Write, also in a service excursion of a running machine),
// or the machine state was replaced (a reset, a loaded state): ends the exemption. What OnStop() recorded
// stays: PrepareResume() decides from the PC as it is at the leave, which is the rule of
// dbg::sh4::OnPcWritten. A PC written onto another breakpoint stops there with nothing executed; a PC written
// back onto the address of the stop resumes through a breakpoint there. A replaced state needs no more: on (E)
// OnStop() follows and records anew, and while Stopped the owed resume goes with the state, so the next
// PrepareResume() comes behind a new OnStop(). Also callable under L while no emulator thread exists.
void OnPcWritten();

} }
```

#### H6-6 `libswirl/hw/arm7/SoundCPU.h` — `DebugIsRecompiler`, last in the debug block

```cpp
	// Is the backend that executes the ARM7 a recompiler. The debugger's breakpoints and steps of the ARM7 then
	// run the interpreter on the same context, with other timing (NOTICE_ARM7_FALLBACK). The answer changes
	// when setBackend() does, which is at the start of an emulator thread. No side effects. Same threads as
	// DebugEnabled().
	virtual bool DebugIsRecompiler() { return false; }
```

### 2.2 Comment-only changes in frozen headers

Each item: where, and the text as it stands afterwards. The C numbers are those of `headers-m2.md` section 3.

#### `libswirl/debugger/debugger.h`

**H6-10, "Contexts on (E)", the park context (C2).**

```
	  park context  in dc_run after SuperH4::Run() returned and before it is called again: no code of the SH4
	                is on the host stack, the SH4 context in Sh4cntx is complete. The park loop is the part of it
	                that waits while a debugger stop is in force; a service excursion is a visit that only runs
	                queued calls and goes back into Run(). One thing of the guest does execute in the park
	                context: the rest of a sound burst that the debugger suspended (instructions of the ARM7 and
	                the time steps of the AICA between them), when the park loop or dc_run runs it on. The SH4
	                executes nothing there and no emulated time passes
```

**H6-11, `g_stop_latched` (C1).** "three rules" becomes four; the fourth line is new.

```cpp
// The stop latch: a debugger stop has been decided and (E) is on its way into the park context, or in it.
// (E) only. Set by emu::LatchStop() and at the end of a step, cleared when the park context is left.
// While it is set, emulator code obeys four rules:
//   UpdateSystem()  returns 1 behind its test of g_attn (ATTN_LATCHED): no emulated time passes, no scheduler
//                   callback runs, the slice hook is not called
//   UpdateINTC()    accepts nothing: sets g_intc_deferred and returns 0
//   SLEEP handler   tests it in a slice of its wait that reported something and accepted nothing; when set
//                   it sets g_sleep_cut, leaves the wait and re-executes SLEEP (next_pc -= 2)
//   AICA callback   starts with a verify that no sound burst is suspended (Dreamcast_impl::AicaUpdate). A
//                   burst the debugger suspended is run on under the latch: by the park loop before the SH4
//                   executes anything, and by dc_run in front of the stop callback. Never by the scheduler
extern bool g_stop_latched;
```

**H6-12, `emu::SliceHook()` (C3).** Item 2 of "Lifetime Running" and the "Lifetime Parked" paragraph; the rest
stays.

```cpp
//   2. latches the stop (emu::LatchStop) if a freezing cause is recorded (an event breakpoint; a breakpoint
//      or a step of the ARM7, which the sound burst of this scheduler tick recorded: emu::OnSoundStop), or
//      if g_in_sleep is clear, CpuRunning is set and a pause was requested or a call of another class is
//      queued. (With CpuRunning clear the machine itself is about to leave Run(), and the park context that
//      follows takes them.)
//      ...
// Lifetime Parked (a step is executing: a step of the SH4, or the run of the SH4 towards the sound burst that
// a step of the ARM7 waits for): runs no call; latches the stop if a freezing cause is recorded, which ends
// the step at this slice boundary.
//
// It never changes the armed breakpoint set and never touches the code cache: a block may be executing.
// Guest state is only read, and Sh4cntx is complete here only on a backend with SH4DBG_CTX_AT_SLICE. A sound
// burst can be suspended when the hook runs its calls: the ARM7 then stands between two instructions.
```

**H6-13, the table of the lifetime hooks (C4).** Two rows.

```
	  Reset       LifetimeLock; discard a suspended sound burst; reset the machine; OnGuestStateReplaced()
	  ...
	  dc_run      OnThreadEntered(); select the backends; start the cpu; set emu_started;
	              the loop of Park() below; run a suspended sound burst to its end, without checks;
	              stop callback; OnThreadExiting()                                    Running -> Stopped
```

**H6-14, `OnThreadExiting()` (D19 of `headers-m2.md`).**

```cpp
// (E), last action of dc_run, after the stop callback ran; no lock held by the caller; lifetime Running
// or Parked. guestExit: the thread ends because the guest asked for it (reios_exit), not because the host
// stopped it. In front of the stop callback dc_run has run a sound burst that the debugger had suspended to
// its end, without checks (VirtualDreamcast::RunSoundBurst): what the callback sees, and what the exit line
// of an automated run prints, is the state behind a completed burst.
// Takes L (blocks while (U) holds it, which (U) never does while it waits for this thread); makes the
// interrupt acceptance that the latch deferred; ends what belonged to a stop (the temporary breakpoints of
// both processors, a pending step-out, a step of the ARM7 that is armed); drains the call queue under L:
// CC_READ and CC_TABLE calls run, CC_WRITE and CC_EXEC calls complete with -32001; drops a pause request that
// was not honoured; publishes Stopped (reason host-stopped, or guest-exit). After it returns the thread
// touches no guest state.
void OnThreadExiting(bool guestExit);
```

**H6-15, `OnGuestStateReplaced()` (C5).** The middle of the comment.

```cpp
// The debugger drops what belonged to the old state: the owed remainder and resume exemption of the SH4
// breakpoint mechanism, the resume exemption of the ARM7, g_sleep_cut, a deferred interrupt acceptance
// (g_intc_deferred), the temporary breakpoints of both processors, a pending step-out, a step of the ARM7
// that is armed, and the resume that a thread exit taken in a debugger stop left owed. A suspended sound
// burst is the machine's: Dreamcast_impl::Reset has discarded it before this call.
// On (E) it calls dbg::sh4::OnPcWritten() and OnStop(nullptr), and dbg::arm7::OnPcWritten() and OnStop().
// While Stopped it calls the two OnPcWritten() only: no temporary breakpoint, no step-out and no armed step
// exist then, because OnThreadExiting() ended them.
```

**H6-16, the pseudo-code in front of `PARK_RUN`, last line.**

```
	    ...; run a suspended sound burst to its end (RunSoundBurst(false)); stop callback;
	    OnThreadExiting(guestExit);
```

**H6-17, `Park()` (C6, C7).** The second and third bullet are rewritten and a fourth is new; the first
paragraph and the first bullet stay. The last bullet and the "Returns" paragraph are H6-19.

```cpp
// - Parks while a debugger stop is in force: ... (unchanged up to "the threads that the selftest of dbg::emu
//   starts itself.")
//   Entering a stop: g_parked is set, CpuRunning cleared, the breakpoint state of the SH4 and of the ARM7
//   drop their temporary breakpoints, a pending step-out and a step that is armed, the listener is invoked,
//   the window title becomes "nullDC 3.0.0 [paused by debugger]" (...unchanged...). The park loop then
//   waits, without limit, for a call, for a run-control action that a call armed, or for Kick(). Armed steps
//   are executed by the loop itself, with one exception: a step of the ARM7 asked for while no sound burst
//   is suspended (below).
// - Leaves the park context for a resume and at the end of a service excursion, in this order: for a
//   resume decides the resume exemption of the ARM7 (dbg::arm7::PrepareResume); runs a suspended sound
//   burst on, with the checks of the ARM7 (VirtualDreamcast::RunSoundBurst(true)): if a
//   breakpoint of the ARM7 suspends it again nothing is left, the thread stays parked, that stop is published
//   (the stop counter advances) and the resume, or the step of the SH4 the burst was run on for, is dropped;
//   otherwise clears g_stop_latched; makes the interrupt acceptance that the latch deferred; calls
//   sh4_host_fp_resync(); for a resume arms the resume exemption of the SH4; sets CpuRunning and clears
//   g_parked, unless a host stop or a guest exit is pending; for a resume invokes the listener with Running.
//   No burst is ever suspended at the end of a service excursion: a suspension is followed by a stop.
// - A step of the ARM7 (emu::Step). With a burst suspended the loop runs the burst on, in the park context,
//   until the check behind the instruction stops it or the burst ends: no emulated time passes, the SH4
//   executes nothing, the latch stays set, nothing is published in between. With no burst suspended the
//   instruction is in the next burst, which only the scheduler starts: the loop leaves as for a resume
//   (latch, deferred acceptance, FP state, the resume exemption of the SH4, the loop that Run() enters) but
//   the stop stays in force: g_parked stays set, nothing is published, the listener is not invoked, and the
//   slice hook treats the run as a step that is executing. The SH4 runs for one period of the AICA callback
//   at most (145124 cycles and the rest of a slice); the stop of the step is recorded in that callback,
//   latched in its UpdateSystem() and published by the next Park(). A stop of the SH4 that comes first (a
//   code breakpoint, an event breakpoint) ends the run with its own cause. A host stop, a guest exit or a
//   guest reset that comes first is found by dc_run, as behind a step of the SH4: no stop of the step is
//   published (a reset publishes its own, StopCause::Reset), the armed step ends with it (OnThreadExiting(),
//   OnGuestStateReplaced()), and the wait counts as a step that has executed, so the thread leaves no resume
//   owed. The state "a step is executing" ends at the Park() that follows the wait, whatever that Park()
//   finds. Calls that were queued during the wait (the two of the grace expiry among them) are served behind
//   the stop of the step, in their order.
```

**H6-18, `EventSlow()` (C8).** One sentence replaced.

```cpp
// On (E) while the guest executes (Running, or a step) it also tests the event breakpoints (emu::OnEvent):
// a hit asks for a pause, which the slice hook latches in the UpdateSystem() call that is running or the
// next one. Events raised on other threads, or on (E) in the park context, never stop: among them the events
// of the AICA and of the ARM7 that a suspended sound burst raises while the park loop runs it on. They are
// logged.
```

**H6-19, `Park()`, its last bullet and the "Returns" paragraph** (the comment of H6-17; both become wrong with
M2: the owed resume is also the ARM7's, and a step of the ARM7 that waits returns `PARK_RUN` with the stop in
force). The bullet's last two sentences ("This holds when ... stops.") stay.

```cpp
// - The first Park() of a thread that follows a thread exit taken in a debugger stop (the host stopped the
//   machine while the thread before it was parked) is a resume of that stop: the debugger pause that was in
//   force before the host stop is not applied again, and dbg::emu calls dbg::sh4::PrepareResume() there, so
//   that the instruction of the stop executes and its breakpoint does not stop again at once. The ARM7 has no
//   part in it: no sound burst is suspended then, because the thread before ran it to its end in front of its
//   stop callback, and so the instruction of a stop of the ARM7 has executed. The dbg::arm7::PrepareResume()
//   that dbg::emu calls there as well arms nothing: OnThreadExiting() has ended the stop for the breakpoint
//   state of the ARM7 (dbg::arm7::OnStop()).
//   ... (unchanged)
//
// Returns PARK_RUN after leaving, with CpuRunning set and g_stop_latched clear; g_parked is clear too, but
// for a step of the ARM7 that waits for its burst.
// Returns PARK_EVALUATE, with CpuRunning clear, whenever a stop reason of the machine may be pending: after
// Kick(), after a call that asked for a reset, after a step in which the SH4 executed, and in place of
// PARK_RUN when a host stop or a guest exit is pending. A debugger stop that was in force stays in force
// across the evaluation: dc_run does not save ROM files and does not run the stop callback for it.
```

#### `libswirl/debugger/dbg_emu.h`

**H6-20, `RunState::stopId` (C9).**

```cpp
	u32 stopId;				// counts stops: advances on every transition into a state reported as paused (a park
							// for any cause, Stopped, NoMachine), on every completed step, on a stop that the ARM7
							// check makes while the park loop runs a suspended sound burst on (no park is entered
							// for it), on a reset made while paused and on a reset or state load while Stopped. A
							// service excursion leaves it alone
```

**H6-21, `NOTICE_ARM7_FALLBACK` (C10).**

```cpp
	NOTICE_ARM7_FALLBACK       = 4,		// the ARM7 runs on the interpreter fallback: timing differs. Set while the
										// backend that executes the ARM7 is a recompiler
										// (SoundCPU::DebugIsRecompiler) and dbg_arm7_armed is not 0: the debug loop
										// then replaces the recompiler. Also set in the stop that ends a step, a
										// resume or a run in which the debug loop executed in place of the
										// recompiler, whatever is armed at that stop (a step and a temporary
										// breakpoint end before the stop is published). Never set where that
										// backend is the interpreter, which is every x86-64 build
```

**H6-22, `Call`, the last bullet (C11).** "CC_TABLE: also SetSh4Breakpoints, SetArm7Breakpoints,
SetEventBreakpoints, SetActiveMask."

**H6-23, `Resume()` (C12).**

```cpp
// Inside a CC_EXEC call. Arms a resume: after the call returns, the park loop leaves the park context and
// publishes Running. With a sound burst suspended it runs the burst on first (debugger.h, Park()): if a
// breakpoint of the ARM7 suspends it again, the thread stays parked, that stop is published and Running is
// not. Returns 0; kErrNotPaused, with nothing armed, if the thread is not parked or an action is already
// armed.
int Resume();
```

**H6-24, `Step()` (C13, C14).**

```cpp
// Inside a CC_EXEC call. Validates a step and arms it; the park loop acts on it after the call returns, and
// a new stop is published when it is complete (StopCause::Step unless a breakpoint or an event ended it).
//
// Target::Sh4. The park loop executes the step. With a sound burst suspended it runs the burst on first: a
// breakpoint of the ARM7 that suspends it again is published as the stop, and the step is dropped. A step
// over or out that turns into a resume publishes Running first. A step of any kind is complete, without
// executing anything, when the interrupt acceptance that the latch deferred accepts an interrupt on the way
// out of the park context: the stop is at the vector (dbg::sh4::PrepareStep).
//
// Target::Arm7. Into: one instruction of the ARM7 (dbg::arm7::PrepareStep). With a burst suspended the park
// loop runs it on in the park context until the check behind the instruction stops it: nothing is published
// in between, no emulated time passes, the SH4 executes nothing; when the instruction was the last of its
// burst, the step is complete at the end of the burst, with none suspended. With no burst suspended the park
// loop leaves without publishing (debugger.h, Park()): the SH4 runs to the next burst, one period of the
// AICA callback at most, and the step is the first instruction of that burst. The step is complete without
// executing anything when a FIQ is entered in front of its instruction (the stop is at the vector) and when
// that burst finds the ARM7 held in reset. A stop of the SH4 that comes first is published with its own
// cause. Over: on a BL the temporary breakpoint behind it and a resume, else as Into. Out: the temporary
// breakpoint at R14 and a resume (dbg::arm7::PrepareStepOver, PrepareStepOut). That resume is Resume()'s: it
// publishes Running when it leaves. Like Into, both execute the instruction they stand on, also with a
// listed breakpoint on it and whatever the stop before them was.
//
// Returns 0 when armed. Otherwise nothing is armed and it returns
//   kErrNotPaused     the thread is not parked, or an action is already armed
//   kErrNotSupported  the target cannot be stepped: TargetSteps(t) is false; Over and Out on Dsp always
//   kErrCannotStep    a precondition of an SH4 step failed (dbg::sh4::StepCheck); *why, if why is not null,
//                     receives the reason as a constant string
//   kErrNotClocked    Arm7 while the ARM7 is held in reset (dbg::arm7::StepCheck); Dsp likewise, from M3
int Step(Target t, StepKind k, const char** why = nullptr);
```

**H6-25, `Reset()` (C15).**

```cpp
// Inside a CC_WRITE call. Parked: resets the machine through Dreamcast_impl::Reset at once, which discards a
// suspended sound burst; the thread stays parked and publishes a new stop with StopCause::Reset. Running (the
// call runs in a service excursion): asks the machine for a guest reset; it resets and keeps running, nothing
// is published.
// Returns 0. Called anywhere but on (E) in the park context it changes nothing and returns kErrHostStopped;
// a CC_WRITE call is never made there.
int Reset();
```

**H6-26, `SetActiveMask()` (C16).** The third sentence: "Forwarded to dbg::sh4::SetActiveMask and
dbg::arm7::SetActiveMask; it can make UseFallbackLoop() change as SetSh4Breakpoints can, and dbg_arm7_armed as
SetArm7Breakpoints can."

**H6-27, `GetRunState()` (C17).** "notice is current even when nothing was published since: dbg::emu updates it
when a service excursion switches loops and when a table call changes whether the ARM7 runs on its fallback."

**H6-28, `LatchStop()` (C18).** One sentence added behind the first paragraph: "The sound side does not call it:
a breakpoint or a step of the ARM7 is recorded through OnSoundStop, below, and latched by the slice hook."

**H6-29, `OnEvent()`.** The last sentences: "In the park context it does nothing, also while the park loop runs
a suspended sound burst on: the events of that burst are logged and do not stop. Never blocks."

**H6-40, the file comment, second paragraph.** `dbg_arm7.h` is one of the headers that include this one.

```cpp
	This header and those that include it (dbg_sh4.h, dbg_arm7.h, dbg_core.h, dbg_mem.h, dbg_callstack.h,
	dbg_selftest.h) exist only in builds with NULLDC_DEBUGGER: debugger.h has stand-ins for a build without it,
	these have none.
```

**H6-41, `SetListener()`.** The line on (E); H6-20 gives `stopId` the same fourth kind of stop.

```cpp
//   on (E)  for every stop (park for any cause, completed step, reset while paused, a stop that the ARM7
//           check makes while the park loop runs a suspended sound burst on) and every resume;
```

#### `libswirl/debugger/dbg_events.h`

**H6-42, the row `EV_DEBUGGER_ENGINE`, its where-raised comment.** No enumerator is added: the change of the
ARM7's fallback is logged under the event that exists.

```cpp
/* debugger/dbg_emu.cpp: switch to or from the fallback loop of the SH4 (E4a); the ARM7 starts or stops running on its interpreter fallback (M2) */
```

#### `libswirl/debugger/dbg_sh4.h`

**H6-30, `PrepareResume()` and `OnStop()` (C19).** First sentence of each.

```cpp
// Park loop, when the park context is left for a resume, for a step of the ARM7 that waits for its burst
// (for the SH4 that is a resume), or at the first Park() of a thread that follows a thread exit taken in a
// debugger stop, after the deferred interrupt acceptance and sh4_host_fp_resync().
// ... (unchanged)
void PrepareResume();

// Entering the park context for a stop, once per published stop and before the listener: after
// SuperH4::Run() returned with a debugger stop decided, after every step (executed, or complete at a vector),
// for the start park, for a reset made while paused, and for a stop that the ARM7 check makes while the park
// loop runs a suspended sound burst on (the SH4 has not executed since the stop before it: its pc is the
// same, and the owed remainder stays, which is what keeps such a stop invisible to the SH4). Never for a
// service excursion, ... (unchanged)
void OnStop(StopInfo* out);
```

#### `libswirl/debugger/dbg_regs.h`

**H6-31, `kRegWritable`.** `kRegWritable = 1,		// Write() can set it; debugger.describe says so (flags.writable)`

**H6-32, the "Values" paragraph (C20).**

```cpp
/*
	Values. On (E), or on a thread that holds L while no emulator thread exists; Write() on (E) only. Read()
	never calls a guest register handler: a register whose read has side effects is read from its array or by
	a dedicated reader, and has kRegNoReader when neither exists. Write() applies the effects that belong to
	the register it writes (below) and nothing else of the guest.
*/
```

**H6-33, `Write()` (C21).** The text below is final; until package E7a has given the body, the return sentence
also ends in the old clause "or kErrNotSupported (-32004) while no writer exists at all" (2.5). Its two
sentences on the code cache are M2-D41 (FPSCR) and M2-D42 (the PC), and M2-Q4 asks the owner about both.
**R6 does not wait for the answer**: it writes the text as printed (6.2: work proceeds on the recommendation).
If the answer is "no", the two sentences from "When PR, SZ or the rounding mode changed" become: "Compiled
blocks keep the FP mode they were compiled in (they are found by address alone): on a recompiler a resume
behind a write that changes PR, SZ or the rounding mode runs them in the old mode, while a step runs in the
new one."; and the clause of the PC from "and, when it differs from the PC it replaces" to the end of its
sentence becomes: ". The code cache stays: on a recompiler a resume from a PC written onto an address that
shares its slot of the block cache with a compiled block (addresses a multiple of 32 MiB apart do: main memory
at 0x0C000000, 0x8C000000 and 0xAC000000, and the boot ROM and main memory) runs the block of the other
address, while a step decodes what is at the PC." A "no"
that arrives before E7a starts is applied by the integrator as a correction of revision 6, comment only
(recorded as 6a in the table of the design's Appendix B), and E7a's brief loses the two `ResetCache()` calls,
the cache half of `writes.reg.fpscr` and the mirror case of `writes.reg.pc`; a "no" behind E7a's merge is
header revision 7 with a fix round of E7a.

What a register reads back is what was **stored**, and three rows store less than is written; the text says
so, because "bits of value above its width are ignored" alone would promise that a written value reads back.

```cpp
// Writes register `index` of the node; bits of value above its width are ignored. (E) in the park context
// (inside a CC_WRITE call: the park loop, or a service excursion of a running machine), or a thread that holds
// L while no emulator thread exists, as for Read(). Never blocks.
// Writable are the rows with kRegWritable: in M2 the registers of dc.sh4.cpu and those of dc.aica.arm7 but
// ENABLED. No other row has a writer.
//   dc.sh4.cpu     a store into the SH4 context. PC: a value with bit 0 set is refused (nothing executes at an
//                  odd address, and the block cache is indexed by pc >> 1); an accepted one also tells the
//                  breakpoint state (dbg::sh4::OnPcWritten: the owed remainder goes; the resume exemption is
//                  decided at the leave from the PC as it then is) and, when it differs from the PC it
//                  replaces, empties the code cache of the SH4 (SuperH4::ResetCache()): a block is found by
//                  (pc >> 1) under FPCB_MASK with no address in its key, so the slot of the written PC, or
//                  of an address the code there goes on to, can hold the block of another address
//                  (addresses a multiple of 32 MiB apart share a slot: main memory at 0x0C000000,
//                  0x8C000000 and 0xAC000000 does, and so do the boot ROM and main memory; the mirror
//                  16 MiB up is the same memory in other slots), and a
//                  resume would run that block where a step decodes what is at the PC. SR goes through
//                  sh4_sr_SetFull() and
//                  UpdateSR() (the register banks swap). SR is stored under 0x700083F3, and with MD clear
//                  RB is stored as 0: the register reads back what was stored, not what was written; so do
//                  FPSCR (FPSCR_MASK) and the CPSR of the ARM7 (its flags, I, F and the mode: 0xF00000DF).
//                  When UpdateSR() says an interrupt can be taken (one is pending and the SR as written
//                  lets it in: also when the write changed neither),
//                  UpdateINTC() is called, which under the latch only records the acceptance as owed: it is
//                  made when the park context is left. At a stop that was latched at a slice end the
//                  acceptance is owed already and nothing moves; at a code breakpoint of the SH4 it moves
//                  from the next slice end to the leave, as it does behind an LDC of the guest. FPSCR is
//                  masked with FPSCR_MASK and goes through
//                  UpdateFPSCR() (the FP banks swap, the host FP mode of the emulator thread follows). When
//                  PR, SZ or the rounding mode changed, the code cache of the SH4 is emptied
//                  (SuperH4::ResetCache()): a compiled block has the FP mode of the moment it was compiled
//                  and is found by its address alone. An owed remainder is data and stays
//   dc.aica.arm7   SoundCPU::DebugSetReg (hw/arm7/SoundCPU.h): CPSR through the mode switch (it reads back
//                  as the core composes it, above); PC as the
//                  address of the next instruction, stored with bits 0 and 1 clear, which also tells the
//                  breakpoint state (dbg::arm7::OnPcWritten). Also while the ARM7 is held in reset and while
//                  a sound burst is suspended (the ARM7 stands between two instructions then)
// Returns 0. Returns kErrNotWritable (-32013), having changed nothing, for a register without kRegWritable,
// for an index out of range, with no machine, and when the device refuses the value (an odd PC of the SH4;
// the CPSR of the ARM7 with mode bits that are no mode).
int Write(const Node& node, uint32_t index, uint64_t value);
```

#### `libswirl/debugger/dbg_mem.h`

**H6-34, the file comment, second paragraph, last sentence.** "Video memory and the PVR registers are also
written by the renderer's threads while a frame is queued: a read can see a frame in progress, and a write
lands under one as a write by the guest does."

**H6-35, `Write()` (C22).** Final text; until E7a the return sentence also ends in "or kErrNotSupported (-32004)
while the writer does not exist" (2.5).

```cpp
// Writes len bytes at addr. All or nothing: the whole range is checked before the first byte is stored.
// (E) in the park context (inside a CC_WRITE call: the park loop, or a service excursion), or a thread that
// holds L while no emulator thread exists, as for Read(). It waits for nothing but the lock of the texture
// cache, which a write to video memory takes as a faulting write by the guest does.
// Writable are main memory, video memory (both views) and wave memory (both targets), every mirror of them;
// a range may run from one mirror of a store into the next. No other region is writable, and neither is a
// range that would pass the end of the address space (addr + len > 2^32): a write does not wrap.
//   main memory    in a build with an SH4 recompiler the compiled blocks of every page the range touches are
//                  discarded first. Where the virtual memory layout is in use: bm_LockedWrite per page of
//                  REI_PAGE_SIZE, which also marks the page as data and removes its protection. Where it is
//                  not (virt_ram_base is null): SuperH4Backend::DebugDiscardPage of the active backend per
//                  4 KiB page, and the writer removes the protection of the pages itself
//                  (VLockedMemory::UnLockRegion). A build without a recompiler stores the bytes
//   video memory   VramLockedWrite first, for every page of REI_PAGE_SIZE of the store that the range
//                  touches (for the 32-bit view: the pages of pvr_map32 of each byte): textures locked on the
//                  page are invalidated and its protection goes
//   wave memory    the bytes are stored, then SoundCPU::InvalidateJitCache() once: the ARM7 recompiler has no
//                  detection of self-modifying code
// Breakpoints need nothing: no armed address is a patch in memory. An owed remainder of the SH4 is decoded
// from memory as it is when the park context is left.
// Returns 0. Returns kErrNotWritable (-32013), having stored nothing, when any byte of the range is not
// writable and when no machine exists.
int Write(Target t, u32 addr, u32 len, const u8* data);
```

#### `libswirl/debugger/dbg_selftest.h`

**H6-36, the "guest test" paragraph.** "... and uses dbg::emu (SetSh4Breakpoints, SetArm7Breakpoints,
SetEventBreakpoints, SetActiveMask, Resume, Step, Reset, ...) to arm what happens next."

#### `libswirl/hw/sh4/sh4_if.h`

**H6-37, `SuperH4Backend::DebugDiscardPage`, the last sentence.** "Called when the debugger arms or disarms the
address, and when the debugger writes main memory on a host without the virtual memory layout (the writer of
dbg_mem then removes the protection of the page itself)."

#### `libswirl/hw/arm7/SoundCPU.h`, the debug block

**H6-38, the block comment, `DebugEnabled()`, `DebugSetReg()` (C23).** `DebugGetReg` and `DebugGetInterrupt`
stay as they are. The sentences of the `RN_CPSR` row on the live SPSR are there because the row `SPSR` of
`dc.aica.arm7` reads `regs[RN_SPSR]` in every mode, and the core's switch writes that register in two places:
when the mode it leaves is user or system (`CPUSwitchMode`, the cases `0x10` / `0x1F` of its first half:
`reg[17].I = reg[16].I`), and when the mode it enters has an SPSR (the second half:
`reg[17].I = reg[SPSR_<mode>].I` with `saveState` false, which both `MSR CPSR` cases of the guest pass and
the writer passes as well; entering user or system mode leaves the register alone). The writer is the core's
switch, called as the guest's `MSR` calls it, and is not made to hide either (M2-D67). The four cases are in
the table behind the text.

```cpp
	/*
		Debugger access. Called on the emulator thread between two instructions of the ARM7 - outside a sound
		burst, or while the debugger has one suspended (in the slice hook and in the park context) -, or by a
		thread that holds the debugger's lifetime lock while no emulator thread exists; the setter on the
		emulator thread only. The defaults are those of a device that gives the debugger nothing.
	*/

	// False while the ARM7 is held in reset (ARMRST set): it is not clocked and a sound burst executes none
	// of its instructions.
	virtual bool DebugEnabled() { return false; }

	// Writes a register, same indices as DebugGetReg. Emulator thread only, in the debugger's park context;
	// also while the ARM7 is held in reset and while a sound burst is suspended.
	//   0..14, RN_SPSR, the banked registers   stored; for the bank of the active mode into the live register
	//   15                                     the address of the next instruction (R15_ARM_NEXT), stored with
	//                                          bits 0 and 1 cleared, as every write of R15 by the core is: the
	//                                          fetch reads four bytes at the address under the mask of the wave
	//                                          memory and does not align it
	//   RN_CPSR                                the mode bits have to be one of 0x10, 0x11, 0x12, 0x13, 0x17,
	//                                          0x1B, 0x1F. The write goes through the mode switch (the banks
	//                                          swap); the flags and the interrupt enables follow, and the
	//                                          pending FIQ is worked out again. The register reads back as
	//                                          the core composes it: N Z C V, I, F and the mode (the value
	//                                          under 0xF00000DF); no other bit is stored. The switch stores
	//                                          the live R13 and R14, and the live SPSR, into the slots of
	//                                          the mode it leaves, as the core's own switch does (an MSR of
	//                                          the guest makes it also when the mode stays). Leaving user or
	//                                          system mode, which have no SPSR, it copies the CPSR as it
	//                                          was before the write into the live SPSR instead, and a mode
	//                                          that has an SPSR loads its own over that when it is entered,
	//                                          as behind an MSR of the guest. So behind a write from one of
	//                                          those two modes RN_SPSR reads the CPSR as it was before the
	//                                          write when the mode written is one of the two as well (also
	//                                          behind a write of the value the CPSR has), and the SPSR of
	//                                          the mode written otherwise, which it goes on reading when
	//                                          the CPSR is written back. A CPSR written and written back
	//                                          leaves every register as it read, but RN_SPSR when the mode
	//                                          it is written from is one of those two, and can leave the
	//                                          save state different in those slots
	// No compiled code is dropped: the code cache of the ARM7 is found by address. Returns false, having
	// changed nothing, for any other index and for a CPSR with other mode bits.
	virtual bool DebugSetReg(u32 reg, u32 value) { return false; }
```

What that comment says of `RN_SPSR`, case by case. It is the reference for every test that compares rows
around a CPSR write (3.4 `arm7.setreg.*`, 3.5 `writes.reg.arm7`); it is not header text. "A mode with an SPSR"
is one of `0x11`, `0x12`, `0x13`, `0x17`, `0x1B`; `SPSR_<mode>` is that mode's row of `dc.aica.arm7`, which
reads the slot while another mode is active and the live register while its own is.

| The CPSR is written from | with the mode | The row SPSR then reads | And behind the write back |
| --- | --- | --- | --- |
| user or system | user or system (also: the value the CPSR has) | the CPSR as it read in front of the write | the CPSR as it read in between: the value written, as stored |
| user or system | a mode with an SPSR | what the row `SPSR_<mode>` of the mode written read in front of the write | that value still: entering user or system mode loads nothing |
| a mode with an SPSR | user or system | what it read in front of the write (the SPSR of the mode left) | what it read at the start |
| a mode with an SPSR | a mode with an SPSR | what the row `SPSR_<mode>` of the mode written read in front of the write | what it read at the start |

In all four, every row but SPSR reads behind the write back as in front of the write. A test asserts the row
SPSR **by these values, never as "changed"**: the value the row gets can be the one it had (a second write of
the same CPSR; an SPSR that held the value already). The sentence "RN_SPSR then reads that" which the fifth
cut had for every write from user or system mode holds in the first line only; a writer made to fit it in the
second would call the switch with `saveState` true, overwrite the SPSR of the mode it enters and store that
into the slot on the way back.

#### `libswirl/hw/aica/aica_mmio.h`, the debug block

**H6-39, the block comment and `DebugWriteReg()` (C24).**

```cpp
	/*
		Debugger access. Called on the emulator thread outside Update() and TimeStep() - between two sound
		bursts, or while the debugger has one suspended in front of an ARM7 instruction, where the sound
		generator stands between two samples -, or by a thread that holds the debugger's lifetime lock while
		no emulator thread exists; DebugWriteReg on the emulator thread only. Update() and TimeStep()
		themselves also run in the debugger's park context, when a suspended burst is run on. The defaults are
		those of a device that gives the debugger nothing.
	*/

	// Writes the 16-bit register at offset (even, below 0x8000) as a write by the SH4 does: the sound
	// generator, the timers, the interrupt lines and the ARM7 reset follow it. Returns false, having
	// changed nothing, when the register cannot be written. No device has a body for it and nothing calls
	// it: in M2 the debugger writes no register of the AICA, ARMRST among them (the debugger does not hold or
	// release the ARM7).
	virtual bool DebugWriteReg(u32 offset, u32 value) { return false; }
```

### 2.3 Declarations fixed here that are not in a frozen header

Both sides of each are E6b's, so no package boundary runs through them; they are fixed so that the brief, the
verifier and M3 have one text. E6b adds them; it changes them only with the integrator.

**`libswirl/libswirl.h`, `struct VirtualDreamcast`** (behind `StartCpu`):

```cpp
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
    // or a guest exit). Returns true when the burst ran to its end, the rest of the callback included (the
    // batch render, the tick of the sound regression harness: once per burst, behind the last sample), and at
    // once when none is suspended. Returns false when it is suspended again, which it never is with checks false.
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
```

with `enum : u32 { SOUND_BURST_NONE = 0, SOUND_BURST_ARM7 = 1 };` in front of the struct (M3 adds the phase of
the DSP as 2). The discard needs no declaration: it is a statement of `Dreamcast_impl::Reset()`.

`SoundBurstCounts` is there because the calls that `arm7.burst.plain` and the neutrality test `idlebp` count
are made in emulator code (the loop and the tail in `Dreamcast_impl`, the ARM phase in `SoundCPU_impl`), and
emulator code includes no debugger header but `debugger.h` (design 2, rules), which is frozen and has no
counter; the private header of 3.4 item 3 is the debugger's and cannot be reached from there (M2-D61).
`Dreamcast_impl` keeps the four numbers in the burst struct **in every build**: two increments per sample and
two per burst, which are part of the 48 instructions a sample that check (b) of 3.4 allows. `out[1]` has a
second use: it is how `arm7.step.fiqbehind` and the test `steps` know that a step stayed inside one ARM
phase (3.4; M2-D66) — the burst's `ticks_done` is not theirs to read.

**`libswirl/hw/arm7/SoundCPU.h`, outside the debug block** (behind `Update`):

```cpp
	// The ARM phase of one sample of a sound burst: the ARM7 executes until `ticks` are used up. Emulator
	// thread. *done is how many ticks were used when the call returned: the caller passes 0 for a new phase and
	// what it got back to go on with a suspended one.
	// A new phase with dbg_arm7_armed (debugger/debugger.h) 0, or with checks false: the backend's Run(ticks);
	// returns true.
	// A new phase with checks true and dbg_arm7_armed not 0: the debug loop. Returns false when the debugger
	// suspended it in front of an instruction (dbg_arm7_Check), with *done below ticks and nothing of the ARM7
	// changed since the instruction before; true when the ticks are used up.
	// A suspended phase with *done not 0 is always gone on with by the functions of the debug loop, whatever is
	// armed then: the backend has no entry for the rest of a phase. With checks false it makes no check. A phase
	// that was suspended in front of its first instruction (*done 0) is a new phase again: nothing of it has
	// used a tick, and a FIQ that was entered in front of that instruction stays entered.
	virtual bool RunSample(u32 ticks, u32* done, bool checks) = 0;
```

**`libswirl/hw/arm7/arm7.h`, `struct ARM7Backend`**: `static void DYNACALL StepIrq(Arm7Context* ctx);` (the
entry of a pending FIQ: statement (1) of today's `Step`) and `static u32 DYNACALL StepExec(Arm7Context* ctx);`
(the rest; returns its ticks). `Step` is the two in that order and keeps its callers **and its cost**:
`StepMany` runs it for every ARM7 instruction of every x86-64 run, so nothing is added per instruction on that
path. How — `Step` as one body behind its FIQ test with `StepIrq` / `StepExec` as the entries of the debug loop
only, or the two inlined into it — is E6b's choice, and the compiled code of its gate decides (3.4, "Host
cost" (a): `StepMany` and `Step` are the same instructions in base and tip). The first shape gives that by
construction, since it leaves the source of both functions alone; if the second does not, E6b takes the first.
The second virtual call per sample (`RunSample` in front of the backend's `Run`) is the cost the change is
allowed: 32 calls and 32 tests a burst, bounded by (b) of the same bullet.
**In the first shape nothing that exists runs the two halves**: every suite of `tests/arm7` steps through
`arm7m_step()`, which calls `ARM7Backend::Step`, and `StepExec` is a third inclusion of the interpreter's
body (`gpl/vba-arm/arm-new.h`) that every armed sample runs, and that replaces the recompiler on arm64. So
the halves have suites of their own in either shape: `arm7m_step()` runs `StepIrq(ctx); return StepExec(ctx);`
when the environment variable `ARM7M_STEP` is `halves`, and the ARM7 suites are registered a second time with
it (3.4, Delivers 2 and Gate; M2-D60).

### 2.4 What the revision is delivered with (package R6)

So that the tree builds and behaves as before, and so that no later package of the round has to open a file of
another.

1. **Stub bodies.**
   - New `libswirl/debugger/dbg_arm7.cpp`: `dbg_arm7_armed` is 0 and stays 0; `dbg_arm7_Check` returns 0;
     `dbg_arm7_OnBurstUnclocked` does nothing; `MaskAddr` returns `addr & ~3`; `SetBreakpoints` keeps the set
     and arms nothing, and `GetBreakpoints` gives it back; `StepCheck` returns `kErrNotSupported`;
     `CompleteStepAtBurstEnd` and `PrepareStepOver` return false; every other function does nothing. Entered
     in the source list of the debugger (`libswirl/debugger/cmake/` or wherever `dbg_sh4.cpp` is listed).
   - `libswirl/debugger/dbg_emu.cpp`: `SetArm7Breakpoints` forwards to `arm7::SetBreakpoints`; `SetActiveMask`
     also forwards to `arm7::SetActiveMask`; `OnSoundStop` does nothing; `TargetSteps(t)` is
     `t == Target::Sh4`. `emu::Step` keeps its `kErrNotSupported` for every target but the SH4.
2. **The wiring of the core, against the stubs** (`libswirl/debugger/dbg_core.cpp`; M2-D16):
   - the field `stepping` of `TargetInfo` goes; its three readers (`BuildDescribe`, `MethodBreakpointsAdd`,
     `Step`) ask `emu::TargetSteps(info.target)`;
   - `PushBreakpoints()` collects the enabled code breakpoints of `Target::Arm7` in a third pair of vectors and
     calls `emu::SetArm7Breakpoints` between the two calls it makes today;
   - `MethodBreakpointsAdd` refuses an address above the target's `addressMax` with `-32602` for every target
     (today for the DSP only; M2-D12).
   - `libswirl/debugger/dbg_selftest_core.cpp`: the expectation `targets[1]["stepping"] == false` of `core.tick`
     becomes "equals `emu::TargetSteps(Target::Arm7)`". In `core.tables` (the session has no test `core.methods`)
     the one `Check` "a code breakpoint of a target without stepping: -32004, before its address is looked at"
     is split. Its DSP half (`dc.aica.dsp.step` with address 3) stays `-32004`. Its ARM7 half follows
     `emu::TargetSteps(Target::Arm7)`: `-32004` while it is false; while it is true the test asks for
     `-32602` without an address, for `2` and for `0x01000000`, accepted and listed for a multiple of 4 up to
     `0x00FFFFFF`, and **the hand-over**: with three ARM7 breakpoints added, one of them toggled off,
     `dbg::arm7::GetBreakpoints` gives the two enabled ones with their ids at their addresses, and after their
     removal none.
   With the stubs nothing changes on the wire: describe says `stepping: false` for the ARM7, and the selftests
   and the M1 gate pass as before. The ARM7 half of `core.tables` runs for real from E6b on (its
   `TargetSteps` is true and `debugger.selftest.core` is in its gate); in R6 it is run by the verifier's
   forced flip (3.2).
3. **The panel ids.** `tests/debugger/regs/regs_test.cpp`: `sh4-registers` and `arm7-registers` join the list
   `panel_ids` (37 ids). The test asks for the UI's `PANEL_IDS` to have as many and for every action of a node
   to be one of them; no node uses the two until E7b. This needs the pinned UI checkout at F3a's merge or later, which
   is why F3a is merged first (section 4).
4. **The shared registration lines.** `tests/CMakeLists.txt` and `tests/README.md` get the directory
   `tests/debugger/guest/`, so that E6a adds `dsptest/` and E6b adds `neutral/` without touching a shared
   file, and `neutral/` can call the function `dsptest/` defines (3.3 item 3). `tests/CMakeLists.txt` adds
   `debugger/guest` behind `add_subdirectory(system)`, beside `debugger/selftest`: the directory needs
   `NULLDC_SYSTEM_TESTS`, `NULLDC_EMULATOR` and `NULLDC_ELF_DIR`, which `tests/system/CMakeLists.txt` creates.
   `tests/debugger/guest/CMakeLists.txt` finds python and sets `nulldc_guest_emulator` and `nulldc_guest_skip`
   as the selftest directory sets its two (`tests/debugger/selftest/CMakeLists.txt`), and then adds every
   subdirectory that has a `CMakeLists.txt`, **in the order of their names**. A CMake function runs in the
   scope of its caller, and a variable that `dsptest/CMakeLists.txt` sets is not visible in its sibling
   `neutral/`: a function of `dsptest/` therefore uses only these two variables, python as the parent found
   it, `nulldc_tests_path` and cache variables (`NULLDC_ELF_DIR`). `tests/README.md` has one row per group
   of targets and three counts in prose (the tests skipped without `NULLDC_ELF_DIR`); every package that adds
   targets owns its rows and the counts in its turn, and the same in the READMEs of
   `tests/debugger/selftest/` and `tests/debugger/contract/`.
5. **The leftovers of J1** (design 6.1.2; M2-D17), in commits of their own:
   - item 1: the members `resetting` and `stopping` of `SuperH4_impl`, their five writes (four in
     `libswirl.cpp`, one in `SuperH4_impl::Stop()`) and `StopForReset()` are removed; `RequestReset()` calls
     `Stop()`;
   - item 2: `emu.startstop`, `emu.startpark` and `emu.stopruns` (`dbg_selftest_emu.cpp`) also read
     `p_sh4rcb->cntx.CpuRunning` and `dbg::g_parked` once the lifetime is `Stopped`.
   Item 3 (the window sessions) stays with W64.
6. **The repository's copy of this document**, in a commit of its own (M2-D65):
   `docs/nulldc3/devtools-integration-m2-round.md`, with a "This copy" box as the design's copy has, its row
   (`M2-ROUND.md`) in the name table at the top of `docs/nulldc3/devtools-integration-design.md`, and its line
   in `docs/nulldc3/README.md`. From this merge on a frozen header cites the round (H6-4: "package E6b of the
   M2 round"), and the repository has to hold what it cites. The design's own text is not touched: that is
   M2G's (3.10 item 5), which also replaces this copy by the file as it then stands.

### 2.5 The two clauses that go with a body

| Header | Clause R6 leaves in | Goes with | The text then is |
| --- | --- | --- | --- |
| `dbg_regs.h`, `Write()` | "..., or kErrNotSupported (-32004) while no writer exists at all." at the end of the return sentence | E7a, in the commit that gives `regs::Write` its body | H6-33 as printed |
| `dbg_mem.h`, `Write()` | "..., or kErrNotSupported (-32004) while the writer does not exist." | E7a, in the commit that gives `mem::Write` its body | H6-35 as printed |

Everything else of the revision is final when R6 is merged. Comments that describe behaviour E6b builds
(`Step`, `Resume`, `Park`, the ARM7 block) are true of the tree from E6b on; between R6 and E6b the functions
they describe are unreachable (`TargetSteps(Target::Arm7)` is false, `dbg_arm7_armed` is 0).

---

## 3. Packages

| Pkg | Repository | Content in one line | Cut from |
| --- | --- | --- | --- |
| **F3a** | `dreamcast-devtools` | the contract of M2: two panel ids, the mock as an M2 server, the runner's M2 rules, `check` on the M2 gate | F3 (4.4), the runner and mock items |
| **R6** | `devcast` (integrator) | header revision 6 with its stubs, the ARM7 wiring of the core, the panel-id list, the leftovers of J1, the repository's copy of this file | 6.3 "header revision 6"; 6.1.2 |
| **E6a** | `devcast` | the sounding guest, the runner of the sound lines, the baseline | E6, its "first step" |
| **E6b** | `devcast` | the resumable burst, ARM7 breakpoints and steps, the ARM7 register writer | E6 |
| **E7a** | `devcast` | memory writes, register writes of the two processors, reset, the three capabilities | E7, the methods |
| **E7b** | `devcast` | the M2 rows of the device tree, read-only, and the two panel actions | E7, the tables |
| **F3b** | `dreamcast-devtools` | the end of protocol v2 compatibility | F3 |
| **F3c** | `dreamcast-devtools` | the register panel, register and memory editing, the narrow rule of the memory view | F3 |
| **F3d** | `dreamcast-devtools` | toolbar run control for the focused target, reset, hotkeys; a smoke spec against the emulator | F3 |
| **M2G** | both (integrator) | the M2 gate on the emulator and the hand-over | the gate of milestone M2 |

Why E6 and E7 are each cut in two. E6: the guest's lines are the baseline that the burst is held to, so they
have to be verified, and what the guest's soak finds fixed, before the burst is touched (`guest-harness.md` 9
items 5 and 8). E7: the methods and the tables are two kinds of work with two risks — a write that executes
stale code, and a tick ten times its size — and the tables are the one part of M2 whose shape may change after a
measurement (M2-D27). F3 is cut as `devtools-m2.md` section 5 proposes: the contract first, because it is the
gate of the emulator packages.

All packages run one after another (section 4), each based on the merge of the one before it in its
repository. "Owns" therefore names what a package may edit **in its turn**; where two packages name the same
file, the later one is based on the merge of the earlier.

### 3.0 The surface in `dbg_core.cpp`, solved

Three packages need the core: the sound side (the ARM7 as a target that steps, its breakpoints handed over), the
methods of E7a, and two buffer sizes of E7b. `core-m2.md` 6.1 shows that no function is edited by two of them, and
that the real surface is the ownership rule, two neighbouring expectations of `core.tick`, the registration files
and the gate. It is cut this way (way A of `core-m2.md` 6.2; M2-D16):

| Need | Where | Who | When |
| --- | --- | --- | --- |
| the ARM7 breakpoint set handed to `dbg::emu` | `PushBreakpoints()` | R6 | before any package |
| `targets[arm7].stepping`, and the two tests of it in `breakpoints.add` and in the steps | the three readers ask `emu::TargetSteps()` | R6 writes the readers; **E6b flips it by the body in its own `dbg_emu.cpp`** | R6; visible from E6b |
| an ARM7 breakpoint address above `addressMax` | `MethodBreakpointsAdd` | R6 | before any package |
| `-32012` of `breakpoints.add` | — | nobody: dropped (M2-D11) | — |
| the ARM7 expectations of `core.tick` and `core.tables` in `dbg_selftest_core.cpp`, and the check of the hand-over | follow `TargetSteps()` (2.4 item 2) | R6 | before any package; the ARM7 half runs for real from E6b on |
| the four methods, `TableCall` with a class, the three capabilities, `flags.writable` in describe | `dbg_core.cpp` | E7a | its turn |
| the `-32004` expectations of the four methods and the capability counts in `dbg_selftest_core.cpp` | | E7a | its turn |
| the `reserve` sizes of `BuildTick` and `BuildDescribe` | `dbg_core.cpp` | E7b | its turn |
| the expectations that follow the tree — node and register counts, and the watch bound of `core.tables`, which takes its other branch for the first time when the tree has more than 256 registers | `dbg_selftest_core.cpp` | E7b | its turn |

So **E6b never opens `dbg_core.cpp` or `dbg_selftest_core.cpp`**, E7a and E7b own them one after the other, and
E6b can show its part of S16 by itself: the day its `TargetSteps(Target::Arm7)` answers true, describe says
`stepping: true`, the core accepts ARM7 breakpoints and forwards ARM7 steps. A package shows a part of S16 with
the runner's `--parts` (F3a; M2-D37), because S16 with only some of its parts is `partial`, which is no pass.

The same question for the test files that three packages register in:

- `tests/debugger/selftest/CMakeLists.txt` needs **no function change** for a guest of the tree.
  `nulldc_debugger_selftest` passes `ARGS` through to `selftest.py`, whose `--guest` takes any path (the
  option `ELF` is for a program of `NULLDC_ELF_DIR` and is not used). A session on the guest of E6a is one
  line, and its run on the forced fallback a second:
  `nulldc_debugger_selftest(<name> ARGS --guest "${nulldc_tests_path}/debugger/guest/dsptest/dsptest.elf")`,
  `nulldc_debugger_selftest(<name>.fallback SELFTEST <name> ENGINES jit CONFIG Debugger:ForceFallback=yes ARGS --guest ...)`.
  Such a test needs no `NULLDC_ELF_DIR`. Two sessions have a further line each. The session of the sound
  side also runs on the path that stays when the batch render goes (PLAN tracks 5 and 10; `aica.NoBatch` is
  `no` by default): `nulldc_debugger_selftest(arm7.persample SELFTEST arm7 CONFIG config:aica.NoBatch=yes ARGS --guest ...)`
  (3.4). And the witness of E7a's discard runs where a missing discard can be seen:
  `nulldc_debugger_selftest(writes.smc SELFTEST writes.mem.cached ENGINES jit CONFIG config:Dynarec.SmcCheckLevel=-1 ARGS --guest ...)`
  (3.5).
  **A test that the run ends is not named under the prefix of a session.** The driver's filter selects every
  test whose name starts with the item and a dot (`dbg_selftest.h`), runs the selected tests in name order and
  prints its summary only when the last one returned: a test `writes.<x>` or `arm7.<x>` that never returns
  would hang the session `writes` or `arm7`. Hence `arm7n.*` beside `arm7.*` and `writesrun.*` beside
  `writes.*`, as the tree has `emurun.*` beside `emu.*`.
- `tests/debugger/contract/CMakeLists.txt` and `contract.py` do need one: `nulldc_debugger_contract` boots
  2ndmix of `NULLDC_ELF_DIR` and nothing else, and `contract.py` has the mode `m1` only. **E6b**, the first
  package that needs more, adds the option `GUEST <path>` of the function (with it the test does not ask for
  `NULLDC_ELF_DIR`) and the mode `parts` of `contract.py`, which passes `--only S00,S16 --parts <...>` and
  `--require-note <text>` through to the runner. Both are options of the runner, delivered by F3a (3.1 item
  3): `contract.py` reads no note out of the runner's output, it takes the runner's exit code (M2-D34,
  M2-D37). E7a adds lines, on 2ndmix and on the guest (3.5); E7b adds the mode `m2` (the whole gate, and its
  second run, which also passes `--require-note`). Each package that delivers a part of S16 runs that part
  against the guest in its own gate: a part that fails there fails in the package that owns the code.
- `tests/debugger/guest/`: E6a's `dsptest/` defines the runner of the sound lines and the function
  `nulldc_sound_neutral`; E6b's `neutral/` calls it (3.3, 3.4). The function uses only what R6's
  `CMakeLists.txt` of the directory sets (2.4 item 4).

Each of E6b, E7a and E7b owns the two files of `tests/debugger/contract/`, its lines of
`tests/debugger/selftest/CMakeLists.txt`, and its rows and counts of the three READMEs (2.4 item 4) in its turn.

### 3.1 F3a — the contract of M2

**Delivers.**

1. Schema (`src/lib/debuggerSchema.ts`): `sh4-registers` and `arm7-registers` in `PANEL_IDS` and `PanelIdSchema`,
   with the registry and tab entries the types force, bound to the existing placeholder component (F3c builds
   the panel; the placeholder is not exported today, so two exports in `Sh4RegisterPanels.tsx` come with it).
   `flags: {"writable": true}` on a register of `debugger.describe` uses the optional `flags` record the schema
   has (M2-D19). **One shape changes:** the params of `state.writeMemory` take `target: sh4 | arm7 | dsp`
   (`TargetProcessorSchema`, as the reads do), in the client copy and in the strict copy. Today the enum has
   two values and the mock validates params with it in front of every handler, so `dsp` would be `-32602`
   there; by M2-D21 it is the in-band `-32004`, decided by the handler as one of the method's own errors
   behind the lifetime. The type of `DebuggerClient.writeMemory` widens with the schema; no file of
   `src/services/` is edited.
2. The mock becomes an M2 server (`devtools-m2.md` 3):
   - `state.writeMemory` with the capability `memory-write`: a write overlay over its fabricated memory, keyed
     by store and offset so that mirrors and the two targets' views of wave memory agree; writable regions and
     refusals as 2.7 and M2-D21; a tick behind every write. **The mock's stores**, which the overlay and the
     two address rules need and the mock does not have today (a byte is a hash of the unmasked address and
     the target): main memory is 16 MiB (area 3 under `& 0x00FFFFFF`, through P0, P1 and P2); wave memory is
     8 MiB, the size its readable map already has, so SH4 `0x00800000 + X` and ARM7 `X` are one byte and the
     ARM7 ring at `0x00200010` is no mirror of anything (the e2e suite pins its addresses); video memory is
     8 MiB in each of its two views, the mirrors of one view agree (`0x04…` with `0x06…`, `0x05…` with
     `0x07…`), and the mock does not alias one view onto the other (the interleaving is the emulator's, held
     by `writes.mem.vram`). The three stores are reached through P0, P1 and P2 alike (`0xA5000000` is
     `0x05000000`), and wave memory is written through either target;
   - `control.reset` with the capability `reset`: paused, a new stop with `reason: "reset"` and no running
     tick; running, `{}` and an event-log entry `dc.system.reset`, nothing published. The breakpoint, watch
     and category tables are behind a reset what they were in front of it, and a resume behind it runs. **The mock's ARM7 is
     held behind a reset**, and by the option and machine method "the ARM7 is held in reset"; it is released
     when the machine has run for 8 emulation steps since (as a guest releases it). While held: `ENABLED`
     reads 0, `callstacks.arm7` is `[]`, the ARM7 ring does not advance, the three ARM7 steps answer `-32012`
     (behind `-32003` while running), and code breakpoints are accepted;
   - `state.setRegister` / `state.editWatch` by M2-D20 (case, error order, value grammar); `ENABLED` of
     `dc.aica.arm7` answers `-32013`; `flags.writable` in describe for the registers it writes; a `PC` of
     `dc.sh4.cpu` with bit 0 set answers `-32013`; a `PC` of `dc.aica.arm7` is stored with bits 0 and 1
     clear; a `CPSR` of `dc.aica.arm7` whose mode bits are none of `0x10`, `0x11`, `0x12`, `0x13`, `0x17`,
     `0x1B`, `0x1F` answers `-32013` (M2-D12, M2-D20, M2-D42);
   - **the mock's ARM7 program has a call**, so that the `bl` rule of item 3 runs on the mock. Today its ring
     is drawn from the instructions that are no branch and ends in `b <base>`, `control.stepOver` is always
     one step and `control.stepOut` a resume to the wrap of the loop. One instruction of the ring becomes `bl`
     to a leaf of two plain instructions and a return, outside the ring; `R14` follows the call and the return
     goes to it. `control.stepOver {arm7}` on the `bl` line is a temporary breakpoint at line + 4 and a resume
     (a running tick, then a stop with reason `step`), elsewhere a step; `control.stepOut {arm7}` is a
     temporary breakpoint at `R14` and a resume. The temporary breakpoint goes at the stop it makes and at
     any other stop. A step of the mock's ARM7 moves nothing of its SH4 (today's behaviour, now a rule of
     the runner: item 3);
   - **the three effects the runner's write rules look at**, so that no rule is bound to a profile and
     `npm run check` runs every one of them (M2-D45): (a) a write of `SR` that changes `RB` while `MD` is set
     exchanges `R0..R7` with `R0_BANK..R7_BANK`; (b) a step of the mock's SH4 from a PC outside its ring and
     its handler ends at PC + 2 (`mockNextAddress` does this already); (c) when the write overlay holds
     `mov #imm,Rn` (`0xEnii`) at the PC, that step executes it;
   - the M2 rows of 3.6.1 in its tree and the two actions. The registers the mock's M1 nodes have today stay
     as they are (e2e specs pin them: `DMAOR` and `CHCR0` of `dc.holly.dmac` among them). The new nodes get
     short lists of their own, at most 8 registers a channel, so that the whole tree stays below 1,000 rows
     (`e2e/device-tree.spec.ts` expects a three-digit count for a search that matches everything);
   - the ARM7 breakpoint address rule of M2-D12, with the mock's key `address & 0x007FFFFC` (its wave memory
     is 8 MiB; today it compares ARM7 addresses exactly); `breakpoints.add` never answers `-32012` (M2-D11);
   - `MOCK_M1_SUPPORT` / `--m1` also hides `memory-write`, `reset`, the M2 rows and the two actions;
   - **`MOCK_M2_SUPPORT` / `--m2`: what the M2 emulator can do and no more** (M2-D45): three targets listed,
     stepping for `sh4` and `arm7` and not for `dsp`, the capabilities of M1 and M2 (no `sgc-capture`), and
     `flags.writable` on the registers of `dc.sh4.cpu` and of `dc.aica.arm7` but `ENABLED` only — a write of
     any other register answers `-32013`. The default mock keeps everything it can do. `MockSupport` gains
     the field that names the writable nodes.
3. The runner (`scripts/contract/`), each rule with a test that goes red when the rule is taken back:
   - `M2_DEVICE_TREE` in `design.ts` and a fifth part of S16, `tree`: the paths, parents and actions of 3.6.1,
     and at least one register on each node 3.6.1 marks; **and each of the twelve register-panel ids
     (`sh4-registers`, `arm7-registers`, the ten `*-registers` of M1) that the tree offers is an action of
     exactly one node** — S01's check of an action is "the listed node has it", which a second node with the
     same action passes, and F3c binds a panel to the node that offers its id (3.8). Applicable iff
     `dc.sh4.cpu` lists the action `sh4-registers` (M2-D24);
   - S01 under the M2 gate also asks for `targets[arm7].stepping === true`;
   - S16 `arm7`, the start. If `ENABLED` reads 0 at its start, the three ARM7 steps are probed for the
     in-band `-32012`, a breakpoint on `dc.aica.arm7.pc` is added and removed (accepted), the part notes
     `arm7: held at the start` and **settles**; else it notes `arm7: clocked at the start`. Settling: resume;
     wait until `ENABLED` has read 1 in every running tick of one second, at least two of them (5 s; never:
     the part fails saying that the ARM7 was not released); pause; the paused tick has to show `ENABLED` 1 and
     a PC of the ARM7 at most `addressMax`, else settle again; at most five settlings in a part (never
     settled: the part fails saying so). Until its first breakpoint is added the part starts over from a
     settling, once, when an ARM7 step answers the in-band `-32012` or a paused tick shows `ENABLED` 0 or a PC
     of the ARM7 above `addressMax`. The reason: a guest may hold and release its ARM7 more than once while
     it starts. 2ndmix does (`spu_init` releases it onto a one-word program, `play_s3m` holds it again, loads
     the player and releases it), and in the first window its ARM7 runs through the top of the address space
     (`0xFFFFFFE8` and on), where the PC is reported as it is (M2-D12) and no line can be asked for. The
     package gates meet that window: with `--only S00,S16` the part starts at the anchor;
   - S16 `arm7`, then as today, and further: `control.stepOver {arm7}` on a line that is not a call is a
     step; `control.stepOut {arm7}` answers `{}`, a running tick or a paused tick with a new `stopId` follows
     within 1 s, a stop within 5 s has reason `step` or names a breakpoint, and when none came in 5 s the
     runner pauses (S04's rule, but for the stop that can come with no running tick: M2-D9); an ARM7 step
     while running is `-32003`;
     `breakpoints.add` with address `2` and with `0x01000000` is `-32602`; `dc.timing.SH4_CYCLES` does not go
     back across an ARM7 step, **and across a `control.step {arm7}` it grows by at most two periods of the
     AICA callback (2 x 145124 cycles)**: from a stop outside a burst the SH4 runs to the next burst and no
     further (M2-D5). **At the stop of the first hit, with the breakpoint still enabled,
     `control.step {arm7}` ends at the breakpoint's address + 4 or at a vector, with reason `step`, and
     leaves `dc.timing.SH4_CYCLES` and the `PC` of `dc.sh4.cpu` as they were** (a step
     from a breakpoint stop that reports the breakpoint again is a broken server, and so is one in which the
     SH4 executes: a burst is suspended at such a stop and the step stays inside it, M2-D4; the cut otherwise
     never steps with an enabled breakpoint on the PC). The breakpoint that was hit, toggled off, is not hit
     (`expectToKeepRunning`);
     enabled again it is; with the category `arm7` muted it is not, unmuted it is; a twin on the same address:
     the stop reports the lower id. **A breakpoint at the site's address + `0x00800000` is hit, and its tick
     has the PC as it is, the site's address** (the address is a mirror of the site on every server: a wave
     memory is 2 MiB or 8 MiB, and breakpoints are compared under its mask, M2-D12; skipped with a note when
     the site is at or above `0x00800000`). **Then the breakpoint is removed, the target resumed, and the breakpoint
     added again, enabled, while the target runs: a paused tick with its id follows within 2 s** (what S20
     asks of the SH4; every other ARM7 table change of the part is made at a stop, where a breakpoint that
     never arms while running would not show). The part's breakpoints are removed then.
     **The `bl` rule.** Two arrivals at a `bl` line are looked for by single steps, **each with 96 steps of
     its own**: the count starts at 0 where the first search starts, and again behind the step-over. (One
     budget of 64 for both, as the third cut had it, can fail a correct server: the second arrival is a
     whole loop behind the step-over, and either search may walk through a FIQ handler.) At the
     first arrival `stepOver` ends in a paused tick with reason `step` at that line + 4 (a running tick may
     come in between). At the second: a step into the callee and, from its first line, `stepOut`, which
     ends at the value `R14` had there, reason `step`; **behind that stop, with no breakpoint listed, the
     target keeps running (`expectToKeepRunning`)** — the temporary breakpoint of a step-over or a step-out
     goes at its stop, and a loop passes its address at every turn. A step on the `bl` line that ended at a
     vector is not the step into the callee: the search goes on, and the steps through the handler are
     charged to the count of that arrival (a guest is built for it: 5.2 bounds the way to the next `bl` and
     the length of a handler). The part notes `arm7: bl stepped over and out`, or that no `bl` was reached
     (an arrival that used its 96 steps);
   - S16 `setRegister`: `R0` carries `flags.writable` in describe; `ENABLED` does not and answers `-32013`,
     also with the value `twelve` (the order of M2-D20); no register of width 0 carries the flag; `name: "r0"`
     and `path: "DC.SH4.CPU"` are accepted; ` 0x1f ` is accepted and `-1`, the empty string, `1.5` and `0x`
     are `-32602`. In every node, the first register with the flag whose value is not `"n/a"` accepts a write
     of the value it reads, and the first without it answers `-32013` (on both processor nodes the first is
     `PC`: on the emulator that write drops the owed remainder of the SH4, which is no guest state).
     `R1` of `dc.aica.arm7`: a round trip.
     `PC` of `dc.aica.arm7`, written as its value with bits 0 and 1 set, reads back as it was (stored
     aligned). `PC` of `dc.sh4.cpu`, written as its value with bit 0 set, is `-32013` and unchanged.
     `CPSR` of `dc.aica.arm7`, where the node lists it with the flag, written as its value with the mode bits
     0, is `-32013` and unchanged. While the target runs, `state.setRegister` of `ENABLED` answers `-32013`
     and not `-32003`: that a write is served while running can be asked with a refusal, without writing
     into a running guest.
     `state.editWatch` with an unused id is `-32602`, and on a watch of `ENABLED` `-32013`. With `MD` set in
     `SR` (else skipped with a note): a write of `SR` that flips `RB` exchanges `R0..R7` with
     `R0_BANK..R7_BANK` in the tick that follows; `SR` is written back;
   - S16 `writeMemory`: a range that starts in main memory and ends above it (`0x0FFFFFF8`, 16 bytes) is
     `-32013` and its first half is unchanged; **65536 bytes are accepted: the runner reads 65536 bytes of
     main memory at the SH4's `defaultMemoryAddress` (`state.getMemorySlice`) and writes the same bytes
     back**, and the tick behind the write is paused with the `stopId` it had; 65537 bytes, and a byte value
     of 256, are sent with that address, are `-32602` and are never stored. (Not at the part's scratch
     address: from there 64 KiB run through the guest's live stack and, with the stack at the top of main
     memory, through the end of the store into its next mirror; and not a pattern: a part that fails half
     way could not take it back.) A missing `target` is `-32602`; `target: "dsp"` is the in-band `-32004`; a
     byte of wave memory
     written through the ARM7's address reads back through the SH4's, **and written through the SH4's it
     reads back through the ARM7's** (and is restored) — the pair is ARM7
     `X` and SH4 `0x00800000 + X`, with `X` the ARM7's `defaultMemoryAddress` under `& 0x001FFFFF` (the
     size of the wave memory is not on the wire, and 2 MiB is the smallest there is); the 16 bytes written
     at the scratch address read back at the same place through the two other regions that map main memory
     (of `0x0C…`, `0x8C…` and `0xAC…` the two the stack pointer is not in); **16 bytes of video memory are
     read at `0xA5000000`, written there with a pattern, read back at `0x05000000` and at `0x07000000`, and
     restored** (one view through three of its addresses; that the other view shows them is the emulator's
     interleaving and `writes.mem.vram`'s to hold); flash, a register window,
     P4 and a store queue answer `-32013`; while the target runs, `state.writeMemory {sh4, 0x00000000, [0]}`
     (the boot ROM) answers `-32013` and not `-32003`; a tick follows every accepted write, paused, with the
     `stopId` it had;
   - S16 `writeMemory`, **the probe that executes what was written** (it needs `register-write` as well;
     without it, it is skipped with a note). It executes one instruction, and that one the runner wrote: a
     PC moved onto a line of the program would execute a guest instruction out of sequence, and its effect
     could not be taken back. A = (R15 − 256) & ~3, the scratch address of the part. Save `PC`, `R0` and the
     two bytes at A. Write `mov #0x5A,r0` there (bytes `5A E0`), write `PC` = A, `control.step {sh4}`. Expect
     `PC` = A + 2 and `R0` = `0x0000005A`. Restore the bytes, `R0` and `PC`. A step may end at an exception
     vector with nothing executed (S04's test: VBR + `0x100` / `0x400` / `0x600`; likely here, because the
     step runs a suspended burst on and accepts what it raised). Then `SPC` reads A, which is what shows that
     nothing executed (`R0` is not compared there: the entry may have exchanged the register banks). The
     runner writes `SPC` back to the saved `PC` and makes the probe once more from the vector, where `SR.BL`
     is set and no interrupt is accepted: it saves the `PC` and the `R0` it finds there, writes `PC` = A,
     steps, expects A + 2 and `0x0000005A`, and restores that `R0`, that `PC` and the bytes;
   - S16 `reset`: first while running (`{}`, no paused tick for 1 s, `stopId` unchanged, an event-log entry
     `dc.system.reset` when the tree lists the event), then while paused as today, and there also: `ENABLED`
     reads 0 and `callstacks.arm7` is `[]` when `arm7` is listed. **The tables are kept, and the part brings
     something to keep**: nothing is listed when it starts (the scenarios and parts before it removed what
     they added), and "what they were" would compare nothing with nothing. Before the paused reset the part
     adds a disabled code breakpoint of the SH4 and a watch (on a register that no listed watch names), and
     flips `muted` of the category `dsp`; the tick of the reset stop lists that breakpoint, disabled, and
     that watch under their ids, and the category as set; the part then removes both and restores the
     category. **Where the server has `register-write`: at the reset stop, with `ENABLED` 0, `R1` of
     `dc.aica.arm7` makes its round trip** (H6-33 and H6-38 promise the ARM7's registers to a held ARM7, and
     the register panel of F3c offers them behind every reset). **Then the machine runs behind its reset**:
     `control.runUntil`, a running tick within 1 s; `control.pause`, a paused tick. (The baseline of an
     emulator run is paused, so the clean-up resumes nothing, and S16 is the last scenario: without this
     step no run of the gate would resume a machine behind a parked reset.) A reset is asked at a pause
     stop only; that the stop of a reset asked at a breakpoint stop names no breakpoint is invariant I7's
     where it occurs, and `core.reset`'s to hold (3.5);
   - the two M1-level rules of 4.4. The pairing as an invariant: in a paused tick whose reason is not
     `host-stopped`, `guest-exit` or `no-game`, of a server whose latest describe lists `arm7` and has
     `callstack`, `ENABLED` reads 0 or 1; with 0, `callstacks.arm7` is `[]`; with 1 it has at least two
     frames, frame 0 at the `PC` of `dc.aica.arm7` without a location, frame 1 at `R14` with location `lr`
     (what S11 asks at its one stop). `DescribeIndex` carries the two facts of the describe, since
     `checkTick` knows neither targets nor capabilities today. And the S09 probe across `addressMax` of the
     SH4;
   - invariant I4 gains: no register of a tick carries `flags` (M2-D19; the strict schema has one register
     shape for describe and ticks and cannot say it);
   - S13 (two clients), on a server with `register-write`: while paused, A writes `R0` of `dc.sh4.cpu` with
     the value it reads, and B gets a tick within 1 s. S13 has no write today, and a server that sends the
     tick of a write to the writing client only would pass;
   - `--parts <scenario>:<part>[+<part>...]` (M2-D37), with the part keys of S16 `arm7`, `writeMemory`,
     `setRegister`, `reset`, `tree`;
   - `--require-note <text>` (repeatable; also with `--milestone`): the run exits 1 unless a note of a
     selected scenario equals the text. The three notes of the `arm7` part — `arm7: held at the start`,
     `arm7: clocked at the start`, `arm7: bl stepped over and out` — are an interface to the emulator's
     contract targets (3.4, 3.6): they are verbatim, and a runner test pins them;
   - `--mock-support m1|m2`: the profile of the mock the runner starts in its own process. Today that is
     `new MockDebugger()` with no options, and `--m2` of `server/mockServer.ts` does not reach it.
4. `package.json`: `check` runs the mock's gate as `--milestone M2` twice, on the default mock and, with
   `--mock-support m2`, on the mock with `MOCK_M2_SUPPORT`; both runs with
   `--require-note "arm7: bl stepped over and out"`. `README.md`: the sections on the protocol, the mock's
   options and the runner (the M2 gate, the second run of M2-D34, `--parts`, `--require-note`,
   `--mock-support`, the notes of the `arm7` part).

**Owns.** `src/lib/debuggerSchema.ts` and its test; `server/` and its tests; `scripts/contract/`,
`scripts/contract-test.ts`; the `check` line of `package.json`; the README sections named. Of the UI: the two
registry entries, the two tab entries, the two exports of the placeholder in `Sh4RegisterPanels.tsx`, and
every e2e expectation that the mock's M2 tree, capabilities, ARM7 program or answers change — in
`e2e/device-tree.spec.ts`, `e2e/device-tree-perf.spec.ts`, `e2e/unsupported.spec.ts`, `e2e/watches.spec.ts`,
the specs that count panels, `e2e/disassembly.spec.ts` and `e2e/breakpoints.spec.ts` (the lines of the ARM7
ring that the call moves), `e2e/toolbar.spec.ts` (its `-32005` test writes an odd PC through the watch editor,
which the mock now refuses: the test moves to a PC that is even and not readable, `0x10000000`, "cannot step:
pc not readable"; `server/mockDebugger.test.ts` pins the same path), and any other spec an answer of the mock
changes — each such change listed in the report. The mock is shaped so that few are needed (item 2: the M1
nodes keep their registers, the tree stays below 1,000 rows, the ring keeps its addresses).

**Must not touch.** `src/state/`, `src/services/`, the panels and views but for the two exports; the emulator
repository.

**Depends on.** This document (sections 3.6.1, 6.1). Nothing of the emulator but the merged M1 tree, for the
observation and the last line of the gate.

**Before the `arm7` rules are written: the observation.** F3a builds the merged emulator with 2ndmix for its
gate anyway, and M1 has all the observation needs (the ARM7's PC and `ENABLED` in ticks,
`state.getDisassembly {arm7}`). On the JIT, the interpreter and the forced fallback it records: the values of
`ENABLED` over the start of 2ndmix with the host time and `SH4_CYCLES` of each change; the ARM7's PC in the
first released window; and whether, once the ARM7 has settled, it offers S16 a breakpoint site (a PC whose
line and next line are not control flow, within 64 steps). The settle times of item 3 are confirmed or changed
by it, and it is in the report. If 2ndmix offers no site, the integrator decides the outcome of that case
(the first run "not shown, with the second run as the holder") before F3a closes (6.3).

**Gate.** `npm run check` with the mock's M2 gate, on the default mock and on the mock with
`MOCK_M2_SUPPORT`: 17 scenarios passed, 0 partial, 0 not applicable each, and each run has the note
`arm7: bl stepped over and out` (the `check` line requires it); the e2e suite green; every new rule
has its red test and is exercised by the mock; **the emulator's targets that execute UI source pass against
the merged emulator with the UI checkout at F3a's tip**: `debugger.contract.*` — the M1 gate on the JIT, the
interpreter and the forced fallback with the new invariants and the new probe, and
`debugger.contract.hash.idlebp` with `.interp`, which run this package's runner in its `--idle-breakpoint`
mode —, `debugger.disasm.dsp_playground` and `debugger.net.interop_ws`. In that window one emulator test is
red by
construction, `debugger.regs.panels` (35 ids against 37): R6 closes it. If an M1-level rule fails on the merged
emulator, F3a does not touch the emulator and does not weaken the rule: it stops and reports; the integrator
fixes the emulator with R6, and this last line of F3a's gate is run on R6's merge.

**Verifier.** For every new rule a broken server (a patched mock, or a proxy in front of it) that the runner
has to fail with the expected line, and a legal variant it has to pass. Broken, among them: an ARM7 that is
never released; a disabled or muted ARM7 breakpoint that stops; a twin reported under the higher id;
`SH4_CYCLES` that goes back; `flags` in a tick; an odd `PC` accepted; an ARM7 `PC` stored as given; `dsp`
answered `-32602`; a write without a tick behind it; a written instruction that does not execute; `SR.RB`
flipped without the exchange; a callstack whose frame 1 is not `R14`; a `stepOver` on a `bl` that stops in the
callee; a `stepOut` that stops elsewhere than `R14`; a step from an ARM7 breakpoint stop that reports the
breakpoint again; an ARM7 breakpoint added while running that never arms; a write to a running target
answered `-32003`; an ARM7 `CPSR` with the mode bits 0 accepted; the tick of a write sent to the writing
client only; a 65536-byte write that is refused, or that changes a byte; an ARM7 step from a breakpoint stop
in which `SH4_CYCLES` or the `PC` of the SH4 moves, and an ARM7 step across which `SH4_CYCLES` grows by three
AICA periods; a temporary breakpoint of the ARM7 that outlives its stop (the target stops again behind the
step-out); ARM7 breakpoints compared without the mask (the mirror is never hit), and a mirror hit whose tick
shows the breakpoint's address as the PC; an ARM7 register write refused while `ENABLED` reads 0; video
memory refused, or stored at one of its addresses only; wave memory refused through the SH4's address; a
reset that empties the breakpoint or the watch table, or that forgets a category; a reset behind which no
running tick comes; `sh4-registers` offered by a second node. Legal: other M2 register lists, a
server without `reset`, a held ARM7 at the start of S16, an ARM7 that is released, held and released again, a
step that ends at a vector (in S16's probe too, and on a `bl` line), a guest without a `bl`, a program whose
two arrivals at a `bl` are 80 steps each from where their search starts. Mutations of the mock that the mock's
own gate has to catch: a write stored before the range is checked, a reset that publishes `running`, a reset
that leaves the ARM7 clocked, a writable flag on `ENABLED`, `r0` refused, a ring without its `bl` (the
required note is missing: exit 1). `--parts`: a named part that is not
applicable exits 1, an unknown part and `--parts` together with `--milestone` exit 2. `--require-note`: a
text no selected scenario noted exits 1, and so does one that differs from a note by a character. The M2 gate
fifty times on each of the two mocks, no flake.

### 3.2 R6 — header revision 6 (integrator)

**Delivers.** Section 2, all of it: the declarations of 2.1, the comments of 2.2 (H6-19 among them), and
what 2.4 lists (stubs, the wiring of the core, the panel-id list, the registration lines, the leftovers of
J1, the repository's copy of this document). It edits no text of the design: section 7 is applied at M2G.

**Owns.** The frozen headers; `dbg_arm7.h` and the stub `dbg_arm7.cpp`; the stub functions in `dbg_emu.cpp`;
the three places of `dbg_core.cpp` and the ARM7 expectations of `core.tick` and `core.tables` in
`dbg_selftest_core.cpp`; the `panel_ids` list of
`tests/debugger/regs/regs_test.cpp`; `tests/CMakeLists.txt`, `tests/README.md` and a new
`tests/debugger/guest/CMakeLists.txt`; for 6.1.2: `hw/sh4/SuperH4_impl.h/.cpp`, the five statements of
`libswirl.cpp`, three tests of `dbg_selftest_emu.cpp`; for 2.4 item 6: the new
`docs/nulldc3/devtools-integration-m2-round.md`, one row of the name table of
`docs/nulldc3/devtools-integration-design.md` and one line of `docs/nulldc3/README.md`.

**Depends on.** F3a merged (the two panel ids in the UI's schema).

**Gate.** The five builds compile: the default build, the builds with `-DNULLDC_DEBUGGER=OFF`,
`-DTARGET_NO_REC=ON` and `-DHAS_TA_LLE=OFF`, and the generic host (the fifth configuration of P-desktop: an
unknown `CMAKE_SYSTEM_PROCESSOR`; no recompiler, no virtual memory layout, the debugger on), whose 2ndmix state
hash is unchanged; `tests/arm7` and `tests/debugger/regs/aica` build (they compile sound sources without the
debugger). All ctest targets green twice, none skipped, the number of targets stated in the report (A.0),
`debugger.regs.panels` among them. The state hashes of 5.7 and the M1 gate on three engines unchanged;
`debugger.describe` of a headless emulator is byte for byte what it was, both captures from builds that
embed no UI bundle (A.0: `ui` in describe is the bundle's version). The header texts equal section 2. The
copy of 2.4 item 6 equals this file but for its box, and has no home path, host name or user name in it.

**Verifier.** Compares every comment and declaration with section 2, character for character but for wrapping.
Searches the tree for `resetting`, `stopping`, `StopForReset`: nothing left. Mutations: a `g_parked` left set
at the end of a thread, and a run flag left set, each turn one of the three selftests of 6.1.2 item 2 red. With
`TargetSteps(Target::Arm7)` forced true over the stubs, `core.tables` and `core.tick` follow it and a step of
the ARM7 answers `-32004` "on this backend" — nothing hangs; and in that forced build two mutations of
`PushBreakpoints` (a disabled ARM7 breakpoint handed over; ids and addresses shifted by one) turn the
hand-over check of `core.tables` red. ThreadSanitizer run of the session `emu`.

### 3.3 E6a — the sounding guest and the sound lines

**Delivers.**

1. `tests/debugger/guest/dsptest/`: the guest of section 5 — sources, `gen.sh`, the committed ELF, the
   committed ARM7 image with its label file for the selftests, a manifest of SHA-256 sums (of the six files
   5.3 names, and of no other), a README.
2. The runner of the sound lines in the same directory, `soundlines.py` (a script in the style of
   `tests/debugger/contract/contract.py`): one run is `Debug.AicaHash=<N>`, headless, `audio:backend=none`,
   the pinned clock, a sandbox of its own; it parses the line. A second kind of run gives the exit line of
   the same program: `config:Exit.AfterVblanks=<V> , config:Exit.StateHash=yes` in place of the harness key
   (the harness ends the process without an exit line). Modes:
   - *same* (two runs of one binary, one line), *sounds* (`samples` is none of the `N + 1` hashes of
     silence, M2-D32), *engines* (the JIT's line and the interpreter's agree in the fields its argument
     names: `samples`, `regs`, `arm7`, `ticks`, `cycles` unless item 3 has it otherwise);
   - *record* and *compare* (a file of lines outside the tree, for a before / after between two builds);
     both also take the exit line with its state hash, as a second run;
   - *neutral* `--selftest <filter> [--expect <text>]...` (E6b's neutrality targets; M2-D43). Four runs with
     the same N, V and keys: the plain line, the line of a run with `Debugger:SelfTest=<filter>`, the plain
     exit line, the exit line of a run with the session. It passes when the two lines are equal in every
     field; the two exit lines are equal in every part of the state hash (the fields of `mode_same_hash` of
     `tests/debugger/selftest/selftest.py`); and each of the two session runs has no `SelfTest:` line with
     `: FAILED: ` or `: FAIL, `, has at least one with `: ok: `, has every `--expect` text in a `SelfTest:`
     line, and has **no** summary line `SelfTest: <p> passed, <f> failed`. A session run ends by the harness
     or by the exit key, never by the session: the test of such a session never returns true, and a summary
     line means the session ended the process before the run was over.
3. ctest targets: `debugger.guest.dsptest.files` (the manifest; `gen.sh --check` where the assemblers exist),
   `debugger.sound.lines.dsptest.<jit|interp>.<batch|persample>` (same, sounds, `ticks`),
   `debugger.sound.engines.dsptest.<batch|persample>`, `debugger.sound.lines.2ndmix.<...>` (same; needs
   `NULLDC_ELF_DIR`, a test whose data is missing without it).
   **What `engines` compares.** The guest is built so that the two engines agree in every field but `sh4`
   (5.1), and `engines` compares the fields the report of E6a shows equal. If `samples`, `regs` or `arm7`
   differ, E6a stops before the soak and reports the first tick at which the two lines part and which write
   of the SH4 lands in another tick there (the guest has one SH4-timed write behind its start: the pan write
   of `g_irq`). The integrator then decides whether the guest changes (`g_irq` without a heard write) or the
   target compares fewer fields; the soak and the baseline come afterwards. The baseline itself does not
   depend on the agreement: it is one build against itself (5.5).
   And, in the `CMakeLists.txt` of the directory,
   the function `nulldc_sound_neutral(<name> SELFTEST <filter> [EXPECT <text>...])`, which makes the four
   targets `debugger.sound.neutral.<name>.<jit|interp>.<batch|persample>` in the *neutral* mode with N = 2000
   and V = 90 (it uses only what the `CMakeLists.txt` of `tests/debugger/guest/` sets: 2.4 item 4; its first
   call from another directory is E6b's). E6a registers none with it; it shows the mode on a session that exists and never ends by
   itself (`emurun.reads`, which the mode `same-hash` of the selftests runs on 2ndmix today), in its report
   and not as a target.
4. `docs/nulldc3/automated-runs.md`: the key `Debug.AicaHash`, the line, that it ends the process without an
   exit line.
5. A soak and a memcheck run of the guest, and whatever they find (section 5.4).
6. One observation on the merged tree, in its report: a paused tick of 2ndmix (the ARM7's PC, the word at
   ARM address `0x80`, `SCIEB`), which confirms or refutes `guest-harness.md` 2.3 (M2-Q3). The other
   observation the first cut had here — what the ARM7 of 2ndmix offers S16 — is F3a's, made before the runner
   is closed (3.1).

**Owns.** `tests/debugger/guest/dsptest/**`; the `Debug.AicaHash` paragraph of `docs/nulldc3/automated-runs.md`;
its rows and counts in `tests/README.md`.

**Must not touch.** `libswirl/` — with one exception: a defect of uninitialised or host-dependent sound state
that its soak or memcheck run finds is fixed in `hw/aica/` or `hw/arm7/`, in a commit of its own that says what
was found, and reported. The harness line stays as 5.6 has it.

**Depends on.** R6.

**Gate.** All ctest targets green twice, none skipped, the number stated (A.0), the targets of item 3 among
them. Soak: 50 runs of each of the four configurations of the guest, half of them with the machine loaded, one
line each. The lines of the two engines agree in the fields item 3 fixes: the five the guest is built for,
or fewer by the integrator's decision that item 3 describes, which the report then records. `gen.sh --check`
reproduces the committed files byte for byte. **The FIQ entries are counted** (5.2), **by the FIQ test that
makes the entry and not by its return address**: a scratch build of the emulator, of which nothing is
committed, prints for every entry the tick (burst) it is made in, its return address and **the number of the
`Step` of its `StepMany` call whose FIQ test made it**, and the report has three numbers for each of the four
configurations, over the 2000-tick run:

- made by the first `Step`: the first kind, an entry at the first instruction of a sample;
- made by the second `Step`: the second kind (inside a sample, behind the `msr` that ends the F window) with
  that `msr` as the first instruction of its sample — the time step in front of it raised the request while
  F was set;
- made by the third or a later `Step`: the second kind with at least one instruction of the sample in front
  of that `msr`.

**The gate is on the third number: at least 100 entries made by the third or a later `Step`, and no stretch of
more than 32 ticks without one** (the report has the longest stretch; 32 is half the 64 bursts in which
`arm7.step.fiqbehind` has to meet one: 3.4). With fewer, or with a longer stretch, the window is widened
before the baseline — a wider window is a longer loop as well, which moves where the samples end in it. The
third class is the one E6b's two tests can count (3.4: under steps the stop on an `msr` that starts its sample
stands in a new phase, `out[1]` has moved in front of it, and the step is not counted), and the only one that
shows a debug loop without the FIQ test where it goes on with a suspended sample. And its share is not left
to an expectation. `StepMany` starts every sample at 0 ticks and drops the overshoot of the one before, so
where the loop stands when a time step overflows the timer is the orbit of a deterministic map on the
positions of the loop, and it can stay on few of them: a guest whose window is only ever met in front of that
`msr` passes a count of the second kind as a whole with not one entry the two tests count, and E6b, which
may not touch the guest, would find that out behind the baseline (M2-D66). The return address
alone does not tell the kinds apart: an entry of the first kind at a sample that starts with the instruction
behind that `msr` has the one every entry of the second kind has, `a_fwin_end` + 8 (`CPUFiq` stores the next
PC + 4 either way), and by the figures of 5.2 a run has 150 to 200 of those — a count by address reads
"at least 100" on a guest with no entry inside a sample at all, the unlucky phase the count is there to find.
On this guest every entry of the second kind has that return address; one with another is in the report as
a finding. The disassembly of the two loops
and of the handler **as the emulator prints it** is in the report, with the two bounds of 5.2 counted in it,
and the mode of the ARM7 that `a_main_loop` runs in (5.2).
The state hashes of 5.7 and the M1 gate unchanged (also behind a
fix of the exception above; a fix that moves a hash is stopped and reported).

**Verifier.** Rebuilds the guest from source with its own toolchain call and compares bytes. Makes the guest
silent by `MVOL` 0, and by no key-on at all (neither the table's nor the one `a_fiq` makes every 64th
interrupt, which would sound from tick 26 on): *sounds* fails in both modes. With the ARM7 never released,
*sounds* fails in per-sample mode (no pair exists: `samples` is the offset basis) and **passes in batch
mode**: the SH4 has keyed the channels on before it releases the ARM7 (5.2), and the batch render steps the
64 channels in every callback, with or without the ARM7 — only the per-sample path sits behind the ARM7's
clock. There the verifier sees instead that the line differs from that of the released run in `samples`,
`regs` and `arm7`. (The key-on does not move into the ARM7 for this: 5.2 keeps everything the SH4 does to the
sound in front of the first AICA tick.) Alters one sample in a scratch build (as E3's verifiers did) and sees
the lines move. Checks by
disassembly that the properties section 5.2 asks for are there — **with the emulator's disassembly of the
lines (`state.getDisassembly` of a debugger session on the guest) and the runner's test of a control-flow
line (5.2), not objdump's**: the plain run in the ARM7 loop, with no literal load in it; the `bl`; the F
window, with the instruction in front of `a_fwin_end` one of its plain ones; the two bounds (the way to the next `bl`, the length of the handler); the FIQ period; the SH4
labels; the folds into registers — the loop's with its clear of `SAMPLE_DONE`, the handler's with `lr` and
`SPSR` —; that the SH4 program leaves FPSCR alone; **that `g_main_loop` goes back by `bra` and has no
`jmp`, `jsr` or `rts`** (the mirror case of E7a's `writes.reg.pc` rests on it: 5.2); and that no SH4
instruction stores into a page that
holds SH4 code. Counts, in a scratch build of the emulator that prints for every FIQ entry its tick, its
return address and the number of the `Step` of its `StepMany` call whose FIQ test made it, the entries of the
2000-tick run by that number — 1, 2, 3 or more —: at least 100 made by the third or a later `Step` in each of
the four configurations, and no stretch of more than 32 ticks without one (the gate; 5.2). **Not by the
return address:** an entry by the first test of a sample that starts behind the `msr` of `a_fwin_end` has
the same one, `a_fwin_end` + 8, and is of the first kind. **And not the second kind as a whole:** an entry
made by the second `Step` — the `msr` starts its sample — is one the two tests of E6b do not count. Moves
one FIQ entry by an instruction in a scratch
build of the emulator, with the instruction count of the tick kept, and sees the `arm7` field of the line
move, whichever entry of the burst it is — one of the first kind, and one made by the third or a later
`Step`, chosen by that number and not by where it returns to:
the folds of 5.2 are what makes the line a witness of the order inside a burst. The *neutral* mode: it fails a
session that prints a `FAILED` line, one that prints no `ok` line, one that lacks an `--expect` text, one that
ends by itself, and one whose run differs from the plain run in the line or in a part of the state hash. Looks
for a user name, a host name, a date or a path in the committed files.

### 3.4 E6b — the resumable burst and ARM7 run control

**Delivers.**

1. The burst machine in `Dreamcast_impl` (M2-D1, M2-D2): state `{suspended, i, ticks_done, arm_on}` outside
   `Arm7Context`; `AicaUpdate` starts with `verify(!suspended)`, latches `arm_on` once, calls
   `dbg_arm7_OnBurstUnclocked()` when the ARM7 is held and `dbg_arm7_armed` is not 0, runs
   `32 x { RunSample(256, &ticks_done, true); libAICA_TimeStep(); }` and then `AICA::Update(32)`;
   `SoundBurstSuspended()` and `RunSoundBurst(checks)` (2.3); the discard in `Reset()`; in `dc_run`, behind
   the loop and in front of `SaveRomFiles` and the stop callback, `RunSoundBurst(false)`; the four counts of
   `SoundBurstCounts()` (2.3), kept in the burst struct in every build.
   `SoundCPU::Update(cycles)` keeps what it does for its callers outside the emulator's loop.
2. `ARM7Backend::StepIrq` / `StepExec`, `SoundCPU::RunSample`, the debug loop (2.1 H6-1, 2.3), with nothing
   added per instruction on the path of a run that uses no debugger (2.3; the host-cost bullet of the gate).
   **And the suites that run the two halves** (M2-D60): `arm7m_step()` of `tests/arm7/arm7_machine.cpp` runs
   `StepIrq(ctx); return StepExec(ctx);` when the environment variable `ARM7M_STEP` is `halves` (read once),
   and `tests/arm7/CMakeLists.txt` registers `arm7.vendor.halves`, `arm7.directed.halves` and
   `arm7.wren7.<suite>.halves` (four suites) with it, each beside its plain twin, under the same conditions
   and on the same `baseline.txt`.
3. `dbg_arm7.cpp`: everything H6-1 and H6-5 declare; a count of the checks made, for the selftests, in a
   private header of the package (`dbg_arm7_private.h`, beside `dbg_core_private.h` and `dbg_regs_private.h`).
   That header is the debugger's: what emulator code counts is `SoundBurstCounts()` (2.3).
4. `dbg_emu.cpp`: `TargetSteps(Target::Arm7)` true; `emu::Step` for the ARM7 (`arm7::StepCheck`, three
   actions); `OnSoundStop`; the park loop by H6-17 — step "a" in front of every leave and every SH4 step, the
   ARM7 step in the park context, the third way out for a step that waits (nothing published, `g_parked`
   kept, the hook as during a step, the wait counted as a step for the resume a thread exit leaves owed, the
   state "a step is executing" ended at the next `Park()` whatever it finds), the resume that stays parked;
   `arm7::OnStop()` beside `sh4::OnStop()` wherever that is called, `arm7::OnPcWritten()` in
   `OnGuestStateReplaced()`; `arm7::PrepareResume()` for every leave H6-5 names — a resume, an SH4 step with
   or without a burst suspended, the first `Park()` with a resume owed — and not for a step-over on a `BL` or
   a step-out of the ARM7, whose functions arm the exemption themselves.
   `NOTICE_ARM7_FALLBACK` by H6-21: the backend captured at the first `Park()` of a thread through
   `SoundCPU::DebugIsRecompiler`; the notice refreshed when a table call changes `dbg_arm7_armed`; the change
   logged as `dc.debugger.engine` (H6-42); and the stop's half of the rule carried by a flag of the park loop,
   as `e.stepFallback` carries the SH4's: set at every leave of the park context, and when a suspended burst
   is run on, with a recompiler selected and `dbg_arm7_armed` not 0 or a burst suspended; `EnterStop()` adds
   the notice from it and clears it. (A step and a temporary breakpoint disarm in `arm7::OnStop()`, in front
   of the publication: without the flag the stop of a fallback step would carry no notice.)
5. `SoundCPU_impl::DebugSetReg` and `DebugIsRecompiler` (H6-38, H6-6).
6. The selftest session `arm7` on the guest of E6a (`dbg_selftest_arm7.cpp`; registered by the three lines
   of 3.0: on the JIT, the interpreter and the forced fallback in the default setting, and on the JIT and the
   interpreter with `aica.NoBatch=yes`), the three tests of the neutrality targets
   in the same file (`arm7n.idlebp`, `arm7n.hitresume`, `arm7n.steps`: sessions that never end by
   themselves), the change of `emu.step` (`dbg_selftest_emu.cpp`: the ARM7 answers by its state, the DSP
   `-32004`), the neutrality targets and the contract targets below.

**Owns.** `libswirl/libswirl.cpp`, `libswirl/libswirl.h`; `libswirl/hw/arm7/**`; `libswirl/hw/aica/**` (no
change is expected there in M2; the directory is its to keep the sound side in one hand);
`libswirl/debugger/dbg_arm7.cpp`, the new `dbg_arm7_private.h`, `dbg_emu.cpp`, `dbg_selftest_emu.cpp`, the new
`dbg_selftest_arm7.cpp`; `tests/arm7/arm7_machine.cpp` (where an interface grew, and the two halves of
`arm7m_step()`) and `tests/arm7/CMakeLists.txt` (the six `.halves` registrations), with the rows of
`tests/arm7/README.md` that name them. Tests: a
new directory `tests/debugger/guest/neutral/` (its `CMakeLists.txt`, picked up by R6's
`tests/debugger/guest/CMakeLists.txt`, has the three calls of `nulldc_sound_neutral`); the *neutral* mode of
`tests/debugger/guest/dsptest/soundlines.py` and the function `nulldc_sound_neutral`, which E6a wrote for
this package and nothing else uses — a defect found there is E6b's to fix; its lines of
`tests/debugger/selftest/CMakeLists.txt`; `tests/debugger/contract/CMakeLists.txt` and `contract.py` (the
option `GUEST` and the mode `parts` of 3.0, and its targets); its rows and counts in `tests/README.md`,
`tests/debugger/selftest/README.md` and `tests/debugger/contract/README.md`.

**Must not touch.** The frozen headers; `dbg_core.cpp`, `dbg_selftest_core.cpp`; `dbg_regs*.cpp`, `dbg_mem.cpp`,
`dbg_selftest_regs.cpp` (which calls `SoundCPU::Update` and must pass unchanged); `hw/sh4/`, `jit/`, the other
device directories; the guest's files (`dsptest.s`, `arm7.s`, `gen.sh`, `dsptest.elf`, `arm7.bin`,
`dsptest_labels.inc`, `SHA256SUMS`: it uses the labels; a guest change is E6a's and needs a new baseline) and
the other modes of the runner; the UI repository — a defect of a runner rule that its contract targets meet is
reported, and the integrator has it fixed there.

**Depends on.** E6a merged, and **the baseline taken on that merge** (section 5.5).

**Gate.**

- Builds: default, `-DNULLDC_DEBUGGER=OFF`, `-DTARGET_NO_REC=ON`, `-DHAS_TA_LLE=OFF` and the generic host
  (as for R6: it compiles, its 2ndmix state hash is unchanged); `tests/arm7` and `tests/debugger/regs/aica`.
  **The gate of `StepIrq` / `StepExec` is the six `.halves` suites** (`arm7.vendor.halves`,
  `arm7.directed.halves`, `arm7.wren7.<suite>.halves`: Delivers 2), in either shape of 2.3; the plain
  suites hold `Step` as every run executes it, and in the shape E6b is told to prefer they never enter the
  two halves. All ctest targets green twice, none skipped, the number stated (A.0).
- **Before / after:** the sound lines of the guest (N = 2000) and of 2ndmix (N = 40000), both `aica.NoBatch`
  settings, JIT and interpreter, are identical between the base and the tip, whole lines. The state hashes of
  5.7, and the exit state hash of the guest at 90 vblanks, are identical. **And of the
  `-DNULLDC_DEBUGGER=OFF` build**, which is another program on the changed path (`dbg_arm7_armed` is the
  `NoCount` stand-in there, `dbg_arm7_Check` an inline 0, and the moved loop of 32 and `RunSample` exist in it
  too): its sound lines of the guest and of 2ndmix and its state hashes equal those of the default build, at
  the base and at the tip (design 5.7, first item; the verifier's one extra build).
- **Host cost** (the other half of the rule of the round), in two checks. One count cannot tell the two costs
  apart: both workloads keep the ARM7 busy for the whole 256 ticks of a sample, 28 to 43 instructions
  (`sound-burst.md` 1.3), so one host instruction added per ARM7 instruction costs about as much as the
  per-sample overhead that is allowed.
  - **(a) Nothing per ARM7 instruction.** The compiled code of `ARM7Backend::StepMany`, and of
    `ARM7Backend::Step` where the compiler emits it out of line, is the same in the base and the tip of the
    default build: the disassembly of the two functions, instruction text for instruction text, once
    addresses are named by symbol (the method of package J1's `fn_disasm.sh`). Leaving the source of both
    functions alone gives that by construction (2.3).
  - **(b) Per sample.** User-mode instructions of a whole run, tip minus base, are at most 48 x 32 x N (the
    second virtual call, its test, and the counts of `SoundBurstCounts`, two increments a sample, are all
    inside the 48): for
    the guest at N = 2000 (3.1 million) and for 2ndmix at N = 40000 (61 million), on the JIT and on the
    interpreter, three runs each, the medians compared (two runs of one binary are a few thousand
    instructions apart in 10^11: J1's counts). The counter is the `perf_event_open` wrapper of package J1
    (`agents/nulldc3/j1/scripts/icount.c`; the `perf` tool is not installed for this kernel), copied into the
    package's work directory; its driver script is written anew, with relative paths and with
    `Debug.AicaHash` as what ends the run. Nothing under `tests/` counts instructions.
  - Speed is reported and decides nothing by itself: `tests/perf/perf.py` on 2ndmix with the `none` renderer,
    in the `-DHAS_TA_LLE=OFF` build, five runs of base and tip in turn; the medians by CPU time differ by no
    more than the spread `tests/perf/BASELINE.md` gives for two rows of one binary on a quiet machine
    (1.5 %). Where they differ by more, (a) and (b) decide.
- **Neutrality with the debugger in use** (M2-D43). Twelve ctest targets,
  `debugger.sound.neutral.<idlebp|hitresume|steps>.<jit|interp>.<batch|persample>`, on the guest, each the
  *neutral* mode of 3.3 on one test of the session file. Each target holds two things against the plain run
  of the same build: the sound line at N = 2000, whole, and the exit state hash at 90 vblanks, part for part
  (the line has no field for wave memory, main memory or the scheduler; the hash has). The guest's SH4 does
  not sleep, so both are held whole (on a sleeping guest a sound stop is the documented skew of 2.4). A test
  that armed nothing would pass both, so each test says what it did in `ok` lines that its target requires
  (`EXPECT`):
  - `idlebp`: an active ARM7 breakpoint on an address the guest never executes. Late in the run the test
    takes one stop behind a burst (an event breakpoint on `dc.aica.timer`, then removed and resumed) and
    requires there that `dbg_arm7_armed` is not 0 and that the checks made since the breakpoint was set are
    at least 32 times the bursts the scheduler started since (`SoundBurstCounts()`, `out[0]`: 2.3; the
    tick count of the harness exists only in the run with `Debug.AicaHash`): the debug loop ran every sample;
  - `hitresume`: 200 stops at breakpoints, each resumed: at least 50 on `a_main_loop` and at least 50 on
    `a_fiq`, in at least 100 different bursts (a breakpoint in the main loop would be hit 200 times within
    two bursts: the test removes it at each stop and sets it again at a stop of that event breakpoint);
  - `steps`: single steps of the ARM7, then a resume, until each kind was taken at least 20 times: from a
    stop with no burst suspended, across a time step of the AICA, across the end of a burst; **and until a
    fourth kind was taken at least five times: a step from the `msr` that ends the F window of
    `a_main_loop` (label `a_fwin_end`) which ends at `0x1C`, counted only when it stayed inside one ARM
    phase: `out[1]` of `SoundBurstCounts()` (2.3) reads the same at the stop in front of the step that
    arrived on the `msr`, at the stop on the `msr` and at the stop of the step** (a step of the `msr` that
    is the first of its run of steps has no step in front of it and is not counted). No ARM phase ended in
    between: the `msr` was stepped in a sample that had already executed an instruction, no time step fell
    into the step, and the entry was made by the FIQ test of the loop that goes on with a suspended sample
    — a FIQ entered inside a sample that was gone on with: in the plain run, an entry made by the third or a
    later FIQ test of its sample, which E6a's gate counts for this (3.3, 5.2: at least 100 in the 2000
    ticks, no 32 ticks without one; by the expected share about every second or third stepped burst has
    one). A step of that
    `msr` which ends at `0x1C` and is not counted shows nothing of the kind: with the `msr` as the last
    instruction of its sample, or with the stop on it in front of the first instruction of one (a new phase
    again: 2.3; in E6a's count the entry made by the second test of its sample), the entry is made in a new
    phase, with the same `R14_fiq`, about once in ten bursts — also
    by a loop that never looks at the FIQ while it goes on with a suspended sample. The test is not
    500 steps, as the first cut had it, but as many as it takes: a sample is some tens of instructions and a
    burst more than a thousand, so tens of thousands of steps, which cost no emulated time inside a burst.
  A session whose filter selects one of these three ends only by the run (3.3 item 2).
- The session `arm7`, on the JIT, the interpreter and the forced fallback, **and with `aica.NoBatch=yes` on
  the JIT and the interpreter** (`debugger.selftest.arm7.persample`, `.interp`: the third line of 3.0). The
  default setting is the batch path without the DSP, which PLAN removes; on the path that stays, the time
  step renders and hands samples to the audio stream, now also in the park context, and reset, host stop and
  exit on a suspended burst, the same-slice cases, `arm7.step.sample` and `arm7.events.park` have to hold
  there too. At least these tests (the names are fixed, the list may grow; no test named `arm7.<x>` is one
  that the run ends: 3.0):
  `arm7.burst.plain` (32 / 32 / 1 calls per burst with the ARM7 clocked, 0 / 0 / 1 with it held:
  `SoundBurstCounts()` of 2.3 read at two stops with at least one burst between them — pauses: a held ARM7
  raises no timer event —, the differences of `out[1]`, `out[2]` and `out[3]` against that of `out[0]`);
  `arm7.bp.hit` (a breakpoint on `a_main_loop` stops within one burst of the resume: what the idle
  breakpoint of the neutrality target would do on an address that is executed), `.lowestid`, `.mask` (a
  category that is not active stops nothing and arms nothing), `.mirror`, `.fiqvector` (a breakpoint on `0x1C`
  is hit; a hit elsewhere is not followed by a detour through the handler), `.resume` (the exemption: once;
  and its three edges — **a breakpoint added behind the leave and in front of the next burst**: at the stop
  of a step that completed at the end of its burst (nothing listed, none suspended, the ARM7 standing on
  some instruction P: the stop `arm7.step.burstend` takes), a thread of the test queues a table call that
  lists a breakpoint on P, and the test resumes while that call waits; the park loop leaves first, the call
  is served at the first slice end, and the next burst stops in front of P with every register of the ARM7
  as the step left it (the edge is taken again when the handler's counter shows that a FIQ was entered in
  front of P: its check ends an exemption unused). An exemption that was armed at the leave, with `dbg_arm7_armed` 0, and that outlived
  the change of the flag would run through P there; a client that sends `control.runUntil` and
  `breakpoints.add` in one go can do the same. (The edge the third cut had here — the last breakpoint
  removed at a breakpoint stop and another added while the machine runs — shows nothing: the first check
  that is made ends an exemption, used or not, and it is at P one time in a loop's length.) **A step that
  completed at a burst with the ARM7 held while it stood on address
  0**, then a breakpoint on address 0, the SH4 sent to `g_release_arm7` and a resume: the release stops at 0
  — an exemption wrongly armed there would be for address 0 and would run through. The ARM7 is brought to
  stand on 0 by a release that is held again before a burst ran: with the ARM7 held, the test sends the SH4
  to `g_release_arm7` with an SH4 breakpoint on `g_main_loop`, and from that stop to `g_hold_arm7`, and
  steps the ARM7; at the stop of that step the PC of the ARM7 has to read 0 and `ENABLED` 0, else the test
  starts the edge over —; a FIQ entered in front of the exempt instruction with a
  breakpoint on `0x1C`: it stops at the vector), `.completion` (a resume and an SH4 step whose burst hits a
  second ARM7 breakpoint: parked, a new `stopId`, no `Running`, the SH4 command dropped), `.insleep`;
  `arm7.step.suspended` (PC + 4, a new `stopId`, cause `Step`, no `Running`, the scheduler's time and the SH4's
  PC unchanged), `.sample` (a step across a time step of the AICA: the timers moved by one sample),
  `.burstend` (the tail ran once, nothing suspended), `.frompause` (no `Running`; the SH4 ran one period at
  most), `.fiq` (complete at `0x1C` with nothing executed), `.fiqbehind` (a step of the `msr` that ends the F
  window of `a_main_loop`, in a sample that is gone on with. The breakpoint is on the instruction in front of
  the `msr`, `a_fwin_end` - 4, the last plain instruction of the window (5.2); at each stop the test steps
  twice — onto the `msr`, then the `msr` — and **counts the pair only when `out[1]` of `SoundBurstCounts()`
  reads the same at the three stops**: no ARM phase ended, so the `msr` was stepped in a sample that had
  already executed an instruction and no time step fell into the step. A counted pair ends at the line
  behind the `msr` or at `0x1C`, and one that ends at `0x1C` has to come within 64 bursts (every entry that
  the third or a later FIQ test of its sample makes in the plain run is such a pair here, and E6a's gate
  leaves no 32 ticks without one: 3.3); at `0x1C` the
  `msr` has executed and `R14_fiq` is the address behind it + 4 — the FIQ was entered inside the sample
  that was gone on with. A pair that is not counted is resumed and asserts nothing: its `msr` ended a
  sample or started one, and a stop at `0x1C` with that `R14_fiq` then comes about in a new phase, which
  the two mutations this test is named for leave alone), `.unclocked` (the guest holds the ARM7 before the
  burst: `Step`, nothing executed, `ENABLED` 0), `.notclocked` (`-32012` for the three kinds), `.sh4first`
  (an SH4 breakpoint ends the wait with its own cause, and no step is left armed), `.breakpointed` (a step
  executes its instruction with a listed breakpoint on it);
  the wait of a step (H6-17, third bullet), where the SH4 runs with the stop in force and "a step is
  executing" set across the return of `Park()`, which no path of the park loop does today:
  `arm7.step.wait.hoststop` and `.wait.exit` (the thread ends, no stop of the step is published,
  `dbg_arm7_armed` is 0 behind it, and the first `Park()` of the next thread resumes nothing: the wait counted
  as a step), `.wait.reset` (one stop, reason `reset`, no step left armed), `.wait.grace` (the two calls of
  the grace expiry, `SetActiveMask(0)` and `Resume()`, queued during the wait, are served behind the stop of
  the step, and the machine then runs);
  `arm7.stepover.bl` (asked from a stop the ARM7 check did not decide, with the ARM7 standing on the `bl`
  and a listed breakpoint on that `bl` — the test puts it there by a PC write at a pause: it ends at the line
  behind the `bl` with cause `Step`, not on the breakpoint), `arm7.stepover.plain`, `arm7.stepout` (the same
  with a breakpoint on the line it is asked from);
  `arm7.reset.suspended`, `arm7.hoststop.suspended`, `arm7.exit.suspended` (the parked cases: the burst is
  complete in front of the stop callback, nothing stops, nothing hangs, the next callback's `verify` holds);
  `arm7.sameslice.reset`, `.hoststop`, `.exit` (the request comes in the tick in which a check suspends, so
  that `dc_run` meets a suspended burst with no `Park()` in between: a scheduler callback of the test,
  registered behind the AICA's so that it runs behind it in its tick, as `dbg_selftest_regs.cpp` does, calls
  `RequestReset()` / `Stop()` / `RequestExit()` when `SoundBurstSuspended()` is not 0. Reset: the burst is
  discarded, the stop published is `reset`, the next callback's `verify` holds. The others: the burst is
  complete in front of the stop callback and nothing is published);
  `arm7.intc.order` (an SH4 interrupt raised by a burst that was run on in the park context is accepted
  before the first SH4 instruction); `arm7.events.park` (logged, not stopping; the same event stops in a
  run); `arm7.reads.suspended` (every accessor twice, equal; frame 0 of the ARM7 callstack is its PC);
  `arm7.setreg.*` (every index class, the mode switch, a refused CPSR, a CPSR that reads back as it was
  stored — the written value under `0xF00000DF`: H6-38 —, a PC write followed by a step, and a
  PC written as `0xFFFFFFFF` that reads back `0xFFFFFFFC` and from which a step does not end the process.
  **Where the test compares the other rows around a CPSR write, the row SPSR is held to H6-38 and not to
  "unchanged", by value and never as "changed"** (the table under H6-38): while the mode the CPSR is written
  from and the mode written are both `0x10` or `0x1F`, SPSR reads the CPSR as it was before the write, also
  behind a write of the value the CPSR has; written from one of the two into a mode that has an SPSR, SPSR
  reads what the row `SPSR_<mode>` of the mode written read in front of the write, and keeps that value
  when the CPSR is written back; from any other mode a CPSR written and written back leaves all 41 rows as
  they read. **The test makes both writes from system mode itself**, so that the two cases run whatever mode
  the guest's loop is in, which 5.2 does not fix: the CPSR set to mode `0x1F`; then written again with mode
  `0x1F` (the value it has will do) — SPSR reads the CPSR as it was before that write; then `SPSR_irq`
  written with a value of the test's, and the CPSR written with mode `0x12` and back to `0x1F` — SPSR reads
  that value both times, `SPSR_irq` reads it still, and every other row reads behind the pair as in front
  of it; then the restore. (Read behind the write, the rows SPSR and `SPSR_irq` are one register, so the
  comparison is with the value the test wrote, not with the row as it reads then.));
  `arm7.notice` (on this host: the notice is never set, armed or not; the flag of item 4 is read, not run);
  `arm7.grace` (the mask cleared while a burst is suspended: the resume runs through).
- Contract: the `arm7` part of S16 through `--only S00,S16 --parts S16:arm7` (mode `parts`, 3.0), on three
  engines against 2ndmix (`debugger.contract.m2arm7`, `.interp`, `.fallback`) and on the JIT and the
  interpreter against the guest (`debugger.contract.m2arm7.guest`, `.interp`; option `GUEST`), the latter
  requiring the notes `arm7: held at the start` and `arm7: bl stepped over and out` (M2-D34; the runner's
  `--require-note`, which the mock's gate has run since F3a); the M1 gate
  still passes on three engines. These targets start S16 at the anchor, with the ARM7 held: they are where
  the settling of 3.1 is used.
- ThreadSanitizer: the session `arm7` and the M1 gate on the interpreter, no report.

**Verifier.** Mutations, each of which a named test or hash has to catch: `ticks_done` not kept across a
suspension (the resumed sample starts again at 0), and the tick test left out once behind the first
instruction of a resumed sample (`hitresume` catches the first and `steps` both: every step is a resumed
sample, and one in some thirty-five stands on the last instruction of its sample); an overshoot carried into
the next sample; burst state kept in an `Arm7Context` slot or in `CYCL_CNT`; an extra `CPUUpdateCPSR`; the tail
skipped when the ARM7 is held, or run twice behind a completion in the park context (the counts of 2.3:
`arm7.burst.plain`, `arm7.step.burstend`); the completion in
`dc_run` made with checks; no discard in `Reset()`; `OnSoundStop` while the guest executes merged straight
into the decided stop, without the freezing cause and `ATTN_PAUSE` (the slice hook is not called behind the
tick, the SH4 runs on with a burst suspended, and the next callback's `verify` fails: `arm7.bp.hit`); the
exemption not ended by its check; the exemption armed with `dbg_arm7_armed` 0 **and** not ended at a change
of the flag (the two rules of H6-5 hold the same case, so the verifier removes both: the first edge of
`arm7.bp.resume`); the exemption armed with the ARM7 held (the second edge, which holds the ARM7 on address
0 for it); the exemption used at a pc that is not its own; the pending FIQ looked at once per sample
(`StepIrq` in front of the debug loop and not in it: every neutrality target, by its line, and
`arm7.step.fiqbehind`, no counted pair of which then ends at `0x1C`); no `StepIrq` in the loop that goes on
with a suspended sample (`steps`, by its line and by its fourth kind, and `arm7.step.fiqbehind`, the same
way. **What the two named tests count is what makes them catchers**: under either mutant a step of that
`msr` still reaches `0x1C`, with the same `R14_fiq`, whenever the `msr` is the last instruction of its
sample, and under the second also when the stop on it is in front of the first instruction of one — a new
phase each time, which neither mutant touches, about once in ten bursts. A test that takes any such step
for an entry inside a sample, as the fourth cut had both, can stay green under both mutants; the verifier
runs that once, with the count taken out of `arm7.step.fiqbehind`, and reports what it saw); a `StepIrq`
that tests the request line and not `INTR_PEND` (it
enters a FIQ inside the F window: every neutrality target); `reg[15] = armNextPC + 8` left out of
`StepExec`, or a `StepIrq` that enters nothing (a `.halves` suite goes red; the plain suites stay green in
the shape that leaves `Step` alone, which the verifier shows once); a step-over or step-out that arms none (it stops on the breakpoint
it stands on); a step that ignores the FIQ rule (ends at `0x20`); `Running` published by a step that waits;
"a step is executing" not ended at the `Park()` behind a wait; the wait not counted as a step for the owed
resume; an `OnSoundStop` that tests the run flag; `dbg_arm7_armed` that ignores the mask; the lowest id not
chosen; `arm7::OnStop()` not called for a stop of the SH4 (a step left armed); a PC stored as given; a CPSR
writer that calls the switch with `saveState` true (the SPSR of the mode it enters is overwritten with the
CPSR, which is what the fifth cut's sentence on RN_SPSR asked for: `arm7.setreg.*`, by its write from system
mode into mode `0x12`, where SPSR no longer reads the value the test put into `SPSR_irq`, and by that row
behind the write back); a call
added per instruction to `Step` (check (a) of the host cost: the disassembly); the fallback flag of the
notice not set at a leave
(read: `arm7.notice` cannot run it here). Each neutrality target fails when its test is made to arm nothing,
and when the debug loop is made to enter one pending FIQ an instruction late with the tick's instruction
count kept (the handler's fold of `lr` and `SPSR` sees it whichever entry it is: 5.2); `steps` also fails
when only the loop that goes on with a suspended sample does. (The third cut asked each target to fail
when "a resumed sample" moves a FIQ entry. Two of the three cannot show that: `idlebp` has no resumed
sample, and in `hitresume` the rest of a resumed sample holds no FIQ entry — a request is raised by a time
step, between two samples, or it waits behind the F window, which a breakpoint on the top of the loop or in
the handler is never inside.) Three mutations of the third cut are **not** in the list in the form they
had, because they are equivalent. `OnSoundStop` in the park context recorded as a freezing cause: the park
loop starts every turn with the fold of the requests, which merges a freezing cause into the decided stop
and clears `ATTN_PAUSE`, so the stop is published identically. The exemption armed with `dbg_arm7_armed` 0,
alone: no check is made while the flag is 0, and the exemption ends when the flag changes. The exemption
armed with the ARM7 held, against the edge as it was: the release puts the PC on 0, and an exemption for
the address the ARM7 was held on is never used there. Two mutations of the first
cut are **not** in the list, because nothing of M2 can see them. A tick test added in front of the first
instruction of a resumed sample is an equivalent mutant: a suspension is always in front of an instruction
whose predecessor passed the test, so `*done < ticks` at every suspension. And `arm_on` read per sample
instead of once: inside a burst the reset state of the ARM7 cannot change in M2 — `SetResetState` is reached
only from the SH4's MMIO write, the SH4 executes nothing while a burst is suspended, the ARM7's own write of
`0x2C00` lands in the register file, and the debugger has no writer of ARMRST (H6-39). The latch is held by
reading; it becomes observable with the AICA writer of M3, whose cut gives it its test. Stress: 10,000 stops
with a resume, and
10,000 steps, on the guest with the line still equal; 1,000 exit runs with a host stop at a random moment while
ARM7 breakpoints are being hit (no hang, the exit line printed); 1,000 resets while suspended; ARM7
breakpoints interleaved with pauses, SH4 steps and SH4 breakpoints; the grace expiry on a suspended burst; all
of it once under load, and once with `aica.NoBatch=yes`. arm64 is read, not run: the debug loop leaves
`CYCL_CNT` alone, and nothing keeps an `ARM7Backend*` across a thread start.

### 3.5 E7a — writes and reset

**Delivers.**

1. `dbg::mem::Write` (H6-35): the check of the whole range, then the store per region.
2. `dbg::regs::Write` (H6-33) and `kRegWritable` on the 69 rows of `dc.sh4.cpu` and the 40 rows of
   `dc.aica.arm7` that are not `ENABLED`. No other row gets the flag. Three rules of value go with it: an odd
   PC of the SH4 is refused (M2-D42); an FPSCR write that changes PR, SZ or the rounding mode empties the
   code cache of the SH4 (M2-D41); and so does an accepted write of the SH4's PC that differs from the PC it
   replaces (M2-D42). Both are `SuperH4::ResetCache()`, a call that exists, and nothing of the block manager
   is asked.
3. The core: `state.writeMemory`, `state.setRegister`, `state.editWatch`, `control.reset` (a `CC_WRITE` twin of
   `TableCall`; the rules of M2-D20, M2-D21, M2-D23); the capabilities `memory-write`, `register-write`,
   `reset`, each in the commit that has its method and its writer; `flags: {"writable": true}` on the
   registers of `debugger.describe` that have `kRegWritable`, and on no register of a tick (M2-D19).
4. The two comment clauses of 2.5.
5. The selftest session `writes` (`dbg_selftest_writes.cpp`), on the guest of E6a, with its second
   registration `writes.smc`; in the same file the test `writesrun.neutral`, which the run ends and which is
   therefore no test of the session (3.0); in the session `core` the
   new tests `core.writes` and `core.reset` and the changed expectations; the changed expectations of `regs`.

**Owns.** `libswirl/debugger/dbg_mem.cpp`, `dbg_regs.cpp`, `dbg_regs_tables.cpp` (the flags of those two
nodes), `dbg_regs_private.h`, `dbg_core.cpp`, `dbg_core_private.h`, `dbg_selftest_core.cpp`,
`dbg_selftest_regs.cpp`, the new `dbg_selftest_writes.cpp`; `tests/debugger/regs/regs_test.cpp` (what it says
about writable rows); its lines of `tests/debugger/selftest/CMakeLists.txt`; `tests/debugger/contract/`
(`CMakeLists.txt`, `contract.py`: its targets, on the option and the mode E6b added); its rows and counts in
`tests/README.md`, `tests/debugger/selftest/README.md` and `tests/debugger/contract/README.md`; the two clauses
of `dbg_regs.h` and `dbg_mem.h`.

**Must not touch.** `hw/` (it calls what exists: `bm_LockedWrite`, `DebugDiscardPage`,
`VLockedMemory::UnLockRegion`, `VramLockedWrite`, `InvalidateJitCache`, `sh4_sr_SetFull`, `UpdateSR`,
`UpdateINTC`, `UpdateFPSCR`, `SuperH4::ResetCache`, `SoundCPU::DebugSetReg`); `libswirl.cpp`; `dbg_emu.cpp`,
`dbg_arm7.cpp`, `dbg_sh4.cpp`; the frozen headers but for the two clauses; the UI repository. **The runner's
write rules meet their first emulator in this gate.** They have run against the mock, which has the effects
they look at (3.1 item 2), but a rule can still be wrong about the emulator: a defect of a rule is reported,
with the frames, and the integrator has it fixed in `dreamcast-devtools`; E7a does not bend the emulator to a
rule that the header texts of section 2 contradict.

**Depends on.** E6b merged (the ARM7 writer's body, `arm7::OnPcWritten`).

**Gate.**

- Builds and all ctest targets as for E6b (five builds; green twice, none skipped, the number stated). The
  sound lines and the state hashes unchanged.
- The session `writes`, **on the guest of E6a** (its tests need known code: `g_main_loop` for code writes
  and the breakpoint in the middle of a block, `g_irq` with the AICA's interrupt for `writes.reg.sr`,
  `a_main_loop` and `a_leaf` for `writes.reg.arm7`; labels from `dsptest_labels.inc`), on the JIT, the
  interpreter and the forced fallback, registered by the two lines of 3.0 (`writes`, `writes.fallback`; and
  `writes.smc` for its one test, below):
  `writes.mem.ram` (the three mirrors, each written and read through the two others; a range across the end
  of main memory refused whole, its first half unchanged), `writes.mem.code` (an instruction written at the
  stop PC and ahead of it executes at the next step; the same under an owed remainder, behind a breakpoint
  stop in the middle of a block, followed by a resume: the owed rest of the block is decoded from memory as
  it is at the leave. This test holds that half and **shows nothing about the block cache**: below),
  `writes.mem.cached` (the witness of the discard: below), `writes.mem.bppage` (a write
  into a page with an armed breakpoint: the breakpoint still stops), `writes.mem.vram` (both views show the
  bytes, written through either), `writes.mem.aram` (both targets show the byte), `writes.mem.refused` (boot
  ROM, flash, each register window, P4, the store queues, an unreadable range, a range past 2^32, the ARM7
  above its wave memory, the DSP);
  `writes.reg.sh4` (every row: written, read back **under the stored mask of the row** — SR under
  `0x700083F3` and with RB as 0 when MD is written clear, FPSCR under `FPSCR_MASK`: H6-33 —, restored),
  `writes.reg.pc` (**the owed remainder goes: at a breakpoint stop in the middle of a block the PC is written
  with the value it has, and `dbg_sh4_TakeRemainder(pc, ...)` then answers false**, as the session `sh4bp`
  asks behind a step. That is the one case that needs `OnPcWritten`: for any other value the remainder goes
  without the call — `PrepareResume()` drops a remainder whose PC is not the PC of the leave — and holds
  nothing about it. Further: a PC
  written onto the address of a breakpoint other than the stop's own stops there at the resume with nothing
  executed; a PC written back onto the stop's own address resumes through it — `dbg::sh4::OnPcWritten`
  leaves the key of the stop, and E7a must not touch `dbg_sh4.cpp`; an odd PC: `-32013`, nothing changed;
  **the mirror: at a stop that owes no remainder, with the PC on `g_main_loop`, the PC is written into the
  `0x0C…` mirror of the same address, and behind a resume the next such stop in the loop has its PC in the
  `0x0C…` mirror** — the loop branches back relative to itself, a property of the guest (5.2: `g_main_loop`
  goes back by `bra` and takes the PC from no register, and `g_irq` returns by `rte` to where it was entered
  from), and without the cache reset of H6-33 the
  dispatcher enters the block that was compiled at `0x8C…`, whose branch goes there. **The mirror has to be
  one that shares its slots with `0x8C…`: `0x0C…` or `0xAC…`, a multiple of 32 MiB away.** `0x8D…` is the
  same memory 16 MiB up, in other slots of the block cache (the mask is 32 MiB and main memory 16 MiB): a
  PC written there finds no block, and the case passes with no reset. The witness is the
  JIT's: the interpreter and the forced fallback decode at the PC and pass with or without the reset. The
  test writes the PC back at its end; a PC written with the value it has resets nothing),
  `writes.reg.sr` (the banks swap with `RB`; an interrupt the new mask lets in is accepted on the way out,
  not in the park context), `writes.reg.fpscr` (the FP banks swap; the mask; and the FP mode of compiled
  code: the test writes FP instructions into `g_main_loop` and runs them once **with PR 0 and SZ 0, the
  guest's FPSCR**, so that their block is compiled, then writes PR to 1 and, with PR 0 again, SZ to 1: the
  instructions execute in the written precision and size by a step, **and by a resume from a stop that owes
  no remainder, the loop's block having been entered from the cache since the write** (below: the direction
  of the write decides whether the test can see anything), on the three engines), `writes.reg.arm7` (every row,
  read back under its stored mask: the CPSR under `0xF00000DF`, the PC aligned; a
  PC write followed by an ARM7 step; a PC written as `0xFFFFFFFF` reads back `0xFFFFFFFC`; a PC written onto
  another ARM7 breakpoint stops there at the resume with nothing executed, and written back onto the stop's
  own address it resumes through: the rule of the SH4, H6-5; a CPSR with a mode that is none: `-32013`,
  nothing changed; `ENABLED`: `-32013`. **Behind an accepted CPSR write the test asserts "the other rows
  are unchanged" only with the exception of H6-38, and asserts the row SPSR by value, never as "changed"**
  (the table under H6-38): written in user or system mode with one of those two modes, the row SPSR reads
  the CPSR as it was before the write, also behind a write of the value the CPSR has, and behind a restore
  that stays in the two modes it reads the value written in between; written from one of the two into a
  mode that has an SPSR, it reads what the row `SPSR_<mode>` of the mode written read in front of the
  write, and keeps that value behind the restore; written in a mode that has an SPSR and restored, every
  row reads as before — the core's switch, which
  the guest's own `MSR` makes too; which mode the loop of the guest runs in is not fixed (5.2: system, or a
  mode with an SPSR), and E7a may
  touch neither the guest nor the writer's body), `writes.reg.readonly` (every row without the flag: `-32013`, and
  the save states of the devices are what they were), `writes.reg.width` (bits above the width ignored).
  No test named `writes.<x>` is one that the run ends: the filter `writes` selects it (3.0).
- **The witness of the block cache** (`writes.mem.cached`, the cache half of `writes.reg.fpscr`, the mirror
  case of `writes.reg.pc`). Three
  facts of the tree decide how such a test has to be written; a test written to the words "followed by a
  resume" would pass with no discard and with no cache reset.
  - *A resume behind a breakpoint stop proves as little as a step.* The check that stops leaves the rest of
    its block owed, also when it stands on the block's first instruction, and the recompiler runs that rest
    as a temporary block: decoded from memory, with the FPSCR of the moment, outside the block cache (the
    forced fallback decodes every block anew). So a witness is taken only from code that the dispatcher
    enters **from the cache**. The stop in front of the witnessing resume owes no remainder — an event
    breakpoint or a pause, where the PC stands at a block start (the neutrality tests of E6b take such stops
    with `dc.aica.timer`) —, or the written instruction lies in front of the breakpoint's own instruction in
    its block and the value is read at the next arrival there. **From the write to the witness no SH4
    breakpoint of that page is added, removed or toggled**: arming discards the blocks of the page by
    itself. A step, and a resume behind a breakpoint stop, hold the other half: that the owed rest of a
    block is decoded from memory as it is at the leave.
  - *In the default configuration nothing can see a store without the discard.* On a page that is not yet a
    data page the blocks have write-protected it: the debugger's store faults on the emulator thread, the
    fault handler calls `bm_LockedWrite`, the store is repeated, and the end state is that of the writer
    that discards. On a page that is a data page nothing is protected, but `Dynarec.SmcCheckLevel` is
    `FullCheck` (0) by default, and every block of such a page compares its code bytes at entry and discards
    itself on a mismatch. Stale code can execute only from a data page whose blocks have no check:
    `Dynarec.SmcCheckLevel=-1` (`NoCheck`; `types.h` — not 0, which is the default).
  - *Only a block compiled with PR = 0 has an FP mode in it.* With PR set the decoder emits every FP
    instruction as a call of the interpreter's handler (`dec_generic` of `hw/sh4/dyna/decoder.cpp` gives it
    up), and the handler reads FPSCR when it runs. A block compiled with PR clear has single precision
    in its code, and the size of an `fmov` as SZ was. So the witnessing block of `writes.reg.fpscr` is
    compiled with PR = 0 and SZ = 0 — the guest's FPSCR: reios leaves `0x00040001` and the guest does not
    write it (5.2) —, and the witness is taken after PR was written to 1; then, with PR written back to 0
    and the block compiled again by a pass of the loop, after SZ was written to 1. The other direction (a
    block compiled under PR = 1 and PR written to 0; an SZ flip made while PR is 1) passes without the cache
    reset. The test checks PR == 0 and SZ == 0 before each of the two compiles and says so in an `ok` line.
    The rounding mode has no witness on this host: the one operation the decoder selects by it (`float
    FPUL,FRn`) is the same host instruction either way, which follows the host's mode. That the reset is
    also made for a changed rounding mode is held by reading.
  So the discard has a registration of its own, **`debugger.selftest.writes.smc`** (the line of 3.0: the JIT,
  `config:Dynarec.SmcCheckLevel=-1`, the one test `writes.mem.cached`; the run prints "Warning: SMC check
  mode is -1", which the report quotes). The test: at a stop that owes no remainder, a first write into the
  page of `g_main_loop` (it makes the page a data page); a resume through at least one pass of the loop, so
  that its block is compiled again, without protection and without a check; a stop that owes no remainder;
  the write of an instruction of the loop; a resume; such a stop again; and the value the instruction
  leaves is read. In the session `writes` the same test runs in the default configuration, where it passes
  with or without the discard — the report states that. `writes.reg.fpscr` needs no key: no block checks its
  FP mode at entry in any configuration. On the forced fallback the cache only runs while no SH4 breakpoint
  is listed and active (`sh4::FallbackWanted`), so the witness there is a value the guest computed, read at a
  pause.
- **Neutrality of the writes: `writesrun.neutral`** (a memory write and its restore, a write into a code page
  and its restore, a register write and its restore — a general register, and SR; **not the PC**: a PC write
  drops the owed remainder, which is no guest state but moves a block boundary —, an FPSCR write that
  empties the cache and its restore, R0 to R12 of the ARM7 and their restore: the state hash at the end of
  the run is that of a run without them). **Every write of the test is made at a stop that was latched at a
  slice end** (an event breakpoint on `dc.aica.timer`, a pause): there the interrupt acceptance is owed
  already and an SR write moves nothing. At a code breakpoint of the SH4 a write of SR, also of the value it
  has, moves the acceptance of an interrupt that is pending from the next slice end to the leave, as the
  guest's own `LDC` would (H6-33); that is no defect, and it is not in this test. **The CPSR of the ARM7 is
  not written by it either**: its write goes through the mode switch, which stores the live R13, R14 and
  SPSR into the slots of the mode it leaves and, leaving user or system mode, the CPSR into the live SPSR
  (H6-38); every row reads as before, but SPSR while the ARM7 is in user or system mode, and the save state
  of the sound CPU, which is the `arm7` part of the hash, holds the slots and the live SPSR. It
  is no test of the session `writes`, which ends by itself, and it is not named under its prefix (3.0): the
  run ends it. Its targets are `debugger.selftest.writesrun.neutral` and `.interp`, from
  `nulldc_debugger_selftest(writesrun.neutral MODE same-hash ARGS --guest <dsptest.elf> --expect ...)`, with
  `--expect` texts that say how many of each it made.
- **The other builds of the main-memory writer** (H6-35 has four combinations, of which three run on this
  machine): `writes.mem.*` and `writesrun.neutral` also pass on the emulator of the `-DTARGET_NO_REC=ON` build
  (no recompiler, the layout in use: "a build without a recompiler stores the bytes") and on that of the
  generic-host build (no recompiler, `virt_ram_base` null), each given to a configure of the tests project as
  `NULLDC_EMULATOR`. The fourth, a recompiler without the layout, has no configuration: the verifier makes
  it, and whether it can run a guest at all is open until then (Verifier, below).
- The session `core`: `core.writes` — the four methods by parameter, error order, channel and lifetime
  (`-32002`, `-32001`; **served while running, with a tick behind, never `-32003`**); the tick behind a write
  is paused with the `stopId` it had; five capabilities besides the engine's; `flags.writable` exactly on the
  rows with `kRegWritable`, and on no register of a tick; `core.reset` — a reset while parked is one stop
  with reason `reset` and no `Running`, **and behind it the PC of the SH4 is the one a machine reset leaves,
  not the PC of the stop** (reason, `stopId`, `ENABLED` 0, an empty ARM7 callstack and the kept tables, which
  is all the runner asks, also hold for a reset that leaves the SH4 as it was); **asked at a breakpoint
  stop, the stop of the reset names no breakpoint, and the breakpoint that had stopped, a disabled one, a
  watch and a muted category are listed behind the reset as they were in front of it** (the runner resets
  at a pause stop only); a reset while running
  publishes nothing **and resets the machine** (a breakpoint on the entry of the program is hit behind it).
- Contract: `--only S00,S16 --parts S16:arm7+writeMemory+setRegister+reset` on three engines against 2ndmix
  (`debugger.contract.m2writes`, `.interp`, `.fallback`), **and on the JIT and the interpreter against the
  guest** (`debugger.contract.m2writes.guest`, `.interp`; the option `GUEST` of E6b), the same four parts,
  requiring the two notes of M2-D34. What is particular to the guest is on the write side — the scratch
  address below a stack at the top of main memory and its two mirrors; the 64 KiB written back over the
  guest's own code at `0x8C010000`; the `SR.RB` flip over the banked counters of `g_irq`; the probe's second
  attempt from `g_irq`; the reset of a program that reios loads again —, and it is met here, in the gate of
  the package that owns the writers, not in E7b's, which may touch none of them. The M1 gate on three
  engines. The 65536-byte write
  of the `writeMemory` part (3.1 item 3: main memory at the SH4's `defaultMemoryAddress`, written back as
  read) discards the compiled blocks of the sixteen pages it covers and marks them as data: host cost for
  the rest of that run, and no guest state.

**Verifier.** Mutations, each with the test that has to catch it: a main-memory store without the discard
(stale code executes: `writes.mem.cached` in `debugger.selftest.writes.smc`; in the default configuration
nothing can see it, which the report states; in the scratch build without the layout it ends the process); a
store before the range check; a mirror
of main memory refused, or stored at another mirror's place (`writes.mem.ram`); video memory written in one
view only, or the 32-bit view written through the 64-bit mapping (`writes.mem.vram`); a PC write without
`OnPcWritten` (`writes.reg.pc`, by its assertion on the remainder behind a write of the value the PC has: on
the JIT, where a remainder is owed); an odd PC accepted (`writes.reg.pc`); a write that moves the PC of the
SH4 and leaves the code cache (`writes.reg.pc`, its mirror case, on the JIT; the verifier also shows the
trap the gate names: with `0x8D…` in place of `0x0C…` the case no longer catches the mutant); an ARM7 PC stored as given
(`writes.reg.arm7`); an SR
write that accepts an interrupt in the park context, or skips `UpdateSR`; FPSCR without the mask; an FPSCR
write that leaves the code cache (`writes.reg.fpscr` on the JIT, by its resume from a stop that owes no
remainder; the mutation is applied to the writes that set PR and SZ, and a second mutant resets for a
changed PR only, which the SZ half catches. The verifier also shows the two traps the gate names: the test
rewritten to witness behind a breakpoint stop no longer catches the mutant, and neither does the test in
the other direction, a block compiled under PR = 1 and PR written to 0); a tick with a new `stopId`,
or none; a write to a running target answered `-32003` (`core.writes`); a capability advertised over a
method that answers `-32004`; a value parser that takes a sign or truncates a 70-bit text wrongly; the flag
on `ENABLED`, or in a tick; the error order of M2-D20 swapped; a reset that publishes `Running`; a reset
while running that logs the event and resets nothing (`core.reset`); a write served in `Stopped`. A scratch
build in which `_vmem_reserve` takes its fallback (the recompiler without the virtual memory layout, the one
combination of H6-35 that no configuration gives): `writes.mem.*` on the JIT there. That build protects the
pages of compiled blocks as every build does and cannot resolve a fault in them (`bm_LockedWrite` measures
the faulting address from a null base, so the fault handler gives up): a guest that stores into a page from
which a block was compiled ends the process there, with or without a debugger — 2ndmix certainly. The guest
of E6a stores into no page of its code (5.2), so it can run there. If the build cannot bring the guest to
its main loop all the same, for a reason outside `dbg_mem.cpp`, the verifier says so with the faulting
address, the branch of the writer is read instead of run, and the finding goes to PLAN's backlog (the
recompiler without the layout is then no working combination). Stress: 10,000 writes
with their restore and the state hash equal — on the guest of E6a, at event-breakpoint stops (latched at a
slice end), of main memory, of a code page, of R8, SR and FPSCR of the SH4 and of R0 to R12 of the ARM7. Not
the PC of either processor (the SH4's drops the owed remainder, which is no guest state but moves a block
boundary); not SR at a code breakpoint of the SH4 (it can move the acceptance
of a pending interrupt to the leave, which is the writer doing what an `LDC` does); and not the CPSR of the
ARM7 (the mode switch rewrites bank slots that the save state of the sound CPU holds and no register row
shows, and in user or system mode the live SPSR, which the row SPSR does show: H6-38). A stress that includes
one of the three would report a writer that is correct. 100,000 writes of `MACH`, which the guest of E6a never reads
(5.2), into the running guest without one `-32000` and with the guest alive at the end — not of a register
the guest uses: a PC, PR or R15 written into a running guest crashes it, which is M2-D42 and no finding;
writes racing a host stop (each answered, with `{}` or `-32001`); ThreadSanitizer on the session and on the
contract parts.

### 3.6 E7b — the M2 rows of the device tree

**Delivers.**

1. The nodes and rows of 3.6.1 in `dbg_regs_tables.cpp`, with the sources they need in `dbg_regs_private.h`
   and the readers in `dbg_regs.cpp`. Every row is read-only. No new accessor in an `hw/` header (M2-D30).
2. The actions `sh4-registers` on `dc.sh4.cpu` and `arm7-registers` on `dc.aica.arm7`.
3. In `hw/holly/sb.cpp`, as its first commit: a read-only slot that the emulator never assigns reads 0 instead
   of the low half of a host pointer (M2-D29) — after one run has shown that it does read that way.
4. The measurement of M2-D27: the bytes of a full tick with empty tables, and the time to build one on the
   emulator thread; the `reserve` sizes of `BuildTick` and `BuildDescribe` follow.
5. The pure test and the selftest `regs` for all of it; the contract targets of the whole M2 gate.

**Owns.** `libswirl/debugger/dbg_regs_tables.cpp`, `dbg_regs.cpp`, `dbg_regs_private.h`,
`dbg_selftest_regs.cpp`; the two `reserve` statements of `dbg_core.cpp`; `dbg_selftest_core.cpp`, where an
expectation follows the tree (node and register counts, the watch bound of `core.tables`: 3.0);
`tests/debugger/regs/**`;
`hw/holly/sb.cpp` for item 3; `hw/gdrom/`, `hw/maple/`, `hw/pvr/` are its as in 6.3, and no change is expected
there; `tests/debugger/contract/` (`CMakeLists.txt`, `contract.py`: the mode `m2` for the whole gate and its
second run, and its targets); its rows and counts in `tests/README.md`, `tests/debugger/selftest/README.md`
and `tests/debugger/contract/README.md`.

**Must not touch.** `hw/aica/`, `hw/arm7/`, `libswirl.cpp`, `dbg_emu.cpp`, `dbg_arm7.cpp`, `dbg_mem.cpp`, the
frozen headers; the methods of `dbg_core.cpp`, and in `dbg_selftest_core.cpp` everything but the
expectations named above; the UI repository — a defect of the runner's S08 that its gate meets is reported.

**Depends on.** E7a merged; the pinned UI checkout (section 4), which is F3a's merge or later.

**Gate.**

- Builds and all ctest targets as for E6b (five builds; green twice, none skipped, the number stated); the
  sound lines unchanged; the state hashes of 5.7 unchanged, also behind item 3.
- `debugger.regs.*`: the node list (104 nodes) with parents, labels and actions; names upper case, of
  `[A-Z0-9_]`, unique in their node; every system bus row a name and address of `sb_regs.h`, every PVR row a
  name and offset of `pvr_regs.h` (both headers read as text); the fields of one AICA word do not overlap and
  lie inside it; no row of these nodes has `kRegWritable`; `debugger.regs.panels` with 37 ids.
- The selftest `regs`: every row reads the same twice on a standing machine; reading every row a hundred
  times leaves the save state of every device, and the PVR register file, as it was; a row on a write-only
  slot reads `"n/a"`, on the system bus and in the AICA's common block (`SCILV0-2`, whatever the register
  file holds); `L` and `M` of `dc.aica.common` are what `SoundCPU::DebugGetInterrupt` says; the rows of `dc.gdrom`
  are the snapshot; a full tick with empty tables is at most 192 KiB, and its build time is printed.
- The session `core` passes **with the bound of 256 watches reached**: the tree now has more than 256
  registers, so `core.tables` takes the branch it has never taken (the call that would make more than 256
  watches is refused whole; design 5.7 says the bound cannot be reached on the M1 tree), and the runner's S08
  makes its bound probe against the emulator for the first time.
- `debugger.contract.hash.listening` (new, JIT and interpreter): a client that connects, describes and then
  only receives, for the whole run, leaves the state hash what it is with the server off — the periodic full
  tick now reads every device.
- Contract: **the M2 gate**, `--milestone M2`, on the JIT, the interpreter and the forced fallback against
  2ndmix (`debugger.contract.m2`, `.interp`, `.fallback`), and its second run against the guest
  (`debugger.contract.m2.guest`, `.interp`; M2-D34), which requires the notes `arm7: held at the start` and
  `arm7: bl stepped over and out` (the runner's `--require-note`, passed through by the mode `m2`). What
  the second run adds over E7a's guest targets (3.5) is the `tree` part: the four other parts have passed
  against the guest in the gates of the packages that own their code (E6b, E7a).
  `debugger.contract.slow` still passes with the larger ticks.

**Verifier.** Mutations: a field with the wrong shift or width; a row at its neighbour's address; a reader
that goes through the guest's read function (`0x2810` then clears a loop flag: the save-state comparison
catches it); a channel index off by one; a node missing or under the wrong parent; an action on the wrong
node; a writable flag on an M2 row; a `SCILV` row that reads the accessor's 0 instead of `"n/a"`; `L` and `M`
read from the register file instead of the sound CPU. Reads against the guest's own view for registers whose guest read has no
side effect (system bus data slots, the PVR file, channel words). One hour of a running target with a
listening client and the periodic full tick, the hash at the end equal to a run without it; the tick build
under ThreadSanitizer.

#### 3.6.1 The M2 tree

Normative for a server that claims M2 (M2-D24) are the paths, the parents, the two new actions, and that a node
marked **R** carries at least one register. The register lists are the emulator's.

| Path | Parent | **R** | Registers of the emulator | Rows |
| --- | --- | --- | --- | --- |
| `dc.sh4.cpu` | (M1) | (M1) | as M1; action `sh4-registers` added | 69 |
| `dc.aica.arm7` | (M1) | (M1) | as M1; action `arm7-registers` added | 41 |
| `dc.holly.dmac` | (M1) | R | ch2: `SB_C2DSTAT SB_C2DLEN SB_C2DST`; sort: `SB_SDSTAW SB_SDBAAW SB_SDWLT SB_SDLAS SB_SDST SB_SDDIV`; G1: `SB_GDSTAR SB_GDLEN SB_GDDIR SB_GDEN SB_GDST SB_GDSTARD SB_GDLEND`; G2, for each of `AD E1 E2 DD`: `STAG STAR LEN DIR TSEL EN ST SUSP STAGD STARD LEND` | 60 |
| `dc.holly.sb` | `dc.holly` | — | none | 0 |
| `dc.holly.sb.g1` | `dc.holly.sb` | R | `SB_G1RRC SB_G1RWC SB_G1FRC SB_G1FWC SB_G1CRC SB_G1CWC SB_G1GDRC SB_G1GDWC SB_G1SYSM SB_G1CRDYC SB_GDAPRO` (ten of them write-only: `"n/a"`) | 11 |
| `dc.holly.sb.g2` | `dc.holly.sb` | R | `SB_G2ID SB_G2DSTO SB_G2TRTO SB_G2MDMTO SB_G2MDMW SB_G2APRO` | 6 |
| `dc.holly.sb.pvr` | `dc.holly.sb` | R | the block at `0x005F7C00`: `SB_PDSTAP SB_PDSTAR SB_PDLEN SB_PDDIR SB_PDTSEL SB_PDEN SB_PDST SB_PDAPRO SB_PDSTAPD SB_PDSTARD SB_PDLEND` | 11 |
| `dc.holly.sb.misc` | `dc.holly.sb` | R | `SB_DBREQM SB_BAVLWC SB_C2DPRYC SB_C2DMAXL SB_TFREM SB_LMMODE0 SB_LMMODE1 SB_FFST SB_SFRES SB_SBREV SB_RBSPLT SB_PDTNRM SB_PDTEXT SB_G2DTNRM SB_G2DTEXT` | 15 |
| `dc.holly.ta` | (M1) | R | the 14 registers of `pvr_regs.h` named `TA_*` | 14 |
| `dc.holly.core` | (M1) | R | the 45 registers of `pvr_regs.h` that are neither `TA_*` nor `SPG_*` | 45 |
| `dc.holly.spg` | `dc.holly` | R | the 9 registers named `SPG_*` | 9 |
| `dc.aica.common` | `dc.aica` | R | the fields of the 23 words of the common block up to `0x2C00`, as `AICA::DebugReadRegs` gives them (`devices-m2.md` Appendix C), under the naming rule below. Five of them read `"n/a"` (`kRegNoReader`; M2-D28): `SCILV0`, `SCILV1`, `SCILV2`, which are write-only on the console and which the accessor gives as 0 whatever is stored, and `SCIRE`, `MCIRE`, which store nothing. The words at `0x2D00` and `0x2D04` of the file are **not** rows: they are not the interrupt controller in front of the ARM7. That controller's state is two rows read through `SoundCPU::DebugGetInterrupt`, an accessor that exists: `L` (the level the ARM7 reads at its `0x00802D00`) and `M` (the level in service) | about 50 |
| `dc.aica.rtc` | `dc.aica` | R | `SECONDS` (32), `WRITE_ENABLE` (1) | 2 |
| `dc.aica.sgc.0` .. `dc.aica.sgc.63` | `dc.aica.sgc` | R | the fields of the 18 words of the channel (`devices-m2.md` Appendix D), under the naming rule | 38 each |
| `dc.gdrom` | (M1) | R | the members of `GDRomDebugState`, the packet as `PACKET_0_3`, `PACKET_4_7`, `PACKET_8_11` (32 bits each) | 27 |
| `dc.maple` | (M1) | R | the twelve `SB_M*` registers | 12 |

The 115 system bus registers that M1 left out are in exactly one node each (60 + 11 + 6 + 11 + 15 + 12). 70
nodes are new (`dc.holly.sb` and its four, `dc.holly.spg`, `dc.aica.common`, `dc.aica.rtc`,
`dc.aica.sgc.2` to `.63`); the tree has 104.

The naming rule for rows that have no `_addr` name in the code: the name of the data sheet, which is the
member name of the code's struct in upper case, without `$` and without a `_r` suffix (`$T` is `T`, `Mono` is
`MONO`); a quantity that the register file splits over two words is one row (`SA` from `SA_hi` and `SA_low`,
`DMEA` likewise); members of a snapshot are upper case with underscores; no row is wider than 64 bits.

### 3.7 F3b — the end of protocol v2

**Delivers.** `devtools-m2.md` 1.6, every site of its table but one: the literals for absent `targets`, every
capability assumed, the running-to-paused stop rule, the kept callstack, headerless binary
frames, the "no capture" object, the event-log row without `metadata`, the v2 fixture and tests. Kept, under
names that say what they are for: the frame of a page that has had no server yet, and the label of a target a
v3 server does not list. **`mergeCategories` (`src/state/tickState.ts`) stays as it is**: "a tick without
`categories` keeps the stored states" is the per-key rule of a v3 tick (a key the tick does not carry keeps
its state), which the table below also says; only a v2 comment there goes. A `debugger.describe` result
without `protocol` gets the banner and is treated as a
server that lists no target and no capability (M2-D39). One e2e test against a scripted server without
`protocol` pins the banner — and it is the only e2e server without `protocol` that is left: **the synthetic
server of `e2e/support/syntheticDebugger.ts` becomes a v3 server** (`protocol: 3`, `ui`, `capabilities` and
`targets` in its describe; `stopId`, `reason` and `categories` in its ticks). Today it has none of them, no
table of v2 sites lists it (the search was in `src/`), and after this package the client would give it the
banner and treat it as listing no target and no capability: `e2e/device-tree-perf.spec.ts`, and F3c's block
on 3,000 registers, would measure a server the client no longer supports.

**What stands in place of the v2 branches.** An absent field has no v2 meaning any more, and nothing is
inferred from it:

| Absent | The client then |
| --- | --- |
| `targets` or `capabilities` in a describe (with or without `protocol`) | an empty list: every panel shows its notice, nothing is requested |
| `stopId` in a tick | no new stop: the views are not moved (no stop is made from "running, then paused") |
| `callstacks` in a tick | no frames (none are kept from the stop before) |
| `categories` in a tick | the stored states stay |
| `watches` in a tick | none |
| the header of a binary frame | the frame is not taken, and the request ends in its "the frame did not arrive" error |
| `metadata` of an event-log entry | a row without an event name |

**The schemas.** F3b changes one client schema: the result of `state.getSgcFrameData` loses the in-band object
of the v2 mock (`V2InBandErrorSchema`). `always()` stays, the v3 fields remain optional for the client, and
`ConformanceSchemas` is unchanged in every shape: `scripts/contract/` and its tests pass unedited. F3b changes
no panel id, nothing under `scripts/contract/` and nothing under `server/`. (The runner validates with the
strict copy and reads a describe leniently with the client copy; a client copy that made the v3 fields
required would change what the runner reports for a describe without `targets`, in a test F3a owns.)

**Owns.** `src/state/targets.ts`, `tickState.ts`, `debuggerSelectors.ts`; `src/services/debuggerClient.ts`;
the v2 parts and comments of `src/lib/debuggerSchema.ts` and `binaryFrame.ts`, within the limit above; the
banner text in `AppLayout.tsx`; `EventLogPanel.tsx`; their tests;
`src/lib/test-fixtures/protocol-v2-frames.json`; `e2e/v2-server.spec.ts`, `e2e/support/scriptedDebugger.ts`,
`e2e/support/syntheticDebugger.ts` (its describe and its ticks), `e2e/callstack-eventlog.spec.ts`; the README
lines.

**Depends on.** F3a. **Gate.** `npm run check`; the e2e suite; and, because the emulator's contract gate
executes this package's source (the runner imports `src/lib/debuggerSchema`, `src/lib/binaryFrame` and two
files of `src/services/`), the emulator's `debugger.contract.*` targets once, against the emulator of the
integration branch as it then is, with the UI checkout at F3b's tip (section 4). **Verifier.** One question:
is every behaviour 4.4 lists gone, and nothing else. A scripted server for each row of the table above, and
one without `protocol`: each gets what the table says and no panel falls back to a literal. The two cases
that are kept still work (no server yet; a target that is not listed), and so does the per-key rule of
`mergeCategories`, which is no v2 behaviour. `e2e/device-tree-perf.spec.ts` passes against the synthetic
server with no banner on the page. `git diff` of `scripts/contract/` and `server/` is empty.

### 3.8 F3c — registers and memory

**Delivers.** The generic register panel bound to the twelve ids, finding its node by the `actions` of the
describe tree; rows with name, value and width, values following ticks, `"n/a"`, the stale dimming of
`dc.sh4.cpu`, a watch button per row; an editor for a register that carries `flags.writable`, while the
target is paused and the server has `register-write`. Store actions for `state.setRegister` and
`state.writeMemory`. Byte editing in the memory view (hex column; a run of bytes typed in one go is one
request), while paused and with `memory-write`. The narrow rule of the memory view (4.4; the disassembly's
mechanism). And, each decided here so that the engineer does not have to ask (M2-D39):

- **Where the two new panels stand.** `sh4-registers` is a fourth panel of the centre group of the workspace
  `sh4-debugger`, and `arm7-registers` of `arm7-debugger`, behind the breakpoints panel and not the active
  one; the other two workspaces are unchanged. `e2e/docking.spec.ts`, which pins every default panel and
  "nothing else is open", follows.
- **A register panel whose id no node offers** (`arm7-registers` on an M1 server) shows a notice, "This
  server has no <title>", and requests nothing. **If several nodes offer its id, the panel shows the first
  in the order of the describe tree** (depth first, as the tree lists them). An M2 server offers each id on
  one node: the `tree` part of S16 holds that (3.1 item 3).
- **The watch editor is offered while paused only**, like the other two editors (today it opens while the
  target runs), and only for a watch whose register carries the flag.
- **Where the flag is read.** Ticks carry no `flags` (M2-D19, invariant I4), and the store replaces the list
  of a path with the tick's, so `registersByPath[path]` never has it. Rows, widths and `flags.writable`
  therefore come from the node of the describe tree (the store's `deviceTree`), and values from
  `registersByPath`. The register of a watch is the described register whose `<path>.<name>` equals the
  watch's expression without regard to case, split at the last dot — the rule of the servers and of invariant
  I6 of the runner; a watch that resolves to no described register has no editor. (A watch is `{id,
  expression, value}` on the wire, and the client has only the forward direction today, `watchExpression`.)
- **What reads again after a write, and what follows the PC.** A second counter in the store, beside
  `refresh`, is advanced by the write actions when they are answered `{}`: on it the memory and disassembly
  panels read again **in place** (a byte edit must not move the disassembly). **No action advances
  `refresh`**, the signal that makes the disassembly follow the PC. `applyTick` advances it when a paused
  tick with the `stopId` of the stored stop shows another value of a listed target's PC register
  (`targets[].pcPath` / `pcRegister`) than the stored one. So the order of response and tick does not matter
  — design 2.9, "Order": the tick a request caused can reach the client before or after its response, and
  the tick of a write is no new stop that would move the view later —, and a PC write through a watch, or by
  another client, moves the view the same way. A reset is F3d's action (3.9): paused, its new stop advances
  `refresh` by itself. F3c owns this wiring in `tickState.ts` and `DisassemblyPanel.tsx`.
- **The synthetic server** (`e2e/support/syntheticDebugger.ts`, a v3 server since F3b) gets what the perf
  gate and the panel need: a node that offers a panel action, the capability `register-write`, and
  `flags.writable` on its registers.

**Owns.** New `src/app/panels/RegisterPanel.tsx` and its model; `Sh4RegisterPanels.tsx`; `panelRegistry.tsx`
and the tab lists and default layouts of `AppLayout.tsx` / `DockingLayout.tsx`; `MemoryView.tsx`,
`MemoryPanel.tsx`, `WatchesPanel.tsx`; the read-again wiring of `DisassemblyPanel.tsx`; the write actions and
the second counter of `debuggerDataStore.ts` / `tickState.ts`; `e2e/docking.spec.ts`, `memory.spec.ts`,
`watches.spec.ts`, the memory cases of `layout.spec.ts`, `e2e/support/syntheticDebugger.ts`, a new
register-panel spec; docs.

**Depends on.** F3a (the mock is what it is tested against), F3b. **Gate.** `npm run check`; the e2e suite,
the default layouts readable at 1366x768; the register-panel, memory and watches specs each have a block
against the mock with `supports: MOCK_M2_SUPPORT`, the server the M2 emulator resembles: there the ten
on-chip module panels and the M2 rows have no editor, and nothing is sent that would be answered `-32013`
by construction; a tree of 3,000 registers at five ticks a second re-renders only what changed (the method
of `e2e/device-tree-perf.spec.ts`). **Verifier.** An M1 server (no editors, nothing
sent that would be answered `-32004`); a scripted server on which two nodes offer one panel id (the panel
shows the first in tree order); a refused write (`-32013`, `-32602`, `-32001`, `-32000`) leaves the
editor open and says why; a late answer; a register without the flag has no editor, and neither has a watch
whose expression names no described register; a PC write whose tick arrives before its response, and one
whose tick arrives after it (a scripted server for each order): the disassembly follows the new PC both
times, and a byte edit moves it neither time; the memory row keeps its
text content (the specs that match it pass unchanged); the narrow panel cuts nothing at any width.

### 3.9 F3d — toolbar, reset, hotkeys

**Delivers.** The run controls as a component of their own: Run, Pause, Step Into / Over / Out for the focused
target, Reset; "Run to next breakpoint" removed. The focused target (M2-D39). The hotkeys F5, F6, F10, F11,
Shift+F11, with F8 and Shift+F8 as alternatives. `control("reset")` in the store (`ControlAction` has no
`reset` today), and **the reset action advances the read-again counter of 3.8 when it is answered `{}`**:
paused, the new stop advances `refresh` by itself; running, nothing is published, and the counter is all the
fetched views get. The in-app documentation
and the README. Decided here (M2-D39):

- **The focused target at first is the workspace's own**: `sh4` for `sh4-debugger` and for
  `mixed-mode-debugger`, `arm7` for `arm7-debugger`, `dsp` for `audio-debugger`.
- **"ARM7 held in reset"**: while `ENABLED` reads 0, off with that reason are the toolbar's three step
  buttons when the ARM7 is the focused target, and the step buttons of the ARM7 disassembly view. F3d owns
  the step-button state in `DisassemblyPanel.tsx` (F3c, before it, owned the read-again wiring there).
- **The help says what a write can do**: a PC, or code bytes, written so that the guest reaches what is no
  instruction ends the emulator in its default build, as it does for a guest that gets there by itself
  (M2-D42).
- **The smoke spec**, `e2e/emulator-smoke.spec.ts`, skipped unless `E2E_EMULATOR_URL` is set — the page URL
  the emulator prints (`Debugger: http://...`). The emulator is started on the guest of E6a as
  `tests/debugger/contract/contract.py` starts one (its list `STARTPAUSED`; the recipe of design 5.4):
  `-config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800 , Debugger:Enabled=yes ,
  Debugger:Port=0 , Debugger:Required=yes , Debugger:StartPaused=yes <dsptest.elf>`. A headless run is an
  automated run, in which `Debugger:Enabled` is `no` by default, and `StartPaused` without an enabled server
  ends the process with exit code 2; without `Port=0` the run would take the fixed port beside whatever else
  runs. Against that emulator the spec: connects at the start stop; runs to the entry of the program (a breakpoint
  at `0x8C010000`, where reios enters every program and the guest has `_start`; no scenario steps at the
  start stop, and neither does this); steps the SH4 from the toolbar; edits `R0` in the register panel and sees it in the tick; edits a
  byte in memory and reads it back; resets; and after a resume steps the ARM7.

**Owns.** A new `RunControls` component and hotkey hook, the focused-target state, the toolbar part of
`AppLayout.tsx`, `control` in `debuggerDataStore.ts`, the focus reports of the panels and the step-button
state of `DisassemblyPanel.tsx`, `ExecutionStatus.tsx` (room), `e2e/toolbar.spec.ts`, the run-control cases of
`layout.spec.ts`, the new smoke spec, docs.

**Depends on.** F3a, F3c; **and E7b merged**, for the smoke spec: it is written against an emulator that
exists. **Gate.** `npm run check`; the e2e suite, every run control usable at 1280x720; the toolbar spec has
a block against the mock with `supports: MOCK_M2_SUPPORT` (no DSP step is offered, reset is), and a case
for a reset while the target runs (no tick comes: the memory and disassembly views read again by the
counter); **the smoke
spec passes** against an emulator built from the head of `devcast` `skmp/nulldc3` with this package's bundle
embedded, started as above: the emulator is configured by itself with
`-DNULLDC_DEVTOOLS_DIST=<this package's checkout>/dist-native`, after `npm run build:native` there (A.0;
the option is never given to the tests project). The first run of the spec is this package's, not M2G's.
**Verifier.** The hotkeys with the focus in the page, in an address box, in Monaco, in an open dialog; a held
key; each button's off state and its reason (running, a host stop, a target without `stepping`, the DSP's
over and out, a server without `reset`, the ARM7 held); the focus following the panels in the mixed workspace;
no request is sent that would be answered `-32004`.

### 3.10 M2G — the gate (integrator)

On the merge of everything above, with the pinned UI checkout moved to the UI's head (section 4) and **the
emulators of items 1, 3 and 4 carrying the UI bundle of that head**. The tests project cannot make such an
emulator: the ones it builds are configured by `tests/system/build_emulator.cmake`, which passes on nothing
but `HAS_TA_LLE`, and `-DNULLDC_DEVTOOLS_DIST` given to the tests project replaces the small bundle of
`tests/debugger/net` instead (A.0). So the integrator configures the default build and the
`-DHAS_TA_LLE=OFF` build by themselves with `-DNULLDC_DEVTOOLS_DIST=<the pin>/dist-native`, after
`npm run build:native` in the pin, and hands them to the tests project as `-DNULLDC_EMULATOR=` and
`-DNULLDC_EMULATOR_TA_HLE=`; `ui` of `debugger.describe` says that the bundle is in. The five builds of
item 2 are compared as the tests project builds them: a state hash does not see a bundle.

1. The contract gate M2 against the emulator on the JIT, the interpreter and the forced fallback: 19
   scenarios passed, 0 partial, 0 not applicable each; and its second run against the guest, with its two
   notes.
2. The sound lines of the guest and of 2ndmix: identical to those of the tree before E6b (the baseline of
   section 5.5, taken again on that commit with this machine and build settings, since a line is good for one
   build on one host). The state hashes of 5.7 identical to those before the round, in the five builds.
3. Every ctest target green twice, none skipped, the number stated. `npm run check` and the e2e suite in the
   UI repository.
4. `e2e/emulator-smoke.spec.ts` against a headless emulator on the guest, started with the line of 3.9 (its
   second run; F3d made the first).
5. The documents. The design text changed by section 7 (the rows for design 5.2 to 5.4 and 2.5.2 among
   them); decision 9 of `DESIGN-V3-DECISIONS.md`; the plan's status line; `DESIGN-V3-OPEN.md`, whose head
   and last paragraph say "the next revision is 6 ... it is listed there, not here" (revision 6 is applied,
   and its list is section 2 of this file). And their copies under `docs/nulldc3/` of the emulator
   repository: `devtools-integration-design.md`, `devtools-integration-decisions.md`,
   `devtools-integration-open.md`, `PLAN.md`, and **this file as `devtools-integration-m2-round.md`** as it
   then stands, in place of the copy R6 committed (2.4 item 6), with its row in the name table of the
   design's copy and its line in `docs/nulldc3/README.md`. After M2G the design, the decisions and a frozen
   header cite this file for header revision 6, for the M2 tree (3.6.1) and for the guest's contract (5.2):
   the repository holds what it cites (M2-D65).

What it finds goes back as a fix round to the package that owns the file.


### 3.11 EXe — the leftovers of the emulator packages (added by the integrator, 2026-10-14)

Between F3d and M2G. What the verifiers of R6 to E7b left as notes and the integrator collected in Appendix I.4 to
I.6: gaps of the tests, waits without a bound, two small refusals and two sentences of frozen comments. Nothing in it
changes what the emulator does for a guest; nothing in it blocked a merge. Two parts, one per repository: this one
and 3.12.

**Delivers.**

1. **Every page of a range is witnessed.** `writes.mem.cached` and `smcwrites.cached` (`dbg_selftest_writes.cpp`)
   write, beside the range of three pages that witnesses its middle page, one range that **ends** in the loop's page
   and one that **begins** in it, each with an instruction of the loop in the bytes written (the verifier's edit of
   E7a: `agents/nulldc3/e7a/verify-2/scripts/mutants_z.py`, TEST; 142 bytes from `0x8C00FFF0` with `mov #0x5E,r12` in
   the slot). A writer that leaves out the first page of a range of more than one page, or its last, fails
   `smcwrites.cached` (I.5, leftover 1).
2. **No wait without a bound.** Every wait of the sessions `arm7`, `writes`, `core` and of the `arm7n.*` tests ends
   by a check that fails, within a bound the test states, where the machine never gives what it waits for; the
   session then prints its summary. Named by the verifiers: `ArmStop` of `writes.reg.arm7` (a device that stores the
   ARM7's PC as given); `core.reset` when `control.reset` is not answered `{}`; the helper thread of `arm7n.hoststop`
   when the machine does not stop; `arm7.bp.resume`, `arm7.burst.plain`, `arm7.bp.fiqvector` and `arm7.bp.context`
   under the four mutants that today end only by the 300 s watchdog (I.4, I.5 leftover 2). A bound is generous
   (a correct machine on a loaded host is far inside it) and is one constant a session.
3. **A NUL in an event name.** `breakpoints.add` refuses an event name with a NUL in it with -32602, as
   `state.setRegister` and `state.watch` do since E7a (`dbg_core.cpp`, the lookup `FindEvent(event.c_str())`;
   I.5 leftover 3). The selftest `core` holds it.
4. **Nothing written into the source tree by a test run.** A ctest run of `debugger.contract.*` (and of any other
   target that starts Python from the tree) leaves no `__pycache__` in the source tree: the targets run Python with
   `PYTHONDONTWRITEBYTECODE=1` (I.5 leftover 4). A check: a full ctest run on a clean checkout leaves `git status`
   empty.
5. **The made-up drive state is a state.** `regs.rows.gdrom` keeps its made-up drive state and disc type inside
   their enums, so the session `regs` is clean under UBSan (I.6).
6. **The completion in `dc_run` with a FIQ in the rest of its sample** (I.4 and I.5 leftover 5): `arm7n.hoststop`
   takes some of its stops where the rest of the sample has a FIQ to enter (on the `msr` that ends the F window, or
   on the instruction in front of it, with a request of the AICA latched) and requires at least five such stops in its
   ok line, as `steps` requires its fourth kind; the mutant that enters a pending FIQ one executed instruction late
   in the loop `dc_run` goes on with then fails it. If the guest gives no such stop within the test's run, the report
   says so with the numbers and the item is closed as a known limit; the guest is not changed.
7. **Two sentences of frozen comments, comment text only** (I.4 limit 3, I.5 limit 4): H6-19 (`dbg_emu.h`,
   `Park()`) no longer says that the first `Park()` of a thread behind an exit in a debugger stop arms the ARM7's
   half of an owed resume (the completion in `dc_run` has run the instruction of the stop); H6-33 and H6-35
   (`dbg_regs.h`, `dbg_mem.h`) say the thread rule of `Write()` as `Read()` has it ("(E) in the park context, or a
   thread that holds L while no emulator thread exists"), so that "returns kErrNotWritable with no machine" can be
   met. ASCII only; the integrator reads the new text before the merge.
8. The README rows these change (`tests/README.md`, `tests/debugger/selftest/README.md`), and the count of
   `regs.rows.sb` in its README row and comment: 115 new rows, of them 100 with a value, 14 write-only and SB_FFST
   behind a read function (I.6).

**Owns.** `libswirl/debugger/dbg_selftest_arm7.cpp`, `dbg_selftest_writes.cpp`, `dbg_selftest_core.cpp`,
`dbg_selftest_regs.cpp`; in `dbg_core.cpp` the event lookup of `breakpoints.add` and nothing else; the comment text
of the three sentences of item 7 in `dbg_emu.h`, `dbg_regs.h`, `dbg_mem.h` and nothing else of those headers;
`tests/debugger/**` CMake files, READMEs and `contract.py` where item 4 needs it; `tests/README.md`. No file under
`hw/`, no other production file.

**Depends on.** E7b merged (`199cdb4a`). **Gate.** All ctest targets green twice, none skipped, the number stated
(259 and what the package adds), with the UI pin at `6c753dc`; `git status` of the checkout empty behind them; the
five builds compile; the 46 sound lines and state hashes of the base and the tip identical (only tests changed);
the contract gates M1 and M2 on the three engines unchanged; under each mutant named in items 1, 2, 3 and 6 the
target that holds it fails by a check, with a summary line, inside its bound (no watchdog, no kill).
**Verifier.** The mutants of items 1, 2, 3 and 6 again, and one more for each bound: a bound that is too tight
(halved twice) on a host under the load of four emulators still passes the correct machine; the comment text of
item 7 against the code it describes; that nothing outside "Owns" changed.

### 3.12 EXu — the leftovers of the UI packages (added by the integrator, 2026-10-14)

The second part of the leftovers: what the verifiers of F3b, F3c and F3d left as notes (Appendix I.7 to I.9).
Two of them change what a person sees (the last rows of an address space in a small memory panel; a watch's
expression renamed from the keyboard); the rest are tests and words.

**Delivers.**

1. **The end of an address space can be reached in a small panel** (I.8, leftover 5). In a memory panel that shows
   fewer rows than a slice holds, a person reaches the last row of every address space the view offers (SH4, ARM7,
   and the others it lists) with the wheel and with the keyboard, and the listing is still not a box the browser
   scrolls (`overflow: clip` stays, I.8 decision 4). How (the view may stand so that the last row is the panel's last
   visible row; or the slice shrinks to what the panel shows) is the package's to decide and to say; an e2e test
   holds it at a panel ten rows tall, for the SH4 and the ARM7.
2. **No editor follows its byte into another row** (I.8, leftover 6): no byte editor opens while the slice of a move
   is being read, or the editor is recorded with the base of the rows on screen; the scratch sequence of F3c's
   verifier (held `state.getMemorySlice` answers, `f3c/verify-2/scripts/zz-v2.spec.ts`, R2-B) becomes an e2e test.
3. **A watch's expression is renamed from the keyboard** (I.8, leftover 8), with the keys of I.8 decision 1 (Enter,
   the space bar or F2 on the expression), the focus rule of decision 2, and nothing while the target runs.
4. **Tests that discriminate.** (a) `e2e/v2-server.spec.ts`, "a describe result without `targets` lists none":
   breakpoints at `0x1000` and at DSP step 5, matched as the two lines F3b's verifier tried (I.7, leftover 1).
   (b) One of the two no-server-yet e2e tests also expects the SH4 callstack panel without its note and the SGC panel
   with its Record button (I.7, leftover 3). (c) `e2e/device-tree-perf.spec.ts` expects no alert on the measured page,
   at its start and its end (I.7). (d) `e2e/memory.spec.ts`, "ARM7 memory ends where the address space of the ARM7
   ends", reads the settled fetch count and passes under `E2E_CPU_THROTTLE=4` (I.8, leftover 7).
5. **Two readers of v2 leniency go** (I.7, leftovers 2 and 4): `DeviceTreePanel.tsx` dims `dc.sh4.cpu` for a stale
   tick by the SH4's `pcPath` only when the server lists the SH4 (`supported`); `registerNumber` (`targets.ts`) takes
   hex with `0x` only, as every v3 server sends it, and its comment says so; a unit test holds each. Among the values
   that are no number are digit runs with a leading `0` and no `x` (`01`, `08c0000a0`), so that a pattern that takes a
   bare `0` for the prefix fails (added 2026-10-15). The emulator writes every value as `0x` and the digits
   (`regs::FormatValue`).
6. **A dialog in its exit transition counts as closed** for the hotkeys (I.9, leftover 11): F5 right behind Escape is
   taken by the toolbar, not left to the browser; the toolbar spec holds it.
7. **Words** (I.8 leftover 9, I.9 leftover 10): the README's "an arrow never takes the place to a row the panel cuts
   off"; the block "The UI against the emulator" makes its three run directories (`mkdir -p`) as its first line.

**Owns.** `src/app/components/MemoryView.tsx`, `src/app/panels/MemoryPanel.tsx`, `WatchesPanel.tsx`,
`DeviceTreePanel.tsx` (the one line of item 5), `src/state/targets.ts` (`registerNumber`) and their unit tests,
`src/app/layout/useRunHotkeys.ts`; `e2e/memory.spec.ts`, `watches.spec.ts`, `v2-server.spec.ts`,
`device-tree-perf.spec.ts`, `toolbar.spec.ts`, and one of `connection.spec.ts` / `offline.spec.ts`; `README.md`,
`src/app/layout/documentationSections.ts`; **for item 5, the two `arm7Held` assertions of `src/state/runControls.test.ts`
that the rule changes** ("is what ENABLED of its node says" and "is not assumed ...": `arm7Held` reads `ENABLED` through
`registerNumber`; granted by the integrator on 2026-10-15, after the package's third verification found the item
undeliverable without them), and nothing else of that file. Not `scripts/contract/`, not `server/`, not the schemas.

**Depends on.** F3d verified (`6c753dc`). **Gate.** `npm run check`; the whole unit suite three times in a fresh
clone; the e2e suite whole (`--workers=2`); `e2e/memory.spec.ts` and the specs of items 1 to 3 under
`E2E_CPU_THROTTLE=4`; the smoke spec once against the emulator `6fe01759` (EXe merged) with this package's bundle embedded, run by
`agents/nulldc3/f3d/scripts/smoke.sh` in the order of I.9. **Verifier.** Item 1 at panels of 3, 10 and 40 rows for
each address space the view offers, by wheel, keys and the address box; the mutants that take back each item; the
keyboard and focus rules of I.8 on the new watch control; nothing sent that would be answered -32004 or -32013.

---

## 4. Order

One package at a time. "Based on" is the head of the integration branch of its repository at that moment.

**The UI checkout an emulator gate runs with is pinned** (M2-D44). The emulator's gates execute UI source:
`debugger.regs.panels` parses `src/lib/debuggerSchema.ts`; every contract target that uses the runner —
the gates, and `debugger.contract.hash.idlebp` with `.interp`, which run it in its `--idle-breakpoint` mode
— runs `npm run contract`
from `NULLDC_DEVTOOLS_DIR`, whose runner imports the schemas, `src/lib/binaryFrame` and two files of
`src/services/`; `debugger.disasm.dsp_playground` imports `src/app/dsp/dspUtils.ts`; and
`debugger.net.interop_ws` uses the checkout's `node_modules`. (No package of M2 edits what the last two
use; the pin names them all the same.) UI packages commit onto the one branch `skmp/nulldc3` as work lands, so its head can be a
package half done. An emulator package and its verifier therefore run their gates with the UI checkout at a
commit the integrator names: **the last verified UI head**, recorded when that UI package passed (at the start
of the round: F3a's merge commit). It is a detached worktree of `dreamcast-devtools`,
**`agents/nulldc3/wt/ui-pin`**, created once by the integrator at F3a's verified head with its `node_modules`
and kept until M2G, moved to each UI head that is verified; it is passed as `NULLDC_DEVTOOLS_DIR`. **It is never a directory named
`dreamcast-devtools` beside the worktrees**: for a worktree `agents/nulldc3/wt/<pkg>` that name is where the
emulator's own configure looks for a UI bundle (`<tree>/../dreamcast-devtools/dist-native`) and where the
tests project looks for the UI checkout by default, so from the day a `dist-native` is built there every
emulator of every worktree would embed it and `debugger.describe` would change (A.0). Where an emulator has
to carry the UI — F3d's gate, M2G — `NULLDC_DEVTOOLS_DIST` names a `dist-native` directory, built by
`npm run build:native`, and is given to the emulator's own configure, never to the tests project (3.9,
3.10). A fix round of F3a that an emulator gate asks for is made on the branch
and moves the pin when it is verified.

| # | Package | Based on | What a person can try when it is merged, and where |
| --- | --- | --- | --- |
| 1 | **F3a** | devtools `22b358e`; the merged M1 emulator for its observation and the last line of its gate | the UI against the mock (the UI repository's mock server): the device tree shows the M2 nodes, `dc.sh4.cpu` offers a "registers" action that opens a placeholder. `npm run contract -- --milestone M2` passes on the mock, and with `--mock-support m2` on the mock that can do what the M2 emulator can |
| 2 | **R6** | devcast `d546aa76`; devtools pinned at F3a | nothing: the wire is what it was (that is its gate) |
| 3 | **E6a** | R6 | `tests/debugger/guest/dsptest/dsptest.elf` in the emulator with sound on: a tone whose pitch moves. The sound lines as ctest targets |
| 4 | **E6b** | E6a, baseline taken | the UI against the emulator (the page the emulator serves at the address it prints as `Debugger: http://...`, or the UI's dev server with `?ws=`): the ARM7 disassembly panel has its step buttons and its gutter, ARM7 breakpoints stop, the ARM7 callstack follows. These controls are in the UI since F2 and wake up with `stepping: true` |
| 5 | **E7a** | E6b | in the UI: a watch on a register of `dc.sh4.cpu` or `dc.aica.arm7` can be edited (the watch editor exists since F2). Memory editing and the reset button wait for F3c and F3d; the runner's parts show them |
| 6 | **E7b** | E7a | in the UI: the device tree shows the system bus, PVR, AICA, GD-ROM and maple registers of the running machine, live |
| 7 | **F3b** | F3a (devtools head) | a v2 server gets the banner and notices instead of half-working panels |
| 8 | **F3c** | F3b | register panels for the SH4, the ARM7 and the ten on-chip modules; registers and memory bytes edited in place |
| 9 | **F3d** | F3c; devcast at E7b or later | step, reset and the function keys from the toolbar; the UI driven against the emulator by a spec |
| 10 | **M2G** | everything | M2 on the emulator |

- **F3b and F3c may be moved forward** to a place behind F3a: between two emulator packages, never beside
  one, and only as whole packages — verified, and the pin moved, before the next emulator gate starts. They
  stand last here so that the emulator's part of the gate is not kept waiting by UI work; if the emulator
  packages wait for a fix round, a UI package takes the machine. F3b's gate has the emulator's contract
  targets in it for this reason (3.7); F3c edits nothing the runner imports. **F3d stays behind E7b**: its
  gate runs the smoke spec against an M2 emulator (3.9).
- E6b before E7a, because E7a marks the ARM7 rows writable and tests them, which needs the body E6b gives
  `SoundCPU::DebugSetReg`; and because E6b is the package with the most risk, which should not be the last.
- The one red window of the order: between the merges of F3a and R6 the emulator's `debugger.regs.panels`
  fails (2.4 item 3). Nothing else runs in it.

What can be prepared while another package has the machine (documents only):

- the owner's answers, each wanted before the package that builds on it (no package waits: work proceeds on
  the recommendation, and 6.2 says what a late "no" costs):
  **M2-Q1** before F3a builds its `MOCK_M2_SUPPORT` profile, and at the latest before R6 freezes H6-33 and
  H6-39; **M2-Q5** before R6 freezes H6-23 — behind R6 a "no" to either is header revision 7 with fix rounds
  of the packages 6.2 names; **M2-Q4** before E7a starts (R6 writes H6-33 as printed; what a "no" costs
  before and behind E7a is in 2.2 with H6-33); **M2-Q2** before E7b starts (a "no" changes its tables from
  their first line); **M2-Q3** and **M2-Q6** at any time;
- the briefs of Appendix A are ready; a verifier's brief is the "Verifier" paragraph of its package in
  section 3 with the gate above it;
- for E7b, the row lists as a table (name, word offset, shift, width) for `dc.aica.common` and a channel, from
  `devices-m2.md` Appendices C and D, and the split of the 68 PVR registers and the 115 system bus registers
  by 3.6.1 (Appendices G and H);
- for E6b, a walk through `dbg_emu.cpp` that names, for each sentence of H6-17, the function it changes
  (`sound-burst.md` 4.4 and D8, D10, D11, D18 have the places);
- for E6a, the check that an `arm-eabi` assembler is at hand (5.3).

---

## 5. The sounding guest

### 5.1 The choice

**Design B of `guest-harness.md` 6.2: a bare-metal SH4 program with its own small ARM7 program**, written
fresh, in `tests/debugger/guest/dsptest/` (M2-D31). The reasons:

- It can be committed. Its bytes are the project's own source assembled by binutils: no date, user, host or
  path in them, nothing linked in, about 5 KiB. A KallistiOS ELF carries the library's build banner (date,
  user at host, path), is about 0.4 MB of third-party code with a prebuilt ARM driver, and is not the same
  bytes after the library is rebuilt (`guest-harness.md` 3.4).
- It sounds under reios by itself. The stock KallistiOS driver, like the player of 2ndmix, waits on a timer
  FIQ that it never enables: under reios it is silent unless the guest writes what the retail boot ROM leaves
  (`guest-harness.md` 3.2, inferred).
- Its lines can be reasoned about. Everything the SH4 does to the sound happens before the first AICA tick;
  from then on the ARM7 and AICA-timed interrupts drive it, and those run on the same engine in both columns
  of the matrix (one write of the SH4 follows the start: the pan write of `g_irq`, in the tick whose
  interrupt it answers; 3.3 item 3 has what happens if it lands differently on the two engines).
  So `samples`, `regs` and `arm7` can be equal on the JIT and the interpreter, which gives the
  matrix a check it did not have: a change that moves one engine's line and not the other's is on the SH4
  side.
- It is built for what the burst machine has to survive: ARM7 state that differs at every sample, a FIQ that
  meets every position of the burst, register writes by the ARM7 that are heard at their sample.
- It follows the pattern the tree has for guests (`tests/debugger/selftest/emu_guest.s` with `gen.sh`).

Rejected: A, a KallistiOS program on the stock driver — what a real program looks like, which is its one
advantage; it stays what 2ndmix is, a program named by `NULLDC_ELF_DIR`, a further row of the matrix and not
the guest. C, KallistiOS with an own ARM7 program — the banner and the size of A with the assembler of B.

### 5.2 What it is, and what later packages rely on

The package writes the program; these properties are its contract, and E6b's selftests and neutrality
targets, E7a's session `writes`, the contract's second run, F3d's smoke spec and E8 use them by name.

SH4 (`dsptest.s`, linked at `0x8C010000`, its first instruction there: reios enters every program at that
address):

| Label | What |
| --- | --- |
| `_start` | sets `SR` (privileged, `FD` clear: a test may write FP instructions into `g_main_loop`), `VBR` and the stack (the top of main memory); holds the ARM7; copies the ARM7 image to wave memory 0 and one period of a 16-bit wave to wave memory `0x10000`; writes a table of (address, value) to the AICA — `MVOL`, two channels by the recipe of `guest-harness.md` 2.5, key on; unmasks the AICA's interrupt to the SH4; releases the ARM7; goes to `g_main_loop`. All of it is done before the first AICA tick on both engines |
| `g_main_loop` | counts; at least four consecutive instructions that are neither control flow nor a delay slot, then **one PC-relative branch back (`bra`)**. No instruction of the loop takes the PC from a register or from memory (no `jmp`, `jsr`, `rts`): a PC that is written into another mirror of main memory stays in that mirror while the loop runs, and `g_irq` returns by `rte` to where it was entered from. The mirror case of E7a's `writes.reg.pc` rests on it (3.5), and E7a may not touch the guest |
| `g_irq` | at `VBR + 0x600`: acknowledges the AICA's interrupt with word accesses, makes one register write that is heard (the pan of the second channel), counts, returns. It leaves every register of the interrupted code as it was and uses no stack (the banked registers are its own): the contract's write probe keeps bytes below `R15` |
| `g_sleep_loop` | `SLEEP` in a loop; the AICA's interrupt wakes it. Not reached by itself: a test sets the PC |
| `g_hold_arm7`, `g_release_arm7` | hold the ARM7 and spin; release it and go to `g_main_loop`. Not reached by themselves |
| the table | has a reserved, empty part for E8: `MPRO`, `COEF`, `MADRS`, the ring buffer, `EFSDL` / `EFPAN`, the playing channel's `IMXL` / `ISEL` |

ARM7 (`arm7.s`, linked at 0, at most 100 instructions):

| Label | What |
| --- | --- |
| the vectors | `0x00` to `0x1C`; the FIQ vector branches to `a_fiq` |
| `a_reset` | stacks and the FIQ-mode registers; **every address and constant that `a_main_loop` and `a_fiq` use, into registers** (the two run without a literal load: below); the interrupt levels (`SCILV0-2`) so that timer A has a level that is not 0; every pending bit cleared; timer A with a period of **13 samples**, enabled in `SCIEB`; F cleared in the CPSR |
| `a_main_loop` | at least four consecutive instructions that are not control flow (it counts in a register and in wave memory, copies the timer's count and `SCIPD` to memory, **folds both into a register that nothing else writes, and then clears `SAMPLE_DONE` through `SCIRE` with a word access**, so that the fold says between which two iterations each time step fell), then `bl a_leaf`, then one branch back. **The F window: at least four of its plain instructions execute with F set in the CPSR, an `msr` in front of them (label `a_fwin`) and an `msr` behind (label `a_fwin_end`); the instruction in front of `a_fwin_end` is one of them** (E6b's `arm7.step.fiqbehind` sets its breakpoint there: 3.4). A request that a time step raises while F is set is entered behind the second `msr`: inside a sample, not at its first instruction — unless that `msr` is the last instruction of its sample, when the first FIQ test of the next sample makes the entry. The state of the ARM7 differs after every sample, and the `arm7` field of every later tick carries every observation the loop made |
| `a_leaf` | two plain instructions and the return: a leaf, for step over and step out |
| `a_fiq` | reads L (the acknowledge), clears and re-arms the timer with word accesses, counts, **folds L and the timer count it finds into a FIQ-mode register, and into the same register its return address and saved status (`lr`, `SPSR`)**: where, and under which flags, every entry was made. **Changes something that is heard** (the pitch word of the first channel, between two values); every 64th time it keys the second channel off and on and raises the AICA's interrupt to the SH4; ends the service and returns |
| two bounds | from any instruction of `a_main_loop` or `a_leaf` the next `bl a_leaf` is **at most 32 instructions** away; from the FIQ vector to the return of `a_fiq` **at most 48 instructions** execute, on its 64th-time path too. The runner's `bl` rule has 96 steps for each of its two arrivals and may walk through one handler on the way (3.1 item 3): 32 + 48 fit, and the period of the FIQ puts no second handler into one search |

- **"Not control flow" is what the contract runner takes for it** (`isSh4ControlFlow` and `isArmControlFlow`
  in `scripts/contract/disasm.ts` of `dreamcast-devtools`), applied to the line as the emulator disassembles
  it. For the SH4: none of the branch mnemonics, `trapa`, `sleep`, `.word`. For the ARM7: no `b`, `bl`, `bx`,
  `swi` in any condition, no `.word`, **and no operand that names `pc` or `r15`**. The emulator prints a
  literal load as `ldr rN, [pc, #..]`, which the runner therefore takes for control flow: a loop whose
  "plain" run loads its AICA or wave-memory addresses with `ldr rN, =addr` is plain for the assembler and
  offers the runner no breakpoint site (it needs two consecutive plain lines, and the line behind the site
  plain as well). Hence the registers `a_reset` loads; the four consecutive instructions of `a_main_loop`
  have no literal load, and an `msr CPSR_c` (with an immediate: no register, no literal) is a plain line.
  E6a's report has the lines as the emulator prints them (3.3, Gate).
- The period of 13 samples is odd on purpose: the FIQ meets each of the 32 positions of a burst within 13
  ticks.
- **A FIQ is entered in two ways, and the guest has both.** A request is raised by a time step — timer A
  overflows there —, so without more every FIQ of this guest would be entered by the first FIQ test of a
  sample, and a debug loop that looked at the pending FIQ once per sample, where the interpreter looks in
  front of every instruction, would pass every gate; on a guest that masks F around a critical section such
  a loop moves ARM7 timing whenever a breakpoint is active. The F window is that critical section: a request
  raised while F is set waits, and is entered behind the `msr` that clears F (`MSR` works out the pending
  FIQ again). About one entry in five is of that kind. E6b's `steps` target, its test
  `arm7.step.fiqbehind` and three mutations of its verifier rest on **those of the kind that the third or
  a later FIQ test of their sample makes** (3.4). When the `msr` of `a_fwin_end` is the first instruction
  of its sample — the time step in front of it raised the request —, the entry is made by the second test;
  under steps the stop on that `msr` stands in a new phase, which the two tests do not count and in which
  a debug loop without the FIQ test where it goes on with a suspended sample still makes the entry. With a
  window of four instructions that is one in five of the
  kind by expectation, **but the phase is no expectation**: `StepMany` starts every sample at 0 ticks and
  drops the overshoot of the one before, so where the loop stands when a time step overflows the timer is
  the orbit of a deterministic map on the positions of the loop, and it can stay on few of them. So E6a
  counts the entries before the baseline by the number of the test that made each — the first, the second,
  the third or a later one — and needs **at least 100 made by the third or a later test in the 2000
  ticks** of each configuration, **and no stretch of more than 32 ticks without one** (3.3, Gate;
  otherwise the window is widened, which also lengthens the loop and moves the orbit).
  **The two kinds are told apart by the FIQ test that makes the entry, not by where the entry returns
  to** (M2-D66). The first kind is made by the first FIQ test of a sample; the second by a later test of
  the same sample, which on this guest is the one behind the `msr` of `a_fwin_end`, with the return address
  `a_fwin_end` + 8. An entry of the first kind has that same return address when its sample starts with the
  instruction behind the `msr` — the `msr` was the last instruction of the sample before, with a request
  waiting or with one that the time step in between raises —, and with some 4900 entries in 2000 ticks, four
  in five of the first kind, over a loop of some 25 instructions, a run has 150 to 200 of those. So the
  scratch build of 3.3 prints the number of the test that made each entry, and a step is taken for an entry
  inside a sample only when no ARM phase ended in it or in the step before it (3.4: `out[1]` of
  `SoundBurstCounts()`), which holds for the entries that the third or a later test makes and for no
  other.
- **The mode `a_main_loop` runs in is E6a's choice: system, or a mode with an SPSR** (the reset leaves the
  ARM7 in SVC). Never user: `MSR CPSR_c` changes the control bits in a privileged mode only, and the F
  window needs them. E6a's report names the mode. The tests that write the CPSR are written for either
  (the table under H6-38; M2-D67): what the row SPSR reads behind such a write depends on the mode it is
  made in.
- **The folds are what makes the sound line a witness of the order inside a burst.** The line folds the
  samples, the AICA's register file, the save state of the sound CPU and twenty words of the SH4; wave memory,
  main memory and the scheduler are in no field (`guest-harness.md` 1.6). What the ARM7 loop reads — the
  timer's count and `SCIPD` at the instruction where it reads them — would be in wave memory only, and a
  resumed sample or a step that moved one time step or one FIQ entry by an instruction, with the instruction
  count of the tick kept, would leave every field as it was. A fold is a running sum that depends on order
  (rotate and add); every register of the ARM7, the banked ones included, is in the `arm7` field.
  **Their resolution: every FIQ entry to the instruction; a time step to the loop iteration.** The `arm7`
  field is folded once per tick, and a tick has about two and a half FIQ entries (32 samples, a period of
  13): `R14_fiq` and `SPSR_fiq` by themselves show the last entry of a burst only, and the loop's two reads
  show an entry only when it crosses one of them. Hence the handler's fold of `lr` and `SPSR` at every
  entry, of either kind: `lr` is the instruction the entry was made in front of, at the first instruction of
  a sample or behind the `msr` of the F window. A time step that does not overflow the timer changes nothing the handler sees; the loop's clear of
  `SAMPLE_DONE` makes its fold of `SCIPD` say in which iteration the step fell, and a step that does
  overflow the timer moves a FIQ entry.
- The SH4 program uses no FP instruction and no `MACH` / `MACL`: a stress may write `MACH` into the running
  guest (3.5), and a test may write FP instructions into `g_main_loop`. **It does not write FPSCR**, which
  stays what reios leaves, `0x00040001` (PR 0, SZ 0): E7a's test of the FP mode of compiled code has to
  compile its block with PR and SZ clear (3.5), and checks that before it does.
- **The SH4 program stores into no page that holds its code.** Its counters are in registers (`g_irq` in
  its banked ones), and what it stores it stores in wave memory, in AICA registers and on the stack page
  (the top of main memory); it does not write `CCR`. Three things rest on it. E7a's code-write tests start
  from a page that is not a data page (a page the guest stores into is a data page before a test's first
  write). With `Dynarec.SmcCheckLevel` other than its default, a write of `CCR.ICI` empties the code cache,
  which would hide a missing discard from `writes.mem.cached` (3.5). And the build with a recompiler and
  without the virtual memory layout ends the process at a guest store into a page of compiled code, with or
  without a debugger (3.5, Verifier): this guest can run there. E6a's verifier checks the property by
  disassembly.
- What a guest must not do in this emulator, on either processor: a byte write to `SCIPD`, `SCIRE`, `MCIPD`
  or `MCIRE`, and a read of `0x2810` with `AFSET` set (each ends the emulator: `guest-harness.md` 2.5).
- The guest is for the emulator only (no G2 FIFO waits, no loader); its README says so. It is kept small: the
  correctness of the sound generator is PLAN track 5's subject and has the caique cases for it. It does not
  grow a feature per later fix.
- Nothing of `wren7-rtl` is copied (no licence line yet: `PLAN.md`, "Needs the user").

### 5.3 How it is built and what is committed

- `gen.sh`, in the pattern of `tests/debugger/selftest/gen.sh`: the SH4 part with the sh-elf binutils
  (`SH_ELF_BIN`), the ARM7 part with an arm-eabi binutils (`DC_ARM_BIN`: GNU `as`, `ld`, `objcopy`); the link
  discards `.comment`; labels carry a type and a size, so that the debugger's symbol view ranks them.
- Committed: `dsptest.s`, `arm7.s`, `gen.sh`, **`dsptest.elf`**, **`arm7.bin`** (the assembled ARM7 image,
  which the SH4 source takes in with `.incbin`, so that an SH4-only change needs no ARM assembler),
  `dsptest_labels.inc` (every SH4 and ARM7 label as a `#define`, generated, for the selftests), `SHA256SUMS`,
  `README.md`. The emulator and every test build and run without either toolchain.
- `SHA256SUMS` lists `dsptest.s`, `arm7.s`, `gen.sh`, `dsptest.elf`, `arm7.bin` and `dsptest_labels.inc`, and
  nothing else: not `soundlines.py`, not `CMakeLists.txt`, not `README.md`. They are the files E6b must not
  touch (3.4). The runner's *neutral* mode and the function of the CMake file are E6b's from its turn on, and
  a manifest over every file of the directory would turn `debugger.guest.dsptest.files` red at E6b's first
  fix there.
- `gen.sh --check` rebuilds and compares with the committed files, byte for byte.
- One assembler is the reference for the ARM7 image, GNU `as`: two assemblers can place a literal pool
  differently. If no arm-eabi binutils is at hand, clang with `--target=armv4t-none-eabi` and `llvm-objcopy`
  may stand in (E3's verifier built its guests that way), and the README then names it as the reference.

### 5.4 Before its lines count

A sounding guest is also what finds uninitialised sound state: E3's verifier guests did, in channels that
were never keyed on (`guest-harness.md` 5.4). So before the lines are a baseline: the soak of 3.3 (50 runs per
configuration, half under load) and one memcheck run on the interpreter with a short N. A finding is fixed in
`hw/aica/` or `hw/arm7/` by E6a under its exception, before the baseline.

N is **2000** ticks for the guest (1.45 emulated seconds: every period in it — 13 ticks for the FIQ's
positions, 26 for the key-off — more than seventy times). 2ndmix stays in the matrix at 40000 (M2-D33).

### 5.5 The lines to take before the burst is touched

On the commit that merges E6a, with the default build, by the runner's *record* mode, into a file under the
package's work directory (`agents/nulldc3/e6b/`), with the commit, the build flags and the compiler beside
them. **No line is stored in the tree** (design 5.7 has the same rule for state hashes): a line is good for one
build on one host (the generator's tables come from `pow()`).

| Lines | Runs |
| --- | --- |
| the guest, N = 2000: `aica.NoBatch` no / yes x JIT / interpreter | 2 each, 8 lines, 4 distinct |
| 2ndmix, N = 40000: the same four configurations | 2 each |
| the state hashes of 5.7 on 2ndmix (the existing targets), and the exit state hash of the guest at 90 vblanks (`config:Exit.AfterVblanks=90 , config:Exit.StateHash=yes`; the runner's second kind of run), both `aica.NoBatch` settings, JIT and interpreter | 2 each |
| the same lines and hashes of the `-DNULLDC_DEBUGGER=OFF` build of that commit (3.4, "before / after") | 1 each |

E6b compares its tip against that file as its own check. Its verifier does not use the file: it builds the
base and the tip itself and compares its own two sets. The recorded lines of E3 are not a baseline for
anything: their `sh4` field predates the capability on both engines (`guest-harness.md` 1.2).

### 5.6 Behind M2

- E8 adds the DSP program part. That changes the ELF, and a changed ELF is another workload: E8's first step is
  E6a's again — the new bytes, the soak, a new baseline — and only then its own change (M2-D35).
- The lines of the guest are what PLAN tracks 5 and 6 are held to as well. Their fixes move fields on purpose;
  the cut of those tracks says which field each fix may move.
- The package that wires the T5.0 core removes `aica.NoBatch` and the batch path (PLAN: the DSP is always
  on). With them it removes the `.batch` half of the `debugger.sound.*` targets — the runner's key, and the
  `batch` half of what `nulldc_sound_neutral` registers — and the line `arm7.persample` of the selftests,
  whose setting is then the only path (the line `arm7` stays).

---

## 6. Decisions and questions

### 6.1 Decisions of this cut

Each: what is decided, and the alternative that was not taken. "Design" means `TRACK1-DESIGN.md`; section 7
has the text changes that follow.

**The sound side**

- **M2-D1. The burst machine is in `Dreamcast_impl`.** The loop of 32 leaves `SoundCPU_impl::Update` for
  `AicaUpdate`, and `SoundCPU` gets `RunSample` (2.3). `SoundCPU::Update(cycles)` keeps its behaviour for the
  selftest `regs`. *Rejected:* the machine's ARM half inside `SoundCPU_impl` with `AicaUpdate` keeping the
  owed tail — the smaller change today, moved again in M3 when the DSP phase needs the sequence above both
  devices.
- **M2-D2. M2's burst state is `{suspended, i, ticks_done, arm_on}`.** No `per_sample` latch, no phases, no
  `mixl` / `mixr`: `aica.NoBatch` stays read live. *Rejected:* building the state of design 2.5.2 now —
  nothing of M2 can suspend where it would matter, the latch breaks the selftest that flips the setting
  around `Update`, and the wiring of the T5.0 core removes the batch path it is for.
- **M2-D3. A check that hits always suspends.** It does not look at the run flag. A host stop or guest exit in
  the same slice runs the burst to its end without checks in `dc_run`; a guest reset discards it. *Rejected:*
  the rule of `emu::OnEvent` (no stop while the machine is leaving `Run()`) — it narrows the window and cannot
  close it, so both paths would exist.
- **M2-D4. A step of the ARM7 with a burst suspended stays in the park context,** and when its instruction
  was the last of the burst it is complete at the end of the burst, with none suspended and the tail run.
  *Rejected:* going on into the SH4's run to the next burst — a step would then cost up to a tick of SH4 time
  for one instruction that has already executed.
- **M2-D5. A step of the ARM7 with no burst suspended runs the SH4 to the next burst without publishing
  `Running`.** It is a third way out of the park context (H6-17): left as for a resume, kept as a step.
  *Rejected:* design 2.5.3 as written, "resume with a one-shot break before the next instruction" — a resume
  publishes `Running`, which S16 forbids, and a break before the next instruction executes nothing. *Also
  rejected:* running the next burst early, in the park context — the ARM7 would execute ahead of SH4 writes
  that come before it in a run: not what runs.
- **M2-D6. The rules of that step.** It executes its instruction also with a listed breakpoint on it. It is
  complete with nothing executed when a FIQ is entered in front of the instruction (the stop is at the
  vector; otherwise it would end at `0x20`, which S16 rejects) and when the burst finds the ARM7 held in reset
  (otherwise the machine would run on with a stop published and no call served). A stop of the SH4 that comes
  first is published with its own cause. *Rejected:* the SH4's rule that an uncovered breakpoint on the
  step's own instruction stops it — it exists for the SH4's mechanism, not for the user.
- **M2-D7. `dbg_arm7_armed` follows activity.** A listed breakpoint arms the debug loop only while the
  category `arm7` is in the active mask. *Rejected:* armed while enabled, as design 2.5.3 reads — on arm64 a
  muted breakpoint, or one left behind by a client that is gone, would keep the ARM7 on the interpreter and
  the notice up; and no code cache makes the difference cost anything here.
- **M2-D8. The fallback notice follows the backend that is selected** (`SoundCPU::DebugIsRecompiler`), not the
  host: arm64 with the ARM7 recompiler switched off is native. It is set while the debug loop replaces a
  recompiler **and in the stop that ends a step, a resume or a run in which it did** (H6-21): a step and a
  temporary breakpoint disarm before their stop is published, so "while armed" alone gives 0 in the tick that
  matters. The SH4's notice has the same second half. arm64 is read and not run in this round, so the frozen
  sentence is the whole specification. *Rejected:* repeating `dc_run`'s selection in `dbg_emu.cpp`.
- **M2-D9. Step over and step out of the ARM7 are resumes with one temporary breakpoint.** They publish
  `Running` when they leave; the temporary breakpoint goes at every published stop, as the SH4's do. With a
  burst suspended the temporary breakpoint can stop in that same burst, and then nothing was left and no
  `Running` was published. *Rejected:* a stack condition as the SH4's step over has — the design calls the
  ARM7's a heuristic and the UI says so.
- **M2-D10. Events raised while a suspended burst is run on in the park context are logged and do not
  stop.** *Rejected:* stopping behind the burst — a second kind of stop inside a resume for at most 32
  samples of events.
- **M2-D11. `breakpoints.add` on `dc.aica.arm7.pc` never answers `-32012`.** The code is for the three steps.
  *Rejected:* design 3.3 as written — it makes a breakpoint on the ARM7's first instruction impossible (a
  machine comes out of reset with the ARM7 held), it protects nothing (`toggle` has no such answer, and a
  breakpoint outlives the clock), it has no answer in `NoMachine`, and no scenario asks for it.
- **M2-D12. The ARM7 address rule.** The key is the address under the mask of the wave memory with bits 0 and
  1 clear. A breakpoint address is a multiple of 4 and at most `0x00FFFFFF`, else `-32602`. A stop reports the
  PC as it is, not masked: it can be above `addressMax` (2ndmix's first ARM7 program runs through
  `0xFFFFFFE8`), where no line can be asked for and no breakpoint set, and the runner's `arm7` part knows it
  (3.1). **A PC the debugger writes is stored aligned** (H6-38): the fetch reads four bytes at the address
  under the mask without aligning it, every write of R15 by the core clears bits 0 and 1, so an unaligned
  next-PC is a state no run produces — and with the virtual memory layout a written PC whose low 21 bits are
  `0x1FFFFD` to `0x1FFFFF` would make the next fetch read past the mapping of the wave memory and end the
  process. The key drops bits 0 and 1 all the same. *Rejected:* storing the PC as given, which the first cut
  had ("the fetch does not clear them").
- **M2-D13. The resume exemption of the ARM7.** `PrepareResume()` arms it only behind a stop the ARM7 check
  decided, with the PC at the leave equal to the PC of that stop (a PC the debugger wrote away and back
  counts: the SH4's rule, and `OnPcWritten` is the same for both processors), with the ARM7 clocked, and
  with `dbg_arm7_armed` not 0. It is used only by a check at that PC, and it ends at the first check, at a
  change of `dbg_arm7_armed` to or from 0, and at a burst that finds the ARM7 held. A step-over on a `BL`
  and a step-out arm it for the PC they are asked from whatever decided the last stop: every run-control
  gesture of the ARM7 executes the instruction it stands on. A pause that happens to find the ARM7 in front
  of a listed breakpoint does not skip it at a resume. *Rejected:* "the PC is that of the last stop" alone,
  which would lose that hit; an exemption that waits for a breakpoint added later, or for an ARM7 released
  later (a breakpoint on the ARM7's first instruction, the case of M2-D11, would be skipped and never come
  again); `OnPcWritten` that forgets the stop, which made a PC written back onto the stop's address stop at
  once on the ARM7 and run through on the SH4.
- **M2-D14. The ARM7 breakpoint state is a module of its own** (`dbg_arm7.h`, frozen; `dbg_arm7.cpp`), as
  `dbg_sh4` is. *Rejected:* inside `dbg_emu.cpp`, which is sequencing.
- **M2-D15. The names emulator code sees are global** (`dbg_arm7_armed`, `dbg_arm7_Check`), as the SH4's.
  *Rejected:* the design's spelling `dbg::arm7_armed`.

**The core and the ownership**

- **M2-D16. Way A for `dbg_core.cpp`:** the revision carries stubs and the ARM7 wiring of the core, and the
  flip is a query, `emu::TargetSteps` (3.0). *Rejected:* B, the wiring in the second package — the ARM7 is
  then not seen from a client until a package that does not own the sound side is merged; C, a third package
  for the core — a third verification round for forty lines.
- **M2-D17. The leftovers of J1 (6.1.2 items 1 and 2) go to R6.** *Rejected:* E6 with its ownership widened
  into `hw/sh4/` — dead state of the run flag is not sound work, and R6 touches `libswirl.cpp` before E6b
  makes it its own.

**Writes**

- **M2-D18. Writable in M2 are the registers of the two processors:** all 69 of `dc.sh4.cpu`, and the 40 of
  `dc.aica.arm7` that are not `ENABLED`. Every other row is read-only and answers `-32013`;
  `AICA::DebugWriteReg` gets no body. *Rejected:* device registers writable now — each needs its write
  semantics stated first (M2-Q1). What the processor registers and memory writes can still do is M2-D41 and
  M2-D42.
- **M2-D19. A register says whether it has a writer:** `flags: {"writable": true}` on the register in
  `debugger.describe`, absent otherwise, never in a tick. The schema has the optional `flags` record already.
  *Rejected:* nothing on the wire — the UI would offer an editor on some three thousand registers that refuse.
- **M2-D20. `state.setRegister`.** `path` and `name` are matched without regard to case, as a watch
  expression is. Its own errors, in this order, behind the lifetime: unknown path or name `-32602`; a register
  of width 0 or without a writer `-32013`; a value that cannot be parsed `-32602`; a value the device refuses
  `-32013` — in M2 two: a PC of the SH4 with bit 0 set, and a CPSR of the ARM7 whose mode bits are no mode.
  Such a value is not a `-32602`: that code is for a text that is no number, and design 3.5 is brought to say
  so (section 7; as it stands its `-32602` row, "misaligned or out-of-range address", reads as if an odd PC
  were one). A register reads back what was **stored**: SR, FPSCR and the ARM7's CPSR store under a mask
  (H6-33).
  The value: ASCII blanks around it are dropped; then `0x` or `0X` and one or more hex digits of
  either case, or one or more decimal digits; nothing else (no sign, no empty text); any length, of which the
  low `width` bits count. `state.editWatch` is the same from "width 0" on; an unknown `watchId` is `-32602`.
  *Rejected:* exact matching (the mock's) and a value check in front of the lifetime.
- **M2-D21. `state.writeMemory`.** A range is refused whole (`-32013`) when any byte of it is not writable,
  and when it would pass the end of the address space. `target: "dsp"` is the in-band `-32004`, as a read of
  that target is `-32004`: one of the method's own errors, behind the lifetime, so the schema of the params
  takes the three targets (3.1 item 1; the two-valued enum of today would make it `-32602`). The discard is
  per page of `REI_PAGE_SIZE`, not "4 KiB". In a build with the
  recompiler and without the virtual memory layout the writer removes the page protection itself. *Rejected:*
  wrapping; `-32602` for `dsp`; refusing main-memory writes in that build (v2's rule, which header revision 4
  took back).
- **M2-D22. Writes and a reset are served while the target runs,** as the call class gives it (a service
  excursion). The UI offers register and memory editing while paused only; reset in both states. Design 3.3's
  list of what a running target is asked gains the three writes and the reset (section 7), and the runner
  asks it with two refusals (3.1 item 3). *Rejected:*
  `-32003` for a write to a running target.
- **M2-D23. `control.reset` sends no tick of its own.** The stop of a reset while paused is the listener's;
  a reset while running publishes nothing.

**The tables**

- **M2-D24. Normative of the M2 rows** are the paths, the parents, the two new actions, and that a node whose
  registers are M2's carries at least one (3.6.1). The lists are the server's. The `tree` part of S16 is
  applicable when `dc.sh4.cpu` offers `sh4-registers`: a server claims the M2 tree by offering the M2 panel.
  **Each of the twelve register-panel ids is offered by at most one node**, which the `tree` part holds: a
  panel is bound to the node that offers its id (M2-D39).
  *Rejected:* a capability string for a tree; a check under a milestone gate only.
- **M2-D25. The 115 system bus rows** go to `dc.holly.dmac` (ch2, sort, G1 and G2 DMA with their counters:
  60), `dc.holly.sb.g1` (11), `.g2` (6), `.pvr` (the whole PVR interface block, its DMA included: 11), `.misc`
  (15) and `dc.maple` (12). *Rejected:* the PVR DMA in `dc.holly.dmac` — the design's list for that node does
  not name it, and `dc.holly.sb.pvr` would be left with one write-only register.
- **M2-D26. The PVR registers are split by the prefix of their name** (`TA_`, `SPG_`, the rest). The fog
  table, the object-list pointers and the palette are not rows: the memory view shows them.
- **M2-D27. Channel and common registers are rows per field,** as design 2.8 says, with a budget: a full tick
  with empty tables is at most 192 KiB and its build time is reported. If the measurement of E7b breaks the
  budget, or the build takes more than 5 ms on the quiet machine, the 64 channel nodes carry their 18 words
  instead and the report says so; nothing else changes with that (M2-Q2). The state of a channel that no
  register holds is not in the M2 tree. *Rejected:* word rows from the start — what a person debugging a
  sound driver wants to read is `KYONB` and `AR`, not `0xC01F`.
- **M2-D28. A write-only register is a row that reads `"n/a"`.** On the system bus, and the same for the
  AICA: `SCILV0`, `SCILV1`, `SCILV2` (write-only on the console; `DebugReadRegs` gives 0 for them whatever
  the guest wrote, and a stored 0 cannot be told from a stored 2) and `SCIRE`, `MCIRE` (which store nothing)
  have `kRegNoReader`. The words at `0x2D00` / `0x2D04` of the AICA's file are not rows; the level and the
  service in progress of the ARM7's interrupt controller are, as `L` and `M`, read from the sound CPU
  (3.6.1). *Rejected:* the stored value — no guest read gives it, the memory view of the same address says
  "unreadable", and it needs a reader past the accessor; rows "as the SH4 reads them" for the three level
  registers, which the first cut had: they are the registers M2-Q3 is about, and they would read 0 always.
- **M2-D29. Read-only system bus slots that the emulator never assigns are made to read 0** (they hold the
  low half of a host function pointer: `devices-m2.md` 3.2, read not run). E7b confirms it by one run, fixes
  it in a commit of its own and holds the state hashes. *Rejected:* showing a host address in the register
  tree; it is also the one value in the machine that differs from run to run.
- **M2-D30. `dc.maple` is the twelve `SB_M*` registers, `dc.gdrom` the snapshot that exists, `dc.aica.rtc`
  the seconds and the write enable, `dc.holly.spg` the nine `SPG_*` registers.** No accessor is added to an
  `hw/` header in this round. *Rejected:* snapshots of the maple devices and of the SPG's counters now.

**The guest and the lines**

- **M2-D31. The guest is bare metal** (section 5). *Rejected:* "a KOS program", as design 5.5, 5.6 and 6.3
  say.
- **M2-D32. "Not the hash of silence" is a test of the runner:** `samples` is none of
  `basis * prime^(128 k)`, `k = 0 .. N`. The harness line stays as 5.6 has it. *Rejected:* a count of non-zero
  pairs as a new field; a comparison with the one batch value, which a silent per-sample run passes.
- **M2-D33. N is 2000 for the guest and stays 40000 for 2ndmix; no line is stored in the tree;** a before /
  after is two builds compared by whoever holds the gate.
- **M2-D34. The M2 gate has a second contract run, against the guest:** `--elf dsptest.elf --milestone M2
  --only S00,S16`. Under reios the ARM7 of 2ndmix takes no FIQ (by reading: M2-Q3), so the first run cannot
  show a step that ends at a vector or a breakpoint in a handler; and at the anchor of the second run the ARM7
  is still held, which gives the gate the `-32012` half. So that this is shown and not hoped for, the `arm7`
  part says in notes what it met, and the second-run targets require two of them: `arm7: held at the start`
  and `arm7: bl stepped over and out` (the guest has a `bl`; 2ndmix's waiting loop has none). The
  requirement is the runner's own option `--require-note` (M2-D52), and the `bl` rule has run on the mock
  before it meets the guest (M2-D53). **The rule and the guest are fitted to each other before either
  exists:** each of the rule's two arrivals has 96 steps of its own, and the guest bounds the way to its
  next `bl` (32 instructions) and the length of its FIQ handler (48), which a search may walk through
  (3.1 item 3, 5.2). **And every package that delivers a part of S16 runs it against the guest in its own
  gate** (`debugger.contract.m2arm7.guest` in E6b, `.m2writes.guest` in E7a): what E7b's second run adds is
  the `tree` part. *Rejected:*
  trusting 2ndmix for the ARM7 part; a `-32012` rule that passes when its branch never ran; one budget of 64
  steps for both arrivals beside a guest bounded only as "40 to 80 instructions" — a correct server could
  fail the required note in a package that may touch neither the runner nor the guest; the write parts
  meeting the guest for the first time in the gate of E7b, which owns no writer.
- **M2-D35. E8 retakes the baseline as its first step** when it changes the guest.

**The contract and the UI**

- **M2-D36. F3 is four packages and the contract part goes first; the mock becomes an M2 server and `check`
  gates M2.** *Rejected:* F3 as one package behind "emulator M2" — the runner would change while E6b and E7
  are verified against it, and S16's `writeMemory` and `reset` parts would meet their first server in an
  emulator package's gate.
- **M2-D37. `--parts <scenario>:<part>[+<part>...]`.** Only the named parts of that scenario run; the
  scenario passes when each of them passed; a named part that is not applicable fails; with `--milestone` the
  option is an error. *Rejected:* a package gate that reads the `--json` summary (the same reason makes the
  required notes an option of the runner: M2-D52).
- **M2-D38. What the runner gains for M2** is the list of 3.1 item 3. Left out on purpose: anything of the
  M2 register lists. (The tick that a write causes on a second client was left out by the first two cuts
  with "S13 holds the mechanism"; S13 has no write, and it gains one.) Held by
  a named selftest and its mutation instead of the runner, because the runner cannot see them without
  running guest code out of sequence or writing into a running guest: compiled blocks discarded by a
  main-memory write (`writes.mem.cached` under `Dynarec.SmcCheckLevel=-1`: M2-D48), the accepted half of a
  write served while the target runs (`core.writes`; the refused half is the runner's, which needs no write
  into a running guest), a reset while running that really resets, and a parked reset that really resets the
  SH4 (`core.reset`), the interleaving of the two views of video memory (`writes.mem.vram`; that one view
  is writable through its addresses is the runner's), the FP mode of compiled code behind an
  FPSCR write (`writes.reg.fpscr`), the block a resume enters behind a PC written into a mirror
  (`writes.reg.pc`), the hand-over of the ARM7 breakpoint set (`core.tables`), and a resume
  from an ARM7 breakpoint stop that stops again at once with nothing executed (`arm7.bp.resume`: in a hot
  loop a correct server also answers a resume with a paused tick, a new `stopId` and no running tick, so the
  runner's hit, toggle, mute and twin rules cannot tell). **The third review found six more broken servers
  that both runs passed, and five are rules of the runner now** (3.1 item 3): an ARM7 step from a
  breakpoint stop in which the SH4 executes, or one that lets it run for many AICA periods; a temporary
  breakpoint of the ARM7 that outlives its stop; ARM7 register writes lost while the ARM7 is held; video
  memory, or wave memory through the SH4's address, refused; ARM7 breakpoints compared without the mask of
  the wave memory. Three of them also have their selftest in E6b's gate (`arm7.step.suspended`,
  `arm7.step.frompause`, `arm7.bp.mirror`); the runner holds them at the wire, where a core that answers
  for the emulator side could still be wrong. The sixth, a reset at a breakpoint stop that keeps
  `breakpointId`, is left to invariant I7 where it occurs and to `core.reset`: the part resets at a pause
  stop. **The reset rule brings its own tables** (a disabled breakpoint, a watch, a flipped category), since
  nothing is listed when the part runs, and **S16 ends with a machine that has run on from its reset and is
  paused**: no other run of the gate resumes a machine behind a parked reset. What the scenarios of M3 need
  behind S16 is for the cut of M3.
- **M2-D39. The points 4.4 left open.** A register panel shows the node whose `actions` name its id (the
  first in the order of the describe tree if several do; an M2 server has exactly one: M2-D24), and a
  notice when no node offers it; the M2 tables that have no panel id are read in the device tree. The two new
  panels stand in the centre group of their processor's workspace, behind the breakpoints panel (3.8).
  Editors exist only for a register with `flags.writable`, the watch editor included, and all three editors
  are offered while paused only; the flag is read from the describe tree, since ticks carry none, and a
  watch finds its register by its expression (3.8). The focused target is that of the panel of a target (disassembly, memory,
  breakpoints, callstack, registers) that last had the focus, at first the workspace's own — `sh4` for
  `sh4-debugger` and `mixed-mode-debugger`, `arm7` for `arm7-debugger`, `dsp` for `audio-debugger`; the
  toolbar says which it is and the views keep their own step buttons, which are off with the toolbar's while
  the ARM7 is held. The function keys are handled on workspace pages wherever the focus is, but for Monaco
  and an open dialog; a held key is not repeated. Reset has no confirmation and is shown, off, with its
  reason, on a server without the capability. After its own memory write, register write or reset the client
  reads its fetched views again in place (a second counter beside `refresh`: the write actions advance it in
  F3c, the reset action in F3d). The views that follow the PC are moved by no action: they move when a
  paused tick of the stored stop shows another PC, whoever wrote it and in whichever order response and tick
  arrive, and at the new stop of a paused reset (3.8). Other clients learn nothing more than the tick. A server that
  answers describe without `protocol` gets the banner and is treated as listing no target and no capability;
  what the client does for each absent v3 field is the table of 3.7. *Rejected for the keys:* 4.4's "outside
  an input" — F5 in an address box would reload the page.
- **M2-D40. The UI is run against the emulator by a spec** (`e2e/emulator-smoke.spec.ts`), not by `check`:
  first in the gate of F3d, which writes it and therefore stands behind E7b, then in M2G. *Rejected:* a
  checklist by hand; a gate in every `check`; a spec whose first run is the integrator's gate.

**Added by the review of round 1**

- **M2-D41. An FPSCR write by the debugger that changes PR, SZ or the rounding mode empties the code cache of
  the SH4** (H6-33). The recompiler bakes those three into a block when it compiles it, finds blocks by
  address alone and has no entry check for the FP mode (design 2.3.7 item 2). Without the reset the register
  shows one mode, a step runs in it (a temporary block is decoded with the current FPSCR, and so is the owed
  rest of a block behind a breakpoint stop) and a resume runs
  the old blocks in the other: the debugger would make a correct run wrong. `SuperH4::ResetCache()` is legal
  in the park context, and J1 holds an empty cache neutral. Its test takes its witness from a block entered
  from the cache (M2-D48). R6 writes the sentence without waiting for M2-Q4 (M2-D54). *Rejected:* the limit
  stated in the help (M2-Q4
  asks); a reset at every FPSCR write, which the three bits make unnecessary. The guest's own limit, a
  program that runs one address in two FP modes, is untouched and stays PLAN T3.4's.
- **M2-D42. What a write of the SH4's PC, or of code, may do.** A PC with bit 0 set is refused (`-32013`):
  nothing executes at an odd address, the block cache is indexed by `pc >> 1`, so a resume from one would run
  or compile the code of the even address under it, and `StepCheck` refuses every step from there. Anything
  else is accepted, also when it takes the guest to a word that is no instruction: in the default build
  (`NO_MMU`) that ends the emulator, as it does for a guest that gets there by itself, and the UI help says
  so. **An accepted write that moves the PC empties the code cache of the SH4** (H6-33; added by the third
  review). There is a third outcome beside "accepted" and "no instruction", of the kind M2-D41 was written
  against: the recompiler finds a block by `(pc >> 1)` under `FPCB_MASK`, 32 MiB of address, with no address
  in the key, so all addresses equal modulo `0x02000000` share a slot — main memory at `0x0C…`, `0x8C…` and
  `0xAC…` among themselves (not the mirror 16 MiB up, `0x8D…`: the same memory in other slots, M2-D68),
  and the boot ROM against main memory (the emulator's own list of soft-reset addresses exists
  for the latter). Behind a PC typed as `0C010000` for `8C010000`, or set to `0xA0000000` to start over, a
  step decodes what is at the written PC, while a resume enters the cached block of the other address: its
  branch targets and the PR of its calls are the other mirror's, and against the boot ROM it is other code;
  the block's own check compares the bytes at the address it was compiled for and sees nothing. With the
  cache empty every block is compiled at the address the guest reaches it by. The call is
  `SuperH4::ResetCache()`, legal in the park context and held neutral by J1; a write of the value the PC
  has resets nothing. A guest that changes mirror by itself is as before (1.2; design 2.3.7 item 6).
  *Rejected:* `emu::Resume` answering `-32005` when the instruction at the PC cannot execute — it narrows
  the case by one instruction and cannot close it (M2-Q5 asks). *Also rejected:* the reset only when the
  slot of the written PC holds a block of another address (`bm_GetBlock(pc)->addr != pc`), as the review
  proposed it — it is right for the first block and no further: the x64 dispatcher looks every block up, so
  with the slot of the written PC empty (a PC written into the middle of a block) the first block is
  compiled at the written address and its branch finds the other mirror's block in the next slot; and it
  asks the block manager from `dbg_regs.cpp`, where a build without a recompiler, or one whose selected
  backend is the interpreter, has nothing to ask. The limit stated in the help instead of the reset is what
  a "no" to M2-Q4 gives.
- **M2-D43. The witness of the debugger in use is two things, and a test says what it did.** Each neutrality
  target holds the sound line at N = 2000 and the exit state hash at 90 vblanks against the plain run; the
  guest folds what its ARM7 observes into registers (5.2), so that the line sees the order inside a burst —
  every FIQ entry to the instruction (the handler folds its `lr` and `SPSR`), whether it is made at the
  first instruction of a sample or inside one, behind the F window of the loop (M2-D59); a time step to the
  iteration of the loop (which clears `SAMPLE_DONE` behind its fold) —;
  and each test prints `ok` lines its target requires, so that a test that armed nothing, took no stop, or
  made its 200 stops within two bursts is not green. *Rejected:* the line alone, with no field for wave
  memory, main memory or the scheduler; targets whose pass says only that the process ended.
- **M2-D44. The UI checkout an emulator gate runs with is pinned** to the last verified UI head (section 4).
  The pin is the worktree `agents/nulldc3/wt/ui-pin`, and no directory beside the emulator's worktrees is
  named `dreamcast-devtools`: that name is a default look-up of the emulator's configure (for a bundle) and
  of the tests project (for the checkout). An emulator carries the UI only where a gate says so, and then
  by `-DNULLDC_DEVTOOLS_DIST` given to its own configure (A.0, 3.9, 3.10).
  *Rejected:* "the head of the branch", which can be a UI package half done and makes the runner move under
  the emulator packages, the situation M2-D36 was written against; the pin "under `agents/nulldc3/wt/`"
  without a name, passed "as `NULLDC_DEVTOOLS_DIST`" — the option takes a `dist-native` directory, and given
  to the tests project it replaces the bundle of `tests/debugger/net`.
- **M2-D45. The mock has the effects the write rules look at, and a profile of the M2 emulator.** The three
  effects (the `RB` exchange, the step from a PC outside its ring, the written `mov #imm,Rn` that executes)
  make every runner rule run in `npm run check`; `MOCK_M2_SUPPORT` makes the UI packages test against a
  server that writes 109 registers of two nodes, steps two targets and captures nothing, which is what the
  emulator of M2 is. *Rejected:* three rules bound to the emulator profile, which would meet their first
  server in E7a's gate, in a repository E7a must not edit; UI specs against the full mock only, whose
  editors on the ten module panels and on the M2 rows do not exist on the emulator.
- **M2-D46. A gate is "none skipped".** The tests project is configured with every data path and with
  `-DNULLDC_TESTS_REQUIRE_DATA=ON` (A.0), and a report states the number of targets. In a worktree under
  `agents/nulldc3/wt/` every default look-up misses — the two vector submodules among them, which a new
  worktree has empty —, and a skipped test counts as passed for ctest: the new
  targets of the round would be exactly the skipped ones. One look-up must go on missing: the emulator's own
  configure embeds a UI bundle it finds beside its tree, so the emulators of a package gate embed none, and
  the pin has a name that look-up does not find (M2-D44).
- **M2-D47. The generic host is the fifth build of every emulator package** (compiles; 2ndmix state hash
  unchanged), and `-DTARGET_NO_REC=ON` and the generic host run the memory-write tests of E7a: they are two
  of the four combinations H6-35 describes, and PLAN's ground rule keeps that path buildable. *Rejected:* the
  first cut's "only the first runs on this machine".

**Added by the review of round 2**

- **M2-D48. A witness of the SH4's block cache is taken from code the dispatcher enters from the cache, and
  the discard of a main-memory write has a registration of its own.** A step, and a resume behind a
  breakpoint stop, decode what they run from memory in the FPSCR of the moment (the owed rest of a block is
  a temporary block), and arming an SH4 breakpoint discards the blocks of its page: tests written to the
  words "followed by a resume" would pass with no discard and with no cache reset. And in the default
  configuration a store without the discard cannot be seen at all — the fault handler, or the block's own
  check, makes it good —, so `writes.mem.cached` also runs as `debugger.selftest.writes.smc`, with
  `Dynarec.SmcCheckLevel=-1` (3.5). **And the witness of the FP mode has a direction** (third review): only
  a block compiled with PR = 0 has an FP mode in its code — with PR set every FP instruction is a call of
  the interpreter's handler, which reads FPSCR when it runs —, so the witnessing block is compiled with PR
  and SZ clear, the guest's FPSCR, and the writes that are witnessed set them; the other direction passes
  with no cache reset. The mirror case of `writes.reg.pc` is taken the same way, behind a stop that owes no
  remainder, in a mirror that shares its slots and on a loop that goes back PC-relative (M2-D68).
  *Rejected:* the value 0 the review gave for that key: 0 is `FullCheck`,
  the default, and `NoCheck` is -1 (`types.h`). *Also rejected:* leaving the discard to reading: it is the
  headline risk of E7a.
- **M2-D49. A test that the run ends is never named under the prefix of a session** (`writesrun.neutral`,
  `arm7n.*`; 3.0). The filter of a session selects by prefix, runs in name order and waits for its last test
  to return. *Rejected:* `writes.neutral` "not part of the session `writes`", which a name cannot say to the
  driver.
- **M2-D50. The host cost of the burst change is held by two checks** (3.4): the compiled code of `StepMany`
  and `Step` is what it was (nothing per ARM7 instruction), and the instructions of a whole run grow by at
  most 48 per sample. Speed by CPU time is reported against the 1.5 % spread of `tests/perf/BASELINE.md` and
  decides nothing by itself. *Rejected:* one count for both — a host instruction per ARM7 instruction costs
  about what the per-sample overhead is allowed to cost; "within 1 %", which this machine cannot decide (the
  medians of J1's own measurement moved by +1.8 % and -1.5 %).
- **M2-D51. The session `arm7` also runs with `aica.NoBatch=yes`** (a third registration line; the
  verifier's stress once). The default setting is the batch path that PLAN removes, and every functional
  gate of the burst machine ran on it alone. The package that wires the T5.0 core removes the batch halves
  (5.6). *Rejected:* the contract targets in both settings as well: they test the wire, and the neutrality
  targets already run in both.
- **M2-D52. `--require-note` and `--mock-support` are options of the runner, delivered by F3a;** the three
  notes of the `arm7` part are verbatim and pinned by a runner test. *Rejected:* `contract.py` reading notes
  out of the runner's output — a channel nothing freezes, and what M2-D37 rejected for package gates; a
  second mock process for `check`.
- **M2-D53. The mock has stores, and its ARM7 program has a call.** Main memory 16 MiB; wave memory 8 MiB, so
  that the ARM7 ring at `0x00200010` stays where the e2e suite pins it; video memory in two views that are
  not aliased; the ARM7 key `address & 0x007FFFFC`; a `bl` to a leaf in the ring, with step-over and
  step-out as temporary breakpoints. Without them the overlay "keyed by store and offset", the mirror rule
  and the `bl` rule could not be implemented or run. *Rejected:* a wave memory of 2 MiB in the mock (the
  ring would be a mirror, and every pinned address would move); a `bl` rule that meets its first server in
  E6b's gate.
- **M2-D54. R6 does not wait for an answer of the owner.** It writes H6-33 as printed. A "no" to M2-Q4
  before E7a starts is a correction of revision 6 by the integrator, comment only (6a); behind E7a's merge
  it is revision 7 with a fix round of E7a. **No package waits for any answer, and each question has the
  moment up to which a "no" is cheap** (section 4; 6.2 names what it costs later): M2-Q1 before F3a builds
  the M2 profile of the mock and R6 freezes H6-33 and H6-39, M2-Q5 before R6 freezes H6-23, M2-Q4 before
  E7a, M2-Q2 before E7b. *Rejected:* "before the first commit: the answer", which the
  second cut had in A.2 against its own 6.2; "the others at any time", which the third cut had for three
  questions whose "no" changes frozen text or merged packages.
- **M2-D55. The runner's large write puts back what it read:** 65536 bytes of main memory at the SH4's
  `defaultMemoryAddress`. *Rejected:* a pattern, which a part that fails half way cannot take back; the
  part's scratch address, from which 64 KiB run through the live stack and, with the stack at the top of
  main memory, into the next mirror of the store.
- **M2-D56. `engines` compares the fields E6a's report shows equal,** and a difference in `samples`, `regs`
  or `arm7` stops E6a before its soak, for the integrator's decision (3.3 item 3). *Rejected:* a gate that
  demands five equal fields beside a risk that says the baseline does not depend on them.
- **M2-D57. The `arm_on` latch is held by reading in M2,** and two mutations of the burst leave the
  verifier's list as invisible (3.4). *Rejected:* a selftest that calls `SoundCPU::SetResetState` inside a
  suspended burst to make the latch observable: it would pin, by a state that no run and no debugger call
  of M2 produces, a rule that the cut of M3 decides with its AICA writer.
- **M2-D58. The guest stores into no page of its code, and writes no `CCR`** (5.2). The code-write tests of
  E7a start from a known page state, `writes.mem.cached` is not defeated by a cache reset of the guest's
  own, and the one build that cannot resolve a fault in a code page can still run this guest. *Rejected:*
  leaving where the counters live to the engineer.

**Added by the review of round 3**

- **M2-D59. The guest's ARM7 loop masks F around a part of each pass (the F window), so that a FIQ is also
  entered inside a sample.** On the guest as the third cut had it a request is raised only by a time step,
  between two samples, and every entry is made by the first FIQ test of a sample: a debug loop that looks
  at the pending FIQ once per sample, or not at all when it goes on with a suspended sample, would pass
  every gate of E6b, and on a guest with a critical section it moves ARM7 timing whenever a breakpoint is
  active — the neutrality the round is held to. With the window a request can wait and be entered behind
  an `msr` (5.2). E6a counts the entries of that kind before the baseline, by the number of the FIQ test
  of their sample that made each (at least 100 by the third or a later test in 2000 ticks: M2-D66);
  `steps` has a fourth kind of step for them, the session a test (`arm7.step.fiqbehind`), the verifier
  three mutations (3.4). How an entry of that kind is recognised, by E6a's count and by the two tests, is
  M2-D66. *Rejected:* the guest as written, with "a FIQ that becomes pending inside a sample
  is exercised by no gate of M2 and is held by reading" in 6.3 — E6a has not started, the change is two
  instructions, and the sentence would stand beside the one rule every package is held to.
- **M2-D60. The two halves of `Step` have suites of their own.** `arm7m_step()` runs `StepIrq` and
  `StepExec` under `ARM7M_STEP=halves`, and the six ARM7 suites are registered a second time with it (3.4).
  In the shape 2.3 prefers, `Step` keeps its source and nothing else that exists enters the halves; the
  plain suites, named by the third cut as "the gate of the `Step` split", would then be the gate of
  nothing. *Rejected:* an option of the three test programs — their mains are not E6b's; trusting the 40
  to 100 instructions of the guest for the third inclusion of the interpreter's body.
- **M2-D61. The burst machine counts what it does, in `VirtualDreamcast::SoundBurstCounts`** (2.3): bursts
  started, ARM phases completed, time steps, tails; in every build, in no save state and no hash. The
  tests that count calls per burst (`arm7.burst.plain`, the tail "once", `idlebp`'s 32 checks a burst)
  count calls made in emulator code, which may include no debugger header but the frozen `debugger.h`.
  *Rejected:* the counts in `dbg_arm7_private.h`, which emulator code cannot reach; counters under
  `NULLDC_DEBUGGER` only, a second shape of the changed path for four increments; the timers of the AICA
  as a proxy, which show the time steps and neither the ARM phases nor the tail.
- **M2-D62. What a neutrality test of the writes may write.** At stops latched at a slice end: main memory,
  a code page, a general register, SR and FPSCR of the SH4, R0 to R12 of the ARM7. Not the PC of either
  processor. Not SR at a code breakpoint of the SH4: `UpdateSR()` answers whether an interrupt is pending
  and let in now, not whether the write made it so, and the acceptance then moves from the next slice end
  to the leave, as behind the guest's own `LDC` (H6-33). Not the CPSR of the ARM7: the mode switch rewrites
  bank slots that only the save state shows, and in user or system mode the live SPSR, which the row SPSR
  shows as well (H6-38, M2-D67). *Rejected:* "10,000 writes with their restore and the
  state hash equal" with no guest, no stop kind and no registers named — it reports a non-neutral writer
  that is correct.
- **M2-D63. H6-19: the last bullet and the "Returns" paragraph of the `Park()` comment are part of
  revision 6.** The owed resume calls `dbg::arm7::PrepareResume()` too, and a step of the ARM7 that waits
  returns `PARK_RUN` with `g_parked` set. *Rejected:* leaving them ("the first, the last and the Returns
  paragraphs stay"): after R6 the header is frozen, and E6b would build behaviour its comment does not
  have, or the fix would cost a revision 7.
- **M2-D64. Until M2G, where this file and the design differ, this file is the contract.** Section 7 lists
  every such place; a design section a brief names is read with its rows of section 7; a departure to
  report is a departure from this file; `dbg_arm7.h` is frozen by this file from R6's merge on, and joins
  the list of the design's Appendix B at M2G. *Rejected:* applying section 7 to the design before F3a
  starts — the rows describe what the packages still have to build and their verifiers to confirm, a fix
  round would then edit two documents, and the design would call M2 built while it is not.
- **M2-D65. The emulator repository holds a copy of this file** (`docs/nulldc3/devtools-integration-m2-round.md`):
  R6 commits the first one with the header revision, M2G the last, with the other copies it updates
  (`PLAN.md` and the open-questions document among them: 3.10 item 5). *Rejected:* a design, a decisions
  file and a frozen header that cite a file of the working area only.

**Added by the review of round 4**

- **M2-D66. A FIQ entry inside a sample is recognised by the FIQ test that made it, never by its return
  address or by where a step ended; and E6a's gate is on the entries E6b's tests can count** (the second
  half by the review of round 5). E6a's scratch build prints, for every entry, the number of the `Step` of
  its `StepMany` call whose FIQ test made it: 1 is the first kind; 2 the second kind with the `msr` of
  `a_fwin_end` as the first instruction of its sample; 3 or more the second kind with an instruction of
  the sample in front of that `msr`. Its gate asks for at least 100 of the last in 2000 ticks and for no
  stretch of more than 32 ticks without one, half the 64 bursts of `arm7.step.fiqbehind` (3.3, 5.2).
  E6b's two tests take a step of the `msr` at `a_fwin_end` that ends at `0x1C` for
  such an entry only when no ARM phase ended in it or in the step that arrived on the `msr` (`out[1]` of
  `SoundBurstCounts()` unchanged over both) — an entry of the third class, and of no other: under steps an
  `msr` that starts its sample is stepped in a new phase —; `arm7.step.fiqbehind` breaks on the
  instruction in front of
  the `msr` and steps twice for that (3.4). The reasons are two facts of the core. `CPUFiq` stores the next
  PC + 4, so an entry by the first test of a sample that starts behind the `msr` has the return address of
  an entry behind the `msr`, `a_fwin_end` + 8 — 150 to 200 such entries in a run of 2000 ticks, and a step
  of the `msr` that reaches `0x1C` through a new phase about once in ten stepped bursts. And `StepMany`
  starts every sample at 0 ticks, so where the loop stands at an overflowing time step is the orbit of a
  deterministic map and no uniform phase. *Rejected:* the count by return address, which reads "at least
  100" with no
  entry inside a sample at all; any step of the `msr` that ends at `0x1C`, under which the two tests
  stayed green with the two debug-loop mutants they are named for; reading the burst's `ticks_done` from
  the test, which is state of `Dreamcast_impl` that no header gives the debugger; the count of the second
  kind as a whole, tied to the two tests by an expected share ("four in five", the fifth cut) — a guest
  whose window is only met in front of the `msr` passes it with no entry the tests count, the two tests
  then fail a correct burst machine, and the package they are in may not touch the guest.
- **M2-D67. The writer of the ARM7's CPSR is the core's mode switch, called as the guest's `MSR` calls it
  (`saveState` false), also for what the switch does to the live SPSR** (made exact by the review of
  round 5). Leaving user or system mode, `CPUSwitchMode` copies the CPSR into `regs[RN_SPSR]`, which
  the row SPSR reads in every mode; entering a mode that has an SPSR, it loads that mode's slot over it.
  So behind a CPSR write made in one of the two modes the row SPSR reads the CPSR as it was before the
  write when the mode written is one of the two as well — also for a write of the value it has —, and
  what the row `SPSR_<mode>` of the mode written read otherwise, which it keeps when the CPSR is written
  back (the table under H6-38 has the four cases). H6-38 says so; `arm7.setreg.*` holds both cases, making
  both writes from system mode itself; a test that compares rows around a CPSR write holds SPSR to those
  values and asserts no "changed" (3.4, 3.5).
  Which mode the guest's ARM7 loop runs in stays E6a's choice: system, or a mode with an SPSR (5.2).
  *Rejected:* "leaves every register as it
  read", which the fourth cut had in the text to be frozen; "RN_SPSR then reads the CPSR as it was" for
  every write made in the two modes, which the fifth had — it holds only while the mode written is one of
  the two, no writer built on the core's switch makes it and the sentence behind it both true, and one
  made to fit it (`saveState` true) overwrites the SPSR of the mode it enters; a writer that puts the live
  SPSR back when it
  leaves one of the two modes — a second behaviour beside the guest's own `MSR`, for a register that has
  no meaning there, and it would not make the write neutral (the slots of R13 and R14 are still
  rewritten).
- **M2-D68. The guest's SH4 loop goes back by a PC-relative branch, and the mirror case of `writes.reg.pc`
  uses a mirror that shares its slots.** `g_main_loop` ends in `bra` and has no `jmp`, `jsr` or `rts`
  (5.2; E6a's verifier checks it by disassembly), so a PC written into `0x0C…` stays there with the cache
  reset and falls back to `0x8C…` without it. The mirror is a multiple of 32 MiB away (`0x0C…`, `0xAC…`):
  the mask of the block cache is 32 MiB and main memory 16 MiB, so `0x8D…` is the same memory in other
  slots, and a case written with it passes with no reset. "The mirrors of main memory share their slots"
  is said exactly wherever it stood (H6-33, 1.2, M2-D42, section 7 row 69). *Rejected:* leaving the way
  back to E6a — the ARM7 side of the same guest is told to preload every address into registers, a
  `jmp @Rn` on such an address returns to `0x8C…` after one pass, and E7a, whose case then fails with a
  correct writer, may not touch the guest.

**The review of round 5** added no decision and made two exact, each in its own text above: M2-D66 (the
gate of E6a's count is on the entries the third or a later FIQ test of their sample makes, with a longest
stretch) and M2-D67 (what the row SPSR reads behind a CPSR write when the mode written has an SPSR; the
writer passes `saveState` false). M2-D59 follows M2-D66 in one clause.

### 6.2 Questions for the project owner

**Answered on 2026-10-11: the project owner accepts every recommendation.** M2-Q1: yes, M2 writes only the registers
of the two processors and the device tables of M2 are read-only (the write table is M3 / M4 work). M2-Q2: yes, a
paused tick of about 130 KiB is acceptable, with the budget and the fallback of M2-D27. M2-Q3: not in this round
(recorded for PLAN tracks 5 and 7, with a console check). M2-Q4: yes, a debugger write of FPSCR, and one that moves
the SH4's PC, may empty the SH4 code cache. M2-Q5: yes, a debugger write can end the emulator, stated in the help;
only an odd PC is refused. M2-Q6: not in this round (E7a's verifier builds that configuration once; the result goes
to PLAN's backlog). The "if the answer is no" branches below are therefore not taken; the text is kept as the
record of what was asked.

Work proceeds on each recommendation; an answer the other way changes what is named. Each question says up
to when a "no" is cheap (section 4 has the same dates in the order of the packages). The reviews of rounds 4
and 5 added none: M2-D66 to M2-D68, and what round 5 made exact in the first two, decide how a test
recognises what it tests and what a frozen comment says
about a writer that was decided before; none changes what the debugger does to a guest.

- **M2-Q1. Is it right that M2 writes only the registers of the two processors, and that the device tables
  of M2 are read-only?** The milestone says "memory and register writes" and "full register tables with the
  generic register panel"; 4.4 says "edit through `state.setRegister`". *Recommendation:* yes (M2-D18). A
  debugger write to a device register is either the guest's write, which for some thirty registers starts a
  DMA inside the call, raises or drops an interrupt, resets something or ends the process, or a plain store,
  which leaves the device's derived state behind; which it is has to be said per register, and the AICA's
  writer also decides whether the debugger may hold the ARM7. That table is proposed for M3 (AICA) and M4
  (Holly, SH4 modules). *If the answer is no:* E7b grows a write table and accessors
  (`AICA::DebugWriteReg`'s body, `SystemBus::DebugWrite`), with a design step before it. What else changes
  depends on when. The recommendation is built into four places: F3a's `MOCK_M2_SUPPORT` (writable flags on
  two nodes only) with the blocks of F3c and F3d that are gated against it; the frozen sentences "No other
  row has a writer" (H6-33) and "in M2 the debugger writes no register of the AICA" (H6-39); and E7a's 109
  flags with `writes.reg.readonly`. *Wanted:* before F3a builds the profile, at the latest before R6 freezes
  the two sentences. Behind R6 a "no" is header revision 7 (H6-33, H6-39) with a fix round of F3a and of
  whichever of E7a, F3c and F3d is merged; R6's declarations, E6a and E6b do not change.
- **M2-Q2. Is a paused tick of about 130 KiB acceptable?** With the 64 channels by field a full tick carries
  about 3,000 registers, and every stop, step and write sends one. *Recommendation:* yes, with the budget and
  the fallback of M2-D27; the UI is measured for that many, and an on-demand way to read one node belongs to
  the protocol work of PLAN track 10, where settings need it too. *If the answer is no:* E7b builds the
  channels as words from the start (1,152 rows for them, about 80 KiB a tick), and 3.6.1 says 18 rows a
  channel. *Wanted:* before E7b starts; its tables and their tests follow the answer from the first line.
- **M2-Q3. Should reios leave the AICA's interrupt registers as the retail boot ROM does?** Outside M2, and
  only the owner can say what the boot ROM leaves: under reios 2ndmix and every program on the stock
  KallistiOS driver are silent, because their ARM7 code relies on timer A being enabled at level 2 and reios
  writes no AICA register (inferred from reading; E6a's first run confirms or refutes it). *Recommendation:*
  not in this round; recorded for PLAN tracks 5 and 7 under "needs the console". M2 does not depend on it:
  the guest sets what it needs. A fix would make 2ndmix a sounding workload and move its lines, so it lands
  with a new baseline.
- **M2-Q4. May a debugger write of FPSCR, and one that moves the PC of the SH4, empty the SH4's code
  cache?** Blocks are compiled for the FP mode of the moment and found by address alone, so after a write
  that flips PR or SZ a step would run in the new mode and a resume in the old one. And blocks are found
  under a mask of 32 MiB with no address in the key, so after a PC written into another mirror of main
  memory (`0C010000` typed for `8C010000`), or onto a boot ROM address that shares its slot with compiled
  code of main memory, a step would decode what is at the PC and a resume would run the block of the other
  address. *Recommendation:* yes to both (M2-D41, M2-D42): one cache reset in the park context, when PR, SZ
  or the rounding mode changed and when the written PC differs from the PC it replaces; the next blocks
  are compiled again, which costs a moment and no guest-visible state. *If the answer is no:* the limits
  are stated instead — in H6-33, in design 2.3.7 items 2 and 6 and in the UI help ("a resume on the JIT
  keeps the FP mode its blocks were compiled in", "a resume on the JIT from a PC written into another
  mirror runs the code of the mirror it was compiled in") — and the JIT leaves the cache half of
  `writes.reg.fpscr` and the mirror case of `writes.reg.pc`. It is a change of two sentences of the header
  revision. R6 does not wait for it (M2-D54). *Wanted:* before E7a starts, where a "no" costs a comment
  correction and three lines of a brief; behind E7a's merge it costs header revision 7 and a fix round.
- **M2-Q5. Is it acceptable that a debugger write can end the emulator?** In the default build a guest that
  reaches a word that is no instruction ends the process (`die`; PLAN's backlog has it with "unaligned
  access / odd PC are fatal rather than address errors"). A PC written into zeroed memory, or such a word
  written ahead of the PC, followed by a resume, gets there. *Recommendation:* yes, stated in the help
  (M2-D42): the debugger refuses the one value that is wrong by itself (an odd PC), and the rest is what a
  crashing guest does without a debugger; the real fix is the guest-visible exception, which is PLAN's item
  and not this round's. *If the answer is no:* `emu::Resume` and `control.runUntil` answer `-32005` when the
  instruction at the PC cannot execute, knowing that the instruction behind it is not looked at. That is no
  help text: it changes the frozen return list of `emu::Resume()` (H6-23: today 0 or `kErrNotPaused`), its
  body in `dbg_emu.cpp` (E6b's), `control.runUntil` in `dbg_core.cpp` (E7a's), a row of design 3.3, and a
  rule of the runner with the mock's answer (F3a's). *Wanted:* before R6 freezes H6-23. Behind R6 a "no" is
  header revision 7 with a fix round of F3a and of whichever of E6b and E7a is merged. (What F3d's help
  says follows the answer either way and waits for nothing.)
- **M2-Q6. Does the build with an SH4 recompiler and without the virtual memory layout have to run guests?**
  Outside M2, found while the memory writer was cut (by reading, not run). That build write-protects the
  pages of compiled blocks as every build does, and its fault handler cannot resolve a fault in them
  (`bm_LockedWrite` measures the faulting address from a base that is null there): a guest that stores into
  a page from which a block was compiled ends the process, with or without a debugger. No configuration of
  this machine gives that build (`_vmem_reserve` takes the fallback only when the layout cannot be set up),
  and the generic host, which has no layout either, has no recompiler. *Recommendation:* not in this round.
  M2 keeps its writer correct for that build — it removes the protection itself (H6-35) —, the guest of E6a
  is built so that it can run there (M2-D58), and E7a's verifier makes the build once and says what it
  found; the result goes to PLAN's backlog. *If the answer is yes:* a fix in `hw/sh4/dyna/` (the fault
  resolved from the address of main memory when there is no layout) is a package of its own, with its
  state hashes, before or behind M2.

### 6.3 Risks the round carries

- **The ARM7 of 2ndmix as S16's subject.** By reading, it waits in a loop of three instructions of which the
  first two are plain, which is a breakpoint site; and on the way there it is released twice, the first time
  onto a one-word program that runs through the top of the address space, where the PC is above `addressMax`
  and a pause finds no line to ask for. The settling of 3.1 item 3 is for that window, which is widest on
  the interpreter and the forced fallback and which only the package gates meet (the full gate reaches S16
  last). F3a's observation (3.1) says whether both hold, on the three engines, **before the runner is
  closed**. The second run against the guest shows the ARM7 part whatever 2ndmix does; if 2ndmix offers no
  site, the first run of the gate cannot pass as written and the integrator decides the outcome of that case
  before F3a closes.
- **The lines of the guest on the two engines.** They are built to agree but for `sh4`. If E6a finds that they
  do not, 3.3 item 3 has what happens: it stops before its soak, reports the tick at which the lines part,
  and the integrator decides whether the guest changes or the `engines` targets compare fewer fields
  (M2-D56). The baseline does not depend on the agreement; the gate of E6a no longer demands a fixed list of
  equal fields.
- **The size of a tick** (M2-D27, M2-Q2): measured in E7b, with a fallback that is decided here.
- **arm64.** The ARM7 fallback, its notice and the claim that all state of the ARM7 recompiler is in the
  context between two `Run` calls cannot be run on this machine; E6b's verifier reads them.
- **The ARM assembler.** E6a needs one once, to make `arm7.bin`; nothing else of the round does.
- **`g_intc_deferred` behind a burst run on in the park context.** By reading it is set whenever a burst was
  suspended from the scheduler callback, so the interrupt that burst raises is accepted on the way out;
  `arm7.intc.order` pins it (`headers-m2.md` Q11).
- **The stops the neutrality tests place themselves.** They use an event breakpoint on `dc.aica.timer` to get
  a stop behind a burst and to spread their code breakpoints over bursts. By design 2.4 such a stop is neutral
  outside a SLEEP wait, and the guest does not sleep; no gate of M1 has held it on a sounding guest. If such
  a stop moves the line or the hash, E6b reports it as a finding about the merged tree and places its stops
  another way (a pause asked for by a second thread) until it is fixed.
- **The four combinations of the main-memory writer.** Three run here; the recompiler without the virtual
  memory layout runs only in the verifier's scratch build (3.5), and by reading it cannot run a guest that
  stores into a page of its code. The guest of E6a does not (5.2). If the build cannot run it all the same,
  the writer's branch is read, not run, and the finding goes to PLAN's backlog (M2-Q6).
- **The witness of the discard rests on reading.** That a store without `bm_LockedWrite` is made good by
  the fault handler on a protected page and by the block's own check on a data page, and executes stale code
  only with `Dynarec.SmcCheckLevel=-1`, was read and not run; so was the rule that a resume behind a
  breakpoint stop runs the owed rest of its block from memory. E7a's verifier runs both mutations in both
  registrations and reports what each one caught; a reading that turns out wrong changes a test, not the
  writer.
- **Check (a) of the host cost** asks for unchanged compiled code of two functions. If the compiler emits
  them differently in the tip although their source is what it was, E6b reports the difference and the
  integrator accepts it instruction by instruction or not at all.
- **Decided by reading, changed by an answer.** M2-D18, M2-D41 and M2-D42 stand on recommendations (M2-Q1,
  M2-Q4, M2-Q5). No package waits; up to when a "no" is cheap, and what it costs later, is in 6.2 for each
  (M2-D54).
- **The share of FIQ entries inside a sample.** The F window of the guest makes them possible (M2-D59); how
  many there are, and where in their sample, depends on where the samples end in the loop, which nobody
  has run and which is no uniform phase (the orbit of a deterministic map: 5.2). E6a counts them — by
  the number of the FIQ test that made each, since the return address is shared with entries at a sample
  that starts behind the `msr`, and since an entry by the second test of its sample is one E6b's two tests
  cannot count (M2-D66) — and widens the window, before the baseline, if those made by the third or a
  later test are fewer than 100 in 2000 ticks or leave a stretch of more than 32 ticks (5.2). What
  `arm7.step.fiqbehind` and the fourth kind of `steps` need is thereby measured while the guest can still
  change, and no expected share stands between E6a's count and them: by the expected share (one entry in
  five inside a sample, four in five of those counted) the 64 bursts of `arm7.step.fiqbehind` are some
  twenty-five times the distance between two pairs it counts; at the least E6a's gate lets through they
  are twice the longest stretch. What is left: the count is of the plain run, and the tests of the session
  that run in front of `arm7.step.fiqbehind` hold, release and write the ARM7, which starts the orbit
  from another place. A guest on which 64 bursts do not suffice all the same is
  reported by E6b with E6a's three numbers and its longest stretch, not worked around in the test.
- **The `bl` rule against the guest.** The rule's budget and the guest's two bounds are fixed before either
  exists (M2-D34). If E6a cannot keep a bound it reports that before its baseline, and the integrator has
  the budget changed in F3a's file, with the pin moved, while the rule has met no emulator.
- **The cache reset at a PC write.** That an emptied code cache is neutral in the middle of a run rests on
  J1 (every thread start) and on `writesrun.neutral` for the FPSCR write; a PC write is not neutral for
  another reason (the owed remainder) and is in no neutrality test. The reset is a call that exists; what
  it holds, a resume that agrees with a step behind a PC written into a mirror, is one case of
  `writes.reg.pc`, on the JIT.

---

## 7. Changes to the design text this cut requires

`TRACK1-DESIGN.md` is not edited by this cut, and by no package before M2G (3.10 item 5): **until then this
file is the contract wherever a row below changes the design, and a design section a brief names is read
with its rows** (M2-D64). The list is by section; "now" quotes the passage as it stands
(shortened with "..."), "becomes" gives the new text or says what it has to state. Where a section only has to
follow section 2 or 3 of this file, it says so. Rows 52 to 58 came with the review of round 1, rows 59 to
67 with that of round 2 and rows 68 to 73 with that of round 3; they stand behind the others, whatever their
section. The review of round 4 added no row and changed four: 32 (the live SPSR behind a CPSR write), 44 (how
the two kinds of FIQ entry are counted, and the way back of the SH4 loop), 69 (which mirrors share their
slots) and 72 (the CPSR; the mirror case of `writes.reg.pc`; what `arm7.step.fiqbehind` and the fourth kind
of `arm7n.steps` count). The review of round 5 added no row and changed two: 32 (the live SPSR when the mode
written has an SPSR) and 44 (the count is by the number of the FIQ test, and its gate on the third class).
Rows 65 and 73 are not of the design: 65 is of `DESIGN-V3-DECISIONS.md`, which the design cites for
what a gate is, and 73 of `DESIGN-V3-OPEN.md` and of the copies in the emulator repository.

| # | Section | Now | Becomes |
| --- | --- | --- | --- |
| 1 | head, D6 | "The next revision is 6, with the cut of the M2 round." | "Revision 6 came with the cut of the M2 round (`M2-ROUND.md` section 2, package R6): the declarations of the ARM7's run control, the new frozen header `dbg_arm7.h`, and the comments M2 changes." |
| 2 | 1, milestone table, M2 | "full register tables (SB, PVR, AICA common and channels, GD-ROM, maple) with the generic register panel ..."; gate "... sound regression hashes identical before/after (5.6); UI unit tests" | content: "register writes for the SH4 and the ARM7; the full register tables (...) read-only in the device tree; the generic register panel ..."; gate: "contract gate M2 in its two runs (5.5.3); sound lines identical before / after with the sounding guest, and with the debugger in use on it (5.6); `npm run check` with the mock's M2 gate, and the e2e suite" |
| 3 | 1.1, the table | (no row) | a row "Writes of device registers (system bus, PVR, SH4 on-chip modules, AICA, RTC, GD-ROM, maple)": "not in M2: the rows are read-only (`-32013`). Each needs its write semantics stated; AICA with M3, the others with M4" |
| 4 | 2.1 | no module for the ARM7 breakpoint state | a row `dbg_arm7.h/.cpp`: the listed set, the mask, the temporary breakpoint, the exemption, the step; called by the sound burst through `debugger.h` and by `dbg_emu`, `dbg_regs` |
| 5 | 2.2.1, the bullets on `OnThreadExiting` and `OnGuestStateReplaced` | list the SH4's temporary state | also the ARM7's (H6-14, H6-15); a suspended burst is completed by `dc_run` in front of the stop callback and discarded by `Reset()` |
| 6 | 2.2.3, rule 4 | "The AICA scheduler callback starts with `verify(!burst.suspended)` (M2, 2.5.2)." | adds: "A burst the debugger suspended is run on under the latch, by the park loop (2.2.4 a) and by `dc_run` in front of the stop callback, never by the scheduler." |
| 7 | 2.2.3, "During a step ..." | "During a step (the lifetime is `Parked`) the hook runs no call ..." | adds: "The run of the SH4 towards the sound burst that a step of the ARM7 waits for (2.5.3) is such a step." And in step 2 of the hook: a breakpoint or step of the ARM7 is recorded as a freezing cause by `emu::OnSoundStop` |
| 8 | 2.2.4, the loop of `dc_run` | "... `StartCpu(false); resetting = false;` ... `CpuRunning = false; g_parked = false; stopping = false;` ... `stop callback; OnThreadExiting(guestExit);`" | the two assignments go (R6); behind the loop, in front of `SaveRomFiles`: `RunSoundBurst(false);  // a burst the debugger suspended runs to its end, nothing can stop it` |
| 9 | 2.2.4, `Park()` step 3 | "a step is executed by the loop itself (below)" | adds: "but for a step of the ARM7 asked for with no sound burst suspended: the loop leaves for it without publishing (g, h)" |
| 10 | 2.2.4, leaving, a | "(M2, 2.5.2) complete a suspended sound burst; if that hits a sound breakpoint the thread stays parked, the tick reports that breakpoint, and the SH4 command is dropped;" | "run a suspended sound burst on, with the checks of the ARM7 (`RunSoundBurst(true)`, behind `dbg::arm7::PrepareResume()`, or `PrepareStep()` for a step of the ARM7, which is then complete here). If a breakpoint of the ARM7 suspends it again the thread stays parked, that stop is published with a new `stopId`, and the resume or SH4 step is dropped. No burst is suspended at the end of a service excursion;" |
| 11 | 2.2.4, leaving, g and h | a resume, a step, an excursion | a fourth case: "a step of the ARM7 with no burst suspended leaves as a resume does (b to f, with the SH4's resume exemption) and is kept as a step: `g_parked` stays set, nothing is published, the slice hook treats the run as a step; the stop is recorded in the next AICA callback and published by the next `Park()`" |
| 12 | 2.2.5, the bullet on `SuperH4_impl::Stop()` | "**`resetting`, `stopping` and `StopForReset()` are dead state since J1** ... They are removed ... by the next package that owns `libswirl.cpp`, which is E6 (6.1.2)." | "`resetting`, `stopping` and `StopForReset()` were removed with header revision 6 (package R6): `RequestReset()` calls `Stop()`." |
| 13 | 2.3.1, `stopId` | "... on every completed step (paused -> paused), on a reset performed while paused ..." | adds: "on a stop that a breakpoint of the ARM7 makes while a suspended sound burst is run on for a resume or a step (paused -> paused, no `running` tick)" |
| 14 | 2.3.2, the table, the `arm7` row | "M2, 2.5.3; `-32004` before" | "`step`: one ARM7 instruction, `{}`; one tick `paused`, new `stopId`, `reason: "step"`; no `running` tick in between (from a stop outside a burst the SH4 runs up to one AICA tick first: 2.5.3). `stepOver` on a `BL`, and `stepOut`: a resume (tick `running`) until the temporary breakpoint stops — or a stop at once, with no `running` tick, when it is reached in the burst that was suspended. `-32012` while the ARM7 is held in reset" |
| 15 | 2.3.3 | "a multiple of 4 for ARM7"; "The core hands the emulator side the enabled sets (`emu::SetSh4Breakpoints`, `emu::SetEventBreakpoints`) ..." | "a multiple of 4 up to `0x00FFFFFF` for ARM7 (above: `-32602`)"; the sets also through `emu::SetArm7Breakpoints`; a sentence for the ARM7's address compare (`dbg::arm7::MaskAddr`: under the mask of the wave memory, bits 0 and 1 dropped; the tick reports the PC unmasked) |
| 16 | 2.4 | (the skews of a stop) | adds: a stop at an ARM7 breakpoint is a freezing cause; latched while the SH4 waits in SLEEP it is skew 1; anywhere else the stop and its resume, and a step of the ARM7, leave the sound lines of 5.6 unchanged, which the neutrality targets of the M2 round hold on the sounding guest |
| 17 | 2.5.2, first paragraph | "The machine is owned by `Dreamcast_impl::AicaUpdate` (a file-local struct), because the batch render is called from there after `SoundCPU::Update` returns. Latched at burst start: `arm_on = ctx.enabled`, and, while the batch path exists, `per_sample = settings.aica.NoBatch` ..." | "The machine is owned by `Dreamcast_impl` (a struct beside `AicaUpdate`). The loop over the 32 samples moves there from `SoundCPU_impl::Update`, and `SoundCPU` gets the ARM phase of one sample as a call that can return unfinished (`RunSample`). M2 latches `arm_on` at burst start and nothing else; the `per_sample` latch and the phases inside a sample come with the DSP part (E8)." |
| 18 | 2.5.2, sequence and state | the five-phase sequence; "State: `{i, phase, arm_ticks_done, mixl, mixr, arm_on, per_sample}`" | for M2: `if (arm_on) for i in 0..31 { ARM(i); libAICA_TimeStep(); }  AICA::Update(32)` — the last call is the batch render while it exists and the tick of the sound regression harness, once per burst on every path, also behind a completion in the park context. State in M2: `{suspended, i, ticks_done, arm_on}`, outside `Arm7Context`. The sequence and state as printed are M3's |
| 19 | 2.5.2, first bullet | "the callback records the state, requests a pause (`ATTN_PAUSE` with the breakpoint as cause, a freezing cause) and returns `AICA_TICK` as usual" | "the check records the stop (`emu::OnSoundStop`: a freezing cause, `ATTN_PAUSE`), the machine keeps where the burst is, and the callback returns `AICA_TICK` as usual" |
| 20 | 2.5.2, lifecycle | "a host stop and exit first complete the burst with sound breakpoints suppressed ...; a reset discards it; ... While save / load states exist in the tree: a save completes the burst first, as a host stop does, and a load discards it." | "a host stop and a guest exit run the burst to its end without checks, in `dc_run` in front of `SaveRomFiles` and the stop callback (not in `OnThreadExiting()`, which is behind the callback that prints the state hash); `Dreamcast_impl::Reset()` discards it; ... Save and load run only without an emulator thread, where no burst can be suspended: they need no code. Events raised while a suspended burst is run on in the park context are logged and do not stop." |
| 21 | 2.5.2, gate | "with no sound breakpoint armed the sequence of calls is exactly today's, and the hashes of 5.6 are identical before and after the change" | adds: "and, on the sounding guest, with an active breakpoint that is never reached, with breakpoints hit and resumed, and with steps (5.6)" |
| 22 | 2.5.3, second bullet | "`if (unlikely(dbg::arm7_armed)) RunDbg(); else arm->Run(256);` ... Breakpoint addresses are compared after `& aram_mask`. `skip_once` is the resume exemption (one instruction, as R9)." | the flag is `dbg_arm7_armed` and the check `dbg_arm7_Check` (`debugger.h`); "compared under `dbg::arm7::MaskAddr`: `& aram_mask` with bits 0 and 1 dropped"; the exemption by M2-D13: "armed at a resume only behind a stop that the ARM7 check decided, with the PC of that stop, the ARM7 clocked and a check to come; used only by a check at that PC; ended by the first check, by a change of `dbg_arm7_armed` to or from 0 and by a burst that finds the ARM7 held; a step-over on a `BL` and a step-out arm it for the PC they are asked from" |
| 23 | 2.5.3, third bullet | "On hosts with the ARM7 JIT (arm64) ... ARM7 timing differs while an ARM7 breakpoint is armed ..." | "Where the selected ARM7 backend is the recompiler (arm64 with it enabled) ... timing differs while the debug loop is in use: while a breakpoint of the ARM7 is **active** (enabled, its category in the active mask), a temporary breakpoint exists or a step is armed; the notice is also in the stop that ends a step, a resume or a run in which the debug loop executed, since a step and a temporary breakpoint disarm before their stop is published" |
| 24 | 2.5.3, fourth bullet | "`control.step {arm7}`: if a burst is suspended in the ARM phase, execute one ARM instruction; otherwise resume with a one-shot "break before the next ARM instruction" (the SH4 runs until the next burst, at most one `AICA_TICK`). `stepOver`: ... `stepOut`: ..." | "`control.step {arm7}` executes one ARM7 instruction and never publishes `running`. With a burst suspended: in the park context, the stop at the check behind the instruction (the next instruction, a branch target, a vector); if the instruction was the last of the burst, at the burst's end with none suspended. With no burst suspended: the park context is left without a publication, the SH4 runs to the next burst (at most one `AICA_TICK`), and the step is the first instruction of that burst; it is complete with nothing executed when a FIQ is entered in front of the instruction (the stop is at the vector) or when that burst finds the ARM7 held in reset; a stop of the SH4 that comes first is published with its own cause. The step executes its instruction also with a listed breakpoint on it. `stepOver`: the temporary breakpoint at `PC + 4` after a `BL` and a resume, else a step. `stepOut`: the temporary breakpoint at `LR` and a resume (heuristic ...). Both execute the instruction they are asked from, also with a listed breakpoint on it. One temporary breakpoint exists at a time; it goes at every published stop. A host stop, a guest exit or a guest reset during the wait of a step ends the step unpublished (a reset publishes its own stop)." |
| 25 | 2.5.3, fifth bullet | "`control.step / stepOver / stepOut {arm7}` and `breakpoints.add` for `dc.aica.arm7.pc` return the in-band error `-32012`" | "`control.step / stepOver / stepOut {arm7}` return the in-band error `-32012`. A breakpoint on `dc.aica.arm7.pc` can be added, toggled and removed in every state: it is what stops at the ARM7's first instruction." |
| 26 | 2.6 | "Events raised ... on (E) in the park context never stop" | adds the case of the sound burst run on in the park context (H6-18) |
| 27 | 2.7, writes, main RAM | "for every touched 4 KiB page call `bm_LockedWrite(...)` ... Without the virtual memory layout ... discards the blocks of each touched page through the backend's `DebugDiscardPage` (2.3.4 l) and stores the bytes; nothing in it needs the mapping or page protection (D7) ..." | "for every touched page of `REI_PAGE_SIZE` (4 KiB; 16 KiB on macOS) ... Without the virtual memory layout ... discards through `DebugDiscardPage`, removes the protection of the pages itself (`bm_AddBlock` protects the pages of a block with or without the layout) and stores the bytes" |
| 28 | 2.7, writes | "anything else is refused with `-32013`, and the range is checked as a whole before the first byte is stored" | adds: "A range that would pass the end of the address space is refused too: a write does not wrap. `target: "dsp"` is the in-band `-32004`." And in the code block: `int Write(...)  // 0 or -32013` |
| 29 | 2.8, "What is normative" | the M1 items | adds: "for a server that offers the action `sh4-registers`: the paths and parents of the M2 rows, the two actions `sh4-registers` and `arm7-registers`, and at least one register on every node whose registers are M2's (S16, part `tree`)" |
| 30 | 2.8, the table | the rows `dc.holly.dmac`, `dc.holly.sb.{g1,g2,pvr,misc}`, `dc.holly.ta` / `.core`, `dc.holly.spg`, `dc.aica.common` / `.rtc`, `dc.aica.sgc.0 .. .63`, `dc.gdrom` / `dc.maple` | the register lists of `M2-ROUND.md` 3.6.1, each marked read-only; `dc.holly.sb` itself without registers; "controller / state registers (device snapshots)" becomes "`dc.gdrom`: the drive's snapshot; `dc.maple`: the twelve maple registers of the system bus" |
| 31 | 2.8, behind the table | "`sh4-registers` and `arm7-registers` are added in M2 (schema and server together)" | "(the UI's schema with F3a, the emulator's list of ids with R6, the two actions with E7b)" |
| 32 | 2.8, "Register values" and `dc.aica.arm7` | "a register with `width: 0` is free text and read-only"; "Writes (M2): `CPSR` through `CPUSwitchMode` + `CPUUpdateFlags`, `PC` into `R15_ARM_NEXT`." | adds: "A register that `state.setRegister` can write carries `flags: {"writable": true}` in `debugger.describe` (not in ticks). In M2 those are the registers of `dc.sh4.cpu` and the registers of `dc.aica.arm7` but `ENABLED`; a write to any other answers `-32013`. A write of the ARM7's CPSR with mode bits that are no mode is refused (`-32013`), and so is a PC of the SH4 with bit 0 set. The ARM7's PC is stored with bits 0 and 1 clear. An FPSCR write that changes PR, SZ or the rounding mode empties the SH4's code cache (2.3.7 item 2), and so does a write that moves the PC of the SH4 (2.3.7 item 6). An SR write calls `UpdateINTC()` when an interrupt is pending and let in, as the guest's `LDC` does: under the latch the acceptance is owed, and at a code breakpoint of the SH4 it thereby moves from the next slice end to the leave. A CPSR write of the ARM7 goes through the mode switch also when the mode stays, which rewrites the bank slots of the mode it leaves and, when that mode is user or system, copies the CPSR as it was into the live SPSR, over which a mode that has an SPSR loads its own when it is entered: behind a write and its write back the registers read as before, but SPSR when the write is made in one of those two modes, and the save state can differ." A write-only register reads `"n/a"`: on the system bus, and `SCILV0-2`, `SCIRE`, `MCIRE` of the AICA; `dc.aica.common` has `L` and `M` from the sound CPU in place of the file's words at `0x2D00` / `0x2D04` |
| 33 | 2.9 | "Broadcast after every successful mutating RPC (`breakpoints.*`, the watch methods, the writes)" | adds: "`control.reset` sends no tick of its own: paused, the stop of the reset is its tick; running, nothing is published (2.3.2)" |
| 34 | 3.1 | the register of a describe node: `name`, `value`, `width` | adds `flags: {"writable": true}` (row 32); `targets[arm7].stepping` is what `emu::TargetSteps` says |
| 35 | 3.3, `breakpoints.add`, row and the order paragraph | "... or the table is full; `-32012`; in the order below"; "Then, for both kinds, the table bound (1024), `-32602`; then `-32012` (a sound target that is not clocked, 2.5.3, 2.5.4)." | `-32012` goes from both; "`address` present and valid for the target (2.3.3: aligned, at most `addressMax`), else `-32602`" |
| 36 | 3.3, `state.setRegister`, `state.editWatch` | "`value` is `0x` hex or decimal, truncated to the register's `width`"; "in-band: `-32602` unknown path / name or unparsable value; `-32013` register with `width: 0` or without a writer" | the rules of M2-D20: matching without regard to case, the order of the four answers, the grammar of the value, the two values a device refuses (an odd PC of the SH4, a CPSR of the ARM7 that is no mode); for `state.editWatch` an unknown `watchId` is `-32602`; a register reads back what was stored (SR under `0x700083F3` and with RB 0 when MD is clear, FPSCR under `FPSCR_MASK`, the ARM7's CPSR under `0xF00000DF`, its PC aligned) |
| 37 | 3.3, `state.writeMemory` | "in-band: `-32013` range not writable" | adds: no wrap; `target` is `sh4`, `arm7` or `dsp` in the params, and `dsp` is the in-band `-32004`; served while the target runs; a tick behind every accepted write |
| 38 | 3.5, `-32012` | "sound target not clocked (2.5.3, 2.5.4)" | "a step of a sound target that is not clocked (2.5.3, 2.5.4)" |
| 39 | 4.4 | ten bullets, one package | F3 is F3a to F3d (`M2-ROUND.md` 3.1, 3.7 to 3.9). Changed bullets: the register panel finds its node by `actions` (the first in the order of the tree if several offer its id), takes its rows, widths and `flags.writable` from the describe node and its values from `registersByPath[path]` (now: "fed by `registersByPath[path]`"), and edits a register with the flag; a watch has an editor when its expression names a described register with the flag; the views that follow the PC move when a paused tick of the same stop shows another PC, and no action moves them; hotkeys "handled on workspace pages wherever the focus is, but for Monaco and an open dialog"; the toolbar acts on the focused target as M2-D39 defines it; "the mock becomes an M2 server (`state.writeMemory`, `control.reset`, the M2 rows, an ARM7 that a reset holds and the run releases, the three effects of M2-D45, the stores and the call of M2-D53) and `npm run check` runs its M2 gate, on the full mock and on the mock with `MOCK_M2_SUPPORT`"; the end of v2 by the table of `M2-ROUND.md` 3.7; the smoke spec against the emulator is in F3d's gate |
| 40 | 5.5, S16 | four parts and "the M2 rows of 2.8 are in the tree (checked from F3 on, 4.4)" | five parts, `arm7`, `writeMemory`, `setRegister`, `reset`, `tree`, with what 3.1 item 3 of `M2-ROUND.md` adds to each, and the gate of `tree` (the action `sh4-registers`). Among it: `tree` asks that each register-panel id is offered by one node; `writeMemory` writes video memory through three addresses of one view and wave memory through both targets; `reset` adds a disabled breakpoint, a watch and a flipped category before the paused reset and finds them behind it, writes a register of the held ARM7, and ends with the machine resumed and paused again |
| 41 | 5.5, the `dsptest` paragraph | "a KOS program that loads a fixed DSP program and plays a tone through it, built with the sh-elf toolchain of PLAN track 7 ..." | "a bare-metal SH4 program with an ARM7 program of its own (`M2-ROUND.md` section 5), assembled by its `gen.sh` with binutils; its ELF and its ARM7 image are committed. Package E6a writes it with the tone and the ARM7 side; E8 adds the DSP program part." |
| 42 | 5.5.3 | "M2 adds S16"; "The mock's gate is `npm run contract -- --milestone M1` ..., which `npm run check` runs; the mock is "M1 plus", so without `--milestone` its S16 is partial ..." | "M2 adds S16 and is two runs: the one with the M1 test program, and `--elf <dsptest.elf> --milestone M2 --only S00,S16` against an emulator started with the guest"; "the mock's gate is `--milestone M2`: its S16 passes"; `--parts` for a package that delivers a part of a scenario; the second run requires the notes `arm7: held at the start` and `arm7: bl stepped over and out` with the runner's `--require-note`; `--mock-support` (`m1` or `m2`) is the profile of the mock the runner starts itself, and `check` runs the gate on the default mock and on `m2` |
| 43 | 5.5.4 | "Held by the selftest of the core and not by the gate on 2ndmix: the half of S11's pairing in which `ENABLED` reads 0 ..., and the no-wrap rule of a memory slice ..." | both are held by the gate from F3a on: the pairing is an invariant of every paused tick (the anchor stop has `ENABLED` 0), and S09 asks across `addressMax` |
| 44 | 5.6, the guest | "a KOS program that plays a tone through the sound generator, with its ELF committed. Its `samples` hash has to differ from the hash of silence. The guest is written as the first step of E6 ..." | the guest of row 41, by package E6a; "its `samples` hash is none of the hashes of silence, `basis * prime^(128 k)` for `k = 0 .. N` (in per-sample mode no pair exists while the ARM7 is held, so silence is a family)"; the runner (`soundlines.py`) and its ctest targets live in `tests/debugger/guest/dsptest/`; N is 2000 for the guest; no line is stored in the tree; "the guest folds what its ARM7 reads of the timer and of the pending bits into registers, and its FIQ handler folds its return address and saved status, so that the `arm7` field carries the order inside a burst — every FIQ entry to the instruction, a time step to the iteration of the loop: the line has no field for wave memory, main memory or the scheduler"; "its ARM7 loop masks F over a part of each pass, so that a FIQ is entered at the first instruction of a sample and, when the request came while F was set, inside one; the package that writes the guest counts the entries before the baseline, by the number of the FIQ test of its sample that makes each (the first; the second, when the unmasking `MSR` starts its sample; the third or a later one) and not by the return address, which an entry at a sample that starts behind the unmasking `MSR` shares with the entries behind it; the count's gate is on the third class, with a longest stretch of ticks without one: it is the class a stepped `MSR` shows inside one ARM phase, and where the loop stands when the timer overflows is no uniform phase"; "its SH4 main loop goes back by a PC-relative branch, so that a PC written into another mirror of main memory stays there"; "from any instruction of its ARM7 loop the next call is at most 32 instructions away and its FIQ handler at most 48 long, and its loops are plain by the contract runner's test of a line, which takes a literal load for control flow"; "in batch mode the channels the SH4 keyed on are rendered without the ARM7: a guest whose ARM7 is never released is silent per sample only" |
| 45 | 5.6, "its two specified workloads" and the gate | "On its two specified workloads the sample hash saw only silence"; "Gate for M2 and M3: identical lines before and after the change, same matrix with the sounding guest" | "On 2ndmix, and on the boot ROM run E3 also made, ..."; the gate adds: "with the debugger in use on the guest — an active ARM7 breakpoint never reached, breakpoints hit and resumed, steps — the line equals the plain line and the exit state hash at 90 vblanks equals the plain one, part for part; each such test says in lines its target requires what it did (the debug loop ran every sample; how many stops, in how many bursts; how many steps of each kind)"; "the lines and hashes of the `NULLDC_DEBUGGER=OFF` build equal those of the default build"; "E8 changes the guest and retakes the baseline first"; "the selftest session of the sound side runs in both `aica.NoBatch` settings; the package that wires the T5.0 core removes the batch half of the sound targets and of that session" |
| 46 | 6.1, the nine contracts, items 3 and 9 | the declarations of `dbg_emu.h` and `dbg_sh4.h` | item 3 gains `SetArm7Breakpoints` and `TargetSteps`; item 9 gains (b') the block of `dbg_arm7.h` and in (c) `OnSoundStop`; a pointer to `M2-ROUND.md` section 2 for the texts |
| 47 | 6.1, the last paragraph of the nine contracts | "The device accessors that E0 declared and E3 implemented (no side effects; (E), or under L): `AICA::DebugReadRegs / DebugWriteReg / DebugGetChannel`, ... `SoundCPU::DebugEnabled / DebugGetReg / DebugSetReg / DebugGetInterrupt`, ..." | the two writers leave the list; a sentence: "`SoundCPU::DebugSetReg` is a writer ((E) only, the effects its comment gives; body with E6b). `AICA::DebugWriteReg` has no body in M2." |
| 48 | 6.1.2, items 1 and 2 | "It is done by the next package that owns `libswirl.cpp`, which is E6." | done with R6 |
| 49 | 6.2, row F3 | one row | four rows, F3a to F3d, with the owns and gates of `M2-ROUND.md` 3.1 and 3.7 to 3.9; "Depends on: emulator M2" goes for F3a to F3c; F3d depends on E7b (its smoke spec), and M2G needs both |
| 50 | 6.3, intro and rows E6, E7 | the two rows | rows R6, E6a, E6b, E7a, E7b, M2G, each pointing at its subsection here; E8's row gains: "retakes the sound baseline when it changes the guest; the write semantics of the AICA registers and the body of `AICA::DebugWriteReg` (M2-Q1)"; E9's row gains: "writes of system bus, PVR and SH4 on-chip module registers, the snapshots of maple and the SPG" |
| 51 | Appendix B | the list of frozen headers; the table of revisions; "No revision is pending. The next one is revision 6 ..." | `dbg_arm7.h` joins the list; a row "6 — R6, the cut of the M2 round — six declarations (the ARM7 block of `debugger.h`, `SetArm7Breakpoints`, `OnSoundStop`, `TargetSteps`, `dbg_arm7.h`, `SoundCPU::DebugIsRecompiler`) and the comments H6-10 to H6-42; texts in `M2-ROUND.md` section 2"; "No revision is pending. The next one comes with the cut of M3." |
| 52 | 2.3.7, item 2 | "... a step and a run can differ exactly where the run is already wrong. PLAN T3.4 (blocks keyed on PR / SZ) removes it." | adds: "A debugger write of FPSCR would make a correct run wrong the same way, so `dbg::regs::Write` empties the code cache when it changes PR, SZ or the rounding mode (M2, `M2-ROUND.md` M2-D41)." |
| 53 | 2.3.7, a new item | (none) | "**A write can end the emulator.** A PC, or code bytes, written so that the guest reaches a word that is no instruction ends the process in the default build, as for a guest that gets there by itself (PLAN's backlog: undefined instructions and odd PCs are fatal). The debugger refuses an odd PC of the SH4 and stores the ARM7's PC aligned; nothing else is checked. The UI help says so." |
| 54 | 2.5.2, the lifecycle bullet (with row 20) | (the same-slice cases are not told apart) | adds: "A host stop, a guest exit or a guest reset asked for in the tick in which a check suspends is met by `dc_run` with a burst suspended and no park in between: the first two run it to its end, and nothing is published; a reset discards it and the stop published is the reset's." |
| 55 | 5.5, S16 `arm7` | "a pending FIQ is entered before the break test ..." | adds the settling (an ARM7 that is released more than once while the guest starts, a PC above `addressMax` in between), the toggled, muted and twin breakpoint, the breakpoint on a mirror address, `SH4_CYCLES` across a step (never back; at most two AICA periods forward; unchanged, with the SH4's PC, by a step from a breakpoint stop), the `bl` stepped over and out with 96 steps for each of its two arrivals and the target that keeps running behind the step-out, and the notes |
| 56 | 5.5, the invariants | I4 | "no register of a tick carries `flags`"; the pairing of `ENABLED` with `callstacks.arm7` with its frames 0 and 1 |
| 57 | 5.7, first and third items, and "Speed" | the builds and what a verifier adds; "within 2 % ..., three runs each" | adds: the generic host as a fifth build of a package's gate; "all ctest targets green twice, none skipped" with the configure line that makes missing data a failure (the two vector checkouts among the paths it names); for a package on the sound path, the host cost in two checks — the compiled code of the path an ARM7 instruction takes is unchanged, and the user-mode instructions of a whole run of the sounding guest and of 2ndmix grow by at most 48 per sample —, and the ARM7 suites a second time with `Step` as its two halves (`arm7.*.halves`), with speed by CPU time, five runs of base and tip in turn, held against the spread `tests/perf/BASELINE.md` gives for two rows of one binary (1.5 %) and deciding nothing by itself |
| 58 | 4.4, 5.5.3, 6.2 | the UI checkout of an emulator gate is "the UI repository" | "the UI checkout an emulator gate runs with is a commit the integrator names, the last verified UI head (`M2-ROUND.md` section 4)" |
| 59 | 3.5, the table, `-32013` | "memory range or register cannot be written" | "a memory range or a register cannot be written, or the register's device refuses the value (M2: a PC of the SH4 with bit 0 set, a CPSR of the ARM7 whose mode bits are no mode); such a value is not a `-32602`, which is for a text that is no number" — the `-32602` row keeps "misaligned or out-of-range address" for address parameters |
| 60 | 3.3, "A running target is asked things" | the list of methods that work while the target runs | the list gains "`state.writeMemory`, `state.setRegister`, `state.editWatch` and `control.reset` (in a service excursion; each accepted write followed by its tick, `control.reset` by none)"; none of them may answer `-32003` |
| 61 | 5.3 | the sessions and tests of M1; "A guest test ... uses `dbg::emu` (`SetSh4Breakpoints`, `SetEventBreakpoints`, ...)" | adds the sessions `arm7` (also with `aica.NoBatch=yes`) and `writes`, the tests `core.writes` and `core.reset`, the registration `writes.smc` (`Dynarec.SmcCheckLevel=-1`), the run-ended tests `arm7n.*` and `writesrun.neutral`, and the guest of the tree as their program; the guest-test sentence gains `SetArm7Breakpoints` (H6-36); a rule: "a test that the run ends is not named under the prefix of a session: the filter of a session selects by prefix and waits for its last test to return" |
| 62 | 5.4 | the modes of `contract.py` and its targets on 2ndmix | adds the modes `parts` and `m2`, the option `GUEST` of `nulldc_debugger_contract`, the pass-through of the runner's `--parts` and `--require-note`, and the targets `debugger.contract.m2arm7*` with `.m2arm7.guest*`, `.m2writes*` with `.m2writes.guest*`, `.m2*` with `.m2.guest*`, and `.hash.listening*`; a sentence: "an emulator that has to carry the UI is configured by itself with `-DNULLDC_DEVTOOLS_DIST=<a dist-native directory>` and handed to the tests project as `NULLDC_EMULATOR`; the option is not given to the tests project, where it replaces the bundle of `tests/debugger/net`" |
| 63 | 5.2, the row of `tests/debugger/regs/`; with it 2.8 ("the 35 ids of protocol v2 in M1") and 5.3 / 5.7 ("the bound of 256 watches cannot be reached on the M1 tree, which has 247 registers") | the checks and counts of the M1 tree | the row gains the M2 tree: 104 nodes with parents, labels and actions, no M2 row writable, the checks of `M2-ROUND.md` 3.6; 37 panel ids from R6 on; from E7b on the tree has more than 256 registers, `core.tables` takes its other branch and S08 makes its bound probe against the emulator |
| 64 | 2.5.2, third bullet | "Scheduler callbacks that share the tick of a suspended burst run before the burst completes; this reordering exists only in a debug stop and is not covered by the bit-identity gate." | "... and is not covered by the bit-identity gate in general; on the sounding guest, whose same-tick callbacks read no sound state, the neutrality targets hold it (row 21)" |
| 65 | `DESIGN-V3-DECISIONS.md`, decision 9 (and its copy `docs/nulldc3/devtools-integration-decisions.md`) | "(M2-M4 alike; the M3 DSP part is a second run with the DSP test program)" | "(M3 and M4 alike; the M2 gate is two runs: the second is `--elf <dsptest.elf> --milestone M2 --only S00,S16` against an emulator started with the sounding guest, with its two required notes — `M2-ROUND.md` M2-D34; the M3 DSP part is a further run with the DSP test program)" |
| 66 | 2.3.7, a new item | (none) | "**The recompiler without the virtual memory layout cannot resolve a write fault in a page of compiled code**: `bm_AddBlock` protects such a page in every build, and `bm_LockedWrite` measures the faulting address from `virt_ram_base`, which is null there. A guest that stores into a page from which a block was compiled ends the process. No configuration gives that build on the development machine (`M2-ROUND.md` M2-Q6)." |
| 67 | 2.7, writes, main RAM (with row 27) | (the discard is described; what a missing discard would do is not) | adds: "With the layout in use a store without the discard is made good in the default configuration — by the fault handler on a page its blocks protect, by the block's own check (`Dynarec.SmcCheckLevel`, `FullCheck` by default) on a data page; the test of the discard therefore runs with `Dynarec.SmcCheckLevel=-1`. A step, and a resume behind a breakpoint stop, decode what they run from memory: a witness of the block cache is taken from code the dispatcher enters from the cache." |
| 68 | 2.2.1 (the owed resume) and 2.2.4 (what `Park()` returns; leaving, e) | "... `dbg::sh4::PrepareResume()` is called ..."; "`PARK_RUN` (... `CpuRunning` is set and `g_stop_latched` clear: call `Run()`)"; "`PARK_EVALUATE` ... after a step" | the owed resume also calls `dbg::arm7::PrepareResume()`, and no sound burst is suspended then (the thread before ran it to its end in front of its stop callback); `PARK_RUN` comes with `g_parked` clear but for a step of the ARM7 that waits for its burst; `PARK_EVALUATE` follows a step in which the SH4 executed (H6-19) |
| 69 | 2.3.7, item 6 | "**`fpcb` aliasing** between the boot ROM and RAM (the dispatcher clears the cache at three boot addresses) is untouched. ..." | adds: "Main memory at `0x0C…`, `0x8C…` and `0xAC…` shares its slots the same way: the cache is indexed by `(pc >> 1)` under a mask of 32 MiB, with no address in the key, so addresses a multiple of 32 MiB apart share a slot (the mirror 16 MiB up, `0x8D…`, is the same memory in other slots). A debugger write that moves the PC of the SH4 empties the code cache, so that a resume agrees with a step behind a PC written into another mirror or onto a boot ROM address (`M2-ROUND.md` M2-D42); a guest that changes mirror by itself is as before." |
| 70 | 2.5.2, state (with row 18) | (nothing counts what a burst did) | adds: "The machine counts the bursts the scheduler started, the ARM phases that ran to their end, the time steps and the tails (`VirtualDreamcast::SoundBurstCounts`), in every build and outside every save state and hash: the selftests count calls that emulator code makes, and emulator code includes no debugger header but `debugger.h`." |
| 71 | 2.5.3, second bullet (with row 22) | the debug loop | adds: "The debug loop looks at the pending FIQ in front of every instruction, as `Step` does, and so does the loop that goes on with a suspended sample: a request can become enterable inside a sample (an `MSR` that clears F with a request waiting). `StepIrq` and `StepExec`, the two halves the loop is made of, are run by the ARM7 suites under `ARM7M_STEP=halves` (`arm7.*.halves`)." |
| 72 | 5.3 and 5.7 (what a neutrality test may do) | (no rule for writes) | adds: "A neutrality test of the debugger's writes writes at stops latched at a slice end, and restores: main memory, a code page, general registers, SR and FPSCR of the SH4, R0 to R12 of the ARM7. Not a PC (the owed remainder goes), not SR at a code breakpoint of the SH4 (the acceptance of a pending interrupt moves to the leave), not the CPSR of the ARM7 (the mode switch rewrites bank slots that only the save state shows, and in user or system mode the live SPSR)." And in 5.3, `writes.reg.fpscr`: "the witnessing block is compiled with PR and SZ clear: a block compiled with PR set calls the interpreter's handlers, which read FPSCR when they run"; `writes.reg.pc`, the mirror case: "the PC is written into a mirror a multiple of 32 MiB away, which shares the slots of the block cache, on a loop that goes back PC-relative"; `arm7.step.fiqbehind` and the fourth kind of `arm7n.steps`: "a step of the unmasking `MSR` that ends at the FIQ vector counts as an entry inside a sample only when no ARM phase ended in it or in the step before it" |
| 73 | `DESIGN-V3-OPEN.md`, its head and its last paragraph; the copies under `docs/nulldc3/` of the emulator repository | "The next revision is 6; it comes with the cut of the M2 round and is not listed here."; "**The next revision is 6.** ... it is listed there, not here."; the name table at the top of `devtools-integration-design.md` and the list of `README.md` have no row for the round's document | "Revision 6 is applied (package R6 of the M2 round); its list is section 2 of `M2-ROUND.md`."; the name table gains "`M2-ROUND.md` — `devtools-integration-m2-round.md`" and the README its line (both with R6, 2.4 item 6); at M2G the copies `devtools-integration-design.md`, `-decisions.md`, `-open.md`, `PLAN.md` and `devtools-integration-m2-round.md` are brought to the working documents as they then stand |

---

## Appendix A. Briefs

One brief per package, for the orchestrator to hand to its engineer. A.0 goes with every one of them. Each
brief names sections of this file as part of itself: the engineer reads them here.

### A.0 What every brief carries

- Work in your own worktree and your own out-of-tree build directory under `agents/nulldc3/` (emulator:
  `agents/nulldc3/wt/<pkg>`, branch `skmp/nulldc3-<pkg>` of repository `devcast`, cut from the integration
  branch `skmp/nulldc3` as you find it; UI: branch `skmp/nulldc3` of `dreamcast-devtools`, packages are
  sequential on it). Commit as work lands; push nothing. Reports and scratch go to `agents/nulldc3/<pkg>/`.
  Delete your build trees before you return.
- The machine: one workflow at a time, `ninja -j8`, `ctest -j4`, at most four emulator processes, every
  emulator run headless (`config:Headless=yes`, `docs/nulldc3/automated-runs.md`). Gates that take more than
  five minutes are run on a subset while you iterate and in full once at the end.
- **A gate is green when its tests ran** (M2-D46). In a worktree under `agents/nulldc3/wt/` every default
  look-up of the tests project misses (the UI checkout, the `wren7-rtl` and `caique-rtl` checkouts, the
  programs, **and the two vector submodules, `tests/vendor/sh4-tests` and
  `tests/vendor/arm7di-tests-dreamcast`, which a new worktree has empty**), and a test whose data is missing
  is reported skipped, which ctest counts as passed. Configure the tests project with `-DNULLDC_ELF_DIR`,
  `-DNULLDC_DEVTOOLS_DIR` (the UI commit the
  integrator named: `M2-ROUND.md` section 4), `-DNULLDC_WREN7_DIR`, `-DNULLDC_CAIQUE_DIR`,
  `-DNULLDC_SHRIKE4_DIR`, `-DNULLDC_SH4_TESTS_DIR` and `-DNULLDC_ARM7_TESTS_DIR` (the two vector checkouts;
  or `git submodule update --init tests/vendor/sh4-tests tests/vendor/arm7di-tests-dreamcast` in your
  worktree) and `-DNULLDC_TESTS_REQUIRE_DATA=ON`; the orchestrator gives the paths. Without the vectors
  `sh4.singlestep.*`, `sh4.differential` and `arm7.vendor` are tests whose data is missing, and they fail
  under that option. "All ctest targets
  green" in a gate means: twice, none skipped, and the number of targets is in your report.
- **The UI bundle is the one look-up that has to miss.** The emulator's own configure looks for a bundle
  (`<tree>/devtools/dist-native`, then `<tree>/../dreamcast-devtools/dist-native`) and embeds what it finds;
  `ui` of `debugger.describe` is that bundle's version. The emulators the tests project builds are
  configured by `tests/system/build_emulator.cmake`, which passes on nothing but `HAS_TA_LLE`: in a worktree
  under `agents/nulldc3/wt/` they find no bundle, and **the emulators of a package gate embed none**. That
  is why the pinned UI checkout is `agents/nulldc3/wt/ui-pin` and no directory beside your worktree is ever
  named `dreamcast-devtools`; and why a gate is run from a worktree, not from the main checkout of
  `devcast`, which has the UI repository beside it. `-DNULLDC_DEVTOOLS_DIST` is never given to the tests
  project: there it replaces the small bundle that `tests/debugger/net` is written against. An emulator
  that has to carry the UI (F3d's gate, M2G) is configured by itself with
  `-DNULLDC_DEVTOOLS_DIST=<UI checkout>/dist-native`, after `npm run build:native` there, and handed to the
  tests project as `-DNULLDC_EMULATOR=` and `-DNULLDC_EMULATOR_TA_HLE=`.
- **Where this file and `TRACK1-DESIGN.md` differ, this file is the contract** (M2-D64). The design is
  brought to it at the end of the round; section 7 lists every place where the two differ. A design section
  your brief names is read with its rows of section 7 — design 2.5 still has the burst state of M3, a step
  of the ARM7 as "a resume with a one-shot break" and `-32012` for `breakpoints.add`; design 2.7, 2.8, 3.3
  and 3.5 still have "4 KiB page", the ARM7's PC "into `R15_ARM_NEXT`" as given, and the old error order.
- The builds of an emulator package's gate are five: the default, `-DNULLDC_DEBUGGER=OFF`,
  `-DTARGET_NO_REC=ON`, `-DHAS_TA_LLE=OFF`, and the generic host (an unknown `CMAKE_SYSTEM_PROCESSOR`, as
  package P-desktop built it: `agents/nulldc3/p-desktop/` has the configuration). The last compiles and its
  2ndmix state hash is what it was.
- You edit what your brief says you own and nothing else. The headers of `libswirl/debugger/` and the debug
  blocks of the `hw/` headers are frozen: their text is section 2 of `M2-ROUND.md`. If you need a declaration
  changed, or a file of another package, stop and report: the integrator decides.
- Time neutrality: a debugger that is not used changes no guest-visible state. The state hashes of design 5.7
  and the sound lines of design 5.6 are compared exactly; a change that moves one is a defect until shown
  otherwise.
- No absolute home paths, host names or user names in anything you commit. No hash or sound line is stored in
  the tree.
- Your report: what was delivered against each item of the brief; every place where you departed from this
  file, and why (a departure is a departure from this file: where it changes the design, section 7 says
  so, and following it is none); the commands and results of the gate; what is open. A verifier who did not write
  the package checks it, with mutations; expect fix rounds.

### A.1 F3a — the contract of M2

Repository `dreamcast-devtools`, branch `skmp/nulldc3`, on head `22b358e`.

Read: `M2-ROUND.md` 3.0, 3.1, 3.6.1, the two bounds and the "not control flow" bullet of 5.2 (what your `bl`
rule and your site search may count on in the guest) and the decisions M2-D11, D12, D19 to D24, D34, D36 to
D38, D45, D52, D53, D55; `understand-m2/devtools-m2.md` sections 1.5, 1.8, 1.9, 1.11, 2 and 3; design 3.3,
3.5, 4.4, 5.5 (with 5.5.1 to 5.5.4), each with its rows of section 7 (A.0: where the two differ, this file
is the contract). In the repository: `scripts/contract/` (scenarios, invariants, design, run and their tests),
`server/mockDebugger.ts` with its tests, `src/lib/debuggerSchema.ts`, the README.

First: build the emulator from `devcast` `skmp/nulldc3` as it stands (under `agents/nulldc3/`, change nothing
in it) and make the observation of 3.1 on 2ndmix, on the three engines, before you write the `arm7` rules: the
settle times of the rule are yours to confirm or to change with what you see, and the report has the numbers.

Deliver: the four items of 3.1 "Delivers". The order that keeps the tree green: the schema; the mock's M2 with
its tests (its stores, its ARM7 held behind a reset, the call in its ARM7 ring with step-over and step-out as
temporary breakpoints, the three effects, the `--m2` profile); then the runner's rules one
by one, each with the patched server that it fails; then the options `--parts`, `--require-note` and
`--mock-support`; then the gate line of `check`. Every new runner rule is
exercised by the mock (so that it has run before it meets an emulator) and has a test that goes red when the
rule is taken back — the `bl` rule among them: both runs of `check` require the note
`arm7: bl stepped over and out`. No rule is bound to the emulator profile: the mock has the effects the write
rules look
at. `target: "dsp"` of `state.writeMemory` is decided inside the call, behind `-32002` / `-32001`, not with
the parameter checks. The probe that executes what was written executes one instruction, and that one yours;
it never moves the PC onto a line of the program. The 65536-byte write puts back what it read, at the SH4's
`defaultMemoryAddress`; nothing of the part writes a pattern over memory it did not save. The three notes of
the `arm7` part are an interface to the emulator's targets: their texts are in 3.1 item 3, verbatim.

Five things of 3.1 item 3 came with the review of round 3 and are easy to read past. The `bl` rule counts 96 steps
for each of its two arrivals, from 0 each time, and charges the steps through a FIQ handler to the arrival
they fall in; a step on the `bl` line that ended at a vector is not the step into the callee. The `reset`
part brings its own tables (a disabled SH4 breakpoint, a watch, the flipped category `dsp`), writes `R1` of
the held ARM7, and ends by resuming the machine behind the reset and pausing it. An ARM7 step from a
breakpoint stop leaves `SH4_CYCLES` and the SH4's `PC` alone, and no ARM7 step lets `SH4_CYCLES` grow by
more than two AICA periods. A breakpoint on the site's address + `0x00800000` is hit, with the PC as it is.
And in the `tree` part each register-panel id is the action of one node.

You own and must not touch: 3.1. The e2e expectations you change because the mock's tree, ARM7 program or
answers changed (`e2e/toolbar.spec.ts` and the specs of the ARM7 ring among them) are listed in the report,
one line each.

Gate: 3.1 "Gate". For its last line: with `NULLDC_DEVTOOLS_DIR` at your tip, the targets of that emulator
build that execute UI source pass — `debugger.contract.m1`, `.m1.interp` and `.m1.fallback` with 18 scenarios
each, the other `debugger.contract.*` targets (`hash.idlebp` and `.interp` run your runner in its
`--idle-breakpoint` mode), `debugger.disasm.dsp_playground` and `debugger.net.interop_ws`.
`debugger.regs.panels` of that build fails by construction (35 ids against 37); say so in the
report, it is closed by the next package. If one of the two M1-level rules fails on the merged emulator, do
not weaken it and do not touch the emulator: stop and report, with the frames.

Report also: the list of rules added, each with its test; the observation; anything in the emulator's M1
behaviour that the two M1-level rules found.

### A.2 R6 — header revision 6 (integrator)

Repository `devcast`, on `skmp/nulldc3` at `d546aa76`; the UI checkout pinned at F3a's merge commit (section
4). You wait for no answer of the owner: H6-33 is written as printed (M2-D54). If M2-Q4 is answered "no"
before E7a starts, you apply the two alternative sentences that 2.2 gives with H6-33 as a correction of the
revision, comment only, and tell E7a's orchestrator that its brief loses the two `ResetCache()` calls, the
cache half of `writes.reg.fpscr` and the mirror case of `writes.reg.pc`; a later "no" is header revision 7.
If M2-Q1 or M2-Q5 has been answered "no" when you start, stop: H6-23, H6-33 and H6-39 are then not the
texts to freeze, and this file is revised first (6.2).

Read: `M2-ROUND.md` section 2 (it is the specification, word for word), 3.0 and 3.2;
`understand-m2/headers-m2.md` sections 2 and 3, `core-m2.md` sections 2.7, 2.9 and 4; design 6.1.2.

Deliver: the declarations of 2.1 and the comments of 2.2 as printed (H6-1 to H6-6 and H6-10 to H6-42; H6-19,
the last bullet and the "Returns" paragraph of the `Park()` comment, is among them); what
2.4 lists, in this order of commits: the headers with the stubs (the stub `SetBreakpoints` keeps the set, for
`GetBreakpoints`); the wiring of the core with the selftest expectations (`core.tick`, and the split `Check`
of `core.tables` with the hand-over check that runs when `TargetSteps` is true); the panel-id list; the
registration lines (2.4 item 4: `debugger/guest` added behind `system`, and its `CMakeLists.txt` sets the
emulator path, the skip reason and python for the subdirectories that come); the two leftovers of J1, each
on its own; the repository's copy of this file with its two rows (2.4 item 6), on its own. 2.3 is not yours:
E6b adds those declarations. You edit no text of the design: its Appendix B gains `dbg_arm7.h` at M2G, and
until then the header is frozen by this file.
Run the ARM7 half of `core.tables` once yourself with `TargetSteps(Target::Arm7)` forced true in a scratch
build: it is the only way it runs before E6b.

Gate: 3.2 "Gate". The wire does not change: capture `debugger.describe` and one full tick of a headless
emulator before and after, and compare the bytes but for `tickId` and `timestamp`. Both captures come from
builds that embed no UI bundle (A.0): `ui` of describe is the version of whatever bundle the configure
found, and a `dist-native` beside one of the two trees would show as a change of the wire.

Report also: the diff of every frozen header against section 2, and any sentence of section 2 you could not
apply as printed.

### A.3 E6a — the sounding guest and the sound lines

Repository `devcast`, cut from `skmp/nulldc3` after R6.

Read: `M2-ROUND.md` section 5 (all of it) and 3.3; `understand-m2/guest-harness.md` sections 1, 2.5, 5, 6.2
and 7; `tests/debugger/selftest/gen.sh` and `emu_guest.s` (the pattern); `tests/debugger/regs/aica/aica_test.cpp`
(the shortest recipe for a sounding channel); `hw/aica/aica_hash.h/.cpp`; design 5.6 (with rows 44 and 45 of
section 7: the guest is bare metal, not a KOS program). And in the UI repository, to read only:
`scripts/contract/disasm.ts` (what the contract runner takes for a control-flow line) and the `bl` rule of
`M2-ROUND.md` 3.1 item 3 — your guest is what both are run against, in gates of packages that may not touch
it.

Before you write anything: the observation of 3.3 item 6, on the tree as it is. It takes one debugger session
on 2ndmix (`NULLDC_ELF_DIR`).

Deliver: the six items of 3.3 "Delivers". The guest has every property of 5.2 by the names given there; they
are what E6b's selftests and neutrality targets, E7a's session and the contract's second run use. Three of
them are easy to leave out and carry the round. The folds into registers: the loop's of the timer count and
`SCIPD`, with its clear of `SAMPLE_DONE` behind it, and the handler's of L, the timer count, `lr` and `SPSR`
(without them the sound line cannot see the order inside a burst; without `lr` it sees two FIQ entries in
five). A `g_irq` that keeps every register and uses no stack. And an SH4 program that stores into no page
of its code, writes no `CCR` and leaves FPSCR as reios sets it, **with a `g_main_loop` that goes back by a
PC-relative branch (`bra`) and has no `jmp`, `jsr` or `rts`** — a later package writes the PC into another
mirror of main memory and expects the loop to stay there; on the ARM7 side you preload every address into
registers, on the SH4 side the way back must not come from one. Four more came with the review of round 3. **The
F window** of `a_main_loop`: at least four plain instructions between an `msr` that sets F (label `a_fwin`)
and one that clears it (label `a_fwin_end`), the instruction in front of `a_fwin_end` one of them, so that a
FIQ is also entered inside a sample. Count the entries in a scratch build of the emulator that
prints, for every entry, its tick, its return address **and the number of the `Step` of its `StepMany` call
whose FIQ test made it** (commit nothing of it), and put three numbers per configuration in your report:
made by the first `Step` (the first kind); by the second (inside a sample, with the `msr` of `a_fwin_end` as
the first instruction of its sample); by the third or a later one. **Your gate is on the last: at least 100
in 2000 ticks, in each configuration, and no stretch of more than 32 ticks without one** (report the
longest); else widen the window, before the baseline. The next package's two tests can count only that
class — under steps an `msr` that starts its sample is stepped in a new phase —, and where your loop stands
when the timer overflows is no uniform phase: every sample starts at 0 ticks and drops the overshoot of the
one before, so it is the orbit of a deterministic map and can stay on few positions of the loop. A wider
window is a longer loop too and moves it. **Do not count
by the return address:** an entry by the first test of a sample that starts behind the `msr` has the same
one, `a_fwin_end` + 8, and is of the first kind; a run has 150 to 200 of those, so a count by address is
met with no entry inside a sample at all. An entry that is not made by the first `Step` and has another
return address is a finding for your report.
**The two bounds**: at most 32 instructions from anywhere in `a_main_loop` or `a_leaf` to the next
`bl a_leaf`, at most 48 from the FIQ vector to the return of `a_fiq`. **No literal load in the loops**: the
emulator prints `ldr rN, =addr` as `ldr rN, [pc, #..]`, and the runner takes every line that names `pc` for
control flow; `a_reset` loads the addresses into registers. And **look at the lines as the emulator
disassembles them**, not as objdump does: a debugger session on your guest
(`state.getDisassembly {arm7}` and `{sh4}`), whose output for the two loops and the handler goes into the
report. The key-on stays with the SH4, in front of the release of the ARM7: in batch
mode the guest then sounds also when its ARM7 is never released, and that is as it should be (3.3,
Verifier). You need the sh-elf binutils (`SH_ELF_BIN`) and an arm-eabi binutils (`DC_ARM_BIN`) to generate;
nothing else may need them. `SHA256SUMS` covers the six files 5.3 names and no other.

The two engines: when you have the first lines, compare the JIT's and the interpreter's. If `samples`, `regs`
or `arm7` differ, stop before the soak and report where they part (3.3 item 3); the integrator decides what
`engines` compares.

The runner's mode *neutral* and the function `nulldc_sound_neutral` are written by you and first used by the
next package, whose session does not exist yet: 3.3 item 2 says to the letter what such a run has to end
with. A session run ends in the harness' `_Exit` or at the exit key and prints no summary line, so the rules
of the mode `session` of `tests/debugger/selftest/selftest.py` do not fit it; those of its `mode_same_hash`
do. Show the mode on a session that exists. The function is first called from another directory by the next
package: it may use only what `tests/debugger/guest/CMakeLists.txt` sets (2.4 item 4), `nulldc_tests_path`
and cache variables, never a variable your own `CMakeLists.txt` sets.

You own and must not touch: 3.3, with its one exception. A fix under that exception is a commit of its own
and is reported before you go on; if it moves a state hash or a line of 2ndmix, stop.

Gate: 3.3 "Gate".

Report also: the lines of the guest in the four configurations and whether the two engines agree (which
fields); the outcome of the soak and the memcheck run; the observation; the FIQ entries counted
in each configuration by the number of the `Step` that made them (1, 2, 3 or more), the longest stretch of
ticks without one of the last class, and the return addresses of the entries not made by the first; the
emulator's disassembly of `g_main_loop`, `a_main_loop`, `a_leaf` and `a_fiq`,
with the two bounds counted in it; the mode of the ARM7 that `a_main_loop` runs in (system, or one with an
SPSR: two later packages write the CPSR there); the commit on which the baseline of 5.5 is to be taken.

### A.4 E6b — the resumable burst and ARM7 run control

Repository `devcast`, cut from `skmp/nulldc3` after E6a. **Before your first change: take the baseline of
`M2-ROUND.md` 5.5 on that commit.**

Read: `M2-ROUND.md` section 2 (H6-1, H6-3, H6-5, H6-17, H6-19, H6-21, H6-24, H6-38 and 2.3 are your
specification), 3.0, 3.3 (the runner's *neutral* mode), 3.4, 5.2 and the decisions M2-D1 to M2-D15, M2-D43,
M2-D49 to M2-D51, M2-D57, M2-D59 to M2-D61, M2-D66 and M2-D67;
`understand-m2/sound-burst.md` whole (its section 2 is the instruction boundary, 4.4 the park loop as it is,
7.1 the ten ways to move a hash); `understand-m2/headers-m2.md` section 5 (Q9 to Q11); design 2.2.3, 2.2.4,
2.4, 2.5, 5.7, each with its rows of section 7 — design 2.5 as it stands has the burst state of M3, a step
of the ARM7 as a resume, and `-32012` for `breakpoints.add`: none of the three is what you build (A.0).

Deliver: the six items of 3.4 "Delivers". The shape, for orientation (the texts of section 2 decide):

```
AicaUpdate:    verify(!burst.suspended);
               burst = { arm_on: the ARM7 is clocked (read once), i: 0, ticks_done: 0 };
               if (!burst.arm_on && dbg_arm7_armed != 0) dbg_arm7_OnBurstUnclocked();
               RunBurst(true);  return AICA_TICK;                // also when it was suspended
RunBurst(c):   if (burst.arm_on) for (; burst.i < 32; burst.i++) {
                   if (!scpu->RunSample(256, &burst.ticks_done, c)) { burst.suspended = true; return false; }
                   burst.ticks_done = 0;  libAICA_TimeStep();
               }
               burst.suspended = false;  aica->Update(32);  return true;      // the tail: once, on every path
debug loop:    for (;;) { StepIrq(ctx);
                          if (checks && dbg_arm7_armed != 0 && dbg_arm7_Check(next pc)) return false;
                          done += StepExec(ctx);  if (done >= ticks) return true; }
```

What must not move, because a hash folds it (`sound-burst.md` 7.1): the burst's state lives outside
`Arm7Context` and no slot is added to it; no `CPUUpdateCPSR` is added; `reg[15] = armNextPC + 8` stays between
the FIQ entry and the fetch; the tick test is behind every instruction and the overshoot of a sample is
dropped; `ticks_done` is kept across a suspension; `arm_on` is read once per burst (held by reading: nothing
of M2 can change the ARM7's reset state inside a burst, so no test can see the latch; do not add one that
calls `SetResetState` in a suspended burst, M2-D57); `settings.aica.NoBatch` is still read where it is read
today; the
tail runs once per burst, also with the ARM7 held and also behind a completion in the park context. For
arm64, by reading: the debug loop leaves `CYCL_CNT` alone, and nothing keeps an `ARM7Backend*` across a thread
start.

What must not cost, because every run pays it (2.3, the host-cost bullet of the gate): nothing is added per
ARM7 instruction to the path `StepMany` runs. `Step` split into two calls per instruction is the mistake to
avoid. Before you build on the split, compare the disassembly of `ARM7Backend::StepMany` and `Step` with the
base (check (a); `agents/nulldc3/j1/scripts/fn_disasm.sh` is the method, with the two symbols in place of
its list): leaving the source of the two functions alone, with `StepIrq` and `StepExec` as functions of the
debug loop only, gives equal code by construction. A whole-run instruction count cannot tell a cost per
instruction from the allowed cost per sample, so it is the second check, (b), and not the first; its counter
is `agents/nulldc3/j1/scripts/icount.c`, copied to your work directory, with a driver script of your own
(relative paths; the run ends by `Debug.AicaHash`).

If you leave `Step` alone, nothing that exists runs your two halves: every suite of `tests/arm7` steps
through `arm7m_step()`, which calls `Step`. So `arm7m_step()` runs `StepIrq(ctx); return StepExec(ctx);`
when the environment variable `ARM7M_STEP` is `halves`, and you register the six suites a second time with
it (`arm7.vendor.halves`, `arm7.directed.halves`, `arm7.wren7.<suite>.halves`; same conditions and the same
`baseline.txt` as their twins). They are the gate of the halves, in either shape.

The counts (2.3, `SoundBurstCounts`): bursts started, ARM phases that ran to their end, time steps, tails.
They live in the burst struct, in every build, outside the save state; two increments per sample are inside
the 48 of check (b). `arm7.burst.plain` and `idlebp` read them; your private header cannot serve here,
because the calls they count are made in emulator code, which includes no debugger header but `debugger.h`.

The pending FIQ is looked at in front of every instruction, also in the loop that goes on with a suspended
sample — the shape above does both, since a suspended phase enters the same loop. The guest has a place
where that shows: the F window of `a_main_loop` (5.2). A step from the `msr` that ends it, with a request
waiting, ends at `0x1C` (`arm7.step.fiqbehind`, and the fourth kind of step of `steps`). **Both tests take
such a step for an entry inside a sample only when it stayed inside one ARM phase**: `out[1]` of
`SoundBurstCounts()` reads the same at the stop in front of the step that arrived on the `msr`, at the stop
on the `msr` and at the stop of the step. When the `msr` ends its sample, or starts one, the same stop with
the same `R14_fiq` comes about in a new phase and says nothing about the loop that goes on (about once in
ten bursts; M2-D66). So `arm7.step.fiqbehind` sets its breakpoint on the instruction in front of the `msr`
(`a_fwin_end` - 4), steps twice at each stop, counts the pair by that condition and asserts nothing of a
pair it does not count. What the two tests count are the entries that the third or a later FIQ test of
their sample makes in the plain run, and the guest's gate was on exactly those: at least 100 in the 2000
ticks, no stretch of more than 32 ticks without one (3.3; E6a's report has the numbers). If your session
meets no counted pair at `0x1C` in 64 bursts all the same — the tests in front of it hold, release and
write the ARM7, which moves the phase —, report it with those numbers; do not loosen the count.

Both settings: the session `arm7` is registered three times (3.0), the third with `aica.NoBatch=yes`. That
is the path that stays when the batch render goes: reset, host stop and exit on a suspended burst, the
same-slice cases and the time step run on in the park context have to hold there as in the default.

In the park loop (`dbg_emu.cpp`), with H6-17:

- a step of the ARM7 that waits is a state no path has today: the SH4 runs with the stop in force and "a
  step is executing" set across the return of `Park()` (today `DoStep` sets and clears it around one call).
  It ends at the next `Park()`, whatever that finds; a host stop, a guest exit and a guest reset during the
  wait each have a test of their own (`arm7.step.wait.*`), and so have the two calls of the grace expiry
  queued during it;
- the wait counts as "a step has executed" for the resume a thread exit leaves owed;
- a request of the machine in the tick in which a check suspends reaches `dc_run` with a burst suspended and
  no `Park()` in between (`arm7.sameslice.*`);
- `arm7::PrepareResume()` goes wherever a leave can let the ARM7 execute — a resume, an SH4 step with or
  without a burst suspended, the first `Park()` with a resume owed — and not in front of a step-over on a
  `BL` or a step-out of the ARM7; the four conditions of the exemption and its five ends are in H6-5, and
  each has a case in `arm7.bp.resume` or a mutation of the verifier. Two edges of that test are built so
  that a wrong exemption can be seen at all (3.4, Gate). One needs the ARM7 held while it stands on address
  0, which a release that is held again before a burst gives: held anywhere else, an exemption that was
  wrongly armed is never used. The other adds its breakpoint behind the leave and in front of the next
  burst, by a table call that a thread of the test queued while the test still held the stop: the first
  check that is made ends an exemption, so a wrong one shows only when that check is at its own PC;
- what `Park()` returns is H6-19: `PARK_RUN` with `g_parked` still set for a step of the ARM7 that waits,
  `PARK_EVALUATE` behind a step in which the SH4 executed;
- the stop's half of the notice needs a flag set at the leave (3.4 item 4): `arm7::OnStop()` disarms a step
  in front of the publication. It cannot run on this host; write it so that it reads true;
- the order of the core's grace expiry (mask first, release second) is what makes a release on a suspended
  burst safe and is not yours to change.

The neutrality targets: their three tests are sessions that never return true (the run ends them), and each
prints the `ok` lines its target requires. A test that arms nothing must fail its target; try it. The count
of checks they read is in your private header, not in a frozen one; the bursts it is measured against are
`out[0]` of `SoundBurstCounts()`. `steps` has four kinds of step, the fourth at least five times: a step
from the `msr` that ends the F window which ends at `0x1C`, counted only when `out[1]` is unchanged over it
and over the step in front of it (above).

The register writer (H6-38): the CPSR goes through the core's switch, called as the guest's `MSR` calls it
(`saveState` false), and you add nothing to it. Leaving
user or system mode the switch copies the CPSR as it was into the live SPSR, which the row SPSR reads, and
entering a mode that has an SPSR it loads that mode's slot over it. So behind a write made in one of those
two modes the row SPSR reads the CPSR as it was before the write when the mode written is one of the two as
well — also behind a write of the value the CPSR has —, and what the row `SPSR_<mode>` of the mode written
read otherwise, which it keeps when the CPSR is written back (the table under H6-38 has the four cases).
That is the
writer being right (M2-D67). Do not pass `saveState` true to make the row read the old CPSR in every case:
it overwrites the SPSR of the mode entered, and the write back stores that into the slot. In
`arm7.setreg.*` no assertion says "the other rows are unchanged behind a
CPSR write" without that exception, and SPSR is asserted by value, never as "changed". The test makes both
writes from system mode itself: the CPSR set to mode `0x1F`; written again with mode `0x1F` (the value it
has will do), where SPSR reads the CPSR as it was before that write; then `SPSR_irq` written with a value of
yours and the CPSR written with mode `0x12` and back to `0x1F`, where SPSR reads your value both times,
`SPSR_irq` reads it still and every other row is as in front of the pair; then the restore. (Behind the
write into mode `0x12` the rows SPSR and `SPSR_irq` are one register: compare with the value you wrote.)
5.2 does not fix the mode the guest's loop runs in — system, or a mode with an SPSR; E6a's report names
it.

You own and must not touch: 3.4. You never open `dbg_core.cpp`: `TargetSteps` is the switch. In
`tests/debugger/contract/` you add the option `GUEST` and the mode `parts` (3.0), which hands `--parts` and
`--require-note` to the runner and takes its exit code: it reads no note itself. In
`tests/debugger/selftest/CMakeLists.txt` you add only lines (`ARGS --guest` takes the guest of the tree: no
function change). No test of yours that the run ends is named `arm7.<x>` (3.0): that is why the three of
the neutrality targets are `arm7n.*`. Of the guest's directory you own the runner's *neutral* mode and the
function `nulldc_sound_neutral`, and none of the six files of its manifest.

Gate: 3.4 "Gate", all of it; the neutrality targets, the before / after (the `-DNULLDC_DEBUGGER=OFF` build
included) and the host cost are the heart.

Report also: the baseline file's place; for every test of the session `arm7` one line on what it holds; the
diff of check (a) (empty), the instruction counts of check (b), base and tip, and the speed figures; which
shape of 2.3 you took, and the results of the six `.halves` suites; what you read for arm64.

### A.5 E7a — writes and reset

Repository `devcast`, cut from `skmp/nulldc3` after E6b.

Read: `M2-ROUND.md` section 2 (H6-31 to H6-35, H6-37 and 2.5 are yours to implement; H6-5 `OnPcWritten`,
H6-25 and H6-38 are what you call and test), 3.0, 3.1 item 3 (the probes your core has to answer), 3.5, 5.2
(the guest your session runs on) and the decisions M2-D18 to M2-D23, M2-D41, M2-D42, M2-D48, M2-D49, M2-D62,
M2-D67, M2-D68;
`understand-m2/core-m2.md` sections 1 to 3 (where each method plugs in, the error precedence of the
dispatcher), `devices-m2.md` sections 4.1, 4.10, 6 and Appendix F (the writer of `dbg_mem.cpp` region by
region), `headers-m2.md` 1.3 to 1.5; design 2.3.7 items 2 and 6, 2.7, 2.8 (the paragraph on `dc.aica.arm7`),
2.9, 3.3, 3.5, 3.6, each with its rows of section 7 — as they stand, design 2.7 says "4 KiB page", 2.8 puts
the ARM7's PC "into `R15_ARM_NEXT`" as given, and 3.3 and 3.5 have the old error order: H6-33, H6-35 and
M2-D20 are what you build (A.0).

Deliver: the five items of 3.5 "Delivers". Points to hold on to:

- A capability string, the row of its method and the writer under it arrive in one commit: a describe that
  advertises a method which answers `-32004` fails the contract.
- A method checks what the request alone shows first (`-32602`), then makes one `CC_WRITE` call; the lifetime
  answers are that call's; the method's own errors are decided inside the closure, in the order of M2-D20. A
  successful write broadcasts a tick inside the call; `control.reset` broadcasts none.
- The value parser keeps the low 64 bits of a text of any length (decimal as well as hex).
- `regs::Write` for the ARM7's PC calls `dbg::arm7::OnPcWritten()` after the device accepted it; for the SH4's
  PC `dbg::sh4::OnPcWritten()`, after it refused an odd value. Neither forgets the stop: a PC written back
  onto the address of the stop resumes through a breakpoint there, on both processors, and your tests say
  so (`writes.reg.pc`, `writes.reg.arm7`). You do not open `dbg_sh4.cpp` or `dbg_arm7.cpp`. The one thing
  only `sh4::OnPcWritten()` does is drop the owed remainder when the PC is written with the value it has
  (for any other value the remainder goes without it): `writes.reg.pc` asserts exactly that, with
  `dbg_sh4_TakeRemainder`. For the same reason a PC write is not neutral, and `writesrun.neutral` writes
  none.
- A register reads back what was stored, not what was written (H6-33): SR under `0x700083F3`, and with RB 0
  when MD is written clear (`UpdateSR` prints a line then); FPSCR under `FPSCR_MASK`; the ARM7's CPSR under
  `0xF00000DF`; the ARM7's PC aligned. "Written, read back" in your tests is under that mask.
- An SR write that lets an interrupt in calls `UpdateINTC()`, which under the latch only records it.
  `UpdateSR()` answers whether an interrupt is pending and let in now, not whether your write made it so:
  at a code breakpoint of the SH4 a write of SR, also of the value it has, moves the acceptance of a
  pending interrupt from the next slice end to the leave, as the guest's own `LDC` does. That is the
  writer being right; a neutrality test writes SR at stops latched at a slice end, where the acceptance is
  owed already (M2-D62).
- An FPSCR write that changes PR, SZ or the rounding mode calls `SuperH4::ResetCache()` (M2-D41), and so
  does an accepted write of the SH4's PC that differs from the PC it replaces (M2-D42): the block cache is
  indexed by `(pc >> 1)` under a mask of 32 MiB with no address in the key, and a resume from a PC written
  into a mirror a multiple of 32 MiB away (`0x0C…` or `0xAC…` for `0x8C…`) would run the other mirror's
  block. Both are that one call; you ask the block manager
  nothing, and a PC written with the value it has resets nothing. (Unless M2-Q4 was answered the other way:
  the integrator tells you, and you then lose both calls, the cache half of `writes.reg.fpscr` and the
  mirror case of `writes.reg.pc`.)
- **The FP witness has a direction.** Only a block compiled with PR = 0 has an FP mode in its code: with PR
  set the decoder emits every FP instruction as a call of the interpreter's handler (`dec_generic` of
  `hw/sh4/dyna/decoder.cpp`), which reads FPSCR when it runs. Compile the witnessing block with PR = 0 and
  SZ = 0 — the guest's FPSCR, `0x00040001`; check it and say so in an `ok` line — and take the witness after
  PR was written to 1; then, with PR back at 0 and the block compiled again, after SZ was written to 1. A
  test in the other direction passes with no cache reset. The rounding mode has no witness on this host
  (`float FPUL,FRn` is one host instruction either way): do not write a test for it.
- The CPSR of the ARM7 goes through the mode switch, which rewrites the bank slots of the mode it leaves,
  copies the CPSR as it was into the live SPSR when that mode is user or system, and loads the SPSR of the
  mode it enters when that mode has one (H6-38, with the table of the four cases under it): written and
  written back, every row reads as before, but SPSR when the write is made in user or system mode, and the
  save state can differ. So `writesrun.neutral` writes R0 to R12 of the ARM7 and not its CPSR. And in
  `writes.reg.arm7` no assertion says "the other rows are unchanged behind a CPSR write" without that
  exception, and the row SPSR is asserted by value, never as "changed". Written in user or system mode
  with one of those two modes, it reads the CPSR as it was before the write, also behind a write of the
  value the CPSR has, and behind a restore that stays in the two modes it reads the value written in
  between. Written from one of the two into a mode that has an SPSR, it reads what the row `SPSR_<mode>`
  of the mode written read in front of the write — read that row first: behind the write the two rows are
  one register —, and keeps that value behind the restore. Written in a mode that has an SPSR and
  restored, every row reads as before. That is the core's switch, which
  the guest's own `MSR` makes too (M2-D67); which mode the guest's ARM7 loop runs in is not fixed (system,
  or a mode with an SPSR: E6a's report names it), 2ndmix's
  is not known, and you may touch neither the guest nor the body of the writer.
- **A witness of the block cache is taken only from code that the dispatcher enters from the cache** (3.5,
  the bullet of that name; M2-D48). A step proves nothing about the cache, and neither does a resume behind
  a breakpoint stop: the check that stops leaves the rest of its block owed, and that rest is decoded from
  memory, in the FPSCR of the moment. So the stop in front of the witnessing resume owes no remainder (an
  event breakpoint or a pause: the PC stands at a block start), or the written instruction lies in front of
  the breakpoint's own instruction in its block and is read at the next arrival there; and from the write to
  the witness you add, remove or toggle no SH4 breakpoint of that page, because arming discards its blocks.
  That holds for the FP mode behind an FPSCR write (`writes.reg.fpscr`), for the discard of a main-memory
  write (`writes.mem.cached`) and for the block a resume enters behind a PC written into the `0x0C…` mirror
  (`writes.reg.pc`).
- **The mirror case needs a mirror that shares its slots, and a loop that stays in it** (M2-D68). `0x0C…`
  and `0xAC…` are a multiple of 32 MiB from `0x8C…` and share its slots of the block cache. `0x8D…` is the
  same memory 16 MiB up, in other slots: a PC written there finds no block, and your case passes with no
  reset — do not use it. The guest's `g_main_loop` goes back by `bra` and takes the PC from no register,
  and `g_irq` returns by `rte` (5.2): with the reset the loop stays in the mirror you wrote, without it the
  block compiled at `0x8C…` takes it back there. Take the witness from a stop whose PC is in the loop, not
  in `g_irq`, which runs at the address `VBR` gives it in either case.
- In the default configuration nothing can see a store without the discard: the fault handler makes it good
  on a protected page, the block's own check on a data page. `writes.mem.cached` is therefore registered a
  second time, as `writes.smc`, with `config:Dynarec.SmcCheckLevel=-1` (`NoCheck`; 0 is the default,
  `FullCheck`), on the JIT. Say in your report that the default run of the test cannot fail for a missing
  discard.
- The main-memory writer has four combinations (the layout in use or not, a recompiler or not). Three run
  on this machine: the default build, `-DTARGET_NO_REC=ON` (no recompiler, the layout in use) and the generic
  host (no recompiler, no layout); run `writes.mem.*` and `writesrun.neutral` on all three, the last two
  through `NULLDC_EMULATOR`. The fourth, a recompiler without the layout, has no configuration: it is read
  by you and run by the verifier in a scratch build.
- The session `writes` runs on the guest of E6a, registered by the two lines of 3.0, with the line
  `writes.smc` beside them; it needs no `NULLDC_ELF_DIR`. No test named `writes.<x>` is one that the run
  ends: the filter `writes` would select it and the session would never print its summary. The neutrality
  test is `writesrun.neutral`, a registration of its own in the mode `same-hash`.
- The runner's write rules meet their first emulator in your gate. They ran against the mock, which has the
  effects they look at; if a rule is wrong about the emulator all the same, report it with the frames. You
  do not edit `dreamcast-devtools`.
- Your four parts of S16 also run against the guest, on the JIT and the interpreter
  (`debugger.contract.m2writes.guest`, `.interp`: the option `GUEST` and the mode `parts` that E6b added,
  with the two required notes). The write side is where the guest differs from 2ndmix — the stack at the
  top of main memory, the 64 KiB written back over its own code, the banked counters of `g_irq`, the reset
  of a program reios loads again —, and the next package may touch none of your files.
- `core.reset` also resets at a breakpoint stop (the stop of the reset names no breakpoint) and with a
  breakpoint, a watch and a muted category listed (they are behind the reset what they were in front).

You own and must not touch: 3.5.

Gate: 3.5 "Gate".

Report also: for each of the four combinations of the main-memory writer what was run and what was read; the
answers your core gives for the probes of 3.1 item 3 (they are what F3a's runner asks).

### A.6 E7b — the M2 rows of the device tree

Repository `devcast`, cut from `skmp/nulldc3` after E7a; the UI checkout pinned by the integrator (section 4).

Read: `M2-ROUND.md` 3.6 with 3.6.1 and the decisions M2-D24 to M2-D30; `understand-m2/devices-m2.md` sections
2, 3, 4 and Appendices B, C, D, G, H (the registers, how each is read without a side effect, and the pointer
in the read-only slots, 3.2 last item); `core-m2.md` section 7; design 2.8, 2.9, with rows 29 to 32 of
section 7 (the tree of 3.6.1 is the contract, not the table of design 2.8 as it stands: A.0).

First: the one run that shows what a never-assigned read-only system bus slot reads (`SB_MST` for one), then
the fix as a commit of its own with the state hashes held.

Second: the measurement. Build the channel rows for one channel, generate the 64, and measure a full tick
with empty tables (bytes, and the time to build it on the emulator thread, on a quiet machine) before the
tests are written around the table. Over 192 KiB, or over 5 ms: the channel nodes carry their 18 words
instead (M2-D27); say which it is in the first lines of your report.

Deliver: the five items of 3.6 "Delivers". The rows are generated where they repeat (a macro per channel, as
the tables do for the SH4's rows). A reader never goes through a guest read function: the AICA's file through
`AICA::DebugReadRegs`, the system bus through `SystemBus::DebugRead`, the PVR file from its array, the drive
through `gdrom_DebugGetState`, the clock through `libAICA_DebugGetRTC`. No row is writable. In
`dc.aica.common`: `SCILV0-2`, `SCIRE` and `MCIRE` are rows with `kRegNoReader` (`"n/a"`: the accessor gives 0
for the first three whatever is stored); the file's words at `0x2D00` and `0x2D04` are not rows; `L` and `M`
come from `SoundCPU::DebugGetInterrupt` (M2-D28).

You own and must not touch: 3.6. In `tests/debugger/contract/` you add the mode `m2` and your targets; its
second run passes the two required notes to the runner with `--require-note`. In `dbg_selftest_core.cpp`
you own the expectations that follow the tree and nothing else: with your rows the tree has more than 256
registers, so `core.tables` takes its other branch for the first time (the call that would pass the bound of
256 watches is refused whole) and the runner's S08 makes its bound probe against the emulator. The session
`core` has to pass that way; a defect of S08 itself is reported, not worked around.

Gate: 3.6 "Gate". It ends with the M2 gate of the contract in both runs, on three engines; the second run
requires its two notes. The four other parts of S16 have passed against the guest in E6b's and E7a's gates:
what your second run adds is the `tree` part, in which each register-panel id has to be the action of
exactly one node of your tree.

Report also: the number of nodes and rows; the measured tick; the rows that read `"n/a"` and why; the result
of the hash with a listening client.

### A.7 F3b — the end of protocol v2

Repository `dreamcast-devtools`, branch `skmp/nulldc3`, behind F3a.

Read: `M2-ROUND.md` 3.7 (its table is what stands in place of each v2 branch) and M2-D39 (its sentence on a
server without `protocol`), section 4 (why your gate has the emulator's contract targets in it);
`understand-m2/devtools-m2.md` 1.6 (the table of every site, and the two things that look like v2
compatibility and are not); design 3.0, 4.4 (4.4 with row 39 of section 7: A.0).

Deliver: 3.7 "Delivers". Remove; do not redesign. The built-in target values stay for two uses, and get names
that say so: a page that has had no server yet, and a target that a v3 server does not list. One client
schema changes (`state.getSgcFrameData`); `always()` stays, the strict copy keeps every shape, and
`scripts/contract/` and `server/` are not opened: the emulator's gate runs your source. Two things the
reader's table does not show. `mergeCategories` stays as it is: its "no `categories`, the stored states
stay" is the per-key rule of a v3 tick, and only the kept callstack of that table row goes. And
`e2e/support/syntheticDebugger.ts` is a v2-shaped server the table does not list (the search was in `src/`):
you make it a v3 server, or the perf spec measures a page with the banner on it.

Gate and verifier's question: 3.7. The orchestrator gives you an emulator build of the integration branch for
the contract targets; you change nothing in it.

### A.8 F3c — registers and memory

Repository `dreamcast-devtools`, branch `skmp/nulldc3`, behind F3b.

Read: `M2-ROUND.md` 3.8 and M2-D19, D22, D39; `understand-m2/devtools-m2.md` 0.2, 1.1, 1.2, 1.7 and 4.1; design
4.3 (the behaviour every panel keeps: not connected, no game, nothing requested that would be answered
`-32004`), 4.4 (with row 39 of section 7: A.0). In the repository: `DeviceTreePanel.tsx` and its model (the rendering model for thousands of
registers), `WatchesPanel.tsx` (the edit gesture and its refusal handling), `DisassemblyView.tsx` (the
mechanism for narrow panels).

Deliver: 3.8 "Delivers", with its six decided points (where the panels stand; the notice of a panel no node
offers, and the first node in tree order when several offer its id; editors while paused only; where the writable flag is read — the describe tree, never a tick — and
how a watch finds its register; the second counter that makes views read again in place, and the rule that
no action moves the view that follows the PC: a paused tick of the same stop with another PC does, in
`applyTick`, so that the order of response and tick does not matter; what the synthetic server gains). The
reset action is not yours: F3d adds it, and advances your counter from it. The pure parts (which node a panel shows, which rows are editable, what an edit
sends, the byte-edit model) are modules that vitest can test; what a component does is tested by the e2e
suite against the mock, which is an M2 server since F3a — and against the mock with
`supports: MOCK_M2_SUPPORT`, which is the server the emulator of M2 resembles: two nodes with editors, no more.

Gate: 3.8 "Gate".

### A.9 F3d — toolbar, reset, hotkeys

Repository `dreamcast-devtools`, branch `skmp/nulldc3`, behind F3c; `devcast` `skmp/nulldc3` at E7b or later
for the smoke spec.

Read: `M2-ROUND.md` 3.9 and M2-D39, D40, D42; `understand-m2/devtools-m2.md` 0.3, 1.3, 1.4; design 4.3, 4.4.

Deliver: 3.9 "Delivers", with its decided points (the first focused target of each of the four workspaces;
which step buttons the held ARM7 turns off; the help's sentence on what a write can do; the smoke spec's
variable and its steps). Take the run controls out into a component of their own first; the toolbar keeps its
room at 1280x720 with four more buttons. Your reset action advances the read-again counter F3c made, when it
is answered `{}`: a reset while the target runs publishes nothing, and that counter is all the fetched views
get (your gate has an e2e case for it). The smoke spec is skipped unless `E2E_EMULATOR_URL` is set, and is
not part of `check`; it is part of your gate.

Gate: 3.9 "Gate". For the smoke spec the orchestrator gives you the emulator's worktree to build with your
bundle embedded; you change nothing in it. `npm run build:native` makes `dist-native` in your checkout, and
the emulator is configured by itself with `-DNULLDC_DEVTOOLS_DIST=<your checkout>/dist-native` (A.0: the
option goes to the emulator's configure, never to the tests project; the page the emulator serves then
shows your UI, and `ui` of `debugger.describe` its version). The emulator is started as `tests/debugger/contract/contract.py`
starts one (design 5.4):
`-config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800 , Debugger:Enabled=yes ,
Debugger:Port=0 , Debugger:Required=yes , Debugger:StartPaused=yes tests/debugger/guest/dsptest/dsptest.elf`;
`E2E_EMULATOR_URL` is the `Debugger: http://...` line it prints. `Debugger:StartPaused=yes` alone ends a
headless run with exit code 2: the server is off by default there.

### A.10 M2G — the gate (integrator)

Both repositories at their heads, the pinned UI checkout (`agents/nulldc3/wt/ui-pin`) moved to the UI's
head. The emulators of items 1, 3 and 4 carry the UI bundle of that head, and the tests project cannot
build such an emulator (3.10): `npm run build:native` in the pin; the default build and the
`-DHAS_TA_LLE=OFF` build of the emulator configured by themselves with
`-DNULLDC_DEVTOOLS_DIST=<the pin>/dist-native`; both handed to the tests project as `-DNULLDC_EMULATOR=`
and `-DNULLDC_EMULATOR_TA_HLE=`. `-DNULLDC_DEVTOOLS_DIST` is not given to the tests project.

Run: the five items of `M2-ROUND.md` 3.10. For item 2 the tree before E6b is built again with this build's
settings and its lines recorded, so that both sides of the comparison are from one host and one compiler.

Hand over: the results; the documents of 3.10 item 5 — the design text changed by section 7 (73 rows;
decision 9 of `DESIGN-V3-DECISIONS.md` and the two sentences of `DESIGN-V3-OPEN.md` among them), the status
line of `PLAN.md`, and the copies under `docs/nulldc3/`: the design, the decisions, the open questions, the
plan, and this file as `devtools-integration-m2-round.md` in place of the copy R6 committed —; the
answers to M2-Q1 to M2-Q6 as they stand, and what each left for the cut of M3; for PLAN's backlog what
E7a's verifier found about the recompiler without the virtual memory layout (M2-Q6), and the block cache
without an address in its key (1.2); for the package that
wires the T5.0 core the list of 5.6 (the batch halves it removes).


### A.11 EXe — the leftovers of the emulator packages

Repository `devcast`, cut from `skmp/nulldc3` at `199cdb4a` (E7b merged); the UI checkout pinned at `6c753dc`.

Read: `M2-ROUND.md` 3.11 and Appendix I.4, I.5 and I.6 whole (every leftover and limit is described there with the
verifier's evidence); the logs and scripts the items name (`agents/nulldc3/e6b/verify-2/`, `e7a/verify-2/`,
`e7b/verify-2/`), whose mutants you run again; `tests/debugger/selftest/README.md`.

Deliver: the eight items of 3.11. Items 1 to 5 and 8 first, each a commit of its own with the mutant that it makes
fail shown failing in your report; then item 6, which may end as a known limit with numbers; then item 7, the
comment text, last. Mutation runs: at most two emulators at a time, each with a guard on its resident memory (kill
above 1.5 GB and record which mutant it was; `ulimit -v` does not work, the emulator reserves address space by
design).

Gate: 3.11 "Gate".

### A.12 EXu — the leftovers of the UI packages

Repository `dreamcast-devtools`, branch `skmp/nulldc3`, behind F3d (`6c753dc`).

Read: `M2-ROUND.md` 3.12 and Appendix I.7, I.8 and I.9 whole (I.8's decisions 1 to 6 are the rules your new
controls keep; I.9 says how the smoke spec is run); 3.8 and 3.9 with their decided points; the scratch specs the
items name (`agents/nulldc3/f3b/verify-1/logs/`, `f3c/verify-2/scripts/`).

Deliver: the seven items of 3.12. Item 1 first, with its decision said in the first lines of your report and in the
README; then 2 and 3; then the tests and words.

Gate: 3.12 "Gate".

---

## Appendix R. The five reviews

(Lettered R so that "Appendix B" in this file stays the design's, the list of frozen headers.) R.1 to R.4 are
the review of round 1, R.5 to R.8 that of round 2, R.9 to R.12 that of round 3, R.13 and R.14 that of
round 4, R.15 to R.17 that of round 5; the numbering of the findings
runs on (S10, W7, P15, I10 are the first of round 2; S15, W14, P26, I22 the first of round 3; S19 and W17
the first of round 4; S21, W19 and I27 are round 5).

**Round 1.** The first cut
was read by four reviewers, each through one lens. Every finding was checked against the two
source trees and the design. 38 were raised (9, 6, 14 and 9). All are taken, as raised or in a stated
variant; one part of two of them (S1, I1) is declined with its evidence. "Where" names the places of this file that changed; a rule that is
stated in several places was changed in all of them (the header text, the package, the decision, the design
row, the brief).

### R.1 Sound

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| S1 | E6b owns no file in which the twelve neutrality targets, or a session on the guest, can be made; the runner's hook is one clause | taken. E6a writes the mode *neutral* to a stated rule and the function `nulldc_sound_neutral`; E6b owns a new directory `neutral/`, and that mode and function from its turn on. **Declined:** an option `GUEST` of `nulldc_debugger_selftest`. The function hands `ARGS` to `selftest.py` unchanged (its `add_test` line) and `--guest` of that script takes any path (it is the option the `ELF` branch itself appends), so a session on the guest of the tree is one line with `ARGS --guest` | 2.4 item 4, 3.0, 3.3 items 2 and 3, 3.4 Owns / Must not touch, A.3, A.4 |
| S2 | the three neutrality targets pass vacuously | taken, with one change: `steps` cannot hold "500 steps, each kind 20 times" (a burst is more than a thousand instructions), so it steps until each kind was taken 20 times | 3.4 Gate and Verifier, M2-D43 |
| S3 | the sound line is not a sufficient witness: no field for wave memory, main memory, the scheduler | taken: the guest folds what its ARM7 observes into registers, and each target also holds the exit state hash at 90 vblanks | 5.2, 5.5, 3.3 item 2, 3.4 Gate, M2-D43 |
| S4 | neutrality with nothing armed is held for the default build only | taken | 3.4 Gate "before / after", 5.5 |
| S5 | nothing bounds the host cost of a run that uses no debugger | taken | 2.3, 3.4 Delivers 2 and Gate, A.4 |
| S6 | the wait of an ARM7 step, and a machine request in the tick of a suspension, have no test and no mutation | taken; H6-3 and H6-17 now say what is published in each case | H6-3, H6-17, 3.4 Gate and Verifier, A.4 |
| S7 | 2ndmix releases its ARM7 twice; a pause in the first window meets a PC above `addressMax` or a second hold | taken, merged with P7 into one settling rule; the observation moved to F3a | 3.1 item 3 and "the observation", 6.3, M2-D12 |
| S8 | the resume exemption leaves four cases open | taken, all four; and `dbg_arm7_OnBurstUnclocked` ends an exemption | H6-1, H6-5, H6-24, M2-D13, 3.4 Gate |
| S9 | `NOTICE_ARM7_FALLBACK` reads 0 in the stop of a fallback step | taken | H6-21, 3.4 Delivers 4, M2-D8 |

### R.2 Writes

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| W1 | an ARM7 PC stored as given can make the next fetch read past the mapping of the wave memory | taken: stored with bits 0 and 1 clear | H6-33, H6-38, M2-D12, 3.1 items 2 and 3, 3.4 and 3.5 Gate and Verifier |
| W2 | an FPSCR write leaves compiled blocks in the old FP mode | taken as the reset of the code cache; the alternative is M2-Q4 | H6-33, M2-D41, 3.5, section 7 rows 32 and 52 |
| W3 | the SH4's PC has no rule for its value | (a) taken: an odd PC is refused. (b) decided: a write that takes the guest to what is no instruction is accepted and stated in the help; the alternative is M2-Q5. The stress writes `MACH` | H6-33, M2-D20, M2-D42, 3.1, 3.5 Verifier, 5.2, 3.9 |
| W4 | "a PC written onto a breakpoint address stops there" does not hold for the stop's own address; the two processors were specified differently | taken: one rule for both, the SH4's as built | H6-5 `OnPcWritten`, H6-33, M2-D13, 3.5 Gate |
| W5 | the cut does not say which guest the session `writes` runs on, nor how the forced fallback and an ELF of the tree are registered | taken; the registration needs no function change (S1) | 3.0, 3.5 Gate, 5.2, A.5 |
| W6 | `SCILV0-2` as the SH4 reads them are always 0; the words at `0x2D00` / `0x2D04` are not the ARM7's interrupt controller | taken | 3.6.1, M2-D28, 3.6 Gate and Verifier, A.6 |

### R.3 Frontend

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| P1 | three runner rules cannot run on the mock | taken, the first way: the mock has the three effects | 3.1 item 2, M2-D45, A.1 |
| P2 | the PC-write and written-instruction rules fail a correct server and damage the guest | taken: one probe that executes an instruction the runner wrote, at the scratch address | 3.1 item 3, 5.2 (`g_irq`) |
| P3 | `target: "dsp"` is `-32602` by the schema the mock validates with | taken: one shape changes | 3.1 item 1, M2-D21, section 7 row 37 |
| P4 | the end of v2 does not say what stands in place of each branch, nor whether a schema changes | taken | 3.7, M2-D39, A.7 |
| P5 | the mock's M2 tree changes e2e specs F3a does not own | taken, with I8 | 3.1 item 2 and Owns |
| P6 | eleven broken servers the M2 gate passes | (b) to (g), (k) taken as runner rules, and the tick behind a write and the mirrors of main memory of (h) and (j); (a), (h), (i), (j) are named selftests with their mutations, since the runner cannot see them without running guest code out of sequence or writing into a running guest; the hand-over of (b) has a check in R6 through a getter of `dbg_arm7.h`. Of (f), "no width other than 32" has nothing to hold: every writable row of the M2 emulator is 32 bits wide (the mock's wider rows are the mock's) | 3.1 item 3, H6-5 `GetBreakpoints`, 2.4 items 1 and 2, 3.2 Verifier, 3.5 Gate and Verifier, M2-D38 |
| P7 | S16 at the anchor of 2ndmix flakes | taken, with S7 | 3.1 item 3 |
| P8 | the runner's reset rule needs a held ARM7 that the mock never has | taken | 3.1 item 2 |
| P9 | the UI packages are tested against a server no M2 emulator resembles | taken: `MOCK_M2_SUPPORT` | 3.1 items 2 and 4, 3.8, 3.9, M2-D45 |
| P10 | five points F3c would have to ask about | taken, all five | 3.8, M2-D39, A.8 |
| P11 | four points F3d would have to ask about | taken; F3d stays behind E7b | 3.9, section 4, M2-D40, A.9 |
| P12 | a UI package merged between two emulator packages changes the gate of the next one | taken as the pin, with I5; and F3b's gate has the contract targets | section 4, 3.7, M2-D44 |
| P13 | two findings that belong before the runner is closed are scheduled after it | taken | 3.1 "the observation" and Gate, 3.3 item 6, 6.3 |
| P14 | the content of the pairing invariant is not given | taken | 3.1 item 3 |

### R.4 Integration

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| I1 | as S1; and the contract function boots 2ndmix only; and nothing says how a session with the harness ends | taken as S1. E6b adds the option `GUEST` and the mode `parts` of the contract files, which do need a change. The same part declined as in S1, for the selftest function | 3.0, 3.3, 3.4 |
| I2 | three packages may not write their rows of `tests/README.md` and of the two test READMEs | taken | 2.4 item 4, 3.0, 3.4 to 3.6 Owns |
| I3 | the session has no test `core.methods` | taken: it is `core.tables`, and its one `Check` is split | 2.4 item 2, 3.2, A.2 |
| I4 | three comments of frozen headers become incomplete and have no item | taken: H6-40, H6-41, H6-42 | 2.2 |
| I5 | "the head" of the UI branch can be a package half done | taken | section 4, 3.7, M2-D44, A.0 |
| I6 | "all ctest targets green" can be met by tests that did not run | taken | the head of this file, A.0, every Gate, M2-D46 |
| I7 | two of the builds A.5 calls "read" run here; the generic host is not even compiled | taken | 3.2, 3.4 to 3.6 Gate, 3.5 Gate and Verifier, M2-D47, A.0, A.5 |
| I8 | as P5 | taken | 3.1 Owns |
| I9 | an odd PC of the SH4 is accepted and a resume from it ends the emulator | taken, with W3 (a) | H6-33, M2-D20, M2-D42 |

**Round 2.** The revised cut was read again through the same four lenses. Every finding was checked against
the two source trees and the design, by reading: nothing was built or run. 35 were raised (5, 7, 11 and 12),
four of them as must-fix (W7, W8, P15, I10). All are taken, as raised or in a stated variant. Three parts are
not taken as raised, each with its evidence: the value of the configuration key in W7 (the finding names 0,
which is the default), the test S12 offers for the `arm_on` latch, and the address W13 names for the large
write (P20 names another for the same write, and one had to be chosen).

### R.5 Sound, round 2

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| S10 | "silent in three ways, *sounds* fails each time, in both modes" is false for one of the six cases: in batch mode the channels the SH4 keyed on are rendered without the ARM7 | taken. The batch render steps the 64 channels in every callback and only the per-sample path is behind the ARM7's clock. The key-on stays with the SH4; "no key-on" also removes the one `a_fiq` makes | 3.3 Verifier, A.3, section 7 row 44 |
| S11 | the folds do not see a single displaced FIQ entry in most cases: the line folds the ARM7's state once per tick, `R14_fiq` and `SPSR_fiq` are overwritten by the next entry, and the loop's reads see an entry only when it crosses one of them | taken: `a_fiq` folds `lr` and `SPSR`, `a_main_loop` clears `SAMPLE_DONE` behind its fold (`TimeStep` sets the bit every sample; `SCIRE` clears it with a word access), and the resolution is stated | 5.2, 3.3 and 3.4 Verifier, M2-D43, A.3, section 7 row 44 |
| S12 | two mutations of 3.4 can be caught by nothing in M2 | taken. The tick test in front is an equivalent mutant and is replaced by two that differ. `arm_on` read per sample is dropped: the reset state cannot change inside a burst in M2. **Declined:** the offered test that calls `SetResetState` in a suspended burst (M2-D57) | 3.4 Verifier, M2-D57, A.4 |
| S13 | the host-cost gate has no number and no tool, and a count of a whole run cannot tell a cost per ARM7 instruction from the allowed cost per sample | taken: two checks. Added: leaving the source of `StepMany` and `Step` alone gives check (a) by construction, and E6b takes that shape if the other does not | 2.3, 3.4 Gate and Verifier, M2-D50, 6.3, A.4, section 7 row 57 |
| S14 | the gate of E6a demands five equal fields on the two engines, 6.3 says a difference is only reported | taken | 3.3 items 2 and 3 and Gate, 5.1, 6.3, M2-D56, A.3 |

### R.6 Writes, round 2

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| W7 | a writer that makes no discard passes `writes.mem.code` in every page state of the default configuration | taken, with one correction. **Declined:** the value `Dynarec.SmcCheckLevel=0`. `SmcCheckEnum` is `NoCheck = -1`, `FullCheck = 0` (`types.h`), and the default is `FullCheck`: a registration with 0 would be the default configuration again. The registration has `-1` | 3.0, 3.5 Gate and Verifier, 5.2 (no `CCR` write), M2-D38, M2-D48, 6.3, A.5, section 7 rows 61 and 67 |
| W8 | a resume behind a breakpoint stop proves as little as a step (the owed rest of the block is decoded from memory), and arming an SH4 breakpoint discards its page | taken | 3.5 Gate and Verifier, M2-D41, M2-D48, A.5 |
| W9 | nothing `writes.reg.pc` lists can see a writer without `OnPcWritten`; a PC write is not neutral | taken | 3.5 Gate and Verifier, 3.1 item 3 (the note on `PC` as first register), A.5 |
| W10 | the recompiler without the layout cannot resolve a fault in a page of compiled code and may not run a guest at all | taken; the guest's property is a decision, and the build is asked of the owner | 5.2, 3.3 Verifier, 3.5 Gate and Verifier, 1.2, M2-D58, M2-Q6, 6.3, section 7 row 66 |
| W11 | by its words H6-33 promises that a written value reads back; SR, FPSCR and the ARM7's CPSR store under a mask | taken; the CPSR's sentence also in H6-38 | H6-33, H6-38, 3.4 (`arm7.setreg.*`), 3.5 Gate, M2-D20, A.5, section 7 row 36 |
| W12 | design 3.5 (`-32602` against `-32013` for a refused value) and design 3.3 (what a running target is asked) stay contradicted after section 7 | taken: two rows | section 7 rows 59 and 60, M2-D20, M2-D22 |
| W13 | the 65536-byte write has no place, no content and no restore | taken as "read, and written back as read". **Declined:** the part's scratch address, which the finding names; the SH4's `defaultMemoryAddress` is taken, as P20 has it: 64 KiB inside one mirror of main memory on both servers, wherever the guest's stack is | 3.1 item 3, 3.5 Gate (contract), M2-D55 |

### R.7 Frontend, round 2

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| P15 | the `bl` rule cannot run on the mock: its ARM7 ring has no call, step-over is one step, step-out a resume to the wrap | taken | 3.1 item 2, Owns, Gate and Verifier, M2-D34, M2-D53, A.1 |
| P16 | `--require-note` has no owner and no channel | taken as an option of the runner, delivered by F3a; `contract.py` passes it through | 3.0, 3.1 items 3 and 4, 3.4, 3.6, M2-D37, M2-D52, A.1, A.4, A.6 |
| P17 | the mock has no wave-memory size, and both address rules need one | taken; for video memory the mirrors of a view agree and the two views are not aliased | 3.1 items 2 and 3, M2-D53 |
| P18 | `e2e/toolbar.spec.ts` writes an odd PC, which the mock now refuses, in a file F3a did not own | taken | 3.1 Owns, A.1 |
| P19 | seven broken servers pass both runs of the M2 gate | (b) to (f) taken as rules of the runner — the `CPSR` rule where the node lists the register with the flag, the S13 write while paused; (a) and (g) are named as held by `arm7.bp.resume` and by `core.reset`, which gains an assertion | 3.1 items 2 and 3 and Verifier, 3.5 Gate, M2-D38 |
| P20 | three things F3a would have to ask: the large write, the step-out sentence, how `check` starts the second mock | taken, all three (the first with W13) | 3.1 items 3 and 4, M2-D52, M2-D55 |
| P21 | "the kept callstack and categories" among what goes, against the table's "the stored states stay" | taken: `mergeCategories` stays | 3.7 Delivers and Verifier, A.7 |
| P22 | the synthetic e2e server is v2-shaped, no table lists it and nobody owns its change | taken: F3b makes it a v3 server, F3c gives it what the panel needs | 3.7 Delivers, Owns and Verifier, 3.8, A.7 |
| P23 | the cut does not say where `flags.writable` is read, nor how a watch finds its register | taken (the store's field is `deviceTree`; the `shape.deviceTree` of the finding is a local of the store) | 3.8, M2-D39, section 7 row 39, A.8 |
| P24 | advancing `refresh` when the request is answered races the tick | taken: no action advances it; a paused tick of the same stop with another PC does | 3.8 and its Verifier, M2-D39, section 7 row 39, A.8 |
| P25 | F3c is told to advance its counter "by reset", an action F3d adds | taken | 3.8, 3.9 Delivers and Gate, M2-D39, A.8, A.9 |

### R.8 Integration, round 2

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| I10 | `writes.neutral` is selected by the filter `writes`, never returns, and the session never prints its summary | taken: `writesrun.neutral`, and the rule for every session | 3.0, 3.4 Gate, 3.5 Gate, M2-D49, A.4, A.5, section 7 row 61 |
| I11 | a new worktree has the two vector submodules empty; the UI bundle is no look-up that misses | taken; the two paths are named first, the submodule fetch as the other way | A.0, M2-D46, section 7 row 57 |
| I12 | "speed by CPU time within 1 %" cannot be decided on this machine; no build and no tool is named | taken, with S13 | 3.4 Gate, M2-D50, section 7 row 57 |
| I13 | every functional gate of the burst machine runs in the default setting only, the batch path PLAN removes | taken: a third registration line, the verifier's stress once per sample, and who removes the batch halves | 3.0, 3.4 Delivers, Gate and Verifier, 5.6, M2-D51, A.4, section 7 rows 45 and 61 |
| I14 | the smoke spec's emulator, started as written, ends with exit code 2 | taken | 3.9, 3.10 item 4, A.9 |
| I15 | E7b does not own `dbg_selftest_core.cpp`, whose `core.tables` takes a new branch with E7b's rows | taken | 3.0, 3.6 Owns, Must not touch and Gate, A.6, section 7 row 63 |
| I16 | A.2 waits for the answer to M2-Q4, 6.2 says work proceeds | taken: R6 does not wait, and what a late "no" costs is fixed | 2.2 (H6-33), section 4, 6.2, 6.3, M2-D54, A.2, A.5 |
| I17 | H6-5 names an accessor for the mask of the wave memory that does not exist | taken | H6-5 |
| I18 | F3a's last gate line leaves out `hash.idlebp`; two more targets execute UI source | taken | 3.1 Gate, section 4, A.1 |
| I19 | a function of `dsptest/` called from `neutral/` does not see the variables of its own directory; where `guest/` is added is not said | taken | 2.4 item 4, 3.0, 3.3 item 3, A.2, A.3 |
| I20 | the cut does not say which files the manifest covers | taken | 3.3 item 1, 5.3, A.3, A.4 |
| I21 | five rows are missing in section 7 | taken: rows 61 to 65; rows 59, 60, 66 and 67 come with W12, W10 and W7 | section 7, 3.10 item 5, A.10 |

**Round 3.** The cut as revised twice was read a third time through the four lenses. Every finding was
checked against the two source trees and the design, by reading: nothing was built or run. 18 were raised
(4, 3, 6 and 5), all as should-fix. None is wrong: all are taken, as raised or in a stated variant. Five
parts are not taken as raised, each with its evidence: the mechanism W14 offers for a written PC (a test of
one slot of the block cache); the limit "a CPSR that changes the mode" of W16 (the switch is made also when
the mode stays); the numbers of P26 (64 steps an arrival with bounds of 24 and 32: taken as 96 with 32 and
48); one mutant of S15, which is equivalent in the narrow reading of its words; and the edge S17 leaves in
place for the pair it proposes, which does not catch the pair.

### R.9 Sound, round 3

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| S15 | on the guest of 5.2 a FIQ request is raised only by a time step, so every FIQ is entered by the first FIQ test of a sample: the verifier's sentence cannot be met by two of the three neutrality targets, and a debug loop that looks at the pending FIQ once per sample passes every gate of E6b | taken, the first way: the guest gets an F window (`MSR` works out the pending FIQ again: `arm-new.h`, the two `MSR CPSR` cases, through `CPUUpdateFlags`), E6a counts the entries made behind it, `steps` gets a fourth kind of step, the session `arm7.step.fiqbehind`, the verifier three mutations, and the sentence is replaced. **Variant:** "StepIrq left out when a suspended sample is gone on with" is taken as "no `StepIrq` in the loop that goes on with a suspended sample". Left out only in front of that loop's first instruction it is an equivalent mutant: a suspension is behind the FIQ test of its instruction, and nothing but a debugger write can make a FIQ enterable during the stop | 5.2, 3.3 Gate and Verifier, 3.4 Gate (`steps`, `arm7.step.fiqbehind`) and Verifier, M2-D43, M2-D59, 6.3, A.3, A.4, section 7 rows 44 and 71 |
| S16 | in the shape 2.3 prefers nothing is split, and the suites named as "the gate of the `Step` split" never execute `StepIrq` or `StepExec` (`arm7m_step()` calls `ARM7Backend::Step`) | taken: `ARM7M_STEP=halves` and six `.halves` registrations; the mutation "a `StepIrq` that enters with F masked" is given to the neutrality targets, which the F window lets see it, and the `.halves` suites get "a `StepIrq` that enters nothing" (the FIQ cases of `arm7.directed`) | 2.3, 3.4 Delivers 2, Owns, Gate and Verifier, M2-D60, A.4, section 7 rows 57 and 71 |
| S17 | three mutations of the list cannot be caught by the tests as described | taken, all three. `FoldRequests()` merges a freezing cause at the head of every turn of the park loop; an exemption armed with `dbg_arm7_armed` 0 is gone at the flag's next change; the release puts the PC on 0, where an exemption for another address is never used. The first is replaced by its twin while the guest executes; the second is a pair; the edge of `arm7.bp.resume` holds the ARM7 on address 0. **Added by checking the fix:** the pair is not caught by the edge the finding leaves in place either — the first check that is made ends an exemption, used or not, and behind a breakpoint added while the machine runs that check is at the exemption's PC one time in a loop's length. The edge is replaced by one that adds the breakpoint behind the leave and in front of the next burst, at a stop where the ARM7 stands on the instruction the next burst starts with | 3.4 Gate (`arm7.bp.resume`) and Verifier, A.4 |
| S18 | the calls `arm7.burst.plain` counts are made in emulator code, which may include no debugger header but `debugger.h`; the cut's one counter is in a debugger-private header | taken: `VirtualDreamcast::SoundBurstCounts`, in every build | 2.3, 3.4 Delivers 1 and 3, Gate (`arm7.burst.plain`, `idlebp`), M2-D61, A.4, section 7 row 70 |

### R.10 Writes, round 3

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| W14 | a PC written onto an address that shares its slot of the block cache (the cache is indexed by `(pc >> 1)` under `FPCB_MASK`, 32 MiB, without an address tag: the mirrors of main memory, the boot ROM against main memory) makes a step and a resume disagree | taken as a cache reset, in H6-33 and M2-D42, with a case of `writes.reg.pc` and its mutation; M2-Q4 asks for both resets. **Declined:** the reset only when `bm_GetBlock(pc)->addr != pc`. The x64 dispatcher looks every block up (`rec_x64.cpp`, the main loop; `Relink()` is a stub), so that test covers the first block only — with the slot of the written PC empty, the block compiled there branches into the other mirror's block in the next slot —, and it asks the block manager from `dbg_regs.cpp` in builds and backend selections that have none. Taken instead: `SuperH4::ResetCache()` at every accepted write that moves the PC | 1.1, 1.2, H6-33, 3.5 Delivers 2, Gate (`writes.reg.pc`, the witness bullet) and Verifier, M2-D38, M2-D42, M2-D48, M2-Q4, 6.3, A.2, A.5, section 7 rows 32 and 69 |
| W15 | the cache half of `writes.reg.fpscr` sees a missing reset in one direction of PR only: a block compiled under PR = 1 calls the interpreter's handlers (`dec_generic` gives every FP opcode up when `FPR64` is set) | taken; added by reading: the rounding mode has no witness on this host (`shop_cvt_i2f_n` and `_z` are the same `cvtsi2ss` in the x64 backend), so no test is asked for it; the guest leaves FPSCR as reios sets it, as a property of 5.2 | 3.5 Gate (`writes.reg.fpscr`, the witness bullet) and Verifier, 5.2, M2-D48, A.3, A.5, section 7 row 72 |
| W16 | two writes the cut treats as neutral with their restore are not always: SR at a code breakpoint stop with an interrupt pending (`UpdateSR()` returns `SRdecode()`), and a CPSR of the ARM7 that changes the mode (`CPUSwitchMode` rewrites the bank slots, which the save state holds) | taken; H6-33 gets the sentence on SR as well. **Variant:** the CPSR is left out of the neutrality test and of the stress whatever it changes — `CPUSwitchMode` stores the live R13, R14 and SPSR into the slots of the mode it leaves also when the mode it enters is the same, as the guest's own `MSR` does | H6-33, H6-38, 3.5 Gate (`writesrun.neutral`) and Verifier, M2-D62, A.5, section 7 rows 32 and 72 |

### R.11 Frontend, round 3

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| P26 | both arrivals of the `bl` rule share one budget of 64 steps, the sentence on a step that ended at a vector reads two ways, and the guest is bounded only as "40 to 80 instructions": a correct server can fail the required note | taken. **Variant:** 96 steps for each arrival, and the guest's bounds 32 (to the next `bl`) and 48 (the handler) in place of 64 with 24 and 32 — the loop has grown by the F window, and the handler's 64th-time path is the long one | 3.1 item 3 and Verifier, 5.2, M2-D34, 6.3, A.1, A.3, section 7 rows 44 and 55 |
| P27 | the write parts meet the guest for the first time in E7b's gate, which owns none of the writers | taken: `debugger.contract.m2writes.guest`, `.interp` in E7a's gate | 3.0, 3.5 Gate, 3.6 Gate, M2-D34, A.5, A.6, section 7 row 62 |
| P28 | two halves of the reset rule hold nothing: no table is listed when the part runs, and nothing resumes the machine behind the parked reset in an emulator-profile run | taken; the category flipped is `dsp`, which arms nothing on a server of M2 (the finding names `events`); `core.reset` holds the tables too | 3.1 item 2 (the mock), item 3 (`reset`) and Verifier, 3.5 Gate (`core.reset`), M2-D38, A.1, A.5, section 7 row 40 |
| P29 | six broken servers pass both runs of the M2 gate | (a) to (e) taken as rules of the runner, each with its broken server in the verifier's list; the mock needs nothing new for (a) (its ARM7 step moves nothing of its SH4). (f), a reset at a breakpoint stop that keeps `breakpointId`, is left to invariant I7 where it occurs, as the finding has it, and `core.reset` gains the case | 3.1 item 2, item 3 (`arm7`, `writeMemory`, `reset`) and Verifier, 3.5 Gate (`core.reset`), M2-D38, A.1, section 7 rows 40 and 55 |
| P30 | "not control flow" is not the runner's definition: `isArmControlFlow` takes every line that names `pc` for control flow, and the emulator prints a literal load as `ldr rN, [pc, #..]` | taken | 5.2, 3.3 Gate and Verifier, A.1, A.3, section 7 row 44 |
| P31 | nothing makes the node a register panel is bound to unique | taken | 3.1 item 3 (`tree`) and Verifier, 3.8 and its Verifier, M2-D24, M2-D39, A.6, A.8, section 7 rows 39 and 40 |

### R.12 Integration, round 3

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| I22 | the UI bundle is a look-up: the emulator's configure embeds `<tree>/../dreamcast-devtools/dist-native`, which for a worktree is the natural name of the pin; the emulators of the ctest targets are configured with nothing but `HAS_TA_LLE`; `NULLDC_DEVTOOLS_DIST` given to the tests project goes to `tests/debugger/net` | taken | A.0, section 4, 3.2 Gate, 3.9, 3.10, M2-D44, M2-D46, A.2, A.9, A.10, section 7 row 62 |
| I23 | the last bullet and the "Returns" paragraph of the `Park()` comment become wrong with M2 and have no item | taken: H6-19 | H6-17, H6-19, 3.2, M2-D63, A.2, A.4, section 7 row 68 |
| I24 | the design is edited at M2G, but a rule of the revision edits its Appendix B, and the briefs send engineers to design sections that section 7 rewrites, with no rule for which text wins | taken, the first way: this file is the contract until M2G. The other way, section 7 applied before F3a starts, is rejected in M2-D64 | the head of this file, section 2 (rules), section 7 (intro), M2-D64, A.0, the "Read" lines of A.1 and A.4 to A.8 |
| I25 | after M2G the design, the decisions and a frozen header cite this file, which is copied nowhere in the repository; `docs/nulldc3/PLAN.md` and two sentences of `DESIGN-V3-OPEN.md` are missing from item 5 | taken; R6 commits the first copy | 2.4 item 6, 3.2, 3.10 item 5, M2-D65, A.2, A.10, section 7 row 73 |
| I26 | three answers cost more the later they come, and section 4 has a date for one question only; M2-Q1's last sentence is wrong about what a "no" changes | taken | section 4, 6.2 (M2-Q1, M2-Q2, M2-Q4, M2-Q5), 6.3, M2-D54, A.2 |

**Round 4.** A closing round: each lens read the cut for whether its findings of round 3 are settled wherever
the same rule is stated, and for what the third revision broke next to them. Four findings needed a change,
all as should-fix and all residues of a finding of round 3: two of the sound lens (both of S15) and two of
the writes lens (of W16 and of W14); the frontend and the integration lens had none. Each was checked against
the source tree of the emulator and the cut, by reading: nothing was built or run. All four are right in what
they conclude and are taken; each has an addition that checking the fix gave, named in its row, and one
detail of S19 is corrected there (its second route does not survive the first of the two mutants: that entry
is made by the FIQ test behind the `msr` in the new phase, which "once per sample" removes; the first route
survives both). No package, no
ownership, no declaration and no question changes: the changes are one frozen comment (H6-38) with a
precision in a second (H6-33), two properties of the guest that were assumed and are now asked for, what
two tests and one count take for their witness, and three decisions (M2-D66 to M2-D68).

### R.13 Sound, round 4

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| S19 | (of S15) `arm7.step.fiqbehind` and the fourth kind of `steps` take any step of the `msr` at `a_fwin_end` that ends at `0x1C` with `R14_fiq` = `a_fwin_end` + 8 for a FIQ entered inside the sample that was gone on with. Two other routes give that stop — the `msr` is the last instruction of its sample and the entry is made by the first FIQ test of the next; the stop on the `msr` is in front of the first instruction of a sample, which by 2.3 is a new phase —, about once in ten bursts each, so both tests stay green under the two mutations they are named for | taken. `CPUFiq` stores `R15_ARM_NEXT + 4` on every route (`hw/arm7/arm7.cpp`), and under "once per sample" the first route remains, under "no `StepIrq` in the loop that goes on" both. A step is counted only when `out[1]` of `SoundBurstCounts()` is unchanged over it and over the step that arrived on the `msr`; `arm7.step.fiqbehind` breaks on the instruction in front of the `msr` and steps twice. **Added:** the guest is asked for what that breakpoint needs (the instruction in front of `a_fwin_end` is a plain one of the window); a step of the `msr` that starts a run of steps is not counted; the verifier runs the uncounted form once | 3.4 Gate (`steps`, `arm7.step.fiqbehind`) and Verifier, 5.2, M2-D59, M2-D66, 6.3, A.4, section 7 row 72 |
| S20 | (of S15) the count that guards the F window is taken by return address, and an entry by the first FIQ test of a sample that starts behind the `msr` has the return address of an entry behind it (`a_fwin_end` + 8): 150 to 200 such entries in a run, so "at least 100 of the second kind" is met with no entry inside a sample | taken: the scratch build prints, for every entry, its return address and whether the first FIQ test of its `Run` call made it (the first `Step` of `StepMany`); the two kinds are defined by that, and the verifier picks its entry of the second kind by the flag. **Added:** on this guest every entry of the second kind has that one return address, and one with another is a finding of E6a's report | 3.3 Gate and Verifier, 5.2, M2-D59, M2-D66, 6.3, A.3, section 7 row 44 |

### R.14 Writes, round 4

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| W17 | (of W16) the sentence the third revision put into H6-38, "a CPSR written and written back leaves every register as it read", is false while the ARM7 is in user or system mode: leaving one of them `CPUSwitchMode` copies the CPSR into the live SPSR (`hw/arm7/arm7.cpp`, the cases `0x10` / `0x1F`), and the row `SPSR` is `regs[RN_SPSR]` in every mode (`SoundCPU.cpp`, `DebugGetReg`). A test that asserts the sentence fails a correct writer in a file its engineer may not touch | taken: H6-38 is corrected before it is frozen, and the clause is added wherever the rule is repeated. The writer stays the core's switch (M2-D67; a writer that puts the live SPSR back is rejected there). **Added:** `arm7.setreg.*` holds the corrected sentence, writing from system mode once itself, so that the frozen text has a test whatever mode the guest runs in; the two briefs say what a comparison of rows around a CPSR write may assert | H6-38, 3.4 Gate (`arm7.setreg.*`), 3.5 Gate (`writes.reg.arm7`, `writesrun.neutral`) and Verifier, M2-D62, M2-D67, A.4, A.5, section 7 rows 32 and 72 |
| W18 | (of W14) the mirror case of `writes.reg.pc` rests on a `g_main_loop` that goes back PC-relative, which 5.2 does not ask for (a `jmp @Rn` on a preloaded address returns to `0x8C…` after one pass, and E7a may not touch the guest); and `0x8D…` is a mirror that does not share its slots: `FPCB_MASK` covers 32 MiB (`hw/sh4/sh4_if.h`), main memory is 16 MiB (`build.h`) | taken, both. 5.2 asks for `bra` and no `jmp`, `jsr` or `rts` in the loop, E6a's verifier checks it by disassembly, and the case names the mirrors that witness, with the trap among the verifier's. **Added:** "the mirrors of main memory share their slots" is made exact wherever it stood, the frozen sentence of H6-33 and its alternative for a "no" to M2-Q4 first | 5.2, 3.3 Verifier, 3.5 Gate (`writes.reg.pc`) and Verifier, H6-33, 1.2, M2-D42, M2-D48, M2-D68, A.3, A.5, section 7 rows 44, 69 and 72 |

**Round 5.** A second closing round, on the cut as revised four times: each lens read it for whether the
findings of round 4 are settled wherever the same rule is stated, and for what the fourth revision broke next
to them. Three findings needed a change, all as should-fix and all residues of round 4: one of the sound lens
(the fixes of S19 and S20 do not meet), and one each of the writes and the integration lens, which are one
finding seen from two sides (the clause the fix of W17 added beside the sentence it corrected); none came
from the frontend lens. Each was checked against the source tree of the emulator and the cut, by reading:
nothing was built or run. All three are right and are taken as raised; no part is declined, and one part is
taken further than its finding asks (section 7 row 32, in I27). Each row names what checking the fix added.
No package, no ownership, no declaration and no question changes, and no decision is added: the changes are
one frozen comment (H6-38, corrected a second time before R6 freezes it, with a table of its four cases),
what the gate of E6a's count is on (M2-D66), and what two tests assert of the row SPSR, with a case and a
mutation for the corrected clause (M2-D67).

### R.15 Sound, round 5

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| S21 | (of S19 and S20) the two fixes of round 4 do not meet. E6a's gate counts every entry that a later FIQ test than the first of its sample makes; E6b's two tests count a step only when `out[1]` is unchanged over it and over the step in front of it. An entry made by the second `Step` of its `StepMany` call — the `msr` at `a_fwin_end` starts its sample — is in the first count and never in the second, and what the cut puts between the two is an expected share ("four in five", 6.3) over a phase that is the orbit of a deterministic map. A guest can pass E6a's gate and leave `arm7.step.fiqbehind` and `arm7n.steps` red with a correct burst machine, behind the baseline, in a package that may not touch the guest | taken as raised. `StepMany` starts every sample at 0 ticks and drops the overshoot (`hw/arm7/arm7.cpp`), and what an instruction costs is fixed by the instruction and the guest's own state (the access macros of that file are 1), so nothing spreads the phase: it is the guest's orbit and can stay on few positions. The scratch build prints the number of the `Step` whose FIQ test made each entry; the report has three numbers a configuration and the longest stretch without an entry of the third class; the gate is on that class: at least 100, **and no stretch of more than 32 ticks** — the number the finding left to the cut, half the 64 bursts of `arm7.step.fiqbehind`. **Added:** the scratch build prints the tick of each entry, which the stretch needs; a wider window is named as what also moves the orbit; and 6.3 keeps what the count cannot give: it is of the plain run, and the tests of the session in front of `arm7.step.fiqbehind` hold, release and write the ARM7 | 3.3 Gate and Verifier, 3.4 Gate (`steps`, `arm7.step.fiqbehind`), 5.2, M2-D59, M2-D66, 6.3, A.3, A.4, section 7 row 44 |

### R.16 Writes, round 5

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| W19 | (of W17) the clause the fourth revision added to H6-38 and gave to two tests as their assertion — leaving user or system mode, "RN_SPSR then reads" the CPSR as it was before the write — holds only when the mode written is user or system as well. Into a mode that has an SPSR the switch makes the copy and then loads that mode's slot over it (`CPUSwitchMode`, its second half, with `saveState` false — as it has to be: the guest's `MSR` passes false, and with true the SPSR of the mode entered is overwritten and stored into its slot on the way back). `arm7.setreg.*`, whose write "again" is not said to keep the mode, and `writes.reg.arm7`, on a guest whose loop runs in system mode, fail a correct writer against a frozen comment; and "stays changed behind the restore" is nothing a test can assert | taken as raised: H6-38 is corrected before it is frozen, the condition on the mode written stands in every place where a test is told what to assert, and values stand in place of "changed". The last sentence of H6-38 stays: it holds for every pair of modes (the four cases were walked through the switch). **Added:** a table of the four cases under H6-38, as the one reference of the tests; the comparison "SPSR reads the row `SPSR_<mode>`" is made with what that row read in front of the write, in `arm7.setreg.*` a value the test wrote — behind the write the two rows are one register (`DebugGetReg` gives the live register for the bank of the active mode), and the comparison would be an identity; `arm7.setreg.*` makes the write into a mode with an SPSR itself, from system mode, so that the corrected clause has a test whatever mode the guest runs in; the verifier gets the mutation the old sentence invited (the switch called with `saveState` true); 5.2 says which modes the guest's loop can run in (never user: `MSR CPSR_c` changes the control bits with `armMode > 0x10` only, `gpl/vba-arm/arm-new.h`), and E6a's report names the one it has | H6-38 (the comment, the paragraph in front of it, the table behind it), 3.3 Gate, 3.4 Gate (`arm7.setreg.*`) and Verifier, 3.5 Gate (`writes.reg.arm7`), 5.2, M2-D67, A.3, A.4, A.5 |

### R.17 Integration, round 5

| # | Finding | Outcome | Where |
| --- | --- | --- | --- |
| I27 | as W19, from the side of the freeze: no writer makes that sentence of H6-38 and the one behind it both true; the two gates and the two briefs repeat the unrestricted form; M2-D67 says the write "changes that row", which a second write of the same value does not; and behind R6 the correction is header revision 7 | taken with W19, in one text: the two wordings the findings offer for the comment say the same and are merged. M2-D67 names values, and that the writer passes `saveState` false. **Taken further:** section 7 row 32, which both findings leave as it is, gains the clause on the mode entered — it is the design's one sentence on the matter, and read alone it gives the copy as what SPSR then reads | as W19; M2-D67, section 7 row 32 |

---

## Appendix I. The integrator's notes, by package

What the verifiers of the packages left for the cut and for later packages. A later brief is read with these.

### I.1 F3a (verified at `e0f7895` of `dreamcast-devtools`, three verification rounds; the UI pin of the round starts there)

- **For section 7 (design text), from the runner as built:** (1) the probe of the `writeMemory` part that executes what was
  written saves the PC, R0 and the bytes at the stop it is made from; the write to a running target comes first, and the
  part's first scratch address is not reused behind a resume. (2) The tick of an accepted write is the first tick since the
  request, in front of the response or behind it. (3) The SR.RB rule sets R0 apart from R0_BANK before the flip and restores
  it. (4) Both PCs carry `flags.writable`, or the `setRegister` part fails there (M2-D18).
- **For the verifier paragraphs of the UI packages (F3b, F3c, F3d):** the unit suite is run at least three times in a fresh
  clone, whole and in parallel as `npm run check` runs it: F3a's one red gate showed only there (2 of 3), never in a single
  test and never in the contract gate.
- **For F3c, F3d and every later test over the mock:** the mock's SH4 executes on a free-running 16 ms timer, so a step falls
  into a short run now and then (7 of 100 measured). A test never compares the mock's PC with a constant behind anything that
  resumes the target: it compares with the PC of the stop, or uses a machine that executes a fixed number of steps at each
  resume (the helper `pausing(steps)` in `scripts/contract/m2.test.ts`).
- **Known and expected:** between F3a and R6 the emulator's `debugger.regs.panels` is red (35 panel ids known against 37);
  R6 closes it. The M2 rules of the runner have met the mock and patched mocks only; their first emulator is E6b's (`arm7`),
  E7a's (writes, reset) and E7b's (tree).
- **Not noticed by any test of the mock (five of the verifier's extra mutations; notes, for a later mock package):** a tab
  around a value; a held ARM7 keeping a step-out's temporary breakpoint; `flags.writable` without `register-write`; a reset
  restoring the ARM7's registers only; `--m2` of `mockServer.ts` not reaching the mock.

### I.2 R6 (verified at `bec9cbcc` of `devcast`, first verification; merged as `af1f1d8c`, three read-only reviewers found nothing)

- **Frozen text is ASCII.** H6-38 and H6-39 carried four dashes outside ASCII into `hw/arm7/SoundCPU.h` and
  `hw/aica/aica_mmio.h` (a compiler that does not read the source as UTF-8 warns); the integrator replaced them by ASCII
  dashes in the tree (`f6949750`) and in section 2 of this file. A frozen comment carries no character outside ASCII.
- **`dbg_arm7.h` stands behind the licence head** of five lines that every header of `libswirl/debugger` has; H6-5 prints it
  from `#pragma once`.
- **For E6a (its first commit):** `tests/debugger/guest/CMakeLists.txt` finds the guest directories with a plain glob, so a
  directory added to a tree whose tests project is already configured is not registered until cmake is re-run, and ctest is
  green without its tests. E6a makes the glob `CONFIGURE_DEPENDS` (three lines, tried by R6's verifier:
  `file(GLOB nulldc_guest_lists RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*/CMakeLists.txt")`,
  sorted, `add_subdirectory` of each entry's directory); this one change of R6's file is E6a's by the integrator's decision.
  E6a also replaces the sentence "No guest is there yet, and the directory registers no test." of `tests/README.md`.
- **For E6a and E6b:** a guest directory cannot call `nulldc_debugger_selftest(...)` or the contract function (they read
  variables of their own directories and would register a command without an emulator path). Sessions on the guest are lines
  of `tests/debugger/selftest/CMakeLists.txt` and `tests/debugger/contract/CMakeLists.txt`, as 3.0 has it.
- **For E7a (it owns `dbg_selftest_core.cpp` next):** the hand-over check of `core.tables` reads only what `dbg::arm7` was
  handed. A `PushBreakpoints` that also hands the ARM7's breakpoints to the SH4, or the SH4's to the ARM7, passes it. E7a adds
  the two cross-target checks R6's verifier tried (`agents/nulldc3/r6/verify-1/scripts/proposed_fix.py`, results in
  `logs/proposal.txt` beside it): with only ARM7 breakpoints listed `dbg_sh4_armed == 0`; with an enabled SH4 code breakpoint
  beside them `dbg_sh4_armed == 1` and the set handed to the ARM7 unchanged. E7a also removes the two clauses of 2.5
  (`dbg_regs.h` and `dbg_mem.h`, `Write()`), each there exactly once.
- **For the gates' wording:** "the M1 gate unchanged" compares the result line (18 passed, 0 failed, 0 partial, 0 not
  applicable, 0 invariant violations); the tick count differs from run to run. "No home path, host name or user name" does not
  count branch names (`skmp/nulldc3`, `skmp/nulldc3-<pkg>`), which the working documents carry.
- **Not covered, and accepted:** the clears of the run flag and of `g_parked` at the end of a thread are not held by a test
  (removing them passes: `SuperH4_impl::Stop()` clears both at the request); a test that latches a debugger stop in the slice
  of a host stop would hold them. The ARM7 half of `core.tables` and `stepping: true` on the wire ran in forced builds only;
  their first real run is E6b's gate.
- The repository's copy of this file is the cut as it stood at R6; this appendix and later notes reach it at M2G.

### I.3 E6a (verified at `83c2ee0a` of `devcast`, second verification; merged as `5a3ad3d6`, two read-only reviewers found one item each, both fixed)

- **The baseline of 5.5 is taken on `5a3ad3d6`** (its tree is that of `83c2ee0a`), by E6b as the first step of A.4. Lines of the
  guest recorded on `52b32fe4` or earlier are void: the fix round changed the guest's bytes (`arm7`, `sh4` and the per-sample
  `samples` moved; batch `samples` and `regs` did not).
- **The guest as merged:** `arm7.bin` is 352 bytes, `dsptest.elf` 2028 bytes. The labels a tool needs come from
  `dsptest_labels.inc` (`a_main_loop` 0x94, `a_fwin` 0x9C, `a_fwin_end` 0xB8 with the return address `a_fwin_end + 8` at 0xC0,
  `a_leaf` 0xCC, `a_fiq` 0xD8, `a_fiq_end` 0x154); nothing copies an address out of a log of round 1.
- **5.2, the `a_reset` row, as built:** "every pending bit cleared" is not enough on this emulator. A release latches a pending
  request again (`SetResetState` accepts the interrupt), and clearing SCIPD lowers neither nFIQ nor M. `a_reset` ends the
  service of a latched request: SCIEB 0, SCILV0-2, TIMA, the SCIRE clear, **then** the read of L and the write of M, then SCIEB
  timer A and the `msr`. With L and M in front of SCIRE the write of M latches the request again.
- **For E6b (it holds and releases in front of `arm7.step.fiqbehind`):** a hold and release of the guest's ARM7 between two
  bursts restarts the counted FIQ sequence entry for entry, with or without a request pending at the hold (checked for a
  release in front of each of the ticks 3 to 202). So "every entry of the second or a later kind returns to `a_fwin_end + 8`"
  and the count of E6a hold behind every such release, not only in the plain run. The count: 4920 entries in 2000 ticks, 2861
  by the first `Step`, 13 by the second, 2046 by the third or a later one; 1710 of 2000 ticks have one of the third class and
  the longest stretch without is 1 tick. The risk of 6.3 about hold and release reduces to what `a_reset` does not write (the
  channel registers, timers B and C, the wave memory).
- **For E6b, E7a and the contract's sessions on the guest:** the guest's ARM7 is released, held and released again within the
  first two ticks; the guest does its set-up between the first tick and the second (5.1 and the `_start` row of 5.2 say "before
  the first AICA tick"). A session that reads `ENABLED` or the ARM7's PC that early sees the one-word program at address 0.
- **Targets the cut does not list (3.3 item 3):** `debugger.sound.release.dsptest.<jit|interp>.<batch|persample>` and
  `debugger.sound.runner`. The tests project has **206** targets at this commit.
- **The runner checks the engine of every run.** On an emulator without a recompiler (`-DNULLDC_EMULATOR=` with a
  `TARGET_NO_REC` or generic binary) the `.jit.` sound targets and `engines` fail, they are not skipped (8 of the 16
  `debugger.sound.*` / `debugger.guest.*` targets), and E6b's `debugger.sound.neutral.*.jit.*` will do the same. No gate of
  the round hands such a binary to the tests project; a package's fifth build is run by its own commands.
- **`nulldc_sound_neutral`** also takes `CONFIG` and `TIMEOUT` (default 900 s) and can be called from a sibling directory.
- **3.3, Verifier, as measured:** with the ARM7 never released (the second release left out) the per-sample `samples` is the
  hash of 32 zero pairs, not the offset basis: the first tick runs on the one-word program.
- **Toolchain:** an arm-eabi binutils (2.43) is on this machine, though not at the default `DC_ARM_BIN` of `gen.sh`; GNU as
  and clang give the same 352 bytes, and `debugger.guest.dsptest.files` runs `gen.sh --check` with whichever it finds. The
  check compares the ELF byte for byte, so a host whose sh-elf binutils lays the ELF out differently fails it (known limit).
- **M2-Q3 as observed:** 2ndmix under reios is silent. Its ARM7 idles in a three-instruction loop, SCIEB reads 0, SCIPD
  0x5C0 (pending, never delivered), no FIQ was ever entered.
- **Kept, by the integrator's decision:** the slow pitch steps of the second channel and the SCIPD fold in `a_fiq` go beyond
  5.2. They stay: taking either out is a guest change with new bytes, a new count and a new soak, and nothing later needs it.
- **Not held by a test, and accepted:** the order of the TIMA write and the SCIRE clear in `a_reset` (no time step falls
  between them on this emulator); a hold and release at a later tick (the line's fields are cumulative; the scratch trace of
  the verifier holds it, and E6b's selftests are the place if they come to depend on it).

### I.4 E6b (verified at `e3e12143` of `devcast`, second verification; merged as `b4cb3e82`; three read-only reviewers, one item to fix; the first verification found three test gaps and no defect of the emulator)

- **Counts where the cut says otherwise (3.4 Gate):** sixteen neutrality targets, not twelve (a fourth `nulldc_sound_neutral`
  call, `debugger.sound.neutral.hoststop.<jit|interp>.<batch|persample>`, is the witness of the completion of a suspended
  burst in `dc_run`); four `arm7n.*` tests, not three; the session `arm7` has 43 tests in each of its five registrations; the
  tests project has **238** targets. Allowed by the integrator.
- **For E7a (its brief A.5 is read with these):**
  1. `DebugSetReg` has its body. `regs::Write` calls `dbg::arm7::OnPcWritten()` behind a PC write of the ARM7 (the session's
     helper does), and `writes.reg.arm7` writes the PC in a service excursion of a running machine as well: the only place
     where that call can be seen. No row of `dc.aica.arm7` is marked writable yet.
  2. Nothing holds the stored CPSR word behind a write (H6-38, "no other bit is stored"): `CPUUpdateCPSR` taken out of
     `DebugSetReg` survives every test. E7a's register tests hold it.
  3. `libswirl/debugger/dbg_core_private.h` says "What the core's own files share, and nothing else includes"; from this merge
     on `dbg_selftest_arm7.cpp` includes it (for `events::Snapshot`: `arm7.events.park` has to show an event logged and 3.4
     gave no other accessor). The integrator allows the include; E7a, which owns the header, corrects the sentence.
  4. An SPSR (or `SPSR_<mode>`) written with mode bits that are no mode is stored, as H6-38 says, and ends the process at the
     next exception return (`CPUSwitchMode`, default case), where a CPSR of that kind is refused. By the owner's answer to
     M2-Q4 a debugger write can end the emulator; E7a keeps H6-38 and its error table as they are, and the case is a line of
     the design's known limits at M2G.
  5. Mutation runs on this tree: use a guard on resident memory and at most two emulators (`e6b/verify-2/scripts/`, the
     guard and its use). The session's step loops are bounded now (`kStepsMax` = 100000, the published list capped), so a
     machine on which a step ends no ARM phase fails by a check in seconds; before the fix two mutants of the cut's own list
     grew an emulator by 90 MB a second. `ctest --timeout` does not shorten the TIMEOUT these targets set themselves.
- **Known limits, for the design text at M2G (section 7) - none blocks the round:**
  1. *The exit line in the tick of a latched stop.* When a check or an event breakpoint latches a stop in the tick of an exit
     or host-stop request, the stop callback sees the SH4 in front of an interrupt acceptance that the plain run has already
     made: the line's `sh4` and `hash` parts can differ from the plain run's although the guest does not sleep. The sentence
     of 3.4 Gate ("does not sleep, so both are held whole", here and in the repository's copy) holds for a run that goes on.
     The neutrality tests keep their stops early (by burst 42, 124, 402 and about 900, of 2000 for the line and about 2070
     for the hash). **Integrator's decision:** documented, not changed in M2 - making it whole needs the deferred acceptance
     in front of the stop callback, a new entry of `dbg::emu`, so a header revision; it goes to the backlog with the scratch
     test that shows it (`e6b/fix-1/`, `p4_test.inc`, `p4_run.sh`, `logs/p4-exitintc.txt`).
  2. *Step out inside the FIQ handler.* The temporary breakpoint stands at the live R14, as H6-5 words it. In FIQ mode R14 is
     the return address plus 4 (the handler returns with `subs pc, lr, #4`), so a step out asked inside the handler ends one
     instruction behind the interrupted one, after it has executed, or elsewhere when it branches. Backlog: a step out that
     knows the mode it was asked in (header text of H6-5).
  3. *H6-19 against H6-5.* The ARM7 half of a resume owed by the thread before can never arm: `OnThreadExiting` calls
     `arm7::OnStop()`, which records a decision that the stop's own `OnStop` has already consumed. The effect is right - the
     completion in `dc_run` has run the instruction of the stop - and the sentence of H6-19 is to be reworded with the next
     header revision (comment only).
  4. *An unaligned PC from `LDM` with pc in the list and no `^`* is executed by the core as it was loaded; the debugger's
     breakpoint key and the word `PrepareStepOver` decodes are the aligned ones, so the `bl` decision can be wrong there.
  5. `dbg_arm7_Check` walks the listed set linearly (up to 1024 entries a checked instruction). A pause asked for while the
     guest's SH4 sleeps is taken by the 250 ms liveness bound.
- **Not held by a test, accepted as notes:** the completion in `dc_run` of a sample in whose rest a FIQ becomes enterable
  (the state is never reached by the 21 targets; the loop is the one `steps` and `arm7.step.fiqbehind` hold on the checks
  path); the mask test of the check while a temporary breakpoint arms the loop; a step whose next check stands on a listed
  breakpoint published as a step; four ends of the resume exemption that no run of M2 can show (equivalent mutants, as the
  three the cut names); four mutants of the cut's list are caught by a session that never ends (the 300 s watchdog a
  registration) and not by a failed check; on a failure path `arm7n.hoststop` waits for the watchdog as well.
- **3.4 Verifier, wording:** "one pending FIQ an instruction late" means one *executed* instruction, and an *early* FIQ of
  the run (the debug loop of `arm7n.hoststop` sees fewer than 20; "the 3rd" turns all sixteen red). "Once under load" is run
  as four emulators at a time and no other load: the machine rule has no load generators.
- **Speed, for M2G:** by CPU time on the `-DHAS_TA_LLE=OFF` build with the `none` renderer the tip is slower than the base in
  every set that was measured (four sets of five rounds by three agents): the JIT by 1.3 to 1.7 %, the SH4 interpreter by 1.2
  to 2.4 %, with base rows that spread 1.3 to 7.9 % on this machine. Checks (a) and (b) of the cut decide and pass (the
  compiled code of an ARM7 instruction's path is unchanged; +27.1 to +27.4 host instructions a sample against the 48
  allowed, which is +0.06 % of the instructions for +0.7 to +1.0 % of the cycles). The cause is not found. M2G measures it
  again on a quiet machine with more rounds, and looks at code placement if it stands.
- **Not run:** the notice and the fallback of the arm64 recompiler (no arm64 host); read only.
- **The UI pin stays at `e0f7895`:** the runner's part `arm7` met its first emulator (five `debugger.contract.m2arm7*`
  targets, also under ThreadSanitizer) and needed no change.

### I.5 E7a (verified at `a86d9a24` of `devcast`, second verification; merged as `b951e440`; three read-only reviewers, three items to fix; the first verification found two test gaps; one comment-only commit of the integrator behind it, `d3fe072d`)

- **Counts and names where the cut says otherwise:** the tests project has **249** targets (238 + six
  `debugger.selftest.writes*` + five `debugger.contract.m2writes*`). The test of `debugger.selftest.writes.smc` is
  `smcwrites.cached`, registered with `SELFTEST smcwrites` (the steps of `writes.mem.cached` behind two checks of its
  configuration: the recompiler, and `Dynarec.SmcCheckLevel=-1`); 3.0, 3.5 Gate and Verifier and A.5 name it
  `writes.mem.cached`.
- **A fourth rule of value that 3.5 Delivers 2 and M2-D42 do not have:** an accepted write of the ARM7's PC that stores
  another address than it had empties the ARM7's code cache (`SoundCPU::InvalidateJitCache()`; a recompiler of the ARM7
  enters a block by the masked PC and has the unmasked one compiled in). The integrator put the sentence into H6-33
  (`dbg_regs.h`) and pointed H6-38's "No compiled code is dropped" (`SoundCPU.h`) at the caller (`d3fe072d`, comment
  text only). On x86-64 only the writer's count is held; an ARM64 host runs `writes.reg.arm7` when there is one.
- **A text with a NUL in it** is no path, name or watch expression of the tree: -32602 in `state.setRegister` and
  `state.watch` (M2-D20 is silent on it). M1's `breakpoints.add` still looks its event up to the first NUL
  (`dbg_core.cpp`, `FindEvent(event.c_str())`); read, not run.
- **Accepted departures, declared by the package:** the test hook `core::SetTickSinkForTest` (one atomic load on the path
  without a server, null outside a selftest); sentences of E6b's rows in `tests/README.md` and the contract README that
  this package made false were reworded.
- **For E7b:** the `tables` row of `tests/debugger/regs/README.md` gains the writable-rows check (69 + 40 rows, no
  other). `core.writes`, `writes.reg.readonly` and `regs_test.cpp` pin 109 writable rows; the totals of the tree are
  computed, so new rows break nothing unless they carry `kRegWritable`, which no row of E7b does.
- **Known limits, for the design text at M2G:**
  1. A write into wave memory or video memory discards no SH4 block: SH4 code a guest executes from there (compiled without
     an entry check) is not invalidated by a debugger write of its bytes. The ARM7's cache is.
  2. `bm_LockedWrite` leaves the page marked as holding data until the next cache clear. In the default configuration that
     is host cost. With `Dynarec.SmcCheckLevel=-1` blocks compiled from the page afterwards get neither protection nor a
     check, so a guest that later stores new code there runs the old blocks (a speed setting, after a debugger write).
  3. On a recompiler without `SH4DBG_CTX_AT_SLICE` (arm64) the tick behind a write to a running target says
     `registersStale`, although the context is complete in the service excursion in which it is built. Not seen on x86-64.
  4. H6-33 and H6-35 say "(E) in the park context only" and "returns kErrNotWritable with no machine"; no emulator thread
     exists without a machine, so no test reaches the clause. Reworded with the next header revision, as `Read()` has it.
  5. Two FPSCR properties are held by reading only: a write that changes none of PR, SZ and the rounding mode resets
     nothing, and a changed rounding mode resets.
- **ThreadSanitizer and the contract runner:** the part `arm7` against 2ndmix is red by wall-clock time on a
  ThreadSanitizer build (the runner waits 10 s for a paused tick at a breakpoint), on this head and on E6b's merge alike;
  against the guest it passes, and no report was written. M2G asks ThreadSanitizer of the contract parts on the guest only.
- **Leftovers, collected for one small package in front of M2G (`EX`, test hardening; nothing in it blocks a merge):**
  1. `writes.mem.cached` / `smcwrites.cached` witness the middle page of a range only: a writer that leaves out the last
     page (the verifier's mutant n3), or the first, passes. The edit is written and tried
     (`e7a/verify-2/scripts/mutants_z.py`, TEST: a range that ends in the loop's page, 142 bytes from 0x8C00FFF0 with
     `mov #0x5E,r12` in the slot; and one that begins in it).
  2. Waits without a bound on failure paths: `ArmStop` of `writes.reg.arm7` (under a device that stores the ARM7's PC as
     given the session ends by the 300 s watchdog), `core.reset` when the reset is not answered, and `arm7n.hoststop` when
     the machine does not stop (I.4). Each gets a bound and fails by a check.
  3. `breakpoints.add` and a NUL in its event name.
  4. A ctest run of `debugger.contract.*` leaves `tests/debugger/contract/__pycache__/` in the source tree.
  5. Optional, I.4: a witness of the completion in `dc_run` of a sample in whose rest a FIQ becomes enterable.
- **Other builds of the writer** (no recompiler, generic, the recompiler without the layout) are no targets of the tree; the
  commands are in `e7a/verify-1/scripts/alt.sh` and `logs/nolayout.txt` if M2G repeats them.
- **The UI pin stays at `e0f7895`:** the runner's parts `writeMemory`, `setRegister`, `reset` and `arm7` met the emulator
  (five `debugger.contract.m2writes*` targets) and no rule of the runner was wrong about it.

### I.6 E7b (verified at `205aa585` of `devcast`, second verification; merged as `199cdb4a`; two read-only reviewers, one item each; the first verification found one test gap and no defect of the emulator)

- **The emulator packages of the round are all merged.** The contract gate M2 passes against the emulator on the JIT, the
  interpreter and the forced fallback (19 passed, 0 partial, 0 not applicable each) and its second run against the guest
  (2 passed, both notes). The tests project has **259** targets.
- **One change the guest can see, and it removes a difference between runs:** a read-only system bus register that nothing
  assigns (SB_MST, SB_MTXDAD, SB_MRXDAD, SB_MRXDBD, the twelve G2 counters, the three PVR counters; SB_SDDIV, SB_GDSTARD and
  SB_GDLEND until their first DMA) reads 0, where it read the low half of a host function's address (`hw/holly/sb.cpp`,
  item 3 of 3.6). State hashes and sound lines are unchanged against the base on the five builds.
- **Accepted departures, declared by the package:** the targets `debugger.contract.hash.listening.guest` and `.guest.interp`
  (2ndmix cannot catch a read that touches the sound CPU) and `debugger.contract.hash.listening.rule` with the mode
  `hash-listening-rule` of `contract.py` (a tick that says `registersStale` is full without `dc.sh4.cpu`; on x86-64 the rule
  is held by this target alone and was shown against a stand-in, the x64 recompiler built without `CtxAtSlice`); the option
  `ONLY` of `nulldc_debugger_contract`; `Debugger:TickMs=20` and `FullTickEvery=1` in the listening runs with one
  `control.runUntil` behind the describe; `--emulator`, `--work` and `--guest` of `contract.py` checked behind the parser;
  the walk of `core.tables` restated as a computed rule per call; `regs.mem.read` writes made-up values into the 95
  plain-data slots of the on-chip modules and puts them back; the row choices where 3.6.1 is silent (DMEA 23 bits with bits
  0 and 1 reading 0; DRGA and DLG as 13-bit members; L and M 8 bits where the mock has 3; the PACKET rows first byte highest;
  `kRegComputed` on L, M, the clock, the drive and SB_FFST).
- **For F3c and F3d:** measure the UI against a paused tick of about **165 KB** (the sounding guest with a full event log),
  not the 127 KiB of an empty log. M2-D27's "about 130 KiB" is the floor: every tick carries the whole event log (200
  entries), so a paused tick of a live session is 160 KiB and more before watches and breakpoints, above `BuildTick`'s
  reserve of 144 KiB (one regrow) and, with long messages, near the limit of 192 KiB behind which the fallback of M2-D27
  applies.
- **Section 7 at M2G:** row 62 names `.hash.listening.guest*`, `.hash.listening.rule`, the mode and `ONLY`; the list of
  differences between mock and emulator gains the widths of L and M; M2-D27's figure says which event log it is measured
  with; 3.6.1's "M (the level in service)" is the in-service flag (0 or 1) the accessor gives; "one hour of a running
  target" is emulated time; a run listened to from its first instruction needs the `StartPaused` stop and one
  `control.runUntil`.
- **What the state hash of `hash.listening*` does not see:** it has the SH4 context, scheduler time, main, video and wave
  memory and the sound CPU's save state, and no part for the system bus slots, the AICA's register file and channel state,
  the drive or the PVR file. A tick reader with a side effect inside one of those shows only when the guest reads it back.
  The readers are free of side effects by reading (reviewer `reads`) and by the session `regs`.
- **Notes, accepted:** eight rows of `dc.aica.common` whose values are worked out (MIEMP, MOEMP, EG, SGC, LP, CA, AR, VREG)
  carry no `kRegComputed` (nothing on the wire depends on it); the README row of `regs.rows.sb` counts 111 + 14 for 115 new
  rows (the 111 include the 11 of `dc.holly.intc`); `hash.listening` needs ten full ticks, which a host four times faster
  might not give in 600 vblanks (it can fail a correct server, never pass a broken one); no call of `core.tables` lands on
  exactly 256 watches (S08 of the contract gate holds the bound, `core.tables` the partial fill).
- **For `EX` (I.5), one more optional item:** under UBSan the session `regs` loads values outside their enums in
  `gdrom_DebugGetState` (`hw/gdrom/gdromv3.cpp`), from the made-up drive state of `regs.rows.gdrom`; the test keeps its
  values inside the enums.
- **Not run:** `debugger.contract.hash.listening*` on an arm64 host; an arcade board (`dc.gdrom` reading "n/a" is by reading).
- **The UI pin stays at `e0f7895`.**

### I.7 F3b (verified at `1183dab` of `dreamcast-devtools`, first verification, one read-only reviewer with no item to fix; the UI pin is moved there)

- **The UI pin is at `1183dab`** (`package-lock.json` unchanged, its `node_modules` kept). With the UI at that head the
  emulator's 259 targets pass twice, and the hash and sound lines of 50 targets are those of a run with the pin at `e0f7895`.
  `npm run check`: 488 unit tests, the M2 gate on the mock 17 passed twice; e2e 251 passed.
- **Three choices of the package, accepted by the integrator** (section 7 at M2G takes them into the design text):
  1. One test's set-up in `src/state/debuggerDataStore.test.ts` was edited outside "Owns" (its server now lists the DSP; the
     assertion is unchanged): the test depended on two behaviours 3.7 removes.
  2. A code breakpoint whose event belongs to no listed target is listed with the events, its address as plain hex ("0x" and
     the digits, not padded). 3.7's table has no row for it.
  3. The lists `targets` and `capabilities` are read for any present `protocol`; only an absent one leaves them unread.
     Design 3.0 says "only when protocol >= 3"; no server sends a lower number.
  Also: six more e2e tests in `e2e/v2-server.spec.ts` than "Delivers" asks (a scripted server per row of the table, which
  the Verifier paragraph needs), and the entry without `metadata` of `e2e/callstack-eventlog.spec.ts` is kept as the
  scripted server of the `metadata` row.
- **For F3c:** `StopInfo.stopId` is a number. The built-in target values are reachable only as
  `targetBeforeFirstServer(id)` (a page that has had no server yet) and `unlistedTarget(id)`. The synthetic server
  (`e2e/support/syntheticDebugger.ts`, yours) is a v3 server: it lists the three targets of the scripted server with the
  capabilities event-breakpoints and callstack, is paused in stop 1 for every tick, and has `sent` (its describe result and
  last tick); the scripted server has `sendBinary(bytes)`. If you open `e2e/device-tree-perf.spec.ts` for the block of
  3,000 registers, add there that no alert is on the measured page: today only the seventh test of `e2e/v2-server.spec.ts`
  notices a synthetic server the client does not support. `DisassemblyPanel.tsx` opens a panel that is added while paused
  with the PC line on screen whether or not the ticks carry `stopId` (where a view opens; your read-again wiring is beside
  it).
- **For F3d:** the toolbar's Pause sends target "sh4" (`AppLayout.tsx`, `control("pause", "sh4")`) whatever the server
  lists; your focused target covers a server that lists no target.
- **Leftovers of the UI, collected for the UI part of `EX` (I.5; none blocks):**
  1. `e2e/v2-server.spec.ts`, the test "a describe result without `targets` lists none": its breakpoint address 0x8C001000
     prints the same in the removed built-in format, so the mutant that formats by the built-in target passes it (the unit
     test holds the behaviour). Breakpoints at 0x1000 and at DSP step 5 with the two lines the verifier tried
     (`f3b/verify-1/logs/scratch-plainhex-*.log`).
  2. `DeviceTreePanel.tsx`, the stale dimming reads `targetInfo(state.shape, "sh4").pcPath` without `supported`.
  3. "A page that has had no server yet has every capability" is held by a unit test alone (the mutant passes all 251 e2e
     tests): one of the two no-server-yet e2e tests also expects the SH4 callstack panel without its note and the SGC panel
     with its Record button.
  4. `registerNumber` (`targets.ts`) takes hex without "0x", which only the v2 mock sent, and its comment still speaks of a
     v3 server: the leniency goes or the comment says what is taken.
- **Gate wording:** for a UI package "the emulator's `debugger.contract.*` targets once" was run as all 259 targets twice,
  by the engineer and by the verifier.
- **The main UI checkout's `dist-native`** (not tracked) was rebuilt by the package and says `3.0.0-pre+1183dab`: an emulator
  configured from the main emulator checkout embeds it at its next configure.

### I.8 F3c (verified at `af97c4d` of `dreamcast-devtools`, second verification; two read-only reviewers, three items to fix; the first verification found five problems, all in the editors' behaviour and its tests; the UI pin is moved there)

- **The UI pin is at `af97c4d`** (`package.json` and `package-lock.json` unchanged since `1183dab`). `npm run check`: 543
  unit tests, the M2 gate on the mock 17 passed twice; e2e 312 passed, among them the layouts at 1366x768, the
  `MOCK_M2_SUPPORT` blocks of the registers, memory and watches specs and the tree of 3,000 registers at five ticks a
  second. 34 mutants, all caught by the package's own tests.
- **Decided by the fix round where 3.8 and M2-D39 are silent, accepted by the integrator** (section 7 at M2G puts them into
  design 4.3 and 4.4):
  1. *A keyboard path to every editor.* A value of a register the server writes, a watch value, and one byte place a
     memory listing are Tab stops with `role=button`; Enter, the space bar and F2 open the editor, and in the hex column the
     arrow keys move the place (an arrow never takes it to a row the panel cuts off).
  2. *Where the focus goes.* It returns to the value or byte only when the box went with the focus in it.
  3. *A view that moves is leaving the box:* a memory view that moves, and rows replaced by an error, put the byte editor
     away.
  4. *The memory listing is not a box the browser scrolls* (`overflow: clip`).
  5. *While the target runs* the value and byte controls stay focusable, with `aria-disabled`, and open nothing (design 4.3
     says that of the run controls).
  6. An editor goes when the target runs again, with its draft; an answer that arrives for a write whose box was reopened
     with other text does not close the new box.
- **For F3d, the DOM as built:** `td.register-value span` and the textbox "New value of <NAME>" are what they were. On a
  register the server writes that span is `role=button`, named "<NAME> <value>, write", with `aria-disabled="true"` while
  the target runs; the hex cell of the row that holds the keyboard's place (`[data-memory-hex]`) is the same kind of
  control, named "<address> <byte>, write". A Playwright click or dblclick on either while the target runs needs
  `{ force: true }`. A memory row keeps its text content and its first three children; the row with the keyboard's place
  has a fourth, text-less, `aria-hidden` child, and an open editor is a fifth: match a row by its first three spans.
  Your hotkeys (F5, F6, F10, F11) do not collide with the keys these controls take (Enter, space, F2, and the arrows in
  the hex column; `preventDefault`, no `stopPropagation`); your verifier adds two focus places to "the hotkeys with the
  focus in ...": on a value or byte control, and inside an open write editor (a resume from inside an editor closes it and
  leaves the focus on the value). Your reset action advances the `reread` counter when it is answered `{}`; nothing of the
  store changed in the fix round.
- **Leftovers for the UI part of `EX`, beside those of I.7:**
  5. The last rows of an address space are on screen only in a panel tall enough for the whole slice: the view cannot move
     further and the listing does not scroll. A person has to be able to reach them in a small panel.
  6. An editor opened between a move of the memory view and the arrival of its bytes is recorded with the address the view
     goes to, and follows its byte into the new row (the write goes to the address the box names, so nothing is written by
     mistake): no editor opens while a slice is being read, or the view is recorded as the base of the rows on screen.
  7. `e2e/memory.spec.ts`, "ARM7 memory ends where the address space of the ARM7 ends", fails under
     `E2E_CPU_THROTTLE=4` (3 slices fetched for 2 expected), as it did at `1183dab`: it reads the settled fetch count.
  8. A watch expression is renamed by double click only.
  9. The README's "the place is never on a row the panel cuts off" holds for the arrow keys only: "an arrow never takes
     the place to a row the panel cuts off".
- **The main UI checkout's `dist-native` and `dist-wasm`** (not tracked) say `3.0.0-pre+af97c4d`.

### I.9 F3d (verified at `6c753dc` of `dreamcast-devtools`, second verification; two read-only reviewers, five items to fix; the first verification found seven problems; the UI pin is moved there)

- **The UI pin is at `6c753dc`** (`package.json` and `package-lock.json` unchanged since `af97c4d`). `npm run check`: 583
  unit tests, the M2 gate on the mock 17 passed twice; e2e 347 passed and the smoke spec skipped, as it is without
  `E2E_EMULATOR_URL`. **The UI packages of the round are all verified.**
- **The smoke spec has met the emulator:** 4 of 4 runs passed against a headless emulator built from `devcast` `199cdb4a`
  with the package's bundle embedded, on the guest: the start stop, the breakpoint at the program's entry, SH4 steps from
  the toolbar and by key, R0 edited in the register panel and seen in the tick, two bytes of memory written and read back,
  the reset, and ARM7 steps behind a resume. State hashes and sound lines with that bundle embedded equal those without one.
- **For M2G item 4, how the smoke spec is run:**
  1. The order is part of the gate: `npm run build:native` in the pin; the emulator's own configure with
     `-DNULLDC_DEVTOOLS_DIST=<pin>/dist-native` and its build; then the spec. The spec fails at its first step unless `ui` of
     `debugger.describe` equals the version in the checkout's `dist-native/bundle-info.json` and all 30 files are served
     byte for byte, and the version string carries the commit: after every UI commit the three steps are made again. Run
     the configure in front of every build behind a changed `dist-native` (that a build alone embeds again is the
     engineer's word, not tried by the verifier).
  2. `agents/nulldc3/f3d/scripts/smoke.sh <emulator> <emulator tree> <UI checkout> <run dir>`: exit 0 is the only green
     (3: the spec passed and the emulator died, did not end or printed one of `contract.py`'s BAD strings; 130: ended from
     outside). It is stopped with SIGTERM, never SIGKILL (the emulator would then end by `timeout`, 600 s by default).
  3. Every run has its own `HOME`, `XDG_CONFIG_HOME` and `XDG_DATA_HOME`, as `contract.py` gives its runs; the start line
     of 3.9 and A.9 has the `-config` list only. Without them the emulator reads the user's `emu.cfg` (Bind, AllowedHosts,
     WebRoot) and opens the user's memory cards.
  4. The emulator the spec leaves paused resumes by itself 10 s after the page is gone and then runs on one core: whoever
     starts it ends it by pid.
- **Decided by the package where 3.9 and M2-D39 are silent, accepted by the integrator** (section 7 at M2G, design 4.3 and
  4.4): a widget of a code editor is the editor's and no dialog of the page, open or closed (Monaco's Find box has the
  dialog role and stays in the document when it is closed); a dialog is open when it is on screen, also when it carries
  `aria-hidden`; the run state stands under the toolbar below 1440 px and the controls on a line of their own below
  1200 px; Pause names the focused target only when the server lists it; a click on a tab counts as focus.
- **For `N1` (the rebrand):** the README's recipe "The UI against the emulator", `smoke.sh` and `contract.py` isolate a run
  by the three directory variables and name the binary `devcast` and the switch `-config`; with `--portable <dir>` and the
  new names all three change, in both repositories.
- **Leftovers for the UI part of `EX`, beside those of I.7 and I.8:**
  10. The README block "The UI against the emulator" has no line that makes its three run directories (`mkdir -p`);
      without them the spec passes and the emulator writes nothing.
  11. F5 pressed right behind Escape, while a dialog is still closing, is left to the browser (the rule as written; a
      dialog in its exit transition could count as closed).

### I.10 EXe (verified at `a2d0c22a` of `devcast`, second verification; merged as `6fe01759`; one read-only reviewer, one item to fix; the first verification found two problems, both in the bounded waits of the session `core`)

- **Delivered, all eight items of 3.11.** Item 6 is a witness, not a known limit: `arm7n.hoststop` takes stops on the
  last plain instruction of the F window and on the `msr` that ends it only with a request of the AICA latched and F
  set, and the loop that enters a pending FIQ one executed instruction late in the completion of `dc_run` now fails it.
  The tests project still has **259** targets; behind two full runs `git status` of the checkout is empty. Sound lines,
  state hashes and the contract gates M1 and M2 are those of the base.
- **The three sentences of item 7 are applied** in `debugger.h` (H6-19 lives there, not in `dbg_emu.h` as 3.11 said),
  `dbg_regs.h` (H6-33) and `dbg_mem.h` (H6-35); the integrator read them and brought section 2 of this file to the same
  text. The repository's copy of this file is refreshed at M2G.
- **The bounds:** one constant a session (`kBoundMs`); the longest waits of a correct run are under a tenth of a second
  for `arm7` and `writes` and under a second for `core` (the guest about 0.9 s on the interpreter); with all three bounds
  quartered, the 29 bounded targets passed twice under `ctest -j4`. The watch of the session `core` and the test threads
  that make the machine again share one lock (`bound.restart`).
- **Known, accepted by the integrator (test code, a failure path only):** a window remains between a test thread of
  `core.lifetime` / `core.writes` finding the machine Stopped and taking `bound.restart`, in which the watch can start the
  machine that the thread then takes apart. It is reached only when the session is already failing and the machine
  stops within milliseconds of the 60 s bound; a probe shows the state, no effect was seen in six runs, and it was there
  before the fix round. The fix (check the lifetime and `bound.cut` under the lock) and its mutants are in
  `exe/verify-2/scripts/mutants_v2.py` if a later package owns the file.
- **Also accepted:** a test's own thread that holds the machine longer than the bound and a tenth ends the session by the
  watch's own FAILED line without a summary (documented in the code and the README); the watches of the sessions `arm7`
  and `writes` can print an empty "asked the machine for" when the machine is already stopped at the cut.
- **For a later header revision:** two neighbouring sentences on `Write()` (H6-32's "Values" paragraph and the file
  comment of `dbg_mem.h`) are the next candidates of the same kind.

### I.11 EXu (four verifications; item 5 needed a grant the integrator made on 2026-10-15; the head is the integrator's commit `d59336b` on the verified `b3e3600`; the UI pin is moved there)

- **Delivered, all seven items of 3.12.** Item 1, as decided by the package: the memory view stands so that the last row of
  an address space is the panel's last whole row, and what it reads there ends with the address space in as many rows as
  the panel shows (the listing stays `overflow: clip`). Item 5's 0x-only rule needed two assertions of
  `src/state/runControls.test.ts`, which 3.12 did not grant; three rounds stopped there (the engineer rightly did not edit a
  file outside "Owns"), and the integrator granted them on 2026-10-15. The emulator writes every value as `0x` and the
  digits, so nothing against the emulator changes.
- **Accepted departure:** `e2e/disassembly.spec.ts` (`30c2998`), outside "Owns": its test pinned the slice of sixty rows at
  the end of the address space, which item 1 replaces; it now holds where the view stands and what it reads there.
- **The fourth verification at `b3e3600`:** `npm run check` (584 unit tests, the M2 gate on the mock 17 passed twice), the
  unit suite three more times, e2e 368 passed, the smoke spec against the emulator `6fe01759` with the bundle embedded,
  state hashes and sound lines unchanged; one red step, `e2e/memory.spec.ts` under `E2E_CPU_THROTTLE=4`: the test "bytes the
  debugger cannot read ..." read the answer to the last slice once while the running view may ask for the same slice
  again (3 of 30 runs, the same at `6c753dc`: older than the package). **The integrator applied the verifier's fix**
  (poll for the answer; the assertions unchanged) and the test values that hold both anchors of the pattern and the 0 of
  its prefix (`0x10zz`, `0x200MHz`, `R0=0x10`, `x10`; the three surviving variants of the pattern now fail) as `d59336b`,
  and ran: the unit tests of `src/state` (172), `tsc -b`, eslint on the two files, the test 30 of 30 under the throttle,
  and the memory and watches specs whole under the throttle (70 passed). M2G runs the whole gate of the UI again.
- **Known, accepted:** whether About closes synchronously (outside "Owns": a key pressed right after a click on its Close
  button); the leftover clone of the third fix round was deleted by the integrator (its head was in the repository).
