#!/usr/bin/env python3
"""smoke.py: the smoke run of a release file (PL-D76), called by appimage.sh, windows.ps1 and macos.sh with the
program they made, on the system that made it.

    smoke.py --exe <program> --version <version> --ui-version <version> --guest <elf> --work <dir>
             [--portable <dir> | --beside <dir>] [--env NAME=VALUE]... [-- <launcher word>...]

Three runs, each with P0's keys (config:Headless=yes, audio:backend=none, config:Dreamcast.RTC=946684800; PL-D71),
each through the launcher words after "--" in front of the program (nulldc3-winrun --timeout on Windows, macrun.py
--timeout on macOS; none on Linux, where smoke.py itself starts every run through timeout -k 5 <the run's limit>, so
that no run outlives its limit even when smoke.py is killed), each in a session of its own (not on Windows), with
DISPLAY and WAYLAND_DISPLAY out of its environment and a core file size limit of 1 byte (not on Windows):
  1. --version: exit status 0, stdout exactly "nullDC <version>" and a line end;
  2. the guest headless for 90 vblanks: exit status 0 and its exit line "nulldc-exit: reason=vblanks ... vblanks=90";
     with --beside <dir> the run has no --portable, and its files (emu.cfg, vmu_save_A1.bin, vmu_save_A2.bin) have
     to be in <dir> and nowhere below --work; with --portable <dir> they have to be in <dir>;
  3. the guest headless with the debugger enabled (loopback, a port of the system's choice): one debugger.describe
     of the tree's Python client (tests/debugger/contract/wsclient.py) has emulator.version <version> and ui
     <ui-version>; then the run is ended (the launcher's process, which ends what it started).
Every run is ended whatever happens to smoke.py: a run that has not ended by itself gets SIGTERM to its session (on
Windows its launcher is ended), then SIGKILL after a grace; on POSIX its session gets SIGKILL once its first process
has ended, so that nothing of it is left. A failure of the debugger's client of any kind is the check's failure, not
smoke.py's end; SIGTERM, SIGINT or SIGHUP to smoke.py ends the run in progress first and then smoke.py, with
128 + the signal's number.
--env NAME=VALUE adds to the environment of every run (APPIMAGE_EXTRACT_AND_RUN=1 for an AppImage). Exit status 0
when every check held; every check prints one line. Python 3.9, the standard library only.
"""
import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tests', 'debugger', 'contract'))
import wsclient  # noqa: E402

P0_KEYS = ['config:Headless=yes', 'audio:backend=none', 'config:Dreamcast.RTC=946684800']
RUN_FILES = ('emu.cfg', 'vmu_save_A1.bin', 'vmu_save_A2.bin')
WINDOWS = os.name == 'nt'
LINUX = sys.platform.startswith('linux')
if not WINDOWS:
    import resource

failed = []
live = []               # the runs started and not ended yet
grace = {'s': 30}       # how long a run has to end after SIGTERM before SIGKILL; 5 s once smoke.py is being ended


class Ended(BaseException):
    """A signal to smoke.py: raised where smoke.py is, so that every run in progress is ended on the way out."""

    def __init__(self, number):
        BaseException.__init__(self, number)
        self.number = number


def on_signal(number, frame):
    for name in ('SIGTERM', 'SIGINT', 'SIGHUP', 'SIGBREAK'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), signal.SIG_IGN)   # the ending is not cut short by another one
    grace['s'] = 5
    raise Ended(number)


def check(condition, text):
    print('%s %s' % ('PASS' if condition else 'FAIL', text), flush=True)
    if not condition:
        failed.append(text)
    return condition


def config(keys):
    words = ['--config']
    for i, key in enumerate(keys):
        if i:
            words.append(',')
        words.append(key)
    return words


def core_limit():
    # 1 byte, the hard limit kept: the one limit with which the kernel does not pipe a crash to a core handler
    # (apport on Ubuntu, WSL2's handler)
    resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))


