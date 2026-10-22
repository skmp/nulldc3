#!/bin/bash
# macos.sh: the macOS job of CI (.github/workflows/ci.yml), the same on a runner and on a Mac of one's own (macOS's
# own bash 3.2 runs it). Every path comes from the arguments; on a runner RUNNER_TEMP is the default of --work.
#
#   macos.sh <ci|full> --dist <dist-native> --ui <ui checkout> [--work <dir>] [--out <dir>] [--data <dir>]
#            [--ccache] [--no-npm-ci] [--no-emsdk] [--jobs <n>] [-- <cmake argument of the tests project>...]
#
#  1. The UI checkout: its LICENSE (the zip's LICENSE-devtools) has to be there and the checkout at the commit the
#     tree pins for devtools (git rev-parse HEAD:devtools); else the script stops and says what it compared.
#  2. The release bundle <work>/release/nulldc3.app: Release, deployment target 11.0, the UI bundle --dist embedded
#     (an explicit path: no bundle there is a configure error; the bundle's Resources then carry LICENSE-devtools from
#     the submodule devtools/ of the tree, which has to be initialised), -DNULLDC_PREFIX_MAP=ON,
#     -DCMAKE_VERBOSE_MAKEFILE=OFF, through ccache only with --ccache (a runner's; never on a Mac where Homebrew's
#     ccache cannot start); its configure lines "Dreamcast DevTools: ..." into
#     <out>/nulldc3-<version>-macos-arm64.configure.txt; codesign --verify --strict --deep of the bundle.
#  3. The tests project (<work>/tests) with the bundle's executable as NULLDC_EMULATOR, the vector submodules, the UI
#     checkout (npm ci there, unless --no-npm-ci) as NULLDC_DEVTOOLS_DIR; ci: what CI has, missing data skips its
#     tests; full: the data of the gates from --data (elf/, wren7-rtl/, caique-rtl/), NULLDC_TESTS_REQUIRE_DATA=ON.
#     ctest -j4 --output-junit; the tests/macos targets and the harnesses run the emulator through
#     tests/macos/macrun.py (on a runner it applies the sandbox profile, inside a macrun.py --detach on a Mac they find
#     their caller sandboxed: PL-D69). Two targets that flake on a Mac's loopback now and then
#     (debugger.net.test_e2e, debugger.contract.m2.guest.interp; MACb, LA64) are run once more when they are the only
#     ones that failed, and the summary says so. The skipped tests with their reasons go into $GITHUB_STEP_SUMMARY
#     when it is set (junit.py), and onto stdout. --no-emsdk: an emsdk of the machine is not the run's to use.
#  4. The zip <out>/nulldc3-<version>-macos-arm64.zip, ditto -c -k --sequesterRsrc --keepParent of the folder
#     nulldc3-<version>-macos-arm64/: nulldc3.app (copied with ditto, its signature kept), LICENSE, LICENSE-GPL,
#     LICENSE-LGPL, LICENSE-shell (nulldc3/LICENSE), LICENSE-devtools (the UI checkout's LICENSE) and README.txt
#     (readme-macos.txt of this directory).
#  5. The smoke run of the zip unpacked with ditto -x -k: codesign --verify --strict --deep of the unpacked bundle,
#     the folder's files, then smoke.py: the inner executable's --version, a headless guest run and the debugger's
#     describe, each through macrun.py, with P0's keys and a --portable directory below <work>.
# --jobs <n>: ninja's -j (default: ninja's own). Exit status 0 when the build, the tests, the zip and the smoke run all
# passed.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
tree=$(cd "$here/../.." && pwd)
FLAKY="debugger.net.test_e2e debugger.contract.m2.guest.interp"
LICENCES="LICENSE LICENSE-GPL LICENSE-LGPL LICENSE-devtools LICENSE-shell"

die() { echo "macos.sh: $*" >&2; exit 1; }
usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' >&2; exit 2; }
summary() { if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then printf '%s\n' "$@" >> "$GITHUB_STEP_SUMMARY"; fi; }

[ $# -ge 1 ] || usage
mode=$1
shift
case $mode in ci|full) ;; *) usage ;; esac
dist="" ui="" work="" out="" data="" ccache=no npm_ci=yes emsdk=yes jobs=""
extra=""
while [ $# -gt 0 ]; do
  case $1 in
    --dist) dist=$2; shift 2 ;;
    --ui) ui=$2; shift 2 ;;
    --work) work=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --data) data=$2; shift 2 ;;
    --ccache) ccache=yes; shift ;;
    --no-npm-ci) npm_ci=no; shift ;;
    --no-emsdk) emsdk=no; shift ;;
    --jobs) jobs="-j $2"; shift 2 ;;
    --) shift; extra=$*; break ;;
    *) usage ;;
  esac
