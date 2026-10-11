#!/usr/bin/env python3
"""Write the racer's track from its layout below, the same bytes every time (--check: write
nothing, fail when a file is stale):

    python3 tools/gen_track.py [--check]

The layout is a loop of Kenney's Racing Kit pieces (assets/kit/, tools/gen_kit.py) on the
kit's grid, TILE metres a tile: straights and corners, the way the kit's preview lays them.
From it:

    assets/track/section_<n>.scene   the pieces of each of SECTIONS stretches of the loop,
                                     what is streamed in during the race: its road pieces,
                                     the barriers along it, the gates over it, and the trees
                                     beside it, each a model of a kit file placed on the grid
    assets/track/collision.glb       the whole loop's collision, one file, loaded first:
      ground                         the grass, 800 m square (drawn: it is the field)
      road                           the road and its shoulders, curb to tile edge
      walls                          the barriers, taller than they are drawn, and the gates'
                                     posts (road and walls aren't drawn: the game hides them)
    assets/scenes/track.scene        the collision file placed, its bodies, and the
                                     checkpoints' sensors at the gates
    src/TrackData.hx                 the centerline every STEP metres (what the game drives
                                     by: the grid, the grass, recovery, the driver's
                                     telemetry), the checkpoints' samples, and each
                                     section's first sample and files, in streaming order
    assets/manifest.json (and one    the asset manifests (libwgf's tools/gen_manifest.py),
      in each directory under it)    so a returning browser reads every file from its cache

A section's files are its scene and the kit pieces first placed in it, so each piece's
file is fetched once, with the first section that places it. Standard library only (and
libwgf's gen_manifest.py, run as a program).
"""
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
LIBWGF = Path(os.environ.get('LIBWGF', Path.home() / 'projects/github/whirlinggizmo/libwgf'))


# ---- arithmetic the same on every system ---------------------------------------------
# The track is the same bytes everywhere only if every number in it is: +, -, *, /, and
# sqrt are IEEE's, rounded alike on every machine, but sin, cos, atan2, hypot, and pow are
# the C library's, whose last digit differs between glibc and Windows' CRT. So the
# trigonometry is ours, from those alone (a reduction by fdlibm's split pi/2, then series
# accurate to about an ulp on the reduced range), and squares are products.

PI = 3.141592653589793
PIO2_HI = 1.5707963267341256  # pi/2 = PIO2_HI + PIO2_LO (fdlibm's split)
PIO2_LO = 6.077100506506192e-11


def _sin_series(r):
    r2 = r * r
    term, total = r, r
    for k in range(1, 12):
        term = -term * r2 / ((2 * k) * (2 * k + 1))
        total += term
    return total


def _cos_series(r):
    r2 = r * r
    term, total = 1.0, 1.0
    for k in range(1, 12):
        term = -term * r2 / ((2 * k - 1) * (2 * k))
        total += term
    return total


def _reduce(x):
    k = int(round(x / (PIO2_HI + PIO2_LO)))
    return (x - k * PIO2_HI) - k * PIO2_LO, k % 4


def sin(x):
    r, q = _reduce(x)
    return (_sin_series(r), _cos_series(r), -_sin_series(r), -_cos_series(r))[q]


def cos(x):
    r, q = _reduce(x)
    return (_cos_series(r), -_sin_series(r), -_cos_series(r), _sin_series(r))[q]


def _atan(t):
    """atan(t) for t >= 0: reduced below tan(pi/8), then its series."""
    if t > 1.0:
        return (PIO2_HI + PIO2_LO) - _atan(1.0 / t)
    if t > 0.41421356237309503:  # tan(pi/8): atan(t) = pi/4 + atan((t - 1) / (t + 1))
        return (PIO2_HI + PIO2_LO) / 2 + _atan_small((t - 1.0) / (t + 1.0))
    return _atan_small(t)


def _atan_small(t):
    t2 = t * t
    power, total = t, t
    for k in range(1, 40):
        power = -power * t2
        total += power / (2 * k + 1)
    return total


