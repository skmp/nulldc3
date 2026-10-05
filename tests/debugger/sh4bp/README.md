# debugger.sh4bp: SH4 code breakpoints and steps in the active backend

The gates of the breakpoint mechanism that need no emulator thread. The mechanism is the code the emulator
runs: `libswirl/debugger/dbg_sh4.cpp` with the `dbg_sh4_*` helpers, the decoder, the x64 backend with its main
loop and block manager, the interpreter, and the interpreter fallback of `hw/sh4/dyna/driver.cpp`. Around it
stands a small machine (`machine.h`): RAM behind memory handlers, a time base that counts slices, the SH4's
own interrupt controller with one request line, and a stand-in for run control (`dbg::emu`) whose park context
is the test itself.

```
cmake -S tests/debugger/sh4bp -B <build dir> -G Ninja
ninja -C <build dir>
ctest --test-dir <build dir>            # one test per gate: debugger.sh4bp.b1 ...
<build dir>/sh4bp_test b3 b7            # or the gates by name; no argument: all of them
```

The directory also works as a part of the tests project (`add_subdirectory(debugger/sh4bp)` in
`tests/CMakeLists.txt`). It needs an x86-64 host with the SH4 recompiler; elsewhere the tests are skipped.

Every gate runs on three engines: `jit` (the x64 backend, checks and steps native to it), `interpreter`, and
`fallback` (the x64 backend with the debugger told to use the interpreter fallback, as the configuration key
`Debugger:ForceFallback` does).

## The gates

Ids are those of the design's gate list. *State* is scheduler time, slice counter, pc, a hash of the SH4
context and a hash of RAM.

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
| `b12` | Integer workload: the state of the fallback loop, with continue-only checks and with stops, is the jit's baseline, in fast timing and with `idleskip` off. The loop is selected only while a breakpoint is active. With FPU code time and pc are the jit's; the comparison of the context is printed |
| `b13` | Interpreter: 6000 steps through the workload with interrupts, and a run to the same instruction: identical context, memory and scheduler time |
| `b14` | Jit: 3000 steps through FPU code, the workload and integer code, and runs to the same instructions: identical registers, the FPU registers bit for bit. No cost function of the tree is a sum over instructions, so the times are printed. The fallback's steps cost what the jit's cost |
| `slot` | The interpreter runs the slot of a `bt/s` or `bf/s` that is not taken as an instruction of its own, so a slice can end, and an interrupt be accepted, between the two; a block of the recompiler holds both. A step on the handler's `rte` ends at the slot, which has not run, and the next step runs the slot alone. 30000 steps with interrupts: pc, integer registers and time behind every step are those of the interpreter's plain loop cut into units by a loop of the test's own, and the state behind the steps is that of a run to the same slice end. A slot armed while the loop is left between its branch and it: the slot runs, the next pair stops at its branch, with nothing armed before and with the checking loop already running. The address of a slot that a jump arrives at stops as itself. A breakpoint on a slot stops the handler's return to it, at the slot. An event breakpoint that fires in a slice that ends between the two ends a run there, and a step too, in the same state. Stop + resume at every arrival, and pauses at every slice end, leave the state of a run |
| `arm` | What arming discards: a block that starts on the page in front of the address, the block of a branch whose slot is on the next page, nothing on other pages, nothing for a second breakpoint on an armed address. Two breakpoints on one address report the lowest id. The category mask stops and lets pass without a block being compiled. A breakpoint added during a stop, in the stopped block, stops the resume. Breakpoints on two instructions in a row, and on a delayed branch together with its slot |

Not here, because they need the emulator: the park loop itself and its arbitration with the host, a step over
the reios trap, a reset and a loaded state made by the emulator, pauses and service excursions that another
thread asks for, the rewrite of a memory access inside a block that runs outside the block cache (it happens
on the emulator's host mapping of guest memory), the instruction counts of B11, and the 2ndmix workload.
`libswirl/debugger/dbg_selftest_sh4bp.cpp` has those, for `devcast -config Debugger:SelfTest=sh4bp`.

## The selftest of the emulator

`libswirl/debugger/dbg_selftest_sh4bp.cpp` is the same mechanism on the real park loop and the real machine:
the tests `sh4bp.b1` to `sh4bp.b14` of `devcast -config Debugger:SelfTest=sh4bp , Debugger:StartPaused=yes <elf>`.
They write the guest programs of this directory into main memory and drive them through `dbg::emu`, and add
what only the emulator has: the boot of the guest through reios, a reset while stopped, an address armed by a
call from another thread while the guest runs, the write protection of the host mapping. A session tests the
backend that is configured.

```
tests/debugger/sh4bp/selftest.sh <devcast> <elf> [work dir]
```

runs three sessions (JIT, interpreter, JIT with `Debugger:ForceFallback=yes`) and compares what they have to
agree on: registers and step traces of the JIT and of the fallback with the interpreter's, time and the state
of the integer workload of the fallback with the JIT's.

The same tests also run here, so that they are known to be right before the emulator's park loop runs them:
`sh4bp_selftest <jit|interpreter|fallback>` is the selftest file and its driver on the machine of this
directory, with a park loop in place of `dbg::emu` (`selftest_shim.cpp`) and the boot of the guest reduced to
a jump to the entry point. The test `debugger.sh4bp.selftest` runs the three sessions and the comparison.

## The guest

`guest.s` holds the guest programs; `guest.inc` is that file assembled (the image as bytes, the addresses of
the `g_*` labels) and is what the tests build from. `gen.sh` makes it and needs an sh-elf binutils; the build
does not.

## The call sites of run control

Four calls into the mechanism live in files that belong to run control. While those files do not have them,
the build of these tests compiles copies of the two files with the calls put in (`CMakeLists.txt`,
`sh4bp_callsites`); a file that has its call is compiled as it is.

| File | Where | The call |
| --- | --- | --- |
| `hw/sh4/sh4_interrupts.cpp` | first statement of `UpdateINTC()` | `if (unlikely(dbg::g_stop_latched)) { dbg::g_intc_deferred = true; return 0; }` |
| `hw/sh4/sh4_interrupts.cpp` | `Do_Interrupt()` and `Do_Exception()`, on every call, in front of the first change of the context | `dbg_sh4_OnException();` |
| `hw/sh4/interpr/sh4_opcodes.cpp` | the `rte` handler, between `next_pc = newpc;` and its `UpdateINTC();` | `if (unlikely(dbg_sh4_stepout != 0)) dbg_sh4_CheckFlow(DBG_SH4_FLOW_RTE);` |
| `hw/sh4/interpr/sh4_opcodes.cpp` | the wait loop of the `sleep` handler | `dbg::g_in_sleep` set around the loop; in front of each `UpdateSystem_INTC()`: `if (dbg::g_stop_latched) { dbg::g_sleep_cut = true; ` leave and re-execute `}` |

The rest of run control's side is `machine.cpp`: `UpdateSystem()` with the latch test in front and the slice
hook behind the tick, `dbg::emu::LatchStop`, and leaving and entering the park context in the order of
`dbg::emu::Park()`.
