# System tests

The emulator binary itself, started headless on a guest program, and what it prints and returns. The other suites
of this project link translation units of libswirl against stubs; these run the whole thing: reios, the SH4 on the
JIT and on the interpreter, the scheduler, the SPG, the renderers that work without GL, the command line and the
config, the shutdown.

`docs/nulldc3/automated-runs.md` has the `-config` keys the tests use (headless mode, the exit conditions, the exit
line with its state hash, the pinned RTC) and what a fatal fault does.

## Which emulator

Two builds are tested: the default one (`system.*`) and the one without the TA of lxdream, `-DHAS_TA_LLE=OFF`
(`system.hle.*`), which has the GL renderers instead of the software reference renderers.

By default the tests project **builds both from the tree it is in**, into `<build dir>/system/emulator` and
`<build dir>/system/emulator-ta-hle`, every time it is built (`tests/system/build_emulator.cmake`; the output of the
two builds is in `<build dir>/system/emulator.log` and `emulator-ta-hle.log`, and on the console when one fails).
A test run therefore tests the sources as they are, with no step to forget. The first build takes about a minute
per emulator, later ones a few seconds. The emulator is built with the default C and C++ compilers, whatever the
tests are configured with (it links with GCC only).

    -DNULLDC_EMULATOR=<binary>           test this binary as the default build, and do not build one
    -DNULLDC_EMULATOR_TA_HLE=<binary>    the same for the -DHAS_TA_LLE=OFF build
    -DNULLDC_SYSTEM_TESTS=OFF            no emulator is built, the system tests are reported as skipped

The tests need Linux (headless mode) and `python3`; without them they are reported as skipped.

## Guest programs

* Three programs of a few instructions each are part of the suite: `guest/guest_exit.s` (prints a line on the serial
  port, loops, returns to the BIOS), `guest/guest_spin.s` (prints a line, then counts in a word of RAM for ever)
  and `guest/guest_rtc.s` (prints the real-time clock). `system_test.py` has them assembled, as raw images that
  reios loads at 8C010000, so the tests need no toolchain. After a change of a source:

      sh-elf-as --little --isa=sh4 -o guest_exit.o guest/guest_exit.s
      sh-elf-ld -EL -Ttext=0x8c010000 -e start -o guest_exit.elf guest_exit.o
      sh-elf-objcopy -O binary -j .text guest_exit.elf guest_exit.bin      # -j .text: the image is 68 bytes
      xxd -p guest_exit.bin | tr -d '\n'                                   # goes into GUESTS of system_test.py

* `2ndmix.elf` and `hello.elf`, the KallistiOS examples, are referenced by path: `-DNULLDC_ELF_DIR=<directory with
  both>`. Without it the tests that boot them are skipped, or fail with `-DNULLDC_TESTS_REQUIRE_DATA=ON`, like every
  test of this project whose data is missing. Nothing is copied.

## The tests

Every test is `system_test.py --case <case>` on one of the two emulators.

