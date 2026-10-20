# nulldc3-winrun

The run envelope of the Windows host: every program a test, a harness or an agent starts on Windows (the emulator,
a C++ suite, ctest itself, a harness's Python or node) runs inside it, so that nothing the program does can reach the
desktop of the person who uses the computer. `winrun.cpp` has the whole description at its top.

    nulldc3-winrun [--timeout <s>] [--own-console] [--detach --log <file> --rc <file>] -- <program> [<arg>...]
    nulldc3-winrun --probe

- **The error mode** `SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX`, set first and
  inherited by every descendant.
- **A job** with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` (nothing of the program outlives nulldc3-winrun) and
  `JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION` (a process the job holds ends on an unhandled exception without the
  error box). Jobs nest: a program that runs nulldc3-winrun again is fine.
- **A desktop of its own**, `winrun-<pid>`, made in the caller's window station: a window or a message box the
  program makes is on that desktop, which nobody sees. The program starts suspended, goes into the job and only then
  runs; when the desktop or the job cannot be had it never runs, and nulldc3-winrun exits with 126.
- **Its console**: the program shares nulldc3-winrun's console and standard handles, or gets `CREATE_NO_WINDOW`
  when there is none; `--own-console` gives it a pseudo console of its own (`CreatePseudoConsole`), whose output is
  copied to nulldc3-winrun's standard output. Never a console window.
- **The windows of the desktop** are listed every 100 ms while the program runs and once more at its end (class,
  visible or not, the process's id and the name of its executable), and printed on stderr, with the job's peak of
  committed memory and what of the job was still running when the program ended.
- **The exit status**: 126 a set-up that failed; 125 a visible window, or a window of the emulator's class
  (`WINDOW_CLASS` of `libswirl/windows/win_common.h`), seen at all; 124 `--timeout` ran out (the job is ended);
  otherwise the program's.
- **Its handles**: the program inherits nulldc3-winrun's standard handles and no other handle of it
  (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`).
- **`--detach --log <file> --rc <file>`** starts a copy of itself with `DETACHED_PROCESS`, which runs the program as
  above with the log as its output and writes the exit status into the rc file at the end, prints its id and
  returns at once: a long step (a build, a ctest run) runs that way and is polled. The copy inherits the log and
  `NUL` and nothing else, so that a caller reading nulldc3-winrun's output through a pipe (WSL's interop, CMake's
  `execute_process`, Python's `subprocess`) has its end of file when nulldc3-winrun returns, not when the copy ends
  (the test `windows.winrun.detach`). The copy leaves the caller's job (`CREATE_BREAKAWAY_FROM_JOB`); where that
  job does not allow it, nulldc3-winrun says so on stderr, and the copy stays in that job and ends with it.
- **`--probe`** prints the name of the desktop its thread is on, exits with 3 without making a window when that is
  not `winrun-<n>`, and otherwise makes a visible window and waits: the first program nulldc3-winrun starts on a
  host is `nulldc3-winrun --timeout 10 -- nulldc3-winrun --probe`, which has to print the private desktop's name and
  end with 125 (the test `windows.winrun.probe` runs it).

**What the private desktop does not hide**: crash reporting, a just-in-time debugger, the system's error boxes and
the firewall's prompt are other processes and show on the interactive desktop whatever desktop the program is on.
The error mode and the job's flag keep them away, with the emulator's own policy (`docs/nulldc3/automated-runs.md`);
no test or run may turn them off.

**Building it.** The tests project builds it on Windows as `nulldc3-winrun.exe` (`tests/windows`). Before there is
one, `bootstrap.cmd <output directory>` builds it with `cl.exe` alone (from a developer prompt of Visual Studio, or
it takes the newest one with `vswhere`). Windows 10 1809 or later.
