"""The C examples in examples/c/<layer>-<name>/, built against a staged variant, for
the tools that run them.

Each example is its own CMake project, built against out/ as a program outside libwgf
would be: the variant is built (cmake --build --preset), staged
(tools/stage_variant.py), then each example is configured and built, its work in
build/examples/<example>-<variant>/ and the program in the variant's programs
directory beside what it was built against: out/<platform>/<variant>/bin/, or on the
web out/wasm32/<variant>/site/ (the page, its script and wasm, and its bundled files).
Standard library only.
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
EXAMPLES = ROOT / 'examples' / 'c'


def names():
    """Every C example, by its directory's name."""
    return sorted(d.name for d in EXAMPLES.iterdir() if (d / 'CMakeLists.txt').exists())


def emscripten_toolchain():
    """Emscripten's CMake toolchain file, found as the top-level CMakeLists.txt finds it:
    through $EMSDK, else the emcc on PATH."""
    if os.environ.get('EMSDK'):
        emscripten = Path(os.environ['EMSDK']) / 'upstream' / 'emscripten'
    else:
        emcc = shutil.which('emcc') or shutil.which('emcc.bat')
        if not emcc:
            raise RuntimeError('a web variant needs Emscripten: no EMSDK, and no emcc on PATH')
        emscripten = Path(emcc).resolve().parent
    toolchain = emscripten / 'cmake' / 'Modules' / 'Platform' / 'Emscripten.cmake'
    if not toolchain.exists():
        raise RuntimeError(f'no Emscripten toolchain at {toolchain}')
    return toolchain


def _run(command, what):
    done = subprocess.run([str(c) for c in command], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if done.returncode != 0:
        output = '\n'.join(done.stdout.decode(errors='replace').strip().splitlines()[-20:])
        raise RuntimeError(f'{what} failed:\n{output}')


def finish(variant):
    """Finish a web variant's site after its examples are built (tools/finish_site.py):
    the versions stamped into the pages, examples.json, and the launcher."""
    if variants.is_web(variant):
        _run([sys.executable, ROOT / 'tools' / 'finish_site.py', '--site', variants.programs(variant)],
             f'finishing the site of {variant}')


def link_assets(assets):
    """examples/assets/ as `assets`, one level above the variant's programs (bin/, site/),
    where each names its host "../assets": a symbolic link, or a copy where this machine
    refuses links (Windows without developer mode), remade when it isn't the link."""
    source = ROOT / 'examples' / 'assets'
    if assets.is_symlink():
        if assets.resolve() == source.resolve():
            return
        assets.unlink()
    elif assets.is_dir():
        shutil.rmtree(assets)  # a copy from before: fresh
    try:
        os.symlink(source, assets, target_is_directory=True)
    except OSError:
        shutil.copytree(source, assets)


def prepare(variant):
    """Build the variant and stage it in out/, for its examples to build against."""
    if variant not in variants.names():
        raise RuntimeError(f'no variant "{variant}" in CMakePresets.json')
    if not (Path(variants.field(variant, 'binaryDir')) / 'CMakeCache.txt').exists():
        _run(['cmake', '--preset', variant], f'configuring {variant}')
    _run(['cmake', '--build', '--preset', variant], f'building {variant}')
    _run([sys.executable, ROOT / 'tools' / 'stage_variant.py', variant], f'staging {variant}')
    link_assets(Path(variants.field(variant, 'installDir')) / 'assets')
    # programs of examples that are gone (renamed, removed): never run or published again
    programs = variants.programs(variant)
    for path in programs.iterdir() if programs.is_dir() else ():
        if path.is_file() and path.name.split('.')[0] not in names():
            path.unlink()


def build(variant, example, defines=None, into=None):
    """Build one example against the staged variant: the path of what to run (the
    executable, or the web page). `defines` are more -D options for its configure
    (WGF_CHECK_EXPORTS, say). `into`: a directory of its own for the build and the
    program, so a build with other options (a full one, say) leaves the usual ones, and
    their cached options, as they were."""
    return build_at(variant, example, EXAMPLES / example, defines, into)


def build_at(variant, example, source, defines=None, into=None):
    """build(), for a program at `source` (a directory with a CMakeLists.txt that includes
    examples/c/wgf_example.cmake) named `example`. A web program is linked with a map
    beside it (<name>.map), for the size tools."""
    build_dir = Path(into) / 'build' if into else ROOT / 'build' / 'examples' / f'{example}-{variant}'
    programs = Path(into) if into else variants.programs(variant)
    defines = dict(defines or {})
    link_map = build_dir / f'{example}.map'
    if variants.is_web(variant) and 'CMAKE_EXE_LINKER_FLAGS' not in defines:
        defines['CMAKE_EXE_LINKER_FLAGS'] = f'-Wl,--Map={link_map}'
    config = variants.configuration(variant) or variants.cache_variable(variant, 'CMAKE_BUILD_TYPE') or 'Debug'
    msvc = '-msvc-' in variant  # Visual Studio's generator, as the variant's: a configuration per build
    configure = ['cmake', '-S', source, '-B', build_dir, f'-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={programs}',
                 f'-DWGF_OUT={variants.field(variant, "installDir")}',
                 f'-DWGF_HEADLESS={"ON" if variants.is_headless(variant) else "OFF"}',
                 f'-DCMAKE_BUILD_TYPE={config}']
    if msvc:  # a generator of several configurations puts each in a folder of its own, unless told
        configure += [f'-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_{config.upper()}={programs}', '-A', 'x64', f'-DCMAKE_MSVC_RUNTIME_LIBRARY={variants.cache_variable(variant, "CMAKE_MSVC_RUNTIME_LIBRARY")}']
    elif shutil.which('ninja'):
        configure += ['-G', 'Ninja']
    if variants.is_web(variant):
        configure.append(f'-DCMAKE_TOOLCHAIN_FILE={emscripten_toolchain()}')
    elif variants.field(variant, 'toolchainFile'):  # a cross build: MinGW-w64 on Linux, say
        configure.append(f'-DCMAKE_TOOLCHAIN_FILE={variants.field(variant, "toolchainFile")}')
    configure += [f'-D{name}={value}' for name, value in (defines or {}).items()]
    _run(configure, f'configuring {example}')  # each time: cheap with a cache, and the options always apply
    _run(['cmake', '--build', build_dir, '--config', config], f'building {example}')
    if variants.is_web(variant):
        for stale in (programs / f'{example}.html', programs / f'{example}.js', programs / f'{example}.wasm',
                      programs / f'{example}.data'):
            if stale.exists():
                stale.unlink()  # from before each example had a folder of the site
        return programs / example / 'index.html'
    return programs / (f'{example}.exe' if os.name == 'nt' or variant.startswith('windows-') else example)


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('examples.py: a module the other tools import: there is nothing to run')
