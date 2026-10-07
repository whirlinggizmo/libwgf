#!/usr/bin/env python3
"""Check the JS binding (bindings/js/): generated, typed, and every JS example running in
a browser through it.

    tools/check_js_binding.py [--only STEP[,STEP...]] [--browser PATH]

The steps, in order:
  generated   tools/gen_binding.py --check: wgf.js, wgf-typed.js, and their .d.ts current
  types       the declarations hold up under TypeScript: bindings/js/tests/types.ts
              compiles with --strict, and each mistake it marks @ts-expect-error is
              caught. Needs tsc: TSC naming one, one on PATH, or TypeScript TYPESCRIPT
              in npm's cache
  examples    each JS example (examples/js/<name>/) built as a site on the full web host
              (wasm32-debug), loaded in a headless Chromium-based browser, and flown by
              its autopilot (example.json's; else 120 frames) to a PASS with no error;
              then again trimmed, as a release export is (its host and both binding
              layers cut to what it names), so a call the trimming lost is caught
A step that can't run here (no clang, tsc, Emscripten, or browser) says
`check_js_binding: SKIPPING <step> (<why>)`, and the last line repeats every skip. Exits 0
when every step that ran passed. Standard library only.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
sys.path.insert(0, str(Path(__file__).resolve().parent / 'wgf'))
import browser  # noqa: E402
import game as games  # noqa: E402  (the wgf tool's: how a page is flown by an autopilot)
import jsbinding  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
STEPS = ('generated', 'types', 'examples')


TYPESCRIPT = '7.0.2'  # the one CI installs (.github/workflows/ci.yml)


def tsc():
    """How to run tsc here, or None: TSC's, else one on PATH, else TYPESCRIPT from npm's
    cache, offline (never fetched here). A tsc on PATH is a node script: run through node
    where one is found, since its `#!/usr/bin/env node` may find none (emsdk's)."""
    found = os.environ.get('TSC') or shutil.which('tsc')
    if found:
        node = shutil.which('node') or os.environ.get('EMSDK_NODE')
        return [node, found] if node and not found.endswith(('.exe', '.cmd')) else [found]
    npx = shutil.which('npx')
    if npx:
        cached = [npx, '--offline', '--yes', '-p', f'typescript@{TYPESCRIPT}', 'tsc']
        if subprocess.run([*cached, '--version'], capture_output=True).returncode == 0:
            return cached
    return None


def step(name, browser_option):
    if name == 'generated':
        done = subprocess.run([sys.executable, ROOT / 'tools' / 'gen_binding.py', '--check'])
        return done.returncode == 0
    if name == 'types':
        command = tsc()
        if command is None:
            return f'no tsc (npm install -g typescript@{TYPESCRIPT}, or TSC naming one)'
        done = subprocess.run([*command, '--noEmit', '--strict', '--target', 'es2022', '--module', 'es2022',
                               '--moduleResolution', 'bundler', 'tests/types.ts'], cwd=jsbinding.BINDING)
        print(f'check_js_binding: types: {"PASS" if done.returncode == 0 else "FAIL"}')
        return done.returncode == 0
    try:
        webhost.emcc()
    except RuntimeError as e:
        return str(e)
    try:
        found = browser.find_browser(browser_option)
    except RuntimeError as e:
        return str(e)
    ok = True
    for example, trimmed in [(p, cut) for p in sorted(p for p in webhost.JS_EXAMPLES.iterdir()
                                                      if (p / 'main.js').is_file()) for cut in (False, True)]:
        site = Path(variants.work(variants.web())) / 'js-examples' / (example.name + ('-trimmed' if trimmed else ''))
        webhost.build_js_example(example, variants.web(), site, trimmed=trimmed)
        _, autopilot_path = webhost.js_example_config(example)
        autopilot = (autopilot_path.read_text(encoding='utf-8') if autopilot_path
                     else games.frames_autopilot(120))
        lines = games.run_page(site, 'index.html', autopilot, browser_path=found, echo=False, timeout=300)
        passed = games.judged(lines)
        for line in lines:
            if '[ERROR]' in line or 'FAIL' in line or 'uncaught' in line:
                print(line)
        verdict = {True: 'PASS', False: 'FAIL', None: 'no verdict'}[passed]
        print(f'check_js_binding: {example.name}{" trimmed" if trimmed else ""}: {verdict}'
              + (f' ({autopilot_path.relative_to(ROOT).as_posix()})' if autopilot_path else ' (120 frames)'))
        ok = ok and passed is True
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--only', help='comma-separated steps: ' + ', '.join(STEPS))
    ap.add_argument('--browser', help='a Chromium-based browser (default: found)')
    args = ap.parse_args()
    steps = args.only.split(',') if args.only else list(STEPS)
    unknown = [s for s in steps if s not in STEPS]
    if unknown:
        print(f'check_js_binding: no step {", ".join(unknown)} ({", ".join(STEPS)})')
        return 2
    failed, skipped = [], []
    for name in steps:
        print(f'== {name}', flush=True)
        result = step(name, args.browser)
        if isinstance(result, str):
            print(f'check_js_binding: SKIPPING {name} ({result})')
            skipped.append(f'{name} ({result})')
        elif not result:
            failed.append(name)
    tail = f'; SKIPPED {", ".join(skipped)}' if skipped else ''
    print(f'check_js_binding: {"FAIL (" + ", ".join(failed) + ")" if failed else "PASS"}{tail}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
