#!/usr/bin/env python3
"""The racer streaming its track, in a browser on a slow network (docs/ROADMAP.md,
milestone 2, step 10), on the asset cache check's pattern (tools/check_asset_cache.py).

    tools/check_racer_stream.py [--game DIR] [--display xvfb|headless] [--throttle N]
                                [--browser PATH] [--verbose]

Exports the racer's web build (`wgf export --web`, as it ships), serves it as a host
does (Last-Modified, and a 304 for an unchanged file), and visits it three times in one
browser context, so its asset cache (IndexedDB) carries over as a returning visitor's
does. Each visit flies the game's stream autopilot (autopilot/stream.autopilot): the lap,
whose expectations are the game's -- its race begun within its time with sections still
to come, and each section drawn before the car reaches it, which the game publishes as
probes -- and which logs "streaming" (an autopilot's `log streaming` line) where its race
starts. Every visit is on Chrome DevTools' "Fast 4G" network (9 Mbps down, 1.5 up, 165 ms
of latency, as DevTools sets it):

  first    nothing cached: the autopilot passes; and no frame from its "streaming" on
           (the race, with the track still arriving) takes over 33 ms of main-thread work,
           the frame budget's worst frame (docs/HISTORY.md, "Milestone 2's plan")
  again    the autopilot passes, with no file under assets/ downloaded: each one is the
           cache's, a 304 at most (a manifest, the root one alone)
  offline  every file under assets/ blocked: the race starts from the cache, and the
           autopilot passes

The reference machine's settings are the default, as tools/bench/measure_frames.py's: the
GPU through ANGLE under Xvfb (--display xvfb) and the CPU throttled 4 times (--throttle 4).
--display headless draws on SwiftShader, whose frames are no measure of the budget: there
the frames aren't held to it, and the run says so.

A game folder other than libwgf's games/racer is --game's (the racer's own repository).
Exits 77 (ctest's skip) with no browser, Emscripten, or haxe, or with no
autopilot/stream.autopilot in the game, each said as a skip. Standard library only.
"""
import argparse
import json
import subprocess
import sys
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import binding  # noqa: E402
import browser  # noqa: E402
import server  # noqa: E402
import webhost  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PASS, FAIL = 'wgf_autopilot: PASS', 'wgf_autopilot: FAIL'
MARK = 'wgf_autopilot: streaming'  # the autopilot's `log streaming`
WORST_FRAME_MS = 33.0  # the frame budget's: no frame over it once play has started
# Chrome DevTools' "Fast 4G" preset: 9 Mbps down and 1.5 up, each at 90%, and 60 ms of round
# trip times 2.75 (its packet-level estimate), in bytes a second and milliseconds
FAST_4G = {'offline': False, 'downloadThroughput': 9_000_000 / 8 * 0.9, 'uploadThroughput': 1_500_000 / 8 * 0.9,
           'latency': 60 * 2.75}
# the page wraps console's calls so the autopilot's "streaming" line marks the trace where it
# was logged: console.timeStamp is a trace event, on the frames' clock
MARKER = """(() => {
    for (const name of ['log', 'info', 'warn', 'error', 'debug']) {
        const original = console[name];
        console[name] = function (...args) {
            if (args.some(a => String(a).includes(%s))) console.timeStamp('racer-stream');
            return original.apply(this, args);
        };
    }
})();""" % json.dumps(MARK)
TIMEOUT = 900  # a visit's seconds: the lap is 2,600 frames, and the first visit downloads everything


def say(text):
    print(f'check_racer_stream: {text}', flush=True)


def why_not(game, browser_path):
    """Why it can't run here, or None."""
    if browser_path is None:
        return 'no Chromium-based browser found'
    try:
        webhost.emcc()
    except RuntimeError:
        return 'no Emscripten'
    if binding.haxe() is None:
        return 'no haxe on PATH'
    if not (game / 'autopilot' / 'stream.autopilot').is_file():
        return f'{game.name} has no autopilot/stream.autopilot (the game\'s: its lap, logging "streaming" as it races)'
    return None


