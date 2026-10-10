#!/usr/bin/env python3
"""Compress textures for GPUs: for each PNG (or JPEG), write beside it

    name.bc7.ktx    BC7        desktops
    name.astc.ktx   ASTC 4x4   phones
    name.etc2.ktx   ETC2 RGBA  older phones

each with its mipmaps, KTX 1. A program loads "name.ktx" (wgf_texture_create) and libwgf
picks the file this GPU can sample, falling back to name.png: keep the PNG.

    tools/compress_textures.py [--linear] image.png...
    tools/compress_textures.py --gltf model.gltf

--linear: the images hold data, not colors (normal maps, roughness): no sRGB weighting
when compressing, and mipmaps averaged as they are.

--gltf: a model's textures. Every image its textures use is compressed as above (data
maps, normal, metallic-roughness and occlusion, as --linear; colors, base color and
emissive, as sRGB), and model.ktx.gltf written beside the model: the same model, each
texture given the WGR_texture_ktx extension pointing at its compressed image
("name.ktx"). libwgf loads the variant the GPU can sample, else the texture's own image;
other glTF viewers ignore the extension (it's in extensionsUsed, not extensionsRequired)
and use the original images. The model itself is left as it is. Only a .gltf whose
images are separate PNG or JPEG files: an image inside the file (a .glb, a data: URI, a
buffer view) is left uncompressed, with a note.

Encodes with Basis Universal (UASTC, level 2, then transcoded to each format), built the
first time from the pinned commit (BASISU_COMMIT, its 1.16.4 release) into the per-user
cache (tools/usercache.py: ~/.cache/libwgf/basisu-<commit>/ on Linux); that needs git,
CMake, and a C++ compiler. Only making the files needs it, never a build or a program.
wgrender's tools/compress_textures.py and compress_model_textures.py, as one tool.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from usercache import cache_dir  # noqa: E402

BASISU_REPOSITORY = 'https://github.com/BinomialLLC/basis_universal.git'
BASISU_COMMIT = '900e40fb5d2502927360fe2f31762bdbb624455f'  # the 1.16.4 tag, wgrender's
# transcoder format numbers (basist::transcoder_texture_format), the suffix, basisu's label
FORMATS = [(6, 'bc7', 'BC7_RGBA'), (10, 'astc', 'ASTC_RGBA'), (1, 'etc2', 'ETC2_RGBA')]
EXTENSION = 'WGR_texture_ktx'  # wgrender's name, so the files its tool wrote load as they are


def say(text):
    print(f'compress_textures: {text}', file=sys.stderr, flush=True)


def run(args, cwd=None):
    """Run a step of the build; its output shown only when it fails."""
    done = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if done.returncode != 0:
        say(f'{" ".join(str(a) for a in args)} failed:')
        print('\n'.join((done.stdout + done.stderr).splitlines()[-20:]), file=sys.stderr)
        sys.exit(1)
    return done.stdout


def basisu():
    """The basisu program, built from the pinned commit the first time."""
    root = cache_dir(f'basisu-{BASISU_COMMIT[:12]}')
    exe = 'basisu.exe' if os.name == 'nt' else 'basisu'
    source, build = root / 'source', root / 'build'

    def built():  # its CMake writes the program into the source's bin/ (as wgrender's tool found)
        return next((p for p in (source / 'bin' / exe, *sorted(build.rglob(exe))) if p.is_file()), None)

    if built() is not None and (build / '.done').is_file():
        return built()
    for tool in ('git', 'cmake'):
        if shutil.which(tool) is None:
            sys.exit(f'compress_textures: building basisu needs {tool}, which isn\'t on the PATH')
    say(f'building basisu {BASISU_COMMIT[:12]} (once) into {root}')
    for folder in (source, build):
        if folder.is_dir():
            shutil.rmtree(folder)  # a build cut short: start again
    source.mkdir()
    run(['git', 'init', '-q'], cwd=source)
    run(['git', 'fetch', '-q', '--depth', '1', BASISU_REPOSITORY, BASISU_COMMIT], cwd=source)
    run(['git', '-c', 'advice.detachedHead=false', 'checkout', '-q', 'FETCH_HEAD'], cwd=source)
    head = run(['git', 'rev-parse', 'HEAD'], cwd=source).strip()
    if head != BASISU_COMMIT:  # git checks the commit's contents against its hash
        sys.exit(f'compress_textures: fetched {head}, not the pinned {BASISU_COMMIT}')
    run(['cmake', '-S', str(source), '-B', str(build), '-DCMAKE_BUILD_TYPE=Release'])
    run(['cmake', '--build', str(build), '--config', 'Release', '--target', 'basisu', '--parallel'])
    if built() is None:
        sys.exit(f'compress_textures: built basisu, but found no {exe} under {source / "bin"} or {build}')
    (build / '.done').write_text(BASISU_COMMIT + '\n')
    return built()


def compress(tool, image, linear):
    """Write image's three variants beside it."""
    if image.suffix.lower() not in ('.png', '.jpg', '.jpeg'):
        sys.exit(f'compress_textures: {image}: not a .png or .jpg')
    if not image.is_file():
        sys.exit(f'compress_textures: {image}: no such file')
    name = image.stem
    with tempfile.TemporaryDirectory() as work:
        work = Path(work)
        shutil.copyfile(image, work / image.name)
        flags = ['-linear', '-mip_linear'] if linear else []
        run([str(tool), '-uastc', '-uastc_level', '2', '-mipmap', *flags, image.name], cwd=work)
        for number, suffix, label in FORMATS:
            run([str(tool), '-unpack', '-ktx_only', '-format_only', str(number), f'{name}.basis'], cwd=work)
            shutil.move(work / f'{name}_transcoded_{label}_0000.ktx', image.with_name(f'{name}.{suffix}.ktx'))
    sizes = ', '.join(f'{s} {image.with_name(f"{name}.{s}.ktx").stat().st_size}' for _, s, _ in FORMATS)
    print(f'{image}: {image.stat().st_size} bytes -> {sizes}')


