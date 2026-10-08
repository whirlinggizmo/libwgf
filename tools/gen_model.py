#!/usr/bin/env python3
"""Generate the examples' glTF model: examples/assets/models/toy_car.glb.

    tools/gen_model.py [--check] [--out DIR]

A toy car of a few boxes and cylinders, its parts named glTF nodes so a program finds them
(wgf_actor_find: "toy_car/body/wheel_fl"): a body with a checker texture, a see-through cabin,
four wheels sharing one mesh, two emissive headlight lenses, and a spot light at each
(KHR_lights_punctual) shining forward, down +z. Made from these lines alone (tools/gltf.py),
the same bytes every time; --check makes it again and fails when the committed file differs.
Standard library only.
"""
import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gltf  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples' / 'assets' / 'models'
NAME = 'toy_car.glb'


def box(width, height, length, center=(0.0, 0.0, 0.0)):
    """A box's corners, normals, texture coordinates (0..1 a face), and triangles: each face
    its own four corners, wound counterclockwise seen from outside."""
    hx, hy, hz = width / 2, height / 2, length / 2
    cx, cy, cz = center
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    positions, normals, uvs, indices = [], [], [], []
    for n, u, v in faces:
        base = len(positions)
        for su, sv in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [n[i] + su * u[i] + sv * v[i] for i in range(3)]
            positions.append((cx + p[0] * hx, cy + p[1] * hy, cz + p[2] * hz))
            normals.append(n)
            uvs.append(((su + 1) / 2, (1 - sv) / 2))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


def wheel(radius, width, segments=20):
    """A cylinder about x (a wheel's axle), capped: its sides smooth, its caps flat."""
    positions, normals, uvs, indices = [], [], [], []
    hw = width / 2
    for i in range(segments + 1):  # the tread, its seam's corners twice for its texture coordinates
        a = 2 * math.pi * i / segments
        y, z = math.cos(a), math.sin(a)
        for x in (-hw, hw):
            positions.append((x, round(radius * y, 6), round(radius * z, 6)))
            normals.append((0.0, round(y, 6), round(z, 6)))
            uvs.append((i / segments, 0.0 if x < 0 else 1.0))
    for i in range(segments):
        a, b = 2 * i, 2 * i + 2
        indices += [a, b, a + 1, a + 1, b, b + 1]
    for side in (-1, 1):  # the caps: a center and a ring each
        center = len(positions)
        positions.append((side * hw, 0.0, 0.0))
        normals.append((float(side), 0.0, 0.0))
        uvs.append((0.5, 0.5))
        for i in range(segments):
            a = 2 * math.pi * i / segments
            positions.append((side * hw, round(radius * math.cos(a), 6), round(radius * math.sin(a), 6)))
            normals.append((float(side), 0.0, 0.0))
            uvs.append((0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a)))
        for i in range(segments):
            a, b = center + 1 + i, center + 1 + (i + 1) % segments
            indices += [center, a, b] if side > 0 else [center, b, a]
    return positions, normals, uvs, indices


def checker(size=8):
    """A light and a lighter square, alternating: the body's paint, so its texture shows."""
    light, lighter = (210, 210, 210, 255), (255, 255, 255, 255)
    return gltf.png(size, size, [[light if (x + y) % 2 else lighter for x in range(size)] for y in range(size)])


def toy_car():
    out = gltf.Gltf('libwgf tools/gen_model.py')
    paint = out.add_material('paint', color=(0.8, 0.05, 0.03, 1), metallic=0.1, roughness=0.4,
                             texture=out.add_image(checker(), nearest=True))
    glass = out.add_material('glass', color=(0.3, 0.5, 0.7, 0.45), roughness=0.1, blend=True)
    rubber = out.add_material('rubber', color=(0.03, 0.03, 0.03, 1), roughness=0.9)
    lamp = out.add_material('lamp', color=(1, 1, 0.9, 1), emissive=(1, 0.9, 0.6), emissive_strength=4.0)

    body_mesh = out.add_mesh('body', [(*box(1.8, 0.5, 4.0), paint)])
    cabin_mesh = out.add_mesh('cabin', [(*box(1.5, 0.45, 2.0), glass)])
    wheel_mesh = out.add_mesh('wheel', [(*wheel(0.35, 0.3), rubber)])
    lens_mesh = out.add_mesh('lens', [(*box(0.3, 0.12, 0.04), lamp)])
    headlight = out.add_light('spot', color=(1, 0.95, 0.8), intensity=30.0, range_=20.0, cone=(0.25, 0.5))

    root = out.add_node('toy_car')
    body = out.add_node('body', body_mesh, translation=(0, 0.6, 0), parent=root)
    out.add_node('cabin', cabin_mesh, translation=(0, 0.475, -0.3), parent=body)
    for name, x, z in (('wheel_fl', -0.9, 1.3), ('wheel_fr', 0.9, 1.3), ('wheel_rl', -0.9, -1.3),
                       ('wheel_rr', 0.9, -1.3)):
        out.add_node(name, wheel_mesh, translation=(x, -0.25, z), parent=body)
    for name, x in (('headlight_l', -0.6), ('headlight_r', 0.6)):
        lens = out.add_node(name, lens_mesh, translation=(x, 0.05, 2.02), parent=body)
        # a glTF light shines down its node's -z: turned a half turn about y, forward
        out.add_node(name + '_beam', rotation=gltf.quaternion((0, 1, 0), math.pi), light=headlight, parent=lens)
    return out.glb()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='fail when the committed file differs')
    parser.add_argument('--out', type=Path, default=OUT, help=f'where to write it (default: {OUT})')
    args = parser.parse_args()
    made = toy_car()
    path = args.out / NAME
    if args.check:
        if not path.exists() or path.read_bytes() != made:
            print(f'gen_model: {path} differs from what tools/gen_model.py makes: run it and commit the result')
            return 1
        print(f'gen_model: {NAME} is what tools/gen_model.py makes')
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    path.write_bytes(made)
    print(f'gen_model: wrote {path} ({len(made)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
