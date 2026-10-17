#!/usr/bin/env python3
"""What soundlines.py makes of a run that was not made on the engine it was asked for, on stand-ins for the emulator;
and what its mode names makes of the names of a build, on stand-ins for a build directory.

    soundlines_test.py --work <dir>

No emulator is run. The emulator takes config:Dynarec.Enabled=yes for a wish: where it has no recompiler it goes on
with the interpreter, prints "Using MCPU Interpreter" and exits with 0 like any other run. A runner that does not
look at that line compares the interpreter with itself in its mode engines and takes every line of the interpreter
for one of the recompiler; no run of a healthy default build shows that, so it is shown here.

A stand-in is a script that prints what an emulator prints for its keys: the line of the engine of the SH4, a
"SelfTest: ...: ok: ..." line when it is given a session, and the line of the sound harness or, without the key of
the harness, an exit line with a state hash. Its kind says which engine it claims:

    asked                     the one its keys ask for: an emulator, as far as the runner can tell
    interpreter               the interpreter, whatever is asked: a build without a recompiler
    recompiler                the recompiler, whatever is asked
    silent                    it prints no line of an engine
    interpreter-at-exit       the interpreter in the runs for the exit line, else the one that is asked for
    interpreter-in-session    the interpreter in the runs with a session, else the one that is asked for

Each case is one call of soundlines.py as a test makes it, with the exit code and the text it has to end with.
The cases on the kind asked pass: what fails the other kinds is the engine, and nothing else of the stand-in.

A stand-in for a build directory is a tree of CTestTestfile.cmake files, as CMake writes them (add_test with the
name in brackets or in quotes, subdirs), that register the names of the case. The names of the tests of the sound
(debugger.sound.*) and of the session arm7 of the selftests (debugger.selftest.arm7*) must not have a part batch or
persample, the renders their tests were named after; other tests may have those words, which name something else
there (a batch of requests of the debugger's protocol), and a build of the tests with such a test passes.
"""

import argparse
import os
import shutil
import stat
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

STAND_IN = r'''#!%(python)s
import sys

KIND = %(kind)r
keys = dict(item.split('=', 1) for item in sys.argv[1:] if '=' in item)
asked = 'jit' if keys.get('config:Dynarec.Enabled') == 'yes' else 'interp'
line = 'config:Debug.AicaHash' in keys
session = 'Debugger:SelfTest' in keys

engine = asked
if KIND == 'interpreter' or (KIND == 'interpreter-at-exit' and not line) or (KIND == 'interpreter-in-session' and session):
    engine = 'interp'
elif KIND == 'recompiler':
    engine = 'jit'

if KIND != 'silent':
    print('Using MCPU ' + ('Recompiler' if engine == 'jit' else 'Interpreter'))
if session:
    print('SelfTest: stand-in: ok: the session is at work')
if line:
    ticks = int(keys['config:Debug.AicaHash'])
    print('AicaHash: samples=%%016x regs=%%016x arm7=%%016x sh4=%%016x dsp=%%016x ticks=%%d cycles=%%d'
          %% (0x1111, 0x2222, 0x3333, 0x4444 if engine == 'jit' else 0x5555, 0xeeee, ticks, 145124 * ticks))
else:
    print('nulldc-exit: reason=vblanks vblanks=%%s hash=%%016x sh4=%%016x sched=%%016x ram=%%016x vram=%%016x aram=%%016x arm7=%%016x'
          %% (keys['config:Exit.AfterVblanks'], 0x6666, 0x7777 if engine == 'jit' else 0x8888, 0x9999, 0xaaaa, 0xbbbb, 0xcccc, 0xdddd))
'''

ASKED_JIT = 'was asked for the recompiler (config:Dynarec.Enabled=yes) and was made on the interpreter: the output has "Using MCPU Interpreter"'
ASKED_INTERP = 'was asked for the interpreter (config:Dynarec.Enabled=no) and was made on the recompiler: the output has "Using MCPU Recompiler"'

