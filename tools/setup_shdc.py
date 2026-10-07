#!/usr/bin/env python3
"""Set up the pinned sokol-shdc, the shader compiler gfx's shaders are generated with,
and print its path.

    tools/setup_shdc.py

sokol-shdc turns a shader written once in GLSL into the sources and reflection each
graphics backend needs (tools/gen_shaders.py runs it). It is downloaded once, from
floooh/sokol-tools-bin at the commit deps/sokol/VERSION records, or from the mirror
(robknopf/sokol-tools-bin, a fork that keeps the pin tagged) when floooh's can't give
it; checked against its SHA-256 below, wherever it came from; and kept in the per-user
cache (tools/usercache.py: ~/.cache/libwgf/sokol-shdc/<commit>/). It prints the
program's path on stdout; progress goes to stderr. Run again, it checks the cached
copy's SHA-256 and prints; a copy that doesn't match is downloaded again. Building
libwgf needs none of this: the generated shaders are committed. Standard library only.
To move to a newer commit, change the pin in deps/sokol/VERSION, HASHES_PIN, and the
hashes together, and tag the new pin in the mirror (pin-<first 12 characters>).
As libwgt's.
"""
import argparse
import hashlib
import os
import platform
import stat
import sys
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from usercache import cache_dir  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ('floooh/sokol-tools-bin', 'robknopf/sokol-tools-bin')  # tried in order
HASHES_PIN = '11d0cf678105d614d675e6d9bd2aaf3eeff12f8c'  # the sokol-tools-bin commit the hashes are for
BINARIES = {  # (system, machine): (path in the repository, SHA-256)
    ('Linux', 'x86_64'): ('linux/sokol-shdc', 'ed35e89ef381d521a499096ed4ada85e4d135d8011e151cca6b7d893c43b21df'),
    ('Linux', 'aarch64'): ('linux_arm64/sokol-shdc', '446b4bcea0c81d3ae529bc0d93533ea661b017f5b9ec2b2293a4c85f5fdcb639'),
    ('Darwin', 'x86_64'): ('osx/sokol-shdc', '8b4a6ac1172ec0d90dd41d611067d5e87a51e78dc28acb216cfc341d880b1d78'),
    ('Darwin', 'arm64'): ('osx_arm64/sokol-shdc', '92db37975ad7ff3c3c9bc27cba1503287377cb287ebabf60d1c6b597abfa3244'),
    ('Windows', 'AMD64'): ('win32/sokol-shdc.exe', 'bd616287f9ea689d53c6d260e443ee733e61ae1b73a9b37adc482ead0364d561'),
}


def say(text):
    print(f'setup_shdc: {text}', file=sys.stderr, flush=True)


def pinned_commit():
    """The sokol-tools-bin commit deps/sokol/VERSION pins: its `sokol-tools-bin <commit>` line."""
    version = ROOT / 'deps' / 'sokol' / 'VERSION'
    for line in version.read_text().splitlines():
        words = line.split()
        if len(words) == 2 and words[0] == 'sokol-tools-bin':
            return words[1]
    return None


def download(path, sha256, commit):
    """The program's bytes from the first source that gives the pinned one; None if none does."""
    failures = []
    for source in SOURCES:
        url = f'https://github.com/{source}/raw/{commit}/bin/{path}'
        say(f'downloading {url}')
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                data = response.read()
        except OSError as error:  # unreachable, or gone: the next source
            failures.append(f'{url}: {error}')
            continue
        digest = hashlib.sha256(data).hexdigest()
        if digest != sha256:  # never used, wherever it came from
            failures.append(f'{url}: SHA-256 {digest}, not the pinned {sha256}')
            continue
        return data
    say('no source gave the pinned sokol-shdc:\n  ' + '\n  '.join(failures))
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    key = (platform.system(), platform.machine())
    if key not in BINARIES:
        say(f'no sokol-shdc for {key[0]} {key[1]}')
        return 1
    commit = pinned_commit()
    if commit != HASHES_PIN:
        say(f'deps/sokol/VERSION pins sokol-tools-bin {commit}, but the hashes here are for {HASHES_PIN}; '
            'change them together')
        return 1
    path, sha256 = BINARIES[key]
    program = cache_dir('sokol-shdc', commit[:12]) / Path(path).name
    if not program.exists() or hashlib.sha256(program.read_bytes()).hexdigest() != sha256:
        data = download(path, sha256, commit)  # a cached copy that doesn't match is replaced
        if data is None:
            return 1
        partial = program.with_name(program.name + '.part')
        partial.write_bytes(data)
        if os.name != 'nt':
            partial.chmod(partial.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        partial.replace(program)
    print(program)
    return 0


if __name__ == '__main__':
    sys.exit(main())
