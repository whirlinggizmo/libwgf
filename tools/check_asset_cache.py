#!/usr/bin/env python3
"""The web asset cache, end to end: wgrender's tools/check_asset_cache.py, ported
(docs/ROADMAP.md, phase 3).

    tools/check_asset_cache.py PAGE_JS [--manifest [--no-subtle]]
                               [--browser PATH] [--verbose]

Serves the page (asset/tests/wgf_asset_cache_page.c, built for the web) from a scratch
directory beside it, with the one file it loads, textures/tiles.png, beside it too, and
visits it again and again in one browser context, so its IndexedDB cache carries over
from visit to visit as a returning visitor's does. Between visits the file changes, and
each visit is judged by every request it made for the file (and its HTTP status), what
libwgf logged, and what the screen shows: the replacement sheet, solid cyan, or, for a
load that fails, the placeholder, libwgf's magenta checker. Without a manifest (the page
asks for manifest.json, and gets a 404), each cached copy is asked about (the server
says no-store, so no copy is ever fresh):

  first          downloaded (200), the real sheet on screen
  unchanged      asked about and kept (304)
  offline        the file blocked: the cached copy used, no error
  changed        the sheet replaced on disk: downloaded again (200), cyan on screen
  again          the new copy kept (304), still cyan
  cleared        wgf_asset_clear_cache() called in the page first: downloaded again (200)
  gone           the file deleted: 404, the cached copy forgotten, the load fails: the
                 placeholder on screen
  gone, offline  the file blocked: nothing cached any more, so it fails again

--manifest writes the manifests (tools/gen_manifest.py) after each change, and only the
root manifest is asked about:

  first          the root, textures/manifest.json and the sheet downloaded
  unchanged      the root asked about (304), nothing else requested
  offline        the root blocked: the cached one used, and the cached sheet
  changed        the root, the directory's manifest and the sheet downloaded; cyan
  again          the root asked about (304), still cyan, nothing else requested
  cleared        wgf_asset_clear_cache() called in the page first: all three downloaded
  stale host     the manifests list a green sheet, the host serves cyan: downloaded, not
                 kept, the load fails: the placeholder on screen
  caught up      the host serves the green sheet: only it is downloaded

--no-subtle hides crypto.subtle from the page, as a page that isn't a secure context
sees it, so a listed download is hashed by libwgf's C, a slice an update, rather than by
the browser.

Exits 77 (ctest's skip) when no browser is found. Standard library only.
"""
import argparse
import base64
import http.server
import os
import shutil
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402

SKIP = 77
TILES = 'textures/tiles.png'
COLOUR_MIN = 2000  # pixels: the sheet covers the window when it draws
CYAN = 'p[i] < 60 && p[i + 1] > 200 && p[i + 2] > 200'     # the replacement sheet
MAGENTA = 'p[i] > 200 && p[i + 1] < 60 && p[i + 2] > 200'  # the placeholder checker
STARTED = 'wgf_gfx:'  # gfx logs its backend as it starts

PAGE = """<!doctype html>
<meta charset="utf-8">
<title>check_asset_cache</title>
<style>body { margin: 0; background: #000; } canvas { display: block; }</style>
<canvas id="canvas" width="256" height="256"></canvas>
<script>
%(no_subtle)s
var Module = {
    canvas: document.getElementById("canvas"),
    print: (text) => console.log(text),
    printErr: (text) => console.error(text),
};
</script>
<script src="%(script)s"></script>
"""


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('page_js', type=Path)
    ap.add_argument('--manifest', action='store_true', help='with manifests (tools/gen_manifest.py)')
    ap.add_argument('--no-subtle', action='store_true',
                    help='the page as an insecure context sees it: no crypto.subtle, so C hashes a listed file')
    ap.add_argument('--browser')
    ap.add_argument('--verbose', action='store_true')
    ap.add_argument('--settle', type=int, default=20000, help='longest a visit runs, ms')
    ap.add_argument('--quiet', type=int, default=1500, help='quiet time that ends a visit, ms')
    opts = ap.parse_args()
    opts.display = 'headless'
    return opts


