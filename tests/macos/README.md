# tests/macos

The run envelope of the Mac, and the tests of the emulator's headless mode, its fatal ends and its bundle on macOS.
Only on macOS: on the other systems this directory adds nothing. The emulator is `NULLDC_EMULATOR`, the executable of
an emulator build's bundle (`<build>/nulldc3.app/Contents/MacOS/nulldc3`); without it the tests that run it are
missing data. The guest is the sounding guest of the tree, `tests/debugger/guest/dsptest/dsptest.elf`.

## macrun.py

    python3 macrun.py [--name <n>] [--work <dir>] [--timeout <s>] [--detach --log <file> --rc <file>] -- <program> [<arg>...]

Every emulator run on the Mac, and every long step of a gate there (a build, a ctest run), goes through it, so that
nothing of it can reach the desktop of the person who uses the Mac: no window, no Dock icon, no dialog.

- **The profile.** The program runs under `sandbox-exec -f no-windowserver.sb`, which allows everything but the Mach
  services of the window server, the window manager, the Dock and LaunchServices' check-in (the profile names them
  and says why). AppKit cannot connect under it and an application cannot check in, so no window, no Dock icon and
  no alert can appear. A profile binds the process and all of its descendants and cannot be loosened, so
  `macrun.py` first asks the kernel whether it runs under one already (`sandbox_check(getpid(), NULL, 0)` of
  libSystem, through `ctypes`, with the function's three fixed arguments only): not sandboxed, it applies the
  profile; sandboxed, it runs the program directly under the profile it inherited and says
  `macrun: inside the profile already` on stderr. That is the shape of a gate: a build or ctest runs under
  `macrun.py --detach`, and the tests of this directory call `macrun.py` again inside it. (The cut expected macOS to
  refuse a second profile inside one, `sandbox_apply: Operation not permitted`; macOS 26.4 takes this one inside
  itself, so the canary shows which way `macrun.py` went.)
- **It refuses** (exit 126, nothing is run) when the kernel cannot be asked, and when `/usr/bin/sandbox-exec` is
  missing and `CI` is not set in the environment. With `CI` set (a CI runner), the program runs without a profile
  and the line says so.
- **Its own process group**: the program starts in a new session (`setsid`); `--timeout <s>` (default 3600) ends
  the whole group with `SIGKILL`, and every process below the program in a group or a session of its own, and exits
  with 124; what is left of the group, and below it, when the program has ended is killed too, and so is all of it
  when `macrun.py` gets `SIGTERM`, `SIGINT` or `SIGHUP`. The processes below the program come from the kernel's
  process table (`sysctl` `kern.proc.all` through libSystem, checked on `macrun.py`'s own entry; `/bin/ps` is setuid
  root on macOS and cannot run under a profile, "Operation not permitted", so it is only the fallback): the tree is
  stopped (`SIGSTOP`) until it grows no more, then each of its groups and processes gets `SIGKILL`, again until
  nothing is left. That is what ends the program of a `macrun.py` inside a long step (a test in a ctest under
  `macrun.py --detach`): it is in a session of its own, and that `macrun.py` dies in the same sweep, before it could
  end it.
- **Its files**: `TMPDIR` is `<work>/tmp`, the program's id is in `<work>/run/<name>.pid` (`<work>`: `--work`, or
  the current directory). `__CFBundleIdentifier` is taken out of the program's environment: a program `macrun.py`
  runs was never started by LaunchServices (the emulator's alert rule asks it).
- **The exit status** is the program's, 128 + N when a signal N ended it, 124 for the timeout, 126 when nothing was
  run. The last line on stderr says which: `macrun: <name>: exit status <n>`, `... killed by signal <n> (<SIG>)`,
  `... timeout after <s> s`.
- **`--detach --log <file> --rc <file>`** for a step that outlives the ssh connection that starts it (a command of
  a non-pty ssh session that is killed leaves its processes running): a copy of `macrun.py` in a new session under
  `caffeinate -i` (no idle sleep while it runs; it changes no setting), its output into the log; it prints the
  copy's id and returns at once, and the copy writes the program's exit status into the rc file at the end (the rc
  file is removed first).

Python 3.9 and the standard library (`/usr/bin/python3` of Xcode is 3.9).

## The tests

Each case is in `macos.cmake`; every emulator run carries the keys of an automated run, a portable directory under
the test's work directory and `--config config:Headless=yes , audio:backend=none , config:Dreamcast.RTC=946684800`
(the refused command lines leave `config:Headless` out on purpose: the other conditions of the alert rule are what
they check, on a command line that is refused before anything runs). The output of each run is in
`<build>/macos/work/<test>/*.log`.

| test | what it checks |
|---|---|
| `macos.sandbox.canary` | `canary.c` through `macrun.py`: it tries to reach the window server without AppKit (`CGSessionCopyCurrentDictionary`) and prints the kernel's view of itself; under the profile it must not reach the window server, and the kernel must say sandboxed (`sandbox_check(pid, NULL, 0)`) and the window server's `mach-lookup` denied (`sandbox_check(pid, "mach-lookup", SANDBOX_FILTER_GLOBAL_NAME, "com.apple.windowserver.active")`; the SPI is declared in the file, as WebKit's `SandboxSPI.h` does). The test also asks the kernel whether its caller, ctest, is sandboxed, and `macrun.py` has to have applied the profile when it is not and to have run under the caller's when it is. A gate runs it twice: from a ctest that is not sandboxed, and from a ctest under `macrun.py --detach` |
| `macos.macrun.tree` | the shape of a gate's long step: `macrun.py --detach` with a timeout of 20 s around a ctest (a project the test writes into its work directory) whose one test runs `macrun.py` again around a sleeper of 600 s, which that `macrun.py` starts in a session of its own and whose command line names a file of the work directory. The sleeper has to run (its pid file, and `kill -0` of that pid), the step's rc file has to say 124 and its log `timeout after`, and the sleeper has to have ended with the step; one that outlived it is ended by the test, by its pid |
| `macos.headless.nowindow` | the guest 90 vblanks headless with `NULLDC_TEST_UI_REPORT=1`: exit 0 by an exit, the exit line, `nulldc-ui: app=0 windows=0` (no `NSApplication` was made), the ARM7 on the interpreter (`Using SCPU Interpreter`: no ARM7 recompiler is built) |
| `macos.fatal.segv` `.ill` `.trap` `.abort` | `NULLDC_TEST_FATAL=<kind>` (`segv` a write to address 16, which no mapping holds: a `SIGSEGV`, as on Linux; `ill` `udf #0`; `trap` `die()`; `abort` `abort()`): the `fault_handler` line of the signal on stderr and 128 + its number by an exit, not a signal (139, 132, 134), for `segv` with the address `0x10` (the fault handler, which takes the write for a possible guest memory fault first, has come back to it with its registers: a `SIGBUS` at the trap page's address is the handler reporting its own thunk), for `ill` with the instruction as both address and pc; for `trap` `Fatal error : NULLDC_TEST_FATAL` on stdout, no `fault_handler` line and 133; for every kind the line `Config dir is: <work>/portable/` that the start printed on stdout before the fault (stdout is line-buffered: a buffer that stdio still held would be lost at the `_exit`); nothing new in `~/Library/Logs/DiagnosticReports` (its names, before and 5 s after) |
| `macos.refused` | a refused command line (`--bogus`), not headless: with stderr to a pipe, exit 2 and the reason there; with stderr to `/dev/null`, exit 2 at once; headless with stderr to `/dev/null`, exit 2; each `nulldc-ui: app=0 windows=0` (no alert was made) |
| `macos.bundle.codesign` | the bundle of `NULLDC_EMULATOR`: `codesign --verify --strict --deep`, an ad-hoc signature without the hardened runtime and with the identifier `org.emudev.nulldc3`, `plutil -lint`, every key of `nulldc3/apple/Info.plist.in` and no other, and the three version keys against the build's `nulldc3-version.cmake` (`CFBundleShortVersionString` X.Y.Z, `CFBundleVersion` X.Y.Z.N, `NullDCVersion` the version); `Resources` holds the icon and exactly the licences that match the build: `LICENSE`, `LICENSE-GPL`, `LICENSE-LGPL`, `LICENSE-shell`, and `LICENSE-devtools` when the build embeds the UI (the build's `dbg_webui_data.cpp.args`), each equal to its source; nothing else in the bundle. A gate runs it also after a bare reconfigure of the emulator's build and a build |
| `macos.build.ccache` | the tests project's build (this one) and the emulator's (`NULLDC_EMULATOR`'s) compile and link through ccache exactly when their configure line has `-DNULLDC_CCACHE=ON` and a ccache was found (their `CMakeCache.txt`; the commands from `ninja -t commands`, or the `build.make` files of a Makefile build; an Xcode build has no launcher and is not looked at). Nothing is compiled, so a ccache that cannot start is never started |
| `macos.hashes.jit` | the state hashes of the SH4 on the recompiler of an arm64 Mac (`config:Dynarec.Enabled=yes`, the ARM7 on the interpreter): the guest 90 vblanks and `2ndmix.elf` of `NULLDC_ELF_DIR` 600 vblanks, each run through `macrun.py` headless with the keys of an automated run, `Using MCPU Recompiler`, exit 0, and `hash=` of its exit line the Mac's reference (`c9bb6e60601ab606`, `6ab1933550d37163`, in `macos.cmake`): what the arm64 JIT computes, on the Darwin fault context it runs on, which nothing else holds to a value (a change of that context changed these hashes alone). A hash is good for one host: these are compared with nothing of another. On a host that is not arm64 it is skipped; without `2ndmix.elf` its data is missing |
| `macos.icns.iconutil` | Apple's `iconutil -c iconset` reads `nulldc3/apple/nulldc3.icns` into its ten images, each at its size; `iconutil -c icns` makes an `.icns` of the PNGs of `nulldc3/icon/png`, and the test prints which elements it writes. Everything in the test's work directory |

`NULLDC_MACRUN`, set for the directories behind this one, is what `version.binary` (`tests/version`) starts the
emulator through on macOS.
