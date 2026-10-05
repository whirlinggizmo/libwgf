"""The variants, as CMakePresets.json defines them: one configure preset each, named
<platform>-<config>[-<feature>]. For the tools that build or stage one, so none spells
a preset or a directory itself.

    from variants import native, web, out, programs, work, preset_of
    native('debug-headless')      # 'linux-x64-debug-headless' on Linux
    web(debug=False)              # 'wasm32-release'
    out('wasm32-debug')           # what it makes: the preset's installDir
    programs('wasm32-debug')      # its programs: out/wasm32/debug/site
    work('wasm32-debug')          # its work: the preset's binaryDir

A preset's fields follow its `inherits` chain, with ${sourceDir} and ${presetName}
filled in; a name these compose is refused unless the presets define it. Standard
library only.
"""
import json
import platform
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def _all():
    return json.loads((ROOT / 'CMakePresets.json').read_text())['configurePresets']


def names():
    """The variants: the presets a person can pick, the ones not hidden."""
    return [p['name'] for p in _all() if not p.get('hidden')]


def _chain(name):
    """The preset and those it inherits, nearest first."""
    by_name = {p['name']: p for p in _all()}
    todo, seen, chain = [name], set(), []
    while todo:
        current = todo.pop(0)
        if current in seen or current not in by_name:
            continue
        seen.add(current)
        chain.append(by_name[current])
        inherits = by_name[current].get('inherits', [])
        todo.extend([inherits] if isinstance(inherits, str) else inherits)
    return chain


def field(name, key):
    """A field, such as binaryDir or installDir; None when no preset in the chain sets it."""
    for preset in _chain(name):
        if key in preset:
            return preset[key].replace('${sourceDir}', str(ROOT)).replace('${presetName}', name)
    return None


def cache_variable(name, variable):
    """A cache variable the preset sets, as text; None when none in the chain does."""
    for preset in _chain(name):
        value = preset.get('cacheVariables', {}).get(variable)
        if value is not None:
            return value['value'] if isinstance(value, dict) else str(value)
    return None


# this machine's platform, as the presets name it (MSVC's on Windows)
HOST = {'Linux': 'linux-x64', 'Darwin': 'macos-arm64', 'Windows': 'windows-x64-msvc'}.get(platform.system(), 'linux-x64')


def _defined(name):
    if name not in names():
        raise ValueError(f'{name}: no such variant in CMakePresets.json')
    return name


def native(variant='debug'):
    """This machine's preset for a variant: debug, release, debug-headless, debug-tsan..."""
    return _defined(f'{HOST}-{variant}')


def web(debug=True):
    """The web preset, debug or release."""
    return _defined('wasm32-' + ('debug' if debug else 'release'))


def out(name):
    """What a variant makes, staged: its installDir, out/<platform>/<variant>/."""
    return Path(field(_defined(name), 'installDir'))


def programs(name):
    """Where the programs built against a variant go: out/<platform>/<variant>/bin, or for
    the web its site, out/wasm32/<variant>/site."""
    return out(name) / ('site' if is_web(name) else 'bin')


def work(name):
    """A variant's work, CMake's cache and objects: its binaryDir, build/<preset>/."""
    return Path(field(_defined(name), 'binaryDir'))


def preset_of(path):
    """The variant whose out() or work() a directory is, or is under; None for none."""
    path = Path(path).resolve()
    if path.name in ('bin', 'site', 'lib', 'include'):
        path = path.parent
    for name in names():
        for where in (field(name, 'installDir'), field(name, 'binaryDir')):
            if where and (path == Path(where).resolve() or Path(where).resolve() in path.parents):
                return name
    return None


def configuration(name):
    """The configuration a variant's build preset builds (Debug), for a generator that
    builds several in one directory (Visual Studio's); None when it names none."""
    presets = json.loads((ROOT / 'CMakePresets.json').read_text()).get('buildPresets', [])
    return next((b.get('configuration') for b in presets if b.get('configurePreset') == name), None)


def is_web(name):
    return cache_variable(name, 'WGF_WEB') == 'ON'


def is_headless(name):
    return cache_variable(name, 'WGF_HEADLESS') == 'ON'


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('variants.py: a module the other tools import: there is nothing to run')