def texture_uses(gltf):
    """texture index -> 'linear' or 'srgb', from how the materials use it (sRGB wins)."""
    uses = {}

    def note(info, kind):
        if isinstance(info, dict) and 'index' in info:
            prior = uses.get(info['index'])
            uses[info['index']] = 'srgb' if 'srgb' in (prior, kind) else 'linear'

    for material in gltf.get('materials', []):
        pbr = material.get('pbrMetallicRoughness', {})
        note(pbr.get('baseColorTexture'), 'srgb')
        note(material.get('emissiveTexture'), 'srgb')
        note(pbr.get('metallicRoughnessTexture'), 'linear')
        note(material.get('normalTexture'), 'linear')
        note(material.get('occlusionTexture'), 'linear')
    return uses


def compress_gltf(tool, path):
    """Compress a .gltf's textures' images and write model.ktx.gltf beside it."""
    if path.suffix.lower() != '.gltf':
        sys.exit(f'compress_textures: {path}: not a .gltf (a .glb holds its images inside: not yet)')
    gltf = json.loads(path.read_text(encoding='utf-8'))
    textures, images = gltf.get('textures', []), gltf.get('images', [])
    uses = texture_uses(gltf)
    image_kind = {}  # image index -> 'linear' or 'srgb' (sRGB wins when used both ways)
    for t, texture in enumerate(textures):
        source = texture.get('source')
        if source is None:
            continue
        kind = uses.get(t, 'srgb')
        image_kind[source] = 'srgb' if 'srgb' in (image_kind.get(source), kind) else kind

    compressed = {}  # an image's index -> its compressed image's
    for index, kind in sorted(image_kind.items()):
        uri = images[index].get('uri', '')
        stem, ext = os.path.splitext(uri)
        if images[index].get('bufferView') is not None or uri.startswith('data:') or ext.lower() not in (
                '.png', '.jpg', '.jpeg'):
            print(f'{path}: image {index} ({uri or "inside the file"}): not a separate PNG or JPEG, left as it is')
            continue
        compress(tool, path.parent / uri, kind == 'linear')
        compressed[index] = len(images)
        images.append({'uri': stem + '.ktx', 'name': images[index].get('name', Path(stem).name) + ' (KTX)'})

    for texture in textures:
        if texture.get('source') in compressed:
            texture.setdefault('extensions', {})[EXTENSION] = {'source': compressed[texture['source']]}
    if compressed:
        used = gltf.setdefault('extensionsUsed', [])
        if EXTENSION not in used:
            used.append(EXTENSION)
    out = path.with_name(path.name[:-len('.gltf')] + '.ktx.gltf')
    out.write_text(json.dumps(gltf, indent=1) + '\n', encoding='utf-8')
    print(f'{path}: {len(compressed)} image(s) compressed; wrote {out}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--linear', action='store_true', help='the images hold data, not colors')
    parser.add_argument('--gltf', metavar='MODEL', type=Path, help="a .gltf's textures, and model.ktx.gltf")
    parser.add_argument('images', nargs='*', type=Path, help='PNG or JPEG images')
    args = parser.parse_args()
    if args.gltf is not None and (args.images or args.linear):
        parser.error('--gltf takes the model alone (its materials say which images are data)')
    if args.gltf is None and not args.images:
        parser.error('give images, or --gltf and a model')
    tool = basisu()
    if args.gltf is not None:
        compress_gltf(tool, args.gltf.resolve())
        return
    for image in args.images:
        compress(tool, image.resolve(), args.linear)


if __name__ == '__main__':
    main()
