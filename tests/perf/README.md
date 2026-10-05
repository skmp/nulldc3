# Speed measurements

`perf.py` runs a guest program for a fixed emulated time and reports how fast that went, in percent of real time
(100 = a Dreamcast). It is a tool, not a ctest test: nothing here passes or fails on a number. `BASELINE.md` has
the figures of the machine this was written on.

    # no rendering: headless, the none renderer. Any build of the emulator.
    python3 tests/perf/perf.py --emulator <devcast> --program <2ndmix.elf>

    # a hardware GL renderer: a window and gles, which only a -DHAS_TA_LLE=OFF build has
    python3 tests/perf/perf.py --emulator <devcast of a TA_HLE build> --program <2ndmix.elf> --renderer gl

    --engines jit,interp     which SH4 engines (both)
    --seconds 60             emulated seconds per run
    --runs 5                 runs per engine; the median is the result
    --json <file>            the result as JSON: per engine the median, the lowest and the highest, every run,
                             the load of the machine (1 minute average) in front of every run, the renderer,
                             the date, the CPU

A run is one process. It ends itself with `config:Exit.AfterSeconds` and prints its own speed: emulated time over
wall time, both from the first vblank on (`docs/nulldc3/automated-runs.md`), so process start-up and the loading of
the program are not in the figure; the start of the program itself is. Runs are made one after the other. The
clock is pinned and there is no audio backend, so every run does the same work.

Rules the script keeps:

* **The renderer is part of every figure.** Speed is measured with no renderer (`none`) or with a hardware
  renderer, never with the reference software renderers (refsw2, refmister): with those the run is bound by the
  renderer, at a fraction of real time.
* **A software GL is not a GPU.** For `--renderer gl` the script asks `glxinfo -B` for an accelerated context, as
  the environment is or, failing that, with `GALLIUM_DRIVER=d3d12` (WSL2, where the default context is llvmpipe).
  It then takes the GL renderer string the emulator itself prints (`OpenGL renderer: ...`) as the truth: a
  software rasteriser is refused, or with `--allow-software-gl` measured and labelled `gl-software`, never
  `gl-gpu`.
* **The load of the machine is recorded**, not controlled. Figures from a busy machine are lower and wider; compare
  the range, not only the median.
