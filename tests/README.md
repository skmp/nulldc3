# Tests

A standalone CMake project. Every suite but one compiles the libswirl translation units it tests (with the
emulator's own definitions and flags, from `nulldc3/cmake/config.cmake`) and links them against stubs for the
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
configure says that the programs are missing, and seventeen tests are skipped: `system.boot.2ndmix.jit`,
`system.boot.2ndmix.interp`, `system.boot.hello.jit`, `system.boot.hello.interp`, `system.determinism`,
`system.vmem.concurrent`, `system.vmem.none`, `system.vmem.taken` and `system.frame.refsw2`, and the first eight
again as `system.hle.*`. The other system tests run on three programs
of a few instructions that are part of the suite, so a run without the option boots no KallistiOS, renders no
frame and compares no state hash between runs. The selftests of the debugger that boot `2ndmix.elf` are skipped
with them, nineteen tests: `debugger.selftest.core`, `.regs`, `.sh4bp`, `.sh4bpboot`, `.sh4bpbase`,
`.emurun.reads`, `.emurun.pending`, `.emurun.pauses` and `.hostcycles`, each with its `.interp`, and
`debugger.selftest.sh4bp.sessions`. And so are the twenty-six `debugger.contract.*` tests that boot it (the twelve on
the sounding guest of the tree do not: `debugger.contract.m2arm7.guest`, `.m2writes.guest`, `.m2.guest`,
`.m3dsp.guest` and `.m3dsp21.guest`, each with its `.interp`, need the devtools checkout alone, and
`.hash.listening.guest` with its `.interp` needs nothing; nor does `debugger.contract.hash.listening.rule`, which runs
no emulator), and the two `debugger.sound.lines.2ndmix.*` tests. The three `debugger.selftest.arm7*` tests, the three
`debugger.selftest.dsp*` tests, the two `debugger.selftest.sgc*` tests, the six `debugger.selftest.writes*` tests,
the twenty `debugger.sound.neutral.*` tests and the other
`debugger.sound.*` tests of the guest (`.lines.dsptest.*`, `.engines.dsptest`, `.release.dsptest.*`,
`.held.dsptest.*`, `.heard.dsptest.*`, `.muted.dsptest.*`, `.ignored.dsptest`, and `.oracle.dsptest.*`, which need
the caique checkout as well) boot the guest of the tree, and run without the option.

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
`debugger.contract.m2arm7*`, the five `debugger.contract.m2writes*`, the five of the gate of milestone M2
(`debugger.contract.m2`, `.m2.interp`, `.m2.fallback`, `.m2.guest`, `.m2.guest.interp`), the five of the gate of
milestone M3 (`debugger.contract.m3`, `.m3.interp`, `.m3.fallback`, `.m3dsp.guest`, `.m3dsp.guest.interp`) and the two
`debugger.contract.m3dsp21.guest*`, `.hash.idlebp`, `.hash.idlebp.interp`, which also need
`npm`). Its default is the submodule `devtools/` of the repository, else
`../dreamcast-devtools` next to it, the first of the two that has `package.json` and `node_modules` (an
uninitialised submodule is an empty directory and is passed over; `devtools/lookup.cmake`, one rule for the tests
project and for the projects of `debugger/disasm`, `net` and `regs` configured on their own, tested by
`devtools.lookup`); a checkout that sits elsewhere, as it does for a
git worktree of this repository, has to be named, or those tests are skipped. `devtools.commit` checks that the
checkout is at the commit the repository pins for its submodule `devtools/`.

The build includes two builds of the emulator for the system tests, about a minute each the first time
(`-DNULLDC_SYSTEM_TESTS=OFF` leaves them and the system tests out, `-DNULLDC_EMULATOR=` and
`-DNULLDC_EMULATOR_TA_HLE=` test given binaries instead: `system/README.md`).