def environment(options):
    env = dict(os.environ)
    if not WINDOWS:
        for name in ('DISPLAY', 'WAYLAND_DISPLAY'):
            env.pop(name, None)
    for item in options.env:
        name, _, value = item.partition('=')
        env[name] = value
    return env


def popen(options, args, out, limit):
    command = options.launcher + [options.exe] + args
    if LINUX:
        # no launcher on Linux: timeout(1) bounds the run (its group gets SIGTERM after the limit, SIGKILL 5 s on)
        command = ['timeout', '-k', '5', str(limit)] + command
    kwargs = dict(cwd=options.work, env=environment(options), stdin=subprocess.DEVNULL, stdout=out,
                  stderr=subprocess.STDOUT if out is not subprocess.PIPE else subprocess.PIPE)
    if WINDOWS:
        kwargs['creationflags'] = subprocess.CREATE_NO_WINDOW
    else:
        kwargs['start_new_session'] = True
        kwargs['preexec_fn'] = core_limit
    process = subprocess.Popen(command, **kwargs)
    live.append(process)
    return process


def killpg(pid, number):
    try:
        os.killpg(pid, number)
    except (ProcessLookupError, PermissionError):
        pass


def end(process):
    """Ends a run that was started with popen, and returns its exit status: when it is still running, on Windows
    its launcher is ended (nulldc3-winrun closes its job, which ends the program), elsewhere its session gets
    SIGTERM (macrun.py ends what it started on SIGTERM), then SIGKILL after the grace. On POSIX the session gets
    SIGKILL once its first process has been waited for, whether it ended by itself or not: what it started in the
    session (the AppImage runtime's program, a child of the launcher's) does not outlive it."""
    try:
        if process.poll() is None:
            if WINDOWS:
                process.terminate()
            else:
                killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(grace['s'])
            except subprocess.TimeoutExpired:
                if WINDOWS:
                    process.kill()
                else:
                    killpg(process.pid, signal.SIGKILL)
                process.wait()
    finally:
        if not WINDOWS:
            killpg(process.pid, signal.SIGKILL)
        if process in live:
            live.remove(process)
    return process.returncode


def run(options, args, timeout):
    process = popen(options, args, subprocess.PIPE, timeout)
    try:
        try:
            out, err = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            end(process)
            out, err = process.communicate()
            return None, out.decode('utf-8', 'replace'), err.decode('utf-8', 'replace')
        return process.returncode, out.decode('utf-8', 'replace'), err.decode('utf-8', 'replace')
    finally:
        end(process)


def placement(options):
    if options.portable:
        os.makedirs(options.portable, exist_ok=True)
        return ['--portable', options.portable]
    return []


def files_below(top):
    found = []
    for root, dirs, files in os.walk(top):
        for name in files:
            if name in RUN_FILES:
                found.append(os.path.relpath(os.path.join(root, name), top))
    return sorted(found)


def main():
    for name in ('SIGTERM', 'SIGINT', 'SIGHUP', 'SIGBREAK'):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), on_signal)
    try:
        return checks()
    except Ended as ended:
        for process in list(live):
            end(process)
        print('smoke: ended by signal %d; every run it had started is ended' % ended.number, flush=True)
        return 128 + ended.number


