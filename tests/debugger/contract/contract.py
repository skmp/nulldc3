#!/usr/bin/env python3
"""The emulator's debugger server against the contract runner of the devtools UI, and what goes with it.

    contract.py --emulator <nulldc3> --work <dir> --guest <2ndmix.elf> --mode <mode> [--engine jit|interp]
                [--fallback] [--devtools <dreamcast-devtools checkout>] [--npm <npm>] [--idle-symbol <symbol>]
                [--parts <scenario>:<part>[+<part>...]] [--only <scenario>[,<scenario>...]] [--require-note <text>]...
    contract.py --mode hash-listening-rule

Every emulator is run as tests/system runs one: headless, its files in a directory of its own under the work
directory (--portable), an empty environment (no display), the real-time clock pinned, a core file size limit
of 1 byte, and a watchdog. A process this script starts is ended by its pid. On Windows the environment has
SystemRoot and TEMP and TMP of the run's directory, the contract runner's (the caller's environment) has TEMP, TMP
and npm's cache in the work directory, and the emulator and the contract runner (npm.cmd and the node it starts) run
inside nulldc3-winrun, whose job ends all of it when the process is ended (tests/common/emurun.py, which starts
every program of this script and reads what it prints). README.md has the modes:

    m1             the gate of milestone M1: the emulator at its StartPaused stop, and the contract runner of the
                   devtools UI against it (npm run contract -- --url ... --profile emulator --elf ... --milestone M1)
    m2             the gate of milestone M2: the same emulator, and the runner with --milestone M2, which selects
                   the scenarios of M1 and of M2 and requires each of them to pass whole. With --only it is the
                   second run of that gate, against the sounding guest of the tree (--milestone M2 --only S00,S16),
                   and every --require-note is handed on to the runner: a run without the note is no pass
    parts          parts of a scenario of the runner that has parts, by themselves: the same emulator, and the
                   runner with --only S00,<scenario> --parts <scenario>:<part>[+<part>...], and with every
                   --require-note handed on to it. The packages of milestone M2 showed their parts of S16 this
                   way before the emulator had the scenario whole, which the mode m2 holds now. The program is
                   2ndmix.elf, or the sounding guest of the tree
    m3             the gate of milestone M3, its first run: the same emulator on 2ndmix, and the runner with
                   --milestone M3, which selects the scenarios of M1 and M2 and S17 (S00 to S17, S19 and S20) and
                   requires each of them to pass whole
    m3dsp          the second run of that gate, against the sounding guest of the tree, which loads the DSP test
                   program: the runner with --milestone M3 --dsp-program (S00, S17, S21) and every --require-note
                   handed on to it. S17 makes its first capture at the anchor, where the guest holds its ARM7
    dsp            the run of the gate of milestone M3 that holds the run control of the DSP by itself: the same
                   emulator with the sounding guest of the tree, which loads the DSP test program, and the runner
                   with --milestone M3 --dsp-program --only S00,S21 and every --require-note handed on to it. S21
                   makes its first step of the DSP at the anchor, where the guest holds its ARM7, and then waits
                   for the program
    grace          a client leaves the target stopped at a breakpoint that is reached in every frame; 11 s later
                   the target has run on without stopping there, and the breakpoint stops it again, unasked,
                   in front of the next client; the tables as they were, the note of the grace period in the
                   event log, and the event gates open again; and when that client resumes and leaves, the
                   breakpoint stops the target within the new grace period
    config         the configuration errors: three runs that end by themselves with exit code 2; and a server
                   that was enabled and cannot be started (its port is taken): exit code 2 with
                   Debugger:Required or Debugger:StartPaused, one line and a run without the server otherwise;
                   and the server of such a run starts where the port is free
    slow           the slow-client rule: a client that has stopped reading is queued no periodic tick behind
                   a megabyte of ticks, but every tick that a request caused, and a full tick next
    hash-off       the state hash of a run with no Debugger: key and of one with Debugger:Enabled=no; no port
                   line without the key, the port line with Debugger:Enabled=yes
    hash-noclient  the state hash with the server on and no client, and with the server off
    hash-idlebp    the state hash with a client connected and a muted breakpoint on a hot address, and with the
                   server off
    hash-listening the state hash with a client that describes itself at the StartPaused stop, lets the run begin and
                   then only receives until the run ends, with a full tick of every register of the device tree
                   every 20 ms; and with the server off
    hash-listening-rule
                   what the mode hash-listening takes for a full running tick, on ticks that are made up: no
                   emulator is run, and --emulator, --work and --guest are not asked for

Every run is made on the engine it asks for. The SH4 runs on the recompiler with --engine jit and on the
interpreter with --engine interp: the emulator takes config:Dynarec.Enabled=yes for a wish and says "Using MCPU
Recompiler" or "Using MCPU Interpreter", and a run that says the other, or neither, fails. A mode that talks to the
server also requires the SH4 debug engine, by the name debugger.describe gives it among its capabilities:
sh4-debug-fallback with --fallback (Debugger:ForceFallback=yes), sh4-debug-native on the recompiler without it (on
an x86-64 host, whose recompiler has native breakpoints and steps; on another the recompiler has none and the
fallback is what it reports), and sh4-debug-native on the interpreter, which executes its breakpoints and steps
itself and which the key does not change (--fallback is refused with --engine interp). The modes m1, m2, m3, m3dsp,
parts and dsp ask for describe on a connection of their own behind the runner, grace and hash-listening on their clients at the
StartPaused stop, slow on its client once the machine runs, hash-idlebp takes the runner's describe from its last
"server:" line; hash-off, hash-noclient and config have no client
and check the SH4's engine alone (config not even that: its runs end before there is a machine). A failure says the
engine found and the one asked for.
"""

import argparse
import os
import platform
import re
import shutil
import socket
import struct
import subprocess
import sys
import time

# the client of this directory and the run helper of the harnesses; nothing is written into the source tree for them
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'common'))

import emurun  # noqa: E402
import wsclient  # noqa: E402

RTC_PIN = 946684800

BAD = ('Fatal error', 'Verify Failed', 'fault_handler', 'AddressSanitizer', 'ThreadSanitizer', 'runtime error:')

HASH_FIELDS = ('reason', 'vblanks', 'hash', 'sh4', 'sched', 'ram', 'vram', 'aram', 'arm7')

PORT_LINE = re.compile(r'^Debugger: http://127\.0\.0\.1:(\d+)/$', re.M)

# what the emulator thread prints when it has chosen the engine of the SH4 (dc_run of libswirl/libswirl.cpp)
ENGINE_SAYS = {'jit': 'Using MCPU Recompiler', 'interp': 'Using MCPU Interpreter'}
ENGINE_NAMES = {'jit': 'the recompiler', 'interp': 'the interpreter'}

# What executes the breakpoints and steps of the SH4, by the names of debugger.describe's capabilities (design
# 2.3.5): the active backend itself, or the interpreter fallback
DEBUG_ENGINES = ('sh4-debug-native', 'sh4-debug-fallback')

# The recompiler of an x86-64 host executes breakpoints and steps itself (SH4DBG_NATIVE, hw/sh4/sh4_if.h); the one
# of an arm64 host does not, and runs them on the interpreter fallback with or without Debugger:ForceFallback
NATIVE_RECOMPILER = platform.machine().lower() in ('x86_64', 'amd64')


class Failed(Exception):
    pass


def tail(text, lines=40):
    return '\n'.join(text.splitlines()[-lines:])


