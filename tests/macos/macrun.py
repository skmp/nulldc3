#!/usr/bin/env python3
"""macrun.py: the run envelope of the Mac. Every emulator run that a test or an agent makes on macOS, and every long
step of a gate there (a build, a ctest run), goes through it, so that nothing of it can reach the window server, the
Dock or LaunchServices: no window, no Dock icon, no dialog on the desktop of the person who uses the Mac.

    macrun.py [--name <n>] [--work <dir>] [--timeout <s>] [--detach --log <file> --rc <file>] -- <program> [<arg>...]

- The profile. The program runs under sandbox-exec -f no-windowserver.sb (beside this file; --profile names
  another), which allows everything but the Mach services of the window server, the window manager, the Dock and
  LaunchServices' check-in (the profile has the list and why). A profile binds the process and every descendant and
  cannot be loosened, and macOS refuses a second one inside it ("sandbox-exec: sandbox_apply: Operation not
  permitted"). So macrun.py first asks the kernel whether it runs under a profile already, with sandbox_check(pid,
  NULL, 0) of libSystem (1: sandboxed; the call has the function's three fixed arguments only, which is safe on
  arm64, where variadic ones would go on the stack). Not sandboxed: it applies the profile. Sandboxed: it runs the
  program directly, under the profile it inherited, and says "macrun: inside the profile already" on stderr. That
  is the shape of a gate: a build or ctest runs under macrun.py --detach, and the tests/macos targets inside it call
  macrun.py again.
- It refuses to run anything (exit 126) when the kernel cannot be asked, and when /usr/bin/sandbox-exec is missing
  and CI is not set in the environment (with CI set, a runner of the CI, the program runs without a profile and the
  line says so).
- Its own process group: the program starts in a new session (setsid). --timeout <s> (default 3600): when it runs
  out, SIGKILL to the whole group and exit 124. When the program has ended, what is left of its group is killed too.
  A SIGTERM, SIGINT or SIGHUP to macrun.py kills the group and ends macrun.py with 128 + that signal.
- TMPDIR is <work>/tmp (made), <work> being --work or the current directory; the program's id goes into
  <work>/run/<name>.pid. __CFBundleIdentifier is taken out of the program's environment: a program that macrun.py
  runs was never started by LaunchServices (the emulator's alert rule asks it, nulldc3/apple/AppDelegate.mm).
- The exit status: the program's; 128 + N when a signal N ended it; 124 for the timeout; 126 when it was not run.
  The last line on stderr says which: "macrun: <name>: exit status <n>", "macrun: <name>: killed by signal <n>
  (<SIG>)", "macrun: <name>: timeout after <s> s".
- --detach --log <file> --rc <file>: a long step that outlives the ssh connection which started it (a command of a
  non-pty ssh session that is killed leaves its processes running). macrun.py starts a copy of itself in a new
  session under caffeinate -i (no idle sleep while it runs; it changes no setting), with its output into the log,
  prints the copy's id and returns at once. The copy runs the program as above and writes its exit status into the
  rc file at the end (removed first, so that a stale one is never read).

Python 3.9, the standard library only. docs/nulldc3/automated-runs.md and tests/macos/README.md say more.
"""
import ctypes
import os
import signal
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SANDBOX_EXEC = '/usr/bin/sandbox-exec'
LIBSYSTEM = '/usr/lib/libSystem.B.dylib'
NOT_RUN = 126


def say(text):
    sys.stderr.write('macrun: ' + text + '\n')
    sys.stderr.flush()


def usage(text):
    say(text)
    say('usage: macrun.py [--name <n>] [--work <dir>] [--timeout <s>] [--profile <file>] '
        '[--detach --log <file> --rc <file>] -- <program> [<arg>...]')
    sys.exit(2)


def parse(argv):
    opts = {'name': None, 'work': None, 'timeout': 3600.0, 'profile': os.path.join(HERE, 'no-windowserver.sb'),
            'detach': False, 'log': None, 'rc': None, 'worker': False}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--':
            return opts, argv[i + 1:]
        if a in ('--detach', '--worker'):
            opts[a[2:]] = True
            i += 1
            continue
        if a in ('--name', '--work', '--timeout', '--profile', '--log', '--rc'):
            if i + 1 >= len(argv):
                usage(a + ' needs a value')
            opts[a[2:]] = argv[i + 1]
            i += 2
            continue
        usage('unknown switch ' + a)
    usage('no -- in front of the program')
    return None, None


