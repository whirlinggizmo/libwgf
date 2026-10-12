#!/usr/bin/env python3
"""Generate examples/assets/textures/noise.png: tileable value noise, four octaves, 128 by
128, gray (the dissolve shader's, examples/shaders/dissolve.glsl).

    tools/gen_noise.py

Made from a fixed seed, so the same pixels come out every time (the file's bytes may
differ with zlib's version, so there is no --check: it is made once and committed).
wgrender's tools/gen_noise_texture.py (libwgt's noise.png), written again on
tools/gltf.py's PNG writer. Standard library only.
"""
import argparse
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from gltf import png  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples' / 'assets' / 'textures' / 'noise.png'
SIZE = 128
OCTAVES = ((4, 0.5), (8, 0.25), (16, 0.15), (32, 0.1))  # lattice cells across, weight


def octave(cells, rng):
    """Smooth noise over a lattice of `cells` random values each way, wrapping at the edges."""
    grid = [[rng.random() for _ in range(cells)] for _ in range(cells)]

    def at(x, y):
        fx, fy = x * cells / SIZE, y * cells / SIZE
        x0, y0 = int(fx) % cells, int(fy) % cells
        x1, y1 = (x0 + 1) % cells, (y0 + 1) % cells
        tx, ty = fx - int(fx), fy - int(fy)
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        top = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * tx
        bottom = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * tx
        return top + (bottom - top) * ty

    return at


def noise_png():
    """The image's bytes."""
    rng = random.Random(7)
    octaves = [(octave(cells, rng), weight) for cells, weight in OCTAVES]
    values = [[sum(f(x, y) * w for f, w in octaves) for x in range(SIZE)] for y in range(SIZE)]
    low, high = min(min(r) for r in values), max(max(r) for r in values)
    rows = []
    for row in values:
        grays = (max(0, min(255, round((v - low) / (high - low) * 255))) for v in row)  # the full range
        rows.append([(g, g, g, 255) for g in grays])
    return png(SIZE, SIZE, rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    data = noise_png()
    OUT.write_bytes(data)
    print(f'gen_noise: {OUT.relative_to(ROOT)} ({len(data)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
