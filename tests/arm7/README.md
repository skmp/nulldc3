# ARM7 tests

The sound CPU of the emulator, run on its own: `hw/arm7/arm7.cpp` (the interpreter), `arm7_interpreter.cpp` and
`SoundCPU.cpp` are compiled as they are, on 2 MB of wave RAM. The reset state, the memory map and the memory
access functions of the ARM (mirrors, misaligned word loads) are the emulator's. There is no AICA: behind the
register window is a plain register file that only knows the bit a guest raises to say it is done
(`arm7_machine.h`).

| test | what it runs | data |
|---|---|---|
| `arm7.vendor` | the 45 self-checking programs of arm7di-tests-dreamcast, from reset to their end marker: no error bit in r1 (`r1 & 0x3FFFFFFF == 0`) and the pinned r0-r14 and CPSR at the end | `tests/vendor/arm7di-tests-dreamcast` (submodule) |
| `arm7.wren7.vendor` | the same programs as console jobs: r1 and CPSR against the console's | wren7-rtl |
| `arm7.wren7.qemu_diff` | 4 x 1000 random instructions (data processing, MUL/MLA, LDR/STR, LDM/STM, SWP): registers, flags and memory of every case against the console's | wren7-rtl |
| `arm7.wren7.mulsem` | 1024 multiplier inputs: MUL(S)/MLA(S) results and flags, and the Rd == Rm forms, against the console's | wren7-rtl |
| `arm7.wren7.bringup` | a countdown kernel run three times in a row, and one that runs into its timeout: what a reset keeps | wren7-rtl |

## arm7.vendor

The programs are straight lines that collect the checks that failed in r1. A core that executes nothing slides
to the end marker with r1 = 0 as well, so a program has to show more than "no error bit":

* `programs`: the program gets to its end marker and has no error bit in r1,
* `final-state`: r0-r14 and CPSR at the end marker are the ones `baseline.txt` pins for the program (the `pin`
  rows). r1 with its bits 30 and 31 is what a Dreamcast answers too (`arm7.wren7.vendor`); the other registers
  are what the interpreter leaves today, so a fix that changes them (the reset state, `KNOWN_FAILURES.md` D7)
  needs `--update-baseline`,
* `control`: every program is run a second time on a core that skips every instruction, and that run must not
  pass the two checks above. A program for which it does fails the test whatever the baseline says, also when
  the baseline is being updated.

## The wren7 console suites

wren7-rtl has jobs that were run on a Dreamcast, with what the console answered. The `arm7.wren7.*` tests run
the jobs that need nothing but wave RAM on the interpreter and compare with the console's answer, not with
wren7's model. Nothing of wren7-rtl is in this repository; the tests read a checkout:

    cmake -S tests -B <build dir> -G Ninja -DNULLDC_WREN7_DIR=<path to wren7-rtl>

(the default is `../wren7-rtl` next to the repository). The jobs load ARM programs that wren7-rtl builds into
its `build/` directory, which is not part of a checkout:

    make -C <wren7-rtl>/model/hw/arm                               # build/hw/arm: stub.bin, k_hello.bin, k_mulsem.bin
    make -C <wren7-rtl>/model hw_suite                             # the tool for the next line
    cd <wren7-rtl>/model && ../build/model/hw_suite gen qemu_diff  # build/hw/qemu_diff: s1.bin .. s4.bin

The first and the last need the arm-eabi toolchain (`DC_ARM_BIN`, see `model/hw/arm/Makefile`). Without a
checkout, or without one of these build outputs, a test is reported as skipped and says what it did not find;
it fails instead with `-DNULLDC_TESTS_REQUIRE_DATA=ON` (`--require-data`). A checkout that lacks anything else
(a job file, the console's results or captures, its submodules) is not a reason to skip: the test fails.

A job (`wren7_job.h`) is a set of images and patched words in wave RAM. It runs from the release of the ARM
reset until the done stub raises MCIPD.SCPU; the stub saves the registers into a result block first. The
job's timeout is turned into an instruction budget (no instruction is shorter than 8 clocks of 22.5792 MHz), so
a job that timed out on the console has to run out of budget here. Only what a job defines is compared: the
console keeps the ARM registers over a reset, so the registers a job never writes hold what earlier jobs left.

## The baseline

`baseline.txt` has, per test and check, how many cases there are and how many pass, and the rows of words a
test pins. A test fails when

* a check passes fewer cases than its baseline, or has another number of cases,
* a check is missing from the baseline, or the baseline has a check or a pinned row that the run does not,
* guest code makes the emulator code crash (die(), verify(), a signal) or hang.

Passes above the baseline are reported; with `--strict`, as ctest runs the tests, they fail until the baseline
is updated. `KNOWN_FAILURES.md` explains every check that is below its number of cases. After a fix

    <build dir>/arm7/arm7_wren7 mulsem --update-baseline       # or: arm7_vendor --update-baseline

rewrites the rows of that test; commit the file with the fix and take the entry out of `KNOWN_FAILURES.md`.

## Looking at a failure

    arm7_vendor --program LDR_7                 # a part of the name, * and ? work
    arm7_wren7 qemu_diff --verbose              # every case that differs, not only the first of each check
    arm7_wren7 bringup --job hello_1 --emu-output
    arm7_wren7 vendor --wren7 <other checkout>

A run with `--program` / `--job` is not compared with the baseline, it passes when everything in it does.

## Adding a suite

`wren7_load_suite()` reads any `model/tests/hw/<suite>/jobs.txt` with its images and the console's results
(status, SH4 cycles per repeat, read regions); `wren7_run_job()` runs a job and returns the read regions, the
number of instructions and the cycles the interpreter counted for them (`Wren7Run::ticks`, the sum of what
`ARM7Backend::Step` returns). A further executable, for example one that holds the interpreter's cycle counts
against the console's timing kernels, links `nulldc_arm7_interp`, `nulldc_arm7_test` and `nulldc_test_common`
and counts its cases with `arm7_count()` (`arm7_suite.h`). Jobs that have the SH4 set up or read AICA registers,
or put it on the bus (`Wren7Job::needs_sh4`), and jobs that depend on the DSP, channels or interrupts need
more than this machine has.
