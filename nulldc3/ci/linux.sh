#!/bin/bash
# linux.sh: the Linux jobs of CI (.github/workflows/ci.yml), the same on a runner and on a machine of one's own.
# Every path comes from the arguments; on a runner RUNNER_TEMP is the default of --work. Nothing names a machine.
#
#   linux.sh deps
#       the apt packages of the build and the tests (sudo apt-get; a runner's image has sudo without a password)
#   linux.sh ui --ui <ui checkout> --out <dir> [--fetch-tags]
#       npm ci and npm run build:native in the UI checkout, its dist-native/ copied into <dir> (the artifact
#       "dist-native"); --fetch-tags first fetches the tags of the checkout's origin (the UI's public repository on a
#       runner), so that the UI's version finds its tag v* also when the checkout of the submodule left them out
#   linux.sh build --dist <dist-native> [--work <dir>] [--out <dir>]
#       <work>/release: the release emulator, Release, the UI bundle <dist-native> embedded (an explicit path: no
#       bundle there is a configure error), -DNULLDC_PREFIX_MAP=ON, -DCMAKE_VERBOSE_MAKEFILE=OFF; its configure
#       lines "Dreamcast DevTools: ..." into <out>/nulldc3-<version>-linux-<arch>.configure.txt, beside the
#       AppImage appimage.sh makes there. <work>/hle: the -DHAS_TA_LLE=OFF emulator for the system tests (no UI).
#   linux.sh test <ci|full> --ui <ui checkout> [--work <dir>] [--data <dir>] [--no-npm-ci] [--no-emsdk]
#                 [-- <cmake argument>...]
#       the tests project (<work>/tests) with the two emulators of build as NULLDC_EMULATOR and
#       NULLDC_EMULATOR_TA_HLE, the vector submodules of the tree, the UI checkout (npm ci there; with --no-npm-ci
#       its node_modules as they are, for a checkout that is not the script's to change) as NULLDC_DEVTOOLS_DIR, and ctest -j4 --output-junit <work>/tests/junit.xml. ci: what CI has (PL-D58), the
#       data that is missing skips its tests (NULLDC_TESTS_REQUIRE_DATA=OFF). full: the data of the gates from
#       --data <dir>, which has elf/ (2ndmix.elf, hello.elf), wren7-rtl/, caique-rtl/ and shrike4-rtl/, and every
#       test has to run (NULLDC_TESTS_REQUIRE_DATA=ON). The skipped tests, each with its reason, go into
#       $GITHUB_STEP_SUMMARY when it is set, and onto stdout (junit.py). --no-emsdk: an emsdk of the machine is
#       not the run's to use (-DNULLDC_EMCC=OFF, and EMSDK of ctest a directory without one), as on a runner, which
#       has none. The cmake arguments after -- go to the tests project's configure (an objdump of one's own, say).
#
# The UI checkout <ui> is the submodule devtools on a runner, a clone of the UI elsewhere; it has to be at the commit
# the tree pins for devtools (the gitlink), so that the UI the tests use is the one that is embedded.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
tree=$(cd "$here/../.." && pwd)

die() { echo "linux.sh: $*" >&2; exit 1; }
usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' >&2; exit 2; }

arch() {
  case "$(uname -m)" in
    x86_64) echo x86_64 ;;
    aarch64|arm64) echo arm64 ;;
    *) die "no release for the processor $(uname -m)" ;;
  esac
}

# the UI checkout: a directory with package.json, at the commit the tree pins for devtools
check_ui() {
  local ui=$1 pin head
  [ -f "$ui/package.json" ] || die "the UI checkout $ui has no package.json"
  pin=$(git -C "$tree" rev-parse HEAD:devtools 2>/dev/null) || die "the tree $tree pins no commit for devtools (git rev-parse HEAD:devtools)"
  head=$(git -C "$ui" rev-parse HEAD 2>/dev/null) || die "the UI checkout $ui is not a git checkout (git -C $ui rev-parse HEAD)"
  [ "$head" = "$pin" ] || die "the UI checkout $ui is at $head, the tree pins devtools at $pin (git rev-parse HEAD:devtools)"
}

summary() {
  if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
    printf '%s\n' "$@" >> "$GITHUB_STEP_SUMMARY"
  fi
}

default_work() {
  if [ -n "${RUNNER_TEMP:-}" ]; then echo "$RUNNER_TEMP/nulldc3-ci"; else die "--work <dir> is needed outside a runner"; fi
}

