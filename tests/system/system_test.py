#!/usr/bin/env python3
"""System tests: the emulator binary itself, headless, on guest programs.

    system_test.py --emulator <binary> --case <case> --work <directory>
                   [--elf-dir <directory with 2ndmix.elf and hello.elf>] [--shim <fault_shim library>]
                   [--ta-hle] [--watchdog <seconds>] [--keep]

One case per invocation, exit code 0 when it passes. README.md has the cases. Every run of the emulator gets
an empty environment but for fresh HOME / XDG_CONFIG_HOME / XDG_DATA_HOME directories below --work (no DISPLAY:
the runs are headless), a core file limit of 1 byte and a wall-clock watchdog that kills it and fails the case.
"""

import argparse
import json
import os
import re
import resource
import shutil
import signal
import struct
import subprocess
import sys
import time
import zlib
from concurrent.futures import ThreadPoolExecutor

# The guest programs of guest/*.s, assembled (README.md has how). Raw images for 8C010000.
GUESTS = {
    # prints "guest: returning to the BIOS", loops 2,000,000 times, returns to the BIOS
    'guest_exit': '08c7036105d21460082002890022faaf090003d31043fd8b0b0009000c00e8ff80841e00'
                  '67756573743a2072657475726e696e6720746f207468652042494f530a000900',
    # prints "guest: running", then counts in the word at 8C020000 for ever
    'guest_spin': '09c7036106d21460082002890022faaf090004d100e301733221fcaf090009000c00e8ff0000028c'
                  '67756573743a2072756e6e696e670a00',
    # prints "rtc=" and the real-time clock as 8 hex digits, returns to the BIOS
    'guest_rtc': '10d44265415628456d666b250ed272e0002274e0002263e000223de0002208e70ae1044504450445044553600fc9'
                 '1230008b0770307000221047f28b0ae000220b000900000071a00c00e8ff',
}

# A run that prints one of these did not go well, whatever its exit code says
FORBIDDEN = ('Fatal error', 'Verify Failed', 'fault_handler')

# 1/1/2000 00:00:00 as seconds since 1970, and what the RTC register has to hold for it (seconds since 1/1/1950)
RTC_PIN = 946684800
RTC_PIN_REGISTER = RTC_PIN + (20 * 365 + 5) * 24 * 60 * 60

WATCHDOG = 120      # seconds of wall clock for one run of the emulator (--watchdog), generous: the machine may be busy


class Failure(Exception):
    pass


class Run:
    """One finished (or killed) run of the emulator."""

    def __init__(self, tag, args, returncode, hung, log, directory, seconds):
        self.tag = tag
        self.args = args
        self.returncode = returncode    # negative: ended by that signal
        self.hung = hung
        self.log = log
        self.directory = directory
        self.seconds = seconds
        self.exit_line = None
        self.fields = {}

        m = re.search(r'^nulldc-exit: (.*)$', log, re.M)
        if m:
            self.exit_line = m.group(0)
            # key=value pairs, a value is a word or a quoted string
            for key, quoted, word in re.findall(r'(\w+)=(?:"([^"]*)"|(\S+))', m.group(1)):
                self.fields[key] = quoted or word

    def describe(self):
        tail = '\n'.join('    | ' + line for line in self.log.splitlines()[-25:])
        return '  run %s: %s\n  exit code %s%s, %.1f s, log %s\n%s' % (
            self.tag, ' '.join(self.args), self.returncode, ' (killed by the watchdog)' if self.hung else '',
            self.seconds, os.path.join(self.directory, 'log.txt'), tail)


