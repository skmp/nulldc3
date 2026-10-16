# Baseline: 2ndmix, this machine

The speed of the emulator on `2ndmix.elf`, from `tests/perf/perf.py` at its defaults on a quiet machine: the
1 minute load average was below 10, on 24 threads, in front of every run of a row. The one row for which it was
not says so.

| | |
|---|---|
| program | `2ndmix.elf` (KallistiOS 2.2.2 example), booted under reios |
| length | 60 emulated seconds per run (3596 vblanks), 5 runs per row, one after the other |
| settings | `audio:backend=none`, `config:Dreamcast.RTC=946684800`, everything else default: the "fast" timing of today, `Dynarec.idleskip` on, ARM7 and DSP interpreters |
| emulators | the tree at `0f571cf7`, built RelWithDebInfo with g++ 13.3 by the tests project |
| machine | AMD Ryzen 9 3900X (12 cores, 24 threads), Linux 6.18.33.2-microsoft-standard-WSL2 |
| measured | 2026-10-05 between 16:53 and 17:00, during the verification of that tree |

Two figures per row, both in percent of real time (100 = a Dreamcast): the speed by wall clock, from the first
vblank of the run on, and the speed by CPU time, emulated time over the CPU time of the process
(`tests/perf/README.md`). Each is the median of the 5 runs, with the lowest and the highest. Every row is of
2026-10-05 and of the tree at `0f571cf7`, and was measured during its verification; none was measured again for
this file.

| build | renderer | engine | `perf.py` started | speed | by CPU time | load in front of the runs |
|---|---|---|---|---|---|---|
| default (TA_LLE) | `none`, headless | JIT | 16:53 | **1224.8** (1196.9 - 1240.9) | **1202.4** (1174.4 - 1216.7) | 5.3 - 6.6 |
| | | | 16:54, again | **1223.7** (1182.3 - 1246.2) | **1200.2** (1159.6 - 1222.8) | 3.1 - 3.5 |
| default (TA_LLE) | `none`, headless | interpreter | 16:53 | **553.7** (546.2 - 556.0) | **551.1** (543.1 - 552.9) | 3.7 - 5.1 |
| | | | 16:54, again | **545.5** (482.4 - 549.6) | **542.4** (479.3 - 546.4) | 3.0 - 3.2 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `none`, headless | JIT | 16:55 | **1193.8** (1177.1 - 1242.6) | **1169.3** (1145.7 - 1216.7) | 4.2 - 6.8 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `none`, headless | interpreter | 16:55 | **545.6** (485.0 - 561.4) | **542.0** (482.0 - 557.9) | 6.7 - 7.0 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `gles` on the GPU, in a window | JIT | 16:57 | **1111.3** (1087.3 - 1199.3) | **758.5** (740.7 - 833.5) | 8.6 - 9.5 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `gles` on the GPU, in a window | interpreter | 16:57 | not measured on a quiet machine | | 9.7 - 13.8 |

The last row, for illustration only: 401.6 (370.0 - 496.2), by CPU time 286.8 (278.9 - 356.9). Its two runs at a
load below 10 gave 492.1 and 496.2 (353.3 and 356.9 by CPU time), the three at 10.0 to 13.8 gave 370.0, 401.6 and
378.1.

GL for the last two rows: `glxinfo -B` reported `Accelerated: yes` with `GALLIUM_DRIVER=d3d12`, and the emulator
printed `OpenGL renderer: D3D12 (NVIDIA GeForce RTX 2060)`. With `gles` a frame that arrives while the previous
one is still being drawn is dropped: the speed of those rows is that of the emulation with the hardware renderer
running, not a frame rate.

## The spread on a quiet machine

* **A median of five repeats.** The default build twice, one invocation after the other: 1224.8 and 1223.7 for
  the JIT (0.1 % apart), 553.7 and 545.5 for the interpreter (1.5 %).
