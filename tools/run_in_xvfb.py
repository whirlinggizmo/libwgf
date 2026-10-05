#!/usr/bin/env python3
"""Run a native test program in a real window, on a private virtual display.

    tools/run_in_xvfb.py PROGRAM [--timeout S]

Starts Xvfb on a free display, runs PROGRAM there with OpenGL on the CPU (Mesa's
llvmpipe), as tools/check_desktop.py runs the examples, prints what it prints, and
exits with its exit code. A test that opens a window through wgf_app_run reads its
pixels back from the window, as a program would draw them, with nothing shown on
the real screen. Exits 77 (ctest's skip) where there is no Xvfb (it is Linux's).
Everything it starts is stopped, also when it is interrupted. Standard library only.
"""
import argparse
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import browser  # noqa: E402

SKIP = 77


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('program')
    parser.add_argument('--timeout', type=float, default=60.0)
    opts = parser.parse_args()
    if not browser.find_xvfb():
        print(f'run_in_xvfb: SKIPPING {Path(opts.program).name} (no Xvfb, the virtual X display, on Linux)')
        return SKIP
    run = browser.RunProcesses('run-in-xvfb')
    run.install_handlers()
    try:
        display = browser.start_xvfb(run)
        env = dict(os.environ, DISPLAY=display, LIBGL_ALWAYS_SOFTWARE='1', XDG_SESSION_TYPE='x11')
        env.pop('WAYLAND_DISPLAY', None)
        log = Path(run.profile) / 'program.log'
        child = run.spawn([str(Path(opts.program).resolve())], env=env, log=log)
        deadline = time.monotonic() + opts.timeout
        while child.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        if log.exists():
            sys.stdout.write(log.read_text(errors='replace'))
        if child.poll() is None:
            print(f'run_in_xvfb: still running after {opts.timeout:g} s')
            return 1
        return child.returncode
    finally:
        run.stop()


if __name__ == '__main__':
    sys.exit(main())
