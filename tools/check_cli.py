#!/usr/bin/env python3
"""Check the wgf tool: each of its commands, on a game it makes from the template.

    tools/check_cli.py [--only STEP[,STEP...]] [--keep]

In a scratch directory (under this machine's headless build), `wgf new` makes a game,
then each command is run on it as a developer would, and judged by what it made and
what it said:
  new         the game's files, its name in them; a second new into it refused
  build       --headless and --web: the program, its page, its host, its assets beside it
  run         --headless --frames 30, and --autopilot: runs that end at their frame, passing
  autopilot   the game's smoke autopilot, headless and --web: PASS; a failing one: FAIL
  dump        the ship, as a scene's text; flown by --autopilot to its end, turned on
  screenshot  a PNG of frame 30, from a browser, and one flown there by --autopilot
  serve       the page reached in a browser, its Haxe edited while it runs: the new code
              runs, with the frame count it had (state kept, not a restart)
  export      export/web (a trimmed host, under the budget) and export/desktop, each
              smoke-tested by the tool itself
A step that can't run here (no haxe, hxcpp, Emscripten, or browser) says
`check_cli: SKIPPING <step> (<why>)`, and the last line repeats every skip. Standard
library only.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import binding  # noqa: E402
import browser  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
WGF = ROOT / 'wgf'
STEPS = ('new', 'build', 'run', 'autopilot', 'dump', 'screenshot', 'serve', 'export')


def wgf(game_dir, *args, timeout=900):
    """Run `wgf <args>` in `game_dir`: (exit code, its output)."""
    done = subprocess.run([sys.executable, str(WGF), *args], cwd=game_dir, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, errors='replace', timeout=timeout)
    return done.returncode, done.stdout


def problem(what, output=''):
    print(f'check_cli: {what}')
    if output:
        print('\n'.join(output.strip().splitlines()[-25:]))
    return False


def needs(web=False, native=False, browser_too=False):
    """Why a step can't run here, or None."""
    if binding.haxe() is None:
        return 'no haxe on PATH'
    if native and not binding.has_hxcpp():
        return 'no hxcpp'
    if web:
        try:
            webhost.emcc()
        except RuntimeError as e:
            return str(e)
    if browser_too:
        try:
            browser.find_browser()
        except RuntimeError as e:
            return str(e)
    return None


def step_new(base, game):
    code, out = wgf(base, 'new', str(game), '--name', 'clitest')
    if code != 0:
        return problem('wgf new failed', out)
    data = json.loads((game / 'wgf.json').read_text())
    if data.get('name') != 'clitest' or 'clitest' not in (game / 'src' / 'Main.hx').read_text():
        return problem('wgf new: the name isn\'t in wgf.json and Main.hx')
    code, out = wgf(base, 'new', str(game))
    if code == 0:
        return problem('wgf new into a game\'s directory wasn\'t refused', out)
    print('check_cli: new: made, named, and a second new into it refused')
    return True


def step_build(game):
    """Each target this machine can build: headless with hxcpp, the web with Emscripten."""
    native, web = needs(native=True), needs(web=True)
    if native and web:
        return f'{native}; {web}'
    done = []
    if not native:
        code, out = wgf(game, 'build', '--headless')
        exe = game / 'build' / 'headless' / ('clitest.exe' if sys.platform == 'win32' else 'clitest')
        if code != 0 or not exe.exists() or not (exe.parent / 'assets').exists():
            return problem('wgf build --headless: no program, or no assets beside it', out)
        done.append('headless')
    if not web:
        code, out = wgf(game, 'build', '--web')
        site = game / 'build' / 'web'
        missing = [n for n in ('index.html', 'clitest.js', 'wgf-host.js', 'wgf-host.wasm', 'assets')
                   if not (site / n).exists()]
        if code != 0 or missing:
            return problem(f'wgf build --web: missing {missing}', out)
        done.append('web')
    skipped = '; '.join(f'{t} skipped ({why})' for t, why in (('headless', native), ('web', web)) if why)
    print(f'check_cli: build: {" and ".join(done)} built, each with its assets beside it'
          + (f'; {skipped}' if skipped else ''))
    return True


def step_run(game):
    why = needs(native=True)
    if why:
        return why
    code, out = wgf(game, 'run', '--headless', '--frames', '30', '--no-build')
    if code != 0 or 'wgf_autopilot: PASS (0 expectations, 30 frames)' not in out:
        return problem('wgf run --headless --frames 30: not a passing 30-frame run', out)
    fly = game / 'autopilot' / 'fly.autopilot'
    fly.write_text('wgf-autopilot 1\nat 40 expect frames >= 40\nat 90 end\n')
    code, out = wgf(game, 'run', '--headless', '--autopilot', 'autopilot/fly.autopilot', '--no-build')
    if code != 0 or 'wgf_autopilot: PASS (1 expectation, 91 frames)' not in out:
        return problem('wgf run --headless --autopilot: not the file\'s passing 91-frame run', out)
    print('check_cli: run: 30 frames, and an autopilot file\'s 91, headless, passed')
    return True


