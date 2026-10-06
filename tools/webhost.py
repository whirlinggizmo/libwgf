"""Linking libwgf's web host: libwgf for the web with no main, exporting its calls, as
tools/build_host.py and tools/check_binding.py build it. Standard library only."""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
import examples  # noqa: E402
import variants  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
FULL = ROOT / 'hosts' / 'web' / 'exports.json'

# What the binding's runtime reaches in the module besides the calls (wgf/impl/Host.js.hx,
# wgf/Runtime.hx): never trimmed.
RUNTIME = ['addFunction', 'stackSave', 'stackRestore', 'stackAlloc', 'stringToUTF8', 'lengthBytesUTF8',
           'UTF8ToString', 'HEAPU8', 'HEAP32', 'HEAPF32']


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


def build(variant, exports_file=None, out=None, stage=True):
    """Link the host; the directory it is in."""
    if not variants.is_web(variant):
        raise RuntimeError(f'{variant} is not a web variant')
    if stage:
        examples.prepare(variant)
    staged = variants.out(variant)
    archive = staged / 'lib' / 'libwgf.a'
    if not archive.exists():
        raise RuntimeError(f'no staged libwgf in {staged}: run tools/stage_variant.py {variant}')
    listed = json.loads(Path(exports_file or FULL).read_text(encoding='utf-8'))['exports']
    exports = sorted(set(listed) | {'_malloc', '_free', '_wgf_version_get', '_wgf_version_get_major',
                                    '_wgf_version_get_minor', '_wgf_app_run'})
    out = Path(out) if out else staged / 'host'
    out.mkdir(parents=True, exist_ok=True)
    work = Path(variants.work(variant)) / 'host'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'exports.txt').write_text('\n'.join(exports) + '\n', encoding='utf-8')
    headless = variants.is_headless(variant)
    release = variants.cache_variable(variant, 'CMAKE_BUILD_TYPE') == 'Release'
    command = [emcc(), str(archive), '-o', str(out / 'wgf-host.js'), '--no-entry',
               '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createWgfHost',
               f'-sEXPORTED_FUNCTIONS=@{(work / "exports.txt").as_posix()}',
               # a headless host's storage is the wasm's own: node's runner copies files in (FS)
               '-sEXPORTED_RUNTIME_METHODS=' + ','.join(RUNTIME + (['FS'] if headless else [])),
               '-sALLOW_TABLE_GROWTH=1', '-sALLOW_MEMORY_GROWTH=1',
               '-O2' if release else '-O0', *([] if release else ['-g'])]
    if headless:
        command += ['-sENVIRONMENT=node']
    else:
        command += ['-sMIN_WEBGL_VERSION=2', '-sMAX_WEBGL_VERSION=2', '-sENVIRONMENT=web,worker']
    done = subprocess.run(command, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                          errors='replace')
    if done.returncode != 0:
        raise RuntimeError('linking the host failed:\n' + '\n'.join(done.stdout.strip().splitlines()[-30:]))
    return out


if __name__ == '__main__':
    import argparse
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                            epilog='A module the other tools import: there is nothing to run.').parse_args()
    raise SystemExit('webhost.py: a module the other tools import: there is nothing to run')
