#!/usr/bin/env python3
"""Check the Haxe binding: generated, covering every call once, and passing its test on
every target this machine has.

    tools/check_binding.py [--only STEP[,STEP...]] [--browser PATH]

The steps, in order (docs/BINDINGS.md):
  generated   tools/gen_binding.py --check: nothing generated is stale
  coverage    every exported call is reached by exactly one member of the typed API, or
              by the runtime when the generator skips it; and each member calls C once
  hxcpp       the test (bindings/haxe/test/Main.hx) built by hxcpp against this
              machine's staged headless variant, and run
  node        the test built for the JS target, run under node on the headless web
              host (wasm32-debug-headless, tools/webhost.py)
  browser     the test on the full web host (wasm32-debug) in its page
              (hosts/web/page.html), in a headless Chromium-based browser
A step that can't run here (no haxe, no hxcpp, no Emscripten, node, or browser) says
`check_binding: SKIPPING <step> (<why>)`, and the last line repeats every skip. Exits 0
when every step that ran passed. Standard library only.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import binding as places  # noqa: E402
import browser  # noqa: E402
import headers  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = places.ROOT
BINDING = places.BINDING
STEPS = ('generated', 'coverage', 'hxcpp', 'node', 'browser')
VERDICT = 'binding test: '
TEST = BINDING / 'test'


def report(target, passed, output):
    result, fails = places.verdict(output, VERDICT)
    for line in fails:
        print(line)
    if result is None:
        print('\n'.join(output.strip().splitlines()[-30:]))
    print(f'check_binding: {target}: {result or "no verdict"}')
    return passed


def step_generated():
    done = subprocess.run([sys.executable, str(ROOT / 'tools' / 'gen_binding.py'), '--check'], cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    print(done.stdout.strip())
    return done.returncode == 0


def step_coverage():
    """Every exported call: reached once. Read from the Haxe the generator wrote and the
    runtime's own files, as text: Haxe, never C."""
    api = headers.read(ROOT, None, None)
    exported = {f.name for f in api.functions.values() if f.exported}
    reached = {}
    problems = []
    for path in sorted(places.SOURCE.glob('*.hx')):
        text = path.read_text(encoding='utf-8')
        generated = places.MARK in text
        for member in text.split('inline function ')[1:] if generated else []:
            calls = {piece.split('(')[0] for piece in member.split('Raw.')[1:]}
            if len(calls) > 1:  # none is sugar (isNone); more would be a second path to C
                problems.append(f'{path.name}: the member {member.split("(")[0]} calls C {len(calls)} times')
            for call in calls:
                reached.setdefault(call, []).append(path.name)
        if not generated:  # the runtime: its calls into the host by name
            for name in exported:
                if f'_{name}"' in text or f'::{name}(' in text:
                    reached.setdefault(name, []).append(path.name)
    for name in sorted(exported):
        where = reached.get(name, [])
        if len(where) != 1:
            problems.append(f'{name}: reached {len(where)} time(s) ({", ".join(where) or "never"})')
    for name in sorted(set(reached) - exported):
        problems.append(f'{name}: reached, but not an exported call')
    for problem in problems:
        print(f'check_binding: {problem}')
    if not problems:
        print(f'check_binding: every one of {len(exported)} exported calls reached once')
    return not problems


def step_hxcpp():
    if places.haxe() is None:
        return 'no haxe on PATH'
    if not places.has_hxcpp():
        return 'no hxcpp (haxelib install hxcpp)'
    passed, output = places.run_hxcpp('test', TEST, VERDICT)
    return report(f'hxcpp ({variants.native("debug-headless")})', passed, output)


def step_node():
    if places.haxe() is None:
        return 'no haxe on PATH'
    if shutil.which('node') is None:
        return 'no node on PATH'
    try:
        webhost.emcc()
        passed, output = places.run_node('test', TEST, VERDICT)
    except RuntimeError as e:
        return str(e) if 'Emscripten' in str(e) else report('node', False, str(e))
    return report(f'node ({variants.web(headless=True)})', passed, output)


def step_browser(browser_path_option):
    if places.haxe() is None:
        return 'no haxe on PATH'
    try:
        webhost.emcc()
        browser_path = browser.find_browser(browser_path_option)
    except RuntimeError as e:
        return str(e)
    try:
        passed, output = places.run_browser('test', TEST, VERDICT, browser_path)
    except RuntimeError as e:
        return report('browser', False, str(e))
    return report(f'browser ({variants.web()})', passed, output)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='comma-separated steps: ' + ', '.join(STEPS))
    ap.add_argument('--browser', help='the Chromium-based browser to use')
    args = ap.parse_args()
    chosen = args.only.split(',') if args.only else list(STEPS)
    for step in chosen:
        if step not in STEPS:
            print(f'check_binding: no step "{step}" ({", ".join(STEPS)})', file=sys.stderr)
            return 2
    skipped, failed = [], []
    for step in chosen:
        print(f'== {step}', flush=True)
        if step == 'generated':
            outcome = step_generated()
        elif step == 'coverage':
            outcome = step_coverage()
        elif step == 'hxcpp':
            outcome = step_hxcpp()
        elif step == 'node':
            outcome = step_node()
        else:
            outcome = step_browser(args.browser)
        if isinstance(outcome, str):
            print(f'check_binding: SKIPPING {step} ({outcome})', flush=True)
            skipped.append(f'{step} ({outcome})')
        elif not outcome:
            failed.append(step)
    note = f'; skipped: {", ".join(skipped)}' if skipped else ''
    if failed:
        print(f'check_binding: FAIL ({", ".join(failed)}){note}')
        return 1
    print(f'check_binding: PASS{note}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
