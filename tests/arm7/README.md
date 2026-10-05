# ARM7 tests

The sound CPU of the emulator, run on its own: `hw/arm7/arm7.cpp` (the interpreter), `arm7_interpreter.cpp` and
`SoundCPU.cpp` are compiled as they are, on 2 MB of wave RAM, with the register block of the AICA
(`hw/aica/aica_mmio.cpp`) behind the register window. The reset state, the memory map and the memory access
functions of the ARM (mirrors, misaligned word loads), the interrupt and timer registers and the interrupt
controller in front of nFIQ are the emulator's. The channels and the DSP are not there, their registers keep
what is written to them, and a sample passes only when a test says so (`arm7_machine.h`).

| test | what it runs | data |
|---|---|---|
| `arm7.vendor` | the 45 self-checking programs of arm7di-tests-dreamcast, from reset to their end marker: no error bit in r1 (`r1 & 0x3FFFFFFF == 0`) and the pinned r0-r14 and CPSR at the end | `tests/vendor/arm7di-tests-dreamcast` (submodule) |
| `arm7.directed` | hand-written cases: ADC, SBC and RSC on operands at the edges with the carry clear and set (63 single instructions), the logical operations with a ROR by a register amount of 0, of 32 and around them, MUL and MLA with Rd == Rm and C behind their S forms, SWI and the undefined instruction trap with their return, the FIQ handshake of the AICA, also with an ARMRST in the middle of it, and what SCILV0-2 read back, what a reset writes and what it leaves, what a sound CPU holds before its first reset and what a reset of the machine leaves of the interrupt controller | none |
| `arm7.wren7.vendor` | the same programs as console jobs: r1 and CPSR against the console's | wren7-rtl |
| `arm7.wren7.qemu_diff` | 4 x 1000 random instructions (data processing, MUL/MLA, LDR/STR, LDM/STM, SWP): registers, flags and memory of every case against the console's | wren7-rtl |
| `arm7.wren7.mulsem` | 1024 multiplier inputs: MUL(S)/MLA(S) results and flags, and the Rd == Rm forms, against the console's | wren7-rtl |
| `arm7.wren7.bringup` | a countdown kernel run three times in a row, and one that runs into its timeout: what a reset keeps | wren7-rtl |

## arm7.vendor

The programs are straight lines that collect the checks that failed in r1. A core that executes nothing slides
to the end marker with r1 = 0 as well, so a program has to show more than "no error bit":

* `programs`: the program gets to its end marker and has no error bit in r1,
* `final-state`: r0-r14 and CPSR at the end marker are the ones `baseline.txt` pins for the program (the `pin`
  rows). r1 with its bits 30 and 31 and the CPSR are what a Dreamcast answers too (`arm7.wren7.vendor`); the
  other registers are what the interpreter leaves today, on a machine that is switched on for every program
  (r13 is the stack pointer `SoundCPU_impl::Reset` loads), so a fix that changes them needs `--update-baseline`,
* `control`: every program is run a second time on a core that skips every instruction, and that run must not
  pass the two checks above. A program for which it does fails the test whatever the baseline says, also when
  the baseline is being updated.

## arm7.directed

The random instructions of the console suites have no ADC whose carry out comes from the carry in
(0xFFFFFFFF + 0 + C), and the vendor programs have one ADC, SBC and RSC each: an ADCS that computes its carry
without the carry in passes all of them. `directed_main.cpp` has tables of single instructions (register and
immediate operand, with and without S) with the operands, the flags going in and what Rd and N, Z, C, V have
to be afterwards, written down from the definition of the instruction and not taken from an emulator. A new
case is a row in a table and `arm7_directed --update-baseline`.

Three of the 4000 random instructions have a ROR by a register whose low byte is 0 with a C that shows what
becomes of it: a TST and two TEQ. The check `ror` has that amount, 32, and amounts around them for every logical
operation: operand 2 and the carry of the shifter are the data sheet's (4.4.2), and C behind an amount of 0 is
what the console left in those three.

The multiplier probe of the console (`arm7.wren7.mulsem`) has Rd == Rm for MULS and for MLA. The check
`mul-rd-rm` has the four forms: MUL and MULS leave 0, MLA and MLAS the word the console has for MLA with those
operands (the rows are records of the probe), the S forms with N and Z of what they leave.

The probe never reads the flags behind an S form with Rd == Rm, so no capture has their C. The check
`mul-rd-rm-c` pins what the interpreter leaves in it, and is not measured: the bit the barrel shifter drops in
the last step of the multiplier, as the console has it for Rd != Rm, of Rd as that step finds it, which is the
operand of every step when Rd == Rm (`KNOWN_FAILURES.md`, D4).

