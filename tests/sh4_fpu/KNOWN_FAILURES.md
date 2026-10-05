# Where devcast's FPU is not the hardware's (sh4.fpu.*)

Why the counts of `baseline_scalar.txt` and `baseline_vector.txt` are below their totals. Every case that is
not as on the hardware has one of the reasons below; `sh4_fpu scalar --why` and `sh4_fpu vector --why` print the
reasons of every group with their counts, this document is written from that output, and no case is
`NOT EXPLAINED`. The interpreter and the dynarec (x64 backend) have the same counts in every group: they leave
the same registers in every case.

Sources: "console" is a record taken on a Dreamcast (shrike4-rtl, see `README.md`), "manual" the SH7091
programming manual, section 6 (Floating-Point Unit).

A fix takes its entry out of this file and updates the baselines in the same commit.

| | cases | traps | value-i | value-d | flags-i | flags-d | agree | native | crash |
|---|---|---|---|---|---|---|---|---|---|
| `sh4.fpu.scalar` | 225,036 | 19,633 | 205,051 | 205,051 | 109,153 | 109,153 | 225,036 | 113,946 | 0 |
| `sh4.fpu.vector` | 1,371,226 | 1,617 | 706,768 | 706,768 | 13,404 | 13,404 | 1,371,226 | 1,370,977 | 0 |

## 1. There is no FPU exception

**Groups:** every `.trap` group, `value` is 0 of the total in all of them. Scalar 19,633 records, vector 1,617.

On the hardware (console; manual 6.5) an instruction raises the FPU exception (EXPEVT 0x120) and leaves its
destination as it is when

* a denormal operand reaches FADD FSUB FMUL FDIV FMAC FSQRT FCNVDS FCNVSD or FSRRA with DN = 0 (FPU error, E):
  14,077 of the scalar trap records,
* V or Z is raised and enabled,
* an O, U or I enable is set and the instruction is one that can raise it, whatever the result is: every FADD
  FSUB FMUL FDIV FMAC FCNVDS FSQRT FSRRA FSCA FIPR FTRV with EN.I set traps (FTRV also with EN.V). That holds
  for FIPR, FTRV, FSCA and FSRRA with PR = 1 as well, where they do nothing else, on the hardware and in both
  engines (the 85 records of the `console.specials.pr1.trap` groups).

devcast carries the instruction out and goes on: `sh4_ex_FPU` exists only in a commented-out enum
(`hw/sh4/sh4_interrupts.h`), neither engine can raise it.

    FADD.dn0.rn.trap   part 1 FADD #8 [+dmin +1.5], FPSCR 00000000: FR0 is 3FC00000, hardware 00000001 (not written), FPSCR 00020000
    FTRC.dn0.rn.trap   part 1 FTRC #27556 [+inf], FPSCR 00000800: FPUL is 7FFFFFFF, hardware 5A5A5A5A (not written), FPSCR 00010840

## 2. FPSCR's cause and flag fields are never written

**Groups:** the `flags` columns of nearly every group. Scalar: 115,883 records are not the hardware's FPSCR,
vector: 1,357,822 cases.

On the hardware (console; manual 6.5) every instruction that computes rewrites the cause field (an exact
result clears it) and ORs what it raises into the flag field, also when it traps. devcast writes neither in
any instruction of either engine: in every one of these cases FPSCR behind the instruction is the FPSCR in
front of it. What the hardware does in them:

| the hardware | scalar records | vector cases |
|---|---|---|
| raises I | 18,107 | 1,249,147 |
| raises U and I | 68,157 | 37,258 |
| raises O and I | 395 | 32,711 |
| raises V (with I for FIPR and FTRV: 3,285, with more: 63) | 12,712 | 30,446 |
| raises Z | 366 | 8,184 |
| raises E | 14,077 | 36 |
| raises O, U and I (FTRV, different rows) | | 21 |
| raises nothing and clears a cause field that was set | 2,069 | 19 |

The cases that pass are the ones in which the hardware leaves FPSCR as it was: an exact result or a compare
with nothing in the cause field in front of it. FIPR, FTRV, FSCA and FSRRA of numbers are always inexact on
the hardware (FIPR of zeros raises U and I), so their `flags` are 0 but for infinities, quiet NaNs and PR = 1.

    FADD.dn0.rn.normal         part 1 FADD #12 [+minN +1.5], FPSCR 00000000: FPSCR is 00000000, hardware 00001004
    FADD.dn0.rn.zero           part 1 FADD #786 [+0 +1.5], FPSCR 0003F07C: FPSCR is 0003F07C, hardware 0000007C
    FIPR.console.specials.dn1  fipr_specials.txt:2, all zeros: FPSCR is 00040000, hardware 0004300C

## 3. DN = 1: a result that rounds up to the smallest normal number is flushed

**Groups:** `FCNVDS.dn1.rn.normal` 260 of 4,105, `FMUL.D.dn1.rn.normal` 5 of 4,137, `FMUL.dn1.rn.normal` 2 of 4,112.

