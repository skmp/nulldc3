# Baseline: 2ndmix, this machine

Measured 2026-10-05 with `tests/perf/perf.py`, tree at `20179f0f` (branch `skmp/nulldc3-t20`), emulators built
RelWithDebInfo with g++ 13.3 by the tests project.

| | |
|---|---|
| program | `2ndmix.elf` (KallistiOS 2.2.2 example), booted under reios |
| length | 60 emulated seconds per run (3596 vblanks), 5 runs per row, one after the other |
| settings | `audio:backend=none`, `config:Dreamcast.RTC=946684800`, everything else default: the "fast" timing of today, `Dynarec.idleskip` on, ARM7 and DSP interpreters |
| machine | AMD Ryzen 9 3900X (12 cores, 24 threads), Linux 6.18.33.2-microsoft-standard-WSL2 |
| load | **the machine was busy with other work throughout**, and no figure here is from a quiet machine: the 1 minute load average was 12 to 25 on 24 threads in front of the runs of the table, and between 12 and 79 over the half hour of all the measurements below |

Two figures per row, both in percent of real time (100 = a Dreamcast): the speed by wall clock, from the first
vblank of the run on, and the speed by CPU time, emulated time over the CPU time of the process
(`tests/perf/README.md`).

| build | renderer | engine | speed: median (lowest - highest) | by CPU time: median (lowest - highest) | load in front of the runs |
|---|---|---|---|---|---|
| default (TA_LLE) | `none`, headless | JIT | **853.1** (824.6 - 856.3) | **768.5** (728.2 - 825.3) | 23.9 - 25.2 |
| default (TA_LLE) | `none`, headless | interpreter | **421.2** (390.8 - 539.4) | **400.7** (362.8 - 497.2) | 18.4 - 23.4 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `none`, headless | JIT | **1352.0** (1143.3 - 1424.6) | **1235.6** (1013.6 - 1310.5) | 15.9 - 17.4 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `none`, headless | interpreter | **473.0** (441.5 - 598.0) | **431.9** (410.0 - 545.3) | 13.4 - 15.0 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `gles` on the GPU, in a window | JIT | **1308.3** (1178.3 - 1341.1) | **842.8** (733.4 - 873.7) | 13.4 - 15.2 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `gles` on the GPU, in a window | interpreter | **523.7** (333.1 - 577.4) | **344.0** (258.5 - 386.0) | 12.1 - 15.1 |

GL for the last two rows: `glxinfo -B` reports `Accelerated: yes` with `GALLIUM_DRIVER=d3d12` (without it the
context is llvmpipe, `Accelerated: no`), and the emulator printed `OpenGL renderer: D3D12 (NVIDIA GeForce RTX 2060)`.
No figure here is from a software rasteriser.

## What the load of the machine does to them

The first two rows again, the same binary and the same command, three times within twenty minutes:

| started | runs | engine | speed: median (lowest - highest) | by CPU time: median (lowest - highest) | load in front of the runs |
|---|---|---|---|---|---|
| 13:32 | 5 | JIT | 592.9 (360.9 - 669.8) | 745.0 (650.1 - 769.6) | 68.0 - 79.4 |
| 13:32 | 5 | interpreter | 263.4 (175.7 - 372.7) | 317.1 (302.8 - 351.4) | 53.5 - 65.9 |
| 13:48 | 3 | JIT | 837.1 (702.2 - 867.8) | 765.4 (649.5 - 808.6) | 50.0 - 58.8 |
| 13:48 | 3 | interpreter | 366.1 (312.2 - 413.0) | 333.2 (302.4 - 383.7) | 35.5 - 46.5 |
| 13:51 | 5 | JIT | 853.1 (824.6 - 856.3) | 768.5 (728.2 - 825.3) | 23.9 - 25.2 |
| 13:51 | 5 | interpreter | 421.2 (390.8 - 539.4) | 400.7 (362.8 - 497.2) | 18.4 - 23.4 |

Four more single runs of that JIT row between 13:57 and 13:59, at a load of 20 to 34, gave a speed of 945.3,
964.1, 1104.7 and 583.5, on 7.00, 6.93, 5.94 and 8.53 CPU seconds.

* **The speed moves by a factor of three.** One binary, one program: 360.9 to 1104.7 for the JIT and 175.7 to
  539.4 for the interpreter. The median of a row moved from 592.9 to 853.1 and from 263.4 to 421.2.
* **The speed by CPU time moves less, and still by more than half.** 649.5 to 1010.3 for the JIT (5.94 to 9.24
  CPU seconds for the 60 emulated ones), 302.4 to 497.2 for the interpreter. It leaves out the time a run waits
  for a core, which is what took the speed down to 360.9 while the CPU figure of that run was 650.1; it does not
  leave out what the other work does to a core that is running.
* **The load average does not say which figure to expect.** At a load of 24 to 25 the JIT gave 824.6 to 856.3
  here, and 984.3 to 1080.5 for the tree at `cbf82d1c` two hours earlier, at 21.8 to 26.2. The number is an
  average over the last minute of how many tasks wanted a core, not of what they did to it.
