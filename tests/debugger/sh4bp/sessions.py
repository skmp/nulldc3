#!/usr/bin/env python3
"""The sh4bp selftest of the emulator in sessions on three engines, and what the sessions have to agree on.

    sessions.py <nulldc3> <elf> [work dir] [config item ...]
    sessions.py --rule

Runs the sh4bp selftest of the emulator (libswirl/debugger/dbg_selftest_sh4bp.cpp) in three sessions: the SH4 on
the JIT, on the interpreter, and on the JIT with the debugger told to use the interpreter fallback. Every
session has to end with exit code 0. What the sessions print as "note: cmp <key> <value>" is then compared:

  b1.*  b4.*  b7.end     registers and step traces: the JIT's and the fallback's are the interpreter's
  b2.time.blocks  b3.time.blocks  b12.blocks
                         time, and for b12 the state of the integer workload: the fallback's are the JIT's

Then the gates on the booted guest, for the same three and for the JIT and the fallback with idleskip off: a
session of the tests sh4bpboot (a run for every case) and sessions of their twins sh4bpbase (the same runs
with nothing of the cases in them). What they print for a run, "note: cmp boot.<gate>.<case> <state>", has
to be equal:

  two sessions of sh4bpbase agree; when they do not a third is run, and two of the three have to
  the session of sh4bpboot is theirs; when it is not it is run twice more, and fails when it differs again

How the baselines of the fallback compare with the JIT's is printed (B12): their FPU arithmetic differs.

Every session is headless (config:Headless=yes), with no audio device (audio:backend=none) and the real-time clock
pinned (the guest reads the clock), and keeps its files in a directory of its own under the work dir
(--portable). It runs as the harnesses run the emulator (tests/common/emurun.py): an environment with nothing of
the caller's but the options of the sanitizers (no display: a binary that does not know the key ends with an error
instead of opening a window), a core file size limit of 1 byte, and on Windows SystemRoot and TEMP and TMP of its
directory and nulldc3-winrun around it. A session that has not ended after 1800 s is ended and fails. The config
items are given to every session as well, section:key=value each, behind the script's own.
Exit code 0 when all of it held; 2 for a wrong command line.

This is the port of the bash script selftest.sh that this directory had (design 2.3.6): the same sessions, the
same keys, the same comparisons and the same rule of the repeats, and its lines. --rule runs those comparisons on
sessions that are made up, no emulator is run: every key the comparisons hold, changed in one session, has to fail
the run, every key they do not hold has to leave it alone, and the repeats have to go as the rule says.
"""

import os
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

# seconds a session may take
WATCHDOG = 1800

# What the sessions of the three engines have to agree on: the registers and step traces of the JIT and of the
# fallback are the interpreter's, the time (and for b12 the state) of the fallback the JIT's
REGISTERS = r'b1\.[^ ]*|b4\.[^ ]*|b7\.end'
TIME = r'b2\.time\.blocks|b3\.time\.blocks|b12\.blocks'

CMP_LINE = re.compile(r'^SelfTest: .*: note: cmp (.*)$')
SUMMARY = re.compile(r'^SelfTest: .* passed, .* failed')
FAILED = re.compile(r'SelfTest: .*FAIL')


