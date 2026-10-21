# Runs without a person: headless mode, exit conditions, the exit line

Everything here is a `--config` key (`--config section:key=value , section:key=value ...`, the commas are arguments of
their own). The keys can also sit in `emu.cfg`, but they are meant for the command line of a test or a measurement.
The code is `libswirl/utils/run_control.cpp` and `libswirl/gui/gui_headless.cpp`; the tests are `tests/system`.

A complete headless run:

    nulldc3 --config config:Headless=yes , audio:backend=none , config:Debug.SerialConsoleEnabled=yes , \
                    config:Dreamcast.RTC=946684800 , config:Exit.AfterVblanks=600 , config:Exit.StateHash=yes \
            2ndmix.elf

## The keys

| key | value | meaning |
|---|---|---|
| `config:Headless` | `yes` | No window, no GL context, no host input. Linux, Windows and macOS. See below. |
| `config:Exit.AfterVblanks` | N | The run ends at the N-th vblank since the start of the process. |
| `config:Exit.AfterSeconds` | S (a decimal number) | The run ends at the first vblank at or after S emulated seconds (S x 200,000,000 SH4 cycles of the scheduler). |
| `config:Exit.OnGuestExit` | `yes` | The run ends when the guest program returns to the BIOS (the exit hook of reios, `REIOS: Exit`). Always so in headless mode and with `Exit.AfterVblanks` or `Exit.AfterSeconds`. |
| `config:Exit.StateHash` | `yes` | The exit line has a hash of the state of the machine. |
| `config:Exit.FrameHash` | `yes` | The exit line has a hash of the last frame a software renderer presented. |
| `config:Exit.FramePng` | a path | That frame is written there as a PNG (implies `Exit.FrameHash`). |
| `config:Dreamcast.RTC` | N, seconds since 1970 | Pins the real-time clock: every run starts at that second, taken as the time of the console (no time zone). 0 or absent: the time of the host. |
| `config:Debug.FixedRtc` | N, seconds since 1970 | The same pin under a second key. When both keys are there and this one is not 0, this one is taken. |
| `<product id>:Dreamcast.RTC`, `<product id>:Debug.FixedRtc` | as above | The two keys in the section of a game (`[T1234N]` in `emu.cfg`, the product number of the disc). Like every setting of a game they go over the keys of that name in section `config` for that game, 0 included; between the two keys the rule is the same: `Debug.FixedRtc`, when it is not 0, is taken. |
| `config:Debug.BlockOnFault` | `yes` | A fatal fault blocks the process for a native debugger instead of ending it. Linux and macOS. See "Faults". |
| `config:Debug.AicaHash` | N | The sound regression harness: the run ends behind the N-th tick of the AICA with one line of hashes, and with no exit line. See "The sound line". |

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
created, empty). Independently of that, a value given with `--config` is never what a save writes for its key (the
file keeps what it had), so a normal run with overrides does not change the defaults of the next one either; only
a setting that was changed during the run, in the settings of the GUI, is written.

Other things a run writes are not affected: the VMU images and `dc_nvmem.bin` in the data directory. A test gives every
run a directory of its own with `--portable <dir>`: without it a run writes beside the executable (the README of the
tree has the layout of the directory).

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

## The sound line

`config:Debug.AicaHash=<N>` is the sound regression harness (`libswirl/hw/aica/aica_hash.h`). A tick of the AICA is
one scheduler callback, 32 samples (145124 SH4 cycles); the first one comes at the end of the first slice of the
SH4, 448 cycles behind the start of the machine. Behind the N-th tick the emulator prints one line on stdout

    AicaHash: samples=<16 hex digits> regs=<16> arm7=<16> sh4=<16> dsp=<16> ticks=<N> cycles=<decimal>

and **ends the process there**, on the emulator thread, with exit code 0 and without the shutdown of a run: nothing
is saved and **no `nulldc-exit:` line is printed**. A run gives the sound line or the state hash of
`Exit.StateHash`, not both; whoever wants both makes two runs. The line is printed with no newline in front of it:
with `config:Debug.SerialConsoleEnabled=yes` a guest that leaves the cursor in the middle of a line puts its text
in front of `AicaHash:`.

