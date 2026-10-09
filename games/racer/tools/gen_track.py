#!/usr/bin/env python3
"""Write the racer's track: assets/track/track.glb, a glTF of named nodes, and
src/TrackData.hx, the centerline the game drives by -- both from the loop below, the same
bytes every time (--check: write nothing, fail when either is stale).

    python3 tools/gen_track.py [--check]

The centerline is a Catmull-Rom loop through POINTS (scaled by SCALE metres a unit),
resampled every STEP metres. The file's nodes, each a mesh of its own triangles:

    ground             the grass, 800 m square, its top at 0
    road               the asphalt ribbon, WIDTH wide, its top at ROAD_Y
      curbs            raised red and white strips on the corners, both sides
      line             the start line, across checkpoint 0
    barriers           walls BARRIER_OFFSET from the centerline on both sides, left out
                       where they would fold (a corner tighter than the offset) or cross
                       the track's other parts
    gates
      gate_0 .. gate_7 at each checkpoint, facing along the track (+z the way it is
                       driven): post_l, post_r, and banner under each

The game (src/Track.hx) loads the file, puts a static body on ground, road, barriers, and
gates (each body their triangles), and a checkpoint's sensor under each gate.
Standard library only.
"""
import json
import math
import struct
import sys
from pathlib import Path

GAME = Path(__file__).resolve().parents[1]
GLB = GAME / 'assets' / 'track' / 'track.glb'
DATA = GAME / 'src' / 'TrackData.hx'

POINTS = [0.0, -50, 0, 30, 12, 62, 42, 72, 72, 58, 78, 26, 56, 2, 58, -28, 82, -52, 66, -82, 26, -86]
SCALE = 1.7
STEP = 2.0
WIDTH = 12.0
CHECKPOINTS = 8
FIRST_CHECKPOINT = 6  # samples up the first straight: the grid is behind it
ROAD_Y = 0.03
CURB_WIDTH = 1.0
CURB_Y = 0.1
BARRIER_OFFSET = 15.0  # from the centerline: 6 of asphalt, 1 of curb, 8 of grass
BARRIER_HEIGHT = 0.9
BARRIER_THICK = 0.5
GATE_HALF = 7.6  # the posts, either side
GROUND = 800.0

# colors as the game tinted them (sRGB), made linear for glTF's factors
COLORS = {
    'grass': (70, 128, 58), 'asphalt': (58, 60, 66), 'curb_red': (200, 40, 36),
    'curb_white': (235, 235, 235), 'line': (235, 235, 235), 'barrier': (190, 192, 198),
    'post': (232, 232, 232), 'banner': (40, 96, 200),
}


# ---- the centerline ----------------------------------------------------------------

def centerline():
    """Samples every STEP metres along the loop: (xs, zs, headings)."""
    n = len(POINTS) // 2

    def p(i, k):
        return POINTS[((i + n) % n) * 2 + k] * SCALE

    dense = []
    per = 40
    for i in range(n):
        for s in range(per):
            t = s / per
            t2, t3 = t * t, t * t * t

            def cr(p0, p1, p2, p3):
                return 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
                              (-p0 + 3 * p1 - 3 * p2 + p3) * t3)
            dense.append((cr(p(i - 1, 0), p(i, 0), p(i + 1, 0), p(i + 2, 0)),
                          cr(p(i - 1, 1), p(i, 1), p(i + 1, 1), p(i + 2, 1))))
    xs, zs = [dense[0][0]], [dense[0][1]]
    carried = 0.0
    m = len(dense)
    for i in range(m):
        ax, az = dense[i]
        bx, bz = dense[(i + 1) % m]
        d = math.sqrt((bx - ax) ** 2 + (bz - az) ** 2)
        t = STEP - carried
        while t <= d:
            xs.append(ax + (bx - ax) * t / d)
            zs.append(az + (bz - az) * t / d)
            t += STEP
        carried = d - (t - STEP)
    if carried < STEP * 0.5:  # the last sample too near the first
        xs.pop()
        zs.pop()
    c = len(xs)
    headings = [math.atan2(xs[(i + 1) % c] - xs[i], zs[(i + 1) % c] - zs[i]) for i in range(c)]
    return xs, zs, headings


def wrap(a):
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


def checkpoint_sample(index, count):
    return (int(index * count / CHECKPOINTS) + FIRST_CHECKPOINT) % count


# ---- triangles ---------------------------------------------------------------------

