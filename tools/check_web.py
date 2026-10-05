#!/usr/bin/env python3
"""Run every C example on the web, in a browser, and fail any that doesn't start, starts
on the wrong backend, reports an error, or is still loading when its time is up; save a
screenshot of each.

    tools/check_web.py [--variant NAME] [--startup MS] [--settle MS] [--quiet MS]
                       [--jobs N] [--headed] [--out DIR] [--browser PATH] [--verbose]
                       [example ...]

Builds the variant (default wasm32-debug), stages it, builds the examples against it
(tools/examples.py, with WGF_CHECK_EXPORTS: the loader's pending count and log exported
for this check alone), serves the variant's site (out/wasm32/<variant>/site/, where they are built), and loads each in a Chromium-based browser
(tools/browser.py), --jobs at a time (default 4), each in a browser context of its own,
so no example sees another's storage: headless, with WebGL2 on the CPU (--headed: on
the screen).

An example has started when gfx logs its backend ("wgf_gfx: ..."), within --startup ms
(default 30000), and must have started on the variant's (else the build is stale). It
is then checked once nothing is loading -- the loader has no request pending and the
page no request in flight -- and nothing has changed for --quiet ms (default 1500), or
--settle ms after it started (default 20000), whichever is first; one still loading
then fails, and the loader names what is stuck and where. It fails if, from loading
until then, it throws, logs a console error (but core's log lines below error level,
which arrive as console errors), logs an [ERROR] or [FATAL] line or a sokol panic, or
the browser logs an error (network ones aside: a missing favicon is a 404). Its
screenshot is saved in --out (default build/<preset>/check_web/<example>.png).
--verbose prints every console line. The browser and server are always stopped.
Standard library only.
"""
import argparse
import base64
import json
import re
import sys
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402
import examples  # noqa: E402
import server  # noqa: E402
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
STARTED = 'wgf_gfx: '
ERROR_LINE = re.compile(r'\[(ERROR|FATAL)')
# core's log lines below error level: Emscripten sends stderr, where they go, to console.error
QUIET_LINE = re.compile(r'\[(TRACE|DEBUG|INFO|WARN) *\]')


def serve(directory):
    """Serve `directory` on a free port, in a thread, as a host would (tools/server.py:
    the examples' files one level above the pages, examples/assets/ at /assets/): (its base URL, the server)."""
    return server.serve(directory)


def first_line(text):
    return text.strip().split('\n')[0]


PENDING = ("typeof Module !== 'undefined' && Module._wgf_core_priv_load_get_pending_count"
           " ? Module._wgf_core_priv_load_get_pending_count() : -1")
LOG_PENDING = ("typeof Module !== 'undefined' && Module._wgf_core_priv_load_log_pending"
               " && Module._wgf_core_priv_load_log_pending()")


