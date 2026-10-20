nulldc3: the platform shell
===========================

What the emulator needs around `libswirl/` to be built and run on each system. The top-level `CMakeLists.txt`
names this directory `nulldc_shell_path`.

- `cmake/`: the CMake modules. `config.cmake` holds the build settings (the host system and processor, the
  compiler, the recompilers, the definitions and flags); the tests project takes it too, so that a test compiles
  libswirl as the emulator does. `version.cmake` makes the version at every build (below).
- `windows/`: the Windows resources, `nulldc3.rc` (the icon and the version information) and `resource.h`.
- `linux/`: the Linux desktop files: `nulldc3.desktop`, the man pages `man/nulldc3.1` and
  `man/nulldc3-joyconfig.1`, the controller mappings of `mappings/` and `tools/nulldc3-joyconfig.py`, which
  makes them.
- `apple/`: the macOS sources (`main.m`, the application delegate, the menus, the GL view, the console, the input
  devices) and the bundle files, `Info.plist` and `build_app_bundle.sh`.
- `icon/`: the emulator's icon, the approved art (`nulldc.svg`, `nulldc-small.svg` for 16 and 24 px, `png/` from 16
  to 1024 px, `preview.png`) and `make_icons.py`, which makes from the PNGs the three icon files of the tree:
  `windows/nulldc3.ico`, `apple/nulldc3.icns` and, outside this directory, `libswirl/linux-dist/x11_icon.h`, the
  icon the X11 window sets as `_NET_WM_ICON` with the class `nulldc3` (`WM_CLASS`, which `StartupWMClass` of
  `linux/nulldc3.desktop` names).
  The three are committed; `make_icons.py --check` (the test `icons.generated`) holds them to the PNGs.
  `icon/README.md` says what each file is, how the PNGs were made and how to run the script.
- `emscripten/`: the scaffolding of the Emscripten port: its compiler and linker flags (`Makefile`), its page
  (`ui.html`) and the `emu.cfg` it packs.

`LICENSE` is the BSD licence of the files here.

The version
-----------

`version-base.txt` holds the base, `3.0.0-pre`. `cmake/version.cmake` makes the version from it and from git, by
the rule the UI (`scripts/version.ts` of dreamcast-devtools) follows as well:

| the checkout | the version |
| --- | --- |
| on the tag `v<T>`, clean | `<T>` (`3.0.0-pre`) |
| N commits after it, clean | `<T>+N.g<h>` (`3.0.0-pre+2.g1a2b3c4d5e`) |
| on the tag, a tracked file changed | `<T>+dirty` |
| N after it, a tracked file changed | `<T>+N.g<h>.dirty` |
| no tag reachable (none exists yet; a shallow clone), or a describe output of another form | `<base>+g<h>`, `.dirty` behind it when dirty |
| no git, or not the top of a checkout (an archive, a copy inside another repository) | `<base>+nogit` |

`<h>` is the first 10 characters of `git rev-parse HEAD`, always 10. The tag is found only by `git describe --tags
--long --match v[0-9]*`, read from the right; no other tag counts. A tracked file changed means that `git status
--porcelain --untracked-files=no` says something (a submodule at another commit included); untracked and ignored
files never count. A clone, a worktree and a submodule are checkouts. The comment at the top of `version.cmake` has
the rule in full; `tests/version` tests it (`version.derive`), that the version is made at the build and not at the
configure (`version.build_time`), and the emulator's `--version` (`version.binary`).

The emulator's build runs it at every build (the target `nulldc3_version`, which `nulldc3` depends on; some 30 ms)
and it writes these three files into the build directory, never into the tree, each only when its text changes:

- `generated/nulldc3_version.h`, plain defines (no `#include`, no `#pragma`: the Windows resource compiler reads it
  too): `NULLDC_VERSION`, the version; `NULLDC_VERSION_NUMBERS`, `X,Y,Z,N` (`3,0,0,0`), Windows' `FILEVERSION` and
  `PRODUCTVERSION`; `NULLDC_VERSION_DOTTED`, `"X.Y.Z.N"`, macOS' `CFBundleVersion`; `NULLDC_VERSION_SHORT`,
  `"X.Y.Z"`, macOS' `CFBundleShortVersionString`; `NULLDC_GIT_HASH`, the full hash (`""` without a checkout);
  `NULLDC_GIT_DIRTY`, 1 or 0. X.Y.Z are the numbers at the start of the version itself (the tag's when one is
  reachable, else the base's), N the commits since the tag (0 on it and without one), at most 65535.
- `nulldc3-version.txt`: the version, one line; what release files are named after.
- `nulldc3-version.cmake`: the same six values as CMake variables, for a step of the build that runs after the
  version's (`include()` it).

One C++ file includes the header: `libswirl/version.cpp`, the only one with the generated directory on its include
path, which defines `nulldc_version()`, `nulldc_git_hash()` and `nulldc_git_dirty()`; `libswirl/nulldc_version.h`
declares them and `nulldc_title()` and includes nothing. A new commit, tag or change compiles `version.cpp` again
and relinks, and nothing else. `libswirl/version_title.cpp` defines `nulldc_title()`, `VER_EMUNAME` and the version
("nullDC 3.0.0-pre+g<h>"), which `VER_SHORTNAME` and `VER_FULLNAME` of `types.h` are: the window title. What
reads the version: `--version` (`nullDC <version>`) and `--help`'s list of switches; `debugger.describe`'s
`emulator.version`; the register `dc.BUILD` (`nullDC <version>`); the window title, and while the debugger holds
the machine `nullDC <version> [paused by debugger]`; the page the debugger serves without an embedded UI (`nullDC
<version> debugger`); the imgui About tab (the version, the hash with `-dirty`) and the welcome screen. The tests
project and the project of `tests/debugger/net` run the same script into their own build directories, under the
targets `nulldc_tests_version` and `dbg_net_version`.
