#!/usr/bin/env python3
"""Summarise a perf recorder file from the console (debug/perf-NNNN.csv).

    platform/3ds/tools/perf-analyze.py FILE.csv [--worst N] [--by-state]

Prints the frame rate and its percentiles, how many frames missed a 60 Hz vblank, what each
part of the frame costs (mean, p95, max), and the worst frames with their parts, so it is
clear whether a slow frame was the game logic, the 3D view, the SoftPoly raster, the 2D, the
3DS compositor, the presenter or the bottom screen. Columns: src/common/platform/3ds/perf_3ds.h.
From ../zelda-tmc-3ds/tools/perf-csv/analyze.py. Standard library only.
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict

PARTS = ['logic_ms', 'world_ms', 'raster_wait_ms', 'twod_ms', 'compose_ms', 'present_ms', 'limit_ms',
         'bottom_ms', 'other_ms']
TEXT = ('state',)
FRAME_MS = 1000.0 / 59.8261  # the 3DS LCD


def pct(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    k = min(len(s) - 1, max(0, int(round(p / 100.0 * (len(s) - 1)))))
    return s[k]


def load(path):
    header = None
    with open(path, newline='') as f:
        lines = f.read().splitlines()
    if lines and lines[0].startswith('#'):
        header = lines[0][1:].strip()
    rows = []
    for r in csv.DictReader(l for l in lines if not l.startswith('#')):
        for k, v in r.items():
            if k not in TEXT:
                r[k] = float(v)
        rows.append(r)
    return header, rows


def summarise(rows, worst=10, by_state=False, out=sys.stdout):
    if len(rows) < 2:
        print('not enough rows', file=out)
        return
    frames = rows[1:]  # the first row's interval starts at the recorder's start
    intervals = [r['interval_ms'] for r in frames]
    total_s = (rows[-1]['t_ms'] - rows[0]['t_ms']) / 1000.0
    tics = sum(r['tics'] for r in frames)
    print(f'{len(frames)} frames in {total_s:.1f} s: {len(frames) / total_s:.2f} fps, '
          f'{tics / total_s:.2f} game tics/s (35 is full speed)', file=out)
    over1 = sum(1 for v in intervals if v > FRAME_MS * 1.5)
    over2 = sum(1 for v in intervals if v > FRAME_MS * 2.5)
    print(f'interval ms: mean {statistics.mean(intervals):.2f}  p50 {pct(intervals, 50):.2f}  '
          f'p95 {pct(intervals, 95):.2f}  p99 {pct(intervals, 99):.2f}  max {max(intervals):.2f}  '
          f'(1% low {1000.0 / pct(intervals, 99):.1f} fps)', file=out)
    print(f'missed vblanks: {over1} frames took 2+ vblanks ({100.0 * over1 / len(frames):.1f}%), '
          f'{over2} took 3+', file=out)
    sizes = sorted({(int(r['canvas_w']), int(r['canvas_h'])) for r in frames})
    print('canvas: ' + ', '.join(f'{w}x{h}' for w, h in sizes), file=out)
    print(file=out)
    print(f'{"part":<16}{"mean":>8}{"p95":>8}{"max":>8}{"share":>8}', file=out)
    mean_interval = statistics.mean(intervals)
    for p in PARTS:
        vals = [r[p] for r in frames]
        print(f'{p:<16}{statistics.mean(vals):>8.2f}{pct(vals, 95):>8.2f}{max(vals):>8.2f}'
              f'{100.0 * statistics.mean(vals) / mean_interval:>7.1f}%', file=out)
    print(file=out)
    print(f'worst {worst} frames by interval:', file=out)
    print(f'{"frame":>7}{"interval":>9}' + ''.join(f'{p[:-3][:9]:>10}' for p in PARTS) + '  tics state', file=out)
    for r in sorted(frames, key=lambda r: -r['interval_ms'])[:worst]:
        print(f'{int(r["frame"]):>7}{r["interval_ms"]:>9.2f}' + ''.join(f'{r[p]:>10.2f}' for p in PARTS)
              + f'  {int(r["tics"]):>4} {r["state"]}{" menu" if r["menu"] else ""}', file=out)
    if by_state:
        print(file=out)
        by = defaultdict(list)
        for r in frames:
            by[r['state'] + (' + menu' if r['menu'] else '')].append(r)
        print(f'{"state":<20}{"frames":>7}{"fps":>8}{"p95 ms":>8}' + ''.join(f'{p[:-3][:9]:>10}' for p in PARTS),
              file=out)
        for key, group in sorted(by.items(), key=lambda kv: -len(kv[1])):
            vals = [r['interval_ms'] for r in group]
            print(f'{key:<20}{len(group):>7}{1000.0 / statistics.mean(vals):>8.2f}{pct(vals, 95):>8.2f}'
                  + ''.join(f'{statistics.mean(r[p] for r in group):>10.2f}' for p in PARTS), file=out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('csv')
    ap.add_argument('--worst', type=int, default=10, help='how many of the slowest frames to list')
    ap.add_argument('--by-state', action='store_true', help='frame rate and parts per game state / menu')
    a = ap.parse_args()
    header, rows = load(a.csv)
    if header:
        print(header)
    summarise(rows, a.worst, a.by_state)


if __name__ == '__main__':
    main()
