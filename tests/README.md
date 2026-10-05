# Tests

A standalone CMake project. It does not build the emulator: every suite compiles the libswirl translation
units it tests (with the emulator's own definitions and flags, from `reicast/cmake/config.cmake`) and links them
against stubs for the rest.

## Getting the vectors

The test data are git submodules:

    git submodule update --init tests/vendor/sh4-tests                 # SingleStepTests/sh4, 233 x 500 vectors, ~90 MB
    git submodule update --init tests/vendor/arm7di-tests-dreamcast    # 45 ARM7 test binaries

A suite whose data is missing is still built; its test is reported as skipped and configure says which
submodule to fetch. Other checkouts can be used with `-DNULLDC_SH4_TESTS_DIR=`, `-DNULLDC_ARM7_TESTS_DIR=` and
`-DNULLDC_WREN7_DIR=` (wren7-rtl, default `../wren7-rtl` next to the repository if it is there).

A skipped test has not run and is not a pass: `ctest` exits with 0 when everything that did run passes, also
when that is only the tests that need no data. With `-DNULLDC_TESTS_REQUIRE_DATA=ON` missing data fails its
tests instead (the submodules, the wren7-rtl checkout and its build outputs alike). CI should configure that
way.

## Building and running

    cmake -S tests -B <build dir> -G Ninja
    ninja -C <build dir>
    ctest --test-dir <build dir> --output-on-failure

| test | what it runs | data |
|---|---|---|
| `common.selftest` | the harness on itself, and the properties of `hw/mem/_vmem` the suites lean on (unaligned accesses end in the handler, constant address lookups return the handlers that are mapped and their context) | none |
| `sh4.singlestep.interp` | every single-step vector through the SH4 interpreter, gated on `sh4_singlestep/baseline.txt` | `vendor/sh4-tests` |
| `sh4.singlestep.dynarec` | every single-step vector through the SH4 dynarec (decoder, shil, x64 backend), gated on `sh4_dynarec/baseline_singlestep.txt` | `vendor/sh4-tests` |
| `sh4.differential` | interpreter against dynarec from the initial state of every vector, gated on `sh4_dynarec/baseline_differential.txt` | `vendor/sh4-tests` |
| `sh4.directed.interp` `sh4.directed.dynarec` `sh4.directed.differential` | the three above on the hand-written vectors of `sh4_directed` (carry, borrow, overflow, compares at the edges, delayed branches with a delay slot that changes what the branch uses) | none |
| `arm7.vendor` | the 45 self-checking programs of arm7di-tests-dreamcast on the ARM7 interpreter: no error bit, the pinned final registers, and a core that executes nothing must fail | `vendor/arm7di-tests-dreamcast` |
| `arm7.directed` | hand-written cases: ADC, SBC and RSC at the edges with the carry clear and set, the logical operations with a ROR by a register, MUL and MLA with Rd == Rm, SWI and the undefined instruction trap with their return, the FIQ handshake of the AICA and what SCILV0-2 read back, what a reset of the ARM writes and what it leaves, what a sound CPU holds before its first reset and what a reset of the machine leaves of the interrupt controller | none |
| `arm7.wren7.vendor` `arm7.wren7.qemu_diff` `arm7.wren7.mulsem` `arm7.wren7.bringup` | console jobs of wren7-rtl on the ARM7 interpreter, against what a Dreamcast answered | a wren7-rtl checkout with its ARM programs built |

The SH4 dynarec tests run on x86-64 hosts only. The wren7 tests need `-DNULLDC_WREN7_DIR=<path to wren7-rtl>`
unless wren7-rtl sits next to the repository; `arm7/README.md` has what has to be built in it.

Every test but `common.selftest` is gated on a baseline file, and ctest runs them with `--strict`: a count
that is worse than its baseline fails, and so does one that is better, until the baseline is updated and
committed.

More about the suites:

* this file: the single-step vectors, the filter, the baseline and the directed vectors,
* `sh4_dynarec/README.md`: how the dynarec is run, the differential, the native count;
  `sh4_dynarec/KNOWN_DIFFERENCES.md`: what is behind the two dynarec baselines,
* `arm7/README.md`: the ARM7 tests and the wren7 console suites; `arm7/KNOWN_FAILURES.md`: why checks of the
  ARM7 baseline are below their number of cases, and what else is known to be wrong in the ARM7.

## Layout

    common/            shared: libswirl.cmake (nulldc_add_libswirl_library, NULLDC_SH4_INTERP_SOURCES),
                       test_guard (die()/verify() trap, fatal signals, watchdog), sh4_env + sh4_stubs*
                       (what the SH4 core needs instead of an emulator), ss_* (vector reader, guest memory,
                       policy, state, suite runner), selftest
    sh4_singlestep/    the vectors through the interpreter
    sh4_dynarec/       the vectors through the dynarec, and the interpreter against the dynarec (README.md,
                       KNOWN_DIFFERENCES.md)
    sh4_directed/      hand-written vectors for all of the above
    arm7/              the ARM7 interpreter: vendor programs, directed cases, wren7 console suites (README.md,
                       KNOWN_FAILURES.md)
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
| `trapa-nop` | TRAPA, recorded as a nop |
| `host-nan` | the instruction produces a NaN, or converts one with FTRC: the expected value is the host's |

Nothing else is skipped. The report has one line per file: total / run / pass-exact / pass-lenient / failed /
filtered by reason, and a line for its notified vectors if it has any, then the totals and, for every file with
vectors that are not an exact pass, what its policy says about them.

The FPU results in the vectors are host arithmetic. They are an oracle for the `fast` timing tier only, not for
an exact FPU.

Things the vectors do that devcast currently does as well, and that pass for that reason: FPSCR is loaded with
its reserved bits (LDS / LDS.L to FPSCR), the delay slot of RTE runs on the register bank of the old SR, SR.RB is
forced to 0 when SR.MD=0, SLEEP is executed again until an interrupt arrives.

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
delayed jump or call takes where it goes, and a call writes PR, in front of its delay slot. The file has (9
vectors)

* RTS with `lds r4,pr` in the delay slot: it returns to the PR it is entered with,
* JMP @r3, BRAF r3 and BSRF r3 with `mov r4,r3` in the delay slot: they go by the r3 they are entered with,
* JSR @r3, BSR and BSRF r3 with `sts pr,r5` in the delay slot: r5 gets the return address of the call.

The interpreter does not pass the last three: it writes PR behind the delay slot. The dynarec passes them, and
the differential has the difference; `sh4_dynarec/KNOWN_DIFFERENCES.md` has more on it.

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
