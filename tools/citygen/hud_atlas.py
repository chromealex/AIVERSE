#!/usr/bin/env python3
"""Draws the HUD sprites of the city game (scripts/view/hud.lua): ui/hud.png — white, antialiased shapes the script
tints per objective — and ui/hud_digits.png — the distance digits in the engine's UI font.

    python3 tools/citygen/hud_atlas.py <project> [engine/assets/fonts/ui.ttf]

hud.png, 4 x 2 cells of 128 px (drawn at 64 points: 2x for dense screens):
  0 badge (dark disc with a soft shadow)   1 ring            2 arrow head (points up)   3 glow
  4 pin (points down)                      5 dashed ring     6 pill (label background)  7 dot
hud_digits.png: "0123456789mk." in cells of 40 x 56 px, each glyph centred in its cell.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

C, SS = 128, 4                      # cell size, supersampling
GLYPHS = '0123456789mk.'
GW, GH = 40, 56


def cell(draw_fn, blur=0):
    im = Image.new('RGBA', (C * SS, C * SS), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(im), C * SS)
    if blur:
        im = im.filter(ImageFilter.GaussianBlur(blur * SS))
    return im.resize((C, C), Image.LANCZOS)


def badge(d, s):
    pass


def make_badge():
    s = C * SS
    sh = Image.new('RGBA', (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse((s * 0.14, s * 0.17, s * 0.86, s * 0.89), fill=(0, 0, 0, 150))
    sh = sh.filter(ImageFilter.GaussianBlur(s * 0.035))
    d = ImageDraw.Draw(sh)
    d.ellipse((s * 0.14, s * 0.14, s * 0.86, s * 0.86), fill=(18, 22, 30, 236))
    d.ellipse((s * 0.17, s * 0.17, s * 0.83, s * 0.83), fill=(30, 36, 48, 240))        # a lighter core: reads as a bevel
    return sh.resize((C, C), Image.LANCZOS)


def ring(d, s):
    d.ellipse((s * 0.14, s * 0.14, s * 0.86, s * 0.86), outline=(255, 255, 255, 255), width=int(s * 0.04))


def arrow(d, s):
    # a rounded chevron head pointing up, with a notch at the back
    pts = [(s * 0.5, s * 0.16), (s * 0.80, s * 0.74), (s * 0.5, s * 0.60), (s * 0.20, s * 0.74)]
    d.polygon(pts, fill=(255, 255, 255, 255))
    d.line(pts + [pts[0]], fill=(255, 255, 255, 255), width=int(s * 0.05), joint='curve')


def glow(d, s):
    for i in range(40, 0, -1):
        r = s * 0.5 * i / 40
        a = int(255 * (1 - i / 40) ** 2)
        d.ellipse((s / 2 - r, s / 2 - r, s / 2 + r, s / 2 + r), fill=(255, 255, 255, a))


def pin(d, s):
    # a map pin pointing down: a disc on a tapered point, with a hole
    d.polygon([(s * 0.26, s * 0.44), (s * 0.74, s * 0.44), (s * 0.5, s * 0.92)], fill=(255, 255, 255, 255))
    d.ellipse((s * 0.20, s * 0.08, s * 0.80, s * 0.68), fill=(255, 255, 255, 255))
    d.ellipse((s * 0.38, s * 0.26, s * 0.62, s * 0.50), fill=(18, 22, 30, 255))


def dashed(d, s):
    n = 12
    for k in range(n):
        a0 = 360 / n * k
        d.arc((s * 0.10, s * 0.10, s * 0.90, s * 0.90), a0, a0 + 360 / n * 0.62, fill=(255, 255, 255, 255), width=int(s * 0.045))


def pill(d, s):
    d.rounded_rectangle((s * 0.04, s * 0.30, s * 0.96, s * 0.70), radius=s * 0.20, fill=(18, 22, 30, 225))


def dot(d, s):
    d.ellipse((s * 0.22, s * 0.22, s * 0.78, s * 0.78), fill=(18, 22, 30, 255))
    d.ellipse((s * 0.30, s * 0.30, s * 0.70, s * 0.70), fill=(255, 255, 255, 255))


def build(project, font_path):
    sheet = Image.new('RGBA', (C * 4, C * 2), (0, 0, 0, 0))
    cells = [make_badge(), cell(ring), cell(arrow), cell(glow), cell(pin), cell(dashed), cell(pill), cell(dot)]
    for i, im in enumerate(cells):
        sheet.paste(im, ((i % 4) * C, (i // 4) * C))
    os.makedirs(os.path.join(project, 'ui'), exist_ok=True)
    sheet.save(os.path.join(project, 'ui', 'hud.png'))
    font = ImageFont.truetype(font_path, 40 * SS)
    strip = Image.new('RGBA', (GW * len(GLYPHS) * SS, GH * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(strip)
    widths = {}
    for i, ch in enumerate(GLYPHS):
        box = d.textbbox((0, 0), ch, font=font, anchor='ls')
        adv = d.textlength(ch, font=font)
        widths[ch] = round(adv / SS / GW, 3)            # advance as a fraction of the cell
        d.text((i * GW * SS + (GW * SS - adv) / 2, GH * SS * 0.80), ch, font=font, fill=(255, 255, 255, 255), anchor='ls')
    strip.resize((GW * len(GLYPHS), GH), Image.LANCZOS).save(os.path.join(project, 'ui', 'hud_digits.png'))
    return widths


if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    font = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '..', '..', 'engine', 'assets', 'fonts', 'ui.ttf')
    print(build(sys.argv[1], font))
