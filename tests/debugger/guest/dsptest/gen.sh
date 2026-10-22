#!/bin/bash
# Makes the committed files of the sounding guest from its three sources:
#
#   dsptest_dsp.txt -> dsptest_dsp.inc       the DSP program, assembled (below): the table entries of its MEMS_H
#                                            lines and the block g_dsp
#   arm7.s          -> arm7.bin              the ARM7 program, linked at 0
#   dsptest.s       -> dsptest.elf           the SH4 program, linked at 8C010000, with dsptest_dsp.inc and
#                                            arm7.bin inside it
#   the two programs -> dsptest_labels.inc   every label of the SH4 and the ARM7 program as a #define, for the
#                                            selftests
#   the eight       -> SHA256SUMS            the manifest of dsptest.s, arm7.s, dsptest_dsp.txt, gen.sh and the
#                                            four files above
#
#   gen.sh           writes the five files
#   gen.sh --check   builds them again and compares with the files that are there, byte for byte; writes nothing.
#                    Exit code 0: the same. 1: something differs, or does not assemble. 3: there is no sh-elf
#                    binutils, nothing was compared
#
# The SH4 part needs an sh-elf binutils (SH_ELF_BIN, default the one of KallistiOS). The ARM7 part needs an
# arm-eabi binutils (DC_ARM_BIN, default the one of KallistiOS): GNU as is the reference for arm7.bin. Where
# there is none, clang (--target=armv4t-none-eabi) with ld.lld and llvm-objcopy stands in; arm7.s is written so
# that both give the same bytes (no literal pool, no constant an assembler could encode in two ways). Where
# there is neither, arm7.bin and the ARM7 labels stay as they are: a change of dsptest.s needs no ARM assembler.
# The DSP part needs awk and nothing else. The emulator and every test build and run without any of these tools.
#
# The DSP program is written in the syntax of the DSP playground of the devtools UI (assembleSource of its
# src/app/dsp/dspUtils.ts): COEF[i] = <the 16 bits of the register>, MADRS[i] = <value>, MEMS_H[i] = <bits 23:8
# of MEMS[i]>, MPRO[n] = <fields>, a value in decimal or 0x hex; and lines that start with # or //. The awk step
# encodes the fields of a step into its four words by the layout of libswirl/hw/aica/dsp_core/aica_dsp_core.h,
# which the playground, the emulator and caique share, and refuses an MPRO line that is not exactly the line the
# emulator's disassembler (DisasmDsp) prints for those words: the fields in its order, the zero ones left out,
# a field of one bit by its name. The steps are given in their order from step 0, none of them empty and step 127
# not among them; a program has COEF, MADRS and MPRO lines (each part of g_dsp is copied by a loop of _start
# that writes at least one word).
set -e
export LC_ALL=C
cd "$(dirname "$0")"

SH=${SH_ELF_BIN:-/opt/toolchains/dc/sh-elf/bin}
ARM=${DC_ARM_BIN:-/opt/toolchains/dc/arm-eabi/bin}
FILES="dsptest.s arm7.s dsptest_dsp.txt gen.sh dsptest.elf arm7.bin dsptest_labels.inc dsptest_dsp.inc"

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

# The size of a file in bytes: GNU stat on Linux, the BSD stat of macOS there (which has no -c)
if [ "$(uname -s)" = Darwin ]; then
	size() { stat -f %z "$1"; }
else
	size() { stat -c %s "$1"; }
fi

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

