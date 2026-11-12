#!/usr/bin/env python3
"""Draws the radar picture of a city map (maps/*.json made by the planner): ui/minimap.png, north up, `--scale` pixels
a cell. Roads light on dark blocks, buildings, parks, water, bridges — what scripts/view/hud.lua shows round the hero.

    python3 tools/citygen/minimap_png.py <project> [maps/city.json] [ui/minimap.png] [--scale 2]
"""
import json
import os
import sys

from PIL import Image

COL = {'a': (150, 152, 160), 'h': (150, 152, 160), 'v': (150, 152, 160), 's': (128, 130, 138), 'k': (98, 100, 108),
       'p': (74, 76, 84), 'l': (58, 60, 66), 'g': (44, 82, 52), 'w': (38, 84, 130), 'x': (196, 170, 122)}
EDGE = (16, 18, 24)


def build(proj, map_file='maps/city.json', out_file='ui/minimap.png', scale=2):
    m = json.load(open(os.path.join(proj, map_file)))
    w, h = m['size']
    rows = m['tiles']['rows']
    im = Image.new('RGB', (w, h), EDGE)
    px = im.load()
    for z in range(h):
        row = rows[z]
        for x in range(w):
            px[x, z] = COL.get(row[x], (58, 60, 66))
    for b in m.get('city', {}).get('bridges', []):          # bridges: road over the water
        (x0, z0), (x1, z1) = b.get('from', [0, 0]), b.get('to', [0, 0])
        n = max(abs(x1 - x0), abs(z1 - z0), 1)
        for i in range(n + 1):
            x, z = round(x0 + (x1 - x0) * i / n), round(z0 + (z1 - z0) * i / n)
            if 0 <= x < w and 0 <= z < h and rows[z][x] == 'w':
                px[x, z] = COL['a']
    im = im.resize((w * scale, h * scale), Image.NEAREST)
    px = im.load()
    for x in range(im.width):                                 # a dark frame: what shows past the map edge
        px[x, 0] = px[x, im.height - 1] = EDGE
    for z in range(im.height):
        px[0, z] = px[im.width - 1, z] = EDGE
    out = os.path.join(proj, out_file)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    im.save(out)
    return out, im.size


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    scale = 2
    if '--scale' in sys.argv:
        scale = int(sys.argv[sys.argv.index('--scale') + 1])
        args = [a for a in args if a != str(scale)]
    print(*build(args[0], args[1] if len(args) > 1 else 'maps/city.json', args[2] if len(args) > 2 else 'ui/minimap.png', scale))


if __name__ == '__main__':
    main()
