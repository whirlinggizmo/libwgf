#!/usr/bin/env python3
"""Check every game in games/: its generated files current, its playthrough autopilot
passing headless and in a browser, and its exports made, smoke-tested, and the web one
within the game's size budget.

    tools/check_games.py [--only STEP[,STEP...]] [game ...]

For each game (default: every games/<name>/ with a wgf.json), with the wgf tool, run in
the game's directory as a developer would:
  generated   tools/gen_sounds.py --check, for a game whose sounds it makes
  playthrough wgf autopilot <its playthrough>, headless: wgf.json's "playthrough" in its
              autopilot folder, playthrough.autopilot by default
  browser     the same in a headless browser (wgf autopilot --web)
  web         wgf export --web: the trimmed host, the budget, the smoke run in a browser
  desktop     wgf export --desktop: the release build, its smoke run in a window
A step that can't run here (no haxe, hxcpp, Emscripten, or browser) says
`check_games: SKIPPING <step> (<why>)`, and the last line repeats every skip. Standard
library only.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import binding  # noqa: E402
import browser  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
GAMES = ROOT / 'games'
WGF = ROOT / 'wgf'
STEPS = ('generated', 'playthrough', 'browser', 'web', 'desktop')
GENERATORS = {'asteroids': ['tools/gen_sounds.py', '--check']}


def games(names):
    found = sorted(d.name for d in GAMES.iterdir() if (d / 'wgf.json').exists()) if GAMES.is_dir() else []
    for name in names:
        if name not in found:
            raise SystemExit(f'check_games: no game "{name}" in games/ ({", ".join(found) or "none"})')
    return names or found


def wgf(game_dir, *args, timeout=1800):
    done = subprocess.run([sys.executable, str(WGF), *args], cwd=game_dir, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, errors='replace', timeout=timeout)
    return done.returncode, done.stdout


def playthrough(game_dir):
    """The game's playthrough autopilot, relative to it: wgf.json's "playthrough" in its
    autopilot folder (playthrough.autopilot by default)."""
    data = json.loads((game_dir / 'wgf.json').read_text(encoding='utf-8'))
    return f'{data.get("autopilot", "autopilot")}/{data.get("playthrough", "playthrough.autopilot")}'


def why_not(step):
    """Why `step` can't run here, or None."""
    if step == 'generated':
        return None
    if binding.haxe() is None:
        return 'no haxe on PATH'
    if step in ('playthrough', 'desktop') and not binding.has_hxcpp():
        return 'no hxcpp'
    if step in ('browser', 'web'):
        try:
            webhost.emcc()
            browser.find_browser()
        except RuntimeError as e:
            return str(e)
    return None


def run_step(name, step):
    game_dir = GAMES / name
    if step == 'generated':
        command = GENERATORS.get(name)
        if command is None:
            return True, 'nothing generated'
        done = subprocess.run([sys.executable, *command], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True)
        return done.returncode == 0, done.stdout.strip()
    if step == 'playthrough':
        code, out = wgf(game_dir, 'autopilot', playthrough(game_dir))
        return code == 0 and 'PASS' in out.splitlines()[-1], out
    if step == 'browser':
        code, out = wgf(game_dir, 'autopilot', playthrough(game_dir), '--web')
        return code == 0 and 'PASS' in out.splitlines()[-1], out
    if step == 'web':
        code, out = wgf(game_dir, 'export', '--web')
        return code == 0 and 'web export: smoke PASS' in out, out
    code, out = wgf(game_dir, 'export', '--desktop')
    return code == 0 and ('desktop export: smoke PASS' in out or 'desktop export: SKIPPING' in out), out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('games', nargs='*', help='the games to check (default: every one)')
    ap.add_argument('--only', help='comma-separated steps: ' + ', '.join(STEPS))
    args = ap.parse_args()
    steps = args.only.split(',') if args.only else list(STEPS)
    for step in steps:
        if step not in STEPS:
            print(f'check_games: no step "{step}" ({", ".join(STEPS)})', file=sys.stderr)
            return 2
    skipped, failed = [], []
    for name in games(args.games):
        for step in steps:
            print(f'== {name}: {step}', flush=True)
            why = why_not(step)
            if why:
                print(f'check_games: SKIPPING {name} {step} ({why})', flush=True)
                skipped.append(f'{name} {step} ({why})')
                continue
            ok, out = run_step(name, step)
            summary = [line for line in out.splitlines()
                       if line.startswith('wgf: ') or 'wgf_autopilot: PASS' in line or 'gen_sounds' in line]
            print('\n'.join(summary[-4:]) if ok else '\n'.join(out.strip().splitlines()[-30:]), flush=True)
            print(f'check_games: {name} {step}: {"PASS" if ok else "FAIL"}', flush=True)
            if not ok:
                failed.append(f'{name} {step}')
    note = f'; skipped: {", ".join(skipped)}' if skipped else ''
    if failed:
        print(f'check_games: FAIL ({", ".join(failed)}){note}')
        return 1
    print(f'check_games: PASS{note}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
