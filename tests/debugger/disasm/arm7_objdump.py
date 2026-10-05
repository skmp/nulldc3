#!/usr/bin/env python3
"""
Compares dbg::DisasmArm7 with GNU objdump (-m armv3) over a structured + random sample of encodings.

The sample is laid out as a little-endian binary image at BASE. objdump's text is rewritten into the
canonical forms of dbg_disasm.h (see README.md for the rules); every remaining difference has to fall
into one of the explained classes below.

--self-test checks the comparison instead of the disassembler: every info field it counts as checked is made
wrong or removed in the dump, or added where there is none, and each change has to be reported.

Exit status: 0 no unexplained difference (--self-test: every change reported), 1 otherwise,
77 oracle not available.
"""
import argparse
import os
import random
import re
import struct
import subprocess
import sys

BASE = 0x00100000
SKIP = 77
SEED = 0x41524D37

COND = ['eq', 'ne', 'cs', 'cc', 'mi', 'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le', '', 'nv']
DP = ['and', 'eor', 'sub', 'rsb', 'add', 'adc', 'sbc', 'rsc', 'tst', 'teq', 'cmp', 'cmn', 'orr', 'mov', 'bic', 'mvn']
SHIFT_ALIAS = ('lsl', 'lsr', 'asr', 'ror')

# Flag letters of the info signature: valid, branch, delayed, call, return, conditional, trap
SIGNATURE = 'vbdcr?t'


def classify(op):
    """Instruction class of an encoding on the ARM7DI, from the ARM7DI data sheet's decode."""
    top = (op >> 25) & 7
    if top == 0 and (op & 0x90) == 0x90:
        if (op & 0x0FC000F0) == 0x00000090:
            return 'mul'
        if (op & 0x0FB00FF0) == 0x01000090:
            return 'swp'
        return 'mulswp-other'
    if top in (0, 1):
        if (op & 0x01900000) == 0x01000000:
            if not op & (1 << 21):
                return 'mrs' if (op & 0x0FBF0FFF) == 0x010F0000 else 'psr-other'
            if op & (1 << 25):
                return 'msr' if (op & 0x0FB0F000) == 0x0320F000 else 'psr-other'
            return 'msr' if (op & 0x0FB0FFF0) == 0x0120F000 else 'psr-other'
        return 'dp'
    if top == 3 and op & 0x10:
        return 'undefined'
    if top in (2, 3):
        return 'ldr-str'
    if top == 4:
        return 'ldm-stm'
    if top == 5:
        return 'b-bl'
    if top == 6:
        return 'ldc-stc'
    if op & (1 << 24):
        return 'swi'
    return 'mrc-mcr' if op & 0x10 else 'cdp'


