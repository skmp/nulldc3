#!/usr/bin/env python3
"""scripts_test.py: the test ci.scripts (CMakeLists.txt of this directory), the parts of the CI scripts that can be
held to their rules without a build.

    scripts_test.py --tree <the top of the tree> --work <scratch directory>

1. nulldc3/ci/zipcheck.py (every host) on zips made here: a zip as tar.exe -a -c makes it (the folder's own entry
   and the seven files below it, "/" alone) passes; the same names with "\\" (as Compress-Archive of Windows
   PowerShell 5.1 writes them, without the folder's entry), "/" without the folder's entry, a file missing, a file
   more, a file below a folder and an entry outside the folder are each refused.
Exit status 0 when every case holds. Python 3.9, the standard library only.
"""
import argparse
import os
import shutil
import subprocess
import sys
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

    print('ci.scripts: %d cases failed' % len(failures))
    if not failures:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
