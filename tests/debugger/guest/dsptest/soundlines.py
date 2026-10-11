#!/usr/bin/env python3
"""The sound lines of the emulator: runs with the sound regression harness, and what they have to agree on.

    soundlines.py --emulator <devcast> --work <dir> --guest <program> --ticks <N> --mode <mode> [--mode <mode>]...
                  [--engine jit|interp] [--render batch|persample] [--fields <field,...>] [--file <path>]
                  [--vblanks <V>] [--selftest <filter>] [--expect <text>]... [--config <section:key=value>]...
    soundlines.py --mode files [--dir <the directory of the guest>]

A run is the emulator on the program, headless, with no audio device (audio:backend=none), the real-time clock
pinned, the SH4 on the recompiler or on the interpreter (--engine) and the sound rendered in batches of 32
samples or sample by sample (--render: config:aica.NoBatch no or yes), in an environment of its own: fresh HOME,
XDG_CONFIG_HOME and XDG_DATA_HOME under the work directory, no display, a core file size limit of 1 byte, and a
watchdog. A process this script starts is ended by its pid. There are two kinds of run:

    the line        config:Debug.AicaHash=<N>: after N ticks of the AICA (a tick is 32 samples) the emulator prints
                        AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> ticks=<N> cycles=<decimal>
                    and ends the process, with no exit line (docs/nulldc3/automated-runs.md). The line of every
                    run has to have ticks=<N>
    the exit line   config:Exit.AfterVblanks=<V> , config:Exit.StateHash=yes in place of the harness key: the
                    state hash of the same program, which has what the line has no field for (wave memory, main
                    memory, the scheduler)

README.md has the modes:

    same      two runs with the same keys print the same line, field for field
    sounds    samples is none of the hashes of silence. The hash of k * 32 pairs of zeroes is one value for each
              k, and a silent run prints one of them for a k from 0 to N: N in batches, fewer sample by sample,
              where no pair exists while the ARM7 is held in reset
    engines   the line of the recompiler and the line of the interpreter agree in the fields of --fields
    record    the lines (and with --vblanks the exit lines) of the four configurations, or of the one that
              --engine and --render name, into the file of --file, which is outside of the tree: a line is good
              for one build on one host. Every run is made --runs times and has to give one line. Entries of
              other programs and of other N or V stay in the file
    compare   the same runs, against the entries of that file: every line and every part of the state hash
    neutral   the debugger in use changes nothing: a run with -config Debugger:SelfTest=<filter> prints the line
              of the run without it, and ends in its state hash, part for part, at --vblanks; and the session did
              something (see mode_neutral)
    files     the committed files of the guest are those of its manifest, SHA256SUMS; and where the assemblers
              are, gen.sh --check: they are what the sources give
"""

import argparse
import hashlib
import os
import re
import resource
import shutil
import subprocess
import sys

RTC_PIN = 946684800

BAD = ('Fatal error', 'Verify Failed', 'fault_handler', 'AddressSanitizer', 'ThreadSanitizer', 'runtime error:')

LINE_FIELDS = ('samples', 'regs', 'arm7', 'sh4', 'ticks', 'cycles')

# the parts of the state hash, as the mode same-hash of tests/debugger/selftest/selftest.py compares them
HASH_FIELDS = ('reason', 'vblanks', 'hash', 'sh4', 'sched', 'ram', 'vram', 'aram', 'arm7')

ENGINES = ('jit', 'interp')
RENDERS = ('batch', 'persample')

# what SHA256SUMS lists, in this order, and nothing else
MANIFEST = ('dsptest.s', 'arm7.s', 'gen.sh', 'dsptest.elf', 'arm7.bin', 'dsptest_labels.inc')

FNV_BASIS = 0xcbf29ce484222325
FNV_PRIME = 0x100000001b3


class Failed(Exception):
    pass


def check(ok, what, log=None):
    if not ok:
        tail = ''
        if log is not None:
            tail = '\n--- the end of the output ---\n' + '\n'.join(log.splitlines()[-40:])
        raise Failed(what + tail)


