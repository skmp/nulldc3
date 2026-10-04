#!/usr/bin/env python3
"""
Compares dbg::DisasmSh4 with GNU objdump (-m sh4) over all 65536 opcodes.

The opcodes are laid out as a little-endian binary image at BASE; both sides resolve PC-relative
literals against that image. Opcode n sits at BASE + 2n, so the comparison is repeated at BASE + 2 to
see every opcode at both alignments of pc. See README.md for the normalisation rules and the explained
differences.

Exit status: 0 no unexplained difference, 1 unexplained differences, 77 oracle not available.
"""
import argparse
import os
import re
import struct
import subprocess
import sys

BASE = 0x8C010000
SKIP = 77

# FPU instructions whose register operands become DRn when FPSCR.PR = 1
PR_ARITH = {'fadd', 'fsub', 'fmul', 'fdiv', 'fcmp/eq', 'fcmp/gt', 'fabs', 'fneg', 'fsqrt', 'float', 'ftrc'}
BRANCH = {'bra', 'bsr', 'bt', 'bf', 'bt.s', 'bf.s'}

# Info flags by objdump mnemonic, in the order of the signature: branch, delayed, call, return, conditional, trap
FLAGS = {
    'bra': 'bd----', 'braf': 'bd----', 'jmp': 'bd----',
    'bsr': 'bdc---', 'bsrf': 'bdc---', 'jsr': 'bdc---',
    'rts': 'bd-r--', 'rte': 'bd-r--',
    'bt': 'b---?-', 'bf': 'b---?-', 'bt.s': 'bd--?-', 'bf.s': 'bd--?-',
    'trapa': '-----t',
}


def squeeze(text):
    return ' '.join(text.split())


def run_objdump(objdump, workdir, base):
    image = os.path.join(workdir, 'sh4_all.bin')
    with open(image, 'wb') as f:
        f.write(b''.join(struct.pack('<H', op) for op in range(0x10000)))
    out = subprocess.run([objdump, '-D', '-b', 'binary', '-m', 'sh4', '-EL', '--adjust-vma=0x%x' % base, image],
                         check=True, capture_output=True, text=True).stdout
    result = {}
    for line in out.splitlines():
        m = re.match(r'^([0-9a-f]+):\t([0-9a-f]{2}) ([0-9a-f]{2}) +\t(.*)$', line)
        if not m:
            continue
        op = int(m.group(3) + m.group(2), 16)
        assert int(m.group(1), 16) == base + op * 2, line
        text, _, comment = m.group(4).partition('!')
        result[op] = (squeeze(text), comment.strip())
    assert len(result) == 0x10000, len(result)
    return result


def run_ours(test_bin, mode, base):
    out = subprocess.run([test_bin, 'sh4', str(mode), '0x%x' % base], check=True, capture_output=True, text=True).stdout
    result = {}
    for line in out.splitlines():
        op, sig, text = line.split('\t')
        flags, *rest = sig.split(' ')
        info = {'flags': flags}
        for item in rest:
            key, _, value = item.partition('=')
            info[key] = value
        text, _, comment = text.partition(' ! ')
        result[int(op, 16)] = (squeeze(text), comment.strip(), info)
    assert len(result) == 0x10000, len(result)
    return result


def expect_for_mode(text, mode):
    """objdump text rewritten by the header's FPSCR rules. objdump itself knows no FPSCR state."""
    mnemonic, _, operands = text.partition(' ')
    if mode & 1 and mnemonic in PR_ARITH:
        operands = re.sub(r'\bfr(\d+)', lambda m: 'dr%d' % (int(m.group(1)) & 14), operands)
    if mnemonic == 'fmov':
        if mode & 2:
            operands = re.sub(r'\bfr(\d+)',
                              lambda m: ('xd%d' if int(m.group(1)) & 1 else 'dr%d') % (int(m.group(1)) & 14), operands)
        elif '@' in operands:
            mnemonic = 'fmov.s'
    return (mnemonic + ' ' + operands).strip()


def explained(op, obj_text, our_text):
    """Returns the class of a known, documented difference, or None."""
    if obj_text.startswith('.word') and (op & 0xF1EF) == 0xF1AD:
        return 'fcnvsd / fcnvds with bit 8 set'
    return None


