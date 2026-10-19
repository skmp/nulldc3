#!/usr/bin/env python3
"""rebrand.names: the tree says "reicast" only where allowed.txt allows it.

    rebrand_names.py --tree <top of the tree> --allowed <allowed.txt> [--git <git>] [--require-data]

Every line of a tracked text file that has "reicast" in it, in any case (git -C <tree> grep -I -i -n reicast), has
to be allowed by an entry of allowed.txt, and every row of allowed.txt has to find the lines it allows: an
occurrence that no entry allows fails the test, and so does a row whose line is gone (the output names both). No
tracked file may have "reicast" in its name either, unless a whole-path entry allows it. Submodules are other
repositories and are not searched, whatever the git config says (submodule.recurse). The format of allowed.txt is
described at its top and in tests/rebrand/README.md.

A tree that is not a git checkout (the top of one) has nothing to compare: the test is skipped (exit 77), or with
--require-data it fails.

Python 3.9 and the standard library. git is called with an argument list (no shell), its output is read as bytes
and decoded as UTF-8, paths are compared with '/', and the report is written as UTF-8 bytes.
"""
import argparse
import os
import subprocess
import sys

EXIT_SKIPPED = 77
WORD = 'reicast'


def out(text):
    sys.stdout.buffer.write((text + '\n').encode('utf-8', 'replace'))


def git(gitexe, tree, args):
    env = dict(os.environ)
    for name in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE', 'GIT_PREFIX', 'GIT_NAMESPACE', 'GIT_CEILING_DIRECTORIES'):
        env.pop(name, None)
    env['GIT_OPTIONAL_LOCKS'] = '0'
    # The settings that change what git grep prints are pinned, submodule.recurse among them: with it set to true in
    # a user's or the system's git config, git grep also searches the initialised submodules (other repositories:
    # tests/vendor/*, devtools/), whose lines are not the tree's.
    cmd = [gitexe, '-C', tree, '-c', 'core.quotePath=false', '-c', 'color.ui=never', '-c', 'color.grep=never',
           '-c', 'grep.column=false', '-c', 'grep.patternType=fixed', '-c', 'submodule.recurse=false'] + args
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    return p.returncode, p.stdout, p.stderr.decode('utf-8', 'replace').strip()


class Entry:
    def __init__(self, lineno, path, pattern, klass, reason):
        self.lineno, self.path, self.pattern, self.klass, self.reason = lineno, path, pattern, klass, reason
        self.whole = pattern == '*'

    def covers_path(self, path):
        if self.path.endswith('/**'):
            return path.startswith(self.path[:-2])
        return path == self.path

    def text(self):
        return '%s | %s' % (self.path, self.pattern)


