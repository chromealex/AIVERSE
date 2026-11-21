#!/usr/bin/env python3
"""Road paint atlas for spline roads (docs/roads.md § Look): engine/assets/roads/markings.png.

4 x 4 cells, white paint with alpha (tinted per decal): line, arrows (straight, left, right, straight+left,
straight+right, left+right), STOP, BUS. Arrows and text point to the top of their cell (v = 0); text is stretched
along the road as painted road text is. Slot order matches roadkit::Mark (engine/roads/RoadKit.h).
Letters: Inter SemiBold (third_party/fonts/inter, SIL OFL). Needs Pillow. Usage: make_markings.py [out.png]
"""
import os, random, sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CELL, SS = 512, 4                      # cell pixels, supersampling of the shapes
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'engine', 'assets', 'roads', 'markings.png')
FONT = os.path.join(ROOT, 'third_party', 'fonts', 'inter', 'Inter-SemiBold.ttf')
LINE, ARROW_S, ARROW_L, ARROW_R, ARROW_SL, ARROW_SR, STOP, BUS, ARROW_LR = range(9)


def shape(polys):
    """white polygons (cell units 0..1) on transparent, antialiased"""
    big = Image.new('L', (CELL * SS, CELL * SS), 0)
    d = ImageDraw.Draw(big)
    for p in polys:
        d.polygon([(x * CELL * SS, y * CELL * SS) for x, y in p], fill=255)
    return big.resize((CELL, CELL), Image.LANCZOS)


def rect(u0, v0, u1, v1):
    return [(u0, v0), (u1, v0), (u1, v1), (u0, v1)]


def arrow(straight, left, right):
    p = []
    if straight:
        p += [rect(0.44, 0.34, 0.56, 0.98), [(0.5, 0.02), (0.68, 0.36), (0.32, 0.36)]]
    else:
        p += [rect(0.44, 0.52, 0.56, 0.98)]
    if left:   # the stem bends left with a rounded elbow, the head points left
        p += [rect(0.24, 0.46, 0.56, 0.58), [(0.06, 0.52), (0.26, 0.36), (0.26, 0.68)]]
    if right:
        p += [rect(0.44, 0.46, 0.76, 0.58), [(0.94, 0.52), (0.74, 0.36), (0.74, 0.68)]]
    return shape(p)


def text(word):
    """the word in the cell, letters stretched about 2.4 times along the road"""
    font = ImageFont.truetype(FONT, 400)
    l, t, r, b = font.getbbox(word)
    img = Image.new('L', (r - l + 40, b - t + 40), 0)
    ImageDraw.Draw(img).text((20 - l, 20 - t), word, font=font, fill=255)
    return img.resize((int(CELL * 0.9), int(CELL * 0.94)), Image.LANCZOS)


def worn(a, seed):
    """paint wear: a soft noise in the alpha and a few chips"""
    rnd = random.Random(seed)
    noise = Image.new('L', (CELL // 16, CELL // 16))
    noise.putdata([200 + rnd.randrange(56) for _ in range(noise.width * noise.height)])
    noise = noise.resize((CELL, CELL), Image.BICUBIC)
    chips = Image.new('L', (CELL, CELL), 255)
    d = ImageDraw.Draw(chips)
    for _ in range(60):
        x, y, r = rnd.randrange(CELL), rnd.randrange(CELL), rnd.randrange(2, 7)
        d.ellipse((x - r, y - r, x + r, y + r), fill=110)
    chips = chips.filter(ImageFilter.GaussianBlur(1.2))
    return _mul(_mul(a, noise), chips)


def _mul(a, b):
    from PIL import ImageChops
    return ImageChops.multiply(a, b)


def main():
    atlas = Image.new('RGBA', (CELL * 4, CELL * 4), (255, 255, 255, 0))
    cells = {
        LINE: shape([rect(0.06, 0.0, 0.94, 1.0)]),
        ARROW_S: arrow(True, False, False), ARROW_L: arrow(False, True, False), ARROW_R: arrow(False, False, True),
        ARROW_SL: arrow(True, True, False), ARROW_SR: arrow(True, False, True), ARROW_LR: arrow(False, True, True),
    }
    for slot, word in ((STOP, 'STOP'), (BUS, 'BUS')):
        a = Image.new('L', (CELL, CELL), 0)
        t = text(word)
        a.paste(t, ((CELL - t.width) // 2, (CELL - t.height) // 2))
        cells[slot] = a
    for slot, a in cells.items():
        a = worn(a, slot)
        tile = Image.merge('RGBA', [Image.new('L', a.size, 255)] * 3 + [a])
        atlas.paste(tile, ((slot % 4) * CELL, (slot // 4) * CELL))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    atlas.save(OUT, optimize=True)
    print(OUT, os.path.getsize(OUT), 'bytes')


if __name__ == '__main__':
    main()
