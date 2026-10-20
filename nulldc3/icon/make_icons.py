#!/usr/bin/env python3
"""make_icons.py: the emulator's icon files, made from the approved PNGs of nulldc3/icon/png/.

    python3 nulldc3/icon/make_icons.py            writes the three files below
    python3 nulldc3/icon/make_icons.py --check    writes nothing: rebuilds the three in memory, compares them byte for
                                                  byte with the committed ones and re-parses the committed .ico and
                                                  .icns; exits 1 naming each file that differs or is broken

Inputs, found relative to this file: png/nulldc-<n>.png for n = 16, 24, 32, 48, 64, 128, 180, 192, 256, 512, 1024.
Each must have the PNG signature, chunks with good CRCs and only IHDR, IDAT and IEND among them, IHDR width = height
= n, bit depth 8, colour type 6 (RGBA), compression and filter method 0, no interlace, and image data that inflates
(zlib) to exactly n rows of a filter type byte (0 to 4) and n * 4 bytes, every row filter undone. Anything else is
refused with the file named.

Outputs, relative to the top of the tree (two directories above this file):
    nulldc3/windows/nulldc3.ico     the Windows icon: an ICONDIR and seven ICONDIRENTRYs (16, 24, 32, 48, 64, 128,
                                    256 px; 32 bits, 1 plane; bWidth and bHeight 0 for 256), then the seven PNG files
                                    verbatim
    nulldc3/apple/nulldc3.icns      the macOS icon: 'icns' and the total length, then the elements icp4 (16), icp5
                                    (32), ic11 (16@2x), ic12 (32@2x), ic07 (128), ic13 (128@2x), ic08 (256), ic14
                                    (256@2x), ic09 (512), ic10 (512@2x), each a PNG file verbatim; no TOC
    libswirl/linux-dist/x11_icon.h  the X11 window's _NET_WM_ICON: for 16, 24, 32, 48, 64 and 128 px the width, the
                                    height, then the pixels row by row, each A << 24 | R << 16 | G << 8 | B as the
                                    PNG stores it (not premultiplied), as uint32_t, with the size table, the length
                                    checked by a static_assert and the 64-bit FNV-1a of the values

The script renders nothing and compresses nothing: the PNGs are the approved inputs and are embedded as they are, so
the outputs depend on their bytes alone. The header's text has a fixed layout (8 values a line, lower-case hex, LF)
and names only paths inside the tree. Every file is read and written in binary mode, so no system turns LF into CRLF.

Python 3.9 and the standard library.
"""
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, os.pardir, os.pardir))

PNG_SIZES = (16, 24, 32, 48, 64, 128, 180, 192, 256, 512, 1024)
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
ICNS_ELEMENTS = ((b'icp4', 16), (b'icp5', 32), (b'ic11', 32), (b'ic12', 64), (b'ic07', 128), (b'ic13', 256),
                 (b'ic08', 256), (b'ic14', 512), (b'ic09', 512), (b'ic10', 1024))
X11_SIZES = (16, 24, 32, 48, 64, 128)

ICO_PATH = 'nulldc3/windows/nulldc3.ico'
ICNS_PATH = 'nulldc3/apple/nulldc3.icns'
X11_PATH = 'libswirl/linux-dist/x11_icon.h'
GENERATOR = 'nulldc3/icon/make_icons.py'

PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'
FNV64_OFFSET = 0xcbf29ce484222325
FNV64_PRIME = 0x100000001b3


class IconError(Exception):
    pass


def out(text):
    sys.stdout.buffer.write((text + '\n').encode('ascii', 'replace'))
    sys.stdout.buffer.flush()


def png_rel(size):
    return 'nulldc3/icon/png/nulldc-%d.png' % size


def png_file(size):
    return os.path.join(HERE, 'png', 'nulldc-%d.png' % size)


def top_file(rel):
    return os.path.join(TOP, *rel.split('/'))


def read_bytes(path):
    with open(path, 'rb') as f:
        return f.read()


def write_bytes(path, data):
    with open(path, 'wb') as f:
        f.write(data)


# ---- the inputs ---------------------------------------------------------------------------------------------------