def read_allowed(path):
    """The entries of allowed.txt and the lines that are no entry, as (line number, text, why)."""
    entries, bad = [], []
    data = open(path, 'rb').read().decode('utf-8', 'replace')
    for n, line in enumerate(data.split('\n'), 1):
        line = line.rstrip('\r')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        parts = line.split(' | ')
        if len(parts) < 4:
            bad.append((n, line, 'not four fields "<path> | <pattern> | <class> | <reason>"'))
            continue
        entry = Entry(n, parts[0].strip(), ' | '.join(parts[1:-2]).strip(), parts[-2].strip(), parts[-1].strip())
        directory = entry.path.endswith('/**')
        stem = entry.path[:-3] if directory else entry.path
        if not stem or '\\' in stem or stem.startswith('/') or stem.endswith('/') or '*' in stem:
            bad.append((n, line, 'the path is not a tree path with "/" (a directory: "<dir>/**", no other wildcard)'))
        elif not entry.klass or ' ' in entry.klass or not entry.reason:
            bad.append((n, line, 'the class (one word) or the reason is missing'))
        elif directory and not entry.whole:
            bad.append((n, line, 'a directory ("/**") can only be allowed whole (pattern "*")'))
        elif not entry.whole and WORD not in entry.pattern.lower():
            bad.append((n, line, 'the pattern does not say "%s": a row names the occurrence it allows' % WORD))
        else:
            entries.append(entry)
    for e in entries:
        if not e.whole and any(w.whole and w.covers_path(e.path) for w in entries):
            bad.append((e.lineno, e.text(), 'the path is allowed whole by another entry: the row has nothing to do'))
    entries = [e for e in entries if e.whole or not any(w.whole and w.covers_path(e.path) for w in entries)]
    return entries, bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tree', required=True)
    ap.add_argument('--allowed', required=True)
    ap.add_argument('--git', default='git')
    ap.add_argument('--require-data', action='store_true')
    a = ap.parse_args()

    rc, prefix, err = git(a.git, a.tree, ['rev-parse', '--show-prefix'])
    if rc != 0 or prefix.strip():
        why = ('%s is not a git checkout (git rev-parse: %s)' % (a.tree, err or 'exit %d' % rc) if rc != 0
               else '%s is inside a git checkout, not the top of one' % a.tree)
        if a.require_data:
            out('MISSING: %s: nothing to compare allowed.txt with' % why)
            return 1
        out('SKIPPED: %s: nothing to compare allowed.txt with' % why)
        return EXIT_SKIPPED

    entries, bad = read_allowed(a.allowed)

    rc, raw, err = git(a.git, a.tree, ['grep', '-z', '-I', '-i', '-n', '-e', WORD])
    if rc not in (0, 1):
        out('FAILED: git grep ended with %d: %s' % (rc, err))
        return 1
    lines = []                      # (path, line number, text)
    for rec in raw.split(b'\n'):
        if not rec:
            continue
        fields = rec.split(b'\0', 2)
        if len(fields) != 3:
            out('FAILED: git grep printed a line that is not <path>\\0<line>\\0<text>: %r' % rec[:200])
            return 1
        lines.append((fields[0].decode('utf-8', 'replace'), int(fields[1]), fields[2].decode('utf-8', 'replace').rstrip('\r')))

    rc, raw, err = git(a.git, a.tree, ['ls-files', '-z'])
    if rc != 0:
        out('FAILED: git ls-files ended with %d: %s' % (rc, err))
        return 1
    named = [p for p in raw.decode('utf-8', 'replace').split('\0') if p and WORD in p.lower()]

    whole = [e for e in entries if e.whole]
    rows = [e for e in entries if not e.whole]
    by_row = {}                     # (path, pattern) -> the rows with that key
    for e in rows:
        by_row.setdefault((e.path, e.pattern), []).append(e)

    failures = ['malformed: allowed.txt:%d: %s: %s' % b for b in bad]
    by_whole = 0
    by_rows = 0
    matched = {k: [] for k in by_row}
    for path, n, text in lines:
        if any(e.covers_path(path) for e in whole):
            by_whole += 1
            continue
        keys = [k for k in by_row if k[0] == path and k[1] in text]
        if not keys:
            failures.append('not allowed: %s:%d: %s' % (path, n, text.strip()))
            continue
        by_rows += 1
        for k in keys:
            matched[k].append(n)
    for k, group in sorted(by_row.items(), key=lambda kv: kv[1][0].lineno):
        found = matched[k]
        first = group[0]
        if not found:
            failures.append('gone: allowed.txt:%d: %s: no line of %s has it any more' % (first.lineno, first.text(), first.path))
        elif len(found) != len(group):
            failures.append('count: allowed.txt:%s: %s: %d lines of %s have it (%s), the list has %d row%s for it'
                            % (','.join(str(e.lineno) for e in group), first.text(), len(found), first.path,
                               ', '.join(str(x) for x in found), len(group), '' if len(group) == 1 else 's'))
    for p in named:
        if not any(e.covers_path(p) for e in whole):
            failures.append('not allowed (file name): %s' % p)

    files = len(set(p for p, _, _ in lines))
    out('rebrand.names: %d lines say %s in %d tracked files: %d allowed as whole paths (%d entries), %d by the %d rows '
        'of allowed.txt; %d tracked file names say it' % (len(lines), WORD, files, by_whole, len(whole), by_rows,
                                                          len(rows), len(named)))
    if failures:
        for f in failures:
            out(f)
        out('FAILED: %d finding%s (tests/rebrand/README.md says how to read and change allowed.txt)'
            % (len(failures), '' if len(failures) == 1 else 's'))
        return 1
    out('OK: every occurrence is allowed, and every row finds its line')
    return 0


if __name__ == '__main__':
    sys.exit(main())
