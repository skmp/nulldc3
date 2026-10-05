# Baseline: 2ndmix, this machine

Measured 2026-10-05 with `tests/perf/perf.py`, tree at `cbf82d1c` (branch `skmp/nulldc3-t20`), emulators built
RelWithDebInfo with g++ 13.3 by the tests project.

| | |
|---|---|
| program | `2ndmix.elf` (KallistiOS 2.2.2 example), booted under reios |
| length | 60 emulated seconds per run (3596 vblanks), 5 runs per row, one after the other |
| settings | `audio:backend=none`, `config:Dreamcast.RTC=946684800`, everything else default: the "fast" timing of today, `Dynarec.idleskip` on, ARM7 and DSP interpreters |
| machine | AMD Ryzen 9 3900X (12 cores, 24 threads), Linux 6.18.33.2-microsoft-standard-WSL2 |
| load | **the machine was busy with other work throughout**: 1 minute load average 21 to 29 on 24 threads in front of the runs. On an idle machine the figures are higher |

Speed in percent of real time (100 = a Dreamcast), from the first vblank of the run on:

| build | renderer | engine | median | lowest | highest | load in front of the runs |
|---|---|---|---|---|---|---|
| default (TA_LLE) | `none`, headless | JIT | **1047.5** | 984.3 | 1080.5 | 21.8 - 26.2 |
| default (TA_LLE) | `none`, headless | interpreter | **352.1** | 288.4 | 405.3 | 20.5 - 23.8 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `none`, headless | JIT | **859.6** | 689.9 | 907.8 | 24.5 - 25.2 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `none`, headless | interpreter | **332.5** | 306.6 | 389.1 | 25.4 - 26.7 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `gles` on the GPU, in a window | JIT | **811.7** | 707.4 | 857.7 | 27.3 - 28.6 |
| `-DHAS_TA_LLE=OFF` (TA_HLE) | `gles` on the GPU, in a window | interpreter | **363.9** | 309.3 | 366.5 | 28.8 - 29.0 |

GL for the last two rows: `glxinfo -B` reports `Accelerated: yes` with `GALLIUM_DRIVER=d3d12` (without it the
context is llvmpipe, `Accelerated: no`), and the emulator printed `OpenGL renderer: D3D12 (NVIDIA GeForce RTX 2060)`.
No figure here is from a software rasteriser.

How to read them:

* The spread between the runs of a row is up to a third of the median, and the rows were measured under
  different load. The differences between the three JIT rows and between the three interpreter rows are inside
  that spread: these figures do not rank the builds or say what the GL renderer costs. They are a floor to compare
  later work on the SH4 against, with the same script, preferably on an idle machine.
* With `gles` the emulator thread does not wait for the renderer: a frame that arrives while the previous one is
  still being drawn is dropped. The figure is the speed of the emulation with the hardware renderer running, not
  a frame rate.

Every run, in the order they were made, and the 1 minute load average in front of it:

| build, renderer | engine | speed of runs 1 to 5 | load |
|---|---|---|---|
| default, `none` | JIT | 984.3 996.1 1047.5 1049.9 1080.5 | 26.2 24.4 23.9 23.0 21.8 |
| default, `none` | interpreter | 405.3 295.2 365.6 352.1 288.4 | 21.0 20.5 22.1 23.2 23.8 |
| TA_HLE, `none` | JIT | 907.8 859.6 689.9 725.7 893.7 | 24.5 24.7 24.8 25.0 25.2 |
| TA_HLE, `none` | interpreter | 389.1 384.0 311.7 332.5 306.6 | 25.4 25.8 26.1 26.2 26.7 |
| TA_HLE, `gles` GPU | JIT | 857.7 857.6 798.8 707.4 811.7 | 27.3 27.6 28.2 28.4 28.6 |
| TA_HLE, `gles` GPU | interpreter | 363.9 366.5 365.5 315.2 309.3 | 28.9 29.0 28.8 28.9 28.8 |

    python3 tests/perf/perf.py --emulator <build>/system/emulator/devcast --program 2ndmix.elf
    python3 tests/perf/perf.py --emulator <build>/system/emulator-ta-hle/devcast --program 2ndmix.elf
    python3 tests/perf/perf.py --emulator <build>/system/emulator-ta-hle/devcast --program 2ndmix.elf --renderer gl