class Png:
    def __init__(self, size, data, rgba):
        self.size = size
        self.data = data    # the file's bytes, embedded verbatim
        self.rgba = rgba    # size * size pixels, row by row, 4 bytes each: R, G, B, A


def unfilter(raw, width, height, rel):
    """Undoes the five PNG row filters at 4 bytes a pixel (8-bit RGBA); returns the pixel bytes without the filter
    type bytes."""
    stride = width * 4
    out_rows = bytearray()
    prev = bytearray(stride)
    pos = 0
    for y in range(height):
        ftype = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if ftype == 0:
            pass
        elif ftype == 1:
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xff
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xff
        elif ftype == 3:
            for i in range(4):
                line[i] = (line[i] + (prev[i] >> 1)) & 0xff
            for i in range(4, stride):
                line[i] = (line[i] + ((line[i - 4] + prev[i]) >> 1)) & 0xff
        elif ftype == 4:
            for i in range(4):
                line[i] = (line[i] + prev[i]) & 0xff   # left and upper left are 0: the predictor is the byte above
            for i in range(4, stride):
                a = line[i - 4]
                b = prev[i]
                c = prev[i - 4]
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                if pa <= pb and pa <= pc:
                    pred = a
                elif pb <= pc:
                    pred = b
                else:
                    pred = c
                line[i] = (line[i] + pred) & 0xff
        else:
            raise IconError('%s: row %d has filter type %d (0 to 4 exist)' % (rel, y, ftype))
        out_rows += line
        prev = line
    return bytes(out_rows)


def read_png(size):
    """Reads and validates png/nulldc-<size>.png; IconError names the file and what is wrong with it."""
    rel = png_rel(size)
    path = png_file(size)
    if not os.path.isfile(path):
        raise IconError('%s: missing' % rel)
    data = read_bytes(path)
    if data[:8] != PNG_SIGNATURE:
        raise IconError('%s: no PNG signature' % rel)
    pos = 8
    chunks = []
    idat = []
    while pos < len(data):
        if pos + 12 > len(data):
            raise IconError('%s: a chunk is cut off at byte %d' % (rel, pos))
        length, ctype = struct.unpack('>I4s', data[pos:pos + 8])
        if pos + 12 + length > len(data):
            raise IconError('%s: chunk %r at byte %d runs past the end of the file' % (rel, ctype, pos))
        body = data[pos + 8:pos + 8 + length]
        crc = struct.unpack('>I', data[pos + 8 + length:pos + 12 + length])[0]
        if zlib.crc32(ctype + body) & 0xffffffff != crc:
            raise IconError('%s: chunk %r at byte %d has a bad CRC' % (rel, ctype, pos))
        if ctype not in (b'IHDR', b'IDAT', b'IEND'):
            raise IconError('%s: chunk %r at byte %d: only IHDR, IDAT and IEND are accepted' % (rel, ctype, pos))
        chunks.append(ctype)
        if ctype == b'IDAT':
            idat.append(body)
        elif ctype == b'IHDR':
            ihdr = body
        pos += 12 + length
        if ctype == b'IEND':
            break
    if pos != len(data):
        raise IconError('%s: %d bytes after IEND' % (rel, len(data) - pos))
    if not chunks or chunks[0] != b'IHDR' or chunks.count(b'IHDR') != 1:
        raise IconError('%s: IHDR is not the first chunk, or not the only one' % rel)
    if chunks[-1] != b'IEND' or chunks.count(b'IEND') != 1:
        raise IconError('%s: IEND is not the last chunk' % rel)
    if b'IDAT' not in chunks:
        raise IconError('%s: no IDAT chunk' % rel)
    first = chunks.index(b'IDAT')
    if chunks[first:first + len(idat)] != [b'IDAT'] * len(idat):
        raise IconError('%s: the IDAT chunks are not consecutive' % rel)
    if len(ihdr) != 13:
        raise IconError('%s: IHDR is %d bytes long, not 13' % (rel, len(ihdr)))
    width, height, depth, ctype, comp, filt, interlace = struct.unpack('>IIBBBBB', ihdr)
    if width != size or height != size:
        raise IconError('%s: %d x %d pixels, the name says %d x %d' % (rel, width, height, size, size))
    if depth != 8:
        raise IconError('%s: bit depth %d, only 8 is accepted' % (rel, depth))
    if ctype != 6:
        raise IconError('%s: colour type %d, only 6 (RGBA) is accepted' % (rel, ctype))
    if comp != 0 or filt != 0:
        raise IconError('%s: compression method %d, filter method %d, only 0 and 0 exist' % (rel, comp, filt))
    if interlace != 0:
        raise IconError('%s: interlaced (method %d), only non-interlaced images are accepted' % (rel, interlace))
    inflater = zlib.decompressobj()
    try:
        raw = inflater.decompress(b''.join(idat))
        raw += inflater.flush()
    except zlib.error as e:
        raise IconError('%s: the image data does not inflate: %s' % (rel, e))
    if not inflater.eof:
        raise IconError('%s: the image data stream is cut off' % rel)
    if inflater.unused_data:
        raise IconError('%s: %d bytes after the end of the image data stream' % (rel, len(inflater.unused_data)))
    if len(raw) != height * (1 + width * 4):
        raise IconError('%s: the image data inflates to %d bytes, %d x %d RGBA needs %d'
                        % (rel, len(raw), width, height, height * (1 + width * 4)))
    return Png(size, data, unfilter(raw, width, height, rel))