| field | meaning |
|---|---|
| `samples` | 64-bit FNV-1a of every pair handed to the audio stream: right then left, two bytes each, low byte first |
| `regs` | the same hash over the `0x8000` bytes of the register file of the AICA, at every tick |
| `arm7` | over what the sound CPU writes into a save state (its registers of every mode, the interrupt latches, the reset state), at every tick |
| `sh4` | over PC, R0-R15, PR, SR and FPSCR of the SH4, at every tick, on a backend whose context is complete inside a scheduler callback (the interpreter and the x64 recompiler); on any other it stays the offset basis `cbf29ce484222325` |
| `dsp` | over the state blob of the DSP core (`AicaDsp::SaveState`, `AicaDsp::StateSize()` bytes, `libswirl/hw/aica/dsp_core/aica_dsp_core.h`), at every tick: its registers, its datapath, MDEC_CT, both MIXS banks. The same on every host |
| `ticks` | N |
| `cycles` | scheduler time behind the N-th tick, in SH4 cycles |

The line has no field for wave memory, main memory or the scheduler: those are in the state hash of the exit line.

Two runs of one binary with the same program and keys print the same line when the run does not depend on the
host, which takes what every deterministic run takes: `audio:backend=none`, the clock pinned (`Dreamcast.RTC` or
`Debug.FixedRtc`) and no host input (`Headless`). The sound is made sample by sample, with the DSP, in every time
step of the AICA, and every tick has its 32 time steps, whether the ARM7 is held in reset or not: the AICA is clocked
whatever `ARMRST` says, and only the ARM7 is held (`config:aica.NoBatch`, which chose a render of 32 samples a burst
without the DSP in older builds, is read no more); `config:aica.NoSound=yes` hands the
audio stream a pair of zeroes for every sample, and `samples` is then the hash of those. The SH4 engine
(`config:Dynarec.Enabled`) is another `sh4`, and for a program whose SH4 reaches the sound at times of its own
another everything. A line is good for one build on one host (the generator's tables come from `pow()`), so no
line is stored in the tree: a test compares runs it makes itself.

`config:Dynarec.Enabled=yes` is a wish: an emulator without a recompiler, or one whose recompiler cannot be set
up, runs the SH4 on its interpreter, prints `Using MCPU Interpreter` where the other prints `Using MCPU Recompiler`,
and ends like any other run. Whoever compares the lines of the two engines looks for that line first.

A program that plays nothing has a `samples` field too: the hash of `k * 32` pairs of zeroes, one value for each
`k`, and `k` is N for a line at N ticks. `tests/debugger/guest/dsptest/` has a program that does play
(`dsptest.elf`, part of the tree), the script that makes these runs and compares their lines (`soundlines.py`),
and the tests `debugger.sound.*`.

## Headless mode

`config:Headless=yes` on Linux:

* No X11 window is created, no display is opened, no GL context exists. The process needs neither `DISPLAY` nor
  anything else from its environment (it runs under `env -i`): its files are in the directory of the executable,
  or in the one of `--portable <dir>`; only `--shared` reads `HOME` and `XDG_*`.
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

Not headless: save states with the GUI's notifications, anything that needs the GUI (settings, the game list, disc
swaps).

### Windows

`config:Headless=yes` on Windows is the same mode, with these Windows parts (`libswirl/windows/`):

* No window class is registered and no window is made, no XInput device, keyboard or mouse gamepad is set up, no GL
  context exists (`os_CreateWindow` and `os_SetupInput` return). The main thread makes no COM apartment either: the
  `CoInitialize` of a windowed run is in the windowed path of `os_CreateWindow`, since an apartment has hidden windows
  of its own. DirectSound, when `audio:backend` picks it, takes the desktop window for its cooperative level; a
  DirectSound that fails (a system without a sound device: `DirectSound: <step> failed (<code>): no sound` on stdout)
  is no sound, and the run goes on as with `audio:backend=none`, its speed limit (`aica.LimitFPS`) waiting for no
  sound device. Automated runs pass `audio:backend=none` anyway.