None of the 55 console jobs that run here returns from an exception. The check `trap` has SWI and the
undefined instruction trap with `movs pc, lr` in their vector, one trap and 8 in a row: R14 of the mode the
trap enters is the address of the instruction behind it, and the return goes on there. That is the program
of wren7-rtl's timing kernels `swi` and `und`, whose time on the console is that of 8 traps and 8 returns per
iteration (`model/tests/hw/timing/SUMMARY.txt`).

None gets an interrupt either. The checks `fiq` and `scilv` drive the interrupt controller in front of nFIQ
through the registers a guest has for it, with the one sample interval as the request: a request of level 0
does not interrupt, level 1 and 7 do and L reads the level; reading L releases nFIQ and nothing else does;
writing bit 0 of M lets the next request in and nothing else does; L reads 0 behind a job whose requests had
level 0; SCILV0-2 read back as 0. What has to happen is what a Dreamcast did in wren7-rtl's `fiqdiag` jobs
(`model/tests/hw/fiqdiag/hw`, `model/NOTES.md` "Interrupts"); the comment in `directed_main.cpp` names the
capture behind each rule.

A fourth case of `scilv` is about where the registers that read 0 end: MCIEB, the register behind SCILV2, reads
back what was written to it. No capture reads MCIEB, the case holds what the emulator did before SCILV0-2 read
as 0.

Two cases of `fiq` have no capture behind them. They hold and release ARMRST in the middle of a handler, between
its read of L and its write of M, and in front of the read of L: the program behind the first, which writes M in
its handler only as the ARM crt0 of KallistiOS does, has to get an interrupt with every sample, and the one
behind the second is interrupted as soon as it turns FIQs on. `KNOWN_FAILURES.md` (D5) has why.

The jobs that run here are reset in SVC mode, with the flags their kernel leaves. The check `reset` holds and
releases ARMRST in FIQ, SVC, IRQ and user mode with other flags: R14_svc and SPSR_svc take the PC and the CPSR,
the CPSR gets SVC mode with I and F set, the PC is 0, and N, Z, C and V, the other registers and the bank of the
mode that is left stay (data sheet 3.5). `d4.bin` of `fiqdiag` is a job that was reset out of FIQ mode: the
first case has the CPSR that reset found.

The emulator resets the AICA in front of the sound CPU, and the reset of the AICA releases ARMRST. The check
`power-on` is about what that first release finds: a sound CPU that was created and not reset yet has to hold
what its reset gives it, with the ARM held in reset (`arm7m_created_reset()`). Its other cases reset the machine
in the middle of a FIQ handler, as a guest can with SB_SFRES: nothing of the interrupt controller is left
(`arm7m_controller_idle()`), and the program that is started next gets the interrupts it asks for and no other.

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
Here the machine is switched on for every job, and the registers are kept from repeat to repeat of a job.

## The baseline

`baseline.txt` has, per test and check, how many cases there are and how many pass, and the rows of words a
test pins. A test fails when

* a check passes fewer cases than its baseline, or has another number of cases,
* a check is missing from the baseline, or the baseline has a check or a pinned row that the run does not,
* guest code makes the emulator code crash (die(), verify(), a signal) or hang.

Passes above the baseline are reported; with `--strict`, as ctest runs the tests, they fail until the baseline
is updated. `KNOWN_FAILURES.md` explains every check that is below its number of cases. After a fix

    <build dir>/arm7/arm7_wren7 mulsem --update-baseline       # or: arm7_vendor / arm7_directed --update-baseline

rewrites the rows of that test; commit the file with the fix and take the entry out of `KNOWN_FAILURES.md`.

## Looking at a failure

    arm7_vendor --program LDR_7                 # a part of the name, * and ? work
    arm7_directed --check sbc
    arm7_wren7 qemu_diff --verbose              # every case that differs, not only the first of each check
    arm7_wren7 bringup --job hello_1 --emu-output
    arm7_wren7 vendor --wren7 <other checkout>

A run with `--program` / `--job` / `--check` is not compared with the baseline, it passes when everything in it
does.

## Adding a suite

`wren7_load_suite()` reads any `model/tests/hw/<suite>/jobs.txt` with its images and the console's results
(status, SH4 cycles per repeat, read regions); `wren7_run_job()` runs a job and returns the read regions, the
number of instructions and the cycles the interpreter counted for them (`Wren7Run::ticks`, the sum of what
`ARM7Backend::Step` returns). A further executable, for example one that holds the interpreter's cycle counts
against the console's timing kernels, links `nulldc_arm7_interp`, `nulldc_arm7_test` and `nulldc_test_common`
and counts its cases with `arm7_count()` (`arm7_suite.h`). Jobs that have the SH4 set up or read AICA registers,
or put it on the bus (`Wren7Job::needs_sh4`), and jobs that depend on the DSP or the channels need more than
this machine has. For jobs that take interrupts the machine has `arm7m_sample()`; `wren7_run_job()` does not
let samples pass.
