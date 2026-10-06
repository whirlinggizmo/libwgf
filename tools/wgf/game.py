"""A game made with `wgf new`: its project file (wgf.json), and its builds and runs on each
target, for the wgf tool (tools/wgf/cli.py, devserver.py).

A game's directory:
  wgf.json        its name, title, main class, where its sources, assets, and autopilot
                  are, and its web size budget, gzipped (web_budget_kb)
  src/ assets/ autopilot/
  web/index.html  its page: written once from libwgf's (hosts/web/page.html) when it has
                  none, never overwritten; the game's to change
  build/<target>/ what `wgf build` makes, for web, desktop, and headless: the program,
                  with its assets beside it, as `Asset.setHost("assets")` finds them
  export/         what `wgf export` makes: web/ (a static folder) and desktop/

libwgf is the one this tool is part of: a game names none. Standard library only.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))  # an embedded Python (Windows) doesn't add it
import binding  # noqa: E402
import browser  # noqa: E402
import examples  # noqa: E402
import server  # noqa: E402
import variants  # noqa: E402
import webhost  # noqa: E402

ROOT = TOOLS.parent
PAGE = ROOT / 'hosts' / 'web' / 'page.html'
HOTRELOAD = ROOT / 'deps' / 'hotreload-hx' / 'src'
TARGETS = ('web', 'desktop', 'headless')
AUTOPILOT_PASS = 'wgf_autopilot: PASS'
AUTOPILOT_FAIL = 'wgf_autopilot: FAIL'


class GameError(RuntimeError):
    pass


class Game:
    """A game's project: wgf.json read, its paths made absolute."""

    def __init__(self, root):
        self.root = Path(root).resolve()
        path = self.root / 'wgf.json'
        try:
            data = json.loads(path.read_text(encoding='utf-8'))
        except (OSError, ValueError) as e:
            raise GameError(f'{path}: {e}')
        self.name = data.get('name', '')
        if not self.name or not self.name.replace('-', '').replace('_', '').isalnum():
            raise GameError(f'{path}: a name of letters, digits, - and _ is needed ("{self.name}")')
        self.title = data.get('title', self.name)
        self.main = data.get('main', 'Main')
        self.source = self.root / data.get('source', 'src')
        self.assets = self.root / data.get('assets', 'assets')
        self.autopilot = self.root / data.get('autopilot', 'autopilot')
        self.budget_kb = data.get('web_budget_kb')

    def build_dir(self, target):
        return self.root / 'build' / target


def find(start=None):
    """The game whose wgf.json is in `start` (the working directory) or above it."""
    here = Path(start or os.getcwd()).resolve()
    for directory in (here, *here.parents):
        if (directory / 'wgf.json').exists():
            return Game(directory)
    raise GameError(f'no wgf.json in {here} or above: run this in a game made by `wgf new`')


def variant_for(target, release=False):
    if target == 'web':
        return variants.web(debug=not release)
    if target == 'headless':
        return variants.native('debug-headless')
    return variants.native('release' if release else 'debug')


def place_assets(game, beside):
    """The game's assets at `beside`/assets: a link to them where the system makes one,
    else a copy."""
    dest = beside / 'assets'
    if dest.is_symlink() or dest.is_file():
        dest.unlink()
    elif dest.is_dir():
        shutil.rmtree(dest)
    if not game.assets.is_dir():
        dest.mkdir(parents=True)
        return dest
    try:
        os.symlink(game.assets, dest, target_is_directory=True)
    except OSError:
        shutil.copytree(game.assets, dest)
    return dest


