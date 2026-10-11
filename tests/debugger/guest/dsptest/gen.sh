#!/bin/bash
# Makes the committed files of the sounding guest from its two sources:
#
#   arm7.s     -> arm7.bin             the ARM7 program, linked at 0
#   dsptest.s  -> dsptest.elf          the SH4 program, linked at 8C010000, with arm7.bin inside it
#   both       -> dsptest_labels.inc   every label of the two programs as a #define, for the selftests
#   the six    -> SHA256SUMS           the manifest of dsptest.s, arm7.s, gen.sh and the three files above
#
#   gen.sh           writes the four files
#   gen.sh --check   builds them again and compares with the files that are there, byte for byte; writes nothing.
#                    Exit code 0: the same. 1: something differs, or does not assemble. 3: there is no sh-elf
#                    binutils, nothing was compared
#
# The SH4 part needs an sh-elf binutils (SH_ELF_BIN, default the one of KallistiOS). The ARM7 part needs an
# arm-eabi binutils (DC_ARM_BIN, default the one of KallistiOS): GNU as is the reference for arm7.bin. Where
# there is none, clang (--target=armv4t-none-eabi) with ld.lld and llvm-objcopy stands in; arm7.s is written so
# that both give the same bytes (no literal pool, no constant an assembler could encode in two ways). Where
# there is neither, arm7.bin and the ARM7 labels stay as they are: a change of dsptest.s needs no ARM assembler.
# The emulator and every test build and run without any of these tools.
set -e
export LC_ALL=C
cd "$(dirname "$0")"

SH=${SH_ELF_BIN:-/opt/toolchains/dc/sh-elf/bin}
ARM=${DC_ARM_BIN:-/opt/toolchains/dc/arm-eabi/bin}
FILES="dsptest.s arm7.s gen.sh dsptest.elf arm7.bin dsptest_labels.inc"

check=no
if [ "$1" = --check ]; then
	check=yes
elif [ -n "$1" ]; then
	echo "usage: gen.sh [--check]" >&2
	exit 2
fi

if [ ! -x "$SH/sh-elf-as" ]; then
	echo "gen.sh: no sh-elf binutils in $SH (SH_ELF_BIN)" >&2
	[ $check = yes ] && exit 3
	exit 1
fi

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

cat > "$T/arm7.ld" <<'EOF'
SECTIONS
{
	. = 0;
	.text : { *(.text) }
	/DISCARD/ : { *(.comment) *(.ARM.attributes) *(.ARM.exidx*) *(.note*) }
}
EOF

cat > "$T/sh4.ld" <<'EOF'
OUTPUT_FORMAT("elf32-shl")
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); }
SECTIONS
{
	. = 0x8C010000;
	.text : { *(.text) } :text
	/DISCARD/ : { *(.comment) *(.note*) }
}
EOF

# The ARM7 part
cp arm7.s dsptest.s "$T/"
if [ -x "$ARM/arm-eabi-as" ]; then
	arm="GNU as"
	(cd "$T" && "$ARM/arm-eabi-as" -EL -march=armv3 -o arm7.o arm7.s)
	"$ARM/arm-eabi-ld" -EL -e 0 -T "$T/arm7.ld" -o "$T/arm7.elf" "$T/arm7.o"
	"$ARM/arm-eabi-objcopy" -O binary -j .text "$T/arm7.elf" "$T/arm7.bin"
	"$ARM/arm-eabi-nm" "$T/arm7.elf" > "$T/arm7.sym"
elif command -v clang > /dev/null && command -v ld.lld > /dev/null && command -v llvm-objcopy > /dev/null && command -v llvm-nm > /dev/null; then
	arm="clang, standing in for GNU as"
	(cd "$T" && clang --target=armv4t-none-eabi -c -o arm7.o arm7.s)
	ld.lld -e 0 -T "$T/arm7.ld" -o "$T/arm7.elf" "$T/arm7.o"
	llvm-objcopy -O binary -j .text "$T/arm7.elf" "$T/arm7.bin"
	llvm-nm "$T/arm7.elf" > "$T/arm7.sym"
else
	arm=""
	cp arm7.bin "$T/arm7.bin"
fi

# The SH4 part, with the ARM7 image of this build
(cd "$T" && "$SH/sh-elf-as" --little --isa=sh4 -o dsptest.o dsptest.s)
"$SH/sh-elf-ld" -EL -T "$T/sh4.ld" -o "$T/dsptest.elf" "$T/dsptest.o"
"$SH/sh-elf-nm" "$T/dsptest.elf" > "$T/sh4.sym"

# The labels: _start and g_* of the SH4 program, a_* of the ARM7 program, in the order of their addresses
labels() {
	awk '$3 ~ /^(_start$|[ag]_)/ { name = toupper($3); sub(/^_/, "", name); printf "%s #define DSPTEST_%s 0x%s\n", toupper($1), name, toupper($1) }' "$1" \
		| sort | cut -d' ' -f2-
}
{
	echo "// Made by gen.sh from dsptest.s and arm7.s. Not edited by hand."
	echo "// The labels of the sounding guest: of its SH4 program (dsptest.elf, linked at DSPTEST_BASE) and of its ARM7"
	echo "// program (arm7.bin, DSPTEST_ARM7_SIZE bytes at 0 of the wave memory)."
	echo
	echo "#define DSPTEST_BASE 0x8C010000"
	echo "#define DSPTEST_ARM7_SIZE $(stat -c %s "$T/arm7.bin")"
	echo
	labels "$T/sh4.sym"
	echo
	if [ -n "$arm" ]; then
		labels "$T/arm7.sym"
	else
		grep '^#define DSPTEST_A_' dsptest_labels.inc
	fi
} > "$T/dsptest_labels.inc"

if [ $check = yes ]; then
	rc=0
	for f in dsptest.elf arm7.bin dsptest_labels.inc; do
		if ! cmp -s "$T/$f" "$f"; then
			echo "gen.sh --check: $f is not what the sources give"
			rc=1
		fi
	done
	if ! sha256sum $FILES | cmp -s - SHA256SUMS; then
		echo "gen.sh --check: SHA256SUMS is not the manifest of the six files"
		rc=1
	fi
	if [ $rc = 0 ]; then
		echo "gen.sh --check: dsptest.elf, dsptest_labels.inc and SHA256SUMS are what the sources give; arm7.bin: ${arm:-not assembled, there is no ARM assembler}"
	fi
	exit $rc
fi

cp "$T/dsptest.elf" "$T/arm7.bin" "$T/dsptest_labels.inc" .
chmod 644 dsptest.elf arm7.bin dsptest_labels.inc
sha256sum $FILES > SHA256SUMS

echo "dsptest.elf: $(stat -c %s dsptest.elf) bytes; arm7.bin: $(stat -c %s arm7.bin) bytes (${arm:-kept, there is no ARM assembler}); dsptest_labels.inc: $(grep -c '^#define DSPTEST_[AG]_\|^#define DSPTEST_START' dsptest_labels.inc) labels"
