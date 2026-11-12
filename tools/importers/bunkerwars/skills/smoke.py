#!/usr/bin/env python3
"""Smoke game for converted Bunker Wars skills: templates/spellbunkers + the converter output + stub types for the
names the output expects (bw_skeleton, bw_barrier, bw_flying_tower, bw_<kind><level>), so the engine loads every
ability and the bots cast them in a headless match.

    python3 tools/importers/bunkerwars/skills/smoke.py --skills skills.json --out /tmp/bwskills_game
    build/linux-release/aiverse_headless --project /tmp/bwskills_game --seed 3
"""
import argparse
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATE = os.path.join(HERE, '..', '..', '..', '..', 'templates', 'spellbunkers')


def load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.loads(re.sub(r'^\s*//[^\n]*', '', f.read(), flags=re.M))


def referenced(obj, key, out):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k == key and isinstance(v, str):
                out.add(v)
            referenced(v, key, out)
    elif isinstance(obj, list):
        for v in obj:
            referenced(v, key, out)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--skills', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--template', default=TEMPLATE)
    o = ap.parse_args(argv)
    if os.path.exists(o.out):
        shutil.rmtree(o.out)
    shutil.copytree(o.template, o.out)
    rules = load_json(os.path.join(o.template, 'rules.json'))
    skills = load_json(o.skills)
    res = skills.get('bwSkills', {}).get('energyResource', 'skillEnergy')
    rules['resources'][res] = {'start': 1000, 'regen': 50, 'capped': True, 'storage': 1000}
    for t in rules['types'].values():
        if 'garrison' in t:
            t['tags'] = sorted(set(t.get('tags', [])) | {'house', 'tower', 'smithy'})
    names = set()
    referenced(skills, 'unit', names)
    referenced(skills, 'to', names)
    for n in sorted(names):
        if n in rules['types']:
            continue
        if n == 'bw_barrier':
            rules['types'][n] = {'health': {'max': 20}, 'move': {'speed': 0, 'radius': 0.6}, 'blocksLanes': True}
        elif n in ('bw_skeleton',):
            rules['types'][n] = {'health': {'max': 1}, 'move': {'speed': 2, 'radius': 0.15}, 'weapons': [{'range': 0.5, 'damage': 1, 'cooldown': 1}]}
        elif n == 'bw_flying_tower':
            rules['types'][n] = {'health': {'max': 50}, 'move': {'speed': 0, 'radius': 0.6}, 'weapons': [{'range': 4, 'damage': 1, 'cooldown': 1}]}
        else:   # cast buildings
            base = dict(rules['types'].get('tower') or next(iter(rules['types'].values())))
            rules['types'][n] = base
    rules['playerAbilities'] = skills['playerAbilities']
    rules.pop('loadoutSlots', None)
    for mp in os.listdir(os.path.join(o.out, 'maps')):   # every converted ability for every player
        path = os.path.join(o.out, 'maps', mp)
        m = load_json(path)
        for pl in m.get('players', []):
            pl.pop('loadout', None)
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(m, f, ensure_ascii=False)
    with open(os.path.join(o.out, 'rules.json'), 'w', encoding='utf-8') as f:
        json.dump(rules, f, ensure_ascii=False, indent=1)
    print(f"{len(skills['playerAbilities'])} abilities -> {o.out}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
