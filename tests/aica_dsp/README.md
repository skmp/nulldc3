# AICA DSP core tests

What is tested is `libswirl/hw/aica/dsp_core`: the DSP of the AICA as one self-contained core (its README has the
hardware rules and the interface), and how the emulator's sound chip drives it (`aica.dsp.emu.*` and the `.emu`
cases, below). The tests of the core alone need nothing of the emulator; those of the wiring compile the
emulator's own `hw/aica/aica_mmio.cpp`, `sgc_if.cpp` and `dsp.cpp` (`emu_aica.h`).

The reference is the caique project (`caique-rtl`), which is not in this repository: its sample model of the AICA,
the DSP programs it ran on a Dreamcast and what the console answered are compiled in and read from a checkout,
`-DNULLDC_CAIQUE_DIR=<path to caique-rtl>` (default `../caique-rtl` next to the repository if it is there).
Without it the tests that need it are skipped, or fail with `-DNULLDC_TESTS_REQUIRE_DATA=ON`.

| test | what it runs | data |
|---|---|---|
| `aica.dsp.api` | the core's interface on itself: `RunSample` from every step against 128 `StepOne`, state out and in before every step, two cores in turns, every register as the CPU has it, fields with bits their registers do not have, a wave RAM of 4, 8 and 16 MB. And the known-answer programs: the shifter, the address of a memory step, the ring, the read latch, the MIXS banks, on programs small enough to say by hand what they leave | none |
| `aica.dsp.diff.random` `aica.dsp.diff.sparse` `aica.dsp.diff.directed` | the core against the DSP of the caique sample model on generated programs, compared behind every step | a caique-rtl checkout |
| `aica.dsp.case.<case>` | a DSP program caique ran on a Dreamcast, on the model with the core in step and on the core alone, against the console's files | a caique-rtl checkout |
| `aica.dsp.capi` | the C interface of the core (`wasm/aica_dsp_capi.h`), compiled for the host, against the core called itself, call for call, on random programs and on the known-answer programs | none |
| `aica.dsp.wasm` | `aica_dsp_core.wasm` and its loader in node against the core compiled for the host and called itself, call for call, on the same programs | emcc (on the PATH, or an emsdk in `EMSDK` or `~/emsdk`) and node; skipped without them |
| `aica.dsp.wasm.emsdk` `aica.dsp.wasm.noemcc` | `wasm/build.sh` finding its emcc, run by sh on a PATH that has none: with the emsdk in `EMSDK` it builds the module, with no emsdk there it says that it has no emcc | an emcc that is in an emsdk, and node; skipped otherwise |
| `aica.dsp.wasm.devtools` | the module the UI commits, `src/wasm/aica-dsp-core/` of `NULLDC_DEVTOOLS_DIR` (the playground's DSP): its `PROVENANCE.json` has the keys `repository`, `treeCommit`, `dspCoreCommit`, `dirty`, `emscripten`, `apiVersion`, `files` and no other, and the two files beside it are those `files` names, size and SHA-256; the trace of `aica.dsp.wasm` replayed on its loader and wasm gives every answer of the native core; and where the emcc `build.sh` uses (on the `PATH`, else in the emsdk of `EMSDK`, or of `~/emsdk` when `EMSDK` is not set) says by `emcc -dumpversion` the `emscripten` of `PROVENANCE.json`, the two files are the bytes of a fresh `build.sh` of this tree (`devtools_test.mjs`) | node and the UI checkout; no emcc. A test whose data is missing without the checkout |
| `aica.dsp.case.<case>.emu` | the seven cases anchored directly on the console's files (below) on the emulator's own sound chip, against those files and against the core alone | a caique-rtl checkout |
| `aica.dsp.emu.lockstep` | generated programs through the emulator's sound chip, with CPU writes of every form and channels that send, against a core of the test's own, behind every sample | none |
| `aica.dsp.emu.state` | the mix takes EFREG from the core, the DSP device's reset is the core's power on, its save state is the core's blob, the AICA's save state keeps the bus a channel sends to, "Disable Sound" hands the audio stream a pair of zeroes in every sample, which the harness folds, and changes nothing else | none |
| `debugger.sound.oracle.dsptest.jit` `.interp` | the DSP program of the sounding guest (`tests/debugger/guest/dsptest`) on the emulator, on the JIT and on the interpreter, against the core and the caique model in lockstep on the same writes and schedule (`aica_dsp_guest_oracle`, below): the field `dsp` of the line of a copy of the guest whose sends are 0 is what the tool computes | a caique-rtl checkout, the emulator, python3 |

All but `aica.dsp.capi` and the `aica.dsp.wasm` tests count their cases into checks and are gated on
`baseline.txt`: a check that passes fewer cases than its baseline, or has another number of cases, fails, and
ctest runs the tests with `--strict`, where more passes than the baseline fail as well until the baseline is
updated (`<executable> ... --update-baseline`) and committed. `KNOWN_FAILURES.md` has the reason for every check
that is below its total. `aica.dsp.capi` and `aica.dsp.wasm` are lists of answers that all have to be the same,
the two tests of `build.sh` are what the script returns and prints, and `aica.dsp.wasm.devtools` is a list of checks
that all have to hold.

## Step for step: `dsp_lockstep`

The caique model runs the 128 steps of a sample in one loop. `caique_model.cpp` compiles the model as it is in
the checkout, with a hook in front of the one place where its DSP starts a step (the decode of the MPRO row), so
a test gets control between any two steps. `DspLockstep` runs the core next to the model: both get the same CPU
accesses and events, and behind every step everything the DSP has is compared: TEMP, MEMS, both MIXS banks,
EFREG, MDEC_CT, ACC, FRC_REG, Y_REG, ADRS_REG, the read latch, its copy, what IWT stores, the NOFL of the last two
steps, and the wave RAM a write can reach (each has its own wave RAM). The view the core gives of the step
(`AicaDspStepView`, what the debugger and the playground show) is compared in all of its words: INPUTS, SHIFTED,
X, Y, B and the wave RAM access with its address and word are worked out from the model's state in front of the
step, the rest is the model's state behind it. The test fails as a problem, whatever the counts, when the model
does not announce 128 steps for a sample: the checkout's model is then not the one the hook was written for.

A second core runs every sample with `RunSample`, from step 0 or from a step in the middle, and has to be where
the first core is behind every sample. That is the check named `run-samples`.

Two ways to run a sample:

* the whole model (`StepChip`): its sound generator makes the sends and the channel fetches. The cases run this
  way.
* the model's DSP alone (`StepDsp`), on events the test makes up. The generated programs run this way. The
  sample model has bus reads between samples only; one in front of a later step is written into the model's
  latch at that step, which is where caique's clocked model has it. For those steps the reference is the model's
  code for the latch with a read placed by the test, not a behaviour of the sample model as it comes.

## The generated programs

`aica_dsp_diff`, from fixed seeds:

| set | programs | what |
|---|---|---|
| `random` | 1500 x 16 samples | every bit of MPRO, COEF, MADRS, TEMP, MEMS and the wave RAM random; random ring (also across the end of the wave RAM), MDEC_CT anywhere and near 0, the test mode of MDEC_CT in 1 of 16 |
| `sparse` | 4000 x 24 samples | programs whose steps do little, half of them on operands from the edges of their ranges |
| `directed` | 40048 programs | `read`: MRD and its IWT at every step and distance 0 to 5, with and without NOFL, a second read behind, MWT in the step, a channel in the slot. `write`: MWT at every step in every shifter mode, float or not, ring or table, ADRS_REG, NXADR. `bus-read`: a bus read in front of every step, next to a read of the DSP. `fetch`: a channel in every slot against nothing, MRD, MWT and both. `ring`: every ring size at its ends. `test-mode`: bit 15 of 0x2804 on and off. `float`: the float format through MWT, MRD and IWT. `datapath`: every combination of the selects and the shifter on edge operands behind a chain that fills ACC. `mixs`: sweeps with several sends to a bus, past 20 bit, or none, and a CPU that writes buses. `cpu`: every register written and read back. And without a program: the float format against the model's header on all 2^24 values and all 2^16 words |

Every sample also gets random sends, channel fetches, bus reads and EXTS, and between two samples the CPU writes
and reads registers and reads wave RAM. The checks `did:*` count what the compared steps did (reads, writes,
slots lost to a channel, IWT of both kinds, bus reads): a generator that stops producing them changes the
baseline.

## The known-answer programs

`dsp_known.cpp`: programs whose answer is a number that is written down in the test, with the rule of the
hardware it follows from. They need no data, so they hold what they cover in a build without a caique checkout
as well:

| group | what is known |
|---|---|
| `shifter` | SHIFTED in the four modes for 18 values of ACC: inside 24 bit, where twice the value leaves 24 bit, where the value does, at the ends of 26 bit and behind them. Which mode doubles, which saturates and at 0x7FFFFF / -0x800000, which cuts; the sum in ACC and its wrap at 26 bit; EWT |
| `address` | the byte of the wave RAM that an MWT writes and an MRD reads, 28 cases on a wave RAM of 2, 4, 8 and 16 MB: TABLE with its wrap at 64K words, upwards (NXADR, ADRS_REG) and downwards (a negative ADRS_REG), every ring size, MDEC_CT under the ring's mask, ADRS_REG from SHIFTED and from INPUTS, RBP up to 0xFFF, the last word of 2, 4 and 8 MB, and the ring that goes on behind it: at byte 0 where the wave RAM ends there, at the next byte where it is larger. The write has to be the only two bytes of the wave RAM that changed; the read finds other words planted where an address that is cut short, or not cut, would read |
| `ring` | a word written at MADRS is read at MADRS + 1 by the next sample and at MADRS + 2 by the one after, with MDEC_CT going through 0 |
| `rules` | the read latch: the distances from MRD to IWT, a second read behind the first, a bus read, MRD with MWT, a channel in the slot, NOFL of IWT; EFREG, ACC of step 127, MDEC_CT |
| `mixs` | the sends of a sweep, the two banks, the late sends, the CPU's bank |

The programs are written on a machine that is nothing but the calls a user of the core has, and every program
runs twice, by single steps and with `RunSample`. `aica.dsp.api` runs the set on the core called itself, and
counts it into its checks. `aica_dsp_wasm_trace` runs the same set through the C interface, and its trace takes
it to the module: a defect that is in `aica_dsp_core.wasm` only, in a rule the set knows, fails `aica.dsp.wasm`.

### Wave RAM above 2 MB

The core takes any power of two, the header names 8 MB, and the emulator has builds with 8 MB. The caique model
has a wave RAM of 2 MB and no other (`AicaModel::RAM_SIZE`), so the tests against the model, `aica.dsp.diff.*`
and `aica.dsp.case.*`, run on 2 MB and say nothing about more. What does:

* `address` of the known-answer programs, on 4, 8 and 16 MB, natively and in the module;
* `ram-sizes` of `aica.dsp.api`: 160 random programs on each of 4, 8 and 16 MB, with the ring anywhere RBP reaches
  (the first 8 MB, or all of a smaller wave RAM), in the upper half of that, with 64K words that start in its last
  16 KB, and as far up as RBP goes. Every memory step has to be at the byte that the address rule, written out in
  the test, gives for the state in front of it, and a second core that runs the samples with `RunSample` has to
  leave what the steps leave, in the state and in all of the wave RAM. The checks at the end count the memory
  steps in the upper half, behind the end of the wave RAM and, on 16 MB, above 8 MB: a generator that stops
  reaching them fails.

16 MB is more than the hardware has. It is there because a ring at the top of RBP's 8 MB ends 126 KB above them:
only a wave RAM with room there shows that the core's addresses are not cut at 8 MB.

## Fields with more than their bits

`aica_dsp_core.h` says which bits each field of the state has, and that the core computes with those and ignores
what else a field holds (a debugger and the typed arrays of the module's loader write fields directly). The check
`fields` of `aica.dsp.api` takes 96 states with every such field filled beyond its bits, with random bits, with
all ones and with the largest and smallest numbers of its type, and holds the core against itself on the same
state cut to its bits (what `LoadState` makes of it): what the CPU reads at every offset, the view of every step
to the end of the next sample, and the state and wave RAM that a send to each bank, the steps and `RunSample`
leave, cut again, have to be the same. Under `-fsanitize=undefined` the same check is what shows that no such
value overflows a sum.

## The C interface and the module

`aica_dsp_wasm_trace` runs 108 random programs and the known-answer programs on the core twice, call for call: on
an `AicaDsp` with the calls of `aica_dsp_core.h`, and through the C interface compiled for the host. The second
has to answer what the first answers, and that is `aica.dsp.capi`: every register as the CPU and as a debugger
read it, the registers by index after writes with more bits than they have, the steps up to a breakpoint, the
state blob, the wave RAM, the view of every single step, and every array behind a pointer of the interface as
the numbers it holds, over the length the header gives it. What a debugger's write and read do other than the
CPU's, where a run stops and how a value is cut to its register are written out in the tests a second time: they
are not taken from the interface.

The random programs take turns on a wave RAM of 64 KB, 2 MB and 256 KB. 64 KB is smaller than a ring, so the
addresses wrap in the wave RAM; 256 KB is more than the 128 KB that TABLE reaches, so TABLE's own wrap is what
limits it; 2 MB is the console's. The known-answer programs go up to 16 MB.

The trace the tool writes has every call with the answer of the core that was called itself. `aica.dsp.wasm`
makes the calls on the module through the loader's object and compares with those answers, the arrays as the
loader's typed arrays over their whole length. So the C interface and the loader are both on the side that is
tested: a defect in `aica_dsp_capi.cpp` fails both tests, one in `aica_dsp_core.mjs` fails `aica.dsp.wasm`.

## The cases

`aica_dsp_cases <case>`. A case is a C program of caique on a small register and wave RAM interface, compiled
from the checkout as it is. It runs on

* **the model**, the whole sound chip, with the core in step. What the case writes out is the model's answer.
* **the core alone**, with a silent sound generator around it: 64 channels that send 0 to their ISEL bus.

Both on the time base of caique's own harness (an access costs 2400 ns, a sample starts every 1 / 44100 s).

| case | what it measures | model | core alone | compared with the console |
|---|---|---|---|---|
| `dsp_basic` | multiplier, shifter, operand selects, FRC_REG, Y_REG, the first memory steps, MIXS and EXTS as inputs | yes | yes | `dsp_basic.txt`, 309 lines, runs `hw9` and `hw` |
| `dsp_mem` | addressing: ring sizes, RBP, TABLE, NXADR, ADRS_REG; even MWT; read pairs; EFREG twice; ACC width | yes | yes | `dsp_mem.txt`, 26 lines, `hw9` and `hw` |
| `dsp_mem2` | ADRS_REG's sign, the NOFL of IWT | yes | yes | `dsp_mem2.txt`, 24 lines, `hw9` and `hw` |
| `dsp_mem3` | MRD and MWT in one step and in adjacent steps, the CPU's read in the latch | yes | yes | `dsp_mem3.txt`, 12 lines, `hw9` and `hw` |
| `dsp_wslot` | MWT and MRD in every pair of adjacent steps, 15 + 15 in 30 steps | yes | yes | `dsp_wslot.txt`, 14 lines, `hw9` and `hw` |
| `dsp_unpack` | all 65536 words through MRD and IWT | yes | yes | `unpack.bin`, 65536 words, and the report, `hw9` and `hw` |
| `dsp_pack` | all 2^24 values through a float MWT, checksum over all results | no | yes | the report with its CRC, `hw9` and `hw` |
| `dsp_temp` | dumps of TEMP while a program fills it | yes | yes | not compared: when a dump is taken is the console's timing |
| `dsp_coll` | 18 runs with a playing channel: what the DSP reads and writes in the channel's slot, logged every sample | yes | no | the 18 logs, `hw9` and `hw`, sample for sample at the shift where they overlap |
| `sh4_latch` | five reads logged every sample while the SH4 reads wave RAM and a channel plays | yes | no | the two runs without SH4 reads, `hw11`; the two with them: see below |
| `mdec_hold` | bit 15 of 0x2804 held for 22 lengths of time | yes | no | not compared: the count depends on the clock inside the hold |

So, of what the console answered:

* **directly on the core** (no model in between): dsp_basic, dsp_mem, dsp_mem2, dsp_mem3, dsp_wslot, dsp_unpack,
  dsp_pack. The core alone writes the console's report and, for dsp_unpack, the console's 65536 words. Checks
  `console-<run>:core:*`.
* **through the model**: dsp_coll, and the runs `idle` and `ch` of sh4_latch. The model writes the console's
  logs, and the core is the model's DSP at every step of the run. The model's part is counted by the test
  itself, in `console-<run>:model:*`: if the model in the checkout stops reproducing the console's files, those
  counts drop below the baseline and the test fails, it does not go on comparing the core with something that is
  no longer the console. The same counts are taken for the seven cases above.
* **sh4_latch with SH4 reads** (runs `sh4`, `sh4_ch`): the sample model cannot replay them, where an SH4 read
  falls is the bus's timing. The core is given, for every sample of the console's log, reads of the SH4's
  word (0xBEEF1234) in the slots that explain the five logged words, plus reads behind the two odd MRDs that must
  not show, and has to return the console's five words. The words of the reads are the two halves of the SH4's
  word and nothing else: a sample with any other word, or with one of the odd reads replaced, does not pass.
  This shows that the latch rule of the core produces every sample the console logged; it does not show when
  the bus reads. Checks `console-hw11:core:sh4`, `console-hw11:core:sh4_ch`.
* **not anchored on the console**: dsp_temp, mdec_hold (the model's behaviour only, with the core in step), and
  caique's `arm_latch` (the ARM7's reads: it needs the ARM7 and the bus).

dsp_pack does not run on the model: it is 4.1 million samples, and the float format is compared with the model's
on every value in `aica.dsp.diff.directed`.

The case sources use C that is not C++, so the tests project enables C when the checkout is there.

## The emulator's sound chip: `emu_aica.h`

`EmuAica` is the emulator's AICA device with its sound generator and its DSP device, which holds the core, built
from the emulator's own `aica_mmio.cpp`, `sgc_if.cpp` and `dsp.cpp` with the emulator's definitions and flags;
`emu_aica.cpp` has what they reference of the rest of the emulator, after the stubs of `tests/arm7` and
`tests/debugger/regs/aica` (no SH4, no ARM7, no scheduler). A register access is `AICA::ReadReg` /
`AICA::WriteReg`, the CPU's path with its byte and slot handling; a sample is `libAICA_TimeStep()`, the
emulator's time step of the AICA; the wave memory is the device's. The audio stream is a stand-in that keeps what
each sample hands it, and so is the CD-DA drive: off it gives silence, on a pair of its own in every sample,
neither half 0 (`EmuCddaPair`).

* **`aica.dsp.case.<case>.emu`**: `dsp_basic`, `dsp_mem`, `dsp_mem2`, `dsp_mem3`, `dsp_wslot`, `dsp_unpack` and
  `dsp_pack` on that chip (`aica_dsp_cases <case>.emu`). A register access of the case is a 32-bit access of the
  SH4. No channel is keyed, so every channel sends 0 to the bus of its ISEL: the silent sound generator of the
  core machine, which runs next to it (`emu-as-core:lines`). The emulator puts no CPU read of the wave memory into
  the DSP's latch (`AicaDsp::BusRead` is not wired, the cut of the M3 round, M3-D10): five lines of `dsp_mem3`
  are not the console's for that (`KNOWN_FAILURES.md`). `dsp_coll` and `sh4_latch` key channels, and the
  emulator's channels are not caique's: they do not run here.
* **`aica.dsp.emu.lockstep`** (`aica_dsp_emu lockstep`): 1200 programs of 48 samples from the generator of
  `aica_dsp_diff` (`dsp_gen.h`), random and sparse, each on a new machine with random wave memory, against a core
  of the test's own. Every write of the program and between the samples is in one of the forms a CPU has: a 16-bit
  or a 32-bit write of a slot, its two bytes, one of them, a write of the upper half of the slot (16-bit or a byte)
  with a value other than the low half's behind a write of the low half; between the samples also the upper half
  alone, with no write of the low half in front of it, which on a TEMP, MEMS, MIXS or EFREG slot the program or the
  sweep has written since leaves the DSP with another value than the file's word. The test keeps the register file
  as its CPU wrote it and gives its core
  what the rules of the file make of each write: the file's word of the slot when the low half of a slot of the
  DSP was written, nothing for the upper half. One to three keyed channels on each side, IMXL = DISDL, the pan all
  to that side, MVOL 15, no output of the DSP or of the CD-DA mixed: a channel's send is then its direct output, so
  the pair a sample hands the stream is, side by side, the sum of the sends to the left bus and to the right bus.
  The test's core gets, behind a `SweepBegin`, those sums times 16 and 0 for every other bus a channel names, the
  CD-DA pair of the stand-in in `EXTS`, and runs the sample. Behind every sample the two state blobs are equal;
  CPU reads between the samples (of every form, also of the offsets next to the DSP's) give what the test's core
  answers, the upper half of a slot 0, the file elsewhere; and behind every program the two wave memories are
  equal. The checks `did:*` count what was done: samples with sends on both buses, with a CD-DA pair, writes of
  each form, writes the core was given, and per array the writes of an upper half alone on a slot where the DSP
  given the file's word again would differ (`did:upper-alone-moved:*`).
* **`aica.dsp.emu.state`** (`aica_dsp_emu state`): `efreg-mix`, a program whose EWT writes 0x1234 into EFREG3: the
  pair of each sample with EFSDL 15 differs from the pair with EFSDL 0 by the VOLPAN of the core's EFREG3, which is
  0x1234; `reset`, the DSP device's `Reset(false)` behind a program that ran leaves the core's blob that of a core
  after `Reset()`; `save-load`, the DSP device's save state, a CPU write and a sample, the save state loaded:
  the core's blob of before, and the save state is the core's blob, `StateSize()` bytes; `refused-blob`, a save
  state whose DSP part has another header resets the DSP; `save-bus`, a channel keyed with IMXL 15 and ISEL 13, the
  AICA's and the DSP's save states, ISEL written with 6 and a sample (it sends to bus 6), the states loaded: the
  two samples behind the load send to bus 13 and to no bus 6, and the core is that of a machine that ran the same
  samples with no save, write and load. 13 has bit 3 set and differs from 6 in the low three bits: a load that kept
  three bits of the bus would send to bus 5; `nosound`, "Disable Sound" (`settings.aica.NoSound`, the emulator's
  `config:aica.NoSound`) on a machine that sounds (a channel keyed with a direct output to both sides and a send to
  the DSP, the program of `efreg-mix` at EFSDL 15, the CD-DA stand-in in EXTS and in the mix), against a machine
  without it, over 64 samples: behind every sample the muted machine has handed the audio stream exactly one pair
  more, and that pair is (0, 0) where the other machine's is not; the harness (`aica_hash.h`) has folded what each
  machine handed the stream, the other's pairs and as many pairs of zeroes; and the DSP's blob, the AICA's save
  state and the register file are the other machine's. With the setting off again, the 65th sample hands the stream
  the other machine's 65th pair. The stream is the speed limit of the emulator (`LimitFPS`): a sample that hands
  it nothing, or folds another pair than it hands, fails, which no run of the emulator with `audio:backend=none`
  shows.

## The sounding guest's DSP: `aica_dsp_guest_oracle`

The guest of `tests/debugger/guest/dsptest` loads a DSP program of 16 steps between the first tick of the AICA and
the second (its README, "The DSP"). Its lines move with the program, and what justifies them is the model, which
ends at EFREG and has a sound generator that is not the emulator's: so the oracle holds the DSP's state where its
inputs are known. `soundlines.py --mode oracle` builds a copy of the guest whose first channel has `IMXL` 0, so that
every send to the DSP is exactly 0 (the emulator's attenuation of a send is `TL` plus 255 for `IMXL` 0, capped by
what the envelope leaves, and its table of levels has 0 there: `hw/aica/sgc_if.cpp`, `UpdateAtts`,
`ChannelEx::Step`), runs the emulator's line on it at N = 2000, and runs

    aica_dsp_guest_oracle --elf <the copy> --ticks 2000

The tool reads from the ELF, by its symbols, the table `_start` writes (`g_table` to `g_table_end`: the writes of
the DSP's registers and of the channels' `ISEL`) and the block `g_dsp` (`COEF`, `MADRS`, `MPRO`, each 32-bit word
the low half of a register), and runs the core and the model's DSP in lockstep (`DspLockstep`, `StepDsp`) on the
guest's schedule: tick 1, 32 samples of the empty program from the state after reset; then every write, in the
order `_start` makes them, the table first; then 32 samples a tick. Every sample has the emulator's sweep:
`SweepBegin`, then a send of 0 to every bus a channel's `ISEL` names (bus 0 before the table, bus 0 and 3 behind
it), EXTS 0, no channel fetch and no bus read. It folds the core's blob behind every tick as the harness folds the
line's `dsp` and prints `dsp=<hex>`; exit code 1 when the model and the core differ. The sends of 0 are part of
it: the blob has the buses a sweep sent to, and a send marks its bus whatever the value. The line's `dsp` folds
the DSP's whole state at every tick, so the test holds every datapath register, TEMP, MEMS, MIXS, EFREG and
MDEC_CT of the emulator's DSP to the model's, tick by tick, on both engines; that the set-up's writes all fall
between the first tick and the second on both engines is what the schedule rests on (the guest's README, "The
first tick").

## Looking at a failure

    aica_dsp_diff directed --verbose          # the first difference of each family, with sample, step and register
    aica_dsp_cases dsp_basic --verbose        # every line that is not the console's, both versions
    aica_dsp_api --verbose                    # every check that does not hold
    aica_dsp_wasm_trace                       # the first answers of the C interface that are not the core's, and
                                              # the first known answers the core does not give
    node wasm_test.mjs <build dir>/aica_dsp/wasm/aica_dsp_core.mjs <build dir>/aica_dsp/wasm/aica_dsp_core.wasm \
         <build dir>/aica_dsp/wasm_trace.bin

A run with `--update-baseline` rewrites the rows of that test in `baseline.txt` and leaves the others.

## Speed

`aica_dsp_bench [samples]` prints samples a second on three programs. It is not a test: the numbers are the
machine's. The core's README has the ones of the machine it was written on.

## Files

    dsp_suite.*        options, report, checks and baseline; a reproducible random stream
    dsp_known.*        the known-answer programs, on a machine of the core's calls
    dsp_gen.*          the generator of programs of aica_dsp_diff, also for aica.dsp.emu.lockstep
    caique_model.*     the caique sample model behind a wrapper, with the hook between its steps
    dsp_lockstep.*     the core next to the model, step for step; the second core for RunSample
    diff_main.cpp      aica.dsp.diff.*: the generated programs
    cases_main.cpp     aica.dsp.case.*: the three machines, caique's case interface on them, the console's files
    emu_aica.*         the emulator's sound chip on a machine of its own, and its stand-ins
    emu_main.cpp       aica.dsp.emu.*: the lockstep with a core of the test's own, the mix, the reset, the save state
    api_main.cpp       aica.dsp.api: the interface, and the known-answer programs on the core
    wasm_trace.cpp     aica.dsp.capi: the C interface against the core's own calls, on random programs and on
                       the known-answer programs; writes those calls and the core's answers for aica.dsp.wasm
    wasm_test.mjs      aica.dsp.wasm: the replay on the module, in node
    devtools_test.mjs  aica.dsp.wasm.devtools: the UI's module against this tree, its provenance, the replay of
                       wasm_test.mjs on it and its bytes against a fresh build
    guest_oracle_main.cpp
                       aica_dsp_guest_oracle: the sounding guest's DSP program on the core and the model in
                       lockstep, for debugger.sound.oracle.dsptest.*
    bench_main.cpp     aica_dsp_bench
