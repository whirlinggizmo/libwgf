#!/usr/bin/env python3
"""Generate examples/assets/fonts/gizmo5/: Gizmo 5, a pixel font of printable ASCII drawn here,
as an AngelCode BMFont and as an image font.

    tools/gen_bitmap_font.py

Writes gizmo5.fnt (BMFont's text format: a line 10 pixels high, its baseline 8 down, glyphs
5 pixels wide or narrower, a few kerning pairs) with its page gizmo5_0.png (white glyphs on
transparent, to be tinted), and gizmo5_image.png: the same glyphs from the space on, in rows
on a magenta key, colored top to bottom, as raylib's LoadFont reads an image of glyphs. The
glyphs are this file's own, under libwgf's license. Its output is the same every time (the
PNGs' bytes may differ with zlib's version, so there is no --check: they are made once and
committed). Standard library only.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # an embedded Python (Windows) doesn't add it
from gltf import png  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'examples' / 'assets' / 'fonts' / 'gizmo5'
LINE_HEIGHT, BASE, TOP = 10, 8, 1  # a glyph's first row is TOP below the line's top
PAGE = 128
KEY = (255, 0, 255, 255)

# Each glyph's rows from the cap height's top, '#' set: seven above the baseline, and up to
# two below it for descenders. A row's length is the glyph's width.
GLYPHS = {
    ' ': '... ... ... ... ... ... ...',
    '!': '# # # # # . #',
    '"': '#.# #.# ... ... ... ... ...',
    '#': '.#.#. .#.#. ##### .#.#. ##### .#.#. .#.#.',
    '$': '..#.. .#### #.#.. .###. ..#.# ####. ..#..',
    '%': '##..# ##.#. ...#. ..#.. .#... .#.## #..##',
    '&': '.##.. #..#. #.#.. .#... #.#.# #..#. .##.#',
    "'": '# # . . . . .',
    '(': '..# .#. #.. #.. #.. .#. ..#',
    ')': '#.. .#. ..# ..# ..# .#. #..',
    '*': '..... ..#.. #.#.# .###. #.#.# ..#.. .....',
    '+': '..... ..#.. ..#.. ##### ..#.. ..#.. .....',
    ',': '.. .. .. .. .. .# .# #.',
    '-': '.... .... .... #### .... .... ....',
    '.': '. . . . . . #',
    '/': '....# ...#. ...#. ..#.. .#... .#... #....',
    '0': '.###. #...# #..## #.#.# ##..# #...# .###.',
    '1': '.#. ##. .#. .#. .#. .#. ###',
    '2': '.###. #...# ....# ...#. ..#.. .#... #####',
    '3': '##### ...#. ..#.. ...#. ....# #...# .###.',
    '4': '...#. ..##. .#.#. #..#. ##### ...#. ...#.',
    '5': '##### #.... ####. ....# ....# #...# .###.',
    '6': '..##. .#... #.... ####. #...# #...# .###.',
    '7': '##### ....# ...#. ..#.. .#... .#... .#...',
    '8': '.###. #...# #...# .###. #...# #...# .###.',
    '9': '.###. #...# #...# .#### ....# ...#. .##..',
    ':': '. . # . . # .',
    ';': '.. .. .# .. .. .# .# #.',
    '<': '...# ..#. .#.. #... .#.. ..#. ...#',
    '=': '.... .... #### .... #### .... ....',
    '>': '#... .#.. ..#. ...# ..#. .#.. #...',
    '?': '.###. #...# ....# ...#. ..#.. ..... ..#..',
    '@': '.###. #...# #.### #.#.# #.### #.... .###.',
    'A': '.###. #...# #...# ##### #...# #...# #...#',
    'B': '####. #...# #...# ####. #...# #...# ####.',
    'C': '.###. #...# #.... #.... #.... #...# .###.',
    'D': '####. #...# #...# #...# #...# #...# ####.',
    'E': '##### #.... #.... ####. #.... #.... #####',
    'F': '##### #.... #.... ####. #.... #.... #....',
    'G': '.###. #...# #.... #.### #...# #...# .####',
    'H': '#...# #...# #...# ##### #...# #...# #...#',
    'I': '### .#. .#. .#. .#. .#. ###',
    'J': '..### ...#. ...#. ...#. ...#. #..#. .##..',
    'K': '#...# #..#. #.#.. ##... #.#.. #..#. #...#',
    'L': '#.... #.... #.... #.... #.... #.... #####',
    'M': '#...# ##.## #.#.# #.#.# #...# #...# #...#',
    'N': '#...# #...# ##..# #.#.# #..## #...# #...#',
    'O': '.###. #...# #...# #...# #...# #...# .###.',
    'P': '####. #...# #...# ####. #.... #.... #....',
    'Q': '.###. #...# #...# #...# #.#.# #..#. .##.#',
    'R': '####. #...# #...# ####. #.#.. #..#. #...#',
    'S': '.#### #.... #.... .###. ....# ....# ####.',
    'T': '##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..',
    'U': '#...# #...# #...# #...# #...# #...# .###.',
    'V': '#...# #...# #...# #...# #...# .#.#. ..#..',
    'W': '#...# #...# #...# #.#.# #.#.# #.#.# .#.#.',
    'X': '#...# #...# .#.#. ..#.. .#.#. #...# #...#',
    'Y': '#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..',
    'Z': '##### ....# ...#. ..#.. .#... #.... #####',
    '[': '### #.. #.. #.. #.. #.. ###',
    '\\': '#.... .#... .#... ..#.. ...#. ...#. ....#',
    ']': '### ..# ..# ..# ..# ..# ###',
    '^': '..#.. .#.#. #...# ..... ..... ..... .....',
    '_': '..... ..... ..... ..... ..... ..... #####',
    '`': '#. .# .. .. .. .. ..',
    'a': '..... ..... .###. ....# .#### #...# .####',
    'b': '#.... #.... #.##. ##..# #...# #...# ####.',
    'c': '..... ..... .###. #.... #.... #...# .###.',
    'd': '....# ....# .##.# #..## #...# #...# .####',
    'e': '..... ..... .###. #...# ##### #.... .###.',
    'f': '..## .#.. #### .#.. .#.. .#.. .#..',
    'g': '..... ..... .#### #...# #...# .#### ....# #...# .###.',
    'h': '#.... #.... #.##. ##..# #...# #...# #...#',
    'i': '.#. ... ##. .#. .#. .#. ###',
    'j': '...# .... ..## ...# ...# ...# ...# #..# .##.',
    'k': '#... #... #..# #.#. ##.. #.#. #..#',
    'l': '##. .#. .#. .#. .#. .#. ###',
    'm': '..... ..... ##.#. #.#.# #.#.# #.#.# #.#.#',
    'n': '..... ..... #.##. ##..# #...# #...# #...#',
    'o': '..... ..... .###. #...# #...# #...# .###.',
    'p': '..... ..... ####. #...# #...# ####. #.... #.... #....',
    'q': '..... ..... .#### #...# #...# .#### ....# ....# ....#',
    'r': '..... ..... #.##. ##..# #.... #.... #....',
    's': '..... ..... .#### #.... .###. ....# ####.',
    't': '.#.. .#.. #### .#.. .#.. .#.# ..#.',
    'u': '..... ..... #...# #...# #...# #..## .##.#',
    'v': '..... ..... #...# #...# #...# .#.#. ..#..',
    'w': '..... ..... #...# #...# #.#.# #.#.# .#.#.',
    'x': '..... ..... #...# .#.#. ..#.. .#.#. #...#',
    'y': '..... ..... #...# #...# #...# .#### ....# #...# .###.',
    'z': '..... ..... ##### ...#. ..#.. .#... #####',
    '{': '..# .#. .#. #.. .#. .#. ..#',
    '|': '# # # # # # #',
    '}': '#.. .#. .#. ..# .#. .#. #..',
    '~': '..... ..... .#... #.#.# ...#. ..... .....',
}

# Pairs drawn closer than their advances would put them.
KERNINGS = (('A', 'V', -1), ('V', 'A', -1), ('A', 'T', -1), ('T', 'A', -1), ('T', 'o', -1), ('L', 'T', -1),
            ('A', 'Y', -1), ('Y', 'o', -1))


def rows_of(ch):
    """The glyph's rows, each a list of booleans."""
    return [[c == '#' for c in row] for row in GLYPHS[ch].split()]


