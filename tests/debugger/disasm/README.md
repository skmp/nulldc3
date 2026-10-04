# Disassembler tests

Tests for `libswirl/debugger/dbg_disasm.{h,cpp}` (SH-4, ARM7DI, AICA DSP). This is a standalone CMake
project; it is not part of the emulator build.

```
cmake -S tests/debugger/disasm -B <builddir> -G Ninja
cmake --build <builddir>
ctest --test-dir <builddir>            # add -V for the comparison numbers
```

Options: `-DSH_OBJDUMP=<sh-elf-objdump>`, `-DARM_OBJDUMP=<arm-eabi-objdump>`,
`-DDEVTOOLS_DIR=<dreamcast-devtools checkout with node_modules>`,
`-DDISASM_SANITIZE=ON` (`-fsanitize=address,undefined`).

| ctest name | needs | what it does |
|---|---|---|
| `golden` | nothing | checked-in tables `golden_*.inc`, plus invariants over all 65536 SH-4 opcodes x 4 FPSCR modes, 1,000,000 random ARM words and 200,000 random DSP steps |
| `sh4_objdump` | python3, a SuperH GNU objdump | every SH-4 opcode against `objdump -m sh4` |
| `sh4_objdump_selftest` | python3, a SuperH GNU objdump | the `sh4_objdump` comparison itself: wrong info fields in the dump must be reported |
| `sh4_opcode_table` | python3 | every SH-4 opcode against `libswirl/hw/sh4/sh4_opcode_list.cpp` |
| `arm7_objdump` | python3, an ARM GNU objdump | 249,364 ARM encodings against `objdump -m armv3` |
| `dsp_playground` | `tsx` and `src/app/dsp/dspUtils.ts` of dreamcast-devtools | 100,193 DSP steps against the playground's disassembler and assembler |

A test whose tool is missing prints `SKIP: <reason>` and is reported by ctest as skipped. `golden` always runs.

`disasm_test` is also the dump tool the scripts drive (`sh4 MODE BASE`, `arm BASE`, `dsp`), and
`disasm_test rows` prints table rows for new golden cases (see the comment at the top of `disasm_test.cpp`).
Rows are reviewed by hand before they go into a `golden_*.inc`.

## What `golden` checks without a tool

- the golden tables: text and info for hand-picked encodings, with and without symbol / memory callbacks
- SH-4: `valid` <=> not `.word`; info and non-FPU text independent of `fp_mode`; null `info` / empty context give the
  same text; branch targets and literal addresses follow `pc` (long literals from `pc & ~3`); 59049 valid opcodes
- ARM: the condition field only adds its suffix (text and info of any condition equal those of the AL encoding);
  operand column; `.word` text for invalid encodings
