#!/bin/bash
# appimage.sh: the AppImage of a release build (PL-D51, PL-D81), then its smoke run (PL-D76).
#
#   appimage.sh --build <release build dir> --dist <dist-native> --ui <ui checkout> [--work <dir>] [--out <dir>]
#               [--tools <dir>]
#
# <build> is linux.sh build's <work>/release (its nulldc3 and nulldc3-version.txt), <dist> the UI bundle it embeds,
# <ui> the UI checkout at the tree's gitlink, whose LICENSE goes in as LICENSE-devtools (the script stops when it is
# missing or the checkout is at another commit). Writes <out>/nulldc3-<version>-linux-<x86_64|arm64>.AppImage.
#
# The AppDir: usr/bin/nulldc3; usr/share/applications/nulldc3.desktop (nulldc3/linux/nulldc3.desktop with
# X-AppImage-Version=<version>); usr/share/icons/hicolor/<N>x<N>/apps/nulldc3.png for N = 16 ... 512 and nulldc3.png
# with .DirIcon at the top, all copied from nulldc3/icon/png/ (PL-D22: nothing is committed twice);
# usr/share/doc/nulldc3/ with LICENSE, LICENSE-GPL, LICENSE-LGPL, LICENSE-shell (nulldc3/LICENSE) and
# LICENSE-devtools (<ui>/LICENSE) (PL-D80). linuxdeploy copies in the libraries that are not on its exclude list
# (libcurl and its TLS stack, libpulse-simple, libevdev), appimagetool makes the AppImage with the static type-2
# runtime given by --runtime-file. The three tools are tagged releases pinned below by version and SHA-256, fetched
# into <tools> before anything is packaged and checked there (a file that is there with the right SHA-256 is taken
# as it is); nothing downloads anything while packaging. They are AppImages themselves and run with
# APPIMAGE_EXTRACT_AND_RUN=1 (no FUSE needed), never from their own directory as the working directory.
#
# The smoke run (smoke.py) of the file made, copied into a directory of its own and started with
# APPIMAGE_EXTRACT_AND_RUN=1 from another working directory: --version, a headless guest run whose files have to be
# beside the AppImage and nowhere in the extraction, and the debugger's describe naming the UI's version. Then the
# AppImage's usr/share/doc/nulldc3/ is extracted (--appimage-extract) in a scratch directory and has to hold exactly
# the five licences; the extraction is removed.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
tree=$(cd "$here/../.." && pwd)

# the pinned tools: <release URL> <SHA-256>, per architecture
LINUXDEPLOY_TAG=1-alpha-20251107-1
APPIMAGETOOL_TAG=1.9.1
RUNTIME_TAG=20251108
pinned() {
  case "$1-$2" in
    linuxdeploy-x86_64) echo "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d" ;;
    linuxdeploy-aarch64) echo "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-aarch64.AppImage 620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff" ;;
    appimagetool-x86_64) echo "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-x86_64.AppImage ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0" ;;
    appimagetool-aarch64) echo "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-aarch64.AppImage f0837e7448a0c1e4e650a93bb3e85802546e60654ef287576f46c71c126a9158" ;;
    runtime-x86_64) echo "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_TAG/runtime-x86_64 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d" ;;
    runtime-aarch64) echo "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_TAG/runtime-aarch64 00cbdfcf917cc6c0ff6d3347d59e0ca1f7f45a6df1a428a0d6d8a78664d87444" ;;
    *) return 1 ;;
  esac
}
ICON_SIZES="16 24 32 48 64 128 256 512"
LICENCES="LICENSE LICENSE-GPL LICENSE-LGPL LICENSE-shell LICENSE-devtools"

die() { echo "appimage.sh: $*" >&2; exit 1; }
usage() { sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed '$d' >&2; exit 2; }

