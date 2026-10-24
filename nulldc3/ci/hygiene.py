#!/usr/bin/env python3
"""hygiene.py: the tracked files of a repository name no machine (the cut's 6.6, PL-D61): no home directory of Linux,
macOS or Windows, no path of WSL's network or of its drive mounts, no private IPv4 address, no .local host name,
unless a row of the allow list says why the line may.

    hygiene.py [--tree <dir>] [--allow <file>] [--extra <file>]

--tree: the top of a git checkout (default: this script's tree); every file `git ls-files` names there, as it is in
the working tree (submodules are other repositories and are not read). --allow: the allow list (default
nulldc3/ci/hygiene-allow.txt of the tree; for the UI repository nulldc3/ci/hygiene-allow-ui.txt is given), one row
per line, three fields separated by " | ":

    <path> | <text> | <reason>

<path> is a tracked file (or <dir>/** for every file below <dir>), <text> a piece of the line the row allows, copied
from it (case and spaces count; "*": every line of the file), <reason> why. A row that allows no line any more fails
too ("gone"), so that the list says what the tree has. Lines starting with # and empty lines are comments.
--extra: a file of more patterns, one per line, from outside the repositories (the owner-specific patterns of
PL-D61): each is a piece of text matched without regard to letter case, and no allow list row allows it.

Exit status 0 when every hit is allowed and every row is used, 1 otherwise, 77 when --tree is no git checkout (the
test ci.hygiene is skipped then). Python 3.9, the standard library only.
"""
import argparse
import fnmatch
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# The generic patterns: (name, expression)
PATTERNS = [
    ('a Linux home', re.compile(r'/home/')),
    ('a macOS home', re.compile(r'/Users/')),
    ('a Windows profile', re.compile(r'(?i)\\+Users\\+|\b[A-Z]:/+Users/')),
    ('a WSL network path', re.compile(r'(?i)(\\\\|//)wsl(\$|\.localhost)')),
    ('a WSL drive mount', re.compile(r'(?<![A-Za-z0-9_.-])/mnt/[a-z]/')),
    ('a private IPv4 address', re.compile(
        r'(?<![0-9.])(10(\.{0}){{3}}|172\.(1[6-9]|2[0-9]|3[01])(\.{0}){{2}}|192\.168(\.{0}){{2}})(?![0-9]|\.[0-9])'
        .format(r'(25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])'))),
    ('a .local host name', re.compile(r'(?i)(?<![A-Za-z0-9_./~-])[A-Za-z0-9][A-Za-z0-9-]*\.local(?![A-Za-z0-9_-])(?!/)')),
]


def git_files(tree):
    result = subprocess.run(['git', '-c', 'submodule.recurse=false', '-C', tree, 'ls-files', '-z'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        return None
    return [name for name in result.stdout.decode('utf-8', 'surrogateescape').split('\0') if name]


def is_checkout(tree):
    result = subprocess.run(['git', '-C', tree, 'rev-parse', '--show-prefix'], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    return result.returncode == 0 and result.stdout.strip() == b''


def read_allow(path):
    rows = []
    with open(path, encoding='utf-8') as f:
        for number, line in enumerate(f, 1):
            text = line.rstrip('\n')
            if not text.strip() or text.lstrip().startswith('#'):
                continue
            parts = text.split(' | ')
            if len(parts) < 3 or not parts[0] or not parts[1] or not parts[-1].strip():
                raise SystemExit('hygiene: %s:%d: not "<path> | <text> | <reason>": %s' % (path, number, text))
            rows.append({'line': number, 'path': parts[0], 'text': ' | '.join(parts[1:-1]), 'reason': parts[-1],
                         'used': 0})
    return rows


def path_matches(pattern, path):
    if pattern.endswith('/**'):
        return path.startswith(pattern[:-2])
    return fnmatch.fnmatchcase(path, pattern) if any(c in pattern for c in '*?[') else path == pattern


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tree', default=os.path.join(HERE, '..', '..'))
    parser.add_argument('--allow')
    parser.add_argument('--extra')
    options = parser.parse_args()
    tree = os.path.abspath(options.tree)
    allow_path = options.allow or os.path.join(tree, 'nulldc3', 'ci', 'hygiene-allow.txt')

    if not is_checkout(tree):
        print('SKIPPED: %s is not the top of a git checkout: there are no tracked files to read' % tree)
        return 77
    files = git_files(tree)
    if files is None:
        print('FAIL: git ls-files failed in %s' % tree)
        return 1
    rows = read_allow(allow_path)
    extra = []
    if options.extra:
        with open(options.extra, encoding='utf-8') as f:
            extra = [line.strip() for line in f if line.strip()]

    hits = []
    read = 0
    for name in files:
        path = os.path.join(tree, name)
        if not os.path.isfile(path) or os.path.islink(path):
            continue
        with open(path, 'rb') as f:
            data = f.read()
        read += 1
        text = data.decode('utf-8', 'replace')
        for number, line in enumerate(text.split('\n'), 1):
            found = [label for label, expression in PATTERNS if expression.search(line)]
            lower = line.lower()
            owner = [p for p in extra if p.lower() in lower]
            if not found and not owner:
                continue
            allowed = False
            if found and not owner:
                for row in rows:
                    if path_matches(row['path'], name) and (row['text'] == '*' or row['text'] in line):
                        row['used'] += 1
                        allowed = True
                        break
            if not allowed:
                what = ', '.join(found + ['an owner-specific pattern of --extra'] * bool(owner))
                hits.append('%s:%d: %s: %s' % (name, number, what, line.strip()[:200] if not owner else '(the line is not shown)'))

    gone = ['%s:%d: %s | %s' % (os.path.basename(allow_path), row['line'], row['path'], row['text'])
            for row in rows if not row['used']]
    for hit in hits:
        print('not allowed: ' + hit)
    for row in gone:
        print('gone (the row allows no line): ' + row)
    print('hygiene: %d tracked files of %s read, %d patterns%s; %d lines allowed by %d rows of %s; %d not allowed, '
          '%d rows gone' % (read, tree, len(PATTERNS), ' and %d more of --extra' % len(extra) if extra else '',
                            sum(row['used'] for row in rows), len(rows), os.path.basename(allow_path), len(hits),
                            len(gone)))
    return 1 if hits or gone else 0


if __name__ == '__main__':
    sys.exit(main())
