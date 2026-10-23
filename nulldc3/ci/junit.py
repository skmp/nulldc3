#!/usr/bin/env python3
"""junit.py: what a ctest run of the CI scripts (linux.sh test, windows.ps1, macos.sh) ran, from the JUnit file of
ctest --output-junit: the counts, every skipped test with its reason, every failed test.

    junit.py <junit.xml> [--title <text>] [--summary <file>] [--failed-only]

Prints "<n> tests, <p> passed, <s> skipped, <f> failed", then "skipped: <test>: <reason>" and "failed: <test>" lines.
--summary appends the same as Markdown (a title, the counts, a table of the skipped tests and their reasons, the
failed tests) to <file>, the job summary of GitHub Actions ($GITHUB_STEP_SUMMARY). --failed-only prints the names of
the failed tests alone, one per line. The reason of a skipped test is the line of its output that says why: the
SKIPPED:/SKIP: line of the tree's skip helpers, a KNOWN LIMIT or OPEN DEFECT line, else its last line. Python 3.9,
the standard library only.
"""
import argparse
import sys
import xml.etree.ElementTree as ET

MARKERS = ('SKIPPED:', 'SKIP:', 'MISSING:', 'KNOWN LIMIT', 'OPEN DEFECT', 'Skipped')


def read(path):
    root = ET.parse(path).getroot()
    tests, skipped, failed = [], [], []
    for case in root.iter('testcase'):
        name = case.get('name')
        tests.append(name)
        status = case.get('status', '')
        out = (case.findtext('system-out') or '').replace('\r\n', '\n')
        if case.find('skipped') is not None or status in ('notrun', 'disabled'):
            reason = ''
            for line in out.split('\n'):
                line = line.strip()
                if any(marker in line for marker in MARKERS):
                    reason = line
                    break
            if not reason:
                message = case.find('skipped').get('message', '') if case.find('skipped') is not None else ''
                lines = [line.strip() for line in out.split('\n') if line.strip()]
                reason = lines[-1] if lines else (message or '(no output)')
            skipped.append((name, reason))
        elif case.find('failure') is not None or case.find('error') is not None or status == 'fail':
            failed.append(name)
    return tests, sorted(skipped), sorted(failed)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('junit')
    parser.add_argument('--title', default='Tests')
    parser.add_argument('--summary')
    parser.add_argument('--failed-only', action='store_true')
    options = parser.parse_args()
    tests, skipped, failed = read(options.junit)
    if options.failed_only:
        for name in failed:
            print(name)
        return 0
    counts = '%d tests, %d passed, %d skipped, %d failed' % (len(tests), len(tests) - len(skipped) - len(failed),
                                                             len(skipped), len(failed))
    print(counts)
    for name, reason in skipped:
        print('skipped: %s: %s' % (name, reason))
    for name in failed:
        print('failed: %s' % name)
    if options.summary:
        lines = ['### %s' % options.title, '', counts, '']
        if skipped:
            lines += ['| skipped test | reason |', '|---|---|']
            lines += ['| `%s` | %s |' % (name, reason.replace('|', '/')[:300]) for name, reason in skipped]
            lines.append('')
        if failed:
            lines += ['Failed: ' + ', '.join('`%s`' % name for name in failed), '']
        with open(options.summary, 'a', encoding='utf-8') as f:
            f.write('\n'.join(lines) + '\n')
    return 0


if __name__ == '__main__':
    sys.exit(main())
