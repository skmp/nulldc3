# Device tree tables

Tests of `libswirl/debugger/dbg_regs_tables.cpp`, the constant tables behind `debugger.describe`: the nodes of the
device tree, their registers, events and panels. The tables link without the emulator, and so does this test.

    cmake -S tests/debugger/regs -B <builddir> -G Ninja [-DDEVTOOLS_DIR=<dreamcast-devtools checkout>]
    cmake --build <builddir> && ctest --test-dir <builddir> --output-on-failure

| test | what it checks |
| --- | --- |
| `tables` | The tables by themselves. Paths are unique, lower case, and a node stands behind its parent with its subtree in one piece. Register names are upper case and unique within a node; widths, flags and keys are consistent (a text has width 0 and no address, a register behind a read function is either computed or without a reader). Every panel id is one of the UI's, every event is listed by the node the event table names for it and by no other. The paths, labels and panels clients were written against are there, and so are the registers they name: the program counter of every target first in its node, the nodes of a running tick, the time. On-chip registers lie inside their module, in rising order. `FormatValue` gives the wire form. |
| `sources` | The on-chip rows against the `NAME_addr` definitions of `libswirl/hw/sh4/sh4_mmr.h` (name, address and the width of the comment in front of each), and the system bus rows against `libswirl/hw/holly/sb_regs.h`. Both headers are read as text: a register added to one of them and not to the tables fails here. |
| `panels` | The panel ids against `PANEL_IDS` of the UI (`src/lib/debuggerSchema.ts`). Skipped without a devtools checkout. |

`regs_test dump` prints the tree, one line per node and per register.

`aica/` has a second program, `aica_test`, for device state that the tree shows and that can be tested without the
machine. It compiles the sound generator (`libswirl/hw/aica/sgc_if.cpp`) and the settings of the sound regression
harness (`libswirl/hw/aica/aica_hash.cpp`) with the definitions and flags of the emulator build and stands in for the
little that the two units reference from the rest of the emulator.

| test | what it checks |
| --- | --- |
| `aica.channels` | The state of a channel that no register write sets. A sound generator is created in memory that was filled with ones: no channel has its loop flag set, neither for a guest read of the channel status (`LP`, bit 15 of `0x2810`) nor for the debugger (`DebugGetChannel`, `DebugReadCommonReg`), and every channel has its play position (`CA`, `0x2814`) with its fraction, its two decoded samples, its low-frequency oscillator and its filter envelope at 0. Generators created in memory filled with different bytes play the same samples, show the guest the same registers and the debugger the same 64 channels: with a loop, with a loop under the amplitude modulation of an oscillator the guest never reset, and with the noise generator. The loop flag is one byte of the save state; whatever that byte holds, the guest and the debugger see its bit 0, the debugger's reads leave it and the guest's read clears it. |
| `aica.rtc` | The keys that pin the real-time clock: `config:Debug.FixedRtc`, and `config:Dreamcast.RTC` when the first is missing or 0. Neither key, or the keys in another section, leave the clock to the host. |

What needs the machine is not here but in the emulator's selftest (`libswirl/debugger/dbg_selftest_regs.cpp`,
`devcast -config Debugger:SelfTest=regs <program>`): that every row is a register the emulator has, that every value
reads without a side effect and equals what the guest would read, and the memory map of `dbg_mem.h`.
