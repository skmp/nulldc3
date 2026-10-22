# nullDC 3.0.0

nullDC 3.0.0 is a Sega Dreamcast emulator made to aid development: of Dreamcast programs, of other Dreamcast
emulators, of reference renderers and of other Dreamcast projects. This tree is a fork of reicast (it was named
devcast before), with the SH4 on a recompiler or an interpreter, the ARM7 and the AICA, the PVR with its reference
renderers, and reios in place of the BIOS. It adds a debugger that runs inside the emulator and serves a web UI,
Dreamcast DevTools (the submodule `devtools/`, repository `dreamcast-devtools`): breakpoints and steps on the SH4 and
the ARM7, the registers of the devices, guest memory; and a test suite, `tests/`.

## Downloads

The Releases of `skmp/nulldc3` on GitHub have four files for each version (and `SHA256SUMS`), each with the Dreamcast
DevTools UI of the same version built in:

| file | what it needs |
|---|---|
| `nulldc3-<version>-linux-x86_64.AppImage` | Linux x86_64 with glibc 2.39 or later (Ubuntu 24.04 and newer) and SSE4.1; FUSE 3 (`fusermount3`) to start it, or `--appimage-extract-and-run` |
| `nulldc3-<version>-linux-arm64.AppImage` | Linux arm64 with glibc 2.39 or later and a kernel with 4 KiB pages; FUSE 3, or `--appimage-extract-and-run` |
| `nulldc3-<version>-windows-x86_64.zip` | Windows 10 1803 or later, 64-bit: the folder `nulldc3-<version>-windows-x86_64/` with `nulldc3.exe` |
| `nulldc3-<version>-macos-arm64.zip` | macOS 11 or later on Apple silicon: the folder `nulldc3-<version>-macos-arm64/` with `nulldc3.app` |

Every one is portable by default ("Where the files are" below): the folder that holds `nulldc3.exe` or `nulldc3.app`,
and for an AppImage the folder that holds the `.AppImage` file. None is signed by a publisher: Windows' SmartScreen
asks once ("More info", "Run anyway"), and macOS refuses a downloaded `nulldc3.app` until it is allowed in System
Settings, Privacy & Security ("Open Anyway") or the quarantine of its unpacked folder is cleared (`xattr -dr
com.apple.quarantine nulldc3-<version>-macos-arm64`; before that macOS runs it from a read-only place, App
Translocation, and nullDC says it cannot keep its files there). Each file carries five licences: `LICENSE` (the
emulator's), `LICENSE-GPL` and `LICENSE-LGPL` (the GNU licences that parts of it are under), `LICENSE-shell` (the BSD
licence of the platform shell, `nulldc3/LICENSE`) and `LICENSE-devtools` (the MIT licence of the UI): in the AppImage
under `usr/share/doc/nulldc3/`, in the Windows and macOS folders beside the program (and in
`nulldc3.app/Contents/Resources`). The tag of a Release, in this repository and in `dreamcast-devtools`, is its
source.

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

## Quick start (Windows)

You need Windows 10 1803 or newer (x86_64), Visual Studio 2026 with the workload "Desktop development with C++" (it
brings MSVC, CMake and Ninja; the Windows build is made and tested with it), Git for Windows and Node.js 22.13 or
newer for the UI. Everything below runs in the "x64 Native Tools Command Prompt" of Visual Studio (or a prompt where
its `vcvars64.bat` was called).

Get the source and build the UI as on Linux:

    git clone https://github.com/skmp/nulldc3.git
    cd nulldc3
    git submodule update --init devtools
    cd devtools && npm ci && npm run build:native && cd ..

The checkout keeps every file as it is committed (`.gitattributes`), whatever `core.autocrlf` says.

Build the emulator with the CMake and the Ninja of Visual Studio:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    ninja -C build

