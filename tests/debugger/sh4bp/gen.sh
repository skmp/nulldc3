#!/bin/bash
# Makes guest.inc from guest.s: the image as bytes and the addresses of its g_* labels.
# Needs an sh-elf binutils (KallistiOS: /opt/toolchains/dc/sh-elf/bin); the tests build without it, from guest.inc.
set -e
cd "$(dirname "$0")"
BIN=${SH_ELF_BIN:-/opt/toolchains/dc/sh-elf/bin}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

"$BIN/sh-elf-as" --little --isa=sh4 -o "$T/guest.o" guest.s
"$BIN/sh-elf-ld" -EL -Ttext=0x8C010000 -e _start -o "$T/guest.elf" "$T/guest.o"
"$BIN/sh-elf-objcopy" -O binary -j .text "$T/guest.elf" "$T/guest.bin"

{
	echo "// Made by gen.sh from guest.s. Not edited by hand."
	echo
	echo "#define G_BASE 0x8C010000"
	"$BIN/sh-elf-nm" "$T/guest.elf" | awk '$3 ~ /^g_/ { printf "#define G_%s 0x%s\n", substr($3, 3), toupper($1) }' | sort
	echo
	echo "static const unsigned char guest_image[] = {"
	od -An -v -tx1 -w16 "$T/guest.bin" | sed -e 's/^ */\t/' -e 's/\([0-9a-f][0-9a-f]\)/0x\1,/g'
	echo "};"
} > guest.inc

echo "guest.inc: $(stat -c %s "$T/guest.bin") bytes of image, $(grep -c '^#define G_' guest.inc) names"
