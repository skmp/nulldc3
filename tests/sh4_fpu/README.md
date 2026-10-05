# SH4 FPU against the hardware

Two tests that run single FPU instructions through the SH4 interpreter and through the SH4 dynarec (x64 backend)
and compare what each leaves with what a Dreamcast leaves, bit for bit:

| test | instructions | the hardware's result |
|---|---|---|
| `sh4.fpu.scalar` | FADD FSUB FMUL FDIV FSQRT FTRC FCMP/EQ FCMP/GT FABS FNEG in single and double precision, FMAC, FSRRA, FCNVDS, FCNVSD | 225,036 records taken on a console |
| `sh4.fpu.vector` | FIPR FTRV FSCA FSRRA | shrike4-rtl's bit-exact model on 1,355,584 generated cases, and 15,642 records taken on a console |

They are the fast tier's FPU baseline: devcast computes with the host's FPU, and the baselines say how much of
the hardware's behaviour that gives, per kind of instruction, setting and operand. `KNOWN_FAILURES.md` has the
reason for everything that is below 100 %.

`sh4.fpu.scalar.canonical` and `sh4.fpu.vector.canonical` are the two tests on another build of the dynarec, see
"The canonical helpers" below.

## The data

Nothing of the hardware's results is in this repository. The tests read them from a shrike4-rtl checkout, and
compile three of its headers in from there:

    cmake -S tests -B <build dir> -G Ninja -DNULLDC_SHRIKE4_DIR=<path to shrike4-rtl>

The default is `shrike4-rtl` next to the repository, if it is there. Without a checkout the tests are not built
and are reported as skipped, with `-DNULLDC_TESTS_REQUIRE_DATA=ON` they fail. A checkout that lacks one of the
data files is found when a test runs: it says what is missing and is skipped, or fails with `--require-data`
(which is what `NULLDC_TESTS_REQUIRE_DATA` adds to the ctest command).

| path in shrike4-rtl | what |
|---|---|
| `tools/hw/fpdn/fpdn_vec.h` (compiled in) | the instructions, FPSCR settings and operand generators of the console program of the scalar records: the operands of a record follow from its position |
| `tools/hw/fpdn/fpdn_p1.bin` `fpdn_p2.bin` `fpdn_p3.bin` | the scalar records: FR0-FR3, FPUL, FPSCR and T behind the instruction, and the exception code |
| `model/sh4_fpu_approx.h` (compiled in) | the model of FIPR, FTRV, FSCA and FSRRA: values, FPSCR, exceptions |
| `model/fipr/kos_fpu_dump/fsca_rnrz.bin` | sine and cosine of all 65,536 angles in both rounding modes, from a console |
| `model/fipr/hw_specials/fipr_specials.txt` `ftrv_specials.txt`, `ftrv_specials_vectors.h` (compiled in) | FIPR and FTRV on special operands under 34 and 23 FPSCR settings, from a console |
| `model/fsrra/hw_specials/fpu_specials.txt` `fpu_specials2.txt` | FSRRA and FSCA likewise |
| `model/fipr/kos_fipr_test/fipr_test.out` | results of FIPR, FSRRA and FSCA in both rounding modes, from a console |

The tests need an x86-64 host: the dynarec is entered the way `sh4.singlestep.dynarec` enters it
(`sh4_dynarec/dynarec_runner.h`), and the host's control word is read back as MXCSR.

## How a case is run

A case is an opcode, the FP registers (FR0-FR15, XF0-XF15), FPUL, FPSCR and T in front of it, the same behind
it on the hardware, and whether the hardware raises the FPU exception. Registers that an instruction does not
read hold sentinels, so an instruction that writes a register it should not write is seen.

Both engines run the instruction and a `nop` behind it from the same state (the decoder cannot end a block
on an FPU instruction):

* the interpreter as `SH4IInterpreter::Loop` does: fetch through the memory handlers, `ExecuteOpcode`,
* the dynarec as the other dynarec tests do: `dec_DecodeBlock`, `AnalyseBlock`, the x64 backend's `Compile`,
  and the block is entered once. It is compiled for every case, with the case's FPSCR.

FPSCR is written the way `LDS Rm,FPSCR` writes it: the value, then `UpdateFPSCR()`, which loads the host's
rounding mode and denormal handling (`SetFloatStatusReg`). The host is part of the result, so it is checked:

* when a test starts, the four settings of RM and DN are loaded from a cache that knows nothing. The host has to
  round 1/3 the way RM says and must have its FP exceptions masked. The report prints the four MXCSR values.
* around every case, in front of each engine and behind it, MXCSR is read back and has to be the value
  `SetFloatStatusReg` loads for the case's FPSCR. One that is not fails the test.