cmd_deps() {
  local packages=(
    build-essential cmake ninja-build ccache python3 pkg-config git curl file
    libx11-dev libgl-dev libegl-dev libgles-dev libasound2-dev libpulse-dev libudev-dev libevdev-dev
    libcurl4-openssl-dev
    binutils-arm-none-eabi
  )
  # The SH objdump of Ubuntu's binutils-sh4-linux-gnu is an oracle of debugger.disasm.sh4_objdump on x86_64 only:
  # its arm64 build reads the 8-bit displacements of bt/bf with an unsigned char and names 512 wrong targets (the
  # char of arm64 Linux is unsigned), so the arm64 job installs none and the two sh4_objdump tests skip there
  if [ "$(arch)" = x86_64 ]; then
    packages+=(binutils-sh4-linux-gnu)
  fi
  sudo apt-get update -q
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q --no-install-recommends "${packages[@]}"
}

cmd_ui() {
  local ui="" out="" fetch_tags=no
  while [ $# -gt 0 ]; do
    case $1 in
      --ui) ui=$2; shift 2 ;;
      --out) out=$2; shift 2 ;;
      --fetch-tags) fetch_tags=yes; shift ;;
      *) usage ;;
    esac
  done
  [ -n "$ui" ] && [ -n "$out" ] || usage
  check_ui "$ui"
  if [ $fetch_tags = yes ]; then
    git -C "$ui" fetch --quiet --tags origin
  fi
  (cd "$ui" && npm ci && npm run build:native)
  [ -f "$ui/dist-native/index.html" ] && [ -f "$ui/dist-native/bundle-info.json" ] || die "$ui/dist-native is no bundle"
  rm -rf "$out"
  mkdir -p "$out"
  cp -R "$ui/dist-native/." "$out/"
  local version
  version=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$out/bundle-info.json")
  echo "the UI bundle: $version"
  summary "Dreamcast DevTools \`$version\` (the bundle every release file embeds)"
}

# ccache on a runner: its directory under RUNNER_TEMP, which the workflow restores and saves (actions/cache)
if [ -n "${RUNNER_TEMP:-}" ] && [ -z "${CCACHE_DIR:-}" ]; then
  export CCACHE_DIR=$RUNNER_TEMP/ccache CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-2G}
fi

cmd_build() {
  local dist="" work="" out=""
  while [ $# -gt 0 ]; do
    case $1 in
      --dist) dist=$2; shift 2 ;;
      --work) work=$2; shift 2 ;;
      --out) out=$2; shift 2 ;;
      *) usage ;;
    esac
  done
  [ -n "$dist" ] || usage
  [ -n "$work" ] || work=$(default_work)
  [ -n "$out" ] || out=$work/out
  [ -f "$dist/bundle-info.json" ] || die "the UI bundle $dist has no bundle-info.json"
  dist=$(cd "$dist" && pwd)
  mkdir -p "$work" "$out"
  local launcher=()
  if command -v ccache > /dev/null 2>&1; then
    launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  fi

  # the release emulator
  cmake -S "$tree" -B "$work/release" -G Ninja -DCMAKE_BUILD_TYPE=Release "${launcher[@]}" \
    -DNULLDC_DEVTOOLS_DIST="$dist" -DNULLDC_PREFIX_MAP=ON -DCMAKE_VERBOSE_MAKEFILE=OFF -Wno-dev | tee "$work/release-configure.log"
  ninja -C "$work/release"
  local version arch_
  version=$(cat "$work/release/nulldc3-version.txt")
  arch_=$(arch)
  grep -q '^-- Dreamcast DevTools: embedding ' "$work/release-configure.log" || die "the release build embeds no UI (no line \"Dreamcast DevTools: embedding\" in its configure output)"
  grep '^-- Dreamcast DevTools: ' "$work/release-configure.log" | sed 's/^-- //' > "$out/nulldc3-$version-linux-$arch_.configure.txt"
  echo "nulldc3 $version, linux-$arch_: $(grep 'embedding' "$out/nulldc3-$version-linux-$arch_.configure.txt")"
  summary "nullDC \`$version\` linux-$arch_: $(grep 'embedding' "$out/nulldc3-$version-linux-$arch_.configure.txt" | sed 's/^Dreamcast DevTools: //')"

  # the emulator without the TA of lxdream, for the system tests' system.hle.* (no UI: it ships nowhere)
  cmake -S "$tree" -B "$work/hle" -G Ninja -DCMAKE_BUILD_TYPE=Release "${launcher[@]}" \
    -DHAS_TA_LLE=OFF -DNULLDC_DEVTOOLS_DIST=OFF -DNULLDC_PREFIX_MAP=ON -DCMAKE_VERBOSE_MAKEFILE=OFF -Wno-dev > "$work/hle-configure.log"
  ninja -C "$work/hle"
}

