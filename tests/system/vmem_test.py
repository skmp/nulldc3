#!/usr/bin/env python3
"""System tests of the memory layout: every emulator gets the file of its memory for itself, also one that starts
at the same moment as another emulator; and one that cannot have the layout runs without it.

    vmem_test.py --emulator <binary> --case <case> --work <directory> --elf-dir <directory with 2ndmix.elf>
                 [--shim <vmem_shim library>] [--ta-hle] [--watchdog <seconds>] [--keep]

One case per invocation, exit code 0 when it passes, a line "SKIPPED: <why>" and 0 when the host cannot make the
case (ctest counts it skipped); README.md has the cases. The runs are made as system_test.py makes them (its
Harness), with one difference: the data directory of a run cannot be made (its XDG_DATA_HOME is below a directory
that does not exist), as in a fresh HOME. An emulator that lost the file of its memory there runs without the
virtual memory layout. On the SH4 recompiler 2ndmix then ended at its first store into its start-up page as long as
the recompiler measured such a fault from the base of the layout (vmem.none).
"""

import argparse
import os
import resource
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import system_test as st    # noqa: E402

# What the emulator prints when it has the layout, and when it runs without it
LAYOUT = 'Info: nvmem is enabled'
NO_LAYOUT = 'nvmem is DISABLED'

# 2ndmix is through the start-up code of KallistiOS in its first frame; 60 vblanks also see its display start
VBLANKS = 60

# system.vmem.concurrent: BATCHES times, TOGETHER runs started at the same moment. Measured on f640f21a, where
# every emulator used one shm name for the file of its memory (2ndmix on the JIT, 60 vblanks, a gate as below,
# 25 batches each): 2 together, 14 of 25 batches had a run without the layout, which ended at the store;
# 4 together, 16 of 25. The oldest commit with headless runs (de892a8f), 4 together for 600 vblanks: 8 of 25.
# With one batch in five, 40 batches all pass with a probability of 0.8^40 = 1e-4.
BATCHES = 40
TOGETHER = 2

# How long the test waits for every run of a batch to be at the gate before it opens it. A run that comes later
# goes through at once: it only makes the batch less of a race.
GATE_SECONDS = 0.1


class Skip(Exception):
    pass


def keys(vblanks=VBLANKS):
    return st.headless('config:pvr.backend=none', 'config:Exit.AfterVblanks=%d' % vblanks, 'config:Exit.StateHash=yes',
                       engine='jit')


def command(h, keys, program):
    args = [h.options.emulator, '-config']
    for i, key in enumerate(keys):
        if i:
            args.append(',')
        args.append(key)
    return args + [program]


def start(h, tag, args, env=None, data_dir=False, stdin=subprocess.DEVNULL):
    """Harness.start() for a command that is not the emulator itself (it ends in the emulator's command line), and
    a data directory that cannot be made unless data_dir. Returns what Harness.finish() takes."""
    directory = os.path.join(h.work, tag)
    for sub in ('home', 'config'):
        os.makedirs(os.path.join(directory, sub))
    environment = {
        'HOME': os.path.join(directory, 'home'),
        'XDG_CONFIG_HOME': os.path.join(directory, 'config'),
        # the emulator makes <XDG_DATA_HOME>/reicast with one mkdir: below a directory that is not there it cannot
        'XDG_DATA_HOME': os.path.join(directory, 'data') if data_dir else os.path.join(directory, 'none', 'data'),
    }
    if data_dir:
        os.makedirs(os.path.join(directory, 'data'))
    environment.update(env or {})

    def limit_core():
        resource.setrlimit(resource.RLIMIT_CORE, (1, resource.getrlimit(resource.RLIMIT_CORE)[1]))

    log = open(os.path.join(directory, 'log.txt'), 'wb')
    process = subprocess.Popen(args, cwd=directory, env=environment, stdin=stdin, stdout=log,
                               stderr=subprocess.STDOUT, preexec_fn=limit_core, start_new_session=True)
    return (tag, args, process, log, directory, time.time())


def check_booted(run, reference=None):
    """The run had its layout and ended at its vblanks, cleanly, with the state hash of the reference."""
    st.check(NO_LAYOUT not in run.log, 'the emulator ran without the virtual memory layout ("%s")' % NO_LAYOUT, run)
    st.check_clean(run, 'vblanks')
    st.check(LAYOUT in run.log, 'the emulator does not say that it has the virtual memory layout ("%s")' % LAYOUT, run)
    st.check('Using MCPU Recompiler' in run.log, 'not the recompiler', run)
    got = st.check_hash(run)
    if reference is not None:
        st.check(got == reference.fields['hash'], 'state hash %s, the run %s ended in %s\n  %s\n  %s' % (
            got, reference.tag, reference.fields['hash'], reference.exit_line, run.exit_line), run)
    return got


