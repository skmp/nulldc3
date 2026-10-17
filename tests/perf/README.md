# Speed measurements

`perf.py` runs a guest program for a fixed emulated time and reports how fast that went, in percent of real time
(100 = a Dreamcast). It is a tool, not a ctest test: nothing here passes or fails on a number (`system.perf.script`
runs it on a program of the system tests and checks that its figures are there, not what they are). `BASELINE.md`
has the figures of the machine this was written on, measured while it was quiet.

    # no rendering: headless, the none renderer. Any build of the emulator.
    python3 tests/perf/perf.py --emulator <nulldc3> --program <2ndmix.elf>

    # a hardware GL renderer: a window and gles, which only a -DHAS_TA_LLE=OFF build has
    python3 tests/perf/perf.py --emulator <nulldc3 of a TA_HLE build> --program <2ndmix.elf> --renderer gl

    --engines jit,interp     which SH4 engines (both)
    --seconds 60             emulated seconds per run
    --runs 5                 runs per engine; the median is the result
    --config <keys>          section:key=value for every run, behind the script's own keys (more than once, or
                             a list separated by commas), e.g. config:aica.NoBatch=yes for a base of before the
                             M3 round, which had its per-sample path behind that key
    --json <file>            the result as JSON: per engine the median, the lowest and the highest of both
                             figures, every run (both figures, its CPU seconds and emulated seconds), the load
                             of the machine (1 minute average) in front of every run, the renderer, the date,
                             the CPU

A run is one process. It ends itself with `config:Exit.AfterSeconds`, and two figures come out of it:

* the **speed**, which the emulator prints itself: emulated time over wall time, both from the first vblank on
  (`docs/nulldc3/automated-runs.md`), so process start-up and the loading of the program are not in the figure;
  the start of the program itself is. It is what a person in front of the machine gets, and what the machine
  does besides is in it: a run that waits for a core is slower by that wait.
* the **speed by CPU time**: the emulated time of the run over the CPU time its process used, user and system,
  of all its threads and from its start to its end (`getrusage` of the child). It is what the run costs. Time
  the process waits for a core is not in it; what the other work on the machine does to a core that is running
  still is, and `BASELINE.md` has how far that moved it on one machine. Headless, one thread does the work, and
  on a quiet machine the two figures agree within about 2 % (`BASELINE.md`): the CPU time has the start and the
  end of the process in it, which the speed leaves out. With a window the thread of the user interface uses CPU
  time as well, and the figure is well below the speed.

Runs are made one after the other. The clock is pinned and there is no audio backend, so every run does the same
work. A program that returns to the BIOS
before the time is up ends its run there (`reason=guest-exit`, with a window as well), and the script stops with
an error: a run that was not as long as asked gives no figure.

Rules the script keeps:

* **The renderer is part of every figure.** Speed is measured with no renderer (`none`) or with a hardware
  renderer, never with the reference software renderers (refsw2, refmister): with those the run is bound by the
  renderer, at a fraction of real time.
* **A software GL is not a GPU.** For `--renderer gl` the script asks `glxinfo -B` for an accelerated context, as
  the environment is or, failing that, with `GALLIUM_DRIVER=d3d12` (WSL2, where the default context is llvmpipe).
  It then takes the GL renderer string the emulator itself prints (`OpenGL renderer: ...`) as the truth: a
  software rasteriser is refused, or with `--allow-software-gl` measured and labelled `gl-software`, never
  `gl-gpu`.
* **The load of the machine is recorded**, not controlled. Figures from a busy machine are lower and wider, both
  of them (`BASELINE.md`, "What load does"), and the 1 minute average that is recorded trails what the machine
  is doing and does not say which figure to expect. A figure to keep is measured with that average below 10, as
  the baseline was. Two builds are compared by running them in turn and by the CPU seconds of their runs, not by
  two measurements made at different times.
