#!/usr/bin/env python3
"""Progress map of the native port: what the port already does natively and what still runs on emulation.

Writes docs/progress/progress-map.svg (treemaps) and docs/progress/PROGRESS.md (tables), with the counts kept in
docs/progress/progress-map.json so the map can be redrawn where the generated code is not present:
- Xbox system calls: every kernel / XAM import of default.xex (generated/default/crackdown_init.cpp), by library,
  as the ReXGlue runtime provides it (implemented, stub, missing) - each one a native PC replacement to write
  for a full native port;
- game code: the recompiled functions by address area, with those the port replaces natively (CRACKDOWN_BIND /
  REX_HOOK in src/) and those checked against the recompiled code in a sandbox (tests/golden);
- render passes: each of the title's tagged passes by native renderer stage (docs/progress/measurements.json);
- D3D runtime entry points the port hooks (docs/progress/d3d_entry_points.txt).
Only counts and public API names are written: no game code or data.

Usage: python tools/progress_map.py   (tools/progress.py runs it too). Standard library only.
"""
import json
import re
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'docs' / 'progress'
GENERATED = ROOT / 'generated' / 'default'
SDK_KERNEL = ROOT / 'out' / 'rexglue-sdk-src' / 'src' / 'kernel'
HOOK = re.compile(r'(?:CRACKDOWN_BIND(?:_REPLACEMENT)?\(|REX_HOOK(?:_RAW)?\(sub_)(?:0x)?([0-9A-Fa-f]{8})\b')
D3D_RUNTIME = (0x826A0000, 0x826D0000)

STAGES = [  # render pass stages, in order
    ('emulated', '#6e7681', 'drawn by the GPU emulation'),
    ('records', '#1f6feb', 'draw records exact'),
    ('dry run', '#8957e5', 'native pipelines built'),
    ('native draw', '#bf8700', 'drawn natively, not yet matching'),
    ('validated', '#2da44e', 'native image matches emulation'),
    ('native', '#1a7f37', 'shown from the native renderer'),
]
CALL_STATES = [('implemented', '#2da44e'), ('stub', '#bf8700'), ('missing', '#cf222e')]
FUNCTION_STATES = [('native', '#2da44e', 'replaced natively'), ('verified', '#1f6feb', 'checked in a sandbox'),
                   ('recompiled', '#30363d', 'recompiled guest code')]


def system_calls():
    init = GENERATED / 'crackdown_init.cpp'
    if not init.exists() or not SDK_KERNEL.exists():
        return None
    names = sorted(set(re.findall(r'\{ 0x[0-9A-Fa-f]+, __imp__([A-Za-z0-9_]+) \}', init.read_text(errors='replace'))))
    exports = {}
    for path in SDK_KERNEL.rglob('*.cpp'):
        library = path.relative_to(SDK_KERNEL).parts[0]
        for macro, name in re.findall(r'REX_EXPORT(_STUB[A-Z_]*)?\(__imp__([A-Za-z0-9_]+)', path.read_text(errors='replace')):
            state = 'stub' if macro else 'implemented'
            if exports.get(name, ('', ''))[1] != 'implemented':
                exports[name] = (library, state)
    calls = {}
    for name in names:
        library, state = exports.get(name, ('xam' if name.startswith(('Xam', 'XNet', 'NetDll', 'XMsg', 'XNotify'))
                                            else 'xboxkrnl', 'missing'))
        calls.setdefault(library, []).append({'name': name, 'state': state})
    return calls