def check(ok, what, run=None):
    if not ok:
        more = ''
        if run is not None:
            run.read()
            more = '\n--- the end of its stdout ---\n' + tail(run.out) + '\n--- the end of its stderr ---\n' + tail(run.err)
        raise Failed(what + more)


class Run:
    """One run of the emulator. Its stdout and stderr go to files of the work directory."""

    def __init__(self, args, name):
        self.args = args
        self.name = name
        self.dir = os.path.join(args.work, name)
        shutil.rmtree(self.dir, ignore_errors=True)
        # the directory of the run's files (--portable), with the data subdirectory a state or the flash is saved in
        os.makedirs(os.path.join(self.dir, 'portable', 'data'))
        self.out_path = os.path.join(self.dir, 'stdout.txt')
        self.err_path = os.path.join(self.dir, 'stderr.txt')
        self.process = None
        self.out = ''
        self.err = ''
        self.code = None

    def command(self, keys):
        # the recipe of the design (5.4): headless, no audio device, the clock pinned; and the engine
        engine = 'yes' if self.args.engine == 'jit' else 'no'
        items = ['config:Headless=yes', 'audio:backend=none', 'config:Dreamcast.RTC=%d' % RTC_PIN, 'config:Dynarec.Enabled=' + engine]
        if self.args.fallback:
            items.append('Debugger:ForceFallback=yes')
        items += keys
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
        self.out_file = open(self.out_path, 'wb')
        self.err_file = open(self.err_path, 'wb')
        self.process = emurun.start(self.cmdline, self.args.watchdog, cwd=self.dir, env=emurun.environment(self.dir),
                                    stdout=self.out_file, stderr=self.err_file)
        self.started = time.monotonic()
        return self

    def read(self):
        self.out = emurun.read_text(self.out_path)
        self.err = emurun.read_text(self.err_path)
        return self.out

    def alive(self):
        return self.process is not None and self.process.poll() is None

    def wait(self, seconds):
        """True when the process ended within that time."""
        try:
            self.code = self.process.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            return False
        finally:
            self.read()
        self.close_files()
        return True

    def close_files(self):
        for f in (self.out_file, self.err_file):
            if not f.closed:
                f.close()

    def kill(self):
        """Ends the process by its pid, whatever it is doing (on Windows nulldc3-winrun, and its job with it)."""
        if self.process is not None:
            self.code = emurun.kill(self.process)
        self.close_files()
        self.read()

    def terminate(self, seconds=20):
        """SIGTERM, as a harness ends an emulator that is parked; SIGKILL when that does not end it. Windows has no
        SIGTERM: there nulldc3-winrun is ended, and the job with it (emurun.terminate)."""
        emurun.terminate(self.process)
        if not self.wait(seconds):
            self.kill()
            return False
        return True

    def run(self, keys):
        """A run that ends by itself."""
        self.start(keys)
        if not self.wait(self.args.watchdog):
            self.kill()
            raise Failed('the run did not end within %d s: %s' % (self.args.watchdog, ' '.join(self.cmdline))
                         + '\n--- the end of its stdout ---\n' + tail(self.out) + '\n--- the end of its stderr ---\n' + tail(self.err))
        self.no_bad()
        return self

    def no_bad(self):
        self.read()
        for bad in BAD:
            check(bad not in self.out and bad not in self.err, 'the output of the emulator has "%s"' % bad, self)

    def port(self, seconds=30):
        """The port of the line the emulator prints for its server, on stdout."""
        deadline = time.monotonic() + seconds
        while True:
            self.read()
            found = PORT_LINE.search(self.out)
            if found:
                return int(found.group(1))
            check(self.alive(), 'the emulator ended (exit code %s) before it printed its "Debugger: http://" line' % self.process.poll(), self)
            check(time.monotonic() < deadline, 'no "Debugger: http://127.0.0.1:<port>/" line on stdout within %d s' % seconds, self)
            time.sleep(0.05)


def check_backend(args, run):
    """The SH4 of the run is on the engine --engine asked for."""
    run.read()
    other = 'interp' if args.engine == 'jit' else 'jit'
    asked = '%s (config:Dynarec.Enabled=%s)' % (ENGINE_NAMES[args.engine], 'yes' if args.engine == 'jit' else 'no')
    check(ENGINE_SAYS[other] not in run.out, 'the run was asked for %s and was made on %s: its output has "%s"'
          % (asked, ENGINE_NAMES[other], ENGINE_SAYS[other]), run)
    check(ENGINE_SAYS[args.engine] in run.out, 'the run was asked for %s and does not say that it was made on it: its '
          'output has no "%s"' % (asked, ENGINE_SAYS[args.engine]), run)


def debug_engine_asked(args):
    """The SH4 debug engine a run of these arguments asks for, and what asks for it."""
    if args.engine == 'interp':
        return 'sh4-debug-native', 'the interpreter (config:Dynarec.Enabled=no), whose breakpoints and steps are its own'
    if args.fallback:
        return 'sh4-debug-fallback', 'the recompiler (config:Dynarec.Enabled=yes) with Debugger:ForceFallback=yes (--fallback)'
    if NATIVE_RECOMPILER:
        return 'sh4-debug-native', 'the recompiler (config:Dynarec.Enabled=yes) of an x86-64 host, without Debugger:ForceFallback'
    return 'sh4-debug-fallback', 'the recompiler (config:Dynarec.Enabled=yes) of a %s host, which has no native breakpoints' % platform.machine()


def check_engine(args, capabilities, run, where):
    """The run is made on the engine it asked for: its SH4 (check_backend), and the debug engine that the
    capabilities of a debugger.describe name. Returns a line that says which."""
    check_backend(args, run)
    found = [name for name in capabilities if name in DEBUG_ENGINES]
    wanted, why = debug_engine_asked(args)
    check(found == [wanted], 'the SH4 debug engine: %s names %s, the run asked for %s (%s)'
          % (where, ' and '.join(found) if found else 'neither %s nor %s' % DEBUG_ENGINES, wanted, why), run)
    return 'the SH4 on %s, its breakpoints and steps on %s, as asked' % (ENGINE_NAMES[args.engine], wanted)


def described_engine(args, port, run):
    """check_engine() on a debugger.describe of a connection of this script's own."""
    try:
        client = wsclient.Client('127.0.0.1', port)
        try:
            client.describe()
            return check_engine(args, client.shape.get('capabilities', []), run, 'debugger.describe')
        finally:
            client.close()
    except wsclient.Error as error:
        check(False, 'the client that asks for the engine: %s' % error, run)


def exit_line(log):
    lines = [line for line in log.splitlines() if line.startswith('nulldc-exit: ')]
    if not lines:
        return None
    return dict(pair.split('=', 1) for pair in lines[-1][len('nulldc-exit: '):].split(' ') if '=' in pair)


def has_port_line(log):
    return any(line.startswith('Debugger: http://') for line in log.splitlines())


