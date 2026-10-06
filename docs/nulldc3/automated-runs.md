# Runs without a person: headless mode, exit conditions, the exit line

Everything here is a `-config` key (`-config section:key=value , section:key=value ...`, the commas are arguments of
their own). The keys can also sit in `emu.cfg`, but they are meant for the command line of a test or a measurement.
The code is `libswirl/utils/run_control.cpp` and `libswirl/gui/gui_headless.cpp`; the tests are `tests/system`.

A complete headless run:

    devcast -config config:Headless=yes , audio:backend=none , config:Debug.SerialConsoleEnabled=yes , \
                    config:Dreamcast.RTC=946684800 , config:Exit.AfterVblanks=600 , config:Exit.StateHash=yes \
            2ndmix.elf

## The keys

| key | value | meaning |
|---|---|---|
| `config:Headless` | `yes` | No window, no GL context, no host input. Linux only. See below. |
| `config:Exit.AfterVblanks` | N | The run ends at the N-th vblank since the start of the process. |
| `config:Exit.AfterSeconds` | S (a decimal number) | The run ends at the first vblank at or after S emulated seconds (S x 200,000,000 SH4 cycles of the scheduler). |
| `config:Exit.OnGuestExit` | `yes` | The run ends when the guest program returns to the BIOS (the exit hook of reios, `REIOS: Exit`). Always so in headless mode and with `Exit.AfterVblanks` or `Exit.AfterSeconds`. |
| `config:Exit.StateHash` | `yes` | The exit line has a hash of the state of the machine. |
| `config:Exit.FrameHash` | `yes` | The exit line has a hash of the last frame a software renderer presented. |
| `config:Exit.FramePng` | a path | That frame is written there as a PNG (implies `Exit.FrameHash`). |
| `config:Dreamcast.RTC` | N, seconds since 1970 | Pins the real-time clock: every run starts at that second, taken as the time of the console (no time zone). 0 or absent: the time of the host. |
| `config:Debug.FixedRtc` | N, seconds since 1970 | The same pin under a second key. When both keys are there and this one is not 0, this one is taken. |
| `<product id>:Dreamcast.RTC`, `<product id>:Debug.FixedRtc` | as above | The two keys in the section of a game (`[T1234N]` in `emu.cfg`, the product number of the disc). Like every setting of a game they go over the keys of that name in section `config` for that game, 0 included; between the two keys the rule is the same: `Debug.FixedRtc`, when it is not 0, is taken. |
| `config:Debug.BlockOnFault` | `yes` | A fatal fault blocks the process for a native debugger instead of ending it. Linux. See "Faults". |

Keys that were there before and that a headless run usually wants: `audio:backend=none` (no audio device is opened;
with a backend and `config:aica.LimitFPS`, the default, the run is held at real time), `config:Debug.SerialConsoleEnabled=yes`
(what the guest writes to the serial port goes to stdout: the `printf` of a KallistiOS program), `config:pvr.backend`,
`config:Dynarec.Enabled=no` (the SH4 interpreter).

## What ends a run, and how

The three exit conditions are checked on the emulator thread: the vblank ones in the vblank of the SPG, the guest
exit in the exit hook. The CPU is stopped there, at a fixed point of the guest program, and the process then takes
the shutdown path of a closed window. The exit code is 0.

A guest program that returns to the BIOS ends every run that has `Headless` or one of the three exit keys, with
`reason=guest-exit`, with a window as well as without one: the exit hook stops the CPU, no vblank follows, and a
limit of vblanks or seconds would never be reached. A run that asked for N vblanks or S seconds has to look at
`reason` to know that it got them. Only a run with none of these keys keeps its window after the program has
ended, with the CPU stopped, for the person in front of it.

A run with `Headless` or with one of `Exit.AfterVblanks`, `Exit.AfterSeconds`, `Exit.OnGuestExit` never writes
`emu.cfg`: settings are not saved, the file is byte for byte what it was (an `emu.cfg` that does not exist is still
created, empty). Independently of that, a value given with `-config` is never what a save writes for its key (the
file keeps what it had), so a normal run with overrides does not change the defaults of the next one either; only
a setting that was changed during the run, in the settings of the GUI, is written.

Other things a run writes are not affected: the VMU images and `dc_nvmem.bin` in the data directory. A test gives every
run its own `HOME`, `XDG_CONFIG_HOME` and `XDG_DATA_HOME`.

## The exit line

One line on stdout, after a newline of its own (the guest may have left the cursor in the middle of a line):

    nulldc-exit: reason=vblanks emu_s=10.032015 vblanks=600 wall_s=0.768558 speed_pct=1303.0 hash=eb9cb70ab8d6227b sh4=3665fcff6f63a000 sched=2006402916 ram=6a82fdf12429a98a vram=c0f4419ed58fad71 aram=6b6ac1095a24df16 arm7=ee638363ad5f8158

`key=value` pairs separated by spaces, in this order; a reader should go by the keys.

| field | meaning |
|---|---|
| `reason` | `vblanks`, `seconds`, `guest-exit`, or `error` |
| `emu_s` | emulated seconds since the start: scheduler time / 200,000,000 |
| `vblanks` | vblanks since the start of the process |
| `wall_s` | seconds of host wall clock from the first vblank to the end of the run |
| `speed_pct` | emulated time over wall time, both from the first vblank on, in percent: 100 is real time. Start-up (process, program load) is left out; a run that ends before its first vblank has `wall_s=0.000000 speed_pct=0.0` |
| `hash` | with `Exit.StateHash`: XXH64 over the six values that follow |
| `sh4` | XXH64 of the SH4 registers a program can see: R0-R15, R0_BANK-R7_BANK, PC, PR, SR, GBR, VBR, SSR, SPC, SGR, DBR, MACH, MACL, FPSCR, FPUL, the 32 floating point registers, the two store queues |
| `sched` | scheduler time in SH4 cycles, decimal |
| `ram` `vram` `aram` | XXH64 of main RAM, video RAM and AICA RAM |
| `arm7` | XXH64 of what the ARM7 writes into a save state: its registers of every mode, CPSR and SPSRs, its interrupt latches |
| `frame` `frame_size` | with `Exit.FrameHash`: XXH64 of the last presented frame (B, G, R, A bytes) and its size, or `frame=none` when nothing presented one |