def atan2(y, x):
    if x == 0.0 and y == 0.0:
        return 0.0
    if abs(x) >= abs(y):
        a = _atan(abs(y) / abs(x))
        a = a if x > 0 else PI - a
    else:
        t = _atan(abs(x) / abs(y))
        a = (PIO2_HI + PIO2_LO) - t if x >= 0 else (PIO2_HI + PIO2_LO) + t
    return a if y >= 0 else -a


def length(dx, dz):
    return math.sqrt(dx * dx + dz * dz)


def wrap(a):
    while a > PI:
        a -= 2 * PI
    while a <= -PI:
        a += 2 * PI
    return a


# ---- the layout --------------------------------------------------------------------
# Headings are yaws: the way (sin h, cos h) on x and z, 0 along +z. Facing +z, +x is the
# left (the car's convention), so a left turn adds to the heading.

TILE = 16.0  # m a kit tile
ROAD_SY = 4.0  # the road pieces' height scale: the kit's 0.01-tile road a 4 cm step
# The loop from the grid's straight, heading +z: S<n> a straight of n tiles; L<k>, R<k> a
# corner turning left or right through a k-by-k block of tiles (the kit's large, 2, and
# larger, 3: radii of 1.5 and 2.5 tiles to the centerline).
LAYOUT = ['S6', 'L3', 'S3', 'L2', 'R2', 'S1', 'L3', 'S4', 'L3', 'S3', 'R2', 'L2', 'S1', 'L3', 'S4']
FIRST = 'roadStartPositions'  # the grid's piece, the first two tiles of the first straight
CORNERS = {2: 'roadCornerLarge', 3: 'roadCornerLarger'}

STEP = 2.0  # m between the centerline's samples
CHECKPOINTS = 8
FIRST_CHECKPOINT = 16  # samples up the first straight: past the grid's piece, under gate 0
SECTIONS = 8

WIDTH = 0.69 * TILE  # the asphalt (the kit's 0.155 to 0.845 of a tile)
CURB_HALF = 0.418 * TILE  # to the curb's outer edge
ROAD_Y = 0.01 * ROAD_SY  # the road's top
GROUND_Y = -0.03  # below the pieces' grass (at 0), so the two never fight
GROUND = 800.0

BARRIER_OFFSET = 12.0  # from the centerline to the barriers' middle
BARRIER_THICK = 0.5  # drawn: the kit's wall, scaled
BARRIER_HEIGHT = 0.9
WALL_THICK = 0.7  # collision: thicker, and much taller, so a car rolling at it can't go over
WALL_HEIGHT = 2.6
STRAIGHT_RUN = 8  # samples a barrier piece spans on a straight (16 m)
CURVED_RUN = 4  # and on a curve, its chords short enough to keep near the arc

GATE_SCALE = (TILE, 12.0, TILE)  # the kit's overhead, 1.26 tiles across, 8 m up
GATE_POST = 9.04  # its posts' middles from the centerline (the kit's -0.065 and 1.065 of a tile)

TREES = 90
TREE_CLEAR = BARRIER_OFFSET + 5.0

# the collision file's colors (glTF linear): its ground is drawn, the kit's grass
GRASS = (0.074214, 0.274677, 0.155926)