SAME = ['--mode', 'same']
ENGINES = ['--mode', 'engines']
NEUTRAL = ['--mode', 'neutral', '--vblanks', '3', '--selftest', 'stand-in', '--expect', 'the session is at work']

# (the kind of the stand-in, the arguments, the exit code, text of the output)
CASES = [
    ('asked', SAME + ['--engine', 'jit'], 0, 'two runs, one line: AicaHash: '),
    ('asked', SAME + ['--engine', 'interp'], 0, 'two runs, one line: AicaHash: '),
    ('asked', ENGINES, 0, 'the recompiler and the interpreter agree in samples=0000000000001111'),
    ('asked', NEUTRAL + ['--engine', 'jit'], 0, 'with the session stand-in and without it: one line, '),

    ('interpreter', SAME + ['--engine', 'jit'], 1, 'FAIL: the run line-1 ' + ASKED_JIT),
    ('interpreter', SAME + ['--engine', 'interp'], 0, 'two runs, one line: AicaHash: '),
    ('interpreter', ENGINES, 1, 'FAIL: the run jit ' + ASKED_JIT),
    ('interpreter', NEUTRAL + ['--engine', 'jit'], 1, 'FAIL: the run session-line ' + ASKED_JIT),

    ('recompiler', SAME + ['--engine', 'interp'], 1, 'FAIL: the run line-1 ' + ASKED_INTERP),
    ('recompiler', ENGINES, 1, 'FAIL: the run interp ' + ASKED_INTERP),

    ('silent', SAME + ['--engine', 'jit'], 1,
     'FAIL: the run line-1 was asked for the recompiler (config:Dynarec.Enabled=yes) and does not say that it was made on it: the output has no "Using MCPU Recompiler"'),

    ('interpreter-at-exit', SAME + ['--engine', 'jit'], 0, 'two runs, one line: AicaHash: '),
    ('interpreter-at-exit', NEUTRAL + ['--engine', 'jit'], 1, 'FAIL: the run session-hash ' + ASKED_JIT),
    ('interpreter-in-session', SAME + ['--engine', 'jit'], 0, 'two runs, one line: AicaHash: '),
    ('interpreter-in-session', NEUTRAL + ['--engine', 'jit'], 1, 'FAIL: the run session-line ' + ASKED_JIT),
]


# (what the build is, the names of its directories: the top one first, each behind the one before, the exit code,
# text of the output)
NAME_CASES = [
    ('the names of this build, and other tests with the words',
     [['debugger.sound.lines.dsptest.jit', 'debugger.selftest.arm7', 'debugger.selftest.arm7.interp', 'debugger.selftest.arm7.fallback'],
      ['debugger.sound.neutral.idlebp.jit', 'debugger.net.batch', 'devtools.rpc.batch.call'],
      ['debugger.selftest.writes.persample', 'arm7.persample.vectors', 'batch']],
     0, '10 tests, 2 of them debugger.sound.*: none of those and of debugger.selftest.arm7* has a part batch or persample'),
    ('a sound line of the batch render',
     [['debugger.sound.lines.dsptest.jit'], ['debugger.net.batch', 'debugger.sound.lines.dsptest.jit.batch']],
     1, 'FAIL: 1 of the 3 tests are named after a render of the sound: debugger.sound.lines.dsptest.jit.batch'),
    ('a neutrality test of the per-sample render, two directories down',
     [['debugger.sound.lines.dsptest.jit'], ['debugger.contract.m2'], ['debugger.sound.neutral.idlebp.jit.persample']],
     1, 'FAIL: 1 of the 3 tests are named after a render of the sound: debugger.sound.neutral.idlebp.jit.persample'),
    ('the session arm7 of the per-sample render',
     [['debugger.sound.lines.dsptest.jit', 'debugger.selftest.arm7', 'debugger.selftest.arm7.persample.interp']],
     1, 'FAIL: 1 of the 3 tests are named after a render of the sound: debugger.selftest.arm7.persample.interp'),
    ('no test of the sound',
     [['debugger.selftest.arm7', 'debugger.net.batch']],
     1, 'registers no test debugger.sound.*: it is not the build of the tests'),
]


