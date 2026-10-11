# The sounding guest (`dsptest`) and the sound lines

`dsptest.elf` is a guest program of the tree: a bare-metal SH4 program with a small ARM7 program inside it, written
for the tests of the debugger's sound side and for the sound regression harness (`-config config:Debug.AicaHash=<N>`,
`docs/nulldc3/automated-runs.md`). It plays two channels of the sound generator, its ARM7 runs all the time, and a
timer interrupt of the AICA changes what is heard. `2ndmix.elf`, the program the harness was accepted with, plays
nothing under reios (its ARM7 player waits for a timer interrupt that it never enables), so its lines say nothing
about samples; this one's do.

It is for the emulator only: it waits for no G2 FIFO, has no loader and no console in mind. It is kept small. The
correctness of the sound generator is not its subject, and it does not grow a feature for every later fix: packages
that are not allowed to touch it lean on its bytes, and **a change of the guest is a new workload**, with a new
count of its FIQ entries and new lines wherever lines of it are compared between two builds.

With the sound on (no `audio:backend=none`) a person hears two tones at once: a rough one near 1.7 kHz, which is
the first channel switched between two pitches 3392 times a second, and under it a lower one that steps through
four pitches, a step every 0.15 s, while it starts over and changes sides 53 times a second.

## The files

| file | what | made by |
|---|---|---|
| `dsptest.s` | the SH4 program, linked at `0x8C010000` | hand |
| `arm7.s` | the ARM7 program, linked at 0 | hand |
| `gen.sh` | makes the four files below; `gen.sh --check` builds them again and compares, byte for byte | hand |
| `dsptest.elf` | what a run boots: one loadable segment, the labels as symbols with a type and a size | `gen.sh` |
| `arm7.bin` | the assembled ARM7 program; `dsptest.s` takes it in with `.incbin` | `gen.sh` |
| `dsptest_labels.inc` | every label of the two programs as a `#define` (`DSPTEST_G_MAIN_LOOP`, `DSPTEST_A_FIQ`, ...), for the selftests of the emulator | `gen.sh` |
| `SHA256SUMS` | the manifest of the six files above, and of nothing else | `gen.sh` |
| `soundlines.py` | the runner of the sound lines (below) | hand |
| `CMakeLists.txt` | the tests of this directory, and `nulldc_sound_neutral` for the directories behind it | hand |

The emulator and every test build and run without an assembler. `gen.sh` needs the sh-elf binutils (`SH_ELF_BIN`,
default `/opt/toolchains/dc/sh-elf/bin`) for the SH4 part, and for the ARM7 part an arm-eabi binutils (`DC_ARM_BIN`,
default `/opt/toolchains/dc/arm-eabi/bin`). **GNU `as` is the reference for `arm7.bin`** (the files here were made
with binutils 2.43 for both processors). Where there is no arm-eabi binutils, clang with
`--target=armv4t-none-eabi`, `ld.lld` and `llvm-objcopy` stand in: `arm7.s` has no literal pool and no constant
that an assembler could encode in two ways, and clang 18 gives the same 340 bytes. Where there is neither, `gen.sh`
keeps `arm7.bin` and the ARM7 labels as they are, so that a change of `dsptest.s` needs no ARM assembler. Nothing in
the outputs depends on the machine, the user, the date or a path.

`SHA256SUMS` does not list `soundlines.py`, `CMakeLists.txt` and this file on purpose: they change without the
guest changing.

## What it does

**The SH4** (`dsptest.s`). reios enters every program at `0x8C010000`, where `_start` is.

| label | what |
|---|---|
| `_start` | sets `SR` (privileged, interrupts blocked for the set-up, `FD` clear), `VBR` and the stack (`0x8D000000`, the top of main memory); **lets the first tick of the AICA pass** (below); holds the ARM7; copies the ARM7 program to wave memory 0 and one period of a 16-bit sine (32 samples) to wave memory `0x10000`; writes the table; releases the ARM7; falls into `g_main` |
| `g_table` | what `_start` writes, as (address, value) pairs, a long write each: `MCIRE` and `MCIEB` (the interrupt the ARM7 raises is let through to the SH4), `SB_IML4EXT` (it reaches the SH4 as IRL 11), `MVOL`, two channels (16-bit PCM, the loop of 32 samples, `AR` 31, `DISDL` 15; the second an octave below and on one side), and last the key on of both. Between `g_table_dsp` and `g_table_dsp_end` 40 entries are empty (an address of 0 is skipped): **reserved for the DSP part of package E8** (`MPRO`, `COEF`, `MADRS`, the ring buffer, `EFSDL` / `EFPAN`, `IMXL` / `ISEL` of the first channel), in front of the key on |
| `g_main` | opens the interrupts (`SR = 0x40000000`) and falls into the loop |
| `g_main_loop` | counts in `r8` to `r11`: eight instructions that are neither control flow nor a delay slot, then `bra g_main_loop` and a `nop` in its slot |
| `g_irq` | at `VBR + 0x600`, the interrupt of the AICA: acknowledges it (`MCIRE` bit 5), counts in `r2` of its bank, and writes the pan of the second channel, from one side to the other (heard); `rte` |
| `g_sleep_loop` | `SLEEP` in a loop with the interrupts open; the AICA's interrupt wakes it. Not reached by itself: a test sets the PC |
| `g_hold_arm7` | holds the ARM7 (`ARMRST` 1) and spins at `g_hold_arm7_spin`. Not reached by itself |
| `g_release_arm7` | releases the ARM7 and goes to `g_main`. Not reached by itself |
| `g_wave`, `g_arm7_image` | the sine and the ARM7 program, data |

