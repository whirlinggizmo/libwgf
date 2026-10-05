"""Serving a built site as a static host serves it, for tools/serve_site.py and the checks
that load pages in a browser (tools/check_web.py, tools/run_in_browser.py). Ported from
wgrender's tools/serve_site.py, its handler made a module, so every check serves the
same way the dev server does. Standard library only.

    from server import handler_class, serve
    base_url, httpd = serve(site_dir)                # a thread, on a free port
    Handler = handler_class(site_dir, cache=True, gzip=True)   # to subclass for a page of its own

What the handler does:

- The web's MIME types, whatever this machine says: Python's mimetypes reads Windows'
  registry, where .js is often text/plain, and a browser refuses to run a module script
  served as that.
- The shared asset tree mounted at the site's root (examples/assets/ at /assets/: one
  level above every program's page, which names its host "../assets"), so assets are
  never copied or symlinked into a site: one source of truth, on every platform. A path
  that climbs out of it is refused.
- Cross-Origin-Opener-Policy: same-origin and Cross-Origin-Embedder-Policy: require-corp
  on every response: cross-origin isolation, which a threaded build needs for
  SharedArrayBuffer.
- A single Range request answered 206 (a suffix range too); a malformed one gets the
  whole file.
- Cache-Control: no-store unless `cache`, so a reload always gets the latest build. With
  `cache`, as a host should serve: a versioned file (?v=<hash> in its URL, as the page
  loads code: tools/finish_site.py) is immutable for a year, and the rest no-cache,
  answered 304 while unchanged.
- With `gzip`, the page, scripts, wasm, JSON and the other types a static host compresses
  (GitHub Pages gzips .glb, .gltf and .ttf too: keeping this list short once hid a bug
  that broke the asset fetch on exactly the types a short list never compressed) are
  sent gzip-encoded when the browser accepts it, never for a Range request.
"""
import email.utils
import gzip
import http.server
import io
import os
import posixpath
import threading
import urllib.parse
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / 'examples' / 'assets'
MOUNT = '/assets/'  # one level above every program's page, which names its host "../assets"

GZIP_TYPES = ('.html', '.js', '.wasm', '.json', '.css', '.txt', '.glb', '.gltf', '.ttf')

MIME_TYPES = {
    '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
    '.json': 'application/json', '.html': 'text/html', '.css': 'text/css',
    '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml',
    '.gltf': 'model/gltf+json', '.glb': 'model/gltf-binary', '.ttf': 'font/ttf',
    '.ogg': 'audio/ogg', '.mp3': 'audio/mpeg', '.wav': 'audio/wav',
}


class _LimitReader:
    """Wraps a file so copyfile() stops after `remaining` bytes (one Range)."""

    def __init__(self, f, remaining):
        self.f, self.remaining = f, remaining

    def read(self, n=-1):
        if self.remaining <= 0:
            return b''
        if n is None or n < 0 or n > self.remaining:
            n = self.remaining
        data = self.f.read(n)
        self.remaining -= len(data)
        return data

    def close(self):
        self.f.close()


def parse_range(header, file_len):
    """A single byte range as (start, end), inclusive; None for anything else (the whole file)."""
    if not header.startswith('bytes=') or ',' in header:
        return None
    first, _, last = header[len('bytes='):].partition('-')
    try:
        if first == '':  # a suffix: the last N bytes
            n = int(last)
            if n <= 0:
                return None
            start, end = max(0, file_len - n), file_len - 1
        else:
            start = int(first)
            end = min(int(last), file_len - 1) if last else file_len - 1
    except ValueError:
        return None
    if start > end or start >= file_len:
        return None
    return start, end


