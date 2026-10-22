# ci.*

The tests of the emulator's CI files: the workflow `.github/workflows/ci.yml` and the scripts of `nulldc3/ci/` that
its jobs call (README.md of the tree, "CI", says what runs when).

| test | what has to hold | needs |
|---|---|---|
| `ci.hygiene` | `nulldc3/ci/hygiene.py` on the tree: no tracked file (as it is in the working tree) names a home directory of Linux (`/home/`), macOS (`/Users/`) or Windows (`\Users\`, `C:/Users/`), a path of WSL's network (`\\wsl$`, `\\wsl.localhost`) or of its drive mounts (`/mnt/<letter>/`), a private IPv4 address (10/8, 172.16/12, 192.168/16) or a `.local` host name, but the lines that a row of `nulldc3/ci/hygiene-allow.txt` allows, each with its reason; and every row still allows a line ("gone" otherwise). Skipped when the tree is not a git checkout | git |
| `ci.workflows` | `nulldc3/ci/check_workflows.py --self-test` on `.github/workflows/ci.yml`: the rules of the cut's 6.1 and 6.5 that actionlint does not check (below), and each of 29 mutations of the workflow caught by one of them; then (Linux and macOS) `release_test.py`: `nulldc3/ci/release.sh` against a stand-in for `gh` in a scratch repository below the build directory (below). On Windows the stand-in part is not run (`release.sh` is a bash script, and no test there runs a POSIX shell), and the test says so | python3; git and cmake for the stand-in |

## check_workflows.py

It reads the workflow as text, with a reader of its own for the YAML the file uses (block mappings and sequences,
flow sequences, quoted, plain and block scalars; anything else is refused, and the test is red), and checks:

- the triggers: `pull_request`, `push` of tags `v*` and no branch, `workflow_dispatch` with exactly the inputs `tag`
  (a string, required) and `require_ui_tag` (a boolean, false by default); no other trigger, nothing that says
  `dry_run`;
- `permissions: contents: read` at the top and on `release-check`, `contents: write` on `release` and on no other
  job, no other permission;
- the seven jobs; `release`'s `if:` exactly `github.event_name == 'push' && github.ref_type == 'tag'` and its `needs`
  exactly `ui`, `linux-x86_64`, `linux-arm64`, `windows-x86_64`, `macos-arm64` (never `release-check`, which is
  skipped on a tag push); `release-check`'s `if:` exactly `github.event_name != 'push'`; the four platform jobs need
  `ui`;
- `release.sh publish` called once, in `release` alone, `release.sh check` once, in `release-check` alone; no step
  runs `gh`;
- every `uses:` pinned by a full commit SHA with its version in a comment;
- no `secrets.` but `secrets.GITHUB_TOKEN`; no step that prints the environment (`printenv`, `env` or `set` alone,
  `export -p`, `declare -p`, PowerShell's `env:` drive, `toJSON` of a context) in any command of a run line: each
  line is split into its commands at `&&`, `||`, `;`, `|` and `&` (not the `&` of `2>&1` or `&>`), and into what a
  `$(...)`, a backquote or a `(...)` holds, so that `nulldc3/ci/linux.sh deps && env` is caught as `env` alone is;
- `concurrency` per ref, cancelled in progress for pull requests only; every artifact kept 7 days; every checkout with
  the whole history and no token kept; every command of every `run:` line a call of a script of `nulldc3/ci/` or one
  of two provisioning lines (no build logic in the file, not even behind a call of a script on its line, and no
  command substitution or subshell).

Run it alone: `python3 nulldc3/ci/check_workflows.py [--self-test]`. `actionlint` (a pinned release) checks the
syntax and the expressions; neither replaces the other.

## release_test.py

A scratch git repository below the build directory with `release.sh`, `release-notes.md`,
`nulldc3/cmake/version.cmake` and `nulldc3/version-base.txt` of the tree, a stand-in `gh` first on the `PATH` that logs
every call and keeps one Release (none, a draft, published) in a file, stand-in release files and configure lines.
`check` (a pull request; a dispatch with the tag, without it, with `v3.0.1`, with `require_ui_tag` and the UI
untagged and tagged; a configure line naming another UI; a file missing; a run with `GITHUB_EVENT_NAME=push` and
`GITHUB_REF_TYPE=tag`) never calls `gh`. `publish` on the tag `v<base>` made in the scratch repository: refused for a
dispatch or a branch push, with nothing created; the draft made, its assets checked, then published; a draft there
already uploaded with `--clobber` and published; a published Release refused, nothing changed; an asset uploaded
wrong left a draft; a UI `<base>+g<h>`, a configure line naming another UI and the tag `v3.0.1` refused before any
`gh` call. No tag is made anywhere else. With `--clone` the scratch repository is a clone of the tree's `HEAD`
(`git clone --shared`) instead of the four files.

    python3 tests/ci/release_test.py --tree . --work <scratch directory> [--clone]
