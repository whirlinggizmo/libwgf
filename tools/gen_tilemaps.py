#!/usr/bin/env python3
"""Generate gfx-tilemap-grids' Tiled maps and the tiles they and gfx-tilemap draw.

    tools/gen_tilemaps.py

Writes, under examples/assets/:
  textures/iso_tiles.png   32 by 32 tiles on 32 by 16 diamonds: grass, water (four frames,
                           animated), a stone block, a tree, a lamp post
  textures/hex_tiles.png   28 by 32 hexagons (pointed tops, sides of 16): grass, sand, water,
                           a hill
  textures/terrain.png     a corner terrain set of a dirt trail over transparency: tile i has
                           dirt at its corners where i's bits say (1 top-left, 2 top-right,
                           4 bottom-right, 8 bottom-left), drawn over gfx-tilemap's ground
  textures/clouds.png      soft clouds over transparency, gfx-tilemap's image layer
  maps/iso_tiles.tsj, maps/hex_tiles.tsj, maps/terrain.tsj (its corner set, "trail")
  maps/grids_iso.tmj, grids_staggered.tmj, grids_hex.tmj, grids_infinite.tmj
                           an isometric, a staggered, a hexagonal, and an infinite orthogonal
                           map (in chunks, over maps/tiles.tsj), with tall tiles drawn in
                           Tiled's order and tile objects
The maps are Tiled's JSON as Tiled writes it; the drawing is the same every time (the PNGs'
bytes may differ with zlib's version, so there is no --check: they are made once and
committed). Standard library only.
"""
import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from gltf import png  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'examples' / 'assets'
CLEAR = (0, 0, 0, 0)


def shade(color, f):
    return tuple(max(0, min(255, round(c * f))) for c in color[:3]) + (color[3] if len(color) > 3 else 255,)


def noise(x, y, seed):
    """A repeatable value in 0..1 for a texel."""
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


class Sheet:
    """An image of tiles in a row: pixels by tile and place."""

    def __init__(self, tile_w, tile_h, count):
        self.w, self.h, self.count = tile_w, tile_h, count
        self.rows = [[CLEAR] * (tile_w * count) for _ in range(tile_h)]

    def put(self, tile, x, y, color):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.rows[y][tile * self.w + x] = color

    def save(self, name):
        (ASSETS / 'textures' / name).write_bytes(png(self.w * self.count, self.h, self.rows))


def in_diamond(x, y, top, w, h):
    """Whether texel x, y is in the diamond w by h whose box's top is at `top`."""
    dx = abs(x + 0.5 - w / 2) / (w / 2)
    dy = abs(y + 0.5 - (top + h / 2)) / (h / 2)
    return dx + dy <= 1.0


def iso_tiles():
    """grass, water x4, stone block, tree, lamp: 32 by 32, the diamond at the bottom."""
    s = Sheet(32, 32, 8)
    grass, water, stone = (86, 150, 70), (52, 108, 170), (150, 146, 140)
    for y in range(32):
        for x in range(32):
            if in_diamond(x, y, 16, 32, 16):
                s.put(0, x, y, shade(grass, 0.88 + 0.2 * noise(x, y, 1)))
                for f in range(4):
                    wave = math.sin((x + y * 2) * 0.55 + f * math.pi / 2) * 0.12
                    s.put(1 + f, x, y, shade(water, 0.95 + wave + 0.06 * noise(x, y, 2)))
    for y in range(32):  # a block: its top a diamond at 0..16, its sides down to the bottom diamond's lower edges
        for x in range(32):
            if in_diamond(x, y, 0, 32, 16):
                s.put(5, x, y, shade(stone, 1.1 + 0.1 * noise(x, y, 3)))
            elif in_diamond(x, y, 16, 32, 16) or 8 <= y <= 24:
                s.put(5, x, y, shade(stone, (0.75 if x < 16 else 0.6) + 0.08 * noise(x, y, 4)))
    for y in range(32):  # a tree: a trunk and a round crown
        for x in range(32):
            if 14 <= x <= 17 and 16 <= y <= 27:
                s.put(6, x, y, shade((110, 76, 44), 0.9 + 0.2 * noise(x, y, 5)))
            d = math.hypot(x + 0.5 - 16, y + 0.5 - 11)
            if d <= 9:
                s.put(6, x, y, shade((50, 120, 56), 0.75 + 0.35 * (1 - d / 9) + 0.1 * noise(x, y, 6)))
    for y in range(32):  # a lamp post with a light on top
        for x in range(32):
            if 15 <= x <= 16 and 8 <= y <= 27:
                s.put(7, x, y, (60, 60, 70, 255))
            d = math.hypot(x + 0.5 - 16, y + 0.5 - 6)
            if d <= 4:
                s.put(7, x, y, shade((255, 214, 120), 1.0 - 0.3 * d / 4))
    s.save('iso_tiles.png')