* **A single run does not.** The headless JIT runs are from 3.4 % below to 4.1 % above the median of their row,
  the JIT runs with `gles` from 2.2 % below to 7.9 % above. One interpreter row has its runs within 1.4 % of its
  median; in the other two the last run is 11 to 12 % below it (482.4, 485.0) and the one before it 3 to 5 %
  (520.1, 529.8), at a load that had not moved (3.0 to 3.2, 6.7 to 7.0).
* **The two figures agree within about 2 % headless.** The speed is 0.4 to 2.7 % above the speed by CPU time in
  the 30 headless runs, 0.5 to 2.1 % for the medians of the rows. The CPU time counts the process from its start
  to its end and the speed counts from the first vblank: a run has 0.05 to 0.14 CPU seconds more than wall
  seconds, whether it takes 5 seconds or 12. With `gles` a JIT run has 2.2 to 2.6 more, of the thread that
  draws.
* **The table does not rank the two builds.** Their rows were measured one after the other, and the ranges of
  the default and the TA_HLE build overlap for both engines.

## What load does

The same binaries and the same command at a higher load, to hold against the rows above:

| started | build, renderer, engine | runs | load | speed | by CPU time |
|---|---|---|---|---|---|
| 16:59 | TA_HLE, `none`, JIT | 5 | 16.5 - 18.9 | 980.2 (950.0 - 982.3) | 955.3 (932.1 - 959.8) |
| 16:59 | TA_HLE, `none`, interpreter | 5 | 14.7 - 18.4 | 468.2 (414.5 - 548.2) | 464.9 (411.9 - 544.8) |
| 17:19 | default, `none`, JIT | 1 | 31.4 | 714.3 | 738.1 |
| 13:32, tree `20179f0f` | default, `none`, JIT | 5 | 68.0 - 79.4 | 592.9 (360.9 - 669.8) | 745.0 (650.1 - 769.6) |
| 13:32, tree `20179f0f` | default, `none`, interpreter | 5 | 53.5 - 65.9 | 263.4 (175.7 - 372.7) | 317.1 (302.8 - 351.4) |

The emulator sources of `20179f0f` are those of `0f571cf7`.

* **The speed falls by up to a factor of three.** At a load of 53.5 to 79.4 a single run is 3.4 times below the
  quiet median for the JIT (360.9) and 3.2 times for the interpreter (175.7), the median of five 2.1 times for
  both.
* **The speed by CPU time falls less, and still falls**: 1.6 and 1.7 times at that load. It leaves out the time a
  run waits for a core, not what the other work does to the core it runs on: at a load of 15 to 19, four minutes
  after the quiet rows, both figures of the TA_HLE build were 18 % lower for the JIT and 14 % for the
  interpreter, together.
* **The load average does not say which figure to expect.** The TA_HLE JIT row at 13:53 the same day, at a load
  of 15.9 to 17.4 (tree `20179f0f`, 5 runs), had a median of 1352.0 and of 1235.6 by CPU time, above the quiet
  row. Why is not known; its two figures are 8 to 13 % apart, run by run, where a quiet JIT run has 2 to 3 %.

Two binaries are therefore compared in one session: alternating them, run by run, and by the CPU seconds of each
run (the JSON of `perf.py` has them). A later measurement held against this file says something only from a
machine as quiet as this one was, and then no more than the spread above allows: two rows of one binary are up to
1.5 % apart.

## Measuring again

With a 1 minute load average below 10 in `uptime` first, and `--json <file>` to keep every run with its load:

    python3 tests/perf/perf.py --emulator <build>/system/emulator/nulldc3 --program 2ndmix.elf
    python3 tests/perf/perf.py --emulator <build>/system/emulator-ta-hle/nulldc3 --program 2ndmix.elf
    python3 tests/perf/perf.py --emulator <build>/system/emulator-ta-hle/nulldc3 --program 2ndmix.elf --renderer gl
