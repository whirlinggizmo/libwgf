#!/usr/bin/env python3
"""Write the track's pieces, assets/kit/*.glb, from Kenney's Racing Kit (CC0, version 2.0,
https://kenney.nl/assets/racing-kit): the pieces tools/gen_track.py lays, each its kit
file with its triangles as they are and three things changed, the same bytes every time.

    python3 tools/gen_kit.py KIT.zip [--check]

KIT.zip is the kit's download (kenney_racing-kit.zip, checked by its sha256). --check:
write nothing, fail when a piece is stale.

What changes, in each file's JSON alone (its binary chunk is the kit's):

    the node    its translation dropped (the kit's (-0.35, -0.01, -0.65), an export's
                leftover), so a piece's own coordinates are the file's: gen_track places
                each by the corner of its tile, as the kit's preview lays them
    materials   each color, written for a renderer that showed it as it is (the kit's own
                look is unlit), taken from sRGB to the linear factor glTF means; its
                roughness, so the sun lights it as what it is; no metal. Written out
                below, not computed, since pow() is the C library's (gen_track's rule)
    unlit       KHR_materials_unlit, which the files list as used and no material uses,
                left out

Standard library only.
"""
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
OUT = GAME / 'assets' / 'kit'
KIT_SHA256 = '8a71ea16219315a01d00d5a90c4f6b5c090faddbc56d80ecf727e2b3b853c6c0'

PIECES = ['roadStraight', 'roadStraightLong', 'roadStartPositions', 'roadCornerLarge', 'roadCornerLarger',
          'barrierWall', 'overhead', 'treeLarge', 'treeSmall',
          # the trackside props
          'pylon', 'flagRed', 'flagGreen', 'flagCheckers', 'bannerTowerRed', 'bannerTowerGreen', 'lightPostLarge',
          'grandStand']

# the kit's material, its base color as the files give it (sRGB in intent), and what is
# written: the linear factor and a roughness
MATERIALS = {
    'grass': ((0.074214, 0.274677, 0.155926), 0.95),
    'grey': ((0.879622, 0.887923, 0.921582), 0.7),
    '_defaultMat': ((0.879622, 0.887923, 0.921582), 0.7),  # roadCornerLarger's curbs, white there
    'road': ((0.057805, 0.057805, 0.057805), 0.85),
    'red': ((0.806952, 0.090842, 0.086500), 0.6),
    'bark': ((0.658375, 0.386429, 0.149960), 0.9),
    'pylon': ((0.913099, 0.485150, 0.054480), 0.6),  # the cones' orange, and the light posts' lamps
    'checkers': ((1.0, 1.0, 1.0), 0.8),  # its texture's (the kit's PNG, kept)
}


def piece(data):
    """The kit's .glb bytes, changed as above."""
    magic, version, _ = struct.unpack_from('<III', data, 0)
    assert magic == 0x46546C67 and version == 2
    json_len, json_kind = struct.unpack_from('<II', data, 12)
    assert json_kind == 0x4E4F534A
    gltf = json.loads(data[20:20 + json_len])
    binary = data[20 + json_len:]
    for node in gltf['nodes']:
        node.pop('translation', None)
        if node.get('name', '').endswith('(Clone)'):
            node['name'] = node['name'][:-len('(Clone)')]
    for m in gltf['materials']:
        color, roughness = MATERIALS[m['name']]
        pbr = m.setdefault('pbrMetallicRoughness', {})
        pbr['baseColorFactor'] = [color[0], color[1], color[2], 1.0]
        pbr['metallicFactor'] = 0.0
        pbr['roughnessFactor'] = roughness
        m.pop('extensions', None)
    used = [e for e in gltf.get('extensionsUsed', []) if e != 'KHR_materials_unlit']
    if used:
        gltf['extensionsUsed'] = used
    else:
        gltf.pop('extensionsUsed', None)
    text = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    while len(text) % 4:
        text += b' '
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(text) + len(binary))
    return out + struct.pack('<II', len(text), 0x4E4F534A) + text + binary


def main():
    args = [a for a in sys.argv[1:] if a != '--check']
    check = '--check' in sys.argv[1:]
    if len(args) != 1:
        print(__doc__)
        return 2
    kit = Path(args[0]).read_bytes()
    if hashlib.sha256(kit).hexdigest() != KIT_SHA256:
        print('gen_kit: not the Racing Kit this was written for (its sha256 differs)')
        return 1
    stale = []
    with zipfile.ZipFile(args[0]) as z:
        for name in PIECES:
            data = piece(z.read(f'Models/GLTF format/{name}.glb'))
            path = OUT / f'{name}.glb'
            if path.exists() and path.read_bytes() == data:
                continue
            if check:
                stale.append(path)
            else:
                OUT.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                print(f'gen_kit: wrote {path.relative_to(GAME)} ({len(data)} bytes)')
    if stale:
        print('gen_kit: stale: ' + ', '.join(str(p.relative_to(GAME)) for p in stale))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
