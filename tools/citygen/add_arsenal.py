#!/usr/bin/env python3
"""Money and weapons for the city game (rules "arsenal", docs/arsenal.md).

Adds to a project made by make_city.py: the hero's weapons (fists, pistol, machine gun, shotgun, rocket launcher),
the arsenal block, pickup types with their ground icons (sprites/pickups/*.png), money for kills (bounties of people
and cars) and pickups laid out on the pavements of maps/city.json. Run again at any time: it replaces its own types
and pickups and leaves everything else alone. make_city.py calls apply() at the end of a build.

  tools/citygen/add_arsenal.py <project> [--seed 7] [--spacing 22]
"""
import argparse
import json
import math
import os
import random

from PIL import Image, ImageDraw

WEAPONS = [   # id, name, weapon stats (cells, seconds; a cell is 4 m), slot
    ('fists', 'Fists', {'range': 0.3, 'damage': 8, 'cooldown': 0.45}, {'start': -1}),
    ('pistol', 'Pistol', {'range': 6, 'damage': 14, 'cooldown': 0.38, 'projectileSpeed': 28,
                          'flight': {'radius': 0.03, 'maxDistance': 6}}, {'start': 24, 'max': 99}),
    ('smg', 'Machine gun', {'range': 7, 'damage': 9, 'cooldown': 0.09, 'projectileSpeed': 32,
                            'flight': {'radius': 0.03, 'maxDistance': 7}}, {'start': 0, 'max': 300}),
    ('shotgun', 'Shotgun', {'range': 4, 'damage': 13, 'cooldown': 0.85, 'projectileSpeed': 26,
                            'flight': {'radius': 0.03, 'maxDistance': 4}}, {'start': 0, 'max': 40, 'pellets': 6, 'spread': 26}),
    ('rocket', 'Rocket launcher', {'range': 10, 'damage': 220, 'cooldown': 1.4, 'projectileSpeed': 11, 'splash': 1.1,
                                   'flight': {'radius': 0.06, 'maxDistance': 10}}, {'start': 0, 'max': 12}),
]
PICKUPS = {   # type id: arsenal entry, display name, colour, glyph, share of the pickups on the map
    'pickup_pistol':  ({'weapon': 'pistol', 'ammo': 24, 'respawn': 45}, 'Pistol', (70, 130, 220), 'pistol', 6),
    'pickup_smg':     ({'weapon': 'smg', 'ammo': 60, 'respawn': 60}, 'Machine gun', (225, 150, 40), 'smg', 5),
    'pickup_shotgun': ({'weapon': 'shotgun', 'ammo': 10, 'respawn': 60}, 'Shotgun', (190, 80, 60), 'shotgun', 4),
    'pickup_rocket':  ({'weapon': 'rocket', 'ammo': 3, 'respawn': 120}, 'Rocket launcher', (150, 60, 190), 'rocket', 2),
    'pickup_cash':    ({'money': 250, 'respawn': 90}, 'Cash', (60, 170, 80), 'cash', 5),
    'pickup_health':  ({'heal': 60, 'respawn': 60}, 'Health', (220, 60, 70), 'health', 4),
}
BOUNTY_PERSON, BOUNTY_CAR = 10, 100
BOUNTY_CARS = {'police': 300, 'armored': 400, 'limousine': 200, 'taxi': 150}
START = ['pickup_pistol', 'pickup_smg', 'pickup_health', 'pickup_shotgun', 'pickup_cash', 'pickup_rocket']   # near the hero, nearest first
SIZE = 0.3   # cells: 1.2 m icons


def icon(path, colour, glyph, px=128):
    """A round token seen from above: dark rim, coloured disc, white glyph."""
    s = 4
    n = px * s
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse([4 * s, 4 * s, n - 4 * s, n - 4 * s], fill=(18, 20, 24, 255))
    d.ellipse([12 * s, 12 * s, n - 12 * s, n - 12 * s], fill=colour + (255,))
    d.ellipse([18 * s, 18 * s, n - 18 * s, n - 18 * s], outline=tuple(min(255, c + 60) for c in colour) + (255,), width=2 * s)
    w = (255, 255, 255, 255)

    def box(x0, y0, x1, y1):
        d.rectangle([x0 * s, y0 * s, x1 * s, y1 * s], fill=w)

    def poly(pts):
        d.polygon([(x * s, y * s) for x, y in pts], fill=w)

    if glyph == 'pistol':
        box(34, 46, 94, 60); box(40, 60, 58, 88); poly([(58, 60), (72, 60), (64, 72), (58, 72)])
    elif glyph == 'smg':
        box(26, 48, 102, 60); box(38, 60, 52, 84); box(64, 60, 76, 92); box(92, 44, 102, 48); poly([(26, 48), (26, 70), (34, 60)])
    elif glyph == 'shotgun':
        box(24, 52, 104, 59); box(24, 62, 78, 68); poly([(24, 52), (24, 80), (40, 68), (40, 52)]); box(62, 68, 74, 74)
    elif glyph == 'rocket':
        box(30, 54, 86, 72); poly([(86, 50), (106, 63), (86, 76)]); poly([(30, 54), (20, 44), (40, 54)]); poly([(30, 72), (20, 82), (40, 72)])
    elif glyph == 'health':
        box(54, 34, 74, 94); box(34, 54, 94, 74)
    else:   # cash: a banknote
        box(30, 46, 98, 82)
        d.rectangle([36 * s, 52 * s, 92 * s, 76 * s], fill=colour + (255,))
        d.ellipse([54 * s, 54 * s, 74 * s, 74 * s], fill=w)
    im = im.resize((px, px), Image.LANCZOS)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path)


