"""How the harnesses of the tests start the emulator and the contract runner, on every host (PL-D5).

    import emurun

The harnesses tests/debugger/selftest/selftest.py, tests/debugger/contract/contract.py and
tests/debugger/guest/dsptest/soundlines.py, and tests/debugger/sh4bp/sessions.py, start their processes through
this file and read what those print through it. Python 3.9 and the standard library (PL-D74).

    environment(work)       the environment of an emulator run. On a POSIX host nothing of the caller's but the
                            options of the sanitizers: the run has its directory (--portable) and needs nothing else.
                            On Windows also SystemRoot, without which Winsock does not start (WSAStartup fails with
                            10106), and TEMP and TMP in the run's own directory (<work>/tmp): nothing of the profile
    tool_environment(work)  the environment of a program that is not the emulator (the contract runner, the tool of a
                            mode): the caller's. On Windows with TEMP and TMP in <work>/tmp and npm's cache, where npm
                            writes a log of every command, in <work>/npm-cache: node keeps its files in TEMP (tsx its
                            cache), and the caller's TEMP and npm cache are the profile's unless the caller moved them
    executable(path)        the program at a path that names it without its suffix: nulldc3 is nulldc3.exe on Windows
    command(argv, timeout)  what is started for argv: on Windows argv inside nulldc3-winrun (its path from the
                            environment variable NULLDC_WINRUN, which the tests project passes; a harness on Windows
                            refuses to start a program without it), with a timeout of its own behind the harness's
                            watchdog: a desktop of its own, a job that ends everything of the program when the job is
                            ended, and the error mode that keeps the system's boxes away (tests/winrun/README.md). On
                            a POSIX host argv itself
    start(argv, ...)        Popen of command(argv); on a POSIX host with a core file size limit of 1 byte for an
                            emulator (WSL2 pipes a crash to a core handler for any other limit). The handle is
                            nulldc3-winrun's on Windows: its exit status is the program's (or 124, 125, 126), and
                            ending it ends the job, so everything the program started ends with it
    kill(process)           ends what start() started, whatever it is doing: on Windows nulldc3-winrun, and with it
                            the job; on a POSIX host the process (SIGKILL)
    terminate(process)      as a harness ends an emulator that is parked: SIGTERM on a POSIX host, on Windows the
                            same as kill (Windows has no SIGTERM; the emulator has no handler for it on Linux either)
    read_text(path)         the text of a file a program wrote, as lines that end with \\n: a \\r in front of a \\n
                            is not part of the line, whatever wrote it (a program whose stdout is in text mode on
                            Windows writes \\r\\n)
    npm(name)               the npm to run: on Windows npm.cmd (the npm without a suffix beside it is a shell script,
                            which CreateProcess cannot run), found on the PATH or beside a path that names npm
    no_shell()              None where a POSIX shell can run a script, else why not: on Windows bash on the PATH is
                            WSL's launcher (C:\\Windows\\System32\\bash.exe), which would run the script inside a Linux
                            of the computer with a path of Windows; no test or script started on Windows resolves
                            bash or sh

The host is the one this Python runs on; every function takes the system ('posix' or 'windows') as an argument as
well, so that its test (emurun_test.py, harness.emurun) holds both branches on every host.
"""

import os
import shutil
import subprocess
import sys

SANITIZER_VARIABLES = ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS', 'LSAN_OPTIONS')

# The seconds nulldc3-winrun gives a program behind the watchdog of the harness: the harness ends it first
WINRUN_MARGIN = 60


class Error(Exception):
    pass


def system():
    """'windows' or 'posix': the host this Python runs on."""
    return 'windows' if sys.platform == 'win32' else 'posix'


def environment(work, host=None, environ=None):
    """The environment of an emulator run whose files are under work (see the top of the file)."""
    host = host or system()
    environ = os.environ if environ is None else environ
    env = {}
    for name in SANITIZER_VARIABLES:
        if name in environ:
            env[name] = environ[name]
    if host == 'windows':
        # Windows' names of variables do not care about case, and os.environ there has them in capitals
        root = environ.get('SystemRoot') or environ.get('SYSTEMROOT') or 'C:\\Windows'
        env['SystemRoot'] = root
        temp = os.path.join(work, 'tmp')
        os.makedirs(temp, exist_ok=True)
        env['TEMP'] = temp
        env['TMP'] = temp
    return env


