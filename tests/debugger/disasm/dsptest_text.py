#!/usr/bin/env python3
"""The text of the sounding guest's DSP program against its ELF and the emulator's disassembler.

    dsptest_text.py --test-bin <disasm_test> --guest <tests/debugger/guest/dsptest>
                    [--tsx <tsx> --dsp-utils <dspUtils.ts>]

The guest's DSP program is written once, as lines of the DSP playground's assembler (dsptest_dsp.txt), and
gen.sh assembles it into the ELF (dsptest.elf: between the symbols g_dsp_mpro and g_dsp_mpro_end one 32-bit word,
low half, for each 16-bit word of MPRO, four for each step from step 0). This test holds the three together:

  - dbg::DisasmDsp (disasm_test dsp) of the words of every one of the 128 steps as the ELF loads them gives the
    MPRO lines of the text, line for line, and MPRO[n] = TRA:0 for every step behind the block
  - with a dreamcast-devtools checkout (--tsx, --dsp-utils): the playground's assembleSource, given the text,
    gives the words of the ELF for every MPRO line, the COEF and MADRS of the ELF's block (g_dsp_coef,
    g_dsp_madrs) for every COEF and MADRS line, and the values of the ELF's table entries of MEMS (bits 23:8 of
    MEMS[i], at 0x00704404 + 8 i of the AICA) for every MEMS_H line (dsptest_text.mts)

Without the checkout the second part is not run: the test says "SKIP: " and exits with 77, after the first has
passed. Exit code 0: both parts hold; 1: one of them does not.
"""
import argparse
import os
import struct
import subprocess
import sys


def fail(what):
    print('FAIL: ' + what)
    sys.exit(1)


def elf_symbols_and_bytes(path):
    """The symbols of a 32-bit ELF, low byte first, and a function that gives the bytes the ELF loads at an address."""
    with open(path, 'rb') as f:
        image = f.read()
    if image[:6] != b'\x7fELF\x01\x01':
        fail('%s is no ELF of 32 bits, low byte first' % path)
    phoff, shoff = struct.unpack_from('<II', image, 0x1C)
    phentsize, phnum, shentsize, shnum = struct.unpack_from('<HHHH', image, 0x2A)
    symbols = {}
    for i in range(shnum):
        sh = shoff + i * shentsize
        kind, = struct.unpack_from('<I', image, sh + 4)
        if kind != 2:   # SHT_SYMTAB
            continue
        offset, size, link = struct.unpack_from('<III', image, sh + 0x10)
        entsize, = struct.unpack_from('<I', image, sh + 0x24)
        stroff, = struct.unpack_from('<I', image, shoff + link * shentsize + 0x10)
        for at in range(offset, offset + size, entsize):
            name, value = struct.unpack_from('<II', image, at)
            end = image.index(b'\0', stroff + name)
            symbols[image[stroff + name:end].decode('ascii', 'replace')] = value

    def loaded(address, length):
        for n in range(phnum):
            kind, offset, vaddr, paddr, filesz = struct.unpack_from('<5I', image, phoff + n * phentsize)
            if kind == 1 and vaddr <= address and address + length <= vaddr + filesz:
                return image[offset + address - vaddr:offset + address - vaddr + length]
        fail('the ELF loads nothing at %08X (%d bytes)' % (address, length))

    return symbols, loaded


