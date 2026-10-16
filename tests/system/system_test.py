#!/usr/bin/env python3
"""System tests: the emulator binary itself, headless, on guest programs.

    system_test.py --emulator <binary> --case <case> --work <directory>
                   [--elf-dir <directory with 2ndmix.elf and hello.elf>] [--shim <fault_shim library>]
                   [--ta-hle] [--watchdog <seconds>] [--keep]

One case per invocation, exit code 0 when it passes. README.md has the cases. Every run of the emulator keeps
its files in a directory of its own below --work (--portable <run>/portable), has an empty environment (no
DISPLAY: the runs are headless), a core file limit of 1 byte and a wall-clock watchdog that kills it and fails
the case. The cases dirs.* run it without --portable as well, and with --shared.
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

# The product number in the IP.BIN of the disc image of the tests: the id of the game whose settings the emulator loads
GAME_ID = 'T-PIN01'

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

    def disc(self):
        """A disc image that has a product number and nothing else: the 256 sectors reios reads to find out which
        game is in the drive, as an .iso, with GAME_ID where an IP.BIN has it. The emulator loads the settings of
        that game; what it runs is given with reios:ElfFile."""
        path = os.path.join(self.work, 'disc.iso')
        if not os.path.exists(path):
            ip_bin = bytearray(256 * 2048)
            ip_bin[0:16] = b'SEGA SEGAKATANA '
            ip_bin[64:74] = GAME_ID.ljust(10).encode()
            with open(path, 'wb') as f:
                f.write(ip_bin)
        return path

    def elf(self, name):
        path = os.path.join(self.options.elf_dir or '', name)
        if not self.options.elf_dir or not os.path.isfile(path):
            raise Failure('no %s in --elf-dir (%s)' % (name, self.options.elf_dir))
        return path

    def start(self, tag, keys, program=None, extra_args=(), env=None, seed_config=None, portable=True, binary=None,
              cwd=None):
        """Starts the emulator. keys are "section:key=value" strings. portable: --portable <run>/portable in front
        of the rest, which is made; with seed_config as its emu.cfg. binary and cwd: another file to run than the
        emulator, and another working directory than the run's. Returns what finish() takes."""
        directory = os.path.join(self.work, tag)
        os.makedirs(directory)

        args = [binary or self.options.emulator]
        if portable:
            os.makedirs(os.path.join(directory, 'portable'))
            args += ['--portable', os.path.join(directory, 'portable')]
        if seed_config is not None:
            with open(os.path.join(directory, 'portable', 'emu.cfg'), 'w') as f:
                f.write(seed_config)

        if keys:
            args.append('--config')
            for i, key in enumerate(keys):
                if i:
                    args.append(',')
                args.append(key)
        args.extend(extra_args)
        if program:
            args.append(program)

        environment = dict(env or {})

        def limit_core():
            # 1 byte, not 0: the one value with which the kernel does not start a piped core handler either
            resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))

        log = open(os.path.join(directory, 'log.txt'), 'wb')
        process = subprocess.Popen(args, cwd=cwd or directory, env=environment, stdin=subprocess.DEVNULL, stdout=log,
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

    def wait_for_output(self, started, text):
        """Waits until the output of a started run has text. False: the run ended, or used up its watchdog time,
        without it."""
        tag, args, process, log, directory, t0 = started
        while True:
            over = process.poll() is not None or time.time() - t0 >= self.options.watchdog
            if text in self.read_log(directory):
                return True
            if over:
                return False
            time.sleep(0.02)

    @staticmethod
    def read_log(directory):
        with open(os.path.join(directory, 'log.txt'), 'rb') as f:
            return f.read().decode('utf-8', 'replace')

    def run(self, tag, keys, program=None, watchdog=None, **kwargs):
        return self.finish(self.start(tag, keys, program, **kwargs), watchdog)

    def config_file(self, run):
        path = os.path.join(run.directory, 'portable', 'emu.cfg')
        with open(path) as f:
            return f.read()


def limit_core():
    # 1 byte, not 0: the one value with which the kernel does not start a piped core handler either
    resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))


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


# What --help lists: every switch of a Linux build, each at the start of a line of its own (two blanks in front,
# a blank or the end of the line behind), with the form of the list behind --config and of the directory of
# --portable
HELP_SWITCHES = ('--config section:key=value [ , section:key=value ]...', '--portable <dir>', '--shared', '--help',
                 '--')
HELP_TEXTS = ('Usage: nulldc3 [switches] [game]', 'docs/nulldc3/automated-runs.md')


