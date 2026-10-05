# AICA DSP core tests

What is tested is `libswirl/hw/aica/dsp_core`: the DSP of the AICA as one self-contained core (its README has the
hardware rules and the interface). The tests need nothing of the emulator.

The reference is the caique project (`caique-rtl`), which is not in this repository: its sample model of the AICA,
the DSP programs it ran on a Dreamcast and what the console answered are compiled in and read from a checkout,
`-DNULLDC_CAIQUE_DIR=<path to caique-rtl>` (default `../caique-rtl` next to the repository if it is there).
Without it the tests that need it are skipped, or fail with `-DNULLDC_TESTS_REQUIRE_DATA=ON`.

| test | what it runs | data |
|---|---|---|
| `aica.dsp.api` | the core's interface on itself: `RunSample` from every step against 128 `StepOne`, state out and in before every step, two cores in turns, every register as the CPU has it, the MIXS banks, the read latch rules on programs small enough to say by hand what they leave | none |
| `aica.dsp.diff.random` `aica.dsp.diff.sparse` `aica.dsp.diff.directed` | the core against the DSP of the caique sample model on generated programs, compared behind every step | a caique-rtl checkout |
| `aica.dsp.case.<case>` | a DSP program caique ran on a Dreamcast, on the model with the core in step and on the core alone, against the console's files | a caique-rtl checkout |
| `aica.dsp.wasm` | `aica_dsp_core.wasm` and its loader in node against the core compiled for the host, call for call | emcc (on the PATH, or an emsdk in `EMSDK` or `~/emsdk`) and node; skipped without them |

All but `aica.dsp.wasm` count their cases into checks and are gated on `baseline.txt`: a check that passes fewer
cases than its baseline, or has another number of cases, fails, and ctest runs the tests with `--strict`, where
more passes than the baseline fail as well until the baseline is updated (`<executable> ... --update-baseline`)
and committed. `KNOWN_FAILURES.md` has the reason for every check that is below its total. `aica.dsp.wasm` is a
list of answers that all have to be the same.

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

## Looking at a failure

    aica_dsp_diff directed --verbose          # the first difference of each family, with sample, step and register
    aica_dsp_cases dsp_basic --verbose        # every line that is not the console's, both versions
    aica_dsp_api --verbose                    # every check that does not hold
    node wasm_test.mjs <build dir>/aica_dsp/wasm/aica_dsp_core.mjs <build dir>/aica_dsp/wasm/aica_dsp_core.wasm \
         <build dir>/aica_dsp/wasm_trace.bin

A run with `--update-baseline` rewrites the rows of that test in `baseline.txt` and leaves the others.

## Speed

`aica_dsp_bench [samples]` prints samples a second on three programs. It is not a test: the numbers are the
machine's. The core's README has the ones of the machine it was written on.

## Files

    dsp_suite.*        options, report, checks and baseline; a reproducible random stream
    caique_model.*     the caique sample model behind a wrapper, with the hook between its steps
    dsp_lockstep.*     the core next to the model, step for step; the second core for RunSample
    diff_main.cpp      aica.dsp.diff.*: the generated programs
    cases_main.cpp     aica.dsp.case.*: the two machines, caique's case interface on them, the console's files
    api_main.cpp       aica.dsp.api
    wasm_trace.cpp     what aica.dsp.wasm replays: the calls and answers of the native core
    wasm_test.mjs      aica.dsp.wasm: the replay on the module, in node
    bench_main.cpp     aica_dsp_bench
