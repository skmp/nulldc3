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
| `debugger.selftest.emu` `.interp` | the session `emu`, the eleven tests below |
| `emu.machine` | before the guest has run: the lifetime is Running, the park context holds the stop latch, the engine is known, run control without a stop is refused, a `CC_EXEC` call for a running thread is answered at once |
| `emu.pause` | 2000 pauses of a loop, 1000 of a loop with interrupts, 300 of a guest that waits in SLEEP, each asked for by another thread and resumed at once: every stop is a pause with the next stop id, `GetRunState` is the stop, the listener saw Running and Parked in turns on the emulator thread; emulated time at the park is the time at the latch, no scheduler callback ran, nothing executed and no interrupt was accepted in between; an interrupt that the latch held back is accepted by the resume; a pause is latched at the first slice end that has the request; a request first seen inside a SLEEP wait is taken behind the wait. A SLEEP wait that nothing ends (interrupts masked, the slices of the wait made slow): the pause is latched in the wait once it is 250 ms old, with the pc on the SLEEP |
| `emu.events` | an event breakpoint, with the event raised by a scheduler callback of the test: in a SLEEP wait in the slice that raised the interrupt the wait is for (the wait is cut, the acceptance is owed, the resume accepts it and it returns behind the SLEEP), in a SLEEP wait with nothing pending (the SLEEP is executed again), with the pc written during such a stop, in a loop, together with a pause request (one stop, the event's). Each time the scheduler stopped in the call that raised the event. Two breakpoints on the event report the lower id. With the category inactive, and with the gate of the event closed, nothing stops |
| `emu.calls` | a call of each class for a running thread (`CC_READ` in the slice hook; `CC_TABLE` and `CC_WRITE` in the park context, in a service excursion that publishes nothing; `CC_EXEC` -32003) and for a parked one (all four in the park loop; a resume armed by a call, a second one refused; a `CC_EXEC` call that waited in the queue across the resume: -32003). 800 calls of four threads run one at a time |
| `emu.cancel` | a closure that keeps the emulator thread for 2.6 s is waited for; a `CC_READ` and a `CC_TABLE` call behind it are given up after 2 s with -32000 and never run: their closures use memory the callers free when the call has returned, which ASan would see |
| `emu.hoststop` | the host stops a parked machine, twice in a row: one thread end, both callbacks, in order. Stopped: `CC_READ` and `CC_TABLE` run on the caller, `CC_WRITE` and `CC_EXEC` -32001, a pause request does nothing; a state saved and loaded puts the guest back and is a new stop. A call made while the next thread starts is taken by it. 60 times a pause and a host stop asked for together: the thread ends every time, each callback runs once. A stop during a call: the call ends, the thread ends; calls queued while the stop callback runs are taken by the leaving thread (`CC_READ`, `CC_TABLE`) or answered -32001 (`CC_WRITE`) |
| `emu.lifetime` | the machine is stopped, reset while stopped, taken apart (no machine: `CC_TABLE` runs on the caller, every other class -32002, no engine) and made and started again; the listener saw every change, each a new stop |
| `emu.reset` | the reset register written while parked: the machine is reset and stays parked, reason reset. Written by the guest with a pause asked for in the same slice: the same. A reset by a call for the running thread: the machine resets and runs on, nothing is published. By a call for the parked thread, and from the stop itself: reset, parked, reason reset |
| `emu.exit` | a guest exit asked for while parked ends the thread: Stopped, guest-exit |
| `emu.fp` (x86-64) | the host FP mode of the emulator thread is FPSCR's when the guest goes on: after a park in which it was changed, and in a thread whose creator had another one |
| `emu.step` | steps, as far as run control goes. With a backend that steps: a step is one stop that says step, with no Running in between; a write of the reset register in a step is one stop, the reset's; a host stop during a step (a SLEEP that nothing ends) is taken when the step has returned, and the stop of the step is never published; a call made during a step runs in the park loop behind it; an event breakpoint ends a step in the slice that raised the event. In a build whose backend does not step: a step is refused and nothing is armed |
| `debugger.selftest.emuexit.running` `.pending` `.parked` (and `.interp`) | the guest returns to the BIOS while running, with a pause asked for in the same slice, and the exit hook is called while parked: a headless run ends there, exit code 0, `reason=guest-exit` |
| `debugger.selftest.emurun.reads` (and `.interp`) | 2ndmix for 600 vblanks with another thread reading guest state through `CC_READ` calls for the whole run: the state hash of the exit line is that of the same run without a session, part for part |
| `debugger.selftest.emurun.pending` (and `.interp`) | the same with a pause asked for in the very slice that ends the run: the run ends, with that hash |
| `debugger.selftest.emurun.pauses.interp` | the same with 150 pauses, each resumed at once: that hash. On the interpreter only: a pause is exact on a backend whose slice counter is the same after its loop was left and entered again |
| `debugger.selftest.emuhang.parked` | a session that stays parked at its start, with `Exit.AfterVblanks=5`: 4 s later the process is still there and has printed no exit line. Emulated time stands still while the thread is parked, so no `Exit.*` key ends such a run |
| `debugger.selftest.startpaused` | the same for `Debugger:StartPaused=yes` without a session: the start park waits without limit |

## With a window

`window_checklist.sh <devcast> <work dir>` does at the window of the emulator what a person does there, once
with the emulator thread parked and once with a pause asked for all the time (the sessions `emuwin.parked` and
`emuwin.pending`): the menu key and the buttons of the menu (Save State, Load State, Restart, Resume, Exit), the
escape key, and the close button of the window. It needs a display (`DISPLAY`, default `:0`) and `xsend`, which
it builds from `xsend.c` (Xlib). Events are sent to the window of the run; nothing is typed or clicked on the
desktop. It prints one line per item, `PASS` or `FAIL`, and is not a ctest test: it opens windows.