def case_help(h):
    """--help prints the usage text and exits with 0, with no display and no config: every switch and the form of
    the list of --config, in two-dash forms only. Nothing is made, not the directory of a --portable in front."""
    portable = os.path.join(h.work, 'help', 'not-made')
    run = h.run('help', [], extra_args=['--portable', portable, '--help'], portable=False)
    check(not run.hung, '--help did not end', run)
    check(run.returncode == 0, 'exit code %s' % run.returncode, run)
    for text in HELP_TEXTS:
        check(text in run.log, 'the usage text has no "%s"' % text, run)
    for switch in HELP_SWITCHES:
        check(re.search(r'^  %s(\s|$)' % re.escape(switch), run.log, re.M) is not None,
              'the usage text lists no "%s" (at the start of a line)' % switch, run)
    one_dash = re.findall(r'(?<![-\w])-(?:config|portable|shared|help)\b', run.log)
    check(not one_dash, 'the usage text has a one-dash switch: %s' % one_dash, run)
    check('Config dir is' not in run.log and 'nulldc-exit' not in run.log, 'a run after --help', run)
    check(not os.path.exists(portable), '--help made the directory of --portable', run)
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
    """Dreamcast.RTC pins the clock the guest reads; another value is another clock. Debug.FixedRtc pins it as well,
    and with both keys it is the one the guest reads."""
    for engine in ('jit', 'interp'):
        run = h.run('rtc_' + engine, headless(engine=engine), h.guest('guest_rtc'))
        check_clean(run, 'guest-exit')
        check('rtc=%08X' % RTC_PIN_REGISTER in run.log, 'the guest did not read %08X' % RTC_PIN_REGISTER, run)

    run = h.run('rtc_other', headless(rtc=RTC_PIN + 86400), h.guest('guest_rtc'))
    check_clean(run, 'guest-exit')
    check('rtc=%08X' % (RTC_PIN_REGISTER + 86400) in run.log, 'the guest did not read the other clock', run)

    # Debug.FixedRtc is the same pin under a second key: alone, and next to a Dreamcast.RTC of another second,
    # where it goes first
    for tag, days, dreamcast_rtc in (('rtc_fixed', 2, None), ('rtc_both', 3, RTC_PIN)):
        run = h.run(tag, headless('config:Debug.FixedRtc=%d' % (RTC_PIN + days * 86400), rtc=dreamcast_rtc),
                    h.guest('guest_rtc'))
        check_clean(run, 'guest-exit')
        check('rtc=%08X' % (RTC_PIN_REGISTER + days * 86400) in run.log,
              'the guest did not read the clock of Debug.FixedRtc', run)

    # without a key the clock is the host's: this century, in any time zone
    run = h.run('rtc_host', headless(rtc=None), h.guest('guest_rtc'))
    check_clean(run, 'guest-exit')
    m = re.search(r'rtc=([0-9A-F]{8})', run.log)
    check(m is not None, 'the guest printed no clock', run)
    host = time.time() + (20 * 365 + 5) * 24 * 60 * 60
    check(abs(int(m.group(1), 16) - host) < 2 * 24 * 60 * 60, 'the unpinned clock is not the time of the host', run)