def handler_class(site, assets=ASSETS, mount=MOUNT, cache=False, gzip_files=False, quiet=True):
    """A request handler serving `site` at /, with `assets` mounted at `mount`."""
    site = Path(site).resolve()
    assets = Path(assets).resolve() if assets else None
    mount_parts = tuple(p for p in mount.split('/') if p)
    gzipped = {}  # (path, mtime) -> compressed bytes

    class Handler(http.server.SimpleHTTPRequestHandler):
        extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, **MIME_TYPES}

        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(site), **kwargs)

        def translate_path(self, path):
            path = posixpath.normpath(urllib.parse.unquote(urllib.parse.urlsplit(path).path))
            parts = tuple(p for p in path.split('/') if p not in ('', '.', '..'))
            if assets is not None and parts[:len(mount_parts)] == mount_parts:
                return str(assets.joinpath(*parts[len(mount_parts):]))
            return str(site.joinpath(*parts))

        def send_gzipped(self, path):
            """The file compressed (304 while unchanged, with `cache`), or None to serve it as is."""
            if not gzip_files or not path.endswith(GZIP_TYPES) or 'gzip' not in self.headers.get('Accept-Encoding', ''):
                return None
            try:
                fs = os.stat(path)
            except OSError:
                return None
            since = self.headers.get('If-Modified-Since')
            if since and cache:
                try:
                    if int(fs.st_mtime) <= email.utils.parsedate_to_datetime(since).timestamp():
                        self.send_response(304)
                        self.send_header('Last-Modified', self.date_time_string(fs.st_mtime))
                        self.end_headers()
                        return io.BytesIO(b'')
                except (TypeError, ValueError):
                    pass
            key = (path, fs.st_mtime)
            if key not in gzipped:
                with open(path, 'rb') as f:
                    gzipped[key] = gzip.compress(f.read(), 6)
            body = gzipped[key]
            self.send_response(200)
            self.send_header('Content-Type', self.guess_type(path))
            self.send_header('Content-Encoding', 'gzip')
            self.send_header('Vary', 'Accept-Encoding')
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Last-Modified', self.date_time_string(fs.st_mtime))
            self.end_headers()
            return io.BytesIO(body)

        def send_head(self):
            path = self.translate_path(self.path)
            wanted = self.headers.get('Range')
            if os.path.isdir(path):
                return super().send_head()
            if not wanted:
                zipped = self.send_gzipped(path)
                return zipped if zipped is not None else super().send_head()
            try:
                f = open(path, 'rb')
            except OSError:
                self.send_error(404, 'File not found')
                return None
            try:
                fs = os.fstat(f.fileno())
                rng = parse_range(wanted, fs.st_size)
                if rng is None:
                    f.close()
                    return super().send_head()  # malformed: the whole file, 200
                start, end = rng
                self.send_response(206)
                self.send_header('Content-Type', self.guess_type(path))
                self.send_header('Content-Range', f'bytes {start}-{end}/{fs.st_size}')
                self.send_header('Content-Length', str(end - start + 1))
                self.send_header('Last-Modified', self.date_time_string(fs.st_mtime))
                self.end_headers()
                f.seek(start)
                return _LimitReader(f, end - start + 1)
            except Exception:
                f.close()
                raise

        def end_headers(self):
            if not cache:
                self.send_header('Cache-Control', 'no-store')
            elif 'v=' in urllib.parse.urlsplit(self.path).query:
                self.send_header('Cache-Control', 'public, max-age=31536000, immutable')
            else:
                self.send_header('Cache-Control', 'no-cache')
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
            self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
            super().end_headers()

        def log_message(self, *args):
            if not quiet:
                super().log_message(*args)

    return Handler


def serve(site, port=0, host='127.0.0.1', handler=None, **options):
    """Serve `site` in a thread: (its base URL, the server). `handler` is a class from
    handler_class (or a subclass of one); without it, one with `options`."""
    Handler = handler or handler_class(site, **options)
    httpd = http.server.ThreadingHTTPServer((host, port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return f'http://{host}:{httpd.server_address[1]}', httpd


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run (tools/serve_site.py serves).').parse_args()
    raise SystemExit('server.py: a module the other tools import: there is nothing to run (tools/serve_site.py serves)')