def sample():
    """Deterministic sample: every class exhaustively over its mode bits, plus uniform random words."""
    rnd = random.Random(SEED)
    ops = []
    regs = [0, 1, 7, 12, 13, 14, 15]

    def cond():
        # AL half of the time, NV included
        return 14 if rnd.random() < 0.5 else rnd.randrange(16)

    def add(body, repeat=1):
        for _ in range(repeat):
            ops.append((cond() << 28) | (body & 0x0FFFFFFF))

    def reg():
        return rnd.choice(regs) if rnd.random() < 0.5 else rnd.randrange(16)

    # data processing: 16 opcodes x S x every operand 2 form
    for opcode in range(16):
        for s in range(2):
            head = (opcode << 21) | (s << 20)
            for rot in range(16):
                for imm in (0, 1, 2, 0x3F, 0x40, 0x80, 0xC0, 0xFF, rnd.randrange(256), rnd.randrange(256)):
                    add(head | (1 << 25) | (reg() << 16) | (reg() << 12) | (rot << 8) | imm)
            for stype in range(4):
                for amount in (0, 1, 2, 16, 31, rnd.randrange(32), rnd.randrange(32)):
                    add(head | (reg() << 16) | (reg() << 12) | (amount << 7) | (stype << 5) | reg(), 3)
                for _ in range(12):
                    add(head | (reg() << 16) | (reg() << 12) | (reg() << 8) | (stype << 5) | 0x10 | reg())
            # pc-relative add / sub and the return idioms
            add(head | (1 << 25) | (15 << 16) | (reg() << 12) | rnd.randrange(0x1000), 6)
            add(head | (reg() << 16) | (15 << 12) | 14, 2)
    # mov r0, r0 under every condition: objdump calls the AL one nop
    ops.extend((c << 28) | 0x01A00000 for c in range(16))

    # mul / mla
    for bits in range(4):
        for _ in range(200):
            add((bits << 20) | (reg() << 16) | (reg() << 12) | (reg() << 8) | 0x90 | reg())
    # swp / swpb
    for b in range(2):
        for _ in range(200):
            add(0x01000090 | (b << 22) | (reg() << 16) | (reg() << 12) | reg())
    # the rest of the multiply / swap space: ARMv4 long multiplies and halfword transfers
    for _ in range(6000):
        add((rnd.getrandbits(25) & ~0xF0) | 0x90 | (rnd.randrange(4) << 5))

    # mrs / msr: both PSRs, every field mask, register and immediate
    for p in range(2):
        for _ in range(60):
            add(0x010F0000 | (p << 22) | (reg() << 12))
        for mask in range(16):
            for _ in range(12):
                add(0x0120F000 | (p << 22) | (mask << 16) | reg())
                add(0x0320F000 | (p << 22) | (mask << 16) | rnd.randrange(0x1000))
    # the same space with should-be-zero / should-be-one fields violated
    for _ in range(6000):
        add((rnd.getrandbits(26) & ~0x01900000) | 0x01000000)

    # ldr / str: P U B W L x immediate and register offsets
    for bits in range(32):
        head = (1 << 26) | (bits << 20)
        for offset in (0, 1, 4, 8, 9, 10, 0xFF, 0x100, 0xFFF, rnd.randrange(0x1000), rnd.randrange(0x1000)):
            add(head | (reg() << 16) | (reg() << 12) | offset, 3)
            add(head | (15 << 16) | (reg() << 12) | offset)
        for stype in range(4):
            for amount in (0, 1, 31, rnd.randrange(32), rnd.randrange(32)):
                add(head | (1 << 25) | (reg() << 16) | (reg() << 12) | (amount << 7) | (stype << 5) | reg(), 3)
        # pop pc
        add(head | (13 << 16) | (15 << 12) | 4)
    # undefined instruction space
    for _ in range(4000):
        add((3 << 25) | rnd.getrandbits(25) | 0x10)

    # ldm / stm: P U S W L x register lists
    for bits in range(32):
        head = (4 << 25) | (bits << 20)
        lists = [0, 1, 0x8000, 0xFFFF, 0x4010, 0x8010, 0x00FF, 0x5FFF]
        lists += [1 << n for n in range(16)]
        lists += [rnd.getrandbits(16) for _ in range(24)]
        for regs_list in lists:
            add(head | (reg() << 16) | regs_list)
            add(head | (13 << 16) | regs_list)

    # b / bl
    for link in range(2):
        head = (5 << 25) | (link << 24)
        for disp in (0, 1, 2, 0x7FFFFF, 0x800000, 0xFFFFFF, 0xFFFFFE, 0xFFFFFD):
            add(head | disp, 8)
        for _ in range(1500):
            add(head | rnd.getrandbits(24))

    # ldc / stc: P U N W L x every coprocessor
    for bits in range(32):
        head = (6 << 25) | (bits << 20)
        for cp in range(16):
            for offset in (0, 1, 0xFF, rnd.randrange(256), rnd.randrange(256)):
                add(head | (reg() << 16) | (rnd.randrange(16) << 12) | (cp << 8) | offset)

    # cdp, mrc / mcr: every coprocessor
    for cp in range(16):
        for _ in range(250):
            add((14 << 24) | (rnd.getrandbits(24) & ~0xF10) | (cp << 8))
            add((14 << 24) | (rnd.getrandbits(24) & ~0xF00) | (cp << 8) | 0x10)

    # swi
    for comment in (0, 1, 0x123456, 0xFFFFFF, 0x800000):
        add((15 << 24) | comment, 8)
    for _ in range(1500):
        add((15 << 24) | rnd.getrandbits(24))

    # uniform random words on top
    ops += [rnd.getrandbits(32) for _ in range(200000)]
    return ops


