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
| `b11` | What a guest that uses nothing of the mechanism pays, in host instructions: the build with the debugger against the build without, and one armed address that is never reached against none. Below: [the cost when nothing is armed](#the-cost-when-nothing-is-armed) |
| `b12` | Integer workload: the state of the fallback loop, with continue-only checks and with stops, is the jit's baseline, in fast timing and with `idleskip` off. The loop is selected only while a breakpoint is active. With FPU code time and pc are the jit's; the comparison of the context is printed |
| `b13` | Interpreter: 6000 steps through the workload with interrupts, and a run to the same instruction: identical context, memory and scheduler time |
| `b14` | Jit: 3000 steps through FPU code, the workload and integer code, and runs to the same instructions: identical registers, the FPU registers bit for bit. No cost function of the tree is a sum over instructions, so the times are printed. The fallback's steps cost what the jit's cost |
| `slot` | The interpreter runs the slot of a `bt/s` or `bf/s` that is not taken as an instruction of its own, so a slice can end, and an interrupt be accepted, between the two; a block of the recompiler holds both. A step on the handler's `rte` ends at the slot, which has not run, and the next step runs the slot alone. 30000 steps with interrupts: pc, integer registers and time behind every step are those of the interpreter's plain loop cut into units by a loop of the test's own, and the state behind the steps is that of a run to the same slice end. A slot armed while the loop is left between its branch and it: the slot runs, the next pair stops at its branch, with nothing armed before and with the checking loop already running. The address of a slot that a jump arrives at stops as itself. A breakpoint on a slot stops the handler's return to it, at the slot. An event breakpoint that fires in a slice that ends between the two ends a run there, and a step too, in the same state. Stop + resume at every arrival, and pauses at every slice end, leave the state of a run |
| `arm` | What arming discards: a block that starts on the page in front of the address, the block of a branch whose slot is on the next page, nothing on other pages, nothing for a second breakpoint on an armed address. Two breakpoints on one address report the lowest id. The category mask stops and lets pass without a block being compiled. A breakpoint added during a stop, in the stopped block, stops the resume. Breakpoints on two instructions in a row, and on a delayed branch together with its slot |

Not here, because they need the emulator: the park loop itself and its arbitration with the host, a step over
the reios trap, a reset and a loaded state made by the emulator, pauses and service excursions that another
thread asks for, the rewrite of a memory access inside a block that runs outside the block cache (it happens
on the emulator's host mapping of guest memory), and the 2ndmix workload.
`libswirl/debugger/dbg_selftest_sh4bp.cpp` has those, for `devcast -config Debugger:SelfTest=sh4bp`.

## The selftest of the emulator

`libswirl/debugger/dbg_selftest_sh4bp.cpp` is the same mechanism on the real park loop and the real machine:
the tests `sh4bp.b1` to `sh4bp.b14` of `devcast -config Debugger:SelfTest=sh4bp , Debugger:StartPaused=yes <elf>`
(none for B11: it compares two builds, and a session is one).
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
| recompiler, second window, with the debugger against without | slice | 48 host instructions | 17 | 23 |
| interpreter, second window, with the debugger against without | slice | 48 | 6 | 8 |
| recompiler, first window less second, with the debugger against without | decoded instruction | 24 | 4 | 11 |
| recompiler, first window less second, one armed address against none | decoded instruction | 128 | 59 | 48 |
| recompiler, second window, one armed address against none | | 0.01 % | 1 host instruction of 100.7 million | 1 of 95.3 million |

The counts of two runs of one program differ by some tens of host instructions; those of the program linked
again with its objects in another order by up to 160 in the first window and by as little as two runs in the
second. The bounds are per slice and per decoded instruction, not shares of a run, so that they say the same
for a short run and a long one. A slice of this workload is 56 instructions of the interpreter and about 40
blocks of the recompiler: one host instruction more for each instruction or block that runs is out of bounds.

What the numbers are made of:

- **Compiled code and the dispatcher** are the same with and without the debugger, and with addresses armed
  that the code does not hold.
- **Per slice** the build with the debugger tests the stop latch and the attention word in `UpdateSystem()`
  and the latch in `UpdateINTC()`, and reports an accepted interrupt (`dbg_sh4_OnException`): the calls of
  the table below, which here are the copies this directory compiles. They are the whole difference of the
  second window; nothing of it is per block or per instruction executed.
- **Per decoded instruction** the decoder asks `dbg_sh4_WantsCheck`: a load and a branch while nothing is
  armed; a block adds the test for a pending step-out. With an address armed, the query looks up the page of
  the instruction, and searches the armed set when that page holds an armed address, as the page of this
  workload does in the measurement.
- **Per compiled block**, with and without the debugger, the mechanism costs against the tree without it: the
  decoder keeps the limits and the cost of what it decodes for the refunds, and the register allocator
  makes two more compares in its loops over ops and registers. The same program built from that tree
  without the debugger takes 7600 host instructions less for the 19 blocks this workload compiles (16
  stay in the cache): 400 per compiled block, of about 100000 that compiling one costs. Its second
  window, and both windows of the interpreter, are within 0.2 host instructions per slice of this tree's.
  In the emulator the difference is 170 to 190 host instructions per compiled block, measured with 2ndmix
  and with the BIOS over 2689 and 6636 blocks, which are shorter than this workload's.

`sh4bp_cost <jit|interpreter> [never]` prints the counts of one run.

## The guest

`guest.s` holds the guest programs; `guest.inc` is that file assembled (the image as bytes, the addresses of
the `g_*` labels) and is what the tests build from. `gen.sh` makes it and needs an sh-elf binutils; the build
does not.

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
| | first statement of `UpdateSystem()` | `if (unlikely(dbg::g_stop_latched)) return 1;` |
| | `UpdateSystem()`, behind the scheduler tick | `dbg::emu::SliceHook()` when `dbg::g_attn` is not zero |
| `hw/sh4/sh4_interrupts.cpp` | first statement of `UpdateINTC()` | `if (unlikely(dbg::g_stop_latched)) { dbg::g_intc_deferred = true; return 0; }` |
| | `Do_Interrupt()` and `Do_Exception()`, on every call, in front of the first change of the context | `dbg_sh4_OnException();` |
| `hw/sh4/interpr/sh4_opcodes.cpp` | the `rte` handler, between `next_pc = newpc;` and its `UpdateINTC();` | `if (unlikely(dbg_sh4_stepout != 0)) dbg_sh4_CheckFlow(DBG_SH4_FLOW_RTE);` |
| | the wait loop of the `sleep` handler | `dbg::g_in_sleep` set around the loop; in front of each `UpdateSystem_INTC()`: `if (dbg::g_stop_latched) { dbg::g_sleep_cut = true; ` leave and re-execute `}` |
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
