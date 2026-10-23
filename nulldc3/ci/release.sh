#!/bin/bash
# release.sh: the release files of the platform jobs checked, and on the push of a tag their GitHub Release
# (PL-D76, PL-D77). Two invocations, each named on the command line; nothing is chosen from the environment:
#
#   release.sh check --dir <files> --ui-info <bundle-info.json> [--out <dir>] [--tag <tag>]
#                    [--require-ui-tag true|false] [--version-file <file>]
#       (the job release-check: pull requests and dispatches.) Creates nothing and never runs gh.
#   release.sh publish --dir <files> --ui-info <bundle-info.json> [--out <dir>] [--tag <tag>] [--version-file <file>]
#       (the job release, on the push of a tag v*, alone.) Refuses, and creates nothing, unless GITHUB_EVENT_NAME is
#       push and GITHUB_REF_TYPE is tag; the tag is GITHUB_REF_NAME (--tag, when given, has to be the same).
#
# <files> holds what the four platform jobs made: nulldc3-<version>-linux-x86_64.AppImage,
# nulldc3-<version>-linux-arm64.AppImage, nulldc3-<version>-windows-x86_64.zip, nulldc3-<version>-macos-arm64.zip and
# beside each its configure lines, nulldc3-<version>-<system>-<arch>.configure.txt ("Dreamcast DevTools: embedding
# <dir> (<UI version>, protocol <n>)"); nothing else but an SHA256SUMS and release-notes.md of an earlier run.
# <bundle-info.json> is the one of the ui job's bundle, the UI every platform job embedded. The version is the one
# nulldc3/cmake/version.cmake makes of this checkout (or --version-file's line), the base nulldc3/version-base.txt.
#
# Both: the four files there and named with the version; each configure line names the ui job's version; the UI's
# version is the base exactly or the base followed by "+" (check), and the base exactly with --require-ui-tag true.
# SHA256SUMS (the four files, sha256sum's format, sorted by name) and release-notes.md (from release-notes.md beside
# this script) are written into <out> (default: <files>).
#   check: on a dispatch (GITHUB_EVENT_NAME workflow_dispatch) --tag is its input, and the base has to be the tag
#     without its v; on a pull request no tag is given and nothing more is asked of the version. It prints the gh
#     commands publish would run on the push of that tag (v<base> without one), with the tag's version in the names.
#   publish: the tag is v<version> (no build metadata), and the version, the base, the UI's version and every
#     configure line's are all the tag's. Then
#       gh release create <tag> --verify-tag --draft [--prerelease] --title "nullDC <version>"
#          --notes-file release-notes.md <the four files> SHA256SUMS
#     (--prerelease for a version with a pre-release part), the draft's assets checked against the files and
#     SHA256SUMS (names, sizes, digests), then gh release edit <tag> --draft=false. With the draft there already (a
#     run again) it uploads with --clobber and publishes; a Release that is published already is refused. GH_TOKEN
#     is gh's (the job gives github.token).
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
tree=$(cd "$here/../.." && pwd)
PLATFORMS="linux-x86_64 linux-arm64 windows-x86_64 macos-arm64"
SEMVER='(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?'

die() { echo "release.sh $command: $*" >&2; exit 1; }
usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' >&2; exit 2; }

[ $# -ge 1 ] || usage
command=$1
shift
case $command in check|publish) ;; *) usage ;; esac

dir="" ui_info="" out="" tag="" require_ui_tag=false version_file=""
while [ $# -gt 0 ]; do
  case $1 in
    --dir) dir=$2; shift 2 ;;
    --ui-info) ui_info=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --tag) tag=$2; shift 2 ;;
    --require-ui-tag) require_ui_tag=$2; shift 2 ;;
    --version-file) version_file=$2; shift 2 ;;
    *) usage ;;
  esac
done
[ -n "$dir" ] && [ -n "$ui_info" ] || usage
case $require_ui_tag in true|false) ;; *) die "--require-ui-tag is true or false, not '$require_ui_tag'" ;; esac
[ -n "$out" ] || out=$dir
event=${GITHUB_EVENT_NAME:-}

