# Tests

A standalone CMake project. Every suite but one compiles the libswirl translation units it tests (with the
emulator's own definitions and flags, from `reicast/cmake/config.cmake`) and links them against stubs for the
rest. The system tests run the emulator itself, which the project builds for them.

The suites of `debugger/` are CMake projects of their own, with their own flags, which this project adds: "The
debugger's suites" below.

## Getting the vectors

The test data are git submodules:

    git submodule update --init tests/vendor/sh4-tests                 # SingleStepTests/sh4, 233 x 500 vectors, ~90 MB
    git submodule update --init tests/vendor/arm7di-tests-dreamcast    # 45 ARM7 test binaries

A suite whose data is missing is still built; its test is reported as skipped and configure says which
submodule to fetch. Other checkouts can be used with `-DNULLDC_SH4_TESTS_DIR=`, `-DNULLDC_ARM7_TESTS_DIR=` and
`-DNULLDC_WREN7_DIR=` (wren7-rtl, default `../wren7-rtl` next to the repository if it is there).

The system tests that boot a KallistiOS program take the programs by path: `-DNULLDC_ELF_DIR=<directory with
2ndmix.elf and hello.elf>`, the two KallistiOS examples of those names. The option has no default. Without it
configure says that the programs are missing, and eleven tests are skipped: `system.boot.2ndmix.jit`,
`system.boot.2ndmix.interp`, `system.boot.hello.jit`, `system.boot.hello.interp`, `system.determinism` and
`system.frame.refsw2`, and the first five again as `system.hle.*`. The other system tests run on three programs
of a few instructions that are part of the suite, so a run without the option boots no KallistiOS, renders no
frame and compares no state hash between runs. The selftests of the debugger that boot `2ndmix.elf` are skipped
with them, nineteen tests: `debugger.selftest.core`, `.regs`, `.sh4bp`, `.sh4bpboot`, `.sh4bpbase`,
`.emurun.reads`, `.emurun.pending`, `.emurun.pauses` and `.hostcycles`, each with its `.interp`, and
`debugger.selftest.sh4bp.sessions`. And so are the fifteen `debugger.contract.*` tests that boot it (the two on the
sounding guest of the tree, `debugger.contract.m2arm7.guest` and its `.interp`, need the devtools checkout alone),
and the four `debugger.sound.lines.2ndmix.*` tests. The five `debugger.selftest.arm7*` tests and the twelve
`debugger.sound.neutral.*` tests boot the guest of the tree, and run without the option.

A skipped test has not run and is not a pass: `ctest` exits with 0 when everything that did run passes, also
when that is only the tests that need no data. With `-DNULLDC_TESTS_REQUIRE_DATA=ON` missing data fails its
tests instead (the submodules, the wren7-rtl checkout and its build outputs, the programs of `NULLDC_ELF_DIR`
alike). CI should configure that way.

## Building and running

    cmake -S tests -B <build dir> -G Ninja -DNULLDC_ELF_DIR=<directory with 2ndmix.elf and hello.elf> \
          -DNULLDC_DEVTOOLS_DIR=<dreamcast-devtools checkout with its node_modules>
    ninja -C <build dir>
    ctest --test-dir <build dir> --output-on-failure

`-DNULLDC_DEVTOOLS_DIR=` is for the tests that compare the debugger with the devtools UI (`debugger.net.interop_ws`,
`debugger.disasm.dsp_playground`, `debugger.regs.panels`; "The debugger's suites" below) and for those that run its
contract runner against the emulator (`debugger.contract.m1`, `.m1.interp`, `.m1.fallback`, the five
`debugger.contract.m2arm7*`, `.hash.idlebp`, `.hash.idlebp.interp`, which also need `npm`). Its default is `devtools` in the repository or
`../dreamcast-devtools` next to it, whichever is there; a checkout that sits elsewhere, as it does for a git worktree
of this repository, has to be named, or those tests are skipped.

The build includes two builds of the emulator for the system tests, about a minute each the first time
(`-DNULLDC_SYSTEM_TESTS=OFF` leaves them and the system tests out, `-DNULLDC_EMULATOR=` and
`-DNULLDC_EMULATOR_TA_HLE=` test given binaries instead: `system/README.md`).

| test | what it runs | data |
|---|---|---|
| `common.selftest` | the harness on itself, and the properties of `hw/mem/_vmem` the suites lean on (unaligned accesses end in the handler, constant address lookups return the handlers that are mapped and their context) | none |
| `sh4.singlestep.interp` | every single-step vector through the SH4 interpreter, gated on `sh4_singlestep/baseline.txt` | `vendor/sh4-tests` |
| `sh4.singlestep.dynarec` | every single-step vector through the SH4 dynarec (decoder, shil, x64 backend), gated on `sh4_dynarec/baseline_singlestep.txt` | `vendor/sh4-tests` |
| `sh4.differential` | interpreter against dynarec from the initial state of every vector, gated on `sh4_dynarec/baseline_differential.txt` | `vendor/sh4-tests` |
| `sh4.directed.interp` `sh4.directed.dynarec` `sh4.directed.differential` | the three above on the hand-written vectors of `sh4_directed` (carry, borrow, overflow, compares at the edges, delayed branches with a delay slot that changes what the branch uses, RTE with a delay slot that reads what it restores, loads of FPSCR, TRAPA, FTRC at the edges of its range and of NaNs, FMAC on operands that show how often it rounds) | none |
| `sh4.directed.dynarec.canonical` | `sh4.directed.dynarec` on a dynarec that calls the canonical helpers of the shil opcodes (`CANONICAL_TEST`, `sh4_dynarec/README.md`), against the same baseline | none |
| `sh4.core` | the SH4 on the emulator's own CPU object (`SuperH4_impl`): the registers a reset leaves and the host FP mode that goes with them, the host rounding mode of every FPSCR.RM, the host FP mode of a thread that runs the CPU, the exception registers of TRAPA | none |
| `debugger.sh4bp.*` | SH4 code breakpoints and steps in the active backend: `libswirl/debugger/dbg_sh4.cpp`, the decoder, the x64 backend with its main loop and block manager, the interpreter and the interpreter fallback, on a machine of the suite's own with a stand-in for run control. Where a breakpoint stops and with what registers, that stops, pauses and steps leave the time of a run alone, steps, step over and step out, addresses armed while the guest runs and in every instruction of a block, what the mechanism costs a guest that uses none of it, what the recompiler of a new emulator thread starts with (the code region of the process, prepared once, and an empty code cache), and the selftest of the emulator (`Debugger:SelfTest=sh4bp`) on that machine (`debugger/sh4bp/README.md` has the gates). x86-64 hosts only | none |
| `arm7.vendor` | the 45 self-checking programs of arm7di-tests-dreamcast on the ARM7 interpreter: no error bit, the pinned final registers, and a core that executes nothing must fail | `vendor/arm7di-tests-dreamcast` |
| `arm7.directed` | hand-written cases: ADC, SBC and RSC at the edges with the carry clear and set, the logical operations with a ROR by a register, MUL and MLA with Rd == Rm and C behind their S forms, SWI and the undefined instruction trap with their return, the FIQ handshake of the AICA, also with an ARMRST in the middle of it, and what SCILV0-2, the register behind them and M read back, what a reset of the ARM writes and what it leaves, what a sound CPU holds before its first reset and what a reset of the machine leaves of the interrupt controller | none |
| `arm7.wren7.vendor` `arm7.wren7.qemu_diff` `arm7.wren7.mulsem` `arm7.wren7.bringup` | console jobs of wren7-rtl on the ARM7 interpreter, against what a Dreamcast answered | a wren7-rtl checkout with its ARM programs built |
| `arm7.vendor.halves` `arm7.directed.halves` `arm7.wren7.vendor.halves` `arm7.wren7.qemu_diff.halves` `arm7.wren7.mulsem.halves` `arm7.wren7.bringup.halves` | the six suites above a second time, with every step of the ARM7 made of `ARM7Backend::StepIrq` and `ARM7Backend::StepExec` (`ARM7M_STEP=halves`): the two halves of a step that the debug loop of the sound burst runs in place of `ARM7Backend::Step`, which is not made of them. Same conditions and the same baseline as their twins (`arm7/README.md`) | as its twin |
| `cfg.overrides` | the config store and the command line parser on a config file of their own: a value `-config` gave is in effect for the run and is not what a save of the settings writes to `emu.cfg`, a changed one is written, and a read-only store (the automated runs) writes nothing | none |
| `runctl.guest_exit.*` | `utils/run_control.cpp` on the config store, with stand-ins for the machine and the user interface, one process per run (`window`: no key, `key`: `Exit.OnGuestExit`, `vblanks`: `Exit.AfterVblanks`, `seconds`: `Exit.AfterSeconds`, `headless`): a guest program that returns to the BIOS ends every run but the first, with `reason=guest-exit` on the exit line and the user interface stopped, and leaves the first to its window | none |
| `system.*` | the emulator binary of the default build, headless: `-help`, the keys that end a run and the line it prints, the pinned RTC with its keys in section `config` and in the section of a game, `emu.cfg` left alone, what a fatal fault and a signal that is sent do and that the faults of the guest are still handled, programs that boot under reios on the JIT and on the interpreter with their serial output, the software renderer into a PNG, a run that is repeated and has to end in the same state hash every time, and the speed script of `tests/perf` on one of them (`system/README.md` has the list, and what a deterministic run takes) | none, or the programs of `NULLDC_ELF_DIR` |
| `system.hle.*` | the same on the emulator built with `-DHAS_TA_LLE=OFF` | none, or the programs of `NULLDC_ELF_DIR` |
| `debugger.selftest.emu` `.interp` | the selftest of the debugger in the emulator (`devcast -config Debugger:SelfTest=emu`, headless, on the emulator the system tests build; the SH4 on the JIT and, as `.interp`, on the interpreter): the emulator thread and run control, nineteen tests on a guest of the suite's own. Pauses, event breakpoints, calls of every class in every state, host stops against a parked thread, resets, guest exits, steps, the lifetime of the machine (`debugger/selftest/README.md` has every test of these rows) | none |
| `debugger.selftest.core` `.interp` | the session `core`: the core of the debugger (`libswirl/debugger/dbg_core.cpp`, `dbg_events.cpp`, `dbg_symbols.cpp`, `dbg_callstack.cpp`) against the live machine, in a session without a server. Six tests: `core.events` (the event log: 200 entries, ids that ascend and stay, the oldest dropped, a message cut to its bound, nothing logged and no argument evaluated with the gates closed, the direct append), `core.opcodes` (for all 65536 encodings the disassembler reports an instruction exactly where the opcode table executes one, but for the reios trap `0x085B`, whose line is `.word 0x085b ! reios trap`), `core.tables` (breakpoint and watch ids from 1 and never used twice, the bounds of 1024 breakpoints, 64 expressions and 256 characters, the order of the checks of `breakpoints.add`, watch resolution without regard to case, duplicates, all-or-nothing, the category of a breakpoint, the active mask for mute, solo and no client, the event gates), `core.tick` (the JSON of ticks and of describe: no `null`, the required keys, every register list that of describe, values in their wire form, a full tick and the four base paths, watches on paths a tick does not carry, a symbol name and an event message with bytes that are no ASCII as `?`, the envelope errors, memory that does not wrap), `core.guest` (on the booted program: a step on the reios trap runs the boot, the entry resolves to a symbol, the SH4 call stack, the ARM7 call stack against `dc.aica.arm7.ENABLED` held in reset and running, `breakpointId` in the ticks of a stop until its breakpoint is removed, an event and a code breakpoint under mute), `core.lifetime` (with a thread that plays the host and sends what a client sends: the machine stopped, its guest exited, the machine gone and made again: reason, a new `stopId` for each, `registers: {}` and a tree of `"n/a"` without a machine, bookkeeping in every state, -32001 and -32002) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.regs` `.interp` | the session `regs`: the registers of the device tree, guest memory for the debugger and the device accessors against the live machine, that reading changes nothing, and `dc.aica.arm7.ENABLED` (0 while the ARM7 is held in reset, 1 while it is clocked) where the booted program holds and releases the ARM7 | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.sh4bp` `.sh4bpboot` `.sh4bpbase`, each with `.interp`; `debugger.selftest.sh4bp.sessions` | the gates of the SH4 breakpoints and steps on the park loop of the emulator, on the booted guest and with its baselines; and that sessions on the JIT, on the interpreter and on the JIT with the interpreter fallback forced agree on registers, step traces and time (`debugger/sh4bp/README.md`) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.emuexit.running` `.pending` `.parked`, each with `.interp` | a guest that returns to the BIOS while running, with a pause asked for and while parked ends a headless run, `reason=guest-exit` | none |
| `debugger.selftest.arm7` `.interp` `.fallback` `.persample` `.persample.interp` | the session `arm7`, forty-three tests on the sounding guest of the tree (`debugger/guest/dsptest`): the sound burst that the debugger suspends in front of an ARM7 instruction and runs on, the breakpoints and steps of the ARM7, and its register writer. What a burst is made of with the ARM7 clocked and held; the context of the ARM7 as the debug loop leaves it; a breakpoint that stops inside a burst with no time passed behind it, the lowest id, the active mask, mirrors of the wave memory, the FIQ vector, the resume exemption and its edges, a resume and a step of the SH4 whose burst stops again, the SH4 in SLEEP; steps in a suspended burst, across a time step of the AICA, across the end of a burst, from a stop with no burst suspended (the SH4 runs to the next burst with the stop in force), complete at the FIQ vector, of the `msr` that lets a waiting FIQ in, with the ARM7 held (`-32012`, and a burst that finds it held), with a breakpoint on the instruction, with a breakpoint of the SH4 first, and a host stop, a guest exit, a guest reset and the two calls of the grace expiry during the wait of a step, whose leave is a resume for the SH4 (its exemption, the interrupt acceptance it owes); step over and step out; a reset, a host stop and a guest exit on a suspended burst, and each in the very tick in which a check suspends; a breakpoint hit while the host has stopped a machine whose SH4 sleeps; the interrupt acceptance behind a burst that was run on in the park context; events raised there; every accessor read twice; the registers written; the notice; the grace expiry. On the JIT, on the interpreter, on the JIT with the interpreter fallback of the SH4 forced, and on the first two with the sound rendered sample by sample (`config:aica.NoBatch=yes`) | none |
| `debugger.selftest.emurun.reads` `.pending` `.pauses`, each with `.interp` | 2ndmix for 600 vblanks with the debugger at work (reads for the whole run, a pause pending at the end, 150 pauses): the state hash of the same run without a session | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.hostcycles` `.interp` | the host stops the running machine and starts it again, 600 times in one process, on the way of 2ndmix to its 1200th vblank: the machine counts as running from the moment the host starts it until its thread takes the stop callback, every emulator thread starts with an empty code cache, the cache never fills although the threads together compile six times what it holds, the guest does not get slower with the number of threads, and the run ends in the state hash of the same run without a session. On the interpreter, which has no code cache: the cycles, what counts as running, the speed and the hash | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.emuhang.parked` `debugger.selftest.startpaused` | a run that is parked at its start stays parked, whatever `Exit.*` key it has: a session that resumes nothing, and `Debugger:StartPaused=yes` with the server enabled and no client | none |
| `debugger.contract.m1` `.m1.interp` `.m1.fallback` | the gate of milestone M1 of the devtools integration: the emulator, headless, at its `StartPaused` stop with the server enabled, and the contract runner of the devtools UI against it (`npm run contract -- --url ... --profile emulator --elf 2ndmix.elf --milestone M1`): every selected scenario passed, no invariant violated. On the JIT, on the interpreter and on the JIT with the interpreter fallback forced (`debugger/contract/README.md` has every test of these rows) | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2arm7` `.m2arm7.interp` `.m2arm7.fallback` | the part `arm7` of the runner's scenario S16, by itself (`--only S00,S16 --parts S16:arm7`): breakpoints of the ARM7 that stop, are toggled, muted and found through a mirror, steps that publish no running tick, step over and step out, and `-32012` while the ARM7 is held. The scenario as a whole is partial, which is no pass, until the writes, the reset and the tree of milestone M2 are there. On the three engines of the M1 gate | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2arm7.guest` `.m2arm7.guest.interp` | the same against the sounding guest of the tree, where the run has to have found the ARM7 held at its start and a `bl` to step over and out of: the runner fails a run without the notes `arm7: held at the start` and `arm7: bl stepped over and out` (`--require-note`) | a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.grace` | a client leaves watches and a target behind that is stopped at a breakpoint 2ndmix reaches in every frame: inside the grace period another client finds that stop; 11 s after the last client left the target has run on through the breakpoint (the active mask is cleared), and a client that connects and asks for nothing is sent the next stop of it (breakpoints are active again); the tables are as they were, the event log has the note, and the event gates are open again; that client resumes the target and leaves, and inside the grace period that begins there the breakpoint stops the target within three frames (the connect ended the expired state) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.config` | `Debugger:Required=yes` or `Debugger:StartPaused=yes` without an enabled server is a configuration error: exit code 2, a line on stderr that names the key, no port line and no exit line. And with an enabled server whose port is taken: the same exit code 2 with either key, one line on stderr and a run without the server with neither | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.slow` | the slow-client rule: a client that has stopped reading and is 10 MiB of ticks behind is queued none of the periodic ticks of 3 s, every tick that a request caused, and a full tick next | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.hash.off` `.hash.noclient`, each with `.interp` | 2ndmix for 600 vblanks, twice: the same state hash with no `Debugger:` key and with `Debugger:Enabled=no` (and no server without the key: the default of an automated run); the same with the server on and no client and with the server off | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.hash.idlebp` `.interp` | the same state hash with a client connected and a muted breakpoint on a function 2ndmix calls in every frame (the runner's `--idle-breakpoint`), and with the server off | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.guest.dsptest.files` | the sounding guest of the tree (`debugger/guest/dsptest`: a bare-metal SH4 program with an ARM7 program inside it): its six committed files are those of its manifest `SHA256SUMS`, and what `gen.sh` makes of the two sources, byte for byte (`gen.sh --check`) | none; an sh-elf binutils, and an arm-eabi binutils or clang, for the second half |
| `debugger.sound.lines.dsptest.jit.batch` `.jit.persample` `.interp.batch` `.interp.persample` | the sound line of the guest (`-config config:Debug.AicaHash=2000`, headless, `docs/nulldc3/automated-runs.md`) on the JIT and on the interpreter, rendered in batches and sample by sample (`config:aica.NoBatch`): two runs print one line, it has `ticks=2000`, and its `samples` is none of the 2001 hashes of silence (`debugger/guest/dsptest/README.md` has the script, `soundlines.py`, and every test of these rows) | none |
| `debugger.sound.engines.dsptest.batch` `.persample` | the line of the JIT and the line of the interpreter agree in `samples`, `regs`, `arm7`, `ticks` and `cycles`: everything the guest's SH4 does to the sound falls between the same two ticks of the AICA on both | none |
| `debugger.sound.release.dsptest.jit.batch` `.jit.persample` `.interp.batch` `.interp.persample` | a release of the guest's ARM7 starts the same program whatever request of the AICA is latched for it: the line of a copy of the guest whose SH4 leaves a request latched in front of the release is the line of the guest in `samples`, `regs`, `arm7`, `ticks` and `cycles`, and copies whose ARM7 reset routine lacks the read of L or the write of M print another `arm7`, which says that the request was there. The copies are made by the test, in its work directory | none |
| `debugger.sound.neutral.idlebp.*` `.hitresume.*` `.steps.*`, each `.jit.batch` `.jit.persample` `.interp.batch` `.interp.persample` | the debugger at work on the ARM7 of the guest changes nothing of the run: with a selftest session whose test never returns (`arm7n.idlebp`: an active breakpoint that is never reached, so that the debug loop runs every ARM phase; `arm7n.hitresume`: 200 stops at breakpoints, each resumed; `arm7n.steps`: some twenty thousand single steps, from stops with no burst suspended, across time steps, across the ends of bursts, and of the `msr` that lets a waiting FIQ in) the sound line at 2000 ticks is the line of the run without it, whole, and the state hash at 90 vblanks is its hash, part for part. Each test says what it did in lines the target requires (`debugger/guest/neutral/CMakeLists.txt`) | none |
| `debugger.sound.runner` | the runner of the sound lines on stand-ins for the emulator (`debugger/guest/dsptest/soundlines_test.py`; no emulator is run): a run that was not made on the SH4 engine it was asked for fails, in every mode, with or without a session | none |
| `debugger.sound.lines.2ndmix.jit.batch` `.jit.persample` `.interp.batch` `.interp.persample` | the sound line of 2ndmix at 40000 ticks, the program the harness was accepted with: two runs print one line (it plays nothing under reios, so no more is asked) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `aica.dsp.api` | the AICA DSP core (`libswirl/hw/aica/dsp_core`) on itself: a sample in one go against its 128 steps from every step, state out and in, two cores in turns, its registers, fields that hold more than their registers' bits, a wave RAM of 4, 8 and 16 MB; and its known-answer programs: the shifter, the address of a memory step on 2 to 16 MB, the ring, the read latch rules, the MIXS banks | none |
| `aica.dsp.diff.random` `aica.dsp.diff.sparse` `aica.dsp.diff.directed` | the AICA DSP core against the DSP of the caique sample model on generated programs, compared behind every step | a caique-rtl checkout |
| `aica.dsp.case.dsp_basic` `.dsp_mem` `.dsp_mem2` `.dsp_mem3` `.dsp_wslot` `.dsp_unpack` `.dsp_pack` `.dsp_temp` `.dsp_coll` `.sh4_latch` `.mdec_hold` | the DSP programs caique ran on a Dreamcast, on the model with the core in step and on the core alone, against the console's files | a caique-rtl checkout |
| `aica.dsp.capi` | the C interface of the AICA DSP core, compiled for the host, against the core called itself, on random programs and on the known-answer programs | none |
| `aica.dsp.wasm` | the WebAssembly build of the AICA DSP core in node, through its loader and its C interface, against the core compiled for the host, on the same programs | emcc and node, skipped without them |
| `aica.dsp.wasm.emsdk` `aica.dsp.wasm.noemcc` | the script that builds it, run by sh on a PATH without emcc: it builds with the emsdk in `EMSDK`, and says that it has no emcc when there is no emsdk | an emcc that is in an emsdk, and node; skipped otherwise |

The SH4 dynarec tests run on x86-64 hosts only. The wren7 tests need `-DNULLDC_WREN7_DIR=<path to wren7-rtl>`
unless wren7-rtl sits next to the repository; `arm7/README.md` has what has to be built in it. The system tests
and the `debugger.selftest.*`, `debugger.contract.*` and `debugger.sound.*` tests run on Linux hosts with python3;
the latter three are skipped where there is no emulator to run (`-DNULLDC_SYSTEM_TESTS=OFF` without
`-DNULLDC_EMULATOR=`), but for `debugger.sound.runner`, which runs none.
The `aica.dsp.diff.*` and `aica.dsp.case.*` tests need `-DNULLDC_CAIQUE_DIR=<path to caique-rtl>` unless caique-rtl
sits next to the repository; they compile its sample model and its DSP programs (C) from there and read its console
captures.

The emulator has an SH4 recompiler for x86-64 hosts and one for arm64 hosts, an ARM7 and a DSP recompiler for arm64
hosts, and its interpreters on every host; it does not configure for 32-bit x86 and arm hosts. Of the recompilers the
tests here run the x86-64 backend. `-DTARGET_NO_REC=ON` builds the emulator without a recompiler on any host. A
generic host, one whose processor `reicast/cmake/config.cmake` has no name for, gets that build without the fault
handler, so without nvmem; on an x86-64 machine `-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=<such a name>`
configures it. The system tests take such a binary with `-DNULLDC_EMULATOR=`. On a `TARGET_NO_REC` build every case
passes but `guest_exit.jit`, `boot.2ndmix.jit` and `boot.hello.jit`, which have to see the recompiler, and
`fault.guest`, whose faults are the recompiler's; without the fault handler the `fault.*` cases have nothing to wait
for either. The `debugger.sound.*` tests ask of every run that it was made on the engine it was asked for: with
such a binary their `.jit.` tests and `debugger.sound.engines.*` fail, and their `.interp.` tests pass.

Every test of this table but `common.selftest`, `sh4.core`, the `debugger.sh4bp.*`, `debugger.selftest.*` and
`debugger.contract.*` tests, `cfg.overrides`, `runctl.guest_exit.*`, the system tests, `aica.dsp.capi` and the `aica.dsp.wasm` tests is
gated on a baseline file, and ctest runs them with `--strict`: a count that is worse than its baseline fails, and
so does one that is better, until the baseline is updated and committed. `common.selftest`, `sh4.core`, each
`debugger.sh4bp.*` test, each `debugger.selftest.*` test, each `debugger.contract.*` test, `cfg.overrides`, each
`runctl.guest_exit.*` test, each
system test, `aica.dsp.capi` and `aica.dsp.wasm` are lists of checks that all have to hold;
`aica.dsp.wasm.emsdk` and `aica.dsp.wasm.noemcc` are what the build script returns and prints.

More about the suites:

* this file: the single-step vectors, the filter, the baseline and the directed vectors,
* `sh4_dynarec/README.md`: how the dynarec is run, the differential, the native count;
  `sh4_dynarec/KNOWN_DIFFERENCES.md`: what is behind the two dynarec baselines,
* `arm7/README.md`: the ARM7 tests and the wren7 console suites; `arm7/KNOWN_FAILURES.md`: why checks of the
  ARM7 baseline are below their number of cases, and what else is known to be wrong in the ARM7,
* `system/README.md`: the system tests, which emulator they run and how, their guest programs,
* `debugger/selftest/README.md`: the selftests of the debugger in the emulator, test by test, and how a package
  registers its own,
* `debugger/contract/README.md`: the server of the emulator against the contract runner of the devtools UI, the
  grace period, the configuration errors, the slow-client rule and the state hashes, test by test; where the mock
  of the UI repository and the emulator's server differ,
* `aica_dsp/README.md`: the AICA DSP core's tests, which of the console's captures are compared with the core
  directly and which through the caique model; `aica_dsp/KNOWN_FAILURES.md`: the three lines of a console report
  that depend on the console's timing.

## Layout

    common/            shared: libswirl.cmake (nulldc_add_libswirl_library, NULLDC_SH4_INTERP_SOURCES),
                       test_guard (die()/verify() trap, fatal signals, watchdog), sh4_env + sh4_stubs*
                       (what the SH4 core needs instead of an emulator), ss_* (vector reader, guest memory,
                       policy, state, suite runner), selftest
    sh4_singlestep/    the vectors through the interpreter
    sh4_dynarec/       the vectors through the dynarec, and the interpreter against the dynarec (README.md,
                       KNOWN_DIFFERENCES.md)
    sh4_directed/      hand-written vectors for all of the above
    sh4_core/          the SH4 on SuperH4_impl: what is not one instruction on a vector state (reset, the host
                       FP mode, also across threads, EXPEVT and TRA)
    arm7/              the ARM7 interpreter: vendor programs, directed cases, wren7 console suites (README.md,
                       KNOWN_FAILURES.md)
    cfg/               the config store: -config overrides and what a save writes
    run_control/       the keys that end a run: which runs the end of the guest program ends
    system/            the emulator binary, headless, on guest programs (README.md)
    perf/              not a test: perf.py measures the speed of the emulator on a guest program (README.md),
                       BASELINE.md has the figures of one machine
    aica_dsp/          the AICA DSP core: its interface, the caique model step for step, caique's console
                       captures, the C interface and the WebAssembly build (README.md, KNOWN_FAILURES.md)
    vendor/            the submodules
    files/             fixtures
    debugger/          the debugger's modules that link without the emulator, one CMake project per directory:
                       net/ (transport, embedded UI), disasm/ (disassemblers, README.md), regs/ (device tree
                       tables, and device state that has a test without the machine, README.md);
                       symbols/ (the symbol table of the loaded ELF, README.md); sh4bp/,
                       a suite of this project: SH4 breakpoints and steps in the SH4 backends (README.md);
                       selftest/, the selftests of the debugger run in the emulator the system tests
                       build (README.md); contract/, the server of that emulator against the contract
                       runner of the devtools UI (README.md); and guest/, the guest programs of the tree
                       that the debugger's tests boot, one directory each with the tests that run that
                       emulator on it

## The single-step vectors

Each vector is a CPU state, 4 instructions (`nop`, the instruction under test, `add r1,r1`, `nop`; `add r2,r2`
anywhere else) and the state and bus activity that are expected. The suite loads the state, runs the instructions
and checks

* the bus: every fetch, read and write, in order, with its address, width and written value, and that every
  expected access was made. There is no guest memory besides these expectations (`common/ss_memory.h`).
* the state: every register, bit for bit ("exact"). FP registers are also compared the way the vectors' README
  proposes ("lenient", `ss_float_lenient` in `common/ss_state.h`), both results are counted.
* that the emulator code raised no notification on the way (`msgboxf`, an error box in the emulator, after which
  the code carries on): a vector with one is counted as "notified", whatever its bus and state are.

The vectors come from an emulator (Reicast's interpreter on an ARM64 host), not from a console, so
`common/ss_policy.cpp` has, per file, what the files do not record (the width of the data accesses, the class
of the instruction) and what is known not to pass and why. Vectors that cannot be run or that expect something
an SH4 does not do are filtered, each with a reason:

| reason | vectors |
|---|---|
| `unaligned` | 16/32/64 bit data access at an address that is not a multiple of its width |
| `odd-pc` | instruction fetch from an odd address |
| `fpu-disabled` | FPU instruction with SR.FD=1 (executed by the vectors, FPU disable exception on an SH4) |
| `privileged` | privileged instruction with SR.MD=0 (executed by the vectors, illegal instruction on an SH4) |
| `trapa-nop` | TRAPA that is recorded as a nop: SR stays and the instruction behind it is run. All 500 of the single-step vectors |
| `host-nan` | the instruction produces a NaN, or converts one with FTRC: the expected value is the host's |
| `fpscr-bits` | the vector expects unimplemented FPSCR bits stored: LDS / LDS.L to FPSCR of a value with bits 22-31 set. An SH4 has the low 22 bits, the others read 0 |
| `rte-bank` | RTE to the other register bank: the vector runs the delay slot on the bank of the old SR, an SH4 on the bank of the SR that RTE restores |
| `ftrc-range` | single precision FTRC of 2^31 or more, +inf too: the vector has 7FFFFF80 (its generator cuts the operand to 2147483520.0f first), an SH4 gives 7FFFFFFF |

Nothing else is skipped. The report has one line per file: total / run / pass-exact / pass-lenient / failed /
filtered by reason, and a line for its notified vectors if it has any, then the totals and, for every file with
vectors that are not an exact pass, what its policy says about them.

The FPU results in the vectors are host arithmetic. They are an oracle for the `fast` timing tier only, not for
an exact FPU.

Things the vectors do that devcast currently does as well, and that pass for that reason: SR.RB is forced to 0
when SR.MD=0, SLEEP is executed again until an interrupt arrives.

### The baseline

`sh4_singlestep/baseline.txt` has, per file, how many vectors are run, pass exactly, pass leniently and are
notified (none today). The test fails when

* a file passes fewer vectors than its baseline (exact or lenient), runs fewer, or has more notified ones,
* a vector crashes (die(), verify(), a signal, an unaligned access) in a file for which neither the policy
  (`SSP_MAY_CRASH`) nor the suite (`SsSuite::known`) announces it, or a vector hangs,
* a vector raises a notification that is not announced by its text, by the policy (`ss_policy_notification`) or
  by the suite (`SsSuiteKnown::notifies`). The count in the baseline does not make it pass. The one notification
  that is announced today is "Full MMU support needed" for an FPU instruction with SR.FD=1; those vectors are
  filtered, the differential and `--unfilter fpu-disabled` run them,
* a vector file is missing from the policy table or from the baseline, or the baseline lists a file that is
  not there.

Counts that are better than the baseline (more passes, fewer notified vectors) are reported. Run by hand they do
not fail; ctest runs every test with `--strict`, where they do: a baseline that is behind lets a later regression
back to its counts pass. After a fix:

    <build dir>/sh4_singlestep/sh4_singlestep_interp --update-baseline

rewrites the file; commit it with the fix. A change of the policy (a new filter reason, a width) shifts the
counts as well and needs the same.

### Looking at a failure

    sh4_singlestep_interp --file 0000nnnn11000011 --verbose      # a part of the file name ...
    sh4_singlestep_interp --file fipr --vector 18 --verbose      # ... or of the instruction; * and ? work
    sh4_singlestep_interp --emu-output ...                       # do not hide what the emulator code prints
    sh4_singlestep_interp --unfilter fpu-disabled,privileged     # run filtered vectors as well (or: all)

`--verbose` prints, for every vector that is not an exact pass or that raises a notification nobody announces,
the opcodes, the expected and the actual bus activity and the registers before, expected and actual.

A run with `--vector`, `--unfilter` or `--update-baseline` is not compared with the baseline and does not end in
`PASS`. It ends in `<name>: not gated (...)` with exit code 0, or in `FAIL` when it has what fails whatever the
baseline says: a hang, a crash or a notification nobody announces, the vector of `--vector` not passing.

## The directed vectors

The single-step vectors are random. None of the 500 ADDC vectors has a sum of 0xFFFFFFFF with T set, so an ADDC
that loses the carry of adding T passes all of them. `sh4_directed/directed_gen.cpp` has hand-written vectors for
the instructions that take or leave a carry, a borrow or an overflow in T and for the compares: ADDC, SUBC, NEGC,
ADDV, SUBV, ROTCL, ROTCR, DT, DIV1 and the CMP instructions, on 0, 0xFFFFFFFF, 0x7FFFFFFF and 0x80000000 with T
clear and set (176 vectors).

Every delay slot of the single-step vectors is `add r1,r1`, which leaves T as it is, so a BT/S that tests T behind
its delay slot passes all of them as well. The same file has BT/S and BF/S, taken (forwards and backwards) and not
taken, with `clrt` or `sett` in the delay slot (12 vectors).

`add r1,r1` leaves PR as it is as well, and moves a jump through a register only when that register is r1. A
delayed jump or call takes where it goes, and a call writes PR, in front of its delay slot. The file has (12
vectors)

* RTS with `lds r4,pr` in the delay slot: it returns to the PR it is entered with,
* JMP @r3, BRAF r3 and BSRF r3 with `mov r4,r3` in the delay slot: they go by the r3 they are entered with,
* JSR @r3, BSR and BSRF r3 with `sts pr,r5` in the delay slot: r5 gets the return address of the call,
* JSR @r3, BSR and BSRF r3 with `lds r4,pr` in the delay slot: PR is left with what the delay slot loads.

The delay slot of RTE runs with the SR that RTE restores: on its register bank, with its T. The single-step
vectors that return to the other bank expect the `add r1,r1` of the delay slot on the old one (`rte-bank`, 116 of the
252 that can be run). The file has RTE to the other bank, both ways and to user mode, and to the same bank, with
`mov r0,r8` in the delay slot, and RTE to another T with `movt r8` in it (9 vectors): r8 gets R0 of the new bank and
the new T, which is what a console's delay slot reads.

TRAPA is a nop in the single-step vectors (`trapa-nop`). The file has it (7 vectors) with SR.BL = 0, where it
is the exception a console takes (SSR, SPC and SGR are saved, MD, RB and BL are set and the rest of SR stays, T,
FD and IMASK among it, and the handler at VBR + H'100 is run on bank 1), and with SR.BL = 1, where the manual has a
manual reset of the CPU: SR = 700000F0, VBR = 0, FPSCR = 00040001 and the code at A0000000 is run. No console was
measured for the reset, and the vectors expect every register the manual leaves undefined after it as it was;
`sh4_dynarec/KNOWN_DIFFERENCES.md` has that under what is not measured.

All but one of the single-step vectors of LDS Rm,FPSCR and LDS.L @Rm+,FPSCR that can be run load a value with
bits that FPSCR does not have, and expect them stored (`fpscr-bits`). The file has the loads instead (13 vectors):
FFFFFFFF leaves 003FFFFF, which is what a console reads back, values with only the missing bits leave 0, a
value without them is stored as it is, and the reserved RM = 2 and 3 are stored. A load that changes FR leaves
FR0-FR15 and XF0-XF15 swapped.

FTRC of a NaN is 0 in the single-step vectors and FTRC of a single of 2^31 or more is 7FFFFF80 (`host-nan`,
`ftrc-range`). The file has FTRC in single and in double precision (27 vectors) on numbers in range (it cuts toward
zero), on the last number below 2^31 and on 2^31, the largest number and +inf (7FFFFFFF from 2^31 on), on -2^31,
the next number below it and -inf (80000000) and on NaNs of both kinds and signs (80000000). That is the manual's
FTRC, and what a console answers for +max, the infinities and the NaNs (`sh4.fpu.scalar` has those records).

FMAC rounds FR0 * FRm + FRn once on a console (the FMAC records of `sh4.fpu.scalar`), but none of those records
and none of the 258 single-step vectors that are run has operands for which that shows: a sum that is rounded to
double precision and then to single precision passes them all. The file has FMAC (8 vectors, in both rounding
modes) on operands that are worked out for it: the exact sum is 2^-40 off the middle of two singles, where a
product that is rounded first, or a sum that is rounded to double precision first, lands on the middle and goes
to the other single, and a product whose last bits are all that is left of the sum.

What an instruction leaves is written down from its definition in the SH-4 software manual, nothing of it comes
from an emulator.

The build writes them into `<build dir>/sh4_directed/vectors` in the format and under the file names of the
single-step vectors, and `sh4.directed.interp`, `sh4.directed.dynarec` and `sh4.directed.differential` are the
three SH4 executables on that directory, with the baselines `sh4_directed/baseline_*.txt`. They need no
submodule.

    sh4_singlestep_interp --name sh4.directed.interp --vectors <build dir>/sh4_directed/vectors \
                          --baseline tests/sh4_directed/baseline_interp.txt --verbose

A new case is a row in a table of `directed_gen.cpp`, then a build and `--update-baseline` of the three. A
hand-written vector that is not an exact pass is a wrong row or a defect of the emulator: find out which before
the baseline is committed, and a defect goes into the `known` list of the suite that has it (`sh4_singlestep/main.cpp`,
`sh4_dynarec/singlestep.cpp`, `sh4_dynarec/differential.cpp`) and into `sh4_dynarec/KNOWN_DIFFERENCES.md`.

## The SH4 core checks

`sh4.core` (`sh4_core/core_main.cpp`) has what is not one instruction on a vector state. It runs on `SuperH4_impl`
(`hw/sh4/SuperH4_impl.cpp`, the CPU object of the emulator) with empty devices and module registers around it:

* the reset state, after a power-on and after a manual reset: PC, SR, VBR and FPSCR as the hardware manual's table
  of initial register values has them (A0000000, 700000F0, 0, 00040001), the host FP mode of that FPSCR on the thread
  that did the reset (round to zero, flush to zero, a denormal operand is zero), and 0 in every register the manual
  leaves undefined.
* FPSCR.RM and the host rounding mode, loaded with LDS Rm,FPSCR for RM = 0 to 3 with DN clear and set: 0 rounds
  to nearest and 1 to zero. 2 and 3 are reserved and no console was measured with them: they are expected to round
  as their bit 0 says, like the RTL and the soft-float reference of shrike4-rtl. Both engines compute with the
  host FPU in that mode.
* the host FP mode and threads. The mode (MXCSR, FPCR) belongs to a thread, and the emulator resets and loads
  states on one thread and runs the CPU on another. The test starts the CPU and runs it (`SuperH4_impl::Run`) on a
  thread of its own, on a program of nops and loads of FPSCR, stops it from the first thread as the emulator does,
  and looks at the mode the thread that ran is left in. It has to be the mode of FPSCR: on a thread that is there
  before another one resets, on a thread that is started after the guest loaded FPSCR on the one before it, on a
  thread that is started after a state was loaded, and it has to follow the FPSCR loads that come after.
* TRAPA, for the two registers of it that are not part of a vector state: with SR.BL = 0 EXPEVT = 160 and TRA =
  imm << 2 next to SSR, SPC, SGR, SR and PC, with SR.BL = 1 EXPEVT = 020, TRA as it was, and SR, VBR, FPSCR, PC and
  the host FP mode of a reset.

A check that does not hold is printed with its line, and the test fails.

## Adding a suite

A suite is a directory with a `CMakeLists.txt` that is added in `tests/CMakeLists.txt`. From `common` it gets

* `nulldc_add_libswirl_library(<target> [SHREC|AREC|DSPREC <DYNAREC_*>] SOURCES <files relative to libswirl>)`:
  an object library of libswirl translation units, built like the emulator builds them.
* `nulldc_test_common`: link it after the libswirl library. Stubs are taken from it only for the symbols the
  libswirl objects leave undefined; `msgboxf` and `os_DebugBreak` always come from `test_guard`.
* `NULLDC_HAVE_SH4_TESTS` / `NULLDC_HAVE_ARM7_TESTS` / `NULLDC_HAVE_WREN7` / `NULLDC_HAVE_CAIQUE`, the `*_DIR` and `*_MISSING`
  variables and `nulldc_add_missing_data_test(<name> <reason>)` for data that is not there (skipped, or failed
  with `NULLDC_TESTS_REQUIRE_DATA`); `nulldc_add_skipped_test(<name> <reason>)` for a test the host cannot run.

A suite of the debugger is a directory of `tests/debugger` instead, named in the list of them in
`tests/CMakeLists.txt`: a CMake project that also builds alone, see "The debugger's suites".

A second way to run the SH4 vectors only has to fill an `SsSuite` (`common/ss_suite.h`): how the 4 instructions
of a loaded vector are executed, whether instruction fetches are part of the expected bus order, and what that
way of executing is known to fail, and to notify of, on top of the policy table. It gets its own baseline file.
`sh4_singlestep/main.cpp` is the whole interpreter suite.

## The FPU against the hardware

`sh4_fpu` has two more tests. They need a shrike4-rtl checkout, whose headers they are compiled with and whose
console records they read: `-DNULLDC_SHRIKE4_DIR=<path to shrike4-rtl>`, default `../shrike4-rtl` next to the
repository if it is there. Without it they are skipped, or fail with `-DNULLDC_TESTS_REQUIRE_DATA=ON`.

| test | what it runs | data |
|---|---|---|
| `sh4.fpu.scalar` | FADD FSUB FMUL FDIV FMAC FSQRT FTRC FCMP FABS FNEG FSRRA FCNVDS FCNVSD, single and double precision, through the SH4 interpreter and through the dynarec, against the 225,036 records a Dreamcast answered | a shrike4-rtl checkout |
| `sh4.fpu.vector` | FIPR FTRV FSCA FSRRA through both, against shrike4-rtl's bit-exact model on 1,355,584 generated cases and against 15,642 console records, which the model has to answer as the console did | a shrike4-rtl checkout |
| `sh4.fpu.scalar.canonical` `sh4.fpu.vector.canonical` | the two on a dynarec that calls the canonical helpers of the shil opcodes (`CANONICAL_TEST`), against the same baselines; FSCA is left out of the second | a shrike4-rtl checkout |

Both count, per group of instruction, FPSCR setting and kind of operand, the cases in which each engine leaves
the hardware's values (bit for bit), the hardware's FPSCR, and in which the two engines agree, and are gated
on `sh4_fpu/baseline_scalar.txt` and `sh4_fpu/baseline_vector.txt` like the other tests (`--strict` in ctest,
`sh4_fpu scalar --update-baseline` and `sh4_fpu vector --update-baseline` after a fix). They run on x86-64
hosts only.

* `sh4_fpu/README.md`: the data, how a case is run, the groups and columns, the baselines, looking at a failure,
* `sh4_fpu/KNOWN_FAILURES.md`: why the counts are below their totals, cause by cause.

## The debugger's suites

The modules of `libswirl/debugger` that link without the emulator have their tests under `tests/debugger`, one
CMake project per directory. Each builds alone, with its own options (the head of its `CMakeLists.txt`):

    cmake -S tests/debugger/net -B <build dir> -G Ninja && ninja -C <build dir> && ctest --test-dir <build dir>

and this project adds them all, so that one `ctest` runs everything. They are not compiled with the emulator's
flags: the debugger's sources have to build as standard C++20, without `-fpermissive`, and with warnings on. That
is why `tests/CMakeLists.txt` adds them in front of `common/libswirl.cmake`, and why they come first in the
output of `ctest`. Here their tests are named `debugger.<suite>.<name>`; alone, `<name>`. One directory of them,
`debugger/regs/aica`, compiles two translation units of the emulator and takes its definitions and flags, for
itself only.

| test | what it runs | needs |
|---|---|---|
| `debugger.net.test_ws` `debugger.net.test_http` | the WebSocket handshake and framing of `dbg_net.cpp` against the vectors of RFC 6455; its HTTP side: request parsing, the Host and Origin rules, static files, path sanitising | nothing |
| `debugger.net.test_fuzz` | random and mutated input to both parsers: no crash, no hang, no unbounded buffering, no byte from outside the web root | nothing |
| `debugger.net.test_e2e` | `dbg::net::Server` over loopback, through its public interface, against the test's own client | nothing |
| `debugger.net.test_webui` | the embedded UI: the bundle the build embedded is served file for file | nothing (the bundle is `debugger/net/bundle`) |
| `debugger.net.interop_ws` | the server against the `ws` package, the WebSocket implementation of the devtools tooling | node, a dreamcast-devtools checkout with `node_modules` |
| `debugger.net.embed_off` `embed_auto` `embed_mismatch` `embed_incomplete` `embed_rebuilt` `embed_corrupt` `embed_names` | the UI embed step of the emulator build: which bundle is picked, what stops the configure step or the build, what a rebuilt bundle does, which files are embedded | nothing |
| `debugger.disasm.golden` | `dbg_disasm.cpp`: checked-in encodings of SH-4, ARM7 and the AICA DSP, and the invariants that need no tool | nothing |
| `debugger.disasm.sh4_objdump` `debugger.disasm.sh4_objdump_selftest` | every SH-4 opcode against GNU objdump, and that comparison on dumps with a wrong field | python3, a SuperH objdump |
| `debugger.disasm.sh4_opcode_table` | every SH-4 opcode against `hw/sh4/sh4_opcode_list.cpp` | python3 |
| `debugger.disasm.arm7_objdump` `debugger.disasm.arm7_objdump_selftest` | 249,364 ARM encodings against GNU objdump, and that comparison on dumps with a changed field | python3, an ARM objdump |
| `debugger.disasm.dsp_playground` | 100,193 DSP steps against the disassembler and assembler of the devtools playground | a dreamcast-devtools checkout with `node_modules` |
| `debugger.regs.tables` `debugger.regs.sources` | `dbg_regs_tables.cpp`, the device tree behind `debugger.describe`: the tables by themselves, and their on-chip and system bus rows against `hw/sh4/sh4_mmr.h` and `hw/holly/sb_regs.h` | nothing |
| `debugger.regs.panels` | the panel ids of the tables against the list of the devtools UI | a dreamcast-devtools checkout |
| `debugger.regs.aica.channels` `debugger.regs.aica.rtc` | the sound generator (`hw/aica/sgc_if.cpp`) and the settings of the sound regression harness (`hw/aica/aica_hash.cpp`), compiled as the emulator compiles them: the state of a channel that no register write sets, for the guest, for the debugger and in the samples; the two keys that pin the real-time clock | nothing |
| `debugger.symbols.table` `debugger.symbols.damaged` | `dbg_symbols.cpp`, the symbols of the ELF that reios loaded: the `.symtab` walk on images the test builds byte for byte, lookup by address and by name, `name` and `name+0x<offset>`, a symbol without a size, names with bytes above `0x7F` and control characters; and images that are cut, that are no ELF, or that name offsets outside of themselves, each in memory that ends where the image ends | nothing |

What the tests of the last column look for, and the option that says where it is:

* a dreamcast-devtools checkout with its packages installed (`node_modules`): `-DNULLDC_DEVTOOLS_DIR=<path>`,
  default `devtools` in the repository or `../dreamcast-devtools` next to it, whichever is there. A suite built
  alone has its own option for it (`-DDEVTOOLS_DIR=`, `-DDBG_NET_NODE_MODULES=`);
* the objdumps: `-DSH_OBJDUMP=<sh-elf-objdump>` and `-DARM_OBJDUMP=<arm-eabi-objdump>`; without the option they
  are looked for on the `PATH` and in the KallistiOS toolchain directories under `/opt/toolchains`;
* `node` and `python3` on the `PATH`.

A test that does not find its tool is skipped, and fails with `-DNULLDC_TESTS_REQUIRE_DATA=ON`, like a test
without its data.

`ctest -E '^debugger\.'` leaves them out, `ctest -R '^debugger\.net\.'` runs one suite.

* `debugger/disasm/README.md`: what is compared with objdump, the opcode table and the playground, and every
  difference that is accepted,
* `debugger/regs/README.md`: what the table tests check, and the tests of `regs/aica`, the one directory of these
  suites that takes the emulator's definitions and flags (`reicast/cmake/config.cmake`, for itself only),
* `debugger/symbols/README.md`: the fixture images of the symbol table's tests and what each test breaks in them,
* the head of `debugger/net/CMakeLists.txt`: the sanitizer builds and the bundle of the net suite,
* `debugger/sh4bp/README.md`: the suite of `tests/debugger` that compiles libswirl translation units as the
  emulator does and is added with the suites that do the same (the last paragraph of this section): its gates,
  the machine they run on, and the calls that run control makes into the mechanism.

A new suite is a directory `tests/debugger/<suite>` with a `CMakeLists.txt` that starts with its own
`cmake_minimum_required` and `project`, uses `CMAKE_CURRENT_SOURCE_DIR` / `CMAKE_CURRENT_BINARY_DIR` and names
its tests `${NULLDC_TEST_PREFIX}<name>` (the prefix is empty when the suite is built alone). `tests/CMakeLists.txt`
adds the directories it lists that are there. From this project a suite gets `NULLDC_DEVTOOLS_DIR` and
`NULLDC_TESTS_REQUIRE_DATA`; it has to work without them.

A suite of `tests/debugger` that needs more of the emulator than such a module, and compiles libswirl
translation units as the other suites of this project do, is one of those suites ("Adding a suite"):
`tests/CMakeLists.txt` adds it behind `common/libswirl.cmake`, and it names its tests `debugger.<suite>.<name>`
itself.

`tests/debugger/guest` is no suite: it holds the guest programs of the tree that the debugger's tests boot, one
directory each, with its sources, its committed image and the tests that run the emulator on it.
`tests/CMakeLists.txt` adds the directory behind the system tests, whose options it uses (`NULLDC_SYSTEM_TESTS`,
`NULLDC_EMULATOR`, `NULLDC_ELF_DIR`). Its `CMakeLists.txt` finds python, sets the emulator that is run
(`nulldc_guest_emulator`) and the reason why none can be (`nulldc_guest_skip`), as `debugger/selftest` sets its
two, and adds every subdirectory that has a `CMakeLists.txt`, in the order of the names: a guest is registered by
putting its directory there. A function that one of those directories defines for the ones behind it uses these
two variables, python as it was found there, `nulldc_tests_path` and cache variables, and nothing its own
`CMakeLists.txt` sets: it runs in the scope of its caller. The directories are found again at every build, so a
new one needs no configure by hand. One guest is there: `dsptest`, the sounding guest, with the runner of the sound
lines, the tests `debugger.guest.dsptest.files` and `debugger.sound.*` above, and the function
`nulldc_sound_neutral` for the directories behind it (`debugger/guest/dsptest/README.md`). The directory `neutral`
behind it calls that function: it has the twelve `debugger.sound.neutral.*` tests and no program of its own.
