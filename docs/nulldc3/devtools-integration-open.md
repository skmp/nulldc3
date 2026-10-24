# Track 1 design, revision 3 — what is open, and header revision 4

Companion of `TRACK1-DESIGN.md` v3. Part 1: the questions that are still open, each with its options and a
recommendation, and the list of the questions that were answered on 2026-10-09. None of the open ones blocked the core
package (E5), which is done: milestone M1 is complete. Part 2: "header revision 4", the comment-only changes that
brought the frozen headers to what the document says: it is applied, and its list is kept as the record. Part 3:
header revision 5, applied with package J1: one declaration (the run flag as an `AtomicU32`) and one comment.
Revision 6 is applied (package R6 of the M2 round); its list is section 2 of `M2-ROUND.md`. Revision 7 is applied
(package R7 of the M3 round); its list is section 2 of `M3-ROUND.md`. Revision 8, comment-only, is applied (packages
RB, VTe and A64 of the platform round); its list is 2.3 of `PLATFORM-ROUND.md` (H8-1 to H8-4) and A64's brief (H8-5,
the one line of H15 below that it changes). Q10, the host for the arm64 backend, was answered by the platform round
(1.1).

History. The independent check of revision 3 added Q15 and Q16, widened Q4, added H17 to H19 and extended H8, H9 and
one row of H10. Its second pass added H20 and H21 and rewrote H8; it added no question. On 2026-10-09 thirteen of the
sixteen questions were answered (1.2); the answers are final and are in the design as normal text. The third pass of
the check (`design-v3-check-r3.json`, twelve items, all applied: design Appendix A.3) and those answers changed H8
(the configuration error of Q4, the port line of an IPv6 `Bind`) and H9 (which count "a client is connected" is),
added H22 and the `tests/debugger/selftest` texts behind the headers, and left one new question, Q17, which was answered
the same day (1.2). Questions keep
their numbers. Until a question is answered the design holds as written. On 2026-10-10, after the devtools round,
decisions 30 to 45 (`DESIGN-V3-DECISIONS.md`, section C) were folded into the design (its Appendix A.3 has the
sections). They answer what the verifiers of the round and of the corrections left, open no question, and record
header revision 4 as applied (part 2). After that the core (E5) was built, verified and merged, and the design was
brought to what was built (its 6.1, "As built", and Appendix A.3, "E5 as built"): that opened no question and put
no item on the list of header revision 5 (part 3). Then the PLAN package P-desktop and package J1 were built,
verified and merged; J1 made header revision 5, which is what part 3 now records (design 6.3, Appendix B, Appendix
A.3 "J1 as built"). No question was opened.

## 1. Open questions

### 1.1 Still open

**Q10. A host for the arm64 backend** (project owner). The arm64 JIT uses the interpreter fallback for breakpoints and
has not received the SH4 FPU fast-tier fixes; it is kept source-consistent only. Package A64 (design 6.3) needs a
machine that builds and runs it. No recommendation beyond: not before M1. Open because there is no such host yet.

**Answered by the platform round (2026-10-19 to 2026-10-24):** the owner's Mac (an Apple M2, macOS), an arm64 Linux
host the owner made available on 2026-10-22, and CI. The platform round (`PLATFORM-ROUND.md`) builds and runs the
arm64 JIT on the Mac (packages MACa and MACb), on the arm64 host (LA64, natively: Ubuntu 24.04, GCC 13) and in CI on
GitHub's `ubuntu-24.04-arm` and `macos-26` runners (CIe). Its package A64, added at the owner's word before the round's
gate, gave the arm64 JIT the debugger's native checks, its kept slice counter and its context at the slice end
(design 2.3.5; `PLATFORM-ROUND.md` 2.15, PL-D86) on those hosts: the engine there is `sh4-debug-native`, and the
contract gates M1 to M3 pass in full on every engine of both arm64 systems. The SH4 FPU fast-tier fixes of the arm64
JIT and the arm64 ARM7 recompiler stay in PLAN's backlog (PL-D46). The question is closed.

**Q13. The config section** (project owner). The keys stay under `Debugger:` for now (decision 27). When PLAN track 10
defines the settings model the section can be renamed with an alias for the old keys, or kept. It is decided with the
settings model of track 10; nothing in track 1 depends on it.

**Q14. Whether `dreamcast-devtools` becomes a submodule of the emulator repository** (project owner; PLAN, "Needs the
user"). Today the UI bundle is found as a sibling directory or by `NULLDC_DEVTOOLS_DIST`, and the tests that need the
UI repository (`debugger.net.interop_ws`, `debugger.disasm.dsp_playground`, `debugger.regs.panels`, and E5's
`debugger.contract.m1*` and `.hash.idlebp*`, five targets that run the contract runner) are tests without their data
when `NULLDC_DEVTOOLS_DIR` is not given; the other contract tests (`.grace`, `.config`, `.slow`, `.hash.off`,
`.hash.noclient`) need only the test program. A submodule would make the contract gate part of every checkout. It is
decided when M1 is merged, which it now is: the question is due.

**Answered by the user 2026-10-15:** yes, "as part of the rebrand": a submodule at `devtools/`, in package N1 (PLAN.md,
"Rebrand", has the proposed details).

### 1.2 Answered on 2026-10-09

The answers are final. "Sections" are the sections of `TRACK1-DESIGN.md` that state the answer; where the text already
had the answer's form, the change was to take the question out of it (the references, the "until answered" wording,
the "Open" column of Appendix A.3).