**The first tick.** The cut of the M2 round had everything the SH4 does to the sound "in front of the first tick of
the AICA, on both engines". There is no such place. The scheduler starts with the first tick due, so it runs at
the end of the first slice of the SH4, 448 cycles behind the start of the machine: the interpreter has executed 56
instructions by then, the recompiler all of a set-up of 800. The first lines of this guest showed it: the
interpreter's line was the recompiler's a tick late in `samples`, `regs` and `arm7`. So `_start` waits for that
tick, in a way that is the same on every engine: it writes one ARM instruction that branches to itself to wave
memory 0 and releases the ARM7 onto it, which gives the AICA its clock (while the ARM7 is held no timer steps and,
sample by sample, no sample is made), and reads `SCIPD` until `SAMPLE_DONE` is set. Then it holds the ARM7 again
and does the set-up, which is over 6500 cycles later on the interpreter and far sooner on the recompiler: between
the first tick and the second on both. A reset of the machine goes the same way, whenever it comes. So:

- the ARM7 is held at the entry of the program (`ENABLED` 0), clocked for one tick on a one-word program, held
  again and released onto `a_reset`: a debugger that looks during the first two ticks sees that;
- the first tick has 32 pairs of silence in it sample by sample, as it has in batches; a guest whose second
  release is taken out is silent sample by sample (the line of 32 zero pairs) and sounds in batches, where the
  channels the SH4 keyed on are rendered without the ARM7.

**The ARM7** (`arm7.s`, 85 instructions). `a_main_loop` runs in **SVC mode** (the mode the reset leaves, a mode with
an SPSR), the handler in FIQ mode; IRQ stays masked.

| label | what |
|---|---|
| `a_vectors` | `0x00` to `0x1C`: reset to `a_reset`, FIQ (`0x1C`) to `a_fiq`, everything else to `a_hang`, a branch to itself that is never executed |
| `a_reset` | the registers of FIQ mode and of SVC mode, among them every address the loop and the handler use (`r5` the common registers of the AICA, `r6` the variables at wave memory `0x400`, `r12_fiq` the channel registers); `SCILV0-2` so that timer A has level 2; every pending bit cleared; timer A with a period of **13 samples** (`TIMA = 0xF3`), enabled in `SCIEB`; F cleared. Falls into the loop |
| `a_main_loop` | one pass: counts in `r3` and at `[0x400]`; **the F window** (`a_fwin`: `msr CPSR_c, #0xD3`, F set); reads the timer's count and `SCIPD`, stores them at `[0x404]` and `[0x408]`, folds both into `r4`; `a_fwin_end`: `msr CPSR_c, #0x93`, F cleared; clears `SAMPLE_DONE` through `SCIRE`; `bl a_leaf`; `b a_main_loop`. 14 instructions, all but the last two plain |
| `a_fwin`, `a_fwin_end` | the two `msr` of the window. Six plain instructions execute with F set; `a_fwin_end - 4` is the last of them |
| `a_leaf` | two plain instructions and `mov pc, lr` |
| `a_fiq` | reads L (the acknowledge), the timer's count and `SCIPD`, and folds them, its `lr` and its `SPSR` into `r9_fiq`; clears the timer's pending bit and arms the timer again; counts in `r8_fiq`; flips the pitch word of the first channel between `0x0000` and `0x0200` (heard); every 64th time (`a_fiq` falls through at the `bne`): moves the pitch word of the second channel, keys that channel off and on, and raises the AICA's interrupt to the SH4 (`MCIPD` bit 5); `a_fiq_end`: ends the service (M) and returns with `subs pc, lr, #4` |