def apply_rules(project, rules):
    types = rules['types']
    heroes = rules.get('topDown', {}).get('heroes', ['player'])
    for h in heroes:
        t = types[h]
        mine = {w[0] for w in WEAPONS}
        t['weapons'] = [dict(w[2], id=w[0]) for w in WEAPONS] + [w for w in t.get('weapons', []) if w.get('id') not in mine and w.get('id') != 'pistol']
    rules.setdefault('resources', {}).setdefault('cash', {'name': 'Cash', 'kind': 'stock', 'start': 0})
    money = 'cash' if 'cash' in rules['resources'] else next(iter(rules['resources']))
    rules['arsenal'] = {
        'money': money, 'loseOnDeath': True, 'deathFine': 100, 'hitsNeutral': True, 'payNeutral': True, 'pickupReach': 0.12,
        'weapons': [dict(w[3], weapon=w[0], name=w[1]) for w in WEAPONS],
        'pickups': {pid: p[0] for pid, p in PICKUPS.items()},
    }
    for pid, (_, name, colour, glyph, _) in PICKUPS.items():
        icon(os.path.join(project, 'sprites', 'pickups', pid + '.png'), colour, glyph)
        types[pid] = {'name': name, 'health': {'max': 1}, 'invulnerable': True, 'sight': 0,
                      'move': {'speed': 0, 'radius': 0.12, 'layer': 'air'},   # nothing bumps into it, bullets fly over it
                      'sprite': {'sheet': 'sprites/pickups/%s.png' % pid, 'frame': [128, 128], 'facings': 1, 'mode': 'ground', 'lift': 0.03,
                                 'size': [SIZE, SIZE], 'team': 'none', 'filter': 'linear', 'shadow': 'none'}}
    vehicles = set(rules.get('driving', {}).get('vehicles', {}))
    for tid, t in types.items():
        if tid in vehicles:
            t['bounty'] = {money: BOUNTY_CARS.get(tid, BOUNTY_CAR)}
        elif tid == 'ped' or tid.startswith('ped_'):
            t['bounty'] = {money: BOUNTY_PERSON}
    return rules


def apply_map(m, seed, spacing):
    """Pickups on pavement cells: a starter row around the hero, then one about every `spacing` cells."""
    ents = [e for e in m['entities'] if e['type'] not in PICKUPS]
    tiles = m.get('tiles', {}).get('rows')
    terrain = m.get('terrain', {}).get('rows')
    if not tiles or not terrain:
        m['entities'] = ents
        return 0
    H, W = len(tiles), len(tiles[0])
    taken = {}
    for e in ents:
        taken.setdefault((int(e['pos'][0]), int(e['pos'][1])), []).append(e)

    def free(x, z):
        if not (1 <= x < W - 1 and 1 <= z < H - 1) or tiles[z][x] != 'p' or terrain[z][x] != '.':
            return False
        return not any((x + dx, z + dz) in taken for dx in (-1, 0, 1) for dz in (-1, 0, 1))

    placed = []

    def put(pid, x, z):
        placed.append({'type': pid, 'owner': 0, 'pos': [x + 0.5, z + 0.5]})
        taken[(x, z)] = placed[-1:]

    hero = next((e for e in ents if e.get('owner') == 1), None)
    if hero:
        hx, hz = int(hero['pos'][0]), int(hero['pos'][1])
        ring = sorted(((x, z) for x in range(hx - 12, hx + 13) for z in range(hz - 12, hz + 13)),
                      key=lambda c: (max(abs(c[0] - hx), abs(c[1] - hz)), math.atan2(c[1] - hz, c[0] - hx)))
        queue = list(START)
        last = None
        for x, z in ring:
            if not queue:
                break
            if max(abs(x - hx), abs(z - hz)) < 2 or not free(x, z) or (last and abs(x - last[0]) + abs(z - last[1]) < 3):
                continue
            put(queue.pop(0), x, z)
            last = (x, z)
    rnd = random.Random(seed * 7919 + 17)
    bag = [pid for pid, p in PICKUPS.items() for _ in range(p[4])]
    for gz in range(0, H, spacing):
        for gx in range(0, W, spacing):
            cells = [(x, z) for z in range(gz, min(H, gz + spacing)) for x in range(gx, min(W, gx + spacing)) if free(x, z)]
            if len(cells) < 8:
                continue   # no real pavement here (water, countryside)
            x, z = cells[rnd.randrange(len(cells))]
            put(bag[rnd.randrange(len(bag))], x, z)
    m['entities'] = ents + placed
    return len(placed)


def save(path, obj):
    tmp = path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
        f.write('\n')
    os.replace(tmp, path)


def apply(project, seed=7, spacing=22):
    rp = os.path.join(project, 'rules.json')
    rules = apply_rules(project, json.load(open(rp)))
    save(rp, rules)
    out = {'weapons': [w[0] for w in WEAPONS], 'pickupTypes': len(PICKUPS), 'maps': {}}
    for name in sorted(os.listdir(os.path.join(project, 'maps'))):
        if name != 'city.json':   # test maps keep their own layout
            continue
        mp = os.path.join(project, 'maps', name)
        m = json.load(open(mp))
        out['maps'][name] = apply_map(m, seed, spacing)
        save(mp, m)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('project')
    ap.add_argument('--seed', type=int, default=7)
    ap.add_argument('--spacing', type=int, default=22, help='one pickup per square of this many cells')
    a = ap.parse_args()
    print(json.dumps(apply(os.path.expanduser(a.project), a.seed, a.spacing)))


if __name__ == '__main__':
    main()
