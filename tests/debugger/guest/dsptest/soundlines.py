#!/usr/bin/env python3
"""The sound lines of the emulator: runs with the sound regression harness, and what they have to agree on.

    soundlines.py --emulator <nulldc3> --work <dir> --guest <program> --ticks <N> --mode <mode> [--mode <mode>]...
                  [--engine jit|interp] [--fields <field,...>] [--file <path>] [--vblanks <V>]
                  [--selftest <filter>] [--expect <text>]... [--ignored <section:key=value>]...
                  [--config <section:key=value>]... [--oracle <aica_dsp_guest_oracle>]
    soundlines.py --mode files [--dir <the directory of the guest>] [--no-posix-shell]
    soundlines.py --mode names --build <the build directory of the tests>

A run is the emulator on the program, headless, with no audio device (audio:backend=none), the real-time clock
pinned and the SH4 on the recompiler or on the interpreter (--engine), with its files in a directory of its own
under the work directory (--portable), an empty environment (no display), a core file size limit of 1 byte, and a
watchdog. A process this script starts is ended by its pid. On Windows the environment has SystemRoot and TEMP and
TMP of the run's directory, the tool of the mode oracle has the caller's with TEMP and TMP of its own directory, and
the emulator and the tool run inside nulldc3-winrun (tests/common/emurun.py, which starts every program of this
script and reads what it prints). The sound is made sample by sample, with the DSP, in every time step of the AICA:
the emulator has no other way to make it.

Every run has to be made on the engine it was asked for. The emulator takes config:Dynarec.Enabled=yes for a wish:
a build without a recompiler, or one whose recompiler cannot be set up, goes on with the interpreter and says
"Using MCPU Interpreter" where the other says "Using MCPU Recompiler". A run whose output does not have the line
of its engine, or has the other one, fails: the line of a run on the interpreter is no line of the recompiler,
and two of them agree in every field.

There are two kinds of run:

    the line        config:Debug.AicaHash=<N>: after N ticks of the AICA (a tick is 32 samples) the emulator prints
                        AicaHash: samples=<hex> regs=<hex> arm7=<hex> sh4=<hex> dsp=<hex> ticks=<N> cycles=<decimal>
                    and ends the process, with no exit line (docs/nulldc3/automated-runs.md). The line of every
                    run has to have ticks=<N>
    the exit line   config:Exit.AfterVblanks=<V> , config:Exit.StateHash=yes in place of the harness key: the
                    state hash of the same program, which has what the line has no field for (wave memory, main
                    memory, the scheduler)

README.md has the modes:

    same      two runs with the same keys print the same line, field for field
    sounds    samples is none of the hashes of silence. The hash of k * 32 pairs of zeroes is one value for each
              k. Every tick makes its 32 pairs, whether the ARM7 is held in reset or not, so a silent run of N
              ticks prints the one for N; the mode checks against the whole family, k from 0 to N, a superset
    silent    a program that plays nothing (2ndmix): samples is the hash of exactly N * 32 pairs of zeroes, the
              32 pairs of every one of the N ticks (see mode_silent)
    muted     "Disable Sound" mutes: a run with config:aica.NoSound=yes prints the line of the run without it in
              every field but samples, and samples is the hash of N * 32 pairs of zeroes (see mode_muted; the
              audio stream itself, which no run here shows, is held by aica.dsp.emu.state)
    ignored   keys that an older build read and this one does not: a run with each key of --ignored prints the
              line of the run without it, field for field
    engines   the line of the recompiler and the line of the interpreter agree in the fields of --fields, and in dsp
    record    the lines (and with --vblanks the exit lines) of both engines, or of the one that --engine names,
              into the file of --file, which is outside of the tree: a line is good for one build on one host.
              Every run is made --runs times and has to give one line. Entries of other programs and of other N
              or V stay in the file
    compare   the same runs, against the entries of that file: every line and every part of the state hash
    neutral   the debugger in use changes nothing: a run with --config Debugger:SelfTest=<filter> prints the line
              of the run without it, and ends in its state hash, part for part, at --vblanks; and the session did
              something (see mode_neutral)
    release   the guest only: a release of the ARM7 starts the same program whatever the AICA holds for it. The
              line of a copy of the guest whose SH4 leaves a request latched for the ARM7 in front of the release
              is the line of the guest, in the fields of --fields (see mode_release)
    held      the guest only: a held ARM7 is an idle one for the rest of the AICA. A copy of the guest whose ARM7
              stays held behind the set-up and a copy whose ARM7 idles on a branch to itself print the same line
              in every field but arm7 (see mode_held)
    heard     the guest only: its DSP is heard. A copy of the guest whose table gives the outputs of the DSP
              program EFSDL 0 prints the line of the guest in arm7, sh4, dsp, ticks and cycles, and another samples
              (see mode_heard)
    oracle    the guest only: the field dsp of a copy of the guest whose sends to the DSP are all 0 (its first
              channel at IMXL 0) is what --oracle, aica_dsp_guest_oracle of tests/aica_dsp, computes for that copy
              on the caique model (see mode_oracle)
    files     the committed files of the guest are those of its manifest, SHA256SUMS; and where the assemblers
              are, gen.sh --check: they are what the sources give. gen.sh is a bash script: on Windows it is not
              run (bash there is WSL's launcher, which would run it inside a Linux of the computer), and the mode
              says so: "gen.sh --check was not run: no POSIX shell on Windows"
    names     the tests of a build (the CTestTestfile.cmake files of --build): no test of the sound
              (debugger.sound.*) or of the session arm7 of the selftests (debugger.selftest.arm7*) has a part
              batch or persample. The sound was made in batches of 32 samples by default and sample by sample with
              config:aica.NoBatch=yes until that render went, and the tests of both had it in their names. Other
              tests may: there the words name something else (a batch of requests of the debugger's protocol)
"""

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

# the run helper of the harnesses; nothing is written into the source tree for it
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'common'))

import emurun  # noqa: E402

RTC_PIN = 946684800

BAD = ('Fatal error', 'Verify Failed', 'fault_handler', 'AddressSanitizer', 'ThreadSanitizer', 'runtime error:')

