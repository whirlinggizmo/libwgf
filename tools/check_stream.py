#!/usr/bin/env python3
"""A streamed sound on the web: played from a copy here when there is one, else streamed
by its <audio> element as it arrives, one download (docs/HISTORY.md, "Phase 3, asset:
closed").

    tools/check_stream.py PAGE_JS [--browser PATH] [--verbose]

Serves the page (libwgf/audio/tests/wgf_audio_stream_page.c, built for the web) from a scratch
directory beside it, with the file it plays, music/long.ogg (libwgf/audio/tests/data), sent
slowly, 16 KB every 0.3 s (about 7.5 s), as a slow connection would, no-store (so a
cached copy is always asked about), with Last-Modified and a 304 for an unchanged one. A
mod's redirect (mods/loud/music/) is asked first and hasn't it (404). Visits the page
again and again in one browser context, as a returning visitor, so its asset cache
(IndexedDB) carries over, and judges each by the page's log (its sound's status, the
voice's position) against the server's sending, the requests DevTools saw (by the
element, Media, or by asset, Fetch), the element's source, and the asset cache's keys:

  first      not local: the element streams it, playing while it is sent, the mod's
             404 fallen through; one request for the file, and nothing cached
  again      the same: what the element fetched was never cached
  ensured    the page ensures the file first (asset fetches it, cached), then makes the
             sound: it plays from a blob, with no request by the element (the mod's
             file is asked about by each, as every load asks about a path rule's)
  unchanged  the cached copy asked about: a 304, it plays from a blob
  changed    the file touched: a 200, dropped at its headers, and the element streams it,
             playing while it is sent

Exits 77 (ctest's skip) when no browser is found. Standard library only.
"""
import argparse
import email.utils
import http.server
import os
import shutil
import sys
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402

SKIP = 77
MUSIC = 'music/long.ogg'
MOD = 'mods/loud/music/long.ogg'
# bytes sent at a time, and the wait between: ~7.5 s for its 391 KB, in bursts, as a jittery
# connection delivers; the pace that showed the voice's seek before play() hanging the element on a
# host without Range (HISTORY.md, "Streaming, step by step"), kept so that would show again
PIECE, PAUSE = 16 * 1024, 0.3
STARTED = 'stream page: started'

PAGE = """<!doctype html>
<meta charset="utf-8">
<title>check_stream</title>
<style>body { margin: 0; background: #000; } canvas { display: block; }</style>
<canvas id="canvas" width="64" height="64"></canvas>
<script>
var Module = {
    canvas: document.getElementById("canvas"),
    print: (text) => console.log(text),
    printErr: (text) => console.error(text),
};
</script>
<script src="%(script)s"></script>
"""

# the element's source, and whether the asset cache holds the file: asked of the page
PROBE = """(async () => {
    const audio = Module.wgf_audio;
    let src = '';
    if (audio) audio.sounds.forEach((e) => { if (e.element) src = e.element.src || src; });
    let cached = false;
    if (Module.wgf_core_fs_db) {
        cached = await new Promise((done) => {
            const keys = Module.wgf_core_fs_db.transaction('files').objectStore('files').getAllKeys();
            keys.onsuccess = () => done(keys.result.some((k) => String(k).endsWith('%s')));
            keys.onerror = () => done(false);
        });
    }
    return {src: src, cached: cached};
})()""" % MUSIC


def parse_args():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('page_js', type=Path)
    ap.add_argument('--browser')
    ap.add_argument('--verbose', action='store_true')
    opts = ap.parse_args()
    opts.display = 'headless'
    return opts


def serve(directory, script):
    """Serve `directory` on a free port, in a thread, no-store, the page at /, and
    music/long.ogg slowly, with a 304 when unchanged; `state()` counts its slow sends
    and says whether one is open."""
    sending = {'count': 0, 'open': 0, 'ended': 0.0}
    lock = threading.Lock()

    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                          '.wasm': 'application/wasm', '.js': 'text/javascript', '.ogg': 'audio/ogg'}

        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(directory), **kwargs)

        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            if path == '/':
                body = (PAGE % {'script': script}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'text/html; charset=utf-8')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if path == '/' + MUSIC:
                self.send_slowly(directory / MUSIC)
                return
            super().do_GET()

        def send_slowly(self, file):
            modified = int(file.stat().st_mtime)
            since = self.headers.get('If-Modified-Since')
            if since:
                try:
                    if int(email.utils.parsedate_to_datetime(since).timestamp()) >= modified:
                        self.send_response(304)
                        self.end_headers()
                        return
                except (TypeError, ValueError):
                    pass
            data = file.read_bytes()
            with lock:
                sending['count'] += 1
                sending['open'] += 1
            try:
                self.send_response(200)
                self.send_header('Content-Type', 'audio/ogg')
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Last-Modified', email.utils.formatdate(modified, usegmt=True))
                self.end_headers()
                for at in range(0, len(data), PIECE):
                    self.wfile.write(data[at:at + PIECE])
                    self.wfile.flush()
                    time.sleep(PAUSE)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass  # the browser let go of it (a 200 dropped at its headers)
            finally:
                with lock:
                    sending['open'] -= 1
                    sending['ended'] = time.monotonic()

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

    def state():
        with lock:
            return dict(sending)

    return f'http://127.0.0.1:{server.server_address[1]}', server, state