def case_concurrent(h):
    """BATCHES times TOGETHER runs of 2ndmix on the JIT, started at the same moment, none of them with a data
    directory: every run has the layout and ends at its vblanks, all in one state hash."""
    program = h.elf('2ndmix.elf')
    runs = []
    for batch in range(BATCHES):
        # The gate: each run is a shell that reads its stdin, a pipe whose write end only the test holds, and then
        # becomes the emulator; when the test closes it, the reads end together. (Not a pipe on a descriptor of
        # its own: its number is whatever the test got, which under ctest is not the one of a run by hand.)
        gate_read, gate_write = os.pipe()
        started = []
        for i in range(TOGETHER):
            args = ['/bin/sh', '-c', 'read _; exec "$@" < /dev/null', 'gate'] + command(h, keys(), program)
            started.append(start(h, 'batch%02d_%d' % (batch, i), args, stdin=gate_read))
        os.close(gate_read)
        time.sleep(GATE_SECONDS)
        os.close(gate_write)
        runs.extend(h.finish(s) for s in started)

    without = [run for run in runs if NO_LAYOUT in run.log]
    ended = [run for run in runs if run.hung or run.returncode != 0 or run.exit_line is None]
    if without or ended:
        bad = (without or ended)[0]
        batches = len(set(run.tag.split('_')[0] for run in without + ended))
        st.check(False, 'of %d runs started %d at a time, %d ran without the virtual memory layout and %d did not '
                 'end at their vblanks with exit code 0 (%d of them without the layout); %d of %d batches had such '
                 'a run' % (len(runs), TOGETHER, len(without), len(ended), len([r for r in ended if r in without]),
                            batches, BATCHES), bad)
    reference = runs[0]
    for run in runs:
        check_booted(run, reference)
    print('  %d runs of 2ndmix, %d at a time: each with its layout, state hash %s' % (
        len(runs), TOGETHER, reference.fields['hash']))


# Runs in a user and mount namespace of its own, with a tmpfs of its own on /dev/shm:
#   ns.sh <ro 0|1> <hold 0|1> <own 0|1> <listing> <command...>
# ro: the tmpfs is read-only. hold: /dev/shm/dcnzorz_mem is there, the name every emulator before used for the file
# of its memory, as while another one of them starts. own: the command runs in a process that has made
# /dev/shm/dcnzorz_mem.<its pid>.0, the first name of its own that an emulator without memfd_create takes. Behind
# the command the names in /dev/shm are written to <listing>; the exit status is the command's.
NS_SCRIPT = '''#!/bin/sh
ro=$1 hold=$2 own=$3 listing=$4
shift 4
if [ "$ro" = 1 ]; then options=ro; else options=rw; fi
@MOUNT@ -t tmpfs -o $options nulldc-vmem-test /dev/shm || { echo "ns.sh: cannot mount a tmpfs on /dev/shm"; exit 125; }
if [ "$hold" = 1 ]; then : > /dev/shm/dcnzorz_mem; fi
if [ "$own" = 1 ]; then
  /bin/sh -c ': > "/dev/shm/dcnzorz_mem.$$.0" && exec "$@"' own "$@"
else
  "$@"
fi
status=$?
@LS@ -A /dev/shm > "$listing"
exit $status
'''


