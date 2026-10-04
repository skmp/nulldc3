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

## Building and running

    cmake -S tests -B <build dir> -G Ninja
    ninja -C <build dir>
    ctest --test-dir <build dir> --output-on-failure

| test | what it runs |
|---|---|
| `common.selftest` | the harness on itself, and the two properties of `hw/mem/_vmem` the suites lean on (unaligned accesses end in the handler, constant address lookups return the handlers that are mapped) |
| `sh4.singlestep.interp` | every single-step vector through the SH4 interpreter, gated on `sh4_singlestep/baseline.txt` |

## Layout

    common/            shared: libswirl.cmake (nulldc_add_libswirl_library, NULLDC_SH4_INTERP_SOURCES),
                       test_guard (die()/verify() trap, fatal signals, watchdog), sh4_env + sh4_stubs*
                       (what the SH4 core needs instead of an emulator), ss_* (vector reader, guest memory,
                       policy, state, suite runner), selftest
    sh4_singlestep/    the vectors through the interpreter
    sh4_dynarec/       the vectors through the dynarec
    arm7/              ARM7 tests
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
filtered by reason, then the totals and, for every file with vectors that are not an exact pass, what its policy
says about them.

The FPU results in the vectors are host arithmetic. They are an oracle for the `fast` timing tier only, not for
an exact FPU.

Things the vectors do that devcast currently does as well, and that pass for that reason: FPSCR is loaded with
its reserved bits (LDS / LDS.L to FPSCR), the delay slot of RTE runs on the register bank of the old SR, SR.RB is
forced to 0 when SR.MD=0, SLEEP is executed again until an interrupt arrives.

### The baseline

`sh4_singlestep/baseline.txt` has, per file, how many vectors are run, pass exactly and pass leniently. The test
fails when

* a file passes fewer vectors than its baseline (exact or lenient), or runs fewer,
* a vector crashes (die(), verify(), a signal, an unaligned access) in a file for which neither the policy
  (`SSP_MAY_CRASH`) nor the suite (`SsSuite::known`) announces it, or a vector hangs,
* a vector file is missing from the policy table or from the baseline, or the baseline lists a file that is
  not there.

Counts above the baseline are reported. Run by hand they do not fail; ctest runs every test with `--strict`,
where they do: a baseline that is behind lets a later regression back to its counts pass. After a fix:

    <build dir>/sh4_singlestep/sh4_singlestep_interp --update-baseline

rewrites the file; commit it with the fix. A change of the policy (a new filter reason, a width) shifts the
counts as well and needs the same.

### Looking at a failure

    sh4_singlestep_interp --file 0000nnnn11000011 --verbose      # a part of the file name ...
    sh4_singlestep_interp --file fipr --vector 18 --verbose      # ... or of the instruction; * and ? work
    sh4_singlestep_interp --emu-output ...                       # do not hide what the emulator code prints
    sh4_singlestep_interp --unfilter fpu-disabled,privileged     # run filtered vectors as well (or: all)

`--verbose` prints, for every vector that is not an exact pass, the opcodes, the expected and the actual bus
activity and the registers before, expected and actual.

## Adding a suite

A suite is a directory with a `CMakeLists.txt` that is added in `tests/CMakeLists.txt`. From `common` it gets

* `nulldc_add_libswirl_library(<target> [SHREC|AREC|DSPREC <DYNAREC_*>] SOURCES <files relative to libswirl>)`:
  an object library of libswirl translation units, built like the emulator builds them.
* `nulldc_test_common`: link it after the libswirl library. Stubs are taken from it only for the symbols the
  libswirl objects leave undefined; `msgboxf` and `os_DebugBreak` always come from `test_guard`.
* `NULLDC_HAVE_SH4_TESTS` / `NULLDC_HAVE_ARM7_TESTS` / `NULLDC_HAVE_WREN7`, the `*_DIR` and `*_MISSING`
  variables and `nulldc_add_skipped_test(<name> <reason>)` for data that is not there.

A second way to run the SH4 vectors only has to fill an `SsSuite` (`common/ss_suite.h`): how the 4 instructions
of a loaded vector are executed, whether instruction fetches are part of the expected bus order, and what that
way of executing is known to fail on top of the policy table. It gets its own baseline file.
`sh4_singlestep/main.cpp` is the whole interpreter suite.
