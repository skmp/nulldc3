# nullDC 3.0.0 — plan

devcast (`dreamster/devcast`) becomes **nullDC 3.0.0**: the development emulator that the web devtools attach to, with a test
suite and three timing tiers per processor, backed by the console measurements in shrike4 / caique / wren7.

This file is the work list. Each track has milestones, a gate, and its status. Evidence for every statement is in the reader
reports under `agents/nulldc3/understand/` (one per subsystem) — they are the maps; read the relevant one before touching a
track.

## Ground rules

- Repos: emulator `dreamster/devcast`, UI `dreamster/dreamcast-devtools`; work on local branch `skmp/nulldc3` in each;
  packages commit on local branches and push nothing; after every milestone the integrator pushes `skmp/nulldc3` of both
  repositories and opens review PRs into `main`, the UI's first (the owner, 2026-10-16); nothing is merged by the
  integrator. The platform round ends the same way (`PLATFORM-ROUND.md` section 7): the UI's PR first, the emulator's
  only after the owner has merged the UI's, with the emulator's submodule `devtools/` moved to the UI's `main` commit.
  A second, older checkout of `devcast` outside `dreamster` is stale — do not use it.
- New surfaces say "nullDC" and the build's version. **The executable is `nulldc3` and portable by default** (user,
  2026-10-12; package `N1` below, done). **The tree was renamed by the platform round** (the owner, 2026-10-19;
  `PLATFORM-ROUND.md` 1.3 item 1, package RB): the platform shell `reicast/` is `nulldc3/`, every live name that said
  reicast has a nulldc3 name, and "reicast" stays only as history (licences, credits, the documents that record
  history, git history), held by the test `rebrand.names`. Company and copyright holder: "nullDC Team"; macOS bundle
  identifier `org.emudev.nulldc3` (1.3 item 2).