build="" dist="" ui="" work="" out="" tools=""
while [ $# -gt 0 ]; do
  case $1 in
    --build) build=$2; shift 2 ;;
    --dist) dist=$2; shift 2 ;;
    --ui) ui=$2; shift 2 ;;
    --work) work=$2; shift 2 ;;
    --out) out=$2; shift 2 ;;
    --tools) tools=$2; shift 2 ;;
    *) usage ;;
  esac
done
[ -n "$build" ] && [ -n "$dist" ] && [ -n "$ui" ] || usage
if [ -z "$work" ]; then
  [ -n "${RUNNER_TEMP:-}" ] || die "--work <dir> is needed outside a runner"
  work=$RUNNER_TEMP/nulldc3-ci
fi
[ -n "$out" ] || out=$work/out
[ -n "$tools" ] || tools=$work/appimage-tools
build=$(cd "$build" && pwd)
dist=$(cd "$dist" && pwd)
ui=$(cd "$ui" && pwd)
mkdir -p "$work" "$out" "$tools"
work=$(cd "$work" && pwd)
out=$(cd "$out" && pwd)
tools=$(cd "$tools" && pwd)

case "$(uname -m)" in
  x86_64) arch=x86_64; tool_arch=x86_64 ;;
  aarch64|arm64) arch=arm64; tool_arch=aarch64 ;;
  *) die "no AppImage for the processor $(uname -m)" ;;
esac

# the UI checkout: its LICENSE, at the commit the tree pins for devtools (PL-D85)
[ -f "$ui/LICENSE" ] || die "there is no $ui/LICENSE, the UI's licence that the AppImage carries as LICENSE-devtools"
pin=$(git -C "$tree" rev-parse HEAD:devtools 2>/dev/null) || die "the tree $tree pins no commit for devtools (git rev-parse HEAD:devtools)"
ui_head=$(git -C "$ui" rev-parse HEAD 2>/dev/null) || die "the UI checkout $ui is not a git checkout (git -C $ui rev-parse HEAD)"
[ "$ui_head" = "$pin" ] || die "the UI checkout $ui is at $ui_head, the tree pins devtools at $pin: its LICENSE is not the pinned UI's"

[ -x "$build/nulldc3" ] && [ -f "$build/nulldc3-version.txt" ] || die "$build has no nulldc3 and nulldc3-version.txt (linux.sh build)"
version=$(cat "$build/nulldc3-version.txt")
ui_version=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["version"])' "$dist/bundle-info.json")
file=nulldc3-$version-linux-$arch.AppImage
echo "appimage.sh: $file, the UI $ui_version"

# 1. the tools, fetched and checked before anything is packaged
fetch() {
  local name=$1 url sum path got
  read -r url sum <<< "$(pinned "$name" "$tool_arch" || true)"
  [ -n "$sum" ] || die "no pinned $name for $tool_arch"
  path=$tools/$(basename "$url")
  if [ -f "$path" ] && [ "$(sha256sum "$path" | cut -d' ' -f1)" = "$sum" ]; then
    echo "$name: $(basename "$url") ($sum), there already" >&2
  else
    rm -f "$path" "$path.part"
    curl -fsSL --retry 3 -o "$path.part" "$url"
    got=$(sha256sum "$path.part" | cut -d' ' -f1)
    [ "$got" = "$sum" ] || { rm -f "$path.part"; die "$url: SHA-256 $got, pinned $sum"; }
    mv "$path.part" "$path"
    echo "$name: $(basename "$url") fetched ($sum)" >&2
  fi
  chmod +x "$path"
  echo "$path"
}
linuxdeploy=$(fetch linuxdeploy)
appimagetool=$(fetch appimagetool)
runtime=$(fetch runtime)

