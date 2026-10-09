# debugger.selftest: the selftests of the debugger, in the emulator

`devcast -config Debugger:SelfTest=<filter>` runs the tests that the files `libswirl/debugger/dbg_selftest_*.cpp`
register, on the emulator thread, against the live machine (`libswirl/debugger/dbg_selftest.h`). The tests of
this directory run such sessions headless, on the emulator that the system tests build
(`<build dir>/system/emulator`, or `-DNULLDC_EMULATOR=`), and are skipped where those are.

    python3 tests/debugger/selftest/selftest.py --emulator <devcast> --work /tmp/w --selftest emu --keep

A package adds the selftest of its file with one line in `CMakeLists.txt`:

    nulldc_debugger_selftest(<name>)

That makes `debugger.selftest.<name>` (the SH4 on the JIT) and `debugger.selftest.<name>.interp` (on the
interpreter): a session with the filter `<name>` that has to end with exit code 0 and the summary of the driver,
with no check failed. The options of the function are at the top of `CMakeLists.txt`; `ELF 2ndmix.elf` boots a
program of `NULLDC_ELF_DIR` instead of the guest of this directory.

Every run has an environment of its own (fresh `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, no display), the
real-time clock pinned, a core size limit of 1 byte and a watchdog, as in `tests/system`.

## The tests of dbg::emu

`libswirl/debugger/dbg_selftest_emu.cpp`: the emulator thread and run control. Its guest is `emu_guest.s`, a
raw image of a few routines (a loop, a loop with a timer interrupt, SLEEP waits with and without that
interrupt, a write of the reset register, a return to the BIOS); `gen.sh` assembles it into `emu_guest.bin`,
which the runs boot, and `emu_guest.inc`, which the emulator is built with. `gen.sh` needs an sh-elf binutils,
the build and the tests do not.

What another thread of the debugger does (a call, a pause request) and what the host does (a stop, a reset
or a loaded state of a stopped machine, a machine that is taken apart and made again) is done by a thread the
test starts. Headless, nothing else plays the host.

| test | what has to hold |
|---|---|
| `debugger.selftest.emu` `.interp` | the session `emu`, the nineteen tests below |
| `emu.machine` | before the guest has run: the lifetime is Running, the park context holds the stop latch, the engine is known, run control without a stop is refused, a `CC_EXEC` call for a running thread is answered at once |
| `emu.pause` | 2000 pauses of a loop, 1000 of a loop with interrupts, 300 of a guest that waits in SLEEP, each asked for by another thread and resumed at once: every stop is a pause with the next stop id, `GetRunState` is the stop, the listener saw Running and Parked in turns on the emulator thread; emulated time at the park is the time at the latch, no scheduler callback ran, nothing executed and no interrupt was accepted in between; an interrupt that the latch held back is accepted by the resume; a pause is latched at the first slice end that has the request; a request first seen inside a SLEEP wait is taken behind the wait. A SLEEP wait that nothing ends (interrupts masked, the slices of the wait made slow): the pause is latched in the wait once it is 250 ms old, with the pc on the SLEEP |
| `emu.events` | before anybody has set the active mask no category is active: the event of an event breakpoint is raised and nothing stops. Then an event breakpoint, with the event raised by a scheduler callback of the test: in a SLEEP wait in the slice that raised the interrupt the wait is for (the wait is cut, the acceptance is owed, the resume accepts it and it returns behind the SLEEP), in a SLEEP wait with nothing pending (the SLEEP is executed again), with the pc written during such a stop, in a loop, together with a pause request (one stop, the event's). Each time the scheduler stopped in the call that raised the event. Two breakpoints on the event report the lower id. With the category inactive, and with the gate of the event closed, nothing stops |
| `emu.calls` | a call of each class for a running thread (`CC_READ` in the slice hook; `CC_TABLE` and `CC_WRITE` in the park context, in a service excursion that publishes nothing; `CC_EXEC` -32003) and for a parked one (all four in the park loop; a resume armed by a call, a second one refused; a `CC_EXEC` call that waited in the queue across the resume: -32003). 800 calls of four threads run one at a time |
| `emu.cancel` | a closure that keeps the emulator thread for 2.6 s is waited for; a `CC_READ` and a `CC_TABLE` call behind it are given up after 2 s with -32000 and never run: their closures use memory the callers free when the call has returned, which ASan would see |
| `emu.hoststop` | the host stops a parked machine, twice in a row: one thread end, both callbacks, in order. Stopped: `CC_READ` and `CC_TABLE` run on the caller, `CC_WRITE` and `CC_EXEC` -32001, a pause request does nothing; a state saved and loaded puts the guest back and is a new stop. A call made while the next thread starts is taken by it. 60 times a pause and a host stop asked for together: the thread ends every time, each callback runs once. A stop during a call: the call ends, the thread ends; calls queued while the stop callback runs are taken by the leaving thread (`CC_READ`, `CC_TABLE`) or answered -32001 (`CC_WRITE`) |
| `emu.lifetime` | the machine is stopped, reset while stopped, taken apart (no machine: `CC_TABLE` runs on the caller, every other class -32002, no engine) and made and started again; the listener saw every change, each a new stop |
| `emu.reset` | the reset register written while parked: the machine is reset and stays parked, reason reset. Written by the guest with a pause asked for in the same slice: the same. A reset by a call for the running thread: the machine resets and runs on, nothing is published. By a call for the parked thread, and from the stop itself: reset, parked, reason reset |
| `emu.resetruns` | a machine that is being reset counts as running: while the guest resets it, and while a call does, another thread looks at `IsRunning()` as a host does, over a million times, and never finds it false. (A host that takes the machine for stopped ends its user interface under the emulator thread.) A reset that is asked for behind the end of the thread leaves the stopped machine stopped |
| `emu.exit` | a guest exit asked for while parked ends the thread: Stopped, guest-exit |
| `emu.fp` (x86-64) | the host FP mode of the emulator thread is FPSCR's when the guest goes on: after a park in which it was changed, and in a thread whose creator had another one |
| `emu.step` | steps, as far as run control goes. With a backend that steps: a step is one stop that says step, with no Running in between; a write of the reset register in a step is one stop, the reset's; a host stop during a step (a SLEEP that nothing ends) is taken when the step has returned, and the stop of the step is never published; a call made during a step runs in the park loop behind it; an event breakpoint ends a step in the slice that raised the event. In a build whose backend does not step: a step is refused and nothing is armed |
| `emu.startstop` | 40 times the host stops the machine while its emulator thread starts, from another thread than the one that starts it, with the threads starting as they do without a session: the thread ends, the callback runs once, and the stopped machine does not count as running |
| `emu.startpark` | the same for threads that park at their start, as the threads of a session do. 20 times the stop is made exactly when the machine takes it for the new thread and the debugger has not heard of that thread, so that it wakes no park loop: the thread finds its cpu left stopped, makes no park and ends. Then 200 times wherever the two threads meet, from the first stop that the machine takes for the new thread to a stop that finds it in its start park: the thread ends, every stop that was taken runs its callback once, and the stopped machine does not count as running |
| `emu.stopevent` | the host stops the machine in the very slice in which an event breakpoint decides a stop: the host stop wins, the machine does not count as running when the stop callback looks, and the stop of the event is never published |
| `emu.stopresume` | the host stops the machine while its emulator thread is parked on a breakpoint, and starts it again: the next thread does not park for that stop again and resumes it, so the instruction of the breakpoint executes and the breakpoint stops at its next arrival (a thread that starts as it does without a session). A pc that was written in the stop, onto another breakpoint, is not the instruction of the stop: that breakpoint stops the next thread at once, as it stops a resume. With the start park of a session in between the guest goes on from the stop as well |
| `emu.stopruns` | a machine that has been asked to stop counts as running until its emulator thread has taken the stop callback. The host stops the machine and acts again at once, as the window does for its close button: parked, running (from a call in the slice hook) and behind an exit of the guest the second action finds the machine running and stops it as well, its callback behind the first; an exit of the guest is asked for once. Then 240 times from another thread, at any moment of the running guest, with a pause asked for and with the thread parked: the host never finds a machine stopped whose thread still has its stop callback to take. (A host that does ends its user interface under a thread that waits for it to draw the frames it queued: the process never ends.) |
| `emu.term` | 48 times the machine is taken apart under its emulator thread and made again: while the guest runs, with a pause asked for, during a service excursion, with a reset by a call and with one by the guest on its way, and while a call resumes the parked thread. The thread is ended first, and finds its machine until it is gone |
| `emu.title` | the window says that the debugger paused at every stop, and the emulator thread calls the window for nothing else. 30 stops of one thread, one behind the other: the park loop puts its title on the window exactly where the running machine has written its frame rate there since the last stop. A stop behind 3 s of a running guest: the title is put again, once. A thread that the host stops makes no call of the window behind its stop callback (the thread of the user interface may be waiting for it there, and a title call waits for that thread on some hosts) |
| `debugger.selftest.emuexit.running` `.pending` `.parked` (and `.interp`) | the guest returns to the BIOS while running, with a pause asked for in the same slice, and the exit hook is called while parked: a headless run ends there, exit code 0, `reason=guest-exit` |
| `debugger.selftest.emurun.reads` (and `.interp`) | 2ndmix for 600 vblanks with another thread reading guest state through `CC_READ` calls for the whole run: the state hash of the exit line is that of the same run without a session, part for part |
| `debugger.selftest.emurun.pending` (and `.interp`) | the same with a pause asked for in the very slice that ends the run: the run ends, with that hash |
| `debugger.selftest.emurun.pauses` (x86-64 hosts) `.interp` | the same with 150 pauses, each resumed at once: that hash. On the interpreter and the x64 recompiler: a pause is exact on a backend whose slice counter is the same after its loop was left and entered again |
| `debugger.selftest.emuhang.parked` | a session that stays parked at its start, with `Exit.AfterVblanks=5`: 4 s later the process is still there and has printed no exit line. Emulated time stands still while the thread is parked, so no `Exit.*` key ends such a run |
| `debugger.selftest.startpaused` | the same for `Debugger:StartPaused=yes` without a session, with the server enabled (`Debugger:Enabled=yes`, `Debugger:Port=0`: without it the key is a configuration error) and no client: the start park waits without limit |

## The tests of dbg::regs and dbg::mem

`libswirl/debugger/dbg_selftest_regs.cpp`: the registers of the device tree, guest memory for the debugger and
the device accessors behind them. The first eight are machine tests: they run once, before the guest has executed
anything, on the machine that boots `2ndmix.elf` of `NULLDC_ELF_DIR`. They read, and where they compare with
what the guest would read they put the machine back as it was. The ninth, `regs.enabled`, is a guest test: it
runs that program for a second of emulated time and reads at stops of it.

| test | what has to hold |
|---|---|
| `debugger.selftest.regs` `.interp` | the session `regs`, the nine tests below |
| `regs.slots` | the tables against the register slots of the live machine: every on-chip row is a registered slot, every registered on-chip slot has a row, and a row and its slot agree about a read function; every system bus row is a register that can be read |
| `regs.read` | every register of every node reads, or says that it cannot, as its flags say: values inside their width, texts not empty and cut to the buffer, the outputs untouched on failure; reads without outputs and outside a node |
| `regs.values` | values against a second source: `dc.sh4.cpu` against the SH4 context, the on-chip registers and `dc.holly.intc` against a guest read where that read changes nothing, the timer counters, `SB_FFST` without counting as a read, the time, the texts |
| `regs.devices` | the debug accessors of the devices against a second source: `dc.aica.arm7` and the interrupt controller of the sound CPU against its save state, in each of the seven modes; `ENABLED` of that node, one bit that reads 0 before the guest has executed anything, and 1 and 0 for a sound CPU that is clocked and one that is held in reset, as `SoundCPU::DebugEnabled()` says; the AICA register file against byte reads by the SH4, and the state of its 64 channels; the RTC; the DSP (`dc.aica.dsp`: its four widths, `MDEC_CT` as the counter modulo 2^16, `RBP` and `RBL` as the fields of the common register, the program against its registers); the interrupt controller; the GD-ROM; boot ROM and flash, and the flash bytes as a guest read delivers them |
| `regs.mem.map` | `Classify()` against the map of `dbg_mem.h`: the regions of the SH4 at both ends and in the middle of every run in every mirror, the regions of the ARM7 and the DSP |
| `regs.mem.read` | what `Read()` delivers, against the stores and the register readers: main memory in its mirrors and across the end of one, video memory in both views, wave memory for both processors, boot ROM and flash, the system bus, PVR and AICA registers and the RTC, the store queues, the on-chip registers at both of their addresses; the GD-ROM block, the modem, the FIFO registers and what is not mapped read as nothing; the readable bits of the 64 KiB around every region boundary |
| `regs.mem.arm7` | the register window of the AICA for the ARM7 target against loads by the ARM7 itself: the whole window, the two bytes behind `ARMRST` and `VREG`, and the bytes where the ARM7 has its interrupt controller, in states of it that are made up |
| `regs.pure` | reading changes nothing: the machine, as a save state has it, is the same before and after every register, every device accessor and the memory around every boundary was read |
| `regs.enabled` | `dc.aica.arm7.ENABLED` where the booted program holds and releases the ARM7. The program runs from its start for 200,000,000 cycles of the scheduler and is stopped wherever it has written another value to `ARMRST` (a scheduler callback of the test looks at the byte in every slice and asks for a pause): at the start, with nothing executed, the register reads 0; at the stop behind a write of 1 it reads 0, behind a write of 0 it reads 1; at every stop it is the flag in the save state of the sound CPU and what `SoundCPU::DebugEnabled()` says, which is what the unwinder of the ARM7 call stack asks; and the program releases the ARM7 on the way. 2ndmix makes four such writes: `spu_init()` holds the ARM7 and releases it, `play_s3m()` does so again |

## The tests of the SH4 breakpoints and steps

`libswirl/debugger/dbg_selftest_sh4bp.cpp`, which `tests/debugger/sh4bp/README.md` describes: the gates of that
mechanism on the park loop of the emulator. They boot `2ndmix.elf` of `NULLDC_ELF_DIR`.

| test | what has to hold |
|---|---|
| `debugger.selftest.sh4bp` `.interp` | the session `sh4bp`: the gates B1 to B14 (none for B11) |
| `debugger.selftest.sh4bpboot` `.interp` | the session `sh4bpboot`: B3, B6 and B7 on the booted guest, every check of its own |
| `debugger.selftest.sh4bpbase` `.interp` | the session `sh4bpbase`: the baselines of those runs |
| `debugger.selftest.sh4bp.sessions` | `tests/debugger/sh4bp/selftest.sh`: the sessions of `sh4bp` on the JIT, on the interpreter and on the JIT with the interpreter fallback forced agree on registers, step traces and time; the runs of the booted guest are in the state of their baselines, on those three and on the JIT and the fallback with idleskip off |

## With a window

`window_checklist.sh <devcast> <work dir> [once]` does at the window of the emulator what a person does there, once
with the emulator thread parked and once with a pause asked for all the time (the sessions `emuwin.parked` and
`emuwin.pending`): the menu key and the buttons of the menu (Save State, Load State, Restart, Resume, Exit), the
escape key, and the close button of the window. Then two actions that the window finds in one pass over its
events, the second while the machine is on its way to the stop of the first: the close button twice, and the
escape key with the close button behind it, with the guest running (`emuwin.running`), parked and with a pause
asked for all the time; a guest that is stopped while it runs in these sessions has a frame queued for the
window, as a program that draws has at most moments. Then the sessions `emu.reset` and `emu.step` with a window
(a reset of the guest while parked, with a pause pending and in a step, a stop by the host while a step executes)
and a guest that returns to the BIOS. Then it reads the title of the window at two stops of one
thread, with the guest running in between (`emuwin.twice`), and has the window closed in the moment in which
the guest resets the machine, ten times (`emuwin.closereset`, which does what the window does for its close
button as soon as it finds the cpu stopped for the reset). It needs a display (`DISPLAY`, default `:0`) and `xsend`, which
it builds from `xsend.c` (Xlib). Events are sent to the window of the run; nothing is typed or clicked on the
desktop. It prints one line per item, `PASS` or `FAIL`, and is not a ctest test: it opens windows, one at a
time, 53 of them; with `once` it does every item one time and none of the repetitions, in 18 windows.
