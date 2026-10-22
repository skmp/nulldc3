#!/usr/bin/env python3
"""PL-D78's rule of contract.py on made-up summaries of the contract runner: debugger.contract.s04.rule.

    s04_rule_test.py --work <dir>

S04 on the recompiler of an arm64 host is recorded, not gated: there a step can move dc.timing.SH4_CYCLES by less
than a slice (contract.py, the top of the file). The rule is contract.py's s04_recorded, which run_runner asks with
platform.machine() and --engine when the runner did not pass. This test holds it on summaries that are made up, as
the runner writes them with --json-file (ContractSummary of the UI's scripts/contract/run.ts), on every host:

    accepted    on arm64 and aarch64 with --engine jit, with and without --fallback: a run whose one failed
                scenario is S04, failed on "dc.timing.SH4_CYCLES went back from <a> to <b> in a step" with
                0 < a - b < 448 (SH4_TIMESLICE), or on "dc.timing.SH4_CYCLES did not increase in a step that did not
                end at an exception vector", and nothing else of the summary out of place
    refused     a went-back of 448 or more (and one that went forward, a "went back" of 0); a second failed
                scenario, S04 passed and another failed, another scenario failed on the detail while the list of the
                required scenarios not passed names S04, a count of failed, partial, not applicable or passed out of
                place by itself, a partial, a scenario not applicable, an invariant violation, a clean-up problem, a
                required note missing, another required scenario not passed, nothing verified;
                another check of S04 (its PC, two vectors in a row, the SH4_CYCLES of a step of the ARM7), a second
                detail of S04; the interpreter; an x86-64 host (x86_64, AMD64); an exit code other than 1 (2: no run);
                no summary

and run_runner itself, with the emulator, the runner and the describe behind it made up by the test (no emulator
or runner is run) and platform.machine() answering the host of each case: the accepted forms pass the target and it
prints "S04: recorded (PL-D78): <the detail>"; every refused one fails it, with the message of a runner that did
not pass; a runner that passed passes as before, with no such line.

Python 3.9 and the standard library. Exit code 0 when all of it held.
"""

import argparse
import copy
import importlib.util
import io
import json
import os
import platform
import shutil
import sys
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))

# nothing is written into the source tree for the modules
sys.dont_write_bytecode = True


class Failed(Exception):
    pass


def check(ok, what):
    if not ok:
        raise Failed(what)


