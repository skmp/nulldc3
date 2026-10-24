# Track 1 — devtools integration: design

> **This copy.** `docs/nulldc3/devtools-integration-design.md` is the repository's copy of the working document
> `TRACK1-DESIGN.md`, revision 3, brought to the platform round at its end (package PLG, 2026-10-24). The text below
> names its companion documents by their working names; in `docs/nulldc3/` they are:
>
> | Named below | In `docs/nulldc3/` |
> | --- | --- |
> | `TRACK1-DESIGN.md` | `devtools-integration-design.md` (this file) |
> | `DESIGN-V3-OPEN.md` | `devtools-integration-open.md` |
> | `DESIGN-V3-DECISIONS.md` | `devtools-integration-decisions.md` |
> | `PLAN.md`, `agents/nulldc3/PLAN.md` | `PLAN.md` |
> | `M2-ROUND.md` | `devtools-integration-m2-round.md` |
> | `M3-ROUND.md` | `devtools-integration-m3-round.md` |
> | `PLATFORM-ROUND.md` | `platform-round.md` |
> | `TRACK1-DESIGN.v2.md` | not copied: revision 2 is the earlier text of this file, in its history (`git log -- docs/nulldc3/devtools-integration-design.md`) |
>
> `TRACK1-DESIGN.v1.md` (the draft) and `design-v3-check-r3.json` were never part of this repository. Other paths of
> the form `agents/nulldc3/...` point into the working area outside it (`docs/nulldc3/README.md`).

Status: v3 — decisions of 2026-10-09 and 2026-10-10 folded in; implementation contract; milestones M1, M2 and M3
complete (M3: the round's gate M3G, 2026-10-19); the platform round complete (its gate PLG, 2026-10-24). Supersedes v2
(`TRACK1-DESIGN.v2.md`), which superseded DRAFT v1 (`TRACK1-DESIGN.v1.md`). Owner of this file: the orchestrator.

What revision 3 is. Since v2 most of this design was implemented and independently verified. Revision 3 brings the text
to what was built where the two differed, folds in the decisions of `DESIGN-V3-DECISIONS.md` — the 29 of 2026-10-09 and
the 16 that followed the devtools round on 2026-10-10 (decisions 30 to 45) — and the direction the project owner set on
2026-10-08 (`PLAN.md`), and disposes of the verifiers' feedback. Section numbers are those of v2; new subsections are
added behind the existing ones. Appendix A.3 says which section each decision changed, where a decision was applied
with a difference, which feedback was not adopted, what the independent check of the revision changed, and which
sections the answers to the open questions changed. What is still open is in `DESIGN-V3-OPEN.md`, with the answered
questions and the comment-only header revision 4, which is applied and kept there as the record. Since then the
core (package E5) was built to this text, verified and merged: **milestone M1 is complete**, and the text is
brought to what was built wherever the two differed (6.1, "As built"; Appendix A.3, "E5 as built"). Behind M1 the
PLAN package P-desktop (desktop targets only) and package J1 (the run flag, the code cache, `IsRunning()`; header
revision 5) were built, verified and merged, and the text is at that state too (6.3; Appendix A.3, "J1 as built").
Then the M2 round was cut (`M2-ROUND.md`, which was the contract wherever it and this text differed), built, verified
and merged, and its gate (M2G) passed: **milestone M2 is complete**. The text is brought to the cut by the 73 rows of
its section 7 and to what the round built, with the departures and decisions the integrator accepted and the known
limits the verifiers found (Appendix A.3, "M2 as built"; 2.3.7 items 14 to 24). After M2G the design, the decisions
and the frozen headers cite `M2-ROUND.md` for header revision 6 (its section 2), for the M2 tree (its 3.6.1) and for
the guest's contract (its 5.2). Then PLAN's rebrand (package N1: the binary `nulldc3`, portable by default, two-dash
switches, the UI as the submodule `devtools/`) and package D1 were built and merged, and the M3 round was cut
(`M3-ROUND.md`, which was the contract wherever it and this text differed), built, verified and merged, and its gate
(M3G) passed: **milestone M3 is complete**. The text is brought to the cut by the 43 rows of its section 8 and to what
the round built, with the departures the integrator accepted and the known limits the verifiers found (Appendix A.3,
"M3 as built"; 2.3.7 items 25 to 30). After M3G the design, the decisions and the frozen headers cite `M3-ROUND.md` for
header revision 7 (its section 2), for the M3 tree and for the guest's DSP program (its section 6). Then the platform
round was cut (`PLATFORM-ROUND.md`: the rebrand, the icons, the versions, Windows, macOS, Linux arm64, CI and releases;
it was the contract wherever it and this text differed), built, verified and merged, with package A64 (the arm64
recompiler's native debugger: 2.3.5, 6.3) added to it at the owner's word before its gate, and its gate (PLG) passed:
the text is brought to the round by the 16 rows of the cut's Appendix D (D4, D7, D10; 2.3.4 f and g, 2.3.5, 2.3.7
items 31 to 33; 3.1; 5.4 to 5.8; 6.3; Appendix B), as A64 left the tree where a row was written before A64 (rows 1,
3, 8 and 13). After PLG the design and the frozen headers cite `PLATFORM-ROUND.md` for header revision 8 (its 2.3;
H8-5 A64's), for the version string (its 5.1) and for the platform decisions (PL-D1 ...). What comes next is the cut
of M4.

Inputs: the reader reports in `agents/nulldc3/understand/` (cited, not re-derived): `devtools-protocol.md` (wire spec of
protocol v2), `devtools-mock.md`, `devtools-panels-debugger.md`, `devtools-panels-tools.md`, `devtools-build.md`,
`nulldc-rust-backend.md`, `devcast-debug-surface.md`, `devcast-build.md`; the plan, `agents/nulldc3/PLAN.md`; the three
reviews of v1 in `agents/nulldc3/review/` (dispositions in Appendix A.1); the specification of the SH4 breakpoint
mechanism with its prototype (in the emulator repository: `docs/nulldc3/sh4-breakpoints-spec.md`; results in
Appendix C); for revision 3: `agents/nulldc3/DESIGN-V3-DECISIONS.md`, the code as merged (the headers of
`libswirl/debugger/`, `hw/sh4/sh4_dbg.h`, `hw/sh4/sh4_if.h`, `hw/sh4/dyna/decoder.h`, the READMEs under
`tests/debugger/`, `docs/nulldc3/automated-runs.md`; in the UI repository `src/lib/debuggerSchema.ts`,
`src/lib/binaryFrame.ts`, `scripts/contract/`, `server/mockDebugger.ts`, `README.md`), and the verifiers' reports under
`agents/nulldc3/resume/`, `e0/`, `integ-1d/` and `b64/`; for decisions 30 to 45 the verdicts of the devtools round
and of the corrections of 6.1.1 (`agents/nulldc3/resume/round3.final-result.json`).

Repositories and branches (packages work on local branches and push nothing; the integrator pushes `skmp/nulldc3` of both
repositories after a milestone, D5):
- emulator: repository `devcast`, integration branch `skmp/nulldc3`. One git worktree per work package under
  `agents/nulldc3/wt/<pkg>` on branch `skmp/nulldc3-<pkg>`, own out-of-tree build directory; an integrator merges.
- web UI: repository `dreamcast-devtools`, branch `skmp/nulldc3` (frontend packages are sequential on it).

Decisions in force (project owner and design owner; everything below is written to them):
- **D1 — what you debug is what runs.** Breakpoints and single steps are native to the active SH4 backend; nothing switches
  engine when a breakpoint is armed, and scheduler time is what the active timing tier charges (2.3, 2.4). An interpreter
  fallback exists only for a JIT backend that does not implement the native check yet; the UI says so when it is in use.
  Three points of D1's wording were made precise by the prototype (Appendix A.2), and the mechanism as built is the
  specification wherever v2's text differed from it (2.3.4).
- **D2 — one DSP.** The DSP that the debugger steps, the emulator runs and the devtools playground executes is the single
  caique-matching C++ DSP core of PLAN T5.0 (merged as `libswirl/hw/aica/dsp_core`; wired into the emulator's AICA by
  packages W1 and W2 of the M3 round and into the playground by F4b). The DSP is always on in nullDC 3: there is no
  "Enable DSP" setting. There is no DSP-less path: the AICA renders every sample with the DSP, and every sample of every
  burst, also while the guest holds the ARM7 (W3).
- **D3 — performance figures** are taken with the hardware GL renderers or the `none` renderer, never the reference
  software renderers, and state their renderer.
- **D4 — naming.** New surfaces say nullDC 3.0.0, and where they show a version it is the build's (3.1). The tree was
  renamed by the platform round (`PLATFORM-ROUND.md` 2.5, PL-D9 to PL-D13): the binary is `nulldc3` (PLAN's N1), the
  platform shell `reicast/` is `nulldc3/`, and the names that said reicast say nulldc (`nulldc_init`, `nulldc_term`,
  `nulldc_root_path`, `NULLDC_VERSION`, `NULLDC_S`, the Lua module `nulldc3`); "reicast" stays only as history (the
  licences, credits, provenance notes, the documents that record history, git history), and `rebrand.names` holds it.
  Names derived from reicast that do not say it stay (`rc_serialize`, `reios`, `reprof`).
- **D5 — commits.** Packages commit on local branches and push nothing; after every milestone the integrator pushes
  `skmp/nulldc3` of both repositories and opens review PRs into `main`, the UI's first (the owner, 2026-10-16); nothing is
  merged by the integrator. The platform round ends the same way and then, after the owner's two merges and with the
  owner's go, with the annotated tags `v3.0.0-pre` of both repositories, the UI's first at the commit the emulator's
  gitlink names (`PLATFORM-ROUND.md` section 7); CI makes the two Releases from the tags (6 of the cut).
- **D6 — the shared interfaces are frozen**: the API of E1 and E2, which started first (Appendix B), and the headers E0
  delivered. Header revisions 2 and 3 are part of the contract (Appendix B). Revision 4 is comment-only, changed no
  declaration and is applied; its list is kept as the record in `DESIGN-V3-OPEN.md`. Revision 5 came with package J1
  and is applied: one declaration, the run flag as an `AtomicU32`, and one reworded comment (Appendix B). Revision 6
  came with the cut of the M2 round (`M2-ROUND.md` section 2, package R6): the declarations of the ARM7's run control,
  the new frozen header `dbg_arm7.h`, and the comments M2 changes. Revision 7 came with the cut of the M3 round
  (`M3-ROUND.md` section 2, package R7): the declarations of the DSP's run control and of the SGC capture, the new frozen
  headers `dbg_dsp.h` and `dbg_sgc.h`, the datapath of `DspDebugState`, the channels' outputs of `AicaChannelDebug`, the
  comments M3 changes; the forcing names removed. Revision 8 came with the platform round (`PLATFORM-ROUND.md` 2.3,
  packages RB, VTe and A64): comment-only, the names of the entry points (`nulldc_init`, `nulldc_term`), the paused
  title's version and the arm64 JIT's `CtxAtSlice` (H8-5). A declaration changes only by an explicit, listed approval of
  the integrator.
- **D7 — targets.** Desktop only (Linux, Windows, macOS), plus wasm later. Processors: x86_64 and arm64 now; later a
  wasm32 build with its own JIT, possibly riscv64. There are no 32-bit native builds, no cached-SHIL `cpp` backend and no
  libretro core. What exists: the x64 and arm64 JITs and the interpreter. New code stays clean for a 32-bit address space
  (no assumption that pointers are 64 bits, no reliance on huge virtual mappings, host signals or RWX pages in portable
  parts), and the interpreter-only generic host path stays buildable. The tree is at this since PLAN package
  P-desktop (merged): what it builds for is desktop Linux, Windows and macOS on x86_64 and arm64, the generic host
  path, and the emscripten scaffolding the wasm build starts from; every other target, host and build switch is
  gone from it (6.3). **The release targets** (the owner's decision of 2026-10-19, `PLATFORM-ROUND.md` 1.3 item 3):
  Linux an AppImage only, x86_64 and arm64, portable (the folder that holds the AppImage is its portable folder; no
  `.deb`, no per-user default); Windows a zip with `nulldc3.exe` (x86_64); macOS an ad-hoc signed `nulldc3.app` in a zip
  (arm64 only). Since the platform round all four are built and tested on their systems (Windows with MSVC, W64a and
  W64b; macOS with AppleClang, MACa and MACb; Linux arm64 natively, LA64) and made by CI on pull requests and tags
  (CIe, 6.3). arm64 is built on Linux and macOS since that round, the SH4 recompiler with the debugger's native
  checks since its package A64 (`sh4-debug-native`, 2.3.5; until A64 the SH4 fallback engine was its debug engine),
  and with no ARM7 recompiler (`FEAT_AREC` none on every host, `PLATFORM-ROUND.md` PL-D46: the ARM7 runs on its
  interpreter; the arm64 ARM7 recompiler is unsafe on Apple silicon as written and stays unbuilt, PLAN's backlog).
  macOS x86_64 stays source-consistent and unbuilt.
- **D8 — the server is the control surface, not only a debugger** (PLAN track 10). It works with no game loaded, nothing in
  it assumes that a client is a debugging session, and its configuration keys stay under `Debugger:` for now (2.11). The
  emulator's own imgui GUI, its game list and save / load states are leaving the product; what this design says about
  them describes the tree as it is today and is marked transitional.
- **D9 — the tier picks the engine.** Timing tiers (`fast`, `cycle-similar`, `cycle-accurate`) are run-time settings per
  processor (PLAN ground rules). There is no user-facing interpreter choice: the interpreter stays as the reference of the
  test suites, as the breakpoint fallback of a JIT backend without native checks, and as the start of new host ports; its
  config key (`config:Dynarec.Enabled=no`) remains for tests and is not shown.
- **D10 — every emulator run of a test, a gate or an agent is headless** (`config:Headless=yes`,
  `docs/nulldc3/automated-runs.md`), and agent runs keep to the machine limits: one workflow at a time, `ninja -j8`,
  `ctest -j4`, at most 4 emulator processes. `config:Headless` exists on Linux, Windows (W64a) and macOS (MACa) since
  the platform round: no window, no Dock icon, no dialog of any kind. Windows has a no-dialog policy for every run
  (`PLATFORM-ROUND.md` PL-D34: the error mode, WER without UI, the CRT's handlers, a vectored fatal handler, no
  `__debugbreak` without a debugger), Darwin ends a fatal signal of an automated run with `_exit(128+N)` so that no
  crash report is written (PL-D45), and both print Linux's fatal line, `fault_handler: unhandled <SIG> at address <a>,
  pc <p>, not a guest memory fault: terminating`, with 128 + the signal's number (PL-D35). On the remote systems every
  program of an automated run on Windows runs inside `nulldc3-winrun` (`tests/winrun`: an error mode, a job object, a
  private desktop, a timeout), and every emulator run on the Mac goes through `tests/macos/macrun.py` under the sandbox
  profile that denies the window server (PL-D69; `docs/nulldc3/automated-runs.md`). The window items of the Windows
  run (5.8) and of the window sessions are the owner's, by hand (PL-D40).

Terms: an **in-band error** is a successful JSON-RPC response whose result is `{"error":{"code":n,"message":"..."}}`; a
**JSON-RPC error** is a response with a top-level `error` member. Which one a method uses is fixed per method (3.5).
An **automated run** is a run with `config:Headless=yes` or with one of the keys `config:Exit.AfterVblanks`,
`config:Exit.AfterSeconds`, `config:Exit.OnGuestExit` (the rule of `libswirl/utils/run_control.cpp`). A **gate** is a
contract run that names a milestone (5.5.3).

## 1. Goal and milestones

The web UI talks to a server inside the emulator and every panel shows real state.

| Milestone | Content | Gate | State |
| --- | --- | --- | --- |
| M0 | devtools hygiene: lint/typecheck/test/build green, real scripts, versions, docs; a fresh clone builds without Rust; the wasm-transport build keeps building | `npm run check` = lint + typecheck + test:run + contract (mock, `--milestone M1`; `--milestone M2` since F3a) + build:native + build:wasm | done (`check` runs `build:wasm` and the contract as the M1 gate since the devtools round, 4.1) |
| M1 | protocol v3 (schema, client, mock module) and the contract runner. Emulator: transport and embedded UI, lifetime states and park loop, SH4 debugging (registers, memory, disassembly, run / pause / step / over / out, native code breakpoints, event breakpoints, watches, callstack, event log), the reduced M1 device tree (2.8), all three targets listed. Frontend: endpoint selection and reconnect; disassembly follows the PC and memory refetches when `stopId` changes; per-target defaults from `targets`; results of `control.*` shown; `reason` and `notice` in the toolbar; error boundary per dock panel; device-tree panel rework; notices for unsupported targets | the contract gate M1 (5.5.3) passes against the mock and against the emulator: scenarios S01-S14, S19, S20 on the mock, S00-S15, S19, S20 on the emulator; pure-module tests; the in-emulator selftests in ctest (`debugger.selftest.*`, the session `regs` among them), which include the breakpoint and step gates of 2.3.6 (B11 is `debugger.sh4bp.b11` in the pure suite); Windows gate (5.8) | **done: M1 is complete.** The core (E5, 6.1) is verified and merged: the M1 gate passes against the emulator on the JIT, on the interpreter and with the forced fallback, 18 scenarios passed, 0 partial, 0 not applicable each, and against the mock, 16 of 16; 187 ctest targets pass. The Windows gate was met in the form 5.8 gave it then (a syntax-only compile and a review); the real MSVC build and the Windows run came with the platform round (W64a, W64b; 5.8, 6.3) |
| M2 | ARM7 step / breakpoints (resumable sound burst); memory writes; register writes for the SH4 and the ARM7; reset; the full register tables (SB, PVR, AICA common and channels, GD-ROM, maple) read-only in the device tree; the generic register panel and the panel ids `sh4-registers`, `arm7-registers`; toolbar run control with hotkeys | contract gate M2 in its two runs (5.5.3); sound lines identical before / after with the sounding guest, and with the debugger in use on it (5.6); `npm run check` with the mock's M2 gate, and the e2e suite | **done: M2 is complete.** The round's gate (M2G, 2026-10-15) ran on `devcast` `6fe01759` with the UI at `d59336b` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+d59336b`): the M2 gate passes against the emulator on the JIT, the interpreter and the forced fallback (19 scenarios passed, 0 partial, 0 not applicable each) and its second run against the sounding guest (2 passed, both notes); the 259 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix equal those of the tree before E6b, and the state hashes of 5.7 those of the tree before the round, in the five builds; `npm run check` and the e2e suite pass in a fresh clone; the smoke spec passes against a headless emulator on the guest (5.4, 5.6, 5.7, 5.1) |
| M3 (on the T5.0 core, wired into the AICA by the round) | SGC capture; DSP single-step / breakpoints / disassembly on the one C++ DSP core; playground runs the emscripten build of the same core and loads its program from the live emulator | contract gate M3 in its two runs (5.5.3) and the M2 gate's second run on the changed guest; sound lines and state hashes identical before / after, with the DSP test ELF and with the debugger at work on its DSP; the lines that the DSP program moves justified by the caique model (5.6); the AICA clocked whatever ARMRST says, held by the `held` targets (5.6); the playground's module equal to the native core (`aica.dsp.wasm.devtools`); `npm run check` with the mock's M3 gate, the e2e suite, the smoke spec; speed measured and reported (5.7) | **done: M3 is complete.** The round's gate (M3G, 2026-10-19) ran on `devcast` `9fbc988a` (its documents behind it, on the branch `skmp/nulldc3-m3g`) with the UI at `066fcbc` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+066fcbc`): run 1 of the M3 gate passes against the emulator on the JIT, the interpreter and the forced fallback (20 scenarios passed, 0 partial, 0 not applicable each), run 2 against the sounding guest on the JIT and the interpreter (S00, S17, S21: 3 passed with the note `sgc: captured with the arm7 held`; `--only S00,S21`: 2 passed with the note `dsp: stepped with the arm7 held`), and the M2 gate's second run on the changed guest (2 passed, both notes); the 307 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix and the guest's exit state hash equal B4's (12 of 12 entries, the default build and `-DNULLDC_DEBUGGER=OFF`), and so do the state hashes of 5.7 (38 of 38 rows, 2ndmix 600 in the five builds); `npm run check` (735 unit tests passed, 1 skipped; the four contract runs on the mock OK) and the e2e suite (401 passed, 1 skipped: the smoke spec, run on its own against the emulator) pass in a fresh clone of the UI; the smoke spec passes against a headless emulator on the guest; ThreadSanitizer of the sessions `dsp` and `sgc` and of the contract runs on the guest (interpreter) gives no report; speed is measured and reported (5.7) |
| M4 | Holly: CORE inspector fed by an emulator capture, TA inspector, table panels (TLB / SQ / OC-RAM, caches), TA / CORE event-breakpoint controls in the inspectors | contract gate M4 (adds S18) | to do |
| M5 | SH4 simulator panel re-based on the measured model (moves with tracks 3/4) | vitest fixture gate, 300/305 or better | to do |

M0 and M1 are the first implementation round, and it is done; so are the two packages that followed M1, the PLAN
package P-desktop (desktop targets only) and J1 (6.3), the M2 round (`M2-ROUND.md`; 6.2, 6.3) and the M3 round
(`M3-ROUND.md`; 6.2, 6.3). M4 follows; each milestone is independently shippable. Offline Monaco and
route-level code splitting, planned for M2 in v2, were delivered with F0 (4.1).

PLAN track 10 (one web UI: settings, machine lifecycle, input mapping, notifications; the imgui GUI goes) starts after
M1 and extends protocol v3 by new methods under the rules of section 3. It is not specified here; what this design owes
it is D8.

### 1.1 Explicitly out of scope for M0..M4

| Item | Source | State after M4 |
| --- | --- | --- |
| Conditional breakpoints, hit counts | IDEA.md:6 | not in the protocol, no UI field. The native check already calls a helper (2.3.4 c), so adding them later needs no recompile |
| Run "until next interrupt / exception" | IDEA.md:6 | only as an event breakpoint (`dc.sh4.interrupt`, `dc.sh4.exception`); no command; the stop is up to one slice late (2.2.7) |
| CORE "step to next primitive / tile", TA "step until next vertex / polygon" | IDEA.md:6 | list / render granularity only (events of 2.6) |
| "Step a sample in AICA" | IDEA.md:6 | no command; a DSP breakpoint on step 0, or a step of the DSP from step 127, which ends at the end of the sample, is the nearest thing (M3) |
| The step view of the DSP (`AicaDspStepView`: its operands, `SHIFTED`, `X`, `Y`, `B`) on the wire | 2.5.4, `M3-ROUND.md` M3-D25 | not planned: no wire form exists and nothing of M3 needs it |
| DMA step / run | IDEA.md:6 | transfers are atomic; start / end events only |
| Memory / disassembly failing on TLB misses | IDEA.md:4 | no address translation: `WITH_MMU` builds are read through the same fixed map as `NO_MMU` builds (the default); `validity` is always `ok` |
| ICACHE / OCACHE tables | IDEA.md:4, panels `icache-contents`, `ocache-contents` | the `fast` tier has no cache model: `state.getTable` returns no rows and a `note`. From `cycle-similar` on (PLAN T3.3b: real instruction and operand caches) the tables show the real lines; that part moves with track 3 |
| TLB table | IDEA.md:4 | meaningful only in `WITH_MMU` builds; otherwise a `note` |
| "Hear a section of audio", per-stage waveforms | IDEA.md:4 | playback not planned |
| Threads list | IDEA.md:9 | not planned |
| Debugger state surviving an emulator restart | IDEA.md:8 | breakpoints, watches and categories are in memory only |
| wasm / cross-tab transport as a server of this emulator | IDEA.md:2, PLAN track 8 | not in M0..M4. `build:wasm` keeps building and one vitest covers `BroadcastChannelTransport`. The wasm32 emulator build of track 8 puts another transport under the same core: nothing above `dbg_net` may assume a socket (2) |
| Settings, machine lifecycle (load by path, start, stop, reset, exit), input mapping, notifications over the protocol | PLAN track 10 | not in M0..M4; they extend protocol v3 later. `control.reset` (M2) is the debugger's reset, not the lifecycle method |
| Save / load state, the game list | PLAN track 10 | no protocol method, now or later in this track: both leave the product with the imgui GUI. The "guest state replaced" hook (2.2.1) stays for when states return |
| Writes of device registers (system bus, PVR, SH4 on-chip modules, AICA, RTC, GD-ROM, maple) | 4.4 ("edit through `state.setRegister`"), `M2-ROUND.md` M2-Q1, `M3-ROUND.md` M3-Q3 | not in M2 or M3: the rows are read-only (`-32013`). Each needs its write semantics stated; every device's write table comes with M4 (the owner's answer to `M3-ROUND.md` M3-Q3, "Move to M4"; package E9 states the AICA's with the others) |
| A user-facing choice of SH4 engine | D9 | none. `dc.timing.SH4_BACKEND` and `SH4_DEBUG_ENGINE` say what executed |
| Native SH4 breakpoints in the arm64 JIT backend | D1 | done in the platform round: the arm64 JIT has them since package A64 (2.3.5; `sh4-debug-native` on the Mac and on Linux arm64), the x64 JIT on Windows since W64b (5.8). A new JIT port uses the interpreter fallback with a notice until it implements the items of 2.3.5 |
| ARM7 native breakpoints on hosts with the ARM7 JIT (arm64) | D1 | interpreter fallback with a notice (2.5.3); no build has an ARM7 JIT since the platform round (`PLATFORM-ROUND.md` PL-D46), so the fallback is not reached |
| SH4 simulator panel | route `/tool/sh4-sim` | M5 |

## 2. Architecture (emulator side)

Directory `libswirl/debugger/`. Language: **C++20** (`-std=c++20`, extensions off, plus `-fno-operator-names
-fpermissive` on GCC). Debugger code must also be valid without `-fpermissive` semantics, because MSVC `/std:c++20` implies
`/permissive-`.

Build: `libswirl/debugger/debugger.cmake` is `include()`d once from `CMakeLists.txt` and contains everything else: the
source glob (evaluated at configure time: re-run CMake after a merge that adds a file), the option, the UI embed step
(2.11; its scripts are in `libswirl/debugger/cmake/`), `ws2_32` on Windows and the definition `NULLDC_DEBUGGER=1`. The
protocol number has one source, `dbg::kProtocolVersion` in `dbg_webui.h`; the embed check reads it from that line.
`cmake_minimum_required` stays 3.10; embedding the UI needs CMake 3.19 (`string(JSON)`, `file(ARCHIVE_CREATE)`), and an
older CMake builds the debugger without the bundle. Option `NULLDC_DEBUGGER`: it has one default, ON, also on the
interpreter-only generic host path. (Until PLAN package P-desktop it defaulted to OFF for three build switches of
targets that are gone with that package; a configure that still passes one of the three stops with an error instead
of building the desktop emulator.) It stays an option: the tests project
and the no-regression gates of 5.7 build with it OFF. When OFF, `debugger.h` provides empty inline stand-ins (the SH4
breakpoint query is constant false, so no check is ever compiled) and no file of `libswirl/debugger/` is compiled. The
CMake build is the only build of the tree (the make build went with package B64).

Vendored: `libswirl/deps/nlohmann/json.hpp` and `json_fwd.hpp` (nlohmann/json 3.11.3, MIT). SHA-1 for the WebSocket accept
key comes from `libswirl/deps/crypto/sha1.h`.

Rules for every debugger source file:
- `json.hpp` (7 to 9 s and about 350 MB per translation unit) is included by `dbg_core.cpp`, `dbg_main.cpp` and
  `dbg_selftest_core.cpp` (which reads the ticks and responses it checks) only and by no header; a header that needs the
  type includes `json_fwd.hpp`. `json.hpp` is always included before any tree header.
- Never include `hw/sh4/sh4_core.h` (it `#define`s `r`, `pr`, `sr`, `fr`, ...; `json.hpp` after it does not compile). Use
  `p_sh4rcb->cntx.*` and `sh4_sr_GetFull/SetFull` from `sh4_if.h`, and `UpdateSR()` / `UpdateFPSCR()` /
  `sh4_host_fp_resync()` from `hw/sh4/sh4_dbg.h`.
- Never call `ReadMemN/WriteMemN` with a debugger-supplied address (they spin forever or abort on several inputs;
  devcast-debug-surface.md 5.3). Use `dbg_mem`.
- The rest of the emulator includes only `debugger/debugger.h` (no JSON, no STL containers; `<atomic>` and the tree's
  `types.h`, which every emulator file includes already and which supplies `DYNACALL` for the helpers that generated
  code calls, 2.3.4 l). A file that includes a picoTCP header cannot include `debugger.h` (picoTCP defines `dbg` as a
  macro); `debugger.h` stops the compile there.
- Enumerators and identifiers in debugger headers avoid the `<Windows.h>` macro names (`ERROR`, `IN`, `OUT`, `DELETE`,
  `small`, `near`, `far`, `interface`).
- `dbg_net.cpp` includes no tree header except `debugger/dbg_net.h` (which uses `<cstdint>` types, not `types.h`) and
  `deps/crypto/sha1.h`, uses `std::thread` / `std::mutex`, and on Windows includes `<winsock2.h>` and `<ws2tcpip.h>` as its
  first lines and calls `WSAStartup(2.2)` itself at server start. Nothing above `dbg_net` includes a socket header or
  assumes that the transport is a socket (1.1, wasm).
- No debugger module caches a pointer into guest state across a lifetime change (2.2.1).
- No debugger module assumes that a client is a debugging session (D8): connecting a client stops nothing and changes
  nothing in what the machine executes; what depends on a client being connected is listed in 2.9 (event gates, the
  activity of breakpoints, the grace period) and nothing else does. Every request has a defined answer with no game
  loaded (2.2.1).
- Nothing of track 1 writes a configuration key (`cfgSave*`): a `Debugger:` key exists only where a person or a harness
  put it (2.11 relies on that). A key a person saves through the settings methods of track 10 is put there by a person:
  the rule does not forbid those methods, it forbids code of this track that saves a key by itself.
- Debugger code keeps to D7: no pointer is stored in 32 bits or assumed to have 64, and nothing outside `dbg_net.cpp`
  needs sockets, signals or executable memory.

### 2.1 Modules

| File(s) | Responsibility | Package | Pure (links without the emulator) |
| --- | --- | --- | --- |
| `debugger.h` | The only header emulator code includes: `g_attn`, the stop latch, `DBG_EVENT`, the lifetime hooks, `Park()`, the SH4 backend interface of 2.3.4 l (`dbg_sh4_*`), `dbg::Init/Term`; inline stand-ins when `NULLDC_DEBUGGER` is OFF | E0 (frozen) | — |
| `dbg_net.h/.cpp` | HTTP/1.1 static file server and RFC 6455 WebSocket server on one listening socket, own thread (Appendix B); text and binary messages counted apart (E8b) | E1 (done); E8b | yes |
| `dbg_webui.h/.cpp`, generated `dbg_webui_data.cpp` | Embedded UI bundle lookup (`path -> gzip bytes`), bundle info (`Embedded()`, `Version()`, `GitRev()`), `dbg::kProtocolVersion`, built-in fallback page | E1 (done) | yes |
| `debugger.cmake`, `cmake/dbg_webui.cmake`, `cmake/dbg_webui_embed.cmake` | Build integration (above) and the embed step (2.11) | E1 (done) | — |
| `dbg_disasm.h/.cpp` | Disassemblers: SH4, ARM7DI (ARM state), AICA DSP step (Appendix B) | E2 (done) | yes |
| `dbg_events.h` | Event id enumeration and the event table (name, subsystem, severity, rate class, node) | E0 (frozen) | yes |
| `dbg_events.cpp` | The event gates `g_event_on`, `EventSlow`, the event ring behind its own mutex (2.6), and the two entries of the private header (`events::Snapshot`, `events::Append`) | E5 (done) | no |
| `dbg_regs.h`, `dbg_regs_tables.cpp` | Constant node and register tables (SH4 context and on-chip modules, Holly interrupt registers, AICA / ARM7 / DSP, timing; the M2 rows of `M2-ROUND.md` 3.6.1) | E0 / E3 (M1), E7b (M2) (done) | yes |
| `dbg_regs.cpp`, `dbg_regs_private.h` | Side-effect-free register readers; the writer of the registers of the two processors (M2) | E3 (M1), E7a, E7b (M2) (done) | no |
| `dbg_mem.h/.cpp` | Side-effect-free guest memory read for targets `sh4`, `arm7`; write (M2, 2.7) | E0 / E3 (read), E7a (write) (done) | no |
| `dbg_emu.h/.cpp` | Emulator-thread integration: lifetime, call queue, slice hook, stop latch, park loop, run-control sequencing; the shared types and wire constants (`Target`, `Category`, `StopCause`, `ErrorCode`, bounds) | E0 / E4a (done) | no |
| `dbg_sh4.h/.cpp` | SH4 code-breakpoint state used by the backends (2.3.4): armed set and page bitmap, the check helpers, resume exemption, owed remainder, temporary breakpoints, step-over / step-out state, step preconditions | E0 / E4b (done) | no |
| `dbg_arm7.h/.cpp`, `dbg_arm7_private.h` | ARM7 breakpoint state (2.5.3): the listed set, the mask (`MaskAddr`), the temporary breakpoint, the resume exemption, the step; called by the sound burst through `debugger.h` (`dbg_arm7_armed`, `dbg_arm7_Check`) and by `dbg_emu`, `dbg_regs` | R6 (frozen header) / E6b (done) | no |
| `dbg_symbols.h/.cpp` | Symbol table from the ELF loaded by reios (own `Elf32_Sym` walk; the vendored libelf has no symbol API) | E0 / E5 (done) | yes |
| `dbg_callstack.h/.cpp` | SH4 / ARM7 unwinders | E0 / E5 (done) | no |
| `dbg_dsp.h/.cpp`, `dbg_dsp_private.h` | DSP breakpoint state (2.5.4): the listed set, the mask, the resume exemption, the step; called by the sound burst through `debugger.h` (`dbg_dsp_armed`, `dbg_dsp_Check`, `dbg_dsp_SampleEnd`) and by `dbg_emu` | R7 (frozen header) / E8a (done) | no |
| `dbg_sgc.h/.cpp`, `dbg_sgc_private.h` | The SGC capture (3.3, 3.4): the frame of a sample, a capture's buffer of its own and the server's, the completion notice; called by the sound burst through `debugger.h` (`dbg_sgc_active`, `dbg_sgc_Frame`), by the core and by `dbg_emu` | R7 (frozen header) / E8b (done) | no |
| `dbg_core.h/.cpp`, `dbg_core_private.h` | The core thread; JSON-RPC dispatch; breakpoint / watch / category tables; device tree; tick builder. The private header (E5's, not frozen, no JSON types) is what `dbg_core.cpp`, `dbg_main.cpp`, `dbg_events.cpp` and the selftests `dbg_selftest_core.cpp`, `dbg_selftest_arm7.cpp` (for `events::Snapshot`) and `dbg_selftest_writes.cpp` share (6.1); it has the test hook `core::SetTickSinkForTest` (E7a: one atomic load on the path without a server, null outside a selftest); through it `dbg::Init` hands the core its tick configuration (`core::Configure`, 2.11) | E0 / E5 (done) | no |
| `dbg_main.cpp` | `dbg::Init/Term`: config (every key of 2.11 that `dbg::Init` reads; the two tick keys go on to the core through `core::Configure` of the private header), the server object, wiring net <-> core <-> emu | E5 (done) | no |
| `dbg_selftest.h/.cpp`, `dbg_selftest_*.cpp`, `dbg_selftest_sh4bp_guest.inc` | In-emulator selftest driver and per-package test files (5.3) | E0 / E3, E4a, E4b, E5, J1; E6b, E7a, E7b, EXe; R7, W1, W2, W3, E8a, E8b (done) | no |

The private headers (`dbg_*_private.h`) are not frozen (`M3-ROUND.md` M3-D40): they are the test hooks and the shared
internals of their modules, and a package edits one only where its brief says so. The frozen headers are those Appendix
B lists.

### 2.2 Threads, lifetime, calls, stop arbitration

Threads: (U) main / UI thread; (E) emulator thread (`Dreamcast_impl::dc_run` and everything it calls, generated code
included); (N) network thread (`dbg::net::Server`, socket I/O only — its `Handler` callbacks only enqueue); (C) debugger
core thread (started by `dbg::core::Start`: parses requests, owns the timers for the periodic tick, the capture timeout
and the client-absent grace period, and calls `dbg::emu::Call`). In a headless run the thread that would draw the user
interface only starts the program and waits; what (E) queues for it runs on (E) itself.

**The one rule: guest state and the debugger's own tables (breakpoints, watches, categories, capture buffers) are touched
only on (E), or on another thread that holds the lifetime lock L while no emulator thread exists.**
(Reasons: JIT register caching, per-thread MXCSR set by `UpdateFPSCR`, the non-re-entrant SIGSEGV handler, unsynchronised
block manager / scheduler / devices — devcast-debug-surface.md.) Two stated exceptions: the event ring has its own small
mutex (2.6); VRAM and `pvr_regs` are read and written by (U) and the refsw workers while a frame is queued, so debugger
reads of them can observe a frame in progress, and M4 captures wait for the queued frame.

Contexts on (E): the **slice hook** runs inside `UpdateSystem()`, between two slices of guest code; a block may be on the
host stack (the SLEEP handler calls `UpdateSystem()` from inside one). The **park context** is in `dc_run` after
`SuperH4::Run()` returned and before it is called again: no guest code is on the host stack and the SH4 context in
`Sh4cntx` is complete. The **park loop** is the part of it that waits while a debugger stop is in force; a **service
excursion** is a visit that only runs queued calls and goes back into `Run()`. One thing of the guest does execute in
the park context (M2): the rest of a sound burst that the debugger suspended (instructions of the ARM7 and the time steps
of the AICA between them; from M3 also the steps of the DSP and the rest of a sample), when the park loop or `dc_run`
runs it on (2.5.2). The SH4 executes nothing there and no
emulated time passes.

Locks of `dbg::emu`, outermost first: L (recursive lifetime lock); the listener lock (held across an invocation of the
listener; taken with no lock held but, possibly, L); Q (the queue lock: the call queue, the published state, the
requests of other threads; a leaf, held for a few statements only, never across a closure, the listener or anything of
the machine; `GetRunState()` takes it, also from inside the listener, which is therefore allowed). The machine's own
`callback_lock` nests inside L and never with Q.

#### 2.2.1 Lifetime

Machine / thread state, guarded by L in `dbg::emu`:
`NoMachine | Stopped | Running | Parked` (`NoMachine` = no `VirtualDreamcast`; `Stopped` = machine exists, no emulator
thread; `Running` = the emulator thread exists and runs the guest, or is on its way into or out of `SuperH4::Run`;
`Parked` = the emulator thread exists and sits in the park loop).

The hooks `Dreamcast_impl` calls (`debugger.h`); each one that publishes a lifetime change invokes the run-state listener
(2.2.4) on the calling thread before it returns:

| `Dreamcast_impl` | What it does | Lifetime |
| --- | --- | --- |
| `Init` | `LifetimeLock`; create the machine; `OnMachineCreated()` | `NoMachine` -> `Stopped` |
| `StartGame` | `LifetimeLock`; ...; `Reset()`; `Resume()` | |
| `Reset` | `LifetimeLock`; discard a suspended sound burst (2.5.2); reset the machine; `OnGuestStateReplaced()` | |
| `LoadState` | `LifetimeLock`; load; `OnGuestStateReplaced()` | |
| `SaveState` | `LifetimeLock`; save | |
| `Resume` | join a previous thread (`emu_thread.WaitToEnd()`, without L); `LifetimeLock`; `OnThreadStarting()`; start the thread; wait for `emu_started` | `Stopped` -> `Running` |
| `dc_run` | `OnThreadEntered()`; select the backends; `StartCpu(false)`; set `emu_started`; the loop of `Park()` (2.2.4); run a suspended sound burst to its end, without checks (2.5.2); stop callback; `OnThreadExiting(guestExit)` | `Running` -> `Stopped` |
| `Term` | `StopAndWait()` (stop and join a live thread, without L); `LifetimeLock`; `OnMachineDestroying()`; destroy the machine | `Stopped` -> `NoMachine` |

- L is recursive (`StartGame` holds it across `Reset()` and `Resume()`). The holder must not join the emulator thread
  (`OnThreadExiting` takes L on (E)): `Resume` and `Term` join first, then lock. `Resume` may wait for `emu_started` while
  holding L, because `dc_run` takes no L before it set `emu_started`. (C) never holds L while it waits for (E); it holds
  L only for the calls it runs itself while no emulator thread exists.
- `OnThreadExiting(guestExit)` is the last action of `dc_run`, after the stop callback ran: it takes L, makes the
  interrupt acceptance the latch held back, ends what belonged to a stop — the temporary breakpoints of the SH4 and the
  ARM7, a pending step-out, a step of the ARM7 or of the DSP that is armed, the DSP's resume exemption
  (`dbg::dsp::OnStop()`) —, drains the call queue (2.2.2), cancels an SGC capture that records (`dbg::sgc::Cancel`: its
  request is answered `-32001`), drops a pause request that was not honoured and publishes `Stopped` (reason
  `host-stopped`, or `guest-exit`). **The cancel stands behind the drain**: the drain's `CC_TABLE` calls run on the
  exiting thread, on (E), and a `state.recordSgcFrames` among them may arm a capture there; a cancel in front of the
  drain would find none, and the thread would exit with a capture armed (`M3-ROUND.md` 2.3, `debugger.h`). A sound burst that
  the debugger suspended is not its business: `dc_run` has run it to its end, without checks, in front of the stop
  callback (`VirtualDreamcast::RunSoundBurst(false)`, 2.5.2), so what the callback sees, and what the exit line of an
  automated run prints, is the state behind a completed burst.
- `OnGuestStateReplaced()` is called in `Reset` and `LoadState` after the new guest state is in place: the debugger drops
  what belonged to the old state — the owed remainder and resume exemption of the SH4 mechanism (2.3.4 h), the resume
  exemption of the ARM7 (2.5.3), the resume exemption and a step of the DSP that is armed (2.5.4), `g_sleep_cut`, a
  deferred interrupt acceptance, the temporary breakpoints of the SH4 and the ARM7, a pending step-out, a step of the
  ARM7 that is armed, an SGC capture that records (`dbg::sgc::Cancel`: its request is answered `-32014`), an owed
  resume (below). A suspended sound burst is
  the machine's: `Dreamcast_impl::Reset()` has discarded it before this call. In `Stopped` it publishes a new stop
  (`stopId` + 1, 2.3.1) so clients refetch; on (E) the stop, if one is in force, is published by `Park()`.
- `SaveState` and `LoadState` exist in the tree today (the imgui menu calls them) and are covered by the selftests; they
  leave the product with track 10 (D8). The hook stays for when states return.
- Every lifetime change invokes the run-state listener on the thread that made it, with L held.

What works in each state:

| State | Wire `executionState` | Reads of guest state | Bookkeeping (breakpoints, watches, categories) | Writes, run control |
| --- | --- | --- | --- | --- |
| `NoMachine` | `paused`, `reason: "no-game"` | error `-32002`; `debugger.describe` returns the static tree with every value `"n/a"`; ticks carry `registers: {}` | on (C) under L | error `-32002` |
| `Stopped` | `paused`, `reason: "host-stopped"` (or `"guest-exit"`) | on (C) under L | on (C) under L | error `-32001` |
| `Running` | `running` | on (E), slice hook | on (E), park context (service excursion) | writes: on (E), park context (service excursion); steps: error `-32003`; pause: requested; resume: `{}` |
| `Parked` | `paused`, `reason` of the stop | on (E), park loop | on (E), park loop | on (E), park loop |

The server works in every one of these states, `NoMachine` included (D8): a client that connects while no game is loaded
gets its describe answer and its ticks, and its bookkeeping requests work.

A host stop taken while the thread is parked in a debugger stop, followed by a host resume: the first `Park()` of the
next thread is a resume of that stop when the guest is where that stop was published — no step has executed since and
the PC was not written. The debugger pause that was in force is not applied again, `dbg::sh4::PrepareResume()` is called
there, the instruction of the stop executes and its breakpoint stops again only at its next arrival. The owed resume
also calls `dbg::arm7::PrepareResume()` and `dbg::dsp::PrepareResume()`, which arm nothing: no sound burst is suspended
then (the thread before ran it to its end in front of its stop callback, so the instruction or the step of a stop of
the ARM7 or of the DSP has executed), and `OnThreadExiting()` has ended the stop for both (`dbg::arm7::OnStop()`,
`dbg::dsp::OnStop()`). Otherwise (a step
executed, or the PC was written during the stop) nothing is resumed: the thread runs from where the guest is, and a
breakpoint on that PC stops. The tick after the host resume reports `running` in both cases.

#### 2.2.2 Calls

`int dbg::emu::Call(CallClass cls, std::function<void()> fn)` — callable from any thread except (E), and not from inside
a call, the listener or a selftest (those are on (E) or hold L and do the work directly; calling it there ends the
process). The caller holds neither L nor a lock that `fn` or the listener takes. It waits at most `kCallTimeoutMs`
(2 s) for a call that has not started.

Where `fn` runs, by the lifetime at the moment the call is taken:

| Class | Meaning | `NoMachine` | `Stopped` | `Running` | `Parked` |
| --- | --- | --- | --- | --- | --- |
| `CC_READ` | reads guest state and debugger tables | not run, `-32002` | caller thread, under L | (E), slice hook | (E), park loop |
| `CC_TABLE` | changes debugger tables; may need code-cache maintenance | caller thread, under L | caller thread, under L (no code-cache work: every thread start builds a fresh cache) | (E), park context (service excursion, 2.2.4) | (E), park loop |
| `CC_WRITE` | writes guest state (memory, registers, FPSCR / SR), reset | not run, `-32002` | not run, `-32001` | (E), park context (service excursion) | (E), park loop |
| `CC_EXEC` | run control (resume, step, step over / out) | not run, `-32002` | not run, `-32001` | not run, `-32003` (answered from the published state, not queued) | (E), park loop |

Return value: 0 when `fn` ran and has returned; `kErrNoGame` (`-32002`), `kErrHostStopped` (`-32001`) or `kErrNotPaused`
(`-32003`) per the table, `fn` did not run; `kErrBusy` (`-32000`) on timeout. `control.runUntil` while running answers
success: the core maps the `kErrNotPaused` that `Call` returns for its resume call (the thread runs, `fn` did not run) to
`{}`. The `kErrNotPaused` that `emu::Resume()` returns inside the call is another case — an action is already armed for
the stop (2.3.2) — and is answered in-band.

- A call is one heap object shared (`std::shared_ptr`) by the caller and the queue: `{cls, fn, state: Queued | Running |
  Done | Cancelled, result}`, guarded by Q, with a `std::condition_variable` waited with a predicate (`cResetEvent` is
  not usable: its state is uninitialised and `Wait()` has no predicate loop). `fn` captures by value or by `shared_ptr`
  only, never a reference or pointer to the caller's stack; its result lives in the captured object. It must not let an
  exception escape and must not wait for another thread.
- Timeout: a `Queued` call is marked `Cancelled`, skipped by (E), and its closure is destroyed by the caller at that
  moment; a `Running` call is waited for without limit. Where the caller runs `fn` itself, the wait for L has the same
  limit and the same result.
- (E) runs calls one at a time; a call never runs inside another call (depth counter, `verify(depth == 0)`).
- A call that waits in the queue when the emulator thread exits is taken by that thread in `OnThreadExiting()`, under L,
  with the outcome of the `Stopped` column: `CC_READ` and `CC_TABLE` calls run, `CC_WRITE` and `CC_EXEC` calls complete
  with `-32001`. A call queued for a parked thread that is taken after the thread resumed follows the `Running` column
  (a queued `CC_EXEC` call then completes with `-32003`).
- `CC_READ` does not run in `NoMachine`. A caller that needs only the debugger's tables there (the tick that follows
  `debugger.describe`) makes a `CC_TABLE` call, which the caller runs itself under L.
- A guest that spins the host (`hw/mem/_vmem.h`, `hw/holly/sh4_mem_area0.cpp`) or an (E) blocked for more than 2 s
  yields `-32000`; this is not recoverable from the debugger. Measured: no busy answer in about 1.9 million calls except
  behind a closure or a host hold of L that itself lasted more than 2 s.

#### 2.2.3 Attention word, slice hook, stop latch

`dbg::g_attn` is a `std::atomic<u32>`; bits `ATTN_CALL` (1: at least one call queued), `ATTN_PAUSE` (2: a pause is wanted:
a client asked, or an event or sound breakpoint fired) and `ATTN_LATCHED` (4: (E) only, set exactly while
`g_stop_latched` is). Bits are set by any thread and cleared by (E), only ever with `fetch_or` / `fetch_and`; the
emulator thread may store 0 at its end, under Q. `dbg::g_stop_latched`, `dbg::g_intc_deferred`, `dbg::g_in_sleep` and
`dbg::g_sleep_cut` are plain `bool`s read and written by (E) only. `dbg::g_parked` is a `std::atomic<bool>` (2.2.5).

The slice end (`hw/sh4/SuperH4_impl.cpp`); one test of the attention word is all a slice pays for the debugger:

```cpp
static inline int slice_end() {                        // always inlined into the two functions below
    if (unlikely(dbg::g_attn.load(std::memory_order_relaxed)))
        return slice_end_attention();
    Sh4cntx.sh4_sched_next -= SH4_TIMESLICE;
    if (Sh4cntx.sh4_sched_next < 0) {
        sh4_sched_tick(SH4_TIMESLICE);
        if (unlikely(dbg::g_attn.load(std::memory_order_relaxed)))   // what a callback of this tick asked for
            dbg::emu::SliceHook();                                   // is seen in this call
    }
    return Sh4cntx.interrupt_pend | (sh4_int_bCpuRun == false);
}
static NOINLINE int slice_end_attention() {
    if (dbg::g_stop_latched) return 1;                 // no emulated time, no scheduler callback, no hook
    Sh4cntx.sh4_sched_next -= SH4_TIMESLICE;
    if (Sh4cntx.sh4_sched_next < 0) sh4_sched_tick(SH4_TIMESLICE);
    if (dbg::g_attn.load(std::memory_order_relaxed)) dbg::emu::SliceHook();   // behind the scheduler tick
    return Sh4cntx.interrupt_pend | (sh4_int_bCpuRun == false);
}
int UpdateSystem()      { return slice_end(); }
int UpdateSystem_INTC() { return slice_end() ? UpdateINTC() : 0; }

int UpdateINTC() {                                     // hw/sh4/sh4_interrupts.cpp, covers every backend's call site
    if (unlikely(dbg::g_stop_latched)) { dbg::g_intc_deferred = true; return 0; }
    ... unchanged ...
}
```

The word is not zero while a call or a pause waits for the hook and for as long as the latch is set, so the latch is
looked at only behind it. `SliceHook()` has no return value. `UpdateSystem()` is called from two places: the slice end of
a backend loop (or of a step), and the wait loop of the SLEEP handler, which sets `g_in_sleep` around its loop. Stop
causes are of two kinds. An event breakpoint and a sound breakpoint are **freezing** causes: the scheduler must stop in
the very call in which they were raised (2.5.2). A pause request and a service excursion are **deferrable**: they are
taken only at a slice end outside a SLEEP wait, where leaving and re-entering `Run()` is exact (R13).

While the lifetime is `Running` the hook:
1. runs the queued `CC_READ` calls, one at a time; a queued `CC_EXEC` call is completed with `-32003`;
2. if a freezing cause is recorded, latches the stop with it (`emu::LatchStop`) and returns. A breakpoint or a step of
   the ARM7 or of the DSP, or the end of an SGC capture armed at a stop, is such a cause: the sound burst of this
   scheduler tick records it through `emu::OnSoundStop` (2.5.2), and the hook latches it (the sound side does not call
   `LatchStop` itself);
3. otherwise, if a pause was requested or a call of another class is queued, and `CpuRunning` is set (when it is not,
   the machine itself is about to leave `Run()` and the park context that follows takes them), and `g_in_sleep` is false:
   latches — with cause `pause` for a pause request, with no cause for a queued call alone (`Run()` then returns for a
   service excursion, which is no stop and publishes nothing);
4. deferrable causes met inside a SLEEP wait stay pending (`g_attn` stays set) and are latched at the next slice end of
   the backend loop. Liveness: a deferrable cause that has been pending for `kPauseLivenessMs` (250 ms of host time) is
   latched at the next `UpdateSystem()` wherever it is called from (a guest that sleeps with interrupts masked, or one
   emulated very slowly); the SLEEP skew of 2.4 then applies.

Latching the stop is: `g_stop_latched = true` with `ATTN_LATCHED`, `g_parked = true`, `Sh4cntx.CpuRunning = false`, and
the cause recorded (when several are recorded before the park context is entered, the one published follows the priority
of 2.3.1, and among breakpoints of the winning cause the lowest id).

During a step (the lifetime is `Parked`) the hook runs no call (calls are taken only by the park loop, between steps);
it does one thing: if a freezing cause is recorded it latches the stop, which ends the step at this slice boundary.
The run of the SH4 towards the sound burst that a step of the ARM7 or of the DSP waits for (2.5.3, 2.5.4) is such a
step.

An SH4 code-breakpoint hit latches the stop in the same way from the check helper (2.3.4 c), at the instruction. The hook
never changes the armed breakpoint set and never touches the code cache: a block may be executing.

From the latch until the park context is left again:
1. `UpdateSystem()` returns 1 behind its attention test: emulated time does not advance, no scheduler callback can fire,
   the hook is not called.
2. `UpdateINTC()` accepts nothing and records `g_intc_deferred`.
3. The SLEEP handler (`hw/sh4/interpr/sh4_opcodes.cpp`, also reached from JIT blocks through `shop_ifb`) tests the latch
   in a slice of its wait that reported something and accepted nothing, and then leaves with `next_pc -= 2` (SLEEP is
   re-executed after resume), recording `g_sleep_cut` for the leave order of 2.2.4. That is its only change:
   ```cpp
   int i = 0, s = 1;
   dbg::g_in_sleep = true;
   for (;;) {
       if (UpdateSystem()) {                                          // one slice of the wait
           if (UpdateINTC()) break;                                   // interrupt accepted
           if (unlikely(dbg::g_stop_latched)) { dbg::g_sleep_cut = true; s = 0; break; }
       }
       if (i++ > 1000) { s = 0; break; }                              // today's bound: 1002 slices
   }
   dbg::g_in_sleep = false;
   if (s == 0) next_pc -= 2;                                          // re-execute SLEEP
   ```
   The handler does not leave when `CpuRunning` is false for any other reason: a host stop, and a step, run the wait to
   its end as today (at most 1002 slices, 448 896 cycles), which keeps a host pause and resume exact. After a latch the
   loop that executed the SLEEP re-executes it until its slice runs out — under the x64 JIT up to 448 times (the SLEEP
   block costs one cycle), in the interpreter up to 55 times; these re-executions have no side effect and are charged to
   the slice counter without scheduler time. Documented (2.4); it happens only for freezing causes and past the liveness
   bound.
4. The AICA scheduler callback starts with `verify(!burst.suspended)` (M2, 2.5.2). A burst the debugger suspended is
   run on under the latch, by the park loop (2.2.4 a) and by `dc_run` in front of the stop callback, never by the
   scheduler.

#### 2.2.4 Stop reasons and the park context

`Sh4cntx.CpuRunning` keeps meaning "the backend loop continues". Why `Run()` returned has two owners.

The machine's own reasons are a bit set in `Dreamcast_impl` (`stop_reasons`), set under `callback_lock` by the thread
that asks, before it stops the CPU:

| Bit | Set by |
| --- | --- |
| `HOST_STOP` | `Dreamcast_impl::Stop(cb)`: the host UI (menu, ESC, window close, exit), the end of an automated run. The thread runs the callback and ends |
| `GUEST_RESET` | `RequestReset()` on (E): an `SB_SFRES` write; `control.reset` while running (`dbg::emu::Reset()` in a service excursion). The thread resets the machine and runs on |
| `GUEST_EXIT` | `RequestExit()` on (E): `reios_exit`. The thread ends |
| `HOST_WAITS` | `StopAndWait()`, with `HOST_STOP`: the thread that draws the frames waits for this one to end, so the thread ends without waiting for its frames to be drawn |

A stop that the debugger decided is not in that set: it is `dbg::emu::Park()`'s, which holds the cause that `LatchStop()`
recorded, a pending start park and pause requests. (v2's `DBG_PAUSE`, `DBG_SERVICE` and `DBG_SWITCH` bits do not exist: a
latch without a cause is a service excursion, and the loop that `Run()` enters next is selected when the park context is
left.)

`SuperH4_impl::Stop()` and `Start()` have no `verify`s. The loop of `dc_run`:

```cpp
OnThreadEntered(); ... select the backends ...
bool park = StartCpu(false);                 // false: the machine was asked to stop while this thread started
emu_started.Set();
for (;;) {
    if (park && dbg::emu::Park() == dbg::emu::PARK_RUN)
        sh4_cpu->Run();
    park = true;
    reasons = stop_reasons; stop_reasons &= ~GUEST_RESET;          // under callback_lock
    if (reasons & GUEST_RESET) {                                   // the reset first
        SaveRomFiles(...); Reset(); sh4_host_fp_resync(); StartCpu(false);
    }
    if (reasons & (HOST_STOP | GUEST_EXIT)) break;
}
RunSoundBurst(false);  // a burst the debugger suspended runs to its end, nothing can stop it
SaveRomFiles(...); WaitQueueEmpty() unless HOST_WAITS;             // the machine still counts as running
thread_live = false; sh4_cpu->thread_live = false;                 // under callback_lock: the machine's own word, and
CpuRunning = false; g_parked = false;                              // the one word IsRunning() reads (2.2.5)
stop callback; OnThreadExiting(guestExit);
```

`Resume()`, which starts the thread, sets both `thread_live` words under the same lock before the thread exists.

`StartCpu(leavePark)` sets `CpuRunning` (and clears `g_parked` when `leavePark`) unless a stop reason is pending, in which
case it leaves the cpu stopped and returns false. A thread whose cpu is left stopped at thread start makes no `Park()` in
front of its first evaluation: the request may have come in before `OnThreadStarting()`, where `Kick()` finds no thread,
and a park made there would wait with the request pending. Calls queued for such a thread are drained by
`OnThreadExiting()`; a `StartPaused` park and an owed resume stay for the next thread.

`u32 dbg::emu::Park()` — (E), `dc_run` only; called before the first `Run()` of a thread and after every evaluation of
the machine's stop reasons that did not end the thread. It returns `PARK_RUN` (the park context has been left,
`CpuRunning` is set and `g_stop_latched` clear, and `g_parked` clear but for a step of the ARM7 or of the DSP that
waits for its burst: call `Run()`) or `PARK_EVALUATE` (evaluate the machine's stop reasons, then call `Park()` again; returned after
`Kick()`, after a call that asked for a reset, after a step in which the SH4 executed, and in place of `PARK_RUN` when
a host stop or a guest exit is pending). On entry it sets the latch: in the park context nothing is accepted and no time passes,
whatever a call does. Then it loops:

1. *Fold* what other threads asked for and what the guest raised into the stop that is decided: a pause request becomes
   cause `pause`, a freezing cause that the hook had not latched yet is taken. A guest reset seen on this thread
   (`OnGuestStateReplaced()` on (E)) while a debugger stop was in force or one is wanted makes the stop a `reset` stop.
   At the first `Park()` of a thread: the SH4 engine is captured (2.3.5), a requested selftest session is started
   (5.3), and a start park is decided (2.2.9).
2. *Enter the stop*, if one is decided (**entering the park context for a stop**): `g_parked` is set and `CpuRunning`
   cleared; `dbg::sh4::OnStop()` removes the temporary breakpoints and cancels a pending step-out (2.3.4 j), whatever
   caused the stop, and reports what stopped; beside it `dbg::arm7::OnStop()` and `dbg::dsp::OnStop()` drop the ARM7's
   temporary breakpoint and a step of either that is armed, and the DSP's records the step it stands in front of and
   whether the DSP decided this stop (2.5.4); the stop is published (`stopId` + 1) and the run-state listener invoked;
   the window title is set (2.2.9); a selftest session is handed the stop (5.3).
3. If a stop is in force and a run-control action is armed (a call armed it): execute it — a resume leaves (below); a
   step is executed by the loop itself (below), but for a step of the ARM7 or of the DSP asked for with no sound burst
   suspended: the loop leaves for it without publishing (g, h). After an executed step the stop of the step is decided and
   `PARK_EVALUATE` returned, so that a host stop or guest reset requested during the step is found first; the next
   `Park()` publishes the stop. A step that is complete at a vector (below) goes back to 1 at once.
4. Otherwise run one queued call (of any class; a `CC_EXEC` call completes with `-32003` when no stop is in force). If
   the call asked the machine for a reset (`control.reset` while running), return `PARK_EVALUATE`. Back to 1.
5. With the queue empty and no stop in force (a service excursion): if a pause was requested or a call arrived
   meanwhile, back to 1 (this is a slice end, they are taken here); else leave. No tick, no `stopId` change: a service
   excursion is not a stop.
6. With the queue empty and a stop in force: wait on the queue condition variable, without limit, for a call or
   `Kick()`; after `Kick()` return `PARK_EVALUATE`. A debugger stop that was in force stays in force across the
   evaluation: `dc_run` does not save ROM files and does not run the stop callback for it.

**Leaving the park context** (resume, step, end of a service excursion), in this order:
a. for a resume decide the resume exemptions of the ARM7 and of the DSP (`dbg::arm7::PrepareResume()`,
   `dbg::dsp::PrepareResume()`), then run a suspended sound burst on, with the checks of the ARM7 and of the DSP
   (`VirtualDreamcast::RunSoundBurst(true)`): if a breakpoint of the ARM7 or of the DSP suspends it again, or an SGC
   capture armed at this stop completes in it (also in the last sample of the burst, which then ends with none
   suspended: a stop that `emu::OnSoundStop` recorded during the run-on is the stop all the same), nothing more is done,
   the thread stays parked, that stop is published with a new `stopId`, and the resume, or the step of the SH4 the burst
   was run on for, is dropped. A step of the SH4 decides the two exemptions before it runs a suspended burst on, as a
   resume does; a step of the ARM7 decides the DSP's and arms its own (`dbg::arm7::PrepareStep()`), a step of the DSP
   decides the ARM7's and arms its own (`dbg::dsp::PrepareStep()`), and each is complete in the run-on (below). No
   burst is suspended at the end of a service excursion: a suspension is followed by a stop (2.5.2);
b. clear `g_stop_latched` (and `ATTN_LATCHED`);
c. if `g_intc_deferred`: clear it and call `UpdateINTC()` once (the acceptance the latch suppressed happens now, in the
   order of an undebugged slice: burst, interrupt acceptance, instructions). If the latch cut a SLEEP short
   (`g_sleep_cut`) and the PC is still on that SLEEP, the call is made with the PC advanced past the SLEEP, and the PC
   is put back on the SLEEP only when nothing was accepted — so an interrupt accepted here returns to the instruction
   after the SLEEP, as in an undebugged run. `g_sleep_cut` is cleared. No `UpdateINTC()` call is made when nothing was
   deferred, so a stop in the middle of a slice does not add an interrupt check that an undebugged run would not have;
d. `sh4_host_fp_resync()`;
e. for a resume (also the owed resume of 2.2.1, which also calls `dbg::arm7::PrepareResume()` and
   `dbg::dsp::PrepareResume()`, which arm nothing; no sound burst is suspended then, the thread before ran it to its end
   in front of its stop callback): `dbg::sh4::PrepareResume()` arms
   the resume exemption for the PC as it is now and keeps or drops the owed remainder (2.3.4 h). For a step:
   `PrepareStepOver()` / `PrepareStepOut()` and then `PrepareResume()`, or `PrepareStep()`. A step of any kind whose step c accepted an interrupt is complete at this
   point: only `PrepareStep()` is called, nothing executes, the stop is at the vector (2.3.2);
f. the loop that `Run()` enters next is selected (2.3.5; a change is logged as `dc.debugger.engine`);
g. `StartCpu()`: under `callback_lock`, and only if no machine stop reason is pending, `CpuRunning = true`, and for a
   resume or the end of an excursion also `g_parked = false`. When a reason is pending `Park()` returns `PARK_EVALUATE`
   instead. A step keeps `g_parked` set (the machine counts as running for the host all through the step), runs with
   `CpuRunning` set (R12) through `SuperH4Backend::Step()` or `sh4_fallback_step()`, and ends by clearing `CpuRunning`
   and setting the latch again. A host stop or guest reset requested during the step is found by the evaluation that
   follows the step, and the stop of that step is then never published;
h. a resume that ends a stop in force publishes `Running` (the `running` tick); a step or an excursion does not.

A fourth case: a step of the ARM7 or of the DSP with no burst suspended leaves as a resume does (b to f, with the SH4's
resume exemption) and is kept as a step: `g_parked` stays set, nothing is published, the slice hook treats the run as a
step; the stop is recorded in the next AICA callback and published by the next `Park()` (for the DSP the step is step 0
of the first sample of that burst; a held ARM7 does not matter to it, the AICA is clocked whatever ARMRST says, 2.5.2). The SH4 runs for one period of the
AICA callback at most (145124 cycles and the rest of a slice). A stop of the SH4 that comes first ends the run with its
own cause; a host stop, a guest exit or a guest reset that comes first is found by `dc_run`, as behind a step of the
SH4: no stop of the step is published (a reset publishes its own), the armed step ends with it, and the wait counts as
a step that has executed, so the thread leaves no resume owed. Calls queued during the wait are served behind the stop
of the step, in their order.

The steps of the sound side with a burst suspended (`M3-ROUND.md` 2.3, `Park()` item 3), executed by the loop in the
park context, where no emulated time passes, the SH4 executes nothing and nothing is published in between:

- **A step of the ARM7** runs the burst on until the check behind the instruction stops it, or a check of the DSP or the
  end of an SGC capture armed at a stop in the rest of the burst stops it first (that stop is published with its own
  cause and the step ends), or the burst ends (the step is then complete at the end of the burst, with none
  suspended). A burst suspended in front of a DSP step is run on through the rest of that sample to the ARM phase of
  the next, where the instruction is; in the last sample of the burst there is none, and the step then waits for the
  next burst as with none suspended.
- **A step of the DSP** runs the burst on until the check behind the step stops it, or until the end of the sample
  when the step was the last of its sample (127: the sample's mix and interrupt update run, the step is complete at the
  end of the sample, `STEP` 0, and behind the last sample of the burst the burst ends with none suspended); the ARM
  phase of a sample it runs through uses the checks of the ARM7, and a stop of the ARM7 that comes first is published
  with its own cause. The step executes its step also with a listed breakpoint on it, and whether the ARM7 is held or
  not.

**Run-state listener** (`void dbg::emu::SetListener(void (*)(const RunState&))`, installed by `dbg_core` at
`dbg::Init`): invoked with the state just published, `{lifetime, stopId, cause, breakpointId, notice}` (`notice` is a bit
set of the statements of 3.2), on (E) for every stop (park for any cause, completed step, reset while paused, a stop
that a check of the ARM7 or of the DSP, or the end of an SGC capture, makes while the park loop runs a suspended sound
burst on) and every resume, and on the thread
that made a lifetime change, with L held, for `Stopped`, `NoMachine`, and `Running` at a thread start. A service excursion publishes nothing. At every invocation guest state is quiescent and may be read
directly unless the lifetime is `NoMachine`; the listener builds the tick and hands the text to `dbg::net` (sends never
block). It must not call `dbg::emu::Call` or `SetListener`, must not wait for another thread and must not let an
exception escape. `SetListener` returns when no invocation of the previous listener is in progress.

The armed SH4 breakpoint set used by the backends is owned by (E); other threads change it only through
`Call(CC_TABLE, ...)`, and a change made while running is applied in a service excursion (2.3.4 g).

#### 2.2.5 Host stops against a parked thread

- `SuperH4_impl::IsRunning()` is **one word and one load**: `SuperH4_impl::thread_live`, a `std::atomic<bool>` of the
  machine. It is true from `Resume()` — before the emulator thread exists — until that thread takes its stop
  callback, whatever the cpu does in between: a machine counts as running also while the debugger has it stopped
  (latched, parked, in a step), while its cpu is stopped for a reset, and while it has been asked to stop, or its
  guest has exited, and is on its way there. The machine writes the word in two places only, in `Resume()` and where
  the thread takes its stop callback, each time under `callback_lock`, beside the `thread_live` of its own that its
  stop requests go by. It reads neither `CpuRunning` nor `g_parked`: the cpu's word is 0 for a machine that is
  parked, being reset or on its way to its stop, because the generated code of the recompilers leaves its loop on
  0. (As built by E4a the answer was put together from five loads, `CpuRunning || g_parked || resetting ||
  CpuRunning || stopping`, and a reader whose loads fell between the stores of two hand-overs in a row — a guest
  reset and the debugger stop behind it — could find every one of them clear for a live machine. Package J1 made
  it one word, 2.3.7 item 12. v2's rule that `IsRunning()` turns false the moment a stop is requested does not hold
  either way: a host that takes a machine for stopped while its thread still has the stop callback to take ends its
  user interface under a thread that waits for it to draw the frames it queued, and the process never ends.)
- `g_parked` is set by (E) before it clears `CpuRunning` for any debugger stop (and again when it re-parks after a
  reset). It is cleared by (E) when it resumes, where the guest stops the cpu for a reset, and at the end of the thread
  (where it takes its stop callback, and in `OnThreadExiting()`); and by `SuperH4_impl::Stop()` on any thread (a stop
  request ends the debugger stop).
- `SuperH4_impl::Stop()` clears `CpuRunning` and `g_parked` and calls `dbg::emu::Kick()`, which wakes the
  park loop so that it returns `PARK_EVALUATE` (without a parked thread it does nothing; a kick made before
  `OnThreadStarting()` is not kept). `Dreamcast_impl::Stop(cb)` records `HOST_STOP` and stores the callback
  under `callback_lock` before it calls `sh4_cpu->Stop()`; with no live thread it runs the callback at once; a second
  `Stop(cb)` while a stop is on its way chains its callback behind the stored one. `RequestReset()` sets `GUEST_RESET`
  and stops the cpu (the machine goes on counting as running while its cpu is stopped for the reset: `thread_live`);
  `reios_exit` asks for `GUEST_EXIT` through `VirtualDreamcast::RequestExit()`, once per thread and never behind a
  stop. `resetting`, `stopping` and `StopForReset()` were removed with header revision 6 (package R6): `RequestReset()`
  calls `Stop()`.
- `Dreamcast_impl::Term()` never runs with a live emulator thread: `StopAndWait()` records `HOST_STOP | HOST_WAITS`, stops
  the cpu and joins the thread, without L, before `sh4_cpu->Term()`.
- `dbg::Init()` is called at the end of `nulldc_init()`; `dbg::Term()` at the start of `nulldc_term()`, before the GUI
  (and with it the machine) is destroyed. `dbg::Term()` stops the server, ends (C) and joins both — where `dbg::Init`
  left them running: with no server enabled, or one that could not be started, there is neither (2.11) —; a call
  that (C) is waiting for is given up. A parked emulator thread is released by the stop request that
  `nulldc_term()` makes right behind it (`virtualDreamcast->StopAndWait()`), while the globals that thread uses are
  still valid.
- A guest soft reset during a step, the ESC key while parked and the menu key while parked therefore take the paths of
  2.2.4 instead of a `verify` abort or a lost callback.
- The callers of `Stop(cb)` today are the window and the imgui GUI (menu key, ESC, close button, the menu's Exit) and the
  exit keys of an automated run. With PLAN track 10 the GUI's callers are replaced by requests from the web UI; the model
  of this section — host stop apart from debugger pause — is what those requests use, and it does not change.

#### 2.2.6 Host floating-point state

FPSCR and SR are written only on (E). `sh4_host_fp_resync()` (`hw/sh4/sh4_core_regs.cpp`: forgets what was applied last
and applies FPSCR's rounding and denormal mode to the calling thread) is called at every entry of `SuperH4_impl::Run()`,
after `Reset()` in the reset path of `dc_run`, and when the park context is left for a resume, an excursion's end or a
step. `state.setRegister` is a `CC_WRITE` call and therefore rejected in `Stopped`. (A thread inherits its creator's
MXCSR while the tree caches "already applied" in globals; without the resync the guest runs with the wrong host rounding
/ denormal mode.) On the generic host path `SetFloatStatusReg` applies nothing yet (package B64's finding): a port fixes
that, not the debugger.

#### 2.2.7 Latency and limits

- Pause request, service excursion (any write or bookkeeping call while running, including a change of the armed set):
  taken at the first slice end of the backend loop that has the request visible, i.e. within one slice of executed code
  — 56 instructions in the interpreter (`fast` timing), up to 448 blocks in the JIT. Time spent waiting in SLEEP does
  not count towards that slice: a SLEEP wait ends at the next accepted interrupt (at most 1002 slices, 448 896 cycles).
  The bound is 250 ms of host time (2.2.3). Until a change of the armed set is applied the old blocks run.
  Measured (host time from the request to the `Parked` publication): a busy loop 1 µs median, at most 896 emulated
  cycles; 2ndmix 3 µs median, 0.4 ms at the 99th percentile on the JIT, at most 3.2 million emulated cycles (SLEEP
  waits); a SLEEP that nothing ends: latched at 250.2 to 250.5 ms.
- Event and sound breakpoints: an event raised by a scheduler callback is latched in the same `UpdateSystem()` call, also
  inside a SLEEP wait; an event raised by an SH4 instruction at the next `UpdateSystem()`. The event log entry carries the
  exact event; the PC reported is where the loop stopped.
- SH4 code breakpoint, temporary breakpoint, the end of a step-out: at the instruction, no latency (2.3.4). Mute / solo
  and every other helper-side state take effect at the next arrival.
- Cost on x64 (Appendix C, 2.3.6 B11): an arrival that continues about 35 host instructions; a stop + resume about 33 000
  host instructions (one compile); a pause + resume at a slice end about 800. With the debugger compiled in and nothing
  armed: the attention-word test per slice (measured 2 to 3 host instructions per slice; with the latch test of
  `UpdateINTC()` and the exception report, 18 to 23 per slice on the recompiler and 6 to 8 on the interpreter in the
  gate's workload), and one load and branch per decoded instruction.
- Calls that arrive during a step wait for the park loop. The longest step is a step on SLEEP (at most 1002 slices).
- `Call`s of class `CC_READ` are served in every `UpdateSystem()`, also inside a SLEEP wait.
- (E) blocked on the UI or audio, or a guest that spins the host: `-32000` after 2 s for every call; pause does not
  complete.

#### 2.2.8 Backends without a complete context at the slice hook

A backend reports `SH4DBG_CTX_AT_SLICE` (2.3.4 l) when `Sh4cntx` is complete, pc included, whenever it calls
`UpdateSystem()`: at a slice end, and from the SLEEP handler inside a block. The interpreter and the x64 and arm64 JITs
report it (the x64 JIT's epilogue has written the pc between two blocks, and the register allocator flushes in front of
the SLEEP fallback; the arm64 JIT since package A64 of the platform round, whose slice end stores the pc it goes to into
the context in front of `UpdateSystem()`: header revision 8, H8-5). Until A64 the arm64 JIT did not: it kept the next PC
in a host register across the call, so `Sh4cntx.pc` was stale there, and its running ticks said `registersStale` on the
Mac and on Linux arm64 (2.3.7 item 18). No backend of the tree lacks the bit since; what follows is the rule for one
that does (a new port), held against a stand-in on every host (`debugger.contract.hash.listening.rule`, 5.4). On such a
backend running ticks omit the path `dc.sh4.cpu` and set `executionState.registersStale: true` (2.9),
SH4 callstacks are not built while running (`state.getCallstack {sh4}` answers `frames: []`), and watches on
`dc.sh4.cpu.*` evaluate to `"n/a"` while running. The context is valid after `Run()` returns, and writes only ever happen
there; `registersStale` never appears in a paused tick. Whatever else reads SH4 state inside `UpdateSystem()` reads it
only under the bit (the sound regression harness, 5.6).

#### 2.2.9 `StartPaused`; what the window shows

- `Debugger:StartPaused=yes` parks once per machine: at the first `Park()` after `OnMachineCreated()`, i.e. after
  `emu_started.Set()`, with `CpuRunning` false, before the first `Run()`; `reason: "start"`. At that point the backends
  are selected (so the SH4 engine is known to `debugger.describe` and `dc.timing`, 2.3.5) and reios has not booted: PC is
  `0xA0000000`, RAM is empty and there are no symbols (the ELF is loaded by the first instruction, the reios trap). This
  park waits for a client without limit (the grace period of 2.9 starts only at a disconnect). The key is read by
  `dbg::emu`, which parks whether or not a server runs; the configuration in which nothing could resume that park is
  refused before any machine exists: **`StartPaused=yes` while the server is not enabled is a configuration error** —
  `dbg::Init` prints one line on stderr and ends the process with exit code 2 (2.11) — except in a selftest session
  (`Debugger:SelfTest` set), which has no server by default and whose test is handed the stop and resumes it. A
  thread whose start was refused by a stop request (2.2.4) leaves the start park to the next thread.
- Emulated time stands still while (E) is parked, so no `Exit.*` key ends a run that is parked: an automated run started
  with `StartPaused` (and, as it must be, `Enabled=yes`) stays parked until a client resumes it or the harness ends
  it, and so does a selftest session whose test resumes nothing (pinned by the tests `debugger.selftest.startpaused`,
  a run with `StartPaused=yes`, `Enabled=yes` and no client, and `debugger.selftest.emuhang.parked`, a session; 6.1.1
  item 7 has what the first of them needs for the rule above).
- A selftest session (5.3) makes the same start park at the first `Park()` of every emulator thread while it is active,
  except for the threads that the selftest of `dbg::emu` starts itself.
- While (E) is parked the window shows the last swapped frame; it is not repainted and shows no OSD. Entering a stop,
  the park loop sets the window title to `nullDC <version> [paused by debugger]` (`os_SetWindowText`), `<version>` being
  the build's version string (`NULLDC_VERSION`, 3.1; header revision 8, H8-4), where it is not on the window already:
  at the first stop of a thread, and when the running machine has written its frame rate there since. The title stays
  until the running machine writes its frame rate again. The emulator thread makes no window call
  behind its stop callback (the thread of the user interface may be waiting for it there, and a title call waits for
  that thread on some hosts). The UI thread never blocks on a parked emulator thread. Headless there is no window and
  the call does nothing.

### 2.3 Execution control

#### 2.3.1 State on the wire

One global state, as the protocol has: `running` or `paused`, plus (3.2) `stopId`, `reason`, `breakpointId`,
`registersStale`, `notice`.

- `stopId` counts stops. It is incremented on every transition into a state reported as `paused` (park for any cause,
  `Stopped`, `NoMachine`), on every completed step (paused -> paused), on a reset performed while paused, on a stop that
  a breakpoint of the ARM7 or of the DSP, or the end of an SGC capture, makes while a suspended sound burst is run on for
  a resume or a step (paused -> paused, no `running` tick), and on `Reset` / `LoadState` in `Stopped`. Service excursions and resumes do not change it. It never decreases in a process.
- `reason` values the server sends (the client treats the field as an open string):

  | `reason` | When |
  | --- | --- |
  | `pause` | `control.pause` honoured |
  | `step` | `control.step`, `stepOver` or `stepOut` completed (a single step, the stop of a temporary breakpoint, the return a step-out waited for) |
  | `breakpoint` | a code breakpoint hit |
  | `event` | an event breakpoint hit |
  | `reset` | reset performed while a debugger stop was in force or a pause was wanted |
  | `start` | the `StartPaused` park |
  | `capture` | an SGC capture armed at a stop (started from `paused`) took its last frame and paused again (M3; 3.3) |
  | `host-stopped` | lifetime `Stopped`: the host stopped the machine (today: menu, pause in the emulator's own GUI), or the machine exists and no thread has started yet |
  | `guest-exit` | lifetime `Stopped` after `reios_exit` |
  | `no-game` | lifetime `NoMachine` |

  If several causes coincide in one stop, the one published is the first of `reset`, `start`, `breakpoint`, `event`,
  `step`, `pause`, `capture`; among breakpoints of the winning cause the lowest id. `capture` ranks below every other
  cause: a stop that meets it wins, and the capture's response is sent all the same (3.3).
- `reason` is repeated in every paused tick of the same stop. Running ticks carry neither `reason` nor `breakpointId`.
- `breakpointId` is carried by every paused tick of a stop with reason `breakpoint` or `event` for as long as that
  breakpoint exists. When the breakpoint is removed during the stop, the paused ticks of that stop stop carrying it; the
  `reason` stays. So a `breakpointId` in a tick is always a key of `breakpoints` in the same tick, and the two reasons
  can come without one. A client that meets an id it cannot find announces it as `#<id>`.
- **No game loaded** (`reason: "no-game"`): ticks carry `registers: {}`, and the client drops every register value it
  holds while that reason lasts (the per-path merge of 3.2 would otherwise keep the values of the machine that is gone).
  When a game starts, the machine is created (`Stopped`, reason `host-stopped`, a new `stopId`) and its thread started:
  that is a new stop when the machine comes up parked (`reason: "start"`, `stopId` incremented) and otherwise a running
  tick. As built the thread start itself publishes `Running` (`OnThreadStarting()`, 2.2.4), so a machine that comes up
  parked sends three ticks behind the last `no-game` one: `host-stopped`, then the `running` tick of the thread start,
  then the `start` stop. The client reads its views again when the reason leaves `no-game`, whatever follows it.

#### 2.3.2 Run control commands

Requests are answered as soon as they are validated and armed (the client has a 10 s timeout); completion is reported by a
tick. All are `CC_EXEC` calls except `control.pause` (which only calls `dbg::emu::RequestPause()`) and `control.reset`
(`CC_WRITE`).

| RPC | `Parked` | `Running` | `Stopped` / `NoMachine` |
| --- | --- | --- | --- |
| `control.pause {target?}` | `{}`, nothing changes | request the pause, `{}`; tick `paused`, `reason: "pause"` when parked | `-32001` / `-32002` |
| `control.runUntil {}` | resume, `{}`; tick `running` | `{}`, nothing changes | `-32001` / `-32002` |
| `control.step {target:"sh4"}` | one SH4 instruction (below), `{}`; one tick `paused`, new `stopId`, `reason: "step"`; no `running` tick in between | `-32003` | `-32001` / `-32002` |
| `control.stepOver {target:"sh4"}` | below: a step, or a resume (tick `running`) until the temporary breakpoint stops (tick `paused`, `reason: "step"`) | `-32003` | `-32001` / `-32002` |
| `control.stepOut {target:"sh4"}` | below: a resume (tick `running`) until the return (tick `paused`, `reason: "step"`) | `-32003` | `-32001` / `-32002` |
| `control.step / stepOver / stepOut {target:"arm7"}` | `step`: one ARM7 instruction, `{}`; one tick `paused`, new `stopId`, `reason: "step"`; no `running` tick in between (from a stop outside a burst the SH4 runs up to one AICA tick first: 2.5.3). `stepOver` on a `BL`, and `stepOut`: a resume (tick `running`) until the temporary breakpoint stops — or a stop at once, with no `running` tick, when it is reached in the burst that was suspended. `-32012` while the ARM7 is held in reset | `-32003` | `-32001` / `-32002` |
| `control.step / stepOver / stepOut {target:"dsp"}` | `step`: one DSP step, `{}`; one tick `paused`, new `stopId`, `reason: "step"`, `STEP` advanced by one (127 wraps to 0, at the end of the sample); no `running` tick (from a stop with no burst suspended the SH4 runs up to one AICA tick first); also while the ARM7 is held in reset: the AICA is clocked whatever ARMRST says (2.5.2, 2.5.4). `stepOver` / `stepOut`: `-32004` | `-32003` | `-32001` / `-32002` |
| `control.reset {}` (M2) | `Reset()` directly through the emulator's existing reset path; an owed remainder is dropped (2.3.4 h); stays paused, new `stopId`, `reason: "reset"` | asks the machine for a guest reset (`GUEST_RESET`): the machine resets and keeps running, nothing is published; an event-log entry `dc.system.reset` | `-32001` / `-32002` |

`control.pause`: there is one run state, so `target` is ignored when it is one of `sh4`, `arm7`, `dsp` — also when the
server does not list that target, and never `-32004`. Any other value is `-32602`. A request that is still pending when
the emulator thread exits is dropped.

Run control is refused, with nothing armed, when another action is already armed for the same stop (`-32003`).

**SH4 step.** One instruction is executed by the active backend (R12; 2.3.4 i, k); a delayed branch executes together
with its delay slot, taken or not. Preconditions (`dbg::sh4::StepCheck`), checked on (E) before anything executes, with
the opcodes read through `dbg_mem` — on failure the in-band error `-32005` and no stop: PC is odd (the emulator's text:
`cannot step: pc is odd`); the word at PC is not readable (`cannot step: pc not readable`); the opcode at PC is not an
instruction the interpreter table executes (`cannot step: undefined opcode`; executing it ends the process in `NO_MMU`
builds); PC holds a delayed branch whose slot word is not readable, is undefined (a word without a handler in the
opcode table, `0x0000` included) or is an instruction that writes PC (`cannot step: illegal slot instruction`; the
decoder `verify`s that). The reios trap `0x085B` is an instruction of that table: a step on it is allowed and executes
the whole HLE call. In `WITH_MMU` builds the words are read without address translation: steps in translated code
(outside P1 / P2) are not supported. Documented behaviour:
- Time: a step is charged as a block of that one instruction (2.4).
- If leaving the park context accepts a deferred interrupt (2.2.4 c), the step ends at the vector without executing an
  instruction. This holds for a step of every kind: a step over or out asked for there is complete at the vector too.
  Only one step in a row can end that way: the step after it starts at the handler's first instruction with `SR.BL`
  set and cannot be interrupted. The contract holds a server to it (5.5 S04: a second step in a row that ends at a
  vector fails). Behind a step that completed with `reason: "step"` nothing is deferred (a step accepts its
  interrupts itself, next item), so a step out asked for there resumes and its `running` tick comes, which is what
  S04 waits for.
- When the step uses up the slice, `UpdateSystem()` runs (scheduler callbacks fire) and a pending interrupt is accepted:
  the step ends at `VBR + 0x600`.
- `RTE`: its end calls `UpdateINTC`, so the step can end in the next handler.
- SLEEP: one step runs the wait as a run does: it ends at the handler when an interrupt is accepted, or on the SLEEP
  again after the handler's bound of 1002 slices (448 896 cycles). A host stop requested meanwhile is honoured when the
  step returns.
- If an event or sound breakpoint fires during the step, the step ends at that slice boundary and the stop is reported
  with that breakpoint.
- A step from a breakpoint stop executes the instruction of the stop (the resume exemption, R9). A breakpoint on the
  instruction a step lands on does not make the stop a breakpoint stop: the step is complete in front of it,
  `reason: "step"`. A step whose own instruction has an active breakpoint that the exemption does not cover (the stop
  was not the stop of that check: a pause that landed in front of it, a PC written onto it) is stopped by the check:
  nothing runs, `reason: "breakpoint"`.
- The interpreter executes the slot of a `BT/S` or `BF/S` that is not taken as an instruction of its own, so in a run a
  slice can end, and an interrupt be accepted, between the two; a block of the recompiler holds both. A step is the same
  unit in both — the branch with its slot — except there: when the slice ends between the two and an interrupt is
  accepted or a stop latched, the interpreter's step ends where the loop is (at the handler, or in front of the slot),
  and a step that starts on such a slot (the handler's `RTE` returned to it) runs the slot alone.

**SH4 step over.** At `BSR`, `BSRF` or `JSR`: a temporary breakpoint at `PC + 4` with `sp0 = R15`, then resume; it stops
only when `R15` is at the level of the request (`OnLevel(R15, sp0)`, 2.3.4 j — a recursive callee reaches `PC + 4` at a
deeper stack level first). At `TRAPA`: the same at `PC + 2`. At any other instruction: a step. A temporary-breakpoint
stop is reported with `reason: "step"` and no `breakpointId`. A listed breakpoint inside the call ends the step over
with its own stop.

**SH4 step out.** Resume with a step-out pending: the emulator stops immediately after the `RTS` or `RTE` (and its delay
slot) that leaves the activation in which the request was made, `reason: "step"`. Which return that is, is decided by the
stack-level rule of 2.3.4 j (calls made at the request's level are counted, handlers entered after the request are
skipped whole). Its limits are documented in the UI help (2.3.7 item 1). (`PR` is not used: guest code restores it only
just before `RTS`, so after a function's first call it points into the function itself; in a handler the return address
is `SPC`.)

Temporary breakpoints and a pending step-out are internal: never listed, never muted, and removed on every published
stop, whatever caused it (a service excursion, which publishes nothing, leaves them).

#### 2.3.3 Breakpoints, and the requirements on every SH4 backend

Breakpoints are server-owned; ids are allocated from 1 and never reused within a server process (watch ids likewise,
2.8); in the tick they are keyed by the decimal id string. Every `breakpoints.add` creates a new breakpoint (no
de-duplication: the same request twice gives two ids); when several match, the lowest id is reported.

- A code breakpoint is `(event, address)` with events `dc.sh4.cpu.pc`, `dc.aica.arm7.pc` (M2), `dc.aica.dsp.step` (M3).
  `address` is required: any u32 for SH4 (odd -> `-32602`), a multiple of 4 up to `0x00FFFFFF` for ARM7 (above:
  `-32602`), steps 0..127 for the DSP (else `-32602`). These three
  names are not events: no node lists them, and an event breakpoint cannot be set on them (`-32004`). An active DSP
  breakpoint is reached in every sample (2.5.4).
- An event breakpoint is `(event)` with `kind: "event"`; the event must be one the server's device tree lists (2.6,
  2.8), and a server accepts a breakpoint on every event its tree lists. A name that is a row of `dbg_events.h` and in
  no node is refused like any unknown name, `-32004` (since header revision 7 every row of `dbg_events.h` is listed by
  exactly one node: the row `dc.debugger.dsp_forced`, which none listed, went with forcing, 2.5.5). An `address` given with
  `kind: "event"` is ignored and not stored: the breakpoint a tick lists has none.
- The order in which `breakpoints.add` checks a request, and so which error wins, is in 3.3.
- Category (as devtools-protocol.md 5.6): `kind == "event"` -> `events`; a code breakpoint is in the category of its
  target: `sh4`, `arm7`, `dsp`.
- Two sets are kept apart. A code breakpoint is **armed** while it is enabled (every temporary breakpoint is armed): the
  armed set decides where a backend tests. A breakpoint is **active** iff it is enabled, its category is not muted and (no
  category is soloed or its category is soloed), and at least one client is connected or the grace period of 2.9 has not
  expired: activity is evaluated at each arrival and decides whether the arrival stops. The core hands the emulator
  side the enabled sets (`emu::SetSh4Breakpoints`, `emu::SetArm7Breakpoints`, `emu::SetDspBreakpoints`,
  `emu::SetEventBreakpoints`) and one mask of the active categories (`emu::SetActiveMask`); before the first
  `SetActiveMask` the mask is 0 and nothing stops.
- Address compare (`dbg::sh4::MaskAddr`): in `NO_MMU` builds (the default) SH4 code addresses are compared as
  `addr & 0x1FFFFFFF` — breakpoints, temporary breakpoints and the unwinder's "executable address" test — because the
  same code is reachable at `0x0C..`, `0x8C..`, `0xAC..` (reios boots discs at `0xAC008300`, ELFs at `0x8C010000`). In
  `WITH_MMU` builds P1 / P2 addresses are compared masked and all others on the full 32 bits. The tick reports the PC
  unmasked and the breakpoint address as the client gave it; clients compare masked (2.3.4 f). ARM7 code addresses are
  compared under `dbg::arm7::MaskAddr`: under the mask of the wave memory with bits 0 and 1 dropped (2.5.3); the tick
  reports the ARM7's PC unmasked.
- Hit: park; tick `paused` with `reason: "breakpoint"` and `breakpointId`.
- Event breakpoints: `dbg::EventSlow` (2.6) tests the enabled event set on (E) while the guest executes; a hit records a
  freezing cause, which the slice hook latches in the `UpdateSystem()` call that is running or the next one (2.2.7).

**Requirements on SH4 code breakpoints and steps (every backend; observable behaviour).**

- R1 — *What runs.* A breakpoint and a step are executed by the backend that runs the guest. On a backend that reports
  `SH4DBG_NATIVE` (2.3.4 l; the interpreter and, where the System V calling convention is used, the x64 JIT) nothing
  changes engine because a breakpoint is listed, armed or hit, or for a step.
- R2 — *Guest memory* is never modified to implement a breakpoint (no opcode patching).
- R3 — *Cost when unused.* With nothing armed, a JIT's compiled code and dispatcher contain no added instruction; the
  only cost is one load and branch per *decoded* instruction. The interpreter runs its unmodified loop.
- R4 — *Arming.* Adding, removing, enabling or disabling an SH4 code breakpoint, and adding or removing a temporary
  breakpoint, discards the compiled blocks that overlap the 4 KiB page of that address, in every mirror, and nothing
  else; a change that moves no address in or out of the armed set discards nothing. Category mute / solo, client
  presence and (later) conditions and hit counts are evaluated at each arrival and never need a recompile. A step-out
  discards blocks by their end type (2.3.4 j).
- R5 — *A test that answers "continue" changes nothing.* Block boundaries, block costs and the points at which the slice
  counter is tested are unchanged: a run whose tests all answer "continue" (breakpoints never reached, or inactive) has
  the same guest state and the same scheduler time, instruction for instruction, as a run with no breakpoints.
- R6 — *Stop.* A stop reports PC = the instruction in front of which it stopped. Nothing of that instruction has
  executed; the complete context (integer, FP, SR with T, FPSCR, PR, MAC, banks) is in `Sh4cntx`; no further guest
  instruction executes and no scheduler time passes before the park context is entered.
- R7 — *Time at a stop.* The scheduler is not advanced. The slice counter holds the block cost charged for the
  instructions that ran; the cost of the rest of the block is refunded (2.3.4 b). With a cost model that is additive over
  instructions that is exactly what the timing tier charges up to the instruction; in `fast` timing, where a block is
  charged as a unit, it is the proportional share.
- R8 — *Stop + resume is neutral.* A stop followed by a resume is identical to no stop — same instruction stream, same
  total cycles, same block boundary after it, same slice-end points — in every timing tier, provided the PC was not
  written and no step, reset or state load was made during the stop.
- R9 — *Resume exemption.* A resume or a step from a stop at X executes the instruction at X once without testing it;
  the next arrival at X stops again. The exemption applies only if execution continues at X: if the deferred interrupt
  acceptance of 2.2.4 c moved the PC to a vector, or an interrupt or exception is accepted before X runs, a breakpoint
  on the vector is honoured, and X stops again when the handler returns to it (X has not executed).
- R10 — *Delay slots.* A breakpoint on the slot of a delayed branch (`Delayslot` type bit, `hw/sh4/sh4_interpreter.h`)
  stops once per execution of the pair, in front of the branch, with PC = the branch and the id of that breakpoint,
  whether or not the branch is taken. Nothing is ever tested, and no stop is possible, between a delayed branch and its
  slot (the context is mid-instruction there: `RTE` has loaded SR without `UpdateSR()`, `BSR` / `JSR` have not written
  `PR`). A slot address that is reached as a branch target — also by a branch to its own slot — is an ordinary
  instruction and stops with PC = that address.
- R11 — *Addresses* are compared as stated above.
- R12 — *Step.* A step executes exactly one instruction (a delayed branch with its slot) through the same decoder, code
  generator and cost function as a run, and does the end-of-slice work a run would do when the slice counter expires.
  Arithmetic (FPU included) and time are the running backend's, not the interpreter's. `CpuRunning` is set for the
  duration of the step, so `UpdateSystem()` and the SLEEP handler behave as in a run.
- R13 — *Counters persist.* The slice counter survives `Run()` exit and entry and steps; it is reset only for a machine
  reset (`SuperH4Backend::ResetSlice()`). Leaving and re-entering `Run()` (stop and resume, service excursion, a host
  pause at a slice end) is therefore invisible to the guest.
- R14 — *Self-modifying code.* The existing handling (`bm_LockedWrite`, page protection, the check levels) is unchanged;
  a block recompiled for any reason is tested again.
- R15 — *Step preconditions.* An odd PC, an unreadable PC, an undefined opcode and an illegal slot instruction make a
  step fail with an error (2.3.2); they do not hang or end the process.

The interpreter and the x64 and arm64 JITs satisfy R1-R15 with the mechanism of 2.3.4 (gates of 2.3.6). The x64 JIT in
Windows builds until W64b and the arm64 JIT until A64 (both of the platform round) used the fallback of 2.3.5; a JIT
that does not implement the mechanism uses it, and what the fallback does not meet (R1, R12 and, until the backend's
counter persists, R13) is stated there and in the tick's `notice`. A new host port (wasm32, riscv64) starts on the
interpreter, which is native.

#### 2.3.4 The mechanism: breakpoints and steps in the active backend

This section implements D1. It is implemented and merged for the interpreter and the x64 JIT (package E4b, on the park
loop of E4a), and **the mechanism as built is the specification**: where the code and this text differ, the code and its
gates (2.3.6) are right and this text is corrected. The prototype that preceded it and its results are Appendix C; the
specification with the full code walk is `docs/nulldc3/sh4-breakpoints-spec.md` in the emulator repository; the call
sites of run control are tabulated in `tests/debugger/sh4bp/README.md`. Line references below are `libswirl/...` at the
upstream base `34a62ea8` and locate the code the mechanism was added to.

Terms. A *block* is what `dec_DecodeBlock` produces (`hw/sh4/dyna/decoder.cpp`): straight-line instructions ended by a
branch with its slot, by a fallback that writes PC / FPSCR, or by the size limits (500 SHIL ops, or
`guest_cycles >= max_cycles`; never between a branch and its slot). Its cost `guest_cycles` is a compile-time constant,
subtracted from the slice counter in the block prologue. The *slice counter* is the cycles left until the next
`UpdateSystem()`: the global `cycle_counter` of the x64 JIT (`jit/backend/x64/rec_x64.cpp`), tested by the dispatcher
after each block; the file-static `interp_slice` in the interpreter. *Time* is
`sh4_sched_now64() + SH4_TIMESLICE - counter`. A *check* is code in front of one guest instruction that calls the helper
and can stop there. A *temporary block* is compiled for one execution and never entered in the block cache; it serves a
single step (i), the *remainder* of a block that a stop interrupted (h) and the rest of a block that holds more checks
than its backend compiles into one (c). An *armed address* is a masked address with an enabled breakpoint or a temporary
breakpoint (2.3.3).

**a. Decoder query and the check op.** In `dec_DecodeBlock`, right after the opcode is read and before anything is
charged or emitted for it, for every instruction that is not a delay slot:

```cpp
const bool dbg_checks = state.ngen.DebugChecks || (limits && limits->no_compile);
...
if (dbg_checks && !state.cpu.is_delayslot && dbg_sh4_WantsCheck(state.cpu.rpc, (OpDesc[op]->type & Delayslot) != 0)) {
    Emit(shop_dbg_check, shil_param(), mk_imm(state.cpu.rpc), mk_imm(blk->guest_cycles));   // rs2 becomes the refund (b)
    state.dbg_checks++;
}
```

`dbg_sh4_WantsCheck(pc, has_slot)` is inline: nothing armed -> false (one load and branch, R3); otherwise
`IsArmed(pc) || (has_slot && IsArmed(pc + 2))`, with a per-page bitmap in front of the exact lookup.

- **A compiled check exists only on a backend with `ngen_features::DebugChecks`.** On any other JIT a compiled block gets
  no check and no flow report, whatever the query answers. Armed addresses do reach the decoder of such a JIT while its
  own `Loop()` runs: an address is armed while a breakpoint on it is enabled, and the fallback loop is selected only
  while one is active (2.3.5). The decode that the fallback makes for its charges (`DecodeLimits::no_compile`) carries
  the checks on every backend: it takes the refunds from them and compiles nothing.
- The block is **not ended** at a check: ending it would change the number of blocks and, in `fast` timing, the cycles
  charged (one per block). `shop_dbg_check` is a SHIL op at the end of `shil_canonical.h` with a native implementation
  required (`shil_recimp`). `shop_ifb` is not reused: its register flush is partial for most opcode classes, it has no
  conditional block exit, and the optimiser must know that the op reads `sr.T`.
- Size limits ignore check ops: the soft limit test and the hard `verify` use `oplist.size() - state.dbg_checks`;
  otherwise a check could move a block end. `guest_cycles`, `guest_opcodes`, `has_readm / has_writem / has_fpu`,
  `BlockType`, the idle-loop classification and the block hash do not see the op.
- `srt_waw` (`hw/sh4/dyna/shil.cpp`) treats the op as a reader of `sr.T`, as it treats `shop_ifb`. Without that, a T write
  in front of the check is deleted when a later instruction overwrites T, and the stop shows a stale T.
- An idiom that folds several instructions into one unit (the DIV32 matchers, off by default) is not matched when an
  address it would fold is armed (`dbg_sh4_AnyArmed`), nor under `DecodeLimits::max_opcodes`, nor across `end_pc`.
- Register allocation: the op is a full flush (`IsFlushOp`, `hw/sh4/dyna/regalloc.h`, shared by x64 and arm64). Every
  span is closed in front of it — `r0-r15`, the banked `r0-r7`, `gbr ssr spc sgr dbr vbr`, `mach macl`, `pr`, `fpul`,
  `sr.T`, `sr.status`, `fpscr` and the 32 FP registers — so the context is complete when the helper is called (R6), and
  nothing is cached after it: every register is reloaded from the context on its next use, which is how register writes
  made during a stop take effect. PC is not flushed: it is an argument, and the helper writes it on a stop.

**b. Refund.** The decoder samples its running cost in front of the instruction and, once the block cost is final (after
all cost post-processing, and after an override, h), rewrites `rs2` of every check:

```
P         = cost accumulated by the per-instruction charges
p_i       = the same accumulator in front of instruction i
C         = final guest_cycles
charged_i = P ? floor(C * p_i / P) : 0        // cost of the instructions in front of i
refund_i  = C - charged_i                      // cost of the instructions from i on
```

| Timing | Example | Charged in front of i | Refund |
| --- | --- | --- | --- |
| `fast`, ordinary block (`C = 1`) | any i | 0 | 1 |
| `fast`, idle-class block (`C = 90`, 3 opcodes) | i = 1 | 30 | 60 |
| idleskip off (`C = floor(1.5 P)`) | P = 8, p = 3 | 4 | 8 |
| per-instruction cost models (`C = P`) | | `p_i` | `C - p_i` |

In `fast` timing a block is charged as a unit, so "the cost up to instruction i" has no natural value; the proportional
share is the definition. It only decides what time a stop *shows*: R8 holds for any `charged_i` in `[0, C]`, because the
remainder is charged exactly the refund (h). A cost model that is not a sum (pairing, run-time carries — PLAN T3.2) keeps
the rule: the decoder samples whatever its running cost is in front of the instruction. Terms such a model charges at
run time at block entry or exit must not be charged twice by stop + remainder (entry terms belong to the interrupted
block, exit terms to the remainder).

**c. The check, the helper, leaving the dispatcher (x64).** A check is 15 bytes in its block: two arguments and a call
of the block's own tail, a piece of 10 bytes that the block has once for all its checks, which adds the third argument
and jumps to a stub that every block shares:

```
in the block, per check:        mov  arg0, pc_i
                                mov  arg1, refund_i
                                call tail
tail, once per block:           mov  arg2, block->addr + block->sh4_code_size      ; end of the block
                                jmp  check_stub
check_stub, once per backend:   push arg1 ; call dbg_sh4_Check ; pop arg1
                                test eax, eax ; jnz stop
                                ret                                                ; continue: back into the block
                        stop:   add  dword [cycle_counter], arg1                   ; the refund
                                add  rsp, 8 + STACK_ALIGN + 8                      ; the return addresses into the block and into
                                jmp  mainloop_exit                                 ; the main loop, and the frame of the block
```

A block is entered by `call rax` with the main loop's frame below it, and callee-saved registers are saved by the main
loop, not per block, so the stop path needs nothing else. `mainloop_exit` is the main loop's `end_loop` label. The
dispatcher fast path is unchanged. No `UpdateSystem()` runs on the stop path: time does not advance and the counter is
not refilled (R7). Measured: one check adds 25 bytes to a block; 56 checks on consecutive instructions add 50 bytes each
in an integer line and 42 in a floating point line, the flushes included.

*Budget.* A compiled block holds at most `ngen_features::DebugCheckBudget` checks — 56 on x64 (16 KiB over the 291 bytes
a check with its flush can take) — which bounds the host code they add whatever is armed. The decoder decodes a block to
its end whatever is armed: its boundaries and its cost are those of the block without a check. When the block has more
checks than the budget, the backend compiles the instructions in front of the check that is one too many
(`RuntimeBlockInfo::dbg_rest_pc`), and the code ends by handing the rest over (`rdv_BlockRest`): it gives the cycles of
the rest back to the slice counter and leaves the main loop, and the rest runs as a temporary block that ends where the
block ends and costs those cycles. The block is charged once and the slice counter is tested behind its last instruction
only, so the split is not visible to the guest. The rest is compiled each time it runs: a block with more than 56 armed
instructions is slow, not different.

`u32 dbg_sh4_Check(u32 pc, u32 refund, u32 block_end)`:
1. resume exemption (h): if it is armed, disarm it; if it is for this masked pc, return 0;
2. find the breakpoints at `pc`, else those at `pc + 2` (slot rule, e);
3. evaluate them in id order: the stack condition of a temporary breakpoint (j); the active state of a listed
   breakpoint (the active mask, 2.3.3; later its condition and hit count). If none stops, return 0;
4. stop: `Sh4cntx.pc = pc`; record `{pc, block_end, refund}` as the owed remainder (none when `block_end` is 0, or when
   the check stopped a step: the block of a step is its one instruction and a later resume dispatches normally); latch
   the stop through `dbg::emu::LatchStop(cause, id)` (2.2.3); return non-zero.

The helper reads guest state freely and writes none when it returns 0. An arrival that continues costs about 35 host
instructions; a stop + resume costs about 33 000 (one compile of the remainder), so anything evaluated per arrival
belongs in the helper.

**d. The slice counter survives.** The x64 main loop does not set `cycle_counter` at entry; the counter is initialised
statically to `SH4_TIMESLICE` and set again only by `ResetSlice()` (l), which `SuperH4_impl` calls for a machine reset:
in `Reset()` on the installed backend, and after a `setBackend()` that follows a `Reset()` which no emulator thread has
run since (the counters outlive a backend object). The interpreter's counter is file-static state with the same
lifetime. This is R13; it also makes an ordinary host pause and resume exact, which it was not before. A loop entered
with a counter that is not positive and no remainder to run (a flow report stopped behind the block that used the slice
up, j) does the end-of-slice work before its first block; `Step()` does not apply that rule (2.4). PLAN T3.0 moves the
counter into `Sh4Context`.

**e. Delay slots.**
- No check is emitted while decoding a slot.
- In front of an instruction with the `Delayslot` type bit (`braf bsrf rte rts jmp jsr bf.s bt.s bra bsr`) a check is
  emitted if the branch address **or** `pc + 2` is armed. A stop reports PC = the branch and the id of the armed
  breakpoint (the helper looks up `pc`, then `pc + 2`).
- Branch and slot are always in one block, and a compiled `bt.s` / `bf.s` block runs its slot whether or not the branch
  is taken, so the breakpoint fires exactly once per execution of the pair (R10).
- A block that *starts* at the slot address (it was reached as a branch target) decodes it as an ordinary instruction;
  its check reports PC = that address.
- The page discard of g covers a pair that straddles a page boundary: the block contains both addresses, so it is in
  the list of the slot's page.

**f. Address identity.** The mask `0x1FFFFFFF` (`DBG_SH4_ADDR_MASK`; the complete rule, MMU builds included, is
`dbg::sh4::MaskAddr`) is applied in the armed set (stored masked), the decoder query, the helper lookup, the exemption
and temporary breakpoints. The page discard finds its blocks through the block manager's own index, `(addr & RAM_MASK) /
REI_PAGE_SIZE`, which is the same for every mirror, and discards those that overlap the address's 4 KiB page (R4; g);
ROM at `0x00000000 + X` and RAM at `0x0C000000 + X` stay different addresses (aliasing between areas only causes extra
discards). The PC a stop reports is the address the running block was compiled for:
blocks are looked up by `fpcb[(addr >> 1) & FPCB_MASK]`, so a block compiled as `0x8C..` also runs for `0xAC..`, and the
reported PC can be another mirror of the instruction than the one the guest jumped to. The JIT does not run in
`WITH_MMU` builds today; the MMU rule of 2.3.3 concerns the interpreter.

**g. Changing the armed set.** `dbg::sh4` applies every change of the armed set (add, remove, enable, disable, temporary
breakpoints) on (E) in the park context — the park loop, or a service excursion (2.2.4) when the change arrives while
running — so no block is executing and none is on the host stack when the set changes. (The slice hook can run inside a
block, because the SLEEP fallback loops on `UpdateSystem`; that is why the hook only requests.) `SetBreakpoints` gets
the whole enabled set and diffs it against the armed set: each key that becomes armed or stops being armed updates the
masked set, the page bitmap and `dbg_sh4_armed`, and calls `DebugDiscardPage(addr)` of the active backend
(`sh4_cpu->DebugBackend()`); the JIT forwards it to `bm_DiscardAddrPage(addr)`, which copies
`page_blocks[(addr & RAM_MASK) / REI_PAGE_SIZE]` and discards each block of it that overlaps the 4 KiB page of the
address, in every mirror, and nothing else (R4). Where the block manager's page is 4 KiB that is every block of the
list; where it is 16 KiB (`REI_PAGE_SIZE` on Apple silicon) the list holds the blocks of four such pages, and only those
that overlap the address's page go (package A64 of the platform round, `7c025c83`: until then the whole 16 KiB list
went,
which `debugger.sh4bp.arm` showed on the Mac). The memory writer's discard without the virtual memory layout steps by
4 KiB through each page it unprotects for the same reason (2.7), and so does the sh4bp suite's guest load.

- Complete: `bm_AddBlock` enters a block in the list of every page between its first and last byte, so the page's list
  holds every block that contains the address, including blocks that start on the previous page and blocks that contain
  it as a delay slot. (The existing `bm_DiscardAddress` is not used: it is undeclared and erases while iterating.)
  `bm_DiscardBlock` moves a block to `del_blocks`; it never frees or overwrites host code.
- Self-modifying-code handling is untouched (R14): the discard changes neither `page_has_data` nor the page protection;
  recompiled blocks get the same `DoCheck` result as before; a guest write to the page still faults into
  `bm_LockedWrite`, and the blocks compiled afterwards carry the check again because the decoder asks at every decode.
  `FullCheck` compares guest bytes and is unaffected (R2).
- `bm_Reset` needs nothing: the armed set lives in the debugger and is consulted at decode. The same holds for the
  code-cache flush of a tier change (PLAN ground rules).
- With no emulator thread (`Stopped`) only the set changes, under L: every thread start builds a fresh cache.

**h. Resume: exemption and remainder.**

*Exemption.* When the park context is left for a resume or a step (2.2.4 e, after the deferred interrupt acceptance),
`dbg::sh4` arms the exemption iff `Sh4cntx.pc`, masked, is still the PC of the stop, `dbg_sh4_WantsCheck` is true for it
(opcode read through `dbg_mem`), and a check will be made by what runs next (the active backend has `SH4DBG_NATIVE`, or
the fallback is wanted) — otherwise it would wait for a check that never comes. The first check call clears the flag
and, if its pc matches, answers "continue". It is also cleared at the next stop, by `dbg_sh4_OnException()` (an
interrupt or exception accepted before the instruction ran: the instruction of the stop has not run when its handler
returns to it), and by `dbg_sh4_SlotRuns()` (k) (R9).

*Remainder.* If the stop came from a check in block B at instruction i and the PC is unchanged, the backend's `Loop()`
asks `dbg_sh4_TakeRemainder(pc, &end_pc, &cycles)` before it enters its main loop and first runs the rest of B as a
temporary block (i), decoded from `pc_i` with

```
end_pc = B.addr + B.sh4_code_size       // the decoder ends the block in front of this address
cycles = refund_i                       // replaces the computed cost, after the clamps
```

and then enters the main loop. The remainder starts with the check for `pc_i` (consumed by the exemption) and contains
the checks of every other armed instruction of B. Result: instructions `i..end` run once, the total charged for B is
`charged_i + refund_i = C`, the next block starts where B's successor starts, and the slice-end test happens after the
remainder as it would after B (R8). A stop inside the remainder works the same way (its refunds are computed from the
overridden cost). The remainder is decoded from current memory with the current FPSCR and armed set, so breakpoints,
registers and memory changed during the stop take effect. Nothing is owed after `dbg_sh4_TakeRemainder`, whatever it
returned.

The form D1 sketches — resume into a cached block that starts at `pc_i` — is not used: such a block has its own cost (in
`fast` timing 1, or an idle-class multiple) and, when B ended at a size limit, a different end, so stop + resume would not
be neutral; and a block registered at `pc_i` must be the one any jump to `pc_i` gets.

The owed remainder is dropped when the PC at resume differs from `pc_i` or was written during the stop (also to the
value it had: `dbg::sh4::OnPcWritten()`), when a step is made, and on reset or state load. Resume then dispatches
normally — the first block is the cached or newly compiled block at PC, its first check consumed by the exemption — and
the stop is not neutral: `charged_i` stays charged and the next block costs what it costs (in `fast` timing at most one
block cost of skew).

**i. Single step: temporary blocks.** `SuperH4Backend::Step()`; the JIT's is `rdv_RunTempBlock(&one)` with
`DecodeLimits one = { 1, 0, -1, false }` in `hw/sh4/dyna/driver.cpp`:

1. apply what the dispatcher does when it has to compile at this pc: the boot soft-reset rule and the free-space rule
   (`ResetCache()` first);
2. `rbi->Setup(pc, fpscr, limits)`: the decoder ends the block after one instruction; a delayed branch keeps its slot.
   There is no cost override: the cost is what the cost function gives a block of that one instruction (`fast`: 1 cycle,
   or the idle / syscall-area multiples);
3. `rdv_ngen->Compile(rbi, NoCheck, ...)`: no SMC entry check (the code was decoded this instant, and that check's
   failure path looks the block up in the cache). The breakpoint check of an armed PC is still emitted; the exemption
   consumes it. A compile that finds no room in what is left of the code cache has the cache emptied and is made again;
   code that an empty cache has no room for is made of fewer instructions, down to one;
4. `bm_SetTempBlock(rbi)`; `rdv_ngen->RunBlock(code)`; `bm_SetTempBlock(0)`;
5. give the code space back: if `LastAddr` is still where the compile left it, set it back to its value before the
   compile (nothing else compiles while a block runs; a cache clear from inside the block moves `LastAddr` and wins);
   delete `rbi`.

A temporary block is not registered in `fpcb` / `blkmap` / `all_blocks` / the page lists: a cached block at that PC may
exist and must stay, and a one-instruction block must never be found by the dispatcher (it would change the block
boundaries and costs of the run). The block manager knows it only through one pointer (`bm_SetTempBlock`), consulted
first by `bm_GetBlock(void* code)`, so that the memory-access rewrite on a fault finds its `memory_accesses`.

`RunBlock` is an `NGenBackend` entry, generated next to the main loop with the main loop's frame:

```
call  arg0                              ; the block
mov   ecx, [cycle_counter] ; test ecx, ecx ; jg done
add   ecx, SH4_TIMESLICE ; mov [cycle_counter], ecx
call  UpdateSystem_INTC                 ; exactly the main loop's slow path
done: jmp mainloop_exit
```

so a stop inside a temporary block leaves through the same `mainloop_exit`.

**j. Step over and step out.**

*Temporary breakpoints* are entries of the armed set with a stack condition; they are added and removed like any other
(page discard) and removed at every published stop.

*Stack test*, shared: `OnLevel(sp, sp0) = sp >= sp0 && sp - sp0 < kStackWindow` (`0x100000`) — at the level of the
request or above it, on the same stack. Below `sp0` is a deeper activation; 1 MiB or more above it is taken to be another
guest thread's stack. Step over (2.3.2) stops only if `OnLevel(R15, sp0)`.

*Step out.* State: `sp0 = R15` at the request, `level = 0`, `calls = 0`. Events are reported after the instruction and
its slot ran; the first matching row applies.

| Event | Action |
| --- | --- |
| interrupt or exception accepted (`dbg_sh4_OnException()` from `Do_Interrupt`, `Do_Exception`; `TRAPA` included) | `level++` |
| `RTE`, `level > 0` | `level--` (a handler entered after the request is skipped whole) |
| any event, `level > 0` | ignore |
| call (`BSR` / `BSRF` / `JSR`), `R15 == sp0` | `calls++` |
| `RTS` or `RTE`, `!OnLevel(R15, sp0)` | ignore |
| `RTS`, `R15 == sp0`, `calls > 0` | `calls--` (the return of something called at the request's level) |
| `RTE`, `R15 == sp0`, `calls > 0` | ignore |
| `RTS` or `RTE` otherwise (`R15 > sp0`, or `R15 == sp0` with no call outstanding) | **stop**, reason `step` |

(The simpler rule "the first `RTS` / `RTE` executed with `R15 >= sp0`" is wrong twice: a callee that uses no stack
returns with `R15 == sp0`, and so does the `RTE` of an interrupt taken in the function's body.)

*Instrumentation.* While a step-out is pending (`dbg_sh4_stepout != 0`) the decoder of a backend with `DebugChecks` marks
blocks by their end type (`dec_BlockFlow`: `BET_StaticCall` and `BET_DynamicCall` as call, `BET_DynamicRet` as `RTS`,
`BET_DynamicIntr` as `RTE`) in `RuntimeBlockInfo::dbg_flow`. The epilogue of a marked block, after it has written
`next_pc`, calls `dbg_sh4_CheckFlow(flow)` (a `DbgSh4Flow`: 1 call, 2 `RTS`, 3 `RTE`) and on non-zero leaves through
`mainloop_exit`. For an `RTE` block the report comes before `UpdateINTC`, and `UpdateINTC` still runs before leaving
(under the latch it accepts nothing and leaves the acceptance owed, so that if the `RTE` lets an interrupt in, the
resume takes it). No SHIL op and no register flush are needed (at the epilogue every span is written back); block
boundaries and costs are unchanged, and the block has been charged in full, so there is no remainder. The report can stop
behind the block that used the slice up, before the loop tested its counter (d).

*Which blocks are discarded.* Not pages: at the request, every cached block whose end type is one of the four
(`bm_DiscardFlowBlocks(false)`, a scan of `all_blocks`); when the step-out ends or is cancelled by any stop, every block
with `dbg_flow` set (`bm_DiscardFlowBlocks(true)`). Both on (E) in the park context, through the backend's
`DebugDiscardFlow`. (`ResetCache()` is not used: it also clears `page_has_data` and so changes the SMC handling of later
blocks. A permanent flag test in every call / return epilogue would cost run time with no debugger; single-stepping to
the return is slow and, in `fast` timing, changes the time charged.)

**k. Interpreter-type backends.** `SH4IInterpreter::Loop()` runs a separate debug loop (`LoopDebug()`) while anything is
armed or a step-out is pending; the normal loop is untouched apart from the persistent counter and from running the last
instruction of a slice apart from the others (so that the loop knows, when it is left there, whether a slot is what
runs next). The choice is made at `Loop()` entry; every change of the armed set while running goes through a service
excursion, which re-enters `Run()`. The debug loop has the normal loop's slice structure and counter; one iteration:

```
addr = next_pc; next_pc = addr + 2; op = IReadMem16(addr)
if (addr == slot_pc)            dbg_sh4_SlotRuns()                                      // the slot of an untaken BT/S, BF/S
else if (dbg_sh4_WantsCheck(addr, has_slot(op)) && dbg_sh4_Check(addr, 0, 0)) return   // stop: nothing ran, nothing charged
slot_pc = none
ExecuteOpcode(op)
if (op is BT/S or BF/S and was not taken) slot_pc = addr + 2
l -= CPU_RATIO
if (step-out pending) { report call / RTS; if (dbg_sh4_StopPending()) return }
```

- A stop returns with the pc back on the instruction and the counter unchanged. No remainder exists (`block_end = 0`);
  the loop is exact by construction.
- The interpreter runs the slot of an untaken `BT/S` / `BF/S` as a separate instruction. `slot_pc` suppresses the check
  in front of it once — it holds for the instruction that runs next only, and only if that is the slot: an interrupt or
  exception accepted in between returns to the slot as to any instruction — so the breakpoint fires once, in front of
  the branch, as in the JIT (R10). `dbg_sh4_SlotRuns()` tells the breakpoint state that the slot runs without a check,
  which ends the resume exemption.
- `Step()`: one iteration plus the end-of-slice work if the slice ran out; after an untaken delayed branch that it ran
  itself, the slot too, so a step is the same unit in both backends (the exception is in 2.3.2: a slice that ends
  between the two).
- Step-out: calls and `RTS` are reported by the loop after `ExecuteOpcode`; `RTE` reports from its handler between
  `next_pc = newpc` and its interrupt check, the same order as the JIT epilogue.
- In MMU builds the debug loop and `Step()` keep the normal loop's `SH4ThrownException` catch.
- `SuperH4_impl::Step()` is a forward to the backend's `Step()`.
- The cycle-accurate backend (PLAN track 4) follows this subsection, and so does every new host port, which starts on
  the interpreter.

**l. Interface between the backends and the debugger.** Everything is (E) only. As built (the headers are the contract;
their comments carry thread, state and blocking rules):

```cpp
// debugger/debugger.h — what backends call. With NULLDC_DEBUGGER OFF: inline stand-ins, the query is constant false.
#define DBG_SH4_ADDR_MASK 0x1FFFFFFF
enum DbgSh4Flow { DBG_SH4_FLOW_NONE = 0, DBG_SH4_FLOW_CALL = 1, DBG_SH4_FLOW_RTS = 2, DBG_SH4_FLOW_RTE = 3 };
extern u32 dbg_sh4_armed;                                          // number of armed addresses
bool dbg_sh4_IsArmed(u32 pc);
static inline bool dbg_sh4_WantsCheck(u32 pc, bool has_slot);      // a
bool dbg_sh4_AnyArmed(u32 pc, u32 bytes);                          // a: idioms that fold instructions
extern "C" u32 DYNACALL dbg_sh4_Check(u32 pc, u32 refund, u32 block_end);       // c
void dbg_sh4_SlotRuns();                                           // k: the slot of an untaken conditional branch runs
extern u32 dbg_sh4_stepout;                                        // non-zero: instrument call / return blocks (j)
extern "C" u32 DYNACALL dbg_sh4_CheckFlow(u32 flow);               // j
void dbg_sh4_OnException();                                        // from Do_Interrupt / Do_Exception (h, j)
bool dbg_sh4_StopPending();                                        // did a check or flow report stop
bool dbg_sh4_TakeRemainder(u32 pc, u32* end_pc, u32* cycles);      // h: handed out once

// hw/sh4/sh4_if.h — struct SuperH4Backend. Default bodies: a backend that implements nothing gets the fallback (2.3.5).
enum { SH4DBG_NATIVE = 1, SH4DBG_CTX_AT_SLICE = 2 };
virtual u32  DebugCaps()                              { return 0; }      // constant for the life of the backend
virtual bool Step()                                   { return false; }  // i, k; false: the backend cannot step
virtual int  SliceCyclesLeft()                        { return 0; }      // the counter the guest code executed last was charged to
virtual void ResetSlice()                             { }                // d
virtual void DebugDiscardPage(u32 addr)               { }                // g; 2.7: a RAM write without the virtual memory layout
virtual void DebugDiscardFlow(bool instrumented_only) { }                // j
// SuperH4_impl::DebugBackend() returns the backend object that Run() drives.

// hw/sh4/dyna: decoder.h, blockmanager.h, ngen.h, driver.cpp
struct ngen_features { ... bool DebugChecks; bool CtxAtSlice; u32 DebugCheckBudget; };   // recSH4::DebugCaps() reports the first two
struct DecodeLimits  { u32 max_opcodes; u32 end_pc; s32 cycles; bool no_compile; };      // 0 / 0 / negative / false: an ordinary block
u32  dec_BlockFlow(BlockEndType type);                                   // DbgSh4Flow of a block end
void dec_DecodeBlock(RuntimeBlockInfo*, u32 max_cycles, const DecodeLimits* = 0);
void RuntimeBlockInfo::Setup(u32 pc, fpscr_t, const DecodeLimits* = 0);  // fields dbg_flow, dbg_rest_pc
void rdv_RunTempBlock(const DecodeLimits*);                              // only with DebugChecks
void DYNACALL rdv_BlockRest(u32 pc, u32 cycles, u32 end_pc);             // c: the rest of a block over the budget
void bm_DiscardAddrPage(u32 addr);
void bm_DiscardFlowBlocks(bool instrumented_only);
void bm_SetTempBlock(RuntimeBlockInfo*);
struct NGenBackend   { ... RunBlockFnPtr_t RunBlock;                     // 0 without DebugChecks
                       virtual int  SliceCyclesLeft(); virtual void ResetSlice();
                       virtual bool KeepsSlice(); virtual void SetSliceCyclesLeft(int); };   // 2.3.5: counter hand-over
```

The two helpers that generated code calls are `extern "C"` and `DYNACALL`. The interpreter reports both capability bits;
the x64 JIT sets `CtxAtSlice`, and `DebugChecks` with `DebugCheckBudget` in builds with the debugger, on Windows too
since
W64b of the platform round (5.8); the arm64 JIT sets the same since A64 of that round (its budget 113, 2.3.5). The
debugger-side state behind these
functions — the armed set and its page bitmap, the exemption, the owed remainder, temporary breakpoints, step-out state —
lives in `dbg_sh4.cpp`; run control reaches it through `dbg_sh4.h` (6.1, contract 9).

#### 2.3.5 The backends, and the fallback

Three SH4 engines exist (D7): the interpreter, the x64 JIT and the arm64 JIT. A JIT backend sets
`ngen_features::DebugChecks` when it implements all of the following. The x64 JIT does, on Linux and, since package W64b
of the platform round (`PLATFORM-ROUND.md` PL-D38), on Windows, where the gates of 2.3.6 passed on the Windows host
with the check stub's shadow space and the Windows flush budget (5.8): it reports `sh4-debug-native` on both. The arm64
JIT does too, since package A64 of the platform round (`PLATFORM-ROUND.md` 2.15, PL-D86), on macOS and on Linux arm64,
where the gates of 2.3.6 run since then: it reports `sh4-debug-native`, and `sh4-debug-fallback` only with
`Debugger:ForceFallback=yes` ("The arm64 JIT in the platform round", below).

| Item | x64 (2.3.4) | arm64 (A64, as built) |
| --- | --- | --- |
| `shop_dbg_check` | c | four instructions per check (the pc and the refund in `w0` / `w1`, a call of the block's tail); one tail per block with checks (the block's end in `w2`), one check stub at the start of the code cache; the flush comes from the shared `regalloc.h` |
| leave without time advance | `jmp mainloop_exit` | the stub leaves through the main loop's one exit, without the end of the slice and without storing `w29` over the pc the helper wrote |
| refund | `add [cycle_counter]` | the stub adds it to `w27` and stores the counter |
| counter survives (`KeepsSlice()`) | static initialisation | a process-static counter, loaded into `w27` at every entry of `Mainloop` and `RunBlock` and stored at every exit and in front of every `UpdateSystem` the generated code makes (the end of a slice, SLEEP's handler) |
| `RunBlock` | generated thunk | generated at `Init` with the main loop's frame (`x19-x30`, `d8-d15` whole) and exit; a temporary block is charged without a test, stores its next pc at every end and branches to `RunBlock`'s tail (the counter's test, the end of the slice, the exit); it links nothing and nothing links it (a `verify` in `rdv_LinkBlock` and `DynaRBI::Relink`) |
| step-out report | epilogue (j) | in front of `relink_offset`, behind the store of the pc, for call / ret / intr ends; an RTE's `UpdateINTC` made before the exit on a stop |
| check budget | 56 per block, rest outside the cache | 113 per block (16 KiB less the tail, over a check and its worst flush); the rest of a block over it through `rdv_BlockRest`, never linked; a compile that does not fit into what is left of the code cache gives the block up (`block->code` 0, the cache as it was) |
| `Sh4cntx.pc` at a stop | the helper writes it | same; the end of a slice stores the pc (`w29`, which every static and conditional block end sets to its target) into the context (2.2.8) |

Since A64 the arm64 JIT also takes the x64 slice model (PL-D86): a block's prologue tests the counter in front of its
charge, so a slice ends behind the block that used it up, as on x64 and in the fallback loop (a block whose test finds
the counter spent is dispatched again behind the end of the slice, never charged and not run); `intc_sched` and
`rdv_DoInterrupts` left the arm64 path. Until A64 it tested the counter at block *entry* in front of the charge of the
block that overdrew it and restarted the block after an interrupt. Every arm64 slice end moved with it, and so did its
state hashes (5.7). It still lacks the SH4 FPU fast-tier fixes (PLAN's backlog): its results are held to its own
references, not compared with the interpreter's.

**Fallback** — only for a JIT backend that does not report `SH4DBG_NATIVE` (none in the tree since the platform round:
the x64 JIT on Windows was one until W64b, the arm64 JIT until A64; later, a new JIT port until it implements the items
above), and for the test key `Debugger:ForceFallback=yes`, which makes the debugger treat the active JIT as such a
backend (it has no effect on the interpreter):

```cpp
void SuperH4_impl::Run() {
    ...; sh4_host_fp_resync();
    if (dbg::emu::UseFallbackLoop()) sh4_fallback_loop();     // hw/sh4/sh4_dbg.h
    else sh4_backend->Loop();
}
```

- `UseFallbackLoop()` is asked once per entry of `Run()`. It is true while breakpoints and steps of the active backend
  come from the fallback and `dbg::sh4::FallbackWanted()` is true: an enabled listed breakpoint exists whose category is
  active, a temporary breakpoint exists, or a step-out is pending. An armed set that is not empty does not make it true:
  while the active mask lacks the category of the enabled breakpoints, the backend's own `Loop()` runs with their
  addresses armed, and what it compiles gets no check (2.3.4 a). A step on such a backend is `sh4_fallback_step()`.
- The configured backend object and its block cache are never destroyed by the debugger (`setBackend` is not called).
- `UseFallbackLoop()` changes only in the park context, through a `CC_TABLE` call (the first breakpoint becomes active;
  the last is removed, disabled, muted, or the client grace period expires) or a step. While running, that call runs in
  a service excursion — latched at a slice end (2.2.3); when the park context is left the loop for the next `Run()` is
  selected, without parking, and an event-log entry (`dc.debugger.engine`) records each switch.
- A fallback must charge time as the active timing tier does (D1). The fallback loop executes through the interpreter's
  handlers and charges **by the decoder's blocks**: at each block start it decodes the block at PC
  (`RuntimeBlockInfo::Setup` with `DecodeLimits::no_compile`, nothing is compiled), charges its `guest_cycles`, and
  interprets instructions until the block's end (a control transfer, or `next_pc == addr + sh4_code_size`) before it
  tests the counter. That decode carries the checks with their refunds whatever `DebugChecks` says: a stop inside a
  block refunds as in 2.3.4 b, and the resume runs to the same block end for the refunded cost. `sh4_fallback_step()`
  charges a one-instruction block the same way. That is the active timing tier's charge in every tier, because the
  decoder alone defines block boundaries and costs.
- The slice counter. Where the JIT backend keeps its counter across `Loop()` (`NGenBackend::KeepsSlice()`; the x64 JIT
  does), the fallback continues that counter when it runs behind the backend's loop and hands its own back when the
  backend's loop or `Step()` runs behind it (`SetSliceCyclesLeft()`): a switch between the two loops moves no slice end.
  Behind a backend that restarts its slice at loop entry the counter is the fallback's own, such a backend does not
  meet R13, and every exit and re-entry of its loop — a pause, a service excursion, a switch to or from the fallback
  loop — moves SH4 time by less than one slice (documented). `SliceCyclesLeft()` reports the counter that the guest code
  executed last was charged to, so `dc.timing.SH4_CYCLES` does not go back across a step or a switch on a backend that
  keeps its counter. Every JIT of the tree keeps it since A64 (the arm64 JIT restarted it until then: there a step
  could take `SH4_CYCLES` back by less than a slice, and S04 was recorded, not gated, by `PLATFORM-ROUND.md` PL-D78,
  5.5.4).
- What the fallback cannot match is arithmetic: the interpreter's FPU paths differ from the JIT's (devcast-fpu.md). So
  while the fallback loop is selected, and in the tick of a fallback step, `executionState.notice` is
  `"SH4 runs on the interpreter while breakpoints are armed (JIT backend <name> has no native breakpoints): FPU results can differ from a run without breakpoints"`
  (`NOTICE_SH4_FALLBACK`). `NOTICE_SH4_FALLBACK_TIMING` is reserved for a fallback that does not charge by the decoder's
  blocks; `sh4_fallback_loop()` does, so it is never set. Whether or not a breakpoint exists,
  `dc.timing.SH4_DEBUG_ENGINE` reads `interpreter-fallback` on such a backend (`native` otherwise) and
  `describe.capabilities` carries `"sh4-debug-fallback"` instead of `"sh4-debug-native"`.
- `dc.timing.SH4_BACKEND` (`interpreter`, `x64`, `arm64`), `SH4_DEBUG_ENGINE`, the capability and the notice stay
  although there is no user-facing engine choice (D9): they tell what executed. The engine is known from the first
  `Park()` of the first thread of a machine (`dbg::emu::GetSh4Engine`), so a `debugger.describe` made at the
  `StartPaused` stop carries the capability.

**The arm64 JIT in the platform round** (MACb on the Mac and LA64 on Linux arm64 met it; A64 made it native). MACb
and LA64 built and ran it without the items of the table and with two properties more: it kept the next pc in a host
register across `UpdateSystem()` (no `SH4DBG_CTX_AT_SLICE`, 2.2.8), so its running ticks said `registersStale` and
left out `dc.sh4.cpu`; and it started its slice counter anew at every entry of its loop, so that a pause, a step or a
host stop moved SH4 time by less than a slice (446 cycles back in the step S04 makes, on both arm64 systems). Its
breakpoints and steps came from the fallback, with or without `Debugger:ForceFallback`; S04 was recorded there by
PL-D78's rule of `contract.py` (5.5.4), and thirteen targets that read the running SH4's context or compared a stopped
and resumed run's state hash with an untouched one (`debugger.selftest.emu`, `.regs`, `.core`, `.hostcycles`,
`.writesrun.neutral` and the eight `debugger.sound.neutral.*.jit`) were registered on every arm64 host as `KNOWN LIMIT
(A64)` (the owner's decision of 2026-10-22). At the owner's request A64 was added to the round before its gate and gave
the JIT the items of the table and the x64 slice model in two steps (the kept counter, the prologue's test in front of
the charge, the pc stored at every slice end, `CtxAtSlice`, the frame that saves `d8-d15` whole; then the native
checks, `RunBlock`, temporary blocks and the budget). Since then the thirteen targets pass on every arm64 host, their
list, `debugger.a64.knownlimits` and PL-D78's rule are gone, the sh4bp suite runs on arm64 Linux and macOS (2.3.6) and
the contract gates pass in full there on all three engines (5.4).

#### 2.3.6 Gates for 2.3.3 to 2.3.5

The gates exist in two places and both run in ctest (5.2, 5.3).

- `tests/debugger/sh4bp/` — the mechanism as the emulator compiles it (`dbg_sh4.cpp`, the decoder, the recompiler of
  the host with its main loop and block manager, the interpreter, the fallback) around a small machine of the suite's
  own, with a stand-in for run control whose park context is the test itself. Tests `debugger.sh4bp.<gate>`, on three
  engines, each on those it applies to: `jit`, `interpreter`, `fallback` (that recompiler with the fallback forced).
  Hosts with an SH4 recompiler: x86-64 (Linux, and Windows since W64b of the platform round) and, since A64 of that
  round, arm64 (Linux and macOS; the gate `abi`, arm64 only, holds what `Mainloop` and `RunBlock` give a caller back,
  through every exit; `unwind` is Windows-only; `b11` counts with Linux's `perf_event_open` and says `SKIPPED` on
  Windows, on macOS, and on a Linux kernel that does not let a process count). Its README has every gate, the measured
  costs and the call-site table. The two stand-ins of that directory
  for the emulator's slice end (`machine.cpp`, `cost.cpp`) keep the latch test in front of the attention word: the
  cost of B11 was measured on that form. The emulator tests the attention word first (2.2.3), and the README's
  "Per slice" paragraph says that the stand-ins differ in this, instead of claiming the emulator's order for them.
- `libswirl/debugger/dbg_selftest_sh4bp.cpp` — the same tests on the real park loop and the real machine: sessions
  `sh4bp` (B1 to B14, none for B11, which compares two builds), `sh4bpboot` and `sh4bpbase` (B3, B6 and B7 on the booted
  guest and their baselines), as `debugger.selftest.sh4bp[.interp]`, `.sh4bpboot[.interp]`, `.sh4bpbase[.interp]` and
  `debugger.selftest.sh4bp.sessions` (`tests/debugger/sh4bp/selftest.sh`: JIT, interpreter and forced fallback agree on
  registers, step traces and time; the runs of the booted guest are in the state of their baselines, also with
  `config:Dynarec.idleskip=no`). Guest: 2ndmix.elf under reios, headless, RTC pinned, plus the guest programs of
  `tests/debugger/sh4bp/guest.s`, which the selftest writes into RAM.

*State* is scheduler time, slice counter, pc, a hash of the SH4 context and a hash of guest memory at a fixed point (a
slice end in the suite, a fixed vblank on the booted guest). A reset does not put the whole machine back (the scheduler
keeps its time, the devices their place in it), so the baseline of a run on the booted guest is the run at the same
place of a second session; two sessions of baselines have to agree, or two of three, and a session of cases that
differs from them is run twice more and fails when it differs again. (The repeat is not for host nondeterminism, which
is solved (5.6): a case whose other thread came too late to do anything is made again in its session, and that moves the
runs behind it away from their baselines. It is the one repeat left in the gates of this design.)

| Id | Test | Pass |
| --- | --- | --- |
| B1 | breakpoint on the first instruction that runs, in the middle of a block, on the slot of a `bra` and on the slot of an untaken `bt/s` | the stop is at the instruction (for a slot: at its branch), nothing of it has run, the registers are the interpreter's; the time a stop shows is the share of 2.3.4 b |
| B2 | breakpoint in a loop, resumed each time: at the block start, mid-block, on the slot of the loop's `bf/s` | hit count identical on every engine; each resume runs the instruction once; time and state at the loop exit = the run without the breakpoint |
| B3 | state at a later fixed point with: armed addresses never reached; reached addresses with the category muted; stop + resume at every arrival (ordinary block, idle-class block, SLEEP, slots, two breakpoints in one block, blocks ended by the size limit, the interrupt handler and its `rte`); a pause at every n-th slice end; pauses and breakpoints together; addresses armed and disarmed in service excursions; a step-out that stays pending; in `fast` timing and with `idleskip` off | state = baseline in every case |
| B4 | 20000 single steps | PC and integer registers after every step of the JIT and of the fallback = the interpreter's; a delayed branch is one step with its slot; a step from a breakpoint runs its instruction |
| B5 | step, step over and step out at an odd PC, an unreadable PC, an undefined opcode, a branch / undefined opcode / `trapa` in a slot, an unreadable slot; a step on SLEEP; in the emulator: a step over the reios trap | `-32005` with the reason, nothing executed, process alive; SLEEP ends on the SLEEP after 1002 slices or at the handler; the trap step lands at the program's entry |
| B6 | arm an address in an already compiled, hot block while running (a call from another thread); disarm after ten hits | it hits; the cached block has no check, then one, then none; state at the end = baseline |
| B7 | step over a call, a recursive call, `trapa`; step out of a leaf, of a non-leaf after it called a stack-less leaf, of a call that waits across interrupts, of an interrupt handler; a breakpoint ends a pending step-out | stops at `PC + 4` / `PC + 2` at the request's level; stops at the caller; all engines stop at the same PC |
| B8 | breakpoint given as `0x0C..`, `0x8C..`, `0xAC..` | each stops code that runs through any of the three |
| B9 | guest write into a page with an armed address (write-fault path and entry-check path), then re-execution | hits again, in the new code |
| B10 | PC written (also to the value it had), a step, a reset, a state loaded while stopped inside a block | the resume does not run a stale remainder; a step that a breakpoint stops in front of its instruction owes nothing |
| B11 | host instructions (user mode, `perf_event_open`) of a workload that uses nothing of the mechanism: build with the debugger against the build without; one armed address never reached against none | compiled code identical in all three; with against without: at most 48 host instructions per slice (measured 18 to 23 on the recompiler, 6 to 8 on the interpreter) and 24 per decoded instruction; one armed address: within 0.01 % at run time |
| B12 | the forced fallback on the x64 JIT | `SH4_DEBUG_ENGINE` and the `notice` report it; the loop is selected only while a breakpoint is active; on the integer workload the state, with continue-only checks and with stops, = the JIT's baseline (block charging); with FPU code time and pc are the JIT's and the comparison of the context is printed, not failed |
| B13 | interpreter: N steps with interrupts, and a run to the same instruction | identical context, memory and scheduler time |
| B14 | JIT: N steps through FPU, mixed and integer code, and runs to the same instructions | identical registers, FPU registers bit for bit; times are printed (no cost function of the tree is a sum over instructions today); the fallback's steps cost what the JIT's cost |

The suite has further gates named after what they test (`switch`: selecting and dropping the fallback at more than 1000
points of a run; `ownslot`: a delayed branch to its own slot; `dense`: 1024 armed addresses, the check budget, a code
cache that is too small; `slot`, `midpair`: the interpreter's separate slot; `vector`: a stop behind the block that used
the slice up; `arm`: what arming discards; `event`: event breakpoints and SLEEP). One gate is not of the breakpoint
mechanism: `threads` (`debugger.sh4bp.threads`, since J1) is about what the recompiler of a new emulator thread
starts with. It makes 256 backend objects behind the first, as thread starts do, and requires one call to prepare
the code region in all, the same region with the stubs at the same place and nothing behind them, no block in the
tables, and the workload ending in the state of the first object. It is in this suite because the suite's stand-in
for the host counts what the recompiler asks of it: a Linux run of the emulator cannot tell a region prepared once
from one prepared at every start (2.3.7 item 9). With one rule of the mechanism broken
in a scratch copy (the rest of a stopped block not run; the refund not given back; a slice counter started anew at
every entry) the booted-guest tests of the engines that the rule belongs to fail.

#### 2.3.7 Known limits and risks of the mechanism

The known limits of the debugger, the M2 round's among them (items 14 to 24: the writes, the ARM7's run control, the
neutrality of an exit in a debugger stop), the M3 round's (items 25 to 30) and the platform round's (items 31 to 33:
what the first runs on Windows, the Mac and Linux arm64 showed, and what A64's verifiers left).

1. **Guest threads and stack-level conditions.** Step over and step out identify "the same activation" by `R15`; a
   kernel-aware thread id is not available. Documented in the UI help: a function that does not return by `RTS` (a tail
   call by `JMP`, a context-switch primitive resumed by `RTE`) is left at the next enclosing return; a request made while
   extra words are pushed below the function's steady stack pointer can stop at the return of a later callee (still
   inside the function); another guest thread whose stack lies within 1 MiB above `sp0` can satisfy the test.
2. **Blocks keyed by PC only.** A cached block compiled under another FPSCR.PR / SZ runs with stale decode assumptions
   (no entry check), while temporary blocks decode with the current FPSCR: a step and a run can differ exactly where the
   run is already wrong. PLAN T3.4 (blocks keyed on PR / SZ) removes it. A debugger write of FPSCR would make a correct
   run wrong the same way, so `dbg::regs::Write` empties the code cache when it changes PR, SZ or the rounding mode (M2,
   `M2-ROUND.md` M2-D41).
3. **Stop + resume costs a compile** (2.3.4 c). Hit counts, conditions and log points must be answered in the helper.
4. **Linking backends** (arm64) need a non-linking block end for temporary blocks; until then they use the fallback.
   Done for arm64 by A64 of the platform round: a temporary block stores its next pc at every end and branches to
   `RunBlock`'s tail, links nothing and is linked from nothing (2.3.5).
5. **Order against the timing tracks.** The mechanism is in the tree before PLAN T3.0 / T3.2 edit the decoder's cost
   code, the x64 prologue and the interpreter loop. Those tracks keep: the running cost is sampled in front of each
   instruction; a `DecodeLimits::cycles` override is honoured after all cost post-processing; the counter survives loop
   exit; cost terms charged at run time respect 2.3.4 b. Their gates include B3. T3.3b (blocks that never cross a
   32-byte cache line in `cycle-similar`) changes block boundaries for everything alike; the refund rule is per block
   already.
6. **`fpcb` aliasing** between the boot ROM and RAM (the dispatcher clears the cache at three boot addresses) is
   untouched. `rdv_RunTempBlock` applies the same rule, so stepping through those addresses clears the cache as the
   dispatcher would. Main memory at `0x0C…`, `0x8C…` and `0xAC…` shares its slots the same way: the cache is indexed by
   `(pc >> 1)` under a mask of 32 MiB, with no address in the key, so addresses a multiple of 32 MiB apart share a slot
   (the mirror 16 MiB up, `0x8D…`, is the same memory in other slots). A debugger write that moves the PC of the SH4
   empties the code cache, so that a resume agrees with a step behind a PC written into another mirror or onto a boot
   ROM address (`M2-ROUND.md` M2-D42); a guest that changes mirror by itself is as before (PLAN's backlog: a block cache
   with the address in its key).
7. **Not run:** the ARM7's fallback and its notice (2.5.3; kept source-consistent by reading, M2), with M3's rule that
   the notice follows an ARM7 suspension only (`NoteArm7Fallback`, `M3-ROUND.md` M3-D24): no build has an ARM7
   recompiler since the platform round (`FEAT_AREC` is none on arm64 hosts too, `PLATFORM-ROUND.md` PL-D46; the arm64
   one stays unbuilt, PLAN's backlog), so a notice behind any suspension cannot be told from the rule on any host; an
   arcade board (`dc.gdrom` reading `"n/a"` there is by reading); MMU builds. **Run since the platform round:** the
   arm64 builds without the DSP recompiler (M3-D14), on the Mac and on Linux arm64; the arm64 backend's native items
   (A64: the gates of 2.3.6 on both arm64 systems, `debugger.sh4bp.abi` for its frame);
   `debugger.contract.hash.listening*`
   on both arm64 systems, green; the emitted x64 code with the Windows calling convention (`STACK_ALIGN` 40, argument
   registers, the check stub's shadow space), by the gates of 2.3.6 on the Windows host (W64b, 5.8), behind which
   `DebugChecks` is on in Windows builds.
8. **More than 56 armed instructions in one block** have the rest of the block compiled on every pass (2.3.4 c): correct
   but slow (2ndmix with 819 armed, muted addresses in its hottest blocks: 10.7 times the host instructions). A handful
   of breakpoints costs nothing.
9. **Host stop / start cycles filled the x64 code cache: done in J1.** The defect was in the upstream base and had two
   causes, both in `hw/sh4/dyna/driver.cpp`, where the cache and `LastAddr` live. First, every emulator thread makes
   a backend object, and its `Init` let the one code generator of the process write its stubs where the cache had
   got to and set the base of what a cache clear recycles behind them, so the code of every thread before stayed
   below the base for good: after about 240 to 500 host stop / start cycles in one process the 10 MB cache was full
   and the guest crawled with a cache clear at every compile. Now `recSH4::Init()` deletes the discarded blocks and
   rewinds `LastAddr` and `LastAddr_min` in front of the code generator's `Init`, which verifies that the cache is
   empty: every backend object starts with an empty cache and the stubs at the same place. Second, `recSH4::Init()`
   asked the host to prepare the code region at every call; it asks only for the first backend object of the
   process now. On Linux the second cause had no effect (the host hands the same pages out again); on Windows every
   call mapped a new 10 MB region near the program, with room for some two hundred of them (5.8). Gates:
   `debugger.selftest.hostcycles[.interp]` (5.3) and `debugger.sh4bp.threads` (2.3.6); by hand, 2200 host cycles in
   one process with no cache clear, 24 times the cache compiled, and the last cycles as fast as the first.
10. **`Sh4Context::CpuRunning` was a plain volatile** written by host threads and read on (E), and every
    ThreadSanitizer report of the run-control package: **done in J1**, as header revision 5 (Appendix B). The word
    is an `AtomicU32` (`libswirl/oslib/atomic_u32.h`, included from `types.h`) with the place, the size and the
    meaning it had; the generated code of both recompilers loads the same 32 bits with the same instruction. The
    type is not `std::atomic<u32>`, as v3 had planned it, because a `std::atomic` cannot stand where this word is:
    `Sh4Context` is an anonymous struct in an anonymous union, and g++ takes no member with a constructor there; the
    memory is constructed by nothing; and the context is cleared with `memset` and saved with `memcpy`. An
    `AtomicU32` has the size, alignment and representation of a `u32`, no constructor, and the operations and
    defaults of a `std::atomic<u32>`, made through `std::atomic_ref<u32>` (or the compiler's builtins where the
    library has none). Orders: every store is sequentially consistent (none is on a hot path); the emulator thread's
    polls, where its loop decides whether it goes on, are relaxed loads, which is the load the generated code makes;
    every other read is sequentially consistent. Emitted x64 code and the state hashes are unchanged (5.7).
11. **Guest time shifts in two kinds of debugger stop**, both documented skews of 2.4 and both measured as different
    state hashes on the JIT: an event breakpoint latched inside a SLEEP wait, and single steps.
12. **`IsRunning()` was read from several words: done in J1.** As built by E4a a reader preempted between its five
    loads, for the whole time (E) takes from the end of a guest reset to the park that follows it, could read a live
    parked machine as not running (by reading only; never observed in over a million reads). It is one word of the
    machine now, `SuperH4_impl::thread_live`, true from `Resume()` until the thread takes its stop callback (2.2.5):
    one load, and no store of that word lies on the path of a reset or a park. What the one word no longer gives is
    in 6.1.2: `resetting`, `stopping` and `StopForReset()` had no reader left (they were removed with header revision
    6), and a run flag or a `g_parked` left set behind the end of a thread is no longer seen through `IsRunning()`.
13. **ARMRST against the ARM7's clock after a machine reset.** As merged, a machine reset leaves the guest-visible
    register ARMRST reading 0 while the ARM7 is not clocked, until the guest first writes the register: the devices
    are reset in their order, `AicaDevice::Reset` releases the reset, and `SoundCPU_impl::Reset`, behind it, disables
    the core. A defect of the tree outside the debugger, and a question for the AICA / ARM7 tier work (PLAN tracks
    5, 6) and a console measurement; it is not E5's and not the debugger's. The debugger does not depend on it: its
    observable for the reset is the register `ENABLED` of `dc.aica.arm7`, not ARMRST (2.7, 2.10).
14. **A write can end the emulator.** A PC, or code bytes, written so that the guest reaches a word that is no
    instruction ends the process in the default build, as for a guest that gets there by itself (PLAN's backlog:
    undefined instructions and odd PCs are fatal). The debugger refuses an odd PC of the SH4 and stores the ARM7's PC
    aligned; nothing else is checked. The UI help says so. Of the same kind (M2, by the owner's answer to `M2-ROUND.md`
    M2-Q4 and M2-Q5): an SPSR of the ARM7 (or `SPSR_<mode>`) written with mode bits that are no mode is stored, as the
    comment of `SoundCPU::DebugSetReg` says, and ends the process at the next exception return that loads it
    (`CPUSwitchMode`, its default case), where a CPSR of that kind is refused (`-32013`).
15. **The recompiler without the virtual memory layout cannot resolve a write fault in a page of compiled code**:
    `bm_AddBlock` protects such a page in every build, and `bm_LockedWrite` measures the faulting address from
    `virt_ram_base`, which is null there. A guest that stores into a page from which a block was compiled ends the
    process. No configuration gives that build on the development machine (`M2-ROUND.md` M2-Q6). The debugger's writer
    is correct there because it removes the protection of the pages itself (2.7). E7a's verifier made the build once
    (the layout forced off): the session `writes` passes on it on both engines and on the forced fallback, 2ndmix runs
    600 vblanks to the same state hash twice, and a writer with its own unprotect left out ends the process at its own
    store (`fault_handler: unhandled SIGSEGV ... not a guest memory fault`), the fault a guest store meets there. PLAN's
    backlog: the fault resolved from the address of main memory when there is no layout.
16. **A debugger write into wave memory or video memory discards no SH4 block.** SH4 code that a guest executes from
    there (compiled without an entry check) is not invalidated by a debugger write of its bytes. The ARM7's code cache
    is: a write of wave memory, and a write of the ARM7's PC that stores another address than it had, empty it
    (`SoundCPU::InvalidateJitCache()`).
17. **`bm_LockedWrite` leaves the page marked as holding data until the next cache clear.** In the default
    configuration that is host cost. With `Dynarec.SmcCheckLevel=-1` blocks compiled from the page afterwards get
    neither protection nor a check, so a guest that later stores new code there runs the old blocks (a speed setting,
    after a debugger write).
18. **On a recompiler without `SH4DBG_CTX_AT_SLICE` (arm64)** the tick behind a write to a running target says
    `registersStale`, although the context is complete in the service excursion in which it is built. Not seen on
    x86-64, where the rule "a tick that says `registersStale` is full without `dc.sh4.cpu`" is held against a stand-in
    (the x64 recompiler built without `CtxAtSlice`, `debugger.contract.hash.listening.rule`, 5.4). Seen on the arm64
    recompiler in the platform round until A64 (MACb: `registersStale: true` in a running tick on the Mac, 2.3.5); since
    A64 the arm64 recompiler states `CtxAtSlice` too (2.2.8), no backend of the tree sends such a tick, and the modes
    `hash-listening` refuse a running tick that says `registersStale` on every host and engine.
19. **Two FPSCR properties are held by reading only:** a write that changes none of PR, SZ and the rounding mode resets
    nothing, and a changed rounding mode resets the code cache.
20. **The exit line in the tick of a latched stop.** When a check of the ARM7 or an event breakpoint latches a stop in
    the tick of an exit or host-stop request, the stop callback sees the SH4 in front of an interrupt acceptance that
    an undebugged run has already made (the latch deferred it; `OnThreadExiting` makes it behind the callback): the
    `sh4` and `hash` parts of the exit line can differ from the plain run's although the guest does not sleep. The
    neutrality of 5.6 holds for a run that goes on; the neutrality targets keep their stops early. Making it whole
    needs the deferred acceptance in front of the stop callback, a new entry of `dbg::emu` and so a header revision:
    PLAN's backlog, with the scratch test that shows it (`agents/nulldc3/e6b/fix-1/`).
21. **Step out inside the FIQ handler of the ARM7.** The temporary breakpoint stands at the live R14 (2.5.3). In FIQ
    mode R14 is the return address plus 4 (the handler returns with `subs pc, lr, #4`), so a step out asked inside the
    handler ends one instruction behind the interrupted one, after it has executed, or elsewhere when it branches.
    PLAN's backlog: a step out that knows the mode it was asked in (a header text of `dbg_arm7.h`).
22. **An unaligned PC of the ARM7 from `LDM` with the PC in the list and no `^`** is executed by the core as it was
    loaded; the debugger's breakpoint key and the word `PrepareStepOver` decodes are the aligned ones, so the `bl`
    decision of a step over can be wrong there.
23. **The ARM7 check walks the listed set linearly** (`dbg_arm7_Check`: up to 1024 entries for every checked
    instruction while the debug loop runs). A pause asked for while the guest's SH4 sleeps is taken by the 250 ms
    liveness bound of 2.2.3.
24. **What the state hash does not see** (5.7): it has the SH4 context, scheduler time, main, video and wave memory and
    the sound CPU's save state, and no part for the system bus slots, the AICA's register file and channel state, the
    drive or the PVR file. A reader of a device row with a side effect inside one of those would show only when the
    guest reads it back; the readers of the M2 rows are free of side effects by reading and by the session `regs`.
25. **A DSP breakpoint is reached in every sample.** A resume from it runs one sample and stops in front of the same
    step again; an SH4 step from such a stop is dropped (the run-on meets the breakpoint again, 2.5.2); to let the SH4
    run on, disable or mute it (the UI's help says so). A step, a look and a resume are what it is for.
26. **The fast tier's DSP has no channel fetches and no bus reads** (`M3-ROUND.md` M3-D10): `ChannelFetch`, `BusRead`
    and `MixsSendLate` are not called, so a DSP access in a playing channel's slot is not lost and the read latch holds
    only the DSP's own reads. Where the console differs: caique's cases `dsp_coll`, `dsp_mem3` (five lines on the
    emulator's machine, `KNOWN_FAILURES.md`), `sh4_latch`, `arm_latch`. The `cycle-similar` AICA of PLAN track 5 has
    caique's sound generator and calls them.
27. **The AICA is clocked whatever ARMRST says, and only the ARM7 is held** (`M3-ROUND.md` M3-D7, since W3): the
    samples, the DSP, the timers and the interrupt lines run while the guest holds the ARM7, as caique measured on the
    console. What the console does at a release — the phase of the release against the sample edge, a request latched
    for the ARM7 while it was held — is not measured, and the emulator releases the ARM7 as it did before W3.
28. **A byte write to a DSP register merges with the file's other byte** (M3-D5): the core gets the 16-bit word of the
    slot as the register file holds it behind the write. What the console does with a byte write there is unmeasured.
29. **An SGC capture's deadline is taken when its arming call has returned**, not at the request (M3-D27 says "5 s of
    host time from the request"): the time the request waited in (C)'s queue is added, so `-32014` can come up to about
    2 s late (E8b's verifiers). PLAN's backlog.
30. **A 32 MiB binary frame is copied once more by the transport**: `net::Server` puts the header in front of the
    payload by copying it, so peak memory is briefly about three times 32 MiB, and a `std::bad_alloc` there, or in
    the deferred answer, escapes `HandleText` / `TakeNotice` to `std::terminate` (E8b's verifiers). PLAN's backlog.
31. **"Boot BIOS" with no BIOS copied in ends `nulldc3.app` on macOS** (`EXC_BREAKPOINT` in `os_DebugBreak()`; the
    owner's run of MACb's `try` bundle, 2026-10-22): a crash of the windowed start, outside the debugger. By A64's
    reading reios boots `0xAC008300` from RAM filled with `0xFF`, and the opcode `0xFFFF` reaches the `die` of
    `cpu_iNimp`, on every engine. The owner decided to fix it after the round (PLAN's backlog). An automated run ends a
    fatal signal with Linux's line and `_exit(128+N)` instead (D10).
32. **Targets that flaked once in the platform round** and passed when run again; none concerns the emulator a user
    runs: on the Mac's loopback `debugger.net.test_e2e` (all 16 MiB reached a peer that does not read, `test_e2e.cpp`
    line 306: now and then in MACa's and MACb's runs) and `debugger.contract.m2.guest.interp` (S16's reset not seen
    within 5000 ms, once in three full runs under `ctest -j4`; LA64), which `nulldc3/ci/macos.sh` runs once more when
    they are the only failures of a run, and says so; on the arm64 host `debugger.selftest.hostcycles` (its rate window
    on a busy host; A64); on the Windows host `debugger.selftest.emurun.pauses.interp` (its `SelfTest:` line spliced
    into a guest console line; A64).
33. **What A64's verifiers left** (`PLATFORM-ROUND.md` I.12): mutants of the arm64 recompiler that stayed green and were
    judged by reading (the room a compile keeps for `CheckBlock` and for a pending literal pool; five equivalents); the
    `-DNULLDC_DEBUGGER=OFF` build measured about 1.3 % slower on the arm64 host after A64 (`speed_pct` medians 663 to
    654; the default build and the Mac unchanged); `emu.fp` of `debugger.selftest.emu` is x86-64 only (it reads
    MXCSR), so on arm64 nothing holds the host FP mode across the park, while `dbg_sh4_Check` runs C++ code with the
    guest's FPCR (`sh4_host_fp_resync`). The store-queue write with the MMU off on the arm64 recompiler
    (`do_sqw_nommu_area_3`), which LA64's verifier found reached by no test, is held since A64 by
    `debugger.selftest.sqw` and `.sqw.interp` on every host.

### 2.4 Time while debugging

- The scheduler advances only in `UpdateSystem()` (one slice, 448 cycles); the debugger never advances it and never calls
  a scheduler callback itself.
- Every slice counter is persistent (R13, 2.3.4 d): leaving and re-entering `Run()` — stop and resume, a service
  excursion, a host pause at a slice end — is invisible to the guest. Measured on the merged code: 10 000 pauses at
  random host times, 100 000 `CC_READ` calls, 10 000 table / write calls and 10 000 breakpoint stops each leave the
  state hash of the run unchanged, on both engines.
- A code-breakpoint stop shows the time charged for the executed part of the block (R7): exact with a cost model that is
  additive over instructions; in `fast` timing the proportional share (an ordinary block shows nothing charged until it
  completes). The resume charges the rest, so stop + resume is neutral in every tier (R8).
- A step is charged as a block of that one instruction by the backend's own cost function (R12). With per-instruction
  cost models stepping and running charge the same; in the JIT's `fast` timing stepping n instructions of a block costs
  n cycles where the run costs the block cost. The interpreter's step charges its per-instruction `CPU_RATIO`, exactly as
  its run does (gate B13).
- Pause requests and service excursions are taken at a slice end of the backend loop, not inside a SLEEP wait (2.2.3),
  so with a persistent counter they are exact too.
- A debugger stop never moves emulated time: the latch freezes `UpdateSystem()`, and an interrupt acceptance is neither
  lost nor added (2.2.4 c).
- Documented skews, all of them in debugger stops only:
  1. a stop latched while the SH4 waits in SLEEP — an event or sound breakpoint, or a pause request or service
     excursion past the 250 ms liveness bound of 2.2.3: the SLEEP is re-executed after the resume (its block cost and
     its 1002-slice bound start again), and the loop that executed it re-executes it until the slice counter runs out
     before the park (under the x64 JIT up to 448 times, in the interpreter up to 55 times, charged to the slice counter
     without scheduler time);
  2. a dropped remainder — the PC was written, or a step, reset or state load was made during a stop inside a block: in
     `fast` timing up to one block cost;
  3. single steps on the JIT in `fast` timing (n cycles for n steps, above);
  4. a step entered with a slice counter that is not positive (a step taken from a step-out stop that fell behind the
     block that used the slice up): it executes its instruction first and does the end-of-slice work behind it, one
     instruction ahead of an undebugged run;
  5. on a backend that uses the fallback: the interpreter's FPU arithmetic while the fallback loop runs, and, behind a
     backend whose counter does not persist, less than one slice at every exit and re-entry of its loop (2.3.5).
- The sound side (M2): a stop at a breakpoint of the ARM7 is a freezing cause (2.2.3). Latched while the SH4 waits in
  SLEEP it is skew 1; anywhere else the stop and its resume, and a step of the ARM7, leave the sound lines of 5.6
  unchanged, which the neutrality targets of the M2 round hold on the sounding guest (`debugger.sound.neutral.*`, 5.6).
  An exit in the tick of such a stop is 2.3.7 item 20.
- The sound side (M3): a stop of the DSP (a breakpoint, a step) and the end of an SGC capture armed at a stop are sound
  stops, with the ARM7's skews: freezing causes recorded by the burst, latched behind its scheduler tick (skew 1 when
  that is inside a SLEEP wait). Their stops and resumes, the steps of the DSP and captures leave the sound lines and the
  state hashes unchanged, which the neutrality targets of the M3 round hold on the sounding guest
  (`debugger.sound.neutral.dsp.*` and `.sgc.*`: the tests `dspn.*` and `sgcn.*`, 5.6).
- Tracks 3/4 move the cycle counter into `Sh4Context` and change the cost model; what they must keep is 2.3.7 item 5.
- `dc.timing` (2.8) shows the tier, the counters, the backend and the debug engine.

### 2.5 Sound side (M2, M3): the resumable burst

Before M2 (`libswirl.cpp` `Dreamcast_impl::AicaUpdate`, `hw/arm7/SoundCPU.cpp`, `hw/aica/aica_mmio.cpp`): one scheduler
callback ran, if the ARM was enabled, 32 x { ARM7 for 256 ticks; `libAICA_TimeStep()` } — where `TimeStep` stepped the
timers, and in `NoBatch` mode rendered one sample (64 channels, DSP, mix, output), then updated the interrupt lines —
followed in batch mode by one 32-sample render with no DSP. With the ARM held in reset there was no `TimeStep` at all
(until W3 of the M3 round). M2 kept that sequence and made it resumable (2.5.2, as built by package E6b of
`M2-ROUND.md`); M3 cut the sample into its phases, made the T5.0 core the DSP and clocked the AICA whatever ARMRST says
(2.5.2, 2.5.4; `M3-ROUND.md`).

The batch mode and its settings went with the M3 round (W1 removed `Dynarec.DspEnabled` and the arm64 DSP recompiler,
W2 `aica.NoBatch` and the batch render): the AICA renders every sample with the DSP, in every tier. Since W3 of the M3
round the AICA is clocked whatever ARMRST says: a burst is 32 samples whether the ARM7 is held or not, and only the ARM
phase is skipped while it is held (the console's behaviour, measured by caique with the ARM7 held: `M3-ROUND.md`
3.12).

#### 2.5.1 Round-1 part (done, E4a)

The stop latch of 2.2.3 (rules 1 to 3, with the distinction between freezing and deferrable causes) and the leave order
of 2.2.4 are implemented and verified; the sound side depends on their exact semantics. Rule 4, 2.5.2 and 2.5.3 are
M2's (done: packages R6 and E6b of `M2-ROUND.md`); the M3 parts of 2.5.2 and 2.5.4 are M3's (done: packages R7, W1, W2,
W3 and E8a of `M3-ROUND.md`); 2.5.5 records forcing, which was never built.

#### 2.5.2 The burst machine (M2)

The machine is owned by `Dreamcast_impl` (a struct beside `AicaUpdate`). The loop over the 32 samples moves there from
`SoundCPU_impl::Update`, and `SoundCPU` gets the ARM phase of one sample as a call that can return unfinished
(`RunSample`). M2 latched `arm_on` at burst start and nothing else; M3 cut the sample into its phases (W1, E8a) and has
no `per_sample` latch: the batch path went with W2. The sequence in M2 (its history):

```
if (arm_on) for i in 0..31 { ARM(i); libAICA_TimeStep(); }
AICA::Update(32)
```

The last call was the batch render while it existed (until W2 of the M3 round) and is the tick of the sound regression
harness, once per burst on every path, also behind a completion in the park context. State in M2: `{suspended, i, ticks_done, arm_on}`, outside
`Arm7Context` (whose every word is in the save state and in the hashes). Suspension point: in front of an ARM7
instruction, and nowhere else. The machine counts the bursts the scheduler started, the ARM phases that ran to their
end, the time steps and the tails (`VirtualDreamcast::SoundBurstCounts`), in every build and outside every save state and
hash: the selftests count calls that emulator code makes, and emulator code includes no debugger header but
`debugger.h`.

The sequence and state of M3, as built (W1 cut the sample into its phases, W3 clocked them whatever ARMRST says, E8a
made the burst run them one by one, with the DSP phase, the frame of a capture and the end of a sample):

```
for i in 0..31 { if (arm_on) ARM(i); SampleTimers; SampleSweep; [SGC frame]; DSP(i); SampleMix; SampleIrq; [end of the sample] }
AICA::Update(32)
```

The per-sample loop runs also while the ARM7 is held, and only the ARM phase is skipped then (W3, `M3-ROUND.md` M3-D7).
The tail is the harness's tick, once per burst on every path. State: `{phase, i, ticks_done, arm_on, counts, dsp
counts}` (and the flag behind the DSP counts, whether the debug loop executed a step in the DSP phase of the sample),
outside every save state and hash; `arm_on` is latched at the start of the burst and decides the ARM phase alone. The
direct mix of the sweep is kept in the sound generator between `SampleSweep` and `SampleMix`, and the step the DSP
stands in front of is the core's. Suspension points: in front of an ARM7 instruction, in front of a DSP step, and at the
end of a sample that is not the burst's last (`SOUND_BURST_ARM7`, `SOUND_BURST_DSP`, `SOUND_BURST_SAMPLE` of
`VirtualDreamcast::SoundBurstSuspended`), in a burst whose ARM7 is held too (there is no ARM7 suspension in such a
burst); behind the last sample the burst ends with its tail, and a stop recorded in that sample is decided all the
same. `per_sample` and the batch render are gone. The DSP phases are counted apart
(`VirtualDreamcast::SoundBurstDspCounts`: the phases that ran to the end of their sample, and those in which the debug
loop executed a step), and `SoundBurstCounts`' time steps are 32 a burst, counted behind the interrupt update of each
sample.

- A breakpoint or a step of the ARM7 or of the DSP suspends the machine (a complete step of the DSP from step 127 and
  the end of an SGC capture armed at a stop do it at the end of their sample): the check records the stop
  (`emu::OnSoundStop`: a freezing cause, `ATTN_PAUSE`), the machine keeps where the burst is, and the callback returns
  `AICA_TICK` as usual; the slice hook of the same `UpdateSystem()` latches the stop — also inside a SLEEP wait and during
  an SH4 step — the latch freezes the scheduler, and (E) parks. The callback starts by verifying that no burst is
  suspended.
- **A suspended burst is always completed before the SH4 executes anything** (2.2.4 a). It is run on under the latch, by
  the park loop and by `dc_run` in front of the stop callback, never by the scheduler.
- Scheduler callbacks that share the tick of a suspended burst run before the burst completes; this reordering exists
  only in a debug stop and is not covered by the bit-identity gate in general; on the sounding guest, whose same-tick
  callbacks read no sound state, the neutrality targets hold it (the gate below). SH4 interrupt acceptance stays after
  the burst.
- Lifecycle: a host stop and a guest exit run the burst to its end without checks, in `dc_run` in front of
  `SaveRomFiles` and the stop callback (not in `OnThreadExiting()`, which is behind the callback that prints the state
  hash); `Dreamcast_impl::Reset()` discards it; when running a burst on for an SH4 resume or step hits a breakpoint of the
  ARM7 or of the DSP, or an SGC capture armed at the stop completes in it, the machine stays parked, the tick is `paused`
  with that stop (its `breakpointId`, or reason `capture`) and a new `stopId`, and the SH4 command is dropped. Save and load run only without an emulator thread, where no burst can be suspended: they need no code. Events
  raised while a suspended burst is run on in the park context are logged and do not stop (2.6).
- A host stop, a guest exit or a guest reset asked for in the tick in which a check suspends is met by `dc_run` with a
  burst suspended and no park in between: the first two run it to its end, and nothing is published; a reset discards it
  and the stop published is the reset's.
- Gate: with no sound breakpoint armed the sequence of calls is exactly today's, and the hashes of 5.6 are identical
  before and after the change; and, on the sounding guest, with an active breakpoint that is never reached, with
  breakpoints hit and resumed, and with steps (5.6). In M3 the same holds for the DSP and the capture: the debug loop
  forced in every sample, DSP breakpoints hit and resumed, DSP steps of every kind, host stops in the DSP phase and at
  the end of a sample, and captures while running and at stops leave every line and hash as they were (the neutrality
  targets `dspn.*` and `sgcn.*`, 5.6).

#### 2.5.3 ARM7 breakpoints and steps (M2)

- `ARM7Backend` has two halves of `Step`: `StepIrq(ctx)` (the pending-FIQ entry) and `StepExec(ctx)` (the rest; it
  returns its ticks); `StepIrq(ctx); StepExec(ctx);` is `Step(ctx)`. `Step` and `StepMany` keep their own body and
  call neither half (E6b took the first shape of `M2-ROUND.md` 2.3, M2-D60), so nothing is added per instruction:
  `StepExec` is a third inclusion of the interpreter's body (`gpl/vba-arm/arm-new.h`), and an edit of `Step` does not
  reach the debug loop. The halves are run by the debug loop and by the ARM7 suites under `ARM7M_STEP=halves`
  (`arm7.*.halves`); the plain suites run `Step`.
- The ARM phase of a sample is `if (likely(!(checks && dbg_arm7_armed))) arm->Run(256); else` the debug loop — one flag
  test per sample, nothing per instruction (`SoundCPU::RunSample`; the flag `dbg_arm7_armed` and the check
  `dbg_arm7_Check` are in `debugger.h`). The debug loop:
  `loop { StepIrq; if (dbg_arm7_armed && dbg_arm7_Check(regs[R15_ARM_NEXT])) suspend; ticks += StepExec; if (ticks >= 256) break; }`,
  calling the interpreter's functions directly (no backend swap). The test is after `StepIrq`, so a breakpoint on the FIQ
  vector `0x1c` is hit and a hit at X is not followed by a silent detour through the handler. The debug loop looks at the
  pending FIQ in front of every instruction, as `Step` does, and so does the loop that goes on with a suspended sample: a
  request can become enterable inside a sample (an `MSR` that clears F with a request waiting). Breakpoint addresses are
  compared under `dbg::arm7::MaskAddr`: `& aram_mask` with bits 0 and 1 dropped. The resume exemption (M2-D13 of
  `M2-ROUND.md`) is armed at a resume only behind a stop that the ARM7 check decided, with the PC of that stop, the ARM7
  clocked and a check to come; it is used only by a check at that PC; it is ended by the first check, by a change of
  `dbg_arm7_armed` to or from 0 and by a burst that finds the ARM7 held; a step-over on a `BL` and a step-out arm it for
  the PC they are asked from.
- On hosts where the ARM7 runs through the interpreter (every x86-64 build, and every build since the platform round)
  this is native: what is debugged is what runs. Where the selected ARM7 backend is the recompiler (arm64 with it
  enabled; no build has one since the platform round, `PLATFORM-ROUND.md` PL-D46) the debug loop is an interpreter
  fallback on the same context (all JIT state lives in `ctx->regs` between `Run` calls, so no backend swap is needed):
  timing differs while the debug loop is in use: while a breakpoint of the ARM7 is **active** (enabled, its category in
  the active mask), a temporary breakpoint exists or a step is armed. Ticks then carry the `notice` `"ARM7 runs on the
  interpreter fallback: timing differs"` (`NOTICE_ARM7_FALLBACK`); the notice is also in the stop that ends a step, a
  resume or a run in which the debug loop executed, since a step and a temporary breakpoint disarm before their stop is
  published. The ARM7 tiers of PLAN track 6 replace the cost tables on both paths; what they keep is the shape above
  (one flag test per sample, the check after `StepIrq`).
- `control.step {arm7}` executes one ARM7 instruction and never publishes `running`. With a burst suspended: in the park
  context, the stop at the check behind the instruction (the next instruction, a branch target, a vector); if the
  instruction was the last of the burst, at the burst's end with none suspended. With no burst suspended: the park
  context is left without a publication, the SH4 runs to the next burst (at most one `AICA_TICK`), and the step is the
  first instruction of that burst; it is complete with nothing executed when a FIQ is entered in front of the instruction
  (the stop is at the vector) or when that burst finds the ARM7 held in reset; a stop of the SH4 that comes first is
  published with its own cause. The step executes its instruction also with a listed breakpoint on it. `stepOver`: the
  temporary breakpoint at `PC + 4` after a `BL` and a resume, else a step. `stepOut`: the temporary breakpoint at `LR`
  and a resume (heuristic, documented in the UI: correct for leaf code and before `LR` is saved or overwritten; inside
  the FIQ handler see 2.3.7 item 21). Both execute the instruction they are asked from, also with a listed breakpoint on
  it. One temporary breakpoint exists at a time; it goes at every published stop. A host stop, a guest exit or a guest
  reset during the wait of a step ends the step unpublished (a reset publishes its own stop).
- With the ARM held in reset (`SoundCPU::DebugEnabled()` false) the ARM7 is not clocked — the rest of the AICA is
  (2.5.2, since W3 of the M3 round) —: `control.step / stepOver / stepOut {arm7}` return the in-band error `-32012` (the emulator's text: `arm7 held in reset`), and the ARM7 callstack is empty
  (2.10). A breakpoint on `dc.aica.arm7.pc` can be added, toggled and removed in every state: it is what stops at the
  ARM7's first instruction. What a client can see of the reset is the register `ENABLED` of `dc.aica.arm7`, which shows
  that same flag: 1 = the ARM7 is clocked, 0 = it is held in reset (2.8, 2.10). The guest-visible register ARMRST is not
  that observable (2.7).

#### 2.5.4 DSP steps and breakpoints (M3, on the T5.0 core)

As built by the M3 round (`M3-ROUND.md`: W1 wired the core, W2 removed the batch path, W3 clocked the AICA whatever
ARMRST says, E8a built the DSP's run control; header revision 7 has the declarations, `debugger.h`'s DSP block and
`dbg_dsp.h`):

- **One DSP.** The DSP is the caique-matching C++ core of PLAN T5.0, `libswirl/hw/aica/dsp_core` (`AicaDsp`): per-step
  execution with persistent state by construction. `StepOne()` executes the step `AicaDsp::step` names and advances it;
  `RunSample()` executes the steps from there to the end of the sample. The step index lives in the core; the debugger
  adds no field to any serialised structure (the save state's DSP part is the core's blob, V5 since W1). There is one
  implementation: the arm64 DSP recompiler was deleted, not ported (`M3-ROUND.md` M3-D14), and so was the old
  interpreter; what the DSP computes does not depend on the host, nor on whether anything of the debugger is armed, and
  no notice is given for the DSP. A step can report what it computed (`AicaDspStepView`: operands, `ACC`, the read
  latch, the wave-RAM access); the step view is not on the wire (1.1).
- **The DSP phase** of a sample is `DSP::RunPhase(checks)`, behind the sound generator's sweep and in front of its mix
  (2.5.2): `RunSample()` while `checks` is false or `dbg_dsp_armed` is 0, and the debug loop while it is not — per step
  `dbg_dsp_Check(step)`, then `StepOne()` —, which executes the same steps. A phase suspended in front of a step is gone
  on with by either. `dbg_dsp_armed` is tested once a sample; it follows activity: an enabled listed breakpoint of the
  DSP arms the debug loop only while the category `dsp` is active, and a step of the DSP arms it (M3-D26).
- **The sweep's sends** (M3-D8): `SampleSweep` calls `SweepBegin()`, then for each bus that a channel's ISEL names one
  `MixsSend` of the sum of the sends `oDsp << 4` (the 20-bit scale of the console: MIXS holds the sample times 16), then
  the sample is `RunSample()` without inputs held across a suspension; the CD-DA pair goes into `EXTS` (0 with no disc),
  and `SampleMix` takes `EFREG` into the mix through EFSDL / EFPAN. `ChannelFetch` and `BusRead` are not called in the
  `fast` tier (M3-D10): a DSP access in a playing channel's slot is not lost and the read latch holds only the DSP's own
  reads (2.3.7); the `cycle-similar` AICA of PLAN track 5 calls them.
- **The CPUs' accesses to the DSP's registers** (M3-D5): a write stores its bytes in the AICA register file, then the
  core gets the file's 16-bit word of the slot (low halves of the slots it owns; a byte write merges with the file's
  other byte); a read of an owned slot is the core's (`RegRead`: COEF bits 2:0 read 0, TEMP and MEMS live, MIXS of the
  DSP's bank, EFREG as written or computed, 0x2804 and EXTS 0, the upper half of a slot 0). The DSP never writes the
  file, which stays what the CPUs wrote (the sound line's `regs`).
- **A DSP breakpoint** (`dc.aica.dsp.step`, address 0..127; `emu::SetDspBreakpoints`) is checked in front of the step it
  is on, and **an active DSP breakpoint is reached in every sample**. **The resume exemption** (M3-D22) is decided at
  every leave of the park context that can let the DSP execute — a resume, a step of the SH4, a step, step-over or
  step-out of the ARM7 —, armed only behind a stop the DSP decided and with the DSP still in front of that step, and
  used by the first check only: a resume from a DSP breakpoint runs one sample and stops in front of the same step one
  sample later; an SH4 step from such a stop is dropped when the run-on meets the breakpoint again (2.5.2); the UI's
  help says to disable or mute the breakpoint to let the SH4 run on (F4d).
- **`control.step {dsp}`** (M3-D20) executes one DSP step and never publishes `running`. With a burst suspended the
  park loop runs it on in the park context: suspended in front of a DSP step, that step executes and the check behind it
  stops; suspended in the ARM phase of a sample or at the end of the sample in front of it, the burst goes through the
  rest of that ARM phase (with the ARM7's checks: a stop of the ARM7 that comes first is published with its own cause)
  to its DSP phase, whose first step is the step. From step 127 the sample's mix and interrupt update run and the step is
  complete at the end of the sample, `STEP` 0 (`SOUND_BURST_SAMPLE`, or, behind the burst's last sample, the burst ends
  with none suspended). With no burst suspended the SH4 runs to the next burst, one AICA period at most, and the step is
  step 0 of its first sample; a stop of the SH4 that comes first is published with its own cause. The step executes its
  step also with a listed breakpoint on it. `stepOver` / `stepOut` on the DSP answer `-32004` (`not supported: step
  over and step out of target dsp`).
- **The DSP is clocked whatever ARMRST says** (M3-D7): every step executes in every sample, also while the guest holds
  the ARM7 in reset, so the DSP's steps and breakpoints work then, and the DSP never answers `-32012` (only the ARM7's
  steps do, 2.5.3). There is no DSP callstack (2.10).
- **`dc.aica.dsp`** shows the thirteen registers of M3-D25 (2.8), read-only: `STEP` (the core's `step`), `MDEC_CT` (the
  core's 16-bit counter, free running, once a sample), `RBP` and `RBL` from the DSP's `RBPL` (the word the CPU wrote to
  0x2804 last; a CPU read of 0x2804 gives 0), and the datapath of `DspDebugState`: `ACC`, `Y_REG`, `FRC_REG`,
  `ADRS_REG`, `LATCH`, `LATCH_COPY`, `MEMVAL`, `NOFL_PIPE`, `MIXS_BANK`. They are what the playground's "Load from
  emulator" needs to continue the DSP exactly from a stop inside a sample's DSP phase (4.5).
- **The playground** runs the same C++ core, built with emscripten (`dsp_core/wasm/`), in place of the Rust
  `crates/aica-dsp` module: the module is committed in the UI with its provenance, its inputs are at the console's scale
  (`sample << 4`), and the emulator replays the native trace on it (`aica.dsp.wasm.devtools`; M3-D34, 4.5), so the
  playground, the debugger's DSP view and the emulator execute identical DSP semantics.

#### 2.5.5 Per-sample forcing (never built)

Forcing was never built. Header revision 7 removed `NOTICE_DSP_FORCED` and `EV_DEBUGGER_DSP_FORCED`, package F4a the
capability `dsp-forced`.

### 2.6 Events

```cpp
extern std::atomic<u8> dbg::g_event_on[EV_ID_COUNT];
#define DBG_EVENT(id, ...) do { if (unlikely(dbg::g_event_on[id].load(std::memory_order_relaxed))) \
                                    dbg::EventSlow(id, __VA_ARGS__); } while (0)
void dbg::EventSlow(EventId id, const char* fmt, ...);     // printf-style; the format is required
```

- Callable from any thread, in any lifetime state, with or without L (device reset / unserialize code runs on (U),
  rendering on (U) and the refsw worker pool); not from a signal handler. With the gate closed the cost is one byte
  load and the arguments are not evaluated, so they must have no side effect. The ring is guarded by a small mutex of
  its own, held for the append only: `EventSlow` never takes L, never waits for (E) and may be called with any emulator
  lock held.
- `g_event_on[id]` is a byte per event, maintained by the core: 1 iff at least one client is connected and (the event
  is low-rate, or an event breakpoint on it exists — enabled or not). **"A client is connected" is the core's own
  count of open clients**, kept on (C) from the `OnOpen` / `OnClose` messages of its `net::Handler` (and stated by the
  test in a session without a server, 5.3); the gates, the active mask (2.3.3), the periodic tick and the grace
  period (2.9) all follow that one count, and `net::Server::HasClients()` is not consulted for any of them (the flag
  of the transport can be ahead of the messages the core has taken). The
  core rewrites the gates when a client connects or disconnects and when the event breakpoints change. For a
  connect and a disconnect that is a `CC_TABLE` call of its own, which hands over the gates and the active mask for
  the count as it is now: a service excursion while the machine runs (2.2.2). A call that (E) did not take in time
  is made again 250 ms later (2.9). A selftest
  session has no client: a guest test that sets an event breakpoint opens the gate of its event itself, and closes it
  again. **One entry is made with the gates closed:** when the grace period expires (2.9) the core appends
  `dc.debugger.note` to the ring itself, not through `DBG_EVENT` — no client is connected at that moment, so the gate
  of the event is 0 and the macro would evaluate nothing. The append is made inside the table call that clears the
  active mask, not on (C): `metadata.cycles` is the scheduler's time, and that is read where guest state may be
  read. It takes the ring's mutex like any other and tests no event breakpoint; the next client finds the entry in
  its first tick. Its text is the same at every expiry and does not say whether a stop was in force.
- **The protocol note.** A `debugger.describe` whose `protocol` is another number than the server's is logged as
  `dc.debugger.note`, through `DBG_EVENT` (a client is connected, the event is low-rate). It is logged for every
  such describe, not once per connection: a client that repeats the describe adds an entry each time.
- Event breakpoints are evaluated only on (E) while the guest executes (`Running`, or a step): `EventSlow` calls
  `dbg::emu::OnEvent(id)`, which, when an enabled event breakpoint on the event exists and the `events` category is
  active, records a freezing cause; the slice hook latches the stop in the `UpdateSystem()` call that is running, or in
  the next one. Events raised on other threads, or on (E) in the park context, never stop: among them the events of the
  AICA and of the ARM7 that a suspended sound burst raises while the park loop runs it on (2.5.2) — also of a burst
  suspended in front of a DSP step or at the end of a sample (M3). They are logged.
- The ring holds 200 entries (`kEventRingSize`); every tick carries the whole ring, oldest first. A formatted message is
  cut to `kEventMessageMax` (256) bytes.
- Wire fields: `eventId` is a decimal sequence number (as a string) of the process: within one log the entries are
  oldest first and their `eventId`s ascend, and the same `eventId` is the same entry in every tick of a process;
  `timestamp` is wall-clock milliseconds since the Unix epoch, like `tick.timestamp`; `subsystem` and `severity` from the
  table; `message` is the formatted text; `metadata` is
  `{ "event": "<event name>", "cycles": "<sh4_sched_now64(), decimal>" }` (`cycles` is `"0"` while no machine exists).
- Events are divided into **low-rate** (`EVR_LOW`: logged whenever a client is connected, and the grace note, above)
  and **high-rate** (`EVR_HIGH`: can fire at frame rate or faster; logged only while a client is connected and an event
  breakpoint on that event exists, enabled or not). A server lists in the
  `events` of its device tree exactly the events it can raise (2.8), and accepts a breakpoint on every event it lists.
  Event ids are the rows of `dbg_events.h` (`DBG_EVENT_LIST`); they never leave the process, the wire carries the names.
- **`dc.holly.core.vblank` is raised by every running machine** — the video timing always runs, whatever the program
  does — and is in the M1 tree. The contract relies on it (5.5: an event that is certain to come).

| Constant | Event name | Subsystem | Rate | Node | Raised in |
| --- | --- | --- | --- | --- | --- |
| `EV_SH4_INTERRUPT` | `dc.sh4.interrupt` | `sh4` | high | `dc.sh4` | `hw/sh4/sh4_interrupts.cpp` `Do_Interrupt` |
| `EV_SH4_EXCEPTION` | `dc.sh4.exception` | `sh4` | low | `dc.sh4` | `Do_Exception` |
| `EV_DMAC_START`, `EV_DMAC_END` | `dc.holly.dmac.transfer_start`, `.transfer_end` | `holly` | high | `dc.holly.dmac` | ch2 and sort DMA handlers (`hw/pvr/pvr_sb_regs.cpp`) |
| `EV_TA_LIST_INIT`, `EV_TA_LIST_END` | `dc.holly.ta.list_init`, `.list_end` | `ta` | high | `dc.holly.ta` | `hw/pvr/pvr_sb_regs.cpp`; the list end in `hw/pvr/ta.cpp` (TA_HLE) and in `hw/holly/holly_intc.cpp` `RaiseAsicNormal` (TA_LLE, the default build) |
| `EV_CORE_RENDER_START`, `EV_CORE_RENDER_END` | `dc.holly.core.render_start`, `.render_end` | `core` | high | `dc.holly.core` | `hw/pvr/pvr_sb_regs.cpp`, `hw/pvr/spg.cpp` |
| `EV_CORE_VBLANK` | `dc.holly.core.vblank` | `core` | high | `dc.holly.core` | `hw/pvr/spg.cpp` |
| `EV_AICA_INTERRUPT` | `dc.aica.interrupt` | `aica` | high | `dc.aica` | `hw/aica/aica_mmio.cpp` `UpdateSh4Ints` (to the SH4), `hw/arm7/SoundCPU.cpp` `update_e68k` (to the ARM7) |
| `EV_AICA_TIMER` | `dc.aica.timer` | `aica` | high | `dc.aica` | `hw/aica/aica.h` `AicaTimer::StepTimer` |
| `EV_AICA_KEY_ON`, `EV_AICA_KEY_OFF` | `dc.aica.channels.key_on`, `.key_off` | `aica` | low | `dc.aica.sgc` | `hw/aica/sgc_if.cpp` |
| `EV_AICA_LOOP` | `dc.aica.channels.loop` | `aica` | high | `dc.aica.sgc` | `hw/aica/sgc_if.cpp` |
| `EV_GDROM_COMMAND` | `dc.gdrom.command` | `gdrom` | low | `dc.gdrom` | `hw/gdrom/gdromv3.cpp` |
| `EV_MAPLE_DMA` | `dc.maple.dma` | `maple` | high | `dc.maple` | `hw/maple/maple_if.cpp` |
| `EV_SYSTEM_RESET`, `EV_SYSTEM_STATE` | `dc.system.reset`, `dc.system.state` (machine created / destroyed, thread started / ended, state saved / loaded) | `system` | low | `dc` | `libswirl.cpp` |
| `EV_DEBUGGER_ENGINE` | `dc.debugger.engine` (a switch to or from the fallback loop of the SH4; the ARM7 starts or stops running on its interpreter fallback, M2) | `debugger` | low | `dc` | `dbg_emu.cpp` |
| `EV_DEBUGGER_NOTE` | `dc.debugger.note` (the grace period expired — the active mask was cleared and a stop, if one was in force, released; a client described itself with another protocol number) | `debugger` | low | `dc` | `dbg_core.cpp`: the grace note by a direct append to the ring (no client is connected, above), the other through `DBG_EVENT` |

Severity is `info` for all of them except `dc.sh4.exception` (`warn`). No event is logged for a code-breakpoint hit: the
stop itself reports it. Every row is listed by exactly one node of the tree: header revision 7 removed the one that was
not, `EV_DEBUGGER_DSP_FORCED` (`dc.debugger.dsp_forced`, 2.5.5).

### 2.7 Guest memory for the debugger (`dbg_mem`)

Byte-granular, any address, any length; never calls a guest memory handler, so a read changes nothing and cannot spin or
end the process. Read runs on (E) or under L (2.2), write on (E) only. Backing stores are re-read from the machine on
every call (no cached pointers). There is no address translation: `WITH_MMU` builds use the same fixed map (1.1).

Target `sh4`. An address below `0xE0000000` is first reduced to its low 29 bits.

| Region (after the reduction) | `Region` | Backing |
| --- | --- | --- |
| area 0, `0x00000000-0x03FFFFFF`, reduced again to its low 25 bits (`0x02000000` mirrors `0x00000000`) | | |
| `0x00000000-0x001FFFFF` | `Bios` | boot ROM, through `area0_DebugRom` (`hw/holly/sh4_mem_area0.h`; the objects are file globals whose types depend on the platform) |
| `0x00200000-0x0021FFFF` | `Flash` | flash memory as a guest read delivers it, through `area0_DebugNvmemByte`: the stored byte, with the configured region, language and broadcast substituted at the six settings offsets (`0x1A002-0x1A004`, `0x1A0A2-0x1A0A4`) |
| `0x005F6800-0x005F7CFF`, **except** `0x005F7000-0x005F70FF` | `SbRegs` | system-bus registers through `dbg_regs` safe readers |
| `0x005F7000-0x005F70FF` | `Unreadable` | GD-ROM: its reads have side effects; the GD-ROM state is a device snapshot in the device tree (M2) |
| `0x005F8000-0x005F9FFF` | `PvrRegs` | `pvr_regs` array |
| `0x00600000-0x006007FF` | `Unreadable` | modem |
| `0x00700000-0x00707FFF` | `AicaRegs` | AICA registers as a guest read delivers them (`AICA::DebugReadRegs`, not `ReadCommonReg`, which rewrites live fields); the DSP's registers (0x2804, 0x3000..0x45C7) as the DSP core answers a CPU read (below) |
| `0x00710000-0x0071000B` | `AicaRtc` | RTC (`libAICA_DebugGetRTC`) |
| `0x00800000-0x00FFFFFF` | `AicaRam` | wave memory, mirrored by its size (per platform, read from the machine) |
| `0x04000000-0x04FFFFFF`, `0x06000000-0x06FFFFFF` | `Vram64` | video memory, 64-bit view, mirrored by its size |
| `0x05000000-0x05FFFFFF`, `0x07000000-0x07FFFFFF` | `Vram32` | video memory, 32-bit view: `vram[pvr_map32(addr)]` per byte (byte lanes kept) |
| `0x0C000000-0x0FFFFFFF` | `Ram` | main memory, mirrored by its size |
| `0x1F000000-0x1FFFFFFF` | `Sh4Regs` | on-chip registers at their area 7 addresses, through `dbg_regs` safe readers |
| `0xE0000000-0xE3FFFFFF` | `StoreQueue` | the two store queues, 64 bytes, mirrored |
| `0xFF000000-0xFFFFFFFF` | `Sh4Regs` | on-chip registers at their P4 addresses |
| anything else | `Unreadable` | |

A register region delivers the bytes of the register values in little-endian order; a byte that belongs to no register,
or to one without a side-effect-free reader, is not readable.

**ARMRST.** The byte at `0x00702C00` of the AICA register window (`0xA0702C00` as the SH4 addresses it) is the AICA
register ARMRST, and the SH4 view of it goes through the register file like the rest of the window:
`AICA::DebugReadRegs` copies the file and puts ARMRST, and VREG behind it, at their offsets, as a read by the SH4
delivers them. **ARMRST is not the debugger's observable for "the ARM7 is held in reset".** As merged, a machine reset
leaves ARMRST reading 0 while the ARM7 is not clocked, until the guest first writes the register: `AicaDevice::Reset`
releases the reset, and `SoundCPU_impl::Reset`, which runs behind it, then disables the core. The byte is what the
guest reads and no more. The observable is the register `ENABLED` of `dc.aica.arm7` (2.8, 2.10), and the reset order
itself is a known limit of the tree (2.3.7 item 13).

**The DSP's registers** (M3, `M3-ROUND.md` M3-D5, M3-D6): 0x2804 and 0x3000..0x45C7 of the window are read as the DSP
core answers a CPU read (`AicaDsp::RegRead`): COEF with bits 2:0 0, TEMP, MEMS, MIXS (of the DSP's bank) and EFREG live,
0x2804 and EXTS 0, the upper half of a slot 0. The register file itself holds what the CPUs wrote: the DSP never writes
it. The bits of 0x2804 the DSP uses are its `RBPL`, which the rows `RBP` / `RBL` of `dc.aica.dsp` and `RBP` / `RBL` /
`TESTB0` of `dc.aica.common` show (2.8).

Target `arm7`: wave memory at `0x00000000-0x007FFFFF` (mirrored by its size), the AICA registers at
`0x00800000-0x00807FFF`, anything else not readable. The ARM7 sees the register file as the SH4 does, with three
differences: `0x00802D00` is the interrupt level register L as a read by the ARM7 delivers it, without the acknowledge
that goes with that read (`SoundCPU::DebugGetInterrupt`); `0x00802D04` reads 0 (what a read of M by the ARM7 delivers;
the in-service flag is for a register row, not for memory); and the two bytes `0x00802C00` / `0x00802C01`, where the SH4
has ARMRST and VREG, are the bytes of the register file behind them, read through the AICA's register read function,
which is a plain copy at those two offsets (the one place where `dbg_mem` goes through a guest read function, and it has
no side effect there). Target `dsp` has no memory: every address is `Unreadable` (`-32004` on the wire).

```cpp
namespace dbg { namespace mem {
Region Classify(Target t, u32 addr);                                        // by the map above; any thread
u32    Read (Target t, u32 addr, u32 len, u8* out, u8* readableBits);       // returns the readable byte count; unreadable bytes read 0
int    Write(Target t, u32 addr, u32 len, const u8* data);                  // 0 or -32013
bool   Read16(Target t, u32 addr, u16* value);  bool Read32(Target t, u32 addr, u32* value);   // false: a byte is not readable
}}
```

Reads (3.3): `state.getMemorySlice` always returns `length` bytes; unreadable bytes are 0 and are listed in `unreadable`;
the JSON-RPC error `-32010` is returned only when nothing in the range is readable. **A slice does not wrap:** a byte
whose address would lie above the target's `addressMax` (3.1) is unreadable — 0 in `data`, listed in `unreadable` —,
as on the mock: in the answer to `{target: "sh4", address: 0xFFFFFFF0, length: 32}` the bytes 16 to 31 are never the
first bytes of the boot ROM. On the mock they are unreadable and the first sixteen are data. On the emulator the
answer to that request is `-32010`: no on-chip register lies in `0xFFFFFFF0-0xFFFFFFFF`, so nothing in the range is
readable (the selftest `core.tick` pins the rule that way). The core cuts the length it hands to `dbg::mem::Read` at
`addressMax` and fills the rest itself; the function itself wraps at 2^32 (`dbg_mem.h`), and that is not what a
client gets. `state.getMemoryBinary` (M3) follows the same rule. `state.getDisassembly` returns `count` lines, fewer
only at `addressMax` (it does not wrap either, 3.3); an unreadable word gives `bytes: "??"` and
`disassembly: "<unreadable>"`.

Writes (`state.writeMemory`, M2; `CC_WRITE`, so always on (E) in the park context): main RAM, VRAM (both views) and AICA
RAM (both targets) only; anything else is refused with `-32013`, and the range is checked as a whole before the first
byte is stored. A range that would pass the end of the address space is refused too: a write does not wrap.
`target: "dsp"` is the in-band `-32004`.
- Main RAM: for every touched page of `REI_PAGE_SIZE` (4 KiB; 16 KiB on macOS) call
  `bm_LockedWrite(&sh4_cpu->mram.data[page_offset])` before storing (it discards all blocks on the page, marks it as data
  and unprotects it), in builds with an SH4 recompiler. Without the virtual memory layout (`virt_ram_base == 0`: a host
  without the large mapping) the writer discards through `DebugDiscardPage` (2.3.4 l), removes the protection of the
  pages itself (`bm_AddBlock` protects the pages of a block with or without the layout) and stores the bytes; nothing
  in it needs the mapping (D7), and no host is left without RAM writes (2.3.7 item 15 has what that build does with a
  guest store into such a page). With the layout in use a store without the discard is made good in the default
  configuration — by the fault handler on a page its blocks protect, by the block's own check
  (`Dynarec.SmcCheckLevel`, `FullCheck` by default) on a data page; the test of the discard therefore runs with
  `Dynarec.SmcCheckLevel=-1`. A step, and a resume behind a breakpoint stop, decode what they run from memory: a witness
  of the block cache is taken from code the dispatcher enters from the cache. (The comment of `Write()` in the frozen `dbg_mem.h`
  had v2's rule — such a build refuses the write — and `sh4_if.h` named arming as the only caller of
  `DebugDiscardPage`; header revision 4 brought both to this paragraph: `DESIGN-V3-OPEN.md` H20.) If the page holds
  SH4 breakpoints, nothing else is needed: the decoder asks again at every decode, and an owed remainder is decoded
  from the memory as it is at the resume (2.3.4 g, h).
- VRAM pages can be locked by the texture cache: call `VramLockedWrite(vram.data, ptr)` first.
- A write that touches AICA RAM (either target) calls `SoundCPU::InvalidateJitCache()` (the ARM7 JIT has no
  self-modifying-code detection).
- Never rely on the SIGSEGV path from a thread other than (E). (`cycle-similar` needs no page protection for
  self-modifying code at all, PLAN T3.3b; this section describes `fast`, and the write paths of the cache model are
  track 3's.)

### 2.8 Device tree and watches

Structure and register names come only from `debugger.describe`; ticks update values. Paths are lower case and opaque to
the client; register names are upper case and unique within a node. The tree is a flat list of nodes in the emulator
(`dbg::regs::Nodes`), each naming the node it is shown under; `debugger.describe` sends it nested.

**What is normative** (for every server that claims the milestone; the contract checks it, 5.5 S01):
- the paths of the M1 rows below;
- their nesting, as the "parent" of each row gives it — not by the dots of the path (`dc.gdrom`, `dc.maple` and
  `dc.timing` are under `dc.system`; the SH4 modules under `dc.sh4.pbus`);
- the `actions` of each row;
- the anchor registers: `dc.sh4.cpu` `PC`, `dc.aica.arm7` `PC`, `dc.aica.dsp` `STEP`, and, for a server that lists
  the target `arm7`, `dc.aica.arm7` `ENABLED` with width 1 (1 = the ARM7 is clocked, 0 = it is held in reset; 2.10);
- the register list of `dc.sh4.cpu`;
- the widths of the thirteen registers of `dc.aica.dsp` (`M3-ROUND.md` M3-D25), for every server that lists one of
  them: `STEP` 8, `MDEC_CT` 16, `RBP` 16, `RBL` 8, `ACC` 26, `Y_REG` 24, `FRC_REG` 13, `ADRS_REG` 12, `LATCH` 16,
  `LATCH_COPY` 16, `MEMVAL` 16, `NOFL_PIPE` 2, `MIXS_BANK` 1 (S01 checks the width of each one it finds; `STEP`, the
  anchor, has to be there, and under the M3 gate all nine of M3, `ACC` to `MIXS_BANK`, are required);
- for events: a server lists in `events` exactly the events it can raise, and accepts a breakpoint on every event it
  lists. `dc.holly.core.vblank` is listed by every server (2.6);
- for a server that offers the action `sh4-registers` (M2): the paths and parents of the M2 rows, the two actions
  `sh4-registers` and `arm7-registers`, and at least one register on each of `dc.holly.dmac`,
  `dc.holly.sb.{g1,g2,pvr,misc}`, `dc.holly.ta`, `dc.holly.core`, `dc.holly.spg`, `dc.aica.common`, `dc.aica.rtc`,
  `dc.aica.sgc.0` .. `.63`, `dc.gdrom` and `dc.maple` (the column R of `M2-ROUND.md` 3.6.1; not `dc.holly.sb`, which
  has none; S16, part `tree`).

Every other register list is the server's: the emulator's lists are below, the mock's can differ, and an M1 tree may
hold fewer than 256 registers. Labels and descriptions are the server's too. In the emulator the second-level nodes
(`dc.sh4`, `dc.holly`, `dc.aica`, `dc.system`) carry no registers, because the panel expands the first two levels by
default; that is the emulator's choice, not contract, and the runner does not check it.

| Path (parent) | Registers (emulator) | Events | Actions | From |
| --- | --- | --- | --- | --- |
| `dc` | `SYSCLK` (text), `BUILD` (text, `nullDC <version>`, the build's version string of 3.1) | `dc.system.reset`, `dc.system.state`, `dc.debugger.engine`, `dc.debugger.note` | — | M1 |
| `dc.sh4` (`dc`) | — | `dc.sh4.interrupt`, `dc.sh4.exception` | — | M1 |
| `dc.sh4.cpu` (`dc.sh4`) | **normative:** `PC PR SR GBR VBR MACH MACL SSR SPC SGR DBR FPSCR FPUL`, `R0..R15`, `R0_BANK..R7_BANK` (the bank of R0..R7 that is not selected), `FR0..FR15`, `XF0..XF15` (raw 32 bits); all width 32, in this order | — | `sh4-disassembly`, `sh4-memory`; `sh4-registers` from M2 | M1 |
| `dc.sh4.pbus` (`dc.sh4`) | — | — | — | M1 |
| `dc.sh4.{bsc,ccn,cpg,dmac,intc,rtc,sci,scif,tmu,ubc}` (`dc.sh4.pbus`) | every on-chip register of the module (table checked against the registered slots of the live machine by the selftest `regs`) | — | `<name>-registers` | M1 |
| `dc.sh4.{sq,icache,ocache,ocram,tlb}` (`dc.sh4.pbus`) | — | — | `<name>-contents` (panels filled in M4) | M1 |
| `dc.holly` (`dc`) | — | — | — | M1 |
| `dc.holly.intc` (`dc.holly`) | `SB_ISTNRM SB_ISTEXT SB_ISTERR`, `SB_IML{2,4,6}{NRM,EXT,ERR}` | — | — | M1 |
| `dc.holly.dmac` (`dc.holly`) | read-only: ch2 `SB_C2DSTAT SB_C2DLEN SB_C2DST`; sort `SB_SDSTAW SB_SDBAAW SB_SDWLT SB_SDLAS SB_SDST SB_SDDIV`; G1 `SB_GDSTAR SB_GDLEN SB_GDDIR SB_GDEN SB_GDST SB_GDSTARD SB_GDLEND`; G2, for each of `AD E1 E2 DD`: `STAG STAR LEN DIR TSEL EN ST SUSP STAGD STARD LEND` (60 rows) | `dc.holly.dmac.transfer_start`, `.transfer_end` | — | node M1, registers M2 |
| `dc.holly.sb` (`dc.holly`) | — | — | — | M2 |
| `dc.holly.sb.g1` (`dc.holly.sb`) | read-only: `SB_G1RRC SB_G1RWC SB_G1FRC SB_G1FWC SB_G1CRC SB_G1CWC SB_G1GDRC SB_G1GDWC SB_G1SYSM SB_G1CRDYC SB_GDAPRO` (ten of them write-only: `"n/a"`) | — | — | M2 |
| `dc.holly.sb.g2` (`dc.holly.sb`) | read-only: `SB_G2ID SB_G2DSTO SB_G2TRTO SB_G2MDMTO SB_G2MDMW SB_G2APRO` | — | — | M2 |
| `dc.holly.sb.pvr` (`dc.holly.sb`) | read-only: the block at `0x005F7C00`, `SB_PDSTAP SB_PDSTAR SB_PDLEN SB_PDDIR SB_PDTSEL SB_PDEN SB_PDST SB_PDAPRO SB_PDSTAPD SB_PDSTARD SB_PDLEND` | — | — | M2 |
| `dc.holly.sb.misc` (`dc.holly.sb`) | read-only: `SB_DBREQM SB_BAVLWC SB_C2DPRYC SB_C2DMAXL SB_TFREM SB_LMMODE0 SB_LMMODE1 SB_FFST SB_SFRES SB_SBREV SB_RBSPLT SB_PDTNRM SB_PDTEXT SB_G2DTNRM SB_G2DTEXT` | — | — | M2 |
| `dc.holly.ta`, `dc.holly.core` (`dc.holly`) | read-only: the 14 registers of `pvr_regs.h` named `TA_*`; the 45 that are neither `TA_*` nor `SPG_*` | `dc.holly.ta.list_init`, `.list_end`; `dc.holly.core.render_start`, `.render_end`, `.vblank` | `holly-ta`; `holly-core` | nodes M1, registers M2 |
| `dc.holly.spg` (`dc.holly`) | read-only: the 9 registers of `pvr_regs.h` named `SPG_*` | — | — | M2 |
| `dc.aica` (`dc`) | — | `dc.aica.interrupt`, `dc.aica.timer` | — | M1 |
| `dc.aica.common`, `dc.aica.rtc` (`dc.aica`) | read-only: the fields of the 23 words of the common block up to `0x2C00` as `AICA::DebugReadRegs` gives them (naming rule below), but `RBP`, `RBL` and `TESTB0`, the word of 0x2804, which read the DSP's `RBPL` (`DspDebugState::rbpl`, the word the CPU wrote last: a CPU read of 0x2804 gives 0 from M3 on, 2.7; `M3-ROUND.md` M3-D6); `SCILV0-2` (write-only on the console), `SCIRE` and `MCIRE` (they store nothing) read `"n/a"`; in place of the file's words at `0x2D00` / `0x2D04`, `L` (the level the ARM7 reads at its `0x00802D00`) and `M` (the in-service flag, 0 or 1), both 8 bits wide and read from the sound CPU (`SoundCPU::DebugGetInterrupt`). RTC: `SECONDS` (32), `WRITE_ENABLE` (1) | — | — | M2 |
| `dc.aica.arm7` (`dc.aica`) | `PC` (first), `ENABLED` (width 1, **normative**: 1 = clocked, 0 = held in reset), `R0..R14`, `CPSR`, `SPSR`, `R13_USR R14_USR`, `R8_FIQ..R14_FIQ SPSR_FIQ`, `R13_IRQ R14_IRQ SPSR_IRQ`, and the same three for `SVC`, `ABT`, `UND`; all but `ENABLED` width 32 | — | `arm7-disassembly`, `arm7-memory`; `arm7-registers` from M2 | M1 |
| `dc.aica.sgc` (`dc.aica`) | — | `dc.aica.channels.key_on`, `.key_off`, `.loop` | `sgc` | M1 |
| `dc.aica.sgc.0 .. .63` (`dc.aica.sgc`) | read-only: the fields of the 18 words of the channel, by field (38 rows a channel; naming rule below) | — | — | `.0`, `.1` as nodes without registers in M1; all 64 with registers in M2 |
| `dc.aica.dsp` (`dc.aica`) | read-only, the thirteen of M3-D25: `STEP` (width 8), `MDEC_CT` (16), `RBP` (16), `RBL` (8) — `RBP` / `RBL` decoded from the DSP's `RBPL` —, and the core's datapath from `DspDebugState`: `ACC` (26), `Y_REG` (24), `FRC_REG` (13), `ADRS_REG` (12), `LATCH` (16), `LATCH_COPY` (16), `MEMVAL` (16), `NOFL_PIPE` (2), `MIXS_BANK` (1); signed values as their bits under the width | — | `dsp-disassembly` | M1 (`STEP` read 0 until M3); the nine datapath rows M3 (E8a) |
| `dc.system` (`dc`) | — | — | — | M1 |
| `dc.gdrom`, `dc.maple` (`dc.system`) | read-only. `dc.gdrom`: the drive's snapshot, the members of `GDRomDebugState` and the packet as `PACKET_0_3`, `PACKET_4_7`, `PACKET_8_11` (32 bits each, the first byte highest); `dc.maple`: the twelve maple registers of the system bus (`SB_M*`) | `dc.gdrom.command`; `dc.maple.dma` | — | nodes M1, registers M2 |
| `dc.timing` (`dc.system`) | `MODE` (text: the tier, `fast` until track 3), `SH4_CYCLES` (64: `sh4_sched_now64() + SH4_TIMESLICE - SliceCyclesLeft()`, the *time* of 2.3.4), `SCHED_NOW` (64: `sh4_sched_now64()`), `SH4_BACKEND` (text: `interpreter`, `x64`, `arm64`), `SH4_DEBUG_ENGINE` (text: `native` or `interpreter-fallback`, 2.3.5; `n/a` before the first thread of a machine selected a backend); track 3 adds more | — | — | M1 |

The actions are panel ids: the 35 ids of protocol v2 in M1 (`PANEL_IDS` of the UI's schema; the register-table test
`debugger.regs.panels` holds the emulator's tables to that list); `sh4-registers` and `arm7-registers` are added in M2
(the UI's schema with F3a, the emulator's list of ids with R6, the two actions with E7b): 37 ids from R6 on.

The M2 tree (`M2-ROUND.md` 3.6.1, package E7b) has 104 nodes, 70 of them new (`dc.holly.sb` and its four,
`dc.holly.spg`, `dc.aica.common`, `dc.aica.rtc`, `dc.aica.sgc.2` to `.63`); the 115 system bus registers that M1 left
out are in exactly one node each (60 + 11 + 6 + 11 + 15 + 12). Every M2 row is read-only: a write answers `-32013`
(the write semantics of device registers are M4's, the AICA's among them, 1.1). Its readers are free of
side effects: a register whose guest read has an effect is read from its store, and the session `regs` reads every row
a hundred times and finds the save state of every device, and the PVR register file, as they were. A read-only system
bus register that nothing assigns (`SB_MST`, `SB_MTXDAD`, `SB_MRXDAD`, `SB_MRXDBD`, the twelve G2 counters, the three
PVR counters; `SB_SDDIV`, `SB_GDSTARD` and `SB_GDLEND` until their first DMA) reads 0: as merged it read the low half
of a host function's address, different from run to run (E7b made it 0, which the guest can see, and which removes a
difference between runs). The naming rule for rows that have no `_addr` name in the code: the name of the data sheet,
which is the member name of the code's struct in upper case, without `$` and without a `_r` suffix (`$T` is `T`, `Mono`
is `MONO`); a quantity that the register file splits over two words is one row (`SA` from `SA_hi` and `SA_low`, `DMEA`
likewise); members of a snapshot are upper case with underscores; no row is wider than 64 bits. Where 3.6.1 was silent
E7b chose, and the integrator accepted: `DMEA` 23 bits with bits 0 and 1 reading 0; `DRGA` and `DLG` as 13-bit
members; `L` and `M` 8 bits (the mock has 3); the `PACKET` rows first byte highest; `kRegComputed` on `L`, `M`, the
clock, the drive and `SB_FFST`. Eight rows of `dc.aica.common` whose values are worked out (`MIEMP`, `MOEMP`, `EG`,
`SGC`, `LP`, `CA`, `AR`, `VREG`) carry no `kRegComputed`; nothing on the wire depends on it. The state of a channel that
no register holds (envelope levels, play position) is not a row: M3's SGC capture carries it. The M3 tree (E8a)
adds the nine datapath rows of `dc.aica.dsp`: 2,949 registers, 13 of them on `dc.aica.dsp`; every row stays read-only
in M3 (the owner's answer to `M3-ROUND.md` M3-Q3: the device writes are M4's).

Register values: `"0x"` + upper-case hex zero-padded to `ceil(width / 4)` digits (`dbg::regs::FormatValue`; bits of the
value above the width are ignored); a register with `width: 0` is free text and read-only; `"n/a"` when the value cannot
be read without a side effect, when the device has nothing to read from yet, and when no machine exists. A write-only
register reads `"n/a"`: on the system bus, and `SCILV0-2`, `SCIRE`, `MCIRE` of the AICA; `dc.aica.common` has `L` and
`M` from the sound CPU in place of the file's words at `0x2D00` / `0x2D04`. A register that `state.setRegister` can
write carries `flags: {"writable": true}` in `debugger.describe` (not in ticks). In M2 those are the registers of
`dc.sh4.cpu` (69 rows) and the registers of `dc.aica.arm7` but `ENABLED` (40 rows); a write to any other answers
`-32013`. A write of the ARM7's CPSR with mode bits that are no mode is refused (`-32013`), and so is a PC of the SH4
with bit 0 set. The ARM7's PC is stored with bits 0 and 1 clear; a write of it that stores another address than it had
empties the ARM7's code cache (`SoundCPU::InvalidateJitCache()`: a recompiler of the ARM7 enters a block by the masked
PC and has the unmasked one compiled in). An FPSCR write that changes PR, SZ or the rounding mode empties the SH4's code
cache (2.3.7 item 2), and so does a write that moves the PC of the SH4 (2.3.7 item 6). An SR write calls `UpdateINTC()`
when an interrupt is pending and let in, as the guest's `LDC` does: under the latch the acceptance is owed, and at a
code breakpoint of the SH4 it thereby moves from the next slice end to the leave. A CPSR write of the ARM7 goes through
the mode switch also when the mode stays, which rewrites the bank slots of the mode it leaves and, when that mode is
user or system, copies the CPSR as it was into the live SPSR, over which a mode that has an SPSR loads its own when it
is entered: behind a write and its write back the registers read as before, but SPSR when the write is made in one of
those two modes, and the save state can differ. A register
whose read has side effects is read through its array, never through the handler; the SH4 on-chip registers behind a
read function have no accessor in `hw/sh4` and get what `dbg_regs.cpp` gives each of them: `TMU TCNT0..2` computed from
the timer's base, shift and mask and the scheduler's time; `CCN CPU_VERSION` the constant its read function returns;
`CCN PRR`, `INTC IPRD`, `SCIF SCFDR2`, `TMU TCPR2` 0, which their read functions return whatever the state; `BSC PDTRA`,
`SCIF SCFSR2`, `SCIF SCFRDR2` and the SCI registers (the emulator has none of them) `"n/a"`. `SB_ISTNRM` comes through
the ASIC accessor (`DebugReadISTNRM`), the other system-bus registers through `SystemBus::DebugRead`.

`dc.aica.arm7`: `PC` is the address of the next instruction (`regs[R15_ARM_NEXT]`, not `regs[15]`, which is `PC + 8`
during an instruction and stale between them; `R15` is not shown). `CPSR` is composed from the flags, the interrupt
enables and the mode. `Rn_<mode>` and `SPSR_<mode>` are the banked registers of a mode; for the mode the processor is in
they show what `R8..R14` and `SPSR` show (the banked slot of the active mode is stale until the next mode switch, so the
reader substitutes the live values). Writes (M2; `SoundCPU::DebugSetReg`, package E6b, called by `dbg::regs::Write`):
`CPSR` through `CPUSwitchMode` + `CPUUpdateFlags`, stored under `0xF00000DF`; `PC` stored aligned, with
`dbg::arm7::OnPcWritten()` behind it (above). `ENABLED` stands behind `PC`, which stays the first register of the node (in the emulator; where
the register stands in a server's list is not contract). It reads `SoundCPU::DebugEnabled()`: 0 while the ARM7 is
held in reset, 1 while it is clocked (`0x0` and `0x1` on the wire). It is not monotonic: a guest may hold the ARM7
again, and 2ndmix does (the register goes 0, 1, 0, 1 during its start). It is the observable of the reset (2.10).
It is a register without a writer (the guest holds or releases the ARM7 through ARMRST; the debugger does not), and
`state.setRegister` on it answers as 3.3 and `dbg_regs.h` say for such a register: `-32013` (M2; `-32004` was the
answer while the server had no register writes at all). In the emulator it is the row
`{ "ENABLED", 1, kRegComputed, ... }` of `arm7_regs`, read through the selector `kArm7Enabled` (6.1.1 item 8).

`dc.aica.dsp`: `MDEC_CT` has width 16 from M1 on, the register's width on the hardware and in the T5.0 core. `MDEC_CT`
is the core's 16-bit counter, free running, once a sample (since W1 of the M3 round; the DSP before it kept a 32-bit
counter reloaded with the ring length, shown modulo 2^16). `RBP` and `RBL` of `dc.aica.dsp`, and `RBP`, `RBL` and
`TESTB0` of `dc.aica.common`, read the DSP's `RBPL`. The nine datapath rows are what "Load from emulator" needs to
continue the DSP exactly from a stop inside a sample's DSP phase (4.5); `LATCH` and `LATCH_COPY` are equal at every stop
the session `dsp` compares on the guest's program, so a swap of the two rows is caught only by `regs.devices` (5.3).

Which nodes a tick carries: a **full** tick carries every node that has registers; a **running** tick carries the four
**base paths** `dc.sh4.cpu`, `dc.aica.arm7`, `dc.aica.dsp`, `dc.timing` (`kNodeBase`). While running, registers outside
the base paths refresh at the full-tick rate (1 s, 2.9). On a backend without `SH4DBG_CTX_AT_SLICE` every running tick,
the full ones included, omits `dc.sh4.cpu` (`kNodeSh4Context`) and sets `registersStale: true`; the client renders that
path dimmed. A paused tick is full and never says `registersStale`; a tick with `reason: "no-game"` carries
`registers: {}` (2.3.1).

Watches:
- The expression grammar is `<node path>.<register name>`; the whole expression is lower-cased first and then split
  at its last dot, so the path is matched without regard to case as well as the register name (`DC.SH4.CPU.pc`
  resolves; as the mock does; the core lower-cases before `regs::Find`, which matches exactly, and `FindRegister`
  ignores case by itself); the stored expression is that lower-case form; nothing is trimmed. `state.watch` is
  all-or-nothing: every expression is resolved before the first is stored, and one that resolves to no register
  returns the in-band error `-32602` and adds nothing. An expression that is already stored is not added again, and
  one that occurs twice in one call is added once (the call still returns `{}`); the 256 bound counts what would be
  added. `state.modifyWatchExpression` resolves and stores its expression the same way, and may give a watch the
  expression another watch already has (two watches then show the same register).
- Watch ids are allocated from 1 and never reused within a server process, like breakpoint ids. No default watches.
- Watches are evaluated in every tick, running ticks included, in id order, whatever paths the tick carries: a watch
  whose path the tick does not carry still shows its current value. The value of a watch is formatted exactly like the
  value of its register in the same tick, so where the tick carries the path the two are equal. A value that cannot be
  read is `"n/a"`, exactly as in the register list (a register without a side-effect-free reader — `BSC PDTRA`,
  `SCIF SCFSR2`, `SCIF SCFRDR2`, the SCI registers — a device with nothing to read from yet, no machine). Running adds
  one case only: `dc.sh4.cpu.*` under `registersStale`.
- `state.editWatch` is `state.setRegister` on the watched register (M2; `-32004` before); an unknown `watchId` is
  `-32602`.

### 2.9 Tick policy

`event.tick` is the only notification. It is a snapshot of everything except `registers`, which the client merges per
path (3.2). Ticks are built on (E) by the run-state listener or inside a call, or under L when no emulator thread
exists.

- To the requesting client right after the `debugger.describe` response (response first, tick second): always full. A
  client may send `debugger.describe` any number of times on one connection; each is answered and followed by its full
  tick.
- Broadcast after every successful mutating RPC (`breakpoints.*`, the watch methods, the writes), whether or not the
  request changed anything; a `control.pause` or `control.runUntil` that changes nothing sends none. A client must not
  wait for a tick after a request that changed nothing (the mock sends fewer). While running this tick may be a running
  tick with the base paths only. Every accepted write (`state.writeMemory`, `state.setRegister`, `state.editWatch`) is
  followed by its tick, also while the target runs (the tick of an accepted write is the first tick since the request,
  in front of the response or behind it). `control.reset` sends no tick of its own: paused, the stop of the reset is its
  tick; running, nothing is published (2.3.2).
- Broadcast on every state change: resume (`running`), every stop (`paused`, new `stopId`), every lifetime change.
- Broadcast every `Debugger:TickMs` (200 ms) while `Running` and at least one client is connected. Every
  `Debugger:FullTickEvery`-th periodic tick (default 5, so 1 s) is full. The periodic tick is a `CC_READ` call: its
  closure builds nothing unless the lifetime is `Running` when it runs (no periodic tick while paused), and if (E) does
  not service it within 2 s the tick is skipped.
- A paused tick is always full (except `no-game`, 2.3.1). Its size with the M2 tree (channel and common registers by
  field, about 3,000 registers): a full tick with empty tables and an empty event log is about 127 KiB, and every tick
  carries the whole event log (200 entries), so a paused tick of a live session is about 160 KiB and more before watches
  and breakpoints (about 165 KB on the sounding guest with a full log). The budget of `M2-ROUND.md` M2-D27 (accepted by
  the project owner, M2-Q2) is 192 KiB for a full tick with empty tables, built in at most 5 ms on the quiet machine;
  past it the 64 channel nodes would carry their 18 words instead. `BuildTick` reserves 144 KiB (one regrow behind a full
  log). An on-demand way to read one node belongs to the protocol work of PLAN track 10.
- **Order.** A tick is handed to `dbg::net` in the place that built it — the listener, or the call — so ticks reach a
  client in the order they were built. The `debugger.describe` response is handed over inside the same call as its
  tick, directly in front of it: no other tick can come between the two. Responses of other requests are sent by (C)
  when their call has returned, so a tick that a request caused can reach the client before or after that request's
  response; and a broadcast tick can reach a connection before its first `debugger.describe` response. A client
  tolerates both. **A binary frame** (3.4; M3) goes to the requesting client right behind its response, to that client
  only — a tick may come between the two, and a client pairs a frame with its response by its `binaryId` —, and ticks
  behind it are not held up: the transport counts text and binary messages apart (2.11). The response of
  `state.recordSgcFrames` is deferred until the capture is complete (3.3), and its order against the capture's stop
  tick is not specified. A server may send more ticks than this section lists; nothing depends on a tick being "the next one"
  (the contract waits for "a tick within 1 s that shows the effect").
- Slow clients: a periodic tick is not queued for a client whose `QueuedBytes()` exceeds 1 MiB; that client is marked and
  the next tick sent to it is full. No other tick is ever dropped by the server: every tick that a request caused is
  queued, in the order they were built, so a client that keeps sending mutating requests without reading runs into
  the 16 MiB bound of text of the transport and is disconnected (2.11). Pinned by `debugger.contract.slow` (5.4).
- `tickId` increments per tick built; `timestamp` is wall-clock milliseconds since the Unix epoch.
- Running ticks carry `callstacks: {"sh4": [], "arm7": []}`.
- Client-absent grace (`kClientGraceMs`, 10 s): when the last client has been gone that long, the core clears the active
  mask (a `CC_TABLE` call with `emu::SetActiveMask(0)`: breakpoints stay listed and armed but none stops; the
  `categories` of the tick are not changed by it), appends `dc.debugger.note` to the event ring inside that call
  (2.6: no client is connected, so the event gates are closed and `DBG_EVENT` would record nothing), and then
  releases a parked thread (a `CC_EXEC` call with `emu::Resume()`). Inside the grace period breakpoints are still
  active: a target that was left running is stopped by its breakpoint with no client connected, and the expiry
  releases that stop. When (E) does not take the table call in time, the core makes it again 1 s later, and likewise
  the release, for as long as no client is connected. `dbg::emu` has no timer for this. **Breakpoints are active
  again when a client connects, and the connect also ends the expired state:** the next disconnect of a last client
  starts a full grace period, with breakpoints active all through it. A connect also drops a release that was still
  owed: nothing is released behind a client's back. A connect and a disconnect each hand the event gates and the
  active mask to (E) in a `CC_TABLE` call of their own (2.6), made again 250 ms later when (E) did not take it.
  The timer starts only at a disconnect, so a `StartPaused` park waits for its first client without limit. Pinned by
  `debugger.contract.grace` (5.4), the end of the expired state at a connect included since J1 (the fourth client
  of the test).
- What depends on a client being connected is this list — the event gates (2.6), the activity of breakpoints and the
  release of a park by the grace period, the periodic tick — and nothing else (D8). All four follow the core's own
  count of open clients (2.6).
- JSON emission follows 3.0 (never `null`, `{}` for success, required keys always present).

### 2.10 Callstack and symbols

- Symbols: reios hands the loaded image to `dbg::symbols::OnElfLoaded(const void* image, size_t size)` on (E), on every
  boot and reset that loads an ELF, and calls it with `(nullptr, 0)` on a boot that loads none (a disc, a `.bin`);
  `dbg_symbols` walks `.symtab` itself (named, defined symbols: functions, objects and symbols without a type), checks
  every offset of the image against its size, and replaces its table; `OnMachineDestroying()` empties it
  (`symbols::Clear()`). The table is read only inside calls and listeners (the one rule), so it needs no lock.
  Addresses are compared by their low 29 bits. A symbol without a size reaches to the next symbol, at most
  `kMaxSymbolReach` (`0x10000`) bytes. As built: a name ends at a NUL or at the end of its string table; where
  several symbols have one address, the one that names it is chosen by type first (a function before an object, an
  object before a symbol without a type), then by binding (a global symbol before a local one), then by section (a
  symbol of a section before an absolute one); and an absolute symbol counts as defined, as in the runner's ELF
  reader. The last step of that order came with J1: before it, with KallistiOS programs, `__executable_start`
  named the entry in place of `_start`. What is left of taking absolute symbols, seen with 2ndmix.elf: the linker
  constant `__tdata_size` (0) still names `0x00000000` and, by the 29-bit compare, `0xA0000000`, because nothing
  else is at that address. The text form of an address (`symbols::Format`) is `name` at the start of a
  symbol and `name+0x<offset in lower-case hex>` inside it; the wire field `symbol` of a callstack frame and the branch
  targets in disassembly text use it. Hex in the text fields of a frame is lower case throughout: `location` as well
  (below). A client, and the contract runner, accept either case there (the mock writes upper case). A symbol name is bytes the guest image supplies: on its way to the wire every
  byte outside printable ASCII becomes `?` (3.0, JSON emission, item 9). No ELF, no symbols.
- SH4 unwinder (heuristic, labelled as such in the UI; `dbg::callstack::Unwind`): frame 0 = PC. Frame 1 = `PR` with
  `location: "pr"` — for non-leaf code this is a location inside the current function, so it is not presented as the
  caller. Further frames by scanning up to `kScanWords` (1024) words upward from `R15` for values `v` that are even,
  executable (main memory or the boot ROM, compared with the mask of 2.3.3) and where the instruction at `v - 4` is
  `BSR / BSRF / JSR`; `location: "stack+0x<offset>"`, the offset of the word from `R15` in lower-case hex without
  leading zeros, as in `symbol`. The scan ends at the end of the memory the stack pointer is in: it does not go on
  into the mirror behind it, which is within "up to `kScanWords`". At most 32 frames in a tick
  (`kDefaultCallstackFrames`).
- ARM7: frame 0 = PC, frame 1 = `R14` with `location: "lr"`, then the same scan from `R13` for multiples of 4 in wave
  memory with `BL` at `v - 4`. **The ARM7 callstack has at least these two frames whenever the ARM7 is running, and
  more when the stack scan finds them**: the scan of the frozen `dbg_callstack.h` is part of the rule, and every
  statement of it — here, 4.2.1, 5.3, S11 — says "at least two". While the ARM7 is held in reset `callstacks.arm7`
  is `[]` and `state.getCallstack {arm7}` answers `frames: []`. **The reset has an observable**: the register
  `ENABLED` of `dc.aica.arm7` (width 1; 1 = the ARM7 is clocked, 0 = it is held in reset). Its source is
  `SoundCPU::DebugEnabled()`, the same one the unwinder and the step preconditions (2.5.3) use, so the three cannot
  disagree. In a paused tick `callstacks.arm7` is `[]` exactly when `ENABLED` reads 0 and has at least two frames
  when it reads 1, and `state.getCallstack {arm7}` agrees. The pairing is checked inside one tick: `dc.aica.arm7` is
  one of the four base paths (2.8), so every tick of a machine carries the register (a `no-game` tick has no
  registers and no frames, 2.3.1). In running ticks `callstacks.arm7` stays `[]` whatever `ENABLED` reads. The
  contract checks the pair (5.5 S11). The guest-visible register ARMRST is not the observable (2.7).
- Sent in paused ticks (`callstacks.sh4`, `callstacks.arm7`) and through `state.getCallstack` (`maxFrames` default 32,
  at most 64). Running ticks carry empty arrays. `state.getCallstack` works while running: it unwinds at the slice hook,
  the SH4 only on a backend with `SH4DBG_CTX_AT_SLICE` (otherwise `frames: []`, 2.2.8).

### 2.11 Serving the UI

Configuration: section `Debugger`; the command-line form is `-config Debugger:Port=0` (existing keys are written the
same way, `-config audio:backend=none`; several items are separated by ` , `). The keys stay under `Debugger:` for now
although the server is the control surface (D8); a rename, if there is one, comes with PLAN track 10.

| Key | Default | Read by | Meaning |
| --- | --- | --- | --- |
| `Enabled` | `yes`; in an automated run and in a selftest session `no` | `dbg::Init` | start the server at `dbg::Init`. **In an automated run (`RunControl::automated()`: headless, or one of the three `Exit.*` keys that end a run — `Exit.AfterVblanks`, `Exit.AfterSeconds`, `Exit.OnGuestExit`; a run with only `Exit.StateHash`, `Exit.FrameHash` or `Exit.FramePng` is not automated) the server is off unless the key is set explicitly** — given on the command line or present in `emu.cfg` (`cfgExists("Debugger", "Enabled") == 2`); a test run opens no listening socket unasked. The same holds while `Debugger:SelfTest` is set (`dbg::selftest::Requested()`): a session starts no server unless the key is set explicitly, headless or not — its tests stand in place of a client, and a client beside them would rewrite the event gates and the active mask they set (5.3) |
| `Bind` | `127.0.0.1` | `dbg::Init` | listen address. Anything but a loopback address requires `AllowedHosts` to be set and logs a warning that the server is reachable without authentication. A loopback address is one of the texts `127.0.0.1`, `localhost`, `::1` (the names the Host rule below accepts without `AllowedHosts`), compared without regard to case; `dbg::Init` compares text and resolves nothing (it includes no socket header, 2), so `127.0.0.2`, or a host name that resolves to the loopback interface, counts as non-loopback. With a non-loopback address and an empty `AllowedHosts` the server is not started (one line on stderr; decided before anything is started); under `Required` that is exit code 2 |
| `Port` | `55543` | `dbg::Init` | `0` asks the OS for a free port. A value outside 0..65535 is a server that cannot be started (`net::Server::Start` refuses it with its line on stderr): as a failed bind, below |
| `Required` | `no` | `dbg::Init` | `yes` makes a server that was asked for and cannot be started fatal. "Cannot be started" is any `false` of `core::Start` or `net::Server::Start` (a failed bind, an address that does not resolve, a `Port` out of range, a thread or the socket library that cannot be set up), or a non-loopback `Bind` without `AllowedHosts`. In every such case `dbg::Init` first calls `core::Stop()` where `core::Start` had succeeded: no core thread and no run-state listener stay behind. With `yes` it then prints one line on stderr, which starts `Debugger: ` and names the key, and calls `exit(2)`; no machine and no emulator thread exist yet, and no `nulldc-exit` line is printed (that line and its exit code 1 are `runctl_fail`'s, for a run that had started). With `no` the failure is one line of `dbg::Init` on stderr (a warning; a busy port is the usual case) and the emulator continues without the server and without the core. In both cases the line of `dbg::Init` is the second one on stderr when it was `net::Server::Start` that failed: the transport has printed its own line, which says why (Appendix B), in front of it. **`Required=yes` while the server is not enabled is a configuration error** (below): the key never silently does nothing |
| `AllowedHosts` | empty | `dbg::Init` | comma-separated extra host names accepted in `Host` and `Origin` |
| `WebRoot` | empty | `dbg::Init` | directory served instead of the embedded bundle |
| `StartPaused` | `no` | `dbg::emu` (at `OnMachineCreated()`); `dbg::Init`, for the check below | 2.2.9. **`StartPaused=yes` while the server is not enabled is a configuration error, and with a server that cannot be started it is fatal as `Required=yes` is** (below), except in a selftest session, whose test resumes the start park |
| `TickMs` | `200` | `dbg::Init` | period of the running tick, in milliseconds; a value below 1 is replaced by the default. `dbg::Init` hands the value to the core, which owns the periodic tick, before it starts it: `core::Configure(tickMs, fullTickEvery)` of E5's private header (6.1); a core that was never configured has the defaults. The contract gates are run with the defaults of this key and the next (5.5.3) |
| `FullTickEvery` | `5` | `dbg::Init` | every n-th periodic tick is full; a value below 1 is replaced by the default (1 makes every periodic tick full). Handed to the core with `TickMs`, in the same call |
| `SelfTest` | empty | `dbg::selftest` | run the in-emulator selftest (5.3) and exit. While it is set the default of `Enabled` is `no` (above) |
| `ForceFallback` | `no` | `dbg::emu` (at `OnMachineCreated()`) | testing only: treat the active SH4 JIT backend as one without native breakpoints (2.3.5, gate B12); no effect on the interpreter |

Without `Debugger:Enabled` nothing is started; the hooks and helpers of `debugger.h` work all the same (a selftest
session needs no server, and has none unless the key is set explicitly).

**Keys that need a server, without one.** `Debugger:Required=yes` or `Debugger:StartPaused=yes` while the server is not
enabled — `Enabled` is `no`, as given or by its default (an automated run, a selftest session) — is a configuration
error: `dbg::Init` prints one line on stderr that names the key and says that `Debugger:Enabled=yes` is missing, and
calls `exit(2)`, as for a required server that cannot be started (no machine and no emulator thread exist yet; no
`nulldc-exit` line).
A harness that passes `Required=yes` and forgets `Enabled=yes` would otherwise wait for a port line that never comes,
and a run with `StartPaused=yes` and no server would park at its start with nothing to resume it. One exception: in a
selftest session (`dbg::selftest::Requested()`) `StartPaused=yes` without a server is allowed — the sessions `sh4bp`,
`sh4bpboot` and `sh4bpbase` set the key, have no server, and their tests resume the park; `Required=yes` is an error
there as anywhere. `dbg::Init` makes the check directly behind the default and the key of `Enabled`, before anything
is started; for it, it reads `StartPaused`, which is otherwise `dbg::emu`'s key. The same holds for the outcome: outside a
selftest session `StartPaused=yes` makes a server that was enabled and cannot be started fatal, exactly as
`Required=yes` does (the `Required` row: `core::Stop()` where the core had started, one line of `dbg::Init` on
stderr, `exit(2)`, no `nulldc-exit` line) — a start park without a server has nothing to resume it, whichever way the server came to be
missing. A harness that starts paused may set `Required=yes` as well (5.4 does); it changes nothing. A build without the debugger (`NULLDC_DEBUGGER=OFF`) reads none of
the keys.

At start the emulator prints exactly `Debugger: http://<bind>:<port>/`, with the `Bind` text and the real port, as one
line on stdout, flushed (test harnesses parse it; a run without a server prints no such line). A `Bind` text that
contains a colon is an IPv6 literal and is printed in brackets, so that the line is a URL: `Bind=::1` gives
`Debugger: http://[::1]:<port>/`. The `client` text of a `debugger.describe` (3.0) is printed as one line on stdout,
`Debugger: client <id>: <text>` — `<id>` the transport's client id; the text with every byte outside printable ASCII
replaced by `?` and cut to 256 bytes, so that it stays one line — once per connection, for the first well-formed
describe that carries a `client` string. It is printed before the request is dispatched, so also when that describe
is then answered `-32000`. No harness parses that line, and it cannot be taken for the port line, which starts
`Debugger: http://`.
`dbg::Init` blocks for the bind only and does not change signal dispositions. The state of the core is allocated
once and never destroyed, so an `exit()` that does not pass `dbg::Term` (the `exit(2)` above, a failed run) finds no
thread object that is still joinable.

The automated-run rule has one definition, `RunControl::automated()` in `libswirl/utils/run_control.cpp` (headless, or
`Exit.AfterVblanks`, `Exit.AfterSeconds` or `Exit.OnGuestExit` set); `dbg::Init` asks it through
`bool runctl_automated()`, which E5 added to `utils/run_control.h` (6.1). `runctl_configure()` runs before `dbg::Init`.
The selftest-session rule is `dbg::selftest::Requested()` (`dbg_selftest.h`: is `Debugger:SelfTest` set; callable from
`dbg::Init` on). So the default of `Enabled` is `no` exactly when `runctl_automated() || dbg::selftest::Requested()`.

HTTP and WebSocket rules (implemented by E1):
- One listening socket. Only `GET` is served (405 otherwise). `GET /ws` is the WebSocket upgrade; no
  `Sec-WebSocket-Protocol` is required; every other `GET` is a static file. The query string is ignored.
- Every request (static file and upgrade) is rejected with 403 unless the `Host` header, port stripped, is one of
  `127.0.0.1`, `localhost`, `[::1]` or an entry of `AllowedHosts` (case-insensitive). This is the defence against DNS
  rebinding. It matters more once the server can change settings and load files (track 10); nothing is relaxed for that.
- The upgrade additionally requires `Origin` to be absent, or to parse as `http(s)://<host>[:port]` with `<host>` exactly
  (case-insensitive) in the same list; the literal `null` is rejected.
- Static files, in order: `WebRoot`, the embedded bundle, the built-in page (it says where to get the UI and names
  the build: `<title>nullDC <version> debugger</title>`, `<version>` the build's version string of 3.1). A path that is
  not found falls back to `index.html` only when its last segment has no file extension;
  otherwise 404. `index.html` is sent with `Cache-Control: no-cache`, hashed assets (`assets/*`) with
  `max-age=31536000, immutable`. MIME table: html, js, css, json, wasm (`application/wasm`), png, svg, ico, ttf, woff2,
  map. Every response carries `X-Content-Type-Options: nosniff`. Embedded files are stored gzip-compressed and sent with
  `Content-Encoding: gzip`.
- Limits: 1 MiB per inbound message. A larger one is answered with a close, code 1009, and the close frame is on the
  wire before the socket is closed, also while the client is still sending (a client that finds the socket cut
  before it has read the frame sees 1006; 3.6). The transport of E1 does this as built: measured with an 8 MiB and a
  32 MiB message, the close frame arrives while the client is still sending and the socket is cut 2 s later. 16 clients (a further upgrade
  gets 503); a client that lets more than 16 MiB of text, or more than 64 MiB of binary messages, queue up is
  disconnected; the two are counted apart (`M3-ROUND.md` M3-D29, package E8b: a capture of 4096 frames is a binary
  message of 32 MiB and its header, and the ticks behind it must not cut the client off). A message of any size is
  taken; the bounds are on what waits. Request parameters are bounded as in 3.6.
- All socket writes use `MSG_NOSIGNAL` (Linux) / `SO_NOSIGPIPE` (macOS). On Windows the listening socket uses
  `SO_EXCLUSIVEADDRUSE`, never `SO_REUSEADDR`.

Embedding the UI (`debugger.cmake` with `cmake/dbg_webui.cmake` at configure time and `cmake/dbg_webui_embed.cmake` at
build time; as built):
- Every build of the UI (`build:native`, `build:wasm`) ends by writing `bundle-info.json` into its output directory
  (`scripts/bundle-info.ts`): `{version, protocol, gitRev, files: [{path, bytes, sha256}]}`. `version` is the UI's
  version by the rule both repositories share since the platform round (`PLATFORM-ROUND.md` 5.1, package VCu;
  `scripts/version.ts`): the base `3.0.0-pre` of `package.json`, made `3.0.0-pre+g<h>` (`<h>` the first 10 characters
  of the commit) while no tag `v*` is reachable, `3.0.0-pre` on the tag `v3.0.0-pre`, `3.0.0-pre+<N>.g<h>` N commits
  after it, with `.dirty` (`+dirty` on the tag) behind a tracked change, and `<base>+nogit` outside a checkout (before
  VCu it was the base followed by `+<git describe --tags --always --dirty>`); `protocol` is the UI's
  `PROTOCOL_VERSION`; `gitRev` is the full commit hash, with `-dirty` when the tree had changes, and `""` without git;
  both strings are limited to the characters the embed script accepts
  (`[A-Za-z0-9_.+~:/ -]`, others replaced by `_`); `files` lists every file of the directory but hidden ones and
  `bundle-info.json` itself, sorted, with lower-case hex hashes.
- Cache variable `NULLDC_DEVTOOLS_DIST`: a `dist-native` directory; empty (the default) for auto-detection of
  `<devcast>/devtools/dist-native`, then `<devcast>/../dreamcast-devtools/dist-native`; `OFF` to embed nothing. In a
  package worktree the sibling path does not exist, so the variable is passed explicitly. Configure prints
  `nullDC devtools UI: embedding <dir> (<version>, protocol <n>)` or `nullDC devtools UI: not embedded (<reason>)`.
- A bundle whose `protocol` differs from `dbg::kProtocolVersion` is a configure error. So is a directory named
  explicitly that has no `index.html` or no `bundle-info.json`.
- Embedding needs CMake 3.19. With an older CMake an auto-detected bundle is not embedded (stated at configure time); an
  explicitly named one is a configure error.
- The embed command runs at build time, `DEPENDS <dir>/index.html <dir>/bundle-info.json`, checks every listed file
  against its `sha256` (a half-written or mixed-up build fails there), gzip-compresses each file
  (`file(ARCHIVE_CREATE ... FORMAT raw COMPRESSION GZip)`, with the timestamp bytes of the gzip header cleared so that
  the output depends on the content only) and emits `dbg_webui_data.cpp` in the build directory as brace-initialised
  `unsigned char` arrays with line breaks (the only form portable to MSVC).
- With no bundle the generated file holds an empty table and the built-in page is served. `dbg_webui.h` exposes the
  bundle info (`Embedded()`, `Version()`, `GitRev()`); `debugger.describe` reports `ui: "none"` or the bundle version.
- The client detects a mismatched bundle served from `WebRoot` or a dev server by comparing `describe.protocol` with its
  own `PROTOCOL_VERSION` (3.0).

## 3. Protocol v3 (schema + client + mock + emulator change together)

Protocol v2 is devtools-protocol.md; everything in it stays valid unless this section changes it. "v3 server" below means
the mock and the emulator; both implement this section, and the contract runner (5.5) holds both to it. The zod schemas
of the UI repository (`src/lib/debuggerSchema.ts`, `src/lib/binaryFrame.ts`) are this section as code: the client
schemas, and `ConformanceSchemas`, the strict copy the runner uses.

### 3.0 Rules

- **Framing.** JSON-RPC 2.0, one message per WebSocket text frame, UTF-8. No batches. Requests carry a numeric `id`,
  echoed as a JSON number, and named parameters: `params` is an object, and a request without `params` means `{}`. The
  server sends no requests, only responses and the notification `event.tick`. Binary frames are described in 3.4.
  Members of a request, and keys of `params`, that this section does not name are ignored by a v3 server: the request
  is answered as without them, and they are never `-32600` or `-32602` (a later client can add an optional parameter
  and still talk to this server; the mock's parameter objects strip what they do not know). A member this section
  does name keeps its rules: a `params` that is not an object is `-32600`, a missing or mistyped named parameter is
  `-32602`. Numbers, as the emulator reads them: a parameter that is an unsigned integer may be written as any JSON
  number with that value, so `16.0` is 16 (a client written in JavaScript has one kind of number); a negative number
  or one with a fraction is mistyped. The `id` of a request is echoed as the JSON number the server's JSON library
  writes for it (an integer as it came).
- **Malformed messages.** Text that is not JSON is answered with `-32700` and `id: null`. A batch (an array), any other
  non-object, an object without `"jsonrpc": "2.0"`, a request whose `id` is not a number, a request without a `method`
  string, and a request whose `params` is present and not an object are answered with `-32600`; the response `id`
  echoes the request's `id` when that is a number and is `null` otherwise. A well-formed message without `id` (a
  notification from the client) is ignored: no answer.
- **Versioning.** `debugger.describe` params gain optional `protocol` (the client's `PROTOCOL_VERSION`, 3) and `client`
  (free text; the emulator prints it as one line on stdout, 2.11, and does nothing else with it). The result carries
  `protocol: 3`, an integer. The server answers whatever the client's number
  is, and without one: a client that says 2, or sends no params, gets the same v3 result and the same tick. The client
  compares `describe.protocol` with its `PROTOCOL_VERSION` and shows a banner on mismatch (it can be dismissed and
  returns with the next connection). Only an absent `protocol` means a v2 server — never an absent `capabilities`,
  which v2 servers send with a vocabulary of their own and which a client reads only when `protocol` is present (F3b:
  the lists `targets` and `capabilities` are read for any present `protocol`; no server sends a lower number than 3).
- **Protocol v2 compatibility of the client ended with package F3b** (M2; 4.4). The only v2 server was the mock of
  old builds. Until F3b the M1 client kept what it had: for a v2 server absent `targets`, `categories`, `stopId`
  select the v2 behaviour (the literals of 3.1, the stop rule of 3.2), binary frames have no header (3.4), and every
  target and capability is assumed. Since F3b a result without `protocol` gets the version banner, like any other
  mismatch, and is treated as listing no target and no capability; nothing else is promised for that connection:
  those four behaviours and the branches of the panels that served them are gone, and the clauses of 3.1, 3.2 and 3.4
  that name a v2 server lapsed with them.
- **Additive fields.** The client's zod objects strip unknown keys, so new optional fields are safe for an old client;
  a v3 server ignores what it does not know in a request (Framing), so a new optional parameter is safe for an old
  server. New fields are optional in the client schema (a v2 server omits them; until F3b the client still served
  one); "always" below is the v3 server's obligation, and the strict copy requires it.
- **Open enums.** In server-to-client messages the client parses `subsystem`, `severity`, `validity`, `actions` entries
  and `reason` as open strings (`z.string()`); it renders an unknown `subsystem` / `severity` neutrally and ignores an
  unknown action. The server sends only the values listed in this section. Closed and unchanged: `state`
  (`running | paused`), `kind` (`code | event`), `build` (`native | wasm`), `target` (`sh4 | arm7 | dsp`).
- **JSON emission (both servers; nlohmann's default value is `null`, so this is enforced in typed serialisers).**
  1. `null` never appears, except as the `id` of a `-32700` or `-32600` response. Optional fields are omitted.
  2. The success result of every method whose result type is `RpcError` is the empty object `{}`.
  3. Every tick has the keys `tickId`, `timestamp`, `executionState`, `registers`, `breakpoints`, `eventLog`, `watches`,
     `callstacks`, `categories` (objects and arrays possibly empty).
  4. Every node has `path`, `label`, `description` (possibly `""`); every register has `name`, `value`, `width`; in
     `debugger.describe` a register that `state.setRegister` can write also carries `flags: {"writable": true}` (2.8);
     no register of a tick carries `flags` (invariant I4, 5.5).
  5. Empty `actions`, `events`, `children`, `registers` arrays of a node are omitted.
  6. Addresses, ids and counters are JSON numbers (unsigned integers, 0..4294967295); register values are strings.
  7. `error` objects have `code` and `message` only; no `data`, no stack trace.
  8. A response has exactly the keys `jsonrpc`, `id` and one of `result` / `error`.
  9. Every string is valid UTF-8. In the emulator, text that comes from the guest or the host — ELF symbol names in
     `symbol` and in disassembly text, event messages, text registers — has every byte outside printable ASCII
     (`0x20`..`0x7E`) replaced by `?` before it is serialised, so that building a tick or a response cannot fail on
     its content (nlohmann's `dump()` throws on invalid UTF-8: in the listener that is forbidden, 2.2.4, and in a call
     it would turn every tick into `-32603`).
- **Which error wins** when several apply to one request, first to last: `-32700`; `-32600`; `-32601` (unknown
  method); `-32004` for a method of a later milestone or of a capability the server does not have; `-32602` for what
  the request alone shows (a parameter that is missing or mistyped, a bound of 3.6 on a parameter); the lifetime
  (`-32002`, `-32001`); `-32003` (a step while running); then the method's own errors, in the order the method states.
  The `-32004` of a target or an event is one of the method's own errors, and so are the `-32602`s that need the tables
  or the target to be decided (an unknown id, an unresolvable expression, an address that does not fit its target, the
  `address` a code breakpoint lacks, a full table). A probe for the `-32004` of a milestone or capability therefore does
  not depend on its parameters being valid, nor on the lifetime: both servers answer it before either is looked at —
  the emulator because such a method is simply absent from its dispatcher, the mock also for a method whose capability
  it was configured without (4.2.1); a step for a target without stepping answers `-32003` while running and
  `-32004` while paused; and `breakpoints.add` answers the `-32004` of its event or target before the `-32602` of its
  address (the order is in 3.3).
- **Extensions.** The methods of PLAN track 10 (settings, machine lifecycle, input mapping, notifications) will be added
  to this section under the same rules: new method names, announced by new capability strings, failures on the channel
  their result type fixes, gated by new contract scenarios. Nothing in v3 reserves or forbids them; a v3 server answers
  a method it does not know with `-32601`.

### 3.1 `debugger.describe`

Params: `{ protocol?: number, client?: string }`. Result:

```ts
{ emulator: { name: "nullDC", version: string, build: "native" },   // version: the build's version (below)
  protocol: 3,                 // always; an integer
  ui: string,                  // always: "none" or the version of the embedded bundle
  capabilities: string[],      // always (possibly empty)
  targets: Target[],           // always
  deviceTree: DeviceNodeDescriptor[] }

Target = { id: "sh4" | "arm7" | "dsp", label: string,
           pcPath: string, pcRegister: string,   // where the current PC / step is in the tree
           breakpointEvent: string,              // event name of this target's code breakpoints
           instructionSize: number, addressMax: number,
           addressRadix: 10 | 16, addressDigits: number,   // how panels, inputs and toasts format and parse addresses
           defaultDisassemblyAddress: number,
           defaultMemoryAddress?: number,        // absent iff memory is false
           memory: boolean,                      // state.getMemorySlice works for this target
           stepping: boolean }                   // control.step* and code breakpoints work for this target
```

**The target table is normative for every target a v3 server lists**: `id`, `pcPath`, `pcRegister`, `breakpointEvent`,
`instructionSize`, `addressMax`, `addressRadix`, `addressDigits` and `memory` have the values below. `label`,
`defaultDisassemblyAddress` and `defaultMemoryAddress` are the server's choice (an emulator may give the entry of the
loaded program); the last column is what the emulator sends in M1. `stepping` follows the milestone.

| `id` | `pcPath` / `pcRegister` | `breakpointEvent` | size | `addressMax` | radix / digits | `memory` | `stepping` | default disassembly / memory (emulator) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `sh4` | `dc.sh4.cpu` / `PC` | `dc.sh4.cpu.pc` | 2 | `0xFFFFFFFF` | 16 / 8 | true | true | `0x8C010000` / `0x8C010000` |
| `arm7` | `dc.aica.arm7` / `PC` | `dc.aica.arm7.pc` | 4 | `0x00FFFFFF` | 16 / 8 | true | false in M1, true from M2 (what `emu::TargetSteps(Target::Arm7)` says) | `0x00000000` / `0x00000000` |
| `dsp` | `dc.aica.dsp` / `STEP` | `dc.aica.dsp.step` | 1 | 127 | 10 / 3 | false | true from E8a of the M3 round (what `emu::TargetSteps(Target::Dsp)` says; false before it) | 0 / — |

- **An M1 emulator lists all three targets**, `arm7` and `dsp` with `stepping: false` until their milestones. In general
  a target missing from a present `targets` array is unsupported: its panels show a notice instead of controls. A listed
  target supports `state.getDisassembly`; `memory` and `stepping` say what else works. Something has to be readable at
  the `defaultMemoryAddress` of a target with memory. Until F3b, when `targets` was absent (v2 server) the panels fell
  back to the literals of protocol v2 for that connection; that fallback went with F3b (3.0).
- `capabilities` (open strings) say which optional methods work: M1 `"event-breakpoints"`, `"callstack"`; M2 adds
  `"memory-write"`, `"register-write"`, `"reset"`; M3 adds `"sgc-capture"`, `"memory-binary"` (the emulator from E8b of
  the M3 round on); M4 adds `"tables"`,
  `"holly-capture"`. A server that implements a method advertises its capability, and one that does not advertise it
  answers `-32004`. Two more describe the emulator at the moment of the call: `"sh4-debug-native"` or
  `"sh4-debug-fallback"` (2.3.5; exactly one of them once an SH4 backend is selected, which is from the first park of a
  machine's first thread, so also at the `StartPaused` stop; neither with no game loaded or before that). Their live
  form is the tick: `notice` and `dc.timing`. There is no `"dsp-forced"`: forcing was never built, and F4a removed the
  capability from the UI's schema (2.5.5).
- `deviceTree` is 2.8.
- `emulator.version` is the build's version string (`nulldc_version()`, made at build time from the tag, the commits
  since it and a dirty tree by the rule of `PLATFORM-ROUND.md` 5.1, package VTe): `3.0.0-pre+g<h>` (`<h>` the first 10
  characters of the commit) until the tag `v3.0.0-pre` exists, `3.0.0-pre` on it, `3.0.0-pre+<N>.g<h>` after it,
  `.dirty` or `+dirty` behind a tracked change, `3.0.0-pre+nogit` without git. It is what `--version` prints behind
  `nullDC `, and it was the literal `"3.0.0"` until the platform round. `ui` is the embedded bundle's version by the
  same rule (2.11).
- In `NoMachine` the result is the static tree with every register value `"n/a"`, and the two engine capabilities are
  absent.
- `debugger.describe` may be sent any number of times on one connection (a client asks again after a describe that
  failed, on the reconnect delays of 4.3); each is followed by a full tick.

### 3.2 `event.tick`

```ts
{ tickId: number, timestamp: number,   // timestamp: wall-clock ms since the Unix epoch
  executionState: {
    state: "running" | "paused",
    stopId: number,              // always; 2.3.1
    reason?: string,             // always in paused ticks, never in running ticks; values in 2.3.1
    breakpointId?: number,       // in the paused ticks of a stop caused by a breakpoint, while that breakpoint exists
    registersStale?: boolean,    // only when true, and only in running ticks; 2.2.8
    notice?: string },           // only when a fallback or forcing statement applies (2.3.5, 2.5.3, 2.5.5): one line per
                                 // statement, joined with "\n", each 1 to 200 characters
  registers: Record<string, RegisterValue[]>,
  breakpoints: Record<string, BreakpointDescriptor>,      // key = decimal id
  eventLog: EventLogEntry[],                               // at most 200, oldest first; 2.6
  watches: WatchDescriptor[],                              // always; id order
  callstacks: { sh4: CallstackFrame[], arm7: CallstackFrame[] },   // always both keys; empty arrays while running
  categories: Record<string, { muted: boolean, soloed: boolean }> } // always the keys events, sh4, arm7, dsp
```

- **`registers` is merged per path.** A path present in a tick carries its complete register list (exactly the describe
  names and widths, in describe order) and replaces the stored list; an absent path keeps its value. The client clears
  what it holds of a connection when the connection ends; nothing of one connection is kept for the next. The tick
  after `debugger.describe` is always full; a paused tick is always full; while running every fifth periodic tick is
  full (2.9); a running tick carries at least the four base paths (2.8; `dc.sh4.cpu` is absent exactly when
  `registersStale` is true). **With `reason: "no-game"` the tick carries `registers: {}` and the client drops every
  register value it holds while that reason lasts**; it reads its views again when the reason leaves `no-game` (2.3.1).
- `stopId`: the client follows the PC in the disassembly, refetches memory and disassembly and fires the breakpoint toast
  when `state` is `paused` and `stopId` differs from the last one it acted on (the first paused tick of a connection
  counts). It never does so merely because a paused tick arrived. **A client may skip stops when several are queued and
  must end on the latest; it never has to act on each.** (The v2 rule for a tick without `stopId` went with F3b,
  3.0.)
- `breakpointId`: 2.3.1. When its breakpoint is removed during the stop, the later paused ticks of that stop do not
  carry it.
- `categories` is `z.record(z.string(), BreakpointCategoryStateSchema).optional()` in the client schema (an enum-keyed
  record is exhaustive and closed in zod 4). The server always sends the four known keys; the client reads only keys it
  knows, and restores its mute / solo buttons from them.
- `registersStale`: the client renders `dc.sh4.cpu` dimmed while it is true. It never appears in a paused tick.
- `notice` and `reason` are shown next to the run controls.
- `timestamp` of the tick and of every event-log entry is wall-clock milliseconds since the Unix epoch (the UI shows the
  entry's as a time of day; the contract checks both against its own clock, within one day).
- `EventLogEntry`: `eventId` decimal string, a sequence number of the process: ascending within a log, and the same id
  is the same entry in every tick; `subsystem` one of `sh4`, `holly`, `ta`, `core`, `aica`, `dsp`, `arm7`, `gdrom`,
  `maple`, `system`, `debugger`; `severity` one of `trace`, `info`, `warn`, `error`;
  `metadata: { event: string, cycles: string }` (`cycles` decimal).
- `WatchDescriptor.value` is a string, formatted like the register's value (2.8).
- `RegisterValue`, `BreakpointDescriptor`, `WatchDescriptor`, `CallstackFrame`, `DeviceNodeDescriptor` are otherwise
  unchanged from v2 (`CallstackFrame.location` now carries `"pr"`, `"lr"` or `"stack+0x.."` — the emulator writes the
  offset in lower-case hex, as in `symbol`, and a client accepts either case (2.10); frame 0 has none; a code
  breakpoint always has its `address`, an event breakpoint never has one). From M2 a register of a describe node is
  `name`, `value`, `width` and, for a register that `state.setRegister` can write, `flags: {"writable": true}` (2.8): in
  `debugger.describe` only, never in a tick (the contract's invariant I4: no register of a tick carries `flags`).
- **A callstack frame carries `sp`**: a v3 server sends it in every frame. The emulator sends the stack pointer of the
  frame's processor at the unwind (`R15`, `R13`), the same value in every frame of a callstack (`callstack::Frame::sp`
  — the heuristic unwinder knows no frame's own stack pointer; where a scanned return address was found is in
  `location`); the mock gives its scanned frames the address they were found at. No client and no test reads more into
  the field than that it is there.

### 3.3 Methods

"Errors" names the channel (3.5). "Class" is the call class of 2.2.2. Every method can also fail with `-32000`, `-32001`,
`-32002` according to the lifetime state (2.2.1), and with `-32602` for a missing or mistyped parameter or a bound of 3.6.
A method of a later milestone answers `-32004` until that milestone (before its parameters are looked at, 3.0).

| Method | Params | Result | Errors | Class | From |
| --- | --- | --- | --- | --- | --- |
| `debugger.describe` | `{protocol?, client?}` | 3.1; followed by a full tick to this client | JSON-RPC | read (in `NoMachine` answered from the constant tables; its tick is built in a table call) | M1, changed |
| `state.getMemorySlice` | `{target?: Target id = "sh4", address: number, length: number}` | `{baseAddress: number, data: number[], validity: "ok", unreadable?: [number, number][]}` — `baseAddress` is the requested address; `data` always has `length` entries, integers 0..255; unreadable bytes are 0; `unreadable` lists `[offset, length]` runs, omitted when empty; no wrap-around: a byte above the target's `addressMax` is unreadable (2.7) | JSON-RPC: `-32010` nothing readable; `-32004` target without memory | read | M1, changed |
| `state.getMemoryBinary` | `{target?, address, length}` | `{baseAddress, totalBytes, unreadable?, binaryId}`, then one binary frame `kind` 2 with `totalBytes == length` bytes | JSON-RPC, as above; no binary frame after an error | read | M3 |
| `state.getDisassembly` | `{target? = "sh4", address: number, count: number, context?: number}` | `{lines: [{address, bytes, disassembly}]}` — the lines whose address is at most `addressMax`: `count` of them, fewer only at `addressMax` (no wrap-around); addresses ascend by `instructionSize`; `bytes` is space-separated upper-case hex pairs in memory order (`"09 00"`); an unreadable word gives `bytes: "??"`, `disassembly: "<unreadable>"`; the value of `context` is ignored, but it has to be a number when it is present | JSON-RPC: `-32602` misaligned address, address above `addressMax`, or a `context` that is not a number (a mistyped named parameter, 3.0) | read | M1, changed |
| `state.getCallstack` | `{target: "sh4" \| "arm7", maxFrames?: number = 32}` | `{target, frames: CallstackFrame[]}`, indices from 0, at most `maxFrames` | JSON-RPC | read | M1 |
| `state.watch` | `{expressions: string[]}` | RpcError; all-or-nothing; resolved without regard to case and stored in lower case; stored duplicates, and duplicates within the call, skipped (2.8) | in-band: `-32602` unresolvable expression, or more than 256 watches after the call | table | M1, changed |
| `state.unwatch` | `{watchIds: number[]}` | RpcError; unknown ids ignored; the array has no bound of its own | in-band | table | M1 |
| `state.modifyWatchExpression` | `{watchId: number, newExpression: string}` | RpcError; the id is kept; an expression another watch has is allowed | in-band: `-32602` unknown id or unresolvable | table | M1 |
| `state.editWatch` | `{watchId: number, value: string}` | RpcError; = `state.setRegister` on the watched register (below) | in-band: `-32602` unknown `watchId`, then as `state.setRegister` from "width 0" on; `-32004` before M2 | write | M2 |
| `control.pause` | `{target?}` | RpcError | in-band; 2.3.2 | — | M1 |
| `control.runUntil` | `{}` | RpcError | in-band; 2.3.2 | exec | M1 |
| `control.step`, `control.stepOver`, `control.stepOut` | `{target}` | RpcError | in-band: `-32003`, `-32004`, `-32005`, `-32012` (the ARM7 held in reset; the DSP never answers it); 2.3.2 | exec | M1 `sh4`, M2 `arm7`, M3 `dsp` |
| `control.reset` | `{}` | RpcError | in-band | write | M2, new |
| `breakpoints.add` | `{event: string, address?: number, kind?: "code" \| "event", enabled?: boolean = true}`; `kind` defaults to `code` when `address` is present, else `event` | RpcError; the new breakpoint appears in a tick with a new id; the same request again makes a second breakpoint | in-band: `-32004` event not listed (for `event`: by no node of the device tree; for `code`: not the `breakpointEvent` of a listed target) or target without `stepping`; `-32602` missing or invalid `address` for a code breakpoint, or the table is full; in the order below | table | M1, changed |
| `breakpoints.remove` | `{id: number}` | RpcError | in-band: `-32602` unknown id | table | M1 |
| `breakpoints.toggle` | `{id: number, enabled: boolean}` | RpcError | in-band: `-32602` unknown id | table | M1 |
| `breakpoints.setCategoryStates` | `{categories: Record<string, {muted, soloed}>}` | RpcError; unknown keys ignored, absent keys unchanged; never needs a recompile | in-band | table | M1 |
| `state.writeMemory` | `{target: "sh4" \| "arm7" \| "dsp", address: number, data: number[]}` (integers 0..255) | RpcError; all-or-nothing; no wrap; served while the target runs; a tick behind every accepted write | in-band: `-32013` range not writable, or one that would pass the end of the address space; `-32004` for `dsp` | write | M2, new |
| `state.setRegister` | `{path: string, name: string, value: string}`; `value` is `0x` hex or decimal, the low `width` bits of it count (below) | RpcError | in-band, in this order: `-32602` unknown path / name; `-32013` register with `width: 0` or without a writer; `-32602` unparsable value; `-32013` a value the device refuses (below) | write | M2, new |
| `state.recordSgcFrames` | `{frames?: number = 1024}` (1..4096) | RpcError, **sent when the capture is complete** (deferred, below) | in-band: `-32003` a capture records already (it goes on); `-32014` not complete within 5 s, or ended by a reset; `-32001` ended by a host stop or a guest exit; `-32001` / `-32002` by lifetime, as a run-control request, answered by the core itself (below). It records whatever the ARM7 does: no `-32012` (`M3-ROUND.md` M3-D7) | exec when the target is paused (the call arms the capture and its resume), table when it runs (below) | M3 (E8b) |
| `state.getSgcFrameData` | `{}` | `{frameCount, channelsPerFrame: 64, bytesPerChannel: 128, totalBytes, binaryId}`, then one binary frame `kind` 1; while a capture records, the capture before it | JSON-RPC: `-32011` no capture; no binary frame after an error | read | M3 (E8b) |
| `state.getTable` | `{id: "tlb" \| "sq" \| "icache" \| "ocache" \| "ocram"}` | `{columns: string[], rows: string[][], note?: string}`; every row has `columns.length` cells | JSON-RPC | read | M4, new |
| `state.captureHolly` | `{what: "core" \| "ta"}` | `{what, format: "refsw2-log-gzip" \| "taframe4", totalBytes, binaryId}`, then one binary frame `kind` 3 (`core`) or 4 (`ta`) | JSON-RPC: `-32004` the active renderer produces no CORE log; `-32011` no frame rendered yet | read | M4, new |

- **A running target is asked things.** `debugger.describe`, `state.getMemorySlice` and `state.getDisassembly` (for
  `sh4` and `arm7`), `state.getCallstack`, `breakpoints.add / toggle / remove`, `breakpoints.setCategoryStates`,
  `state.watch / modifyWatchExpression / unwatch` all work while the target runs — the reads in the slice hook, the
  others in a service excursion (2.2.2) — each mutating one followed by its tick, `debugger.describe` by its full tick.
  None of them may answer `-32003`: among requests to a running target that code is for steps alone (3.5 has its
  other use, run control behind an armed action: a `control.runUntil` or a step for a stop that already has an action
  armed, 2.3.2). A breakpoint added while running is armed at the next slice end and hit at its next arrival. From M2
  the list gains `state.writeMemory`, `state.setRegister`, `state.editWatch` and `control.reset` (in a service
  excursion; each accepted write followed by its tick, `control.reset` by none, 2.9); none of them may answer
  `-32003`.
- **`state.setRegister` and `state.editWatch`** (`M2-ROUND.md` M2-D20). `path` and `name` are matched without regard to
  case, as a watch expression is; a text with a NUL in it is no path, name or watch expression (`-32602`). The answers,
  in this order behind the lifetime: unknown path or name `-32602`; a register of width 0 or without a writer `-32013`;
  a value that cannot be parsed `-32602`; a value the device refuses `-32013` — in M2 two: a PC of the SH4 with bit 0
  set, and a CPSR of the ARM7 whose mode bits are no mode. The value: ASCII blanks around it are dropped; then `0x` or
  `0X` and one or more hex digits of either case, or one or more decimal digits; nothing else (no sign, no empty
  text); any length, of which the low `width` bits count. `state.editWatch` is the same from "width 0" on; an unknown
  `watchId` is `-32602`. A register reads back what was **stored**: SR under `0x700083F3` and with RB 0 when MD is
  clear, FPSCR under `FPSCR_MASK`, the ARM7's CPSR under `0xF00000DF`, its PC aligned. What a write does besides the
  store is 2.8 ("Register values").
- **`breakpoints.add` checks in this order** (behind the generic checks of 3.0: a mistyped `event`, `address`, `kind`
  or `enabled` is `-32602` before any of it). First `kind`: as given, else `code` when `address` is present, else
  `event`. For `event`: the name must be an event the device tree lists, else `-32004` (the three `breakpointEvent`
  names are not events, and neither is a row of `dbg_events.h` that no node lists); a given `address` is ignored and
  not stored. For `code`: the name must be the `breakpointEvent` of a listed target, else `-32004`; that target must
  have `stepping`, else `-32004`; then `address` present and valid for the target (2.3.3: aligned, at most
  `addressMax`), else `-32602`. Then, for both kinds, the table bound (1024), `-32602`. An event name with a NUL in it is
  `-32602` (EXe of the M2 round). `breakpoints.add` has no `-32012`: a breakpoint on `dc.aica.arm7.pc` can be added in
  every state (2.5.3). The mock checks in the same order.
- `state.getDisassembly`: the text is what `dbg_disasm` prints (Appendix B): lower case, GNU objdump mnemonics and
  operand order (`bt.s` / `bf.s`), `.word 0x<raw>` for a word that is not an instruction; clients and the contract runner
  read the first word of a line as its mnemonic. On the `sh4` target the reios trap `0x085B` — which the emulator
  executes as an HLE call but which is not an SH4 instruction — is `.word 0x085b ! reios trap`. For `dsp`, `address` is
  the step, `bytes` is the step's four MPRO words in register order as 16 hex digits — each word as two pairs, high
  byte first: words `0x0123 0x4567 0x89AB 0xCDEF` give `"01 23 45 67 89 AB CD EF"` — and the text is the playground
  assembler's line (`MPRO[12] = TRA:3 TWT ...`); the line of an empty step is `MPRO[n] = TRA:0` (S10 holds the text
  rule against the playground's own disassembler, 5.5). A client does not assume more of a reply than this section promises:
  it drops lines whose address repeats or does not ascend.
- `state.recordSgcFrames`: the capture is the next `frames` AICA samples. If the emulator was running it keeps running.
  If it was paused it runs for exactly `frames` samples and pauses again: a `running` tick first (2.9: every state
  change is broadcast), then a `paused` tick with `reason: "capture"` and a new `stopId`. The response is sent when the
  capture is complete; its order against that paused tick is not specified. The buffer is server-global and replaced by
  each capture. As built (`M3-ROUND.md` M3-D27, M3-D28; package E8b):
  - **The frame's moment**: a frame is taken behind the sound generator's sweep of its sample (the channels'
    registers and state after their step, their outputs of that sample) and in front of its DSP phase; a capture's
    first frame is the first sample whose sweep comes behind the arm, so one armed with a burst suspended in front of a
    DSP step starts with the next sample. Frames come whatever the ARM7 does (the AICA is clocked whatever ARMRST
    says).
  - **The capture's own buffer**: a capture records into a buffer of its own and replaces the server's buffer when it
    is complete; until then `state.getSgcFrameData` gives the capture before it (or `-32011`). The server's buffer stays
    across a reset and a thread exit. A second `recordSgcFrames` while one records is `-32003`, and the first goes on.
  - **Stops and steps during a capture** keep it recording (a step's samples are frames). A guest reset or
    `control.reset` cancels it and answers `-32014` at once; a host stop or a guest exit cancels it and answers
    `-32001` at once (the cancel stands behind the drain of the call queue, 2.2.1); a capture whose machine does not run
    (a stop that is not resumed) is answered by the deadline, 5 s of host time (`kCaptureTimeoutMs`): `-32014`, and the
    capture is cancelled. `Capture` ranks below every other cause of a stop (2.3.1): a stop that meets it wins, and the
    response is sent all the same.
  - **The completion inside a suspended burst**: a capture armed at a stop with a burst suspended may complete in the
    rest of that burst, its last sample included; its stop is then published with no `running` tick in between, as a
    stop that the run-on of the park loop meets (2.2.4 a).
  - **The deferred response** (M3-D28): the method checks, makes its call (exec at a stop, arming the capture and the
    resume; table while running) and, when the capture was armed, keeps a pending record `{client, request id,
    deadline}`; the completion notice of `dbg::sgc` (`dbg_sgc.h`), given once for every capture armed — at its
    completion, or wherever a cancel ends it —, queues a message for (C), which answers the pending record and clears
    it. The core counts the notices it is owed and answers a new `recordSgcFrames` `-32003` while one is owed; it never
    answers a capture it can still cancel: at the deadline it cancels it in a table call and the notice answers (only
    when that call is not taken does (C) answer `-32014` itself, and the notice, owed still, answers nothing). A resume
    refused behind the arm cancels the capture and the notice answers `-32003`; a table closure that finds no emulator
    thread arms nothing and the method answers by the lifetime then. A selftest request (client 0) gets the response
    through a test hook.
  - **Its call class** follows the state, because the frozen `dbg_emu.h` allows `emu::Resume()` inside a `CC_EXEC`
    call only, and a `CC_TABLE` call returns no lifetime error (it runs on the caller in `Stopped` and `NoMachine`,
    2.2.2): the core looks at the published state (`emu::GetRunState()`) and answers `-32002` in `NoMachine` and
    `-32001` in `Stopped` itself, without a call; for a parked thread it makes a `CC_EXEC` call whose closure arms the
    capture and the resume; for a running one a `CC_TABLE` call whose closure arms the capture. A call that finds the
    other state than the one it was made for arms nothing and is made again in the other class (the `-32003` that
    `Call` returns for an exec call when the thread runs; a table closure that finds a stop in force), and one that
    finds no thread answers the lifetime error. No declaration changes for this (the mock classes the method as exec).
- `state.getMemoryBinary` (`M3-ROUND.md` M3-D36) is `state.getMemorySlice` with another bound (16 MiB,
  `kMaxMemoryBinaryBytes`) and a binary payload: a read (in the slice hook while running), no wrap above `addressMax`,
  unreadable bytes 0 and listed in `unreadable`, JSON-RPC `-32010` when nothing is readable, `-32004` for `dsp`, no
  `validity`. Its frame (kind 2) goes behind the response to the requesting client only; `binaryId` counts per
  connection from 1, one counter for kinds 1 and 2 (3.4).
- `state.captureHolly {core}` returns the refsw2 render log of the next rendered frame (the text format of
  devtools-panels-tools.md 6.2, first line `REFSW2LOG: 0`), gzip-compressed; it is available only with a renderer that
  produces that log: `refsw2` / `refmister` today, and the wgpu renderer of PLAN track 9, which is expected to keep
  producing it. `{ta}` returns the `TAFRAME4` image of the last completed frame (`tactx_write_frame`,
  `hw/pvr/ta_ctx.cpp`). Both wait for the queued frame (2.2).
- `state.getTable`: `sq` and `ocram` return their contents; `tlb` returns the entries in `WITH_MMU` builds and otherwise
  no rows and a `note`; `icache` / `ocache` return the lines of the cache model in a tier that has one (PLAN T3.3b) and
  otherwise no rows and the `note` "no cache model".

### 3.4 Binary frames

A v3 server prefixes every binary WebSocket frame with a 16-byte little-endian header (`kBinaryHeaderBytes`):

| Offset | Type | Field |
| --- | --- | --- |
| 0 | 4 bytes | magic `"NDB3"` (`4E 44 42 33`) |
| 4 | u32 | `kind`: 1 = SGC frames, 2 = memory, 3 = CORE capture, 4 = TA capture |
| 8 | u32 | `binaryId`: allocated per connection from 1, equal to the `binaryId` of the JSON result it belongs to |
| 12 | u32 | `payloadBytes`, equal to the result's `totalBytes` |

The JSON response is sent first, the binary frame second, to the requesting client only. Client (`debuggerClient.ts`,
`binaryFrame.ts`): subscribe before sending the request; accept only a frame whose magic, `kind` and `binaryId` match (the
transport fans binary frames out to all subscribers); require `payloadBytes == totalBytes` (and
`== frameCount * 8192` for kind 1); 10 s timeout; unsubscribe on every path; strip the header and pass on only the
payload, as `{frameCount, data}` for SGC. (A client connected to a server without `protocol` expected no header
until F3b dropped what the client did for a v2 server, 3.0.)

SGC payload (kind 1): `frameCount` x 64 channels x 128 bytes, little-endian; one frame is one AICA sample; the record
layout is devtools-protocol.md section 8 with two changes: `aeg_value` (offset 96) is the **level** (0 = silent,
`0x3FF` = loudest); byte 109 is flags — bit 0 channel enabled ("keyed on and not run to its end",
`AicaChannelDebug::enabled`), bits 1-2 AEG state (0 attack, 1 decay 1, 2 decay 2, 3 release); bytes 110..127 are zero.
As the emulator fills it (`dbg_sgc.h`, M3-D27): bytes 0..71 are the channel's registers as `AICA::DebugReadRegs` gives
them; bytes 76..83 (the filtered, post-AEG, post-FEG and post-TL samples) are 0 in the `fast` tier, whose sound generator
computes none of them (the `cycle-similar` AICA will); bytes 84, 86 and 88 are the channel's outputs of the sample (its
direct left and right signal and its send to the DSP, at 16-bit scale), which the sound generator keeps only while a
capture records. One `binaryId` counter per connection serves kinds 1 and 2 (M3-D36).

### 3.5 Errors

One convention. A method whose result type is `RpcError` (`control.*`, `breakpoints.*`, `state.watch`, `state.unwatch`,
`state.modifyWatchExpression`, `state.editWatch`, `state.writeMemory`, `state.setRegister`, `state.recordSgcFrames`:
`IN_BAND_ERROR_METHODS` of the schema) reports **every** failure of a well-formed request in-band, `-32603` included.
Every other method reports failures as JSON-RPC errors. `-32700`, `-32600` and `-32601` are always JSON-RPC errors.

**Error codes are normative; message texts are not.** `error.message` is a non-empty human-readable string, and no
client and no test may depend on its wording (the contract runner checks the code and that a message is there). The
table gives the texts the emulator sends.

| Code | The emulator's text | Meaning |
| --- | --- | --- |
| `-32700` | `parse error` | the frame is not JSON; response `id` is `null` |
| `-32600` | `invalid request` | not a JSON-RPC 2.0 request object as 3.0 defines it, or a batch; response `id` is the request's when that is a number, else `null` |
| `-32601` | `method not found: <name>` | method not in this section |
| `-32602` | `invalid params: <what>` | missing / mistyped parameter, bound of 3.6 exceeded, unknown breakpoint or watch id, unresolvable watch expression, unknown register path or name, a register value that is no number (3.3), misaligned or out-of-range address (address parameters), a `control.pause` target that is not a target id |
| `-32603` | `internal error` | an exception caught in the dispatcher; on the method's own channel |
| `-32000` | `emulator busy` | the emulator thread did not service the request within 2 s |
| `-32001` | `emulator stopped by host UI` | lifetime `Stopped` and the request must write or execute |
| `-32002` | `no game loaded` | lifetime `NoMachine` and the request needs a machine |
| `-32003` | `not paused` | a step was requested while running, or run control while another action is armed, or a capture while one records (`state.recordSgcFrames`, 3.3) |
| `-32004` | `not supported: <what>` | target, event, capability or milestone the server does not implement |
| `-32005` | `cannot step: <why>` | step precondition failed (2.3.2) |
| `-32010` | `address not readable` | nothing in the requested range is readable |
| `-32011` | `no capture available` | `state.getSgcFrameData` / `state.captureHolly` without data |
| `-32012` | `arm7 held in reset` | a step of the ARM7 while the guest holds it in reset (2.5.3); the DSP and the capture never answer it (2.5.2; `M3-ROUND.md` M3-D30) |
| `-32013` | `not writable: <what>` | a memory range or a register cannot be written, or the register's device refuses the value (M2: a PC of the SH4 with bit 0 set, a CPSR of the ARM7 whose mode bits are no mode); such a value is not a `-32602`, which is for a text that is no number |
| `-32014` | `capture timed out` | `state.recordSgcFrames` not complete within 5 s, or ended by a reset |

The codes are `dbg::ErrorCode` in `dbg_emu.h` (`kErrParse` ... `kErrCaptureTimeout`) and `DebuggerErrorCode` in the
schema. The client checks and shows the result of every `control.*` call; a refusal that repeats while its notice is on
screen is shown once, with a count (4.3).

### 3.6 Bounds

Violations are `-32602`. `state.getMemorySlice.length` 1..65536; `state.getMemoryBinary.length` 1..16777216;
`state.getDisassembly.count` 1..1024; `state.getCallstack.maxFrames` 1..64; `state.recordSgcFrames.frames` 1..4096;
`state.writeMemory.data.length` 1..65536; watch expression at most 256 characters, at most 64 expressions per call, at
most 256 watches; at most 1024 breakpoints; a tick carries at most 200 event-log entries and 32 callstack frames per
target; inbound message at most 1 MiB. The constants are in `dbg_emu.h` (`kMax...`) and `PROTOCOL_BOUNDS` of the schema.

Above 1 MiB the transport closes the connection (WebSocket close code 1009); a server may instead answer the oversized
message with an error. Either way the request is not executed and the server stays up. **The emulator answers with
the close, code 1009, and its close frame is on the wire before the socket is closed, also while the client is still
sending**: a client that finds the socket cut before it has read the frame sees 1006, and S12 fails on that (5.5).
The 16-client limit (2.11) is the transport's and is tested with it (5.2), not by the contract.

## 4. Frontend work (dreamcast-devtools)

Packages are sequential on `skmp/nulldc3`: F0, F1 and F2 are committed, and so are the devtools round (4.2.1, 4.3.1),
which closed what their verifications left and what revision 3 changed, and the runner follow-up (4.2.2: what the
decisions made after the round changed); the head is `22b358e`. Then one package per milestone. PLAN track 10 later merges the
machine side (load, settings, controls) into the same application: no page built by F3..F6 may depend on a debugging
session being in use, and the store keeps working with no game loaded.

### 4.1 F0 — hygiene (M0; done)

Delivered: the lint fixes of devtools-build.md; eslint ignores; `tsconfig` covers server, scripts and tests; scripts
`build` (= build:native), `typecheck`, `test:run`, `check`; the generated DSP wasm glue committed so a fresh clone
type-checks and builds without Rust (`build:dspsim` stayed for regeneration until M3, where F4b made it the emscripten
build of the emulator's core, 4.5); version from `package.json`
(`3.0.0-pre`, followed by `+<git describe>` in a checkout until the platform round, whose package VCu gave it the rule
of 2.11, `PLATFORM-ROUND.md` 5.1: `3.0.0-pre+g<h>` until the tag `v3.0.0-pre`), never "unknown"; README and in-app docs
describe the real
scripts and protocol; Dockerfile / fly.toml use the real scripts (they deploy only the mock demo); CI runs `npm ci` and
`npm run check` on Node 22, and a second job runs the e2e suite. No dependency major bumps.

Delivered beyond the plan: a Playwright end-to-end suite (`npm run test:e2e`, headless Chromium against the production
build and a mock per test, a scripted server for exact ticks and protocol v2; not part of `check`); an error boundary
around every dock panel and every route; Monaco bundled locally (no CDN) and route-level code splitting (both were M2
items of v2).

Closed by the devtools round: `check` runs `lint`, `typecheck`, `test:run`, the contract as the M1 gate
(`npm run contract -- --milestone M1`, 5.5.3), `build:native` and `build:wasm` (no v3 message breaks the wasm
transport build; the risk was silent rot).

### 4.2 F1 — protocol v3, mock module, contract runner (M1; done)

- **Schema** (`src/lib/debuggerSchema.ts`, with `src/lib/protocolVersion.ts` and `src/lib/binaryFrame.ts`): everything of
  section 3 — `PROTOCOL_VERSION = 3`; describe params and the new result fields; `Target`; tick fields (`stopId`,
  `reason`, `registersStale`, `notice`, `categories`); `unreadable`; open enums; every method in `RpcMethod` and
  `DebuggerRpcMethodSchemas`; `IN_BAND_ERROR_METHODS`, `DebuggerErrorCode`, `PROTOCOL_BOUNDS`, `CAPABILITIES`,
  `STOP_REASONS`. Two sets of schemas are built from one definition: the client schemas (objects strip unknown keys, v3
  fields optional, open enums are strings) and `ConformanceSchemas` / `StrictDebuggerRpcMethodSchemas`, the strict copy
  for the contract runner (unknown keys fail, v3 fields required, open enums closed, numbers bounded).
- **Client plumbing**: global timer functions instead of `window.*` in `jsonRpcClient.ts` and `transport.ts` (the code
  runs under node); pending requests are rejected when the socket closes; a connect failure produces a readable message;
  a dropped tick (validation failure) is reported through a client callback with the zod path;
  `DebuggerClient.describe` sends `{protocol, client}`; binary-frame handling of 3.4.
- **Mock**: the protocol core is a module, `server/mockDebugger.ts` (no HTTP, no WebSocket, no Vite in it);
  `server/mockWebSocket.ts` puts it on a socket and `server/mockServer.ts` (Express + Vite) adds the page. It implements
  v3 around a fabricated machine: the fields and rules of section 3, a 200 ms running tick with every fifth full,
  `stopId` / `reason`, `categories`, lifetime states (`setLifetime`), the error table, bounds, the binary header, the
  memory map of 2.7. It is deliberately "M1 plus": besides milestone M1 it steps the ARM7 and the DSP, records SGC
  frames and writes registers, as the mock did before v3. Options (`MockDebuggerOptions`; on the command line
  `--start-paused`, `--force-fallback`, `--stale-registers`, `--m1`): `startPaused`, `forceFallback`, `sliceContext`,
  `supports` (targets, stepping, capabilities; `MOCK_M1_SUPPORT` is what an M1 emulator implements), `tickMs`,
  `fullTickEvery`, `graceMs`, `protocol`, `ui`.
- **Bundle info**: every build writes `bundle-info.json` (2.11).
- **Contract runner**: `scripts/contract-test.ts` with `scripts/contract/` (`scenarios.ts`, `invariants.ts`,
  `session.ts`, `run.ts`, `design.ts` — the tables of 3.1 and 2.8 —, `disasm.ts`, `elfSymbols.ts`).
  `npm run contract -- [--url ws://host:port/ws] [--profile mock|emulator] [--elf <path>] [--milestone M1|M2|M3|M4]
  [--require <ids>] [--only <ids>] [--timeout-factor <n>] [--json] [--json-file <path>] [--verbose] [--list]`, and the
  helper mode `--idle-breakpoint <address|symbol>` of 5.7. Without `--url` it starts the mock module in-process (no
  Vite); with `--url` the profile defaults to `emulator`. It uses `JsonRpcClient` + `WebSocketTransport` directly (not
  the stores), taps the transport **before** it sends `debugger.describe`, uses the `ws` package for the raw cases
  (malformed JSON, `Origin` / `Host` headers), validates every response and tick with the strict schema, checks the
  invariants on every tick of every connection, runs the scenarios of 5.5 and cleans up after itself (5.5.2). `--elf`
  gives it the entry point and the symbol table of the test program (a small ELF32 `.symtab` reader).
- **v2 compatibility**: the client keeps working against a v2 server, as delivered; it does so until F3, which drops
  it (3.0, 4.4).
- **Tests** (vitest): `jsonRpcClient`, `WebSocketTransport` against a mock WebSocket, `BroadcastChannelTransport`, schema
  samples for both protocol versions and both schema sets, binary header parsing, the mock module in-process, and the
  runner itself against conforming and broken variants of the mock (`scripts/contract/run.test.ts`).
- Gate: `npm run check`.

#### 4.2.1 Devtools round: the runner and the mock after revision 3 (done)

**Done.** Delivered at `11d4136` of branch `skmp/nulldc3` of `dreamcast-devtools`: all thirteen runner items and every
mock sentence below; 382 unit tests; the M1 gate on the mock ends with 16 scenarios passed, 0 partial, 0 not
applicable; the runner judged 278 broken or legal servers correctly. What follows is the record of the round. Its
verification left points that decisions 30 to 37 settled; what they change in the runner and the mock is 4.2.2.

The runner is the gate of the emulator's server, so this round was closed before the core package (E5) was verified.
Every item is a decision of revision 3 or a finding of its check; none changes the shapes of the client schemas.

Runner (`scripts/`):
1. Gate semantics of 5.5.3: `--milestone` selects and requires; a run in which no scenario passed exits non-zero;
   `--only` never turns an empty result into a success.
2. Error messages are not matched any more: the code, and a non-empty `message` (3.5). `DEBUGGER_ERROR_MESSAGES` (the
   patterns "a v3 server sends with each code") leaves `debuggerSchema.ts`, and with it its uses in
   `scripts/contract/session.ts` and `scenarios.ts`; the mock keeps its texts privately, and its unit tests and those
   of the schema check codes.
3. S01: the normative columns of the target table only (not `label` and the default addresses); under a milestone gate
   all three targets and the milestone's capabilities; of the tree: the M1 paths, their parents, their actions, the
   three anchor registers, the register list of `dc.sh4.cpu`, and `dc.holly.core.vblank` among the events (2.8). (The
   widths of `dc.aica.dsp`, and `ENABLED` of `dc.aica.arm7` as a fourth anchor, were added to what is normative
   afterwards: 4.2.2.)
4. S02: the repeated pause once per target id, listed or not, and one other string (`-32602`).
5. S07: the re-enable construction at the interrupt entry (5.5), with `VBR` read at a stop the scenario makes after the
   target has run, never at the anchor of S00; S20 reads it the same way.
6. S11: `callstacks.arm7` is `[]` (ARM7 held in reset) or at least two frames, frame 0 the PC and frame 1 `R14` with
   location `lr`. (Which of the two it has to be is told by the register `ENABLED` of `dc.aica.arm7` since decision
   31: 4.2.2.)
7. S12: the `-32600` probes with the `id` rule of 3.0 (a malformed message without an `id` is answered, with
   `id: null`); a well-formed notification gets no answer; a request with an unknown
   top-level member and an unknown `params` key is answered as without them (sent raw, since the client's own request
   type has neither); the 1 MiB probe; one `-32004` probe of an unadvertised method is sent with a missing parameter
   as well (still `-32004`: 3.0, which error wins). The runner's own tests make that probe against the mock
   configured as an M1 server (`MOCK_M1_SUPPORT`), where the unadvertised method has a handler.
8. S19, S20: the running target.
9. Invariants: I5 with four base paths; `eventLog[].timestamp`; a `breakpointId` is a key of `breakpoints` in every
   tick that carries one (I7), not only in the first tick of a stop; the check `reason "breakpoint"` / `"event"`
   without a `breakpointId` goes (2.3.1: after its breakpoint is removed the reason stays and the id leaves).
10. The `(mock: PC)` branch of S03 and the "Emulator:" markers that no longer apply go.
11. The strict copy bounds the `state.getCallstack` result to 64 frames, refuses an `address` on an event
    breakpoint and requires `sp` in a callstack frame (3.2).
12. S03: the third watch is on a register that has a value at the stop before the resume (2.8: some registers are
    `"n/a"` always).
13. S05: after `breakpoints.remove` of the reported breakpoint a paused tick of the same stop (same `stopId`,
    `reason: "breakpoint"`) carries no `breakpointId`. S10: the `bytes` of a `dsp` line are 8 pairs (as built the pair
    count is skipped for `dsp`).

Mock (`server/`): `breakpointId` leaves the ticks of a stop when its breakpoint is removed; `dc.aica.dsp` widths of 2.8;
the `dc` node and the event table drop `dc.debugger.dsp_forced` (2.6: a server lists the events it can raise);
`control.pause` accepts every target id and refuses other strings with `-32602` (it does already); with `startPaused`,
leaving `no-game` parks with `reason: "start"` (it does already); a method whose capability the mock was configured
without (`supports.capabilities`, `--m1`) answers `-32004` before its parameters and the lifetime are looked at, as
its methods without a handler already do (3.0, which error wins; as built it parses the parameters and checks the
lifetime first); and its fabricated SH4 takes an interrupt at every vblank of its
machine — the PC leaves the loop for a handler at `VBR + 0x600` that ends in `rte` and comes back — so that the mock
passes S07 and S20 the way a program does (5.5.3): one construction for both servers, and the mock's disassembly gains
a handler with an `rte`, which the UI tests can use.

`package.json`: `check` runs the contract as `contract -- --milestone M1`, and `build:wasm`.

#### 4.2.2 Runner follow-up (done)

**Done.** Delivered at `22b358e` of branch `skmp/nulldc3` of `dreamcast-devtools` (ten commits on `17f3bff`): the six
runner items and the mock change below, each with a test of the runner's own that goes red when the item is taken
back; 391 unit tests, 246 e2e tests, and the M1 gate on the mock as before, 16 scenarios passed, 0 partial, 0 not
applicable. Against the emulator the gate passes with these changes on all three engines (5.4). What follows is the
record. Where the list was silent the follow-up chose, and each choice is a rule now, stated here with its item and
in the row or section it belongs to (5.5: S04, S06, S07, S11, S16, S18; 5.5.1; 5.5.2).

What the decisions made after the round (30 to 37) change in the runner and the mock. It was one small piece of work
on `skmp/nulldc3` of the UI repository, done before the gate was used against the core (E5).

Runner (`scripts/contract/`):
1. S04 (decision 30): a second step in a row that ends at an exception vector fails the scenario. The four attempts
   and the result "passed with a note" go. As built: two steps in a row at a vector fail whichever steps of the
   scenario they are, a `control.stepOver` among them; a single `control.stepOver` may end at a vector; and after a
   `control.step` that ended at a vector the step from the next plain line has to show PC + 2.
2. The register `ENABLED` of `dc.aica.arm7` (decision 31), when `arm7` is listed. S01: the register is there and
   has width 1. S11: the pairing inside one paused tick, in both directions — `ENABLED` reads 0: `callstacks.arm7`
   is `[]`; `ENABLED` reads 1: at least two frames, frame 0 the PC and frame 1 `R14` with location `lr` — and the
   answer of `state.getCallstack {arm7}` at that stop agrees with the register in the same way (asked with
   `maxFrames: 8` and without). As built: S11 fails, and not only S01, when the register is missing or reads
   neither 0 nor 1 (`"n/a"`); where the register stands in the list is not checked.
3. S01 (decision 32): the width of each of `STEP` (8), `MDEC_CT` (16), `RBP` (16), `RBL` (8) that `dc.aica.dsp`
   lists.
4. S06, S07 (decision 34): the helper behind "only running ticks for 600 ms" (`expectToKeepRunning`) requires that no
   paused tick comes in the window and nothing else; its demand for two ticks inside the window goes. As built: the
   running tick of the resume itself is still awaited, within 1 s.
5. S18 and S16 (decision 36). S18: the `icache` and `ocache` tables have rows, or no rows and a `note`; as built,
   rows together with a note pass as well. S16: an ARM7 step asserts the new `stopId`, the `reason` and that no
   running tick came in between; the PC is compared only on a line that is not control flow, as S04 does. As built:
   the runner compares and does not search for a plain line, so on a control-flow line nothing is compared; on a
   plain line the step ends at PC + 4 or at an ARM exception vector (`0x00` to `0x1C`: a pending FIQ is entered
   before the break test, 2.5.3).
6. A server that stops answering (decision 37): after several request timeouts in a row the runner fails the
   remaining scenarios as "not run" instead of sending each of their requests into its timeout. As built: "several"
   is three, counted over all connections of a run; an answered `debugger.describe` does not end the row, any other
   answered request does; behind the third the runner sends nothing at all, the clean-up included (5.5.1, 5.5.2).

Mock (`server/mockDebugger.ts`; decision 31): `dc.aica.arm7` gains the register `ENABLED`, width 1; it reads 1 while
the mock's ARM7 runs. As built it stands behind `PC`, and `state.setRegister` and `state.editWatch` on it answer
`-32004`; that becomes `-32013`, the code of a register without a writer, with F3 (4.4, 2.8). The mock has no "held
in reset" mode: the runner's tests patch one in.

No runner change comes from decision 33 (the runner does not look at the registers of second-level nodes) nor from
decision 35 (the `state.getMemoryBinary` part of S17 and the mock's method are F4's, 4.5).

### 4.3 F2 — client behaviour and panels (M1; done)

What the UI does, as built and verified against the mock and scripted servers; it is the contract for later packages.

- **Store** (`src/state/`): the tick handler is registered before `debugger.describe` is sent; `registers` merged per
  path (`tickState.ts`), with stable identities for what a tick did not change, so that a tick re-renders only what it
  changed (measured: 2500 registers with every value new at five ticks a second, no long task with the panels open);
  nothing of a connection is kept for the next; `stopId` tracking (follow / refetch / toast only on a new stop; when
  stops arrive faster than the page renders them their ticks are all applied and the views move and read for the
  latest only); with `reason: "no-game"` every register value is dropped, and the views read again when the reason
  leaves it; category state taken from the tick, and the category client registered centrally; watch expressions
  lower-cased on both add paths; `breakpoints.setCategoryStates` always sends all four keys.
- **Describe.** A `debugger.describe` that failed (the emulator was busy, did not answer within 10 s, or sent a result
  the schema rejects) is sent again on the same connection after 1, 2, 5 and then every 10 s, with "Retry now" in its
  notice; until it is answered the panels show no data. A server of another protocol version, a v2 server included, gets
  the banner of 3.0.
- **Endpoint and reconnect** (`sessionStore.ts`, `sessionController.ts`): `?ws=` is read from `window.location.search`
  (before the `#`; the app uses `HashRouter` and the memory panel rewrites the hash query). The endpoint is the first of
  `?ws=`, the last manual entry (localStorage), `VITE_WS_URL`, same-origin `/ws` that is a `ws://` or `wss://` URL: a
  priority, not a fallback chain — **an explicit endpoint is retried and is not replaced by the next one**. Picking the
  build default or the page's own endpoint in the connection picker puts a remembered manual entry away; `?ws=` is never
  cleared. **Reconnect**: on a lost connection, or one that cannot be made, pending requests are rejected, the state is
  `reconnecting`, and the client tries again after 1, 2, 5, 10 s — each delay counted from the failure before it — and
  then every 10 s; a first attempt that fails is retried the same way; **any connection that opens resets the
  sequence**; `error` is only for an endpoint that cannot be tried; "Stop" ends the retries, "Connect" and "Retry now"
  try at once. Every connection that opens is described again. In wasm mode a reconnect waits for a discovery
  announcement instead of a clock: the same id if it announced itself after the loss, else the only emulator that
  announced itself after the loss, else it keeps waiting; an announcement from before the loss does not count. A
  refusal by the Origin / Host check is not visible to script; the error text lists what a failed connection can be,
  that check among it.
- **Without a connection** the panels keep their frame — search boxes, toolbars, the focus in them — and show
  "Not connected" in place of data; only data is cleared. **Text typed into a box and not sent is frame and is kept;
  an address box is rewritten only when its view moves or the target's address format changes.** Over a reconnect
  the focus is kept in static controls, in tree nodes and in list rows (the enable switch and the Remove button of
  a breakpoint row, the Remove button of a watch row). **A row control gets the focus back only while the focus is
  on the page body: a focus the user moved during the outage stays where they put it.**
- **Toolbar**: next to the run controls, `Running`, or `Paused` with the `reason` and the `stopId`, and below it the
  `notice`; the line wraps where the toolbar has little room, and a window narrower than 1280 px shows both in lines of
  their own under the toolbar. The results of all `control.*` calls are checked and a refusal is shown; **a refusal
  that repeats while its notice is on screen is counted in that notice (`×n`), not shown again** (the count stays five
  seconds from the last repeat). In the stops of the emulator's own window (`host-stopped`, `guest-exit`, `no-game`) Run
  and the step buttons are off, because the server refuses them there; Pause stays. A run control that cannot be used
  keeps the keyboard focus (`aria-disabled`, not `disabled`). How the controls of a toolbar are aligned is not
  contract.
- **Disassembly**: follows the PC and refetches when `stopId` changes (and after the panel's own breakpoint edits,
  without moving). "Follows" means: after every stop — the one a client connects into included — the PC line is on
  screen, with up to ten instructions of context above it, fewer when they do not fit; a view whose PC line is already
  on screen does not scroll; a view that is a dock tab behind another follows when its tab is shown. The first address
  is the PC of the first tick of the connection, also while running, and `defaultDisassemblyAddress` only when that
  tick carries no PC (stale registers, no game); a panel opened later uses the latest tick; after a reconnect or an
  endpoint switch the views keep their place unless the new connection starts in a stop. Target metadata
  (`instructionSize`, `addressMax`, radix, digits, defaults) comes from `targets`; nothing is requested beyond
  `addressMax`. The gutter adds and removes breakpoints; the server keeps two breakpoints for two adds on one address
  (2.3.3), so the client is the one that guards against a double click (4.3.1). **Narrow panels: the instruction
  column keeps a minimum of 12 characters; a listing narrower than its columns scrolls sideways (wheel or scroll
  bar) and cuts nothing.** That holds for any panel width, also for a layout made in a wider window and narrowed.
  The view's toolbar wraps instead of cutting its controls off.
- **Memory**: refetches when `stopId` changes; opens at `defaultMemoryAddress` of the first server that reads that
  memory; unreadable bytes shown as `??`, and a window in which nothing is readable (`-32010`) as all `??`, so that the
  view can be scrolled out of it. The rule for narrow panels is the disassembly's: a listing narrower than its
  columns scrolls sideways and cuts nothing. As built the memory view cuts its hex and text columns in a
  side-by-side layout that was narrowed; it gets the rule in package F3 (4.4).
- **Default layouts** are readable in a 1366x768 window: no panel of a default layout loses a column there, and e2e
  runs at that window size pin it (4.3.1 item 7).
- **Unsupported targets**: a panel whose target is missing from `targets`, or whose `stepping` / `memory` is false, shows
  a notice instead of the corresponding controls; so do the SGC panel without `"sgc-capture"`, the callstack panels
  without `"callstack"` and the breakpoint panels of a target without `stepping` or without `"event-breakpoints"`. No
  request that would be answered `-32004` is sent. `Target.memory: false` is pinned with a scripted server: no real
  server has a target without memory that owns a memory panel.
- **Device tree panel**: one memoised component per node that selects `registersByPath[path]` itself; expansion is
  controlled state that survives search and ticks; a search expands at most 200 register rows; `dc.sh4.cpu` is dimmed
  while `registersStale`; the description is shown for nodes with and without actions. The watch autocomplete limits
  its list to 50 matches.
- **Breakpoints**: the category and the panel of a breakpoint come from `targets[].breakpointEvent`; a target's panel
  lists only its own code breakpoints, the events panel the event breakpoints. Address inputs, lists, the hit toast and
  the reason line use `addressRadix` / `addressDigits` of the target as `targets` describes it, not built-in values (DSP
  steps are decimal everywhere). A stop whose `breakpointId` is not in `breakpoints` is announced as `#<id>`.
- **Event log**: shows `metadata.event` and `metadata.cycles` (an entry without `metadata`, from a v2 server, without
  the two fields; that case goes with F3, 3.0).
- **Error boundaries**: each dock panel and each route is wrapped in an error boundary that shows the error and a reload
  button; an unknown panel id in a saved layout or in `actions` is ignored. Validation failures are surfaced in the UI,
  including the dropped-tick case with the zod path.
- **Callstack**: frames labelled with `location` (`pr`, `lr`, `stack+..`) and a note that the unwinder is heuristic.
- Tests: vitest for the merge and `stopId` logic of the store, the reconnect state machine (both builds), the endpoint
  order; the e2e suite for everything above. **What is taken from `targets` is pinned by a test against a scripted
  server whose targets differ from the table of 3.1** — a client that uses its built-in literals passes any test against
  a conforming server.
- Gate: `npm run check`; the e2e suite; the contract gate M1 against the mock and against the emulator.

#### 4.3.1 Devtools round: the panels after revision 3 (done)

**Done.** Delivered at `17f3bff` of branch `skmp/nulldc3` of `dreamcast-devtools`: all seven items, item 7 in this
round and not in F3; 246 e2e tests and 382 unit tests pass. What follows is the record of the round ("as built" in it
is the state before the round); what the UI does now is 4.3. Decisions 38 to 41 answered what the verification of the
round left: the width rule of the disassembly, and that the memory view gets it in F3 (38); what "frame" is (39);
a focus the user moved during an outage (40); toolbar alignment (41).

1. **Without a connection the panels keep their frame** — search boxes, toolbars, the focus in them — and show "not
   connected" in place of data; only data is cleared. (As built the whole store is emptied and every panel says
   "No Data"; the device tree loses its search box.)
2. The stop a client connects into is followed: the PC line is on screen (as built the view opens ten instructions above
   the PC and does nothing more).
3. The hit toast and the address box of the breakpoint panels are tested against a scripted server whose radix and
   digits differ from the built-in ones (the lists are already).
4. The run and step buttons and "Go to current PC" have accessible names (the tooltip's label is on a wrapping span
   today).
5. Over a reconnect the focus is kept in list rows too (the enable switch and the Remove button of a breakpoint row, the
   Remove button of a watch row), as it is for static controls and tree nodes.
6. Note-level: a `state.getDisassembly` answer that repeats an address leaves no stale row; a second gutter click while
   the first add is unanswered sends nothing.
7. **The default layouts are readable in a 1366x768 window.** As built the default three-dock layouts are unreadable
   at 1280 to 1440 px width (the instruction column of the disassembly collapses), which is what laptops have.
   Required: at 1366x768 the disassembly keeps a minimum width for its instruction column, the toolbar wraps instead
   of cutting its controls off, and no panel of a default layout loses a column; one e2e run at that window size pins
   it (the instruction text of the PC line and every run control are visible). Placement: this item is part of this
   round if it fits; if it does not, it is F3's (4.4), and the round's hand-over says which. (It fitted: the round
   delivered it, with a minimum of 12 characters and a sideways scroll: 4.3.)

### 4.4 F3 — M2 panels (done)

F3 is four packages, F3a to F3d (`M2-ROUND.md` 3.1, 3.7 to 3.9), and the leftovers of their verifiers, EXu (3.12):
F3a the contract of M2 (schema ids, the mock as an M2 server, the runner's M2 rules and gate), F3b the end of protocol
v2, F3c the register panels and register and memory editing, F3d the toolbar, reset and hotkeys with a smoke spec
against the emulator. All verified; the M2 round's gate (M2G) ran the whole UI gate again. The emulator's gates ran
with the UI checkout pinned at the last verified UI head, a commit the integrator names (`M2-ROUND.md` section 4), not
with the UI repository as it stands. What they do:

- One generic register-table panel: new panel ids `sh4-registers` and `arm7-registers` (in `PANEL_IDS`,
  `PanelIdSchema`, `PANEL_REGISTRY`, `mainTabs` and the default workspace layouts; a saved layout gains them only when
  the user resets it; the two stand in the centre group of their processor's workspace, behind the breakpoints panel),
  and the ten peripheral ids (replacing the "Coming Soon" panels). A register panel finds its node by `actions` (the
  first in the order of the tree if several offer its id; an M2 server has exactly one), shows a notice when no node
  offers it, takes its rows, widths and `flags.writable` from the describe node and its values from
  `registersByPath[path]`, and edits a register with the flag through `state.setRegister`. The M2 tables that have no
  panel id are read in the device tree.
- Memory view edits bytes through `state.writeMemory`; watches edit through `state.editWatch`: a watch has an editor
  when its expression names a described register with the flag (the flag is read from the describe tree, since ticks
  carry none). All three editors are offered while paused only. After its own memory write, register write or reset
  the client reads its fetched views again in place (a second counter beside `refresh`). Other clients learn nothing
  more than the tick.
- The views that follow the PC are moved by no action: they move when a paused tick of the stored stop shows another
  PC, whoever wrote it and in whichever order response and tick arrive, and at the new stop of a paused reset.
- Toolbar: run / pause / step / over / out for the **focused target**, and reset; "Run to next breakpoint" removed (it is
  identical to Run). The focused target is that of the panel of a target (disassembly, memory, breakpoints, callstack,
  registers) that last had the focus — a click on a tab counts as focus —, at first the workspace's own (`sh4` for
  `sh4-debugger` and `mixed-mode-debugger`, `arm7` for `arm7-debugger`, `dsp` for `audio-debugger`); the toolbar says
  which it is, and the views keep their own step buttons, which are off with the toolbar's while the ARM7 is held, for
  the ARM7's views.
  Pause names the focused target only when the server lists it. Reset has no confirmation and is shown, off, with its
  reason, on a server without the capability. Hotkeys F5 (run), F6 (pause), F10 (over), F11 (step), Shift+F11 (out),
  with F8 and Shift+F8 accepted as alternatives to F11 and Shift+F11; handled with `preventDefault` on workspace pages
  wherever the focus is, but for Monaco and an open dialog; a held key is not repeated. A widget of a code editor is the
  editor's and no dialog of the page, open or closed (Monaco's Find box has the dialog role and stays in the document
  when it is closed); a dialog is open when it is on screen, also when it carries `aria-hidden`, and a dialog in its
  exit transition counts as closed (F5 right behind Escape is the toolbar's). The run state stands under the toolbar
  below 1440 px and the controls on a line of their own below 1200 px. (4.4's first text, "outside an input", was
  rejected: F5 in an address box would reload the page.)
- ARM7 controls enabled by `targets[arm7].stepping`, and off while the ARM7 is held in reset (`ENABLED` 0). DSP
  controls enabled by `targets[dsp].stepping`, whatever `ENABLED` of `dc.aica.arm7` reads: the DSP is clocked while the
  ARM7 is held (2.5.4; F4d of the M3 round).
- Editing from the keyboard (F3c, EXu): a value of a register the server writes, a watch value, a watch's expression
  and one byte place of a memory listing are Tab stops with `role=button`; Enter, the space bar and F2 open the editor,
  and in the hex column the arrow keys move the place (an arrow never takes the place to a row the panel cuts off). The
  focus returns to the value or byte only when the editor went with the focus in it. A memory view that moves, and rows
  replaced by an error, put the byte editor away; no byte editor opens while the slice of a move is being read. While the
  target runs the value and byte controls stay focusable, with `aria-disabled`, and open nothing. An editor goes when
  the target runs again, with its draft; an answer that arrives for a write whose editor was reopened with other text
  does not close the new editor.
- Runner, with this package: the pairing of `dc.aica.arm7` `ENABLED` with `callstacks.arm7` (2.10) is an invariant on
  every paused tick, not only S11's stop, and S09 has a probe across the target's `addressMax` (the no-wrap rule of
  2.7).
- **Protocol v2 compatibility is dropped** (3.0; F3b, by the table of `M2-ROUND.md` 3.7): a `debugger.describe` result
  without `protocol` gets the version banner and is treated as listing no target and no capability. Removed: the
  literals fallback for absent `targets` (3.1), the running -> paused stop rule for an absent `stopId` (3.2), headerless
  binary frames (3.4), the assumption of every target and capability, the event-log row without `metadata` (4.3), the
  branches of the panels that serve them, and the v2 schema samples and e2e tests — but for one test, against a
  scripted server without `protocol`, that pins the banner. F3b decided where 3.7 is silent, and the integrator
  accepted: a code breakpoint whose event belongs to no listed target is listed with the events, its address as plain
  hex (`0x` and the digits, not padded); the lists `targets` and `capabilities` are read for any present `protocol`,
  only an absent one leaves them unread (3.0 said "only when protocol >= 3"; no server sends a lower number). A register
  value is read as hex with `0x` only, as every v3 server sends it (EXu; the emulator writes every value as `0x` and the
  digits, `regs::FormatValue`).
- The memory view: a listing narrower than its columns scrolls sideways (wheel or scroll bar) and cuts nothing, at any
  panel width, also in a layout made in a wider window and narrowed; the listing itself is not a box the browser
  scrolls (`overflow: clip`). In a panel that shows fewer rows than a slice holds the view stands so that the last row
  of an address space is the panel's last whole row, and what it reads there ends with the address space in as many
  rows as the panel shows (EXu), so the end of every address space can be reached with the wheel and the keyboard.
- The contract runner's S16 checks the M2 rows of the device tree (2.8) as S01 checks the M1 rows (5.5).
- The mock is an M2 server: `state.writeMemory`, `control.reset`, the M2 rows, an ARM7 that a reset holds and the run
  releases, the three effects the write rules look at (the `RB` exchange, the step from a PC outside its ring, the
  written `mov #imm,Rn` that executes), stores (main memory 16 MiB; wave memory 8 MiB; video memory in two views that
  are not aliased) and a `bl` to a leaf in its ARM7 ring, with step-over and step-out as temporary breakpoints; and a
  profile, `MOCK_M2_SUPPORT`, that can do what the M2 emulator can and no more (it writes the 109 registers of
  `dc.sh4.cpu` and `dc.aica.arm7`, steps two targets and captures nothing). `npm run check` runs its M2 gate on the full
  mock and on the mock with `MOCK_M2_SUPPORT`. The mock answers `state.setRegister` (and `state.editWatch`) on
  `dc.aica.arm7` `ENABLED` with `-32013`, as for every register without a writer. Where mock and emulator differ in M2:
  the widths of `L` and `M` of `dc.aica.common` (3 bits in the mock, 8 in the emulator).
- The smoke spec `e2e/emulator-smoke.spec.ts` runs the UI against a headless emulator on the sounding guest (F3d's gate
  and the round's gate): the start stop, a breakpoint at the program's entry, SH4 steps from the toolbar and by key, R0
  edited in the register panel and seen in the tick, two bytes of memory written and read back, the reset, and ARM7 steps
  behind a resume. The emulator has to carry the bundle of the checkout (`ui` of `debugger.describe` equals the version
  in `dist-native/bundle-info.json`, all 30 files served byte for byte), so after every UI commit `npm run build:native`,
  the emulator's own configure with `-DNULLDC_DEVTOOLS_DIST` and its build are made again, in that order. Every run has
  its own `HOME`, `XDG_CONFIG_HOME` and `XDG_DATA_HOME` (without them the emulator reads the user's `emu.cfg` and opens
  the user's memory cards), and whoever starts the emulator ends it by pid (the server releases the stop 10 s after the
  page is gone, and the guest then runs on one core).
- The embedded bundle is about 5 to 6 MB, about 1.4 MB gzip-compressed, which is how it is embedded.
- The UI is measured against a paused tick of about 165 KB (the sounding guest with a full event log, 2.9): the tree of
  3,000 registers at five ticks a second is an e2e test.

### 4.5 F4 — M3 audio (done)

F4 is four packages, F4a to F4d (`M3-ROUND.md` 3.1, 3.4, 3.6, 3.10), on the UI branch one after another; the client was
done in F1 (`fetchSgcFrameData` per 3.4, `recordSgcFrames(frames)`), and so was the mock's record. All verified; the M3
round's gate (M3G) ran the whole UI gate again. What they do:

- **F4a, the contract of M3**: the capability string `"dsp-forced"` left the schema and the describe fixture (2.5.5);
  the mock is an M3 server — `state.getMemoryBinary` with the capability `memory-binary` (a frame of kind 2 to the
  requester only, `binaryId` from the connection's counter that kind 1 shares), `MOCK_M3_SUPPORT` (`--m3`, the runner's
  `--mock-support m3`: what the M3 emulator does and no more, no device write), an AICA clocked whatever its ARM7 does
  (its DSP steps, its breakpoints are reached and its captures record while `ENABLED` reads 0; only the ARM7 answers
  `-32012`), one DSP program (register file, disassembly bytes and text agree), the thirteen rows of `dc.aica.dsp` and
  the three rows of 0x2804 in `dc.aica.common`, the emulator's sound clock (an AICA period of 145124 cycles and 32
  samples), a read of 16 MiB within 2 s, the capture's rules of M3-D27, and, under `--dsp-program`, a start paused with
  its ARM7 held as the guest's anchor is (M3-D31) —; the runner's M3 rules (S01, S10, S17, S21, 5.5) with a broken
  scripted server for each; `npm run check` runs the M3 gate (5.5.3).
- **F4b, the playground on the one DSP core**: the module is the emscripten build of the emulator's C++ core
  (`libswirl/hw/aica/dsp_core/wasm/build.sh --check`), committed in `src/wasm/aica-dsp-core/` (`aica_dsp_core.wasm`,
  `aica_dsp_core.mjs`, a hand-written `aica_dsp_core.d.mts`, `PROVENANCE.json` with the emulator's head, the core's last
  commit and the pinned emscripten version, `NOTICE.md` with caique's MIT text) and regenerated by `npm run build:dspsim
  -- --emulator <tree>`, which refuses another emscripten unless asked (`--emcc-version`) (M3-D34). `src/lib/aicaDsp.ts`
  takes the core's interface (steps in order, `run` with stops, the view, `get` / `set` of the core's registers, the
  typed views of RAM, TEMP, MEMS, MIXS and EFREG) and type-checks in every TypeScript project that includes it; the
  inputs are at the console's scale (`sample << 4`), so a program sounds the same in the playground and in the
  emulator. `crates/aica-dsp` (Rust) and wasm-pack are gone; the UI builds without Rust and without emcc. A vitest runs
  the native core's known answers (`known-answers.json`) on the committed module, and **the emulator holds the module
  equal to its native core** (`aica.dsp.wasm.devtools`, W2, on the UI pin).
- **F4c, the SGC panel on the v3 contract**: `AudioPanel.tsx` takes the frame count from the result, clamps the playback
  position and the hover when a shorter capture arrives, has an empty state for `frameCount == 0`, reads "active" from
  bit 0 of byte 109 (not from `aeg_value`), takes `-32011` as "no capture yet" without logging it; the constants are
  those of `binaryFrame.ts`; the waveform views clamp every index and read a view on the buffer in place of a copy per
  frame and channel (a capture of 4096 frames no longer freezes the page). Record asks for `{frames: 1024}`; a "Capture
  length" label stands in the panel's top bar.
- **F4d, the Audio Debugger against the emulator**: **"Load from emulator"** (M3-D35; `src/app/dsp/emulatorSnapshot.ts`)
  works at a stop, in a workspace's dock panel: one ARM7 read of `0x00808000` bytes from 0 through
  `state.getMemoryBinary` (wave memory and the register file of one moment) and the rows of `dc.aica.dsp` and
  `dc.aica.common` of the paused tick of the same `stopId`, loaded into the playground's core; the assembly source is
  regenerated as "Load Regs" does; a banner names `<emulator.name> @ <host>`, the `stopId` and the tick's time, and goes
  when the program it describes is replaced. The load continues the emulator exactly from a stop in the DSP phase
  behind step 0 (`STEP` 1 to 127) up to the end of that sample; at any other stop the banner says that MIXS of the next
  sample is not known, and at every stop that EXTS is loaded as 0. While the target runs the button is off and says
  why; the tool route keeps no connection. **The DSP's controls whatever the ARM7 does** (M3-D7): which target is held
  is decided in `runControls.ts` (`heldFor`: the ARM7 alone), and the toolbar and the DSP disassembly offer the DSP's
  step and breakpoints while `ENABLED` of `dc.aica.arm7` reads 0. **The help on DSP breakpoints**: a DSP breakpoint is
  reached in every sample; to let the SH4 run on, disable or mute it (also in the README's "Breakpoints").
- **The smoke spec's M3 part** (`e2e/emulator-smoke.spec.ts`, against a headless emulator on the sounding guest with the
  bundle embedded, 31 files at F4d's head): a DSP breakpoint and steps from the toolbar; a DSP step at the guest's
  anchor, where its ARM7 is held (`ENABLED` 0), from the toolbar and from the DSP disassembly; a capture with a sounding
  channel shown active; "Load from emulator" at a DSP breakpoint, then k steps in the playground equal to k steps of the
  emulator in the nine rows and in the TEMP, MEMS and EFREG words read back; the banner of a load at an SH4 stop. Its
  timeout is 420 s; a run takes about 25 s.
- Known limits (F4d's verifiers): the SGC panel's default view (Pre-VolPan) is flat against the emulator, since the
  `fast` tier leaves bytes 76 to 83 of a record at 0 (3.4); the smoke spec reads Post-VolPan (bytes 84, 86). The
  playground has no sound generator: across step 0 its inputs are its own (the banner says so). On the guest `_start`
  releases the ARM7 in front of the first tick, so the smoke spec's two held DSP steps are made at two entry stops: it
  shows the step offered and accepted while `ENABLED` reads 0; that the step executes with the ARM7 held is the
  emulator's `dsp.step.held` (5.3).

### 4.6 F5 — M4 Holly, and F6 — M5

- F5: the CORE inspector renders the parsed shading fields and primitive previews (port from `refsw2-logview`) and fetches
  its log with `state.captureHolly {core}`; the TA inspector is defined on the `TAFRAME4` capture; table panels on
  `state.getTable` (the cache tables show lines when the tier has a cache model, else the server's `note`); buttons in
  both inspectors that add the event breakpoints `dc.holly.core.render_start` / `dc.holly.ta.list_end`. The PVR TA and
  CORE debuggers that PLAN track 10 names for graphics debugging and dumping are planned from here; M4 is their
  placeholder.
- F6: the SH4 simulator panel on the measured model, with the fixture gate.

## 5. Verification

Rules for every run in this section (D10): every emulator run is headless (`config:Headless=yes`,
`docs/nulldc3/automated-runs.md`): no window, no display needed, no host input; it has an environment of its own (fresh
`HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`), the real-time clock pinned, a core size limit of 1 byte and a watchdog, as
`tests/system` does it. Agent runs keep to the machine limits: one workflow at a time, `ninja -j8`, one build at a time,
`ctest -j4`, at most 4 emulator processes, no load generators, one sanitizer configuration at a time. `config:Headless`
exists on Linux, Windows and macOS since the platform round (D10): on Windows every program of an automated run runs
inside `nulldc3-winrun` (a private desktop, a job, the error mode), on the Mac every emulator run goes through
`tests/macos/macrun.py` under its sandbox profile, and the harnesses start their programs through the shared run
helper `tests/common/emurun.py` (5.4). The only runs that may open a window are the window checklist of E4a
(`tests/debugger/selftest/window_checklist.sh`, not a ctest test) and the window items of 5.8, both the owner's by hand
(`PLATFORM-ROUND.md` PL-D40).

### 5.1 devtools

`npm run check` (lint, typecheck, vitest, the contract gate on the mock — M1 until F3a, M2 since, on the default mock
and on `MOCK_M2_SUPPORT` —, both builds: 4.1), and `npm run test:e2e` (Playwright, headless Chromium; with at most 2
workers on the development machine). At the end of the devtools round: 382 unit tests, 246 e2e tests, and the gate on
the mock with 16 scenarios passed, 0 partial, 0 not applicable. At the end of the runner follow-up (`22b358e`): 391
unit tests, 246 e2e tests, the same gate result. At the end of the M2 round (`d59336b`, the round's
gate in a fresh clone): 585 unit tests passed and one skipped (it reads `2ndmix.elf` and is skipped without
`NULLDC_TEST_ELF`; with it, it passes), the M2 gate on the mock with 17 scenarios passed, 0 partial, 0 not applicable,
on the default mock and on `MOCK_M2_SUPPORT`; e2e 368 passed and the smoke spec skipped, as it is without
`E2E_EMULATOR_URL`; the smoke spec against the emulator passed.
- The unit suite of a UI package is run at least three times in a fresh clone, whole and in parallel as `npm run
  check` runs it: F3a's one red gate showed only there (2 of 3), never in a single test and never in the contract gate.
- The mock's SH4 executes on a free-running 16 ms timer, so a step falls into a short run now and then (7 of 100
  measured). A test never compares the mock's PC with a constant behind anything that resumes the target: it compares
  with the PC of the stop, or uses a machine that executes a fixed number of steps at each resume (`pausing(steps)` in
  `scripts/contract/m2.test.ts`).
- Not noticed by any test of the mock (F3a's verifier; notes for a later mock package): a tab around a value; a held
  ARM7 keeping a step-out's temporary breakpoint; `flags.writable` without `register-write`; a reset restoring the
  ARM7's registers only; `--m2` of `mockServer.ts` not reaching the mock.
- `e2e/emulator-smoke.spec.ts` is skipped without `E2E_EMULATOR_URL`; it is run against an emulator by
  `agents/nulldc3/f3d/scripts/smoke.sh` (4.4).

### 5.2 Pure-module tests (emulator)

Plain C++ executables under `tests/debugger/<package>/` of the emulator repository, each directory a small CMake project
with the tree's definitions and `-std=c++20`, and each also a part of the tests project (`tests/CMakeLists.txt`), where
its tests are named `debugger.<package>.<name>`. The pure set is `dbg_net.cpp`, `dbg_webui.cpp`, `dbg_disasm.cpp`,
`dbg_regs_tables.cpp`, `dbg_symbols.cpp`, `deps/crypto/sha1.cpp`; `tests/debugger/sh4bp` compiles the mechanism of 2.3.4
around a machine of its own.

| Directory | Package | Content | State |
| --- | --- | --- | --- |
| `tests/debugger/net/` | E1 | WebSocket framing and handshake vectors (RFC 6455 examples), fragmentation, the 1 MiB limit, the 16-client limit; HTTP path sanitising; Host / Origin rules of 2.11 including `null` and `localhost.evil.example`; MIME and cache headers; fallback and 404 rules; a send to a closed socket does not raise `SIGPIPE`; embedded-bundle lookup with a fixture bundle and the embed check; a fuzz test; interoperation with the UI's `ws` client | done |
| `tests/debugger/disasm/` | E2 | SH4 and ARM7: comparison with GNU objdump, PC-relative targets, the SH4 opcode table; DSP steps against the devtools playground's disassembler and assembler | done |
| `tests/debugger/regs/` | E3 | register-table sanity: unique names per node, unique paths, parents, addresses inside the module ranges, widths, events against `dbg_events.h` (every row is listed by exactly one node, the one it names; until header revision 7 removed it, `EV_DEBUGGER_DSP_FORCED` was the one row no node listed, decision 42); the panel ids against `PANEL_IDS` of the UI (37 from R6 on); the AICA channel state the debugger shows. With the M2 tree (E7b): the node list, 104 nodes with parents, labels and actions; names upper case, of `[A-Z0-9_]`, unique in their node; every system bus row a name and address of `sb_regs.h`, every PVR row a name and offset of `pvr_regs.h` (both headers read as text); the fields of one AICA word do not overlap and lie inside it; no M2 row writable, and the writable rows exactly the 69 + 40 of the two processors (`M2-ROUND.md` 3.6) | done |
| `tests/debugger/sh4bp/` | E4b | the gates of 2.3.6 on three engines; the selftest file on that machine; the cost gate B11 | done |
| `tests/debugger/symbols/` | E5 | `debugger.symbols.table`: the `.symtab` walk, lookup by address, `name` / `name+0x<offset>`, a symbol without a size, several symbols at one address, a symbol name with bytes above `0x7F` and with control characters (the table keeps the bytes; the wire form of 3.0 item 9 is tested by `core.tick`). `debugger.symbols.damaged`: damaged images (every offset checked against the size). The fixture is no file: the test builds its ELF32 images byte for byte and hands each over in memory that ends where the image ends, with an unreadable page behind it | done |

The tests project is configured with the data it can find; tests whose data is missing are skipped, and
`-DNULLDC_TESTS_REQUIRE_DATA=ON` makes that a failure:

```
cmake -S tests -B <build> -G Ninja -DNULLDC_ELF_DIR=<directory with 2ndmix.elf and hello.elf> \
      -DNULLDC_DEVTOOLS_DIR=<the UI checkout> -DNULLDC_TESTS_REQUIRE_DATA=ON [the console data options]
ninja -C <build> -j8 && ctest --test-dir <build> -j4
```

Without `NULLDC_DEVTOOLS_DIR` three debugger tests have no data (`debugger.net.interop_ws`,
`debugger.disasm.dsp_playground`, `debugger.regs.panels`).

`tests/debugger/net` got no further test from E5: the transport already puts the close frame with code 1009 for an
oversized message on the wire while the client is still sending (2.11), so the correction of `dbg_net.cpp` that 6.1
allowed E5 was not needed. S12 holds the behaviour in the gate.

### 5.3 In-emulator selftest

`nulldc3 --config Debugger:SelfTest=<filter> <game>` runs the registered tests on the emulator thread against the live
machine, prints one line per check and per test (`SelfTest: <test>: PASS, <n> checks` / `FAIL, ...`; last line
`SelfTest: <p> passed, <f> failed`), and ends the process with 0 (every selected test passed) or 1 (also when the filter
selects nothing). The filter is a comma-separated list; an item selects the test of that name and every test whose name
starts with the item and a dot; `all` selects every test. Tests register themselves with a macro from `dbg_selftest.h`
(`DBG_SELFTEST`, `DBG_SELFTEST_GUEST`), one file per package, so packages do not edit a shared list. A session needs no
server and no client, and has no server unless `Debugger:Enabled` is set explicitly: `Debugger:SelfTest` turns the
default of that key to `no`, with a window as well as headless (2.11).

Two kinds of test, both on (E): a **machine test** runs once, to completion, before the guest has executed anything (the
machine exists, the SH4 backend is selected, the lifetime is `Running`). A **guest test** drives the guest from debugger
stops, in place of a client: the park loop hands it every stop; in that call it stands where a call of every class may
stand, reads and writes guest state and the tables directly and uses `dbg::emu` (`SetSh4Breakpoints`,
`SetArm7Breakpoints`, `SetDspBreakpoints`, `SetEventBreakpoints`, `SetActiveMask`, `Resume`, `Step`, `Reset`) to arm
what happens next. It never calls
`dbg::emu::Call`; what another thread of the debugger does, and what the host does, is done by a thread the test starts.
During a session every emulator thread makes the start park (2.2.9).

In ctest: `nulldc_debugger_selftest(<name>)` in `tests/debugger/selftest/CMakeLists.txt` makes
`debugger.selftest.<name>` (the SH4 on the JIT) and `debugger.selftest.<name>.interp` (on the interpreter): a headless
session with the filter `<name>` that has to end with exit code 0 and the summary of the driver. They run on the
emulator the system tests build. **A test that the run ends is not named under the prefix of a session**: the filter of
a session selects by prefix and waits for its last test to return, so a test that never returns would hang the session
(hence `emurun.*` beside `emu.*`, `arm7n.*` beside `arm7.*`, `writesrun.*` beside `writes.*`). **No wait without a
bound** (EXe of the M2 round): every wait of the sessions `arm7`, `writes` and `core` and of the tests `arm7n.*` and
`writesrun.neutral` ends by a check that fails within a bound, one constant a session (`kBoundMs`: 30 s for `arm7`
and `writes`, 60 s for `core`; the longest wait of a correct run is under a tenth of a second for the first two and
under a second for `core`); a thread of the session cuts a wait at the bound, asks the machine for a stop it can still
give, and the session prints its summary. Two windows of test code on a failure path only are known and accepted: a
test thread of `core.lifetime` / `core.writes` that finds the machine Stopped can lose it to the watch before it takes
the lock the two share (reached only when the session is already failing and the machine stops within milliseconds of
the bound), and a test's own thread that holds the machine longer than the bound and a tenth ends the session by the
watch's FAILED line without a summary.

- E3, `dbg_selftest_regs.cpp` (session `regs`, done; a ctest target, `debugger.selftest.regs[.interp]`, since 6.1.1
  item 6): every register row of `dbg_regs_tables` maps to a registered slot of the live `sh4mmr` / SB arrays and every
  registered slot has a row; `dbg_mem` region boundaries, mirrors, the GD-ROM and modem holes, a read across
  `0x0021FFF0`, and that no read changes `SB_FFST`'s counter, the SCIF FIFO or the AICA common registers. Since
  6.1.1 item 8 the session has nine tests and runs the guest: eight machine tests, and the guest test
  `regs.enabled`, which runs 2ndmix for 200,000,000 cycles, pauses whenever the ARMRST byte the SH4 reads changes,
  and at each stop requires `ENABLED` to be what the guest wrote, what `SoundCPU::DebugEnabled()` says and what the
  save state holds (in 2ndmix the register goes 0, 1, 0, 1).
- E4a, `dbg_selftest_emu.cpp` (session `emu`, nineteen tests, done; `tests/debugger/selftest/README.md` has them): the
  machine test; pauses (2000 of a loop, 1000 with interrupts, 300 inside SLEEP waits, the 250 ms liveness bound); event
  breakpoints in and out of SLEEP waits; calls of every class in every state, cancellation after 2 s; host stops
  against a parked thread, a stop twice, a stop during a call, during a thread start, in the slice of an event stop;
  the owed resume after a host stop taken in a breakpoint stop; lifetime (machine destroyed and made again); resets by
  the guest, by a call, while parked; `IsRunning()` while resetting and while stopping; guest exit; host FP mode; steps
  as far as run control goes; the window title. With them: `debugger.selftest.emuexit.*` (a guest exit ends a headless
  run), `emurun.reads / .pending / .pauses` (2ndmix for 600 vblanks with reads, with a pause pending at the end, with
  150 pauses: the state hash of the run without a session), `emuhang.parked` (a session that stays parked at its
  start, with `Exit.AfterVblanks=5`) and `startpaused` (the same for `Debugger:StartPaused=yes` without a session;
  2.2.9). The second is a run without `Debugger:SelfTest`, so the rule of 2.11 concerns it: it sets
  `Debugger:Enabled=yes` and `Debugger:Port=0` (6.1.1 item 7) and pins that a start park with a server and no client
  waits without limit. The first is a selftest session without the key and does not change, and neither do the
  sessions `sh4bp`, `sh4bpboot` and `sh4bpbase`, which set `Debugger:StartPaused=yes` and are excepted as sessions.
- E4b, `dbg_selftest_sh4bp.cpp` (sessions `sh4bp`, `sh4bpboot`, `sh4bpbase`, done): 2.3.6.
- J1, `dbg_selftest_hostcycles.cpp` (session `hostcycles`, done; `debugger.selftest.hostcycles[.interp]`, mode
  same-hash): the host stops the running machine and starts it again, 600 times in one process, on the way of
  2ndmix to its 1200th vblank; the cycles begin at vblank 300, where the program is in its main loop and every thread
  has the same work. A thread of the test plays the host; the stop is asked for on the emulator thread, so that it
  never meets the stop that ends the run. What has to hold: the machine counts as running (`IsRunning()`, 2.2.5)
  at every one of the 600 starts, inside `Resume()`, where its emulator thread does not exist yet, and in none of
  its stop callbacks; on the recompiler every thread starts with the stubs of the code generator at the start of
  the code cache and nothing behind them, with no block in the tables and no entry in the lookup table for a block
  of the thread before; the cache never fills although the threads together compile six times what it holds; the
  guest does not get slower with the number of threads; and the run ends in the state hash of the same run without
  a session (2.3.7 items 9 and 12). On the interpreter, which has no code cache: the cycles, what counts as
  running, the speed and the hash.
- E5, `dbg_selftest_core.cpp` (session `core`, six tests, done; `debugger.selftest.core[.interp]`):
  - `core.opcodes` (machine test): for all 65536 encodings, `dbg_disasm` reports the word as an instruction
    (`Sh4InsnInfo::valid`) iff the emulator's opcode table executes it (`OpDesc[op]->oph != iNotImplemented`), with the
    one exception E2 documents: `0x085B`, the reios trap, which the table executes and the disassembler does not call
    an instruction (the opcode table cannot be linked standalone, hence this test is here); and the core's line for
    that word is `.word 0x085b ! reios trap`.
  - `core.tick` (machine test): the JSON emission rules of 3.0 on ticks built with `core::BuildTickText(full)`: no
    `null`, the required keys, every register list equal to the describe list, values formatted to their widths
    (I4), a full tick carries every node with registers, a running tick the four base paths, watches evaluated on
    paths the tick does not carry, `tickId` advancing. A machine test stands in a running machine, so its ticks are
    running ticks: what holds for a paused tick is checked in `core.guest` and `core.lifetime`. The no-wrap rule of
    a memory slice (2.7: `{sh4, 0xFFFFFFF0, 32}` answers `-32010`, never bytes of the boot ROM) is pinned here. Strings (3.0 item 9): with a symbol table loaded from a fixture
    image whose names have bytes above `0x7F` and control characters, and with an event whose message has such bytes,
    the tick and the answers of `state.getDisassembly` and `state.getCallstack` are valid UTF-8 and show `?` in place
    of each such byte.
  - `core.tables` (machine test): the breakpoint, watch and category tables as the dispatcher's calls use them: ids
    from 1 and never reused, the bound of 1024 breakpoints, watch resolution (case of the register name and of the path —
    an upper-case path resolves —, lower-casing, an expression that is stored already, a duplicate inside one call,
    unresolvable; the watch bounds: the 64 expressions of a call, the 256 characters of an expression, and the bound of
    256 watches, which the M1 tree (247 registers, while `state.watch` stores no expression twice) could not reach and
    the M2 tree (2,940 registers, from E7b on; 2,949 in the M3 tree, from E8a on) does: a call that would pass it is
    refused whole with `-32602`; S08 of
    the contract makes its bound probe against the emulator since then),
    the category of a breakpoint, the active mask handed to `emu::SetActiveMask` for mute, solo and no client, the
    event gates for low-rate and high-rate events with and without a (disabled) event breakpoint.
  - `core.events` (machine test): the event ring: 200 entries, `eventId`s ascending and stable across ticks, the oldest
    dropped, a message cut to its bound, `metadata.cycles`.
  - `core.lifetime` (guest test, with a thread that plays the host as E4a's tests do): the tick and the describe result
    in `Stopped` (`host-stopped`, after a guest exit `guest-exit`) and in `NoMachine` (`no-game`: `registers: {}`, the
    static tree with `"n/a"`, no engine capability), built under L; bookkeeping requests work there; a new `stopId` for
    each; the machine made again parks at its start.
  - `core.guest` (guest test, on the booted ELF; its breakpoint is on `stars_one_frame`, the function of 5.7 that
    2ndmix reaches in every frame): after the boot the symbol table has the ELF's symbols (the entry
    resolves); the SH4 callstack has frame 0 = PC and frame 1 = PR; the ARM7 callstack is `[]` while the ARM7 is held in
    reset and at least two frames (frame 0 = PC, frame 1 = R14 with location `lr`) once it runs, and in each of the two
    ticks the register `ENABLED` of `dc.aica.arm7` reads 0 and 1 respectively (the pairing of 2.10); a tick at a breakpoint
    stop carries the `breakpointId` and stops carrying it when the breakpoint is removed.

  The session has no server: `Debugger:SelfTest` keeps it off unless `Debugger:Enabled` is set explicitly (2.11). So
  there is no client and no core thread; with a server running the tests of `core` fail their first check instead of
  running beside (C) (every `core` test first checks that the core thread was not started, which the private header
  tells). The ctest sessions are headless and would have no server for that reason alone; the rule is for a session
  with a window — the checklist of E4a, a session made by hand —, where a devtools page that reconnects to the
  default port would otherwise rewrite the event gates and the active mask a test set. The tests reach the core
  through E5's private header `dbg_core_private.h` (6.1): they hand request texts to the dispatcher and get the
  response texts — in place where the test stands on (E), which is where a call of every class may stand (a machine
  test, a guest test at its stop: no `dbg::emu::Call` is made there, which a test on (E) must not make), and from a
  thread the test starts where the request has to come in as (C)'s does, through `dbg::emu::Call` (while the guest
  runs, or against the host that `core.lifetime` plays). They state the number of connected clients themselves
  (`core.tables`: none, one, none again). Nothing is sent anywhere; a test reads ticks with `core::BuildTickText` and
  the describe result as the text the private header gives.
  M2 added to the session (E7a, EXe; eight tests now): `core.writes`, the three write methods as a client meets them
  (the capabilities, `flags.writable` on the 109 rows and on no other, the errors in their order, the value grammar, a
  tick behind every accepted write, the lifetime errors in front of what a method finds out itself); `core.reset`
  (`control.reset` paused — one stop with reason `reset`, no tick of its own, breakpoints, watches and categories kept —
  and running — nothing published, `dc.system.reset` logged, the machine boots again); and in `core.tables` the
  hand-over of the ARM7's breakpoints to `dbg::emu` and the cross-target checks (with only ARM7 breakpoints listed
  `dbg_sh4_armed` is 0; with an enabled SH4 code breakpoint beside them it is 1 and the ARM7's set is unchanged).
- E6b, `dbg_selftest_arm7.cpp` (session `arm7`, forty-three tests in M2, forty-four since W3 of the M3 round, done;
  `debugger.selftest.arm7[.interp|.fallback]`; the registrations `arm7.persample[.interp]`, with
  `config:aica.NoBatch=yes`, went with W2, when the per-sample path became the only one): the
  burst machine and its counts (`VirtualDreamcast::SoundBurstCounts`), ARM7 breakpoints (hit, mask, mirror, lowest id,
  the FIQ vector, the resume exemption, the completion behind a resume or an SH4 step, inside SLEEP), the steps of
  every kind with and without a burst suspended (a FIQ entered inside a sample counted by the number of the FIQ test
  of its sample), the ARM7's register writer, the callstack while stepping, `g_intc_deferred` behind a burst run on in
  the park context (`arm7.intc.order`), the events of a burst run on in the park context (logged, no stop). Its guest is
  the sounding guest of 5.6 (`--guest`), so the session needs no `NULLDC_ELF_DIR`. The run-ended tests `arm7n.*`
  (`idlebp`, `hitresume`, `steps`, `hoststop`) are the neutrality targets of 5.6, registered by
  `tests/debugger/guest/neutral/`; `arm7n.hoststop` holds the completion of a suspended burst in `dc_run`, among its
  stops at least five where the rest of the sample has a FIQ to enter. Not held by a test, and accepted (E6b's
  verifier): the mask test of the check while a temporary breakpoint arms the loop; a step whose next check stands on a
  listed breakpoint, published as a step; four ends of the resume exemption that no run of M2 can show (equivalent
  mutants).
- E7a, `dbg_selftest_writes.cpp` (session `writes`, fourteen tests, done; `debugger.selftest.writes[.interp|.fallback]`,
  on the sounding guest): main memory through its three mirrors, written code that executes (steps and a resume), the
  witness of the discard (`writes.mem.cached`: three ranges of more than one page so that every page of a range is
  witnessed), video memory through its two views, wave memory through both targets, 36 refused ranges, every writable
  register of both processors written and restored, the FPSCR and PC rules of 2.8 (`writes.reg.fpscr`: the witnessing
  block is compiled with PR and SZ clear — a block compiled with PR set calls the interpreter's handlers, which read
  FPSCR when they run; `writes.reg.pc`, the mirror case: the PC is written into a mirror a multiple of 32 MiB away,
  which shares the slots of the block cache, on a loop that goes back PC-relative). `debugger.selftest.writes.smc` runs
  the steps of `writes.mem.cached` as `smcwrites.cached` with `config:Dynarec.SmcCheckLevel=-1`, the configuration in
  which a missing discard executes stale code (2.7). `writesrun.neutral` (mode same-hash) is the neutrality test of the
  writes: **a neutrality test of the debugger's writes writes at stops latched at a slice end, and restores: main
  memory, a code page, general registers, SR and FPSCR of the SH4, R0 to R12 of the ARM7. Not a PC (the owed remainder
  goes), not SR at a code breakpoint of the SH4 (the acceptance of a pending interrupt moves to the leave), not the CPSR
  of the ARM7 (the mode switch rewrites bank slots that only the save state shows, and in user or system mode the live
  SPSR).** In `arm7.step.fiqbehind` and the fourth kind of `arm7n.steps`, a step of the unmasking `MSR` that ends at the
  FIQ vector counts as an entry inside a sample only when no ARM phase ended in it or in the step before it.
- E7b, `dbg_selftest_regs.cpp` (the session `regs` with the M2 rows, done): every row reads the same twice on a standing
  machine; reading every row a hundred times leaves the save state of every device, and the PVR register file, as it
  was; a row on a write-only slot reads `"n/a"`; `L` and `M` are what `SoundCPU::DebugGetInterrupt` says; the rows of
  `dc.gdrom` are the snapshot (its made-up drive state kept inside its enums, so that the session is clean under UBSan);
  a full tick with empty tables is at most 192 KiB, and its build time is printed. `regs.mem.read` writes made-up
  values into the 95 plain-data slots of the on-chip modules and puts them back. No call of `core.tables` lands on
  exactly 256 watches; S08 of the contract holds the bound, `core.tables` the partial fill. The watches of the sessions
  `arm7` and `writes` can print an empty "asked the machine for" when the machine is already stopped at the cut.

The M3 round (`M3-ROUND.md`; `tests/debugger/selftest/README.md` has every test):

- R7, W1, W2, W3 (done): the DSP's expectations of `core.tables`, `core.tick` and `emu.step` follow
  `emu::TargetSteps(Target::Dsp)` (R7); the session `regs` holds the DSP block against the core (`MDEC_CT` 16 bits and
  free running, `StoredWord(0x2804)` false, 0x2804 and a `COEF` word written through `AICA::WriteReg`: a CPU read and
  `DebugReadRegs` give 0 and the word with bits 2:0 clear, the rows the written `RBPL`; W1); `Arm7Loads` makes no sample
  (W2); a held burst is 32 time steps, and **`arm7.held.clocked`** (W3) holds the clock: with the guest's ARM7 held
  across three bursts (`g_hold_arm7`) each is one burst, no ARM phase, 32 time steps and one tail, nothing of the ARM7
  executed (every register as the debugger reads it, every word of its stored context but `INTR_PEND`), `MDEC_CT` 96
  less, the channels the guest keyed play on, and the interrupt update ran in the held bursts (timer A's request raised
  and latched for the held ARM7, and the AICA's interrupt pending for the SH4 at Holly).
- E8a, `dbg_selftest_dsp.cpp` (session `dsp`, thirty-one tests, done; `debugger.selftest.dsp[.interp|.fallback]`, on the
  sounding guest of 5.6, whose DSP program `_start` loads): the DSP counts (`VirtualDreamcast::SoundBurstDspCounts`),
  DSP breakpoints (hit, lowest id, the mask, the resume exemption and its ends, reached in every sample, with the ARM7
  held), the steps of every kind (inside a phase, from 127 across the end of a sample and of a burst, from a pause, from
  an ARM7 stop and from the end of a sample, with the ARM7 held, with an SH4 stop first, on a listed breakpoint; over and
  out `-32004`), an SH4 step dropped and an ARM7 step that is not, resets, host stops and exits with a burst suspended
  in the DSP phase and at the end of a sample (also in the tick that suspends it), the waits of a step ended by the host
  and by the guest, the owed resume, the accessors at a suspension (`dsp.reads.suspended`), the nine datapath rows over
  one sample of the program (`dsp.regs.move`), the grace expiry while suspended. Its test hooks are in
  `dbg_dsp_private.h` (the count of the checks; a hook that arms the debug loop with nothing listed, for `dspn.loop`).
  The run-ended tests `dspn.loop`, `.hitresume`, `.steps`, `.hoststop` are the neutrality targets
  `debugger.sound.neutral.dsp.*` of 5.6. Known by its verifiers: on the guest's program `LATCH` and `LATCH_COPY` are
  equal at every stop the session compares (a swap of the two rows is caught only by `regs.devices`); the
  `dbg_dsp_armed == 0` guard of `PrepareResume` has no effect a test can see (`Rearm`'s clear at the 0-to-1 change
  ends the exemption first), so the contract is `Rearm`'s clear; `NoteArm7Fallback`'s rule is not exercised on this
  host (2.3.7 item 7).
- E8b, `dbg_selftest_sgc.cpp` (session `sgc`, twenty-five tests with `mem.binary.*`, done; `debugger.selftest.sgc[.interp]`,
  the filter `sgc,mem.binary`, on the sounding guest): `state.getMemoryBinary` (the bytes of `getMemorySlice`, the
  unreadable runs, the errors, the bounds, `binaryId` per connection and shared by the kinds), the frame's layout against
  the accessors and each frame's sends against MIXS (`sgc.layout`), the frame count of a capture armed at each
  suspension point (`sgc.count`), the capture's stop (`sgc.stop`), captures while running, a second one, the previous
  buffer, the cancels by a reset, a host stop and a guest exit, a capture completed by `dc_run`'s run to the end
  (`sgc.runtoend`), a capture with the ARM7 held (`sgc.held`), the deadline and its races (`sgc.timeout`, `.late`,
  `.untaken`), a refused resume, a closure with no thread, the drain of the exiting thread (`sgc.drain`), a capture's
  last frame in a DSP step's sample (`sgc.merge`), the kept outputs (`sgc.outputs`), an allocation that fails
  (`sgc.nomemory`). The session has no server: the test starts the core thread without one and hands it requests as a
  client would, through the hooks of `dbg_core_private.h` (`core::StartForTest`, `core::TextForTest`,
  `core::SetSendSinkForTest`, which takes binary frames and deferred responses); `dbg_sgc_private.h` gives what `Arm`
  was asked and did, the notices given and the frames of the capture that records, and makes the allocation of a
  capture's buffer fail. The run-ended tests `sgcn.running`, `sgcn.paused` are the neutrality targets
  `debugger.sound.neutral.sgc.*` of 5.6. Known by its verifiers: two possible flakes, never a false pass —
  `sgc.drain` relies on (C) queuing its table call within the stop callback's fixed nap of 300 ms, and `test_bounds`'
  32 MiB binary message then ticks has about 4.6 times the margin of its slow read before the 16 MiB text bound cuts
  the client; a failed check between `CoreOn()` and `CoreOff()` skips `CoreOff()`, so every later core-based test of
  that run fails too; `sgc.held`'s refused capture goes red only through the 30 s watch; not tested: a
  `recordSgcFrames` table call that the park loop takes at a stop, a request while a step runs, and
  `HandleRequestText` with no core thread.

### 5.4 Integration recipe (emulator + contract runner)

- Build the UI (`npm run build:native`) and the emulator with `-DNULLDC_DEVTOOLS_DIST=<dreamcast-devtools>/dist-native`.
- Start the emulator headless with the test program `2ndmix.elf` (the KallistiOS example of that name, taken by path;
  it runs under reios, no BIOS; starts the ARM7; has `.symtab`; entry `0x8C010000`):

  ```
  nulldc3 --config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800 , \
                  Debugger:Enabled=yes , Debugger:Port=0 , Debugger:Required=yes , Debugger:StartPaused=yes \
          2ndmix.elf
  ```

  `Enabled=yes` is needed because a headless run is an automated run (2.11); a recipe that forgets it ends at once
  with exit code 2, since `Required=yes` and `StartPaused=yes` without an enabled server are a configuration error.
  `Port=0` avoids collisions between runs in
  parallel; `Required=yes` makes a failed bind fatal instead of a test against somebody else's emulator, or a start
  park that no client can reach; `StartPaused=yes` gives the runner its anchor. The harness parses the `Debugger: http://127.0.0.1:<port>/` line,
  which is on stdout and flushed when it is printed (2.11). The recipe sets neither `Debugger:TickMs` nor
  `Debugger:FullTickEvery`: the gates are run with the defaults, for which the contract's time bounds are written
  (5.5.3).
- One emulator process serves a whole contract run. Emulated time stands still at the start park, so no exit key ends
  the process: the harness ends it by pid with SIGTERM when the runner has returned, and has a watchdog.
- The gate: `npm run contract -- --url ws://127.0.0.1:<port>/ws --profile emulator --elf <path> --milestone M1` (5.5.3);
  from M2 `--milestone M2`, in two runs: the one above with the M1 test program, and
  `--elf <dsptest.elf> --milestone M2 --only S00,S16` against an emulator started with the sounding guest of 5.6, with
  `--require-note "arm7: held at the start" --require-note "arm7: bl stepped over and out"`; from M3 `--milestone M3`
  on 2ndmix, and on the sounding guest `--elf <dsptest.elf> --milestone M3 --dsp-program` twice (whole, with
  `--require-note "sgc: captured with the arm7 held"`, and `--only S00,S21`, with `--require-note "dsp: stepped with the
  arm7 held"`), and the M2 gate's second run again (5.5.3). The runner waits for a paused tick with `reason: "start"` and
  asks `debugger.describe` again there before the first scenario.
- Timeouts in 5.5 are wall time for the mock; the emulator profile multiplies them by 5.
- In ctest (PLAN T2.6): `tests/debugger/contract/` (E5) has twelve targets. `contract.py` starts every emulator with
  the recipe above plus `config:Dynarec.Enabled=yes|no` for the engine, and nothing else; `wsclient.py`, a WebSocket
  client that needs nothing but the Python library, is the client of the tests that are not the runner's; the
  README of the directory has every test. All of them need `NULLDC_ELF_DIR` (2ndmix.elf). The ones that run the
  contract runner, `m1*` and `hash.idlebp*`, also need `NULLDC_DEVTOOLS_DIR` (with its `node_modules`) and npm, and
  are tests whose data is missing without them (5.2); `grace`, `config`, `slow`, `hash.off` and `hash.noclient` need
  nothing more.
  - `debugger.contract.m1` (JIT), `debugger.contract.m1.interp` (`config:Dynarec.Enabled=no`) and
    `debugger.contract.m1.fallback` (`Debugger:ForceFallback=yes`): the gate, on three engines. The runner has to end
    with exit code 0 and `contract: OK`, with every selected scenario passed (none failed, none partial, none not
    applicable) and no invariant violated; the emulator has to be alive when the runner has ended, and to end within
    20 s of SIGTERM.
  - `debugger.contract.grace`: the grace period (2.9), and the client that connects behind it. A client at the start
    park adds an enabled code breakpoint on `stars_one_frame`, a disabled event breakpoint, two watches and a muted
    category, resumes, and disconnects when the breakpoint has stopped the target. The breakpoint is on the function
    of 5.7, which 2ndmix reaches in every frame: on an address that is never reached again a cleared active mask and
    breakpoints that are active again look alike. A client that connects 3 s later finds the stop it left and leaves
    again. 11 s after that a third client connects and sends nothing. What it has to see is not asked for with a
    describe: the connect itself makes the breakpoint active again and the target reaches it within a frame, so a
    describe sent right behind the connect races that stop. **The stop has to arrive unasked**: within 2 s a paused
    tick with `reason: "breakpoint"`, the same `breakpointId` and `stopId` + 1, **and the SH4 has to have run at least
    two frames since the stop that was released** (`SH4_CYCLES` of `dc.timing` in the two ticks). A stop is sent to
    the clients that are connected when it is made, so the unasked stop was made in front of this client: until then
    the target ran, released by the grace period, with the breakpoint listed and enabled and nothing stopping there,
    which is the cleared mask; and the breakpoint is active again for no other reason than the connect. The tick has
    the breakpoints, the watches and the categories as they were, and one `dc.debugger.note` entry in its event log,
    the one the core appended with no client connected (2.6). Then the client sends `debugger.describe` with
    `protocol: 2`, and the tick behind it has a second `dc.debugger.note`: the event gates were rewritten at the
    connect as well. When no paused tick comes within the 2 s, the test asks with a describe and names the broken
    rule: the stop was not released, the mask was not cleared, or breakpoints are not active again. Since J1 the test
    goes on, for the rule that a connect ends the expired state (2.9): behind the describe with `protocol: 2` the
    third client sends `control.runUntil` and ends its connection (it closes its sending side and reads until the
    server closes, so that the request cannot be lost); 3 s later a fourth client describes and has to find the
    target paused at the next stop of the breakpoint — `reason: "breakpoint"`, the same `breakpointId`, `stopId` one
    more — and `SH4_CYCLES` of that stop at most three frames behind the stop the third client left (measured 1.0;
    with the reset of the expired state taken out, the fourth client finds the target running).
  - `debugger.contract.config`, the configuration errors of 2.11 and a server that cannot be started: six headless
    runs with 2ndmix, each of which has to end by itself within 60 s. Three without an enabled server —
    `Debugger:Required=yes` without `Debugger:Enabled`, `Debugger:StartPaused=yes` without it, and
    `Debugger:Required=yes` with `Debugger:Enabled=no` given —, each with exit code 2, a line on stderr that names
    the key and says that `Debugger:Enabled=yes` is missing, no `Debugger: http://` line and no `nulldc-exit` line.
    And three with `Debugger:Enabled=yes` on a port that is taken (the test itself has bound it and listens on it):
    with `Debugger:Required=yes`, and with `Debugger:StartPaused=yes` instead, exit code 2, a `Debugger: ` line on
    stderr that names the key, no `Debugger: http://` line and no `nulldc-exit` line; with neither key and
    `config:Exit.AfterVblanks=5` the emulator runs on without the server: exit code 0, `reason=vblanks`, exactly one
    `Debugger: ` line on stderr and no `Debugger: http://` line.
  - `debugger.contract.slow`, the slow-client rule of 2.9, on a running target. One client makes the ticks large (a
    watch on every register and 300 disabled event breakpoints: about 80 KiB a tick) before a second one connects
    with a small receive buffer, describes itself and stops reading. The first changes a category back and forth
    until 10 MiB of ticks have come to it — more than the megabyte of the rule, less than the 16 MiB of text at which
    the transport ends a client —, is idle for 3 s and keeps the `tickId`s of the periodic ticks of that time, and makes
    one more change; then the second client reads everything. It has to have been sent none of those periodic ticks;
    every tick that a request caused, in the order they were built; and the tick of the last change with every
    register path of a full tick.
  - `debugger.contract.hash.off`, `.hash.noclient` and `.hash.idlebp`, each also as `.interp`: the state-hash tests
    of 5.7.

  With E5 the tests project has 187 ctest targets: the 171 of before, `debugger.selftest.core[.interp]`,
  `debugger.symbols.table` and `.damaged`, and the twelve of this directory. With J1 it has 190: those, and
  `debugger.selftest.hostcycles[.interp]` (5.3) and `debugger.sh4bp.threads` (2.3.6).
  - M2 (packages E6b, E7a, E7b of `M2-ROUND.md`): `contract.py` gained the modes `parts` (the runner with
    `--only S00,<scenario> --parts <scenario>:<part>[+<part>...]`, every `--require-note` handed on) and `m2` (the whole
    gate, and with `ONLY S00,S16` its second run, which also passes `--require-note`), and `hash-listening` with its
    rule `hash-listening-rule`; `nulldc_debugger_contract` gained the options `GUEST <path>` (with it the test does not
    ask for `NULLDC_ELF_DIR`) and `ONLY`. `contract.py` reads no note out of the runner's output: it takes the runner's
    exit code. The targets: `debugger.contract.m2arm7` (`.interp`, `.fallback`) with `.m2arm7.guest` (`.interp`), the
    part `arm7` of S16; `.m2writes*` with `.m2writes.guest*`, the parts `arm7`, `writeMemory`, `setRegister`, `reset`;
    `.m2` (`.interp`, `.fallback`), the whole M2 gate on 2ndmix, 19 scenarios passed, 0 partial, 0 not applicable each,
    with `.m2.guest` (`.interp`), its second run on the sounding guest (2 passed, both notes); and
    `.hash.listening` (`.interp`), `.hash.listening.guest` (`.interp`) and `.hash.listening.rule`: a client that
    connects, describes and then only receives for a whole run of 600 vblanks, with `Debugger:TickMs=20` and
    `Debugger:FullTickEvery=1` and one `control.runUntil` behind the describe (a run listened to from its first
    instruction needs the `StartPaused` stop), leaves the state hash what it is with the server off, although every
    full tick reads every device; a tick that says `registersStale` is full without `dc.sh4.cpu` (the rule, shown on
    made-up ticks: no engine sends a stale tick on x86-64, nor on arm64 since A64 of the platform round, and the
    modes refuse one from either engine on every host since then). The targets that start Python from the tree run it
    with
    `PYTHONDONTWRITEBYTECODE=1`, so that a test run leaves the source tree as it was (EXe). The tests project has 259
    targets at the end of the M2 round. `hash.listening` needs ten full ticks, which a host four times faster might
    not give in 600 vblanks: it can fail a correct server, never pass a broken one.
  - M3 (packages E8a and E8b of `M3-ROUND.md`): `contract.py` gained the modes `dsp` (the guest, `--milestone M3
    --dsp-program --only S00,S21`, E8a), `m3` (2ndmix, `--milestone M3`: S00 to S17, S19, S20) and `m3dsp` (the guest,
    `--milestone M3 --dsp-program`: S00, S17, S21; E8b), and accepts `--require-note` in `dsp` and `m3dsp`. The
    targets: `debugger.contract.m3` (`.interp`, `.fallback`), run 1 of the M3 gate, 20 scenarios passed, 0 partial, 0 not
    applicable each; `debugger.contract.m3dsp.guest` (`.interp`), 3 passed with the note `sgc: captured with the arm7
    held` required; `debugger.contract.m3dsp21.guest` (`.interp`), 2 passed with the note `dsp: stepped with the arm7
    held` required — the two of run 2 (5.5.3). The modes `m3` and `m3dsp` require passed above 0 and nothing failed,
    partial or not applicable, but do not assert 20 and 3: the round's gate (M3G) states the numbers. A test of the
    transport in `tests/debugger/net` holds its two bounds (2.11: a 32 MiB binary message followed by ticks, read slowly,
    keeps the client; more than 16 MiB of text, or more than 64 MiB of binary messages, disconnects it). The tests
    project has 307 targets at the end of the M3 round.
  - The rebrand (PLAN package N1) changes what `contract.py`, `agents/nulldc3/f3d/scripts/smoke.sh` and the UI README's
    recipe "The UI against the emulator" name: they isolate a run by `HOME`, `XDG_CONFIG_HOME` and `XDG_DATA_HOME` and
    name the binary `devcast` and the switch `-config`; with `--portable <dir>` and the new names all three change, in
    both repositories.
  - An emulator that has to carry the UI (F3d's smoke spec, the round's gate) is configured by itself with
    `-DNULLDC_DEVTOOLS_DIST=<a dist-native directory>` and handed to the tests project as `NULLDC_EMULATOR` (and
    `NULLDC_EMULATOR_TA_HLE`); the option is not given to the tests project, where it replaces the bundle of
    `tests/debugger/net`. The UI checkout an emulator gate runs with (`NULLDC_DEVTOOLS_DIR`) is a commit the integrator
    names, the last verified UI head (`M2-ROUND.md` section 4).
- The contract tests run on the emulator the system tests build (`<build>/system/emulator/nulldc3`, or
  `NULLDC_EMULATOR`), as the selftests do; no embedded bundle is needed: S15 passes on the built-in page. The first
  item of this recipe is for a run by hand against the UI.
- **The run helper, and the gates on Windows, macOS and Linux arm64** (the platform round; `PLATFORM-ROUND.md` PL-D5,
  PL-D69). The harnesses — `tests/debugger/selftest/selftest.py`, `tests/debugger/contract/contract.py`,
  `tests/debugger/guest/dsptest/soundlines.py`, `tests/debugger/sh4bp/sessions.py` — start every program through one
  helper, `tests/common/emurun.py` (W64b; its Darwin branch MACb; `harness.emurun` holds every branch on every host):
  the environment of a run (nothing of the caller's but the sanitizers' options; on Windows `SystemRoot` and `TEMP` in
  the run's directory, on macOS `TMPDIR` there), the program itself inside `nulldc3-winrun` on Windows (its path in
  `NULLDC_WINRUN`, which the tests project passes: a harness there starts nothing without it), through
  `tests/macos/macrun.py` on macOS, and on another POSIX host directly with a core file size limit of 1 byte; the end of
  a run (on Windows the job, on macOS a SIGTERM to `macrun.py`, which kills everything below it); output read without
  the `\r` of a `\r\n`; no POSIX shell on Windows. `tests/system`, the Linux-only system-test harness, is not ported
  (the owner's decision) and builds no emulator there: the tests project takes `-DNULLDC_EMULATOR=` (`tests/README.md`,
  "On Windows", "On macOS"). **A gate on those systems** is every target that runs green twice, with the skipped listed
  with their reasons and numbers (PL-D5: on Linux x86_64 "none skipped" holds, on the others the owner's decisions skip
  the Linux-only harness and what needs an x86-64 build). The skip lists and their counts, as the platform round's gate
  (PLG) measured them, are in `tests/README.md`: on Windows 77 of 349 (the Linux-only system-test harness 67, the four
  objdump oracles, the three emcc tests, `cfg.log_file`, `debugger.sound.runner`, `debugger.sh4bp.b11`); on macOS 81 of
  340 (the Linux-only harness 67, the x64 backend's blocks 9, the three emcc tests, `debugger.sh4bp.b11`,
  `debugger.sound.runner`); on Linux arm64 16 of 330 (the x64 backend's blocks 9, the three emcc tests, the four `vmem`
  system tests that skip themselves where unprivileged user namespaces are refused). The contract gates M1 to M3 run
  there with Linux's numbers: M1 18 passed, M2 19 and M3's run 1 20 on the JIT, the interpreter and the forced fallback,
  M2's guest run 2 and M3's two guest runs 3 and 2 with their notes on the JIT and the interpreter (on the arm64
  recompiler since A64; until then S04 was recorded there, 5.5.4). In CI (`.github/workflows/ci.yml`, CIe) the same
  tests run with the data that public repositories give (PL-D58: no 2ndmix, hello or RTL checkout), and the job
  summaries list the skipped with their reasons.

### 5.5 Contract invariants and scenarios

Applied to every tick of every connection in every scenario:

- I1. Validates against the strict copy of the schema (unknown keys fail), and the raw JSON contains no `null`. Every
  response has the envelope of 3.0 (the keys `jsonrpc`, `id`, and `result` or `error`; an `error` of `code` and
  `message` only; `id` `null` only with `-32700` and `-32600`).
- I2. `tickId` strictly increases per connection (not necessarily by 1). `timestamp` is within one day of the runner's
  clock.
- I3. `breakpoints` keys equal `String(id)`. The event log has at most 200 entries; its `eventId`s are unique and
  ascend (oldest first); an `eventId` seen in an earlier tick names the same entry; every entry's `timestamp` is within
  one day of the runner's clock.
- I4. Every `registers` path exists in the describe tree and carries exactly the describe register names and widths in
  order; values with `width > 0` match `0x` + `ceil(width/4)` upper-case hex digits or are `"n/a"`; no register of a tick
  carries `flags` (M2).
- The pairing of 2.10 (M2, F3a): in every paused tick whose reason is not `host-stopped`, `guest-exit` or `no-game`, of
  a server that lists `arm7`, `ENABLED` of `dc.aica.arm7` and `callstacks.arm7` agree — `ENABLED` 1: frame 0 is the PC
  and frame 1 is `R14` with location `lr`; `ENABLED` 0: `[]`.
- I5. A paused tick is full, unless its reason is `no-game` (`registers: {}`), and never says `registersStale`. A running
  tick has at least the four base paths (`dc.sh4.cpu` is absent exactly when `registersStale` is true). `categories` has
  the four keys. `stopId` never decreases. A running tick carries no callstack frames, no `reason` and no
  `breakpointId`.
- I6. Watches are in id order. A watch on `<path>.<name>` reads what that register reads in the same tick when the tick
  carries the path; under `registersStale` a watch on `dc.sh4.cpu.*` reads `"n/a"`.
- I7. A `breakpointId` comes with reason `breakpoint` or `event` and is a key of `breakpoints` in the same tick.
- I8. Ids are never reused: no breakpoint id and no watch id that a tick of the run listed and a later tick did not
  comes back for another breakpoint or watch.

Scenarios, both profiles unless marked. "A tick within 1 s" means a tick that shows the effect within that time (times
the profile's factor); the server may send other ticks in between. The time bounds are written for the default
`TickMs` and `FullTickEvery`, with which the gates are run (5.5.3).

| Id | Scenario | Assertion |
| --- | --- | --- |
| S00 | anchor (emulator) | from the `StartPaused` stop: add a code breakpoint at the ELF entry (from `--elf`; `0x8C010000` without it); resume; within 10 s a paused tick with that `breakpointId`, `reason: "breakpoint"`, PC = the entry; `dc.timing.SH4_DEBUG_ENGINE` is `native` and the tick has no `notice`, or it is `interpreter-fallback` (arm64, `Debugger:ForceFallback=yes`) and the `notice` is present; `debugger.describe`, asked again at this stop, lists the `sh4-debug-` capability of that engine and not the other; remove the breakpoint. All later emulator scenarios start from here; later breakpoints use addresses constructed as in S05 or S07, or ELF symbols resolved by the runner, never the PC of an arbitrary pause |
| S01 | describe | result valid, every issue reported; `protocol == 3`; `targets` present; the first notification after the response is a full tick; the same for a describe with `{}` and with `{protocol: 2}`; paths unique and lower case; register names unique per node and upper case; the values of the tree formatted as I4; `actions` within `PanelId`; no empty arrays; every target's `pcPath` / `pcRegister` is in the tree; the normative columns of 3.1 for every listed target; of 2.8: the M1 paths, their parents, their actions, the anchor registers (the three of the targets, and `dc.aica.arm7` `ENABLED` with width 1 when `arm7` is listed), the register list of `dc.sh4.cpu`, `dc.holly.core.vblank` among the events, and the width of each of the thirteen registers of `dc.aica.dsp` (2.8: `STEP` 8, `MDEC_CT` 16, `RBP` 16, `RBL` 8 and the nine of M3) that it lists. Under a milestone gate also: `sh4`, `arm7` and `dsp` are listed, and the capabilities of the milestone are advertised (M1: `event-breakpoints`, `callstack`); under M3 also `targets[dsp].stepping === true` and the nine registers of M3 (`ACC` to `MIXS_BANK`) present on `dc.aica.dsp` with their widths |
| S02 | pause | `control.pause {target: "sh4"}` (what the UI sends) returns `{}`; a paused tick within 2 s with `reason: "pause"`, a new `stopId` and no `breakpointId`; a further pause for each of `sh4`, `arm7`, `dsp` — listed by the server or not — returns `{}` and changes nothing; a pause with another string as target is in-band `-32602` |
| S03 | resume | `control.runUntil` returns `{}`; running tick within 1 s, `stopId` unchanged; at least 2 more running ticks within 1.5 s; `dc.timing.SH4_CYCLES` differs between them; a full running tick within 2 s of the resume (the default `TickMs` and `FullTickEvery`, with which the gates are run: 5.5.3); three watches kept through the running phase are listed in every running tick: on `dc.timing.sh4_cycles` (its value moves), on the SH4 PC (`"n/a"` under `registersStale`), and on a register of a path outside the base paths that has a value at the stop before the resume — some registers are `"n/a"` always, 2.8 — (a value, not `"n/a"`, in every running tick); a second `runUntil` returns `{}` and changes nothing |
| S04 | step | paused; step to a line that is not control flow and does not follow a delayed branch (at most 64 steps; none found fails). `control.step {sh4}`: a paused tick with a new `stopId` and `reason: "step"`, and no running tick in between; `dc.timing.SH4_CYCLES` does not decrease, and increases unless PC1 is an exception vector (a deferred interrupt can end the step before an instruction runs, 2.3.2); PC1 == PC0+2 or PC1 is an exception vector. One step may end at a vector. The step after it starts at the handler's first instruction with `SR.BL` set and cannot be interrupted: **a second step in a row that ends at a vector fails S04**, whichever steps of the scenario the two are, and the step the runner then makes from the next plain line has to show PC1 == PC0+2. `control.stepOver` on a line that is not a call behaves as a step: it too may end at a vector, as long as it is not the second step in a row that does. `control.stepOut`: `{}`, a running tick within 1 s (the step out follows completed steps, so nothing is deferred there and it is not complete at a vector, 2.3.2); a stop within 5 s has reason `step` or carries a `breakpointId`; otherwise the runner pauses. `control.step {sh4}` while running: in-band `-32003`. Mock: PC1 is the next address of its loop. Gated in full on every host and engine (on the arm64 recompiler since A64 of the platform round; between MACb and A64 it was recorded there, not gated, by `PLATFORM-ROUND.md` PL-D78: 5.5.4) |
| S05 | breakpoint hit | paused; step until the lines at PC and PC+2 are both not control flow (at most 64 steps); add a code breakpoint at PC+2 with `kind` and `enabled` given; a tick within 1 s lists it with a new id N; the same request again gives a second id; resume; within 2 s a paused tick with `breakpointId == N` (the lower id), `reason: "breakpoint"`, PC == PC+2; a later paused tick of the same stop (provoked by adding a watch) repeats `breakpointId`, `reason` and `stopId`; after `breakpoints.remove {N}` the paused ticks of that stop carry no `breakpointId` |
| S06 | mute | same construction; set `sh4.muted`; a tick echoes it in `categories`; resume; the running tick of the resume within 1 s, then only running ticks for 600 ms — no paused tick in the window, and nothing else is asked of it: no number of ticks —; pause; unmute. Repeat with `arm7.soloed` (not hit), and with `sh4.soloed` (hit). Two requests of one key each leave the other keys alone; a request with an unknown key returns `{}` and changes nothing |
| S07 | toggle, remove, event breakpoints | a disabled breakpoint is not hit (as S06: the running tick of the resume, then no paused tick for 600 ms). **Re-enabling:** at a stop taken after the target has run past its start-up (the pause that ends the first part of this scenario; not the anchor of S00, where the program has not installed its vector base), read `VBR` from `dc.sh4.cpu`; add a code breakpoint at the interrupt entry `VBR + 0x600`, which every program with interrupts enabled reaches at each vblank; resume and see it hit; disable it; resume and see the target run through at least two further vblank events (a disabled event breakpoint on `dc.holly.core.vblank` puts them in the event log); enable it; require the hit within 2 s. A target that takes no interrupt in the window (the first hit does not come) makes the scenario **partial**. Remove deletes; removing again and toggling an unknown id return in-band `-32602`. Event breakpoints: every event the tree lists accepts a breakpoint (added disabled, with `kind`, listed, removed); none stops while disabled; an enabled one on `dc.holly.core.vblank` (added without `kind` and `enabled`) gives a paused tick with `reason: "event"` and its id within 2 s. The table takes 1024 breakpoints (filled with disabled event breakpoints) and refuses the next one with in-band `-32602` |
| S08 | watches | add `dc.sh4.cpu.pc`: listed with an id and a value equal to the PC register in the same paused tick; rename to `dc.sh4.cpu.PR`: same id, value equals PR; unknown path: in-band `-32602`; unwatch: gone; unwatch of an unknown id returns `{}`; 64 expressions in one call are accepted, 65 refused; a 257-character expression is refused (the length bound cannot be told from "unresolvable": both are `-32602`, and no resolvable expression has 256 characters); the 256-watch bound is probed up to what the tree has (a tree with fewer registers is noted, not failed). The initial list is not assumed empty |
| S09 | memory | paused; 64 bytes at PC: `baseAddress` echoed, 64 bytes, equal to the `bytes` of the disassembly lines; a read across `0x0021FFF0` length 32 returns 32 bytes with `unreadable: [[16,16]]`; a fully unreadable range (`0x10000000`, 16) returns `-32010`; every other target with memory is read for 16 bytes at its `defaultMemoryAddress`; a length of 65536 is accepted, 65537 refused; a read across the SH4's `addressMax` does not wrap: 32 bytes from 16 below it give the second half unreadable and 0, or `-32010` (F3a, 2.7) |
| S10 | disassembly | for every listed target: 16 lines at its PC / step; addresses step by `instructionSize`; `bytes` format (`instructionSize` pairs; for `dsp` 8 pairs); non-empty text; every milestone: each line of `dsp` has the text `"MPRO[" + n + "] = "` followed by the playground's disassembly of its own `bytes` (`disassembleDesc(decodeInst(words))` of the UI's `dspUtils.ts`, `TRA:0` for an empty step; the emulator's `debugger.disasm.dsp_playground` holds the same rule); `context` ignored; the default target; from the second-to-last address 2 lines; an address above `addressMax` is `-32602`; an unreadable word (`sh4`, `0x10000000`) is `??` / `<unreadable>`; a count of 1024 is accepted, 1025 refused |
| S11 | callstack | the capability `callstack` is required. Paused tick: `sh4` frame 0 `pc` == PC, frame 1 `pc` == PR with location `pr`, later frames `stack+0x..`; `arm7`, when listed: the pairing with the register `ENABLED` of `dc.aica.arm7` in the same paused tick (1 = the ARM7 is clocked, 0 = it is held in reset; 2.10), in both directions. `ENABLED` reads 1: two or more frames with frame 0 == PC and frame 1 == `R14` with location `lr`. `ENABLED` reads 0: `[]`. `state.getCallstack {arm7}` agrees with the register in the same way, asked with `maxFrames: 8` and without. A register that is missing, or that reads neither 0 nor 1 (`"n/a"`), fails S11 as well as S01; where it stands in the node's list is not checked. `state.getCallstack {maxFrames: 8}` returns at most 8 frames with indices from 0, for `sh4` and for `arm7`; without `maxFrames` it works; 64 is accepted, 65 refused. Running tick: both arrays empty |
| S12 | errors | unknown method gives `-32601` with the request id; `state.getMemorySlice` with a string `address` gives JSON-RPC `-32602`; `control.step` with a numeric `target` gives in-band `-32602`; malformed JSON gives `-32700` with id `null`; a batch, a bare number, an object without `jsonrpc` (with a numeric `id`, and without any `id`: answered with `id: null`), a request with a string id give `-32600` with the `id` of 3.0; a well-formed notification gets no answer; a request with an unknown top-level member and an unknown `params` key is answered as without them (3.0, Framing: a valid result, not `-32600` or `-32602`); `-32004` on the method's own channel for every method whose capability is not advertised (one of these probes is sent a second time with a required parameter missing: still `-32004`, not `-32602`; 3.0), for the steps and code breakpoints of a target without `stepping`, for `stepOver` / `stepOut` of `dsp`, and for `state.editWatch` before M2; a text message of 1 MiB + 1 byte closes the connection with close code 1009 (a close the client sees as 1006, the socket cut before the close frame arrived, fails) or is answered with an error, is not executed, and the server answers a new connection afterwards; a message just under 1 MiB is answered; every error has a non-empty `message` and no `data` member |
| S13 | two clients | a breakpoint added and removed, a watch added and removed and a category change by A appear in a tick received by B within 1 s; on a server with `register-write`, while paused A writes `R0` of `dc.sh4.cpu` with the value it reads and B receives a tick within 1 s (M2, F3a: the tick of a write goes to every client) |
| S14 | reconnect | the runner closes its connection, waits until the server has finished the close (at most 3 s) and has no client left, and reopens within the grace period: the first tick is paused with the same `stopId`, and breakpoints, watches and categories are unchanged |
| S15 | HTTP, Host and Origin (emulator) | `GET /` is HTML; `GET /?ws=x` is HTML; a missing asset is 404; `POST /` is 405; `GET /` with `Host: evil.example:<port>` is 403; upgrade with `Origin: http://evil.example` is 403; with `Origin: null` is 403; with `Origin: http://localhost:5173` is 101 |
| S16 | M2, five parts | Each part gated on the capability or target flag it needs; `M2-ROUND.md` 3.1 item 3 has every check. **`arm7`** (`arm7.stepping`): a held ARM7 at the start (`ENABLED` 0: the three steps `-32012`, a breakpoint on `dc.aica.arm7.pc` accepted, the note `arm7: held at the start`) and the settling (an ARM7 that is released more than once while the guest starts, a PC above `addressMax` in between: resume until `ENABLED` has read 1 for a second, pause, at most five times); a step gives a paused tick with a new `stopId` and `reason: "step"`, and no running tick in between; the PC is compared only on a line that is not control flow, as S04 does — on a plain line the step ends at PC + 4 or at an ARM exception vector (`0x00` to `0x1C`; a pending FIQ is entered before the break test, 2.5.3) —; `stepOver` on a line that is not a call is a step; `stepOut` ends in a stop within 5 s; a step while running is `-32003`; addresses `2` and `0x01000000` are `-32602`; `SH4_CYCLES` across a step never goes back and grows by at most two AICA periods, and is unchanged, with the SH4's PC, by a step from a breakpoint stop (which ends at the address + 4 or at a vector, reason `step`); the breakpoint toggled, muted and twinned (the lower id stops), on a mirror address (`+ 0x00800000`: hit, the tick has the PC as it is), removed and added again while running (a stop within 2 s); the `bl` stepped over and out, with 96 steps for each of its two arrivals, and the target that keeps running behind the step-out (the note `arm7: bl stepped over and out`). **`writeMemory`** (`memory-write`): a range across the end of main memory `-32013`, its first half unchanged; 65536 bytes read and written back; 65537 bytes and a byte of 256 `-32602`; `dsp` `-32004`; wave memory through both targets; main memory through its other two mirrors; video memory through three addresses of one view; flash, a register window, P4 and a store queue `-32013`; while running the boot ROM `-32013`, not `-32003`; a tick behind every accepted write; and a probe that executes what was written (`mov #0x5A,r0` at a scratch address and a step; the probe saves the PC, R0 and the bytes at the stop it is made from, the write to a running target comes first, and its first scratch address is not reused behind a resume). **`setRegister`** (`register-write`): the flag on `R0`, none on `ENABLED` (`-32013`, also with `twelve`) or on a width 0; case of path and name; the value grammar; in every node the first register with the flag takes a write of its value and the first without it answers `-32013` (both PCs carry the flag); `R1` of the ARM7 round trip; the ARM7's PC stored aligned; an odd SH4 PC and a CPSR that is no mode `-32013`; `ENABLED` while running `-32013`; `state.editWatch` with an unused id `-32602`, on `ENABLED` `-32013`; with `MD` set, an SR write that flips `RB` exchanges the banks (the rule sets R0 apart from `R0_BANK` before the flip and restores it). **`reset`** (`reset`): while running `{}`, no paused tick, `dc.system.reset` logged; paused: `reason: "reset"`, `ENABLED` 0 and `callstacks.arm7` `[]`; a disabled breakpoint, a watch and a flipped category added before the paused reset are found behind it; a register of the held ARM7 written; the machine resumed and paused again. **`tree`** (applicable iff `dc.sh4.cpu` offers `sh4-registers`): the paths, parents and actions of the M2 rows (2.8), a register on every node 2.8 names for it (the column R of `M2-ROUND.md` 3.6.1), and each register-panel id offered by exactly one node. Under the M2 gate S01 also asks for `targets[arm7].stepping === true` |
| S17 | M3, per part (`M3-ROUND.md` 3.1 item 3) | **`sgcCapture`** (`sgc-capture`): no settling. **A capture with the ARM7 held**: when `ENABLED` of `dc.aica.arm7` reads 0 at S17's first pause (under `--dsp-program` the anchor), its first capture is made there, from paused, and once it is complete S17 notes `sgc: captured with the arm7 held` (a server that answers `-32012` there fails S17). A capture from paused: the response only when the capture is complete, a paused tick with `reason: "capture"` and a new `stopId`, **the response and that tick in either order**; `getSgcFrameData` right behind the response of a capture whose frame count differs from the one before has the new count; its JSON response comes before its binary frame (header magic, `kind` 1, `binaryId`, `payloadBytes == frameCount * 8192`); an error result is followed by no binary frame; a capture while running: no paused tick, then `frameCount` as asked; the default of 1024 frames; `frames: 0` and `4097` `-32602`, `4096` accepted once (the client survives the 32 MiB frame); `SH4_CYCLES` from the stop before to the capture's stop grows by `ceil(frames / 32)` AICA periods, give or take one; a second connection reads the same capture; under `--dsp-program` a record with flags bit 0 set and a level above 0. **A capture held open** at a DSP breakpoint on step 0 (a stop keeps it recording): a second `recordSgcFrames` is `-32003` and `getSgcFrameData` gives the capture before; then, the breakpoint removed and the target resumed, the response and the `capture` stop in either order, and the new count. **`memoryBinary`** (`memory-binary`): from a pause, 64 bytes: `baseAddress` echoed, `totalBytes == length`, the frame behind the response, kind 2, its `binaryId`, `payloadBytes == totalBytes == length`, the bytes of `getMemorySlice`; across `0x0021FFF0` `unreadable: [[16, 16]]` and zeros; `0x10000000` `-32010` and no frame; `dsp` `-32004`; 16777216 bytes of main memory accepted, 16777217 `-32602`; wave memory through the ARM7 equal to the SH4's; increasing `binaryId`s shared with kind 1; a second connection receives no frame (to the requester only) |
| S18 | M4, per part | `state.getTable` for each id: every row has `columns.length` cells; the `icache` and `ocache` tables have rows, or no rows and a `note` (3.3: a tier with a cache model has rows); rows together with a `note` pass as well (`tables`); `state.captureHolly {ta}`: header, `kind` 4, payload starts with `TAFRAME4` (`holly-capture`) |
| S19 | running target: reads (M1) | with the target running: `debugger.describe` (valid, followed by a full tick; the target still runs), `state.getMemorySlice` and `state.getDisassembly` for `sh4` and for `arm7`, `state.getCallstack` for both: valid answers, none of them `-32003` or `-32000` |
| S20 | running target: bookkeeping (M1) | with the target running, each answered `{}` and followed by a tick within 1 s that shows the effect, the target still running: `breakpoints.add` (a code breakpoint and an event breakpoint, both disabled), `breakpoints.toggle`, `breakpoints.remove`, `breakpoints.setCategoryStates`, `state.watch`, `state.modifyWatchExpression`, `state.unwatch`; none answers `-32003`. Then: a code breakpoint added enabled while running at the interrupt entry (`VBR + 0x600`, read at a pause the scenario makes itself, and resumes from, after a `dc.holly.core.vblank` entry was seen in the event log — a disabled event breakpoint on that event puts the entries there, as in S07; not at the anchor, where the program has not installed its vector base) is hit — a paused tick with its id and `reason: "breakpoint"` within 2 s (no interrupt in the window: **partial**); and an event breakpoint on `dc.holly.core.vblank` added enabled while running stops the target with `reason: "event"` within 2 s |
| S21 | M3: DSP (the second run of the gate, with the DSP test program; `M3-ROUND.md` 3.1 item 3) | **A step with the ARM7 held**: at its first pause (under `--dsp-program` the anchor), when `ENABLED` reads 0, `control.step {dsp}` is a step by the rule below, and S21 notes `dsp: stepped with the arm7 held` (a server that answers `-32012` there fails S21). **The program wait**: S21 reads the 128 steps of `getDisassembly {dsp}` and, while the program is not loaded (with `--elf`, while their `bytes` differ from the words between `g_dsp_mpro` and `g_dsp_mpro_end` of the ELF; without, while no step is loaded), resumes, lets the target run for a second and pauses, at most five times. A step: a paused tick with a new `stopId`, reason `step`, no `breakpointId`, no running tick in between, `STEP` + 1 mod 128; the 128 lines by the text rule of S10, and with `--elf` their `bytes` the ELF's words (the steps behind them `00`); a breakpoint on a loaded step behind `STEP` hit with its id, reason `breakpoint`, `STEP` its address; at that stop a DSP step leaves the SH4's PC and `SH4_CYCLES` as they were; from that stop, the breakpoint still active, `control.runUntil` stops again at the same step with a new `stopId`, `MDEC_CT` one less mod 2^16 and `SH4_CYCLES` grown by at most one AICA period and a slice (the breakpoint is reached in every sample); a breakpoint on 127 hit, and the step from it ends at 0 with `MDEC_CT` one less; the first DSP step from a pause grows `SH4_CYCLES` by at most 145124 + 448 cycles; the nine registers of M3 in every paused tick, and at least one of them differs between two of S21's steps; a disabled breakpoint, and one whose category is muted, is not hit; two on one step stop with the lower id; one added while running is hit within 2 s; address 128 `-32602`; a step while running `-32003`; `stepOver` / `stepOut` `-32004` |

S16 to S18 and S21 gate each part on the capability or target flag it needs. The M1 scenarios need what M1 has: S07
and S20 fail, not skip, without `event-breakpoints`; S11 without `callstack`. S19 to S21 were added by revision 3; ids
are names, not an order: the runner runs S19 and S20 behind S14.

The M3 DSP scenarios need a program that loads the DSP (2ndmix never does): `tests/debugger/guest/dsptest/` in the
emulator repository, a bare-metal SH4 program with an ARM7 program of its own (`M2-ROUND.md` section 5), assembled by
its `gen.sh` with binutils; its ELF and its ARM7 image are committed, so the gate needs no toolchain. It is the sounding
guest of 5.6: package E6a wrote it with the tone and the ARM7 side, and it is the guest of the second run of the M2 gate
(5.5.3); package G of the M3 round added the DSP program part (`M3-ROUND.md` section 6: a program of 18 steps that
`_start` loads behind the anchor, between the first AICA tick and the second), with which it serves the M3 gate (6.3).
There is one such guest; no other example program stands in for it.

#### 5.5.1 Results

Each scenario ends as **passed**, **failed**, **not applicable** (the server does not advertise the capability or target
the scenario needs, or the scenario belongs to the other profile) or **partial** (some of its parts passed and the rest
could not be shown: a capability that is not advertised, a program that takes no interrupt). Only passed is a pass.
A `debugger.describe` that cannot be used is S01 failed, and the scenarios that could not run are failed as "not run";
it is not an aborted run. The same holds for a server that stops answering: after **three request timeouts in a row**
the runner fails the remaining scenarios as "not run" instead of sending each of their requests into its timeout.
The row is counted over all connections of the run. An answered `debugger.describe` does not end it (the clean-up
behind a failed scenario reads the server with a describe, so a server that answers describe and nothing else would
otherwise never be found out); any other answered request does, with a result or with an error. A server that has
lost one method only is therefore not "stopped": each scenario still fails on its own timeout. Behind the third
timeout the runner sends nothing at all, neither inside the scenario that is running nor in the clean-up.

#### 5.5.2 Clean-up

The runner leaves the server with the breakpoints, watches, category states and run state it found: after a failed
scenario and at the end of the run it removes every breakpoint and watch that was not there when it connected (so
nobody else should add any while it runs), restores the categories and the run state, and reports what it could not put
back, which fails the run. When its connection is gone it cleans up on a new one. For a server that stopped
answering (5.5.1) nothing is sent: the clean-up reports that nothing could be put back, and what the last tick shows
as left behind.

#### 5.5.3 What a gate is

- `--milestone Mx` **selects** that milestone's scenarios — those whose milestone is Mx or earlier, without those of the
  other profile — and **requires each to pass**: `partial` and `not applicable` fail a milestone gate, and so does a
  scenario that was not run. M1: S00-S15, S19, S20 (S00 and S15 in the emulator profile only); M2 adds S16; M3 adds S17
  and, in its second run, S21 (`--dsp-program` selects S00, S17 and S21); M4 adds S18.
- Without `--milestone` the runner looks at a server: scenarios the server does not claim to support are not
  applicable, and that is no failure. But a run in which no scenario passed exits non-zero ("nothing verified").
- `--only <ids>` narrows the selection, also inside a gate, and never turns an empty result into a success: a selection
  that leaves nothing to pass is a failed run. `--require <ids>` names further scenarios that have to pass.
- Exit codes: 0 — every selected scenario that had to pass passed, no invariant was violated, the server was put back;
  1 — anything else that is a result; 2 — there was no run (a wrong option, a server that could not be connected to).
- The gates are invoked as

  ```
  npm run contract -- --url ws://127.0.0.1:<port>/ws --profile emulator --elf <path> --milestone M1
  ```

  and likewise for M2, M3 and M4, against an emulator started as in 5.4. **The gate of M3 is two runs**: that one with
  the M1 test program, which selects everything of M3 but S21 (S00 to S17, S19, S20: 20 scenarios), and a second one
  against an emulator started with the DSP test program, `--elf <dsptest.elf> --milestone M3 --dsp-program`, which
  selects S00, S17 and S21, run on the guest twice: whole, with the note `sgc: captured with the arm7 held` required,
  and `--only S00,S21`, with the note `dsp: stepped with the arm7 held` required (in the first, S17's capture lets the
  guest release its ARM7 before S21, whose first pause then finds `ENABLED` 1; `M3-ROUND.md` M3-D32, M3-D44). The M3
  gate also reruns the M2 gate's second run on the changed guest. **M2 adds S16 and is two runs**: the one with the M1 test program, and `--elf <dsptest.elf> --milestone M2 --only S00,S16` against an
  emulator started with the sounding guest (5.6), which requires the notes `arm7: held at the start` and
  `arm7: bl stepped over and out` with the runner's `--require-note`. A package that delivers a part of a scenario
  shows it with `--parts <scenario>:<part>[+<part>...]` (a scenario with only some of its parts is `partial`, which is
  no pass). The mock's gate is `npm run contract -- --milestone M3` (in-process, profile mock): its S16 and S17 pass.
  `--mock-support` (`m1`, `m2` or `m3`) is the profile of the mock the runner starts itself, and `npm run check` runs
  `--milestone M3` on the default mock and on `m3` (the mock that can do what the M3 emulator can, 4.5) with the note
  of S16 required, and the two `--dsp-program` runs on the mock with their notes, on a mock that starts paused with its
  ARM7 held (its anchor, as S00's stop is the emulator's: `M3-ROUND.md` M3-D31). The UI checkout an emulator
  gate runs with is a commit the integrator names, the last verified UI head (`M2-ROUND.md` section 4): UI packages
  commit onto one branch as work lands, so its head can be a package half done. (The gate kept passing when S17
  gained its `state.getMemoryBinary` part: package F4a gave the mock the method with the scenario part, 4.5.)
- **The gates are run with the default `TickMs` and `FullTickEvery`** (200 ms, every fifth periodic tick full: 2.11);
  the contract's time bounds are written for them. A window in which "only running ticks" may come (S06, S07) asks
  for no paused tick in it and for nothing else; the running tick of the resume in front of the window is still
  awaited, within 1 s.
- The mock has to pass its gate as a real program would: its fabricated SH4 takes an interrupt at every vblank and
  goes through a handler at `VBR + 0x600`, so that S07 and S20 pass on it (4.2.1); and its `dc.aica.arm7` has the
  register `ENABLED`, which reads 1 while its ARM7 runs, so that S01 and S11 pass on it (4.2.2).

#### 5.5.4 What the contract does not assert

- Not assertable against a real emulator, and therefore not in the scenarios: the absolute PC after a pause; that a
  breakpoint at an arbitrary address is hit; event-log contents beyond the events the runner provokes; the exact tick
  period; callstack depth beyond frame 1; completion of `stepOver` on a call and of `stepOut`.
- Not contract, and not looked at by the runner: whether the second-level nodes (`dc.sh4`, `dc.holly`, `dc.aica`,
  `dc.system`) carry registers (2.8: the emulator's choice), and how many running ticks come in a window in which
  no paused tick may come (S06, S07).
- The lifetime states `host-stopped`, `guest-exit` and `no-game` have no scenario: a client cannot bring them about, and
  a headless run ends at a guest exit. `no-game` is not reached over a socket in a headless run at all: a headless
  run without a game is an error exit. They are pinned by the selftest `core.lifetime` (5.3), by `emu.lifetime` and
  `emu.hoststop` of E4a, and for the UI by the e2e suite against the mock's `setLifetime`. A whole run made in one of
  those states fails with `-32002` / `-32001`, as it should.
- The grace period is `debugger.contract.grace` (5.4), not a scenario (it takes 10 s of real time and releases the
  stop the other scenarios stand on). The slow-client rule is `debugger.contract.slow` (it needs megabytes of unread
  ticks), and a server that cannot be started is `debugger.contract.config` (process start-up, which the runner
  never sees).
- The half of S11's pairing in which `ENABLED` reads 0, and the no-wrap rule of a memory slice, are held by the gate
  from F3a on: the pairing is an invariant of every paused tick (the anchor stop has `ENABLED` 0), and S09 asks across
  `addressMax`. (Until then the selftests of the core held them alone: `core.guest`, `core.tick`.)
- `registersStale` appears only on a backend without `SH4DBG_CTX_AT_SLICE` (2.2.8): none in the tree since package
  A64 of the platform round (the arm64 JIT was one until then). Its rules (I5, I6, S03) are held on the mock and against
  the stand-in of `debugger.contract.hash.listening.rule` on every host. Between MACb and A64 the contract gates ran on
  the Mac and on the arm64 Linux host, whose running ticks said `registersStale`, and the runner held the rules against
  the emulator there (0 invariant violations in every gate); since A64 the modes `hash-listening` refuse a running tick
  that says it, on every host and engine.
- The 16-client limit is the transport's (`tests/debugger/net`).
- `dc.timing.SH4_CYCLES` across a step (S04) holds on a backend that keeps its slice counter across `Loop()`; behind one
  that restarts its slice a switch of loops can move it by less than a slice (2.3.5), and S04 is then not a gate for
  that backend's fallback. Every JIT of the tree keeps its counter since A64, and S04 is gated in full on every host
  and engine. **Between MACb and A64** the arm64 recompiler restarted its counter, and its runs with and without
  `--fallback` were that fallback (`CaptureEngine` in `dbg_emu.cpp` set `engine.fallback` there either way), so S04
  was recorded there, not gated, by a rule in `run_runner` of `tests/debugger/contract/contract.py` (`PLATFORM-ROUND.md`
  PL-D78): on an `arm64` or `aarch64` host, a JIT runner result whose only failed scenario was S04, failing on one of
  its `dc.timing.SH4_CYCLES` step checks by less than `SH4_TIMESLICE` (448), passed the target with `S04: recorded
  (PL-D78): <details>`, held on made-up summaries by `debugger.contract.s04.rule`. Measured: 446 cycles back in the
  step, in every recorded S04 on the Mac and on Linux arm64. A64 removed the rule, its test and its target with the
  kept counter (PL-D86); the UI's runner was never changed for it.
- M3 (`M3-ROUND.md` 3.1): what an SGC record holds beyond its layout, its flags and its level (the emulator's selftests
  hold it: `sgc.layout`, 5.3); whether a capture's data is stale when its count is right; what the DSP computes (J1 to
  J3 of `M3-ROUND.md` 5.2 hold it: the core against caique, the emulator's wiring of the core, the guest's program
  against the caique model); the length of a capture beyond one AICA period either way. And the clock of a held burst:
  on the guest the bursts that make S17's capture and S21's step at the anchor are clocked (`_start` releases the ARM7
  in front of the first tick), so S17 and S21 hold that a capture and a DSP step are accepted while `ENABLED` reads 0;
  the clock itself is held by `debugger.sound.held.*`, `arm7.held.clocked`, `dsp.bp.held`, `dsp.step.held` and
  `sgc.held` (M3-D44).

### 5.6 Sound regression harness

Built in round 1 (E3, `hw/aica/aica_hash.h/.cpp`), before the sound burst is touched.

- `--config config:Debug.AicaHash=<N>`: the emulator folds every sample pair handed to the audio stream, and at each
  AICA
  tick (32 samples) the `0x8000` bytes of the AICA register file, what the sound CPU writes into a save state and (on a
  backend with `SH4DBG_CTX_AT_SLICE`; on any other it stays the offset basis) the SH4 PC, `R0..R15`, `PR`, `SR` and
  `FPSCR`, and (from W1 of the M3 round) the DSP core's state blob (`AicaDsp::SaveState`, `StateSize()` bytes), into
  64-bit FNV-1a hashes; after N AICA ticks it prints
  `AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> dsp=<hex> ticks=<N> cycles=<scheduler time, decimal>` and exits
  with 0. The field `dsp` (`M3-ROUND.md` M3-D12) sees the whole DSP at every tick, every datapath register among it, so
  every neutrality target sees it too; the state hash of an exit line gains nothing (`docs/nulldc3/automated-runs.md`,
  "The sound line").
- The real-time clock is pinned by one rule: `config:Debug.FixedRtc`, and when that is missing or 0,
  `config:Dreamcast.RTC` (seconds since 1970); the result is what the RTC's reset value, the flash system block and a
  VMU's clock read.
- Acceptance of the harness itself (done): two consecutive runs of the same binary print the same line, for
  2ndmix.elf, N = 40000, in both `aica.NoBatch` settings, with the interpreter and with the JIT.
- Determinism is solved for automated runs: the causes of the earlier "one run in about 150" (Appendix C) were the RTC
  taken from host time and window input; with the RTC pinned and no input, 600 JIT and 600 interpreter runs give one
  hash each, also under load. Lines and state hashes of plain runs are therefore compared exactly; v2's rule of
  repeating such a comparison is gone. The one repeat left is that of the booted-guest sessions of 2.3.6, which has
  another cause.
- **The sounding guest.** On 2ndmix, and on the boot ROM run E3 also made, the sample hash saw only silence (2ndmix
  under reios is silent: its ARM7 idles in a three-instruction loop and never gets its timer FIQ, because reios leaves
  `SCIEB` and `SCILV0-2` at 0; `M2-ROUND.md` M2-Q3, recorded for PLAN tracks 5 and 7): the harness covered registers and
  ARM7 state there, not audio. Before the burst was changed (M2), and before the AICA and ARM7 tiers (PLAN tracks 5, 6),
  the matrix gained a guest that sounds: `tests/debugger/guest/dsptest/` (package E6a), a bare-metal SH4 program with
  an ARM7 program of its own (`M2-ROUND.md` section 5), assembled by its `gen.sh` with binutils; its ELF and its ARM7
  image are committed (`dsptest.elf`, 2028 bytes; `arm7.bin`, 352 bytes; `gen.sh --check` rebuilds and compares them
  byte for byte), and the emulator and every test build and run without either toolchain. Package G of the M3 round
  added the DSP program part (`M3-ROUND.md` section 6: `dsptest_dsp.txt` in the playground's syntax, assembled by an awk
  step of `gen.sh` into the block `g_dsp` that a copy loop of `_start` writes; 18 steps that read the first channel's
  send, keep a counter and a delay line in the ring of wave memory, read back every word they write, and write two EFREG
  outputs that are heard; the ELF is 2708 bytes since then) and with it the DSP test program of the M3 gate (5.5). Its
  `samples` hash is none of the hashes of silence, `basis * prime^(128 k)` for `k = 0 .. N` (every tick makes 32 pairs,
  whether the ARM7 is held or not; the runner's check against the family of silence stays, a superset). N is 2000 for the guest (1.45 emulated seconds) and 40000 for 2ndmix. No line is stored in the tree: a line is
  good for one build on one host (the generator's tables come from `pow()`). The runner (`soundlines.py`) and its ctest
  targets (`debugger.sound.*`, `debugger.guest.*`) live in `tests/debugger/guest/dsptest/`.
  What the guest does for the line: it folds what its ARM7 reads of the timer and of the pending bits into registers,
  and its FIQ handler folds its return address and saved status, so that the `arm7` field carries the order inside a
  burst — every FIQ entry to the instruction, a time step to the iteration of the loop: the line has no field for wave
  memory, main memory or the scheduler. Its ARM7 loop masks F over a part of each pass, so that a FIQ is entered at the
  first instruction of a sample and, when the request came while F was set, inside one; E6a counted the entries before
  the baseline by the number of the FIQ test of its sample that makes each (the first; the second, when the unmasking
  `MSR` starts its sample; the third or a later one), not by the return address, which an entry at a sample that starts
  behind the unmasking `MSR` shares with the entries behind it; the count's gate is on the third class, with a longest
  stretch of ticks without one (measured: 4920 entries in 2000 ticks, 2046 of the third class or later, 1710 of 2000
  ticks with one, the longest stretch 1 tick). Its SH4 main loop goes back by a PC-relative branch, so that a PC
  written into another mirror of main memory stays there. From any instruction of its ARM7 loop the next call is at
  most 32 instructions away and its FIQ handler at most 48 long, and its loops are plain by the contract runner's test
  of a line, which takes a literal load for control flow. The channels the SH4 keys on are rendered whether the ARM7 is
  held or not: a guest whose ARM7 is never released sounds with them. Its ARM7 is released, held and
  released again within the first two ticks: a session that reads `ENABLED` or the ARM7's PC that early sees the
  one-word program at address 0. As built by E6a (`M2-ROUND.md` I.3): "every pending bit cleared" is not enough on this
  emulator to restart the ARM7 cleanly (a release latches a pending request again, and clearing `SCIPD` lowers neither
  nFIQ nor M), so `a_reset` ends the service of a latched request — `SCIEB` 0, `SCILV0-2`, `TIMA`, the `SCIRE` clear,
  then the read of L and the write of M, then `SCIEB` timer A and the `msr`; a hold and release of the ARM7 between two
  bursts restarts the counted FIQ sequence entry for entry. Kept by the integrator's decision, beyond the cut's 5.2: the
  slow pitch steps of the second channel and the `SCIPD` fold in `a_fiq` (taking either out is a guest change with new
  bytes, a new count and a new soak). Not held by a test, and accepted: the order of the `TIMA` write and the `SCIRE`
  clear in `a_reset`, and a hold and release at a later tick. The targets of the directory are also
  `debugger.sound.release.dsptest.<jit|interp>`, `debugger.sound.engines.dsptest`, `debugger.sound.muted.*`,
  `debugger.sound.ignored.dsptest`, `debugger.sound.names` and `debugger.sound.runner` (the guest's README has every
  one), and from the M3 round **`debugger.sound.held.dsptest.<jit|interp>`** (W3, the mode `held`: a copy whose ARM7
  stays held behind the set-up and a copy whose ARM7 idles on `b .` with its interrupts masked agree in `samples regs
  sh4 dsp ticks cycles` — a held ARM7 is an idle one for the rest of the AICA —, the two `arm7` fields differ, the held
  copy is not silent and the idle copy's `samples` differ from the guest's), `debugger.sound.heard.dsptest.<jit|interp>`
  (G: the guest's `samples` differ from those of the copy with the EFSDL of every DSP output 0: the DSP is heard) and
  **`debugger.sound.oracle.dsptest.<jit|interp>`** (G, J3 of `M3-ROUND.md` 5.2: the `dsp` field of a copy whose sends
  are 0 equals the caique model's DSP driven by `aica_dsp_guest_oracle` with the guest's program, on the guest's
  schedule and with the emulator's sweep; registered in `tests/aica_dsp`; it holds the DSP's state on the values the
  copy gives it — the send's half of the program is 0 there, so a defect only a send shows is for `aica.dsp.diff.*` and
  `aica.dsp.emu.lockstep`); with `debugger.disasm.dsptest` (the program's text, the ELF's words and the emulator's
  disassembly together). The render key left the runner and the names with W2 (`.batch` and `.persample` are gone); the
  runner checks the engine of every run, so on an emulator without a recompiler the `.jit` targets fail and are not
  skipped (no gate hands such a binary to the tests project; the fifth build of a gate is run by its own commands).
  `nulldc_sound_neutral` also takes `CONFIG` and `TIMEOUT` (default 900 s) and can be called from a sibling directory;
  a guest directory cannot call `nulldc_debugger_selftest` or the contract function (they read variables of their own
  directories), so sessions on the guest are lines of `tests/debugger/selftest/CMakeLists.txt` and
  `tests/debugger/contract/CMakeLists.txt`. Toolchain: GNU `as` and clang give the same 352 bytes of `arm7.bin`, and
  `debugger.guest.dsptest.files` runs `gen.sh --check` with whichever it finds; the check compares the ELF byte for
  byte, so a host whose sh-elf binutils lays the ELF out differently fails it (a known limit).
- Gate for M2 and M3: identical lines before and after the change, same matrix with the sounding guest; M3 additionally
  with the DSP test ELF. With the debugger in use on the guest — an active ARM7 breakpoint never reached, breakpoints
  hit and resumed, steps — the line equals the plain line and the exit state hash at 90 vblanks equals the plain one,
  part for part; each such test says in lines its target requires what it did (the debug loop ran every sample; how
  many stops, in how many bursts; how many steps of each kind): the targets
  `debugger.sound.neutral.<idlebp|hitresume|steps|hoststop>.<jit|interp>.<batch|persample>`
  (`tests/debugger/guest/neutral/`, the function `nulldc_sound_neutral`; from W2 of the M3 round without the render
  key: `debugger.sound.neutral.<idlebp|hitresume|steps|hoststop>.<jit|interp>`, and from E8a and E8b also
  `.dsp.<loop|hitresume|steps|hoststop>.<jit|interp>` and `.sgc.<running|paused>.<jit|interp>`, twenty in all). The
  lines and hashes of the `NULLDC_DEBUGGER=OFF` build equal those of the default build.
- **The M3 round's rule** (`M3-ROUND.md` 5.1, M3-D42): a field that moves where 5.1 of the cut says "identical" is a
  defect until shown otherwise, and a move is justified by an oracle or a witness, never by a stored line. W1 left every
  line identical in the six fields of B0, in both `aica.NoBatch` settings (the old per-sample path against the new
  core: no workload of the tree loaded a DSP program); W2's default line equals B1's per-sample line in all seven
  fields (the batch render, which `samples` alone showed, went); W3 moved the workloads with held bursts — 2ndmix (18
  held bursts on the JIT, 362 on the interpreter, at its start) now prints the silence of 32 × N pairs in `samples`, and
  `dsp` and `regs` (the timers and the pending bits of the held bursts) moved, shown by the `held` targets and by dumps
  of the first held burst; the guest, whose bursts never find its ARM7 held, stayed identical —; G moved the guest's
  `samples`, `regs`, `sh4` and `dsp` and its exit hash's `sh4`, `ram` and `aram` (the program), justified by J3, the
  oracle above, and by G1 to G5 of the cut; E8a and E8b left every line and hash identical against B4, also with the
  debugger at work on the DSP and with captures (`dspn.*`, `sgcn.*`).
- The M2 round's result (its gate, M2G, on one host with one compiler, both sides built again with the same settings):
  the sound lines of the guest (N = 2000) and of 2ndmix (N = 40000), in both `aica.NoBatch` settings on the JIT and on
  the interpreter, and the guest's exit state hash at 90 vblanks, of the end of the round (`6fe01759`) equal those of
  the tree before E6b (`5a3ad3d6`, E6a's merge): 36 of 36 lines identical (the default build twice, the build with
  `NULLDC_DEBUGGER=OFF` once), and the build without the debugger gives the lines of the default build. The guest's
  `samples` hash is the same on both engines and differs between the two `aica.NoBatch` settings, as it should.
- The M3 round's result (its gate, M3G, on one host with one compiler, both sides built again with the same settings):
  the sound lines of the guest (N = 2000) and of 2ndmix (N = 40000) on the JIT and on the interpreter, and the guest's
  exit state hash at 90 vblanks, of the end of the round (`9fbc988a`) equal those of B4, G's merge with the
  integrator's text fixes (`fe90f9f1`), in all seven fields and all parts: 12 of 12 entries identical in the default
  build (two runs each) and in the build with `NULLDC_DEBUGGER=OFF`, which gives the lines of the default build at
  base and tip. The guest's `samples` (`7f5e196ea8d164a6`), `regs`, `arm7` and `dsp` (`d26e9e100def99cc`) are the same
  on both engines, `sh4` differs between them as it always has; 2ndmix prints the silence of 40000 ticks
  (`386b438fa36ca325`) and the same `dsp` (`2ac453da894bfdc6`) on both engines. The oracle targets
  (`debugger.sound.oracle.dsptest.*`), the `held` targets (`debugger.sound.held.dsptest.*`), the replay of the UI's
  module (`aica.dsp.wasm.devtools`) and the twenty neutrality targets are green in the gate's two ctest runs.

### 5.7 No regression, and performance

- With `NULLDC_DEBUGGER=OFF`, with the debugger compiled in and the server off (`Debugger:Enabled=no`, and the default
  of every automated run), and with the server on and no client, the state hash of a headless run
  (`config:Exit.StateHash=yes`) is identical, part for part, for the workloads `tests/system` holds (2ndmix 600 and 4000
  vblanks, hello 300), on the JIT and on the interpreter; and so is the harness line of 5.6. A BIOS boot is among them
  only with a data path for a BIOS the owner names (`M3-ROUND.md` M3-D38; the owner's answer to M3-Q4, "Not in M3"):
  no data path of the tests project names one, and no package since E5 has run one. With the server
  on and no client the only costs are the attention-word test per slice, the latch test in `UpdateINTC()`, the event
  byte tests, and `dbg_sh4_WantsCheck` at decode time (gate B11).
- **Hashes per host** (the platform round; `PLATFORM-ROUND.md` 4.3, PL-D3, PL-D83). A state hash or a sound line is good
  for one build on one host (5.6), so the platform round held one baseline, P0 (RB's, equal to M3's B4: 38 rows of state
  hashes, 12 sound lines), taken again on its commit by every package and compared on Linux x86_64 only, where every
  package's gate kept it identical. On other hosts the hashes are reported, not compared with P0: on the Windows host
  both engines end in Linux's hashes in every field (W64a, 16 runs; at PLG the guest of the zip's smoke run, the JIT at
  90 vblanks, `6a1e1008459e6ab3`); on the Mac and on Linux arm64 the interpreter's equal Linux's, and the arm64
  recompiler's are its own, the same on both, which `macos.hashes.jit` and `linux_arm64.hashes.jit` hold as the one pair
  of references in the tree (a host whose recompiler computes otherwise needs references of its own): since A64
  (`PLATFORM-ROUND.md` PL-D86, the owner's acceptance of 2026-10-23) the guest at 90 vblanks `6a1e1008459e6ab3`, equal
  to the x64 recompiler's, and 2ndmix at 600 `56bd7274490f91aa`; until A64, whose slice model moved every arm64 slice
  end, they were `c9bb6e60601ab606` and `6ab1933550d37163`. The shipped binaries meet P0 too (PL-D83): the release
  AppImage's binary (Release, the prefix map, the embedded UI) runs P0's default rows (2ndmix 600 and 4000, hello 300,
  the guest 90, both engines) equal to P0, in CIe's gate and in PLG's (at PLG: the 10 rows of P0's default build equal
  and its 6 sound lines identical).
- With an SH4 breakpoint listed but never hit, the state is identical too (R5 and R13 on a real workload): the runner
  mode `--idle-breakpoint <address|symbol>` (4.2) connects at the `StartPaused` stop, mutes the `sh4` category, adds a
  breakpoint on a hot address — one the program reaches in every frame (below) —, resumes and stays connected until
  the emulator ends (`config:Exit.AfterVblanks`) and prints its exit line.
- A neutrality test of the debugger's writes keeps to the rule of 5.3 (`writesrun.neutral`): it writes at stops latched
  at a slice end and restores what a restore makes whole, and writes no PC, no SR at a code breakpoint and no CPSR of
  the ARM7.
- In ctest (E5, `tests/debugger/contract/`), three tests, each on the JIT and as `.interp` on the interpreter, each
  running 2ndmix for 600 vblanks (`config:Exit.AfterVblanks=600`, `config:Exit.StateHash=yes`) twice and requiring the
  two state hashes to be equal, part for part: `debugger.contract.hash.off` (`Debugger:Enabled=no` against no
  `Debugger:` key at all, the default of an automated run; the test also requires that the run without a `Debugger:`
  key prints no `Debugger: http://` line, and that a third run, with `Debugger:Enabled=yes , Debugger:Port=0`,
  prints it on stdout (that run has only the line to show and runs 30 vblanks, not 600) — this is the test of the
  default of 2.11, which a hash cannot show, since a server without a
  client leaves the hash alone), `debugger.contract.hash.noclient` (`Debugger:Enabled=yes`,
  `Port=0`, no client, against `Enabled=no`) and `debugger.contract.hash.idlebp` (the runner's `--idle-breakpoint`
  connected, against `Enabled=no`). The address of `.idlebp` is a symbol of 2ndmix.elf that the program reaches in
  every frame, `stars_one_frame` (the function that moves and draws its star field, called from the main loop); the
  CMake line of the test names it and gives the runner `--elf`. `debugger.contract.grace` and the selftest
  `core.guest` stop on the same function.
  `tests/debugger/contract/README.md` records the symbol and how it was shown to be reached (an unmuted breakpoint on
  it stops within one second of a resume): the test itself cannot show it, since a muted breakpoint reports nothing,
  and an address that is never executed would pass it vacuously (the entry `0x8C010000` runs once and is not such an
  address). A test makes its runs itself (two; `.off` three); no hash is stored in the tree, as in
  `tests/system` and in the mode `same-hash` of the selftests. They run on the emulator the system tests build and are
  skipped as the other contract tests are (5.4; `.idlebp` needs the UI repository, the other two only
  `NULLDC_ELF_DIR`). The comparison against a `NULLDC_DEBUGGER=OFF` build, and the 4000-vblank and hello workloads of
  the first item, are the verifier's, made once per package (one extra build). For E5 it was made on the
  merged tree: 2ndmix for 600 and for 4000 vblanks and hello for 300, on the JIT and on the interpreter, each
  compared between the tree before E5, the build with `NULLDC_DEBUGGER=OFF`, a run with no `Debugger:` key, with
  `Enabled=no`, and with `Enabled=yes , Port=0` and no client, and for 2ndmix 600 also with a muted idle breakpoint
  and a client connected: 32 comparisons, none different.
- Instruction counts (user mode, `perf_event_open`) of the build with the debugger against the build without show no
  unexplained difference: what is there is the one attention-word test per slice (measured +1196 ppm on the JIT and
  +718 ppm on the interpreter over 2ndmix 600) and the per-decoded-instruction query.
- Known limits that these gates meet (2.3.7 item 11): event breakpoints latched inside SLEEP waits, and single steps
  on the JIT, change the state hash, which is the documented skew and not a regression. Two limits of this list
  went with package J1 (2.3.7 items 9 and 10): a gate may stop and start the emulator thread as often as it likes
  in one process on the JIT (2200 cycles ran with no cache clear; `debugger.selftest.hostcycles` holds 600 in
  ctest), and a ThreadSanitizer run no longer reports the `CpuRunning` word. J1 itself met the gates of this
  section: base, head and the build with `NULLDC_DEBUGGER=OFF` end in the same state hashes for 2ndmix 600 and 4000
  vblanks and hello 300 on both engines (24 comparisons, none different, the `-DHAS_TA_LLE=OFF` build among them);
  the emitted x64 code of a 600-vblank run is the same instruction for instruction once host addresses are named
  by symbol; and speed did not move (`tests/perf`, the `none` renderer: within 1 %, inside the run-to-run spread).
- Sanitizer runs, as E5, J1 and their verifiers made them. **The build recipe:** the tree's `nulldc3/cmake/config.cmake`
  overwrites `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` with GCC and Clang (with MSVC it appends to CMake's defaults since
  the platform round, `PLATFORM-ROUND.md` PL-D32), so sanitizer flags go into `CMAKE_<LANG>_FLAGS_<CONFIG>` (for
  example `CMAKE_CXX_FLAGS_RELWITHDEBINFO`), or into the name of the compiler as the M3 round's scripts give them. A
  build that was given them in `CMAKE_CXX_FLAGS` links the sanitizer's
  library, instruments nothing and reports nothing; whoever makes such a run counts the compile lines that carry
  the flag first (389 of 389 in J1's build). A ThreadSanitizer build cannot run the x64 JIT at all (it ends
  at `recSh4 Init`, before any debugger code runs), so every ThreadSanitizer statement of this design is about the
  interpreter: there the M1 gate, `debugger.contract.grace`, `.slow` and `.config` and the session `core` gave no
  report for E5, and since J1 the session `emu` (three times), `hostcycles` and the M1 gate give no report of any
  kind — the run-control package had recorded three to four a run, the write of the run flag among them. The control: with the word of
  `IsRunning()` made a plain `bool` the same run reports it. With AddressSanitizer and UBSan, on both engines, no report names a debugger file; the tree's own UBSan
  lines (`_vmem.h`, `regalloc.h`, the ARM7 core, `sh4_sched.cpp`) have to be filtered for a harness to pass, and the
  leak check finds a leak in `reios_loadElf` on a run that exits normally. Those are the tree's, outside the
  debugger.
- Limits of what the tests of E5 can reach on the development machine: `registersStale` and `no-game` over a socket
  (5.5.4); a real MSVC build (5.8: made since the platform round, on the Windows host and in CI). (The bound of 256
  watches was one until M2: the M2 tree has 2,940 registers, the
  M3 tree 2,949, and `core.tables` and S08 reach it since E7b.)
- **The gate of a package, from M2 on** (`M2-ROUND.md` A.0): five builds — the default, `-DNULLDC_DEBUGGER=OFF`,
  `-DTARGET_NO_REC=ON`, `-DHAS_TA_LLE=OFF`, and the generic host (an unknown `CMAKE_SYSTEM_PROCESSOR`, as package
  P-desktop built it), which compiles and whose 2ndmix state hash is what it was; and all ctest targets green twice,
  none skipped, the number stated, with the tests project configured so that missing data is a failure
  (`-DNULLDC_TESTS_REQUIRE_DATA=ON` and every data path: the programs, the UI checkout, `wren7-rtl`, `caique-rtl`,
  `shrike4-rtl`, and the two vector checkouts `tests/vendor/sh4-tests` and `tests/vendor/arm7di-tests-dreamcast`,
  which a new worktree has empty). For a package on the sound path, the host cost in two checks: the compiled code of
  the path an ARM7 instruction takes is unchanged, and the user-mode instructions of a whole run of the sounding guest
  and of 2ndmix grow by at most 48 per sample (E6b: +27.1 to +27.4); and the ARM7 suites a second time with `Step` as
  its two halves (`arm7.*.halves`). Speed by CPU time, five runs of base and tip in turn, is held against the spread
  `tests/perf/BASELINE.md` gives for two rows of one binary (1.5 %) and decides nothing by itself. **"Grow by at most 48
  per sample" and the speed rule did not gate the M3 round** (the owner's answer to `M3-ROUND.md` M3-Q2, "don't mind
  perf for now"; M3-D15): its packages measured and reported host instructions a sample and speed by the method of the
  cut's 5.4 — counts against a base without the `dsp` field on runs the harness does not fold (the exit runs of this
  section: the guest for 90 vblanks, 2ndmix for 4000; samples from the exit line's `sched` / 145124 x 32), counts
  against a base with the field on harness runs, speed by `perf.py --config` as the ratio of the medians of rounds of
  base and tip in turn in one session — and M3's speed is recorded below. Whether a later milestone bounds speed again
  is that milestone's cut. A contract gate
  "unchanged" compares the result line (for M1: 18 passed, 0 failed, 0 partial, 0 not applicable, 0 invariant
  violations); the tick count differs from run to run. The state hashes and sound lines a gate compares are taken from
  base and tip built on one host with one compiler, and none is stored in the tree.
- ThreadSanitizer in M2: the sessions `arm7` (both `aica.NoBatch` settings), `writes` and `core`, `writesrun.neutral`,
  `arm7n.hoststop` and the parts of S16 on the guest give no report on the interpreter. The part `arm7` against 2ndmix is
  red on a ThreadSanitizer build by wall-clock time alone (the runner waits 10 s for a paused tick at a breakpoint,
  which the instrumented interpreter does not reach in time), at E6b's merge and behind it alike, with no report
  written; the round's gate ran the contract parts on the guest only. At M2G (the tip `6fe01759`, 392 of 392 compile lines instrumented) the second run of the M2 gate against the guest (`--mode m2 --only S00,S16` with both required notes) passed twice on the interpreter, 2 scenarios each, with no report of any kind.
- ThreadSanitizer in M3: the sessions `dsp` (E8a) and `sgc` with `mem.binary.*` (E8b), and the contract runs on the
  guest — `m3dsp` with its note, the `--only S00,S21` run with its note and the M2 gate's second run with both notes —
  give no report of any kind on the interpreter, at E8a's and E8b's gates and again at M3G (every compile line of the
  build instrumented; the recipe above).
- **Speed at the end of M2** (the round's gate, M2G): `tests/perf` on the `-DHAS_TA_LLE=OFF` build with the `none` renderer, 2ndmix for 60 emulated seconds, the end of the round (`6fe01759`) against the tree before E6b (`5a3ad3d6`), ten rounds with the order alternated, by CPU time, on a quiet machine. The JIT: median 1459.1 % against 1450.0 %, the tip **0.6 % faster** (spread of the runs 1.1 % tip, 8.8 % base, from two low runs of the base in rounds 1 and 6). The interpreter: median 566.1 % against 586.1 %, the tip **3.4 % slower** (per round from 1.1 to 5.2 % slower, every round; spread 1.7 % tip, 3.0 % base). A loss above the 1.5 % of `tests/perf/BASELINE.md` stands on the interpreter, so the two binaries were compared with `perf` (user mode, three runs each): the tip executes 0.07 % more instructions (104.835 G against 104.763 G: 71 M, which is E6b's measured +27 host instructions a sample over the 2.65 M samples of the run) in 3.8 % more cycles, with 3.5 % more branch misses (294.6 M against 284.5 M). The cycles go to the interpreter's dispatch: `ExecuteOpcode` (+0.42 G), `SH4IInterpreter::Loop` (+0.57 G, and 8.8 M of the 10 M further branch misses), the handler `i0011_nnnn_mmmm_0100` (+0.23 G) and `ARM7Backend::Step` (+0.14 G), each with the instructions it had and a higher CPI. The machine code of those functions is the same instruction for instruction in both binaries (the SH4 interpreter's sources did not change in the round); what changed is where it stands: the code of the interpreter moved by 0x4FA60 bytes, which is 32 bytes off its place within a 64-byte line (`ExecuteOpcode` and the handlers from offset 32 to 0, `Loop` from 48 to 16; `ARM7Backend::Step`, also the same code, from 16 to 32). The loss is one of code placement, not of work the round added; E6b's host-cost checks of the ARM7 path stand (+27 host instructions a sample, the ARM7's compiled path unchanged). No code was changed for it; aligning the interpreter's hot functions, or the profile-guided layout of PLAN track 3, would decide it. The logs are under `agents/nulldc3/m2g/logs/` (`perf.txt`, `profile/`).
- **Speed at the end of M3** (the round's gate, M3G; measured and reported, it gates nothing: the owner's answer to
  `M3-ROUND.md` M3-Q2): `tests/perf` on the `-DHAS_TA_LLE=OFF` build with the `none` renderer, 2ndmix for 60 emulated
  seconds, by CPU time, ten rounds in which B0 (R7's merge `b921699b`, the round's start) in its default setting (the
  batch render, what users ran before the round), B0 with `config:aica.NoBatch=yes` (the old per-sample path with the
  old DSP; `perf.py --config`) and the end of the round (`9fbc988a`) ran in turn, on a quiet machine. The JIT: medians
  1431.7 % (B0 default), 1266.3 % (B0 per-sample) and 1142.9 % (tip): the tip **0.798 of B0's default** (-20.2 %; per
  round 0.789 to 0.805) and 0.902 of B0's per-sample path (-9.8 %); the ratios make 1164.7 % and 1316.9 % of M2G's
  1459.1 %, for the record. The interpreter: medians 557.2 %, 533.1 % and 512.0 %: the tip **0.919 of B0's default**
  (-8.1 %; per round 0.914 to 0.934) and 0.960 of the per-sample path (-4.0 %); 520.1 % and 543.7 % of M2G's 566.1 %.
  The spread of each column is 1.3 to 4.3 %. **Host instructions a sample** (user mode, `perf_event_open`, three runs
  each, medians), on runs the harness does not fold (exit runs; the guest of R7's merge, without a DSP program, for 90
  vblanks, and 2ndmix for 4000; samples from the exit line's `sched` / 145124 x 32): W1, W2 and W3 together, G's merge
  (B4, `fe90f9f1`: their product code and nothing after it, since G changed only tests, documents and the gitlink)
  against B0's default, +3079.6 a sample on the guest and +3037.9 / +3038.8 on 2ndmix (JIT / interpreter), the
  per-sample path with the core and its sweep in place of a batch render of 32 samples without a DSP; for the record
  the guest with its DSP program +4362.2 (the program's 18 steps among it) and hello for 300 vblanks +3258 / +3262.
  The round's total (W1 to W3, E8a and E8b: the end of the round against B0's default, the same runs): +4019.4 /
  +4019.6 on the guest and +3977.6 / +3978.4 on 2ndmix; the guest with its DSP program +5302.2, hello +4198 / +4201.
  E8a and E8b together against G's merge, on harness runs (both fold `dsp`): +939.9 on the guest and +939.7 on 2ndmix,
  on both engines (the burst in its phase calls, the DSP's and the capture's flag tests); on the exit runs they are
  the difference of the two counts above, the same +940. W1, W2 and W3 measured their own steps: W1 0.82 (JIT) and
  0.91 (interpreter) of B0's default, W2 0.85 and 0.94, W3 0.99 to 1.01 of B2 (the cut's Appendix I). Nothing was
  changed for speed in M3; the logs are under `agents/nulldc3/m3g/logs/` (`perf.txt`, `perf/`, `icount-report.txt`).
- Speed: emulation speed with the server on and no client is within 2 % of the server off, 2ndmix, three runs each,
  measured in the `-DHAS_TA_LLE=OFF` build with the `none` renderer (or GL with `GALLIUM_DRIVER=d3d12`); the figure states
  its renderer. No figure is taken with the reference software renderers (D3). On a loaded machine wall time is not
  usable; the comparison is then made on instruction counts.

### 5.8 Windows

**As built** (the platform round: `PLATFORM-ROUND.md` 2.8 and 2.9, packages W64a, W64b and W64c, which are this
design's W64). The emulator and the tests project build with MSVC, x64 only (Visual Studio 2026, MSVC 14.51, on the
Windows host; in CI the runner image's newest MSVC, `windows-2025`): CMake's defaults kept and extended (`/EHsc /GR /W3
/utf-8 /bigobj /Zc:__cplusplus /permissive-`), the static CRT (no redistributable), `/Z7`, an 8 MB stack as Linux
threads have, `/CETCOMPAT:NO` (the recompiler leaves its main loop without `ret`), `/OPT:NOICF` (PL-D32;
`nulldc3/cmake/config.cmake`); CHD's LZMA and FLAC codecs and the 7z and zip archives are built (PL-D33); picoTCP (the
modem) stays GCC/Clang-only. `nulldc3.exe` is a console program (PL-D31) with the `.rc`'s icon group and version
resource (`FILEVERSION` `X,Y,Z,N`, the version string in `FileVersion` and `ProductVersion`, `CompanyName` "nullDC
Team"). The ABI and memory fixes (PL-D37): `Mainloop` and `RunBlock` save XMM6-15, the virtual memory layout is made
through placeholders (`VirtualAlloc2` / `MapViewOfFile3`, found at run time; Windows 10 1803 or later), the unwind
table is registered over the prepared code region, `_mm_setcsr` sets the host FP mode, and the check stub has its
shadow space and the Windows flush budget (W64b). What MSVC refused elsewhere was fixed in compile-only portability
fixes (PL-D68, each listed in W64a's report), and two runtime defects only MSVC showed were fixed with tests: the
interpreter's NaN, which MSVC folded into a quiet constant, is read at run time (`fixNaN`), and `test_guard`'s
`longjmp` no longer unwinds through generated code (`PLATFORM-ROUND.md` I.5). Headless, the no-dialog policy and the
fatal line: D10 and `docs/nulldc3/automated-runs.md`, "Windows".

**The gates passed on the Windows host** (W64b, W64c; again at the platform round's gate, PLG, on its merged tree): the
three builds (default, `-DNULLDC_DEBUGGER=OFF`, `-DTARGET_NO_REC=ON`); the tests project inside an outer
`nulldc3-winrun` (every program it starts runs inside one too), every target that runs green twice and the skipped
listed with their reasons (PL-D5; `tests/README.md`, "On Windows": 349 targets, 272 passed and 77 skipped, twice); the
contract gates M1 to M3 on the JIT, the interpreter and the forced fallback with Linux's numbers, the selftests
(`debugger.selftest.hostcycles` among them: J1's bound of thread starts is gone on Windows too), the sh4bp gates B1 to
B14 but B11 (its counters are Linux's `perf_event_open`), the sound tests. The x64 JIT sets `DebugChecks` in Windows
builds since W64b (`sh4-debug-native`, PL-D38: the gates of 2.3.6 passed there). The state hashes of 16 runs on the
Windows host equal Linux's in every field (W64a; a hash is good for one build on one host, 5.7, so this is reported, not
gated).

**By hand, the owner's** (PL-D40: no agent opens a window on the owner's desktop): the windowed start (the icon in the
title bar, the task bar and Alt-Tab; Explorer's Properties, Details: the version and "nullDC Team"), About, a game,
E4a's item — pause with the debugger from the UI, then close the window, with the debugger parked and with a pause
pending (on Windows the title call of the park loop is a sent message that waits for the UI thread) —, and the window
sessions of 6.1.2. The list per system is in the emulator PR of the platform round.

**History.** Until the platform round no Windows build had been made. E5 merged on two checks the development machine
could make: a syntax-only compile of its files with the MinGW-w64 cross compiler (`-std=c++20 -fsyntax-only
-fno-operator-names`, the tree's Windows definitions on the command line, a shim directory for `<Windows.h>`; the
scripts of E0's header review, `agents/nulldc3/e0/review-build/`), which `dbg_symbols.cpp` passed and five files could
not reach past `libswirl/build.h` (no 32-bit hosts since B64), and a review of every file against the MSVC rules of
section 2, which found a `std::min` call a `min` macro could take and a local named `small`. Until the gates of 2.3.6
had run on a Windows x64 build the x64 backend did not set `DebugChecks` there and Windows used the fallback with its
notice (E4b); J1's fix of the code region over thread starts (2.3.7 item 9) was correct on Windows by reading only.
The CI recipes P-desktop had kept (AppVeyor, Azure, Wercker) were reicast's and stale, and went with the platform
round's rebrand; the emulator's CI is the platform round's (CIe).

## 6. Work packages and file ownership

Rules:
- One worktree and one build directory per emulator package (`agents/nulldc3/wt/<pkg>`, branch `skmp/nulldc3-<pkg>`);
  commits as work lands; an integrator merges into `skmp/nulldc3`. Scratch, build directories and reports stay under
  `agents/nulldc3/`; a package deletes its clones and build trees before it returns.
- Ownership is by directory and file. A package edits only what it owns. The shared headers are frozen (D6): a change
  goes through the integrator, as a numbered header revision.
- Every package is verified independently (a verifier that did not write it), with fix rounds until it passes.
- D10 applies to every package and verifier: headless runs, one workflow at a time, `ninja -j8`, `ctest -j4`, at most 4
  emulator processes.
- State: **M1 is complete, and the two packages behind it are done.** The emulator's integration branch `skmp/nulldc3`
  has every package of round 1 merged — E1, E2, E0 (header revisions 2 and 3), E3, E4b, E4a, B64, header revision 4
  with the corrections of 6.1.1 (items 1 to 7 on branch `skmp/nulldc3-integ-v3`, item 8 in commit `bc924abf`) and
  the core, E5 (branch `skmp/nulldc3-dbg-e5`, head `58d0097b`, merged as `ceac25db`) — and behind them the PLAN
  package P-desktop (desktop targets only; merged as `5ac00392`) and J1 (the run flag as an atomic with header
  revision 5, the code cache over thread starts, `IsRunning()` from one word; branch `skmp/nulldc3-j1`, head
  `ab97adbb`, merged as `c0c81436`). On that tree the tests project has 190 ctest targets, green twice, and the M1
  gate of the contract runner passes against the emulator on the JIT, on the interpreter and with
  `Debugger:ForceFallback=yes`: 18 scenarios passed, 0 partial, 0 not applicable each. The UI repository has F0, F1,
  F2, the devtools round and the runner follow-up committed on `skmp/nulldc3` (head `22b358e`; 391 unit tests, 246
  e2e tests, the M1 gate on the mock with 16 scenarios passed, 0 partial, 0 not applicable).
- **M2 is complete** (`M2-ROUND.md`; 6.2, 6.3): header revision 6 (R6), the sounding guest (E6a), the resumable burst and
  the ARM7's run control (E6b), writes and reset (E7a), the M2 rows of the device tree (E7b) and the leftovers of the
  emulator packages (EXe) are merged on `skmp/nulldc3` (head `6fe01759`, 259 ctest targets); the UI packages F3a to
  F3d and their leftovers (EXu) are on the UI's `skmp/nulldc3` (head `d59336b`). The round's gate, M2G: passed (2026-10-15), on `devcast` `6fe01759` with the UI at `d59336b` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+d59336b`): the M2 gate passes against the emulator on the JIT, the interpreter and the forced fallback (19 scenarios passed, 0 partial, 0 not applicable each) and its second run against the sounding guest (2 passed, both notes); the 259 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix equal those of the tree before E6b, and the state hashes of 5.7 those of the tree before the round, in the five builds; `npm run check` and the e2e suite pass in a fresh clone; the smoke spec passes against a headless emulator on the guest.
  Then PLAN's rebrand (package N1: the binary `nulldc3`, portable by default) and D1.
- **M3 is complete** (`M3-ROUND.md`; 6.2, 6.3): header revision 7 (R7), the T5.0 core as the emulator's DSP (W1), the
  batch path out (W2), the AICA clocked whatever ARMRST says (W3), the guest's DSP program (G), the DSP's run control
  (E8a) and the SGC capture with the binary path (E8b) are merged on `skmp/nulldc3` (head `9fbc988a`, which moves the
  submodule `devtools/` to the UI's head; 307 ctest targets); the UI packages F4a to F4d are on the UI's `skmp/nulldc3`
  (head `066fcbc`). The round's gate, M3G: passed (2026-10-19), on `devcast` `9fbc988a` (its documents behind it, on the branch `skmp/nulldc3-m3g`) with the UI at `066fcbc` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+066fcbc`): run 1 of the M3 gate passes against the emulator on the JIT, the interpreter and the forced fallback (20 scenarios passed, 0 partial, 0 not applicable each), run 2 against the sounding guest on the JIT and the interpreter (S00, S17, S21: 3 passed with the note `sgc: captured with the arm7 held`; `--only S00,S21`: 2 passed with the note `dsp: stepped with the arm7 held`), and the M2 gate's second run on the changed guest (2 passed, both notes); the 307 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix and the guest's exit state hash equal B4's (12 of 12 entries, the default build and `-DNULLDC_DEBUGGER=OFF`), and so do the state hashes of 5.7 (38 of 38 rows, 2ndmix 600 in the five builds); `npm run check` (735 unit tests passed, 1 skipped; the four contract runs on the mock OK) and the e2e suite (401 passed, 1 skipped: the smoke spec, run on its own against the emulator) pass in a fresh clone of the UI; the smoke spec passes against a headless emulator on the guest; ThreadSanitizer of the sessions `dsp` and `sgc` and of the contract runs on the guest (interpreter) gives no report; speed is measured and reported (5.7).
- **The platform round is complete** (`PLATFORM-ROUND.md`; 6.3): its packages VCu (the UI's version, `.gitattributes`,
  its CI and Release; the UI's `skmp/nulldc3` = `7b88410`), RB, IC, VTe, W64a, W64b, W64c, MACa, MACb, LA64, CIe and A64
  (added at the owner's word before the gate: the arm64 recompiler's native debugger, 2.3.5) are merged on
  `skmp/nulldc3` (`12069163`, 327 ctest targets on Linux x86_64), with header revision 8 (H8-1 to H8-4; H8-5 A64's). The
  round's gate, PLG (2026-10-24), with the UI at `7b88410` and emulators that carry its bundle (`ui` of
  `debugger.describe` is `3.0.0-pre+g7b88410915`): on Linux x86_64, at the documents' head, the M1 gate 18 passed, M2 19
  and M3 20 on the JIT, the interpreter and the forced fallback (0 partial, 0 not applicable), M2's guest run 2 and M3's
  two guest runs 3 and 2 with their notes on the JIT and the interpreter; the 327 ctest targets green twice, none
  skipped; P0's 38 rows and 12 sound lines identical in the five builds; ThreadSanitizer of the guest's contract runs
  and of the sessions `dsp` and `sgc` (interpreter): no report; `npm run check` (793 unit tests passed, 1 skipped; the
  four contract runs on the mock OK) and the e2e suite (401 passed, 1 skipped: the smoke spec, run on its own against
  the emulator) in a fresh clone of the UI; the smoke spec against a headless emulator with the bundle; the x86_64
  AppImage made by `appimage.sh` (its five licences, its smoke run) and its binary on P0's default rows, equal. On the
  merged tree `12069163`: the Windows host 349 targets green twice (272 passed, 77 skipped with their reasons), the
  contract gates with Linux's numbers on every engine, the zip of `windows.ps1` (its entries, its smoke run through
  `nulldc3-winrun`, its version resource: `3.0.0.0`, the version strings, "nullDC Team"); the Mac 340 targets green
  twice (259 passed, 81 skipped), the canary in both shapes, the contract gates with Linux's numbers on every engine,
  the sh4bp suite (29 of its 30 targets; `b11` skips there), `macos.hashes.jit` on A64's references, the zip of
  `macos.sh` (unpacked with `ditto`, `codesign --verify`, its smoke run through `macrun.py`); the arm64 Linux host 330
  targets green twice (314 passed, 16 skipped), the contract gates in full on every engine, `linux_arm64.hashes.jit` and
  `.pages.refused`, the arm64 AppImage and its smoke run. No crash report on any system. Both workflows pass
  `actionlint`, and neither repository's tracked files name a machine of the project. **Next:** the cut of M4 (E9, F5).
  What is left for the next emulator package is 6.1.2 and PLAN's backlog of the M3 round and of the platform round.

### 6.1 Emulator, round 1 (M1)

| Pkg | State | What was delivered, and where it differs from v2's plan |
| --- | --- | --- |
| **E1** transport, build skeleton, UI embed | done | `dbg_net.h/.cpp`, `dbg_webui.h/.cpp`, `debugger.cmake`, `libswirl/debugger/cmake/`, `tests/debugger/net/` as planned. Beyond the plan: `Handler::OnBinary`, `SetStaticProvider`, the embed step checks every file against its `sha256`, a fuzz test and an interoperation test with the UI's client |
| **E2** disassemblers | done | `dbg_disasm.h/.cpp`, `tests/debugger/disasm/` as planned; the DSP line is the playground assembler's and is tested against it (100,193 steps) |
| **E0** shared headers | done, frozen | the headers of 2.1, the driver of the selftest, a stub per module, the debug virtuals of `sh4_if.h`, `hw/sh4/sh4_dbg.h`, the accessor declarations in the `hw/` headers. Differs from v2: run control is `Park()` with `PARK_RUN` / `PARK_EVALUATE` instead of stop-reason bits owned by the debugger; `dbg_events.cpp` is a module of its own; header revision 2 (twelve items, four of them declarations: `area0_DebugNvmemByte`, `SoundCPU::DebugGetInterrupt`, `dbg_sh4_SlotRuns`, the spellings of the stand-ins) and revision 3 (`ATTN_LATCHED`, the slice-end and SLEEP rules, the `IsRunning()` terms — superseded by revision 5, with which `IsRunning()` is one word —, the start of the cpu, `Term`) are part of the contract |
| **E3** registers, memory, device side | done for M1 | `dbg_regs.cpp`, `dbg_regs_tables.cpp`, `dbg_mem.cpp` (read), the accessor bodies and `DBG_EVENT` call sites under `hw/`, the selftest `regs`, `tests/debugger/regs/`, the sound regression harness. Differs from v2: the harness is `hw/aica/aica_hash.h/.cpp`; the flash region is what a guest read delivers; the ARM7 has its own view of the AICA register window (2.7); the SH4 on-chip registers behind a read function are computed or `"n/a"` (2.8); writers are M2 |
| **E4b** SH4 breakpoints and steps in the active backend | done | the port of the prototype: `dbg_sh4.cpp`, the decoder, SHIL, register allocator, driver, block manager, the x64 backend, the interpreter, the fallback; `tests/debugger/sh4bp/` and the selftests `sh4bp*`. Differs from v2: a check is a 15-byte call of a shared stub, not 60 bytes inline; a block holds at most 56 checks and its rest runs outside the cache; a compiled check exists only with `DebugChecks`, and the fallback's decode carries the checks itself (`DecodeLimits::no_compile`); the fallback continues the backend's slice counter; the interpreter's separate slot is handled by `dbg_sh4_SlotRuns`; eight gates beyond B1-B14. The Windows run of 5.8 is package W64 (6.3) |
| **E4a** emulator thread and run control | done | `dbg_emu.cpp`, the lifetime hooks and `dc_run` in `libswirl.cpp`, `SuperH4_impl`, the latch tests in `UpdateSystem()` / `UpdateINTC()` / SLEEP, the reios hooks, the selftest `emu` (nineteen tests) and the headless selftest runner `tests/debugger/selftest/`. Four verification rounds: 10,000 pauses, calls and breakpoint stops each leave the state hash unchanged; 7,382 exit runs with no hang. Differs from v2: the machine keeps its own stop reasons and the debugger's stop is `Park()`'s (2.2.4); `IsRunning()` does not turn false at the request (2.2.5; E4a built it from five words with the terms `resetting` and `stopping`, and J1 made it one word of the machine, `SuperH4_impl::thread_live`); a thread whose start is refused makes no park; the first park after a host stop taken in a debugger stop is a resume; the slice end tests the attention word first; the selftests run from ctest |
| **B64** no 32-bit hosts, no cached-SHIL backend | done | not a package of v2. Removed: the SH4 backends `jit/backend/arm32`, `x86` and `cpp` with their emitters, the 32-bit ARM7 and DSP JITs, the `CPU_X86` / `CPU_ARM` branches, the make build and what only built 32-bit targets (59 files, 23,700 lines); emitted x64 code, baselines and state hashes unchanged; the interpreter-only build (`-DTARGET_NO_REC=ON`) and the generic host path now build and boot. For this design: 2.3.5 has two JITs, 1.1 names the arm64 JIT (and the x64 JIT on Windows until 5.8), 2.5.3 the ARM7 JIT of arm64 |
| **E5** core | done | below: the core thread with the dispatcher, the tables and the tick builder, `dbg::Init` / `Term`, the event ring, the symbol table, the unwinders, the selftest `core`, `tests/debugger/symbols/` and `tests/debugger/contract/`. Two verification rounds; the first found three gaps in the tests and no defect of the core. Where it differs from the plan: "As built", below |

**E5 — the core (done).** It completed M1. Branch `skmp/nulldc3-dbg-e5`, 19 commits, head `58d0097b`, merged into
`skmp/nulldc3` as `ceac25db`. The table and the list below are the plan the package was given, kept as the record,
with what was delivered added where a row spoke of something still to come; "As built", behind them, has every
point where the delivered core differs from the plan or where the plan was silent.

| | |
| --- | --- |
| Owns | `libswirl/debugger/dbg_core.cpp`, `dbg_main.cpp`, `dbg_events.cpp`, `dbg_symbols.cpp`, `dbg_callstack.cpp`, `dbg_selftest_core.cpp`; a private header `libswirl/debugger/dbg_core_private.h` (new, not frozen, no JSON types; included by `dbg_core.cpp`, `dbg_main.cpp`, `dbg_events.cpp` and `dbg_selftest_core.cpp` only): the tick period and the full-tick count that `dbg::Init` read, handed over before `core::Start` (`void core::Configure(u32 tickMs, u32 fullTickEvery)`, on (U); a core that was never configured has the defaults of 2.11); the snapshot of the event ring that `dbg_events.cpp` gives the tick builder and the direct append of the grace note (2.6); and what the selftest calls — handing one request text to the dispatcher and getting the response text (in place on (E), and through `dbg::emu::Call` from a thread the test started: 5.3), the describe text, whether the core thread was started (the first check of every `core` test: it must not be), and setting the number of connected clients for a session without a server; `tests/debugger/symbols/`, `tests/debugger/contract/`; its lines in `tests/CMakeLists.txt`, `tests/debugger/selftest/CMakeLists.txt` (`nulldc_debugger_selftest(core ...)`) and `tests/README.md`; in `libswirl/utils/run_control.h/.cpp` the one accessor `bool runctl_automated()` and nothing else |
| Must not touch | `hw/`, `jit/`, `reios/`, `libswirl.cpp`, the frozen headers (header revision 4 was the integrator's and is applied), the files of E1 to E4b. **One exception:** the 1 MiB bound has to be answered with close code 1009, the close frame on the wire before the socket is closed, also while the client is still sending (3.6); if the transport of E1 does not do that, E5 may fix it in `dbg_net.cpp`, in a commit of its own, with a test in `tests/debugger/net`. The exception was not needed: the transport does it (2.11), and `dbg_net.cpp` is untouched |
| Depends on | E0-E4b and B64 merged, header revision 4 and items 1 to 7 of 6.1.1 applied; item 8 of 6.1.1 applied (the register `ENABLED` of `dc.aica.arm7`: the integrator's, commit `bc924abf`, in front of E5); for its gate the runner of the devtools round (4.2.1) with the follow-up of 4.2.2 (`22b358e`). All of it was there when E5 started. No open question stands in front of it: of what `DESIGN-V3-OPEN.md` still lists, the host for arm64, the name of the config section and the submodule are not E5's, and `StartPaused` with a server that was enabled and cannot be started is fatal as under `Required` (2.11) |
| The control-surface rule (D8) | the server is the control surface, not only a debugger: it works with no game loaded (every request has its answer in `NoMachine` and `Stopped`, 2.2.1); nothing in the core assumes that a client is a debugging session (2: connecting a client stops nothing; what depends on a client is the list of 2.9); the config keys stay under `Debugger:`; no method, table or message is shaped so that settings, lifecycle and input methods (track 10) could not be added beside it (3.0, extensions) |
| The automated-run default | in an automated run (`runctl_automated()`: headless, or one of the three `Exit.*` keys that end a run) and in a selftest session (`dbg::selftest::Requested()`) the server is off unless `Debugger:Enabled` is set explicitly (2.11). Nothing of track 1 writes a config key (2). The automated-run half is pinned by `debugger.contract.hash.off` (5.7: the port line is absent without a `Debugger:` key and present with `Enabled=yes`); the selftest half shows only with a window (5.3) and is reviewed, not gated |
| Keys that need a server | `Debugger:Required=yes` or `Debugger:StartPaused=yes` while the server is not enabled is a configuration error: one line on stderr and exit code 2, decided in `dbg::Init` directly behind `Enabled`; a selftest session is excepted for `StartPaused` (2.11, 2.2.9). Pinned by `debugger.contract.config` (5.4), which also has the other half of 2.11, a server that was enabled and cannot be started (three runs on a taken port); `debugger.selftest.startpaused` sets `Enabled=yes` for it (6.1.1 item 7) and the sessions `sh4bp*`, which set `StartPaused=yes` without a server, keep passing as they are |
| The REIOS trap opcode | `0x085B` is executed by the opcode table as an HLE call and is not an SH4 instruction: `dbg_disasm` prints it as `.word 0x085b` and reports it not `valid`; the core's `state.getDisassembly` line for it is `.word 0x085b ! reios trap` (3.3), a step on it is allowed (2.3.2), and the selftest's opcode cross-check excepts exactly this encoding |
| Selftest (`core`, 5.3) | `core.opcodes` (disassembler against the opcode table, all 65536 encodings), `core.tick` (JSON rules, strings that are not UTF-8, full and running ticks, watches), `core.tables` (ids, bounds, watch resolution, categories, active mask and event gates with and without a client), `core.events` (the ring), `core.lifetime` (`Stopped`, `guest-exit`, `NoMachine`), `core.guest` (symbols, callstacks, `breakpointId` on the booted ELF); as `debugger.selftest.core[.interp]` in ctest |
| Gate | `npm run contract -- --url ws://127.0.0.1:<port>/ws --profile emulator --elf <path> --milestone M1` passes (5.5.3) against the emulator started as in 5.4, on the JIT, on the interpreter and with `Debugger:ForceFallback=yes` (`debugger.contract.m1*`); `debugger.contract.grace`; `debugger.contract.config` (the configuration errors of 2.11); `debugger.selftest.core[.interp]`; `tests/debugger/symbols`; the whole tests project green (`ctest -j4`); 5.7: `debugger.contract.hash.off / .noclient / .idlebp[.interp]` (state hashes with the server off, with the server on and no client, with an idle breakpoint); the automated-run default (`debugger.contract.hash.off`: no `Debugger: http://` line without a `Debugger:` key, the line with `Enabled=yes`); and, by the verifier, once, the same hash with the debugger compiled out (`NULLDC_DEBUGGER=OFF`, one extra build); both builds (`NULLDC_DEBUGGER` ON and OFF); the package files compile without `-fpermissive` (g++ and clang++); Windows (5.8): a syntax-only compile of the package files with the MinGW-w64 cross compiler the development machine has, made as the header review of E0 made it (`-std=c++20 -fsyntax-only -fno-operator-names`, the tree's Windows definitions on the command line, a shim directory for the header names whose case differs; the scripts are under `agents/nulldc3/e0/review-build/`); a file that a tree header keeps from compiling that way is named in the hand-over and reviewed instead; and a review of every package file against the MSVC rules of section 2. E5 merges on these two; the real MSVC build is made before the first push, and the Windows run of the breakpoint gates is W64's (6.3), not E5's. **Passed**, on head `58d0097b`, which is the merged tree: the M1 gate with 18 scenarios passed, 0 failed, 0 partial, 0 not applicable and no invariant violated on each of the three engines; 187 ctest targets, twice green, none skipped; the state hashes identical with the server off, on without a client, with an idle breakpoint and compiled out (5.7); ThreadSanitizer runs on the interpreter without a report (5.7); the Windows item as 5.8 records it. Added by the fix round of the verification: `debugger.contract.slow`, the runs of `debugger.contract.config` on a taken port, and `debugger.contract.grace` on a breakpoint that is reached in every frame (5.4) |

What E5 implements, by where it is specified (all of it delivered):
- `dbg_main.cpp`: `dbg::Init` (2.11), in this order: the default of `Enabled` (`no` in an automated run and in a
  selftest session: `runctl_automated() || dbg::selftest::Requested()`) and the key; **with the server not enabled:
  if `Required` is set, or `StartPaused` is set outside a selftest session, one line on stderr that names the key
  and `exit(2)`, with no `nulldc-exit` line (a configuration error); otherwise return, nothing is started**; the
  other keys with their
  out-of-range rules; the `Bind` rule, decided before anything is started (a loopback address is one of the texts
  `127.0.0.1`, `localhost`, `::1`, compared without regard to case; nothing is resolved); the `net::Options`;
  `core::Configure(tickMs, fullTickEvery)` (the private header), `core::Start(server)`,
  `server.Start(options, core::NetHandler())`; the line `Debugger: http://<bind>:<port>/` on stdout, flushed, with a
  `Bind` text that contains a colon in brackets. A
  server that cannot be started — any `false` of `core::Start` or `net::Server::Start` (a failed bind, an address
  that does not resolve, a `Port` out of range, a thread or the socket library that cannot be set up), or a
  non-loopback `Bind` without `AllowedHosts` — has `core::Stop()` called where `core::Start` had succeeded, so that
  no core thread and no listener stay behind, and is one line of `dbg::Init` on stderr, behind the transport's own line where the transport failed; then, under `Required` — or under `StartPaused` outside a selftest session (2.11) —
  `exit(2)` with no `nulldc-exit` line, and otherwise the emulator runs on as without `Enabled`. `dbg::Term` (2.2.5) stops what
  `dbg::Init` left running, and nothing when it left nothing. `json.hpp` is included by `dbg_core.cpp`,
  `dbg_main.cpp` and `dbg_selftest_core.cpp` only (2).
- `dbg_core.cpp`: the core thread (C) with its queue and timers (the periodic tick, the grace period; the capture
  timeout from M3), with the period and the full-tick count that `core::Configure` was given; the `net::Handler`
  whose callbacks only enqueue; the dispatcher with the envelope rules of 3.0 (members of a request and keys of
  `params` it does not know are ignored), the error precedence of 3.0 and the channels of 3.5; the methods of M1
  (3.3), every other method of this section answered `-32004`; the tables (breakpoints, watches, categories: 2.3.3,
  2.8) touched inside calls and the listener only; the
  enabled sets and the active mask handed to `dbg::emu` (`SetSh4Breakpoints`, `SetEventBreakpoints`, `SetActiveMask`)
  inside the same `CC_TABLE` call that changed them; `debugger.describe` (3.1: three targets, the M1 capabilities, the
  engine capability from `emu::GetSh4Engine`, the tree from `dbg::regs::Nodes`); the tick builder (3.2, 2.8, 2.9:
  registers through `dbg::regs::Read` and `FormatValue`, watches, callstacks through `dbg::callstack::Unwind` with
  `location` in lower-case hex (2.10), the notice lines from `RunState::notice`); the listener `core::OnRunState`; `core::BuildTickText`; the order rule of 2.9;
  the slow-client rule; the `client` text of a describe as one line on stdout (2.11); memory and disassembly through
  `dbg::mem` and `dbg_disasm` (a slice is cut at the target's `addressMax` before `dbg::mem::Read` is called, which
  would wrap: 2.7; `DisasmCtx` with `dbg::symbols` and
  `dbg::mem` behind its callbacks; the SH4's `fp_mode` from FPSCR; the DSP's MPRO words through
  `DSP::DebugGetProgramStep`); every string of guest or host origin made printable ASCII before it is serialised (3.0,
  JSON emission, item 9); `breakpoints.add` in the order of 3.3.
- Run control (2.3.2): `control.pause` answers from `emu::GetRunState()` and calls `emu::RequestPause()`;
  `control.runUntil` is a `CC_EXEC` call of `emu::Resume()`: `-32003` returned by `emu::Call` (the thread runs, the
  closure did not) becomes `{}`; `-32003` returned by `emu::Resume()` inside the call (an action is already armed for
  the stop) is answered in-band; the steps are a `CC_EXEC` call of `emu::Step(target, kind, &why)`, whose code goes back
  in-band (`cannot step: <why>`); for `arm7` and `dsp` the target check answers `-32004` before the call while paused,
  and `-32003` while running.
- `dbg_events.cpp`: the gates, `EventSlow`, the ring (2.6), and the two entries of the private header: the ring's
  snapshot for the tick builder, and the direct append with which the core records the grace note while every gate is
  closed. The core rewrites the gates when a client connects or disconnects and when event breakpoints change; "a
  client is connected", here and for the active mask, the periodic tick and the grace period, is the core's own
  count of open clients from `OnOpen` / `OnClose`, never `net::Server::HasClients()` (2.6).
- `dbg_symbols.cpp`, `dbg_callstack.cpp`: 2.10, to the contracts of their headers. The ARM7's `[]` goes with the
  register `ENABLED` of `dc.aica.arm7` in the same tick (2.10, S11); both read `SoundCPU::DebugEnabled()`, and the
  register itself was there before E5 started (6.1.1 item 8).
- The 1 MiB bound of an inbound message (3.6, 2.11): it is answered with close code 1009, and the close frame is on
  the wire before the socket is closed, also while the client is still sending; S12 sees 1006 otherwise. The
  transport is E1's, and it does this as built: E5 showed the behaviour with its gate (S12) and with a probe (the
  close frame arrives while an 8 MiB and a 32 MiB message are still being sent), and did not need the exception of
  its "Must not touch" row.

**As built.** Where the delivered core differs from the plan above, or where the plan was silent. The rule: the
verified implementation is right unless a decision says otherwise. Every point is in the text of the section named;
Appendix A.3 ("E5 as built") has the same points with their outcome.

1. The slice example. On the emulator `{sh4, 0xFFFFFFF0, 32}` answers `-32010`: nothing in the last sixteen bytes of
   the address space is readable. The no-wrap rule holds, and `core.tick` pins it that way (2.7).
2. The bound of 256 watches cannot be reached on the M1 tree: 247 registers, and no expression is stored twice.
   `core.tables` covers the 64 expressions of a call and the 256 characters of an expression and prints a note (5.3,
   5.7).
3. `core.tick` is a machine test, so its ticks are running ticks; what holds for a paused tick is checked in
   `core.guest` and `core.lifetime` (5.3).
4. `tests/debugger/selftest/README.md` had no section for the session `core`: the file was not E5's. J1 wrote it
   (6.1.2).
5. A server that cannot be started is two lines on stderr where the transport failed: the transport's own and the
   one of `dbg::Init` (2.11).
6. A connect and a disconnect each make a `CC_TABLE` call for the event gates and the active mask, a service
   excursion while running, made again after 250 ms when (E) did not take it. The table call of the grace expiry and
   the release behind it are made again after 1 s (2.6, 2.9).
7. The grace note is appended inside the table call of the expiry, not on the core thread, so that the scheduler's
   time is read where guest state may be read. Its text is fixed and does not say whether a stop was released (2.6,
   2.9).
8. The protocol note is logged for every describe that names another protocol number, not once per connection
   (2.6).
9. The `Debugger: client` line is printed for the first well-formed describe of a connection that carries a
   `client` string, before the request is dispatched: also when that describe is then answered busy (2.11).
10. The stack scan of the unwinders ends at the end of the memory the stack pointer is in; it does not go on into a
    mirror (2.10).
11. Several symbols at one address: a function names it before an object, an object before a symbol without a type,
    a global symbol before a local one. An absolute symbol counts as defined; since J1 it ranks behind a symbol of a
    section (2.10, 6.1.2).
12. `state.getDisassembly.context` has to be a number when it is present; its value is ignored (3.3).
13. Request ids are echoed in the number form of the JSON library, and an unsigned parameter may be written as an
    integral float (3.0).
14. The contract tests add `config:Dynarec.Enabled=yes|no` to the recipe of 5.4 and nothing else (5.4).
15. The third run of `debugger.contract.hash.off`, which has only the port line to show, runs 30 vblanks, not 600
    (5.7).
16. `debugger.contract.grace` needs only `NULLDC_ELF_DIR`, not the UI repository: its client is `wsclient.py` (5.4).
17. The state of `dbg_core.cpp` is allocated once and never destroyed, so an `exit()` that does not pass
    `dbg::Term` finds no joinable thread (2.11).

What could not be done: a real MSVC build (5.8: the syntax checks stand in for it until the first push); `NoMachine`
over a real socket, since a headless run without a game is an error exit (5.5.4: `core.lifetime` tests the state at
the dispatcher); `registersStale`, which needs the arm64 JIT (5.5.4, 5.7). The slow-client rule, which the first
hand-over listed here, has its test since the fix round (`debugger.contract.slow`, 5.4).

The private header as delivered (`dbg_core_private.h`): `core::Configure`, `core::Started`,
`core::HandleRequestText`, `core::DescribeText`, `core::SetClientCountForTest`, `core::ActiveMaskForTest`,
`core::PrintableAscii`, and `events::Snapshot` / `events::Append`. No frozen header was changed and none needs a
comment for any of the points above (`DESIGN-V3-OPEN.md`, section 3); no change of the runner was asked for.

Verification: two rounds. The first found no defect of the core and three gaps in its tests: nothing held what the
core thread does with its own client count (the grace test had its breakpoint on the entry, which is never reached
again), nothing tested a server that was enabled and cannot be started, and the slow-client rule had no test. The
fix round closed all three with tests and changed no source of the core. The second round passed, with one gap
noted, which J1 closed (6.1.2). Where the mock of the UI repository and this server
differ is listed in `tests/debugger/contract/README.md`; the design decides each: the server is right about unknown
keys of `breakpoints.setCategoryStates` (3.3), the text of `symbol` and the case of its hex (2.10), the ARM7's
memory (2.7), the note of the grace period and the protocol note (2.6); both forms are allowed for `sp` of a frame
(3.2) and for the order of a tick against the response of its request (2.9); and the code for a write to `ENABLED`
is 2.8's.

**6.1.1 Corrections revision 3 makes to merged code** — **applied**, all eight items. Items 1 to 7 were applied together with header revision 4
(comment-only; its list is kept in `DESIGN-V3-OPEN.md`), on branch `skmp/nulldc3-integ-v3`, which is merged into the
integration branch. Items 2 and 3 are worded as decisions 42 and 43 settled them, item 5 as decision 45 did. The
branch had applied items 2 and 3 in their first wording; the rewordings were made on the integration branch, behind
the merge: the rule of the regs test in commit `fbb01dc1`, and the README sentence together with the re-wrap of the
one comment line that header revision 4 left at 180 columns (`debugger.h`, the `Init()` comment; decision 45) in
commit `ecc4652c`. The whole tests project was run again on that head: 171 of 171 ctest targets passed (the 169 of
before and `debugger.selftest.regs[.interp]`). Item 8 came with decision 31 and was applied by the integrator in
front of E5, in commit `bc924abf` (branch `skmp/nulldc3-integ-v3b`, which E5 was based on): 171 of 171 again,
twice, with the determinism hashes unchanged.
1. `dbg_regs_tables.cpp`: the widths of `dc.aica.dsp` become `STEP` 8, `MDEC_CT` 16, `RBP` 16, `RBL` 8 (as built: 16,
   32, 12, 2), with the expectations of `tests/debugger/regs` and of the selftest `regs`.
2. `dbg_regs_tables.cpp`: the `dc` node does not list `dc.debugger.dsp_forced` (nothing raises it, 2.5.5), with the
   expectation of `tests/debugger/regs` (`regs_test.cpp`, test `debugger.regs.tables`): **every row of
   `dbg_events.h` is listed by exactly one node, the node the row names, except `EV_DEBUGGER_DSP_FORCED`, which is
   listed by none.** The README of that directory says the same. (The first wording, "at most once", no longer
   noticed an event dropped from its node; decision 42 replaced it.)
3. `tests/debugger/sh4bp/README.md`: the two call-site rows that predate header revision 3 (the latch test of
   `UpdateSystem()`, the SLEEP test) say what 2.2.3 says. Its "Per slice" paragraph says that the two stand-ins of
   that directory keep the latch test in front of the attention word — the B11 cost was measured on that form —
   instead of claiming the order of `slice_end()` for them (decision 43). The stand-ins do not change.
4. `tests/README.md`: rows for `debugger.selftest.*`; the documented configure line names `-DNULLDC_DEVTOOLS_DIR=`.
5. `docs/nulldc3/devtools-integration-design.md` in the emulator repository, the tree's copy of this design, is
   replaced by revision 3 (it still named the removed backends and `core.mk`). The docs copies beside it are part
   of this item (decision 45): `devtools-integration-open.md` (`DESIGN-V3-OPEN.md`),
   `devtools-integration-decisions.md` (`DESIGN-V3-DECISIONS.md`) and `PLAN.md`.
6. `tests/debugger/selftest/CMakeLists.txt`: `nulldc_debugger_selftest(regs ELF 2ndmix.elf)`, which makes
   `debugger.selftest.regs[.interp]`. The session `regs` of E3 was not a ctest target as built (the file registered
   `emu`, `sh4bp*`, `emuexit.*`, `emurun.*`, `emuhang.parked` and `startpaused`), and item 1 changes what it expects.
7. `tests/debugger/selftest/CMakeLists.txt`, the line of `debugger.selftest.startpaused`, and its row in
   `tests/debugger/selftest/README.md`. As built the test is a headless run without a session
   (`SELFTEST NONE CONFIG Debugger:StartPaused=yes`, `MODE parked`, `Exit.AfterVblanks=5`): it has no server, stays
   parked and is killed by its harness, which is the pass. With E5's `dbg::Init` that configuration is an error and
   the run would end with exit code 2 (2.11), which `MODE parked` reports as a failure. The line gains
   `Debugger:Enabled=yes Debugger:Port=0` in its `CONFIG`; the test then pins what 2.2.9 says: a start park with a
   server and no client waits without limit, whatever `Exit.*` key the run has. The README row says so. When it was
   applied the change was inert (the `dbg::Init` of that time was a stub that read no key), so the test stayed
   green before and after E5. Nothing else of that file changes: `debugger.selftest.emuhang.parked` is a selftest session
   (`Debugger:SelfTest=emuhang.parked`) that sets no `StartPaused` key, and `sh4bp`, `sh4bpboot` and `sh4bpbase` are
   sessions that set `Debugger:StartPaused=yes` without a server, which the rule excepts.
8. The register `ENABLED` of `dc.aica.arm7` (decision 31; 2.8, 2.10): an integrator's correction, applied in front
   of E5 (commit `bc924abf`). `dbg_regs_tables.cpp`: the row `{ "ENABLED", 1, kRegComputed, kNoAddr,
   ARM7(kArm7Enabled) }` in `arm7_regs`, behind `PC`, so that `PC` stays the first register of the node.
   `dbg_regs_private.h` / `dbg_regs.cpp`: the selector `kArm7Enabled = 0x100`, inside the source `kSrcArm7` and above
   every register index of the ARM7 context; it is not handed to `SoundCPU::DebugGetReg`, and the reader answers it
   with `SoundCPU::DebugEnabled()`. The row has no `kRegWritable`. With the expectations of `tests/debugger/regs`
   (the node has 41 registers, the tree 247) and of the selftest `regs`: the register reads 0 while the ARM7 is held
   in reset and 1 while it is clocked. It is not monotonic: in 2ndmix it goes 0, 1, 0, 1, since the program holds
   the ARM7 a second time before it plays. The session `regs` runs the guest for it: its ninth test, the guest test
   `regs.enabled`, stops at every change of ARMRST and compares the register with what the guest wrote and with
   `SoundCPU::DebugEnabled()` (5.3).

Where each part of the mechanism is implemented (all merged):

| Part | File(s) | Package |
| --- | --- | --- |
| decoder query, `shop_dbg_check`, size limits, idiom rule, `DecodeLimits`, refund rewrite, `dbg_flow`, the check budget (`dbg_rest_pc`), `ngen_features` | `hw/sh4/dyna/decoder.cpp`, `decoder.h` | E4b |
| the SHIL op; `srt_waw` T-reader rule; the flush rule | `hw/sh4/dyna/shil_canonical.h`, `shil.cpp`, `regalloc.h` | E4b |
| page discard, flow-block discard, temporary-block pointer | `hw/sh4/dyna/blockmanager.h/.cpp` | E4b |
| temporary blocks (`rdv_RunTempBlock`), the remainder and the block rest in `recSH4::Loop`, `recSH4::Step`, the fallback loop and step with the counter hand-over | `hw/sh4/dyna/driver.cpp`, `ngen.h` | E4b |
| x64 check stub and tails, dispatcher exit, `RunBlock`, epilogue report, persistent counter | `jit/backend/x64/rec_x64.cpp` | E4b |
| interpreter debug loop, step, persistent counter, the separate slot | `hw/sh4/interpr/sh4_interpreter.cpp` | E4b |
| armed set, helpers, exemption, remainder, temporary breakpoints, step-over / step-out state, step preconditions | `debugger/dbg_sh4.cpp` | E4b |
| the stop latch, the park context, entering and leaving it, `UseFallbackLoop()`, `Run()`, `IsRunning()`, the start and stop of the cpu | `debugger/dbg_emu.cpp`, `libswirl.cpp`, `hw/sh4/SuperH4_impl.cpp` | E4a |
| call sites: exception report, `RTE` report, SLEEP latch test | `hw/sh4/sh4_interrupts.cpp`, `hw/sh4/interpr/sh4_opcodes.cpp` | E4a |
| declarations of all of the above that cross a package boundary | `debugger.h`, `dbg_sh4.h`, `dbg_emu.h`, `hw/sh4/sh4_if.h`, `hw/sh4/sh4_dbg.h` | E0 |

E4a checklist (done; with a window, once, by `tests/debugger/selftest/window_checklist.sh`; each item with (E) parked and
again with a pause pending): the menu key opens the menu; the ESC key stops; window close exits the process without a
hang; menu "Exit", "Load State", "Save State", "Restart" work from the resulting `Stopped` state; a guest soft reset
(`SB_SFRES`) while parked resets and stays parked; `reios_exit` exits the thread; after each, the published state has
the right `reason`; a guest soft reset and a host stop during a step. The menu items leave with the imgui GUI (track
10); their headless equivalents are the selftests of `emu`.

**The nine contracts on the declarations** (as built; the comments of the headers carry thread, lifetime state and
blocking behaviour, and are the contract):

1. *`dbg::emu::Call`* — `int Call(CallClass cls, std::function<void()> fn)`; classes, per-state behaviour, timeout and
   cancellation, capture rule: 2.2.2.
2. *Emulator -> core* — `void dbg::emu::SetListener(void (*)(const RunState&))`; when and on which thread it is invoked,
   what it may do: 2.2.4. `struct RunState { Lifetime lifetime; u32 stopId; StopCause cause; u32 breakpointId; u32 notice; }`
   (`notice` is a bit set of `NOTICE_*`, the statements of 3.2; `cause` is `NoCause` exactly when the lifetime is
   `Running`). `dbg_core.h` declares the core's entry points: `bool core::Start(net::Server*)`, `void core::Stop()`,
   `net::Handler* core::NetHandler()` (its callbacks only enqueue to (C)), the listener `core::OnRunState`, and
   `std::string core::BuildTickText(bool full)` for the selftest. That is all the frozen header has; what else
   `dbg_main.cpp` (the tick period and the full-tick count, `core::Configure`), `dbg_events.cpp` and the selftest
   need of the core is in E5's own `dbg_core_private.h` (above, "Owns"). `core::Stop()` is also called by `dbg::Init`,
   behind a `core::Start` that succeeded for a server that then could not be started (2.11; header revision 4, H21).
3. *Core -> emulator* (all in `dbg_emu.h`):
   ```cpp
   void    RequestPause();                                   // any thread; no effect unless Running
   int     Resume();                                         // inside a CC_EXEC call: 0, or -32003 (not parked, or an action armed)
   int     Step(Target t, StepKind k, const char** why = nullptr);   // inside a CC_EXEC call: validates (0, -32003, -32004,
                                                             // -32005 with *why, -32012), arms; the park loop executes it
   int     Reset();                                          // inside a CC_WRITE call
   void    SetSh4Breakpoints(const u32* ids, const u32* addrs, u32 n);   // the enabled SH4 code breakpoints, all of them; CC_TABLE
   void    SetArm7Breakpoints(const u32* ids, const u32* addrs, u32 n);  // the enabled ARM7 code breakpoints (M2, revision 6); CC_TABLE
   void    SetEventBreakpoints(const u32* ids, const u8* events, u32 n); // the enabled event breakpoints, all of them; CC_TABLE
   void    SetActiveMask(u32 categories);                    // CategoryBit() per active category; 0 before the first call
   RunState GetRunState();                                   // any thread; the state last published
   bool    OnEmuThread();
   bool    GetSh4Engine(Sh4EngineInfo* out);                 // backend name, caps, fallback, fallbackLoop, sliceCyclesLeft
   bool    TargetSteps(Target t);                            // M2, revision 6: whether the target steps (targets[].stepping)
   ```
4. *`dbg::net`* — Appendix B: sends are callable from (E) and never block. `HasClients()` is lock-free and stays in the
   API, but it is not what the core follows: `g_event_on`, the active mask, the periodic tick and the grace period
   follow the core's own count of open clients, kept from `OnOpen` / `OnClose` (2.6; the comment of `g_event_on` in
   `debugger.h` was brought to this by header revision 4, H9).
5. *Events* — `DBG_EVENT`, `EventSlow`, `g_event_on`, the `EventId` constants and table: 2.6.
6. *`dbg_mem` / `dbg_regs`* — thread requirement "(E), or under L"; no guest register or memory handler is called (the
   one stated exception is in 2.7):
   ```cpp
   dbg::mem::Region Classify(Target t, u32 addr);
   u32  dbg::mem::Read (Target t, u32 addr, u32 len, u8* out, u8* readableBits);   // returns readable byte count
   int  dbg::mem::Write(Target t, u32 addr, u32 len, const u8* data);              // M2; (E) park context; 0 or -32013
   const dbg::regs::Node* dbg::regs::Nodes(uint32_t* count);                       // constant tables (pure); Find, FindRegister
   void dbg::regs::FormatValue(uint32_t width, uint64_t value, char* out);         // the wire form of a value
   bool dbg::regs::Read (const Node&, uint32_t index, uint64_t* value, char* text, uint32_t n);   // false -> "n/a"
   int  dbg::regs::Write(const Node&, uint32_t index, uint64_t value);             // M2; (E) park context
   ```
7. *Hooks called from `Dreamcast_impl` and `SuperH4_impl`* — `LifetimeLock` (RAII over L), `OnMachineCreated()`,
   `OnMachineDestroying()`, `OnThreadStarting()` (in `Resume`, under L), `OnThreadEntered()` (on (E), no lock),
   `OnThreadExiting(bool guestExit)` (on (E): takes L, drains, publishes `Stopped`), `OnGuestStateReplaced()`, `Kick()`,
   `u32 Park()`, `SliceHook()`, `UseFallbackLoop()`: 2.2.1, 2.2.3, 2.2.4, 2.2.5.
8. *`hw/sh4/sh4_dbg.h`* — `void UpdateFPSCR(); bool UpdateSR(); void sh4_host_fp_resync();` and
   `void sh4_fallback_loop(); void sh4_fallback_step();` (2.3.5; compiled under
   `defined(NULLDC_DEBUGGER) && FEAT_SHREC != DYNAREC_NONE`, inline stand-ins that end the process otherwise).
9. *The SH4 mechanism* — (a) the backend side exactly as 2.3.4 l lists it (`debugger.h`, `hw/sh4/sh4_if.h`,
   `hw/sh4/dyna/`). (b) Run control -> breakpoint state, in `dbg_sh4.h`; all on (E) in the park context unless a
   function says otherwise:
   ```cpp
   namespace dbg { namespace sh4 {
   u32  MaskAddr(u32 addr);                                        // the address rule (2.3.3)
   void SetBreakpoints(const u32* ids, const u32* addrs, u32 n);   // the enabled set: diff against the armed set, page discards (2.3.4 g)
   void SetActiveMask(u32 categories);                             // read by the helper only; never a discard
   int  StepCheck(const char** why);                               // step preconditions: 0 or -32005 (2.3.2)
   void PrepareResume();                                           // 2.2.4 e: exemption; keeps the remainder if the PC is unchanged
   void PrepareStep();                                             // 2.2.4 e: exemption; drops the remainder
   bool PrepareStepOver();                                         // true: temporary breakpoint armed, resume; false: do a step
   void PrepareStepOut();                                          // step-out state, discard of the call / return blocks
   void OnStop(StopInfo* out);                                     // entering a published stop: what stopped; removes temporary
                                                                   // breakpoints, cancels a step-out, clears the exemption
   void OnPcWritten();                                             // PC written, reset, state load: drops remainder and exemption
   bool FallbackWanted();                                          // an active (temporary) breakpoint exists or a step-out is pending
   }}
   ```
   (b') The same for the ARM7 (M2, header revision 6), in the frozen `dbg_arm7.h`: the listed set, the mask
   (`MaskAddr`), the active mask, the resume exemption, the temporary breakpoint, the steps (`StepCheck`, `PrepareResume`,
   `PrepareStep`, `PrepareStepOver`, `PrepareStepOut`, `OnStop`, `OnPcWritten`), and the ARM7 block of `debugger.h`
   that the sound burst calls (`dbg_arm7_armed`, `dbg_arm7_Check`, `dbg_arm7_OnBurstUnclocked`).
   (c) Breakpoint state and events -> run control, in `dbg_emu.h`: `void dbg::emu::LatchStop(StopCause, u32 breakpointId)`,
   `void dbg::emu::OnEvent(EventId)` and, for the sound side, `dbg::emu::OnSoundStop` (M2) — (E) only; what the helpers,
   the ARM7 check and `EventSlow` call to stop (2.2.3).
   The texts of the declarations of revision 6 are in `M2-ROUND.md` section 2 (H6-1 to H6-6 and the comments H6-10 to
   H6-42).

The device accessors that E0 declared and E3 implemented (no side effects; (E), or under L): `AICA::DebugReadRegs /
DebugGetChannel`, `libAICA_DebugGetRTC`, `SoundCPU::DebugEnabled / DebugGetReg / DebugGetInterrupt` (and
`DebugIsRecompiler`, revision 6), `DSP::DebugGetState / DebugGetProgramStep`, `gdrom_DebugGetState`,
`SystemBus::DebugRead`, `DebugReadISTNRM`, `area0_DebugRom / area0_DebugNvmem / area0_DebugNvmemByte`. The two writers
are not on that list: `SoundCPU::DebugSetReg` is a writer ((E) only, the effects its comment gives; body with E6b).
`AICA::DebugWriteReg` has no body in M2.

#### 6.1.2 Left for the next emulator package

**The three items this list had are done with J1**: the step of `debugger.contract.grace` that holds the end of the
expired state at a connect (commit `9bd949b9`; 2.9, 5.4), the section for the session `core` in
`tests/debugger/selftest/README.md` (`3d4c8a07`), and the rank of a symbol of a section ahead of an absolute one in
`dbg_symbols` (`e5dbfe39`; 2.10).

What is left now, from J1 and its verification; none is a defect:

1. **`resetting`, `stopping` and `StopForReset()` are removed** (the design owner's decision; 2.2.5). Since
   `IsRunning()` is one word, nothing reads the two members of `SuperH4_impl`, and `StopForReset()` differs from
   `Stop()` only in not clearing one of them. To remove: the two members, their five writes (four in `libswirl.cpp`,
   one in `SuperH4_impl::Stop()`), and `StopForReset()`, with `RequestReset()` calling `Stop()`. **Done with R6** (the
   integrator's step of the M2 round); the pseudo-code of 2.2.4 lost its two assignments with it.
2. **Three selftests also read the run flag and `g_parked`.** The checks "the stopped machine never counted as
   running" of `emu.startstop`, `emu.startpark` and `emu.stopruns` watch one word since J1, so a run flag or a
   `g_parked` left set behind the end of a thread is seen by no test (it has no effect on a run: the next thread
   sets both itself). The three tests also read `p_sh4rcb->cntx.CpuRunning` and `dbg::g_parked` once the lifetime is
   `Stopped`: the coverage the one word no longer gives. **Done with R6.** (Not held by a test, and accepted: the clears
   of the run flag and of `g_parked` at the end of a thread — removing them passes, because `SuperH4_impl::Stop()`
   clears both at the request; a test that latches a debugger stop in the slice of a host stop would hold them.)
3. **The window sessions, when a host with a display runs them** (W64, or the work of PLAN track 10): confirm that
   `emuwin.closereset` still ends the process. Its probe can no longer find a live machine counted as stopped, and
   the three window-close callers (`linux-dist/x11.cpp`, `sdl/sdl.cpp`, `windows/win_common.cpp`) still test
   `IsRunning()` before they act, where both branches now do the same thing (by reading only). After that run the
   three callers call `Stop(cb)` unconditionally.

### 6.2 Frontend

| Pkg | State | Owns | Depends on | Gate; what was delivered |
| --- | --- | --- | --- | --- |
| **F0** hygiene (M0) | done | scripts, lint / tsconfig, README and docs, Dockerfile, CI, committed wasm glue | — | `npm run check` on a fresh clone without Rust. Delivered with it, beyond v2's plan: the Playwright e2e suite, error boundaries, offline Monaco and code splitting. `build:wasm` and the M1 gate in `check` came with the devtools round (4.1) |
| **F1** protocol v3, mock module, contract runner | done | `src/lib/debuggerSchema.ts`, `protocolVersion.ts`, `binaryFrame.ts`, `src/lib/jsonRpc.ts`, `src/services/jsonRpcClient.ts`, `transport.ts`, `debuggerClient.ts`, `server/mockDebugger.ts`, `mockWebSocket.ts`, `mockServer.ts`, `scripts/`, the bundle-info Vite plugin, their tests | F0 | 4.2. Differs from v2: the strict copy is built from the same definition as the client schemas; the mock is "M1 plus" with options down to M1; the runner has `--milestone` / `--require` / `--only`, a fourth result `partial`, per-part gating of S16-S18 and far more probes than v2's rows. Its last verification left gaps that became decisions and were closed by the devtools round (4.2.1) |
| **F2** M1 client behaviour and panels | done | `src/state/*`, `src/app/**`, `e2e/` | F1 | 4.3. Differs from v2: the store is reset as a whole on disconnect (changed back by 4.3.1); a failed describe is retried; the endpoint order is a priority; refusals fold into one notice; stops are coalesced. Its last verification left one test gap and four should-fix items, closed by the devtools round (4.3.1) |
| **F2r** devtools round after revision 3 | done | as F1 and F2 | this document | 4.2.1 and 4.3.1, every item, item 7 (the 1366x768 window) included. Delivered on `skmp/nulldc3`: the runner and the mock at `11d4136`, the panels at `17f3bff` (the head). `npm run check` passes (382 unit tests; the M1 gate on the mock with 16 scenarios passed, 0 partial, 0 not applicable; `build:wasm`); the e2e suite passes (246 tests); the runner judged 278 broken or legal servers correctly; a mutation for every new rule, none survived. Its verification left the points that decisions 30 to 41 settle |
| **F2r, follow-up** the runner after decisions 30 to 37 | done | `scripts/contract/`, `server/mockDebugger.ts`, their tests | F2r | 4.2.2: six runner changes and one mock change. Delivered at `22b358e` of `skmp/nulldc3`: `npm run check` passes (391 unit tests; the M1 gate on the mock with 16 scenarios passed, 0 partial, 0 not applicable); the e2e suite passes (246 tests); every changed rule taken back turns a test of the runner red. It was done before the gate of E5 was used, and that gate then passed on three engines without a finding against the runner |
| **F3a** the contract of M2 | done (verified at `e0f7895`, three verification rounds) | `src/lib/debuggerSchema.ts` (two panel ids, the params of `state.writeMemory`), `server/mockDebugger.ts` and its options, `scripts/contract/`, their tests (`M2-ROUND.md` 3.1) | F2r and its follow-up | the mock is an M2 server and passes the M2 gate, also as `MOCK_M2_SUPPORT`; `npm run check` runs it; the runner has the M2 rules (S16 with five parts, `--parts`, `--require-note`, `--mock-support`) and the two M1-level rules of 4.4; each rule with a test that goes red when it is taken back |
| **F3b** the end of protocol v2 | done (`1183dab`, first verification) | the client, the panels, the schema samples and the e2e tests that serve v2 (`M2-ROUND.md` 3.7) | F3a | 4.4 (by the table of `M2-ROUND.md` 3.7); 488 unit tests, the M2 gate on the mock, e2e 251; the emulator's 259 targets twice with the UI at that head. Accepted departures: one test's set-up of `src/state/debuggerDataStore.test.ts` edited outside "Owns" (its server lists the DSP; the assertion is unchanged), six more e2e tests than asked (a scripted server per row of the table of 3.7) |
| **F3c** registers and memory | done (`af97c4d`, second verification) | the register panels, the editors, the memory view (`M2-ROUND.md` 3.8) | F3b | 4.4; 543 unit tests, e2e 312 (the layouts at 1366x768, the `MOCK_M2_SUPPORT` blocks, the tree of 3,000 registers at five ticks a second); 34 mutants caught |
| **F3d** toolbar, reset, hotkeys | done (`6c753dc`, second verification) | the run controls, `useRunHotkeys`, the help, `e2e/emulator-smoke.spec.ts` (`M2-ROUND.md` 3.9) | F3c; the emulator at E7b or later (its smoke spec) | 4.4; 583 unit tests, e2e 347; the smoke spec 4 of 4 against a headless emulator with the bundle embedded |
| **EXu** the leftovers of the UI packages | done (`b3e3600` verified, four verifications; the integrator's test-only commit `d59336b` on it) | `M2-ROUND.md` 3.12 | F3d | 4.4 (the end of an address space in a small memory panel; no editor that follows its byte; a watch renamed from the keyboard; `0x`-only register values; a dialog in its exit transition counts as closed); 584 unit tests, e2e 368, the smoke spec against `6fe01759`. Accepted departures: `e2e/disassembly.spec.ts` changed outside "Owns" (its test pinned the slice at the end of the address space that item 1 replaces); two assertions of `src/state/runControls.test.ts`, granted by the integrator for item 5; the integrator's fix of a test that read a slice once while the running view may ask again (`d59336b`, test-only) |
| **F4a** the contract of M3 | done (`f13444e`, 13 commits, three verifications) | `src/lib/debuggerSchema.ts`, the describe fixture, `server/**`, `scripts/contract/**`, `scripts/contract-test.ts`, the `check` line of `package.json`, their tests; the README's mock and runner sections (`M3-ROUND.md` 3.1) | EXu; devtools `18435de` | 4.5; `npm run check` with the M3 gate on the mock (also `--mock-support m3` and the two `--dsp-program` runs with their notes); the unit suite three times in a fresh clone; the e2e suite; every rule of the runner red when taken back; the emulator's contract targets unchanged with the UI at its head. A timing bound in a test is measured on a monotonic clock (the wall clock of the development machine steps) |
| **F4b** the playground on the one DSP core | done (`8db2795`, 8 commits, two verifications) | `src/wasm/**`, `crates/**` (removed), `scripts/build-dspsim.ts` and its driver, `src/lib/aicaDsp.ts` and its tests, `DspPlaygroundPanel.tsx`, `writeRegisters` of `dspUtils.ts`, the `build:dspsim` / `mock:dev` lines, the module's README parts, `e2e/dsp-playground.spec.ts` (`M3-ROUND.md` 3.4) | F4a; the emulator at W1's merge | 4.5; `build:dspsim` from the emulator at W1's merge gives the committed bytes (emscripten 6.0.8); the known-answer vitest; 683 unit tests passed (1 skipped), e2e 376; `debugger.disasm.dsp_playground` green. Accepted: "Start DSP" sets STEP and SAMPLES to 0 and does not reset the core |
| **F4c** the SGC panel on the v3 contract | done (`b35728f`, 8 commits, two verifications) | `AudioPanel.tsx`, `sgcChannelData.ts` and its tests, `Sgc*.tsx`, `SgcWaveformRenderer.ts`, `e2e/audio.spec.ts`, `KNOWN.sgcNoData` (`M3-ROUND.md` 3.6) | F4b | 4.5; 702 unit tests passed (1 skipped), e2e 384; a capture of 4096 frames shown about 150 ms after its frame reached the page. Accepted: ten spec files lost only their `KNOWN.sgcNoData` allowances; Record asks for `{frames: 1024}`; a "Capture length" label |
| **F4d** the Audio Debugger against the emulator | done (`066fcbc`, 11 commits, two verifications) | `emulatorSnapshot.ts` and its tests, the call and the banner in `DspPlaygroundPanel.tsx`, `RunControls.tsx`, `DisassemblyPanel.tsx`, `runControls.ts` and its test, `documentationSections.ts`, the help, the README's "The UI against the emulator", `e2e/emulator-smoke.spec.ts`, the load-path and held-ARM7 e2e tests (`M3-ROUND.md` 3.10) | F4c; the emulator at E8b's merge for its smoke spec | 4.5; 735 unit tests passed (1 skipped), e2e 401; the smoke spec twice against the emulator with the bundle embedded; "Load from emulator" at a DSP breakpoint: 17 steps of the playground equal to the emulator's in the 10 rows and 336 words of TEMP, MEMS and EFREG. Accepted: the Load Regs source generator shared from `emulatorSnapshot.ts`; F4c's leftover test in `e2e/dsp-snapshot.spec.ts`; the help's sentence also in the README; the button shown off with its reason on the tool page; the smoke spec's timeout 420 s |
| **F5** M4 Holly | to do | CORE / TA inspectors, table panels | F4; emulator M4 | `npm run check`; contract gate M4 |
| **F6** M5 | to do | SH4 simulator panel | tracks 3/4 | fixture gate |

### 6.3 Emulator, later rounds (cut when M1 is merged; the ownership rule stays)

M1 is merged (6.1), and of the packages below J1 is done, as is the PLAN package P-desktop (the last two rows).
**The M2 round is done**: it was cut in `M2-ROUND.md` (header revision 6, the packages R6, E6a, E6b, E7a, E7b, the UI
packages F3a to F3d of 6.2, the leftovers EXe and EXu, and the gate M2G, with their ownership and gates), and every
package of it was built, verified and merged (rows R6 to M2G below; the rows E6 and E7 were cut into them). **The M3
round is done**: it was cut in `M3-ROUND.md` (header revision 7, the emulator packages R7, W1, W2, W3, G, E8a, E8b, the
UI packages F4a to F4d of 6.2, and the gate M3G), and every package of it was built, verified and merged (rows R7 to
M3G below; the row E8 was cut into them, and the wiring of the T5.0 core with them). **The platform round is done**:
it was cut in `PLATFORM-ROUND.md` (the owner's decisions of 2026-10-19: the rebrand, the icons, the versions from the
tag `v3.0.0-pre`, Windows, macOS, Linux arm64, CI and releases; header revision 8), and its packages VCu, RB, IC, VTe,
W64a, W64b, W64c, MACa, MACb, LA64, CIe and A64 were built, verified and merged (rows VCu to A64 below), with the gate
PLG on Linux, the Windows host, the Mac and the arm64 Linux host (row PLG). **W64**, the Windows run with the real MSVC
build that 5.8 asked for, is done as W64a and W64b; **A64**, the arm64 SH4 recompiler's native checks, is done as the
platform round's A64, added before its gate at the owner's word; the arm64 ARM7 recompiler and the SH4 FPU fast-tier
fixes of the arm64 JIT that the row named with it stay in PLAN's backlog (PL-D46). Then E9 for M4, from a cut of its
own. What the packages so far left for the next one is 6.1.2, for M2 the known limits of 2.3.7 (items 14 to 24) and
the answers of `M2-ROUND.md` 6.2, and for the platform round the known limits of 2.3.7 (items 31 to 33) and PLAN's
backlog of the round.

| Pkg | Content | Owns | Must not touch | Depends on | Gate |
| --- | --- | --- | --- | --- | --- |
| **R6** header revision 6 (integrator; **done**, merged as `af1f1d8c`, the ASCII fix of two frozen comments `f6949750`) | `M2-ROUND.md` 3.2: the declarations and comments of its section 2 with stub bodies, the ARM7 wiring of the core (`PushBreakpoints`, the three readers of `TargetSteps`), the panel-id list (37), the leftovers of J1 (6.1.2 items 1 and 2), the repository's copy of the cut | as 3.2 | as 3.2 | M1, J1 | the wire as before, bit for bit: 190 ctest targets |
| **E6a** the sounding guest (**done**, merged as `5a3ad3d6`) | `M2-ROUND.md` 3.3: `tests/debugger/guest/dsptest/` (5.6), the runner of the sound lines and its targets; the soak (200 runs) and memcheck clean | as 3.3 | as 3.3 | R6 | 206 ctest targets; the guest sounds, its lines agree but for `sh4` on the two engines |
| **E6b** the resumable burst and ARM7 run control (**done**, merged as `b4cb3e82`) | `M2-ROUND.md` 3.4: 2.5.2, 2.5.3, the body of `SoundCPU::DebugSetReg`, the ARM7 callstack while stepping, the session `arm7`, the neutrality targets of 5.6 | as 3.4 | as 3.4 | E6a, its baseline taken | 238 ctest targets; sound lines and state hashes identical before and after, also with the debugger compiled out and at work on the ARM7; the host-cost checks of 5.7 |
| **E7a** writes and reset (**done**, merged as `b951e440`; `d3fe072d` comment only) | `M2-ROUND.md` 3.5: `state.writeMemory`, `state.setRegister`, `state.editWatch`, `control.reset`, the three capabilities, the session `writes` | as 3.5 | as 3.5 | E6b | 249 ctest targets; the parts `writeMemory`, `setRegister`, `reset` of S16 on 2ndmix and on the guest |
| **E7b** the M2 rows of the device tree (**done**, merged as `199cdb4a`) | `M2-ROUND.md` 3.6: the tree of 3.6.1, read-only (2.8), the two actions, the `reserve` sizes, `hash.listening*` | as 3.6 | as 3.6 | E7a | 259 ctest targets; the contract gate M2 on three engines (19 passed each) and its second run on the guest |
| **EXe** the leftovers of the emulator packages (**done**, merged as `6fe01759`) | `M2-ROUND.md` 3.11: every page of a range witnessed, no wait without a bound, a NUL in an event name, nothing written into the source tree by a test run, the made-up drive state inside its enums, the completion in `dc_run` with a FIQ in the rest of its sample, three sentences of frozen comments | as 3.11 | as 3.11 | E7b | 259 ctest targets; under each named mutant the target that holds it fails by a check |
| **M2G** the gate (integrator) | `M2-ROUND.md` 3.10: the contract gate M2 against emulators that carry the UI's bundle; the sound lines and state hashes against the trees before the round; every ctest target twice; `npm run check` and the e2e suite; the smoke spec; this text brought to the cut (its section 7) | the documents | — | everything above | **passed** (2026-10-15): on `devcast` `6fe01759` with the UI at `d59336b` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+d59336b`): the M2 gate passes against the emulator on the JIT, the interpreter and the forced fallback (19 scenarios passed, 0 partial, 0 not applicable each) and its second run against the sounding guest (2 passed, both notes); the 259 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix equal those of the tree before E6b, and the state hashes of 5.7 those of the tree before the round, in the five builds; `npm run check` and the e2e suite pass in a fresh clone; the smoke spec passes against a headless emulator on the guest; ThreadSanitizer of the contract parts on the guest gives no report; speed: 5.7 |
| **R7** header revision 7 (integrator; **done**, merged as `b921699b`) | `M3-ROUND.md` 3.2: the declarations and comments of its section 2 with stub bodies (`dbg_dsp.cpp`, `dbg_sgc.cpp`), the DSP wiring of the core (`PushBreakpoints`, `Rank(Capture)`), the rows of 0x2804 from the DSP's `RBPL`, the forcing names out, the repository's copy of the cut | as 3.2 | as 3.2 | M2, N1, D1; F4a | the wire as before, bit for bit: 278 ctest targets |
| **W1** the T5.0 core is the emulator's DSP (**done**, merged as `ed385ee2`; `33f7835d` the Xcode project's four files) | `M3-ROUND.md` 3.3: the core in the DSP device, the sample in phases, the sends as per-bus sums, the register routing, the old DSPs and `Dynarec.DspEnabled` out (the arm64 DSP recompiler deleted), save state V5, the line's `dsp` field, `aica-dsp-asm` ported, the J2 targets | as 3.3 | as 3.3 | R7, B0 taken | 287 ctest targets; every line in the six fields of B0 identical in both `aica.NoBatch` settings, 76 of 76 state hashes equal |
| **W2** the batch path goes (**done**, merged as `dd74f348`) | `M3-ROUND.md` 3.5: the batch render and `aica.NoBatch` out, "Disable Sound" mutes, save state V6, the runner without the render key, the replay of the UI's module (`aica.dsp.wasm.devtools`) | as 3.5 | as 3.5 | W1, B1 taken; the pin at F4b | 275 ctest targets; every line equal to B1's per-sample line in all seven fields, 114 state-hash rows equal |
| **W3** the AICA clocked whatever ARMRST says (**done**, merged as `0222385f`; `6fe198d7` text fixes) | `M3-ROUND.md` 3.12: the 32 time steps of a burst whatever `arm_on` says, only the ARM phase under it; the witness `held`; `arm7.held.clocked`; the texts that said the AICA stands still while the ARM7 is held | as 3.12 | as 3.12 | W2, B2 taken | 277 ctest targets; the guest identical to B2 (no held burst); 2ndmix's moves shown (`samples` the silence of 32 × N pairs, `dsp`, `regs` of the timers and pending bits from the first held burst) |
| **G** the guest's DSP program (**done**, merged as `dfbad7dd`; `fe90f9f1` text fixes) | `M3-ROUND.md` 3.7 and section 6: `dsptest_dsp.txt`, the step of `gen.sh`, the block `g_dsp` and the copy loop, the oracle tool and `debugger.sound.oracle.*`, `debugger.sound.heard.*`, `debugger.disasm.dsptest` | as 3.7 | as 3.7 | W3, B3 taken | 282 ctest targets; G1 to G5 of the cut's 5.1; the oracle equal over 8,192,000 compared steps; soak and memcheck run |
| **E8a** DSP breakpoints and steps (**done**, merged as `005a9507`; `fbf47ecd` comment fixes) | `M3-ROUND.md` 3.8: the burst in phases, `RunPhase`'s debug loop, `dbg_dsp.cpp`, the DSP's run control in `dbg_emu.cpp`, the nine rows, the session `dsp`, `dspn.*`, the contract mode `dsp` | as 3.8 | as 3.8 | G, B4 taken | 295 ctest targets; lines and hashes identical to B4, also with the debugger at work on the DSP; ThreadSanitizer of the session and the mode `dsp`, no report |
| **E8b** SGC capture, memory binary, the binary path (**done**, merged as `c0d39a91`; `91c678a5` comment fixes) | `M3-ROUND.md` 3.9: `dbg_sgc.cpp`, the kept outputs, the three methods and the deferred response in `dbg_core.cpp`, the two cancels, the transport's two bounds, the session `sgc`, `sgcn.*`, the contract modes `m3` and `m3dsp` | as 3.9 | as 3.9 | E8a | 307 ctest targets; the contract gate M3 (`m3` 20 passed on three engines, `m3dsp` 3 and `m3dsp21` 2 with their notes on two); lines and hashes identical to B4 with captures; ThreadSanitizer of the session and the guest's contract runs, no report |
| **M3G** the gate (integrator) | `M3-ROUND.md` 3.11: the contract gate M3 in its two runs and the M2 gate's second run on the changed guest, against emulators that carry the UI's bundle; the sound lines and state hashes against B4; every ctest target twice; `npm run check` and the e2e suite in a fresh clone; ThreadSanitizer; the smoke spec; speed measured and reported; this text brought to the cut (its section 8); the two review PRs | the documents | — | everything above | **passed** (2026-10-19): on `devcast` `9fbc988a` (its documents behind it, on the branch `skmp/nulldc3-m3g`) with the UI at `066fcbc` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+066fcbc`): run 1 of the M3 gate passes against the emulator on the JIT, the interpreter and the forced fallback (20 scenarios passed, 0 partial, 0 not applicable each), run 2 against the sounding guest on the JIT and the interpreter (S00, S17, S21: 3 passed with the note `sgc: captured with the arm7 held`; `--only S00,S21`: 2 passed with the note `dsp: stepped with the arm7 held`), and the M2 gate's second run on the changed guest (2 passed, both notes); the 307 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix and the guest's exit state hash equal B4's (12 of 12 entries, the default build and `-DNULLDC_DEBUGGER=OFF`), and so do the state hashes of 5.7 (38 of 38 rows, 2ndmix 600 in the five builds); `npm run check` (735 unit tests passed, 1 skipped; the four contract runs on the mock OK) and the e2e suite (401 passed, 1 skipped: the smoke spec, run on its own against the emulator) pass in a fresh clone of the UI; the smoke spec passes against a headless emulator on the guest; ThreadSanitizer of the sessions `dsp` and `sgc` and of the contract runs on the guest (interpreter) gives no report; speed is measured and reported (5.7) |
| **E9** M4 | `state.getTable`, `state.captureHolly`; writes of system bus, PVR and SH4 on-chip module registers (each with its write semantics stated, `M2-ROUND.md` M2-Q1), and of the AICA's registers with the body of `AICA::DebugWriteReg` (the owner's answer to `M3-ROUND.md` M3-Q3: every device's write table in one design step), the snapshots of maple and the SPG | `dbg_core.cpp`, `hw/pvr/`, the capture hook in `rend/refsw2/` | `hw/sh4/`, `hw/arm7/` | M3 | contract gate M4 |
| **W64** the Windows run (**done as W64a and W64b of the platform round**, with W64c behind them; rows below) | gates B1-B4 and the E4a item of 5.8 on a Windows x64 build; then `DebugChecks` in Windows builds. Also for this run: the real MSVC build (5.8), J1's item (more than two hundred thread starts in one process: `debugger.selftest.hostcycles`), and with a display the window sessions (6.1.2). Done: the MSVC build (W64a), the gates of 2.3.6 and `DebugChecks` on Windows (W64b, PL-D38), `debugger.selftest.hostcycles` on the Windows host (W64b, W64c); the E4a item and the window sessions are the owner's by hand (PL-D40) | the `_WIN32` guard in `rec_x64.cpp` | everything else | a Windows host | 5.8 |
| **VCu** the UI's version, line endings and CI (the UI repository's package of the platform round; **done**, `skmp/nulldc3` of the UI = `7b88410`, 10 commits on `3cf8b15`) | `PLATFORM-ROUND.md` 2.4: `scripts/version.ts` (the rule of 5.1 of the cut; 2.11 here), `.gitattributes` with `* -text`, the UI's workflow on pull requests and tags with its Release (`release-files.ts pack`, `check`, `publish`) | as 2.4 | the emulator | M3 | `npm run check` (793 unit tests passed, 1 skipped; the contract runs on the mock), e2e 401 passed, 1 skipped; the version cases 16 of 16 in scratch repositories; the emulator's 307 targets with the pin moved there |
| **RB** the rebrand (**done**, merged as `60bce5b7`, head `c893e925`, 30 commits) | `PLATFORM-ROUND.md` 2.5: `reicast/` becomes `nulldc3/` in a commit of pure moves (55 renames at R100), the names of PL-D9 (D4), reicast's dead files out (PL-D14), header revision 8's H8-1 to H8-3 (Appendix B), P0 taken, `rebrand.names` | as 2.5 | as 2.5 | VCu, the pin moved | 308 ctest targets; P0 38 of 38 rows equal to B4 |
| **IC** the icons (**done**, merged as `c2f8ce03`, head `01b3ca10`) | `PLATFORM-ROUND.md` 2.6: the approved art in `nulldc3/icon/`, the standard-library generator of the `.ico` (7 PNG entries), the `.icns` (10 PNG elements) and the X11 icon data, its check target; the X11 window's `_NET_WM_ICON` and `WM_CLASS` | as 2.6 | as 2.6 | RB | 310 ctest targets (`icons.generated`, `icons.x11`); P0 equal |
| **VTe** the emulator's version (**done**, merged as `2ac231ac`, head `e5a4fd2f`) | `PLATFORM-ROUND.md` 2.7: the version made at build time (`nulldc3/cmake/version.cmake`, `<build>/generated/nulldc3_version.h`, read by `libswirl/version.cpp` alone), every reader of it (3.1, 2.2.9, 2.8, 2.11), `--version`, header revision 8's H8-4 | as 2.7 | as 2.7 | IC | 314 ctest targets; P0 equal; a new commit recompiles `version.cpp` alone |
| **W64a** the MSVC build and Windows headless (**done**, merged as `7543be6b`, head `eb29a4a8`) | `PLATFORM-ROUND.md` 2.8: the MSVC build (the static CRT, CMake's defaults extended, PL-D32), the `.rc` with the icon and the version, Windows headless with the no-dialog policy and Linux's fatal line (D10), the ABI fixes (XMM6-15 in `Mainloop` and `RunBlock`, the vmem placeholders, the unwind table, `_mm_setcsr`), `nulldc3-winrun`, the portable C++ suites on the Windows host | as 2.8 | as 2.8 | VTe | Linux 314 targets, P0 equal, the compile commands unchanged; the Windows host: the probe on its private desktop first, three builds, the headless report `windows=0`, the state hashes of 16 runs equal to Linux's |
| **W64b** the gates on Windows (**done**, merged as `fb5049b5`, head `f2b234a9`) | `PLATFORM-ROUND.md` 2.9: the harnesses' run helper `tests/common/emurun.py` (5.4), the selftests, the sound tests and the contract gates M1 to M3 on the Windows host; the check stub's shadow space and the Windows flush budget, `DebugChecks` on Windows (2.3.5, PL-D38) | as 2.9 | as 2.9 | W64a | Linux 317 targets; the Windows host 335 targets twice inside an outer `nulldc3-winrun`, the contract gates with Linux's numbers, the JIT on `sh4-debug-native` |
| **W64c** the selftests on Windows (**done**, merged as `963353d9`, head `1d413b3c`; added by the integrator behind W64b) | the selftests' naps for fractions of a millisecond on Windows (`dbg_selftest_nap`), `dsp_playground`'s CR LF, the crash policy in the remaining test programs | as its brief | — | W64b | Linux 318 targets; the Windows host 340 targets green twice (73 platform skips, no open defect) |
| **MACa** the macOS bundle and macOS headless (**done**, merged as `af2acc83`, head `ad179a62`) | `PLATFORM-ROUND.md` 2.10: the bundle made by CMake (`Info.plist.in`, `org.emudev.nulldc3`, the `.icns`, the licences, the ad-hoc signature by a stamp step, PL-D42), the Xcode project gone, macOS headless decided after `nulldc_init` (PL-D43), the `printf` macro gone, `tests/macos/macrun.py` and its sandbox profile, the portable C++ suites on the Mac | as 2.10 | as 2.10 | W64c | Linux 318 targets; the Mac: the canary first (the window server denied), three bundles, headless `app=0 windows=0`, the interpreter's hashes equal to Linux's |
| **MACb** the gates on the Mac (**done**, merged as `7c87d517`, head `47878491`) | `PLATFORM-ROUND.md` 2.11: `emurun.py`'s Darwin branch, the harnesses, selftests, sound tests and contract gates on the Mac; S04's arm64 rule in `contract.py` (PL-D78); `macos.hashes.jit` | as 2.11 | as 2.11 | MACa | Linux 319 targets; the Mac 330 targets twice (212 passed, 105 platform skips, the 13 targets that became the known limits of the arm64 recompiler's debugger, which A64 ended, 2.3.5); the contract gates with S04 recorded on the recompiler |
| **LA64** Linux arm64 (**done**, merged as `58273bfa`, head `59be3df4`; natively on the arm64 host) | `PLATFORM-ROUND.md` 2.12 (its brief's revision 4): the arm64 SH4 recompiler built and run on Linux, the 4 KiB page rule, `linux_arm64.hashes.jit` and `.pages.refused`, the known limits registered on every arm64 host (`debugger.a64.knownlimits`; gone with A64) | as its brief | as its brief | MACb | Linux x86_64 320 targets; the arm64 host 321 targets twice (263 passed, 58 skipped), the recompiler's hashes equal to the Mac's |
| **CIe** the emulator's CI and its release files (**done**, merged as `31dafee9`, head `2f936d71`) | `PLATFORM-ROUND.md` 2.13: `nulldc3/ci/` (`linux.sh`, `appimage.sh`, `windows.ps1`, `macos.sh`, `release.sh`, `hygiene.py`, `check_workflows.py`), `.github/workflows/ci.yml` (pull requests, tags `v*`, dispatch; `release` alone with `contents: write`), the AppImage's portable directory (PL-D51), the licences in every release file (PL-D80), `ci.hygiene`, `ci.workflows`, `ci.scripts` | as 2.13 | as 2.13 | LA64 | 327 ctest targets; the scripts on the three systems in `ci` and `full` mode; the release AppImage's binary on P0's default rows equal to P0 |
| **A64** the arm64 recompiler's native debugger (**done**, merged as `12069163`, head `7c025c83`; a package of the platform round, added at the owner's word of 2026-10-23 before its gate, `PLATFORM-ROUND.md` 2.15) | in two steps: the x64 slice model on arm64 (PL-D86: the slice counter kept across `Loop()` and tested in front of a block's charge, the pc stored at every slice end, `SH4DBG_CTX_AT_SLICE` stated, `Mainloop`'s one frame with `d8-d15` whole), which ended the thirteen known limits and S04's arm64 rule (PL-D78); then the items of 2.3.5 in `jit/backend/arm64` (the native checks, `SH4DBG_NATIVE`, `RunBlock`, temporary blocks that never link, the budget and the rest of a block), so that the engine is `sh4-debug-native` on arm64; the sh4bp suite on arm64 (`abi` new); `debugger.selftest.sqw` for the store-queue write with the MMU off; the arm64 recompiler's state-hash references taken anew; header revision 8's H8-5; `bm_DiscardAddrPage` to R4's 4 KiB rule on 16 KiB hosts (2.3.4 g; the integrator extended its Owns). Not in it, though this row named them before the platform round: the SH4 FPU fast-tier fixes of the arm64 JIT and **the arm64 ARM7 recompiler** (`PLATFORM-ROUND.md` PL-D46: unsafe on Apple silicon as written; with it the ARM7's fallback and notice of 2.5.3 would run for the first time), both in PLAN's backlog | `jit/backend/arm64/`, the tests and harness lines its brief names, `bm_DiscardAddrPage` and the guest load of `dbg_selftest_sh4bp.cpp` (by extension) | everything else | CIe | Linux x86_64 327 targets, P0 38 of 38 equal, compile commands unchanged; the arm64 host 330 targets (314 passed, 16 skipped), the Mac 340 (259 passed, 81 skipped), the Windows host 349 in `full` mode (272 passed, 77 skipped); the contract gates M1 18, M2 19, M3 20 on all three engines on both arm64 hosts, no `S04: recorded`, no `KNOWN LIMIT`; the 30 sh4bp targets on both arm64 hosts |
| **PLG** the platform round's gate (integrator) | `PLATFORM-ROUND.md` 2.14: the gate on Linux, the Windows host, the Mac and the arm64 host, the release files made here and their binary against P0, the hygiene of both repositories, this text brought to the round (its Appendix D), the two PR texts; the PRs and the tags by the integrator outside the package (section 7 of the cut) | the documents | — | everything above | **passed** (2026-10-24): the Linux part at the documents' head and the Windows host's, the Mac's and the arm64 host's at the merged tree `12069163`, with the UI at `7b88410` (`ui` `3.0.0-pre+g7b88410915`): the contract gates M1 18, M2 19, M3 20 passed on three engines on every system, M2's guest run 2 and M3's guest runs 3 and 2 with their notes; ctest Linux 327 targets twice, none skipped, the Windows host 349 (77 skipped), the Mac 340 (81 skipped), the arm64 host 330 (16 skipped); P0 38 of 38 rows, 12 sound lines; ThreadSanitizer, no report; `npm run check` and e2e in a fresh clone; the smoke spec; the x86_64 and arm64 AppImages, the Windows zip with its version resource and the macOS zip, each with its smoke run; the release AppImage's binary equal to P0 on its default rows; `actionlint` and the hygiene of both repositories with the owner's patterns, clean | |
| **J1** the run flag and the code cache (**done**: branch `skmp/nulldc3-j1`, 14 commits, head `ab97adbb`, merged as `c0c81436`; two verification rounds, three read-only reviewers beside the first) | as delivered: `Sh4Context::CpuRunning` is an `AtomicU32` (`oslib/atomic_u32.h`, new; not `std::atomic<u32>`, which cannot stand there: 2.3.7 item 10) with the place, size and meaning it had, every reader and writer of the word says its memory order, and the generated code of both JITs is unchanged; every backend object starts with an empty code cache, and the code region is prepared once per process (2.3.7 item 9); `IsRunning()` is one word of the machine, `SuperH4_impl::thread_live` (2.2.5, 2.3.7 item 12: the fix round of the verification); the three items that 6.1.2 then had | the declaration of `CpuRunning` in `hw/sh4/sh4_if.h` — a declaration change of a frozen header, made as header revision 5 (Appendix B) —, the uses of the word under `hw/sh4/` and `jit/`, the start of the code cache. **Touched outside this row, accepted by the integrator:** two statements in `libswirl.cpp` (the one word set in `Resume()` and cleared where the thread takes its stop callback); under `libswirl/debugger/` the new selftest `dbg_selftest_hostcycles.cpp`, one probe statement in `dbg_selftest_emu.cpp`, the reworded `g_parked` comment of `debugger.h` (header revision 5), and the `dbg_symbols.cpp` change of 6.1.2; the cache fix in `hw/sh4/dyna/driver.cpp`, where the cache and `LastAddr` live, not in `jit/backend/x64/` (which got a `verify`); `oslib/atomic_u32.h` and its include line in `types.h` | `libswirl/debugger/` but for comments that name the word (see the column before for what was accepted there); the block manager's handling of self-modifying code (untouched) | M1 merged | **passed**: 190 ctest targets, twice green (the 187 of before, `debugger.selftest.hostcycles[.interp]` and `debugger.sh4bp.threads`); the gates of 2.3.6 and the state hashes of 5.7 unchanged, the emitted x64 code the same instruction for instruction; ThreadSanitizer runs of the session `emu`, of `hostcycles` and of the M1 gate on the interpreter without a report of any kind; 2200 host stop / start cycles in one process without a cache clear (600 in ctest); no slowdown; arm64 kept source-consistent by reading (no host builds it). Not run: Windows (5.8, W64) and the window sessions (6.1.2) |
| **P-desktop** desktop targets only (**done**; a PLAN package, not of this track: listed for what it changes here; branch `skmp/nulldc3-p-desktop`, 18 commits, merged as `5ac00392`) | every target, host and build switch that is not a desktop one is removed from the tree (147 files deleted, 43 modified, 13,600 lines). What the tree builds for: desktop Linux, Windows and macOS on x86_64 and arm64, the generic host path (`-DTARGET_NO_REC=ON`, an unknown processor), and the emscripten scaffolding the wasm build starts from. For this design: `NULLDC_DEBUGGER` has one default (2), D7 describes the tree, and no section names a removed target any more | the build files, `libswirl/build.h` and the conditionals of removed targets across the tree | third-party directories; renderer, audio and input backends not tied to a removed target (later packages) | M1 merged | passed: the 187 ctest targets of that time, twice green; the default build and the builds with `-DHAS_TA_LLE=OFF`, `-DTARGET_NO_REC=ON`, `-DNULLDC_DEBUGGER=OFF` and for a generic host each compile; the state hashes of 2ndmix unchanged in each; every translation unit preprocesses to the text it had, but for three unused declarations in the GUI. Nothing for Windows, macOS or arm64 was compiled (no host) |

## Appendix A. Review dispositions

A.1 and A.2 are the dispositions of v2 and are kept as its record, unchanged; the section numbers in them are still
valid, because revision 3 kept the numbering. "D1-D6" there are the decisions as v2 stated them (D6 then read "E1 and E2
have started with the API in Appendix B; that API is frozen"). Where revision 3 changed what a disposition points to,
A.3 says so; the three places where a disposition names something that was built differently are: E-04 (the bits
`DBG_SERVICE` and `DBG_SWITCH` became a latch without a cause and the loop selection of 2.2.4), M-14 with S-06's retry
rule (determinism is solved, 5.6; the one repeat left is that of the booted-guest sessions of 2.3.6, which has another
cause), and E-01's `IsRunning()` (2.2.5).

### A.1 The 72 review issues

Issue ids are in review order: `E-nn` = review-emulator.md, `P-nn` = review-protocol.md (issues 1..25), `S-nn` =
review-sound-scope.md (S1..S23). 46 are applied as proposed, 26 are adapted, none is declined. "Adapted" names what
differs from the proposal and why (a decision D1-D6, another review, or the prototype).

| Issue | Disposition |
| --- | --- |
| E-01 | applied (2.2.5) |
| E-02 | adapted (2.2.1, 2.2.2): applied in full; the thread that runs calls when no emulator thread exists is the core thread (C), not the network thread, because E1's callbacks arrive on (N) and E1 has no timer hook; `Resume` / `Term` join a previous thread before taking L |
| E-03 | applied (2.2.2, 2.2.3); during a step the hook still latches for an event or sound breakpoint, and for nothing else |
| E-04 | adapted (2.2.4): applied in full; `DBG_SERVICE` added for the non-read calls of E-03; `DBG_SWITCH` exists only for the D1 fallback and is set inside a service excursion; `guest-exit` reason added |
| E-05 | adapted, D1 (2.3.4 k, 2.3.5): the interpreter gets a separate debug loop and a `Step()` in its backend object instead of three free functions (prototype). That loop serves the interpreter when it is the configured backend and, charging by decoder blocks, the fallback of JIT backends without native checks; `Run()` selects the fallback loop as proposed; there is no "interpreter while armed" on a native backend |
| E-06 | adapted, D1 (2.4, R13, 2.3.4 d): persistent slice counters, with gates B3 and B13; a native backend keeps its own timing while breakpoints are armed; the fallback charges by decoder blocks, and what remains different (FPU arithmetic) is a tick `notice` |
| E-07 | adapted (2.2.3, 2.2.7): items 3 to 6 applied. A SLEEP wait is cut short only by the stop latch of an event or sound breakpoint (S-01); a pause request and a service excursion are taken at the next slice end of the backend loop (bounded by 250 ms of host time), and the handler does not leave on `CpuRunning == false` — the prototype measured pause + resume exact only with the wait left intact (D1). A step on SLEEP runs the wait as a run does (prototype), not "exactly one slice" |
| E-08 | adapted (2.3.2, 2.3.4 j): step over as proposed, with the stack test bounded to the stack of the request (1 MiB) and applied to `TRAPA` too. Step out: no engine switch (D1); the `R15 >= sp0` rule is replaced by the stack-level rule the prototype needed — it stopped at the return of a stack-less callee — reported from block epilogues |
| E-09 | adapted, D1 (R9, R10, 2.3.4 e, h, k): a slot breakpoint stops in front of its branch whether or not the branch is taken (a compiled block runs the slot either way; the interpreter suppresses the second test), which replaces "only if the branch will be taken"; the resume exemption is tied to the stop address |
| E-10 | applied (2.3.3, 2.3.4 f, 2.10); `WITH_MMU` builds compare P1 / P2 masked |
| E-11 | applied (2.2.2) |
| E-12 | applied (2.2.3, 2.2.4) |
| E-13 | applied (2.2.6) |
| E-14 | adapted (6.1): reconciled with S-12 — all device directories including `hw/pvr`, `hw/maple` go to E3; `hw/sh4`, `jit/` and `reios/` are split by file between E4a (thread, park, call sites) and E4b (the D1 mechanism, including `dyna/blockmanager.*`; `dbg_mem` only calls the existing `bm_LockedWrite`); `sh4_if.h` is an E0 header |
| E-15 | applied (6.1); a ninth contract covers the SH4 mechanism |
| E-16 | adapted (2.2.1, 2.3.2): applied; the error code follows the state: `-32001` in `Stopped`, `-32002` in `NoMachine` (merged with E-02 and P-05) |
| E-17 | applied (2.11; frozen in E1) |
| E-18 | applied (2.6) |
| E-19 | applied (2.7; item g in 2.2) |
| E-20 | applied (2.3.2), code `-32005`; an unreadable PC and the slot preconditions (unreadable, undefined, writes PC) added |
| E-21 | adapted (2.2.8): the whole `dc.sh4.cpu` path is omitted with `registersStale` (P-02), because a path present in a tick is complete (P-01) |
| E-22 | applied (2.2.9) |
| E-23 | applied (2.10) |
| E-24 | applied (2.2.9): frozen frame stated, plus the window title of S-20 |
| P-01 | applied (3.2, 2.9) |
| P-02 | applied (2.8, 3.2) |
| P-03 | applied (4.2, 4.3) |
| P-04 | adapted (3.0, 2.11): open enums, version compare and describe params applied; `version.json` is merged into S-08's `bundle-info.json` |
| P-05 | adapted (2.2.1, 2.3.2): lifetime reported as `paused` with a reason and a tick per change; the reason is `no-game` (E-02's state model); codes `-32001` / `-32002` by state; `control.pause` also returns the error (E-16, stricter) instead of being a no-op |
| P-06 | applied (2.7, 3.3, 4.3) |
| P-07 | applied (5.5), combined with S-11's anchor |
| P-08 | adapted (2.6): applied; the high-rate class is defined by its criterion (frame rate or faster) and so also contains the render, DMA-transfer and channel-loop events |
| P-09 | applied (3.3, 3.4, 4.5); code `-32014` assigned to the 5 s timeout |
| P-10 | applied (4.3, 2.11) |
| P-11 | adapted (3.1, 3.5): `-32004` and the "missing target" rule applied; `Target.stepping` added, because M1 serves ARM7 / DSP memory and disassembly but not their run control |
| P-12 | applied (2.3.1, 3.2, 4.3) |
| P-13 | applied (2.8, 4.3) |
| P-14 | applied (4.3) |
| P-15 | adapted (2.11; frozen in E1): applied, except that the `Host` rule is S-07's stricter one (`AllowedHosts`, not "the configured Bind address") |
| P-16 | applied (1, 4.1, 4.2) |
| P-17 | applied (4.4, 2.8, 3.1) |
| P-18 | applied (3.0) |
| P-19 | applied (3.2) |
| P-20 | applied (2.8) |
| P-21 | applied (2.9, 2.10, 3.2) |
| P-22 | applied (3.1, 4.3) |
| P-23 | applied (4.4) |
| P-24 | applied (3.3, 3.6); `state.getMemoryBinary` bounded at 16 MiB |
| P-25 | applied (4.2) |
| S-01 | adapted (2.2.3, 2.2.4): rules 1, 2 and 4 applied. Rule 3 (leave the SLEEP wait at the latch) applies to the causes that need the scheduler frozen at once, event and sound breakpoints; a pause request and a service excursion are latched at the next slice end of the backend loop, which keeps them exact (D1, prototype). Rule 5: `UpdateINTC()` on leaving the park only when the latch suppressed an acceptance, and with the PC moved back past a SLEEP that the latch cut short, so that the interrupt returns after the SLEEP; an SH4 breakpoint hit latches at the instruction |
| S-02 | applied (2.5.2) |
| S-03 | adapted (2.5.5, 2.5.3, 2.5.4): applied; error code `-32012` (`-32011` is "no capture", P-09); the empty-program walk is expressed against the T5.0 core (D2) |
| S-04 | adapted (2.5.3): applied; on hosts with the ARM7 JIT the interpreter path is a fallback in the sense of D1 and carries a tick `notice` |
| S-05 | adapted, D2 (2.5.4): `Begin / StepRange / End` is the interface of the T5.0 core; the debugger adds nothing to serialised state; the "output differs from the JIT" caveat does not apply (T5.0 makes the JITs match or drops them) |
| S-06 | adapted (5.6, 5.7): applied; built in round 1 (E3, inside `hw/aica/`) so the M1 no-regression gates use it; an SH4 register fold, a repeatability acceptance step and a retry rule for comparisons added |
| S-07 | applied (2.11; frozen in E1); `getMemorySlice.length` takes P-24's stricter 65536 |
| S-08 | adapted (2.11): applied as E1 built it — `debugger.cmake` belongs to E1 from the start (D6); the protocol number is read from `dbg_webui.h`; `cmake_minimum_required` stays 3.10 and embedding needs CMake 3.19 |
| S-09 | applied (2, 5.8) |
| S-10 | adapted (5.2, 5.3): applied; the selftest is started with `-config Debugger:SelfTest=...` (the config spelling of S-19; no new argument); the opcode cross-check excepts the reios trap encoding, as E2 defines `valid` |
| S-11 | applied (5.4, 5.5); the DSP test program is named |
| S-12 | adapted (6.1): reconciled with E-14 and D6 (see E-14); the reios hook call belongs to E4a, the `dbg_symbols` body to E5 |
| S-13 | adapted (1, 2.8, 4.3): applied; follow / refetch is keyed on `stopId` (P-12) instead of "every paused tick" |
| S-14 | applied (2, 2.2.3) |
| S-15 | applied (2) |
| S-16 | applied (2.8) |
| S-17 | applied (2.7) |
| S-18 | applied (2), with the OFF list E1 implemented |
| S-19 | adapted (2.11): cache headers and config spelling applied; the fallback rule is P-15's (extension-less paths), as frozen for E1 by D6 |
| S-20 | applied (2.2.2, 2.9, 2.2.9); the grace timer starts only at a disconnect |
| S-21 | applied (2.5.2) |
| S-22 | adapted (2.5.5, 4.5): capture forcing applied; under D2 the playground runs the same core, so the "results differ" note is void; the banner names the snapshot's source |
| S-23 | applied (1.1); "Offline" is placed in M2 |

### A.2 What the mechanism prototype changed

The prototype of 2.3.4 (Appendix C) was written after the reviews. Where it contradicted D1's wording, a review proposal
or an assumption of the v2 draft (the text written from the three reviews before the prototype existed), the
prototype's behaviour is what this document specifies.

| # | Was | Is | Reason |
| --- | --- | --- | --- |
| M-1 | D1: after a stop, resume runs "a block compiled starting at that PC", first instruction exempt | the rest of the interrupted block runs as a temporary block with that block's end and the refunded cost; the helper-side exemption is kept and consumed by it (2.3.4 h) | a cached block at that PC has its own cost (`fast`: 1, or an idle multiple) and, after a size-limit end, its own boundary: stop + resume would not be neutral. Measured exact with the temporary block |
| M-2 | D1: "the cycles of the unexecuted remainder are refunded, so scheduler time is exactly what the active timing mode charges up to that instruction" | refund = block cost minus its proportional share for the instructions in front; exact with per-instruction cost models, a definition in `fast` timing, where an ordinary block shows nothing charged until it completes (2.3.4 b) | `fast` timing charges blocks, not instructions; the choice only affects the time a stop shows |
| M-3 | D1: a step is a one-instruction block "through the same cost function" | as stated; the consequence is written down: in `fast` timing n steps cost n cycles where the run costs one block (2.4) | inherent in the decision |
| M-4 | draft: step out by a call / return depth counter, tested in front of every call and return, with an internal stop before the qualifying return followed by a step, and a discard of the whole code cache when tracking toggles. E-08: first `RTS` / `RTE` with `R15 >= sp0` | stack-level rule with calls at the request's level counted and handlers entered later skipped; reported after the instruction from block epilogues; blocks discarded by end type (2.3.4 j) | E-08's rule stopped at the return of a stack-less callee in the prototype; a whole-cache reset also clears `page_has_data` and changes later SMC handling |
| M-5 | draft R8 and E-09: a slot breakpoint hits only if the branch will be taken; an untaken slot hits by itself with PC = the slot | always in front of the branch, once (R10) | a compiled `bt.s` / `bf.s` block runs its slot either way; the interpreter is made to agree |
| M-6 | draft: the fallback runs the plain interpreter debug loop; its notice says "timing and arithmetic differ"; each switch starts a fresh slice | the fallback charges by decoder blocks, so only arithmetic differs and the notice says so; the lost partial slice at a switch remains only on backends whose counter does not yet persist (2.3.5) | D1: a fallback must charge time as the active timing mode does |
| M-7 | draft R10: a step is entered and left with `CpuRunning == false`; E-07: a step over SLEEP is exactly one slice | a step runs with `CpuRunning` set; a step on SLEEP waits for the interrupt or the handler's bound (R12, 2.3.2) | "what you debug is what runs": `UpdateSystem()` and SLEEP behave as in a run |
| M-8 | draft 2.2.3: every latch cuts a SLEEP wait short; the handler also leaves on `CpuRunning == false`; documented skew for every debugger stop inside SLEEP | only event and sound breakpoints latch inside a SLEEP wait; pause requests and service excursions are taken at the next slice end of the backend loop (liveness bound: 250 ms of host time); a deferred interrupt accepted after a cut SLEEP returns after the SLEEP (2.2.3, 2.2.4 c) | the prototype's pause + resume (227 351 of them, also inside SLEEP waits) is exact only with the wait intact; with the draft's rule a bookkeeping call made while the guest idles would shift guest time, and the deferred acceptance would have returned into the SLEEP |
| M-9 | draft R13: backend virtuals `DebugCaps / DebugStep / DebugInvalidate` with bits `SH4DBG_BP`, `SH4DBG_STEP`; debugger entry points `BpQuery / BpHit / TrackEntry` | the prototype's interface: `dbg_sh4_WantsCheck / Check / CheckFlow / OnException / TakeRemainder`, `SuperH4Backend::Step()`, `ngen_features::DebugChecks`; one capability bit `SH4DBG_NATIVE` (2.3.4 l) | the helper needs the refund and the block end; the flag covers all items of 2.3.5 together |
| M-10 | dispatcher and interpreter reset the slice counter at loop entry | the counter survives (2.3.4 d); this also corrects an ordinary host pause / resume | R13 |
| M-11 | decoder | size limits ignore check ops; `srt_waw` treats the op as a T reader; the DIV32 idiom is not folded over an armed address or in temporary blocks (2.3.4 a) | otherwise a check could move a block end, show a stale T, or be skipped |
| M-12 | draft: the x64 JIT uses the fallback until E4b merges, so E5 can start on E4a alone | the interpreter loop and the fallback are part of the mechanism package; E5 starts when E4b is merged (6) | one implementation of the loop; the prototype exists |
| M-13 | draft gates G1-G4 | gates B1-B14 (2.3.6): the specification's twelve tests plus the draft's two step-exactness gates (G3 = B13, G4 = B14); G1 and G2 are B3 and B1 / B2 / B8 | one list |
| M-14 | — | hash comparisons are repeated before they fail (2.3.6, 5.6) | one prototype run in about 150 diverged with identical inputs |

### A.3 Revision 3

**The decisions of 2026-10-09** (`DESIGN-V3-DECISIONS.md`), and the sections each one changed:

| # | Decision | Sections |
| --- | --- | --- |
| 1 | watch ids, like breakpoint ids, are never reused | 2.3.3, 2.8, 5.5 (I8) |
| 2 | `eventLog[].timestamp` is wall-clock ms like the tick's; ids ascend and are stable | 2.6, 2.9, 3.2, 5.5 (I2, I3), 4.2.1 |
| 3 | malformed requests: `-32600` and the `id` of its answer; notifications ignored; `-32700` | 3.0, 3.5, 5.5 (I1, S12), 4.2.1 |
| 4 | error codes are normative, message texts are not | 3.5, 2.3.2, 2.5.3, 2.5.4, 5.5 (S12), 4.2.1 |
| 5 | the target table is normative but for label and defaults; an M1 emulator lists all three targets | 3.1, 1, 5.5 (S01), 4.2.1, 6.1 (E5) |
| 6 | the normative parts of the device tree; events listed are events raised; fewer than 256 registers allowed | 2.8, 2.6, 2.3.3, 3.3, 5.5 (S01, S08), 6.1.1, 4.2.1 (the mock's tree) |
| 7 | a running target is asked things; watches are evaluated in every tick | 3.3, 2.8, 2.10, 2.2.8, 5.5 (I6, S03, S19, S20), 4.2.1 |
| 8 | re-enabling a breakpoint is shown at the interrupt entry `VBR + 0x600` | 5.5 (S07, S20), 5.5.3, 4.2.1 |
| 9 | what a gate is; the gate invocations | 5.5.1, 5.5.3, 1, 4.1, 5.1, 5.4, 6.1 (E5), 6.2 |
| 10 | the 1 MiB inbound bound is probed, next to the other bounds | 3.6, 2.11, 5.5 (S07 to S12) |
| 11 | `control.pause {target}`: ignored for the three target ids, `-32602` otherwise | 2.3.2, 3.3, 3.5, 5.5 (S02), 4.2.1 |
| 12 | running ticks carry four base paths; `registersStale` never in a paused tick | 2.8, 2.2.8, 3.2, 5.5 (I5) |
| 13 | the ARM7 callstack: at least the two frames PC and R14 while the ARM7 runs (the stack scan is kept), `[]` while it is held in reset and in running ticks | 2.10, 2.5.3, 5.3, 5.5 (S11), 4.2.1 |
| 14 | `dc.holly.core.vblank` is raised by every running machine and is in the M1 tree | 2.6, 2.8, 5.5 (S07, S20) |
| 15 | no game loaded: `registers: {}`, the client drops its values; leaving `no-game` | 2.3.1, 2.2.1, 3.1, 3.2, 4.3, 5.3, 5.5 (I5, 5.5.4) |
| 16 | a client may skip queued stops and ends on the latest | 3.2, 4.3 |
| 17 | `breakpointId` leaves the ticks of a stop when its breakpoint is removed | 2.3.1, 3.2, 5.5 (I7, S05), 4.2.1 |
| 18 | reconnect delays; an explicit endpoint is retried and not replaced | 4.3 |
| 19 | without a connection the panels keep their frame | 4.3.1; 4.3 since the devtools round |
| 20 | repeated refusals are shown once with a count | 4.3, 3.5 |
| 21 | `Target.memory: false` is pinned with a scripted server | 4.3 |
| 22 | DSP disassembly `bytes`; the widths of the `dc.aica.dsp` registers | 3.3, 2.8, 5.5 (S10), 6.1.1, 4.2.1 |
| 23 | header revisions 2 and 3 are the contract; revision 4 is comment-only | D6, 2.2.1, 2.2.4, 2.2.5, 2.7, Appendix B, 6.1.1, `DESIGN-V3-OPEN.md` |
| 24 | the mechanism as built is the specification | 2.3.4 (a, c, d, h, i, k, l), 2.3.5, 2.3.6, 2.3.7, 6.1 |
| 25 | only the x64 and arm64 JITs and the interpreter exist; no user-facing interpreter choice | D7, D9, 1.1, 2 (build), 2.2.8, 2.3.3, 2.3.5, 2.3.7, 2.5.3, 6.3 |
| 26 | per-sample forcing is transitional | D2, 2.5, 2.5.2, 2.5.4, 2.5.5, 2.6, 2.8, 3.1, 3.2, 4.5, 6.3 |
| 27 | the server is the control surface; off in automated runs unless enabled explicitly; save states and the game list leave | D8, 1, 1.1, 2 (rules), 2.2.1, 2.2.5, 2.2.9, 2.9, 2.11, 3.0, 5.3, 5.4, 5.7, 6.1 (E5) |
| 28 | known limits: the x64 code cache over host stop / start cycles, `CpuRunning`, the two skews | 2.3.7 (9 to 12), 2.4, 5.7 |
| 29 | verification: headless runs, the integration recipe, a sounding guest, the machine limits | D10, 5 (rules), 5.4, 5.6, 6 (rules) |

**Applied with a difference.** Eleven decisions are in the text in a form that differs from their wording; each line
says what differs and why. Seven of them were put as open questions and confirmed on 2026-10-09 (the last column names
the question; the answers are listed below and in `DESIGN-V3-OPEN.md`); the other four are what was built.

| # | What the text has, against the decision's wording | Why | Confirmed |
| --- | --- | --- | --- |
| 3 | only a *well-formed* message without `id` is a notification and ignored; a malformed one without `id` is answered `-32600` with `id: null` (3.0) | the decision's two rules overlap for such a message; the mock answers, and a client that sends rubbish gets told | Q3 |
| 7 | a watch is `"n/a"` also for a register that reads `"n/a"` in the register list (no side-effect-free reader, a device with nothing to read from yet, no machine); running adds the one case of the decision, `dc.sh4.cpu.*` under `registersStale` (2.8) | the decision names the running case only; a watch is formatted exactly like its register, and some registers are `"n/a"` always | — |
| 8 | the mock's fabricated SH4 takes an interrupt at every vblank through a handler at `VBR + 0x600` (4.2.1, 5.5.3); `VBR` is read at a stop made after the target has run, not at the anchor (S07, S20) | "partial" fails a milestone gate (decision 9), so a mock without interrupts could not pass its own M1 gate; at the anchor the program has not installed its vector base | Q2 |
| 9 | the DSP part of M3 is a scenario of its own, S21, selected by `--milestone M3 --dsp-program`, which selects S00 and S21 alone (5.5.3) | the first run, with 2ndmix, cannot pass a scenario that needs a loaded DSP, and the second cannot be told from the first without an option | Q1 |
| 13 | *at least* two frames while the ARM7 runs: the stack scan of the frozen `dbg_callstack.h` is kept (2.10, S11) | the header's contract and the mock have the scan; "exactly two" would drop frames the unwinder finds | Q5 |
| 15 | between the last `no-game` tick and the start (or running) tick there is a paused tick with `reason: "host-stopped"` and a new `stopId`, and, in front of a start park, the `running` tick of the thread start (2.3.1) | as built the machine is created `Stopped` before its thread starts, the thread start publishes `Running` (`OnThreadStarting()`), and every lifetime change is published; the client's rule (read again when the reason leaves `no-game`) is the decision's | — |
| 22 | DSP `bytes` is 8 space-separated upper-case pairs, each word high byte first, not 16 unbroken digits (3.3, S10); `MDEC_CT` has width 16 already now and shows the counter modulo 2^16 until the T5.0 core is wired in (2.8) | one form of `bytes` for every target (pairs in a row), which the mock sends and the runner's S10 checks; the DSP that runs today keeps the counter in 32 bits | Q15; Q6 |
| 24 | a block over the check budget keeps its decoded boundaries and its cost; only its *compiled code* ends early and hands the rest over as a temporary block (2.3.4 c) | that is the mechanism as built, and it is what makes the split invisible to the guest; "the block is ended early" would read as a moved block end | — |
| 25 | the fallback is not for arm64 alone: the x64 JIT uses it in Windows builds until the run of 5.8 (`DebugChecks` is compiled off under `_WIN32`), and the test key `Debugger:ForceFallback` selects it on any JIT (1.1, 2.3.5, 2.3.7 item 7, 5.8) | as built: the check, the stop path and `RunBlock` have only executed with the System V calling convention; the key is how the fallback is gated on the development machine, which has no arm64 host (B12, `debugger.contract.m1.fallback`) | Q9 |
| 26 | the three forcing names (`NOTICE_DSP_FORCED`, `EV_DEBUGGER_DSP_FORCED`, the capability string `dsp-forced` of the schema) stay declared until the header revision of the M3 work; nothing sets, raises or lists them (2.5.5, 6.1.1 item 2, 4.2.1) | removing them is a declaration change of frozen headers (decision 23: none now) | — |
| 27 | an automated run is headless, or has one of `Exit.AfterVblanks`, `Exit.AfterSeconds`, `Exit.OnGuestExit` — not "any `Exit.*` key": `Exit.StateHash`, `Exit.FrameHash` or `Exit.FramePng` alone do not make a run automated (Terms, 2.11). And the server is off by default in one more case, a selftest session (`Debugger:SelfTest` set), with a window as well as headless (2.11, 5.3) | one definition, `RunControl::automated()` as built: the keys that end a run. A session's tests stand in place of a client: they set the event gates and the active mask themselves (`dbg_selftest.h`), and the tests of `core` need a session without a core thread | Q16 (the keys); the selftest case was not a question |

Where a decision reaches code that is merged, the change is listed where it is made: 6.1.1 (the emulator), 4.2.1
(the runner and the mock), 4.3.1 (the panels) — all three applied since, but for item 8 of 6.1.1, which decision 31
added — and 4.2.2 (the runner and the mock after decisions 30 to 37, still to do).

**The independent check of revision 3** found two contradictions and eighteen gaps; all twenty are settled in the
text. The contradictions: the grace note is appended to the event ring by the core itself, with every gate closed
(2.6, 2.9, 5.4, header item H9); decision 13 reads "at least two frames" in every place (2.10, 4.2.1, 5.3, S11, row 13
above). The gaps, by section: the lock order of `dbg::emu` as the code has it (2.2); the one repeat that is left and
its cause (2.3.6, 5.6, Appendix C); the session `regs` in ctest and B11 outside the selftests (1, 5.3, 6.1.1 item 6);
headless is Linux only (D10, 5, 5.8); three more header comments (H17 to H19); what E5's row depends on and how its
Windows item is closed meanwhile (6.1); a private header for the core's selftest and for the ring, and `json.hpp` in
`dbg_selftest_core.cpp` (2, 2.1, 5.3, 6.1); the unstated cases of `dbg::Init` (2.11, 2.2.9, Q4 widened); the order of
`breakpoints.add`'s checks, `control.runUntil`'s two `-32003`s, `sp` in a frame (3.0, 3.2, 3.3, 2.2.2, 2.3.3, 6.1);
strings that are not UTF-8 (3.0 item 9, 2.10, 5.2, 5.3); the state-hash tests as ctest targets and the emulator the
contract tests run on (5.7, 5.4); where S07 and S20 read `VBR` (5.5, 4.2.1); `"n/a"` in a watch (2.8, S03); this
list of differences, with Q15 and Q16; decision 4 and 6 in the schema and the mock (4.2.1); RAM writes without the
virtual memory layout (2.7); the owner of the sounding guest (6.3, 5.5, 5.6); no forcing in M2 (6.3, 2.5.2).

**A second pass of that check** found eight more gaps; all are settled in the text. How the two tick keys reach the
core: `core::Configure` in E5's private header, which `dbg_main.cpp` includes too (2.1, 2.11, 6.1). No server in a
selftest session unless it is enabled explicitly (2.11, 2.2.9, 5.3, 6.1, header item H8, row 27 above). RAM writes
without the virtual memory layout, which the frozen headers still refused (2.7, 2.3.4 l, header item H20). The
remaining cases of `dbg::Init`: what a loopback `Bind` is, every way a server cannot be started and that the core is
stopped again then, the stream of the port line (2.11, 2.2.5, 5.4, 6.1, header items H8 and H21). Members of a request
and keys of `params` that a server does not know are ignored (3.0, S12, 4.2.1 item 7). The case of a watch's path,
and a duplicate inside one `state.watch` call (2.8, 3.3, 5.3 `core.tables`). Decision 25 among the differences (row 25
above; the B64 row of 6.1). A test that can fail for the default of automated runs (5.7
`debugger.contract.hash.off`, the gate row of E5 in 6.1).

**A third pass of that check** (`design-v3-check-r3.json`) found no contradiction of a decision, six gaps that
should be closed before E5 and six notes; all twelve are settled in the text, one line each:

| # | What was missing or wrong | Settled in |
| --- | --- | --- |
| 1 | removing `dc.debugger.dsp_forced` from the `dc` node turns `debugger.regs.tables` red: the test wants every event row listed by exactly one node | 6.1.1 item 2 (the expectation changes with the table: exactly once, by the node the row names, except `EV_DEBUGGER_DSP_FORCED`, by none; this pass first wrote "at most once", which decision 42 replaced) |
| 2 | the mock answers the `-32004` of a capability it was configured without only behind `-32602` and the lifetime errors, against 3.0 | 4.2.1 (the mock is corrected; runner item 7 probes it with a missing parameter), 3.0 (which error wins), 5.5 (S12), "What was built" below |
| 3 | two runner changes were not listed: the invariant that wants a `breakpointId` with every `breakpoint` / `event` reason, and the pair count of a `dsp` line | 4.2.1 (runner item 9 rewritten, item 13 new) |
| 4 | the Windows item of E5's gate named a compiler the development machine does not have, and neither the command nor what counts as passed | 5.8, 6.1 (the gate row of E5): the MinGW-w64 compiler that is there, as the header review of E0 used it; a file a tree header stops is reviewed |
| 5 | `debugger.contract.hash.idlebp` named no address and could pass on one that is never executed | 5.7 (a symbol of 2ndmix.elf reached in every frame, named by the CMake line, shown to be reached in the README of the test directory) |
| 6 | what a memory slice answers across the end of the address space: `dbg::mem::Read` wraps, the mock does not | 2.7 (no wrap: a byte above `addressMax` is unreadable; the core cuts the length), 3.3 (the row of `state.getMemorySlice`), 6.1 (what E5 implements) |
| 7 | "`-32003` is for steps alone" was wider than 2.2.2, 2.3.2 and 3.5 | 3.3 (among requests to a running target) |
| 8 | `state.recordSgcFrames` as a table call that arms a resume and answers lifetime errors cannot be written on the frozen `dbg_emu.h` | 3.3 (the row and its paragraph: exec when paused, table when running, the lifetime errors answered by the core; no declaration changes) |
| 9 | "the debugger never writes a configuration key" read as forbidding the settings methods of track 10 | 2 (rules: nothing of track 1 writes a key; a key a person saves through those methods is a person's), 6.1 (E5) |
| 10 | four details without a rule: the hex case of `location`, the port line of an IPv6 `Bind`, where `client` is logged, which count "a client is connected" is | 2.10 and 3.2 (lower-case hex, header item H22); 2.11 and 6.1 (brackets; the `Debugger: client` line; header item H8); 3.0; 2.6, 2.9 and 6.1 contract 4 (the core's own count, not `HasClients()`; header item H9) |
| 11 | two differences from a decision's wording were in the text and not in the table | "Applied with a difference" above (row 7 new, row 15 extended), 2.3.1 (the `running` tick in front of a start park) |
| 12 | `control.pause`'s target rule was listed as work for the mock, which has it | 4.2.1 ("it does already") |

**The open questions answered on 2026-10-09.** Thirteen of the sixteen questions of `DESIGN-V3-OPEN.md` were
answered; the answers are final and are in the text as design. Q10, Q13 and Q14 stay open. One case that the
answer to Q4 did not name (Q17: `StartPaused=yes` with a server that was enabled and cannot be started) was answered
the same day: fatal, as under `Required` (2.11, 6.1).

| Q | Answer | Sections |
| --- | --- | --- |
| Q1 | the DSP part of the M3 gate is scenario S21, run with `--milestone M3 --dsp-program` | 5.5 (S21), 5.5.3, 4.5, 1, 6.2, 6.3 — as written; row 9 above confirmed |
| Q2 | the mock's fabricated SH4 takes an interrupt at every vblank through a handler at `VBR + 0x600` | 4.2.1, 5.5.3 — as written; row 8 above confirmed |
| Q3 | only a well-formed message without `id` is a notification; a malformed one is answered `-32600` with `id: null` | 3.0, 4.2.1 (runner item 7), 5.5 (S12), 6.1 (E5 depends on nothing open) |
| Q4 | `Debugger:Required=yes` or `Debugger:StartPaused=yes` while the server is not enabled is a configuration error: one line on stderr, exit code 2; a selftest session is excepted for `StartPaused` | 2.11 (the two rows, "Keys that need a server, without one"), 2.2.9, 5.3, 5.4 (the recipe; `debugger.contract.config`), 6.1 (E5: the row, the order of `dbg::Init`, the gate), 6.1.1 item 7 (`debugger.selftest.startpaused` sets `Enabled=yes`), header item H8 |
| Q5 | at least two ARM7 frames, more when the stack scan finds them | 2.10; row 13 above confirmed |
| Q6 | `MDEC_CT` is shown at width 16 already now | 2.8 (`dc.aica.dsp`), 6.1.1 item 1; row 22 above confirmed |
| Q7 | protocol v2 compatibility of the client is dropped after M1, with F3: a result without `protocol` gets the version banner and nothing else is promised; until then the M1 client keeps what it has | 3.0 (its own rule), 3.1, 3.2, 3.4, 4.2, 4.3, 4.4, 6.2 |
| Q8 | the default layouts must be readable at 1366x768, with one e2e run at that size; in the devtools round if it fits, else in F3 | 4.3.1 item 7, 4.4, 6.2; the row "a supported minimum window size" left the table of feedback not adopted |
| Q9 | E5 merges on the syntax-only MinGW-w64 check and the review against the MSVC rules; the real MSVC build is made before the first push; until a Windows run of the breakpoint gates, x64 on Windows uses the interpreter fallback with its notice | 5.8, 6.1 (the gate row of E5), 6.3 (W64); row 25 above confirmed |
| Q11 | the sounding guest `tests/debugger/guest/dsptest/` is written as the first step of E6 and serves the sound harness and, with its DSP program part, the M3 gate | 5.5, 5.6, 6.3 (E6, E8) |
| Q12 | `Sh4Context::CpuRunning` becomes an atomic in one small package after M1, together with the fix of the x64 code cache over thread starts | 2.3.7 items 9 and 10, 5.7, 6 (state), 6.3 (package J1); the row of the feedback table below |
| Q15 | DSP `bytes` are 8 space-separated upper-case pairs, each word high byte first | 3.3, 5.5 (S10), 4.2.1 (runner item 13); row 22 above confirmed |
| Q16 | "automated" is `RunControl::automated()`: headless, or one of `Exit.AfterVblanks`, `Exit.AfterSeconds`, `Exit.OnGuestExit` | Terms, 2.11; row 27 above confirmed |

**After the devtools round (decisions 30-45).** The round (4.2.1, 4.3.1) and the corrections of 6.1.1 with header
revision 4 were verified and passed; that they are done is in 1 (the M0 and M1 rows), 4 (its first paragraph), 4.1,
4.2.1, 4.3.1, 5.1, 6 (state) and 6.2. Their verifiers left sixteen points, which `DESIGN-V3-DECISIONS.md` (section C,
2026-10-10) answers; the answers are final and are in the text as design.

| # | Decision | Sections |
| --- | --- | --- |
| 30 | S04: one step may end at an exception vector; a second step in a row that does fails; the step out of S04 follows completed steps | 2.3.2, 5.5 (S04), 4.2.2 (item 1) |
| 31 | S11: the ARM7 held in reset has an observable, the register `ENABLED` of `dc.aica.arm7` (width 1, an anchor register; 1 = clocked, 0 = held in reset); in a paused tick `callstacks.arm7` is `[]` exactly when it reads 0. ARMRST is not the observable (the decision was revised the same day, after the reset order of the tree was found) | 2.10, 2.8 (the normative list, the row and the paragraph of `dc.aica.arm7`), 2.7 (ARMRST), 2.5.3, 2.3.7 (item 13), 5.5 (S01, S11), 5.5.3, 4.2.1 (items 3 and 6), 4.2.2 (item 2, the mock), 6.1 (E5: "Depends on", what E5 implements), 6.1.1 (item 8), 6 (state), 1 (M1) |
| 32 | the widths of the `dc.aica.dsp` registers are normative for every server that lists them | 2.8, 5.5 (S01), 4.2.1 (item 3), 4.2.2 (item 3) |
| 33 | second-level nodes without registers is the emulator's choice, not contract | 2.8, 5.5.4, 4.2.2 |
| 34 | the gates are run with the default `TickMs` and `FullTickEvery`; "only running ticks for 600 ms" asks for no paused tick and nothing else | 5.5.3, 5.5 (its first paragraph, S03, S06, S07), 5.5.4, 5.4, 2.11, 4.2.2 (item 4) |
| 35 | F4 gives the mock `state.getMemoryBinary` with the S17 part, so the mock keeps passing S17 without `--milestone` | 4.5, 5.5 (S17), 5.5.3, 6.2 (F4), 4.2.2 |
| 36 | S18: `icache` / `ocache` have rows, or no rows and a note. S16: what an ARM7 step asserts | 5.5 (S16, S18), 4.2.2 (item 5) |
| 37 | a server that stops answering: the remaining scenarios fail as "not run" | 5.5.1, 4.2.2 (item 6) |
| 38 | disassembly in narrow panels: 12 characters at least for the instruction column, a sideways scroll, nothing cut; the memory view gets the rule in F3 | 4.3 (Disassembly, Memory, Default layouts), 4.3.1, 4.4, 6.2 (F3) |
| 39 | what "frame" is without a connection: text typed and not sent is kept; when an address box is rewritten | 4.3 (Without a connection), 4.3.1 |
| 40 | focus over a reconnect: a row control gets it back only while the focus is on the page body | 4.3 (Without a connection), 4.3.1 |
| 41 | toolbar alignment is not contract | 4.3 (Toolbar), 4.3.1 |
| 42 | `debugger.regs.tables`: every event row is listed by exactly one node, except `EV_DEBUGGER_DSP_FORCED`, by none | 6.1.1 item 2, 5.2, the third pass above (row 1) |
| 43 | the sh4bp README: the two stand-ins keep the latch test in front of the attention word, and the "Per slice" paragraph says so | 6.1.1 item 3, 2.3.6 |
| 44 | the 1 MiB bound is answered with close code 1009, the close frame on the wire before the socket is closed; E5 may fix the transport for it | 3.6, 2.11, 5.5 (S12), 5.2, 6.1 (E5: "Must not touch", what E5 implements) |
| 45 | header revision 4 is applied; the 180-column line is re-wrapped; the docs copies are part of 6.1.1 item 5 | the status line and the first paragraph, D6, 1 (M1), 2.7, 5.3, 6 (state), 6.1 (E5: "Must not touch", "Depends on"; contract 4), 6.1.1 (its first paragraph, item 5), Appendix B, `DESIGN-V3-OPEN.md` (section 2, and section 3 for revision 5) |

**E5 as built.** The core was built to this text, verified in two rounds and merged (6.1). Its implementer listed
seventeen points where the text was wrong or silent or where the code differs from it, and three things that could
not be done. The rule: the verified implementation is right unless a decision says otherwise. No point needed a
change of a frozen header, of the runner or of the code.

| # | Point | Outcome | Sections |
| --- | --- | --- | --- |
| 1 | the slice example `{sh4, 0xFFFFFFF0, 32}` answers `-32010` on the emulator | text corrected; the no-wrap rule stands | 2.7, 5.3 (`core.tick`) |
| 2 | the bound of 256 watches cannot be reached on the M1 tree (247 registers, no expression twice) | a limit until M2, listed; the test description corrected | 5.3 (`core.tables`), 5.7 |
| 3 | `core.tick` is a machine test: its ticks are running ticks | text corrected: paused-tick rules are in `core.guest` and `core.lifetime` | 5.3 |
| 4 | no section for `core` in `tests/debugger/selftest/README.md` | left for the next emulator package; done in J1 | 6.1.2 |
| 5 | a server that cannot be started: two lines on stderr, the transport's and `dbg::Init`'s | text corrected | 2.11, 6.1 |
| 6 | a connect and a disconnect make a `CC_TABLE` call; what is retried, and when | text added | 2.6, 2.9 |
| 7 | the grace note is appended inside the table call of the expiry; its text is fixed | text added | 2.6, 2.9 |
| 8 | the protocol note is logged for every describe with another number | text added, as built (the design owner may still choose once per connection) | 2.6 |
| 9 | the `Debugger: client` line: the first well-formed describe with a `client` string, before the dispatch | text corrected | 2.11 |
| 10 | the stack scan ends at the end of the memory the stack pointer is in | text added | 2.10 |
| 11 | several symbols at one address: the order; absolute symbols are taken | text added, as built, with what it costs with KallistiOS programs; the design owner then ranked absolute symbols last, done in J1 | 2.10, 6.1.2 |
| 12 | `state.getDisassembly.context` has to be a number when present | text corrected | 3.3 |
| 13 | ids echoed in the JSON library's number form; integral floats accepted for unsigned parameters | text added | 3.0 |
| 14 | the contract tests add only `config:Dynarec.Enabled=yes\|no` to the recipe | text added | 5.4 |
| 15 | the third run of `debugger.contract.hash.off` runs 30 vblanks | text corrected | 5.7 |
| 16 | `debugger.contract.grace` needs only `NULLDC_ELF_DIR` | text corrected | 5.4, `DESIGN-V3-OPEN.md` (Q14) |
| 17 | the state of `dbg_core.cpp` is never destroyed, so an `exit()` past `dbg::Term` is safe | text added | 2.11 |
| — | not done: a real MSVC build | as 5.8 says; what the syntax checks showed is recorded | 5.8 |
| — | not done: `NoMachine` over a real socket; `registersStale` on this machine | limits, listed | 5.5.4, 5.7 |
| — | not done in the first hand-over: a test of the slow-client rule | done in the fix round, `debugger.contract.slow` | 2.9, 5.4 |
| — | the exception for `dbg_net.cpp` (decision 44) | not needed: the transport sends the 1009 close as built | 2.11, 5.2, 6.1 |
| — | the verification: three gaps in the tests (the core thread's client count, a server that cannot be started, the slow-client rule), no defect of the core; one gap left, the end of the expired state at a connect | the tests of 5.4 are described as they are now; the rule is in 2.9, and the missing step was added to the grace test in J1 | 2.9, 5.4, 6.1.2 |
| — | the register `ENABLED` as built: not monotonic, a computed row behind `kArm7Enabled`, no writer; the session `regs` runs the guest | text corrected; the write code is that of a register without a writer | 2.8, 5.3, 6.1.1 (item 8), 4.4 |
| — | the runner follow-up's nine choices where the design was silent | each is a rule in its row or section | 5.5 (S04, S06, S07, S11, S16, S18), 5.5.1, 5.5.2, 5.5.3, 4.2.2, 4.4 |

**J1 as built.** Package J1 (6.3) was verified in two rounds, with three read-only reviewers beside the first, and
merged. Its verifier left seven points of design feedback and four notes; each is settled in the text. The first
round's two findings — `IsRunning()` read from five words, and the code region prepared at every thread start —
were fixed at their cause in the fix round.

| # | Point | Outcome | Sections |
| --- | --- | --- | --- |
| 1 | the design still described `IsRunning()` as five words; as delivered it is one word of the machine, `SuperH4_impl::thread_live`, true from `Resume()` until the thread takes its stop callback | text corrected; 2.3.7 item 12 is done | 2.2.4, 2.2.5, 2.3.7 (item 12), 6.1 (the E4a and E0 rows), Appendix B, `DESIGN-V3-OPEN.md` (H1) |
| 2 | the code cache defect had two causes, both fixed: `LastAddr` rewound at every backend `Init`, the code region prepared once per process; the second removes the Windows bound of thread starts by reading only | text corrected; the Windows bound is an item for W64 to run | 2.3.7 (item 9), 5.7, 5.8, 6.3 (W64) |
| 3 | the row of J1 did not match what was delivered: files outside its "Owns", files in its "Must not touch", the place of the cache fix, the type | accepted by the integrator; the row is as delivered, and why the type is `AtomicU32` is stated | 6.3 (J1), 2.3.7 (item 10) |
| 4 | header revision 5 has two items and "no item yet" was no longer true | listed, with both texts quoted; the next revision is 6 | Appendix B, D6, `DESIGN-V3-OPEN.md` (section 3) |
| 5 | the sanitizer build recipe: the tree overwrites `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` | written down; the paragraph says what gives no report now | 5.7 |
| 6 | the gate `threads` of `tests/debugger/sh4bp`, the tests `debugger.selftest.hostcycles[.interp]`, 190 tests | text added | 2.3.6, 5.3, 5.4, 6 (state) |
| 7 | `resetting`, `stopping` and `StopForReset()` have no reader left: removed, or kept as bookkeeping? | the design owner's decision: removed, by the next package that owns `libswirl.cpp` (E6) | 2.2.5, 6.1.2 (item 1) |
| note | the same dead state, with its five writes | as point 7 | 6.1.2 (item 1) |
| note | three selftests of `emu` watch one word only: a run flag or `g_parked` left set behind a thread's end is seen by no test | the three tests also read both words once the lifetime is `Stopped`; left for the next emulator package | 6.1.2 (item 2), 2.3.7 (item 12) |
| note | the window sessions were not run; `emuwin.closereset` and the three window-close callers are right by reading only | confirmed when a host with a display runs them (W64 or track 10); the callers then call `Stop(cb)` unconditionally | 6.1.2 (item 3), 5.8 |
| note | one mutation passes every test: the clean-up of deleted blocks in `recSH4::Init()` taken out | no action: the change is equivalent today (the `Term()` of the object before has deleted them), and the line stays as a safeguard for an `Init()` that no `Term()` preceded | 2.3.7 (item 9) |
| — | the three items of 6.1.2 (the grace test, the selftest README, the rank of absolute symbols) | done in J1 | 6.1.2, 2.9, 5.4, 2.10 |

**M2 as built.** The M2 round (`M2-ROUND.md`) was cut on 2026-10-10 after five rounds of adversarial review, and its
six questions were answered by the project owner on 2026-10-11, every recommendation accepted (6.2 of the cut, below).
Its packages were built, verified with mutations by verifiers who did not write them, and merged: the emulator at
`devcast` `6fe01759` (259 ctest targets), the UI at `dreamcast-devtools` `d59336b`. Its gate, M2G, ran on that state
and brought this text to the cut: the 73 rows of the cut's section 7 are applied in the sections they name (rows 65 and
73 in `DESIGN-V3-DECISIONS.md`, `DESIGN-V3-OPEN.md` and the repository's copies). What the integrator's notes on the
packages (the cut's Appendix I) left for this text is below, each with where it went.

| From | Point | Outcome | Sections |
| --- | --- | --- | --- |
| F3a (I.1) | the runner as built: the probe that executes what was written saves PC, R0 and the bytes at its stop, the write to a running target comes first, its scratch address is not reused behind a resume; the tick of an accepted write is the first since the request; the SR.RB rule sets R0 apart from `R0_BANK`; both PCs carry `flags.writable` | taken in | 5.5 (S16), 2.9 |
| F3a (I.1) | the unit suite three times in a fresh clone; the mock's free-running SH4 and what a test may compare behind a resume; five mutations no test of the mock notices | taken in, as rules and as notes for a later mock package | 5.1 |
| R6 (I.2) | frozen text is ASCII | taken in | Appendix B (revision 6) |
| R6 (I.2) | a guest directory cannot register sessions; the sessions on the guest are lines of the selftest and contract CMake files | taken in | 5.6 |
| R6 (I.2) | the clears of the run flag and of `g_parked` at a thread's end are held by no test (accepted) | taken in | 6.1.2 (item 2) |
| R6 (I.2) | the gates' wording: "the M1 gate unchanged" compares the result line (18 passed, 0 failed, 0 partial, 0 not applicable, 0 invariant violations); the tick count differs from run to run; branch names are no home path | taken in | 5.7 (the gate of a package) |
| E6a (I.3) | the guest as merged (sizes, labels, the FIQ count), `a_reset` as built, the guest's start (released, held, released within two ticks), the targets the cut did not list, the runner checks the engine, `nulldc_sound_neutral`'s options, the hash of an ARM7 never released, the toolchain and its known limit, M2-Q3 as observed | taken in | 5.6 |
| E6a (I.3) | the slow pitch steps of the second channel and the `SCIPD` fold of `a_fiq`, beyond the cut's 5.2 | accepted departure, kept | 5.6 |
| E6a (I.3) | the order of `TIMA` and `SCIRE` in `a_reset`; a hold and release at a later tick: not held by a test | accepted, noted | 5.6 |
| E6b (I.4) | sixteen neutrality targets (the fourth call `hoststop`), four `arm7n.*` tests, 43 tests in the session `arm7`, 238 targets | accepted by the integrator; taken in | 5.3, 5.6, 6.3 |
| E6b (I.4) | `dbg_selftest_arm7.cpp` includes `dbg_core_private.h` (for `events::Snapshot`) | accepted; the header's sentence corrected by E7a | 2.1 |
| E6b (I.4) | an SPSR written with mode bits that are no mode is stored and ends the process at the next exception return | known limit (by the owner's answers to M2-Q4 and M2-Q5) | 2.3.7 (item 14) |
| E6b (I.4) | known limits: the exit line in the tick of a latched stop (the integrator's decision: documented, backlog); step out inside the FIQ handler; an unaligned PC from `LDM`; the linear walk of the ARM7 check and the 250 ms bound while the SH4 sleeps | known limits | 2.3.7 (items 20 to 23), 2.4 |
| E6b (I.4) | H6-19 against H6-5 (the ARM7 half of an owed resume never arms) | comment reworded by EXe | Appendix B, 2.2.1, 2.2.4 |
| E6b (I.4) | not held by a test: the mask test of the check while a temporary breakpoint arms the loop, a step whose next check stands on a listed breakpoint, four ends of the resume exemption; the completion in `dc_run` with a FIQ in the rest of its sample and the four mutants caught by the watchdog alone | the first three accepted, noted; the last two closed by EXe (items 6 and 2) | 5.3, 6.3 (EXe) |
| E6b (I.4) | speed: the tip 1.2 to 2.4 % slower by CPU time in every set measured, cause not found; M2G measures again | measured by M2G | 5.7 |
| E6b (I.4) | not run: the ARM7 fallback and its notice (no arm64 host) | taken in | 2.3.7 (item 7) |
| E7a (I.5) | 249 targets; `debugger.selftest.writes.smc` runs `smcwrites.cached` | taken in | 5.3 |
| E7a (I.5) | a fourth rule of value: an accepted write of the ARM7's PC that stores another address empties the ARM7's code cache | taken in (H6-33 and H6-38 carry it since `d3fe072d`) | 2.8, 2.3.7 (item 16) |
| E7a (I.5) | a text with a NUL in it is no path, name or expression (`-32602`); `breakpoints.add` likewise since EXe | taken in | 3.3 |
| E7a (I.5) | the test hook `core::SetTickSinkForTest`; the rewording of E6b's README rows | accepted departures | 2.1 |
| E7a (I.5) | known limits: a write of wave or video memory discards no SH4 block; `bm_LockedWrite` leaves a page marked as data; the stale tick behind a write on arm64; two FPSCR properties held by reading only | known limits | 2.3.7 (items 16 to 19) |
| E7a (I.5) | H6-33 and H6-35 "(E) in the park context only" against "with no machine" | comment reworded by EXe | Appendix B |
| E7a (I.5) | ThreadSanitizer: the part `arm7` against 2ndmix is red by wall-clock time only; on the guest it passes | taken in; M2G ran the parts on the guest only | 5.7 |
| E7a (I.5) | the leftovers (every page of a range, waits without a bound, a NUL in an event name, `__pycache__` in the tree, the optional FIQ witness) | done by EXe | 6.3 (EXe), 5.3, 5.4 |
| E7a (I.5) | the builds of the writer that are no targets (no recompiler, generic, the recompiler without the layout) and what the last does with a guest store into a page of code (M2-Q6) | taken in, and for PLAN's backlog | 2.3.7 (item 15) |
| E7b (I.6) | the M2 gate on three engines and its second run; 259 targets | taken in | 5.4, 6.3 |
| E7b (I.6) | read-only system bus registers that nothing assigns read 0 (the one change a guest can see; it removes a difference between runs) | taken in | 2.8 |
| E7b (I.6) | `.hash.listening.guest*`, `.hash.listening.rule` with the mode `hash-listening-rule`, the option `ONLY`, `TickMs=20` / `FullTickEvery=1` with one `control.runUntil`, `--emulator` / `--work` / `--guest` of `contract.py` checked behind the parser, the walk of `core.tables` as a computed rule per call, `regs.mem.read`'s made-up values, the row choices where 3.6.1 is silent | accepted departures; taken in | 5.4, 5.3, 2.8 |
| E7b (I.6) | a paused tick of about 165 KB with a full event log, against M2-D27's 130 KiB measured with an empty one; the widths of `L` and `M` as a difference between mock and emulator; `M` is the in-service flag; a run listened to from its first instruction | taken in | 2.9, 4.4, 2.8, 5.4 |
| E7b (I.6) | what the state hash does not see | known limit | 2.3.7 (item 24) |
| E7b (I.6) | eight computed rows of `dc.aica.common` without `kRegComputed`; `hash.listening` needs ten full ticks; no call of `core.tables` lands on exactly 256 watches; the README count of `regs.rows.sb` (fixed by EXe) | accepted notes | 2.8, 5.4, 5.3 |
| E7b (I.6) | "one hour of a running target" (the cut's verifier paragraph) is emulated time | not taken: a verifier's wording, no text of this design has it | — |
| E7b (I.6) | not run: `hash.listening*` on arm64; an arcade board | taken in | 2.3.7 (item 7) |
| F3b (I.7) | a code breakpoint of an unlisted target listed with the events, its address as plain hex; `targets` and `capabilities` read for any present `protocol` | accepted decisions; taken in | 4.4 |
| F3b (I.7) | one test's set-up of `src/state/debuggerDataStore.test.ts` edited outside "Owns"; six more e2e tests than asked | accepted departures | 6.2 (F3b) |
| F3b (I.7) | the toolbar's Pause named `sh4` whatever the server lists | decided by F3d: Pause names the focused target only when the server lists it | 4.4 |
| F3b, F3c, F3d (I.7 to I.9) | the leftovers of the UI packages | done by EXu | 4.4, 6.2 (EXu) |
| F3c (I.8) | decisions 1 to 6: a keyboard path to every editor, where the focus goes, a view that moves is leaving the box, `overflow: clip`, the controls while the target runs, an editor goes when the target runs | accepted decisions; taken in | 4.4 |
| F3c, F3b (I.7, I.8) | the DOM as built and the synthetic server's shape (for F3c and F3d), the main UI checkout's `dist-native` | not taken: instructions to a package, not design | — |
| F3d (I.9) | widgets of a code editor and dialogs; the run state below 1440 px and 1200 px; Pause; a click on a tab counts as focus | accepted decisions; taken in | 4.4 |
| F3d (I.9) | how the smoke spec is run (the order, `smoke.sh`, its own `HOME` and XDG directories, the emulator ended by pid) | taken in, and followed by M2G | 4.4, 5.1 |
| F3d (I.9) | for N1: the isolation and the names of `contract.py`, `smoke.sh` and the README's recipe | taken in | 5.4 |
| EXe (I.10) | all eight items; item 6 is a witness, not a known limit; the three sentences in `debugger.h`, `dbg_regs.h`, `dbg_mem.h` | taken in | 5.3, 6.3, Appendix B |
| EXe (I.10) | the window of `core.lifetime` / `core.writes` on a failure path; a test thread that holds the machine past the bound; an empty "asked the machine for" | accepted, noted | 5.3 |
| EXe (I.10) | H6-32's "Values" paragraph and the file comment of `dbg_mem.h` as the next candidates of a comment revision | taken in | Appendix B |
| EXu (I.11) | item 1 as decided by the package (the last row of an address space is the panel's last whole row) | accepted decision; taken in | 4.4 |
| EXu (I.11) | `e2e/disassembly.spec.ts` changed outside "Owns"; the integrator's grant of two assertions of `runControls.test.ts`; the integrator's fix of a flaky memory test (`d59336b`) | accepted departures | 6.2 (EXu) |
| EXu (I.11) | whether About closes synchronously (a key pressed right after a click on its Close button) | accepted, not changed: outside the package | — |
| the cut, 1.2 | blocks of the SH4 recompiler keyed by the FP mode (T3.4); the block cache without an address in its key; undefined instructions and odd PCs as guest exceptions; reios and the AICA's interrupt registers (M2-Q3); the GD-ROM drive and the system bus slots across a machine reset; the silent boot ROM workload | recorded for PLAN's backlog and tracks 5, 6, 7 | 2.3.7 (items 2, 6, 14, 15), 5.6 |

**The project owner's answers to the M2 questions** (2026-10-11, the cut's 6.2), and what each left for the cut of M3:

| Question | Answer | Left for M3 and later |
| --- | --- | --- |
| M2-Q1 M2 writes only the registers of the two processors; the device tables are read-only | yes | the write semantics of every device register, stated per register (the guest's write, a plain store, or refused): the AICA with M3 (E8, with the body of `AICA::DebugWriteReg` and whether the debugger may hold the ARM7), system bus, PVR and SH4 on-chip modules with M4 (E9) |
| M2-Q2 a paused tick of about 130 KiB is acceptable | yes, with the budget and the fallback of M2-D27 | measured at about 165 KB with a full event log (2.9); an on-demand way to read one node belongs to the protocol work of PLAN track 10 |
| M2-Q3 should reios leave the AICA's interrupt registers as the retail boot ROM does | not in this round | PLAN tracks 5 and 7, with a console check; a fix makes 2ndmix a sounding workload and moves its lines, so it lands with a new baseline |
| M2-Q4 a debugger write of FPSCR, and one that moves the SH4's PC, may empty the code cache | yes | blocks keyed by the FP mode (PLAN T3.4) and a block cache with the address in its key (backlog) would make the two cache resets unnecessary |
| M2-Q5 a debugger write may end the emulator | yes, stated in the help; only an odd PC is refused | undefined instructions and odd PCs as guest exceptions (PLAN's backlog) |
| M2-Q6 the recompiler without the virtual memory layout has to run guests | not in this round | E7a's verifier made the build once (2.3.7 item 15): PLAN's backlog has the fault resolved from the address of main memory |

For the package that wires the T5.0 core (PLAN track 5): it removes `aica.NoBatch` and the batch path, and with them the
`.batch` half of the `debugger.sound.*` targets (the runner's key, and the `batch` half of what `nulldc_sound_neutral`
registers) and the selftest line `arm7.persample`, whose setting is then the only path (the line `arm7` stays) (5.6).
(Done by W2 of the M3 round.)

**M3 as built.** The M3 round (`M3-ROUND.md`) was cut on 2026-10-16 after two rounds of adversarial review; the project
owner answered its four questions the same day (M3-Q1 "Fix it in M3", M3-Q2 "don't mind perf for now", M3-Q3 "Move to
M4", M3-Q4 "Not in M3"), round 3 applied the answers (a package W3 among them) and was reviewed in its turn. Its twelve
packages were built, verified with mutations by verifiers who did not write them, and merged: the emulator at `devcast`
`9fbc988a` (307 ctest targets; the submodule `devtools/` at the UI's head), the UI at `dreamcast-devtools` `066fcbc`. Its
gate, M3G, ran on that state and brought this text to the cut: the 43 rows of the cut's section 8 are applied in the
sections they name (row 39 in `DESIGN-V3-DECISIONS.md`, rows 40 and 41 in `DESIGN-V3-OPEN.md` and `PLAN.md`, and the
repository's copies). What the integrator's notes on the packages (the cut's Appendix I) and the verifiers left for this
text is below, each with where it went.

| From | Point | Outcome | Sections |
| --- | --- | --- | --- |
| F4a (I.1) | a timing bound in a test is measured on a monotonic clock that fake timers do not replace (the wall clock of the development machine steps 3 s about every 35 s) | taken in, as a rule | 6.2 (F4a) |
| F4a (I.1) | the held-ARM7 rules of the mock, S17 and S21 first meet the emulator in E8a's and E8b's gates; the mock's `TESTB0` option | taken in | 4.5, 5.5 |
| R7 (I.2) | the fix round rewrote the comments of the rows of 0x2804 in `dbg_regs_tables.cpp`, which R7's Owns did not name | accepted departure | 2.8 |
| R7 (I.2) | the design's `dc.aica.common` rows read "as `AICA::DebugReadRegs` gives them", wrong from W1 on for `RBP`, `RBL`, `TESTB0` | taken in (section 8 row 20) | 2.8 |
| W1 (I.3) | `dsp_mem3.emu` misses five lines, not one; the channel keeps a bus number (save state V5) | taken in | 2.3.7 (item 26), 2.5.4 |
| W1 (I.3) | two mutations survive (the `dsp` fold without the blob's last byte; the CD-DA pair given to EXTS under the dead `CDDAMute`); the macOS Xcode project lists none of the files added since the debugger came in | accepted; for PLAN's backlog | — |
| W1, W2, W3 (I.3, I.5, I.6) | speed and host instructions, reported by each | measured again by M3G | 5.7 |
| W2 (I.5) | the fix round edited W1's `.emu` machine (`EmuNoSound`, `EmuHarnessOn`, `EmuHarnessSamples`) and added `aica.dsp.emu.state` check 5 (`nosound`): the stand-in stream is the one place the stream half of "Disable Sound" mutes can be observed | accepted departure | 5.6 |
| W2 (I.5) | `profiler.cpp`'s `samples_gen` inside `HAS_PROFILE`; the unused `bool` of `ChannelEx::Step` | for PLAN's backlog | — |
| W3 (I.6) | B2's held bursts (the guest 0; 2ndmix 18 and 362; hello 6461 and 6547 of 6917); the guest identical; 2ndmix's `samples`, `dsp` and `regs` (the timers and pending bits) moved from the first held burst on, shown by dumps; the `muted` texts, `perf.py`'s example, three more stand-in kinds; gate (d) read per build | accepted; taken in | 5.6 |
| W3 (I.6) | hello is held to its state hash only, which has no AICA part, so it says nothing of the clock | known limit | 2.3.7 (item 24), 5.6 |
| G (I.8) | 18 steps and a TABLE 1 write with its read-back where the cut said one access; the oracle holds the send's half of the program at 0 only (covered by `aica.dsp.diff.*` and `aica.dsp.emu.lockstep`) | accepted departures; taken in | 5.5, 5.6 |
| G (I.8) | the leak of `reios_loadElf`, the oracle tool's ELF reads without bounds checks, no target checking the rules of the cut's 6.3 on the program's text | for PLAN's backlog | — |
| E8a (I.9) | `arm7.held.clocked` writes `MCIEB` / `MCIRE` through `AICA::WriteReg` (no debugger writer of the AICA's registers in M3); `dsp.sameslice.*` cover the DSP phase; the run-on rule as one function (`RunSuspendedBurst`); `Settle` through the boot; the session on the tree's guest; the neutrality targets named `debugger.sound.neutral.dsp.<x>.<engine>` | accepted departures; taken in | 5.3, 5.6 |
| E8a (I.9) | equivalent mutants: `PrepareResume`'s `dbg_dsp_armed == 0` guard (`Rearm`'s clear is the contract), and `NoteArm7Fallback` behind any suspension on this x86-64 host; `LATCH` and `LATCH_COPY` equal at every stop the session compares | known limits | 2.3.7 (item 7), 2.8, 5.3 |
| E8a (the brief of M3G) | the register counts (2,949 in the tree, 13 on `dc.aica.dsp`) and the DSP's run control (the burst in phases, DSP breakpoints and steps, the session `dsp` of 31 tests, `debugger.sound.neutral.dsp.*`, the contract mode `dsp`) | taken in | 2.5.2, 2.5.4, 2.8, 5.3, 5.4, 5.6 |
| E8b (I.10) | the core's notice installed in `core::Start` / `StartForTest` and removed in `core::Stop`; `sgc::Arm` refuses a frame count outside 1 to 4096; with no capture the sweep tests the flag and stores one value a sample | accepted departures | 3.3 |
| E8b (I.10, the brief of M3G) | the capture's deadline taken behind the arming call; `std::bad_alloc` on the send path of a 32 MiB frame; two possible flakes; a failure in the session `sgc` cascading; three paths not tested; `contract.py` not asserting 20 and 3 | known limits (2.3.7, 5.3, 5.4) and PLAN's backlog | 2.3.7 (items 29, 30), 5.3, 5.4 |
| F4b (I.4) | "Start DSP" sets STEP and SAMPLES to 0 and does not reset the core; inputs written when a sample starts; "not on the starting step" per run; MADRS unsigned and a MIXS_BANK row in the register view; the e2e test needs a Chromium whose `decodeAudioData` does not clip | accepted; taken in | 4.5, 6.2 (F4b) |
| F4c (I.7) | ten spec files lost only their `KNOWN.sgcNoData` allowances; `SgcWaveformRenderer.test.ts`; Record asks for `{frames: 1024}`; a "Capture length" label; cursors one pixel to either side; `SgcFrameData.getBuffer()` removed | accepted departures | 6.2 (F4c) |
| F4d (I.11) | the Load Regs source generator shared from `emulatorSnapshot.ts`; F4c's leftover test in `e2e/dsp-snapshot.spec.ts`; the help's sentence also in the README; the button shown off with its reason on the tool page; the smoke spec's two held DSP steps at two entry stops; its timeout 420 s; the equivalent mutant `banner-kept` | accepted departures | 4.5, 6.2 (F4d) |
| F4d (I.11, the brief of M3G) | the SGC panel's default view flat against the emulator; the playground without a sound generator; what the smoke spec's held steps show; the UI's backlog (an unhandled rejection, audio of a closed panel, Start DSP's clear of the banner untested, the smoke spec's capture by text) | known limits; for PLAN's backlog | 4.5 |
| the cut, 1.2 | the SGC's sends and fetches in time (`cycle-similar`); the fast tier's time step where the console differs (`MCIPD` bit 10, the prescaler); the channel arithmetic; bytes 76..83; the step view on the wire; a DSP callstack; the playground's sound generator; a BIOS workload; changes for speed; the device writes | recorded for PLAN's backlog and tracks 5, 10; the device writes in M4 | 1.1, 2.3.7 (items 26, 27), 3.4, PLAN |
| M3G | the gate's results: passed, on `devcast` `9fbc988a` (its documents behind it, on the branch `skmp/nulldc3-m3g`) with the UI at `066fcbc` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+066fcbc`): run 1 of the M3 gate passes against the emulator on the JIT, the interpreter and the forced fallback (20 scenarios passed, 0 partial, 0 not applicable each), run 2 against the sounding guest on the JIT and the interpreter (S00, S17, S21: 3 passed with the note `sgc: captured with the arm7 held`; `--only S00,S21`: 2 passed with the note `dsp: stepped with the arm7 held`), and the M2 gate's second run on the changed guest (2 passed, both notes); the 307 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix and the guest's exit state hash equal B4's (12 of 12 entries, the default build and `-DNULLDC_DEBUGGER=OFF`), and so do the state hashes of 5.7 (38 of 38 rows, 2ndmix 600 in the five builds); `npm run check` (735 unit tests passed, 1 skipped; the four contract runs on the mock OK) and the e2e suite (401 passed, 1 skipped: the smoke spec, run on its own against the emulator) pass in a fresh clone of the UI; the smoke spec passes against a headless emulator on the guest; ThreadSanitizer of the sessions `dsp` and `sgc` and of the contract runs on the guest (interpreter) gives no report; speed is measured and reported (5.7) | taken in | 1, 5.6, 5.7, 6.3 |

**The project owner's answers to the M3 questions** (2026-10-16, the cut's 7.2), and what each left for M4 and later:

| Question | Answer | Left for M4 and later |
| --- | --- | --- |
| M3-Q1 may a stretch with the ARM7 held stay without samples until track 5 clocks the AICA apart from the ARM7 | "Fix it in M3": the AICA is clocked whatever ARMRST says; only the ARM7 stays unclocked (W3) | what the console does at a release of the ARM7 (its phase against the sample edge, a request latched while held): unmeasured, 2.3.7 item 27 |
| M3-Q2 which speed is the floor for the always-on DSP | "don't mind perf for now": no floor, ceiling or threshold in M3; measured and reported (5.7) | a later round that takes speed up starts from M3G's numbers (5.7), by the same method |
| M3-Q3 may the AICA's register writes move to M4, into one write table for every device | "Move to M4" | E9: the write semantics of every device register, the AICA's among them, and the body of `AICA::DebugWriteReg` (1.1, 6.3) |
| M3-Q4 should a boot of the console's BIOS be among the state-hash workloads | "Not in M3" | a package of its own once the owner names a data path for a BIOS (5.7, PLAN) |

**The direction of 2026-10-08** (`PLAN.md`) changed: D7 to D10; 1 and 1.1 (track 10, caches, wasm, backends); the build
paragraph of 2; 2.2.5 (who calls a host stop); 2.3.5 (two JITs); 2.5 (the DSP is always on); 2.7 and 3.3 (cache tables,
no page protection in `cycle-similar`, the wgpu renderer's log); 2.8 (`MODE` is the tier); 3.0 (extensions).

**What was built** changed, with no decision involved: the whole of 2.2 (calls, the slice end, the park context,
`IsRunning()`), 2.3.4 to 2.3.6, 2.5.4 (the interface of the DSP core), 2.6 (where events are raised), 2.7 (the memory
map as `dbg_mem.h` has it), 2.8 (the register lists), 2.9 (order, no-op requests, extra ticks), 2.10, 2.11
(`bundle-info.json`), 3.0 (params, error precedence — built on both servers but for one case: the mock answers the
`-32004` of a capability it was configured without behind `-32602` and the lifetime errors, which 4.2.1 corrected),
4.1 to 4.3, 5.2, 5.3, 5.6, 5.7, 6, Appendix B.

**Verifier feedback that was not adopted**, with the reason (everything else in the verifiers' lists is settled by a
decision above or by the implementation, or is not a design matter):

| Feedback | From | Why not |
| --- | --- | --- |
| keep repeating `breakpointId` after its breakpoint was removed, and require the listing only in the first tick of a stop | protocol verifier | decision 17: the id leaves the ticks; a `breakpointId` is always a key of `breakpoints` |
| the texts of `error.message` as part of the contract | runner verifier | decision 4 |
| reset the reconnect sequence only after `debugger.describe` was answered | panels verifier | decision 18: any connection that opens resets it |
| send every path with `"n/a"` while no game is loaded, in place of `registers: {}` | protocol and panels verifiers | decision 15: the client drops its values |
| a `stopId` change of its own for a game that starts running from `no-game` | panels verifier | decision 15: the client reads again when the reason leaves `no-game` |
| the server keeps one code breakpoint per (event, address) | panels verifier | 2.3.3 stands: every add makes a breakpoint, and the gate asserts it; the client guards against a double click (4.3.1) |
| relax the timestamp check for a host without a set clock | runner verifier | decision 2; the targets are desktop hosts (D7) |
| contract scenarios for `host-stopped`, `guest-exit`, `no-game` | protocol and panels verifiers | a client cannot bring those states about and a headless run ends at a guest exit; pinned by the selftest `core.lifetime` and the UI's e2e suite (5.5.4) |
| the 10 s grace rule as a contract scenario | runner verifier | it is a test of its own, `debugger.contract.grace` (5.4): it takes 10 s and releases the stop the scenarios stand on |
| a contract probe of the 16-client limit | protocol and runner verifiers | the limit is the transport's; `tests/debugger/net` has it |
| a probe that a 256-character watch expression is accepted; a bound on `state.unwatch.watchIds` | runner verifier | no resolvable expression is that long; the message bound covers the array |
| the e2e suite once under CPU throttling as a gate | protocol verifier | the requirement is stated instead (4.3: a tick re-renders only what it changed) and has its own performance test |
| `Sh4Context::CpuRunning` as `std::atomic<u32>`, now | run-control verifier | decision 23: no declaration changes in header revision 4; it is a known limit until M1 (2.3.7 item 10) and is done directly behind it, in package J1 (6.3). Done: an `AtomicU32`, header revision 5 |
| an accessor `sh4_tmu_DebugReadTCNT` in `sh4_dbg.h` | header review | header revision 2 chose the reader in `dbg_regs.cpp`; no package owns `hw/sh4/modules` |
| the GNU printf archetype for MinGW in `DBG_PRINTF_LIKE` | header review | no MinGW build is a target |
| let an automated run end from a debugger park | run-control verifier | emulated time stands still while parked; the behaviour is documented and pinned (2.2.9) |
| the interpreter loop tests the run flag behind SLEEP, so that only the JIT re-executes it | run-control verifier | the re-execution is harmless and is documented for both loops (2.2.3, 2.4) |
| `0x00802C00` / `0x00802C01` not readable for the ARM7 target | integration note of header revision 2 | superseded by what was built: the two bytes are read through the register read, a plain copy there (2.7) |
| a rule for the event-log row format and for the toolbar's layout | panels verifier | presentation is the UI's; 4.3 records what was built |
| a statement on Chromium's local-network check for a page from another origin | panels verifier | browser policy, not protocol; the client's error text lists it among the causes |
| the leftovers package B64 listed (unreachable switches of removed targets, a CI recipe of one, helpers without callers, sources no build lists) | B64 verifier | not this design: PLAN package P-desktop, which is done and removed them (6.3) |
| a file of pinned determinism hashes that a test reads, so that "the hashes equal the baseline" is a gate | verifier of the corrections of 6.1.1 | the suite compares runs with each other on purpose: a deliberate fix of behaviour then needs no baseline edit (5.7: no hash is stored in the tree) |
| the tests project generates `libswirl/version.h` in the build directory (as built it writes the ignored file into the source tree, so a worktree that must not change cannot be built from) | verifier of the corrections of 6.1.1 | not a design matter; noted for the build work. **Done by VTe of the platform round**: no build writes into the source tree any more; the version is `<build>/generated/nulldc3_version.h`, made at build time and read by `libswirl/version.cpp` alone (`PLATFORM-ROUND.md` 5.2, PL-D75), and `libswirl/version.h` and its template are gone |

## Appendix B. APIs of the started packages, and the header revisions (frozen)

E1, `libswirl/debugger/dbg_net.h` (names, defaults and guarantees below are the contract; the header and `dbg_net.cpp`
use no tree header, so they also build outside the emulator):

```cpp
namespace dbg { namespace net {
struct Handler {                      // every method is called on the network thread, never concurrently
    virtual void OnOpen (uint32_t client) = 0;
    virtual void OnText (uint32_t client, const std::string& text) = 0;
    virtual void OnClose(uint32_t client) = 0;        // exactly one per OnOpen, also when the server stops
    virtual void OnBinary(uint32_t client, const std::string& bytes);   // default: the message is dropped
};
struct Options {
    std::string bind = "127.0.0.1";   // numeric address or host name; the first resolved address is used
    int port         = 55543;         // 0 = assigned by the OS
    std::string webRoot;
    std::vector<std::string> allowedHosts;
    size_t maxMessage = 1u << 20;     // largest inbound WebSocket message
    int maxClients    = 16;           // further upgrades get 503
};
class Server {
    bool   Start(const Options&, Handler*);             // false: nothing bound; one line on stderr says why
    void   Stop();
    int    Port() const;                                // the bound port, 0 while stopped
    bool   HasClients() const;                          // lock-free
    void   SendText  (uint32_t client, std::string text);
    void   SendBinary(uint32_t client, std::string bytes);
    void   BroadcastText(const std::string& text);
    size_t QueuedBytes(uint32_t client) const;          // bytes not yet handed to the socket
};
struct StaticFile { std::string body; bool gzip = false; };
typedef bool (*StaticProvider)(const std::string& path, bool acceptGzip, StaticFile* file);
void SetStaticProvider(StaticProvider provider);        // dbg_webui.cpp installs the embedded bundle
}}  // sends are thread-safe and never block the caller on a slow or dead socket
```

Client ids start at 1 and are not reused while the `Server` lives; text and binary messages are counted apart, and a
client with more than 16 MiB of text, or more than 64 MiB of binary messages, queued is disconnected (since E8b of the M3
round; before it one bound of 16 MiB);
`Start` and `Stop` are for one controlling thread, everything else may be called from any thread. `dbg_webui.h` has
`dbg::kProtocolVersion` (the one source of the protocol number), `webui::Embedded()`, `Version()`, `GitRev()`, `Files()`,
`Find()`, `Inflate()`, `BuiltinPage()`. HTTP and WebSocket behaviour: 2.11.

E2, `libswirl/debugger/dbg_disasm.h/.cpp` — pure functions, callable from any thread, no emulator dependency:

```cpp
namespace dbg {
struct DisasmCtx { SymbolFn symbol; ReadFn read; void* user; };     // both callbacks optional; a null ctx is both null
enum Sh4FpMode { SH4_FP_PR = 1, SH4_FP_SZ = 2 };
std::string DisasmSh4 (uint32_t pc, uint16_t op, unsigned fp_mode, const DisasmCtx*, Sh4InsnInfo*);
std::string DisasmArm7(uint32_t pc, uint32_t op, const DisasmCtx*, ArmInsnInfo*);
std::string DisasmDsp (unsigned step, const uint16_t mpro[4]);
}
```

`Sh4InsnInfo` / `ArmInsnInfo` carry `valid`, `is_branch`, `is_delayed_branch` (SH4), `is_call`, `is_return`,
`is_conditional`, `is_trap`, the branch target and the PC-relative literal. `Sh4InsnInfo::valid` is true for exactly the
encodings the interpreter's opcode table executes, except `0x085B` (the reios trap). Text conventions: lower case; SH4
and ARM follow GNU objdump (`mov.l   @(8,r1),r2`, comments start with `!`; `ldrbeq  r0, [r1, #4]!`, comments start with
`@`); addresses are `0x%08x`; an encoding that is not an instruction disassembles as `.word 0x<raw>`; a DSP step is one
line of the playground assembler (`MPRO[12] = TRA:3 TWT TWA:5 YSEL:1 SHIFT:2`).

E0, the shared headers: `debugger.h`, `dbg_emu.h`, `dbg_sh4.h`, `dbg_core.h`, `dbg_regs.h`, `dbg_mem.h`, `dbg_events.h`,
`dbg_symbols.h`, `dbg_callstack.h`, `dbg_selftest.h`, `hw/sh4/sh4_dbg.h`, the debugger support block of `hw/sh4/sh4_if.h`
and the debug accessors of the `hw/` headers; and, from revision 6, `dbg_arm7.h`; from revision 7, `dbg_dsp.h` and
`dbg_sgc.h`. The private headers (`dbg_*_private.h`) are not frozen (2.1). Their declarations are quoted in 2.3.4 l and
6.1, and those of revision 7 in `M3-ROUND.md` section 2; their comments are the contract. Revisions since the freeze:

| Revision | With | Content |
| --- | --- | --- |
| 2 | the integration of E3 and E4b | comment-only: where three events are raised (`dbg_events.h`); the flash store against what a guest reads (`sh4_mem_area0.h`); no remainder for a check that stopped a step, and an exception clears the exemption (`debugger.h`); the exemption needs a check behind it, an unhandled slot word refuses a step (`dbg_sh4.h`); which slice counter the fallback charges and `SliceCyclesLeft()` reports (`sh4_dbg.h`, `sh4_if.h`); the first `Park()` after an exit taken in a debugger stop is a resume (`debugger.h`, `dbg_sh4.h`); the SH4 on-chip registers behind a read function (`dbg_regs.h`); the ARM7 register indices 21..25 (`SoundCPU.h`). Declarations: `area0_DebugNvmemByte`, `SoundCPU::DebugGetInterrupt`, `dbg_sh4_SlotRuns`, and `NoWord::operator u32` / `NoFlag::exchange` of the stand-ins |
| 3 | E4a | `ATTN_LATCHED`; the slice end tests `g_attn` first and looks at the latch behind it; the SLEEP handler tests the latch in a slice that reported something and accepted nothing; `g_attn` may be stored 0 at the thread's end; `IsRunning()` counts a cpu stopped for a reset; the cpu is started through the machine's own start function; a parked thread is released by the stop request behind `dbg::Term`; the start park of a selftest session; a call that is given up has its closure destroyed by the caller (`debugger.h`, `dbg_emu.h`, `dbg_selftest.h`) |
| 4 | the corrections of 6.1.1, before E5 (branch `skmp/nulldc3-integ-v3`, merged) | comment-only, applied: the 22 items H1 to H22 in seven headers (`debugger.h`, `dbg_emu.h`, `dbg_events.h`, `dbg_mem.h`, `dbg_core.h`, `dbg_callstack.h`, `hw/sh4/sh4_if.h`); no declaration changed. The list, with the text before and after, is kept as the record in `DESIGN-V3-OPEN.md`. The one comment line it left at 180 columns (`debugger.h`, the `Init()` comment) is re-wrapped, line breaks only |
| 5 | J1 (branch `skmp/nulldc3-j1`, merged) | applied: one declaration and one comment. `hw/sh4/sh4_if.h`: `Sh4Context::CpuRunning` is declared as an `AtomicU32` (it was `volatile u32`), with a comment that says who writes and reads the word and in which order; nothing else of the file changed, and the word keeps its offset and its size (pinned by a `static_assert`). `debugger.h`: the comment of `g_parked` no longer gives the terms of `IsRunning()`, which is one word of the machine; this supersedes H1 of revision 4. Both texts are quoted below |
| 6 | R6, the cut of the M2 round (merged as `af1f1d8c`; `f6949750` made two of its comments ASCII; EXe reworded three sentences, comment text only) | six declarations — the ARM7 block of `debugger.h`, `SetArm7Breakpoints`, `OnSoundStop`, `TargetSteps` (`dbg_emu.h`), `dbg_arm7.h` (new, frozen from this revision), `SoundCPU::DebugIsRecompiler` — and the comments H6-10 to H6-42; texts in `M2-ROUND.md` section 2. No existing signature changed and no enumerator was added. A frozen comment carries no character outside ASCII. The three sentences EXe reworded (H6-19 in `debugger.h`, H6-33 in `dbg_regs.h`, H6-35 in `dbg_mem.h`) say that the ARM7 has no part in an owed resume, and the thread rule of `Write()` as `Read()` has it ("(E) in the park context, or a thread that holds L while no emulator thread exists"); the cut's section 2 has the same text. Next candidates of the same kind: H6-32's "Values" paragraph and the file comment of `dbg_mem.h` on `Write()`; the step out of the ARM7 that knows the mode it was asked in (2.3.7 item 21) |
| 7 | R7, the cut of the M3 round (merged as `b921699b`; the clauses of 2.6 of the cut went with W2, W3 and E8b, each in the commit that ended the state they described) | the DSP block and the SGC block of `debugger.h`, `SetDspBreakpoints` (`dbg_emu.h`), `dbg_dsp.h` and `dbg_sgc.h` (new, frozen from this revision), `DspDebugState` with the DSP's datapath (`hw/aica/dsp.h`), the channels' outputs of `AicaChannelDebug` (`hw/aica/aica_mmio.h`); removed: `NOTICE_DSP_FORCED` and the row `EV_DEBUGGER_DSP_FORCED`; the comments M3 changes (the burst in its phases, the DSP's checks and the end of a sample, the capture, the two bounds of `dbg_net.h`). Texts in `M3-ROUND.md` section 2. No existing signature changed; no enumerator was added to `ErrorCode`, `StopCause`, `Target`, `Category`, `CallClass`, the bounds or the event table. Frozen text is ASCII |
| 8 | the platform round (`PLATFORM-ROUND.md` 2.3): RB (H8-1 to H8-3, in the commit that renamed the entry points, `7b38efcf`), VTe (H8-4, in the commit that made the paused title carry the version, `11e407cc`) and A64 (H8-5, in the commit that stated `CtxAtSlice` on arm64, `e363a5d8`) | comment-only, `debugger.h` and `hw/sh4/sh4_if.h`, no declaration changed: the comment of `Init()` says "at the end of nulldc_init()" (H8-1); the comment of `Term()` says "at the start of nulldc_term()" (H8-2) and "(nulldc_term)" (H8-3) where they said `reicast_init()` / `reicast_term()`; the bullet "Parks while a debugger stop is in force" of the park loop says the window title becomes "nullDC <version> [paused by debugger]", `<version>` being the build's version string (`NULLDC_VERSION`), where it said "nullDC 3.0.0 [paused by debugger]" (H8-4); in `sh4_if.h` the comment of `SH4DBG_CTX_AT_SLICE` says "The interpreter and the x64 and arm64 JITs do." where it said "The interpreter and the x64 JIT do; the arm64 JIT does not." (H8-5). The texts of H8-1 to H8-4 are fixed in the cut's 2.3, H8-5's in A64's brief (item 16). Frozen text is ASCII |

Header revision 5, as the tree has it. In `libswirl/hw/sh4/sh4_if.h`, inside `Sh4Context`, in place of
`volatile u32 CpuRunning;`:

```cpp
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
```

In `libswirl/debugger/debugger.h`, the first sentence pair of the comment of `g_parked` (the rest of the comment is
as it was):

```cpp
// (E) is in a debugger stop: latched, parked or executing a step. SuperH4_impl::IsRunning() reads neither it
// nor CpuRunning: it is one word of the machine, set from the start of an emulator thread until that thread
// has taken its stop callback, so that a parked machine and one on its way to a stop count as running for the
// host UI.
```

No revision is pending. The next one comes with the cut of M4 (a declaration a compiler refused in a frozen header
would have been revision 9 in the platform round, `PLATFORM-ROUND.md` 2.2: none was).

## Appendix C. The SH4 mechanism: prototype and results

Prototype: branch `skmp/nulldc3-proto-bp` (commits `947f0eca`, `12e46c3e`, `540d4767`, `8e693134`, `d49b8f58` on
`34a62ea8`; not merged: package E4b is its port), builds, logs and the run script under `agents/nulldc3/proto-bp/`. In
it the debugger is replaced by `hw/sh4/sh4_debug.{h,cpp}` (names `sh4dbg_*` for this document's `dbg_sh4_*`), driven by
environment variables, and the park loop by a stand-in called from `SuperH4_impl::Run()`.

The specification (`docs/nulldc3/sh4-breakpoints-spec.md` in the emulator repository; the review-time copy is
`agents/nulldc3/review/jit-breakpoints-spec.md`) holds what is not repeated here: the code walk with line references,
the prototype's test knobs (its Appendix A) and the full measurement tables (its Appendix B). Its sections 3.a to 3.j
are 2.3.4 and 2.3.5 of this document; its tests T1 to T12 are gates B1 to B12.

Conditions: 2ndmix.elf under reios, `none` renderer (D3), audio backend `none`, fixed RTC; x86-64 Linux host under heavy
unrelated load, so wall time was not usable and host instruction counts are the cost figure. *State* as in 2.3.6.

| Proof point | Result |
| --- | --- |
| stop at a block start and in the middle of a block | PC exact, integer registers identical to the interpreter's first arrival (three addresses) |
| resume and next arrival | a 5-opcode loop with the breakpoint at its block start, mid-block and on the slot of its `bf.s` (reported at the branch): 5209 hits in the JIT and in the interpreter; time at the loop exit identical with and without the 5209 stops |
| timing neutrality | state at vblank 1500 equal to the baseline for 15.7 M tests answering continue and for 5 171 548 stop + resume cycles (mid-block, and slot); also for an idle-class block, the SLEEP block, two breakpoints in one block, `idleskip` off, blocks ended by the size limit, 227 351 pause + resume cycles, a disarm in mid-run and a step-out left pending. Skew per stop: 0 cycles. The interpreter's debug loop equals its normal loop |
| single step | 20000 steps from `0x8C010000`: PC and integer registers identical to the interpreter after every step; a step over the reios trap lands at `0x8C010000`; a step on SLEEP ends on the SLEEP after 1002 slices, or at the handler |
| cost with no breakpoint | host instructions of (E) up to vblank 4000: query compiled out 46 685.36 to 46 685.48 M; query present with nothing armed 46 685.35 to 46 685.51 M (+0.5 ppm, inside the 2.5 ppm run-to-run spread); one armed address never reached +14 ppm. About 500 vblanks/s in all three |
| cost of a test | answering continue: about 35 host instructions; stop + resume: about 33 000 host instructions (14.5 µs); pause + resume at a slice end: about 800 |

Also run: arming an address in an already compiled, hot block hits; step over and step out stop at the same PC in the
JIT and in the interpreter.

What the prototype did not cover has since been built and gated — the park protocol of 2.2 around the stop (E4a), the
step preconditions and the fallback (E4b) — or has left the tree (the arm32, x86 and cpp backends, B64). Still not run:
MMU builds (2.3.7 item 7); the arm64 backend's native items and the Windows ABI path of the emitted code have run since
the platform round (A64, W64b). The prototype's
pause left a SLEEP wait intact; rule M-8 of Appendix A.2 keeps that property, and the merged code is measured to keep
it (2.4).

Host determinism: in the prototype's runs one run in about 150 took a different guest path with identical inputs (no
breakpoints, query compiled out: 3128 blocks compiled instead of 2687). The causes were found later (PLAN T2.0): the
real-time clock taken from host time, and window input. With the RTC pinned and headless runs every run of a
configuration gives one state hash, so the repeat rule that v2 derived from this (M-14) is gone for harness lines and
state hashes (5.6); 2.3.6 keeps a repeat for another reason.