def advance_of(ch):
    """Its width, and a column between it and the next."""
    return len(rows_of(ch)[0]) + 1


def fnt_files():
    """The .fnt's text and its page's bytes."""
    page = [[(0, 0, 0, 0)] * PAGE for _ in range(PAGE)]
    chars, x, y = [], 1, 1
    for code in range(32, 127):
        ch = chr(code)
        rows = rows_of(ch)
        width, height = len(rows[0]), len(rows)
        if ch == ' ':
            chars.append((code, 0, 0, 0, 0, 0, TOP, advance_of(ch)))
            continue
        if x + width + 1 > PAGE:
            x, y = 1, y + LINE_HEIGHT + 1
        for r, row in enumerate(rows):
            for c, on in enumerate(row):
                if on:
                    page[y + r][x + c] = (255, 255, 255, 255)
        chars.append((code, x, y, width, height, 0, TOP, advance_of(ch)))
        x += width + 2
    lines = ['info face="Gizmo 5" size=10 bold=0 italic=0 charset="" unicode=1 stretchH=100 smooth=0 aa=1 '
             'padding=0,0,0,0 spacing=1,1 outline=0',
             f'common lineHeight={LINE_HEIGHT} base={BASE} scaleW={PAGE} scaleH={PAGE} pages=1 packed=0 '
             'alphaChnl=0 redChnl=4 greenChnl=4 blueChnl=4',
             'page id=0 file="gizmo5_0.png"',
             f'chars count={len(chars)}']
    for code, cx, cy, w, h, xo, yo, adv in chars:
        lines.append(f'char id={code:<4} x={cx:<5} y={cy:<5} width={w:<5} height={h:<5} xoffset={xo:<5} '
                     f'yoffset={yo:<5} xadvance={adv:<5} page=0  chnl=15')
    lines.append(f'kernings count={len(KERNINGS)}')
    for first, second, amount in KERNINGS:
        lines.append(f'kerning first={ord(first):<3} second={ord(second):<3} amount={amount}')
    return '\n'.join(lines) + '\n', png(PAGE, PAGE, page)


