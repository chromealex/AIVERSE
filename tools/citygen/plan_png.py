#!/usr/bin/env python3
"""Draws a city map (maps/*.json made by the planner) as a plan picture: ground kinds, buildings by zone, cars, trees.

    python3 tools/citygen/plan_png.py <project> [maps/city.json] [out.png] [--scale 4]
"""
import json
import os
import sys

from PIL import Image, ImageDraw

COL = {'a': (60, 60, 64), 's': (84, 84, 88), 'p': (170, 164, 150), 'l': (110, 108, 104), 'g': (74, 122, 62), 'w': (50, 95, 130),
       'k': (88, 88, 96), 'h': (60, 60, 64), 'v': (60, 60, 64)}
ZONE = {'residential': (182, 100, 84), 'commercial': (98, 130, 196), 'industrial': (156, 146, 92)}


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    scale = int(sys.argv[sys.argv.index('--scale') + 1]) if '--scale' in sys.argv else 4
    if '--scale' in sys.argv:
        args = [a for a in args if a != str(scale)]
    proj = args[0]
    m = json.load(open(os.path.join(proj, args[1] if len(args) > 1 else 'maps/city.json')))
    r = json.load(open(os.path.join(proj, 'rules.json')))
    w, h = m['size']
    im = Image.new('RGB', (w * scale, h * scale))
    d = ImageDraw.Draw(im)
    for z, row in enumerate(m['tiles']['rows']):
        for x, ch in enumerate(row):
            d.rectangle((x * scale, z * scale, x * scale + scale - 1, z * scale + scale - 1), fill=COL.get(ch, (255, 0, 255)))
    vehicles = set(r.get('driving', {}).get('vehicles', {}))
    for e in m['entities']:
        t = r['types'].get(e['type'], {})
        x, z = e['pos']
        tags = t.get('tags', [])
        if 'structure' in t:
            fw, fh = t['structure']['footprint']
            c = [ZONE[k] for k in tags if k in ZONE] or [(150, 150, 150)]
            d.rectangle(((x - fw / 2) * scale, (z - fh / 2) * scale, (x + fw / 2) * scale - 1, (z + fh / 2) * scale - 1), fill=c[0], outline=(20, 20, 20))
        elif e['type'] in vehicles:
            d.rectangle((x * scale - 1, z * scale - 1, x * scale + 1, z * scale + 1), fill=(235, 70, 60))
        elif 'tree' in tags or 'bush' in tags:
            d.ellipse((x * scale - 2, z * scale - 2, x * scale + 2, z * scale + 2), fill=(36, 96, 44))
    for e in m['entities'][:1]:
        x, z = e['pos']
        d.ellipse((x * scale - 6, z * scale - 6, x * scale + 6, z * scale + 6), fill=(255, 235, 0), outline=(0, 0, 0))
    out = args[2] if len(args) > 2 else os.path.join(proj, 'plan.png')
    im.save(out)
    print(out, im.size)


if __name__ == '__main__':
    main()
