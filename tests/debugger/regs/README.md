# Device tree tables

Tests of `libswirl/debugger/dbg_regs_tables.cpp`, the constant tables behind `debugger.describe`: the nodes of the
device tree, their registers, events and panels. The tables link without the emulator, and so does this test.

    cmake -S tests/debugger/regs -B <builddir> -G Ninja [-DDEVTOOLS_DIR=<dreamcast-devtools checkout>]
    cmake --build <builddir> && ctest --test-dir <builddir> --output-on-failure

| test | what it checks |
| --- | --- |
| `tables` | The tables by themselves. Paths are unique, lower case, and a node stands behind its parent with its subtree in one piece. Register names are upper case, of `A-Z`, `0-9` and `_`, and unique within a node; widths, flags and keys are consistent (a text has width 0 and no address, a register behind a read function is either computed or without a reader). Every panel id is one of the UI's, an event is listed by the node the event table names for it, by no other and exactly once. **The node list**: the 104 nodes of milestone M2, each with its parent, its label and its actions, and no other: the 29 paths clients were written against, the five that milestone M1 added, `dc.holly.sb` with `g1`, `g2`, `pvr` and `misc`, `dc.holly.spg`, `dc.aica.common`, `dc.aica.rtc`, and the 64 channels `dc.aica.sgc.0` to `.63` one behind the other. How many registers each node has (2940 in all), and that each of the twelve register panels is the action of one node: `sh4-registers` of `dc.sh4.cpu`, `arm7-registers` of `dc.aica.arm7`. The registers clients name are there: the program counter of every target first in its node, the nodes of a running tick, the time. `dc.aica.arm7` has 41 registers, all 32 bits wide but `ENABLED`, which stands behind `PC`: one bit, computed and read-only, and the one row that is read as the clocked flag of the sound CPU (1 while the ARM7 is clocked, 0 while it is held in reset). **The writable rows** are the 69 of `dc.sh4.cpu` and the 40 of `dc.aica.arm7` that are not `ENABLED`, each a plain 32-bit register of its processor, and no other row of the tree: no register of a device has a writer. On-chip registers lie inside their module, in rising order. **The register file of the AICA**: the rows of `dc.aica.common` and of every channel against a list the test writes out word by word in the notation of a data sheet (`D2R<15:11> D1R<10:6> AR<4:0>`), bit for bit: name, word, bits, width, address and whether the row has a reader (`SCILV0-2`, `SCIRE` and `MCIRE` have none); the fields of one word do not overlap and lie inside it; `SA` and `DMEA` are one row of two words each; no row is in the words at `0x2D00` and above, and `L` and `M` are the two registers of the ARM7's interrupt controller. The rows of `dc.gdrom` and `dc.aica.rtc` are the members of their snapshots, each once and in order. `FormatValue` gives the wire form. |
| `sources` | The on-chip rows against the `NAME_addr` definitions of `libswirl/hw/sh4/sh4_mmr.h` (name, address and the width of the comment in front of each). The system bus rows against `libswirl/hw/holly/sb_regs.h`: each of its 127 registers is a row of one node, with its name and address, and the seven nodes that have system bus registers have exactly the lists of the round, in their order (`dc.holly.intc` 12, `dc.holly.dmac` 60, `dc.holly.sb.g1` 11, `.g2` 6, `.pvr` 11, `.misc` 15, `dc.maple` 12). The PVR rows against `libswirl/hw/pvr/pvr_regs.h`: each of its 68 registers (every `NAME_addr` but the bounds of the fog table, the object list pointers and the palette) is a row with its name and `0x005F8000` plus its offset, on the node the prefix of its name gives (`TA_` 14, `SPG_` 9, the other 45 on `dc.holly.core`), in the order of the header. The headers are read as text: a register added to one of them and not to the tables fails here. |
| `panels` | The panel ids against `PANEL_IDS` of the UI (`src/lib/debuggerSchema.ts`): 37 ids. Skipped without a devtools checkout. |

`regs_test dump` prints the tree, one line per node and per register.

`aica/` has a second program, `aica_test`, for device state that the tree shows and that can be tested without the
machine. It compiles the sound generator (`libswirl/hw/aica/sgc_if.cpp`) and the settings of the sound regression
harness (`libswirl/hw/aica/aica_hash.cpp`) with the definitions and flags of the emulator build and stands in for the
little that the two units reference from the rest of the emulator.

| test | what it checks |
| --- | --- |
| `aica.channels` | The state of a channel that no register write sets. A sound generator is created in memory that was filled with ones: no channel has its loop flag set, neither for a guest read of the channel status (`LP`, bit 15 of `0x2810`) nor for the debugger (`DebugGetChannel`, `DebugReadCommonReg`), and every channel has its play position (`CA`, `0x2814`) with its fraction, its two decoded samples, its low-frequency oscillator and its filter envelope at 0. Generators created in memory filled with different bytes play the same samples, show the guest the same registers and the debugger the same 64 channels: with a loop, with a loop under the amplitude modulation of an oscillator the guest never reset, and with the noise generator. The loop flag is one byte of the save state; whatever that byte holds, the guest and the debugger see its bit 0, the debugger's reads leave it and the guest's read clears it. |
| `aica.rtc` | The keys that pin the real-time clock: `config:Debug.FixedRtc`, and `config:Dreamcast.RTC` when the first is missing or 0. Neither key, or the keys in another section, leave the clock to the host. |

The tests project (`tests/CMakeLists.txt`, `tests/README.md`) adds this directory as well. There the tests are
named `debugger.regs.<name>`, the dreamcast-devtools checkout is given with `-DNULLDC_DEVTOOLS_DIR=`, and
`-DNULLDC_TESTS_REQUIRE_DATA=ON` makes `panels` fail without it instead of being skipped; that option works here
too.

What needs the machine is not here but in the emulator's selftest (`libswirl/debugger/dbg_selftest_regs.cpp`,
`nulldc3 --config Debugger:SelfTest=regs <program>`): that every row is a register the emulator has, that every value
reads without a side effect and equals what the guest would read, the memory map of `dbg_mem.h`, that the ARM7
target has the register window of the AICA byte for byte as loads by the ARM7 deliver it (`regs.mem.arm7`, which
runs a loop of its own on the sound CPU and puts the machine back), that `dc.aica.arm7.ENABLED` reads 0 and 1
where the booted program holds and releases the ARM7 (`regs.enabled`, which runs the program), and what the readers
make of the rows of the devices: the tests `regs.rows.*` read them at states they make up, where a value is in
every word, and `regs.tick` holds a full tick of the tree to its budget (`tests/debugger/selftest/README.md`).