class Harness:
    def __init__(self, options):
        self.options = options
        self.work = os.path.abspath(options.work)
        self.runs = []

        shutil.rmtree(self.work, ignore_errors=True)
        os.makedirs(self.work)

    def guest(self, name):
        path = os.path.join(self.work, name + '.bin')
        if not os.path.exists(path):
            with open(path, 'wb') as f:
                f.write(bytes.fromhex(GUESTS[name]))
        return path

    def elf(self, name):
        path = os.path.join(self.options.elf_dir or '', name)
        if not self.options.elf_dir or not os.path.isfile(path):
            raise Failure('no %s in --elf-dir (%s)' % (name, self.options.elf_dir))
        return path

    def start(self, tag, keys, program=None, extra_args=(), env=None, seed_config=None):
        """Starts the emulator. keys are "section:key=value" strings. Returns what finish() takes."""
        directory = os.path.join(self.work, tag)
        for sub in ('home', 'config', 'data'):
            os.makedirs(os.path.join(directory, sub))

        if seed_config is not None:
            os.makedirs(os.path.join(directory, 'config', 'reicast'))
            with open(os.path.join(directory, 'config', 'reicast', 'emu.cfg'), 'w') as f:
                f.write(seed_config)

        args = [self.options.emulator]
        if keys:
            args.append('-config')
            for i, key in enumerate(keys):
                if i:
                    args.append(',')
                args.append(key)
        args.extend(extra_args)
        if program:
            args.append(program)

        environment = {
            'HOME': os.path.join(directory, 'home'),
            'XDG_CONFIG_HOME': os.path.join(directory, 'config'),
            'XDG_DATA_HOME': os.path.join(directory, 'data'),
        }
        environment.update(env or {})

        def limit_core():
            # 1 byte, not 0: the one value with which the kernel does not start a piped core handler either
            resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))

        log = open(os.path.join(directory, 'log.txt'), 'wb')
        process = subprocess.Popen(args, cwd=directory, env=environment, stdin=subprocess.DEVNULL, stdout=log,
                                   stderr=subprocess.STDOUT, preexec_fn=limit_core, start_new_session=True)
        return (tag, args, process, log, directory, time.time())

    def finish(self, started, watchdog=None, kill_when=None):
        """Waits for the run. kill_when(log text) -> True ends a run that is not meant to end by itself."""
        tag, args, process, log, directory, t0 = started
        watchdog = watchdog or self.options.watchdog
        hung = False
        killed_on_purpose = False

        while True:
            try:
                process.wait(timeout=0.2 if kill_when else max(0.1, t0 + watchdog - time.time()))
                break
            except subprocess.TimeoutExpired:
                pass

            if kill_when and kill_when(self.read_log(directory)):
                killed_on_purpose = True
            elif time.time() - t0 >= watchdog:
                hung = True
            else:
                continue

            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            break

        log.close()
        run = Run(tag, args, process.returncode, hung, self.read_log(directory), directory, time.time() - t0)
        run.killed_on_purpose = killed_on_purpose
        self.runs.append(run)
        return run

    @staticmethod
    def read_log(directory):
        with open(os.path.join(directory, 'log.txt'), 'rb') as f:
            return f.read().decode('utf-8', 'replace')

    def run(self, tag, keys, program=None, watchdog=None, **kwargs):
        return self.finish(self.start(tag, keys, program, **kwargs), watchdog)

    def config_file(self, run):
        path = os.path.join(run.directory, 'config', 'reicast', 'emu.cfg')
        with open(path) as f:
            return f.read()


def headless(*keys, engine='jit', rtc=RTC_PIN):
    """The keys of a headless run: no audio backend, the serial port on stdout, a pinned clock."""
    base = ['config:Headless=yes', 'audio:backend=none', 'config:Debug.SerialConsoleEnabled=yes',
            'config:Dynarec.Enabled=' + ('yes' if engine == 'jit' else 'no')]
    if rtc is not None:
        base.append('config:Dreamcast.RTC=%d' % rtc)
    return base + list(keys)


def check(condition, message, run=None):
    if not condition:
        raise Failure(message + ('\n' + run.describe() if run else ''))


def check_clean(run, reason, exit_code=0):
    """The run ended by itself, the way it was asked to, without a complaint in its output."""
    check(not run.hung, 'the run did not end: killed by the watchdog', run)
    check(run.returncode == exit_code, 'exit code %s, expected %d' % (run.returncode, exit_code), run)
    for text in FORBIDDEN:
        check(text not in run.log, 'the output has "%s"' % text, run)
    check(run.exit_line is not None, 'no nulldc-exit line', run)
    check(run.fields.get('reason') == reason, 'reason=%s, expected %s' % (run.fields.get('reason'), reason), run)


