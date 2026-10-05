#!/usr/bin/env python3
"""
Cross-checks dbg::DisasmSh4 against the interpreter's opcode table, libswirl/hw/sh4/sh4_opcode_list.cpp,
which is what the emulator executes. The table is only read: its entries are extracted with a regular
expression and expanded the way BuildOpcodeTables() does.

Checked for every one of the 65536 opcodes:
  - valid <=> the table has a handler (the HLE BIOS trap excepted)
  - is_branch / is_delayed_branch <=> the table's opcode type (Branch_*, *_d)
  - the mnemonic is the first word of the table's format string (documented exceptions below)

Exit status: 0 agreement, 1 differences, 77 opcode table not found.
"""
import argparse
import os
import re
import subprocess
import sys

SKIP = 77
REIOS_OPCODE = 0x085B

# (entries, shift) per mask, as in BuildOpcodeTables()
EXPANSION = {
    'Mask_none': (1, 0), 'Mask_n': (16, 8), 'Mask_n_m': (256, 4), 'Mask_n_m_imm4': (4096, 0),
    'Mask_imm8': (256, 0), 'Mask_n_ml3bit': (256, 4), 'Mask_nh3bit': (8, 9), 'Mask_nh2bit': (4, 10),
    'Mask_n_imm8': (4096, 0), 'Mask_imm12': (4096, 0),
}

# Table format strings whose first word is not the mnemonic the SH-4 manual and objdump use.
# (mask, bits) of the opcode -> our mnemonic
MNEMONIC_EXCEPTIONS = {
    (0xF08F, 0x4083): 'stc.l',   # table: "stc <RM_BANK>,@-<REG_N>", the handler pushes: stc.l
    (0xF0FF, 0x003A): 'stc',     # table: "sts SGR,<REG_N>"; SGR is a control register
    (0xF0FF, 0x00FA): 'stc',     # table: "sts DBR,<REG_N>"; DBR is a control register
}


def extract(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    masks = {name: int(value, 16) for name, value in re.findall(r'#define (Mask_\w+) (0x[0-9A-Fa-f]+)', src)}
    body = src[src.index('sh4_opcodelistentry opcodes[]='):src.index('void BuildOpcodeTables')]
    table = {}
    entries = 0
    for line in body.split('\n'):
        line = line.strip()
        if not line.startswith('{'):
            continue
        m = re.match(r'\{\s*([^,]*?)\s*,\s*(\w+)\s*,\s*(Mask_\w+)\s*,\s*(0x[0-9A-Fa-f]+|REIOS_OPCODE)\s*,'
                     r'\s*([^,]*?)\s*,\s*"([^"]*)"', line)
        if not m:
            # the opening brace and the all-zero terminator
            assert line == '{' or line.startswith('{0,0,0,0,'), line
            continue
        _, _, mask, rez, optype, diss = m.groups()
        rez = REIOS_OPCODE if rez == 'REIOS_OPCODE' else int(rez, 16)
        count, shift = EXPANSION[mask]
        entries += 1
        for i in range(count):
            table[((i << shift) & ~masks[mask] & 0xFFFF) + rez] = (optype, diss)
    return table, entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--opcode-list', required=True)
    ap.add_argument('--test-bin', required=True)
    args = ap.parse_args()

    if not os.path.exists(args.opcode_list):
        print('SKIP: %s not found' % args.opcode_list)
        return SKIP

    table, entries = extract(args.opcode_list)
    out = subprocess.run([args.test_bin, 'sh4', '0', '0x8c010000'], check=True, capture_output=True, text=True).stdout
    ours = {}
    for line in out.splitlines():
        op, sig, text = line.split('\t')
        ours[int(op, 16)] = (sig.split(' ')[0], text)

    bad = []
    hle = 0
    mnemonic_exceptions = 0
    for op in range(0x10000):
        flags, text = ours[op]
        valid = flags[0] == 'v'
        if op == REIOS_OPCODE:
            # executed by the emulator as the HLE BIOS trap; not an SH-4 instruction
            if valid or op not in table:
                bad.append((op, 'HLE trap', text))
            hle += 1
            continue
        if valid != (op in table):
            bad.append((op, 'validity: table %s' % ('has it' if op in table else 'does not have it'), text))
            continue
        if not valid:
            continue

        optype, diss = table[op]
        branch = 'Branch_' in optype
        delayed = branch and optype.split('|')[0].strip().endswith('_d')
        if (flags[1] == 'b') != branch or (flags[2] == 'd') != delayed:
            bad.append((op, 'branch flags: table type %s' % optype, flags + ' ' + text))

        want = diss.split(' ')[0].lower()
        for (mask, bits), mnemonic in MNEMONIC_EXCEPTIONS.items():
            if (op & mask) == bits:
                want = mnemonic
                mnemonic_exceptions += 1
        if text.split(' ')[0] != want:
            bad.append((op, 'mnemonic: table "%s"' % diss, text))

    print('sh4 vs %s' % os.path.basename(args.opcode_list))
    print('  table entries %d, opcodes with a handler %d' % (entries, len(table)))
    print('  valid in both           %d' % (len(table) - hle))
    print('  invalid in both         %d' % (0x10000 - len(table)))
    print('  explained differences   %d' % hle)
    print('    HLE BIOS trap 0x%04x   %d' % (REIOS_OPCODE, hle))
    print('  mnemonic exceptions     %d opcodes (stc.l Rm_BANK, stc SGR, stc DBR)' % mnemonic_exceptions)
    print('  unexplained             %d' % len(bad))
    for op, why, text in bad[:20]:
        print('  UNEXPLAINED %04x (%s): ours "%s"' % (op, why, text))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
