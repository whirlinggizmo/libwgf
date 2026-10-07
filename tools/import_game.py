#!/usr/bin/env python3
"""Import a game written outside libwgf into games/: a copy of its directory, less its
build/ and export/ (and __pycache__/), with IMPORTED.md at its top saying it is a copy
and where its source is, so CI, the size table, the frame times, and Pages cover it as
they cover every game in games/.

    tools/import_game.py SOURCE [--name NAME] [--playthrough FILE]

SOURCE is the game's directory (its wgf.json); the copy is games/<NAME> (default: the
name in its wgf.json). A copy there already is replaced whole, its build/ and export/
kept; nothing else in games/ is touched. IMPORTED.md names the source as given, and its
git commit when it is in a repository (git is asked, if it is there), and whether the
source had changes not committed. The copy is not edited here: a change is made in the
source and imported again. The one exception is --playthrough, for a game whose own
wgf.json names none: the copy's wgf.json is given "playthrough" (the autopilot file
libwgf's checks fly, in its autopilot folder), and IMPORTED.md says so. Standard library
only.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAMES = ROOT / 'games'
LEFT_OUT = ('build', 'export', '__pycache__')
HEADER = 'IMPORTED.md'


def git(source, *args):
    """git's answer in `source`, or None (no git, or not a repository)."""
    try:
        done = subprocess.run(['git', '-C', str(source), *args], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def header(source, given, name, playthrough):
    commit = git(source, 'rev-parse', '--short', 'HEAD')
    lines = [f'# {name}: an imported copy', '',
             f'A copy of `{given}`, made by `tools/import_game.py`, less its `build/` and `export/`. '
             'It is not edited here: its source is, and it is imported again.', '']
    if commit is not None:
        dirty = git(source, 'status', '--porcelain', '--', '.')
        when = git(source, 'log', '-1', '--format=%cs')
        lines.append(f'Its source at commit {commit} ({when})'
                     + (', with changes not committed.' if dirty else '.'))
    else:
        lines.append('Its source is not in a git repository here.')
    if playthrough:
        lines += ['', f'Changed by the import: its `wgf.json` given `"playthrough": "{playthrough}"`, the autopilot '
                  'libwgf\'s checks and frame times fly.']
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('source', help='the game\'s directory, with its wgf.json')
    ap.add_argument('--name', help='the copy\'s directory in games/ (default: its wgf.json\'s name)')
    ap.add_argument('--playthrough', help='the autopilot file the copy\'s checks fly, set in its wgf.json')
    args = ap.parse_args()
    source = Path(args.source).resolve()
    try:
        data = json.loads((source / 'wgf.json').read_text(encoding='utf-8'))
    except (OSError, ValueError) as e:
        sys.exit(f'import_game: {source} is no game: {e}')
    name = args.name or data.get('name', '')
    if not name or not name.replace('-', '').replace('_', '').isalnum():
        sys.exit(f'import_game: a name of letters, digits, - and _ is needed ("{name}")')
    dest = GAMES / name
    if source == dest.resolve() or dest.resolve() in source.parents:
        sys.exit('import_game: the source is the copy, or inside it')
    if dest.exists():
        for item in dest.iterdir():
            if item.name in LEFT_OUT:
                continue
            shutil.rmtree(item) if item.is_dir() and not item.is_symlink() else item.unlink()
    dest.mkdir(parents=True, exist_ok=True)
    count = 0
    for item in sorted(source.iterdir()):
        if item.name in LEFT_OUT or item.name == HEADER:
            continue
        if item.is_dir():
            shutil.copytree(item, dest / item.name, ignore=shutil.ignore_patterns('__pycache__'))
            count += sum(1 for f in (dest / item.name).rglob('*') if f.is_file())
        else:
            shutil.copy2(item, dest / item.name)
            count += 1
    if args.playthrough:
        data['playthrough'] = args.playthrough
        (dest / 'wgf.json').write_text(json.dumps(data, indent='\t') + '\n', encoding='utf-8')
    (dest / HEADER).write_text(header(source, args.source, name, args.playthrough), encoding='utf-8')
    print(f'import_game: {count} files from {source} into {dest.relative_to(ROOT)}, with {HEADER}')


if __name__ == '__main__':
    main()
