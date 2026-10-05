#!/usr/bin/env python3
"""Run every C example built headless for a while, and fail any that crashes, hangs,
or logs an error.

    tools/run_smoke.py [--variant NAME] [--frames N] [--runner CMD] [example ...]

Builds the variant (default this machine's debug-headless, linux-x64-debug-headless on Linux), stages it, builds the examples
against it (tools/examples.py), then runs each for N frames
(default 180; headless frames run at 60 a second, so about 3 s), all at once. An
example fails on a non-zero exit, on running past its time, or on an [ERROR] or
[FATAL] line, a sokol panic ([panic]), or ABORTING in its log: sokol can panic and
still exit 0. --runner runs each under a command; a Windows variant built elsewhere
(windows-x64-mingw-debug-headless on Linux) runs under tools/run_wine.py unless told
otherwise. Headless means no window and no GPU, so nothing is drawn: tools/check_web.py
runs the examples on the web, with screenshots. Standard library only. From
wgrender's tools/run_smoke_test.py.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import examples  # noqa: E402
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROBLEM = re.compile(r'\[(ERROR|FATAL)|\[panic\]|ABORTING')  # in the program's output


def run(binary, frames, timeout, runner):
    """(exit code, or None when it ran out of time; its log; seconds)."""
    env = dict(os.environ, LIBWGF_HEADLESS_FRAMES=str(frames))
    start = time.monotonic()
    try:
        done = subprocess.run([*runner, str(binary)], cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=timeout)
        code, log = done.returncode, done.stdout
    except subprocess.TimeoutExpired as e:
        code, log = None, e.stdout or b''
    return code, log.decode(errors='replace'), time.monotonic() - start


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--variant', help="default: this machine's debug-headless preset")
    parser.add_argument('--frames', type=int, default=180)
    parser.add_argument('--runner', help='a command to run each example under (default: Wine for a Windows variant '
                        'built elsewhere)')
    parser.add_argument('examples', nargs='*')
    args = parser.parse_args()
    args.variant = args.variant or variants.native('debug-headless')
    if args.frames < 1:
        sys.exit('run_smoke: --frames is 1 or more')
    if not variants.is_headless(args.variant):
        sys.exit(f'run_smoke: {args.variant} isn\'t a headless variant: its examples would open windows')
    names = args.examples or examples.names()
    unknown = [n for n in names if n not in examples.names()]
    if unknown:
        sys.exit(f'run_smoke: no such example: {", ".join(unknown)}')

    try:
        examples.prepare(args.variant)
        binaries = [examples.build(args.variant, n) for n in names]
    except RuntimeError as e:
        sys.exit(f'run_smoke: {e}')
    if args.runner is not None:
        runner = shlex.split(args.runner)
    elif args.variant.startswith('windows-') and os.name != 'nt':
        runner = [sys.executable, str(ROOT / 'tools' / 'run_wine.py')]
    else:
        runner = []
    timeout = args.frames / 60 + 20 + (30 if runner else 0)  # Wine starts slower
    print(f'run_smoke: {len(names)} example(s), {args.frames} frames each, {args.variant}', flush=True)
    with ThreadPoolExecutor(max_workers=len(names)) as pool:
        results = list(pool.map(lambda b: run(b, args.frames, timeout, runner), binaries))

    failed = 0
    for name, (code, log, seconds) in zip(names, results):
        problems = [line for line in log.splitlines() if PROBLEM.search(line)]
        if code is None:
            why = f'still running after {timeout:.0f} s'
        elif code != 0:
            why = f'exit {code}, after {seconds:.1f} s'
        elif problems:
            why = f'error logs, {seconds:.1f} s'
        else:
            print(f'  ok    {name} ({seconds:.1f} s)')
            continue
        failed += 1
        print(f'  FAIL  {name} ({why})')
        for line in problems or log.splitlines()[-5:]:
            print(f'          {line}')
    print(f'FAIL: {failed} of {len(names)} example(s)' if failed else f'PASS: {len(names)} example(s)')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