* `nulldc3.exe` is a console program: run by a harness it writes into the handles it was given and never opens a
  console of its own. stdout and stderr are in binary mode (no `\r` in front of a `\n`, the bytes Linux writes) and
  unbuffered. `main` returns the exit code of run control: 0, 1 (`runctl_fail`, a failed selftest session), 2 for a
  refused command line and a bad configuration of the debugger, as on Linux.
* `--help`, `--version` and a refused command line show their text in a message box as well only where nobody can
  read the output once the process has ended (no console, or a console the process has alone) and when no
  `--config` item of the command line sets `config:Headless`: a headless run never shows one.

**No dialog of any kind, in every run.** `main` installs, before anything else: the error mode
`SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX` (no "no disk" box, no fault box),
`WerSetFlags(WER_FAULT_REPORTING_NO_UI)`, no message box and no report of `abort()` (`_set_abort_behavior`) and a
`SIGABRT` handler, the C runtime's invalid-parameter and pure-call handlers (without them an invalid parameter ends in
`__fastfail`, straight to Windows Error Reporting), the debug runtime's reports to stderr, a vectored fatal handler
behind the handler of guest memory faults, and the unhandled-exception filter. `die()` and `verify()` break into a
debugger only when one is attached (`IsDebuggerPresent()`).

**The fatal line and the exit codes** are Linux's: one line on stderr, written with `WriteFile`,

    fault_handler: unhandled SIGSEGV at address 0x10, pc 0x7ff6a1b2c3d4, not a guest memory fault: terminating

and the process ends with 128 + the signal's number:

| what | signal | exit |
|---|---|---|
| an access violation, an in-page error, a misaligned access, a stack overflow | `SIGSEGV` | 139 |
| an illegal or a privileged instruction | `SIGILL` | 132 |
| a breakpoint instruction, no debugger attached | `SIGTRAP` | 133 |
| `abort()`, `std::terminate`, an invalid parameter, a pure call, an exception nobody took | `SIGABRT` | 134 |
| an integer divide by zero, or the overflow of a divide | `SIGFPE` | 136 |

