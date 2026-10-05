# nullDC 3.0.0 — plan

devcast (`dreamster/devcast`) becomes **nullDC 3.0.0**: the development emulator that the web devtools attach to, with a test
suite and three timing tiers per processor, backed by the console measurements in shrike4 / caique / wren7.

This file is the work list. Each track has milestones, a gate, and its status. Evidence for every statement is in the reader
reports under `agents/nulldc3/understand/` (one per subsystem) — they are the maps; read the relevant one before touching a
track.

## Ground rules

- Repos: emulator `dreamster/devcast`, UI `dreamster/dreamcast-devtools`; work on local branch `skmp/nulldc3` in each;
  commits are made as work lands, nothing is pushed. `~/projects/devcast` is a stale checkout — do not use it.
- New surfaces say "nullDC" / "3.0.0". The tree is not mass-renamed (config/data dirs stay `reicast` until the user renames).
- Scratch, build dirs, reports: `dreamster/agents/nulldc3/` only.
- Console access: none in this work unless the user asks; unmeasured behaviour is listed under "Needs the console".
- **Performance numbers** are taken with the hardware-accelerated renderers or the `none` renderer, never the reference
  software renderers (those make the run render-bound: 34-44 % of real time with refsw2). Both live in the
  `-DHAS_TA_LLE=OFF` build. For GL on this WSL2 machine run with `GALLIUM_DRIVER=d3d12` (default GLX is llvmpipe; with it:
  D3D12 on the RTX 2060) and check `glxinfo -B` first. Every figure states its renderer. Existing `none` baseline (2ndmix):
  JIT 557-819 %, interpreter ~350 %. The TA_HLE build needs the `refsw2_do_dump` link fix (definition is inside the TA_LLE
  guard) and `none` should register in the default build too — T2.0 items.
- Timing tiers are **run-time** settings, one per processor, in a new settings group:
  `Timing.SH4`, `Timing.AICA`, `Timing.ARM7` = `fast` | `cycle-similar` | `cycle-accurate` (+ one preset selector in the GUI).
  `fast` keeps today's speed tricks but with its functional bugs fixed. A tier change flushes the affected code caches.
- "Exact" behaviour (FPU values, AICA rules, ARM7 semantics) is tied to the tier: bug fixes go into every tier; behaviour that
  costs speed (bit-exact vector FPU, per-sample AICA with the measured rules) is on from `cycle-similar` up.

## Tracks, in order

### Track 1 — devtools integration  (IN PROGRESS)

Design: `TRACK1-DESIGN.md` (v2: three adversarial reviews, 72 issues dispositioned, SH4 mechanism prototyped). Summary: a debugger server inside the emulator (HTTP + WebSocket
on one port, default `127.0.0.1:55543`, UI bundle embedded), all guest-state access on the emulator thread, protocol v3
(v2 + target metadata, register merge, writes, tables, correlated binary frames), and a contract test runner that validates
either server (mock or emulator) with the UI's own zod schemas.

| Milestone | Content | Gate |
| --- | --- | --- |
| M0 | devtools hygiene: lint/typecheck/tests/build green, real scripts, versions, docs, fresh clone works without Rust — **DONE** (13 commits, verified from a fresh clone); follow-up in progress: Playwright e2e smoke suite, error boundaries, offline Monaco | `npm run check` |
| M1 | protocol v3 + contract runner; emulator transport; SH4 debugging complete; device tree with real registers; events | contract test green on mock and emulator |
| M2 | ARM7 step/breakpoints; memory/register writes; reset; generic register panels; follow-PC; toolbar run control | contract (arm7), UI tests |
| M3 | SGC capture; DSP step/breakpoints/disassembly; playground linked to the live emulator | contract (sgc/dsp) |
| M4 | CORE inspector fed by the emulator; TA inspector; TLB/SQ/OC-RAM tables; TA/CORE event breakpoints | contract (holly) |
| M5 | SH4 simulator panel re-based on the measured model (shares the table generator of track 3) | fixture gate >= 300/305 |

### Track 2 — test suite  (T2.1-T2.3 DONE, verified, merged into `skmp/nulldc3` at d261fcc0; T2.4 FPU console suite in progress)

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
- T3.3 Fixed costs outside the pipeline: exception/interrupt entry, uncached and device accesses, P4 classes. Caches are not
  simulated in this tier (documented).
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
- **T5.0 (pulled forward, ahead of track 1 M3): one DSP core.** A standalone C++ AICA DSP that matches caique (derived from the
  sample model's DSP: per-step execution with persistent state, the shared read latch, MRD/MWT/IWT step distances, the NOFL
  float format, ring buffer, MEMS/MIXS/TEMP/COEF/MADRS/MPRO/EFREG), with no dependency on the rest of the emulator. It is the
  single source for: (a) devcast's DSP in every tier (replaces the MAME-derived interpreter; the existing DSP JITs are ported
  to its semantics or dropped), (b) the debugger's DSP single-step / breakpoints / disassembly (track 1 M3), and (c) the
  **devtools DSP playground, built from the same C++ with emscripten** — the Rust `crates/aica-dsp` and the wasm-pack
  dependency are removed (user, 2026-10-05). Gate: caique's DSP cases against their console outputs (dsp_basic, dsp_mem*,
  dsp_wslot, dsp_unpack, dsp_pack, dsp_coll), identical results native and under wasm.
- `fast` fixes (order from the report): DSP float pack/unpack; effective-rate formula; slot filter + filter envelope; pitch
  LFO / ALFO depth; ADPCM loop-start state; envelope shape and key-on; timers/interrupts not gated by the ARM, MCIPD bit 10,
  monitor values; noise LFSR; `SA` masked to the RAM size; DSP on by default once it is right.
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

## Order of work

1. Track 1 M0 + M1 (now), then M2-M4.
2. Track 2 T2.0-T2.2 as soon as M1 is in (the debugger contract test needs the headless mode).
3. Track 3 (T3.5 fixes first, then T3.0-T3.2, FPU T3.4 in parallel).
4. Tracks 5 and 6 `fast` fixes and `cycle-similar` (each gated by track 2's ARM7/AICA tests).
5. Track 4, then the cycle-accurate plans of 5/6, then track 7.

## Decisions made (say if any should change)

- Debugger server on by default, localhost only, with an Origin check; port 55543.
- JSON: nlohmann/json single header vendored into `libswirl/deps`.
- The generated DSP wasm glue is committed in the devtools repo so the UI builds without Rust.
- Breakpoints and single-steps are native to the active SH4 backend (compiled-in checks on the x64 JIT, temporary
  one-instruction blocks for steps): "what you debug is what runs". Prototyped on `skmp/nulldc3-proto-bp` with zero time
  skew; other JIT backends use a block-charging interpreter fallback with a UI notice until they implement the check.

## Needs the user

- A licence line for the shrike4 FPU headers, wren7 and the console dumps before they are copied into devcast (caique is MIT).
- Whether `dreamcast-devtools` becomes a submodule of devcast (today: auto-detected sibling directory, or a path option).
- Needs the console (unmeasured, behaviour chosen and marked in the code until then): FLOAT; scalar ops with DN=1 + RZ (the
  reset mode); FTRC boundaries; reserved RM values; FDIV/FSQRT rounding sweeps; ARM7 STR-of-R15 / base-in-list / TEQP probes
  (already written in the runner's format).
