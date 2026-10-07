#!/usr/bin/env python3
"""Measure what a call from JS into libwgf's wasm host costs, in a headless browser on
the release host.

    tools/bench/measure_calls.py [--runs N] [--browser PATH]

Builds tools/bench/calls/Main.hx (Haxe, the JS target, as a game's release build is) into
the full host's page, and main.js (the same calls in JS) beside the same host, loads
each, and reads its lines: for each shape and each, the median of its runs of 200,000
calls, in ns per call. Both reach the host through the JS binding (bindings/js/wgf.js):
the Haxe column is the binding and the Haxe binding over it, the JS column the JS
binding alone.
  set        wgf_actor_set_position: a handle and three floats in, a bool out
  get        wgf_actor_get_position: a vec3 out, into a kept vector
  transform  wgf_actor_set_transform: nine floats
  string     wgf_actor_set_name: a string in (copied, no search), a bool out
  bulk       wgf_actor_get_positions over 1,000 actors (an array in, one out);
             bulk-actor is the same per actor
Each page load is one run; with --runs, each shape's median over the loads, and every
load's number after it. Timing, so not a check: CI doesn't run it. Standard library only.
"""
import argparse
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # an embedded Python (Windows) doesn't add it
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'wgf'))
import binding  # noqa: E402
import browser  # noqa: E402
import game as games  # noqa: E402  (the wgf tool's: a page run until a line)
import variants  # noqa: E402
import webhost  # noqa: E402

PROGRAM = Path(__file__).resolve().parent / 'calls'
MARK = 'calls: '
VERDICT = 'calls done: '
SHAPES = ('set', 'get', 'transform', 'string', 'bulk', 'bulk-actor')


def numbers(output):
    """shape -> ns per call, from a run's lines."""
    found = {}
    for line in output.splitlines():
        if MARK not in line:
            continue
        words = line.split(MARK, 1)[1].split()
        if len(words) == 2 and words[0] in SHAPES:
            found[words[0]] = float(words[1])
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--runs', type=int, default=3, help='page loads (default 3)')
    ap.add_argument('--browser', help='a Chromium-based browser (default: found)')
    args = ap.parse_args()
    if binding.haxe() is None:
        print('measure_calls: no haxe on PATH')
        return 1
    try:
        webhost.emcc()
    except RuntimeError as e:
        print(f'measure_calls: {e}')
        return 1
    try:
        found = browser.find_browser(args.browser)
    except RuntimeError as e:
        print(f'measure_calls: {e}')
        return 1
    site = binding.work_dir('calls-js') / 'site'
    webhost.build_js_example(PROGRAM, variants.web(debug=False), site)
    runs = {'haxe': [], 'js': []}
    for _ in range(max(args.runs, 1)):
        passed, output = binding.run_browser('calls', PROGRAM, VERDICT, found, release=True, timeout=300)
        lines = games.run_page(site, 'index.html', None, until=(VERDICT,), browser_path=found, echo=False,
                               timeout=300)
        if not passed or not any(VERDICT + 'PASS' in line for line in lines):
            print('\n'.join(output.strip().splitlines()[-10:] + lines[-10:]))
            print('measure_calls: FAIL')
            return 1
        runs['haxe'].append(numbers(output))
        runs['js'].append(numbers('\n'.join(lines)))
    print(f'measure_calls: ns per call, the median of {args.runs} page load(s) each (each load\'s after it)')
    print(f'  {"shape":<12} {"haxe":>9} {"js":>9}')
    for shape in SHAPES:
        cells, each = [], []
        for column in ('haxe', 'js'):
            values = [run[shape] for run in runs[column] if shape in run]
            cells.append(f'{statistics.median(values):9.2f}' if values else f'{"-":>9}')
            each.append(', '.join(f'{n:.2f}' for n in values))
        print(f'  {shape:<12} {cells[0]} {cells[1]}   (haxe {each[0]}; js {each[1]})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