class Visitor:
    """One tab in one browser context, visited again and again (its storage stays)."""

    def __init__(self, browser_session, debug_base, base_url, sending):
        self.browser, self.base_url, self.sending = browser_session, base_url, sending
        self.context = browser_session.send('Target.createBrowserContext')['browserContextId']
        self.target = browser_session.send('Target.createTarget', {'url': 'about:blank',
                                                                  'browserContextId': self.context})['targetId']
        self.session = browser.open_session(
            f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{self.target}')
        self.lock = threading.Lock()
        self.v = None
        self.session.on_event(self.on_event)
        for domain in ('Runtime', 'Network'):
            self.session.send(f'{domain}.enable')

    def on_event(self, msg):
        method, params = msg['method'], msg.get('params', {})
        with self.lock:
            v = self.v
            if v is None:
                return
            if method == 'Network.requestWillBeSent':
                v['requests'][params['requestId']] = {'path': urllib.parse.urlsplit(params['request']['url']).path,
                                                      'type': params.get('type', ''), 'status': None}
            elif method == 'Network.responseReceived' and params['requestId'] in v['requests']:
                v['requests'][params['requestId']]['status'] = params['response']['status']
            elif method == 'Runtime.consoleAPICalled':
                text = ' '.join(str(a['value']) if 'value' in a else a.get('description', '')
                                for a in params['args']).strip().split('\n')[0]
                # and whether the file was still being sent then
                v['console'].append((text, self.sending()['open'] > 0))
            elif method == 'Runtime.exceptionThrown':
                v['console'].append(('exception: ' + params['exceptionDetails'].get('text', ''), False))

    def visit(self, hash_='', longest=40.0):
        """Load the page; watch until its voice has played 1.5 s and no slow send is open
        (or `longest`); then ask it its element's source and its cache."""
        with self.lock:
            self.v = {'requests': {}, 'console': []}
        self.session.send('Page.navigate', {'url': f'{self.base_url}/{hash_}'})
        deadline = time.monotonic() + longest
        while time.monotonic() < deadline:
            with self.lock:
                states = [page_state(text) for text, _ in own_lines(self.v['console'])]
            played = any(s and s[3] >= 1.5 for s in states)
            if played and self.sending()['open'] == 0:
                break
            time.sleep(0.1)
        probe = self.session.send('Runtime.evaluate', {'expression': PROBE, 'awaitPromise': True,
                                                       'returnByValue': True}, 30)['result'].get('value') or {}
        with self.lock:
            v, self.v = self.v, None
        v['src'], v['cached'] = probe.get('src', ''), probe.get('cached', False)
        return v

    def close(self):
        self.session.close()
        self.browser.try_send('Target.closeTarget', {'targetId': self.target})
        self.browser.try_send('Target.disposeBrowserContext', {'browserContextId': self.context})


def own_lines(console):
    """The lines this visit's page logged: from its "started" line on. Before it, the last
    visit's page may still be logging while the navigation takes effect."""
    for i, (text, _) in enumerate(console):
        if STARTED in text:
            return console[i:]
    return []


def page_state(text):
    """The page's line as (status, duration, state, position), or None."""
    if 'stream page: status' not in text:
        return None
    words = text.split()
    at = words.index('status')
    try:
        return int(words[at + 1]), float(words[at + 3]), int(words[at + 5]), float(words[at + 7])
    except (ValueError, IndexError):
        return None


def judge(v, requests, streamed, cached):
    """What's wrong with a visit, as lines. `requests`: (path, type, status) of every
    request for the file and the mod's, in any order; `streamed`: the element's source is
    the network's, and its voice played while the file was sent (else a blob); `cached`:
    the asset cache holds the file after."""
    problems = []
    lines = own_lines(v['console'])
    if not lines:
        return ['the page never started']
    states = [(page_state(text), sending) for text, sending in lines if page_state(text)]
    if not any(s[0] == 2 and s[2] == 1 and s[3] >= 1.0 for s, _ in states):
        problems.append('its voice never played a second')
    if streamed and not any(sending and s[0] == 2 and s[2] == 1 and s[3] > 0.5 for s, sending in states):
        problems.append('not READY, its voice advancing, while its file was still being sent')
    if any(s[0] == 3 for s, _ in states):
        problems.append('the sound FAILED')
    got = sorted((r['path'][1:], r['type'], r['status']) for r in v['requests'].values()
                 if r['path'] in ('/' + MUSIC, '/' + MOD))
    if got != sorted(requests):
        problems.append(f'requests: expected {sorted(requests)}, got {got}')
    if streamed != (not v['src'].startswith('blob:')):
        problems.append(f'its element plays {v["src"] or "nothing"}, expected '
                        f'{"the file streamed" if streamed else "a blob of the copy here"}')
    if cached != v['cached']:
        problems.append('the asset cache holds the file' if v['cached'] else 'the asset cache hasn\'t the file')
    errors = [text for text, _ in lines
              if '[ERROR' in text or '[FATAL' in text or text.startswith('exception') or 'FAILED' in text]
    problems += [f'error: {text}' for text in errors]
    return problems


def main():
    opts = parse_args()
    page_js = opts.page_js.resolve()
    if not page_js.exists():
        sys.exit(f'check_stream: no page at {page_js} (build libwgf/audio/tests/wgf_audio_stream_page.c for the web)')
    try:
        browser_path = browser.find_browser(opts.browser)
    except RuntimeError as e:
        print(f'check_stream: SKIPPING ({e})')
        return SKIP
    work = page_js.parent / 'streamcheck'
    shutil.rmtree(work, ignore_errors=True)
    (work / MUSIC).parent.mkdir(parents=True)
    for built in (page_js, page_js.with_suffix('.wasm')):
        shutil.copy2(built, work / built.name)
    music = work / MUSIC
    shutil.copy2(browser.ROOT / 'libwgf' / 'audio' / 'tests' / 'data' / 'long.ogg', music)
    os.utime(music, (time.time() - 60, time.time() - 60))

    def touch():
        later = time.time() + 2  # past the last second Last-Modified said
        os.utime(music, (later, later))

    stream = [(MOD, 'Media', 404), (MUSIC, 'Media', 200)]
    steps = [
        ('first', '', None, lambda v: judge(v, stream, True, False)),
        ('again', '', None, lambda v: judge(v, stream, True, False)),
        # the ensure walks the redirects, and the create again, as every load on the web
        # asks about a path rule's file: the mod's 404 twice
        ('ensured', '?ensure', None, lambda v: judge(v, [(MOD, 'Fetch', 404), (MOD, 'Fetch', 404),
                                                         (MUSIC, 'Fetch', 200)], False, True)),
        ('unchanged', '', None, lambda v: judge(v, [(MOD, 'Fetch', 404), (MUSIC, 'Fetch', 304)], False, True)),
        ('changed', '', touch, lambda v: judge(v, [(MOD, 'Fetch', 404), (MUSIC, 'Fetch', 200), (MUSIC, 'Media', 200)],
                                               True, True)),
    ]

    base_url, server, sending = serve(work, page_js.name)
    run = browser.RunProcesses('streamcheck')
    run.install_handlers()
    failed = 0
    try:
        debug_base, browser_session = browser.launch_browser(run, browser_path, opts.display)
        visitor = Visitor(browser_session, debug_base, base_url, sending)
        print(f'check_stream: {page_js.name} ({opts.display})', flush=True)
        for name, hash_, before, check in steps:
            if before:
                before()
            v = visitor.visit(hash_)
            problems = check(v)
            failed += 1 if problems else 0
            print(f'  {"FAIL" if problems else "ok  "}  {name}', flush=True)
            for p in problems:
                print(f'          {p}')
            if opts.verbose or problems:
                for text, while_sent in v['console']:
                    print(f'          {"|" if while_sent else ":"} {text}')
        visitor.close()
        browser_session.close()
        print(f'FAIL: {failed} of {len(steps)} visit(s)' if failed else f'PASS: {len(steps)} visits')
        return 1 if failed else 0
    finally:
        run.stop()
        server.shutdown()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except RuntimeError as e:
        sys.exit(f'check_stream: {e}')