`die()` and `verify()` print their own `Fatal error : ...` / `Verify Failed` line on stdout and end with 133 without
a `fault_handler` line, so that a test tells them from a breakpoint instruction. The vectored handler takes an access
violation, an in-page error and a misaligned access only when they happen in `nulldc3.exe`'s image (the static C
runtime is part of it) or in the recompiler's code region: another module's (a graphics driver's) first fault goes on
to that module's own handlers, and one that nobody takes reaches the unhandled-exception filter, which prints the same
line and ends the same way. Nothing reaches Windows Error Reporting or a just-in-time debugger.

**Test hooks** (Windows builds): `NULLDC_TEST_FATAL=<kind>` raises a fault right after the emulator has started,
`segv`, `ill`, `trap` (`die()`), `abort`, `div`, `stack`, `invparam` (a C runtime call with an invalid parameter) or
`purecall`; `NULLDC_TEST_DSOUND=fail` makes DirectSound fail as on a system without a sound device (its first step
answers `DSERR_NODRIVER`, and nothing of DirectSound is called); `NULLDC_TEST_UI_REPORT=1` prints one line at the end
of `main`,

    nulldc-ui: app=0 windows=0 errmode=8003 wer=20 invparam=1 purecall=1 sigabrt=1 abort=0 classes=

`app` 1 when a window class was registered, `windows` the process's top-level windows that are visible or of the
emulator's class, `errmode` `GetErrorMode()`, `wer` `WerGetFlags()`, `invparam`, `purecall` and `sigabrt` 1 when the
handler in force is the policy's own, `abort` the flags of `_set_abort_behavior` (read without a change), `classes`
the class of every top-level and message-only window of the process. `tests/windows` holds these.

**Automated runs on Windows go through `nulldc3-winrun`** (`tests/winrun/README.md`): every program an automated run
starts there, the emulator, a test, ctest itself, runs inside it, on a desktop of its own, in a job, with the error
mode above inherited by everything below it; a long step runs detached with a log and an rc file. Its exit status is
the program's, or 124 (a timeout), 125 (a window was seen) or 126 (its set-up failed). Every run also carries the keys
of an automated run: `--portable <dir>` and `--config config:Headless=yes , audio:backend=none ,
config:Dreamcast.RTC=946684800` with the run's `Exit.*` keys.

**The harnesses of the tests on Windows.** `tests/debugger/selftest/selftest.py`, `tests/debugger/contract/contract.py`,
`tests/debugger/guest/dsptest/soundlines.py` and `tests/debugger/sh4bp/sessions.py` start every program through
`tests/common/emurun.py`, the run helper they share (Python 3.9 and the standard library; `harness.emurun` tests it).
It gives an emulator run its environment, which on Windows is `SystemRoot` (without it Winsock does not start:
`WSAStartup` fails with 10106) and `TEMP` and `TMP` in the run's own directory, nothing of the profile; a program
that is not the emulator (the contract runner, the tools of the tests) gets the caller's environment with `TEMP`,
`TMP` and npm's cache (`npm_config_cache`, where npm writes a log of every command) in its run's directory, so that
`node`, `tsx` and npm write nothing into the profile; it starts the emulator, the contract runner (`npm.cmd`, and the
`cmd.exe` and `node` behind it) and the tools of the tests inside `nulldc3-winrun`, whose path the tests project passes
in `NULLDC_WINRUN`, so that ending a run (Windows has no SIGTERM) ends its whole job; it finds `nulldc3.exe` for
`nulldc3`, reads the output without the `\r` of a `\r\n`, and resolves no POSIX shell: `bash` on the PATH of
Windows is WSL's launcher, so the check of the sounding guest's generated files (`gen.sh --check`) is not run there
and says so. The emulator's own keys are those above. On a POSIX host the helper starts the program itself, with an
environment of the sanitizers' options alone (a tool: the caller's) and a core size limit of 1 byte. `tests/system`
builds no emulator on Windows: the tests project takes `-DNULLDC_EMULATOR=<nulldc3.exe>` (`tests/README.md`, "On
Windows"). Every program of the tests project has the emulator's policy for a program that dies on Windows
(`tests/common/win_policy.cpp`: no box, no crash report, one line and 128 + the signal's number), which
`common.policy.windows` reads in every program the project builds and `common.guard.windows` makes four of them show;
`nulldc3-winrun` does not stop crash reports. The selftest sessions of the debugger (`Debugger:SelfTest`) nap for
fractions of a millisecond with a high-resolution waitable timer on Windows (`libswirl/debugger/dbg_selftest_nap.h`),
where `std::this_thread::sleep_for` lasts a tick of the system timer; the emulator asks for no timer resolution.

### macOS

`config:Headless=yes` on macOS is the same mode, with these macOS parts (`nulldc3/apple/AppDelegate.mm`,
`libswirl/linux/common.cpp`):

* The process makes nothing of AppKit. `main` reads the directories (Foundation alone), sets up the fault handler and
  runs `nulldc_init`, which reads the command line and the configuration; a headless `os_CreateWindow` returns before
  there is an `NSApplication`, and so does `os_SetupInput`. No `NSApplication`, no window, no menu bar, no Dock
  icon, no check-in with LaunchServices, no connection to the window server. The loop of the user interface runs on
  the main thread, as on Linux, and `main` returns the exit code of run control: 0, 1, and 2 for a refused command
  line and a bad configuration of the debugger. With a window the `NSApplication` is made there, when the emulator
  asks for its window (the Dock icon, the menu, the window); a run that ends by itself (an exit key) ends the
  application, with the same exit codes. Windows are not restorable: a windowed run writes no state of its windows
  into `~/Library/Saved Application State`.
* A refused command line shows an alert only where nobody can read its message: no `--config` item of the command
  line sets `config:Headless`, stderr is `/dev/null` or closed, and LaunchServices started the process (the Finder,
  the Dock, `open`: `__CFBundleIdentifier` is in its environment, or launchd is its parent). Otherwise the message is
  on stderr alone, with the exit code 2.
* stdout is line-buffered, as on Linux, and every `printf` goes there; the setup's line `Linux paging: ...` goes to
  stderr, so that the stdout of `--version` is its one line.
* The executable is the bundle's, `nulldc3.app/Contents/MacOS/nulldc3`. An automated run starts it by its path, never
  with `open`: LaunchServices makes an application of what it opens.

**Fatal ends in automated runs** (headless, or with one of the `Exit.*` keys) are an exit, not the signal: macOS
writes a crash report into `~/Library/Logs/DiagnosticReports` for every process that a signal ends. The line is
Linux's, on stderr,

    fault_handler: unhandled SIGBUS at address 0x10, pc 0x1004c2f34, not a guest memory fault: terminating

and the process ends with `_exit(128 + N)`:

| what | signal | exit |
|---|---|---|
| an access to an address with no page | `SIGSEGV` | 139 |
| an access that the page does not allow: a write to a page mapped read-only, an address in `__PAGEZERO` (a null pointer) | `SIGBUS` | 138 |
| an illegal instruction | `SIGILL` | 132 |
| a breakpoint instruction (`__builtin_trap()`), no debugger attached | `SIGTRAP` | 133 |
| `abort()`, `std::terminate` | `SIGABRT` | 134 |
| a floating-point exception (arm64 traps neither an integer divide by zero nor a floating-point operation) | `SIGFPE` | 136 |

`die()` and `verify()` print their own `Fatal error : ...` / `Verify Failed` line on stdout and, with no debugger
attached, end with 133 without a `fault_handler` line, so that a test tells them from a breakpoint instruction. A run
with a person in front of it dies of the signal, as before, and its crash report is that person's. A `SIGSEGV` or
`SIGBUS` that is a guest memory fault is handled as on Linux (on macOS a write to a page that is mapped read-only is a
`SIGBUS`).

**Test hooks** (macOS builds): `NULLDC_TEST_FATAL=<kind>` raises a fault right after the emulator has started,
`segv` (a write to address 16, a `SIGBUS` on macOS), `ill`, `trap` (`die()`) or `abort` (`div` on x86_64 hosts only);
`NULLDC_TEST_UI_REPORT=1` prints one line when `main` ends,

    nulldc-ui: app=0 windows=0

`app` 1 when the process has an `NSApplication`, `windows` its windows. `tests/macos` holds these.

**Automated runs on the Mac go through `tests/macos/macrun.py`** (`tests/macos/README.md`): every emulator run of an
automated run there, and every long step (a build, ctest), runs under the sandbox profile
`tests/macos/no-windowserver.sb`, which denies the window server, the window manager, the Dock and LaunchServices, so
that nothing it runs can make a window, a Dock icon or a dialog; in a process group of its own, with a timeout,
`TMPDIR` in its work directory and a PID file; a long step runs detached under `caffeinate -i` with a log and an rc
file. It applies the profile when its caller has none, and runs the program under the caller's otherwise (a ctest
under `macrun.py --detach`); it runs nothing without `/usr/bin/sandbox-exec` unless `CI` is set. Its exit status is
the program's, 128 + N for a signal, 124 for its timeout, 126 when it ran nothing. Every run also carries the keys of
an automated run: `--portable <dir>` and `--config config:Headless=yes , audio:backend=none ,
config:Dreamcast.RTC=946684800` with the run's `Exit.*` keys. The harnesses of the tests (the selftests, the contract
gates, the sound lines) do not run on macOS yet (`tests/README.md`, "On macOS").

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

## `--help` and `--version`

`--help` prints the usage text and exits with code 0, without opening a display or reading the config, and without
making or choosing a directory. `--version` does the same with the line `nullDC <version>`, the last on stdout (the
Linux build prints three lines of its own setup in front of it, the personality and the page size, as it does for
`--help`; the macOS build prints its line of the page size on stderr, and the version's line is its stdout alone):
the version the build made from git, `3.0.0-pre+g<h>` (the first 10 characters of the commit) until a tag `v<T>` is
reachable, `<T>` on the tag, `.dirty` or `+dirty` behind it when a tracked file had changed
(`nulldc3/README.md` has the rule). A script that has a build directory and not the binary reads the same string
from `nulldc3-version.txt` there. A command line that is refused (a one-dash switch, an unknown one, `--portable`
without a directory, a directory that cannot be written, ...) prints one line `nulldc3: <why>` on stderr and exits
with code 2 before a machine is made.