# publish: the push of a tag, before anything else is looked at
if [ "$command" = publish ]; then
  if [ "$event" != push ] || [ "${GITHUB_REF_TYPE:-}" != tag ]; then
    die "runs on the push of a tag alone (GITHUB_EVENT_NAME push, GITHUB_REF_TYPE tag), and this is '${event:-no event}', '${GITHUB_REF_TYPE:-no ref type}': nothing is created"
  fi
  [ -n "${GITHUB_REF_NAME:-}" ] || die "GITHUB_REF_NAME names no tag: nothing is created"
  if [ -n "$tag" ] && [ "$tag" != "$GITHUB_REF_NAME" ]; then
    die "the tag $tag is not the tag pushed, $GITHUB_REF_NAME: nothing is created"
  fi
  tag=$GITHUB_REF_NAME
else
  if [ "$event" = workflow_dispatch ] && [ -z "$tag" ]; then
    die "a dispatch names the tag it checks (its input tag)"
  fi
  if [ "$event" = pull_request ] && [ -n "$tag" ]; then
    die "a pull request has no tag to check, and $tag was given"
  fi
fi

tag_version() {
  printf '%s\n' "$1" | grep -Eq "^v$SEMVER\$" || die "'$1' is not a version tag (v<X.Y.Z>, a pre-release part allowed, no build metadata)"
  printf '%s\n' "${1#v}"
}

base=$(tr -d '\r\n' < "$tree/nulldc3/version-base.txt")
if [ -n "$version_file" ]; then
  version=$(head -n 1 "$version_file" | tr -d '\r\n')
else
  scratch=$(mktemp -d)
  trap 'rm -rf "$scratch"' EXIT
  cmake "-DSOURCE_DIR=$tree" "-DOUTPUT_DIR=$scratch" -P "$tree/nulldc3/cmake/version.cmake" > /dev/null
  version=$(cat "$scratch/nulldc3-version.txt")
fi
[ -f "$ui_info" ] || die "there is no $ui_info, the bundle-info.json of the ui job's bundle"
ui_version=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$ui_info")
echo "release.sh $command: the checkout is $version (base $base), the UI bundle $ui_version, event '${event:-none}'${tag:+, tag $tag}"

# the files: exactly the four and their configure lines, named with the version
problems=()
names() { local v=$1 p; for p in $PLATFORMS; do case $p in linux-*) echo "nulldc3-$v-$p.AppImage" ;; *) echo "nulldc3-$v-$p.zip" ;; esac; done; }
files=()
while read -r name; do files+=("$name"); done < <(names "$version")
expected=$(for f in "${files[@]}"; do echo "$f"; echo "${f%.*}.configure.txt"; done | sort)
there=$(cd "$dir" && ls -A | grep -vxE 'SHA256SUMS|release-notes\.md' | sort || true)
if [ "$there" != "$expected" ]; then
  problems+=("$dir holds [$(echo $there)], the release files of $version are [$(echo $expected)]")
fi
for f in "${files[@]}"; do
  [ -s "$dir/$f" ] || problems+=("$f is missing or empty")
  c=$dir/${f%.*}.configure.txt
  if [ -f "$c" ]; then
    line=$(grep -E '^Dreamcast DevTools: embedding ' "$c" || true)
    if [ "$(printf '%s\n' "$line" | grep -c .)" != 1 ]; then
      problems+=("$(basename "$c"): not one line \"Dreamcast DevTools: embedding ...\"")
    else
      said=$(printf '%s\n' "$line" | sed -nE 's/^Dreamcast DevTools: embedding .* \(([^,]+), protocol [0-9]+\)$/\1/p')
      [ "$said" = "$ui_version" ] || problems+=("$(basename "$c") names the UI '$said', the ui job's bundle is $ui_version")
    fi
  fi
done