def case_rtc_game(h):
    """The two keys that pin the clock are settings like any other: in the section of the game that is in the drive
    they go over the ones of section config, and Debug.FixedRtc, wherever it comes from, goes over Dreamcast.RTC."""
    a, b, c = RTC_PIN, RTC_PIN + 86400, RTC_PIN + 3 * 86400

    def game_run(tag, keys, sections, engine='jit'):
        seed = ''.join('[%s]\n%s\n' % (name, ''.join('%s = %d\n' % kv for kv in entries)) for name, entries in sections)
        run = h.run(tag, headless('config:bios.UseReios=1', 'reios:ElfFile=' + h.guest('guest_rtc'), *keys, engine=engine,
                                  rtc=None), h.disc(), seed_config=seed)
        check_clean(run, 'guest-exit')
        check('Game ID is [%s]' % GAME_ID in run.log, 'the emulator did not take the disc image for the game %s' % GAME_ID,
              run)
        m = re.search(r'rtc=([0-9A-F]{8})', run.log)
        check(m is not None, 'the guest printed no clock', run)
        return run, int(m.group(1), 16) - (RTC_PIN_REGISTER - RTC_PIN)

    dreamcast_rtc = lambda second: 'config:Dreamcast.RTC=%d' % second
    fixed_rtc = lambda second: 'config:Debug.FixedRtc=%d' % second

    for tag, keys, sections, want, what in (
            # Dreamcast.RTC of the game: alone, on both engines, and over the one of section config
            ('game_jit', [], [(GAME_ID, [('Dreamcast.RTC', b)])], b, 'Dreamcast.RTC of the game'),
            ('game_interp', [], [(GAME_ID, [('Dreamcast.RTC', b)])], b, 'Dreamcast.RTC of the game'),
            ('game_over_config', [dreamcast_rtc(a)], [(GAME_ID, [('Dreamcast.RTC', b)])], b,
             'Dreamcast.RTC of the game, not the one of section config'),
            ('game_over_config_file', [], [('config', [('Dreamcast.RTC', a)]), (GAME_ID, [('Dreamcast.RTC', b)])], b,
             'Dreamcast.RTC of the game, not the one of section config'),
            # the section of the game on the command line
            ('game_line', [dreamcast_rtc(a), '%s:Dreamcast.RTC=%d' % (GAME_ID, b)], [], b,
             'Dreamcast.RTC of the game, given with --config'),
            # Debug.FixedRtc of the game: over its own Dreamcast.RTC, and over both keys of section config
            ('game_fixed', [], [(GAME_ID, [('Debug.FixedRtc', c), ('Dreamcast.RTC', b)])], c, 'Debug.FixedRtc of the game'),
            ('game_fixed_over_config', [dreamcast_rtc(a), fixed_rtc(b)], [(GAME_ID, [('Debug.FixedRtc', c)])], c,
             'Debug.FixedRtc of the game, not a key of section config'),
            # Debug.FixedRtc of section config stays in front of a Dreamcast.RTC, also of one the game has
            ('config_fixed', [fixed_rtc(a)], [(GAME_ID, [('Dreamcast.RTC', b)])], a,
             'Debug.FixedRtc of section config, in front of Dreamcast.RTC of the game'),
            # a game that has other settings, and the settings of another game, leave the keys of section config
            ('game_without', [fixed_rtc(a), dreamcast_rtc(b)], [(GAME_ID, [('Dreamcast.Cable', 3)])], a,
             'Debug.FixedRtc of section config, the game has neither key'),
            ('other_game', [dreamcast_rtc(a)], [('T-OTHER', [('Dreamcast.RTC', b), ('Debug.FixedRtc', c)])], a,
             'Dreamcast.RTC of section config, the keys of another game are not for this one')):
        run, got = game_run('rtc_' + tag, keys, sections, engine='interp' if tag.endswith('_interp') else 'jit')
        check(got == want, 'the guest read the second %d, expected %d: %s' % (got, want, what), run)

    # 0 in the section of the game takes the pin of section config away: the clock is the host's
    run, got = game_run('rtc_game_zero', [dreamcast_rtc(a)], [(GAME_ID, [('Dreamcast.RTC', 0)])])
    check(abs(got - time.time()) < 2 * 24 * 60 * 60, 'Dreamcast.RTC = 0 for the game did not leave the clock to the host', run)


def case_cfg_untouched(h):
    """A headless run with --config overrides leaves emu.cfg byte for byte as it was."""
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


def start_spin(h, tag, keys=(), engine='interp', env=None):
    """Starts guest_spin and waits for the text of the guest: from there on the handlers are installed, the
    config is loaded and the machine runs, however long the emulator took to get there. Returns what finish()
    takes."""
    started = h.start(tag, headless(*keys, engine=engine), h.guest('guest_spin'), env=env)
    if not h.wait_for_output(started, 'guest: running'):
        check(False, 'the text of the guest did not come: no fault was asked for, no signal sent', h.finish(started))
    return started


def start_fault(h, tag, kind, keys=(), engine='interp', env=None):
    """start_spin() with the fault shim. Returns what finish() takes and the path of the file that makes the shim
    fault when it is created."""
    check(h.options.shim and os.path.isfile(h.options.shim), 'no fault shim (--shim)')
    request = os.path.join(h.work, tag, 'fault-now')
    environment = {'LD_PRELOAD': h.options.shim, 'NULLDC_TEST_FAULT': kind, 'NULLDC_TEST_FAULT_WHEN': request}
    environment.update(env or {})
    return start_spin(h, tag, keys, engine, environment), request


def ask_for_fault(request):
    open(request, 'w').close()


def case_fault(h, kind):
    """A fault that is not a guest memory fault ends the process: a diagnostic, and a status that is not 0."""
    name = {'segv': 'SIGSEGV', 'bus': 'SIGBUS', 'ill': 'SIGILL'}[kind]
    started, request = start_fault(h, 'fault', kind)
    ask_for_fault(request)
    run = h.finish(started)
    check(not run.hung, 'the process did not end after the fault: killed by the watchdog', run)
    check(run.returncode == -getattr(signal, name), 'exit code %s, expected the end by %s' % (run.returncode, name),
          run)
    check(re.search(r'fault_handler: unhandled %s at address 0x[0-9a-f]+, pc 0x[0-9a-f]+.*terminating' % name,
                    run.log) is not None, 'no diagnostic of the fault', run)
    check('Blocking' not in run.log, 'the process blocked', run)
    # what was printed before the fault is not lost with the process
    check('guest: running' in run.log, 'the output in front of the fault is missing', run)


