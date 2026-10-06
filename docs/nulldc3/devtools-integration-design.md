# Track 1 — devtools integration: design

Status: v2 — reviewed; implementation contract. Supersedes DRAFT v1 (`TRACK1-DESIGN.v1.md`). Owner of this file: the
orchestrator.

Inputs: the reader reports in `agents/nulldc3/understand/` (cited, not re-derived): `devtools-protocol.md` (wire spec of
protocol v2), `devtools-mock.md`, `devtools-panels-debugger.md`, `devtools-panels-tools.md`, `devtools-build.md`,
`nulldc-rust-backend.md`, `devcast-debug-surface.md`, `devcast-build.md`; the plan, `agents/nulldc3/PLAN.md`; the three
reviews of v1 in `agents/nulldc3/review/` (`review-emulator.md`, `review-protocol.md`, `review-sound-scope.md`; every
issue is accounted for in Appendix A); and the specification of the SH4 breakpoint mechanism with its prototype,
`agents/nulldc3/review/jit-breakpoints-spec.md` (integrated as 2.3.4 to 2.3.7; results in Appendix C).

Repos and branches (local only, never pushed; commits are made as work lands):
- emulator: the `devcast` repository, integration branch `skmp/nulldc3`. One git worktree per work package
  under `agents/nulldc3/wt/<pkg>` on branch `skmp/nulldc3-<pkg>`, own out-of-tree build directory; an integrator merges.
- web UI: the `dreamcast-devtools` repository (a sibling checkout), branch `skmp/nulldc3` (frontend packages are sequential on it).

Decisions in force (project owner; everything below is written to them):
- **D1 — what you debug is what runs.** Breakpoints and single steps are native to the active SH4 backend; nothing switches
  engine when a breakpoint is armed, and scheduler time is what the active timing mode charges (2.3, 2.4). An interpreter
  fallback exists only for JIT backends that do not implement the native check yet; the UI says so when it is in use.
  The prototype made three points of D1's wording precise (the block a resume runs, the refund in `fast` timing, the cost
  of a step); they are listed in Appendix A.2.
- **D2 — one DSP.** The DSP that the debugger steps, the emulator runs and the devtools playground executes is the single
  caique-matching C++ DSP core of PLAN.md T5.0 (the playground uses its emscripten build; the Rust crate goes away).
  M3 starts after T5.0.
- **D3 — performance figures** are taken with the hardware GL renderers or the `none` renderer, never the reference
  software renderers, and state their renderer.
- **D4 — naming.** New surfaces say nullDC 3.0.0. The tree is not mass-renamed (binary `devcast`, config directory
  `reicast`, existing identifiers stay).
- **D5 — commits** are made as work lands, on local branches.
- **D6 — E1 and E2 have started** with the API in Appendix B; that API is frozen.

Terms: an **in-band error** is a successful JSON-RPC response whose result is `{"error":{"code":n,"message":"..."}}`; a
**JSON-RPC error** is a response with a top-level `error` member. Which one a method uses is fixed per method (3.5).

## 1. Goal and milestones

The web UI talks to a debugger server inside the emulator and every panel shows real state.

| Milestone | Content | Gate |
| --- | --- | --- |
| M0 | devtools hygiene: lint/typecheck/test/build green, real scripts, versions, docs; a fresh clone builds without Rust; the wasm-transport build keeps building | `npm run check` = lint + typecheck + test:run + build:native + build:wasm |
| M1 | protocol v3 (schema, client, mock module) and the contract runner. Emulator: transport and embedded UI, lifetime states and park loop, SH4 debugging (registers, memory, disassembly, run / pause / step / over / out, native code breakpoints, event breakpoints, watches, callstack, event log), the reduced M1 device tree (2.8). Frontend: endpoint selection and reconnect; disassembly follows the PC and memory refetches when `stopId` changes; per-target defaults from `targets`; results of `control.*` shown; `reason` and `notice` in the toolbar; error boundary per dock panel; device-tree panel rework; notices for unsupported targets | contract scenarios S01-S14 green against the mock and S00-S15 against the emulator; pure-module tests; in-emulator selftest exit 0, including the breakpoint and step gates B1-B14 (2.3.6); Windows gate (5.8) |
| M2 | ARM7 step / breakpoints (resumable sound burst); memory and register writes; reset; full register tables (SB, PVR, AICA common and channels, GD-ROM, maple) with the generic register panel and the panel ids `sh4-registers`, `arm7-registers`; toolbar run control with hotkeys; offline Monaco and route-level code splitting | contract S16; sound regression hashes identical before/after (5.6); UI unit tests |
| M3 (after PLAN T5.0) | SGC capture; DSP single-step / breakpoints / disassembly on the one C++ DSP core; playground runs the emscripten build of the same core and loads its program from the live emulator | contract S17; sound regression hashes with the DSP test ELF; playground results equal native results |
| M4 | Holly: CORE inspector fed by an emulator capture, TA inspector, table panels (TLB / SQ / OC-RAM), TA / CORE event-breakpoint controls in the inspectors | contract S18 |
| M5 | SH4 simulator panel re-based on the measured model (moves with tracks 3/4) | vitest fixture gate, 300/305 or better |

M0 and M1 are the first implementation round. M2..M4 follow in order; each is independently shippable.

### 1.1 Explicitly out of scope for M0..M4

| Item | Source | State after M4 |
| --- | --- | --- |
| Conditional breakpoints, hit counts | IDEA.md:6 | not in the protocol, no UI field. The native check already calls a helper (2.3.4 c), so adding them later needs no recompile |
| Run "until next interrupt / exception" | IDEA.md:6 | only as an event breakpoint (`dc.sh4.interrupt`, `dc.sh4.exception`); no command; the stop is up to one slice late (2.2.7) |
| CORE "step to next primitive / tile", TA "step until next vertex / polygon" | IDEA.md:6 | list / render granularity only (events of 2.6) |
| "Step a sample in AICA" | IDEA.md:6 | no command; a DSP breakpoint on step 0 is the nearest thing (M3) |
| DMA step / run | IDEA.md:6 | transfers are atomic; start / end events only |
| Memory / disassembly failing on TLB misses | IDEA.md:4 | no address translation: `WITH_MMU` builds are read through the same fixed map as `NO_MMU` builds (the default); `validity` is always `ok` |
| ICACHE / OCACHE tables | IDEA.md:4, panels `icache-contents`, `ocache-contents` | the emulator has no cache model; `state.getTable` returns no rows and a `note` |
| TLB table | IDEA.md:4 | meaningful only in `WITH_MMU` builds; otherwise a `note` |
| "Hear a section of audio", per-stage waveforms | IDEA.md:4 | playback not planned |
| Threads list | IDEA.md:9 | not planned |
| Debugger state surviving an emulator restart | IDEA.md:8 | breakpoints, watches and categories are in memory only |
| wasm / cross-tab transport as a server of this emulator | IDEA.md:2 | not applicable; `build:wasm` keeps building and one vitest covers `BroadcastChannelTransport` |
| Native SH4 breakpoints in the arm32, arm64, x86 and cpp JIT backends | D1 | interpreter fallback with a notice (2.3.5); each backend leaves it when it implements the items of 2.3.5 |
| ARM7 native breakpoints on hosts with the ARM7 JIT | D1 | interpreter fallback with a notice (2.5.3) |
| SH4 simulator panel | route `/tool/sh4-sim` | M5 |

## 2. Architecture (emulator side)

New directory `libswirl/debugger/`. Language: **C++20** (`-std=c++20`, extensions off, plus `-fno-operator-names
-fpermissive` on GCC). Debugger code must also be valid without `-fpermissive` semantics, because MSVC `/std:c++20` implies
`/permissive-`.

Build: `libswirl/debugger/debugger.cmake` (owned by E1) is `include()`d once from `CMakeLists.txt` and contains everything
else: the source glob, the option, the UI embed step (2.11; its scripts are in `libswirl/debugger/cmake/`), `ws2_32` on
Windows and the definition `NULLDC_DEBUGGER=1`. The protocol number has one source, `dbg::kProtocolVersion` in
`dbg_webui.h`; the embed check reads it from that line. `cmake_minimum_required` stays 3.10; embedding the UI needs
CMake 3.19 (`string(JSON)`, `file(ARCHIVE_CREATE)`), and an older CMake builds the debugger without the bundle. Option
`NULLDC_DEBUGGER`: default ON; OFF for `LIBRETRO_CORE`, `TARGET_NSW` and `TARGET_PS4` (the two targets without threads,
and the library build, which must not open a listening socket unasked). When OFF, `debugger.h` provides empty inline
stubs (the SH4 breakpoint query is constant false, so no check is ever compiled) and no file of `libswirl/debugger/` is
compiled. Only the CMake build is supported; `core.mk` is not updated.

Vendored: `libswirl/deps/nlohmann/json.hpp` and `json_fwd.hpp` (nlohmann/json 3.11.3, MIT). SHA-1 for the WebSocket accept
key comes from `libswirl/deps/crypto/sha1.h`.

Rules for every debugger source file:
- `json.hpp` (7 to 9 s and about 350 MB per translation unit) is included by `dbg_core.cpp` and `dbg_main.cpp` only and by
  no header; a header that needs the type includes `json_fwd.hpp`. `json.hpp` is always included before any tree header.
- Never include `hw/sh4/sh4_core.h` (it `#define`s `r`, `pr`, `sr`, `fr`, ...; `json.hpp` after it does not compile). Use
  `p_sh4rcb->cntx.*` and `sh4_sr_GetFull/SetFull` from `sh4_if.h`, and `UpdateSR()` / `UpdateFPSCR()` /
  `sh4_host_fp_resync()` from the new `hw/sh4/sh4_dbg.h` (6, E0).
- Never call `ReadMemN/WriteMemN` with a debugger-supplied address (they spin forever or abort on several inputs;
  devcast-debug-surface.md 5.3). Use `dbg_mem`.
- The rest of the emulator includes only `debugger/debugger.h` (no JSON, no STL containers; `<atomic>`, `<cstdint>` and
  the tree's `types.h`, which every emulator file includes already and which supplies `DYNACALL` for the two helpers
  that generated code calls, 2.3.4 l).
- Enumerators and identifiers in debugger headers avoid the `<Windows.h>` macro names (`ERROR`, `IN`, `OUT`, `DELETE`,
  `small`, `near`, `far`, `interface`).
- `dbg_net.cpp` includes no tree header except `debugger/dbg_net.h` (which uses `<cstdint>` types, not `types.h`) and
  `deps/crypto/sha1.h`, uses `std::thread` / `std::mutex`, and on Windows includes `<winsock2.h>` and `<ws2tcpip.h>` as its
  first lines and calls `WSAStartup(2.2)` itself at server start.
- No debugger module caches a pointer into guest state across a lifetime change (2.2.1).

### 2.1 Modules

| File(s) | Responsibility | Package | Pure (links without the emulator) |
| --- | --- | --- | --- |
| `debugger.h` | The only header emulator code includes: `g_attn`, the stop latch, `DBG_EVENT`, lifetime hooks, the SH4 backend interface of 2.3.4 l (`dbg_sh4_*`), `dbg::Init/Term`; inline stubs when `NULLDC_DEBUGGER` is OFF | E0 | — |
| `dbg_net.h/.cpp` | HTTP/1.1 static file server and RFC 6455 WebSocket server on one listening socket, own thread (Appendix B) | E1 | yes |
| `dbg_webui.h/.cpp`, generated `dbg_webui_data.cpp` | Embedded UI bundle lookup (`path -> gzip bytes`), bundle info (`Embedded()`, `Version()`, `GitRev()`), `dbg::kProtocolVersion`, built-in fallback page | E1 | yes |
| `debugger.cmake`, `cmake/dbg_webui.cmake`, `cmake/dbg_webui_embed.cmake` | Build integration (above) and the embed step (2.11) | E1 | — |
| `dbg_disasm.h/.cpp` | Disassemblers: SH4, ARM7DI (ARM state), AICA DSP step (Appendix B) | E2 | yes |
| `dbg_events.h` | Event id enumeration and the event table (name, subsystem, rate class) | E0 | — |
| `dbg_regs.h`, `dbg_regs_tables.cpp` | Constant node and register tables (SH4 on-chip modules, Holly SB, PVR, AICA common / channel, GD-ROM, timing) | E0 / E3 | yes |
| `dbg_regs.cpp` | Side-effect-free register readers, writers (M2) | E3 | no |
| `dbg_mem.h/.cpp` | Side-effect-free guest memory read / write for targets `sh4`, `arm7` | E0 / E3 | no |
| `dbg_emu.h/.cpp` | Emulator-thread integration: lifetime, call queue, slice hook, stop latch, park loop, run-control sequencing | E0 / E4a | no |
| `dbg_sh4.h/.cpp` | SH4 code-breakpoint state used by the backends (2.3.4): armed set and page bitmap, the check helpers, resume exemption, owed remainder, temporary breakpoints, step-over / step-out state, step preconditions | E0 / E4b | no |
| `dbg_symbols.h/.cpp` | Symbol table from the ELF loaded by reios (own `Elf32_Sym` walk; the vendored libelf has no symbol API) | E0 / E5 | yes |
| `dbg_callstack.h/.cpp` | SH4 / ARM7 unwinders | E0 / E5 | no |
| `dbg_sgc.h/.cpp` | SGC frame capture ring (M3) | M3 | no |
| `dbg_core.h/.cpp` | JSON-RPC dispatch; breakpoint / watch / category tables; device tree; tick builder; event ring | E0 / E5 | no |
| `dbg_main.cpp` | `dbg::Init/Term`: config, the core thread, wiring net <-> core <-> emu | E5 | no |
| `dbg_selftest.h/.cpp`, `dbg_selftest_*.cpp` | In-emulator selftest driver and per-package test files (5.3) | E0 / E3, E4a, E4b, E5 | no |

### 2.2 Threads, lifetime, calls, stop arbitration

Threads: (U) main / UI thread; (E) emulator thread (`Dreamcast_impl::dc_run`); (N) network thread (`dbg::net::Server`,
socket I/O only — its `Handler` callbacks only enqueue); (C) debugger core thread (`dbg_main.cpp`: parses requests, owns the
timers for the periodic tick, the capture timeout and the client-absent grace period, and calls `dbg::emu::Call`).

**The one rule: guest state and the debugger's own tables (breakpoints, watches, categories, capture buffers) are touched
only on (E), or on another thread that holds the lifetime mutex L while no emulator thread exists.**
(Reasons: JIT register caching, per-thread MXCSR set by `UpdateFPSCR`, the non-re-entrant SIGSEGV handler, unsynchronised
block manager / scheduler / devices — devcast-debug-surface.md.) Two stated exceptions: the event ring has its own small
mutex (2.6); VRAM and `pvr_regs` are read and written by (U) and the refsw workers while a frame is queued
(`hw/pvr/Renderer_if.cpp:433-451`), so debugger reads of them can observe a frame in progress, and M4 captures wait for the
queued frame.

#### 2.2.1 Lifetime

Machine / thread state, guarded by the recursive lifetime mutex **L** in `dbg::emu`:
`NoMachine | Stopped | Running | Parked` (`NoMachine` = no `VirtualDreamcast`; `Stopped` = machine exists, no emulator
thread; `Parked` = emulator thread exists and sits in the park loop).

- `Dreamcast_impl::Init`, `StartGame`, `Reset`, `Resume`, `SaveState`, `LoadState`, `Term` hold L for their whole body
  (`LifetimeLock`, RAII) and publish the change: `Init` end -> `Stopped`; `Term` start -> `NoMachine`; `Resume` sets
  `Running` before it starts the thread; `dc_run` exit -> `Stopped`.
- `Resume()` and `Term()` first join a previous emulator thread (`emu_thread.WaitToEnd()`), **without** holding L, then
  take L. `Term()` with a live thread requests a stop without callback before the join (2.2.5). `dc_run` does not take L
  at entry (so `Resume` can hold L while it waits for `emu_started`); at exit it runs the stop callback as today, then
  takes L, drains the call queue (2.2.2), publishes `Stopped` and releases L.
- `Reset` and `LoadState` executed in `Stopped` publish a new stop (`stopId` + 1, 2.3.1) so clients refetch.
- (C) never holds L while it waits for (E).
- Every lifetime change invokes the run-state listener (2.2.4) on the thread that made it, with L held.

What works in each state:

| State | Wire `executionState` | Reads of guest state | Bookkeeping (breakpoints, watches, categories) | Writes, run control |
| --- | --- | --- | --- | --- |
| `NoMachine` | `paused`, `reason: "no-game"` | error `-32002`; `debugger.describe` returns the static tree with every value `"n/a"`; ticks carry `registers: {}` | on (C) under L | error `-32002` |
| `Stopped` | `paused`, `reason: "host-stopped"` (or `"guest-exit"`) | on (C) under L | on (C) under L | error `-32001` |
| `Running` | `running` | on (E), slice hook | on (E), park context (service excursion) | on (E), park context; steps: error `-32003` |
| `Parked` | `paused`, `reason` of the stop | on (E), park loop | on (E), park loop | on (E), park loop |

When the host UI resumes a `Stopped` machine the tick reports `running`; a debugger pause that was in force before the host
stop is not re-applied.

#### 2.2.2 Calls

`int dbg::emu::Call(CallClass cls, std::function<void()> fn)` — callable from any thread except (E); blocks at most 2 s.

| Class | Meaning | `NoMachine` | `Stopped` | `Running` | `Parked` |
| --- | --- | --- | --- | --- | --- |
| `CC_READ` | reads guest state and debugger tables | `-32002` | caller thread, under L | slice hook | park loop |
| `CC_TABLE` | changes debugger tables; may need code-cache maintenance | caller thread, under L | caller thread, under L (no code-cache work: every thread start builds a fresh cache, `libswirl.cpp:704-712`) | park context (service excursion, 2.2.4) | park loop |
| `CC_WRITE` | writes guest state (memory, registers, FPSCR / SR), reset | `-32002` | `-32001` | park context (service excursion) | park loop |
| `CC_EXEC` | run control (resume, step, step over / out) | `-32002` | `-32001` | not queued: answered from the lifetime snapshot (resume: 0; steps: `-32003`) | park loop |

Return value: 0 when `fn` ran; `-32000` (emulator busy) on timeout; `-32001` / `-32002` as above.

- A call is one heap object shared (`std::shared_ptr`) by the caller and the queue: `{cls, fn, state: Queued | Running |
  Done | Cancelled}`, guarded by the queue mutex, with a `std::condition_variable` waited with a predicate (`cResetEvent`
  is not usable: its state is uninitialised and `Wait()` has no predicate loop). `fn` captures by value or by
  `shared_ptr` only, never a reference or pointer to the caller's stack; its result lives in the captured object.
- Timeout: a `Queued` call is marked `Cancelled` and skipped by (E); a `Running` call is waited for without limit.
- (E) runs calls one at a time; a call never runs inside another call (depth counter, `verify(depth == 0)`).
- `dc_run` drains the queue as its last action before publishing `Stopped`: `CC_READ` and `CC_TABLE` calls run,
  `CC_WRITE` and `CC_EXEC` calls complete with `-32001`.
- A guest that spins the host (`hw/mem/_vmem.h:213-241`, `hw/holly/sh4_mem_area0.cpp:244-248`) or an (E) blocked for more
  than 2 s yields `-32000`; this is not recoverable from the debugger.

#### 2.2.3 Attention word, slice hook, stop latch

`dbg::g_attn` is a `std::atomic<uint32_t>` modified only with `fetch_or` / `fetch_and`; bits `ATTN_CALL` (at least one call
queued) and `ATTN_PAUSE` (a pause was requested). `dbg::g_stop_latched`, `dbg::g_intc_deferred`, `dbg::g_in_sleep` and
`dbg::g_sleep_cut` are plain `bool`s read and written by (E) only. `dbg::g_parked` is a `std::atomic<bool>` (2.2.5).

```cpp
int UpdateSystem() {                                   // hw/sh4/SuperH4_impl.cpp
    if (unlikely(dbg::g_stop_latched)) return 1;       // no emulated time, no scheduler callback
    Sh4cntx.sh4_sched_next -= SH4_TIMESLICE;
    if (Sh4cntx.sh4_sched_next < 0) sh4_sched_tick(SH4_TIMESLICE);
    if (unlikely(dbg::g_attn.load(std::memory_order_relaxed))) dbg::emu::SliceHook();   // after the scheduler tick
    return Sh4cntx.interrupt_pend | (sh4_int_bCpuRun == false);
}
int UpdateINTC() {                                     // hw/sh4/sh4_interrupts.cpp, covers every backend's call site
    if (unlikely(dbg::g_stop_latched)) { dbg::g_intc_deferred = true; return 0; }
    ... unchanged ...
}
```

`SliceHook()` has no return value. `UpdateSystem()` is called from two places: the slice end of a backend loop (or of a
step), and the wait loop of the SLEEP handler, which sets `g_in_sleep` around its loop. Stop causes are of two kinds.
An event breakpoint and a sound breakpoint are **freezing** causes: the scheduler must stop in the very call in which
they were raised (2.5.2). A pause request and `DBG_SERVICE` are **deferrable**: they are taken only at a slice end
outside a SLEEP wait, where leaving and re-entering `Run()` is exact (R13).

While the lifetime is `Running` the hook:
1. runs the queued `CC_READ` calls;
2. if a non-read call is queued, wants stop reason `DBG_SERVICE`;
3. if `ATTN_PAUSE` is set, wants stop reason `DBG_PAUSE` with the recorded cause (pause request, event breakpoint, sound
   breakpoint);
4. **latches the stop** — sets the wanted reasons and `g_stop_latched = true; g_parked = true; Sh4cntx.CpuRunning = false`
   — if a freezing cause is recorded, or if `g_in_sleep` is false and `DBG_PAUSE` or `DBG_SERVICE` is wanted. Deferrable
   causes met inside a SLEEP wait stay pending (`g_attn` stays set) and are latched at the next slice end of the backend
   loop. Liveness: a deferrable cause that has been pending for 250 ms of host time is latched at the next
   `UpdateSystem()` wherever it is called from (a guest that sleeps with interrupts masked, or one emulated very slowly);
   the SLEEP skew of 2.4 then applies.