class Mesh:
    """Triangles in primitives, one a material: flat-shaded quads, each its own corners."""

    def __init__(self):
        self.parts = {}  # material -> (positions, normals, uvs, indices)

    def quad(self, material, a, b, c, d, normal, uv=((0, 0), (1, 0), (1, 1), (0, 1))):
        pos, nor, uvs, idx = self.parts.setdefault(material, ([], [], [], []))
        base = len(pos)
        for v, t in zip((a, b, c, d), uv):
            pos.append(v)
            nor.append(normal)
            uvs.append(t)
        # counterclockwise seen from the front: the order whose cross product is the normal
        e1 = [b[k] - a[k] for k in range(3)]
        e2 = [c[k] - a[k] for k in range(3)]
        cross = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        if sum(cross[k] * normal[k] for k in range(3)) >= 0:
            idx += [base, base + 1, base + 2, base, base + 2, base + 3]
        else:
            idx += [base, base + 2, base + 1, base, base + 3, base + 2]

    def box(self, material, center, size, yaw=0.0):
        """A box, `size` (w, h, l) about `center`, turned `yaw` about y."""
        cx, cy, cz = center
        hw, hh, hl = size[0] / 2, size[1] / 2, size[2] / 2
        s, c = math.sin(yaw), math.cos(yaw)

        def P(x, y, z):  # local to world: +z along the yaw
            return (cx + x * c + z * s, cy + y, cz - x * s + z * c)

        def N(x, y, z):
            return (x * c + z * s, y, -x * s + z * c)
        self.quad(material, P(-hw, hh, -hl), P(hw, hh, -hl), P(hw, hh, hl), P(-hw, hh, hl), N(0, 1, 0))
        self.quad(material, P(-hw, -hh, -hl), P(-hw, -hh, hl), P(hw, -hh, hl), P(hw, -hh, -hl), N(0, -1, 0))
        self.quad(material, P(hw, -hh, -hl), P(hw, -hh, hl), P(hw, hh, hl), P(hw, hh, -hl), N(1, 0, 0))
        self.quad(material, P(-hw, -hh, -hl), P(-hw, hh, -hl), P(-hw, hh, hl), P(-hw, -hh, hl), N(-1, 0, 0))
        self.quad(material, P(-hw, -hh, hl), P(-hw, hh, hl), P(hw, hh, hl), P(hw, -hh, hl), N(0, 0, 1))
        self.quad(material, P(-hw, -hh, -hl), P(hw, -hh, -hl), P(hw, hh, -hl), P(-hw, hh, -hl), N(0, 0, -1))


def side_point(xs, zs, i, offset, y):
    """The point `offset` across the track from sample i (right positive), at height y: the
    normal there the average of the two segments' meeting at it, so a ribbon has no gaps."""
    c = len(xs)
    hp = math.atan2(xs[i] - xs[i - 1], zs[i] - zs[i - 1])
    hn = math.atan2(xs[(i + 1) % c] - xs[i], zs[(i + 1) % c] - zs[i])
    h = hp + wrap(hn - hp) / 2
    return (xs[i] + offset * math.cos(h), y, zs[i] - offset * math.sin(h))