def check(browser_session, debug_base, url, opts, shot_path):
    """Load one example in a browser context of its own: what it logged, the problems
    found, when it started, and what was still loading."""
    result = {'console': [], 'errors': [], 'started': None, 'backend': None, 'pending': -1}
    lock = threading.Lock()
    inflight = set()
    state = {'loaded': time.monotonic(), 'activity': time.monotonic()}

    def on_event(message):
        method, params = message['method'], message.get('params', {})
        with lock:
            if method == 'Network.requestWillBeSent':
                if params.get('loaderId'):  # a worker's own script reports its end to the worker
                    inflight.add(params['requestId'])
                    state['activity'] = time.monotonic()
            elif method in ('Network.loadingFinished', 'Network.loadingFailed'):
                inflight.discard(params['requestId'])
                state['activity'] = time.monotonic()
            elif method == 'Runtime.consoleAPICalled':
                text = ' '.join(str(a['value']) if 'value' in a else a.get('description', '') for a in params['args'])
                result['console'].append(first_line(text))
                if STARTED in text and result['started'] is None:
                    result['started'] = time.monotonic() - state['loaded']
                    result['backend'] = text[text.index(STARTED) + len(STARTED):].split(',')[0].strip()
                    state['activity'] = time.monotonic()
                if ERROR_LINE.search(text) or (params.get('type') == 'error' and not QUIET_LINE.match(text)):
                    result['errors'].append(first_line(text))
            elif method == 'Runtime.exceptionThrown':
                details = params['exceptionDetails']
                result['errors'].append(first_line((details.get('exception') or {}).get('description')
                                                   or details.get('text') or 'exception'))
            elif method == 'Log.entryAdded':
                entry = params['entry']
                line = f'[{entry["source"]}/{entry["level"]}] {first_line(entry.get("text") or "")}'
                result['console'].append(line)
                if entry['level'] == 'error' and entry['source'] != 'network':
                    result['errors'].append(line)

    context = browser_session.send('Target.createBrowserContext', {'disposeOnDetach': True})['browserContextId']
    target = browser_session.send('Target.createTarget', {'url': 'about:blank', 'browserContextId': context})['targetId']
    session = browser.open_session(f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{target}')
    session.on_event(on_event)
    try:
        for domain in ('Runtime', 'Log', 'Page', 'Network'):
            session.send(f'{domain}.enable')
        state['loaded'] = time.monotonic()
        session.send('Page.navigate', {'url': url})
        startup_deadline = state['loaded'] + opts.startup / 1000
        while result['started'] is None and time.monotonic() < startup_deadline:
            time.sleep(0.1)  # nothing is asked of the page before it runs: an exported call before then aborts it
        if result['started'] is not None:
            settle_deadline = state['loaded'] + result['started'] + opts.settle / 1000
            pending = -1
            while time.monotonic() < settle_deadline:
                value = session.send('Runtime.evaluate', {'expression': PENDING, 'returnByValue': True})['result'].get('value')
                with lock:
                    if value != pending:
                        pending = value
                        state['activity'] = time.monotonic()
                    if pending == 0 and not inflight and time.monotonic() - state['activity'] >= opts.quiet / 1000:
                        break
                time.sleep(0.1)
            result['pending'] = pending
            if pending and pending > 0:  # out of time: the loader names what is stuck, and where
                session.try_send('Runtime.evaluate', {'expression': LOG_PENDING})
                time.sleep(0.3)  # its warnings arrive as console events
        shot = session.try_send('Page.captureScreenshot', {'format': 'png'})
        if shot:
            shot_path.write_bytes(base64.b64decode(shot['data']))
        else:
            result['console'].append('(no screenshot: the page was still loading)')
    finally:
        session.close()
        browser_session.try_send('Target.closeTarget', {'targetId': target})
        browser_session.try_send('Target.disposeBrowserContext', {'browserContextId': context})
    return result


def problems_of(result, opts, backend):
    problems = list(result['errors'])
    if result['started'] is None:
        problems.append(f'never started (no "{STARTED}..." log) in {opts.startup} ms; its last lines:')
        problems += [f'  | {line}' for line in result['console'][-6:]]
    elif result['backend'] != backend:
        problems.append(f'started on {result["backend"]}, not {backend} (a stale build?)')
    elif result['pending'] is None or result['pending'] < 0:
        problems.append('never answered how many loads are pending (built without WGF_CHECK_EXPORTS?)')
    elif result['pending'] > 0:
        problems.append(f'still loading {opts.settle} ms after starting ({result["pending"]} pending):')
        stuck = [line for line in result['console'] if 'wgf_core_load: pending:' in line]
        problems += [f'  | {line}' for line in (stuck or result['console'][-6:])]
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--variant', help='default: wasm32-debug')
    parser.add_argument('--startup', type=int, default=30000)
    parser.add_argument('--settle', type=int, default=20000)
    parser.add_argument('--quiet', type=int, default=1500)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--headed', action='store_true', help='show the browser on the screen')
    parser.add_argument('--out', type=Path, help="default: the variant's work, build/<preset>/check_web")
    parser.add_argument('--browser')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('examples', nargs='*')
    opts = parser.parse_args()
    opts.variant = opts.variant or variants.web()
    opts.out = opts.out or variants.work(opts.variant) / 'check_web'
    if not variants.is_web(opts.variant):
        sys.exit(f'check_web: {opts.variant} isn\'t a web variant')
    names = opts.examples or examples.names()
    unknown = [n for n in names if n not in examples.names()]
    if unknown:
        sys.exit(f'check_web: no such example: {", ".join(unknown)}')
    try:
        browser_path = browser.find_browser(opts.browser)
        examples.prepare(opts.variant)
        pages = [examples.build(opts.variant, n, {'WGF_CHECK_EXPORTS': 'ON'}) for n in names]
        examples.finish(opts.variant)  # the versions, the list, and the launcher, as a deploy would have
    except RuntimeError as e:
        sys.exit(f'check_web: {e}')
    opts.out.mkdir(parents=True, exist_ok=True)
    backend = 'WebGL2 / GLES3'  # as gfx names it

    site = variants.programs(opts.variant)
    base, httpd = serve(site)
    run = browser.RunProcesses('check-web')
    run.install_handlers()
    results = [None] * len(names)
    lock = threading.Lock()
    progress = {'next': 0, 'reported': 0, 'failed': 0}

    def report():
        """Print the results that are in, in order."""
        with lock:
            while progress['reported'] < len(names) and results[progress['reported']] is not None:
                name, result = names[progress['reported']], results[progress['reported']]
                progress['reported'] += 1
                problems = problems_of(result, opts, backend)
                progress['failed'] += bool(problems)
                started = f' (started in {result["started"]:.1f} s)' if result['started'] else ''
                print(f'  {"FAIL" if problems else "ok  "}  {name}{started}')
                for problem in problems:
                    print(f'          {problem}')
                if opts.verbose:
                    for line in result['console']:
                        print(f'          | {line}')
                sys.stdout.flush()

    try:
        display = 'screen' if opts.headed else 'headless'
        debug_base, browser_session = browser.launch_browser(run, browser_path, display)
        jobs = max(1, min(opts.jobs, len(names)))
        print(f'check_web: {len(names)} example(s), {opts.variant}, {browser_path}, {display}, {jobs} at a time',
              flush=True)

        def worker():
            while True:
                with lock:
                    index = progress['next']
                    if index >= len(names):
                        return
                    progress['next'] += 1
                url = f'{base}/{urllib.parse.quote(pages[index].relative_to(site).as_posix())}'
                try:
                    results[index] = check(browser_session, debug_base, url, opts, opts.out / f'{names[index]}.png')
                except Exception as e:
                    results[index] = {'console': [], 'errors': [f'check failed: {e}'], 'started': 0.0,
                                      'backend': backend, 'pending': 0}
                report()

        workers = [threading.Thread(target=worker) for _ in range(jobs)]
        for w in workers:
            w.start()
        for w in workers:
            w.join()
        browser_session.close()
    finally:
        run.stop()
        httpd.shutdown()
    failed = progress['failed']
    print(f'screenshots: {opts.out}')
    print(f'FAIL: {failed} of {len(names)} example(s)' if failed else f'PASS: {len(names)} example(s)')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
