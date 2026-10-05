#!/usr/bin/env python3
"""Stage a built variant: its headers and archives, into out/, fresh.

    tools/stage_variant.py PRESET

Reads PRESET's build and install directories from CMakePresets.json, empties what
`cmake --install` writes in the install directory under out/ (all of it but the
programs built against it, in bin/ and site/), then runs `cmake --install` into it, so
a header that was renamed or removed doesn't linger there. Build the preset first.
Standard library only.
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROGRAMS = ('bin', 'site', 'assets')  # what programs built against a variant put beside it, kept


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('preset', help='a configure preset in CMakePresets.json, built already')
    name = parser.parse_args().preset
    if name not in variants.names():
        sys.exit(f'stage_variant: no preset "{name}" in CMakePresets.json')
    build = variants.field(name, 'binaryDir')
    install = variants.field(name, 'installDir')
    if build is None or install is None:
        sys.exit(f'stage_variant: preset "{name}" has no binaryDir or no installDir')
    build, install = Path(build).resolve(), Path(install).resolve()
    out = (ROOT / 'out').resolve()
    if out not in install.parents:
        sys.exit(f'stage_variant: {install} is not under {out}; not emptying it')
    if not (build / 'CMakeCache.txt').exists():
        sys.exit(f'stage_variant: {build} isn\'t built; run cmake --preset {name} and build it first')

    for entry in install.iterdir() if install.is_dir() else ():
        if entry.name in PROGRAMS:
            continue  # the examples', built against the staged variant (tools/examples.py)
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()
    config = variants.configuration(name)  # Visual Studio's builds several configurations: say which
    result = subprocess.run(['cmake', '--install', str(build), '--prefix', str(install),
                             *(['--config', config] if config else [])])
    if result.returncode != 0:
        return result.returncode
    print(f'stage_variant: {name} staged in {install.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
