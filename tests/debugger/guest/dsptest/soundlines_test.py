#!/usr/bin/env python3
"""What soundlines.py makes of a run that was not made on the engine it was asked for, on stand-ins for the emulator;
what its mode held makes of the copies of the guest it builds, on stand-ins whose line depends on the guest; and
what its mode names makes of the names of a build, on stand-ins for a build directory.

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

Those stand-ins print one fixed line. The mode held compares copies of the guest that it builds itself, so its
stand-ins print a line that depends on the guest they are given, as the emulator's does: the engine it asks for,
and a samples field that is one value where that guest's ARM7 would run its program (the halfword at g_main - 4 is
the mov #0, r0 of the release, and the word at a_reset of the ARM7 program is the guest's) and another where it
would not; arm7 is formed from the two words, the other fields are fixed. The mode runs on a copy of the
committed dsptest.elf, at the labels of this directory:

    held                      that line: a held ARM7 is an idle one, and the mode passes
    held-regs                 another regs for the copy whose ARM7 stays held: the mode names the field
    held-silent               the silence of no pair for both copies: the copy held is silent
    held-deaf                 one samples for every guest: the copy idle sounds as the guest

and on a copy whose halfword at g_main - 4 is a nop, which the mode refuses to patch, naming the halfword. On
the kind asked both copies print one arm7: the patches did not take.

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
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# the runner's own reading of the guest's labels and of its ELF; no bytecode goes into the source tree
sys.dont_write_bytecode = True
sys.path.insert(0, HERE)
import soundlines  # noqa: E402

STAND_IN = r'''#!%(python)s
import struct
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

samples, regs, arm7 = 0x1111, 0x2222, 0x3333
if KIND.startswith('held'):
    # the line depends on the guest it is given, as the emulator's does: one samples where that guest's ARM7 would
    # run its program (the release in front of g_main and the instruction at a_reset are the guest's), another
    # where it would not; arm7 is formed from both words
    with open(sys.argv[-1], 'rb') as f:
        image = f.read()
    half, = struct.unpack_from('<H', image, %(release_at)d)
    word, = struct.unpack_from('<I', image, %(reset_at)d)
    runs = half == 0xE000 and word == %(reset_word)d
    samples = 0x1111 if runs else 0x6060
    arm7 = (half << 32) | word
    if KIND == 'held-regs' and half != 0xE000:
        regs = 0x2223          # another regs for the copy whose ARM7 stays held
    if KIND == 'held-deaf':
        samples = 0x6060       # the guest's FIQ handler is not heard: the guest sounds as the copy idle
    if KIND == 'held-silent' and not runs:
        samples = 0xcbf29ce484222325      # the copies are silent: the hash of no pair

if KIND != 'silent':
    print('Using MCPU ' + ('Recompiler' if engine == 'jit' else 'Interpreter'))
if session:
    print('SelfTest: stand-in: ok: the session is at work')
if line:
    ticks = int(keys['config:Debug.AicaHash'])
    print('AicaHash: samples=%%016x regs=%%016x arm7=%%016x sh4=%%016x dsp=%%016x ticks=%%d cycles=%%d'
          %% (samples, regs, arm7, 0x4444 if engine == 'jit' else 0x5555, 0xeeee, ticks, 145124 * ticks))
else:
    print('nulldc-exit: reason=vblanks vblanks=%%s hash=%%016x sh4=%%016x sched=%%016x ram=%%016x vram=%%016x aram=%%016x arm7=%%016x'
          %% (keys['config:Exit.AfterVblanks'], 0x6666, 0x7777 if engine == 'jit' else 0x8888, 0x9999, 0xaaaa, 0xbbbb, 0xcccc, 0xdddd))
'''

ASKED_JIT = 'was asked for the recompiler (config:Dynarec.Enabled=yes) and was made on the interpreter: the output has "Using MCPU Interpreter"'
ASKED_INTERP = 'was asked for the interpreter (config:Dynarec.Enabled=no) and was made on the recompiler: the output has "Using MCPU Recompiler"'

SAME = ['--mode', 'same']
ENGINES = ['--mode', 'engines']
NEUTRAL = ['--mode', 'neutral', '--vblanks', '3', '--selftest', 'stand-in', '--expect', 'the session is at work']

# the mode held on a copy of the guest of this directory ({elf}), at its labels; {nop} is a copy whose halfword at
# g_main - 4 is a nop, not the mov #0, r0 of the release the mode makes a hold
HELD = ['--mode', 'held', '--guest', '{elf}', '--dir', HERE]
HELD_NOP = ['--mode', 'held', '--guest', '{nop}', '--dir', HERE]
HELD_PASS = 'a held ARM7 is an idle one for the rest of the AICA: samples=0000000000006060 regs=0000000000002222 sh4='

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

    # held: a stand-in whose line depends on the guest it is given, and its variants
    ('held', HELD + ['--engine', 'jit'], 0, HELD_PASS),
    ('held', HELD + ['--engine', 'interp'], 0, HELD_PASS),
    ('held-regs', HELD + ['--engine', 'jit'], 1,
     'FAIL: the copy whose ARM7 stays held and the copy whose ARM7 idles on b . differ in regs (0000000000002223 and 0000000000002222)'),
    ('held', HELD_NOP + ['--engine', 'jit'], 1, 'FAIL: the halfword at g_main - 4 (%(release)08X) of the guest is 0009, not E000 (mov #0, r0)'),
    ('asked', HELD + ['--engine', 'jit'], 1, 'FAIL: the copy held and the copy idle print one arm7 field, 0000000000003333'),
    ('held-silent', HELD + ['--engine', 'jit'], 1, 'FAIL: the copy whose ARM7 stays held is silent: samples=cbf29ce484222325 is the hash of 0 * 32 pairs'),
    ('held-deaf', HELD + ['--engine', 'jit'], 1, 'FAIL: the copy whose ARM7 idles on b . prints the samples of the guest, 0000000000006060'),
    ('interpreter', HELD + ['--engine', 'jit'], 1, 'FAIL: the run line-1 ' + ASKED_JIT),
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

    # the guest of this directory, a copy of it with a nop at g_main - 4, and where the stand-ins of the kind held
    # read the two words of the guest they are given
    labels = soundlines.guest_labels(HERE)
    with open(os.path.join(HERE, 'dsptest.elf'), 'rb') as f:
        image = f.read()
    release = labels['G_MAIN'] - 4
    release_at = soundlines.file_offset(image, release, 'the release in front of g_main')
    reset_at = soundlines.file_offset(image, labels['G_ARM7_IMAGE'] + labels['A_RESET'], 'a_reset of the ARM7 program')
    reset_word, = struct.unpack_from('<I', image, reset_at)
    guests = {'elf': os.path.join(work, 'dsptest.elf'), 'nop': os.path.join(work, 'dsptest-nop.elf')}
    with open(guests['elf'], 'wb') as f:
        f.write(image)
    nop = bytearray(image)
    struct.pack_into('<H', nop, release_at, 0x0009)
    with open(guests['nop'], 'wb') as f:
        f.write(nop)

    failed = 0
    for n, (kind, arguments, code, text) in enumerate(CASES, 1):
        emulator = os.path.join(work, 'stand-in-' + kind)
        if not os.path.isfile(emulator):
            with open(emulator, 'w', encoding='utf-8') as f:
                f.write(STAND_IN % {'python': sys.executable, 'kind': kind, 'release_at': release_at, 'reset_at': reset_at, 'reset_word': reset_word})
            os.chmod(emulator, os.stat(emulator).st_mode | stat.S_IXUSR)

        arguments = [guests[a[1:-1]] if a[1:-1] in guests and a[0] == '{' else a for a in arguments]
        text = text.replace('%(release)08X', '%08X' % release)
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