def haxe(game, target_args, defines=(), extra=()):
    """Build the game with Haxe; (ok, its output)."""
    if binding.haxe() is None:
        raise GameError('no haxe on PATH: install Haxe 4.3.7 (BUILDING.md)')
    command = [binding.haxe(), '-cp', binding.SOURCE.parent, '-cp', game.source, '--main', game.main,
               *target_args, *extra]
    for define in defines:
        command += ['-D', define]
    done = subprocess.run([str(c) for c in command], cwd=game.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace')
    return done.returncode == 0, done.stdout


def page(game):
    """The game's page, web/index.html: written once from libwgf's when it has none."""
    path = game.root / 'web' / 'index.html'
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(PAGE.read_text(encoding='utf-8').replace('@WGF_PROGRAM@', game.name), encoding='utf-8')
    return path


def hot_args():
    """A hot build's additions (wgf serve): hotreload-hx's macro and its define."""
    return ['-cp', HOTRELOAD, '--macro', 'hotreload.Builder.init()']


def build_web(game, release=False, hot=False, out=None, exports=None):
    """The game for the web in `out` (build/web): its page, the host (the full one, or a
    trimmed one from `exports`), the program, and its assets beside them. The directory."""
    out = Path(out) if out else game.build_dir('web')
    out.mkdir(parents=True, exist_ok=True)
    try:
        webhost.build(variant_for('web', release), exports, out, constants=False)  # Haxe has its own enums
    except RuntimeError as e:
        raise GameError(str(e))
    shutil.copyfile(page(game), out / 'index.html')
    defines = ['js-es=6'] + (['analyzer-optimize'] if release else []) + (['hotreload'] if hot else [])
    extra = (['-dce', 'full'] if release else []) + (hot_args() if hot else [])
    ok, output = haxe(game, ['--js', out / f'{game.name}.js'], defines, extra)
    if not ok:
        raise GameError(f'the build failed:\n{output.strip()}')
    place_assets(game, out)
    return out


def build_native(game, target, release=False, out=None):
    """The game built by hxcpp against libwgf's staged variant for `target` (desktop or
    headless), its executable in `out` (build/<target>) with its assets beside it. The
    executable's path."""
    variant = variant_for(target, release)
    if not binding.has_hxcpp():
        raise GameError('no hxcpp: haxelib install hxcpp 4.3.2 (BUILDING.md)')
    try:
        examples.prepare(variant)
    except RuntimeError as e:
        raise GameError(str(e))
    out = Path(out) if out else game.build_dir(target)
    work = game.build_dir(target) / 'cpp'
    defines = ['HXCPP_M64', f'wgf_out={variants.out(variant).as_posix()}',
               f'wgf_binding={binding.BINDING.as_posix()}'] + (['wgf_headless'] if target == 'headless' else [])
    ok, output = haxe(game, ['--cpp', work], defines, [] if release else ['-debug'])
    if not ok:
        raise GameError(f'the build failed:\n{output.strip()}')
    suffix = '.exe' if os.name == 'nt' else ''
    built = work / (f'{game.main}{"-debug" if not release else ""}{suffix}')
    if not built.exists():
        built = work / f'{game.main}{suffix}'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / f'{game.name}{suffix}'
    shutil.copy2(built, exe)
    place_assets(game, out)
    return exe


def frames_autopilot(frames):
    """An autopilot that runs `frames` frames, then ends."""
    return f'wgf-autopilot 1\nat {max(frames - 1, 0)} end\n'


def run_native(exe, autopilot=None, timeout=600, echo=True):
    """Run a native build, with `autopilot` (its text) flying it; (exit code, its
    output). Its output is echoed as it comes."""
    env = dict(os.environ)
    scratch = None
    if autopilot is not None:  # in a scratch folder, never beside the program (an export ships that folder)
        scratch = tempfile.TemporaryDirectory(prefix='wgf-run-')
        path = Path(scratch.name) / 'run.autopilot'
        path.write_text(autopilot, encoding='utf-8')
        env['LIBWGF_AUTOPILOT'] = str(path)
    lines = []
    with subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, errors='replace') as process:
        timer = threading.Timer(timeout, process.kill)
        timer.start()
        for line in process.stdout:
            lines.append(line.rstrip('\n'))
            if echo:
                print(line, end='', flush=True)
        timer.cancel()
    if scratch is not None:
        scratch.cleanup()
    return process.returncode, '\n'.join(lines)


