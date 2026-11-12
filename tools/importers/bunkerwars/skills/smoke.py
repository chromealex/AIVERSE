#!/usr/bin/env python3
"""Smoke game for converted Bunker Wars skills: a copy of templates/bunkerwars with the converter output merged in
(convert.py --template), plenty of energy and no loadout limit, so every player owns every ability and the bots cast
them all in a headless match.

    python3 tools/importers/bunkerwars/skills/convert.py --bw <client> --out skills.json --template /tmp/bwsmoke   (a copy)
    python3 tools/importers/bunkerwars/skills/smoke.py --game /tmp/bwsmoke
    build/linux-release/aiverse_headless --project /tmp/bwsmoke --seed 3      (run twice: same checksum)
"""
import argparse
import json
import os
import re
import sys


def load_json(path):
    with open(path, encoding='utf-8') as f:
        return json.loads(re.sub(r'^\s*//[^\n]*', '', f.read(), flags=re.M))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True, help='a COPY of templates/bunkerwars after convert.py --template')
    ap.add_argument('--energy', default='energy')
    o = ap.parse_args(argv)
    rp = os.path.join(o.game, 'rules.json')
    rules = load_json(rp)
    rules['resources'][o.energy] = dict(rules['resources'].get(o.energy, {}), start=1000, regen=50, capped=True, storage=1000)
    rules.pop('loadoutSlots', None)
    md = os.path.join(o.game, 'maps')
    for mp in os.listdir(md):   # every converted ability for every player
        if not mp.endswith('.json'):
            continue
        path = os.path.join(md, mp)
        m = load_json(path)
        for pl in m.get('players', []):
            pl.pop('loadout', None)
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(m, f, ensure_ascii=False)
    with open(rp, 'w', encoding='utf-8') as f:
        json.dump(rules, f, ensure_ascii=False, indent=1)
    print(f"{len(rules.get('playerAbilities', []))} abilities for every player -> {o.game}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
