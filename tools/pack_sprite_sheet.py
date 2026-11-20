#!/usr/bin/env python3
"""Pack a folder of PNG frames into a sprite sheet: one PNG and its atlas, TexturePacker's
JSON hash, as wgf_sheet_create reads it (wgf_sheet.h).

    tools/pack_sprite_sheet.py frames/ -o sheets/hero      (writes hero.png and hero.json)

Each frame is trimmed to what isn't transparent (its place in its whole picture kept, so it
draws where it was), packed in rows, tallest first, never turned, with `--padding` texels
between (1 by default) and `--max-width` texels a row (1024). Frames named alike but for a
number (run_01.png, run_02.png) are a clip when read; `--duration` milliseconds gives every
frame a duration (none by default: 0.1 s each when read). The folder's PNGs are read in name
order: 8-bit or 16-bit, grey, grey and alpha, RGB, RGBA, or palette; interlaced ones aren't.
Standard library only.
"""
import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


def fail(message):
    sys.exit(f'pack_sprite_sheet: {message}')


def read_png(path):
    """A PNG's width, height, and RGBA bytes."""
    data = path.read_bytes()
    if data[:8] != PNG_SIGNATURE:
        fail(f'{path}: not a PNG')
    pos, idat, palette, alphas = 8, b'', None, None
    width = height = depth = color = interlace = None
    while pos < len(data):
        length, kind = struct.unpack('>I4s', data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'PLTE':
            palette = [chunk[i:i + 3] for i in range(0, len(chunk), 3)]
        elif kind == b'tRNS':
            alphas = chunk
        elif kind == b'IDAT':
            idat += chunk
        elif kind == b'IEND':
            break
    if width is None or interlace != 0 or depth not in (8, 16) or color not in (0, 2, 3, 4, 6) or (color == 3 and depth != 8):
        fail(f'{path}: a PNG of 8 or 16 bits a channel, not interlaced, is what it reads')
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    step = channels * depth // 8  # bytes a pixel, for the filters
    stride = width * step
    raw = zlib.decompress(idat)
    rows, prior = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = row[i - step] if i >= step else 0
            b = prior[i]
            c = prior[i - step] if i >= step else 0
            if kind == 1:
                row[i] = (row[i] + a) & 255
            elif kind == 2:
                row[i] = (row[i] + b) & 255
            elif kind == 3:
                row[i] = (row[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(row))
        prior = row
    rgba = bytearray()
    for row in rows:
        values = row[::2] if depth == 16 else row  # 16 bits: the high byte of each
        for x in range(width):
            v = values[x * channels:(x + 1) * channels]
            if color == 0:
                rgba += bytes((v[0], v[0], v[0], 255))
            elif color == 2:
                rgba += bytes((v[0], v[1], v[2], 255))
            elif color == 3:
                if palette is None or v[0] >= len(palette):
                    fail(f'{path}: a palette index past its palette')
                alpha = alphas[v[0]] if alphas is not None and v[0] < len(alphas) else 255
                rgba += palette[v[0]] + bytes((alpha,))
            elif color == 4:
                rgba += bytes((v[0], v[0], v[0], v[1]))
            else:
                rgba += bytes(v)
    return width, height, bytes(rgba)


def write_png(path, width, height, rgba):
    """RGBA as a PNG, each row unfiltered."""
    raw = b''.join(b'\x00' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    path.write_bytes(PNG_SIGNATURE + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def trim(width, height, rgba):
    """What isn't transparent: x, y, width, height (the whole picture's 1 by 1 corner when it all is)."""
    xs = [x for y in range(height) for x in range(width) if rgba[(y * width + x) * 4 + 3]]
    ys = [y for y in range(height) for x in range(width) if rgba[(y * width + x) * 4 + 3]]
    if not xs:
        return 0, 0, 1, 1
    return min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1


def pack(folder, out, padding, max_width, duration):
    files = sorted(p for p in Path(folder).iterdir() if p.suffix.lower() == '.png')
    if not files:
        fail(f'{folder}: no PNG frames in it')
    frames = []
    for path in files:
        w, h, rgba = read_png(path)
        tx, ty, tw, th = trim(w, h, rgba)
        frames.append({'name': path.name, 'w': w, 'h': h, 'rgba': rgba, 'trim': (tx, ty, tw, th)})
    # rows, tallest first: each frame where the row has room, else a new row under it
    order = sorted(range(len(frames)), key=lambda i: -frames[i]['trim'][3])
    x = y = row_height = sheet_width = 0
    for i in order:
        tw, th = frames[i]['trim'][2], frames[i]['trim'][3]
        if tw > max_width:
            fail(f'{frames[i]["name"]}: {tw} texels wide, past --max-width {max_width}')
        if x > 0 and x + tw > max_width:
            x, y, row_height = 0, y + row_height + padding, 0
        frames[i]['at'] = (x, y)
        x += tw + padding
        row_height = max(row_height, th)
        sheet_width = max(sheet_width, x - padding)
    sheet_height = y + row_height
    pixels = bytearray(sheet_width * sheet_height * 4)
    for f in frames:
        (ax, ay), (tx, ty, tw, th) = f['at'], f['trim']
        for row in range(th):
            src = ((ty + row) * f['w'] + tx) * 4
            dst = ((ay + row) * sheet_width + ax) * 4
            pixels[dst:dst + tw * 4] = f['rgba'][src:src + tw * 4]
    out = Path(out)
    image = out.with_suffix('.png')
    write_png(image, sheet_width, sheet_height, bytes(pixels))
    atlas = {'frames': {}, 'meta': {'image': image.name, 'size': {'w': sheet_width, 'h': sheet_height},
                                     'app': 'libwgf tools/pack_sprite_sheet.py', 'format': 'RGBA8888'}}
    for f in frames:  # in name order, as the folder was read
        (ax, ay), (tx, ty, tw, th) = f['at'], f['trim']
        entry = {'frame': {'x': ax, 'y': ay, 'w': tw, 'h': th}, 'rotated': False,
                 'trimmed': (tx, ty, tw, th) != (0, 0, f['w'], f['h']),
                 'spriteSourceSize': {'x': tx, 'y': ty, 'w': tw, 'h': th}, 'sourceSize': {'w': f['w'], 'h': f['h']}}
        if duration > 0:
            entry['duration'] = duration
        atlas['frames'][f['name']] = entry
    out.with_suffix('.json').write_text(json.dumps(atlas, indent=1) + '\n', encoding='utf-8')
    return f'pack_sprite_sheet: wrote {image} ({sheet_width} x {sheet_height}) and {out.with_suffix(".json")}, {len(frames)} frame(s)'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('folder', help='the frames, PNGs')
    parser.add_argument('-o', dest='out', required=True, help='the sheet, without its extension (name.png and name.json)')
    parser.add_argument('--padding', type=int, default=1, help='texels between frames (1)')
    parser.add_argument('--max-width', type=int, default=1024, help='texels a row at most (1024)')
    parser.add_argument('--duration', type=int, default=0, help='each frame\'s milliseconds (none: 0.1 s each when read)')
    opts = parser.parse_args()
    if opts.padding < 0 or opts.max_width <= 0 or opts.duration < 0:
        fail('padding and duration are 0 or more, and max-width above 0')
    print(pack(opts.folder, opts.out, opts.padding, opts.max_width, opts.duration))


if __name__ == '__main__':
    main()