class Run:
    """One run of the emulator. Its output goes to a file of the work directory."""

    def __init__(self, args, name, engine, render):
        self.args = args
        self.name = name
        self.engine = engine
        self.render = render
        self.dir = os.path.join(args.work, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        for sub in ('home', 'cfg', os.path.join('data', 'reicast', 'data')):
            os.makedirs(os.path.join(self.dir, sub))
        self.log_path = os.path.join(self.dir, 'log.txt')
        self.log = ''
        self.code = None

    def command(self, keys):
        items = ['config:Headless=yes', 'audio:backend=none', 'config:Dreamcast.RTC=%d' % RTC_PIN,
                 'config:Dynarec.Enabled=' + ('yes' if self.engine == 'jit' else 'no'),
                 'config:aica.NoBatch=' + ('yes' if self.render == 'persample' else 'no')]
        items += keys + self.args.config
        command = [self.args.emulator, '-config']
        for i, item in enumerate(items):
            if i:
                command.append(',')
            command.append(item)
        command.append(self.args.guest)
        return command

    def run(self, keys):
        env = {'HOME': os.path.join(self.dir, 'home'), 'XDG_CONFIG_HOME': os.path.join(self.dir, 'cfg'),
               'XDG_DATA_HOME': os.path.join(self.dir, 'data')}
        for name in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS', 'LSAN_OPTIONS'):
            if name in os.environ:
                env[name] = os.environ[name]

        def limits():
            # a crash is not piped to a core handler (WSL2 runs it for any other limit)
            resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))

        self.cmdline = self.command(keys)
        with open(self.log_path, 'wb') as out:
            process = subprocess.Popen(self.cmdline, cwd=self.dir, env=env, stdin=subprocess.DEVNULL, stdout=out,
                                       stderr=subprocess.STDOUT, preexec_fn=limits)
            try:
                self.code = process.wait(timeout=self.args.watchdog)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        with open(self.log_path, 'rb') as f:
            self.log = f.read().decode('utf-8', 'replace')
        check(self.code is not None, 'the run did not end within %d s: %s' % (self.args.watchdog, ' '.join(self.cmdline)), self.log)
        for bad in BAD:
            check(bad not in self.log, 'the output has "%s"' % bad, self.log)
        return self


def sound_line(log):
    """The harness line of a run as (the text, its fields), or None when the run printed none, or more than one."""
    lines = [line for line in log.splitlines() if line.startswith('AicaHash: ')]
    if len(lines) != 1:
        return None
    fields = dict(pair.split('=', 1) for pair in lines[0][len('AicaHash: '):].split(' ') if '=' in pair)
    if tuple(fields) != LINE_FIELDS:
        return None
    return lines[0], fields


def exit_line(log):
    lines = [line for line in log.splitlines() if line.startswith('nulldc-exit: ')]
    if not lines:
        return None
    return dict(pair.split('=', 1) for pair in lines[-1][len('nulldc-exit: '):].split(' ') if '=' in pair)


def line_run(args, name, engine, render, more=()):
    """A run that the harness ends: the Run, the text of its line and the fields."""
    run = Run(args, name, engine, render).run(['config:Debug.AicaHash=%d' % args.ticks] + list(more))
    found = sound_line(run.log)
    check(found is not None, 'the run %s printed no line of the sound harness, or more than one' % name, run.log)
    check(run.code == 0, 'the run %s ended with exit code %s' % (name, run.code), run.log)
    text, fields = found
    check(fields['ticks'] == str(args.ticks), 'the line of the run %s has ticks=%s, not %d: %s' % (name, fields['ticks'], args.ticks, text), run.log)
    check(exit_line(run.log) is None, 'the run %s printed an exit line: the harness ends the process without one' % name, run.log)
    return run, text, fields


def hash_run(args, name, engine, render, more=()):
    """A run that config:Exit.AfterVblanks ends: the Run and the fields of its exit line."""
    run = Run(args, name, engine, render).run(['config:Exit.AfterVblanks=%d' % args.vblanks, 'config:Exit.StateHash=yes'] + list(more))
    line = exit_line(run.log)
    check(line is not None and 'hash' in line, 'the run %s printed no exit line with a state hash' % name, run.log)
    check(run.code == 0, 'the run %s ended with exit code %s' % (name, run.code), run.log)
    check(line.get('reason') == 'vblanks' and line.get('vblanks') == str(args.vblanks),
          'the run %s ended with reason=%s after %s vblanks, not at vblank %d' % (name, line.get('reason'), line.get('vblanks'), args.vblanks), run.log)
    return run, line


def different(a, b, fields):
    return ', '.join('%s (%s and %s)' % (f, a.get(f), b.get(f)) for f in fields if a.get(f) != b.get(f))


def silence(ticks):
    """The samples field of a run that only ever handed pairs of zeroes to the audio stream: FNV-1a over zero bytes
    only multiplies, so the hash of k * 32 pairs (128 bytes each) is basis * prime^(128 k)."""
    step = pow(FNV_PRIME, 128, 1 << 64)
    value = FNV_BASIS
    values = {}
    for k in range(ticks + 1):
        values['%016x' % value] = k
        value = value * step % (1 << 64)
    return values


