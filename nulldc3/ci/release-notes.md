nullDC {{version}} is a Sega Dreamcast emulator made to aid development: of Dreamcast programs, of other Dreamcast
emulators, of reference renderers and of other Dreamcast projects. It has a debugger that runs inside the emulator and
serves a web UI, Dreamcast DevTools, on `http://127.0.0.1:55543/` (loopback only): breakpoints and steps on the SH4
and the ARM7, the registers of the devices, guest memory. This is a pre-release of nullDC 3.0.0.

## The files

| file | what it is | what it needs |
|---|---|---|
| `{{linux_x86_64}}` | Linux x86_64, an AppImage | glibc 2.39 or later (Ubuntu 24.04 and newer), an x86_64 processor with SSE4.1; FUSE 3 (`fusermount3`) to start it, or start it with `--appimage-extract-and-run` |
| `{{linux_arm64}}` | Linux arm64, an AppImage | glibc 2.39 or later, a kernel with 4 KiB pages (16 or 64 KiB kernels, such as Asahi Linux or the Raspberry Pi 5's own, are refused at start); FUSE 3 (`fusermount3`), or `--appimage-extract-and-run` |
| `{{windows_x86_64}}` | Windows x86_64, `nulldc3.exe` in a folder | Windows 10 1803 or later, 64-bit; no redistributable (the C runtime is in the executable) |
| `{{macos_arm64}}` | macOS on Apple silicon, `nulldc3.app` in a folder | macOS 11 or later on Apple silicon (arm64) |
| `SHA256SUMS` | the SHA-256 of the four files | `sha256sum -c SHA256SUMS` |

Each file carries the Dreamcast DevTools UI of the same version, built in: the Release
[Dreamcast DevTools {{version}}](https://github.com/skmp/dreamcast-devtools/releases/tag/{{tag}}) has the UI by itself.

## Portable by default

nullDC keeps everything it reads and writes (the configuration, the BIOS and the flash, the VMUs, the input mappings,
the save states) in the folder that holds the program: the folder of `nulldc3.exe`, the folder that holds
`nulldc3.app`, and for an AppImage the folder that holds the `.AppImage` file. `--portable <dir>` names another
folder, `--shared` uses the per-user folders of the system. The BIOS and the flash of a Dreamcast (`dc_boot.bin`,
`dc_flash.bin`) go into `data/` there; without them nullDC boots with reios, its replacement BIOS.

## Starting a download that is not signed

- **Linux**: `chmod +x {{linux_x86_64}}` (or the arm64 file), then start it. Without FUSE 3 start it as
  `./{{linux_x86_64}} --appimage-extract-and-run`.
- **Windows**: unpack the zip. The program is not signed: SmartScreen says "Windows protected your PC" the first
  time; "More info", then "Run anyway". `nulldc3.exe` is a console program: started from Explorer it has a console
  beside its window, where the log is.
- **macOS**: unpack the zip (the Finder, or `ditto -x -k {{macos_arm64}} .`). The application is signed ad hoc, not
  by a developer Apple knows: the first start is refused; allow it with System Settings, Privacy & Security, "Open
  Anyway", or clear the quarantine of the unpacked folder with
  `xattr -dr com.apple.quarantine nulldc3-{{version}}-macos-arm64`. Opened before that, from where it was unpacked,
  macOS runs it from a read-only place (App Translocation), where it cannot keep its files: nullDC says so. Start it
  from the unpacked folder after clearing the quarantine, or move the folder with the Finder first.

## Licences

`LICENSE` is the emulator's licence, `LICENSE-GPL` and `LICENSE-LGPL` the GNU licences that parts of it are under,
`LICENSE-shell` the BSD licence of its platform shell, and `LICENSE-devtools` the MIT licence of the built-in
Dreamcast DevTools UI. Every file has the five: the AppImage under `usr/share/doc/nulldc3/`, the Windows and macOS
folders beside the program (and `nulldc3.app` in `Contents/Resources`).

## The source

The tag {{tag}} of [skmp/nulldc3](https://github.com/skmp/nulldc3/tree/{{tag}}) is the source of these files, with the
tag {{tag}} of [skmp/dreamcast-devtools](https://github.com/skmp/dreamcast-devtools/tree/{{tag}}), the UI it pins in
its submodule `devtools/`. `README.md` there says how to build it.