def in_hex(x, y, w, h, side):
    """Whether texel x, y is in the hexagon of a w by h box, its left and right sides `side` long."""
    a, b, s = w / 2, h / 2, side / 2
    dx, dy = abs(x + 0.5 - a), abs(y + 0.5 - b)
    return dx <= a and dy <= b and dy <= s + (b - s) * (1 - dx / a)


def hex_tiles():
    s = Sheet(28, 32, 4)
    colors = [(92, 156, 72), (212, 190, 128), (56, 112, 176), (120, 140, 80)]
    for t, color in enumerate(colors):
        for y in range(32):
            for x in range(28):
                if not in_hex(x, y, 28, 32, 16):
                    continue
                edge = not in_hex(x, y, 28 - 3, 32 - 3, 16) or not in_hex(x + 1, y + 1, 28 + 1, 32 + 1, 16)
                f = 0.9 + 0.15 * noise(x, y, 10 + t)
                if t == 3:  # a hill: lighter toward its top
                    f += 0.25 * max(0.0, 1 - math.hypot(x + 0.5 - 14, y + 0.5 - 13) / 10)
                s.put(t, x, y, shade(color, 0.7 if edge else f))
    s.save('hex_tiles.png')


def terrain_tiles():
    """Tile i of 16: dirt in each quarter whose corner i's bits name, pulled in where it meets a
    quarter without."""
    s = Sheet(16, 16, 16)
    dirt = (140, 100, 62)
    across = {1: 2, 2: 1, 4: 8, 8: 4}  # the corner beside each, left or right
    down = {1: 8, 2: 4, 4: 2, 8: 1}  # and above or below
    for i in range(16):
        for y in range(16):
            for x in range(16):
                px, py = x + 0.5, y + 0.5
                bit = (2 if px >= 8 else 1) if py < 8 else (4 if px >= 8 else 8)
                if not i & bit:
                    continue
                if not i & across[bit] and abs(px - 8) < 3:
                    continue
                if not i & down[bit] and abs(py - 8) < 3:
                    continue
                s.put(i, x, y, shade(dirt, 0.85 + 0.25 * noise(x, y, 20 + i)))
    s.save('terrain.png')


def clouds():
    w, h = 192, 72
    rows = [[CLEAR] * w for _ in range(h)]
    puffs = [(30, 40, 18), (52, 30, 22), (78, 40, 17), (120, 44, 15), (140, 34, 20), (162, 44, 14)]
    for y in range(h):
        for x in range(w):
            a = 0.0
            for cx, cy, r in puffs:
                d = math.hypot(x + 0.5 - cx, (y + 0.5 - cy) * 1.4)
                a = max(a, min(1.0, (r - d) / 6))
            if a > 0:
                rows[y][x] = (250, 250, 255, round(a * 200))
    (ASSETS / 'textures' / 'clouds.png').write_bytes(png(w, h, rows))


def tileset(name, image, tile_w, tile_h, count, extra=None):
    t = {'columns': count, 'image': f'../textures/{image}', 'imageheight': tile_h, 'imagewidth': tile_w * count,
         'margin': 0, 'name': name, 'spacing': 0, 'tilecount': count, 'tiledversion': '1.10.2', 'tileheight': tile_h,
         'tilewidth': tile_w, 'type': 'tileset', 'version': '1.10'}
    t.update(extra or {})
    return t


def write_json(path, value):
    (ASSETS / 'maps' / path).write_text(json.dumps(value, indent=1) + '\n', encoding='utf-8')


def tile_layer(lid, name, width, height, data):
    return {'data': data, 'height': height, 'id': lid, 'name': name, 'opacity': 1, 'type': 'tilelayer',
            'visible': True, 'width': width, 'x': 0, 'y': 0}


def map_of(orientation, width, height, tw, th, tilesets, layers, extra=None):
    m = {'compressionlevel': -1, 'height': height, 'infinite': False, 'layers': layers,
         'nextlayerid': len(layers) + 1, 'nextobjectid': 10, 'orientation': orientation, 'renderorder': 'right-down',
         'tiledversion': '1.10.2', 'tileheight': th, 'tilesets': tilesets, 'tilewidth': tw, 'type': 'map',
         'version': '1.10', 'width': width}
    m.update(extra or {})
    return m


