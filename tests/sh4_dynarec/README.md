# SH4 dynarec tests

| test | what it runs |
|---|---|
| `sh4.singlestep.dynarec` | every single-step vector through the SH4 dynarec, gated on `baseline_singlestep.txt` |
| `sh4.differential` | interpreter against dynarec from the initial state of every vector, gated on `baseline_differential.txt` |

Both run the recompiler of the emulator build: `hw/sh4/dyna` (decoder, shil, driver, block manager) and the x64
backend. On hosts without that backend they are registered as skipped. `tests/README.md` has the vectors, the
filter and how a baseline works; `KNOWN_DIFFERENCES.md` has what the two baselines here contain and why.

    dynarec_runner      a vector's instructions as blocks: decoder, shil, x64 backend
    singlestep.cpp      sh4.singlestep.dynarec, an SsSuite (tests/common/ss_suite.h) on the runner
    diff_memory         the guest memory of the differential
    diff_engines        the two engines on a vector and the comparison of what they make of it
    differential.cpp    sh4.differential: files, filter, report and baseline

## How the dynarec is run

`dynarec_runner` does what `rdv_CompilePC_OrFail` and the generated main loop do, for one vector at a time:

* a block is set up like `RuntimeBlockInfo::Setup`, but decoded with a `max_cycles` that ends it at the 4th
  instruction of the vector. The decoder ends blocks on its own at branches and at instructions that load SR or
  FPSCR, so a vector is one to three blocks.
* the decoder fetches a whole block before any of it runs. While it decodes, 16 bit reads are served the vector's
  opcodes by address; the block itself only makes its data accesses, through the memory handlers (there is no host
  mapping of guest memory, `nvmem` is off).
* the block is compiled, entered once and deleted, and the code cache is rewound for the next vector. Blocks are
  never registered with the block manager.
* blocks keep guest registers in callee saved host registers and rely on the main loop to save them. The runner
  enters them through a generated stub that does what that loop does around the call.

## sh4.singlestep.dynarec

The interpreter suite with another way to execute the 4 instructions. Instruction fetches are not part of the
expected bus order here; instead the addresses the blocks were decoded from have to be the vector's fetch addresses.

The decoder hands the instructions it has no translation for to the interpreter's handler (`shop_ifb`); a vector
of such an instruction passes without any native code being tested. Both tests count, per file, the vectors
whose instruction was compiled (`native` in the report and in the baselines) and fail when a file has fewer than
its baseline. 35 files are at 0 today: ADDV, SUBV, NEGC, DIV1, XTRCT, TAS.B, the logic instructions on
`@(R0,GBR)`, MOVCA.L, OCBI / OCBP / OCBWB, CLRMAC, LDTLB, LDC.L @Rm+,SR, STC.L SR,@-Rn, LDS and LDS.L to FPSCR,
SLEEP, TRAPA and the FPU instructions with PR=1. In every other file all vectors that are run are native.

## sh4.differential

Does not look at what the vectors expect. Both engines start from the vector's initial state, run its 4
instructions on the same memory and are compared on

* the path: the addresses the instructions are run from,
* the bus: every data access in order (read or write, width, address, value),
* the state: every register, bit for bit,
* the outcome: running through, and what the emulator code notified of (`msgboxf`) on the way.

The memory (`diff_memory`) returns the vector's opcodes for fetches, the vector's recorded value for its data read
and a value made of the address anywhere else, whatever the order of the accesses. Only the vectors devcast cannot
run are filtered (`unaligned`, `odd-pc`): 91,989 of 116,500 run.

A vector is *identical*, *different* (by the first of: crash, path, bus, state, notify), *both-crash* (both
engines end in the same `die()` / `verify()` / signal) or *both-notify* (the engines agree in everything, and both
carry on from the same notifications of the emulator code: that is not an identical vector). The baseline has these
counts per file. The test fails when a file

* has fewer identical vectors than its baseline, runs fewer, or has fewer whose instruction is native,
* has more vectors than its baseline that differ by crash, path, bus or state, that crash in both engines or that
  both engines notify on. A difference may go away or become a notification in one engine; it may not become a
  difference of another kind,

and then lists the vectors of the kinds of difference the file has more of, each with its opcode, PR/SZ, FPSCR, SR
and what differs:

    FAIL 1111mmmm00111101_sz0_pr0 (ftrc frm,fpul): identical 253, baseline 254
      state 1, baseline 0 (the final states differ):
        #383 op F43D PR=0 SZ=0, FPSCR 00000001 (FR=0 DN=0 RM=1), SR 700083B3 (MD=1 RB=1 BL=1 FD=1): state: FPUL interpreter 7FFFFFFF dynarec 80000000; ...

An engine that hangs fails the test as well, and so does a notification an engine carries on from that the policy
does not announce by its text (`ss_policy_notification` in `tests/common/ss_policy.cpp`; today that is "Full MMU
support needed" for an FPU instruction with SR.FD=1), whatever the baseline says. Every other count that is not the
baseline's (more identical vectors, notifications of one engine that come or go, fewer of another kind) is reported,
and fails with `--strict`, as ctest runs it.

## Looking at a failure, updating a baseline

    sh4_differential --file ftrc --verbose              # every vector the engines differ on: path, data accesses, all registers
    sh4_differential --file ftrc --vector 383 --verbose # one vector, also when it is identical
    sh4_differential --unfilter all                     # run the unaligned and odd-pc vectors as well
    sh4_differential --update-baseline                  # after a fix, commit the file with it

`sh4_singlestep_dynarec` has the options of `sh4_singlestep_interp`. As there, a run with `--vector`, `--unfilter`
or `--update-baseline` is not compared with the baseline and ends in `not gated (...)`, not in `PASS`.
