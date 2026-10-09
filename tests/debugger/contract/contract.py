#!/usr/bin/env python3
"""The emulator's debugger server against the contract runner of the devtools UI, and what goes with it.

    contract.py --emulator <devcast> --work <dir> --guest <2ndmix.elf> --mode <mode> [--engine jit|interp]
                [--fallback] [--devtools <dreamcast-devtools checkout>] [--npm <npm>] [--idle-symbol <symbol>]

Every emulator is run as tests/system runs one: headless, an environment of its own with fresh HOME,
XDG_CONFIG_HOME and XDG_DATA_HOME under the work directory, no display, the real-time clock pinned, a core file
size limit of 1 byte, and a watchdog. A process this script starts is ended by its pid. README.md has the modes:

    m1             the gate of milestone M1: the emulator at its StartPaused stop, and the contract runner of the
                   devtools UI against it (npm run contract -- --url ... --profile emulator --elf ... --milestone M1)
    grace          a client leaves the target stopped at a breakpoint that is reached in every frame; 11 s later
                   the target has run on without stopping there, and the breakpoint stops it again, unasked,
                   in front of the next client; the tables as they were, the note of the grace period in the
                   event log, and the event gates open again
    config         the configuration errors: three runs that end by themselves with exit code 2; and a server
                   that was enabled and cannot be started (its port is taken): exit code 2 with
                   Debugger:Required or Debugger:StartPaused, one line and a run without the server otherwise
    slow           the slow-client rule: a client that has stopped reading is queued no periodic tick behind
                   a megabyte of ticks, but every tick that a request caused, and a full tick next
    hash-off       the state hash of a run with no Debugger: key and of one with Debugger:Enabled=no; no port
                   line without the key, the port line with Debugger:Enabled=yes
    hash-noclient  the state hash with the server on and no client, and with the server off
    hash-idlebp    the state hash with a client connected and a muted breakpoint on a hot address, and with the
                   server off
"""

import argparse
import os
import re
import resource
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time

# the client of this directory; nothing is written into the source tree for it
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import wsclient  # noqa: E402

RTC_PIN = 946684800

BAD = ('Fatal error', 'Verify Failed', 'fault_handler', 'AddressSanitizer', 'ThreadSanitizer', 'runtime error:')

HASH_FIELDS = ('reason', 'vblanks', 'hash', 'sh4', 'sched', 'ram', 'vram', 'aram', 'arm7')

PORT_LINE = re.compile(r'^Debugger: http://127\.0\.0\.1:(\d+)/$', re.M)


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
        for sub in ('home', 'cfg', os.path.join('data', 'reicast', 'data')):
            os.makedirs(os.path.join(self.dir, sub))
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
        command = [self.args.emulator, '-config']
        for i, item in enumerate(items):
            if i:
                command.append(',')
            command.append(item)
        command.append(self.args.guest)
        return command

    def start(self, keys):
        env = {'HOME': os.path.join(self.dir, 'home'), 'XDG_CONFIG_HOME': os.path.join(self.dir, 'cfg'),
               'XDG_DATA_HOME': os.path.join(self.dir, 'data')}
        for name in ('ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS', 'LSAN_OPTIONS'):
            if name in os.environ:
                env[name] = os.environ[name]

        def limits():
            # a crash is not piped to a core handler (WSL2 runs it for any other limit)
            resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))

        self.cmdline = self.command(keys)
        self.out_file = open(self.out_path, 'wb')
        self.err_file = open(self.err_path, 'wb')
        self.process = subprocess.Popen(self.cmdline, cwd=self.dir, env=env, stdin=subprocess.DEVNULL,
                                        stdout=self.out_file, stderr=self.err_file, preexec_fn=limits)
        self.started = time.monotonic()
        return self

    def read(self):
        with open(self.out_path, 'rb') as f:
            self.out = f.read().decode('utf-8', 'replace')
        with open(self.err_path, 'rb') as f:
            self.err = f.read().decode('utf-8', 'replace')
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
        """Ends the process by its pid, whatever it is doing."""
        if self.process is not None and self.process.poll() is None:
            self.process.kill()
        if self.process is not None:
            self.code = self.process.wait()
        self.close_files()
        self.read()

    def terminate(self, seconds=20):
        """SIGTERM, as a harness ends an emulator that is parked; SIGKILL when that does not end it."""
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
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
    env = dict(os.environ)
    # npm starts node, which is where npm is
    env['PATH'] = os.path.dirname(os.path.abspath(args.npm)) + os.pathsep + env.get('PATH', '')
    env.pop('DISPLAY', None)
    return env


STARTPAUSED = ['Debugger:Enabled=yes', 'Debugger:Port=0', 'Debugger:Required=yes', 'Debugger:StartPaused=yes']