def image_font():
    """The image of glyphs: rows of 16 from the space, each glyph's cell its advance wide and as
    high as the tallest glyph, a key column and row around every cell."""
    height = max(len(rows_of(chr(c))) for c in range(32, 127))
    codes = list(range(32, 127))
    lines = [codes[i:i + 16] for i in range(0, len(codes), 16)]
    width = 1 + max(sum(advance_of(chr(c)) + 1 for c in line) for line in lines)
    image = [[KEY] * width for _ in range(1 + len(lines) * (height + 1))]
    for n, line in enumerate(lines):
        x, y = 1, 1 + n * (height + 1)
        for code in line:
            rows = rows_of(chr(code))
            for r in range(height):
                shade = r / (height - 1)
                color = (255, round(236 - 120 * shade), round(120 - 80 * shade), 255)  # gold to orange
                for c in range(advance_of(chr(code))):
                    on = r < len(rows) and c < len(rows[r]) and rows[r][c]
                    image[y + r][x + c] = color if on else (0, 0, 0, 0)
            x += advance_of(chr(code)) + 1
    return png(width, len(image), image)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    text, page = fnt_files()
    (OUT / 'gizmo5.fnt').write_text(text, encoding='utf-8', newline='\n')
    (OUT / 'gizmo5_0.png').write_bytes(page)
    (OUT / 'gizmo5_image.png').write_bytes(image_font())
    for name in ('gizmo5.fnt', 'gizmo5_0.png', 'gizmo5_image.png'):
        print(f'gen_bitmap_font: {(OUT / name).relative_to(ROOT)} ({(OUT / name).stat().st_size} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
