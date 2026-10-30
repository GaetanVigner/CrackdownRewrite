"""Reduced 8-bit previews of the 16-bit greyscale depth PNGs the HLE comparison writes.

Usage: depth_preview.py <in.png> <out.png> [factor]
Depth 1.0 (cleared) is drawn black; the written range is stretched to 64..255 so the shapes show.
"""
import struct
import sys
import zlib


def read_grey16(path):
    data = open(path, 'rb').read()
    pos = 8
    width = height = 0
    idat = b''
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos + 4])[0]
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b'IHDR':
            width, height = struct.unpack('>II', body[:8])
        elif kind == b'IDAT':
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = 1 + width * 2
    rows = []
    for y in range(height):
        row = raw[y * stride + 1:(y + 1) * stride]
        rows.append(struct.unpack('>%dH' % width, row))
    return width, height, rows


def write_grey8(path, width, height, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 0, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    factor = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    width, height, rows = read_grey16(src)
    written = [v for r in rows[::factor] for v in r[::factor] if v < 65535]
    lo = min(written) if written else 0
    hi = max(written) if written else 65535
    span = max(hi - lo, 1)
    out = []
    for y in range(0, height, factor):
        row = rows[y]
        out.append([0 if v >= 65535 else 64 + (v - lo) * 191 // span for v in row[::factor]])
    write_grey8(dst, (width + factor - 1) // factor, len(out), out)
    print('%s: %dx%d, written %d of %d sampled, range %.4f..%.4f' % (
        src, width, height, len(written), len(out) * len(out[0]), lo / 65535, hi / 65535))


main()
