#!/usr/bin/env python3
"""The browser tools' watchdog: stops what a run started if the run itself can't.

    tools/watch_browser.py TOOL_PID PROFILE

tools/browser.py starts one per run, detached. It waits for the process TOOL_PID (the tool) to
exit, however it went (a crash, a kill), then kills every process the run recorded in
PROFILE.pids (with its children) and every process whose command line names PROFILE, the
run's browser profile directory, and removes the profile and its files. When the run
cleaned up after itself, there is nothing left for it to do.
"""
import argparse
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

WINDOWS = os.name == 'nt'


def wait_for_exit(pid):
    if WINDOWS:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        SYNCHRONIZE, INFINITE = 0x00100000, 0xFFFFFFFF
        handle = kernel32.OpenProcess(SYNCHRONIZE, False, pid)
        if handle:
            kernel32.WaitForSingleObject(handle, INFINITE)
            kernel32.CloseHandle(handle)
        return
    while True:
        try:
            os.kill(pid, 0)
        except ProcessLookupError:
            return
        except PermissionError:
            pass
        time.sleep(1)


def strays(profile):
    """Processes, other than this one, whose command line names the profile."""
    marker = str(profile)
    if WINDOWS:
        script = ("Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and "
                  f"$_.CommandLine.Contains('{marker}') }} | ForEach-Object {{ $_.ProcessId }}")
        out = subprocess.run(['powershell', '-NoProfile', '-Command', script],
                             capture_output=True, text=True).stdout
    else:
        out = subprocess.run(['ps', '-eo', 'pid=,args='], capture_output=True, text=True).stdout
        out = '\n'.join(line for line in out.splitlines() if marker in line)
    pids = []
    for line in out.splitlines():
        field = line.strip().split(' ', 1)[0]
        if field.isdigit() and int(field) != os.getpid():
            pids.append(int(field))
    return pids


def kill_tree(pid):
    if WINDOWS:
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(pid)], capture_output=True)
        return
    for target in (lambda: os.killpg(pid, signal.SIGKILL), lambda: os.kill(pid, signal.SIGKILL)):
        try:
            target()
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('tool_pid', type=int, help='the process of the tool whose run this watches')
    parser.add_argument('profile', type=Path, help="the run's browser profile directory")
    opts = parser.parse_args()
    tool_pid, profile = opts.tool_pid, opts.profile
    wait_for_exit(tool_pid)
    pids_file = Path(f'{profile}.pids')
    if pids_file.exists():
        for pid in pids_file.read_text().split():
            kill_tree(int(pid))
    for pid in strays(profile):
        kill_tree(pid)
    time.sleep(0.5)  # a killed browser lets go of its profile
    shutil.rmtree(profile, ignore_errors=True)
    for suffix in ('.pids', '.log'):
        Path(f'{profile}{suffix}').unlink(missing_ok=True)


if __name__ == '__main__':
    main()
