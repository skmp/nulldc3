# Where devcast's FPU is not the hardware's (sh4.fpu.*)

Why the counts of `baseline_scalar.txt` and `baseline_vector.txt` are below their totals. Every case that is
not as on the hardware has one of the reasons below; `sh4_fpu scalar --why` and `sh4_fpu vector --why` print the
reasons of every group with their counts, this document is written from that output, and no case is
`NOT EXPLAINED`. The numbers are of an x86-64 host with FMA and SSE3. Where two numbers are given they are
interpreter / dynarec.

Sources: "console" is a record taken on a Dreamcast (shrike4-rtl, see `README.md`), "manual" the SH7091
programming manual, section 6 (Floating-Point Unit).

A fix takes its entry out of this file and updates the baselines in the same commit.

| | cases | traps | value-i | value-d | flags-i | flags-d | agree | native | crash |
|---|---|---|---|---|---|---|---|---|---|
| `sh4.fpu.scalar` | 225,036 | 19,633 | 188,189 | 188,189 | 109,153 | 109,153 | 224,924 | 113,946 | 0 |
| `sh4.fpu.vector` | 1,371,226 | 1,617 | 660,047 | 641,389 | 13,254 | 13,254 | 1,112,265 | 1,370,977 | 150 |

## 1. There is no FPU exception

**Groups:** every `.trap` group, `value` is 0 of the total in all of them. Scalar 19,633 records, vector 1,567 of
the 1,617 trap records (the other 50 are in 8.).

On the hardware (console; manual 6.5) an instruction raises the FPU exception (EXPEVT 0x120) and leaves its
destination as it is when

* a denormal operand reaches FADD FSUB FMUL FDIV FMAC FSQRT FCNVDS FCNVSD or FSRRA with DN = 0 (FPU error, E):
  14,077 of the scalar trap records,
* V or Z is raised and enabled,
* an O, U or I enable is set and the instruction is one that can raise it, whatever the result is: every FADD
  FSUB FMUL FDIV FMAC FCNVDS FSQRT FSRRA FSCA FIPR FTRV with EN.I set traps (FTRV also with EN.V).

devcast carries the instruction out and goes on: `sh4_ex_FPU` exists only in a commented-out enum
(`hw/sh4/sh4_interrupts.h`), neither engine can raise it.

    FADD.dn0.rn.trap   part 1 FADD #8 [+dmin +1.5], FPSCR 00000000: FR0 is 3FC00000, hardware 00000001 (not written), FPSCR 00020000
    FTRC.dn0.rn.trap   part 1 FTRC #27556 [+inf], FPSCR 00000800: FPUL is 7FFFFFFF, hardware 5A5A5A5A (not written), FPSCR 00010840

## 2. FPSCR's cause and flag fields are never written

**Groups:** the `flags` columns of nearly every group. Scalar: 115,883 records are not the hardware's FPSCR,
vector: 1,357,822 cases (and 150 of 8.).

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

## 3. A NaN result has the host's bit pattern

**Groups:** scalar 12,473 records: every record of the `.nan` groups of FADD FSUB FMUL FDIV FSQRT (single and
double), FMAC, FSRRA, FCNVDS and FCNVSD (11,477, `value` is 0 there), and the invalid operations of the other
classes: inf - inf, 0 * inf, inf / inf and the square root of -inf in `.inf` (663), 0 / 0 in `.zero` (56), the
square root of a negative number in `.normal` (19) and in `.denormal` with DN = 0 (18), FMAC of a denormal with
an invalid operation in `.denormal` (240). Vector 37,348 / 37,362 cases: `FIPR.*.nan`, `FIPR.*.inf`,
`FTRV.*.nan`, `FTRV.*.inf`, `FSRRA.*.nan`, `FSRRA.*.negative`, `FSRRA.*.inf` (-inf) and their
`console.specials`.