* **The rows do not rank the builds or price the GL renderer.** They were measured one after the other while
  the load changed. The default build is below the TA_HLE build in the table (853.1 against 1352.0); in the runs
  of the next section, which alternate the two, their ranges overlap.
* **None of this is a floor or a ceiling.** A quiet machine was not available for these measurements, and what
  one gives is not measured here.

On this machine the kernel counts 1.08 CPU seconds for every second of wall clock a thread spends on its core
(a thread that spins for 3 seconds: 3.24 s of user + system time; the same ratio over 200 windows of 20 ms, 1.076
to 1.082). A headless run that never waits for a core therefore has a speed by CPU time of 1 / 1.08 of its
speed, less the 0.1 to 0.2 CPU seconds that the start and the end of the process take (a run of one vblank). The
headless rows of the table, measured at the lower loads, have their median speed at 1.05 to 1.11 times their median
by CPU time.

With `gles` the emulator thread does not wait for the renderer: a frame that arrives while the previous one is
still being drawn is dropped. The speed of those rows is the speed of the emulation with the hardware renderer
running, not a frame rate, and their CPU time is that of every thread of the process, the thread of the user
interface that draws among them.

## Comparing two binaries

Not by two tables like the one above, measured at different times. What gave an answer here is alternating the
binaries, run by run, and looking at the CPU seconds of each (`/usr/bin/time` on the command of a `perf.py` run,
with `config:Exit.StateHash=yes` added). The binaries of this tree against those of `d92d8fcb`, the JIT with the
`none` renderer, four rounds of default `d92d8fcb`, default this tree, TA_HLE `d92d8fcb`, TA_HLE this tree,
between 13:57 and 13:59 at a load of 20 to 34:

| build | binary | CPU seconds of runs 1 to 4 | speed |
|---|---|---|---|
| default | `d92d8fcb` | 6.30 7.36 5.51 8.30 | 1072.6 899.3 1187.4 598.0 |
| default | this tree | 7.00 6.93 5.94 8.53 | 945.3 964.1 1104.7 583.5 |
| TA_HLE | `d92d8fcb` | 5.93 5.81 6.06 6.25 | 1111.2 1142.6 1023.1 1080.3 |
| TA_HLE | this tree | 6.62 5.18 7.18 6.09 | 989.3 1272.6 495.2 1085.8 |

Each binary differs more from itself, from one round to the next, than from the other tree's: these runs show
no difference between the two trees. The ranges of the two builds overlap (5.51 to 8.53 CPU seconds against 5.18
to 7.18). All eight runs of a build end in the same state hash, so the two trees did the same work. The runs of
one binary are up to a half apart here (5.51 to 8.30 CPU seconds): a difference smaller than that takes a
quieter machine or more rounds to see.

## Every run of the table

In the order they were made, with the 1 minute load average in front of each:

| build, renderer | engine | speed of runs 1 to 5 | by CPU time | CPU seconds | load |
|---|---|---|---|---|---|
| default, `none` | JIT | 824.6 855.6 852.1 853.1 856.3 | 728.2 768.5 825.3 779.1 757.1 | 8.24 7.81 7.27 7.70 7.93 | 25.2 24.9 24.9 24.1 23.9 |
| default, `none` | interpreter | 421.2 409.9 390.8 539.4 474.4 | 382.2 400.7 362.8 497.2 424.8 | 15.70 14.98 16.54 12.07 14.12 | 23.4 22.2 20.9 20.5 18.4 |
| TA_HLE, `none` | JIT | 1143.3 1288.4 1371.7 1424.6 1352.0 | 1013.6 1163.8 1270.8 1310.5 1235.6 | 5.92 5.16 4.72 4.58 4.86 | 17.3 17.4 17.4 16.4 15.9 |
| TA_HLE, `none` | interpreter | 598.0 552.8 469.0 473.0 441.5 | 545.3 509.4 431.9 431.8 410.0 | 11.00 11.78 13.89 13.90 14.63 | 15.0 14.1 13.4 14.0 14.5 |
| TA_HLE, `gles` GPU | JIT | 1178.3 1276.6 1308.3 1341.1 1318.9 | 733.4 798.6 842.8 873.7 849.7 | 8.18 7.51 7.12 6.87 7.06 | 15.2 14.9 14.6 14.2 13.4 |
| TA_HLE, `gles` GPU | interpreter | 577.4 395.7 523.7 549.3 333.1 | 386.0 302.5 344.0 367.4 258.5 | 15.55 19.83 17.44 16.33 23.21 | 13.1 12.1 15.1 14.5 13.5 |

    python3 tests/perf/perf.py --emulator <build>/system/emulator/devcast --program 2ndmix.elf
    python3 tests/perf/perf.py --emulator <build>/system/emulator-ta-hle/devcast --program 2ndmix.elf
    python3 tests/perf/perf.py --emulator <build>/system/emulator-ta-hle/devcast --program 2ndmix.elf --renderer gl