| test (`system.` and `system.hle.`) | what has to hold | needs |
|---|---|---|
| `help` | `-help` prints the usage text and exits with 0 | |
| `guest_exit.jit` `guest_exit.interp` | a program that returns to the BIOS ends the run: its text is on stdout, `reason=guest-exit`, exit code 0, the engine is the one that was asked for | |
| `exit.vblanks.jit` `exit.vblanks.interp` | `Exit.AfterVblanks=20` ends a program that never ends: `reason=vblanks vblanks=20`, the emulated time of 20 vblanks, a wall time and a speed, a state hash | |
| `exit.seconds` | `Exit.AfterSeconds=0.25` ends the run at the first vblank from 0.25 emulated seconds on; no hash that was not asked for | |
| `rtc` | with `Dreamcast.RTC` the guest reads exactly that clock, on both engines; another value is another clock; without the key it is the time of the host | |
| `cfg.untouched` | a headless run with `-config` overrides leaves an `emu.cfg` byte for byte as it was, and writes no settings into a new one | |
| `headless.no_program` | headless with nothing to run: exit code 1 and `reason=error` | |
| `headless.gl_renderer` | a renderer that needs GL (`refsw` in the default build, `gles` in the other) is refused: exit code 1 and `reason=error` | |
| `fault.segv` `fault.bus` `fault.ill` | a fault that is not a guest memory fault ends the process by that signal, with the `fault_handler: unhandled ...` line, and what was printed before it is in the output | |
| `fault.on_request` | the fault of these tests comes when the test asks for it and not before: a second after the text of the guest the process still runs and has printed no `fault_handler` line; asked for, the fault ends it, behind that text | |
| `fault.block` | with `NULLDC_FAULT_BLOCK=1`, and with `config:Debug.BlockOnFault=yes`, the process stays and prints `fault_handler: Blocking` | |
| `fault.sent` | a SIGSEGV that is sent to a run is not taken for a fault of the guest: `kill -SEGV` on a run on the JIT and on one on the interpreter, and a `raise(SIGSEGV)` on a thread inside the process, each end the run by SIGSEGV within 10 s, with the one line `fault_handler: SIGSEGV sent by pid <the sender> (si_code <0 for kill, -6 for raise>), not a fault: terminating`; with `NULLDC_FAULT_BLOCK=1` the process stays and says `blocking for a debugger` | |
| `fault.guest` | the faults of the guest are still handled: a run of `guest_spin` on the JIT takes SIGSEGVs the kernel raises for a fault (counted on their way to the handler of the emulator: at least one, 6 on an x86-64 host), and ends at its 20 vblanks with exit code 0 and no `fault_handler` line | |
| `boot.2ndmix.jit` `boot.2ndmix.interp` | 2ndmix boots under reios with the `none` renderer and runs for 600 vblanks: exit code 0, `reason=vblanks vblanks=600`, the serial port shows the KallistiOS banner, `2ndMix/KallistiOS starting` and `Starting display` | `NULLDC_ELF_DIR` |
| `boot.hello.jit` `boot.hello.interp` | the same for hello and 30 vblanks: the banner, `Hello world!`, `arch: exit return code 0` | `NULLDC_ELF_DIR` |
| `determinism` | 2ndmix for 600 vblanks, 6 times on the JIT and 6 times on the interpreter, 4 runs at a time: every run of an engine has the same state hash, part for part. About 8 s | `NULLDC_ELF_DIR` |
| `perf.script` | `tests/perf/perf.py` on `guest_spin`, headless, half an emulated second and 3 runs per engine: it ends with 0, and its JSON has for each engine the speed by wall clock and the speed by CPU time of every run, with the median, the lowest and the highest of each; the speed by CPU time is the emulated time of the run over the CPU time of its process; the table it prints has both. No number is judged | |
| `frame.refsw2` (default build only) | refsw2 runs headless: after 240 vblanks of 2ndmix the exit line has the hash of a 640x480 frame, `Exit.FramePng` wrote a valid PNG of that size that is not an empty frame, and the run differs from the same run with the `none` renderer in VRAM and in nothing else of the state hash | `NULLDC_ELF_DIR` |

In every run that is expected to end by itself, the output must not have `Fatal error`, `Verify Failed` or
`fault_handler`.

The fault tests make the emulator fault with a library the project builds, `fault_shim.so`, preloaded into the
emulator: once the emulator has installed its handler for the signal and the test has created the file the library
waits for, a thread of the library writes to a page nothing knows (SIGSEGV), to a mapping behind the end of its
file (SIGBUS), or executes an undefined instruction (SIGILL). A test creates the file when the text of the guest
is in the output of the run: the fault is then in a machine that runs, with its config loaded and that text in
front of it, however long the emulator took to get there. `fault.sent` sends its signals at the same point, with
`kill` from the test and with `raise` from the thread of the library.

For `fault.guest` the library counts instead of faulting. Its `sigaction` is found in front of the one of libc, so
the handler the emulator installs for SIGSEGV sits behind one that counts the signals with a `si_code` above 0,
the ones the kernel raises for a fault, into a file. Every guest memory fault is two of them: the fault, and the
write to a page of its own with which the handler comes back from the work it does outside the signal.

hello returns from `main` into its own start-up code (KallistiOS returns to the PR it was started with, which it
keeps on the stack it then uses) and stays there, at about a quarter of real time on the JIT; that is the program,
not the emulator, and the reason its run is cut after 30 vblanks.

## Determinism

A run is the same every time, to the bit of its state hash, when it is made the way `system.determinism` makes
it: **headless, `config:Dreamcast.RTC` set, `audio:backend=none`**, a renderer that does not need GL. That is what a
test that compares state has to use. The JIT and the interpreter each repeat themselves; they do not agree with
each other (`docs/nulldc3/automated-runs.md`, the exit line).