def run_objdump(objdump, workdir, ops):
    image = os.path.join(workdir, 'arm7_sample.bin')
    with open(image, 'wb') as f:
        f.write(b''.join(struct.pack('<I', op) for op in ops))
    out = subprocess.run([objdump, '-D', '-b', 'binary', '-m', 'armv3', '-M', 'reg-names-std', '-EL',
                          '--adjust-vma=0x%x' % BASE, image], check=True, capture_output=True, text=True).stdout
    result = []
    for line in out.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\t([0-9a-f]{8}) \t(.*)$', line)
        if not m:
            continue
        assert int(m.group(1), 16) == BASE + 4 * len(result), line
        assert int(m.group(2), 16) == ops[len(result)], line
        result.append(m.group(3))
    assert len(result) == len(ops), (len(result), len(ops))
    return result


def run_ours(test_bin, ops):
    out = subprocess.run([test_bin, 'arm', '0x%x' % BASE], input=''.join('%08x\n' % op for op in ops),
                         check=True, capture_output=True, text=True).stdout
    result = []
    for line in out.splitlines():
        op, sig, text = line.split('\t')
        flags, *rest = sig.split(' ')
        info = {'flags': flags}
        for item in rest:
            key, _, value = item.partition('=')
            info[key] = value
        assert int(op, 16) == ops[len(result)]
        result.append((text, info))
    assert len(result) == len(ops)
    return result


def num(value):
    """A number the way dbg_disasm prints it."""
    return '%d' % value if value < 10 else '0x%x' % value


def split_mnemonic(mnemonic, roots):
    """Splits '<root><cond>' for the longest matching root; returns (root, cond) or None."""
    for root in sorted(roots, key=len, reverse=True):
        if mnemonic.startswith(root) and mnemonic[len(root):] in COND:
            return root, mnemonic[len(root):]
    return None


def expected_flags(op, text):
    """
    The info flags that follow from an instruction's text, as documented in dbg_disasm.h:
    valid, branch, (delayed: never), call, return, conditional, trap.
    """
    mnemonic, _, operands = text.partition(' ')
    first = operands.split(',')[0]
    branch = call = ret = trap = False
    cls = classify(op)
    if cls == 'b-bl':
        branch = True
        call = split_mnemonic(mnemonic, ['b', 'bl'])[0] == 'bl'
    elif cls == 'swi':
        trap = True
    elif cls == 'dp' and first == 'pc' and not split_mnemonic(mnemonic, [n + p for n in DP[8:12] for p in ('', 'p')]):
        branch = True
        ret = bool(re.match(r'movs?(%s) pc, lr$' % '|'.join(COND), text)) or \
            bool(re.match(r'subs(%s) pc, lr, #' % '|'.join(COND), text))
    elif cls == 'ldr-str' and mnemonic.startswith('ldr') and first == 'pc':
        branch = True
        ret = operands.startswith('pc, [sp')
    elif cls == 'ldm-stm' and mnemonic.startswith('ldm') and re.search(r'\bpc\b', operands.partition('{')[2]):
        branch = True
        ret = first in ('sp', 'sp!')
    return 'v' + ''.join(c if on else '-' for c, on in
                         (('b', branch), ('d', False), ('c', call), ('r', ret), ('?', op >> 28 != 14), ('t', trap)))