| test | what it runs | data |
|---|---|---|
| `common.selftest` | the harness on itself, and the properties of `hw/mem/_vmem` the suites lean on (unaligned accesses end in the handler, constant address lookups return the handlers that are mapped and their context) | none |
| `harness.emurun` | `common/emurun.py`, how the harnesses of the tests start the emulator and node on every host (`common/emurun_test.py`): the environment of a run (POSIX: the sanitizers' options alone; Windows: `SystemRoot`, `TEMP` and `TMP` in the run's directory, nothing of the profile) and of a program that is not the emulator (the caller's; on Windows with `TEMP`, `TMP` and npm's cache in the run's directory), `nulldc3.exe` for `nulldc3`, output read without the `\r` of a `\r\n`, every program inside `nulldc3-winrun` on Windows and none without it, `npm.cmd`, no POSIX shell on Windows; that the four harnesses start no program but through it, each with an environment of it, and name neither the host nor the caller's environment to it; the environments `debugger/contract/contract.py` (`runner_env`) and `debugger/guest/dsptest/soundlines.py` (`tool_run`) build for their tools, built as on a Windows host for a caller with a `TEMP`, `TMP` and npm cache of its own: `TEMP`, `TMP` and npm's cache of their run, nothing of the caller's; a program started through it on this host, as the emulator and as a tool: on Windows inside `nulldc3-winrun` on a desktop of its own, the program it left running ended with the job; and `selftest.py --mode log-rule` writing under its `--work` alone | python3 |
| `common.guard.windows` | Windows only: the policy of the tests project for a program that dies (`common/win_policy.cpp`: no box, no crash report, one line and 128 + the signal's number), which every program of the project has (`common.policy.windows`): `common_selftest`, `disasm_test`, `sh4bp_test` and `aica_dsp_guest_oracle` each made to fault eight ways before `main`, through `nulldc3-winrun`, each way taken by one handler of the policy, which its line names (an access violation and a divide by zero: the vectored handler; `abort()`: the SIGABRT handler; a C++ exception and a breakpoint: the filter; an invalid parameter and a pure virtual call: their handlers; a fault on a stack that cannot be searched: the vectored handler, as nothing else can; 139, 134, 136, 134, 133, 134, 134, 139, no window), and no new entry of them in the crash-report places of the profile, listed by name before and after (`common/guard_windows.cmake`) | none |
| `common.policy.windows` `.catches` | Windows only: the policy for a program that dies is in every program the tests project builds: the mark of `common/win_policy.cpp` read in the file of every executable target, found by a walk of the project's directories once all of them are made (`common/policy_windows.cmake`), but `nulldc3-winrun`, the runner the others run inside; and a program built without it, `common_policy_absent`, is told from the others (`.catches`: the check on `common_selftest` and that program fails and names it alone) | none |
| `sh4.singlestep.interp` | every single-step vector through the SH4 interpreter, gated on `sh4_singlestep/baseline.txt` | `vendor/sh4-tests` |
| `sh4.singlestep.dynarec` | every single-step vector through the SH4 dynarec (decoder, shil, x64 backend), gated on `sh4_dynarec/baseline_singlestep.txt` | `vendor/sh4-tests` |
| `sh4.differential` | interpreter against dynarec from the initial state of every vector, gated on `sh4_dynarec/baseline_differential.txt` | `vendor/sh4-tests` |
| `sh4.directed.interp` `sh4.directed.dynarec` `sh4.directed.differential` | the three above on the hand-written vectors of `sh4_directed` (carry, borrow, overflow, compares at the edges, delayed branches with a delay slot that changes what the branch uses, RTE with a delay slot that reads what it restores, loads of FPSCR, TRAPA, FTRC at the edges of its range and of NaNs, FMAC on operands that show how often it rounds) | none |
| `sh4.directed.dynarec.canonical` | `sh4.directed.dynarec` on a dynarec that calls the canonical helpers of the shil opcodes (`CANONICAL_TEST`, `sh4_dynarec/README.md`), against the same baseline | none |
| `sh4.core` | the SH4 on the emulator's own CPU object (`SuperH4_impl`): the registers a reset leaves and the host FP mode that goes with them, the host rounding mode of every FPSCR.RM, the host FP mode of a thread that runs the CPU, the exception registers of TRAPA | none |
| `debugger.sh4bp.*` | SH4 code breakpoints and steps in the active backend: `libswirl/debugger/dbg_sh4.cpp`, the decoder, the x64 backend with its main loop and block manager, the interpreter and the interpreter fallback, on a machine of the suite's own with a stand-in for run control. Where a breakpoint stops and with what registers, that stops, pauses and steps leave the time of a run alone, steps, step over and step out, addresses armed while the guest runs and in every instruction of a block, what the mechanism costs a guest that uses none of it, what the recompiler of a new emulator thread starts with (the code region of the process, prepared once, and an empty code cache), and the selftest of the emulator (`Debugger:SelfTest=sh4bp`) on that machine (`debugger/sh4bp/README.md` has the gates). x86-64 hosts only, Linux and Windows; on Windows also `debugger.sh4bp.unwind` (a stop of a check walked by the system's unwinder through the check stub, the block and the main loop), and every gate checks the walks of its stops | none |
| `arm7.vendor` | the 45 self-checking programs of arm7di-tests-dreamcast on the ARM7 interpreter: no error bit, the pinned final registers, and a core that executes nothing must fail | `vendor/arm7di-tests-dreamcast` |
| `arm7.directed` | hand-written cases: ADC, SBC and RSC at the edges with the carry clear and set, the logical operations with a ROR by a register, MUL and MLA with Rd == Rm and C behind their S forms, SWI and the undefined instruction trap with their return, the FIQ handshake of the AICA, also with an ARMRST in the middle of it, and what SCILV0-2, the register behind them and M read back, what a reset of the ARM writes and what it leaves, what a sound CPU holds before its first reset and what a reset of the machine leaves of the interrupt controller | none |
| `arm7.wren7.vendor` `arm7.wren7.qemu_diff` `arm7.wren7.mulsem` `arm7.wren7.bringup` | console jobs of wren7-rtl on the ARM7 interpreter, against what a Dreamcast answered | a wren7-rtl checkout with its ARM programs built |
| `arm7.vendor.halves` `arm7.directed.halves` `arm7.wren7.vendor.halves` `arm7.wren7.qemu_diff.halves` `arm7.wren7.mulsem.halves` `arm7.wren7.bringup.halves` | the six suites above a second time, with every step of the ARM7 made of `ARM7Backend::StepIrq` and `ARM7Backend::StepExec` (`ARM7M_STEP=halves`): the two halves of a step that the debug loop of the sound burst runs in place of `ARM7Backend::Step`, which is not made of them. Same conditions and the same baseline as their twins (`arm7/README.md`) | as its twin |
| `cfg.overrides` | the config store and the command line parser on a config file of their own: a value `--config` gave is in effect for the run and is not what a save of the settings writes to `emu.cfg`, a changed one is written, and a read-only store (the automated runs) writes nothing | none |
| `cfg.log_file` | `cl_LogToFile` of the command line parser, what the Windows switch `--log` does with stdout and stderr: one file, made empty first, with every line of both streams in the order they were written and none lost when the process ends without a flush (a child process that ends with `_exit`); a log that cannot be opened leaves the streams as they were | none |
| `runctl.guest_exit.*` | `utils/run_control.cpp` on the config store, with stand-ins for the machine and the user interface, one process per run (`window`: no key, `key`: `Exit.OnGuestExit`, `vblanks`: `Exit.AfterVblanks`, `seconds`: `Exit.AfterSeconds`, `headless`): a guest program that returns to the BIOS ends every run but the first, with `reason=guest-exit` on the exit line and the user interface stopped, and leaves the first to its window | none |
| `system.*` | the emulator binary of the default build, headless: `--help`, the keys that end a run and the line it prints, the pinned RTC with its keys in section `config` and in the section of a game, `emu.cfg` left alone, the directory a run keeps its files in (the one of the executable, the one that holds an AppImage, `--portable <dir>`, `--shared`) and every command line that is refused, what a fatal fault and a signal that is sent do and that the faults of the guest are still handled, programs that boot under reios on the JIT and on the interpreter with their serial output, the software renderer into a PNG, a run that is repeated and has to end in the same state hash every time, emulators started together that each get the memory of their layout, a run on the JIT without the layout, and the speed script of `tests/perf` on one of them (`system/README.md` has the list, and what a deterministic run takes) | none, or the programs of `NULLDC_ELF_DIR` |
| `system.hle.*` | the same on the emulator built with `-DHAS_TA_LLE=OFF` | none, or the programs of `NULLDC_ELF_DIR` |
| `devtools.commit` | the UI checkout the tests are given (`NULLDC_DEVTOOLS_DIR`) is at the commit the repository pins for its submodule `devtools/`, when both are git checkouts; it says what it compared | `NULLDC_DEVTOOLS_DIR` |
| `devtools.commit_check` | the script of `devtools.commit` on repositories made for the test: a UI at the commit the tree pins passes and says so; one at another commit fails and names both; a tree without the gitlink fails; a UI directory that is no git checkout or not the top of one, and a tree that is no git checkout, are not compared and pass | git |
| `devtools.lookup` | where the tests look for the UI when they are given none (`devtools/lookup.cmake`), on scratch layouts of a tree and a checkout beside it: an uninitialised submodule (an empty directory), one without `node_modules`, one with `node_modules` as a file and one with half of what is needed are passed over for the checkout beside the tree; a complete submodule is taken first; with neither the look-up is empty | none |
| `rebrand.names` | the tree says the name of the emulator it was forked from only as history: every line of a tracked text file that has the name, in any case (`git grep -I -i -n`), and every tracked file whose name has it, is allowed by an entry of `rebrand/allowed.txt`, a whole path (the licence files, `docs/nulldc3/**`, the test's own files) or a row of path and line pattern, and every row still finds its line; an occurrence no entry allows and a row whose line is gone fail and are named (`rebrand/README.md` has the format and how a package changes a row). Skipped when the tree is not a git checkout | git, the tree as a git checkout |
| `icons.generated` | `nulldc3/icon/make_icons.py --check`: the eleven approved PNGs of `nulldc3/icon/png/` are inputs it accepts (the PNG signature, every chunk's CRC, only IHDR, IDAT and IEND, the size the name says, 8-bit RGBA, not interlaced, image data that inflates to exactly the image's rows, the five row filters), the three files it makes from them (`nulldc3/windows/nulldc3.ico`, `nulldc3/apple/nulldc3.icns`, `libswirl/linux-dist/x11_icon.h`) are byte for byte the committed ones, and the committed `.ico` and `.icns` are well formed (count, sizes, offsets, lengths, a PNG at every offset); it writes nothing, and names each file that differs or is broken (`nulldc3/icon/README.md` has how the files are made). Skipped without python3 | python3 |
| `icons.x11` | `x11_icon_cardinals()` (`libswirl/linux-dist/x11_icon.cpp`, no X11 header), what the X11 window gets as its `_NET_WM_ICON`, without a display: 24,652 values, the header's `uint32_t` values zero-extended to `unsigned long` (each below 2^32, as Xlib's format 32 needs), the blocks of 16, 24, 32, 48, 64 and 128 px with their width and height, the 64-bit FNV-1a of the values equal to the header's constant, the corners' alpha 0 from 24 px up and at most 12 at 16 px (the approved small art is antialiased into its corners), the centre opaque | none |
| `version.derive` | `nulldc3/cmake/version.cmake`, the rule that makes the emulator's version at every build, on git repositories the test makes below its build directory (`version/derive.cmake`; every tag is made there, none in a repository of the tree): on the tag `v3.0.0-pre` (`3.0.0-pre`), two commits after it (`3.0.0-pre+2.g<h>`), dirty by a changed, a staged and a deleted tracked file (`+dirty`, `.dirty`) and not by untracked and ignored files, no tag reachable (`3.0.0-pre+g<h>`), the tags `foo`, `release-1` and `vnext` passed over also when they are nearer than the version tag, a tag whose describe output does not read as `v<T>-<N>-g<hash>` taken as none, a later tag `v3.0.1` over the base `3.0.0-pre` (`3.0.1`, numbers `3,0,1,0`), a worktree (its own HEAD), the repository as a submodule and its superproject (a submodule at another commit or with a changed file makes it dirty, one with an untracked file does not), a shallow clone through a `file://` URL (`+g<h>` with the full clone's `<h>`), a copy without `.git` and one inside another repository (`+nogit`), no git on the `PATH` (`+nogit`), `GIT_DIR` of another repository in the environment; in every case the full hash, the dirty flag, the three files (the header's defines, no `#include` or `#pragma` in it) and `<h>` as the first 10 hexadecimal digits of `git rev-parse HEAD`; a second run with nothing new rewrites none of the files; a base that is missing or no version stops it; through the script's own functions the derived forms for N = 0, 5, 65535, 65536 and 70000 (capped at 65535) and twelve describe outputs of other forms | git |
| `version.build_time` | the version is made when the emulator is built, not when it is configured (`version/build_time.cmake`): a clone of the tree as it is now (`git clone --shared` at its `HEAD`, the tracked files the tree has changed since copied over and committed), configured once with `-DNULLDC_DEVTOOLS_DIST=OFF`, its target `nulldc3_version` built, an empty commit, the target built again; after each build `nulldc3-version.txt` is what `version.cmake` makes of the clone then and the header's `NULLDC_GIT_HASH` its `git rev-parse HEAD` (after the commit the new `<h>`), CMake did not run again (`CMAKE_PROJECT_INCLUDE` counts the configures) and `git status --porcelain --ignored` of the clone is empty; nothing is compiled. A version made at configure time, or made again by a configure that depends on `.git`, fails, whatever emulator the tests ask | git; the tree a git checkout |
| `version.cl` | `--version` of `cfg/cl.cpp`, without the emulator (`version/cl_test.cpp`, on `nulldc_cfg`): `ParseCommandLine` returns `rv_cli_finish`, stdout is exactly `nullDC <version>` and a line end, `<version>` being `nulldc_version()`, and `cl_FinishText()` is that same text (what the Windows build shows where stdout cannot be read once the process has ended); for `--help` stdout and `cl_FinishText()` are the usage text; a refused command line leaves `cl_FinishText()` empty | none |
| `version.binary` | the emulator under test (`NULLDC_EMULATOR`, else the one the system tests build) exits with 0 for `--version`, and its stdout is the one line `nullDC <version>`, `<version>` what `version.cmake` makes of the tree when the test runs, with nothing in front of it on any system (the Linux build answers `--version` before `common_linux_setup()` prints its lines; on macOS the setup's `Linux paging:` goes to stderr): a binary of another commit fails (one not built again since a commit) (`version/binary.cmake`). The system tests' emulator is configured again at every build of the tests (`system/build_emulator.cmake`), so with it this test cannot tell a version made at configure time; `version.build_time` can. With `NULLDC_EMULATOR` an emulator built again after a commit without a configure, it sees that too. Skipped when there is no emulator to ask | the emulator |
| `debugger.selftest.emu` `.interp` | the selftest of the debugger in the emulator (`nulldc3 --config Debugger:SelfTest=emu`, headless, on the emulator the system tests build; the SH4 on the JIT and, as `.interp`, on the interpreter): the emulator thread and run control, nineteen tests on a guest of the suite's own. Pauses, event breakpoints, calls of every class in every state, host stops against a parked thread, resets, guest exits, steps, the lifetime of the machine (`debugger/selftest/README.md` has every test of these rows) | none |
| `debugger.selftest.nap` | the naps of the selftests of the debugger (`libswirl/debugger/dbg_selftest_nap.h`, `Debugger:SelfTest=nap`, a machine test on the JIT): naps of 50 ms last never less and at most 25 ms more and take at most 5 ms of the thread's CPU time; naps of 100 us to 1.5 ms are never shorter, their median at most 400 us longer, and those of a millisecond or more are not spun whole. On a POSIX host a nap is `std::this_thread::sleep_for`, and the medians of 40 naps of each request through `NapUs` and as many through `std::this_thread::sleep_for`, one after the other, are within 50 us of each other; on Windows, whose `sleep_for` lasts a tick of the system timer (15.6 ms), a high-resolution waitable timer and a spin of its last 500 us, and the test holds the way without that timer too | none |
| `debugger.selftest.core` `.interp` | the session `core`: the core of the debugger (`libswirl/debugger/dbg_core.cpp`, `dbg_events.cpp`, `dbg_symbols.cpp`, `dbg_callstack.cpp`) against the live machine, in a session without a server. Eight tests: `core.events` (the event log: 200 entries, ids that ascend and stay, the oldest dropped, a message cut to its bound, nothing logged and no argument evaluated with the gates closed, the direct append), `core.opcodes` (for all 65536 encodings the disassembler reports an instruction exactly where the opcode table executes one, but for the reios trap `0x085B`, whose line is `.word 0x085b ! reios trap`), `core.tables` (breakpoint and watch ids from 1 and never used twice, the bounds of 1024 breakpoints, 64 expressions and 256 characters, and of 256 watches, which a watch on every register of the tree reaches: of its 46 calls each that fits is taken whole and each that would pass the bound is refused whole; the order of the checks of `breakpoints.add`, the breakpoints of each processor handed to that processor alone, watch resolution without regard to case, duplicates, all-or-nothing, the category of a breakpoint, the active mask for mute, solo and no client, the event gates), `core.tick` (the JSON of ticks and of describe: no `null`, the required keys, every register list that of describe, values in their wire form, a full tick and the four base paths, watches on paths a tick does not carry, a symbol name and an event message with bytes that are no ASCII as `?`, the envelope errors, memory that does not wrap), `core.guest` (on the booted program: a step on the reios trap runs the boot, the entry resolves to a symbol, the SH4 call stack, the ARM7 call stack against `dc.aica.arm7.ENABLED` held in reset and running, `breakpointId` in the ticks of a stop until its breakpoint is removed, an event and a code breakpoint under mute), `core.lifetime` (with a thread that plays the host and sends what a client sends: the machine stopped, its guest exited, the machine gone and made again: reason, a new `stopId` for each, `registers: {}` and a tree of `"n/a"` without a machine, bookkeeping in every state, -32001 and -32002), `core.reset` (`control.reset` at the stop of a breakpoint: one more stop, reason `reset`, no `Running` and no breakpoint named, the PC of the SH4 the one a reset leaves, the breakpoints, watches and categories kept, no tick of its own; and while the machine runs: `{}`, nothing published, and the machine boots again into a breakpoint on its entry), `core.writes` (`state.writeMemory`, `state.setRegister` and `state.editWatch` by parameter, by the order of their own errors, in-band, and by lifetime, -32001 and -32002 behind -32602 and in front of every error of a method's own; a path, a name or a watch expression with a NUL in it is none of the tree; the text of a value, of which the low bits of any length count; five capabilities besides the engine's; `flags.writable` in describe on the 109 registers that have a writer and on none of a tick; one tick behind every accepted write, of the stop it was made in; writes served while the machine runs, with a running tick behind them, never -32003) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.regs` `.interp` | the session `regs`, fifteen tests: the registers of the device tree, guest memory for the debugger and the device accessors against the live machine, that reading changes nothing, and `dc.aica.arm7.ENABLED` (0 while the ARM7 is held in reset, 1 while it is clocked) where the booted program holds and releases the ARM7. The rows of the devices (milestone M2) at states the tests make up, a value of its own in every word a row reads: `regs.rows.sb` (the system bus rows against a guest read and against the value each slot was given; a write-only register reads `"n/a"` whatever its slot holds; `SB_MST`, which nothing assigns, reads 0), `regs.rows.pvr` (the PVR's register file against a guest read), `regs.rows.aica` (the fields of the common block against the emulator's own struct; the 2479 fields of the register file as the accessor delivers them, as the state was made, and against guest reads of the channel words; `AR`, `VREG`, `MIEMP`, `MOEMP` and the loop flag as a read delivers them; `SCILV0-2`, `SCIRE` and `MCIRE` `"n/a"` while the file holds a value; `L` and `M` what `SoundCPU::DebugGetInterrupt` says, in four states of the controller), `regs.rows.gdrom` (the rows of `dc.gdrom` against the drive's snapshot in 24 made-up states, whose state and disc type are enumerators of their enums, and the clock), `regs.rows.pure` (every register read a hundred times: the save state of every device and the PVR's register file as they were, also with a channel's loop flag set, which a guest read of the status clears), `regs.tick` (a full tick with no breakpoint and no watch listed is at most 192 KiB; the time the emulator thread takes to build one is printed) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.sh4bp` `.sh4bpboot` `.sh4bpbase`, each with `.interp`; `debugger.selftest.sh4bp.sessions` | the gates of the SH4 breakpoints and steps on the park loop of the emulator, on the booted guest and with its baselines; and that sessions on the JIT, on the interpreter and on the JIT with the interpreter fallback forced agree on registers, step traces and time (`debugger/sh4bp/sessions.py`, the port of `selftest.sh`; `debugger/sh4bp/README.md`) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.sh4bp.sessions.rule`, `debugger.selftest.log.rule` | no emulator: the comparisons of `sessions.py` on made-up sessions (every key they hold, changed in one session, fails; a key they do not hold does not; the repeats on the booted guest go as their rule says), and the checks of `selftest.py` on made-up output of a session whose lines end in `\n` and in `\r\n` | python3 |
| `debugger.selftest.emuexit.running` `.pending` `.parked`, each with `.interp` | a guest that returns to the BIOS while running, with a pause asked for and while parked ends a headless run, `reason=guest-exit` | none |
| `debugger.selftest.arm7` `.interp` `.fallback` | the session `arm7`, forty-four tests on the sounding guest of the tree (`debugger/guest/dsptest`): the sound burst that the debugger suspends in front of an ARM7 instruction and runs on, the breakpoints and steps of the ARM7, and its register writer. What a burst is made of with the ARM7 clocked and held (32 time steps either way: the AICA is clocked whatever `ARMRST` says; in bursts that find the ARM7 held nothing of it executes, while the DSP and the channels run on, its timer's request is raised and latched, and the SH4's interrupt line follows the AICA); the context of the ARM7 as the debug loop leaves it; a breakpoint that stops inside a burst with no time passed behind it, the lowest id, the active mask, mirrors of the wave memory, the FIQ vector, the resume exemption and its edges, a resume and a step of the SH4 whose burst stops again, the SH4 in SLEEP; steps in a suspended burst, across a time step of the AICA, across the end of a burst, from a stop with no burst suspended (the SH4 runs to the next burst with the stop in force), complete at the FIQ vector, of the `msr` that lets a waiting FIQ in, with the ARM7 held (`-32012`, and a burst that finds it held), with a breakpoint on the instruction, with a breakpoint of the SH4 first, and a host stop, a guest exit, a guest reset and the two calls of the grace expiry during the wait of a step, whose leave is a resume for the SH4 (its exemption, the interrupt acceptance it owes); step over and step out; a reset, a host stop and a guest exit on a suspended burst, and each in the very tick in which a check suspends; a breakpoint hit while the host has stopped a machine whose SH4 sleeps; the interrupt acceptance behind a burst that was run on in the park context; events raised there; every accessor read twice; the registers written; the notice; the grace expiry. On the JIT, on the interpreter, and on the JIT with the interpreter fallback of the SH4 forced | none |
| `debugger.selftest.dsp` `.interp` `.fallback` | the session `dsp`, thirty-one tests on the sounding guest of the tree, whose DSP program the guest loads: the run control of the DSP. Its DSP phase of every sample, also while the ARM7 is held, which the debug loop runs only while something of the DSP is armed (`SoundBurstDspCounts`); a breakpoint of the DSP, which is reached in every sample: it stops in front of its step with no time passed behind it, the lowest id, the active mask, the resume exemption (once; not behind a stop the DSP did not decide, nor with nothing to check; one armed at a leave ended at a change of `dbg_dsp_armed` to 0 and back, not by a burst that finds the ARM7 held), a resume stopping in front of the same step one sample later, with the ARM7 held (nothing of it executes); steps of the DSP, which never publish `Running`: in a suspended DSP phase, of step 127 to the end of its sample (the burst suspended there, `SOUND_BURST_SAMPLE`) and to the end of its burst (the tail once), from a stop with no burst suspended (the SH4 runs to the next burst, the step is step 0 of its first sample), from a stop of the ARM7 (the rest of its ARM phase, whose breakpoint comes first with its own cause), from the end of a sample, with the ARM7 held, with an SH4 breakpoint first, with a breakpoint on the step, and `-32004` for step over and step out; a step of the SH4 dropped by the breakpoint, a step of the ARM7 not; a reset, a host stop and a guest exit on a burst suspended in front of a DSP step and at the end of a sample, and each in the very tick in which a check of the DSP suspends; a host stop and a guest exit in the wait of a step of the DSP, which leave no step armed for the next thread, and a host stop at a breakpoint of the DSP whose owed resume arms nothing for the DSP; every accessor read twice at a stop in a DSP phase; the nine datapath rows of `dc.aica.dsp` through the steps of a sample; the grace expiry. On the JIT, on the interpreter, and on the JIT with the interpreter fallback of the SH4 forced | none |
| `debugger.selftest.sgc` `.interp` | the session `sgc` (`--config Debugger:SelfTest=sgc,mem.binary`), twenty-five tests on the sounding guest of the tree, whose two channels play: the SGC frame capture (`libswirl/debugger/dbg_sgc.cpp`), the outputs the sound generator keeps for it, the binary frames of the core and its deferred response of `state.recordSgcFrames` (`dbg_core.cpp`), and the capture's cancels in run control. Every record of a frame against the accessors of the AICA at its sample, behind the sweep, and its sends against MIXS; the frame count by the AICA's time steps from the arm to the capture's stop (one more where a burst was suspended in front of a DSP step), armed with none suspended, in front of an ARM7 instruction, in front of a DSP step and at the end of a sample; the capture's stop at the end of the sample of its last frame, with no running tick where the park loop runs a suspended burst on, also where the burst ends with it and in the run-on of a step of the SH4, which it drops; a capture while running, with no stop, and with the ARM7 held, which records in held bursts; a second request (`-32003`), the capture before while one records, a reset (`-32014` at once), a host stop and a guest exit (`-32001` at once, and a capture whose last frame comes in the rest of the burst the thread runs to its end), the deadline at 5 s through the notice of the cancel, a completion in front of the deadline's cancel call, a cancel call that is not taken (the core answers itself, a request is `-32003` until the notice came, also where no capture records any more), the refused resume, a buffer that cannot be allocated (`-32603`), a call with no emulator thread, and one the exiting thread's drain takes; a capture's last frame in the sample of a DSP step, a host stop, a guest exit and a reset in the tick of a capture's last frame; the kept outputs (the pair of a channel alone at the end of a sample, 0 while no capture records); and `state.getMemoryBinary` (its bytes, the unreadable runs, its errors with no frame, the response in front of the frame, one counter of binaryIds for both kinds, per connection from 1). Through the core thread without a server (`dbg_core_private.h`, `StartForTest`), with what a client is sent going to the test. On the JIT and on the interpreter | none |
| `debugger.selftest.writes` `.interp` `.fallback` | the session `writes`, fourteen tests on the sounding guest of the tree: the debugger's writes of guest memory (`dbg::mem::Write`) and of the registers of the two processors (`dbg::regs::Write`), with what each owes the machine. Main memory through its mirrors and all of a range or nothing of it; an instruction that is written executes, by a step and under the owed rest of a block; the compiled blocks of a written page are discarded; a breakpoint in a written page still stops; video memory through both views and wave memory through both targets; everything else refused. Every row of `dc.sh4.cpu` and of `dc.aica.arm7` read back as it is stored; a written PC of the SH4 (odd: refused; the owed remainder goes; onto another breakpoint it stops there, back onto the stop's own it resumes through; into another mirror of main memory the loop stays there, because the code cache is emptied); SR (the register banks, and an interrupt it lets in, accepted on the way out of the park context); FPSCR (the FP banks, and compiled code that runs in the precision and the move size that were written); the ARM7 (the CPSR through the mode switch, the PC aligned, a write that moves the PC emptying the ARM7's code cache, and a PC written in a service excursion of a running machine); the rows without a writer; bits above a width (`debugger/selftest/README.md` has every test). On the JIT, on the interpreter and on the JIT with the interpreter fallback of the SH4 forced | none |
| `debugger.selftest.writes.smc` | the one test `smcwrites.cached`, which is the steps of `writes.mem.cached` with `config:Dynarec.SmcCheckLevel=-1`, on the JIT: the configuration in which a main-memory write without the discard of the compiled blocks of its pages would execute stale code. Everywhere else the fault handler, or the check a block of a written page makes of its code, makes such a write good, so the test first holds that it runs on a recompiler with that level, and fails where it does not | none |
| `debugger.selftest.writesrun.neutral` `.interp` | the guest of the tree for 600 vblanks with the debugger writing: 240 stops latched at slice ends, each with a write of main memory, of the page of the guest's code, of R8, SR and FPSCR of the SH4 (the last empties the code cache) and of R0 to R12 of the ARM7, and the restore of each, half of them as the requests of a client: the state hash of the same run without a session | none |
| `debugger.selftest.emurun.reads` `.pending` `.pauses`, each with `.interp` | 2ndmix for 600 vblanks with the debugger at work (reads for the whole run, a pause pending at the end, 150 pauses): the state hash of the same run without a session | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.hostcycles` `.interp` | the host stops the running machine and starts it again, 600 times in one process, on the way of 2ndmix to its 1200th vblank: the machine counts as running from the moment the host starts it until its thread takes the stop callback, every emulator thread starts with an empty code cache, the cache never fills although the threads together compile six times what it holds, the guest does not get slower with the number of threads, and the run ends in the state hash of the same run without a session. On the interpreter, which has no code cache: the cycles, what counts as running, the speed and the hash | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.selftest.emuhang.parked` `debugger.selftest.startpaused` | a run that is parked at its start stays parked, whatever `Exit.*` key it has: a session that resumes nothing, and `Debugger:StartPaused=yes` with the server enabled and no client | none |
| `debugger.contract.m1` `.m1.interp` `.m1.fallback` | the gate of milestone M1 of the devtools integration: the emulator, headless, at its `StartPaused` stop with the server enabled, and the contract runner of the devtools UI against it (`npm run contract -- --url ... --profile emulator --elf 2ndmix.elf --milestone M1`): every selected scenario passed, no invariant violated. On the JIT, on the interpreter and on the JIT with the interpreter fallback forced (`debugger/contract/README.md` has every test of these rows) | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2arm7` `.m2arm7.interp` `.m2arm7.fallback` | the part `arm7` of the runner's scenario S16, by itself (`--only S00,S16 --parts S16:arm7`): breakpoints of the ARM7 that stop, are toggled, muted and found through a mirror, steps that publish no running tick, step over and step out, and `-32012` while the ARM7 is held. The scenario as a whole is in the gate of milestone M2 (`debugger.contract.m2`). On the three engines of the M1 gate | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2arm7.guest` `.m2arm7.guest.interp` | the same against the sounding guest of the tree, where the run has to have found the ARM7 held at its start and a `bl` to step over and out of: the runner fails a run without the notes `arm7: held at the start` and `arm7: bl stepped over and out` (`--require-note`) | a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2writes` `.m2writes.interp` `.m2writes.fallback` | four parts of S16 (`--only S00,S16 --parts S16:arm7+writeMemory+setRegister+reset`): besides `arm7`, memory writes (the three stores through their addresses, a range refused whole, 65536 bytes, an instruction that is written and stepped, a write refused while the target runs with `-32013` and not `-32003`), register writes (the value in its forms, the order of the errors, `flags.writable`, the register banks under `SR.RB`, `state.editWatch`), and the reset (while running and while paused, with the tables kept and the machine resumed behind it). On the three engines of the M1 gate | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2writes.guest` `.m2writes.guest.interp` | the same four parts against the sounding guest of the tree, with the notes `arm7: held at the start` and `arm7: bl stepped over and out` required: the stack at the top of main memory, the 64 KiB written back over the guest's own code, the register banks under the handler of the AICA's interrupt, the reset of a program that reios loads again | a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2` `.m2.interp` `.m2.fallback` | the gate of milestone M2 (`--milestone M2`): the eighteen scenarios of the M1 gate and S16 whole, with its five parts. The fifth, `tree`, is the device tree of M2: the 70 new nodes with their parents, the panels `sh4-registers` and `arm7-registers`, a register on every node whose registers are M2's, and each of the twelve register panels the action of one node. S08 reaches the bound of 256 watches, which the tree of 2940 registers lets it (2949 from milestone M3 on). On the three engines of the M1 gate | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m2.guest` `.m2.guest.interp` | the second run of that gate, against the sounding guest of the tree (`--milestone M2 --only S00,S16`), with the notes `arm7: held at the start` and `arm7: bl stepped over and out` required | a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m3` `.m3.interp` `.m3.fallback` | the gate of milestone M3, its first run (`--milestone M3`): the scenarios of the M2 gate and S17, the SGC capture (from a stop, the response only when the capture is complete, its stop in either order with it, while running, the bounds, 4096 frames in one binary frame of 32 MiB, the capture before while one is held open at a DSP breakpoint, a second request `-32003`) and the memory binary (the bytes of `state.getMemorySlice`, the unreadable runs, `-32010` and `-32004` with no frame, 16 MiB of main memory, wave memory through both targets, the frame to the requester only, one counter of binaryIds for both kinds): twenty scenarios passed, none partial and none not applicable. On the JIT, on the interpreter, and on the JIT with the interpreter fallback forced | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m3dsp.guest` `.m3dsp.guest.interp` | the second run of that gate, against the sounding guest of the tree (`--milestone M3 --dsp-program`: S00, S17 and S21), with the note `sgc: captured with the arm7 held` required: S17 makes its first capture at the anchor, where the guest holds its ARM7, and a record of a channel that sounds is required | a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.m3dsp21.guest` `.m3dsp21.guest.interp` | S00 and S21 of the gate of milestone M3, the scenario of the DSP, against the sounding guest of the tree (`--milestone M3 --dsp-program --only S00,S21`), with the note `dsp: stepped with the arm7 held` required: S21 steps the DSP at the anchor, where the guest holds its ARM7, waits for the guest's DSP program, and holds its steps, breakpoints and the nine datapath rows to the contract. The run of the gate in which S21 meets the held ARM7 (in the run S00, S17, S21 the capture of S17 lets the guest release it first) | a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.grace` | a client leaves watches and a target behind that is stopped at a breakpoint 2ndmix reaches in every frame: inside the grace period another client finds that stop; 11 s after the last client left the target has run on through the breakpoint (the active mask is cleared), and a client that connects and asks for nothing is sent the next stop of it (breakpoints are active again); the tables are as they were, the event log has the note, and the event gates are open again; that client resumes the target and leaves, and inside the grace period that begins there the breakpoint stops the target within three frames (the connect ended the expired state) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.config` | `Debugger:Required=yes` or `Debugger:StartPaused=yes` without an enabled server is a configuration error: exit code 2, a line on stderr that names the key, no port line and no exit line. And with an enabled server whose port is taken: the same exit code 2 with either key, one line on stderr and a run without the server with neither | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.slow` | the slow-client rule: a client that has stopped reading and is 10 MiB of ticks behind is queued none of the periodic ticks of 3 s, every tick that a request caused, and a full tick next | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.hash.off` `.hash.noclient`, each with `.interp` | 2ndmix for 600 vblanks, twice: the same state hash with no `Debugger:` key and with `Debugger:Enabled=no` (and no server without the key: the default of an automated run); the same with the server on and no client and with the server off | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.hash.idlebp` `.interp` | the same state hash with a client connected and a muted breakpoint on a function 2ndmix calls in every frame (the runner's `--idle-breakpoint`), and with the server off | `2ndmix.elf` of `NULLDC_ELF_DIR`, a dreamcast-devtools checkout with `node_modules`, npm |
| `debugger.contract.hash.listening` `.interp` | the same state hash with a client that describes itself at the `StartPaused` stop, lets the run begin and then only receives until the run ends, and with the server off. The periodic tick is a full one every 20 ms for this run (`Debugger:TickMs=20`, `Debugger:FullTickEvery=1`): every register of the device tree, which from milestone M2 on is every device, is read on the emulator thread some tens of times while 2ndmix runs its 600 vblanks. The client has to have got ten full running ticks at least, and no stop | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `debugger.contract.hash.listening.guest` `.interp` | the same on the sounding guest of the tree, whose AICA works: its registers, its channels and the interrupt controller of the ARM7 are read while the guest plays | none |
| `debugger.contract.hash.listening.rule` | what these four take for a full running tick, on ticks that are made up: every path that has registers, and every path but `dc.sh4.cpu` in a tick that says `registersStale`, which is every running tick of a recompiler whose SH4 context is not complete where a running machine is read (not the x64 one, so no run on an x64 host has such a tick). No emulator is run | none |
| `debugger.guest.dsptest.files` | the sounding guest of the tree (`debugger/guest/dsptest`: a bare-metal SH4 program with an ARM7 program and a DSP program inside it): its eight committed files are those of its manifest `SHA256SUMS`, and what `gen.sh` makes of the three sources, byte for byte (`gen.sh --check`). On Windows `gen.sh`, a bash script, is not run (`bash` there is WSL's launcher), and the test has to say so | none; an sh-elf binutils, and an arm-eabi binutils or clang, for the second half |
| `debugger.sound.lines.dsptest.jit` `.interp` | the sound line of the guest (`--config config:Debug.AicaHash=2000`, headless, `docs/nulldc3/automated-runs.md`) on the JIT and on the interpreter: two runs print one line, it has `ticks=2000`, and its `samples` is none of the 2001 hashes of silence (`debugger/guest/dsptest/README.md` has the script, `soundlines.py`, and every test of these rows) | none |
| `debugger.sound.engines.dsptest` | the line of the JIT and the line of the interpreter agree in `samples`, `regs`, `arm7`, `ticks`, `cycles` and `dsp`: everything the guest's SH4 does to the sound falls between the same two ticks of the AICA on both | none |
| `debugger.sound.muted.dsptest.jit` `.interp` | "Disable Sound" mutes: with `config:aica.NoSound=yes` the line of the guest is its line without the key in every field but `samples`, and `samples` is the hash of 2000 * 32 pairs of zeroes, a pair for every sample, folded where the sample hands its pair to the audio stream; the run without the key sounds. The line holds the harness's fold: that the stream itself, the speed limit of the emulator, is handed a pair of zeroes in every sample is held by `aica.dsp.emu.state` (its check `nosound`), since no run with `audio:backend=none` shows it | none |
| `debugger.sound.ignored.dsptest` | `config:aica.NoBatch`, which older builds read (the sound in batches of 32 samples without the DSP, or sample by sample) and wrote into every configuration file they saved, changes nothing: a run of the guest with it, `no` or `yes`, prints the line of the run without it | none |
| `debugger.sound.names` | no test of the sound (`debugger.sound.*`) or of the session `arm7` of the selftests (`debugger.selftest.arm7*`) has a part `batch` or `persample` in its name: the sound has one way to be made, and the tests of the two renders that were went with the batch render or lost the suffix (the `CTestTestfile.cmake` files of the build; no emulator is run). Other tests may have the words, which name something else there | none |
| `debugger.sound.release.dsptest.jit` `.interp` | a release of the guest's ARM7 starts the same program whatever request of the AICA is latched for it: the line of a copy of the guest whose SH4 leaves a request latched in front of the release is the line of the guest in `samples`, `regs`, `arm7`, `ticks` and `cycles`, and copies whose ARM7 reset routine lacks the read of L or the write of M print another `arm7`, which says that the request was there. The copies are made by the test, in its work directory | none |
| `debugger.sound.heard.dsptest.jit` `.interp` | the guest's DSP is heard: a copy of the guest whose table gives the two outputs of its DSP program `EFSDL` 0 (the same writes with another level) prints the guest's line in `arm7`, `sh4`, `dsp`, `ticks` and `cycles`, and another `samples`: the mix takes EFREG from the DSP. The copy is made by the test, in its work directory | none |
| `debugger.sound.oracle.dsptest.jit` `.interp` | the guest's DSP program on the emulator is the DSP of the caique sample model: the `dsp` field of a copy of the guest whose first channel has `IMXL` 0, so that every send to the DSP is 0, is what `aica_dsp_guest_oracle` (`aica_dsp/`) computes for that copy, the core and caique's model in lockstep on the guest's writes, on its schedule and with the emulator's sweep; and the guest itself, which sends, prints another `dsp`. The copy holds the half of the program that is the send's at 0 only, and the addresses of the wave memory where two ways of addressing meet only (`aica_dsp/README.md`). The copy is made by the test, in its work directory; the tests are registered in `aica_dsp/`, where the tool is built | a caique-rtl checkout |
| `debugger.sound.held.dsptest.jit` `.interp` | the AICA is clocked whatever `ARMRST` says, and only the ARM7 is held by it: a copy of the guest whose last release of the ARM7 is made a hold (the `mov #0, r0` at `g_main - 4` made `mov #1, r0`, checked to be `0xE000` first) and a copy whose ARM7 program has `b .` at `a_reset` print the same line in `samples`, `regs`, `sh4`, `dsp`, `ticks` and `cycles`: a held ARM7 is an idle one for the rest of the AICA. And their `arm7` fields differ, the held copy sounds (the channels the SH4 keyed play while the ARM7 is held), and the idle copy is not the guest (whose FIQ handler is heard). The copies are made by the test, in its work directory | none |
| `debugger.sound.neutral.idlebp.*` `.hitresume.*` `.steps.*` `.hoststop.*`, each `.jit` `.interp` | the debugger at work on the ARM7 of the guest changes nothing of the run: with a selftest session whose test never returns (`arm7n.idlebp`: an active breakpoint that is never reached, so that the debug loop runs every ARM phase; `arm7n.hitresume`: 200 stops at breakpoints, each resumed; `arm7n.steps`: some twenty thousand single steps, from stops with no burst suspended, across time steps, across the ends of bursts, and of the `msr` that lets a waiting FIQ in; `arm7n.hoststop`: sixty host stops, each with the thread parked on a burst that a breakpoint suspended inside a sample, so that the thread that ends runs the rest of the burst in front of its stop callback, and a start; at least five of them where the rest of the sample has a FIQ to enter behind the `msr` that ends the F window) the sound line at 2000 ticks is the line of the run without it, whole, and the state hash at 90 vblanks is its hash, part for part. Each test says what it did in lines the target requires (`debugger/guest/neutral/CMakeLists.txt`) | none |
| `debugger.sound.neutral.dsp.loop.*` `.dsp.hitresume.*` `.dsp.steps.*` `.dsp.hoststop.*`, each `.jit` `.interp` | the same for the debugger at work on the DSP of the guest (`dspn.*` of `dbg_selftest_dsp.cpp`): `dspn.loop`, the debug loop of the DSP forced for the whole run with nothing listed, so that every DSP phase goes through it; `dspn.hitresume`, at least 200 stops at breakpoints of the DSP, each resumed, in at least 100 bursts at at least 8 steps; `dspn.steps`, at least twenty single steps of the DSP of each of five kinds (from a stop with no burst suspended, inside a DSP phase, of step 127 across the end of a sample and across the end of a burst, from a stop of the ARM7); `dspn.hoststop`, sixty host stops with the thread parked on a burst suspended in front of a DSP step or at the end of a sample, which the thread that ends runs on in front of its stop callback. The sound line, whose `dsp` field folds the state of the DSP core at every tick, and the state hash are those of the run without the session | none |
| `debugger.sound.neutral.sgc.running.*` `.sgc.paused.*`, each `.jit` `.interp` | the same for the SGC capture (`sgcn.*` of `dbg_selftest_sgc.cpp`): `sgcn.running`, captures of 1 to 4096 frames while the machine runs, one behind the other, each armed in a table call and complete with its frames; `sgcn.paused`, captures armed at stops, each resumed, ten of each kind (behind a burst, in front of an ARM7 instruction, in front of a DSP step, at the end of a sample), each stopping behind its last frame. The sound line and the state hash are those of the run without the session | none |
| `debugger.sound.runner` | the runner of the sound lines on stand-ins for the emulator (`debugger/guest/dsptest/soundlines_test.py`; no emulator is run): a run that was not made on the SH4 engine it was asked for fails, in every mode, with or without a session; its modes `held`, `heard` and `oracle` on copies of `dsptest.elf`, with stand-ins whose line depends on the guest they are given (and for `oracle` stand-ins for its tool): each passes where the copies agree, fails naming the field where a copy differs in one of the fields the mode compares (`regs`, `sh4`, `dsp` and `samples` for `held`; `arm7`, `sh4`, `dsp` and `cycles` for `heard`; `dsp` for `oracle`), and fails each of its controls and on a guest that does not have what the copy changes; its mode `silent` passes on the silence of N ticks and fails on that of N - 1; its mode `release` fails on a guest whose table has no empty end; and its mode `names` on stand-ins for a build directory: a name of the sound or of the session `arm7` with a part `batch` or `persample` fails, in any directory of the build, and other tests with those words pass | none |
| `debugger.sound.lines.2ndmix.jit` `.interp` | the sound line of 2ndmix at 40000 ticks, the program the harness was accepted with: two runs print one line, and its `samples` is the silence of exactly 40000 ticks, 32 pairs of zeroes for each (it plays nothing under reios; every tick makes its 32 samples, also while it holds its ARM7) | `2ndmix.elf` of `NULLDC_ELF_DIR` |
| `aica.dsp.api` | the AICA DSP core (`libswirl/hw/aica/dsp_core`) on itself: a sample in one go against its 128 steps from every step, state out and in, two cores in turns, its registers, fields that hold more than their registers' bits, a wave RAM of 4, 8 and 16 MB; and its known-answer programs: the shifter, the address of a memory step on 2 to 16 MB, the ring, the read latch rules, the MIXS banks | none |
| `aica.dsp.diff.random` `aica.dsp.diff.sparse` `aica.dsp.diff.directed` | the AICA DSP core against the DSP of the caique sample model on generated programs, compared behind every step | a caique-rtl checkout |
| `aica.dsp.case.dsp_basic` `.dsp_mem` `.dsp_mem2` `.dsp_mem3` `.dsp_wslot` `.dsp_unpack` `.dsp_pack` `.dsp_temp` `.dsp_coll` `.sh4_latch` `.mdec_hold` | the DSP programs caique ran on a Dreamcast, on the model with the core in step and on the core alone, against the console's files | a caique-rtl checkout |
| `aica.dsp.case.dsp_basic.emu` `.dsp_mem.emu` `.dsp_mem2.emu` `.dsp_mem3.emu` `.dsp_wslot.emu` `.dsp_unpack.emu` `.dsp_pack.emu` | the seven of those programs anchored directly on the console's files, on the emulator's own sound chip (its AICA device with the register routing, its sound generator, its DSP device: `aica_dsp/emu_aica.h`), against the console's files and against the core alone | a caique-rtl checkout |
| `aica.dsp.emu.lockstep` `aica.dsp.emu.state` | the emulator's sound chip against a core of the test's own on generated programs, with CPU writes of every form and channels whose sends the test works out from the samples, behind every sample; the mix taking EFREG from the core, the DSP device's reset and its save state, the bus of a channel in the AICA's save state, and "Disable Sound": a pair of zeroes to the audio stream in every sample, the pair the harness folds, and nothing else changed | none |
| `aica.dsp.capi` | the C interface of the AICA DSP core, compiled for the host, against the core called itself, on random programs and on the known-answer programs | none |
| `aica.dsp.wasm` | the WebAssembly build of the AICA DSP core in node, through its loader and its C interface, against the core compiled for the host, on the same programs | emcc and node, skipped without them |
| `aica.dsp.wasm.emsdk` `aica.dsp.wasm.noemcc` | the script that builds it, run by sh on a PATH without emcc: it builds with the emsdk in `EMSDK`, and says that it has no emcc when there is no emsdk | an emcc that is in an emsdk, and node; skipped otherwise |
| `aica.dsp.wasm.devtools` | the DSP module the UI commits (`src/wasm/aica-dsp-core/` of `NULLDC_DEVTOOLS_DIR`) is this tree's core: its `PROVENANCE.json` has the fixed keys and names the two files beside it, byte for byte; the trace that `aica.dsp.wasm` replays is replayed on the UI's loader and wasm with every answer the native core's; and where the emcc `build.sh` uses (the `PATH`, else the emsdk of `EMSDK`, else `~/emsdk`) has the version `PROVENANCE.json` names, the two files are the bytes of a fresh `build.sh` of the tree | node and the UI checkout (`NULLDC_DEVTOOLS_DIR`); no emcc |
| `windows.winrun.probe` | Windows only (`windows/README.md` has these): the probe of `nulldc3-winrun` (`winrun/README.md`) names its private desktop `winrun-<n>`, its visible window is in the list, the verdict is 125 | none |
| `windows.winrun.detach` | Windows only: `nulldc3-winrun --detach` returns at once though its output is read through a pipe, and the copy runs the program to its end and writes its rc file | none |
| `windows.headless.nowindow` | Windows only: the guest of the tree headless with `NULLDC_TEST_UI_REPORT=1`, through `nulldc3-winrun`: exit 0, the exit line, no window and no COM apartment window, the no-dialog policy read back (`wer`, `invparam`, `purecall`, `sigabrt`, `abort`) | the emulator |
| `windows.fatal.segv` `.ill` `.trap` `.abort` `.div` `.stack` `.invparam` `.purecall` | Windows only: `NULLDC_TEST_FATAL=<kind>`: the `fault_handler` line and 128 + the signal (for `trap`, `die()`'s line, no `fault_handler` line, 133), no window, nothing new in the crash-report places | the emulator |
| `windows.helpbox` | Windows only: the help/error box is shown only where the output cannot be read and the run is not headless: a pipe, `NUL`, a console of its own headless (exit 2, no window), a console of its own not headless (the box, on the private desktop) | the emulator |
| `windows.abi.xmm` | Windows only: the x64 backend's `Mainloop` and `RunBlock` give `xmm6`-`xmm15` back (an Xbyak canary thunk, a machine of its own), and a stack walk from `UpdateSystem_INTC` through each comes to the thunk with the emulator's unwind data | none |
| `windows.binary.stdout` `windows.exitcode` | Windows only: no `\r` in the emulator's stdout or stderr; headless with nothing to run exits with 1 | the emulator |
| `windows.dsound.failed` | Windows only: DirectSound that fails (`NULLDC_TEST_DSOUND=fail`) is no sound, and the guest headless with the speed limit on still reaches its exit | the emulator |
| `macos.sandbox.canary` | macOS only (`macos/README.md` has these): the canary of `macos/macrun.py` cannot reach the window server under it, and the kernel says that it is sandboxed and that the window server's lookup is denied; `macrun.py` applied the profile for a caller (ctest) without one, and ran under the caller's when ctest itself runs under a profile | none |
| `macos.macrun.tree` | macOS only: the timeout of a long step, `macos/macrun.py --detach` around a ctest whose test runs `macrun.py` again around a sleeper (which that one starts in a session of its own): the step ends with 124, and the sleeper ends with it, though its `macrun.py` dies in the same sweep | none |
| `macos.headless.nowindow` | macOS only: the guest of the tree headless with `NULLDC_TEST_UI_REPORT=1`, through `macrun.py`: exit 0 by an exit, the exit line, `nulldc-ui: app=0 windows=0` (no `NSApplication`), the ARM7 on the interpreter | the emulator |
| `macos.fatal.segv` `.ill` `.trap` `.abort` | macOS only: `NULLDC_TEST_FATAL=<kind>`: the `fault_handler` line and 128 + the signal by an exit, not the signal (`segv` writes to address 16, a `SIGSEGV` at address `0x10` as on Linux: 139, 132, 134; for `trap`, `die()`'s line, no `fault_handler` line, 133), the stdout printed before the fault (stdout is line-buffered), nothing new in `~/Library/Logs/DiagnosticReports` | the emulator |
| `macos.refused` | macOS only: a refused command line, not headless, with stderr to a pipe and to `/dev/null`, and headless: exit 2, no `NSApplication` (no alert), the reason on stderr where it goes | the emulator |
| `macos.bundle.codesign` | macOS only: the emulator's `nulldc3.app`: its ad-hoc signature (`codesign --verify --strict --deep`), its `Info.plist` (`plutil -lint`, every key of the template, the version keys against the build's `nulldc3-version.cmake`), the icon and exactly the licences that match whether the build embeds the UI | the emulator |
| `macos.build.ccache` | macOS only: neither the tests project's build nor the emulator's (`NULLDC_EMULATOR`'s) compiles through ccache unless its configure line has `-DNULLDC_CCACHE=ON`, and with it and a ccache found both do; read from their commands (`ninja -t commands`, or the `build.make` files), nothing is compiled | none |
| `macos.icns.iconutil` | macOS only: Apple's `iconutil` reads `nulldc3/apple/nulldc3.icns` into its ten images, and makes an `.icns` of the approved PNGs (which elements it writes: the test's output) | none |

The SH4 dynarec tests run on x86-64 hosts only. The wren7 tests need `-DNULLDC_WREN7_DIR=<path to wren7-rtl>`
unless wren7-rtl sits next to the repository; `arm7/README.md` has what has to be built in it. The system tests
and the `debugger.selftest.*`, `debugger.contract.*` and `debugger.sound.*` tests run on Linux hosts with python3;
the latter three are skipped where there is no emulator to run (`-DNULLDC_SYSTEM_TESTS=OFF` without
`-DNULLDC_EMULATOR=`), but for `debugger.sound.runner`, which runs none.
The `aica.dsp.diff.*` and `aica.dsp.case.*` tests (the `.emu` ones among them) and `debugger.sound.oracle.dsptest.*` need `-DNULLDC_CAIQUE_DIR=<path to caique-rtl>` unless caique-rtl
sits next to the repository; they compile its sample model and its DSP programs (C) from there and read its console
captures.

The emulator has an SH4 recompiler for x86-64 hosts and one for arm64 hosts, an ARM7 recompiler for arm64 hosts
that no build has in this round (`FEAT_AREC` is none on every host: the ARM7 runs on its interpreter everywhere),
and its interpreters on every host; its DSP is the one core of `libswirl/hw/aica/dsp_core` on every host; it does not configure for 32-bit x86 and arm hosts. Of the recompilers the
tests here run the x86-64 backend. `-DTARGET_NO_REC=ON` builds the emulator without a recompiler on any host. A
generic host, one whose processor `nulldc3/cmake/config.cmake` has no name for, gets that build without the fault
handler, so without nvmem; on an x86-64 machine `-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=<such a name>`
configures it. The system tests take such a binary with `-DNULLDC_EMULATOR=`. On a `TARGET_NO_REC` build every case
passes but `guest_exit.jit`, `boot.2ndmix.jit`, `boot.hello.jit` and the three `vmem.*` cases, which have to see the
recompiler, and `fault.guest`, whose faults are the recompiler's; without the fault handler the `fault.*` cases have
nothing to wait for either, and the `vmem.*` cases no layout. The `debugger.sound.*` tests ask of every run that it
was made on the engine it was asked for: with such a binary their `.jit.` tests and `debugger.sound.engines.*` fail,
and their `.interp.` tests pass. The `debugger.selftest.*` and `debugger.contract.*` tests ask the same of their
runs: on such a binary every one of them that asks for the JIT fails.

Every test of this table but `common.selftest`, `sh4.core`, the `debugger.sh4bp.*`, `debugger.selftest.*` and
`debugger.contract.*` tests, `cfg.overrides`, `cfg.log_file`, `runctl.guest_exit.*`, the system tests, the `devtools.*` tests, `rebrand.names`, `icons.generated`, `icons.x11`, the `version.*` tests, `aica.dsp.capi` and the `aica.dsp.wasm` tests is
gated on a baseline file, and ctest runs them with `--strict`: a count that is worse than its baseline fails, and
so does one that is better, until the baseline is updated and committed. `common.selftest`, `sh4.core`, each
`debugger.sh4bp.*` test, each `debugger.selftest.*` test, each `debugger.contract.*` test, `cfg.overrides`,
`cfg.log_file`, each `runctl.guest_exit.*` test, each
system test, each `devtools.*` test, `rebrand.names`, `icons.generated`, `icons.x11`, `version.derive`, `version.build_time`, `version.cl`, `version.binary`, `aica.dsp.capi`, `aica.dsp.wasm` and `aica.dsp.wasm.devtools` are lists of checks that all have to hold;
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
* `rebrand/README.md`: `rebrand.names`, the format of its allow list `rebrand/allowed.txt` and how a package
  changes a row of it when it adds, moves, rewraps or deletes a line of history,
* `../nulldc3/icon/README.md`: the icon files `icons.generated` checks, how they are made from the approved PNGs and
  how to make them again,
* `aica_dsp/README.md`: the AICA DSP core's tests, which of the console's captures are compared with the core
  directly and which through the caique model, and the tests of the core in the emulator's sound chip;
  `aica_dsp/KNOWN_FAILURES.md`: the three lines of a console report that depend on the console's timing, and the
  five of another that need a CPU's read in the DSP's latch, which the emulator does not wire.

## On Windows

The project builds with MSVC (Visual Studio 2026, its CMake and Ninja, from its developer prompt) the way it builds on
Linux, RelWithDebInfo by default, with the static C runtime. Every program it runs there runs inside
`nulldc3-winrun` (`winrun/README.md`): ctest itself runs inside one, the tests of `windows/` start the emulator
through it, and the harnesses of the emulator-driven tests (`debugger/selftest`, `debugger/contract`,
`debugger/guest`, the oracle of `aica_dsp`, `debugger/sh4bp/sessions.py`) start the emulator, the contract runner
and their tools through another, inner one (`common/emurun.py`), and `debugger.disasm.dsptest` and
`debugger.disasm.dsp_playground` run their scripts (and the `tsx.cmd`, `cmd.exe` and `node` behind them) inside one of
their own, with `TEMP`, `TMP` and npm's cache in the build directory, so that no window or box of a test reaches the
desktop and ending a run ends all of it. It does not stop crash reports; the programs' own policies do: the
emulator's (`docs/nulldc3/automated-runs.md`), and the same for every program of the tests project,
`common/win_policy.cpp`: every suite that links `common/test_guard.cpp` and every other program, which links the file
itself (no box, no crash report, one line and 128 + the signal's number; `common.policy.windows` reads it in every
program the project builds, `common.guard.windows` makes four of them fault). The harnesses give the programs that
are not the emulator (the contract runner, the tools) `TEMP`, `TMP` and npm's cache in their run's directory; set
`TEMP`, `TMP` and `npm_config_cache` to a directory of your own for the rest of a run as well (ctest, the scripts,
`npm ci`), or they are the profile's.

    set TEMP=<scratch dir>& set TMP=<scratch dir>& set npm_config_cache=<npm cache dir>
    cmake -S tests -B <build dir> -G Ninja -DNULLDC_EMULATOR=<build>\nulldc3.exe -DNULLDC_ELF_DIR=<dir> ^
          -DNULLDC_WREN7_DIR=<dir> -DNULLDC_CAIQUE_DIR=<dir> -DNULLDC_SHRIKE4_DIR=<dir> ^
          -DNULLDC_DEVTOOLS_DIR=<UI checkout with node_modules> -DPython3_EXECUTABLE=<python.exe> ^
          "-DNULLDC_NPM=C:/Program Files/nodejs/npm.cmd" -DNULLDC_TESTS_REQUIRE_DATA=ON
    ninja -C <build dir>
    nulldc3-winrun --timeout 5400 -- ctest --test-dir <build dir> -j 4 --output-on-failure

- `NULLDC_EMULATOR`: `nulldc3.exe` of an emulator build (configured with `-DNULLDC_DEVTOOLS_DIST=OFF` for a gate).
  `tests/system` builds no emulator on Windows: without it the emulator-driven tests are skipped, and say so.
- `NULLDC_WINRUN`: the `nulldc3-winrun.exe` the tests start programs through; by default the one `windows/` builds.
  The tests that run a harness have it in their environment, and a harness on Windows starts nothing without it.
- `Python3_EXECUTABLE`: a Python of 3.9 or later; on Windows the `python.exe` of the PATH may be the Store's alias.
- `NULLDC_NPM`: `npm.cmd` (the `npm` beside it is a shell script, which `CreateProcess` cannot start); found as
  `npm.cmd` first when it is not given. The UI checkout (`NULLDC_DEVTOOLS_DIR`) has `npm ci` made on Windows, and the
  contract runner runs from it against `127.0.0.1`.
- The data: the sibling checkouts need only the files the suites read: 74 of caique-rtl (its sample model, its DSP
  programs and their console captures), 120 of wren7-rtl (the job files of `model/tests`, `vendor/` and the ARM
  programs of `build/hw`) and 12 of shrike4-rtl (the three headers of `sh4_fpu`, `tools/hw/fpdn/*.bin` and the console
  records the vector suite reads), about 12 MB; and `2ndmix.elf` and `hello.elf` in `NULLDC_ELF_DIR`.

What is skipped on Windows, with its reason (73 of the 340 tests; every other one runs, and the gate has no open
defect):

| tests | reason |
|---|---|
| `system.*`, `system.hle.*` (63) | the Linux-only system-test harness |
| `aica.dsp.wasm` `.wasm.emsdk` `.wasm.noemcc` (3) | no emcc |
| `debugger.disasm.sh4_objdump` `.sh4_objdump_selftest`, `.arm7_objdump` `.arm7_objdump_selftest` (4) | no sh-elf-objdump / arm-eabi-objdump on this host (a skip of the platform also with `NULLDC_TESTS_REQUIRE_DATA`; on Linux a missing tool fails) |
| `debugger.sh4bp.b11` (1) | it counts host instructions with the performance counters of Linux (`perf_event_open`) and says `SKIPPED` elsewhere |
| `cfg.log_file` (1) | the test forks: POSIX hosts only |
| `debugger.sound.runner` (1) | its stand-ins for the emulator are scripts that the host runs by their first line (`#!`): a Linux host |

The selftests of the debugger run on Windows as on Linux: their naps for fractions of a millisecond go through
`dbg::selftest::NapUs` (`libswirl/debugger/dbg_selftest_nap.h`), which is `std::this_thread::sleep_for` on a POSIX host
and on Windows a high-resolution waitable timer and a spin of the last 500 us of a nap, asking for no resolution of
the system timer: `std::this_thread::sleep_for` of Windows lasts a tick of that timer, 15.6 ms, whatever it is asked
for, and `debugger.selftest.emu` outlasted its watchdog there. `debugger.selftest.nap` holds the naps.
`disasm_test` writes its stdout in binary mode on Windows, each line ending in `\n` as on Linux, which is what
`debugger.disasm.dsp_playground` takes apart. A build of `debugger/disasm` by itself has no `nulldc3-winrun` on
Windows unless `-DNULLDC_WINRUN=` names one, and there `dsp_playground` is a skip of the platform
(`debugger.disasm.standalone`).

## On macOS

The project builds with the AppleClang of Xcode and CMake and Ninja from Homebrew, as on Linux (macOS 11 or newer
on Apple silicon). The emulator is the executable of an emulator build's bundle; `tests/system` builds none on macOS.
Every emulator run the tests make there goes through `macos/macrun.py` (`macos/README.md`), under the sandbox profile
`macos/no-windowserver.sb`, which denies the window server, the window manager, the Dock and LaunchServices: the
tests of `macos/` and `version.binary` start the emulator through it, the harnesses of the emulator-driven tests
(`debugger/selftest`, `debugger/contract`, `debugger/guest`, the oracle of `aica_dsp`, `debugger/sh4bp/sessions.py`)
start the emulator, the contract runner and their tools through it (`common/emurun.py`: a session of its own, which
`macrun.py` kills with everything below it when a harness ends the run; `TMPDIR` in the run's directory, and for the
contract runner and the tools npm's cache there too), and so does ctest itself when it runs as a long step (inside it
`macrun.py` finds its caller sandboxed and runs under that profile). Set `TMPDIR` and `npm_config_cache` to
directories of your own for the rest of a run as well (ctest, the scripts, `npm ci`), and `EMSDK` (below).

    cmake -S tests -B <build dir> -G Ninja -DNULLDC_EMULATOR=<build>/nulldc3.app/Contents/MacOS/nulldc3 \
          -DNULLDC_ELF_DIR=<dir> -DNULLDC_WREN7_DIR=<dir> -DNULLDC_CAIQUE_DIR=<dir> \
          -DNULLDC_DEVTOOLS_DIR=<UI checkout with node_modules> -DPython3_EXECUTABLE=/usr/bin/python3 \
          -DNULLDC_TESTS_REQUIRE_DATA=ON
    ninja -C <build dir>
    python3 tests/macos/macrun.py --detach --log <file> --rc <file> --name ctest --work <dir> --timeout 5400 -- \
          ctest --test-dir <build dir> -j 4 --output-on-failure

- `NULLDC_EMULATOR`: `nulldc3.app/Contents/MacOS/nulldc3` of an emulator build (configured with
  `-DNULLDC_DEVTOOLS_DIST=OFF` for a gate); `macos.bundle.codesign` checks the bundle around it. `tests/system`
  builds no emulator on macOS: without it, or with one that names no file, every test that runs the emulator is a test
  whose data is missing (a failure with `NULLDC_TESTS_REQUIRE_DATA`), and the configure says so.
- `NULLDC_DEVTOOLS_DIR`: a checkout of the UI with `npm ci` made on the Mac (`node` and `npm` from Homebrew); the
  contract runner runs from it against `127.0.0.1`.
- ccache: on macOS neither the tests project nor the emulator compiles through ccache unless the configure line has
  `-DNULLDC_CCACHE=ON` (a ccache that cannot start ends every compile in dyld's abort, and each abort leaves a crash
  report); `macos.build.ccache` holds it for the tests project's build and for the emulator's.
- `Python3_EXECUTABLE`: Xcode's `/usr/bin/python3` (3.9) is the one the round's scripts keep to.
- The data: the same files of caique-rtl (74) and wren7-rtl (120) as on Windows, and `2ndmix.elf` and `hello.elf`;
  shrike4-rtl is not needed (`sh4.fpu.*` is x86-64 only).
- `NULLDC_EMCC` and `EMSDK`: the AICA DSP's WebAssembly tests look for an emsdk in `EMSDK` and in `~/emsdk`, and emcc
  writes into the emsdk it runs from (its cache, and the bytecode of its Python, which it runs with `-E`). Where that
  emsdk is not the run's to write into, `-DNULLDC_EMCC=OFF` skips the three that build with it, and `EMSDK` set to a
  directory without one for ctest keeps `aica.dsp.wasm.devtools` from asking `~/emsdk` (it then says that the
  committed module is not compared with a fresh build).

What is skipped on macOS (arm64), with its reason (118 of the 331 tests; every other one runs):

| tests | reason |
|---|---|
| `system.*`, `system.hle.*` (63) | the Linux-only system-test harness |
| `debugger.sh4bp.*` (29) | the sh4bp gates need an x86-64 build with the SH4 recompiler |
| `sh4.singlestep.dynarec`, `sh4.directed.dynarec*`, `sh4.differential`, `sh4.directed.differential`, `sh4.fpu.*` (9) | they run the blocks of the x64 backend |
| `aica.dsp.wasm` `.wasm.emsdk` `.wasm.noemcc` (3) | no emcc (or `NULLDC_EMCC=OFF`) |
| `debugger.sound.runner` (1) | its stand-ins for the emulator are scripts that the host runs by their first line (`#!`): a Linux host |
| the known limits of the arm64 recompiler's debugger (13) | `KNOWN LIMIT (A64), NOT RUN`: below, "Known limits of the arm64 recompiler's debugger (A64)" |

The objdump oracles of `debugger/disasm` run where the binutils are: `arm-none-eabi-objdump` of Homebrew
(`brew install arm-none-eabi-binutils`), and `sh-elf-objdump` where a KallistiOS toolchain is at
`/opt/toolchains/dc` (Homebrew has none); `gen.sh --check` of the sounding guest likewise runs where `sh-elf-as`
is. `macos.hashes.jit` holds the state hashes of the arm64 recompiler to the Mac's own references (a hash is good for
one host: nothing compares the Mac's lines or hashes with Linux's).

**The arm64 recompiler and the debugger's arm64 rules** (design 2.2.8, 2.3.5). Its breakpoints and steps run on the
interpreter fallback, with or without `Debugger:ForceFallback` (`sh4-debug-fallback`); its running ticks say
`registersStale` and leave out `dc.sh4.cpu`; and it restarts its slice counter at every entry of its loop, so that a
pause, a step or a stop of the host moves SH4 time by less than a slice. The harnesses expect the fallback engine
there (`NATIVE_RECOMPILER`), `debugger.selftest.emurun.pauses` runs on the interpreter only on a host that is not
x86-64, and S04 of the contract gates is recorded, not gated, on the recompiler of an arm64 host, with and without
`--fallback` (PL-D78; `debugger/contract/README.md`): `contract.py` passes a gate whose runner failed S04 alone, on
its `SH4_CYCLES` checks only, and prints `S04: recorded (PL-D78): <detail>`; the interpreter is gated in full.
Thirteen targets on the recompiler meet those rules and fail, on the Mac and on Linux arm64 alike: they are the known
limits of the arm64 recompiler's debugger, registered as such on every arm64 host (below).

## On Linux arm64

The project builds natively on an arm64 Linux host (Ubuntu 24.04 on aarch64, GCC 13, CMake and Ninja of the
distribution) with the configure line of Linux x86_64, and `tests/system` builds its emulators there as on Linux
x86_64: the arm64 SH4 recompiler (`libswirl/jit/backend/arm64`, its debug engine `sh4-debug-fallback`), no ARM7
recompiler (the ARM7 runs on its interpreter, as on every host), `-ffp-contract=off` (`nulldc3/cmake/config.cmake`).
The emulator is built for 4 KiB pages: on a kernel with 16 or 64 KiB pages it refuses to start, with
`nulldc3: this build needs 4 KiB pages; this kernel uses <n> KiB` on stderr and exit status 1
(`libswirl/linux/common.cpp`; `linux_arm64.pages.refused` shows it through the test hook `NULLDC_TEST_PAGE_SIZE`).
The tests of `linux_arm64/` run there and nowhere else: `linux_arm64.hashes.jit` holds the state hashes of the arm64
recompiler on Linux to the references of `linux_arm64/linux_arm64.cmake` (the arm64 host's, which equal the Mac's of
`macos.hashes.jit`; a hash is good for one host, and another host whose recompiler computes otherwise needs references
of its own), and `linux_arm64.pages.refused` the refusal. Their runs are headless, without `DISPLAY` and
`WAYLAND_DISPLAY`, with a core file size limit of 1 byte (as `common/emurun.py` starts an emulator on a POSIX host, so
that a crash is not piped to the system's core handler, apport or systemd-coredump), and neither test starts an
emulator before a shell run the same way has shown that envelope. The harnesses expect the debugger's arm64 rules
there as on the Mac (S04 of the contract gates recorded on the recompiler and the forced fallback, PL-D78).

The objdump oracles of `debugger/disasm` need `sh-elf-objdump` and `arm-eabi-objdump` (`-DSH_OBJDUMP=`,
`-DARM_OBJDUMP=`), and fail without them on Linux. An `sh-elf-objdump` for an arm64 host has to be built with
`-fsigned-char`: binutils' SH disassembler takes the sign of an 8-bit branch displacement from a cast to `char`
(`opcodes/sh-dis.c`), which is unsigned on arm64 Linux, and an objdump built as it is puts the target of every `bt`,
`bf`, `bt/s` and `bf/s` with a negative displacement 512 bytes too far (`debugger.disasm.sh4_objdump`: 512
unexplained encodings).

What is skipped on Linux arm64, with its reason (58 of the 321 tests; every other one runs):

| tests | reason |
|---|---|
| `debugger.sh4bp.*` (29) | the sh4bp gates need an x86-64 build with the SH4 recompiler |
| `sh4.singlestep.dynarec`, `sh4.directed.dynarec*`, `sh4.differential`, `sh4.directed.differential`, `sh4.fpu.*` (9) | they run the blocks of the x64 backend |
| `aica.dsp.wasm` `.wasm.emsdk` `.wasm.noemcc` (3) | no emcc |
| `system.vmem.none`, `system.vmem.taken`, and the two of `system.hle.*` (4) | they say `SKIPPED` where the host gives no unprivileged user and mount namespace (`unshare -rm`), as Ubuntu 24.04's AppArmor restricts them by default |
| the known limits of the arm64 recompiler's debugger (13) | `KNOWN LIMIT (A64), NOT RUN`: below |

## Known limits of the arm64 recompiler's debugger (A64)

The arm64 SH4 recompiler has no native debugger support (design 2.3.5: the x86-64 recompiler has it; a later
package, A64, gives it to the arm64 one). Thirteen targets fail on it for that, and on an arm64 host, macOS and Linux,
they are registered as known limits (the owner's decision of 2026-10-22): each prints
`KNOWN LIMIT (A64), NOT RUN: <cause>` and ctest counts it as skipped. Their twins on the interpreter (`.interp`) run on
every host, and on every host that is not arm64 the targets themselves run as they always did.

| target | cause |
|---|---|
| `debugger.selftest.emu`, `debugger.selftest.regs`, `debugger.selftest.core` | the arm64 recompiler keeps the SH4's registers in host registers inside a slice and stores its context only at the slice's end: the SH4's registers of a running tick are stale (`registersStale`), and what the checks read of the SH4's context while the machine runs, or at a slice end, is not the machine's (`emu.pause`: the pc of the context at the park is not the one at the latch; `regs.tick` and `core.tick`: a running full tick has no `dc.sh4.cpu`; `core.tables`, `core.writes`: a watch and a register write of the running SH4) |
| `debugger.selftest.hostcycles`, `debugger.selftest.writesrun.neutral` | the arm64 recompiler starts its slice counter anew at every entry of its loop: a stop and its resume move SH4 time by up to a slice (PL-D78's S04), and the run the debugger stopped and resumed does not end in the state hash of the run nobody touched |
| `debugger.sound.neutral.hitresume.jit`, `.steps.jit`, `.hoststop.jit`, `.dsp.hitresume.jit`, `.dsp.steps.jit`, `.dsp.hoststop.jit`, `.sgc.running.jit`, `.sgc.paused.jit` (8) | the same as the line above: the state hash at 90 vblanks of the run with the session is not the one of the run without it (its `sh4` part); the sound line at 2000 ticks is |

The list is `nulldc_a64_known_limits()` of `debugger/selftest/CMakeLists.txt`: whole target names, each a run of the
SH4 on the recompiler with a twin on the interpreter, and the cause of each; `nulldc_debugger_selftest` and
`nulldc_sound_neutral` (`debugger/guest/dsptest/CMakeLists.txt`) register them. `debugger.a64.knownlimits` checks the
registration on every host, deciding from `CMAKE_SYSTEM_PROCESSOR` by itself whether the host is arm64: every entry is
such a target, its twin is registered to run, on an arm64 host the target is a known limit and on any other host it is
registered to run, and nothing else is a known limit. On the Mac the thirteen failed in every run of MACb's gate with
the same lines, on Linux arm64 in LA64's, and passed on the interpreter; nothing else of the debugger's tests fails on
the arm64 recompiler. When A64 has given the recompiler its native debugger support, an entry leaves the list with
the change that makes its target pass there.

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
    cfg/               the config store: --config overrides and what a save writes; the log of --log
    run_control/       the keys that end a run: which runs the end of the guest program ends
    system/            the emulator binary, headless, on guest programs (README.md)
    perf/              not a test: perf.py measures the speed of the emulator on a guest program (README.md),
                       BASELINE.md has the figures of one machine
    aica_dsp/          the AICA DSP core: its interface, the caique model step for step, caique's console
                       captures, the C interface and the WebAssembly build (README.md, KNOWN_FAILURES.md)
    devtools/          devtools.commit: the UI checkout of the tests against the submodule devtools/, and
                       its script on scratch repositories (devtools.commit_check); lookup.cmake, where the
                       tests look for the UI, on scratch layouts (devtools.lookup)
    rebrand/           rebrand.names: where the tree may say the name of the emulator it was forked from
                       (allowed.txt, README.md)
    icons/             icons.generated: the icon files made from the approved PNGs of nulldc3/icon/ are the
                       committed ones; icons.x11: the values of the X11 window's icon
    macos/             macOS only: macrun.py, the run envelope of the Mac, its sandbox profile and its canary;
                       the emulator headless, its fatal ends, a refused command line, the bundle and its icon
                       (README.md)
    linux_arm64/       Linux arm64 only: the state hashes of the arm64 recompiler there, and the refusal of a
                       kernel whose pages are not 4 KiB (CMakeLists.txt)
    version/           version.derive: the rule of the emulator's version (nulldc3/cmake/version.cmake) on
                       scratch repositories; version.build_time: the version is made at the build, not at the
                       configure (a clone, a commit, no reconfigure); version.cl: --version of cfg/cl.cpp and
                       cl_FinishText(); version.binary: the emulator's --version is the tree's version
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

Things the vectors do that nullDC currently does as well, and that pass for that reason: SR.RB is forced to 0
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

* `nulldc_add_libswirl_library(<target> [SHREC|AREC <DYNAREC_*>] SOURCES <files relative to libswirl>)`:
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
| `debugger.net.test_bounds` | the two bounds of what a client may leave waiting, counted apart: a binary message of 32 MiB and its header (an SGC capture of 4096 frames) followed by ticks, read slowly, keeps the client and comes whole and in order with them; more than 16 MiB of text, behind such a binary message, and more than 64 MiB of binary messages, with text between them, cut it off | nothing |
| `debugger.net.test_webui` | the embedded UI: the bundle the build embedded is served file for file | nothing (the bundle is `debugger/net/bundle`) |
| `debugger.net.interop_ws` | the server against the `ws` package, the WebSocket implementation of the devtools tooling | node, a dreamcast-devtools checkout with `node_modules` |
| `debugger.net.embed_off` `embed_auto` `embed_pin` `embed_mismatch` `embed_incomplete` `embed_rebuilt` `embed_corrupt` `embed_names` | the UI embed step of the emulator build: which bundle is picked (an uninitialised submodule `devtools/` is not one), what configure says about the commit the bundle was built from against the commit the tree pins for its submodule, what stops the configure step or the build, what a rebuilt bundle does, which files are embedded | nothing |
| `debugger.disasm.golden` | `dbg_disasm.cpp`: checked-in encodings of SH-4, ARM7 and the AICA DSP, and the invariants that need no tool | nothing |
| `debugger.disasm.sh4_objdump` `debugger.disasm.sh4_objdump_selftest` | every SH-4 opcode against GNU objdump, and that comparison on dumps with a wrong field | python3, a SuperH objdump |
| `debugger.disasm.sh4_opcode_table` | every SH-4 opcode against `hw/sh4/sh4_opcode_list.cpp` | python3 |
| `debugger.disasm.arm7_objdump` `debugger.disasm.arm7_objdump_selftest` | 249,364 ARM encodings against GNU objdump, and that comparison on dumps with a changed field | python3, an ARM objdump |
| `debugger.disasm.dsp_playground` | 100,193 DSP steps against the disassembler and assembler of the devtools playground | a dreamcast-devtools checkout with `node_modules` |
| `debugger.disasm.standalone.configure` `debugger.disasm.standalone` | Windows only: `debugger/disasm` built by itself, as its `CMakeLists.txt` says, without `NULLDC_WINRUN` (configured only, with the generator, compiler and Python of the tests project and its devtools checkout): its `dsp_playground` is the skip of the platform there, `SKIP: no nulldc3-winrun to run the script inside`, and runs no `tsx.cmd` or `node` outside an `nulldc3-winrun` | none |
| `debugger.disasm.dsptest` | the DSP program of the sounding guest: the disassembler of the 128 steps as its ELF loads them gives the `MPRO` lines of its text, `dsptest_dsp.txt`, and `TRA:0` behind them; and the playground's assembler gives the ELF's words for those lines, and its `COEF`, `MADRS` and `MEMS_H` | python3; a dreamcast-devtools checkout with `node_modules` for the second half |
| `debugger.regs.tables` `debugger.regs.sources` | `dbg_regs_tables.cpp`, the device tree behind `debugger.describe`: the tables by themselves (104 nodes; 2940 registers in milestone M2, 2949 from milestone M3 on, with the nine of the DSP's datapath, the fields of the AICA's register file bit for bit), and their on-chip, system bus and PVR rows against `hw/sh4/sh4_mmr.h`, `hw/holly/sb_regs.h` and `hw/pvr/pvr_regs.h` | nothing |
| `debugger.regs.panels` | the panel ids of the tables against the list of the devtools UI | a dreamcast-devtools checkout |
| `debugger.regs.aica.channels` `debugger.regs.aica.rtc` | the sound generator (`hw/aica/sgc_if.cpp`) and the settings of the sound regression harness (`hw/aica/aica_hash.cpp`), compiled as the emulator compiles them, with the DSP core: the state of a channel that no register write sets, for the guest, for the debugger and in the samples, rendered sample by sample with the core's sample between the sweep and the mix; the two keys that pin the real-time clock | nothing |
| `debugger.symbols.table` `debugger.symbols.damaged` | `dbg_symbols.cpp`, the symbols of the ELF that reios loaded: the `.symtab` walk on images the test builds byte for byte, lookup by address and by name, `name` and `name+0x<offset>`, a symbol without a size, names with bytes above `0x7F` and control characters; and images that are cut, that are no ELF, or that name offsets outside of themselves, each in memory that ends where the image ends | nothing |

What the tests of the last column look for, and the option that says where it is:

* a dreamcast-devtools checkout with its packages installed (`node_modules`): `-DNULLDC_DEVTOOLS_DIR=<path>`,
  default the submodule `devtools/` of the repository, else `../dreamcast-devtools` next to it, the first that has
  what the test needs (an uninitialised submodule is an empty directory). A suite built alone has its own option for
  it (`-DDEVTOOLS_DIR=`, `-DDBG_NET_NODE_MODULES=`);
* the objdumps: `-DSH_OBJDUMP=<sh-elf-objdump>` and `-DARM_OBJDUMP=<arm-eabi-objdump>`; without the option they
  are looked for on the `PATH` and in the KallistiOS toolchain directories under `/opt/toolchains`;
* `node` and `python3` on the `PATH`.

A test that does not find its tool is skipped, and fails with `-DNULLDC_TESTS_REQUIRE_DATA=ON`, like a test
without its data.

`ctest -E '^debugger\.'` leaves them out, `ctest -R '^debugger\.net\.'` runs one suite.

* `debugger/disasm/README.md`: what is compared with objdump, the opcode table and the playground, and every
  difference that is accepted,
* `debugger/regs/README.md`: what the table tests check, and the tests of `regs/aica`, the one directory of these
  suites that takes the emulator's definitions and flags (`nulldc3/cmake/config.cmake`, for itself only),
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
behind it calls that function: it has the sixteen `debugger.sound.neutral.*` tests and no program of its own.
