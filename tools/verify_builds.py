#!/usr/bin/env python3
"""Run before calling a change done: every build and check this machine can run.

    tools/verify_builds.py [--web] [--windows HOST] [--only STEP[,STEP...]] [--list]

This machine's own presets, each configured, built, and tested (ctest, which runs the
header checks, tools/check_api.py, and tools/check_tools.py too), fastest first:
  linux-x64-debug-headless, -debug, -release, -debug-ubsan, -debug-asan, -debug-tsan
                  on Linux (on Windows, windows-x64-msvc-debug-headless, -debug, and
                  -release, through Visual Studio's generator)
  windows-x64-mingw-debug-headless, -debug
                  on Linux, when MinGW-w64 is installed: built, and tested under Wine
                  when there is one (tools/wine.py)
then the checks of what is built on them (CHECKS below; each names what it needs):
  smoke           every example headless (tools/run_smoke.py)
  smoke-mingw     the same built with MinGW-w64, under Wine
  desktop         every example in a window on a virtual display (tools/check_desktop.py)
  desktop-mingw   the same built with MinGW-w64, under Wine, on the virtual display
With --web, also (needs Emscripten; the browser tests need a Chromium-based browser):
  wasm32-debug-headless, wasm32-debug, wasm32-release
                  configured, built, and tested (node, and the browser tests; the
                  headless one under node alone, as the binding's tests run)
and the web's check:
  web             every example in a browser (tools/check_web.py)
The binding's checks (tools/check_binding.py), when there is a haxe:
  binding         generated files current, every call reached once, the test on hxcpp
  binding-web     with --web: the test under node on the headless host, and in a browser
  features        the feature test, every public call reached, on hxcpp (tools/check_features.py)
  features-web    with --web: the same under node, and in a browser
With --windows HOST, also, on that Windows machine over ssh, the working tree as it is
(tools/run_remote_windows.py, nothing left there):
  windows-msvc    windows-x64-msvc-debug-headless and -debug, then every example
                  headless (tools/run_smoke.py), and the binding's test and the feature test on hxcpp
  windows-mingw   windows-x64-mingw-debug-headless and -debug, natively, then the same

--only runs the steps named (a preset's name, or a check's); --list prints the steps it
would run, and runs nothing. Stops at the first step that fails; a PASS names what was
skipped for want of a tool, so it can't be misread. Standard library only.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))  # an embedded Python (Windows) doesn't add it
from variants import names, native, web  # noqa: E402
from wine import find_wine  # noqa: E402

NATIVE = ['debug-headless', 'debug', 'release', 'debug-ubsan', 'debug-asan', 'debug-tsan']  # fastest first
MINGW = ['windows-x64-mingw-debug-headless', 'windows-x64-mingw-debug']
WEB = [web(headless=True), web(), web(debug=False)]
# The checks after the presets: name -> (command, needs the web, what else it needs:
# None, or (a program on PATH, or a platform prefix such as 'linux', and why it's needed)).
CHECKS = {
    'smoke': (['tools/run_smoke.py'], False, None),
    'smoke-mingw': (['tools/run_smoke.py', '--variant', 'windows-x64-mingw-debug-headless'], False,
                    ('x86_64-w64-mingw32-gcc', 'no MinGW-w64')),
    'desktop': (['tools/check_desktop.py'], False, ('Xvfb', 'no Xvfb, the virtual X display')),
    'desktop-mingw': (['tools/check_desktop.py', '--variant', 'windows-x64-mingw-debug'], False,
                      ('x86_64-w64-mingw32-gcc', 'no MinGW-w64')),
    'web': (['tools/check_web.py'], True, None),
    'binding': (['tools/check_binding.py', '--only', 'generated,coverage,hxcpp'], False, ('haxe', 'no haxe')),
    'binding-web': (['tools/check_binding.py', '--only', 'node,browser'], True, ('haxe', 'no haxe')),
    'features': (['tools/check_features.py', '--only', 'hxcpp'], False, ('haxe', 'no haxe')),
    'features-web': (['tools/check_features.py', '--only', 'node,browser'], True, ('haxe', 'no haxe')),
}
REMOTE = {'windows-msvc': ['--msvc', '--then', 'tools/run_smoke.py --variant windows-x64-msvc-debug-headless',
                           '--then', 'tools/check_binding.py --only hxcpp', '--then', 'tools/check_features.py --only hxcpp'],
          'windows-mingw': ['--then', 'tools/run_smoke.py --variant windows-x64-mingw-debug-headless']}


def names_of_host():
    """The variants this machine's presets have: debug-headless, debug, ..."""
    prefix = native('debug').rsplit('-debug', 1)[0] + '-'
    return {n[len(prefix):] for n in names() if n.startswith(prefix)}


def available(need):
    if need is None:
        return True
    what = need[0]
    return sys.platform.startswith(what) if what in ('linux', 'win32', 'darwin') else shutil.which(what) is not None


