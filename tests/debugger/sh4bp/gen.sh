#!/bin/bash
# Makes libswirl/debugger/dbg_selftest_sh4bp_guest.inc from guest.s: the image as bytes and the addresses of its
# g_* labels. The file stands next to the selftest of the emulator, which includes it as these tests do.
# Needs an sh-elf binutils (KallistiOS: /opt/toolchains/dc/sh-elf/bin); the emulator and the tests build without
# it, from the file.
set -e
cd "$(dirname "$0")"
OUT=../../../libswirl/debugger/dbg_selftest_sh4bp_guest.inc
BIN=${SH_ELF_BIN:-/opt/toolchains/dc/sh-elf/bin}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

"$BIN/sh-elf-as" --little --isa=sh4 -o "$T/guest.o" guest.s
"$BIN/sh-elf-ld" -EL -Ttext=0x8C010000 -e _start -o "$T/guest.elf" "$T/guest.o"
"$BIN/sh-elf-objcopy" -O binary -j .text "$T/guest.elf" "$T/guest.bin"

{
	echo "// The guest programs of the sh4bp selftest and of the tests in tests/debugger/sh4bp, assembled."
	echo "// Made by tests/debugger/sh4bp/gen.sh from guest.s. Not edited by hand."
	echo
	echo "#define G_BASE 0x8C010000"
	"$BIN/sh-elf-nm" "$T/guest.elf" | awk '$3 ~ /^g_/ { printf "#define G_%s 0x%s\n", substr($3, 3), toupper($1) }' | sort
	echo
	echo "static const unsigned char guest_image[] = {"
	od -An -v -tx1 -w16 "$T/guest.bin" | sed -e 's/^ */\t/' -e 's/\([0-9a-f][0-9a-f]\)/0x\1,/g'
	echo "};"
} > "$OUT"

echo "$OUT: $(stat -c %s "$T/guest.bin") bytes of image, $(grep -c '^#define G_' "$OUT") names"
