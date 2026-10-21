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
  out, SIGKILL to the whole group, and to every process below the program in a group or a session of its own, and
  exit 124. When the program has ended, what is left of its group, and below it, is killed too. A SIGTERM, SIGINT or
  SIGHUP to macrun.py kills the same and ends macrun.py with 128 + that signal. (A macrun.py that the program runs,
  a test inside a ctest of a long step, starts its own program in a session of its own; it dies in the same sweep,
  before it can end that program, so the sweep reads the whole tree from the kernel's process table first, stops
  it, then kills it.)
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
import errno
import os
import signal
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SANDBOX_EXEC = '/usr/bin/sandbox-exec'
LIBSYSTEM = '/usr/lib/libSystem.B.dylib'
PS = '/bin/ps'
NOT_RUN = 126

# struct kinfo_proc of <sys/sysctl.h>, the same on arm64 and x86_64: its size, and the offsets of kp_proc.p_stat,
# kp_proc.p_pid, kp_eproc.e_ppid and kp_eproc.e_pgid; p_stat SZOMB is a zombie
KINFO_PROC_SIZE = 648
KP_STAT = 36
KP_PID = 40
KP_PPID = 560
KP_PGID = 564
SZOMB = 5


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


def signal_pid(pid, sn):
    try:
        os.kill(pid, sn)
    except (ProcessLookupError, PermissionError):
        pass


def process_table():
    """{pid: (ppid, pgid)} of every process that has not ended (zombies left out): from the kernel, else from ps;
    None when neither can tell."""
    table = table_from_kernel()
    if table is None:
        table = table_from_ps()
    return table


def table_from_kernel():
    """The table from sysctl kern.proc.all through libSystem, what ps reads. ps itself is setuid root on macOS and
    so cannot run under a sandbox profile ("Operation not permitted"), where macrun.py runs inside a long step. The
    layout of struct kinfo_proc is checked on this process's own entry; None when that or the call fails. sysctl
    has no variadic arguments."""
    if sys.platform != 'darwin':
        return None
    try:
        sysctl = ctypes.CDLL(LIBSYSTEM, use_errno=True).sysctl
    except (OSError, AttributeError):
        return None
    sysctl.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.c_uint, ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t),
                       ctypes.c_void_p, ctypes.c_size_t]
    sysctl.restype = ctypes.c_int
    mib = (ctypes.c_int * 4)(1, 14, 0, 0)	# CTL_KERN, KERN_PROC, KERN_PROC_ALL
    data = None
    for _ in range(5):
        size = ctypes.c_size_t(0)
        if sysctl(mib, 4, None, ctypes.byref(size), None, 0) != 0:
            return None
        size.value += size.value // 4 + 32 * KINFO_PROC_SIZE	# room for the processes started since
        buf = ctypes.create_string_buffer(size.value)
        if sysctl(mib, 4, buf, ctypes.byref(size), None, 0) == 0:
            data = buf.raw[:size.value]
            break
        if ctypes.get_errno() != errno.ENOMEM:
            return None
    if data is None or len(data) % KINFO_PROC_SIZE:
        return None
    table = {}
    for at in range(0, len(data), KINFO_PROC_SIZE):
        if data[at + KP_STAT] == SZOMB:
            continue
        pid = struct.unpack_from('=i', data, at + KP_PID)[0]
        table[pid] = struct.unpack_from('=ii', data, at + KP_PPID)
    if table.get(os.getpid()) != (os.getppid(), os.getpgrp()):
        return None
    return table


def table_from_ps():
    """The table from ps, where the kernel's cannot be read."""
    try:
        r = subprocess.run([PS, '-A', '-o', 'pid=,ppid=,pgid=,stat='], stdin=subprocess.DEVNULL,
                           stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    except OSError:
        return None
    if r.returncode != 0:
        return None
    table = {}
    for line in r.stdout.decode('ascii', 'replace').splitlines():
        f = line.split()
        if len(f) < 4 or not (f[0].isdigit() and f[1].isdigit() and f[2].isdigit()):
            continue
        if f[3].startswith('Z'):
            continue
        table[int(f[0])] = (int(f[1]), int(f[2]))
    return table


def tree_of(table, leader, waited):
    """What macrun.py started that is still there: the program (until it has been waited for: then its pid may be
    another process's), what is left of its process group, and every process below those, whatever its group or
    session; and the process groups of all of them."""
    children = {}
    for pid, (ppid, _) in table.items():
        children.setdefault(ppid, []).append(pid)
    todo = [pid for pid, (_, pgid) in table.items() if pgid == leader]
    if not waited and leader in table:
        todo.append(leader)
    me = os.getpid()
    found = set()
    while todo:
        pid = todo.pop()
        if pid in found or pid == me or pid <= 1:
            continue
        found.add(pid)
        todo.extend(children.get(pid, []))
    return found, {table[pid][1] for pid in found}


def end_tree(p):
    """SIGKILL to the program's process group and to every process below the program, also one in a group or a
    session of its own. The tree is read from ps and stopped (SIGSTOP), again until it has grown no more, so that
    nothing of it forks or ends between the look and the kill; then each of its groups and each of its processes
    gets SIGKILL; all of it again until nothing is left. A macrun.py inside the program dies in that sweep, and the
    program it started, in a session of its own, with it. Without a process table: the program's group alone."""
    leader = p.pid
    own_group = os.getpgrp()
    for _ in range(20):
        stopped = set()
        groups = set()
        for _ in range(50):
            table = process_table()
            if table is None:
                kill_group(leader)
                return
            found, found_groups = tree_of(table, leader, p.returncode is not None)
            groups |= found_groups
            new = found - stopped
            if not new:
                break
            for pid in new:
                signal_pid(pid, signal.SIGSTOP)
            stopped |= new
        if not stopped:
            return
        for pgid in groups:
            if pgid > 1 and pgid != own_group:
                kill_group(pgid)
        for pid in stopped:
            signal_pid(pid, signal.SIGKILL)
        time.sleep(0.05)


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
        end_tree(p)
        say('%s: %s to macrun.py: the process group %d and what is below it are killed' % (name, signal_name(sn),
                                                                                         p.pid))
        sys.exit(128 + sn)

    for sn in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sn, on_signal)

    try:
        status = p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        end_tree(p)
        p.wait()
        say('%s: timeout after %g s: the process group %d and what is below it are killed' % (name, timeout, p.pid))
        return 124
    end_tree(p)
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