LINE_FIELDS = ('samples', 'regs', 'arm7', 'sh4', 'dsp', 'ticks', 'cycles')

# the parts of the state hash, as the mode same-hash of tests/debugger/selftest/selftest.py compares them
HASH_FIELDS = ('reason', 'vblanks', 'hash', 'sh4', 'sched', 'ram', 'vram', 'aram', 'arm7')

ENGINES = ('jit', 'interp')

# the parts of a test name that named the two renders of the sound (mode names), in the tests that had them
RENDER_PARTS = ('batch', 'persample')

# what the emulator thread prints when it has chosen the engine of the SH4 (dc_run of libswirl/libswirl.cpp)
ENGINE_SAYS = {'jit': 'Using MCPU Recompiler', 'interp': 'Using MCPU Interpreter'}
ENGINE_NAMES = {'jit': 'the recompiler', 'interp': 'the interpreter'}

# what SHA256SUMS lists, in this order, and nothing else
MANIFEST = ('dsptest.s', 'arm7.s', 'dsptest_dsp.txt', 'gen.sh', 'dsptest.elf', 'arm7.bin', 'dsptest_labels.inc', 'dsptest_dsp.inc')

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

    def __init__(self, args, name, engine, guest=None):
        self.args = args
        self.name = name
        self.engine = engine
        self.guest = guest or args.guest
        self.dir = os.path.join(args.work, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        # the directory of the run's files (--portable), with the data subdirectory the flash is saved in
        os.makedirs(os.path.join(self.dir, 'portable', 'data'))
        self.log_path = os.path.join(self.dir, 'log.txt')
        self.log = ''
        self.code = None

    def command(self, keys):
        items = ['config:Headless=yes', 'audio:backend=none', 'config:Dreamcast.RTC=%d' % RTC_PIN,
                 'config:Dynarec.Enabled=' + ('yes' if self.engine == 'jit' else 'no')]
        items += keys + self.args.config
        command = [self.args.emulator, '--portable', os.path.join(self.dir, 'portable'), '--config']
        for i, item in enumerate(items):
            if i:
                command.append(',')
            command.append(item)
        command.append(self.guest)
        return command

    def run(self, keys):
        # nothing of the environment but the options of the sanitizers (on Windows also what Windows needs, in the
        # directory of the run): the directory is the one of --portable
        self.cmdline = self.command(keys)
        with open(self.log_path, 'wb') as out:
            process = emurun.start(self.cmdline, self.args.watchdog, cwd=self.dir, env=emurun.environment(self.dir),
                                   stdout=out, stderr=subprocess.STDOUT)
            try:
                self.code = process.wait(timeout=self.args.watchdog)
            except subprocess.TimeoutExpired:
                emurun.kill(process)
        self.log = emurun.read_text(self.log_path)
        check(self.code is not None, 'the run did not end within %d s: %s' % (self.args.watchdog, ' '.join(self.cmdline)), self.log)
        for bad in BAD:
            check(bad not in self.log, 'the output has "%s"' % bad, self.log)

        # the engine that ran is the engine that was asked for: the emulator goes on with the interpreter when it
        # has no recompiler, and such a run would pass for one of the recompiler
        other = 'interp' if self.engine == 'jit' else 'jit'
        asked = '%s (config:Dynarec.Enabled=%s)' % (ENGINE_NAMES[self.engine], 'yes' if self.engine == 'jit' else 'no')
        check(ENGINE_SAYS[other] not in self.log, 'the run %s was asked for %s and was made on %s: the output has "%s"'
              % (self.name, asked, ENGINE_NAMES[other], ENGINE_SAYS[other]), self.log)
        check(ENGINE_SAYS[self.engine] in self.log, 'the run %s was asked for %s and does not say that it was made on it: the output has no "%s"'
              % (self.name, asked, ENGINE_SAYS[self.engine]), self.log)
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


def line_run(args, name, engine, more=(), guest=None):
    """A run that the harness ends: the Run, the text of its line and the fields."""
    run = Run(args, name, engine, guest).run(['config:Debug.AicaHash=%d' % args.ticks] + list(more))
    found = sound_line(run.log)
    check(found is not None, 'the run %s printed no line of the sound harness, or more than one' % name, run.log)
    check(run.code == 0, 'the run %s ended with exit code %s' % (name, run.code), run.log)
    text, fields = found
    check(fields['ticks'] == str(args.ticks), 'the line of the run %s has ticks=%s, not %d: %s' % (name, fields['ticks'], args.ticks, text), run.log)
    check(exit_line(run.log) is None, 'the run %s printed an exit line: the harness ends the process without one' % name, run.log)
    return run, text, fields


def hash_run(args, name, engine, more=()):
    """A run that config:Exit.AfterVblanks ends: the Run and the fields of its exit line."""
    run = Run(args, name, engine).run(['config:Exit.AfterVblanks=%d' % args.vblanks, 'config:Exit.StateHash=yes'] + list(more))
    line = exit_line(run.log)
    check(line is not None and 'hash' in line, 'the run %s printed no exit line with a state hash' % name, run.log)
    check(run.code == 0, 'the run %s ended with exit code %s' % (name, run.code), run.log)
    check(line.get('reason') == 'vblanks' and line.get('vblanks') == str(args.vblanks),
          'the run %s ended with reason=%s after %s vblanks, not at vblank %d' % (name, line.get('reason'), line.get('vblanks'), args.vblanks), run.log)
    return run, line


def tool_run(args, command, log_path):
    """A program that is not the emulator (aica_dsp_guest_oracle, gen.sh), run to its end through emurun with the
    caller's environment (on Windows with TEMP and TMP in the directory of its output): what it printed, and its
    exit code (None when the watchdog ended it)."""
    directory = os.path.dirname(log_path)
    os.makedirs(directory, exist_ok=True)
    with open(log_path, 'wb') as out:
        process = emurun.start(command, args.watchdog, env=emurun.tool_environment(directory), stdout=out,
                               stderr=subprocess.STDOUT, emulator=False)
        try:
            code = process.wait(timeout=args.watchdog)
        except subprocess.TimeoutExpired:
            emurun.kill(process)
            code = None
    return emurun.read_text(log_path), code


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
            self.made.append(line_run(self.args, 'line-%d' % (len(self.made) + 1), self.args.engine))
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


def mode_silent(args, lines):
    """A program that plays nothing (2ndmix under reios): every tick makes its 32 samples, whether it finds the ARM7
    clocked or held in reset, and each hands the audio stream a pair of zeroes. So samples is the hash of exactly
    N * 32 pairs of zeroes. A tick that makes fewer or more samples than 32 (a time step left under the ARM7's clock,
    a burst of 31 or 33 steps) gives the hash of another k, and a sample that hands no pair, too."""
    (run, text, fields), = lines.get(1)
    k = silence(args.ticks).get(fields['samples'])
    check(k is not None, 'the run is not silent: samples=%s is none of the hashes of 0 to %d * 32 pairs of zeroes\n%s' % (fields['samples'], args.ticks, text), run.log)
    check(k == args.ticks, 'the run handed the audio stream %d * 32 pairs of zeroes in %d ticks, not %d * 32: its ticks did not make 32 samples each\n%s'
          % (k, args.ticks, args.ticks, text), run.log)
    print('silent: samples=%s is the hash of %d * 32 pairs of zeroes, 32 for each of the %d ticks' % (fields['samples'], k, args.ticks))


def mode_muted(args, lines):
    """"Disable Sound" mutes. With config:aica.NoSound=yes the emulator makes every sample as without it, and the
    pair the harness folds, where the sample hands its pair to the audio stream, is a pair of zeroes. So the run
    with the key prints the line of the run without it in every field but samples, and samples is the hash of
    N * 32 pairs of zeroes: one pair for each sample of each of the N ticks, which every burst makes, whether it
    finds the ARM7 clocked or held in reset. A sound that is folded where zeroes go out, a sample that folds no
    pair, or one that is not made (the ARM7, the DSP, the register file of the run would differ), fails. What a
    line has is the harness's fold, not the stream: the runs have no audio device (audio:backend=none). That the
    stream itself, the speed limit of the emulator (config:aica.LimitFPS), is handed a pair of zeroes in every
    sample is held by aica.dsp.emu.state (its check nosound), on a stand-in stream."""
    (plain, plain_text, plain_fields), = lines.get(1)
    muted, text, fields = line_run(args, 'muted', args.engine, ['config:aica.NoSound=yes'])
    k = silence(args.ticks).get(fields['samples'])
    check(k is not None, 'the run with config:aica.NoSound=yes is not silent: samples=%s is none of the hashes of 0 to %d * 32 pairs of zeroes\n%s'
          % (fields['samples'], args.ticks, text), muted.log)
    check(k == args.ticks, 'the run with config:aica.NoSound=yes handed the audio stream %d * 32 pairs of zeroes, not %d * 32: a sample of a tick went '
          'without its pair\n%s' % (k, args.ticks, text), muted.log)
    others = [f for f in LINE_FIELDS if f != 'samples']
    check(not different(plain_fields, fields, others), 'the run with config:aica.NoSound=yes differs from the run without it in '
          + different(plain_fields, fields, others) + '\n' + plain_text + '\n' + text, muted.log)
    print('muted: samples=%s is the hash of %d * 32 pairs of zeroes, and %s as without the key' % (fields['samples'], k,
          ' '.join('%s=%s' % (f, fields[f]) for f in others)))


def mode_ignored(args, lines):
    """Keys of an older build that this one does not read: a configuration file keeps them (the emulator writes it
    back with every key it found), and a run with one is the run without it."""
    check(args.ignored, 'the mode ignored needs --ignored')
    (plain, plain_text, plain_fields), = lines.get(1)
    for n, key in enumerate(args.ignored, 1):
        run, text, fields = line_run(args, 'ignored-%d' % n, args.engine, [key])
        check(text == plain_text, 'the run with %s prints another line than the run without it: they differ in %s\n%s\n%s'
              % (key, different(plain_fields, fields, LINE_FIELDS), plain_text, text), run.log)
    print('with each of %s and without it: one line, %s' % (', '.join(args.ignored), plain_text))


def mode_engines(args, lines):
    names = [f for f in args.fields.split(',') if f]
    check(names and all(f in LINE_FIELDS for f in names), '--fields takes names of ' + ', '.join(LINE_FIELDS))
    # the DSP's whole state, which the SH4's engine does not reach: what the guest's DSP does is the same on both
    if 'dsp' not in names:
        names.append('dsp')
    jit, jit_text, jit_fields = line_run(args, 'jit', 'jit')
    interp, interp_text, interp_fields = line_run(args, 'interp', 'interp')
    check(not different(jit_fields, interp_fields, names), 'the line of the recompiler and the line of the interpreter differ in '
          + different(jit_fields, interp_fields, names) + '\n' + jit_text + '\n' + interp_text, interp.log)
    print('the recompiler and the interpreter agree in ' + ' '.join('%s=%s' % (f, jit_fields[f]) for f in names))


def configurations(args):
    return [engine for engine in ENGINES if args.engine in (None, engine)]


def entries(args):
    """What record stores and compare looks at: key -> text, for every configuration that is asked for."""
    guest = os.path.basename(args.guest)
    found = {}
    for engine in configurations(args):
        texts = set()
        for n in range(args.runs):
            run, text, fields = line_run(args, 'line-%s-%d' % (engine, n + 1), engine)
            texts.add(text)
        check(len(texts) == 1, '%d runs on %s print %d lines:\n%s' % (args.runs, engine, len(texts), '\n'.join(sorted(texts))))
        found['%s ticks=%d %s' % (guest, args.ticks, engine)] = texts.pop()
        if args.vblanks:
            texts = set()
            for n in range(args.runs):
                run, line = hash_run(args, 'hash-%s-%d' % (engine, n + 1), engine)
                texts.add('nulldc-exit: ' + ' '.join('%s=%s' % (f, line.get(f)) for f in HASH_FIELDS))
            check(len(texts) == 1, '%d runs on %s end in %d state hashes:\n%s' % (args.runs, engine, len(texts), '\n'.join(sorted(texts))))
            found['%s vblanks=%d %s' % (guest, args.vblanks, engine)] = texts.pop()
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
        comments = ['# Sound lines and state hashes, recorded by soundlines.py: <program> <ticks=N or vblanks=V> <engine> | <line>.',
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
    session = Run(args, 'session-line', args.engine).run(['config:Debug.AicaHash=%d' % args.ticks] + session_key)
    session_did(args, session, 'the line')
    found = sound_line(session.log)
    check(found is not None, 'the run with the session printed no line of the sound harness, or more than one', session.log)
    check(session.code == 0, 'the run with the session ended with exit code %s' % session.code, session.log)
    session_text, session_fields = found
    check(session_fields['ticks'] == str(args.ticks), 'the line of the run with the session has ticks=%s, not %d' % (session_fields['ticks'], args.ticks), session.log)

    plain, plain_text, plain_fields = line_run(args, 'plain-line', args.engine)
    check(not different(plain_fields, session_fields, LINE_FIELDS), 'the line of the run with the session differs from the line of the run nobody touched in '
          + different(session_fields, plain_fields, LINE_FIELDS) + '\n' + session_text + '\n' + plain_text, session.log)

    session_hash = Run(args, 'session-hash', args.engine).run(
        ['config:Exit.AfterVblanks=%d' % args.vblanks, 'config:Exit.StateHash=yes'] + session_key)
    session_did(args, session_hash, 'the state hash')
    line = exit_line(session_hash.log)
    check(line is not None and 'hash' in line, 'the run with the session printed no exit line with a state hash', session_hash.log)
    check(session_hash.code == 0, 'the run with the session ended with exit code %s' % session_hash.code, session_hash.log)

    plain_hash, base = hash_run(args, 'plain-hash', args.engine)
    check(not different(base, line, HASH_FIELDS), 'the run with the session differs from the run nobody touched in '
          + different(line, base, HASH_FIELDS), session_hash.log)

    print('with the session %s and without it: one line, %s' % (args.selftest, plain_text))
    print('and both runs end in hash=%s after %s vblanks' % (line['hash'], line['vblanks']))


def guest_labels(directory):
    """The labels of the guest: dsptest_labels.inc of its directory."""
    path = os.path.join(directory, 'dsptest_labels.inc')
    check(os.path.isfile(path), 'there is no ' + path)
    found = {}
    with open(path, 'r', encoding='utf-8') as f:
        for line in f.read().splitlines():
            define = re.match(r'#define DSPTEST_(\w+) (0x[0-9A-Fa-f]+|\d+)$', line)
            if define:
                found[define.group(1)] = int(define.group(2), 0)
    return found


def file_offset(image, address, what):
    """Where a byte of the program is in its ELF (32 bits, low byte first): the loaded segment that has the address."""
    check(image[:6] == b'\x7fELF\x01\x01', 'the program is no ELF of 32 bits, low byte first')
    table, = struct.unpack_from('<I', image, 0x1C)
    size, count = struct.unpack_from('<HH', image, 0x2A)
    for n in range(count):
        kind, offset, base, physical, length = struct.unpack_from('<5I', image, table + n * size)
        if kind == 1 and base <= address < base + length:
            return offset + address - base
    raise Failed('%s, %08X, is in no loaded segment of the program' % (what, address))


# Registers of the AICA at their addresses for the SH4, and the two instructions with which a_reset ends a service
# of the interrupt. The second of each pair is the same instruction on a word of the wave memory that nothing
# uses (the loop's variables are at 0x400, 0x404 and 0x408): what it costs the ARM7 stays, what it does goes
SH4_SCIEB, SH4_SCIPD, SH4_SCILV1 = 0xA070289C, 0xA07028A0, 0xA07028AC
ARM_READ_L = (0xE5952500, 0xE596200C)     # ldr r2, [r5, #0x500]  ->  ldr r2, [r6, #12]
ARM_WRITE_M = (0xE5852504, 0xE586200C)    # str r2, [r5, #0x504]  ->  str r2, [r6, #12]


def free_end(image, labels):
    """The empty entries (address 0) at the end of the DSP part of the guest's table, g_table_dsp to
    g_table_dsp_end, in their order: the DSP part fills it from its front, and the copies of this script write
    into its end."""
    free = []
    for entry in range(labels['G_TABLE_DSP_END'] - 8, labels['G_TABLE_DSP'] - 8, -8):
        if image[file_offset(image, entry, 'an entry of the table'):][:8] != bytes(8):
            break
        free.insert(0, entry)
    return free


def table_entries(image, labels):
    """The entries of the guest's table, (address, value), in the order _start writes them; address 0 is an empty
    entry, which it skips."""
    at = file_offset(image, labels['G_TABLE'], 'the table')
    return [struct.unpack_from('<II', image, at + n) for n in range(0, labels['G_TABLE_END'] - labels['G_TABLE'], 8)]


def guest_copy(args, labels, name, table=(), arm=()):
    """A copy of the guest in the work directory: with (address, value) entries in the empty end of the DSP part
    of the table that _start writes while it holds the ARM7 (the last entries in front of the key on: the
    entries of the part are written behind every other register of the table), and with instructions of a_reset
    replaced (old word, new word). Returns its path."""
    with open(args.guest, 'rb') as f:
        image = bytearray(f.read())

    # the last empty entries of the part, in their order: whoever fills the part starts at its front. At least
    # three are left there (the guest's dsptest.s holds that), which the copies of the mode release need
    free = free_end(image, labels)
    check(len(free) >= len(table), 'the DSP part of the table of the guest has %d empty entries at its end, the copy %s needs %d' % (len(free), name, len(table)))
    for entry, (address, value) in zip(free[len(free) - len(table):], table):
        struct.pack_into('<II', image, file_offset(image, entry, 'an entry of the table'), address, value)

    program = labels['G_ARM7_IMAGE']
    for old, new in arm:
        places = [at for at in range(labels['A_RESET'], labels['A_MAIN_LOOP'], 4)
                  if struct.unpack_from('<I', image, file_offset(image, program + at, 'the ARM7 program'))[0] == old]
        check(len(places) == 1, 'a_reset of the guest has %d instructions %08X, the copy %s replaces the one there is' % (len(places), old, name))
        struct.pack_into('<I', image, file_offset(image, program + places[0], 'the ARM7 program'), new)

    os.makedirs(args.work, exist_ok=True)
    path = os.path.join(args.work, name + '.elf')
    with open(path, 'wb') as f:
        f.write(image)
    return path


def mode_release(args, lines):
    """A release of the ARM7 starts the same program whatever the AICA holds for it (the guest only).

    A release latches a request again that was pending when the ARM7 was held, and a_reset ends its service before
    it clears F: else the request is entered behind a_reset, an interrupt nobody raised for the program that
    starts, and every count of the handler is one ahead. A test that sends the SH4 through g_hold_arm7 and
    g_release_arm7 holds the ARM7 at a tick of its own choice, with the timer's request pending or not.

    The runs are made on copies of the guest that differ from it in data only. "A request latched": three entries
    in the empty end of the DSP part of the table, which _start writes while it holds the ARM7 (the guest leaves
    at least three there) - SCILV1 and SCIEB bit 5, and SCIPD bit 5, the one request a program can raise: it is
    latched at once, with the level of the timer's request, and again by the release. "Without the end of the
    service": the read of L or the write of M of a_reset goes to a word of the wave memory instead, at the same
    cost.

        the copy without a change                             the line every other run is compared with
        a request latched                                     the same line, in the fields of --fields
        no request, without the read and without the write    the same line: the two are there for a request only
        a request latched, without the read of L              another arm7 field: the request is entered
        a request latched, without the write of M             another arm7 field: no request is let in again

    The last two say that the request was there: without them the second run could pass on a copy whose entries
    the SH4 never wrote. sh4 is in no comparison: the SH4 of a copy has made three more writes when it enters its
    loop."""
    names = [f for f in args.fields.split(',') if f]
    check(names and all(f in LINE_FIELDS for f in names), '--fields takes names of ' + ', '.join(LINE_FIELDS))
    labels = guest_labels(args.dir)
    for label in ('G_TABLE_DSP', 'G_TABLE_DSP_END', 'G_ARM7_IMAGE', 'A_RESET', 'A_MAIN_LOOP'):
        check(label in labels, 'dsptest_labels.inc in %s has no DSPTEST_%s' % (args.dir, label))
    request = ((SH4_SCILV1, 0x20), (SH4_SCIEB, 0x20), (SH4_SCIPD, 0x20))

    # the three entries go into the empty end of the DSP part, which the guest keeps for them
    with open(args.guest, 'rb') as f:
        free = free_end(f.read(), labels)
    check(len(free) >= len(request), 'the DSP part of the table of the guest (g_table_dsp to g_table_dsp_end) has %d empty entries at its end, '
          'not the %d the copies of this mode write there' % (len(free), len(request)))

    def made(name, table=(), arm=()):
        return line_run(args, name, args.engine, guest=guest_copy(args, labels, name, table, arm))

    plain, plain_text, plain_fields = made('plain')

    latched, text, fields = made('latched', request)
    check(not different(plain_fields, fields, names), 'a release of the ARM7 that finds a request latched does not start the program of the run without one: the lines differ in '
          + different(plain_fields, fields, names) + '\n' + plain_text + '\n' + text, latched.log)

    without, text, fields = made('without', arm=(ARM_READ_L, ARM_WRITE_M))
    check(not different(plain_fields, fields, names), 'with no request latched, the read of L and the write of M of a_reset change the run: the lines differ in '
          + different(plain_fields, fields, names) + '\n' + plain_text + '\n' + text, without.log)

    for name, arm, what in (('latched-no-l', (ARM_READ_L,), 'the read of L'), ('latched-no-m', (ARM_WRITE_M,), 'the write of M')):
        control, text, fields = made(name, request, arm)
        check(fields['arm7'] != plain_fields['arm7'], 'the copy with a request latched and without %s of a_reset prints the arm7 field of the guest: '
              'no request was latched when its ARM7 was released, and the run with the whole a_reset shows nothing\n' % what + plain_text + '\n' + text, control.log)

    print('a release that finds a request latched starts the program of the run without one: ' + ' '.join('%s=%s' % (f, plain_fields[f]) for f in names))
    print('and the request was there: without the read of L or the write of M of a_reset the arm7 field is another')


# The release of the ARM7 with which _start ends, in front of g_main: mov.l .L_armrst, r1; mov #0, r0; mov.b r0, @r1.
# The copy held makes its mov #0, r0 (the halfword at g_main - 4) a mov #1, r0; the copy idle makes the instruction
# at a_reset of the ARM7 program a branch to itself
SH4_MOV_0_R0, SH4_MOV_1_R0 = 0xE000, 0xE001
ARM_B_SELF = 0xEAFFFFFE

# the fields the two copies of the mode held agree in: every field of the line but arm7
HELD_FIELDS = tuple(f for f in LINE_FIELDS if f != 'arm7')


def mode_held(args, lines):
    """A held ARM7 is an idle ARM7 for the rest of the AICA (the guest only): the AICA is clocked whatever ARMRST
    says, and only the ARM7 is not.

    The runs are made on two copies of the guest, built in the work directory, that differ from it in one
    instruction each. "held": the last release of the ARM7 in _start, in front of g_main, is made a hold (its
    mov #0, r0, the halfword at g_main - 4, made mov #1, r0; the halfword has to be E000, else the mode fails):
    the ARM7 stays held behind the set-up, and every burst from the second on finds it held. "idle": the
    instruction at a_reset of the ARM7 program is made b . (EAFFFFFE): the released ARM7 stays on it, its FIQ
    and IRQ masked by its reset, and touches nothing of the AICA. Neither copy's SH4 is interrupted (no ARM7
    raises the AICA's interrupt to it), and the channels the SH4 keys play at the pitch it gave them.

        held and idle      the same line in every field but arm7: the samples, the register file, the SH4, the DSP,
                           the ticks and the time of a burst whose ARM7 is held are those of one whose ARM7 does
                           nothing
        held and idle      another arm7 field each: both patches took
        held               samples is none of the hashes of silence: the channels the SH4 keyed play while the ARM7
                           is held
        idle and the guest another samples field: the guest's FIQ handler is heard, and the copy idle is not it

    A build whose bursts make no time step while the ARM7 is held prints the 32 pairs of the first tick for the
    copy held, and a DSP and a register file that stood still from then on."""
    labels = guest_labels(args.dir)
    for label in ('G_MAIN', 'G_ARM7_IMAGE', 'A_RESET'):
        check(label in labels, 'dsptest_labels.inc in %s has no DSPTEST_%s' % (args.dir, label))
    with open(args.guest, 'rb') as f:
        image = bytes(f.read())

    release = labels['G_MAIN'] - 4
    at = file_offset(image, release, 'the release in front of g_main')
    half, = struct.unpack_from('<H', image, at)
    check(half == SH4_MOV_0_R0, 'the halfword at g_main - 4 (%08X) of the guest is %04X, not %04X (mov #0, r0): the copy held makes the release '
          'with which _start ends a hold, and this guest has no such release there' % (release, half, SH4_MOV_0_R0))

    reset = labels['G_ARM7_IMAGE'] + labels['A_RESET']
    reset_at = file_offset(image, reset, 'a_reset of the ARM7 program')

    def made(name, offset, fmt, value):
        copy = bytearray(image)
        struct.pack_into(fmt, copy, offset, value)
        os.makedirs(args.work, exist_ok=True)
        path = os.path.join(args.work, name + '.elf')
        with open(path, 'wb') as f:
            f.write(copy)
        return line_run(args, name, args.engine, guest=path)

    (plain, plain_text, plain_fields), = lines.get(1)
    held, held_text, held_fields = made('held', at, '<H', SH4_MOV_1_R0)
    idle, idle_text, idle_fields = made('idle', reset_at, '<I', ARM_B_SELF)

    check(not different(held_fields, idle_fields, HELD_FIELDS), 'the copy whose ARM7 stays held and the copy whose ARM7 idles on b . differ in '
          + different(held_fields, idle_fields, HELD_FIELDS) + ': a held ARM7 is not an idle one for the rest of the AICA\n' + held_text + '\n' + idle_text, held.log)
    check(held_fields['arm7'] != idle_fields['arm7'], 'the copy held and the copy idle print one arm7 field, %s: one of the two patches did not take\n'
          % held_fields['arm7'] + held_text + '\n' + idle_text, idle.log)
    k = silence(args.ticks).get(held_fields['samples'])
    check(k is None, 'the copy whose ARM7 stays held is silent: samples=%s is the hash of %d * 32 pairs of zeroes, where the channels the SH4 keyed '
          'play while the ARM7 is held\n' % (held_fields['samples'], k if k is not None else 0) + held_text, held.log)
    check(idle_fields['samples'] != plain_fields['samples'], 'the copy whose ARM7 idles on b . prints the samples of the guest, %s: the guest\'s FIQ '
          'handler is not heard, and the copy is not the idle one\n' % plain_fields['samples'] + plain_text + '\n' + idle_text, idle.log)

    print('a held ARM7 is an idle one for the rest of the AICA: ' + ' '.join('%s=%s' % (f, held_fields[f]) for f in HELD_FIELDS))
    print('and the two copies are what they are: arm7=%s held, arm7=%s idle; the held one sounds; the guest, samples=%s, is another'
          % (held_fields['arm7'], idle_fields['arm7'], plain_fields['samples']))


# The levels of the outputs of the DSP, EFSDL (bits 11:8) and EFPAN, at 0x00702000 + 4 k of the AICA, k < 16
SH4_EFSDL, DSP_OUTPUTS = 0xA0702000, 16


def mode_heard(args, lines):
    """The guest's DSP is heard (the guest only): the mix takes EFREG from the DSP.

    The guest's table gives the outputs of its DSP program a level (EFSDL and EFPAN at 0x00702000 + 4 k). The run
    is made on a copy of the guest, built in the work directory, in which each of those entries has EFSDL 0, the
    level at which the mix takes nothing of that output; EFPAN, and every other byte, are the guest's. So the copy
    makes the same writes with other values: its line is the guest's in arm7, sh4, dsp, ticks and cycles (the
    ARM7, the SH4 and the DSP run as they run in the guest), and samples has to differ: what the guest's DSP
    outputs is in what the guest makes heard. regs differs as well, the register file has the levels the SH4
    wrote, and is not compared. The copy's line is printed: its samples is the guest's samples without the DSP,
    which a before / after holds against the samples of the guest without a DSP program."""
    labels = guest_labels(args.dir)
    for label in ('G_TABLE', 'G_TABLE_END'):
        check(label in labels, 'dsptest_labels.inc in %s has no DSPTEST_%s' % (args.dir, label))
    with open(args.guest, 'rb') as f:
        image = bytearray(f.read())

    at = file_offset(image, labels['G_TABLE'], 'the table')
    levels = []
    for n, (address, value) in enumerate(table_entries(image, labels)):
        if SH4_EFSDL <= address < SH4_EFSDL + 4 * DSP_OUTPUTS and address % 4 == 0 and (value >> 8) & 15:
            levels.append('EFREG[%d] (EFSDL %d)' % ((address - SH4_EFSDL) // 4, (value >> 8) & 15))
            struct.pack_into('<I', image, at + 8 * n + 4, value & ~0xF00)
    check(levels, 'the table of the guest gives no output of the DSP a level (EFSDL, at 0x00702000 + 4 k): nothing the DSP computes is heard')

    os.makedirs(args.work, exist_ok=True)
    path = os.path.join(args.work, 'deaf.elf')
    with open(path, 'wb') as f:
        f.write(image)

    (plain, plain_text, plain_fields), = lines.get(1)
    deaf, text, fields = line_run(args, 'deaf', args.engine, guest=path)
    same = ('arm7', 'sh4', 'dsp', 'ticks', 'cycles')
    check(not different(plain_fields, fields, same), 'the copy whose outputs of the DSP have EFSDL 0 and the guest differ in '
          + different(plain_fields, fields, same) + ': the levels changed more than what is heard\n' + plain_text + '\n' + text, deaf.log)
    check(fields['samples'] != plain_fields['samples'], 'the copy whose outputs of the DSP have EFSDL 0 prints the samples of the guest, %s: '
          'the mix does not take what the DSP outputs\n' % plain_fields['samples'] + plain_text + '\n' + text, deaf.log)
    print('the DSP is heard: with EFSDL 0 for %s the copy prints the guest\'s %s and another samples' % (', '.join(levels), ' '.join('%s=%s' % (f, fields[f]) for f in same)))
    print('the guest: ' + plain_text)
    print('the copy:  ' + text)


# The first channel's ISEL (bits 3:0) and IMXL (bits 7:4), at 0x00700020 of the AICA
SH4_CH0_MIX = 0xA0700020


def mode_oracle(args, lines):
    """The DSP of the guest is the console's DSP, as caique's sample model has it (the guest only).

    caique's model ends at EFREG and the emulator's sound generator is not caique's, so what the two can be held to
    is the DSP's state where its inputs are known. The run is made on a copy of the guest, built in the work
    directory, with one entry in the empty end of the DSP part of the table: IMXL 0 for the first channel, its
    ISEL as the guest's table sets it, which the SH4 writes behind the guest's own entry for it. A channel's send
    to the DSP is its sample times the level of its IMXL, and that level is 0 for IMXL 0 (the emulator's
    attenuation of the send is then 255 or more, capped at what the envelope leaves, and the table of levels has
    0 there: hw/aica/sgc_if.cpp, UpdateAtts and ChannelEx::Step), so every bus the program reads holds 0. The
    field dsp of that copy's line, the state of the DSP folded at every tick, has to be what --oracle
    (aica_dsp_guest_oracle of tests/aica_dsp) computes for the copy: the DSP core and caique's model in lockstep
    on the guest's writes, with the emulator's sweep of sends of 0. And the guest itself, whose first channel
    sends, prints another dsp: the comparison sees the sends."""
    check(args.oracle, 'the mode oracle needs --oracle, the path of aica_dsp_guest_oracle')
    check(os.path.isfile(args.oracle), 'there is no aica_dsp_guest_oracle at ' + args.oracle)
    labels = guest_labels(args.dir)
    for label in ('G_TABLE', 'G_TABLE_END', 'G_TABLE_DSP', 'G_TABLE_DSP_END'):
        check(label in labels, 'dsptest_labels.inc in %s has no DSPTEST_%s' % (args.dir, label))
    with open(args.guest, 'rb') as f:
        image = f.read()

    mix = [value for address, value in table_entries(image, labels) if address == SH4_CH0_MIX]
    check(mix, 'the table of the guest does not set ISEL / IMXL of its first channel (0x00700020): it has no send to take away')
    check((mix[-1] >> 4) & 15, 'the table of the guest leaves its first channel at IMXL 0: it has no send to take away')
    value = mix[-1] & ~0xF0
    path = guest_copy(args, labels, 'oracle', ((SH4_CH0_MIX, value),))
    copy, text, fields = line_run(args, 'oracle', args.engine, guest=path)

    command = [args.oracle, '--elf', path, '--ticks', str(args.ticks)]
    said, code = tool_run(args, command, os.path.join(args.work, 'oracle-tool.txt'))
    check(code is not None, 'aica_dsp_guest_oracle did not end within %d s: %s' % (args.watchdog, ' '.join(command)))
    check(code == 0, 'aica_dsp_guest_oracle ended with exit code %d: %s\n%s' % (code, ' '.join(command), said))
    found = [line[len('dsp='):] for line in said.splitlines() if re.match(r'^dsp=[0-9a-f]{16}$', line)]
    check(len(found) == 1, 'aica_dsp_guest_oracle printed no line dsp=<hex>, or more than one:\n' + said)

    check(fields['dsp'] == found[0], 'the DSP of the copy of the guest whose sends are 0 is not the DSP of the caique model: the line has dsp=%s, '
          'aica_dsp_guest_oracle computes dsp=%s\n%s\n%s' % (fields['dsp'], found[0], text, said), copy.log)
    (plain, plain_text, plain_fields), = lines.get(1)
    check(plain_fields['dsp'] != found[0], 'the guest, whose first channel sends to the DSP, prints the dsp of the copy whose sends are 0, %s: '
          'the comparison does not see the sends\n' % found[0] + plain_text, plain.log)

    for line in said.splitlines():
        print('aica_dsp_guest_oracle: ' + line)
    print('the copy with IMXL 0 for the first channel (ISEL %d): dsp=%s, the DSP of the caique model on the guest\'s writes and schedule; %s'
          % (value & 15, fields['dsp'], text))
    print('and the guest, whose first channel sends, prints another: dsp=%s' % plain_fields['dsp'])


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

    # where the assemblers are: the committed files are what the sources give. gen.sh is a bash script, and on
    # Windows bash is WSL's launcher: nothing there looks for a shell
    why = emurun.no_shell()
    check(why is not None or not args.no_posix_shell, 'the host has no POSIX shell (--no-posix-shell), and the mode looks for one '
          'to run gen.sh with')
    if why is not None:
        print('gen.sh --check was not run: ' + why)
        return
    bash = shutil.which('bash')
    if bash is None:
        print('gen.sh --check was not run: no bash')
        return
    # its output goes to a file of the work directory, or of a scratch directory of its own when the mode has none
    work = args.work or tempfile.mkdtemp(prefix='soundlines-files-')
    try:
        said, code = tool_run(args, [bash, os.path.join(directory, 'gen.sh'), '--check'], os.path.join(work, 'gen-check.txt'))
    finally:
        if not args.work:
            shutil.rmtree(work, ignore_errors=True)
    said = said.strip()
    check(code is not None, 'gen.sh --check did not end within %d s' % args.watchdog)
    if code == 3:
        print('gen.sh --check was not run: ' + said)
        return
    check(code == 0, 'gen.sh --check ended with exit code %d:\n%s' % (code, said))
    print(said)


def build_tests(directory, names):
    """The names of the tests the CTestTestfile.cmake of a build directory registers, and of those of the
    directories it names, in their order. They are read, not asked of ctest: a ctest started inside a test of the
    same build directory writes its log over that of the ctest that runs the test."""
    path = os.path.join(directory, 'CTestTestfile.cmake')
    check(os.path.isfile(path), 'there is no ' + path)
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f.read().splitlines():
            test = re.match(r'\s*add_test\(\s*(?:\[(=*)\[(.*?)\]\1\]|"([^"]*)"|([^\s()"]+))', line)
            if test:
                names.append(test.group(2) or test.group(3) or test.group(4))
            sub = re.match(r'\s*subdirs\(\s*"([^"]*)"\s*\)', line)
            if sub:
                build_tests(os.path.join(directory, sub.group(1)), names)
    return names


def render_named(name):
    """A test named after a render of the sound: a test of the sound (debugger.sound.*) or of the session arm7 of the
    selftests (debugger.selftest.arm7, debugger.selftest.arm7.*), where the tests of the two renders were, with a
    part batch or persample behind those first parts. Another test's name may have the words: they name something
    else there (a batch of requests of the debugger's protocol)."""
    parts = name.split('.')
    ours = parts[:2] == ['debugger', 'sound'] or parts[:3] == ['debugger', 'selftest', 'arm7']
    return ours and bool(set(parts[2:]) & set(RENDER_PARTS))


def mode_names(args, lines):
    """No test of the sound or of the session arm7 of the selftests is named after a render of the sound: the tests
    that ran in both had .batch or .persample in their names, and those that stayed lost it (render_named)."""
    check(args.build, 'the mode names needs --build')
    names = build_tests(os.path.abspath(args.build), [])
    sound = [name for name in names if name.startswith('debugger.sound.')]
    check(sound, 'the build in %s registers no test debugger.sound.*: it is not the build of the tests' % args.build)
    old = [name for name in names if render_named(name)]
    check(not old, '%d of the %d tests are named after a render of the sound: %s' % (len(old), len(names), ', '.join(old)))
    print('%d tests, %d of them debugger.sound.*: none of those and of debugger.selftest.arm7* has a part %s'
          % (len(names), len(sound), ' or '.join(RENDER_PARTS)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    modes = {'same': mode_same, 'sounds': mode_sounds, 'muted': mode_muted, 'ignored': mode_ignored, 'engines': mode_engines,
             'record': mode_record, 'compare': mode_compare, 'neutral': mode_neutral, 'release': mode_release, 'held': mode_held,
             'heard': mode_heard, 'oracle': mode_oracle, 'silent': mode_silent, 'files': mode_files, 'names': mode_names}
    parser.add_argument('--mode', action='append', required=True, choices=sorted(modes), help='one or more; they share their runs')
    parser.add_argument('--emulator', default='')
    parser.add_argument('--work', default='')
    parser.add_argument('--guest', default='', help='the program: dsptest.elf of this directory, or another')
    parser.add_argument('--ticks', type=int, default=0, help='N: config:Debug.AicaHash')
    parser.add_argument('--engine', default=None, choices=ENGINES, help='default jit; record, compare: both')
    parser.add_argument('--fields', default='samples,regs,arm7,dsp,ticks,cycles', help='engines, release: the fields the lines agree in (engines: and dsp)')
    parser.add_argument('--file', default='', help='record, compare: the file of lines, outside of the tree')
    parser.add_argument('--note', action='append', default=[], help='record: a comment line for the file (the commit, the build)')
    parser.add_argument('--runs', type=int, default=2, help='record, compare: the runs of each kind in each configuration')
    parser.add_argument('--vblanks', type=int, default=0, help='V: config:Exit.AfterVblanks of the runs for the exit line (record, compare, neutral)')
    parser.add_argument('--selftest', default='', help='neutral: the filter of the session')
    parser.add_argument('--expect', action='append', default=[], help='neutral: text a line of the session has to have')
    parser.add_argument('--ignored', action='append', default=[], help='ignored: section:key=value, a key this build does not read')
    parser.add_argument('--build', default='', help='names: the build directory of the tests')
    parser.add_argument('--config', action='append', default=[], help='section:key=value, for every run')
    parser.add_argument('--oracle', default='', help='oracle: aica_dsp_guest_oracle, the tool of tests/aica_dsp')
    parser.add_argument('--no-posix-shell', action='store_true', help='files: the host has no POSIX shell (Windows): the mode fails '
                                                                         'when it looks for one to run gen.sh with')
    parser.add_argument('--dir', default=os.path.dirname(os.path.abspath(__file__)), help='files, release, held, heard, oracle: the directory of the guest, with its manifest and its labels')
    parser.add_argument('--watchdog', type=int, default=300, help='seconds a run may take')
    parser.add_argument('--keep', action='store_true', help='keep the work directory of a test that passed')
    args = parser.parse_args()

    runs_emulator = [mode for mode in args.mode if mode not in ('files', 'names')]
    if runs_emulator:
        for name in ('emulator', 'work', 'guest'):
            if not getattr(args, name):
                parser.error('the mode %s needs --%s' % (runs_emulator[0], name))
        if args.ticks <= 0 or args.runs <= 0:
            parser.error('the mode %s needs --ticks, and --runs is at least 1' % runs_emulator[0])

        # nulldc3 is nulldc3.exe on Windows
        args.emulator = emurun.executable(os.path.abspath(args.emulator))
        args.work = os.path.abspath(args.work)
        args.guest = os.path.abspath(args.guest)
        if args.oracle:
            args.oracle = os.path.abspath(args.oracle)

        if not os.path.isfile(args.emulator):
            print('FAIL: no emulator at ' + args.emulator)
            return 1
        if not os.path.isfile(args.guest):
            print('FAIL: no program at ' + args.guest)
            return 1

    # one engine, but for record and compare, which take both unless one is named
    if not set(args.mode) & {'record', 'compare'}:
        args.engine = args.engine or 'jit'

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