def name_build(directory, levels):
    """A stand-in for a build directory: levels[0] registered in directory, levels[1] in its subdirectory, ..."""
    os.makedirs(directory)
    with open(os.path.join(directory, 'CTestTestfile.cmake'), 'w', encoding='utf-8') as f:
        f.write('# CMake generated Testfile for a stand-in\n')
        for n, name in enumerate(levels[0]):
            quoted = '"%s"' % name if n % 2 else '[=[%s]=]' % name
            f.write('add_test(%s "/bin/true" "--case" "%d")\nset_tests_properties(%s PROPERTIES  TIMEOUT "120")\n' % (quoted, n, quoted))
        if len(levels) > 1:
            f.write('subdirs("sub")\n')
    if len(levels) > 1:
        name_build(os.path.join(directory, 'sub'), levels[1:])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()

    work = os.path.abspath(args.work)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    program = os.path.join(work, 'program.elf')
    with open(program, 'wb') as f:
        f.write(b'no program: a stand-in does not load it\n')

    failed = 0
    for n, (kind, arguments, code, text) in enumerate(CASES, 1):
        emulator = os.path.join(work, 'stand-in-' + kind)
        if not os.path.isfile(emulator):
            with open(emulator, 'w', encoding='utf-8') as f:
                f.write(STAND_IN % {'python': sys.executable, 'kind': kind})
            os.chmod(emulator, os.stat(emulator).st_mode | stat.S_IXUSR)

        command = [sys.executable, os.path.join(HERE, 'soundlines.py'), '--emulator', emulator, '--work', os.path.join(work, 'case-%d' % n),
                   '--guest', program, '--ticks', '5'] + arguments
        done = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        said = done.stdout.decode('utf-8', 'replace')

        what = 'the stand-in %s, %s' % (kind, ' '.join(arguments))
        wrong = []
        if done.returncode != code:
            wrong.append('exit code %d, not %d' % (done.returncode, code))
        if text not in said:
            wrong.append('the output has no "%s"' % text)
        if ('PASS' in said.splitlines()) != (code == 0):
            wrong.append('the output has a line PASS' if code else 'the output has no line PASS')
        if wrong:
            failed += 1
            print('FAILED: %s: %s\n--- the output ---\n%s' % (what, '; '.join(wrong), said))
        else:
            print('ok: %s: %s' % (what, 'passes' if code == 0 else 'fails, "%s"' % text))

    for n, (what, levels, code, text) in enumerate(NAME_CASES, 1):
        build = os.path.join(work, 'names-%d' % n)
        name_build(build, levels)
        command = [sys.executable, os.path.join(HERE, 'soundlines.py'), '--mode', 'names', '--build', build]
        done = subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        said = done.stdout.decode('utf-8', 'replace')

        what = 'the names of a build, %s' % what
        wrong = []
        if done.returncode != code:
            wrong.append('exit code %d, not %d' % (done.returncode, code))
        if text not in said:
            wrong.append('the output has no "%s"' % text)
        if ('PASS' in said.splitlines()) != (code == 0):
            wrong.append('the output has a line PASS' if code else 'the output has no line PASS')
        if wrong:
            failed += 1
            print('FAILED: %s: %s\n--- the output ---\n%s' % (what, '; '.join(wrong), said))
        else:
            print('ok: %s: %s' % (what, 'passes' if code == 0 else 'fails, "%s"' % text))

    print('%d cases, %d failed' % (len(CASES) + len(NAME_CASES), failed))
    if failed:
        print('the output of every case is in ' + work)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