def step_autopilot(game):
    """The smoke autopilot headless (and a failing one), and in a browser: each where it can."""
    native, web = needs(native=True), needs(web=True, browser_too=True)
    if native and web:
        return f'{native}; {web}'
    said = []
    if not native:
        code, out = wgf(game, 'autopilot', 'autopilot/smoke.autopilot', '--no-build')
        if code != 0 or 'autopilot autopilot/smoke.autopilot: PASS' not in out:
            return problem('wgf autopilot: the smoke autopilot didn\'t pass headless', out)
        failing = game / 'autopilot' / 'failing.autopilot'
        failing.write_text('wgf-autopilot 1\nat 10 expect frames > 1000\nat 10 end\n')
        code, out = wgf(game, 'autopilot', 'autopilot/failing.autopilot', '--no-build')
        if code == 0 or 'FAIL' not in out:
            return problem('wgf autopilot: a failing one passed', out)
        said.append('the smoke autopilot passed headless, a failing one failed')
    if not web:
        code, out = wgf(game, 'autopilot', 'autopilot/smoke.autopilot', '--web', '--no-build')
        if code != 0 or 'PASS' not in out:
            return problem('wgf autopilot --web: the smoke autopilot didn\'t pass in a browser', out)
        said.append('the smoke autopilot passed in a browser')
    skipped = '; '.join(f'{t} skipped ({why})' for t, why in (('headless', native), ('web', web)) if why)
    print(f'check_cli: autopilot: {"; ".join(said)}' + (f'; {skipped}' if skipped else ''))
    return True


def step_dump(game):
    why = needs(native=True)
    if why:
        return why
    code, out = wgf(game, 'dump', '--frame', '20', '--no-build')
    if code != 0 or not out.startswith('wgf-scene 1') or 'entity "ship"' not in out or 'motion ' not in out:
        return problem('wgf dump: not the ship as a scene', out)
    early = turned(out)
    fly = game / 'autopilot' / 'fly.autopilot'
    fly.write_text('wgf-autopilot 1\nat 40 expect frames >= 40\nat 90 end\n')
    code, out = wgf(game, 'dump', '--autopilot', 'autopilot/fly.autopilot', '--no-build')
    late = turned(out) if code == 0 else None
    if late is None or early is None or not late > early:
        return problem(f'wgf dump --autopilot: not the ship turned on to frame 90 ({early} at 20, {late} after)', out)
    print(f'check_cli: dump: the ship, as a scene\'s text; flown on by an autopilot, turned {early:.2f} -> {late:.2f}')
    return True


def turned(dump):
    """The ship's turn in a dump, radians: its transform's rotation z."""
    for line in dump.splitlines():
        if 'transform ' in line and 'rotation=' in line:
            return float(line.split('rotation=')[1].split()[0].split(',')[2])
    return None


def step_screenshot(game):
    why = needs(web=True, browser_too=True)
    if why:
        return why
    shot = game / 'build' / 'shot.png'
    code, out = wgf(game, 'screenshot', '--frame', '30', '--out', str(shot), '--no-build')
    if code != 0 or not shot.exists() or shot.read_bytes()[:8] != b'\x89PNG\r\n\x1a\n':
        return problem('wgf screenshot: no PNG', out)
    flown = game / 'build' / 'flown.png'
    fly = game / 'autopilot' / 'fly.autopilot'
    fly.write_text('wgf-autopilot 1\nat 40 expect frames >= 40\nat 90 end\n')
    code, out = wgf(game, 'screenshot', '--autopilot', 'autopilot/fly.autopilot', '--out', str(flown), '--no-build')
    if code != 0 or not flown.exists() or 'frame 90 saved' not in out:
        return problem('wgf screenshot --autopilot: no PNG at the autopilot\'s end (frame 90)', out)
    print(f'check_cli: screenshot: frame 30, {shot.stat().st_size} bytes of PNG; and frame 90, flown there')
    return True


