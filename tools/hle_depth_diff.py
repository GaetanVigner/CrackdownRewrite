"""Diff image of a native / emulated depth pair over the native pass's box: grey = equal, red = native nearer,
blue = native farther, black = not written natively. Usage: depth_diff.py <preview.py> <emulated> <native> <out> [step]"""
import struct
import sys
import zlib

src = open(sys.argv[1]).read().replace('\nmain()\n', '\n')
ns = {}
exec(compile(src, 'prev', 'exec'), ns)
read = ns['read_grey16']
w, h, emu = read(sys.argv[2])
_, _, nat = read(sys.argv[3])
step = int(sys.argv[5]) if len(sys.argv) > 5 else 2
box = [w, h, 0, 0]
for y in range(0, h, 8):
    for x in range(0, w, 8):
        if nat[y][x] < 65535:
            box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
x0, y0, x1, y1 = box[0], box[1], min(w, box[2] + 8), min(h, box[3] + 8)
rows = []
counts = {'equal': 0, 'nearer': 0, 'farther': 0}
for y in range(y0, y1, step):
    row = []
    for x in range(x0, x1, step):
        n, e = nat[y][x], emu[y][x]
        if n >= 65535:
            row += [0, 0, 0]
        elif abs(n - e) <= 1:
            row += [110, 110, 110]
            counts['equal'] += 1
        elif n < e:
            row += [230, 40, 40]
            counts['nearer'] += 1
        else:
            row += [40, 90, 230]
            counts['farther'] += 1
    rows.append(row)
width = len(rows[0]) // 3
raw = b''.join(b'\x00' + bytes(r) for r in rows)


def chunk(kind, body):
    return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)


png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, len(rows), 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')
open(sys.argv[4], 'wb').write(png)
print('box', box, 'size', width, len(rows), counts)