def build(xs, zs, headings):
    c = len(xs)
    up = (0.0, 1.0, 0.0)
    meshes = {}

    ground = Mesh()
    g = GROUND / 2
    ground.quad('grass', (-g, 0, -g), (g, 0, -g), (g, 0, g), (-g, 0, g), up, ((0, 0), (40, 0), (40, 40), (0, 40)))
    meshes['ground'] = ground

    road = Mesh()
    v = 0.0
    for i in range(c):
        j = (i + 1) % c
        a, b = side_point(xs, zs, i, -WIDTH / 2, ROAD_Y), side_point(xs, zs, i, WIDTH / 2, ROAD_Y)
        d, e = side_point(xs, zs, j, -WIDTH / 2, ROAD_Y), side_point(xs, zs, j, WIDTH / 2, ROAD_Y)
        road.quad('asphalt', a, b, e, d, up, ((0, v), (1, v), (1, v + STEP / WIDTH), (0, v + STEP / WIDTH)))
        v += STEP / WIDTH
    meshes['road'] = road

    curbs = Mesh()
    for i in range(c):
        turn = wrap(headings[(i + 3) % c] - headings[(i - 3) % c])
        if abs(turn) <= 0.25:
            continue
        j = (i + 1) % c
        material = 'curb_red' if i % 2 == 0 else 'curb_white'
        for sign in (-1, 1):
            inner, outer = sign * WIDTH / 2, sign * (WIDTH / 2 + CURB_WIDTH)
            ti, to = side_point(xs, zs, i, inner, CURB_Y), side_point(xs, zs, i, outer, CURB_Y)
            ni, no = side_point(xs, zs, j, inner, CURB_Y), side_point(xs, zs, j, outer, CURB_Y)
            curbs.quad(material, ti, to, no, ni, up)
            for (p, q) in ((ti, ni), (to, no)):  # its two sides, down to the ground
                pb, qb = (p[0], 0.0, p[2]), (q[0], 0.0, q[2])
                dx, dz = q[0] - p[0], q[2] - p[2]
                ln = math.hypot(dx, dz) or 1.0
                n = (dz / ln, 0.0, -dx / ln)  # right of the way it runs
                if (p is ti) == (sign > 0):
                    n = (-n[0], 0.0, -n[2])
                curbs.quad(material, pb, qb, q, p, n)
    meshes['curbs'] = curbs

    line = Mesh()
    s = checkpoint_sample(0, c)
    line.box('line', (xs[s], ROAD_Y + 0.006, zs[s]), (WIDTH, 0.012, 1.0), headings[s])
    meshes['line'] = line

    barriers = Mesh()
    for i in range(c):
        j = (i + 1) % c
        for sign in (-1, 1):
            if not barrier_fits(xs, zs, headings, i, sign) or not barrier_fits(xs, zs, headings, j, sign):
                continue
            pts = []
            for k in (i, j):
                pts.append((side_point(xs, zs, k, sign * (BARRIER_OFFSET - BARRIER_THICK / 2), 0.0),
                            side_point(xs, zs, k, sign * (BARRIER_OFFSET + BARRIER_THICK / 2), 0.0)))
            (a_in, a_out), (b_in, b_out) = pts

            def top(p):
                return (p[0], BARRIER_HEIGHT, p[2])
            barriers.quad('barrier', top(a_in), top(a_out), top(b_out), top(b_in), up)
            for p, q in ((a_in, b_in), (a_out, b_out)):
                dx, dz = q[0] - p[0], q[2] - p[2]
                ln = math.hypot(dx, dz) or 1.0
                n = (dz / ln, 0.0, -dx / ln)
                facing_track = p is a_in
                if (sign > 0) == facing_track:  # the inner face looks back at the track
                    n = (-n[0], 0.0, -n[2])
                barriers.quad('barrier', p, q, top(q), top(p), n)
    meshes['barriers'] = barriers

    post = Mesh()
    post.box('post', (0, 0, 0), (0.3, 3.6, 0.3))
    meshes['post'] = post
    banner = Mesh()
    banner.box('banner', (0, 0, 0), (15.5, 0.7, 0.12))
    meshes['banner'] = banner
    return meshes


def barrier_fits(xs, zs, headings, i, sign):
    """Whether the barrier's point at sample i stays outside the track: not folded inside a
    corner tighter than its offset, nor on another part of the loop."""
    c = len(xs)
    p = side_point(xs, zs, i, sign * BARRIER_OFFSET, 0.0)
    for k in range(c):
        if (xs[k] - p[0]) ** 2 + (zs[k] - p[2]) ** 2 < (BARRIER_OFFSET - 1.0) ** 2:
            return False
    return True


# ---- glTF --------------------------------------------------------------------------