def rewrite(op, text):
    """
    objdump's line for an encoding, rewritten into dbg_disasm's canonical form.
    Returns (text, rules) where rules names the rewrites that changed something.
    """
    rules = set()
    parts = text.split('\t')
    mnemonic = parts[0]
    operands = parts[1] if len(parts) > 1 and not parts[1].startswith('@') else ''
    if any(p.startswith('@') for p in parts[1:]):
        rules.add('comment dropped')
    cls = classify(op)

    # unified-syntax aliases back to the instruction they encode
    m = split_mnemonic(mnemonic, ['svc'])
    if m:
        mnemonic = 'swi' + m[1]
        operands = '0x%06x' % int(operands, 16)
        rules.add('svc -> swi')

    m = split_mnemonic(mnemonic, ['push', 'pop'])
    if m:
        if cls == 'ldm-stm':
            mnemonic = ('stmdb' if m[0] == 'push' else 'ldmia') + m[1]
            operands = 'sp!, ' + operands
        else:
            reg = operands.strip('{}')
            mnemonic = ('str' if m[0] == 'push' else 'ldr') + m[1]
            operands = reg + (', [sp, #-4]!' if m[0] == 'push' else ', [sp], #4')
        rules.add('push / pop -> stmdb / ldmia / str / ldr')

    m = split_mnemonic(mnemonic, ['ldm', 'stm', 'ldmfd', 'stmfd'])
    if m:
        mnemonic = m[0][:3] + ('db' if m[0] == 'stmfd' else 'ia') + m[1]
        rules.add('ldm / stm / ldmfd / stmfd -> ldmia / stmia / stmdb')

    if mnemonic == 'nop':
        mnemonic, operands = 'mov', 'r0, r0'
        rules.add('nop -> mov r0, r0')

    m = split_mnemonic(mnemonic, [a + s for a in SHIFT_ALIAS + ('rrx',) for s in ('', 's')])
    if m and cls == 'dp':
        root, s = (m[0][:-1], 's') if m[0].endswith('s') else (m[0], '')
        fields = [f.strip() for f in operands.split(',')]
        mnemonic = 'mov' + s + m[1]
        if root == 'rrx':
            operands = '%s, %s, rrx' % (fields[0], fields[1])
        else:
            operands = '%s, %s, %s %s' % (fields[0], fields[1], root, fields[2])
        rules.add('shift alias -> mov')

    # numbers
    def immediate(match):
        value = int(match.group(1))
        if cls in ('dp', 'msr'):
            out = '#' + num(value & 0xFFFFFFFF)
        else:
            out = '#' + ('-' if match.group(1).startswith('-') else '') + num(abs(value))
        if out != match.group(0):
            rules.add('number format')
        return out

    if cls in ('dp', 'msr'):
        # "#imm8, rot": the rotation stays decimal
        m = re.search(r'#(-?\d+), (\d+)$', operands)
        if m:
            operands = operands[:m.start()] + '#' + num(int(m.group(1))) + ', ' + m.group(2)
            if int(m.group(1)) >= 10:
                rules.add('number format')
        else:
            operands = re.sub(r'#(-?\d+)\b(?! *,? *\d)', immediate, operands) if '#' in operands else operands
            operands = re.sub(r'(lsl|lsr|asr|ror) #0x([0-9a-f]+)', lambda mm: '%s #%d' % (mm.group(1), int(mm.group(2), 16)), operands)
    elif cls in ('ldr-str', 'ldc-stc'):
        # offsets; shift amounts stay decimal
        operands = re.sub(r'(?<!lsl )(?<!lsr )(?<!asr )(?<!ror )#(-?\d+)', immediate, operands)

    if cls in ('cdp', 'mrc-mcr', 'ldc-stc'):
        before = operands
        operands = re.sub(r'^(\d+),', r'p\1,', operands)
        operands = re.sub(r'\bcr(\d+)', r'c\1', operands)
        if cls != 'ldc-stc':
            operands = re.sub(r'\{(\d+)\}$', r'\1', operands)
        if operands != before:
            rules.add('coprocessor operand spelling')
        if 'APSR_nzcv' in operands:
            operands = operands.replace('APSR_nzcv', 'pc')
            rules.add('mrc APSR_nzcv -> pc')

    if cls == 'ldc-stc' and not op & 0xFF:
        # objdump prints a zero offset as "[rn]" / "[rn, #-0]" / "[rn], #-0" whatever the write-back bit
        # says, and the unindexed option of a U = 0 encoding as {-0}; the bits are restored here
        before = operands
        pre, up, wb = op & (1 << 24), op & (1 << 23), op & (1 << 21)
        if pre and wb:
            operands = operands[:-1] + ', #0]!' if up else operands + '!'
        elif wb and up:
            operands += ', #0'
        elif not wb and not pre:
            operands = operands.replace('{-0}', '{0}')
        if operands != before:
            rules.add('ldc / stc zero offset: write-back restored')

    if cls == 'b-bl':
        padded = '0x%08x' % int(operands, 16)
        if padded != operands:
            rules.add('address padded to 8 digits')
        operands = padded

    return (mnemonic + ' ' + operands).strip(), rules


