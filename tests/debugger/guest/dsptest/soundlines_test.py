#!/usr/bin/env python3
"""What soundlines.py makes of a run that was not made on the engine it was asked for, on stand-ins for the emulator;
what its modes held, heard and oracle make of the copies of the guest they build, on stand-ins whose line depends
on the guest (and for oracle on stand-ins for aica_dsp_guest_oracle); what its mode silent makes of a silence of
another length; what its mode release makes of a guest with no empty end in its table; and what its mode names
makes of the names of a build, on stand-ins for a build directory.

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
    held-regs, held-sh4,      another regs, sh4, dsp or samples for the copy whose ARM7 stays held: the mode names
    held-dsp, held-samples    the field
    held-silent               the silence of no pair for both copies: the copy held is silent
    held-deaf                 one samples for every guest: the copy idle sounds as the guest

and on a copy whose halfword at g_main - 4 is a nop, which the mode refuses to patch, naming the halfword. On
the kind asked both copies print one arm7: the patches did not take.

The mode heard runs a copy of the guest whose outputs of the DSP have EFSDL 0. Its stand-ins print one samples and
one regs for a guest whose table gives an output of the DSP a level and others for a guest whose table gives none
(the copy):

    heard                     that line: the DSP is heard, and the mode passes
    heard-arm7, heard-sh4,    another arm7, sh4, dsp or cycles for the copy: the mode names the field
    heard-dsp, heard-cycles
    heard-deaf                one samples for every guest: the mix does not take what the DSP outputs

and on a copy of the guest whose table gives no output a level, which the mode refuses.

The mode oracle runs a copy of the guest whose first channel has IMXL 0 and the tool on it (--oracle). Its
stand-ins print a dsp that depends on the IMXL the guest's table leaves the first channel at, the tool's
stand-ins (a script of each kind, oracle-<kind>) a dsp of their own:

    oracle and the tool       the dsp of the copy: the mode passes
    oracle-other              another dsp: the mode names the field and both values
    oracle-fails              exit code 1, as the tool when the model and the core differ: the mode fails with it
    oracle-silent             no line dsp=: the mode fails
    oracle-deaf (emulator)    one dsp for every guest: the comparison does not see the sends, and the mode says so

and on a copy of the guest whose table sets no ISEL / IMXL of its first channel, which the mode refuses.

The mode silent: on the kind silent-run, whose samples is the hash of N * 32 pairs of zeroes, it passes; on
silent-short, the hash of (N - 1) * 32 pairs, it fails with that k; on asked, which sounds, it fails. The mode
release on a copy of the guest whose last entry of the DSP part of the table is not empty: it fails, naming the
empty end it needs, before any run.

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

samples, regs, arm7, sh4, dsp = 0x1111, 0x2222, 0x3333, 0x4444 if engine == 'jit' else 0x5555, 0xeeee
cycles = 145124
basis, prime = 0xcbf29ce484222325, 0x100000001b3
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
    if KIND == 'held-sh4' and half != 0xE000:
        sh4 += 1
    if KIND == 'held-dsp' and half != 0xE000:
        dsp = 0xeeef
    if KIND == 'held-samples' and half != 0xE000:
        samples = 0x1112
    if KIND == 'held-deaf':
        samples = 0x6060       # the guest's FIQ handler is not heard: the guest sounds as the copy idle
    if KIND == 'held-silent' and not runs:
        samples = 0xcbf29ce484222325      # the copies are silent: the hash of no pair

if KIND.startswith('heard') or KIND.startswith('oracle'):
    # the line depends on the guest's table: for heard on whether it gives an output of the DSP a level (EFSDL, bits
    # 11:8 of 0x00702000 + 4 k), for oracle on the IMXL of the last entry for the first channel (0x00700020)
    with open(sys.argv[-1], 'rb') as f:
        image = f.read()
    table = [struct.unpack_from('<II', image, %(table_at)d + n) for n in range(0, %(table_len)d, 8)]
    heard = any(0xA0702000 <= a < 0xA0702040 and (v >> 8) & 15 for a, v in table)
    mix = [v for a, v in table if a == 0xA0700020]
    if KIND.startswith('heard'):
        if not heard:
            samples, regs = 0x7070, 0x2229
            arm7 += KIND == 'heard-arm7'
            sh4 += KIND == 'heard-sh4'
            dsp += KIND == 'heard-dsp'
            cycles += KIND == 'heard-cycles'
        if KIND == 'heard-deaf':
            samples = 0x7070
    elif KIND == 'oracle-deaf' or not mix or (mix[-1] >> 4) & 15:
        dsp = 0xeeee
    else:
        dsp = 0x0de0           # the dsp of the copy whose first channel sends nothing

if KIND != 'silent':
    print('Using MCPU ' + ('Recompiler' if engine == 'jit' else 'Interpreter'))
if session:
    print('SelfTest: stand-in: ok: the session is at work')
if line:
    ticks = int(keys['config:Debug.AicaHash'])
    if KIND in ('silent-run', 'silent-short'):
        # the hash of k * 32 pairs of zeroes: k = N, or one tick short
        samples = basis * pow(prime, 128 * (ticks - (KIND == 'silent-short')), 1 << 64) %% (1 << 64)
    print('AicaHash: samples=%%016x regs=%%016x arm7=%%016x sh4=%%016x dsp=%%016x ticks=%%d cycles=%%d'
          %% (samples, regs, arm7, sh4, dsp, ticks, cycles * ticks))
else:
    print('nulldc-exit: reason=vblanks vblanks=%%s hash=%%016x sh4=%%016x sched=%%016x ram=%%016x vram=%%016x aram=%%016x arm7=%%016x'
          %% (keys['config:Exit.AfterVblanks'], 0x6666, 0x7777 if engine == 'jit' else 0x8888, 0x9999, 0xaaaa, 0xbbbb, 0xcccc, 0xdddd))
'''

# A stand-in for aica_dsp_guest_oracle: it prints what the tool prints, with a dsp of its kind, and exits as the tool
ORACLE = r'''#!%(python)s
import sys

KIND = %(kind)r
args = dict(zip(sys.argv[1::2], sys.argv[2::2]))
assert '--elf' in args and int(args['--ticks']) > 0
print("the guest's DSP writes: a stand-in reads none")
if KIND == 'oracle-fails':
    print('the model and the core differ: sample 40 step 3: ACC is 1 in the model, 2 in the core')
    sys.exit(1)
if KIND != 'oracle-silent':
    print('dsp=%%016x' %% {'oracle-other': 0x0de1, 'oracle-deaf-tool': 0xeeee}.get(KIND, 0x0de0))
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

# the mode heard on a copy of the guest, and on a copy whose table gives no output of the DSP a level ({mute})
HEARD = ['--mode', 'heard', '--guest', '{elf}', '--dir', HERE]
HEARD_MUTE = ['--mode', 'heard', '--guest', '{mute}', '--dir', HERE]
HEARD_FIELD = 'FAIL: the copy whose outputs of the DSP have EFSDL 0 and the guest differ in '

# the mode oracle on a copy of the guest with a stand-in for the tool ({oracle-<kind>}), and on a copy whose table
# sets no ISEL / IMXL of its first channel ({nomix})
ORACLE_ARGS = ['--mode', 'oracle', '--guest', '{elf}', '--dir', HERE, '--oracle']
ORACLE_NOMIX = ['--mode', 'oracle', '--guest', '{nomix}', '--dir', HERE, '--oracle', '{oracle-tool}']
SILENT = ['--mode', 'silent', '--engine', 'jit']

# the mode release on a copy of the guest whose last entry of the DSP part of the table is filled ({full})
RELEASE_FULL = ['--mode', 'release', '--guest', '{full}', '--dir', HERE, '--fields', 'samples,regs,arm7,ticks,cycles', '--engine', 'jit']

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
    ('held-sh4', HELD + ['--engine', 'jit'], 1,
     'FAIL: the copy whose ARM7 stays held and the copy whose ARM7 idles on b . differ in sh4 (0000000000004445 and 0000000000004444)'),
    ('held-dsp', HELD + ['--engine', 'interp'], 1,
     'FAIL: the copy whose ARM7 stays held and the copy whose ARM7 idles on b . differ in dsp (000000000000eeef and 000000000000eeee)'),
    ('held-samples', HELD + ['--engine', 'jit'], 1,
     'FAIL: the copy whose ARM7 stays held and the copy whose ARM7 idles on b . differ in samples (0000000000001112 and 0000000000006060)'),
    ('held', HELD_NOP + ['--engine', 'jit'], 1, 'FAIL: the halfword at g_main - 4 (%(release)08X) of the guest is 0009, not E000 (mov #0, r0)'),
    ('asked', HELD + ['--engine', 'jit'], 1, 'FAIL: the copy held and the copy idle print one arm7 field, 0000000000003333'),
    ('held-silent', HELD + ['--engine', 'jit'], 1, 'FAIL: the copy whose ARM7 stays held is silent: samples=cbf29ce484222325 is the hash of 0 * 32 pairs'),
    ('held-deaf', HELD + ['--engine', 'jit'], 1, 'FAIL: the copy whose ARM7 idles on b . prints the samples of the guest, 0000000000006060'),
    ('interpreter', HELD + ['--engine', 'jit'], 1, 'FAIL: the run line-1 ' + ASKED_JIT),

    # heard: the line depends on whether the guest's table gives an output of the DSP a level
    ('heard', HEARD + ['--engine', 'jit'], 0, 'the DSP is heard: with EFSDL 0 for EFREG[0] (EFSDL 10), EFREG[1] (EFSDL 10) the copy prints the guest\'s arm7='),
    ('heard', HEARD + ['--engine', 'interp'], 0, 'and another samples'),
    ('heard-arm7', HEARD + ['--engine', 'jit'], 1, HEARD_FIELD + 'arm7 (0000000000003333 and 0000000000003334)'),
    ('heard-sh4', HEARD + ['--engine', 'jit'], 1, HEARD_FIELD + 'sh4 (0000000000004444 and 0000000000004445)'),
    ('heard-dsp', HEARD + ['--engine', 'interp'], 1, HEARD_FIELD + 'dsp (000000000000eeee and 000000000000eeef)'),
    ('heard-cycles', HEARD + ['--engine', 'jit'], 1, HEARD_FIELD + 'cycles (725620 and 725625)'),
    ('heard-deaf', HEARD + ['--engine', 'jit'], 1, 'FAIL: the copy whose outputs of the DSP have EFSDL 0 prints the samples of the guest, 0000000000007070'),
    ('heard', HEARD_MUTE + ['--engine', 'jit'], 1, 'FAIL: the table of the guest gives no output of the DSP a level'),

    # oracle: the emulator's dsp depends on the IMXL of the first channel, the tool's is its own
    ('oracle', ORACLE_ARGS + ['{oracle-tool}', '--engine', 'jit'], 0,
     'the copy with IMXL 0 for the first channel (ISEL 3): dsp=0000000000000de0, the DSP of the caique model'),
    ('oracle', ORACLE_ARGS + ['{oracle-tool}', '--engine', 'interp'], 0, 'and the guest, whose first channel sends, prints another: dsp=000000000000eeee'),
    ('oracle', ORACLE_ARGS + ['{oracle-other}', '--engine', 'jit'], 1,
     'FAIL: the DSP of the copy of the guest whose sends are 0 is not the DSP of the caique model: the line has dsp=0000000000000de0, aica_dsp_guest_oracle computes dsp=0000000000000de1'),
    ('oracle', ORACLE_ARGS + ['{oracle-fails}', '--engine', 'jit'], 1, 'FAIL: aica_dsp_guest_oracle ended with exit code 1'),
    ('oracle', ORACLE_ARGS + ['{oracle-silent}', '--engine', 'jit'], 1, 'FAIL: aica_dsp_guest_oracle printed no line dsp=<hex>, or more than one'),
    ('oracle-deaf', ORACLE_ARGS + ['{oracle-other}', '--engine', 'jit'], 1, 'FAIL: the DSP of the copy of the guest whose sends are 0 is not the DSP of the caique model'),
    ('oracle-deaf', ORACLE_ARGS + ['{oracle-deaf-tool}', '--engine', 'jit'], 1,
     'FAIL: the guest, whose first channel sends to the DSP, prints the dsp of the copy whose sends are 0, 000000000000eeee: the comparison does not see the sends'),
    ('oracle', ORACLE_NOMIX + ['--engine', 'jit'], 1, 'FAIL: the table of the guest does not set ISEL / IMXL of its first channel (0x00700020)'),

    # silent: the hash of N * 32 pairs of zeroes, and of another k
    ('silent-run', SILENT, 0, 'silent: samples='),
    ('silent-short', SILENT, 1, 'FAIL: the run handed the audio stream 4 * 32 pairs of zeroes in 5 ticks, not 5 * 32'),
    ('asked', SILENT, 1, 'FAIL: the run is not silent: samples=0000000000001111'),

    # release: the empty end of the DSP part of the table
    ('asked', RELEASE_FULL, 1, 'FAIL: the DSP part of the table of the guest (g_table_dsp to g_table_dsp_end) has 0 empty entries at its end, not the 3'),
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
    table_at = soundlines.file_offset(image, labels['G_TABLE'], 'the table')
    table_len = labels['G_TABLE_END'] - labels['G_TABLE']

    # copies of the guest: a nop at g_main - 4; no output of the DSP with a level; no ISEL / IMXL of the first
    # channel; the last entry of the DSP part of the table filled (with a write to a word of the wave memory)
    nop = bytearray(image)
    struct.pack_into('<H', nop, release_at, 0x0009)
    mute, nomix = bytearray(image), bytearray(image)
    for n in range(0, table_len, 8):
        address, value = struct.unpack_from('<II', image, table_at + n)
        if 0xA0702000 <= address < 0xA0702040:
            struct.pack_into('<I', mute, table_at + n + 4, value & ~0xF00)
        if address == 0xA0700020:
            struct.pack_into('<II', nomix, table_at + n, 0, 0)
    full = bytearray(image)
    struct.pack_into('<II', full, soundlines.file_offset(image, labels['G_TABLE_DSP_END'] - 8, 'the table'), 0xA0810100, 0)
    guests = {}
    for name, data in (('elf', image), ('nop', nop), ('mute', mute), ('nomix', nomix), ('full', full)):
        guests[name] = os.path.join(work, 'dsptest-%s.elf' % name)
        with open(guests[name], 'wb') as f:
            f.write(data)

    # the stand-ins for aica_dsp_guest_oracle
    for kind in ('oracle-tool', 'oracle-other', 'oracle-fails', 'oracle-silent', 'oracle-deaf-tool'):
        guests[kind] = os.path.join(work, 'stand-in-' + kind)
        with open(guests[kind], 'w', encoding='utf-8') as f:
            f.write(ORACLE % {'python': sys.executable, 'kind': kind})
        os.chmod(guests[kind], os.stat(guests[kind]).st_mode | stat.S_IXUSR)

    failed = 0
    for n, (kind, arguments, code, text) in enumerate(CASES, 1):
        emulator = os.path.join(work, 'stand-in-' + kind)
        if not os.path.isfile(emulator):
            with open(emulator, 'w', encoding='utf-8') as f:
                f.write(STAND_IN % {'python': sys.executable, 'kind': kind, 'release_at': release_at, 'reset_at': reset_at, 'reset_word': reset_word,
                                    'table_at': table_at, 'table_len': table_len})
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