What was measured for that (2026-10-05, 2ndmix and hello, runs in parallel batches of 12 to 16 on a machine that
was busy with other work, load 30 to 60 on 24 threads):

| run | runs | different state hashes |
|---|---|---|
| RTC not pinned, 2ndmix 600 vblanks, JIT | 400 | 73, different in `ram` only |
| RTC not pinned, 2ndmix 600 vblanks, interpreter | 400 | 122, different in `ram` only |
| 2ndmix 600 vblanks, JIT / interpreter, default build | 1400 / 1400 | 1 / 1 |
| 2ndmix 4000 vblanks, JIT / interpreter, default build | 400 / 400 | 1 / 1 |
| 2ndmix 600 vblanks, JIT / interpreter, `-DHAS_TA_LLE=OFF` build | 400 / 400 | 1 / 1 |
| hello 60 vblanks, JIT / interpreter | 400 / 400 | 1 / 1 |
| 2ndmix 300 vblanks with refsw2, JIT, headless | 24 | 1 |

What makes two runs of the same program differ, and what was done about it:

* **The real-time clock.** It is loaded from the time of the host at every reset, KallistiOS reads it while it
  boots and keeps it in RAM: two runs that start in different seconds differ from there on (the first two rows:
  as many hashes as seconds the runs started in). `config:Dreamcast.RTC` pins it, and with it the time in the flash
  system block and the clock of a VMU.
* **Input through the window.** With a window, a key or a mouse button that reaches it is input of the first
  controller. A synthetic Return key (START) sent to the window of a run of 2ndmix made the demo return from `main`,
  where the same run without the event, and the headless run, ended in the same state hash. This is what a run
  on a desktop that is in use picks up now and then. Headless mode sets up no host input at all.
* **Uninitialised members**, found with valgrind (memcheck, interpreter): the time of a pending reconnect of the
  maple bus, which its vblank compares with the scheduler time on every frame, and the state of a `cResetEvent`
  on POSIX hosts. Both are initialised now. Forcing the first to a value that triggers (the devices are created a
  second time) did not change the state hash of 2ndmix; it is fixed because nothing says that holds for every
  program. With the two fixes memcheck reports no error for hello (30 vblanks) and 2ndmix (200 vblanks; `none` in
  both builds, refsw2), and each of those runs under valgrind, a tenth as fast, ends in the state hash of the
  native run.

What was checked and does not reach the guest:

* The pace of the frames and the hand-over of frames to the thread of the user interface: a run with a window ends
  in the state hash of the headless run with the same renderer (`none` in both builds; refsw2, 16 runs; and with
  `gles` on a GPU in the `-DHAS_TA_LLE=OFF` build the hash is the one of `none`).
* The audio backend: the AICA pushes samples to it and uses nothing that comes back; `aica.LimitFPS` only makes
  the push wait (by reading `oslib/audiostream.cpp`, not by measurement: a real backend plays sound).
* What the heap holds (glibc `MALLOC_PERTURB_` with four fill patterns), where the stack is (5000 bytes more of
  environment), where the address space is (ASLR, on in every run), how loaded the machine is.

What is left:

* With a window and a software renderer of the default build, the render runs on the thread of the user interface
  from the write of STARTRENDER until the emulator thread waits for it at the end of the render time; in between
  it reads the lists and textures and writes the framebuffer in VRAM while the guest runs. No run of 2ndmix showed
  a difference from it. Headless mode renders on the emulator thread, at STARTRENDER.
* The hashes say little about the sound side: 2ndmix leaves AICA RAM the same at 600 and at 4000 vblanks.
* The JIT and the interpreter end in different states on a program that depends on time.

## How a run is made

`system_test.py` gives every run of the emulator an empty environment but for `HOME`, `XDG_CONFIG_HOME` and
`XDG_DATA_HOME`, which are fresh directories under `<build dir>/system/work/<test>/`: no `DISPLAY`, nothing of the
user's config. A core file size limit of 1 byte keeps a crash from being piped to a core handler (WSL2). A
wall-clock watchdog (120 s per run, `--watchdog`) kills a run that does not end and fails the test; ctest has a
second one behind it. The directories of a failed test are kept, with the output of every run in `log.txt`; the
failure message has the command line and the end of that output.

    python3 tests/system/system_test.py --emulator <binary> --case boot.2ndmix.jit --work /tmp/w \
            --elf-dir <dir> --keep