def compare(obj, ours, mode):
    counts = {'identical': 0, 'normalised': 0, 'unexplained': 0}
    rules = {}
    classes = {}
    checked = {'flag sets': 0, 'branch targets': 0, 'literal addresses': 0, 'literal values': 0}
    bad = []

    def rule(name):
        rules[name] = rules.get(name, 0) + 1

    for op in range(0x10000):
        obj_text, obj_comment = obj[op]
        our_text, our_comment, info = ours[op]
        obj_valid = not obj_text.startswith('.word')
        our_valid = info['flags'][0] == 'v'
        used = []

        if obj_valid != our_valid or (not obj_valid and obj_text != our_text):
            cls = explained(op, obj_text, our_text)
            if cls:
                classes[cls] = classes.get(cls, 0) + 1
            else:
                counts['unexplained'] += 1
                bad.append((op, obj_text, our_text, 'classification'))
            continue

        if obj_valid:
            want = expect_for_mode(obj_text, mode)
            if want != obj_text:
                used.append('FPSCR register / fmov.s naming')
            if want != our_text:
                counts['unexplained'] += 1
                bad.append((op, obj_text, our_text, 'text'))
                continue

            mnemonic = our_text.split(' ')[0]
            address = re.search(r'0x([0-9a-f]+)', obj_text)
            ok = info['flags'] == 'v' + FLAGS.get(obj_text.split(' ')[0], '------')
            checked['flag sets'] += 1
            if mnemonic in BRANCH:
                ok = info.get('t') is not None and int(info['t'], 16) == int(address.group(1), 16)
                checked['branch targets'] += 1
            elif 't' in info:
                ok = False
            if address and mnemonic not in BRANCH:
                size = {'mov.w': 2, 'mov.l': 4, 'mova': 0}[mnemonic]
                ok = ok and info.get('l') == '%08x/%d' % (int(address.group(1), 16), size)
                checked['literal addresses'] += 1
            elif 'l' in info:
                ok = False

            # objdump prints the raw literal, 4 or 8 hex digits; ours is the value the register receives
            if obj_comment or our_comment:
                if not obj_comment or not our_comment:
                    ok = False
                else:
                    raw = int(obj_comment, 16)
                    if mnemonic == 'mov.w':
                        raw = raw - 0x10000 if raw & 0x8000 else raw
                    ok = ok and our_comment == '0x%08x' % (raw & 0xFFFFFFFF)
                    checked['literal values'] += 1
                    used.append('literal value comment')
            if not ok:
                counts['unexplained'] += 1
                bad.append((op, obj_text + ' ! ' + obj_comment, our_text + ' ! ' + our_comment + ' ' + str(info),
                            'flags / target / literal'))
                continue

        if used:
            counts['normalised'] += 1
            for name in used:
                rule(name)
        else:
            counts['identical'] += 1

    return counts, rules, classes, checked, bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--objdump', default='')
    ap.add_argument('--test-bin', required=True)
    ap.add_argument('--workdir', required=True)
    args = ap.parse_args()

    if not args.objdump or args.objdump.endswith('-NOTFOUND') or not os.path.exists(args.objdump):
        print('SKIP: no SuperH objdump (sh-elf-objdump); set -DSH_OBJDUMP=<path>')
        return SKIP

    os.makedirs(args.workdir, exist_ok=True)
    version = subprocess.run([args.objdump, '--version'], capture_output=True, text=True).stdout.splitlines()[0]
    status = 0

    for mode, base in ((0, BASE), (0, BASE + 2), (1, BASE), (2, BASE), (3, BASE)):
        obj = run_objdump(args.objdump, args.workdir, base)
        counts, rules, classes, checked, bad = compare(obj, run_ours(args.test_bin, mode, base), mode)
        name = 'PR=%d SZ=%d at 0x%08x' % (mode & 1, (mode >> 1) & 1, base)
        print('sh4 %s vs %s%s' % (name, version, '' if mode == 0 else ' rewritten by the FPSCR rules'))
        print('  encodings compared    65536')
        print('  identical             %d' % counts['identical'])
        print('  normalised-equal      %d' % counts['normalised'])
        for rule_name in sorted(rules):
            print('    %-34s %d' % (rule_name, rules[rule_name]))
        print('  explained differences %d' % sum(classes.values()))
        for cls in sorted(classes):
            print('    %-34s %d' % (cls, classes[cls]))
        print('  unexplained           %d' % counts['unexplained'])
        print('  checked: ' + ', '.join('%d %s' % (checked[k], k) for k in checked))
        for op, obj_text, our_text, why in bad[:20]:
            print('  UNEXPLAINED %04x (%s): objdump "%s" ours "%s"' % (op, why, obj_text, our_text))
        if counts['unexplained']:
            status = 1

    return status


if __name__ == '__main__':
    sys.exit(main())