# the UI's version against the base
if [ "$command" = publish ] || [ "$require_ui_tag" = true ]; then
  [ "$ui_version" = "$base" ] || problems+=("the UI's version is $ui_version, not the base $base: the UI is not at its tag v$base")
else
  case $ui_version in "$base"|"$base+"*) ;; *) problems+=("the UI's version $ui_version is neither the base $base nor $base+...") ;; esac
fi

# the version against the tag
would_tag=v$base
if [ "$command" = publish ]; then
  tv=$(tag_version "$tag")
  [ "$tv" = "$version" ] || problems+=("the tag $tag is not the version of the checkout, $version")
  [ "$tv" = "$base" ] || problems+=("the tag $tag is not the base $base of nulldc3/version-base.txt")
  would_tag=$tag
elif [ -n "$tag" ]; then
  tv=$(tag_version "$tag")
  [ "$tv" = "$base" ] || problems+=("the tag $tag is not the base $base of nulldc3/version-base.txt: publish would refuse it")
  would_tag=$tag
fi

if [ ${#problems[@]} -gt 0 ]; then
  printf 'release.sh %s: %s\n' "$command" "${problems[@]}" >&2
  die "${#problems[@]} problems: nothing is created"
fi

# SHA256SUMS and the notes
mkdir -p "$out"
python3 - "$dir" "$out/SHA256SUMS" "${files[@]}" <<'PY'
import hashlib, os, sys
directory, target, files = sys.argv[1], sys.argv[2], sys.argv[3:]
lines = ['%s  %s\n' % (hashlib.sha256(open(os.path.join(directory, name), 'rb').read()).hexdigest(), name)
         for name in sorted(files)]
open(target, 'w', newline='\n').write(''.join(lines))
PY
notes_version=${would_tag#v}
python3 - "$here/release-notes.md" "$out/release-notes.md" "$notes_version" "$would_tag" $(names "$notes_version") <<'PY'
import re, sys
template, target, version, tag, linux_x86_64, linux_arm64, windows_x86_64, macos_arm64 = sys.argv[1:9]
values = {'version': version, 'tag': tag, 'linux_x86_64': linux_x86_64, 'linux_arm64': linux_arm64,
          'windows_x86_64': windows_x86_64, 'macos_arm64': macos_arm64}
text = open(template, encoding='utf-8').read()
missing = sorted(set(re.findall(r'\{\{([a-z0-9_]+)\}\}', text)) - set(values))
if missing:
    sys.exit('release-notes.md: no value for %s' % ', '.join(missing))
text = re.sub(r'\{\{([a-z0-9_]+)\}\}', lambda m: values[m.group(1)], text)
if 'nullDC %s' % version not in text:
    sys.exit('release-notes.md: the notes do not say "nullDC %s"' % version)
open(target, 'w', encoding='utf-8', newline='\n').write(text)
PY
echo "release.sh $command: ${files[*]} named with $version, each configure line names the UI $ui_version; SHA256SUMS and release-notes.md written into $out"

prerelease=""
case ${notes_version%%+*} in *-*) prerelease=--prerelease ;; esac
assets=()
while read -r name; do assets+=("$name"); done < <(names "$notes_version")
assets+=(SHA256SUMS)

if [ "$command" = check ]; then
  echo "publish, on the push of the tag $would_tag, would run (the files named with $notes_version):"
  echo "  gh release view $would_tag --json tagName,isDraft,isPrerelease,assets"
  echo "  gh release create $would_tag --verify-tag --draft $prerelease --title \"nullDC $notes_version\" --notes-file release-notes.md ${assets[*]}"
  echo "  (with the draft there already: gh release upload $would_tag --clobber ${assets[*]})"
  echo "  gh release view $would_tag --json tagName,isDraft,isPrerelease,assets   (the draft's assets against the files and SHA256SUMS)"
  echo "  gh release edit $would_tag --draft=false"
  echo "the notes:"
  sed 's/^/  | /' "$out/release-notes.md"
  exit 0
fi

# publish
paths=()
for f in "${files[@]}"; do paths+=("$dir/$f"); done
paths+=("$out/SHA256SUMS")
view() {
  local err rc=0
  err=$(mktemp)
  gh release view "$tag" --json tagName,isDraft,isPrerelease,assets 2> "$err" || rc=$?
  if [ $rc != 0 ]; then
    if grep -qi 'release not found' "$err"; then rm -f "$err"; echo none; return 0; fi
    cat "$err" >&2; rm -f "$err"; return $rc
  fi
  rm -f "$err"
}
state() { python3 -c 'import json, sys
text = sys.stdin.read()
if text.strip() == "none":
    print("none")
else:
    print("draft" if json.loads(text)["isDraft"] else "published")'; }

echo "+ gh release view $tag --json tagName,isDraft,isPrerelease,assets"
existing=$(view)
case $(printf '%s' "$existing" | state) in
  published) die "the Release $tag is published already: nothing is changed" ;;
  draft)
    echo "the draft of $tag is there already: its files are uploaded again"
    echo "+ gh release upload $tag --clobber ${assets[*]}"
    gh release upload "$tag" --clobber "${paths[@]}" ;;
  none)
    echo "+ gh release create $tag --verify-tag --draft $prerelease --title \"nullDC $version\" --notes-file release-notes.md ${assets[*]}"
    gh release create "$tag" --verify-tag --draft $prerelease --title "nullDC $version" --notes-file "$out/release-notes.md" "${paths[@]}" ;;