class Lines:
    """The lines of one configuration, each run made once."""

    def __init__(self, args):
        self.args = args
        self.made = []

    def get(self, count):
        while len(self.made) < count:
            self.made.append(line_run(self.args, 'line-%d' % (len(self.made) + 1), self.args.engine, self.args.render))
        return self.made[:count]


def mode_same(args, lines):
    (first, text, fields), (second, other_text, other) = lines.get(2)
    check(text == other_text, 'two runs of one binary with the same keys print two lines: they differ in '
          + different(fields, other, LINE_FIELDS) + '\n' + text + '\n' + other_text, second.log)
    print('two runs, one line: ' + text)


def mode_sounds(args, lines):
    (run, text, fields), = lines.get(1)
    k = silence(args.ticks).get(fields['samples'])
    check(k is None, 'the run is silent: samples=%s is the hash of %d * 32 pairs of zeroes' % (fields['samples'], k if k is not None else 0), run.log)
    print('it sounds: samples=%s is none of the %d hashes of silence' % (fields['samples'], args.ticks + 1))


def mode_engines(args, lines):
    names = [f for f in args.fields.split(',') if f]
    check(names and all(f in LINE_FIELDS for f in names), '--fields takes names of ' + ', '.join(LINE_FIELDS))
    jit, jit_text, jit_fields = line_run(args, 'jit', 'jit', args.render)
    interp, interp_text, interp_fields = line_run(args, 'interp', 'interp', args.render)
    check(not different(jit_fields, interp_fields, names), 'the line of the recompiler and the line of the interpreter differ in '
          + different(jit_fields, interp_fields, names) + '\n' + jit_text + '\n' + interp_text, interp.log)
    print('the recompiler and the interpreter agree in ' + ' '.join('%s=%s' % (f, jit_fields[f]) for f in names))


def configurations(args):
    return [(engine, render) for engine in ENGINES for render in RENDERS
            if args.engine in (None, engine) and args.render in (None, render)]


def entries(args):
    """What record stores and compare looks at: key -> text, for every configuration that is asked for."""
    guest = os.path.basename(args.guest)
    found = {}
    for engine, render in configurations(args):
        texts = set()
        for n in range(args.runs):
            run, text, fields = line_run(args, 'line-%s-%s-%d' % (engine, render, n + 1), engine, render)
            texts.add(text)
        check(len(texts) == 1, '%d runs on %s, %s print %d lines:\n%s' % (args.runs, engine, render, len(texts), '\n'.join(sorted(texts))))
        found['%s ticks=%d %s %s' % (guest, args.ticks, engine, render)] = texts.pop()
        if args.vblanks:
            texts = set()
            for n in range(args.runs):
                run, line = hash_run(args, 'hash-%s-%s-%d' % (engine, render, n + 1), engine, render)
                texts.add('nulldc-exit: ' + ' '.join('%s=%s' % (f, line.get(f)) for f in HASH_FIELDS))
            check(len(texts) == 1, '%d runs on %s, %s end in %d state hashes:\n%s' % (args.runs, engine, render, len(texts), '\n'.join(sorted(texts))))
            found['%s vblanks=%d %s %s' % (guest, args.vblanks, engine, render)] = texts.pop()
    return found


def read_file(path):
    stored = {}
    comments = []
    if os.path.isfile(path):
        with open(path, 'r', encoding='utf-8') as f:
            for line in f.read().splitlines():
                if line.startswith('#') or not line.strip():
                    comments.append(line)
                elif ' | ' in line:
                    key, text = line.split(' | ', 1)
                    stored[key] = text
    return stored, comments


def need_file(args):
    check(args.file, 'the mode needs --file')
    path = os.path.abspath(args.file)
    # tests/debugger/guest/dsptest/soundlines.py: the tests of the tree
    tests = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    check(os.path.commonpath([path, tests]) != tests, 'no line is stored in the tree: --file is among its tests, in ' + tests)
    return path


def mode_record(args, lines):
    path = need_file(args)
    found = entries(args)
    stored, comments = read_file(path)
    stored.update(found)
    if not comments:
        comments = ['# Sound lines and state hashes, recorded by soundlines.py: <program> <ticks=N or vblanks=V> <engine> <render> | <line>.',
                    '# A line is good for one build on one host.']
    for note in args.note:
        if '# ' + note not in comments:
            comments.append('# ' + note)
    with open(path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(comments + ['%s | %s' % (key, stored[key]) for key in sorted(stored)]) + '\n')
    for key in sorted(found):
        print('%s | %s' % (key, found[key]))
    print('%d entries recorded in %s, which has %d' % (len(found), path, len(stored)))


