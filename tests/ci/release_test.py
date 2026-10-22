#!/usr/bin/env python3
"""release_test.py: nulldc3/ci/release.sh against a stand-in for gh that records its arguments, in a scratch git
repository below --work (never in a repository of the tree, and no tag anywhere else).

    release_test.py --tree <the top of the tree> --work <scratch directory>

The scratch repository holds release.sh, release-notes.md, nulldc3/cmake/version.cmake and nulldc3/version-base.txt
of the tree, one commit; its tags are made there. The stand-in gh keeps a Release in a JSON file (none, a draft, a
published one) and logs every call. The cases:
  check (creates nothing; gh is never called, the state stays):
    - a pull request: the files named with the checkout's version (no tag reachable: <base>+g<h>), the UI's version
      the base followed by "+": passes, prints the gh commands of the push of v<base>;
    - a pull request with a UI of another base: refused; a pull request given a tag: refused;
    - a dispatch with the tag v<base>: passes, prints the commands of that tag; without a tag: refused; with the
      tag v3.0.1: refused;
    - a dispatch with require_ui_tag true and the UI <base>+g<h>: refused; with the UI <base>: passes;
    - a configure line naming another UI: refused; a file missing: refused;
    - GITHUB_EVENT_NAME push and GITHUB_REF_TYPE tag: passes, no gh call;
  publish (the tag v<base> made in the scratch repository, the files named with <base>):
    - GITHUB_EVENT_NAME workflow_dispatch on the tag: refused, no gh call;
    - no Release: the draft is made (create --verify-tag --draft --prerelease --title "nullDC <base>" with the four
      files and SHA256SUMS), its assets checked, then published (edit --draft=false);
    - a draft there already: upload --clobber, then published;
    - a published Release there: refused, nothing created or changed;
    - a draft whose asset the upload got wrong (the stand-in's digest): it stays a draft, no edit;
    - the UI <base>+g<h>: refused, no gh call; a configure line naming another UI: refused, no gh call;
    - the tag v3.0.1 (a commit after it): refused, no gh call.
Exit status 0 when every case holds. Python 3.9, the standard library only.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

STUB = r'''#!/usr/bin/env python3
# the stand-in for gh of release_test.py: logs its arguments (GH_STUB_LOG) and keeps one Release (GH_STUB_STATE)
import hashlib, json, os, sys
args = sys.argv[1:]
with open(os.environ['GH_STUB_LOG'], 'a') as f:
    f.write(json.dumps(args) + '\n')
path = os.environ['GH_STUB_STATE']
state = json.load(open(path)) if os.path.exists(path) else None
def asset(file):
    data = open(file, 'rb').read()
    digest = 'sha256:' + hashlib.sha256(data).hexdigest()
    if os.environ.get('GH_STUB_CORRUPT') == os.path.basename(file):
        digest = 'sha256:' + '0' * 64
    return {'name': os.path.basename(file), 'size': len(data), 'digest': digest, 'state': 'uploaded'}
def save():
    json.dump(state, open(path, 'w'))
if args[:2] == ['release', 'view']:
    if state is None or state['tagName'] != args[2]:
        sys.stderr.write('release not found\n')
        sys.exit(1)
    print(json.dumps(state))
elif args[:2] == ['release', 'create']:
    tag = args[2]
    files = []
    i = 3
    while i < len(args):
        if args[i] in ('--title', '--notes-file'):
            i += 2
            continue
        if args[i].startswith('--'):
            i += 1
            continue
        files.append(args[i])
        i += 1
    state = {'tagName': tag, 'isDraft': '--draft' in args, 'isPrerelease': '--prerelease' in args,
             'assets': [asset(f) for f in files]}
    save()
elif args[:2] == ['release', 'upload']:
    files = [a for a in args[3:] if not a.startswith('--')]
    names = set(os.path.basename(f) for f in files)
    state['assets'] = [a for a in state['assets'] if a['name'] not in names] + [asset(f) for f in files]
    save()
elif args[:2] == ['release', 'edit'] and '--draft=false' in args:
    state['isDraft'] = False
    save()
else:
    sys.stderr.write('the stand-in for gh knows no %s\n' % ' '.join(args))
    sys.exit(2)
'''

PLATFORMS = ('linux-x86_64', 'linux-arm64', 'windows-x86_64', 'macos-arm64')

failures = []


def check(condition, text):
    print('%s %s' % ('PASS' if condition else 'FAIL', text), flush=True)
    if not condition:
        failures.append(text)


def git(repo, *args):
    return subprocess.run(['git', '-C', repo, '-c', 'user.name=release_test', '-c', 'user.email=release_test@example.invalid',
                           '-c', 'commit.gpgsign=false', '-c', 'tag.gpgsign=false'] + list(args),
                          check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.decode().strip()


def file_name(version, platform):
    return 'nulldc3-%s-%s.%s' % (version, platform, 'AppImage' if platform.startswith('linux') else 'zip')


def make_files(directory, version, ui_version, other_ui=None, missing=None):
    if os.path.isdir(directory):
        shutil.rmtree(directory)
    os.makedirs(directory)
    for platform in PLATFORMS:
        name = file_name(version, platform)
        if name != missing:
            with open(os.path.join(directory, name), 'wb') as f:
                f.write(hashlib.sha256(name.encode()).digest() * 64)
        said = other_ui if (other_ui and platform == 'windows-x86_64') else ui_version
        with open(os.path.join(directory, name.rsplit('.', 1)[0] + '.configure.txt'), 'w') as f:
            f.write('Dreamcast DevTools: the bundle was built from the submodule\'s commit 0123456789abcdef\n'
                    'Dreamcast DevTools: embedding /runner/temp/dist-native (%s, protocol 3)\n' % said)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--tree', required=True)
    parser.add_argument('--work', required=True)
    options = parser.parse_args()
    tree = os.path.abspath(options.tree)
    work = os.path.abspath(options.work)
    if shutil.which('git') is None or shutil.which('cmake') is None:
        print('SKIPPED: release_test needs git and cmake')
        return 77
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)

    # the scratch repository
    repo = os.path.join(work, 'repo')
    for part in ('nulldc3/ci/release.sh', 'nulldc3/ci/release-notes.md', 'nulldc3/cmake/version.cmake',
                 'nulldc3/version-base.txt'):
        os.makedirs(os.path.dirname(os.path.join(repo, part)), exist_ok=True)
        shutil.copy2(os.path.join(tree, part), os.path.join(repo, part))
    git(work, 'init', '-q', repo)
    git(repo, 'add', '-A')
    git(repo, 'commit', '-q', '-m', 'scratch')
    base = open(os.path.join(repo, 'nulldc3', 'version-base.txt')).read().strip()
    head = git(repo, 'rev-parse', 'HEAD')

    bin_dir = os.path.join(work, 'bin')
    os.makedirs(bin_dir)
    stub = os.path.join(bin_dir, 'gh')
    with open(stub, 'w') as f:
        f.write(STUB.replace('#!/usr/bin/env python3', '#!' + sys.executable, 1))
    os.chmod(stub, 0o755)
    log = os.path.join(work, 'gh.log')
    state = os.path.join(work, 'gh.json')
    files = os.path.join(work, 'files')
    out = os.path.join(work, 'out')
    ui_info = os.path.join(work, 'bundle-info.json')

    def ui(version):
        with open(ui_info, 'w') as f:
            json.dump({'version': version, 'protocol': 3}, f)

    def release(command, env, *args, gh_state=None, corrupt=None):
        for path in (log, state):
            if os.path.exists(path):
                os.remove(path)
        if gh_state is not None:
            with open(state, 'w') as f:
                json.dump(gh_state, f)
        environment = {k: v for k, v in os.environ.items() if not k.startswith('GITHUB_')}
        environment.update(env)
        environment.update({'PATH': bin_dir + os.pathsep + environment.get('PATH', ''), 'GH_STUB_LOG': log,
                            'GH_STUB_STATE': state})
        if corrupt:
            environment['GH_STUB_CORRUPT'] = corrupt
        p = subprocess.run(['bash', os.path.join(repo, 'nulldc3', 'ci', 'release.sh'), command, '--dir', files,
                            '--ui-info', ui_info, '--out', out] + list(args),
                           cwd=work, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        calls = [json.loads(line) for line in open(log)] if os.path.exists(log) else []
        after = json.load(open(state)) if os.path.exists(state) else None
        return p.returncode, p.stdout.decode('utf-8', 'replace'), calls, after

    def show(output):
        return ' | '.join(line for line in output.splitlines() if line.strip())[-400:]

    pr = {'GITHUB_EVENT_NAME': 'pull_request'}
    dispatch = {'GITHUB_EVENT_NAME': 'workflow_dispatch', 'GITHUB_REF_TYPE': 'branch', 'GITHUB_REF_NAME': 'main'}
    pushed = {'GITHUB_EVENT_NAME': 'push', 'GITHUB_REF_TYPE': 'tag', 'GITHUB_REF_NAME': 'v' + base}
    untagged = '%s+g%s' % (base, head[:10])
    ui_untagged = base + '+g7b88410915'

    # ---- check, no tag reachable -------------------------------------------------------------------------------
    make_files(files, untagged, ui_untagged)
    ui(ui_untagged)
    rc, output, calls, after = release('check', pr)
    check(rc == 0 and not calls and after is None and ('gh release create v%s --verify-tag --draft --prerelease --title "nullDC %s"' % (base, base)) in output
          and file_name(base, 'linux-x86_64') in output and os.path.isfile(os.path.join(out, 'SHA256SUMS')),
          'check, a pull request: the files of %s and the UI %s pass, the commands of v%s are printed, no gh call: rc=%d %s'
          % (untagged, ui_untagged, base, rc, show(output) if rc else ''))
    sums = open(os.path.join(out, 'SHA256SUMS')).read().splitlines() if os.path.isfile(os.path.join(out, 'SHA256SUMS')) else []
    names = [line.split('  ', 1)[1] for line in sums]
    check(names == sorted(file_name(untagged, p) for p in PLATFORMS) and all(
        hashlib.sha256(open(os.path.join(files, n), 'rb').read()).hexdigest() == line.split('  ')[0] for n, line in zip(names, sums)),
        'check writes SHA256SUMS: the four files, sorted, in sha256sum\'s format: %s' % names)
    ui('3.0.1+g7b88410915')
    rc, output, calls, after = release('check', pr)
    check(rc != 0 and not calls and 'is neither the base' in output, 'check, a pull request, the UI of another base: refused: rc=%d %s' % (rc, show(output)))
    ui(ui_untagged)
    rc, output, calls, after = release('check', pr, '--tag', 'v' + base)
    check(rc != 0 and not calls, 'check, a pull request given a tag: refused: rc=%d %s' % (rc, show(output)))
    rc, output, calls, after = release('check', dispatch, '--tag', 'v' + base, '--require-ui-tag', 'false')
    check(rc == 0 and not calls and 'the tag v%s' % base in output, 'check, a dispatch with the tag v%s: passes: rc=%d %s' % (base, rc, show(output) if rc else ''))
    rc, output, calls, after = release('check', dispatch)
    check(rc != 0 and not calls, 'check, a dispatch without a tag: refused: rc=%d %s' % (rc, show(output)))
    rc, output, calls, after = release('check', dispatch, '--tag', 'v3.0.1')
    check(rc != 0 and not calls and 'is not the base' in output, 'check, a dispatch with the tag v3.0.1: refused: rc=%d %s' % (rc, show(output)))
    rc, output, calls, after = release('check', dispatch, '--tag', 'v' + base, '--require-ui-tag', 'true')
    check(rc != 0 and not calls and 'not at its tag' in output,
          'check, a dispatch with require_ui_tag and the UI %s: refused: rc=%d %s' % (ui_untagged, rc, show(output)))
    make_files(files, untagged, base)
    ui(base)
    rc, output, calls, after = release('check', dispatch, '--tag', 'v' + base, '--require-ui-tag', 'true')
    check(rc == 0 and not calls, 'check, a dispatch with require_ui_tag and the UI %s: passes: rc=%d %s' % (base, rc, show(output) if rc else ''))
    make_files(files, untagged, ui_untagged, other_ui=base + '+g0000000000')
    ui(ui_untagged)
    rc, output, calls, after = release('check', pr)
    check(rc != 0 and not calls and 'names the UI' in output, 'check, a configure line naming another UI: refused: rc=%d %s' % (rc, show(output)))
    make_files(files, untagged, ui_untagged, missing=file_name(untagged, 'macos-arm64'))
    rc, output, calls, after = release('check', pr)
    check(rc != 0 and not calls, 'check, the macOS zip missing: refused: rc=%d %s' % (rc, show(output)))
    make_files(files, untagged, ui_untagged)
    rc, output, calls, after = release('check', dict(pushed), gh_state=None)
    check(not calls and after is None, 'check with GITHUB_EVENT_NAME push and GITHUB_REF_TYPE tag: no gh call (rc=%d)' % rc)

    # ---- publish, on the tag v<base> ----------------------------------------------------------------------------
    git(repo, 'tag', '-a', 'v' + base, '-m', 'scratch')
    make_files(files, base, base)
    ui(base)
    rc, output, calls, after = release('publish', dict(pushed, GITHUB_EVENT_NAME='workflow_dispatch'))
    check(rc != 0 and not calls and after is None and 'nothing is created' in output,
          'publish, a dispatch on the tag: refused, nothing created: rc=%d %s' % (rc, show(output)))
    rc, output, calls, after = release('publish', dict(pushed, GITHUB_REF_TYPE='branch'))
    check(rc != 0 and not calls and after is None, 'publish, a push of a branch: refused, nothing created: rc=%d' % rc)
    rc, output, calls, after = release('publish', pushed)
    verbs = [c[1] for c in calls]
    create = [c for c in calls if c[1] == 'create']
    wanted_assets = sorted([file_name(base, p) for p in PLATFORMS] + ['SHA256SUMS'])
    check(rc == 0 and verbs == ['view', 'create', 'view', 'edit', 'view'] and create
          and create[0][2:6] == ['v' + base, '--verify-tag', '--draft', '--prerelease']
          and create[0][6:8] == ['--title', 'nullDC ' + base]
          and sorted(os.path.basename(a) for a in create[0][10:]) == wanted_assets
          and after and after['isDraft'] is False and sorted(a['name'] for a in after['assets']) == wanted_assets,
          'publish, no Release: the draft is made with the four files and SHA256SUMS, checked, published: rc=%d calls %s %s'
          % (rc, verbs, show(output) if rc else ''))
    draft = dict(after or {}, isDraft=True, assets=[])
    rc, output, calls, after = release('publish', pushed, gh_state=draft)
    verbs = [c[1] for c in calls]
    check(rc == 0 and verbs == ['view', 'upload', 'view', 'edit', 'view'] and '--clobber' in calls[1]
          and after['isDraft'] is False and sorted(a['name'] for a in after['assets']) == wanted_assets,
          'publish, a draft there already: uploaded with --clobber, published: rc=%d calls %s %s' % (rc, verbs, show(output) if rc else ''))
    published = after
    rc, output, calls, after = release('publish', pushed, gh_state=published)
    check(rc != 0 and [c[1] for c in calls] == ['view'] and after == published and 'published already' in output,
          'publish, a published Release there: refused, nothing changed: rc=%d calls %s' % (rc, [c[1] for c in calls]))
    rc, output, calls, after = release('publish', pushed, corrupt=file_name(base, 'linux-arm64'))
    verbs = [c[1] for c in calls]
    check(rc != 0 and 'edit' not in verbs and after and after['isDraft'] is True and 'stays a draft' in output,
          'publish, an asset uploaded wrong: it stays a draft, no edit: rc=%d calls %s' % (rc, verbs))
    ui(ui_untagged)
    make_files(files, base, ui_untagged)
    rc, output, calls, after = release('publish', pushed)
    check(rc != 0 and not calls and after is None, 'publish, the UI %s: refused, no gh call: rc=%d %s' % (ui_untagged, rc, show(output)))
    ui(base)
    make_files(files, base, base, other_ui=base + '+g0000000000')
    rc, output, calls, after = release('publish', pushed)
    check(rc != 0 and not calls and after is None, 'publish, a configure line naming another UI: refused, no gh call: rc=%d %s' % (rc, show(output)))
    rc, output, calls, after = release('publish', dict(pushed, GITHUB_REF_NAME='v' + base), '--tag', 'v3.0.1')
    check(rc != 0 and not calls, 'publish, --tag another than the tag pushed: refused: rc=%d' % rc)

    # the tag v3.0.1 on a commit after it
    git(repo, 'commit', '-q', '--allow-empty', '-m', 'after')
    git(repo, 'tag', '-a', 'v3.0.1', '-m', 'scratch')
    make_files(files, '3.0.1', base)
    rc, output, calls, after = release('publish', dict(pushed, GITHUB_REF_NAME='v3.0.1'))
    check(rc != 0 and not calls and after is None and 'is not the base' in output,
          'publish, the tag v3.0.1 with the base %s: refused, no gh call: rc=%d %s' % (base, rc, show(output)))

    print('release_test: %d cases failed' % len(failures))
    shutil.rmtree(work, ignore_errors=True) if not failures else None
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
