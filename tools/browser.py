"""Shared by the browser tools (run_in_browser.py): finding and
launching a Chromium-based browser (headless, on a virtual X display, or on the
screen), a minimal DevTools-protocol session, and a record of every process a run
starts so all of it is stopped, whatever happens to the run. Standard library only.

    from browser import RunProcesses, find_browser, free_port, launch_browser, open_session, wait_for

The DevTools protocol is JSON over a WebSocket; the client here is the small part of
RFC 6455 that talking to a local browser needs (text frames, no extensions, no TLS).
"""
import base64
import hashlib
import itertools
import json
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = os.name == 'nt'
PYTHON = sys.executable

BROWSERS = (['brave.exe', 'chrome.exe', 'msedge.exe'] if WINDOWS else
            ['brave-browser-stable', 'google-chrome-stable', 'google-chrome', 'chromium', 'chromium-browser',
             'brave-browser', 'microsoft-edge-stable', 'microsoft-edge'])


def installed_browsers():
    """Where the browsers install themselves on Windows and macOS, which isn't on PATH:
    Brave, then Chrome, Chromium, then Edge (on every Windows 11)."""
    if WINDOWS:
        roots = [os.environ.get(v) for v in ('ProgramFiles', 'ProgramFiles(x86)', 'LOCALAPPDATA')]
        apps = ['BraveSoftware/Brave-Browser/Application/brave.exe', 'Google/Chrome/Application/chrome.exe',
                'Chromium/Application/chrome.exe', 'Microsoft/Edge/Application/msedge.exe']
        return [Path(root) / app for app in apps for root in roots if root]
    if sys.platform == 'darwin':
        return [Path(f'/Applications/{app}.app/Contents/MacOS/{app}')
                for app in ('Brave Browser', 'Google Chrome', 'Chromium', 'Microsoft Edge')]
    return []


def find_browser(explicit=None):
    explicit = explicit or os.environ.get('WEBCHECK_BROWSER')
    if explicit:
        return explicit
    for name in BROWSERS:
        found = shutil.which(name)
        if found:
            return found
    for path in installed_browsers():
        if path.exists():
            return str(path)
    raise RuntimeError(f'no Chromium-based browser found (tried {", ".join(BROWSERS)}, and where they '
                       'install); set --browser or WEBCHECK_BROWSER')


def find_xvfb():
    return None if WINDOWS else shutil.which('Xvfb')


def display_in_use(n):
    """Whether X display :n is taken: its lock file names a process still running, as an
    X server itself decides. A server killed before it cleaned up leaves its lock and
    socket behind; that display is free, and the next server there replaces both."""
    lock = Path(f'/tmp/.X{n}-lock')
    try:
        pid = int(lock.read_text().strip())
    except FileNotFoundError:
        return Path(f'/tmp/.X11-unix/X{n}').exists() and not stale_socket(n)
    except (OSError, ValueError):
        return True  # unreadable: someone else's, or being written
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True  # running, as another user
    return True


def stale_socket(n):
    """A display socket with nothing listening on it."""
    try:
        with socket.socket(socket.AF_UNIX) as s:
            s.connect(f'/tmp/.X11-unix/X{n}')
        return False
    except OSError:
        return True