def checks():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--ui-version', required=True)
    parser.add_argument('--guest', required=True)
    parser.add_argument('--work', required=True)
    where = parser.add_mutually_exclusive_group()
    where.add_argument('--portable')
    where.add_argument('--beside')
    parser.add_argument('--env', action='append', default=[])
    # the launcher's words come after "--" (they have "--" of their own, which argparse would take as its own)
    argv = sys.argv[1:]
    launcher = []
    if '--' in argv:
        launcher = argv[argv.index('--') + 1:]
        argv = argv[:argv.index('--')]
    options = parser.parse_args(argv)
    options.launcher = launcher
    options.work = os.path.abspath(options.work)
    if os.path.isdir(options.work):
        shutil.rmtree(options.work)
    os.makedirs(os.path.join(options.work, 'tmp'))
    guest = os.path.abspath(options.guest)

    # 1. --version
    rc, out, err = run(options, placement(options) + config(P0_KEYS) + ['--version'], 120)
    out = out.replace('\r\n', '\n')
    check(rc == 0 and out == 'nullDC %s\n' % options.version,
          '--version: exit status %s, stdout %r (expected 0 and %r)' % (rc, out, 'nullDC %s\n' % options.version))

    # 2. the guest, 90 vblanks
    if options.beside:
        for name in RUN_FILES:
            path = os.path.join(options.beside, name)
            if os.path.exists(path):
                os.remove(path)
    keys = P0_KEYS + ['config:Exit.AfterVblanks=90', 'config:Exit.StateHash=yes']
    rc, out, err = run(options, placement(options) + config(keys) + [guest], 300)
    lines = [line for line in (out + err).replace('\r\n', '\n').split('\n') if line.startswith('nulldc-exit:')]
    check(rc == 0 and len(lines) == 1 and 'reason=vblanks' in lines[0] and 'vblanks=90' in lines[0],
          'the guest, 90 vblanks: exit status %s, exit line %r' % (rc, lines[-1] if lines else None))
    if options.beside:
        there = sorted(name for name in RUN_FILES if os.path.isfile(os.path.join(options.beside, name)))
        stray = files_below(options.work)
        check(there == sorted(RUN_FILES) and not stray,
              'the files of a run without --portable: %s beside the program (expected %s), %s below the run\'s '
              'working directory (expected none)' % (there, sorted(RUN_FILES), stray))
    elif options.portable:
        there = sorted(name for name in RUN_FILES if os.path.isfile(os.path.join(options.portable, name)))
        check(there == sorted(RUN_FILES), 'the files of the run in its --portable directory: %s' % there)

    # 3. the debugger: debugger.describe names the version and the UI
    keys = P0_KEYS + ['config:Exit.AfterVblanks=360000', 'Debugger:Enabled=yes', 'Debugger:Port=0']
    log_path = os.path.join(options.work, 'debugger.log')
    with open(log_path, 'wb') as log:
        process = popen(options, placement(options) + config(keys) + [guest], log, 180)
    port = None
    shape = {}
    error = None
    try:
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline and port is None and process.poll() is None:
            time.sleep(0.2)
            with open(log_path, 'rb') as f:
                match = re.search(r'Debugger: http://127\.0\.0\.1:(\d+)/', f.read().decode('utf-8', 'replace'))
            if match:
                port = int(match.group(1))
        if port is not None:
            client = None
            try:
                client = wsclient.Client('127.0.0.1', port)
                client.describe()
                shape = client.shape
            except Exception as e:
                # whatever the answer was (a connection reset, no JSON, a reply of another shape): the check's
                error = '%s: %s' % (type(e).__name__, e)
            finally:
                if client is not None:
                    client.close()
    finally:
        rc = end(process)
    if not isinstance(shape, dict):
        error = error or 'debugger.describe answered %r, not an object' % (shape,)
        shape = {}
    emulator = shape.get('emulator') if isinstance(shape.get('emulator'), dict) else {}
    check(port is not None and error is None and emulator.get('version') == options.version
          and shape.get('ui') == options.ui_version,
          'debugger.describe on 127.0.0.1: emulator.version %r, ui %r (expected %r and %r)%s%s' % (
              emulator.get('version'), shape.get('ui'), options.version, options.ui_version,
              '' if port is not None else '; no line "Debugger: http://127.0.0.1:<port>/"',
              '' if error is None else '; ' + error))
    print('the run with the debugger ended with %s' % rc)
    if failed:
        with open(log_path, 'rb') as f:
            print('its output:\n' + f.read().decode('utf-8', 'replace')[-3000:])
    print('smoke: %d check%s failed' % (len(failed), '' if len(failed) == 1 else 's'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
