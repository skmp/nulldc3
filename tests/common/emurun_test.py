#!/usr/bin/env python3
"""The run helper of the harnesses (emurun.py) on itself: harness.emurun.

    emurun_test.py --work <dir>

What it holds, on every host, for every system (each function of emurun takes the system as an argument):

    environment     on a POSIX host nothing of the caller's environment but the options of the sanitizers; on Windows
                    those, SystemRoot (Winsock does not start without it) and TEMP and TMP in <work>/tmp, made there,
                    and nothing of the profile (no PATH, no USERPROFILE, no APPDATA, no LOCALAPPDATA, no HOME); on
                    macOS those and TMPDIR in <work>/tmp, made there, and nothing else (no HOME, no PATH, not the
                    caller's TMPDIR)
    tool_environment
                    the environment of a program that is not the emulator: the caller's; on Windows with TEMP and TMP
                    in <work>/tmp, made there, and npm's cache in <work>/npm-cache, in place of the caller's in any
                    letter case; on macOS with TMPDIR in <work>/tmp, made there, and npm's cache in <work>/npm-cache,
                    in place of the caller's
    executable      nulldc3 is nulldc3.exe on Windows where nulldc3 names no file; on a POSIX host a path is a path
    read_text       a \\r in front of a \\n is not part of the line
    command         on Windows the program inside nulldc3-winrun (NULLDC_WINRUN), with a timeout of its own behind
                    the harness's; without NULLDC_WINRUN, or with one that names no file, no command; on macOS the
                    program behind tests/macos/macrun.py, run by this Python with -I -B, with the run's directory as
                    its work directory and a timeout of its own behind the harness's; without the run's directory no
                    command; on another POSIX host the program itself
    start on macOS  (emurun.system answering 'darwin', and the start of the process taken over by the test) the
                    command is macrun.py's, with the directory of the environment's TMPDIR (<work>/tmp) as its work
                    directory, and the environment is the one the harness gave; an environment whose TMPDIR is not
                    <a directory>/tmp, or that has none, starts nothing; kill() sends macrun.py a SIGTERM first,
                    on which it ends its program and what is below it, and terminate() a SIGTERM alone
    npm             on Windows npm.cmd, beside a path that names npm without its suffix; never the npm without a
                    suffix, a shell script
    no_shell        Windows has no POSIX shell; a POSIX host has one
    the harnesses   tests/debugger/selftest/selftest.py, tests/debugger/contract/contract.py,
                    tests/debugger/guest/dsptest/soundlines.py and tests/debugger/sh4bp/sessions.py start no program
                    but through emurun: no subprocess call of their own, and no module that knows the host's limits;
                    every start names the environment of the program (emurun.start(..., env=...)), none reads the
                    caller's environment but through emurun (no os.environ, no os.getenv), and none names the host
                    or the caller's environment to emurun.environment or emurun.tool_environment (the host they run
                    on decides)
    the harnesses' tools
                    the environments contract.py and soundlines.py build for the programs that are not the emulator,
                    built as on a Windows host (emurun.system answering 'windows'), with a caller whose TEMP, TMP and
                    npm cache are directories of its own: contract.py's runner_env (the contract runner, npm.cmd and
                    node) and the environment soundlines.py's tool_run starts a tool with (the oracle of a mode; the
                    start itself is taken over by the test) have TEMP and TMP in <work>/tmp and npm's cache in
                    <work>/npm-cache of their run, and no value of the caller's; and built as on macOS (emurun.system
                    answering 'darwin'), with a caller whose TMPDIR and npm cache are its own: TMPDIR in <work>/tmp and
                    npm's cache in <work>/npm-cache of their run, and no value of the caller's

and with a program it starts (this Python, on a script of the work directory), once as an emulator and once as a
program that is not one (emulator=False: the contract runner, npm.cmd and node, and the tools of the modes):

    on Windows      the program ran inside nulldc3-winrun: its output ends with nulldc3-winrun's line of its desktop,
                    winrun-<n>, and the program found itself on a desktop of that name that is not the one of this
                    test (which may be inside a nulldc3-winrun of its own, ctest's); as an emulator it was given the
                    environment of emurun and nothing else of the profile, as a tool the caller's with the TEMP, TMP
                    and npm cache of its run; kill() ended it and the program it had started, which nothing waited
                    for: the job ended both
    on macOS        the program ran through macrun.py: its output ends with macrun.py's line of its end, "exit
                    status 0", behind the line that says under which profile, and the kernel says the program is
                    sandboxed (sandbox_check); its TMPDIR is <the run's directory>/tmp, and macrun.py wrote its id
                    into <the run's directory>/run; as an emulator it was given the environment of emurun and nothing
                    else but what Python and the system put into a process themselves (LC_CTYPE, Python's locale
                    coercion in macrun.py and in the program, and __CF_USER_TEXT_ENCODING, which CoreFoundation sets
                    in every process that loads it), as a tool the caller's with the TMPDIR and npm cache of its run;
                    kill() ended it and the program it had started, in a session of its own, which nothing waited
                    for: macrun.py ended both
    on another POSIX host
                    the program ran; as an emulator with a core file size limit of 1 byte and the environment of
                    emurun, as a tool with the caller's environment

and the scratch files of a harness: selftest.py --mode log-rule, run with Python's own temporary directory made one
that is not there, writes what it writes under its --work and nothing into the caller's TEMP.

Python 3.9 and the standard library. Exit code 0 when all of it held.
"""

import argparse
import ast
import os
import re
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)

# the helper; nothing is written into the source tree for it
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)

