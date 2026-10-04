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

A vector is *identical*, *different* (by the first of: crash, path, bus, state, notify) or *both-crash* (both
engines end in the same `die()` / `verify()` / signal). The baseline has these counts per file. The test fails when
a file has fewer identical vectors or runs fewer than its baseline, and then lists the vectors of the kinds of
difference the file has more of, each with its opcode, PR/SZ, FPSCR, SR and what differs:

    FAIL 1111mmmm00111101_sz0_pr0 (ftrc frm,fpul): identical 253, baseline 254
      state 1, baseline 0 (the final states differ):
        #383 op F43D PR=0 SZ=0, FPSCR 00000001 (FR=0 DN=0 RM=1), SR 700083B3 (MD=1 RB=1 BL=1 FD=1): state: FPUL interpreter 7FFFFFFF dynarec 80000000; ...

An engine that hangs fails the test as well. Counts that moved without costing identical vectors are reported and
do not fail.

## Looking at a failure, updating a baseline

    sh4_differential --file ftrc --verbose              # every vector the engines differ on: path, data accesses, all registers
    sh4_differential --file ftrc --vector 383 --verbose # one vector, also when it is identical
    sh4_differential --unfilter all                     # run the unaligned and odd-pc vectors as well
    sh4_differential --update-baseline                  # after a fix, commit the file with it

`sh4_singlestep_dynarec` has the options of `sh4_singlestep_interp`.