class Sessions:
    """The sessions of one run of the script and what is compared of them. run(name, items) makes a session:
    (exit code or None when it did not end, its output)."""

    def __init__(self, run, say):
        self.run = run
        self.say = say
        self.cmp = {}
        self.fail = False

    def session(self, name, *items):
        code, text = self.run(name, list(items))
        lines = text.splitlines()

        if code is None:
            self.say('FAIL %s: the session did not end' % name)
            self.fail = True
            return

        summaries = [line for line in lines if SUMMARY.match(line)]
        last = summaries[-1] if summaries else ''

        if code == 0:
            self.say('ok   %s: %s' % (name, last))
        else:
            self.say('FAIL %s: exit code %d; %s' % (name, code, last))
            for line in [line for line in lines if FAILED.search(line)][:20]:
                self.say(line)
            self.fail = True

        # "note: cmp " behind the last ": " of a line of the session that has one, sorted (sed and sort)
        self.cmp[name] = sorted(CMP_LINE.match(line).group(1) for line in lines if CMP_LINE.match(line))

    def keys(self, name, pattern):
        # grep -E "^(pattern) " of the session's values; a session that did not end has none
        rx = re.compile('^(%s) ' % pattern)
        return [line for line in self.cmp.get(name, []) if rx.match(line)]

    def compare(self, a, b, pattern):
        ka, kb = self.keys(a, pattern), self.keys(b, pattern)

        if ka and ka == kb:
            self.say('ok   %s = %s: %d values (%s)' % (a, b, len(ka), pattern))
        else:
            self.say('FAIL %s = %s (%s)' % (a, b, pattern))
            self.diff(ka, kb)
            self.fail = True

    def boot(self, name):
        # grep '^boot\.' of the session's values
        return [line for line in self.cmp.get(name, []) if line.startswith('boot.')]

    def same(self, a, b):
        """Both printed the same states of the booted guest, and printed some."""
        ka, kb = self.boot(a), self.boot(b)
        return bool(ka) and ka == kb

    def diff(self, a, b, most=20):
        """What diff <(a) <(b) | head prints of two lists of lines, about."""
        sa, sb = set(a), set(b)
        out = ['< ' + line for line in a if line not in sb] + ['> ' + line for line in b if line not in sa]
        for line in out[:most]:
            self.say(line)

    def booted(self, name, *items):
        """The gates on the booted guest in this configuration; <name>-base is left as the baseline that two
        sessions agreed on."""
        self.session(name + '-boot', *(items + (',', 'Debugger:SelfTest=sh4bpboot')))
        self.session(name + '-base', *(items + (',', 'Debugger:SelfTest=sh4bpbase')))
        self.session(name + '-base2', *(items + (',', 'Debugger:SelfTest=sh4bpbase')))

        if not self.same(name + '-base', name + '-base2'):
            self.say('note %s: two sessions of baselines differ, a third decides' % name)
            self.diff(self.boot(name + '-base'), self.boot(name + '-base2'), 6)
            self.session(name + '-base3', *(items + (',', 'Debugger:SelfTest=sh4bpbase')))

            if self.same(name + '-base2', name + '-base3'):
                self.cmp[name + '-base'] = list(self.cmp.get(name + '-base2', []))
            elif not self.same(name + '-base', name + '-base3'):
                self.say('FAIL %s: no two of three sessions of baselines agree on the booted guest' % name)
                self.fail = True
                return

        if self.same(name + '-boot', name + '-base'):
            self.say('ok   %s: %d runs of the booted guest, each in the state of its baseline' % (name, len(self.boot(name + '-base'))))
            return

        self.say('note %s: the cases differ from the baselines, they are run twice more' % name)
        self.diff(self.boot(name + '-boot'), self.boot(name + '-base'), 6)

        self.session(name + '-boot2', *(items + (',', 'Debugger:SelfTest=sh4bpboot')))
        self.session(name + '-boot3', *(items + (',', 'Debugger:SelfTest=sh4bpboot')))

        if self.same(name + '-boot2', name + '-base') and self.same(name + '-boot3', name + '-base'):
            self.say('ok   %s: %d runs of the booted guest, each in the state of its baseline; one session differed and two '
                     'repetitions did not' % (name, len(self.boot(name + '-base'))))
        else:
            self.say('FAIL %s: the booted guest with the cases is not in the state of its baseline' % name)
            self.diff(self.boot(name + '-boot2'), self.boot(name + '-base'))
            self.diff(self.boot(name + '-boot3'), self.boot(name + '-base'))
            self.fail = True

    def all(self):
        """Everything of the script, in its order. True when all of it held."""
        self.session('jit')
        self.session('interpreter', ',', 'config:Dynarec.Enabled=no')
        self.session('fallback', ',', 'Debugger:ForceFallback=yes')

        self.compare('jit', 'interpreter', REGISTERS)
        self.compare('fallback', 'interpreter', REGISTERS)
        self.compare('fallback', 'jit', TIME)

        self.booted('jit')
        self.booted('interpreter', ',', 'config:Dynarec.Enabled=no')
        self.booted('fallback', ',', 'Debugger:ForceFallback=yes')
        self.booted('jit-idleskip-off', ',', 'config:Dynarec.idleskip=no')
        self.booted('fallback-idleskip-off', ',', 'Debugger:ForceFallback=yes', ',', 'config:Dynarec.idleskip=no')

        # B12 on the booted guest: printed, not failed
        for a, b in (('fallback', 'jit'), ('fallback-idleskip-off', 'jit-idleskip-off')):
            ka, kb = self.boot(a + '-base'), self.boot(b + '-base')
            if ka and kb:
                differ = len([line for line in ka if line not in set(kb)])
                self.say('note %s against %s: %d of %d baselines of the booted guest differ' % (a, b, differ, len(ka)))

        return not self.fail