def functions():
    files = sorted(GENERATED.glob('crackdown_recomp.*.cpp'))
    if not files:
        return None
    addresses = []
    for path in files:
        addresses += [int(a, 16) for a in re.findall(r'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)', path.read_text(errors='replace'))]
    hooked = set()
    for path in (ROOT / 'src').rglob('*'):
        if path.suffix in ('.cpp', '.h', '.inc'):
            hooked |= {int(a, 16) for a in HOOK.findall(path.read_text(errors='replace'))}
    verified = set()
    golden = ROOT / 'tests' / 'golden'
    for path in golden.glob('*.txt'):
        for line in path.read_text().splitlines():
            if line and not line.startswith('#'):
                verified.add(int(line.split()[0], 16))
    areas = {}
    for address in addresses:
        if D3D_RUNTIME[0] <= address < D3D_RUNTIME[1]:
            area = 'D3D runtime'
        else:
            area = f'code {address >> 20:03X}xxxxx'
        counts = areas.setdefault(area, {'native': 0, 'verified': 0, 'recompiled': 0})
        if address in hooked:
            counts['native'] += 1
        elif address in verified:
            counts['verified'] += 1
        else:
            counts['recompiled'] += 1
    return dict(sorted(areas.items()))


# --- squarified treemap ------------------------------------------------------------------------------------------

def squarify(items, x, y, w, h):
    """items: [(key, weight)] sorted by weight descending -> [(key, x, y, w, h)]."""
    total = sum(weight for _, weight in items)
    if not items or total <= 0 or w <= 0 or h <= 0:
        return []
    scale = w * h / total
    areas = [(key, weight * scale) for key, weight in items]
    out = []

    def worst(row, side):
        s = sum(a for _, a in row)
        return max(max(side * side * a / (s * s), (s * s) / (side * side * a)) for _, a in row)

    while areas:
        side = min(w, h)
        row = [areas.pop(0)]
        while areas and worst(row + [areas[0]], side) <= worst(row, side):
            row.append(areas.pop(0))
        s = sum(a for _, a in row)
        if w >= h:
            cw = s / h
            cy = y
            for key, a in row:
                out.append((key, x, cy, cw, a / cw))
                cy += a / cw
            x += cw
            w -= cw
        else:
            ch = s / w
            cx = x
            for key, a in row:
                out.append((key, cx, y, a / ch, ch))
                cx += a / ch
            y += ch
            h -= ch
    return out