import emurun  # noqa: E402

HARNESSES = (
    'debugger/selftest/selftest.py',
    'debugger/contract/contract.py',
    'debugger/guest/dsptest/soundlines.py',
    'debugger/sh4bp/sessions.py',
)

# what a harness may name of subprocess: its constants and its exception, not a way to start a program
SUBPROCESS_NAMES = ('DEVNULL', 'PIPE', 'STDOUT', 'TimeoutExpired')

# The program of the runs: what it finds about itself, one line each, and with an argument a program of its own that
# it starts and leaves running (its process id on a line)
CHILD = r'''
import os
import subprocess
import sys

print('environment: ' + ','.join(sorted(name.upper() for name in os.environ)))
print('temp: ' + os.environ.get('TEMP', ''))
print('npmcache: ' + os.environ.get('npm_config_cache', ''))
if sys.platform == 'win32':
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    user32.GetThreadDesktop.restype = wintypes.HANDLE
    user32.GetThreadDesktop.argtypes = [wintypes.DWORD]
    user32.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    name = ctypes.create_unicode_buffer(256)
    needed = wintypes.DWORD(0)
    user32.GetUserObjectInformationW(user32.GetThreadDesktop(kernel32.GetCurrentThreadId()), 2, name, ctypes.sizeof(name), ctypes.byref(needed))
    print('desktop: ' + name.value)
else:
    import resource
    print('core: %d' % resource.getrlimit(resource.RLIMIT_CORE)[0])
if sys.platform == 'darwin':
    import ctypes
    print('pid: %d' % os.getpid())
    check = ctypes.CDLL('/usr/lib/libSystem.B.dylib').sandbox_check
    check.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
    check.restype = ctypes.c_int
    print('sandboxed: %d' % check(os.getpid(), None, 0))
    print('tmpdir: ' + os.environ.get('TMPDIR', ''))
sys.stdout.flush()
if len(sys.argv) > 1:
    kid = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)'])
    print('kid: %d' % kid.pid)
    sys.stdout.flush()
    import time
    time.sleep(120)
'''


class Failed(Exception):
    pass


def check(ok, what):
    if not ok:
        raise Failed(what)