def namespace(h):
    """The command line that runs ns.sh in a namespace of its own, or Skip when the host does not give one."""
    unshare, mount, ls = shutil.which('unshare'), shutil.which('mount'), shutil.which('ls')
    if not (unshare and mount and ls):
        raise Skip('no unshare, mount and ls on the PATH')
    script = os.path.join(h.work, 'ns.sh')
    if not os.path.exists(script):
        with open(script, 'w') as f:
            f.write(NS_SCRIPT.replace('@MOUNT@', mount).replace('@LS@', ls))
    probe = subprocess.run([unshare, '-rm', '/bin/sh', '-c', '%s -t tmpfs nulldc-vmem-test /dev/shm' % mount],
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if probe.returncode != 0:
        raise Skip('the host gives no user and mount namespace with a tmpfs of its own on /dev/shm (unshare -rm: %s)'
                   % probe.stdout.decode('utf-8', 'replace').strip())
    return [unshare, '-rm', '/bin/sh', script]


def run_in_namespace(h, ns, tag, ro, hold, own, env=None, data_dir=False, vblanks=VBLANKS):
    listing = os.path.join(h.work, tag + '.shm')
    args = ns + ['1' if ro else '0', '1' if hold else '0', '1' if own else '0', listing] + \
        command(h, keys(vblanks), h.elf('2ndmix.elf'))
    run = h.finish(start(h, tag, args, env=env, data_dir=data_dir))
    st.check(run.returncode != 125, 'the namespace of the run has no tmpfs on /dev/shm', run)
    names = None
    if os.path.exists(listing):
        with open(listing) as f:
            names = sorted(f.read().split())
    return run, names


def case_taken(h):
    """The name another emulator holds while it starts, and the ways to the file of the memory that are left
    without memfd_create, each in a /dev/shm of its own (a user and a mount namespace): every run has the layout
    and ends in the state hash of a run beside nothing."""
    ns = namespace(h)
    st.check(h.options.shim and os.path.isfile(h.options.shim), 'no vmem shim (--shim)')
    shim = {'LD_PRELOAD': h.options.shim}
    check_shim = lambda run: st.check('vmem_shim: memfd_create refused' in run.log,
                                      'the run did not ask for a memfd (no line of the shim)', run)

    reference = h.run('alone', keys(), h.elf('2ndmix.elf'))
    check_booted(reference)

    # the name of the emulators before is taken: what a run of 4 at a time met one time in four
    run, names = run_in_namespace(h, ns, 'name_taken', ro=False, hold=True, own=False)
    check_booted(run, reference)
    st.check(names == ['dcnzorz_mem'], 'the names in /dev/shm behind the run are %s, expected the one that was '
             'there before it, left alone' % names, run)

    # no memfd: an shm name of the process's own, the first of which is taken too
    run, names = run_in_namespace(h, ns, 'no_memfd', ro=False, hold=True, own=True, env=shim)
    check_shim(run)
    check_booted(run, reference)
    st.check(names is not None and len(names) == 2 and 'dcnzorz_mem' in names and
             any(n.startswith('dcnzorz_mem.') and n.endswith('.0') for n in names),
             'the names in /dev/shm behind the run are %s, expected the two that were there before it, left alone, '
             'and none of its own' % names, run)

    # no memfd and no shm: a file of the data directory, which is removed once it is open
    run, names = run_in_namespace(h, ns, 'no_shm', ro=True, hold=False, own=False, env=shim, data_dir=True)
    check_shim(run)
    check_booted(run, reference)
    data = os.path.join(run.directory, 'data', 'reicast')
    left = [n for n in os.listdir(data) if n.startswith('dcnzorz_mem')] if os.path.isdir(data) else []
    st.check(not left, 'the data directory keeps %s' % left, run)
    print('  4 runs of 2ndmix on the JIT, each with its layout, state hash %s' % reference.fields['hash'])


# vmem.none runs long enough for the guest to store into pages of compiled code after its start-up as well
NONE_VBLANKS = 600


def case_none(h):
    """No way to the file of the memory is left: no memfd_create, a read-only /dev/shm of its own and no data
    directory. The emulator runs without the virtual memory layout, on the JIT: the guest's stores into the pages
    of compiled code (the start-up page of KallistiOS first) are resolved, and the run ends at its vblanks in the
    state hash of the run with the layout."""
    ns = namespace(h)
    st.check(h.options.shim and os.path.isfile(h.options.shim), 'no vmem shim (--shim)')

    reference = h.run('alone', keys(NONE_VBLANKS), h.elf('2ndmix.elf'))
    check_booted(reference)

    run, names = run_in_namespace(h, ns, 'no_layout', ro=True, hold=False, own=False,
                                  env={'LD_PRELOAD': h.options.shim}, data_dir=False, vblanks=NONE_VBLANKS)
    # what the case is about, whichever ways to the file the emulator tried (one from before memfd_create tried none)
    st.check(NO_LAYOUT in run.log and LAYOUT not in run.log, 'the run was to have no virtual memory layout and does '
             'not say "%s"' % NO_LAYOUT, run)
    st.check('Using MCPU Recompiler' in run.log, 'not the recompiler', run)
    st.check_clean(run, 'vblanks')
    got = st.check_hash(run)
    st.check(got == reference.fields['hash'], 'state hash %s without the layout, the run %s with it ended in %s\n  %s\n  %s'
             % (got, reference.tag, reference.fields['hash'], reference.exit_line, run.exit_line), run)
    st.check(names == [], 'the names in /dev/shm behind the run are %s, expected none' % names, run)
    print('  2ndmix on the JIT without the layout and with it: state hash %s' % got)


CASES = {
    'vmem.concurrent': case_concurrent,
    'vmem.none': case_none,
    'vmem.taken': case_taken,
}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--emulator', required=True)
    parser.add_argument('--case', required=True, choices=sorted(CASES))
    parser.add_argument('--work', required=True, help='scratch directory of this case, emptied first')
    parser.add_argument('--elf-dir', help='directory with 2ndmix.elf')
    parser.add_argument('--shim', help='the vmem_shim library, for vmem.taken')
    parser.add_argument('--ta-hle', action='store_true', help='the emulator is a -DHAS_TA_LLE=OFF build')
    parser.add_argument('--watchdog', type=int, default=st.WATCHDOG,
                        help='seconds of wall clock one run of the emulator gets (%d)' % st.WATCHDOG)
    parser.add_argument('--keep', action='store_true', help='keep the directories of the runs of a passed case')
    options = parser.parse_args()

    if not os.path.isfile(options.emulator):
        print('FAIL %s: no emulator at %s' % (options.case, options.emulator))
        return 1

    harness = st.Harness(options)
    try:
        CASES[options.case](harness)
    except Skip as skip:
        print('SKIPPED: %s: %s' % (options.case, skip))
        shutil.rmtree(harness.work, ignore_errors=True)
        return 0
    except st.Failure as failure:
        print('FAIL %s: %s' % (options.case, failure))
        return 1

    print('PASS %s (%d run%s of the emulator, %.1f s)' % (
        options.case, len(harness.runs), '' if len(harness.runs) == 1 else 's', sum(r.seconds for r in harness.runs)))
    if not options.keep:
        shutil.rmtree(harness.work, ignore_errors=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
