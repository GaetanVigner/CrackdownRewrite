"""Checks config/tu6/address-map.csv against the two guest images.

  python tools/verify-address-map.py [<base image> <title update 6 image>]

The images are written by crackdown_dump_image (see tools/codegen.ps1 and tools/codegen-tu6.ps1); the defaults are
game/extracted/default_image.bin and out/tu6/default_image.bin. Nothing is checked against an image whose SHA-256 is not
the pinned one, so a different update can never be verified against these mappings.

Function rows must still look like the same function (instruction skeleton similarity not below the row's ratio minus
0.02), and site and return rows must still hold the recorded instruction word.
"""
import csv
import difflib
import hashlib
import os
import re
import struct
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
MAP = os.path.join(ROOT, 'config', 'tu6', 'address-map.csv')
BASE_SHA256 = 'e2e74dd9905dbbb73e9a6769b78726fba8e8abc508149d51a6c7c983f92575f5'
TU6_SHA256 = 'c5fa7e119b08981aac05e3cc43779a1b4d1efc49cc132ff6bb6bae20d102e872'
IMAGE_BASE = 0x82000000


class Image:
    def __init__(self, path, sha256):
        data = open(path, 'rb').read()
        if hashlib.sha256(data).hexdigest() != sha256:
            raise SystemExit('%s is not the pinned guest image (sha256 %s)' % (path, sha256))
        self.data = data

    def word(self, address):
        offset = address - IMAGE_BASE
        return struct.unpack('>I', self.data[offset:offset + 4])[0]

    def function_words(self, start, size):
        return [self.word(start + 4 * i) for i in range(size // 4)]


def skeleton(word):
    op = word >> 26
    if op == 18:
        return (18, word & 3)
    if op == 16:
        return (16, (word >> 16) & 0x3FF, word & 1)
    if op in (14, 15, 7, 8, 10, 11, 12, 13, 24, 25, 26, 27, 28, 29) or 32 <= op <= 55:
        return (op, (word >> 16) & 0x3FF)
    return (op, word & 0x3FFFFFF)


def similarity(base, tu6, row):
    a = [skeleton(w) for w in base.function_words(int(row['tu0'], 16), int(row['tu0_size']))]
    b = [skeleton(w) for w in tu6.function_words(int(row['tu6'], 16), int(row['tu6_size']))]
    return difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()


def main():
    base_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'game', 'extracted', 'default_image.bin')
    tu6_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'out', 'tu6', 'default_image.bin')
    base = Image(base_path, BASE_SHA256)
    tu6 = Image(tu6_path, TU6_SHA256)
    failures = 0
    checked = 0
    with open(MAP, newline='') as handle:
        for row in csv.DictReader(handle):
            kind, name = row['kind'], row['name']
            if kind in ('fn', 'call') and row['tu6']:
                want = re.search(r'ratio (\d\.\d\d)', row['evidence'])
                got = similarity(base, tu6, row)
                if want and got < float(want.group(1)) - 0.02:
                    print('MISMATCH %s %s: similarity %.2f, recorded %s' % (kind, name, got, want.group(1)))
                    failures += 1
                checked += 1
            elif kind in ('site', 'return'):
                for column, image, word in (('tu0', base, 'tu0_word'), ('tu6', tu6, 'tu6_word')):
                    if row[column]:
                        got = image.word(int(row[column], 16))
                        if got != int(row[word], 16):
                            print('MISMATCH %s %s %s: word %08X, recorded %s' % (kind, name, column, got, row[word]))
                            failures += 1
                        checked += 1
    print('checked %d entries, %d mismatches' % (checked, failures))
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