def check_exit_line(run):
    """The fields every exit line of a run that ran has, as numbers."""
    for field in ('emu_s', 'vblanks', 'wall_s', 'speed_pct'):
        check(field in run.fields, 'the exit line has no %s' % field, run)
    try:
        return float(run.fields['emu_s']), int(run.fields['vblanks']), float(run.fields['wall_s']), \
               float(run.fields['speed_pct'])
    except ValueError:
        raise Failure('the exit line does not parse\n' + run.describe())


HASH_FIELDS = ('hash', 'sh4', 'sched', 'ram', 'vram', 'aram', 'arm7')


def check_hash(run):
    for field in HASH_FIELDS:
        check(field in run.fields, 'the exit line has no %s' % field, run)
    for field in ('hash', 'sh4', 'ram', 'vram', 'aram', 'arm7'):
        check(re.fullmatch(r'[0-9a-f]{16}', run.fields[field]) is not None, '%s is not 16 hex digits' % field, run)
    return run.fields['hash']


# ---------------------------------------------------------------------------------------------------------------------
# The cases


def case_help(h):
    """-help prints the usage text and exits with 0, with no display and no config."""
    run = h.run('help', [], extra_args=['-help'])
    check(not run.hung, '-help did not end', run)
    check(run.returncode == 0, 'exit code %s' % run.returncode, run)
    check('Usage:' in run.log and '-config' in run.log, 'no usage text', run)
    for text in FORBIDDEN:
        check(text not in run.log, 'the output has "%s"' % text, run)


def case_guest_exit(h, engine):
    """A program that returns to the BIOS ends the run: its text is on stdout, reason=guest-exit, exit code 0."""
    run = h.run('guest_exit', headless('config:Exit.StateHash=yes', engine=engine), h.guest('guest_exit'))
    check_clean(run, 'guest-exit')
    check('guest: returning to the BIOS' in run.log, 'the text of the guest is not in the output', run)
    check('REIOS: Exit' in run.log, 'no "REIOS: Exit"', run)
    check_exit_line(run)
    check_hash(run)
    check(('Using MCPU Recompiler' if engine == 'jit' else 'Using MCPU Interpreter') in run.log,
          'not the %s' % engine, run)


def case_exit_vblanks(h, engine):
    """Exit.AfterVblanks = N ends a program that never ends at vblank N."""
    run = h.run('exit_vblanks', headless('config:Exit.AfterVblanks=20', 'config:Exit.StateHash=yes', engine=engine),
                h.guest('guest_spin'))
    check_clean(run, 'vblanks')
    check('guest: running' in run.log, 'the text of the guest is not in the output', run)
    emu_s, vblanks, wall_s, speed = check_exit_line(run)
    check(vblanks == 20, 'vblanks=%d, expected 20' % vblanks, run)
    # 20 vblanks of the mode a reset leaves are a third of a second, give or take the mode
    check(0.2 < emu_s < 0.5, 'emu_s=%f for 20 vblanks' % emu_s, run)
    check(wall_s > 0 and speed > 0, 'no wall time or speed', run)
    check_hash(run)


def case_exit_seconds(h):
    """Exit.AfterSeconds = S ends the run at the first vblank at or after S emulated seconds."""
    run = h.run('exit_seconds', headless('config:Exit.AfterSeconds=0.25', engine='interp'), h.guest('guest_spin'))
    check_clean(run, 'seconds')
    emu_s, vblanks, wall_s, speed = check_exit_line(run)
    check(0.25 <= emu_s < 0.25 + 0.021, 'emu_s=%f, expected the first vblank from 0.25 on' % emu_s, run)
    check('hash' not in run.fields, 'a state hash that was not asked for', run)


