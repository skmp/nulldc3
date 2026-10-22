nullDC {{version}} for Windows x86_64
=====================================

nulldc3.exe is the emulator, one file: the Dreamcast DevTools UI, the C runtime and the icon are in it. It needs
Windows 10 1803 or later, 64-bit.

Portable: everything it reads and writes (emu.cfg, the BIOS and the flash in data\, the VMUs, the mappings, the save
states) is kept in this folder, beside nulldc3.exe. --portable <dir> names another folder, --shared uses
%APPDATA%\nulldc3. The BIOS and the flash of a Dreamcast, dc_boot.bin and dc_flash.bin, go into data\ here; without
them nullDC boots with reios, its replacement BIOS.

nulldc3.exe is a console program: started from a terminal it writes there; started from Explorer, Windows gives it a
console beside its window, where the log is. The debugger's UI is at http://127.0.0.1:55543/ while it runs (the
emulator prints "Debugger: http://..." when it is up; it listens on the loopback address only). nulldc3 --help lists
the switches.

The program is not signed: the first time, SmartScreen says "Windows protected your PC"; "More info", then "Run
anyway".

The licences: LICENSE (the emulator's), LICENSE-GPL and LICENSE-LGPL (the GNU licences that parts of it are under),
LICENSE-shell (the BSD licence of its platform shell), LICENSE-devtools (the MIT licence of the built-in Dreamcast
DevTools UI). The source is https://github.com/skmp/nulldc3 at the commit {{version}} names (a release: its tag
v{{version}}), with the UI it pins in its submodule devtools/; its README.md says how to build it.
