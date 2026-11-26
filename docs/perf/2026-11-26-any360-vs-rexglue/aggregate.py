"""Aggregate Crackdown 'guest frames' periods per variant from bench runs output.

  python agg.py <runs file> <logs dir> [--max-fps 600] [--skip 1]

City periods: the guest-frames lines after the route (frame rate under --max-fps: the title screen and loading run
far above it at an unlimited rate, or at 0 while loading), the first --skip of them dropped as warm-up. Per launch:
the period with the lowest 1% low is its dip; the others are steady. Per variant: medians and means of the steady
periods' figures; each variant against the first by a Mann-Whitney rank test (z) over the launches' steady means.
"""
import argparse
import math
import re
import statistics
from pathlib import Path

RUN = re.compile(r'^﻿?=== (?P<variant>\S+) \((?P<log>[^()]+\.log)\)')
PRIV = re.compile(r'private at end: (?P<mb>\d+) MB')
LINE = re.compile(r'Crackdown: guest frames: (?P<fps>[\d.]+) FPS over [\d.]+ s; frame times: median (?P<median>[\d.]+) ms '
                  r'\([\d.]+ FPS\), 10% low (?P<p10>[\d.]+) FPS, 1% low (?P<p1>[\d.]+) FPS; slowest frame '
                  r'(?P<slowest>[\d.]+) ms; (?P<private>\d+) MB private')


def ranks_z(a, b):
    both = sorted([(x, 0) for x in a] + [(x, 1) for x in b])
    r = [0.0] * len(both)
    i = 0
    while i < len(both):
        j = i
        while j + 1 < len(both) and both[j + 1][0] == both[i][0]:
            j += 1
        for k in range(i, j + 1):
            r[k] = (i + j) / 2 + 1
        i = j + 1
    n1, n2 = len(a), len(b)
    r1 = sum(rk for rk, (_, g) in zip(r, both) if g == 0)
    u = r1 - n1 * (n1 + 1) / 2
    mu, sd = n1 * n2 / 2, math.sqrt(n1 * n2 * (n1 + n2 + 1) / 12)
    return (u - mu) / sd if sd else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('runs')
    ap.add_argument('logs')
    ap.add_argument('--max-fps', type=float, default=600)
    ap.add_argument('--skip', type=int, default=1)
    args = ap.parse_args()
    launches = []
    cur = None
    for line in Path(args.runs).read_text(encoding='utf-8', errors='replace').splitlines():
        m = RUN.match(line)
        if m:
            cur = {'variant': m['variant'], 'log': m['log'], 'private_end': None}
            launches.append(cur)
            continue
        m = PRIV.search(line)
        if m and cur is not None and cur['private_end'] is None:
            pass
    # private at end follows the === line in bench output
    text = Path(args.runs).read_text(encoding='utf-8', errors='replace').splitlines()
    for i, line in enumerate(text):
        m = RUN.match(line)
        if m and i + 1 < len(text):
            p = PRIV.search(text[i + 1])
            for l in launches:
                if l['log'] == m['log'] and p:
                    l['private_end'] = int(p['mb'])
    by = {}
    print('launch log                     variant   periods  steady-mean-FPS  median-ms  p10  p1   dip-p1  private-end')
    for l in launches:
        periods = []
        for line in (Path(args.logs) / l['log']).read_text(encoding='utf-8', errors='replace').splitlines():
            m = LINE.search(line)
            if m and float(m['fps']) < args.max_fps and float(m['fps']) > 1:
                periods.append({k: float(v) for k, v in m.groupdict().items()})
        periods = periods[args.skip:]
        if len(periods) < 2:
            print(f"{l['log']:30} {l['variant']:9} {len(periods)} periods: dropped (did not reach the city?)")
            continue
        dip = min(periods, key=lambda p: p['p1'])
        steady = [p for p in periods if p is not dip]
        s = {k: statistics.mean(p[k] for p in steady) for k in ('fps', 'median', 'p10', 'p1', 'private')}
        s['dip_p1'] = dip['p1']
        s['private_end'] = l['private_end']
        by.setdefault(l['variant'], []).append(s)
        print(f"{l['log']:30} {l['variant']:9} {len(periods):3}      {s['fps']:8.1f}        {s['median']:6.2f}  "
              f"{s['p10']:5.1f} {s['p1']:5.1f} {dip['p1']:6.1f}  {l['private_end']}")
    print()
    first = None
    for v, runs in by.items():
        med = {k: statistics.median(r[k] for r in runs) for k in ('fps', 'median', 'p10', 'p1', 'private', 'dip_p1')}
        ends = [r['private_end'] for r in runs if r['private_end']]
        print(f"{v}: {len(runs)} launches; avg FPS {med['fps']:.1f} (range {min(r['fps'] for r in runs):.1f}-"
              f"{max(r['fps'] for r in runs):.1f}), median frame {med['median']:.2f} ms ({1e3 / med['median']:.1f} FPS), "
              f"10% low {med['p10']:.1f}, 1% low {med['p1']:.1f}, dip 1% low {med['dip_p1']:.1f}, private "
              f"{med['private']:.0f} MB (log), {statistics.median(ends) if ends else 0:.0f} MB (process at end)")
        if first is None:
            first = (v, runs)
        else:
            for k in ('fps', 'p10', 'p1', 'private'):
                z = ranks_z([r[k] for r in runs], [r[k] for r in first[1]])
                print(f"   {k}: z = {z:+.2f} against {first[0]}")


main()