def symbol_address(path, name):
    """The address of a symbol of an ELF32 file, from its .symtab: `name`, or `_name` as a C compiler for the SH4
    writes it. The contract runner resolves --idle-breakpoint the same way (scripts/contract/elfSymbols.ts)."""
    with open(path, 'rb') as f:
        image = f.read()
    check(len(image) >= 52 and image[:4] == b'\x7fELF' and image[4] == 1 and image[5] in (1, 2), path + ' is not an ELF32 file')
    order = '<' if image[5] == 1 else '>'
    section_offset, = struct.unpack_from(order + 'I', image, 32)
    section_size, section_count = struct.unpack_from(order + 'HH', image, 46)

    def section(index):
        kind, _, _, offset, size, link, _, _, entry_size = struct.unpack_from(order + '9I', image, section_offset + index * section_size + 4)
        return kind, offset, size, link, entry_size

    found = {}
    try:
        for index in range(section_count):
            kind, offset, size, link, entry_size = section(index)
            if kind != 2 or link >= section_count:      # SHT_SYMTAB
                continue
            strings = section(link)
            for at in range(offset, offset + size - 15, entry_size or 16):
                name_at, value, _, info, _, defined_in = struct.unpack_from(order + 'IIIBBH', image, at)
                # defined functions, objects and labels
                if name_at == 0 or defined_in == 0 or (info & 0x0F) > 2:
                    continue
                start = strings[1] + name_at
                found.setdefault(image[start:image.index(b'\0', start)].decode('latin-1'), value)
    except (struct.error, ValueError):
        raise Failed('the section headers or the symbol table of %s reach outside of the file' % path)
    address = found.get(name, found.get('_' + name))
    check(address is not None, '%s has no symbol "%s"' % (path, name))
    return address


def register_value(tick, path, name):
    """A register of a tick that carries its path, as a number."""
    for register in tick['registers'].get(path, []):
        if register['name'] == name:
            return int(register['value'], 16)
    raise Failed('the tick %s has no register %s of %s' % (tick.get('tickId'), name, path))


def notes(tick):
    return [entry for entry in tick['eventLog'] if entry['metadata']['event'] == 'dc.debugger.note']


def runner_command(args, port, more):
    """npm run contract -- --url ... in the devtools checkout: the command of the design."""
    return [args.npm, 'run', '--silent', 'contract', '--', '--url', 'ws://127.0.0.1:%d/ws' % port, '--profile', 'emulator',
            '--elf', args.guest] + more


def runner_env(args):
    # the caller's environment; on Windows TEMP, TMP and npm's cache in the work directory (tsx keeps its cache in
    # TEMP, npm a log of every command in its cache), not in the profile (tests/common/emurun.py)
    env = emurun.tool_environment(args.work)
    # npm starts node, which is where npm is
    env['PATH'] = os.path.dirname(os.path.abspath(args.npm)) + os.pathsep + env.get('PATH', '')
    env.pop('DISPLAY', None)
    return env


STARTPAUSED = ['Debugger:Enabled=yes', 'Debugger:Port=0', 'Debugger:Required=yes', 'Debugger:StartPaused=yes']


def run_runner(args, selection, what):
    """The emulator at its StartPaused stop and the contract runner against it, with the selection of the mode.
    The runner decides: its exit code, "contract: OK" and a summary in which every selected scenario passed and
    no invariant was violated. Nothing else of its report is read here."""
    run = Run(args, 'emulator').start(STARTPAUSED)
    try:
        port = run.port()
        log_path = os.path.join(args.work, 'runner.txt')
        command = runner_command(args, port, selection + ['--json-file', os.path.join(args.work, 'summary.json')])
        with open(log_path, 'wb') as log:
            runner = emurun.start(command, args.watchdog, cwd=args.devtools, env=runner_env(args), stdout=log,
                                  stderr=subprocess.STDOUT, emulator=False)
            try:
                code = runner.wait(timeout=args.watchdog)
            except subprocess.TimeoutExpired:
                emurun.kill(runner)
                code = None
        text = emurun.read_text(log_path)

        # the report of the runner is the report of this test
        print(text)
        check(code is not None, 'the contract runner did not end within %d s' % args.watchdog, run)
        check(run.alive(), 'the emulator ended (exit code %s) while the runner was at it' % run.process.poll(), run)
        # what the runner checked was made on the engine the test asked for: a describe of its own, behind the
        # runner, which it does not see
        engine = described_engine(args, port, run)
        check(code == 0, 'the contract runner ended with exit code %d: %s did not pass' % (code, what), run)
        summary = [line for line in text.splitlines() if ' passed, ' in line and ' failed, ' in line]
        check(summary and 'contract: OK' in text, 'the runner did not say "contract: OK"', run)
        counts = re.match(r'(\d+) passed, (\d+) failed, (\d+) partial, (\d+) not applicable; (\d+) ticks checked, (\d+) invariant violation', summary[-1])
        check(counts is not None, 'the summary of the runner is not understood: ' + summary[-1], run)
        check(int(counts.group(1)) > 0 and all(int(counts.group(i)) == 0 for i in (2, 3, 4, 6)),
              'every selected scenario has to pass, with no invariant violated: ' + summary[-1], run)
    finally:
        # emulated time stands still at a stop, so no exit key ends this process: the harness does
        ended = run.terminate()
    run.no_bad()
    check(ended, 'the emulator did not end within 20 s of SIGTERM', run)
    print('engine: ' + engine)
    print('%s: %s' % (what, summary[-1]))


def mode_m1(args):
    """The gate of milestone M1."""
    run_runner(args, ['--milestone', 'M1'], 'the gate of M1')


def mode_m2(args):
    """The gate of milestone M2: the scenarios of M1 and S16 with its five parts, each of them whole. With --only,
    its second run, which the emulator makes with the sounding guest of the tree: there the ARM7 is held at the
    anchor and has a bl and an interrupt handler, and the runner fails a run that lacks a note --require-note asks
    for."""
    selection = ['--milestone', 'M2']
    what = 'the gate of M2'
    if args.only:
        selection += ['--only', args.only]
        what = 'the gate of M2, %s' % args.only
    for note in args.require_note:
        selection += ['--require-note', note]
    run_runner(args, selection, what)


def mode_m3(args):
    """The gate of milestone M3, its first run: the scenarios of M1, of M2 and S17 (the SGC capture and the memory
    binary), each of them whole, on 2ndmix."""
    run_runner(args, ['--milestone', 'M3'], 'the gate of M3')


def mode_m3dsp(args):
    """The second run of the gate of milestone M3: S00, S17 and S21 with the DSP test program, on the sounding guest
    of the tree, whose ARM7 is held at the anchor, where S17 makes its first capture; the runner fails a run that
    lacks a note --require-note asks for."""
    selection = ['--milestone', 'M3', '--dsp-program']
    for note in args.require_note:
        selection += ['--require-note', note]
    run_runner(args, selection, 'the gate of M3 with the DSP test program')


def mode_dsp(args):
    """S00 and S21 of the gate of milestone M3, with the DSP test program: the sounding guest of the tree, whose ARM7
    is held at the anchor, where S21 makes its first step of the DSP; the runner fails a run that lacks a note
    --require-note asks for."""
    selection = ['--milestone', 'M3', '--dsp-program', '--only', 'S00,S21']
    for note in args.require_note:
        selection += ['--require-note', note]
    run_runner(args, selection, 'the gate of M3, S00,S21 with the DSP test program')


def mode_parts(args):
    """Parts of one scenario, by themselves. The runner has the rules: a named part that is not applicable fails,
    and a note that --require-note asks for and no selected scenario made fails the run. S00 goes with it: the
    emulator is at its StartPaused stop, which is where the scenario starts from."""
    scenario = args.parts.split(':', 1)[0]
    selection = ['--only', 'S00,' + scenario, '--parts', args.parts]
    for note in args.require_note:
        selection += ['--require-note', note]
    run_runner(args, selection, 'the parts %s' % args.parts)


# One frame of 59.94 Hz in SH4 cycles, about (README.md: a breakpoint on the function of --idle-symbol stops
# 3,335,817 to 3,337,097 cycles behind the stop before it)
FRAME_CYCLES = 3336670