def load_contract():
    """contract.py as a module of its own: it runs nothing when it is not the main program."""
    path = os.path.join(HERE, 'contract.py')
    spec = importlib.util.spec_from_file_location('contract_under_test', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


WENT_BACK = 'dc.timing.SH4_CYCLES went back from %d to %d in a step'
DID_NOT_MOVE = 'dc.timing.SH4_CYCLES did not increase in a step that did not end at an exception vector'

GATE = ['S00', 'S01', 'S02', 'S03', 'S04', 'S05', 'S06']


def scenario(sid, status='passed', details=None):
    return {'id': sid, 'title': sid, 'status': status, 'durationMs': 10, 'details': list(details or []), 'notes': []}


def summary_with(detail):
    """A summary of a gate in which S04 alone failed, on detail."""
    scenarios = [scenario(sid) for sid in GATE]
    scenarios[GATE.index('S04')] = scenario('S04', 'failed', [detail])
    return {
        'ok': False, 'nothingVerified': False, 'milestone': 'M1', 'dspProgram': False, 'selected': list(GATE),
        'required': list(GATE), 'requiredNotPassed': [{'id': 'S04', 'status': 'failed'}], 'requiredNotes': [],
        'requiredNotesMissing': [], 'url': 'ws://127.0.0.1:1/ws', 'profile': 'emulator', 'timeoutFactor': 1,
        'server': {'emulator': {'name': 'nullDC', 'version': '3.0.0-pre', 'build': 'x'}, 'protocol': 1, 'ui': None,
                   'capabilities': ['sh4-debug-fallback'], 'targets': []},
        'passed': len(GATE) - 1, 'failed': 1, 'partial': 0, 'notApplicable': 0, 'ticksChecked': 1234,
        'invariantViolations': [], 'cleanUpProblems': [], 'finalState': 'paused (start)', 'scenarios': scenarios,
    }


def changed(base, change):
    summary = copy.deepcopy(base)
    change(summary)
    return summary


def second_failure(summary):
    summary['scenarios'][GATE.index('S05')] = scenario('S05', 'failed', ['a breakpoint was not hit'])
    summary['failed'] = 2
    summary['passed'] -= 1
    summary['requiredNotPassed'].append({'id': 'S05', 'status': 'failed'})


def other_failed(summary):
    # S04 passed; S03 failed, on what S04 fails on
    summary['scenarios'][GATE.index('S04')] = scenario('S04')
    summary['scenarios'][GATE.index('S03')] = scenario('S03', 'failed', [WENT_BACK % (1000, 990)])
    summary['requiredNotPassed'] = [{'id': 'S03', 'status': 'failed'}]


def mismatch(summary):
    # the scenarios say S03 failed on the detail, the list of the required that did not pass says S04
    summary['scenarios'][GATE.index('S04')] = scenario('S04')
    summary['scenarios'][GATE.index('S03')] = scenario('S03', 'failed', [DID_NOT_MOVE])


def count(name, value):
    def change(summary):
        summary[name] = value
    return change


def partial(summary):
    summary['scenarios'][GATE.index('S06')] = scenario('S06', 'partial', ['parts: a, not b'])
    summary['partial'] = 1
    summary['passed'] -= 1
    summary['requiredNotPassed'].append({'id': 'S06', 'status': 'partial'})


def not_applicable(summary):
    summary['scenarios'][GATE.index('S06')] = scenario('S06', 'not-applicable', ['the server does not claim it'])
    summary['notApplicable'] = 1
    summary['passed'] -= 1
    summary['requiredNotPassed'].append({'id': 'S06', 'status': 'not-applicable'})


def invariant(summary):
    summary['invariantViolations'] = ['I2: tickId 12 after 13']


def invariant_in_s04(summary):
    summary['invariantViolations'] = ['I5: a paused tick says registersStale']
    summary['scenarios'][GATE.index('S04')]['details'].append('invariant: I5: a paused tick says registersStale')


def clean_up(summary):
    summary['cleanUpProblems'] = ['a breakpoint was left behind']


def note_missing(summary):
    summary['requiredNotes'] = ['arm7: held at the start']
    summary['requiredNotesMissing'] = ['arm7: held at the start']


def not_run(summary):
    summary['requiredNotPassed'].append({'id': 'S19', 'status': 'not-run'})


def nothing_verified(summary):
    summary['nothingVerified'] = True


def second_detail(summary):
    summary['scenarios'][GATE.index('S04')]['details'].append('dc.timing.SH4_CYCLES did not increase in a step that did not end at an exception vector')


def no_details(summary):
    summary['scenarios'][GATE.index('S04')]['details'] = []


ACCEPTED_DETAILS = (WENT_BACK % (1000000, 999999), WENT_BACK % (2004810276, 2004809829), WENT_BACK % (448, 1), DID_NOT_MOVE)

REFUSED_DETAILS = (
    (WENT_BACK % (2004810276, 2004809828), 'a went-back of 448, SH4_TIMESLICE'),
    (WENT_BACK % (100000, 90000), 'a went-back of 10000'),
    (WENT_BACK % (5000, 5000), 'a "went back" of 0'),
    (WENT_BACK % (5000, 5100), 'a "went back" that went forward'),
    ('dc.timing.SH4_CYCLES went back from 5000 to 4990 across a step of the ARM7', 'the SH4_CYCLES check of a step of the ARM7'),
    ('"mov #1,r0" at 0x8C0100A2 is not control flow, and the step ended at 0x8C0100A8, not at 0x8C0100A4 or an exception vector',
     'S04\'s check of the PC'),
    ('two steps in a row ended at an exception vector: the step to 0x8C000600, and control.step {sh4} from there, which ended at 0x8C000600. '
     'One step may (a deferred interrupt is accepted before an instruction runs); the step after it starts in the handler with SR.BL set and cannot be interrupted',
     'S04\'s check of two vectors in a row'),
    ('dc.timing.SH4_CYCLES is in every paused tick (2.8)', 'S04 without SH4_CYCLES in a tick'),
    (WENT_BACK % (5000, 4990) + ' ', 'a went-back with more behind it'),
    ('dc.timing.SH4_CYCLES went back from 0x1388 to 0x1380 in a step', 'a went-back in hex'),
)

REFUSED_SUMMARIES = (
    (second_failure, 'a second failed scenario'),
    (other_failed, 'S04 passed and another scenario failed'),
    (mismatch, 'another scenario failed on the detail, the required list naming S04'),
    (count('failed', 2), 'a count of 2 failed'),
    (count('partial', 1), 'a count of 1 partial'),
    (count('notApplicable', 1), 'a count of 1 not applicable'),
    (count('passed', 0), 'a count of 0 passed'),
    (partial, 'a partial'),
    (not_applicable, 'a scenario not applicable'),
    (invariant, 'an invariant violation'),
    (invariant_in_s04, 'an invariant violation in S04'),
    (clean_up, 'a clean-up problem'),
    (note_missing, 'a required note missing'),
    (not_run, 'a required scenario not run'),
    (nothing_verified, 'nothing verified'),
    (second_detail, 'a second detail of S04'),
    (no_details, 'S04 without its detail'),
)

ARM_HOSTS = ('arm64', 'aarch64')
X64_HOSTS = ('x86_64', 'AMD64')


def test_rule(contract):
    """s04_recorded on its own."""
    cases = 0
    for machine in ARM_HOSTS:
        for detail in ACCEPTED_DETAILS:
            got = contract.s04_recorded(machine, 'jit', 1, summary_with(detail))
            check(got == detail, '%s, jit: S04 failed on "%s" is not recorded: %r' % (machine, detail, got))
            cases += 1
        for detail, what in REFUSED_DETAILS:
            got = contract.s04_recorded(machine, 'jit', 1, summary_with(detail))
            check(got is None, '%s, jit: %s is recorded: %r' % (machine, what, got))
            cases += 1
        for change, what in REFUSED_SUMMARIES:
            got = contract.s04_recorded(machine, 'jit', 1, changed(summary_with(DID_NOT_MOVE), change))
            check(got is None, '%s, jit: %s is recorded: %r' % (machine, what, got))
            cases += 1
        for engine in ('interp', 'jitfallback', ''):
            got = contract.s04_recorded(machine, engine, 1, summary_with(DID_NOT_MOVE))
            check(got is None, '%s: the engine %r is recorded: %r' % (machine, engine, got))
            cases += 1
        for code in (0, 2, 3, None):
            got = contract.s04_recorded(machine, 'jit', code, summary_with(DID_NOT_MOVE))
            check(got is None, '%s, jit: the exit code %r is recorded: %r' % (machine, code, got))
            cases += 1
        for summary, what in ((None, 'no summary'), ([], 'a summary that is a list'), ({}, 'an empty summary'),
                              (changed(summary_with(DID_NOT_MOVE), lambda s: s.pop('scenarios')), 'a summary without scenarios')):
            got = contract.s04_recorded(machine, 'jit', 1, summary)
            check(got is None, '%s, jit: %s is recorded: %r' % (machine, what, got))
            cases += 1
    check(contract.s04_recorded('ARM64', 'jit', 1, summary_with(DID_NOT_MOVE)) == DID_NOT_MOVE, 'ARM64 is not an arm64 host')
    for machine in X64_HOSTS + ('i686', 'riscv64', 'armv7l', ''):
        for detail in ACCEPTED_DETAILS:
            got = contract.s04_recorded(machine, 'jit', 1, summary_with(detail))
            check(got is None, 'the host %r: S04 is recorded: %r' % (machine, got))
            cases += 1
    return 's04_recorded: %d made-up summaries, each accepted or refused as PL-D78 says' % cases


class FakeProcess(object):
    def __init__(self, code):
        self.code = code

    def wait(self, timeout=None):
        return self.code

    def poll(self):
        return self.code


class FakeRun(object):
    """The emulator of run_runner: at its StartPaused stop, until the harness ends it."""

    def __init__(self, args, name):
        self.process = FakeProcess(None)
        self.out = ''
        self.err = ''

    def start(self, keys):
        return self

    def port(self, seconds=30):
        return 1

    def alive(self):
        return True

    def read(self):
        return self.out

    def terminate(self, seconds=20):
        return True

    def no_bad(self):
        pass


def runner_text(summary):
    """What the runner prints at its end, for that summary."""
    s = summary
    return ('%d passed, %d failed, %d partial, %d not applicable; %d ticks checked, %d invariant violation%s; left %s\n%s\n'
            % (s['passed'], s['failed'], s['partial'], s['notApplicable'], s['ticksChecked'], len(s['invariantViolations']),
               '' if len(s['invariantViolations']) == 1 else 's', s['finalState'], 'contract: OK' if s['ok'] else 'contract: FAILED'))


def run_gate(contract, work, machine, engine, fallback, code, summary):
    """run_runner with a made-up emulator and runner on a host whose platform.machine() is machine: (passed, its
    output, the failure's text)."""
    saved = (contract.Run, contract.emurun.start, contract.described_engine, contract.platform.machine)

    def start(argv, timeout, cwd=None, env=None, stdout=None, stderr=None, emulator=True):
        json_file = argv[argv.index('--json-file') + 1]
        if summary is not None:
            with open(json_file, 'w', encoding='utf-8') as f:
                json.dump(summary, f)
        stdout.write(runner_text(summary if summary is not None else summary_with(DID_NOT_MOVE)).encode('utf-8'))
        stdout.flush()
        return FakeProcess(code)

    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    args = argparse.Namespace(work=work, devtools=work, npm=os.path.join(work, 'npm'), guest=os.path.join(work, '2ndmix.elf'),
                              watchdog=60, engine=engine, fallback=fallback)
    out = io.StringIO()
    try:
        contract.Run = FakeRun
        contract.emurun.start = start
        contract.described_engine = lambda args, port, run: 'the SH4 on the recompiler, its breakpoints and steps on sh4-debug-fallback, as asked'
        contract.platform.machine = lambda: machine
        try:
            with redirect_stdout(out):
                contract.run_runner(args, ['--milestone', 'M1'], 'the gate of M1')
        except contract.Failed as failure:
            return False, out.getvalue(), str(failure)
        return True, out.getvalue(), ''
    finally:
        contract.Run, contract.emurun.start, contract.described_engine, contract.platform.machine = saved


def passed_summary():
    summary = summary_with(DID_NOT_MOVE)
    summary['scenarios'][GATE.index('S04')] = scenario('S04')
    summary.update(ok=True, failed=0, passed=len(GATE), requiredNotPassed=[])
    return summary


def test_run_runner(contract, work):
    """run_runner on the made-up runner, with platform.machine() answering the host of each case."""
    cases = 0
    run = os.path.join(work, 'run_runner')
    for machine in ARM_HOSTS:
        for fallback in (False, True):
            for detail in ACCEPTED_DETAILS:
                ok, out, why = run_gate(contract, run, machine, 'jit', fallback, 1, summary_with(detail))
                check(ok and ('S04: recorded (PL-D78): ' + detail + '\n') in out,
                      '%s, jit%s: a run whose S04 alone failed on "%s" did not pass with the line "S04: recorded (PL-D78)": %s\n%s'
                      % (machine, ' --fallback' if fallback else '', detail, why, out))
                cases += 1
        for detail, what in REFUSED_DETAILS[:2]:
            ok, out, why = run_gate(contract, run, machine, 'jit', False, 1, summary_with(detail))
            check(not ok and 'the contract runner ended with exit code 1' in why and 'S04: recorded' not in out,
                  '%s, jit: %s passed the target: %s' % (machine, what, out))
            cases += 1
        for change, what in REFUSED_SUMMARIES[:5]:
            ok, out, why = run_gate(contract, run, machine, 'jit', False, 1, changed(summary_with(DID_NOT_MOVE), change))
            check(not ok and 'S04: recorded' not in out, '%s, jit: %s passed the target: %s' % (machine, what, out))
            cases += 1
        ok, out, why = run_gate(contract, run, machine, 'interp', False, 1, summary_with(DID_NOT_MOVE))
        check(not ok and 'the contract runner ended with exit code 1' in why, '%s: the interpreter\'s S04 is recorded: %s' % (machine, out))
        ok, out, why = run_gate(contract, run, machine, 'jit', False, 1, None)
        check(not ok and 'the contract runner ended with exit code 1' in why, '%s, jit: a run without a summary passed: %s' % (machine, out))
        ok, out, why = run_gate(contract, run, machine, 'jit', False, 2, summary_with(DID_NOT_MOVE))
        check(not ok and 'the contract runner ended with exit code 2' in why, '%s, jit: an exit code 2 passed: %s' % (machine, out))
        cases += 3
    for machine in X64_HOSTS:
        for fallback in (False, True):
            ok, out, why = run_gate(contract, run, machine, 'jit', fallback, 1, summary_with(DID_NOT_MOVE))
            check(not ok and 'the contract runner ended with exit code 1' in why and 'S04: recorded' not in out,
                  '%s: S04 is recorded on an x86-64 host: %s' % (machine, out))
            cases += 1
    for machine in ARM_HOSTS + X64_HOSTS:
        ok, out, why = run_gate(contract, run, machine, 'jit', False, 0, passed_summary())
        check(ok and 'S04: recorded' not in out and 'the gate of M1: %d passed, 0 failed' % len(GATE) in out,
              '%s: a run of the runner that passed is not a pass as before: %s %s' % (machine, why, out))
        cases += 1
    shutil.rmtree(run, ignore_errors=True)
    return 'run_runner: %d runs of a made-up runner on arm64, aarch64 and x86-64 hosts, each passed or failed as PL-D78 says' % cases


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--work', required=True)
    args = parser.parse_args()
    work = os.path.abspath(args.work)
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)

    contract = load_contract()
    check(contract.platform is platform, 'contract.py does not ask the platform module of this test')
    failed = 0
    for test in (lambda: test_rule(contract), lambda: test_run_runner(contract, work)):
        try:
            print('ok   ' + test())
        except Failed as failure:
            print('FAIL %s' % failure)
            failed += 1
    if failed:
        print('the files of the test are in ' + work)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    print('PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