There is no guest memory: 16 bit reads return the opcode or `nop`, any other access ends the case.

## What is compared

| column | |
|---|---|
| `total` | cases of the group |
| `trap` | of them: the hardware raises the FPU exception (EXPEVT 0x120) and writes no result |
| `value-i` `value-d` | interpreter / dynarec: FR0-FR15, XF0-XF15, FPUL and T are the hardware's, and the engine went on to the next instruction. For a trap case: and the engine did **not** go on to the next instruction (it has to raise the exception; PC is what the tests look at) |
| `flags-i` `flags-d` | FPSCR behind the instruction is the hardware's, all 32 bits. The cause and flag fields are what an instruction changes |
| `agree` | interpreter and dynarec leave the same registers, FPSCR and PC, or end in the same `die()` |
| `native` | the dynarec compiled the instruction to native code. With PR = 1, and for FCNVDS and FCNVSD, its block calls the interpreter's handler: the dynarec's counts are then the interpreter's |
| `crash` | an engine ends in `die()`, `verify()` or a signal |

A trap case can only pass `value` once devcast raises the FPU exception. Today it has none.

## The groups

`sh4.fpu.scalar`: `<instruction>.<dn0|dn1>.<rn|rz>.<class>`, 380 groups. The console's records are of DN = 0
in both rounding modes and of DN = 1 with round to nearest; the records with exception enables and with
preset cause and flag fields are in `dn0.rn`. The class is the first of these that one of the operands is:
`nan`, `denormal`, `inf`, `zero`, `normal`. Records in which the console raised the FPU exception are in
`<instruction>.<dn>.<rm>.trap` instead.

`sh4.fpu.vector`, 99 groups:

* `<instruction>.dn1.<rn|rz>.<class>`: generated operands with FPSCR = 0x00040000 and 0x00040001, the
  settings games run with. The operands of a case depend on the instruction, the class and the case's number
  only (`vector_cases.cpp`), and the register numbers of the instruction change from case to case.

  | instruction | classes (cases per rounding mode) |
  |---|---|
  | FIPR | `normal` 64,000, `vec3` 24,000 (4th product zero), `single` 24,000 (one product), `self` 16,000 (FIPR FVn,FVn), `exact` 16,000 (7 bit numbers), `cancel` 16,000, `wide` 16,000 (exponents far apart), `overflow` 8,000, `underflow` 8,000, `zero` 4,000, `denormal` 4,000, `inf` 4,000, `nan` 4,000 |
  | FTRV | `normal` 64,000, `affine` 48,000 (a transform as games set it up, times (x, y, z, 1)), `diagonal` 16,000, `exact` 16,000, `cancel` 16,000, `wide` 16,000, `overflow` 8,000, `underflow` 8,000, `zero` 4,000, `denormal` 4,000, `inf` 4,000, `nan` 4,000 |
  | FSCA | `angle` 65,536 (every angle), `upper` 8,192 (something in FPUL[31:16]) |
  | FSRRA | `normal` 128,000, `one-to-four` 32,000, `simple` 16,000 (short fractions), `zero` 32, `denormal` 4,000, `negative` 4,000, `inf` 32, `nan` 4,000 |

* `<instruction>.console.values.<rn|rz>`: the console's records that are results alone (`fipr_test.out`).
* `<instruction>.console.specials.<dn1|dn0|pr1>` and `...<setting>.trap`: the console's records with FPSCR and
  the exception, by the FPSCR in front of the instruction. With PR = 1 the four instructions do nothing on
  the hardware (but still raise the exception when its enable is set).

### The model has to be the console

In `sh4.fpu.vector` the hardware's result is what `sh4_fpu_approx.h` computes, so the test is only as good as
the way it calls the model: which of FIPR's two vectors is which (the hardware treats them differently),
how the matrix is laid out, which register gets the result. `vector_reference()` in `vector_cases.cpp` is the
one place that calls it. Before anything is run, it has to give the console's answer for every console record:
the 15,642 of the text files (value, FPSCR and exception where a record has them) and the 131,072 results of
`fsca_rnrz.bin`. The report has a line per file. A record that is not answered as the console answered, or a
file with another number of records than it is known to have, fails the test whatever the baseline says, and
the baseline cannot be written.

The same holds for the emulator's FSCA table (`sin_table`, `hw/sh4/sh4_rom.cpp`): it is compared, entry by entry,
with the round to nearest half of `fsca_rnrz.bin`, and has to be the console's sine and cosine for all 65,536
angles.

## The baselines

`baseline_scalar.txt` and `baseline_vector.txt` have the counts of every group. A test fails when