During a step (the lifetime is `Parked`) the hook runs no call (calls are taken only by the park loop, between steps);
it does one thing: if a freezing cause is recorded it latches the stop, which ends the step at this slice boundary.

An SH4 code-breakpoint hit latches the stop in the same way from the check helper (2.3.4 c), at the instruction.

From the latch until the park context is entered:
1. `UpdateSystem()` returns 1 as its first statement: emulated time does not advance and no scheduler callback can fire.
2. `UpdateINTC()` accepts nothing and records `g_intc_deferred`.
3. The SLEEP handler (`hw/sh4/interpr/sh4_opcodes.cpp:1043-1061`, also reached from JIT blocks through `shop_ifb`) tests
   the latch before each `UpdateSystem_INTC()` call and leaves with `next_pc -= 2` (SLEEP is re-executed after resume),
   recording `g_sleep_cut` for step c of 2.2.4. That is its only change:
   ```cpp
   int i = 0; bool again = false;
   dbg::g_in_sleep = true;
   for (;;) {
       if (dbg::g_stop_latched) { again = true; dbg::g_sleep_cut = true; break; }
       if (UpdateSystem_INTC()) break;                           // interrupt accepted
       if (i++ > 1000) { again = true; break; }                  // today's bound: 1002 slices
   }
   dbg::g_in_sleep = false;
   if (again) next_pc -= 2;                                      // re-execute SLEEP
   ```
   The handler does not leave when `CpuRunning` is false: a host stop, and a step, run the wait to its end as today
   (at most 1002 slices, 448 896 cycles), which keeps a host pause and resume exact. Under a JIT the SLEEP block is
   re-executed after a latch until the slice counter runs out (up to 448 times); these re-executions have no side effect
   and are charged to the slice counter without scheduler time. Documented (2.4); it happens only for freezing causes
   and past the liveness bound.
4. The AICA scheduler callback starts with `verify(!burst.suspended)` (M2, 2.5.2).

#### 2.2.4 Stop reasons and the park context

`Sh4cntx.CpuRunning` keeps meaning "the backend loop continues". Why `Run()` returned is a bit set in `Dreamcast_impl`,
written under `callback_lock`:

| Bit | Set by |
| --- | --- |
| `HOST_STOP` | `Dreamcast_impl::Stop(cb)` on (U) (menu, ESC, window close, exit) |
| `GUEST_RESET` | `RequestReset()` on (E) (`SB_SFRES` write, `hw/holly/sb.cpp:106-113`); `control.reset` while running |
| `GUEST_EXIT` | `reios_exit` on (E) (`reios/reios.cpp:384-392`) |
| `DBG_PAUSE` | the stop latch: pause request, breakpoint hit, event or sound breakpoint, `StartPaused` |
| `DBG_SERVICE` | the slice hook, to run non-read calls outside the backend loop |
| `DBG_SWITCH` | `dbg::emu`, inside a service excursion on a backend without native breakpoints, when a call changed `UseFallbackLoop()`: `Run()` is re-entered on the other loop and the switch is logged (2.3.5) |

`SuperH4_impl::Stop()` and `Start()` lose their `verify`s. After `Run()` returns, and after every step, `dc_run` evaluates
**in this order**:

1. `GUEST_RESET`: `SaveRomFiles`; discard a suspended sound burst; `Reset()` (which resets the slice counter, 2.3.4 d);
   `sh4_host_fp_resync()`; the owed remainder, the exemption, `g_sleep_cut`, temporary breakpoints and a pending
   step-out are dropped (2.3.4 h, j). The rest of the debugger state is kept: if a debugger stop was in force or
   `DBG_PAUSE` is pending, the thread parks (setting `g_parked` again) with `reason: "reset"`; otherwise `Start()` and
   back into `Run()`.
2. `HOST_STOP` or `GUEST_EXIT`: leave the park context and take the normal stop path — complete a suspended sound burst
   with sound breakpoints suppressed, drop temporary breakpoints and a pending step-out, `SaveRomFiles`,
   `WaitQueueEmpty`, stop callback, then (under L) drain the queue and publish `Stopped`. The tick is `paused` with
   `reason: "host-stopped"` (`"guest-exit"` for `GUEST_EXIT`).
3. Queued calls are run, one at a time (this is the *service excursion* when the thread was `Running`). If a call set a
   stop reason (`control.reset` while running sets `GUEST_RESET`), evaluation restarts at 1.
4. `DBG_PAUSE`: park. The park loop waits on the queue condition variable for a call, a pending run-control action or
   `dbg::emu::Kick()`, and re-evaluates steps 1 to 3 after every wake-up and after every step. `SaveRomFiles` is not run
   and the thread does not exit.
5. Otherwise (`DBG_SERVICE` / `DBG_SWITCH` handled, nothing else pending): leave the park context and re-enter `Run()`.
   No tick, no `stopId` change: a service excursion is not a stop.

**Entering the park context** for a stop: `dbg::sh4` removes the temporary breakpoints and cancels a pending step-out
(2.3.4 j), whatever caused the stop, and reports what stopped (a listed breakpoint, the end of a step-over or step-out,
or nothing); then the run-state listener is invoked.

**Leaving the park context** (resume, step, end of a service excursion), in this order:
a. complete a suspended sound burst (M2, 2.5.2); if that hits a sound breakpoint the thread stays parked, the tick reports
   that breakpoint, and the SH4 command is dropped;
b. clear `g_stop_latched`;
c. if `g_intc_deferred`: clear it and call `UpdateINTC()` once (the acceptance the latch suppressed happens now, in the
   order of an undebugged slice: burst, interrupt acceptance, instructions). If the latch cut a SLEEP short
   (`g_sleep_cut`), the call is made with `next_pc` advanced past the SLEEP again, and the PC is put back on the SLEEP
   only when nothing was accepted — so an interrupt accepted here returns to the instruction after the SLEEP, as in an
   undebugged run. `g_sleep_cut` is cleared. No `UpdateINTC()` call is made when nothing was deferred, so a stop in the
   middle of a slice does not add an interrupt check that an undebugged run would not have;
d. `sh4_host_fp_resync()`;
e. for a resume or a step: `dbg::sh4` arms the resume exemption for the PC as it is now and keeps or drops the owed
   remainder (2.3.4 h). A step whose step c accepted an interrupt is complete at this point (2.3.2);
f. under `callback_lock`, and only if `HOST_STOP` and `GUEST_EXIT` are clear: `CpuRunning = true`, and for a resume or
   the end of an excursion also `g_parked = false`. A step keeps `g_parked` set, runs with `CpuRunning` set (R12), and
   ends by clearing `CpuRunning` and setting `g_stop_latched` again. A host stop or guest reset requested during the
   step clears `CpuRunning` and `g_parked` itself (2.2.5) and is found by the evaluation that follows the step;
g. a resume invokes the run-state listener with `Running` (the `running` tick); a step or an excursion does not.

**Run-state listener** (`dbg::emu::SetListener`, implemented by `dbg_core`): invoked with
`{lifetime, stopId, reason, breakpointId, notice}` on (E) for every stop and every resume, and on the thread that made a
lifetime change (with L held) for `Stopped` / `NoMachine`. At each invocation guest state is quiescent; the listener may
read it, builds the tick and hands the text to `dbg::net` (sends never block). It must not call `dbg::emu::Call`.

The armed SH4 breakpoint set used by the backends is owned by (E); other threads change it only through
`Call(CC_TABLE, ...)`, and a change made while running is applied in a service excursion (2.3.4 g).

#### 2.2.5 Host UI stops against a parked thread

- `SuperH4_impl::IsRunning()` becomes `CpuRunning || dbg::g_parked`. `g_parked` is set by (E) before it clears
  `CpuRunning` for any debugger stop (and again when it re-parks after a reset), and cleared when it resumes or when a
  stop is requested.
- `SuperH4_impl::Stop()` clears `CpuRunning` and `g_parked` and calls `dbg::emu::Kick()` (wakes the park loop). So every
  existing caller (`linux-dist/x11.cpp:132`, `sdl/sdl.cpp:134`, `windows/win_common.cpp:243`, `gui/gui.cpp:318`,
  `input/gamepad_device.cpp:84-96`, `reios/reios.cpp:385`) works unchanged, and `IsRunning()` still turns false the moment
  a stop is requested. `Dreamcast_impl::Stop(cb)` sets `HOST_STOP` and stores the callback under `callback_lock` before it
  calls `sh4_cpu->Stop()`; `RequestReset()` sets `GUEST_RESET` the same way; `reios_exit` sets `GUEST_EXIT` through a new
  `VirtualDreamcast::RequestExit()`. A second `Stop(cb)` while `HOST_STOP` is pending chains its callback after the stored
  one instead of aborting on `verify(callback == nullptr)`.
- `Dreamcast_impl::Term()` never runs with a live emulator thread: if one exists it requests a stop without callback and
  calls `emu_thread.WaitToEnd()` before `sh4_cpu->Term()`.
- `dbg::Init()` is called at the end of `reicast_init()`; `dbg::Term()` at the start of `reicast_term()`, before the GUI
  (and with it the machine) is destroyed.
- A guest soft reset during a step, the ESC key while parked and the menu key while parked therefore take paths 1 and 2 of
  2.2.4 instead of a `verify` abort or a lost callback.

#### 2.2.6 Host floating-point state

FPSCR and SR are written only on (E). `dc_run` calls `sh4_host_fp_resync()` (`hw/sh4/sh4_core_regs.cpp`:
`old_rm = old_dn = 0xFF; SetFloatStatusReg();`) on thread entry, after `Reset()` in the reset path, and when leaving the
park context. `state.setRegister` is a `CC_WRITE` call and therefore rejected in `Stopped`. (A thread inherits its
creator's MXCSR while the tree caches "already applied" in globals; without the resync the guest runs with the wrong host
rounding / denormal mode.)

#### 2.2.7 Latency and limits

