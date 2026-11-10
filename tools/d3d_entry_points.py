#!/usr/bin/env python3
"""Lists the title's D3D runtime entry points: the functions of the statically linked D3D runtime that code outside
it calls directly (static call graph of the generated code), into docs/progress/renderer/d3d_entry_points.txt.
tools/progress.py counts the ones the port hooks.

The runtime's range is the one library recognition found (Any360's titles/crackdown/modules.json, "XDK D3D runtime"):
from the first to the last function it shares with the D3D runtime of other titles. Addresses only: no game code.

Usage: python tools/d3d_entry_points.py   (needs generated/default). Standard library only.
"""
import re
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENERATED = ROOT / 'generated' / 'default'
OUT = ROOT / 'docs' / 'progress' / 'renderer' / 'd3d_entry_points.txt'
D3D_RUNTIME = (0x826ACC90, 0x826CF4B0)  # keep in step with tools/progress_map.py
FUNC = re.compile(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)')
CALL = re.compile(r'\bsub_([0-9A-F]{8})\(ctx, base\)')


def inside(address):
    return D3D_RUNTIME[0] <= address < D3D_RUNTIME[1]


def main():
    entry_points = set()
    for path in sorted(GENERATED.glob('crackdown_recomp.*.cpp')):
        text = path.read_text(errors='replace')
        starts = [(m.start(), int(m.group(1), 16)) for m in FUNC.finditer(text)]
        for i, (start, caller) in enumerate(starts):
            if inside(caller):
                continue
            body = text[start:starts[i + 1][0] if i + 1 < len(starts) else len(text)]
            entry_points |= {int(a, 16) for a in CALL.findall(body) if inside(int(a, 16))}
    if not entry_points:
        raise SystemExit(f'no generated code in {GENERATED}')
    lines = [f'# The title\'s D3D runtime entry points: functions in 0x{D3D_RUNTIME[0]:08X}-0x{D3D_RUNTIME[1] - 1:08X} that '
             'game code outside that range calls',
             f'# (static call graph of the generated code, tools/d3d_entry_points.py, {date.today().isoformat()}). '
             'tools/progress.py counts the ones the port hooks.']
    lines += [f'{a:08X}' for a in sorted(entry_points)]
    OUT.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'{len(entry_points)} D3D runtime entry points; wrote {OUT}')


if __name__ == '__main__':
    main()
