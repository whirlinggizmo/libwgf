"""hxcpp, pinned: the release every native Haxe build here uses, in a haxelib folder of
libwgf's own, so a build here is made with the toolchain CI's is, whatever this machine's
global haxelib holds (a newer release, a fork set with `haxelib dev`). The module the
tools that build with hxcpp import; tools/setup_hxcpp.py is the command.

The folder is $LIBWGF_HAXELIB when it is set (CI's cached ~/haxelib), else one per
release in the per-user cache (tools/usercache.py: ~/.cache/libwgf/haxelib/4.3.2 on
Linux), shared by every checkout as Emscripten's is, never the machine's own haxelib
($HAXELIB_PATH, which a developer's shell may set); the tools run Haxe with HAXELIB_PATH
naming the folder (env()). Haxe runs `haxelib run hxcpp` with no version, so a version
in a build's arguments would mix that release's Haxe classes with whichever native
runtime haxelib resolves: the folder is what pins it, and ensure() refuses a build unless
hxcpp resolves there to exactly the pinned release. Upstream releases from 4.3.106 on
break haxe.zip.Uncompress past 64 KiB of output (HaxeFoundation/hxcpp#1422); 4.3.2
predates that. Standard library only.
"""
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from usercache import cache_dir  # noqa: E402

VERSION = '4.3.2'

_ready = None  # ensure()'s answer, once a run


def folder():
    """The haxelib folder hxcpp is pinned in."""
    return Path(os.environ['LIBWGF_HAXELIB']) if os.environ.get('LIBWGF_HAXELIB') else cache_dir('haxelib', VERSION)


def env():
    """The environment a native Haxe build runs in: haxelib reading the pinned folder."""
    return dict(os.environ, HAXELIB_PATH=str(folder()))


def resolved():
    """Where haxelib resolves hxcpp in the folder, when it is the pinned release; else None."""
    try:
        out = subprocess.run(['haxelib', 'path', 'hxcpp'], env=env(), capture_output=True, text=True,
                             timeout=60).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    paths = [line.strip() for line in out.splitlines() if line.strip() and not line.startswith('-')]
    if not paths:
        return None
    path = Path(paths[0]).resolve()
    try:
        inside = path.is_relative_to(folder().resolve())
        version = json.loads((path / 'haxelib.json').read_text(encoding='utf-8'))['version']
    except (OSError, ValueError, KeyError):
        return None
    return path if inside and version == VERSION else None


def ensure():
    """None when the pinned hxcpp is ready (installed into the folder if it wasn't, from
    lib.haxe.org, retried), else why not: a build needing hxcpp says so and stops."""
    global _ready
    if _ready is not None:
        return _ready or None
    if shutil.which('haxelib') is None:
        _ready = 'no haxelib on PATH (Haxe 4.3.7: BUILDING.md)'
        return _ready
    if resolved() is None:
        folder().mkdir(parents=True, exist_ok=True)
        for attempt in range(3):
            done = subprocess.run(['haxelib', '--always', 'install', 'hxcpp', VERSION], env=env(),
                                  capture_output=True, text=True)
            if done.returncode == 0:
                break
            time.sleep(10 * (attempt + 1))
        subprocess.run(['haxelib', '--always', 'set', 'hxcpp', VERSION], env=env(), capture_output=True)
    _ready = '' if resolved() is not None else f'hxcpp {VERSION} could not be installed into {folder()}'
    return _ready or None


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: tools/setup_hxcpp.py is the command.').parse_args()
    raise SystemExit('hxcpp.py: a module the other tools import: run tools/setup_hxcpp.py')