def desktop_of_this_thread():
    """The name of the desktop this test runs on (Windows)."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    user32.GetThreadDesktop.restype = wintypes.HANDLE
    user32.GetThreadDesktop.argtypes = [wintypes.DWORD]
    user32.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    name = ctypes.create_unicode_buffer(256)
    needed = wintypes.DWORD(0)
    user32.GetUserObjectInformationW(user32.GetThreadDesktop(kernel32.GetCurrentThreadId()), 2, name, ctypes.sizeof(name), ctypes.byref(needed))
    return name.value


def process_gone(pid, seconds):
    """Windows: the process of that id has ended within that time (or is not there)."""
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    SYNCHRONIZE = 0x00100000
    handle = kernel32.OpenProcess(SYNCHRONIZE, False, pid)
    if not handle:
        return True
    try:
        return kernel32.WaitForSingleObject(handle, int(seconds * 1000)) == 0
    finally:
        kernel32.CloseHandle(handle)


def test_environment(work):
    caller = {'HOME': '/home/u', 'PATH': '/usr/bin', 'DISPLAY': ':0', 'ASAN_OPTIONS': 'detect_leaks=0', 'TSAN_OPTIONS': 'x=1'}
    env = emurun.environment(work, 'posix', caller)
    check(env == {'ASAN_OPTIONS': 'detect_leaks=0', 'TSAN_OPTIONS': 'x=1'},
          'POSIX: the environment of a run is the options of the sanitizers and nothing else: %r' % (env,))

    # os.environ of Windows has its names in capitals
    caller = {'SYSTEMROOT': 'C:\\Windows', 'PATH': 'C:\\Windows\\system32', 'USERPROFILE': 'C:\\Users\\u', 'APPDATA': 'x',
              'LOCALAPPDATA': 'y', 'HOME': 'z', 'TEMP': 'C:\\Users\\u\\AppData\\Local\\Temp', 'UBSAN_OPTIONS': 'print_stacktrace=1'}
    run = os.path.join(work, 'run-windows')
    env = emurun.environment(run, 'windows', caller)
    temp = os.path.join(run, 'tmp')
    check(env == {'SystemRoot': 'C:\\Windows', 'TEMP': temp, 'TMP': temp, 'UBSAN_OPTIONS': 'print_stacktrace=1'},
          'Windows: the environment of a run is SystemRoot, TEMP and TMP of its directory and the options of the sanitizers: %r' % (env,))
    check(os.path.isdir(temp), 'Windows: the TEMP of a run is not made: ' + temp)
    env = emurun.environment(run, 'windows', {'SystemRoot': 'D:\\Win'})
    check(env.get('SystemRoot') == 'D:\\Win', 'Windows: SystemRoot is not taken as the caller has it: %r' % (env,))

    # a program that is not the emulator: the caller's environment; on Windows TEMP, TMP and npm's cache of the run in
    # place of the caller's, whatever the letter case of their names
    caller = {'HOME': '/home/u', 'PATH': '/usr/bin', 'TMPDIR': '/tmp/u', 'npm_config_cache': '/home/u/.npm'}
    env = emurun.tool_environment(os.path.join(work, 'tool-posix'), 'posix', caller)
    check(env == caller and env is not caller, 'POSIX: the environment of a tool is not the caller\'s: %r' % (env,))
    caller = {'SYSTEMROOT': 'C:\\Windows', 'PATH': 'C:\\Windows\\system32', 'USERPROFILE': 'C:\\Users\\u',
              'TEMP': 'C:\\Users\\u\\AppData\\Local\\Temp', 'tmp': 'C:\\Users\\u\\AppData\\Local\\Temp',
              'NPM_CONFIG_CACHE': 'C:\\Users\\u\\AppData\\Local\\npm-cache', 'npm_config_fund': 'false'}
    before = dict(caller)
    run = os.path.join(work, 'tool-windows')
    env = emurun.tool_environment(run, 'windows', caller)
    temp = os.path.join(run, 'tmp')
    check(env == {'SYSTEMROOT': 'C:\\Windows', 'PATH': 'C:\\Windows\\system32', 'USERPROFILE': 'C:\\Users\\u', 'TEMP': temp,
                  'TMP': temp, 'npm_config_cache': os.path.join(run, 'npm-cache'), 'npm_config_fund': 'false'} and caller == before,
          'Windows: the environment of a tool is not the caller\'s with TEMP, TMP and npm\'s cache of its run: %r' % (env,))
    check(os.path.isdir(temp), 'Windows: the TEMP of a tool is not made: ' + temp)

    # macOS: the sanitizers' options and TMPDIR of the run, nothing of the caller's (whose TMPDIR is the user's
    # temporary directory); a tool the caller's with TMPDIR and npm's cache of its run
    caller = {'HOME': '/Users/u', 'PATH': '/usr/bin', 'TMPDIR': '/var/folders/xy/T/', 'UBSAN_OPTIONS': 'print_stacktrace=1',
              '__CF_USER_TEXT_ENCODING': '0x1F5:0x0:0x0'}
    run = os.path.join(work, 'run-darwin')
    env = emurun.environment(run, 'darwin', caller)
    temp = os.path.join(run, 'tmp')
    check(env == {'TMPDIR': temp, 'UBSAN_OPTIONS': 'print_stacktrace=1'},
          'macOS: the environment of a run is TMPDIR of its directory and the options of the sanitizers: %r' % (env,))
    check(os.path.isdir(temp), 'macOS: the TMPDIR of a run is not made: ' + temp)
    caller = {'HOME': '/Users/u', 'PATH': '/usr/bin', 'TMPDIR': '/var/folders/xy/T/', 'NPM_CONFIG_CACHE': '/Users/u/.npm',
              'npm_config_fund': 'false'}
    before = dict(caller)
    run = os.path.join(work, 'tool-darwin')
    env = emurun.tool_environment(run, 'darwin', caller)
    temp = os.path.join(run, 'tmp')
    check(env == {'HOME': '/Users/u', 'PATH': '/usr/bin', 'TMPDIR': temp, 'npm_config_cache': os.path.join(run, 'npm-cache'),
                  'npm_config_fund': 'false'} and caller == before,
          'macOS: the environment of a tool is not the caller\'s with TMPDIR and npm\'s cache of its run: %r' % (env,))
    check(os.path.isdir(temp), 'macOS: the TMPDIR of a tool is not made: ' + temp)
    return ('the environment: POSIX the sanitizers\' options alone; Windows SystemRoot, TEMP and TMP of the run and nothing of '
            'the profile; macOS TMPDIR of the run and nothing of the caller\'s; a tool the caller\'s, on Windows with TEMP, '
            'TMP and npm\'s cache of its run, on macOS with TMPDIR and npm\'s cache of its run')


def test_executable(work):
    d = os.path.join(work, 'exe')
    os.makedirs(d)
    with open(os.path.join(d, 'nulldc3.exe'), 'wb'):
        pass
    with open(os.path.join(d, 'both'), 'wb'):
        pass
    with open(os.path.join(d, 'both.exe'), 'wb'):
        pass
    p = os.path.join(d, 'nulldc3')
    check(emurun.executable(p, 'windows') == p + '.exe', 'Windows: nulldc3 is not found as nulldc3.exe: ' + emurun.executable(p, 'windows'))
    check(emurun.executable(p, 'posix') == p, 'POSIX: a path is not taken as it is')
    check(emurun.executable(p + '.exe', 'windows') == p + '.exe', 'Windows: nulldc3.exe is not taken as it is')
    b = os.path.join(d, 'both')
    check(emurun.executable(b, 'windows') == b, 'Windows: a path that names a file is not taken as it is')
    m = os.path.join(d, 'missing')
    check(emurun.executable(m, 'windows') == m, 'Windows: a path that names nothing is changed')
    return 'the program: nulldc3 is nulldc3.exe on Windows, a path is a path on a POSIX host'


def test_read_text(work):
    path = os.path.join(work, 'lines.txt')
    with open(path, 'wb') as f:
        f.write(b'SelfTest: engine: x sh4-debug-native\r\nnulldc-exit: reason=vblanks arm7=6\r\nlast\n')
    text = emurun.read_text(path)
    check('\r' not in text and text.splitlines() == ['SelfTest: engine: x sh4-debug-native', 'nulldc-exit: reason=vblanks arm7=6', 'last'],
          'a \\r in front of a \\n is part of a line: %r' % (text,))
    m = re.search(r'^SelfTest: engine: (.*)$', text, re.M)
    check(m.group(1).endswith(' sh4-debug-native'), 'a field read with (.*)$ ends in a \\r: %r' % (m.group(1),))
    return 'the output: \\r\\n is read as \\n'


def test_command(work):
    runner = os.path.join(work, 'nulldc3-winrun.exe')
    with open(runner, 'wb'):
        pass
    argv = ['C:\\b\\nulldc3.exe', '--portable', 'p', 'x.elf']
    got = emurun.command(argv, 300, 'windows', {'NULLDC_WINRUN': runner})
    check(got == [runner, '--timeout', str(300 + emurun.WINRUN_MARGIN), '--'] + argv,
          'Windows: the program is not started inside nulldc3-winrun with a timeout behind the harness\'s: %r' % (got,))
    for environ, what in (({}, 'without NULLDC_WINRUN'), ({'NULLDC_WINRUN': os.path.join(work, 'none.exe')}, 'with a NULLDC_WINRUN that names no file')):
        try:
            got = emurun.command(argv, 300, 'windows', environ)
        except emurun.Error:
            continue
        raise Failed('Windows: a program is started %s: %r' % (what, got))
    check(emurun.command(argv, 300, 'posix', {'NULLDC_WINRUN': runner}) == argv, 'POSIX: the program is not started by itself')

    # macOS: behind macrun.py of tests/macos, with the run's directory as its work directory
    argv = ['/b/nulldc3.app/Contents/MacOS/nulldc3', '--portable', 'p', 'x.elf']
    macrun = os.path.join(TESTS, 'macos', 'macrun.py')
    check(os.path.isfile(macrun) and os.path.samefile(emurun.MACRUN, macrun), 'macOS: emurun.MACRUN is not tests/macos/macrun.py: ' + emurun.MACRUN)
    run = os.path.join(work, 'run-macrun')
    got = emurun.command(argv, 300, 'darwin', {}, run)
    check(got == [sys.executable, '-I', '-B', emurun.MACRUN, '--work', run, '--timeout', str(300 + emurun.MACRUN_MARGIN), '--'] + argv,
          'macOS: the program is not started behind macrun.py with the run\'s directory and a timeout behind the harness\'s: %r' % (got,))
    try:
        got = emurun.command(argv, 300, 'darwin', {})
    except emurun.Error:
        pass
    else:
        raise Failed('macOS: a program is started without the run\'s directory: %r' % (got,))
    return ('the command: on Windows inside nulldc3-winrun, and none without it; on macOS behind macrun.py, and none without '
            'the run\'s directory; on another POSIX host the program')


class Recorded(object):
    """What emurun.start() handed subprocess.Popen, and what was done to the process (test_start_darwin)."""

    def __init__(self, cmdline, **kwargs):
        self.cmdline = list(cmdline)
        self.kwargs = kwargs
        self.done = []
        self.ends = 1          # wait() raises TimeoutExpired so many times more
        self.returncode = None

    def poll(self):
        return self.returncode

    def terminate(self):
        self.done.append('SIGTERM')

    def kill(self):
        self.done.append('SIGKILL')
        self.returncode = -9

    def wait(self, timeout=None):
        self.done.append('wait %s' % timeout)
        if self.returncode is None and self.ends > 0 and timeout is not None:
            self.ends -= 1
            raise subprocess.TimeoutExpired(self.cmdline, timeout)
        if self.returncode is None:
            self.returncode = 143
        return self.returncode


def test_start_darwin(work):
    """start(), kill() and terminate() as on macOS, on every host: the start of the process is the test's."""
    started = []

    def popen(cmdline, **kwargs):
        started.append(Recorded(cmdline, **kwargs))
        return started[-1]

    saved = emurun.system, emurun.subprocess.Popen
    try:
        emurun.system = lambda: 'darwin'
        emurun.subprocess.Popen = popen
        run = os.path.join(work, 'start-darwin')
        argv = ['/b/nulldc3.app/Contents/MacOS/nulldc3', '--portable', os.path.join(run, 'portable'), 'x.elf']
        env = emurun.environment(run)
        process = emurun.start(argv, 300, cwd=run, env=env, stdout=None, stderr=None)
        check(process.cmdline == emurun.command(argv, 300, 'darwin', {}, run) and process.kwargs.get('env') is env,
              'macOS: the emulator is not started behind macrun.py with the run\'s directory and the harness\'s environment: %r' % (process.cmdline,))
        check(process.kwargs.get('preexec_fn') is not None, 'macOS: the emulator is started without the core file size limit')
        tool_run = os.path.join(work, 'start-darwin-tool')
        tool = emurun.start(['/usr/local/bin/npm', 'run', 'contract'], 300, cwd=work, env=emurun.tool_environment(tool_run),
                            emulator=False)
        check(tool.cmdline[:7] == [sys.executable, '-I', '-B', emurun.MACRUN, '--work', tool_run, '--timeout'],
              'macOS: a tool is not started behind macrun.py with the directory of its run: %r' % (tool.cmdline,))
        for bad, what in (({}, 'no TMPDIR'), ({'TMPDIR': '/var/folders/xy/T/'}, 'the user\'s temporary directory as TMPDIR'),
                          ({'TMPDIR': 'tmp'}, 'a TMPDIR that is no absolute path')):
            try:
                emurun.start(argv, 300, cwd=run, env=bad)
            except emurun.Error:
                continue
            raise Failed('macOS: a program is started with an environment of %s: %r' % (what, started[-1].cmdline))

        # kill(): a SIGTERM to macrun.py, which ends its program, and a SIGKILL only when macrun.py has not ended
        process.ends = 0
        code = emurun.kill(process)
        check(process.done == ['SIGTERM', 'wait %d' % emurun.KILL_WAIT] and code == 143,
              'macOS: kill() did not end macrun.py by a SIGTERM: %r, exit status %r' % (process.done, code))
        stuck = emurun.start(argv, 300, cwd=run, env=env)
        stuck.ends = 1
        emurun.kill(stuck)
        check(stuck.done[:3] == ['SIGTERM', 'wait %d' % emurun.KILL_WAIT, 'SIGKILL'],
              'macOS: kill() of a macrun.py that does not end: %r' % (stuck.done,))
        parked = emurun.start(argv, 300, cwd=run, env=env)
        emurun.terminate(parked)
        check(parked.done == ['SIGTERM'], 'macOS: terminate() did not send macrun.py a SIGTERM alone: %r' % (parked.done,))
    finally:
        emurun.system, emurun.subprocess.Popen = saved
    return ('macOS: start() runs the emulator and a tool behind macrun.py with the directory of their TMPDIR, and nothing with '
            'another environment; kill() and terminate() end macrun.py by a SIGTERM')


