#!/usr/bin/env python3
"""Generate examples/assets/ui/skin.png: the sprite sheet examples/c/ui-document's menu is
drawn from (examples/assets/ui/options.rcss names each sprite's place in it).

    tools/gen_ui_skin.py

Rounded panels with a border, each a nine-patch (its corners kept, its edges and middle
stretched): a button in each state (normal, hovered, pressed, focused, disabled), a panel,
a check box empty and checked (each also focused), a slider's track and its thumb, and a
tab, chosen and not. Drawn with antialiased edges from their distance to each shape, so
the same pixels come out every time (the file's bytes may differ with zlib's version, so
there is no --check: it is made once and committed). Standard library only.
"""
import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from gltf import png  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples' / 'assets' / 'ui' / 'skin.png'
WIDTH, HEIGHT = 256, 128

# (x, y, w, h, radius, fill, border, border width): every sprite, where options.rcss says
BOX = 48
SPRITES = [
    (0, 0, BOX, BOX, 10, (46, 53, 70, 255), (90, 102, 128, 255), 2),       # button
    (48, 0, BOX, BOX, 10, (63, 72, 96, 255), (140, 160, 200, 255), 2),     # button, hovered
    (96, 0, BOX, BOX, 10, (30, 34, 46, 255), (90, 102, 128, 255), 2),      # button, pressed
    (144, 0, BOX, BOX, 10, (46, 53, 70, 255), (255, 208, 64, 255), 3),     # button, focused
    (192, 0, BOX, BOX, 10, (38, 40, 46, 200), (58, 61, 69, 200), 2),       # button, disabled
    (0, 48, 64, 64, 16, (20, 24, 36, 238), (90, 160, 230, 255), 2),        # panel
    (64, 48, 24, 24, 5, (30, 34, 46, 255), (140, 160, 200, 255), 2),       # check box
    (88, 48, 24, 24, 5, (90, 160, 230, 255), (140, 190, 240, 255), 2),     # check box, checked (a mark drawn on)
    (112, 48, 24, 24, 5, (30, 34, 46, 255), (255, 208, 64, 255), 2),      # check box, focused
    (136, 48, 24, 24, 5, (90, 160, 230, 255), (255, 208, 64, 255), 2),    # check box, checked and focused
    (160, 48, 48, 12, 6, (30, 34, 46, 255), (90, 102, 128, 255), 1),       # slider track
    (208, 48, 20, 28, 6, (230, 233, 240, 255), (90, 160, 230, 255), 2),    # slider thumb
    (228, 48, 20, 28, 6, (255, 224, 120, 255), (255, 208, 64, 255), 2),    # slider thumb, focused
    (64, 80, 48, 32, 8, (30, 34, 46, 255), (90, 102, 128, 255), 2),        # tab
    (112, 80, 48, 32, 8, (63, 72, 96, 255), (90, 160, 230, 255), 2),       # tab, chosen
]
CHECKED = ((88, 48), (136, 48))  # the check boxes with a mark


def rounded(px, py, w, h, r):
    """The distance from (px, py) to a w by h rectangle at the origin with corners of radius
    r: negative inside."""
    qx, qy = abs(px - w / 2) - (w / 2 - r), abs(py - h / 2) - (h / 2 - r)
    outside = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return outside + min(max(qx, qy), 0.0) - r


def segment(px, py, ax, ay, bx, by):
    """The distance from (px, py) to the segment from a to b."""
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - dx * t, py - ay - dy * t)


def over(dst, src, coverage):
    """`src` (r, g, b, a) drawn over `dst` at `coverage` (0 to 1), straight alpha."""
    a = src[3] / 255 * coverage
    da = dst[3] / 255
    out_a = a + da * (1 - a)
    if out_a <= 0:
        return (0, 0, 0, 0)
    rgb = [(src[i] * a + dst[i] * da * (1 - a)) / out_a for i in range(3)]
    return tuple(round(c) for c in rgb) + (round(out_a * 255),)


def skin_png():
    """The image's bytes."""
    rows = [[(0, 0, 0, 0)] * WIDTH for _ in range(HEIGHT)]
    for x0, y0, w, h, r, fill, border, bw in SPRITES:
        for y in range(h):
            for x in range(w):
                d = rounded(x + 0.5, y + 0.5, w, h, r)
                shape = max(0.0, min(1.0, 0.5 - d))
                if shape <= 0:
                    continue
                inner = max(0.0, min(1.0, 0.5 - (d + bw)))  # inside the border
                pixel = over((0, 0, 0, 0), border, shape)
                pixel = over(pixel, fill, inner)
                if (x0, y0) in CHECKED:
                    m = min(segment(x + 0.5, y + 0.5, 6, 12.5, 10, 17), segment(x + 0.5, y + 0.5, 10, 17, 18, 7))
                    pixel = over(pixel, (255, 255, 255, 255), max(0.0, min(1.0, 2.0 - m)))
                rows[y0 + y][x0 + x] = pixel
    return png(WIDTH, HEIGHT, rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    data = skin_png()
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(data)
    print(f'gen_ui_skin: {OUT.relative_to(ROOT)} ({len(data)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
