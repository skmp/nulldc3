# Track 1 design — decisions for revision 3

Authoritative decisions of the design owner (2026-10-09). They settle what the verifiers of the packages and of the contract
runner found open or contradictory in `TRACK1-DESIGN.md` v2, and they carry the direction the project owner set on 2026-10-08.
Where a decision changes what a package already implemented, the package is named; nothing here asks for a redesign.

## A. Protocol (section 3) and the contract (section 5.5)

1. **Ids are never reused**: watch ids as well as breakpoint ids are allocated from 1 and never reused within a server process.
2. **`eventLog[].timestamp`** is wall-clock milliseconds since the Unix epoch, like `tick.timestamp`. The runner checks both
   (within one day of its own clock). Within one log, entries are oldest first, their `eventId`s ascend, and the same `eventId`
   is the same entry in every tick of a process.
3. **Malformed requests.** A batch (array), a non-object, an object without `"jsonrpc":"2.0"`, a request whose `id` is not a
   number, and a request without `method` are answered with `-32600`; the response `id` echoes the request's `id` when that is a
   number and is `null` otherwise. A message without `id` (a notification from the client) is ignored: no answer. Unparseable
   text is `-32700` with `id: null`.
4. **Error codes are normative; message texts are not.** `error.message` is a non-empty human-readable string; no client and no
   test may depend on its wording. (The runner stops matching texts and prefixes.)
5. **The target table (3.1)** is normative for every target a v3 server lists: `id`, `pcPath`, `pcRegister`, `breakpointEvent`,
   `instructionSize`, `addressMax`, `addressRadix`, `addressDigits`, `memory`. `label`, `defaultDisassemblyAddress` and
   `defaultMemoryAddress` are the server's choice (an emulator may give the loaded program's entry). **An M1 emulator lists all
   three targets**, `arm7` and `dsp` with `stepping: false` until their milestones.
6. **The device tree (2.8)**: normative are the paths of the M1 table, their nesting (as the table gives it, not by the dots of
   the path), the `actions`, the three anchor registers (`dc.sh4.cpu` `PC`, `dc.aica.arm7` `PC`, `dc.aica.dsp` `STEP`) and the
   register list of `dc.sh4.cpu`. A server lists in `events` exactly the events it can raise, and accepts a breakpoint on every
   event it lists. Other register lists are the server's (the M1 tree may hold fewer than 256 registers; the 256-watch bound is
   then probed up to what exists).
7. **A running target is asked things** (new rows in 5.5): `debugger.describe`, `state.getMemorySlice` and
   `state.getDisassembly` for `sh4` and `arm7`, `state.getCallstack`, `breakpoints.add / toggle / remove`,
   `breakpoints.setCategoryStates`, `state.watch / modifyWatchExpression / unwatch` must all work while running, each followed by
   its tick; none may answer `-32003`. Watches on any path are evaluated in every tick, running ones included (a watch whose path
   the running tick does not carry still shows its current value; `"n/a"` only for `dc.sh4.cpu` under `registersStale`).
8. **Re-enabling a breakpoint must be shown to work** (S07): the runner uses an address that is certainly reached again — the
   interrupt entry `VBR + 0x600` read from `dc.sh4.cpu`, which every program with interrupts enabled reaches at each vblank — adds
   a breakpoint there, sees it hit, disables it, sees the target run through at least two further vblank events, enables it and
   requires the hit within the scenario's window. A target that takes no interrupt in the window makes the scenario **partial**.
9. **What a gate is.** `--milestone Mx` selects that milestone's scenarios and requires each to **pass**; `partial` and `not
   applicable` fail a milestone gate. Without `--milestone`, a run in which no scenario passed exits non-zero ("nothing
   verified"). `--only` narrows the selection and never turns an empty result into a success. The gates are invoked as
   `npm run contract -- --url ws://127.0.0.1:<port>/ws --profile emulator --elf <path> --milestone M1` (M2-M4 alike; the M3 DSP
   part is a second run with the DSP test program).
10. **Bounds are probed**: the 1 MiB inbound message bound (a larger message closes the connection with 1009 or is answered with
    an error; the server stays up), next to the bounds already probed.
11. **`control.pause {target}`**: the target is ignored when it is one of `sh4`, `arm7`, `dsp` — also when the server does not
    list that target; any other string is `-32602`.
12. **Running ticks carry four base paths** (`dc.sh4.cpu`, `dc.aica.arm7`, `dc.aica.dsp`, `dc.timing`); invariant I5 says four.
    `registersStale` never appears in a paused tick.
13. **ARM7 callstack**: two frames (PC; R14 with location `lr`) whenever the ARM7 is running; `callstacks.arm7` is `[]` while the
    ARM7 is held in reset and in running ticks.