# The DSP part: dsptest_dsp.txt into the two macros of dsptest_dsp.inc, dsp_table (an entry of the table for each
# MEMS_H line, in their order) and dsp_block (the block g_dsp)
cat > "$T/dsp.awk" <<'EOF'
function fail(what) {
	printf "gen.sh: dsptest_dsp.txt, line %d: %s\n", NR, what > "/dev/stderr"
	failed = 1
	exit 1
}
# a value of the text, decimal or 0x hex with an optional minus, as the 16 bits of a register
function word(text,    neg, v, i) {
	neg = 0
	if (substr(text, 1, 1) == "-") { neg = 1; text = substr(text, 2) }
	if (text ~ /^0[xX][0-9a-fA-F]+$/) {
		v = 0
		for (i = 3; i <= length(text); i++)
			v = v * 16 + index("0123456789abcdef", tolower(substr(text, i, 1))) - 1
	} else if (text ~ /^[0-9]+$/)
		v = text + 0
	else
		fail("\"" text "\" is no number")
	if (neg) v = -v
	v = v % 65536
	if (v < 0) v += 65536
	return v
}
function index_of(text, limit, what,    i) {
	if (text !~ /^[0-9]+$/) fail("the index of " what " is no number")
	i = text + 0
	if (i >= limit) fail(what "[" i "]: there are " limit)
	return i
}
BEGIN {
	# the fields of a step (aica_dsp_core.h), in the order the disassembler of the emulator prints them: name, word,
	# lowest bit, bits
	nf = split("TRA TWT TWA XSEL YSEL IRA IWT IWA TABLE MWT MRD EWT EWA ADRL FRCL SHIFT YRL NEGB ZERO BSEL NOFL MASA ADREB NXADR", name, " ")
	split("0 0 0 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 2 2 3 3 3 3", fword, " ")
	split("9 8 1 15 13 7 6 1 15 14 13 12 8 7 6 4 3 2 1 0 15 9 8 7", fshift, " ")
	split("7 1 7 1 2 6 1 5 1 1 1 1 4 1 1 2 1 1 1 1 1 6 1 1", fbits, " ")
	for (f = 1; f <= nf; f++) field[name[f]] = f
	ncoef = nmadrs = nmpro = nmems = 0
}
{ sub(/^[ \t]+/, ""); sub(/[ \t\r]+$/, "") }
$0 == "" || /^#/ || /^\/\// { next }
/^(COEF|MADRS|MEMS_H)\[[0-9]+\][ \t]*=[ \t]*[^ \t]+$/ {
	kind = $0; sub(/\[.*/, "", kind)
	at = $0; sub(/^[A-Z_]+\[/, "", at); sub(/\].*/, "", at)
	value = $0; sub(/^[^=]*=[ \t]*/, "", value)
	if (kind == "COEF") {
		i = index_of(at, 128, kind)
		if (i in coef) fail(kind "[" i "] twice")
		coef[i] = word(value)
		if (i + 1 > ncoef) ncoef = i + 1
	} else if (kind == "MADRS") {
		i = index_of(at, 64, kind)
		if (i in madrs) fail(kind "[" i "] twice")
		madrs[i] = word(value)
		if (i + 1 > nmadrs) nmadrs = i + 1
	} else {
		i = index_of(at, 32, kind)
		if (i in mems) fail(kind "[" i "] twice")
		mems[i] = word(value)
		mems_order[nmems++] = i
	}
	next
}
/^MPRO\[/ {
	at = $0; sub(/^MPRO\[/, "", at); sub(/\].*/, "", at)
	s = index_of(at, 128, "MPRO")
	if (s != nmpro) fail("MPRO[" s "]: the steps are given in their order from step 0, the next one is " nmpro)
	if (s == 127) fail("MPRO[127]: step 127 stays empty")
	text = $0
	if (!sub(/^MPRO\[[0-9]+\][ \t]*=[ \t]*/, "", text)) fail("no line of the assembler of the playground: " $0)
	w[0] = w[1] = w[2] = w[3] = 0
	for (f = 1; f <= nf; f++) given[f] = 0
	n = split(text, token, " ")
	for (t = 1; t <= n; t++) {
		key = token[t]; value = "1"
		if (index(key, ":")) { value = substr(key, index(key, ":") + 1); key = substr(key, 1, index(key, ":") - 1) }
		if (!(key in field)) fail("MPRO[" s "]: there is no field " key)
		f = field[key]
		if (given[f]) fail("MPRO[" s "]: " key " twice")
		given[f] = 1
		if (value !~ /^[0-9]+$/) fail("MPRO[" s "]: " key ":" value ", the value is no decimal number")
		v = value + 0
		if (v >= 2 ^ fbits[f]) fail("MPRO[" s "]: " key ":" v " does not fit into its " fbits[f] " bits")
		w[fword[f]] += v * 2 ^ fshift[f]
	}
	# the line the disassembler of the emulator prints for the four words
	line = "MPRO[" s "] ="
	for (f = 1; f <= nf; f++) {
		v = int(w[fword[f]] / 2 ^ fshift[f]) % 2 ^ fbits[f]
		if (v == 0) continue
		line = line " " name[f]
		if (fbits[f] != 1) line = line ":" v
	}
	if (line == "MPRO[" s "] =") fail("MPRO[" s "]: an empty step inside the program")
	if (line != $0) fail("the disassembler of the emulator prints this step as \"" line "\"")
	for (k = 0; k < 4; k++) mpro[s, k] = w[k]
	mtext[s] = $0
	nmpro++
	next
}
{ fail("no line of the assembler of the playground: " $0) }
END {
	if (failed) exit 1
	if (!ncoef || !nmadrs || !nmpro) fail("the program has no COEF, MADRS or MPRO lines")
	print "! Made by gen.sh from dsptest_dsp.txt. Not edited by hand."
	print "! The DSP program of the sounding guest, for dsptest.s. dsp_table: an entry of the table that _start writes"
	print "! for each MEMS_H line, (the address of bits 23:8 of MEMS[i], the value). dsp_block: the block g_dsp, COEF,"
	print "! MADRS and MPRO as the register area of the DSP holds them, one .long for each 16-bit word (the word in its"
	print "! low half), MPRO four words a step from step 0; the steps behind it are empty."
	print ""
	print "\t.macro\tdsp_table"
	for (k = 0; k < nmems; k++) {
		i = mems_order[k]
		printf "\t.long\t0xA070%04X, 0x0000%04X\t! MEMS_H[%d] = 0x%04X\n", 17412 + 8 * i, mems[i], i, mems[i]
	}
	print "\t.endm"
	print ""
	print "\t.macro\tdsp_block"
	print "\t.align\t2"
	print "\t.type\tg_dsp, @object"
	print "g_dsp:"
	print "g_dsp_coef:"
	for (i = 0; i < ncoef; i++)
		printf "\t.long\t0x0000%04X\t! COEF[%d]\n", (i in coef) ? coef[i] : 0, i
	print "g_dsp_coef_end:"
	print "g_dsp_madrs:"
	for (i = 0; i < nmadrs; i++)
		printf "\t.long\t0x0000%04X\t! MADRS[%d]\n", (i in madrs) ? madrs[i] : 0, i
	print "g_dsp_madrs_end:"
	print "g_dsp_mpro:"
	for (s = 0; s < nmpro; s++)
		printf "\t.long\t0x0000%04X, 0x0000%04X, 0x0000%04X, 0x0000%04X\t! %s\n", mpro[s, 0], mpro[s, 1], mpro[s, 2], mpro[s, 3], mtext[s]
	print "g_dsp_mpro_end:"
	print "g_dsp_end:"
	print "\t.size\tg_dsp, .-g_dsp"
	print "\t.endm"
}
EOF
awk -f "$T/dsp.awk" dsptest_dsp.txt > "$T/dsptest_dsp.inc"

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

# The SH4 part, with the ARM7 image and the DSP program of this build
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
	echo "#define DSPTEST_ARM7_SIZE $(size "$T/arm7.bin")"
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
	for f in dsptest_dsp.inc dsptest.elf arm7.bin dsptest_labels.inc; do
		if ! cmp -s "$T/$f" "$f"; then
			echo "gen.sh --check: $f is not what the sources give"
			rc=1
		fi
	done
	if ! sha256sum $FILES | cmp -s - SHA256SUMS; then
		echo "gen.sh --check: SHA256SUMS is not the manifest of the eight files"
		rc=1
	fi
	if [ $rc = 0 ]; then
		echo "gen.sh --check: dsptest_dsp.inc, dsptest.elf, dsptest_labels.inc and SHA256SUMS are what the sources give; arm7.bin: ${arm:-not assembled, there is no ARM assembler}"
	fi
	exit $rc
fi

cp "$T/dsptest_dsp.inc" "$T/dsptest.elf" "$T/arm7.bin" "$T/dsptest_labels.inc" .
chmod 644 dsptest_dsp.inc dsptest.elf arm7.bin dsptest_labels.inc
sha256sum $FILES > SHA256SUMS

echo "dsptest.elf: $(size dsptest.elf) bytes; arm7.bin: $(size arm7.bin) bytes (${arm:-kept, there is no ARM assembler}); dsptest_labels.inc: $(grep -c '^#define DSPTEST_[AG]_\|^#define DSPTEST_START' dsptest_labels.inc) labels; dsptest_dsp.inc: $(grep -c '! MPRO\[' dsptest_dsp.inc) steps"