def twin(op):
    """
    The encoding objdump is shown in place of op, with the rules that explain the substitution.
    - objdump has no NV condition (the space was reused from ARMv5 on): it gets the AL encoding
    - objdump rejects mov with a non-zero Rn field, which the ARM7DI ignores: it gets Rn = 0
    """
    rules = set()
    if op >> 28 == 15:
        op = (op & 0x0FFFFFFF) | 0xE0000000
        rules.add('NV compared with the AL encoding')
    if classify(op) == 'dp' and (op >> 21) & 15 == 13 and op & 0x000F0000:
        op &= ~0x000F0000
        rules.add('mov with a non-zero Rn field compared with Rn = 0')
    return op, rules


def expected_literal(op, cls, pc):
    """
    The pc-relative data address of an encoding at pc as (address, size), or None when it has none. Whether there
    is one depends on the encoding only: ldr / str with pc as base and a pre-indexed immediate offset (1 or 4
    bytes), add / sub rd, pc, #imm (the address alone, size 0).
    """
    if (op >> 16) & 15 != 15:
        return None
    if cls == 'ldr-str' and op & (1 << 24) and not op & (1 << 25):
        offset = op & 0xFFF
        return (pc + 8 + (offset if op & (1 << 23) else -offset)) & 0xFFFFFFFF, 1 if op & (1 << 22) else 4
    if cls == 'dp' and op & (1 << 25) and DP[(op >> 21) & 15] in ('add', 'sub'):
        rot = (op >> 7) & 30
        value = ((op & 0xFF) >> rot | (op & 0xFF) << (32 - rot)) & 0xFFFFFFFF
        return (pc + 8 + (value if DP[(op >> 21) & 15] == 'add' else -value)) & 0xFFFFFFFF, 0
    return None


GENERIC_COPROC = ('cdp', 'mrc', 'mcr', 'ldc', 'stc')


def explained(op, cls, obj_raw, our_text, our_valid):
    """Class of a documented difference between objdump -m armv3 and the ARM7DI, or None."""
    obj_mnemonic = obj_raw.split('\t')[0]
    obj_undefined = '<UNDEFINED>' in obj_raw

    if cls == 'mulswp-other' and not our_valid:
        return 'multiply / swap space beyond mul, mla, swp (ARMv4 long multiply, halfword transfers)'
    if cls == 'psr-other' and not our_valid:
        return 'PSR transfer space (tst / teq / cmp / cmn, S clear) outside the mrs / msr encodings'
    if cls == 'undefined' and not our_valid:
        return 'undefined instruction space'
    if cls in ('cdp', 'mrc-mcr', 'ldc-stc') and our_valid:
        generic = split_mnemonic(obj_mnemonic, GENERIC_COPROC + ('ldcl', 'stcl'))
        if not obj_undefined and not generic:
            return 'coprocessor space: objdump prints an FPA / VFP / other coprocessor mnemonic'
        if obj_undefined and (op >> 8) & 15 in (9, 10, 11):
            return 'coprocessors 9-11: objdump knows only the VFP / Neon encodings'
    return None