14. **`dc.holly.core.vblank`** is raised by every running machine (the video timing always runs) and is in the M1 tree.
15. **No game loaded** (`reason: "no-game"`): ticks carry `registers: {}` and the client drops every register value it holds while
    that reason lasts. Leaving `no-game` (a game starts) is a new stop when the machine comes up parked (`reason: "start"`,
    `stopId` incremented) and otherwise a running tick; the client reads its views again when the reason leaves `no-game`.
16. **Stops in a row**: a client may skip stops when several are queued and must end on the latest; it never has to act on each.
17. **`breakpointId` after its breakpoint is removed during the same stop**: the paused ticks of that stop stop carrying it.
18. **Reconnect**: delays of 1, 2, 5, 10 s, each from the failure before it, repeating at 10 s; any connection that opens resets
    the sequence; an explicit endpoint (`?ws=`) is retried and is not replaced by a fallback.
19. **Disconnected UI**: the panels keep their frame (search boxes, toolbars, focus) and show "not connected" in place of data;
    only data is cleared.
20. **Repeated refusals** are shown once with a count, not as a stack of identical notices.
21. `Target.memory: false` is pinned with a scripted server (no real server has a target without memory that owns a memory panel).
22. `state.getDisassembly` for `dsp`: `bytes` is the step's four MPRO words as 16 hex digits; widths of `dc.aica.dsp` registers:
    `STEP` 8, `MDEC_CT` 16, `RBP` 16, `RBL` 8.

## B. Emulator side (sections 2 and 6)

23. **Header revisions 2 and 3 are part of the contract**; revision 4 collects the remaining comment-only requests
    (`resume/header-requests.e4a.md`, the E3 and E4b notes): the `IsRunning()` terms as implemented, the Park() bullets (first
    Park after a host stop taken in a debugger stop is a resume only when the guest is where that stop was published; a thread
    whose cpu is left stopped at thread start makes no Park before its first evaluation), `dbg_mem.h` (the ARM7 view of two AICA
    bytes goes through the register read, which is a plain copy there), the sh4bp README rows. No declaration changes.
24. **The mechanism as built is the specification** where v2's text differs: a compiled check exists only on a backend with
    `DebugChecks`; the fallback's decode carries the checks; the fallback continues the backend's slice counter where that
    persists; a block is ended early when its checks would exceed the backend's budget (56 on x64) and its rest is compiled per
    pass (slow, not different); the resume remainder and the one-instruction step blocks are temporary blocks.
25. **Backends** (project owner, 2026-10-08): only the x64 and arm64 JITs and the interpreter exist. The breakpoint fallback
    concerns arm64 only (until it implements the check) and, later, new ports. There is **no user-facing interpreter choice**;
    `dc.timing.SH4_BACKEND` / `SH4_DEBUG_ENGINE` and the fallback notice stay, because they tell what executed.
26. **Per-sample forcing (2.5.5) is transitional**: the DSP is always on in nullDC 3 and the DSP-less batch path is removed when
    the one DSP core is wired in (PLAN T5.0); from then on nothing is forced and the capability `dsp-forced` does not exist.
27. **The server is the control surface, not only a debugger** (PLAN track 10): it works with no game loaded (already so), its
    config keys stay under `Debugger:` for now, and nothing in the core may assume that a client is a debugging session. In
    automated runs (headless, or any `Exit.*` key) the server is off unless `Debugger:Enabled` is set explicitly. Save states
    and the game list are leaving the product (the "guest state replaced" hook stays for when states return).
28. **Known limits to state in 2.3.7 / 5.7**: host stop/start cycles fill the x64 code cache (a tree defect outside the
    debugger, to be fixed with the JIT work); `CpuRunning` is a plain volatile; event breakpoints latched inside a SLEEP wait and
    single steps on the JIT shift guest time (documented skew).
29. **Verification section**: every emulator run is headless; the integration recipe uses `config:Headless=yes`, the pinned RTC
    and `Debugger:Port=0 / Required=yes / StartPaused=yes`; the sound harness needs a sounding guest before the AICA tiers are
    touched; machine limits for agent runs (one workflow, `ninja -j8`, `ctest -j4`).

## C. After the devtools round (2026-10-10)

The round (runner and mock: design 4.2.1; panels: 4.3.1) and the corrections of 6.1.1 with header revision 4 were verified
and passed. Their verifiers left these points for the design owner; the answers are final.