# 2. the AppDir
appdir=$work/AppDir
rm -rf "$appdir"
mkdir -p "$appdir/usr/bin" "$appdir/usr/share/applications" "$appdir/usr/share/doc/nulldc3"
cp "$build/nulldc3" "$appdir/usr/bin/nulldc3"
desktop=$appdir/usr/share/applications/nulldc3.desktop
cp "$tree/nulldc3/linux/nulldc3.desktop" "$desktop"
echo "X-AppImage-Version=$version" >> "$desktop"
for n in $ICON_SIZES; do
  mkdir -p "$appdir/usr/share/icons/hicolor/${n}x${n}/apps"
  cp "$tree/nulldc3/icon/png/nulldc-$n.png" "$appdir/usr/share/icons/hicolor/${n}x${n}/apps/nulldc3.png"
done
doc=$appdir/usr/share/doc/nulldc3
cp "$tree/LICENSE" "$doc/LICENSE"
cp "$tree/LICENSE-GPL" "$doc/LICENSE-GPL"
cp "$tree/LICENSE-LGPL" "$doc/LICENSE-LGPL"
cp "$tree/nulldc3/LICENSE" "$doc/LICENSE-shell"
cp "$ui/LICENSE" "$doc/LICENSE-devtools"

# 3. the libraries (linuxdeploy: AppRun, the top-level desktop file and icon, the libraries and their rpath; no
# strip, so that the program is the one that was built and tested), then the AppImage (appimagetool, the pinned
# runtime). Both from the work directory, not from the tools' directory.
cd "$work"
APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 "$linuxdeploy" --appdir "$appdir" \
  --executable "$appdir/usr/bin/nulldc3" --desktop-file "$desktop" \
  --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/nulldc3.png"
cp --remove-destination "$tree/nulldc3/icon/png/nulldc-256.png" "$appdir/nulldc3.png"
cp --remove-destination "$tree/nulldc3/icon/png/nulldc-256.png" "$appdir/.DirIcon"
rm -f "$out/$file"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=$tool_arch "$appimagetool" --no-appstream --runtime-file "$runtime" "$appdir" "$out/$file"
[ -f "$out/$file" ] || die "appimagetool made no $out/$file"
chmod +x "$out/$file"
rm -rf "$appdir"
echo "appimage.sh: $out/$file, $(stat -c %s "$out/$file") bytes, sha256 $(sha256sum "$out/$file" | cut -d' ' -f1)"

# 4. the smoke run, of a copy in a directory of its own, from another working directory
smoke=$work/smoke
rm -rf "$smoke"
mkdir -p "$smoke/dir" "$smoke/extract"
cp "$out/$file" "$smoke/dir/$file"
rc=0
python3 -B "$here/smoke.py" --exe "$smoke/dir/$file" --version "$version" --ui-version "$ui_version" \
  --guest "$tree/tests/debugger/guest/dsptest/dsptest.elf" --work "$smoke/run" --beside "$smoke/dir" \
  --env APPIMAGE_EXTRACT_AND_RUN=1 --env NO_CLEANUP=1 --env "TMPDIR=$smoke/run/tmp" || rc=1

# the licences, from an extraction of usr/share/doc/nulldc3 alone, in a scratch directory
(cd "$smoke/extract" && "$smoke/dir/$file" --appimage-extract 'usr/share/doc/nulldc3/*' > /dev/null)
listed=$(cd "$smoke/extract/squashfs-root/usr/share/doc/nulldc3" && ls -A | sort | tr '\n' ' ')
wanted=$(printf '%s\n' $LICENCES | sort | tr '\n' ' ')
if [ "$listed" = "$wanted" ] && cmp -s "$smoke/extract/squashfs-root/usr/share/doc/nulldc3/LICENSE-devtools" "$ui/LICENSE"; then
  echo "PASS usr/share/doc/nulldc3: $listed(LICENSE-devtools is $ui/LICENSE)"
else
  echo "FAIL usr/share/doc/nulldc3: $listed(expected $wanted, LICENSE-devtools the UI's LICENSE)"
  rc=1
fi
rm -rf "$smoke"
exit $rc
