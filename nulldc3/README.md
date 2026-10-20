nulldc3: the platform shell
===========================

What the emulator needs around `libswirl/` to be built and run on each system. The top-level `CMakeLists.txt`
names this directory `nulldc_shell_path`.

- `cmake/`: the CMake modules. `config.cmake` holds the build settings (the host system and processor, the
  compiler, the recompilers, the definitions and flags); the tests project takes it too, so that a test compiles
  libswirl as the emulator does. `GetGitRevisionDescription.cmake` and its `.in` give the build its version from
  `git describe`.
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
