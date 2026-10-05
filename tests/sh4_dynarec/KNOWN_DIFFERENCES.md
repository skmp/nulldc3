# SH4 interpreter and dynarec: known differences

What `sh4.differential` finds today, what `sh4.singlestep.dynarec` fails on top of the interpreter suite, and
what the two engines differ in on the directed vectors (`sh4.directed.*`). The baselines (`baseline_differential.txt`,
`baseline_singlestep.txt`, `tests/sh4_directed/baseline_*.txt`) carry these counts; a fix moves a count up and is
committed with the updated baseline and without its entry here and in the `known` table of `differential.cpp` /
`singlestep.cpp` / `tests/sh4_singlestep/main.cpp`.

Measured on x86-64 (GCC 13, a CPU with SSE3 and FMA, without AVX-512), x64 backend, 233 files, 116,500 vectors.

## sh4.differential

| | vectors |
|---|---|
| run | 91,989 |
| identical | 79,467 |
| different | 12,144 (crash 0, path 0, bus 0, state 261, notify 11,883) |
| both-crash | 378 |
| both-notify | 0: no vector on which both engines carry on from a notification of the emulator code |
| native | 76,038: the instruction under test is native code in the dynarec. 35 files have none |
| filtered | 24,511 (unaligned 23,524, odd-pc 987) |

A vector with more than one difference is counted under the first of crash, path, bus, state, notify.

### Different

| file | instruction | kind | vectors | cause |
|---|---|---|---|---|
| `1111nn0111111101` | FTRV XMTRX,FVn | state | 196 | order of the sum of products |
| `1111nnmm11101101` | FIPR FVm,FVn | state | 64 | order of the sum of products |
| `1111mmmm00111101` | FTRC FRm,FPUL | state | 1 | NaN operand |
| 66 files, every FPU instruction | | notify | 11,883 | SR.FD is only looked at by the interpreter |

**FTRV and FIPR, order of the sum of products.** The interpreter adds the four products one by one,
`((p0 + p1) + p2) + p3` (`sh4_fpu.cpp`). The x64 backend multiplies with `mulps` and adds in pairs,
`(p0 + p1) + (p2 + p3)` (`shop_ftrv`: three `addps`, `shop_fipr`: two `haddps`, `jit/backend/x64/rec_x64.cpp`).
Every product and sum is rounded to single precision, and saturates at the largest finite number when FPSCR.RM is
round to zero, so the order shows: 233 of the 260 vectors differ by one unit in the last place and nothing else,
the other 27 by more (opposite signs, or a saturated partial sum against a small or zero result). Neither order is
the SH4's, see the note on these two files in `tests/common/ss_policy.cpp`.