def steps(with_web, windows):
    """(name, what to run) for each step, in order; what to run is a preset's tested
    flag, or the command of a check."""
    notes = []
    if sys.platform.startswith('linux'):
        out = [(native(v), True) for v in NATIVE]
        if shutil.which('x86_64-w64-mingw32-gcc'):
            wine = find_wine() is not None
            out += [(p, wine) for p in MINGW]
            if not wine:
                notes.append('the MinGW presets\' tests (no Wine)')
        else:
            notes.append('the windows-x64-mingw presets (no MinGW-w64: x86_64-w64-mingw32-gcc)')
    elif os.name == 'nt':
        out = [(native(v), True) for v in NATIVE if v in names_of_host()]
    else:
        out = []
        notes.append(f'every preset (libwgf has none for {sys.platform} yet)')
    if with_web:
        out += [(p, True) for p in WEB]
    for name, (command, needs_web, need) in CHECKS.items():
        if needs_web and not with_web:
            continue
        if available(need):
            out.append((name, command))
        else:
            notes.append(f'{name} ({need[1]})')
    if windows:
        out += [(step, REMOTE[step]) for step in REMOTE]
    return out, notes


def why_skipped(preset, tests):
    """Each skipped test's own SKIPPING line: ctest shows no output of a test it skipped,
    so those few run again, verbosely (a skip exits at once)."""
    pattern = '|'.join(re.escape(t) for t in tests)  # a test's name, never a pattern
    done = subprocess.run(['ctest', '--preset', preset, '-V', '-R', f'^({pattern})$'], cwd=ROOT, capture_output=True,
                          text=True, errors='replace')
    return [line.split(': ', 1)[1] for line in done.stdout.splitlines() if 'SKIPPING' in line and ': ' in line]


def run(*cmd, skipped=None):
    """Run a command, its output shown as it comes; True when it succeeds. `skipped`, a
    list, collects the tests ctest says it skipped (exit 77: a tool missing)."""
    print('$ ' + ' '.join(cmd), flush=True)
    if skipped is None:
        return subprocess.run(cmd, cwd=ROOT).returncode == 0
    with subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          errors='replace') as process:
        for line in process.stdout:
            sys.stdout.write(line)
            if '(Skipped)' in line and ' - ' in line:  # ctest's list of the tests that did not run
                skipped.append(line.split(' - ', 1)[1].replace('(Skipped)', '').strip())
    sys.stdout.flush()
    return process.returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--web', action='store_true', help='the web presets too, and the web checks')
    ap.add_argument('--windows', metavar='HOST', help='also build and test on this Windows machine over ssh')
    ap.add_argument('--only', help='comma-separated steps to run')
    ap.add_argument('--list', action='store_true', help='print the steps, and run nothing')
    args = ap.parse_args()
    only = set(args.only.split(',')) if args.only else None

    web_checks = {name for name, (_, needs_web, _) in CHECKS.items() if needs_web}
    with_web = args.web or bool(only and only & (set(WEB) | web_checks))
    if only and only & set(REMOTE) and not args.windows:
        sys.exit('verify_builds: the windows-msvc and windows-mingw steps need --windows HOST')
    plan, notes = steps(with_web, args.windows)
    if only:
        unknown = only - {name for name, _ in plan}
        if unknown:
            sys.exit(f'verify_builds: no step {", ".join(sorted(unknown))} on this machine (--list shows them)')
        plan = [(name, what) for name, what in plan if name in only]
    for note in notes:  # before anything runs, so a skip is never only in the last line
        print(f'verify_builds: SKIPPING {note}', flush=True)
    if args.list:
        for name, _ in plan:
            print(name)
        return
    start_all = time.monotonic()
    for name, what in plan:
        print(f'== {name}', flush=True)
        start = time.monotonic()
        tests_skipped = []
        if name in REMOTE:
            ok = run(sys.executable, 'tools/run_remote_windows.py', args.windows, *what, skipped=tests_skipped)
            if tests_skipped:
                notes.append(f'{len(tests_skipped)} of {name}\'s tests on {args.windows} ({", ".join(tests_skipped)})')
                print(f'verify_builds: SKIPPED {notes[-1]}', flush=True)
        elif name in CHECKS:
            ok = run(sys.executable, *what)
        else:
            ok = (run('cmake', '--preset', name) and run('cmake', '--build', '--preset', name)
                  and (not what or run('ctest', '--preset', name, '--output-on-failure', skipped=tests_skipped)))
            if tests_skipped:
                notes.append(f'{len(tests_skipped)} of {name}\'s tests ({", ".join(tests_skipped)})')
                print(f'verify_builds: SKIPPED {notes[-1]}; why, from each:', flush=True)
                for line in why_skipped(name, tests_skipped):
                    print(f'  {line}', flush=True)
        if not ok:
            sys.exit(f'verify_builds: FAIL at {name}')
        print(f'== {name}: ok ({time.monotonic() - start:.0f} s)', flush=True)
    skipped = f'; SKIPPED: {"; ".join(notes)}' if notes else ''
    print(f'verify_builds: PASS ({len(plan)} steps, {time.monotonic() - start_all:.0f} s{skipped})')


if __name__ == '__main__':
    main()