def case_fault_on_request(h):
    """The fault of the fault cases comes when the case asks for it, and not before: a case that asks once the
    guest has printed its text always has that text in front of the fault."""
    started, request = start_fault(h, 'fault_on_request', 'segv')
    tag, args, process, log, directory, t0 = started
    # long enough for a fault that does not wait for the request to come
    time.sleep(1)
    if process.poll() is not None or 'fault_handler' in h.read_log(directory):
        check(False, 'the fault came before it was asked for', h.finish(started))
    ask_for_fault(request)
    run = h.finish(started)
    check(not run.hung, 'the fault that was asked for did not come: killed by the watchdog', run)
    check(run.returncode == -signal.SIGSEGV, 'exit code %s, expected the end by SIGSEGV' % run.returncode, run)
    text, fault = run.log.find('guest: running'), run.log.find('fault_handler: unhandled SIGSEGV')
    check(0 <= text < fault, 'the text of the guest is not in front of the fault', run)


def case_fault_block(h):
    """NULLDC_FAULT_BLOCK=1: the process stays, for a debugger, and says so."""
    started, request = start_fault(h, 'fault_block', 'segv', env={'NULLDC_FAULT_BLOCK': '1'})
    ask_for_fault(request)
    run = h.finish(started, kill_when=lambda log: log.count('fault_handler: Blocking') >= 2)
    check(run.killed_on_purpose, 'the process did not block after the fault', run)
    check('blocking for a debugger' in run.log, 'no diagnostic of the fault', run)

    # the config key does the same for a fault after the config is loaded
    started, request = start_fault(h, 'fault_block_key', 'segv', keys=['config:Debug.BlockOnFault=yes'])
    ask_for_fault(request)
    run = h.finish(started, kill_when=lambda log: log.count('fault_handler: Blocking') >= 2)
    check(run.killed_on_purpose, 'the process did not block after the fault (config key)', run)


# A signal that is sent ends the process as soon as it is delivered. This is how long that may take on a machine
# that is busy before the run counts as one that went on.
SENT_SIGNAL_SECONDS = 10


def send_sigsegv(started):
    """kill -SEGV on a started run."""
    tag, args, process, log, directory, t0 = started
    os.kill(process.pid, signal.SIGSEGV)


def finish_sent(h, started):
    """Waits for a run that was just sent a signal, for SENT_SIGNAL_SECONDS at most."""
    tag, args, process, log, directory, t0 = started
    return h.finish(started, watchdog=time.time() - t0 + SENT_SIGNAL_SECONDS)


def check_sent(run, sender, si_code):
    """The run was ended by a SIGSEGV that was sent to it, and its diagnostic names the sender."""
    check(not run.hung, 'the run went on for %d s after a SIGSEGV was sent to it: killed' % SENT_SIGNAL_SECONDS, run)
    check(run.returncode == -signal.SIGSEGV, 'exit code %s, expected the end by SIGSEGV' % run.returncode, run)
    lines = [line for line in run.log.splitlines() if line.startswith('fault_handler')]
    expected = 'fault_handler: SIGSEGV sent by pid %d (si_code %d), not a fault: terminating' % (sender, si_code)
    check(lines == [expected], 'the diagnostic is not\n    %s' % expected, run)
    check(run.exit_line is None, 'a nulldc-exit line from a run that a signal ended', run)
    check('guest: running' in run.log, 'the output in front of the signal is missing', run)


def case_fault_sent(h):
    """A SIGSEGV that is sent to the process is nobody's fault: it ends the run at once, with a diagnostic that
    names the sender, and is not taken for a guest memory fault."""
    # kill, from another process. On the JIT the handler is in use for the faults of the guest.
    for engine in ('jit', 'interp'):
        started = start_spin(h, 'kill_' + engine, engine=engine)
        send_sigsegv(started)
        check_sent(finish_sent(h, started), os.getpid(), 0)

    # raise(), from a thread of the process itself that is not the one of the emulator: si_code is SI_TKILL
    started, request = start_fault(h, 'raise', 'raise', engine='jit')
    tag, args, process, log, directory, t0 = started
    ask_for_fault(request)
    check_sent(finish_sent(h, started), process.pid, -6)

    # for a debugger the process stays, as it does for a fault
    started = start_spin(h, 'block', engine='jit', env={'NULLDC_FAULT_BLOCK': '1'})
    send_sigsegv(started)
    run = h.finish(started, kill_when=lambda log: log.count('fault_handler: Blocking') >= 2)
    check(run.killed_on_purpose, 'the process did not block after the signal', run)
    check('fault_handler: SIGSEGV sent by pid %d (si_code 0), not a fault: blocking for a debugger' % os.getpid()
          in run.log, 'no diagnostic of the signal', run)