def solid_png(width, height, rgba):
    """A PNG of one colour, `width` x `height`."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    row = b'\0' + bytes(rgba) * width
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(row * height)) + chunk(b'IEND', b''))


def png_size(data):
    return struct.unpack('>II', data[16:24])


# what a page that isn't a secure context sees (served over plain http from anywhere but
# localhost): no crypto.subtle, so a listed download is hashed in C, a slice an update
NO_SUBTLE = 'Object.defineProperty(Crypto.prototype, "subtle", {get: () => undefined});'


def serve(directory, script, no_subtle=False):
    """Serve `directory` on a free port, in a thread, no-store (every copy stale, so every
    visit asks), with the page at /: its base URL, and the server. Last-Modified and
    If-Modified-Since are http.server's own: a 304 when the file hasn't changed."""

    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                          '.wasm': 'application/wasm', '.js': 'text/javascript'}

        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(directory), **kwargs)

        def do_GET(self):
            if urllib.parse.urlsplit(self.path).path == '/':
                body = (PAGE % {'script': script, 'no_subtle': NO_SUBTLE if no_subtle else ''}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            super().do_GET()

        def end_headers(self):
            self.send_header('Cache-Control', 'no-store')
            # cross-origin isolation, as tools/server.py sends: a threaded build's page needs it
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            super().end_headers()

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', browser.free_port()), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return f'http://127.0.0.1:{server.server_address[1]}', server


def count_pixels(session, png_base64, test):
    """Pixels of a screenshot that pass `test` (a JS condition on p[i], p[i + 1], p[i + 2]),
    decoded in the page on a canvas of its own (never the page's)."""
    expression = f"""(async () => {{
        const image = await createImageBitmap(await (await fetch("data:image/png;base64,{png_base64}")).blob());
        const canvas = new OffscreenCanvas(image.width, image.height);
        const context = canvas.getContext("2d");
        context.drawImage(image, 0, 0);
        const p = context.getImageData(0, 0, image.width, image.height).data;
        let n = 0;
        for (let i = 0; i < p.length; i += 4)
            if ({test}) n++;
        return n;
    }})()"""
    return session.send('Runtime.evaluate', {'expression': expression, 'awaitPromise': True,
                                             'returnByValue': True}, 30)['result']['value']


class Visitor:
    """One tab in one browser context, visited again and again (the context's storage,
    IndexedDB included, stays between visits)."""

    def __init__(self, browser_session, debug_base, base_url, opts):
        self.browser, self.base_url, self.opts = browser_session, base_url, opts
        self.context = browser_session.send('Target.createBrowserContext')['browserContextId']
        self.target = browser_session.send('Target.createTarget', {'url': 'about:blank',
                                                                  'browserContextId': self.context})['targetId']
        self.session = browser.open_session(
            f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{self.target}')
        self.lock = threading.Lock()
        self.visit_state = None
        self.session.on_event(self.on_event)
        for domain in ('Runtime', 'Log', 'Page', 'Network'):
            self.session.send(f'{domain}.enable')

    def on_event(self, msg):
        method, params = msg['method'], msg.get('params', {})
        with self.lock:
            v = self.visit_state
            if v is None:
                return
            now = time.monotonic()
            if method == 'Network.requestWillBeSent':
                v['inflight'].add(params['requestId'])
                v['urls'][params['requestId']] = params['request']['url']
                v['activity'] = now
            elif method == 'Network.responseReceived':
                path = urllib.parse.urlsplit(params['response']['url']).path
                v['status'].setdefault(path, []).append(params['response']['status'])
                v['answered'].add(params['requestId'])
                if params['response']['status'] >= 300:
                    # a 304's or an error's body is never read, and DevTools need not report its
                    # load finished (a 404 on the GPU's display, here): the answer is the end
                    v['inflight'].discard(params['requestId'])
                    v['activity'] = now
            elif method in ('Network.loadingFinished', 'Network.loadingFailed'):
                v['inflight'].discard(params['requestId'])
                v['activity'] = now
                # a 304's body is never read, and DevTools calls that a failed load: only
                # a request that got no answer at all failed
                if method == 'Network.loadingFailed' and params['requestId'] not in v['answered']:
                    url = v['urls'].get(params['requestId'], '')
                    v['status'].setdefault(urllib.parse.urlsplit(url).path, []).append('failed')
            elif method == 'Runtime.consoleAPICalled':
                text = ' '.join(str(a['value']) if 'value' in a else a.get('description', '')
                                for a in params['args']).strip().split('\n')[0]
                v['console'].append(text)
                if STARTED in text:
                    v['started'] = True
                    v['activity'] = now
            elif method == 'Runtime.exceptionThrown':
                v['console'].append('exception: ' + params['exceptionDetails'].get('text', ''))

    def visit(self, blocked=()):
        self.session.send('Network.setBlockedURLs', {'urls': list(blocked)})
        with self.lock:
            self.visit_state = {'inflight': set(), 'urls': {}, 'status': {}, 'answered': set(), 'console': [],
                                'started': False, 'activity': time.monotonic()}
        self.session.send('Page.navigate', {'url': f'{self.base_url}/'})
        deadline = time.monotonic() + self.opts.settle / 1000
        pending = -1
        while time.monotonic() < deadline:
            with self.lock:
                started = self.visit_state['started']
            if not started:
                time.sleep(0.1)
                continue
            value = self.session.send('Runtime.evaluate', {
                'expression': "typeof Module !== 'undefined' && Module['_wgf_core_priv_load_get_pending_count']"
                              " ? Module['_wgf_core_priv_load_get_pending_count']() : -1",
                'returnByValue': True})['result'].get('value')
            with self.lock:
                v = self.visit_state
                if value != pending:
                    pending = value
                    v['activity'] = time.monotonic()
                if pending == 0 and not v['inflight'] and time.monotonic() - v['activity'] >= self.opts.quiet / 1000:
                    break
            time.sleep(0.1)
        shot = self.session.send('Page.captureScreenshot', {'format': 'png'})['data']
        with self.lock:
            v, self.visit_state = self.visit_state, None
        v['unsettled'] = sorted(v['urls'].get(r, r) for r in v['inflight']) if time.monotonic() >= deadline else []
        v['pending'] = pending
        v['cyan'] = count_pixels(self.session, shot, CYAN)
        v['magenta'] = count_pixels(self.session, shot, MAGENTA)
        v['shot'] = shot
        return v

    def close(self):
        self.session.close()
        self.browser.try_send('Target.closeTarget', {'targetId': self.target})
        self.browser.try_send('Target.disposeBrowserContext', {'browserContextId': self.context})


def judge(v, requests, replaced, failed=False, log=None):
    """What's wrong with a visit, as lines. `requests`: path under the page -> the status
    of its one request ('failed': no answer); a request for any other manifest or file
    under textures/ is wrong. `replaced`: the replacement sheet is on screen; `failed`: the load
    failed, so the placeholder is."""
    problems = []
    got = {path[1:]: statuses for path, statuses in v['status'].items()
           if path == '/manifest.json' or path.startswith('/textures/')}
    for path in sorted(set(got) | set(requests)):
        want = [requests[path]] if path in requests else []
        if got.get(path, []) != want:
            problems.append(f'{path}: expected {want or "no request"}, got {got.get(path) or "no request"}')
    if v['pending'] != 0:
        problems.append(f'still loading ({v["pending"]} load(s) pending)')
    if replaced and v['cyan'] < COLOUR_MIN:
        problems.append(f'the replacement sheet is not on screen ({v["cyan"]} cyan pixels)')
    if not replaced and v['cyan'] >= COLOUR_MIN:
        problems.append(f'the replacement sheet is on screen ({v["cyan"]} cyan pixels)')
    if failed and v['magenta'] < COLOUR_MIN:
        problems.append(f'the placeholder is not on screen ({v["magenta"]} magenta pixels)')
    if not failed and v['magenta'] >= COLOUR_MIN:
        problems.append(f'the placeholder is on screen ({v["magenta"]} magenta pixels)')
    errors = [line for line in v['console'] if '[ERROR' in line or '[FATAL' in line or line.startswith('exception')]
    if failed and not any(TILES in line and 'not found' in line for line in v['console']):
        problems.append(f'the load of {TILES} did not fail')
    if errors:
        problems += [f'error: {line}' for line in errors]
    if log and not any(log in line for line in v['console']):
        problems.append(f'libwgf did not log "{log}"')
    return problems


def main():
    opts = parse_args()
    page_js = opts.page_js.resolve()
    if not page_js.exists():
        sys.exit(f'check_asset_cache: no page at {page_js} (build asset/tests/wgf_asset_cache_page.c for the web)')
    try:
        browser_path = browser.find_browser(opts.browser)
    except RuntimeError as e:
        print(f'check_asset_cache: SKIPPING ({e})')
        return SKIP
    work = page_js.parent / (f'cachecheck{"-manifest" if opts.manifest else ""}'
                             f'{"-nosubtle" if opts.no_subtle else ""}')
    shutil.rmtree(work, ignore_errors=True)
    (work / TILES).parent.mkdir(parents=True)
    for built in (page_js, page_js.with_suffix('.wasm')):
        shutil.copy2(built, work / built.name)
    tiles = work / TILES
    original = (browser.ROOT / 'examples' / 'assets' / TILES).read_bytes()
    tiles.write_bytes(original)

    base_url, server = serve(work, page_js.name, opts.no_subtle)
    run = browser.RunProcesses('cachecheck')
    run.install_handlers()
    failed = 0
    try:
        debug_base, browser_session = browser.launch_browser(run, browser_path, opts.display)
        visitor = Visitor(browser_session, debug_base, base_url, opts)
        blocked = [f'{base_url}/textures/*', f'{base_url}/manifest.json']
        clock = {'later': time.time()}
        cyan = solid_png(*png_size(original), (0, 255, 255, 255))

        green = solid_png(*png_size(original), (0, 160, 0, 255))

        def touch(path):
            """A modification time past every earlier one: Last-Modified counts whole
            seconds, and a change within one would answer 304."""
            clock['later'] = max(clock['later'], time.time()) + 2
            os.utime(path, (clock['later'], clock['later']))

        def serve_bytes(data):
            tiles.write_bytes(data)
            touch(tiles)

        def deploy():
            subprocess.run([browser.PYTHON, browser.ROOT / 'tools' / 'gen_manifest.py', work, '--quiet'], check=True)
            for manifest in work.rglob('manifest.json'):
                touch(manifest)

        M, D, T = 'manifest.json', 'textures/manifest.json', TILES
        if opts.manifest:
            def stale_host():
                serve_bytes(green)
                deploy()
                serve_bytes(cyan)  # the manifests list green

            deploy()
            steps = [
                ('first', None, lambda v: judge(v, {M: 200, D: 200, T: 200}, False)),
                ('unchanged', None, lambda v: judge(v, {M: 304}, False)),
                ('offline', 'block', lambda v: judge(v, {M: 'failed'}, False)),
                ('changed', lambda: (serve_bytes(cyan), deploy()), lambda v: judge(v, {M: 200, D: 200, T: 200}, True)),
                ('again', None, lambda v: judge(v, {M: 304}, True)),
                ('cleared', 'clear', lambda v: judge(v, {M: 200, D: 200, T: 200}, True)),
                ('stale host', stale_host, lambda v: judge(v, {M: 200, D: 200, T: 200}, False, failed=True,
                                                          log="isn't what the manifest lists")),
                ('caught up', lambda: serve_bytes(green), lambda v: judge(v, {M: 304, T: 200}, False)),
            ]
        else:
            steps = [
                ('first', None, lambda v: judge(v, {M: 404, T: 200}, False)),
                ('unchanged', None, lambda v: judge(v, {M: 404, T: 304}, False)),
                ('offline', 'block', lambda v: judge(v, {M: 'failed', T: 'failed'}, False)),
                ('changed', lambda: serve_bytes(cyan), lambda v: judge(v, {M: 404, T: 200}, True)),
                ('again', None, lambda v: judge(v, {M: 404, T: 304}, True)),
                ('cleared', 'clear', lambda v: judge(v, {M: 404, T: 200}, True)),
                ('gone', tiles.unlink, lambda v: judge(v, {M: 404, T: 404}, False, failed=True,
                                                      log='gone from the host')),
                ('gone, offline', 'block', lambda v: judge(v, {M: 'failed', T: 'failed'}, False, failed=True)),
            ]
        print(f'check_asset_cache: {page_js.name} ({opts.display})'
              f'{", with manifests" if opts.manifest else ""}{", no crypto.subtle" if opts.no_subtle else ""}',
              flush=True)
        for name, before, check in steps:
            if callable(before):
                before()
            elif before == 'clear':  # on the page the last visit left open
                visitor.session.send('Runtime.evaluate', {'expression': "Module['_wgf_asset_clear_cache']()"})
            v = visitor.visit(blocked if before == 'block' else ())
            problems = check(v)
            failed += 1 if problems else 0
            print(f'  {"FAIL" if problems else "ok  "}  {name}', flush=True)
            for p in problems:
                print(f'          {p}')
            if v['unsettled']:
                print(f'          (ran its {opts.settle} ms; still in flight: {", ".join(v["unsettled"]) or "nothing"})')
            if opts.verbose or problems:
                for line in v['console']:
                    print(f'          | {line}')
            (work / f'{name.replace(", ", "-")}.png').write_bytes(base64.b64decode(v['shot']))
        visitor.close()
        browser_session.close()
        print(f'screenshots: {work}')
        print(f'FAIL: {failed} of {len(steps)} visit(s)' if failed else f'PASS: {len(steps)} visits')
        return 1 if failed else 0
    finally:
        run.stop()
        server.shutdown()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'check_asset_cache: {e}')