def start_xvfb(run, extra_args=()):
    """Xvfb on a free display number: ':<n>' once its socket exists. extra_args go to
    Xvfb, such as -fbdir to keep the screen in a file."""
    for n in range(90, 200):
        if display_in_use(n):
            continue
        run.spawn([find_xvfb(), f':{n}', '-screen', '0', '1280x1024x24', '-nolisten', 'tcp', *extra_args])
        for _ in range(100):
            if Path(f'/tmp/.X11-unix/X{n}').exists():
                return f':{n}'
            time.sleep(0.05)
        raise RuntimeError(f'Xvfb :{n} did not start')
    raise RuntimeError('no free X display number for Xvfb')


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def wait_for(url, what, timeout=10):
    """The body of the first successful GET of url, polling until timeout (seconds)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=2) as r:
                return r.read()
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f'{what} did not start ({url})')


def page_target(debug_base, timeout=10):
    """The browser's page among its DevTools targets, polled until there is one: its list can
    answer before the page is in it (found on Windows, where the first answer was empty)."""
    deadline = time.monotonic() + timeout
    while True:
        targets = json.loads(wait_for(f'{debug_base}/json/list', 'page', timeout))
        page = next((t for t in targets if t.get('type') == 'page'), None)
        if page is not None:
            return page
        if time.monotonic() >= deadline:
            raise RuntimeError(f'the browser has no page ({debug_base}/json/list)')
        time.sleep(0.1)


class WebSocket:
    """A WebSocket client for a local DevTools endpoint: text messages in and out."""

    def __init__(self, url, timeout=15):
        parts = urllib.parse.urlsplit(url)
        self.sock = socket.create_connection((parts.hostname, parts.port or 80), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        path = parts.path + (f'?{parts.query}' if parts.query else '')
        self.sock.sendall((f'GET {path} HTTP/1.1\r\nHost: {parts.hostname}:{parts.port}\r\n'
                           'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                           f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        head = b''
        while b'\r\n\r\n' not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError(f'DevTools connection to {url} closed during the handshake')
            head += chunk
        head, self.buffer = head.split(b'\r\n\r\n', 1)
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        status = head.split(b'\r\n', 1)[0]
        if b' 101 ' not in status or accept not in head:
            raise ConnectionError(f'DevTools connection to {url} refused: {status.decode()}')
        self.sock.settimeout(None)
        self.send_lock = threading.Lock()

    def _read(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(max(65536, n - len(self.buffer)))
            if not chunk:
                raise ConnectionError('DevTools connection closed')
            self.buffer += chunk
        data, self.buffer = self.buffer[:n], self.buffer[n:]
        return data

    def _send_frame(self, opcode, payload):
        header = bytes([0x80 | opcode])
        n = len(payload)
        if n < 126:
            header += bytes([0x80 | n])
        elif n < 65536:
            header += bytes([0x80 | 126]) + struct.pack('!H', n)
        else:
            header += bytes([0x80 | 127]) + struct.pack('!Q', n)
        mask = os.urandom(4)  # a client masks everything it sends
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload)) if n < 4096 else _mask(payload, mask)
        with self.send_lock:
            self.sock.sendall(header + mask + masked)

    def send(self, text):
        self._send_frame(0x1, text.encode())

    def recv(self):
        """The next text message, or None when the connection closes."""
        message = b''
        while True:
            first, second = self._read(2)
            opcode, n = first & 0x0F, second & 0x7F
            if n == 126:
                n = struct.unpack('!H', self._read(2))[0]
            elif n == 127:
                n = struct.unpack('!Q', self._read(8))[0]
            payload = self._read(n)  # a server doesn't mask
            if opcode == 0x8:
                return None
            if opcode == 0x9:
                self._send_frame(0xA, payload)
                continue
            if opcode in (0x0, 0x1, 0x2):
                message += payload
                if first & 0x80:
                    return message.decode()

    def close(self):
        try:
            self._send_frame(0x8, b'')
        except OSError:
            pass
        try:
            self.sock.close()
        except OSError:
            pass


def _mask(payload, mask):
    """XOR a large payload with the 4-byte mask, fast: as one big integer."""
    n = len(payload)
    key = int.from_bytes((mask * (n // 4 + 1))[:n], 'big')
    return (int.from_bytes(payload, 'big') ^ key).to_bytes(n, 'big')


class Session:
    """A DevTools-protocol session on one target: requests with timeouts, and events."""

    def __init__(self, ws_url):
        try:
            self.ws = WebSocket(ws_url)
        except OSError as e:
            raise RuntimeError(f'DevTools connection failed: {e}') from e
        self.ids = itertools.count(1)
        self.pending = {}
        self.listeners = []
        self.lock = threading.Lock()
        self.closed = False
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        try:
            while True:
                text = self.ws.recv()
                if text is None:
                    break
                msg = json.loads(text)
                if 'id' in msg:
                    with self.lock:
                        waiter = self.pending.pop(msg['id'], None)
                    if waiter:
                        waiter[1].append(msg)
                        waiter[0].set()
                elif 'method' in msg:
                    for fn in list(self.listeners):
                        try:
                            fn(msg)
                        except Exception as e:  # a listener's bug must not end the session
                            print(f'browser: event listener failed: {e!r}', file=sys.stderr)
        except (OSError, ConnectionError, ValueError):
            pass
        self.closed = True
        with self.lock:
            for event, _ in self.pending.values():
                event.set()
            self.pending.clear()

    def send(self, method, params=None, timeout=15):
        """The result of a request. Every request times out: a hung or crashed page must
        fail the check, not stall the whole run."""
        if self.closed:
            raise RuntimeError(f'{method}: the DevTools connection is closed')
        request_id = next(self.ids)
        event, box = threading.Event(), []
        with self.lock:
            self.pending[request_id] = (event, box)
        self.ws.send(json.dumps({'id': request_id, 'method': method, 'params': params or {}}))
        if not event.wait(timeout):
            with self.lock:
                self.pending.pop(request_id, None)
            raise RuntimeError(f'{method}: no response from the browser in {timeout * 1000:.0f} ms')
        if not box:
            raise RuntimeError(f'{method}: the DevTools connection closed')
        if 'error' in box[0]:
            raise RuntimeError(f'{method}: {box[0]["error"].get("message")}')
        return box[0].get('result', {})

    def try_send(self, method, params=None, timeout=15):
        try:
            return self.send(method, params, timeout)
        except RuntimeError:
            return None

    def on_event(self, fn):
        self.listeners.append(fn)

    def close(self):
        self.ws.close()


def open_session(ws_url):
    return Session(ws_url)


class RunProcesses:
    """Everything a run starts, and how to stop all of it.

    Killing the spawned browser process isn't enough: Chromium-based browsers leave
    helper processes (zygotes, renderers, crashpad) that outlive the launcher. So:
      - every child is stopped with its descendants: its process group, which it starts
        in, on Linux and macOS, and its process tree (taskkill /T) on Windows;
      - every run has a unique profile directory, and any process whose command line
        names it belongs to this run and is swept up afterwards;
      - a detached watchdog (tools/watch_browser.py) waits for this process to disappear (a
        crash, a kill) and then does the same, so nothing leaks even if this never gets
        to run its cleanup. It is harmless when cleanup already ran.
    """

    def __init__(self, name):
        self.profile = tempfile.mkdtemp(prefix=f'libwgf-{name}-')
        self.pids = []
        self.children = []
        self.stopped = False
        self.lock = threading.Lock()
        flags = subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS if WINDOWS else 0
        subprocess.Popen([PYTHON, str(ROOT / 'tools' / 'watch_browser.py'), str(os.getpid()), self.profile],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                         start_new_session=not WINDOWS, creationflags=flags)

    def install_handlers(self):
        """Stop everything when this process exits or is interrupted."""
        import atexit
        atexit.register(self.stop_now)
        for name, code in (('SIGINT', 130), ('SIGTERM', 143), ('SIGHUP', 129)):
            if hasattr(signal, name):
                signal.signal(getattr(signal, name), lambda *_, code=code: (self.stop(), os._exit(code)))

    def spawn(self, command, env=None, log=None):
        """Start a child (in its own process group, off Windows) and record it for the
        watchdog. log: a file for its output (else it's discarded)."""
        out = open(log, 'wb') if log else subprocess.DEVNULL
        flags = subprocess.CREATE_NO_WINDOW if WINDOWS else 0
        try:
            child = subprocess.Popen([str(c) for c in command], stdin=subprocess.DEVNULL, stdout=out,
                                     stderr=subprocess.STDOUT if log else subprocess.DEVNULL, env=env,
                                     start_new_session=not WINDOWS, creationflags=flags)
        finally:
            if log:
                out.close()
        with self.lock:
            self.pids.append(child.pid)
            self.children.append(child)
            Path(f'{self.profile}.pids').write_text(' '.join(map(str, self.pids)))
        return child

    def strays(self):
        """Processes (other than this one) whose command line names the run's profile."""
        try:
            if WINDOWS:
                script = ("Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and "
                          f"$_.CommandLine.Contains('{self.profile}') }} | ForEach-Object {{ $_.ProcessId }}")
                listing = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True,
                                         text=True, creationflags=subprocess.CREATE_NO_WINDOW).stdout
            else:
                listing = '\n'.join(line for line in subprocess.run(['ps', '-eo', 'pid=,args='], capture_output=True,
                                                                     text=True).stdout.splitlines()
                                    if self.profile in line)
        except OSError:
            return []
        pids = []
        for line in listing.splitlines():
            field = line.strip().split(' ', 1)[0]
            if field.isdigit() and int(field) != os.getpid():
                pids.append(int(field))
        return pids

    def kill(self, pid, sig):
        if WINDOWS:
            # no signals on Windows: the tree, forcibly (a headless browser has no window
            # to be asked to close)
            subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], capture_output=True,
                           creationflags=subprocess.CREATE_NO_WINDOW)
            return
        for target in (lambda: os.killpg(pid, sig), lambda: os.kill(pid, sig)):
            try:
                target()
            except OSError:
                pass

    def signal_all(self, sig):
        for pid in list(self.pids):
            self.kill(pid, sig)
        for pid in self.strays():
            self.kill(pid, sig)

    def remove_files(self):
        for _ in range(10):  # a browser that just died can hold its profile for a moment
            shutil.rmtree(self.profile, ignore_errors=True)
            if not Path(self.profile).exists():
                break
            time.sleep(0.1)
        for suffix in ('.pids', '.log'):
            Path(f'{self.profile}{suffix}').unlink(missing_ok=True)

    def stop(self):
        """Ask politely, give the browser a moment, then force."""
        if self.stopped:
            return
        self.stopped = True
        if WINDOWS:  # taskkill /F is already the forceful kind
            self.signal_all(None)
        else:
            self.signal_all(signal.SIGTERM)
            for _ in range(20):  # its own children too: Xvfb names no profile, and cleans up on SIGTERM
                if not self.strays() and all(child.poll() is not None for child in self.children):
                    break
                time.sleep(0.1)
            self.signal_all(signal.SIGKILL)
        self.remove_files()

    def stop_now(self):
        """Last resort, from exit: force at once."""
        if self.stopped:
            return
        self.stopped = True
        self.signal_all(None if WINDOWS else signal.SIGKILL)
        self.remove_files()


