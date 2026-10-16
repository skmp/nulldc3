#!/bin/bash
# Makes emu_guest.inc and emu_guest.bin from emu_guest.s: the image as bytes with the addresses of its g_*
# labels, and the image as the file a run boots.
# Needs an sh-elf binutils (KallistiOS: /opt/toolchains/dc/sh-elf/bin); the emulator and the tests build without
# it, from the two files it made.
set -e
cd "$(dirname "$0")"
BIN=${SH_ELF_BIN:-/opt/toolchains/dc/sh-elf/bin}
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

"$BIN/sh-elf-as" --little --isa=sh4 -o "$T/guest.o" emu_guest.s
"$BIN/sh-elf-ld" -EL -Ttext=0x8C010000 -e _start -o "$T/guest.elf" "$T/guest.o"
"$BIN/sh-elf-objcopy" -O binary -j .text "$T/guest.elf" emu_guest.bin

{
	echo "// Made by gen.sh from emu_guest.s. Not edited by hand."
	echo
	echo "#define EMU_GUEST_BASE 0x8C010000"
	"$BIN/sh-elf-nm" "$T/guest.elf" | awk '$3 ~ /^g_/ { printf "#define EMU_G_%s 0x%s\n", substr($3, 3), toupper($1) }' | sort
	echo
	echo "static const unsigned char emu_guest_image[] = {"
	od -An -v -tx1 -w16 emu_guest.bin | sed -e 's/^ */\t/' -e 's/\([0-9a-f][0-9a-f]\)/0x\1,/g'
	echo "};"
} > emu_guest.inc

echo "emu_guest.inc, emu_guest.bin: $(stat -c %s emu_guest.bin) bytes of image, $(grep -c '^#define EMU_G_' emu_guest.inc) names"