def case_rtc(h):
    """Dreamcast.RTC pins the clock the guest reads; another value is another clock."""
    for engine in ('jit', 'interp'):
        run = h.run('rtc_' + engine, headless(engine=engine), h.guest('guest_rtc'))
        check_clean(run, 'guest-exit')
        check('rtc=%08X' % RTC_PIN_REGISTER in run.log, 'the guest did not read %08X' % RTC_PIN_REGISTER, run)

    run = h.run('rtc_other', headless(rtc=RTC_PIN + 86400), h.guest('guest_rtc'))
    check_clean(run, 'guest-exit')
    check('rtc=%08X' % (RTC_PIN_REGISTER + 86400) in run.log, 'the guest did not read the other clock', run)

    # without the key the clock is the host's: this century, in any time zone
    run = h.run('rtc_host', headless(rtc=None), h.guest('guest_rtc'))
    check_clean(run, 'guest-exit')
    m = re.search(r'rtc=([0-9A-F]{8})', run.log)
    check(m is not None, 'the guest printed no clock', run)
    host = time.time() + (20 * 365 + 5) * 24 * 60 * 60
    check(abs(int(m.group(1), 16) - host) < 2 * 24 * 60 * 60, 'the unpinned clock is not the time of the host', run)


def case_cfg_untouched(h):
    """A headless run with -config overrides leaves emu.cfg byte for byte as it was."""
    seed = '[config]\nDynarec.Enabled = yes\npvr.backend = refsw2\nDebug.SerialConsoleEnabled = no\n\n' \
           '[audio]\nbackend = auto\n\n'
    run = h.run('cfg', headless('config:pvr.backend=none', 'config:Exit.AfterVblanks=5', engine='interp'),
                h.guest('guest_spin'), seed_config=seed)
    check_clean(run, 'vblanks')
    check('Using MCPU Interpreter' in run.log and 'RendIF: Using renderer: none' in run.log,
          'the overrides were not in effect', run)
    after = h.config_file(run)
    check(after == seed, 'emu.cfg changed:\n--- before\n%s--- after\n%s' % (seed, after), run)

    # and a config directory that had no emu.cfg gets no settings either
    run = h.run('cfg_fresh', headless('config:Exit.AfterVblanks=5'), h.guest('guest_spin'))
    check_clean(run, 'vblanks')
    check(h.config_file(run).strip() == '', 'settings were written to a fresh emu.cfg', run)


def case_headless_no_program(h):
    """Headless with nothing to run is an error: exit code 1 and an error line, not a wait for a user."""
    run = h.run('no_program', headless())
    check(not run.hung, 'the run did not end', run)
    check(run.returncode == 1, 'exit code %s, expected 1' % run.returncode, run)
    check(run.fields.get('reason') == 'error', 'no error line', run)


def case_headless_gl_renderer(h):
    """A renderer that needs a GL context is refused in headless mode: exit code 1 and an error line."""
    backend = 'gles' if h.options.ta_hle else 'refsw'
    run = h.run('gl_renderer', headless('config:pvr.backend=' + backend, 'config:Exit.AfterVblanks=5'),
                h.guest('guest_spin'))
    check(not run.hung, 'the run did not end', run)
    check(run.returncode == 1, 'exit code %s, expected 1' % run.returncode, run)
    check(run.fields.get('reason') == 'error' and backend in run.fields.get('error', ''), 'no error line', run)
    for text in FORBIDDEN:
        check(text not in run.log, 'the output has "%s"' % text, run)


BOOT = {
    # program: (vblanks, texts the serial port has to show)
    '2ndmix': (600, ('KallistiOS', '2ndMix/KallistiOS starting', 'Starting display')),
    # hello returns into its own start-up code after main (its saved PR is overwritten by its stack) and stays
    # there, slowly: the run is cut soon after the text
    'hello': (30, ('KallistiOS', 'Hello world!', 'arch: exit return code 0')),
}


def case_boot(h, program, engine):
    """A KallistiOS program boots under reios and runs for a fixed number of vblanks."""
    vblanks, texts = BOOT[program]
    run = h.run('boot', headless('config:pvr.backend=none', 'config:Exit.AfterVblanks=%d' % vblanks,
                                 'config:Exit.StateHash=yes', engine=engine), h.elf(program + '.elf'))
    check_clean(run, 'vblanks')
    for text in texts:
        check(text in run.log, 'the serial port did not show "%s"' % text, run)
    check(('Using MCPU Recompiler' if engine == 'jit' else 'Using MCPU Interpreter') in run.log,
          'not the %s' % engine, run)
    check('RendIF: Using renderer: none' in run.log, 'not the none renderer', run)
    emu_s, count, wall_s, speed = check_exit_line(run)
    check(count == vblanks, 'vblanks=%d, expected %d' % (count, vblanks), run)
    check_hash(run)
    print('  %s, %s: %.2f emulated seconds at %.0f %% of real time' % (program, engine, emu_s, speed))