def mode_grace(args):
    """The grace period: a stop that a client left behind is released and no breakpoint stops any more, the
    tables stay; a client that connects makes the breakpoints active again and opens the event gates."""
    address = symbol_address(args.guest, args.idle_symbol)
    where = '%s (0x%08X)' % (args.idle_symbol, address)
    run = Run(args, 'emulator').start(STARTPAUSED)
    try:
        port = run.port()
        a = wsclient.Client('127.0.0.1', port)
        first = a.describe()
        check(first['executionState'].get('reason') == 'start', 'the emulator is not at its StartPaused stop: %r' % (first['executionState'],), run)
        print('engine: ' + check_engine(args, a.shape.get('capabilities', []), run, 'debugger.describe'))

        # an enabled breakpoint on a function the program calls in every frame, and the target left stopped at it
        a.call('breakpoints.add', {'event': 'dc.sh4.cpu.pc', 'address': address, 'kind': 'code', 'enabled': True})
        a.call('breakpoints.add', {'event': 'dc.holly.core.vblank', 'kind': 'event', 'enabled': False})
        a.call('state.watch', {'expressions': ['dc.sh4.cpu.pc', 'dc.timing.sh4_cycles']})
        a.call('breakpoints.setCategoryStates', {'categories': {'arm7': {'muted': True, 'soloed': False}, 'dsp': {'muted': False, 'soloed': False}}})
        a.call('control.runUntil', {})
        # (the boot and the set-up of the program are in front of it: 0.25 s on the JIT, 0.85 s on the interpreter,
        # 11 s on the interpreter of a ThreadSanitizer build, each with one emulator at work)
        stopped = a.wait_tick(lambda t: t['executionState']['state'] == 'paused' and t['executionState'].get('reason') == 'breakpoint', 120,
                              'the stop of the breakpoint on ' + where)
        stop = stopped['executionState']
        check(len(stopped['breakpoints']) == 2 and len(stopped['watches']) == 2 and stopped['categories']['arm7']['muted'],
              'the tick of the stop does not list two breakpoints, two watches and the muted category', run)
        check(register_value(stopped, 'dc.sh4.cpu', 'PC') == address and stop.get('breakpointId') is not None,
              'the stop is not at the breakpoint on %s: %r' % (where, stop), run)
        a.close()
        left = time.monotonic()

        # still stopped a while into the grace period: a client that comes back finds its stop
        time.sleep(3.0)
        b = wsclient.Client('127.0.0.1', port)
        again = b.describe()
        check(again['executionState'] == stop, 'a client that reconnects inside the grace period finds the stop it left: %r, not %r'
              % (again['executionState'], stop), run)
        b.close()

        # the period starts again at this disconnect: 11 s without a client
        time.sleep(11.0)
        check(run.alive(), 'the emulator ended (exit code %s) during the grace period' % run.process.poll(), run)

        # This client asks for nothing. The breakpoint is active again because it connected, and the stop comes to
        # it as the tick of a state change. A stop that the target had made on its own, with no client, would
        # not: nothing is sent to a client for a stop that was there before it
        c = wsclient.Client('127.0.0.1', port)
        try:
            hit = c.wait_tick(lambda t: t['executionState']['state'] == 'paused', 2.0, 'a paused tick')
        except wsclient.Timeout:
            hit = None
        if hit is None:
            now = c.describe()
            state = now['executionState']
            check(state != stop, 'the grace period did not release the stop: 11 s after the last client left, the next one finds the stop '
                  'that was left (%r)' % (state,), run)
            if state['state'] == 'paused':
                check(False, 'the grace period did not clear the active mask: with no client connected the released target was stopped '
                      'again by its breakpoint on %s, %d SH4 cycles behind the stop that was released (a frame has %d); the new client '
                      'finds %r, was %r' % (where, register_value(now, 'dc.timing', 'SH4_CYCLES') - register_value(stopped, 'dc.timing', 'SH4_CYCLES'),
                                            FRAME_CYCLES, state, stop), run)
            check(False, 'breakpoints are not active again when a client connects: the target runs, a client is connected, and the '
                  'enabled breakpoint on %s, which is reached in every frame, has not stopped it within 2 s (%r)' % (where, state), run)

        state = hit['executionState']
        check(state.get('reason') == 'breakpoint' and state.get('breakpointId') == stop['breakpointId'] and state['stopId'] == stop['stopId'] + 1,
              'the first stop in front of the new client is the next stop of the breakpoint on %s: %r, the stop that was released was %r'
              % (where, state, stop), run)

        # It was released, and it ran with the breakpoint listed and enabled: the function is reached in every
        # frame, so with breakpoints active the target would not have got further than one. (Hundreds of frames
        # on the JIT, in the second it has; two are asked for, which the slowest build makes too)
        ran = register_value(hit, 'dc.timing', 'SH4_CYCLES') - register_value(stopped, 'dc.timing', 'SH4_CYCLES')
        check(ran >= 2 * FRAME_CYCLES, 'between the stop that the grace period released and the stop in front of the new client the SH4 ran '
              '%d cycles, which is not two frames (%d): the target was not released a second before, or it did not run on' % (ran, 2 * FRAME_CYCLES), run)

        def tables(t):
            return (sorted((k, sorted(v.items())) for k, v in t['breakpoints'].items()), [(w['id'], w['expression']) for w in t['watches']],
                    sorted((k, sorted(v.items())) for k, v in t['categories'].items()))

        check(tables(hit) == tables(stopped), 'breakpoints, watches and categories are as the first client left them:\n%r\nnot\n%r'
              % (tables(hit), tables(stopped)), run)
        noted = notes(hit)
        check(len(noted) == 1, 'the event log has the note of the grace period, once (%d dc.debugger.note entries of %d)'
              % (len(noted), len(hit['eventLog'])), run)

        # The event gates were rewritten at the connect as well: a low-rate event is logged again. The one a
        # client can raise is the note for a describe that names another protocol number
        described = c.describe(protocol=2)
        check(described['executionState'] == state, 'a describe changes nothing of the stop: %r, was %r' % (described['executionState'], state), run)
        check(len(notes(described)) == 2, 'the event gates are open again for the new client: its describe with protocol 2 is noted in the event '
              'log (%d dc.debugger.note entries, 2 with the note of the grace period)' % len(notes(described)), run)

        # The connect ended the expired state of the grace period, and not only for as long as this client stays.
        # It resumes the target and leaves right behind its request: a new period begins, breakpoints are active
        # in it, and the function of every frame stops the target within a frame. With the expired state left as
        # it was, breakpoints would be inactive the moment this client is gone, and the target would run until
        # the next client connects
        c.send_and_leave('control.runUntil', {})
        time.sleep(3.0)
        d = wsclient.Client('127.0.0.1', port)
        found = d.describe()
        new = found['executionState']
        left_again = 'the client that the grace period had expired for resumed the target and left'
        check(new != state, '%s, and 3 s later the target is at the stop that was resumed (%r): the resume was not taken' % (left_again, new), run)
        check(new['state'] == 'paused', 'the connect did not end the expired state of the grace period: %s, and 3 s later, inside the new '
              'grace period, the target runs (%r). The enabled breakpoint on %s, which is reached in every frame, has not stopped it: '
              'breakpoints were inactive the moment the last client had left' % (left_again, new, where), run)
        check(new.get('reason') == 'breakpoint' and new.get('breakpointId') == stop['breakpointId'] and new['stopId'] == state['stopId'] + 1,
              '%s: the stop a client finds 3 s later is not the next stop of the breakpoint on %s: %r, the stop that was resumed was %r'
              % (left_again, where, new, state), run)
        behind = register_value(found, 'dc.timing', 'SH4_CYCLES') - register_value(hit, 'dc.timing', 'SH4_CYCLES')
        check(0 <= behind <= 3 * FRAME_CYCLES, 'the connect did not end the expired state of the grace period: %s, and the breakpoint on %s, '
              'which is reached in every frame, stopped the target %d SH4 cycles behind the stop that was resumed, which is more than three '
              'frames (%d). Breakpoints were inactive when the last client had left, and are active again since this one connected'
              % (left_again, where, behind, 3 * FRAME_CYCLES), run)
        d.close()
        print('stopped at %s (stop %d), left alone for %.1f s: released, %.1f frames without a stop there, then stop %d in front of the next '
              'client, unasked; 2 breakpoints, 2 watches and the categories as they were; note: %s; that client resumed and left, and stop %d '
              'was made %.1f frames behind its resume, inside the new grace period'
              % (where, stop['stopId'], time.monotonic() - left, ran / FRAME_CYCLES, state['stopId'], noted[0]['message'], new['stopId'],
                 behind / FRAME_CYCLES))
    except wsclient.Error as error:
        check(False, 'the client: %s' % error, run)
    finally:
        ended = run.terminate()
    run.no_bad()
    check(ended, 'the emulator did not end within 20 s of SIGTERM', run)


