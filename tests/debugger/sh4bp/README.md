# debugger.sh4bp: SH4 code breakpoints and steps in the active backend

The gates of the breakpoint mechanism that need no emulator thread. The mechanism is the code the emulator
runs: `libswirl/debugger/dbg_sh4.cpp` with the `dbg_sh4_*` helpers, the decoder, the recompiler of the host (the x64
backend on x86-64, the arm64 backend on arm64) with its main loop and block manager, the interpreter, and the
interpreter fallback of `hw/sh4/dyna/driver.cpp`. Around it
stands a small machine (`machine.h`): RAM behind memory handlers, a time base that counts slices, the SH4's
own interrupt controller with one request line, one debugger event (the vblank), and a stand-in for run
control (`dbg::emu`) whose park context is the test itself.

```
cmake -S tests/debugger/sh4bp -B <build dir> -G Ninja
ninja -C <build dir>
ctest --test-dir <build dir>            # one test per gate: debugger.sh4bp.b1 ...
<build dir>/sh4bp_test b3 b7            # or the gates by name; no argument: all of them
```

The directory also works as a part of the tests project (`add_subdirectory(debugger/sh4bp)` in
`tests/CMakeLists.txt`). It needs a host with the SH4 recompiler: x86-64, Linux or Windows, or arm64, Linux or macOS;
elsewhere the tests are skipped. On Windows and on macOS `b11` says `SKIPPED` (it counts host instructions with
Linux's performance counters; on Linux too where the kernel does not let a process count its own). On arm64 the
suite links the arm64 backend with vixl, and has what the emulator has for it there (`stubs.cpp`): the flush of the
caches over code the backend wrote, and on macOS the code region as MAP_JIT memory at the address of `SH4_TCB`, as
`libswirl/linux/posix_vmem.cpp` maps it; a Mac that refuses that region makes every program of the suite say
`SKIPPED: the code region: ...` with the request, the error and the distance, which ctest counts as a skip. The gate
`abi` (arm64 only) holds what the main loop and RunBlock give a caller back. On Windows the
suite has the emulator's code region with its unwind data (`libswirl/windows/win_jit_unwind.cpp`, handed the region
by `stubs.cpp`) and its policy for a program that dies (no box, no crash report, 128 + the signal's number): at every
stop `machine.cpp` walks the stack with the system's unwinder back to the machine, every gate checks that each of its
walks came through, and the gate `unwind` (Windows only) that walks from stops of the recompiler's checks came through
the check stub, the block and the main loop.

The gates run on three engines, each on those it applies to: `jit` (the recompiler of the host, checks and steps
native to it), `interpreter`, and `fallback` (that recompiler with the debugger told to use the interpreter fallback,
as the configuration key `Debugger:ForceFallback` does).

## The gates

`b1` to `b14` are the ids of the design's gate list; the other gates are named after what they test. *State*
is scheduler time, slice counter, pc, a hash of the SH4 context and a hash of the first megabyte of RAM, which
holds the guest programs.

| Gate | What it checks here |
| --- | --- |
| `b1` | A breakpoint on the first instruction that runs, in the middle of a block, on the slot of a `bra` and on the slot of a `bt/s` that is not taken: the stop is at the instruction (for a slot: at its branch), nothing of it has run, the registers are the interpreter's. The time a stop shows: nothing of a block of fast timing, the share of the instructions that ran with `idleskip` off and in an idle-class block |
| `b2` | A loop of 200 passes with the breakpoint at the start of the loop block, in its middle and on the slot of its `bf/s`: 200 hits on every engine, each resume runs the instruction once, time and state at the loop exit are those of the run without the breakpoint |
| `b3` | State at a later slice end is the baseline's with: armed addresses that are never reached; reached addresses with the category muted; a stop and resume at every arrival (ordinary block, idle-class block, `SLEEP`, slots of `jsr`, `bsr` and `bf/s`, two breakpoints in one block, the block that the size limit ended and the one behind it, the interrupt handler and its `rte`, `ldc sr`); a pause at every 7th slice end; pauses and breakpoints together; an address armed and disarmed in a service excursion at every 3rd slice end; a step-out that stays pending. In fast timing and with `idleskip` off |
| `b4` | 20000 single steps through the workload: pc and integer registers behind every step of the jit and of the fallback are the interpreter's. A delayed branch is one step with its slot, taken or not. A step from a breakpoint runs its instruction |
| `b5` | A step, step over and step out are refused with `-32005` and the reason, and nothing runs, at: an odd pc, a pc that cannot be read, an undefined opcode, a branch / an undefined opcode / a `trapa` in a delay slot, a delay slot that cannot be read. A step on `SLEEP` ends on the `SLEEP` after 1002 slices without an interrupt, at the handler with one |
| `b6` | An address is armed in a service excursion while its block is compiled and hot: it hits; after ten hits it is disarmed and the state at the end is the baseline's. The cached block has no check, then one, then none |
| `b7` | Step over a `jsr`, the recursive call of a factorial (stops on the level of the request), a `trapa`; a breakpoint inside the call ends the step over. Step out of a leaf, of a function that goes on to call a frameless leaf twice, from the entry of that function, of a call that waits for three interrupts, of an interrupt handler; a breakpoint ends a pending step-out. All engines stop at the same pc |
| `b8` | A breakpoint given through `0x0C..`, `0x8C..` or `0xAC..` stops code that runs through any of the three |
| `b9` | The guest overwrites an instruction in a page with an armed address, in the block manager's write-fault path and then in the path of the entry check: the breakpoint hits again, in the new code. A stop in front of the instruction that does the write |
| `b10` | After a stop inside a block the rest of the block is owed once. It is not run when the pc was written (also to the value it had), after a step, after a reset or a loaded state. A step that a breakpoint stops in front of its instruction runs nothing and owes nothing |
| `b11` | What a guest that uses nothing of the mechanism pays, in host instructions: the build with the debugger against the build without, and one armed address that is never reached against none. Below: [the cost when nothing is armed](#the-cost-when-nothing-is-armed) |
| `b12` | Integer workload: the state of the fallback loop, with continue-only checks and with stops, is the jit's baseline, in fast timing and with `idleskip` off. The loop is selected only while a breakpoint is active. With FPU code time and pc are the jit's; the comparison of the context is printed |
| `switch` | The forced fallback is selected and dropped while the guest runs the integer workload with its interrupts: once and in turns at slice ends, in a stop inside a block of the fallback, and at more than 1000 points of a run that a random sequence picks (slice ends both ways, and inside a block from the fallback to the jit's loop). The state at a later slice end is that of a run on the jit's loop alone and of a run on the fallback alone, and the first stop of a fallback that was just selected shows the time that the jit's own check shows there. In fast timing and with `idleskip` off |
| `ownslot` | A delayed branch whose target is its own slot, for `bra`, `bsr`, `bt/s`, `bf/s`, `braf`, `bsrf`, `jmp`, `jsr` and `rts`: the slot runs as the slot and then as the instruction the branch arrived at, which starts a block and has its own check. With breakpoints on the branch, on the slot, behind it and on all three, the stops of the jit and of the fallback are the interpreter's, with their registers: a breakpoint on the slot stops the pair at its branch and the arrival at the slot's address. A step on the branch ends on the slot's address with the slot run once. The state of the fallback loop behind 200 slices with interrupts is the jit's, in fast timing and with `idleskip` off. `bt/s` and `bf/s` that are not taken, for the difference |
| `dense` | Every instruction of a straight line armed, an integer line and a floating point line, in one page and across two: 1024 addresses, the most a session lists. With every arrival stopping, the stops of the jit and of the fallback are the interpreter's, with their registers. No cached block has more checks than the recompiler compiles into one ([below](#checks-in-compiled-code)). With every check answering continue the state behind 45 slices with interrupts is that of the run with nothing armed, in fast timing and with `idleskip` off; so it is with a stop and a resume at every arrival in a block of 150 armed instructions, on the jit and on the fallback, and with a step-out pending. A step-out asked for in the rest of a block that ends with a call is not ended by the callee's return. The run of the floating point line with its 1024 addresses armed and muted, in which the rest of the block is compiled on every pass, leaves the heap of the process as large as it was when it is made a second time. A block whose code is larger than what is left of the code cache has the cache emptied for it, and so has the rest of a block that is compiled outside the block cache; in a code cache of 2600 bytes the lines run in smaller blocks and their stops are the interpreter's, and so they do in a code cache of 1600 bytes in the first 64 KiB of RAM, whose pages hold data, so that each block checks its code when it is entered (`FullCheck`): the room a compile keeps for that check is held there |
| `b13` | Interpreter: 6000 steps through the workload with interrupts, and a run to the same instruction: identical context, memory and scheduler time |
| `b14` | Jit: 3000 steps through FPU code, the workload and integer code, and runs to the same instructions: identical registers, the FPU registers bit for bit. No cost function of the tree is a sum over instructions, so the times are printed. The fallback's steps cost what the jit's cost |
| `slot` | The interpreter runs the slot of a `bt/s` or `bf/s` that is not taken as an instruction of its own, so a slice can end, and an interrupt be accepted, between the two; a block of the recompiler holds both. A step on the handler's `rte` ends at the slot, which has not run, and the next step runs the slot alone. 30000 steps with interrupts: pc, integer registers and time behind every step are those of the interpreter's plain loop cut into units by a loop of the test's own, and the state behind the steps is that of a run to the same slice end. A slot armed while the loop is left between its branch and it: the slot runs, the next pair stops at its branch, with nothing armed before and with the checking loop already running. The address of a slot that a jump arrives at stops as itself. A breakpoint on a slot stops the handler's return to it, at the slot. An event breakpoint that fires in a slice that ends between the two ends a run there, and a step too, in the same state. Stop + resume at every arrival, and pauses at every slice end, leave the state of a run |
| `midpair` | A program whose pass holds a `bt/s` that is not taken, its slot, and a jump that arrives at the slot's address. The first slice end is a pause, for the interpreter in front of each of the 16 instructions of the pass in turn, the place between the branch and its slot included, for the jit and the fallback in front of the four blocks. The slot's address is armed in that stop, alone and together with the instruction behind it. The six stops that follow are the checked places behind the first instruction that runs, on every engine: the slot of a pair that is under way runs without a check and ends the resume exemption, so the jump that arrives at its address in the same pass stops there |
| `vector` | A step-out of the leaf at every arrival, until one stops behind the block (the interpreter: the instruction) that used its slice up. The instruction of that stop is armed and the interrupt made to arrive with the next slice end, which is the first thing the loop of the resume does: the handler runs, returns to the instruction, and it stops again with nothing of it executed; the resume of that stop runs it. On the three engines, in fast timing and with `idleskip` off |
| `arm` | What arming discards: a block that starts on the page in front of the address, the block of a branch whose slot is on the next page, nothing on other pages, nothing for a second breakpoint on an armed address. Two breakpoints on one address report the lowest id. The category mask stops and lets pass without a block being compiled. A breakpoint added during a stop, in the stopped block, stops the resume. Breakpoints on two instructions in a row, and on a delayed branch together with its slot |
| `event` | The machine's vblank event with an event breakpoint on it, which the selftest counts the vblanks of a booted guest with: a stop where the slice of every vblank ends, with the cause and the id, and the state of a run behind them; none with the gate of the event closed or the category muted. A vblank in a SLEEP wait cuts the wait short and leaves the pc on the SLEEP, which is executed again; an interrupt that is accepted on the way out of that stop returns behind the SLEEP |
| `unwind` | Windows only: a breakpoint at the first instruction of a block, in its middle and on the slot of a `bra`, on the jit: the stop of the check is walked by the system's unwinder from the debugger's latch through `dbg_sh4_Check`, the check stub, the block and the main loop back to the machine. The stub's frame at its call is the callee's home space and the refund, 40 bytes below the return address into the block, which is the frame of a block: what the unwind data of the code region describes for every pc that is not the main loop's or RunBlock's |
| `abi` | arm64 only, as `unwind` is Windows-only: the registers a caller keeps (`x19`-`x29`, `sp` and the low 64 bits of `v8`-`v15`, `d8`-`d15`, which the AAPCS64 makes a callee give back) come back from the main loop and RunBlock of the arm64 backend through each way they end. A thunk loads canaries into them, calls the entry and writes what they hold when it returns; wrappers of it take the place of both entries (`rdv_ngen`) while the machine runs a line whose instructions write `r0`-`r7` and `fr0`-`fr9`, so that every host register of the blocks holds a guest register (the blocks write `s8`-`s15`, which clears the rest of `v8`-`v15`). Cases: the loop entered with the cpu stopped, a host stop at a slice end, pauses latched at slice ends, the loop entered with the slice used up, a check that stops in a block, steps (RunBlock) of which one ends the slice, a step-out whose flow report stops, a block whose code hands its rest over (that exit, and RunBlock for the rest) |
| `threads` | Not of the breakpoint mechanism: what the recompiler of a new emulator thread starts with, on the suite's machine because its stand-in for the host counts what the recompiler asks of it. Every emulator thread makes its backend objects anew, and a process has as many emulator threads as its host stops and starts the machine: 256 objects behind the first. The host is asked to prepare the code region once, for the first of them (asked at every start, Linux gives the same pages again, and Windows a new region of 10 MB near the program every time, with room for some two hundred). Every object has the code cache where the first had it, with the stubs of the code generator at its start, nothing behind them and no block in the tables; and the workload run on it ends in the state of the first object's run, with the same blocks in as many bytes of code |

Not here, because they need the emulator: the park loop itself and its arbitration with the host, a step over
the reios trap, a reset and a loaded state made by the emulator, pauses and service excursions that another
thread asks for, the rewrite of a memory access inside a block that runs outside the block cache (it happens
on the emulator's host mapping of guest memory), and the 2ndmix workload.
`libswirl/debugger/dbg_selftest_sh4bp.cpp` has those, for `nulldc3 --config Debugger:SelfTest=sh4bp`.

## The selftest of the emulator

`libswirl/debugger/dbg_selftest_sh4bp.cpp` is the same mechanism on the real park loop and the real machine:
the tests `sh4bp.b1` to `sh4bp.b14` of `nulldc3 --config Debugger:SelfTest=sh4bp , Debugger:StartPaused=yes <elf>`
(none for B11: it compares two builds, and a session is one).
They write the guest programs of this directory into main memory and drive them through `dbg::emu`, and add
what only the emulator has: the boot of the guest through reios, a reset while stopped, an address armed by a
call from another thread while the guest runs, the write protection of the host mapping. A session tests the
backend that is configured, in the timing mode that is configured. The tests `sh4bpboot.*` and `sh4bpbase.*`
of the same file are B3, B6 and B7 on the booted guest ([below](#the-booted-guest)).

```
tests/debugger/sh4bp/sessions.py <nulldc3> <elf> [work dir] [config item ...]
```

runs three sessions of `sh4bp` (JIT, interpreter, JIT with `Debugger:ForceFallback=yes`) and compares what
they have to agree on: registers and step traces of the JIT and of the fallback with the interpreter's, time
and the state of the integer workload of the fallback with the JIT's. Then it makes the gates on the booted
guest, for those three and for the JIT and the fallback with `config:Dynarec.idleskip=no`. Every session is
headless (`config:Headless=yes`, `audio:backend=none`, and an environment of its own: no window is opened) and has
the real-time clock pinned (`config:Dreamcast.RTC`), and runs as the harnesses run the emulator
(`tests/common/emurun.py`; on Windows inside `nulldc3-winrun`). The test `debugger.selftest.sh4bp.sessions` runs it,
on every host. It is the port to Python of `selftest.sh`, which this directory keeps as the bash script it was
ported from: the same sessions, keys, comparisons and repeats, and the same lines (it pinned the clock with
`config:Debug.FixedRtc`, the same second). `sessions.py --rule` (`debugger.selftest.sh4bp.sessions.rule`) makes
its comparisons on sessions that are made up: every key they hold, changed in one session, fails the run, a key
they do not hold leaves it alone, and the repeats on the booted guest go as their rule says.

The same tests also run here, so that they are known to be right before the emulator's park loop runs them:
`sh4bp_selftest <jit|interpreter|fallback> [filter] [idleskip=off]` is the selftest file and its driver on the
machine of this directory, with a park loop in place of `dbg::emu` (`selftest_shim.cpp`) and the boot of the
guest reduced to what the boot trap leaves behind. The test `debugger.sh4bp.selftest` runs the three sessions
of `sh4bp` and the comparison (`selftest_compare.cmake`); the tests `debugger.sh4bp.boot.<configuration>` make
the gates on the booted guest (`selftest_boot.cmake`), and fail when a session leaves one of their cases out.

### The booted guest

The tests `sh4bp.*` run the guest programs with every interrupt masked. `sh4bpboot.b3`, `sh4bpboot.b6` and
`sh4bpboot.b7` make the same gates on the guest that the session booted, with its interrupts live: the ELF of
the command line in the emulator, the boot program of `guest.s` here. *State* is compared at a fixed vblank.

- **A run** is a reset, main memory cleared, and the guest resumed up to its vblank 400. An event breakpoint on
  `dc.holly.core.vblank` counts the vblanks: the guest stops at every one of them, where the slice of the
  vblank ends. What a case tests it arms in the stop of vblank 300. A test is a row of runs, one for each of
  its cases, and prints the state each ended in as `note: cmp boot.<gate>.<case> <state>`.
- **The baseline** of a run is the run at the same place of a second session. A reset does not put the whole
  machine back (the scheduler keeps its time, the devices their place in it and some of their registers), so
  two runs of one session start from different machines and cannot be compared. The tests `sh4bpbase.*` are
  the twins of `sh4bpboot.*`: the same rows of runs with nothing of the cases in them. A session of each comes
  to every reset with the same machine, as long as the runs in front of it changed nothing for the guest. Two
  sessions of baselines have to agree, or two of three; a session of cases that differs from them is run
  twice more and fails when it differs again. The scripts do that; a session of these tests holds nothing
  else, because nothing else may have a say in when its resets come.
- **What a case stops at** is taken from the guest's own path, in a run of its own that every session begins
  with: 3000 single steps from the stop of vblank 300, sorted by their opcodes (instructions that end no
  block, two of them in a row, delay slots, SLEEPs) and, on a recompiler, by the blocks cached for them (blocks
  that the decoder charges as idle, blocks that a size limit ended). A case with breakpoints disarms them
  behind 20000 stops.
- **The cases of `b3`**: an armed address that is not reached; every address of the other cases with the
  category muted; a stop and a resume at every arrival at each kind of address; pauses that another thread
  asks for as fast as they are taken; calls of another thread that arm and disarm the addresses in service
  excursions. **`b6`**: a call of another thread arms the instruction that the path holds most often, while
  the guest runs; it is disarmed in its tenth stop. **`b7`**: a step-out asked for in the stop of every vblank,
  and again where one ended, so that one is pending for the whole run and the ones that end stop behind a
  return of the guest.
- **SLEEPs**: a vblank that finds the guest waiting in a SLEEP cuts the wait short (the event breakpoint
  freezes time in the call that raised it), and how the loop goes on to the park differs with a breakpoint
  on the SLEEP: it does not run the rest of its slice out. That is the skew of a stop inside a SLEEP wait, not
  one of the breakpoint. The run that stops at SLEEPs therefore counts no vblank behind 300: it ends, as its
  baseline does, at the 200th interrupt that the guest accepts from there on, which a breakpoint on the
  interrupt vector counts.
- **Forced fallback**: from the stop of vblank 300 on every run has a breakpoint on the entry point of the
  guest, where the guest does not come back to, so that the fallback loop runs the rest of it, in the
  baselines too. `sessions.py` prints how many of them differ from the JIT's (B12 on the booted guest: the
  FPU arithmetic of the two differs).
- A run whose other thread came too late to do anything is made again in its session. In the emulator that
  moves the runs behind it away from their baselines: the session then differs from them and is repeated.

The machine of this directory boots a guest that has all of it. A vblank is every 29th slice end and an
interrupt request every 2nd; the boot program opens the interrupts behind 24576 instructions with them masked
(the 20000 steps of `sh4bp.b4` stay in front of that) and then runs a main loop of its own with a block of
every kind. That loop is busy: it waits for an interrupt in one pass of 32 only. A guest that sleeps through
most of its time would hide what a stop costs it, because every wait ends at the next interrupt whenever it
began; this one works through several interrupts between two waits, and a cycle that a stop and its resume
lose or add moves the next wait. With one rule of the mechanism broken in a scratch copy (the rest of a
stopped block not run; the refund of a stop not given back; the slice counter of the recompiler's loop, or
of the interpreter's, started anew at every entry) the tests `debugger.sh4bp.boot.*` of the engines that the
rule belongs to fail.

## Checks in compiled code

What a check costs the x64 backend in host code, and why no set of armed addresses can make a block too large
for it; the arm64 backend's numbers are at the end of each point. `debugger.sh4bp.dense` measures and tests all of it.

- **A check** is 15 bytes in its block: two arguments (the address of the instruction, the cycles a stop gives
  back) and a call. The call goes to a tail of 10 bytes (11 on Windows, whose third argument register is `r8d`)
  that the block has once for all its checks, which adds the third argument (the block end) and jumps to a stub
  that every block shares. The stub calls `dbg_sh4_Check` and returns into the block, or adds the refund to the
  slice counter and leaves the block and the main loop. On Windows the stub gives `dbg_sh4_Check` the 32 bytes of
  home space the callee owns, below the refund, so that its frame at the call is the frame of a block, 40 bytes
  below the return address (the gate `unwind`). The register allocator flushes for a check, so nothing is saved
  around the call; the flush itself is at most 276 bytes (each of the 6 general and 4 xmm host registers of the
  System V convention written to the context and loaded again), 508 on Windows (8 general and 10 xmm registers).
  Measured: one check adds 25 bytes to a block (26 on Windows); 56 checks on consecutive instructions add 50
  bytes each in an integer line and 42 in a floating point line, the flushes included. A check in front of which
  every host register was written and behind which every one is read again adds 301 bytes (the call, 276 and the
  tail), 522 on Windows (the call, 496 and the tail): `dense` holds the budget's worth of those within 16 KiB.
  On arm64 a check is 16 bytes (the address in two instructions, the refund in one, the call), the tail 12 (the
  block end in two, the branch to the stub), and the stub keeps the refund and the return address into the block in
  one pair on the stack; the flush is at most 128 bytes (8 general and 8 FP host registers, one `str` and one `ldr`
  each). Measured there: one check adds 28 bytes, 113 on consecutive instructions 26 each in both lines, and a
  check between writes of every register 156 (the call, 128 and the tail).
- **A block holds at most 56 checks** (`ngen_features::DebugCheckBudget`: 16 KiB over the 291 bytes a check
  can take), 31 on Windows (over 523). The decoder decodes a block to its end whatever is armed: its boundaries and its cost are those
  of the block without a check. When the block has more checks than that, the backend compiles the
  instructions in front of the check that is one too many, and the code ends by handing the rest of the block
  over: it gives the cycles of the rest back to the slice counter and leaves the main loop, and the rest runs
  as a block outside the block cache that ends where the block ends and costs those cycles (the way the rest
  of a block runs behind a stop). The block is charged once and the slice counter is tested behind its last
  instruction only, so the split is not visible to the guest. The rest is compiled each time it runs: a
  block with more than 56 armed instructions is slow, not different. A compile leaves nothing on the heap
  (the register allocator frees the spans of a block when the block compiler goes), so a block that is
  compiled on every pass does not make the process grow. On arm64 a block holds at most 113 checks (16 KiB less the
  tail, over the 144 bytes of a check and its flush).
- **Code that does not fit** into what is left of the code cache is given up by the backend, which writes with
  the free space as its bound. The cache is emptied and the block compiled again; code that an empty cache
  has no room for is made of fewer instructions, down to one. (`dense` makes such a block of 500 double precision
  instructions, each a call around which the System V backend saves `xmm8`-`xmm11`; on Windows, where the callee
  keeps `xmm6`-`xmm15` and such a call is a few bytes, of 500 `ftrv`.) That last step moves a block end, and time
  with it: no block of a guest comes near it in a cache of 10 MiB. On arm64 the line is of 500 `ftrv` too (52 bytes
  each, the most code an instruction that costs no cycles compiles to there, 26028 bytes in all), and the backend's
  test of the room it has is strict: vixl grows a buffer that is full with `realloc`, which in the code cache would
  be a crash, so the compile stops in front of the op whose code, with everything behind the last op and vixl's
  pools, might not fit. No such line has the code behind its first 121 instructions that the bound on the rest of a
  block of the other hosts asks for (more than 18 KiB + 2048 bytes), so on arm64 `dense` measures what that bound
  stands for: the block of the first 113 instructions leaves room for a compile to begin behind it, and the rest's
  compile there is given up and the cache emptied for it.

## The cost when nothing is armed

`debugger.sh4bp.b11` measures it. `sh4bp_cost` (`cost.cpp`) runs the workload of the gates on a machine of its
own, without run control, and counts the host instructions of its thread in user mode with the performance
counter of the kernel (`perf_event_open`; where the kernel does not allow that, and on other hosts, the test
is skipped). It is built twice from the same sources, with `NULLDC_DEBUGGER` and without. A run is two
windows of 20000 slices: in the first the recompiler compiles the workload, in the second it compiles
nothing. `b11_compare.cmake` runs every configuration three times and compares the lowest counts:

| Compared | Per | Bound | Measured, g++ 13 | clang 18 |
| --- | --- | --- | --- | --- |
| compiled code: blocks, their guest instructions, bytes of host code; without the debugger, with it, with it and one armed address that is never reached | | equal | 16 blocks, 332 instructions, 7564 bytes | the same |
| recompiler, second window, with the debugger against without | slice | 48 host instructions | 18 | 23 |
| interpreter, second window, with the debugger against without | slice | 48 | 6 | 8 |
| recompiler, first window less second, with the debugger against without | decoded instruction | 24 | 0 | 16 |
| recompiler, first window less second, one armed address against none | decoded instruction | 128 | 59 | 48 |
| recompiler, second window, one armed address against none | | 0.01 % | within 200 host instructions of 100.7 million | within 200 of 95.3 million |

The counts of two runs of one program differ by up to two hundred host instructions; those of the program linked
again with its objects in another order by up to 160 in the first window and by as little as two runs in the
second. The bounds are per slice and per decoded instruction, not shares of a run, so that they say the same
for a short run and a long one. A slice of this workload is 56 instructions of the interpreter and about 40
blocks of the recompiler: one host instruction more for each instruction or block that runs is out of bounds.

What the numbers are made of:

- **Compiled code and the dispatcher** are the same with and without the debugger, and with addresses armed
  that the code does not hold.
- **Per slice** the build with the debugger tests the attention word in `UpdateSystem()`, and the stop latch
  behind it and the latch in `UpdateINTC()`, and reports an accepted interrupt (`dbg_sh4_OnException`): the
  calls of the table below, which here are the copies this directory compiles. The copies keep the test of
  the stop latch in front of the attention word (the numbers above were measured on that form); the emulator
  tests the attention word first. They are the whole difference of the second window; nothing of it is per
  block or per instruction executed.
- **Per decoded instruction** the decoder asks `dbg_sh4_WantsCheck`: a load and a branch while nothing is
  armed; a block adds the test for a pending step-out. With an address armed, the query looks up the page of
  the instruction, and searches the armed set when that page holds an armed address, as the page of this
  workload does in the measurement.
- **Per compiled block**, with and without the debugger, the mechanism costs against the tree without it: the
  decoder keeps the limits and the cost of what it decodes for the refunds, and the register allocator
  makes two more compares in its loops over ops and registers. That is 300 host instructions for a block of
  this workload, of about 100000 that compiling one costs. The x64 backend has its block compiler on the
  stack, where the tree without the mechanism allocates one for every block, and its register allocator
  frees the spans of a block, which that tree leaves on the heap: together 1100 host instructions fewer.
  In all, the same program built from that tree without the debugger takes 14950 host instructions more
  for the 19 blocks this workload compiles (16 stay in the cache), 790 for each. Its second window, and both
  windows of the interpreter, are within 0.3 host instructions per slice of this tree's. In the emulator
  this tree takes 60 to 340 host instructions fewer per compiled block than the tree without the
  mechanism, with the debugger and without it: 340 and 210 with 2ndmix over 4000 vblanks and 2684 blocks,
  60 with the BIOS over 3000 vblanks and 6604 blocks, which are shorter than this workload's. The blocks,
  and the host code of every one of them, are the same in both trees, and behind the compiling the two
  cost the same within 0.0001 % without the debugger and within 0.002 % with it.

`sh4bp_cost <jit|interpreter> [never]` prints the counts of one run.

## The guest

`guest.s` holds the guest programs; `libswirl/debugger/dbg_selftest_sh4bp_guest.inc` is that file assembled
(the image as bytes, the addresses of the `g_*` labels). It stands next to the selftest of the emulator, which
includes it as the tests here do: the emulator's sources include nothing from `tests/`. `gen.sh` makes it and
needs an sh-elf binutils; the builds do not. The image starts with its boot program: what the selftest gets
when it boots the guest.

## The call sites of run control

The mechanism is called from files that belong to run control (`dbg::emu`, `SuperH4_impl`, the interrupt
controller, the opcode handlers) and to the register writer. This is every such call, as the headers
declare it (`debugger/debugger.h`, `debugger/dbg_sh4.h`, `hw/sh4/sh4_if.h`, `hw/sh4/sh4_dbg.h`).

| File | Where | The call |
| --- | --- | --- |
| `hw/sh4/SuperH4_impl.cpp` | `DebugBackend()` | returns the backend object that `Run()` drives |
| | `Run()`, once per entry | `dbg::emu::UseFallbackLoop() ? sh4_fallback_loop() : backend->Loop()` |
| | `Step()` | forwards to `backend->Step()` |
| | `Reset()`, and after a `setBackend()` that follows a `Reset()` which no emulator thread has run since | `backend->ResetSlice()` |
| | the slice end of `UpdateSystem()` | `dbg::g_attn` is tested first; behind it, `if (dbg::g_stop_latched) return 1;` (the latch has its bit in the word, `ATTN_LATCHED`) |
| | `UpdateSystem()`, behind the scheduler tick | `dbg::emu::SliceHook()` when `dbg::g_attn` is not zero |
| `hw/sh4/sh4_interrupts.cpp` | first statement of `UpdateINTC()` | `if (unlikely(dbg::g_stop_latched)) { dbg::g_intc_deferred = true; return 0; }` |
| | `Do_Interrupt()` and `Do_Exception()`, on every call, in front of the first change of the context | `dbg_sh4_OnException();` |
| `hw/sh4/interpr/sh4_opcodes.cpp` | the `rte` handler, between `next_pc = newpc;` and its `UpdateINTC();` | `if (unlikely(dbg_sh4_stepout != 0)) dbg_sh4_CheckFlow(DBG_SH4_FLOW_RTE);` |
| | the wait loop of the `sleep` handler | `dbg::g_in_sleep` set around the loop; in a slice of the wait that reported something and accepted nothing: `if (dbg::g_stop_latched) { dbg::g_sleep_cut = true; ` leave and re-execute `}` |
| `debugger/dbg_emu.cpp` | `LatchStop()`, which the checks and flow reports call to stop | sets `g_stop_latched` and `g_parked`, clears `CpuRunning`, keeps the cause and id it is given |
| | `OnEmuThread()` | true on the emulator thread: `dbg::sh4` discards compiled blocks only then |
| | `SetSh4Breakpoints()`, in the park context, or under the lifetime lock without an emulator thread | `dbg::sh4::SetBreakpoints(ids, addrs, n)` |
| | `SetActiveMask()`, the same | `dbg::sh4::SetActiveMask(categories)` |
| | `UseFallbackLoop()` | `dbg::sh4::FallbackWanted()`, on a JIT without `SH4DBG_NATIVE` (`DebugBackend()->DebugCaps()`) or with `Debugger:ForceFallback` |
| | `GetSh4Engine()` | `DebugBackend()->DebugCaps()` and `SliceCyclesLeft()` |
| | `Step()`, for every kind of SH4 step | `dbg::sh4::StepCheck(&why)`: not zero refuses the step, nothing is armed |
| | `Park()`, entering a stop: once per published stop, in front of the listener; after `Run()` returned with a stop decided, after every step (executed, or complete at a vector), for the start park, for a reset made while paused. Not for a service excursion | `dbg::sh4::OnStop(&info)` |
| | a guest reset, a loaded state and `OnThreadExiting()`: temporary breakpoints and a pending step-out end without a stop | `dbg::sh4::OnStop(nullptr)` |
| | `OnGuestStateReplaced()`: a reset, a loaded state | `dbg::sh4::OnPcWritten()` |
| | `Park()`, leaving the park context: the latch cleared, the deferred interrupt acceptance made, `sh4_host_fp_resync()` called; then, for a resume | `dbg::sh4::PrepareResume()` |
| | the same point, a step of one instruction | `dbg::sh4::PrepareStep()`, then with `CpuRunning` set for its duration `backend->Step()`, or `sh4_fallback_step()` where breakpoints and steps use the fallback |
| | the same point, a step over | `dbg::sh4::PrepareStepOver()`: true, `PrepareResume()` and `Run()`; false, as a step of one instruction |
| | the same point, a step out | `dbg::sh4::PrepareStepOut()`, `PrepareResume()` and `Run()` |
| | the same point, any step whose interrupt acceptance accepted an interrupt | `dbg::sh4::PrepareStep()` alone: the step is complete at the vector, nothing executes |
| | the same point, the end of a service excursion | none of the `Prepare` functions |
| `debugger/dbg_regs.cpp` | a write of the SH4 PC | `dbg::sh4::OnPcWritten()` |

The discards of compiled blocks (`DebugDiscardPage`, `DebugDiscardFlow`) are made by `dbg::sh4` itself,
through `sh4_cpu->DebugBackend()`.

Four of these calls are in files that the gates compile: the two of `hw/sh4/sh4_interrupts.cpp` and the two
of `hw/sh4/interpr/sh4_opcodes.cpp`. While those files do not have them, the build of these tests compiles
copies of the two files with the calls put in (`CMakeLists.txt`, `sh4bp_callsites`); a file that has its
call is compiled as it is. The rest of run control's side is `machine.cpp`: `UpdateSystem()` with the latch
test in front and the slice hook behind the tick, `dbg::emu::LatchStop`, and leaving and entering the park
context in the order of `dbg::emu::Park()`.
