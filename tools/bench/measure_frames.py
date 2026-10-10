#!/usr/bin/env python3
"""Measure frame times: a game's web export flown by its autopilot in a browser, each
frame's main-thread work and the garbage collections traced by Chrome, recorded beside
the sizes; and a benchmark's cases, the same way.

    tools/bench/measure_frames.py [--display xvfb|headless] [--throttle N] [--runs N]
                                  [--write] [--browser PATH] [program ...]

A program is game:<name> (default: every game in games/), or a game's folder anywhere (one
with a wgf.json: a game outside libwgf measured as libwgf's are): its web export, made as
`wgf export --web` makes it (a release build, its host and JS binding trimmed), flown by
the game's autopilot/bench.autopilot, else its playthrough (wgf.json's "playthrough",
playthrough.autopilot by default). A game's frame budget (HISTORY.md, "Milestone 2's
plan"): a median of 6 ms and a 95th percentile of 10 ms of main-thread work on the
reference machine, and no frame over 33 ms once play has started. Or bench:<name>, a benchmark in tools/bench/<name>/
(BENCHES: shadowbench, libwgt's), built in wasm32-release against the staged variant and
run with no autopilot until it logs "<name>: done": it marks each case in the trace
(console.timeStamp "<name> <case> <models>" as the case's measured frames begin, "<name>
rest" as they end), and each case is the frames between, a frame a mark falls in left
out. A benchmark is never part of the default set.

The reference machine (docs/HISTORY.md, "Milestone 2's plan, reviewed"): this machine's
GPU through ANGLE on Vulkan under Xvfb (--display xvfb, the default), with Chrome's CPU
throttled 4 times (--throttle 4, the default), Lighthouse's stand-in for a mid-tier
device. --display headless draws on SwiftShader, a CPU renderer: CI's, which has no GPU.

The run is traced (Chrome's devtools.timeline category, as wgrender-c's pages.py traced
its collections). Each FireAnimationFrame event is one frame's main-thread work: the JS
and wasm the frame ran, which is what libwgf's frame budget holds (the GPU's own time
can't be read reliably through WebGL2). MinorGC and MajorGC events are the collections.
Reported, in milliseconds: the frames' mean, median, 95th and 99th percentiles, and the
worst;
how many frames took over 16.7 ms and over 33 ms; the collections' count, total, and the
longest. Every frame of the run is counted, its loading among them: the worst frame is
often a load's. With --runs, each number is the median of the runs'. Chrome throttles the
CPU by suspending the page's main thread in slices, so a frame shorter than a slice can
run unslowed: under a throttle the mean (the time taken as a whole) is the number that
scales, while a light frame's median barely moves (measured: Asteroids' median 0.13 ms
unthrottled, 0.15 at 4x; its 95th 0.32 against 0.87).

--write records each program's numbers, with the machine, the throttle, the display,
and the commit, in docs/benchmarks.json ("frames"), and renders docs/benchmarks.md again
(tools/measure_sizes.py --render). Timing, not a check: CI runs it on SwiftShader to keep
it working and records nothing. Standard library only.
"""
import argparse
import json
import platform
import statistics
import subprocess
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402
import examples  # noqa: E402
import server  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / 'docs' / 'benchmarks.json'
PASS, FAIL = 'wgf_autopilot: PASS', 'wgf_autopilot: FAIL'
FRAME_EVENTS = ('FireAnimationFrame',)
GC_EVENTS = ('MinorGC', 'MajorGC')
MARK_EVENTS = ('TimeStamp',)
BENCHES = ('shadowbench',)
GPU = """(() => { const gl = document.createElement('canvas').getContext('webgl2');
    const info = gl && gl.getExtension('WEBGL_debug_renderer_info');
    return info ? gl.getParameter(info.UNMASKED_RENDERER_WEBGL) : (gl ? gl.getParameter(gl.RENDERER) : ''); })()"""


def games():
    found = ROOT / 'games'
    return [f'game:{d.name}' for d in sorted(found.iterdir()) if (d / 'wgf.json').exists()] if found.is_dir() else []