def test_npm(work):
    d = os.path.join(work, 'nodejs')
    os.makedirs(d)
    for name in ('npm', 'npm.cmd'):
        with open(os.path.join(d, name), 'wb'):
            pass
    p = os.path.join(d, 'npm')
    check(emurun.npm(p, 'windows') == p + '.cmd', 'Windows: the npm beside the shell script is not npm.cmd: %r' % (emurun.npm(p, 'windows'),))
    check(emurun.npm(p + '.cmd', 'windows') == p + '.cmd', 'Windows: npm.cmd is not taken as it is')
    e = os.path.join(work, 'nonpm')
    os.makedirs(e)
    with open(os.path.join(e, 'npm'), 'wb'):
        pass
    check(emurun.npm(os.path.join(e, 'npm'), 'windows') is None, 'Windows: the npm without a suffix is taken')
    return 'npm: npm.cmd on Windows, never the shell script beside it'


def test_no_shell(work):
    check(emurun.no_shell('windows') == 'no POSIX shell on Windows', 'Windows: a POSIX shell is looked for: %r' % (emurun.no_shell('windows'),))
    check(emurun.no_shell('posix') is None, 'POSIX: no shell')
    return 'a POSIX shell: none on Windows'


def test_harnesses(work):
    """No harness starts a program but through emurun."""
    found = []
    for name in HARNESSES:
        path = os.path.join(TESTS, *name.split('/'))
        with open(path, 'r', encoding='utf-8') as f:
            tree = ast.parse(f.read(), path)
        imports = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                imports.update(alias.name.split('.')[0] for alias in node.names)
            elif isinstance(node, ast.ImportFrom):
                imports.add((node.module or '').split('.')[0])
            elif isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name):
                if node.value.id == 'subprocess' and node.attr not in SUBPROCESS_NAMES:
                    found.append('%s:%d subprocess.%s' % (name, node.lineno, node.attr))
                if node.value.id == 'os' and (node.attr.startswith('spawn') or node.attr.startswith('exec') or node.attr in ('system', 'popen', 'fork')):
                    found.append('%s:%d os.%s' % (name, node.lineno, node.attr))
                # the environment of a program comes from emurun (environment, tool_environment): on Windows the
                # caller's TEMP and npm cache are the profile's
                if node.value.id == 'os' and node.attr in ('environ', 'environb', 'getenv', 'getenvb', 'putenv', 'unsetenv'):
                    found.append('%s:%d os.%s' % (name, node.lineno, node.attr))
            # emurun builds the environment for the host the harness runs on, from the caller's: a harness that names
            # either one (a second argument, host=, environ=) builds another host's, on Windows the caller's TEMP
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name) \
                    and node.func.value.id == 'emurun' and node.func.attr in ('environment', 'tool_environment'):
                if len(node.args) > 1 or any(keyword.arg in ('host', 'environ', None) for keyword in node.keywords):
                    found.append('%s:%d emurun.%s names the host or the environment' % (name, node.lineno, node.func.attr))
            if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name) \
                    and node.func.value.id == 'emurun' and node.func.attr == 'start':
                env = [keyword.value for keyword in node.keywords if keyword.arg == 'env']
                if not env or (isinstance(env[0], ast.Constant) and env[0].value is None):
                    found.append('%s:%d emurun.start without the environment of its program (env=)' % (name, node.lineno))
        for module in ('resource', 'signal', 'multiprocessing', 'pty'):
            if module in imports:
                found.append('%s imports %s' % (name, module))
        check('emurun' in imports, '%s does not import emurun' % name)
    check(not found, 'a harness starts a program, or ends one, or makes its environment, without emurun: ' + '; '.join(found))
    return 'the %d harnesses start every program through emurun, with an environment of emurun' % len(HARNESSES)


