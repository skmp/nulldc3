#!/usr/bin/env python3
"""A selftest session of the emulator, headless, and what it has to end with.

    selftest.py --emulator <nulldc3> --work <dir> --selftest <filter> [--mode <mode>] [--engine jit|interp]
                [--guest <image>] [--config <section:key=value>]... [--watchdog <seconds>]

The emulator is run as tests/system runs it: its files in a directory of its own under the work directory
(--portable), an empty environment (no display), a core file size limit of 1 byte, and a watchdog. The
real-time clock is pinned. On Windows the environment has SystemRoot and TEMP and TMP of the run's directory, and the
emulator runs inside nulldc3-winrun (tests/common/emurun.py, which starts every program of this script and reads
what it prints). README.md has the modes:

    session     --config Debugger:SelfTest=<filter>: exit code 0 and the summary line of the driver, nothing failed
    guest-exit  a session whose guest returns to the BIOS: the run ends with its exit line, reason guest-exit
    same-hash   a session that runs to the end an Exit.* key gives it, and the same run without a session:
                the two exit lines have the same state hash, part for part
    parked      the run stays parked: after --hold seconds the process is still there and has printed no exit
                line. It is killed, and that is a pass
    log-rule    what the checks of the other modes make of the output of a run, on outputs that are made up, with
                lines that end in \n and in \r\n: no emulator is run, and --emulator and --work are not asked for

Every run has to be made on the engine it was asked for, and a run with a session on the SH4 debug engine as well.
The emulator takes config:Dynarec.Enabled=yes for a wish: a build without a recompiler, or one whose recompiler
cannot be set up, goes on with the interpreter and says "Using MCPU Interpreter" where the other says "Using MCPU
Recompiler". The driver of a session says first what the SH4 runs on and what executes its breakpoints and steps,
by the name debugger.describe gives it ("SelfTest: engine: the SH4 runs on ..., its breakpoints and steps on ..."):
the SH4 on the engine of that line too, and on the recompiler sh4-debug-fallback with Debugger:ForceFallback=yes
among the --config items and sh4-debug-native without it (on an x86-64 host, whose recompiler has native
breakpoints; on another the recompiler has none and runs them on the fallback), on the interpreter
sh4-debug-native, which the key does not change. Behind that line the emulator says once that this is the engine
its configuration asks for ("SelfTest: engine.asked: note: ..."; when it is not, the line is a failed check). A run
that was made on another engine fails, and says which engine it found and which one was asked for.
"""

import argparse
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile

# the run helper of the harnesses; nothing is written into the source tree for it
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'common'))

import emurun  # noqa: E402

RTC_PIN = 946684800

BAD = ('Fatal error', 'Verify Failed', 'fault_handler', 'AddressSanitizer', 'ThreadSanitizer', 'runtime error:')

# what the emulator thread prints when it has chosen the engine of the SH4 (dc_run of libswirl/libswirl.cpp)
ENGINE_SAYS = {'jit': 'Using MCPU Recompiler', 'interp': 'Using MCPU Interpreter'}
ENGINE_NAMES = {'jit': 'the recompiler', 'interp': 'the interpreter'}

# what the driver of a session prints first (dbg_selftest.cpp): the SH4's engine, and its debug engine by the name
# of debugger.describe's capabilities
SESSION_ENGINE = re.compile(r'^SelfTest: engine: (.*)$', re.M)
SESSION_SH4 = {'jit': 'the SH4 runs on the recompiler (', 'interp': 'the SH4 runs on the interpreter,'}
DEBUG_ENGINES = ('sh4-debug-native', 'sh4-debug-fallback')

# what the emulator says behind it, when the engine is the one its configuration asks for (engine.asked of
# dbg_selftest_emu.cpp; a FAILED line of it when not, which no_failures() finds)
SESSION_ASKED = re.compile(r'^SelfTest: engine\.asked: note: the engine of the session is the one its configuration asks for: ', re.M)