def kernel_says_sandboxed():
    """True or False, from the kernel; None when it cannot be asked (with the reason in the second value)."""
    if sys.platform != 'darwin':
        return None, 'this is not macOS (%s)' % sys.platform
    try:
        lib = ctypes.CDLL(LIBSYSTEM)
        check = lib.sandbox_check
    except (OSError, AttributeError) as e:
        return None, 'no sandbox_check in %s (%s)' % (LIBSYSTEM, e)
    # int sandbox_check(pid_t pid, const char *operation, enum sandbox_filter_type type, ...): the fixed
    # arguments only
    check.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    check.restype = ctypes.c_int
    answer = check(os.getpid(), None, 0)
    if answer == 1:
        return True, ''
    if answer == 0:
        return False, ''
    return None, 'sandbox_check answered %d' % answer


def kill_group(pgid):
    try:
        os.killpg(pgid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass


def signal_name(n):
    try:
        return signal.Signals(n).name
    except ValueError:
        return 'signal %d' % n


def detach(opts, command):
    if not opts['log'] or not opts['rc']:
        usage('--detach needs --log <file> and --rc <file>')
    rc_file = os.path.abspath(opts['rc'])
    log_file = os.path.abspath(opts['log'])
    try:
        os.remove(rc_file)
    except FileNotFoundError:
        pass
    args = [sys.executable, '-I', '-B', os.path.abspath(__file__), '--worker', '--rc', rc_file,
            '--timeout', str(opts['timeout']), '--profile', opts['profile']]
    if opts['name']:
        args += ['--name', opts['name']]
    args += ['--work', os.path.abspath(opts['work'] or os.getcwd()), '--']
    args += command
    if os.path.exists('/usr/bin/caffeinate'):
        args = ['/usr/bin/caffeinate', '-i'] + args
    with open(os.devnull, 'rb') as null, open(log_file, 'wb') as log:
        p = subprocess.Popen(args, stdin=null, stdout=log, stderr=subprocess.STDOUT, start_new_session=True,
                             close_fds=True)
    say('%s: detached, pid %d, log %s, rc %s' % (opts['name'] or os.path.basename(command[0]), p.pid, log_file,
                                               rc_file))
    sys.stdout.write('%d\n' % p.pid)
    sys.stdout.flush()
    return 0


def run(opts, command):
    name = opts['name'] or os.path.basename(command[0])
    name = name.replace('/', '_')
    work = os.path.abspath(opts['work'] or os.getcwd())
    timeout = float(opts['timeout'])

    sandboxed, why = kernel_says_sandboxed()
    if sandboxed is None:
        say('%s: cannot ask the kernel whether this process is sandboxed: %s; not run' % (name, why))
        return NOT_RUN
    if sandboxed:
        say('inside the profile already')
        argv = list(command)
    elif os.path.exists(SANDBOX_EXEC):
        if not os.path.isfile(opts['profile']):
            say('%s: no profile %s; not run' % (name, opts['profile']))
            return NOT_RUN
        say('%s: under the profile %s' % (name, opts['profile']))
        argv = [SANDBOX_EXEC, '-f', opts['profile']] + list(command)
    elif os.environ.get('CI'):
        say('%s: no %s, and CI is set: the program runs without the profile' % (name, SANDBOX_EXEC))
        argv = list(command)
    else:
        say('%s: no %s, and CI is not set: not run without the profile' % (name, SANDBOX_EXEC))
        return NOT_RUN

    tmp = os.path.join(work, 'tmp')
    rundir = os.path.join(work, 'run')
    os.makedirs(tmp, exist_ok=True)
    os.makedirs(rundir, exist_ok=True)
    env = dict(os.environ)
    env['TMPDIR'] = tmp + '/'
    env.pop('__CFBundleIdentifier', None)

    try:
        p = subprocess.Popen(argv, env=env, start_new_session=True)
    except OSError as e:
        say('%s: cannot start %s: %s' % (name, argv[0], e))
        return NOT_RUN
    with open(os.path.join(rundir, name + '.pid'), 'w') as f:
        f.write('%d\n' % p.pid)

    def on_signal(sn, frame):
        kill_group(p.pid)
        say('%s: %s to macrun.py: the process group %d is killed' % (name, signal_name(sn), p.pid))
        sys.exit(128 + sn)

    for sn in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sn, on_signal)

    try:
        status = p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        kill_group(p.pid)
        p.wait()
        say('%s: timeout after %g s: the process group %d is killed' % (name, timeout, p.pid))
        return 124
    kill_group(p.pid)
    if status < 0:
        say('%s: killed by signal %d (%s)' % (name, -status, signal_name(-status)))
        return 128 - status
    say('%s: exit status %d' % (name, status))
    return status


def main(argv):
    opts, command = parse(argv)
    if not command:
        usage('no program to run')
    if opts['detach']:
        return detach(opts, command)
    rc = run(opts, command)
    if opts['worker'] and opts['rc']:
        part = opts['rc'] + '.part'
        with open(part, 'w') as f:
            f.write('%d\n' % rc)
        os.replace(part, opts['rc'])
    return rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
