# nullDC 3.0.0

nullDC 3.0.0 is a Sega Dreamcast emulator made to aid development: of Dreamcast programs, of other Dreamcast
emulators, of reference renderers and of other Dreamcast projects. This tree is a fork of reicast (it was named
devcast before), with the SH4 on a recompiler or an interpreter, the ARM7 and the AICA, the PVR with its reference
renderers, and reios in place of the BIOS. It adds a debugger that runs inside the emulator and serves a web UI,
Dreamcast DevTools (the submodule `devtools/`, repository `dreamcast-devtools`): breakpoints and steps on the SH4 and
the ARM7, the registers of the devices, guest memory; and a test suite, `tests/`.

## Quick start (Linux)

You need git, CMake 3.19 or newer, Ninja, a C++20 compiler (GCC), the development files of X11 and OpenGL (ALSA,
PulseAudio, libevdev, libudev and curl are used when they are there), and Node.js 22.13 or newer for the UI.

Get the source, with the UI:

    git clone https://github.com/skmp/nulldc3.git
    cd nulldc3
    git submodule update --init devtools

The submodule's URL is relative (`../dreamcast-devtools.git`), so the UI comes the way the emulator was cloned: over
https as above, or over ssh after `git clone git@github.com:skmp/nulldc3.git`.

Build the UI (it writes `devtools/dist-native/`; the emulator's build runs no npm):

    (cd devtools && npm ci && npm run build:native)

Build the emulator (configure finds `devtools/dist-native` and embeds it; it says so in a line that begins with
`Dreamcast DevTools:`):

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    ninja -C build

Run it, and open the UI in a browser at `http://127.0.0.1:55543/` (the emulator prints `Debugger: http://...` when
the server is up; it listens on the loopback address only):

    ./build/nulldc3 <game>

A game is a disc image (`.gdi`, `.chd`, `.cdi`, `.cue`, `.iso`, `.lst`), a program that runs under reios (`.elf`,
`.bin`), or `nodisk`; without one the emulator starts at its menu. The BIOS and the flash of a Dreamcast,
`dc_boot.bin` and `dc_flash.bin`, go into the subdirectory `data/` of the emulator's directory (below); without them
the emulator boots with reios, its replacement BIOS.

## Switches

Every switch is written with two dashes. A word that begins with a dash is always a switch: an unknown one, or one
written with one dash, is an error that says so (exit status 2, nothing is started). `nulldc3 --help` lists them.

    --config section:key=value [ , section:key=value ]...
                        values of the configuration for this run, not written to emu.cfg; the comma between
                        two of them is a word of its own (blanks around it)
    --portable <dir>    keep the emulator's files in <dir> (made when it is missing) instead of the directory
                        of the executable
    --shared            keep them in the per-user directories of the system instead (below)
    --help              the usage text
    --version           the version, "nullDC <version>": 3.0.0-pre+g<first 10 characters of the commit> until
                        the tag v3.0.0-pre is reachable (nulldc3/README.md has the rule)
    --                  every word after it is a game, also one whose name begins with a dash
    --console, --log    Windows only: a console window for the output; the output into nulldc3-log.txt in
                        the emulator's directory

The keys for runs without a person (headless, the conditions that end a run, the state hash) are in
`docs/nulldc3/automated-runs.md`; the debugger's keys (section `Debugger`) are at the top of
`libswirl/debugger/debugger.h`.

## Where the files are

nullDC is portable by default: everything it reads and writes (the configuration, the BIOS and the flash, the VMUs,
the input mappings, the save states) lives in the directory of its executable (on macOS, the directory that holds
`nulldc3.app`; on Linux the directory of the file `/proc/self/exe` names, so a symbolic link to the executable uses
the directory of the file it points at). The working directory plays no part. `--portable <dir>` names another
directory; it is made when it is not there. A directory that cannot be written is an error at start that names
`--portable <dir>` and `--shared`, never a silent change of place.

The layout is the same in every mode:

    emu.cfg                   the configuration
    mappings/                 the input mappings
    vmu_save_A1.bin ...       the VMU images, vmu_save_<port><slot>.bin
    data/dc_boot.bin          the BIOS and the flash of a Dreamcast, yours to put there (reios runs programs
    data/dc_flash.bin         without them)
    data/dc_nvmem.bin         the flash as the emulator writes it back
    data/<game>.state         the save states
    data/textures/<game id>/  custom textures, read; data/texdump/ textures dumped by the GL renderer
    data/dumps/               the debugging dumps of the renderers: refsw2-dump-<time>/ (refsw2,
                              refmister), vram0.bin, vram1.bin and pvr_regs (refsw)
    nulldc3-log.txt           Windows, with --log

`--shared` uses the per-user directories of the system, named `nulldc3`: on Linux `$XDG_CONFIG_HOME/nulldc3`
(`~/.config/nulldc3`) for `emu.cfg` and `mappings/`, and `$XDG_DATA_HOME/nulldc3` (`~/.local/share/nulldc3`)
for the rest, with the system directories of `$XDG_CONFIG_DIRS` and `$XDG_DATA_DIRS` (`/etc/xdg/nulldc3`,
`/usr/local/share/nulldc3`, `/usr/share/nulldc3`) read behind them; on Windows `%APPDATA%\nulldc3`; on macOS
`~/Library/Application Support/nulldc3`. An emulator installed where it cannot write, such as `/usr/bin`, is run
with `--shared`. The directories of reicast (`~/.reicast`, `~/.config/reicast`, ...) are not read.

## Tests and developer documents

- `tests/README.md`: the test suite, a CMake project of its own (`cmake -S tests -B <build dir> -G Ninja`), with
  what each test needs and how to give it; `tests/system/README.md` for the tests that run the emulator itself.
  Every run of the emulator there has a directory of its own (`--portable`): a test run writes nothing beside the
  binary under test.
- `docs/nulldc3/README.md`: the plan and the design of nullDC 3.0.0 and of its debugger.
- `devtools/README.md`: Dreamcast DevTools, its build, its tests and its mock emulator.
