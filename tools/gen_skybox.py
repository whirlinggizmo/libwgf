#!/usr/bin/env python3
"""Generate examples/assets/textures/skybox.png: a sky drawn here, as a cubemap's 4 by 3 cross.

    tools/gen_skybox.py

A sky over a checkered plain, worked out along each texel's direction (OpenGL's cube
faces, as wgf_cubemap.h says them): a gradient from the horizon's haze to the zenith's blue,
a sun with its glow, bands of cloud, and the plain's squares fading into the haze, so the
seams between faces meet and the cube shows which way is which. Faces of 256 pixels laid
out as raylib's CUBEMAP_LAYOUT_CROSS_FOUR_BY_THREE (+Y above +Z; -X, +Z, +X, -Z across; -Y
below), the rest transparent: gfx-cubemap's sky and reflections. Its output is the same
every time (the PNG's bytes may differ with zlib's version, so there is no --check: it is made
once and committed). Standard library only.
"""
import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from gltf import png  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples' / 'assets' / 'textures' / 'skybox.png'
FACE = 256
# each face's place in the cross, in faces from its top-left: +X, -X, +Y, -Y, +Z, -Z
CELLS = [(2, 1), (0, 1), (1, 0), (1, 2), (1, 1), (3, 1)]
SUN = (0.55, 0.32, -0.77)
HAZE, ZENITH = (1.0, 0.82, 0.66), (0.16, 0.36, 0.78)
GRASS, LIGHT_GRASS = (0.20, 0.34, 0.16), (0.30, 0.46, 0.22)


def direction(face, s, t):
    """The unit direction of face `face` at (s, t), 0..1 from its top-left: GL's cube faces."""
    sc, tc = 2 * s - 1, 2 * t - 1
    d = [(1, -tc, -sc), (-1, -tc, sc), (sc, 1, tc), (sc, -1, -tc), (sc, -tc, 1), (-sc, -tc, -1)][face]
    n = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
    return d[0] / n, d[1] / n, d[2] / n


def mix(a, b, f):
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def sky(d):
    """The sky's color along unit direction `d`, 0..1 RGB."""
    x, y, z = d
    if y >= 0:
        color = mix(HAZE, ZENITH, min(1.0, y * 1.6) ** 0.7)
        cloud = math.sin(math.atan2(x, -z) * 7 + y * 9) * math.sin(math.atan2(x, -z) * 3 - y * 13)
        if 0.05 < y < 0.45 and cloud > 0.35:  # bands of cloud above the horizon
            color = mix(color, (1.0, 0.97, 0.94), min(1.0, (cloud - 0.35) * 3) * (1 - abs(y - 0.25) / 0.2) * 0.8)
    else:
        u, v = x / -y, z / -y  # where the direction meets the plain, one below
        square = (math.floor(u * 0.5) + math.floor(v * 0.5)) % 2
        color = mix(GRASS, LIGHT_GRASS, square)
        color = mix(color, HAZE, math.exp(y * 12))  # the plain fades into the haze toward the horizon
    sn = math.sqrt(sum(c * c for c in SUN))
    sun = (x * SUN[0] + y * SUN[1] + z * SUN[2]) / sn
    if sun > 0.9985:
        color = (1.0, 0.98, 0.9)
    elif y > -0.05:
        color = mix(color, (1.0, 0.9, 0.7), max(0.0, sun) ** 64 * 0.8)
    return color


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    width, height = FACE * 4, FACE * 3
    rows = [[(0, 0, 0, 0)] * width for _ in range(height)]
    for face, (column, row) in enumerate(CELLS):
        for y in range(FACE):
            for x in range(FACE):
                color = sky(direction(face, (x + 0.5) / FACE, (y + 0.5) / FACE))
                srgb = tuple(max(0, min(255, round(c * 255))) for c in color)
                rows[row * FACE + y][column * FACE + x] = srgb + (255,)
    OUT.write_bytes(png(width, height, rows))
    print(f'gen_skybox: wrote {OUT.relative_to(ROOT)} ({width} by {height}, faces of {FACE})')


if __name__ == '__main__':
    main()