esac

echo "+ gh release view $tag --json tagName,isDraft,isPrerelease,assets"
view > "$out/draft.json"
python3 - "$out/draft.json" "$dir" "$out/SHA256SUMS" "${files[@]}" <<'PY'
import hashlib, json, os, sys
view_path, directory, sums_path, files = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
text = open(view_path).read()
if text.strip() == 'none':
    sys.exit('the Release is not there after it was made: not published')
view = json.loads(text)
if not view.get('isDraft'):
    sys.exit('the Release is not a draft after it was made: not published')
expected = {}
for name in files:
    data = open(os.path.join(directory, name), 'rb').read()
    expected[name] = (len(data), hashlib.sha256(data).hexdigest())
sums = open(sums_path, 'rb').read()
expected['SHA256SUMS'] = (len(sums), hashlib.sha256(sums).hexdigest())
for line in sums.decode('utf-8').splitlines():
    digest, name = line.split('  ', 1)
    if expected.get(name, (0, ''))[1] != digest:
        sys.exit('SHA256SUMS: %s is not the sha256 of %s' % (digest, name))
problems = []
names = sorted(a['name'] for a in view.get('assets', []))
if names != sorted(expected):
    problems.append('its assets are %s, not %s' % (', '.join(names) or 'none', ', '.join(sorted(expected))))
for asset in view.get('assets', []):
    want = expected.get(asset['name'])
    if want is None:
        continue
    if asset.get('size') != want[0]:
        problems.append('%s has %s bytes, not %d' % (asset['name'], asset.get('size'), want[0]))
    digest = asset.get('digest')
    if isinstance(digest, str) and digest != 'sha256:' + want[1]:
        problems.append('%s has the digest %s, not sha256:%s' % (asset['name'], digest, want[1]))
    if asset.get('state', 'uploaded') != 'uploaded':
        problems.append('%s is %s, not uploaded' % (asset['name'], asset.get('state')))
if problems:
    sys.exit('the draft is not the release files: %s. It stays a draft' % '; '.join(problems))
print('the draft holds %s, each of the size and sha256 of the files' % ', '.join(names))
PY
rm -f "$out/draft.json"
echo "+ gh release edit $tag --draft=false"
gh release edit "$tag" --draft=false > /dev/null
echo "+ gh release view $tag --json tagName,isDraft,isPrerelease,assets"
[ "$(view | state)" = published ] || die "the Release $tag is still a draft"
echo "the Release $tag, \"nullDC $version\", is published${prerelease:+ as a pre-release}"