def words_between(symbols, loaded, start, end):
    for name in (start, end):
        if name not in symbols:
            fail('dsptest.elf has no symbol %s' % name)
    length = symbols[end] - symbols[start]
    if length < 0 or length % 4:
        fail('%s to %s is %d bytes, no block of 32-bit words' % (start, end, length))
    data = loaded(symbols[start], length)
    return [struct.unpack_from('<I', data, 4 * n)[0] for n in range(length // 4)]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--test-bin', required=True)
    parser.add_argument('--guest', required=True, help='the directory of the sounding guest')
    parser.add_argument('--tsx', default='')
    parser.add_argument('--dsp-utils', default='')
    args = parser.parse_args()

    text_path = os.path.join(args.guest, 'dsptest_dsp.txt')
    with open(text_path, 'r', encoding='utf-8') as f:
        lines = [line.strip() for line in f.read().splitlines()]
    mpro_lines = [line for line in lines if line.startswith('MPRO[')]
    if not mpro_lines:
        fail('%s has no MPRO line' % text_path)

    symbols, loaded = elf_symbols_and_bytes(os.path.join(args.guest, 'dsptest.elf'))
    block = words_between(symbols, loaded, 'g_dsp_mpro', 'g_dsp_mpro_end')
    if len(block) % 4 or len(block) > 4 * 128:
        fail('g_dsp_mpro to g_dsp_mpro_end has %d words, not four for each of at most 128 steps' % len(block))
    for n, word in enumerate(block):
        if word >> 16:
            fail('word %d of g_dsp_mpro is %08X: its high half is not 0' % (n, word))
    steps = len(block) // 4
    program = [block[4 * s:4 * s + 4] for s in range(steps)] + [[0, 0, 0, 0]] * (128 - steps)

    # the emulator's disassembler, on the 128 steps as the ELF loads them
    request = ''.join('%d %x %x %x %x\n' % ((s,) + tuple(program[s])) for s in range(128))
    done = subprocess.run([args.test_bin, 'dsp'], input=request.encode('ascii'), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    if done.returncode != 0:
        fail('disasm_test dsp ended with exit code %d: %s' % (done.returncode, done.stdout.decode('utf-8', 'replace')))
    printed = done.stdout.decode('ascii', 'replace').splitlines()
    if len(printed) != 128:
        fail('disasm_test dsp printed %d lines for 128 steps' % len(printed))

    if len(mpro_lines) != steps:
        fail('dsptest_dsp.txt has %d MPRO lines, the block g_dsp_mpro of dsptest.elf %d steps' % (len(mpro_lines), steps))
    wrong = ['step %d: the text has "%s", the disassembler prints "%s" for %s' % (s, mpro_lines[s], printed[s], ' '.join('%04X' % w for w in program[s]))
             for s in range(steps) if mpro_lines[s] != printed[s]]
    wrong += ['step %d, behind the program: the disassembler prints "%s"' % (s, printed[s])
              for s in range(steps, 128) if printed[s] != 'MPRO[%d] = TRA:0' % s]
    if wrong:
        fail('the MPRO lines of dsptest_dsp.txt are not the disassembly of the ELF:\n  ' + '\n  '.join(wrong))
    print('the emulator\'s disassembler of the %d steps of dsptest.elf gives the %d MPRO lines of dsptest_dsp.txt, and MPRO[n] = TRA:0 for the %d steps behind them'
          % (steps, len(mpro_lines), 128 - steps))

    # the playground's assembler, on the text
    if not (args.tsx and args.dsp_utils and os.path.isfile(args.tsx) and os.path.isfile(args.dsp_utils)):
        print('SKIP: the playground\'s half: no tsx and dspUtils.ts of a dreamcast-devtools checkout (--tsx "%s", --dsp-utils "%s")' % (args.tsx, args.dsp_utils))
        sys.exit(77)
    helper = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dsptest_text.mts')
    done = subprocess.run([args.tsx, helper, '--dsp-utils', args.dsp_utils, '--text', text_path], stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    said = done.stdout.decode('utf-8', 'replace')
    if done.returncode != 0:
        fail('the playground\'s assembler did not take dsptest_dsp.txt (exit code %d):\n%s' % (done.returncode, said))
    assembled = {}
    for line in said.splitlines():
        parts = line.split()
        if parts and parts[0] in ('MPRO', 'COEF', 'MADRS', 'MEMS_H', 'MEMS_L'):
            assembled.setdefault(parts[0], []).append(tuple(int(p, 0) for p in parts[1:]))

    coef = words_between(symbols, loaded, 'g_dsp_coef', 'g_dsp_coef_end')
    madrs = words_between(symbols, loaded, 'g_dsp_madrs', 'g_dsp_madrs_end')
    table = words_between(symbols, loaded, 'g_table', 'g_table_end')
    mems = {}
    for n in range(0, len(table) - 1, 2):
        address, value = table[n], table[n + 1]
        if 0xA0704404 <= address < 0xA0704500 and (address - 0xA0704404) % 8 == 0:
            mems[(address - 0xA0704404) // 8] = value

    wrong = ['MEMS_L[%d]: the low byte of MEMS is not the CPU\'s to write, the guest has no such line' % m[0] for m in assembled.get('MEMS_L', [])]
    mpro = assembled.get('MPRO', [])
    if [m[0] for m in mpro] != list(range(steps)):
        wrong.append('the playground assembles MPRO lines for the steps %s, the ELF has the steps 0 to %d' % ([m[0] for m in mpro], steps - 1))
    for m in mpro:
        if m[0] < steps and list(m[1:]) != program[m[0]]:
            wrong.append('MPRO[%d]: the playground assembles %s, the ELF has %s' % (m[0], ' '.join('%04X' % w for w in m[1:]), ' '.join('%04X' % w for w in program[m[0]])))
    for kind, block_words in (('COEF', coef), ('MADRS', madrs)):
        for index, value in assembled.get(kind, []):
            have = block_words[index] if index < len(block_words) else None
            if have != value:
                wrong.append('%s[%d]: the playground assembles %04X, the block g_dsp of the ELF has %s' % (kind, index, value, 'nothing' if have is None else '%08X' % have))
    for index, value in assembled.get('MEMS_H', []):
        if mems.get(index) != value:
            wrong.append('MEMS_H[%d]: the playground assembles %04X, the table of the ELF writes %s' % (index, value, 'nothing' if index not in mems else '%08X' % mems[index]))
    if wrong:
        fail('the playground\'s assembler does not give the words of the ELF for dsptest_dsp.txt:\n  ' + '\n  '.join(wrong))
    print('the playground\'s assembler gives the words of dsptest.elf for the %d MPRO, %d COEF, %d MADRS and %d MEMS_H lines of dsptest_dsp.txt'
          % (len(mpro), len(assembled.get('COEF', [])), len(assembled.get('MADRS', [])), len(assembled.get('MEMS_H', []))))
    print('PASS')


if __name__ == '__main__':
    main()