`build\nulldc3.exe` is the emulator, one file: it has the static C runtime (no redistributable beside it), its icon
and its version (Explorer's Properties, Details). It is a console program: started from a terminal it writes there,
started from Explorer Windows gives it a console beside its window, where the log is. It is portable as on Linux: its
files are in the directory of `nulldc3.exe` (here `build\`), or in the one of `--portable <dir>`; `--shared` keeps
them in `%APPDATA%\nulldc3`. Run it as on Linux, and open the UI at `http://127.0.0.1:55543/`:

    build\nulldc3.exe <game>

`config:Headless=yes` works on Windows too (no window, no dialog of any kind), and so does every key of
`docs/nulldc3/automated-runs.md`, which says how an automated run on Windows goes (`tests/winrun`).

## Quick start (macOS)

You need macOS 11 or newer on Apple silicon (arm64), Xcode (the build is made and tested with Xcode 26; it brings the
compiler, git and codesign), and CMake, Ninja and Node.js 22.13 or newer for the UI, from Homebrew:

    brew install cmake ninja node

Get the source and build the UI as on Linux. Build the emulator:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    ninja -C build

On macOS the build uses no ccache unless the configure line asks for it, `-DNULLDC_CCACHE=ON` (as on Linux it uses
the one it finds): a ccache that cannot start, as Homebrew's after an upgrade has taken a library it links, would end
every compile in an abort and a crash report.

`build/nulldc3.app` is the emulator, an application bundle: its executable `Contents/MacOS/nulldc3`, its icon and the
licences in `Contents/Resources`, its version in `Contents/Info.plist`, signed ad hoc by the build (no Developer ID,
not notarized). It is portable: its files are in the folder that holds `nulldc3.app` (here `build/`), or in the one of
`--portable <dir>`; `--shared` keeps them in `~/Library/Application Support/nulldc3`. Open it from the Finder, or run
its executable from a terminal with a game, and open the UI at `http://127.0.0.1:55543/`:

    build/nulldc3.app/Contents/MacOS/nulldc3 <game>

Scripts, tests and CI run that executable by its path, never with `open`: `config:Headless=yes` works on macOS too (no
window, no Dock icon, no dialog of any kind), and `docs/nulldc3/automated-runs.md` says how an automated run on the Mac
goes (`tests/macos/macrun.py`). For an IDE, `cmake -S . -B build-xcode -G Xcode` makes an Xcode project of the same
build, and Xcode signs the bundle ad hoc itself.

A copy of `nulldc3.app` that was downloaded has no signature Gatekeeper knows: open it the first time with the
Finder's Open in its context menu (or allow it in System Settings, Privacy & Security), and move its folder with the
Finder before (or clear its quarantine, `xattr -dr com.apple.quarantine <folder>`): opened where it was unpacked,
macOS runs it from a read-only place (App Translocation), where it cannot keep its files, and the emulator says so.

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
    --console, --log    Windows only: --console is accepted and does nothing (nulldc3.exe is a console
                        program); --log puts the output into nulldc3-log.txt in the emulator's directory

The keys for runs without a person (headless, the conditions that end a run, the state hash) are in
`docs/nulldc3/automated-runs.md`; the debugger's keys (section `Debugger`) are at the top of
`libswirl/debugger/debugger.h`.

## Where the files are

nullDC is portable by default: everything it reads and writes (the configuration, the BIOS and the flash, the VMUs,
the input mappings, the save states) lives in the directory of its executable (on macOS, the directory that holds
`nulldc3.app`; on Linux the directory of the file `/proc/self/exe` names, so a symbolic link to the executable uses
the directory of the file it points at; started from an AppImage, the directory that holds the `.AppImage` file, since
the AppImage itself is a read-only image). The working directory plays no part. `--portable <dir>` names another
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

## CI

`.github/workflows/ci.yml` runs on GitHub-hosted runners for every pull request, for the push of a tag `v*`, and by
hand (`workflow_dispatch`); a push to a branch starts nothing. Its jobs only provision a runner and call the scripts
of `nulldc3/ci/`, which run the same on a machine of one's own (each says how at its top). On a Windows machine or a
Mac of one's own nothing they built runs outside the envelope of that system: `windows.ps1` runs the tests project's
build, ctest and the smoke run inside `nulldc3-winrun`, and `macos.sh` runs off a runner only inside the profile of
`tests/macos/macrun.py` (it stops otherwise: start it with `macrun.py --detach ... -- nulldc3/ci/macos.sh ...`):

| job | runner | what it does |
|---|---|---|
| `ui` | `ubuntu-24.04` | `linux.sh ui`: `npm ci` and `npm run build:native` in the submodule `devtools/`; the bundle `dist-native` is the artifact every release file embeds |
| `linux-x86_64`, `linux-arm64` | `ubuntu-24.04`, `ubuntu-24.04-arm` | `linux.sh deps` (apt), `linux.sh build` (the release emulator, Release, the UI embedded, `-DNULLDC_PREFIX_MAP=ON`; and the `-DHAS_TA_LLE=OFF` emulator), `linux.sh test ci` (the tests project on both, with the data public repositories give: the vector submodules, the UI with `npm ci`, the SH and ARM binutils of Ubuntu (the SH one on x86_64 alone); the tests that need the KallistiOS programs or the RTL checkouts are skipped), `appimage.sh` (the AppImage and its smoke run) |
| `windows-x86_64` | `windows-2025` | `windows.ps1 -Mode ci` (Windows PowerShell 5.1): Visual Studio's newest MSVC, the release `nulldc3.exe`, the tests project with ctest inside `nulldc3-winrun`, the zip made with `tar.exe`, its smoke run |
| `macos-arm64` | `macos-26` | `macos.sh ci`: the release `nulldc3.app`, `codesign --verify`, the tests project (the emulator through `tests/macos/macrun.py`), the zip made with `ditto`, its smoke run |
| `release-check` | `ubuntu-24.04` | pull requests and dispatches: `release.sh check`, the four files named with the version, each configure line naming the `ui` job's UI, `SHA256SUMS` and the notes written, and the `gh` commands a tag would run printed; it creates nothing |
| `release` | `ubuntu-24.04` | the push of a tag alone, the only job that can write: `release.sh publish`, the version, the base `nulldc3/version-base.txt`, the UI's version and the four configure lines all the tag's; a draft Release with the four files and `SHA256SUMS`, its assets checked, then published (a pre-release for a version with a pre-release part) |

Every job's skipped tests are listed with their reasons in its summary; every artifact (the four files, each with its
configure line, the UI bundle, the would-be notes) is kept 7 days. A dispatch takes the tag it checks against (`tag`)
and whether the UI must be the tagged one (`require_ui_tag`), and makes nothing. The smoke run of each file, on its
own runner, starts it as a user would (the AppImage with `APPIMAGE_EXTRACT_AND_RUN=1`): `--version`, a headless run of
the guest of `tests/debugger/guest/dsptest`, and one with the debugger, whose `debugger.describe` has to name the
version and the UI. `tests/ci` holds the rules of the workflow (`ci.workflows`), the check that no tracked file
names a machine (`ci.hygiene`) and the tests of the Windows zip's entry check and of the smoke run's ending
(`ci.scripts`); `actionlint` checks the file's syntax.

## Tests and developer documents

- `tests/README.md`: the test suite, a CMake project of its own (`cmake -S tests -B <build dir> -G Ninja`), with
  what each test needs and how to give it; `tests/system/README.md` for the tests that run the emulator itself.
  Every run of the emulator there has a directory of its own (`--portable`): a test run writes nothing beside the
  binary under test.
- `docs/nulldc3/README.md`: the plan and the design of nullDC 3.0.0 and of its debugger.
- `devtools/README.md`: Dreamcast DevTools, its build, its tests and its mock emulator.
