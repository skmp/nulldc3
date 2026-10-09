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
