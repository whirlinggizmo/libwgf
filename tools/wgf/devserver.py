"""`wgf serve`: the game in a browser, rebuilt as its Haxe is saved, and reloaded in the
running page with its state kept, for the wgf tool (tools/wgf/cli.py).

The first build is a hot one (hotreload-hx's macro, -D hotreload: deps/hotreload-hx) on
the full web host, which a reload never relinks, since the page has it. The page's
program asks /__hotreload?since=<n> for a build newer than its own (a long poll); each
rebuild, through a compilation server (haxe --wait), is stamped with its number on its
first line, and the page loads it beside itself and takes on its classes between two
frames (hotreload-hx's JsSwap): every static keeps its value, every object its fields,
the C side -- entities, nodes, textures, the window -- is untouched. A failed build is
reported here, and the page keeps what it has.

The game's assets are watched too: a file saved under them is told to the page, which
asks /__hotreload_assets?since=<n> (a long poll of its own, so an asset never re-imports the
program) and loads each one again in place (wgf_asset_reload, from the Haxe runtime's
hot build): a texture or a glTF, its handle kept, the game's state with it; a file that
doesn't load keeps what it had, with one error logged in the page. Standard library only.
"""
import json
import shutil
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
    def __init__(self, game, port, echo=lambda text: print(text, flush=True)):
        self.game = game
        self.port = port
        self.echo = echo
        self.out = game.build_dir('web')
        self.builds = Builds()
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
        """The page, the full host, the hot program, and the assets."""
        games.build_web(self.game, hot=True, out=self.out)
        self.args = [str(a) for a in ['-cp', games.binding.SOURCE.parent, '-cp', self.game.source, '--main',
                                      self.game.main, '--js', self.program, '-D', 'js-es=6', '-D', 'hotreload',
                                      *games.hot_args()]]

    def rebuild(self, changed):
        names = ', '.join(Path(p).name for p in changed)
        self.echo(f'wgf serve: building ({names} changed)')
        started = time.monotonic()
        listing = self.out / '.hotreload-changed'
        listing.write_text('\n'.join(changed), encoding='utf-8')
        command = [games.binding.haxe()]
        if self.compile_port is not None:
            command += ['--connect', str(self.compile_port)]
        command += self.args + ['-D', 'hotreload_reload', '-D', f'hotreload_invalidate={listing}']
        done = subprocess.run(command, cwd=self.game.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors='replace')
        if done.returncode != 0:
            self.echo(done.stdout.strip())
            self.echo('wgf serve: the build failed; the page keeps what it has')
            return False
        number = self.builds.version + 1
        text = self.program.read_text(encoding='utf-8')
        self.program.write_text(f'globalThis.__hotreload_built = {number}; ' + text, encoding='utf-8')
        self.builds.bump()
        self.echo(f'wgf serve: built in {round((time.monotonic() - started) * 1000)} ms; the page loads it next')
        return True

    def assets_saved(self, changed):
        """Asset files saved (or gone): told to the page, which loads each again; copied
        first where the build has a copy of the assets rather than a link to them."""
        served = self.out / 'assets'
        names = []
        for path in changed:
            name = Path(path).relative_to(self.game.assets).as_posix()
            names.append(name)
            if not served.is_symlink() and Path(path).is_file():
                (served / name).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, served / name)
        self.echo(f'wgf serve: {", ".join(names)} saved; the page loads it again')
        self.saved.bump(names)

    def handler(self):
        builds, saved, program, name = self.builds, self.saved, self.program, self.game.name

        class Handler(server.handler_class(self.out, assets=None)):
            def end_headers(self):
                self.send_header('Cache-Control', 'no-store')  # a reload's files are new each build
                super().end_headers()

            def do_GET(self):
                parts = urllib.parse.urlsplit(self.path)
                if parts.path not in ('/__hotreload', '/__hotreload_assets'):
                    return super().do_GET()
                try:
                    since = int(urllib.parse.parse_qs(parts.query).get('since', ['0'])[0] or 0)
                except ValueError:
                    since = 0
                if parts.path == '/__hotreload_assets':
                    version, files = saved.wait_newer(since, POLL_SECONDS)
                    body = json.dumps({'version': version, 'files': files}).encode()
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
        """Build, serve, and rebuild on each save until `stop` is set (or forever)."""
        self.first_build()
        self.compile_port = self.start_compilation_server()
        base, httpd = server.serve(self.out, port=self.port, handler=self.handler())
        self.echo(f'wgf serve: {self.game.name} at {base}/ (saving its Haxe or an asset reloads it, its state '
                  f'kept; Ctrl-C stops)')
        code = Watch(lambda: sources(self.game.source), self.rebuild)
        assets = Watch(lambda: sources(self.game.assets, '*'), self.assets_saved)
        try:
            while stop is None or not stop.is_set():
                time.sleep(CHECK_SECONDS)
                code.check()
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