On the hardware (console; manual 6.2.2) every NaN result is 0x7FBFFFFF, or 0x7FF7FFFF FFFFFFFF in double
precision. Sign and payload of an operand are never passed on. devcast leaves what the host makes: 0xFFC00000
for an invalid operation, the operand's NaN with its quiet bit set otherwise. `fixNaN()` (`hw/sh4/sh4_core.h`),
which the interpreter and the canonical helpers call on single precision results, returns its argument: its
body is commented out. `CHECK_FPU_64` is empty.

    FADD.dn0.rn.nan       part 1 FADD #6 [qNaN +1.5]: FR0 is 7FC00001, hardware 7FBFFFFF
    FADD.dn0.rn.inf       part 1 FADD #61 [-inf +inf]: FR0 is FFC00000, hardware 7FBFFFFF
    FSQRT.dn0.rn.normal   part 1 FSQRT #27441 [-3]: FR0 is FFC00000, hardware 7FBFFFFF
    FCNVSD.dn1.rn.nan     part 1 FCNVSD #36728 [qNaN], FPUL 7F800001: DR0 is 7FF80000 20000000, hardware 7FF7FFFF FFFFFFFF

Not touched by this: FCMP (T is the hardware's for every NaN record), FABS and FNEG (bit operations, all
records pass value and flags).

## 4. DN = 1: a denormal operand is computed with

**Groups:** scalar `<instruction>.dn1.rn.denormal`, 4,389 of their 13,564 records: FCMP/EQ 1,150, FCMP/GT 564,
FCMP/EQ.D 1,235, FCMP/GT.D 632, FMAC 387, FDIV.D 120, FMUL.D 72, FSUB.D 47, FDIV 44, FADD.D 43, FMUL 32, FSUB 21,
FADD 20, FSQRT.D 10, FSQRT 4, FSRRA 4, FCNVSD 4. Vector 9,278 / 9,253 cases: `FSRRA.dn1.*.denormal` (all 8,000),
`FIPR.dn1.*.denormal` (1,035 / 993), `FTRV.dn1.*.denormal` (193 / 210), 50 console records.

On the hardware (console; manual 6.2.3) a denormal operand is the zero of its sign when DN = 1, for
arithmetic, compares and conversions. devcast's `SetFloatStatusReg()` (`hw/sh4/sh4_core_regs.cpp`) turns DN
into the host's flush-to-zero mode, which is about results; the denormals-are-zero mode is not set (MXCSR
is 0x9F80 / 0xFF80, bit 6 clear, the tests print it). The host computes with the denormal.

Each of these cases is confirmed by running it again with the denormal operands replaced by zeros: the engine
then leaves the hardware's result. In 362 of them (scalar 356, vector 6) the zero makes an invalid operation,
and the NaN has the host's pattern (3.).

    FCMP/GT.dn1.rn.denormal   part 1 FCMP/GT #7876 [+dmin +0]: T is 1, hardware 0
    FMUL.dn1.rn.denormal      part 1 FMUL #3734 [+dmax +1.5]: FR0 is 00BFFFFE, hardware 00000000
    FMAC.dn1.rn.denormal      part 1 FMAC #22016 [+dmin +inf +1.5]: FR2 is 7F800000, hardware 7FBFFFFF (0 * inf)
    FADD.D.dn1.rn.denormal    part 1 FADD.D #28949 [-dmid +0]: DR0 is 80000000 00000000, hardware +0
    FSRRA.dn1.rn.denormal     denormal #0, FR0 8069C0AE: FR0 is FFC00000, hardware FF800000 (1 / sqrt(-0))

The records of these groups that pass are the ones in which the denormal makes no difference (a denormal
added to a normal number, a denormal times zero).

## 5. DN = 1: a result that rounds up to the smallest normal number is flushed

**Groups:** `FCNVDS.dn1.rn.normal` 260 of 4,105, `FMUL.D.dn1.rn.normal` 5 of 4,137, `FMUL.dn1.rn.normal` 2 of 4,112.

On the hardware (console) a result is rounded once, on the grid of the denormals, and becomes the zero of
its sign only if it is still below the smallest normal number then. A result just below 2^-126 (2^-1022) that
rounds up to it stays that number, with I and without U. The host's flush-to-zero mode decides before that
rounding and returns zero.

    FMUL.dn1.rn.normal     part 2 FMUL #10594, 15FFFFFF * AA000000: FR0 is 80000000, hardware 80800000
    FCNVDS.dn1.rn.normal   part 2 FCNVDS #69634, DR0 380FFFFF E214AE4B: FPUL is 00000000, hardware 00800000

## 6. DN = 0, double precision FDIV: a denormal with nothing in its high word

**Groups:** `FDIV.D.dn0.rn.denormal` 57 of 139, `FDIV.D.dn0.rz.denormal` 18 of 46.

