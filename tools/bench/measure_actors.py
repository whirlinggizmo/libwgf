#!/usr/bin/env python3
"""Measure what an actor costs, in bytes and in time (SPEC.md, "An actor benchmark"), and
hold it to the baseline.

    tools/bench/measure_actors.py [--runs N] [--write [--as before]] [--check]
                                  [--program PATH] [row ...]

The program (tools/bench/actors/main.c, whose header says what each row and number is)
is built against linux-x64-release-headless, staged, and run once a row (default: every
row it lists), flown by an autopilot so each frame is one tick and none waits for a
display. Its rows: 10k and 50k actors static and moving, in flat and deep trees, with and
without behaviors, 1,000 spawned and destroyed a second, and finding by name, path, and
component. Each row's bytes an actor is the first run's (the heap's growth is the same
every run); its nanoseconds the median of --runs (default 3).

--write records the rows, the machine, and the commit in docs/benchmarks.json ("actors")
and renders docs/benchmarks.md again (tools/measure_sizes.py --render); --as before
records them as the measurement from before actors (the nodes and entities they
replaced), kept beside the current one, with --program naming that build of the
program. --check fails a row past its baseline: bytes by more than BYTES_TOLERANCE,
time by more than TIME_TOLERANCE times (wide: a CI runner is not the machine that
recorded it, and timing on a shared one is noisy; bytes are the check that holds
exactly). Linux alone: the program reads glibc's mallinfo2. Standard library only.
"""
import argparse
import json
import os
import platform
import statistics
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # an embedded Python (Windows) doesn't add it
import examples  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / 'docs' / 'benchmarks.json'
VARIANT = 'linux-x64-release-headless'
SOURCE = ROOT / 'tools' / 'bench' / 'actors'
WORK = ROOT / 'build' / 'bench' / 'actors'
BYTES_TOLERANCE = (0.05, 4)  # a row's bytes an actor may grow by 5%, or 4 bytes, whichever is more
TIME_TOLERANCE = 3.0  # and its time to three times the baseline's
SECONDS = 300


def build():
    examples.prepare(VARIANT)
    return examples.build_at(VARIANT, 'bench_actors', SOURCE, into=WORK)


def run(program, row):
    """One row's numbers: {"<row>": {"bytes": .., "ns": ..}, ...} (a lookup row gives
    several)."""
    WORK.mkdir(parents=True, exist_ok=True)
    autopilot = WORK / 'bench.autopilot'
    autopilot.write_text('wgf-autopilot 1\nat 1000000 end\n')
    env = dict(os.environ, LIBWGF_AUTOPILOT=str(autopilot))
    done = subprocess.run([str(program), row], capture_output=True, text=True, env=env, timeout=SECONDS)
    if done.returncode != 0:
        raise RuntimeError(f'{row}: exit {done.returncode}\n' + (done.stdout + done.stderr)[-2000:])
    found = {}
    for line in done.stdout.splitlines():
        if not line.startswith('bench_actors: '):
            continue
        name, *pairs = line[len('bench_actors: '):].split()
        values = dict(pair.split('=', 1) for pair in pairs)
        found[name] = {'ns': float(values['ns'])}
        if 'bytes' in values:
            found[name]['bytes'] = float(values['bytes'])
    if row not in found:
        raise RuntimeError(f'{row}: no result in its output\n' + done.stdout[-2000:])
    return found


def measure(program, rows, runs):
    results = {}
    for row in rows:
        each = [run(program, row) for _ in range(runs)]
        for name in each[0]:
            results[name] = dict(each[0][name])
            results[name]['ns'] = round(statistics.median(r[name]['ns'] for r in each), 2)
        for name, r in sorted(results.items()):
            if name == row or name.startswith(row + '-'):
                print(f'{name:<28} ' + (f'{r["bytes"]:>8.1f} bytes ' if 'bytes' in r else ' ' * 15)
                      + f'{r["ns"]:>9.2f} ns', flush=True)
    return results