def case_fault_guest(h):
    """The faults of the guest are still handled: a run on the JIT takes SIGSEGVs that the kernel raises for a
    fault, the handler gives every one of them back to the guest, and the run ends as it was asked to."""
    check(h.options.shim and os.path.isfile(h.options.shim), 'no fault shim (--shim)')
    count = os.path.join(h.work, 'segv-faults')
    run = h.run('guest', headless('config:Exit.AfterVblanks=20', engine='jit'), h.guest('guest_spin'),
                env={'LD_PRELOAD': h.options.shim, 'NULLDC_TEST_FAULT_COUNT': count})
    check_clean(run, 'vblanks')
    check('guest: running' in run.log, 'the text of the guest is not in the output', run)
    check(os.path.isfile(count), 'the fault shim counted nothing: no %s' % count, run)
    with open(count, 'rb') as f:
        faults, = struct.unpack('I', f.read())
    # a run without one would say nothing about the handler
    check(faults > 0, 'the run took no SIGSEGV for a fault: nothing went through the handler', run)
    print('  %d SIGSEGVs for a fault went through the handler' % faults)


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


# ---------------------------------------------------------------------------------------------------------------------
# The directories: the one of the executable by default, --portable <dir>, --shared, and the command lines that
# are refused. Each case runs a copy of the emulator in a scratch directory, never the binary under test itself,
# with HOME and the XDG variables pointed into scratch as well, and lists everything there before and after.

# What a headless run of guest_exit writes into its directory
RUN_FILES = ('emu.cfg', 'vmu_save_A1.bin', 'vmu_save_A2.bin')

# An emu.cfg that would show if it were read: the exit line of a run that does not ask for it gets a state hash
HASH_CFG = '[config]\nExit.StateHash = yes\n'


def listing(top):
    """Every directory (with a / at its end) and file below top, relative to it, files with their size."""
    found = {}
    for root, dirs, files in os.walk(top):
        for name in dirs:
            found[os.path.relpath(os.path.join(root, name), top) + '/'] = None
        for name in files:
            path = os.path.join(root, name)
            found[os.path.relpath(path, top)] = os.path.getsize(path)
    return found


def scratch(h, name, env_dirs=True):
    """<work>/<name>: bin/nulldc3, a copy of the emulator; cwd/, an empty working directory; home/, an empty HOME;
    and with env_dirs xdg/config and xdg/data, empty, for XDG_CONFIG_HOME and XDG_DATA_HOME. Returns the
    directory, the copy and the environment. The directory is named with every symbolic link resolved, the way
    the emulator names the directory of its executable (/proc/self/exe) and a relative --portable (getcwd): --work
    may be reached through a link."""
    top = os.path.realpath(os.path.join(h.work, name))
    for sub in ('bin', 'cwd', 'home'):
        os.makedirs(os.path.join(top, sub))
    binary = os.path.join(top, 'bin', 'nulldc3')
    shutil.copy2(h.options.emulator, binary)
    env = {'HOME': os.path.join(top, 'home')}
    if env_dirs:
        for sub in ('config', 'data'):
            os.makedirs(os.path.join(top, 'xdg', sub))
        env.update({'XDG_CONFIG_HOME': os.path.join(top, 'xdg', 'config'),
                    'XDG_DATA_HOME': os.path.join(top, 'xdg', 'data')})
    return top, binary, env


def check_new(run, top, before, expected, what):
    """What the run added below top is expected (a set of paths of listing()), and it changed nothing there."""
    after = listing(top)
    added = sorted(set(after) - set(before))
    gone = sorted(set(before) - set(after))
    changed = sorted(p for p in before if p in after and before[p] != after[p])
    check(added == sorted(expected) and not gone and not changed,
          '%s: the run wrote %s (expected %s), removed %s, changed %s' % (what, added, sorted(expected), gone, changed),
          run)


def check_dirs_line(run, directory, data=None):
    check('Config dir is: %s/\n' % directory in run.log and 'Data dir is:   %s/\n' % (data or directory) in run.log,
          'the run does not name %s as its directory' % directory, run)


def guest_run(h, tag, binary, cwd, env, extra_args=(), keys=(), program=True):
    """guest_exit, headless, on a copy of the emulator, without the --portable of the harness."""
    return h.run(tag, headless(*keys), h.guest('guest_exit') if program else None, extra_args=extra_args,
                 portable=False, binary=binary, cwd=cwd, env=env)


def case_dirs_default(h):
    """Without --portable and --shared the files are in the directory of the executable, from the system: a copy of
    the emulator in a scratch directory, started from another working directory, writes them beside itself and
    nowhere else (not into the working directory, HOME or the XDG directories). Started through a symbolic link in
    a third directory, the directory is the one of the file the link points at."""
    top, binary, env = scratch(h, 'default.dirs')
    before = listing(top)
    run = guest_run(h, 'default', binary, os.path.join(top, 'cwd'), env)
    check_clean(run, 'guest-exit')
    check_dirs_line(run, os.path.join(top, 'bin'))
    check_new(run, top, before, ['bin/' + name for name in RUN_FILES], 'a run in the directory of its executable')
    check(sorted(os.listdir(run.directory)) == ['log.txt'], 'the run wrote into its working directory', run)

    top, binary, env = scratch(h, 'link.dirs')
    os.makedirs(os.path.join(top, 'link'))
    link = os.path.join(top, 'link', 'nulldc3')
    os.symlink(binary, link)
    before = listing(top)
    run = guest_run(h, 'link', link, os.path.join(top, 'cwd'), env)
    check_clean(run, 'guest-exit')
    check_dirs_line(run, os.path.join(top, 'bin'))
    check_new(run, top, before, ['bin/' + name for name in RUN_FILES], 'a run through a symbolic link')