def mode_compare(args, lines):
    path = need_file(args)
    check(os.path.isfile(path), 'there is no file ' + path)
    stored, comments = read_file(path)
    found = entries(args)
    wrong = []
    for key in sorted(found):
        if key not in stored:
            wrong.append('%s: not in the file' % key)
        elif stored[key] != found[key]:
            wrong.append('%s:\n    the file: %s\n    this run: %s' % (key, stored[key], found[key]))
    check(not wrong, 'the runs differ from %s:\n%s' % (path, '\n'.join(wrong)))
    for key in sorted(found):
        print('%s | %s' % (key, found[key]))
    print('%d entries, each as %s has it' % (len(found), path))


SUMMARY = re.compile(r'^SelfTest: \d+ passed, \d+ failed$')


def session_did(args, run, what):
    """What a run with a session has to show: its test ran and said what it did, nothing failed, and it did not end
    the run. A session of this mode is one whose test never returns true: the driver prints its summary and ends
    the process when the last test of a session has returned, so a summary means that the run was not left to the
    harness, or to the exit key."""
    said = [line for line in run.log.splitlines() if line.startswith('SelfTest: ')]
    failed = [line for line in said if ': FAILED: ' in line or ': FAIL, ' in line]
    check(not failed, 'checks of the session failed in the run for %s:\n%s' % (what, '\n'.join(failed[:20])), run.log)
    summary = [line for line in said if SUMMARY.match(line)]
    check(not summary, 'the session ended by itself in the run for %s ("%s"): the run was over before the harness or the exit key ended it'
          % (what, summary[0] if summary else ''), run.log)
    check(any(': ok: ' in line for line in said), 'the test of the session did not run in the run for %s: no "SelfTest: ...: ok: ..." line' % what, run.log)
    for text in args.expect:
        check(any(text in line for line in said), 'the session did not say "%s" in the run for %s' % (text, what), run.log)


def mode_neutral(args, lines):
    """Four runs with the same N, V and keys: the line, the line of a run with the session, the exit line, the exit
    line of a run with the session."""
    check(args.selftest, 'the mode neutral needs --selftest')
    check(args.vblanks, 'the mode neutral needs --vblanks')
    session_key = ['Debugger:SelfTest=' + args.selftest]

    # (the runs with the session first: what they have to show is asked of them before they are compared)
    session = Run(args, 'session-line', args.engine, args.render).run(['config:Debug.AicaHash=%d' % args.ticks] + session_key)
    session_did(args, session, 'the line')
    found = sound_line(session.log)
    check(found is not None, 'the run with the session printed no line of the sound harness, or more than one', session.log)
    check(session.code == 0, 'the run with the session ended with exit code %s' % session.code, session.log)
    session_text, session_fields = found
    check(session_fields['ticks'] == str(args.ticks), 'the line of the run with the session has ticks=%s, not %d' % (session_fields['ticks'], args.ticks), session.log)

    plain, plain_text, plain_fields = line_run(args, 'plain-line', args.engine, args.render)
    check(not different(plain_fields, session_fields, LINE_FIELDS), 'the line of the run with the session differs from the line of the run nobody touched in '
          + different(session_fields, plain_fields, LINE_FIELDS) + '\n' + session_text + '\n' + plain_text, session.log)

    session_hash = Run(args, 'session-hash', args.engine, args.render).run(
        ['config:Exit.AfterVblanks=%d' % args.vblanks, 'config:Exit.StateHash=yes'] + session_key)
    session_did(args, session_hash, 'the state hash')
    line = exit_line(session_hash.log)
    check(line is not None and 'hash' in line, 'the run with the session printed no exit line with a state hash', session_hash.log)
    check(session_hash.code == 0, 'the run with the session ended with exit code %s' % session_hash.code, session_hash.log)

    plain_hash, base = hash_run(args, 'plain-hash', args.engine, args.render)
    check(not different(base, line, HASH_FIELDS), 'the run with the session differs from the run nobody touched in '
          + different(line, base, HASH_FIELDS), session_hash.log)

    print('with the session %s and without it: one line, %s' % (args.selftest, plain_text))
    print('and both runs end in hash=%s after %s vblanks' % (line['hash'], line['vblanks']))