def check(measured, rows):
    worse, notes = [], []
    for name, new in sorted(measured.items()):
        old = rows.get(name)
        if old is None:
            notes.append(f'{name}: new: --write it into the baseline')
            continue
        if 'bytes' in new and 'bytes' in old:
            allowed = old['bytes'] + max(old['bytes'] * BYTES_TOLERANCE[0], BYTES_TOLERANCE[1])
            if new['bytes'] > allowed:
                worse.append(f'{name}: {new["bytes"]:.1f} bytes an actor, was {old["bytes"]:.1f} '
                             f'(allowed {allowed:.1f})')
        if new['ns'] > old['ns'] * TIME_TOLERANCE:
            worse.append(f'{name}: {new["ns"]:.2f} ns, was {old["ns"]:.2f} (allowed {TIME_TOLERANCE:g} times)')
    return worse, notes


def cpu_name():
    try:
        for line in Path('/proc/cpuinfo').read_text().splitlines():
            if line.startswith('model name'):
                return line.split(':', 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def commit():
    done = subprocess.run(['git', '-C', str(ROOT), 'log', '-1', '--format=%h %cs'], capture_output=True, text=True)
    return done.stdout.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rows', nargs='*', help='rows to run (default: every row the program lists)')
    ap.add_argument('--runs', type=int, default=3, help='runs of each row, the median taken (default 3)')
    ap.add_argument('--write', action='store_true', help='record the rows in docs/benchmarks.json and .md')
    ap.add_argument('--as', dest='label', choices=['current', 'before'], default='current',
                    help='with --write: the current measurement (default), or the one from before actors')
    ap.add_argument('--check', action='store_true', help='fail a row past its baseline')
    ap.add_argument('--program', type=Path, help='run this build of the program instead of building it')
    args = ap.parse_args()
    if not sys.platform.startswith('linux'):
        print('measure_actors: SKIPPING (the program reads glibc\'s mallinfo2: Linux alone)')
        return 77 if args.check else 0
    try:
        program = args.program.resolve() if args.program else build()
        listed = subprocess.run([str(program), '--list'], capture_output=True, text=True, check=True).stdout.split()
        rows = args.rows or listed
        unknown = set(rows) - set(listed)
        if unknown:
            print(f'measure_actors: no row {", ".join(sorted(unknown))} (the program has {", ".join(listed)})',
                  file=sys.stderr)
            return 2
        measured = measure(program, rows, args.runs)
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as e:
        print(f'measure_actors: {e}', file=sys.stderr)
        return 1
    baseline = json.loads(BASELINE.read_text()) if BASELINE.exists() else {}
    actors = baseline.get('actors', {})
    status = 0
    if args.check:
        worse, notes = check(measured, actors.get('rows', {}))
        for line in notes:
            print(f'measure_actors: {line}')
        for line in worse:
            print(f'measure_actors: FAIL {line}')
        status = 1 if worse else 0
        print(f'measure_actors: {"FAIL" if worse else "PASS"}: {len(measured)} row(s) against the baseline')
    if args.write:
        record = {'commit': commit(), 'cpu': cpu_name(), 'variant': VARIANT, 'runs': args.runs, 'rows': measured}
        if args.label == 'before':
            actors['before'] = record
        else:
            rows_kept = dict(actors.get('rows', {})) if args.rows else {}
            rows_kept.update(measured)
            actors.update(record, rows=rows_kept)
        baseline['actors'] = actors
        BASELINE.write_text(json.dumps(baseline, indent=1, sort_keys=True) + '\n')
        subprocess.run([sys.executable, str(ROOT / 'tools' / 'measure_sizes.py'), '--render'], check=True)
        print(f'measure_actors: wrote {BASELINE.relative_to(ROOT)}')
    return status


if __name__ == '__main__':
    sys.exit(main())
