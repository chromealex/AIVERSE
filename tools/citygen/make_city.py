#!/usr/bin/env python3
"""Builds a top-down city game project from a folder of source art (agent-gta2).

    python3 tools/citygen/make_city.py --assets /Volumes/KINGSTON/GTA2 --out /Volumes/KINGSTON/GTA2/project [--seed 7]

Reads (the user's own art, nothing is taken from another game):
  <assets>/detailed-buildings/{buildings/*.glb, layouts/*.json}   buildings on a 4 x 4 x 3 m grid
  <assets>/fleet11-4096/{atlas.json, sprites/<vehicle>/<part>/*.png}   layered top-down vehicles
Writes a game folder: game.json, rules.json (topDown hero + driving), maps/city.json (street grid, blocks,
buildings, parked cars, people), tilesets/city.{json,png} (plain placeholder ground drawn here),
sprites/cars/*.png (body + closed doors + wheels, painted through the paint mask), models/buildings/*.glb.

One map cell = 4 m (rules driving.metersPerCell). Needs Pillow.
"""
import argparse
import hashlib
import json
import math
import os
import random
import shutil
import struct

from PIL import Image, ImageDraw, ImageFilter

M = 4.0                      # metres per cell
ROAD, BLOCK_W, BLOCK_H = 4, 12, 10
BLOCKS_X, BLOCKS_Z = 4, 3
TILE = 128                   # placeholder tiles
MODEL_TEXTURE = 512          # largest side of a building texture in the project
RT = 128                     # tiles cut from the road / pavement textures: 32 px per metre, enough for 4K at the game's zoom
PAINTS = {'sedan': (0.10, 0.28, 0.64), 'taxi': (0.95, 0.72, 0.08), 'police': (0.92, 0.92, 0.95), 'hatchback': (0.72, 0.12, 0.10),
          'wagon': (0.20, 0.42, 0.30), 'muscle': (0.85, 0.35, 0.05), 'limousine': (0.06, 0.06, 0.07), 'pickup': (0.45, 0.10, 0.10),
          'van': (0.80, 0.80, 0.78), 'classic': (0.35, 0.08, 0.12), 'armored': (0.30, 0.34, 0.30)}
HEALTH = {'limousine': 380, 'pickup': 400, 'van': 420, 'armored': 900, 'police': 380}
MASS = {'limousine': 1900, 'pickup': 1800, 'van': 2100, 'armored': 4200, 'muscle': 1500, 'hatchback': 1000}


