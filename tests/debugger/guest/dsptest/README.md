# The sounding guest (`dsptest`) and the sound lines

`dsptest.elf` is a guest program of the tree: a bare-metal SH4 program with a small ARM7 program inside it, written
for the tests of the debugger's sound side and for the sound regression harness (`--config config:Debug.AicaHash=<N>`,
`docs/nulldc3/automated-runs.md`). It plays two channels of the sound generator, its ARM7 runs all the time, a
timer interrupt of the AICA changes what is heard, and its DSP runs a program of 16 steps whose two outputs are
heard ("The DSP"). `2ndmix.elf`, the program the harness was accepted with, plays nothing under reios (its ARM7
player waits for a timer interrupt that it never enables), so its lines say nothing about samples; this one's do.

It is for the emulator only: it waits for no G2 FIFO, has no loader and no console in mind. It is kept small. The
correctness of the sound generator is not its subject, and it does not grow a feature for every later fix: packages
that are not allowed to touch it lean on its bytes, and **a change of the guest is a new workload**, with a new
count of its FIQ entries and new lines wherever lines of it are compared between two builds.

With the sound on (no `audio:backend=none`) a person hears two tones at once: a rough one near 1.7 kHz, which is
the first channel switched between two pitches 3392 times a second, and under it a lower one that steps through
four pitches, a step every 0.15 s, while it starts over and changes sides 53 times a second. The DSP adds its two
outputs at a lower level: one made of its counter and its delay line, in the middle, and one made of the first
channel's send, which the counter modulates, on the left.

## The files