def config_run(args, name, keys, what=''):
    """A run that has to end by itself, with no exit key that could end it: before any machine exists."""
    run = Run(args, name).start(keys)
    if not run.wait(args.config_watchdog):
        run.kill()
        check(False, '%s%s: the run did not end by itself within %d s' % (' , '.join(keys), what, args.config_watchdog), run)
    return run


def mode_config(args):
    """Keys that need the server, without one: a configuration error. And a server that cannot be started."""
    cases = (('required', ['Debugger:Required=yes'], 'Debugger:Required'),
             ('startpaused', ['Debugger:StartPaused=yes'], 'Debugger:StartPaused'),
             ('required-off', ['Debugger:Required=yes', 'Debugger:Enabled=no'], 'Debugger:Required'))
    for name, keys, key in cases:
        run = config_run(args, name, keys)
        what = ' , '.join(keys) + ': '
        check(run.code == 2, what + 'exit code %s, not 2' % run.code, run)
        lines = [line for line in run.err.splitlines() if key in line]
        check(lines, what + 'no line on stderr names ' + key, run)
        check(any('Debugger:Enabled=yes' in line for line in lines), what + 'the line does not say that Debugger:Enabled=yes is missing', run)
        check(not has_port_line(run.out) and not has_port_line(run.err), what + 'a "Debugger: http://" line was printed', run)
        check(exit_line(run.out) is None and 'nulldc-exit' not in run.err, what + 'a nulldc-exit line was printed', run)
        run.no_bad()
        print('%s: exit code 2 after %.2f s; %s' % (' , '.join(keys), time.monotonic() - run.started, lines[0]))

    # A server that was enabled and cannot be started: this test holds its port. Fatal with Required, and with
    # StartPaused, whose start park nothing could resume; otherwise one line, and the emulator runs without it
    taken = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        taken.bind(('127.0.0.1', 0))
        taken.listen(1)
        enabled = ['Debugger:Enabled=yes', 'Debugger:Port=%d' % taken.getsockname()[1]]

        for name, key in (('taken-required', 'Debugger:Required'), ('taken-startpaused', 'Debugger:StartPaused')):
            keys = enabled + [key + '=yes']
            # a run that goes on without its server has no exit key, and with StartPaused it parks at its start
            run = config_run(args, name, keys, ', with the port taken (the emulator went on without the server)')
            what = ' , '.join(keys) + ', with the port taken: '
            check(run.code == 2, what + 'exit code %s, not 2' % run.code, run)
            lines = [line for line in run.err.splitlines() if line.startswith('Debugger: ') and key in line]
            check(lines, what + 'no "Debugger: " line on stderr names ' + key, run)
            check(not has_port_line(run.out) and not has_port_line(run.err), what + 'a "Debugger: http://" line was printed', run)
            check(exit_line(run.out) is None and 'nulldc-exit' not in run.err, what + 'a nulldc-exit line was printed', run)
            run.no_bad()
            print('%s, with the port taken: exit code 2 after %.2f s; %s' % (' , '.join(keys), time.monotonic() - run.started, lines[0]))

        keys = enabled + ['config:Exit.AfterVblanks=5']
        run = Run(args, 'taken-plain').run(keys)
        what = ' , '.join(keys) + ', with the port taken: '
        check(run.code == 0, what + 'exit code %s, not 0' % run.code, run)
        lines = [line for line in run.err.splitlines() if line.startswith('Debugger: ')]
        check(len(lines) == 1, what + '%d "Debugger: " lines on stderr, not the one that says that the server is not there' % len(lines), run)
        check(not has_port_line(run.out) and not has_port_line(run.err), what + 'a "Debugger: http://" line was printed', run)
        line = exit_line(run.out)
        check(line is not None and line.get('reason') == 'vblanks', what + 'the run did not go on to the end its exit key gives it (%r)' % (line,), run)
        print('%s, with the port taken: runs without the server, reason=%s; %s' % (' , '.join(keys), line['reason'], lines[0]))
    finally:
        taken.close()

    # And the server of the same runs starts on a port that is free: what failed above is the port, not the server
    # (on Windows a run whose environment has no SystemRoot has no Winsock, and no server on any port)
    keys = ['Debugger:Enabled=yes', 'Debugger:Port=0', 'Debugger:Required=yes', 'config:Exit.AfterVblanks=5']
    run = Run(args, 'free').run(keys)
    what = ' , '.join(keys) + ': '
    check(run.code == 0, what + 'exit code %s, not 0' % run.code, run)
    check(PORT_LINE.search(run.out) is not None, what + 'no "Debugger: http://127.0.0.1:<port>/" line on stdout: the server did not start', run)
    check(not [line for line in run.err.splitlines() if line.startswith('Debugger: ')], what + 'a "Debugger: " line on stderr', run)
    print('%s: the server starts on a port of the system\'s choice (%s), and the run ends at its exit key' % (' , '.join(keys), PORT_LINE.search(run.out).group(0)))


# What a client that reads nothing is left behind by: more than the megabyte of the rule also when the buffers
# of the kernel have taken their part (a few megabytes at most, the receive buffer of that client is made
# small), and less than the 16 MiB at which the transport ends a client
SLOW_BEHIND = 10 << 20


def tree_nodes(nodes):
    for node in nodes:
        yield node
        yield from tree_nodes(node.get('children', []))