30. **S04, a step that ends at an exception vector.** One step may end at a vector (a deferred interrupt is accepted before
    an instruction runs, 2.3.2). The step after it starts at the handler's first instruction with `SR.BL` set and cannot be
    interrupted: **a second step in a row that ends at a vector fails S04.** The "four times at most, then passed with a
    note" rule goes. The `control.stepOut` of S04 follows completed steps, so nothing is deferred there and the running
    tick within 1 s holds as the row says.
31. **S11, the ARM7 held in reset has an observable** (revised the same day): the register `ENABLED` of `dc.aica.arm7`
    (width 1; 1 = the ARM7 is clocked, 0 = it is held in reset — what `SoundCPU::DebugEnabled()` says, the same source the
    unwinder and the step preconditions use). It is one of the anchor registers of 2.8 for a server that lists `arm7`, and
    it is in every tick, so the pairing is checked inside one tick: `callstacks.arm7` is `[]` exactly when `ENABLED` reads 0
    in a paused tick, and has at least two frames when it reads 1; `state.getCallstack {arm7}` agrees. The guest-visible
    register ARMRST (the byte the SH4 reads at `0xA0702C00`) is NOT the observable: as merged, a machine reset leaves it
    reading 0 while the ARM7 is not clocked until the guest first writes it (the reset order of the AICA and of the sound
    CPU), which is a question for the AICA / ARM7 tier work and a console measurement, not for the debugger. The emulator
    gains the register as an integrator's correction before E5 (`dbg_regs_tables.cpp`, `dbg_regs.cpp` and their tests);
    the mock gains it with the runner follow-up.
32. **The widths of `dc.aica.dsp`** (`STEP` 8, `MDEC_CT` 16, `RBP` 16, `RBL` 8; decision 22) are normative for every server
    that lists those registers: 2.8's list of what is normative gains them, and S01 checks the width of each one it finds.
33. **Second-level nodes without registers** (`dc.sh4`, `dc.holly`, `dc.aica`, `dc.system`) is the emulator's choice, not
    contract; the runner does not check it.
34. **The gates are run with the default `TickMs` and `FullTickEvery`**; the contract's time bounds are written for them.
    "Only running ticks for 600 ms" (S06, S07) asks for no paused tick in the window and nothing else: the runner's demand
    for two ticks inside it goes.
35. **S17 and `state.getMemoryBinary`**: package F4 gives the mock the method together with the scenario part, so the mock
    keeps passing S17 without `--milestone` (5.5.3 stays as written; 4.5 says so).
36. **S18**: the `icache` / `ocache` tables have rows, or no rows and a note (3.3: a tier with a cache model has rows).
    **S16**: an ARM7 step asserts `stopId`, `reason` and no running tick; the PC is compared only on a line that is not
    control flow, as S04 does.
37. **A server that stops answering** (several request timeouts in a row): the runner fails the remaining scenarios as
    "not run" instead of sending each request into its timeout.
38. **Disassembly, narrow panels**: the instruction column keeps a minimum of 12 characters; a listing narrower than its
    columns scrolls sideways (wheel or scroll bar) and cuts nothing. That holds for any panel width, also for a layout made
    in a wider window and narrowed. The memory view gets the same rule in package F3 (as built it cuts its hex and text
    columns in a narrowed side-by-side layout).
39. **Disconnected UI, what "frame" is** (decision 19): text typed into a box and not sent is frame and is kept; an address
    box is rewritten only when its view moves or the target's address format changes.
40. **Focus over a reconnect**: a row control gets the focus back only while the focus is on the page body; a focus the
    user moved during the outage stays where they put it.
41. **Toolbar alignment is not contract.**
42. **`tests/debugger/regs`, the event listing**: every row of `dbg_events.h` is listed by exactly one node, except
    `EV_DEBUGGER_DSP_FORCED`, which is listed by none (replaces "at most once" of 6.1.1 item 2, which no longer noticed an
    event dropped from its node).
43. **`tests/debugger/sh4bp`, the "Per slice" paragraph**: the two stand-ins of that directory keep the latch test in front
    of the attention word (the B11 cost was measured on that form); the README says so instead of claiming the order of
    `slice_end()`.
44. **For E5**: the 1 MiB bound is answered with close code 1009, and the close frame is on the wire before the socket is
    closed, also while the client is still sending (S12 sees 1006 otherwise). If the transport of E1 does not do that, E5
    may fix it in `dbg_net.cpp` in a commit of its own, with a test in `tests/debugger/net`: the one exception to its
    "must not touch" list.
45. **Header revision 4 is applied** (branch `skmp/nulldc3-integ-v3`, merged). The one comment line it left at 180 columns
    (`debugger.h`, the `Init()` comment) is re-wrapped; the docs copies beside the design (`devtools-integration-open.md`,
    `-decisions.md`, `PLAN.md`) are part of 6.1.1 item 5.
