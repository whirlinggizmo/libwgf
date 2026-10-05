"""Finding Wine, for the tools that run Windows programs on Linux or macOS
(run_wine.py, verify_builds.py).

    from wine import find_wine
    wine = find_wine()    # a path, or None

$WINE if set, else wine64 or wine on PATH, else the newest Proton in a Steam library
(its files/bin/wine; Proton is Valve's Wine, installed from Steam's Library > Tools).
Standard library only. From wgrender's tools/wine.py.
"""
import os
import re
import shutil
from pathlib import Path


def version_key(wine):
    """'Proton 11.0' after 'Proton 9.0'; Experimental and the like before numbers."""
    return [int(n) for n in re.findall(r'\d+', wine.parent.parent.parent.name)]


def find_wine():
    if os.environ.get('WINE'):
        return os.environ['WINE']
    for name in ('wine64', 'wine'):
        if shutil.which(name):
            return shutil.which(name)
    home = Path.home()
    libraries = [home / '.local' / 'share' / 'Steam', home / '.steam' / 'steam']
    folders = home / '.local' / 'share' / 'Steam' / 'steamapps' / 'libraryfolders.vdf'
    if folders.exists():
        libraries += [Path(p) for p in re.findall(r'"path"\s*"([^"]*)"', folders.read_text(errors='replace'))]
    found = {w for library in libraries for w in library.glob('steamapps/common/Proton*/files/bin/wine')
             if os.access(w, os.X_OK)}
    return str(max(found, key=version_key)) if found else None


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('wine.py: a module the other tools import: there is nothing to run')
