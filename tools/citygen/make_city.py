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
import json
import os
import random
import shutil

from PIL import Image, ImageDraw

M = 4.0                      # metres per cell
ROAD, BLOCK_W, BLOCK_H = 4, 12, 10
BLOCKS_X, BLOCKS_Z = 4, 3
TILE = 128
PAINTS = {'sedan': (0.10, 0.28, 0.64), 'taxi': (0.95, 0.72, 0.08), 'police': (0.92, 0.92, 0.95), 'hatchback': (0.72, 0.12, 0.10),
          'wagon': (0.20, 0.42, 0.30), 'muscle': (0.85, 0.35, 0.05), 'limousine': (0.06, 0.06, 0.07), 'pickup': (0.45, 0.10, 0.10),
          'van': (0.80, 0.80, 0.78), 'classic': (0.35, 0.08, 0.12), 'armored': (0.30, 0.34, 0.30)}
HEALTH = {'limousine': 380, 'pickup': 400, 'van': 420, 'armored': 900, 'police': 380}
MASS = {'limousine': 1900, 'pickup': 1800, 'van': 2100, 'armored': 4200, 'muscle': 1500, 'hatchback': 1000}


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
                d.rectangle((0, 0, TILE, 5), fill=(196, 150, 40))        # on the road axis = the tile's top edge
            if n == 'line_v':
                d.rectangle((0, 0, 5, TILE), fill=(196, 150, 40))
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

def load_buildings(assets, out):
    src = os.path.join(assets, 'detailed-buildings')
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
        dst = os.path.join(out, 'models', 'buildings', d['id'] + '.glb')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(glb):
            shutil.copyfile(glb, dst)
        ox, oz = d.get('originOffset', [0, 0])
        cx = ox + (min(xs) + max(xs)) * M / 2      # footprint centre in model space, metres
        cz = oz + (min(zs) + max(zs)) * M / 2
        res.append({'id': 'b' + d['id'][:2], 'file': 'models/buildings/' + d['id'] + '.glb', 'nx': nx, 'nz': nz,
                    'centre': (cx, cz), 'storeys': d['storeys'], 'name': d['id'][3:].replace('_', ' ').capitalize()})
    return res


