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
  local       the calls the binding works itself (jsbinding's LOCAL, bindings/js/src/
              local.js) held to the host's C, to the bit: bindings/js/tests/local.mjs
              under node on the full headless host (wasm32-debug-headless)
  examples    each JS example (examples/js/<name>/) built as a site on the full web host
              (wasm32-debug), loaded in a headless Chromium-based browser, and flown by
              its autopilot (example.json's; else 120 frames) to a PASS with no error;
              then again trimmed, as a release export is (its host and both binding
              layers cut to what it names), so a call the trimming lost is caught
  input       text typed into the Clay UI's text field in a browser, as a person's would be
              (bindings/js/tests/input/, the browser's own input events through its debugging
              protocol): a press where the pointer never moved focuses the field and the
              page's hidden input; keys type; text the browser puts in the input (a phone's
              keyboard, its paste) is typed; and Ctrl+V pastes the clipboard once. A browser
              that refuses the clipboard skips the paste, and says so
A step that can't run here (no clang, tsc, Emscripten, or browser) says
`check_js_binding: SKIPPING <step> (<why>)`, and the last line repeats every skip. Exits 0
when every step that ran passed. Standard library only.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
sys.path.insert(0, str(Path(__file__).resolve().parent / 'wgf'))
import browser  # noqa: E402
import game as games  # noqa: E402  (the wgf tool's: how a page is flown by an autopilot)
import jsbinding  # noqa: E402
import server  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
STEPS = ('generated', 'types', 'local', 'examples', 'input')


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


INPUT_PAGE = jsbinding.BINDING / 'tests' / 'input'


def check_input(found):
    """The input step: True, False, or what it skipped (the rest having passed)."""
    site = Path(variants.work(variants.web())) / 'js-tests' / 'input'
    webhost.build_js_example(INPUT_PAGE, variants.web(), site)
    base, httpd = server.serve(site)
    run = browser.RunProcesses('check-js-input')
    run.install_handlers()
    try:
        debug_base, top = browser.launch_browser(run, found, 'headless')
        context = top.send('Target.createBrowserContext', {'disposeOnDetach': True})['browserContextId']
        target = top.send('Target.createTarget', {'url': 'about:blank', 'browserContextId': context})['targetId']
        session = browser.open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{target}')
        for domain in ('Page', 'Runtime'):
            session.send(f'{domain}.enable')
        top.try_send('Browser.grantPermissions', {'permissions': ['clipboardReadWrite', 'clipboardSanitizedWrite'],
                                                  'browserContextId': context, 'origin': base})
        session.send('Page.navigate', {'url': base + '/index.html'})

        def js(expression):
            result = session.send('Runtime.evaluate', {'expression': expression, 'awaitPromise': True,
                                                       'returnByValue': True})
            if 'exceptionDetails' in result:
                raise RuntimeError(json.dumps(result['exceptionDetails'])[:300])
            return result['result'].get('value')

        def frames(count):
            """Wait for `count` more frames."""
            until, deadline = (js('globalThis.frames || 0') or 0) + count, time.time() + 30
            while (js('globalThis.frames || 0') or 0) < until:
                if time.time() > deadline:
                    raise RuntimeError('the page stopped drawing frames')
                time.sleep(0.05)

        def key(kind, name, code, virtual, modifiers=0, text=None, commands=None):
            params = {'type': kind, 'key': name, 'code': code, 'windowsVirtualKeyCode': virtual, 'modifiers': modifiers}
            if text:
                params['text'] = text
            if commands:
                params['commands'] = commands
            session.send('Input.dispatchKeyEvent', params)

        deadline = time.time() + 60
        while not js('typeof globalThis.wgf === "object" && (globalThis.frames || 0) > 5'):
            if time.time() > deadline:
                raise RuntimeError('the page never ran')
            time.sleep(0.1)
        x, y = js('(() => { const b = wgf.wgf_ui_get_bounds("name"), r = document.getElementById("canvas")'
                  '.getBoundingClientRect(); return [r.left + b.x + b.z / 2, r.top + b.y + b.w / 2]; })()')
        problems = []
        # a press with no move before it: the pointer has never been over the page
        session.send('Input.dispatchMouseEvent', {'type': 'mousePressed', 'x': x, 'y': y, 'button': 'left',
                                                  'buttons': 1, 'clickCount': 1})
        frames(3)
        session.send('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'x': x, 'y': y, 'button': 'left',
                                                  'clickCount': 1})
        frames(4)
        if js('wgf.wgf_ui_get_focus()') != 'name':
            problems.append('a press where the pointer never moved did not focus the field')
        if js('document.activeElement && document.activeElement.tagName') != 'INPUT':
            problems.append("the page's hidden input was not focused for the field")
        for letter in 'ab':
            key('keyDown', letter, 'Key' + letter.upper(), ord(letter.upper()), text=letter)
            key('keyUp', letter, 'Key' + letter.upper(), ord(letter.upper()))
        frames(4)
        typed = js('wgf.wgf_ui_get_text("name")')
        if typed != 'ab':
            problems.append(f'keys a and b typed {typed!r}')
        session.send('Input.insertText', {'text': ' cd'})  # as a phone's keyboard, or its paste, gives text
        frames(6)
        given = js('wgf.wgf_ui_get_text("name")')
        if given != 'ab cd':
            problems.append(f'text put in the input left {given!r}, not "ab cd"')
        skipped = None
        try:
            js('navigator.clipboard.writeText(" once")')
        except RuntimeError as e:
            skipped = f"its paste: the browser refused the clipboard ({str(e)[:80]})"
        if skipped is None:
            key('rawKeyDown', 'Control', 'ControlLeft', 17, 2)
            key('rawKeyDown', 'v', 'KeyV', 86, 2, commands=['paste'])
            key('keyUp', 'v', 'KeyV', 86, 2)
            key('keyUp', 'Control', 'ControlLeft', 17, 0)
            frames(6)
            pasted = js('wgf.wgf_ui_get_text("name")')
            if pasted != 'ab cd once':
                problems.append(f'Ctrl+V left {pasted!r}, not "ab cd once"')
        session.close()
        top.close()
    except RuntimeError as e:
        problems, skipped = [str(e)], None
    finally:
        run.stop()
        httpd.shutdown()
    for problem in problems:
        print(f'check_js_binding: input: {problem}')
    print(f'check_js_binding: input: {"FAIL" if problems else "PASS"}')
    if problems:
        return False
    return skipped if skipped else True


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
    if name == 'local':
        if shutil.which('node') is None:
            return 'no node on PATH'
        host = webhost.build(variants.web(headless=True))  # the full host: every call's C, to compare with
        done = subprocess.run(['node', str(jsbinding.BINDING / 'tests' / 'local.mjs'), str(host)])
        return done.returncode == 0
    try:
        found = browser.find_browser(browser_option)
    except RuntimeError as e:
        return str(e)
    if name == 'input':
        return check_input(found)
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
