# Tests

A standalone CMake project. Every suite but one compiles the libswirl translation units it tests (with the
emulator's own definitions and flags, from `reicast/cmake/config.cmake`) and links them against stubs for the
rest. The system tests run the emulator itself, which the project builds for them.

## Getting the vectors

The test data are git submodules:

    git submodule update --init tests/vendor/sh4-tests                 # SingleStepTests/sh4, 233 x 500 vectors, ~90 MB
    git submodule update --init tests/vendor/arm7di-tests-dreamcast    # 45 ARM7 test binaries

A suite whose data is missing is still built; its test is reported as skipped and configure says which
submodule to fetch. Other checkouts can be used with `-DNULLDC_SH4_TESTS_DIR=`, `-DNULLDC_ARM7_TESTS_DIR=` and
`-DNULLDC_WREN7_DIR=` (wren7-rtl, default `../wren7-rtl` next to the repository if it is there).

The system tests that boot a KallistiOS program take the programs by path: `-DNULLDC_ELF_DIR=<directory with
2ndmix.elf and hello.elf>`. Without it those tests are skipped.

A skipped test has not run and is not a pass: `ctest` exits with 0 when everything that did run passes, also
when that is only the tests that need no data. With `-DNULLDC_TESTS_REQUIRE_DATA=ON` missing data fails its
tests instead (the submodules, the wren7-rtl checkout and its build outputs, the programs of `NULLDC_ELF_DIR`
alike). CI should configure that way.

## Building and running

    cmake -S tests -B <build dir> -G Ninja
    ninja -C <build dir>
    ctest --test-dir <build dir> --output-on-failure

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
| `arm7.vendor` | the 45 self-checking programs of arm7di-tests-dreamcast on the ARM7 interpreter: no error bit, the pinned final registers, and a core that executes nothing must fail | `vendor/arm7di-tests-dreamcast` |
| `arm7.directed` | hand-written cases: ADC, SBC and RSC at the edges with the carry clear and set, the logical operations with a ROR by a register, MUL and MLA with Rd == Rm and C behind their S forms, SWI and the undefined instruction trap with their return, the FIQ handshake of the AICA, also with an ARMRST in the middle of it, and what SCILV0-2, the register behind them and M read back, what a reset of the ARM writes and what it leaves, what a sound CPU holds before its first reset and what a reset of the machine leaves of the interrupt controller | none |
| `arm7.wren7.vendor` `arm7.wren7.qemu_diff` `arm7.wren7.mulsem` `arm7.wren7.bringup` | console jobs of wren7-rtl on the ARM7 interpreter, against what a Dreamcast answered | a wren7-rtl checkout with its ARM programs built |
| `cfg.overrides` | the config store and the command line parser on a config file of their own: a value `-config` gave is in effect for the run and is not what a save of the settings writes to `emu.cfg`, a changed one is written, and a read-only store (the automated runs) writes nothing | none |
| `runctl.guest_exit.*` | `utils/run_control.cpp` on the config store, with stand-ins for the machine and the user interface, one process per run (`window`: no key, `key`: `Exit.OnGuestExit`, `vblanks`: `Exit.AfterVblanks`, `seconds`: `Exit.AfterSeconds`, `headless`): a guest program that returns to the BIOS ends every run but the first, with `reason=guest-exit` on the exit line and the user interface stopped, and leaves the first to its window | none |
| `system.*` | the emulator binary of the default build, headless: `-help`, the keys that end a run and the line it prints, the pinned RTC, `emu.cfg` left alone, what a fatal fault does, programs that boot under reios on the JIT and on the interpreter with their serial output, the software renderer into a PNG, a run that is repeated and has to end in the same state hash every time, and the speed script of `tests/perf` on one of them (`system/README.md` has the list, and what a deterministic run takes) | none, or the programs of `NULLDC_ELF_DIR` |
| `system.hle.*` | the same on the emulator built with `-DHAS_TA_LLE=OFF` | none, or the programs of `NULLDC_ELF_DIR` |

The SH4 dynarec tests run on x86-64 hosts only. The wren7 tests need `-DNULLDC_WREN7_DIR=<path to wren7-rtl>`
unless wren7-rtl sits next to the repository; `arm7/README.md` has what has to be built in it. The system tests
run on Linux hosts with python3.

Every test but `common.selftest`, `sh4.core`, `cfg.overrides`, `runctl.guest_exit.*` and the system tests is gated
on a baseline file, and ctest runs them with `--strict`: a count that is worse than its baseline fails, and so does
one that is better, until the baseline is updated and committed. `common.selftest`, `sh4.core`, `cfg.overrides`,
each `runctl.guest_exit.*` test and each system test are lists of checks that all have to hold.

More about the suites:

* this file: the single-step vectors, the filter, the baseline and the directed vectors,
* `sh4_dynarec/README.md`: how the dynarec is run, the differential, the native count;
  `sh4_dynarec/KNOWN_DIFFERENCES.md`: what is behind the two dynarec baselines,
* `arm7/README.md`: the ARM7 tests and the wren7 console suites; `arm7/KNOWN_FAILURES.md`: why checks of the
  ARM7 baseline are below their number of cases, and what else is known to be wrong in the ARM7,
* `system/README.md`: the system tests, which emulator they run and how, their guest programs.

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
    vendor/            the submodules
    files/             fixtures

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
* `NULLDC_HAVE_SH4_TESTS` / `NULLDC_HAVE_ARM7_TESTS` / `NULLDC_HAVE_WREN7`, the `*_DIR` and `*_MISSING`
  variables and `nulldc_add_missing_data_test(<name> <reason>)` for data that is not there (skipped, or failed
  with `NULLDC_TESTS_REQUIRE_DATA`); `nulldc_add_skipped_test(<name> <reason>)` for a test the host cannot run.

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