def mode_slow(args):
    """The slow-client rule: no periodic tick is queued behind more than a megabyte that a client has not read,
    the next tick that client is sent is a full one, and no other tick is dropped."""
    run = Run(args, 'emulator').start(['Debugger:Enabled=yes', 'Debugger:Port=0', 'Debugger:Required=yes'])
    try:
        port = run.port()
        a = wsclient.Client('127.0.0.1', port)
        a.describe()
        a.wait_tick(lambda t: t['executionState']['state'] == 'running', 30, 'a running tick')
        paths = len(a.describe()['registers'])
        # the engine from the describe of a running machine: this run has no StartPaused, and the first describe
        # can be answered before the machine has selected its engine (with no sh4-debug-* capability then)
        print('engine: ' + check_engine(args, a.shape.get('capabilities', []), run, 'debugger.describe'))

        # Large ticks, before the other client is there: a watch on every register (as many as there may be),
        # and breakpoints that stop nothing
        registers = [(node['path'] + '.' + register['name']).lower() for node in tree_nodes(a.shape['deviceTree'])
                     for register in node.get('registers', [])][:256]
        for at in range(0, len(registers), 64):
            a.call('state.watch', {'expressions': registers[at:at + 64]})
        for _ in range(300):
            a.call('breakpoints.add', {'event': 'dc.holly.core.vblank', 'kind': 'event', 'enabled': False})
            # each of these is answered with a tick as well: not kept
            a.pending = []

        # The other client describes itself and reads nothing from here on
        b = wsclient.Client('127.0.0.1', port, receive_buffer=1 << 16)
        b.describe()

        # Every change of a table is a tick to both clients, and the one that shows the change first is the
        # tick the request caused: a periodic tick is built in a call of its own
        muted = False

        def change():
            nonlocal muted
            muted = not muted
            wanted = muted
            a.call('breakpoints.setCategoryStates', {'categories': {'dsp': {'muted': wanted, 'soloed': False}}})
            return a.wait_tick(lambda t: t['categories']['dsp']['muted'] == wanted, 10, 'the tick of a change of the categories')

        caused = []
        start = a.received
        while a.received - start < SLOW_BEHIND:
            caused.append(change()['tickId'])
        behind = a.received - start
        check(run.alive(), 'the emulator ended (exit code %s) with a client that reads nothing' % run.process.poll(), run)

        # Three seconds in which nobody asks for anything: every tick of these is a periodic one
        idle = a.collect(3.0)
        periodic = [t['tickId'] for t in idle]
        check(len(periodic) >= 10 and all(t['executionState']['state'] == 'running' for t in idle),
              'the client that reads got %d periodic ticks in 3 s, not about 15' % len(periodic), run)
        base = min(len(t['registers']) for t in idle)
        check(base < paths, 'a periodic tick that is not a full one has fewer register paths than a full one (%d, %d)' % (base, paths), run)

        # One more change: its tick is the next one the other client is sent
        last = change()
        caused.append(last['tickId'])

        # And now that client reads, up to this tick
        got = {}

        def read(tick):
            got[tick['tickId']] = len(tick['registers'])
            return tick['tickId'] >= last['tickId']

        b.wait_tick(read, 120, 'the tick of the last change, at the client that had stopped reading')
        order = list(got)
        check(order == sorted(order), 'the ticks reached the client that had stopped reading in another order than they were built in', run)
        queued = [tick for tick in periodic if tick in got]
        check(not queued, 'the slow-client rule: %d of the %d periodic ticks of 3 s were queued for a client that had %.1f MiB of ticks to '
              'read (tickId %s)' % (len(queued), len(periodic), behind / (1 << 20), ', '.join(str(tick) for tick in queued[:8])), run)
        dropped = [tick for tick in caused if tick not in got]
        check(not dropped, 'only periodic ticks are held back: %d of the %d ticks that a request caused did not reach the client that had '
              'stopped reading (tickId %s)' % (len(dropped), len(caused), ', '.join(str(tick) for tick in dropped[:8])), run)
        check(got[last['tickId']] == paths, 'the next tick sent to a client that periodic ticks were held back from is a full one: it has %d '
              'of the %d register paths' % (got[last['tickId']], paths), run)
        a.close()
        b.close()
        print('%d changes of a table, a tick of %d KiB each to both clients: %.1f MiB for a client that reads nothing; the %d periodic '
              'ticks of the next 3 s were not queued for it; the tick of the next change was, with %d of %d register paths (a tick that '
              'is not full has %d); it then read %d ticks, in order, every one of the %d that a request caused among them'
              % (len(caused) - 1, behind // (len(caused) - 1) >> 10, behind / (1 << 20), len(periodic), got[last['tickId']], paths, base, len(order),
                 len(caused)))
    except wsclient.Error as error:
        check(False, 'the client: %s' % error, run)
    finally:
        ended = run.terminate()
    run.no_bad()
    check(ended, 'the emulator did not end within 20 s of SIGTERM', run)


def hash_run(args, name, keys, vblanks=None):
    keys = ['config:Exit.AfterVblanks=%d' % (vblanks or args.vblanks), 'config:Exit.StateHash=yes'] + keys
    run = Run(args, name).run(keys)
    check_backend(args, run)
    line = exit_line(run.out)
    check(line is not None and 'hash' in line, name + ': the run printed no state hash', run)
    check(run.code == 0, name + ': exit code %s' % run.code, run)
    return run, line


def same_hash(base, line, what, run):
    different = [f for f in HASH_FIELDS if base.get(f) != line.get(f)]
    check(not different, what + ' differ in ' + ', '.join('%s (%s, not %s)' % (f, line.get(f), base.get(f)) for f in different), run)


def mode_hash_off(args):
    """The default of an automated run: no server. And Debugger:Enabled=no is the run without the key."""
    plain, base = hash_run(args, 'plain', [])
    check(not has_port_line(plain.out) and not has_port_line(plain.err),
          'a headless run without a Debugger: key printed a "Debugger: http://" line: it started a server unasked', plain)

    off, line = hash_run(args, 'off', ['Debugger:Enabled=no'])
    check(not has_port_line(off.out), 'the run with Debugger:Enabled=no printed a "Debugger: http://" line', off)
    same_hash(base, line, 'the run with Debugger:Enabled=no and the run without a Debugger: key', off)

    # what a hash cannot show: the key, when it is given, starts the server
    on, _ = hash_run(args, 'on', ['Debugger:Enabled=yes', 'Debugger:Port=0'], vblanks=30)
    check(PORT_LINE.search(on.out) is not None, 'the run with Debugger:Enabled=yes printed no "Debugger: http://127.0.0.1:<port>/" line on stdout', on)
    check(not has_port_line(on.err), 'the port line is on stderr', on)
    print('no server without a Debugger: key, one with Debugger:Enabled=yes; both runs without one end in hash=%s after %s vblanks'
          % (line['hash'], line['vblanks']))


def mode_hash_noclient(args):
    """A server that nobody connects to leaves the run alone."""
    _, base = hash_run(args, 'off', ['Debugger:Enabled=no'])
    on, line = hash_run(args, 'on', ['Debugger:Enabled=yes', 'Debugger:Port=0'])
    check(PORT_LINE.search(on.out) is not None, 'the run with Debugger:Enabled=yes printed no "Debugger: http://127.0.0.1:<port>/" line', on)
    same_hash(base, line, 'the run with the server on and no client and the run with the server off', on)
    print('server on with no client, and server off: both runs end in hash=%s after %s vblanks' % (line['hash'], line['vblanks']))


