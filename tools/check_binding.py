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
import json
import os
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402
import examples  # noqa: E402
import binding as places  # noqa: E402
import headers  # noqa: E402
import server  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
BINDING = ROOT / 'bindings' / 'haxe'
STEPS = ('generated', 'coverage', 'hxcpp', 'node', 'browser')
VERDICT = 'binding test: '


def work_dir(name):
    path = Path(variants.work(variants.native('debug-headless'))) / 'binding' / name
    path.mkdir(parents=True, exist_ok=True)
    return path


def run(command, what, cwd=ROOT, timeout=900):
    done = subprocess.run([str(c) for c in command], cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace', timeout=timeout)
    return done.returncode == 0, done.stdout


def verdict(output):
    """The test's PASS or FAIL line from its log, and the lines that said FAIL."""
    lines = output.splitlines()
    found = [line for line in lines if VERDICT in line]
    fails = [line for line in lines if 'FAIL' in line or 'ERROR' in line]
    return (found[-1].split(VERDICT, 1)[1].strip() if found else None), fails


def step_generated():
    ok, out = run([sys.executable, ROOT / 'tools' / 'gen_binding.py', '--check'], 'gen_binding')
    print(out.strip())
    return ok


def step_coverage():
    """Every exported call: reached once. Read from the Haxe the generator wrote and the
    runtime's own files, as text: Haxe, never C."""
    api = headers.read(ROOT, None, None)
    exported = {f.name for f in api.functions.values() if f.exported}
    reached = {}
    problems = []
    for path in sorted((BINDING / 'src' / 'wgf').glob('*.hx')):
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


def haxe_command():
    return shutil.which('haxe')


def build_test(target, out, extra):
    command = [haxe_command(), '-cp', BINDING / 'src', '-cp', BINDING / 'test', '--main', 'Main', *target, *extra]
    return run(command, 'haxe', cwd=BINDING)


def step_hxcpp():
    if haxe_command() is None:
        return 'no haxe on PATH'
    ok, out = run(['haxelib', 'path', 'hxcpp'], 'haxelib')
    if not ok:
        return 'no hxcpp (haxelib install hxcpp)'
    variant = variants.native('debug-headless')
    examples.prepare(variant)
    staged = variants.out(variant)
    work = work_dir('hxcpp')
    ok, out = build_test(['--cpp', work], work, ['-D', 'HXCPP_M64', '-D', f'wgf_out={staged.as_posix()}', '-D', 'wgf_headless',
                                                 '-D', f'wgf_binding={BINDING.as_posix()}'])
    if not ok:
        print('\n'.join(out.strip().splitlines()[-30:]))
        return False
    program = work / ('Main.exe' if os.name == 'nt' else 'Main')
    done = subprocess.run([str(program)], cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          errors='replace', timeout=120)
    result, fails = verdict(done.stdout)
    for line in fails:
        print(line)
    print(f'check_binding: hxcpp ({variant}): {result or "no verdict"}, exit {done.returncode}')
    return done.returncode == 0 and result == 'PASS'


def js_test(work):
    ok, out = build_test(['--js', work / 'binding_test.js'], work, ['-D', 'js-es=6', '-dce', 'full'])
    if not ok:
        print('\n'.join(out.strip().splitlines()[-30:]))
    return ok


def step_node():
    if haxe_command() is None:
        return 'no haxe on PATH'
    node = shutil.which('node')
    if node is None:
        return 'no node on PATH'
    try:
        webhost.emcc()
    except RuntimeError as e:
        return str(e)
    variant = variants.web(headless=True)
    try:
        host = webhost.build(variant)
    except RuntimeError as e:
        print(f'check_binding: {e}')
        return False
    work = work_dir('node')
    if not js_test(work):
        return False
    done = subprocess.run([node, str(BINDING / 'test' / 'node.mjs'), str(host / 'wgf-host.js'),
                           str(work / 'binding_test.js')], cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace', timeout=120)
    result, fails = verdict(done.stdout)
    for line in fails:
        print(line)
    print(f'check_binding: node ({variant}): {result or "no verdict"}, exit {done.returncode}')
    return done.returncode == 0 and result == 'PASS'


def write_page(directory, program):
    page = (ROOT / 'hosts' / 'web' / 'page.html').read_text(encoding='utf-8').replace('@WGF_PROGRAM@', program)
    (directory / 'index.html').write_text(page, encoding='utf-8')


def step_browser(browser_path_option):
    if haxe_command() is None:
        return 'no haxe on PATH'
    try:
        webhost.emcc()
        browser_path = browser.find_browser(browser_path_option)
    except RuntimeError as e:
        return str(e)
    variant = variants.web()
    work = work_dir('browser')
    try:
        webhost.build(variant, out=work)
    except RuntimeError as e:
        print(f'check_binding: {e}')
        return False
    if not js_test(work):
        return False
    write_page(work, 'binding_test')
    base, httpd = server.serve(work)
    processes = browser.RunProcesses('check-binding')
    processes.install_handlers()
    lines, done = [], threading.Event()

    def on_event(message):
        if message.get('method') == 'Runtime.consoleAPICalled':
            text = ' '.join(str(a.get('value', a.get('description', ''))) for a in message['params']['args'])
            lines.append(text)
            if VERDICT in text:
                done.set()
        elif message.get('method') == 'Runtime.exceptionThrown':
            details = message['params']['exceptionDetails']
            lines.append('ERROR exception: ' + str(details.get('exception', {}).get('description', details.get('text'))))
            done.set()

    try:
        debug_base, session = browser.launch_browser(processes, browser_path, 'headless')
        session.close()
        target = json.loads(browser.wait_for(f'{debug_base}/json/list', 'page'))
        page = next(t for t in target if t.get('type') == 'page')
        session = browser.open_session(page['webSocketDebuggerUrl'])
        session.on_event(on_event)
        session.send('Runtime.enable')
        session.send('Page.enable')
        started = time.monotonic()
        session.send('Page.navigate', {'url': f'{base}/index.html'})
        done.wait(60)
        session.close()
    finally:
        processes.stop()
        httpd.shutdown()
    result, fails = verdict('\n'.join(lines))
    for line in fails:
        print(line)
    print(f'check_binding: browser ({variant}): {result or "no verdict within 60 s"} '
          f'({time.monotonic() - started:.1f} s)')
    return result == 'PASS'


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