def pieces_and_centerline():
    """Walk the layout: each piece placed ((file, position, rotation y, scale), its start along
    the loop in m), and the segments the centerline is sampled from."""
    x, z, h = 0.0, 0.0, 0.0
    placed, segments = [], []
    along = 0.0
    for step in LAYOUT:
        kind, n = step[0], int(step[1:])
        if kind == 'S':
            tiles = n
            files = []
            if not placed:
                files.append((FIRST, 2))
                tiles -= 2
            files += [('roadStraightLong', 2)] * (tiles // 2) + [('roadStraight', 1)] * (tiles % 2)
            for name, t in files:
                # the kit's straight runs -z from its entry edge, x 0 to 1 across: turned by h
                # + pi, its entry edge's middle (0.5, 0) on (x, z)
                rot = wrap(h + PI)
                px, pz = local_to_world(x, z, rot, 0.5 * TILE, 0.0)
                placed.append(((name, (px, 0.0, pz), rot, (TILE, ROAD_SY, TILE)), along))
                segments.append(('line', x, z, h, t * TILE))
                x, z = x + t * TILE * sin(h), z + t * TILE * cos(h)
                along += t * TILE
        else:
            k = n
            radius = (k - 0.5) * TILE
            name = CORNERS[k]
            if kind == 'R':
                # the kit's corner, entered at its z = 0 edge (x 0.5) running -z, turns right
                # to leave its x = k edge: turned as a straight is
                rot = wrap(h + PI)
                px, pz = local_to_world(x, z, rot, 0.5 * TILE, 0.0)
                sign = -1
            else:
                # entered from its other end (x = k, z = -k + 0.5, running -x) it turns left
                rot = wrap(h + PI / 2)
                px, pz = local_to_world(x, z, rot, k * TILE, -(k - 0.5) * TILE)
                sign = 1
            placed.append(((name, (px, 0.0, pz), rot, (TILE, ROAD_SY, TILE)), along))
            # the arc: its center to the side it turns to (left is (cos h, -sin h))
            cx, cz = x + sign * radius * cos(h), z - sign * radius * sin(h)
            segments.append(('arc', cx, cz, h, radius, sign))
            h2 = h + sign * PI / 2
            x, z = cx - sign * radius * cos(h2), cz + sign * radius * sin(h2)
            h = wrap(h2)
            along += radius * PI / 2
    assert abs(x) < 1e-6 and abs(z) < 1e-6 and abs(h) < 1e-9, f'the layout does not close: {x}, {z}, {h}'
    return placed, segments, along


def local_to_world(x, z, rot, lx, lz):
    """Where a piece turned `rot` about y goes so that its own point (lx, lz) is at (x, z)."""
    c, s = cos(rot), sin(rot)
    return x - (lx * c + lz * s), z - (-lx * s + lz * c)


def centerline(segments, total):
    """Samples every STEP metres around the loop: (xs, zs, headings)."""
    xs, zs, hs = [], [], []
    count = int(total / STEP)
    if total - count * STEP < STEP * 0.5:
        count -= 1  # the last sample too near the first
    k = 0
    start = 0.0
    for seg in segments:
        seg_len = seg[4] if seg[0] == 'line' else seg[4] * PI / 2
        while k <= count and k * STEP < start + seg_len - 1e-9:
            d = k * STEP - start
            if seg[0] == 'line':
                _, x0, z0, h, _ = seg
                xs.append(x0 + d * sin(h))
                zs.append(z0 + d * cos(h))
                hs.append(wrap(h))
            else:
                _, cx, cz, h0, r, sign = seg
                h = h0 + sign * d / r
                xs.append(cx - sign * r * cos(h))
                zs.append(cz + sign * r * sin(h))
                hs.append(wrap(h))
            k += 1
        start += seg_len
    return xs, zs, hs


def side_point(xs, zs, hs, i, offset, y):
    """The point `offset` to the right of sample i, at height y (right is (-cos h, sin h))."""
    return (xs[i] - offset * cos(hs[i]), y, zs[i] + offset * sin(hs[i]))


def checkpoint_sample(index, count):
    return (int(index * count / CHECKPOINTS) + FIRST_CHECKPOINT) % count


# ---- triangles ---------------------------------------------------------------------

class Solid:
    """Triangles for a body alone, never drawn: shared corners and no normals (glTF allows
    none), a third the size of a drawn mesh's."""

    def __init__(self):
        self.parts = {}  # material -> (positions, None, indices)

    def corner(self, material, p):
        pos, _, _ = self.parts.setdefault(material, ([], None, []))
        pos.append(p)
        return len(pos) - 1

    def tri(self, material, a, b, c):
        """A triangle of three corners, counterclockwise seen from its outside."""
        self.parts[material][2].extend((a, b, c))

    def box(self, material, center, size, yaw=0.0):
        """A box, `size` (w, h, l) about `center`, turned `yaw` about y (+z along it)."""
        cx, cy, cz = center
        hw, hh, hl = size[0] / 2, size[1] / 2, size[2] / 2
        s, c = sin(yaw), cos(yaw)
        v = [self.corner(material, (cx + x * c + z * s, cy + y, cz - x * s + z * c))
             for y in (-hh, hh) for z in (-hl, hl) for x in (-hw, hw)]
        # corners: bit 0 x, bit 1 z, bit 2 y; each face's quad counterclockwise from outside
        for a, b, cc, d in ((4, 6, 7, 5), (0, 1, 3, 2), (1, 5, 7, 3), (0, 2, 6, 4), (2, 3, 7, 6), (0, 4, 5, 1)):
            self.tri(material, v[a], v[b], v[cc])
            self.tri(material, v[a], v[cc], v[d])


class Mesh:
    """Triangles in primitives, one a material: flat-shaded quads, each its own corners."""

    def __init__(self):
        self.parts = {}  # material -> (positions, normals, indices)

    def quad(self, material, a, b, c, d, normal):
        pos, nor, idx = self.parts.setdefault(material, ([], [], []))
        base = len(pos)
        for v in (a, b, c, d):
            pos.append(v)
            nor.append(normal)
        # counterclockwise seen from the front: the order whose cross product is the normal
        e1 = [b[k] - a[k] for k in range(3)]
        e2 = [c[k] - a[k] for k in range(3)]
        cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if sum(cross[k] * normal[k] for k in range(3)) >= 0:
            idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        else:
            idx += [base, base + 2, base + 1, base, base + 3, base + 2]

    def box(self, material, center, size, yaw=0.0):
        """A box, `size` (w, h, l) about `center`, turned `yaw` about y (+z along it)."""
        cx, cy, cz = center
        hw, hh, hl = size[0] / 2, size[1] / 2, size[2] / 2
        s, c = sin(yaw), cos(yaw)

        def P(x, y, z):
            return (cx + x * c + z * s, cy + y, cz - x * s + z * c)

        def N(x, y, z):
            return (x * c + z * s, y, -x * s + z * c)
        self.quad(material, P(-hw, hh, -hl), P(hw, hh, -hl), P(hw, hh, hl), P(-hw, hh, hl), N(0, 1, 0))
        self.quad(material, P(-hw, -hh, -hl), P(-hw, -hh, hl), P(hw, -hh, hl), P(hw, -hh, -hl), N(0, -1, 0))
        self.quad(material, P(hw, -hh, -hl), P(hw, -hh, hl), P(hw, hh, hl), P(hw, hh, -hl), N(1, 0, 0))
        self.quad(material, P(-hw, -hh, -hl), P(-hw, hh, -hl), P(-hw, hh, hl), P(-hw, -hh, hl), N(-1, 0, 0))
        self.quad(material, P(-hw, -hh, hl), P(-hw, hh, hl), P(hw, hh, hl), P(hw, -hh, hl), N(0, 0, 1))
        self.quad(material, P(-hw, -hh, -hl), P(hw, -hh, -hl), P(hw, hh, -hl), P(-hw, hh, -hl), N(0, 0, -1))


def barrier_fits(xs, zs, hs, i, sign):
    """Whether the barrier's point at sample i stays clear of the track: not inside a corner
    tighter than its offset, nor on another part of the loop."""
    p = side_point(xs, zs, hs, i, sign * BARRIER_OFFSET, 0.0)
    near = (BARRIER_OFFSET - 1.0) * (BARRIER_OFFSET - 1.0)
    for k in range(len(xs)):
        if (xs[k] - p[0]) * (xs[k] - p[0]) + (zs[k] - p[2]) * (zs[k] - p[2]) < near:
            return False
    return True


def barrier_runs(xs, zs, hs):
    """The barriers, as chords between samples on each side: (side, from, to), each drawn as
    one kit wall and walled in collision the same way."""
    c = len(xs)
    chords = []
    for sign in (-1, 1):
        fits = [barrier_fits(xs, zs, hs, i, sign) for i in range(c)]
        # from where a run begins (a fitting sample after one that doesn't), or 0 when all fit
        begin = next((i for i in range(c) if fits[i] and not fits[i - 1]), 0)
        k = 0  # samples gone round from `begin`
        while k < c:
            i = (begin + k) % c
            if not fits[i]:
                k += 1
                continue
            curved = abs(wrap(hs[(i + STRAIGHT_RUN) % c] - hs[i])) > 1e-6
            run = CURVED_RUN if curved else STRAIGHT_RUN
            span = 0
            while span < run and k + span < c and fits[(i + span + 1) % c]:
                span += 1
            if span > 0:
                chords.append((sign, i, (i + span) % c))
            k += max(span, 1)
    return chords


def collision(xs, zs, hs, chords, gates):
    c = len(xs)
    up = (0.0, 1.0, 0.0)
    ground = Mesh()
    g = GROUND / 2
    ground.quad('grass', (-g, GROUND_Y, -g), (g, GROUND_Y, -g), (g, GROUND_Y, g), (-g, GROUND_Y, g), up)

    road = Solid()  # flat curb to curb, then down to the ground at the tile's edge
    edge = TILE / 2
    across = [-edge, -CURB_HALF, CURB_HALF, edge]
    ys = [GROUND_Y, ROAD_Y, ROAD_Y, GROUND_Y]
    rows = [[road.corner('road', side_point(xs, zs, hs, i, o, y)) for o, y in zip(across, ys)] for i in range(c)]
    for i in range(c):
        here, there = rows[i], rows[(i + 1) % c]
        for k in range(3):
            # facing up (counterclockwise from above: left is +x facing +z)
            road.tri('road', here[k], there[k + 1], there[k])
            road.tri('road', here[k], here[k + 1], there[k + 1])

    walls = Solid()
    for sign, i, j in chords:
        p = side_point(xs, zs, hs, i, sign * BARRIER_OFFSET, 0.0)
        q = side_point(xs, zs, hs, j, sign * BARRIER_OFFSET, 0.0)
        span = length(q[0] - p[0], q[2] - p[2])
        yaw = atan2(q[0] - p[0], q[2] - p[2])
        walls.box('wall', ((p[0] + q[0]) / 2, GROUND_Y + WALL_HEIGHT / 2, (p[2] + q[2]) / 2),
                  (WALL_THICK, WALL_HEIGHT, span + WALL_THICK), yaw)
    for s in gates:
        for side in (-1, 1):
            p = side_point(xs, zs, hs, s, side * GATE_POST, 0.0)
            walls.box('wall', (p[0], GROUND_Y + WALL_HEIGHT / 2, p[2]), (2.1, WALL_HEIGHT, 3.0), hs[s])
    return {'ground': ground, 'road': road, 'walls': walls}


# ---- glTF --------------------------------------------------------------------------

def f32(x):
    """x as the float32 the file stores, back as a Python float: min and max exact."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def glb(meshes):
    materials = ['grass', 'road', 'wall']
    colors = {'grass': GRASS, 'road': (0.057805, 0.057805, 0.057805), 'wall': (0.5, 0.5, 0.5)}
    gltf = {
        'asset': {'version': '2.0', 'generator': 'racer tools/gen_track.py'},
        'scene': 0, 'scenes': [{'name': 'collision', 'nodes': []}],
        'nodes': [], 'meshes': [], 'materials': [], 'accessors': [], 'bufferViews': [], 'buffers': [],
    }
    for name in materials:
        r, g, b = colors[name]
        gltf['materials'].append({'name': name, 'pbrMetallicRoughness': {
            'baseColorFactor': [r, g, b, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 0.95}})
    blob = bytearray()

    def view(data, target):
        while len(blob) % 4:
            blob.append(0)
        gltf['bufferViews'].append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data), 'target': target})
        blob.extend(data)
        return len(gltf['bufferViews']) - 1

    def accessor(values, kind):
        if kind == 'index':
            short = max(values) < 65536
            data = struct.pack(f'<{len(values)}{"H" if short else "I"}', *values)
            gltf['accessors'].append({'bufferView': view(data, 34963), 'componentType': 5123 if short else 5125,
                                      'count': len(values), 'type': 'SCALAR'})
        else:
            flat = [x for v in values for x in v]
            data = struct.pack(f'<{len(flat)}f', *flat)
            acc = {'bufferView': view(data, 34962), 'componentType': 5126, 'count': len(values), 'type': 'VEC3'}
            if kind == 'position':
                acc['min'] = [f32(min(v[k] for v in values)) for k in range(3)]
                acc['max'] = [f32(max(v[k] for v in values)) for k in range(3)]
            gltf['accessors'].append(acc)
        return len(gltf['accessors']) - 1

    for name, mesh in meshes.items():
        prims = []
        for material in materials:  # in a fixed order: the same bytes every time
            if material not in mesh.parts:
                continue
            pos, nor, idx = mesh.parts[material]
            attributes = {'POSITION': accessor(pos, 'position')}
            if nor is not None:
                attributes['NORMAL'] = accessor(nor, 'normal')
            prims.append({'attributes': attributes, 'indices': accessor(idx, 'index'),
                          'material': materials.index(material)})
        gltf['meshes'].append({'name': name, 'primitives': prims})
        gltf['nodes'].append({'name': name, 'mesh': len(gltf['meshes']) - 1})
        gltf['scenes'][0]['nodes'].append(len(gltf['nodes']) - 1)

    while len(blob) % 4:
        blob.append(0)
    gltf['buffers'].append({'byteLength': len(blob)})
    text = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
    while len(text) % 4:
        text += b' '
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob))
    out += struct.pack('<II', len(text), 0x4E4F534A) + text
    out += struct.pack('<II', len(blob), 0x004E4942) + bytes(blob)
    return out


# ---- the sections' scenes ----------------------------------------------------------

def num(v, places=4):
    text = f'{v:.{places}f}'.rstrip('0').rstrip('.')
    return '0' if text in ('-0', '') else text


def placement(name, pos, rot, scale, shadows=True):
    return ['actor', f'  model path="kit/{name}.glb"' + ('' if shadows else ' cast_shadows=false'),
            f'  transform position={num(pos[0])},{num(pos[1])},{num(pos[2])} rotation=0,{num(rot, 6)},0 '
            f'scale={num(scale[0])},{num(scale[1])},{num(scale[2])}', 'end']


def tree_spots(xs, zs):
    """Trees around the track, clear of its barriers: the same woods every time (a Park-Miller
    generator, exact in a double)."""
    seed = 7
    lo_x, hi_x = min(xs) - 90, max(xs) + 90
    lo_z, hi_z = min(zs) - 90, max(zs) + 90
    spots = []
    tries = 0

    def rnd(lo, hi):
        nonlocal seed
        seed = (seed * 16807) % 2147483647
        return lo + (hi - lo) * (seed / 2147483647)
    while len(spots) < TREES and tries < 5000:
        tries += 1
        x, z = rnd(lo_x, hi_x), rnd(lo_z, hi_z)
        big, scale, yaw = rnd(0, 1) < 0.5, rnd(6.0, 9.0), rnd(-PI, PI)
        best, nearest = 1e18, 0
        for k in range(len(xs)):
            d = (xs[k] - x) * (xs[k] - x) + (zs[k] - z) * (zs[k] - z)
            if d < best:
                best, nearest = d, k
        if best < TREE_CLEAR * TREE_CLEAR:
            continue
        spots.append((x, z, 'treeLarge' if big else 'treeSmall', scale, yaw, nearest))
    return spots


def sections(placed, total, xs, zs, hs, chords, gates):
    """Each section's scene text, its new kit files, its first sample, and its piece count, in
    the loop's order from the grid."""
    c = len(xs)
    span = total / SECTIONS
    blocks = [[] for _ in range(SECTIONS)]
    starts = [None] * SECTIONS
    for (name, pos, rot, scale), along in placed:
        n = min(int(along / span), SECTIONS - 1)
        if starts[n] is None:
            starts[n] = along
        blocks[n].append((name, placement(name, pos, rot, scale, shadows=False)))
    first_sample = [int(math.ceil(starts[n] / STEP - 1e-9)) % c for n in range(SECTIONS)]

    def section_of(sample):
        n = 0
        for k in range(SECTIONS):
            if sample >= first_sample[k]:
                n = k
        return n

    for sign, i, j in chords:
        p = side_point(xs, zs, hs, i, sign * BARRIER_OFFSET, 0.0)
        q = side_point(xs, zs, hs, j, sign * BARRIER_OFFSET, 0.0)
        span_m = length(q[0] - p[0], q[2] - p[2])
        ux, uz = (q[0] - p[0]) / span_m, (q[2] - p[2]) / span_m
        rot = atan2(-uz, ux)  # the kit wall runs along its +x, 0.123 deep in -z
        sz = BARRIER_THICK / 0.123
        px, pz = local_to_world(p[0], p[2], rot, 0.0, -0.0615 * sz)
        blocks[section_of(i)].append(('barrierWall', placement(
            'barrierWall', (px, 0.0, pz), rot, (span_m, BARRIER_HEIGHT / 0.131, sz))))
    for s in gates:
        rot = wrap(hs[s] + PI)  # across the road, its posts either side
        px, pz = local_to_world(xs[s], zs[s], rot, 0.5 * GATE_SCALE[0], 0.095 * GATE_SCALE[2])
        blocks[section_of(s)].append(('overhead', placement('overhead', (px, 0.0, pz), rot, GATE_SCALE)))
    for x, z, name, scale, yaw, nearest in tree_spots(xs, zs):
        blocks[section_of(nearest)].append((name, placement(name, (x, 0.0, z), yaw, (scale, scale, scale))))

    seen = set()
    out = []
    for n in range(SECTIONS):
        names = []
        for name, _ in blocks[n]:
            if name not in seen and name not in names:
                names.append(name)
        seen.update(names)
        lines = ['wgf-scene 2', f'# Section {n} of the track: its kit pieces placed. Generated by tools/gen_track.py.']
        for _, block in blocks[n]:
            lines += block
        out.append(('\n'.join(lines) + '\n', [f'kit/{name}.glb' for name in names], first_sample[n], len(blocks[n])))
    return out


def track_scene(xs, zs, hs, gates):
    lines = ['wgf-scene 2',
             "# The track's collision and checkpoints, generated by tools/gen_track.py (its drawn",
             "# pieces are the sections', assets/track/section_<n>.scene, streamed in). Each surface a",
             '# static body of its own triangles at its own friction; the road and walls are hidden by',
             "# the game (src/Track.hx: a scene line can't hide a model), the ground is the field.",
             'actor track', '  model path="track/collision.glb"', 'end',
             'actor track/ground', '  model cast_shadows=false', '  body type=static shape=mesh friction=0.7', 'end',
             'actor track/road', '  model cast_shadows=false', '  body type=static shape=mesh friction=1', 'end',
             'actor track/walls', '  model cast_shadows=false', '  body type=static shape=mesh friction=0.3', 'end',
             '',
             '# A checkpoint: a sensor box across the track, barrier to barrier, its middle 2 m up,',
             "# meeting the car's layer.",
             'prefab checkpoint',
             f'  body type=sensor shape=box size={num(2 * BARRIER_OFFSET - 1)},4,1 offset=0,2,0 layer=2 mask=1',
             '  behavior name=Checkpoint', 'end']
    for i, s in enumerate(gates):
        lines += [f'actor checkpoint_{i}', '  from checkpoint',
                  f'  transform position={num(xs[s])},0,{num(zs[s])} rotation=0,{num(hs[s], 6)},0',
                  f'  behavior index={i}', 'end']
    return '\n'.join(lines) + '\n'


def haxe(xs, zs, hs, gates, secs, total):
    def floats(values):
        # 6 decimals (a micrometre, a microradian): what the game needs, and short of the
        # last digits, where a last-ulp difference anywhere upstream would show first
        return ',\n\t\t'.join(', '.join(f'{v:.6f}' for v in values[k:k + 6]) for k in range(0, len(values), 6))
    files = ',\n\t\t'.join('[' + ', '.join([f'"track/section_{n}.scene"'] + [f'"{f}"' for f in s[1]]) + ']'
                           for n, s in enumerate(secs))
    return f'''// Generated by tools/gen_track.py from its layout: run it again, never edit this.
package;

/** The track's centerline, every {STEP:g} m around its {total:.1f} m, its checkpoints, and its sections. **/
class TrackData {{
	public static inline var WIDTH = {WIDTH!r};
	public static inline var STEP = {STEP!r};
	public static inline var CHECKPOINTS = {CHECKPOINTS};
	public static inline var BARRIER_OFFSET = {BARRIER_OFFSET!r};
	public static inline var SECTIONS = {SECTIONS};

	/** The sample each checkpoint's gate stands on, the first the start line. **/
	public static final checkpoints:Array<Int> = [{', '.join(str(s) for s in gates)}];

	/** Each section's first sample, in the loop's order from the grid. **/
	public static final sectionStarts:Array<Int> = [{', '.join(str(s[2]) for s in secs)}];

	/** Each section's files: its scene, then the kit pieces first placed in it. **/
	public static final sectionFiles:Array<Array<String>> = [
		{files}
	];

	public static final xs:Array<Float> = [
		{floats(xs)}
	];
	public static final zs:Array<Float> = [
		{floats(zs)}
	];
	/** Yaw at each sample: the way along the track, (sin, cos) on x and z. **/
	public static final headings:Array<Float> = [
		{floats(hs)}
	];
}}
'''.encode('utf-8')


# ---- the files ---------------------------------------------------------------------

def outputs():
    placed, segments, total = pieces_and_centerline()
    xs, zs, hs = centerline(segments, total)
    c = len(xs)
    gates = [checkpoint_sample(i, c) for i in range(CHECKPOINTS)]
    chords = barrier_runs(xs, zs, hs)
    secs = sections(placed, total, xs, zs, hs, chords, gates)
    files = {
        GAME / 'assets' / 'track' / 'collision.glb': glb(collision(xs, zs, hs, chords, gates)),
        GAME / 'assets' / 'scenes' / 'track.scene': track_scene(xs, zs, hs, gates).encode('utf-8'),
        GAME / 'src' / 'TrackData.hx': haxe(xs, zs, hs, gates, secs, total),
    }
    for n, (text, _, _, _) in enumerate(secs):
        files[GAME / 'assets' / 'track' / f'section_{n}.scene'] = text.encode('utf-8')
    return files


def manifests(assets, written):
    """The manifests libwgf's tool writes for the tree at `assets` with `written` in place, as
    {path: bytes}."""
    with tempfile.TemporaryDirectory() as tmp:
        copy = Path(tmp) / 'assets'
        shutil.copytree(assets, copy, ignore=shutil.ignore_patterns('manifest.json'))
        for path, data in written.items():
            if assets in path.parents:
                target = copy / path.relative_to(assets)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
        subprocess.run([sys.executable, str(LIBWGF / 'tools' / 'gen_manifest.py'), '--quiet', str(copy)], check=True)
        return {assets / p.relative_to(copy): p.read_bytes() for p in sorted(copy.rglob('manifest.json'))}


def main():
    check = '--check' in sys.argv[1:]
    stale = []

    def put(path, data):
        if path.exists() and path.read_bytes() == data:
            return
        if check:
            stale.append(path)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f'gen_track: wrote {path.relative_to(GAME)} ({len(data)} bytes)')

    files = outputs()
    for path, data in files.items():
        put(path, data)
    # the manifests last, over every file under assets/ (gen_kit's pieces, the hand-written
    # scenes, the sky) as the files above leave it: run this again after changing any of them
    for path, data in manifests(GAME / 'assets', files).items():
        put(path, data)
    if stale:
        print('gen_track: stale: ' + ', '.join(str(p.relative_to(GAME)) for p in stale))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
