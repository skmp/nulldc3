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
| `fault.block` | with `NULLDC_FAULT_BLOCK=1`, and with `config:Debug.BlockOnFault=yes`, the process stays and prints `fault_handler: Blocking` | |
| `boot.2ndmix.jit` `boot.2ndmix.interp` | 2ndmix boots under reios with the `none` renderer and runs for 600 vblanks: exit code 0, `reason=vblanks vblanks=600`, the serial port shows the KallistiOS banner, `2ndMix/KallistiOS starting` and `Starting display` | `NULLDC_ELF_DIR` |
| `boot.hello.jit` `boot.hello.interp` | the same for hello and 30 vblanks: the banner, `Hello world!`, `arch: exit return code 0` | `NULLDC_ELF_DIR` |
| `frame.refsw2` (default build only) | refsw2 runs headless: after 240 vblanks of 2ndmix the exit line has the hash of a 640x480 frame, `Exit.FramePng` wrote a valid PNG of that size that is not an empty frame, and the run differs from the same run with the `none` renderer in VRAM and in nothing else of the state hash | `NULLDC_ELF_DIR` |

In every run that is expected to end by itself, the output must not have `Fatal error`, `Verify Failed` or
`fault_handler`.

The fault tests make the emulator fault with a library the project builds, `fault_shim.so`, preloaded into the
emulator: once the emulator has installed its handler for the signal, a thread of the library writes to a page
nothing knows (SIGSEGV), to a mapping behind the end of its file (SIGBUS), or executes an undefined instruction
(SIGILL).

hello returns from `main` into its own start-up code (KallistiOS returns to the PR it was started with, which it
keeps on the stack it then uses) and stays there, at about a quarter of real time on the JIT; that is the program,
not the emulator, and the reason its run is cut after 30 vblanks.

## How a run is made

`system_test.py` gives every run of the emulator an empty environment but for `HOME`, `XDG_CONFIG_HOME` and
`XDG_DATA_HOME`, which are fresh directories under `<build dir>/system/work/<test>/`: no `DISPLAY`, nothing of the
user's config. A core file size limit of 1 byte keeps a crash from being piped to a core handler (WSL2). A
wall-clock watchdog (120 s per run, `--watchdog`) kills a run that does not end and fails the test; ctest has a
second one behind it. The directories of a failed test are kept, with the output of every run in `log.txt`; the
failure message has the command line and the end of that output.

    python3 tests/system/system_test.py --emulator <binary> --case boot.2ndmix.jit --work /tmp/w \
            --elf-dir <dir> --keep
