r"""Where this machine keeps what the tools set up once and every build shares, such as
the Wine prefix: not a build's result or its work, so in neither out/ nor build/, and
not per checkout. One copy per user, safe to delete, made again on first use.

    from usercache import cache_dir
    prefix = cache_dir('wine')

$LIBWGF_CACHE_DIR if set, else the OS's per-user cache, in a libwgf folder there:
$XDG_CACHE_HOME or ~/.cache on Linux, ~/Library/Caches on macOS, %LOCALAPPDATA% on
Windows -- except under the Microsoft Store's Python, which redirects what it writes in
%LOCALAPPDATA% to a long path of its own that other programs don't look in: there it is
%USERPROFILE%\.cache. Standard library only. From wgrender's tools/hostcache.py.
"""
import os
import platform
import sys
from pathlib import Path


def cache_root():
    if os.environ.get('LIBWGF_CACHE_DIR'):
        return Path(os.environ['LIBWGF_CACHE_DIR'])
    system = platform.system()
    if system == 'Windows':
        base = Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData' / 'Local')
        # the Store's Python, known by where it is installed: probing a folder for the
        # redirect answered as the folder happened to be, so once another Python had made
        # %LOCALAPPDATA%\libwgf the Store's one looked there for what it had kept in
        # .cache (sightblinder's MinGW, under CMake's python3, after hxcpp's setup)
        if 'WindowsApps' in Path(os.path.realpath(sys.executable)).parts or 'WindowsApps' in Path(sys.prefix).parts:
            base = Path.home() / '.cache'
    elif system == 'Darwin':
        base = Path.home() / 'Library' / 'Caches'
    else:
        base = Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache')
    return base / 'libwgf'


def cache_dir(*parts):
    """A directory in the cache, made if it isn't there."""
    path = cache_root().joinpath(*parts)
    path.mkdir(parents=True, exist_ok=True)
    return path


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('usercache.py: a module the other tools import: there is nothing to run')