The hardware (console) tells the class of a double by its high word. A denormal whose fraction is all in the
low word reads as a zero there: such an x divided by zero, or by another such denormal, is the zero of the
result's sign, with no cause at all and without the FPU error. The host divides the numbers: an infinity, and 1.

    FDIV.D.dn0.rn.denormal   part 1 FDIV.D #32082 [+dmin +0]: DR0 is 7FF00000 00000000, hardware +0
    FDIV.D.dn0.rn.denormal   part 1 FDIV.D #32166 [+dmin +dmin]: DR0 is 3FF00000 00000000, hardware +0

## 7. FIPR and FTRV are host float arithmetic

**Groups:** every `FIPR.dn1.*`, `FTRV.dn1.*` and `FIPR.console.values.*` group, and 8 records of
`FIPR.console.specials.dn1`: 465,255 / 483,924 cases with a wrong sum, next to what 3. and 4. explain.

The hardware (console) forms the four products in truncating multipliers that keep 28 bits, aligns them to
the largest by cutting bits off, adds them exactly and rounds once. It recodes one of the two factors (FVn for
FIPR, the matrix element for FTRV), so a product is not even commutative. devcast multiplies and adds floats:

* interpreter: every product rounded, then `((p0 + p1) + p2) + p3`, every sum rounded,
* x64 backend: `mulps`, then `(p0 + p1) + (p2 + p3)` (`haddps` twice; for FTRV four `mulps` and
  `(c0 + c1) + (c2 + c3)` over the columns).

| what is wrong with the sum | cases |
|---|---|
| one unit in the last place off | 273,942 / 317,270 |
| more than one unit off | 158,888 / 134,595 |
| a zero of the other sign. An exact zero is +0 on the hardware, where the host adds -0 and -0 to -0; an underflow keeps the sign of the sum on the hardware, where the host adds flushed zeros of both signs to +0 | 12,509 / 12,510 |
| a NaN where the hardware has a number: products that overflow to infinities of both signs, inf - inf (`overflow` and `denormal` classes, round to nearest) | 12,129 / 12,170 |
| another sign, or an overflow on one side only (the `cancel` and `overflow` classes mostly) | 7,235 / 6,824 |
| the sign of an infinity: several infinite products of one sign add up to an infinity with the sign of the first product on the hardware | 552 / 555 |

How much is right depends on the operands; `value-i` / `value-d` of the generated classes, round to nearest
and round to zero:

| class | FIPR rn | FIPR rz | FTRV rn (all 4 results) | FTRV rz |
|---|---|---|---|---|
| `normal` (of 64,000) | 39,523 / 40,365 | 13,651 / 13,929 | 9,202 / 9,850 | 340 / 171 |
| `vec3` (of 24,000), `affine` (of 48,000) | 15,919 / 15,919 | 7,404 / 7,404 | 24,148 / 15,580 | 15,844 / 3,546 |
| `single` (of 24,000), `diagonal` (of 16,000) | 23,069 / 23,069 | 23,296 / 23,296 | 13,432 / 13,432 | 14,248 / 14,248 |
| `exact` (of 16,000) | 15,997 / 15,996 | 15,997 / 15,996 | 15,992 / 15,989 | 15,990 / 15,986 |
| `cancel` (of 16,000) | 1,522 / 1,530 | 1,338 / 1,328 | 21 / 22 | 14 / 13 |

`single` and `diagonal` are one product per result: there the difference is the hardware's multiplier alone,
which is not the correctly rounded product in 3.9 % (rn) and 2.9 % (rz) of the FIPR cases. With more than one
product round to zero is much worse than round to nearest: the hardware cuts once, the host at every step.

    FIPR.dn1.rn.single   single #35, FIPR FV12,FV8, D62EE7A0 * BD496E90: FR11 is 54099F67, hardware 54099F66
    FIPR.dn1.rn.cancel   cancel #0, FV0 3FB7ECB8 3FB7ECBA 0 0, FV4 3DBD6705 BDBD6705 0 0: FR7 is B2800000, hardware B2B80000
    FIPR.dn1.rn.zero     zero #23: every product is -0: FR11 is 80000000, hardware 00000000
    FIPR.dn1.rz.overflow overflow #0: FR7 is 7F38FF6F, hardware 7F7FFFFF