def esc(text):
    return str(text).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def treemap_panel(out, x0, y0, w, h, title, groups, colors):
    """groups: [(name, [(state, count)])]: a block per group, its cells coloured by state."""
    total = sum(c for _, cells in groups for _, c in cells)
    done = sum(c for _, cells in groups for s, c in cells if s == colors[0][0])
    out.append(f'<text class="h" x="{x0}" y="{y0 - 10}">{esc(title)}: {done / max(total, 1) * 100:.1f}% '
               f'({done:,} of {total:,})</text>')
    out.append(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" fill="#0d1117"/>')
    blocks = squarify(sorted(((name, sum(c for _, c in cells)) for name, cells in groups), key=lambda i: -i[1]),
                      x0, y0, w, h)
    cells_by = dict(groups)
    color_of = {state: color for state, color, *_ in colors}
    for name, bx, by, bw, bh in blocks:
        cells = [(s, c) for s, c in cells_by[name] if c]
        # Cells as a grid: about one square per item, capped so large areas stay readable.
        count = sum(c for _, c in cells)
        per = max(1, count // 400)
        units = []
        for state, c in cells:
            units += [state] * max(1, round(c / per))
        cols = max(1, round((len(units) * bw / max(bh, 1)) ** 0.5))
        rows = max(1, -(-len(units) // cols))
        cw, ch = bw / cols, bh / rows
        for i, state in enumerate(units):
            cx, cy = bx + (i % cols) * cw, by + (i // cols) * ch
            out.append(f'<rect x="{cx:.1f}" y="{cy:.1f}" width="{max(cw - .6, .3):.1f}" height="{max(ch - .6, .3):.1f}" '
                       f'fill="{color_of[state]}"/>')
        share = sum(c for s, c in cells if s == colors[0][0]) / max(count, 1) * 100
        out.append(f'<rect x="{bx:.1f}" y="{by:.1f}" width="{bw:.1f}" height="{bh:.1f}" fill="none" stroke="#0d1117" '
                   f'stroke-width="2"><title>{esc(name)}: {count:,}, {share:.0f}% {colors[0][0]}</title></rect>')
        if bw > 46 and bh > 14:
            out.append(f'<text x="{bx + 4:.1f}" y="{by + 12:.1f}" fill="#ffffff" font-size="10" '
                       f'paint-order="stroke" stroke="#0d1117" stroke-width="3">{esc(name)}</text>')


def legend(out, x, y, entries, spacing=150, per_row=10):
    for i, (label, color) in enumerate(entries):
        lx, ly = x + (i % per_row) * spacing, y + (i // per_row) * 16
        out.append(f'<rect x="{lx}" y="{ly}" width="11" height="11" rx="2" fill="{color}"/>')
        out.append(f'<text class="t" x="{lx + 16}" y="{ly + 10}">{esc(label)}</text>')


def main():
    cache_path = OUT / 'progress-map.json'
    cache = json.loads(cache_path.read_text(encoding='utf-8')) if cache_path.exists() else {}
    calls = system_calls() or cache.get('system_calls', {})
    areas = functions() or cache.get('functions', {})
    measurements = json.loads((OUT / 'measurements.json').read_text(encoding='utf-8'))
    passes = measurements.get('passes', {})
    api = {line.strip().upper() for line in (OUT / 'd3d_entry_points.txt').read_text().splitlines()
           if line.strip() and not line.startswith('#')}
    hooked_api = set()
    for path in (ROOT / 'src').rglob('*'):
        if path.suffix in ('.cpp', '.h', '.inc'):
            hooked_api |= {a.upper() for a in HOOK.findall(path.read_text(errors='replace'))} & api
    cache_path.write_text(json.dumps({'system_calls': calls, 'functions': areas}, indent=1) + '\n', encoding='utf-8')

    width = 1100
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="900" viewBox="0 0 {width} 900" '
           'font-family="Segoe UI, Helvetica, Arial, sans-serif" font-size="12">',
           '<style>.t{fill:#8b949e}.h{fill:#c9d1d9;font-size:14px;font-weight:600}</style>',
           '<rect width="100%" height="100%" fill="#161b22"/>',
           f'<text class="h" x="20" y="28" font-size="17">Crackdown native port: progress map</text>',
           f'<text class="t" x="20" y="46">Generated by tools/progress_map.py on {date.today().isoformat()}. '
           'Green: native. Tables: docs/progress/PROGRESS.md</text>']
    # Game code.
    groups = [(area, [(s, counts.get(s, 0)) for s, *_ in FUNCTION_STATES]) for area, counts in areas.items()]
    treemap_panel(out, 20, 80, 640, 420, 'Game code functions natively replaced', groups, FUNCTION_STATES)
    legend(out, 20, 508, [(f'{s}: {d}', c) for s, c, d in FUNCTION_STATES], spacing=215)
    # System calls.
    groups = [(lib, [(s, sum(1 for c in items if c['state'] == s)) for s, _ in CALL_STATES])
              for lib, items in calls.items()]
    treemap_panel(out, 690, 80, 390, 420, 'Xbox system calls provided', groups, CALL_STATES)
    legend(out, 690, 508, [(s, c) for s, c in CALL_STATES], spacing=120)
    out.append('<text class="t" x="690" y="538">today from the ReXGlue runtime: each a PC implementation '
               'to write</text>')
    # Render passes.
    y = 570
    stage_index = {name: i for i, (name, *_) in enumerate(STAGES)}
    names = measurements.get('pass_names', [])
    out.append(f'<text class="h" x="20" y="{y - 10}">Render passes by native renderer stage '
               f'({sum(1 for n in names if passes.get(n, "emulated") in ("validated", "native"))} of {len(names)} '
               f'native)</text>')
    cols = 10
    cw, ch = 106, 44
    for i, name in enumerate(names):
        stage = passes.get(name, 'emulated')
        color = STAGES[stage_index.get(stage, 0)][1]
        cx, cy = 20 + (i % cols) * (cw + 2), y + (i // cols) * (ch + 2)
        out.append(f'<rect x="{cx}" y="{cy}" width="{cw}" height="{ch}" rx="3" fill="{color}">'
                   f'<title>{esc(name)}: {stage}</title></rect>')
        out.append(f'<text x="{cx + 5}" y="{cy + 17}" fill="#ffffff" font-size="11">{esc(name)}</text>')
        out.append(f'<text x="{cx + 5}" y="{cy + 33}" fill="#ffffff" fill-opacity=".75" font-size="10">{stage}</text>')
    ly = y + 3 * (ch + 2) + 12
    legend(out, 20, ly, [(f'{stage}: {desc}', color) for stage, color, desc in STAGES], spacing=350, per_row=3)
    ly += 16
    # D3D entry points.
    y = ly + 50
    out.append(f'<text class="h" x="20" y="{y - 10}">D3D runtime entry points hooked by the port: {len(hooked_api)} '
               f'of {len(api)}</text>')
    for i, address in enumerate(sorted(api)):
        cx, cy = 20 + (i % 70) * 15, y + (i // 70) * 15
        color = '#2da44e' if address in hooked_api else '#30363d'
        out.append(f'<rect x="{cx}" y="{cy}" width="13" height="13" rx="2" fill="{color}"><title>sub_{address}'
                   f'</title></rect>')
    height = y + (len(api) // 70 + 1) * 15 + 20
    out[0] = out[0].replace('height="900" viewBox="0 0 1100 900"', f'height="{height}" viewBox="0 0 {width} {height}"')
    out.append('</svg>')
    (OUT / 'progress-map.svg').write_text('\n'.join(out) + '\n', encoding='utf-8')

    # Tables.
    md = ['# Native port progress', '',
          f'Generated by `tools/progress_map.py` on {date.today().isoformat()} (run by `tools/progress.py` with every '
          'commit). Raw numbers: [progress-map.json](progress-map.json).', '',
          '![Progress map](progress-map.svg)', '',
          '## Xbox system calls', '',
          'Every kernel and XAM function `default.xex` imports. For now the ReXGlue runtime provides them on PC; '
          'a full native port replaces each with a PC implementation.', '',
          '| Library | Implemented | Stub | Missing | Total | % |', '|---|---|---|---|---|---|']
    for lib, items in calls.items():
        n = {s: sum(1 for c in items if c['state'] == s) for s, _ in CALL_STATES}
        md.append(f'| {lib} | {n["implemented"]} | {n["stub"]} | {n["missing"]} | {len(items)} | '
                  f'{n["implemented"] / len(items) * 100:.0f}% |')
    md += ['', '<details><summary>Every call</summary>', '', '| Library | Function | State |', '|---|---|---|']
    for lib, items in calls.items():
        md += [f'| {lib} | `{c["name"]}` | {c["state"]} |' for c in items]
    md += ['', '</details>', '', '## Game code', '',
           'The recompiled functions of `default.xex` by address area: natively replaced (hooked to a PC '
           'implementation), verified (outputs checked against the recompiled code in the guest sandbox tests), '
           'the rest recompiled guest code.', '',
           '| Area | Native | Verified | Recompiled | Total | % native |', '|---|---|---|---|---|---|']
    for area, c in areas.items():
        total = sum(c.values())
        md.append(f'| {area} | {c["native"]} | {c["verified"]} | {c["recompiled"]} | {total} | '
                  f'{c["native"] / total * 100:.1f}% |')
    md += ['', '## Render passes', '', '| Pass | Stage |', '|---|---|']
    md += [f'| {name} | {passes.get(name, "emulated")} |' for name in names]
    md += ['', f'## D3D runtime entry points', '', f'{len(hooked_api)} of {len(api)} hooked.', '']
    (OUT / 'PROGRESS.md').write_text('\n'.join(md), encoding='utf-8')
    total_functions = sum(sum(c.values()) for c in areas.values())
    native_functions = sum(c['native'] for c in areas.values())
    total_calls = sum(len(i) for i in calls.values())
    implemented = sum(1 for i in calls.values() for c in i if c['state'] == 'implemented')
    print(f'progress map: {native_functions} of {total_functions} functions native, {implemented} of {total_calls} '
          f'system calls implemented, {len(names)} passes; wrote {OUT / "progress-map.svg"} and {OUT / "PROGRESS.md"}')


if __name__ == '__main__':
    main()