def read_pngs():
    return dict((size, read_png(size)) for size in PNG_SIZES)


# ---- the outputs --------------------------------------------------------------------------------------------------

def build_ico(pngs):
    """ICONDIR (reserved 0, type 1, count), one ICONDIRENTRY of 16 bytes per size, then the PNG files verbatim, in
    that order; little-endian. bWidth and bHeight hold the size, 0 for 256."""
    head = struct.pack('<HHH', 0, 1, len(ICO_SIZES))
    offset = 6 + 16 * len(ICO_SIZES)
    entries = []
    for size in ICO_SIZES:
        png = pngs[size].data
        dim = 0 if size == 256 else size
        entries.append(struct.pack('<BBBBHHII', dim, dim, 0, 0, 1, 32, len(png), offset))
        offset += len(png)
    return head + b''.join(entries) + b''.join(pngs[size].data for size in ICO_SIZES)


def build_icns(pngs):
    """'icns' and the total length (with its 8 bytes), then each element: its OSType, its length (with its 8 bytes)
    and the PNG file verbatim; big-endian. The one place that decides the .icns format."""
    body = b''.join(ostype + struct.pack('>I', 8 + len(pngs[size].data)) + pngs[size].data
                    for ostype, size in ICNS_ELEMENTS)
    return b'icns' + struct.pack('>I', 8 + len(body)) + body


def x11_cardinals(pngs):
    """The values of _NET_WM_ICON: per size width, height and the pixels row by row, A << 24 | R << 16 | G << 8 | B."""
    values = []
    for size in X11_SIZES:
        rgba = pngs[size].rgba
        values.append(size)
        values.append(size)
        for i in range(0, len(rgba), 4):
            r, g, b, a = rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]
            values.append((a << 24) | (r << 16) | (g << 8) | b)
    return values


def fnv1a64(values):
    """64-bit FNV-1a of the values, each taken as its four bytes, least significant first."""
    h = FNV64_OFFSET
    for v in values:
        for shift in (0, 8, 16, 24):
            h ^= (v >> shift) & 0xff
            h = (h * FNV64_PRIME) & 0xffffffffffffffff
    return h