def load_harness(name):
    """A harness of the tests (a path under tests/), imported as a module of its own: it runs nothing when it is not the
    main program. It imports the emurun of this test."""
    import importlib.util
    path = os.path.join(TESTS, *name.split('/'))
    spec = importlib.util.spec_from_file_location('harness_' + os.path.splitext(os.path.basename(path))[0], path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    check(module.emurun is emurun, '%s does not use the emurun of this test' % name)
    return module


def check_tool_env(what, env, run, caller):
    """env: the environment a harness built for a tool of its run directory run, on Windows, for a caller whose TEMP,
    TMP and npm cache are the values of caller."""
    temp = os.path.join(run, 'tmp')
    cache = os.path.join(run, 'npm-cache')
    temps = dict((name, value) for name, value in env.items() if name.upper() in ('TEMP', 'TMP'))
    caches = dict((name, value) for name, value in env.items() if name.upper() == 'NPM_CONFIG_CACHE')
    check(temps == {'TEMP': temp, 'TMP': temp} and list(caches.values()) == [cache],
          '%s, built for Windows: TEMP and TMP %r and npm\'s cache %r, not %s and %s of the run' % (what, temps, caches, temp, cache))
    kept = sorted(name for name, value in env.items() if value in caller)
    check(not kept, '%s, built for Windows, keeps the caller\'s %s' % (what, ', '.join(kept)))


def check_tool_env_darwin(what, env, run, caller):
    """env: the environment a harness built for a tool of its run directory run, on macOS, for a caller whose TMPDIR and
    npm cache are the values of caller."""
    temp = os.path.join(run, 'tmp')
    cache = os.path.join(run, 'npm-cache')
    caches = dict((name, value) for name, value in env.items() if name.upper() == 'NPM_CONFIG_CACHE')
    check(env.get('TMPDIR') == temp and list(caches.values()) == [cache],
          '%s, built for macOS: TMPDIR %r and npm\'s cache %r, not %s and %s of the run' % (what, env.get('TMPDIR'), caches, temp, cache))
    kept = sorted(name for name, value in env.items() if value in caller)
    check(not kept, '%s, built for macOS, keeps the caller\'s %s' % (what, ', '.join(kept)))


def test_tool_environments(work):
    """The environments contract.py and soundlines.py build for their tools, built as on a Windows host."""
    contract = load_harness('debugger/contract/contract.py')
    soundlines = load_harness('debugger/guest/dsptest/soundlines.py')
    caller = (os.path.join(work, 'caller', 'tmp'), os.path.join(work, 'caller', 'npm-cache'))
    names = ('TEMP', 'TMP', 'TMPDIR', 'npm_config_cache')
    saved_environ = dict((name, os.environ.get(name)) for name in names)
    saved_system, saved_start = emurun.system, emurun.start
    started = []

    class Ended(object):
        def wait(self, timeout=None):
            return 0

        def poll(self):
            return 0

    def start(argv, timeout, cwd=None, env=None, stdout=None, stderr=None, emulator=True):
        started.append((list(argv), env, emulator))
        return Ended()

    try:
        os.environ['TEMP'] = caller[0]
        os.environ['TMP'] = caller[0]
        os.environ['npm_config_cache'] = caller[1]
        emurun.system = lambda: 'windows'

        # the contract runner: npm.cmd and the node behind it
        run = os.path.join(work, 'contract')
        env = contract.runner_env(argparse.Namespace(work=run, npm=os.path.join(work, 'nodejs', 'npm.cmd')))
        check_tool_env('contract.py runner_env', env, run, caller)

        # a tool of a mode of soundlines.py, started by tool_run (here the start is the test's own)
        emurun.start = start
        log = os.path.join(work, 'soundlines', 'oracle-tool.txt')
        soundlines.tool_run(argparse.Namespace(watchdog=60), ['aica_dsp_guest_oracle'], log)
        check(len(started) == 1 and started[0][2] is False and started[0][1] is not None,
              'soundlines.py tool_run did not start its tool once, as a program that is not the emulator, with an environment: %r' % (started,))
        check_tool_env('soundlines.py tool_run', started[0][1], os.path.dirname(log), caller)

        # as on macOS: TMPDIR and npm's cache of the run, not the caller's (a caller there has TMPDIR, not TEMP and TMP)
        os.environ.pop('TEMP', None)
        os.environ.pop('TMP', None)
        os.environ['TMPDIR'] = caller[0]
        emurun.system = lambda: 'darwin'
        run = os.path.join(work, 'contract-darwin')
        env = contract.runner_env(argparse.Namespace(work=run, npm=os.path.join(work, 'nodejs', 'npm')))
        check_tool_env_darwin('contract.py runner_env', env, run, caller)
        del started[:]
        log = os.path.join(work, 'soundlines-darwin', 'oracle-tool.txt')
        soundlines.tool_run(argparse.Namespace(watchdog=60), ['aica_dsp_guest_oracle'], log)
        check(len(started) == 1 and started[0][2] is False and started[0][1] is not None,
              'soundlines.py tool_run did not start its tool once, as a program that is not the emulator, with an environment: %r' % (started,))
        check_tool_env_darwin('soundlines.py tool_run', started[0][1], os.path.dirname(log), caller)
    finally:
        emurun.system, emurun.start = saved_system, saved_start
        for name, value in saved_environ.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
    return ('the tools\' environments of contract.py and soundlines.py, built for Windows: TEMP, TMP and npm\'s cache of their '
            'run, for macOS: TMPDIR and npm\'s cache of their run; nothing of the caller\'s')


def run_child(work, emulator):
    """CHILD started through emurun on this host, as an emulator or as a program that is not one: what it held."""
    kind = 'emulator' if emulator else 'tool'
    script = os.path.join(work, 'child.py')
    if not os.path.isfile(script):
        with open(script, 'w', encoding='utf-8') as f:
            f.write(CHILD)
    run_dir = os.path.join(work, 'run-' + kind)
    os.makedirs(run_dir)
    env = emurun.environment(run_dir) if emulator else emurun.tool_environment(run_dir)
    log_path = os.path.join(run_dir, 'log.txt')
    with open(log_path, 'wb') as log:
        process = emurun.start([sys.executable, '-I', '-B', script], 60, cwd=run_dir, env=env, stdout=log, stderr=subprocess.STDOUT,
                               emulator=emulator)
        try:
            code = process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            emurun.kill(process)
            raise Failed('%s: the program did not end within 60 s' % kind)
    text = emurun.read_text(log_path)
    check(code == 0, '%s: the program ended with exit code %s:\n%s' % (kind, code, text))
    said = dict(line.split(': ', 1) for line in text.splitlines()
                if ': ' in line and not line.startswith('winrun: ') and not line.startswith('macrun: '))

    if emurun.system() == 'windows':
        desktop = re.search(r'^winrun: desktop \S*\\(winrun-\d+): \d+ window\(s\) seen, 0 visible, 0 of the emulator\'s class; '
                            r'the program\'s exit status 0; winrun exits with 0$', text, re.M)
        check(desktop is not None, '%s: the program did not run inside nulldc3-winrun: its output has no line of nulldc3-winrun\'s '
              'desktop:\n%s' % (kind, text))
        mine = desktop_of_this_thread()
        check(said.get('desktop') == desktop.group(1) and said.get('desktop') != mine,
              '%s: the program ran on the desktop "%s"; nulldc3-winrun made "%s", and this test is on "%s"' % (kind, said.get('desktop'), desktop.group(1), mine))
        names = said.get('environment', '').split(',')
        temp = os.path.normcase(os.path.join(run_dir, 'tmp'))
        check(os.path.normcase(said.get('temp', '')) == temp, '%s: the program\'s TEMP is %s, not the run\'s' % (kind, said.get('temp')))
        if emulator:
            # Windows itself puts a few variables into every process (a missing SystemRoot among them); none of the profile's
            profile = [name for name in names if name in ('PATH', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'HOMEPATH', 'USERNAME', 'HOME')]
            check('SYSTEMROOT' in names and 'TEMP' in names and not profile, 'emulator: the program had the environment %s' % names)
        else:
            check(os.path.normcase(said.get('npmcache', '')) == os.path.normcase(os.path.join(run_dir, 'npm-cache')) and 'PATH' in names,
                  'tool: the program had npm\'s cache in %s and the environment %s, not the caller\'s with the cache of its run' % (said.get('npmcache'), names))

        # kill(): nulldc3-winrun ends, and its job with everything in it, a program the program left running too
        log_path = os.path.join(run_dir, 'kill.txt')
        with open(log_path, 'wb') as log:
            process = emurun.start([sys.executable, '-I', '-B', script, 'kid'], 60, cwd=run_dir, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   emulator=emulator)
            kid = None
            deadline = time.monotonic() + 30
            while kid is None and time.monotonic() < deadline:
                found = re.search(r'^kid: (\d+)$', emurun.read_text(log_path), re.M)
                kid = int(found.group(1)) if found else None
                time.sleep(0.1)
            emurun.kill(process)
        check(kid is not None, '%s: the program did not say which program it started:\n%s' % (kind, emurun.read_text(log_path)))
        check(process_gone(kid, 10), '%s: the program the program started (pid %d) runs on when the run is killed: it is not in the job' % (kind, kid))
        return '%s inside nulldc3-winrun on %s; kill() ended it and its program' % (kind, desktop.group(1))

    if emurun.system() == 'darwin':
        return run_child_darwin(work, emulator, kind, script, run_dir, env, text, said)

    names = said.get('environment', '').split(',')
    if emulator:
        check(said.get('core') == '1', 'emulator: the program ran with a core file size limit of %s, not 1' % said.get('core'))
        check(set(names) <= set(emurun.SANITIZER_VARIABLES) | {'LC_CTYPE', ''}, 'emulator: the program had the environment %s' % names)
        return 'emulator: the program itself, with a core file size limit of 1 byte and the environment of emurun'
    check(set(name.upper() for name in os.environ) <= set(names) | {'LC_CTYPE'}, 'tool: the program had the environment %s, not the caller\'s' % names)
    return 'tool: the program itself, with the caller\'s environment'


# What Python and macOS put into the environment of a process themselves: LC_CTYPE (Python's locale coercion, in
# macrun.py and in the program, a Python here) and __CF_USER_TEXT_ENCODING (CoreFoundation, in every process that
# loads it: macrun.py's Python, the emulator)
DARWIN_OWN = ('LC_CTYPE', '__CF_USER_TEXT_ENCODING')


def pid_gone(pid, seconds):
    """POSIX: the process of that id has ended (and been reaped) within that time."""
    deadline = time.monotonic() + seconds
    while True:
        try:
            os.kill(pid, 0)
        except (ProcessLookupError, PermissionError):
            return True
        if time.monotonic() > deadline:
            return False
        time.sleep(0.1)


def run_child_darwin(work, emulator, kind, script, run_dir, env, text, said):
    """run_child on macOS: what the program found about itself behind macrun.py, and kill()."""
    check(re.search(r'^macrun: \S+: exit status 0$', text, re.M) is not None
          and re.search(r'^macrun: (inside the profile already|\S+: under the profile \S+)$', text, re.M) is not None,
          '%s: the program did not run through macrun.py: its output has no line of macrun.py\'s profile and end:\n%s' % (kind, text))
    check(said.get('sandboxed') == '1', '%s: the kernel says the program is not sandboxed (sandboxed: %s)' % (kind, said.get('sandboxed')))
    temp = os.path.join(run_dir, 'tmp')
    check(os.path.normpath(said.get('tmpdir', '')) == os.path.normpath(temp),
          '%s: the program\'s TMPDIR is %s, not the run\'s %s' % (kind, said.get('tmpdir'), temp))
    pids = [name for name in os.listdir(os.path.join(run_dir, 'run')) if name.endswith('.pid')] if os.path.isdir(os.path.join(run_dir, 'run')) else []
    check(pids, '%s: macrun.py wrote no id of the program into %s/run' % (kind, run_dir))
    check(said.get('core') == '1' or not emulator, 'emulator: the program ran with a core file size limit of %s, not 1' % said.get('core'))
    names = [name for name in said.get('environment', '').split(',') if name]
    if emulator:
        allowed = set(emurun.SANITIZER_VARIABLES) | {'TMPDIR'} | set(DARWIN_OWN)
        check('TMPDIR' in names and set(names) <= allowed, 'emulator: the program had the environment %s' % names)
    else:
        check(os.path.normpath(said.get('npmcache', '')) == os.path.normpath(os.path.join(run_dir, 'npm-cache')),
              'tool: the program had npm\'s cache in %s, not in the run\'s' % said.get('npmcache'))
        check(set(name.upper() for name in os.environ) <= set(names) | set(DARWIN_OWN),
              'tool: the program had the environment %s, not the caller\'s' % names)

    # kill(): macrun.py ends the program, in a session of its own, and the program the program started
    log_path = os.path.join(run_dir, 'kill.txt')
    with open(log_path, 'wb') as log:
        process = emurun.start([sys.executable, '-I', '-B', script, 'kid'], 60, cwd=run_dir, env=env, stdout=log,
                               stderr=subprocess.STDOUT, emulator=emulator)
        child = kid = None
        deadline = time.monotonic() + 30
        while kid is None and time.monotonic() < deadline:
            found = emurun.read_text(log_path)
            child = re.search(r'^pid: (\d+)$', found, re.M)
            kid = re.search(r'^kid: (\d+)$', found, re.M)
            time.sleep(0.1)
        code = emurun.kill(process)
    killed = emurun.read_text(log_path)
    check(child is not None and kid is not None, '%s: the program did not say which program it is and which it started:\n%s' % (kind, killed))
    for pid, what in ((int(child.group(1)), 'the program'), (int(kid.group(1)), 'the program the program started')):
        check(pid_gone(pid, 10), '%s: %s (pid %d) runs on when the run is killed:\n%s' % (kind, what, pid, killed))
    check(code == 128 + signal.SIGTERM and re.search(r'^macrun: \S+: SIGTERM to macrun\.py: ', killed, re.M) is not None,
          '%s: kill() did not end macrun.py by its SIGTERM (exit status %s):\n%s' % (kind, code, killed))
    return '%s through macrun.py, sandboxed, TMPDIR of its run; kill() ended it and its program' % kind


def test_run(work):
    """A program started through emurun, on this host: as an emulator, and as a program that is not one (the contract
    runner, the tools of the modes), which on Windows runs inside nulldc3-winrun all the same."""
    return 'a program through emurun: ' + '; '.join(run_child(work, emulator) for emulator in (True, False))


# Runs a harness with Python's temporary directory made one that is not there: tempfile.mkdtemp() or any other file
# of tempfile made without dir= fails the run. TRAP <directory that is not there> <script> <arguments>...
TRAP = r"""
import runpy
import sys
import tempfile
tempfile.tempdir = sys.argv[1]
sys.argv = sys.argv[2:]
runpy.run_path(sys.argv[0], run_name='__main__')
"""


def test_scratch(work):
    """selftest.py --mode log-rule writes under its --work, nothing into the caller's TEMP."""
    trap = os.path.join(work, 'trap.py')
    with open(trap, 'w', encoding='utf-8') as f:
        f.write(TRAP)
    run_dir = os.path.join(work, 'scratch')
    os.makedirs(run_dir)
    nowhere = os.path.join(run_dir, 'no-such-directory')
    rule = os.path.join(run_dir, 'log-rule')
    log_path = os.path.join(run_dir, 'log.txt')
    with open(log_path, 'wb') as log:
        process = emurun.start([sys.executable, '-I', '-B', trap, nowhere, os.path.join(TESTS, 'debugger', 'selftest', 'selftest.py'),
                                '--mode', 'log-rule', '--work', rule], 60, cwd=run_dir, env=emurun.tool_environment(run_dir),
                               stdout=log, stderr=subprocess.STDOUT, emulator=False)
        try:
            code = process.wait(timeout=60)
        except subprocess.TimeoutExpired:
            emurun.kill(process)
            raise Failed('selftest.py --mode log-rule did not end within 60 s')
    text = emurun.read_text(log_path)
    check(code == 0 and re.search(r'^PASS$', text, re.M) is not None and not os.path.exists(nowhere),
          'selftest.py --mode log-rule with Python\'s temporary directory at %s, which is not there: exit code %s:\n%s' % (nowhere, code, text))
    check(os.path.isdir(rule), 'selftest.py --mode log-rule made nothing under its --work ' + rule)
    return 'selftest.py --mode log-rule writes under its --work, with no temporary directory of Python\'s'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()
    work = os.path.abspath(args.work)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    tests = (test_environment, test_executable, test_read_text, test_command, test_start_darwin, test_npm, test_no_shell,
             test_harnesses, test_tool_environments, test_run, test_scratch)
    failed = 0
    for test in tests:
        try:
            print('ok   ' + test(work))
        except Failed as failure:
            print('FAIL %s: %s' % (test.__name__, failure))
            failed += 1
        except emurun.Error as error:
            print('FAIL %s: %s' % (test.__name__, error))
            failed += 1
    print('%d of %d passed' % (len(tests) - failed, len(tests)))
    if failed:
        print('the files of the test are in ' + work)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