done
[ -n "$dist" ] && [ -n "$ui" ] || usage
if [ -z "$work" ]; then
  [ -n "${RUNNER_TEMP:-}" ] || die "--work <dir> is needed outside a runner"
  work=$RUNNER_TEMP/nulldc3-ci
fi
[ -n "$out" ] || out=$work/out
mkdir -p "$work" "$out"
work=$(cd "$work" && pwd)
out=$(cd "$out" && pwd)
dist=$(cd "$dist" && pwd)
ui=$(cd "$ui" && pwd)
python=/usr/bin/python3

# 1. the UI checkout
[ -f "$ui/LICENSE" ] || die "there is no $ui/LICENSE, the UI's licence that the zip carries as LICENSE-devtools"
pin=$(git -C "$tree" rev-parse HEAD:devtools 2>/dev/null) || die "the tree $tree pins no commit for devtools (git rev-parse HEAD:devtools)"
ui_head=$(git -C "$ui" rev-parse HEAD 2>/dev/null) || die "the UI checkout $ui is not a git checkout (git -C $ui rev-parse HEAD)"
[ "$ui_head" = "$pin" ] || die "the UI checkout $ui is at $ui_head, the tree pins devtools at $pin: its LICENSE is not the pinned UI's"
ui_version=$($python -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$dist/bundle-info.json")

# 2. the release bundle
sdk=$(xcrun --sdk macosx --show-sdk-path)
cc=""
if [ $ccache = yes ]; then
  [ -n "${CCACHE_DIR:-}" ] || export CCACHE_DIR=${RUNNER_TEMP:-$work}/ccache
  export CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-2G}
  cc="-DNULLDC_CCACHE=ON"
fi
release=$work/release
cmake -S "$tree" -B "$release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  "-DCMAKE_OSX_SYSROOT=$sdk" $cc "-DNULLDC_DEVTOOLS_DIST=$dist" -DNULLDC_PREFIX_MAP=ON -DCMAKE_VERBOSE_MAKEFILE=OFF \
  -Wno-dev | tee "$work/release-configure.log"
ninja -C "$release" $jobs
version=$(cat "$release/nulldc3-version.txt")
name=nulldc3-$version-macos-arm64
grep -q '^-- Dreamcast DevTools: embedding ' "$work/release-configure.log" || die "the release bundle embeds no UI (no line \"Dreamcast DevTools: embedding\" in its configure output)"
grep '^-- Dreamcast DevTools: ' "$work/release-configure.log" | sed 's/^-- //' > "$out/$name.configure.txt"
echo "nulldc3 $version, macos-arm64: $(grep embedding "$out/$name.configure.txt")"
summary "nullDC \`$version\` macos-arm64: $(grep embedding "$out/$name.configure.txt" | sed 's/^Dreamcast DevTools: //')"
codesign --verify --strict --deep "$release/nulldc3.app"
echo "PASS codesign --verify --strict --deep $release/nulldc3.app"
failed=""

# 3. the tests project
if [ $npm_ci = yes ]; then
  (cd "$ui" && npm ci)
else
  [ -d "$ui/node_modules" ] || die "--no-npm-ci: the UI checkout $ui has no node_modules"
fi
tests=$work/tests
args="-DNULLDC_EMULATOR=$release/nulldc3.app/Contents/MacOS/nulldc3 -DNULLDC_SH4_TESTS_DIR=$tree/tests/vendor/sh4-tests"
args="$args -DNULLDC_ARM7_TESTS_DIR=$tree/tests/vendor/arm7di-tests-dreamcast -DNULLDC_DEVTOOLS_DIR=$ui"
args="$args -DPython3_EXECUTABLE=$python -DCMAKE_OSX_SYSROOT=$sdk $cc"
if [ $mode = full ]; then
  [ -n "$data" ] || die "full: --data <dir> with elf/, wren7-rtl/ and caique-rtl/"
  data=$(cd "$data" && pwd)
  args="$args -DNULLDC_ELF_DIR=$data/elf -DNULLDC_WREN7_DIR=$data/wren7-rtl -DNULLDC_CAIQUE_DIR=$data/caique-rtl"
  [ -d "$data/shrike4-rtl" ] && args="$args -DNULLDC_SHRIKE4_DIR=$data/shrike4-rtl"
  args="$args -DNULLDC_TESTS_REQUIRE_DATA=ON"
else
  args="$args -DNULLDC_ELF_DIR= -DNULLDC_WREN7_DIR= -DNULLDC_CAIQUE_DIR= -DNULLDC_SHRIKE4_DIR= -DNULLDC_TESTS_REQUIRE_DATA=OFF"
