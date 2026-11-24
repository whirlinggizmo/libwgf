"""`wgf serve`: the game in a browser, rebuilt as its Haxe, a shader, or an asset is saved,
for the wgf tool (tools/wgf/cli.py). What the page then does is the mode's:

  reload   (the default) the page loads again: the game starts over with the new build
  none     (--no-reload) nothing: the new build is there for when the page is loaded again
  hot      (--hot) the new code is taken on by the running page, its state kept, and a
           texture or a glTF saved is loaded again in place; what can't be (a shader, a
           sound, a scene) loads the page again, as in reload

The first build is on the full web host, which a rebuild never relinks. Each rebuild goes
through a compilation server (haxe --wait). A failed build is reported here, and the page
keeps what it has.

A page that loads again is told to by a script this adds to the served page (not the
game's own web/index.html), which asks /__wgf_reload?since=<n> (a long poll).

Hot is hotreload-hx's (https://github.com/whirlinggizmo/hotreload-hx), a haxelib the
machine has installed (`-lib hotreload-hx`, -D hotreload): none of it is libwgf's, and
without it --hot says how to get it. The page's program asks /__hotreload?since=<n> for a
build newer than its own; each rebuild is stamped with its number on its first line, and the
page loads it beside itself and takes on its classes between two frames (its JsSwap):
every static keeps its value, every object its fields, the C side -- entities, nodes,
textures, the window -- is untouched. Its reloader asks /__hotreload_assets?since=<n> too,
answered with the saved files' URLs (/assets/<path>); the Haxe runtime's hot build, told by
it (Reloader.assetsChanged), loads each again in place (wgf_asset_reload), its handle kept;
a file that doesn't load keeps what it had, with one error logged in the page. A file that
can't be loaded again in place (wgf_asset_can_reload says: a scene, a sound, a file
nothing was made from) loads the page again instead.

The game's shaders/*.glsl are watched as well: one saved is packed again
(tools/pack_shader.py, which needs sokol-shdc: tools/setup_shdc.py) into
assets/shaders/<name>.wgfshader, which is then an asset saved; one that doesn't compile
says why here, and the packed file stays as it was. Standard library only.
"""
import json
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time
import urllib.parse
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import game as games  # noqa: E402
import server  # noqa: E402

POLL_SECONDS = 20  # a long poll's longest wait: then the page asks again
MODES = ('reload', 'none', 'hot')
RELOAD_SCRIPT = ('<script>(function ask(n){fetch("/__wgf_reload?since="+n,{cache:"no-store"}).then(function(r){'
                 'return r.json()}).then(function(a){if(n>=0&&a.version>n)location.reload();else ask(a.version)},'
                 'function(){setTimeout(function(){ask(n)},1000)})})(-1)</script>')
CHECK_SECONDS = 0.1


class Builds:
    """The builds so far: the latest's number, waited on by the page's long polls."""

    def __init__(self):
        self.version = 0
        self.changed = threading.Condition()

    def bump(self):
        with self.changed:
            self.version += 1
            self.changed.notify_all()
            return self.version

    def wait_newer(self, since, timeout):
        with self.changed:
            self.changed.wait_for(lambda: self.version > since, timeout)
            return self.version


class Saved:
    """The asset files saved so far, each with the number of the save it came in: waited on
    by the page's asset polls."""

    def __init__(self):
        self.version = 0
        self.files = []  # (version, path under the assets)
        self.changed = threading.Condition()

    def bump(self, files):
        with self.changed:
            self.version += 1
            self.files += [(self.version, f) for f in files]
            self.changed.notify_all()

    def wait_newer(self, since, timeout):
        """The latest number, and the files saved after `since`, each once; a page asking
        with no number yet (since < 0) is told the latest at once, and nothing to load."""
        with self.changed:
            if since >= 0:
                self.changed.wait_for(lambda: self.version > since, timeout)
            files = [] if since < 0 else sorted({f for v, f in self.files if v > since})
            return self.version, files


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def sources(directory, pattern='*.hx'):
    """Each .hx (or each file `pattern` matches) under `directory`, and a stamp of it: its
    time and size."""
    out = {}
    if not Path(directory).is_dir():
        return out
    for path in Path(directory).rglob(pattern):
        if not path.is_file():
            continue
        try:
            stat = path.stat()
        except OSError:
            continue
        out[str(path)] = (stat.st_mtime_ns, stat.st_size)
    return out