def case_dirs_portable(h):
    """--portable <dir>: the files are in <dir> and nowhere else, not beside the executable. A directory that is not
    there is made, with the ones above it; a relative one is relative to the working directory; a name with blanks
    is one word; one that is there is taken as it is. "--" ends the switches: a game behind it may begin with a
    dash."""
    top, binary, env = scratch(h, 'portable.dirs')
    cwd = os.path.join(top, 'cwd')
    os.makedirs(os.path.join(top, 'there'))
    for tag, given, made in (('missing', os.path.join(top, 'p', 'not', 'there'), ['p/', 'p/not/', 'p/not/there/']),
                             ('relative', os.path.join('rel', 'dir'), ['cwd/rel/', 'cwd/rel/dir/']),
                             ('blanks', os.path.join(top, 'with blanks', 'and more'),
                              ['with blanks/', 'with blanks/and more/']),
                             ('there', os.path.join(top, 'there'), [])):
        directory = made[-1].rstrip('/') if made else 'there'
        before = listing(top)
        run = guest_run(h, 'portable_' + tag, binary, cwd, env, extra_args=['--portable', given])
        check_clean(run, 'guest-exit')
        check_dirs_line(run, os.path.join(top, directory))
        check_new(run, top, before, made + [directory + '/' + name for name in RUN_FILES], '--portable %s' % given)

    # a game whose name begins with a dash, behind "--"
    shutil.copy(h.guest('guest_exit'), os.path.join(cwd, '-guest_exit.bin'))
    before = listing(top)
    run = guest_run(h, 'portable_dash_game', binary, cwd, env, program=False,
                    extra_args=['--portable', os.path.join(top, 'dash'), '--', '-guest_exit.bin'])
    check_clean(run, 'guest-exit')
    check('guest: returning to the BIOS' in run.log, 'the game behind "--" did not run', run)
    check_new(run, top, before, ['dash/'] + ['dash/' + name for name in RUN_FILES], '-- -guest_exit.bin')


def case_dirs_shared(h):
    """--shared: the files are in $XDG_CONFIG_HOME/nulldc3 and $XDG_DATA_HOME/nulldc3, made when missing, and
    nowhere else, not beside the executable; without the variables in ~/.config/nulldc3 and ~/.local/share/nulldc3.
    The reicast directories of before are there and not read; the XDG system lists are read behind the user's
    directory."""
    top, binary, env = scratch(h, 'shared.dirs')
    cwd = os.path.join(top, 'cwd')
    # every place the reicast builds read a config from: one that is read asks for a state hash
    for reicast in ('home/.reicast', 'home/.config/reicast', 'xdg/config/reicast', 'sys/a/reicast'):
        os.makedirs(os.path.join(top, reicast))
        with open(os.path.join(top, reicast, 'emu.cfg'), 'w') as f:
            f.write(HASH_CFG)
    os.makedirs(os.path.join(top, 'home', '.local', 'share', 'reicast', 'data'))
    env['XDG_CONFIG_DIRS'] = os.path.join(top, 'sys', 'a')
    env['XDG_DATA_DIRS'] = os.path.join(top, 'sys', 'data')

    before = listing(top)
    run = guest_run(h, 'shared_xdg', binary, cwd, env, extra_args=['--shared'])
    check_clean(run, 'guest-exit')
    check('hash' not in run.fields, 'a reicast emu.cfg was read: the exit line has a state hash', run)
    check_dirs_line(run, os.path.join(top, 'xdg', 'config', 'nulldc3'), os.path.join(top, 'xdg', 'data', 'nulldc3'))
    check_new(run, top, before, ['xdg/config/nulldc3/', 'xdg/config/nulldc3/emu.cfg', 'xdg/data/nulldc3/',
                                 'xdg/data/nulldc3/vmu_save_A1.bin', 'xdg/data/nulldc3/vmu_save_A2.bin'],
              '--shared with XDG_CONFIG_HOME and XDG_DATA_HOME')

    # without the two variables: below HOME
    plain = {k: v for k, v in env.items() if k not in ('XDG_CONFIG_HOME', 'XDG_DATA_HOME')}
    before = listing(top)
    run = guest_run(h, 'shared_home', binary, cwd, plain, extra_args=['--shared'])
    check_clean(run, 'guest-exit')
    check('hash' not in run.fields, 'a reicast emu.cfg was read: the exit line has a state hash', run)
    check_dirs_line(run, os.path.join(top, 'home', '.config', 'nulldc3'),
                    os.path.join(top, 'home', '.local', 'share', 'nulldc3'))
    check_new(run, top, before, ['home/.config/nulldc3/', 'home/.config/nulldc3/emu.cfg',
                                 'home/.local/share/nulldc3/', 'home/.local/share/nulldc3/vmu_save_A1.bin',
                                 'home/.local/share/nulldc3/vmu_save_A2.bin'], '--shared with HOME only')

    # a config of the system list, nulldc3/emu.cfg in its second directory, is read when the user has none
    os.makedirs(os.path.join(top, 'sys', 'b', 'nulldc3'))
    with open(os.path.join(top, 'sys', 'b', 'nulldc3', 'emu.cfg'), 'w') as f:
        f.write(HASH_CFG)
    system = dict(env, XDG_CONFIG_HOME=os.path.join(top, 'xdg2', 'config'), XDG_DATA_HOME=os.path.join(top, 'xdg2', 'data'),
                  XDG_CONFIG_DIRS=os.path.join(top, 'sys', 'a') + ':' + os.path.join(top, 'sys', 'b'))
    run = guest_run(h, 'shared_system', binary, cwd, system, extra_args=['--shared'])
    check_clean(run, 'guest-exit')
    check('hash' in run.fields, 'the emu.cfg of $XDG_CONFIG_DIRS (its second directory) was not read', run)
    check(not os.path.exists(os.path.join(top, 'bin', 'emu.cfg')), 'a --shared run wrote beside the executable', run)