def emulator_runner(binary, elf, work, more):
    """run() of Sessions with the emulator: <work>/<name>.log and <work>/<name>.cmp as the script left them."""

    def run(name, items):
        directory = os.path.join(work, name)
        shutil.rmtree(directory, ignore_errors=True)
        os.makedirs(os.path.join(directory, 'portable'))

        command = [binary, '--portable', os.path.join(directory, 'portable'), '--config', 'config:Headless=yes', ',',
                   'audio:backend=none', ',', 'config:Dreamcast.RTC=%d' % RTC_PIN, ',', 'Debugger:Port=0', ',',
                   'Debugger:SelfTest=sh4bp', ',', 'Debugger:StartPaused=yes'] + more + items + [elf]
        log_path = os.path.join(work, name + '.log')
        with open(log_path, 'wb') as log:
            process = emurun.start(command, WATCHDOG, cwd=directory, env=emurun.environment(directory), stdout=log,
                                   stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=WATCHDOG)
            except subprocess.TimeoutExpired:
                emurun.kill(process)
                code = None
        text = emurun.read_text(log_path)

        if code is not None:
            values = sorted(CMP_LINE.match(line).group(1) for line in text.splitlines() if CMP_LINE.match(line))
            with open(os.path.join(work, name + '.cmp'), 'w', encoding='utf-8', newline='\n') as f:
                f.write(''.join(value + '\n' for value in values))
        return code, text

    return run


# --rule: the comparisons on made-up sessions

# The values a made-up session prints, by key: what every engine agrees on. Each key the script compares between two
# sessions, and one that it compares nowhere
RULE_VALUES = {
    'b1.first': 'pc=8c010000 r0=1',
    'b1.slot': 'pc=8c010010 r1=2',
    'b4.trace': '1a2b3c4d',
    'b7.end': 'pc=8c010040',
    'b2.time.blocks': '123456',
    'b3.time.blocks': '654321',
    'b12.blocks': 'sched=99 hash=abcd',
    'b2.time.steps': '777',
}

# what the script holds of those: (key, the session whose value is changed, the session it is compared with)
RULE_HELD = [
    ('b1.first', 'jit', 'interpreter'), ('b1.slot', 'jit', 'interpreter'), ('b4.trace', 'jit', 'interpreter'),
    ('b7.end', 'jit', 'interpreter'),
    ('b1.first', 'fallback', 'interpreter'), ('b1.slot', 'fallback', 'interpreter'), ('b4.trace', 'fallback', 'interpreter'),
    ('b7.end', 'fallback', 'interpreter'),
    ('b2.time.blocks', 'fallback', 'jit'), ('b3.time.blocks', 'fallback', 'jit'), ('b12.blocks', 'fallback', 'jit'),
]

# and what it does not hold: the time of the JIT against the interpreter's, the registers of the fallback against
# the JIT's (both are the interpreter's), a key no comparison names
RULE_FREE = [
    ('b2.time.blocks', 'interpreter'), ('b3.time.blocks', 'interpreter'), ('b12.blocks', 'interpreter'),
    ('b2.time.steps', 'jit'), ('b2.time.steps', 'fallback'),
]

RULE_BOOT = ['boot.b3.armed 1111', 'boot.b6.hot 2222', 'boot.b7.out 3333']


def rule_session(changes):
    """run() of Sessions on made-up sessions. changes: {name: {key: value}} for the sessions of the engines (a key
    of the booted guest too), and {name: code} under 'code' for the exit codes."""

    def run(name, items):
        values = dict(RULE_VALUES)
        tests = 'sh4bp'
        for item in items:
            if item.startswith('Debugger:SelfTest='):
                tests = item.split('=', 1)[1]
        lines = []
        if tests == 'sh4bp':
            values.update(changes.get(name, {}))
            lines = ['SelfTest: %s.made.up: note: cmp %s %s' % (key.split('.')[0], key, value)
                     for key, value in sorted(values.items()) if value is not None]
        else:
            boot = dict(line.split(' ', 1) for line in RULE_BOOT)
            boot.update(changes.get(name, {}))
            lines = ['SelfTest: %s.made.up: note: cmp %s %s' % (tests, key, value) for key, value in sorted(boot.items()) if value is not None]
        code = changes.get('code', {}).get(name, 0)
        lines.append('SelfTest: %d passed, %d failed' % (len(lines), 0 if code == 0 else 1))
        return code, '\n'.join(lines) + '\n'

    return run


