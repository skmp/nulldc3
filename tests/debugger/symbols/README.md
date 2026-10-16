# Symbol table

Tests of `libswirl/debugger/dbg_symbols.cpp`, the symbols of the ELF that reios loaded: what the debugger names
addresses with, in callstack frames (`symbol`) and in the branch targets of disassembly text. The module reads the
`.symtab` of the image itself and links without the emulator, and so does this test.

    cmake -S tests/debugger/symbols -B <builddir> -G Ninja [-DSYMBOLS_SANITIZE=ON]
    cmake --build <builddir> && ctest --test-dir <builddir> --output-on-failure

The fixture is no file: `symbols_test.cpp` builds its ELF32 images byte for byte (header, `.symtab`, its string
table, four section headers), so that a test can say what every field holds and can break one. Every image is
handed over in memory that ends where the image ends; on hosts with `mmap` the page behind it cannot be read, so a
read past the size of an image ends the test with a fault, with or without a sanitizer.

| test | what it checks |
| --- | --- |
| `table` | The walk: the table takes the named, defined functions, objects and symbols without a type (an absolute symbol is defined), and no section symbol, file symbol, undefined symbol, symbol without a name or symbol of another type. `Find()` gives the address of a name as the ELF has it, the first definition of a name that is there twice, and matches a name whole. `Lookup()` by address: the start of a symbol, its last byte, the byte behind a symbol with a size (no symbol), the address in front of the first symbol (no symbol, and both outputs left alone); the low 29 bits are the address (`0x0C..`, `0x8C..`, `0xAC..` are one); a symbol without a size reaches to the next symbol, and `kMaxSymbolReach` bytes where none is that close, the last symbol of the table included; of several symbols at one address a function names it before an object and a label, a global one before a local one, and of two that are the same in both a symbol of a section before an absolute one, wherever the two are in the `.symtab` (the linker's `__executable_start` does not stand for `_start`). `Format()`: `name` at the start, `name+0x<offset>` in lower-case hex inside, cut to the buffer with its terminator, nothing written for a buffer of 0, the empty text and `false` for an address in no symbol. A name with bytes above `0x7F` and with control characters is kept as the bytes the image has (the wire form, `?` for each such byte, is the core's and is tested by the selftest `core.tick`). A second image replaces the table; a boot without an ELF (`nullptr`, 0) and `Clear()` empty it. |
| `damaged` | Every offset of the image is checked against its size. What is no 32-bit little-endian ELF gives no symbols and leaves none of the image before: another magic, a 64-bit ELF, a big-endian one, an empty image, four bytes, pages of zeros and of `FF`. The fixture cut at each of its lengths. Section headers that start at or behind the end, at `0xFFFFFFF0`, of 39 and of 65535 bytes, a section count of 0 and of 65535. A `.symtab` that starts or ends behind the end, of `0xFFFFFFF0` bytes, whose string table is a section that does not exist (also with bytes behind the headers that look like one) or is no string table, with entries of 8 and of `0xFFFFFFFF` bytes (an entry size of 0 is the 16 bytes of ELF32), cut inside its last entry. A string table that starts or ends behind the end. A name whose offset is the size of the string table or `0xFFFFFFFF` (no name), a name that starts in the middle of another, and a string table without a NUL at its end, also as the last bytes of the image: the name ends where the table ends. Symbols at the top of the address space, with sizes that run over it: no sum wraps. Then 20000 images with random damage to the header, the section headers and the entries, some of them cut: each is loaded and looked into. |

A check that fails prints `FAIL <text>`; the last line is `<test>: <n> checks, <f> failed`.

The tests project (`tests/CMakeLists.txt`, `tests/README.md`) adds this directory as well; there the tests are
named `debugger.symbols.<name>`.
