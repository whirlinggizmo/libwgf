#!/usr/bin/env python3
"""Measure this game's frame times as libwgf measures its own games: the web export flown by
autopilot/bench.autopilot in a browser, each frame's main-thread work traced by Chrome
(libwgf's tools/bench/measure_frames.py, whose run() this calls: it takes only the games in
libwgf's own games/, by name). Prints the numbers and holds them to the frame budget: a
60 Hz frame, 16.7 ms, at the 95th percentile.

    python3 tools/frames.py [--display xvfb|headless] [--throttle N] [--runs N] [--out FILE]

xvfb (the default) is libwgf's reference: the real GPU under Xvfb, the CPU throttled 4
times. --out writes the numbers as JSON too. Exit 0 within the budget, 1 past it, 2 when
the run failed.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
LIBWGF = Path(os.environ.get('LIBWGF', Path.home() / 'projects/github/whirlinggizmo/libwgf'))
BUDGET_MS = 1000.0 / 60.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--display', choices=['xvfb', 'headless'], default='xvfb')
    ap.add_argument('--throttle', type=float, default=4)
    ap.add_argument('--runs', type=int, default=1)
    ap.add_argument('--out')
    args = ap.parse_args()
    sys.path.insert(0, str(LIBWGF / 'tools' / 'bench'))
    sys.path.insert(0, str(LIBWGF / 'tools'))
    import browser  # noqa: E402  (libwgf's tools)
    import measure_frames  # noqa: E402

    dest = GAME / 'build' / 'frames'
    done = subprocess.run([sys.executable, str(LIBWGF / 'wgf'), 'export', '--web', '--out', str(dest)], cwd=GAME,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    if done.returncode != 0:
        print('frames: the export failed:\n' + '\n'.join(done.stdout.strip().splitlines()[-15:]))
        return 2
    flown = GAME / 'autopilot' / 'bench.autopilot'
    found = browser.find_browser(None)
    runs, gpu = [], ''
    try:
        for _ in range(max(args.runs, 1)):
            numbers, gpu = measure_frames.run(dest / 'web', flown, args.display, args.throttle, found)
            runs.append(numbers)
    except RuntimeError as e:
        print(f'frames: {e}')
        return 2
    row = {k: (round(statistics.median(r[k] for r in runs), 2) if isinstance(runs[0][k], float) else runs[0][k])
           for k in runs[0]}
    print(f'frames: {row["frames"]} frames, ms: mean {row["mean"]}, median {row["median"]}, 95th {row["p95"]}, '
          f'99th {row["p99"]}, worst {row["worst"]}; over 16.7: {row["over16"]}, over 33: {row["over33"]}; '
          f'collections {row["gc"]} ({row["gcTotal"]}, longest {row["gcWorst"]})')
    print(f'frames: on {measure_frames.cpu_name()}, {gpu}, {args.display}, CPU throttled {args.throttle:g}x')
    within = row['p95'] <= BUDGET_MS
    print(f'frames: 95th percentile {row["p95"]} ms {"within" if within else "PAST"} the {BUDGET_MS:.1f} ms budget')
    if args.out:
        Path(args.out).write_text(json.dumps({**row, 'gpu': gpu, 'display': args.display,
                                              'throttle': args.throttle}, indent=1) + '\n')
    return 0 if within else 1


if __name__ == '__main__':
    sys.exit(main())