def export(game):
    """The game's web export, made fresh in build/racer_stream/: its site."""
    dest = ROOT / 'build' / 'racer_stream'
    done = subprocess.run([sys.executable, str(ROOT / 'wgf'), 'export', '--web', '--out', str(dest)], cwd=game,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    if done.returncode != 0:
        raise RuntimeError('exporting failed:\n' + '\n'.join(done.stdout.strip().splitlines()[-15:]))
    return dest / 'web'


class Visitor:
    """One tab in one browser context, visited again and again (its storage, IndexedDB
    included, stays between visits), each visit traced."""

    def __init__(self, browser_session, debug_base, base_url, autopilot, throttle):
        self.browser, self.base_url = browser_session, base_url
        self.context = browser_session.send('Target.createBrowserContext')['browserContextId']
        self.target = browser_session.send('Target.createTarget', {'url': 'about:blank',
                                                                  'browserContextId': self.context})['targetId']
        self.session = browser.open_session(
            f'ws://{urllib.parse.urlsplit(debug_base).netloc}/devtools/page/{self.target}')
        self.lock = threading.Lock()
        self.state = None
        self.session.on_event(self.on_event)
        for domain in ('Runtime', 'Page', 'Network'):
            self.session.send(f'{domain}.enable')
        self.session.send('Network.emulateNetworkConditions', FAST_4G)
        if throttle > 1:
            self.session.send('Emulation.setCPUThrottlingRate', {'rate': throttle})
        self.session.send('Page.addScriptToEvaluateOnNewDocument',
                          {'source': f'globalThis.wgfAutopilot = {json.dumps(autopilot)};\n{MARKER}'})

    def on_event(self, msg):
        method, params = msg.get('method'), msg.get('params', {})
        with self.lock:
            v = self.state
            if v is None:
                return
            if method == 'Network.responseReceived':
                v['responses'].append((urllib.parse.urlsplit(params['response']['url']).path,
                                       params['response']['status']))
            elif method == 'Runtime.consoleAPICalled':
                text = ' '.join(str(a.get('value', a.get('description', ''))) for a in params['args'])
                v['console'].append(text)
                if PASS in text or FAIL in text:
                    v['ended'].set()
            elif method == 'Runtime.exceptionThrown':
                v['console'].append('[ERROR] exception: ' + params['exceptionDetails'].get('text', ''))
                v['ended'].set()
            elif method == 'Tracing.dataCollected':
                v['events'].extend(e for e in params['value'] if e.get('name') in ('FireAnimationFrame', 'TimeStamp'))
            elif method == 'Tracing.tracingComplete':
                v['traced'].set()

    def visit(self, blocked=()):
        """A visit flown to its autopilot's end: what was logged, answered, and traced."""
        self.session.send('Network.setBlockedURLs', {'urls': list(blocked)})
        with self.lock:
            self.state = {'responses': [], 'console': [], 'events': [], 'ended': threading.Event(),
                          'traced': threading.Event()}
            v = self.state
        self.session.send('Tracing.start', {'traceConfig': {'includedCategories': ['devtools.timeline']},
                                            'transferMode': 'ReportEvents'})
        start = time.monotonic()
        self.session.send('Page.navigate', {'url': f'{self.base_url}/index.html'})
        v['finished'] = v['ended'].wait(TIMEOUT)
        v['seconds'] = time.monotonic() - start
        self.session.send('Tracing.end')
        v['traced'].wait(60)
        self.session.send('Page.navigate', {'url': 'about:blank'})  # the run's frames end here
        with self.lock:
            self.state = None
        return v

    def close(self):
        self.session.close()
        self.browser.try_send('Target.closeTarget', {'targetId': self.target})
        self.browser.try_send('Target.disposeBrowserContext', {'browserContextId': self.context})


def streaming_frames(v):
    """The frames' main-thread milliseconds from the "streaming" mark on; None without one."""
    marks = [e['ts'] for e in v['events'] if e['name'] == 'TimeStamp' and
             e.get('args', {}).get('data', {}).get('message') == 'racer-stream']
    if not marks:
        return None
    return [e.get('dur', 0) / 1000.0 for e in v['events'] if e['name'] == 'FireAnimationFrame' and e['ts'] >= marks[0]]


def judge(name, v, hold_frames, downloads_allowed):
    """The visit's problems, said; its lines for --verbose."""
    problems = []
    failed = [line for line in v['console'] if FAIL in line or '[ERROR]' in line or '[FATAL]' in line]
    if not v['finished']:
        problems.append(f'the autopilot didn\'t end within {TIMEOUT} s')
    elif failed or not any(PASS in line for line in v['console']):
        problems.append('the autopilot failed:\n      ' + '\n      '.join(failed[:8] or ['(no PASS)']))
    frames = streaming_frames(v)
    if frames is None:
        problems.append('no "streaming" logged: the autopilot\'s `log streaming` where the race starts')
    else:
        over = [f for f in frames if f > WORST_FRAME_MS]
        worst = max(frames, default=0.0)
        say(f'{name}: {len(frames)} frames from the race\'s start, the worst {worst:.1f} ms, '
            f'{len(over)} over {WORST_FRAME_MS:.0f}' + ('' if hold_frames else ' (not held: SwiftShader)'))
        if hold_frames and over:
            problems.append(f'{len(over)} frame(s) over {WORST_FRAME_MS:.0f} ms once the race started '
                            f'(the worst {worst:.1f})')
    downloads = sorted({path for path, status in v['responses'] if path.startswith('/assets/') and status == 200})
    say(f'{name}: {len(downloads)} file(s) downloaded under assets/, {v["seconds"]:.1f} s')
    if not downloads_allowed and downloads:
        problems.append('downloaded again: ' + ', '.join(downloads[:8]) + (' ...' if len(downloads) > 8 else ''))
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', type=Path, default=ROOT / 'games' / 'racer', help='the racer\'s folder')
    ap.add_argument('--display', choices=('xvfb', 'headless'), default='xvfb')
    ap.add_argument('--throttle', type=float, default=4.0, help='Chrome\'s CPU throttling (default 4)')
    ap.add_argument('--browser', help='a Chromium-based browser (default: the first found)')
    ap.add_argument('--verbose', action='store_true', help='every visit\'s log')
    args = ap.parse_args()
    game = args.game.resolve()
    try:
        browser_path = browser.find_browser(args.browser)
    except RuntimeError:
        browser_path = None
    reason = why_not(game, browser_path)
    if reason is not None:
        say(f'SKIPPING the stream check ({reason})')
        sys.exit(77)
    site = export(game)
    autopilot = (game / 'autopilot' / 'stream.autopilot').read_text(encoding='utf-8')
    base, httpd = server.serve(site, assets=None, cache=True)
    processes = browser.RunProcesses('racer_stream')
    processes.install_handlers()
    problems = []
    try:
        debug_base, browser_session = browser.launch_browser(processes, browser_path, args.display)
        visitor = Visitor(browser_session, debug_base, base, autopilot, args.throttle)
        hold = args.display == 'xvfb'
        for name, blocked, downloads_allowed in (('first', (), True), ('again', (), False),
                                                 ('offline', (f'{base}/assets/*',), False)):
            v = visitor.visit(blocked)
            found = judge(name, v, hold, downloads_allowed)
            if args.verbose:
                print('\n'.join(f'    {line}' for line in v['console']))
            problems += [f'{name}: {p}' for p in found]
            say(f'{name}: ' + ('ok' if not found else 'FAIL'))
        visitor.close()
    finally:
        processes.stop()
        httpd.shutdown()
    for problem in problems:
        say(f'FAIL {problem}')
    say('PASS' if not problems else f'FAIL, {len(problems)} problem(s)')
    sys.exit(0 if not problems else 1)


if __name__ == '__main__':
    main()