def mode_m1(args):
    """The gate of milestone M1."""
    run = Run(args, 'emulator').start(STARTPAUSED)
    try:
        port = run.port()
        log_path = os.path.join(args.work, 'runner.txt')
        command = runner_command(args, port, ['--milestone', 'M1', '--json-file', os.path.join(args.work, 'summary.json')])
        with open(log_path, 'wb') as log:
            try:
                code = subprocess.call(command, cwd=args.devtools, env=runner_env(args), stdin=subprocess.DEVNULL, stdout=log,
                                       stderr=subprocess.STDOUT, timeout=args.watchdog)
            except subprocess.TimeoutExpired:
                code = None
        with open(log_path, 'rb') as log:
            text = log.read().decode('utf-8', 'replace')

        # the report of the runner is the report of this test
        print(text)
        check(code is not None, 'the contract runner did not end within %d s' % args.watchdog, run)
        check(run.alive(), 'the emulator ended (exit code %s) while the runner was at it' % run.process.poll(), run)
        check(code == 0, 'the contract runner ended with exit code %d: the gate of M1 did not pass' % code, run)
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
    print('the gate of M1: ' + summary[-1])


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

        # an enabled breakpoint on a function the program calls in every frame, and the target left stopped at it
        a.call('breakpoints.add', {'event': 'dc.sh4.cpu.pc', 'address': address, 'kind': 'code', 'enabled': True})
        a.call('breakpoints.add', {'event': 'dc.holly.core.vblank', 'kind': 'event', 'enabled': False})
        a.call('state.watch', {'expressions': ['dc.sh4.cpu.pc', 'dc.timing.sh4_cycles']})
        a.call('breakpoints.setCategoryStates', {'categories': {'arm7': {'muted': True, 'soloed': False}, 'dsp': {'muted': False, 'soloed': False}}})
        a.call('control.runUntil', {})
        # (the boot and the set-up of the program are in front of it: a quarter of a second on the JIT, the better
        # part of a minute on the interpreter of a build with a sanitizer)
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
        c.close()
        print('stopped at %s (stop %d), left alone for %.1f s: released, %.1f frames without a stop there, then stop %d in front of the next '
              'client, unasked; 2 breakpoints, 2 watches and the categories as they were; note: %s'
              % (where, stop['stopId'], time.monotonic() - left, ran / FRAME_CYCLES, state['stopId'], noted[0]['message']))
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
            runner = subprocess.Popen(command, cwd=args.devtools, env=runner_env(args), stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
            try:
                ended = run.wait(args.watchdog)
                try:
                    code = runner.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    code = None
            finally:
                if runner.poll() is None:
                    runner.kill()
                    runner.wait()
        with open(log_path, 'rb') as log:
            text = log.read().decode('utf-8', 'replace')
        print(text)
        check(ended, 'the run with the idle breakpoint did not end within %d s' % args.watchdog, run)
    finally:
        if run.alive():
            run.kill()
    run.no_bad()
    check('idle breakpoint' in text and 'sh4 muted, target running' in text, 'the runner did not say that it set the breakpoint and resumed', run)
    check(code == 0, 'the runner ended with exit code %s: the muted breakpoint stopped the target, or an invariant was violated' % code, run)
    line = exit_line(run.out)
    check(line is not None and 'hash' in line, 'the run with the idle breakpoint printed no state hash', run)
    check(run.code == 0, 'exit code %s' % run.code, run)
    same_hash(base, line, 'the run with a muted breakpoint on %s and the run with the server off' % args.idle_symbol, run)
    print('a muted breakpoint on %s with a client connected, and server off: both runs end in hash=%s after %s vblanks'
          % (args.idle_symbol, line['hash'], line['vblanks']))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--emulator', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--guest', required=True, help='2ndmix.elf')
    parser.add_argument('--mode', required=True, choices=('m1', 'grace', 'config', 'slow', 'hash-off', 'hash-noclient', 'hash-idlebp'))
    parser.add_argument('--engine', default='jit', choices=('jit', 'interp'))
    parser.add_argument('--fallback', action='store_true', help='Debugger:ForceFallback=yes')
    parser.add_argument('--devtools', default='', help='the dreamcast-devtools checkout, with its node_modules (m1, hash-idlebp)')
    parser.add_argument('--npm', default='npm')
    parser.add_argument('--idle-symbol', default='', help='hash-idlebp, grace: a symbol of the guest that is reached in every frame')
    parser.add_argument('--vblanks', type=int, default=600, help='Exit.AfterVblanks of the hash runs')
    parser.add_argument('--watchdog', type=int, default=600, help='seconds a run, or the runner, may take')
    parser.add_argument('--config-watchdog', type=int, default=60, help='config: seconds a run may take to end by itself')
    parser.add_argument('--keep', action='store_true', help='keep the work directory of a test that passed')
    args = parser.parse_args()

    args.emulator = os.path.abspath(args.emulator)
    args.work = os.path.abspath(args.work)
    args.guest = os.path.abspath(args.guest)

    for path, what in ((args.emulator, 'emulator'), (args.guest, 'guest')):
        if not os.path.isfile(path):
            print('FAIL: no %s at %s' % (what, path))
            return 1

    if args.mode in ('m1', 'hash-idlebp'):
        args.devtools = os.path.abspath(args.devtools)
        if not os.path.isdir(os.path.join(args.devtools, 'node_modules')):
            print('FAIL: no node_modules in the devtools checkout ' + args.devtools)
            return 1
        npm = shutil.which(args.npm)
        if npm is None:
            print('FAIL: no npm to run the contract runner with (%s)' % args.npm)
            return 1
        args.npm = npm
    if args.mode in ('hash-idlebp', 'grace') and not args.idle_symbol:
        print('FAIL: --idle-symbol is missing')
        return 1

    modes = {'m1': mode_m1, 'grace': mode_grace, 'config': mode_config, 'slow': mode_slow, 'hash-off': mode_hash_off,
             'hash-noclient': mode_hash_noclient, 'hash-idlebp': mode_hash_idlebp}

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