On the hardware (console) a result is rounded once, on the grid of the denormals, and becomes the zero of
its sign only if it is still below the smallest normal number then. A result just below 2^-126 (2^-1022) that
rounds up to it stays that number, with I and without U. The host's flush-to-zero mode (MXCSR.FTZ) asks whether
the result is below the smallest normal number when it is rounded to 24 (53) bits with no limit on the exponent,
which is half a unit of the denormal grid earlier: a result from 2^-126 - 2^-150 up to, but not including,
2^-126 - 2^-151 is zero on the host and 2^-126 on the hardware in round to nearest.

That is all the host's mode gets wrong about results. In the console's 65,303 records with DN = 1 that raise no
exception, the hardware flushes a result in 28,585 (U and I): the host's zero is the hardware's in every one of
them. In 1,080 the hardware's result is the smallest normal number with I alone: the host flushes 267 of those.
No bit of MXCSR moves where the host looks; it takes a look at every result in software, which the fast tier
does not do.

    FMUL.dn1.rn.normal     part 2 FMUL #10594, 15FFFFFF * AA000000: FR0 is 80000000, hardware 80800000
    FCNVDS.dn1.rn.normal   part 2 FCNVDS #69634, DR0 380FFFFF E214AE4B: FPUL is 00000000, hardware 00800000

## 4. DN = 0, double precision FDIV: a denormal with nothing in its high word

**Groups:** `FDIV.D.dn0.rn.denormal` 57 of 139, `FDIV.D.dn0.rz.denormal` 18 of 46.

The hardware (console) tells the class of a double by its high word. A denormal whose fraction is all in the
low word reads as a zero there: such an x divided by zero, or by another such denormal, is the zero of the
result's sign, with no cause at all and without the FPU error. The host divides the numbers: an infinity, and 1.

    FDIV.D.dn0.rn.denormal   part 1 FDIV.D #32082 [+dmin +0]: DR0 is 7FF00000 00000000, hardware +0
    FDIV.D.dn0.rn.denormal   part 1 FDIV.D #32166 [+dmin +dmin]: DR0 is 3FF00000 00000000, hardware +0

## 5. FIPR and FTRV are host float arithmetic

**Groups:** every `FIPR.dn1.*`, `FTRV.dn1.*` and `FIPR.console.values.*` group, and 8 records of
`FIPR.console.specials.dn1`: 465,273 cases with a wrong sum.

The hardware (console) forms the four products in truncating multipliers that keep 28 bits, aligns them to
the largest by cutting bits off, adds them exactly and rounds once. It recodes one of the two factors (FVn for
FIPR, the matrix element for FTRV), so a product is not even commutative. devcast multiplies and adds floats:
every product is rounded and every sum is. FIPR adds its four products in pairs, `(p0 + p1) + (p2 + p3)`, FTRV
one by one, `((p0 + p1) + p2) + p3`. That is `sh4_fipr` and `sh4_ftrv` (`hw/sh4/interpr/sh4_fpu.cpp`) in the
interpreter and in the canonical helpers, and `mulps` with `haddps` twice (FTRV: four `mulps` and three `addps`
over the columns) in the x64 backend's code.

Neither order is the hardware's sum. Each instruction has the one of the two that leaves the hardware's result in
more of these cases:

| | in pairs | one by one |
|---|---|---|
| FIPR (of 427,700) | **242,649** | 240,950 |
| FTRV (of 416,391) | 114,572 | **134,938** |

For FTRV nearly all of that is the transform as games set it up (`affine`: three products and a translation that
is added last): 24,148 against 15,580 of 48,000 in round to nearest, 15,844 against 3,546 in round to zero. The
order that is better over all is not better in every group. One by one FTRV loses 738 cases in 7 groups
(`FTRV.dn1.rn.normal` 648, `FTRV.dn1.rz.overflow` 46, `rn.nan` 19, `rn.inf` 14, `rn.wide` 5, `rn.zero` 5,
`rn.cancel` 1) and gains 21,104 in 14. In pairs FIPR loses 121 cases in 11 groups, 13 console records among them
(`FIPR.console.values.rn` 3,933 for 3,944, `.rz` 2,564 for 2,566), and gains 1,820 in 9.

| what is wrong with the sum | cases |
|---|---|
| one unit in the last place off | 283,301 |
| more than one unit off | 149,489 |
| a zero of the other sign. An exact zero is +0 on the hardware, where the host adds -0 and -0 to -0; an underflow keeps the sign of the sum on the hardware, where the host adds flushed zeros of both signs to +0 | 12,521 |
| a NaN where the hardware has a number: products that overflow to infinities of both signs, inf - inf (`overflow` and `denormal` classes, round to nearest) | 12,152 |
| another sign, or an overflow on one side only (the `cancel` and `overflow` classes mostly) | 7,017 |
| the sign of an infinity: several infinite products of one sign add up to an infinity with the sign of the first product on the hardware | 793 |

How much is right depends on the operands; `value` of the generated classes, round to nearest and round to
zero:

| class | FIPR rn | FIPR rz | FTRV rn (all 4 results) | FTRV rz |
|---|---|---|---|---|
| `normal` (of 64,000) | 40,365 | 13,929 | 9,202 | 340 |
| `vec3` (of 24,000), `affine` (of 48,000) | 15,919 | 7,404 | 24,148 | 15,844 |
| `single` (of 24,000), `diagonal` (of 16,000) | 23,069 | 23,296 | 13,432 | 14,248 |
| `exact` (of 16,000) | 15,996 | 15,996 | 15,992 | 15,990 |
| `cancel` (of 16,000) | 1,530 | 1,328 | 21 | 14 |

`single` and `diagonal` are one product per result: there the difference is the hardware's multiplier alone,
which is not the correctly rounded product in 3.9 % (rn) and 2.9 % (rz) of the FIPR cases. With more than one
product round to zero is much worse than round to nearest: the hardware cuts once, the host at every step.

    FIPR.dn1.rn.single   single #35, FIPR FV12,FV8, D62EE7A0 * BD496E90: FR11 is 54099F67, hardware 54099F66
    FIPR.dn1.rn.cancel   cancel #0, FV0 3FB7ECB8 3FB7ECBA 0 0, FV4 3DBD6705 BDBD6705 0 0: FR7 is B2800000, hardware B2B80000
    FIPR.dn1.rn.zero     zero #23: every product is -0: FR11 is 80000000, hardware 00000000
    FIPR.dn1.rz.overflow overflow #0: FR7 is 7F38FF70, hardware 7F7FFFFF

## 6. FSRRA is 1 / sqrtf(x)

**Groups:** `FSRRA.dn1.*.normal`, `.one-to-four`, `.simple`, `FSRRA.console.*` and the scalar `FSRRA.*.normal`:
159,900 vector cases and 10 scalar records.

The hardware (console: every positive number in round to nearest, and [1, 4) in round to zero) computes a
cubic around a table node on the multipliers of FIPR and rounds once. That is not the correctly rounded
1/sqrt(x), and for the flags it is never exact (2.). devcast takes a square root and divides, with two
roundings: `1 / sqrtf(x)` in the interpreter, `sqrtss` and `divss` in the x64 backend.

| class | rn | rz |
|---|---|---|
| `normal` (of 128,000) | 76,498 | 62,917 |
| `one-to-four` (of 32,000) | 19,050 | 15,784 |
| `simple` (of 16,000) | 10,931 | 7,248 |

    FSRRA.dn1.rn.normal   normal #2, FR2 00B45640: FR2 is 5ED7AD28, hardware 5ED7AD29
    FSRRA.dn0.rn.normal   part 1 FSRRA #27832 [+1.5]: FR0 is 3F5105EB, hardware 3F5105EC

The zeros pass (`FSRRA.dn1.*.zero`: an infinity of the zero's sign), and so do the denormals, which are zeros
with DN = 1, +inf, and what gives a NaN: a negative number, -inf and a NaN.

## 7. FSCA is a table of the round to nearest results

**Groups:** `FSCA.dn1.rz.angle` 33,084 of 65,536, `FSCA.dn1.rz.upper` 4,110 of 8,192, `FSCA.console.values.rz` 446
of 1,024, 28 of `FSCA.console.specials.dn1`.

`sin_table` (`hw/sh4/sh4_rom.cpp`, from `hw/sh4/fsca-table.h`) is what the hardware computes in round to nearest:
the test compares the table itself with the console's sine and cosine of all 65,536 angles before it runs
anything, and fails on an entry that is not the console's, a zero of the other sign too. FSCA does not look at
FPSCR.RM, and with round to zero the hardware's sine or cosine is one unit in the last place off the table for
33,084 of the 65,536 angles. Round to zero is what FPSCR is after a reset.

    FSCA.dn1.rz.angle   angle #0: FR1 is 3F800000, hardware 3F7FFFFF (cosine)

FPUL[31:16] is ignored by both (`FSCA.dn1.rn.upper` passes). In round to nearest every case is the hardware's.

## Where the engines differ from each other

Nowhere: `agree` is the total in every group of both tests.

## What the records do not show

* **That FMAC is rounded once.** The hardware rounds a * b + c once (console: 12,288 records with results in and
  next to the denormal range, cancellations among them; manual 6.4), and so do both engines: `fmaf` in the
  interpreter and in the canonical helper, `vfmadd231ss` in the x64 backend's code, or a call of that helper on a
  CPU without FMA. Both leave the hardware's value in every FMAC record that is no trap. But a sum that is
  rounded to double precision first leaves it in all of them as well: no record is one of the operand sets that
  tell the two apart. `sh4.directed.*` have such operands (`tests/README.md`).
* **FADD FSUB FMUL FDIV FSQRT of normal numbers** pass in every record, in both rounding modes, with DN = 0
  results in the denormal range included (but for 3.). The host's IEEE arithmetic is the hardware's there, as far
  as the records go.
* **FLOAT**, the scalar instructions with DN = 1 and round to zero, FPSCR.FR = 1: no console record
  (`README.md`).
