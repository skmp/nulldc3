#!/usr/bin/env python3
"""The run helper of the harnesses (emurun.py) on itself: harness.emurun.

    emurun_test.py --work <dir>

What it holds, on every host, for both systems (each function of emurun takes the system as an argument):

    environment     on a POSIX host nothing of the caller's environment but the options of the sanitizers; on Windows
                    those, SystemRoot (Winsock does not start without it) and TEMP and TMP in <work>/tmp, made there,
                    and nothing of the profile (no PATH, no USERPROFILE, no APPDATA, no LOCALAPPDATA, no HOME)
    tool_environment
                    the environment of a program that is not the emulator: the caller's; on Windows with TEMP and TMP
                    in <work>/tmp, made there, and npm's cache in <work>/npm-cache, in place of the caller's in any
                    letter case
    executable      nulldc3 is nulldc3.exe on Windows where nulldc3 names no file; on a POSIX host a path is a path
    read_text       a \\r in front of a \\n is not part of the line
    command         on Windows the program inside nulldc3-winrun (NULLDC_WINRUN), with a timeout of its own behind
                    the harness's; without NULLDC_WINRUN, or with one that names no file, no command; on a POSIX host
                    the program itself
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
                    <work>/npm-cache of their run, and no value of the caller's

and with a program it starts (this Python, on a script of the work directory), once as an emulator and once as a
program that is not one (emulator=False: the contract runner, npm.cmd and node, and the tools of the modes):

    on Windows      the program ran inside nulldc3-winrun: its output ends with nulldc3-winrun's line of its desktop,
                    winrun-<n>, and the program found itself on a desktop of that name that is not the one of this
                    test (which may be inside a nulldc3-winrun of its own, ctest's); as an emulator it was given the
                    environment of emurun and nothing else of the profile, as a tool the caller's with the TEMP, TMP
                    and npm cache of its run; kill() ended it and the program it had started, which nothing waited
                    for: the job ended both
    on a POSIX host the program ran; as an emulator with a core file size limit of 1 byte and the environment of
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
    return ('the environment: POSIX the sanitizers\' options alone; Windows SystemRoot, TEMP and TMP of the run and nothing of '
            'the profile; a tool the caller\'s, on Windows with TEMP, TMP and npm\'s cache of its run')


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
    return 'the command: on Windows inside nulldc3-winrun, and none without it; on a POSIX host the program'


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


def test_tool_environments(work):
    """The environments contract.py and soundlines.py build for their tools, built as on a Windows host."""
    contract = load_harness('debugger/contract/contract.py')
    soundlines = load_harness('debugger/guest/dsptest/soundlines.py')
    caller = (os.path.join(work, 'caller', 'tmp'), os.path.join(work, 'caller', 'npm-cache'))
    names = ('TEMP', 'TMP', 'npm_config_cache')
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
    finally:
        emurun.system, emurun.start = saved_system, saved_start
        for name, value in saved_environ.items():
            if value is None:
                os.environ.pop(name, None)
            else:
                os.environ[name] = value
    return ('the tools\' environments of contract.py and soundlines.py, built for Windows: TEMP, TMP and npm\'s cache of their '
            'run, nothing of the caller\'s')


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
    said = dict(line.split(': ', 1) for line in text.splitlines() if ': ' in line and not line.startswith('winrun: '))

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

    names = said.get('environment', '').split(',')
    if emulator:
        check(said.get('core') == '1', 'emulator: the program ran with a core file size limit of %s, not 1' % said.get('core'))
        check(set(names) <= set(emurun.SANITIZER_VARIABLES) | {'LC_CTYPE', ''}, 'emulator: the program had the environment %s' % names)
        return 'emulator: the program itself, with a core file size limit of 1 byte and the environment of emurun'
    check(set(name.upper() for name in os.environ) <= set(names) | {'LC_CTYPE'}, 'tool: the program had the environment %s, not the caller\'s' % names)
    return 'tool: the program itself, with the caller\'s environment'


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

    tests = (test_environment, test_executable, test_read_text, test_command, test_npm, test_no_shell, test_harnesses,
             test_tool_environments, test_run, test_scratch)
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