def rule():
    cases = []

    def case(what, changes, passes, must_say=()):
        said = []
        ok = Sessions(rule_session(changes), said.append).all()
        text = '\n'.join(said)
        good = ok == passes and all(s in text for s in must_say)
        cases.append((what, good, text))

    case('every session the same', {}, True, ['ok   jit = interpreter: 4 values', 'ok   fallback = jit: 3 values'])
    for key, changed, other in RULE_HELD:
        case('%s of the session %s against %s' % (key, changed, other), {changed: {key: 'changed'}}, False,
             ['FAIL %s = %s' % (changed, other)])
    for key, changed in RULE_FREE:
        case('%s of the session %s, which nothing compares there' % (key, changed), {changed: {key: 'changed'}}, True)
    case('a key missing in the session jit', {'jit': {'b7.end': None}}, False, ['FAIL jit = interpreter'])
    case('a session with exit code 1', {'code': {'interpreter': 1}}, False, ['FAIL interpreter: exit code 1'])
    case('a session of cases with exit code 1', {'code': {'jit-boot': 1}}, False, ['FAIL jit-boot: exit code 1'])

    # the repeats on the booted guest
    case('one session of baselines differs, the third agrees with the second', {'jit-base': {'boot.b6.hot': 'x'}}, True,
         ['a third decides', 'ok   jit: 3 runs of the booted guest'])
    case('one session of baselines differs, the third agrees with the first', {'jit-base2': {'boot.b6.hot': 'x'}}, True,
         ['a third decides', 'ok   jit: 3 runs of the booted guest'])
    case('no two of three sessions of baselines agree', {'jit-base': {'boot.b6.hot': 'x'}, 'jit-base2': {'boot.b6.hot': 'y'}}, False,
         ['FAIL jit: no two of three'])
    case('the cases differ once, the two repetitions do not', {'interpreter-boot': {'boot.b3.armed': 'x'}}, True,
         ['they are run twice more', 'one session differed and two repetitions did not'])
    case('the cases differ again in a repetition', {'fallback-boot': {'boot.b3.armed': 'x'}, 'fallback-boot3': {'boot.b3.armed': 'x'}}, False,
         ['FAIL fallback: the booted guest with the cases'])
    case('the cases differ with idleskip off', {'jit-idleskip-off-boot': {'boot.b7.out': 'x'}, 'jit-idleskip-off-boot2': {'boot.b7.out': 'x'}},
         False, ['FAIL jit-idleskip-off: the booted guest'])
    case('the baselines of the fallback and of the JIT differ (B12): printed, not failed', {'fallback-base': {'boot.b7.out': 'x'},
         'fallback-base2': {'boot.b7.out': 'x'}, 'fallback-boot': {'boot.b7.out': 'x'}}, True,
         ['note fallback against jit: 1 of 3 baselines of the booted guest differ'])
    case('a session of the booted guest prints no state', {'fallback-idleskip-off-base': {'boot.b3.armed': None, 'boot.b6.hot': None,
         'boot.b7.out': None}, 'fallback-idleskip-off-base2': {'boot.b3.armed': None, 'boot.b6.hot': None, 'boot.b7.out': None},
         'fallback-idleskip-off-base3': {'boot.b3.armed': None, 'boot.b6.hot': None, 'boot.b7.out': None}}, False,
         ['FAIL fallback-idleskip-off'])

    bad = [(what, text) for what, good, text in cases if not good]
    for what, text in bad:
        print('FAIL: %s: the comparisons did not take it as they have to:\n%s' % (what, text))
    print('%d made-up runs of the sessions, %d taken as the comparisons of the script take them' % (len(cases), len(cases) - len(bad)))
    return not bad


def main(argv):
    if argv[1:] == ['--rule']:
        ok = rule()
        print('sh4bp sessions rule: PASS' if ok else 'sh4bp sessions rule: FAIL')
        return 0 if ok else 1

    if len(argv) < 3:
        print('usage: sessions.py <nulldc3> <elf> [work dir] [config item ...]')
        return 2

    # nulldc3 is nulldc3.exe on Windows
    binary = emurun.executable(os.path.abspath(argv[1]))
    elf = os.path.abspath(argv[2])
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK) or not os.path.isfile(elf):
        print('usage: sessions.py <nulldc3> <elf> [work dir] [config item ...]')
        return 2

    work = os.path.abspath(argv[3]) if len(argv) > 3 else tempfile.mkdtemp()
    more = []
    for item in argv[4:]:
        more += [',', item]
    os.makedirs(work, exist_ok=True)

    def say(line):
        print(line)
        sys.stdout.flush()

    ok = Sessions(emulator_runner(binary, elf, work, more), say).all()
    print('sh4bp selftest: PASS' if ok else 'sh4bp selftest: FAIL, logs in %s' % work)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