def build_x11_header(pngs):
    values = x11_cardinals(pngs)
    count = sum(2 + size * size for size in X11_SIZES)
    if len(values) != count:
        raise IconError('%s: %d values made, %d expected' % (X11_PATH, len(values), count))
    offsets = []
    pos = 0
    for size in X11_SIZES:
        offsets.append(pos)
        pos += 2 + size * size
    lines = [
        '// Generated by %s from nulldc3/icon/png/nulldc-{%s}.png: do not edit.'
        % (GENERATOR, ','.join(str(s) for s in X11_SIZES)),
        '// "python3 %s" writes it again from the PNGs; with --check it compares it with them' % GENERATOR,
        '// (the test icons.generated).',
        '//',
        '// The X11 window\'s icon, the property _NET_WM_ICON (CARDINAL, format 32): for each size its width, its height,',
        '// then its pixels row by row from the top, each A << 24 | R << 16 | G << 8 | B as the PNG stores it (not',
        '// premultiplied). Xlib takes the values of a format 32 property as C longs: x11_icon_cardinals() (x11_icon.cpp)',
        '// widens these to unsigned long, which is what the window gets (x11.cpp).',
        '',
        '#pragma once',
        '',
        '#include <cstddef>',
        '#include <cstdint>',
        '#include <vector>',
        '',
        '// The values of _NET_WM_ICON, zero-extended to unsigned long (x11_icon.cpp)',
        'std::vector<unsigned long> x11_icon_cardinals();',
        '',
        'namespace x11_icon',
        '{',
        '// The sizes, in the order of the property, and where each one\'s width is among the values',
        'static const std::size_t kSizeCount = %d;' % len(X11_SIZES),
        'static const std::uint32_t kSizes[kSizeCount] = { %s };' % ', '.join(str(s) for s in X11_SIZES),
        'static const std::size_t kOffsets[kSizeCount] = { %s };' % ', '.join(str(o) for o in offsets),
        '',
        '// The number of values: 2 + width * height for each size',
        'static const std::size_t kValueCount = %d;' % count,
        'static_assert(kValueCount == %s, "x11_icon.h: kValueCount is not the sum of 2 + w * h");'
        % ' + '.join('(2 + %d * %d)' % (s, s) for s in X11_SIZES),
        '',
        '// 64-bit FNV-1a of the values, each as its four bytes, least significant first',
        'static const std::uint64_t kFnv1a64 = 0x%016xull;' % fnv1a64(values),
        '',
        'static const std::uint32_t kValues[] = {',
    ]
    pos = 0
    for size in X11_SIZES:
        lines.append('\t// %d x %d' % (size, size))
        lines.append('\t0x%08x, 0x%08x,' % (values[pos], values[pos + 1]))
        pos += 2
        end = pos + size * size
        while pos < end:
            lines.append('\t' + ' '.join('0x%08x,' % v for v in values[pos:min(pos + 8, end)]))
            pos += 8
    lines += [
        '};',
        'static_assert(sizeof(kValues) / sizeof(kValues[0]) == kValueCount, "x11_icon.h: values are missing or extra");',
        '}',
        '',
    ]
    return '\n'.join(lines).encode('ascii')


def build_all(pngs):
    return [(ICO_PATH, build_ico(pngs)), (ICNS_PATH, build_icns(pngs)), (X11_PATH, build_x11_header(pngs))]


# ---- the structural check of the committed .ico and .icns ---------------------------------------------------------

def png_width(data, at):
    """The IHDR width of the PNG file that starts at byte <at> of <data>, or None if no PNG starts there."""
    if data[at:at + 8] != PNG_SIGNATURE or len(data) < at + 24 or data[at + 12:at + 16] != b'IHDR':
        return None
    return struct.unpack('>I', data[at + 16:at + 20])[0]


def parse_ico(data):
    """Problems of an .ico as this script writes it: count, sizes, offsets, lengths, a PNG at every offset."""
    errors = []
    if len(data) < 6:
        return ['%d bytes, shorter than an ICONDIR' % len(data)]
    reserved, itype, count = struct.unpack('<HHH', data[:6])
    if reserved != 0 or itype != 1:
        errors.append('ICONDIR reserved %d, type %d (0 and 1 expected)' % (reserved, itype))
    if count != len(ICO_SIZES):
        errors.append('%d entries, %d expected' % (count, len(ICO_SIZES)))
    if len(data) < 6 + 16 * count:
        return errors + ['%d entries do not fit in %d bytes' % (count, len(data))]
    expect_offset = 6 + 16 * count
    for i in range(count):
        w, h, colors, res, planes, bits, length, offset = struct.unpack('<BBBBHHII', data[6 + 16 * i:22 + 16 * i])
        size = w if w else 256
        name = 'entry %d' % i
        if i < len(ICO_SIZES) and size != ICO_SIZES[i]:
            errors.append('%s: bWidth %d (%d px), %d px expected there' % (name, w, size, ICO_SIZES[i]))
        if h != w:
            errors.append('%s: bHeight %d differs from bWidth %d' % (name, h, w))
        if colors != 0 or res != 0 or planes != 1 or bits != 32:
            errors.append('%s: bColorCount %d, bReserved %d, wPlanes %d, wBitCount %d (0, 0, 1, 32 expected)'
                          % (name, colors, res, planes, bits))
        if offset != expect_offset:
            errors.append('%s: offset %d, %d expected (the images follow one another)' % (name, offset, expect_offset))
        if offset + length > len(data):
            errors.append('%s: %d bytes at %d run past the end of the file (%d bytes)' % (name, length, offset, len(data)))
        else:
            pw = png_width(data, offset)
            if pw is None:
                errors.append('%s: no PNG at offset %d' % (name, offset))
            elif pw != size:
                errors.append('%s: says %d px, its PNG is %d px wide' % (name, size, pw))
        expect_offset = offset + length
    if not errors and expect_offset != len(data):
        errors.append('the last image ends at %d, the file at %d' % (expect_offset, len(data)))
    return errors