def mode_hash_idlebp(args):
    """A breakpoint that is listed and never stops leaves the run alone."""
    _, base = hash_run(args, 'off', ['Debugger:Enabled=no'])

    keys = ['config:Exit.AfterVblanks=%d' % args.vblanks, 'config:Exit.StateHash=yes'] + STARTPAUSED
    run = Run(args, 'idlebp').start(keys)
    code = None
    try:
        port = run.port()
        log_path = os.path.join(args.work, 'runner.txt')
        # connects at the StartPaused stop, mutes sh4, adds the breakpoint, resumes, and stays until the emulator ends
        command = runner_command(args, port, ['--idle-breakpoint', args.idle_symbol])
        with open(log_path, 'wb') as log:
            runner = emurun.start(command, args.watchdog, cwd=args.devtools, env=runner_env(args), stdout=log,
                                  stderr=subprocess.STDOUT, emulator=False)
            try:
                ended = run.wait(args.watchdog)
                try:
                    code = runner.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    code = None
            finally:
                emurun.kill(runner)
        text = emurun.read_text(log_path)
        print(text)
        check(ended, 'the run with the idle breakpoint did not end within %d s' % args.watchdog, run)
    finally:
        if run.alive():
            run.kill()
    run.no_bad()
    check('idle breakpoint' in text and 'sh4 muted, target running' in text, 'the runner did not say that it set the breakpoint and resumed', run)
    check(code == 0, 'the runner ended with exit code %s: the muted breakpoint stopped the target, or an invariant was violated' % code, run)
    # the engine, from the describe of the runner: its "server:" line has the capabilities. The last one: a runner
    # that connected before the StartPaused stop prints one more behind the describe it makes there, and the first
    # was answered before the machine had an engine
    servers = re.findall(r'^server: .*?; capabilities: ([^;]*);', text, re.M)
    check(servers, 'the runner printed no "server: ...; capabilities: ...;" line: the engine of the run is not known', run)
    engine = check_engine(args, [name.strip() for name in servers[-1].split(',')], run,
                          'debugger.describe, as the last "server:" line of the runner has it,')
    line = exit_line(run.out)
    check(line is not None and 'hash' in line, 'the run with the idle breakpoint printed no state hash', run)
    check(run.code == 0, 'exit code %s' % run.code, run)
    same_hash(base, line, 'the run with a muted breakpoint on %s and the run with the server off' % args.idle_symbol, run)
    print('engine: ' + engine)
    print('a muted breakpoint on %s with a client connected, and server off: both runs end in hash=%s after %s vblanks'
          % (args.idle_symbol, line['hash'], line['vblanks']))


# The periodic tick of the listening run: every 20 ms, and every one of them full
LISTENING_KEYS = ['Debugger:TickMs=20', 'Debugger:FullTickEvery=1']

# The node whose registers are the context of the SH4. A running machine is read where the SH4 gives up its
# slice, and the context is complete there on the interpreter and on a recompiler that says so, which the ones for
# x64 and arm64 do. On any other recompiler every running tick says registersStale and leaves this node out (design
# 2.8; the runner has the same rule in its invariant I5). No engine the tests run sends such a tick: the mode
# hash-listening refuses one, and full_running_tick keeps the design's shape of it for hash-listening-rule
SH4_CONTEXT_PATH = 'dc.sh4.cpu'


def full_running_tick(tick, counts):
    """Is a running tick a full one, and how many register values does a full one of its kind carry? counts has
    the number of registers of every path of the device tree that has registers. A full tick carries every one of
    these paths, no other; one that says registersStale carries every one of them but dc.sh4.cpu."""
    paths = dict(counts)
    if tick['executionState'].get('registersStale'):
        paths.pop(SH4_CONTEXT_PATH, None)
    return sorted(tick['registers']) == sorted(paths), sum(paths.values())


def mode_hash_listening(args):
    """A client that only listens leaves the run alone. Every full tick reads every register of the device tree
    on the emulator thread: the system bus, the PVR's register file, the register file of the AICA by field, the
    drive. The periodic tick is made a full one every 20 ms, so that a run of seconds is read hundreds of times."""
    _, base = hash_run(args, 'off', ['Debugger:Enabled=no'])

    keys = ['config:Exit.AfterVblanks=%d' % args.vblanks, 'config:Exit.StateHash=yes'] + STARTPAUSED + LISTENING_KEYS
    run = Run(args, 'listening').start(keys)
    ticks = []
    try:
        port = run.port()
        client = wsclient.Client('127.0.0.1', port)
        first = client.describe()
        check(first['executionState'].get('reason') == 'start', 'the emulator is not at its StartPaused stop: %r' % (first['executionState'],), run)
        print('engine: ' + check_engine(args, client.shape.get('capabilities', []), run, 'debugger.describe'))
        counts = {node['path']: len(node['registers']) for node in tree_nodes(client.shape['deviceTree']) if node.get('registers')}
        # at a stop the context of the SH4 is complete, whatever the engine: every path
        check(sorted(first['registers']) == sorted(counts), 'the tick behind debugger.describe is not a full one: %d of %d paths'
              % (len(first['registers']), len(counts)), run)

        # the one request behind the describe: the run begins. From here on the client only receives
        client.call('control.runUntil', {})

        def take(tick):
            ticks.append(tick)
            return False

        try:
            client.wait_tick(take, args.watchdog, 'the end of the run')
            ended = True
        except wsclient.Timeout:
            ended = False
        except wsclient.Error:
            # the run ended by its exit key, and the connection with it
            ended = True
        check(ended, 'the run with the listening client did not end within %d s' % args.watchdog, run)
        ended = run.wait(30)
        check(ended, 'the emulator did not end within 30 s of the end of its connection', run)
    except wsclient.Error as error:
        check(False, 'the client: %s' % error, run)
    finally:
        if run.alive():
            run.kill()
    run.no_bad()

    # running ticks, and behind them the ticks of the end of the run: the host stops the machine and takes it away
    states = [tick['executionState'] for tick in ticks]
    last = max([i for i, state in enumerate(states) if state['state'] == 'running'], default=-1)
    running = ticks[:last + 1]
    full = [tick for tick in running if full_running_tick(tick, counts)[0]]
    stops = [state for state in states[:last + 1] if state['state'] != 'running']
    check(not stops, 'the target stopped while the client listened: %d of the ticks in front of the last running one are paused (%r)'
          % (len(stops), stops[:3]), run)
    end = [state.get('reason') for state in states[last + 1:]]
    check(all(reason in ('host-stopped', 'no-game') for reason in end), 'the ticks behind the last running one are not those of the end of '
          'the run: %r' % (end,), run)
    check(len(full) >= 10, 'the listening client got %d full ticks while the target ran (%d ticks in all): too few to say anything about a run '
          'that is read' % (len(full), len(ticks)), run)
    values = sum(len(entries) for entries in full[-1]['registers'].values())
    registers = full_running_tick(full[-1], counts)[1]
    check(values == registers, 'a full running tick has %d register values, the device tree %d registers on its %d paths'
          % (values, registers, len(full[-1]['registers'])), run)
    # every engine of the tests has the SH4's context complete where a running machine is read (SH4DBG_CTX_AT_SLICE)
    stale = sum(1 for tick in running if tick['executionState'].get('registersStale'))
    check(stale == 0, '%d of the %d running ticks say registersStale: the SH4\'s context of a running tick is complete on '
          'the interpreter and on the recompilers of x86-64 and arm64 hosts' % (stale, len(running)), run)

    line = exit_line(run.out)
    check(line is not None and 'hash' in line, 'the run with the listening client printed no state hash', run)
    check(run.code == 0, 'exit code %s' % run.code, run)
    same_hash(base, line, 'the run with a client that only listens and the run with the server off', run)
    print('a client that only listens, sent %d full ticks of %d registers on %d paths while the target ran, and server off: both runs end in '
          'hash=%s after %s vblanks' % (len(full), registers, len(full[-1]['registers']), line['hash'], line['vblanks']))


