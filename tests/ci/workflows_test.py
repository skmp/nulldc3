#!/usr/bin/env python3
"""workflows_test.py: the test ci.workflows (CMakeLists.txt of this directory).

    workflows_test.py --tree <the top of the tree> --work <scratch directory> [--release-test]

Runs nulldc3/ci/check_workflows.py --self-test on .github/workflows/ci.yml (the rules of the workflow, and each
mutation of its list caught by one), then with --release-test release_test.py of this directory (release.sh against a
stand-in for gh, in a scratch repository below --work); without it says that part is not run on this host. Exit
status 0 when both passed. Python 3.9, the standard library only.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tree', required=True)
    parser.add_argument('--work', required=True)
    parser.add_argument('--release-test', action='store_true')
    options = parser.parse_args()
    tree = os.path.abspath(options.tree)

    rules = subprocess.run([sys.executable, '-B', os.path.join(tree, 'nulldc3', 'ci', 'check_workflows.py'), '--self-test',
                            '--workflow', os.path.join(tree, '.github', 'workflows', 'ci.yml')])
    status = 0 if rules.returncode == 0 else 1
    if options.release_test:
        release = subprocess.run([sys.executable, '-B', os.path.join(HERE, 'release_test.py'), '--tree', tree,
                                  '--work', os.path.join(os.path.abspath(options.work), 'release')])
        if release.returncode != 0:
            status = 1
    else:
        print('release.sh against the stand-in for gh: not run on this host (release.sh is a bash script, and no '
              'test here runs a POSIX shell); it runs on Linux and macOS')
    print('ci.workflows: the rules %s, the stand-in test %s' % (
        'passed' if rules.returncode == 0 else 'FAILED',
        ('passed' if release.returncode == 0 else 'FAILED') if options.release_test else 'not run'))
    return status


if __name__ == '__main__':
    sys.exit(main())