Every access of a register of the AICA is a word access, on both processors; a byte write to `SCIPD`, `SCIRE`,
`MCIPD` or `MCIRE` ends this emulator, and so does a read of `0x2810` with `AFSET` set.

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
- **Everything the SH4 does to the sound is done between the first tick and the second**, but for one write: the
  pan write of `g_irq`, which is made in front of the tick behind the one whose interrupt it answers, on every
  engine. That is why the recompiler and the interpreter agree in `samples`, `regs`, `arm7`, `ticks` and `cycles`
  (`debugger.sound.engines.dsptest.*`); `sh4` differs, the SH4 being elsewhere in its loop on each.

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
ticks: 4920 entries; **2864 made by the first `Step`**, in front of each of the ten instructions outside the window
(20 of them with the return address `a_fwin_end + 8`); **13 by the second** (the `msr` of `a_fwin_end` starts its
sample); **2043 by the third or a later one** (the third to the eighth: each of the six instructions of the window
starts such a sample), all with the return address `a_fwin_end + 8`. 1709 of the 2000 ticks have an entry of the
last class, and there are no two ticks in a row without one. The entries meet each of the 32 samples of a tick 153
or 154 times.

The count belongs to these bytes on this ARM7 interpreter. One instruction more or less in the loop or in the
handler gives another orbit: handlers a few instructions longer or shorter left the first class at one or two
places of the loop, or the second class empty. Whoever changes the guest, or what an ARM7 instruction costs,
counts again.

## The sound lines

`soundlines.py` runs the emulator on a program, headless, with no audio device, the clock pinned, in an
environment of its own, and parses the line of the harness:

    python3 tests/debugger/guest/dsptest/soundlines.py --emulator <devcast> --work /tmp/w \
            --guest tests/debugger/guest/dsptest/dsptest.elf --ticks 2000 --mode same --mode sounds --engine interp --render persample

    AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> ticks=<N> cycles=<decimal>

`--engine jit|interp` is the SH4 engine, `--render batch|persample` is `config:aica.NoBatch` no or yes. A second
kind of run gives the exit line of the same program with its state hash (`config:Exit.AfterVblanks=<V> ,
config:Exit.StateHash=yes` in place of the harness key: the harness ends the process without an exit line).

| mode | what has to hold |
|---|---|
| `same` | two runs with the same keys print the same line, field for field. Every line of every mode has to have `ticks=<N>` |
| `sounds` | `samples` is none of the hashes of silence. Silence is not one value: the hash of `k * 32` pairs of zeroes is `basis * prime^(128 k)`, and a silent run prints one of them for a `k` from 0 to N (N in batches; fewer sample by sample, where no pair exists while the ARM7 is held) |
| `engines` | the line of the recompiler and the line of the interpreter agree in the fields of `--fields` |
| `record` | the lines of the four configurations (or of the one `--engine` and `--render` name), and with `--vblanks <V>` their exit lines, into `--file`, each run made `--runs` times (2) and giving one line. For a before / after between two builds |
| `compare` | the same runs against that file: every line whole, every part of the state hash |
| `neutral` | `--selftest <filter> [--expect <text>]...`: four runs with the same N, V and keys, the line and the exit line with `Debugger:SelfTest=<filter>` and without. The two lines are equal in every field and the two exit lines in every part of the state hash; and each of the two runs with the session has no `SelfTest:` line with `: FAILED: ` or `: FAIL, `, at least one with `: ok: `, every `--expect` text in a `SelfTest:` line, and **no** summary line `SelfTest: <p> passed, <f> failed`. The session of such a run is one whose test never returns true: the run ends by the harness or by the exit key, and a summary means that the session ended the process before the run was over |
| `files` | `SHA256SUMS` lists the six files and each has its sum; and, where bash and the sh-elf binutils are, `gen.sh --check` |

**No line is stored in the tree.** The generator's tables come from `pow()`: a line is good for one build on one
host. A test makes its runs itself; `record` refuses a file among the tests of the tree.

| test | what it runs |
|---|---|
| `debugger.guest.dsptest.files` | the mode `files` |
| `debugger.sound.lines.dsptest.<jit\|interp>.<batch\|persample>` | `same` and `sounds` on the guest, N = 2000 (1.45 emulated seconds: the 13 ticks in which the FIQ meets every sample of a tick, and the 26 ticks between two key ons, more than seventy times) |
| `debugger.sound.engines.dsptest.<batch\|persample>` | `engines` on the guest, N = 2000, fields `samples`, `regs`, `arm7`, `ticks`, `cycles` |
| `debugger.sound.lines.2ndmix.<jit\|interp>.<batch\|persample>` | `same` on `2ndmix.elf` of `NULLDC_ELF_DIR`, N = 40000; a test whose data is missing without that directory |

`nulldc_sound_neutral(<name> SELFTEST <filter> [EXPECT <text>...] [CONFIG <section:key=value>...] [TIMEOUT
<seconds>])`, defined in `CMakeLists.txt` for the directories behind this one, makes
`debugger.sound.neutral.<name>.<jit|interp>.<batch|persample>`: the mode `neutral` on the guest with N = 2000 and
V = 90. Nothing is registered with it here; the package of the sound burst registers its sessions in a directory
of its own (`../neutral`). Sessions of the selftests and of the contract that boot the guest are registered in
`tests/debugger/selftest` and `tests/debugger/contract`, whose functions take the path of `dsptest.elf`.