def export(name, game=None):
    """The game's web export, made fresh in build/frames/; (the site, its autopilot's text).
    `game`: its folder, when it isn't libwgf's games/<name>."""
    game = game or ROOT / 'games' / name
    dest = ROOT / 'build' / 'frames' / name
    done = subprocess.run([sys.executable, str(ROOT / 'wgf'), 'export', '--web', '--out', str(dest)], cwd=game,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    if done.returncode != 0:
        raise RuntimeError(f'exporting {name} failed:\n' + '\n'.join(done.stdout.strip().splitlines()[-15:]))
    data = json.loads((game / 'wgf.json').read_text(encoding='utf-8'))
    autopilots = game / data.get('autopilot', 'autopilot')
    flown = next((p for p in (autopilots / 'bench.autopilot', autopilots / data.get('playthrough', 'playthrough.autopilot'))
                  if p.exists()), None)
    if flown is None:
        raise RuntimeError(f'{name} has no bench or playthrough autopilot to fly')
    return dest / 'web', flown


def build_bench(name):
    """The benchmark's page, built fresh in wasm32-release; its folder, the site."""
    return examples.build_at('wasm32-release', name, ROOT / 'tools' / 'bench' / name).parent


def cases(events, name):
    """A benchmark's frames by case, as its marks split the trace: {(case, models): [ms, ...]}."""
    marks = sorted((e['ts'], e.get('args', {}).get('data', {}).get('message', '')) for e in events
                   if e['name'] in MARK_EVENTS)
    marks = [(ts, text[len(name) + 1:]) for ts, text in marks if text.startswith(name + ' ')]
    out = {}
    for e in events:
        if e['name'] not in FRAME_EVENTS:
            continue
        start, end = e['ts'], e['ts'] + e.get('dur', 0)
        if any(start <= ts <= end for ts, _ in marks):
            continue  # a frame a mark falls in: a case begun or ended in it, set up or torn down
        before = [text for ts, text in marks if ts < start]
        if not before or before[-1] == 'rest':
            continue
        label, _, models = before[-1].rpartition(' ')
        out.setdefault((label, int(models)), []).append(e.get('dur', 0) / 1000.0)
    return out


def percentile(sorted_values, q):
    return sorted_values[min(len(sorted_values) - 1, int(len(sorted_values) * q))]


def run(site, flown, display, throttle, browser_path, timeout=600, bench=None):
    """One flown run, traced: its numbers, and the GPU it drew on. A benchmark's (`bench`, its
    name; `flown` None) runs until it logs that it is done: its numbers are its cases'."""
    base, httpd = server.serve(site, assets=None)
    processes = browser.RunProcesses('frames')
    processes.install_handlers()
    lines, events, ended, traced = [], [], threading.Event(), threading.Event()

    def on_event(message):
        method = message.get('method')
        if method == 'Runtime.consoleAPICalled':
            text = ' '.join(str(a.get('value', a.get('description', ''))) for a in message['params']['args'])
            lines.append(text)
            if PASS in text or FAIL in text or (bench is not None and f'{bench}: done' in text):
                ended.set()
        elif method == 'Runtime.exceptionThrown':
            lines.append('[ERROR] exception')
            ended.set()
        elif method == 'Tracing.dataCollected':
            events.extend(e for e in message['params']['value']
                          if e.get('name') in FRAME_EVENTS + GC_EVENTS + MARK_EVENTS)
        elif method == 'Tracing.tracingComplete':
            traced.set()

    try:
        debug_base, session = browser.launch_browser(processes, browser_path, display)
        session.close()
        page = browser.open_session(browser.page_target(debug_base)['webSocketDebuggerUrl'])
        page.on_event(on_event)
        page.send('Runtime.enable')
        page.send('Page.enable')
        if throttle > 1:
            page.send('Emulation.setCPUThrottlingRate', {'rate': throttle})
        if flown is not None:
            page.send('Page.addScriptToEvaluateOnNewDocument',
                      {'source': f'globalThis.wgfAutopilot = {json.dumps(flown.read_text(encoding="utf-8"))};'})
        page.send('Tracing.start', {'traceConfig': {'includedCategories': ['devtools.timeline', 'v8']},
                                    'transferMode': 'ReportEvents'})
        page.send('Page.navigate', {'url': f'{base}/index.html'})
        finished = ended.wait(timeout)
        page.send('Tracing.end')
        traced.wait(60)
        gpu = page.send('Runtime.evaluate', {'expression': GPU, 'returnByValue': True})['result'].get('value', '')
        page.close()
    finally:
        processes.stop()
        httpd.shutdown()
    if not finished:
        raise RuntimeError(f'the autopilot didn\'t end within {timeout} s' if bench is None
                           else f'it didn\'t log "{bench}: done" within {timeout} s')
    failed = [line for line in lines if FAIL in line or '[ERROR]' in line or '[FATAL]' in line]
    if failed:
        raise RuntimeError('the run failed, so its numbers mean nothing:\n  ' + '\n  '.join(failed[:5]))
    if bench is not None:
        split = cases(events, bench)
        if not split:
            raise RuntimeError('no case marked in the trace')
        return {f'{case}|{models}': {'frames': len(f), 'mean': sum(f) / len(f), 'median': percentile(sorted(f), 0.5),
                                     'p95': percentile(sorted(f), 0.95), 'worst': max(f)}
                for (case, models), f in split.items()}, gpu
    frames = sorted(e.get('dur', 0) / 1000.0 for e in events if e['name'] in FRAME_EVENTS)
    if len(frames) < 30:
        raise RuntimeError(f'only {len(frames)} frames traced: did it start?')
    collections = sorted(e.get('dur', 0) / 1000.0 for e in events if e['name'] in GC_EVENTS)
    return {'frames': len(frames), 'mean': sum(frames) / len(frames), 'median': percentile(frames, 0.5), 'p95': percentile(frames, 0.95),
            'p99': percentile(frames, 0.99), 'worst': frames[-1],
            'over16': sum(1 for f in frames if f > 1000.0 / 60.0), 'over33': sum(1 for f in frames if f > 1000.0 / 30.0),
            'gc': len(collections), 'gcTotal': sum(collections), 'gcWorst': collections[-1] if collections else 0.0}, gpu


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


def run_bench(name, args, found, results):
    """bench:<name>: built, run, its cases printed, and put in `results`; the exit code."""
    if name not in BENCHES:
        print(f'measure_frames: bench:{name}: no such benchmark ({", ".join(BENCHES)})')
        return 2
    try:
        site = build_bench(name)
        runs, gpu = [], ''
        for _ in range(max(args.runs, 1)):
            numbers, gpu = run(site, None, args.display, args.throttle, found, timeout=1800, bench=name)
            runs.append(numbers)
    except RuntimeError as e:
        print(f'measure_frames: bench:{name}: {e}')
        return 1
    rows = {}
    for key in runs[0]:
        values = [r[key] for r in runs if key in r]
        rows[key] = {k: round(statistics.median(v[k] for v in values), 2) if k != 'frames' else values[0][k]
                     for k in values[0]}
    print(f'bench:{name}: main-thread ms a frame, by case and models (mean, 95th)')
    for key, row in rows.items():
        case, models = key.split('|')
        print(f'  {case:<16} {models:>5}  {row["mean"]:8.2f} {row["p95"]:8.2f}  ({row["frames"]} frames)')
    print(f'  on {cpu_name()}, {gpu or "an unknown GPU"}, {args.display}, CPU throttled {args.throttle:g}x')
    results[f'bench:{name}'] = {'cases': rows, 'order': list(dict.fromkeys(key.split('|')[0] for key in rows)),
                                'display': args.display, 'throttle': args.throttle,
                                'runs': len(runs), 'cpu': cpu_name(), 'gpu': gpu, 'commit': commit()}
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('programs', nargs='*', help='game:<name>, a game\'s folder, or bench:<name> (default: every game)')
    ap.add_argument('--display', choices=['xvfb', 'headless'], default='xvfb',
                    help='xvfb: the real GPU under Xvfb (the reference); headless: SwiftShader (CI)')
    ap.add_argument('--throttle', type=float, default=4.0, help="Chrome's CPU throttling rate (default 4)")
    ap.add_argument('--runs', type=int, default=1, help='runs of each, each number their median (default 1)')
    ap.add_argument('--write', action='store_true', help='record into docs/benchmarks.json and .md')
    ap.add_argument('--browser', help='a Chromium-based browser (default: found)')
    args = ap.parse_args()
    try:
        found = browser.find_browser(args.browser)
    except RuntimeError as e:
        print(f'measure_frames: SKIPPING ({e})')
        return 0
    programs = args.programs or games()
    results = {}
    for name in programs:
        if name.startswith('bench:'):
            code = run_bench(name[len('bench:'):], args, found, results)
            if code != 0:
                return code
            continue
        folder = Path(name)
        if not name.startswith('game:') and not (folder / 'wgf.json').is_file():
            print(f'measure_frames: {name}: not a game or a benchmark (game:<name>, a game\'s folder, bench:<name>)')
            return 2
        try:
            site, flown = (export(name[len('game:'):]) if name.startswith('game:')
                           else export(folder.resolve().name, folder.resolve()))
            runs, gpu = [], ''
            for _ in range(max(args.runs, 1)):
                numbers, gpu = run(site, flown, args.display, args.throttle, found)
                runs.append(numbers)
        except RuntimeError as e:
            print(f'measure_frames: {name}: {e}')
            return 1
        merged = {key: statistics.median(r[key] for r in runs) for key in runs[0]}
        merged = {key: round(value, 2) if isinstance(value, float) else value for key, value in merged.items()}
        merged.update({'autopilot': flown.name, 'display': args.display, 'throttle': args.throttle,
                       'runs': len(runs), 'cpu': cpu_name(), 'gpu': gpu, 'commit': commit()})
        results[name] = merged
        print(f'{name}: {merged["frames"]} frames ({flown.name}), main-thread ms a frame: mean {merged["mean"]}, median {merged["median"]}, '
              f'95th {merged["p95"]}, 99th {merged["p99"]}, worst {merged["worst"]}; over 16.7 ms {merged["over16"]}, '
              f'over 33 ms {merged["over33"]}; {merged["gc"]} collections, {merged["gcTotal"]} ms, '
              f'longest {merged["gcWorst"]} ms')
        print(f'  on {merged["cpu"]}, {gpu or "an unknown GPU"}, {args.display}, CPU throttled {args.throttle:g}x')
    if args.write:
        baseline = json.loads(BASELINE.read_text(encoding='utf-8'))
        for name in [n for n in results if n.startswith('bench:')]:
            baseline.setdefault('benches', {})[name] = results.pop(name)
        baseline.setdefault('frames', {}).update(results)
        BASELINE.write_text(json.dumps(baseline, indent=1, sort_keys=True) + '\n', encoding='utf-8')
        done = subprocess.run([sys.executable, str(ROOT / 'tools' / 'measure_sizes.py'), '--render'])
        if done.returncode != 0:
            return done.returncode
        print('measure_frames: wrote docs/benchmarks.json and docs/benchmarks.md')
    return 0


if __name__ == '__main__':
    sys.exit(main())