def mode_hash_listening_rule():
    """What full_running_tick() makes of ticks that are made up. The tick without dc.sh4.cpu is what a recompiler
    sends whose SH4 context is not complete at the end of a slice. The ones for x64 and arm64 are not of that kind
    and the interpreter is not either, so no run of the tests has such a tick (the mode hash-listening refuses one):
    the design's rule for it is held here."""
    counts = {'dc.sh4.cpu': 69, 'dc.aica.arm7': 41, 'dc.aica.dsp': 9, 'dc.timing': 5, 'dc.holly.spg': 9, 'dc.aica.sgc.0': 38}
    every = sorted(counts)
    base = ['dc.aica.arm7', 'dc.aica.dsp', 'dc.sh4.cpu', 'dc.timing']
    all_of_them = sum(counts.values())

    def without(paths, *left_out):
        return [path for path in paths if path not in left_out]

    def tick(paths, stale=None):
        state = {'state': 'running'}
        if stale is not None:
            state['registersStale'] = stale
        return {'executionState': state, 'registers': {path: [{'name': 'R%d' % n} for n in range(counts[path])] for path in paths}}

    # (what the tick is, the tick, is it full, the register values of a full tick of its kind)
    cases = [
        ('every path', tick(every), True, all_of_them),
        ('every path, registersStale false', tick(every, False), True, all_of_them),
        ('registersStale, every path but dc.sh4.cpu', tick(without(every, 'dc.sh4.cpu'), True), True, all_of_them - 69),
        ('every path but dc.sh4.cpu, and no registersStale', tick(without(every, 'dc.sh4.cpu')), False, all_of_them),
        ('registersStale, and dc.sh4.cpu all the same', tick(every, True), False, all_of_them - 69),
        ('registersStale, without dc.sh4.cpu and without dc.holly.spg', tick(without(every, 'dc.sh4.cpu', 'dc.holly.spg'), True), False, all_of_them - 69),
        ('every path but dc.aica.sgc.0', tick(without(every, 'dc.aica.sgc.0')), False, all_of_them),
        ('the four paths of every running tick', tick(base), False, all_of_them),
        ('registersStale, the three of them that are left', tick(without(base, 'dc.sh4.cpu'), True), False, all_of_them - 69),
    ]
    for what, made_up, full, values in cases:
        got = full_running_tick(made_up, counts)
        check(got == (full, values), 'a running tick with %s is taken for %s with %d register values in a full tick of its kind; it is %s, with %d'
              % (what, 'a full one' if got[0] else 'one that is not full', got[1], 'a full one' if full else 'not full', values))
    print('%d made-up running ticks, each taken for what it is: a tick that says registersStale is a full one without %s and only without it, any '
          'other with every path that has registers' % (len(cases), SH4_CONTEXT_PATH))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--emulator', default='', help='every mode but hash-listening-rule')
    parser.add_argument('--work', default='', help='every mode but hash-listening-rule')
    parser.add_argument('--guest', default='', help='every mode but hash-listening-rule: 2ndmix.elf; m1, m2, parts, hash-listening: or another '
                                                    'program, the guest of the tree; m3dsp, dsp: the guest of the tree')
    parser.add_argument('--mode', required=True, choices=('m1', 'm2', 'm3', 'm3dsp', 'parts', 'dsp', 'grace', 'config', 'slow', 'hash-off', 'hash-noclient',
                                                          'hash-idlebp', 'hash-listening', 'hash-listening-rule'))
    parser.add_argument('--engine', default='jit', choices=('jit', 'interp'))
    parser.add_argument('--fallback', action='store_true', help='Debugger:ForceFallback=yes')
    parser.add_argument('--devtools', default='', help='the dreamcast-devtools checkout, with its node_modules (m1, m2, m3, m3dsp, parts, dsp, hash-idlebp)')
    parser.add_argument('--npm', default='npm')
    parser.add_argument('--idle-symbol', default='', help='hash-idlebp, grace: a symbol of the guest that is reached in every frame')
    parser.add_argument('--parts', default='', help='parts: <scenario>:<part>[+<part>...], as the runner takes it, e.g. S16:arm7')
    parser.add_argument('--only', default='', help='m2: the scenarios of its second run, as the runner takes them, e.g. S00,S16')
    parser.add_argument('--require-note', action='append', default=[], help='m2, parts, m3dsp, dsp: a note the run has to have, handed on to the runner')
    parser.add_argument('--vblanks', type=int, default=600, help='Exit.AfterVblanks of the hash runs')
    parser.add_argument('--watchdog', type=int, default=600, help='seconds a run, or the runner, may take')
    parser.add_argument('--config-watchdog', type=int, default=60, help='config: seconds a run may take to end by itself')
    parser.add_argument('--keep', action='store_true', help='keep the work directory of a test that passed')
    args = parser.parse_args()

    if args.mode == 'hash-listening-rule':
        # nothing is run, and nothing is written
        try:
            mode_hash_listening_rule()
        except Failed as failure:
            print('FAIL: %s' % failure)
            return 1
        print('PASS')
        return 0

    missing = ['--' + name for name in ('emulator', 'work', 'guest') if not getattr(args, name)]
    if missing:
        parser.error('the following arguments are required: ' + ', '.join(missing))

    # nulldc3 is nulldc3.exe on Windows
    args.emulator = emurun.executable(os.path.abspath(args.emulator))
    args.work = os.path.abspath(args.work)
    args.guest = os.path.abspath(args.guest)

    for path, what in ((args.emulator, 'emulator'), (args.guest, 'guest')):
        if not os.path.isfile(path):
            print('FAIL: no %s at %s' % (what, path))
            return 1

    if args.mode in ('m1', 'm2', 'm3', 'm3dsp', 'parts', 'dsp', 'hash-idlebp'):
        args.devtools = os.path.abspath(args.devtools)
        if not os.path.isdir(os.path.join(args.devtools, 'node_modules')):
            print('FAIL: no node_modules in the devtools checkout ' + args.devtools)
            return 1
        # on Windows npm.cmd: the npm without a suffix beside it is a shell script
        npm = emurun.npm(args.npm)
        if npm is None:
            print('FAIL: no npm to run the contract runner with (%s)' % args.npm)
            return 1
        args.npm = npm
    if args.mode in ('hash-idlebp', 'grace') and not args.idle_symbol:
        print('FAIL: --idle-symbol is missing')
        return 1
    if args.mode == 'parts' and not re.match(r'^S\d+:[A-Za-z0-9]+(\+[A-Za-z0-9]+)*$', args.parts):
        print('FAIL: --parts is missing, or is not <scenario>:<part>[+<part>...]')
        return 1
    if args.only and (args.mode != 'm2' or not re.match(r'^S\d+(,S\d+)*$', args.only)):
        print('FAIL: --only is for the mode m2, and is <scenario>[,<scenario>...]')
        return 1
    if args.require_note and args.mode not in ('m2', 'parts', 'm3dsp', 'dsp'):
        print('FAIL: --require-note is for the modes m2, parts, m3dsp and dsp')
        return 1
    if args.fallback and args.engine != 'jit':
        print('FAIL: --fallback is for --engine jit: the interpreter executes its breakpoints and steps itself, and '
              'Debugger:ForceFallback does not change that (it reports sh4-debug-native)')
        return 1

    modes = {'m1': mode_m1, 'm2': mode_m2, 'm3': mode_m3, 'm3dsp': mode_m3dsp, 'parts': mode_parts, 'dsp': mode_dsp, 'grace': mode_grace,
             'config': mode_config, 'slow': mode_slow,
             'hash-off': mode_hash_off, 'hash-noclient': mode_hash_noclient, 'hash-idlebp': mode_hash_idlebp, 'hash-listening': mode_hash_listening}

    shutil.rmtree(args.work, ignore_errors=True)
    os.makedirs(args.work)

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