def build(assets, out, seed):
    rnd = random.Random(seed)
    os.makedirs(out, exist_ok=True)
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
        driving[name] = {'length': round(length, 3), 'width': round(width, 3), 'wheeled': {'massMilli': MASS.get(name, 1300) * 1000}}
    buildings = load_buildings(assets, out)
    for b in buildings:
        types[b['id']] = {'name': b['name'], 'structure': {'footprint': [b['nx'], b['nz']]}, 'health': {'max': 100000}, 'sight': 1,
                          'invulnerable': True,
                          'view': {'model': b['file'], 'fit': False, 'center': False, 'scale': 1 / M,
                                   'offset': [round(-b['centre'][0] / M, 4), 0, round(-b['centre'][1] / M, 4)]}}
    types['player'] = {'name': 'Player', 'health': {'max': 100, 'regen': 1}, 'move': {'speed': 1.6, 'radius': 0.09}, 'sight': 14,
                       'weapons': [{'id': 'pistol', 'range': 5, 'damage': 12, 'cooldown': 0.35, 'projectileSpeed': 30}],
                       'view': {'shape': 'soldier', 'scale': 0.22}}
    types['ped'] = {'name': 'Pedestrian', 'health': {'max': 40}, 'move': {'speed': 0.4, 'radius': 0.09}, 'sight': 5,
                    'view': {'shape': 'soldier', 'scale': 0.2, 'color': [0.8, 0.75, 0.6]}}
    rules = {
        'victory': 'noUnits',
        'resources': {'cash': {'name': 'Cash', 'kind': 'stock', 'start': 0}},
        'fogOfWar': {'mode': 'none'},
        'topDown': {'heroes': ['player'], 'respawn': 5, 'move': {'accel': 12, 'decel': 18, 'turnRate': 720, 'collideUnits': True},
                    'input': {'sectors': 0, 'magnitudeSteps': 0, 'deadzone': 0.05},
                    'aim': {'snapCone': 12, 'fires': True}, 'autoFire': False},
        'driving': {'metersPerCell': M, 'enterRange': 0.5, 'exitSpeed': 3, 'controls': 'tank', 'wreckDamage': 40, 'doorTime': 0.5,
                    'crash': {'minSpeed': 5, 'damage': 4, 'restitution': 0.2}, 'runOver': {'minSpeed': 3, 'damage': 20},
                    'vehicles': driving},
        'types': types,
    }
    wjson(os.path.join(out, 'rules.json'), rules)

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
            if rnd.random() < 0.15:                      # a park instead of buildings
                for z in range(z0 + 1, z0 + BLOCK_H - 1):
                    for x in range(x0 + 1, x0 + BLOCK_W - 1):
                        tiles[z][x] = 'g'
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
    # parked cars along the east-west streets (kerb lane, facing east), people on the pavements
    for bz in range(BLOCKS_Z + 1):
        z = bz * (BLOCK_H + ROAD) + ROAD - 0.5
        for bx in range(BLOCKS_X):
            x0 = ROAD + bx * (BLOCK_W + ROAD)
            for k in range(2):
                if rnd.random() < 0.75:
                    entities.append({'type': rnd.choice(cars), 'owner': 0, 'pos': [round(x0 + 2.5 + k * 5 + rnd.random(), 2), z]})
    for bz in range(BLOCKS_Z):
        for bx in range(BLOCKS_X):
            x0, z0 = ROAD + bx * (BLOCK_W + ROAD), ROAD + bz * (BLOCK_H + ROAD)
            for _ in range(3):
                entities.append({'type': 'ped', 'owner': 2, 'pos': [round(x0 + 0.5 + rnd.random() * (BLOCK_W - 1), 2), z0 + 0.5]})
    start = [ROAD + 3.5, ROAD + 0.5]
    entities.insert(0, {'type': 'player', 'owner': 1, 'pos': start})
    entities.insert(1, {'type': 'sedan', 'owner': 0, 'pos': [start[0], ROAD - 1.5]})
    wjson(os.path.join(out, 'maps', 'city.json'), {
        'name': 'Downtown', 'size': [W, H],
        'players': [{'name': 'Player', 'team': 1}, {'name': 'City', 'team': 2}],
        'victory': {'mode': 'survive', 'seconds': 360000},
        'terrain': {'rows': ['.' * W for _ in range(H)]},
        'tiles': {'tileset': 'tilesets/city.json', 'rows': [''.join(r) for r in tiles], 'seed': seed},
        'entities': entities})
    wjson(os.path.join(out, 'game.json'), {
        'name': 'Kingston Streets', 'id': 'com.aiverse.kingstonstreets', 'version': '0.1.0', 'build': 1, 'module': 'rts',
        'sourceLanguage': 'en', 'languages': ['en'], 'rules': 'rules.json',
        'maps': [{'file': 'maps/city.json', 'title': 'Downtown'}],
        'lighting': {'time': 0.42, 'clouds': 0.2}, 'orientation': 'landscape',
        'topDown': {'touchControls': 'auto', 'mouseAim': 'hold',
                    'joystick': {'mode': 'floating', 'radius': 64, 'deadzone': 0.12, 'area': [0, 0.3, 0.5, 1], 'center': [0.14, 0.78]},
                    'aimStick': {'enabled': True, 'mode': 'floating', 'radius': 56, 'deadzone': 0.2, 'area': [0.5, 0.3, 1, 1], 'center': [0.8, 0.78]},
                    'buttons': [],
                    'camera': {'pitch': 80, 'yaw': 0, 'distance': 16, 'minDistance': 8, 'maxDistance': 30, 'smoothing': 6,
                               'lookAhead': 1.5, 'aimLookAhead': 1, 'lookAheadSmoothing': 3, 'bounds': True, 'margin': 2}}})
    return {'size': [W, H], 'buildings': sum(1 for e in entities if e['type'][0] == 'b' and e['type'][1:].isdigit()),
            'cars': sum(1 for e in entities if e['type'] in cars), 'vehicleTypes': len(cars), 'buildingTypes': len(buildings)}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--assets', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--seed', type=int, default=7)
    a = ap.parse_args()
    print(json.dumps(build(os.path.expanduser(a.assets), os.path.expanduser(a.out), a.seed)))


if __name__ == '__main__':
    main()