def copy_model(src, dst):
    """Copies a .glb, moving its embedded images to shared files next to it (tex/<md5>.<ext>).

    The building packs embed the same eight wall / roof textures in every file: 955 MB of the 1 GB pack are
    copies. With shared files the engine loads each texture once."""
    if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src):
        return
    d = open(src, 'rb').read()
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if d[:4] != b'glTF':
        shutil.copyfile(src, dst)
        return
    jlen = struct.unpack_from('<I', d, 12)[0]
    j = json.loads(d[20:20 + jlen])
    blen = struct.unpack_from('<I', d, 20 + jlen)[0]
    blob = d[28 + jlen:28 + jlen + blen]
    views = j.get('bufferViews', [])
    drop = set()
    for im in j.get('images', []):
        if 'bufferView' not in im:
            continue
        bv = views[im['bufferView']]
        data = blob[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
        ext = {'image/png': 'png', 'image/jpeg': 'jpg'}.get(im.get('mimeType'), 'png')
        name = 'tex/%s.%s' % (hashlib.md5(data).hexdigest()[:16], ext)
        tex = os.path.join(os.path.dirname(dst), name)
        if not os.path.exists(tex):
            os.makedirs(os.path.dirname(tex), exist_ok=True)
            open(tex, 'wb').write(data)
            pic = Image.open(tex)                          # wall textures repeat every metre or so: 1254 px each is
            if max(pic.size) > MODEL_TEXTURE:              # far more than any zoom of the game shows
                pic.thumbnail((MODEL_TEXTURE, MODEL_TEXTURE), Image.LANCZOS)
                pic.save(tex)
        drop.add(im.pop('bufferView'))
        im.pop('mimeType', None)
        im['uri'] = name
    out, remap, kept = bytearray(), {}, []
    for i, bv in enumerate(views):
        if i in drop:
            continue
        while len(out) % 4:
            out.append(0)
        data = blob[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
        bv = dict(bv, byteOffset=len(out))
        out += data
        remap[i] = len(kept)
        kept.append(bv)
    for a in j.get('accessors', []):
        if 'bufferView' in a:
            a['bufferView'] = remap[a['bufferView']]
        for k in ('indices', 'values'):
            if 'sparse' in a and 'bufferView' in a['sparse'].get(k, {}):
                a['sparse'][k]['bufferView'] = remap[a['sparse'][k]['bufferView']]
    j['bufferViews'] = kept
    while len(out) % 4:
        out.append(0)
    j['buffers'] = [{'byteLength': len(out)}]
    js = json.dumps(j, separators=(',', ':')).encode()
    js += b' ' * (-len(js) % 4)
    with open(dst, 'wb') as f:
        f.write(b'glTF' + struct.pack('<II', 2, 12 + 8 + len(js) + 8 + len(out)))
        f.write(struct.pack('<I', len(js)) + b'JSON' + js)
        f.write(struct.pack('<I', len(out)) + b'BIN\0' + bytes(out))


def _digest(v):
    return hashlib.md5(json.dumps(v, sort_keys=True, ensure_ascii=False).encode()).hexdigest()


def write_shared(out, name, generated, nested=()):
    """Writes a project file that other tools and people edit too (rules.json, game.json) without undoing them.

    `.citygen.json` in the project remembers what this tool wrote last time (a digest per top-level key, and per
    entry of the `nested` objects such as rules "types"). An entry that exists and no longer matches that digest
    was changed by somebody else: it stays as it is. Entries this tool never wrote stay too. Without the memory
    file nothing that exists is overwritten. -> names of the entries that were kept."""
    path = os.path.join(out, name)
    side_path = os.path.join(out, '.citygen.json')
    side = json.load(open(side_path)) if os.path.exists(side_path) else {}
    mine = side.get(name)
    old = json.load(open(path)) if os.path.exists(path) else {}
    kept, digests = [], {}

    def merge(old_obj, new_obj, prefix):
        res = dict(old_obj)                                  # foreign entries stay, in their place
        for k, v in new_obj.items():
            key = prefix + k
            if not prefix and k in nested and isinstance(v, dict):
                res[k] = merge(old_obj.get(k, {}) if isinstance(old_obj.get(k), dict) else {}, v, k + '.')
                continue
            digests[key] = _digest(v)
            if k in old_obj and (mine is None or mine.get(key) != _digest(old_obj[k])) and _digest(old_obj[k]) != digests[key]:
                kept.append(key)                             # edited elsewhere (or never recorded as ours): theirs wins
                continue
            res[k] = v
        return res

    result = merge(old, generated, '')
    wjson(path, result)
    side[name] = digests if mine is None else dict(mine, **digests)
    wjson(side_path, side)
    return result, kept


def wjson(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(obj, f, ensure_ascii=False, indent=1)
        f.write('\n')


# ------------------------------------------------------------------ vehicles

def compose_vehicle(kit, name, out_png):
    """-> (length cells, width cells, frame w, frame h, px per metre). Wheels, body, closed doors; paint through the mask."""
    atlas = json.load(open(os.path.join(kit, 'atlas.json')))
    v = atlas['vehicles'][name]
    cw, ch = [int(round(x)) for x in v['referenceCanvasPx']]
    px, py = v['pivotPx']
    canvas = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
    wheel_png = os.path.join(kit, 'rebuild', 'shared_wheel', 'albedo.png')
    if os.path.exists(wheel_png):
        wheel = Image.open(wheel_png).convert('RGBA')
        for m in v['wheelMounts']:
            w, h = [max(1, int(round(x))) for x in m['renderSizePx']]
            im = wheel.resize((w, h), Image.LANCZOS)
            canvas.alpha_composite(im, (int(round(m['positionPx'][0] - w / 2)), int(round(m['positionPx'][1] - h / 2))))
    paint = PAINTS.get(name, v.get('defaultPaintSRGB', (0.5, 0.5, 0.5)))
    parts = ['body'] + sorted(k.split('/', 1)[1] for k in atlas['sprites'] if k.startswith(name + '/') and k.endswith('_closed'))
    body_box = None
    for part in parts:
        d = os.path.join(kit, 'sprites', name, part)
        sp = atlas['sprites'][name + '/' + part]
        alb = Image.open(os.path.join(d, 'albedo.png')).convert('RGBA')
        mask = Image.open(os.path.join(d, 'paint_mask.png')).convert('L').resize(alb.size)
        ao = Image.open(os.path.join(d, 'ao.png')).convert('L').resize(alb.size)
        r, g, b, a = alb.split()
        tinted = Image.merge('RGB', [c.point(lambda x, k=k: int(min(255, x * (0.25 + 1.5 * k)))) for c, k in zip((r, g, b), paint)])
        rgb = Image.composite(tinted, Image.merge('RGB', (r, g, b)), mask)
        shade = ao.point(lambda x: 128 + x // 2)                      # half-strength occlusion
        rgb = Image.merge('RGB', [Image.composite(c, Image.new('L', c.size, 0), shade) for c in rgb.split()])
        im = Image.merge('RGBA', (*rgb.split(), a))
        x0, y0 = int(round(px - sp['pivotPx'][0])), int(round(py - sp['pivotPx'][1]))
        layer = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
        layer.paste(im, (x0, y0))
        canvas = Image.alpha_composite(canvas, layer)
        if part == 'body':
            body_box = (x0, y0, x0 + im.width, y0 + im.height)
    bb = canvas.getbbox()
    hx = int(max(px - bb[0], bb[2] - px)) + 2
    hy = int(max(py - bb[1], bb[3] - py)) + 2
    crop = canvas.crop((int(px) - hx, int(py) - hy, int(px) + hx, int(py) + hy))   # the pivot stays in the centre
    os.makedirs(os.path.dirname(out_png), exist_ok=True)
    crop.save(out_png)
    ppm = (body_box[3] - body_box[1]) / float(v['lengthMeters'])
    return v['lengthMeters'] / M, (body_box[2] - body_box[0]) / ppm / M, crop.width, crop.height, ppm


# ------------------------------------------------------------------ ground tiles

def make_tileset(out):
    """Plain placeholder ground: asphalt, lane lines, pavement, lot concrete, grass. Drawn here."""
    rnd = random.Random(3)
    names = ['asphalt', 'asphalt2', 'line_h', 'line_v', 'pavement', 'pavement2', 'lot', 'grass']
    sheet = Image.new('RGBA', (TILE * 8, TILE), (0, 0, 0, 255))

    def speckle(base, spread, n):
        im = Image.new('RGB', (TILE, TILE), base)
        d = ImageDraw.Draw(im)
        for _ in range(n):
            x, y = rnd.randrange(TILE), rnd.randrange(TILE)
            k = rnd.randint(-spread, spread)
            d.point((x, y), tuple(max(0, min(255, c + k)) for c in base))
        return im
    for i, n in enumerate(names):
        if n.startswith('asphalt') or n.startswith('line'):
            im = speckle((58, 59, 62), 10, 2600)
            d = ImageDraw.Draw(im)
            if n == 'line_h':
                d.rectangle((0, 2, TILE, 7), fill=(196, 150, 40))        # by the road axis (the tile's top edge); inset: no bleeding into the next atlas tile
            if n == 'line_v':
                d.rectangle((2, 0, 7, TILE), fill=(196, 150, 40))
        elif n.startswith('pavement'):
            im = speckle((128, 122, 114), 8, 1800)
            d = ImageDraw.Draw(im)
            for k in range(0, TILE, TILE // 4):
                d.line((k, 0, k, TILE), fill=(104, 99, 92))
                d.line((0, k, TILE, k), fill=(104, 99, 92))
        elif n == 'lot':
            im = speckle((92, 90, 88), 8, 2000)
        else:
            im = speckle((62, 96, 48), 16, 3200)
        sheet.paste(im, (i * TILE, 0))
    os.makedirs(os.path.join(out, 'tilesets'), exist_ok=True)
    sheet.save(os.path.join(out, 'tilesets', 'city.png'))
    terr = [
        {'id': 'asphalt', 'name': 'Asphalt', 'char': 'a', 'cell': '.', 'color': [0.23, 0.23, 0.24], 'tiles': [0, 1]},
        {'id': 'line_h', 'name': 'Lane line (east-west)', 'char': 'h', 'cell': '.', 'color': [0.3, 0.28, 0.2], 'tiles': [2]},
        {'id': 'line_v', 'name': 'Lane line (north-south)', 'char': 'v', 'cell': '.', 'color': [0.3, 0.28, 0.2], 'tiles': [3]},
        {'id': 'pavement', 'name': 'Pavement', 'char': 'p', 'cell': '.', 'color': [0.5, 0.48, 0.45], 'tiles': [4, 5]},
        {'id': 'lot', 'name': 'Lot', 'char': 'l', 'cell': '.', 'color': [0.36, 0.35, 0.34], 'tiles': [6]},
        {'id': 'grass', 'name': 'Grass', 'char': 'g', 'cell': '.', 'color': [0.24, 0.38, 0.19], 'tiles': [7]},
    ]
    wjson(os.path.join(out, 'tilesets', 'city.json'), {'name': 'City', 'atlas': 'tilesets/city.png', 'tileSize': [TILE, TILE],
                                                        'columns': 8, 'filter': 'linear', 'terrains': terr})


# ------------------------------------------------------------------ the city

def weather(im, plain, seed, strength=1.0):
    """Worn paint: markings (anything much brighter than asphalt) fade towards the asphalt in blotches and specks."""
    try:
        import numpy as np
    except ImportError:
        return im
    rs = np.random.RandomState(seed)
    w, h = im.size
    a = np.asarray(im.convert('RGB')).astype(np.float32)
    p = np.asarray(plain.resize((w, h)).convert('RGB')).astype(np.float32) if plain.size != (w, h) else np.asarray(plain.convert('RGB')).astype(np.float32)
    paint = (a.max(axis=2) > 118).astype(np.float32)
    def noise(cell):
        small = Image.fromarray((rs.rand(max(2, h // cell), max(2, w // cell)) * 255).astype('uint8'))
        return np.asarray(small.resize((w, h), Image.BICUBIC)).astype(np.float32) / 255
    blot = np.clip((noise(RT // 2) * 0.6 + noise(RT // 8) * 0.4 - 0.42) * 2.4, 0, 1)      # big worn areas
    speck = (rs.rand(h, w) < 0.10).astype(np.float32) * 0.7                                  # chipped paint
    k = np.clip((0.12 + blot * 0.62 + speck) * strength, 0, 0.92) * paint
    out = a * (1 - k[..., None]) + p * k[..., None]
    return Image.fromarray(out.astype('uint8'))


def damage(im, plain, seed, cracks, patches):
    """Cracks and repaired patches that look laid, not drawn.

    Cracks: wandering lines that taper and fork, painted four times larger and scaled down (soft edges, sub-pixel
    width), darkening the asphalt under them with a faint pale rim. Patches: blobs with a ragged edge where the
    paint is gone and the asphalt is a little darker or paler, with a tar seam around."""
    try:
        import numpy as np
    except ImportError:
        return im
    rnd = random.Random(seed)
    rs = np.random.RandomState(seed)
    w, h = im.size
    a = np.asarray(im.convert('RGB')).astype(np.float32)
    p = np.asarray(plain.resize((w, h)).convert('RGB')).astype(np.float32)
    for _ in range(patches):
        pw, ph = rnd.randint(RT // 2, RT * 3 // 2), rnd.randint(RT // 3, RT)
        x, y = rnd.randint(-pw // 4, w - pw * 3 // 4), rnd.randint(-ph // 4, h - ph * 3 // 4)
        m = Image.new('L', (w, h), 0)
        ImageDraw.Draw(m).rounded_rectangle((x, y, x + pw, y + ph), radius=RT // 10, fill=255)
        soft = np.asarray(m.filter(ImageFilter.GaussianBlur(RT / 22))).astype(np.float32) / 255
        ragged = np.asarray(Image.fromarray((rs.rand(h // 6 + 1, w // 6 + 1) * 255).astype('uint8')).resize((w, h), Image.BICUBIC)).astype(np.float32) / 255
        inside = np.clip((soft + (ragged - 0.5) * 0.35 - 0.5) * 9, 0, 1)                       # ragged, slightly soft edge
        seam = np.clip(1 - np.abs(soft + (ragged - 0.5) * 0.35 - 0.5) * 14, 0, 1)              # tar along the edge
        tone = rnd.choice([0.80, 0.86, 1.16])
        grain = 1 + (rs.rand(h, w).astype(np.float32) - 0.5) * 0.10
        fresh = p * tone * grain[..., None]
        a = a * (1 - inside[..., None]) + fresh * inside[..., None]
        a *= (1 - 0.45 * seam)[..., None]
    ss = 4
    mask = Image.new('L', (w * ss, h * ss), 0)
    d = ImageDraw.Draw(mask)

    def crack(x, y, ang, length, width, depth, level):
        n = max(4, int(length / (RT * 0.05)))
        for i in range(n):
            ang += rnd.gauss(0, 0.32)
            nx, ny = x + RT * 0.05 * math.cos(ang), y + RT * 0.05 * math.sin(ang)
            wd = width * (1 - i / n) ** 0.7 * rnd.uniform(0.55, 1.25)
            d.line((x * ss, y * ss, nx * ss, ny * ss), fill=int(depth * rnd.uniform(0.6, 1.0)), width=max(1, int(round(wd * ss))))
            x, y = nx, ny
            if level < 2 and rnd.random() < 0.10:
                crack(x, y, ang + rnd.choice([-1, 1]) * rnd.uniform(0.5, 1.2), length * rnd.uniform(0.25, 0.5), width * 0.6, depth * 0.85, level + 1)

    for _ in range(cracks):
        crack(rnd.uniform(0, w), rnd.uniform(0, h), rnd.uniform(0, 6.283), RT * rnd.uniform(0.8, 3.2), RT * rnd.uniform(0.008, 0.022), 255, 0)
    m = np.asarray(mask.filter(ImageFilter.GaussianBlur(ss * 0.55)).resize((w, h), Image.LANCZOS)).astype(np.float32) / 255
    rim = np.asarray(Image.fromarray((m * 255).astype('uint8')).filter(ImageFilter.GaussianBlur(RT / 50))).astype(np.float32) / 255
    a = a * (1 - 0.72 * m)[..., None] + (np.clip(rim - m, 0, 1) * 10)[..., None]
    return Image.fromarray(np.clip(a, 0, 255).astype('uint8'))


def make_road_tileset(assets, out):
    """Ground from the user's road / pavement textures. -> tile bases, or None when the pack is missing."""
    tex = os.path.join(assets, 'roads-sidewalks', 'textures')
    need = ['road_four_lane_ns', 'road_plain_asphalt', 'walk_concrete', 'road_fine_asphalt']
    if not all(os.path.exists(os.path.join(tex, n, 'basecolor.png')) for n in need):
        return None
    def load(n, cells):
        return Image.open(os.path.join(tex, n, 'basecolor.png')).convert('RGB').resize((RT * cells, RT * cells), Image.LANCZOS)
    ns = load('road_four_lane_ns', 4)
    plain4 = load('road_plain_asphalt', 4)
    plain_s = load('road_plain_asphalt', 2)
    variants = {}                                          # key -> names of its variants (the planner picks per segment)

    def add_variants(key, im, cells, plain_ref, seed):
        """Three looks of one road piece: worn paint; + cracks; + cracks and repaired patches."""
        looks = [weather(im, plain_ref, seed), damage(weather(im, plain_ref, seed + 1, 1.2), plain_ref, seed + 2, 3 * cells, 0),
                 damage(weather(im, plain_ref, seed + 3), plain_ref, seed + 4, 2 * cells, max(1, cells // 2))]
        variants[key] = []
        for k, look in enumerate(looks):
            name = key if k == 0 else '%s~%d' % (key, k)
            variants[key].append(name)
            parts.append((name, look, cells))

    parts = [('walk', load('walk_concrete', 2), 2), ('lot', load('road_fine_asphalt', 2), 2)]
    add_variants('ns', ns, 4, plain4, 11)
    add_variants('ew', ns.rotate(90), 4, plain4, 21)
    add_variants('street', plain_s, 2, plain_s, 31)
    for key, name, seed in (('ns2', 'road_two_lane_ns', 41), ('ew2', 'road_two_lane_ew', 51)):
        if os.path.exists(os.path.join(tex, name, 'basecolor.png')):
            add_variants(key, load(name, 2), 2, plain_s, seed)
    if os.path.exists(os.path.join(tex, 'road_parking', 'basecolor.png')):
        parts.append(('parking', weather(load('road_parking', 2), plain_s, 61), 2))
    water = Image.new('RGB', (RT * 2, RT * 2), (38, 70, 92))
    dw = ImageDraw.Draw(water)
    wr = random.Random(9)
    for _ in range(900):
        x, y, k = wr.randrange(RT * 2), wr.randrange(RT * 2), wr.randint(4, 14)
        dw.line((x, y, x + wr.randint(6, 26), y), fill=(38 + k, 70 + k, 92 + k))
    parts.append(('water', water, 2))
    # Junctions are drawn here: the pack's four-lane crossroads has narrower lanes than its straight road, so the
    # markings would jump at every corner. Plain asphalt + a stop line on each arm that exists (right-hand traffic);
    # bit 1 = north arm, 2 = east, 4 = south, 8 = west. Arms that would lead off the map get nothing.
    plain = load('road_plain_asphalt', 4)
    S = RT * 4
    arm = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    da = ImageDraw.Draw(arm)
    da.rectangle((int(S * 0.045), int(S * 0.012), int(S * 0.485), int(S * 0.012) + max(3, S // 70)), fill=(198, 196, 184, 255))   # stop line
    zstep = S // 13
    for k in range(13):                                    # pedestrian crossing behind it
        da.rectangle((k * zstep + zstep // 4, int(S * 0.055), k * zstep + zstep // 4 + zstep // 2, int(S * 0.19)), fill=(198, 196, 184, 255))
    closed = Image.new('RGBA', (S, S), (0, 0, 0, 0))       # no arm on this side: the edge line runs on
    ImageDraw.Draw(closed).rectangle((int(S * 0.045), int(S * 0.045) - max(2, S // 170), S - int(S * 0.045), int(S * 0.045) + max(2, S // 170)), fill=(198, 196, 184, 255))
    corner = Image.new('RGBA', (S, S), (0, 0, 0, 0))       # the kerb line turns the north-west corner between two arms
    rc = int(S * 0.045)
    ImageDraw.Draw(corner).arc((-rc, -rc, rc, rc), 0, 90, fill=(198, 196, 184, 255), width=max(2, S // 170) * 2)
    for mask in range(1, 16):
        im = plain.convert('RGBA')
        for bit in range(4):
            im = Image.alpha_composite(im, (arm if mask & (1 << bit) else closed).rotate(-90 * bit))
            if mask & (1 << bit) and mask & (1 << ((bit + 3) % 4)):       # this arm and the one before it (anticlockwise)
                im = Image.alpha_composite(im, corner.rotate(-90 * bit))
        parts.append(('x%d' % mask, weather(im.convert('RGB'), plain, 70 + mask), 4))
    rnd = random.Random(5)
    grass = Image.new('RGB', (RT * 2, RT * 2), (58, 92, 46))
    d = ImageDraw.Draw(grass)
    for _ in range(26000):
        k = rnd.randint(-18, 18)
        d.point((rnd.randrange(RT * 2), rnd.randrange(RT * 2)), (58 + k // 2, 92 + k, 46 + k // 2))
    parts.append(('grass', grass, 2))
    # T-junctions: the road's edge line opens over the street mouth with rounded corners; the street ends with a
    # pedestrian crossing and a stop line. Groups for the planner (docs/city-planner.md "Tiles").
    plain2 = load('road_plain_asphalt', 4)
    white = (198, 196, 184)
    lw = max(2, RT // 24)

    def open_east(road, cells_w, inset, street_inset, arcs):
        """`road`: a north-south strip cells_w wide; -> 4 cells long, the east edge line open over rows 1..2."""
        im = Image.new('RGB', (cells_w * RT, 4 * RT))
        for k in range(0, 4 * RT, road.height):
            im.paste(road, (0, k))
        x_line = cells_w * RT - int(inset * RT)
        rad = int(inset * RT) if arcs else 0
        z0, z1 = int((1 + street_inset) * RT) - rad, int((3 - street_inset) * RT) + rad
        patch = plain2.crop((0, 0, int(inset * RT) + lw * 2, z1 - z0))
        im.paste(patch, (x_line - lw * 2, z0))
        if arcs:
            d = ImageDraw.Draw(im)
            xr = cells_w * RT
            d.arc((xr - rad, z0 - rad, xr + rad, z0 + rad), 90, 180, fill=white, width=lw)
            d.arc((xr - rad, z1 - rad, xr + rad, z1 + rad), 180, 270, fill=white, width=lw)
        return im

    def street_end_north(road):
        """A north-south two-cell street, its north end: zebra, then the stop line on the lane that arrives."""
        im = road.copy()
        d = ImageDraw.Draw(im)
        step = RT * 2 // 7
        for k in range(7):
            x = k * step + step // 4
            d.rectangle((x, int(0.22 * RT), x + step // 2, int(0.82 * RT)), fill=white)
        d.rectangle((RT + lw, int(1.0 * RT), 2 * RT - int(0.09 * RT), int(1.0 * RT) + lw * 2), fill=white)
        return im

    ns2 = load('road_two_lane_ns', 2) if os.path.exists(os.path.join(tex, 'road_two_lane_ns', 'basecolor.png')) else None
    groups = []
    a_e = open_east(ns, 4, 0.18, 0.085, True)
    groups += [('nsOpenE', a_e, 4, 4), ('nsOpenW', a_e.transpose(Image.FLIP_LEFT_RIGHT), 4, 4),
               ('ewOpenN', a_e.rotate(90, expand=True), 4, 4), ('ewOpenS', a_e.transpose(Image.FLIP_LEFT_RIGHT).rotate(90, expand=True), 4, 4)]
    if ns2 is not None:
        s_e = open_east(ns2, 2, 0.085, 0.085, True)
        groups += [('ns2OpenE', s_e, 2, 4), ('ns2OpenW', s_e.transpose(Image.FLIP_LEFT_RIGHT), 2, 4),
                   ('ew2OpenN', s_e.rotate(90, expand=True), 4, 2), ('ew2OpenS', s_e.transpose(Image.FLIP_LEFT_RIGHT).rotate(90, expand=True), 4, 2)]
        e_n = street_end_north(ns2)
        groups += [('ns2EndN', e_n, 2, 2), ('ns2EndS', e_n.rotate(180), 2, 2), ('ew2EndW', e_n.rotate(90), 2, 2), ('ew2EndE', e_n.rotate(270), 2, 2)]
    cols = 16
    count = sum(c * c for _, _, c in parts) + sum(w * h for _, _, w, h in groups)
    sheet = Image.new('RGB', (cols * RT, ((count + cols - 1) // cols) * RT))
    bases, i = {}, 0
    for name, im, c in parts:
        bases[name] = i
        for ty in range(c):
            for tx in range(c):
                sheet.paste(im.crop((tx * RT, ty * RT, (tx + 1) * RT, (ty + 1) * RT)), ((i % cols) * RT, (i // cols) * RT))
                i += 1
    for gi, (name, im, gw, gh) in enumerate(groups):       # rows along z, columns along x
        im = weather(im, plain2, 100 + gi)
        bases[name] = i
        for ty in range(gh):
            for tx in range(gw):
                sheet.paste(im.crop((tx * RT, ty * RT, (tx + 1) * RT, (ty + 1) * RT)), ((i % cols) * RT, (i // cols) * RT))
                i += 1
    os.makedirs(os.path.join(out, 'tilesets'), exist_ok=True)
    sheet.save(os.path.join(out, 'tilesets', 'city.png'))
    first = {k: bases[k] for k in variants}                # terrains show the first look
    for k, names in variants.items():                      # the planner gets every look and picks one per segment
        bases[k] = [bases[n] for n in names]
        for n in names[1:]:
            del bases[n]
    def terr(tid, name, ch, col, base):
        base = first.get(base, base) if isinstance(base, str) else base
        return {'id': tid, 'name': name, 'char': ch, 'cell': '.', 'color': col, 'tiles': [base]}
    wjson(os.path.join(out, 'tilesets', 'city.json'), {'name': 'City', 'atlas': 'tilesets/city.png', 'tileSize': [RT, RT], 'columns': cols,
          'filter': 'linear', 'terrains': [terr('asphalt', 'Asphalt', 'a', [0.23, 0.23, 0.24], first['ns']),
                                           terr('line_h', 'Road (east-west)', 'h', [0.23, 0.23, 0.24], first['ew']),
                                           terr('line_v', 'Road (north-south)', 'v', [0.23, 0.23, 0.24], first['ns']),
                                           terr('pavement', 'Pavement', 'p', [0.5, 0.48, 0.45], bases['walk']),
                                           terr('lot', 'Lot', 'l', [0.36, 0.35, 0.34], bases['lot']),
                                           terr('grass', 'Grass', 'g', [0.24, 0.38, 0.19], bases['grass']),
                                           terr('street', 'Street', 's', [0.23, 0.23, 0.24], first['street']),
                                           terr('parking', 'Car park', 'k', [0.25, 0.25, 0.26], bases.get('parking', first['street'])),
                                           dict(terr('water', 'Water', 'w', [0.15, 0.27, 0.36], bases['water']), cell='~'),
                                           dict(terr('built', 'Built over (blocked)', 'x', [0.36, 0.35, 0.34], bases['lot']), cell='#')]})
    return bases


def load_props(assets, out):
    """Street furniture: solid decor units. -> {id: type}, names by id."""
    src = os.path.join(assets, 'city-props')
    types = {}
    if not os.path.exists(os.path.join(src, 'catalog.json')):
        return types
    cat = json.load(open(os.path.join(src, 'catalog.json')))
    for it in (cat if isinstance(cat, list) else cat.get('props', [])):
        glb = os.path.join(src, 'models', it['id'] + '.glb')
        if not os.path.exists(glb):
            continue
        dst = os.path.join(out, 'models', 'props', it['id'] + '.glb')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(glb):
            shutil.copyfile(glb, dst)
        (x0, _, z0), (x1, _, z1) = it['bounds']
        num = int(it['id'][:2])
        small = num in (1, 2, 3, 4, 9, 10, 11, 16, 17, 18, 22, 23, 26, 27)       # things that stand on a pavement
        types['p' + it['id'][:2]] = {'name': it['id'][3:].replace('_', ' ').capitalize(),
                                     'tags': ['lamp', 'prop'] if num == 5 else ['prop'] if small else ['street_object'],
                                     'health': {'max': 100000}, 'invulnerable': True,
                                     'move': {'speed': 0, 'radius': round(max(0.03, min(x1 - x0, z1 - z0) / 2 / M), 3)}, 'sight': 0,
                                     'view': {'model': 'models/props/' + it['id'] + '.glb', 'fit': False, 'center': False, 'scale': 1 / M}}
    return types


def load_buildings(assets, out):
    if os.path.isdir(os.path.join(assets, 'buildings-hd', 'layouts')):      # the current version of all buildings
        return load_building_pack(os.path.join(assets, 'buildings-hd'), out)
    res = []
    for pack in ('detailed-buildings', 'buildings-pack03'):
        if os.path.isdir(os.path.join(assets, pack, 'layouts')):
            res += load_building_pack(os.path.join(assets, pack), out)
    return res


def load_building_pack(src, out):
    res = []
    for f in sorted(os.listdir(os.path.join(src, 'layouts'))):
        if not f.endswith('.json'):
            continue
        d = json.load(open(os.path.join(src, 'layouts', f)))
        xs = [c['cell'][0] for c in d['cells']]
        zs = [c['cell'][2] for c in d['cells']]
        nx, nz = max(xs) - min(xs) + 1, max(zs) - min(zs) + 1
        glb = os.path.join(src, 'buildings', d['id'] + '.glb')
        if not os.path.exists(glb):
            continue
        ox, oz = d.get('originOffset', [0, 0])
        cx = ox + (min(xs) + max(xs)) * M / 2      # footprint centre in model space, metres
        cz = oz + (min(zs) + max(zs)) * M / 2
        res.append({'id': 'b' + d['id'][:2], 'file': 'models/buildings/' + d['id'] + '.glb', 'nx': nx, 'nz': nz,
                    'centre': (cx, cz), 'storeys': d['storeys'], 'src': glb, 'name': d['id'][3:].replace('_', ' ').capitalize()})
    return res


def load_trees(assets, out):
    """Trees and bushes (light LOD files) as solid, non-blocking decor units. -> (tree ids, bush ids, types)."""
    src = os.path.join(assets, 'trees-3d', 'lod')
    trees, bushes, types = [], [], {}
    if not os.path.isdir(src):
        return trees, bushes, types
    for f in sorted(os.listdir(src)):
        n = int(f[:2]) if f[:2].isdigit() else 0
        if not f.endswith('.glb') or not 1 <= n <= 12:      # 01-06 trees, 07-12 bushes; hedges and grass later
            continue
        dst = os.path.join(out, 'models', 'trees', 'lod', f)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(os.path.join(src, f)):
            shutil.copyfile(os.path.join(src, f), dst)
        tid = 't%02d' % n
        (trees if n <= 6 else bushes).append(tid)
        types[tid] = {'name': f[3:-4].replace('_lod1', '').replace('_', ' ').capitalize(), 'tags': ['tree' if n <= 6 else 'bush'],
                      'health': {'max': 100000}, 'invulnerable': True,
                      'move': {'speed': 0, 'radius': 0.1 if n <= 6 else 0.15}, 'sight': 0,
                      'view': {'model': 'models/trees/lod/' + f, 'fit': False, 'center': False, 'scale': 1 / M}}
    return trees, bushes, types


def game_manifest():
    return {
        'name': 'Kingston Streets', 'id': 'com.aiverse.kingstonstreets', 'version': '0.1.0', 'build': 1, 'module': 'rts',
        'sourceLanguage': 'en', 'languages': ['en'], 'rules': 'rules.json',
        'maps': [{'file': 'maps/city.json', 'title': 'Downtown'}],
        'lighting': {'time': 0.42, 'clouds': 0.2, 'sunDir': [-0.8, 1.0, 0.5]},   # a slanted sun: shadows are visible from straight above
        'orientation': 'landscape',
        'topDown': {'touchControls': 'auto', 'mouseAim': 'hold',
                    'joystick': {'mode': 'floating', 'radius': 64, 'deadzone': 0.12, 'area': [0, 0.3, 0.5, 1], 'center': [0.14, 0.78]},
                    'aimStick': {'enabled': True, 'mode': 'floating', 'radius': 56, 'deadzone': 0.2, 'area': [0.5, 0.3, 1, 1], 'center': [0.8, 0.78]},
                    'buttons': [{'weapon': 'prev', 'anchor': [1, 0.3], 'offset': [-126, -40], 'radius': 30},
                                # weapon switch on phones, above the aim stick (keys: Z / X, 1..9)
                                {'weapon': 'next', 'anchor': [1, 0.3], 'offset': [-50, -40], 'radius': 30}],
                    # phones: the aim stick bends to the nearest target, a tap on it shoots (docs/topdown.md)
                    'aimAssist': {'touch': True, 'pad': False, 'mouse': False, 'cone': 30, 'tapFire': True},
                    # GTA camera: close on foot, pulled back and shifted ahead at speed (world units = cells of 4 m)
                    'camera': {'pitch': 90, 'yaw': 0, 'distance': 14, 'minDistance': 8, 'maxDistance': 30, 'smoothing': 6,
                               'lookAhead': 1.2, 'aimLookAhead': 1, 'lookAheadSmoothing': 2.5, 'lockOffsetIfNoInput': False,
                               'velocityLookAhead': 0.45, 'maxLookAhead': 5,
                               'speedZoom': {'from': 2, 'to': 9, 'distance': 30, 'smoothing': 1.2},
                               'bounds': True, 'margin': 2}}}


def plan_with_engine(assets, out, seed, headless, size, rules, types, building_types, buildings, bases, cars, tree_types, props, layout='grid'):
    """The layout comes from the engine's city planner (map generator preset "city"), baked into maps/city.json."""
    import subprocess
    types.update(building_types)                         # the planner chooses among all of them; unused ones are dropped after
    people_scripts_overrides(assets, out, rules, types, [])
    merged, kept = write_shared(out, 'rules.json', rules, nested=('types',))
    wjson(os.path.join(out, 'maps', 'city.json'), {'name': 'Downtown', 'size': list(size),
          'players': [{'name': 'Player', 'team': 1}, {'name': 'City', 'team': 2}], 'victory': {'mode': 'survive', 'seconds': 360000}})
    _, kept_game = write_shared(out, 'game.json', game_manifest())
    params = {'size': list(size), 'tileset': 'tilesets/city.json', 'tileBases': bases, 'chars': {'blocked': 'x'}, 'river': True, 'roundabouts': 0.3}
    if layout == 'organic':
        params.update({'layout': 'organic', 'relief': 0, 'treeBudget': 1200})   # flat for now: the ground is a tile layer
    baked = os.path.join(out, 'maps', 'city.baked.json')
    res = subprocess.run([headless, '--project', out, '--generate', 'city', '--seed', str(seed), '--gen-params', json.dumps(params),
                          '--gen-out', baked, '--max-seconds', '1', '--quiet'], capture_output=True, text=True)
    if not os.path.exists(baked):
        raise SystemExit('city planner failed:\n' + res.stdout[-2000:] + res.stderr[-2000:])
    m = json.load(open(baked))
    os.remove(baked)
    m.pop('generate', None)
    used = {e['type'] for e in m['entities']}
    by_id = {b['id']: b for b in buildings}
    for bid in building_types:
        if bid not in used:
            merged['types'].pop(bid, None)                 # the planner did not use it: no type, no model
            continue
        dst = os.path.join(out, by_id[bid]['file'])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        copy_model(by_id[bid]['src'], dst)
    wjson(os.path.join(out, 'rules.json'), merged)
    wjson(os.path.join(out, 'maps', 'city.json'), m)
    g = m.get('generated', {})
    return {'size': m['size'], 'planner': 'engine', 'buildings': g.get('buildings'), 'cars': g.get('cars'), 'people': g.get('people'),
            'roads': g.get('roads'), 'junctions': g.get('junctions'), 'bridges': g.get('bridges'), 'roundabouts': g.get('roundabouts'),
            'blocks': g.get('blocks'), 'parks': g.get('parks'), 'houses': g.get('houses'), 'paths': g.get('paths'),
            'keptAsEdited': {'rules': len(kept), 'game': kept_game, 'rulesSample': kept[:8]}, 'entities': len(m['entities']), 'warnings': g.get('warnings')}


def people_scripts_overrides(assets, out, rules, types, entities):
    # people: the characters pack's types and gait profiles when it is there (characters-3d/source/crowd-types.json)
    crowd = os.path.join(assets, 'characters-3d', 'source', 'crowd-types.json')
    if os.path.exists(crowd):
        patch = json.load(open(crowd))
        for k in ('health', 'move', 'weapons', 'sight'):          # the generator's own numbers for the hero stay
            if k in types['player'] and k in patch.get('player', {}):
                patch['player'][k] = types['player'][k]
        types.update(patch)
        profiles = [k for k in ('ped_commuter', 'ped_office', 'ped_runner', 'ped_worker', 'ped_citizen') if k in patch]
        i = 0
        for e in entities:
            if e['type'] == 'ped' and profiles:
                e['type'] = profiles[(i * 3 + i // 5) % len(profiles)]
                i += 1
    if os.path.exists(os.path.join(out, 'scripts', 'crowd.lua')):
        rules['scripts'] = ['scripts/crowd.lua']
    if os.path.exists(os.path.join(out, 'scripts', 'view', 'crowd.lua')):
        rules['viewScripts'] = ['scripts/view/crowd.lua']
    for key, name in (('scripts', 'scripts/objectives.lua'), ('viewScripts', 'scripts/view/hud.lua')):   # radar + objective markers
        if os.path.exists(os.path.join(out, name)):
            rules[key] = rules.get(key, []) + [name]
    # hand edits and other tools: rules.overrides.json in the project is merged over the generated rules
    over = os.path.join(out, 'rules.overrides.json')
    if os.path.exists(over):
        def merge(a, b):
            for k, v in b.items():
                if isinstance(v, dict) and isinstance(a.get(k), dict):
                    merge(a[k], v)
                else:
                    a[k] = v
        merge(rules, json.load(open(over)))


def build(assets, out, seed, headless=None, size=(96, 72), layout='grid', max_buildings=0):
    rnd = random.Random(seed)
    os.makedirs(out, exist_ok=True)
    bases = make_road_tileset(assets, out)
    if bases is None:
        make_tileset(out)
    kit = os.path.join(assets, 'fleet11-4096')
    atlas = json.load(open(os.path.join(kit, 'atlas.json')))
    types, driving = {}, {}
    cars = []
    for name in atlas['vehicles']:
        length, width, fw, fh, ppm = compose_vehicle(kit, name, os.path.join(out, 'sprites', 'cars', name + '.png'))
        cars.append(name)
        types[name] = {'name': name.capitalize(), 'health': {'max': HEALTH.get(name, 300)}, 'move': {'speed': 0, 'radius': round(length / 2, 3)},
                       'sight': 2,
                       'sprite': {'sheet': 'sprites/cars/%s.png' % name, 'frame': [fw, fh], 'facings': 1, 'mode': 'ground', 'rotate': True,
                                  'size': [round(fw / ppm / M, 4), round(fh / ppm / M, 4)], 'team': 'none', 'filter': 'linear', 'shadow': 'none'}}
        driving[name] = {'length': round(length, 3), 'width': round(width, 3), 'wheeled': {'massMilli': MASS.get(name, 1300) * 1000}}   # the real numbers: vehicle_handling.py
    buildings = load_buildings(assets, out)
    if max_buildings and len(buildings) > max_buildings:      # a memory knob: every k-th building of the catalogue
        buildings = [buildings[i * len(buildings) // max_buildings] for i in range(max_buildings)]
    building_types = {}
    for b in buildings:
        nb = int(b['id'][1:])
        zone = 'residential' if nb <= 6 or nb == 23 or 25 <= nb <= 31 else 'industrial' if 13 <= nb <= 18 or nb >= 44 else 'commercial'
        building_types[b['id']] = {'name': b['name'], 'tags': ['building', zone], 'structure': {'footprint': [b['nx'], b['nz']]}, 'health': {'max': 100000}, 'sight': 1,
                          'invulnerable': True,
                          'view': {'model': b['file'], 'fit': False, 'center': False, 'forward': '+x', 'scale': 1 / M,   # no turn: +x east, +z south
                                   'offset': [round(-b['centre'][0] / M, 4), 0, round(-b['centre'][1] / M, 4)]}}
    trees, bushes, tree_types = load_trees(assets, out)
    types.update(tree_types)
    props = load_props(assets, out)
    types.update(props)
    types['player'] = {'name': 'Player', 'health': {'max': 100, 'regen': 1}, 'move': {'speed': 1.6, 'radius': 0.09}, 'sight': 14,
                       'weapons': [{'id': 'pistol', 'range': 5, 'damage': 12, 'cooldown': 0.35, 'projectileSpeed': 30}],
                       'view': {'shape': 'soldier', 'scale': 0.22}}
    types['ped'] = {'name': 'Pedestrian', 'health': {'max': 40}, 'move': {'speed': 0.4, 'radius': 0.09}, 'sight': 5,
                    'view': {'shape': 'soldier', 'scale': 0.2, 'color': [0.8, 0.75, 0.6]}}
    rules = {
        'victory': 'noUnits',
        'resources': {'cash': {'name': 'Cash', 'kind': 'stock', 'start': 0}},
        'fogOfWar': {'mode': 'none'},
        'sprites': {'shadow': 'cast', 'lit': True},        # the ground and the cars take the scene light and shadows
        'topDown': {'heroes': ['player'], 'respawn': 5, 'move': {'accel': 12, 'decel': 18, 'turnRate': 720, 'collideUnits': True},
                    'input': {'sectors': 0, 'magnitudeSteps': 0, 'deadzone': 0.05},
                    'aim': {'snapCone': 12, 'fires': True}, 'autoFire': False},
        'driving': {'metersPerCell': M, 'enterRange': 0.5, 'exitSpeed': 3, 'controls': 'tank', 'wreckDamage': 40, 'doorTime': 0.5,
                    'crash': {'minSpeed': 5, 'damage': 4, 'restitution': 0.2}, 'runOver': {'minSpeed': 3, 'damage': 20},
                    'vehicles': driving},
        'types': types,
    }

    if headless and bases:
        return plan_with_engine(assets, out, seed, headless, size, rules, types, building_types, buildings, bases, cars, tree_types, props, layout)

    # street grid: roads ROAD cells wide around BLOCKS_X x BLOCKS_Z blocks; a pavement ring inside every block
    W, H = ROAD + BLOCKS_X * (BLOCK_W + ROAD), ROAD + BLOCKS_Z * (BLOCK_H + ROAD)
    tiles = [['a'] * W for _ in range(H)]
    entities = []
    for bz in range(BLOCKS_Z + 1):                       # lane lines along the road axes
        z = bz * (BLOCK_H + ROAD) + ROAD // 2
        for x in range(W):
            if (x % (BLOCK_W + ROAD)) >= ROAD:
                tiles[z][x] = 'h'
    for bx in range(BLOCKS_X + 1):
        x = bx * (BLOCK_W + ROAD) + ROAD // 2
        for z in range(H):
            if (z % (BLOCK_H + ROAD)) >= ROAD:
                tiles[z][x] = 'v'
    for bz in range(BLOCKS_Z):
        for bx in range(BLOCKS_X):
            x0, z0 = ROAD + bx * (BLOCK_W + ROAD), ROAD + bz * (BLOCK_H + ROAD)
            for z in range(z0, z0 + BLOCK_H):
                for x in range(x0, x0 + BLOCK_W):
                    edge = x in (x0, x0 + BLOCK_W - 1) or z in (z0, z0 + BLOCK_H - 1)
                    tiles[z][x] = 'p' if edge else 'l'
            if trees:                                    # street trees by the kerb, north and south sides
                for k in range(3):
                    for zz in (z0 + 0.3, z0 + BLOCK_H - 0.3):
                        entities.append({'type': rnd.choice(trees), 'owner': 0, 'pos': [x0 + 2 + k * 4, zz]})
            if rnd.random() < 0.15 or (bx, bz) == (1, 0):  # a park instead of buildings
                for z in range(z0 + 1, z0 + BLOCK_H - 1):
                    for x in range(x0 + 1, x0 + BLOCK_W - 1):
                        tiles[z][x] = 'g'
                for _ in range(10 if trees else 0):
                    entities.append({'type': rnd.choice(trees), 'owner': 0, 'pos': [round(x0 + 1.5 + rnd.random() * (BLOCK_W - 3), 2),
                                                                                    round(z0 + 1.5 + rnd.random() * (BLOCK_H - 3), 2)]})
                for _ in range(8 if bushes else 0):
                    entities.append({'type': rnd.choice(bushes), 'owner': 0, 'pos': [round(x0 + 1.3 + rnd.random() * (BLOCK_W - 2.6), 2),
                                                                                     round(z0 + 1.3 + rnd.random() * (BLOCK_H - 2.6), 2)]})
                continue
            lot_w, lot_h = BLOCK_W - 2, BLOCK_H - 2      # buildings left to right on the lot, one cell apart
            x = x0 + 1
            pool = [b for b in buildings if b['nz'] <= lot_h and b['nx'] <= lot_w]
            rnd.shuffle(pool)
            for b in pool:
                if x + b['nx'] > x0 + 1 + lot_w:
                    continue
                zc = z0 + 1 + (lot_h - b['nz']) // 2
                entities.append({'type': b['id'], 'owner': 0, 'pos': [x + b['nx'] / 2, zc + b['nz'] / 2]})
                x += b['nx'] + 1
    # parked cars in the kerb lanes: east-west streets (facing east) and north-south streets (facing north)
    for bz in range(BLOCKS_Z + 1):
        z = bz * (BLOCK_H + ROAD) + ROAD - 0.5
        for bx in range(BLOCKS_X):
            x0 = ROAD + bx * (BLOCK_W + ROAD)
            for k in range(2):
                if rnd.random() < 0.75:
                    entities.append({'type': rnd.choice(cars), 'owner': 0, 'pos': [round(x0 + 2.5 + k * 5 + rnd.random(), 2), z], 'facing': 90})
    for bx in range(BLOCKS_X + 1):
        x = bx * (BLOCK_W + ROAD) + ROAD - 0.5
        for bz in range(BLOCKS_Z):
            z0 = ROAD + bz * (BLOCK_H + ROAD)
            if rnd.random() < 0.7:
                entities.append({'type': rnd.choice(cars), 'owner': 0, 'pos': [x, round(z0 + 3 + rnd.random() * 4, 2)], 'facing': 0})
    # street furniture on the pavements: lamps by the kerb (arm over the road), a few things per block
    def prop(pid, x, z, facing=90):
        if pid in props:
            entities.append({'type': pid, 'owner': 0, 'pos': [round(x, 2), round(z, 2)], 'facing': facing})
    small = [p for p in ('p01', 'p02', 'p09', 'p10', 'p11', 'p16', 'p17', 'p27', 'p03', 'p04', 'p18') if p in props]
    for bz in range(BLOCKS_Z):
        for bx in range(BLOCKS_X):
            x0, z0 = ROAD + bx * (BLOCK_W + ROAD), ROAD + bz * (BLOCK_H + ROAD)
            for k in range(3):
                prop('p05', x0 + 0.5 + k * 4 + 3.4, z0 + 0.12, 90)
                prop('p05', x0 + 0.5 + k * 4 + 1.4, z0 + BLOCK_H - 0.12, 270)
            for _ in range(5 if small else 0):
                side = rnd.random() < 0.5
                prop(rnd.choice(small), x0 + 0.6 + rnd.random() * (BLOCK_W - 1.2), z0 + 0.72 if side else z0 + BLOCK_H - 0.72, 0 if side else 180)
            if 'p15' in props and rnd.random() < 0.4:
                prop('p15', x0 + BLOCK_W / 2, z0 + 0.6, 0)
    for bz in range(BLOCKS_Z):
        for bx in range(BLOCKS_X):
            x0, z0 = ROAD + bx * (BLOCK_W + ROAD), ROAD + bz * (BLOCK_H + ROAD)
            for _ in range(3):
                entities.append({'type': 'ped', 'owner': 2, 'pos': [round(x0 + 0.5 + rnd.random() * (BLOCK_W - 1), 2), z0 + 0.5]})
    fixed = []                                         # exact tiles of the road / pavement textures
    PX, PZ = BLOCK_W + ROAD, BLOCK_H + ROAD
    for z in range(H if bases else 0):
        for x in range(W):
            on_x, on_z = (x % PX) < ROAD, (z % PZ) < ROAD
            if on_x and on_z:
                ix, iz = x // PX, z // PZ
                mask = (1 if iz > 0 else 0) | (2 if ix < BLOCKS_X else 0) | (4 if iz < BLOCKS_Z else 0) | (8 if ix > 0 else 0)
                t = bases['x%d' % mask] + (z % PZ) * 4 + (x % PX)
            elif on_x:
                t = bases['ns'] + (z % 4) * 4 + (x % PX)
            elif on_z:
                t = bases['ew'] + (z % PZ) * 4 + (x % 4)
            else:
                t = bases[{'p': 'walk', 'l': 'lot', 'g': 'grass'}[tiles[z][x]]] + (z % 2) * 2 + (x % 2)
            fixed.append([x, z, t])
    # only the buildings that stand on the map become types and get their (large) model copied
    by_id = {b['id']: b for b in buildings}
    for bid in sorted({e['type'] for e in entities if e['type'] in by_id}):
        types[bid] = building_types[bid]
        dst = os.path.join(out, by_id[bid]['file'])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        copy_model(by_id[bid]['src'], dst)
    people_scripts_overrides(assets, out, rules, types, entities)
    wjson(os.path.join(out, 'rules.json'), rules)
    start = [ROAD + 3.5, ROAD + 0.5]
    entities.insert(0, {'type': 'player', 'owner': 1, 'pos': start})
    entities.insert(1, {'type': 'sedan', 'owner': 0, 'pos': [start[0], ROAD - 1.5], 'facing': 90})
    wjson(os.path.join(out, 'maps', 'city.json'), {
        'name': 'Downtown', 'size': [W, H],
        'players': [{'name': 'Player', 'team': 1}, {'name': 'City', 'team': 2}],
        'victory': {'mode': 'survive', 'seconds': 360000},
        'terrain': {'rows': ['.' * W for _ in range(H)]},
        'tiles': {'tileset': 'tilesets/city.json', 'rows': [''.join(r) for r in tiles], 'fixed': fixed, 'seed': seed},
        'entities': entities})
    wjson(os.path.join(out, 'game.json'), game_manifest())
    return {'size': [W, H], 'buildings': sum(1 for e in entities if e['type'][0] == 'b' and e['type'][1:].isdigit()),
            'cars': sum(1 for e in entities if e['type'] in cars), 'trees': sum(1 for e in entities if e['type'] in tree_types), 'props': sum(1 for e in entities if e['type'] in props), 'vehicleTypes': len(cars), 'buildingTypes': len(buildings)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--assets', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--seed', type=int, default=7)
    ap.add_argument('--headless', help='aiverse_headless: lay the city out with the engine planner (preset "city")')
    ap.add_argument('--size', default='96x72', help='map size in cells for the planner, WxH')
    ap.add_argument('--max-building-types', type=int, default=0, help='use at most this many building models (0: all); the HD pack is ~20 MB per model')
    ap.add_argument('--layout', default='grid', choices=['grid', 'organic'], help='organic: grid centre, curved avenues, rings, suburbs')
    a = ap.parse_args()
    w, h = [int(v) for v in a.size.lower().split('x')]
    print(json.dumps(build(os.path.expanduser(a.assets), os.path.expanduser(a.out), a.seed, a.headless, (w, h), a.layout, a.max_building_types)))
    import add_arsenal                                    # weapons, money, pickups on the pavements (rules "arsenal")
    print(json.dumps(add_arsenal.apply(os.path.expanduser(a.out), a.seed)))
    import vehicle_handling                               # every vehicle drives differently; wheels turn and roll
    print('handling:', *vehicle_handling.apply(os.path.expanduser(a.out)))
    import minimap_png                                    # the radar picture of the new map (ui/minimap.png)
    print(*minimap_png.build(os.path.expanduser(a.out)))
    import hud_atlas                                      # HUD sprites and distance digits (ui/hud.png, ui/hud_digits.png)
    hud_atlas.build(os.path.expanduser(a.out), os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'engine', 'assets', 'fonts', 'ui.ttf'))


if __name__ == '__main__':
    main()