# What tool_environment sets on Windows, by its name in capitals (Windows' names of variables do not care about case)
TOOL_VARIABLES = ('TEMP', 'TMP', 'NPM_CONFIG_CACHE')


def tool_environment(work, host=None, environ=None):
    """The environment of a program that is not the emulator, whose files are under work (see the top of the file)."""
    host = host or system()
    environ = os.environ if environ is None else environ
    env = dict(environ)
    if host == 'windows':
        for name in list(env):
            if name.upper() in TOOL_VARIABLES:
                del env[name]
        temp = os.path.join(work, 'tmp')
        os.makedirs(temp, exist_ok=True)
        env['TEMP'] = temp
        env['TMP'] = temp
        env['npm_config_cache'] = os.path.join(work, 'npm-cache')
    return env


def executable(path, host=None):
    """path, or path.exe on Windows where path names no file and path.exe does."""
    host = host or system()
    if host == 'windows' and not os.path.isfile(path) and not os.path.splitext(path)[1] and os.path.isfile(path + '.exe'):
        return path + '.exe'
    return path


def winrun(host=None, environ=None):
    """nulldc3-winrun on Windows (NULLDC_WINRUN), None on a POSIX host. Raises Error on Windows without it."""
    host = host or system()
    if host != 'windows':
        return None
    environ = os.environ if environ is None else environ
    path = environ.get('NULLDC_WINRUN', '')
    if not path:
        raise Error('NULLDC_WINRUN is not set: on Windows every program a harness starts runs inside nulldc3-winrun '
                    '(tests/winrun/README.md), and the tests project passes its path in NULLDC_WINRUN')
    if not os.path.isfile(path):
        raise Error('NULLDC_WINRUN names no file: ' + path)
    return path


def command(argv, timeout, host=None, environ=None):
    """What is started for argv (see the top of the file)."""
    runner = winrun(host, environ)
    if runner is None:
        return list(argv)
    return [runner, '--timeout', str(int(timeout) + WINRUN_MARGIN), '--'] + list(argv)


def _core_limit():
    # a crash is not piped to a core handler (WSL2 runs it for any other limit); POSIX only
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))


def start(argv, timeout, cwd=None, env=None, stdout=None, stderr=None, emulator=True):
    """Popen of command(argv): see the top of the file. emulator: the core file size limit on a POSIX host."""
    cmdline = command(argv, timeout)
    kwargs = {}
    if system() == 'posix' and emulator:
        kwargs['preexec_fn'] = _core_limit
    return subprocess.Popen(cmdline, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, **kwargs)


def kill(process):
    """Ends what start() started and waits for it; its exit status."""
    if process.poll() is None:
        process.kill()
    return process.wait()


def terminate(process):
    """SIGTERM on a POSIX host; on Windows nulldc3-winrun is ended, and its job with it. Does not wait."""
    if process.poll() is None:
        if system() == 'windows':
            process.kill()
        else:
            process.terminate()


def read_text(path):
    """The text of a file, without the \\r of a \\r\\n."""
    with open(path, 'rb') as f:
        data = f.read()
    return data.decode('utf-8', 'replace').replace('\r\n', '\n')


def npm(name='npm', host=None):
    """The npm to run, or None (see the top of the file)."""
    host = host or system()
    if host == 'windows':
        if os.path.dirname(name):
            base, suffix = os.path.splitext(name)
            if suffix.lower() in ('.cmd', '.exe'):
                return name if os.path.isfile(name) else None
            return base + '.cmd' if os.path.isfile(base + '.cmd') else None
        found = shutil.which(name + '.cmd') if not name.lower().endswith('.cmd') else shutil.which(name)
        return found
    return shutil.which(name)


def no_shell(host=None):
    """None where a POSIX shell runs scripts, else the reason why not (see the top of the file)."""
    host = host or system()
    if host == 'windows':
        return 'no POSIX shell on Windows'
    return None