class Server:
    def __init__(self, game, port, mode='reload', echo=lambda text: print(text, flush=True)):
        self.game = game
        self.port = port
        self.mode = mode
        self.echo = echo
        self.out = game.build_dir('web')
        self.builds = Builds()
        self.reloads = Builds()  # the times the page was told to load again
        self.saved = Saved()
        self.program = self.out / f'{game.name}.js'
        self.compile_port = None
        self.compile_server = None
        self.args = None

    def start_compilation_server(self):
        """A haxe --wait for the rebuilds, so each is quick: None when it won't start."""
        port = free_port()
        try:
            process = subprocess.Popen([games.binding.haxe(), '--wait', str(port)], stdout=subprocess.DEVNULL,
                                       stderr=subprocess.DEVNULL)
        except OSError:
            return None
        if not games.wait_for(lambda: self._accepts(port), 10):
            process.kill()
            return None
        self.compile_server = process
        return port

    @staticmethod
    def _accepts(port):
        try:
            with socket.create_connection(('127.0.0.1', port), timeout=0.2):
                return True
        except OSError:
            return False

    def first_build(self):
        """The page, the full host, the program (a hot one, in that mode), and the assets;
        the served page given the script that loads it again, unless it never is."""
        hot = self.mode == 'hot'
        games.build_web(self.game, hot=hot, out=self.out)
        self.args = [str(a) for a in ['-cp', games.binding.SOURCE.parent, '-cp', self.game.source, *games.binding.MACROS, '--main',
                                      self.game.main, '--js', self.program, '-D', 'js-es=6',
                                      *(['-D', 'hotreload', *games.hot_args()] if hot else [])]]
        for define in self.game.defines:
            self.args += ['-D', define]
        if self.mode != 'none':
            page = self.out / 'index.html'
            text = page.read_text(encoding='utf-8')
            at = text.rfind('</body>')
            page.write_text(text[:at] + RELOAD_SCRIPT + '\n' + text[at:] if at >= 0 else text + RELOAD_SCRIPT,
                            encoding='utf-8')

    def rebuild(self, changed):
        names = ', '.join(Path(p).name for p in changed)
        self.echo(f'wgf serve: building ({names} changed)')
        started = time.monotonic()
        hot = self.mode == 'hot'
        command = [games.binding.haxe()]
        if self.compile_port is not None:
            command += ['--connect', str(self.compile_port)]
        command += self.args
        if hot:
            listing = self.out / '.hotreload-changed'
            listing.write_text('\n'.join(changed), encoding='utf-8')
            command += ['-D', 'hotreload_reload', '-D', f'hotreload_invalidate={listing}']
        done = subprocess.run(command, cwd=self.game.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors='replace')
        if done.returncode != 0:
            self.echo(done.stdout.strip())
            self.echo('wgf serve: the build failed; the page keeps what it has')
            return False
        took = round((time.monotonic() - started) * 1000)
        if hot:
            number = self.builds.version + 1
            text = self.program.read_text(encoding='utf-8')
            self.program.write_text(f'globalThis.__hotreload_built = {number}; ' + text, encoding='utf-8')
            self.builds.bump()
            self.echo(f'wgf serve: built in {took} ms; the page takes it on next')
        elif self.mode == 'reload':
            self.reloads.bump()
            self.echo(f'wgf serve: built in {took} ms; the page loads again')
        else:
            self.echo(f'wgf serve: built in {took} ms; load the page again for it')
        return True

    def shaders_saved(self, changed):
        """Shader sources saved: each packed again into the assets, where the assets' watch
        finds it; one that doesn't compile said, its packed file left; one gone, left too."""
        for path in changed:
            source = Path(path)
            if not source.is_file():
                continue
            packed = self.game.assets / 'shaders' / (source.stem + '.wgfshader')
            packed.parent.mkdir(parents=True, exist_ok=True)
            done = subprocess.run([sys.executable, str(games.TOOLS / 'pack_shader.py'), str(source), '-o', str(packed)],
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
            if done.returncode != 0:
                self.echo(done.stdout.strip())
                self.echo(f'wgf serve: {source.name} didn\'t pack; the page keeps the shader it has')
            else:
                self.echo(f'wgf serve: {source.name} packed')

    def assets_saved(self, changed):
        """Asset files saved (or gone): copied first where the build has a copy of the
        assets rather than a link to them; then, by the mode, the page loads again, or
        (hot) is told them, and loads each again in place or itself again (its choice)."""
        served = self.out / 'assets'
        names = []
        for path in changed:
            name = Path(path).relative_to(self.game.assets).as_posix()
            names.append(name)
            if not served.is_symlink() and Path(path).is_file():
                (served / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, served / name)
        said = ', '.join(names)
        if self.mode == 'none':
            self.echo(f'wgf serve: {said} saved; load the page again for it')
        elif self.mode == 'hot':  # the page knows what was made from each: it chooses
            self.echo(f'wgf serve: {said} saved; the page loads it again in place, or loads again')
            self.saved.bump(names)
        else:
            self.echo(f'wgf serve: {said} saved; the page loads again')
            self.reloads.bump()

    def handler(self):
        builds, saved, reloads, program, name = self.builds, self.saved, self.reloads, self.program, self.game.name

        class Handler(server.handler_class(self.out, assets=None)):
            def end_headers(self):
                self.send_header('Cache-Control', 'no-store')  # a reload's files are new each build
                super().end_headers()

            def do_GET(self):
                parts = urllib.parse.urlsplit(self.path)
                if parts.path not in ('/__hotreload', '/__hotreload_assets', '/__wgf_reload'):
                    return super().do_GET()
                try:
                    since = int(urllib.parse.parse_qs(parts.query).get('since', ['0'])[0] or 0)
                except ValueError:
                    since = 0
                if parts.path == '/__wgf_reload':  # a page with no number yet is told the latest at once
                    version = reloads.version if since < 0 else reloads.wait_newer(since, POLL_SECONDS)
                    body = json.dumps({'version': version}).encode()
                elif parts.path == '/__hotreload_assets':
                    version, files = saved.wait_newer(since, POLL_SECONDS)
                    body = json.dumps({'version': version, 'urls': [f'/assets/{f}' for f in files]}).encode()
                else:
                    version = builds.wait_newer(since, POLL_SECONDS)
                    body = json.dumps({'version': version, 'url': f'/{name}.js', 'file': str(program)}).encode()
                self.send_response(200)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        return Handler

    def run(self, stop=None):
        """Build, serve, and rebuild on each save until `stop` is set (or forever). Ended by
        a terminate as by Ctrl-C, so the compilation server it started stops with it: a
        check that ends it (tools/check_cli.py) left one running each time."""
        if threading.current_thread() is threading.main_thread():
            def ended(_signal, _frame):
                raise KeyboardInterrupt
            signal.signal(signal.SIGTERM, ended)
        self.first_build()
        self.compile_port = self.start_compilation_server()
        base, httpd = server.serve(self.out, port=self.port, handler=self.handler())
        does = {'reload': 'loads the page again', 'none': 'rebuilds it: load the page again for it',
                'hot': 'reloads it in the running page, its state kept'}[self.mode]
        self.echo(f'wgf serve: {self.game.name} at {base}/ (saving its Haxe, a shader, or an asset {does}; '
                  f'Ctrl-C stops)')
        code = Watch(lambda: sources(self.game.source), self.rebuild)
        shaders = Watch(lambda: sources(self.game.root / 'shaders', '*.glsl'), self.shaders_saved)
        assets = Watch(lambda: sources(self.game.assets, '*'), self.assets_saved)
        try:
            while stop is None or not stop.is_set():
                time.sleep(CHECK_SECONDS)
                code.check()
                shaders.check()
                assets.check()
        except KeyboardInterrupt:
            pass
        finally:
            httpd.shutdown()
            if self.compile_server is not None:
                self.compile_server.kill()
        return 0


class Watch:
    """Files watched by their stamps: once a change has settled (nothing moved since the last
    look, and no file empty, which is one being saved), `then(changed)` is called with every
    path that moved or went."""

    def __init__(self, stamp, then):
        self.stamp, self.then = stamp, then
        self.stamps = stamp()
        self.changed, self.settling = [], False

    def check(self):
        current = self.stamp()
        moved = [p for p in current if self.stamps.get(p) != current[p]] + [p for p in self.stamps if p not in current]
        if moved:
            self.changed += [p for p in moved if p not in self.changed]
            self.stamps, self.settling = current, True
        elif self.settling and all(size > 0 for _, size in current.values()):
            self.settling = False
            changed, self.changed = self.changed, []
            self.then(changed)


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the wgf tool imports: there is nothing to run (wgf serve serves).').parse_args()
    raise SystemExit('devserver.py: a module the wgf tool imports: there is nothing to run (wgf serve serves)')
