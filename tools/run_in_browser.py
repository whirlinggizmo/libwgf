#!/usr/bin/env python3
"""Run a wasm test program in a real browser: what node can't, such as IndexedDB.

    tools/run_in_browser.py TEST_JS [--visits N] [--browser PATH] [--display headless|xvfb|screen]

Serves TEST_JS's directory, then loads the test in a Chromium-based browser (headless
by default) N times in one browser context, so what one visit stores is there for the
next, as for a returning visitor. Visit k passes k as the program's one argument, so
the program knows which visit it is. Each visit must end with exit code 0; the page's
console is printed as it comes. Exits 77 (ctest's skip) when no browser is found.
Standard library only.
"""
import argparse
import json
import sys
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import browser  # noqa: E402
import server  # noqa: E402

SKIP = 77
EXIT_MARK = 'RUN_IN_BROWSER_EXIT '

# The page a visit loads: the test's own .js, told which visit it is, reporting its
# exit code (or a crash) on the console, where this tool reads it.
PAGE = """<!doctype html>
<meta charset="utf-8">
<title>run_in_browser</title>
<canvas id="canvas" width="64" height="64"></canvas>
<script>
var Module = {
    canvas: document.getElementById("canvas"),
    arguments: [%(visit)s],
    print: (text) => console.log(text),
    printErr: (text) => console.error(text),
    onExit: (code) => console.log("%(mark)s" + code),
    onAbort: (what) => console.log("%(mark)s" + "abort " + what),
};
window.addEventListener("error", (e) => console.log("%(mark)s" + "error " + e.message));
</script>
<script src="%(script)s"></script>
"""


def serve(directory):
    """Serve `directory` on a free port, in a thread, as a host would (tools/server.py),
    with the test's page on top: its base URL, and the server."""

    class Handler(server.handler_class(directory)):
        def do_GET(self):
            parts = urllib.parse.urlsplit(self.path)
            if parts.path == '/__run_in_browser.html':
                query = urllib.parse.parse_qs(parts.query)
                body = (PAGE % {'visit': json.dumps(query['visit'][0]), 'mark': EXIT_MARK,
                                'script': query['script'][0]}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            super().do_GET()

    return server.serve(directory, handler=Handler)


def run_visit(session, url, timeout):
    """Load `url` in the session's page and wait for the test's exit: its code, or a
    description of how it failed to finish."""
    result = []
    done = threading.Event()

    def on_event(msg):
        if msg.get('method') == 'Runtime.consoleAPICalled':
            text = ' '.join(str(arg.get('value', arg.get('description', ''))) for arg in msg['params']['args'])
            if text.startswith(EXIT_MARK):
                result.append(text[len(EXIT_MARK):])
                done.set()
            else:
                print(text, flush=True)
        elif msg.get('method') == 'Runtime.exceptionThrown':
            details = msg['params']['exceptionDetails']
            print(f"exception: {details.get('exception', {}).get('description', details.get('text'))}", flush=True)

    session.on_event(on_event)
    try:
        session.send('Page.navigate', {'url': url})
        if not done.wait(timeout):
            return f'no exit within {timeout} s'
    finally:
        session.listeners.remove(on_event)
    return result[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('test_js', type=Path)
    parser.add_argument('--visits', type=int, default=1)
    parser.add_argument('--browser')
    parser.add_argument('--display', default='headless', choices=('headless', 'xvfb', 'screen'))
    parser.add_argument('--timeout', type=float, default=30)
    args = parser.parse_args()

    try:
        browser_path = browser.find_browser(args.browser)
    except RuntimeError as e:
        print(f'run_in_browser: SKIPPING {args.test_js.name} ({e})')
        return SKIP

    test_js = args.test_js.resolve()
    base, httpd = serve(test_js.parent)
    run = browser.RunProcesses('run-in-browser')
    run.install_handlers()
    try:
        debug_base, browser_session = browser.launch_browser(run, browser_path, args.display)
        browser_session.close()
        page = browser.page_target(debug_base)
        session = browser.open_session(page['webSocketDebuggerUrl'])
        session.send('Runtime.enable')
        session.send('Page.enable')
        for visit in range(1, args.visits + 1):
            print(f'run_in_browser: visit {visit} of {args.visits}', flush=True)
            started = time.monotonic()
            query = urllib.parse.urlencode({'visit': visit, 'script': test_js.name})
            outcome = run_visit(session, f'{base}/__run_in_browser.html?{query}', args.timeout)
            if outcome != '0':
                print(f'run_in_browser: visit {visit} failed: {outcome}')
                return 1
            print(f'run_in_browser: visit {visit} passed in {time.monotonic() - started:.2f} s', flush=True)
        session.close()
        return 0
    finally:
        run.stop()
        httpd.shutdown()


if __name__ == '__main__':
    sys.exit(main())