- DSP: every line assembles back (a C++ restatement of the playground's field table) to the same step and words

## SH-4 against objdump

All 65536 opcodes are written as a little-endian image at `0x8c010000` and disassembled with
`sh-elf-objdump -D -b binary -m sh4 -EL`. Both sides resolve PC-relative literals against the image.
Opcode n sits at base + 2n, which ties the alignment of `pc` to bit 0 of the opcode, so the comparison is
repeated with the image at `0x8c010002`: every opcode is seen at both alignments.
Compared per opcode: valid / invalid, text, the info flags (derived from objdump's mnemonic), branch
targets, literal addresses and literal values.

Normalisation (counted as "normalised-equal"):

| rule | objdump | dbg_disasm | why |
|---|---|---|---|
| whitespace | tab between mnemonic and operands | spaces to column 8 | layout only; not counted |
| `fmov.s` | `fmov @r2,fr1` | `fmov.s @r2,fr1` | with FPSCR.SZ = 0 the memory forms move 32 bits; the suffix makes that explicit, as the opcode table and the Renesas manual do. objdump has no FPSCR state |
| literal value | `! 9002`, `! d003d002` (raw memory) | `! 0xffff9002`, `! 0xd003d002` | the comment is the value the register receives: `mov.w` sign extends |
| FPSCR modes 1..3 | (none: objdump knows no FPSCR) | `dr`/`xd` names | objdump's text is rewritten by the rules in `dbg_disasm.h` before comparing: PR = 1 turns `frN` into `dr(N & 14)` for fadd fsub fmul fdiv fcmp/eq fcmp/gt fabs fneg fsqrt float ftrc; SZ = 1 turns the `frN` of fmov into `drN` (N even) or `xd(N - 1)` (N odd) |

Result (GNU objdump 2.43):

| FPSCR | image base | compared | identical | normalised-equal | explained | unexplained |
|---|---|---|---|---|---|---|
| PR=0 SZ=0 | `0x8c010000` | 65536 | 55792 | 9728 | 16 | 0 |
| PR=0 SZ=0 | `0x8c010002` | 65536 | 55792 | 9728 | 16 | 0 |
| PR=1 SZ=0 | `0x8c010000` | 65536 | 54176 | 11344 | 16 | 0 |
| PR=0 SZ=1 | `0x8c010000` | 65536 | 55536 | 9984 | 16 | 0 |
| PR=1 SZ=1 | `0x8c010000` | 65536 | 53920 | 11600 | 16 | 0 |

Checked in each run: 59033 flag sets, 9216 branch targets, 8448 literal addresses, 8192 literal values.

`sh4_objdump_selftest` (`sh4_objdump.py --self-test`) holds the comparison to these figures. It takes the dump of
the first run and makes one field wrong on every opcode that has it: each of the six flags in turn (set <-> clear),
the branch target, the literal address, the literal value. Each of the nine corrupted dumps must come back
unexplained on exactly the encodings counted above for that field (59033, 9216, 8448, 8192).

Explained differences:

| class | opcodes | objdump | dbg_disasm | decision |
|---|---|---|---|---|
| fcnvsd / fcnvds with bit 8 set | `0xf1ad 0xf3ad .. 0xffad`, `0xf1bd 0xf3bd .. 0xffbd` (16) | `.word` | `fcnvsd fpul,drN` / `fcnvds drN,fpul` with N = field & 14 | the opcode table has them (`Mask_n`) and the interpreter executes them, using bits 11:9 as the register pair. The debugger shows what the emulator runs |

## SH-4 against the opcode table

`sh4_table_check.py` extracts the entries of `sh4_opcodelistentry opcodes[]` with a regular expression and
expands them the way `BuildOpcodeTables()` does (212 entries, 59050 opcodes with a handler). The file is only read.

| check | result |
|---|---|
| `valid` <=> the table has a handler | 59049 valid in both, 6486 invalid in both |
| `is_branch` <=> table type `Branch_*`, `is_delayed_branch` <=> type `*_d` | all agree |
| mnemonic = first word of the table's format string | all agree, with the exceptions below |

| class | opcodes | table | dbg_disasm | decision |
|---|---|---|---|---|
| HLE BIOS trap | `0x085b` (1) | `reios_trap` | `.word 0x085b` | an emulator hook, not an SH7091 instruction. The debugger core can special-case `REIOS_OPCODE` if it wants to name it |
| mnemonic | `0x4n83` with bit 7 (128) | `stc <RM_BANK>,@-<REG_N>` | `stc.l rM_bank,@-rN` | the handler pre-decrements and stores a long: the table string lacks the `.l` (objdump agrees with us) |
| mnemonic | `0x0n3a` (16), `0x0nfa` (16) | `sts SGR,..`, `sts DBR,..` | `stc sgr,rN`, `stc dbr,rN` | SGR and DBR are control registers: `stc` in the manual and in objdump |

So objdump and the table disagree with each other on 17 opcodes only (the 16 conversions above and `0x085b`), and
`dbg_disasm` follows the table for the first and the SH-4 instruction set for the second.

## ARM7 against objdump

Oracle: GNU objdump for ARM (`arm-eabi-objdump -D -b binary -m armv3 -M reg-names-std -EL`), image at `0x00100000`.
Sample (fixed seed, 249,364 encodings): every instruction class enumerated over its mode bits (16 data processing
opcodes x S x every operand-2 form, all P/U/B/W/L and P/U/S/W/L and P/U/N/W/L combinations, all 16 coprocessors,
all 16 msr field masks, edge displacements, pc-relative forms, return idioms), 6000 words each in the multiply /
swap space and the PSR-transfer space, 4000 in the undefined space, and 200,000 uniformly random words.

Compared per encoding: valid / invalid, text, the info flags (derived from the rewritten objdump text by the rules
of `dbg_disasm.h`), `b` / `bl` targets, pc-relative addresses (objdump's `@ 0x...` comment for `ldr`, recomputed from
the encoding for `add` / `sub rd, pc, #imm`), and the `= value` of pc-relative loads against the image.

objdump's unified-syntax output is rewritten into the canonical forms of `dbg_disasm.h`:

| rule | objdump | dbg_disasm | encodings |
|---|---|---|---|
| comment dropped | `@ 0xd3`, `@ <UNPREDICTABLE>`, `@ (str lr, [sp, #-4]!)` | no such comments | 75814 |
| number format | `#211`, `#-16777216`, `[r1, #-131]` | `#0xd3`, `#0xff000000`, `[r1, #-0x83]`: decimal below 10, else hex; data-processing immediates unsigned | 76436 |
| `<UNDEFINED>` -> `.word` | `@ <UNDEFINED> instruction: 0x...` | `.word 0x...` | 28145 |
| coprocessor operand spelling | `mcr 15, 0, r0, cr1, cr0, {0}` | `mcr p15, 0, r0, c1, c0, 0` (ARM's own syntax) | 38789 |
| address padded to 8 digits | `b 0x101008` | `b 0x00101008` | 14686 |
| `svc` -> `swi` | `svc 0x00123456` | `swi 0x123456` | 14059 |
| NV compared with the AL encoding | (objdump has no NV: the space was reused from ARMv5 on) | suffix `nv` | 11490: objdump is shown the same word with condition AL and `nv` is appended to its mnemonic |
| `ldm` / `stm` / `ldmfd` / `stmfd` | `ldm r1, {..}`, `stmfd sp!, {r0}` | `ldmia`, `stmia`, `stmdb`, `ldmia`: the addressing mode is always spelled | 4324 |
| mov with a non-zero Rn field | `<UNDEFINED>` | `mov ...` | 3078: the ARM7DI ignores the field; objdump is shown the word with Rn = 0 |
| shift alias -> mov | `lsl r0, r2, #2`, `rrx r0, r1` | `mov r0, r2, lsl #2`, `mov r0, r1, rrx` | 1424 |
| `mrc` to pc | `APSR_nzcv` | `pc` | 303 |
| ldc / stc zero offset | `[r3]`, `[r3, #-0]` whatever the W bit, `{-0}` | `[r3, #0]!`, `[r3], #0`, `[r3, #-0]!`, `{0}`: objdump drops the write-back mark and the post-index form when the offset is 0; the rule restores them from P, U and W | 251 |
| `push` / `pop` | `push {r4, lr}`, `push {lr}`, `pop {r4, pc}` | `stmdb sp!, {r4, lr}`, `str lr, [sp, #-4]!`, `ldmia sp!, {r4, pc}` | 165 |
| `nop` | `nop @ (mov r0, r0)` | `mov r0, r0` | 2 |

Result (GNU objdump 2.43):

| compared | identical | normalised-equal | explained | unexplained |
|---|---|---|---|---|
| 249364 | 58562 | 169178 | 21624 | 0 |

Checked: 199595 flag sets, 28042 branch targets, 1299 pc-relative addresses, 500 literal values.

Per class (decode of the ARM7DI data sheet):

| class | encodings | identical | normalised-equal | explained |
|---|---|---|---|---|
| data processing | 46832 | 9974 | 36858 | 0 |
| mrs | 120 | 102 | 18 | 0 |
| msr | 1012 | 386 | 626 | 0 |
| rest of the PSR-transfer space | 11596 | 0 | 2808 | 8788 |
| mul / mla | 1204 | 810 | 394 | 0 |
| swp / swpb | 420 | 236 | 184 | 0 |
| rest of the multiply / swap space | 12627 | 0 | 8796 | 3831 |
| ldr / str | 40948 | 13604 | 27344 | 0 |
| undefined space | 16564 | 0 | 16541 | 23 |
| ldm / stm | 28169 | 20874 | 7295 | 0 |
| b / bl | 28042 | 12576 | 15466 | 0 |
| ldc / stc | 27347 | 0 | 22155 | 5192 |
| cdp | 10135 | 0 | 8251 | 1884 |
| mrc / mcr | 10289 | 0 | 8383 | 1906 |
| swi | 14059 | 0 | 14059 | 0 |

Explained differences. `-m armv3` does not make objdump an ARMv3 disassembler: it still prints later
architectures' instructions in several spaces. `dbg_disasm` knows the documented ARM7DI instruction set and
nothing else.

| class | encodings | objdump | dbg_disasm | decision |
|---|---|---|---|---|
| PSR-transfer space outside mrs / msr | 8788 | `cmp` / `cmn` / `tst` / `tstp` / `cmpp` (ignoring the clear S bit), `msr` with a shifted register or an ARMv7 banked register, `hvc`, `mrs r, (UNDEF: n)` | `.word` | tst / teq / cmp / cmn opcodes with S clear are PSR transfers on ARMv3. Only the exact encodings are documented (mrs: `cond 00010 P 001111 Rd 000000000000`; msr: `cond 00010 P 10 mask 1111 00000000 Rm` and `cond 00110 P 10 mask 1111 rot imm8`); what the core does with the others is not known |
| rest of the multiply / swap space | 3831 | `ldrh` `strh` `ldrsb` `ldrsh` (ARMv4), `teq` / shift aliases with `<illegal shifter operand>`, ARMv8 `stlex*` / `lda*` | `.word` | bits 7 and 4 set with bits 27:25 = 000: the ARM7DI documents mul, mla, swp, swpb only. The ARM7DI has no long multiply and no halfword transfers, and the interpreter (`libswirl/gpl/vba-arm/arm-new.h`) has them compiled out |
| undefined space | 23 | `udf #imm` | `.word` | `udf` is objdump's name for one corner (`cond 0111 1111 imm12 1111 imm4`) of the undefined-instruction space (bits 27:25 = 011, bit 4 set); all of it is `.word` |
| coprocessors 9-11 | 5162 | `<UNDEFINED>` | `cdp` / `mrc` / `mcr` / `ldc` / `stc` with `p9`..`p11` | objdump reserves these numbers for VFP / Neon and rejects what is not one of their instructions. ARMv3 has 16 generic coprocessors |
| coprocessor-specific mnemonics | 3820 | `vldr`, `vstmia`, `vmov`, `fldmiax`, ... | generic `cdp` / `mrc` / `mcr` / `ldc` / `stc` | no FPA or VFP exists on the AICA; the generic form shows the fields. The generic text itself is verified on the other coprocessor numbers |

## DSP against the playground

`dsp_playground.mts` is run with dreamcast-devtools' `tsx` and imports its `src/app/dsp/dspUtils.ts` unmodified.
100,193 steps: every step index with an empty instruction, every single bit of the four words, all bits set,
and 100,000 random steps (half of them sparse).

- text: `MPRO[step] = ` + `disassembleDesc(decodeInst(words))` must equal the C++ line, byte for byte
- paste-back: `assembleSource([line])` must return the same step index and `encodeInst(decodeInst(words))`

Result: 100193 compared, 100193 identical, 100193 reassembled to the same words, 0 unexplained.

One spelling is ours: an all-zero step. The playground's `disassembleDesc` returns the empty string for it, and
its assembler rejects `MPRO[n] =` without a field, so `DisasmDsp` prints `MPRO[n] = TRA:0`, which assembles to
four zero words (185 of the sampled steps). Bits outside the 24 fields (bit 0 of words 0 and 1, bits 6:0 of word 3)
are ignored by both.
