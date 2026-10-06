#!/usr/bin/env python3
"""Run the feature test on every target this machine has, and fail when a public call
isn't reached by it.

    tools/check_features.py [--only STEP[,STEP...]] [--browser PATH]

The feature test (examples/haxe/feature-test/) is one Haxe program reaching the whole
public API through the binding, every component in a scene among it. Built with
-D wgf_reach, each of the binding's C calls counts itself (wgf.impl.Reach), and the
program fails naming each call it never made. The steps:
  hxcpp       built by hxcpp against this machine's staged headless variant, run from a
              bin/ directory with the examples' assets beside it
  node        built for the JS target, run under node on the headless web host, the
              assets copied into its storage
  browser     on the full web host in its page, in a headless Chromium-based browser,
              the assets served beside it
A step that can't run here says `check_features: SKIPPING <step> (<why>)`, and the last
line repeats every skip. Exits 0 when every step that ran passed. Standard library only.
"""
import argparse
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import binding as places  # noqa: E402
import browser  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = places.ROOT
PROGRAM = ROOT / 'examples' / 'haxe' / 'feature-test'
STEPS = ('hxcpp', 'node', 'browser')
VERDICT = 'feature test: '
DEFINES = ['wgf_reach']


def report(target, passed, output):
    result, fails = places.verdict(output, VERDICT)
    for line in fails:
        print(line)
    if result is None:
        print('\n'.join(output.strip().splitlines()[-30:]))
    print(f'check_features: {target}: {result or "no verdict"}')
    return passed


def step(name, browser_option):
    if places.haxe() is None:
        return 'no haxe on PATH'
    if name == 'hxcpp':
        if not places.has_hxcpp():
            return 'no hxcpp (haxelib install hxcpp)'
        passed, output = places.run_hxcpp('feature-test', PROGRAM, VERDICT, DEFINES, assets=True)
        return report(f'hxcpp ({variants.native("debug-headless")})', passed, output)
    try:
        webhost.emcc()
    except RuntimeError as e:
        return str(e)
    if name == 'node':
        if shutil.which('node') is None:
            return 'no node on PATH'
        passed, output = places.run_node('feature-test', PROGRAM, VERDICT, DEFINES, assets=True)
        return report(f'node ({variants.web(headless=True)})', passed, output)
    try:
        browser_path = browser.find_browser(browser_option)
    except RuntimeError as e:
        return str(e)
    passed, output = places.run_browser('feature-test', PROGRAM, VERDICT, browser_path, DEFINES)
    return report(f'browser ({variants.web()})', passed, output)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='comma-separated steps: ' + ', '.join(STEPS))
    ap.add_argument('--browser', help='the Chromium-based browser to use')
    args = ap.parse_args()
    chosen = args.only.split(',') if args.only else list(STEPS)
    for name in chosen:
        if name not in STEPS:
            print(f'check_features: no step "{name}" ({", ".join(STEPS)})', file=sys.stderr)
            return 2
    skipped, failed = [], []
    for name in chosen:
        print(f'== {name}', flush=True)
        try:
            outcome = step(name, args.browser)
        except RuntimeError as e:
            print(f'check_features: {e}')
            outcome = False
        if isinstance(outcome, str):
            print(f'check_features: SKIPPING {name} ({outcome})', flush=True)
            skipped.append(f'{name} ({outcome})')
        elif not outcome:
            failed.append(name)
    note = f'; skipped: {", ".join(skipped)}' if skipped else ''
    if failed:
        print(f'check_features: FAIL ({", ".join(failed)}){note}')
        return 1
    print(f'check_features: PASS{note}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