# The recompiler of an x86-64 host executes breakpoints and steps itself (SH4DBG_NATIVE, hw/sh4/sh4_if.h); the one
# of an arm64 host does not, and runs them on the interpreter fallback with or without Debugger:ForceFallback
NATIVE_RECOMPILER = platform.machine().lower() in ('x86_64', 'amd64')


class Failed(Exception):
    pass


def read_log(path):
    """What a run printed, as lines that end with \\n (a \\r in front of one is not part of the line)."""
    return emurun.read_text(path)


def check(ok, what, log=None):
    if not ok:
        tail = ''
        if log is not None:
            tail = '\n--- the end of the output ---\n' + '\n'.join(log.splitlines()[-40:])
        raise Failed(what + tail)


class Run:
    def __init__(self, args, name):
        self.args = args
        self.dir = os.path.join(args.work, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        # the directory of the run's files (--portable); a state is saved into its data/, which the emulator
        # does not make
        os.makedirs(os.path.join(self.dir, 'portable', 'data'))
        self.log_path = os.path.join(self.dir, 'log.txt')
        self.process = None
        self.log = ''
        self.code = None

    def command(self, keys):
        engine = 'yes' if self.args.engine == 'jit' else 'no'
        items = ['config:Headless=yes', 'audio:backend=none', 'config:Debug.SerialConsoleEnabled=yes',
                 'config:Dynarec.Enabled=' + engine, 'config:Dreamcast.RTC=%d' % RTC_PIN, 'config:pvr.backend=none']
        items += keys + self.args.config
        command = [self.args.emulator, '--portable', os.path.join(self.dir, 'portable'), '--config']
        for i, item in enumerate(items):
            if i:
                command.append(',')
            command.append(item)
        command.append(self.args.guest)
        return command

    def start(self, keys):
        # nothing of the environment but the options of the sanitizers (on Windows also what Windows needs, in the
        # directory of the run): the directory is the one of --portable
        self.cmdline = self.command(keys)
        self.out = open(self.log_path, 'wb')
        self.process = emurun.start(self.cmdline, self.args.watchdog, cwd=self.dir, env=emurun.environment(self.dir),
                                    stdout=self.out, stderr=subprocess.STDOUT)

    def read(self):
        self.log = read_log(self.log_path)
        return self.log

    def wait(self, seconds):
        """True when the process ended within that time."""
        try:
            self.code = self.process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            return False
        finally:
            self.read()
        self.out.close()
        return True

    def kill(self):
        self.code = emurun.kill(self.process)
        self.out.close()
        self.read()

    def run(self, keys):
        self.start(keys)
        if not self.wait(self.args.watchdog):
            self.kill()
            raise Failed('the run did not end within %d s: %s' % (self.args.watchdog, ' '.join(self.cmdline))
                         + '\n--- the end of the output ---\n' + '\n'.join(self.log.splitlines()[-40:]))
        for bad in BAD:
            check(bad not in self.log, 'the output has "%s"' % bad, self.log)
        return self


def forced_fallback(args):
    """Is Debugger:ForceFallback set by the --config items? The last one that names the key decides."""
    forced = False
    for item in args.config:
        key, _, value = item.partition('=')
        if key.strip().lower() == 'debugger:forcefallback':
            forced = value.strip().lower() in ('yes', 'true', '1')
    return forced


def debug_engine_asked(args):
    """The SH4 debug engine a run of these arguments asks for, and what asks for it."""
    if args.engine == 'interp':
        return 'sh4-debug-native', 'the interpreter (config:Dynarec.Enabled=no), whose breakpoints and steps are its own%s' % (
            ', with or without Debugger:ForceFallback' if forced_fallback(args) else '')
    if forced_fallback(args):
        return 'sh4-debug-fallback', 'the recompiler (config:Dynarec.Enabled=yes) with Debugger:ForceFallback=yes'
    if NATIVE_RECOMPILER:
        return 'sh4-debug-native', 'the recompiler (config:Dynarec.Enabled=yes) of an x86-64 host, without Debugger:ForceFallback'
    return 'sh4-debug-fallback', 'the recompiler (config:Dynarec.Enabled=yes) of a %s host, which has no native breakpoints' % platform.machine()


def check_engine(args, run, session):
    """The run was made on the engine it asked for; a run with a session also on the SH4 debug engine it asked
    for. Returns what it found, for the line of the test."""
    other = 'interp' if args.engine == 'jit' else 'jit'
    asked = '%s (config:Dynarec.Enabled=%s)' % (ENGINE_NAMES[args.engine], 'yes' if args.engine == 'jit' else 'no')
    check(ENGINE_SAYS[other] not in run.log, 'the run was asked for %s and was made on %s: the output has "%s"'
          % (asked, ENGINE_NAMES[other], ENGINE_SAYS[other]), run.log)
    check(ENGINE_SAYS[args.engine] in run.log, 'the run was asked for %s and does not say that it was made on it: the '
          'output has no "%s"' % (asked, ENGINE_SAYS[args.engine]), run.log)
    if not session:
        return ENGINE_NAMES[args.engine]

    said = SESSION_ENGINE.findall(run.log)
    check(len(said) == 1, 'the session said its engine %d times, not once ("SelfTest: engine: ...")' % len(said), run.log)
    sh4 = [engine for engine in ENGINE_NAMES if said[0].startswith(SESSION_SH4[engine])]
    check(sh4 == [args.engine], 'the SH4 of the session runs on %s, the run asked for %s, and its output has "%s": '
          '"SelfTest: engine: %s"' % (ENGINE_NAMES[sh4[0]] if sh4 else 'an engine that it does not name', asked,
                                       ENGINE_SAYS[args.engine], said[0]), run.log)
    found = [name for name in DEBUG_ENGINES if said[0].endswith(' ' + name)]
    wanted, why = debug_engine_asked(args)
    check(found == [wanted], 'the SH4 debug engine of the session is %s, the run asked for %s (%s): "SelfTest: engine: %s"'
          % (found[0] if found else 'none that it names', wanted, why, said[0]), run.log)
    check(len(SESSION_ASKED.findall(run.log)) == 1, 'the emulator did not say once that the engine of the session is '
          'the one its configuration asks for ("SelfTest: engine.asked: note: ...")', run.log)
    return said[0]


def selftest_lines(log):
    return [line for line in log.splitlines() if line.startswith('SelfTest: ')]


def exit_line(log):
    lines = [line for line in log.splitlines() if line.startswith('nulldc-exit: ')]
    if not lines:
        return None
    return dict(pair.split('=', 1) for pair in lines[-1][len('nulldc-exit: '):].split(' ') if '=' in pair)


def no_failures(run):
    failed = [line for line in selftest_lines(run.log) if ': FAILED: ' in line or ': FAIL, ' in line]
    check(not failed, 'checks failed:\n' + '\n'.join(failed[:20]), run.log)


def mode_session(args):
    run = Run(args, 'session').run(['Debugger:SelfTest=' + args.selftest])
    engine = check_engine(args, run, True)
    no_failures(run)
    lines = selftest_lines(run.log)
    check(lines, 'the session printed nothing', run.log)
    summary = re.match(r'SelfTest: (\d+) passed, (\d+) failed$', lines[-1])
    check(summary is not None, 'the last line of the session is not its summary: ' + lines[-1], run.log)
    check(int(summary.group(1)) > 0 and int(summary.group(2)) == 0, lines[-1], run.log)
    check(run.code == 0, 'exit code %s' % run.code, run.log)
    print('engine: ' + engine)
    print(lines[-1])


def mode_guest_exit(args):
    run = Run(args, 'guest-exit').run(['Debugger:SelfTest=' + args.selftest])
    engine = check_engine(args, run, True)
    no_failures(run)
    check(any(': ok: ' in line for line in selftest_lines(run.log)), 'the test of the session did not run', run.log)
    line = exit_line(run.log)
    check(line is not None, 'the run printed no exit line', run.log)
    check(line.get('reason') == 'guest-exit', 'the run ended with reason=%s' % line.get('reason'), run.log)
    check(run.code == 0, 'exit code %s' % run.code, run.log)
    print('engine: ' + engine)
    print('the run ended at the guest exit: reason=guest-exit')


HASH_FIELDS = ('reason', 'vblanks', 'hash', 'sh4', 'sched', 'ram', 'vram', 'aram', 'arm7')


def mode_same_hash(args):
    keys = ['config:Exit.AfterVblanks=%d' % args.vblanks, 'config:Exit.StateHash=yes']

    plain = Run(args, 'plain').run(keys)
    check_engine(args, plain, False)
    base = exit_line(plain.log)
    check(base is not None and 'hash' in base, 'the run without a session printed no state hash', plain.log)

    run = Run(args, 'session').run(keys + ['Debugger:SelfTest=' + args.selftest])
    engine = check_engine(args, run, True)
    no_failures(run)
    check(any(': ok: ' in line for line in selftest_lines(run.log)), 'the test of the session did not run', run.log)
    for note in args.expect:
        check(any(note in line for line in selftest_lines(run.log)), 'the session did not say "%s"' % note, run.log)

    line = exit_line(run.log)
    check(line is not None and 'hash' in line, 'the session printed no state hash', run.log)

    different = [f for f in HASH_FIELDS if base.get(f) != line.get(f)]
    check(not different, 'the run of the session differs from the run nobody touched in ' + ', '.join(
        '%s (%s, not %s)' % (f, line.get(f), base.get(f)) for f in different), run.log)
    check(run.code == 0 and plain.code == 0, 'exit codes %s and %s' % (plain.code, run.code), run.log)
    print('engine: ' + engine)
    print('both runs end in hash=%s after %s vblanks' % (line['hash'], line['vblanks']))


def mode_parked(args):
    keys = ['config:Exit.AfterVblanks=%d' % args.vblanks]
    if args.selftest:
        keys.append('Debugger:SelfTest=' + args.selftest)

    run = Run(args, 'parked')
    run.start(keys)
    ended = run.wait(args.hold)
    if not ended:
        run.kill()

    check(not ended, 'the run ended (exit code %s) where it had to stay parked' % run.code, run.log)
    check(exit_line(run.log) is None, 'the run printed an exit line while it was parked', run.log)
    for bad in BAD:
        check(bad not in run.log, 'the output has "%s"' % bad, run.log)
    engine = check_engine(args, run, bool(args.selftest))
    no_failures(run)
    for note in args.expect:
        check(note in run.log, 'the run did not say "%s"' % note, run.log)
    print('engine: ' + engine)
    print('still parked after %.1f s, no exit line: the run was killed' % args.hold)


# The made-up output of log-rule: a session on the engine it asks for, behind the lines of the emulator
RULE_LOG = (
    'nullDC 3.0.0-pre\n'
    '%(says)s\n'
    'SelfTest: engine: %(engine)s\n'
    'SelfTest: engine.asked: note: the engine of the session is the one its configuration asks for: made up\n'
    'SelfTest: emu.made.up: ok: a test of the session ran\n'
    'SelfTest: 7 passed, 0 failed\n'
    'nulldc-exit: reason=guest-exit vblanks=12 hash=0123456789abcdef sh4=1 sched=2 ram=3 vram=4 aram=5 arm7=6\n'
)


def mode_log_rule(args):
    """What the checks of the modes make of the output of a run: the engine of the session by the names of its line,
    the summary, the exit line and its last field, on outputs that are made up. Every line ends in \\n, and in a
    second copy in \\r\\n, as a program whose stdout is in text mode writes it on Windows: the reading of the output
    has to give the same fields for both, or the names of the engine and the last field of a line would end in a
    \\r that no check expects. No emulator is run."""
    work = tempfile.mkdtemp(prefix='selftest-log-rule-')
    cases = 0
    try:
        for engine in ('jit', 'interp'):
            args.engine = engine
            args.config = []
            wanted, _ = debug_engine_asked(args)
            said = 'the SH4 runs on %s, its breakpoints and steps on %s' % (
                'the recompiler (x64)' if engine == 'jit' else 'the interpreter', wanted)
            text = RULE_LOG % {'says': ENGINE_SAYS[engine], 'engine': said}
            for ending in ('\n', '\r\n'):
                path = os.path.join(work, 'log-%s-%d.txt' % (engine, len(ending)))
                with open(path, 'wb') as f:
                    f.write(text.replace('\n', ending).encode('utf-8'))

                class Made:
                    pass
                run = Made()
                run.log = read_log(path)
                what = 'the output of a session on %s with lines that end in %s' % (ENGINE_NAMES[engine], repr(ending))
                try:
                    found = check_engine(args, run, True)
                except Failed as failure:
                    raise Failed('%s: %s' % (what, failure))
                check(found == said, '%s: the engine line is read as %r, not %r' % (what, found, said))
                lines = selftest_lines(run.log)
                check(re.match(r'SelfTest: (\d+) passed, (\d+) failed$', lines[-1]) is not None,
                      '%s: the summary is read as %r' % (what, lines[-1]))
                line = exit_line(run.log)
                check(line is not None and line.get('reason') == 'guest-exit' and line.get('arm7') == '6',
                      '%s: the exit line is read as %r' % (what, line))
                cases += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('%d made-up outputs of a session, with lines that end in \\n and in \\r\\n: the engine, the summary and the exit line '
          'read the same from both' % cases)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--emulator', default='', help='every mode but log-rule')
    parser.add_argument('--work', default='', help='every mode but log-rule')
    parser.add_argument('--selftest', default='')
    parser.add_argument('--mode', default='session', choices=('session', 'guest-exit', 'same-hash', 'parked', 'log-rule'))
    parser.add_argument('--engine', default='jit', choices=('jit', 'interp'))
    parser.add_argument('--guest', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'emu_guest.bin'))
    parser.add_argument('--config', action='append', default=[], help='section:key=value, for every run')
    parser.add_argument('--expect', action='append', default=[], help='text a line of the session has to have')
    parser.add_argument('--vblanks', type=int, default=600, help='same-hash, parked: Exit.AfterVblanks of the runs')
    parser.add_argument('--hold', type=float, default=4.0, help='parked: seconds the run has to stay')
    parser.add_argument('--watchdog', type=int, default=300)
    parser.add_argument('--keep', action='store_true', help='keep the work directory of a run that passed')
    args = parser.parse_args()

    if args.mode == 'log-rule':
        # nothing is run, and nothing is written but a scratch directory of its own
        try:
            mode_log_rule(args)
        except Failed as failure:
            print('FAIL: %s' % failure)
            return 1
        print('PASS')
        return 0

    missing = ['--' + name for name in ('emulator', 'work') if not getattr(args, name)]
    if missing:
        parser.error('the following arguments are required: ' + ', '.join(missing))

    # nulldc3 is nulldc3.exe on Windows
    args.emulator = emurun.executable(os.path.abspath(args.emulator))
    args.work = os.path.abspath(args.work)
    args.guest = os.path.abspath(args.guest)

    if not os.path.isfile(args.emulator):
        print('FAIL: no emulator at ' + args.emulator)
        return 1
    if not os.path.isfile(args.guest):
        print('FAIL: no guest at ' + args.guest)
        return 1

    modes = {'session': mode_session, 'guest-exit': mode_guest_exit, 'same-hash': mode_same_hash, 'parked': mode_parked}

    try:
        modes[args.mode](args)
    except Failed as failure:
        print('FAIL: %s' % failure)
        print('the output of every run is in ' + args.work)
        return 1

    if not args.keep:
        shutil.rmtree(args.work, ignore_errors=True)
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