def maps():
    # iso_tiles.tsj: 1 grass, 2..5 water's frames (2 animated), 6 a stone block, 7 a tree, 8 a lamp.
    frames = [{'tileid': 1 + f, 'duration': 240} for f in range(4)]
    write_json('iso_tiles.tsj', tileset('iso_tiles', 'iso_tiles.png', 32, 32, 8, {
        'tiles': [{'id': 1, 'type': 'water', 'animation': frames}, {'id': 5, 'type': 'block'}]}))
    write_json('hex_tiles.tsj', tileset('hex_tiles', 'hex_tiles.png', 28, 32, 4,
                                        {'tiles': [{'id': 2, 'type': 'water'}]}))
    wang = [{'tileid': i, 'wangid': [0, 1 if i & 2 else 0, 0, 1 if i & 4 else 0, 0, 1 if i & 8 else 0, 0,
                                     1 if i & 1 else 0]} for i in range(1, 16)]
    write_json('terrain.tsj', tileset('terrain', 'terrain.png', 16, 16, 16, {'wangsets': [{
        'colors': [{'color': '#8c643e', 'name': 'dirt', 'probability': 1, 'tile': 15}], 'name': 'trail', 'tile': 15,
        'type': 'corner', 'wangtiles': wang}]}))
    iso_set = [{'firstgid': 1, 'source': 'iso_tiles.tsj'}]

    # Isometric, 8 by 8: grass with a pond, blocks and trees on a layer of their own, a lamp as a tile object.
    n = 8
    ground = [2 if 2 <= c <= 4 and 3 <= r <= 5 else 1 for r in range(n) for c in range(n)]
    tall = {(0, 0): 6, (1, 0): 6, (0, 1): 6, (6, 1): 7, (6, 2): 7, (1, 6): 7, (7, 6): 6, (5, 6): 7, (3, 7): 6}
    things = [tall.get((c, r), 0) for r in range(n) for c in range(n)]
    lamp = {'gid': 8, 'height': 32, 'id': 1, 'name': 'lamp', 'rotation': 0, 'type': '', 'visible': True,
            'width': 32, 'x': 16 * 6.5, 'y': 16 * 4.5}
    write_json('grids_iso.tmj', map_of('isometric', n, n, 32, 16, iso_set, [
        tile_layer(1, 'ground', n, n, ground), tile_layer(2, 'things', n, n, things),
        {'draworder': 'topdown', 'id': 3, 'name': 'objects', 'objects': [lamp], 'opacity': 1,
         'type': 'objectgroup', 'visible': True, 'x': 0, 'y': 0}]))

    # Staggered (rows moved), 6 by 12.
    w, h = 6, 12
    ground = [2 if (c + r // 2) % 5 == 0 else 1 for r in range(h) for c in range(w)]
    things = [7 if (c * 7 + r * 3) % 11 == 0 else 6 if (c * 5 + r) % 13 == 0 else 0 for r in range(h) for c in range(w)]
    write_json('grids_staggered.tmj', map_of('staggered', w, h, 32, 16, iso_set, [
        tile_layer(1, 'ground', w, h, ground), tile_layer(2, 'things', w, h, things)],
        {'staggeraxis': 'y', 'staggerindex': 'odd'}))

    # Hexagonal (pointed tops, rows moved), 8 by 7.
    w, h = 8, 7
    ground = [3 if math.hypot(c - 4.5, r - 3) < 1.6 else 2 if math.hypot(c - 4.5, r - 3) < 2.6 else
              4 if (c * 3 + r * 5) % 9 == 0 else 1 for r in range(h) for c in range(w)]
    write_json('grids_hex.tmj', map_of('hexagonal', w, h, 28, 32, [{'firstgid': 1, 'source': 'hex_tiles.tsj'}], [
        tile_layer(1, 'ground', w, h, ground)], {'hexsidelength': 16, 'staggeraxis': 'y', 'staggerindex': 'odd'}))

    # Infinite (orthogonal, in chunks of 8 by 8 either side of Tiled's first cell), over tiles.tsj.
    def chunk(cx, cy):
        data = []
        for y in range(8):
            for x in range(8):
                gx, gy = cx + x, cy + y
                d = math.hypot(gx + 0.5, gy + 0.5)
                data.append(3 if d > 9 else 2 if d > 7.5 else 4 if gx == 0 or gy == 0 else 1)
        return {'data': data, 'height': 8, 'width': 8, 'x': cx, 'y': cy}
    chunks = [chunk(cx, cy) for cy in (-8, 0) for cx in (-8, 0)]
    layer = {'chunks': chunks, 'height': 16, 'id': 1, 'name': 'ground', 'opacity': 1, 'startx': -8, 'starty': -8,
             'type': 'tilelayer', 'visible': True, 'width': 16, 'x': 0, 'y': 0}
    write_json('grids_infinite.tmj', map_of('orthogonal', 30, 20, 16, 16, [{'firstgid': 1, 'source': 'tiles.tsj'}],
                                            [layer], {'infinite': True}))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    iso_tiles()
    hex_tiles()
    terrain_tiles()
    clouds()
    maps()
    print('gen_tilemaps: wrote examples/assets/textures/{iso_tiles,hex_tiles,terrain,clouds}.png and '
          'examples/assets/maps/{iso_tiles,hex_tiles,terrain}.tsj and grids_{iso,staggered,hex,infinite}.tmj')


if __name__ == '__main__':
    main()