def read_png_size(path):
    """Width and height of a PNG whose chunks all have a valid CRC."""
    with open(path, 'rb') as f:
        data = f.read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise Failure('%s is not a PNG' % path)
    position = 8
    size = None
    chunks = []
    while position < len(data):
        length, kind = struct.unpack('>I4s', data[position:position + 8])
        body = data[position + 8:position + 8 + length]
        crc, = struct.unpack('>I', data[position + 8 + length:position + 12 + length])
        if zlib.crc32(kind + body) & 0xffffffff != crc:
            raise Failure('%s: chunk %s has a wrong CRC' % (path, kind))
        if kind == b'IHDR':
            size = struct.unpack('>II', body[:8])
        chunks.append(kind)
        position += 12 + length
    if size is None or chunks[-1] != b'IEND' or b'IDAT' not in chunks:
        raise Failure('%s is not a complete PNG' % path)
    return size


def case_frame_refsw2(h):
    """The software reference renderer runs headless: the frame it presents is hashed and written as a PNG."""
    png = os.path.join(h.work, 'frame.png')
    keys = headless('config:pvr.backend=refsw2', 'config:Exit.AfterVblanks=240', 'config:Exit.StateHash=yes')
    run = h.run('frame', keys + ['config:Exit.FramePng=' + png], h.elf('2ndmix.elf'))
    check_clean(run, 'vblanks')
    check('RendIF: Using renderer: refsw2' in run.log, 'not refsw2', run)
    check(run.fields.get('frame_size') == '640x480', 'frame_size=%s' % run.fields.get('frame_size'), run)
    check(re.fullmatch(r'[0-9a-f]{16}', run.fields.get('frame', '')) is not None, 'no frame hash', run)
    check(os.path.isfile(png), 'no PNG was written', run)
    check(read_png_size(png) == (640, 480), 'the PNG is not 640x480', run)
    # a frame of one colour compresses to a few hundred bytes: this one has the stars and the logo of the demo
    check(os.path.getsize(png) > 4000, 'the PNG is %d bytes: an empty frame' % os.path.getsize(png), run)

    # the renderer changes what the guest can see only in VRAM, where it draws
    none = h.run('frame_none', headless('config:pvr.backend=none', 'config:Exit.AfterVblanks=240',
                                        'config:Exit.StateHash=yes', 'config:Exit.FrameHash=yes'),
                 h.elf('2ndmix.elf'))
    check_clean(none, 'vblanks')
    check(none.fields.get('frame') == 'none', 'the none renderer presented a frame', none)
    for field in ('sh4', 'sched', 'ram', 'aram', 'arm7'):
        check(none.fields[field] == run.fields[field], '%s differs between refsw2 and none' % field, run)
    check(none.fields['vram'] != run.fields['vram'], 'refsw2 left VRAM as the none renderer does', run)


def fault_environment(h, kind):
    check(h.options.shim and os.path.isfile(h.options.shim), 'no fault shim (--shim)')
    return {'LD_PRELOAD': h.options.shim, 'NULLDC_TEST_FAULT': kind}


def case_fault(h, kind):
    """A fault that is not a guest memory fault ends the process: a diagnostic, and a status that is not 0."""
    name = {'segv': 'SIGSEGV', 'bus': 'SIGBUS', 'ill': 'SIGILL'}[kind]
    run = h.run('fault', headless(engine='interp'), h.guest('guest_spin'), env=fault_environment(h, kind))
    check(not run.hung, 'the process did not end after the fault: killed by the watchdog', run)
    check(run.returncode == -getattr(signal, name), 'exit code %s, expected the end by %s' % (run.returncode, name),
          run)
    check(re.search(r'fault_handler: unhandled %s at address 0x[0-9a-f]+, pc 0x[0-9a-f]+.*terminating' % name,
                    run.log) is not None, 'no diagnostic of the fault', run)
    check('Blocking' not in run.log, 'the process blocked', run)
    # what was printed before the fault is not lost with the process
    check('guest: running' in run.log, 'the output in front of the fault is missing', run)