A run that cannot start prints `nulldc-exit: reason=error error="..."` and exits with code 1.

The hash is defined for both SH4 engines, and it is the same every time for a run that is deterministic (see
`tests/system/README.md` for what that takes). It is **not** the same between the JIT and the interpreter on the same
program: the two charge time differently (the interpreter per instruction, the JIT per block and with its own
costs), so interrupts reach the program at different instructions. A program that does not depend on time leaves the
same `sh4`, `ram`, `vram`, `aram` and `arm7` in both, and a different `sched`.

## Headless mode

`config:Headless=yes` on Linux:

* No X11 window is created, no display is opened, no GL context exists. The process needs neither `DISPLAY` nor
  anything else from its environment (it runs under `env -i`; without `HOME` and `XDG_*` the config and data
  directories are the current directory).
* The thread that would draw the user interface starts the program of the command line and waits. What the emulator
  thread queues for the user interface (the frames) runs on the emulator thread itself, at the point where it is
  queued: the guest never waits for another thread, and nothing another thread does reaches the guest.
* No host input is set up: no keyboard, mouse or gamepad reaches the maple bus. The controller and the two VMUs of
  the default configuration are plugged in, with nothing pressed.
* The run ends when the guest program returns to the BIOS, with or without `Exit.OnGuestExit`.
* The command line has to name something to run (an `.elf` / `.bin` for reios, a disc image, `nodisk`); nothing to
  run, a machine that does not start and a renderer that needs GL end the process with an error line and code 1.

Renderers in headless mode (`config:pvr.backend`):

| backend | build | headless |
|---|---|---|
| `auto` (the default) | both | picks `none` |
| `none` | both | yes. Nothing is rendered; in the default build the TA still builds its lists in VRAM. The guest runs at full speed. |
| `refsw2`, `refmister` | default (TA_LLE) | yes. They render into the framebuffer in VRAM, as always, on the emulator thread, and the frame they would hand to GL at each vblank (640x480 with the two fields woven for an interlaced mode) is kept for `Exit.FrameHash` / `Exit.FramePng`. |
| `refsw`, `refsw-dbg` | default (TA_LLE) | no: `refsw` sets up GL in its `Init`. |
| `gles` | `-DHAS_TA_LLE=OFF` (TA_HLE) | no |

`Exit.FrameHash` and `Exit.FramePng` also work with a window, for `refsw2` and `refmister`. The GL renderers
present nothing that is captured (`frame=none`).

Not headless: Windows and the other platform mains (they still create their window), save states with the GUI's
notifications, anything that needs the GUI (settings, the game list, disc swaps).

## Faults

On Linux a SIGSEGV that is not a guest memory fault (VRAM write protection, a locked JIT page, a fast memory
access the JIT rewrites), and every SIGBUS and SIGILL, writes one line to stderr

    fault_handler: unhandled SIGSEGV at address 0x7467c91e4000, pc 0x7467cbeaa2d9, not a guest memory fault: terminating

and ends the process with the default action of the signal: the exit status is 128 + the signal number in a shell
(139, 135, 132), and a core file is written where they are enabled. `die()` and `verify()` print
`Fatal error : ...` and end with SIGTRAP (133), as before. stdout is line buffered, so the lines in front of the
fault are in a redirected log.

A SIGSEGV, SIGBUS or SIGILL that is **sent** to the process is not a fault, and a SIGSEGV that is sent is never
looked at as a guest memory fault. Sent is what `kill -SEGV <pid>`, `raise`, `tgkill` and `sigqueue` do; the kernel
gives such a signal a `si_code` of 0 or below. It ends the process as soon as it is delivered, on whichever thread
and whatever the machine is doing, with one line on stderr

    fault_handler: SIGSEGV sent by pid 179761 (si_code 0), not a fault: terminating

and the default action of the signal, the same exit status as for a fault (139, 135, 132). The pid is the
sender's; `si_code` is 0 for `kill`, -1 for `sigqueue`, and -6 for `tgkill` and for `raise`, which is a `tgkill` of
the process to itself (the pid is then the emulator's own). The run prints no `nulldc-exit` line: a run that is to
end early and say how it ended is given an exit key, not a signal.

To get the old behaviour, the process blocking in the handler so that a native debugger can be attached (set the
variable `break_segfault` to continue into the fault), set `NULLDC_FAULT_BLOCK=1` in the environment, or
`config:Debug.BlockOnFault=yes` for faults after the config is loaded. A signal that was sent blocks the same way,
its line ending in `blocking for a debugger`; when `break_segfault` is set it is raised again and ends the process,
since there is no instruction to return to that would fault again.

On WSL2 the kernel pipes core files to a handler that ignores `ulimit -c 0`; the one core size limit that keeps it
from running is exactly 1 byte (`prlimit --core=1:`, `resource.setrlimit(RLIMIT_CORE, (1, hard))`; `ulimit -c 1` in
bash is 1024 bytes and does not do it). `tests/system` and `tests/perf` set it for every run.

## `-help`

Prints the usage text and exits with code 0, without opening a display or reading the config.