| Q | Answer | Sections |
| --- | --- | --- |
| Q1. The second run of the M3 gate | (a): the DSP part of the M3 gate is scenario S21, run with `--milestone M3 --dsp-program` | 5.5 (S21), 5.5.3, 4.5, 1, 6.2, 6.3; A.3 |
| Q2. The mock and the interrupt entry | (a): the mock's fabricated SH4 takes an interrupt at every vblank through a handler at `VBR + 0x600` | 4.2.1, 5.5.3; A.3 |
| Q3. A malformed message without an `id` | (a): only a well-formed message without `id` is a notification; a malformed one is answered `-32600` with `id: null` | 3.0, 4.2.1 (runner item 7), 5.5 (S12), 6.1 (E5); A.3 |
| Q4. `Required=yes` or `StartPaused=yes` without an enabled server | (b): a configuration error, one line on stderr and exit code 2; a selftest session (`Debugger:SelfTest` set) is excepted for `StartPaused` | 2.11, 2.2.9, 5.3, 5.4 (with the new test `debugger.contract.config`), 6.1 (E5: its row, the order of `dbg::Init`, its gate), 6.1.1 item 7 (`debugger.selftest.startpaused`); H8 and the `tests/debugger/selftest` texts below; A.3 |
| Q17. `StartPaused=yes` with a server that was enabled and cannot be started | (b): fatal outside a selftest session, as under `Required=yes` (one line on stderr, exit code 2) | 2.11 (the `StartPaused` row, "Keys that need a server, without one"), 6.1 (E5: `dbg::Init`, "Depends on"), A.3; H8 |
| Q5. The ARM7 callstack beyond two frames | (a): at least two frames, more when the stack scan finds them | 2.10; A.3 |
| Q6. `MDEC_CT` at width 16 before the DSP core is wired in | (a): width 16 already now; the value is the counter modulo 2^16 until then | 2.8, 6.1.1 item 1; A.3 |
| Q7. Protocol v2 compatibility of the client | (b): dropped after M1, with package F3: a result without `protocol` gets the version banner and nothing else is promised; until then the M1 client keeps what it has | 3.0, 3.1, 3.2, 3.4, 4.2, 4.3, 4.4, 6.2; A.3 |
| Q8. A supported minimum window | (a): the default layouts must be readable at 1366x768, with one e2e run at that size; part of the next devtools round if it fits, else of F3 | 4.3.1 item 7, 4.4, 6.2; A.3 |
| Q9. Who runs the Windows gates | (a): E5 merges on the syntax-only cross check with the MinGW-w64 cross compiler the development machine has (with `-fsyntax-only`, as the E0 header review used it) plus the review against the MSVC rules; the real MSVC build is made before the first push; until a Windows run of the breakpoint gates, x64 on Windows uses the interpreter fallback with its notice | 5.8, 6.1 (the gate row of E5), 6.3 (W64); A.3 |
| Q11. The sounding guest | (a): `tests/debugger/guest/dsptest/` is written as the first step of E6 and serves the sound harness and, with its DSP program part, the M3 gate | 5.5, 5.6, 6.3 (E6, E8); A.3 |
| Q12. `Sh4Context::CpuRunning` as an atomic | (a): in one small package after M1, together with the fix of the x64 code cache over thread starts | 2.3.7 items 9 and 10, 5.7, 6, 6.3 (package J1); A.3 |
| Q15. How the DSP `bytes` are written | (a): 8 space-separated upper-case pairs, each word high byte first | 3.3, 5.5 (S10), 4.2.1 (runner item 13); A.3 |
| Q16. Which `Exit.*` keys make a run automated | (a): "automated" is `RunControl::automated()`: headless, or one of `Exit.AfterVblanks`, `Exit.AfterSeconds`, `Exit.OnGuestExit` | Terms, 2.11; A.3 |

## 2. Header revision 4 (comment-only)

**Applied.** Header revision 4 is in the tree (decision 45): it was applied on branch `skmp/nulldc3-integ-v3`, in a
commit of its own, together with the corrections of design 6.1.1 (its items 1 to 7) on the same branch, and that
branch is merged into the integration branch `skmp/nulldc3`. All 22 items, H1 to H22, are in the seven headers they
name, and no declaration changed. **The list below is kept as the record** of what the revision changed; it is no
longer a list of work. "Current" in it is the text before the revision, which the tree no longer has, and "New" is
what the tree has, with two differences that came behind the list, in commit `ecc4652c` on the integration branch,
behind the merge, and a third that came with header revision 5:

- H8: the new `Init()` comment had one line of 180 columns ("... runs without a server. TickMs and FullTickEvery
  below 1 ..."), and the revision was applied with it. The line is re-wrapped, line breaks only; the words are those
  of the list.
- "Not a header": the "Per slice" sentence of `tests/debugger/sh4bp/README.md` was applied as written below and then
  changed by decision 43. The two stand-ins of that directory keep the latch test in front of the attention word (the
  B11 cost was measured on that form), and the README says so instead of claiming the order of `slice_end()` for
  them.
- H1: the comment of `g_parked` in `debugger.h` no longer has the "New" text of this list. Package J1 made
  `IsRunning()` one word of the machine, and header revision 5 reworded the comment (section 3).

One of the corrections of design 6.1.1 that went with the revision was reworded as well (it is not in this list; see
"What revision 4 does not contain"): the expectation of `debugger.regs.tables` is, by decision 42, that every row of
`dbg_events.h` is listed by exactly one node, except `EV_DEBUGGER_DSP_FORCED`, which is listed by none (commit
`fbb01dc1`, behind the merge as well). The whole tests project was run again on the head that has both commits: 171
of 171 ctest targets passed. What comes after revision 4 is section 3.

The list was written for the integrator, to apply before E5 starts. "Current" is the text at the integration head of
that time (the merge of package B64); every item changes comments only, so every object file stays byte for byte what
it is. Items H1 to H7 are the
requests of the run-control verifiers that revision 3 of the headers left open; H8 to H16 bring the headers to
decisions of design revision 3; H17 to H19 are three comments that the independent check of the revision found still
to disagree with the design and with the code; H20 and H21 are two that its second pass found to disagree with the
design (a write to main memory on a host without the virtual memory layout; who calls `core::Stop()`); H22 is one that
its third pass left (the case of the hex in a frame's location). H8 and H9 carry what the answers of 2026-10-09 and
that third pass changed. The corrections of design 6.1.1 were made on the same branch, in commits of their own.

### H1. `libswirl/debugger/debugger.h`, comment of `g_parked`: the terms of `IsRunning()`

Current:
```
// (E) is in a debugger stop: latched, parked or executing a step. SuperH4_impl::IsRunning() is
// CpuRunning || g_parked || (the cpu is stopped for a reset), so that a parked machine counts as running for
// the host UI.
```
New:
```
// (E) is in a debugger stop: latched, parked or executing a step. SuperH4_impl::IsRunning() is
// CpuRunning || g_parked || (the cpu is stopped for a reset) || (the machine has been asked to stop, or its
// guest has exited, and the emulator thread has not taken its stop callback), so that a parked machine and one
// on its way to a stop count as running for the host UI.
```

### H2. `debugger.h`, `Park()`, the bullet on the first `Park()` after a host stop

Current:
```
// - The first Park() of a thread that follows a thread exit taken in a debugger stop (the host stopped the
//   machine while the thread before it was parked) is a resume of that stop: the debugger pause that was in
//   force before the host stop is not applied again, and dbg::emu calls dbg::sh4::PrepareResume() there, so
//   that the instruction of the stop executes and its breakpoint does not stop again at once.
```
New: the same, followed by
```
//   This holds when the guest is where that stop was published: no step has executed since and the pc was
//   not written. Otherwise nothing is resumed and a breakpoint on the new pc stops.
```

### H3. `debugger.h`, the park-context block in front of `PARK_RUN`: a thread whose start is refused

Current:
```
	    OnThreadEntered(); ...; start the cpu; emu_started.Set();
	    for (;;) {
	        if (Park() == PARK_RUN)
	            sh4_cpu->Run();
```
and
```
	dc_run starts the cpu at thread start and after a guest reset, through the machine's own start function,
	which sets CpuRunning (SuperH4::Start) and leaves the cpu stopped when a stop has been requested; everywhere
	else Park() owns CpuRunning, g_parked and g_stop_latched.
```
New:
```
	    OnThreadEntered(); ...; park = start the cpu; emu_started.Set();
	    for (;;) {
	        if (park && Park() == PARK_RUN)
	            sh4_cpu->Run();
	        park = true;
```
and
```
	dc_run starts the cpu at thread start and after a guest reset, through the machine's own start function,
	which sets CpuRunning (SuperH4::Start) and leaves the cpu stopped when a stop has been requested; everywhere
	else Park() owns CpuRunning, g_parked and g_stop_latched. A thread whose cpu is left stopped at thread start
	makes no Park() in front of its first evaluation: the request may have come in before OnThreadStarting(),
	where Kick() finds no thread.
```

### H4. `debugger.h`, first paragraph of the `Park()` comment

Current:
```
// (E), dc_run only; lifetime Running or Parked; the caller holds no lock. Called before the first
// SuperH4::Run() of a thread, after emu_started is set, and after every evaluation of the machine's stop
// reasons that did not end the thread.
```
New:
```
// (E), dc_run only; lifetime Running or Parked; the caller holds no lock. Called before the first
// SuperH4::Run() of a thread, after emu_started is set, unless the machine's start function left the cpu
// stopped there (dc_run then evaluates first), and after every evaluation of the machine's stop reasons that
// did not end the thread.
```

### H5. `debugger.h`, `Park()`, "Entering a stop": the window title

Current:
```
//   breakpoints and a pending step-out, the listener is invoked, the window title becomes
//   "nullDC 3.0.0 [paused by debugger]". The park loop then waits, without limit, for a call, for a
```
New:
```
//   breakpoints and a pending step-out, the listener is invoked, the window title becomes
//   "nullDC 3.0.0 [paused by debugger]" (it is put where it is not on the window already and stays until the
//   running machine writes its frame rate; the emulator thread makes no window call behind its stop
//   callback). The park loop then waits, without limit, for a call, for a
```

### H6. `debugger.h`, `Kick()`

Current:
```
// reasons. SuperH4_impl::Stop() calls it after it cleared CpuRunning and g_parked. Without a parked thread
// it does nothing.
```
New:
```
// reasons. SuperH4_impl::Stop() calls it after it cleared CpuRunning and g_parked. Without a parked thread
// it does nothing: a kick made before OnThreadStarting() is not kept, and dc_run evaluates the machine's stop
// reasons itself when the cpu was left stopped at thread start.
```

### H7. `debugger.h`, `SliceHook()`, lifetime Running

Current:
```
//   1. runs the queued CC_READ calls (dbg_emu.h), one at a time
//   2. latches the stop (emu::LatchStop) if a freezing cause is recorded (event breakpoint, sound
//      breakpoint), or if g_in_sleep is clear and a pause was requested or a call of another class is queued.
```
New:
```
//   1. runs the queued CC_READ calls (dbg_emu.h), one at a time; a queued CC_EXEC call is completed with
//      kErrNotPaused
//   2. latches the stop (emu::LatchStop) if a freezing cause is recorded (event breakpoint, sound
//      breakpoint), or if g_in_sleep is clear, CpuRunning is set and a pause was requested or a call of
//      another class is queued. (With CpuRunning clear the machine itself is about to leave Run(), and the
//      park context that follows takes them.)
```

### H8. `debugger.h`, the configuration block and `Init()`: the default in automated runs and selftest sessions (decision 27), a server that cannot be started, the keys that need a server (Q4), the port line

Current:
```
	  read by Init()          Enabled yes, Bind 127.0.0.1, Port 55543, Required no, AllowedHosts (empty),
	                          WebRoot (empty), TickMs 200, FullTickEvery 5
	  read by dbg::emu        StartPaused no, ForceFallback no
```
New:
```
	  read by Init()          Enabled yes (no in an automated run: headless, or an Exit.* key that ends the
	                          run; and no in a selftest session: Debugger:SelfTest set), Bind 127.0.0.1,
	                          Port 55543, Required no, AllowedHosts (empty), WebRoot (empty), TickMs 200,
	                          FullTickEvery 5
	  read by dbg::emu        StartPaused no (Init() reads it as well, for its configuration check),
	                          ForceFallback no
```
and the whole comment of `Init()` (design 2.11: the two defaults; what "cannot be started" covers, what a loopback
address is, what stays behind and how the process ends; `Required` and `StartPaused` without an enabled server, the
answer to Q4; the stream of the port line and an IPv6 `Bind` in it).

Current:
```
// (U), once, at the end of reicast_init(); lifetime NoMachine. With Debugger:Enabled it starts the server
// and the core thread and prints exactly "Debugger: http://<bind>:<port>/" with the bound port; it blocks
// for the bind only. A bind that fails ends the process with exit code 2 when Debugger:Required is set;
// otherwise it is logged and the emulator runs without a server. Without Debugger:Enabled nothing is
// started; the hooks and helpers below work all the same. Does not change signal dispositions.
```
New:
```
// (U), once, at the end of reicast_init(); lifetime NoMachine. With Debugger:Enabled (in an automated run and
// in a selftest session: only when the key is set explicitly, on the command line or in emu.cfg) it starts
// the server and the core thread and prints exactly "Debugger: http://<bind>:<port>/" with the bound port, as
// one line on stdout, flushed (a Bind that contains a colon is printed in brackets: http://[::1]:<port>/); it
// blocks for the bind only. A server cannot be started when core::Start or net::Server::Start returns false
// (a bind that fails, an address that does not resolve, a Port outside 0..65535, a thread or the socket
// library that cannot be set up) and when Bind is not a loopback address while AllowedHosts is empty; a
// loopback address is one of the texts 127.0.0.1, localhost, ::1, compared without regard to case, and
// nothing is resolved for the comparison. The core thread, if it was started, is then ended again
// (core::Stop): no core thread and no listener stay behind. With Debugger:Required, or with
// Debugger:StartPaused outside a selftest session, the process ends: one line on stderr and exit(2), with no
// nulldc-exit line; otherwise the failure is logged and the emulator runs without a server. TickMs and FullTickEvery below 1 are replaced by their defaults; both are handed to the
// core before it is started. Without Debugger:Enabled nothing is started, and a key that needs the server is
// a configuration error: with Debugger:Required set, or with Debugger:StartPaused set outside a selftest
// session (whose test resumes the start park), the process ends the same way, one line on stderr that names
// the key and exit(2), decided directly behind Enabled. The hooks and helpers below work all the same. Does
// not change signal dispositions.
```

### H9. `debugger.h`, comment of `g_event_on`; `dbg_events.h`, the rate classes: which count "a client is connected" is; a disabled breakpoint opens the gate; the one entry made with the gates closed

Design 2.6: the gates follow the core's own count of open clients, not the flag of the transport.

Current (`debugger.h`):
```
// One gate per event: 1 while a client is connected (net::Server::HasClients) and the event is EVR_LOW or an
// event breakpoint on it exists, else 0. Written by the debugger core; read by any thread through DBG_EVENT.
```
New:
```
// One gate per event: 1 while a client is connected and the event is EVR_LOW or an event breakpoint on it
// exists (enabled or not), else 0. "A client is connected" is the debugger core's own count of open clients,
// kept from the OnOpen / OnClose of its net::Handler; net::Server::HasClients is not consulted. Written by
// the debugger core; read by any thread through DBG_EVENT.
// One entry is made with every gate closed: when the grace period expires (kClientGraceMs) the core appends
// dc.debugger.note to the event log itself, not through DBG_EVENT and not through EventSlow: no client is
// connected at that moment. The next client finds the entry in its first tick.
```
Current (`dbg_events.h`):
```
	- EVR_LOW:  logged whenever a client is connected
	- EVR_HIGH: can fire at frame rate or faster; logged only while an event breakpoint on it exists
```
New:
```
	- EVR_LOW:  logged whenever a client is connected (and, with none connected, the one note of the grace
	            period: debugger.h, g_event_on)
	- EVR_HIGH: can fire at frame rate or faster; logged only while an event breakpoint on it exists (enabled or
	            not)
```

### H10. `libswirl/debugger/dbg_events.h`, the comments at the end of six rows

| Row | Current | New |
| --- | --- | --- |
| `EV_DMAC_START`, `EV_DMAC_END` | `/* ch2 and sort DMA handlers (E3) */` | `/* hw/pvr/pvr_sb_regs.cpp: ch2 and sort DMA handlers (E3) */` |
| `EV_TA_LIST_INIT` | `/* hw/pvr (E3) */` | `/* hw/pvr/pvr_sb_regs.cpp (E3) */` |
| `EV_CORE_RENDER_START` | `/* hw/pvr (E3) */` | `/* hw/pvr/pvr_sb_regs.cpp (E3) */` |
| `EV_CORE_RENDER_END` | `/* hw/pvr (E3) */` | `/* hw/pvr/spg.cpp (E3) */` |
| `EV_DEBUGGER_DSP_FORCED` | `/* debugger/dbg_emu.cpp: the per-sample DSP path was forced on */` | `/* nothing raises it: per-sample forcing is transitional and not built; no node lists it */` |
| `EV_DEBUGGER_NOTE` | `/* debugger/dbg_emu.cpp, dbg_core.cpp (E4a, E5) */` | `/* debugger/dbg_core.cpp: the grace period expired (appended with the gates closed), a client of another protocol version (E5) */` |

### H11. `libswirl/debugger/dbg_emu.h`, comment of `StopCause`: the priority as built

Current:
```
// Why the state is "paused": the wire value of executionState.reason. When several causes meet in one stop
// the one published is the first of Breakpoint, Event, Step, Pause.
```
New:
```
// Why the state is "paused": the wire value of executionState.reason. When several causes meet in one stop
// the one published is the first of Reset, Start, Breakpoint, Event, Step, Pause.
```
and in the enumeration,
```
	HostStopped,	// "host-stopped"  lifetime Stopped: the host UI stopped the machine
```
becomes
```
	HostStopped,	// "host-stopped"  lifetime Stopped: the host stopped the machine, or the machine was created
					//                 and no thread has started yet
```

### H12. `dbg_emu.h`, `NOTICE_DSP_FORCED` (decision 26)

Current:
```
	NOTICE_DSP_FORCED          = 8,		// the per-sample DSP path is forced on by the debugger
```
New:
```
	NOTICE_DSP_FORCED          = 8,		// the per-sample DSP path is forced on by the debugger. Transitional: it can
										// be set only while the AICA has a path without the DSP, and goes with it
```

### H13. `libswirl/debugger/dbg_mem.h`: the ARM7's view, and the one register read function

Current:
```
	dbg::mem: guest memory for the debugger (dbg_mem.cpp). Byte granular, any address, any length. It reads
	the backing stores and register arrays directly and never calls a guest memory handler, so a read changes
	nothing and cannot spin or end the process, whatever the address. There is no address translation: MMU
	builds are read through the same fixed map.
```
and, under `Target::Arm7`,
```
	  0x00800000-0x00807FFF  AicaRegs
```
New:
```
	dbg::mem: guest memory for the debugger (dbg_mem.cpp). Byte granular, any address, any length. It reads
	the backing stores and register arrays directly. It never calls a guest memory handler, and it calls the
	read function of a register for two bytes only (0x00802C00 and 0x00802C01 of the ARM7 target), where that
	function copies a byte and changes nothing: a read changes nothing and cannot spin or end the process,
	whatever the address. There is no address translation: MMU builds are read through the same fixed map.
```
and
```
	  0x00800000-0x00807FFF  AicaRegs     as a read by the ARM7 delivers them: the register file, with the
	                                      interrupt level L at 0x2D00 (without the acknowledge of that read),
	                                      0 at 0x2D04, and at 0x2C00 / 0x2C01 the two bytes of the register
	                                      file where the SH4 has ARMRST and VREG
```

### H14. `libswirl/debugger/dbg_callstack.h`, `Unwind()`: the ARM7 held in reset (decision 13)

Current:
```
// Writes at most maxFrames frames, innermost first, and returns their number: at least 1 for Sh4 and
// Arm7 when maxFrames is not 0, and 0 for Dsp and when no machine exists. maxFrames <= kMaxCallstackFrames.
```
New:
```
// Writes at most maxFrames frames, innermost first, and returns their number: when maxFrames is not 0, at
// least 1 for Sh4, and for Arm7 while the ARM7 runs (the program counter, and the link register as frame 1
// when maxFrames allows it); 0 for Arm7 while the ARM7 is held in reset (SoundCPU::DebugEnabled() is
// false), for Dsp and when no machine exists. maxFrames <= kMaxCallstackFrames.
```

### H15. `libswirl/hw/sh4/sh4_if.h`, `SH4DBG_CTX_AT_SLICE`: which backends report it

Current:
```
	// A backend states the bit itself when it meets all of this; none does by default, and the interpreter and
	// the x64 JIT do once they implement the debugger support of this header. Whatever reads SH4 state inside
```
New:
```
	// A backend states the bit itself when it meets all of this; none does by default. The interpreter and the
	// x64 JIT do; the arm64 JIT does not. Whatever reads SH4 state inside
```

(Since header revision 8 the second line says `x64 and arm64 JITs do. Whatever reads SH4 state inside`: H8-5 of the
platform round's package A64, which made the arm64 JIT state the bit, design 2.2.8.)

### H16. `libswirl/debugger/dbg_core.h`, the header comment: the order of what a client receives

Current:
```
	grace period after the last client left. It never holds L while it waits for (E). The tables themselves
	follow the one rule: they are touched inside calls and in the listener only.
```
New:
```
	grace period after the last client left. It never holds L while it waits for (E). The tables themselves
	follow the one rule: they are touched inside calls and in the listener only. A tick is handed to the
	server inside the call or the listener that built it, so ticks reach a client in the order they were
	built; the response to debugger.describe is handed over inside the same call as its tick, directly in
	front of it.
```

### H17. `debugger.h`, comment of `g_stop_latched`: where `UpdateSystem()` returns

The form before header revision 3; design 2.2.3 and the code (`slice_end_attention()`) have the latch behind the test
of the attention word. The sh4bp README rows below correct the same wording.

Current:
```
//   UpdateSystem()  returns 1 as its first statement: no emulated time passes, no scheduler callback runs,
//                   the slice hook is not called
```
New:
```
//   UpdateSystem()  returns 1 behind its test of g_attn (ATTN_LATCHED): no emulated time passes, no scheduler
//                   callback runs, the slice hook is not called
```

### H18. `debugger.h`, `OnGuestStateReplaced()`: everything that is dropped

Design 2.2.1 names the deferred interrupt acceptance and the owed resume; `dbg_emu.cpp` clears `g_intc_deferred` and
`resumeOwed` there.

Current:
```
// The debugger drops what belonged to the old state: the owed remainder and resume exemption of the SH4
// breakpoint mechanism, g_sleep_cut, temporary breakpoints, a pending step-out.
```
New:
```
// The debugger drops what belonged to the old state: the owed remainder and resume exemption of the SH4
// breakpoint mechanism, g_sleep_cut, a deferred interrupt acceptance (g_intc_deferred), temporary breakpoints,
// a pending step-out, and the resume that a thread exit taken in a debugger stop left owed.
```

### H19. `dbg_emu.h`, `StopCause::Reset`: also when a pause was wanted

Design 2.3.1 and the code (`Park()` folds a guest reset into a `reset` stop while a debugger stop was in force or one
is wanted). Same enumeration as H11.

Current:
```
	Reset,			// "reset"         a reset made while a debugger stop was in force
```
New:
```
	Reset,			// "reset"         a reset made while a debugger stop was in force or a pause was wanted
```

### H20. `libswirl/debugger/dbg_mem.h`, `Write()`; `libswirl/hw/sh4/sh4_if.h`, `DebugDiscardPage()` and `DebugBackend()`: main memory without the virtual memory layout

Design 2.7 (D7): a host without the large mapping writes main memory as well, with the blocks of each touched page
discarded through the backend. The header still has v2's refusal. The writer is M2 work (`Write()` answers
`kErrNotSupported` today), so no code disagrees with either text.

Current (`dbg_mem.h`):
```
// Writable are main memory, video memory and wave memory. A write to main memory first discards the compiled
// SH4 blocks of every page it touches and is refused when the build cannot do that (no virtual memory
// layout); a write to video memory first unlocks pages the texture cache locked; a write to wave memory
// discards the ARM7's compiled code.
```
New:
```
// Writable are main memory, video memory and wave memory. A write to main memory first discards the compiled
// SH4 blocks of every page it touches: through bm_LockedWrite where the virtual memory layout is in use,
// through SuperH4Backend::DebugDiscardPage of the active backend where it is not (virt_ram_base is null); a
// write to video memory first unlocks pages the texture cache locked; a write to wave memory discards the
// ARM7's compiled code.
```
Current (`sh4_if.h`, `DebugDiscardPage()`):
```
    // Discards every compiled block that overlaps the 4 KiB page of the guest address, in every mirror of
    // that page, and nothing else: page protection and the marks of pages that hold data stay as they are.
    // Called when the debugger arms or disarms the address.
```
New:
```
    // Discards every compiled block that overlaps the 4 KiB page of the guest address, in every mirror of
    // that page, and nothing else: page protection and the marks of pages that hold data stay as they are.
    // Called when the debugger arms or disarms the address, and when the debugger writes main memory on a
    // host without the virtual memory layout.
```
Current (`sh4_if.h`, `DebugBackend()`):
```
    // For the debugger (run control: DebugCaps, Step, SliceCyclesLeft; the SH4 breakpoint state:
    // DebugDiscardPage, DebugDiscardFlow) and for the sound regression harness (DebugCaps); it is there in builds
    // without the debugger as well. Emulator thread, or the thread that holds the debugger's lifetime lock while
    // no emulator thread exists.
```
New:
```
    // For the debugger (run control: DebugCaps, Step, SliceCyclesLeft; the SH4 breakpoint state:
    // DebugDiscardPage, DebugDiscardFlow; the memory writer of dbg_mem: DebugDiscardPage) and for the sound
    // regression harness (DebugCaps); it is there in builds without the debugger as well. Emulator thread, or
    // the thread that holds the debugger's lifetime lock while no emulator thread exists.
```

### H21. `libswirl/debugger/dbg_core.h`, `Stop()`: also called by `dbg::Init`

Design 2.11 and 6.1: `dbg::Init` starts the core in front of the server (a client that connects must find its
handler serving), so a server that then cannot be started leaves a core thread and a run-state listener to take
away again. Same header as H16.

Current:
```
// (U), from dbg::Term, once after a successful Start(); any lifetime state. Removes the listener, ends
// (C) and joins it; a call (C) is waiting for is given up as it would be after its timeout. The caller
// must not hold L. Afterwards nothing of the core runs and the server is no longer used.
```
New:
```
// (U), once after a successful Start(): from dbg::Term, or from dbg::Init when the server could not be
// started behind that Start() (dbg::Term then makes no second call); any lifetime state. Removes the
// listener, ends (C) and joins it; a call (C) is waiting for is given up as it would be after its timeout.
// The caller must not hold L. Afterwards nothing of the core runs and the server is no longer used.
```

### H22. `libswirl/debugger/dbg_callstack.h`, `FrameKind::Stack`: the case of the hex in a frame's location

Design 2.10 and 3.2: the emulator writes the offset in lower-case hex, as `symbols::Format` writes the offset of a
symbol. Same header as H14.

Current:
```
	Stack,		// wire location "stack+0x<stackOffset in hex>"
```
New:
```
	Stack,		// wire location "stack+0x<stackOffset in lower-case hex>"
```

### Not a header, same commit: `tests/debugger/sh4bp/README.md`, the call-site table

Two rows still describe the form before header revision 3 (design 6.1.1 item 3).

Current:
```
| | first statement of `UpdateSystem()` | `if (unlikely(dbg::g_stop_latched)) return 1;` |
```
and
```
| | the wait loop of the `sleep` handler | `dbg::g_in_sleep` set around the loop; in front of each `UpdateSystem_INTC()`: `if (dbg::g_stop_latched) { dbg::g_sleep_cut = true; ` leave and re-execute `}` |
```
New:
```
| | the slice end of `UpdateSystem()` | `dbg::g_attn` is tested first; behind it, `if (dbg::g_stop_latched) return 1;` (the latch has its bit in the word, `ATTN_LATCHED`) |
```
and
```
| | the wait loop of the `sleep` handler | `dbg::g_in_sleep` set around the loop; in a slice of the wait that reported something and accepted nothing: `if (dbg::g_stop_latched) { dbg::g_sleep_cut = true; ` leave and re-execute `}` |
```
The paragraph "Per slice" of the same README ("tests the stop latch and the attention word in `UpdateSystem()`")
becomes "tests the attention word in `UpdateSystem()`, and the stop latch behind it".

### Not a header, same commit: `tests/debugger/selftest/`, the test `startpaused`

Design 6.1.1 item 7, the answer to Q4: `Debugger:StartPaused=yes` without an enabled server becomes a configuration
error when E5's `dbg::Init` is there, and this test is a run with that key, without a session and without a server.
The change is inert until then (the `dbg::Init` of today is a stub that reads no key). The other lines of the file
stay: `emuhang.parked` is a selftest session without the key, and `sh4bp`, `sh4bpboot` and `sh4bpbase` are selftest
sessions, which the rule excepts.

Current (`CMakeLists.txt`):
```
nulldc_debugger_selftest(startpaused    MODE parked ENGINES jit SELFTEST NONE CONFIG Debugger:StartPaused=yes ARGS --vblanks 5)
```
New:
```
nulldc_debugger_selftest(startpaused    MODE parked ENGINES jit SELFTEST NONE CONFIG Debugger:StartPaused=yes Debugger:Enabled=yes Debugger:Port=0 ARGS --vblanks 5)
```
Current (`README.md`):
```
| `debugger.selftest.startpaused` | the same for `Debugger:StartPaused=yes` without a session: the start park waits without limit |
```
New:
```
| `debugger.selftest.startpaused` | the same for `Debugger:StartPaused=yes` without a session, with the server enabled (`Debugger:Enabled=yes`, `Debugger:Port=0`: without it the key is a configuration error) and no client: the start park waits without limit |
```

### What revision 4 does not contain

- `Sh4Context::CpuRunning` as `std::atomic<u32>` (`sh4_if.h`): a declaration. It is made in package J1, directly
  after M1, as a numbered header revision of its own (the answer to Q12; design 2.3.7 item 10, 6.3). Done: it is
  header revision 5, and the type is `AtomicU32` (section 3).
- The removal of `NOTICE_DSP_FORCED`, `EV_DEBUGGER_DSP_FORCED` and the capability string `dsp-forced`: declarations;
  they go in the header revision of the M3 work, when the batch path is gone (design 2.5.5).
- The widths of `dc.aica.dsp` and the event list of the `dc` node, each with the expectation of its test: not
  headers; design 6.1.1 items 1 and 2.
- `libswirl/debugger/dbg_core_private.h`: a new file of package E5, not a frozen header (design 6.1, "Owns"): what
  `dbg_core.cpp`, `dbg_main.cpp`, `dbg_events.cpp` and `dbg_selftest_core.cpp` share. It is also how the tick period
  and the full-tick count that `dbg::Init` reads reach the core (`core::Configure`, called in front of
  `core::Start`): the frozen `dbg_core.h` has no parameter for them and gets none. `dbg_core.h` stays as it is but
  for the comments of H16 and H21.
- The registration of the selftest session `regs` in ctest: not a header; design 6.1.1 item 6.
- A comment for what a memory slice answers above `addressMax`: none is needed. `dbg::mem::Read` keeps wrapping at
  2^32, as its comment says; the core cuts the length it hands over (design 2.7).
- A declaration for `state.recordSgcFrames`: none is needed. The method is an exec call when the target is paused
  and a table call when it runs, and the core answers the lifetime errors itself (design 3.3).

## 3. Header revision 5 (applied with J1)

**Applied.** Header revision 5 came with package J1 (branch `skmp/nulldc3-j1`, merged into `skmp/nulldc3`; the
declaration in commit `48ad5abe`, the comment of `g_parked` in commit `36d1f261`, with the fix round of the
verification) and has two items: one declaration and one comment. It is not the comment-only revision this section
was kept open for: the declaration is the one that the design had listed for J1, made as a numbered revision as its
D6 asks. Below is the record, in the form of section 2.

### R5.1. `libswirl/hw/sh4/sh4_if.h`, `Sh4Context::CpuRunning`: the run flag is an `AtomicU32`

A declaration. The word keeps its offset, its size and its meaning; the generated code of both recompilers loads
the same 32 bits. The type is `AtomicU32` (`libswirl/oslib/atomic_u32.h`, new, included from `types.h`) and not
`std::atomic<u32>`: the context is an anonymous struct in an anonymous union, where g++ takes no member with a
constructor, its memory is constructed by nothing, and it is cleared with `memset` and saved with `memcpy` (design
2.3.7 item 10).

Current:
```
			volatile u32 CpuRunning;
```
New:
```
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

### R5.2. `libswirl/debugger/debugger.h`, comment of `g_parked`: `IsRunning()` is one word

A comment. It supersedes H1 of revision 4 (design 2.2.5: `IsRunning()` is `SuperH4_impl::thread_live`, true from
`Resume()` until the emulator thread takes its stop callback). The rest of the comment of `g_parked` is unchanged.

Current (the "New" text of H1):
```
// (E) is in a debugger stop: latched, parked or executing a step. SuperH4_impl::IsRunning() is
// CpuRunning || g_parked || (the cpu is stopped for a reset) || (the machine has been asked to stop, or its
// guest has exited, and the emulator thread has not taken its stop callback), so that a parked machine and one
// on its way to a stop count as running for the host UI.
```
New:
```
// (E) is in a debugger stop: latched, parked or executing a step. SuperH4_impl::IsRunning() reads neither it
// nor CpuRunning: it is one word of the machine, set from the start of an emulator thread until that thread
// has taken its stop callback, so that a parked machine and one on its way to a stop count as running for the
// host UI.
```

### What revision 5 does not contain

- Anything for the points in which the core (E5) was built differently from the design, or where the design was
  silent (design 6.1, "As built"). They were held against the comments of the frozen headers, and none needs a
  change:
  - `debugger.h`, `Init()`: "one line on stderr and exit(2)" is the line of `dbg::Init`. Where the transport
    failed, its own line stands in front of it, and that one is `dbg_net.h`'s ("one line on stderr says why"). The
    two comments together say what happens.
  - `dbg_callstack.h`: "at most kScanWords words" covers a scan that ends at the end of the memory the stack
    pointer is in.
  - `dbg_symbols.h`: "named, defined symbols" covers an absolute symbol, and says nothing about the order of several
    symbols at one address, which the design states (2.10).
  - `debugger.h`, `g_event_on`, and `dbg_core.h`: the grace note and the order of ticks are as the comments have
    them.
- The re-wrap of the 180-column line that H8 left in the `Init()` comment of `libswirl/debugger/debugger.h`: it was
  done on the integration branch behind the merge of revision 4 (commit `ecc4652c`), with line breaks only (decision
  45; section 2, first paragraph).
- The removal of the three forcing names: declarations, which went with the revision of the M3 work ("What revision 4
  does not contain"): header revision 7 removed `NOTICE_DSP_FORCED` and `EV_DEBUGGER_DSP_FORCED`, F4a the capability
  string `"dsp-forced"`.
- The register `ENABLED` of `dc.aica.arm7` (decision 31, design 6.1.1 item 8): no frozen header is touched. It is a
  row of `dbg_regs_tables.cpp` and a selector in `dbg_regs.cpp` with E3's private header `dbg_regs_private.h`; the
  comment of `Unwind()` in `dbg_callstack.h` (H14) already names the same source, `SoundCPU::DebugEnabled()`.
- The removal of `resetting`, `stopping` and `StopForReset()` (design 2.2.5, 6.1.2): they are members of
  `SuperH4_impl`, whose header is not one of the frozen ones.

**Revision 6 is applied** (package R6 of the M2 round); its list is section 2 of `M2-ROUND.md`. **Revision 7 is
applied** (package R7 of the M3 round); its list is section 2 of `M3-ROUND.md`.
