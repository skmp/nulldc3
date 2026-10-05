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
| identical | 79,584 |
| different | 12,027 (crash 0, path 0, bus 0, state 0, notify 12,027) |
| both-crash | 378 |
| both-notify | 0: no vector on which both engines carry on from a notification of the emulator code |
| native | 76,038: the instruction under test is native code in the dynarec. 35 files have none |
| filtered | 24,511 (unaligned 23,524, odd-pc 987) |

A vector with more than one difference is counted under the first of crash, path, bus, state, notify.

### Different

| file | instruction | kind | vectors | cause |
|---|---|---|---|---|
| 66 files, every FPU instruction | | notify | 12,027 | SR.FD is only looked at by the interpreter |

**SR.FD.** `ExecuteOpcode` (`sh4_interpreter.cpp`) checks SR.FD in front of every FPU instruction. In the NO_MMU
build `RaiseFPUDisableException` is a `msgboxf("Full MMU support needed")` and the instruction is executed anyway.
The dynarec never looks at SR.FD, neither in the code it generates nor when it calls an interpreter handler
(`shop_ifb` calls the handler, not `ExecuteOpcode`). Results are the same, the notification is the difference:
12,027 vectors.

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
* The x64 backend picks code by host CPU feature. With FMA, as measured, FMAC is `vfmadd231ss`; without it the
  backend calls the canonical helper, which is the `fmaf` the interpreter computes FMAC with. That path is not what
  ctest runs on a CPU with FMA. The AVX-512 path of `shop_frswap` was not run either.

## sh4.singlestep.dynarec

Same vectors, filter and comparison as `sh4.singlestep.interp`.

| | interpreter | dynarec |
|---|---|---|
| run | 73,800 | 73,800 |
| pass-exact | 73,287 | 73,287 |
| pass-lenient | 73,465 | 73,465 |
| failed | 335 (248 crash) | 335 (248 crash) |

There is no file in which the dynarec passes fewer vectors than the interpreter, or more: what one does not pass,
the other does not pass either (`tests/common/ss_policy.cpp`). FTRV and FIPR are a sum of four products that both
engines add in pairs, `(p0 + p1) + (p2 + p3)`, each product and each sum rounded to single precision: the
interpreter and the canonical helpers in `sh4_fipr` / `sh4_ftrv` (`hw/sh4/interpr/sh4_fpu.cpp`), the x64 backend
with `mulps` and `haddps` / `addps`. Of their vectors 85 of 242 (FTRV) and 169 of 223 (FIPR) pass exactly. The
SR.FD difference does not show here: those vectors are filtered (`fpu-disabled`).

## sh4.directed.*

The hand-written vectors of `tests/sh4_directed` (264 in 33 files). What they expect is the SH-4 software manual's
description of the instruction, and for the two things of the last section here what that section says. Both engines
pass all of them, and `sh4.directed.differential` finds no difference.

The delay slots: BSR, BSRF and JSR write PR in front of the delay slot (`sts pr,r5` in it reads the return address
of the call, `lds r4,pr` in it is the later write and stays), RTS returns to the PR it is entered with (`lds r4,pr`
in the delay slot), JMP, BRAF and BSRF go by the register they are entered with (`mov r4,r3` in the delay slot of a
jump through R3), BT/S and BF/S test the T they are entered with, and the delay slot of RTE runs with the SR that
RTE restores (`mov r0,r8` in it reads R0 of the new bank, `movt r8` the new T).

FTRC, single and double precision: 2^31 and above and +inf give 7FFFFFFF, -2^31 and below, -inf and every NaN
80000000, in the interpreter and in the x64 backend's code (`shop_cvt_f2i_t`: `cvttss2si`, and 7FFFFFFF from
2147483648.0f on).

FMAC is FR0 * FRm + FRn rounded once, on operands for which a product that is rounded first, or a sum that is
rounded to double precision first, gives another single.

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
