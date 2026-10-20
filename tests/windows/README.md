# tests/windows

The tests of the emulator's headless mode and of its no-dialog policy on Windows, and of the run envelope that every
program of an automated run goes through there (`tests/winrun`). Only on Windows: on the other systems this
directory adds nothing. The emulator is `NULLDC_EMULATOR` (an emulator build's `nulldc3.exe`); without it the tests
that run it are missing data. The guest is the sounding guest of the tree, `tests/debugger/guest/dsptest/dsptest.elf`.

Every program is started through `nulldc3-winrun` (the one this project builds, or `NULLDC_WINRUN`), and every run
of the emulator carries the keys of an automated run: a portable directory under the test's work directory and
`--config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800` (the help box cases leave
`config:Headless` out, with a command line that is refused before anything runs). The cases are in `windows.cmake`;
each keeps the output of its runs in `<build>/windows/work/<test>/*.log`.

| test | what it checks |
|---|---|
| `windows.winrun.probe` | `nulldc3-winrun --timeout 10 -- nulldc3-winrun --probe`: the probe names a desktop `winrun-<n>`, the same nulldc3-winrun ran it on, its visible window is in nulldc3-winrun's list, and the verdict is 125 |
| `windows.winrun.detach` | `nulldc3-winrun --detach --log <file> --rc <file> --timeout 60 -- cmake -E sleep 15`, its output read through a pipe (CMake's): it returns within 5 s with the copy's process id (the copy inherits none of the caller's handles), and the copy writes 0 into the rc file once the program has run its 15 s, its log with nulldc3-winrun's summary line |
| `windows.headless.nowindow` | the guest 90 vblanks headless with `NULLDC_TEST_UI_REPORT=1`: exit 0, the exit line, and the line `nulldc-ui:` with `app=0 windows=0`, no `OleMainThreadWndClass` among the classes (no COM apartment), `wer=` with `WER_FAULT_REPORTING_NO_UI`, `invparam=1 purecall=1 sigabrt=1` (the handlers in force are the policy's) and `abort=` without `_WRITE_ABORT_MSG` and `_CALL_REPORTFAULT`; nulldc3-winrun saw no window |
| `windows.fatal.<kind>` | `NULLDC_TEST_FATAL=<kind>` for `segv`, `ill`, `trap`, `abort`, `div`, `stack`, `invparam` and `purecall`: the `fault_handler` line of the kind's signal on stderr and 128 + its number (139, 132, 134, 136, 139, 134, 134); for `trap` (`die()`) `Fatal error : NULLDC_TEST_FATAL` on stdout, no `fault_handler` line and 133; no window, no timeout, and nothing new in the crash-report places (`%LOCALAPPDATA%\CrashDumps`, the WER `ReportQueue` and `ReportArchive` of the user and of the machine) |
| `windows.helpbox` | the help and error box: a refused command line with its output to a pipe, then to `NUL` (exit 2, no window); with a console of its own (`--own-console`) and `config:Headless=yes` (exit 2 within 10 s, no window); with a console of its own and not headless, the box on the private desktop (`#32770` of `nulldc3.exe` in nulldc3-winrun's list, the verdict 125 at the 10 s timeout) |
| `windows.abi.xmm` | `Mainloop` and `RunBlock` of the x64 backend give `xmm6`-`xmm15` back as they found them (`abi_xmm.cpp`: an Xbyak thunk with ten canaries around each, on a machine of its own whose guest code uses twelve FPU registers) |
| `windows.binary.stdout` | no `\r` in the emulator's stdout (`--help`, and the guest's run with its exit line) |
| `windows.dsound.failed` | `audio:backend=directsound` with `NULLDC_TEST_DSOUND=fail` (DirectSound fails as on a system without a sound device; nothing of it is called) and `aica.LimitFPS=yes`: the line `DirectSound: DirectSoundCreate8 failed (0x88780078): no sound`, and the guest headless still reaches its 90 vblanks, exit 0 and the exit line, within 60 s (no sound is no wait for a sound device) |
| `windows.exitcode` | headless with nothing to run: exit 1 and the error line (`runctl_fail`) |

`docs/nulldc3/automated-runs.md` has the policy, the fatal line and the exit codes these check.