- Pause request, service excursion (any write or bookkeeping call while running, including a change of the armed set):
  taken at the next slice end of the backend loop, i.e. within one slice of executed code — 56 instructions in the
  interpreter (`fast` timing), up to 448 blocks in the JIT. Time spent waiting in SLEEP does not count towards that
  slice: a SLEEP wait ends at the next accepted interrupt (at most 1002 slices, 448 896 cycles), and in the JIT's `fast`
  timing an idle guest, whose SLEEP block costs one cycle, reaches a slice end only every few interrupts (for 2ndmix,
  estimated from the prototype's counts, some tens of milliseconds of guest time). The bound is 250 ms of host time
  (2.2.3). Until a change of the armed set is applied the old blocks run.
- Event and sound breakpoints: an event raised by a scheduler callback is latched in the same `UpdateSystem()` call, also
  inside a SLEEP wait; an event raised by an SH4 instruction at the next `UpdateSystem()`. The event log entry carries the
  exact event; the PC reported is where the loop stopped.
- SH4 code breakpoint, temporary breakpoint, the end of a step-out: at the instruction, no latency (2.3.4). Mute / solo
  and every other helper-side state take effect at the next arrival.
- Cost on x64 (Appendix C): an arrival that continues about 35 host instructions; a stop + resume about 33 000 host
  instructions (one compile); a pause + resume at a slice end about 800.
- Calls that arrive during a step wait for the park loop. The longest step is a step on SLEEP (at most 1002 slices).
- `Call`s of class `CC_READ` are served in every `UpdateSystem()`, also inside a SLEEP wait.
- (E) blocked on the UI or audio, or a guest that spins the host: `-32000` after 2 s for every call; pause does not
  complete.

#### 2.2.8 Backends without a complete context at the slice hook

A backend reports `SH4DBG_CTX_AT_SLICE` (2.3.4 l) when `Sh4cntx` is complete inside `UpdateSystem()`: true for the
interpreter and the x64 JIT (the hook sees the pre-interrupt PC of the next block). arm32, arm64 and x86 keep the next PC
in a host register across the call (`jit/backend/arm64/rec_arm64.cpp:135-153`, `jit/backend/arm32/ngen_arm.S:169-178`), so
`Sh4cntx.pc` is stale there. On such a backend running ticks omit the path `dc.sh4.cpu` and set
`executionState.registersStale: true` (2.9), SH4 callstacks are not built while running, and watches on `dc.sh4.cpu.*`
evaluate to `"n/a"` while running. The context is valid after `Run()` returns, and writes only ever happen there.

#### 2.2.9 `StartPaused`; what the window shows

- `Debugger:StartPaused=yes` parks once per `StartGame`: after `emu_started.Set()`, with `CpuRunning` false, before the
  first `Run()`; `reason: "start"`. At that point reios has not booted: PC is `0xA0000000`, RAM is empty and there are no
  symbols (the ELF is loaded by the first instruction, `reios/reios.cpp:594-631`). This park waits for a client without
  limit (the grace period of 2.9 starts only at a disconnect).
- While (E) is parked the window shows the last swapped frame; it is not repainted and shows no OSD (`gui/gui_renderer.cpp`
  renders the OSD only with a frame). The park loop sets the window title to `nullDC 3.0.0 [paused by debugger]` on entry
  (`os_SetWindowText`); the title returns with the next frame-rate update after resume. The UI thread never blocks on a
  parked emulator thread.

### 2.3 Execution control

#### 2.3.1 State on the wire

One global state, as the protocol has: `running` or `paused`, plus (3.2) `stopId`, `reason`, `breakpointId`,
`registersStale`, `notice`.

- `stopId` counts stops. It is incremented on every transition into a state reported as `paused` (park for any cause,
  `Stopped`, `NoMachine`), on every completed step (paused -> paused), on a reset performed while paused, and on `Reset` /
  `LoadState` in `Stopped`. Service excursions do not change it.
- `reason` values the server sends (the client treats the field as an open string):

  | `reason` | When |
  | --- | --- |
  | `pause` | `control.pause` honoured |
  | `step` | `control.step`, `stepOver` or `stepOut` completed (a single step, the stop of a temporary breakpoint, the return a step-out waited for) |
  | `breakpoint` | a code breakpoint hit (`breakpointId` present) |
  | `event` | an event breakpoint hit (`breakpointId` present) |
  | `reset` | reset performed while a debugger stop was in force |
  | `start` | the `StartPaused` park |
  | `capture` | an SGC capture started from `paused` finished and paused again (M3) |
  | `host-stopped` | lifetime `Stopped` (menu, pause in the emulator's own GUI) |
  | `guest-exit` | lifetime `Stopped` after `reios_exit` |
  | `no-game` | lifetime `NoMachine` |

  If several causes coincide, the priority is `breakpoint`, `event`, `step`, `pause`.
- `reason` and `breakpointId` are repeated in every paused tick of the same stop. Running ticks carry neither.

#### 2.3.2 Run control commands

Requests are answered as soon as they are validated and armed (the client has a 10 s timeout); completion is reported by a
tick. All are `CC_EXEC` calls except `control.pause` (which only sets `ATTN_PAUSE`) and `control.reset` (`CC_WRITE`).

| RPC | `Parked` | `Running` | `Stopped` / `NoMachine` |
| --- | --- | --- | --- |
| `control.pause {target?}` | `{}`, nothing changes | request the pause, `{}`; tick `paused`, `reason: "pause"` when parked. `target` is ignored (one global state) | `-32001` / `-32002` |
| `control.runUntil {}` | resume, `{}`; tick `running` | `{}`, nothing changes | `-32001` / `-32002` |
| `control.step {target:"sh4"}` | one SH4 instruction (below), `{}`; one tick `paused`, new `stopId`, `reason: "step"`; no `running` tick in between | `-32003` | `-32001` / `-32002` |
| `control.stepOver {target:"sh4"}` | below: a step, or a resume (tick `running`) until the temporary breakpoint stops (tick `paused`, `reason: "step"`) | `-32003` | `-32001` / `-32002` |
| `control.stepOut {target:"sh4"}` | below: a resume (tick `running`) until the return (tick `paused`, `reason: "step"`) | `-32003` | `-32001` / `-32002` |
| `control.step / stepOver / stepOut {target:"arm7"}` | M2, 2.5.3; `-32004` before | `-32003` | `-32001` / `-32002` |
| `control.step {target:"dsp"}` | M3, 2.5.4; `-32004` before. `stepOver` / `stepOut` for `dsp`: `-32004` always | `-32003` | `-32001` / `-32002` |
| `control.reset {}` (M2) | `Reset()` directly through the emulator's existing reset path; an owed remainder is dropped (2.3.4 h); stays paused, new `stopId`, `reason: "reset"` | sets `GUEST_RESET` and stops the loop; the machine resets and keeps running; an event-log entry `dc.system.reset` | `-32001` / `-32002` |

**SH4 step.** One instruction is executed by the active backend (R12; 2.3.4 i, k); a delayed branch executes together
with its delay slot, taken or not. Preconditions, checked on (E) before anything executes, with the opcodes read through
`dbg_mem` — on failure the in-band error `-32005` and no stop: PC is odd (`"cannot step: pc is odd"`); the word at PC is
not readable (`"cannot step: pc not readable"`); the opcode at PC decodes to `iNotImplemented`
(`"cannot step: undefined opcode"`; executing it ends the process in `NO_MMU` builds); PC holds a delayed branch and its
slot word is not readable, is undefined, or is an instruction that writes PC (`"cannot step: illegal slot instruction"`;
the decoder `verify`s that). Documented behaviour:
- Time: a step is charged as a block of that one instruction (2.4).
- If leaving the park context accepts a deferred interrupt (2.2.4 c), the step ends at the vector without executing an
  instruction.
- When the step uses up the slice, `UpdateSystem()` runs (scheduler callbacks fire) and a pending interrupt is accepted:
  the step ends at `VBR + 0x600`.
- `RTE`: its end calls `UpdateINTC`, so the step can end in the next handler.
- SLEEP: one step runs the wait as a run does: it ends at the handler when an interrupt is accepted, or on the SLEEP
  again after the handler's bound of 1002 slices (448 896 cycles). A host stop requested meanwhile is honoured when the
  step returns.
- A reios trap: one step executes the whole HLE call.
- If an event or sound breakpoint fires during the step, the step ends at that slice boundary and the stop is reported
  with that breakpoint.

**SH4 step over.** At `BSR`, `BSRF` or `JSR`: a temporary breakpoint at `PC + 4` with `sp0 = R15`, then resume; it stops
only when `R15` is at the level of the request (`OnLevel(R15, sp0)`, 2.3.4 j — a recursive callee reaches `PC + 4` at a
deeper stack level first). At `TRAPA`: the same at `PC + 2`. At any other instruction: a step. A temporary-breakpoint
stop is reported with `reason: "step"` and no `breakpointId`.

**SH4 step out.** Resume with a step-out pending: the emulator stops immediately after the `RTS` or `RTE` (and its delay
slot) that leaves the activation in which the request was made, `reason: "step"`. Which return that is, is decided by the
stack-level rule of 2.3.4 j (calls made at the request's level are counted, handlers entered after the request are
skipped whole). Its limits are documented in the UI help (2.3.7 item 1). (`PR` is not used: guest code restores it only
just before `RTS`, so after a function's first call it points into the function itself; in a handler the return address
is `SPC`.)

Temporary breakpoints and a pending step-out are internal: never listed, never muted, and removed on every stop,
whatever caused it.

#### 2.3.3 Breakpoints, and the requirements on every SH4 backend

Breakpoints are server-owned; ids are allocated from 1 and never reused in a process; in the tick they are keyed by the
decimal id string. Every `breakpoints.add` creates a new breakpoint (no de-duplication); when several match, the lowest id
is reported.

- A code breakpoint is `(event, address)` with events `dc.sh4.cpu.pc`, `dc.aica.arm7.pc` (M2), `dc.aica.dsp.step` (M3).
  `address` is required: any u32 for SH4 (odd -> `-32602`), a multiple of 4 for ARM7, 0..127 for the DSP.
- An event breakpoint is `(event)` with `kind: "event"`; the event must be in the server's event table (2.6).
- Category (as devtools-protocol.md 5.6): `kind == "event"` -> `events`; else the lower-cased `event` contains `sh4` ->
  `sh4`; `arm7` -> `arm7`; `aica` or `dsp` -> `dsp`; else `events`.
- Two sets are kept apart. A code breakpoint is **armed** while it is enabled (every temporary breakpoint is armed): the
  armed set decides where a backend tests. A breakpoint is **active** iff it is enabled, its category is not muted and (no
  category is soloed or its category is soloed), and at least one client is connected or the grace period of 2.9 has not
  expired: activity is evaluated at each arrival and decides whether the arrival stops.
- Address compare: in `NO_MMU` builds (the default) SH4 code addresses are compared as `addr & 0x1FFFFFFF` — breakpoints,
  temporary breakpoints and the unwinder's "executable address" test — because the same code is reachable at `0x0C..`,
  `0x8C..`, `0xAC..` (reios boots discs at `0xAC008300`, ELFs at `0x8C010000`). In `WITH_MMU` builds P1 / P2 addresses are
  compared masked and all others on the full 32 bits. The tick reports the PC unmasked and the breakpoint address as the
  client gave it; clients compare masked (2.3.4 f).
- Hit: park; tick `paused` with `reason: "breakpoint"` and `breakpointId`.
- Event breakpoints: `dbg::Event` (2.6) tests the armed event set on (E); a hit requests a pause that is latched at the
  next `UpdateSystem()` (2.2.7).

**Requirements on SH4 code breakpoints and steps (every backend; observable behaviour).**

- R1 — *What runs.* A breakpoint and a step are executed by the backend that runs the guest. On a backend that reports
  `SH4DBG_NATIVE` (2.3.4 l; in M1 the interpreter and the x64 JIT) nothing changes engine because a breakpoint is listed,
  armed or hit, or for a step.
- R2 — *Guest memory* is never modified to implement a breakpoint (no opcode patching).
- R3 — *Cost when unused.* With nothing armed, a JIT's compiled code and dispatcher contain no added instruction; the
  only cost is one load and branch per *decoded* instruction. The interpreter runs its unmodified loop.
- R4 — *Arming.* Adding, removing, enabling or disabling an SH4 code breakpoint, and adding or removing a temporary
  breakpoint, discards the compiled blocks that overlap the 4 KiB page of that address, in every mirror, and nothing
  else. Category mute / solo, client presence and (later) conditions and hit counts are evaluated at each arrival and
  never need a recompile. A step-out discards blocks by their end type (2.3.4 j).
- R5 — *A test that answers "continue" changes nothing.* Block boundaries, block costs and the points at which the slice
  counter is tested are unchanged: a run whose tests all answer "continue" (breakpoints never reached, or inactive) has
  the same guest state and the same scheduler time, instruction for instruction, as a run with no breakpoints.
- R6 — *Stop.* A stop reports PC = the instruction in front of which it stopped. Nothing of that instruction has
  executed; the complete context (integer, FP, SR with T, FPSCR, PR, MAC, banks) is in `Sh4cntx`; no further guest
  instruction executes and no scheduler time passes before the park context is entered.
- R7 — *Time at a stop.* The scheduler is not advanced. The slice counter holds the block cost charged for the
  instructions that ran; the cost of the rest of the block is refunded (2.3.4 b). With a cost model that is additive over
  instructions that is exactly what the timing mode charges up to the instruction; in `fast` timing, where a block is
  charged as a unit, it is the proportional share.
- R8 — *Stop + resume is neutral.* A stop followed by a resume is identical to no stop — same instruction stream, same
  total cycles, same block boundary after it, same slice-end points — in every timing mode, provided the PC was not
  written and no step, reset or state load was made during the stop.
- R9 — *Resume exemption.* A resume or a step from a stop at X executes the instruction at X once without testing it;
  the next arrival at X stops again. The exemption applies only if execution continues at X: if the deferred interrupt
  acceptance of 2.2.4 c moved the PC to a vector, a breakpoint on the vector is honoured, and X stops again when the
  handler returns to it (X has not executed).
- R10 — *Delay slots.* A breakpoint on the slot of a delayed branch (`Delayslot` type bit, `hw/sh4/sh4_interpreter.h`)
  stops once per execution of the pair, in front of the branch, with PC = the branch and the id of that breakpoint,
  whether or not the branch is taken. Nothing is ever tested, and no stop is possible, between a delayed branch and its
  slot (the context is mid-instruction there: `RTE` has loaded SR without `UpdateSR()`, `BSR` / `JSR` have not written
  `PR`). A slot address that is reached as a branch target is an ordinary instruction and stops with PC = that address.
- R11 — *Addresses* are compared as stated above.
- R12 — *Step.* A step executes exactly one instruction (a delayed branch with its slot) through the same decoder, code
  generator and cost function as a run, and does the end-of-slice work a run would do when the slice counter expires.
  Arithmetic (FPU included) and time are the running backend's, not the interpreter's. `CpuRunning` is set for the
  duration of the step, so `UpdateSystem()` and the SLEEP handler behave as in a run.
- R13 — *Counters persist.* The slice counter survives `Run()` exit and entry and steps; it is reset only by
  `SuperH4_impl::Reset`. Leaving and re-entering `Run()` (stop and resume, service excursion, a host pause at a slice
  end) is therefore invisible to the guest.
- R14 — *Self-modifying code.* The existing handling (`bm_LockedWrite`, page protection, the check levels) is unchanged;
  a block recompiled for any reason is tested again.
- R15 — *Step preconditions.* An odd PC, an unreadable PC, an undefined opcode and an illegal slot instruction make a
  step fail with an error (2.3.2); they do not hang or end the process.

The interpreter and the x64 JIT satisfy R1-R15 with the mechanism of 2.3.4. Every other JIT backend uses the fallback of
2.3.5 until it implements the mechanism; what the fallback does not meet (R1, R12 and, until the backend's counter
persists, R13) is stated there and in the tick's `notice`.

#### 2.3.4 The mechanism: breakpoints and steps in the active backend

This section implements D1. A prototype of it on the x64 JIT and the interpreter exists and passed its proof points
with no time skew per stop (Appendix C: branch `skmp/nulldc3-proto-bp`, not to be merged; the specification with the
full code walk is `agents/nulldc3/review/jit-breakpoints-spec.md`; what the prototype does not cover is 2.3.7 item 7).
Code references are `libswirl/...` at `34a62ea8`.

Terms. A *block* is what `dec_DecodeBlock` produces (`hw/sh4/dyna/decoder.cpp:1035-1223`): straight-line instructions
ended by a branch with its slot, by a fallback that writes PC / FPSCR, or by the size limits (500 SHIL ops, or
`guest_cycles >= max_cycles`; never between a branch and its slot). Its cost `guest_cycles` is a compile-time constant,
subtracted from the slice counter in the block prologue. The *slice counter* is the cycles left until the next
`UpdateSystem()`: the global `cycle_counter` of the x64 JIT (`jit/backend/x64/rec_x64.cpp:41-43`), tested by the dispatcher
after each block; a file-static in the interpreter. *Time* is `sh4_sched_now64() + SH4_TIMESLICE - counter`. A *check* is
code in front of one guest instruction that calls the helper and can stop there. A *temporary block* is compiled for one
execution and never entered in the block cache; it serves a single step (i) and the *remainder* of a block that a stop
interrupted (h). An *armed address* is a masked address with an enabled breakpoint or a temporary breakpoint (2.3.3).

**a. Decoder query and the check op.** In `dec_DecodeBlock`, right after the opcode is read (`decoder.cpp:1079`) and
before anything is charged or emitted for it, for every instruction that is not a delay slot:

```cpp
if (!state.cpu.is_delayslot && dbg_sh4_WantsCheck(state.cpu.rpc, (OpDesc[op]->type & Delayslot) != 0)) {
    Emit(shop_dbg_check, shil_param(), mk_imm(state.cpu.rpc), mk_imm(blk->guest_cycles));   // rs2 becomes the refund (b)
    state.dbg_checks++;
}
```

`dbg_sh4_WantsCheck(pc, has_slot)` is inline: nothing armed -> false (one load and branch, R3); otherwise
`IsArmed(pc) || (has_slot && IsArmed(pc + 2))`, with a per-page bitmap in front of the exact lookup.

- The block is **not ended** at a check: ending it would change the number of blocks and, in `fast` timing, the cycles
  charged (one per block, `decoder.cpp:1208-1211`). `shop_dbg_check` is a new SHIL op at the end of `shil_canonical.h`
  with a native implementation required (`shil_recimp`). `shop_ifb` is not reused: its register flush is partial for most
  opcode classes, it has no conditional block exit, and the optimiser must know that the op reads `sr.T`.
- Size limits ignore check ops: the soft limit test (`:1053-1054`) and the hard `verify` (`:1143`) use
  `oplist.size() - state.dbg_checks`; otherwise a check could move a block end. `guest_cycles`, `guest_opcodes`,
  `has_readm / has_writem / has_fpu`, `BlockType`, the idle-loop classification and the block hash do not see the op.
- `srt_waw` (`hw/sh4/dyna/shil.cpp:832-865`) treats the op as a reader of `sr.T`, as it treats `shop_ifb`. Without that,
  a T write in front of the check is deleted when a later instruction overwrites T, and the stop shows a stale T.
- The DIV32 idiom matchers (`decoder.cpp:644-696`; off by default) decline when an address in `[pc + 2, pc + 130)` is
  armed (`dbg_sh4_AnyArmed`), and in temporary blocks.
- Register allocation: the op is a full flush (`IsFlushOp`, `hw/sh4/dyna/regalloc.h:374-380`, shared by x64, arm32 and
  arm64). Every span is closed in front of it — `r0-r15`, the banked `r0-r7`, `gbr ssr spc sgr dbr vbr`, `mach macl`, `pr`,
  `fpul`, `sr.T`, `sr.status`, `fpscr` and the 32 FP registers — so the context is complete when the helper is called
  (R6), and nothing is cached after it: every register is reloaded from the context on its next use, which is how
  register writes made during a stop take effect. PC is not flushed: it is an argument, and the helper writes it on a
  stop.

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

**c. The check, the helper, leaving the dispatcher (x64).** Emitted per check, about 60 bytes:

```
mov  arg0, pc_i
mov  arg1, refund_i
mov  arg2, block->addr + block->sh4_code_size        ; end of the block
call dbg_sh4_Check
test eax, eax
jz   continue
add  dword [cycle_counter], refund_i                  ; stop path
add  rsp, STACK_ALIGN + 8                             ; drop the block frame and the return address into the dispatcher
jmp  mainloop_exit                                    ; the main loop's end_loop label, recorded by build_mainloop
continue:
```

A block is entered by `call rax` with the main loop's frame below it, and callee-saved registers are saved by the main
loop, not per block, so the stop path needs nothing else. The dispatcher fast path (`rec_x64.cpp:158-165`) is unchanged.
No `UpdateSystem()` runs on the stop path: time does not advance and the counter is not refilled (R7).

`u32 dbg_sh4_Check(u32 pc, u32 refund, u32 block_end)`:
1. resume exemption (h): if it is armed, disarm it; if it is for this masked pc, return 0;
2. find the breakpoints at `pc`, else those at `pc + 2` (slot rule, e);
3. evaluate them in id order: the stack condition of a temporary breakpoint (j); the active state of a listed
   breakpoint (category mute / solo and client presence — one mask, 2.3.3; later its condition and hit count). If none
   stops, return 0;
4. stop: `Sh4cntx.pc = pc`; record `{pc, block_end, refund}` as the owed remainder; record the cause and the breakpoint
   id; latch the stop exactly as 2.2.3 does (`g_stop_latched`, `g_parked`, `CpuRunning = false`, reason `DBG_PAUSE`);
   return 1.

The helper reads guest state freely and writes none when it returns 0. An arrival that continues costs about 35 host
instructions; a stop + resume costs about 33 000 (one compile of the remainder), so anything evaluated per arrival
belongs in the helper.

**d. The slice counter survives.** `mov [cycle_counter], SH4_TIMESLICE` at main-loop entry (`rec_x64.cpp:146`) is
removed; the counter is initialised statically to `SH4_TIMESLICE` and reset by `SuperH4_impl::Reset` through the backend
(`ResetSlice()`, l). The interpreter's counter (`hw/sh4/interpr/sh4_interpreter.cpp:74`, reset at `:79` today) becomes
file-static state with the same lifetime. This is R13; it also makes an ordinary host pause and resume exact, which it is
not today. PLAN T3.0 moves the counter into `Sh4Context`.

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

**f. Address identity.** The mask `0x1FFFFFFF` (`DBG_SH4_ADDR_MASK`) is applied in the armed set (stored masked), the
decoder query, the helper lookup, the exemption and temporary breakpoints. The page discard uses the block manager's own
index, `(addr & RAM_MASK) / REI_PAGE_SIZE`, which is the same for every mirror; ROM at `0x00000000 + X` and RAM at
`0x0C000000 + X` stay different addresses (aliasing between areas only causes extra discards). The PC a stop reports is
the address the running block was compiled for: blocks are looked up by `fpcb[(addr >> 1) & FPCB_MASK]`, so a block
compiled as `0x8C..` also runs for `0xAC..`, and the reported PC can be another mirror of the instruction than the one the
guest jumped to. The JIT does not run in `WITH_MMU` builds today; the MMU rule of 2.3.3 concerns the interpreter.

**g. Changing the armed set.** `dbg::sh4` applies every change of the armed set (add, remove, enable, disable, temporary
breakpoints) on (E) in the park context — the park loop, or a service excursion (2.2.4) when the change arrives while
running — so no block is executing and none is on the host stack when the set changes. (The slice hook can run inside a
block, because the SLEEP fallback loops on `UpdateSystem_INTC`; that is why the hook only requests.) Each change updates
the masked set and the page bitmap and calls the backend's `DebugDiscardPage(addr)`; the JIT forwards it to the new
`bm_DiscardAddrPage(addr)`, which copies `page_blocks[(addr & RAM_MASK) / REI_PAGE_SIZE]` and discards each block.

- Complete: `bm_AddBlock` enters a block in the list of every page between its first and last byte
  (`hw/sh4/dyna/blockmanager.cpp:132-143`), so the page's list holds every block that contains the address, including
  blocks that start on the previous page and blocks that contain it as a delay slot. (The existing `bm_DiscardAddress`
  is not used: it is undeclared and erases while iterating.) `bm_DiscardBlock` moves a block to `del_blocks`; it never
  frees or overwrites host code.
- Self-modifying-code handling is untouched (R14): the discard changes neither `page_has_data` nor the page protection;
  recompiled blocks get the same `DoCheck` result as before; a guest write to the page still faults into
  `bm_LockedWrite`, and the blocks compiled afterwards carry the check again because the decoder asks at every decode.
  `FullCheck` compares guest bytes and is unaffected (R2).
- `bm_Reset` needs nothing: the armed set lives in the debugger and is consulted at decode.
- With no emulator thread (`Stopped`) only the set changes: every thread start builds a fresh cache.

**h. Resume: exemption and remainder.**

*Exemption.* When the park context is left for a resume or a step (2.2.4 e, after the deferred interrupt acceptance),
`dbg::sh4` arms the exemption iff `Sh4cntx.pc`, masked, is still the PC of the stop and `dbg_sh4_WantsCheck` is true for
it (opcode read through `dbg_mem`). The first check call clears the flag and, if its pc matches, answers "continue".
Because the flag is armed only when the first block to run starts with that check, it cannot go stale; it is also
cleared at the next stop (R9).

*Remainder.* If the stop came from a check in block B at instruction i and the PC is unchanged, `recSH4::Loop()` first
runs the rest of B as a temporary block (i), decoded from `pc_i` with

```
end_pc = B.addr + B.sh4_code_size       // the decoder ends the block in front of this address
cycles = refund_i                       // replaces the computed cost, after the clamps
```

and then enters the main loop. The remainder starts with the check for `pc_i` (consumed by the exemption) and contains
the checks of every other armed instruction of B. Result: instructions `i..end` run once, the total charged for B is
`charged_i + refund_i = C`, the next block starts where B's successor starts, and the slice-end test happens after the
remainder as it would after B (R8). A stop inside the remainder works the same way (its refunds are computed from the
overridden cost). The remainder is decoded from current memory with the current FPSCR and armed set, so breakpoints,
registers and memory changed during the stop take effect.

The form D1 sketches — resume into a cached block that starts at `pc_i` — is not used: such a block has its own cost (in
`fast` timing 1, or an idle-class multiple) and, when B ended at a size limit, a different end, so stop + resume would not
be neutral; and a block registered at `pc_i` must be the one any jump to `pc_i` gets.

The owed remainder is dropped when the PC at resume differs from `pc_i` (the PC was written), when a step is made, and
on reset or state load. Resume then dispatches normally — the first block is the cached or newly compiled block at PC,
its first check consumed by the exemption — and the stop is not neutral: `charged_i` stays charged and the next block
costs what it costs (in `fast` timing at most one block cost of skew).

**i. Single step: temporary blocks.** `SuperH4Backend::Step()`; the JIT's is `rdv_RunTempBlock({max_opcodes = 1})` in
`hw/sh4/dyna/driver.cpp`:

1. apply the boot soft-reset rule (`driver.cpp:235-241`) and the free-space rule (`:244-248`): `ResetCache()` first;
2. `rbi->Setup(pc, fpscr, limits)`: the decoder ends the block after one instruction; a delayed branch keeps its slot.
   There is no cost override: the cost is what the cost function gives a block of that one instruction (`fast`: 1 cycle,
   or the idle / syscall-area multiples);
3. `rdv_ngen->Compile(rbi, NoCheck, ...)`: no SMC entry check (the code was decoded this instant, and that check's
   failure path looks the block up in the cache). The breakpoint check of an armed PC is still emitted; the exemption
   consumes it;
4. `bm_SetTempBlock(rbi)`; `rdv_ngen->RunBlock(code)`; `bm_SetTempBlock(0)`;
5. give the code space back: if `LastAddr` is still where the compile left it, set it back to its value before the
   compile (nothing else compiles while a block runs; a cache clear from inside the block moves `LastAddr` and wins);
   delete `rbi`.

A temporary block is not registered in `fpcb` / `blkmap` / `all_blocks` / the page lists: a cached block at that PC may
exist and must stay, and a one-instruction block must never be found by the dispatcher (it would change the block
boundaries and costs of the run). The block manager knows it only through one pointer (`bm_SetTempBlock`), consulted
first by `bm_GetBlock(void* code)`, so that the memory-access rewrite on a fault finds its `memory_accesses`.

`RunBlock` is a new `NGenBackend` entry, generated next to the main loop with the main loop's frame:

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
(page discard) and removed at every stop.

*Stack test*, shared: `OnLevel(sp, sp0) = sp >= sp0 && sp - sp0 < 0x100000` — at the level of the request or above it,
on the same stack. Below `sp0` is a deeper activation; 1 MiB or more above it is taken to be another guest thread's
stack. Step over (2.3.2) stops only if `OnLevel(R15, sp0)`.

*Step out.* State: `sp0 = R15` at the request, `level = 0`, `calls = 0`. Events are reported after the instruction and
its slot ran; the first matching row applies.

| Event | Action |
| --- | --- |
| interrupt or exception accepted (`Do_Interrupt`, `Do_Exception`, `hw/sh4/sh4_interrupts.cpp:153-190`; `TRAPA` included) | `level++` |
| `RTE`, `level > 0` | `level--` (a handler entered after the request is skipped whole) |
| any event, `level > 0` | ignore |
| call (`BSR` / `BSRF` / `JSR`), `R15 == sp0` | `calls++` |
| `RTS` or `RTE`, `!OnLevel(R15, sp0)` | ignore |
| `RTS`, `R15 == sp0`, `calls > 0` | `calls--` (the return of something called at the request's level) |
| `RTE`, `R15 == sp0`, `calls > 0` | ignore |
| `RTS` or `RTE` otherwise (`R15 > sp0`, or `R15 == sp0` with no call outstanding) | **stop**, reason `step` |

(The simpler rule "the first `RTS` / `RTE` executed with `R15 >= sp0`" is wrong twice: a callee that uses no stack
returns with `R15 == sp0`, and so does the `RTE` of an interrupt taken in the function's body.)

*Instrumentation.* While a step-out is pending (`dbg_sh4_stepout != 0`) the decoder marks blocks by their end type —
`BET_StaticCall` and `BET_DynamicCall` as call, `BET_DynamicRet` as `RTS`, `BET_DynamicIntr` as `RTE` — in a new field
`RuntimeBlockInfo::dbg_flow`. The epilogue of a marked block, after it has written `next_pc` (`rec_x64.cpp:770-822`),
calls `dbg_sh4_CheckFlow(kind)` and on non-zero leaves through `mainloop_exit`. For an `RTE` block the report comes
before `UpdateINTC` (`:825`), and `UpdateINTC` still runs before leaving: if the `RTE` lets an interrupt in, the stop is at
the handler entry. No SHIL op and no register flush are needed (at the epilogue every span is written back); block
boundaries and costs are unchanged, and the block has been charged in full, so there is no remainder.

*Which blocks are discarded.* Not pages: at the request, every cached block whose end type is one of the four
(`bm_DiscardFlowBlocks(false)`, a scan of `all_blocks`); when the step-out ends or is cancelled by any stop, every block
with `dbg_flow` set (`bm_DiscardFlowBlocks(true)`). Both on (E) while parked, through the backend's `DebugDiscardFlow`.
(`ResetCache()` is not used: it also clears `page_has_data` and so changes the SMC handling of later blocks. A permanent
flag test in every call / return epilogue would cost run time with no debugger; single-stepping to the return is slow
and, in `fast` timing, changes the time charged.)

**k. Interpreter-type backends.** `SH4IInterpreter::Loop()` runs a separate debug loop while anything is armed or a
step-out is pending; the normal loop (`sh4_interpreter.cpp:81-114`) is untouched apart from the persistent counter. The
choice is made at `Loop()` entry; every change of the armed set while running goes through a service excursion, which
re-enters `Run()`. The debug loop has the normal loop's slice structure and counter; one iteration:

```
addr = next_pc; op = IReadMem16(addr)
if (addr == slot_pc)            slot_pc = none
else if (dbg_sh4_WantsCheck(addr, has_slot(op)) && dbg_sh4_Check(addr, 0, 0)) return   // stop: nothing ran, nothing charged
next_pc = addr + 2; ExecuteOpcode(op)
if (has_slot(op) && next_pc == addr + 2) slot_pc = addr + 2                             // untaken BT/S, BF/S
l -= cost
if (step-out pending) { report call / RTS; if (stop pending) return }
```

- A stop returns with `next_pc = addr` and the counter unchanged. No remainder exists (`block_end = 0`); the loop is
  exact by construction.
- The interpreter runs the slot of an untaken `BT/S` / `BF/S` as a separate instruction
  (`hw/sh4/interpr/sh4_opcodes.cpp:948-957, 972-981`). `slot_pc` suppresses the check in front of it once, so the
  breakpoint fires once, in front of the branch, as in the JIT (R10).
- `Step()`: one iteration plus the end-of-slice work; after an untaken delayed branch, the slot too, so a step is the
  same unit in both backends.
- Step-out: calls and `RTS` are reported by the loop after `ExecuteOpcode`; `RTE` reports from its handler between
  `next_pc = newpc` and its interrupt check (`sh4_opcodes.cpp:905-921`), the same order as the JIT epilogue.
- In MMU builds the debug loop and `Step()` keep the normal loop's `SH4ThrownException` catch.
- `SuperH4_impl::Step()` as it exists today (no charge, no scheduler) is not used; it becomes a forward to the backend's
  `Step()`.
- The cycle-accurate backend (PLAN track 4) follows this subsection.

**l. Interface between the backends and the debugger.** Everything is (E) only.

```cpp
// debugger/debugger.h — what backends call. With NULLDC_DEBUGGER OFF: inline stubs, the query is constant false.
#define DBG_SH4_ADDR_MASK 0x1FFFFFFF
extern u32 dbg_sh4_armed;                                          // number of armed addresses
bool dbg_sh4_IsArmed(u32 pc);
static inline bool dbg_sh4_WantsCheck(u32 pc, bool has_slot);      // a
bool dbg_sh4_AnyArmed(u32 pc, u32 bytes);                          // a: idioms that fold instructions
extern "C" u32 DYNACALL dbg_sh4_Check(u32 pc, u32 refund, u32 block_end);       // c
extern u32 dbg_sh4_stepout;                                        // non-zero: instrument call / return blocks (j)
extern "C" u32 DYNACALL dbg_sh4_CheckFlow(u32 flow);               // j: 1 call, 2 RTS, 3 RTE
void dbg_sh4_OnException();                                        // from Do_Interrupt / Do_Exception (j)
bool dbg_sh4_StopPending();                                        // did the loop return because of a check
bool dbg_sh4_TakeRemainder(u32 pc, u32* end_pc, u32* cycles);      // h: handed out once

// hw/sh4/sh4_if.h — struct SuperH4Backend. Default bodies: a backend that implements nothing gets the fallback (2.3.5).
enum { SH4DBG_NATIVE = 1, SH4DBG_CTX_AT_SLICE = 2 };
virtual u32  DebugCaps()                              { return 0; }      // NATIVE: checks, Step() and flow reports work
virtual bool Step()                                   { return false; }  // i, k
virtual int  SliceCyclesLeft()                        { return 0; }      // for dc.timing
virtual void ResetSlice()                             { }                // d
virtual void DebugDiscardPage(u32 addr)               { }                // g
virtual void DebugDiscardFlow(bool instrumented_only) { }                // j

// hw/sh4/dyna: decoder.h, blockmanager.h, ngen.h, driver.cpp (all E4b)
struct DecodeLimits { u32 max_opcodes; u32 end_pc; s32 cycles; };        // 0 / 0 / negative: no limit, computed cost
void dec_DecodeBlock(RuntimeBlockInfo*, u32 max_cycles, const DecodeLimits* = 0);
void RuntimeBlockInfo::Setup(u32 pc, fpscr_t, const DecodeLimits* = 0);
void rdv_RunTempBlock(const DecodeLimits*);
void bm_DiscardAddrPage(u32 addr);
void bm_DiscardFlowBlocks(bool instrumented_only);
void bm_SetTempBlock(RuntimeBlockInfo*);
struct NGenBackend   { ... RunBlockFnPtr_t RunBlock; };
struct ngen_features { ... bool DebugChecks; bool CtxAtSlice; };         // recSH4::DebugCaps() reports these two
```

The two helpers are called from generated code, hence `extern "C"` and `DYNACALL`. The interpreter reports both
capability bits; the x64 JIT sets both feature flags. The debugger-side state behind these functions — the armed set and
its page bitmap, the exemption, the owed remainder, temporary breakpoints, step-out state — lives in `dbg_sh4.cpp`; run
control reaches it through `dbg_sh4.h` (6.1, contract 9).

#### 2.3.5 Other JIT backends, and the fallback

A JIT backend sets `ngen_features::DebugChecks` when it implements all of the following. In M1 only x64 does (in
Windows builds once the run of 5.8 has passed).

| Item | x64 (2.3.4) | arm64 | arm32 | x86 | cpp (cached SHIL) |
| --- | --- | --- | --- | --- | --- |
| `shop_dbg_check` | c | same shape; the flush comes from the shared `regalloc.h` | same | own register allocator: flush all | an `opcodeExec` that calls the helper |
| leave without time advance | `jmp mainloop_exit` | store `next_pc`, branch to `.end_mainloop` (`rec_arm64.cpp:173-184`; needs a global label) | branch to `cleanup` (`ngen_arm.S`) | jump to the loop epilogue (`rec_lin86_asm.S`) | `longjmp` to a `setjmp` at `rec_mainloop` entry (`rec_cpp.cpp:823-841`); the op chain has no early exit |
| refund | `add [cycle_counter]` | `add w27` in the stop path | `add r9` | `add [cycle_counter]` | `cycle_counter +=` |
| counter survives | static initialisation | `w27` loaded from / stored to the context at loop entry / exit (`:133`) | `r9` likewise (`ngen_arm.S:150`) | drop `mov cycle_counter, 448` (`rec_lin86_asm.S:106`) | drop `cycle_counter = 0` (`:827`), refill after the inner loop |
| `RunBlock` | generated thunk | the block must end by returning to the thunk, not by linking (`RelinkBlock`, `:868-952`; `rdv_LinkBlock` would chain it) | same | same | call `execute()` once |
| step-out report | epilogue (j) | in `RelinkBlock` for call / ret / intr ends, before the link or lookup | same | same | in the block-end op |
| `Sh4cntx.pc` at a stop | the helper writes it | same (the PC lives in `w29` between blocks, 2.2.8) | same (`r4`) | same | same |

arm32, arm64 and x86 test the counter at block *entry* and restart the block after an interrupt (`rdv_DoInterrupts`,
`hw/sh4/dyna/driver.cpp:328-352`); a check stop is independent of that path.

**Fallback** — only for a JIT backend that does not report `SH4DBG_NATIVE` (in M1: arm32, arm64, x86, cpp), and for the
test key `Debugger:ForceFallback=yes`, which makes the debugger treat the active JIT as such a backend:

```cpp
void SuperH4_impl::Run() {
    if (dbg::emu::UseFallbackLoop()) sh4_fallback_loop();     // hw/sh4/sh4_dbg.h
    else sh4_backend->Loop();
}
```

- `UseFallbackLoop()` is true while the backend lacks `SH4DBG_NATIVE` and an SH4 breakpoint or temporary breakpoint is
  active (2.3.3) or a step-out is pending. A step on such a backend is `sh4_fallback_step()`.
- The configured backend object and its block cache are never destroyed by the debugger (`setBackend` is not called).
- `UseFallbackLoop()` changes only through a `CC_TABLE` call (the first breakpoint becomes active; the last is removed,
  disabled, muted, or the client grace period expires). While running, that call runs in a service excursion — latched
  at a slice end (2.2.3) — in which `dbg::emu` sets `DBG_SWITCH`; `dc_run` then re-enters `Run()`, which selects the
  other loop, without parking. An event-log entry (`dc.debugger.engine`) records each switch.
- A fallback must charge time as the active timing mode does (D1). The fallback loop is the interpreter debug loop of
  2.3.4 k charging **by the decoder's blocks**: at each block start it decodes the block at PC
  (`RuntimeBlockInfo::Setup`, no compile), charges its `guest_cycles`, and interprets instructions until the block's end
  (a control transfer, or `next_pc == addr + sh4_code_size`) before it tests the counter. A stop inside a block refunds
  as in 2.3.4 b, and the resume runs to the same block end for the refunded cost. `sh4_fallback_step()` charges a
  one-instruction block the same way. That is the active timing mode's charge in every mode, because the decoder alone
  defines block boundaries and costs.
- The fallback loop has its own persistent slice counter. A backend that has not implemented "counter survives" does
  not meet R13: every exit and re-entry of its loop — a pause, a service excursion, a switch to or from the fallback
  loop — loses the partial slice (a skew below one slice each; documented).
- What the fallback cannot match is arithmetic: the interpreter's FPU paths differ from the JIT's (devcast-fpu.md). So
  while the fallback loop is selected, and in the tick of a fallback step, `executionState.notice` is
  `"SH4 runs on the interpreter while breakpoints are armed (JIT backend <name> has no native breakpoints): FPU results can differ from a run without breakpoints"`.
  A fallback shipped without block charging must also say in the notice that timing differs. Whether or not a
  breakpoint exists, `dc.timing.SH4_DEBUG_ENGINE` reads `interpreter-fallback` on such a backend (`native` otherwise)
  and `describe.capabilities` carries `"sh4-debug-fallback"` instead of `"sh4-debug-native"`.

#### 2.3.6 Gates for 2.3.3 to 2.3.5

Run by the selftest `sh4bp` (5.3), with scripts and expected values in `tests/debugger/sh4bp/`. Guest: 2ndmix.elf under
reios with a fixed RTC (`config:Debug.FixedRtc`, 5.6) and the `none` renderer in the `-DHAS_TA_LLE=OFF` build, started
as in 5.4 (a window opens; there is no headless mode before PLAN T2.0); B13 and B14 use a routine the test writes into
RAM itself (integer and FPU instructions, a delayed branch, a call). *State* is scheduler time, slice counter, PC, a
hash of the SH4 context and a hash of main RAM at a fixed vblank. Every state comparison uses a baseline taken in the
same session, and at least two baseline runs must agree; a comparison that differs is repeated twice and fails only if
it differs again (Appendix C, host determinism).

| Id | Test | Pass |
| --- | --- | --- |
| B1 | breakpoint at a block start, in the middle of a block, on the first instruction after boot | PC = the address; integer registers = the interpreter's at its first arrival |
| B2 | breakpoint in a loop, resumed each time: at the block start, mid-block, on the slot of the loop's `bf.s` | hit count = the interpreter's; the slot case reports PC = the branch; time at the loop exit = time without the breakpoint |
| B3 | state at vblank N with: tests that all answer continue (unreached, muted, no client); stop + resume at every arrival (ordinary block, idle-class block, SLEEP block, slot); two breakpoints in one block; `idleskip` off; blocks ended by the size limit; pause + resume and service excursions requested at every n-th slice | state = baseline in every case |
| B4 | 20000 single steps from the entry point | PC and integer registers after every step = the interpreter's |
| B5 | step over the reios trap; step on SLEEP; step with an odd PC, an unreadable PC, an undefined opcode, a branch in a slot | lands at `0x8C010000`; ends at the handler or on the SLEEP; `-32005`, nothing executed, process alive |
| B6 | arm an address in an already compiled, hot block while running; disarm after k hits | it hits; after disarming the helper is no longer called and state at vblank N = baseline |
| B7 | step over a call, a recursive call, `TRAPA`; step out of a leaf, of a non-leaf after it called a stack-less leaf, of a blocking call across interrupts; a step-out left pending for the whole run | stops at `PC + 4` / `PC + 2` at the request's level; stops at the caller; state at vblank N with the never-finishing step-out = baseline; interpreter and JIT stop at the same PC |
| B8 | breakpoint given as `0x0C..`, `0x8C..`, `0xAC..` | all hit |
| B9 | guest write into a page with an armed address (SMC path), then re-execution | hits again |
| B10 | PC written, reset, state loaded while stopped inside a block | the resume does not run a stale remainder |
| B11 | host instructions of (E) up to vblank N: `NULLDC_DEBUGGER=OFF`; ON with nothing armed; one armed address that is never reached | the first two within the run-to-run spread; the third within 0.01 % |
| B12 | `Debugger:ForceFallback=yes` on the x64 JIT | `SH4_DEBUG_ENGINE` and the `notice` report the fallback; breakpoints and steps work; on the integer part of the RAM routine, state with continue-only tests = the JIT's baseline (block charging); on 2ndmix the comparison is printed, not failed (FPU arithmetic differs) |
| B13 | interpreter backend: N instructions run free, and the same N by stepping | identical `Sh4cntx` and `sh4_sched_now64()` |
| B14 | JIT backend: N steps, and a free run to a breakpoint on the same Nth instruction, interrupts masked | identical guest registers, FPU registers bit for bit; scheduler time equal whenever the active cost function is additive over instructions, printed otherwise (`fast` charges a block as a unit) |

#### 2.3.7 Known limits and risks of the mechanism

1. **Guest threads and stack-level conditions.** Step over and step out identify "the same activation" by `R15`; a
   kernel-aware thread id is not available. Documented in the UI help: a function that does not return by `RTS` (a tail
   call by `JMP`, a context-switch primitive resumed by `RTE`) is left at the next enclosing return; a request made while
   extra words are pushed below the function's steady stack pointer can stop at the return of a later callee (still
   inside the function); another guest thread whose stack lies within 1 MiB above `sp0` can satisfy the test.
2. **Blocks keyed by PC only.** A cached block compiled under another FPSCR.PR / SZ runs with stale decode assumptions
   (`hw/sh4/dyna/blockmanager.h:39`, no entry check), while temporary blocks decode with the current FPSCR: a step and a
   run can differ exactly where the run is already wrong. PLAN T3.4 (blocks keyed on PR / SZ) removes it.
3. **Stop + resume costs a compile** (2.3.4 c). Hit counts, conditions and log points must be answered in the helper.
4. **Linking backends** need a non-linking block end for temporary blocks; until then they use the fallback.
5. **Order against the timing tracks.** The mechanism lands before PLAN T3.0 / T3.2 edit the decoder's cost code, the
   x64 prologue and the interpreter loop. Those tracks keep: the running cost is sampled in front of each instruction;
   a `DecodeLimits::cycles` override is honoured after all cost post-processing; the counter survives loop exit; cost
   terms charged at run time respect 2.3.4 b. Their gates include B3.
6. **`fpcb` aliasing** between the boot ROM and RAM (`driver.cpp:235-241` clears the cache at three boot addresses) is
   untouched. `rdv_RunTempBlock` applies the same rule, so stepping through those addresses clears the cache as the
   dispatcher would.
7. **Not prototyped:** the park protocol of 2.2 around the stop, the step preconditions, the arm / x86 / cpp backends,
   the fallback, MMU builds. The emitted x64 code has only run with the System V ABI; the Windows paths (`STACK_ALIGN`
   40, argument registers) follow the existing `WIN32_ONLY` conventions of `rec_x64.cpp` and are covered by 5.8.

### 2.4 Time while debugging

- The scheduler advances only in `UpdateSystem()` (one slice, 448 cycles); the debugger never advances it and never calls
  a scheduler callback itself.
- Every slice counter is persistent (R13, 2.3.4 d): leaving and re-entering `Run()` — stop and resume, a service
  excursion, a host pause at a slice end — is invisible to the guest.
- A code-breakpoint stop shows the time charged for the executed part of the block (R7): exact with a cost model that is
  additive over instructions; in `fast` timing the proportional share (an ordinary block shows nothing charged until it
  completes). The resume charges the rest, so stop + resume is neutral in every mode (R8).
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
     its 1002-slice bound start again), and under a JIT it is re-executed until the slice counter runs out before the
     park (up to 448 times, charged to the slice counter without scheduler time);
  2. a dropped remainder — the PC was written, or a step, reset or state load was made during a stop inside a block: in
     `fast` timing up to one block cost;
  3. on a backend that uses the fallback: the interpreter's FPU arithmetic while the fallback loop runs, and, until
     that backend's counter persists, the partial slice at every exit and re-entry of its loop (2.3.5).
- Tracks 3/4 move the cycle counter into `Sh4Context` and change the cost model; what they must keep is 2.3.7 item 5.
- `dc.timing` (2.8) shows the mode, the counters, the backend and the debug engine.

### 2.5 Sound side (M2, M3): the resumable burst

Today (`libswirl.cpp:779-795`, `hw/arm7/SoundCPU.cpp:205-214`, `hw/aica/aica_mmio.cpp:740-753`): one scheduler callback
runs, if the ARM is enabled, 32 x { ARM7 for 256 ticks; `libAICA_TimeStep()` } — where `TimeStep` steps the timers, and in
`NoBatch` mode renders one sample (64 channels, DSP, mix, output), then updates the interrupt lines — followed in batch
mode by one 32-sample render with no DSP. With the ARM held in reset there is no `TimeStep` at all.

#### 2.5.1 Round-1 part (E4a)

The stop latch of 2.2.3 (rules 1 to 3, with the distinction between freezing and deferrable causes) and the leave order
of 2.2.4 are implemented in round 1; the sound side depends on their exact semantics. Rule 4 and the rest of 2.5 are
M2 / M3.

#### 2.5.2 The burst machine (M2)

The machine is owned by `Dreamcast_impl::AicaUpdate` (a file-local struct), because the batch render is called from there
after `SoundCPU::Update` returns. Latched at burst start: `arm_on = ctx.enabled`,
`per_sample = settings.aica.NoBatch || dbg::force_per_sample`. Sequence:

```
if (arm_on) for i in 0..31 { ARM(i); TS_TIMERS(i); if (per_sample) { SGC(i); DSP(i); MIX(i); } TS_IRQ(i); }
if (!per_sample) SAMPLE32
```

State: `{i, phase, arm_ticks_done, mixl, mixr, dsp_step, arm_on, per_sample}` (`mixl` / `mixr` are locals that cross the DSP
phase today, `hw/aica/sgc_if.cpp:1338`). Suspension points: before an ARM instruction, before a DSP step, nowhere else.

- A sound breakpoint suspends the machine: the callback records the state, requests a pause (`ATTN_PAUSE` with the
  breakpoint as cause, a freezing cause) and returns `AICA_TICK` as usual; the slice hook of the same `UpdateSystem()`
  latches the stop — also inside a SLEEP wait and during an SH4 step — the latch freezes the scheduler, and (E) parks.
  The callback starts with `verify(!burst.suspended)`.
- **A suspended burst is always completed before the SH4 executes anything** (2.2.4 a).
- Scheduler callbacks that share the tick of a suspended burst run before the burst completes; this reordering exists
  only in a debug stop and is not covered by the bit-identity gate. SH4 interrupt acceptance stays after the burst.
- Lifecycle: a host stop, exit and save state first complete the burst with sound breakpoints suppressed (the machine
  state is never serialised); load state and reset discard it; when completing a burst for an SH4 resume or step hits a
  sound breakpoint, the machine stays parked, the tick is `paused` with that `breakpointId`, and the SH4 command is
  dropped.
- Gate: with no sound breakpoint armed the sequence of calls is exactly today's, and the hashes of 5.6 are identical
  before and after the change.

#### 2.5.3 ARM7 breakpoints and steps (M2)

- `ARM7Backend::Step` is split into `StepIrq(ctx)` (the pending-FIQ entry, `hw/arm7/arm7.cpp:288-291`) and `StepExec(ctx)`
  (the rest); `Step` calls both; no behaviour change.
- The ARM phase of the burst is `if (unlikely(dbg::arm7_armed)) RunDbg(); else arm->Run(256);` — one flag test per sample,
  nothing per instruction. `RunDbg`:
  `loop { StepIrq; if (!skip_once && hit(regs[R15_ARM_NEXT] & aram_mask)) suspend; ticks += StepExec; if (ticks >= 256) break; }`,
  calling the interpreter statics directly (no backend swap). The test is after `StepIrq`, so a breakpoint on the FIQ
  vector `0x1c` is hit and a hit at X is not followed by a silent detour through the handler. Breakpoint addresses are
  compared after `& aram_mask`. `skip_once` is the resume exemption (one instruction, as R9).
- On hosts where the ARM7 runs through the interpreter (every x86-64 build) this is native: what is debugged is what
  runs. On hosts with the ARM7 JIT (x86-32, arm64) `RunDbg` is an interpreter fallback on the same context (all JIT
  state lives in `ctx->regs` between `Run` calls, so no backend swap is needed): ARM7 timing differs while an ARM7
  breakpoint is armed (interpreter cost table instead of 9 ticks per opcode with carry) and ticks carry the `notice`
  `"ARM7 runs on the interpreter fallback: timing differs"`.
- `control.step {arm7}`: if a burst is suspended in the ARM phase, execute one ARM instruction; otherwise resume with a
  one-shot "break before the next ARM instruction" (the SH4 runs until the next burst, at most one `AICA_TICK`).
  `stepOver`: a temporary breakpoint at `PC + 4` after a `BL`, else a step. `stepOut`: a temporary breakpoint at `LR`
  (heuristic, documented in the UI: correct for leaf code and before `LR` is saved or overwritten).
- With the ARM held in reset (`ctx.enabled` false) nothing is clocked: `control.step / stepOver / stepOut {arm7}` and
  `breakpoints.add` for `dc.aica.arm7.pc` return the in-band error `-32012 "arm7 held in reset"`.

#### 2.5.4 DSP steps and breakpoints (M3, on the T5.0 core)

- The DSP is the caique-matching C++ core of PLAN T5.0: per-step execution with persistent state by construction. Its
  interface exposes `Begin()` (start-of-sample work; returns false when the core skips an empty program),
  `StepRange(first, last)` and `End()` (end-of-sample work, `MDEC_CT`); the atomic per-sample run is
  `if (Begin()) { StepRange(0, 128); End(); }`. The step index lives in the burst machine, not in the core; the debugger
  adds no field to any serialised structure (the save-state layout change, if any, belongs to T5.0). Program changes
  (`dyndirty`) are evaluated only in front of `Begin()`.
- A DSP breakpoint (`dc.aica.dsp.step`, address 0..127) is tested before each step when armed; `control.step {dsp}`
  executes one step when the burst is suspended in the DSP phase, else resumes with a one-shot "break before the next DSP
  step".
- If the core skips an empty program, the burst machine still walks 128 steps without executing them while a DSP
  breakpoint or step is armed, leaving all DSP state exactly as the skipped path does, so stepping an empty program works.
  With the ARM in reset the AICA is not clocked: the in-band error `-32012 "aica not clocked (arm7 in reset)"`.
- While a DSP breakpoint or step is armed the DSP runs through the core's step interface on every host, switched at a
  sample boundary. On a host that runs a DSP JIT this replaces the JIT for that time; T5.0 requires the JITs to match
  the core (or drops them), so the output is the same and no notice is needed.
- The same C++ core, built with emscripten, replaces the Rust `crates/aica-dsp` module behind the devtools playground, so
  the playground, the debugger's DSP view and the emulator execute identical DSP semantics.

#### 2.5.5 Per-sample forcing (M2 / M3)

DSP steps, DSP breakpoints and SGC captures need the per-sample path. `dbg::force_per_sample` is a debugger-owned flag,
OR-ed with `settings.aica.NoBatch` in the burst latch; `settings` is never written (it is the GUI checkbox "Enable DSP"
and is saved to `emu.cfg`). The flag changes only at a burst boundary (a flip inside a burst would drop or duplicate
samples). While it is on and the user's setting is off, the event log says so once (`dc.debugger.dsp_forced`),
`describe.capabilities` carries `"dsp-forced"` and ticks carry the `notice`
`"DSP per-sample path forced on by the debugger: audio is the DSP mix"`; audio is then the DSP mix, not the batch dry mix
(documented). An active SGC capture sets the flag for its duration, so frames are captured in `AICA_Sample` order only.

### 2.6 Events

```cpp
#define DBG_EVENT(id, ...) do { if (unlikely(dbg::g_event_on[id])) dbg::EventSlow(id, __VA_ARGS__); } while (0)
void dbg::EventSlow(EventId id, const char* fmt, ...);     // printf-style message
```

- Callable from any thread: device reset / unserialize code runs on (U), rendering on (U) and the refsw worker pool. The
  ring is guarded by a small mutex. Event breakpoints are evaluated only when the caller is the emulator thread
  (`dbg::emu::OnEmuThread()`).
- `g_event_on[id]` is a byte per event, maintained by `dbg_core`: 1 iff at least one client is connected and (the event is
  low-rate, or an event breakpoint on it exists). With no client the cost of a call site is one byte test; the arguments
  are not evaluated.
- The ring holds 200 entries; every tick carries the whole ring, oldest first.
- Wire fields: `eventId` is a decimal sequence number (as a string), unique per process; `timestamp` is wall-clock ms;
  `subsystem` and `severity` from the table; `message` is the formatted text; `metadata` is
  `{ "event": "<event name>", "cycles": "<sh4_sched_now64(), decimal>" }`.
- Events are divided into **low-rate** (logged whenever a client is connected) and **high-rate** (can fire at frame rate
  or faster; logged only while an event breakpoint on that event exists). Only events that are really emitted are listed
  in the device tree (`events` of the node shown). Event ids are constants in `dbg_events.h`, so call sites (E3, E4a)
  and the device tree (E5) agree.

| Constant | Event name | Subsystem | Rate | Node | Raised in (package) |
| --- | --- | --- | --- | --- | --- |
| `EV_SH4_INTERRUPT` | `dc.sh4.interrupt` | `sh4` | high | `dc.sh4` | `hw/sh4/sh4_interrupts.cpp` `Do_Interrupt` (E4a) |
| `EV_SH4_EXCEPTION` | `dc.sh4.exception` | `sh4` | low | `dc.sh4` | `Do_Exception` (E4a) |
| `EV_DMAC_START`, `EV_DMAC_END` | `dc.holly.dmac.transfer_start`, `.transfer_end` | `holly` | high | `dc.holly.dmac` | ch2 and sort DMA handlers (E3) |
| `EV_TA_LIST_INIT`, `EV_TA_LIST_END` | `dc.holly.ta.list_init`, `.list_end` | `ta` | high | `dc.holly.ta` | `hw/pvr` (E3) |
| `EV_CORE_RENDER_START`, `EV_CORE_RENDER_END` | `dc.holly.core.render_start`, `.render_end` | `core` | high | `dc.holly.core` | `hw/pvr` (E3) |
| `EV_CORE_VBLANK` | `dc.holly.core.vblank` | `core` | high | `dc.holly.core` | `hw/pvr/spg.cpp` (E3) |
| `EV_AICA_INTERRUPT` | `dc.aica.interrupt` | `aica` | high | `dc.aica` | `hw/aica/aica_mmio.cpp` (E3) |
| `EV_AICA_TIMER` | `dc.aica.timer` | `aica` | high | `dc.aica` | `hw/aica/aica_mmio.cpp` (E3) |
| `EV_AICA_KEY_ON`, `EV_AICA_KEY_OFF` | `dc.aica.channels.key_on`, `.key_off` | `aica` | low | `dc.aica.sgc` | `hw/aica/sgc_if.cpp` (E3) |
| `EV_AICA_LOOP` | `dc.aica.channels.loop` | `aica` | high | `dc.aica.sgc` | `hw/aica/sgc_if.cpp` (E3) |
| `EV_GDROM_COMMAND` | `dc.gdrom.command` | `gdrom` | low | `dc.gdrom` | `hw/gdrom/gdromv3.cpp` (E3) |
| `EV_MAPLE_DMA` | `dc.maple.dma` | `maple` | high | `dc.maple` | `hw/maple/maple_if.cpp` (E3) |
| `EV_SYSTEM_RESET`, `EV_SYSTEM_STATE` | `dc.system.reset`, `dc.system.state` (lifetime changes, load state) | `system` | low | `dc` | `libswirl.cpp` (E4a) |
| `EV_DEBUGGER_ENGINE`, `EV_DEBUGGER_DSP_FORCED`, `EV_DEBUGGER_NOTE` | `dc.debugger.engine`, `.dsp_forced`, `.note` | `debugger` | low | `dc` | `dbg_emu.cpp`, `dbg_core.cpp` |

Severity is `info` for all of them except `dc.sh4.exception` (`warn`).

### 2.7 Guest memory for the debugger (`dbg_mem`)

Byte-granular; never calls a guest handler; runs on (E) or under L (2.2). Backing stores are re-read from the machine on
every call (no cached pointers).

Target `sh4`. Addresses below `0xE0000000` are masked with `0x1FFFFFFF`; P4 is handled separately.

| Region (after masking) | Backing |
| --- | --- |
| area 0, as a whole masked with `0x01FFFFFF` (so `0x02000000-0x03FFFFFF` mirrors it) | — |
| `0x00000000-0x001FFFFF` BIOS, `0x00200000-0x0021FFFF` flash | `sys_rom`, `sys_nvmem` data through the accessors E0 declares in `hw/holly/sh4_mem_area0.h` (the objects are file globals whose types depend on the platform) |
| `0x005F6800-0x005F7CFF` system-bus registers, **except** `0x005F7000-0x005F70FF` | `dbg_regs` safe readers |
| `0x005F7000-0x005F70FF` GD-ROM | not readable (its reads have side effects); the GD-ROM state is a device snapshot in the device tree (M2) |
| `0x005F8000-0x005F9FFF` PVR registers | `pvr_regs` array |
| `0x00600000-0x006007FF` modem | not readable |
| `0x00700000-0x00707FFF` AICA registers, `0x00710000-0x0071000B` RTC | register arrays via `dbg_regs` safe readers (not `ReadCommonReg`, which rewrites live fields) |
| `0x00800000-0x00FFFFFF` AICA RAM | `aica_ram[addr & (aica_ram.size - 1)]`; the size is per platform, read from `sh4_cpu->aica_ram.size` |
| area 1: 64-bit VRAM view `0x04000000`, `0x06000000`; 32-bit view `0x05000000`, `0x07000000` | `vram[addr & VRAM_MASK]`; `vram[pvr_map32(addr)]` per byte (the low two bits pass through) |
| area 3: `0x0C000000-0x0FFFFFFF` | `mram[addr & RAM_MASK]` |
| P4 store queues `0xE0000000-0xE3FFFFFF` | `sq_both[addr & 63]` |
| P4 / area 7 on-chip registers | `dbg_regs` safe readers |
| anything else | not readable |

Target `arm7`: wave RAM below `0x00800000` (`aica_ram[addr & (size - 1)]`), AICA registers from `0x00800000` (as above),
anything else not readable. Target `dsp` has no memory (`-32004`).

Reads (3.3): `state.getMemorySlice` always returns `length` bytes; unreadable bytes are 0 and are listed in `unreadable`;
the JSON-RPC error `-32010` is returned only when nothing in the range is readable. `state.getDisassembly` always returns
`count` lines; an unreadable word gives `bytes: "??"` and `disassembly: "<unreadable>"`.

Writes (`state.writeMemory`, M2; `CC_WRITE`, so always on (E) in the park context): main RAM, VRAM and AICA RAM only;
anything else is refused with `-32013`, and the range is checked as a whole before the first byte is stored.
- Main RAM: for every touched 4 KiB page call `bm_LockedWrite(&sh4_cpu->mram.data[page_offset])` before storing (it
  discards all blocks on the page, marks it as data and unprotects it), inside `#if FEAT_SHREC != DYNAREC_NONE`. It works
  only when nvmem is active (`virt_ram_base != 0`); otherwise the write is refused. If the page holds SH4 breakpoints,
  nothing else is needed: the decoder asks again at every decode, and an owed remainder is decoded from the memory as it
  is at the resume (2.3.4 g, h).
- VRAM pages can be locked by the texture cache: call `VramLockedWrite(vram.data, ptr)` first.
- A write that touches AICA RAM (either target) calls `SoundCPU::InvalidateJitCache()` (the ARM7 JIT has no
  self-modifying-code detection).
- Never rely on the SIGSEGV path from a thread other than (E).

`WITH_MMU` builds use the same fixed map (1.1).

### 2.8 Device tree and watches

Structure and register names come only from `debugger.describe`; ticks update values. Paths are lower case and opaque to
the client; register names are upper case and unique within a node. The mock's 29 paths, labels and actions
(devtools-protocol.md Appendix A) are kept; nesting follows the table below (it does not have to follow the dots).
Second-level nodes (`dc.sh4`, `dc.holly`, `dc.aica`, `dc.system`) carry no registers, because the panel expands the first
two levels by default.

| Path (parent) | Registers | Events | Actions | From |
| --- | --- | --- | --- | --- |
| `dc` | `SYSCLK` (text), `BUILD` (text, `nullDC 3.0.0 <git revision>`) | `dc.system.reset`, `dc.system.state`, `dc.debugger.engine`, `dc.debugger.dsp_forced`, `dc.debugger.note` | — | M1 |
| `dc.sh4` (`dc`) | — | `dc.sh4.interrupt`, `dc.sh4.exception` | — | M1 |
| `dc.sh4.cpu` (`dc.sh4`) | `PC PR SR GBR VBR MACH MACL SSR SPC SGR DBR FPSCR FPUL`, `R0..R15`, `R0_BANK..R7_BANK`, `FR0..FR15`, `XF0..XF15` (raw hex) | — | `sh4-disassembly`, `sh4-memory`; `sh4-registers` from M2 | M1 |
| `dc.sh4.pbus` (`dc.sh4`) | — | — | — | M1 |
| `dc.sh4.{bsc,ccn,cpg,dmac,intc,rtc,sci,scif,tmu,ubc}` (`dc.sh4.pbus`) | every on-chip register of the module, table generated from the `*_addr` definitions of `hw/sh4/sh4_mmr.h` | — | `<name>-registers` | M1 |
| `dc.sh4.{sq,icache,ocache,ocram,tlb}` (`dc.sh4.pbus`) | — | — | `<name>-contents` (panels filled in M4) | M1 |
| `dc.holly` (`dc`) | — | — | — | M1 |
| `dc.holly.intc` (`dc.holly`) | `SB_ISTNRM SB_ISTEXT SB_ISTERR`, `SB_IML{2,4,6}{NRM,EXT,ERR}` | — | — | M1 |
| `dc.holly.dmac` (`dc.holly`) | ch2, sort, G1, G2 DMA registers of the system bus | `dc.holly.dmac.transfer_start`, `.transfer_end` | — | node M1, registers M2 |
| `dc.holly.sb.{g1,g2,pvr,misc}` (`dc.holly.sb`, itself under `dc.holly`) | the remaining system-bus registers by group | — | — | M2 |
| `dc.holly.ta`, `dc.holly.core` (`dc.holly`) | PVR registers split by function | `dc.holly.ta.list_init`, `.list_end`; `dc.holly.core.render_start`, `.render_end`, `.vblank` | `holly-ta`; `holly-core` | nodes M1, registers M2 |
| `dc.holly.spg` (`dc.holly`) | PVR sync / timing registers | — | — | M2 |
| `dc.aica` (`dc`) | — | `dc.aica.interrupt`, `dc.aica.timer` | — | M1 |
| `dc.aica.common`, `dc.aica.rtc` (`dc.aica`) | common registers (the `0x2800` block); RTC | — | — | M2 |
| `dc.aica.arm7` (`dc.aica`) | `PC`, `R0..R14`, `CPSR`, `SPSR`, banked registers (below) | — | `arm7-disassembly`, `arm7-memory`; `arm7-registers` from M2 | M1 |
| `dc.aica.sgc` (`dc.aica`) | — | `dc.aica.channels.key_on`, `.key_off`, `.loop` | `sgc` | M1 |
| `dc.aica.sgc.0 .. .63` (`dc.aica.sgc`) | per-channel register fields | — | — | `.0`, `.1` as nodes without registers in M1; all 64 with registers in M2 |
| `dc.aica.dsp` (`dc.aica`) | `STEP` (hex), `MDEC_CT`, `RBP`, `RBL`; the core's latches from M3 | — | `dsp-disassembly` | M1 (`STEP` reads 0 until M3) |
| `dc.system` (`dc`) | — | — | — | M1 |
| `dc.gdrom`, `dc.maple` (`dc.system`) | controller / state registers (device snapshots) | `dc.gdrom.command`; `dc.maple.dma` | — | nodes M1, registers M2 |
| `dc.timing` (`dc.system`) | `MODE` (text, `fast`), `SH4_CYCLES` (64: `sh4_sched_now64() + SH4_TIMESLICE - SliceCyclesLeft()`, the *time* of 2.3.4), `SCHED_NOW` (64: `sh4_sched_now64()`), `SH4_BACKEND` (text), `SH4_DEBUG_ENGINE` (text: `native` or `interpreter-fallback`, 2.3.5; `n/a` before a backend is selected); track 3 adds more | — | — | M1 |

Register values: `"0x"` + upper-case hex zero-padded to `ceil(width / 4)` digits; a register with `width: 0` is free text
and read-only; `"n/a"` when the value cannot be read without a side effect. A register whose read has side effects is
read through the register array, never through the handler; function-backed registers (devcast-debug-surface.md 4.4, 11)
use a dedicated side-effect-free reader in `dbg_regs.cpp` (`TMU_TCNTn` from the formula, `SB_ISTNRM` through the ASIC
accessor, `SB_FFST` without the read counter) and are `"n/a"` when there is none (`SCIF_SCFRDR2`).

`dc.aica.arm7`: `PC` is `regs[R15_ARM_NEXT]` (not `regs[15]`, which is `PC + 8` during an instruction and stale between
them; `R15` is not shown). `CPSR` is composed from `RN_PSR_FLAGS`, `armIrqEnable`, `armFiqEnable`, `armMode`
(`regs[16]` is a cache). The active mode's `R13` / `R14` are in `regs[13/14]`; the banked slot of the active mode is stale
until the next mode switch, so the reader substitutes the live values. Writes (M2): `CPSR` through `CPUSwitchMode` +
`CPUUpdateFlags`, `PC` into `R15_ARM_NEXT`.

Which nodes a tick carries: a **full** tick carries every node that has registers; a **running** tick carries
`dc.sh4.cpu`, `dc.aica.arm7`, `dc.aica.dsp`, `dc.timing` (the base paths). While running, registers outside the base
paths refresh at the full-tick rate (1 s, 2.9). On a backend without `SH4DBG_CTX_AT_SLICE` running ticks omit `dc.sh4.cpu`
and set `registersStale: true`; the client renders that path dimmed.

Watches:
- The expression grammar is `<node path>.<register name>`; the last component is resolved case-insensitively; the stored
  expression is normalised to lower case; an expression that is already stored is not added twice (the call still returns
  `{}`); `state.watch` with an expression that resolves to no register returns the in-band error `-32602` and adds nothing.
- Watch ids are allocated from 1. No default watches.
- Watches are evaluated in every tick, in id order, whatever paths the tick carries; a value that cannot be read is
  `"n/a"`.
- `state.editWatch` returns `-32004` until M2; from M2 it is `state.setRegister` on the watched register.

### 2.9 Tick policy

`event.tick` is the only notification. It is a snapshot of everything except `registers`, which the client merges per
path (3.2). Ticks are built on (E) by the run-state listener or a `CC_READ` call, or under L when no emulator thread
exists.

- To the requesting client right after the `debugger.describe` response (response first, tick second): always full.
- Broadcast after every successful mutating RPC (breakpoints, watches, categories, writes).
- Broadcast on every state change: resume (`running`), every stop (`paused`, new `stopId`), every lifetime change.
- Broadcast every `Debugger:TickMs` (200 ms) while `Running` and at least one client is connected. Every
  `Debugger:FullTickEvery`-th periodic tick (default 5, so 1 s) is full. If (E) does not service the periodic call within
  2 s the tick is skipped.
- A paused tick is always full.
- Slow clients: a periodic tick is not queued for a client whose `QueuedBytes()` exceeds 1 MiB; that client is marked and
  the next tick sent to it is full. No other tick is ever dropped by the server.
- `tickId` increments per tick built; `timestamp` is wall-clock ms.
- Running ticks carry `callstacks: {"sh4": [], "arm7": []}`.
- Client-absent grace: 10 s after the last client disconnects, breakpoints are treated as inactive (not shown in
  `categories`) and a debugger park is released; they are active again when a client connects. The timer starts only at a
  disconnect, so a `StartPaused` park waits for its first client without limit.
- JSON emission follows 3.0 (never `null`, `{}` for success, required keys always present).

### 2.10 Callstack and symbols

- Symbols: `reios_loadElf` (`reios/reios_elf.cpp`) hands the loaded image to `dbg::symbols::OnElfLoaded(const void* image,
  size_t size)` on (E), on every boot / reset; `dbg_symbols` walks `.symtab` itself and replaces its table. The table is
  read only inside calls and listeners (the one rule), so it needs no lock. `symbol` is `name+0xOFF`; branch targets in
  disassembly text and callstack frames use it. No ELF, no symbols.
- SH4 unwinder (heuristic, labelled as such in the UI): frame 0 = PC. Frame 1 = `PR` with `location: "pr"` — for
  non-leaf code this is a location inside the current function, so it is not presented as the caller. Further frames by
  scanning up to 1024 words upward from `R15` for values `v` that are even, executable (RAM or ROM, compared with the mask
  of 2.3.3) and where the instruction at `v - 4` is `BSR / BSRF / JSR`; `location: "stack+0x.."`. At most 32 frames in a
  tick.
- ARM7: frame 0 = PC, frame 1 = `LR` (`location: "lr"`), then the same scan with `BL` at `v - 4`.
- Sent in paused ticks (`callstacks.sh4`, `callstacks.arm7`) and through `state.getCallstack` (`maxFrames` default 32,
  at most 64). Running ticks carry empty arrays.

### 2.11 Serving the UI

Configuration: section `Debugger`; the command-line form is `-config Debugger:Port=0` (existing keys are written the
same way, `-config audio:backend=none`).

| Key | Default | Meaning |
| --- | --- | --- |
| `Enabled` | `yes` | start the server at `dbg::Init` |
| `Bind` | `127.0.0.1` | listen address. Anything but a loopback address requires `AllowedHosts` to be set and logs a warning that the debugger is reachable without authentication |
| `Port` | `55543` | `0` asks the OS for a free port |
| `Required` | `no` | `yes` makes a failed bind fatal (exit code 2). With `no`, a busy port logs a warning and the emulator continues without the server |
| `AllowedHosts` | empty | comma-separated extra host names accepted in `Host` and `Origin` |
| `WebRoot` | empty | directory served instead of the embedded bundle |
| `StartPaused` | `no` | 2.2.9 |
| `TickMs` | `200` | period of the running tick |
| `FullTickEvery` | `5` | every n-th periodic tick is full |
| `SelfTest` | empty | run the in-emulator selftest (5.3) and exit |
| `ForceFallback` | `no` | testing only: treat the active SH4 JIT backend as one without native breakpoints (2.3.5, gate B12) |

At start the emulator prints exactly `Debugger: http://<bind>:<port>/` with the real port (test harnesses parse it).

HTTP and WebSocket rules (implemented by E1):
- One listening socket. Only `GET` is served (405 otherwise). `GET /ws` is the WebSocket upgrade; no
  `Sec-WebSocket-Protocol` is required; every other `GET` is a static file. The query string is ignored.
- Every request (static file and upgrade) is rejected with 403 unless the `Host` header, port stripped, is one of
  `127.0.0.1`, `localhost`, `[::1]` or an entry of `AllowedHosts` (case-insensitive). This is the defence against DNS
  rebinding.
- The upgrade additionally requires `Origin` to be absent, or to parse as `http(s)://<host>[:port]` with `<host>` exactly
  (case-insensitive) in the same list; the literal `null` is rejected.
- Static files, in order: `WebRoot`, the embedded bundle, the built-in page (it says where to get the UI and names
  nullDC 3.0.0). A path that is not found falls back to `index.html` only when it has no file extension; otherwise 404.
  `index.html` is sent with `Cache-Control: no-cache`, hashed assets (`assets/*`) with `max-age=31536000, immutable`.
  MIME table: html, js, css, json, wasm (`application/wasm`), png, svg, ico, ttf, woff2, map. Every response carries
  `X-Content-Type-Options: nosniff`. Embedded files are stored gzip-compressed and sent with `Content-Encoding: gzip`.
- Limits: 1 MiB per inbound message; 16 clients (a further upgrade gets 503); a client that lets more than 16 MiB of
  outbound data queue up is disconnected. Request parameters are bounded as in 3.6.
- All socket writes use `MSG_NOSIGNAL` (Linux) / `SO_NOSIGPIPE` (macOS); `dbg::Init` does not change process signal
  dispositions. On Windows the listening socket uses `SO_EXCLUSIVEADDRUSE`, never `SO_REUSEADDR`.

Embedding the UI (`debugger.cmake` with `cmake/dbg_webui.cmake` at configure time and `cmake/dbg_webui_embed.cmake` at
build time; as E1 built it):
- `npm run build:native` writes `dist-native/bundle-info.json`:
  `{version, protocol, gitRev, files: [{path, bytes, sha256}]}` (F1).
- Cache variable `NULLDC_DEVTOOLS_DIST`: a `dist-native` directory; empty (the default) for auto-detection of
  `<devcast>/devtools/dist-native`, then `<devcast>/../dreamcast-devtools/dist-native`; `OFF` to embed nothing. In a
  package worktree the sibling path does not exist, so the variable is passed explicitly. Configure prints
  `nullDC devtools UI: embedding <dir> (<version>, protocol <n>)` or `nullDC devtools UI: not embedded (<reason>)`.
- A bundle whose `protocol` differs from `dbg::kProtocolVersion` is a configure error. So is a directory named
  explicitly that has no `index.html` or no `bundle-info.json`.
- Embedding needs CMake 3.19. With an older CMake an auto-detected bundle is not embedded (stated at configure time); an
  explicitly named one is a configure error.
- The embed command runs at build time, `DEPENDS <dir>/index.html <dir>/bundle-info.json`, globs the directory itself
  (asset names are content-hashed), gzip-compresses each file (`file(ARCHIVE_CREATE ... FORMAT raw COMPRESSION GZip)`,
  with the timestamp bytes of the gzip header cleared so that the output depends on the content only) and emits
  `dbg_webui_data.cpp` in the build directory as brace-initialised `unsigned char` arrays with line breaks (the only form
  portable to MSVC).
- With no bundle the generated file holds an empty table and the built-in page is served. `dbg_webui.h` exposes the
  bundle info (`Embedded()`, `Version()`, `GitRev()`); `debugger.describe` reports `ui: "none"` or the bundle version.
- The client detects a mismatched bundle served from `WebRoot` or a dev server by comparing `describe.protocol` with its
  own `PROTOCOL_VERSION` (3.0).

## 3. Protocol v3 (schema + client + mock + emulator change together)

Protocol v2 is devtools-protocol.md; everything in it stays valid unless this section changes it. "v3 server" below means
the mock and the emulator; both implement this section, and the contract runner (5.5) holds both to it.

### 3.0 Rules

- **Framing.** JSON-RPC 2.0, one message per WebSocket text frame, UTF-8. No batches: a JSON array is answered with
  `-32600`. Requests carry a numeric `id`, echoed as a JSON number. The server sends no requests, only responses and the
  notification `event.tick`. Binary frames are described in 3.4.
- **Versioning.** `debugger.describe` params gain optional `protocol` (the client's `PROTOCOL_VERSION`, 3) and `client`
  (free text, logged). The result carries `protocol: 3`. The server answers whatever the client's number is. The client
  compares `describe.protocol` with its `PROTOCOL_VERSION` and shows a banner on mismatch; an absent `protocol` means a v2
  server, and then absent `targets`, `categories`, `stopId` select the v2 behaviour and binary frames have no header.
- **Additive fields.** The client's zod objects strip unknown keys, so new optional fields are safe for an old client.
  New fields are optional in the schema (a v2 server omits them); "always" below is the v3 server's obligation.
- **Open enums.** In server-to-client messages the client parses `subsystem`, `severity`, `validity`, `actions` entries
  and `reason` as open strings (`z.string()`); it renders an unknown `subsystem` / `severity` neutrally and ignores an
  unknown action. The server sends only the values listed in this section. Closed and unchanged: `state`
  (`running | paused`), `kind` (`code | event`), `build` (`native | wasm`), `target` (`sh4 | arm7 | dsp`).
- **JSON emission (both servers; nlohmann's default value is `null`, so this is enforced in typed serialisers).**
  1. `null` never appears (the single exception is the `id` of a `-32700` response). Optional fields are omitted.
  2. The success result of every method whose result type is `RpcError` is the empty object `{}`.
  3. Every tick has the keys `tickId`, `timestamp`, `executionState`, `registers`, `breakpoints`, `eventLog`, `watches`,
     `callstacks`, `categories` (objects and arrays possibly empty).
  4. Every node has `path`, `label`, `description` (possibly `""`); every register has `name`, `value`, `width`.
  5. Empty `actions`, `events`, `children`, `registers` arrays of a node are omitted.
  6. Addresses and ids are JSON numbers (addresses unsigned, 0..4294967295); register values are strings.
  7. `error` objects have `code` and `message` only; no `data`, no stack trace.

### 3.1 `debugger.describe`

Params: `{ protocol?: number, client?: string }`. Result:

```ts
{ emulator: { name: "nullDC", version: "3.0.0", build: "native" },
  protocol: 3,                 // always
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

| `id` | `pcPath` / `pcRegister` | `breakpointEvent` | size | `addressMax` | radix / digits | default disassembly / memory | `memory` | `stepping` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `sh4` | `dc.sh4.cpu` / `PC` | `dc.sh4.cpu.pc` | 2 | `0xFFFFFFFF` | 16 / 8 | `0x8C010000` / `0x8C010000` | true | true |
| `arm7` | `dc.aica.arm7` / `PC` | `dc.aica.arm7.pc` | 4 | `0x00FFFFFF` | 16 / 8 | `0x00000000` / `0x00000000` | true | false in M1, true from M2 |
| `dsp` | `dc.aica.dsp` / `STEP` | `dc.aica.dsp.step` | 1 | 127 | 10 / 3 | 0 / — | false | false until M3 |

- A target missing from a present `targets` array is unsupported: its panels show a notice instead of controls. A listed
  target supports `state.getDisassembly`; `memory` and `stepping` say what else works. When `targets` is absent (v2
  server) the panels fall back to today's literals.
- `capabilities` (open strings) say which optional methods work: M1 `"event-breakpoints"`, `"callstack"`; M2 adds
  `"memory-write"`, `"register-write"`, `"reset"`; M3 adds `"sgc-capture"`, `"memory-binary"`; M4 adds `"tables"`,
  `"holly-capture"`. Three more describe the emulator at the moment of the call: `"sh4-debug-native"` or
  `"sh4-debug-fallback"` (2.3.5; neither before an SH4 backend is selected) and, while per-sample forcing is on against
  the user's setting, `"dsp-forced"` (2.5.5). Their live form is the tick: `notice` and `dc.timing`.
- `deviceTree` is 2.8. `actions` entries are panel ids: the 35 ids of protocol v2 in M1; `sh4-registers` and
  `arm7-registers` are added in M2 (schema and server together).
- In `NoMachine` the result is the static tree with every register value `"n/a"`.

### 3.2 `event.tick`

```ts
{ tickId: number, timestamp: number,
  executionState: {
    state: "running" | "paused",
    stopId: number,              // always; 2.3.1
    reason?: string,             // always in paused ticks; values in 2.3.1
    breakpointId?: number,       // in every paused tick of a stop caused by a breakpoint
    registersStale?: boolean,    // only when true; 2.2.8
    notice?: string },           // only when a fallback or forcing statement applies (2.3.5, 2.5.3, 2.5.5): one line per
                                 // statement, joined with "\n", each at most 200 characters
  registers: Record<string, RegisterValue[]>,
  breakpoints: Record<string, BreakpointDescriptor>,      // key = decimal id
  eventLog: EventLogEntry[],                               // at most 200, oldest first; 2.6
  watches: WatchDescriptor[],                              // always; id order
  callstacks: { sh4: CallstackFrame[], arm7: CallstackFrame[] },   // always both keys; empty arrays while running
  categories: Record<string, { muted: boolean, soloed: boolean }> } // always the keys events, sh4, arm7, dsp
```

- **`registers` is merged per path.** A path present in a tick carries its complete register list (exactly the describe
  names, in describe order) and replaces the stored list; an absent path keeps its value. The client clears
  `registersByPath` and `callstacks` in `initialize()`. The tick after `debugger.describe` is always full; a paused tick is
  always full; while running every fifth tick is full (2.9).
- `stopId`: the client follows the PC in the disassembly, refetches memory and disassembly and fires the breakpoint toast
  when `state` is `paused` and `stopId` differs from the last one it acted on (the first paused tick of a connection
  counts). It never does so merely because a paused tick arrived. Without `stopId` (v2 server) it keeps the v2 rule
  (running -> paused edge; `breakpointId` only in the hit tick).
- `categories` is `z.record(z.string(), BreakpointCategoryStateSchema).optional()` (an enum-keyed record is exhaustive and
  closed in zod 4). The server always sends the four known keys; the client reads only keys it knows, and restores its
  mute / solo buttons from them.
- `registersStale`: the client renders `dc.sh4.cpu` dimmed while it is true.
- `notice` and `reason` are shown next to the run controls.
- `EventLogEntry`: `eventId` decimal string, unique per process; `subsystem` one of `sh4`, `holly`, `ta`, `core`, `aica`,
  `dsp`, `arm7`, `gdrom`, `maple`, `system`, `debugger`; `severity` one of `trace`, `info`, `warn`, `error`;
  `metadata: { event: string, cycles: string }`.
- `RegisterValue`, `BreakpointDescriptor`, `WatchDescriptor`, `CallstackFrame`, `DeviceNodeDescriptor` are unchanged from
  v2 (`CallstackFrame.location` now carries `"pr"`, `"lr"` or `"stack+0x.."`).

### 3.3 Methods

"Errors" names the channel (3.5). "Class" is the call class of 2.2.2. Every method can also fail with `-32000`, `-32001`,
`-32002` according to the lifetime state (2.2.1), and with `-32602` for a missing or mistyped parameter or a bound of 3.6.
A method of a later milestone answers `-32004` until that milestone.

| Method | Params | Result | Errors | Class | From |
| --- | --- | --- | --- | --- | --- |
| `debugger.describe` | `{protocol?, client?}` | 3.1; followed by a full tick to this client | JSON-RPC | read (in `NoMachine` answered from the constant tables, without a call) | M1, changed |
| `state.getMemorySlice` | `{target?: Target id = "sh4", address: number, length: number}` | `{baseAddress: number, data: number[], validity: "ok", unreadable?: [number, number][]}` — `data` always has `length` entries; unreadable bytes are 0; `unreadable` lists `[offset, length]` runs, omitted when empty | JSON-RPC: `-32010` nothing readable; `-32004` target without memory | read | M1, changed |
| `state.getMemoryBinary` | `{target?, address, length}` | `{baseAddress, totalBytes, unreadable?, binaryId}`, then one binary frame `kind` 2 with `totalBytes == length` bytes | JSON-RPC, as above; no binary frame after an error | read | M3 |
| `state.getDisassembly` | `{target? = "sh4", address: number, count: number, context?: number}` | `{lines: [{address, bytes, disassembly}]}` — exactly `count` lines (fewer only at `addressMax`), addresses stepping by `instructionSize`; `bytes` is space-separated upper-case hex in memory order (`"09 00"`); an unreadable word gives `bytes: "??"`, `disassembly: "<unreadable>"`; `context` is ignored | JSON-RPC: `-32602` misaligned address | read | M1, changed |
| `state.getCallstack` | `{target: "sh4" \| "arm7", maxFrames?: number = 32}` | `{target, frames: CallstackFrame[]}`, indices from 0 | JSON-RPC | read | M1 |
| `state.watch` | `{expressions: string[]}` | RpcError; all-or-nothing; stored duplicates skipped | in-band: `-32602` unresolvable expression | table | M1, changed |
| `state.unwatch` | `{watchIds: number[]}` | RpcError; unknown ids ignored | in-band | table | M1 |
| `state.modifyWatchExpression` | `{watchId: number, newExpression: string}` | RpcError; the id is kept | in-band: `-32602` unknown id or unresolvable | table | M1 |
| `state.editWatch` | `{watchId: number, value: string}` | RpcError; from M2 = `state.setRegister` on the watched register | in-band | write | M2 |
| `control.pause` | `{target?}` | RpcError | in-band; 2.3.2 | — | M1 |
| `control.runUntil` | `{}` | RpcError | in-band; 2.3.2 | exec | M1 |
| `control.step`, `control.stepOver`, `control.stepOut` | `{target}` | RpcError | in-band: `-32003`, `-32004`, `-32005`, `-32012`; 2.3.2 | exec | M1 `sh4`, M2 `arm7`, M3 `dsp` |
| `control.reset` | `{}` | RpcError | in-band | write | M2, new |
| `breakpoints.add` | `{event: string, address?: number, kind?: "code" \| "event", enabled?: boolean = true}`; `kind` defaults to `code` when `address` is present, else `event` | RpcError; the new breakpoint appears in the next tick with a new id | in-band: `-32004` event not in the server's table or target without `stepping`; `-32602` missing or invalid `address` for a code breakpoint; `-32012` | table | M1, changed |
| `breakpoints.remove` | `{id: number}` | RpcError | in-band: `-32602` unknown id | table | M1 |
| `breakpoints.toggle` | `{id: number, enabled: boolean}` | RpcError | in-band: `-32602` unknown id | table | M1 |
| `breakpoints.setCategoryStates` | `{categories: Record<string, {muted, soloed}>}` | RpcError; unknown keys ignored, absent keys unchanged; never needs a recompile | in-band | table | M1 |
| `state.writeMemory` | `{target: "sh4" \| "arm7", address: number, data: number[]}` (integers 0..255) | RpcError; all-or-nothing | in-band: `-32013` range not writable | write | M2, new |
| `state.setRegister` | `{path: string, name: string, value: string}`; `value` is `0x` hex or decimal, truncated to the register's `width` | RpcError | in-band: `-32602` unknown path / name or unparsable value; `-32013` register with `width: 0` or without a writer | write | M2, new |
| `state.recordSgcFrames` | `{frames?: number = 1024}` | RpcError, **sent when the capture is complete** | in-band: `-32012` AICA not clocked; `-32014` not complete within 5 s | table | M3, now in the schema |
| `state.getSgcFrameData` | `{}` | `{frameCount, channelsPerFrame: 64, bytesPerChannel: 128, totalBytes, binaryId}`, then one binary frame `kind` 1 | JSON-RPC: `-32011` no capture; no binary frame after an error | read | M3, now in the schema |
| `state.getTable` | `{id: "tlb" \| "sq" \| "icache" \| "ocache" \| "ocram"}` | `{columns: string[], rows: string[][], note?: string}`; every row has `columns.length` cells | JSON-RPC | read | M4, new |
| `state.captureHolly` | `{what: "core" \| "ta"}` | `{what, format: "refsw2-log-gzip" \| "taframe4", totalBytes, binaryId}`, then one binary frame `kind` 3 (`core`) or 4 (`ta`) | JSON-RPC: `-32004` the active renderer produces no CORE log; `-32011` no frame rendered yet | read | M4, new |

- `state.recordSgcFrames`: the capture is the next `frames` AICA samples, with per-sample forcing on for its duration
  (2.5.5). If the emulator was running it keeps running. If it was paused it runs for exactly `frames` samples and pauses
  again (`reason: "capture"`, new `stopId`); for this the call, although table-class, arms a resume in the park loop. The
  buffer is server-global and replaced by each capture.
- `state.captureHolly {core}` returns the refsw2 render log of the next rendered frame (the text format of
  devtools-panels-tools.md 6.2, first line `REFSW2LOG: 0`), gzip-compressed; it is available only with the `refsw2` /
  `refmister` renderers. `{ta}` returns the `TAFRAME4` image of the last completed frame (`tactx_write_frame`,
  `hw/pvr/ta_ctx.cpp:325-385`). Both wait for the queued frame (2.2).
- `state.getTable`: `sq` and `ocram` return their contents; `tlb` returns the entries in `WITH_MMU` builds and otherwise
  no rows and a `note`; `icache` / `ocache` return no rows and the `note` "no cache model".

### 3.4 Binary frames

A v3 server prefixes every binary WebSocket frame with a 16-byte little-endian header:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | 4 bytes | magic `"NDB3"` (`4E 44 42 33`) |
| 4 | u32 | `kind`: 1 = SGC frames, 2 = memory, 3 = CORE capture, 4 = TA capture |
| 8 | u32 | `binaryId`: allocated per connection from 1, equal to the `binaryId` of the JSON result it belongs to |
| 12 | u32 | `payloadBytes`, equal to the result's `totalBytes` |

The JSON response is sent first, the binary frame second, to the requesting client only. Client (`debuggerClient.ts`):
subscribe before sending the request; accept only a frame whose magic, `kind` and `binaryId` match (the transport fans
binary frames out to all subscribers); require `payloadBytes == totalBytes` (and `== frameCount * 8192` for kind 1);
10 s timeout; unsubscribe on every path; strip the header and pass on only the payload, as `{frameCount, data}` for SGC.
A client connected to a server without `protocol >= 3` expects no header.

SGC payload (kind 1): `frameCount` x 64 channels x 128 bytes, little-endian; one frame is one AICA sample; the record
layout is devtools-protocol.md section 8 with two changes: `aeg_value` (offset 96) is the **level** (0 = silent,
`0x3FF` = loudest); byte 109 is flags — bit 0 channel enabled, bits 1-2 AEG state (0 attack, 1 decay 1, 2 decay 2,
3 release); bytes 110..127 are zero.

### 3.5 Errors

One convention. A method whose result type is `RpcError` (`control.*`, `breakpoints.*`, `state.watch`, `state.unwatch`,
`state.modifyWatchExpression`, `state.editWatch`, `state.writeMemory`, `state.setRegister`, `state.recordSgcFrames`)
reports **every** failure of a well-formed request in-band. Every other method reports failures as JSON-RPC errors.
`-32700`, `-32600` and `-32601` are always JSON-RPC errors.

| Code | Message | Meaning |
| --- | --- | --- |
| `-32700` | `parse error` | the frame is not JSON; response `id` is `null` |
| `-32600` | `invalid request` | not a JSON-RPC 2.0 request object, or a batch |
| `-32601` | `method not found: <name>` | method not in this section |
| `-32602` | `invalid params: <what>` | missing / mistyped parameter, bound of 3.6 exceeded, unknown breakpoint or watch id, unresolvable watch expression, misaligned address |
| `-32603` | `internal error` | an exception caught in the dispatcher |
| `-32000` | `emulator busy` | the emulator thread did not service the request within 2 s |
| `-32001` | `emulator stopped by host UI` | lifetime `Stopped` and the request must write or execute |
| `-32002` | `no game loaded` | lifetime `NoMachine` and the request needs a machine |
| `-32003` | `not paused` | a step was requested while running |
| `-32004` | `not supported: <what>` | target, event, capability or milestone the server does not implement |
| `-32005` | `cannot step: <why>` | step precondition failed (2.3.2) |
| `-32010` | `address not readable` | nothing in the requested range is readable |
| `-32011` | `no capture available` | `state.getSgcFrameData` / `state.captureHolly` without data |
| `-32012` | `arm7 held in reset` / `aica not clocked (arm7 in reset)` | sound target not clocked (2.5.3, 2.5.4) |
| `-32013` | `not writable: <what>` | memory range or register cannot be written |
| `-32014` | `capture timed out` | `state.recordSgcFrames` not complete within 5 s |

The client checks and shows the result of every `control.*` call (today they are discarded).

### 3.6 Bounds

Violations are `-32602`. `state.getMemorySlice.length` 1..65536; `state.getMemoryBinary.length` 1..16777216;
`state.getDisassembly.count` 1..1024; `state.getCallstack.maxFrames` 1..64; `state.recordSgcFrames.frames` 1..4096;
`state.writeMemory.data.length` 1..65536; watch expression at most 256 characters, at most 64 expressions per call, at
most 256 watches; at most 1024 breakpoints; inbound message at most 1 MiB (the transport closes the connection above
that).

## 4. Frontend work (dreamcast-devtools)

Packages are sequential on `skmp/nulldc3`: F0, then F1, then one package per milestone.

### 4.1 F0 — hygiene (M0, in progress)

The lint fixes of devtools-build.md; eslint ignores; `tsconfig` covers server and tests; scripts `build` (= build:native),
`typecheck`, `test:run`, `check`; the generated DSP wasm glue committed so a fresh clone type-checks and builds without
Rust (`build:dspsim` stays for regeneration until M3); version from `package.json` (`3.0.0-pre`), never "unknown"; README
and in-app docs describe the real scripts and protocol; Dockerfile / fly.toml use the real scripts (they deploy only the
mock demo); CI runs `npm ci` and `npm run check` on Node 22. No dependency major bumps.
Remaining item: `check` also runs `build:wasm` (no v3 message breaks the wasm transport build; the risk is silent rot).

### 4.2 F1 — protocol v3, mock module, contract runner (M1)

- **Schema** (`src/lib/debuggerSchema.ts`): everything of section 3 — `PROTOCOL_VERSION = 3`; describe params and the new
  result fields; `Target`; tick fields (`stopId`, `reason`, `registersStale`, `notice`, `categories`); `unreadable`; open
  enums; the SGC methods and every new method in `RpcMethod` and `DebuggerRpcMethodSchemas` (the two `as keyof` casts in
  `debuggerClient.ts` go away). A strict variant of the schema (unknown keys fail) is derived for the contract runner.
- **Client plumbing**: replace `window.setTimeout / clearTimeout / setInterval` in `jsonRpcClient.ts` and `transport.ts`
  with the global functions (the code then runs under node and under vitest's node environment); pending requests are
  rejected when the socket closes; a connect failure produces a readable message, not `[object Event]`; a dropped tick
  (validation failure) is reported through a client callback with the zod path instead of `console.error` only;
  `DebuggerClient.describe` sends `{protocol, client}`; binary-frame handling of 3.4.
- **Mock**: the protocol core becomes a module, `server/mockDebugger.ts`, which `server/mockServer.ts` (Express + Vite)
  wraps and which the contract runner starts in-process. It implements v3: the fields and rules of section 3, a 200 ms
  running tick with every fifth full, `stopId` / `reason`, `categories`, the error table, bounds, the binary header.
- **Bundle info**: the Vite build writes `dist-native/bundle-info.json` (2.11) with `protocol = PROTOCOL_VERSION`.
- **Contract runner**: `scripts/contract-test.ts`, `npm run contract -- [--url ws://host:port/ws] [--profile mock|emulator]
  [--elf <path>] [--idle-breakpoint <address|symbol>]`. With `--profile mock` and no `--url` it starts the mock module
  in-process (no Vite). `--idle-breakpoint` is not a scenario run: it is the helper mode of 5.7 (add one muted breakpoint
  at the `StartPaused` stop, resume, stay connected). It uses
  `JsonRpcClient` + `WebSocketTransport` directly (not the stores), registers its notification handler **before** it sends
  `debugger.describe` (under node both frames of "response, then tick" are dispatched in one turn, so a handler registered
  after `await describe()` never sees the tick), uses the `ws` package for the raw cases (malformed JSON, `Origin` / `Host`
  headers), validates every response and tick with the strict schema, runs the scenarios of 5.5 and exits non-zero on any
  violation. `scripts/` is type-checked by a tsconfig that includes the DOM lib. `--elf` gives it the symbol table of the
  test program (a small ELF32 `.symtab` reader in `scripts/`).
- **v2 compatibility**: the client keeps working against a v2 server (absent `protocol`, `targets`, `categories`, `stopId`
  select the v2 behaviour; no binary header).
- **Tests** (vitest): `jsonRpcClient`, `WebSocketTransport` against a mock WebSocket, `BroadcastChannelTransport` (node 22
  has a global `BroadcastChannel`), schema round-trip against captured frames of both protocol versions, binary header
  parsing.
- Gate: `npm run check` and `npm run contract -- --profile mock` green.

### 4.3 F2 — client behaviour and panels (M1)

- **Store** (`debuggerDataStore.ts`): the tick handler is registered before `debugger.describe` is sent; `initialize()`
  clears `registersByPath` and `callstacks`; `registers` merged per path; `stopId` tracking (follow / refetch / toast only
  on a new stop); category state taken from the tick, and the category client registered centrally (not from a
  Breakpoints panel effect); watch expressions lower-cased on both add paths.
- **Endpoint and reconnect** (`sessionStore.ts`): `?ws=` is read from `window.location.search` (before the `#`; the app
  uses `HashRouter` and the memory panel rewrites the hash query). Endpoint order: `?ws=`, the last manual entry
  (localStorage), `VITE_WS_URL`, same-origin `/ws`. `sessionStore.connect` accepts any `ws://` or `wss://` URL (the manual
  box works). Reconnect lives in `sessionStore`: on close it rejects pending requests, sets the new connection state
  `reconnecting`, retries after 1, 2, 5, 10 s (then every 10 s), and on success runs `initialize` again. In wasm mode a
  reconnect waits for a discovery announcement. A 403 from the Origin / Host check is not visible to script; the error text
  says so.
- **Toolbar**: shows `reason` and `notice` next to the run controls; the results of all `control.*` calls are checked and
  shown (toast with the error message); a banner when `describe.protocol` differs from `PROTOCOL_VERSION`.
- **Disassembly**: follows the PC and refetches when `stopId` changes (and after the panel's own breakpoint edits without
  moving); the first address comes from the first tick; target metadata (`instructionSize`, `addressMax`, radix, digits,
  defaults) from `targets`.
- **Memory**: refetches when `stopId` changes; default address from `targets`; unreadable bytes shown as `??`.
- **Unsupported targets**: a panel whose target is missing from `targets`, or whose `stepping` / `memory` is false, shows
  a notice instead of the corresponding controls; so do the SGC panel without `"sgc-capture"` and the breakpoint panels of
  a target without `stepping`.
- **Device tree panel**: one memoised component per node that selects `registersByPath[path]` itself (today every mounted
  item re-renders on every tick; about 1 ms per item); expansion is controlled state that survives search and ticks; a
  search expands at most 200 register rows; `dc.sh4.cpu` is dimmed while `registersStale`; the description is shown for
  nodes with and without actions. The watch autocomplete limits its list to 50 matches.
- **Breakpoints**: address inputs, lists and the hit toast use `addressRadix` / `addressDigits` of the target (DSP steps
  are decimal everywhere).
- **Event log**: shows `metadata.event` and `metadata.cycles`.
- **Error boundaries**: each dock panel is wrapped in an error boundary that shows the error and a reload button; an
  unknown panel id in a saved layout or in `actions` is ignored. Validation failures are surfaced in the UI, including the
  dropped-tick case with the zod path.
- **Callstack**: frames labelled with `location` (`pr`, `lr`, `stack+..`) and a note that the unwinder is heuristic.
- Tests (vitest): the merge and `stopId` logic of the store, reconnect state machine, endpoint order.
- Gate: `npm run check`; contract green against the mock and against the emulator.

### 4.4 F3 — M2 panels

- One generic register-table panel fed by `registersByPath[path]`: new panel ids `sh4-registers` and `arm7-registers`
  (in `PANEL_IDS`, `PanelIdSchema`, `PANEL_REGISTRY`, `mainTabs` and the default workspace layouts; a saved layout gains
  them only when the user resets it), and the ten peripheral ids (replacing the "Coming Soon" panels); edit through
  `state.setRegister` (registers with `width: 0` are read-only).
- Memory view edits bytes through `state.writeMemory` and refetches after its own write; watches edit through
  `state.editWatch`.
- Toolbar: run / pause / step / over / out for the focused target, reset; "Run to next breakpoint" removed (it is identical
  to Run). Hotkeys F5 (run), F6 (pause), F10 (over), F11 (step), Shift+F11 (out), with F8 and Shift+F8 accepted as
  alternatives to F11 and Shift+F11; handled with `preventDefault` only while the focus is outside an input and outside
  Monaco.
- ARM7 controls enabled by `targets[arm7].stepping`.
- Offline: Monaco bundled locally (no CDN) so the embedded UI works without internet; route-level code splitting (the
  embedded bundle grows to about 5 to 6 MB, about 1.4 MB gzip-compressed, which is how it is embedded).

### 4.5 F4 — M3 audio

- SGC panel on the v3 contract. `debuggerClient.ts`: `fetchSgcFrameData` per 3.4, `recordSgcFrames(frames)`.
  `AudioPanel.tsx`: frame count from the result (no literal 1024), clamp `playbackPosition` when a shorter capture
  arrives, handle `frameCount == 0`, "active" from the flags byte instead of `aeg_value !== 0`.
  `sgcChannelData.ts`: accessors for byte 109; constants 128 / 8192 / 64 from the result. `SgcWaveformCanvas.tsx`,
  `SgcWaveformRenderer.ts`, `SgcChannelView.tsx`: no literal frame counts, indices clamped. The mock produces the same.
- DSP disassembly and breakpoint panels against the real DSP (`targets[dsp].stepping`).
- Playground: the DSP module becomes the emscripten build of the emulator's C++ DSP core behind the existing
  `src/lib/aicaDsp.ts` interface; `crates/aica-dsp` (Rust) and wasm-pack are removed. The wasm artefact is committed and
  regenerated by `npm run build:dspsim`, which then needs emcc and the emulator source path. "Load from emulator" reads the
  AICA register file and DSP RAM through `state.getMemoryBinary`; the playground shows a banner naming the snapshot's
  source and time. Because the core is the same, the playground's result is what the emulator plays.

### 4.6 F5 — M4 Holly, and F6 — M5

- F5: the CORE inspector renders the parsed shading fields and primitive previews (port from `refsw2-logview`) and fetches
  its log with `state.captureHolly {core}`; the TA inspector is defined on the `TAFRAME4` capture; table panels on
  `state.getTable`; buttons in both inspectors that add the event breakpoints `dc.holly.core.render_start` /
  `dc.holly.ta.list_end`.
- F6: the SH4 simulator panel on the measured model, with the fixture gate.

## 5. Verification

### 5.1 devtools

`npm run check` (lint, typecheck, vitest, both builds) and `npm run contract -- --profile mock`.

### 5.2 Pure-module tests (emulator)

Plain C++ executables under `devcast/tests/debugger/<package>/`, each directory a small CMake project that does
`include(../../../reicast/cmake/config.cmake)` to get the tree's definitions and `-std=c++20` (any translation unit that
includes `types.h` needs them). The pure set is `dbg_net.cpp`, `dbg_webui.cpp`, `dbg_disasm.cpp`, `dbg_regs_tables.cpp`,
`dbg_symbols.cpp`, `deps/crypto/sha1.cpp`; nothing else links outside the emulator.

| Directory | Package | Content |
| --- | --- | --- |
| `tests/debugger/net/` | E1 | WebSocket framing and handshake vectors (RFC 6455 examples), fragmentation, the 1 MiB limit; HTTP path sanitising; Host / Origin rules of 2.11 including `null` and `localhost.evil.example`; MIME and cache headers; fallback and 404 rules; a send to a closed socket does not raise `SIGPIPE`; embedded-bundle lookup with a fixture bundle |
| `tests/debugger/disasm/` | E2 | SH4 and ARM7: golden tables generated from GNU objdump, PC-relative targets, and the SH4 opcode table; DSP step against the devtools playground assembler |
| `tests/debugger/regs/` | E3 | register-table sanity: unique names per node, unique paths, addresses inside the module ranges, widths |
| `tests/debugger/symbols/` | E5 | `.symtab` walk on a fixture ELF, lookup by address, `name+0xOFF` |

### 5.3 In-emulator selftest

`devcast -config Debugger:SelfTest=<filter|all>` runs the registered tests on the emulator thread after the machine is
created, prints one line per test, and exits with 0 (all passed) or 1. Tests register themselves with a macro from
`dbg_selftest.h`, one file per package, so packages do not edit a shared list. Tests that need a running guest (`sh4bp`,
the arbitration tests of `emu`) are started with the ELF on the command line and `Debugger:StartPaused=yes`; they run
from the `StartPaused` park and take the place of the client: the park loop hands each stop to the test instead of
waiting for a call, and the test arms, resumes and steps through `dbg::emu` on (E).

- E5, `dbg_selftest_core.cpp`: for all 65536 encodings, `dbg_disasm` reports the word as an instruction
  (`Sh4InsnInfo::valid`) iff the emulator's opcode table executes it (`OpDesc[op]->oph != iNotImplemented`), with the one
  exception E2 documents: `0x085B`, the reios trap, which the table executes and the disassembler does not call an
  instruction (the opcode table cannot be linked standalone, hence this test is here); JSON emission rules on a built
  tick (no `null`, required keys).
- E3, `dbg_selftest_regs.cpp`: every register row of `dbg_regs_tables` maps to a registered slot of the live `sh4mmr` / SB
  arrays and every registered slot has a row; `dbg_mem` region boundaries, mirrors, the GD-ROM and modem holes, a read
  across `0x0021FFF0`, and that no read changes `SB_FFST`'s counter, the SCIF FIFO or the AICA common registers.
- E4a, `dbg_selftest_emu.cpp`: stop arbitration (a host stop while parked, a guest reset while parked, a stop requested
  during a service excursion); a pause request and a service excursion during a SLEEP wait (taken when the wait ends;
  state exact); an event breakpoint during a SLEEP wait (latched in that call; an interrupt accepted on the resume
  returns after the SLEEP); `Call` timeout and cancellation; `sh4_host_fp_resync` after a park. With E4b merged also: a
  guest reset and a host stop during a step.
- E4b, `dbg_selftest_sh4bp.cpp`: gates B1 to B14 of 2.3.6, for the interpreter and for the x64 JIT.

### 5.4 Integration recipe (emulator + contract runner)

- Build the emulator with `-DNULLDC_DEVTOOLS_DIST=<dreamcast-devtools>/dist-native`.
- Start it with the test ELF `2ndmix.elf` from the directory given as `NULLDC_ELF_DIR` (runs under reios, no BIOS; starts the ARM7; has
  `.symtab`; entry `0x8C010000`) and always with
  `-config Debugger:Port=0 -config Debugger:Required=yes -config Debugger:StartPaused=yes -config audio:backend=none`.
  `Port=0` avoids collisions between packages running in parallel; `Required=yes` makes a failed bind fatal instead of a
  test against somebody else's emulator. The harness parses the `Debugger: http://127.0.0.1:<port>/` line.
- Environment: fresh `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `HOME`, `DISPLAY=:0` (WSLg), `ulimit -S -c 1`, kill by pid with
  SIGTERM (devcast-build.md "For automated runs"; `agents/nulldc3/build/devcast-x64/run/smoke.sh`).
- One emulator process serves a whole contract run (every start activates a window).
- `npm run contract -- --url ws://127.0.0.1:<port>/ws --profile emulator --elf <path>`. The runner waits for a paused
  tick with `reason: "start"` before the first scenario.
- Timeouts in 5.5 are wall time for the mock; the emulator profile multiplies them by 5.

### 5.5 Contract invariants and scenarios

Applied to every tick in every scenario:

- I1. Validates against the strict copy of the schema (unknown keys fail), and the raw JSON contains no `null`.
- I2. `tickId` strictly increases per connection (not necessarily by 1).
- I3. `breakpoints` keys equal `String(id)`; event-log `eventId`s are unique; at most 200 entries.
- I4. Every `registers` path exists in the describe tree and carries exactly the describe register names in order; values
  with `width > 0` match `0x` + `ceil(width/4)` upper-case hex digits or are `"n/a"`.
- I5. A paused tick is full. A running tick has at least the three hard-wired paths (`dc.sh4.cpu` may be absent when
  `registersStale` is true). `categories` has the four keys. `stopId` never decreases.

Scenarios, both profiles unless marked:

| Id | Scenario | Assertion |
| --- | --- | --- |
| S00 | anchor (emulator) | from the `StartPaused` stop: add a code breakpoint at the ELF entry `0x8C010000`; resume; within 10 s a paused tick with that `breakpointId`, PC `0x8C010000`; `dc.timing.SH4_DEBUG_ENGINE` is `native` and the tick has no `notice` (x64 JIT and interpreter; with `Debugger:ForceFallback=yes` it is `interpreter-fallback` and the `notice` is present); remove it. All later emulator scenarios start from here; later breakpoints use addresses constructed as in S05 or ELF symbols resolved by the runner (for example `_snd_init`), never the PC of an arbitrary pause |
| S01 | describe | result valid; `protocol == 3`; `targets` present; unique paths; unique register names per node; `actions` within `PanelId` and never empty; the first notification after the response is a full tick |
| S02 | pause | `control.pause` returns `{}`; a paused tick within 2 s with `reason: "pause"` and a new `stopId`; a second pause returns `{}` and changes nothing |
| S03 | resume | `control.runUntil` returns `{}`; running tick within 1 s; at least 2 more ticks within 1.5 s; `dc.timing.SH4_CYCLES` (mock: PC) differs between them |
| S04 | step | paused; after `control.step {sh4}` a paused tick with a new `stopId` and `reason: "step"`, and no running tick in between. Emulator: `dc.timing.SH4_CYCLES` does not decrease, and increases unless PC1 is an exception vector (a deferred interrupt can end the step before an instruction runs, 2.3.2); if the line at PC0 is not control flow and PC0-2 is not a delayed branch then PC1 == PC0+2 or PC1 is an exception vector. Mock: PC1 is the next ring address |
| S05 | breakpoint hit | paused; step until the lines at PC and PC+2 are both not control flow (at most 64 steps); add a code breakpoint at PC+2; the next tick lists it with a new id N; resume; within 2 s a paused tick with `breakpointId == N`, `reason: "breakpoint"`, PC == PC+2; the next paused tick of the same stop (provoked by adding a watch) repeats `breakpointId` and `stopId` |
| S06 | mute | same construction; set `sh4.muted`; the tick echoes it in `categories`; resume; only running ticks for 600 ms; pause; unmute. Repeat with `arm7.soloed` |
| S07 | toggle and remove | a disabled breakpoint is not hit (as S06); remove deletes it; removing again and toggling an unknown id return in-band `-32602` |
| S08 | watches | add `dc.sh4.cpu.pc`: listed with an id and a value equal to the PC register in the same paused tick; rename to `dc.sh4.cpu.PR`: same id, value equals PR; unknown path: in-band `-32602`; unwatch: gone. The initial list is not assumed empty |
| S09 | memory | paused; 64 bytes at PC: `baseAddress` echoed, 64 bytes, equal to the `bytes` of the disassembly lines. Emulator: a read across `0x0021FFF0` length 32 returns 32 bytes with `unreadable: [[16,16]]`; a fully unreadable range (`0x10000000`, 16) returns `-32010` |
| S10 | disassembly | 16 lines at PC; addresses step by `instructionSize`; `bytes` format; non-empty text |
| S11 | callstack | paused tick: frame 0 `pc` == PC. Emulator: frame 1 `pc` == PR. `state.getCallstack {maxFrames: 8}` returns at most 8 frames with indices from 0. Running tick: both arrays empty |
| S12 | errors | unknown method gives `-32601` with the request id; `state.getMemorySlice` with a string `address` gives JSON-RPC `-32602`; `control.step` with a numeric `target` gives in-band `-32602`; `control.step {sh4}` while running gives in-band `-32003`; malformed JSON gives `-32700` with id `null`; no `data` member anywhere |
| S13 | two clients | a breakpoint added by A appears in a tick received by B within 1 s |
| S14 | reconnect | after closing and reopening (within the grace period), breakpoints, watches and categories are unchanged in the first tick |
| S15 | HTTP, Host and Origin (emulator) | `GET /` is HTML; `GET /?ws=x` is HTML; a missing asset is 404; `POST /` is 405; `GET /` with `Host: evil.example:<port>` is 403; upgrade with `Origin: http://evil.example` is 403; with `Origin: null` is 403; with `Origin: http://localhost:5173` is 101 |
| S16 | M2, if capability | ARM7 step and breakpoint; `writeMemory` then read back then restore; `setRegister` R0 round trip; reset stays paused with `reason: "reset"` |
| S17 | M3, if capability | `recordSgcFrames {frames: 64}` completes; header magic, `kind`, `binaryId`, `payloadBytes == 64 * 8192`; an error result is followed by no binary frame. DSP step and breakpoint on the DSP test ELF |
| S18 | M4, if capability | `state.getTable` for each id: every row has `columns.length` cells; `state.captureHolly {ta}`: header, `kind` 4, payload starts with `TAFRAME4` |

Not assertable against a real emulator, and therefore not in the list: the absolute PC after a pause; that a breakpoint at
an arbitrary address is hit; event-log contents and timestamp order; the exact tick period; callstack depth beyond frame
1; completion of `stepOver` on a call and of `stepOut` (assert the running tick, then pause from the runner after 5 s).

The runner removes its breakpoints and watches, restores the category state, and leaves the emulator in the run state it
found.

The M3 DSP scenarios need a program that loads the DSP (2ndmix never does): `devcast/tests/debugger/guest/dsptest/`, a KOS
program that loads a fixed DSP program and plays a tone through it, built with the sh-elf toolchain of PLAN track 7; its
ELF is committed next to the source so the gate does not need the toolchain.

### 5.6 Sound regression harness

Built in round 1 (E3), before the sound burst is touched, because there is no sample sink today (`audio:backend=none`
discards everything), the RTC is host time, and runs are ended by wall clock.

- `-config config:Debug.AicaHash=<N>`: the emulator folds every `WriteSample(r, l)` pair, and at each AICA tick
  `aica_reg[0..0x7FFF]`, the ARM7 `regs[]` and (on a backend with `SH4DBG_CTX_AT_SLICE`) the SH4 PC, `R0..R15`, `PR`,
  `SR` and `FPSCR`, into FNV-1a hashes; after N AICA ticks it prints
  `AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> ticks=<N> cycles=<sh4_sched_now64>` and exits with 0.
- `-config config:Debug.FixedRtc=<value>` pins the RTC.
- Both are implemented inside `hw/aica/` — the sample fold at the `WriteSample` call sites of `sgc_if.cpp`, the per-tick
  fold, the print and the exit in `AicaDevice::Update`, the RTC in `aica_mmio.cpp` — so E3 touches no file of E4a. The
  harness is E3's first commit, on E0's base, so that its line can be taken before any other round-1 change.
- Acceptance of the harness itself: two consecutive runs of the same binary print the same line, for 2ndmix.elf,
  N = 40000, in both `aica.NoBatch` settings, with the interpreter and with the JIT. A remaining host dependence found
  here is removed first.
- Comparing lines: both lines are taken in the same session; a difference is re-run twice and counts only if it
  repeats, and every non-repeating difference is logged with both lines. (The prototype saw one run in about 150 take a
  different guest path with identical inputs — Appendix C; the cause is not known.)
- Gate for M2 and M3: identical lines before and after the change, same matrix; M3 additionally with the DSP test ELF.

### 5.7 No regression, and performance

- With `NULLDC_DEBUGGER=OFF`, with `Debugger:Enabled=no`, and with the debugger enabled and no client, the harness line of
  5.6 is identical (N = 40000, 2ndmix), and equal to the line of the commit that introduced the harness. With the
  debugger enabled and no client the only costs are the `g_attn` and latch tests per slice, the event byte tests, and
  `dbg_sh4_WantsCheck` at decode time (gate B11).
- With an SH4 breakpoint listed but never hit, the harness line is identical too (R5 and R13 on a real workload): the
  runner mode `--idle-breakpoint <address|symbol>` (4.2) connects at the `StartPaused` stop, adds a breakpoint on a hot
  address with the `sh4` category muted, resumes and stays connected until the emulator prints its line.
- Speed: emulation speed with the debugger enabled and no client is within 2 % of `Enabled=no`, 2ndmix, three runs each,
  measured in the `-DHAS_TA_LLE=OFF` build with the `none` renderer (or GL with `GALLIUM_DRIVER=d3d12`); the figure states
  its renderer. No figure is taken with the reference software renderers (D3). On a loaded machine wall time is not
  usable (the prototype's runs were made at load 24-40); the comparison is then made on user-mode host instructions of
  the emulator thread, as in gate B11.

### 5.8 Windows

A Windows build (MSVC or the appveyor recipe) is a merge gate for E1 and E5: Winsock include order, `ws2_32`,
`/permissive-`, the brace-initialised embed arrays. It cannot be checked on the Linux box.

E4b adds a Windows *run*: the check, the stop path and the `RunBlock` thunk have only executed with the System V ABI
(2.3.7 item 7). Until gates B1 to B4 have passed on a Windows x64 build, the x64 backend does not set `DebugChecks` in
Windows builds and Windows uses the fallback with its notice; M1 itself needs only the Windows build.

## 6. Work packages and file ownership

Rules:
- One worktree and one build directory per emulator package (`agents/nulldc3/wt/<pkg>`, branch `skmp/nulldc3-<pkg>`);
  commits as work lands; an integrator merges into `skmp/nulldc3`.
- Ownership is by directory and file. A package edits only what it owns. The headers E0 delivers are frozen after their
  review; a change goes through the integrator.
- Order: E1 and E2 are running. E0 next, alone. E3, E4a and E4b start after E0's review (E4b from the prototype branch);
  E4b's gate runs on the integration branch once E4a is merged, because its selftest drives the real park loop. E5 after
  E0-E4b are merged and F1's contract runner exists. F0 -> F1 -> F2 run in parallel with the emulator packages.

### 6.1 Emulator, round 1 (M1)

| Pkg | Owns | Must not touch | Depends on | Gate |
| --- | --- | --- | --- | --- |
| **E1** transport, build skeleton, UI embed (started) | `libswirl/debugger/dbg_net.h/.cpp`, `dbg_webui.h/.cpp`, `debugger.cmake`, `libswirl/debugger/cmake/`, the `include()` line in `CMakeLists.txt`, `tests/debugger/net/` | everything else | — | `tests/debugger/net` green; the standalone test server passes the HTTP / Host / Origin cases of S15 with a fixture bundle; Windows build (5.8) |
| **E2** disassemblers (started) | `dbg_disasm.h/.cpp`, `tests/debugger/disasm/` | everything else | — | `tests/debugger/disasm` green |
| **E0** shared headers | `debugger.h`, `dbg_emu.h`, `dbg_sh4.h`, `dbg_core.h`, `dbg_regs.h`, `dbg_mem.h`, `dbg_events.h`, `dbg_symbols.h`, `dbg_callstack.h`, `dbg_selftest.h` + `dbg_selftest.cpp` (the driver), a stub `.cpp` per module so the tree links (each stub passes to its implementing package after the merge), `deps/nlohmann/`, the new `hw/sh4/sh4_dbg.h`, the debug virtuals of 2.3.4 l in `hw/sh4/sh4_if.h`, and the debug accessor declarations with stub bodies in `hw/aica/aica_mmio.h`, `hw/arm7/SoundCPU.h`, `hw/aica/dsp.h`, `hw/gdrom/gdromv3.h`, `hw/holly/sb.h`, `hw/holly/holly_intc.h`, `hw/holly/sh4_mem_area0.h` | emulator behaviour (no call sites, no logic); E1 / E2 files | E1's `debugger.cmake` and `dbg_net.h` (E0 branches from the E1 commit that has them) | builds and links with stubs, `NULLDC_DEBUGGER` ON and OFF; the nine contracts below are written as comments on the declarations (thread, lifetime state, blocking behaviour); reviewed; then frozen |
| **E3** registers, memory, device side | `dbg_regs.cpp`, `dbg_regs_tables.cpp`, `dbg_mem.cpp`, `dbg_selftest_regs.cpp`, `tests/debugger/regs/`; every edit under `hw/aica/`, `hw/arm7/`, `hw/holly/`, `hw/gdrom/`, `hw/maple/`, `hw/pvr/` — accessor bodies **and** the `DBG_EVENT` call sites there; the sound regression harness (5.6), first. Round 1: readers and the M1 tables of 2.8 | `libswirl.cpp`, `libswirl.h`, `hw/sh4/`, `jit/`, `reios/`, frozen headers | E0 | `tests/debugger/regs`; selftest `regs`; harness acceptance (5.6); no guest handler is called from `dbg_mem` / `dbg_regs` (review) |
| **E4a** emulator thread and run control (2.2, 2.3.1, 2.3.2) | `dbg_emu.cpp`, `dbg_selftest_emu.cpp`, `libswirl.cpp`, `libswirl.h`, `hw/sh4/SuperH4_impl.h/.cpp`, `hw/sh4/sh4_core_regs.cpp`, `hw/sh4/sh4_interrupts.cpp`, `hw/sh4/sh4_interpreter.h`, `hw/sh4/interpr/sh4_opcodes.cpp`, `reios/` (stop reason of `reios_exit`; the one call handing the loaded ELF to `dbg::symbols::OnElfLoaded`), the `DBG_EVENT` call sites in these. In its files it also places the mechanism's call sites, exactly as E0 declared them: `dbg_sh4_OnException()` in `Do_Interrupt` / `Do_Exception`, the `RTE` flow report in the `RTE` handler, `SuperH4_impl::Step()` forwarding to the backend, `ResetSlice()` in `SuperH4_impl::Reset`, and the `Run()` of 2.3.5 | device directories of E3; `hw/sh4/dyna/`, `hw/sh4/interpr/sh4_interpreter.cpp`, `jit/`; `dbg_sh4.cpp`; frozen headers | E0 | selftest `emu` (5.3); the checklist below |
| **E4b** SH4 breakpoints and steps in the active backend (2.3.3 to 2.3.7); a port of the prototype (`skmp/nulldc3-proto-bp`) onto E0's headers | `dbg_sh4.cpp`, `dbg_selftest_sh4bp.cpp`, `tests/debugger/sh4bp/`, `hw/sh4/dyna/*` (decoder, SHIL, register allocator, driver with `rdv_RunTempBlock` and the fallback loop, block manager, `ngen.h`), `jit/backend/x64/*` (check emission, `mainloop_exit`, `RunBlock` thunk, epilogue report, counter not reset at entry), `hw/sh4/interpr/sh4_interpreter.cpp` (debug loop, `Step()`, persistent counter) | everything else — its call sites in E4a's files are placed by E4a | E0 (contract 9); E4a merged for the gate | gates B1-B14 (2.3.6) for the interpreter and the x64 JIT; 5.7 with listed breakpoints; the Windows run of 5.8. Lands before PLAN T3.0 / T3.2 touch the decoder's cost code, the x64 prologue or the interpreter loop (2.3.7 item 5) |
| **E5** core | `dbg_core.cpp`, `dbg_main.cpp`, `dbg_symbols.cpp`, `dbg_callstack.cpp`, `dbg_selftest_core.cpp`, `tests/debugger/symbols/` | `hw/`, `jit/`, `libswirl.cpp`, frozen headers | E0-E4b merged; F1's contract runner | contract S00-S15 against the emulator (5.4); selftest `all`; 5.7; Windows build (5.8) |

Where each part of the mechanism is implemented:

| Part | File(s) | Package |
| --- | --- | --- |
| decoder query, `shop_dbg_check`, size limits, DIV32 rule, `DecodeLimits`, refund rewrite, `dbg_flow`, `ngen_features::DebugChecks` | `hw/sh4/dyna/decoder.cpp`, `decoder.h` | E4b |
| the SHIL op; `srt_waw` T-reader rule; the flush rule | `hw/sh4/dyna/shil_canonical.h`, `shil.cpp`, `regalloc.h` | E4b |
| page discard, flow-block discard, temporary-block pointer | `hw/sh4/dyna/blockmanager.h/.cpp` | E4b |
| temporary blocks (`rdv_RunTempBlock`), the remainder in `recSH4::Loop`, `recSH4::Step`, the fallback loop and step; the banner prints move out of `Loop()` | `hw/sh4/dyna/driver.cpp`, `ngen.h` | E4b |
| x64 check emission, dispatcher exit, `RunBlock`, epilogue report, persistent counter | `jit/backend/x64/rec_x64.cpp` | E4b |
| interpreter debug loop, step, persistent counter | `hw/sh4/interpr/sh4_interpreter.cpp` | E4b |
| armed set, helpers, exemption, remainder, temporary breakpoints, step-over / step-out state, step preconditions | `debugger/dbg_sh4.cpp` | E4b |
| the stop latch, the park context, entering and leaving it, `UseFallbackLoop()`, `Run()` | `debugger/dbg_emu.cpp`, `libswirl.cpp`, `hw/sh4/SuperH4_impl.cpp` | E4a |
| call sites: exception report, `RTE` report, SLEEP latch test | `hw/sh4/sh4_interrupts.cpp`, `hw/sh4/interpr/sh4_opcodes.cpp` | E4a |
| declarations of all of the above that cross a package boundary | `debugger.h`, `dbg_sh4.h`, `dbg_emu.h`, `hw/sh4/sh4_if.h`, `hw/sh4/sh4_dbg.h` | E0 |

E4a checklist (manual, each with (E) parked and again with a pause pending): the menu key opens the menu; the ESC key
stops; window close exits the process without a hang; menu "Exit", "Load State", "Save State", "Restart" work from the
resulting `Stopped` state; a guest soft reset (`SB_SFRES`) while parked resets and stays parked; `reios_exit` exits the
thread; after each, a connected client receives a tick with the right `reason`. Repeated with E4b merged: a guest soft
reset and a host stop during a step.

**The nine contracts E0 writes on the declarations**

1. *`dbg::emu::Call`* — signature, classes, per-state behaviour, timeout and cancellation, capture rule: 2.2.2.
2. *Emulator -> core* — `void dbg::emu::SetListener(void (*)(const RunState&))`; when and on which thread it is invoked,
   what it may do: 2.2.4. `struct RunState { Lifetime lifetime; u32 stopId; StopCause cause; u32 breakpointId; u32 notice; }`
   (`notice` is a bit set of the statements of 3.2). `dbg_core.h` declares the core's entry points:
   `core::Start(net::Server*)`, `core::Stop()`, the `net::Handler` it implements (its callbacks only enqueue to (C)) and
   the listener.
3. *Core -> emulator* (all in `dbg_emu.h`):
   ```cpp
   void    RequestPause();                                   // any thread; sets ATTN_PAUSE; no effect unless Running
   int     Resume();                                         // inside a CC_EXEC call
   int     Step(Target t, StepKind k);                       // inside a CC_EXEC call: validates (0 or a code of 3.5), arms;
                                                             // the park loop executes it after the call returns
   int     Reset();                                          // inside a CC_WRITE call
   void    SetSh4Breakpoints(const u32* ids, const u32* addrs, u32 n);   // enabled SH4 code breakpoints; CC_TABLE;
                                                                         // forwarded to dbg::sh4 in the park context
   void    SetEventBreakpoints(const u32* ids, const u8* events, u32 n); // enabled event breakpoints; CC_TABLE
   void    SetActiveMask(u32 categories);                    // bit per category, client presence folded in; CC_TABLE
   RunState GetRunState();                                   // any thread; snapshot
   bool    OnEmuThread();
   ```
4. *`dbg::net`* — Appendix B: sends are callable from (E) and never block; `HasClients()` is the lock-free flag behind
   `g_event_on`.
5. *Events* — `DBG_EVENT`, `EventSlow`, the `EventId` constants and table: 2.6.
6. *`dbg_mem` / `dbg_regs`* — thread requirement "(E), or under L"; no guest handler is ever called:
   ```cpp
   u32  dbg::mem::Read (Target t, u32 addr, u32 len, u8* out, u8* readableBits);   // returns readable byte count
   int  dbg::mem::Write(Target t, u32 addr, u32 len, const u8* data);              // M2; (E) park context; 0 or -32013
   const dbg::regs::Node* dbg::regs::Nodes(u32* count);                            // constant tables (pure)
   bool dbg::regs::Read (const Node&, u32 index, u64* value, char* text, u32 n);   // false -> "n/a"
   int  dbg::regs::Write(const Node&, u32 index, u64 value);                       // M2; (E) park context
   ```
7. *Lifetime hooks called from `Dreamcast_impl`* — `LifetimeLock` (RAII over L), `OnMachineCreated()`,
   `OnMachineDestroying()`, `OnThreadStarting()` (in `Resume`, under L), `OnThreadEntered()` (on (E), no lock),
   `OnThreadExiting()` (on (E): takes L, drains, publishes `Stopped`), `OnGuestStateReplaced()` (`Reset` / `LoadState` in
   `Stopped`), `Kick()`: 2.2.1, 2.2.5.
8. *`hw/sh4/sh4_dbg.h`* — `void UpdateFPSCR(); bool UpdateSR(); void sh4_host_fp_resync();` (the first two exist today
   only in `sh4_core.h`, which debugger files must not include; bodies in `sh4_core_regs.cpp`, E4a) and
   `void sh4_fallback_loop(); void sh4_fallback_step();` (2.3.5; bodies in `hw/sh4/dyna/driver.cpp`, E4b; the stubs
   abort, and are not reached while the stub armed set is empty).
9. *The SH4 mechanism* — (a) the backend side exactly as 2.3.4 l lists it (`debugger.h`, `hw/sh4/sh4_if.h`); these names
   are the prototype's and are not changed without its owner. (b) Run control -> breakpoint state, in `dbg_sh4.h`; all
   on (E) in the park context; E0 fixes the exact signatures at its review:
   ```cpp
   namespace dbg { namespace sh4 {
   void SetBreakpoints(const u32* ids, const u32* addrs, u32 n);   // the enabled set: diff against the armed set, page discards (2.3.4 g)
   void SetActiveMask(u32 categories);                             // read by the helper only; never a discard
   int  StepCheck();                                               // step preconditions: 0 or -32005 (2.3.2)
   void PrepareResume();                                           // 2.2.4 e: exemption; keeps the remainder if the PC is unchanged
   void PrepareStep();                                             // 2.2.4 e: exemption; drops the remainder
   bool PrepareStepOver();                                         // true: temporary breakpoint armed, resume; false: do a step
   void PrepareStepOut();                                          // step-out state, discard of the call / return blocks
   void OnStop(StopInfo* out);                                     // entering the park context: what stopped; removes temporary
                                                                   // breakpoints, cancels a step-out, clears the exemption
   void OnPcWritten();                                             // PC written, reset, state load: drops remainder and exemption
   bool FallbackWanted();                                          // an active (temporary) breakpoint exists or a step-out is pending
   }}
   ```
   (c) Breakpoint state -> run control, in `dbg_emu.h`: `void dbg::emu::LatchStop(StopCause, u32 breakpointId)` — (E)
   only; what the helpers call to stop (2.2.3).

### 6.2 Frontend

| Pkg | Owns | Must not touch | Depends on | Gate |
| --- | --- | --- | --- | --- |
| **F0** hygiene (M0, in progress) | scripts, lint / tsconfig, README and docs, Dockerfile, CI, committed wasm glue | protocol semantics | — | `npm run check` (with `build:wasm`) on a fresh clone without Rust |
| **F1** protocol v3, mock module, contract runner | `src/lib/debuggerSchema.ts`, `src/lib/jsonRpc.ts`, `src/services/jsonRpcClient.ts`, `transport.ts`, `debuggerClient.ts`, `server/mockDebugger.ts`, `server/mockServer.ts`, `scripts/`, the bundle-info Vite plugin, their tests | panels and stores beyond compile fixes | F0 | 4.2 |
| **F2** M1 client behaviour and panels | `src/state/*`, `src/app/**` | the schema and the mock, except together with the runner in one commit | F1; E5 for the emulator run | 4.3 |
| **F3** M2 panels | as F2, plus the new panel ids in the schema | — | F2; emulator M2 | `npm run check`; contract S16 |
| **F4** M3 audio | SGC and DSP panels, playground, `src/lib/aicaDsp.ts`, the emscripten artefact; removes `crates/aica-dsp` | — | F3; PLAN T5.0; emulator M3 | `npm run check`; contract S17; playground results equal the native core's |
| **F5** M4 Holly | CORE / TA inspectors, table panels | — | F4; emulator M4 | `npm run check`; contract S18 |
| **F6** M5 | SH4 simulator panel | — | tracks 3/4 | fixture gate |

### 6.3 Emulator, later rounds (cut when M1 is merged; the ownership rule stays)

| Pkg | Content | Owns | Must not touch | Depends on | Gate |
| --- | --- | --- | --- | --- | --- |
| **E6** M2 sound | burst machine (2.5.2), ARM7 breakpoints and steps (2.5.3), per-sample forcing (2.5.5), the bodies of the ARM7 / AICA write accessors E0 declared | `Dreamcast_impl::AicaUpdate` in `libswirl.cpp`, `hw/arm7/`, `hw/aica/`, the ARM7 part of `dbg_emu.cpp` | `hw/sh4/`, `jit/`, the other device directories, `dbg_core.cpp` | M1 merged; the harness of 5.6 | 5.6 identical before / after; contract S16 (ARM7 part) |
| **E7** M2 writes and tables | `state.writeMemory`, `state.setRegister` (a write of the SH4 `PC` calls `dbg::sh4::OnPcWritten()`), `state.editWatch`, `control.reset` (wiring only: `dbg::emu::Reset()` exists since E4a); the M2 rows of 2.8 | `dbg_mem.cpp`, `dbg_regs*.cpp`, `dbg_core.cpp`, `hw/holly/`, `hw/gdrom/`, `hw/maple/`, `hw/pvr/` | `hw/aica/`, `hw/arm7/`, `libswirl.cpp` (it uses the accessors E0 declared) | M1 merged | contract S16; selftest |
| **E8** M3 | SGC capture (`dbg_sgc`), DSP steps and breakpoints (2.5.4), `state.getMemoryBinary`, the DSP test ELF | `dbg_sgc.*`, the DSP part of the burst machine, the M3 methods in `dbg_core.cpp`, `tests/debugger/guest/dsptest/` | the DSP core itself (PLAN T5.0 owns it) | PLAN T5.0; E6, E7 | contract S17; 5.6 with the DSP test ELF |
| **E9** M4 | `state.getTable`, `state.captureHolly` | `dbg_core.cpp`, `hw/pvr/`, the capture hook in `rend/refsw2/` | `hw/sh4/`, `hw/aica/`, `hw/arm7/` | E7, E8 | contract S18 |

## Appendix A. Review dispositions

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

## Appendix B. APIs of the started packages (frozen)

E1, `libswirl/debugger/dbg_net.h` (branch `skmp/nulldc3-dbg-net`; names, defaults and guarantees below are the contract):

```cpp
namespace dbg { namespace net {
struct Handler {                      // every method is called on the network thread, never concurrently
    OnOpen (u32 client);
    OnText (u32 client, const std::string& text);
    OnClose(u32 client);              // exactly one per OnOpen, also when the server stops
};
struct Options {
    bind         = "127.0.0.1";
    port         = 55543;             // 0 = assigned by the OS
    webRoot;
    allowedHosts;
    maxMessage   = 1 MiB;
    maxClients   = 16;
};
class Server {
    bool   Start(const Options&, Handler*);
    void   Stop();
    int    Port() const;
    bool   HasClients() const;                          // lock-free
    void   SendText  (client, std::string);
    void   SendBinary(client, std::string);
    void   BroadcastText(const std::string&);
    size_t QueuedBytes(client) const;
};
}}  // sends are thread-safe and never block the caller on a slow or dead socket
```

As built, the header also has: `Handler::OnBinary` (default: the message is dropped); client ids from 1, not reused
while the `Server` lives; a client with more than 16 MiB queued is disconnected; an upgrade beyond `maxClients` gets 503;
`SetStaticProvider`, through which `dbg_webui.cpp` installs the embedded bundle. `dbg_webui.h` has
`dbg::kProtocolVersion` (the one source of the protocol number), `webui::Embedded()`, `Version()`, `GitRev()`, `Find()`.
The static server drops the query string.

HTTP and WebSocket behaviour: 2.11. Files: `dbg_net.h/.cpp`, `dbg_webui.h/.cpp`, `debugger.cmake`,
`libswirl/debugger/cmake/`, `tests/debugger/net/`.

E2, `libswirl/debugger/dbg_disasm.h/.cpp`, `tests/debugger/disasm/` (branch `skmp/nulldc3-dbg-disasm`) — pure functions,
callable from any thread, no emulator dependency:

```cpp
namespace dbg {
struct DisasmCtx { SymbolFn symbol; ReadFn read; void* user; };     // both callbacks optional
std::string DisasmSh4 (uint32_t pc, uint16_t op, unsigned fp_mode, const DisasmCtx*, Sh4InsnInfo*);   // fp_mode: FPSCR.PR / SZ
std::string DisasmArm7(uint32_t pc, uint32_t op, const DisasmCtx*, ArmInsnInfo*);
std::string DisasmDsp (unsigned step, const uint16_t mpro[4]);
}
```

`Sh4InsnInfo` / `ArmInsnInfo` carry `valid`, `is_branch`, `is_delayed_branch` (SH4), `is_call`, `is_return`,
`is_conditional`, `is_trap`, the branch target and the PC-relative literal. `Sh4InsnInfo::valid` is true for exactly the
encodings the interpreter's opcode table executes, except `0x085B` (the reios trap). An encoding that is not an
instruction disassembles as `.word 0x<raw>`.

## Appendix C. The SH4 mechanism: prototype and results

Prototype: branch `skmp/nulldc3-proto-bp` (commits `947f0eca`, `12e46c3e`, `540d4767`, `8e693134`, `d49b8f58` on
`34a62ea8`; not to be merged), worktree `agents/nulldc3/wt/proto-bp`; builds, logs and the run script under
`agents/nulldc3/proto-bp/`. In it the debugger is replaced by `hw/sh4/sh4_debug.{h,cpp}` (names `sh4dbg_*` for this
document's `dbg_sh4_*`), driven by environment variables, and the park loop by a stand-in called from
`SuperH4_impl::Run()`.

The specification, `agents/nulldc3/review/jit-breakpoints-spec.md`, holds what is not repeated here: the code walk with
line references, the prototype's test knobs (its Appendix A) and the full measurement tables (its Appendix B). Its
sections 3.a to 3.j are 2.3.4 and 2.3.5 of this document; its tests T1 to T12 are gates B1 to B12.

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

Not prototyped: the park protocol of 2.2 around the stop (the latch, `CpuRunning`), the step preconditions, the arm /
x86 / cpp backends, the fallback, MMU builds, the Windows ABI path of the emitted code. The prototype's pause left a
SLEEP wait intact; rule M-8 of Appendix A.2 keeps that property.

Host determinism: one run in about 150 took a different guest path with identical inputs (no breakpoints, query compiled
out: 3128 blocks compiled instead of 2687). The cause was not investigated; the window was on a shared desktop, and an
input event is the likely source. Hence the repeat rule of 2.3.6 and 5.6.
