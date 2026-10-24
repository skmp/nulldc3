nullDC {{version}} for macOS on Apple silicon
=============================================

nulldc3.app is the emulator, with the Dreamcast DevTools UI built in. It needs macOS 11 or later on Apple silicon
(arm64).

The application is signed ad hoc, not by a developer Apple knows, and macOS refuses the first start of a download
like it. Allow it in System Settings, Privacy & Security, "Open Anyway", or clear the quarantine of this folder in a
Terminal:

    xattr -dr com.apple.quarantine nulldc3-{{version}}-macos-arm64

Started from where it was unpacked before that, macOS runs it from a read-only place (App Translocation), where it
cannot keep its files: nullDC says so. Clear the quarantine first, or move this folder to another place with the
Finder, then open nulldc3.app.

Portable: everything it reads and writes (emu.cfg, the BIOS and the flash in data/, the VMUs, the mappings, the save
states) is kept in this folder, the one that holds nulldc3.app. --portable <dir> names another folder, --shared uses
~/Library/Application Support/nulldc3. The BIOS and the flash of a Dreamcast, dc_boot.bin and dc_flash.bin, go into
data/ here; without them nullDC boots with reios, its replacement BIOS. Its executable is
nulldc3.app/Contents/MacOS/nulldc3, which a Terminal can start with switches (--help lists them). The debugger's UI
is at http://127.0.0.1:55543/ while it runs.

The licences: LICENSE (the emulator's), LICENSE-GPL and LICENSE-LGPL (the GNU licences that parts of it are under),
LICENSE-shell (the BSD licence of its platform shell), LICENSE-devtools (the MIT licence of the built-in Dreamcast
DevTools UI), here and in nulldc3.app/Contents/Resources. The source is https://github.com/skmp/nulldc3 at the commit
{{version}} names (a release: its tag v{{version}}), with the UI it pins in its submodule devtools/; its README.md
says how to build it.