def run_page(site, page_path, autopilot=None, until=(AUTOPILOT_PASS, AUTOPILOT_FAIL), timeout=120, on_line=None,
             browser_path=None, echo=True, screenshot=None):
    """`site` served as it is (a game's assets are beside its page), and its page at
    `page_path` run as run_page_url runs one. The lines logged."""
    base, httpd = server.serve(site, assets=None)
    try:
        return run_page_url(f'{base}/{page_path}', autopilot, until, timeout, on_line, browser_path, echo, screenshot)
    finally:
        httpd.shutdown()


def run_page_url(url, autopilot=None, until=(AUTOPILOT_PASS, AUTOPILOT_FAIL), timeout=120, on_line=None,
                 browser_path=None, echo=True, screenshot=None):
    """Load `url` in a headless browser, handing the page `autopilot` to fly it,
    until a console line contains one of `until`; `on_line(line)` sees each line first.
    With `screenshot` (a path), the page is saved there as a PNG when the autopilot logs its
    SCREENSHOT, and the run ends. The lines logged."""
    if screenshot is not None:
        until = tuple(until) + ('wgf_autopilot: SCREENSHOT',)
    browser_path = browser.find_browser(browser_path)
    processes = browser.RunProcesses('wgf')
    processes.install_handlers()
    lines, done = [], threading.Event()

    def on_event(message):
        if message.get('method') == 'Runtime.consoleAPICalled':
            text = ' '.join(str(a.get('value', a.get('description', ''))) for a in message['params']['args'])
        elif message.get('method') == 'Runtime.exceptionThrown':
            details = message['params']['exceptionDetails']
            text = '[ERROR] exception: ' + str(details.get('exception', {}).get('description', details.get('text')))
        else:
            return
        lines.append(text)
        if echo:
            print(text, flush=True)
        if on_line is not None:
            on_line(text)
        if any(mark in text for mark in until) or text.startswith('[ERROR] exception'):
            done.set()

    try:
        debug_base, session = browser.launch_browser(processes, browser_path, 'headless')
        session.close()
        target = json.loads(browser.wait_for(f'{debug_base}/json/list', 'page'))
        page_target = next(t for t in target if t.get('type') == 'page')
        session = browser.open_session(page_target['webSocketDebuggerUrl'])
        session.on_event(on_event)
        session.send('Runtime.enable')
        session.send('Page.enable')
        if autopilot is not None:
            session.send('Page.addScriptToEvaluateOnNewDocument',
                         {'source': f'globalThis.wgfAutopilot = {json.dumps(autopilot)};'})
        session.send('Page.navigate', {'url': url})
        if not done.wait(timeout):
            lines.append(f'[ERROR] wgf: nothing ended the run within {timeout} s')
        elif screenshot is not None and any('wgf_autopilot: SCREENSHOT' in line for line in lines):
            import base64
            shot = session.send('Page.captureScreenshot', {'format': 'png'})
            Path(screenshot).write_bytes(base64.b64decode(shot['data']))
        session.close()
    finally:
        processes.stop()
    return lines


def judged(output):
    """An autopilot run's verdict from what it logged: True for PASS with no error, False
    for FAIL or an error, None for no verdict."""
    lines = output if isinstance(output, list) else output.splitlines()
    errors = [line for line in lines if '[ERROR]' in line or '[FATAL]' in line]
    if any(AUTOPILOT_FAIL in line for line in lines) or errors:
        return False
    return True if any(AUTOPILOT_PASS in line for line in lines) else None


def dumped(output, part='ecs'):
    """What an autopilot's dump logged for `part`, as its text."""
    lines = output if isinstance(output, list) else output.splitlines()
    mark = f'wgf_autopilot: DUMP {part}| '
    return '\n'.join(line.split(mark, 1)[1] for line in lines if mark in line)


def gzip_size(path):
    import gzip
    return len(gzip.compress(Path(path).read_bytes(), 9))


def wait_for(predicate, timeout, step=0.1):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return True
        time.sleep(step)
    return False


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the wgf tool imports: there is nothing to run.').parse_args()
    raise SystemExit('game.py: a module the wgf tool imports: there is nothing to run')
