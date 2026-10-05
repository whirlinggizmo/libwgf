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
from pathlib import Path


def cache_root():
    if os.environ.get('LIBWGF_CACHE_DIR'):
        return Path(os.environ['LIBWGF_CACHE_DIR'])
    system = platform.system()
    if system == 'Windows':
        base = Path(os.environ.get('LOCALAPPDATA') or Path.home() / 'AppData' / 'Local')
        # redirected? Only a folder that exists shows where it really is
        (base / 'libwgf').mkdir(parents=True, exist_ok=True)
        if os.path.realpath(base / 'libwgf') != str(base / 'libwgf'):
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