def expected(op, obj_raw):
    """
    What objdump's line for an encoding stands for in dbg_disasm's terms: (valid, text, rules, flags). rules names
    the rewrites that changed something, flags is the info signature that follows from the text: every flag is
    clear on an encoding that is not an instruction.
    """
    if '<UNDEFINED>' in obj_raw:
        return False, '.word 0x%08x' % op, {'<UNDEFINED> -> .word'}, '-' * len(SIGNATURE)
    want, rules = rewrite(op, obj_raw)
    rules |= twin(op)[1]
    if op >> 28 == 15:
        mnemonic, _, rest = want.partition(' ')
        want = (mnemonic + 'nv ' + rest).strip()
    return True, want, rules, expected_flags(op, want)


def compare(ops, obj, expect, ours):
    """
    Compares the dump of the sample (ours) with objdump's lines (obj) and what they stand for (expect: one
    expected() per encoding).
    """
    counts = {'identical': 0, 'normalised': 0, 'unexplained': 0}
    rule_counts = {}
    class_counts = {}
    per_class = {}
    checked = {'flag sets': 0, 'branch targets': 0, 'literal addresses': 0, 'literal values': 0}
    bad = []

    for index, op in enumerate(ops):
        obj_raw = obj[index]
        our_raw, info = ours[index]
        our_text, _, our_comment = our_raw.partition(' @ ')
        our_text = ' '.join(our_text.split())
        our_valid = info['flags'][0] == 'v'
        cls = classify(op)
        pc = BASE + 4 * index
        stats = per_class.setdefault(cls, [0, 0, 0, 0])
        stats[0] += 1

        obj_valid, want, rules, want_flags = expect[index]

        same_text = obj_valid == our_valid and want == our_text
        ok = same_text
        if ok:
            ok = info['flags'] == want_flags
            checked['flag sets'] += 1
        if ok:
            # addresses: objdump gives them as "0x..." operands (b / bl) or "@ 0x..." comments (pc-relative ldr / str);
            # for add / sub rd, pc, #imm it prints none and the recomputed one stands alone
            if cls == 'b-bl':
                ok = info.get('t') == '%08x' % int(obj_raw.split('\t')[1], 16)
                checked['branch targets'] += 1
            elif 't' in info:
                ok = False
            literal = expected_literal(op, cls, pc)
            if literal:
                address, size = literal
                ok = ok and info.get('l') == '%08x/%d' % literal
                checked['literal addresses'] += 1
                want_comment = '0x%08x' % address
                if cls == 'ldr-str':
                    m = re.search(r'@ 0x([0-9a-f]+)', obj_raw)
                    ok = ok and m is not None and int(m.group(1), 16) == address
                    offset = address - BASE
                    if op & (1 << 20) and 0 <= offset and offset + size <= 4 * len(ops):
                        image = b''.join(struct.pack('<I', o) for o in ops[offset // 4:offset // 4 + 2])
                        value = int.from_bytes(image[offset % 4:offset % 4 + size], 'little')
                        want_comment += ' = 0x%0*x' % (2 * size, value)
                        checked['literal values'] += 1
                ok = ok and our_comment == want_comment
            elif 'l' in info or our_comment:
                ok = False

        if ok:
            if rules - {'comment dropped'} or (rules and want != ' '.join(obj_raw.replace('\t', ' ').split())):
                counts['normalised'] += 1
                stats[2] += 1
                for name in rules:
                    rule_counts[name] = rule_counts.get(name, 0) + 1
            else:
                counts['identical'] += 1
                stats[1] += 1
            continue

        # the documented classes are differences in validity or text; a wrong info field is never one of them
        why = None if same_text else explained(op, cls, obj_raw, our_text, our_valid)
        if why:
            class_counts[why] = class_counts.get(why, 0) + 1
            stats[3] += 1
        else:
            counts['unexplained'] += 1
            bad.append((op, cls, obj_raw, want, our_raw))

    return counts, rule_counts, class_counts, per_class, checked, bad


def corruptions():
    """
    One entry per way an info field compare() counts as checked can be wrong: (name, population, change).
    change() takes the pc, the text and the info of one encoding and returns text and info with that field wrong,
    removed or added, or None when the encoding gives it nothing to change. population names the encodings the
    change applies to; where it is a key of 'checked', that figure has to be their number.
    """
    def without(info, key):
        return {k: v for k, v in info.items() if k != key}

    def flag(position):
        def change(pc, text, info):
            flags = info['flags']
            flipped = SIGNATURE[position] if flags[position] == '-' else '-'
            return text, dict(info, flags=flags[:position] + flipped + flags[position + 1:])
        return change

    def wrong_target(pc, text, info):
        if 't' not in info:
            return None
        return text, dict(info, t='%08x' % (int(info['t'], 16) ^ 4))

    def no_target(pc, text, info):
        if 't' not in info:
            return None
        return text, without(info, 't')

    def added_target(pc, text, info):
        if 't' in info:
            return None
        return text, dict(info, t='%08x' % (pc + 8))

    def wrong_literal_address(pc, text, info):
        if 'l' not in info:
            return None
        address, size = info['l'].split('/')
        return text, dict(info, l='%08x/%s' % (int(address, 16) ^ 4, size))

    def wrong_literal_size(pc, text, info):
        if 'l' not in info:
            return None
        address, size = info['l'].split('/')
        return text, dict(info, l='%s/%d' % (address, 1 if size == '4' else 4))

    # a disassembler that sees no literal prints no comment either, and one that sees a literal prints its address
    def no_literal(pc, text, info):
        if 'l' not in info:
            return None
        return text.partition(' @ ')[0], without(info, 'l')

    def added_literal(pc, text, info):
        if 'l' in info:
            return None
        return text + ' @ 0x%08x' % (pc + 8), dict(info, l='%08x/0' % (pc + 8))

    def wrong_value(pc, text, info):
        head, mark, value = text.partition(' = 0x')
        if not mark:
            return None
        return head + mark + '%0*x' % (len(value), int(value, 16) ^ 1), info

    def no_value(pc, text, info):
        head, mark, _ = text.partition(' = ')
        return (head, info) if mark else None

    def added_value(pc, text, info):
        if 'l' not in info or ' = ' in text:
            return None
        return text + ' = 0x00000000', info

    return [('wrong %s flag' % SIGNATURE[position], 'flag sets', flag(position))
            for position in range(1, len(SIGNATURE))] + [
        ('wrong branch target', 'branch targets', wrong_target),
        ('no branch target', 'branch targets', no_target),
        ('added branch target', 'encodings without a branch target', added_target),
        ('wrong literal address', 'literal addresses', wrong_literal_address),
        ('wrong literal size', 'literal addresses', wrong_literal_size),
        ('no literal', 'literal addresses', no_literal),
        ('added literal', 'encodings without a literal', added_literal),
        ('wrong literal value', 'literal values', wrong_value),
        ('no literal value', 'literal values', no_value),
        ('added literal value', 'literal addresses without a value', added_value)]


def self_test(ops, obj, expect, ours):
    """
    Checks compare() itself: with one info field wrong, removed or added on every encoding that allows it, exactly
    the changed encodings have to come back unexplained, as far as their validity and text agree with objdump.
    Returns the exit status.
    """
    counts, _, _, _, checked, _ = compare(ops, obj, expect, ours)
    if counts['unexplained']:
        print('  needs a dump without unexplained differences, this one has %d' % counts['unexplained'])
        return 1

    # every field is checked where validity and text agree, and no field where they differ in a documented way.
    # Which encodings agree is worked out here, not taken from compare()
    agree = []
    for (obj_valid, want, _, _), (text, info) in zip(expect, ours):
        our_text = ' '.join(text.partition(' @ ')[0].split())
        agree.append(obj_valid == (info['flags'][0] == 'v') and want == our_text)
    if sum(agree) != counts['identical'] + counts['normalised']:
        print('  %d encodings agree in validity and text, %d counted as identical or normalised-equal'
              % (sum(agree), counts['identical'] + counts['normalised']))
        return 1

    status = 0
    for name, population, change in corruptions():
        corrupted = list(ours)
        due = 0
        for index, (text, info) in enumerate(ours):
            changed = change(BASE + 4 * index, text, info)
            if changed:
                corrupted[index] = changed
                due += agree[index]
        reported = compare(ops, obj, expect, corrupted)[0]['unexplained']
        verdict = ''
        if reported != due or not due:
            verdict = '  NOT ALL REPORTED'
        elif checked.get(population, due) != due:
            verdict = '  BUT %d COUNTED AS CHECKED' % checked[population]
        print('  %-21s unexplained on %6d of %6d %s%s' % (name, reported, due, population, verdict))
        if verdict:
            status = 1
    return status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--objdump', default='')
    ap.add_argument('--test-bin', required=True)
    ap.add_argument('--workdir', required=True)
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--self-test', action='store_true',
                    help='test the comparison itself: wrong, missing and added info fields have to be reported')
    args = ap.parse_args()

    if not args.objdump or args.objdump.endswith('-NOTFOUND') or not os.path.exists(args.objdump):
        print('SKIP: no ARM objdump (arm-eabi-objdump / arm-none-eabi-objdump); set -DARM_OBJDUMP=<path>')
        return SKIP

    os.makedirs(args.workdir, exist_ok=True)
    version = subprocess.run([args.objdump, '--version'], capture_output=True, text=True).stdout.splitlines()[0]

    ops = sample()
    obj = run_objdump(args.objdump, args.workdir, [twin(op)[0] for op in ops])
    expect = [expected(op, obj_raw) for op, obj_raw in zip(ops, obj)]
    ours = run_ours(args.test_bin, ops)

    if args.self_test:
        print('arm7 comparison self-test vs %s -m armv3' % version)
        return self_test(ops, obj, expect, ours)

    counts, rule_counts, class_counts, per_class, checked, bad = compare(ops, obj, expect, ours)

    print('arm7 vs %s -m armv3' % version)
    print('  encodings compared    %d' % len(ops))
    print('  identical             %d' % counts['identical'])
    print('  normalised-equal      %d' % counts['normalised'])
    for name in sorted(rule_counts):
        print('    %-52s %d' % (name, rule_counts[name]))
    print('  explained differences %d' % sum(class_counts.values()))
    for name in sorted(class_counts):
        print('    %s: %d' % (name, class_counts[name]))
    print('  unexplained           %d' % counts['unexplained'])
    print('  checked: ' + ', '.join('%d %s' % (checked[k], k) for k in checked))
    print('  per class (encodings / identical / normalised-equal / explained):')
    for cls in sorted(per_class):
        print('    %-14s %7d %7d %7d %7d' % (cls, *per_class[cls]))
    for op, cls, obj_raw, want, our_raw in bad[:(len(bad) if args.verbose else 25)]:
        print('  UNEXPLAINED %08x [%s]: objdump "%s" -> "%s" ours "%s"' % (op, cls, obj_raw.replace('\t', ' '), want, our_raw))
    return 1 if counts['unexplained'] else 0


if __name__ == '__main__':
    sys.exit(main())
