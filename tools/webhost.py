"""Linking libwgf's web host: libwgf for the web with no main, exporting its calls, as
tools/build_host.py and tools/check_binding.py build it, with the JS binding (wgf.js) every
JS program reaches it through beside it. Standard library only."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import examples  # noqa: E402
import jsbinding  # noqa: E402
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
FULL = ROOT / 'hosts' / 'web' / 'exports.json'

# A release host's optimization, and whether Closure minifies its JS (docs/HISTORY.md,
# "The web host's release flags"): measured on Asteroids' trimmed host.
OPTIMIZE = '-O2'
CLOSURE = True
EXTRA = []  # more link flags, for an audit (--profiling-funcs, -Wl,--Map)

# What the JS binding reaches in the module besides the calls: never trimmed.
RUNTIME = jsbinding.RUNTIME


def emcc():
    """Emscripten's emcc, found as the build finds it: through $EMSDK, else on PATH."""
    if os.environ.get('EMSDK'):
        for name in ('emcc', 'emcc.bat'):
            candidate = Path(os.environ['EMSDK']) / 'upstream' / 'emscripten' / name
            if candidate.exists():
                return str(candidate)
    found = shutil.which('emcc') or shutil.which('emcc.bat')
    if not found:
        raise RuntimeError('the web host needs Emscripten: no EMSDK, and no emcc on PATH')
    return found


def build(variant, exports_file=None, out=None, stage=True, constants=True, typed_used=None):
    """Link the host, the JS binding beside it (bindings/js/wgf.js: whole for the full
    host, else trimmed to the same calls, with its enums when `constants`), and the typed
    layer (wgf-typed.js: whole, or trimmed to the members `typed_used` names); the
    directory they are in."""
    if not variants.is_web(variant):
        raise RuntimeError(f'{variant} is not a web variant')
    if stage:
        examples.prepare(variant)
    staged = variants.out(variant)
    archive = staged / 'lib' / 'libwgf.a'
    if not archive.exists():
        raise RuntimeError(f'no staged libwgf in {staged}: run tools/stage_variant.py {variant}')
    listed = json.loads(Path(exports_file or FULL).read_text(encoding='utf-8'))['exports']
    linked = listed if exports_file is None else jsbinding.local_exports(listed)  # the full host keeps every call's C
    exports = sorted(set(linked) | set(jsbinding.LIBRARY) | set(jsbinding.RUN_CALLS))
    out = Path(out) if out else staged / 'host'
    out.mkdir(parents=True, exist_ok=True)
    work = Path(variants.work(variant)) / 'host'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'exports.txt').write_text('\n'.join(exports) + '\n', encoding='utf-8')
    headless = variants.is_headless(variant)
    release = variants.cache_variable(variant, 'CMAKE_BUILD_TYPE') == 'Release'
    # C++'s runtime, linked only for a host with physics in it (Jolt): em++ costs a host
    # without it 2 KB gzip of the runtime's own
    physics = any(e.lstrip('_').startswith(('wgf_physics_', 'wgf_body_', 'wgf_vehicle_')) for e in exports)
    linker = Path(emcc()).with_name('em++' + Path(emcc()).suffix) if physics else Path(emcc())
    command = [str(linker), str(archive), '-o', str(out / 'wgf-host.js'), '--no-entry',
               '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createWgfHost',
               f'-sEXPORTED_FUNCTIONS=@{(work / "exports.txt").as_posix()}',
               # a headless host's storage is the wasm's own: node's runner copies files in (FS)
               '-sEXPORTED_RUNTIME_METHODS=' + ','.join(RUNTIME + (['FS'] if headless else [])),
               '-sALLOW_TABLE_GROWTH=1', '-sALLOW_MEMORY_GROWTH=1',
               '-sSTACK_SIZE=524288',  # physics3d's Jolt (physics3d/CMakeLists.txt)
               OPTIMIZE if release else '-O0', *([] if release else ['-g']), *EXTRA]
    if headless:
        command += ['-sENVIRONMENT=node']
    else:
        command += ['-sMIN_WEBGL_VERSION=2', '-sMAX_WEBGL_VERSION=2', '-sENVIRONMENT=web,worker']
        if release and CLOSURE:
            command += ['--closure=1']  # the examples' release pages' flags (examples/c/wgf_example.cmake)
    done = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          errors='replace')
    if done.returncode != 0:
        raise RuntimeError('linking the host failed:\n' + '\n'.join(done.stdout.strip().splitlines()[-30:]))
    whole = (jsbinding.BINDING / 'wgf.js').read_text(encoding='utf-8')
    binding = whole if exports_file is None else jsbinding.trim(whole, listed, constants)
    (out / 'wgf.js').write_text(binding, encoding='utf-8', newline='\n')
    typed = (jsbinding.BINDING / f'{jsbinding.TYPED}.js').read_text(encoding='utf-8')
    if exports_file is not None:  # the typed layer beside it, cut to the members a program names, or none
        typed = jsbinding.trim_typed(typed, typed_used or set())
    (out / f'{jsbinding.TYPED}.js').write_text(typed, encoding='utf-8', newline='\n')
    return out


JS_EXAMPLES = ROOT / 'examples' / 'js'


def js_example_config(example):
    """A JS example's example.json, if it has one: its assets and its autopilot, each a
    path from the root (default: examples/assets, and none)."""
    path = Path(example) / 'example.json'
    config = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
    assets = ROOT / config.get('assets', 'examples/assets')
    autopilot = ROOT / config['autopilot'] if 'autopilot' in config else None
    return assets, autopilot


def build_js_example(example, variant, out, trimmed=False):
    """A JS example (examples/js/<name>/) as a site in `out`: its page and modules, the
    host and the JS binding beside them (with `trimmed`, both cut to the calls and enums
    its modules name, as a release export is), and its assets copied in. The directory."""
    example, out = Path(example), Path(out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    modules = sorted(example.glob('*.js'))
    for file in [*modules, example / 'index.html']:
        shutil.copyfile(file, out / file.name)
    listing = None
    if trimmed:
        text = ''.join(m.read_text(encoding='utf-8') for m in modules)
        calls, constants = jsbinding.names_in(text)
        typed_used, typed_calls = jsbinding.typed_names_in(
            text, (jsbinding.BINDING / f'{jsbinding.TYPED}.js').read_text(encoding='utf-8'))
        calls |= typed_calls
        known = set(json.loads(FULL.read_text(encoding='utf-8'))['exports'])
        listing = Path(variants.work(variant)) / 'host' / f'{example.name}-exports.json'
        listing.parent.mkdir(parents=True, exist_ok=True)
        listing.write_text(json.dumps({'exports': sorted({'_' + c for c in calls} & known)}, indent=1) + '\n',
                           encoding='utf-8')
        build(variant, listing, out, constants=constants, typed_used=typed_used)
    else:
        build(variant, None, out)
    assets, _ = js_example_config(example)
    shutil.copytree(assets, out / 'assets')
    return out


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('webhost.py: a module the other tools import: there is nothing to run')
