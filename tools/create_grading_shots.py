#!/usr/bin/env python3
"""Take the screenshots helix's Wardens grading names of a page entrant's game (a pixi or
three entrant of tools/run_entrant.py): the shots and the input helix's take_screenshots.py
gives a native program, in a browser.

    tools/create_grading_shots.py <entrant> [--click X,Y] [--enemies 10]

The entrant's directory is served as it is (its index.html, dist/app.js as its last
`node run.cjs` bundled it, and its assets/), and opened at 1280 x 720 in a browser on a
private display, on the GPU, with a fresh profile, its query ?enemies=N&seed=1. Without
--click it saves grading/title.png, 4 seconds after load, to find the Play button on. With
--click it also clicks there and saves play.png, move.png (D held), pause.png (after
Escape), play-later.png and late.png (after Escape again), then opens it again with
?enemies=200 for title-crowd.png and crowd.png. Keys and clicks are sent as the browser's
own input events (DevTools' Input domain), each press held 0.3 s as helix's are. What the
page logs goes to grading/<last shot>.log. Linux only (Xvfb). Standard library only.
"""
import argparse
import base64
import json
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402
import server  # noqa: E402

WIDTH, HEIGHT = 1280, 720
KEYS = {'d': ('d', 'KeyD', 68), 'Escape': ('Escape', 'Escape', 27)}


def key(session, name, kind):
    text, code, vk = KEYS[name]
    params = {'type': kind, 'key': text, 'code': code, 'windowsVirtualKeyCode': vk, 'nativeVirtualKeyCode': vk}
    if kind == 'keyDown' and len(text) == 1:
        params['text'] = text
    session.send('Input.dispatchKeyEvent', params)


def click(session, x, y):
    session.send('Input.dispatchMouseEvent', {'type': 'mouseMoved', 'x': x, 'y': y})
    session.send('Input.dispatchMouseEvent', {'type': 'mousePressed', 'x': x, 'y': y, 'button': 'left', 'clickCount': 1})
    time.sleep(0.3)
    session.send('Input.dispatchMouseEvent', {'type': 'mouseReleased', 'x': x, 'y': y, 'button': 'left', 'clickCount': 1})


def play(run, browser_path, url, out, steps):
    """Opens the page and plays steps: (seconds to wait, action), the action a shot's name,
    ('click', x, y), ('down', key), ('up', key) or ('key', key)."""
    profile = tempfile.mkdtemp(prefix='wgf-grading-')
    debug_base, whole = browser.launch_browser(run, browser_path, 'xvfb', profile=profile, window_size=f'{WIDTH},{HEIGHT}')
    session = browser.open_session(browser.page_target(debug_base)['webSocketDebuggerUrl'])
    lines = []

    def logged(message):
        if message.get('method') == 'Runtime.consoleAPICalled':
            p = message['params']
            lines.append(f"console.{p['type']}: " + ' '.join(str(a.get('value', a.get('description', ''))) for a in p['args']))
        elif message.get('method') == 'Runtime.exceptionThrown':
            lines.append('page error: ' + json.dumps(message['params']['exceptionDetails'].get('exception', {})
                                                     .get('description', message['params']['exceptionDetails']['text'])))
    session.on_event(logged)
    session.send('Runtime.enable')
    session.send('Page.enable')
    session.send('Emulation.setDeviceMetricsOverride', {'width': WIDTH, 'height': HEIGHT, 'deviceScaleFactor': 1, 'mobile': False})
    session.send('Page.navigate', {'url': url})
    try:
        for wait, action in steps:
            time.sleep(wait)
            if isinstance(action, str):
                shot = session.send('Page.captureScreenshot', {'format': 'png'}, timeout=60)
                (out / f'{action}.png').write_bytes(base64.b64decode(shot['data']))
            elif action[0] == 'click':
                click(session, action[1], action[2])
            elif action[0] == 'down':
                key(session, action[1], 'keyDown')
            elif action[0] == 'up':
                key(session, action[1], 'keyUp')
            else:
                key(session, action[1], 'keyDown')
                time.sleep(0.3)
                key(session, action[1], 'keyUp')
    finally:
        (out / f'{steps[-1][1]}.log').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        session.close()
        whole.send('Browser.close')
        whole.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('entrant', type=Path)
    parser.add_argument('--click', help='X,Y of the Play button, in the page\'s pixels (from title.png)')
    parser.add_argument('--enemies', type=int, default=10)
    args = parser.parse_args()
    entrant = args.entrant.resolve()
    if not (entrant / 'index.html').exists() or not (entrant / 'dist' / 'app.js').exists():
        sys.exit(f'create_grading_shots: {entrant} has no index.html and dist/app.js (a page entrant, bundled)')
    browser_path = browser.find_browser()
    out = entrant / 'grading'
    out.mkdir(exist_ok=True)
    base, httpd = server.serve(entrant, assets=None)
    run = browser.RunProcesses('grading')
    run.install_handlers()
    try:
        page = f'{base}/index.html?enemies={args.enemies}&seed=1'
        if not args.click:
            play(run, browser_path, page, out, [(4, 'title')])
            return 0
        x, y = (int(v) for v in args.click.split(','))
        press = ('click', x, y)
        play(run, browser_path, page, out, [
            (4, 'title'), (0, press), (3, 'play'), (0, ('down', 'd')), (0.75, 'move'), (0.75, ('up', 'd')),
            (0, ('key', 'Escape')), (1, 'pause'), (0, ('key', 'Escape')), (8, 'play-later'), (30, 'late')])
        play(run, browser_path, f'{base}/index.html?enemies=200&seed=1', out, [(4, 'title-crowd'), (0, press), (20, 'crowd')])
        return 0
    finally:
        httpd.shutdown()
        run.stop()


if __name__ == '__main__':
    sys.exit(main())