# The command lines that are refused: what follows the program on the command line (the headless keys and
# guest_exit are in front, so that one taken by mistake starts a run, which writes), and the text of the message.
# DIR is a directory that is not there, FILE a file, RO a directory that cannot be written, LONG a directory below
# DIR whose name is longer than a name can be (DIR is made on the way to it, and has to be removed again).
ERRORS = (
    ('one_dash_config', ['-config', 'config:Headless=yes'], '-config is written with two dashes: --config'),
    ('one_dash_portable', ['-portable', 'DIR'], '-portable is written with two dashes: --portable'),
    ('one_dash_shared', ['-shared'], '-shared is written with two dashes: --shared'),
    ('one_dash_help', ['-help'], '-help is written with two dashes: --help'),
    ('portable_last', ['--portable'], '--portable needs a directory: --portable <dir>'),
    ('portable_switch', ['--portable', '--shared'], '--portable needs a directory: --portable <dir>, not --shared'),
    ('portable_empty', ['--portable', ''], '--portable needs a directory'),
    ('portable_file', ['--portable', 'FILE'], 'FILE is not a directory'),
    ('portable_unwritable', ['--portable', 'RO'], 'cannot be written'),
    ('portable_unmakable', ['--portable', 'RO_NEW'], 'the directory cannot be made'),
    ('portable_unmakable_below', ['--portable', 'LONG'], 'the directory cannot be made'),
    ('portable_twice', ['--portable', 'DIR', '--portable', 'DIR2'], '--portable is given twice'),
    ('portable_shared', ['--portable', 'DIR', '--shared'], '--portable <dir> and --shared cannot be given together'),
    ('shared_portable', ['--shared', '--portable', 'DIR'], '--portable <dir> and --shared cannot be given together'),
    ('shared_twice', ['--shared', '--shared'], '--shared is given twice'),
    ('shared_no_home', ['--shared'], 'there are no per-user directories'),
    ('shared_unmakable', ['--shared'], 'nulldc3 cannot be made'),
    ('unknown', ['--bogus'], 'unknown switch --bogus'),
    ('unknown_one_dash', ['-x'], 'unknown switch -x'),
    ('dash_alone', ['-'], 'unknown switch -'),
    ('dash_game', ['-guest_exit.bin'], 'unknown switch -guest_exit.bin'),
    ('windows_switch', ['--console'], '--console is a switch of the Windows build only'),
    ('windows_log', ['--log'], '--log is a switch of the Windows build only'),
    ('portable_equals', ['--portable=DIR'], 'the value of --portable is the next word'),
    ('shared_value', ['--shared=yes'], '--shared takes no value'),
    ('config_last', ['--config'], '--config needs section:key=value'),
    ('config_item', ['--config', 'Headless=yes'], "--config: 'Headless=yes' is not section:key=value"),
    ('config_comma', ['--config', 'config:Headless=yes', ','], "--config: nothing after the last ','"),
    ('config_switch', ['--config', '--portable', 'DIR'], '--config needs section:key=value in front of --portable'),
    ('exe_unwritable', [], 'the directory of the executable, BIN, cannot be written'),
)