cmd_test() {
  local mode=${1:-}
  shift || true
  case $mode in ci|full) ;; *) usage ;; esac
  local ui="" work="" data="" npm_ci=yes emsdk=yes
  local extra=()
  while [ $# -gt 0 ]; do
    case $1 in
      --ui) ui=$2; shift 2 ;;
      --work) work=$2; shift 2 ;;
      --data) data=$2; shift 2 ;;
      --no-npm-ci) npm_ci=no; shift ;;
      --no-emsdk) emsdk=no; shift ;;
      --) shift; extra=("$@"); break ;;
      *) usage ;;
    esac
  done
  [ -n "$ui" ] || usage
  [ -n "$work" ] || work=$(default_work)
  ui=$(cd "$ui" && pwd)
  check_ui "$ui"
  [ -x "$work/release/nulldc3" ] && [ -x "$work/hle/nulldc3" ] || die "no emulators in $work/release and $work/hle: linux.sh build first"
  if [ $npm_ci = yes ]; then
    (cd "$ui" && npm ci)
  else
    [ -d "$ui/node_modules" ] || die "--no-npm-ci: the UI checkout $ui has no node_modules"
  fi

  local args=(
    -DNULLDC_EMULATOR="$work/release/nulldc3" -DNULLDC_EMULATOR_TA_HLE="$work/hle/nulldc3"
    -DNULLDC_SH4_TESTS_DIR="$tree/tests/vendor/sh4-tests" -DNULLDC_ARM7_TESTS_DIR="$tree/tests/vendor/arm7di-tests-dreamcast"
    -DNULLDC_DEVTOOLS_DIR="$ui"
  )
  if [ "$mode" = full ]; then
    [ -n "$data" ] || die "full: --data <dir> with elf/, wren7-rtl/, caique-rtl/ and shrike4-rtl/"
    data=$(cd "$data" && pwd)
    args+=(-DNULLDC_ELF_DIR="$data/elf" -DNULLDC_WREN7_DIR="$data/wren7-rtl" -DNULLDC_CAIQUE_DIR="$data/caique-rtl"
           -DNULLDC_SHRIKE4_DIR="$data/shrike4-rtl" -DNULLDC_TESTS_REQUIRE_DATA=ON)
  else
    # no checkout beside the tree is taken for data: those tests skip, each with its reason
    args+=(-DNULLDC_ELF_DIR= -DNULLDC_WREN7_DIR= -DNULLDC_CAIQUE_DIR= -DNULLDC_SHRIKE4_DIR= -DNULLDC_TESTS_REQUIRE_DATA=OFF)
  fi
  local launcher=()
  if command -v ccache > /dev/null 2>&1; then
    launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
  fi
  if [ $emsdk = no ]; then
    args+=(-DNULLDC_EMCC=OFF)
  fi
  cmake -S "$tree/tests" -B "$work/tests" -G Ninja -Wno-dev "${launcher[@]}" "${args[@]}" ${extra[@]+"${extra[@]}"}
  ninja -C "$work/tests"
  local rc=0
  if [ $emsdk = no ]; then
    EMSDK=$work/no-emsdk ctest --test-dir "$work/tests" -j4 --output-on-failure --output-junit "$work/tests/junit.xml" || rc=$?
  else
    ctest --test-dir "$work/tests" -j4 --output-on-failure --output-junit "$work/tests/junit.xml" || rc=$?
  fi
  python3 -B "$here/junit.py" "$work/tests/junit.xml" --title "Tests, linux-$(arch), $mode mode" ${GITHUB_STEP_SUMMARY:+--summary "$GITHUB_STEP_SUMMARY"}
  return $rc
}

[ $# -ge 1 ] || usage
command=$1
shift
case $command in
  deps) cmd_deps "$@" ;;
  ui) cmd_ui "$@" ;;
  build) cmd_build "$@" ;;
  test) cmd_test "$@" ;;
  *) usage ;;
esac