def launch_browser(run, browser_path, display, profile=None, window_size='1024,900', extra_args=()):
    """Start a browser for a run, with DevTools on a free port: (its base URL, a session on
    the browser). display: 'headless' (WebGL2 on SwiftShader, a CPU renderer), 'xvfb' (the
    GPU through ANGLE on Vulkan, on a private virtual display started here) or 'screen'."""
    profile = profile or run.profile
    env = dict(os.environ)
    if display == 'xvfb':
        env.pop('WAYLAND_DISPLAY', None)  # X11 on the virtual display
        if not getattr(run, 'x_display', None):
            run.x_display = start_xvfb(run)
        env.update(DISPLAY=run.x_display, XDG_SESSION_TYPE='x11')
    debug_port = free_port()
    args = [browser_path,
            *(['--headless=new'] if display == 'headless' else []),
            f'--remote-debugging-port={debug_port}',
            f'--user-data-dir={profile}',
            '--no-first-run',
            '--no-default-browser-check',
            f'--window-size={window_size}',
            '--autoplay-policy=no-user-gesture-required',
            # a fake audio device: examples start audio on load; it still runs, but
            # nothing reaches PipeWire/PulseAudio or the speakers
            '--disable-audio-output',
            *(['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] if display == 'headless' else []),
            *(['--ozone-platform=x11', '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-angle=vulkan']
              if display == 'xvfb' else []),
            *extra_args,
            'about:blank']
    log = f'{profile}.log'
    run.spawn(args, env=env, log=log)
    debug_base = f'http://127.0.0.1:{debug_port}'
    try:
        version = json.loads(wait_for(f'{debug_base}/json/version', 'browser', 30))  # a cold CI start is slow
    except RuntimeError as e:
        try:
            output = '\n'.join(Path(log).read_text(errors='replace').strip().splitlines()[-20:])
        except OSError:
            output = ''
        raise RuntimeError(f'{e}; its last output:\n{output or "(nothing)"}') from e
    return debug_base, open_session(version['webSocketDebuggerUrl'])


def distinct_colours(session, png_base64, cap=64):
    """How many distinct colours a screenshot (Page.captureScreenshot's base64 PNG) has,
    stopping at cap: a cheap "did it draw anything?". The page decodes it, on an
    offscreen canvas of its own, never the example's (a canvas with a 2D or WebGL
    context can't be used for WebGPU)."""
    expression = f"""(async () => {{
        const image = await createImageBitmap(await (await fetch("data:image/png;base64,{png_base64}")).blob());
        const canvas = new OffscreenCanvas(image.width, image.height);
        const context = canvas.getContext("2d");
        context.drawImage(image, 0, 0);
        const pixels = context.getImageData(0, 0, image.width, image.height).data;
        const seen = new Set();
        for (let i = 0; i < pixels.length && seen.size < {cap}; i += 4)
            seen.add((pixels[i] << 16) | (pixels[i + 1] << 8) | pixels[i + 2]);
        return seen.size;
    }})()"""
    return session.send('Runtime.evaluate', {'expression': expression, 'awaitPromise': True,
                                             'returnByValue': True}, 30)['result']['value']


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('browser.py: a module the other tools import: there is nothing to run')
