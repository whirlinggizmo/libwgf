#!/usr/bin/env python3
"""Check for, or install, the system packages a Linux desktop build links.

    tools/setup_system_packages.py [check]    name the missing dev packages; exit 1 if any
    tools/setup_system_packages.py install    install them with apt, dnf, or pacman (sudo)

sokol links the system's OpenGL, X11, and ALSA (audio's device) libraries on Linux, which can't be vendored
under deps/: their development packages come from the system's package manager. They
are found through pkg-config. Configuring a Linux desktop preset runs the check, so a
missing one stops the build there, with the command that installs it, rather than at
the link. Windows and macOS need nothing beyond their compiler, and a headless or web
build links none of these, so there it does nothing. Standard library only. From
wgrender's tools/setup_system_packages.py.
"""
import argparse
import platform
import shutil
import subprocess
import sys

# the pkg-config modules platform links on Linux (libwgf/platform/CMakeLists.txt), and audio's (alsa)
MODULES = ['gl', 'x11', 'xi', 'xcursor', 'xrandr', 'alsa']

PACKAGES = {
    'apt': {'pkg-config': 'pkg-config', 'gl': 'libgl-dev', 'x11': 'libx11-dev', 'xi': 'libxi-dev',
            'xcursor': 'libxcursor-dev', 'xrandr': 'libxrandr-dev', 'alsa': 'libasound2-dev'},
    'dnf': {'pkg-config': 'pkgconf-pkg-config', 'gl': 'mesa-libGL-devel', 'x11': 'libX11-devel',
            'xi': 'libXi-devel', 'xcursor': 'libXcursor-devel', 'xrandr': 'libXrandr-devel', 'alsa': 'alsa-lib-devel'},
    'pacman': {'pkg-config': 'pkgconf', 'gl': 'libglvnd', 'x11': 'libx11', 'xi': 'libxi',
               'xcursor': 'libxcursor', 'xrandr': 'libxrandr', 'alsa': 'alsa-lib'},
}
INSTALL = {
    'apt': ['sudo', 'apt-get', 'install', '-y'],
    'dnf': ['sudo', 'dnf', 'install', '-y'],
    'pacman': ['sudo', 'pacman', '-S', '--needed', '--noconfirm'],
}
HINT = {'apt': 'sudo apt install', 'dnf': 'sudo dnf install', 'pacman': 'sudo pacman -S --needed'}


def say(text):
    print(f'setup_system_packages: {text}', file=sys.stderr, flush=True)


def package_manager():
    for manager, program in (('apt', 'apt-get'), ('dnf', 'dnf'), ('pacman', 'pacman')):
        if shutil.which(program):
            return manager
    return None


def missing():
    """The pkg-config modules not found, or pkg-config itself and all of them."""
    if not shutil.which('pkg-config'):
        return ['pkg-config', *MODULES]
    return [m for m in MODULES if subprocess.run(['pkg-config', '--exists', m]).returncode != 0]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', nargs='?', choices=('check', 'install'), default='check')
    mode = parser.parse_args().mode
    if platform.system() != 'Linux':
        return 0
    absent = missing()
    manager = package_manager()
    packages = [PACKAGES[manager][m] for m in absent] if manager else []
    if not absent:
        if mode == 'install':
            say('every package the Linux desktop build needs is installed')
        return 0
    if mode == 'check':
        say(f'missing for the Linux desktop build: {" ".join(absent)}')
        if manager:
            print(f'  install them: {HINT[manager]} {" ".join(packages)}', file=sys.stderr)
        else:
            print(f'  install the development packages for: {" ".join(absent)}', file=sys.stderr)
        print('  or run: python3 tools/setup_system_packages.py install', file=sys.stderr)
        return 1
    if not manager:
        say(f'no apt, dnf, or pacman here; install the development packages for: {" ".join(absent)}')
        return 1
    say(f'installing {" ".join(packages)}')
    return subprocess.run([*INSTALL[manager], *packages]).returncode


if __name__ == '__main__':
    sys.exit(main())