## 8. FIPR with PR = 1 ends the emulator

**Groups:** `FIPR.console.specials.pr1` (100) and `FIPR.console.specials.pr1.trap` (50): `crash` 150, `value` and
`flags` 0, in both engines (with PR = 1 the dynarec calls the interpreter's handler).

On the hardware (console) FIPR, FTRV, FSCA and FSRRA with PR = 1 do nothing: no register is written, FPSCR is
not touched, there is no illegal instruction exception, and the FPU exception is raised if EN.I is set (for
FTRV: EN.V, EN.O, EN.U or EN.I). devcast's FIPR handler calls `die("FIPR Precision=1")`. The handlers of FTRV,
FSCA and FSRRA print a line and do nothing, which is what the hardware does: `FTRV.console.specials.pr1`,
`FSCA.console.specials.pr1` and `FSRRA.console.specials.pr1` pass value and flags (their `.trap` groups fail
by 1.).

## 9. FSRRA is 1 / sqrtf(x)

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

The zeros pass (`FSRRA.dn1.*.zero`: an infinity of the zero's sign), as does +inf. A negative number, -inf and
a NaN are 3., a denormal 4.

## 10. FSCA is a table of the round to nearest results

**Groups:** `FSCA.dn1.rz.angle` 33,084 of 65,536, `FSCA.dn1.rz.upper` 4,110 of 8,192, `FSCA.console.values.rz` 446
of 1,024, 28 of `FSCA.console.specials.dn1`; and `FSCA.dn1.rn.angle` 2 of 65,536, `FSCA.console.values.rn` 3, 8
of `FSCA.console.specials.dn1`.

* `sin_table` (`hw/sh4/sh4_rom.cpp`, from `hw/sh4/fsca-table.h`) is what the hardware computes in round to
  nearest (console: all 65,536 angles). FSCA does not look at FPSCR.RM, and with round to zero the hardware's
  sine or cosine is one unit in the last place off the table for 33,084 of the 65,536 angles. Round to zero
  is what FPSCR is after a reset.
* The table's second half is the negated first half, which makes the sine of 0x8000, and with it the cosine of
  0x4000, -0. The hardware has +0 for both.

    FSCA.dn1.rz.angle   angle #0: FR1 is 3F800000, hardware 3F7FFFFF (cosine)
    FSCA.dn1.rn.angle   angle #16384: FR1 is 80000000, hardware 00000000

FPUL[31:16] is ignored by both (`FSCA.dn1.rn.upper` passes).

## Where the engines differ from each other

`agree` is below the total in these groups only:

* `FIPR.*` and `FTRV.*`, 258,899 cases: the order of the additions (7.). The classes with one or three
  products per sum agree in every case (`vec3`, `single`, `diagonal`).
* `FIPR.console.specials.*`, 62 records, and some of the above: which NaN comes out when several operands are
  NaNs (`fipr_specials.txt:40`: interpreter 7FC00000, dynarec 7FFFFFFF, hardware 7FBFFFFF).
* `FMAC.*.nan` and `FMAC.dn0.rn.trap`, 112 records, the same: [+inf +0 qNaN] is FFC00000 in the interpreter
  (the invalid product first) and 7FC00001 in the dynarec (the addend's NaN).

## What the records do not show

* **FMAC.** The hardware rounds a * b + c once (console: 12,288 records with results in and next to the
  denormal range, cancellations among them; manual 6.4). The interpreter computes it in double precision and
  rounds the sum twice, the x64 backend emits `vfmadd231ss` on a CPU with FMA and `mulss` + `addss` on one
  without. On this host both engines leave the hardware's value in every FMAC record that has no NaN result,
  no denormal operand and is no trap: no record is one of the operand sets the interpreter's second rounding gets
  wrong, and the backend's unfused code is not what runs here. The counts of the FMAC groups are those of an FMA
  host.
* **FADD FSUB FMUL FDIV FSQRT of normal numbers** pass in every record, in both rounding modes, with DN = 0
  results in the denormal range included (but for 5., and for the square root of a negative number, 3.). The
  host's IEEE arithmetic is the hardware's there, as far as the records go.
* **FLOAT**, the scalar instructions with DN = 1 and round to zero, FPSCR.FR = 1: no console record
  (`README.md`).
