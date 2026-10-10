#!/usr/bin/env python3
"""Write the examples' generated sky, examples/assets/environments/sky.hdr.

    tools/gen_sky.py [--check]

A 1024 by 512 equirectangular Radiance .hdr (RGBE, run-length encoded) of a clear
afternoon: a sky from a pale horizon to a deep blue zenith, a sun 25 degrees up toward -z
and +x with a glow about it and a disc far brighter than the sky (what a smooth metal
mirrors as a hard highlight), and a warm grey ground below the horizon darkening away from
it. Linear radiance; the equirectangular convention gfx's environments read (u 0.5 looks
toward -z, u growing toward +x, v 0 straight up). An environment to light and back the
examples that has no third party: wgf_environment_create reads it as it reads a photograph.
The same bytes every run on one machine (its math is the C library's, which may differ in a
last digit elsewhere); --check writes nothing and fails when the committed file differs.
Standard library only.
"""
import argparse
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples' / 'assets' / 'environments' / 'sky.hdr'
WIDTH, HEIGHT = 1024, 512
SUN_ELEVATION = math.radians(25.0)
SUN_AZIMUTH = math.radians(40.0)  # from -z toward +x
SUN_RADIUS = math.radians(1.2)    # wider than the real one: a few texels at this size
SUN_RADIANCE = (900.0, 820.0, 700.0)
ZENITH = (0.10, 0.22, 0.55)
HORIZON = (0.75, 0.82, 0.92)
GROUND = (0.22, 0.20, 0.17)


def direction(u, v):
    phi = (u - 0.5) * 2.0 * math.pi
    theta = v * math.pi
    return math.sin(theta) * math.sin(phi), math.cos(theta), -math.sin(theta) * math.cos(phi)


SUN = (math.cos(SUN_ELEVATION) * math.sin(SUN_AZIMUTH), math.sin(SUN_ELEVATION),
       -math.cos(SUN_ELEVATION) * math.cos(SUN_AZIMUTH))


def radiance(d):
    x, y, z = d
    cos_sun = x * SUN[0] + y * SUN[1] + z * SUN[2]
    angle = math.acos(max(-1.0, min(1.0, cos_sun)))
    if y >= 0.0:
        t = (1.0 - y) ** 4  # 1 at the horizon, 0 straight up
        sky = [ZENITH[c] + (HORIZON[c] - ZENITH[c]) * t for c in range(3)]
        glow = 4.0 * math.exp(-angle / 0.08) + 0.6 * math.exp(-angle / 0.5)
        sky = [sky[c] + glow * (1.0, 0.85, 0.6)[c] for c in range(3)]
        if angle < SUN_RADIUS:
            return SUN_RADIANCE
        return sky
    fade = math.exp(y * 6.0)  # y < 0: darker away from the horizon
    return [GROUND[c] * (0.35 + 0.65 * fade) + HORIZON[c] * 0.15 * fade for c in range(3)]


def rgbe(color):
    top = max(color)
    if top < 1e-32:
        return 0, 0, 0, 0
    mantissa, exponent = math.frexp(top)
    scale = mantissa * 256.0 / top
    return int(color[0] * scale), int(color[1] * scale), int(color[2] * scale), exponent + 128


def encode_channel(values):
    """One scanline channel, Radiance's run-length encoding: runs of 3 or more equal bytes
    as (128 + n, byte), the rest as (n, bytes...), n at most 127."""
    out = bytearray()
    i, n = 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 127 and values[i + run] == values[i]:
            run += 1
        if run >= 3:
            out += bytes((128 + run, values[i]))
            i += run
            continue
        start = i
        while i < n and i - start < 127:
            if i + 2 < n and values[i] == values[i + 1] == values[i + 2]:
                break
            i += 1
        out.append(i - start)
        out += bytes(values[start:i])
    return out


def sky():
    data = bytearray(f'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {HEIGHT} +X {WIDTH}\n'.encode('ascii'))
    for py in range(HEIGHT):
        row = [rgbe(radiance(direction((px + 0.5) / WIDTH, (py + 0.5) / HEIGHT))) for px in range(WIDTH)]
        data += bytes((2, 2, WIDTH >> 8, WIDTH & 0xFF))
        for c in range(4):
            data += encode_channel([p[c] for p in row])
    return bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help='write nothing; fail when the committed file differs')
    args = ap.parse_args()
    data = sky()
    if args.check:
        if not OUT.exists() or OUT.read_bytes() != data:
            print(f'gen_sky: stale: {OUT.relative_to(ROOT).as_posix()} (run tools/gen_sky.py)')
            return 1
        print('gen_sky: the sky is current')
        return 0
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(data)
    print(f'gen_sky: wrote {OUT.relative_to(ROOT).as_posix()} ({len(data)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
