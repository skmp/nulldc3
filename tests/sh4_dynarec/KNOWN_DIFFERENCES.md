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
| identical | 78,891 |
| different | 12,473 (crash 299, path 0, bus 0, state 442, notify 11,732) |
| both-crash | 625 |
| both-notify | 0: no vector on which both engines carry on from a notification of the emulator code |
| native | 76,038: the instruction under test is native code in the dynarec. 35 files have none |
| filtered | 24,511 (unaligned 23,524, odd-pc 987) |

A vector with more than one difference is counted under the first of crash, path, bus, state, notify.

### Different

| file | instruction | kind | vectors | cause |
|---|---|---|---|---|
| `0000000000101011` | RTE | state | 181 | delay slot register bank |
| `0100mmmm01101010` | LDS Rm,FPSCR | crash | 251 | FPSCR.RM = 2 or 3 |
| `0100mmmm01100110` | LDS.L @Rm+,FPSCR | crash | 48 | FPSCR.RM = 2 or 3 |
| `1111nn0111111101` | FTRV XMTRX,FVn | state | 196 | order of the sum of products |
| `1111nnmm11101101` | FIPR FVm,FVn | state | 64 | order of the sum of products |
| `1111mmmm00111101` | FTRC FRm,FPUL | state | 1 | NaN operand |
| 66 files, every FPU instruction | | notify | 11,732 | SR.FD is only looked at by the interpreter |

**RTE, delay slot register bank.** The dynarec's decoder (`sh4dec(i0000_0000_0010_1011)`, `hw/sh4/dyna/decoder.cpp`)
emits SR = SSR and `shop_sync_sr` (`UpdateSR`, which swaps the R0-R7 banks) in front of the delay slot. The
interpreter (`hw/sh4/interpr/sh4_opcodes.cpp`) sets SR, executes the delay slot and calls `UpdateSR` after it. When
the bank selected by SSR is not the one RTE is entered with, the `add r1,r1` of the delay slot doubles R1 of the new
bank in the dynarec and R1 of the old bank in the interpreter: R1 and R1_BANK differ, nothing else does. The
interpreter's own comment quotes the manual on it: RB after modification is used for the delay slot instruction,
which is what the dynarec does. The single-step vectors expect what the interpreter does.

**LDS / LDS.L to FPSCR, RM = 2 or 3.** The instruction ends its block, and the next block is decoded with the FPSCR
it loaded. `state_Setup` (`hw/sh4/dyna/decoder.cpp`) has `verify(fpu_cfg.RM < 2)`, so the dynarec dies there. The
interpreter runs on: `SetFloatStatusReg` rounds to nearest for every RM but 1.

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
11,732 vectors. Another 295 vectors with SR.FD=1 are counted under what differs first in them (state: FTRV 100,
FIPR 43, FTRC 1; crash: LDS FPSCR 123, LDS.L FPSCR 28).

### Both-crash

Both engines end in the same `die()` / `verify()` / unaligned access. They agree, but there is no result to compare.

| file | instruction | vectors | where |
|---|---|---|---|
| `0000nnnn10110011` | OCBWB @Rn | 248 | `verify(OIX_ADDR[index] == (addr & ~31))`, the operand cache as RAM hack (address bit 25). The dynarec runs the interpreter's handler |
| `11000011iiiiiiii` | TRAPA #imm | 247 | `verify(sr.BL == 0)` in `Do_Exception` with SR.BL=1. The dynarec runs the interpreter's handler |
| `11000011iiiiiiii` | TRAPA #imm | 130 | VBR + 0x100 is odd: the fetch of the handler is an unaligned access |

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
| run | 74,274 | 74,274 |
| pass-exact | 73,780 | 73,497 |
| pass-lenient | 73,939 | 73,675 |
| failed | 335 (248 crash) | 599 (396 crash) |

The files in which the dynarec passes fewer vectors than the interpreter:

| file | instruction | run | interpreter exact / lenient | dynarec exact / lenient | cause |
|---|---|---|---|---|---|
| `0000000000101011` | RTE | 252 | 252 / 252 | 136 / 136 | delay slot register bank, above. The vectors expect the old bank |
| `0100mmmm01101010` | LDS Rm,FPSCR | 256 | 256 / 256 | 128 / 128 | FPSCR.RM = 2 or 3: 128 crash |
| `0100mmmm01100110` | LDS.L @Rm+,FPSCR | 52 | 52 / 52 | 32 / 32 | FPSCR.RM = 2 or 3: 20 crash |
| `1111nn0111111101` | FTRV XMTRX,FVn | 242 | 95 / 211 | 85 / 211 | order of the sum of products, above |
| `1111nnmm11101101` | FIPR FVm,FVn | 223 | 178 / 215 | 169 / 215 | order of the sum of products, above |

The FTRC and SR.FD differences do not show here: those vectors are filtered (`host-nan`, `fpu-disabled`). Everything
else the dynarec does not pass, the interpreter does not pass either (`tests/common/ss_policy.cpp`).

## sh4.directed.*

The hand-written vectors of `tests/sh4_directed` (197 in 26 files). What they expect is the SH-4 software manual's
description of the instruction. The dynarec passes all of them, the interpreter all but three, and those three are
what `sh4.directed.differential` finds:

| file | instruction | delay slot | kind | vectors | cause |
|---|---|---|---|---|---|
| `0100mmmm00001011` | JSR @R3 | STS PR,R5 | state | 1 | PR is written behind the delay slot |
| `1011dddddddddddd` | BSR disp | STS PR,R5 | state | 1 | PR is written behind the delay slot |
| `0000mmmm00000011` | BSRF R3 | STS PR,R5 | state | 1 | PR is written behind the delay slot |

**BSR, BSRF and JSR, PR and the delay slot.** The manual's descriptions of the three write PR and then run the delay
slot (`PR = PC + 4; PC = ...; Delay_Slot(temp + 2);`), so an instruction in the delay slot that reads PR reads the
return address of the call. The interpreter's handlers (`hw/sh4/interpr/sh4_opcodes.cpp`, each with a TODO to check
PR against real hardware) compute the return address, call `ExecuteDelayslot()` and write PR after it: `sts pr,r5`
in the delay slot leaves the PR of before the call in R5 (8C0F6000 in the vectors, the return address is 8C010006).
The dynarec's decoder emits the write of PR in front of the delay slot (`dec_set_pr`, `hw/sh4/dyna/decoder.cpp`)
and passes the three vectors. Nothing else differs in them: the target of the call and the PR that is left are
right in both. The 500 single-step vectors of each of the three files do not see it, their delay slot is
`add r1,r1`. By the same code a delay slot that loads PR (`lds`, `lds.l`) loses what it loads in the interpreter;
no vector has such a delay slot.

The other directed delay slots pass in both engines: RTS returns to the PR it is entered with (`lds r4,pr` in the
delay slot), JMP, BRAF and BSRF go by the register they are entered with (`mov r4,r3` in the delay slot of a jump
through R3), BT/S and BF/S test the T they are entered with.
