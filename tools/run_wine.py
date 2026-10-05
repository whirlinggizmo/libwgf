#!/usr/bin/env python3
"""Run a Windows program under Wine: the windows-x64-mingw-* presets' tests run through
it when built on Linux or macOS (it is their CMAKE_CROSSCOMPILING_EMULATOR).

    tools/run_wine.py program.exe [args...]

Which Wine: $WINE if set, else wine64 or wine on PATH, else the newest Proton in a
Steam library (its files/bin/wine; Proton is Valve's Wine, installed from Steam's
Library > Tools). The Wine prefix, its fake C: drive and registry, shared by every
build, is wine/ in the per-user cache (tools/usercache.py: ~/.cache/libwgf/wine on
Linux) unless WINEPREFIX says otherwise; it is made on first use, which takes a few
seconds. Wine's own debug output is off unless WINEDEBUG is set. Exits with the
program's exit code, or 127 when there is no Wine.

Wine sometimes fails to start a program at all, right after its server restarts
("Application could not be started, or no application associated with the specified
file", then "ShellExecuteEx failed"): the program never ran, so that isn't its failure.
Then it is started again, at most twice, saying so. Standard library only. The finding
is tools/wine.py.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from usercache import cache_dir  # noqa: E402
from wine import find_wine  # noqa: E402

# what Wine says when its launcher failed and the program never ran
NOT_STARTED = b'ShellExecuteEx failed'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('program', help='the Windows program to run')
    parser.add_argument('args', nargs=argparse.REMAINDER, help="the program's own arguments, passed as they are")
    opts = parser.parse_args()
    wine = find_wine()
    if not wine:
        print('run_wine: no Wine found (install wine64, or Proton from Steam\'s Library > Tools, or set WINE)',
              file=sys.stderr)
        return 127
    env = dict(os.environ)
    env.setdefault('WINEPREFIX', str(cache_dir('wine')))
    env.setdefault('WINEDEBUG', '-all')
    Path(env['WINEPREFIX']).mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        done = subprocess.run([wine, opts.program, *opts.args], env=env, capture_output=True)
        sys.stdout.buffer.write(done.stdout)
        sys.stdout.flush()
        sys.stderr.buffer.write(done.stderr)
        sys.stderr.flush()
        never_ran = done.returncode != 0 and NOT_STARTED in done.stdout + done.stderr
        if not never_ran or attempt == 2:
            return done.returncode
        print(f'run_wine: Wine didn\'t start {opts.program} (its launcher failed, the program never ran); '
              'starting it again', file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