* a group has fewer cases than its baseline in `value-i`, `value-d`, `flags-i`, `flags-d`, `agree` or
  `native`, or more in `crash`,
* a group has another `total` or `trap` than its baseline, is not in the baseline, or is in the baseline and
  has no cases,
* a case hangs, the emulator code raises a notification (`msgboxf`), the host's control word is not the
  emulator's, or the model does not answer as the console did. No baseline covers these.

Counts that are better than the baseline are reported. Run by hand they do not fail; ctest runs both tests
with `--strict`, where they do, until the baseline is updated and committed with the fix:

    <build dir>/sh4_fpu/sh4_fpu scalar --update-baseline
    <build dir>/sh4_fpu/sh4_fpu vector --update-baseline

and the entry of what was fixed comes out of `KNOWN_FAILURES.md`.

The x64 backend picks two pieces of its code by the host CPU: FMAC is `vfmadd231ss` or, without FMA, a call of
the helper the interpreter's FMAC is, and FIPR adds with `haddps` or, without SSE3, with `shufps` and `addps` in
the same order. The results are the same, so the baselines are those of any x86-64 host. The report's third line
says what the host has: a test run covers the code of that host only.

## The canonical helpers

The shil opcodes that compute have a canonical helper, a C function in `hw/sh4/dyna/shil_canonical.h`. A backend
calls it for an opcode it has no code of its own for: the x64 backend for FMAC on a CPU without FMA, and a build
without a JIT (`TARGET_NO_JIT`, the `cpp` backend) for all of them. On an x86-64 host with FMA the two tests above
do not reach the helpers of FADD to FDIV, FSQRT, FMAC, FSRRA, FTRC, FIPR and FTRV: the backend has code for each.

`sh4_fpu_canonical` is `sh4_fpu` linked with the x64 backend built with `CANONICAL_TEST`: it compiles the calls of
the interpreter, the jumps, the moves and the memory accesses itself and calls the helper for every other opcode,
the FPU instructions among them.
`sh4.fpu.scalar.canonical` and `sh4.fpu.vector.canonical` run it against the same two baselines, with `--strict`:
the helpers have to leave what the backend's own code leaves, in every group. A fix that moves a baseline has to
move the helper as well, there is no second baseline to update.

    <build dir>/sh4_fpu/sh4_fpu_canonical scalar --name sh4.fpu.scalar.canonical
    <build dir>/sh4_fpu/sh4_fpu_canonical vector --name sh4.fpu.vector.canonical --except FSCA

`--except FSCA` leaves the 11 FSCA groups out: the x64 backend cannot call that helper (`KNOWN_FAILURES.md`, 8.).
The report says how many groups an exception leaves out, and an exception that matches no group fails the run.
`--name` is what the report calls the test.

## Looking at a failure

    sh4_fpu scalar --group FMUL.dn1 --first        # a part of a group's name, * and ? work
    sh4_fpu vector --group FIPR.dn1.rn.cancel --verbose
    sh4_fpu scalar --why

* `--first` prints, under each group, the first case that is not as on the hardware in value and in flags, per
  engine, and the first case in which the engines differ. Without it they are printed for the groups that
  fail the gate.
* `--verbose` prints every such case: where it comes from (the part and number of a record with the names of
  its palette operands, the class and number of a generated case, file and line of a console record), the
  operands, and the registers that differ.
* `--why` counts, per group and over all of them, the reason every such case has. For a wrong value the test
  looks at the operands and at what the engine left, and where it helps runs the engine again on other
  operands: a case counts as "a denormal operand is not taken as zero" when the engine does leave the hardware's
  result once the denormals are replaced by zeros. A case nothing explains is counted as `NOT EXPLAINED`; there
  is none today, and a new one is a new kind of failure. `KNOWN_FAILURES.md` is written from this output.
* `--emu-output` shows what the emulator code prints.

A run with `--group` or `--update-baseline` is not compared with the baseline and does not end in `PASS`. A run
with `--except` is compared, in the groups it has; the baseline cannot be written from it.

## What the tests do not have

* FLOAT: there is no console record of it.
* The scalar instructions with DN = 1 and round to zero (0x00040001, what FPSCR is after a reset): no console
  record either. `sh4.fpu.vector` has that setting.
* FPSCR.FR = 1 and the reserved rounding modes: not on record, and a case that has them is not run.
* FIPR and FTRV with generated operands and DN = 0: the model is proven against a console for FIPR only, the
  special cases of both are in `console.specials.dn0`.
* The scalar instructions are not swept: the records are what the console program ran. They do not tell an FMAC
  that is rounded once from one that is rounded to double precision first, see `KNOWN_FAILURES.md`;
  `sh4.directed.*` have operands that do.