def parse_icns(data):
    """Problems of an .icns as this script writes it: total length, elements, their lengths, a PNG in each."""
    errors = []
    if len(data) < 8 or data[:4] != b'icns':
        return ['no icns header']
    total = struct.unpack('>I', data[4:8])[0]
    if total != len(data):
        errors.append('the header says %d bytes, the file has %d' % (total, len(data)))
    pos = 8
    found = []
    while pos < len(data):
        if pos + 8 > len(data):
            errors.append('an element header is cut off at byte %d' % pos)
            break
        ostype, length = struct.unpack('>4sI', data[pos:pos + 8])
        name = 'element %d (%s)' % (len(found), ostype.decode('ascii', 'replace'))
        if length < 8 or pos + length > len(data):
            errors.append('%s at byte %d: length %d runs past the end of the file (%d bytes)'
                          % (name, pos, length, len(data)))
            break
        found.append(ostype)
        i = len(found) - 1
        pw = png_width(data, pos + 8)
        if pw is None:
            errors.append('%s at byte %d: no PNG in it' % (name, pos))
        elif i < len(ICNS_ELEMENTS) and pw != ICNS_ELEMENTS[i][1]:
            errors.append('%s: its PNG is %d px wide, %d expected' % (name, pw, ICNS_ELEMENTS[i][1]))
        pos += length
    expected = [ostype for ostype, size in ICNS_ELEMENTS]
    if found != expected:
        errors.append('elements %s, %s expected' % (' '.join(t.decode('ascii', 'replace') for t in found),
                                                    ' '.join(t.decode('ascii', 'replace') for t in expected)))
    return errors


# ---- main ---------------------------------------------------------------------------------------------------------

def check(outputs):
    failed = []
    for rel, made in outputs:
        path = top_file(rel)
        if not os.path.isfile(path):
            out('missing: %s' % rel)
            failed.append(rel)
            continue
        have = read_bytes(path)
        if have != made:
            at = next((i for i in range(min(len(have), len(made))) if have[i] != made[i]), min(len(have), len(made)))
            out('differs: %s (%d bytes, the PNGs make %d; the first difference at byte %d)'
                % (rel, len(have), len(made), at))
            failed.append(rel)
        parse = {ICO_PATH: parse_ico, ICNS_PATH: parse_icns}.get(rel)
        if parse:
            for problem in parse(have):
                out('broken: %s: %s' % (rel, problem))
                if rel not in failed:
                    failed.append(rel)
    return failed


def main(argv):
    if argv not in ([], ['--check']):
        out('usage: make_icons.py [--check]')
        return 2
    try:
        pngs = read_pngs()
        outputs = build_all(pngs)
    except IconError as e:
        out('input refused: %s' % e)
        return 1
    if argv == ['--check']:
        failed = check(outputs)
        if failed:
            out('FAILED: %s; "python3 %s" writes them from the PNGs' % (', '.join(failed), GENERATOR))
            return 1
        out('ok: %d PNGs valid; %s identical to what they make, the .ico and .icns well formed'
            % (len(pngs), ', '.join(rel for rel, data in outputs)))
        return 0
    for rel, data in outputs:
        write_bytes(top_file(rel), data)
        out('wrote %s (%d bytes)' % (rel, len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
