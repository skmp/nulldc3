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

What needs the machine is not here but in the emulator's selftest (`libswirl/debugger/dbg_selftest_regs.cpp`,
`devcast -config Debugger:SelfTest=regs <program>`): that every row is a register the emulator has, that every value
reads without a side effect and equals what the guest would read, and the memory map of `dbg_mem.h`.
