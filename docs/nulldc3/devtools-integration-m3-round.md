# M3 round — the cut

> **This copy.** `docs/nulldc3/devtools-integration-m3-round.md` is the repository's copy of the working document
> `M3-ROUND.md` as it stood when header revision 7 was made (package R7, 2026-10-17): the cut with the project
> owner's answers (7.2) and the reviews of the cut (Appendix R); the integrator's notes (Appendix I) are still empty.
> Package M3G replaces it by the file as it stands at the end of the round. The text below names its companion
> documents by their working names; in `docs/nulldc3/` they are:
>
> | Named below | In `docs/nulldc3/` |
> | --- | --- |
> | `M3-ROUND.md`, "this file", "this document" | `devtools-integration-m3-round.md` (this file) |
> | `TRACK1-DESIGN.md`, "the design" | `devtools-integration-design.md` |
> | `M2-ROUND.md` | `devtools-integration-m2-round.md` |
> | `DESIGN-V3-OPEN.md` | `devtools-integration-open.md` |
> | `DESIGN-V3-DECISIONS.md` | `devtools-integration-decisions.md` |
> | `PLAN.md` | `PLAN.md` |
>
> "Beside this file" is the working area: the reader reports of `understand-m3/`, the briefs of `m3-briefs/` and
> every path of the form `agents/nulldc3/...` point outside this repository (`docs/nulldc3/README.md`), and so do the
> paths of the caique checkout (`model/...`). The design in `docs/nulldc3/` is not at this cut yet: section 8 lists
> where the two differ and is applied at the end of the round, and until then this file is the contract (M3-D1).

Status: cut, 2026-10-16; revised the same day after the adversarial review of round 1 (44 findings of three readers),
again after its round 2 (4 findings), and a third time (round 3) with the project owner's answers to M3-Q1 to M3-Q4: a
package W3 clocks the AICA whatever ARMRST says, speed gates nothing, the device writes and a BIOS workload are not in
M3; then once more after the review of that round (12 findings of two readers). Appendix R has each finding and each
answer and what became of it. Nothing is open for the owner (7.2). Documents
only: nothing was built or run for it. It is the document design
6.3 asks for ("E8 for M3 (behind the wiring of the T5.0 core) ... from a cut of its own"). Packages start from here; the
design text is brought to it by the list of section 8 at the end of the round (package M3G). **Until then, where this
file and the design differ, this file is the contract** (M3-D1), and a design section a brief names is read with its rows
of section 8.

State it starts from. Emulator: repository `devcast` (on GitHub `skmp/nulldc3`), integration branch `skmp/nulldc3`
= `main` = `165775e5` (the tree of `54f21352`; 278 ctest targets; the executable is `nulldc3`, portable by default,
switches with two dashes). Its submodule `devtools/` pins `ed4da47`. UI: repository `dreamcast-devtools`, branch
`skmp/nulldc3` = `main` = `18435de` (the tree of `0bbd8d1`, a CI / deployment commit on `ed4da47` that touches no
file of this round). Design: `TRACK1-DESIGN.md` status v3; M1, M2, D1 and N1 are complete; header revisions 2 to 6
are applied (Appendix B of the design), so the revision of this round is **7**.

Inputs: the four reader reports in `understand-m3/` (`wiring.md`, `debugger.md`, `devtools.md`, `guest-gate.md`),
the design sections they name (1, 2.1 to 2.11, 3.1 to 3.6, 4.5, 5.3 to 5.7, 6.2, 6.3, Appendix B), `M2-ROUND.md`
(the model of this file; its section 6 and its Appendix I), `PLAN.md` (ground rules; tracks 1, 2, 5, 8, 10) and
`DESIGN-V3-DECISIONS.md`. The readers' file and line references are not repeated here; a statement of this file that
rests on one of them names the report the first time. Code is cited as a repository-relative path and a function.

The method is that of rounds 1 and 2 and is not restated per package: one worktree and one build directory per
emulator package (`agents/nulldc3/wt/<pkg>`, branch `skmp/nulldc3-<pkg>`), UI packages one after another on the UI
branch; a package edits only what it owns; the shared headers are frozen and change only by the numbered revision of
the integrator (R7, section 2); every package has a gate and is checked by a verifier who did not write it, with
mutations; the integrator merges. One workflow at a time on this machine: `ninja -j8`, `ctest -j4`, at most four
emulator processes, every emulator run headless. **Time neutrality is the rule every package is held to: a debugger
that is not used changes no guest-visible state.** Its witnesses are the state hashes of design 5.7 and the sound lines
of design 5.6 (from W1 on with a field for the DSP, M3-D12). Instruction counts and speed are measured and reported
where a package changes a path every run takes, and gate nothing (5.4, the owner's answer to M3-Q2). What this round
changes on purpose — what the DSP computes, and that the AICA runs while the guest holds the ARM7 — is justified by an
oracle or a witness, never by a stored line (section 5).

Names used below: decisions of this cut are **M3-D1 ...** (7.1), the questions the owner has answered **M3-Q1 ...**
(7.2), items of the header revision **H7-...** (section 2), packages **F4a, R7, W1, F4b, W2, W3, F4c, G, E8a, E8b, F4d,
M3G** (section 3). The baselines of the round are **B0 ... B4** (5.3).

Contents: 1 scope · 2 header revision 7 · 3 packages · 4 order · 5 the sound lines and the speed through the round ·
6 the guest's DSP program · 7 decisions, questions, risks · 8 changes to the design text · 9 the briefs · Appendix I
the integrator's notes · Appendix R the review of the cut.

---

## 1. Scope of M3

### 1.1 In

| What | Design | Package | As cut |
| --- | --- | --- | --- |
| The T5.0 core is the emulator's DSP: the per-sample path runs `AicaDsp`, the CPU's accesses to the DSP's registers go to it, the old interpreter and the arm64 DSP recompiler are deleted | D2, 2.5, 2.5.4, PLAN T5.0 | W1 | the sample split into phases (M3-D9); sends as per-bus sums behind `SweepBegin` (M3-D8); the register file a mirror of CPU writes (M3-D5); the line's `dsp` field (M3-D12); save state V5 (M3-D13); justified by the caique cases on the emulator's own AICA (5.2) |
| The batch path and its setting go: the per-sample path is the only one | D2, 2.5.5, PLAN track 10 | W2 | every per-sample line and state hash unchanged, the default's `samples` becomes the old per-sample value (5.1); `.batch` targets and `arm7.persample` retired; "Disable Sound" mutes (M3-D16) |
| The AICA clocked whatever ARMRST says: the sound generator's samples, the DSP, the timers and the interrupt lines run while the guest holds the ARM7; only the ARM7 is not clocked | 2.5, 2.5.2 to 2.5.4, PLAN track 5 ("timers/interrupts not gated by the ARM") | W3 | the owner's answer to M3-Q1 (M3-D7); its own package between W2 and G (M3-D41); the lines it moves shown by the `held` targets and the rule of 5.1 (M3-D42); a held burst's samples pace the stream (M3-D43) |
| The guest's DSP program | 5.5, 5.6, 6.3 (E8 row) | G | its own package between W3 and E8a (M3-D18); playground syntax assembled by `gen.sh`; lines justified by the caique model (section 6) |
| DSP single step and code breakpoints on the active backend, the DSP datapath registers | 2.5.4, 2.8, 3.3 | E8a (R7 for the hand-over in the core) | a step executes one DSP step and never publishes `running` (M3-D20); the end of a sample is a suspension point (M3-D21); nine read-only datapath rows (M3-D25) |
| SGC frame capture (`state.recordSgcFrames`, `state.getSgcFrameData`), `state.getMemoryBinary`, the binary frames | 3.3, 3.4, 3.6 | E8b | the capture's rules of M3-D27; a deferred response in the core (M3-D28); binary messages counted apart by the transport (M3-D29) |
| The removal of the three forcing names | 2.5.5 | R7 (emulator), F4a (UI) | in the header revision, before the wiring (M3-D3) |
| The contract of M3: the mock as an M3 server, the runner's M3 rules (S01, S10, S17 with its `memoryBinary` part, S17 and S21 with a capture and a DSP step accepted while the ARM7 is held, S21 with its program wait) | 4.5, 5.5, decision 35 | F4a | the first package of the round (M3-D31, M3-D32, M3-D44) |
| The playground on the emscripten build of the one C++ core | D2, 4.5, PLAN T5.0 | F4b | the module committed in the UI, regenerated from the emulator tree with a pinned emscripten, held equal to native by the emulator (M3-D34) |
| The SGC panel on the v3 contract | 4.5 | F4c | short and empty captures, the flags byte, no copy per frame (`devtools.md` 1.1) |
| "Load from emulator", the DSP's controls whatever the ARM7 does, the smoke spec's M3 part | 4.5 | F4d | paused only, in a workspace (M3-D35); the DSP is never "held" (M3-D7) |
| The gate, the documents, and the two review PRs | 1 (M3 row), 5.5.3 | M3G | the UI's PR first, then the emulator's (the owner's instruction of 2026-10-16) |

### 1.2 Not in M3

| What | Design row | Goes to | Why |
| --- | --- | --- | --- |
| Writes of the AICA register file (`dc.aica.common`, `dc.aica.sgc.N`, the RTC) and of the DSP's rows; the body of `AICA::DebugWriteReg` | 6.3 (E8 row), `M2-ROUND.md` M2-Q1 | M4, with the write table of every device (E9) | the owner's answer to M3-Q3 ("Move to M4"): each register needs its write semantics stated; the AICA has registers that act when written (`KYONEX`, the interrupt reset and enable registers, `ARMRST`, the DMA start bits), and S16's probe writes the value a register reads; a write inside a burst suspended at a DSP step has no rule. One design step for all device tables |
| The SGC's sends and fetches in time: `ChannelFetch`, `BusRead`, `MixsSendLate` | 2.5.4 | PLAN track 5, the `cycle-similar` AICA | the fast tier's sound generator has no fetched word and no slot; the CPUs' wave-RAM reads go through the fast memory map. Documented as known limits (M3-D10) |
| What the fast tier's time step computes where the console differs: bit 10 (`SAMPLE_DONE`) set in `SCIPD` only (the console sets it in `MCIPD` too), the timers' prescaler, the monitor values | PLAN track 5, "fast fixes" | track 5 | W3 changes when the time step runs, not what it computes (M3-D7, M3-D11); caique's `model/NOTES.md`, "Timers and interrupts", has the console's rules |
| The channel arithmetic (`pow()` tables, no filter, the send law) | PLAN track 5 | track 5 | the DSP is what M3 changes; the channels stay as they are (M3-D11) |
| SGC record bytes 76..83 (filtered, post-AEG, post-FEG, post-TL samples) | 3.4 | the `cycle-similar` AICA (caique's sound generator has them) | not computed by this sound generator: they are 0 (M3-D27) |
| The step view of the DSP (`AicaDspStepView`: operands, `SHIFTED`, `X`, `Y`, `B`) on the wire | 2.5.4 | later; no milestone | no wire form exists and nothing of M3 needs it (M3-D25) |
| A DSP callstack | 2.10, 3.2 | not planned | the DSP has no calls |
| The playground's sound generator, channel fetches and bus reads | 4.5 | not planned | the playground is the DSP alone; "Load from emulator" continues the emulator's DSP exactly from a stop inside a sample's DSP phase (`STEP` 1 to 127) to the end of that sample, and says what it cannot know at any other stop (M3-D35) |
| A boot of the console's BIOS among the state-hash workloads | 5.7 | a package of its own once the owner names a data path for a BIOS | the owner's answer to M3-Q4 ("Not in M3"): no data path of the tests project names a BIOS, `tests/system` has no such case, and no package since E5 has run one (M3-D38) |
| An empty-program fast path in the core, or any other change made for speed | PLAN T5.0 | not planned; speed is taken up when the owner asks for it | the owner's answer to M3-Q2 ("don't mind perf for now"): speed gates nothing in M3 and the lever of round 2's 5.4 is not started (M3-D15); the core is T5.0's and no package of the round edits it |
| Timing tiers, the arm64 run of anything, the wasm32 emulator | PLAN tracks 3 to 6, A64, track 8 | those tracks | arm64 is read, not run: W1 deletes the arm64 DSP recompiler by reading (M3-D14) |
| One cohesive web UI, the imgui GUI's removal | PLAN track 10 | track 10 | W1 and W2 remove only the two DSP settings and their two imgui controls |

---

## 2. Header revision 7

Made by the integrator as package R7, **before the wiring** (M3-D3): the wiring needs one of its declarations (the
DSP's `RBPL` for the debugger, H7-5), would otherwise leave four frozen comments false for most of the round, and
every package behind it is then cut against frozen text. It has every declaration that crosses a package boundary or
stands in a frozen header, and every comment of a frozen header that M3 makes false. The texts below are the texts as
they stand in the headers after the revision (indentation as the file has it). A comment is given whole where more
than a sentence of it changes; "..." inside a quoted comment stands for lines that stay as they are.

Rules of the revision:

- Appendix B of the design gains `libswirl/debugger/dbg_dsp.h` and `libswirl/debugger/dbg_sgc.h` in its list of
  frozen headers (section 8, applied at M3G). R7 changes no text of the design; it adds the repository's copy of this
  file (2.5 item 8). From R7's merge on the headers are frozen by this file.
- No existing signature changes. Removed: `NOTICE_DSP_FORCED` and the row `EV_DEBUGGER_DSP_FORCED` (H7-7,
  H7-8). Added: the enumerators of `libswirl.h` (2.4, not a frozen header). No enumerator is added to `ErrorCode`,
  `StopCause`, `Target`, `Category`, `CallClass`, the bounds or the event table.
- **A frozen comment carries no character outside ASCII** (`M2-ROUND.md` I.2).
- The revision is delivered with **stub bodies** and with **the wiring of the core against the stubs** (2.5): the
  tree builds and behaves as before it, bit for bit, and `debugger.describe` is byte for byte what it was.
- Three clauses describe a state that a later package ends; R7 leaves them in place and the package named in 2.6
  replaces them in the commit that ends that state. Those are the only edits of a frozen header that a package of this
  round other than R7 makes.
- Comments that describe behaviour W1, W3, E8a or E8b builds (the phases of a sample and `AICA::SampleMix`, which W1
  names; a DSP and a capture that do not depend on the ARM7, which W3's clock makes true; the DSP's checks, the end of
  a sample, the capture) are true of the tree from that package on; between R7 and it the functions they describe do
  not exist yet or are unreachable (`dbg_dsp_armed` and `dbg_sgc_active` are 0, `TargetSteps(Target::Dsp)` is false,
  `dbg::sgc::Arm` answers `kErrNotSupported`). E8a and E8b come behind W3 (section 4): no DSP step and no capture ever
  meets a burst whose AICA is not clocked, so revision 7 declares nothing for one (M3-D7).

### 2.1 New declarations in frozen headers

#### H7-1 `libswirl/debugger/debugger.h` — the DSP block and the SGC block

Behind the ARM7 block, inside `#if defined(NULLDC_DEBUGGER)`. The names are global, as those of the SH4 and ARM7
blocks are.

```cpp
/*
	DSP code breakpoints and steps: what the sound burst calls (dbg_dsp.cpp). Everything is (E) only.

	The DSP phase of a sample comes behind the sound generator's sweep of that sample (AICA::SampleSweep) and in
	front of its mix (AICA::SampleMix): the steps of the DSP core (hw/aica/dsp_core, AicaDsp) from the step it
	stands on to the end of the sample (DSP::RunPhase). It runs through AicaDsp::RunSample() while dbg_dsp_armed
	is 0, and through the debug loop while it is not: per step, the check below, then AicaDsp::StepOne(). The two
	execute the same steps: what the DSP computes, on every host, does not depend on whether anything is armed, and
	no notice is given for the DSP. A phase suspended in front of a step is gone on with by RunSample() or by the
	debug loop, whatever is armed then: both execute the rest of a sample from any step.
	Every step executes in every sample, also while the guest holds the ARM7 in reset: the AICA is clocked
	whatever ARMRST says, and only the ARM phase of a sample is skipped then. An active breakpoint of the DSP is
	reached in every sample, and a resume from it stops in front of the same step one sample later.
*/

// Not 0 while the DSP phase has to use the debug loop: an enabled listed breakpoint of the DSP exists and the
// active mask has CategoryBit(Category::Dsp), or a step of the DSP is armed. Tested once per sample
// (dbg_dsp_armed != 0); zero keeps the DSP phase to one load and a branch. It changes in the park context, or
// under L while no emulator thread exists: never while a burst executes.
extern u32 dbg_dsp_armed;

// The check. From the debug loop only, in front of step `step` (AicaDsp::step: the step that executes next,
// 0..127); nothing of that step has executed. Never blocks.
// Returns 0 to continue: nothing of the guest was changed.
// Returns non-zero to suspend the burst in front of the step (SOUND_BURST_DSP, libswirl.h). It has recorded the
// stop (dbg::emu::OnSoundStop). The caller returns without executing anything more of the DSP, the ARM7 or the
// AICA: the scheduler callback returns its period as usual, VirtualDreamcast::RunSoundBurst returns false. Where
// the burst stands is kept by the core (its step) and by the AICA (the direct mix of the sweep).
// What stops: an active listed breakpoint on `step`, unless the resume exemption covers this check; and, while a
// step of the DSP is armed, every check but the first one behind PrepareStep() (dbg::dsp::PrepareStep).
u32 dbg_dsp_Check(u32 step);

// From the sound burst, behind AICA::SampleIrq of every sample, while dbg_dsp_armed is not 0 and the burst runs
// with checks. Returns non-zero when a step of the DSP is complete here: its step was the last of the sample (127)
// and no check came behind it. It has recorded the stop (dbg::emu::OnSoundStop with StopCause::Step); the caller
// suspends the burst at the end of the sample (SOUND_BURST_SAMPLE), or, behind the last sample of the burst, runs
// the rest of the callback and ends the burst with none suspended. Otherwise 0. Never blocks.
u32 dbg_dsp_SampleEnd();

/*
	SGC frame capture: what the sound burst calls (dbg_sgc.cpp; dbg_sgc.h has the record). Everything is (E) only.
*/

// Not 0 while an SGC capture records. Tested once per sample by the sound burst, behind the sound generator's
// sweep (AICA::SampleSweep), and once per sample by the sound generator, which keeps the outputs of each channel's
// step of the sample while it is not 0 (AicaChannelDebug::outLeft). It changes inside a call (the park loop, a
// service excursion, the drain of the call queue in emu::OnThreadExiting), in dbg_sgc_Frame(), in
// dbg::sgc::Cancel from emu::OnThreadExiting (behind that drain) and emu::OnGuestStateReplaced, or under L while
// no emulator thread exists: never while a burst executes. A capture
// armed while a burst is suspended in front of a DSP step takes its first frame in the next sample: the sweep of
// the sample it was armed in came before it.
extern u32 dbg_sgc_active;

// From the sound burst only, behind AICA::SampleSweep of a sample and in front of its DSP phase, while
// dbg_sgc_active is not 0. Takes the frame of that sample (dbg_sgc.h): the guest is only read. Never blocks.
// checks: the burst runs with the debugger's checks; false only where dc_run runs a suspended burst to its end.
// When this frame is the last of the capture, the capture is complete: its buffer becomes the server's, the
// core's notice is given (dbg::sgc::SetCompletionNotice) and dbg_sgc_active becomes 0. Returns non-zero when,
// besides, the capture was armed at a stop and checks is true: the stop is recorded (dbg::emu::OnSoundStop with
// StopCause::Capture), and the caller runs the rest of the sample and suspends the burst at its end
// (SOUND_BURST_SAMPLE), or, behind the last sample of the burst, runs the rest of the callback and ends the burst
// with none suspended - unless a check of the DSP in the rest of the sample suspends it first. Otherwise 0.
u32 dbg_sgc_Frame(bool checks);
```

In the `#else` half, beside the stand-ins of the ARM7:

```cpp
constexpr dbg::NoCount dbg_dsp_armed{};
constexpr dbg::NoCount dbg_sgc_active{};

static inline u32 dbg_dsp_Check(u32) { return 0; }
static inline u32 dbg_dsp_SampleEnd() { return 0; }
static inline u32 dbg_sgc_Frame(bool) { return 0; }
```

#### H7-2 `libswirl/debugger/dbg_emu.h` — `SetDspBreakpoints`, behind `SetArm7Breakpoints`

```cpp
// Inside a CC_TABLE call. The enabled DSP code breakpoints, all of them: ids[i] stops in front of step steps[i]
// (0..127). n <= kMaxBreakpoints; the arrays are read during the call only. Forwarded to
// dbg::dsp::SetBreakpoints. It never causes code-cache work and never changes the loop of the SH4. It can change
// dbg_dsp_armed (debugger.h). Like the other setters it also runs under L while no emulator thread exists, with or
// without a machine.
void SetDspBreakpoints(const u32* ids, const u32* steps, u32 n);
```

#### H7-3 `libswirl/debugger/dbg_dsp.h` — new, frozen from this revision

Behind the licence head of five lines that every header of `libswirl/debugger` has (`M2-ROUND.md` I.2).

```cpp
#pragma once
#include "types.h"
#include "dbg_emu.h"

/*
	dbg::dsp: the state behind DSP code breakpoints and steps (dbg_dsp.cpp): the listed set, the active mask, the
	resume exemption, the step. The sound burst reaches it through dbg_dsp_armed, dbg_dsp_Check and
	dbg_dsp_SampleEnd of debugger.h; this header is what run control (dbg::emu) calls. The DSP is clocked
	whatever ARMRST says: nothing here depends on whether the ARM7 is held.

	Terms
	  listed breakpoint  an enabled code breakpoint of the client's table, on a step 0..127. It stops only while
	                     the active mask has CategoryBit(Category::Dsp), and only then does it arm the debug loop
	  resume exemption   armed for one step (PrepareResume). The first check behind it answers "continue" if it is
	                     in front of that step: the step the DSP stands on executes, also with a breakpoint on it.
	                     It ends at the first check that is made, used or not; whenever dbg_dsp_armed changes to or
	                     from 0; at OnStop() and at OnStateReplaced()
	  step               armed by PrepareStep(): the first check answers "continue", every later check stops; when no
	                     check comes behind the step in its sample (it was step 127), the step is complete at the end
	                     of that sample (dbg_dsp_SampleEnd)

	Unless a function says otherwise it is called on (E) in the park context only, never blocks, and never while a
	burst executes. Nothing here touches a code cache: there is one DSP and it compiles no check. Nothing here keeps
	a pointer into the machine: the step the DSP stands on comes through DSP::DebugGetState, asked for at each use.
*/

namespace dbg { namespace dsp {

// The enabled listed breakpoints, all of them (emu::SetDspBreakpoints): ids[i] is on step steps[i], 0..127, any
// order. n <= kMaxBreakpoints; the arrays are read during the call only. Replaces the listed set and updates
// dbg_dsp_armed. Also callable under L while no emulator thread exists, with or without a machine.
void SetBreakpoints(const u32* ids, const u32* steps, u32 n);

// The listed set as SetBreakpoints() got it last: copies the first max pairs, in the order they were given, and
// returns how many the set has. Changes nothing. Same contexts as SetBreakpoints(). For the selftests: it is how the
// hand-over of the core (PushBreakpoints) is checked without a stop.
u32 GetBreakpoints(u32* ids, u32* steps, u32 max);

// The active mask (emu::SetActiveMask); only CategoryBit(Category::Dsp) matters here. Updates dbg_dsp_armed: a
// listed set whose category is not active arms nothing. Also callable under L while no emulator thread exists.
// Before the first call the mask is 0: no listed breakpoint stops.
void SetActiveMask(u32 categories);

// Park loop, for every leave of the park context that can let the DSP execute and is not a step of the DSP: a
// resume (also the first Park() of a thread that resumes the stop of the thread before it), a step of the SH4, a
// step, step-over or step-out of the ARM7. Called in front of running a suspended burst on, or of leaving for the
// next burst. Arms the exemption for the step the DSP stands in front of iff all of this holds, and arms nothing
// otherwise: the last published stop was decided by the DSP (OnStop() recorded that, with the step); the DSP
// stands in front of that step now (DSP::DebugGetState); dbg_dsp_armed is not 0, so that a check will be made.
void PrepareResume();

// Park loop, for a step of the DSP (StepKind::Into), in front of running a suspended burst on or of leaving for the
// next burst. Arms the step and makes dbg_dsp_armed not 0: the first check answers "continue", wherever it is; every
// later check stops with StopCause::Step; dbg_dsp_SampleEnd() completes it where debugger.h says. There is no
// precondition: a step of the DSP is armed whether the ARM7 is held or not.
void PrepareStep();

// Entering the park context for a stop, once per published stop and before the listener, beside
// dbg::sh4::OnStop() and dbg::arm7::OnStop(); also when temporary state must end without a stop: for a guest reset
// and when the emulator thread exits. Never for a service excursion. Removes a step that is armed, clears the
// exemption, updates dbg_dsp_armed, and records the step the DSP stands in front of together with whether the DSP
// decided this stop (dbg_dsp_Check or dbg_dsp_SampleEnd called emu::OnSoundStop since the OnStop() before). That is
// so also when another cause met it in the same stop and won the publication: the hit counts as taken, and a
// resume runs through it.
void OnStop();

// The machine state was replaced (a reset, a loaded state): ends the exemption. Also callable under L while no
// emulator thread exists.
void OnStateReplaced();

} }
```

#### H7-4 `libswirl/debugger/dbg_sgc.h` — new, frozen from this revision

Behind the licence head.

```cpp
#pragma once
#include "types.h"
#include "dbg_emu.h"
#include <string>

/*
	dbg::sgc: the SGC frame capture (dbg_sgc.cpp), behind state.recordSgcFrames and state.getSgcFrameData. The
	sound burst reaches it through dbg_sgc_active and dbg_sgc_Frame of debugger.h; the core (dbg_core.cpp) and run
	control (dbg::emu) call what is below.

	A frame is one sample of the AICA: kChannelsPerFrame records of kBytesPerChannel bytes, channel 0 first, taken
	behind the sound generator's sweep of the sample (AICA::SampleSweep) and in front of its DSP phase. A record,
	little endian (design 3.4):
	  0..71     the channel's registers 0x00..0x47, as AICA::DebugReadRegs gives them
	  72  i16   AicaChannelDebug::sampleCurrent          74  i16  samplePrevious
	  76..83    0: this sound generator computes no filtered, post-AEG, post-FEG or post-TL sample
	  84  i16   outLeft          86  i16  outRight          88  i16  outDsp: the channel's outputs of this sample
	  90  u16   caFraction       92  u32  step              96  u32  aegLevel: 0 silent .. 0x3FF loudest
	  100 u32   fegValue         104 u8   lfoState          105 u8   alfo       106 u8  plfo
	  107 u16   ca, its low 16 bits (unaligned)             109 u8   flags: bit 0 enabled, bits 1-2 aegState
	  110..127  0
	A capture records into a buffer of its own and replaces the server's buffer when it is complete. The server's
	buffer stays until the next capture is complete, also across a reset and a thread exit.

	Everything below is (E), or under L while no emulator thread exists, unless it says otherwise. Nothing here
	blocks.
*/

namespace dbg { namespace sgc {

const u32 kChannelsPerFrame = 64;
const u32 kBytesPerChannel  = 128;
const u32 kBytesPerFrame    = kChannelsPerFrame * kBytesPerChannel;	// 8192

// Arms a capture of the next `frames` samples whose sweep comes behind the call, 1..kMaxSgcFrames, allocating its
// buffer. Inside a CC_EXEC call at a stop, with stopWhenDone true: the caller arms the resume behind it
// (emu::Resume; when that is refused the caller ends the capture with Cancel(kErrNotPaused)), and the sample that
// completes the capture records a stop with StopCause::Capture (dbg_sgc_Frame). Inside a CC_TABLE call of a running
// machine, with stopWhenDone false: nothing stops. (E) only: never called without an emulator thread (a CC_TABLE
// closure that emu::Call runs under L, with no thread, arms nothing). A CC_TABLE closure that the exiting thread
// runs in the drain of emu::OnThreadExiting is on (E) and may arm: OnThreadExiting cancels behind its drain.
// The AICA is clocked whatever ARMRST says: a capture records whether the ARM7 is held or not.
// Returns 0 when armed; otherwise nothing is armed and it returns
//   kErrNotPaused     a capture records already: it goes on, and this request is answered with the code
//   kErrNotSupported  this build has no capture (the stub of header revision 7)
int Arm(u32 frames, bool stopWhenDone);

// Ends a capture that records: its frames are dropped and the server's buffer stays as it was. answer is what the
// notice hands on: kErrCaptureTimeout (the core's deadline, inside a CC_TABLE call; a guest reset, from
// dbg::emu::OnGuestStateReplaced), kErrHostStopped (from dbg::emu::OnThreadExiting, behind the drain of its call
// queue) or kErrNotPaused (inside the CC_EXEC call that armed the capture, when emu::Resume() refused to arm the
// resume behind it). Does nothing when no capture records.
void Cancel(int answer);

enum class State : u8 { None, Recording };

// Whether a capture records, and in *frames how many frames the server's buffer holds (0: none). Inside a CC_READ
// call, or under L while no emulator thread exists.
State GetState(u32* frames);

// Appends the server's buffer, frames * kBytesPerFrame bytes, to *out: the payload of a binary frame of kind
// kBinarySgc. Same contexts as GetState(). Returns the number of frames appended (0: there is no buffer).
u32 Copy(std::string* out);

// The core's notice, installed at dbg::Init, removed (null) at dbg::Term. It is given once for every capture that
// Arm() armed: on (E) inside dbg_sgc_Frame() when the capture is complete (answer 0), and wherever Cancel() ends
// one (answer as given). It names no capture: at most one capture is armed whose notice has not been given, since
// Arm() refuses while one records; the core pairs a notice with its request (M3-ROUND.md M3-D28). It must not
// block or call emu::Call: it queues a message for (C).
void SetCompletionNotice(void (*notice)(int answer));

} }
```

#### H7-5 `libswirl/hw/aica/dsp.h` — the debug block: `DspDebugState`

The struct in place of the one with two fields; the block's comment in 2.3.

```cpp
// What the debugger shows of the DSP beyond the registers in the AICA register file (DSP::DebugGetState).
struct DspDebugState {
	u32 step;       // the step that executes next, 0..127: 0 between two samples; any step while the debugger has
	                // a sound burst suspended in front of a step of the DSP
	u32 mdecCt;     // MDEC_CT, the counter of the ring buffer; its low 16 bits are the register
	u32 rbpl;       // the word the CPU wrote to 0x2804 last, as the DSP uses it: RBP bits 11:0, RBL bits 14:13, the
	                // test bit 15. What a read of 0x2804 by the CPU gives is AICA::DebugReadRegs's
	s32 acc;        // ACC, 26 bits, sign extended
	s32 yReg;       // Y_REG, 24 bits, sign extended
	u32 frcReg;     // FRC_REG, 13 bits
	u32 adrsReg;    // ADRS_REG, 12 bits
	u32 latch;      // the wave RAM read latch, 16 bits
	u32 latchCopy;  // its copy, taken at the end of the last odd step, 16 bits
	u32 memval;     // what an IWT stores, before its conversion, 16 bits
	u32 noflPipe;   // NOFL of the last step (bit 0) and of the step before it (bit 1)
	u32 mixsBank;   // the MIXS bank the DSP and the CPU read, 0 or 1
};
```

#### H7-6 `libswirl/hw/aica/aica_mmio.h` — the debug block: three fields of `AicaChannelDebug`

Behind `plfo`, the last field:

```cpp
	s32  outLeft;        // the channel's outputs of the sample of the last sweep: its direct left and right signal
	s32  outRight;       // and its send to the DSP, at 16-bit scale, while an SGC capture records (dbg_sgc_active,
	s32  outDsp;         // debugger/debugger.h); 0 at any other time
```

### 2.2 Removed declarations

#### H7-7 `libswirl/debugger/dbg_emu.h` — `NOTICE_DSP_FORCED`

The enumerator and its comment go; the notice enum keeps `NOTICE_SH4_FALLBACK = 1`, `NOTICE_SH4_FALLBACK_TIMING = 2`,
`NOTICE_ARM7_FALLBACK = 4` as they are. With it go the branch of `NoticeText` (`dbg_core.cpp`) and nothing else: the
notice was never set.

#### H7-8 `libswirl/debugger/dbg_events.h` — the row `EV_DEBUGGER_DSP_FORCED`

The row `X(EV_DEBUGGER_DSP_FORCED, "dc.debugger.dsp_forced", ...)` goes. The ids behind it move by one; ids never leave
the process. With it: `tests/debugger/regs/regs_test.cpp` names the constant (the exception of decision 42 goes: every
row of the table is listed by exactly one node); the comment of `dbg_regs_tables.cpp` above the node table; the probe of
`dbg_selftest_core.cpp` that uses `dc.debugger.dsp_forced` as "a row that no node lists" (it becomes a probe of a name
that is in no row: `-32004` as before). All of them R7's (2.5).

### 2.3 Comment-only changes in frozen headers

#### `libswirl/debugger/debugger.h`

- **The ARM7 block's introduction**, whole (the three bracketed clauses go with a body, 2.6: the first with W3's, the
  other two with W2's):

  ```cpp
  /*
  	ARM7 code breakpoints and steps: what the sound burst calls (dbg_arm7.cpp). Everything is (E) only.

  	A sound burst is the work of one AICA scheduler callback (Dreamcast_impl::AicaUpdate): [if the ARM7 is
  	clocked, ]32 samples, each { the ARM phase: the ARM7 for its ticks; the time step of the AICA: its timers,
  	the sample [ - in the batch mode, while it exists, made by the batch render instead - ] (the sound
  	generator's sweep, the DSP phase of the DSP block below, the mix), the interrupt lines }, then the rest of
  	the callback ([the batch render while it exists, ]the tick of the sound regression harness).
  	The ARM phase runs through the backend's Run() while dbg_arm7_armed is 0, and through the debug loop while
  	it is not. ...
  	A burst is suspended in front of an ARM7 instruction, in front of a DSP step, or at the end of a sample
  	that is not its last (in front of the ARM phase of the next sample, nothing of which has executed), and
  	nowhere else. A suspended burst is state of the machine (VirtualDreamcast::SoundBurstSuspended, libswirl.h).
  	It is run on by the park loop or by dc_run, never by the scheduler: while a burst is suspended the stop
  	latch is set and the AICA callback cannot run.
  */
  ```

  The square brackets mark the three clauses; they are not in the text.
- **`SliceHook()`**, item 2: "(an event breakpoint; a breakpoint or a step of the ARM7, which the sound burst of this
  scheduler tick recorded: emu::OnSoundStop)" becomes "(an event breakpoint; a breakpoint or a step of the ARM7 or of
  the DSP, or the end of an SGC capture armed at a stop, which the sound burst of this scheduler tick recorded:
  emu::OnSoundStop)". "Lifetime Parked (a step is executing: a step of the SH4, or the run of the SH4 towards the sound
  burst that a step of the ARM7 waits for)" becomes "... that a step of the ARM7 or of the DSP waits for)". The last
  sentence, "A sound burst can be suspended when the hook runs its calls: the ARM7 then stands between two
  instructions.", becomes "A sound burst can be suspended when the hook runs its calls: the ARM7 then stands between two
  instructions, and the DSP between two steps or at the end of a sample."
- **`OnThreadExiting()`**: "ends what belonged to a stop (the temporary breakpoints of both processors, a pending
  step-out, a step of the ARM7 that is armed); drains the call queue under L: CC_READ and CC_TABLE calls run, CC_WRITE
  and CC_EXEC calls complete with -32001; drops a pause request" becomes "ends what belonged to a stop (the temporary
  breakpoints of the SH4 and the ARM7, a pending step-out, a step of the ARM7 or of the DSP that is armed); drains the
  call queue under L: CC_READ and CC_TABLE calls run, CC_WRITE and CC_EXEC calls complete with -32001; cancels an SGC
  capture that records, also one that a CC_TABLE call of the drain armed (dbg::sgc::Cancel: its request is answered
  -32001); drops a pause request". **The cancel stands behind the drain**: a `CC_TABLE` call that waits in the queue
  when the thread exits is run by that thread, on (E), where `emu::OnEmuThread()` is still true and the ARM7 may be
  clocked, so a `recordSgcFrames` closure among them arms a capture (H7-4 `Arm`); a cancel in front of the drain would
  find none, and the thread would exit with `dbg_sgc_active` 1, its request answered `-32014` at the deadline, and a
  thread the host starts within those 5 s recording into it (Appendix R, S16).
- **`OnGuestStateReplaced()`**, the list and the calls: "... the temporary breakpoints of both processors, a pending
  step-out, a step of the ARM7 that is armed, and the resume that a thread exit taken in a debugger stop left owed."
  becomes "... the resume exemption and a step of the DSP that is armed, the temporary breakpoints of the SH4 and the
  ARM7, a pending step-out, a step of the ARM7 that is armed, an SGC capture that records (dbg::sgc::Cancel: its
  request is answered -32014), and the resume that a thread exit taken in a debugger stop left owed."; "On (E) it calls
  dbg::sh4::OnPcWritten() and OnStop(nullptr), and dbg::arm7::OnPcWritten() and OnStop(). While Stopped it calls the two
  OnPcWritten() only: no temporary breakpoint, no step-out and no armed step exist then, because OnThreadExiting() ended
  them." becomes "On (E) it calls dbg::sh4::OnPcWritten() and OnStop(nullptr), dbg::arm7::OnPcWritten() and OnStop(),
  and dbg::dsp::OnStateReplaced() and OnStop(). While Stopped it calls the two OnPcWritten() and
  dbg::dsp::OnStateReplaced() only: no temporary breakpoint, no step-out, no armed step and no capture exist then,
  because OnThreadExiting() ended them."
- **`Park()`**, four places:
  1. "Entering a stop: g_parked is set, CpuRunning cleared, the breakpoint state of the SH4 and of the ARM7 drop their
     temporary breakpoints, a pending step-out and a step that is armed, ..." becomes "... the breakpoint state of the
     SH4, of the ARM7 and of the DSP drop their temporary breakpoints, a pending step-out and a step that is armed, ...";
     "Armed steps are executed by the loop itself, with one exception: a step of the ARM7 asked for while no sound
     burst is suspended (below)." becomes "Armed steps are executed by the loop itself, with one exception: a step of
     the ARM7 or of the DSP asked for while no sound burst is suspended (below)."
  2. The leave, whole:

     ```cpp
     // - Leaves the park context for a resume and at the end of a service excursion, in this order: for a
     //   resume decides the resume exemptions of the ARM7 and of the DSP (dbg::arm7::PrepareResume,
     //   dbg::dsp::PrepareResume); runs a suspended sound burst on, with the checks of the ARM7 and of the DSP
     //   (VirtualDreamcast::RunSoundBurst(true)): if a breakpoint of the ARM7 or of the DSP suspends it again, or
     //   an SGC capture armed at this stop completes in it (also in the last sample of the burst, which then ends
     //   with none suspended: a stop that emu::OnSoundStop recorded during the run-on is the stop all the same),
     //   nothing is left, the thread stays parked, that stop is published (the stop counter advances) and the
     //   resume, or the step of the SH4 the burst was run on for, is dropped; otherwise clears g_stop_latched;
     //   makes the interrupt acceptance that the latch deferred; calls
     //   sh4_host_fp_resync(); for a resume arms the resume exemption of the SH4; sets CpuRunning and clears
     //   g_parked, unless a host stop or a guest exit is pending; for a resume invokes the listener with Running.
     //   A step of the SH4 decides the resume exemption of the DSP before it runs a suspended burst on, as a
     //   resume does. No burst is ever suspended at the end of a service excursion: a suspension is followed by a
     //   stop.
     ```

  3. In the bullet "A step of the ARM7 (emu::Step). ...", its first sentence, "With a burst suspended the loop runs the
     burst on, in the park context, until the check behind the instruction stops it or the burst ends: no emulated time
     passes, ..." becomes "With a burst suspended the loop runs the burst on, in the park context, until the check
     behind the instruction stops it, or a check of the DSP or the end of an SGC capture armed at a stop in the rest of
     the burst stops it first (that stop is published with its own cause and the step ends), or the burst ends: no
     emulated time passes, ...", and behind that sentence: "A burst suspended in front of a DSP step is run on through
     the rest of that sample to the ARM phase of the next, where the instruction is; in the last sample of the burst
     there is none, and the step then waits for the next burst as with none suspended." Behind that bullet, a bullet of
     its own:

     ```cpp
     // - A step of the DSP (emu::Step) is executed the same way, with the exemptions of the ARM7 decided first
     //   (dbg::arm7::PrepareResume) and the step armed (dbg::dsp::PrepareStep). With a burst suspended the loop
     //   runs it on, in the park context, until the check behind the step stops it, or until the end of the
     //   sample when the step was the last of its sample: no emulated time passes, the SH4 executes nothing,
     //   nothing is published in between; the ARM phase of a sample it runs through uses the checks of the ARM7,
     //   and a stop of the ARM7 that comes first is published with its own cause. With no burst suspended the loop
     //   leaves as for a step of the ARM7 that waits for its burst, and everything that sentence says of the wait
     //   holds for it; the step is step 0 of the first sample of that burst. A step of the ARM7 decides the resume
     //   exemption of the DSP (dbg::dsp::PrepareResume) before it runs a suspended burst on.
     ```

  4. The owed resume: "The ARM7 has no part in it: no sound burst is suspended then, because the thread before ran it to
     its end in front of its stop callback, and so the instruction of a stop of the ARM7 has executed. The
     dbg::arm7::PrepareResume() that dbg::emu calls there as well arms nothing: OnThreadExiting() has ended the stop for
     the breakpoint state of the ARM7 (dbg::arm7::OnStop())." becomes "The ARM7 and the DSP have no part in it: no sound
     burst is suspended then, because the thread before ran it to its end in front of its stop callback, and so the
     instruction or the step of a stop of the ARM7 or of the DSP has executed. The dbg::arm7::PrepareResume() and
     dbg::dsp::PrepareResume() that dbg::emu calls there as well arm nothing: OnThreadExiting() has ended the stop for
     both (dbg::arm7::OnStop(), dbg::dsp::OnStop())." And in the returns: "g_parked is clear too, but for a step of the
     ARM7 that waits for its burst." becomes "... but for a step of the ARM7 or of the DSP that waits for its burst."
- **`dbg_arm7_OnBurstUnclocked()`**, whole. Its text was written for a burst whose only checks were the ARM7's: from
  W3 on a held burst makes its samples, and from E8a on their DSP phases make the DSP's checks, which can suspend that
  burst, a DSP step can be armed when it starts, and the DSP has a resume exemption of its own, which a held burst does
  not end (E8a's `dsp.bp.resume`). The text names the ARM7 where it means it, and says what the rest of the burst is by
  the block's introduction, whose clause of 2.6 W3 replaces, so that it is true from R7 on:

  ```cpp
  // From the AICA scheduler callback only, at the start of a burst whose ARM7 is held in reset, while
  // dbg_arm7_armed is not 0: no instruction of the ARM7 executes in this burst and no check of the ARM7 is
  // made. A step of the ARM7 that is armed is complete here with nothing executed: the stop is recorded
  // (dbg::emu::OnSoundStop with StopCause::Step) and the slice hook latches it behind this scheduler tick.
  // It does not suspend the burst: the callback goes on with its rest, what the introduction of this block
  // says a burst is (a check of the DSP there can suspend it). Breakpoints are left alone. A resume exemption
  // of the ARM7 that is armed ends here: a held ARM7 takes its reset exception when it is released, and its
  // first instruction then is not the one the exemption was for. Never blocks.
  void dbg_arm7_OnBurstUnclocked();
  ```

#### `libswirl/debugger/dbg_emu.h`

- The file's comment: "This header and those that include it (dbg_sh4.h, dbg_arm7.h, dbg_core.h, dbg_mem.h,
  dbg_callstack.h, dbg_selftest.h)" becomes "(dbg_sh4.h, dbg_arm7.h, dbg_dsp.h, dbg_sgc.h, dbg_core.h, dbg_mem.h,
  dbg_callstack.h, dbg_selftest.h)".
- `StopCause`: "the first of Reset, Start, Breakpoint, Event, Step, Pause." becomes "the first of Reset, Start,
  Breakpoint, Event, Step, Pause, Capture." The comment of `Capture` becomes `// "capture"  an SGC capture armed at a
  stop took its last frame (dbg::sgc::Arm with stopWhenDone)`.
- `kErrNotClocked`: `// "arm7 held in reset": a step of the ARM7 while the guest holds it in reset; the DSP and
  the capture never answer it`.
- `RunState::stopId` and `SetListener()`: "on a stop that the ARM7 check makes while the park loop runs a suspended
  sound burst on" becomes "on a stop that a check of the ARM7 or of the DSP, or the end of an SGC capture, makes while
  the park loop runs a suspended sound burst on" (in both comments).
- `LatchStop()`: "The sound side does not call it: a breakpoint or a step of the ARM7 is recorded through
  OnSoundStop, below, and latched by the slice hook." becomes "The sound side does not call it: a breakpoint or a step
  of the ARM7 or of the DSP, or the end of an SGC capture armed at a stop, is recorded through OnSoundStop, below, and
  latched by the slice hook."
- `Call()`, the classes: "CC_TABLE: also SetSh4Breakpoints, SetArm7Breakpoints, SetEventBreakpoints, SetActiveMask.
  CC_WRITE: also dbg::mem::Write, dbg::regs::Write, Reset. CC_EXEC: also Resume, Step." becomes "CC_TABLE: also
  SetSh4Breakpoints, SetArm7Breakpoints, SetDspBreakpoints, SetEventBreakpoints, SetActiveMask, dbg::sgc::Arm with
  stopWhenDone false, dbg::sgc::Cancel. CC_WRITE: also dbg::mem::Write, dbg::regs::Write, Reset. CC_EXEC: also Resume,
  Step, dbg::sgc::Arm with stopWhenDone true, and dbg::sgc::Cancel behind a Resume that was refused."
- `Resume()`, whole:

  ```cpp
  // Inside a CC_EXEC call. Arms a resume: after the call returns, the park loop leaves the park context and
  // publishes Running. With a sound burst suspended it runs the burst on first (debugger.h, Park()): if a
  // breakpoint of the ARM7 or of the DSP suspends it again, or an SGC capture armed at this stop completes in
  // it, the thread stays parked, that stop is published and Running is not. Returns 0; kErrNotPaused, with
  // nothing armed, if the thread is not parked or an action is already armed.
  int Resume();
  ```

- `Step()`, four places. In `Target::Sh4`'s paragraph, "With a sound burst suspended it runs the burst on first: a
  breakpoint of the ARM7 that suspends it again is published as the stop, and the step is dropped." becomes "With a
  sound burst suspended it runs the burst on first: a breakpoint of the ARM7 or of the DSP that suspends it again, or
  the end of an SGC capture armed at a stop that comes in it, is published as the stop, and the step is dropped." In
  `Target::Arm7`'s paragraph, "With a burst suspended the park loop runs it on in the park context until the check
  behind the instruction stops it: nothing is published in between, ..." becomes "With a burst suspended the park loop
  runs it on in the park context until the check behind the instruction stops it, or a check of the DSP or the end of
  an SGC capture armed at a stop in the rest of the burst stops it first (that stop is published with its own cause
  and the step ends): nothing is published in between, ...", and behind "... the step is complete at the end of the
  burst, with none suspended." the sentence "A burst suspended in front of a DSP step is run on through the rest of
  that sample to the ARM phase of the next, where the instruction is; in the last sample of the burst there is none,
  and the step then waits for the next burst as with none suspended." Then a paragraph behind `Target::Arm7`'s, and
  the return list:

  ```cpp
  // Target::Dsp. Into only: one step of the DSP (dbg::dsp::PrepareStep): the step the DSP executes next executes,
  // and the stop is in front of the step behind it. Running is never published. With a sound burst suspended the
  // park loop runs it on in the park context: suspended in front of a DSP step, that step executes and the check
  // behind it stops; suspended in the ARM phase of a sample, or at the end of the sample in front of it, the burst
  // goes through the rest of that ARM phase (none while the ARM7 is held; with the checks of the ARM7: a stop of
  // the ARM7 that comes first is published with its own cause and the step ends) to its DSP phase, whose first
  // step is the step. When the step is the last of its sample (127), the sample's mix and interrupt update run and
  // the step is complete at the end of the sample: the burst is suspended there, or, behind the last sample of the
  // burst, the burst ends with none suspended. With no burst suspended the park loop leaves without publishing, as
  // for a step of the ARM7: the SH4 runs to the next burst, one period of the AICA callback at most, and the step is
  // step 0 of its first sample; a stop of the SH4 that comes first is published with its own cause. The step
  // executes its step also with a listed breakpoint on it, and whether the ARM7 is held or not: the AICA is clocked
  // whatever ARMRST says.
  //
  // Returns 0 when armed. Otherwise nothing is armed and it returns
  //   kErrNotPaused     the thread is not parked, or an action is already armed
  //   kErrNotSupported  the target cannot be stepped: TargetSteps(t) is false; Over and Out on Dsp always
  //   kErrCannotStep    a precondition of an SH4 step failed (dbg::sh4::StepCheck); *why, if why is not null,
  //                     receives the reason as a constant string
  //   kErrNotClocked    Arm7 while the ARM7 is held in reset (dbg::arm7::StepCheck); never for Dsp
  ```

- `TargetSteps()`: "The DSP: false until M3." becomes "The DSP: true once its run control exists (package E8a of the
  M3 round; false before it)."
- `SetActiveMask()`: "Forwarded to dbg::sh4::SetActiveMask and dbg::arm7::SetActiveMask; it can make UseFallbackLoop()
  change as SetSh4Breakpoints can, and dbg_arm7_armed as SetArm7Breakpoints can." becomes "Forwarded to
  dbg::sh4::SetActiveMask, dbg::arm7::SetActiveMask and dbg::dsp::SetActiveMask; it can make UseFallbackLoop() change as
  SetSh4Breakpoints can, and dbg_arm7_armed and dbg_dsp_armed as their setters can."
- `OnSoundStop()`, its first paragraph and its list, whole:

  ```cpp
  // (E) only, from the checks of the sound burst (dbg_arm7_Check, dbg_arm7_OnBurstUnclocked, dbg_dsp_Check,
  // dbg_dsp_SampleEnd, dbg_sgc_Frame: debugger.h) and from
  // dbg::arm7::CompleteStepAtBurstEnd, when a breakpoint of the ARM7 or of the DSP stops, a step of either is
  // complete, or an SGC capture armed at a stop took its last frame. Never blocks. It records the stop; it latches
  // nothing and leaves nothing. It does not look at the run flag: a check that hits has suspended its burst, or
  // will at the end of its sample, whatever the machine is about to do, and dc_run deals with a burst it finds
  // suspended.
  // ... (the two paragraphs "While the guest executes" and "In the park context" as they are) ...
  //   Breakpoint, id   an active listed breakpoint of the ARM7 or of the DSP stopped; the lowest id when several
  //                    are on the address or the step
  //   Step, 0          the temporary breakpoint of the ARM7 stopped, or a step of the ARM7 or of the DSP is
  //                    complete
  //   Capture, 0       an SGC capture armed at a stop took its last frame
  // When other causes meet it in the same stop the published cause follows the priority of StopCause, and among
  // breakpoints of the winning cause the lowest id, as for LatchStop.
  ```

#### `libswirl/debugger/dbg_arm7.h`

- `PrepareResume()`, its first sentence: "Park loop, for every leave of the park context that can let the ARM7 execute
  and is not a step of the ARM7: a resume; a step of the SH4, with a sound burst suspended or not (its slice end can
  start one); the first Park() of a thread that resumes the stop of the thread before it." becomes "... a resume; a step
  of the SH4, with a sound burst suspended or not (its slice end can start one); a step of the DSP, with a burst
  suspended or not; the first Park() of a thread that resumes the stop of the thread before it."
- `OnStop()`: "beside dbg::sh4::OnStop()" becomes "beside dbg::sh4::OnStop() and dbg::dsp::OnStop()".

#### `libswirl/hw/aica/dsp.h`, the debug block

```cpp
	/*
		Debugger access. Called on the emulator thread between two steps of the DSP - outside a sound burst, or
		while the debugger has one suspended -, or by a thread that holds the debugger's lifetime lock while no
		emulator thread exists. No side effects. The defaults are those of a device that gives the debugger
		nothing.
	*/
```

in place of "Debugger access, independent of the layout of dsp_context_t. Called on the emulator thread outside Step(),
or by ...". `DebugGetProgramStep` keeps its comment.

#### `libswirl/hw/aica/aica_mmio.h`, the debug block

- The block's comment, whole:

  ```cpp
  	/*
  		Debugger access. Called on the emulator thread outside Update() and the time step of a sample (TimeStep,
  		or its phases) - between two sound bursts, or while the debugger has one suspended: in front of an ARM7
  		instruction or at the end of a sample, where the sound generator stands between two samples, or in front
  		of a DSP step, where the sweep of the sample is made (its channels have stepped, its timers have counted)
  		and its mix and its interrupt update are to come -, or by a thread that holds the debugger's lifetime lock
  		while no emulator thread exists; DebugWriteReg on the emulator thread only. Update() and the time step
  		themselves also run in the debugger's park context, when a suspended burst is run on. The defaults are
  		those of a device that gives the debugger nothing.
  	*/
  ```

- `DebugReadRegs()`: behind "0x2C00 is ARMRST, 0x2C01 VREG." the sentence "The registers of the DSP (0x2804,
  0x3000..0x45C7) are what the DSP answers a read of the CPU."
- `DebugWriteReg()`: "in M2 the debugger writes no register of the AICA" becomes "in M2 and M3 the debugger writes no
  register of the AICA" (the owner's answer to M3-Q3: the AICA's writes come with M4).

#### `libswirl/debugger/dbg_net.h` (the API of E1, Appendix B) — a clause that goes with E8b's body

The comment of the send functions, as E8b leaves it (2.6):

```cpp
	// Queue a message and return; nothing here waits for the socket. Unknown or closed clients are ignored.
	// Text and binary messages are counted apart and go out in the order they were queued. A client that lets
	// more than 16 MiB of text, or more than 64 MiB of binary messages, pile up is disconnected: a text message
	// that finds more than 16 MiB of text waiting for the client, or a binary message that finds more than
	// 64 MiB of binary messages waiting, is not queued and the client is dropped. The bounds are not on the
	// message, one of any size is taken: a binary message of the largest size the protocol gives (32 MiB and its
	// header) leaves room for every text message behind it.
	void SendText(uint32_t client, std::string text);
	void SendBinary(uint32_t client, std::string bytes);
	void BroadcastText(const std::string& text);
	size_t QueuedBytes(uint32_t client) const;	// bytes not yet handed to the socket, text and binary together
```

Unchanged, and read again against the code by the verifier of R7: `dbg_regs.h` (its "No other row has a writer" stays
true in M3), `dbg_mem.h` (the `dsp` target has no memory), `dbg_callstack.h` (0 frames for `Dsp`), `dbg_core.h`
("(C) owns the timers: the periodic tick, the capture timeout and the grace period" becomes true with E8b),
`hw/arm7/SoundCPU.h` (the ARM7 stands between two instructions in every suspension; `DebugEnabled()`'s "it is not
clocked and a sound burst executes none of its instructions" stays true with W3, which clocks the rest of the AICA),
`hw/aica/sgc_if.h`; and the texts of the held ARM7 in `dbg_arm7.h` and `dbg_emu.h` (`Step()`, `Target::Arm7`), which
speak of the ARM7 alone (`dbg_arm7_OnBurstUnclocked` of `debugger.h` is in the list above).

### 2.4 Declarations fixed here that are not in a frozen header

Each crosses a package boundary of this round (W1 or W2 declares, E8a or E8b uses, or the other way), so the text is
fixed here; the package named changes it only with the integrator.

**`libswirl/libswirl.h`** (E8a delivers the enumerators and the comments of `SoundBurstSuspended`,
`SoundBurstDspCounts`; W2 the batch clause of `RunSoundBurst`):

```cpp
// The places a sound burst can be suspended at (VirtualDreamcast::SoundBurstSuspended)
enum : u32 { SOUND_BURST_NONE = 0, SOUND_BURST_ARM7 = 1, SOUND_BURST_DSP = 2, SOUND_BURST_SAMPLE = 3 };

    // Emulator thread. Whether a sound burst is suspended, and where: 0 none; SOUND_BURST_ARM7: in front of an
    // ARM7 instruction of the ARM phase of a sample, because the ARM7's check asked (dbg_arm7_Check);
    // SOUND_BURST_DSP: in front of a DSP step, because the DSP's check asked (dbg_dsp_Check): the sweep of the
    // sample is made, its DSP phase has executed the steps in front of that one, its mix and its interrupt update
    // are to come; SOUND_BURST_SAMPLE: at the end of a sample that is not the last of the burst, because a step of
    // the DSP was complete there (dbg_dsp_SampleEnd) or an SGC capture armed at a stop took its last frame in it
    // (dbg_sgc_Frame): nothing of the next sample has executed. In each case the rest of the burst has not run.
    // Not 0 only between such a return and the moment the park loop, dc_run or Reset() has dealt with it; the stop
    // latch is set for all of that time but for the slice end in which it happened.
    // The body here is that of a machine that never suspends
    virtual u32 SoundBurstSuspended() { return 0; }

    // Emulator thread, the CPU is stopped. Runs a suspended sound burst on, outside the scheduler: ... checks
    // true: the ARM phases use the ARM7's check while dbg_arm7_armed is not 0, the DSP phases the DSP's while
    // dbg_dsp_armed is not 0, and the end of a sample stops for a complete step of the DSP or a capture armed at
    // a stop (the park loop). checks false: nothing can stop it (dc_run, in front of the stop callback of a host
    // stop or a guest exit). Returns true when the burst ran to its end, the rest of the callback included (the
    // tick of the sound regression harness: once per burst, behind the last sample), and at once when none is
    // suspended. Returns false when it is suspended again, which it never is with checks false.
    virtual bool RunSoundBurst(bool checks) { return true; }

    // Emulator thread. What the DSP phases of this machine's bursts have done since it was created: out[0] the
    // DSP phases that ran to the end of their sample (DSP::RunPhase returned true), out[1] the DSP phases in which
    // the debug loop executed at least one step. The counts wrap, are in no save state and in no hash, and nothing
    // of the guest reads them. For the selftests of the debugger.
    // The body here is that of a machine that counts nothing
    virtual void SoundBurstDspCounts(u32 out[2]) { out[0] = out[1] = 0; }
```

`SoundBurstCounts(u32 out[4])` keeps its signature and meaning; `out[2]`, "the time steps of the AICA that a burst
made", is counted behind `SampleIrq` from E8a on. A second accessor and not a larger array: a caller with an array of
four would be written past its end (M3-D23).

**`libswirl/hw/aica/aica_mmio.h`, `struct AICA`, outside the debug block** (W1 delivers without the sentence in square
brackets, which W3 adds; E8a calls; W2 removes the batch sentences):

```cpp
	// The tail of a sound burst, once per burst behind its last sample, also when the ARM7 is held: the tick of
	// the sound regression harness (aica_hash.h) and, while it exists, the batch render. Samples is ignored
	virtual void Update(u32 Samples) = 0;

	//Aica reads (both sh4&arm)
	virtual u32 ReadReg(u32 addr, u32 size) = 0;
	virtual void WriteReg(u32 addr, u32 data, u32 size) = 0;

	// One sample of the sound chip, in its phases; emulator thread. The sound burst (Dreamcast_impl, libswirl.cpp)
	// makes them behind the ARM phase of the sample, in this order, and nothing else of the sound chip runs between
	// them[; it makes them in every sample of every burst, also while the guest holds the ARM7 in reset, which
	// skips only the ARM phase]:
	//   SampleTimers(); SampleSweep(); DSP::RunPhase(checks); SampleMix(); SampleIrq();
	// TimeStep() is the five with RunPhase(false), for callers outside the sound burst. While the batch render
	// exists, SampleSweep() and SampleMix() do nothing unless config:aica.NoBatch is set, and the DSP phase is not
	// made then either
	virtual void SampleTimers() = 0;   // the three timers count one sample; SAMPLE_DONE is set
	virtual void SampleSweep() = 0;    // the 64 channels and the CD-DA pair of the sample: the direct mix, kept for
	                                   // SampleMix(); the sends to the DSP (AicaDsp::SweepBegin, then one MixsSend
	                                   // for each bus a channel sends to, of the sum of its sends at 20-bit scale);
	                                   // the CD-DA pair into AicaDsp::EXTS
	virtual void SampleMix() = 0;      // EFREG of the DSP through EFSDL / EFPAN into the kept mix; mono, MVOL, the
	                                   // DAC; the harness's sample fold; the audio stream
	virtual void SampleIrq() = 0;      // the interrupt lines of the ARM7 and of the SH4 follow the AICA's state
	virtual void TimeStep() = 0;
```

**`libswirl/hw/aica/dsp.h`, `struct DSP`, outside the debug block** (W1 delivers, with `RunPhase` running
`AicaDsp::RunSample()` only; E8a gives it the debug loop):

```cpp
struct AicaDsp;

struct DSP : MMIODevice {
	// The DSP phase of one sample: the steps from AicaDsp::step to the end of the sample (all 128 behind a
	// SampleSweep). Emulator thread: from the sound burst between AICA::SampleSweep and AICA::SampleMix, or from
	// AICA::TimeStep. checks false, or dbg_dsp_armed (debugger/debugger.h) 0: AicaDsp::RunSample(); returns true.
	// checks true and dbg_dsp_armed not 0: the debug loop, AicaDsp::StepOne() behind dbg_dsp_Check() for each
	// step; returns false when the debugger suspended it in front of a step (the core's step is that step, nothing
	// of it has executed), true when the sample's last step has executed. A phase that was suspended is gone on
	// with by the same call.
	virtual bool RunPhase(bool checks) = 0;

	// The DSP core (hw/aica/dsp_core/aica_dsp_core.h), for the sound generator, the register routing and the sound
	// regression harness of hw/aica. Debugger modules read the DSP through the accessors of the debug block; a
	// selftest may read the core within one call.
	virtual AicaDsp* Core() = 0;

	... (the debug block, frozen) ...

	static DSP* Create(AicaContext* aica_ctx, u8* aica_ram, u32 aram_size);
};
```

`DspBackends`, `Step`, `WritenMem`, `setBackend`, `GetDspContext`, `dsp_context_t` and `libDSP_Step()` go with W1.

**The sound line** (`hw/aica/aica_hash.h`; W1 delivers; `soundlines.py`, G's oracle tool and every later neutrality
target rely on it):

```
AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> dsp=<hex> ticks=<N> cycles=<scheduler time, decimal>
```

`dsp` is the 64-bit FNV-1a (offset basis `0xcbf29ce484222325`, prime `0x100000001b3`), started at the offset basis,
over the bytes `AicaDsp::SaveState` writes (`AicaDsp::StateSize()` of them), folded at every tick of the harness,
where `regs` and `arm7` are folded. The state hash of an exit line gains nothing (M3-D12).

**The guest's labels** (G delivers, F4a's S21 and E8a's selftests rely; section 6.2): `g_dsp`, `g_dsp_coef` /
`g_dsp_coef_end`, `g_dsp_madrs` / `g_dsp_madrs_end`, `g_dsp_mpro` / `g_dsp_mpro_end`, `g_dsp_end`, in
`dsptest.elf`'s symbol table and as `DSPTEST_G_DSP_*` in `dsptest_labels.inc`. The block between `g_dsp_mpro` and
`g_dsp_mpro_end` is the program as the register area holds it: one `.long` for each 16-bit word (the word in the low
half, the high half 0), four for each step, from step 0; the steps behind it are empty.

### 2.5 What the revision is delivered with (package R7)

So that the tree builds and behaves as before, and no later package of the round has to open a file of another:

1. **Stub bodies.**
   - New `libswirl/debugger/dbg_dsp.cpp`: `dbg_dsp_armed` is 0 and stays 0; `dbg_dsp_Check` and `dbg_dsp_SampleEnd`
     return 0; `SetBreakpoints` keeps the set and arms nothing, `GetBreakpoints` gives it back; `SetActiveMask`
     keeps the mask; the others do nothing.
   - New `libswirl/debugger/dbg_sgc.cpp`: `dbg_sgc_active` is 0; `dbg_sgc_Frame` returns 0; `Arm` returns
     `kErrNotSupported`; `Cancel` does nothing; `GetState` gives `None` and 0 frames; `Copy` returns 0;
     `SetCompletionNotice` keeps the pointer.
   - Both entered in the debugger's source list where `dbg_arm7.cpp` is listed.
   - `libswirl/debugger/dbg_emu.cpp`: `SetDspBreakpoints` forwards to `dsp::SetBreakpoints`; `SetActiveMask` also
     forwards to `dsp::SetActiveMask`; `TargetSteps(Target::Dsp)` stays false; `Step` keeps its `kErrNotSupported`
     for the DSP. **`Rank()` gives `StopCause::Capture` the lowest rank of a cause** (below `Pause`; today it has the
     rank of `NoCause`, so `Merge()` would drop it: `debugger.md` 1).
   - `libswirl/hw/aica/dsp.cpp`: `DebugGetState` fills the new fields: `rbpl` from the register file's word at
     0x2804 (the old DSP decodes RBP and RBL from it: the same values the rows read today), the datapath fields 0
     (the old interpreter keeps its datapath in locals, reset every sample).
2. **The wiring of the core, against the stubs** (`libswirl/debugger/dbg_core.cpp`; M3-D4):
   - `PushBreakpoints()` collects the enabled code breakpoints of `Target::Dsp` in a fourth pair of vectors and calls
     `emu::SetDspBreakpoints` beside the three calls it makes;
   - `NoticeText` loses its `NOTICE_DSP_FORCED` branch;
   - the text of M3-D30: `-32004` of `stepOver` / `stepOut` on the DSP reads `not supported: step over and step out
     of target dsp` (not "on this backend"); `-32012` keeps its one text, `arm7 held in reset`, which only the ARM7
     answers (M3-D7).
   - `libswirl/debugger/dbg_selftest_core.cpp`: the expectation "a code breakpoint of target dsp: -32004" of
     `core.tables` and the `stepping` of `dsp` in `core.tick` follow `emu::TargetSteps(Target::Dsp)`, as R6 made the
     ARM7's follow it: while it is true the test asks for `-32602` without an address, for `128` and for `-1`,
     accepted and listed for 0 and 127, and **the hand-over**: with three DSP breakpoints added, one of them toggled
     off, `dbg::dsp::GetBreakpoints` gives the two enabled ones with their ids at their steps, and after their
     removal none; and the cross-target checks E7a added for the ARM7 (no DSP breakpoint reaches the SH4's or the
     ARM7's set, nor the other way). The probe that used `dc.debugger.dsp_forced` probes a name that is in no row of
     the table. `dbg_selftest_arm7.cpp` (the DSP lines of the held-ARM7 test) and `dbg_selftest_emu.cpp` (`emu.step`)
     follow `TargetSteps(Target::Dsp)` the same way: `-32004` for Into, Over and Out while it is false; while it is
     true, Over and Out `-32004`, and Into with the ARM7 held is no error: in `emu.step` it is a step (`STEP` + 1, a
     stop with cause `Step`, no `Running`), and the held-ARM7 test of `dbg_selftest_arm7.cpp` asks Over and Out only
     (E8a's `dsp.step.held` holds the step with the ARM7 held). The true branch first runs in E8a's gate (the stubs
     keep `-32004` for every DSP step), so E8a owns these lines from its turn on (3.0).
   With the stubs nothing changes on the wire.
3. **The rows of 0x2804** (`dbg_regs.cpp`; M3-D6): `RBP` and `RBL` of `dc.aica.dsp`, and `RBP`, `RBL`, `TESTB0` of
   `dc.aica.common`, read their bits from `DspDebugState::rbpl`. How: **the rows keep their keys, and the three of
   `dc.aica.common` stay `AICA_ROW` rows (`kSrcAica`)**: `ReadAica` takes the 16-bit word of 0x2804 from
   `DSP::DebugGetState()`'s `rbpl` in place of `AICA::DebugReadRegs`, and `ReadDsp`'s `RBP` and `RBL` decode `rbpl`.
   No key, selector or row of `regs_test.cpp` changes (its bit-for-bit spec of 0x2804, the field order and the source
   checks hold as they are), and `dbg_selftest_regs.cpp`'s check that every common row is `kSrcAica` holds. Today
   `rbpl` is the file's word: the values do not change. From W1 on it is the DSP's, while a CPU read of 0x2804 gives
   0 (3.3 item 9 says what the regs session then holds).
4. **The forcing names** (H7-7, H7-8): `dbg_events.h`, `dbg_emu.h`, the `NoticeText` branch, `regs_test.cpp`, the
   comment of `dbg_regs_tables.cpp`, the probe of `dbg_selftest_core.cpp`.
5. **The registration lines**: `dbg_dsp.cpp` and `dbg_sgc.cpp` in the source list; nothing in `tests/` needs a new
   directory (the guest's, the selftests' and the contract's directories exist).
6. **The UI pin**: before R7 is cut, the integrator moves the submodule `devtools/` to F4a's verified head in a commit
   of its own (the gitlink is the UI pin since N1e: `devtools.commit_check` holds `NULLDC_DEVTOOLS_DIR` to it), and
   `agents/nulldc3/wt/ui-pin` with it.
7. Nothing of `hw/aica/` beyond item 1, nothing of `libswirl.cpp`, nothing of the guest.
8. **The repository's copy of this document**, in a commit of its own: `docs/nulldc3/devtools-integration-m3-round.md`
   with a "This copy" box as the design's copy has, its row in the name table of
   `docs/nulldc3/devtools-integration-design.md` and its line in `docs/nulldc3/README.md` (a frozen header cites this
   round from R7 on; the repository holds what it cites). M3G replaces it by this file as it then stands.

### 2.6 The clauses that go with a body

| Header | Clause R7 leaves in | Goes with | The text then is |
| --- | --- | --- | --- |
| `debugger.h`, the ARM7 block's introduction | " - in the batch mode, while it exists, made by the batch render instead - " and "the batch render while it exists, " | W2, in the commit that removes the batch render | the text of 2.3 without these two bracketed clauses |
| `debugger.h`, the ARM7 block's introduction | "if the ARM7 is clocked, " | W3, in the commit that clocks the AICA whatever ARMRST says | the text of 2.3 without that clause, and with "the ARM7 for its ticks" become "the ARM7 for its ticks, none while the guest holds it in reset (whether it is held is latched at the start of the burst)": a burst is 32 samples whatever the ARM7 does |
| `dbg_net.h`, the send functions | the comment as it is today ("A client that lets more than 16 MiB pile up is disconnected ...") | E8b, in the commit that counts text and binary apart | the text of 2.3 |

Everything else of the revision is final when R7 is merged.

---

## 3. Packages

| Pkg | Repository | Content in one line | Brief |
| --- | --- | --- | --- |
| **F4a** | `dreamcast-devtools` | the contract of M3: the mock as an M3 server, the runner's M3 rules, `check` on the M3 gate | `m3-briefs/F4a-BRIEF.md` |
| **R7** | `devcast` (integrator) | header revision 7 with its stubs, the DSP wiring of the core, the rows of 0x2804, the forcing names out | `m3-briefs/R7-BRIEF.md` |
| **W1** | `devcast` | the T5.0 core is the emulator's DSP; the old DSPs and their setting go; the sample in phases; the line's `dsp` field | `m3-briefs/W1-BRIEF.md` |
| **F4b** | `dreamcast-devtools` | the playground on the emscripten build of the core; the Rust crate goes | `m3-briefs/F4b-BRIEF.md` |
| **W2** | `devcast` | the batch path and `aica.NoBatch` go; the emulator holds the UI's module equal to native | `m3-briefs/W2-BRIEF.md` |
| **W3** | `devcast` | the AICA clocked whatever ARMRST says: only the ARM phase is skipped while the ARM7 is held (3.12) | `m3-briefs/W3-BRIEF.md` |
| **F4c** | `dreamcast-devtools` | the SGC panel on the v3 contract | `m3-briefs/F4c-BRIEF.md` |
| **G** | `devcast` | the guest's DSP program, its oracle, the new baseline | `m3-briefs/G-BRIEF.md` |
| **E8a** | `devcast` | DSP breakpoints and steps, the DSP phase and the end of a sample in the burst, the datapath rows | `m3-briefs/E8a-BRIEF.md` |
| **E8b** | `devcast` | SGC capture, `state.getMemoryBinary`, the binary path, the deferred response, the transport's bounds | `m3-briefs/E8b-BRIEF.md` |
| **F4d** | `dreamcast-devtools` | "Load from emulator", the DSP's controls whatever the ARM7 does, the smoke spec's M3 part | `m3-briefs/F4d-BRIEF.md` |
| **M3G** | both (integrator) | the M3 gate, the documents, the two review PRs | `m3-briefs/M3G-BRIEF.md` |

Why the design's three rows (the wiring, E8, F4) are cut into nine packages, and a tenth added. **The wiring** has two
gates that must not be mixed: replacing the DSP is held to "every per-sample line identical to the base's per-sample
line", removing the batch path to "every default line equal to the base's per-sample line" (5.1); one package would show
neither cleanly (M3-D2). **The clock** (the owner's answer to M3-Q1) moves lines on purpose, which neither wiring gate
allows, so it is a package of its own behind them, W3 (M3-D41); its subsection is 3.12, behind M3G's, so that the
numbers 3.6 to 3.11 that the round's texts cite stay what they were; section 4 has the order. **E8** has two kinds of
work that meet in two files only, run control on the DSP and the capture with the binary path; each is a full package,
and the capture's tests need the end of a sample, which the run control builds (M3-D19). The guest's program changes the
baseline that E8 is held to, so it is a package in front of E8 (M3-D18), as E6a was in front of E6b. **F4** is circular
as one package: its gate needs the emulator of M3, and the emulator's gates need its runner. The contract comes first
(F4a), the playground behind the final core (F4b), the panel anywhere (F4c), and the UI against the emulator last (F4d).

All packages run one after another (section 4), each based on the merge of the one before it in its repository. "Owns"
names what a package may edit **in its turn**; where two packages name the same file, the later one is based on the
merge of the earlier.

### 3.0 The conflict surface, solved

The files that more than one package of the round edits, and who edits what in which turn. Nobody edits a file in
another's turn.

| File | Turns |
| --- | --- |
| `libswirl/debugger/dbg_core.cpp` | R7 (`PushBreakpoints`' DSP branch, the `NoticeText` branch, the text of `-32004` for the DSP's step over and out) → E8b (the three methods, the binary path, the deferred response, `binaryId`, the capabilities). **W1, W2, W3, G and E8a never open it**: the day E8a's `TargetSteps(Target::Dsp)` answers true, describe says `stepping: true`, the core accepts DSP breakpoints and forwards DSP steps (as E6b did for the ARM7, M3-D4) |
| `libswirl/debugger/dbg_emu.cpp` | R7 (stubs' forwarding, `Rank(Capture)`) → E8a (run control of the DSP, the stop recorded in a run-on that ends the burst) → E8b (the capture's two cancels; that rule of `RunBurstOn` only where E8a's needs a fix for a capture) |
| `libswirl/libswirl.cpp` | W1 (the DSP backend selection and `Dynarec.DspEnabled`) → W2 (`aica.NoBatch`) → W3 (the burst's time steps out of the ARM7's gate: `RunBurst`, its comment) → E8a (the burst in phases, the DSP phase, the end of a sample, the SGC call site; the clock kept) → E8b (only if the capture's call site needs a fix) |
| `libswirl/libswirl.h` | W2 (the batch clause of `RunSoundBurst`) → E8a (the rest of 2.4) |
| `libswirl/hw/aica/**` | R7 (`DebugGetState`'s new fields) → W1 (the core in, the phases, the routing) → W2 (the batch path out) → W3 (the sentence of 2.4 in `struct AICA`'s comment, nothing of what a sample computes) → E8a (`RunPhase`'s debug loop) → E8b (the capturing sweep, `DebugGetChannel`'s outputs). `dsp_core/` is no package's (M3-D15) |
| `libswirl/debugger/debugger.h` | R7 (H7-1, 2.3) → W2 (its two clauses of 2.6) → W3 (its clause of 2.6) |
| `libswirl/debugger/dbg_net.h` / `.cpp` | E8b (the bounds and the comment of 2.6) |
| `dbg_regs_tables.cpp`, `dbg_regs.cpp` | R7 (the rows of 0x2804 through `ReadAica` and `ReadDsp`, the forcing comment) → W1 (the comment of `MDEC_CT`) → E8a (the nine rows, `ReadDsp`) |
| `libswirl/debugger/dbg_regs_private.h` (not frozen, 9) | E8a (the nine selectors of `enum DspReg`, in front of `kDspRegCount`) |
| `dbg_selftest_core.cpp` | R7 (the DSP's expectations follow `TargetSteps`) → E8a (the register counts only) → E8b (the three methods, the capabilities) |
| `dbg_selftest_regs.cpp` | R7 (the rows of 0x2804, where a test reads them by kind) → W1 (the DSP block, `regs.rows.aica`'s 0x2804, `StoredWord`) → W2 (`Arm7Loads`) → E8a (the DSP block's counts and widths) |
| `dbg_selftest_arm7.cpp`, `dbg_selftest_emu.cpp` | R7 (the DSP lines follow `TargetSteps`) → W3 (the expectations of a burst with the ARM7 held: 32 time steps where they said none; the test `arm7.held.clocked`) → E8a (`emu.step`'s DSP half; the DSP lines of the held-ARM7 test, `arm7.step.notclocked`, whose true branch first runs in E8a's gate) |
| `dbg_selftest_writes.cpp`, other selftests | W3, only where one counts the time steps of a held burst or relies on an AICA that stands still while the ARM7 is held, each named in its report |
| `tests/debugger/regs/regs_test.cpp` | R7 (the forcing exception) → E8a (the counts of the tree; the nine rows in the `named` widths list) |
| `tests/debugger/guest/dsptest/soundlines.py`, `soundlines_test.py` | W1 (the `dsp` field; the stand-in's line with `dsp=<hex>` between `sh4` and `ticks`) → W2 (`--render` out) → W3 (the mode `held`; the `sounds` paragraph of the docstring) → G (the mode `oracle`, the manifest, `release`'s free entries) |
| `tests/debugger/guest/dsptest/` other files | W1 (the README's line of "The sound lines") → W2 (`CMakeLists.txt`, the README's render parts) → W3 (the `held` targets in `CMakeLists.txt`; the README's sentences on the AICA's clock while the ARM7 is held, "The first tick" and the row of the mode `sounds` among them, and the mode `held`) → G (everything; the two comments of `dsptest.s` that W3 makes false, its head's and its first release's) |
| `tests/debugger/guest/neutral/CMakeLists.txt` | W2 (the render key out) → E8a (`dspn.*`) → E8b (`sgcn.*`) |
| `tests/debugger/selftest/CMakeLists.txt`, `tests/debugger/contract/CMakeLists.txt`, `contract.py` | W2 (the `arm7.persample` lines) → W3 (a line for `arm7.held.clocked`, if it needs one) → E8a (the session `dsp`, the mode `dsp` with `--require-note`) → E8b (the session `sgc`, the modes `m3`, `m3dsp` with `--require-note`) |
| `tests/aica_dsp/**` | W1 (the emulator's machine, the lockstep and state targets) → W2 (the replay of the UI's module; the lines that set `settings.aica.NoBatch`) → G (the oracle tool and its targets) |
| `tests/debugger/regs/aica/**`, `tests/arm7/arm7_machine.cpp` | W1 (the core linked, the stubs) → W2 (the lines that set `settings.aica.NoBatch`, if W1 has any) |
| `aica-dsp-asm/**` | W1 (ported to the core's encoder, M3-D39) |
| `tests/README.md`, the READMEs of `tests/debugger/*` | every package, its rows and counts, in its turn (R6's rule, `M2-ROUND.md` 2.4 item 4) |
| `docs/nulldc3/**` | R7 (the copy of this file, its row and line) → W1 ("The sound line" of `automated-runs.md`) → W2 (its render paragraphs) → W3 (its sentence on the pairs of a held ARM7) → M3G (every document of 3.11 item 7) |
| `tests/perf/**` | W1 (`perf.py --config`, its README line) → W2 (the note of `BASELINE.md`) |
| UI `README.md` | F4a (mock, runner) → F4b (the DSP module, the Rust requirement) → F4d (the UI against the emulator) |
| UI `package.json` | F4a (`check`) → F4b (`build:dspsim`, `mock:dev`) |
| UI `src/app/panels/DspPlaygroundPanel.tsx` | F4b (the core's interface) → F4d (the call into the snapshot module, the banner) |

The submodule `devtools/` (the UI pin) is moved by the integrator alone, in a commit of its own, to the verified head of
each UI package before the next emulator package is cut (section 4).

### 3.1 F4a — the contract of M3

**Delivers.**

1. **Schema.** `CAPABILITIES.DSP_FORCED` (`"dsp-forced"`) leaves `src/lib/debuggerSchema.ts`, and
   `dc.debugger.dsp_forced` leaves the describe fixture `src/lib/test-fixtures/protocol-v3-frames.json` and the comment
   of `server/mockDebugger.ts`. No other shape changes: the M3 methods, bounds, kinds and codes are in the schema since
   F1.
2. **The mock as an M3 server** (`server/`; `devtools.md` 3):
   - `state.getMemoryBinary` with the capability `memory-binary` (in `METHOD_CAPABILITY` and
     `OPTIONAL_CAPABILITIES`): the bytes and the unreadable runs of `state.getMemorySlice` over the same map, zeros
     there, `totalBytes == length`, `baseAddress` echoed, a frame of kind 2 behind the response to the requester only,
     `binaryId` from the connection's counter that kind 1 shares; JSON-RPC `-32010` when nothing is readable, `-32004`
     for `dsp`, `-32602` outside 1..16777216; no frame after an error.
   - `MOCK_M3_SUPPORT`, `--m3` of `server/mockServer.ts` and `--mock-support m3` of the runner: what the M3 emulator
     does and no more — the three targets stepping, `sgc-capture`, `memory-binary` and the capabilities of M2,
     `flags.writable` on `dc.sh4.cpu` and on `dc.aica.arm7` but `ENABLED` only (M3-Q3: no device write in M3), the rows
     of M3-D25.
   - **The AICA is clocked whatever the ARM7 does** (M3-D7, the owner's answer to M3-Q1): while `ENABLED` reads 0 the
     mock's DSP steps (`control.step {dsp}` is a step), its breakpoints are reached in every sample, `STEP` and `MDEC_CT`
     move with its sound clock, and `state.recordSgcFrames` records and completes; only `control.step` / `stepOver` /
     `stepOut {arm7}` answer `-32012`, as in M2. The mock does so today (its step checks the ARM7 only, its capture checks
     nothing, its emulation step advances the DSP while the ARM7 is held: `devtools.md` 3, items 3 and 4, which asked
     for the opposite and are withdrawn by the answer); a test of the mock holds it.
   - **One DSP program**: its MPRO words are the register file's at `0x00703400` (SH4) and `0x00803400` (ARM7), the
     `bytes` of `state.getDisassembly {dsp}` are those words, and the text is `"MPRO[" + n + "] = "` followed by the
     playground's disassembly of them (`disassembleDesc(decodeInst(words))`, `TRA:0` for an empty step: design 3.3);
     loaded steps from 0 to some k well below 127, step 127 empty.
   - `dc.aica.dsp` has the thirteen registers of M3-D25 in that order with those widths, and the nine of M3 change with
     a step. `dc.aica.common`, of the default mock and of `MOCK_M3_SUPPORT`, gains `RBP` (12), `RBL` (2) and `TESTB0`
     (1), read from the same word of 0x2804 as `dc.aica.dsp`'s `RBP` / `RBL`; a mock option sets `TESTB0` (for F4d's
     load-path tests, which may not touch the mock).
   - **The mock's sound clock is the emulator's** (the mock today advances `SH4_CYCLES` by 3,200,064 cycles, about 22
     AICA periods, per emulation step, completes a capture on a wall-clock timer, steps its DSP once per emulation
     step and counts `MDEC_CT` per emulation step): an AICA period is 145124 cycles of `dc.timing.SH4_CYCLES` and 32
     samples; `MDEC_CT` counts once a sample (one less, `TESTB0` 0); while running, the DSP executes every sample of the
     time an emulation step covers (at least one whole sample of 128 steps per emulation step), so an active DSP
     breakpoint is reached within one emulation step and in every sample; a capture of n frames armed at a stop
     advances `SH4_CYCLES` by exactly `ceil(n / 32)` AICA periods and stops with reason `capture`, armed while running it
     completes after that much of the mock's time; a DSP step from a pause advances `SH4_CYCLES` by at most one AICA
     period, from a DSP stop by nothing.
   - **The anchor holds the ARM7 in the runs of `--dsp-program`**, as the guest's does: the mock the runner starts with
     `--dsp-program` has `arm7Held` and `startPaused` (reason `start`): that stop is the mock's anchor, as S00's stop is
     the emulator's, and the ARM7 is released only after `MOCK_ARM7_RELEASE_STEPS` emulation steps of running, so that
     the held-ARM7 assertions of S17 and S21 (item 3) and their notes are exercised by `check` (item 4) whatever the
     time the session takes to open. `arm7Held` alone does not do it: the runner builds its mock as `new MockDebugger({
     supports })` (`scripts/contract/run.ts`, `prepare`), `listenMockDebugger` starts it at listen
     (`server/mockWebSocket.ts`), and the running mock releases the ARM7 eight emulation steps of 16 ms later; S00 is
     emulator-only, so on the mock S17 or S21 is the first scenario, and whether it meets `ENABLED` 0 would depend on
     how quickly the session opens, describes and pauses (the runner's own tests pair the two options, `m2.test.ts`).
     The mock's DSP program is loaded from its start; the emulator's guest loads its own behind the anchor (6.1), which
     the runner's test with the emulator-like scripted server holds (item 3).
   - **A read of up to 16 MiB is answered within 2 s** (the mock today makes each fabricated byte with a sha256 digest
     of its own: some 16.8 million digests for one read of 16 MiB, beyond the client's 10 s timeouts): fabricated bytes
     come from one digest per 32-byte block, or another function as fast, the same for `getMemorySlice`,
     `getMemoryBinary` and the disassembly bytes. The tests and e2e specs that pin fabricated values follow, each named
     in F4a's report.
   - The capture by M3-D27: a second `state.recordSgcFrames` while one records is `-32003` and the first goes on;
     `state.getSgcFrameData` while one records gives the capture before it (or `-32011`); a pause, a step or a breakpoint
     during a capture keeps it recording, and with the target stopped 5 s after the request it answers `-32014` and
     cancels; `control.reset` during a capture answers it `-32014` at once.
   - A DSP step from a pause steps `STEP` by one with no running tick, from 127 to 0; `stepOver` / `stepOut` `-32004`;
     a step while running `-32003`; a resume from a DSP breakpoint stop stops in front of the same step one sample
     later (the exemption: once).
3. **The runner's M3 rules** (`scripts/contract/`; M3-D32), each with a broken scripted server in the runner's tests that
   goes red when the rule is taken back:
   - **S01** under M3: `targets[dsp].stepping === true`; the nine registers of M3-D25 present on `dc.aica.dsp` with their
     widths. The widths are normative for every server that lists one of them (`DSP_REGISTER_WIDTHS` of `design.ts`).
   - **S10**, every milestone: each line of `dsp` has the text `"MPRO[" + n + "] = " + (disassembleDesc(decodeInst(words))
     || "TRA:0")` of its own `bytes`, with the functions of `src/app/dsp/dspUtils.ts` (the emulator's own test,
     `debugger.disasm.dsp_playground`, holds the same rule).
   - **S17, part `sgcCapture`** (`sgc-capture`): no settling. **A capture with the ARM7 held**: when `ENABLED` of
     `dc.aica.arm7` reads 0 at S17's first pause (under `--dsp-program` the anchor), its first capture is made there,
     from paused, as every capture from paused (the response, a paused tick with reason `capture`, either order, the
     `SH4_CYCLES` rule below, `getSgcFrameData` with the count asked), and once it is complete S17 notes `sgc: captured
     with the arm7 held` (M3-D7; a server that answers `-32012` there fails S17). Then from paused, as today, and **the
     response only when the capture is complete**:
     `getSgcFrameData` asked right behind the response of a capture whose frame count differs from that of the capture
     before has the new count; a capture **while running**: no paused tick, the target still running, then `frameCount`
     as asked; the default (`frames` absent: 1024); `frames: 0` and `4097` `-32602`, `4096` accepted once (the client
     survives the 32 MiB frame); `dc.timing.SH4_CYCLES` from the stop before to the capture's stop grows by at least
     `ceil(frames / 32) - 1` and at most `ceil(frames / 32) + 1` AICA periods (145124 cycles); a second connection reads
     the same capture; **under `--dsp-program`** at least one record of a capture with flags bit 0 set and a level
     above 0 (the sounding guest keys two channels). **A capture held open** (the second request and the read of the
     previous capture are never raced against a capture's end, which on an unthrottled headless emulator is a few
     milliseconds away): at a stop, with a DSP breakpoint on step 0 added and enabled and the category `dsp` active,
     `recordSgcFrames {frames: 4096}` (a count other than the capture before's); the breakpoint's paused tick comes
     within one sample while the capture records (a stop keeps it recording, M3-D27), and no response to the request
     has come; then a second `recordSgcFrames` is `-32003`, and `getSgcFrameData` gives the frame count of the capture
     before (or `-32011` when there was none); then the breakpoint is removed and the target resumed: the response and
     a paused tick with reason `capture` both come, in either order (design 3.3, M3-D27); then `getSgcFrameData` gives
     4096. Broken scripted servers for these: one that answers at the arm, one that replaces the server's buffer at the
     arm. And a scripted server that sends the capture's stop tick in front of the response (the emulator's other legal
     order: (E) publishes the stop, (C) answers when it takes the notice): S17 passes on it.
   - **S17, part `memoryBinary`** (`memory-binary`): from a pause, 64 bytes at the SH4's `defaultMemoryAddress`:
     `baseAddress` echoed, `totalBytes == length`, the frame behind the response, kind 2, its `binaryId`,
     `payloadBytes == totalBytes == length`, the bytes equal to `getMemorySlice` of the same range; across `0x0021FFF0`
     for 32: `unreadable: [[16, 16]]` and zeros there; `0x10000000` for 16: `-32010` and no frame within 500 ms;
     `target: "dsp"` `-32004`; 16777216 bytes of main memory from `0x0C000000` accepted, and the last 64 bytes of that
     read equal `getMemorySlice {sh4, 0x0CFFFFC0, 64}`; 16777217 `-32602`; the ARM7's wave memory at 0 equal to the
     SH4's at `0x00800000` over 2 MiB, and the last 64 bytes of it equal `getMemorySlice`; two requests in a row get
     increasing `binaryId`s, shared with kind 1; a second connection, opened and described before the 64-byte read,
     receives no binary frame within 500 ms of the first connection's response (the frame goes to the requester only,
     M3-D36). Broken scripted servers for these: one that broadcasts the frame, one that fills a large read with zeros
     past its first 64 KiB.
   - **S21** (`dsp.stepping`): **a step with the ARM7 held**: at its first pause (under `--dsp-program` the anchor),
     when `ENABLED` reads 0, `control.step {dsp}` is a step by the rule below, and S21 notes `dsp: stepped with the
     arm7 held` (M3-D7; a server that answers `-32012` there fails S21); **the program wait** (in place of round 2's
     settling, M3-D44): S21 reads `getDisassembly {dsp}` of the 128 steps and, while the program is not loaded — with
     `--elf`, while their `bytes` differ from the words between `g_dsp_mpro` and `g_dsp_mpro_end` of the ELF; without,
     while no step is loaded —, resumes, lets the target run for a second and pauses, at most five times; a step: a
     paused tick with a new `stopId`, reason `step`, no `breakpointId`, no running tick in between, `STEP` + 1 mod 128
     (M3-D20); the 128 steps of `getDisassembly {dsp}` by the text rule of S10, and **with `--elf`** their `bytes` equal
     to the words between `g_dsp_mpro` and `g_dsp_mpro_end` of the ELF (2.4; the steps behind them `00`); at least one
     loaded step; 16 lines at `STEP` equal to the full read; a breakpoint
     on a loaded step behind `STEP` that is not 127 hit with its id, reason `breakpoint`, `STEP` equal to its address;
     at that stop a DSP step leaves the SH4's PC and `SH4_CYCLES` as they were; **from that breakpoint's stop on step b,
     with the breakpoint still enabled and active, `control.runUntil` gives a paused tick with the same `breakpointId`, a
     new `stopId`, `STEP` b, `MDEC_CT` one less mod 2^16 and `SH4_CYCLES` grown by at most one AICA period and the rest of
     a slice (145124 + 448 cycles)** — a running tick in between or not: none when the next sample is in the same burst,
     which the park loop runs on (2.3, `Resume()`); a breakpoint on 127 hit, and the step from it ends at 0 with
     `MDEC_CT` one less; **the first DSP step from a pause grows `SH4_CYCLES` by at most 145124 + 448 cycles**; the nine
     registers of M3-D25 in every paused tick, and **at least one of the nine differs between two paused ticks of the
     DSP steps S21 makes**; a breakpoint disabled is not hit (600 ms of running ticks, as S06), the category `dsp` muted
     likewise, two on one step stop with the lower id, one added while running is hit within 2 s; address 128
     `-32602`; a step while running `-32003`; `stepOver` / `stepOut` `-32004`. Broken scripted servers for the rules in
     bold: no resume exemption (the resume stops at once, with no `MDEC_CT` change), a breakpoint reached once a burst
     (`MDEC_CT` 32 less, not one), a step from 127 that runs one more sample (`MDEC_CT` two less), a first step that
     lets the SH4 run on, rows that never change.
   - `--dsp-program` selects **S00, S17, S21** (M3-D32). On the guest S17 then comes right behind the anchor and meets
     the held ARM7 (note `sgc: captured with the arm7 held`); its capture lets the guest run, which releases its ARM7,
     so S21 meets the held ARM7 only in a run of its own, `--only S00,S21` (note `dsp: stepped with the arm7 held`): the
     emulator's gate has both runs (3.8 item 9, 3.9 item 8, M3-D32).
   - The broken servers 1 to 11 of `devtools.md` 4.3 are each red; number 5 counts as red for the all-zero capture
     under `--dsp-program` (stale data with the right count is not asserted, design 5.5.4 with section 8 row 32);
     **number 6 is inverted by the owner's answer to M3-Q1**: the broken server is the one whose DSP step or whose
     `recordSgcFrames` answers `-32012` while its ARM7 is held (the readers' number 6, a DSP that steps there, is the
     emulator from W3 on); number 12, a DSP that computes anything else than the core, is not the runner's: J1 to J3
     (5.2) and F4d's smoke spec hold what the DSP computes. And the scripted server that behaves as the emulator on the
     guest (the ARM7 held at the anchor, the DSP clocked there, the DSP program loaded only behind it) passes S21, which
     the runner as built fails (its first step finds no program).
4. `npm run check` runs `--milestone M3 --require-note "arm7: bl stepped over and out"` on the default mock and with
   `--mock-support m3` (S16 is in those runs), `--milestone M3 --dsp-program --require-note "sgc: captured with the
   arm7 held"` on the mock, and `--milestone M3 --dsp-program --only S00,S21 --require-note "dsp: stepped with the arm7
   held"` on the mock
   (the note of S16 is not required where S16 is not selected: the runner fails a run in which no selected scenario
   made a required note). The two `--dsp-program` lines run on the mock of item 2 that starts paused with its ARM7
   held, so their notes do not depend on the time the session takes to open.
5. `README.md`: the mock's and the runner's sections at M3.

**Owns.** `src/lib/debuggerSchema.ts`, `src/lib/test-fixtures/protocol-v3-frames.json`, `server/**`,
`scripts/contract/**`, `scripts/contract-test.ts`, the `check` line of `package.json`, their tests; `README.md`, the
mock's and the runner's sections; e2e specs that a change of the mock makes fail, with the behaviour they hold kept, each
named in the report.

**Must not touch.** `src/app/**` (`dspUtils.ts` is imported, not edited), `src/lib/aicaDsp.ts`, `src/wasm/`, `crates/`,
`scripts/build-dspsim.ts`; the emulator repository.

**Depends on.** Nothing of the round: devtools `18435de`; for its gate's last line the emulator at `165775e5`. The
owner's answers to M3-Q1 and M3-Q3 (7.2) shape its mock and its rules: the AICA clocked whatever ARMRST says, no device
write. Its held-ARM7 rules assert the emulator of W3 on, which they first meet in E8a's and E8b's gates (section 4).

**Gate.** `npm run check`; the unit suite three times in a fresh clone; the e2e suite (`--workers=2`); every rule of item
3 red when taken back; the emulator's contract targets (the M1 and M2 gates on three engines, the M2 guest run) unchanged
with the UI at F4a's head, built from a detached worktree of `devcast` at `165775e5`; `debugger.disasm.dsp_playground`
green there.

**Verifier.** The broken servers 1 to 11 of `devtools.md` 4.3 (5 as the all-zero capture under `--dsp-program`, 6 as
inverted in item 3; 12 is not the runner's) and one of its own per new rule; the mock made to answer `-32012` for a DSP
step and for a capture while its ARM7 is held, round 2's rule (the two `--dsp-program` `check` lines red, their held
assertions failing, and a test of the mock red); a scripted server that answers `-32012` for a DSP step with the ARM7
held (S21 red) and one that does so for a capture (S17 red); the program wait taken out of S21 (the emulator-like
scripted server, whose program is loaded behind the anchor, fails it); the text rule with a fabricated disassembler in
place of `dspUtils.ts`; S17 accepting a response before the capture is complete; `binaryId` counted per kind; a frame in
front of its response; the mock's capture back on its wall clock (S17's `SH4_CYCLES` rule red on the mock); S17 made to
require the response in front of the capture's stop tick (the runner's test with the reordered scripted server goes
red); `startPaused` taken off the `--dsp-program` mock with a delay of 1 s injected before the first scenario (the two
`--dsp-program` `check` lines red: their notes are not made), and with `startPaused` on and the same delay, both pass; a
read of 16 MiB from the mock timed (under 2 s); each `check` line run alone.

### 3.2 R7 — header revision 7 (integrator)

**Delivers.** Section 2, all of it: the declarations of 2.1, the removals of 2.2, the comments of 2.3, and what 2.5
lists (stubs, the wiring of the core, the rows of 0x2804, the forcing names, the registration lines, the repository's copy
of this document). It edits no text of the design: section 8 is applied at M3G.

**Owns.** The frozen headers, to the texts of section 2; `dbg_dsp.h`, `dbg_sgc.h` and the stubs `dbg_dsp.cpp`,
`dbg_sgc.cpp`; in `dbg_emu.cpp` the forwarding and `Rank()`; the places of `dbg_core.cpp` that 2.5 item 2 names; the rows
of 0x2804 in `dbg_regs.cpp` (`ReadAica`, `ReadDsp`) and the forcing comment of `dbg_regs_tables.cpp`; `DebugGetState` in
`hw/aica/dsp.cpp`; the expectations of 2.5 item 2 in `dbg_selftest_core.cpp`, `dbg_selftest_arm7.cpp`,
`dbg_selftest_emu.cpp`, and in `dbg_selftest_regs.cpp` the checks of the rows of 0x2804 if one reads them by kind;
`tests/debugger/regs/regs_test.cpp` (the forcing exception); the debugger's source list; the copy of 2.5 item 8; the
READMEs' rows.

**Must not touch.** `libswirl.cpp`, the burst, `hw/aica/` beyond `DebugGetState`, the guest, the UI repository.

**Depends on.** F4a verified, and the pin moved to its head (2.5 item 6).

**Gate.** The five builds compile (the default, `-DNULLDC_DEBUGGER=OFF`, `-DTARGET_NO_REC=ON`, `-DHAS_TA_LLE=OFF`, the
generic host, whose 2ndmix state hash is unchanged); `tests/arm7` and `tests/debugger/regs/aica` build. All ctest targets
green twice, none skipped, 278 stated. The state hashes of design 5.7 and the sound lines of the guest and of 2ndmix in
both `aica.NoBatch` settings unchanged; the M1 and M2 contract gates unchanged; `debugger.describe` of a headless emulator
byte for byte what it was (both from builds that embed no UI bundle). The header texts equal section 2. The copy of 2.5
item 8 equals this file but for its box, and has no home path, host name or user name in it.

**Verifier.** Compares every comment and declaration with section 2, character for character but for wrapping, and looks
for a character outside ASCII. With `TargetSteps(Target::Dsp)` forced true over the stubs: `core.tables` and `core.tick`
follow it, the DSP half of the hand-over runs, `stepOver` / `stepOut` on the DSP answer `-32004` with the new text, nothing
hangs; in that forced build two mutations of `PushBreakpoints` (a disabled DSP breakpoint handed over; ids and steps
shifted by one) turn the hand-over red, and a third (the DSP's set handed to the ARM7) turns the cross-target check red.
`Rank(Capture)` read. The rows of 0x2804 read the same values as before on the guest and on 2ndmix, and keep their keys
and their source `kSrcAica` (2.5 item 3). ThreadSanitizer run of the session `emu`.

### 3.3 W1 — the T5.0 core is the emulator's DSP

**Delivers.**

1. **The baseline B0 first** (5.3), on R7's merge, before anything is changed. With it, from a scratch trace of B0's
   binary (nothing of it committed), whether a workload of B0 (the guest, 2ndmix, hello) writes a non-empty `MPRO`
   step, and whether one reads a register of the DSP: by reading none does (5.1). If one writes a program, its
   `aica.NoBatch=yes` column is justified by J2 and that trace, the integrator decides, and W1 and M3G compare that
   column with B1, not B0.
2. **The DSP device holds the core** (`hw/aica/dsp.cpp`): `AicaDsp core` in place of the global `dsp_context_t`; `Init`
   (`core.Init(aica_ram, aram_size)`), a `Reset(bool)` override (`core.Reset()`; today the DSP's state survives a guest
   reset), `RunPhase` (`core.RunSample(); return true`), `Core()`, `DebugGetState` from the core's fields (H7-5),
   `DebugGetProgramStep` from `core.MPRO`; `serialize` / `unserialize` over the core's blob (`StateSize()` bytes; a blob
   the core refuses resets the core, with one log line: M3-D13).
3. **The sample in phases** (2.4): `AicaDevice::SampleTimers / SampleSweep / SampleMix / SampleIrq`, and `TimeStep()` as
   the five with `RunPhase(false)`; the sound generator's sample split into its sweep and its mix, with the direct mix
   kept in the sound generator between the two; the order inside a sample exactly today's (timers and `SAMPLE_DONE`, the
   channels, CD-DA, the DSP, the mix, the interrupt lines). While the batch path exists the sweep, the DSP phase and the
   mix are made under `aica.NoBatch` only, as today. The burst in `libswirl.cpp` keeps calling `libAICA_TimeStep()`.
4. **The sends** (M3-D8): `core.SweepBegin()` in place of the clear of the old DSP's MIXS, in front of the channels; each
   channel's send `oDsp << 4` (the console's 20-bit scale) summed per bus into a 32-bit sum, with the mask of the buses
   that a channel's ISEL names (every channel sends, a disabled one 0, as today); then `core.MixsSend(bus, sum)` for each
   bus of the mask. The channel loses its `DSPOut` pointer (the bus is its ISEL when it sends). The CD-DA pair goes into
   `core.EXTS[0..1]` (0 with `CDDAMute`, as today) and no longer into the register file; the mix takes `core.EFREG[i]`.
5. **The register routing** (M3-D5): every write of a CPU (SH4, ARM7, a G2 DMA into the register area) stores its bytes in
   the register file as today; when the slot is one the core owns (`AicaDsp::RegOwned(off & ~3)`) and the write is in the
   low half of the slot (`(off & 2) == 0`), the core then gets `core.RegWrite(off & ~3, <the file's 16-bit word of that
   slot>)`; the write of 0x2804 / 0x2805 through the sound generator likewise, in place of its decode of RBP and RBL. A
   size-4 write is a size-2 write, as today. `WritenMem` goes. A read of a CPU at an owned slot gets `core.RegRead` (its
   byte for a byte read; 0 for the upper half of a slot); every other offset reads the file as today.
   `AicaDevice::DebugReadRegs` gives the same values for the owned slots (`RegRead` has no side effect).
6. **What goes**: `dsp_interp.cpp`, `dsp_arm64.cpp`, `dsp_helpers.cpp`, `dsp_backend.h`; `DspBackends`, `Step`,
   `WritenMem`, `setBackend`, `GetDspContext`, `libDSP_Step`, `dsp_context_t`; the setting `Dynarec.DspEnabled`
   (`settings.dynarec.DspEnable`: its default, load and save) and the DSP backend selection of `dc_run`; the imgui "DSP
   Mode" control; `FEAT_DSPREC` (`reicast/cmake/config.cmake`, `libswirl/build.h` — the AREC half of its `#error` stays
   —, `tests/common/libswirl.cmake`'s `DSPREC`), the include of `serialize.cpp`; `dsp_core/wasm/` leaves the emulator's
   source glob (it is compiled into the emulator today, unused). A configuration file of an older build keeps the key;
   nothing reads it. **`aica-dsp-asm/`**, the standalone assembler at the root of the repository, includes
   `dsp_backend.h` and its three build scripts compile `dsp_helpers.cpp`: it is ported to the core's `AicaDspDecode` /
   `AicaDspEncode` (`aica_dsp_core.h`) and its scripts compile `dsp_core/aica_dsp_core.cpp`, its text and its options
   unchanged (M3-D39).
7. **Save state V5** (`serialize.cpp`): the DSP's part is the core's blob; a channel's `DSPOut` offset is written as its
   ISEL, which is the same number. `dc_unserialize` refuses V4 as it refuses every other version.
8. **The line's `dsp` field** (2.4; M3-D12): `hw/aica/aica_hash.*`; `soundlines.py` (`LINE_FIELDS`, and `dsp` in what
   `engines` compares for the guest); `soundlines_test.py`, whose stand-in emulator prints the line (`dsp=<hex>` between
   `sh4` and `ticks`: `sound_line()` takes a line only when its fields are exactly `LINE_FIELDS`, so every mode of
   `debugger.sound.runner` goes red without it); the line the guest's README shows ("The sound lines");
   `docs/nulldc3/automated-runs.md`, "The sound line".
9. **The tests and debugger files the change reaches** (`wiring.md` 3.5): the stubs of `tests/arm7/arm7_machine.cpp`;
   `tests/debugger/regs/aica/aica_test.cpp` and its CMake file (the core linked, the render per sample); in
   `dbg_selftest_regs.cpp` the DSP block (`MDEC_CT` 16 bits and free running, against `DspDebugState` and the core; the
   test of the 32-bit counter shown modulo 2^16 goes; the program against its registers as now) and the 0x2804 checks of
   the AICA rows: **`StoredWord(0x2804)` is false** (its word is the DSP's: a made-up state restored into the AICA
   device writes the file, not the core); `RBP`, `RBL` and `TESTB0` of `dc.aica.common` and `RBP`, `RBL` of
   `dc.aica.dsp` are held against the core's `RBPL` (`DSP::Core()`, read within the call) and left out of the
   comparisons with the made-up file and with the accessor; and, because the regs session runs on 2ndmix, where 0x2804
   is 0 in the file and in the core, **the DSP block writes 0x2804 and a `COEF` word with bits 2:0 set through
   `AICA::WriteReg`**, checks that a CPU read and `DebugReadRegs` give 0 for 0x2804 and the word with bits 2:0 clear for
   the `COEF` slot while the rows give the written `RBPL`, and puts both devices' states back (their `serialize` /
   `unserialize`). The comment of `MDEC_CT` in `dbg_regs_tables.cpp`.
10. **The justification of what the DSP now computes** (5.2, J2): `aica.dsp.case.<case>.emu` for the seven cases anchored on
    the console's files, `aica.dsp.emu.lockstep` (its sends worked out by the test, 5.2) and **`aica.dsp.emu.state`** on
    the same machine, with three checks: (1) a program whose `EWT` writes a known value, with `EFSDL` 15: the pair the
    sample hands to the stream stand-in differs from the pair with `EFSDL` 0 by the `VOLPAN` of the core's `EFREG` (the
    mix takes `EFREG` from the core); (2) the DSP device's `Reset(false)` behind a program that ran leaves the core's
    `SaveState` bytes those of a core after `Reset()` (`MDEC_CT` 1, `TEMP` 0, `STEP` 0 among them); (3) the DSP
    device's `serialize`, then a CPU write of a DSP register and a sample, then `unserialize`, gives the core's
    `SaveState` bytes of before. Each red under the mutations of 3.3's verifier paragraph that it names.
11. **`tests/perf/perf.py --config <key list>`**, passed to every run of base and tip (so that a base runs with
    `config:aica.NoBatch=yes`), with its README line.
12. In the report: the old DSP against the core on a program the old one can run (a scratch copy of the guest with a
    program without NOFL in a memory step and with every IRA below 0x38, base and tip with `aica.NoBatch=yes`): host
    instructions a sample, for the record; and the changes a user meets in per-sample mode (a DSP program does what the
    console does; a program with NOFL or IRA 0x38 and up no longer ends the emulator; TEMP, MEMS, MIXS and EFREG read live,
    0x2804 and EXTS read 0; a machine reset resets the DSP; the output of an arm64 build equals an x86-64 build's).

**Owns.** `libswirl/hw/aica/**` but `dsp_core/` (M3-D15); in `libswirl/libswirl.cpp` the DSP backend
selection of `dc_run` and the lines of `Dynarec.DspEnabled`; `libswirl/types.h` (`settings.dynarec.DspEnable`);
`libswirl/serialize.cpp`; `libswirl/gui/gui_settings_advanced.cpp` (the "DSP Mode" control); `libswirl/build.h`,
`reicast/cmake/config.cmake`, the source glob of `CMakeLists.txt`, `tests/common/libswirl.cmake`;
`tests/arm7/arm7_machine.cpp`; `tests/debugger/regs/aica/**`; `tests/aica_dsp/**`; in `dbg_selftest_regs.cpp` the DSP
block, `StoredWord` and the 0x2804 checks of the AICA rows; the comment of `MDEC_CT` in `dbg_regs_tables.cpp`;
`tests/debugger/guest/dsptest/soundlines.py` and `soundlines_test.py` (the field, the stand-in's line), the line of "The
sound lines" in that directory's README; `aica-dsp-asm/**`; `tests/perf/perf.py` and its README;
`docs/nulldc3/automated-runs.md` (the line); its rows and counts in the READMEs.

**Must not touch.** The frozen headers; `dbg_core.cpp`, `dbg_emu.cpp`, `dbg_regs.cpp`, `dbg_dsp.cpp`, `dbg_sgc.cpp`; the
burst (`AicaUpdate`, `RunBurst`); the batch path and `aica.NoBatch` (W2's); the guest's committed files; the UI repository.

**Depends on.** R7 merged.

**Gate.**

- Builds: the five; `tests/arm7` and `tests/debugger/regs/aica`. arm64 is read, not built: `dsp_arm64.cpp` and
  `FEAT_DSPREC` are gone from every build file and every `#if`, and nothing else of the arm64 recompilers changed.
- All ctest targets green twice, none skipped, the number stated; the core's suite (`aica.dsp.*`) unchanged and green.
- **Lines and hashes against B0** (5.1): every line of the guest (N = 2000) and of 2ndmix (N = 40000), JIT and interpreter,
  default build and `-DNULLDC_DEBUGGER=OFF`, **in both `aica.NoBatch` settings, identical to B0's in the six fields B0
  has**; the `dsp` field equal in two runs of one binary, between the two builds, and on the guest between the two engines;
  the exit state hashes of the guest at 90 vblanks and of design 5.7 (2ndmix 600 and 4000 vblanks, hello 300; M3-D38)
  identical to B0's, part for part, in both settings and both engines, and 2ndmix 600 in the five builds. A moved field
  is a defect unless W1 shows it is a guest's read of a register of the DSP that the core answers as the console does
  (the read's address, PC and both values, from a scratch trace), or it is the `aica.NoBatch=yes` column of a workload
  that item 1 found writing a program; the integrator decides.
- The save state: the selftests that save and load (`emu`) and that compare the machine before and after debugger
  operations (`MachineState` of `regs` and `writes`) green, and `aica.dsp.emu.state`.
- The M1 and M2 contract gates unchanged on three engines, and the M2 guest run.
- **Host cost and speed, measured and reported; they gate nothing** (5.4, M3-D15, the owner's answer to M3-Q2): on
  runs the harness does not fold (the exit runs of design 5.7 that B0 has: the guest for 90 vblanks, 2ndmix for 4000;
  the harness's new `dsp` fold costs some 500 host instructions a sample of its own and is not what a user runs), in
  both settings, user-mode instructions of a whole run, tip minus B0's binary, divided by the samples of the run (32 for
  each AICA period of its scheduler time, the `sched` part of its exit line / 145124), on 2ndmix and on the guest, JIT
  and interpreter, medians of three runs; the fold's own cost (harness runs, tip minus B0, less the figure above);
  speed by `tests/perf` (the `none` renderer, the `-DHAS_TA_LLE=OFF` build, five rounds of base and tip in turn) in
  both settings.
- `aica-dsp-asm` builds with its `build_linux.sh`, and a program assembled and disassembled by B0's build of it and by
  the tip's gives the same words and the same text.

**Verifier.** Takes its own B0 on a base it builds. Mutations, each of which a named target has to catch:
`SweepBegin` left out (the sends read a sample early: `aica.dsp.emu.lockstep`); the send without `<< 4` (the lockstep
target: its sends are the test's, not the emulator's); a CPU write that reaches the file and not the core (the case
targets); a read of an owned slot from the file (the case targets; the regs session's `COEF` word); a write to slot + 2
forwarded (the lockstep target's writes at slot + 2); the CD-DA pair left in the file and not in `EXTS` (the lockstep
target's CD-DA stand-in); the mix taking EFREG from the file (`aica.dsp.emu.state` check 1); the core not reset at a
machine reset (check 2); the save state without the core's blob (check 3); `DebugReadRegs` without the core's values
(the regs session: 0x2804 and the `COEF` word); `rbpl` from the file (the regs session: the rows against the core's
`RBPL` with a made-up file). Reads the deletions on arm64 and the build files; runs a scratch guest with a reverb program
in per-sample mode and sees EFREG move and `samples` differ from the same guest with EFSDL 0; recounts the host cost on
the runs of the gate (a report, not a gate).

### 3.4 F4b — the playground on the one DSP core (UI)

**Delivers** (`devtools.md` 2; M3-D34).

1. `src/wasm/aica-dsp-core/`: `aica_dsp_core.wasm` and `aica_dsp_core.mjs` as `build.sh` writes them from the emulator
   tree at W1's merge; `aica_dsp_core.d.mts`, the loader's declarations, written by hand; `PROVENANCE.json`, read by
   W2's `aica.dsp.wasm.devtools` in the other repository and therefore fixed here: `{"repository": "skmp/nulldc3",
   "treeCommit": <git rev-parse HEAD of the emulator tree>, "dspCoreCommit": <git log -1 --format=%H --
   libswirl/hw/aica/dsp_core>, "dirty": <whether git status --porcelain -- libswirl/hw/aica/dsp_core is not empty>,
   "emscripten": <emcc -dumpversion of the emcc that build.sh uses>, "apiVersion": <AICA_DSP_API_VERSION>, "files":
   {<name>: {"bytes": <n>, "sha256": <hex>}}}`, no path; `NOTICE.md` (where the module comes from, and caique's MIT notice
   as the core's README has it).
2. `scripts/build-dspsim.ts` rewritten: `npm run build:dspsim -- --emulator <tree>` (or `NULLDC_DIR`) runs
   `<tree>/libswirl/hw/aica/dsp_core/wasm/build.sh --check <tmp>`, refuses an `emcc` of another version than
   `PROVENANCE.json` names unless asked to with `--emcc-version <v>` (which records the new one), copies the two files,
   writes the provenance and the native fixture of item 5; no path of the machine lands in a file. The `emcc` it asks
   for its version is the one `build.sh` uses, found the way `build.sh` finds it (the `PATH`, else `$EMSDK`, else
   `~/emsdk`, under `upstream/emscripten`); versions are compared as strings.
3. `src/lib/aicaDsp.ts` over the loader (the singleton may stay; the interface is the core's: steps in order, `run` with
   stops, `runSample`, the 20-word view, `get` / `set` with `REG.*` (`MIXS_DSP`, `MIXS_FILL` and `RBPL` among them),
   `peekReg` / `pokeReg`, the typed views `ram`, `temp`, `mems`, `mixs`, `efreg`, `reset`; one core with 2 MiB of wave
   RAM, `module.create(2 << 20)`: F4d's load path uses all of these and may not edit this file). **`aicaDsp.ts`
   type-checks in `tsconfig.server.json` (which includes `src/lib/**`) and `tsconfig.scripts.json` (the runner imports
   `dspUtils.ts`, which type-imports it) as well as in the app's**: it locates the module with
   `new URL(..., import.meta.url)` or through the loader, never with a Vite suffix import (`?url`, `?init`), and uses
   no `vite/client` types; `DspPlaygroundPanel.tsx` at its call
   sites (`devtools.md` 2.4): the inputs at the console's scale (`sample << 4`, `setInputs` once a sample), one `run`
   per sample with the breakpoints as its stops, EFREG as signed 16 bits, the register view from the view and the fields
   (TEMP, MEMS, MIXS whole), `reset`; `dspUtils.ts`'s `writeRegisters` writes `MEMS_L` lines with `pokeReg` — the file's
   path and its four exports stay, and it gets no runtime import that `tsx` cannot load (the emulator's
   `debugger.disasm.dsp_playground` imports it).
4. What goes: `crates/aica-dsp/`, `src/wasm/aica-dsp/`, wasm-pack; the Rust requirement and the section "Generated DSP
   module" of `README.md` (rewritten for the new module), the `.gitignore` and ESLint lines of the Rust build;
   `mock:dev` becomes `mock:serve` without `build:dspsim`. The share link stays, and the playground's help says that it
   opens the original online playground, whose DSP is not this one.
5. Tests: a vitest that loads the committed module from its bytes and runs the known-answer programs of the native core
   against a committed fixture that `build:dspsim` writes with the native core of the same tree (a small C++ driver under
   `scripts/`, compiled at regeneration time only); a test that `PROVENANCE.json` names the bytes beside it;
   `e2e/dsp-playground.spec.ts` at the new scale; the comment of `e2e/offline.spec.ts` on `instantiateStreaming`.

**Owns.** `src/wasm/**`, `crates/**`, `scripts/build-dspsim.ts` and the driver under `scripts/`, `src/lib/aicaDsp.ts` and
its tests, `src/app/panels/DspPlaygroundPanel.tsx`, `writeRegisters` of `src/app/dsp/dspUtils.ts`, the `build:dspsim` and
`mock:dev` lines of `package.json`, the DSP module's parts of `README.md`, `.gitignore`, `eslint.config.js`,
`e2e/dsp-playground.spec.ts`, the comment of `e2e/offline.spec.ts`, the playground's help text.

**Must not touch.** The schema, `server/`, `scripts/contract/`; the SGC panel; the toolbar; the emulator repository (the
module is built from a detached worktree of it at W1's merge).

**Depends on.** W1 merged (the core is final for the round); F4a.

**Gate.** `npm run check`; the unit suite three times in a fresh clone; the e2e suite; `build:dspsim` from the emulator at
W1's merge gives the committed bytes; the vitest of item 5; `debugger.disasm.dsp_playground` green with the UI at F4b's
head (an emulator built from a detached worktree); no path, host name or user name in a committed file.

**Verifier.** Regenerates the module itself; an `emcc` of another version is refused, also one that only `$EMSDK` names;
one byte of the module changed turns the vitest and the provenance test red; the inputs written without `<< 4` turn the e2e
level test red; `MEMS_L` written with `writeReg` turns a test red; a runtime wasm import in `dspUtils.ts` turns the
emulator's `dsp_playground` target red; a `?url` import in `aicaDsp.ts` turns `npm run check` red; `PROVENANCE.json` read
against the keys of item 1.

### 3.5 W2 — the batch path goes

**Delivers.**

1. **The baseline B1 first** (5.3), on W1's merge.
2. `SGC::AICA_Sample32`, `mxlr`, `samples_gen` and the stray declaration go; `AicaDevice::Update` keeps the tick of the
   harness; the sweep, the DSP phase and the mix are made unconditionally; `settings.aica.NoBatch` (its default, load and
   save) and the imgui "Enable DSP" control go, and so do the dead `settings.aica.CDDAMute` and `settings.aica.DSPEnabled`.
3. **"Disable Sound" mutes** (M3-D16): the sample is made, zero pairs go to the audio stream (so Limit FPS keeps its
   pace), and the harness folds what goes to the stream.
4. **No pacing of its own for a burst with the ARM7 held** (M3-D43): W3, the next emulator package, makes such a burst's
   32 samples, which go to the audio stream as every burst's do; W2 adds nothing to `AicaUpdate`. Between W2's merge and
   W3's a stretch with the ARM7 held is silent and not paced, as the per-sample path is today; nothing is pushed in
   between (3.11 item 8).
5. **Save state V6**: `mxlr` and `samples_gen` leave the sound generator's part.
6. **`Arm7Loads`** (`dbg_selftest_regs.cpp`) without `aica.NoBatch`: it runs the ARM7 through `SoundCPU::RunSample` with
   checks false (the loop ends within one sample's ticks, so no time step and no sample is made), or keeps the DSP device
   and the stream out of it otherwise; its tests' expectations unchanged. `SoundCPU::Update` goes if nothing calls it any
   more.
7. **The runner and its targets** (M3-D17): `--render`, `RENDERS` and the key `aica.NoBatch` leave `soundlines.py` and
   its test; the fifteen `.batch` targets and the two `arm7.persample` registrations go; the other sound targets lose the
   `.persample` of their names (`debugger.sound.lines.dsptest.<jit|interp>`, `debugger.sound.neutral.<name>.<jit|interp>`,
   ...), and `nulldc_sound_neutral` registers `<name>.<jit|interp>`; the READMEs, `docs/nulldc3/automated-runs.md`,
   `tests/README.md`, the guest's README (its render paragraphs) and the note of `tests/perf/BASELINE.md` on "ARM7 and DSP
   interpreters".
8. **The emulator holds the UI's module** (M3-D34): `aica.dsp.wasm.devtools` replays the native trace of
   `tests/aica_dsp/wasm_test.mjs` on `<NULLDC_DEVTOOLS_DIR>/src/wasm/aica-dsp-core/` (node and the UI checkout, no emcc),
   and, where an `emcc` of the version `PROVENANCE.json` names is found (found the way `build.sh` finds it, its
   `emcc -dumpversion` compared with `emscripten` as a string: 3.4 item 1), compares the committed bytes with a fresh
   `build.sh` of the tree. Without the UI checkout it is a test whose data is missing.
9. Its two clauses of 2.6 in `debugger.h` (W3's third stays); the batch sentences of `RunSoundBurst` (`libswirl.h`) and
   of `struct AICA` (`aica_mmio.h`, outside the debug block); the comments of the selftests that speak of the batch
   path.
10. In the report: what a user meets (the DSP on by default — reverb in games; a channel sent only to the DSP no longer
    played dry; "Disable Sound" mutes; the two settings gone; until W3, a stretch with the ARM7 held silent and
    unpaced).

**Owns.** `libswirl/hw/aica/**` (not `dsp_core/`); in `libswirl/libswirl.cpp` the lines of `aica.NoBatch`;
`libswirl/types.h` (the three settings); `libswirl/gui/gui_settings_audio.cpp`;
`libswirl/serialize.cpp`; `libswirl/hw/arm7/SoundCPU.*` (`Update`, if it goes); the batch clause of `libswirl.h`; its two
clauses of 2.6 in `debugger.h`; `Arm7Loads` in `dbg_selftest_regs.cpp`; `soundlines.py`, `soundlines_test.py`, the
`CMakeLists.txt` and the render paragraphs of the README of `tests/debugger/guest/dsptest/`;
`tests/debugger/guest/neutral/CMakeLists.txt`; the `arm7.persample` lines of `tests/debugger/selftest/CMakeLists.txt`;
the replay target in `tests/aica_dsp/`; the lines that set `settings.aica.NoBatch` in `tests/aica_dsp/` (W1's `.emu`
machine and its targets), `tests/debugger/regs/aica/` and `tests/arm7/arm7_machine.cpp` (they no longer compile once the
setting goes); the READMEs, `docs/nulldc3/automated-runs.md`, the note of `tests/perf/BASELINE.md`.

**Must not touch.** The frozen headers but its two clauses; `dbg_core.cpp`, `dbg_emu.cpp`, `dbg_dsp.cpp`, `dbg_sgc.cpp`;
the burst (`AicaUpdate`, `RunBurst` as W1 left them: W3's); `dsp_core/`; the guest's committed files; the UI
repository.

**Depends on.** W1 merged, B1 taken; F4b verified and the pin moved (item 8).

**Gate.**

- The five builds; all ctest targets green twice, none skipped, the number stated (W1's count, less the seventeen, plus
  what W2 adds).
- **Against B1** (5.1): every line of the tip — guest and 2ndmix, both engines, both builds — equal to B1's
  `aica.NoBatch=yes` line in all seven fields; every state hash of the tip equal to B1's `aica.NoBatch=yes` hash, part for
  part, in the five builds. The neutrality targets, under their new names, green.
- The M1 and M2 contract gates unchanged; the M2 guest run.
- **Host cost and speed, measured and reported; they gate nothing** (5.4, M3-D15): tip against B1's binary with
  `aica.NoBatch=yes`, host instructions a sample (2ndmix, the guest, both engines; harness runs: B1 folds `dsp` as the
  tip does); `tests/perf` on 2ndmix, ten rounds in turn, both engines, against B1 in both settings, and ten rounds of
  the tip against B0's binary in its default setting (the round's start), each as a ratio of medians of the same
  session.

**Verifier.** Mutations: the harness's tick lost with the tail; the batch render still reachable; a key `aica.NoBatch`
that still changes something; "Disable Sound" folding the computed pairs; `Arm7Loads` that makes a sample; an old target
name left; the replay pointed at the emulator's own fresh build instead of the UI's module.

### 3.6 F4c — the SGC panel on the v3 contract (UI)

**Delivers** (`devtools.md` 1.1). `AudioPanel.tsx`: the frame count from `fetchSgcFrameData`'s result, one scan routine
for the first fetch and for a record, `playbackPosition` and the hover clamped when a shorter capture arrives, an empty
state for `frameCount == 0`, "active" from bit 0 of byte 109, `-32011` taken as "no capture yet" and not logged as an
error (`KNOWN.sgcNoData` and its use go); `sgcChannelData.ts`: the accessors of byte 109 (`flags`, `enabled`,
`aegState`), and the AEG state shown in the channel header; the constants of `binaryFrame.ts` in place of literals;
`SgcWaveformCanvas.tsx`, `SgcWaveformRenderer.ts`, `SgcChannelView.tsx`: no literal frame count, every index clamped; **a
view on the buffer in place of a copy per frame and channel** (a capture of 4096 frames with no active channel no longer
copies 2 GiB). Tests: scripted servers with captures of 64, 1 and 0 frames, a shorter behind a longer, 4096 frames
scanned within a stated bound, and a record with a level but bit 0 clear, which is not active.

**Owns.** `src/app/panels/AudioPanel.tsx`, `src/lib/sgcChannelData.ts` and its tests, `src/app/components/Sgc*.tsx`,
`SgcWaveformRenderer.ts`, `e2e/audio.spec.ts`, `KNOWN.sgcNoData` of `e2e/support/fixtures.ts`, and the SGC button's name
in `e2e/offline.spec.ts` if its tooltip changes.

**Must not touch.** The client (`debuggerClient.ts`, done since F1), the schema, the mock, the runner, the playground.

**Depends on.** F4a. **May move forward** to any slot behind F4a, as a whole package: verified, and the pin moved, before
the next emulator package is cut.

**Gate.** `npm run check`; the unit suite three times in a fresh clone; the e2e suite; the specs of the tests above.

**Verifier.** Mutants: `aeg_value` as "active"; a literal 1024 left in one place; a copy per frame (the bound of the 4096
test red); `frameCount` 0 crashing; `-32011` logged.

### 3.7 G — the guest's DSP program

**Delivers** (section 6).

1. The program's text `tests/debugger/guest/dsptest/dsptest_dsp.txt` in the playground's syntax, and the step of `gen.sh`
   that makes `dsptest_dsp.inc` of it; the block `g_dsp` with the labels of 2.4 and the copy loop of `_start`; the short
   entries of 6.2 at the front of `g_table_dsp`, at least three of its entries left empty at its end (`soundlines.py`'s
   `release` writes there); `SHA256SUMS` and `MANIFEST` with the two new files; the README: the DSP part and what tests
   may lean on (6.3). `_start`'s release stays its last three instructions, in front of `g_main` (`mov.l .L_armrst,
   r1`; `mov #0, r0`; `mov.b r0, @r1`): W3's mode `held` patches that `mov #0, r0` (3.12 item 4). The two comments of
   `dsptest.s` that W3 makes false are corrected: the file head's "..., which gives the AICA its clock (no timer steps
   and no sample is made while the ARM7 is held), and waits until SAMPLE_DONE says that a tick has run", and `_start`'s
   "the AICA has a clock from here on"; the code stays (the first tick runs the ARM7 on its one-word program, on which
   `arm7` and E6a's FIQ count depend).
2. The program's properties of 6.3, each shown in the report with the emulator's disassembly of the step.
3. **The oracle** (5.2, J3): the tool `aica_dsp_guest_oracle` in `tests/aica_dsp/` (the core and the caique model's DSP
   in lockstep, `DspLockstep`, each sample behind the emulator's sweep: its sends of 0 to the buses the channels' ISEL
   name, 6.5), the mode `oracle` of `soundlines.py`, and the targets
   `debugger.sound.oracle.dsptest.<jit|interp>`, registered in `tests/aica_dsp/CMakeLists.txt` (they need
   `NULLDC_CAIQUE_DIR` and the emulator).
4. **The text check** `debugger.disasm.dsptest` (`tests/debugger/disasm/`): the emulator's disassembler on the words
   between `g_dsp_mpro` and `g_dsp_mpro_end` of the ELF gives the `MPRO` lines of `dsptest_dsp.txt`, line for line (the
   steps behind the block `MPRO[n] = TRA:0`); with the UI checkout, the playground's assembler gives those words for those
   lines.
5. A soak (50 runs of each engine, one line each; half of them run four at a time, four emulator processes in all and
   no other load) and one memcheck run on the interpreter with a short N, and what they find reported (G may not fix
   the emulator).

**Owns.** `tests/debugger/guest/dsptest/**` (the sources, `gen.sh`, the committed ELF, labels and manifest, the README,
`soundlines.py` and its test, `CMakeLists.txt`); the tool and its lines in `tests/aica_dsp/`; the text check and its line
in `tests/debugger/disasm/`; their rows in the READMEs.

**Must not touch.** `libswirl/`; `arm7.s` and `arm7.bin`, byte for byte; the other modes of the runner (W3's `held` among
them) but for `release`'s free-entry check and the manifest; the UI repository.

**Depends on.** W3 merged, B3 taken (the guest without the program, on W3's merge: G's "before").

**Gate.** G1 to G5 of 5.1 (2ndmix's lines and the state hashes of design 5.7 against B3's); `gen.sh --check` reproduces
the committed files; the soak and memcheck run; all ctest targets
green twice, none skipped, the number stated; the M2 contract runs on the guest (`debugger.contract.m2*.guest`) and the
sessions and neutrality targets on the guest green; W3's `held` targets green on the guest with its program (the DSP
runs the program while the ARM7 is held, as while it is clocked); E6a's FIQ count recounted (equal: `arm7` is
identical).

**Verifier.** Rebuilds the guest from source with its own toolchain call and compares bytes; one field of one line of the
text changed turns `--check` and `debugger.disasm.dsptest` red; the tool's schedule moved by one sample turns the oracle
red; the tool without its sends of 0 turns the oracle red (the core's blob holds the buses a sweep sent to); the oracle
on the guest without the IMXL-0 change turns red (it sees the sends); a scratch change of the emulator's
DSP (one rule of the core's float format flipped in a scratch build) turns the oracle red, so it witnesses the emulator and
not only the tool; the EFSDL-0 copy's `samples` equal B3's; each property of 6.3 checked on the emulator's disassembly.

### 3.8 E8a — DSP breakpoints and steps

**Delivers.**

1. **The baseline B4 first** (5.3), on G's merge.
2. **The burst in phases** (`libswirl.cpp`): `32 x { [ARM phase, while arm_on]; SampleTimers; SampleSweep;
   [dbg_sgc_Frame]; DSP::RunPhase; SampleMix; SampleIrq; [the end of a sample] }` and the tail, the state `{phase, i,
   ticks_done, arm_on, counts, dsp counts}` outside every save state and hash; W3's clock kept (every sample of every
   burst made, only the ARM phase skipped while the ARM7 is held, M3-D7); the suspension at `SOUND_BURST_DSP` (in front
   of a DSP step) and at `SOUND_BURST_SAMPLE` (the end of a sample that is not the last, for `dbg_dsp_SampleEnd` and for
   a capture), and the run-on from each, from `ARM7` as today, also in a burst whose ARM7 is held (its run-on has no ARM
   phase); `dbg_arm7_OnBurstUnclocked` as today, and nothing of the DSP's at a held burst; the call site of
   `dbg_sgc_Frame(checks)` behind the sweep while `dbg_sgc_active` is not 0,
   and its stop at the end of the sample (the stub makes it unreachable until E8b); `SoundBurstSuspended`,
   `RunSoundBurst`, `SoundBurstDspCounts` (2.4); the discard in `Reset()` as today.
3. **`DSP::RunPhase`'s debug loop** (`hw/aica/dsp.cpp`; 2.4): `for (;;) { if (dbg_dsp_armed && dbg_dsp_Check(core.step))
   return false; core.StepOne(); if (core.step == 0) return true; }` behind the flag test; with checks false or the flag 0,
   `RunSample()` from any step.
4. **`dbg_dsp.cpp`**: everything H7-1's DSP block and H7-3 declare; in a private header `dbg_dsp_private.h` a count of
   the checks and a test hook that arms the debug loop with nothing listed (for the target `dspn.loop`).
5. **`dbg_emu.cpp`**: `TargetSteps(Target::Dsp)` true; `emu::Step` for the DSP (`kErrNotSupported` for Over and Out;
   Into armed with no check of the clock, M3-D7; a fourth action, `ACT_DSP_STEP`); the park loop by 2.3 (`Park()` items
   2 and 3): the step with a burst suspended in the park context, the third way out with none (as the ARM7's),
   `dsp::PrepareResume` at every leave of H7-3's list, `arm7::PrepareResume` in front of a DSP step, `dsp::OnStop`
   wherever `arm7::OnStop` is called, `dsp::OnStateReplaced` in `OnGuestStateReplaced`; `NoteArm7Fallback` only for a
   `SOUND_BURST_ARM7` suspension (M3-D24). **A run-on that ends the burst decides the stop that was recorded in it**:
   `RunBurstOn()` returns false, with the stop decided, also when the burst ran to its end in the park context with a
   stop recorded by `emu::OnSoundStop` since the run-on began (the end of a capture armed at the stop, in the burst's
   last sample: `RunSoundBurst` returns true there, and without this rule `Leave` would publish `Running` and leave the
   stop in `e.want`); `DoDspStep` uses the same rule for a step from 127 in the burst's last sample, and `DoArm7Step`
   for a burst it ran on that ended (2.3, `Park()` item 2).
6. **The nine rows** of `dc.aica.dsp` (M3-D25; `dbg_regs_tables.cpp`, `ReadDsp` of `dbg_regs.cpp`, from
   `DspDebugState`), read-only, with their nine selectors in `enum DspReg` of `dbg_regs_private.h` in front of
   `kDspRegCount` (`regs_test.cpp` checks every DSP selector against it); the counts that follow the tree
   (`regs_test.cpp`, and the nine in its `named` widths list; the DSP block of `dbg_selftest_regs.cpp`; the register
   counts of `dbg_selftest_core.cpp`).
7. **The session `dsp`** on the guest (`dbg_selftest_dsp.cpp`; on the JIT, the interpreter and the forced fallback), at
   least these tests (the names are fixed, the list may grow; no test named `dsp.<x>` is one that the run ends):
   `dsp.burst.plain` (the DSP counts with nothing armed: one phase a sample, none by the debug loop); `dsp.bp.hit`,
   `.lowestid`, `.mask` (a category that is not active stops nothing and arms nothing), `.resume` (the exemption: once;
   ended at a change of `dbg_dsp_armed`), `.everysample` (a resume stops in front of the same step one sample later, in
   the same burst when the sample was not the last), `.held` (with the guest's ARM7 held across the bursts, through
   `g_hold_arm7`: the breakpoint is reached in every sample and a resume from it stops in front of the same step one
   sample later, `MDEC_CT` one less, nothing of the ARM7 executed: its registers as the debugger reads them, and of its
   stored context every word but `INTR_PEND`, which the interrupt update of a held sample sets when the timer's request
   latches); `dsp.step.suspended` (`STEP` + 1, a new `stopId`, cause `Step`, no `Running`, the SH4's PC and the
   scheduler's time unchanged), `.wrap` (from 127: `STEP` 0, the mix and the interrupt update of the sample ran,
   `SOUND_BURST_SAMPLE`), `.burstend` (127 of the burst's last sample, with the burst suspended in that sample: none
   suspended behind it, the tail ran once, the step's stop published with a new `stopId` and no `Running`), `.frompause`
   (no burst suspended: no `Running`, the SH4 ran one AICA period at most, `STEP` 1), `.fromarm7` (from an ARM7 stop:
   the rest of the ARM phase ran, `STEP` 1; an ARM7 breakpoint
   in the rest of that phase stops first, with its own cause), `.fromsampleend`, `.held` (the guest holds the ARM7
   before the burst, through `g_hold_arm7`: the step is step 0 of that burst's first sample, `STEP` 1, cause `Step`, no
   `Running`, nothing of the ARM7 executed; M3-D7), `.sh4first`, `.breakpointed` (it executes its step with a listed
   breakpoint on it), `.overout` (`-32004`); `dsp.sh4step.dropped` (an SH4 step from a DSP stop with
   the breakpoint active is dropped, and the new stop is at the same step one sample later); `dsp.arm7step` (an ARM7 step
   from a DSP stop executes its instruction and is not dropped by the DSP's breakpoint); `dsp.reset.suspended`,
   `.hoststop.suspended`, `.exit.suspended` and `dsp.sameslice.reset`, `.hoststop`, `.exit` (as `arm7`'s, for a burst
   suspended in the DSP phase and at the end of a sample); `dsp.reads.suspended` (every accessor twice, equal;
   `DspDebugState` equal to the core's fields; `DebugReadRegs` of the DSP's area equal to `RegRead`); `dsp.regs.move`
   (each of the nine rows takes at least two values over the steps of one sample of the guest's program, and the rows
   equal the core's fields at every stop); `dsp.grace` (the mask cleared while suspended in the DSP phase: the resume
   runs through). And in `arm7.step.notclocked` (`dbg_selftest_arm7.cpp`), with `TargetSteps(Target::Dsp)` true: Over and
   Out on the DSP `-32004`; Into is not asked there (`dsp.step.held` holds it).
8. **Neutrality targets** (`nulldc_sound_neutral`, JIT and interpreter; 5.1): `dspn.loop` (the debug loop forced in every
   sample for the whole run; its ok line: every DSP phase went through the debug loop, by `SoundBurstDspCounts`),
   `dspn.hitresume` (at least 200 stops at DSP breakpoints, in at least 100 bursts, at at least 8 different steps),
   `dspn.steps` (at least 20 steps of each kind: inside a phase, from 127 across the end of a sample, across the end of a
   burst, from a stop with no burst suspended, from an ARM7 stop), `dspn.hoststop` (host stops with the burst suspended in
   the DSP phase and at the end of a sample: the completion in `dc_run`).
9. **Contract**: the mode `dsp` of `contract.py` (`--elf dsptest.elf --milestone M3 --dsp-program --only S00,S21`), the
   targets `debugger.contract.m3dsp21.guest` and `.interp`, which require the note `dsp: stepped with the arm7 held`
   (`contract.py` accepts `--require-note` in the mode `dsp`; today it refuses it outside `m2` and `parts`). They stay
   behind E8b: they are the only run in which S21 meets the held ARM7 (M3-D32).

**Owns.** `libswirl/libswirl.cpp`, `libswirl/libswirl.h` (2.4); `RunPhase` in `libswirl/hw/aica/dsp.cpp`, and in
`hw/aica/` what the phases need, nothing of what a sample computes; `dbg_dsp.cpp`, the new `dbg_dsp_private.h`,
`dbg_emu.cpp`, the DSP node of `dbg_regs_tables.cpp`, `ReadDsp` of `dbg_regs.cpp`, `enum DspReg` of
`dbg_regs_private.h` (the nine selectors), the new `dbg_selftest_dsp.cpp`, the DSP block of `dbg_selftest_regs.cpp`,
`emu.step` of `dbg_selftest_emu.cpp`, the DSP lines of the held-ARM7 test of `dbg_selftest_arm7.cpp`
(`arm7.step.notclocked`), the register counts of `dbg_selftest_core.cpp`; `tests/debugger/regs/regs_test.cpp` (counts, the
nine rows of the `named` widths list); its calls in `tests/debugger/guest/neutral/CMakeLists.txt`; its lines of
`tests/debugger/selftest/CMakeLists.txt`; `tests/debugger/contract/CMakeLists.txt` and `contract.py` (the mode `dsp`,
`--require-note` for it); its rows and counts in the READMEs.

**Must not touch.** The frozen headers; `dbg_core.cpp`; `dbg_sgc.cpp`; the sweep and the mix of `sgc_if.cpp`; `dsp_core/`;
the guest; the UI repository — a defect of a runner rule that its targets meet is reported, and the integrator has it fixed
there.

**Depends on.** G merged and B4 taken (W3's clock is in the tree since before G); the pin at F4a or later.

**Gate.**

- The five builds; all ctest targets green twice, none skipped, the number stated.
- **Before / after against B4**: the lines of the guest and of 2ndmix, both engines, whole (seven fields), and the state
  hashes (the guest at 90 vblanks; design 5.7's, both engines, and 2ndmix 600 in the five builds: B4 has them, 5.3),
  identical; and of the `-DNULLDC_DEBUGGER=OFF` build, which equal the default build's at base and tip.
- The neutrality targets of item 8; the session `dsp` in its three registrations; the contract targets of item 9; W3's
  `held` targets; the M1 and M2 gates unchanged on three engines and the M2 guest run.
- No host cost and no speed: speed gates nothing in M3, and M3G measures E8a and E8b together (5.4, M3-D15).
- ThreadSanitizer: the session `dsp` and the contract mode `dsp` on the interpreter, no report.

**Verifier.** Mutations, each of which a named test or target has to catch: the exemption not armed (`.resume`); not ended
at its first check (`.everysample`); not armed for an SH4 step's leave (`dsp.sh4step.dropped`: the stop comes with nothing
executed); not armed for an ARM7 step (`dsp.arm7step`); `arm7::PrepareResume` left out in front of a DSP step
(`.fromarm7`); the step's first check not let through (`.breakpointed`); the step from 127 complete in front of the next DSP
step instead of at the end of the sample (`.wrap`: an ARM phase ran); `Running` published by a step (`.frompause`); the
debug loop calling `RunSample` for a step (`.suspended`); `dbg_dsp_armed` that ignores the mask (`.mask`); not the lowest
id; the end of a sample not suspending (`.wrap`); the sweep made again when a DSP phase is run on (every line); the tail
skipped or run twice (`.burstend`); `RunBurstOn` returning true for a burst that ended with a stop recorded (`.burstend`:
`Running` published and the step lost); a DSP step that is refused, or completed with nothing executed, while the ARM7
is held (`dsp.step.held`); the DSP phase skipped in a held burst (`dsp.bp.held`, the `held` targets); the ARM phase run
in a held burst (`dsp.bp.held`: the ARM7's registers move). Stress: 10,000
stops at DSP breakpoints with a resume and 10,000 DSP steps on the guest with the line still equal; 1,000 exit runs with a
host stop at a random moment while DSP breakpoints are hit; 1,000 resets while suspended in the DSP phase; DSP breakpoints
interleaved with ARM7 breakpoints, SH4 steps and pauses; once with four emulators running at a time (the run under test
among them, no other load).

### 3.9 E8b — SGC capture, memory binary, the binary path

**Delivers.**

1. **`dbg_sgc.cpp`**: H7-1's SGC block and H7-4; the frame of H7-4's layout, taken through `AICA::DebugReadRegs` and
   `AICA::DebugGetChannel`; a capture's buffer of its own, the server's buffer replaced at completion; the notice.
2. **The channels' outputs** (`hw/aica/sgc_if.cpp`): while `dbg_sgc_active` is not 0 the sweep keeps each channel's
   `oLeft`, `oRight`, `oDsp` of the sample, and `DebugGetChannel` gives them (H7-6); one flag test a sample otherwise.
3. **`dbg_core.cpp`**: `state.getMemoryBinary` (a read, as `state.getMemorySlice` with `kMaxMemoryBinaryBytes`; the
   result `{baseAddress, totalBytes, unreadable?, binaryId}`; a frame of kind 2); `state.getSgcFrameData` (a read;
   JSON-RPC `-32011` without a buffer; `{frameCount, channelsPerFrame: 64, bytesPerChannel: 128, totalBytes, binaryId}`;
   a frame of kind 1); `state.recordSgcFrames` with the classes of design 3.3 and the deferred response of M3-D28 (the
   pairing of a notice with its request, the deadline through `sgc::Cancel`, the refused resume, the closure that finds
   no emulator thread: a `CC_TABLE` closure that `emu::Call` ran under L, `!emu::OnEmuThread()`, arms nothing, and the
   method answers by the lifetime then, `-32001` Stopped, `-32002` NoMachine; a closure that the exiting thread ran in
   the drain of `OnThreadExiting` is on (E) and may arm, and the cancel of item 4 answers it `-32001`); the
   binary path (the method's outcome carries the frame; the response is sent and then the frame, to that client only;
   nothing after an error); `binaryId` per connection from 1, shared by the two kinds; the capabilities `sgc-capture` and
   `memory-binary`; for the selftest client, a test hook in `dbg_core_private.h` that takes binary frames and deferred
   responses.
4. **`dbg_emu.cpp`**: `sgc::Cancel(kErrHostStopped)` in `OnThreadExiting` **behind the drain of the queue** (the queue
   empty, L still held: no queued call runs after it), so that a capture a `CC_TABLE` call of the drain armed is
   cancelled too (2.3); `sgc::Cancel(kErrCaptureTimeout)` in `OnGuestStateReplaced`.
5. **The transport** (M3-D29; `dbg_net.cpp`, the comment of 2.6): text and binary counted apart, 16 MiB and 64 MiB; a test
   in `tests/debugger/net/`: a 32 MiB binary message followed by ticks, read slowly, keeps the client; more than 16 MiB
   of text, or more than 64 MiB of binary messages, disconnects it.
6. **Selftests**: in `core` the expectations of the three methods (`-32004` before) and the capability counts; a session
   `sgc` on the guest (names fixed): `sgc.layout` (each record against the accessors at that sample), `sgc.count` (a
   capture armed at a stop holds exactly `frames` frames: the AICA's time steps from the arm to the capture's stop,
   `SoundBurstCounts` `out[2]`, are `frames`, and `frames` + 1 when it was armed with the burst suspended in front of a
   DSP step, whose sample's sweep came before the arm and its time step behind it; armed at each of `SOUND_BURST_ARM7`,
   `SOUND_BURST_DSP`, `SOUND_BURST_SAMPLE` and with none suspended), `sgc.stop` (armed at a stop: a stop with reason
   `capture` and a new `stopId`; armed at a stop with a burst suspended and fewer frames than its rest: the stop with no
   `Running`; **armed at a stop with a burst suspended and exactly as many frames as the burst has samples left: the stop
   with reason `capture`, a new `stopId`, no `Running`**, the burst ended with none suspended), `sgc.running` (armed
   while running: no stop), `sgc.second` (`-32003`, the first goes on), `sgc.previous` (`getSgcFrameData` while one
   records gives the buffer before), `sgc.reset` (`-32014` at once, the buffer as it was), `sgc.hoststop` (`-32001`),
   `sgc.held` (a capture armed while the guest holds its ARM7, through `g_hold_arm7`, for every burst of the capture:
   it records `frames` frames in `frames` time steps and completes; the records of the two channels the guest keyed
   show them playing, flags bit 0 set and a level above 0; M3-D7), `sgc.timeout` (a capture whose machine stops:
   `-32014` at 5 s through the notice of
   `sgc::Cancel`, and a later resume records no stop), `sgc.late` (the deadline met by a completion the cancel call comes
   behind, through a test hook: one answer, the completion's, and a request behind it is answered by its own capture),
   `sgc.untaken` (the deadline's cancel call not taken, through a test hook: the core answers `-32014` itself, a new
   request is `-32003` until the owed notice has come, and that notice answers nothing), `sgc.refused` (a resume that
   `emu::Resume()` refuses behind the arm, through a test hook: `-32003`, no capture records), `sgc.nothread` (the
   method's `CC_TABLE` closure run under L with no thread, through a test hook, arms nothing: `-32001`, and a later
   thread records no frame for it), `sgc.drain` (the exiting thread's drain takes a `recordSgcFrames` `CC_TABLE` call:
   the request made while the thread naps in its stop callback, as `emu.hoststop`'s queued calls are, or through a
   test hook; the test shows that the closure ran on (E) and that `Arm` returned 0 there, so that it is not
   `sgc.nothread`'s path; the request is answered `-32001` well within the 5 s deadline, `dbg_sgc_active` is 0 behind
   the exit, and a thread the host starts behind it records no frame and gives no second answer), `sgc.merge` (the
   last frame in the sample of a DSP step's stop: the step's stop is published, the response sent), `sgc.outputs` (the
   kept outputs are those of the sample: a channel alone, its direct outputs against the stream's pair; and while no
   capture records, `DebugGetChannel` gives 0 for the outputs of every channel, a sounding one included, H7-6),
   `mem.binary.*`
   (the bytes of `getMemorySlice`, the unreadable runs, `-32010`, `-32004` for `dsp`, the bounds, the order of
   `binaryId`s).
7. **Neutrality targets**: `sgcn.running` (captures of 1 to 4096 frames while running: line and hash whole), `sgcn.paused`
   (captures armed at stops).
8. **Contract**: modes `m3` (2ndmix, `--milestone M3`: S00 to S17, S19, S20; the targets `debugger.contract.m3`,
   `.interp`, `.fallback`) and `m3dsp` (the guest, `--milestone M3 --dsp-program`: S00, S17, S21; `debugger.contract.
   m3dsp.guest`, `.interp`, with the note `sgc: captured with the arm7 held` required; `contract.py` accepts
   `--require-note` in the mode `m3dsp`). E8a's `dsp` mode targets (`debugger.contract.m3dsp21.guest`, `.interp`, with
   the note `dsp: stepped with the arm7 held`) stay: they and `m3dsp` are run 2 of the M3 gate on the guest (M3-D32).

**Owns.** `dbg_sgc.cpp`, and a private header `dbg_sgc_private.h` for its test hooks; `dbg_core.cpp`, `dbg_core_private.h`;
`dbg_net.cpp` and the comment of 2.6 in `dbg_net.h`; the two cancels in `dbg_emu.cpp`, and the rule of 3.8 item 5 in
`RunBurstOn` only where E8a's needs a fix for a capture; `hw/aica/sgc_if.cpp` / `.h` (the kept outputs,
`DebugGetChannel`); in `libswirl.cpp` the capture's call site only where E8a's needs a fix; `dbg_selftest_core.cpp`; the
new `dbg_selftest_sgc.cpp`; `tests/debugger/net/**`; its calls and lines in the neutral, selftest and contract CMake files
and `contract.py` (the modes `m3`, `m3dsp`, `--require-note` for `m3dsp`); its rows and counts in the READMEs.

**Must not touch.** The frozen headers but the clause of `dbg_net.h`; `dbg_dsp.cpp`; `dsp_core/`; the guest; the UI
repository.

**Depends on.** E8a merged.

**Gate.** The five builds; all ctest targets green twice, none skipped, the number stated; the lines and hashes against B4,
whole, with no capture and with the neutrality targets of item 7; **the contract gate M3**: run 1 on 2ndmix on the JIT,
the interpreter and the forced fallback, 20 scenarios passed, 0 partial, 0 not applicable each, and run 2 on the guest on
the JIT and the interpreter: `m3dsp` 3 passed with its note, `m3dsp21` 2 passed with its note; the M1 and M2 gates
unchanged and the M2 guest run; ThreadSanitizer of the session `sgc` and of the contract runs on the guest (interpreter):
no report. No host cost and no speed (5.4: M3G measures E8a and E8b together).

**Verifier.** Mutations: the response sent before the capture is complete (S17's other-length rule, `sgc.count`); one frame
too many or too few (`sgc.count`); a capture's stop not suspending at the end of its sample (`sgc.stop`); `Capture` ranked
as no cause (`sgc.stop`: nothing published); `binaryId` per kind; a frame before its response; a frame behind an error;
binary bytes counted as text by the transport (the net test); no cancel at a reset (a stop after the answer); a deadline
that answers without cancelling (`sgc.timeout`); a deadline that answers itself while its cancel call is taken
(`sgc.late`: two answers, or the next request answered early); a new request armed while a notice is owed
(`sgc.untaken`); the arm left in place behind a refused resume (`sgc.refused`); a closure with no thread that arms
(`sgc.nothread`); the cancel of `OnThreadExiting` in front of the drain (`sgc.drain`: `-32014` at the deadline, or
`dbg_sgc_active` 1 behind the exit); `RunBurstOn` returning true for a burst that ended with the capture's stop
(`sgc.stop`: `Running` published, the reason `capture` lost); the outputs kept while no capture records (`sgc.outputs`:
non-zero outputs with no capture); a capture refused while the ARM7 is held (`sgc.held`, and S17's capture at the
anchor in `m3dsp`, which a `-32012` fails); a capture left without frames in a held burst (`sgc.held` alone: on the
guest `_start` releases the ARM7 in front of the first tick, so S17's capture at the anchor records clocked bursts
only). A capture of 4096 frames from the UI's own client, with ticks flowing, keeps the connection.

### 3.10 F4d — the Audio Debugger against the emulator (UI)

**Delivers.**

1. **"Load from emulator"** (M3-D35), in a module of its own (`src/app/dsp/emulatorSnapshot.ts`) that the playground calls
   in a workspace's dock panel: at a stop, one `state.getMemoryBinary {target: "arm7", address: 0, length: 0x00808000}`
   (wave memory and the register file of one moment) and the rows of `dc.aica.dsp` and `dc.aica.common` of the paused tick
   of the same `stopId`; loaded into the core with `pokeReg` (the registers as the CPU sees them, the MEMS low bytes, the
   MIXS words into the bank `MIXS_BANK` names, RBPL from `RBP`, `RBL` and `TESTB0`), `set(REG.*)` for `STEP`, `MDEC_CT`
   and the nine rows (`MIXS_DSP` from `MIXS_BANK`, `MIXS_FILL` the other bank), and the first 2 MiB of the read as wave
   memory; the assembly source regenerated from the loaded registers as "Load Regs" does; a banner naming
   `<emulator.name> @ <host>`, the `stopId` and the tick's time. **The load continues the emulator exactly from a stop in
   the DSP phase behind step 0** (`STEP` 1 to 127, a burst suspended in front of a DSP step: a DSP breakpoint's or a DSP
   step's stop), up to the end of that sample. At any other stop (`STEP` 0: an SH4 or ARM7 stop, the end of a sample, a
   stop in front of step 0) the bank the DSP reads next is the sweep's fill bank, which neither a CPU read nor
   `MIXS_BANK` shows (step 0 changes the banks), and the banner says that MIXS of the next sample is not known; EXTS has
   no row and reads 0 for a CPU, so the banner says it is loaded as 0 at every stop. While the target runs the button is
   off and says why. The tool route stays without a connection.
2. **The DSP's controls whatever the ARM7 does** (M3-D7): the toolbar and the DSP disassembly offer the DSP's step and
   breakpoints while `ENABLED` of `dc.aica.arm7` reads 0, and only the ARM7's controls are off then, with their text.
   The UI does so today (`held` is computed for the ARM7 alone, `devtools.md` 1.2), but the decision is made in the
   two components: `RunControls.tsx` and `DisassemblyPanel.tsx` each pass `held: target === "arm7" && arm7Held(...)`
   to `stepOff`, which takes `held` as given, so no test of `runControls.ts` reaches it. **The decision moves into
   `runControls.ts`**: `heldFor(target, registersByPath)`, true only for the target `arm7` while `ENABLED` of its node
   reads 0 (`arm7Held`), called by both components in place of their own expression; a unit test holds it false for
   `dsp` and `sh4` with `ENABLED` 0 and true for `arm7`. The UI's unit suite has no component tests (`vitest.config.ts`
   runs in `node`, there is no `.test.tsx` and no DOM library), so the two callers are held end to end: an e2e test
   against the mock, started with `arm7Held`, `startPaused` and the M3 support (as the describe "an ARM7 that is held
   in reset" of `e2e/toolbar.spec.ts` starts its mock), finds, while `ENABLED` reads 0, the DSP's step on and stepping
   from the toolbar and from the DSP disassembly, and the ARM7's off in both; and the smoke spec's step at the anchor
   (item 5) is made from both.
3. **The help**: a DSP breakpoint on a step is reached in every sample; to let the SH4 run on, disable or mute it.
   `documentationSections.ts`: the playground connects in a workspace and stands alone on its tool page.
4. `README.md`, "The UI against the emulator", with two-dash switches (`--config`), and the comment of
   `e2e/emulator-smoke.spec.ts`.
5. **The smoke spec's M3 part**, against a headless emulator on the guest with the bundle embedded: a DSP breakpoint and
   steps from the toolbar; a DSP step at the guest's anchor, where its ARM7 is held (`ENABLED` 0), from the toolbar and
   from the DSP disassembly: each steps; a capture with a sounding channel shown active;
   "Load from emulator" at a DSP breakpoint stop at a loaded step s of at least 1, then k steps in the playground
   (s + k below 128) equal to k steps of the emulator in the nine rows and in the TEMP, MEMS and EFREG words read back;
   the banner of a load at an SH4 stop says that MIXS and EXTS are not known.

**Owns.** `src/app/dsp/emulatorSnapshot.ts` and its tests, the call into it and the banner in `DspPlaygroundPanel.tsx`,
`src/app/layout/RunControls.tsx`, `src/app/panels/DisassemblyPanel.tsx` (their calls of `heldFor`, and a text that says
the DSP is held), `src/state/runControls.ts` and its test, `src/app/layout/documentationSections.ts`, the help,
`README.md` ("The UI against the emulator"), `e2e/emulator-smoke.spec.ts`, e2e specs of the load path against the mock,
the e2e test of the DSP's controls while the ARM7 is held (in the describe of `e2e/toolbar.spec.ts` named above, or a
spec of its own).

**Must not touch.** The schema, the mock, the runner, the SGC panel, the module.

**Depends on.** F4b and F4c (or F4c behind it), E8b merged.

**Gate.** `npm run check`; the unit suite three times in a fresh clone; the e2e suite; the load path against the mock
and the test of item 2 in e2e; the smoke spec against the emulator at E8b's merge with the bundle embedded, run by the
line of the README.

**Verifier.** Mutations: the snapshot taken while running from two reads; MIXS loaded into the other bank (the k steps
differ); `TESTB0` dropped (the mock's `TESTB0` option, against the mock); the DSP's controls turned off while the ARM7 is
held, as round 2 of the cut had it, made in `heldFor` (its unit test, the e2e test and the smoke spec's held steps red)
and in each of its two callers, `RunControls.tsx` and `DisassemblyPanel.tsx` (the e2e test and the smoke spec's held
step from that place red); the banner of another `stopId`; a load at `STEP` 0 without the banner's warning.

### 3.11 M3G — the gate (integrator)

On the merge of every other package of the round (W3, 3.12, among them), with the UI pinned at its head and **the
emulators of items 1, 3 and 5 carrying the UI bundle of that head** (configured by themselves with
`-DNULLDC_DEVTOOLS_DIST`, handed to the tests project as `-DNULLDC_EMULATOR=` and `-DNULLDC_EMULATOR_TA_HLE=`, as at
M2G).

1. The contract gate M3 against the emulator: run 1 on 2ndmix, the JIT, the interpreter and the forced fallback, 20
   passed, 0 partial, 0 not applicable each; run 2 on the guest, the JIT and the interpreter: S00, S17, S21 passed with
   the note `sgc: captured with the arm7 held`, and `--only S00,S21` passed with the note `dsp: stepped with the arm7
   held` (S21 meets the held ARM7 only in a run of its own: in the first, S17's capture lets the guest release it);
   **and the M2 gate's second run on the changed guest** (S00, S16, both notes).
2. The sound lines of the guest and of 2ndmix, and the guest's exit state hash at 90 vblanks, equal B4's (both engines,
   the default build and `-DNULLDC_DEBUGGER=OFF`); the state hashes of design 5.7 (2ndmix 600 and 4000, hello 300;
   M3-D38) equal B4's, both engines, and 2ndmix 600 in the five builds (B4 has them, 5.3; B0's `aica.NoBatch=yes`
   column is the old DSP's and differs wherever W1's integrator accepted a justified move, and B0 to B2 are before W3's
   clock); the oracle targets and the `held` targets green.
3. Every ctest target green twice, none skipped, the number stated; `npm run check` and the e2e suite in a fresh clone.
4. ThreadSanitizer of the sessions `dsp` and `sgc` and of the contract runs on the guest (interpreter): no report.
5. The smoke spec against a headless emulator on the guest with the bundle (its second run; F4d made the first).
6. **Speed, measured and reported; it gates nothing** (5.4, M3-D15, the owner's answer to M3-Q2): `tests/perf` on
   2ndmix, ten rounds in turn, both engines, the end of the round against B0's binary in both of its settings, as ratios
   of medians of the same session and, beside them, M2G's figures (1459.1 %, 566.1 %); the host instructions a sample
   of W1, W2 and W3 together against B0's batch default (on runs the harness does not fold, as W1's), and of E8a and E8b
   together against G's merge (harness runs); the guest with its program, and hello (every burst of which may find its
   ARM7 held), for the record.
7. **The documents**: the design text changed by section 8; `DESIGN-V3-DECISIONS.md` (decisions 9, 22, 26, 32, 35, 42);
   the plan's status (track 1, T5.0, the backlog) and its ground rule on pushing (section 8 row 41, with design D5);
   `DESIGN-V3-OPEN.md`'s head ("revision 7 is applied ..."); their copies under `docs/nulldc3/`, with this file as
   `devtools-integration-m3-round.md` in place of R7's copy.
8. **The review PRs** (the owner's instruction, 2026-10-16): the UI first — branch `skmp/nulldc3` of
   `skmp/dreamcast-devtools` pushed, a PR into `main`; then the emulator — branch `skmp/nulldc3` of `skmp/nulldc3`, its
   submodule naming a commit of the pushed UI branch, a PR into `main`. Each PR's description: what M3 delivers, the gate's
   results, the decisions and the owner's answers, what a user meets (W1's, W2's and W3's lists), the speed for the
   record. Nothing is merged by the integrator.

What it finds goes back as a fix round to the package that owns the file. If the verifiers leave notes that block nothing,
the integrator adds leftover packages in front of M3G as it did in M2 (EXe, EXu), one per repository.

### 3.12 W3 — the AICA clocked whatever ARMRST says

Added in round 3 by the owner's answer to M3-Q1 ("Fix it in M3"). Its place in the order is behind W2 and in front of F4c
and G (section 4, M3-D41); its subsection stands here so that the numbers of the others stay.

**What the console does.** The AICA runs whatever ARMRST says; ARMRST holds the ARM7 alone. caique's console cases
(`caique-rtl`, `model/cases/`) begin every case and sub-test with `aica_reset` (`aica_io.h`), whose first act is
`aw(R_ARMRST, ar(R_ARMRST) | 1)`, "hold the ARM7 in reset: the tests own wave RAM", and then measure, with the ARM7
held: the timers counting at the sample edge, bit 10 of both pending registers at every sample edge, and the SH4's line
as `MCIEB & MCIPD` (`timer_irq.c`, "the ARM stays in reset"; `model/NOTES.md`, "Timers and interrupts"; `TODO.md` 4.1
and 4.2); the DSP's programs and their outputs (`dsp_basic.c`, `dsp_mem.c` and the other DSP cases: the files J1 and J2
hold the core and W1's machine to); the sound generator's streams, replayed sample for sample (`model/HANDOVER.md` X12);
and the envelope's sample counter, which is also the timers' prescaler, untouched by a release of the ARM7 onto `b .`
vectors and a hold again (`HANDOVER.md` T1, `eg_kprobe.c`). Not measured: the phase of a release against the sample
edge ("a console constant that is not known", `INTEGRATION.md`, "Reset"), and what the console does at a release with a
request latched for the ARM7 while it was held. Where nothing was measured the owner's answer is the rule: only the
ARM7 is not clocked while held.

**Delivers.**

1. **The baseline B2 first** (5.3), on W2's merge; with it, from a scratch build of that commit that counts them (nothing
   of it committed), h, the number of bursts that find the ARM7 held, for each run of B2: the guest (by reading 0 on
   both engines: `_start` releases the ARM7 onto its one-word program within the first slice, in front of the first
   tick, and holds it for the set-up between the first tick and the second only; the guest's README, "The first tick"),
   2ndmix (above 0: its ARM7 goes 0, 1, 0, 1 while it starts), and hello's exit run.
2. **The burst** (`libswirl.cpp`, `RunBurst`): the 32 time steps of a burst made whatever `arm_on` says; the ARM phase
   (`SoundCPU::RunSample`) only while `arm_on` is true; `counts[1]` counts the ARM phases (none in a held burst),
   `counts[2]` 32 time steps a burst; `arm_on` latched at the start of the burst as today; `dbg_arm7_OnBurstUnclocked()`
   as today (no instruction of the ARM7 executes in a held burst); the comment of the burst state. Nothing of what a
   time step computes changes: `AicaDevice::TimeStep` and its phases, the timers, the sound generator, the DSP and the
   interrupt functions are as W1 and W2 left them (M3-D7, M3-D11). A held burst's 32 samples go to the audio stream as
   every burst's do, so they pace it, and the harness folds them (M3-D43); CD-DA advances in them.
3. **The texts**: the clause of 2.6 in `debugger.h`, in the commit that clocks the AICA; the sentence of 2.4 in `struct
   AICA`'s comment (`aica_mmio.h`, outside the debug block); every text that says the AICA stands still while the ARM7 is
   held — the guest's README ("The first tick": the release "gives the AICA its clock (while the ARM7 is held no timer
   steps and, sample by sample, no sample is made)", "a guest whose second release is taken out is silent sample by
   sample"), and its row of the mode `sounds` ("fewer sample by sample, where no pair exists while the ARM7 is held":
   every tick makes its 32 pairs), the `sounds` paragraph of `soundlines.py`'s docstring ("fewer sample by sample,
   where no pair exists while the ARM7 is held in reset": every tick makes its 32 pairs now; the family of silence
   hashes the mode checks stays, a superset of the one hash of N ticks), `docs/nulldc3/automated-runs.md` ("no pair at
   all while the ARM7 is held in reset"), `tests/debugger/selftest/README.md` (the paragraph on the burst, "with the
   ARM7 held no timer of the AICA overflows", and the rows of `arm7.burst.plain`, "no ARM phase, no time step and one
   tail while the guest holds it in reset", and of `arm7.step.unclocked`, whose burst "goes on with its tail": both
   have 32 time steps now), `tests/README.md`, and the comments of the selftests that say so. `dsptest.s`'s two
   comments (the file head and the first release) are G's: a file of the guest's manifest (3.7 item 1).
4. **The witness `held`** (M3-D42), a mode of `soundlines.py` on the guest, with two copies built in the work directory
   as `release` builds its copies: **held** — `_start`'s last release, the `mov #0, r0` at `g_main - 4`, made `mov #1,
   r0` (the halfword is checked to be `0xE000` before it is patched, else the mode fails): the ARM7 stays held behind the
   set-up, and every burst from the second on finds it held; **idle** — the ARM7 image's instruction at `a_reset` made
   `b .` (`0xEAFFFFFE`): the released ARM7 stays on it with FIQ and IRQ masked by its reset and touches nothing of the
   AICA. Their lines (N = 2000, the engine of the target) agree in `samples regs sh4 dsp ticks cycles`: a held ARM7 is an
   idle one for the rest of the AICA. Controls: the two `arm7` fields differ (both patches took); the held copy's
   `samples` is none of the hashes of silence (the channels the SH4 keyed play while the ARM7 is held); the idle copy's
   `samples` differs from the guest's (the guest's FIQ handler is heard). The targets
   `debugger.sound.held.dsptest.<jit|interp>` in the guest's `CMakeLists.txt`. The runner's own test
   (`soundlines_test.py`, `debugger.sound.runner`) gains the mode on a copy of the committed `dsptest.elf` with `--dir`
   at its labels, and a stand-in kind `held` whose line depends on the guest it is given, as the emulator's does (its
   stand-ins print one fixed line today, and its program is a text file): its `samples` is one value where that
   guest's ARM7 would run its program (the halfword at `g_main - 4` is `0xE000` and the word at `g_arm7_image +
   a_reset` is the guest's) and another where it would not; its `arm7` is formed from both words; the other fields are
   fixed. Green on that stand-in; red, naming the field, on a variant that prints another `regs` for the held copy;
   red, naming the halfword, on an ELF whose halfword at `g_main - 4` is not `0xE000`. On B2's binary the targets are
   red: the held copy has the 32 pairs of the first tick and a DSP that stood still.
5. **The selftests that assumed a still AICA while the ARM7 is held**: in `dbg_selftest_arm7.cpp` the counts of a held
   burst ("a burst with the ARM7 held is no ARM phase, no time step and one tail", and the step that a held burst
   completes, "one burst, no ARM phase, no time step, one tail": 32 time steps now) and `PauseAfter`'s comment ("works
   with the ARM7 held, where no timer overflows"); any other selftest that counts the time steps of a held burst or relies
   on an AICA that stands still while the ARM7 is held, each named in the report with what changed and why. And a test
   **`arm7.held.clocked`** (the session `arm7`, the guest): with the guest's ARM7 held across k bursts (`g_hold_arm7`),
   each burst is one burst, no ARM phase, 32 time steps and one tail (`SoundBurstCounts`); nothing of the ARM7
   executed: every register of it as the debugger reads it is as before (`ReadArm()`, `ArmDiffers(...) == kArmRegs`,
   as `arm7.step.unclocked` checks), and so is every word of its stored context (`StoredWord`) but `INTR_PEND`
   (`regs[47]`, which `ARM7Backend::UpdateInterrupts` sets to `e68k_out && armFiqEnable` when the interrupt update
   latches a request: it moves with the observable below), and the four fields behind the words (`armIrqEnable`,
   `armFiqEnable`, `armMode`, `enabled`); `MDEC_CT` is 32 k less modulo 2^16 (`DSP::DebugGetState`); the channels the
   guest keyed play on (`AICA::DebugGetChannel`): k is odd, both channels are still `enabled`, and the second channel's
   play position (`ca`, `caFraction`) differs after the k held bursts. Its step (OCT -1, any of the four FNS the ARM7
   gives it) is 16, 20, 24 or 28 samples a burst, so an odd number of bursts never closes its loop of 32 samples; the
   first channel, which at FNS 0 makes exactly one loop a burst, is no witness. And the interrupt update ran in the held
   bursts — an observable the package names in its report, which the verifier's mutation of it must turn red. The hold
   is made with no request pending and none in service (the stored `aica_interr` and `e68k_reg_M` 0, checked in front
   of the held bursts; a hold that finds either is made at a later burst, a bounded number of times), and the k held
   bursts span at least the 13 samples of the timer (one burst does). The expected observable: the request of the
   ARM7's timer, which `a_reset` enabled, raised and latched behind the hold, as the sound CPU's stored interrupt state
   shows: `aica_interr` 1 with `aica_reg_L` 2 (timer A's level), `e68k_reg_M` 1, `e68k_reg_L` 2 and `e68k_out` set;
   `INTR_PEND` set with it where the held ARM7's F is clear, 0 where F is set (inside `a_fwin`, or at the return of
   `a_fiq`).
6. Its rows and counts in the READMEs.
7. **In the report**: B2's h per run; each moved field with what shows it right (5.1, M3-D42); what a user meets (a guest
   that holds its ARM7 is no longer silent: the channels the SH4 keys play, the DSP runs, the timers count and raise
   their interrupts, CD-DA plays and the stream keeps its pace; every boot makes samples from its first tick); speed,
   measured and reported (5.4): `tests/perf` on 2ndmix, ten rounds in turn, both engines, against B2's binary; host
   instructions a sample on harness runs of the guest and of 2ndmix and on hello's exit run.

**Owns.** In `libswirl/libswirl.cpp` the burst (`RunBurst`, `AicaUpdate`, the comment of the burst state); the sentence of
2.4 in `libswirl/hw/aica/aica_mmio.h`; the clause of 2.6 in `libswirl/debugger/debugger.h`; in
`tests/debugger/guest/dsptest/` the mode `held` of `soundlines.py` and the `sounds` paragraph of its docstring, its case
in `soundlines_test.py` (the stand-in kind `held` among it), the `held` targets of `CMakeLists.txt`, and the README's
sentences of item 3 (those of "The first tick" and the row of the mode `sounds`) with the rows of the mode and its
targets; in `dbg_selftest_arm7.cpp` the expectations of held bursts and `arm7.held.clocked`; in other selftests only
what item 5 names; `tests/debugger/selftest/CMakeLists.txt` if the new test needs a line;
`tests/debugger/selftest/README.md` (the paragraph, the two rows of item 3, the row of the new test), `tests/README.md`,
`docs/nulldc3/automated-runs.md` (the sentences of item 3).

**Must not touch.** The frozen headers but the clause; what a time step computes (`aica_mmio.cpp`'s `TimeStep` and its
phases, the timers and the interrupt functions, `sgc_if.cpp`, `dsp.cpp`); `dsp_core/`; `hw/arm7/` (the ARM7 is held and
released as it is today); `dbg_core.cpp`, `dbg_emu.cpp`, `dbg_dsp.cpp`, `dbg_sgc.cpp`; the guest's committed files; the UI
repository.

**Depends on.** W2 merged, B2 taken; the pin at F4b's head (or F4c's, if F4c was moved in front of W3).

**Gate.**

- The five builds; all ctest targets green twice, none skipped, the number stated (W2's count, plus the two `held`
  targets and what else W3 adds).
- **Against B2** (5.1, M3-D42): (a) the guest, h = 0: every line (seven fields) and its exit state hash at 90 vblanks
  identical, both engines, the default and the `-DNULLDC_DEBUGGER=OFF` build; (b) 2ndmix, h above 0: `ticks` and
  `cycles` identical; `samples` the hash of 32 × N zero pairs (2ndmix is silent: `soundlines.py`'s family of silence
  gives k = N), where B2's is that of 32 × (N − h); `dsp` other than B2's; `regs`, `arm7`, `sh4` and the parts of the
  state hashes at 600 and 4000 vblanks identical, or moved by the rule of 5.1; (c) hello's state hash at 300 vblanks the
  same way; (d) the tip's lines and hashes equal between the default and the `-DNULLDC_DEBUGGER=OFF` build, and 2ndmix 600
  equal in the five builds.
- The `held` targets green on both engines; `arm7.held.clocked` and the selftests of item 5 green; the neutrality
  targets green; the M1 and M2 contract gates unchanged on three engines and the M2 guest run (S16's `arm7: held at the
  start`: the ARM7's `-32012` stays).
- Speed and host cost measured and reported (item 7); they gate nothing.

**Verifier.** Takes its own B2 and its own count of held bursts on a base it builds; runs the `held` targets on B2's
binary (red) and on the tip's (green). Mutations, each of which a named target has to catch: the time step left under
`arm_on` (the `held` targets; 2ndmix's k = N); the ARM phase run in a held burst (`arm7.held.clocked`: the ARM7's
registers move; the `held` targets: the held copy plays the guest's FIQ handler); 31 or 33 time steps in a held burst
(2ndmix's k; `arm7.held.clocked`'s counts); `counts[1]` counted for a held burst (`arm7.held.clocked`); a held burst's
pairs kept from the stream, or from the harness's fold (the `held` targets); the interrupt update left out of a held
burst (`arm7.held.clocked`'s observable). Reads the diff for anything of what a time step computes; rechecks each moved
field of 2ndmix and hello against its scratch trace.

---

## 4. Order

One package at a time. "Based on" is the head of the integration branch of its repository at that moment. **The UI
checkout an emulator gate runs with is pinned** (`M2-ROUND.md` section 4): it is the submodule's gitlink since N1e, moved
by the integrator in a commit of its own on `skmp/nulldc3` to the last verified UI head before the next emulator package
is cut, with `agents/nulldc3/wt/ui-pin` (a detached worktree with its `node_modules`) at the same commit, given as
`NULLDC_DEVTOOLS_DIR`. It is never a directory named `dreamcast-devtools` beside a worktree.

| # | Package | Based on | What a person can try when it is merged, and where |
| --- | --- | --- | --- |
| 1 | **F4a** | devtools `18435de`; the emulator `165775e5` for its gate's last line | the UI against the mock (`npm run mock:serve`): the DSP disassembly panel's step and breakpoints on a DSP that runs also while the mock's ARM7 is held, the nine new registers of `dc.aica.dsp` in the device tree; `npm run contract -- --milestone M3` passes on the mock, also with `--mock-support m3` and `--dsp-program` |
| 2 | **R7** | devcast `165775e5`, the pin moved to F4a | nothing: the wire is what it was (that is its gate) |
| 3 | **W1** | R7 | the emulator started with `--config config:aica.NoBatch=yes`: a game's reverb is heard, a DSP program with NOFL no longer ends the emulator; the "DSP Mode" control is gone |
| 4 | **F4b** | F4a (the devtools head); the emulator at W1's merge, from which the module is built | the playground (`/tool/dsp-playground`, no emulator needed) runs the emulator's own C++ core: the same program sounds as the console's DSP would make it |
| 5 | **W2** | W1, B1 taken; the pin moved to F4b | the emulator with no setting at all: the DSP always on, the "Enable DSP" control gone, "Disable Sound" mutes; until W3, a stretch with the ARM7 held is silent and not paced |
| 6 | **W3** | W2, B2 taken | a guest that holds its ARM7 sounds: the channels the SH4 keys play, the DSP runs, the AICA's timers count and raise their interrupts, the stream keeps its pace (2ndmix's start; the guest's copy with its second release taken out, which the `held` targets build) |
| 7 | **F4c** | F4b (the devtools head) | the SGC panel against the mock: captures of any length, empty ones, the channels that play marked by their flags; a capture of 4096 frames no longer freezes the page |
| 8 | **G** | W3, B3 taken; the pin moved to F4c | `tests/debugger/guest/dsptest/dsptest.elf` in the emulator with sound on: the two channels and the DSP's echo of the first one |
| 9 | **E8a** | G, B4 taken | the UI against the emulator on the guest (the page it serves, or the UI's dev server with `?ws=`): the DSP disassembly steps the emulator's DSP, also while the ARM7 is held, DSP breakpoints stop, the datapath registers follow in the device tree. The controls are in the UI since F2 and wake up with `stepping: true` |
| 10 | **E8b** | E8a | in the UI against the emulator: the SGC panel records the guest's channels, also while its ARM7 is held; memory reads of 16 MiB in one request |
| 11 | **F4d** | F4c (the devtools head); devcast at E8b's merge for its smoke spec | in a workspace, "Load from emulator" puts the emulator's DSP, at a stop, into the playground; the DSP's step and breakpoints stay on while the ARM7 is held |
| 12 | **M3G** | everything | M3 on the emulator, and the two PRs for the owner's review |

- **F4c may be moved forward** to any place behind F4a, between two emulator packages, never beside one, and only as a
  whole package (verified, the pin moved). F4b may not: it is built from the core as W1 leaves it, and W2's replay target
  holds its module.
- W1 before W2: W1's gate compares the per-sample path with the per-sample path, which the batch path's removal would take
  away (M3-D2).
- W3 behind W2, and in front of G and E8a (M3-D41): one sample path exists only from W2 on, so the clock is made once;
  W3 moves lines, which neither wiring gate allows; G's program, its README and its oracle's schedule (6.5) are written
  once against the final clock, and G's baseline has it; E8a and E8b build run control and the capture for a DSP that
  is clocked in every burst, so revision 7 declares nothing for an unclocked one.
- G between W3 and E8a: E8a and E8b are held to lines that have the DSP program (M3-D18), and the program cannot be on a
  tree with the old DSP, which ends the emulator on a step with NOFL (`guest-gate.md` 0 item 2).
- **No red window**: R7 changes no wire and no line; F4a's runner meets the M2 emulator only in its M1 and M2 scenarios,
  which it keeps; the S21 and S17 rules, whose held-ARM7 assertions need W3's clock, meet their first emulator in E8a
  and E8b, behind W3. Between W2's merge and W3's a stretch with the ARM7 held is silent and unpaced (M3-D43): a state
  of one package's turn, which nothing pushes (3.11 item 8).
- **The package workflow** (`agents/nulldc3/resume/pkg-brief.workflow.js`) takes the rules as an argument: `COMMON`,
  `IMPL` (twice) and `VERIFY` name `args.rules`, whose default is `M2-ROUND.md` Appendix A.0, and it writes the switch
  with two dashes. **Every package of this round is started with `rules` naming section 9 of this file, the file by its
  absolute path**; a launch without it points every prompt at A.0. The `extra` argument is no substitute: it is
  appended to `IMPL` only, and the verifiers, the reviewers and the fixers, whose prompts are built from `COMMON`,
  would still be pointed at A.0. Section 9 says that it replaces A.0 for this round, and every brief says so too, as a
  backstop.
- What can be prepared while another package has the machine (documents only): for W3, the two copies of its mode
  `held` and the scratch counter of held bursts, read against the guest's `_start` and `arm7.s`; for G, the program's
  text on the playground (it runs it on the core alone before the guest is built); for E8a, a walk through
  `dbg_emu.cpp` that names for each sentence of 2.3's `Park()` items the function it changes (`debugger.md` 5.4 has the
  table). The owner's questions are answered (7.2): nothing waits on them.

---

## 5. The sound lines and the speed through the round

### 5.1 What stays identical and what moves

The line has seven fields from W1 on (`samples regs arm7 sh4 dsp ticks cycles`); a baseline taken before W1 has six. The
state hash of an exit line keeps its parts (`sh4 sched ram vram aram arm7`). A field or a part that moves where this table
says "identical" is a defect of the package until shown otherwise, and the integrator decides; a move this table allows is
justified by the oracle named in its row, never by a stored line.

| Package | Identical | Moves | Justified by |
| --- | --- | --- | --- |
| R7 | every line and hash, both `aica.NoBatch` settings | nothing | — |
| W1 | every line, **both settings**, in the six fields of B0, against B0; every state hash, both settings, both engines, and 2ndmix 600 in the five builds; `dsp` equal between two runs, between the default and the `NULLDC_DEBUGGER=OFF` build, and on the guest between the engines | nothing on the tree's workloads (`dsp` is new): by reading none runs a DSP program (B0's scratch trace shows it, 3.3 item 1), the empty program leaves EFREG at 0, and the register file stays what the CPUs wrote (M3-D5). By reading, a move can come only from a guest that reads a register of the DSP, whose value the core gives as the console does | a scratch trace of that read (address, PC, the two values); the J2 targets for what the DSP computes; for a workload the trace shows writing a program, its `aica.NoBatch=yes` column by J2 and that trace, compared with B1 from then on |
| W2 | every default line equal to B1's `aica.NoBatch=yes` line in all seven fields; every state hash equal to B1's `aica.NoBatch=yes` hash | the default's `samples` (the batch render goes: E6b's data has the two columns differ in `samples` only), and any field or part where B1's two columns differ (to be shown for hello, which has never been run in both: W1's B0 has both) | B1's `aica.NoBatch=yes` column itself: the old per-sample path with the new DSP |
| W3 | the guest, whose bursts never find its ARM7 held (h = 0, B2's count): every line (seven fields) and its exit state hash, both engines, both builds, against B2; of every workload `ticks` and `cycles`; the tip's lines and hashes between its builds | of a workload with held bursts (2ndmix; hello, if its ARM7 is held): `samples` **must** move (each held burst now hands the stream 32 pairs: 2ndmix, silent, prints the silence of 32 × N pairs where B2 printed that of 32 × (N − h)), `dsp` **must** move (the DSP runs in the held bursts); `regs`, `arm7`, `sh4` and the state hash's parts may move, each only from its first held burst on | **J4** (5.2): the `held` targets (a held ARM7 is an idle ARM7 for the rest of the AICA, on the sounding guest) and `arm7.held.clocked`; for a moved `regs`, `arm7` or `sh4` part, a scratch trace or dump of the first tick that differs: for `regs` the bytes of the register file that differ are those a time step writes (the timers' counts, the pending registers, the channels' status words); for `arm7` the sound CPU's interrupt words and `regs[INTR_PEND]` while it is held (what the interrupt update writes when a request latches), or the ARM7's first read, behind its release, of an AICA register whose value the held bursts changed (address, PC, both values); for `sh4` the SH4's first such read, or an AICA interrupt; the integrator decides |
| G (the guest changes) | `arm7`, `ticks`, `cycles` of the guest against B3 (the ARM7 program and its release are as they were); `samples` of the copy with the EFSDL of every DSP output 0 against B3's `samples` (the DSP is heard only through EFREG, and its ring is where no channel plays); the state hash's `sched`, `vram`, `arm7`; `engines` agrees in `samples regs arm7 dsp ticks cycles`; `same`, `sounds`, `release`; 2ndmix's lines and the state hashes of design 5.7 entirely | the guest's `samples` (EFREG in the mix), `regs` (the DSP's registers in the file), `sh4` (a longer `_start`), `dsp` (a program runs); the hash's `sh4`, `ram` (the bytes of the ELF), `aram` (the ring). The guest's `samples` **must** differ from the EFSDL-0 copy's: the DSP is heard | **J3** (5.2): the `dsp` field of a copy whose sends are 0 equals the caique model's DSP driven with the guest's program on the same schedule and with the emulator's sweep; and the G1, G2, G5 rows of this cell |
| E8a | every line (seven fields) and hash against B4, the default and the `NULLDC_DEBUGGER=OFF` build, also with the debugger at work on the DSP (the targets `dspn.*`) | nothing | — |
| E8b | as E8a, also with captures (`sgcn.*`) | nothing | — |
| F4a to F4d | the emulator's lines are not theirs | — | — |

The five rows G1 to G5 that G's gate names: G1 is its row's first "identical" item (with 2ndmix's lines and the state
hashes of design 5.7 against B3's), G2 the EFSDL-0 copy, G3 `engines`, `same`, `sounds`, `release`, G4 the oracle (J3),
G5 the guest's `samples` against the EFSDL-0 copy's: they differ, the DSP is heard (no other target of the round sees a
mix that takes `EFREG` from anywhere but the core; `aica.dsp.emu.state` holds the same on W1's machine).

### 5.2 The oracle and the witnesses: the caique model, the console's cases, and what they can hold

caique's sample model is the console's DSP as far as caique measured it, step for step. It ends at EFREG: the output mixer
(EFSDL / EFPAN), MVOL and the DAC are unverified even there, and the emulator's sound generator is not caique's
(`guest-gate.md` 5.2). So the oracle compares **the DSP's state and EFREG**, never `samples`; and it compares them where
the DSP's inputs are known: zero sends, no CD-DA, the CPU's register writes. Three layers, and a fourth for the clock:

- **J1, the core against caique** (exists, T5.0): `aica.dsp.diff.*` (the core against the model's DSP behind every step,
  45,548 programs), `aica.dsp.case.<case>` (eleven caique cases on the model with the core in lockstep and on the core
  alone, against the console's files), `aica.dsp.api` with its known answers, `aica.dsp.wasm` (native equals wasm). Held
  unchanged and green by every package; W1 may change the core only under 5.4's exception, and then this suite is its
  gate.
- **J2, the emulator's wiring of the core** (W1):
  - `aica.dsp.case.<case>.emu`: a third machine of `tests/aica_dsp/cases_main.cpp`, made of the emulator's own
    `aica_mmio.cpp`, `sgc_if.cpp`, `dsp.cpp` and the core with the stubs `tests/arm7` and `tests/debugger/regs/aica` use:
    the case's register accesses through `AicaDevice::ReadReg` / `WriteReg` (the CPU's path, with its byte and slot
    handling), a sample through `libAICA_TimeStep()` with `aica.NoBatch` on (W2: unconditionally), wave memory the
    device's. With no channel keyed every channel sends 0, which is the silent sound generator of the core-alone machine.
    The seven cases anchored directly on the console's files (`dsp_basic`, `dsp_mem`, `dsp_mem2`, `dsp_mem3`,
    `dsp_wslot`, `dsp_unpack`, `dsp_pack`) run against those files on a baseline as the other case targets do; the
    expected differences are in `KNOWN_FAILURES.md` with their reason: `dsp_basic`'s three lines of console timing (as
    for the core alone) and the line of `dsp_mem3` that needs the SH4's read in the latch (`BusRead` is not wired,
    M3-D10). `dsp_coll` and `sh4_latch` key channels and are not run on this machine (the emulator's channels are not
    caique's).
  - `aica.dsp.emu.lockstep`: generated programs (the generator of `aica_dsp_diff`) run through the emulator's AICA
    device, with CPU writes of the DSP's registers between samples and channels keyed so that MIXS carries sends, against
    a core of the test's own given the same writes, sends and EXTS; the two cores' blobs equal behind every sample. **The
    reference core's sends are worked out by the test, never taken from the emulator's own `MixsSend` calls** (a hook
    there gives the reference whatever the sweep computed, wrong scale included). One way: the keyed channels are set so
    that each one's send equals its direct output (`IMXL` = `DISDL`, the pan to one side, `MVOL` 15, the EFSDL of
    every DSP and CD-DA output 0), one bus to a side, so that the pair the stream stand-in receives is the sum of the
    sends of each bus; the reference gets those sums times 16. **The CD-DA stand-in gives a non-zero pair every
    sample** (the reference's EXTS), **and the CPU writes include byte writes and 16-bit writes at slot + 2** with a
    value other than the low half's. What it must catch is fixed: each mutation of 3.3's verifier paragraph that
    concerns the sweep or the routing turns it, or a case target, red.
- **J3, the guest's program** (G): the copy of the guest in which no channel's send can be other than 0 (6.5) runs in the
  emulator to N = 2000 ticks and prints its line; the tool `aica_dsp_guest_oracle` drives a core and the caique model's
  DSP in lockstep (`DspLockstep`, which compares them behind every step) with the guest's DSP writes, read from the ELF by
  its labels, on the guest's schedule and with the emulator's sweep (6.5: in every sample `SweepBegin` and a send of 0 to
  every bus a channel's ISEL names), and folds the core's blob at every tick as the harness does; the target passes when
  the two `dsp` values are equal, on the JIT and on the interpreter. The blob carries the buses a sweep sent to
  (`mixs_sent`), and `MixsSend` marks a bus whatever the value: a tool without those sends folds another `dsp` from tick
  1 on. Since the line's `dsp` folds the whole state of the DSP at every tick, this holds every datapath register,
  TEMP, MEMS, MIXS, EFREG and `MDEC_CT` of the emulator's DSP to the model's, tick by tick.
- **J4, the AICA's clock while the ARM7 is held** (W3). The rule is the console's: caique's console cases hold the ARM7
  in reset for every case (`aica_reset`, `model/cases/aica_io.h`) and measure with it held the timers, the pending
  registers and the SH4's interrupt line (`timer_irq.c`, `model/NOTES.md` "Timers and interrupts"), the DSP (the DSP
  cases, J1's files) and the sound generator's streams (`model/HANDOVER.md` X12); a release of the ARM7 onto `b .` and a
  hold again leave the envelope's sample counter, the timers' prescaler, as it was (T1). W3 changes when the time step
  runs, not what it computes, so what a held burst computes is what a clocked burst computes, which J1 and J2 (the DSP)
  and track 5 (the rest of the fast tier) answer for. The emulator is held to the rule by the `held` targets: on the
  sounding guest a copy whose ARM7 stays held and a copy whose ARM7 runs `b .` with its interrupts masked agree in every
  field but `arm7` (3.12 item 4), so the samples, the DSP, the register file and the SH4 of a held burst are those of a
  burst whose ARM7 does nothing; and by `arm7.held.clocked` (the counts, nothing of the ARM7 executed, `MDEC_CT`, the
  channels, the interrupt update in held bursts). After G the `held` targets also run the guest's DSP program while the
  ARM7 is held.

What the round's tests cannot hold: the DSP's accesses lost to a channel's slot and the latch's words of channel fetches
and CPU reads (not wired in `fast`: M3-D10); the mix of EFREG and the levels of the output (unverified in caique too);
what the console does with a byte write to a register of the DSP (unmeasured; the emulator merges it with the file's
other byte: M3-D5); the phase of a release of the ARM7 against the sample edge and what the console does at a release
with a request latched while the ARM7 was held (unmeasured: the emulator releases the ARM7 as it does today, M3-D7);
where the fast tier's time step differs from the console's (bit 10 in `MCIPD`, the timers' prescaler: track 5).

### 5.3 The baselines

Taken by the first step of the package named, on the commit named, with the default build and the `-DNULLDC_DEBUGGER=OFF`
build of that commit, by `soundlines.py`'s *record* mode and the runner's exit runs, into
`agents/nulldc3/<pkg>/baseline/` with the commit, the build flags and the compiler beside them. **No line or hash is
stored in the tree.** A verifier does not use the file: it builds the base and the tip itself and compares its own sets.

| Name | Commit | Taken by | What |
| --- | --- | --- | --- |
| B0 | R7's merge | W1 | the guest (N = 2000) and 2ndmix (N = 40000), JIT and interpreter, `aica.NoBatch` no and yes, two runs each; the exit state hash of the guest at 90 vblanks and the state hashes of design 5.7 (2ndmix 600 and 4000 vblanks, hello 300; M3-D38), both settings, both engines, and of the five builds for 2ndmix 600; the same lines of the `NULLDC_DEBUGGER=OFF` build once; the scratch trace of 3.3 item 1 (which workload writes an `MPRO` step or reads a DSP register: by reading, none) |
| B1 | W1's merge | W2 | as B0, with the `dsp` field |
| B2 | W2's merge | W3 | the guest and 2ndmix (one setting now), both engines, both builds; the guest's exit state hash; the state hashes of design 5.7, both engines, and 2ndmix 600 in the five builds; from a scratch build that counts them, h, the bursts that find the ARM7 held in each run (3.12 item 1) |
| B3 | W3's merge | G | as B2, without the count; the copies G2 and J3 need are G's own runs |
| B4 | G's merge | E8a | as B3, on the guest with its program, the state hashes of design 5.7 among it; E8b and M3G compare with it as well |

B0 to B2 are taken before W3's clock: every workload whose bursts find its ARM7 held moves between B2 and B3 (5.1), so no
package behind W3 compares a line or a hash with them; M3G compares only speed and host instructions with B0 (5.4).

### 5.4 The speed of the sound path: measured and reported, never a gate

The DSP is always on (the owner, PLAN track 10) and the AICA is clocked in every burst (the owner's answer to M3-Q1): the
cost of the per-sample path, of the core and of the held bursts is accepted. **Speed gates nothing in M3** (the owner's
answer to M3-Q2, "don't mind perf for now"; M3-D15): no floor, no ceiling, no threshold anywhere in the round. The
packages that change a path every run takes — W1, W2, W3 — measure it, and M3G measures the round; each reports its
numbers, and the integrator records them (Appendix I). No package changes the core or anything else for speed: round 2's
lever (an empty-program path in `AicaDsp::RunSample`) is not started. The method stays fixed, so that the numbers of the
round compare with each other, with M2G's and with a later round's:

- **How instructions are counted.** User-mode instructions of a whole run (`perf_event_open`, the counter of package J1,
  `agents/nulldc3/j1/scripts/icount.c`, copied into the package's work directory), tip minus base, divided by the samples
  of the run, JIT and interpreter, three runs each, medians. Nothing under `tests/` counts instructions. **Two kinds of
  run** (M3-D15):
  - **runs the harness does not fold**, for every count against B0 (W1's, and M3G's of W1, W2 and W3 together): the exit
    runs of design 5.7 that B0 has, the guest for 90 vblanks and 2ndmix for 4000 (`config:Exit.AfterVblanks`,
    `config:Exit.StateHash=yes`), the samples of a run 32 for each AICA period of its scheduler time (the `sched` part of
    its exit line / 145124). The harness's `dsp` field is W1's: `SaveState` writes 2,256 bytes one at a time and the fold
    takes them one at a time, some 500 host instructions a sample in the harness alone, which B0 does not have and no
    user runs; counted on harness runs it would stand in W1's figure as a cost of the wiring. W1 reports the fold's own
    cost apart (harness runs, tip minus B0, less the count above);
  - **harness runs** (the guest at N = 2000, 2ndmix at N = 40000, 32 samples a tick), for W2, W3 and M3G's count of E8a
    and E8b together, whose bases fold `dsp` as their tips do. W3 also counts hello's exit run, whose bursts may all find
    its ARM7 held.
- **What each reports.** W1: with `aica.NoBatch=yes` and with the default, over B0 (the core's own bench gives 1,750 a
  sample for `RunSample` with no program and its sixteen sends; round 2 estimated some 2,300 for the wiring). W2: over
  B1's `aica.NoBatch=yes`. W3: over B2 (the guest's should be about nothing, it has no held burst; 2ndmix's and hello's
  the work of their held bursts). M3G: W1, W2 and W3 together against B0's default, E8a and E8b together against G's
  merge. G, E8a, E8b and the UI packages count nothing.
- **Speed** (by CPU time; design D3): `tests/perf/perf.py` on the `-DHAS_TA_LLE=OFF` build with the `none` renderer,
  2ndmix for 60 emulated seconds, rounds of base and tip in turn on a quiet machine — five in W1 (both settings, with
  W1's `--config`), ten in W2, W3 and M3G — given as the ratio of the tip's median to the base's median of the same
  session (an absolute figure by CPU time moves with the machine's load: M2G's base rows spread by 8.8 %), and beside it
  the figure that ratio makes of M2G's (1459.1 % on the JIT, 566.1 % on the interpreter), for the record. W1 and M3G
  against B0's default (the batch path, what users run before the round) and B0 with `aica.NoBatch=yes`; W2 against B1
  in both settings and against B0's default; W3 against B2. By the readers' estimate the default path becomes about 11
  to 14 % slower on the JIT and 5 to 7 % on the interpreter (`wiring.md` 5, `guest-gate.md` 9.3), and W3 adds the work
  of the held bursts (none on the guest; the start of 2ndmix; every burst of a guest that never releases its ARM7).
- A game's reverb adds the program's work (the core's bench: 6,350 instructions a sample for a reverb of 96 steps); today's
  default runs no DSP at all. That cost is reported, never bounded.

---

## 6. The guest's DSP program

### 6.1 What it is

A short DSP program (16 to 32 steps from step 0, step 127 and the steps behind the program empty) that the SH4 of the
sounding guest loads while it holds the ARM7 in `_start`, between the first AICA tick and the second, on both engines; it
reads the send of the first channel, keeps a counter and a delay line in the ring buffer of wave memory, and writes two
EFREG outputs that are heard. It is written once, in the playground's syntax, and the same text is what the emulator's
disassembly prints and what the playground assembles (M3-D18).

### 6.2 How it is built and committed

- **The source**: `tests/debugger/guest/dsptest/dsptest_dsp.txt`, lines of the playground's assembler: `COEF[i] = <v>`
  (the raw 16-bit register value), `MADRS[i] = <v>`, `MEMS_H[i] = <v>` where the program needs a constant in MEMS (the low
  byte of MEMS is not the CPU's to write; TEMP starts at 0), and `MPRO[n] = <fields>`, **each MPRO line exactly as the
  emulator's `DisasmDsp` prints the four words it assembles to** (zero fields left out, one-bit fields by name).
- **The step of `gen.sh`**: an awk script (`gen.sh` uses awk already, for the labels) that encodes the fields into words by
  the layout of `aica_dsp_core.h` (the same in all three assemblers, `guest-gate.md` 4.1) and writes
  `dsptest_dsp.inc`: the block `g_dsp` of 2.4 (COEF, MADRS, MPRO as the register area holds them, one `.long` a word) and
  the `MEMS_H` lines as (address, value) entries of the table. No new tool: `gen.sh` still needs only binutils and awk.
- **In `dsptest.s`**: the block behind `g_arm7_image_end` (so that nothing in front of it moves but what `_start`'s
  longer code moves, and `g_irq` stays followed by `g_table`, which a selftest relies on); a copy loop in `_start`,
  behind the table, that writes the block to `0xA0703000`, `0xA0703200` and `0xA0703400` with long writes; the short
  entries at the front of `g_table_dsp`: the generated `MEMS_H` entries, then by hand RBP / RBL (`0xA0702804`), EFSDL /
  EFPAN of the two EFREGs written (`0xA0702000 + 4 k`), ISEL / IMXL of the first channel (`0xA0700020`); **at least
  three entries of `g_table_dsp` left empty at its end**.
- **Committed**: `dsptest_dsp.txt` and `dsptest_dsp.inc` join `SHA256SUMS` and `MANIFEST` (eight files then); `gen.sh
  --check` rebuilds and compares all of them byte for byte; the README gains the DSP part.
- **Checked**: `debugger.disasm.dsptest` (3.7 item 4) holds the text, the ELF's words and the emulator's disassembly
  together; F4a's S21 compares the words of the ELF with `state.getDisassembly` of the running emulator.

### 6.3 What it exercises, and its rules

Each of these is in the program, and the report of G shows each on the emulator's disassembly:

- XSEL both ways; YSEL 0 to 3 (FRC_REG, COEF, Y_REG[23:11], Y_REG[15:4]); BSEL both ways, ZERO, NEGB; SHIFT 0 to 3, with
  a value that saturates under SHIFT 0 and one that is cut under SHIFT 2; TWT / TRA relative to `MDEC_CT`;
- IRA from MEMS, from MIXS (`0x20 + b`, the bus of the first channel's ISEL, which the table sets with an IMXL above 0)
  and from EXTS (0x30: 0 with no disc; the decode only);
- YRL, FRCL in both forms (SHIFT 3 and not), ADRL in both forms (from INPUTS and from SHIFTED with SHIFT 3);
- MWT with and without NOFL, TABLE 0 (in the ring, moving with `MDEC_CT`) and one TABLE 1 access; MRD with NOFL and
  without, each followed by an IWT two steps behind it in an odd step; an address with ADREB and one with NXADR;
- two EWT: one of a fold that does not depend on MIXS (the counter, the delay line, MEMS), one of the MIXS path; both
  heard through EFSDL at a moderate level, so that `clip16` never hides them;
- the nine datapath registers each take more than one value over one sample and from sample to sample (`dsp.regs.move`
  of E8a relies on it).

Its rules (`guest-gate.md` 3.2): every memory step is an odd step below 100, away from the slots of the channels the guest
plays (channel K takes the slot of step `2K + 114 mod 128`); every IWT is two steps behind an odd MRD (a word that a CPU
or a channel read in between cannot change, so the latch's unwired inputs do not matter); no test mode (bit 15 of
0x2804), no 8 MB, no value read from EXTS relied on, no CPU write of MIXS or TEMP while the DSP runs; the ring away from
what the ARM7 and the channels use (RBP so that the ring starts at or above `0x20000` of wave memory, RBL 0); the ARM7
program untouched; nothing that ends this emulator (no byte write to SCIPD, SCIRE, MCIPD or MCIRE, no read of 0x2810 with
AFSET).

### 6.4 What later packages rely on

The labels of 2.4; the program loaded before the second tick on both engines; a loaded step behind any `STEP` the runner
meets that is not 127 (S21 takes the first loaded step above the current one, else the first); step 127 empty; at least
one loaded step with each of MRD, IWT, MWT, EWT, YRL, FRCL and ADRL; the properties of `M2-ROUND.md` 5.2 kept (the SH4
loop back by `bra`, no store into a page of code, FPSCR as reios leaves it, `g_irq` on its bank, word accesses to the
AICA).

### 6.5 The oracle copy and the schedule

- **The copy** (the mode `oracle` of `soundlines.py`, built in the work directory as `release` builds its copies): one
  table entry in the free end of `g_table_dsp` sets IMXL 0 for the first channel, its ISEL as the guest's table sets it;
  G shows from the code that the emulator's send of a channel with IMXL 0 is exactly 0 (`SendLevel[0]` is 255, so the
  send's attenuation is capped at `max_att` and the factor it reads is `tl_lut[255]`, which is 0: `hw/aica/sgc_if.cpp`,
  `UpdateAtts`, `ChannelEx::Step`), so that every send is 0 and every bus the program reads holds 0 in the emulator and
  in the model. There is no other copy: a channel pointed at a bus the program does not read would put that bus's
  non-zero MIXS into the folded blob, which the tool cannot reproduce.
- **The schedule** the tool follows, and the emulator has on both engines, which G shows (a scratch build that prints the
  core's `samples` at every tick, of which nothing is committed): every tick makes 32 samples, whatever the ARM7 does
  (W3's clock, M3-D7); tick 1 makes them on the empty program from the state after reset; the guest's DSP writes all
  fall between tick 1 and tick 2, in `_start`'s set-up (while the guest holds its ARM7, which no longer matters to the
  DSP); every later tick runs the program. The tool
  applies the writes in the order `_start` makes them (the table, then the block) through `AicaDsp::RegWrite` for the
  offsets the core owns, with EXTS 0, no fetch and no bus read, and **with the sweep the emulator makes**: in every
  sample `SweepBegin()`, then a send of 0 (`MixsSend(bus, 0)` on the core, the `LockstepEvents` sends for the model) to
  every bus that a channel's ISEL names — before the guest's table bus 0 for all 64 channels; behind it bus 0 and the
  first channel's ISEL as the table writes it (neither the ARM7 program nor `g_irq` writes an ISEL) — and then the
  sample. The core's blob carries the buses a sweep sent to (`mixs_sent`), and `MixsSend` marks a bus whatever the value,
  while the core's own `SweepBegin` at step 0 of a sample with no sweep clears them: a tool without these sends folds
  another `dsp` from tick 1 on. The tool models no ARM7 and no clock gate: the schedule rests on the set-up's writes
  falling between the first tick and the second on both engines. With the AICA clocked in every burst, a set-up that
  crossed a tick would run the DSP on a part of the program on one engine and not the other; G measures where its
  longer `_start` ends, on both engines, before it takes its baseline (7.3).

---

## 7. Decisions, questions, risks

### 7.1 Decisions of this cut

Each: what is decided, and the alternative that was not taken. "Design" is `TRACK1-DESIGN.md`; section 8 has the text
changes that follow.

**The round**

- **M3-D1. This file is the contract until M3G.** Where it and the design differ, it wins, and a design section a brief
  names is read with its rows of section 8. *Rejected:* editing the design before the round — it would be edited again
  by every finding.
- **M3-D2. The wiring is two packages, W1 and W2.** W1 replaces the DSP with the batch path still there, so that its
  gate is "every line, in both settings, as it was" (B0); W2 removes the batch path, so that its gate is "every default
  line equal to the per-sample line before it" (B1). *Rejected:* one package — a moved default line could then be the
  batch path's removal or a defect of the core's wiring, and nothing would tell them apart; *also rejected:* the wiring
  inside E8 (design D2: M3 starts after it).
- **M3-D3. Header revision 7 comes first** (R7, before W1). W1 needs `DspDebugState::rbpl` once a CPU's read of 0x2804
  gives 0, and the wiring would leave frozen comments false (`dsp.h`'s "outside Step()", the ARM7 block's batch render,
  `NOTICE_DSP_FORCED`'s "goes with the path without the DSP") for most of the round. *Rejected:* the revision with E8
  (`wiring.md` 9 item 18) — W1 would change a frozen declaration itself.
- **M3-D4. Way A for `dbg_core.cpp`, as in M2** (`M2-ROUND.md` M2-D16): R7 hands the DSP's breakpoints to `dbg::emu`
  against stubs, the readers of `stepping` already ask `emu::TargetSteps`, and E8a flips it by the body in its own
  `dbg_emu.cpp`; E8b owns the core afterwards. *Rejected:* E8a in the core — two packages in one file one after the other
  for forty lines.

**The wiring**

- **M3-D5. The register file is a mirror of the CPUs' writes; the DSP's registers are the core's.** A write stores its
  bytes in the file as today, then the core gets the file's 16-bit word of the slot (low halves of the owned slots only;
  a byte write merges with the file's other byte, which on the console is unmeasured; size 4 is size 2 as today). A read
  of an owned slot is the core's (`RegRead`: COEF bits 2:0 read 0, both halves of TEMP and MEMS live, MIXS of the DSP's
  bank, EFREG as written or computed, 0x2804 and EXTS 0, the upper half of a slot 0); every other offset is the file's.
  The DSP never writes the file, so `regs` keeps meaning "the register file as the CPUs wrote it", and for a guest that
  writes no DSP register it is what it was. *Rejected:* reads from the file (two DSPs for the guest: the one it reads and
  the one that runs); the file as a mirror of the core (moves `regs` for every guest that wrote a DSP register; the core's
  values are not bytes of a file).
- **M3-D6. `DebugReadRegs` gives what a CPU read gives**, the DSP's slots from the core; the rows `RBP`, `RBL` of
  `dc.aica.dsp` and `RBP`, `RBL`, `TESTB0` of `dc.aica.common` read the DSP's `RBPL` (R7 moves their source, W1 makes it
  the core's). *Rejected:* `DebugReadRegs` from the file at 0x2804 (it would no longer be "as guest reads deliver them",
  its frozen contract); the rows reading 0, or `"n/a"` as write-only rows do (the value exists in the DSP).
- **M3-D7. The AICA is clocked whatever ARMRST says; ARMRST holds the ARM7 alone** (the owner's answer to M3-Q1, "Fix
  it in M3"; package W3). Every burst makes its 32 samples — the timers, the sound generator's sweep, the DSP phase, the
  mix, the interrupt lines of both CPUs —, and only the ARM phase is skipped while the ARM7 is held (`arm_on`, latched at
  the start of the burst as today). The DSP's steps and breakpoints and the capture work whatever the ARM7 does: `-32012`
  is the ARM7's alone (`control.step` / `stepOver` / `stepOut {arm7}`, "arm7 held in reset"), and revision 7 has no
  declaration for a DSP or a capture that is not clocked (no `dbg_dsp_OnBurstUnclocked`, no `dbg::dsp::StepCheck`, no
  `kErrNotClocked` from `dbg::sgc::Arm`): E8a and E8b, which build them, come behind W3. W3 changes when the time step
  runs, not what it computes, and releases and holds the ARM7 as the tree does today. The console does the same (3.12,
  J4); where nothing was measured (the phase of a release against the sample edge, a request latched while the ARM7 is
  held) the answer is the rule. *Rejected:* round 2's rule, no sample while the ARM7 is held (its recommendation, which
  the owner declined: a guest that holds its ARM7 silent, the DSP and the capture unusable then); clocking the samples
  and the DSP but not the timers and the interrupts (the owner's word names them, and caique measured the timers and the
  SH4's line with the ARM7 held); the clock inside W1 or W2 (their gates are "identical", M3-D2); the clock in E8a (its
  gate is "identical" too, and its run control would first be built for an unclocked burst).
- **M3-D8. The sweep sends sums**: `SweepBegin()`, then for each bus that a channel's ISEL names one `MixsSend` of the sum
  of the sends `oDsp << 4`; then `RunSample()` without inputs. The sum is the same number as the sends one by one (the
  20-bit bus wraps), `<< 4` is the console's scale (MIXS holds the sample times 16), and with no inputs held the debugger's
  `StepOne()` needs nothing of the sample across a suspension. *Rejected:* `AicaDspSampleInputs` (204 bytes of the sample
  to keep across a suspension); 64 calls a sample into another translation unit; the sends without `SweepBegin` (read a
  sample early, `aica_dsp_core.h`, `MixsSend`).
- **M3-D9. W1 cuts the sample into its phases** (2.4: `SampleTimers`, `SampleSweep`, `DSP::RunPhase`, `SampleMix`,
  `SampleIrq`; `TimeStep()` is their sequence), with today's order inside a sample, and keeps the direct mix in the sound
  generator between sweep and mix. E8a only calls them from the burst. *Rejected:* E8a cutting `AICA_Sample` (it would
  rewrite W1's functions behind the baseline W1 took, `debugger.md` 10 Q1).
- **M3-D10. `ChannelFetch`, `BusRead` and `MixsSendLate` are not called in `fast`.** The fast sound generator reads wave
  memory through the channel's address and keeps no fetched word, and the CPUs' wave-memory reads go through the fast
  memory map: a hook would cost on the hot path. Known limits: a DSP access in a playing channel's slot is not lost, and
  the latch holds only the DSP's own reads (the console's `dsp_coll`, one line of `dsp_mem3`, `sh4_latch`, `arm_latch`).
  The `cycle-similar` AICA of track 5 has caique's sound generator and calls them. *Rejected:* an approximation of PCM
  fetches (`wiring.md` 9 item 7) — a model that is neither caique's nor today's.
- **M3-D11. The channels stay as they are**: their `pow()` tables, no filter, the send law of `DSPAtt`. *Rejected:*
  anything of track 5's fast fixes in this round.
- **M3-D12. The sound line gains `dsp`**, the FNV-1a of the core's state blob folded at every tick (2.4); the state hash
  of an exit line gains nothing. The blob is versioned and the same on every host. With it every later neutrality target
  sees the whole DSP, and the oracle of the guest compares state, not sound. *Rejected:* no witness of the DSP (a debugger
  that changed a datapath register no output keeps would pass, `guest-gate.md` 0 item 5); a part of the state hash (it
  would move the combined hash of every workload in every comparison across the round).
- **M3-D13. Save states: V5 at W1** (the DSP's part is the core's blob, `StateSize()` bytes, in place of about 33 KB of
  `dsp_context_t`, 32 KB of it the arm64 recompiler's code with absolute pointers), **V6 at W2** (`mxlr`, `samples_gen`
  out). A blob the core refuses — which a V5 file this build wrote never holds — resets the core with one log line, and the
  load goes on. *Rejected:* ending the process (a state is the user's file); one bump for both (W1's format changes either
  way).
- **M3-D14. The arm64 DSP recompiler is deleted, not ported.** One DSP everywhere is design D1 and D2 ("what you debug is
  what runs"); it differs from the interpreter today (MEMVAL across samples), writes RWX pages into a data array (against
  design D7; refused on macOS arm64 without `MAP_JIT`, by reading), prints a disassembly at every recompile, refuses wave
  memory other than 2 or 8 MB, and puts 32 KB of pointers into every save state. On arm64 a program then costs the core's
  work (an estimated twice the recompiler's). Read, not run: no arm64 host. *Rejected:* porting it to the core's semantics
  (a second DSP to hold to the first).
- **M3-D15. Speed is measured and reported, and gates nothing** (the owner's answer to M3-Q2, "don't mind perf for
  now"; 5.4). W1, W2 and W3, which change a path every run takes, count host instructions a sample and measure
  `tests/perf`; M3G measures the round; nobody bounds them, and no package changes the core or anything else for speed
  (round 2's lever, an empty-program path in the core, is not started; `dsp_core/` is no package's). The method stays
  fixed: a count against B0 on runs the harness does not fold (B0's exit runs of design 5.7), a count against a base
  with the `dsp` field on harness runs; speed as a ratio of medians of base and tip in turn in one session. *Rejected:*
  round 2's ceilings (2,300 host instructions a sample for W1, 48 for every later package) and its floor (1150 % and
  500 % of real time, the recommendation the owner declined); a ceiling kept as a warning (a number that stops nothing
  is a report, and 5.4 has the report); dropping the measurement (the numbers of the round would not compare with M2G's,
  nor with a round that takes speed up); W1 counted on harness runs (the new field's fold, some 500 a sample, would
  stand in the wiring's figure as a cost no user has); a scratch build with the fold compiled out (it works, but needs
  a build per count that the exit runs B0 already has do not).
- **M3-D16. "Disable Sound" mutes** (W2): the sample is made and zero pairs go to the audio stream, which keeps its pace;
  the harness folds what goes to the stream. Today the default (batch) ignores the setting and the per-sample path stops
  writing to the stream, which would run the emulator unthrottled once it is the only path. *Rejected:* the per-sample
  path's behaviour as it is.
- **M3-D17. The sound targets lose `.persample`** when the render key leaves the runner (W2): `debugger.sound.lines.
  dsptest.jit`, `debugger.sound.neutral.idlebp.interp`, ... *Rejected:* keeping a suffix that names a choice that no
  longer exists.

**The guest**

- **M3-D18. The guest's DSP program is its own package, G, between W3 and E8a**, written in the playground's syntax and
  assembled by an awk step of `gen.sh` into a block copied by a loop of `_start` (section 6). Its lines move, and the
  caique model justifies them (J3); E8a and E8b are then held to lines that have the program, as E6b was held to E6a's.
  *Rejected:* the program inside E8 (its gate would compare against a baseline it moved itself); in W1 or W2 (the old DSP,
  still in the tree before W1, ends the emulator on its NOFL; W2's gate is "identical"); GNU as macros (a second text form
  beside the one the emulator prints); caique's C encoder (a new tool for `gen.sh` and caique code in the guest's
  toolchain); the reserved part of the table alone (40 entries; one step takes four, and `release` uses its last three).

**The debugger of the DSP**

- **M3-D19. E8 is E8a (run control of the DSP) and E8b (the capture and the binary path).** They meet in `dbg_emu.cpp` and
  the burst; E8a builds the end of a sample as a stop and the call site of the capture's frame against the stub, E8b gives
  the capture its body. *Rejected:* one package (two verifications' worth of tests, and the capture's stop at the end of a
  sample would have no owner of its own); the capture first (it would build the end of a sample without the DSP's step,
  which is its main user).
- **M3-D20. A step of the DSP executes one step and never publishes `running`** (the ARM7's rule, `M2-ROUND.md` M2-D5).
  With a burst suspended it runs on in the park context; with none, the SH4 runs to the next burst and the step is step 0
  of its first sample; from step 127 it is complete at the end of the sample, behind the sample's mix and interrupt update,
  with `STEP` 0 (H7-1, 2.3). *Rejected:* design 2.5.4's "resumes with a one-shot break before the next DSP step" — a
  resume publishes `running` and a break in front of the next step executes nothing, so S21 ("`STEP` advanced by one, no
  running tick") would fail every correct emulator; *also rejected:* a step from 127 complete in front of step 0 of the next
  sample (the ARM phase of that sample would run as part of a step of the DSP, and behind the last sample of a burst the
  SH4 would run a tick for it).
- **M3-D21. The end of a sample is a place to stop**: `SOUND_BURST_SAMPLE`, in front of the ARM phase of the next sample of
  the burst, nothing of which has executed; behind the last sample the burst ends and the stop is latched behind its tick
  or published by the park loop, as every sound stop. A step of the DSP from 127 and a capture armed at a stop end there.
  `SOUND_BURST_DSP` is the suspension in front of a DSP step. *Rejected:* the end of a sample as an ARM7 suspension
  (`SOUND_BURST_ARM7` with no ticks used): the same state of the machine, but the ARM7's fallback notice (M3-D24) and the
  selftests have to tell who stopped there.
- **M3-D22. The DSP has a resume exemption, decided at every leave that can let it execute** (a resume, a step of the SH4,
  every kind of ARM7 step), armed only behind a stop the DSP decided and with the DSP still in front of that step, used by
  the first check only. A step of the DSP decides the ARM7's exemption first. An active DSP breakpoint is reached in every
  sample: a resume from it runs one sample, and an SH4 step from such a stop is dropped when the run-on meets it again
  (design 2.5.2); the UI's help says to disable or mute it (F4d). *Rejected:* an exemption that lasts a sample (it would
  hide the hit of the next sample); no exemption for ARM7 steps (an ARM7 step from a DSP stop would be dropped at once,
  `debugger.md` 5.3).
- **M3-D23. The DSP's counts are a second accessor, `SoundBurstDspCounts(u32 out[2])`** (2.4). *Rejected:* a larger array
  for `SoundBurstCounts` (a caller with an array of four would be written past its end).
- **M3-D24. `NOTICE_ARM7_FALLBACK` follows an ARM7 suspension only**: a burst suspended in front of a DSP step or at the
  end of a sample has no suspended ARM phase, and its run-on uses the debug loop of the ARM7 only if `dbg_arm7_armed` says
  so (`debugger.md` Q18). Read, not run (arm64). *Rejected:* the notice behind any suspended burst, as the code has it
  (it would say the ARM7 ran on its fallback where it did not).
- **M3-D25. `dc.aica.dsp` gains nine read-only registers**: `ACC` (26), `Y_REG` (24), `FRC_REG` (13), `ADRS_REG` (12),
  `LATCH` (16), `LATCH_COPY` (16), `MEMVAL` (16), `NOFL_PIPE` (2), `MIXS_BANK` (1), behind `STEP`, `MDEC_CT`, `RBP`, `RBL`,
  from `DspDebugState`; signed values on the wire as their bits under the width (`FormatValue`). Their widths are normative
  for every server that lists one of them; under the M3 gate S01 requires all nine (M2's tree has 2,940 registers, M3's
  2,949). They are what "Load from emulator" needs to continue the DSP exactly. *Rejected:* the design's five (the latch's
  copy, MEMVAL, the NOFL history and the bank are state a continuation needs); writable rows (with the device write table,
  M3-Q3: a written `STEP` would also have to end the exemption); the step view's operands on the wire (no wire form, and
  nothing of M3 needs it).
- **M3-D26. `dbg_dsp_armed` follows activity** (the ARM7's rule, `M2-ROUND.md` M2-D7): a listed breakpoint arms the debug
  loop only while the category `dsp` is active. A DSP stop carries no notice (one DSP: the debug loop computes what
  `RunSample` computes) and the wire has no DSP callstack. *Rejected:* armed while enabled (a muted breakpoint would keep
  the debug loop, six times the cost of a sample, `dsp_core/README.md`).

**The capture and the binary path**

- **M3-D27. The capture's rules.** A frame is taken behind the sweep of its sample (the channels' registers and state after
  their step, their outputs of that sample). Bytes 76 to 83 are 0 (not computed by this sound generator); 84, 86, 88 the
  channel's outputs, kept by the sound generator only while a capture records; flags bit 0 is `AicaChannelDebug::enabled`
  (keyed on and not run to its end). A capture records into a buffer of its own and replaces the server's when complete;
  until then `getSgcFrameData` gives the capture before it (or `-32011`). A second `recordSgcFrames` while one records is
  `-32003` (an action armed already, design 3.5) and the first goes on. Stops, steps and pauses during a capture keep it
  recording (a step's samples are frames). A guest reset or `control.reset` cancels it and answers `-32014` at once; a host
  stop or a guest exit cancels it and answers `-32001` at once, also a capture that a `CC_TABLE` call taken by the exiting
  thread's drain armed (the cancel stands behind the drain, 2.3). Frames come whatever the ARM7 does (M3-D7); a capture
  whose machine does not run (a stop that is not resumed) is answered by the deadline. The deadline is 5 s of host time
  from the request (`kCaptureTimeoutMs`): `-32014`, and the capture is cancelled. A
  capture's first frame is the first sample whose sweep comes behind the arm: armed with a burst suspended in front of
  a DSP step, that is the next sample. A capture armed at a stop with a burst suspended may complete in the rest of that
  burst, its last sample included; its stop is then published with no `running` tick in between (the ARM7's step-over
  has the same case, `M2-ROUND.md` M2-D9), which needs the run-on of the park loop to decide a stop recorded in a burst
  that ran to its end (3.8 item 5). `Capture` ranks below every other cause: a stop that meets it wins and the response
  is sent all the same. **The response and the capture's stop reach a client in either order** (design 3.3: "its order
  against that paused tick is not specified"; 2.9, "Order"): (E) publishes the stop through the listener, (C) sends the
  response when it takes the notice, and neither waits for the other; the runner (S17) and the UI accept both, and the
  emulator is not held to one. *Rejected:* replacing a pending capture by
  the second request (an answer to the first that is neither done nor a timeout); `-32014` for a reset only at the
  deadline (a client waits 5 s for nothing); refusing captures shorter than the rest of a suspended burst.
- **M3-D28. The response of `recordSgcFrames` is deferred in the core, and a notice answers it**: the method checks,
  makes its call (`CC_EXEC` at a stop, arming the capture and the resume; `CC_TABLE` while running) and, when `Arm`
  returned 0, keeps the pending record `{client, request id, deadline}` before (C) takes its next message; the
  completion notice of `dbg::sgc` (H7-4) queues a message for (C). The rules that make a notice, which names no capture,
  answer the right request:
  - **A notice answers the pending record, if one exists, and clears it**; one that finds none answers nothing. The
    core counts the notices it is owed (one for every `Arm` that returned 0) and **answers a new `recordSgcFrames`
    `-32003` while one is owed**, also when no capture records any more (its notice is queued).
  - **The core does not answer a capture it armed**: at the deadline it calls `sgc::Cancel(kErrCaptureTimeout)` in a
    `CC_TABLE` call, and the notice answers (`-32014`, or the result when the capture completed in front of the call).
    Only when that call is not taken (`kErrBusy`) does (C) answer `-32014` itself and clear the record; the notice stays
    owed (a request is `-32003` until it has come, and it then answers nothing), and the cancel is made again 250 ms
    later while it is owed (as the grace expiry's calls are): no other capture can be armed meanwhile, so it cannot
    cancel one.
  - **A refused resume**: in the `CC_EXEC` closure, `sgc::Arm(frames, true)` and then `emu::Resume()`; when `Resume`
    refuses, `sgc::Cancel(kErrNotPaused)` (H7-4), and the notice answers `-32003`. `Arm` behind `Resume` is not
    possible: a refused `Arm` would leave a resume armed.
  - **No emulator thread**: a `CC_TABLE` closure that finds `!emu::OnEmuThread()` (`emu::Call` ran it on (C) under L:
    the thread exited in between) arms nothing, and the method answers by the lifetime then (`-32001` Stopped, `-32002`
    NoMachine); `Arm` is never called without a thread (H7-4). A `CC_TABLE` closure that the exiting thread runs in the
    drain of `OnThreadExiting` is on (E) and may arm: `OnThreadExiting` cancels behind its drain (2.3), and the notice
    answers the request `-32001`. These are the only two ways a closure meets a thread that goes away: a call (C)
    makes after the drain has set Stopped finds the lifetime Stopped and runs on (C) under L, behind the exiting
    thread's L.
  The deadline joins the waits of the core's loop, beside its other timers. A selftest request (client 0) gets the
  response through a test hook. *Rejected:* (C) waiting inside the method (it serves every client and owns the tick);
  (C) answering at the deadline and cancelling behind (a completion between the two, or a retried cancel, answers or
  cancels the next request: Appendix R, S10); a capture id in the notice (a declaration change of the
  frozen `dbg_sgc.h` for what one counter does).
- **M3-D29. The transport counts text and binary apart** (16 MiB of text, 64 MiB of binary messages; 2.3's `dbg_net.h`
  text). A capture of 4096 frames is 32 MiB and a memory read of the bound 16 MiB, each with a 16-byte header; under one
  bound of 16 MiB the next broadcast tick behind such a frame cuts the client off (`debugger.md` 6.5). *Rejected:* lower
  protocol bounds (2047 frames, 16 MiB less 16 bytes: a frame at the bound still leaves no room for the next tick); a
  larger single bound (the slow-client rule would take 64 MiB of ticks to act); binary frames in pieces (a protocol change).
- **M3-D30. Texts.** `-32012` keeps its one text, `arm7 held in reset`: only the ARM7 answers it (M3-D7); `-32004` of
  `stepOver` / `stepOut` on the DSP reads `not supported: step over and step out of target dsp`. Texts are not normative
  (decision 4). *Rejected:* round 2's second text of `-32012`, `aica not clocked (arm7 held in reset)`, for the DSP and
  the capture (nothing answers it once the AICA is clocked whatever ARMRST says); "on this backend" (it suggests another
  backend could step over on the DSP).
**The contract and the UI**

- **M3-D31. F4a is the first package; the mock is held to the emulator's M3.** Its AICA is clocked whatever its ARM7
  does (its DSP steps, its breakpoints are reached and its captures record while its ARM7 is held; only its ARM7 answers
  `-32012`: M3-D7), its DSP program is one thing (register file, disassembly bytes and text agree), its sound clock is the
  emulator's (an AICA period of 145124 cycles and 32 samples, a DSP breakpoint reached in every sample, a capture as
  long in `SH4_CYCLES` as the emulator's), a read of 16 MiB is answered within 2 s, and `dc.aica.common` has the three
  rows of 0x2804. **The mock the runner starts with `--dsp-program` has `arm7Held` and `startPaused`**: its stop at the
  start (reason `start`) is its anchor, as S00's stop is the emulator's, and its ARM7 is released only after
  `MOCK_ARM7_RELEASE_STEPS` emulation steps of running, so S17 and S21 meet the held ARM7 however long the session takes
  to open. *Rejected:* the runner behind the emulator packages (their gates run it); `arm7Held` alone (the runner's
  `listenMockDebugger` starts the mock's clock at listen, which releases the ARM7 some 128 ms later, a race with the
  session's opening that S17's and S21's required notes lose under load); a mock whose DSP and capture stand still
  while its ARM7 is held (round 2's rule, which the owner's answer to M3-Q1 retired: it would let a runner rule pass
  that the emulator from W3 on fails); the mock's wall-clock capture and its DSP of one step per emulation step (S17's
  length rule and S21's windows fail or are vacuous on it).
- **M3-D32. The runner's M3 rules** (3.1 item 3): S17 and S21 make their first act at their first pause and, when
  `ENABLED` reads 0 there, assert that it is accepted and works — a capture that completes, a DSP step — and note it
  (M3-D7, M3-D44: on the guest the bursts that make them are clocked); S21 then waits for the program (M3-D44);
  `--dsp-program` selects S00, S17 and S21, and S17 there requires a sounding channel; S10's DSP text is the
  playground's line of its bytes; S21 compares the program with the ELF's `g_dsp_mpro`; under M3, S01 requires
  `dsp.stepping` and the nine rows. Scenarios run in the order of `SCENARIOS`, so in `--dsp-program` S17 comes right
  behind the anchor, and its capture lets the guest run and release its ARM7 before S21: **run 2 of the M3 gate on the
  guest is two runs**, S00, S17, S21 with the note `sgc: captured with the arm7 held` required
  (`debugger.contract.m3dsp.*`, E8b) and `--only S00,S21` with the note `dsp: stepped with the arm7 held` required
  (`debugger.contract.m3dsp21.*`, E8a; they stay behind E8b);
  `contract.py` accepts `--require-note` in its modes `dsp` and `m3dsp`. The capture's second request and the read of
  the capture before are made with the first capture held open at a DSP breakpoint's stop, never raced against its end;
  its response and its stop with reason `capture` are accepted in either order (M3-D27).
  M3G also reruns the M2 gate's second run on the changed guest. *Rejected:* S21 as built (it fails every correct
  emulator on the guest: the anchor stop has no program yet); S17 on 2ndmix alone (its records are
  silent, and a capture of zeros would pass); one run 2 with S21 in front of S17 (`SCENARIOS` is one order for every
  run, and S16 and S17 are in front of S21 in it).
- **M3-D33. The forcing names go in R7** (`NOTICE_DSP_FORCED`, `EV_DEBUGGER_DSP_FORCED`) **and F4a** (`"dsp-forced"`): no
  build ever set them (design 2.5.5). *Rejected:* removing them with E8a or W2 (a frozen comment would be false in
  between: `NOTICE_DSP_FORCED` "goes with" the path without the DSP, which W2 removes, and the revision of the round is
  R7).
- **M3-D34. The playground's module is committed in the UI** (`src/wasm/aica-dsp-core/`), regenerated by `npm run
  build:dspsim -- --emulator <tree>` over the emulator's `build.sh --check`, with the emscripten version pinned in a
  provenance file of fixed keys (3.4 item 1: the emulator's head and the last commit of `dsp_core`, `emcc -dumpversion`
  of the `emcc` that `build.sh` uses) and refused otherwise, a hand-written declaration file, and a notice with caique's
  MIT text; `aicaDsp.ts` type-checks in every TypeScript project that includes it (no Vite suffix import); **the
  emulator holds it** (W2's `aica.dsp.wasm.devtools` replays the native trace on the pinned module). The inputs are at the
  console's scale (`sample << 4`), so that a program written once sounds the same in the playground and in the emulator.
  `dspUtils.ts` keeps its path and four exports. The share link stays, with a line of help (it opens the original online
  playground, whose DSP is not this one). `mock:dev` no longer builds the module. *Rejected:* the module served by the
  emulator alone (the UI's tests and the standalone tool route would have no DSP); the C++ copied into the UI and built by
  Vite (every UI build would need emcc); an npm package (infrastructure for 44 KB); the playground's old scale (a program's
  output would be 16 times quieter than in the emulator).
- **M3-D35. "Load from emulator" works at a stop, in a workspace's dock panel**: one ARM7 read of `0x00808000` bytes from 0
  (wave memory and the register file of one moment) and the rows of the paused tick of the same stop; wave memory taken as
  2 MiB. **It continues the emulator exactly from a stop in the DSP phase behind step 0** (`STEP` 1 to 127, a burst
  suspended in front of a DSP step), up to the end of that sample: step 0 changes the MIXS banks, and at any stop with
  `STEP` 0 the bank the DSP reads next is the sweep's fill bank, which neither a CPU read nor `MIXS_BANK` shows; nor
  has the core a register for whether the sample's sweep was made. EXTS has no row and reads 0 for a CPU: it is loaded
  as 0. The banner says so wherever it applies. The tool route keeps no connection (an e2e test pins it). *Rejected:*
  while running (two reads of two moments); the tool page connecting on demand (a reconnect lifecycle on a page that
  has none); the core's state blob over the wire (a protocol method, and the one way to carry the fill bank; for M3 the
  rows give the state of a stop inside the DSP phase, which is where a user debugging a DSP program stands).
- **M3-D36. `state.getMemoryBinary` is `state.getMemorySlice` with another bound and a binary payload**: a read (in the
  slice hook while running), no wrap above `addressMax`, unreadable bytes 0 and listed, `-32010` when nothing is readable,
  `-32004` for `dsp`, no `validity`; `binaryId` per connection from 1, one counter for kinds 1 and 2. *Rejected:* a
  `validity` field (always `ok` without address translation, design 1.1); a counter per kind (the client matches by
  `binaryId` alone, design 3.4).
- **M3-D37. The `dsp` target keeps no memory** (`-32004` for its memory methods): the DSP's registers are read at their
  addresses in the register file, through `sh4` or `arm7`. *Rejected:* a memory of the `dsp` target that maps them
  (a second view of the same registers, with a second address rule).

**Added by the review of the cut (Appendix R)**

- **M3-D38. The state-hash workloads of the round are 2ndmix 600 and 4000 vblanks and hello 300**, with the guest at 90
  vblanks; design 5.7's "the BIOS boot" is not run (the owner's answer to M3-Q4: not in M3). `tests/system` holds no
  such case, no data path of the tests project names a BIOS file, and no package since E5 has run one (E5's and J1's
  comparisons were 2ndmix and hello). A workload that loads a DSP program is found by B0's scratch trace instead of
  being assumed away (3.3 item 1). *Rejected:* defining a BIOS run inside the round (a BIOS file is not the project's to
  name, and the owner keeps it out of M3); leaving the words in the gates (a gate that names a run nobody can make is
  not checked).
- **M3-D39. `aica-dsp-asm/` is ported, not deleted**: the standalone assembler at the repository's root includes
  `dsp_backend.h` and compiles `dsp_helpers.cpp`, which W1 deletes; W1 moves it to the core's `AicaDspDecode` /
  `AicaDspEncode` (the field layout is the same in every assembler, `guest-gate.md` 4.1), its text and options as they
  are, and builds it once in its gate (no ctest target builds it). *Rejected:* deleting it (a user tool this project
  still maintains: its macOS script was fixed on this branch); leaving it on files that are gone (no gate would see it
  break).
- **M3-D40. The frozen headers are those Appendix B of the design lists, with `dbg_dsp.h` and `dbg_sgc.h` from R7; the
  private headers (`dbg_*_private.h`) are not frozen**: a package edits one only where its brief says so (E8a `enum
  DspReg` of `dbg_regs_private.h` and its new `dbg_dsp_private.h`, E8b `dbg_core_private.h` and its new
  `dbg_sgc_private.h`). *Rejected:* "the headers of `libswirl/debugger/`" (wider than Appendix B, and it blocks E8a,
  whose nine rows need nine selectors in a private header).

**Added by round 3 (the owner's answers)**

- **M3-D41. The clock is a package of its own, W3, behind W2 and in front of F4c, G and E8a.** Behind W2: one sample path
  exists only from W2 on, so the clock is made once (in front of it the batch render would need its own rule, and W2's
  gate, "every default line equal to B1's per-sample line", would mix the batch path's removal with the clock); it moves
  lines on purpose, which neither W1's nor W2's gate allows (M3-D2). In front of G: G's program, its README, its oracle's
  schedule (6.5) and its baseline are then made once, against the final clock, and the guest's texts that W3 makes false
  are corrected before G writes its DSP part (3.12 item 3, 3.7 item 1). In front of E8a and E8b: their run control and
  capture are built for a DSP that is clocked in every burst, which is what F4a's rules assert from the round's start, and
  revision 7 declares nothing for an unclocked one (M3-D7). *Rejected:* behind G (W3 would edit G's files, and G's
  schedule would be written for a rule the next package changes); behind E8a or E8b (their unclocked paths would be built
  and then deleted, and F4a's held-ARM7 rules would meet a red emulator); in front of W1 or between W1 and W2 (two
  sample paths to clock, and W2's gate split).
- **M3-D42. W3's gate: identical where no burst finds the ARM7 held; elsewhere the moves are shown, not stored.** The
  guest's bursts never find its ARM7 held (h = 0, counted on B2), so its lines and hash are identical; 2ndmix, whose start
  has held bursts, must move in `samples` (to the silence of 32 × N pairs, from that of 32 × (N − h)) and in `dsp`, and
  may move in `regs`, `arm7`, `sh4` and its hash's parts only by a trace that names the held burst's effect (5.1); the
  rule itself is held by the `held` targets — a held ARM7 and an ARM7 idling on `b .` give the same samples, register
  file, DSP and SH4 — and by `arm7.held.clocked`, and justified by the console's cases (J4). *Rejected:* the caique
  model as the oracle of the moved lines (its sound generator is not the emulator's, and the timers and interrupt lines
  of the fast tier are not caique's: a comparison would show the fast tier's known differences, not W3's change); a
  console check of the emulator's lines (a console capture is no line of this sound generator, 5.2); accepting the moved
  lines without a witness (a stored line justifies nothing: the head of this file).
- **M3-D43. W2 does not pace a burst with the ARM7 held; W3's samples do.** From W3 on a held burst makes its 32 samples
  and hands them to the audio stream like any burst, and the harness folds them. W2's silent pairs (round 2's item 4)
  would be code and a ctest target that W3 deletes one package later. Between W2's merge and W3's, a stretch with the
  ARM7 held is silent and unpaced, as the per-sample path is today: a state of one package's turn, never pushed (M3G
  pushes after the round). *Rejected:* W2's silent pairs (throwaway); the clock inside W2 (M3-D2).
- **M3-D44. S17 and S21 make a capture and a DSP step at the anchor, with the ARM7 held, and S21 waits for the
  program, not for the ARM7.** With the AICA clocked whatever ARMRST says there is nothing to settle: S17's first
  capture and S21's first step are made at their first pause, which under `--dsp-program` is the anchor with the ARM7
  held, and each notes that it worked (`sgc: captured with the arm7 held`, `dsp: stepped with the arm7 held`: asked
  with the ARM7 held); a server that answers `-32012` there fails. **S17 and S21 assert at the anchor that a capture
  and a DSP step are accepted while `ENABLED` reads 0. On the guest the bursts that make them are clocked** (`_start`
  releases the ARM7 in its first slice, in front of the first tick: h = 0, 3.12), **so the clock of a held burst is
  held by `debugger.sound.held.*`, `arm7.held.clocked`, `dsp.bp.held`, `dsp.step.held` and `sgc.held`, not by the
  contract.** The guest loads its DSP program behind the anchor (6.1), so S21 then resumes and pauses until the
  program is loaded (with `--elf`, until the 128 steps' bytes are the ELF's block), at most five times. The two runs
  of the M3 gate's run 2 stay, for the same reason as in round 2: S17's capture lets the guest release its ARM7 before
  S21, whose first pause then finds `ENABLED` 1. *Rejected:* S16's settling kept for S21 (it waits for the ARM7, which
  the DSP no longer needs, and a guest that never releases its ARM7 would fail S21 with a DSP that works); no
  held-ARM7 assertion (nothing in the contract would hold the protocol half of the answer to M3-Q1: that only the
  ARM7's steps answer `-32012`); one run 2 with S21's held step dropped (the `m3dsp21` targets exist anyway: E8a's
  gate needs S21 before E8b's capture exists).

### 7.2 The owner's answers

The cut asked the project owner four questions (round 2); the owner answered them on 2026-10-16, and round 3 of the cut
applies the answers. Nothing is open: no package waits on the owner, and no brief asks a question.

| # | Asked | Answer | What it decided | Where the cut has it |
| --- | --- | --- | --- | --- |
| M3-Q1 | With the batch path gone, may a stretch in which the guest holds the ARM7 stay without samples until track 5 clocks the AICA independently of the ARM7? (Recommended: yes, M3 keeps the per-sample path's gating.) | **"Fix it in M3"** | The AICA is clocked whatever ARMRST says: the sound generator's samples, the DSP, the timers and the interrupts run while the guest holds the ARM7; only the ARM7 stays unclocked (`control.step` / `stepOver` / `stepOut {arm7}` keep `-32012`, "arm7 held in reset"). The cut's "if the answer is no" branch is taken: a package of its own behind W2, W3 | M3-D7, M3-D41 to M3-D44; 3.12; 1.1; 2.1, 2.3, 2.5, 2.6; 3.1, 3.5, 3.7 to 3.11; section 4; 5.1 to 5.3; 6.5; 7.3; section 8 |
| M3-Q2 | Which speed is the floor for the always-on DSP? (Recommended: 1150 % and 500 % of real time on 2ndmix, on the ratio to the round's start.) | **"don't mind perf for now"** | No speed floor, ceiling or threshold anywhere in M3; W1, W2, W3 and M3G measure speed and host instructions by the fixed method of 5.4 and report them; nothing is changed for speed (the empty-program lever is not started) | M3-D15; 5.4; 1.2; 3.3, 3.5, 3.8, 3.9, 3.11, 3.12; section 8 row 34 |
| M3-Q3 | May the writes of the AICA's registers move from M3 to M4, into one write table for every device? (Recommended: yes.) | **"Move to M4"** | M3 is read-only on every device: `AICA::DebugWriteReg` keeps no body, `MOCK_M3_SUPPORT` has no writable device row; E9 of M4 states the AICA's write semantics with the other devices' | 1.2; 2.3 (`DebugWriteReg()`); 3.1; section 8 rows 4, 36, 40 |
| M3-Q4 | Should a boot of the console's BIOS be among the state-hash workloads, and from which file? (Recommended: not in M3.) | **"Not in M3"** | The round's state-hash workloads are 2ndmix 600 and 4000 vblanks, hello 300 and the guest at 90; a BIOS workload comes with a data path the owner names, as a package of its own | M3-D38; 1.2; 5.3; section 8 rows 34, 40 |

The branches the round-2 text gave for the other answers are not taken: no package E8c of the AICA's writes, no BIOS run
in B0, no speed floor or lever.

### 7.3 Risks the round carries

- **A workload that reads the DSP's registers.** W1's guest-visible change (live TEMP and MEMS, 0x2804 and EXTS read 0)
  moves `arm7` or `sh4` of a guest that reads them. By reading neither 2ndmix nor the guest does; if one does, W1 shows the
  read and the integrator decides (5.1).
- **hello has never been run in both render settings.** If it reads channel state while it runs, its state hash differs
  between the two columns of B0 and moves in W2 by the batch path's removal. B0 has both columns, so W2 knows before it
  starts. A workload that loads a DSP program would move in W1's `aica.NoBatch=yes` column (and end the old DSP's run
  there on a step with NOFL): B0's scratch trace says whether one does (3.3 item 1).
- **Speed is not held by anything in M3** (M3-Q2): the always-on DSP (the readers' estimate: 11 to 14 % slower on the
  JIT, 5 to 7 % on the interpreter) and W3's held bursts (the work of a sample where there was none) make the default
  path slower, and the round reports by how much (5.4) without a bound. A later round that takes speed up starts from
  those numbers, measured by the same method.
- **W3 moves every workload with held bursts, and every baseline behind it.** 2ndmix's lines and hashes change at W3;
  B3 is taken on W3's merge and B4 on G's, so no package behind W3 compares with a line of the gated clock. The moved
  fields of 2ndmix (and of hello, if its ARM7 is held) are shown by traces the integrator reads (5.1); `samples` and `dsp`
  must move. If 2ndmix's or hello's SH4 runs another way once the AICA's timers count and raise their interrupts while
  the ARM7 is held, `sh4` and the hash's `ram` and `sched` move too, and the trace names the first read or interrupt.
- **The guest's identity under W3 rests on no burst finding its ARM7 held** (h = 0, by reading the guest's `_start` and
  its README, "The first tick"); B2's scratch count shows it on both engines before W3 changes anything. If an engine's
  first tick found the ARM7 held, the guest would move as 2ndmix does and be shown the same way.
- **Selftests that relied on a frozen AICA while the ARM7 is held** (the counts of a held burst in the session `arm7`, a
  pause "where no timer overflows"): W3 finds them by its all-green gate and names each change; a selftest on 2ndmix whose
  run now meets an AICA interrupt while the ARM7 is held is one of them.
- **What the console does at a release of the ARM7** was not measured: its phase against the sample edge, and a request
  latched while the ARM7 was held. W3 keeps the emulator's release as it is (M3-D7); the guest's `a_reset` ends the
  service of a latched request (design 5.6), so its own lines do not depend on it.
- **S21's program wait and the held assertions against the guest.** At the guest's anchor the ARM7 is held and the DSP
  program not yet loaded; S17 and S21 assert at the anchor that a capture and a DSP step are accepted while `ENABLED`
  reads 0, and S21 then waits for the program. On the guest the bursts that make them are clocked, so the clock of a
  held burst is held by `debugger.sound.held.*`, `arm7.held.clocked`, `dsp.bp.held`, `dsp.step.held` and `sgc.held`,
  not by the contract (M3-D44). Their first emulator is E8a's and E8b's gates; F4a's runner tests hold both against a
  scripted server that behaves as the emulator on the guest.
- **The oracle's schedule** (6.5) rests on the guest's set-up ending between its first and second tick on both engines;
  with the AICA clocked in every burst (W3), a set-up that crossed a tick would run the DSP on a part of the program on
  one engine. G's longer `_start` (a copy loop of some hundred long writes) is measured there before the baseline.
- **arm64 cannot be built or run here**: W1's deletions and the build files are read; M3-D24 is read.
- **The emscripten version.** The module's bytes depend on it; the provenance file pins it, and a regeneration with another
  is refused unless named. A host without that version cannot regenerate the module, only replay it (W2's target needs node
  alone).
- **The transport's new rule** is the one change of E1's behaviour since round 1; the slow-client target
  (`debugger.contract.slow`) and the new net test hold both of its bounds.
- **The deferred response** is a new mechanism in a loop that has served one message at a time since E5; E8b's selftests and
  S17 through the real transport are where it meets a client. Its notice names no capture, so the pairing rests on the
  rules of M3-D28 (one notice owed at most, the core never answering a capture it can still cancel); `sgc.late`,
  `sgc.untaken` and `sgc.refused` hold them, and `sgc.nothread` and `sgc.drain` the two ways a request meets a thread
  that goes away.
- **The playground at the console's scale** changes what users hear from programs written against the Rust DSP (16 times
  louder inputs, EFREG held, one latch); the e2e level thresholds move with it, in F4b.
- **DSP breakpoints are reached in every sample**: usable for a step, a look and a resume; a user who wants the SH4 to run
  on has to disable or mute them. The help says so (F4d).
- **The 16-bit `MDEC_CT`** changes what the register shows (free running in place of a reload at the ring's length); no
  test has held the old values beyond their width (the test that did goes in W1).

---

## 8. Changes to the design text this cut requires

`TRACK1-DESIGN.md` is not edited by this cut, and by no package before M3G (3.11 item 7): **until then this file is the
contract wherever a row below changes the design, and a design section a brief names is read with its rows** (M3-D1).
"Now" quotes the passage as it stands (shortened with "..."), "becomes" gives the new text or what it has to state.

| # | Section | Now | Becomes |
| --- | --- | --- | --- |
| 1 | head, D6 | "Revision 6 came with the cut of the M2 round ..." | adds: "Revision 7 came with the cut of the M3 round (`M3-ROUND.md` section 2, package R7): the declarations of the DSP's run control and of the SGC capture, the new frozen headers `dbg_dsp.h` and `dbg_sgc.h`, the datapath of `DspDebugState`, the channels' outputs of `AicaChannelDebug`, the comments M3 changes; the forcing names removed." |
| 2 | head, D2 | "(merged as `libswirl/hw/aica/dsp_core`; wiring it into the emulator's AICA and into the playground is still to do) ... once the core is wired in, no DSP-less batch path. M3 starts after that wiring." | "... wired into the emulator's AICA by packages W1 and W2 of the M3 round and into the playground by F4b. There is no DSP-less path: the AICA renders every sample with the DSP, and every sample of every burst, also while the guest holds the ARM7 (W3)." |
| 3 | head, the status paragraph; 1, the milestone table, M3 | "to do"; gate "contract gate M3 (adds S17, and S21 in a second run with the DSP test program); sound regression hashes with the DSP test ELF; playground results equal native results" | the state of M3G; the gate: "contract gate M3 in its two runs (5.5.3) and the M2 gate's second run on the changed guest; sound lines and state hashes identical before / after, with the DSP test ELF and with the debugger at work on its DSP; the lines that the DSP program moves justified by the caique model (5.6); the AICA clocked whatever ARMRST says, held by the `held` targets (5.6); the playground's module equal to the native core (`aica.dsp.wasm.devtools`); `npm run check` with the mock's M3 gate, the e2e suite, the smoke spec; speed measured and reported (5.7)" |
| 4 | 1.1, the table | "Writes of device registers ... AICA with M3, the others with M4"; "Step a sample in AICA ... a DSP breakpoint on step 0 is the nearest thing (M3)" | "... not in M2 or M3: every device's write table comes with M4 (the owner's answer to `M3-ROUND.md` M3-Q3)"; "... a DSP breakpoint on step 0, or a step of the DSP from step 127, which ends at the end of the sample (M3)"; a new row: "The step view of the DSP (its operands) on the wire: not planned" |
| 5 | 2.1 | `dbg_sgc.h/.cpp` "SGC frame capture ring (M3)", package "E8" | rows: `dbg_dsp.h/.cpp`, `dbg_dsp_private.h` — "DSP breakpoint state (2.5.4): the listed set, the mask, the resume exemption, the step; called by the sound burst through `debugger.h` and by `dbg_emu`" — "R7 (frozen header) / E8a"; `dbg_sgc.h/.cpp`, `dbg_sgc_private.h` — "the SGC capture (3.3, 3.4)" — "R7 (frozen header) / E8b"; a sentence: the private headers (`dbg_*_private.h`) are not frozen (M3-D40); the row of `dbg_net` adds "text and binary messages counted apart (E8b)" |
| 6 | 2.2.1, the bullets on `OnThreadExiting` and `OnGuestStateReplaced` | the SH4's and the ARM7's temporary state | also the DSP's exemption and step, and the capture, cancelled (`M3-ROUND.md` 2.3, `debugger.h`); in `OnThreadExiting` the capture's cancel stands behind the drain of the call queue, whose `CC_TABLE` calls run on the exiting thread and may arm one |
| 7 | 2.2.3, step 2 of the hook; "During a step" | "a breakpoint or step of the ARM7 is recorded as a freezing cause"; "the run of the SH4 towards the sound burst that a step of the ARM7 waits for" | "a breakpoint or step of the ARM7 or of the DSP, or the end of an SGC capture armed at a stop"; "... that a step of the ARM7 or of the DSP waits for" |
| 8 | 2.2.4, leaving, a, and the cases g, h | the ARM7's exemption and steps | the text of `M3-ROUND.md` 2.3 (`Park()` items 2 and 3): the exemptions of the ARM7 and of the DSP at every leave; a step of the DSP with and without a burst suspended; a capture armed at the stop that completes in the run-on |
| 9 | 2.3.1, `stopId`; the priority | "... on a stop that a breakpoint of the ARM7 makes while a suspended sound burst is run on ..."; "reset, start, breakpoint, event, step, pause" | "... a breakpoint of the ARM7 or of the DSP, or the end of an SGC capture, ..."; "... step, pause, capture" |
| 10 | 2.3.2, the table, a row `dsp` | (none) | "`step`: one DSP step, `{}`; one tick `paused`, new `stopId`, `reason: "step"`, `STEP` advanced by one (127 wraps to 0, at the end of the sample); no `running` tick (from a stop with no burst suspended the SH4 runs up to one AICA tick first); also while the ARM7 is held in reset: the AICA is clocked whatever ARMRST says (2.5.2). `stepOver` / `stepOut`: `-32004`." |
| 11 | 2.3.3 | "`dc.aica.dsp.step` (M3)" | adds: steps 0..127 (else `-32602`); the core hands the set over with `emu::SetDspBreakpoints`; an active DSP breakpoint is reached in every sample |
| 12 | 2.3.7, new items | (none) | four items: **a DSP breakpoint is reached in every sample** (a resume runs one sample; an SH4 step from such a stop is dropped; disable or mute it to let the SH4 run); **the fast tier's DSP has no channel fetches and no bus reads** (`M3-ROUND.md` M3-D10: `dsp_coll`, `dsp_mem3`, `sh4_latch`, `arm_latch` differ from the console); **the AICA is clocked whatever ARMRST says, and only the ARM7 is held** (`M3-ROUND.md` M3-D7: the samples, the DSP, the timers and the interrupt lines run while the guest holds the ARM7; what the console does at a release — its phase against the sample edge, a request latched while the ARM7 was held — is not measured, and the emulator releases the ARM7 as before); **a byte write to a DSP register merges with the file's other byte** (unmeasured on the console) |
| 13 | 2.4 | (the skews of a stop) | adds: a stop of the DSP and the end of a capture armed at a stop are sound stops, with the ARM7's skews; the neutrality targets `dspn.*`, `sgcn.*` hold them on the sounding guest |
| 14 | 2.5, the "Transitional (D2)" paragraph and the first paragraph | "The batch mode and its setting ... go when the T5.0 core is wired in ... Everything below that names `per_sample`, the batch render or forcing describes the tree until then"; "With the ARM held in reset there is no `TimeStep` at all." | "The batch mode and its settings went with the M3 round (W1 removed `Dynarec.DspEnabled` and the arm64 DSP recompiler, W2 `aica.NoBatch` and the batch render): the AICA renders every sample with the DSP, in every tier. Since W3 of the M3 round the AICA is clocked whatever ARMRST says: a burst is 32 samples whether the ARM7 is held or not, and only the ARM phase is skipped while it is held (the console's behaviour, measured by caique with the ARM7 held: `M3-ROUND.md` 3.12)." The first paragraph keeps its history in the past tense ("there was no `TimeStep` at all" until W3) |
| 15 | 2.5.2 | "The sequence and state of M3 (the DSP part, E8), as planned: `if (arm_on) for i in 0..31 { ARM(i); TS_TIMERS(i); if (per_sample) { SGC(i); DSP(i); MIX(i); } TS_IRQ(i); } if (!per_sample) SAMPLE32` ... State: `{i, phase, arm_ticks_done, mixl, mixr, arm_on, per_sample}` ... Suspension points: before an ARM instruction, before a DSP step, nowhere else." | as built: `for i in 0..31 { if (arm_on) ARM(i); SampleTimers; SampleSweep; [SGC frame]; DSP(i); SampleMix; SampleIrq; [end of the sample] }  AICA::Update(32)` — the per-sample loop runs also while the ARM7 is held, and only the ARM phase is skipped then (W3, M3-D7); the tail is the harness's tick, once per burst on every path; state `{phase, i, ticks_done, arm_on, counts, dsp counts}`, `arm_on` latched at the start of the burst and deciding the ARM phase alone; the direct mix kept in the sound generator; suspension points in front of an ARM7 instruction, in front of a DSP step, and at the end of a sample that is not the burst's last (`SOUND_BURST_ARM7`, `_DSP`, `_SAMPLE`), in a burst whose ARM7 is held too (no ARM7 suspension there); `per_sample` and the batch render are gone. The M2 text's "if (arm_on) for i in 0..31 { ARM(i); libAICA_TimeStep(); }" is kept as M2's history |
| 16 | 2.5.4, whole | the bullets as planned | the text of the cut: the DSP phase through `DSP::RunPhase` (`RunSample`, or the debug loop of `StepOne` behind `dbg_dsp_Check`); the sweep's sends as `SweepBegin` and per-bus sums at 20-bit scale, `RunSample` without inputs, EXTS from CD-DA, EFREG into the mix (M3-D8); `ChannelFetch` and `BusRead` not called in `fast` (M3-D10); the step of M3-D20; the exemption and the breakpoint reached in every sample (M3-D22); the DSP clocked whatever ARMRST says, its steps and breakpoints working while the ARM7 is held, no `-32012` for it (M3-D7: "With the ARM in reset the AICA is not clocked: ..." goes); one implementation: the arm64 DSP recompiler was deleted (M3-D14); `dc.aica.dsp` with the thirteen registers of M3-D25, `RBP` / `RBL` from the DSP's `RBPL` (a CPU read of 0x2804 gives 0); the CPU's accesses to the DSP's registers by M3-D5; the playground on the same core at the console's scale (M3-D34) |
| 17 | 2.5.5 | "... the M3 work removes its three names from the tables ... in the header revision that goes with it. Only if an M3 feature were built while the batch path still exists ..." | "Forcing was never built. Header revision 7 removed `NOTICE_DSP_FORCED` and `EV_DEBUGGER_DSP_FORCED`, package F4a the capability `dsp-forced`." The rest of the section goes |
| 18 | 2.6 | events of a suspended burst run on in the park context | adds: also of a burst suspended in front of a DSP step or at the end of a sample |
| 19 | 2.7 | the AICA register file "as a guest read delivers it" | adds: the DSP's registers (0x2804, 0x3000..0x45C7) as the DSP core answers a CPU read — COEF bits 2:0 0, TEMP, MEMS, MIXS and EFREG live, 0x2804 and EXTS 0, the upper half of a slot 0; the register file itself holds what the CPUs wrote (M3-D5) |
| 20 | 2.8, what is normative; the table; the `MDEC_CT` paragraph; the counts | the widths of four registers; `dc.aica.dsp` "`STEP` ..., `MDEC_CT`, `RBP`, `RBL`; the core's datapath registers from M3"; "The DSP that runs until that core is wired in keeps the counter as a 32-bit value ... accepted until the core is wired in"; 2,940 registers | the widths of the thirteen (M3-D25) normative for every server that lists them, all nine of M3 required under the M3 gate; the table row with the thirteen, read-only; "`MDEC_CT` is the core's 16-bit counter, free running, once a sample"; `RBP`, `RBL` and `TESTB0` of `dc.aica.common` read the DSP's `RBPL`; 2,949 registers |
| 21 | 2.9 | (binary frames) | adds: a binary frame goes to the requesting client right behind its response; ticks behind it are not held up (2.11) |
| 22 | 2.11, limits | "a client that lets more than 16 MiB of outbound data queue up is disconnected" | "a client that lets more than 16 MiB of text, or more than 64 MiB of binary messages, queue up is disconnected; the two are counted apart" (M3-D29) |
| 23 | 3.1 | the target table, `dsp`: "false until M3"; the capabilities | "true from E8a of the M3 round (what `emu::TargetSteps(Target::Dsp)` says)"; `sgc-capture` and `memory-binary` from E8b; no `dsp-forced` |
| 24 | 3.3, the table and the bullets | `state.recordSgcFrames` errors "`-32012` AICA not clocked; `-32014` not complete within 5 s; `-32001` / `-32002` by lifetime"; `control.step` ... in-band "`-32012` (a sound target that is not clocked)"; the bullet "`state.recordSgcFrames`: the capture is the next `frames` AICA samples ..." | `recordSgcFrames` loses `-32012` (it records whatever the ARM7 does, M3-D7); its errors gain "`-32003` a capture records already" and "`-32014` ... or ended by a reset; `-32001` ended by a host stop or a guest exit"; `control.step`'s `-32012` is "the ARM7 held in reset" (the DSP never answers it); the bullet gains M3-D27 (the frame's moment, the capture's own buffer and the replacement at completion, stops and steps during a capture, the completion inside a suspended burst with no `running` tick) and M3-D28 (the deferred response); a bullet for `state.getMemoryBinary` (M3-D36); the DSP disassembly line for an empty step is `MPRO[n] = TRA:0` |
| 25 | 3.4 | the SGC record "devtools-protocol.md section 8 with two changes" | adds: bytes 76..83 are 0 in the `fast` tier; 84, 86, 88 are the channel's outputs of the sample; flags bit 0 is "keyed on and not run to its end"; one `binaryId` counter per connection for kinds 1 and 2 |
| 26 | 3.5 | `-32012` "`arm7 held in reset` / `aica not clocked (arm7 in reset)`", "a step of a sound target that is not clocked (2.5.3, 2.5.4)"; `-32014` "`state.recordSgcFrames` not complete within 5 s"; `-32003` "... or run control while another action is armed" | `-32012` "`arm7 held in reset`", "a step of the ARM7 while the guest holds it in reset (2.5.3); the DSP and the capture never answer it (2.5.2)" (M3-D30); `-32014` "... not complete within 5 s, or ended by a reset"; `-32003` adds "or a capture while one records" |
| 27 | 4.5 | one package; "`debuggerClient.ts`: `fetchSgcFrameData` per 3.4, `recordSgcFrames(frames)`"; "constants 128 / 8192 / 64 from the result"; "behind the existing `src/lib/aicaDsp.ts` interface"; "'Load from emulator' reads the AICA register file and DSP RAM through `state.getMemoryBinary`" | F4 is F4a to F4d (`M3-ROUND.md` 3.1, 3.4, 3.6, 3.10); the client was done in F1 and the mock's record too; the constants are those of `binaryFrame.ts`; `aicaDsp.ts` takes the core's interface (steps in order, `run` with stops, the view); the module's place, provenance, pinned emscripten and notice, and the emulator's replay of it (M3-D34); "Load from emulator" by M3-D35; the help on DSP breakpoints |
| 28 | 5.3 | the sessions of M1 and M2 | adds the sessions `dsp` and `sgc` on the guest, the run-ended tests `dspn.*` and `sgcn.*`, the test hooks of `dbg_dsp_private.h`, `dbg_sgc_private.h` and `dbg_core_private.h` |
| 29 | 5.4 | the modes of `contract.py` | adds `m3` and `m3dsp` (and the mode `dsp` E8a used), the targets `debugger.contract.m3*` and `.m3dsp.guest*` |
| 30 | 5.5, S01, S10, S17, S21; the `dsptest` paragraph | the rows as they stand; "E8 adds the DSP program part" | S01 under M3: `targets[dsp].stepping` and the nine rows; S10: the DSP text rule; S17 and S21 as `M3-ROUND.md` 3.1 item 3 has them (the capture and the DSP step that work at the anchor with the ARM7 held, with their notes; S21's program wait; the parts, the ELF's `g_dsp_mpro`; S17's capture response and its stop with reason `capture` in either order); "package G of the M3 round added the DSP program part" |
| 31 | 5.5.3 | "a second one ... `--elf <dsptest.elf> --milestone M3 --dsp-program`, which selects S00 and S21 alone"; "`--mock-support` (`m1` or `m2`) ... `check` runs the gate on the default mock and on `m2`" | "... which selects S00, S17 and S21", run on the guest twice: whole, with the note `sgc: captured with the arm7 held`, and `--only S00,S21`, with the note `dsp: stepped with the arm7 held` (in the first, S17's capture lets the guest release its ARM7 before S21); the M3 gate also reruns the M2 gate's second run on the changed guest; `--mock-support` takes `m3`; `check` runs `--milestone M3` on the default mock and on `m3` with the note of S16, and the two `--dsp-program` runs on the mock with their notes, on a mock that starts paused with its ARM7 held (its anchor, as S00's stop is the emulator's: M3-D31) |
| 32 | 5.5.4 | (M3 not mentioned) | adds: not asserted — what an SGC record holds beyond its layout, its flags and its level (the emulator's selftests hold it), whether a capture's data is stale when its count is right, what the DSP computes (J1 to J3 of `M3-ROUND.md` 5.2 hold it), the length of a capture beyond one AICA period either way |
| 33 | 5.6 | the line's six fields; "E8 adds the DSP program part"; "the wiring of the T5.0 core is the one change that is allowed to move the `samples` hash (it is gated by track 5's own tests against the console)"; the batch half of the targets | the field `dsp` (M3-D12) in the line and in `docs/nulldc3/automated-runs.md`; the guest's DSP program (package G, section 6 of the cut); the round's rule of `M3-ROUND.md` 5.1 (W1 identical in both settings, W2's default equal to the per-sample line, W3's moves of the workloads with held bursts shown by the `held` targets and traces, the program's lines justified by J3); the targets without the render key, and `debugger.sound.held.*`; and the texts W3 makes false: "(in per-sample mode no pair exists while the ARM7 is held, so silence is a family)" becomes "(every tick makes 32 pairs, whether the ARM7 is held or not; the runner's check against the family of silence stays, a superset)"; "In batch mode the channels the SH4 keyed on are rendered without the ARM7: a guest whose ARM7 is never released is silent per sample only." becomes "The channels the SH4 keys on are rendered whether the ARM7 is held or not: a guest whose ARM7 is never released sounds with them."; "With the ARM7 never released the per-sample `samples` is the hash of 32 zero pairs ..." goes; the M3 round's result (M3G) |
| 34 | 5.7 | "the workloads `tests/system` holds (2ndmix 600 and 4000 vblanks, hello, the BIOS boot)"; "for a package on the sound path ... grow by at most 48 per sample"; speed by `tests/perf` | "(2ndmix 600 and 4000 vblanks, hello 300)", and a BIOS boot only with a data path for a BIOS the owner names (M3-D38; the owner's answer to M3-Q4); "grow by at most 48 per sample" and the speed rule did not gate the M3 round (the owner's answer to M3-Q2, "don't mind perf for now"): its packages measured and reported host instructions a sample and speed by the method of `M3-ROUND.md` 5.4 (counts against a base without the `dsp` field on runs the harness does not fold, M3-D15; `perf.py --config`; ratios of medians of rounds in turn), and M3's speed is recorded (M3G); whether a later milestone bounds speed again is that milestone's cut |
| 35 | 6.2, row F4 | one row | four rows, F4a to F4d, with the owns and gates of `M3-ROUND.md` 3.1, 3.4, 3.6, 3.10 |
| 36 | 6.3, intro and the rows E8, E9 | "Then E8 for M3 (behind the wiring of the T5.0 core) ..."; E8's row; E9's row | rows R7, W1, W2, W3, G, E8a, E8b, M3G, each pointing at its subsection of the cut; E8's row replaced by them; E9's row gains "the write semantics of the AICA's registers and the body of `AICA::DebugWriteReg`" (M3-Q3) |
| 37 | Appendix B | the list of frozen headers; the table of revisions; "No revision is pending. The next one comes with the cut of M3."; the API text of `dbg_net.h`: "a client with more than 16 MiB queued is disconnected" | `dbg_dsp.h` and `dbg_sgc.h` join the list; a row "7 — R7, the cut of the M3 round — ... texts in `M3-ROUND.md` section 2"; "No revision is pending. The next one comes with the cut of M4."; the API text with the two bounds |
| 38 | Appendix A.3 | (M2 as built) | a part "M3 as built", made at M3G |
| 39 | `DESIGN-V3-DECISIONS.md`, decisions 9, 22, 26, 32, 35, 42 (and the copy under `docs/nulldc3/`) | "the M3 DSP part is a further run with the DSP test program"; decisions 22 and 32: the widths of four registers of `dc.aica.dsp` normative; "Per-sample forcing ... is transitional"; "package F4 gives the mock the method together with the scenario part"; decision 42: "every row of `dbg_events.h` is listed by exactly one node, except" `EV_DEBUGGER_DSP_FORCED` | "the M3 gate is two runs, the second `--elf <dsptest.elf> --milestone M3 --dsp-program` (S00, S17, S21, and `--only S00,S21`), and the M2 gate's second run is made again on the changed guest"; "the widths of the thirteen registers of M3-D25 are normative for every server that lists one of them, the nine of M3 required under the M3 gate"; "Forcing was never built; its names went with header revision 7"; "package F4a gave the mock the method with the scenario part"; "every row of `dbg_events.h` is listed by exactly one node (revision 7 removed `EV_DEBUGGER_DSP_FORCED`)" |
| 40 | `DESIGN-V3-OPEN.md`, its head; `PLAN.md` | "the next revision is ..."; T5.0 "Still to do under this item: wire it into the emulator's AICA, and swap the playground (devtools F4)"; "Decisions made: The generated DSP wasm glue is committed in the devtools repo so the UI builds without Rust" | "Revision 7 is applied (package R7 of the M3 round); its list is section 2 of `M3-ROUND.md`"; T5.0 done, with the round's packages; "the emscripten module of the DSP core is committed in the devtools repo, so the UI builds without emcc"; the backlog gains: `ChannelFetch` / `BusRead` with the `cycle-similar` AICA (M3-D10), the AICA's writes with M4 (the answer to M3-Q3), a BIOS workload with its data path (the answer to M3-Q4), the core's state over the wire for a load at any stop (M3-D35), speed (the answer to M3-Q2: M3's numbers are the start); track 5's `fast` fixes lose "timers/interrupts not gated by the ARM" (done by W3 of the M3 round, with the samples and the DSP: the whole time step) and keep "MCIPD bit 10" and the rest |
| 41 | design head, D5; `PLAN.md`, ground rules | "commits are made as work lands, on local branches"; "commits are made as work lands, nothing is pushed" | "packages commit on local branches and push nothing; after every milestone the integrator pushes `skmp/nulldc3` of both repositories and opens review PRs into `main`, the UI's first (the owner, 2026-10-16); nothing is merged by the integrator" |
| 42 | 2.5.3, the bullet on the held ARM7 | "With the ARM held in reset (`SoundCPU::DebugEnabled()` false) nothing is clocked: `control.step / stepOver / stepOut {arm7}` return the in-band error `-32012` ..." | "With the ARM held in reset (`SoundCPU::DebugEnabled()` false) the ARM7 is not clocked — the rest of the AICA is (2.5.2, since W3 of the M3 round) —: `control.step / stepOver / stepOut {arm7}` return the in-band error `-32012` ..."; the rest of the bullet as it is |
| 43 | 4.4, the toolbar bullets | "the views keep their own step buttons, which are off with the toolbar's while the ARM7 is held"; "ARM7 controls enabled by `targets[arm7].stepping`, and off while the ARM7 is held in reset (`ENABLED` 0)" | "... which are off with the toolbar's while the ARM7 is held, for the ARM7's views"; adds "DSP controls enabled by `targets[dsp].stepping`, whatever `ENABLED` of `dc.aica.arm7` reads: the DSP is clocked while the ARM7 is held (2.5.4; F4d of the M3 round)" |

Rows 42 and 43 were added in round 3, behind the others so that the row numbers the round cites stay.

---

## 9. The briefs

One brief per package, a file of its own under `m3-briefs/`, for the orchestrator to hand to its engineer:
`F4a-BRIEF.md`, `R7-BRIEF.md`, `W1-BRIEF.md`, `F4b-BRIEF.md`, `W2-BRIEF.md`, `W3-BRIEF.md`, `F4c-BRIEF.md`,
`G-BRIEF.md`, `E8a-BRIEF.md`, `E8b-BRIEF.md`, `F4d-BRIEF.md`, `M3G-BRIEF.md`. Each has the sections the package workflow
reads (a title line and the state it starts from; "What is known"; "What <KEY> delivers", numbered; "Owns"; "Gate";
"Verifier"), is complete enough to work from without this file, and names the sections of this file that are part of it.
A verifier's brief is the "Verifier" section of its package with the gate above it.

**This section replaces `M2-ROUND.md` Appendix A.0 for this round.** The package workflow takes the rules of an engineer
and of a verifier as its argument `rules` and names A.0 when none is given; the orchestrator starts every package of
this round with `rules` naming this section (section 4), not with `extra`, which reaches the engineer only. In this
round the rules are the list below, and where a prompt or a document still names A.0, or A.0 names `M2-ROUND.md` section
2 or section 7, read this section, or this file's section 2 or section 8. Every emulator switch has two dashes
(`--config config:Headless=yes`); the one-dash form is an error since N1.

What every brief carries (in its "What is known"):

- Work in your own worktree and your own out-of-tree build directory under `agents/nulldc3/` (emulator:
  `agents/nulldc3/wt/<pkg>`, branch `skmp/nulldc3-<pkg>` of `devcast`, cut from `skmp/nulldc3` as you find it; UI: the
  branch `skmp/nulldc3` of `dreamcast-devtools`, packages one after another on it). Commit as work lands; push nothing.
  Reports and scratch go to `agents/nulldc3/<pkg>/`. Delete your build trees before you return.
- The machine: one workflow at a time, `ninja -j8`, `ctest -j4`, at most four emulator processes, every emulator run
  headless (`--config config:Headless=yes`, two dashes). Gates that take more than five minutes run on a subset while you
  iterate and in full once at the end.
- **A gate is green when its tests ran.** Configure the tests project with `-DNULLDC_ELF_DIR`, `-DNULLDC_DEVTOOLS_DIR`
  (the UI pin the integrator names), `-DNULLDC_WREN7_DIR`, `-DNULLDC_CAIQUE_DIR`, `-DNULLDC_SHRIKE4_DIR`,
  `-DNULLDC_SH4_TESTS_DIR` and `-DNULLDC_ARM7_TESTS_DIR` (or the two vector submodules initialised in your worktree) and
  `-DNULLDC_TESTS_REQUIRE_DATA=ON`. "All ctest targets green" means twice, none skipped, the number in your report, and
  `git status` empty behind them.
- **The UI bundle is the one look-up that has to miss**: the emulators of a package gate embed none; the UI pin is a
  detached worktree, `agents/nulldc3/wt/ui-pin`, never a directory named `dreamcast-devtools` beside your worktree; an
  emulator that has to carry the UI (F4d's smoke spec, M3G) is configured by itself with
  `-DNULLDC_DEVTOOLS_DIST=<checkout>/dist-native` and handed to the tests project as `-DNULLDC_EMULATOR=`.
- The five builds of an emulator gate: the default, `-DNULLDC_DEBUGGER=OFF`, `-DTARGET_NO_REC=ON`, `-DHAS_TA_LLE=OFF`,
  the generic host (an unknown `CMAKE_SYSTEM_PROCESSOR`, as `agents/nulldc3/p-desktop/` has it).
- You edit what your brief says you own and nothing else. **The frozen headers are those Appendix B of the design lists**
  (`debugger.h`, `dbg_emu.h`, `dbg_sh4.h`, `dbg_core.h`, `dbg_regs.h`, `dbg_mem.h`, `dbg_events.h`, `dbg_symbols.h`,
  `dbg_callstack.h`, `dbg_selftest.h`, `dbg_arm7.h`, `hw/sh4/sh4_dbg.h`, the debugger support block of `hw/sh4/sh4_if.h`,
  the debug blocks of the `hw/` headers, and the APIs of E1 and E2: `dbg_net.h`, `dbg_webui.h`, `dbg_disasm.h`), **with
  `dbg_dsp.h` and `dbg_sgc.h` from R7**: their text is section 2 of `M3-ROUND.md`, and where section 2 has no text for
  one, it is as the tree has it. The private headers (`dbg_*_private.h`) are not frozen; a package edits one only where
  its brief says so (M3-D40). If you need a declaration of a frozen header changed, or a file of another package, stop
  and report: the integrator decides.
- Where `M3-ROUND.md` and `TRACK1-DESIGN.md` differ, `M3-ROUND.md` is the contract; a design section is read with its rows
  of section 8.
- Time neutrality: a debugger that is not used changes no guest-visible state. Lines and hashes are compared exactly; a
  move is a defect until shown otherwise (5.1).
- Speed and host cost gate nothing in M3 (the owner's answer to M3-Q2, 5.4): where your brief asks for them, measure them
  by 5.4's method and report them; change nothing for speed.
- The AICA is clocked whatever ARMRST says from W3 on (M3-D7): only the ARM7 is held by it.
- No absolute home paths, host names or user names in anything you commit. No line or hash is stored in the tree.
- Your report: what was delivered against each item; every departure from the cut and why; the commands and results of
  the gate; what is open. A verifier who did not write the package checks it, with mutations; expect fix rounds.

---

## Appendix I. The integrator's notes, by package

Empty at the cut. As each package is verified and merged, the integrator adds a section here (as `M2-ROUND.md` Appendix I
has one for each package of M2): the commit verified and merged, the counts where they differ from the cut, what the
verifiers left for later packages, the departures accepted, and the known limits. A later brief is read with these.

---

## Appendix R. The review of the cut

Round 1 of an adversarial review read the cut and the briefs against the two trees with three lenses (the sound side,
the frontend, the integration of the packages): 44 findings, 15 + 13 + 16. Round 2 read the revised cut with the same
lenses: 4 findings, numbered on from round 1 (S16, F14, F15, I17; the finding S16 is not the scenario S16), in the
second table below. Round 3 is not a review: it applies the project owner's answers to the four questions of 7.2,
in the third table below; the review of round 3 read the result with two lenses (the sound side, the integration):
12 findings, numbered on (S17 to S21, I18 to I24), in the fourth table. Each was checked against the code and the
design; the column "Outcome" says whether it was taken (as proposed, or with a change the evidence asked for) or not,
and where the cut now settles it. A finding of two lenses is listed under the first and named under the second.
Findings are cited as **S** (sound), **F** (frontend) and **I** (integration) with their number in the review's list.

| # | Sev. | The finding, short | Outcome | Settled in |
| --- | --- | --- | --- | --- |
| S1 | must | J3's tool drives its core with no sends, but the folded blob carries `mixs_sent`, which every send sets, a send of 0 included; W1's sweep sends to bus 0 and the first channel's bus every sample: the oracle is red on a correct emulator | **Taken.** Checked: `aica_dsp_core.cpp`, `mixs_send` sets the bit whatever the value, `SaveState` writes `mixs_sent[0..1]`, the core's own `SweepBegin` at step 0 clears the fill bank's bits. The tool makes the emulator's sweep: `SweepBegin`, sends of 0 to bus 0 (and the first channel's ISEL behind the table; neither the ARM7 program nor `g_irq` writes an ISEL) | 5.2 J3, 6.5, 3.7 items 3 and Verifier, G brief |
| S2 | must | A capture armed at a stop that completes in the last sample of a suspended burst: `RunSoundBurst` returns true, `Leave` publishes `Running`, the `Capture` stop stays in `e.want` | **Taken.** Checked: `dbg_emu.cpp` `RunBurstOn` returns what `RunSoundBurst` returns, `OnSoundStop` in the park context only merges into `e.want`, `EnterStop` alone clears it. Rule for `RunBurstOn`, `DoDspStep` and `DoArm7Step`: a stop recorded in a run-on is decided also when the burst ended; E8a builds it (testable by `.burstend`), E8b's `sgc.stop` gets the case and may fix the rule for a capture | 2.3 `Park()` item 2, 3.8 items 5 and 7 and Verifier, 3.9 items 6 and Owns and Verifier, 3.0, M3-D27 |
| S3 | must | W1's instruction counts are harness runs, and W1's own `dsp` fold (2,256 bytes written and folded byte by byte each tick) costs some 500 a sample: the default setting's 48 cannot be met | **Taken**, by the first of the two proposals (runs the harness does not fold). Checked: E6b's `icount.txt` counted harness runs; `aica_hash_fold` is byte-serial; `SaveState` is a byte writer. W1 (and M3G's count against B0) count B0's exit runs of design 5.7; samples from the exit line's `sched`; the fold's cost reported. Same as I2 | 5.4, 3.3 Gate, 3.11 item 6, M3-D15, W1 and M3G briefs |
| S4 | must | The nine rows need nine `DspReg` selectors in `dbg_regs_private.h`, which no package owns; section 9 freezes "the headers of `libswirl/debugger/`", wider than Appendix B | **Taken.** Checked: `enum DspReg` ends at `kDspRegCount`, `regs_test.cpp` checks every DSP selector against it; Appendix B names no private header. E8a owns the enum; section 9 lists the frozen headers; private ones are not frozen. Same as I5 | 3.0, 3.8 items 6 and Owns, section 9, M3-D40, E8a brief |
| S5 | must | W1 adds `dsp` to `LINE_FIELDS`; `sound_line()` takes only lines with exactly those fields, and `soundlines_test.py`'s stand-in prints six: `debugger.sound.runner` goes red, and W1 does not own the test | **Taken.** Checked: `soundlines.py` `sound_line`, `soundlines_test.py`'s stand-in line. W1 owns the stand-in's line and the README's line | 3.0, 3.3 item 8 and Owns, W1 brief |
| S6 | must | Three of W1's mutations have no named target: the mix taking EFREG from the file, the core not reset at a machine reset, the save state without the core's blob; the DSP could go unheard through every gate | **Taken.** Checked: the empty program leaves EFREG 0 in file and core; `MachineState` compares two states that both lack the blob; the `emu` round trip checks one word of RAM. New target `aica.dsp.emu.state` (three checks) and G5 (the guest is heard) | 3.3 items 10 and Verifier, 5.1, 3.7 Gate, W1 and G briefs |
| S7 | should | B2 and B3 have no state hashes of design 5.7, yet G, E8a, E8b compare 5.7 hashes with them; M3G compares with B0's `aica.NoBatch=yes` column, the old DSP's | **Taken.** B2 and B3 carry the 5.7 hashes (both engines, 2ndmix 600 in the five builds); M3G compares with B3 | 5.3, 3.7 Gate, 3.8 Gate, 3.11 item 2, G, E8a, M3G briefs |
| S8 | should | "The BIOS boot" is a B0 workload no document defines; and nothing shows that no workload loads a DSP program | **Taken.** Checked: `tests/system` has no BIOS case, no data path names a BIOS, E5 and J1 ran 2ndmix and hello only. The BIOS boot leaves the round (M3-D38, M3-Q4 for the owner); B0 records from a scratch trace whether a workload writes an `MPRO` step | 1.2, 3.3 item 1 and Gate, 5.1, 5.3, 7.3, section 8 row 34, M3-D38, M3-Q4, W1 and M3G briefs |
| S9 | should | Revision 7 leaves the SH4 and ARM7 paragraphs of `Step()` and the ARM7 bullet of `Park()` false: a DSP breakpoint or a capture's end can be the stop; a burst suspended in front of a DSP step of its last sample has no ARM phase left | **Taken.** Checked: `dbg_emu.h` `Step()`, `debugger.h` `Park()`, `DoArm7Step` falling through to the wait. Texts added, with `LatchStop` (I9). "At the end of a sample of its last sample" dropped: `SOUND_BURST_SAMPLE` is never the last sample | 2.3 (`debugger.h` `Park()` item 3, `dbg_emu.h` `Step()`, `LatchStop()`) |
| S10 | should | The notice names no capture and the core answers some requests itself: a completion behind a deadline answer, or a retried cancel, answers or cancels the next request | **Taken**, with one change: the cancel is still retried while a notice is owed, which is safe once no capture can be armed meanwhile. Checked: `emu::Call` gives up with `kErrBusy` after 2 s. Merged with I16 | M3-D28, H7-4 (`Cancel`, `SetCompletionNotice`, `Arm`), 2.3 `Call()`, 3.9 items 3, 6 and Verifier, 7.3, E8b brief |
| S11 | should | `dbg_sgc_active`'s comment ("never between a sweep and its frame") is false for a capture armed at `SOUND_BURST_DSP` and for `Cancel` from `OnThreadExiting` / `OnGuestStateReplaced`; `sgc.count`'s rule depends on where the capture was armed | **Taken**, with a change for `sgc.count`: no sweep counter exists, so the rule counts the AICA's time steps (`SoundBurstCounts` `out[2]`), `frames`, and `frames` + 1 when armed in front of a DSP step | H7-1, H7-4 `Arm`, M3-D27, 3.9 item 6, E8b brief |
| S12 | should | `regs.rows.aica`: the common rows must stay `kSrcAica`; a made-up state writes 0x2804 into the file only; on 2ndmix 0x2804 is 0 everywhere, so W1's two mutations are invisible | **Taken**, with a change: a TEMP word tells the core from the file only in its low slot with bits 15:8 set (the core keeps bits 7:0 there; its high slot reads back as written); a `COEF` word with bits 2:0 set is the plainer witness (the core reads them 0). Checked: `StoredWord`, `layout_of`'s `kSrcAica` check, the regs session on 2ndmix. Merged with I8 | 2.5 item 3, 3.3 item 9 and Verifier, R7 and W1 briefs |
| S13 | should | `aica.dsp.emu.lockstep` leaves "how the test knows the sends" open; a hook on the emulator's own sends hides the scale mutation, a zero CD-DA stand-in hides the EXTS one, no write at slot + 2 hides the routing one | **Taken.** Checked: the per-sample channel loop has no left/right fold, so a send equals the direct output when `IMXL` = `DISDL`. Sends worked out by the test; a non-zero CD-DA pair; byte and slot + 2 writes | 5.2 J2, 3.3 item 10 and Verifier, W1 brief |
| S14 | should | The floor is an absolute CPU-time figure; the interpreter's margin (~7 %) is below the load's spread (8.8 %) | **Taken.** Checked: `m2g/logs/perf.txt`, base spread 8.83 %. The floor is checked on the ratio to B0's binary of the same session times M2G's figure; a result near the floor is measured again | 5.4, 3.5 Gate, 3.11 item 6, M3-Q2, W2 and M3G briefs |
| S15 | should | A `CC_TABLE` closure that `emu::Call` runs under L, with no thread, would arm a capture with no emulator thread | **Taken.** Checked: `emu::Call` runs a `CC_TABLE` closure on the caller when no thread exists. The closure arms nothing; `Arm` is (E) only; `sgc.nothread`. Round 2 (S16) found the other path, a closure the exiting thread's drain runs on (E): the cancel behind the drain, `sgc.drain` | H7-4 `Arm`, M3-D28, 3.9 items 3 and 6, E8b brief |
| F1 | must | `check`'s `--dsp-program` run requires S16's note, which no selected scenario makes: red on every run | **Taken.** Checked: `scenariosOfMilestone`, `--require-note` fails a run without the note. The `--dsp-program` runs require their own notes instead. Same as I3 | 3.1 item 4, section 8 row 31, F4a brief |
| F2 | must | S21's "held at the start" is never reached in run 2 (S17 comes first and settles the ARM7); no run requires it; `contract.py` refuses `--require-note` outside `m2` and `parts` | **Taken.** Checked: `SCENARIOS` order, `contract.py`'s refusal. E8a's `m3dsp21` targets stay with the note `dsp: held at the start`; `m3dsp` requires `sgc: held at the start`; `contract.py` accepts the option in `dsp` and `m3dsp`; M3G's run 2 is both | 3.1 item 3, 3.8 item 9 and Owns, 3.9 items 8 and Gate, 3.11 item 1, M3-D32, section 8 rows 31 and 39, E8a, E8b, M3G briefs |
| F3 | must | "The twelve broken servers each red": number 12 (a DSP that computes otherwise) is outside the contract; number 5's stale data is not detectable | **Taken.** Servers 1 to 11 red, 5 as the all-zero capture under `--dsp-program`; 12 held by J1 to J3 and F4d's smoke spec; staleness not asserted (5.5.4). Same as I4 | 3.1 item 3 and Verifier, section 8 row 32, F4a brief |
| F4 | should | S17's second request and "the response only when complete" race the first capture's end, a few ms on an unthrottled emulator | **Taken**, with a change of sequence: a DSP breakpoint on step 0 stops the machine within a sample, so the capture is armed at a stop with the breakpoint enabled (deterministic, no retries), and the breakpoint's stop holds it open. Broken servers: answer at the arm, buffer replaced at the arm | 3.1 item 3, M3-D32, F4a brief |
| F5 | should | The mock's time cannot pass F4a's own rules: 22 AICA periods per emulation step, a wall-clock capture, one DSP step per emulation step, `MDEC_CT` per emulation step | **Taken.** Checked: `mockDebugger.ts` (`CYCLES_PER_EMULATION_STEP`, the capture's `setTimeout`, `MOCK_DSP_RING`, the `MDEC_CT` line). The mock gets the emulator's sound clock | 3.1 item 2 and Verifier, M3-D31, F4a brief |
| F6 | should | The mock makes one sha256 digest per fabricated byte: a 16 MiB read takes tens of seconds, past the client's 10 s timeouts | **Taken.** Checked: `sha256Byte`, `fabricatedByte`, `requestTimeoutMs`, `BINARY_FRAME_TIMEOUT_MS`. One digest per 32-byte block; the pinned tests follow | 3.1 item 2 and Verifier, M3-D31, F4a brief |
| F7 | should | S21 lets through: no resume exemption, a breakpoint once a burst, a step from 127 that runs on, an SH4 that runs on at the first step, frozen rows | **Taken**, with a correction: a resume from a DSP breakpoint in a sample that is not the burst's last publishes no running tick (the park loop runs the burst on: 2.3, `Resume()`), so the rule allows a running tick and does not require one; the once-a-burst server is caught by `MDEC_CT` one less, not by `SH4_CYCLES`; the SH4 bound is one period and the rest of a slice (145124 + 448) | 3.1 item 3, F4a brief |
| F8 | should | S17's `memoryBinary` checks no requester-only delivery and large reads beyond their length | **Taken** | 3.1 item 3, F4a brief |
| F9 | should | The mock's `dc.aica.common` has no `RBP`, `RBL`, `TESTB0`; F4d needs them for its load path and may not touch the mock | **Taken.** Checked: the mock's common node lists MVOL, VER, DMEA, SCIEB, SCIPD, SCILV0, L, M. F4a adds the three and a `TESTB0` option | 3.1 item 2, M3-D31, F4a and F4d briefs |
| F10 | should | At any stop with `STEP` 0 the bank the DSP reads next is the sweep's fill bank, which no CPU read and no row shows; EXTS has no row | **Taken.** Checked: `sample_begin` changes the banks at step 0, `RegRead` gives the bank `mixs_dsp` names, `mixs_swept` has no register index, EXTS reads 0. Exact load from `STEP` 1 to 127; a banner otherwise; the smoke spec's s at least 1 | 1.2, 3.10 items 1 and 5 and Verifier, M3-D35, section 8 row 40, F4d brief |
| F11 | should | `PROVENANCE.json` is read across repositories with no fixed keys, version form, `emcc` or meaning of "the commit of `dsp_core`" | **Taken.** Checked: `build.sh` finds `emcc` on the `PATH`, then `$EMSDK`, then `~/emsdk` | 3.4 items 1 and 2 and Verifier, 3.5 item 8, M3-D34, F4b and W2 briefs |
| F12 | should | `aicaDsp.ts` is type-checked by `tsconfig.server.json` and, through `dspUtils.ts`, by `tsconfig.scripts.json`: a Vite suffix import breaks `check` | **Taken.** Checked: both configurations, `dspUtils.ts`'s type import | 3.4 item 3 and Verifier, M3-D34, F4b brief |
| F13 | should | M3-Q1 is wanted "before W2", but F4a, package 1, already asserts `-32012` while the ARM7 is held | **Taken** | 7.2 M3-Q1, section 4 |
| I1 | must | As S1; and the fallback copy (ISEL to an unread bus) puts that bus's MIXS into the blob | **Taken**, the fallback deleted: G shows `IMXL` 0 gives exactly 0 (`SendLevel[0]` 255, `tl_lut[255]` 0, `sgc_if.cpp`) | 6.5, 3.7, G brief |
| I2 | must | As S3; proposes a scratch build with the fold compiled out | **Taken as S3** (runs without the harness); the scratch build is the rejected alternative of M3-D15 | as S3 |
| I3 | must | As F1 | **Taken as F1** | as F1 |
| I4 | must | As F3 | **Taken as F3** | as F3 |
| I5 | must | As S4; and the nine widths belong in `regs_test.cpp`'s `named` list | **Taken as S4**, with the `named` list in E8a's turn | as S4 |
| I6 | must | W2 deletes `settings.aica.NoBatch`; W1's `.emu` machine (and any other test machine of W1's) sets it, and W2 owns none of those lines | **Taken.** Checked: today only the emulator, the regs selftest, the GUI and `soundlines.py` name it, so the lines are W1's to come. W2 owns them | 3.0, 3.5 Owns, W2 brief |
| I7 | should | W1 deletes `dsp_backend.h` and `dsp_helpers.cpp`, which `aica-dsp-asm/` (repository root) includes and compiles; no gate builds it | **Taken**, as a port (M3-D39): the tool is maintained on this branch (its macOS script changed on 2026-10-08) and the field layout is the core's | 3.3 item 6, Owns and Gate, 3.0, M3-D39, W1 brief |
| I8 | should | R7's "the rows read `rbpl`" does not say how; changing the common rows' keys breaks `regs_test.cpp`'s spec | **Taken as S12**: the rows keep their keys and `kSrcAica`; `ReadAica` takes 0x2804 from `rbpl` | 2.5 item 3, 3.2, R7 brief |
| I9 | should | As S9, and `LatchStop`'s "a breakpoint or a step of the ARM7 is recorded through OnSoundStop" | **Taken** with S9 | 2.3 |
| I10 | should | R7's true branch of the held-ARM7 DSP lines first runs in E8a's gate, which does not own the file; Over and Out answer `-32004` there, not `-32012`; R7's verifier names the wrong text | **Taken.** Checked: `dbg_selftest_arm7.cpp`'s check of the DSP's steps in `arm7.step.notclocked` | 2.5 item 2, 3.0, 3.2 Verifier, 3.8 Owns, R7 and E8a briefs |
| I11 | should | The pacing of a held burst needs a new `struct AICA` declaration (only the sound generator holds the stream); E8a's brief does not carry the pacing | **Partly taken.** Not taken: no declaration is needed, because `Dreamcast_impl` holds the stream itself (`libswirl.cpp`: the member `audio_stream`, created there and handed to `AICA::Create`), so the call is in `AicaUpdate`. Taken: E8a keeps the pacing, W2's test is a named ctest target | 3.0, 3.5 item 4, 3.8 item 2 and Verifier, W2 and E8a briefs |
| I12 | should | F4b's interface list lacks the `ram` and `mixs` views, `REG.MIXS_DSP`, `REG.RBPL` and the 2 MiB core that F4d's load needs, and F4d may not edit `aicaDsp.ts` | **Taken** (with `MIXS_FILL` too) | 3.4 item 3, F4b and F4d briefs |
| I13 | should | The package workflow names `M2-ROUND.md` A.0 as the rules and a one-dash switch | **Taken**, and corrected in round 2 (I17). The workflow is not this task's file: section 9 says it replaces A.0, section 4 tells the orchestrator to edit the workflow's five places before F4a (round 1 also allowed an `extra` text, which reaches the engineer only: withdrawn), and every brief says so as a backstop | section 9, section 4, every brief |
| I14 | should | Decisions 42, 22, 32 and the plan's "nothing is pushed" (with design D5) have no row in section 8 | **Taken** | section 8 rows 39 and 41 |
| I15 | should | "Loaded by four emulators" reads as five emulator processes | **Taken** | 3.7 item 5, 3.8 Verifier, G and E8a briefs |
| I16 | should | Undoing an arm behind a refused `Resume` with `Cancel` gives a notice the frozen `Cancel` comment does not allow | **Taken** with S10: `Cancel(kErrNotPaused)` in the `CC_EXEC` call, and the notice answers `-32003` | H7-4, 2.3 `Call()`, M3-D28, E8b brief |

Nothing was rejected outright. Where a proposal was changed, the evidence is in its row: S10 (the retry stays), S11 (no
sweep counter), S12 (a `COEF` word as the witness), F4 (the order of breakpoint and request), F7 (no running tick
inside a burst; `MDEC_CT` catches the once-a-burst server), I2 (the alternative), I7 (a port), I11 (no declaration).

**Round 2.**

| # | Sev. | The finding, short | Outcome | Settled in |
| --- | --- | --- | --- | --- |
| S16 | must | S15 settled one path only: a `recordSgcFrames` `CC_TABLE` call that waits in the queue when the thread takes a host stop or a guest exit is run by `OnThreadExiting`'s drain on (E), `OnEmuThread()` true, the ARM7 clocked, so it arms; 2.3 put the cancel among "what belonged to a stop", in front of the drain, so the thread exits with `dbg_sgc_active` 1, the request is answered `-32014` at 5 s, and a thread started within 5 s records into the capture | **Taken as proposed**, with a fifth place (the `dbg_sgc_active` comment) and the test's means named. Checked: `dbg_emu.cpp` `OnThreadExiting` ends the stop (`sh4::OnStop`, `arm7::OnStop`), then loops over the queue running `CC_READ` / `CC_TABLE` calls (`RunCall`) until it is empty and sets Stopped, and clears `t_is_emu` only at its end; `dbg_emu.h` `Call()` ("taken by that thread, under L") and `OnEmuThread()` ("until OnThreadExiting() has published Stopped"); `emu.hoststop` in `dbg_selftest_emu.cpp` already queues calls while the exiting thread naps in its stop callback and checks that the drain runs `CC_TABLE` on (E): `sgc.drain` can use the same means, no new hook needed. The cut's own `OnGuestStateReplaced` text ("while Stopped ... no capture exist then, because OnThreadExiting() ended them") was false by the same hole | 2.3 (`OnThreadExiting()`), H7-1 (`dbg_sgc_active`), H7-4 (`Arm`, `Cancel`), M3-D27, M3-D28, 3.9 items 3, 4, 6 and Verifier, 7.3, section 8 row 6, E8b brief |
| F14 | must | The `--dsp-program` mock with `arm7Held` alone has no anchor stop: the runner's mock is started at listen (`listenMockDebugger`), S00 is emulator-only, and the running mock releases the ARM7 after 8 emulation steps (~128 ms); whether S17 / S21 meet `ENABLED` 0 races the session's opening, and the required notes fail under load | **Taken as proposed.** Checked: `run.ts` `prepare` builds `new MockDebugger({ supports })`; `mockWebSocket.ts` calls `mock.start()` behind `listen`; `mockDebugger.ts` `emulationStep` counts `arm7HeldFor` down only while running, `MOCK_ARM7_RELEASE_STEPS` = 8, `emulationStepMs` 16; `startPaused` sets `running` false with reason `start`; `m2.test.ts` pairs the two options. the ARM7 part of scenario S16, whose settling S17 and S21 share, begins with `ensurePaused`, which pauses a running mock wherever it is | 3.1 items 2 and 4 and Verifier, M3-D31, section 8 row 31, F4a brief ("What is known", items 4 and 6, Verifier) |
| F15 | must | S17's held-open rule fixed "the response comes, then a paused tick with reason `capture`", an order design 3.3 leaves open; the mock sends the response first, but the emulator publishes the stop on (E) and answers on (C) when it takes the notice, so E8b's gate could be red on a correct emulator with the fix in a repository E8b may not touch | **Taken as proposed.** Checked: design 3.3 ("its order against that paused tick is not specified") and 2.9 "Order"; `mockDebugger.ts` `recordSgcFrames` sends the response and then `enterStop("capture")`; S17's existing from-paused rule already waits for the tick from a mark set before the call, so only the new sentence fixed an order; `AudioPanel.tsx` awaits the response alone. The rule is now stated once in M3-D27 and named wherever the capture's end is | 3.1 item 3 and Verifier, M3-D27, M3-D32, section 8 row 30, F4a brief ("What is known", item 5, Verifier), E8b brief |
| I17 | should | I13 was settled in section 9 and the briefs but not in the workflow route section 4 allowed: `extra` is appended to `IMPL` only, while `COMMON` (every prompt), `IMPL` and `VERIFY` still name A.0 and `COMMON` writes `-config` | **Taken**, with a fifth place the finding did not list: `IMPL`'s "Your report is what A.0 asks for". Checked: `pkg-brief.workflow.js` (`COMMON`, `IMPL`, `VERIFY`; `REVIEW` and the fix prompt are built from `COMMON`). The `extra` route is withdrawn; the orchestrator edits the five places, or adds one `rules` argument to `COMMON`, before F4a. The workflow is not a file of this task, so the cut says what to change | section 4, section 9, row I13 |

Nothing of round 2 was rejected. Changes against the proposals: S16 (the `dbg_sgc_active` comment of H7-1 also names
the drain; `sgc.drain` may use `emu.hoststop`'s means instead of a new hook, and must show that its closure ran on (E),
or it would pass on `sgc.nothread`'s path), F14 (the verifier also checks the mutation the other way: `startPaused` off
with the delay goes red), I17 (five places, not four).

**Round 3: the owner's answers.** The owner answered M3-Q1 to M3-Q4 on 2026-10-16 (7.2); this round applies them to the
whole cut and to every brief. Two names changed with it: the baselines are B0 to B4 (B2 is now W2's merge taken by W3,
B3 W3's merge taken by G, B4 G's merge taken by E8a; the rows above that name B2 and B3 speak of round 2's B2, now B3,
and round 2's B3, now B4), and the contract's two required notes of run 2 are `sgc: captured with the arm7 held` and
`dsp: stepped with the arm7 held` (round 2's `sgc: held at the start` and `dsp: held at the start` in the rows above).

| Answer | What changed | Settled in |
| --- | --- | --- |
| M3-Q1 "Fix it in M3": the AICA clocked whatever ARMRST says; only the ARM7 stays unclocked | **A new package, W3**, between W2 and F4c / G (M3-D41): the burst makes its 32 time steps whatever `arm_on` says, the ARM phase only while the ARM7 is clocked; its gate is "identical where no burst finds the ARM7 held" (the guest), "`samples` and `dsp` must move, the rest only by a trace" elsewhere (2ndmix, hello), and the rule is held by the `held` targets (a held ARM7 is an idle one for the rest of the AICA) and `arm7.held.clocked`, and justified by caique's console cases, all run with the ARM7 held (`aica_io.h` `aica_reset`; `timer_irq.c`; the DSP cases; the stream replays; T1) (M3-D42, J4). **Revision 7** declares nothing for an unclocked DSP or capture: `dbg_dsp_OnBurstUnclocked` and `dbg::dsp::StepCheck` go, `dbg::sgc::Arm` loses `kErrNotClocked`, `kErrNotClocked`'s comment and `Step()`'s returns say the DSP never answers it, the DSP block says every step executes also while the ARM7 is held; a third clause of 2.6 (W3's "if the ARM7 is clocked, "); R7 keeps `-32012`'s one text (M3-D30). **W2** loses its silent pairs for a held burst (M3-D43). **F4a**: the mock's AICA clocked while its ARM7 is held (as it is today); S17 and S21 assert at the anchor that a capture and a DSP step work with the ARM7 held, with new notes; S21's settling becomes a wait for the program (M3-D44); the readers' broken server 6 inverted. **G**: based on W3, its schedule's reason (6.5), the comment of `_start`'s first release, the release kept where `held` patches it. **E8a**: no unclocked path; `dsp.bp.held`, `dsp.step.held` in place of `.unclocked`, `.notclocked`; the resume exemption not ended by a held burst. **E8b**: `sgc.held` a capture that records with the ARM7 held. **F4d**: the DSP's controls on whatever the ARM7 does (no "held state" for the DSP). Decisions: M3-D7 rewritten, M3-D27, M3-D30 to M3-D32 changed, M3-D41 to M3-D44 new | 1.1, 1.2, section 2 (rules, H7-1, H7-3, H7-4, 2.3, 2.4, 2.5, 2.6), 3, 3.0, 3.1, 3.5, 3.7 to 3.12, section 4, 5.1 to 5.3, 6.5, 7.1, 7.3, section 8 rows 2, 3, 10, 12, 14 to 16, 24, 26, 30, 31, 33, 36, 40, 42, 43, section 9; every brief but F4b's and F4c's, and the new `W3-BRIEF.md` |
| M3-Q2 "don't mind perf for now": no speed floor in M3 | Every floor, ceiling and threshold leaves the gates: W1's 2,300 and 48, W2's 48 and the floor on the ratio, E8a's 48 and its compiled-code check, E8b's 48, M3G's floor; W1, W2, W3 and M3G measure and report by the fixed method of 5.4 (two kinds of run, ratios of rounds in turn); E8a and E8b measure nothing (M3G counts them together); the lever (an empty-program path in the core) is not started and `dsp_core/` is no package's; E8b's mutation "outputs kept while no capture records", which only the host cost caught, is caught by `sgc.outputs` (0 while no capture records, H7-6) (M3-D15) | 1.2, 3.0, 3.3, 3.5, 3.8, 3.9, 3.11, 3.12, 5.4, M3-D15, 7.3, section 8 row 34, section 9; W1, W2, E8a, E8b, M3G and W3 briefs |
| M3-Q3 "Move to M4" | The recommendation stands and is no longer a question: no device write in M3, `DebugWriteReg`'s sentence written without its "if the owner answers otherwise"; no E8c | 1.2, 2.3, 3.1, 7.2, section 8 rows 4, 40; F4a brief |
| M3-Q4 "Not in M3" | The recommendation stands: no BIOS workload; a package of its own once the owner names a data path | 1.2, M3-D38, 7.2, section 8 rows 34, 40; W1 and M3G briefs |

The status line at the head of the file says what round 3 is; 7.2 records the questions with their answers, and nothing
in the cut or the briefs waits on the owner.

**The review of round 3.** Two lenses read the cut and the briefs as round 3 left them, against the trees and the
owner's answers: 12 findings, 5 + 7. A finding of both lenses is listed under the sound side and named under the
integration (I18 is S17; I20's first text is S21).

| # | Sev. | The finding, short | Outcome | Settled in |
| --- | --- | --- | --- | --- |
| S17 | must | `arm7.held.clocked` asks for "the ARM7's context as stored unchanged" and for the timer's request latched in the held bursts, which cannot both hold: the latch writes `regs[INTR_PEND]`, a word of the stored context. `dsp.bp.held` ("the ARM7's context unchanged") and the verifier lines "the ARM7's context moves" have the same defect | **Taken, with a change.** Checked: `update_e68k` (`hw/arm7/SoundCPU.cpp`) calls `backend->UpdateInterrupts()`, `ARM7Backend::UpdateInterrupts` (`arm7.cpp`) sets `regs[INTR_PEND] = e68k_out && armFiqEnable`, `INTR_PEND` is 47 of the `RN_ARM_REG_COUNT` words that `serialize` writes and `StoredWord` indexes; `SetResetState(1)` only clears `ctx.enabled`; `a_main_loop` runs with F clear outside `a_fwin`, `a_fiq` writes M before it returns; `ReadArm()` reads indices 0 to `SPSR_FIQ` (`kArmRegs`). The change: the hold is also made with none in service (`e68k_reg_M` 0: an ARM7 held inside `a_fiq` in front of its write of M gets a request raised and not latched), a hold that finds a request pending or in service is made at a later burst, and the expected observable is written out (`aica_interr` 1, `aica_reg_L` 2, `e68k_reg_M` 1, `e68k_reg_L` 2, `e68k_out` set; `INTR_PEND` set where the held ARM7's F is clear, 0 where it is set); "at least 13 samples" is met by one burst. 5.1's rule for a moved `arm7` names `regs[INTR_PEND]` (I18) | 3.12 item 5 and Verifier, 3.8 item 7 and Verifier, 5.1 (W3 row), 5.2 (J4); W3 brief ("What is known", item 5, Gate, Verifier), E8a brief (item 7, Verifier) |
| S18 | should | The channels of `arm7.held.clocked` "move on" can be red on a correct W3: the first channel at FNS 0 makes exactly one loop of 32 samples a burst, the second's loop closes for k a multiple of 8 (of 4 at FNS 0x200), and the AEG is held at full level; `MDEC_CT` is a 16-bit counter | **Taken as proposed.** Checked: `update_rate` = `(1024 \| FNS)` shifted by OCT (`sgc_if.cpp`), at OCT -1 512, 640, 768 or 896, so 16, 20, 24 or 28 samples a burst, integral, `caFraction` unchanged; the loop of `StreamStep` from LSA 0 to LEA 0x20; the table's AR 31 and no decay; `DspDebugState::mdecCt`, "its low 16 bits are the register" (H7-5) | 3.12 item 5; W3 brief item 5 |
| S19 | should | The runner's test of `held` cannot be built as written: `soundlines_test.py`'s stand-ins print one fixed line and its program is a text file, while `held` reads an ELF at the guest's labels, and a stand-in "whose copies agree" agrees in `arm7` too and fails the mode's own control | **Taken, with the halfword named.** Checked: `STAND_IN` and `program.elf` ("no program: a stand-in does not load it") of `soundlines_test.py`; `guest_labels`, `file_offset`, `guest_copy` of `soundlines.py`; `mov #0, r0` is the 16-bit `0xE000` at `g_main - 4` (`DSPTEST_G_MAIN` 0x8C010066, `_start`'s last three instructions). Item 4 says "halfword" where it said "word" | 3.12 item 4 and Owns; W3 brief (item 4, Owns) |
| S20 | should | On the guest no burst of S17's capture or of S21's step at the anchor finds the ARM7 held: `_start` releases it in its first slice, in front of the first tick (h = 0, W3's own premise). The contract holds that the requests are accepted while `ENABLED` reads 0, not the clock of a held burst; E8b's verifier expects S17 red for a capture without frames in held bursts; M3-D44 says the contract holds the answer to M3-Q1 | **Taken as proposed.** Checked: `dsptest.s` (`_start`'s first release is its eleventh instruction, the first tick comes at the end of the first slice, 448 cycles, by which the interpreter has executed 56; the head comment); 3.12 item 1. The notes keep their texts: they mean asked with the ARM7 held. M3-D44 also says why the two runs of run 2 stay (S21's first pause behind S17's capture finds `ENABLED` 1) | 3.9 Verifier, 1.1 (F4a row), M3-D32, M3-D44, 7.3; E8b brief (Verifier), W3 brief ("Who comes behind you") |
| S21 | should | The head comment of `dsptest.s` ("which gives the AICA its clock (no timer steps and no sample is made while the ARM7 is held), and waits until SAMPLE_DONE says that a tick has run") is false from W3 on and has no owner: only `_start`'s "the AICA has a clock from here on" is handed to G | **Taken as proposed.** Checked: `dsptest.s` lines 9 to 11; W3 may not edit the guest's committed files, G owns them | 3.0, 3.7 item 1, 3.12 item 3; G brief ("What is known", item 2), W3 brief ("What is known", item 3) |
| I18 | must | = S17, read for the integration: the sound CPU's save state stores `ctx.regs` (`REICAST_SA(ctx.regs, RN_ARM_REG_COUNT)`) with `INTR_PEND` in it; 5.1's W3 row names the interrupt words alone for a moved `arm7` | **Taken**, under S17: the ARM7's registers by `ReadArm()` are what stays, what the interrupt update writes (the five interrupt fields and `regs[INTR_PEND]`) is the observable | as S17 |
| I19 | should | The frozen comment of `dbg_arm7_OnBurstUnclocked` ("no instruction executes in this burst and no check is made"; "The burst is not suspended: the callback goes on with its rest") is false from E8a on, when the DSP phases of a held burst make the DSP's checks and can suspend it, and 2.3 lists it as unchanged: no package after R7 may edit it | **Taken, with a change.** Checked: `debugger.h` above `dbg_arm7_OnBurstUnclocked`; E8a's `dsp.bp.held`, `dsp.step.held` and `dsp.bp.resume`. The new text names the ARM7 in all five places where it means it: the checks, the instructions, the step (a DSP step armed at a held burst is not complete there, `dsp.step.held`), and the resume exemption (the DSP's is not ended by a held burst, `.resume`); and it says what the rest of the burst is by the block's introduction, so that it is true from R7 on (before W3 a held burst's rest is its tail, and the introduction's clause of 2.6 is W3's) and needs no clause of 2.6 | 2.3 (`debugger.h`, the paragraph "Unchanged"); R7 brief ("What is known") |
| I20 | should | Three texts that W3 makes false have no owner: (a) the head comment of `dsptest.s` (S21); (b) the guest README's row of the mode `sounds` ("fewer sample by sample, where no pair exists while the ARM7 is held"); (c) the rows `arm7.burst.plain` and `arm7.step.unclocked` of `tests/debugger/selftest/README.md` | **Taken as proposed.** Checked: the guest's `README.md` (the table of the modes; W2 owns only its render paragraphs), `tests/debugger/selftest/README.md` (the two rows: "no ARM phase, no time step and one tail while the guest holds it in reset"; "goes on with its tail") | 3.0, 3.12 item 3 and Owns; W3 brief (item 3, Owns, "What is known") |
| I21 | should | Section 4 and section 9 tell the orchestrator to edit five places of `pkg-brief.workflow.js` that the workflow no longer has: it takes the rules as `args.rules` (default A.0) and writes the switch with two dashes; nothing tells the orchestrator to pass the argument | **Taken as proposed.** Checked: `agents/nulldc3/resume/pkg-brief.workflow.js` (`const RULES = A.rules \|\| ...` used by `COMMON`, by `IMPL` twice and by `VERIFY`; `--config config:Headless=yes`, two dashes; `A.extra` appended to `IMPL` only) | section 4, section 9 |
| I22 | should | F4d's test of `runControls.ts` cannot hold "the DSP is never held": which target is held is decided in `RunControls.tsx` and `DisassemblyPanel.tsx`, and `stepOff` takes `held` as given; the verifier's mutation in `DisassemblyPanel.tsx` passes the unit test and the smoke spec, which steps the DSP from the toolbar only | **Taken, with a change.** Checked: `src/state/runControls.ts` (`stepOff`, `arm7Held`), `RunControls.tsx` and `DisassemblyPanel.tsx` (`target === "arm7" && arm7Held(...)`). The decision moves into `runControls.ts` (`heldFor`), held by a unit test. A component test of `DisassemblyPanel.tsx` is not possible in the UI's unit suite (`vitest.config.ts` runs in `node`, no `.test.tsx`, no DOM library), so the two callers are held end to end: an e2e test against the mock with `arm7Held` and `startPaused` (as `e2e/toolbar.spec.ts`'s held-ARM7 tests start it), and the smoke spec's held step made from the toolbar and from the DSP disassembly | 3.10 items 2 and 5, Owns, Verifier; F4d brief |
| I23 | should | E8b's configure line has a space inside `-DNULLDC_DEVTOOLS_DIR` | **Taken.** | E8b brief |
| I24 | should | The held-ARM7 test is called `arm7.notclocked` in eight places; the tree's is `arm7.step.notclocked` | **Taken.** Checked: `dbg_selftest_arm7.cpp` (`BOUNDED_GUEST_TEST(arm7_step_notclocked, "arm7.step.notclocked")`), `tests/debugger/selftest/README.md` | 3.0, 3.8 item 7 and Owns, I10 above; R7 brief (item 5), E8a brief ("What is known", item 7, Owns) |

Nothing of the review of round 3 was rejected. Changes against the proposals: S17 (the hold also with none in
service, a hold that finds either made again, the observable written out), S19 (the halfword), I19 (a text true from
R7 on, with the step and the resume exemption named as the ARM7's), I22 (the callers held by e2e, not by a component
test).
