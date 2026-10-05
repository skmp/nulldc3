#!/bin/sh
#
#	The AICA DSP core as a WebAssembly module.
#
#	    build.sh <out dir>             writes <out dir>/aica_dsp_core.wasm and <out dir>/aica_dsp_core.mjs
#	    build.sh --check <out dir>     builds twice and fails unless both builds are the same bytes and have no path
#	                                   of this machine in them
#
#	Needs emcc: on the PATH, or an emsdk in $EMSDK or ~/emsdk.
#
#	The module is one .wasm without any JavaScript of emscripten's: aica_dsp_core.mjs, which is written by hand, is
#	all that loads it. It imports one function (emscripten_notify_memory_growth, which may do nothing) and exports
#	its memory, _initialize and the functions of aica_dsp_capi.h.
#	The sources are compiled by their names relative to this directory, without debug information: nothing of the
#	machine that builds gets into the output.
#
set -e

check=0

if [ "$1" = "--check" ]; then
	check=1
	shift
fi

if [ -z "$1" ]; then
	echo "usage: build.sh [--check] <out dir>" >&2
	exit 2
fi

mkdir -p "$1"
out=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)

# The emcc of an emsdk is in its upstream/emscripten, and runs from there with nothing else set. emsdk_env.sh is
# not read: a script that sh reads cannot tell where it is, and it needs to
if ! command -v emcc >/dev/null 2>&1; then
	PATH="${EMSDK:-$HOME/emsdk}/upstream/emscripten:$PATH"
	export PATH

	if ! command -v emcc >/dev/null 2>&1; then
		echo "no emcc on the PATH and no emsdk in ${EMSDK:-$HOME/emsdk}" >&2
		exit 3
	fi
fi

build()
{
	(
		cd "$here"
		emcc -O2 -std=c++20 -fno-exceptions -fno-rtti \
			--no-entry -sSTANDALONE_WASM=1 -sFILESYSTEM=0 -sMALLOC=emmalloc \
			-sINITIAL_MEMORY=4194304 -sALLOW_MEMORY_GROWTH=1 \
			../aica_dsp_core.cpp aica_dsp_capi.cpp \
			-o "$1/aica_dsp_core.wasm"
	)

	cp "$here/aica_dsp_core.mjs" "$1/aica_dsp_core.mjs"
}

build "$out"

if [ $check = 1 ]; then
	again="$out/again"

	mkdir -p "$again"
	build "$again"

	cmp "$out/aica_dsp_core.wasm" "$again/aica_dsp_core.wasm"
	cmp "$out/aica_dsp_core.mjs" "$again/aica_dsp_core.mjs"
	rm -r "$again"

	for word in "$HOME" "$here" "$out" /home/ /Users/ /tmp/ emsdk; do
		if grep -a -q -F "$word" "$out/aica_dsp_core.wasm"; then
			echo "aica_dsp_core.wasm has \"$word\" in it" >&2
			exit 4
		fi
	done

	echo "reproducible: two builds are the same bytes, no path in them"
fi

echo "aica_dsp_core.wasm $(wc -c < "$out/aica_dsp_core.wasm") bytes, aica_dsp_core.mjs $(wc -c < "$out/aica_dsp_core.mjs") bytes"
