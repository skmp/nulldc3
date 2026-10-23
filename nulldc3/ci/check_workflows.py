#!/usr/bin/env python3
"""check_workflows.py: the rules of the emulator's workflow (the cut's 6.1, 6.5 and 6.6) that actionlint does not
check, read from the file as text (no YAML library: the workflow is parsed by the small reader below, which knows the
block mappings and sequences, flow sequences, quoted and plain scalars and block scalars the file uses, and refuses
anything else).

    check_workflows.py [--workflow <file>] [--self-test]

The rules, each printed with PASS or FAIL:
  - the triggers: pull_request, push of tags v* (no branches), workflow_dispatch with exactly the inputs tag (string,
    required) and require_ui_tag (boolean, default false); no other trigger, no dry_run;
  - permissions: contents: read at the top, contents: read on release-check, contents: write on release and on no
    other job, no other permission anywhere;
  - the jobs ui, linux-x86_64, linux-arm64, windows-x86_64, macos-arm64, release-check and release, and no other;
  - release: if exactly github.event_name == 'push' && github.ref_type == 'tag', needs exactly the five build jobs
    (never release-check); release-check: if exactly github.event_name != 'push', needs the five build jobs;
  - release.sh publish is called in the job release alone, once, and release.sh check in release-check alone; no
    step runs gh itself;
  - every uses: is pinned by a full commit SHA (40 hexadecimal digits) with its version in a comment;
  - no secrets. but secrets.GITHUB_TOKEN; no step prints the environment (printenv, env or set alone, export -p,
    declare -p, Get-ChildItem env:, toJSON of env, secrets, vars, github, runner or job) in any command of a run
    line: a line is split into its commands at &&, ||, ;, | and &, and into what a $(...), a backquote or a (...)
    holds;
  - concurrency ci-${{ github.ref }}, cancelled in progress for pull requests only;
  - every artifact kept 7 days; every checkout with fetch-depth 0 and without the token kept;
  - every command of every run line provisions or calls a script of nulldc3/ci/ (no build logic in the file, not
    even behind a call of a script on its line).
Exit status 1 when a rule failed. --self-test applies a list of mutations to the workflow, each of which has to fail
a rule, and fails when one does not. Python 3.9, the standard library only.
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_WORKFLOW = os.path.join(HERE, '..', '..', '.github', 'workflows', 'ci.yml')

BUILD_JOBS = ['ui', 'linux-x86_64', 'linux-arm64', 'windows-x86_64', 'macos-arm64']
JOBS = BUILD_JOBS + ['release-check', 'release']
RELEASE_IF = "github.event_name == 'push' && github.ref_type == 'tag'"
RELEASE_CHECK_IF = "github.event_name != 'push'"

# what a run step may be: a call of a script of nulldc3/ci/, or one of these lines that provision the runner
PROVISION = (
    'git config --global core.autocrlf false',
    'brew install ninja ccache',
)


class YamlError(Exception):
    pass


# ---- a reader for the YAML of the workflow ----------------------------------------------------------------------

def strip_comment(text):
    """The text of a line without its comment (a # at the start or after a blank, outside quotes)."""
    quote = None
    for i, c in enumerate(text):
        if quote:
            if c == quote:
                quote = None
        elif c in '\'"':
            quote = c
        elif c == '#' and (i == 0 or text[i - 1] in ' \t'):
            return text[:i].rstrip()
    return text.rstrip()


def scalar(text):
    text = text.strip()
    if text.startswith("'"):
        if not text.endswith("'") or len(text) < 2:
            raise YamlError('a quoted scalar that does not end: %s' % text)
        return text[1:-1].replace("''", "'")
    if text.startswith('"'):
        if not text.endswith('"') or len(text) < 2:
            raise YamlError('a quoted scalar that does not end: %s' % text)
        return text[1:-1].encode('utf-8').decode('unicode_escape')
    if text.startswith('['):
        if not text.endswith(']'):
            raise YamlError('a flow sequence that does not end on its line: %s' % text)
        inner = text[1:-1].strip()
        return [scalar(item) for item in inner.split(',')] if inner else []
    if text.startswith(('{', '&', '*', '!', '%', '@', '`')):
        raise YamlError('a scalar this reader does not take: %s' % text)
    if text in ('true', 'false'):
        return text == 'true'
    return text


class Line:
    def __init__(self, number, indent, text, raw):
        self.number, self.indent, self.text, self.raw = number, indent, text, raw


def lines_of(source):
    out = []
    for number, raw in enumerate(source.splitlines(), 1):
        if '\t' in raw[:len(raw) - len(raw.lstrip())]:
            raise YamlError('line %d: a tab in the indentation' % number)
        text = strip_comment(raw)
        if not text.strip():
            out.append(Line(number, None, '', raw))
            continue
        out.append(Line(number, len(text) - len(text.lstrip(' ')), text.strip(), raw))
    return out


KEY = re.compile(r'^([A-Za-z0-9_.-]+|\'[^\']*\'|"[^"]*"):(?:\s+(.*))?$')


class Reader:
    def __init__(self, source):
        self.lines = lines_of(source)
        self.at = 0
        self.uses_lines = []    # (line number, raw line) of every uses:

    def skip_blank(self):
        while self.at < len(self.lines) and self.lines[self.at].indent is None:
            self.at += 1

    def peek(self):
        self.skip_blank()
        return self.lines[self.at] if self.at < len(self.lines) else None

    def block_scalar(self, indent):
        """The lines of a | or > block, more indented than indent."""
        parts = []
        while self.at < len(self.lines):
            line = self.lines[self.at]
            raw = line.raw
            if raw.strip() and len(raw) - len(raw.lstrip(' ')) <= indent:
                break
            parts.append(raw.strip())
            self.at += 1
        return '\n'.join(parts).strip('\n') + '\n'

    def value(self, text, indent, line):
        if text in ('|', '>', '|-', '>-', '|+', '>+'):
            return self.block_scalar(indent)
        if text == '':
            nxt = self.peek()
            if nxt is None or nxt.indent < indent or (nxt.indent == indent and not nxt.text.startswith('- ')
                                                       and nxt.text != '-'):
                return None
            return self.node(nxt.indent)
        return scalar(text)

    def node(self, indent):
        line = self.peek()
        if line.text.startswith('- ') or line.text == '-':
            return self.sequence(indent)
        return self.mapping(indent)

    def mapping(self, indent, first=None):
        result = {}
        while True:
            line = self.peek() if first is None else first
            if line is None or line.indent < indent:
                return result
            if line.indent > indent:
                raise YamlError('line %d: indented more than its mapping' % line.number)
            if line.text.startswith('- '):
                return result
            match = KEY.match(line.text)
            if not match:
                raise YamlError('line %d: not a key: %s' % (line.number, line.text))
            key = scalar(match.group(1))
            if key in result:
                raise YamlError('line %d: the key %s twice' % (line.number, key))
            if first is None:
                self.at += 1
            first = None
            if key == 'uses':
                self.uses_lines.append((line.number, line.raw))
            result[key] = self.value(match.group(2) or '', indent, line)

    def sequence(self, indent):
        result = []
        while True:
            line = self.peek()
            if line is None or line.indent < indent:
                return result
            if line.indent != indent or not (line.text.startswith('- ') or line.text == '-'):
                return result
            rest = line.text[2:].strip() if line.text != '-' else ''
            self.at += 1
            if rest == '':
                result.append(self.node(self.peek().indent))
            elif KEY.match(rest):
                # a mapping whose first key is on the line of the dash
                inner = indent + 2 + (len(line.text[2:]) - len(line.text[2:].lstrip(' ')))
                result.append(self.mapping(inner, Line(line.number, inner, rest, line.raw)))
            else:
                result.append(scalar(rest))

    def document(self):
        line = self.peek()
        if line is None or line.indent != 0:
            raise YamlError('the workflow does not start with a mapping at column 0')
        doc = self.mapping(0)
        if self.peek() is not None:
            raise YamlError('line %d: not read' % self.peek().number)
        return doc


# ---- the rules ----------------------------------------------------------------------------------------------------

def unwrap(expression):
    """An if: with or without ${{ }} around it."""
    text = (expression or '').strip() if isinstance(expression, str) else ''
    match = re.match(r'^\$\{\{(.*)\}\}$', text)
    return match.group(1).strip() if match else text


def run_lines(step):
    run = step.get('run') if isinstance(step, dict) else None
    if not isinstance(run, str):
        return []
    return [line.strip() for line in run.splitlines() if line.strip() and not line.strip().startswith('#')]


def commands_of(line):
    """The commands of a run line: split where a shell (bash or PowerShell) starts another command, at &&, ||, ;, |
    and & outside quotes (not the & of a redirection, 2>&1 or &>), so that a rule holds for every command of the
    line and not only for its first one. A command substitution or a subshell ($(...), a backquote, (...) outside
    single quotes) is a command of its own: the line gets the marker "$(" (which no rule takes) and the commands
    inside it are split the same way."""
    commands, current, quote, depth, backquote, i = [], '', None, 0, False, 0

    def cut():
        commands.append(current.strip())
        return ''

    while i < len(line):
        c = line[i]
        if quote == "'":
            if c == "'":
                quote = None
            current += c
        elif c == '`' and backquote:
            current = cut()
            backquote = False
        elif line.startswith('$(', i) or c == '`' or (c == '(' and quote is None):
            current = cut()
            commands.append('$(')
            if c == '`':
                backquote = True
            else:
                depth += 1
            i += 2 if c == '$' else 1
            continue
        elif c == ')' and depth > 0:
            current = cut()
            depth -= 1
        elif quote == '"':
            if c == '"':
                quote = None
            current += c
        elif c in '\'"':
            quote = c
            current += c
        elif c in ';|' or (c == '&' and not current.endswith(('>', '<')) and not line.startswith('&>', i)):
            current = cut()
            while i + 1 < len(line) and line[i + 1] in '&|;':
                i += 1
        else:
            current += c
        i += 1
    cut()
    return [command for command in commands if command]


ENV_DUMP = re.compile(r'(\bprintenv\b|^env\s*(>|$)|^set\s*(>|$)|\bexport\s+-p\b|\bdeclare\s+-[px]\b|'
                      r'\b(Get-ChildItem|gci|ls|dir)\s+env:|toJSON\(\s*(env|secrets|vars|github|runner|job)\s*\)|'
                      r'\bcompgen\s+-v\b|\[System\.Environment\]::GetEnvironmentVariables|\bSet-Item\b.*env:)',
                      re.I)
SCRIPT_CALL = re.compile(r'^(\.[\\/])?nulldc3[\\/]ci[\\/][A-Za-z0-9_.-]+\.(sh|ps1|py)(\s|$)')


def check(source):
    """The results of the rules: a list of (passed, text)."""
    results = []

    def rule(passed, text):
        results.append((bool(passed), text))

    try:
        reader = Reader(source)
        doc = reader.document()
    except YamlError as error:
        return [(False, 'the workflow is read: %s' % error)]
    rule(True, 'the workflow is read')

    # the triggers ("on" is read as a key; YAML 1.1 would make it true)
    on = doc.get('on')
    rule(isinstance(on, dict) and sorted(on) == ['pull_request', 'push', 'workflow_dispatch'],
         'the triggers are pull_request, push and workflow_dispatch, and no other: %s' % (sorted(on) if isinstance(on, dict) else on))
    push = on.get('push') if isinstance(on, dict) else None
    rule(isinstance(push, dict) and list(push) == ['tags'] and push.get('tags') == ['v*'],
         'push starts a run for tags v* alone, no branch: %s' % push)
    pr = on.get('pull_request') if isinstance(on, dict) else None
    rule(pr is None, 'pull_request has no filter (every base): %s' % pr)
    dispatch = on.get('workflow_dispatch') if isinstance(on, dict) else None
    inputs = dispatch.get('inputs') if isinstance(dispatch, dict) else None
    rule(isinstance(inputs, dict) and sorted(inputs) == ['require_ui_tag', 'tag'],
         'the dispatch has exactly the inputs tag and require_ui_tag (no dry_run): %s' % (sorted(inputs) if isinstance(inputs, dict) else inputs))
    tag = inputs.get('tag') if isinstance(inputs, dict) else None
    rule(isinstance(tag, dict) and tag.get('type') == 'string' and tag.get('required') is True,
         'the input tag is a string and required: %s' % tag)
    ui_tag = inputs.get('require_ui_tag') if isinstance(inputs, dict) else None
    rule(isinstance(ui_tag, dict) and ui_tag.get('type') == 'boolean' and ui_tag.get('default') is False,
         'the input require_ui_tag is a boolean, false by default: %s' % ui_tag)
    rule(not re.search(r'dry[_-]?run', source, re.I), 'nothing says dry_run')

    # permissions
    rule(doc.get('permissions') == {'contents': 'read'}, 'permissions at the top: contents: read, and nothing else: %s' % doc.get('permissions'))
    jobs = doc.get('jobs') if isinstance(doc.get('jobs'), dict) else {}
    rule(sorted(jobs) == sorted(JOBS), 'the jobs are %s: %s' % (', '.join(JOBS), ', '.join(jobs)))
    for name, job in jobs.items():
        if not isinstance(job, dict):
            rule(False, 'the job %s is a mapping' % name)
            continue
        permissions = job.get('permissions')
        if name == 'release':
            rule(permissions == {'contents': 'write'}, 'release: permissions contents: write, and nothing else: %s' % permissions)
        elif name == 'release-check':
            rule(permissions == {'contents': 'read'}, 'release-check: permissions contents: read: %s' % permissions)
        else:
            rule(permissions is None, '%s: no permissions of its own (the top\'s read): %s' % (name, permissions))
    rule('write' not in str(doc.get('permissions')) and 'write-all' not in source.replace('contents: write', ''),
         'write appears in the permissions of release alone')

    # release and release-check
    release = jobs.get('release', {}) if isinstance(jobs.get('release'), dict) else {}
    check_job = jobs.get('release-check', {}) if isinstance(jobs.get('release-check'), dict) else {}
    rule(unwrap(release.get('if')) == RELEASE_IF, 'release: if is exactly "%s": %r' % (RELEASE_IF, release.get('if')))
    needs = release.get('needs')
    rule(isinstance(needs, list) and sorted(needs) == sorted(BUILD_JOBS),
         'release needs exactly %s (never release-check): %s' % (', '.join(BUILD_JOBS), needs))
    rule(unwrap(check_job.get('if')) == RELEASE_CHECK_IF,
         'release-check: if is exactly "%s": %r' % (RELEASE_CHECK_IF, check_job.get('if')))
    needs = check_job.get('needs')
    rule(isinstance(needs, list) and sorted(needs) == sorted(BUILD_JOBS),
         'release-check needs %s: %s' % (', '.join(BUILD_JOBS), needs))
    for name in BUILD_JOBS:
        job = jobs.get(name) if isinstance(jobs.get(name), dict) else {}
        rule(job.get('if') is None, '%s runs on every event (no if): %r' % (name, job.get('if')))
        if name != 'ui':
            rule(job.get('needs') == 'ui' or job.get('needs') == ['ui'], '%s needs ui: %r' % (name, job.get('needs')))

    # the release scripts' invocations, and gh
    calls = {}
    for name, job in jobs.items():
        steps = job.get('steps') if isinstance(job, dict) and isinstance(job.get('steps'), list) else []
        for step in steps:
            for line in run_lines(step):
                for mode in re.findall(r'release\.sh\s+(\S+)', line):
                    calls.setdefault(mode, []).append(name)
                if re.search(r'(^|[\s;&|(])gh(\.exe)?\s', line + ' '):
                    calls.setdefault('gh', []).append(name)
    rule(calls.get('publish') == ['release'], 'release.sh publish is called once, in release alone: %s' % calls.get('publish'))
    rule(calls.get('check') == ['release-check'], 'release.sh check is called once, in release-check alone: %s' % calls.get('check'))
    rule(sorted(calls) == ['check', 'publish'], 'release.sh is called with check and publish alone, and no step runs gh: %s' % sorted(calls))

    # the actions, pinned
    bad = []
    for number, raw in reader.uses_lines:
        match = re.match(r'^\s*(?:-\s+)?uses:\s+([^\s#]+)\s*(#\s*(\S+))?\s*$', raw)
        if not match or not re.match(r'^[A-Za-z0-9_.-]+/[A-Za-z0-9_./-]+@[0-9a-f]{40}$', match.group(1)) \
                or not match.group(3) or not re.match(r'^v?\d+(\.\d+)*$', match.group(3)):
            bad.append('line %d: %s' % (number, raw.strip()))
    rule(reader.uses_lines and not bad, 'every uses: is pinned by a full commit SHA with its version in a comment (%d actions)%s'
         % (len(reader.uses_lines), ': ' + '; '.join(bad) if bad else ''))

    # secrets and the environment
    secrets = sorted(set(re.findall(r'secrets\.([A-Za-z0-9_]+)', source)) - {'GITHUB_TOKEN'})
    rule(not secrets and 'secrets[' not in source, 'no secret but GITHUB_TOKEN: %s' % secrets)
    dumps = []
    for name, job in jobs.items():
        steps = job.get('steps') if isinstance(job, dict) and isinstance(job.get('steps'), list) else []
        for step in steps:
            for line in run_lines(step):
                if any(ENV_DUMP.search(command) for command in commands_of(line)):
                    dumps.append('%s: %s' % (name, line))
    for match in re.finditer(r'toJSON\(\s*(env|secrets|vars|github|runner|job)\s*\)', source, re.I):
        dumps.append(match.group(0))
    rule(not dumps, 'no step prints the environment: %s' % dumps)

    # concurrency
    concurrency = doc.get('concurrency')
    rule(isinstance(concurrency, dict) and concurrency.get('group') == 'ci-${{ github.ref }}'
         and concurrency.get('cancel-in-progress') == "${{ github.event_name == 'pull_request' }}",
         'concurrency: ci-${{ github.ref }}, cancelled in progress for pull requests only: %s' % concurrency)

    # artifacts and checkouts
    uploads, checkouts, runs = [], [], []
    for name, job in jobs.items():
        steps = job.get('steps') if isinstance(job, dict) and isinstance(job.get('steps'), list) else []
        for step in steps:
            if not isinstance(step, dict):
                continue
            uses = str(step.get('uses', ''))
            with_ = step.get('with') if isinstance(step.get('with'), dict) else {}
            if uses.startswith('actions/upload-artifact@'):
                uploads.append((name, with_.get('retention-days')))
            if uses.startswith('actions/checkout@'):
                checkouts.append((name, str(with_.get('fetch-depth')), with_.get('persist-credentials')))
            for line in run_lines(step):
                runs.append((name, line))
    rule(uploads and all(days == '7' for _, days in uploads), 'every artifact is kept 7 days: %s' % uploads)
    rule(checkouts and all(depth == '0' and keep is False for _, depth, keep in checkouts),
         'every checkout has the whole history (fetch-depth 0) and keeps no token: %s' % checkouts)
    thin = [(name, line) for name, line in runs
            if not all(SCRIPT_CALL.match(command) or command in PROVISION for command in commands_of(line))]
    rule(not thin, 'every command of every run step provisions or calls a script of nulldc3/ci/: %s' % thin)
    return results


# ---- the mutations of --self-test: each has to fail a rule -------------------------------------------------------

def mutations(source):
    def sub(old, new, count=1):
        if old not in source:
            raise SystemExit('self-test: the workflow has no %r to mutate' % old)
        return source.replace(old, new, count)

    release_at = source.index('\n  release:\n')
    check_at = source.index('\n  release-check:\n')

    def in_job(at, old, new):
        head, tail = source[:at], source[at:]
        if old not in tail:
            raise SystemExit('self-test: the job has no %r' % old)
        return head + tail.replace(old, new, 1)

    yield 'contents: write at the top', sub('permissions:\n  contents: read\n', 'permissions:\n  contents: write\n')
    yield 'release without its if', in_job(release_at, "    if: github.event_name == 'push' && github.ref_type == 'tag'\n", '')
    yield 'release without the event condition', in_job(release_at, "github.event_name == 'push' && github.ref_type == 'tag'", "github.ref_type == 'tag'")
    yield 'release needs release-check', in_job(release_at, 'needs: [ui, linux-x86_64,', 'needs: [release-check, ui, linux-x86_64,')
    yield 'release-check with write', in_job(check_at, 'permissions:\n      contents: read', 'permissions:\n      contents: write')
    yield 'release-check on every event', in_job(check_at, "    if: github.event_name != 'push'\n", '')
    yield 'publish called in release-check', in_job(check_at, 'release.sh check', 'release.sh publish')
    yield 'publish called in a build job', sub('nulldc3/ci/linux.sh deps', 'nulldc3/ci/release.sh publish --dir x --ui-info y')
    yield 'an action by its tag', sub('actions/setup-node@49933ea5288caeca8642d1e84afbd3f7d6820020 # v4.4.0', 'actions/setup-node@v4 # v4')
    yield 'an action by a branch', sub('actions/checkout@11d5960a326750d5838078e36cf38b85af677262 # v4.4.0', 'actions/checkout@main')
    yield 'an action pinned without its version', sub('actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830 # v4.3.0', 'actions/cache@0057852bfaa89a56745cba8c7296529d2fc39830')
    yield 'a printenv step', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: printenv\n      - run: nulldc3/ci/linux.sh deps\n')
    yield 'an env step', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: env | sort\n      - run: nulldc3/ci/linux.sh deps\n')
    # the same behind a call of a script, on its line
    yield 'env behind && on a script\'s line', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: nulldc3/ci/linux.sh deps && env\n')
    yield 'env | sort behind ; on a script\'s line', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: nulldc3/ci/linux.sh deps; env | sort\n')
    yield 'env behind & on a script\'s line', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: nulldc3/ci/linux.sh deps & env > e.txt\n')
    yield 'env in a substitution on a script\'s line', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: nulldc3/ci/linux.sh deps "$(env)"\n')
    yield 'printenv behind || on a script\'s line', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: nulldc3/ci/linux.sh deps || printenv\n')
    yield 'build logic behind && on a script\'s line', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: nulldc3/ci/linux.sh deps && cmake -S . -B b && ninja -C b\n')
    yield 'build logic behind ; on the PowerShell line', sub('-Out "$env:RUNNER_TEMP\\release-files"\n', '-Out "$env:RUNNER_TEMP\\release-files"; cmake --build b\n')
    yield 'a dry_run input', sub('      require_ui_tag:\n', '      dry_run:\n        type: boolean\n        default: true\n      require_ui_tag:\n')
    yield 'a push of branches', sub("  push:\n    tags:\n      - 'v*'\n", "  push:\n    branches:\n      - main\n    tags:\n      - 'v*'\n")
    yield 'a secret', sub('GH_TOKEN: ${{ github.token }}', 'GH_TOKEN: ${{ secrets.RELEASE_PAT }}')
    yield 'gh run in a step', in_job(release_at, 'run: nulldc3/ci/release.sh publish', 'run: gh release create v1 && nulldc3/ci/release.sh publish')
    yield 'a job with write', sub('  ui:\n    runs-on: ubuntu-24.04\n', '  ui:\n    runs-on: ubuntu-24.04\n    permissions:\n      contents: write\n')
    yield 'concurrency cancelled for tags too', sub("cancel-in-progress: ${{ github.event_name == 'pull_request' }}", 'cancel-in-progress: true')
    yield 'an artifact kept 90 days', sub('retention-days: 7', 'retention-days: 90')
    yield 'build logic in the file', sub('      - run: nulldc3/ci/linux.sh deps\n', '      - run: cmake -S . -B build && ninja -C build\n      - run: nulldc3/ci/linux.sh deps\n')
    yield 'a schedule', sub('  workflow_dispatch:\n', "  schedule:\n    - cron: '0 0 * * *'\n  workflow_dispatch:\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--workflow', default=DEFAULT_WORKFLOW)
    parser.add_argument('--self-test', action='store_true')
    options = parser.parse_args()
    with open(options.workflow, encoding='utf-8') as f:
        source = f.read()

    results = check(source)
    for passed, text in results:
        print('%s %s' % ('PASS' if passed else 'FAIL', text))
    failed = sum(1 for passed, _ in results if not passed)
    print('check_workflows: %d rules, %d failed (%s)' % (len(results), failed, os.path.relpath(options.workflow)))
    status = 1 if failed else 0

    if options.self_test:
        missed = []
        count = 0
        for name, mutated in mutations(source):
            count += 1
            caught = [text for passed, text in check(mutated) if not passed]
            print('self-test: %-40s %s' % (name, ('caught: ' + caught[0][:120]) if caught else 'NOT CAUGHT'))
            if not caught:
                missed.append(name)
        print('check_workflows --self-test: %d mutations, %d not caught' % (count, len(missed)))
        if missed:
            status = 1
    return status


if __name__ == '__main__':
    sys.exit(main())