def srgb_to_linear(c):
    c = c / 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def f32(x):
    """x as the float32 the file stores, back as a Python float: min and max exact."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def glb(xs, zs, headings):
    meshes = build(xs, zs, headings)
    materials = list(COLORS)
    gltf = {
        'asset': {'version': '2.0', 'generator': 'racer tools/gen_track.py'},
        'scene': 0, 'scenes': [{'name': 'track', 'nodes': []}],
        'nodes': [], 'meshes': [], 'materials': [], 'accessors': [], 'bufferViews': [], 'buffers': [],
    }
    for name in materials:
        r, g, b = (round(srgb_to_linear(v), 6) for v in COLORS[name])
        gltf['materials'].append({'name': name, 'pbrMetallicRoughness': {
            'baseColorFactor': [r, g, b, 1.0], 'metallicFactor': 0.0, 'roughnessFactor': 0.9}})
    blob = bytearray()

    def view(data, target):
        while len(blob) % 4:
            blob.append(0)
        gltf['bufferViews'].append({'buffer': 0, 'byteOffset': len(blob), 'byteLength': len(data), 'target': target})
        blob.extend(data)
        return len(gltf['bufferViews']) - 1

    def accessor(values, kind, components):
        flat = [x for v in values for x in v] if components > 1 else list(values)
        if kind == 'index':
            data = struct.pack(f'<{len(flat)}I', *flat)
            gltf['accessors'].append({'bufferView': view(data, 34963), 'componentType': 5125,
                                      'count': len(flat), 'type': 'SCALAR'})
        else:
            data = struct.pack(f'<{len(flat)}f', *flat)
            acc = {'bufferView': view(data, 34962), 'componentType': 5126, 'count': len(values),
                   'type': {2: 'VEC2', 3: 'VEC3'}[components]}
            if kind == 'position':
                acc['min'] = [f32(min(v[k] for v in values)) for k in range(3)]
                acc['max'] = [f32(max(v[k] for v in values)) for k in range(3)]
            gltf['accessors'].append(acc)
        return len(gltf['accessors']) - 1

    mesh_index = {}
    for name, mesh in meshes.items():
        prims = []
        for material in materials:  # in a fixed order: the same bytes every time
            if material not in mesh.parts:
                continue
            pos, nor, uvs, idx = mesh.parts[material]
            prims.append({'attributes': {'POSITION': accessor(pos, 'position', 3), 'NORMAL': accessor(nor, 'normal', 3),
                                         'TEXCOORD_0': accessor(uvs, 'uv', 2)},
                          'indices': accessor(idx, 'index', 1), 'material': materials.index(material)})
        gltf['meshes'].append({'name': name, 'primitives': prims})
        mesh_index[name] = len(gltf['meshes']) - 1

    def node(name, mesh=None, children=(), translation=None, yaw=None):
        n = {'name': name}
        if mesh is not None:
            n['mesh'] = mesh_index[mesh]
        if translation is not None:
            n['translation'] = [f32(t) for t in translation]
        if yaw is not None:
            n['rotation'] = [0.0, f32(math.sin(yaw / 2)), 0.0, f32(math.cos(yaw / 2))]
        if children:
            n['children'] = list(children)
        gltf['nodes'].append(n)
        return len(gltf['nodes']) - 1

    c = len(xs)
    roots = [node('ground', 'ground')]
    roots.append(node('road', 'road', [node('curbs', 'curbs'), node('line', 'line')]))
    roots.append(node('barriers', 'barriers'))
    gates = []
    for i in range(CHECKPOINTS):
        s = checkpoint_sample(i, c)
        parts = [node('post_l', 'post', translation=(GATE_HALF, 1.8, 0)),
                 node('post_r', 'post', translation=(-GATE_HALF, 1.8, 0)),
                 node('banner', 'banner', translation=(0, 3.6, 0))]
        gates.append(node(f'gate_{i}', None, parts, translation=(xs[s], 0.0, zs[s]), yaw=headings[s]))
    roots.append(node('gates', None, gates))
    gltf['scenes'][0]['nodes'] = roots

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


def haxe(xs, zs, headings):
    def floats(values):
        return ',\n\t\t'.join(', '.join(repr(v) for v in values[k:k + 6]) for k in range(0, len(values), 6))
    c = len(xs)
    return f'''// Generated by tools/gen_track.py from its centerline: run it again, never edit this.
package;

/** The track's centerline, every {STEP:g} m: what assets/track/track.glb was built along. **/
class TrackData {{
	public static inline var WIDTH = {WIDTH!r};
	public static inline var STEP = {STEP!r};
	public static inline var CHECKPOINTS = {CHECKPOINTS};
	public static inline var BARRIER_OFFSET = {BARRIER_OFFSET!r};

	/** The sample each checkpoint's gate stands on, the first the start line. **/
	public static final checkpoints:Array<Int> = [{', '.join(str(checkpoint_sample(i, c)) for i in range(CHECKPOINTS))}];

	public static final xs:Array<Float> = [
		{floats(xs)}
	];
	public static final zs:Array<Float> = [
		{floats(zs)}
	];
	/** Yaw at each sample: the way to the next, (sin, cos) on x and z. **/
	public static final headings:Array<Float> = [
		{floats(headings)}
	];
}}
'''.encode('utf-8')


def main():
    check = '--check' in sys.argv[1:]
    xs, zs, headings = centerline()
    stale = []
    for path, data in ((GLB, glb(xs, zs, headings)), (DATA, haxe(xs, zs, headings))):
        if path.exists() and path.read_bytes() == data:
            continue
        if check:
            stale.append(path)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            print(f'gen_track: wrote {path.relative_to(GAME)} ({len(data)} bytes)')
    if stale:
        print('gen_track: stale: ' + ', '.join(str(p.relative_to(GAME)) for p in stale))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