| file | what | made by |
|---|---|---|
| `dsptest.s` | the SH4 program, linked at `0x8C010000` | hand |
| `arm7.s` | the ARM7 program, linked at 0 | hand |
| `dsptest_dsp.txt` | the DSP program, in the syntax of the DSP playground of the devtools UI ("The DSP") | hand |
| `gen.sh` | makes the five files below; `gen.sh --check` builds them again and compares, byte for byte | hand |
| `dsptest.elf` | what a run boots: one loadable segment, the labels as symbols with a type and a size | `gen.sh` |
| `arm7.bin` | the assembled ARM7 program; `dsptest.s` takes it in with `.incbin` | `gen.sh` |
| `dsptest_labels.inc` | every label of the two programs as a `#define` (`DSPTEST_G_MAIN_LOOP`, `DSPTEST_A_FIQ`, ...), for the selftests of the emulator | `gen.sh` |
| `dsptest_dsp.inc` | the assembled DSP program: the macro `dsp_table` (the table's entries of its `MEMS_H` lines) and the macro `dsp_block` (the block `g_dsp`); `dsptest.s` takes it in with `.include` | `gen.sh` |
| `SHA256SUMS` | the manifest of the eight files above, and of nothing else | `gen.sh` |
| `soundlines.py` | the runner of the sound lines (below) | hand |
| `soundlines_test.py` | the runner on stand-ins for the emulator: it fails a run that was not made on the engine it asked for | hand |
| `CMakeLists.txt` | the tests of this directory, and `nulldc_sound_neutral` for the directories behind it | hand |

The emulator and every test build and run without an assembler. `gen.sh` needs the sh-elf binutils (`SH_ELF_BIN`,
default `/opt/toolchains/dc/sh-elf/bin`) for the SH4 part, awk for the DSP part, and for the ARM7 part an arm-eabi
binutils (`DC_ARM_BIN`, default `/opt/toolchains/dc/arm-eabi/bin`). **GNU `as` is the reference for `arm7.bin`**
(the files here were made with binutils 2.43 for both processors). Where there is no arm-eabi binutils, clang with
`--target=armv4t-none-eabi`, `ld.lld` and `llvm-objcopy` stand in: `arm7.s` has no literal pool and no constant
that an assembler could encode in two ways, and clang 18 gives the same 352 bytes. Where there is neither, `gen.sh`
keeps `arm7.bin` and the ARM7 labels as they are, so that a change of `dsptest.s` needs no ARM assembler. Nothing in
the outputs depends on the machine, the user, the date or a path.

`SHA256SUMS` does not list `soundlines.py`, `soundlines_test.py`, `CMakeLists.txt` and this file on purpose: they
change without the guest changing.

The DSP part of `gen.sh` is an awk script and no tool of its own. It reads the lines of `dsptest_dsp.txt` that
the playground's assembler reads (`assembleSource` of `src/app/dsp/dspUtils.ts` in the devtools UI): `COEF[i] =
<v>` (the 16 bits of the register, of which the DSP keeps 15:3), `MADRS[i] = <v>`, `MEMS_H[i] = <v>` (bits 23:8 of
`MEMS[i]`: its low byte is not the CPU's to write) and `MPRO[n] = <fields>`, a value in decimal or `0x` hex, and
lines that start with `#` or `//`. It encodes the fields of a step into its four words by the layout of
`libswirl/hw/aica/dsp_core/aica_dsp_core.h`, which the playground, the emulator's disassembler and caique share,
and refuses an `MPRO` line that is not exactly the line the emulator's disassembler (`DisasmDsp`) prints for those
words: the fields in its order, the zero ones left out, a field of one bit by its name. So the one text is the
source, what the debugger's disassembly of the DSP shows, and what the playground assembles. The steps are given
in their order from step 0, none of them empty and step 127 not among them.

## What it does

**The SH4** (`dsptest.s`). reios enters every program at `0x8C010000`, where `_start` is.

| label | what |
|---|---|
| `_start` | sets `SR` (privileged, interrupts blocked for the set-up, `FD` clear), `VBR` and the stack (`0x8D000000`, the top of main memory); **lets the first tick of the AICA pass** (below); holds the ARM7; copies the ARM7 program to wave memory 0 and one period of a 16-bit sine (32 samples) to wave memory `0x10000`; writes the table; copies the DSP program (`g_dsp`) to the registers of the DSP; releases the ARM7 with its last three instructions (`mov.l .L_armrst, r1`, `mov #0, r0`, `mov.b r0, @r1`, in front of `g_main`); falls into `g_main` |
| `g_table` | what `_start` writes, as (address, value) pairs, a long write each: `MCIRE` and `MCIEB` (the interrupt the ARM7 raises is let through to the SH4), `SB_IML4EXT` (it reaches the SH4 as IRL 11), `MVOL`, two channels (16-bit PCM, the loop of 32 samples, `AR` 31, `DISDL` 15; the second an octave below and on one side), the DSP part, and last the key on of both. **The DSP part**, `g_table_dsp` to `g_table_dsp_end`, 40 entries in front of the key on: the entries of the `MEMS_H` lines of the DSP program (the macro `dsp_table`), `RBP` / `RBL` (`0x00702804`), `EFSDL` / `EFPAN` of the two outputs of the program (`0x00702000`, `0x00702004`) and `ISEL` / `IMXL` of the first channel (`0x00700020`); the rest of the part is empty (an address of 0 is skipped), at least three entries at its end, which `dsptest.s` holds when it is assembled: the copies of `soundlines.py` write there |
| `g_main` | opens the interrupts (`SR = 0x40000000`) and falls into the loop |
| `g_main_loop` | counts in `r8` to `r11`: eight instructions that are neither control flow nor a delay slot, then `bra g_main_loop` and a `nop` in its slot |
| `g_irq` | at `VBR + 0x600`, the interrupt of the AICA: acknowledges it (`MCIRE` bit 5), counts in `r2` of its bank, and writes the pan of the second channel, from one side to the other (heard); `rte` |
| `g_sleep_loop` | `SLEEP` in a loop with the interrupts open; the AICA's interrupt wakes it. Not reached by itself: a test sets the PC |
| `g_hold_arm7` | holds the ARM7 (`ARMRST` 1) and spins at `g_hold_arm7_spin`. Not reached by itself |
| `g_release_arm7` | releases the ARM7 and goes to `g_main`. Not reached by itself |
| `g_wave`, `g_arm7_image` | the sine and the ARM7 program, data |
| `g_dsp` | the DSP program as `gen.sh` assembled it from `dsptest_dsp.txt` (the macro `dsp_block`), data, behind `g_arm7_image_end`: `g_dsp_coef` to `g_dsp_coef_end` the `COEF` registers from `COEF[0]`, `g_dsp_madrs` to `g_dsp_madrs_end` the `MADRS` registers from `MADRS[0]`, `g_dsp_mpro` to `g_dsp_mpro_end` the program, four words a step from step 0 (the steps behind it stay empty), then `g_dsp_end`. One `.long` for each 16-bit register, the register in its low half: `_start` copies the three parts with long writes to `0x00703000`, `0x00703200` and `0x00703400` |

**The first tick.** The cut of the M2 round (`docs/nulldc3/devtools-integration-m2-round.md`, 5.2) had everything
the SH4 does to the sound "before the first AICA tick on both engines". There is no such place. The scheduler
starts with the first tick due, so it runs at the end of the first slice of the SH4, 448 cycles behind the start
of the machine: the interpreter has executed 56 instructions by then, the recompiler all of a set-up of 800. The
first lines of this guest showed it: the interpreter's line was the recompiler's a tick late in `samples`, `regs`
and `arm7`. So `_start` waits for that tick, in a way that is the same on every engine: it writes one ARM
instruction that branches to itself to wave memory 0 and releases the ARM7 onto it, so that the first tick finds
the ARM7 clocked on both engines, and reads `SCIPD` until `SAMPLE_DONE` is set. (The AICA is clocked whatever
`ARMRST` says: its timers step, its samples are made and its DSP runs in every tick, also while the ARM7 is
held; the release decides only whether the ARM7 runs in that tick.) Then it holds the ARM7 again and does the
set-up, the DSP program among it, whose last write is 20 slices (8960 cycles) behind the first tick on the
interpreter and in the slice of the first tick on the recompiler: between the first tick and the second (145124
cycles behind it) on both. A reset of the machine goes the same way, whenever it comes. So:

- the ARM7 is held at the entry of the program (`ENABLED` 0), clocked for one tick on a one-word program, held
  again and released onto `a_reset`: a debugger that looks during the first two ticks sees that;
- the first tick has 32 pairs of silence in it, and the DSP runs those 32 samples on the empty program, from
  the state after the reset; from the second tick on it runs the guest's program;
- a guest whose second release is taken out sounds all the same, with the two channels the SH4 keyed, at the
  pitches the table gives them (the ARM7 that moves them is held), and its DSP runs the program.
  `debugger.sound.held.dsptest.*` run that copy.

**The ARM7** (`arm7.s`, 88 instructions). `a_main_loop` runs in **SVC mode** (the mode the reset leaves, a mode with
an SPSR), the handler in FIQ mode; IRQ stays masked.

| label | what |
|---|---|
| `a_vectors` | `0x00` to `0x1C`: reset to `a_reset`, FIQ (`0x1C`) to `a_fiq`, everything else to `a_hang`, a branch to itself that is never executed |
| `a_reset` | the registers of FIQ mode and of SVC mode, among them every address the loop and the handler use (`r5` the common registers of the AICA, `r6` the variables at wave memory `0x400`, `r12_fiq` the channel registers); `SCIEB` 0 and `SCILV0-2` so that timer A has level 2; timer A with a period of **13 samples** (`TIMA = 0xF3`); every pending bit cleared; **the service of a request that is latched ended** (a read of L, a write of M: what the handler does); timer A enabled in `SCIEB`; F cleared. Falls into the loop. It leaves the interrupt of the AICA as the start of the machine has it, whatever ran before |
| `a_main_loop` | one pass: counts in `r3` and at `[0x400]`; **the F window** (`a_fwin`: `msr CPSR_c, #0xD3`, F set); reads the timer's count and `SCIPD`, stores them at `[0x404]` and `[0x408]`, folds both into `r4`; `a_fwin_end`: `msr CPSR_c, #0x93`, F cleared; clears `SAMPLE_DONE` through `SCIRE`; `bl a_leaf`; `b a_main_loop`. 14 instructions, all but the last two plain |
| `a_fwin`, `a_fwin_end` | the two `msr` of the window. Six plain instructions execute with F set; `a_fwin_end - 4` is the last of them |
| `a_leaf` | two plain instructions and `mov pc, lr` |
| `a_fiq` | reads L (the acknowledge), the timer's count and `SCIPD`, and folds them, its `lr` and its `SPSR` into `r9_fiq`; clears the timer's pending bit and arms the timer again; counts in `r8_fiq`; flips the pitch word of the first channel between `0x0000` and `0x0200` (heard); every 64th time (`a_fiq` falls through at the `bne`): moves the pitch word of the second channel, keys that channel off and on, and raises the AICA's interrupt to the SH4 (`MCIPD` bit 5); `a_fiq_end`: ends the service (M) and returns with `subs pc, lr, #4` |

Every access of a register of the AICA is a word access, on both processors; a byte write to `SCIPD`, `SCIRE`,
`MCIPD` or `MCIRE` ends this emulator, and so does a read of `0x2810` with `AFSET` set.

## The DSP

The program is `dsptest_dsp.txt`, 16 steps from step 0 (`MPRO[0]` to `MPRO[15]`, none of them empty; step 127 and
every step behind the program are empty), with its comments; `_start` loads it while it holds the ARM7, between
the first tick and the second, on every engine. Its table sets the ring at `RBP` 0x40 with `RBL` 0, 8192 words
from `0x20000` of the wave memory, where no channel plays and the ARM7 writes nothing; `EFSDL` 10 for `EFREG[0]`
(`EFPAN` 0, both sides) and `EFREG[1]` (`EFPAN` 0x1F, the left), a level at which the two channels and both
outputs at full scale stay below what `clip16` cuts (16384 + 2 * 5791 of 32767 a side); and `ISEL` 3, `IMXL` 15
for the first channel, whose send the program reads as `MIXS[3]`. The program keeps a counter in TEMP (0x100 less
every sample) and its word in the ring, reads the word back one sample later (`MRD` with `NOFL`, its `IWT` two
steps on stores it as it is), keeps a delay line in the float format of the wave memory (`MWT` without `NOFL`, read
back one sample later by an `MRD` whose `IWT` two steps on unpacks it), and makes two outputs: `EFREG[0]` of the
counter, the delay line and what the shifter makes of them, which nothing of `MIXS` reaches, and `EFREG[1]` of
the first channel's send, a filter of one pole on it in TEMP and its product with the counter. What it has in it,
each in the step the comment names:

- `XSEL` both ways; `YSEL` 0 to 3 (`FRC_REG`, `COEF`, `Y_REG[23:11]`, `Y_REG[15:4]`); `BSEL` both ways, `ZERO`,
  `NEGB`; `SHIFT` 0 to 3, where `SHIFT` 0 saturates twice the counter in about half of the samples (step 2) and
  `SHIFT` 2 cuts in about two in three (step 3); `TWT` and `TRA` relative to `MDEC_CT` (the counter, the filter);
- `IRA` from `MEMS`, from `MIXS` (`0x23`) and from `EXTS` (`0x30`, step 9, times a `COEF` of 0: nothing of what
  EXTS holds is used, the decode only);
- `YRL` (steps 5 and 6), `FRCL` with `SHIFT` 3 (step 1) and without (step 4), `ADRL` from `SHIFTED` with `SHIFT` 3
  (step 1) and from `INPUTS` (step 3);
- `MWT` with `NOFL` (steps 1 and 15) and without (steps 5 and 9), `TABLE` 0 in the ring and one `TABLE` 1 access
  (step 9, at `MADRS[2]` + `ADRS_REG`: `ADREB`); `MRD` with `NOFL` (step 3) and without (step 7), with `NXADR`,
  each followed by its `IWT` two steps behind it in an odd step (5 and 9);
- two `EWT` (steps 11 and 14);
- the nine datapath registers of the debugger (`ACC`, `Y_REG`, `FRC_REG`, `ADRS_REG`, the latch, its copy,
  `MEMVAL`, `NOFL_PIPE`, `MIXS_BANK`) each take two values or more over the steps of every sample, and all but
  `NOFL_PIPE`, which the program's `NOFL` bits make the same in every sample, differ from sample to sample.

Its rules: every memory step (`MRD`, `MWT`) is odd and below 100, so none is in the slot of a channel (channel K
has the slot of step `2K + 114 mod 128`, an even step); every `IWT` is two steps behind an odd `MRD`, so no read of
a CPU or of a channel between them can change what it stores (the emulator puts none in the latch anyway); no test
mode (bit 15 of `0x2804`), no 8 MB, no value of `EXTS` relied on, no CPU write of `MIXS` or `TEMP` while the DSP
runs.

## What tests may lean on

- **Plain lines.** "Not control flow" is what the contract runner of the devtools UI takes for it, on the line as
  the emulator disassembles it (`scripts/contract/disasm.ts` there): for the ARM7 no `b`, `bl`, `bx`, `swi`, no
  `.word`, and no operand that names `pc` or `r15`. The emulator prints a literal load as `ldr rN, [pc, #..]`, so
  `a_main_loop`, `a_leaf` and `a_fiq` load nothing from a literal. An `msr CPSR_c` with an immediate is a plain line.
- **Two bounds.** From any instruction of `a_main_loop` or `a_leaf` the next `bl a_leaf` is at most 16 instructions
  away (the cut asks for 32). From the FIQ vector to the return of `a_fiq` 23 instructions execute, 35 every 64th
  time (the cut asks for 48). With a period of 13 samples no second handler falls into one search of the runner.
- **The folds.** The sound line folds the samples, the register file of the AICA, the save state of the sound CPU
  and twenty words of the SH4; it has no field for wave memory, main memory or the scheduler. What the ARM7 reads
  would be in wave memory only. So the loop folds the timer's count and `SCIPD` of every pass into `r4`, and clears
  `SAMPLE_DONE` behind the fold: the fold says in which pass each time step of the AICA fell. And the handler folds
  its `lr` and `SPSR` at every entry: the instruction the entry was made in front of, and under which flags. Every
  register of the ARM7 is in the `arm7` field, so the line sees an entry that moved by one instruction, and a time
  step that moved into another pass, also when the instruction count of the tick stayed.
- **The SH4 program stores into no page that holds its code**, does not write `CCR`, uses no FP instruction and no
  `MACH` / `MACL`, and leaves `FPSCR` as reios sets it (`0x00040001`). Its counters are registers; its stores go to
  wave memory, to registers of the AICA and to `SB_IML4EXT`.
- **`g_main_loop` goes back by `bra`**, and neither it nor `g_irq` takes the PC from a register or from memory
  (`g_irq` returns by `rte`): a PC that a debugger writes into another mirror of main memory stays in that mirror.
- **`g_irq` keeps every register** of the code it interrupts and pushes nothing: it runs on the register bank of
  the handler (`SR.RB`).
- **A release of the ARM7 starts the same program, whatever ran before it.** `g_hold_arm7` and `g_release_arm7`
  hold the ARM7 at a tick of the test's choice, and one time in thirteen the timer has overflowed with the last
  sample of the burst in front of the hold: its request is pending, and the release latches it again (so does the
  release of an ARM7 that was held inside `a_fiq`, in front of its clear of the pending bit). `a_reset` ends that
  service before it clears F. So the first interrupt behind a release is the one of the timer `a_reset` armed, 13
  samples on, and the FIQ entries behind any release that the SH4 makes between two bursts are those of the plain
  run behind its own, entry for entry: the same sample of the same tick counted from the release, the same `Step`,
  the same return address ("The FIQ entries"). `debugger.sound.release.dsptest.*` holds that for the release of
  the plain run, on a copy of the guest whose SH4 leaves a request latched in front of it; a later hold and
  release, at each of the thirteen places of the timer, was looked at in the scratch build that counts the
  entries. (Without the end of the service the request is entered behind the `msr` that ends `a_reset`, inside the
  first sample, with the return address `a_main_loop + 4`: every count of the handler is one ahead and the timer is
  armed a second time.) What a release does not bring back is what `a_reset` does not write: the registers of the
  channels, the timers B and C, whose pending bits the loop folds, and the wave memory.
- **Everything the SH4 does to the sound is done between the first tick and the second**, but for one write: the
  pan write of `g_irq`, which is made in front of the tick behind the one whose interrupt it answers, on every
  engine. That is why the recompiler and the interpreter agree in `samples`, `regs`, `arm7`, `dsp`, `ticks` and
  `cycles` (`debugger.sound.engines.dsptest`); `sh4` differs, the SH4 being elsewhere in its loop on each.
- **The DSP program**: the labels `g_dsp`, `g_dsp_coef` / `g_dsp_coef_end`, `g_dsp_madrs` / `g_dsp_madrs_end`,
  `g_dsp_mpro` / `g_dsp_mpro_end` and `g_dsp_end`, in the ELF's symbols and as `DSPTEST_G_DSP_*` in
  `dsptest_labels.inc`; between `g_dsp_mpro` and `g_dsp_mpro_end` the program as the register area holds it, one
  `.long` for each 16-bit word (the word in the low half), four for each step from step 0, the steps behind it
  empty. It is loaded in front of the second tick on every engine and runs from the second tick on. Its loaded
  steps are 0 to 15, so for every `STEP` there is a loaded step above it, or, from 15 on, the first loaded step,
  0, which is what the contract's S21 takes; step 127 is empty. At
  least one loaded step has each of `MRD`, `IWT`, `MWT`, `EWT`, `YRL`, `FRCL` and `ADRL`. The nine datapath
  registers move ("The DSP"). The DSP part of the table keeps at least three empty entries at its end.

## The FIQ entries

A request is raised by a time step of the AICA, between two samples. With F clear it is entered by the first FIQ
test of the next sample (the interpreter tests in front of every instruction); with F set, in the window of the
loop, it waits and is entered behind the `msr` of `a_fwin_end`, inside a sample. The second kind is what shows a
debugger loop that looks at a pending FIQ once per sample only. Which kind an entry is, is decided by where the
loop stands when the timer overflows, and that is the orbit of a deterministic map (every sample starts at a whole
instruction), not a uniform phase. So the entries are counted, in a scratch build of the emulator that prints for
every entry its tick, its return address and the number of the `Step` of its `StepMany` call whose FIQ test made
it. **Not by the return address:** an entry by the first test of a sample that starts behind that `msr` has the
return address of the entries behind it, `a_fwin_end + 8`.

For this guest, in each of the four configurations (the count is the ARM7's and the same in all four), over 2000
ticks: 4920 entries; **2861 made by the first `Step`**, in front of each of the ten instructions outside the window
(19 of them with the return address `a_fwin_end + 8`); **13 by the second** (the `msr` of `a_fwin_end` starts its
sample); **2046 by the third or a later one** (the third to the eighth: each of the six instructions of the window
starts such a sample), all with the return address `a_fwin_end + 8`. 1710 of the 2000 ticks have an entry of the
last class, and there are no two ticks in a row without one. The entries meet each of the 32 samples of a tick 153
or 154 times.

The DSP program changed no byte of the ARM7 program and nothing of when the ARM7 is released: the `arm7` field,
which folds the return address and the status of every entry, is the one of the guest without it in every
configuration, and so is the count.

The count belongs to these bytes on this ARM7 interpreter. One instruction more or less in the loop or in the
handler gives another orbit: handlers a few instructions longer or shorter left the first class at one or two
places of the loop, or the second class empty; `a_reset`, whose length decides where in its sample the loop starts,
moved 3 entries in 4920 from the first class to the third when it grew by three instructions. Whoever changes
the guest, or what an ARM7 instruction costs, counts again.

The count is of a run from a release of the ARM7, and of every such run: `a_reset` is over within the first sample
behind a release and leaves nothing of an earlier run in the interrupt of the AICA ("What tests may lean on"), so
a test that holds and releases the ARM7 starts this sequence of entries again.

## The sound lines

`soundlines.py` runs the emulator on a program, headless, with no audio device, the clock pinned, in an
environment of its own, and parses the line of the harness:

    python3 tests/debugger/guest/dsptest/soundlines.py --emulator <nulldc3> --work /tmp/w \
            --guest tests/debugger/guest/dsptest/dsptest.elf --ticks 2000 --mode same --mode sounds --engine interp

    AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> dsp=<hex> ticks=<N> cycles=<decimal>

`--engine jit|interp` is the SH4 engine. The sound has one way to be made: sample by sample, with the DSP, in
every time step of the AICA (until the M3 round it was made in batches of 32 samples without the DSP by default,
and sample by sample with `config:aica.NoBatch=yes`; the runner chose with `--render`, and the tests had `.batch`
and `.persample` in their names). A second kind of run gives the exit line of the same program with its state
hash (`config:Exit.AfterVblanks=<V> , config:Exit.StateHash=yes` in place of the harness key: the harness ends the
process without an exit line).

**Every run has to be made on the engine it was asked for.** The emulator takes `config:Dynarec.Enabled=yes` for a
wish: a build without a recompiler (`-DTARGET_NO_REC=ON`, a generic host), or one whose recompiler cannot be set
up, goes on with the interpreter and prints `Using MCPU Interpreter` where the other prints `Using MCPU Recompiler`.
The runner looks for the line of the engine in the output of every run, of either kind and with or without a
session, and fails a run that has the other one or none: without that, `engines` compares the interpreter with
itself and every `.jit.` test is a copy of its `.interp.` test. So on an emulator without a recompiler
(`-DNULLDC_EMULATOR=`) the `.jit.` tests and `debugger.sound.engines.*` fail, as `system.guest_exit.jit` and
`system.boot.*.jit` do there; the `.interp.` tests pass. `soundlines_test.py` (`debugger.sound.runner`) holds the
check on stand-ins for the emulator, scripts that print the line of one engine whatever they are asked for.

| mode | what has to hold |
|---|---|
| `same` | two runs with the same keys print the same line, field for field. Every line of every mode has to have `ticks=<N>` |
| `sounds` | `samples` is none of the hashes of silence. Silence is not one value: the hash of `k * 32` pairs of zeroes is `basis * prime^(128 k)`, and every tick makes its 32 pairs, whether the ARM7 is held or not, so a silent run of N ticks prints the one for `k` = N; the mode checks against the whole family, `k` from 0 to N, a superset |
| `muted` | "Disable Sound" mutes: the run with `config:aica.NoSound=yes` makes every sample, and the pair the harness folds, where the sample hands its pair to the audio stream, is a pair of zeroes. Its line is the line of the run without the key in every field but `samples`, and `samples` is the hash of `N * 32` pairs of zeroes: a pair for every sample of every tick, which every burst makes, whether it finds the ARM7 clocked or held. The line is the harness's fold, not the stream: that the audio stream itself, the speed limit of the emulator (`config:aica.LimitFPS`), is handed a pair of zeroes in every sample, which no run with `audio:backend=none` shows, is the check `nosound` of `aica.dsp.emu.state` (`tests/aica_dsp/`) |
| `ignored` | `--ignored <section:key=value>`, once or more: keys an older build read and this one does not (a configuration file keeps them). A run with each prints the line of the run without it, field for field |
| `engines` | the line of the recompiler and the line of the interpreter agree in the fields of `--fields`, and in `dsp` |
| `record` | the lines of both engines (or of the one `--engine` names), and with `--vblanks <V>` their exit lines, into `--file`, each run made `--runs` times (2) and giving one line. For a before / after between two builds |
| `compare` | the same runs against that file: every line whole, every part of the state hash |
| `neutral` | `--selftest <filter> [--expect <text>]...`: four runs with the same N, V and keys, the line and the exit line with `Debugger:SelfTest=<filter>` and without. The two lines are equal in every field and the two exit lines in every part of the state hash; and each of the two runs with the session has no `SelfTest:` line with `: FAILED: ` or `: FAIL, `, at least one with `: ok: `, every `--expect` text in a `SelfTest:` line, and **no** summary line `SelfTest: <p> passed, <f> failed`. The session of such a run is one whose test never returns true: the run ends by the harness or by the exit key, and a summary means that the session ended the process before the run was over |
| `release` | the guest only: a release of the ARM7 that finds a request of the AICA latched starts the program of the run without one. Five runs on copies of the guest, made in the work directory, that differ from it in data. *A request latched*: three entries in the reserved part of the table, which `_start` writes while it holds the ARM7 (`SCILV1` and `SCIEB` bit 5, then `SCIPD` bit 5, the one request a program can raise: it is latched with the level of the timer's, and again by the release). *Without the end of the service*: the read of L or the write of M of `a_reset` goes to a word of the wave memory that nothing uses, at the same cost. The copy without a change gives the line; the copy with a request latched has to print it, in the fields of `--fields` (`sh4` is not among them: that SH4 has made three more writes when it enters its loop); so has the copy with no request and without the read and the write; and each of the two copies with a request latched and without one of the two has to print another `arm7` field, which says that the request was there |
| `held` | the guest only: a held ARM7 is an idle one for the rest of the AICA. Two runs on copies of the guest, made in the work directory, each with one instruction changed. *Held*: the last release of `_start`, the `mov #0, r0` at `g_main - 4`, made `mov #1, r0` (the halfword has to be `0xE000`, else the mode fails): the ARM7 stays held behind the set-up, and every burst from the second on finds it held. *Idle*: the ARM7 program's instruction at `a_reset` made `b .` (`0xEAFFFFFE`): the released ARM7 stays on it with FIQ and IRQ masked by its reset and touches nothing of the AICA. The two lines agree in `samples`, `regs`, `sh4`, `dsp`, `ticks` and `cycles`; and the controls: their `arm7` fields differ (both patches took), the held copy's `samples` is none of the hashes of silence (the channels the SH4 keyed play while the ARM7 is held), and the idle copy's `samples` is not the guest's (the guest's FIQ handler is heard) |
| `files` | `SHA256SUMS` lists the eight files and each has its sum; and, where bash and the sh-elf binutils are, `gen.sh --check` |
| `names` | `--build <the build directory of the tests>`, no emulator: the tests its `CTestTestfile.cmake` files register (read, not asked of a ctest, which would write its log over that of the ctest that runs the test), some `debugger.sound.*` among them, and no name of the sound (`debugger.sound.*`) or of the session `arm7` of the selftests (`debugger.selftest.arm7*`) with a part `batch` or `persample`, where the tests of the two renders were. Other tests may have those words: they name something else there (a batch of requests of the debugger's protocol) |

**No line is stored in the tree.** The generator's tables come from `pow()`: a line is good for one build on one
host. A test makes its runs itself; `record` refuses a file among the tests of the tree.

| test | what it runs |
|---|---|
| `debugger.guest.dsptest.files` | the mode `files` |
| `debugger.sound.lines.dsptest.<jit\|interp>` | `same` and `sounds` on the guest, N = 2000 (1.45 emulated seconds: the 13 ticks in which the FIQ meets every sample of a tick, and the 26 ticks between two key ons, more than seventy times) |
| `debugger.sound.engines.dsptest` | `engines` on the guest, N = 2000, fields `samples`, `regs`, `arm7`, `ticks`, `cycles` and `dsp` |
| `debugger.sound.release.dsptest.<jit\|interp>` | `release` on the guest, N = 2000, fields `samples`, `regs`, `arm7`, `ticks`, `cycles` |
| `debugger.sound.held.dsptest.<jit\|interp>` | `held` on the guest, N = 2000 |
| `debugger.sound.lines.2ndmix.<jit\|interp>` | `same` on `2ndmix.elf` of `NULLDC_ELF_DIR`, N = 40000; a test whose data is missing without that directory |
| `debugger.sound.muted.dsptest.<jit\|interp>` | `sounds` and `muted` on the guest, N = 2000: the run without the key sounds, the run with `config:aica.NoSound=yes` is its line with the silence of 2000 ticks in `samples` |
| `debugger.sound.ignored.dsptest` | `ignored` on the guest, N = 2000, on the JIT, with `config:aica.NoBatch=no` and `config:aica.NoBatch=yes`: the key of the render that went, which every configuration file an older build saved has |
| `debugger.sound.names` | `names` on the build of the tests |
| `debugger.sound.runner` | `soundlines_test.py`: twenty-three calls of the runner on stand-ins for the emulator (eight of them of the mode `held`, on a copy of `dsptest.elf` at the labels of this directory and on stand-ins whose line depends on the guest they are given: green on the one that agrees, red naming the field on one whose held copy has another `regs`, red naming the halfword on a copy whose `g_main - 4` is no `mov #0, r0`, and red on each control), and five of its mode `names` on stand-ins for a build directory; no emulator is run |

`nulldc_sound_neutral(<name> SELFTEST <filter> [EXPECT <text>...] [CONFIG <section:key=value>...] [TIMEOUT
<seconds>])`, defined in `CMakeLists.txt` for the directories behind this one, makes
`debugger.sound.neutral.<name>.<jit|interp>`: the mode `neutral` on the guest with N = 2000 and
V = 90. Nothing is registered with it here; the package of the sound burst registers its sessions in a directory
of its own (`../neutral`). Sessions of the selftests and of the contract that boot the guest are registered in
`tests/debugger/selftest` and `tests/debugger/contract`, whose functions take the path of `dsptest.elf`.