fi
ctest_env=""
if [ $emsdk = no ]; then
  args="$args -DNULLDC_EMCC=OFF"
  ctest_env="EMSDK=$work/no-emsdk"
fi
# (the paths of this script's arguments have no blanks: a word each)
cmake -S "$tree/tests" -B "$tests" -G Ninja -Wno-dev $args $extra
ninja -C "$tests" $jobs
rc=0
env $ctest_env ctest --test-dir "$tests" -j4 --output-on-failure --output-junit "$tests/junit.xml" || rc=$?
cp "$tests/junit.xml" "$work/junit-1.xml"
$python -B "$here/junit.py" "$tests/junit.xml" --title "Tests, macos-arm64, $mode mode" ${GITHUB_STEP_SUMMARY:+--summary "$GITHUB_STEP_SUMMARY"}
if [ $rc != 0 ]; then
  fails=$($python -B "$here/junit.py" "$tests/junit.xml" --failed-only | tr '\n' ' ')
  others=""
  for t in $fails; do case " $FLAKY " in *" $t "*) ;; *) others="$others $t" ;; esac; done
  if [ -n "$fails" ] && [ -z "$others" ]; then
    pattern="^($(echo $fails | sed 's/\./\\./g; s/ /|/g'))\$"
    echo "macos.sh: only targets that flake on a Mac's loopback failed ($fails): run once more"
    rc=0
    env $ctest_env ctest --test-dir "$tests" -R "$pattern" --output-on-failure --output-junit "$tests/junit-again.xml" || rc=$?
    summary "Run once more, as targets that flake on a Mac's loopback: $fails; $( [ $rc = 0 ] && echo passed || echo 'failed again')"
  fi
fi
[ $rc = 0 ] || failed="$failed ctest"

# 4. the zip
stage=$work/zip
rm -rf "$stage"
mkdir -p "$stage/$name"
ditto "$release/nulldc3.app" "$stage/$name/nulldc3.app"
cp "$tree/LICENSE" "$stage/$name/LICENSE"
cp "$tree/LICENSE-GPL" "$stage/$name/LICENSE-GPL"
cp "$tree/LICENSE-LGPL" "$stage/$name/LICENSE-LGPL"
cp "$tree/nulldc3/LICENSE" "$stage/$name/LICENSE-shell"
cp "$ui/LICENSE" "$stage/$name/LICENSE-devtools"
sed "s/{{version}}/$version/g" "$here/readme-macos.txt" > "$stage/$name/README.txt"
rm -f "$out/$name.zip"
ditto -c -k --sequesterRsrc --keepParent "$stage/$name" "$out/$name.zip"
rm -rf "$stage"
echo "macos.sh: $out/$name.zip, $(wc -c < "$out/$name.zip" | tr -d ' ') bytes"

# 5. the smoke run of the zip unpacked
smoke=$work/smoke
rm -rf "$smoke"
mkdir -p "$smoke/unpacked"
ditto -x -k "$out/$name.zip" "$smoke/unpacked"
app=$smoke/unpacked/$name/nulldc3.app
if codesign --verify --strict --deep "$app"; then
  echo "PASS codesign --verify --strict --deep of the unpacked bundle"
else
  echo "FAIL codesign --verify --strict --deep of the unpacked bundle"
  failed="$failed codesign"
fi
listed=$(cd "$smoke/unpacked/$name" && ls -A | sort | tr '\n' ' ')
wanted=$(printf '%s\n' $LICENCES README.txt nulldc3.app | sort | tr '\n' ' ')
resources=$(cd "$app/Contents/Resources" && ls -A | grep '^LICENSE' | sort | tr '\n' ' ')
licences=$(printf '%s\n' $LICENCES | sort | tr '\n' ' ')
if [ "$listed" = "$wanted" ] && [ "$resources" = "$licences" ] && cmp -s "$smoke/unpacked/$name/LICENSE-devtools" "$ui/LICENSE"; then
  echo "PASS the folder $name: $listed; nulldc3.app/Contents/Resources: $resources"
else
  echo "FAIL the folder $name: $listed(expected $wanted); nulldc3.app/Contents/Resources: $resources(expected $licences)"
  failed="$failed folder"
fi
$python -B "$here/smoke.py" --exe "$app/Contents/MacOS/nulldc3" --version "$version" --ui-version "$ui_version" \
  --guest "$tree/tests/debugger/guest/dsptest/dsptest.elf" --work "$smoke/run" --portable "$smoke/portable" \
  -- $python -I -B "$tree/tests/macos/macrun.py" --name smoke --work "$smoke/run" --timeout 120 -- \
  || failed="$failed smoke"
rm -rf "$smoke"

if [ -n "$failed" ]; then
  echo "macos.sh: failed:$failed"
  exit 1
fi
echo "macos.sh: $out/$name.zip"