- **Versions** (`PLATFORM-ROUND.md` section 5, packages VCu and VTe): both repositories take their version from the tag
  `v3.0.0-pre` by one rule (`git describe --tags --long --match v[0-9]*`, the base `3.0.0-pre` in
  `nulldc3/version-base.txt` and the UI's `package.json`): `3.0.0-pre+g<h>` (`<h>` 10 characters of the commit) until
  the tag exists, `3.0.0-pre` on it, `3.0.0-pre+<N>.g<h>` after it, `.dirty` behind a tracked change. The emulator makes
  it at build time; `nulldc3 --version` prints `nullDC <version>`.
- **CI** (the owner, 2026-10-19; `PLATFORM-ROUND.md` section 6, packages VCu and CIe): GitHub Actions on pull requests
  and
  on tags `v*` in both repositories, GitHub-hosted runners only; a PR builds, tests and makes the release files as
  artifacts (kept 7 days); a tag push also attaches them to a GitHub Release, a pre-release for a tag with `-pre`; a
  dispatch checks the would-be Release of a tag and creates nothing. Actions pinned by full commit SHA; `contents:
  write` only on the job `release`. The jobs are thin over committed scripts (`nulldc3/ci/`, the UI's
  `scripts/release-files.ts`) that run on the three systems as they run on a runner. Nothing in CI files or CI logs may
  name a host, a user, a home path or a LAN address (the owner's words); the runner's own paths, user and machine name
  in GitHub's logs are PL-Q3, worked on as PL-D61 recommends until the owner answers (below, "Needs the user").
- **Tags**: `v3.0.0-pre` on both repositories, annotated, made and pushed by the integrator after both PRs of the
  platform round are merged and only with the owner's go; the UI's first, at the commit the emulator's gitlink names;
  each repository's dispatch check before its tag, and a second emulator check with `require_ui_tag` between the two
  tags (`PLATFORM-ROUND.md` section 7, PL-D66, PL-D67).
- Scratch, build dirs, reports: `dreamster/agents/nulldc3/` only; agents delete their clones and build trees before returning.
- **Machine**: 15 GB RAM. One workflow at a time; `ninja -j8`, `ctest -j4`, at most 4 emulator processes; every emulator run
  headless. Three parallel build-and-stress workflows were OOM-killed on 2026-10-06.
- Console access: none in this work unless the user asks; unmeasured behaviour is listed under "Needs the console".
- **Performance numbers** are taken with the hardware-accelerated renderers or the `none` renderer, never the reference
  software renderers (those make the run render-bound: 34-44 % of real time with refsw2). Both live in the
  `-DHAS_TA_LLE=OFF` build. For GL under WSLg run with `GALLIUM_DRIVER=d3d12` (the default GLX there is llvmpipe) and
  check `glxinfo -B` first. Every figure states its renderer. Existing `none` baseline (2ndmix):
  JIT 557-819 %, interpreter ~350 %. The TA_HLE build needs the `refsw2_do_dump` link fix (definition is inside the TA_LLE
  guard) and `none` should register in the default build too — T2.0 items.
- **Targets: desktop only — Linux, Windows, macOS — plus wasm later** (user, 2026-10-08). Processors: x86_64 and arm64 now;
  later a wasm32 build with its own wasm32 JIT, and possibly riscv64. There are no 32-bit native builds, and no Android, iOS,
  console (Switch, PS4), handheld (Pandora, GCW-Zero) or single-board (dispmanx) targets. Consequences for all new code: it stays clean for a 32-bit address space (no
  assumption that pointers are 64 bits, no reliance on huge virtual mappings, on host signals or on RWX pages in portable
  parts), and the interpreter-only generic host path stays buildable, because wasm32 and riscv64 start from it.
  Package **B64** (right after E4a merges; it touches `jit/`, `hw/arm7`, `hw/aica` and the build files) removes 32-bit host
  support: the SH4 backends `jit/backend/arm32` and `jit/backend/x86` with the emitters and assembly only they use
  (`jit/emitter/arm32`, `jit/emitter/x86`), the ARM7 JIT variants `arm7_jit_virt_arm32.cpp` / `arm7_jit_virt_x86.cpp`, the
  32-bit DSP JIT `dsp_x86.cpp`, the `CPU_X86` / `CPU_ARM` branches of CMake, `config.cmake` and `build.h`, and code that
  exists only for 32-bit hosts; and the cached-SHIL `cpp` backend (`jit/backend/cpp`, `DYNAREC_CPP`, the `TARGET_NO_JIT`
  option). What remains: the x64 and arm64 JITs and the interpreter (a future riscv64 build starts from the
  interpreter).
  New work targets x86_64 and arm64; since the platform round both are built and tested: x86_64 on Linux (here) and on
  Windows (MSVC, the Windows host), arm64 on macOS (the owner's Mac) and on Linux (an arm64 host); CI's
  `ubuntu-24.04-arm` and `macos-26` jobs (CIe) are to build and test them from the round's two PRs on, whose runs are
  the first (none was read when the round's gate ended). **Release targets** (the owner, 2026-10-19; `PLATFORM-ROUND.md`
  1.3 item 3): Linux an AppImage only, x86_64 and arm64, portable (the folder that holds the AppImage; no `.deb`, no
  per-user default); Windows a zip with `nulldc3.exe` (x86_64, the static CRT, a console program); macOS an ad-hoc
  signed `nulldc3.app` in a zip (arm64 only; no Developer ID, no notarization). Not released: Windows on arm64, macOS
  x86_64 (kept source-consistent and unbuilt).
  **B64 is done (merged 2026-10-08).** Its findings for the later ports: the interpreter-only build (`-DTARGET_NO_REC=ON`) and
  the generic host path (any 64-bit processor name CMake does not know as x86_64 / arm64) now build and boot; the generic path
  does not apply FPSCR.RM/DN to the host yet (`SetFloatStatusReg`: a BIOS boot ends in a different hash there), nothing in
  the CMake build defines `TARGET_EMSCRIPTEN`, an emscripten configure stops at the OS layer, and the debugger defaults on
  for generic hosts (sockets, threads). Also removed with it, because they only built 32-bit targets or the removed make
  build: the Android ndk-build files, the armv7 iOS Xcode project, the make-based CI recipes and the snap recipe. Left in
  place: unreachable `TARGET_PANDORA` switches in the window/input/renderer layer, a few helpers whose only callers were the
  removed backends, `deps/vixl/aarch32`, platform sources no build lists (Android shell, dispmanx, emscripten.cpp). The full
  audit list is in `agents/nulldc3/b64/final-verdict.json`.
  Package **P-desktop** — DONE 2026-10-10 (merged, `skmp/nulldc3` = 5ac00392; two verification rounds): Android, iOS, Switch,
  PS4, Pandora, GCW-Zero, Raspberry Pi specifics, NaCl, MIPS Linux, UWP, BSD / QNX / DOS host branches and the libretro core are
  out (147 files deleted, 43 modified, 13,637 lines); a configure for a non-desktop system stops with a message. Kept: Linux,
  Windows, macOS on x86_64 and arm64, the generic host path, the emscripten scaffolding. What a kept target compiles is
  unchanged text (checked mechanically); state hashes unchanged; five build configurations and 187 ctest targets green. Left
  for later packages, listed in `resume/p-desktop.final-result.json`: the three CI recipes of kept targets are stale (Azure
  macOS image, AppVeyor `reicast.sln`, Wercker's old box) — one CI package settles them (done: RB deleted them, CIe made
  the emulator's CI, both in the platform round); unconditional code only Android used
  (virtual gamepad, microphone, screen orientation) goes with the imgui GUI (track 10); SDL / joystick / XAudio backends that
  no CMake target lists; GLES paths and `gl.rpi4_workaround` go with the renderers (track 9).
- Timing tiers are **run-time** settings, one per processor, in a new settings group:
  `Timing.SH4`, `Timing.AICA`, `Timing.ARM7` = `fast` | `cycle-similar` | `cycle-accurate` (+ one preset selector in the UI).
  `fast` keeps today's speed tricks but with its functional bugs fixed, and is the only tier with speed options (idle skip,
  SMC check level, safe mode, unstable optimisations); the other tiers have none. The tier picks the engine; there is no
  user-facing interpreter switch. A tier change flushes the affected code caches.
- "Exact" behaviour (FPU values, AICA rules, ARM7 semantics) is tied to the tier: bug fixes go into every tier; behaviour that
  costs speed (bit-exact vector FPU, per-sample AICA with the measured rules) is on from `cycle-similar` up.

## Tracks, in order

### Track 1 — devtools integration  (IN PROGRESS)

Design: `TRACK1-DESIGN.md` (v2: three adversarial reviews, 72 issues dispositioned, SH4 mechanism prototyped). Summary: a debugger server inside the emulator (HTTP + WebSocket
on one port, default `127.0.0.1:55543`, UI bundle embedded), all guest-state access on the emulator thread, protocol v3
(v2 + target metadata, register merge, writes, tables, correlated binary frames), and a contract test runner that validates
either server (mock or emulator) with the UI's own zod schemas.

**Package status (2026-10-08).** Merged into devcast `skmp/nulldc3` (343c7bc8, 169 ctest targets green): E1 transport + UI
embed, E2 disassemblers, E0 shared headers (frozen; revisions 2 and 3 applied), E3 registers / memory / device side with the
sound regression harness, E4b SH4 breakpoints and steps native to the active backend, **E4a emulator thread and run
control** (four verification rounds; pause/resume, calls and breakpoint stops are time-neutral — 10,000 of each leave the
state hash unchanged; 7,382 exit runs with no hang; the in-emulator selftests run from ctest), and **B64** (no 32-bit hosts,
no `cpp` backend: 59 files and 23,700 lines removed, emitted x64 code / baselines / hashes unchanged; integration head
67b423e4). Devtools continuation done (`dreamcast-devtools` `skmp/nulldc3` = 2ea21ba, 322 unit tests, 218 e2e tests): the
contract runner now catches 165 of 185 deliberately broken servers; what it misses, and one legal server it refuses, hinge on
design decisions, which are made in `DESIGN-V3-DECISIONS.md`; the M1 panels have one test gap and four smaller items open.
Design revision 3 is written (`TRACK1-DESIGN.md` v3, 2026-10-09/10: 45 decisions in `DESIGN-V3-DECISIONS.md`, the verifiers'
feedback, the headers as built, the 2026-10-08 direction; `DESIGN-V3-OPEN.md` keeps what is open — an arm64 host, the config
section name, the submodule question). Done and verified 2026-10-10: the corrections revision 3 makes to merged emulator code
with the comment-only header revision 4 (merged, 171 ctest targets), and the devtools round (`dreamcast-devtools`
`skmp/nulldc3` = 17f3bff: 382 unit tests, 246 e2e tests; the runner's M1 gate requires every scenario to pass and judged 278
broken or legal servers correctly; panels readable at 1366x768).
**M1 IS COMPLETE (2026-10-10):** the debugger core (E5) is verified and merged — devcast `skmp/nulldc3` = ceac25db, 187 ctest
targets; devtools `skmp/nulldc3` = 22b358e. The web UI attaches to the running emulator: the runner's M1 gate passes against a
headless emulator on the JIT, the interpreter and the forced fallback (18 scenarios each), state hashes are identical with the
server off / on without a client / with an idle breakpoint / compiled out. Also merged with it: the register `ENABLED` of
`dc.aica.arm7` and the runner follow-up (design 4.2.2). Known and recorded: after a machine reset ARMRST reads "released" while
the ARM7 is not clocked until the guest first writes it (AICA / ARM7 tier work, needs a console measurement); the grace test
does not pin that a connect ends the expired grace state; no real MSVC build yet (package W64; made in the platform
round by W64a and W64b).
P-desktop (desktop-only cleanup, libretro out) is done and merged the same day (`skmp/nulldc3` = 5ac00392).
J1 is done and merged too (`skmp/nulldc3` = c0c81436, 190 ctest targets; two verification rounds and three read-only
reviewers): the run flag is an atomic word with the layout it had (header revision 5; emitted x64 code unchanged, no
slowdown), every emulator thread starts with an empty code cache and the code region is prepared once per process (2,200
host stop / start cycles in one process: no cache clear, same speed, same hash), `IsRunning()` is one word (a reviewer showed
that the five-word read could report a live parked machine as stopped), and the three leftovers of the core are closed.
The M2 round is cut (`M2-ROUND.md`, 2026-10-10; six review rounds, the last one clean): header revision 6 and ten packages in
this order — F3a (the M2 contract: schema, mock, runner), R6 (header revision 6 with stubs, the core's ARM7 wiring), E6a (the
sounding guest and the sound-line baseline), E6b (the resumable burst, ARM7 breakpoints and steps), E7a (memory and register
writes, reset), E7b (the M2 device-tree rows, read-only), F3b (end of protocol v2), F3c (register panels, editing), F3d
(toolbar, reset, hotkeys, a smoke spec against the emulator), M2G (the gate). The user answered its six questions on
2026-10-11: every recommendation accepted — M2 writes only the two processors' registers (device tables read-only; the device
write table is M3 / M4 work); a paused tick of about 130 KiB is fine; a debugger write of FPSCR or of the SH4 PC may empty the
code cache; a debugger write can end the emulator (only an odd PC is refused); and two items stay out of the round and go to
the backlog: reios leaving the AICA interrupt registers as the retail boot ROM does (tracks 5 and 7, needs a console check;
under reios 2ndmix and stock KallistiOS sound are silent because timer A is never enabled), and whether a build with the SH4
recompiler but without the virtual memory layout has to run guests. F3a is done and verified (2026-10-11; devtools
`skmp/nulldc3` = e0f7895: the mock is an M2 server, the runner has the M2 rules and gate; three verification rounds). R6 is
done too (header revision 6 with stubs, merged: devcast `skmp/nulldc3` = f6949750, 190 ctest targets; the wire is unchanged).
E6a is done (2026-10-11, second verification; merged as 5a3ad3d6, 206 ctest targets): `tests/debugger/guest/dsptest/`, a
bare-metal guest whose ARM7 plays two channels and takes counted FIQs, the runner of the sound lines (`soundlines.py`) and the
`debugger.sound.*` / `debugger.guest.*` targets; soak of 200 runs and memcheck clean; 2ndmix under reios observed silent, as
inferred. E6b is done (2026-10-11, second verification; merged as b4cb3e82, 238 ctest targets): the sound burst of a tick can be
suspended between two ARM7 instructions and run on, ARM7 breakpoints / step / step over / step out, the ARM7's register writer,
the callstack while stepping; with it the UI's ARM7 controls work against the emulator. Sound lines and state hashes are
identical before and after, with the debugger compiled out and with it at work on the ARM7; ThreadSanitizer clean. Open for
M2G: a 1-2 % loss of speed by CPU time that every measured set shows and whose cause is not found (the instruction-count
checks of the cut pass); known limits in the cut's Appendix I.4. E7a is done (2026-10-12, second verification; merged as
b951e440, head d3fe072d with one comment-only commit, 249 ctest targets): `state.writeMemory`, `state.setRegister`,
`state.editWatch` and `control.reset` on the emulator, with the compiled blocks of every written page discarded and each
register write doing what the guest's own write would (bank switch, FP mode, code cache). Leftovers of the emulator packages
(test hardening, none blocks) are collected in the cut's Appendix I.5 for one small package `EX` in front of M2G.
E7b is done (2026-10-12, second verification; merged as 199cdb4a, 259 ctest targets): the M2 rows of the device tree, read-only
and free of side effects; the contract gate M2 passes against the emulator on the three engines (19 scenarios) and against the
guest; a client that only listens leaves the state hash that of a run without a server. It also made read-only system bus
registers that nothing assigns read 0 (they read part of a host address, different from run to run). **The emulator side of M2
is complete.** F3b is done (2026-10-12, first verification; devtools `skmp/nulldc3` = 1183dab, the UI pin moved there): every
branch for protocol v2 is gone from the client, a server without `protocol` gets the banner, the synthetic e2e server is a v3
server; 488 unit tests, 251 e2e, the emulator's 259 targets twice with the UI at that head.
F3c is done (2026-10-12, second verification; devtools `skmp/nulldc3` = af97c4d, the UI pin moved there): the register panels
(SH4, ARM7 and the M2 nodes), editing of registers, memory bytes and watch values while paused, with a keyboard path to every
editor; editors only where the server marks a row writable; 543 unit tests, 312 e2e.
F3d is done (2026-10-12, second verification; devtools `skmp/nulldc3` = 6c753dc, the UI pin moved there): the run controls
as a component (Run, Pause, Step Into / Over / Out for the focused target, Reset), the hotkeys, the ARM7's step buttons off
while it is held in reset, and `e2e/emulator-smoke.spec.ts`, which passed 4 of 4 against a headless emulator of 199cdb4a with
the bundle embedded (break at the entry, step, edit R0, write memory, reset, step the ARM7); 583 unit tests, 347 e2e.
**Every package of M2 but the gate is verified: the emulator at devcast 199cdb4a, the UI at devtools 6c753dc.**
Paused 2026-10-12 at the user's request, resumed 2026-10-14. `EXe` (cut 3.11) is done (2026-10-14, second verification;
merged as 6fe01759, 259 ctest targets: tests, three header sentences, one refusal). `EXu` (3.12) is done (2026-10-15,
four verifications and one integrator commit; devtools `skmp/nulldc3` = d59336b, the UI pin moved there): the end of an address
space reachable in a small memory panel, a watch renamed from the keyboard, 0x-only register values, sharper tests.
M2G, the gate, is done (2026-10-15; branch `skmp/nulldc3-m2g` on 6fe01759, documents only): **M2 IS COMPLETE.** On devcast 6fe01759 with the UI at `d59336b` and emulators that carry its bundle (`ui` of `debugger.describe` is `3.0.0-pre+d59336b`): the M2 gate passes against the emulator on the JIT, the interpreter and the forced fallback (19 scenarios passed, 0 partial, 0 not applicable each) and its second run against the sounding guest (2 passed, both notes); the 259 ctest targets are green twice, none skipped; the sound lines of the guest and of 2ndmix equal those of the tree before E6b, and the state hashes of 5.7 those of the tree before the round, in the five builds; `npm run check` and the e2e suite pass in a fresh clone; the smoke spec passes against a headless emulator on the guest. ThreadSanitizer of the contract parts on the guest: no report. Speed (tests/perf, none renderer, by CPU time, ten rounds): the JIT 0.6 % faster than before E6b, the interpreter 3.4 % slower, from code placement (the same machine code of the interpreter's dispatch 32 bytes off its place in a cache line; 0.07 % more instructions), not from added work; nothing changed for it. The design is brought to the cut (its section 7), with the known limits of the round in its 2.3.7 and what each package left in its Appendix A.3, "M2 as built". What the round leaves to later work is listed below ("Backlog left by the M2 round").
Merged as f640f21a (2026-10-15). Two findings of the gate come first, as package `D1` (the integrator's order, 2026-10-15):
(1) **an intermittent crash at boot**: the default build with the SH4 recompiler ends about one run in six right behind
"REIOS: Booting up" with `fault_handler: unhandled SIGSEGV at address 0x...0d0 ... not a guest memory fault`, with no exit
line; it is there on d546aa76 and 5a3ad3d6 as on the tip, and E4a's verifier runs (2026-10-08) carry the same signature, so it
is older than the round; a run that boots gives the identical hash. (A person who starts the emulator meets it: not so,
see D1 below.) (2) **the
fallback leg of the gates checks no engine**: `contract.py --fallback` and the selftest registrations `arm7.fallback` /
`writes.fallback` pass when the emulator ignores `Debugger:ForceFallback`, and `debugger.contract.m2` on the JIT passes on the
fallback engine; M2G's own check (`m2g/fix-1/scripts/contract_lines.py`) held the gate. Both directions are to be required
(`sh4-debug-fallback` in --fallback mode, `sh4-debug-native` on a JIT target without it), and the two mutants `nofallback`
and `allfallback` (`m2g/verify-2/`) turn every such target red.
**D1 DONE 2026-10-15** (`resume/m2-d1.final-result.json`; two verifications; devcast `skmp/nulldc3` = 388e1b38, merge of
3c4481f9, plus the integrator's comment commit e9b945e0; 265 ctest targets). The cause of (1), shown: every emulator
created its memory file under one shm name, `/dcnzorz_mem`, with `O_EXCL`, and unlinked it right after
(`libswirl/linux/posix_vmem.cpp`, since 2019); an emulator starting inside that window removed the other's name and,
with no data directory to fall back to (the gates' bare `HOME`), ran without the virtual memory layout ("nvmem is
DISABLED"); there the recompiler's write-protected code page could not be resolved (M2-Q6 below) and KOS's first store,
`old_sr` at 8C0100D0 in the page of the code just compiled, ended the process. So it hits emulators started at nearly the
same moment, as the gates start them; a person with a data directory only fell back to a file. Fixed at the cause (the
memory file is the process's own: memfd, else a per-process shm name, else `mkstemp`) and at M2-Q6 (`bm_LockedWrite`
measures from main memory's own buffer without the layout). Measured: base 24-35 of 100 gated boots crashed per JIT
build, the interpreter 0 (it runs without the layout fine); tip 0 of 500. New targets `system.vmem.concurrent`,
`.taken`, `.none` (+ hle). (2): `contract.py` checks the debug engine both ways, every selftest session checks the SH4
engine and the debug engine against its configuration (`engine.asked`); the mutants `nofallback` / `allfallback` turn
every such target red. The `unhandled SIGSEGV at address (nil)` of E4a's windowed close-during-reset runs is another
cause (at shutdown, behind DestroyContext, on the interpreter too), and so is the ThreadSanitizer fault of
`e4a/fix-3` (TSan's shadow store on Xbyak's first write into the code cache, deterministic on a TSan JIT build).
Then: `N1` (the rebrand: `nulldc3`, portable by default, two-dash switches; done).
**M3 IS COMPLETE (M3G, 2026-10-19).** The round was cut in `M3-ROUND.md` (2026-10-16, three review rounds and the owner's
answers to M3-Q1 to M3-Q4: the AICA clocked whatever ARMRST says, speed gates nothing, the device writes to M4, no BIOS
workload) and built as twelve packages, each verified by verifiers who did not write it, with mutations: the UI's F4a
(the contract of M3), F4b (the playground on the emscripten build of the one DSP core), F4c (the SGC panel), F4d ("Load
from emulator", the DSP's controls whatever the ARM7 does) on devtools `skmp/nulldc3` = 066fcbc; the emulator's R7
(header revision 7), W1 (the T5.0 core is the emulator's DSP), W2 (the batch path and its settings go), W3 (the AICA
clocked whatever ARMRST says), G (the guest's DSP program and its oracle), E8a (DSP breakpoints and steps), E8b (SGC
capture, memory binary, the binary path) on devcast `skmp/nulldc3` = 9fbc988a (307 ctest targets), and the gate M3G:
passed (2026-10-19; branch `skmp/nulldc3-m3g` on 9fbc988a, documents only), with emulators that carry the UI's bundle (`ui` of `debugger.describe` is `3.0.0-pre+066fcbc`): the M3 gate's run 1 on the JIT, the interpreter and the forced fallback (20 scenarios passed, 0 partial, 0 not applicable each), its run 2 on the guest (S00, S17, S21: 3 passed with the note `sgc: captured with the arm7 held`; `--only S00,S21`: 2 passed with the note `dsp: stepped with the arm7 held`) and the M2 gate's second run on the changed guest (2 passed, both notes); 307 ctest targets green twice, none skipped; the sound lines and the state hashes equal B4's (12 of 12 entries, 38 of 38 rows); `npm run check` (735 unit tests) and the e2e suite (401 passed, 1 skipped: the smoke spec, run on its own against the emulator) in a fresh clone; the smoke spec against the emulator; ThreadSanitizer of the sessions `dsp` and `sgc` and of the guest's contract runs: no report. Speed (tests/perf, none renderer, by CPU time, ten rounds, measured and reported, gating nothing): the end of the round 0.798 (JIT) and 0.919 (interpreter) of the round's start in its default setting (the batch render without a DSP), 0.902 and 0.960 of its per-sample path; host instructions about +3060 a sample for W1 to W3 together (G's merge against the round's start), +940 for E8a and E8b, about +4000 for the round's total (the end of the round against its start). The design is brought to the cut (its section 8), with the known limits of the round in its 2.3.7
(items 25 to 30) and what each package left in its Appendix A.3, "M3 as built". What the round leaves to later work is
listed below ("Backlog left by the M3 round").
**THE PLATFORM ROUND IS COMPLETE (PLG, 2026-10-24); its PRs and tags are the integrator's, with the owner** (section 7
of the cut). The round was cut in `PLATFORM-ROUND.md` (2026-10-19, three review rounds; the owner's decisions of 1.3:
`reicast/` becomes `nulldc3/`, "nullDC Team" and `org.emudev.nulldc3`, AppImage-only Linux releases for x86_64 and
arm64, a Windows zip, an ad-hoc signed macOS zip, CMake only on macOS, the tag `v3.0.0-pre` on both repositories, CI on
pull requests and tags, headless on Windows and macOS, the portable part of the tests there, the approved icon
everywhere, the UI's PR first) and built as thirteen packages, each verified by verifiers who did not write it, with
mutations: the UI's VCu (its version from the tag, `.gitattributes`, its CI and Release) on devtools `skmp/nulldc3` =
7b88410; the emulator's RB (the rebrand), IC (the icons), VTe (the version at build time, `--version`), W64a (the MSVC
build, Windows headless, the no-dialog policy, the ABI fixes), W64b (the gates on the Windows host, the x64 JIT's native
checks there), W64c (the selftests on Windows), MACa (the CMake bundle, macOS headless, `macrun.py`), MACb (the gates on
the Mac), LA64 (Linux arm64, natively on an arm64 host the owner made available on 2026-10-22), CIe (the emulator's CI,
the release files, the AppImage's portable directory) and A64 (the arm64 recompiler's native debugger, added at the
owner's word of 2026-10-23 before the gate: its slice counter kept, its context at the slice end, the native checks; the
thirteen known limits of the arm64 recompiler's debugger and S04's arm64 rule went with it, and its state-hash
references were taken anew, PL-D86) on devcast `skmp/nulldc3` = 12069163 (327 ctest targets on Linux x86_64), and the
gate PLG (branch `skmp/nulldc3-plg` on 12069163, documents only): on Linux, at the documents' head, with emulators that
carry the UI's bundle (`ui` of `debugger.describe` is `3.0.0-pre+g7b88410915`), the contract gates M1 (18 passed), M2
(19) and M3 (20) on the JIT, the interpreter and the forced fallback and the guest's runs with their notes, 327 ctest
targets green twice, none skipped, P0's 38 rows and 12 sound lines identical, ThreadSanitizer of the guest's contract
runs without a report, `npm run check` (793 unit tests passed, 1 skipped; the four contract runs on the mock OK) and the
e2e suite (401 passed, 1 skipped: the smoke spec, run on its own against the emulator) in a fresh clone of the UI, the
smoke spec, the x86_64 AppImage with its five licences and its binary equal to P0 on P0's default rows; on the merged
tree, the Windows host 349 targets twice (77 skipped with their reasons), the same contract gates on every engine, the
zip with its version resource ("nullDC Team", `3.0.0.0`); the Mac 340 targets twice (81 skipped), the same contract
gates, `macos.hashes.jit` on A64's references, the zip of `macos.sh` with its signature checked and its smoke run; the
arm64 host 330 targets twice (16 skipped), the same contract gates in full on every engine, the arm64 AppImage; no crash
report on any system; `actionlint` and the hygiene of both repositories clean. The design is brought to the round (the
16 rows of the cut's Appendix D, as A64 left the tree), with the known limits of the round in its 2.3.7 (items 31 to
33). What the round leaves to later work is listed below ("Backlog left by the platform round"). The owner's by-hand
list (PL-D40) is in the emulator PR's text.
After that, one workflow at a time, in this order unless the user reorders: M4 (E9, F5), track 10 (one web UI), track 3
(cycle-similar). Devtools: F0, F1, F2 and the round after design revision 3 are done and verified.
Known defects recorded by E4a's verifier, not caused by it (package J1): the x64 JIT keeps the code of earlier emulator threads, so after a
few hundred host stop/start cycles in one process the 10 MB code cache is full and the guest crawls; `Sh4Context::CpuRunning`
is a plain volatile written across threads (every ThreadSanitizer report); event breakpoints latched inside SLEEP waits and
single steps on the JIT shift guest time (the documented skew class).

**Backlog left by the platform round** (from the cut's 1.2, its open questions, the owner's decisions of 2026-10-22,
the integrator's notes of its Appendix I and PLG; none of them blocks M4):
- *The arm64 ARM7 recompiler* (`hw/arm7/arm7_jit_virt_arm64.cpp`, not built in the round, PL-D46: on Apple silicon it
  writes its MAP_JIT cache without lifting the write protection, unmaps a pointer it never set, maps a region per
  emulator thread that it never frees and branches to C++ without a range check); with it the ARM7's fallback and its
  notice (design 2.5.3) would run for the first time. And *the SH4 FPU fast-tier fixes of the arm64 JIT* (design 2.3.5:
  its results are held to its own references, not compared with the interpreter's). Both were named with A64 before
  the round; A64 delivered the native SH4 checks only.
- *"Boot BIOS" with no BIOS copied in ends `nulldc3.app` on macOS* (`EXC_BREAKPOINT` in `os_DebugBreak()`; the owner's
  run, 2026-10-22; by A64's reading reios boots `0xAC008300` from RAM filled with `0xFF` and the opcode `0xFFFF`
  reaches `cpu_iNimp`'s `die`, on every engine): to be fixed after the round, which the round's PRs call M3b (the
  owner's decision).
- *macOS x86_64*: kept source-consistent and unbuilt (PL-D47); no release.
- *CI's test data* (PL-Q1, open): 2ndmix, hello and the RTL checkouts in CI once their licences are settled ("Needs the
  user"); the two ELFs as they are print a KallistiOS build banner that names the Windows host and its user, so CI needs
  copies built without the owner's details.
- *The macOS icon with Apple's margin* (PL-Q2, open): the approved art ships on every system (PL-D23); a padded variant
  for the `.icns` alone if the owner approves one (IC's generator then takes a second PNG set).
- *The runner's own paths, user and machine name in CI logs* (PL-Q3, open): work proceeded on PL-D61's reading (they
  are GitHub's, not the owner's); the integrator scans the first runs' logs for the owner's own details.
- *The Darwin sent-signal rule* (`si_code`, PL-D50): a `kill -SEGV` of a harness is not a path any test takes.
- *`reidbg/` and the `refsw-dbg` server* (PL-D14): the client and the built server are kept or removed together.
- *The owner's by-hand list* (PL-D40; it gates nothing): per system in the emulator PR's text — the windowed start, the
  icon, About, a game, pause with the debugger and close the window (parked, and with a pause pending), the window
  sessions of design 6.1.2, Explorer's Properties on Windows, the Finder's start of the unzipped app on macOS, and on
  the Mac a breakpoint, a step, a step-out and a pause from the UI on the arm64 recompiler (A64).
- *Flakes seen once and passed when run again* (design 2.3.7 item 32): on the Mac's loopback `debugger.net.test_e2e`
  (`test_e2e.cpp` line 306 assumes the kernel cannot hold 16 MiB for a peer that does not read) and
  `debugger.contract.m2.guest.interp` (S16's reset not seen within 5000 ms, once in three full runs), which `macos.sh`
  runs once more when they are a run's only failures; the arm64 host's `debugger.selftest.hostcycles` (its rate window);
  the Windows host's `debugger.selftest.emurun.pauses.interp` (its `SelfTest:` line spliced into a guest console line).
- *Left by VCu* (the UI): `gitFacts` is not held to `gitEnvironment`'s filter by a test (a build run from a git hook
  would take the hook repository's version); `publish`'s size check takes a longer asset with no digest; no unit case
  pins the zip entries' 1980-01-01 time or that `v3.0.0-pre-2` is no tag; `e2e/support/globalSetup.ts` `BUILD_INPUTS`
  does not list `scripts/version.ts`; the `BundleInfo.version` comment of `scripts/bundle-info.ts` still says
  `+<git describe>`.
- *Left by IC*: `icons.x11` checks the header against the generator and its own hash only (a generator change made
  together with new outputs passes both; a few exact values would pin the pixel format), and `parse_ico` /
  `parse_icns` have no literal tables; `x11.cpp` includes the 300 KB data header for a declaration only (an `-O0`
  build carries the array twice).
- *Left by W64c*: `AsSleepFor` compares the POSIX nap with `sleep_for` at five requests only; the `dsp_playground` skip
  is registered whenever `disasm_winrun` is empty (a lost winrun line would turn into a skip, not a failure); a build
  of `tests/debugger/disasm` by itself on Windows without `NULLDC_WINRUN` runs `dsptest`'s tsx outside a winrun;
  `debugger.disasm.standalone.configure` needs the VS developer environment.
- *Left by MACa and MACb*: `macrun.py`'s signal path (the detach worker's SIGTERM) and its `--detach` rc file when a
  signal ends it are held by no test; `macos.build.ccache`'s coverage.
- *Left by LA64 and CIe*: what only the `ubuntu-24.04-arm` runner shows (BTI and PAC on Neoverse N2, Ubuntu's arm64 SH
  objdump with its char signedness: `linux.sh` passes no SH objdump there, so the two `debugger.disasm.sh4_objdump`
  targets skip in the `linux-arm64` job, unprivileged user namespaces); `smoke.py`'s outer `finally` is covered by no
  case; the pinned v4 actions run on Node 20.
- *Left by A64* (design 2.3.7 item 33): mutants of the arm64 recompiler that stayed green and were judged by reading
  (the room a compile keeps for `CheckBlock` and for a pending literal pool; five equivalents); the
  `-DNULLDC_DEBUGGER=OFF` build about 1.3 % slower on the arm64 host after A64 (the owner's "don't mind perf for now"
  of M3); `emu.fp` of `debugger.selftest.emu` is x86-64 only, so on arm64 nothing holds the host FP mode across the
  park while `dbg_sh4_Check` runs C++ with the guest's FPCR (`sh4_host_fp_resync`); no target reloads the sh4bp
  suite's guest after a block in the second to fourth 4 KiB page of a 16 KiB page was compiled and patched (A64's
  `LoadGuest` step of 4 KiB, accepted by reading).
- *Left by PLG*: two documents outside Appendix D that read as before A64: the decisions' item 25
  (`DESIGN-V3-DECISIONS.md`, "The breakpoint fallback concerns arm64 only (until it implements the check)", a dated
  decision whose parenthesis A64 has since met) and `docs/nulldc3/sh4-breakpoints-spec.md` 3.j (the arm64 backend as
  requirements); for a later change of those documents. The release notes' known issues are drafted for CIe's template
  (`nulldc3/ci/release-notes.md` has no section for them), not committed:
  `agents/nulldc3/plg/release-notes-known-issues.md`, the integrator's to take.
**Backlog left by the M3 round** (from the cut's 1.2, the owner's answers to M3-Q1 to M3-Q4, the integrator's notes of its
Appendix I and M3G; none of them blocks M4):
- *The SGC's sends and fetches in time* (M3-D10): `ChannelFetch`, `BusRead` and `MixsSendLate` are not called in the
  `fast` tier; they come with the `cycle-similar` AICA of track 5 (caique's sound generator), and with them SGC record
  bytes 76..83 (the filtered, post-AEG, post-FEG and post-TL samples).
- *The AICA's register writes* (the owner's answer to M3-Q3, "Move to M4"): the write semantics of `dc.aica.common`,
  `dc.aica.sgc.N`, the RTC and the DSP's rows, and the body of `AICA::DebugWriteReg`, with every device's write table in
  M4 (E9).
- *A BIOS workload* (the owner's answer to M3-Q4, "Not in M3"): a package of its own once the owner names a data path
  for a BIOS; the state-hash workloads are 2ndmix 600 and 4000 vblanks, hello 300 and the guest at 90.
- *The DSP core's state over the wire* (M3-D35): a protocol method for the core's blob would let "Load from emulator"
  continue the DSP exactly from any stop (today only from a stop inside a sample's DSP phase, `STEP` 1 to 127).
- *Speed* (the owner's answer to M3-Q2, "don't mind perf for now"): nothing was changed for speed in M3 (the
  empty-program lever of the cut's round 2 is not started); M3G's numbers (design 5.7, "Speed at the end of M3") are the
  start of a round that takes it up.
- *Left by G*: `reios_loadElf` (`libswirl/reios/reios_elf.cpp`) never frees the ELF's buffer (valgrind's memcheck: the
  guest's 2708 bytes definitely lost; older than G); `tests/aica_dsp/guest_oracle_main.cpp` reads the ELF's section and
  symbol tables without bounds checks (a truncated copy could read past the buffer instead of exiting with code 2); no
  target checks the rules of the cut's 6.3 on the guest's program text (`dsptest_dsp.txt`: odd memory steps below 100 and
  away from the channels' slots, every IWT two steps behind an odd MRD, the ring at or above 0x20000, step 127 empty).
- *Left by E8b*: a capture's deadline taken when the arming call has returned, not at the request (design 2.3.7 item
  29); a `std::bad_alloc` on the server's send of a 32 MiB frame escaping to `std::terminate`, peak memory about three
  times the frame (item 30); two possible flakes, never a false pass (`sgc.drain`'s fixed 300 ms nap, which a hook such
  as `SetBeforeTableCallForTest` could replace; `test_bounds`' margin of about 4.6 against its slow read); a failed check
  of the session `sgc` between `CoreOn()` and `CoreOff()` fails every later core-based test of the run; not tested: a
  `recordSgcFrames` table call that the park loop takes at a stop, a request while a step runs, `HandleRequestText` with
  no core thread; `contract.py`'s modes `m3` and `m3dsp` do not assert 20 and 3 scenarios (the gate states them).
- *Left by F4d* (the UI): `emulatorSnapshot.ts` leaves a rejected promise unhandled when `describe` outlasts the tick
  timeout and then fails; closing the playground's panel while the 8 MiB read is on its way still starts its audio (an
  AudioContext nothing closes); no test holds that Start DSP ends the snapshot's banner; the smoke spec finds the
  capture's "1024 frames" by text, not by F4c's "Capture length" status, and does not check the request's
  `{frames: 1024}`; the e2e test of the DSP's controls with the ARM7 held covers its step, not its breakpoints.
- *Left by W1 and W2*: the macOS Xcode project lists none of the files added since the debugger came in (the DSP core,
  `aica_hash.cpp`, `libswirl/debugger/`) and is not built here: one macOS pass; two mutations accepted (the `dsp` fold
  leaving out the blob's last byte; the CD-DA pair given to EXTS under the dead `CDDAMute`);
  `libswirl/profiler/profiler.cpp` declares and prints `samples_gen` inside `#if defined(HAS_PROFILE)` (no build defines
  it; one that did would not link), and `ChannelEx::Step` has an unused `bool` return.

**Backlog left by the M2 round** (what the design's 2.3.7 and its Appendix A.3 cite as "PLAN's backlog"; from the cut's
1.2, the answers to M2-Q3 to M2-Q6 and the integrator's notes of its Appendix I; none of them blocks M3):
- *Left by D1* (its verifier's notes and the engineer's): `debugger.describe` names no SH4 backend, so `contract.py`
  cannot see a backend that prints the wrong one (the selftest sessions do: `engine.asked`; a capability would be a
  protocol decision); `contract.py`'s modes `grace` and `hash-listening` check the StartPaused stop without waiting for
  it, and fail an emulator whose machine is made 1.5 s behind its port line (older than D1; the UI runner's
  `awaitStartStop` is the model); `vmem_platform_destroy` never closes the memory fd, one per re-made machine; a TSan
  build with the JIT faults in the recompiler's init (TSan versus the code cache); the `(nil)` fault at shutdown of
  windowed close-during-reset runs (E4a) is open.
- ~~*The SH4 recompiler without the virtual memory layout* (M2-Q6)~~ **fixed by D1 (e6105303), target
  `system.vmem.none`.** The text as it was: (M2-Q6; design 2.3.7 item 15). `bm_AddBlock` protects a page
  from which a block was compiled in every build, and `bm_LockedWrite` measures the faulting address from
  `virt_ram_base`, which is null there, so a guest store into such a page ends the process. To do: resolve the fault
  from the address of main memory when there is no layout. No configuration gives that build on the development
  machine; E7a's verifier made it, with the layout forced off by a scratch change of `_vmem_reserve`
  (`agents/nulldc3/e7a/verify-1/logs/nolayout.txt`; again in `verify-2/`, `scripts/nolayout.sh` and
  `logs/nolayout.txt`): the session `writes` passes on both engines and on the forced fallback, 2ndmix runs 600 vblanks
  to one state hash twice, and a debugger writer without its own unprotect ends at its store (`fault_handler: unhandled
  SIGSEGV ... not a guest memory fault`), the fault a guest store meets there.
- *An SH4 block cache with the address in its key* (the cut's 1.2; design 2.3.7 item 6; M2-Q4). `fpcb` is indexed by
  `(pc >> 1)` under a mask of 32 MiB, with no address in the key, so addresses a multiple of 32 MiB apart share a slot:
  main memory at `0x0C…`, `0x8C…` and `0xAC…`, and the boot ROM and main memory (the dispatcher clears the cache at three
  boot addresses). A guest that changes mirror by itself runs the blocks of the other one, with or without a debugger.
  M2 only keeps the debugger from doing it to a guest: a debugger write that moves the SH4's PC empties the code cache
  (M2-D42). With the address in the key, and with blocks keyed by the FP mode for FPSCR (T3.4), the debugger's two
  cache resets are no longer needed.
- *The exit line in the tick of a latched stop* (design 2.3.7 item 20; the integrator's decision in the cut's I.4). When
  a check of the ARM7 or an event breakpoint latches a stop in the tick of an exit or host-stop request, the stop
  callback sees the SH4 in front of an interrupt acceptance that an undebugged run has already made (the latch deferred
  it; `OnThreadExiting` makes it behind the callback), so the `sh4` and `hash` parts of the exit line can differ from
  the plain run's although the guest does not sleep. To do: the deferred acceptance in front of the stop callback, a new
  entry of `dbg::emu` and so a header revision. The scratch test that shows it: `agents/nulldc3/e6b/fix-1/`
  (`scripts/p4_test.inc`, `scripts/p4_run.sh`, `logs/p4-exitintc.txt`).
- *A step out of the ARM7 that knows the mode it was asked in* (design 2.3.7 item 21). The temporary breakpoint stands at
  the live R14, which in FIQ mode is the return address plus 4, so a step out asked inside the FIQ handler ends one
  instruction behind the interrupted one, or elsewhere when it branches. With it goes a header text of `dbg_arm7.h`
  (H6-5).
- *Undefined instructions and odd PCs as guest exceptions* (design 2.3.7 item 14; M2-Q5). A guest, or a debugger write,
  that takes the SH4 to a word that is no instruction ends the process in the default build (the odd PC is in track 2's
  defect list too). Until then the debugger refuses an odd PC of the SH4 and stores the ARM7's PC aligned.
- *For tracks 5, 6 and 7* (the cut's 1.2, its last row). reios leaves `SCIEB` and `SCILV0-2` at 0, so the ARM7 of 2ndmix
  and the stock KallistiOS driver never get their timer FIQ and are silent (M2-Q3: tracks 5 and 7, with a console
  check; a fix moves 2ndmix's sound lines and lands with a new baseline). A machine reset does not reset the GD-ROM
  drive and leaves most system bus slots as they were. The boot ROM workload of the sound harness is silent for an
  unknown cause. ARMRST reads 0 behind a machine reset while the ARM7 is not clocked, until the guest first writes it
  (design 2.3.7 item 13; tracks 5 and 6, with a console measurement).

| Milestone | Content | Gate |
| --- | --- | --- |
| M0 | devtools hygiene: lint/typecheck/tests/build green, real scripts, versions, docs, fresh clone works without Rust — **DONE** (13 commits, verified from a fresh clone); follow-up in progress: Playwright e2e smoke suite, error boundaries, offline Monaco | `npm run check` |
| M1 | protocol v3 + contract runner; emulator transport; SH4 debugging complete; device tree with real registers; events | contract test green on mock and emulator |
| M2 | ARM7 step/breakpoints; memory/register writes; reset; generic register panels; follow-PC; toolbar run control | contract (arm7), UI tests |
| M3 | SGC capture; DSP step/breakpoints/disassembly; playground linked to the live emulator — **DONE** (M3G, 2026-10-19; `M3-ROUND.md`) | contract gate M3 in its two runs (sgc/dsp) |
| M4 | CORE inspector fed by the emulator; TA inspector; TLB/SQ/OC-RAM tables; TA/CORE event breakpoints | contract (holly) |
| M5 | SH4 simulator panel re-based on the measured model (shares the table generator of track 3) | fixture gate >= 300/305 |

### Track 2 — test suite  (T2.0-T2.4 DONE, verified, merged; 146 ctest targets on `skmp/nulldc3` with the debugger's suites)

**T2.0 (merged).** Headless runs (no display needed), `Exit.AfterVblanks/AfterSeconds/OnGuestExit` with a machine-readable
exit line and a state hash, pinned RTC (`Dreamcast.RTC`), `-config` overrides not saved, `-help` and fatal faults terminate,
`none` renderer in the default build, the TA_HLE build links, `tests/system`, `tests/run_control`, `tests/perf`
(`docs/nulldc3/automated-runs.md`). **Determinism: solved for automated runs** — the causes were the RTC taken from host
time and window input; with the RTC pinned and no input, 600 JIT and 600 interpreter runs give one hash each (also under
load and pinned to one core). JIT and interpreter hashes differ from each other because fast timing charges them differently.
Quiet-machine baseline (2ndmix, `none`): JIT about 1,224 % of real time, interpreter about 550 %; GL on the GPU: JIT about
1,111 % (`tests/perf/BASELINE.md`). Perf figures taken while other work loads the machine are not comparable.

In progress on top of it (branches `skmp/nulldc3-fpu-tests`, `-sh4-fixes`, `-arm7-fixes`): the FPU suite against the shrike4
console records and reference (by path), and the fast-tier fixes it and the other suites gate — FPSCR reset value and write
mask, PR written before the delay slot (BSR/BSRF/JSR, interpreter; found by the directed rows), RTE delay-slot bank per the
manual, host FP state per thread, FTRC results, fused FMAC, one FIPR/FTRV summation order across engines, PR=1 no-ops, DAZ
with DN=1, FSCA zero signs; ARM7: the console-backed defects only (D1-D7); data-sheet-only ones stay pending the console.

Reports: `nulldc-tests.md`, `devcast-build.md`.

**State (2026-10-05).** `devcast/tests/` is a standalone CMake/ctest project (13 tests, about 3 s): `sh4.singlestep.interp`,
`sh4.singlestep.dynarec`, `sh4.differential` (interpreter against x64 dynarec on the same initial states), `sh4.directed.*`,
`arm7.vendor`, `arm7.directed`, `arm7.wren7.{vendor,qemu_diff,mulsem,bringup}` (console data referenced by path through
`NULLDC_WREN7_DIR`; nothing from wren7 is copied), `common.selftest`. Vectors are git submodules under `tests/vendor/`. Gates
are per-file baseline ratchets (`--strict` in ctest); known defects are named in `KNOWN_DIFFERENCES.md` / `KNOWN_FAILURES.md`.
Verified by fresh clone, vacuity experiments (corrupt/truncate/remove), and mutation (6 of 6 seeded emulator bugs caught).
Counts: interpreter 73,780 exact of 74,274 run (42,226 filtered with a stated reason); dynarec 73,497; differential 78,891
identical of 91,989; ARM7 45/45 vendor, 3,997/4,000 qemu_diff.

**Emulator fixes that came with it:** unaligned accesses end in an overridable fatal handler instead of `for(;;)`;
`_vmem_read_const` / `_vmem_page_info` use the handler id (the x64 JIT returned 0xDEADC0D3 for PC-relative literals executed
from handler-backed pages — a compiled boot ROM running in place hits it; the retail BIOS does not).

**Defect backlog the baselines pin (to be fixed by tracks 3, 5, 6 — each fix must move its baseline):**
- SH4: OCBWB/MOVCA.L "OIX" hack keyed on address bit 25 (248 crashes, 48 wrong); RTE delay-slot bank differs between
  interpreter and dynarec (181); dynarec dies on FPSCR.RM >= 2 (299); FIPR/FTRV summation order differs between engines and
  from the chip; FTRC of NaN differs; DN=1 without DAZ; FPU ops execute with SR.FD=1; privileged ops with MD=0; TRAPA with
  BL=1 dies; unaligned access / odd PC are fatal rather than address errors; FPSCR reserved bits writable.
- ARM7: ROR by zero register amount corrupts C (D2); MULS/MLAS carry (D3); MUL/MLA with Rd == Rm (D4); reset CPSR 0x53
  instead of 0xD3 (D7); plus D1, D5, D6, D8-D12 from the wren7 report, confirmed by probes but not yet pinned by a suite.
- T2.0 **Runnable in CI**: headless mode (no window), `--exit-after-frames/seconds`, speed line on stdout, fatal errors and
  `-help` terminate instead of hanging, `none` renderer available in the default (TA_LLE) build, per-run config isolation.
  Also here: **run-to-run determinism.** The breakpoint prototype saw about 1 run in 150 take a different guest path with
  identical inputs (no debugger involved); the cause is not known. Until it is found, gates that compare state hashes use a
  repeat rule. Cycle-accurate work needs this fixed.
- T2.1 **Harness**: `devcast/tests/` CMake project + `ctest`; core built as a static library the tests link.
- T2.2 **SH4 single-step vectors** (SingleStepTests/sh4, pulled in from nullDC `vendor/sh4-tests`): interpreter, and the
  dynarec on the same vectors. Their FPU results were generated on a host FPU, so they are the oracle for `fast` only.
- T2.3 **ARM7**: `arm7di-tests-dreamcast` (45 jobs) + wren7's console suites (qemu_diff 4000 cases, mulsem 1024, bringup),
  then the timing suite for the timed tiers.
- T2.4 **FPU golden vectors**: the console dumps (about 8.6 MB) + differential tests against the vendored references.
- T2.5 **AICA**: a devcast back-end for caique's case API (85 cases) and the gated stream replays (start with ~7 MB).
- T2.6 **Debugger contract** against a headless emulator. T2.7 **System smoke**: ELFs under reios with serial assertions.

### Track 3 — SH4 `cycle-similar` (JIT) + exact FPU

Reports: `devcast-sh4-timing.md`, `shrike4-timing-model.md`, `devcast-fpu.md`, `shrike4-fpu-refs.md`.
- T3.0 Timing-mode plumbing: the setting, a shared cycle counter in `Sh4Context`, `UpdateSystem(elapsed)`, `dc.timing` in the
  debugger. `fast` stays bit-for-bit what it is today.
- T3.1 Timing table **generated** from shrike4's control ROM (`rtl/timing-proxy/tp_timing.svh` + `isa.py` + `gen_decode.py`);
  generator and its inputs vendored into devcast; TRAPA/SLEEP/illegal rows added by hand from rtl/v1.
- T3.2 Block-static ready-time scoreboard evaluated at block decode (pairing with the dependency rule, integer latencies,
  FPU/MAC columns; 99.96 % of the measured sweep points exact), exit costs per branch kind, a small run-time carry for long
  units (divider, multiplier, last load). The interpreter charges through the same model and is the reference for the JIT.
- T3.3 Fixed costs outside the pipeline: exception/interrupt entry, uncached and device accesses, P4 classes.
- T3.3b **Cycle-similar has real caches** (user, 2026-10-08; replaces the earlier "not simulated in this tier"):
  - **Real contents, not only tags.** The instruction cache (8 KiB, direct mapped, 256 lines of 32 bytes) and the operand
    cache (16 KiB, direct mapped, 512 lines) hold data. Fetches and operand accesses in cached areas with the cache enabled
    are served from the lines; a miss fills a line from memory (writing a dirty victim back first), a store goes to the line
    and to memory at once or at write-back according to the write-through / copy-back setting of its area, and the cache
    instructions (PREF, OCBI, OCBP, OCBWB, MOVCA.L), the index modes, cache-as-RAM and the store queues do what the chip
    does. Stale lines are therefore visible to the guest exactly as on the console, and devices and DMA see memory, not
    the cache — so the write-back paths have to be exact. Reference: rtl/v1's cache specification and model
    (`spec/caches-sq.md`, `tb/sh4_cache.h`) and the console measurements behind them; costs as measured (miss, write-back,
    uncached access).
  - **JIT blocks never cross a 32-byte line boundary** (at most 16 instructions; what a delayed branch in the last slot of
    a line does with its delay slot in the next line is a design point: the slot's fetch is an access to that next line).
  - **Translated code is validated when the instruction cache loads a line**: a block belongs to (line address, the 32
    bytes it was translated from); on a line fill the loaded bytes are compared with what the cached translation was made
    from, and it is reused or retranslated. Self-modifying code thus takes effect exactly when the chip would see it (after
    the line is invalidated or evicted and loaded again), and this tier needs no page protection or host fault handling for
    it — which is also what a wasm32 JIT needs. `fast` keeps today's scheme.
  - **Code executed from uncached regions** (the P2 mirror, or any area while the instruction cache is disabled) has no line
    load to validate at, so it gets **full SMC checking** — the bytes a translation was made from are compared on every
    block entry — and **the cycles of its instruction fetches are charged**: every fetch is a bus access with the measured
    uncached-read cost (rtl/v1 `spec/bsc-memmap.md`, `spec/caches-sq.md`; how many instructions one fetch access covers and
    what the fetch queue does at a branch come from the same specification).
  - Consequences to design for: more and shorter blocks (speed cost of the tier, to be measured early); the debugger's
    cache table panels (track 1 M4) show real lines; the state hash and the test suites cover the cache arrays; breakpoints
    keep working per block (the refund rule is per block already); cycle-accurate reuses the same cache model under the
    pipeline model.
- T3.4 **Exact FPU** from `cycle-similar` up: FIPR/FTRV/FSCA/FSRRA from the bit-exact header; scalar ops = host arithmetic
  with the SH4 fix-ups on the fast path (NaN pattern, DN=1 flush of operands and results, fused FMAC, FTRC saturation) and the
  soft-float reference on the slow path (DN=0, enabled traps); FPSCR cause/flag and traps. JIT blocks keyed on PR/SZ(/DN).
- T3.5 Fixes that go into every tier: FPSCR reset value (0x00040001), FPSCR write mask, FTRC saturation, FIPR with PR=1,
  reserved RM, interpreter/JIT agreement on FIPR/FTRV/FMAC.
- Gate: sweep replay (`vectors.bin`) through the C++ model >= the Python port's figures; console FPU vectors 0 mismatches;
  interpreter == JIT on cycle counts over the test ELFs; speed measured against the 557-819 % fast baseline.

### Track 4 — SH4 `cycle-accurate`

- An event-driven C++ port of the timing-proxy pipeline as a new SH4 backend (work per instruction, not per clock): GPR/FR
  ready times, pairing, the D sequencer, fetch queue, unit holds. Gate: every record of the sweep on all six metrics, i.e. the
  RTL's own result (all but the six OCBI records).
- Layers the proxy does not have, from rtl/v1's specs: instruction/operand caches, store queues, uncached and device waits,
  refresh, exception entry. Scheduler events delivered at their exact cycle.
- Verilated rtl/v1 (about 1/800 of real time) stays an offline oracle on traces of real code.

### Track 5 — AICA tiers

Reports: `devcast-aica-arm7.md`, `caique-for-devcast.md`.
- **T5.0 — DONE, merged: one DSP core.** `libswirl/hw/aica/dsp_core` matches the caique model after every step
  (independent driver: 511 M steps, 0 mismatches), has a C interface and a reproducible emscripten build tested equal to
  native, runs about 40x real time. It is wired into the emulator's AICA and the playground runs it, by the M3 round
  (`M3-ROUND.md`): W1 made it the emulator's DSP (the old interpreter and the arm64 DSP recompiler deleted; the CPUs'
  accesses routed to it; the sound line's `dsp` field), W2 removed the batch path, W3 clocked the AICA whatever ARMRST
  says, F4b put its emscripten build behind the devtools playground (the Rust crate removed), and W2's
  `aica.dsp.wasm.devtools` holds the UI's module equal to native. Original scope: A standalone C++ AICA DSP that matches caique (derived from the
  sample model's DSP: per-step execution with persistent state, the shared read latch, MRD/MWT/IWT step distances, the NOFL
  float format, ring buffer, MEMS/MIXS/TEMP/COEF/MADRS/MPRO/EFREG), with no dependency on the rest of the emulator. It is the
  single source for: (a) devcast's DSP in every tier (replaces the MAME-derived interpreter; the existing DSP JITs are ported
  to its semantics or dropped), (b) the debugger's DSP single-step / breakpoints / disassembly (track 1 M3), and (c) the
  **devtools DSP playground, built from the same C++ with emscripten** — the Rust `crates/aica-dsp` and the wasm-pack
  dependency are removed (user, 2026-10-05). Gate: caique's DSP cases against their console outputs (dsp_basic, dsp_mem*,
  dsp_wslot, dsp_unpack, dsp_pack, dsp_coll), identical results native and under wasm.
- The DSP is always on (user, 2026-10-08): no "Enable DSP" setting and no DSP-less batch path in any tier.
- `fast` fixes (order from the report): DSP float pack/unpack; effective-rate formula; slot filter + filter envelope; pitch
  LFO / ALFO depth; ADPCM loop-start state; envelope shape and key-on; MCIPD bit 10, monitor values; noise LFSR; `SA`
  masked to the RAM size; DSP on by default once it is right. ("Timers/interrupts not gated by the ARM" is done: W3 of the
  M3 round clocks the whole time step — the samples, the DSP, the timers and the interrupt lines — whatever ARMRST says.)
- `cycle-similar`: caique's sample model (bit-exact per sample, 3.5-8x real time) behind an adapter (register access merge,
  RAM pointer, `step()` per sample, interrupts, CDDA in), register writes stamped with their position inside the sample.
- `cycle-accurate` (plan): the clock-level C++ cycle model as the engine (about 1x real time today; needs its own thread or
  optimisation), SH4 wave-RAM accesses routed through its port; Verilated rtl/v1 as the offline reference via `gate_cycle.sh`.

### Track 6 — ARM7 tiers

Report: `wren7-for-devcast.md`.
- `fast` fixes (console-backed first): SWI/undefined return address, ROR by zero register amount, MULS/MLAS flags and
  Rd == Rm, reset CPSR 0xD3, STR of R15, interrupt level + acknowledge; then the measured per-instruction MCLK table in place
  of the GBA-derived ticks, 512 MCLK per sample.
- `cycle-similar`: per-instruction cost function evaluated before execution (within 0.74 % of the console on every kernel
  with the sound generator idle), FIQ entry cost, register-access cost; optional 128-step busy mask for slot contention.
- `cycle-accurate`: **plan only** (the RTL is a separate session): wren7's bus-cycle model on the clock-level AICA's ARM port,
  the future RTL core behind the same port; acceptance = cycle-identical on the 19 console suites.

### Track 7 — custom BIOS (queued last)

Reports: `reios-reconstruction.md`, `bios-rom-requirements.md`.
A replacement boot ROM as SH4 guest code: reset/boot path, the system-call set as freestanding C/C++, a basic 3D menu (play /
files / settings), and a VMU manager (list, icons, copy, delete, format). Host-side unit tests for the pure logic (syscall
dispatch, flash partitions, VMU filesystem, font tables) plus emulator-run tests. No provenance names in code or comments.

**Approach (user, 2026-10-05): clean-room, starting from a fresh decompile of `syscalls.bin`.**
`shrike4-rtl/dreamcast-data/syscalls.bin` (32 KiB, the RAM-resident syscall area 0x8C000000-0x8C007FFF after a retail boot) is
the source of truth for behaviour. Two sides with a wall between them:
- **Analysis side** (may read `syscalls.bin`, its disassembly, and for cross-checking the earlier reconstruction in
  `dreamster/libswirl2/libswirl/reios/reios_syscalls.cpp` — a 22k-line block-by-block transliteration with quoted listings):
  writes a **behavioural specification** per vector / function / GD command — calling convention, guest-memory layouts, state
  machines, return and error values, timing-visible behaviour — in prose and tables. No instruction listings, no pseudo-code
  that mirrors the original's structure, no byte ranges. A separate reviewer checks each spec chapter for copied expression
  before it crosses the wall.
- **Implementation side** (reads only the specification, public documentation such as KOS headers, and devcast's own code):
  writes the ROM. It never opens `syscalls.bin`, a disassembly of it, either sibling tree's `reios/`, or the patch headers.
  Questions go back to the analysis side as spec change requests.
- **Verification**: black-box differential tests — a guest test program exercising every vector/function, run against the new
  ROM and, locally only, against the retail image; observable results compared. The retail image and `syscalls.bin` are never
  committed, embedded, or made a build input.
The orchestrating session has seen fragments of the earlier reconstruction and therefore stays on the analysis/coordination
side: it does not author ROM code and passes only the reviewed specification to implementers.

What is ready: the toolchain (sh-elf-gcc 13.2, KOS, mkdcdisc) builds a freestanding ROM at 0xA0000000 that boots in devcast
with SCIF output; a host-side ISS pattern for unit-testing guest syscall code; a ROM layout (literal-free reset stub that copies
to RAM, resident image at 0x8C000000, font table at ROM offset 0x100020). Prerequisites in the emulator: a third BIOS option
(custom ROM path or embedded image, flash optional/auto-formatted), scripted input + a guest exit hook for menu tests, and the
`_vmem_read_const` fix (the x64 JIT returns 0xDEADC0D3 for every PC-relative literal executed in place from ROM).
Open scope questions: emulator only or real hardware too (SDRAM init, G1 verification, drive unlock); which font fills the
536,496-byte table; what defines the retail menu's behaviour for acceptance.

### Track 8 — wasm32 build with a wasm32 JIT (future; direction set by the user 2026-10-08)

The emulator built for wasm32 (the devtools already have the matching transport: the `build:wasm` UI talks over a
BroadcastChannel), first on the interpreter, then with a JIT that emits WebAssembly. Not started; nothing is scheduled.
What it asks of today's work is in the ground rules (32-bit-clean code, the generic host path kept alive) and: the SH4
timing model and the breakpoint mechanism are specified per backend through the `ngen` feature flags, so a wasm32 backend
can join without changing the contract.

### Track 9 — wgpu renderer (future; direction set by the user 2026-10-08)

Rendering is eventually replaced by a wgpu backend whose compute shaders re-implement the reference rasteriser (refsw /
the polly2 RTL pipeline) — one renderer for native and wasm. Not started. Until then: refsw2 stays the reference, the `none`
renderer the benchmark path, and no work goes into the GL renderers beyond keeping them building. The debugger's Holly
inspectors (track 1 M4) are built on the refsw log format, which that renderer is expected to keep producing.

### Track 10 — one web UI; the imgui GUI goes (direction set by the user 2026-10-08)

The emulator's own UI — configuration (general, video, audio, controls, advanced), loading a disc or ELF by path, start /
stop / reset, notifications, about — moves into the web UI, merged with the devtools into one cohesive application. **Not
carried over** (user, 2026-10-08): the **game list** (this is a developer tool; content is given on the command line or by
path), and **save / load states**, which are removed for now and may come back later (their buttons go with imgui; no
protocol method; the debugger's "guest state replaced" hook stays in the design for when they return). The store / social
remnants of the old GUI (buy, stream, download, report a bug, send logs) are dropped without replacement; `libswirl/gui` (about 3,400 lines of imgui code), `deps/imgui` and the imgui hooks in the renderers and
platform files are removed. The emulator window then only presents frames and takes input.
What it needs:
- **Protocol**: a settings model the server describes (sections, keys, types, ranges, current values, which need a restart)
  with get / set / save; machine lifecycle (load by path, start, stop, reset, exit); input mapping with live capture;
  notifications. These extend protocol v3 the same way the debugger methods are specified, and the
  contract runner gates them.
- **Emulator**: the server stops being debugger-only (it is the control surface, so it must work with no game loaded and be
  enabled in normal use; the Host / Origin rules of the transport matter more once it can change settings and load files); the
  host-UI role that the imgui thread plays today in the run-control design (menu, pause, exit, load state) is taken by
  requests from the web UI — the lifetime states and stop reasons of track 1 already separate "host stop" from "debugger
  pause", so this replaces callers, not the model.
- **UI**: one application with the machine side (load, settings, controls) next to the debug workspaces, same docking and
  theme; no page of it depends on the debugger being in use.
- **Settings inventory of the old GUI** (what each page holds today; decide per item whether it survives):
  General — language, broadcast, region, cable, content directories (go with the game list). Video — renderer choice and a
  set of options that only the GL / HLE renderers honour (clipping, shadows, fog, widescreen, frame skipping, render-to-texture
  copy and upscaling, texture upscaling, custom textures), FPS counter, VMU overlay, scaling. Audio — disable sound, enable DSP
  (the per-sample AICA path), limit FPS, backend. Controls — Dreamcast device per port, physical device mapping, haptic, mouse
  sensitivity. Advanced — SH4 dynarec or interpreter; dynarec options (safe mode = no optimised integer division, unstable
  optimisations, idle skip, self-modifying-code check level); ARM7 dynarec or interpreter; DSP dynarec or interpreter; serial
  console to stdout; virtual serial port (a PTY for dc-load); dump textures. In nullDC 3 the engine choices become the
  per-processor engine + timing tier selectors of tracks 3-6.
- **Decided (user, 2026-10-08):**
  - **The DSP is always enabled.** The "Enable DSP" setting (`aica.NoBatch`) goes; so does the 32-sample batch path that
    runs without the DSP — the AICA always runs per sample with the DSP (track 5: on the one DSP core). With the batch
    path gone, the debugger's "per-sample forcing" (design 2.5.5) is no longer needed.
  - **"Limit FPS" is renamed "Limit Speed" and moves to General.**
  - **"Advanced" is split into "Accuracy" and "Developer".** Accuracy: the timing tier per processor (SH4, ARM7, AICA:
    fast / cycle-similar / cycle-accurate). The speed-for-correctness JIT options — idle skip, self-modifying-code check
    level, safe mode, unstable optimisations — exist **only in `fast`**; cycle-similar and cycle-accurate have no options
    (no idle skip, full checks, fixed behaviour). **There is no interpreter option**: the tier picks the engine (the JIT
    for fast and cycle-similar, the pipeline-model backend for cycle-accurate). The interpreter stays in the tree as the
    reference of the test suites, as the breakpoint fallback of a JIT backend that has no native checks yet, and as the
    starting point of new host ports; its config key remains for tests and is not shown.
    Developer: serial console to stdout, virtual serial port (PTY for dc-load), the debugger / control server's own options
    (port, bind address, start paused), and later the dump and trace switches of the debuggers.
  - **The GL options are removed** (force GLES2 context, synchronous rendering, clipping, shadows, fog, widescreen, frame
    skipping, render-to-texture copy and upscaling, texture upscaling and its limits, custom textures, texture dumping): no
    settings, no UI; the renderers run with fixed defaults. Debugging and dumping of the graphics side will come from the
    PVR TA and CORE debuggers, which are still to be planned (track 1 M4 is their placeholder).
Order: after track 1 M1 (the server, core and run control exist), before or alongside M2-M4; the imgui removal is the last
step, once every setting and action has its web counterpart (gate: a checklist of every imgui page and control mapped to
its web counterpart, each exercised by an e2e test against a headless emulator).

## Rebrand: the executable and its directories (direction set by the user 2026-10-12)

"on the rebrand from reicast/devcast, the main executable name should be nulldc3 and it should by default operate in
portable mode, unless instructed to use the shared directories for cfg/data/bios/vmus and such".

As the tree is today (read 2026-10-12): the target name is one line (`set(TNAME devcast)` in `CMakeLists.txt`), and
`devcast` is named by 9 test scripts / CMake files and 13 documents. Directories: on Linux shared by default
(`~/.reicast` when it exists, else `$XDG_CONFIG_HOME/reicast` and `$XDG_DATA_HOME/reicast`, with `/etc/reicast`,
`/usr/share/reicast` and the XDG system lists behind them; `libswirl/linux-dist/main.cpp`); `-portable` exists and means
the current working directory, not the executable's (`cfg/cl.cpp`); on Windows the executable's directory already
(`windows/winmain.cpp`); on macOS `~/.reicast` plus the bundle's resources (`AppDelegate.mm`). The system tests isolate a
run by fresh `HOME` / `XDG_CONFIG_HOME` / `XDG_DATA_HOME` directories below their work directory.

**Package `N1`, the first package behind M2G** (the M2 briefs and gates name `devcast`; it is not changed under them),
cut like the others (readers, a cut with its decisions, implementation in a worktree, independent verification):
- The executable is `nulldc3` on the three desktop systems (target, bundle, the names in tests, scripts and documents).
- **Portable by default:** configuration, data, BIOS / flash, VMUs, mappings and whatever else is written live in the
  directory of the executable, on every system (decided with the user, 2026-10-12).
- **`--portable <dir>` names another directory** (user, 2026-10-12: "without -portable pick the exe, with -portable <dir>
  pick dir"). The switch takes a directory now; today it takes none and means the current one. What `-portable` as the
  last word of a command line, or in front of another switch or of a game, means is a decision of the cut (proposed: an
  error that says the switch takes a directory, since a game path would otherwise be taken for it).
- **Shared directories when instructed:** a command-line switch (working name `--shared`). **No build option** (user,
  2026-10-12). Proposed, to be fixed in the cut: the shared directories are named `nulldc3` (XDG on Linux, the per-user
  application directory on Windows and macOS); the `reicast` directories are not read. What a portable start does when its
  directory cannot be written is a decision of the cut (proposed: an error that names the switches, not a silent change of
  place).
- **Switches are written with two dashes** (user, 2026-10-12: "while we're changing the options, let's switch to unix
  style -- instead of -"): `--config`, `--portable <dir>`, `--shared`, `--help`, and on Windows `--console` and `--log`
  (`windows/winmain.cpp`). As the tree is today the parser takes both forms of `config`, `portable` and `help`
  (`cfg/cl.cpp`), prints and documents the one-dash form, and the one-dash `-config` is what the tests, the scripts and 13
  files of documents and CMake write. **Decided (user, 2026-10-12, "both proposals make sense"):** the help text, the
  documents, the tests and the scripts write two dashes only, and a one-dash switch is an error that names the two-dash
  form (a clean break with the new executable name; a word that begins with one dash is then never taken for a game path);
  the list of keys behind `--config` keeps its form (`section:key=value , section:key=value`, with the blanks around the
  comma).
- The tests name their directory with `--portable <dir>`: a run of the suite writes nothing beside the binary under test.
- It goes with the items of the preview offered on 2026-10-11 (a quick-start README, a build that embeds the UI) and with
  open question Q13 (the name of the config section). The history scrub is not a package item: it rewrites the trees of
  193 emulator commits and the UI's committed DSP wasm (a home path inside the binary) and is my last step before the
  first push, which the user put after N1 (2026-10-15; `resume/RESUME.md`, "Before anything is pushed").
- **Repository names (user, 2026-10-15):** the emulator's GitHub repository is `skmp/nulldc3` (renamed from
  `skmp/devcast`); the UI stays `skmp/dreamcast-devtools` (a `skmp/nulldc3-devtools` was created and then dropped by the
  user the same day: "i prefer to keep dreamcast-devtools"), so `../dreamcast-devtools` stays the checkout the build and
  the tests look for. The committer of every commit is the git identity configured on the development machine (user,
  same day); no document names it.
- **`dreamcast-devtools` becomes a submodule of the emulator at `devtools/`** (user, 2026-10-15: "as part of the
  rebrand, can you add dreamcast-devtools as a submodule?"; answers Q14). The UI bundle and the tests already look at
  `<tree>/devtools` first (`dbg_webui.cmake`, `tests/CMakeLists.txt`, `tests/debugger/net/CMakeLists.txt`);
  `tests/debugger/{disasm,regs}/CMakeLists.txt` look only at `../dreamcast-devtools` and gain the `devtools` candidate in
  front of it. The gitlink becomes the UI pin: each emulator commit names the UI commit its gates ran with (today the pin
  is the worktree `agents/nulldc3/wt/ui-pin`). Proposed, decisions of the cut: the URL absolute,
  `https://github.com/skmp/dreamcast-devtools.git`, as the two test-vector submodules write theirs (a fork of the emulator
  and a clone without ssh keys still get it); no `shallow`, no `branch`; the build runs no npm (`dist-native` is built,
  not committed; configure says whether a bundle was found, as today; the quick-start README gives `git submodule update
  --init devtools`, `npm ci && npm run build:native` in it, then the emulator build); a gate check that the UI checkout
  the tests are given (`NULLDC_DEVTOOLS_DIR`) is at the gitlink's commit. Local work: worktrees do not clone the UI from
  GitHub (its branch is not pushed): `submodule.devtools.url` in the local config names the local checkout, not
  committed, and gates keep giving a checkout with `node_modules`. **Push order:** the gitlink names a commit of the UI's
  `skmp/nulldc3` branch, so the UI branch has to be on GitHub before the emulator branch, or a clone's `git submodule
  update` fails.
- **The UI is called "Dreamcast DevTools"** (user, 2026-10-15: "can you rebrand the devtools to say "Dreamcast DevTools"
  instead of nulldc?"). As the UI is today (read 2026-10-15; 100 lines in 42 files say nullDC in some form), three kinds,
  proposed for the cut:
  - **Its own name becomes "Dreamcast DevTools":** `index.html` (`<title>`, `og:title`, `twitter:title`), the About
    dialog ("About nullDC DevTools"), the home page ("Welcome to the nullDC DevTools"), the status bar ("nullDC Debugger
    <version>" becomes "Dreamcast DevTools <version>"), the documentation text, `README.md` and `IDEA.md` titles,
    `config.clientName` and the client string `debuggerClient.ts` sends in `debugger.describe`, the contract runner's
    client name ("Dreamcast DevTools contract runner"), `package.json` `name` (`dreamcast-devtools`, the repository's
    name), the plugin name in `scripts/bundle-info.ts`, and the unit and e2e tests that assert those strings. The emulator
    checks no client name (read the same day).
  - **Where it names the emulator it keeps nullDC:** "the web debugger UI for nullDC 3.0.0, the Dreamcast emulator", the
    mock emulator's `nullDC (mock)` and its `BUILD` register, the describe fixtures. **The target label reads the name
    the emulator reports** (user, 2026-10-15: "it should read the emulator name @ host"): `<emulator.name> @ <host>`
    from `debugger.describe`, not the fixed `nullDC @ <host>` of today.
  - **Identifiers no one reads stay:** the localStorage keys `nulldc-debugger-*` and `nulldc-theme-mode` (a rename would
    drop everyone's saved layouts and sources), the `nulldc:crash-test` event, environment
    variable names, and `fly.toml`'s app name (a deployment, the user's; removed with the fly.io deployment by devtools 0bbd8d1, 2026-10-16, at the owner's request, together with the Dockerfile; CI runs on pull requests only). **The in-browser transport's channels become
    `dreamcast-devtools-announce` and `dreamcast-devtools-<id>`** (user, 2026-10-15; no emulator build speaks the old
    `nulldc-debugger-*` names yet, so they are not kept).
  - On the emulator side the configure messages "nullDC devtools UI: ..." (`dbg_webui.cmake`, `dbg_webui_embed.cmake`,
    their test `tests/debugger/net/embed_check/run.cmake`) and the text of the page served without a bundle
    (`dbg_webui.cpp`) name the UI "Dreamcast DevTools".
  - **Open for the user:** `public/nulldc_logo.png` (the favicon and the `og:image`) is the nullDC snail with the word
    "nullDC" on it. Kept until the user gives another image or says otherwise.
  N1 therefore touches both repositories and is run as two packages, like EXe / EXu: N1e (emulator, the submodule pinned
  at N1u's result) behind N1u (UI).
  **N1u DONE 2026-10-16:** devtools `skmp/nulldc3` = 2eb0b9e (the UI pin moved there). N1e started the same day.
  **N1e DONE 2026-10-16** (`resume/m2-n1e.final-result.json`; interrupted overnight and resumed; two verifications):
  merged as 00f9295c (21d1d183), plus the integrator's f986304c (mkdir's reason kept in the "cannot be made" message;
  `portable_unmakable_below` three levels deep; `devtools.lookup` layout `both_incomplete`; each shown by a mutant).
  devcast `skmp/nulldc3` = f986304c, **278 ctest targets** (+ `cfg.log_file`, `devtools.lookup`, `devtools.commit_check`;
  the tests project has to be configured again: `system/work.link` is made at configure time). **N1 IS COMPLETE.**
  Not built here (no MSVC, no Xcode): the Windows help/error message box (`output_is_kept`), `cl_LogToFile` under UCRT,
  `narrow_path` with a UTF-8 code page, the macOS `AppDelegate` filter and scheme (all built and run on their systems in
  the platform round: W64a, MACa). `submodule.devtools.url` is set in the
  local config of the devcast repository to the UI checkout (not committed); a local `git submodule update` needs
  `-c protocol.file.allow=always`. What still says devcast: the README's history sentence, the `docs/nulldc3/` records,
  the internal `DEVCAST_DIR` CMake variables; reicast: the tool `reicast-joyconfig`, the Lua module, the tree's
  directory names.
  **The rest of the rebrand is the platform round's, done** (the owner's decision of 2026-10-19, `PLATFORM-ROUND.md`
  1.3 item 1; package RB, merged 2026-10-20 as `60bce5b7`, `PLATFORM-ROUND.md` I.2): the platform shell `reicast/` is
  `nulldc3/` (`nulldc3/linux`, `nulldc3/windows`, `nulldc3/apple`, `nulldc3/cmake`; a commit of pure moves, every
  file at R100, so that `git log --follow` reads through it), and what still said reicast at N1 is renamed:
  `reicast-joyconfig` is `nulldc3-joyconfig`, the Lua module is `nulldc3` (no `reicast` alias), the HTTP User-Agent
  `nulldc3`, `DEVCAST_DIR` is `nulldc_root_path` in the four test projects, `reicast_init` / `reicast_term` are
  `nulldc_init` / `nulldc_term` (header revision 8), `ReicastUI_impl` is `ImguiGUI`, `REICAST_VERSION` is
  `NULLDC_VERSION`, the save-state macros `NULLDC_S` / `NULLDC_US`; the imgui button "Report a game bug" (a dead
  reicast site) is gone. Reicast's dead files went (the Xcode project, `appveyor.yml` with reicast's old AWS access
  key id, which stays in the public history, `azure-pipelines.yml`, `wercker.yml`, `versioning/`, reicast's
  CONTRIBUTING and release policy, `gdtool/`, the reicast icons). "reicast" stays only as history: `LICENSE`,
  `nulldc3/LICENSE` (reicast's BSD shell licence), the licence blocks, credits, provenance notes, the macOS file
  headers, `docs/nulldc3/**`; the test `rebrand.names` (`git grep -i reicast` minus `tests/rebrand/allowed.txt`) holds
  it, and every package that moves or rewraps a piece of that history edits its row (PL-D79). `reidbg/` stays (the
  client of the built `refsw-dbg` server; relabelled "nullDC host"). The icons came with it (package IC: the approved
  snail in the Windows `.ico`, the macOS `.icns` and the X11 window's own icon, generated from the committed PNGs by
  `nulldc3/icon/make_icons.py`, held by `icons.generated`).
  **Pushed 2026-10-16** at the owner's word (devtools ed4da47 to skmp/dreamcast-devtools, devcast 54f21352 to
  skmp/nulldc3; history as it was). Since 54f21352 the submodule's URL is relative (`../dreamcast-devtools.git`, the
  owner's wish) and pins ed4da47 (the DSP wasm without the builder's cargo path).

## Order of work

1. Track 1 M0 + M1 (now), then M2-M4.
2. Track 2 T2.0-T2.2 as soon as M1 is in (the debugger contract test needs the headless mode).
3. Track 3 (T3.5 fixes first, then T3.0-T3.2, FPU T3.4 in parallel).
4. Tracks 5 and 6 `fast` fixes and `cycle-similar` (each gated by track 2's ARM7/AICA tests).
5. Track 4, then the cycle-accurate plans of 5/6, then track 7.

## Decisions made (say if any should change)

- Debugger server on by default, localhost only, with an Origin check; port 55543.
- JSON: nlohmann/json single header vendored into `libswirl/deps`.
- The emscripten module of the DSP core is committed in the devtools repo (`src/wasm/aica-dsp-core/`, with its
  provenance and pinned emscripten version), so the UI builds without emcc (and, since F4b, without Rust).
- Breakpoints and single-steps are native to the active SH4 backend (compiled-in checks on the x64 JIT, temporary
  one-instruction blocks for steps): "what you debug is what runs". Prototyped on `skmp/nulldc3-proto-bp` with zero time
  skew; other JIT backends use a block-charging interpreter fallback with a UI notice until they implement the check.

## Needs the user

- A licence line for the shrike4 FPU headers, wren7 and the console dumps before they are copied into devcast (caique is MIT).
- ~~Whether `dreamcast-devtools` becomes a submodule of devcast~~ answered 2026-10-15: yes, at `devtools/`, in package N1 (section "Rebrand").
- The platform round's questions (`PLATFORM-ROUND.md` 8.2; work proceeds on each recommendation until the owner
  answers): **PL-Q1**, test data in CI (the KallistiOS ELFs and the RTL checkouts; recommended: not in this round);
  **PL-Q2**, a macOS icon with Apple's margin (recommended: ship the approved art for 3.0.0-pre); **PL-Q3**, the
  runner's own paths in CI logs (recommended: accept them as GitHub's, not the owner's; both PR texts say so).
- The platform round's end (section 7 of the cut): the owner merges the UI's PR, then the emulator's, and gives one go
  for the dispatch checks and the tags `v3.0.0-pre`.
- Needs the console (unmeasured, behaviour chosen and marked in the code until then): FLOAT; scalar ops with DN=1 + RZ (the
  reset mode); FTRC boundaries; reserved RM values; FDIV/FSQRT rounding sweeps; ARM7 STR-of-R15 / base-in-list / TEQP probes
  (already written in the runner's format).