# The rows of ERRORS that need a directory without write permission, made so for the run, and their environment
# when it is not the one of scratch(): shared_unmakable has the data directory below RO, so that the config
# directory is made before the data directory cannot be
LOCKED = {'portable_unwritable': 'RO', 'portable_unmakable': 'RO', 'shared_unmakable': 'RO', 'exe_unwritable': 'BIN'}
# The rows whose message has to name --portable <dir> and --shared: a portable directory that cannot be used
CHOOSE_ANOTHER = ('portable_unwritable', 'portable_unmakable', 'portable_unmakable_below', 'exe_unwritable')


def case_dirs_errors(h):
    """Every command line that is refused: exit code 2, a message on stderr that says why (and for a portable
    directory that cannot be written or made names --portable <dir> and --shared), no machine started, and nothing
    written anywhere: a directory that was made on the way to one that cannot be made is removed again."""
    top, binary, env = scratch(h, 'errors.dirs')
    cwd = os.path.join(top, 'cwd')
    shutil.copy(h.guest('guest_exit'), os.path.join(cwd, '-guest_exit.bin'))
    with open(os.path.join(top, 'file'), 'w') as f:
        f.write('a file\n')
    os.makedirs(os.path.join(top, 'ro'))
    as_root = hasattr(os, 'geteuid') and os.geteuid() == 0
    names = {'DIR': os.path.join(top, 'dir'), 'DIR2': os.path.join(top, 'dir2'), 'FILE': os.path.join(top, 'file'),
             'RO': os.path.join(top, 'ro'), 'RO_NEW': os.path.join(top, 'ro', 'new'), 'BIN': os.path.join(top, 'bin'),
             'LONG': os.path.join(top, 'dir', 'x' * 300)}
    command = [binary, '--config']
    for i, key in enumerate(headless()):
        command += [','] if i else []
        command.append(key)
    command.append(h.guest('guest_exit'))

    done = 0
    for tag, words, text in ERRORS:
        if as_root and tag in LOCKED:
            print('  %s: not made, root writes into a directory without write permission' % tag)
            continue
        args = command + [names.get(w, w) for w in words]
        message = text.replace('FILE', names['FILE']).replace('BIN', names['BIN'])
        environment = env
        if tag == 'shared_no_home':
            environment = {}
        elif tag == 'shared_unmakable':
            environment = dict(env, XDG_DATA_HOME=os.path.join(names['RO'], 'data'))
        locked = names[LOCKED[tag]] if tag in LOCKED else None
        before = listing(top)
        if locked:
            os.chmod(locked, 0o555)
        t0 = time.time()
        try:
            p = subprocess.run(args, cwd=cwd, env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=h.options.watchdog, preexec_fn=limit_core)
        except subprocess.TimeoutExpired:
            raise Failure('%s: the run did not end: %s' % (tag, ' '.join(args)))
        finally:
            if locked:
                os.chmod(locked, 0o755)
        out, err = p.stdout.decode('utf-8', 'replace'), p.stderr.decode('utf-8', 'replace')
        h.runs.append(Run(tag, args, p.returncode, False, out + err, cwd, time.time() - t0))
        what = '%s: %s\n  stdout: %s\n  stderr: %s' % (tag, ' '.join(repr(a) if not a or ' ' in a else a for a in args),
                                                      out.strip().replace('\n', ' | ')[-300:], err.strip())
        check(p.returncode == 2, 'exit code %s, expected 2\n  %s' % (p.returncode, what))
        lines = err.strip().splitlines()
        check(len(lines) == 1 and lines[0].startswith('nulldc3: ') and message in lines[0],
              'stderr is not the one line "nulldc3: ...%s..."\n  %s' % (message, what))
        if tag in CHOOSE_ANOTHER:
            check('--portable <dir>' in lines[0] and '--shared' in lines[0],
                  'the message does not name --portable <dir> and --shared\n  %s' % what)
        for machine in ('Config dir is', 'Virtual cfg', 'Using MCPU', 'nulldc-exit', 'Using \''):
            check(machine not in out, 'a machine was started ("%s" in stdout)\n  %s' % (machine, what))
        after = listing(top)
        check(after == before, 'the refused run wrote %s\n  %s' % (sorted(set(after) ^ set(before)), what))
        done += 1
    print('  %d command lines refused, each with exit code 2, its message and nothing written' % done)


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
    'rtc.game': case_rtc_game,
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
    'fault.on_request': case_fault_on_request,
    'fault.block': case_fault_block,
    'fault.sent': case_fault_sent,
    'fault.guest': case_fault_guest,
    'determinism': case_determinism,
    'perf.script': case_perf_script,
    'dirs.default': case_dirs_default,
    'dirs.portable': case_dirs_portable,
    'dirs.shared': case_dirs_shared,
    'dirs.errors': case_dirs_errors,
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