def case_fault_block(h):
    """NULLDC_FAULT_BLOCK=1: the process stays, for a debugger, and says so."""
    env = fault_environment(h, 'segv')
    env['NULLDC_FAULT_BLOCK'] = '1'
    started = h.start('fault_block', headless(engine='interp'), h.guest('guest_spin'), env=env)
    run = h.finish(started, kill_when=lambda log: log.count('fault_handler: Blocking') >= 2)
    check(run.killed_on_purpose, 'the process did not block after the fault', run)
    check('blocking for a debugger' in run.log, 'no diagnostic of the fault', run)

    # the config key does the same for a fault after the config is loaded
    started = h.start('fault_block_key', headless('config:Debug.BlockOnFault=yes', engine='interp'),
                      h.guest('guest_spin'), env=fault_environment(h, 'segv'))
    run = h.finish(started, kill_when=lambda log: log.count('fault_handler: Blocking') >= 2)
    check(run.killed_on_purpose, 'the process did not block after the fault (config key)', run)


def case_determinism(h):
    """The same run gives the same state hash every time, with the JIT and with the interpreter."""
    repeats = 6
    plan = [(engine, i) for engine in ('jit', 'interp') for i in range(repeats)]

    def one(item):
        engine, i = item
        keys = headless('config:pvr.backend=none', 'config:Exit.AfterVblanks=600', 'config:Exit.StateHash=yes',
                        engine=engine)
        return engine, h.finish(h.start('%s_%d' % (engine, i), keys, h.elf('2ndmix.elf')))

    with ThreadPoolExecutor(4) as pool:
        results = list(pool.map(one, plan))

    for engine in ('jit', 'interp'):
        runs = [run for e, run in results if e == engine]
        for run in runs:
            check_clean(run, 'vblanks')
            check_hash(run)
        first = runs[0]
        for run in runs[1:]:
            differing = [f for f in HASH_FIELDS if run.fields[f] != first.fields[f]]
            check(not differing, '%s: run %s differs from run %s in %s\n  %s\n  %s' % (
                engine, run.tag, first.tag, ', '.join(differing), first.exit_line, run.exit_line), run)
        print('  %s: %d runs, hash %s' % (engine, len(runs), first.fields['hash']))


def case_perf_script(h):
    """tests/perf/perf.py on a program of the suite: for each engine a speed by wall clock and one by CPU time."""
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, 'perf', 'perf.py')
    output = os.path.join(h.work, 'perf.json')
    args = [sys.executable, script, '--emulator', h.options.emulator, '--program', h.guest('guest_spin'),
            '--seconds', '0.5', '--runs', '3', '--json', output]
    try:
        done = subprocess.run(args, cwd=h.work, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, timeout=4 * h.options.watchdog)
    except subprocess.TimeoutExpired:
        raise Failure('perf.py did not end: %s' % ' '.join(args))
    log = done.stdout.decode('utf-8', 'replace')

    def expect(condition, message):
        check(condition, '%s\n  %s\n%s' % (message, ' '.join(args), '\n'.join('    | ' + l for l in log.splitlines())))

    expect(done.returncode == 0, 'perf.py: exit code %s' % done.returncode)
    expect(os.path.isfile(output), 'perf.py wrote no JSON')
    with open(output) as f:
        result = json.load(f)

    try:
        expect([r['engine'] for r in result['results']] == ['jit', 'interp'], 'not one result per engine')
        for r in result['results']:
            expect(r['renderer'] == 'none', 'not the none renderer')
            for figure in ('speed_pct', 'speed_cpu_pct'):
                runs = r[figure + '_runs']
                expect(len(runs) == 3 and all(x > 0 for x in runs), '%s: no %s of 3 runs' % (r['engine'], figure))
                expect(r[figure + '_min'] == min(runs) and r[figure + '_max'] == max(runs) and
                       r[figure + '_median'] == sorted(runs)[1],
                       '%s: %s is not the median, lowest and highest of its runs' % (r['engine'], figure))
            # the speed by CPU time is the emulated time of the run over the CPU time of its process
            for emu, cpu, speed in zip(r['emu_s_runs'], r['cpu_s_runs'], r['speed_cpu_pct_runs']):
                expect(0.5 <= emu < 0.5 + 0.021, '%s: a run of %f emulated seconds' % (r['engine'], emu))
                expect(cpu > 0 and abs(speed - emu / cpu * 100) < 1e-6, '%s: %f %% by CPU time for %f emulated '
                       'seconds on %f CPU seconds' % (r['engine'], speed, emu, cpu))
            expect(len(r['load_1m_runs']) == 3, '%s: no load of the machine per run' % r['engine'])
    except KeyError as missing:
        expect(False, 'the JSON has no %s' % missing)

    # and both are in the table
    header = [line for line in log.splitlines() if line.startswith('engine')]
    expect(len(header) == 1 and 'median %' in header[0] and 'CPU median %' in header[0], 'no table with both figures')
    print('  ' + '\n  '.join(line for line in log.splitlines() if re.match(r'engine|jit |interp ', line)))