def mode_files(args, lines):
    directory = os.path.abspath(args.dir)
    path = os.path.join(directory, 'SHA256SUMS')
    check(os.path.isfile(path), 'there is no ' + path)
    with open(path, 'r', encoding='utf-8') as f:
        listed = [line.split('  ', 1) for line in f.read().splitlines() if line.strip()]
    check(all(len(entry) == 2 for entry in listed), 'SHA256SUMS has a line that is not "<sum>  <name>"')
    names = tuple(name for digest, name in listed)
    check(names == MANIFEST, 'SHA256SUMS lists %s; it has to list %s, in this order, and nothing else' % (', '.join(names), ', '.join(MANIFEST)))
    for digest, name in listed:
        with open(os.path.join(directory, name), 'rb') as f:
            now = hashlib.sha256(f.read()).hexdigest()
        check(now == digest, '%s is not the file of the manifest: its SHA-256 is %s, SHA256SUMS has %s' % (name, now, digest))
    print('the %d files of SHA256SUMS are the files of the manifest' % len(listed))

    # where the assemblers are: the committed files are what the sources give
    bash = shutil.which('bash')
    if bash is None:
        print('gen.sh --check was not run: no bash')
        return
    done = subprocess.run([bash, os.path.join(directory, 'gen.sh'), '--check'], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=args.watchdog)
    said = done.stdout.decode('utf-8', 'replace').strip()
    if done.returncode == 3:
        print('gen.sh --check was not run: ' + said)
        return
    check(done.returncode == 0, 'gen.sh --check ended with exit code %d:\n%s' % (done.returncode, said))
    print(said)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    modes = {'same': mode_same, 'sounds': mode_sounds, 'engines': mode_engines, 'record': mode_record, 'compare': mode_compare,
             'neutral': mode_neutral, 'files': mode_files}
    parser.add_argument('--mode', action='append', required=True, choices=sorted(modes), help='one or more; they share their runs')
    parser.add_argument('--emulator', default='')
    parser.add_argument('--work', default='')
    parser.add_argument('--guest', default='', help='the program: dsptest.elf of this directory, or another')
    parser.add_argument('--ticks', type=int, default=0, help='N: config:Debug.AicaHash')
    parser.add_argument('--engine', default=None, choices=ENGINES, help='default jit; record, compare: both')
    parser.add_argument('--render', default=None, choices=RENDERS, help='default batch; record, compare: both')
    parser.add_argument('--fields', default='samples,regs,arm7,ticks,cycles', help='engines: the fields the two lines agree in')
    parser.add_argument('--file', default='', help='record, compare: the file of lines, outside of the tree')
    parser.add_argument('--note', action='append', default=[], help='record: a comment line for the file (the commit, the build)')
    parser.add_argument('--runs', type=int, default=2, help='record, compare: the runs of each kind in each configuration')
    parser.add_argument('--vblanks', type=int, default=0, help='V: config:Exit.AfterVblanks of the runs for the exit line (record, compare, neutral)')
    parser.add_argument('--selftest', default='', help='neutral: the filter of the session')
    parser.add_argument('--expect', action='append', default=[], help='neutral: text a line of the session has to have')
    parser.add_argument('--config', action='append', default=[], help='section:key=value, for every run')
    parser.add_argument('--dir', default=os.path.dirname(os.path.abspath(__file__)), help='files: the directory of the guest')
    parser.add_argument('--watchdog', type=int, default=300, help='seconds a run may take')
    parser.add_argument('--keep', action='store_true', help='keep the work directory of a test that passed')
    args = parser.parse_args()

    runs_emulator = [mode for mode in args.mode if mode != 'files']
    if runs_emulator:
        for name in ('emulator', 'work', 'guest'):
            if not getattr(args, name):
                parser.error('the mode %s needs --%s' % (runs_emulator[0], name))
        if args.ticks <= 0 or args.runs <= 0:
            parser.error('the mode %s needs --ticks, and --runs is at least 1' % runs_emulator[0])

        args.emulator = os.path.abspath(args.emulator)
        args.work = os.path.abspath(args.work)
        args.guest = os.path.abspath(args.guest)

        if not os.path.isfile(args.emulator):
            print('FAIL: no emulator at ' + args.emulator)
            return 1
        if not os.path.isfile(args.guest):
            print('FAIL: no program at ' + args.guest)
            return 1

    # one configuration, but for record and compare, which take every configuration that is not excluded
    if not set(args.mode) & {'record', 'compare'}:
        args.engine = args.engine or 'jit'
        args.render = args.render or 'batch'

    lines = Lines(args)
    try:
        for mode in args.mode:
            modes[mode](args, lines)
    except Failed as failure:
        print('FAIL: %s' % failure)
        if runs_emulator:
            print('the output of every run is in ' + args.work)
        return 1

    if runs_emulator and not args.keep:
        shutil.rmtree(args.work, ignore_errors=True)
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
