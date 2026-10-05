#!/usr/bin/env python3
"""Run every C example in a real window, and fail any that dies or reports an error;
save a screenshot of each.

    tools/check_desktop.py [--variant NAME] [--seconds S] [--out DIR] [example ...]

Builds the variant (default linux-x64-debug), stages it, builds the examples against
it (tools/examples.py), then runs each in a window on a
private virtual display (Xvfb), so nothing shows on the real screen, with OpenGL on
the CPU (Mesa's llvmpipe). After --seconds (default 3) its screen is saved as a PNG
in --out (default build/<preset>/check_desktop/<example>.png), cut to what was drawn,
and it is stopped. An example fails if it exits before then, or logs an [ERROR] or
[FATAL] line. A Windows variant (windows-x64-mingw-debug) runs each under Wine
(tools/run_wine.py) on the same display, so a Windows program's window is checked here
too; Wine takes a while to start, so its examples get --seconds plus 5. Xvfb keeps its screen in a file (-fbdir, X's XWD format), read here and
written as PNG with the standard library alone. Linux only, for Xvfb: elsewhere it is
skipped (77). Everything it starts is stopped, also when it is interrupted.
tools/check_web.py does the same for the web, tools/run_smoke.py headless.
"""
import argparse
import os
import re
import struct
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import browser  # noqa: E402
import examples  # noqa: E402
import variants  # noqa: E402
import wine  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SKIP = 77
PROBLEM = re.compile(r'\[(ERROR|FATAL)')


def read_xwd(path):
    """(width, height, rows of RGB bytes) from an XWD file of a 24-bit TrueColor screen."""
    data = path.read_bytes()
    fields = struct.unpack('>25I', data[:100])
    header_size, width, height = fields[0], fields[4], fields[5]
    byte_order, bits_per_pixel, bytes_per_line = fields[7], fields[11], fields[12]
    colors = fields[19]
    if bits_per_pixel != 32:
        raise RuntimeError(f'the virtual screen has {bits_per_pixel} bits a pixel; expected 32')
    start = header_size + colors * 12
    rows = []
    for y in range(height):
        line = data[start + y * bytes_per_line:start + y * bytes_per_line + width * 4]
        # 32 bits a pixel, 0x00RRGGBB: least significant byte first is B, G, R, X
        b, g, r = (line[0::4], line[1::4], line[2::4]) if byte_order == 0 else (line[3::4], line[2::4], line[1::4])
        rgb = bytearray(width * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = r, g, b
        rows.append(bytes(rgb))
    return width, height, rows


def crop(width, rows):
    """The rows and columns with anything drawn: the screen is black but for windows."""
    used = [y for y, row in enumerate(rows) if row.strip(b'\0')]
    if not used:
        return width, rows
    rows = rows[used[0]:used[-1] + 1]
    right = max(len(row.rstrip(b'\0')) for row in rows)
    right = (right + 2) // 3
    return right, [row[:right * 3] for row in rows]


def write_png(path, width, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b''.join(b'\0' + row for row in rows)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, len(rows), 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--variant', help="default: this machine's debug preset")
    parser.add_argument('--seconds', type=float, default=3.0)
    parser.add_argument('--out', type=Path, help="default: the variant's work, build/<preset>/check_desktop")
    parser.add_argument('examples', nargs='*')
    opts = parser.parse_args()
    if not browser.find_xvfb():
        print('check_desktop: SKIPPING every example in a window (no Xvfb, the virtual X display, on Linux)')
        return SKIP
    opts.variant = opts.variant or variants.native('debug')
    opts.out = opts.out or variants.work(opts.variant) / 'check_desktop'
    if variants.is_web(opts.variant) or variants.is_headless(opts.variant):
        sys.exit(f'check_desktop: {opts.variant} has no windows; use a native variant')
    names = opts.examples or examples.names()
    unknown = [n for n in names if n not in examples.names()]
    if unknown:
        sys.exit(f'check_desktop: no such example: {", ".join(unknown)}')
    windows = opts.variant.startswith('windows-')
    if windows and not wine.find_wine():
        print(f'check_desktop: SKIPPING {opts.variant}\'s examples in a window (no Wine: tools/wine.py)')
        return SKIP
    try:
        examples.prepare(opts.variant)
        binaries = [examples.build(opts.variant, n) for n in names]
    except RuntimeError as e:
        sys.exit(f'check_desktop: {e}')
    opts.out.mkdir(parents=True, exist_ok=True)

    run = browser.RunProcesses('check-desktop')
    run.install_handlers()
    failed = 0
    try:
        screen_dir = Path(run.profile) / 'screen'
        screen_dir.mkdir()
        display = browser.start_xvfb(run, ['-fbdir', str(screen_dir)])
        env = dict(os.environ, DISPLAY=display, LIBGL_ALWAYS_SOFTWARE='1', XDG_SESSION_TYPE='x11')
        env.pop('WAYLAND_DISPLAY', None)
        print(f'check_desktop: {len(names)} example(s), {opts.variant}, Xvfb {display}', flush=True)
        for name, binary in zip(names, binaries):
            log = Path(run.profile) / f'{name}.log'
            command = [sys.executable, str(ROOT / 'tools' / 'run_wine.py'), str(binary)] if windows else [binary]
            child = run.spawn(command, env=env, log=log)
            deadline = time.monotonic() + opts.seconds + (5.0 if windows else 0.0)
            while time.monotonic() < deadline and child.poll() is None:
                time.sleep(0.05)
            problems = []
            if child.poll() is not None:
                problems.append(f'exited after less than {opts.seconds:g} s, with {child.returncode}')
            else:
                width, height, rows = read_xwd(screen_dir / 'Xvfb_screen0')
                width, rows = crop(width, rows)
                write_png(opts.out / f'{name}.png', width, rows)
                run.kill(child.pid, 15)
                try:
                    child.wait(timeout=5)
                except Exception:
                    run.kill(child.pid, 9)
            text = log.read_text(errors='replace') if log.exists() else ''
            problems += [line for line in text.splitlines() if PROBLEM.search(line)]
            if problems:
                failed += 1
                print(f'  FAIL  {name}')
                for line in problems + [f'  | {line}' for line in text.splitlines()[-5:]]:
                    print(f'          {line}')
            else:
                print(f'  ok    {name}')
    finally:
        run.stop()
    print(f'screenshots: {opts.out}')
    print(f'FAIL: {failed} of {len(names)} example(s)' if failed else f'PASS: {len(names)} example(s)')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
