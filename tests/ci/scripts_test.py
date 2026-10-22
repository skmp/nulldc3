#!/usr/bin/env python3
"""scripts_test.py: the test ci.scripts (CMakeLists.txt of this directory), the parts of the CI scripts that can be
held to their rules without a build.

    scripts_test.py --tree <the top of the tree> --work <scratch directory>

1. nulldc3/ci/zipcheck.py (every host) on zips made here: a zip as tar.exe -a -c makes it (the folder's own entry
   and the seven files below it, "/" alone) passes; the same names with "\\" (as Compress-Archive of Windows
   PowerShell 5.1 writes them, without the folder's entry), "/" without the folder's entry, a file missing, a file
   more, a file below a folder and an entry outside the folder are each refused.
2. nulldc3/ci/smoke.py (Linux) against a stand-in for the emulator, a Python program that answers --version, writes
   the files of a run and the exit line, and with the debugger enabled serves debugger.describe on 127.0.0.1 (or a
   reply that is no JSON, or one of another shape, or nothing, as the case wants), and has a child in its session
   that ignores SIGTERM. Each run of it records its id, its child's, its process group and its parent's name.
   - a good stand-in: smoke.py passes; every run went through timeout(1) (its parent); nothing of any run is left;
   - a reply that is no JSON, and one that is a list: smoke.py fails the debugger's check (no traceback), and
     nothing of the run is left;
   - SIGTERM to smoke.py while it waits for the debugger's port, SIGHUP while the guest's run hangs: smoke.py ends
     with 128 + the signal, and nothing of the run in progress is left.
   On other hosts that part says it is not run (macOS and Windows have their launchers, macrun.py and nulldc3-winrun,
   which end what they started).
Whatever a failed case leaves running (the stand-in's processes alone, by the ids they recorded) is killed at the end.
Exit status 0 when every case holds. Python 3.9, the standard library only.
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import zipfile

FILES = ['LICENSE', 'LICENSE-GPL', 'LICENSE-LGPL', 'LICENSE-devtools', 'LICENSE-shell', 'README.txt', 'nulldc3.exe']

failures = []


def check(condition, text):
    print('%s %s' % ('PASS' if condition else 'FAIL', text), flush=True)
    if not condition:
        failures.append(text)
    return condition


# ---- 1. zipcheck.py ------------------------------------------------------------------------------------------------

def make_zip(path, names):
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in names:
            info = zipfile.ZipInfo(name.replace('\\', '/'))
            info.filename = name    # as written, "\" included (ZipInfo turns it into "/" on Windows)
            archive.writestr(info, b'' if name.endswith('/') else name.encode())


def zipcheck_cases(tree, work):
    script = os.path.join(tree, 'nulldc3', 'ci', 'zipcheck.py')
    folder = 'nulldc3-3.0.0-pre-windows-x86_64'
    good = [folder + '/'] + ['%s/%s' % (folder, f) for f in FILES]
    cases = [
        ('as tar.exe -a -c makes it', good, 0, None),
        ('the names with "\\" and no folder entry, as Compress-Archive writes them',
         ['%s\\%s' % (folder, f) for f in FILES], 1, 'backslash'),
        ('"/" without the folder\'s own entry', good[1:], 1, 'no entry %s/' % folder),
        ('a file missing', good[:-1], 1, 'expected'),
        ('a file more', good + [folder + '/extra.dll'], 1, 'expected'),
        ('a file below a folder', good + [folder + '/sub/', folder + '/sub/x.txt'], 1, 'expected'),
        ('an entry outside the folder', good + ['other/x.txt'], 1, 'outside'),
    ]
    for i, (what, names, wanted, says) in enumerate(cases):
        path = os.path.join(work, 'zip-%d.zip' % i)
        make_zip(path, names)
        p = subprocess.run([sys.executable, '-B', script, path, '--folder', folder, '--files'] + FILES,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = p.stdout.decode('utf-8', 'replace')
        last = output.strip().splitlines()[-1] if output.strip() else ''
        check(p.returncode == wanted and (says is None or says in last),
              'zipcheck.py, %s: exit status %d (expected %d): %s' % (what, p.returncode, wanted, last))


# ---- 2. smoke.py against a stand-in --------------------------------------------------------------------------------

STAND_IN = r'''#!/usr/bin/env python3
# the stand-in for the emulator of scripts_test.py
import json, os, signal, socket, struct, sys, time
args = sys.argv[1:]
mode = os.environ['STAND_IN_MODE']
record = os.environ['STAND_IN_RECORD']
role = 'version' if '--version' in args else ('debugger' if 'Debugger:Enabled=yes' in args else 'guest')

def start_time(pid):
    try:
        return open('/proc/%d/stat' % pid).read().rsplit(')', 1)[1].split()[19]
    except (OSError, IndexError):
        return None

def forever():
    while True:
        time.sleep(1)

child = None
if role != 'version':
    # a child in the session that ignores SIGTERM, as a program the AppImage runtime starts could
    child = os.fork()
    if child == 0:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        forever()
try:
    parent = open('/proc/%d/comm' % os.getppid()).read().strip()
except OSError:
    parent = None
with open(os.path.join(record, role + '.json.part'), 'w') as f:
    json.dump({'pid': os.getpid(), 'start': start_time(os.getpid()), 'child': child,
               'child_start': start_time(child) if child else None, 'pgid': os.getpgid(0), 'parent': parent}, f)
os.replace(os.path.join(record, role + '.json.part'), os.path.join(record, role + '.json'))

if role == 'version':
    print('nullDC ' + os.environ['STAND_IN_VERSION'])
    sys.exit(0)
if role == 'guest':
    if mode == 'hang-guest':
        forever()
    portable = args[args.index('--portable') + 1]
    for name in ('emu.cfg', 'vmu_save_A1.bin', 'vmu_save_A2.bin'):
        open(os.path.join(portable, name), 'w').close()
    print('nulldc-exit: reason=vblanks vblanks=90 state=0123456789abcdef', flush=True)
    os.kill(child, signal.SIGKILL)
    os.waitpid(child, 0)
    sys.exit(0)
if mode == 'hang-debugger':
    forever()

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.bind(('127.0.0.1', 0))
server.listen(1)
print('Debugger: http://127.0.0.1:%d/' % server.getsockname()[1], flush=True)
conn, _ = server.accept()
data = b''
while b'\r\n\r\n' not in data:
    data += conn.recv(4096)
conn.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
             b'Sec-WebSocket-Accept: stand-in\r\n\r\n')
data = data.split(b'\r\n\r\n', 1)[1]

def need(n):
    global data
    while len(data) < n:
        data += conn.recv(4096)

need(2)
length, at = data[1] & 0x7F, 2
if length == 126:
    need(4)
    length, at = struct.unpack('>H', data[2:4])[0], 4
need(at + 4 + length)
mask = data[at:at + 4]
request = json.loads(bytes(b ^ mask[i & 3] for i, b in enumerate(data[at + 4:at + 4 + length])).decode())

def send(payload):
    head = bytes([0x81])
    head += bytes([len(payload)]) if len(payload) < 126 else bytes([126]) + struct.pack('>H', len(payload))
    conn.sendall(head + payload)

if mode == 'no-json':
    send(b'{this is no JSON')
elif mode == 'list':
    send(json.dumps({'jsonrpc': '2.0', 'id': request['id'], 'result': ['emulator', 'ui']}).encode())
    send(json.dumps({'jsonrpc': '2.0', 'method': 'event.tick', 'params': {}}).encode())
else:
    send(json.dumps({'jsonrpc': '2.0', 'id': request['id'],
                     'result': {'emulator': {'version': os.environ['STAND_IN_VERSION']},
                                'ui': os.environ['STAND_IN_UI']}}).encode())
    send(json.dumps({'jsonrpc': '2.0', 'method': 'event.tick', 'params': {}}).encode())
forever()
'''

VERSION = '3.0.0-pre+g0123456789'
UI = '3.0.0-pre+g7b88410915'


def start_time(pid):
    try:
        with open('/proc/%d/stat' % pid) as f:
            fields = f.read().rsplit(')', 1)[1].split()
    except (OSError, IndexError):
        return None, None
    return fields[0], fields[19]


def alive(pid, start):
    """Whether the process the stand-in recorded is still there (the same start time; a zombie is not)."""
    state, started = start_time(pid)
    return state is not None and state != 'Z' and started == start


def records(directory):
    found = {}
    if os.path.isdir(directory):
        for name in sorted(os.listdir(directory)):
            if name.endswith('.json'):
                with open(os.path.join(directory, name)) as f:
                    found[name[:-5]] = json.load(f)
    return found


def left(found, wait=10.0):
    """The processes of the records that are still there, after a wait of up to <wait> s for them to go."""
    deadline = time.monotonic() + wait
    while True:
        there = []
        for role, r in found.items():
            if alive(r['pid'], r['start']):
                there.append('%s %d' % (role, r['pid']))
            if r.get('child') and alive(r['child'], r['child_start']):
                there.append('%s\'s child %d' % (role, r['child']))
        if not there or time.monotonic() > deadline:
            return there
        time.sleep(0.2)


def kill_left(found):
    for r in found.values():
        for pid, start in ((r['pid'], r['start']), (r.get('child'), r.get('child_start'))):
            if pid and alive(pid, start):
                try:
                    os.kill(pid, signal.SIGKILL)
                except OSError:
                    pass


def smoke_cases(tree, work):
    stand_in = os.path.join(work, 'stand-in')
    with open(stand_in, 'w') as f:
        f.write(STAND_IN.replace('#!/usr/bin/env python3', '#!' + sys.executable, 1))
    os.chmod(stand_in, 0o755)
    guest = os.path.join(work, 'guest.elf')
    open(guest, 'wb').close()
    every = []

    def start(case, mode):
        record = os.path.join(work, case, 'record')
        os.makedirs(record)
        env = dict(os.environ, STAND_IN_MODE=mode, STAND_IN_RECORD=record, STAND_IN_VERSION=VERSION, STAND_IN_UI=UI)
        every.append(record)
        p = subprocess.Popen([sys.executable, '-B', os.path.join(tree, 'nulldc3', 'ci', 'smoke.py'), '--exe', stand_in,
                              '--version', VERSION, '--ui-version', UI, '--guest', guest,
                              '--work', os.path.join(work, case, 'run'), '--portable', os.path.join(work, case, 'portable')],
                             env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return p, record

    def finish(p):
        try:
            out, _ = p.communicate(timeout=240)
        except subprocess.TimeoutExpired:
            p.kill()
            out, _ = p.communicate()
        return p.returncode, out.decode('utf-8', 'replace')

    def show(output):
        return ' | '.join(line for line in output.splitlines() if line.strip())[-500:]

    # a good stand-in
    p, record = start('good', 'good')
    rc, output = finish(p)
    found = records(record)
    parents = sorted(set(r['parent'] for r in found.values()))
    check(rc == 0 and output.count('PASS ') == 4, 'smoke.py, a good stand-in: passes: rc=%d %s' % (rc, show(output) if rc else ''))
    check(sorted(found) == ['debugger', 'guest', 'version'] and parents == ['timeout'],
          'smoke.py, a good stand-in: its three runs went through timeout(1): the parents %s of %s' % (parents, sorted(found)))
    there = left(found)
    check(not there, 'smoke.py, a good stand-in: nothing of its runs is left (the debugger\'s child ignores SIGTERM): %s' % there)

    # replies that are no describe
    for mode, what in (('no-json', 'a reply that is no JSON'), ('list', 'a reply that is a list')):
        p, record = start(mode, mode)
        rc, output = finish(p)
        found = records(record)
        check(rc == 1 and 'FAIL debugger.describe' in output and 'Traceback' not in output,
              'smoke.py, %s: the debugger\'s check fails, smoke.py does not: rc=%d %s' % (what, rc, show(output)))
        there = left(found)
        check('debugger' in found and not there, 'smoke.py, %s: nothing of the run is left: %s' % (what, there))

    # signals to smoke.py
    for mode, role, number, name in (('hang-debugger', 'debugger', signal.SIGTERM, 'SIGTERM'),
                                     ('hang-guest', 'guest', signal.SIGHUP, 'SIGHUP')):
        p, record = start(mode, mode)
        deadline = time.monotonic() + 60
        while not os.path.exists(os.path.join(record, role + '.json')) and time.monotonic() < deadline and p.poll() is None:
            time.sleep(0.1)
        time.sleep(0.5)
        p.send_signal(number)
        rc, output = finish(p)
        found = records(record)
        check(rc == 128 + number and 'ended by signal %d' % number in output,
              'smoke.py, %s while the %s run hangs: ends with %d: rc=%s %s' % (name, role, 128 + number, rc, show(output)))
        there = left(found)
        check(role in found and not there, 'smoke.py, %s while the %s run hangs: nothing of the run is left: %s' % (name, role, there))

    for record in every:
        kill_left(records(record))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tree', required=True)
    parser.add_argument('--work', required=True)
    options = parser.parse_args()
    tree = os.path.abspath(options.tree)
    work = os.path.abspath(options.work)
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)

    zipcheck_cases(tree, work)
    if sys.platform.startswith('linux'):
        smoke_cases(tree, work)
    else:
        print('smoke.py against a stand-in: not run on this host (it is Linux\'s: there smoke.py itself ends what it '
              'started; macOS and Windows run each program through macrun.py and nulldc3-winrun)')

    print('ci.scripts: %d cases failed' % len(failures))
    if not failures:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
