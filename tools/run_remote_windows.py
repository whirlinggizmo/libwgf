#!/usr/bin/env python3
"""Build and test this checkout on a Windows machine over SSH, as it is, committed
or not, and leave nothing behind there.

    tools/run_remote_windows.py HOST [--msvc] [--variant NAME ...] [--path DIR ...] [--keep]

Packs the working tree (every tracked file, and new ones git doesn't ignore), copies
it to HOST with scp into a scratch folder in the remote user's profile, configures,
builds, and tests each variant there with its CMake preset (default: both
windows-x64-mingw ones, or with --msvc both windows-x64-msvc ones), and prints how it
went, every compile error of a variant, not the first. Nothing is set up first: the
MSVC presets use Visual Studio's generator, which finds the compiler itself, and the
MinGW ones' toolchain file sets up the pinned gcc. The scratch folder and the copied
files are deleted afterwards, also when a step fails, unless --keep.

HOST is an ssh destination, as `ssh HOST` takes it (a Host from ~/.ssh/config), whose
shell is cmd.exe. It needs CMake, Ninja, and Python 3.9 or later, and for MSVC Visual
Studio with its C++ tools. MinGW-w64 comes from tools/setup_mingw.py, run by the
toolchain file, which sets up the pinned release in the remote user's cache the first
time (about 270 MB). --path puts directories at the front of PATH for the run, for tools
the machine keeps elsewhere, as %USERPROFILE%\\... paths. Exits 0 when every variant
built and passed. Standard library only.
"""
import argparse
import os
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_VARIANTS = ('windows-x64-mingw-debug-headless', 'windows-x64-mingw-debug')
MSVC_VARIANTS = ('windows-x64-msvc-debug-headless', 'windows-x64-msvc-debug')


def working_tree():
    """Every file to send: tracked, and new ones not ignored, as git lists them."""
    listed = subprocess.run(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=ROOT, check=True,
                            stdout=subprocess.PIPE).stdout.decode()
    return [name for name in listed.split('\0') if name and (ROOT / name).is_file()]


def script(folder, archive, variants, paths, keep):
    """The .bat that runs on the remote machine."""
    lines = ['@echo off',
             'rem written by libwgf tools/run_remote_windows.py; it deletes itself',
             f'set ROOT=%USERPROFILE%\\{folder}',
             f'set PATH={";".join(paths + ["%PATH%"])}',
             'set FAILED=0',
             'if exist "%ROOT%" rmdir /s /q "%ROOT%"',
             'mkdir "%ROOT%"',
             'cd /d "%ROOT%"',
             f'tar -xzf "%USERPROFILE%\\{archive}"',
             'if errorlevel 1 (echo run_remote_windows: unpacking failed & set FAILED=1 & goto done)']
    # Nothing to set up first: MSVC's presets use Visual Studio's generator, which finds the
    # compiler, and MinGW's toolchain file sets up the pinned gcc (cmake/mingw-w64.cmake)
    for variant in variants:
        lines += [f'echo run_remote_windows: {variant}',
                  f'cmake --preset {variant} > configure-{variant}.log 2>&1',
                  f'if errorlevel 1 (type configure-{variant}.log & set FAILED=1 & goto next_{variant})',
                  # every error, not the first: Ninja's -k 0 (MSBuild, the msvc presets', goes on by itself)
                  f'cmake --build --preset {variant}' + ('' if '-msvc-' in variant else ' -- -k 0')
                  + f' > build-{variant}.log 2>&1',
                  f'if errorlevel 1 (findstr /c:"error:" /c:"error C" /c:"error MSB" /c:"warning C" /c:"FAILED:"'
                  f' build-{variant}.log & set FAILED=1'
                  f' & goto next_{variant})',
                  f'ctest --preset {variant}',
                  'if errorlevel 1 set FAILED=1',
                  f':next_{variant}']
    lines += [':done', 'cd /d "%USERPROFILE%"']
    if not keep:
        lines += ['rmdir /s /q "%ROOT%"', f'del "%USERPROFILE%\\{archive}"']
    lines += ['(goto) 2>nul & del "%~f0" & exit /b %FAILED%']  # delete this .bat as it ends
    return '\r\n'.join(lines) + '\r\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('host')
    parser.add_argument('--msvc', action='store_true', help='build with MSVC (default: MinGW-w64)')
    parser.add_argument('--variant', action='append', dest='variants')
    parser.add_argument('--path', action='append', dest='paths', default=[])
    parser.add_argument('--keep', action='store_true', help='leave the scratch folder there, to look into it')
    args = parser.parse_args()
    variants = args.variants or list(MSVC_VARIANTS if args.msvc else DEFAULT_VARIANTS)
    tag = f'libwgf-remote-{os.getpid()}'
    archive, bat = f'{tag}.tgz', f'{tag}.bat'

    with tempfile.TemporaryDirectory() as scratch:
        local_archive, local_bat = Path(scratch) / archive, Path(scratch) / bat
        with tarfile.open(local_archive, 'w:gz') as tar:
            for name in working_tree():
                tar.add(ROOT / name, arcname=name)
        local_bat.write_bytes(script(tag, archive, variants, args.paths, args.keep).encode())
        print(f'run_remote_windows: {", ".join(variants)} on {args.host}', flush=True)
        copied = subprocess.run(['scp', '-q', str(local_archive), str(local_bat), f'{args.host}:'])
        if copied.returncode != 0:
            print('run_remote_windows: copying to the host failed')
            return 1
    ran = subprocess.run(['ssh', args.host, bat])
    if args.keep:
        print(f'run_remote_windows: kept in %USERPROFILE%\\{tag} on {args.host}')
    print('run_remote_windows: ' + ('PASS' if ran.returncode == 0 else 'FAIL'))
    return 0 if ran.returncode == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