**FTRC of a NaN.** For a NaN with the sign bit clear the interpreter returns 7FFFFFFF: the host conversion gives
80000000 and the handler corrects it by the sign bit of the operand. The x64 backend (`shop_cvt_f2i_t`) uses
`minss` and `cvttss2si` and leaves 80000000. One vector has such an operand (#383). A NaN with the sign bit set gives
80000000 in both, and with FPSCR.PR=1 the dynarec runs the interpreter's handler.

**SR.FD.** `ExecuteOpcode` (`sh4_interpreter.cpp`) checks SR.FD in front of every FPU instruction. In the NO_MMU
build `RaiseFPUDisableException` is a `msgboxf("Full MMU support needed")` and the instruction is executed anyway.
The dynarec never looks at SR.FD, neither in the code it generates nor when it calls an interpreter handler
(`shop_ifb` calls the handler, not `ExecuteOpcode`). Results are the same, the notification is the difference:
11,883 vectors. Another 144 vectors with SR.FD=1 are counted under what differs first in them (state: FTRV 100,
FIPR 43, FTRC 1).

### Both-crash

Both engines end in the same `die()` / `verify()` / unaligned access. They agree, but there is no result to compare.

| file | instruction | vectors | where |
|---|---|---|---|
| `0000nnnn10110011` | OCBWB @Rn | 248 | `verify(OIX_ADDR[index] == (addr & ~31))`, the operand cache as RAM hack (address bit 25). The dynarec runs the interpreter's handler |
| `11000011iiiiiiii` | TRAPA #imm | 130 | SR.BL=0 and VBR + 0x100 is odd: the fetch of the handler is an unaligned access. The dynarec runs the interpreter's handler |

### Not seen by the differential

* Unaligned data accesses and odd PCs (24,511 vectors) end both engines in the unaligned access handler and are
  filtered. In the emulator the dynarec with a host memory mapping does not go through that handler.
* An instruction the decoder has no translation for is one call of the interpreter's handler in both engines. That
  compares the decoder's bookkeeping around it (PC, block ends, register write-back), not two implementations. The
  `native` column of the baselines has, per file, for how many vectors it is not so; `README.md` lists the 35 files
  that are at 0.
* The x64 backend picks code by host CPU feature. With FMA, as measured, FMAC is `vfmadd231ss` and agrees with the
  interpreter's double precision multiply-add on all 500 vectors; the path without FMA (`mulss`, `addss`) was not run.
  The AVX-512 path of `shop_frswap` was not run either.

## sh4.singlestep.dynarec

Same vectors, filter and comparison as `sh4.singlestep.interp`.

| | interpreter | dynarec |
|---|---|---|
| run | 73,851 | 73,851 |
| pass-exact | 73,357 | 73,338 |
| pass-lenient | 73,516 | 73,516 |
| failed | 335 (248 crash) | 335 (248 crash) |

The files in which the dynarec passes fewer vectors than the interpreter:

| file | instruction | run | interpreter exact / lenient | dynarec exact / lenient | cause |
|---|---|---|---|---|---|
| `1111nn0111111101` | FTRV XMTRX,FVn | 242 | 95 / 211 | 85 / 211 | order of the sum of products, above |
| `1111nnmm11101101` | FIPR FVm,FVn | 223 | 178 / 215 | 169 / 215 | order of the sum of products, above |

The FTRC and SR.FD differences do not show here: those vectors are filtered (`host-nan`, `fpu-disabled`). Everything
else the dynarec does not pass, the interpreter does not pass either (`tests/common/ss_policy.cpp`).

## sh4.directed.*

The hand-written vectors of `tests/sh4_directed` (229 in 30 files). What they expect is the SH-4 software manual's
description of the instruction, and for the two things of the last section here what that section says. Both engines
pass all of them, and `sh4.directed.differential` finds no difference.

The delay slots: BSR, BSRF and JSR write PR in front of the delay slot (`sts pr,r5` in it reads the return address
of the call, `lds r4,pr` in it is the later write and stays), RTS returns to the PR it is entered with (`lds r4,pr`
in the delay slot), JMP, BRAF and BSRF go by the register they are entered with (`mov r4,r3` in the delay slot of a
jump through R3), BT/S and BF/S test the T they are entered with, and the delay slot of RTE runs with the SR that
RTE restores (`mov r0,r8` in it reads R0 of the new bank, `movt r8` the new T).

## Not measured on a console

What both engines do alike where the manual does not say, or says something that no console was measured for. A
test pins it, a measurement may still move it.

**FPSCR.RM = 2 and 3.** LDS and LDS.L store them like every other value (FPSCR keeps its low 22 bits, which a
console was measured for: FFFFFFFF reads back as 003FFFFF). The chip is not measured for RM >= 2. Both engines round by
RM bit 0, RM = 2 to nearest and RM = 3 to zero: that is what the RTL (`rtl/v1/v1_fpu.sv`, `rz = fpscr[0]`) and the
soft-float reference (`tb/sh4_fpdn.h`, `rz = fpscr & 1`) of shrike4-rtl do. `FPSCR_RM_ZERO` in `hw/sh4/sh4_if.h` is the
one place that decides it, for the host FP mode (`SetFloatStatusReg`) and for the decoder; `sh4.core` checks the host
mode of all four RM values and `sh4.directed.*` that a block that starts with RM = 2 or 3 is compiled.

**A general exception with SR.BL = 1.** The manual has it as a manual reset of the CPU (SH7091 hardware manual
5.5.3 and 5.6.1 (2)): EXPEVT = H'020, VBR = 0, SR with MD, RB and BL set, FD clear and IMASK = 1111, a branch to
H'A0000000, and "the registers of the other modules retain their contents". A console cannot be measured for it, it
reboots into the BIOS. `Do_Exception` (`hw/sh4/sh4_interrupts.cpp`) writes what table 2.1 defines after a reset (SR =
700000F0, VBR, FPSCR = 00040001, PC) and EXPEVT, and keeps everything else: the general registers of both banks,
GBR, SSR, SPC, SGR, DBR, MACH, MACL, PR, FPUL and the FP registers (undefined after a reset in the manual), TRA, and
MMUCR, CCR and the on-chip modules. That is also what shrike4-rtl predicts (`rtl/v1/spec/exceptions-intc.md` 3.4,
3.16). In the NO_MMU build TRAPA is the only general exception there is, so TRAPA with SR.BL = 1 is the one way
into it; with a BIOS the BIOS starts again, without one reios boots the program again (its hook is at A0000000).
`sh4.directed.*` have three such TRAPAs, `sh4.core` the EXPEVT and TRA of them.