def step_serve(game):
    """The page in a browser; Main.hx edited while it runs; the new code's line, with the
    frame count going on from where it was."""
    why = needs(web=True, browser_too=True)
    if why:
        return why
    port = browser.free_port()
    process = subprocess.Popen([sys.executable, str(WGF), 'serve', '--port', str(port)], cwd=game,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    served = []
    threading.Thread(target=lambda: served.extend(process.stdout), daemon=True).start()
    try:
        if not wait(lambda: any(' at http://' in line for line in served), 600):
            return problem('wgf serve: never said where it serves', ''.join(served))
        sys.path.insert(0, str(ROOT / 'tools' / 'wgf'))
        import game as games  # the tool's own module, for its page runner
        main = game / 'src' / 'Main.hx'
        state = {'edited': False, 'before': 0}

        def on_line(line):
            m = re.search(r'(hello|reloaded), frame (\d+)', line)
            if m and m.group(1) == 'hello' and not state['edited'] and int(m.group(2)) >= 120:
                state['before'] = int(m.group(2))
                state['edited'] = True
                # code, not a static's first value: a reload keeps every static's value
                main.write_text(main.read_text().replace("'hello, frame $frames'", "'reloaded, frame $frames'"))

        lines = games.run_page_url(f'http://127.0.0.1:{port}/index.html', until=('reloaded, frame',), timeout=240,
                                   on_line=on_line, echo=False)
        after = [int(m.group(1)) for line in lines for m in [re.search(r'reloaded, frame (\d+)', line)] if m]
        if not state['edited']:
            return problem('wgf serve: the page never said hello', '\n'.join(lines) + ''.join(served))
        if not after:
            return problem('wgf serve: the edit never reached the page', '\n'.join(lines) + ''.join(served))
        if after[0] <= state['before']:
            return problem(f'wgf serve: the reloaded page counted from {after[0]}, not on from {state["before"]}: '
                           'its state was lost', '\n'.join(lines))
        print(f'check_cli: serve: edited at frame {state["before"]}; the new code said so at frame {after[0]}, '
              'its state kept')
        return True
    finally:
        process.terminate()
        try:
            process.wait(20)
        except subprocess.TimeoutExpired:
            process.kill()


def step_export(game):
    """The web export (a trimmed host, smoke-tested in a browser) and the desktop one
    (smoke-tested in a window), each where it can be."""
    native, web = needs(native=True), needs(web=True, browser_too=True)
    if native and web:
        return f'{native}; {web}'
    args = ['export'] + (['--web'] if native else ['--desktop'] if web else [])
    code, out = wgf(game, *args, timeout=1800)
    said = []
    if not web:
        site = game / 'export' / 'web'
        if 'web export: smoke PASS' not in out or not (site / 'wgf-host.wasm').exists():
            return problem('wgf export: the web export failed', out)
        trimmed = json.loads((game / 'build' / 'export-web' / 'exports.json').read_text())['exports']
        full = json.loads((ROOT / 'hosts' / 'web' / 'exports.json').read_text())['exports']
        if not len(trimmed) < len(full) / 2:
            return problem(f'wgf export: the host wasn\'t trimmed ({len(trimmed)} of {len(full)} calls)', out)
        sizes = [line for line in out.splitlines() if 'KB gzipped in all' in line]
        said.append(f'web: {len(trimmed)} of {len(full)} calls in the host, '
                    f'{sizes[0].split("; ")[-1] if sizes else ""}, smoke passed')
    if not native:
        if 'desktop export: smoke PASS' not in out and 'desktop export: SKIPPING' not in out:
            return problem('wgf export: the desktop export failed', out)
        said.append('desktop: ' + ('smoke passed' if 'desktop export: smoke PASS' in out else 'not smoke-tested here'))
    if code != 0:
        return problem('wgf export: failed', out)
    if any(path.is_symlink() for path in game.joinpath('export').rglob('*')):
        return problem('wgf export: a link in the export, which a copy elsewhere would break')
    skipped = '; '.join(f'{t} skipped ({why})' for t, why in (('desktop', native), ('web', web)) if why)
    print(f'check_cli: export: {"; ".join(said)}' + (f'; {skipped}' if skipped else ''))
    return True


def wait(predicate, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(0.2)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='comma-separated steps: ' + ', '.join(STEPS) + ' (new always runs first)')
    ap.add_argument('--keep', action='store_true', help='keep the game made, to look at')
    args = ap.parse_args()
    chosen = args.only.split(',') if args.only else list(STEPS)
    for name in chosen:
        if name not in STEPS:
            print(f'check_cli: no step "{name}" ({", ".join(STEPS)})', file=sys.stderr)
            return 2
    base = Path(variants.work(variants.native('debug-headless'))) / 'check_cli'
    if base.exists():
        shutil.rmtree(base)
    base.mkdir(parents=True)
    game = base / 'clitest'
    skipped, failed = [], []
    for name in ['new'] + [s for s in chosen if s != 'new']:
        print(f'== {name}', flush=True)
        if name == 'new':
            outcome = step_new(base, game)
        else:
            outcome = globals()[f'step_{name}'](game)
        if isinstance(outcome, str):
            print(f'check_cli: SKIPPING {name} ({outcome})', flush=True)
            skipped.append(f'{name} ({outcome})')
        elif not outcome:
            failed.append(name)
            if name == 'new':
                break
    if not args.keep and not failed:
        shutil.rmtree(base, ignore_errors=True)
    note = f'; skipped: {", ".join(skipped)}' if skipped else ''
    if failed:
        print(f'check_cli: FAIL ({", ".join(failed)}){note}')
        return 1
    print(f'check_cli: PASS{note}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