CASES = {
    'help': case_help,
    'guest_exit.jit': lambda h: case_guest_exit(h, 'jit'),
    'guest_exit.interp': lambda h: case_guest_exit(h, 'interp'),
    'exit.vblanks.jit': lambda h: case_exit_vblanks(h, 'jit'),
    'exit.vblanks.interp': lambda h: case_exit_vblanks(h, 'interp'),
    'exit.seconds': case_exit_seconds,
    'rtc': case_rtc,
    'cfg.untouched': case_cfg_untouched,
    'headless.no_program': case_headless_no_program,
    'headless.gl_renderer': case_headless_gl_renderer,
    'boot.2ndmix.jit': lambda h: case_boot(h, '2ndmix', 'jit'),
    'boot.2ndmix.interp': lambda h: case_boot(h, '2ndmix', 'interp'),
    'boot.hello.jit': lambda h: case_boot(h, 'hello', 'jit'),
    'boot.hello.interp': lambda h: case_boot(h, 'hello', 'interp'),
    'frame.refsw2': case_frame_refsw2,
    'fault.segv': lambda h: case_fault(h, 'segv'),
    'fault.bus': lambda h: case_fault(h, 'bus'),
    'fault.ill': lambda h: case_fault(h, 'ill'),
    'fault.block': case_fault_block,
    'determinism': case_determinism,
    'perf.script': case_perf_script,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--emulator', required=True)
    parser.add_argument('--case', required=True, choices=sorted(CASES))
    parser.add_argument('--work', required=True, help='scratch directory of this case, emptied first')
    parser.add_argument('--elf-dir', help='directory with 2ndmix.elf and hello.elf')
    parser.add_argument('--shim', help='the fault_shim library, for the fault cases')
    parser.add_argument('--ta-hle', action='store_true', help='the emulator is a -DHAS_TA_LLE=OFF build')
    parser.add_argument('--watchdog', type=int, default=WATCHDOG,
                        help='seconds of wall clock one run of the emulator gets (%d)' % WATCHDOG)
    parser.add_argument('--keep', action='store_true', help='keep the directories of the runs of a passed case')
    options = parser.parse_args()

    if not os.path.isfile(options.emulator):
        print('FAIL %s: no emulator at %s' % (options.case, options.emulator))
        return 1

    harness = Harness(options)
    try:
        CASES[options.case](harness)
    except Failure as failure:
        print('FAIL %s: %s' % (options.case, failure))
        return 1

    print('PASS %s (%d run%s of the emulator, %.1f s)' % (
        options.case, len(harness.runs), '' if len(harness.runs) == 1 else 's', sum(r.seconds for r in harness.runs)))
    if not options.keep:
        shutil.rmtree(harness.work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
