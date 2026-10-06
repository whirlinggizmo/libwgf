#!/usr/bin/env python3
"""Set up the pinned MinGW-w64 for building libwgf on Windows, and print its bin.

    tools/setup_mingw.py

On Windows, the windows-x64-mingw presets build with the gcc this sets up, never
whichever gcc another tool put on PATH: configuring them runs it (cmake/mingw-w64.cmake).
It sets up the one libwgf is tested with: a
WinLibs release (standalone GCC and MinGW-w64, no installer), downloaded once, checked
against its SHA-256, and unpacked into the per-user cache
(tools/usercache.py: %LOCALAPPDATA%\\libwgf\\mingw\\<release>). It prints its bin
on stdout; progress goes to stderr. Run
again, it only prints. To move to a newer release, change RELEASE, URL, and SHA256
below together, from the release's page. Standard library only; Python 3.12 or later.
"""
import argparse
import hashlib
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from usercache import cache_dir  # noqa: E402

RELEASE = '16.2.0-ucrt-r2'  # short: gcc's include paths are long, and Windows' limit is 260
URL = ('https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-14.0.0-ucrt-r2/'
       'winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r2.zip')
SHA256 = 'd5dbafc4a170e762ca6143151ec918fb9e2c72736fb14cd704abebc6bdd5276a'


def say(text):
    print(f'setup_mingw: {text}', file=sys.stderr, flush=True)


def download(url, to):
    """`url` into the file `to`, with its SHA-256 as it arrives."""
    digest = hashlib.sha256()
    with urllib.request.urlopen(url, timeout=60) as response, open(to, 'wb') as out:
        total = int(response.headers.get('Content-Length') or 0)
        done = 0
        while True:
            block = response.read(1 << 20)
            if not block:
                break
            out.write(block)
            digest.update(block)
            done += len(block)
            if total and done % (32 << 20) < len(block):
                say(f'{done * 100 // total}%')
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    root = cache_dir('mingw')
    home = root / RELEASE
    bin_dir = home / 'mingw64' / 'bin'
    if not (bin_dir / 'gcc.exe').exists():
        archive = root / f'{RELEASE}.zip.part'
        say(f'downloading {URL}')
        digest = download(URL, archive)
        if digest != SHA256:
            archive.unlink()
            say(f'the download\'s SHA-256 is {digest}, not the pinned {SHA256}; not using it')
            return 1
        say(f'unpacking into {home}')
        partial = root / f'{RELEASE}.part'
        shutil.rmtree(partial, ignore_errors=True)
        with zipfile.ZipFile(archive) as zipped:
            # Windows' long-path form: some of GCC's files are deep
            zipped.extractall(f'\\\\?\\{partial}' if sys.platform == 'win32' else partial)
        archive.unlink()
        shutil.rmtree(home, ignore_errors=True)
        partial.rename(home)  # whole, or not there at all
    print(bin_dir)
    return 0


if __name__ == '__main__':
    sys.exit(main())
