#!/usr/bin/env python3
"""Bunker Wars energy skills -> AIVERSE rules.json player abilities (all skills, talents, campaign / map skills,
with every upgrade level). Reads the user's own Bunker Wars client checkout (read-only); writes external data.

    python3 tools/importers/bunkerwars/skills/convert.py --bw ~/Projects/bunker-wars --out skills.json \
        [--report coverage.md] [--px-per-cell 50] [--energy-resource skillEnergy] [--legacy]

Output: {"playerAbilities": [...], "bwSkills": {...}} to merge into a game's rules.json (the bunker wars game
importer merges it; `rules_merge` over MCP does the same). Mapping table: docs/bunkerwars-skills.md.
"""
import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bwproject  # noqa: E402
import mapping as MP  # noqa: E402
import skilldata as SD  # noqa: E402
from unityyaml import num, ref_id  # noqa: E402

TG = 'ME.TexturesGenerator'
HEADERS = TG + '/_DatabaseData/GameInfo/SkillsHeaders'
HEROES = TG + '/_DatabaseData/GameInfo/_Heroes'
SCAN_ASSETS = [TG + '/_DatabaseData/GameInfo', TG + '/_Gameplay/SkillsData', TG + '/_Gameplay/MapsEnvironment', TG + '/Heroes_DELETE']
SCAN_SCRIPTS = ['MW2Data/Gameplay/Database']


def open_project(bw):
    assets = bw
    for sub in (('Client', 'Assets'), ('Assets',)):   # the repository, its Client folder or Client/Assets itself
        if os.path.isdir(os.path.join(bw, *sub)):
            assets = os.path.join(bw, *sub)
            break
    p = bwproject.Project(assets)
    p.index_assets(*[d for d in SCAN_ASSETS if os.path.isdir(os.path.join(assets, d))])
    p.index_scripts(*SCAN_SCRIPTS)
    p.load_gamedata(os.path.join(assets, 'MW2Data/Resources/MW2.Modules/GameData/GameDataSettings.asset'))
    return p


# ------------------------------------------------------------------------------------------------ filters

def filter_keys(proj, skill, flt, px, notes):
    """(keys, kind) for an element filter. kind: 'structures' | 'units' | 'players' | 'point' | 'zone'."""
    if flt is None:
        return {}, 'point'
    c, f = flt.cls, flt.f
    rng = num(f.get('inRange'), -1)
    keys = {}
    if c in ('SkillPointsFilter', 'SkillPointsFilterBase', 'SkillRandomPointsFilter'):
        keys['kind'] = 'structure'
        keys.update(MP.owner_keys(f.get('ownerFilter')))
        if keys.get('affects') == 'self':
            keys['affects'] = 'own'
        tags = MP.point_tags(f.get('typeFilter'))
        if tags:
            keys['tags'] = tags
        lf, lt = int(num(f.get('levelFrom'), -1)), int(num(f.get('levelTo'), -1))
        if lf >= 0 or lt >= 0:
            keys['level'] = [max(lf, 0), lt if lt >= 0 else 99]
        if c == 'SkillRandomPointsFilter':
            keys.update(shape='map', sort='random')
        elif rng >= 0:
            keys['radius'] = round(rng / px, 3)
            if not num(f.get('oneNearest')):
                keys['aspect'] = ASPECT[0]   # SkillPointsFilter.GetPoints: IsInEllipse(range, range x aspectRatio)
        else:
            keys['shape'] = 'map'
        if num(f.get('oneNearest')):
            keys.update(maxTargets=1, sort='nearest')
        ma = int(num(f.get('maxAmount'), -1))
        if ma >= 0:
            keys['maxTargets'] = ma
        sd, sl = int(num(f.get('sortByDistance'))), int(num(f.get('sortByLevel')))
        if sd:
            keys['sort'] = 'nearest' if sd == 1 else 'farthest'
        if sl:
            keys['sort'] = 'levelLow' if sl == 1 else 'levelHigh'
        if num(f.get('affectsOnlyFirstPlayerPoints')):
            notes.append('affectsOnlyFirstPlayerPoints: structures of the first matched player only (not converted)')
        if num(f.get('selectPointOwners')):
            return keys, 'players'
        return keys, 'structures'
    if c == 'SkillUnitsFilter':
        keys.update(kind='unit', inTransit=True)
        keys.update(MP.owner_keys(f.get('ownerFilter'), unit=True))
        if rng >= 0:
            keys['radius'] = round(rng / px, 3)
            keys['aspect'] = ASPECT[0]       # SkillUnitsFilter: IsInEllipse(range, range x aspectRatio)
        else:
            keys['shape'] = 'map'
        rw = num(f.get('ringWidth'), -1)
        if rw > 0 and rng >= 0:
            keys.update(shape='ring', ring=round(rw / px, 3))
        mc = int(num(f.get('maxCount'), -1))
        if mc >= 0:
            keys['maxTargets'] = mc
        return keys, 'units'
    if c == 'SkillPlayersFilter':
        keys.update(MP.owner_keys(f.get('playerFilter')))
        return keys, 'players'
    if c == 'SkillZoneFilter':
        keys.update(shape='map', area='bw_zone_%d' % int(num(f.get('zoneType'))))
        keys.update(MP.owner_keys(f.get('ownerFilter'), unit=True))
        notes.append('zone filter: named map area bw_zone_<type> (map areas from the environment importer)')
        return keys, 'zone'
    return keys, 'point'


ASPECT = [0.5]   # BW world.aspectRatio (BattleParametersInfo); --aspect


def intersect_keys(a, b):
    """Both filters must pass (BW modificator executeFilter): the narrower of each key."""
    if a.get('kind') and b.get('kind') and a['kind'] != b['kind']:
        return None
    out = dict(a)
    for k, v in b.items():
        if k not in out:
            out[k] = v
        elif k == 'radius':
            out[k] = min(out[k], v)
        elif k == 'maxTargets':
            out[k] = min(out[k], v)
        elif k == 'tags':
            t = [x for x in out[k] if x in v]
            if not t:
                return None
            out[k] = t
        elif k == 'affects' and out[k] != v:
            out[k] = v if out[k] in ('all', 'any') else out[k] if v in ('all', 'any') else v
        elif k == 'level':
            out[k] = [max(out[k][0], v[0]), min(out[k][1], v[1])]
        elif k == 'shape' and 'target' in (out[k], v):
            out[k] = 'target'
    if out.get('shape') == 'map' and 'radius' in out and not (a.get('shape') == 'map' and b.get('shape') == 'map'):
        out.pop('shape')   # one of them has a range: the intersection is that circle
    return out


def modificator_filter(proj, skill, e, m, ekeys, dur, px, notes):
    """The filter a modificator uses (SkillModificatorInfoBase.GetObjects): the element filter, its intersection with
    an enabled executeFilter, or with ignoreSkillFilter the executeFilter alone (filter copiers copy the element range)."""
    xf = skill.comps.get(ref_id(m.f.get('executeFilter'))) if num(m.f.get('executeFilterEnabled')) else None
    if xf is None:
        return ekeys
    xkeys, _ = filter_keys(proj, skill, xf, px, notes)
    cp = skill.comps.get(ref_id(m.f.get('skillCopier'))) if num(m.f.get('useSkillCopier')) else None
    if cp is not None and e.filter is not None:
        er = num(e.filter.f.get('inRange'), -1)
        if cp.cls == 'SkillFilterVelocityToRangeCopier':   # range + timeout x rangeVelocity of a units filter
            rv = num(e.filter.f.get('rangeVelocity'), -1)
            if e.filter.cls == 'SkillUnitsFilter' and dur > 0 and er >= 0 and rv > 0:
                er = er + dur * rv
        if er >= 0:
            xkeys['radius'] = round(er / px, 3)
            xkeys.pop('shape', None) if xkeys.get('shape') == 'map' else None
    if num(m.f.get('ignoreSkillFilter')):
        return xkeys
    return intersect_keys(ekeys, xkeys)


def cond_time(conds, cls):
    for d in conds:
        if d.cls == cls:
            return num(d.f.get('time'))
        if d.cls == 'SkillModificatorConditionalTwoStage' and cls == 'SkillModificatorConditionTimeout':
            return num(d.f.get('timeFirstStage')) + num(d.f.get('timeSecondStage'))
    return 0.0


# ------------------------------------------------------------------------------------------------ abilities

def make_ability(proj, skill, px, opts):
    """One level of a skill -> (ability dict, per-modificator coverage rows)."""
    notes, rows, effects = list(skill.notes), [], []
    target = 'none'
    clicked = False
    for e in skill.elements:
        if not e.mods:
            continue
        conds = [d.cls for d in e.activate]
        ekeys, ekind = filter_keys(proj, skill, e.filter, px, notes)
        dur = cond_time(e.deactivate, 'SkillModificatorConditionTimeout') or cond_time(e.deactivate, 'SkillModificatorConditionTimeoutFromDuration')
        if 'SkillModificatorConditionMapClick' in conds:
            clicked = True
            if ekind == 'structures' and ekeys.get('maxTargets') == 1:
                target = 'structure'
                ekeys = {k: v for k, v in ekeys.items() if k not in ('radius', 'sort', 'maxTargets')}
                ekeys['shape'] = 'target'
            elif target == 'none':
                target = 'point'
        elif ekind in ('structures', 'units') and 'shape' not in ekeys:
            ekeys['shape'] = 'map'      # instant skills: BW measures from the hero/screen; the whole map here
            ekeys.pop('radius', None)
        if ekind == 'players' and target == 'none':
            pass
        delay = cond_time(e.activate, 'SkillModificatorConditionTimeout')
        el_effects = []
        for m in e.mods:
            mkeys = modificator_filter(proj, skill, e, m, ekeys, dur, px, notes)
            if mkeys is None:   # the two filters exclude each other: nothing is affected
                rows.append((m.cls, 'exact', ''))
                continue
            ctx = MP.Ctx(proj, skill, e, mkeys, dur, px)
            ctx.star = opts.morale_star_points
            ctx.aspect = ASPECT[0]
            rv = num(e.filter.f.get('rangeVelocity'), -1) if e.filter is not None and e.filter.cls == 'SkillUnitsFilter' else -1
            ctx.grow = rv / px if rv > 0 else 0.0
            ctx.el_notes = 'growing' if rv > 0 else ''
            out = MP.convert_modificator(m, ctx)
            el_effects += out
            rows.append((m.cls, ctx.fidelity, '; '.join(ctx.notes)))
        if delay > 0 and el_effects and e.spends is False:
            el_effects = [{'type': 'area', 'delay': round(delay, 3), 'radius': 0.5, 'effects': el_effects}]
        effects += el_effects
    a = {'target': target, 'cooldown': round(skill.cooldown, 3)}
    if skill.initial_cooldown:
        a['initialCooldown'] = round(skill.initial_cooldown, 3)
    if not clicked and target == 'none':
        a['smart'] = 'self'
    a['effects'] = effects
    return a, rows, notes


def uniquify_statuses(obj, prefix):
    if isinstance(obj, dict):
        st = obj.get('status')
        if isinstance(st, dict) and 'id' in st and not st['id'].startswith(prefix):
            st['id'] = prefix + '_' + st['id']
        for v in obj.values():
            uniquify_statuses(v, prefix)
    elif isinstance(obj, list):
        for v in obj:
            uniquify_statuses(v, prefix)


def header_levels(proj, path):
    """[(level, level prefab path)] of a skill header / talent header asset."""
    out = []
    for fid, (u, body) in proj.docs(path).items():
        for lv in body.get('levels') or []:
            p = proj.path_of(lv.get('skill')) if isinstance(lv, dict) else None
            if p:
                out.append((int(num(lv.get('level'), len(out) + 1)), p))
    return sorted(out)


def base_of_level(proj, level_path):
    """The SkillData prefab a level prefab upgrades (level -> _Desc -> skillResource)."""
    for fid, (u, body) in proj.docs(level_path).items():
        if u != 'MonoBehaviour' or 'skillDescription' not in body:
            continue
        desc_path = proj.path_of(body.get('skillDescription'))
        if not desc_path:
            continue
        for _, (u2, b2) in proj.docs(desc_path).items():
            res = b2.get('skillResource') if isinstance(b2, dict) else None
            if isinstance(res, dict):
                rp = res.get('resourcePath')
                p = proj.path_of({'guid': res.get('guid')}) or (os.path.join(proj.assets, rp[len('Assets/'):]) if rp else None)
                return p, num(b2.get('cost')), b2
    return None, 0, None


def slug(name):
    s = re.sub(r'^_+', '', name)
    s = re.sub(r'_Config_|_Config', '_', s)
    return re.sub(r'[^A-Za-z0-9]+', '_', s).strip('_').lower()


def convert(proj, opts):
    abilities, coverage, report = [], {}, []
    headers = sorted(glob.glob(os.path.join(proj.assets, HEADERS, '*.asset')) + glob.glob(os.path.join(proj.assets, HEADERS, '_CampaignSkills', '*.asset')))
    headers += sorted(glob.glob(os.path.join(proj.assets, HEROES, '**', '*-Talent*.asset'), recursive=True))
    seen_bases = set()
    for h in headers:
        levels = header_levels(proj, h)
        if not levels:
            continue
        hid = os.path.basename(h)[:-6]
        base_path, cost, desc = base_of_level(proj, levels[0][1])
        if not base_path or not os.path.exists(base_path):
            report.append(f'{hid}: base skill prefab not found')
            continue
        base = SD.load_skill(proj, base_path)
        if not base:
            report.append(f'{hid}: no SkillData in {base_path}')
            continue
        seen_bases.add(os.path.abspath(base_path))
        aid = slug(hid)
        per_level = []
        for lv, lp in levels:
            sk = SD.apply_level(proj, base, lp)
            a, rows, notes = make_ability(proj, sk, opts.px_per_cell, opts)
            per_level.append(a)
            for cls, fid, note in rows:
                cov = coverage.setdefault(cls, {'skills': set(), 'fidelity': fid, 'notes': set()})
                cov['skills'].add(aid)
                if MP.FIDELITY.index(fid) > MP.FIDELITY.index(cov['fidelity']):
                    cov['fidelity'] = fid
                if note:
                    cov['notes'].add(note)
            if lv == levels[0][0]:
                for n in notes:
                    report.append(f'{aid}: {n}')
        ab = {'id': aid, 'name': base.name.strip('_') or aid, 'bw': {'clientId': base.client_id, 'header': hid, 'source': os.path.relpath(base_path, proj.assets)}}
        ab.update(per_level[0])
        if cost and opts.energy_resource:
            ab['cost'] = {opts.energy_resource: round(cost, 3)}
        if len(per_level) > 1:
            ab['levels'] = per_level[1:]
        uniquify_statuses(ab, aid)
        abilities.append(ab)
    # skills without headers: map / campaign environment skills and (with --legacy) deleted hero skills
    extra_dirs = [TG + '/_Gameplay/SkillsData', TG + '/_Gameplay/MapsEnvironment'] + ([TG + '/Heroes_DELETE'] if opts.legacy else [])
    for d in extra_dirs:
        for p in sorted(glob.glob(os.path.join(proj.assets, d, '**', '*.prefab'), recursive=True)):
            if os.path.abspath(p) in seen_bases:
                continue
            with open(p, encoding='utf-8', errors='replace') as fh:
                if '59c223b35ff2c8f4591ed9e8d4e83165' not in fh.read():   # SkillData script guid
                    continue
            sk = SD.load_skill(proj, p)
            if not sk:
                continue
            aid = ('legacy_' if 'Heroes_DELETE' in p else 'map_' if 'MapsEnvironment' in p else 'extra_') + slug(os.path.basename(p)[:-7])
            a, rows, notes = make_ability(proj, sk, opts.px_per_cell, opts)
            ab = {'id': aid, 'name': sk.name.strip('_'), 'bw': {'clientId': sk.client_id, 'source': os.path.relpath(p, proj.assets)}}
            ab.update(a)
            uniquify_statuses(ab, aid)
            abilities.append(ab)
            for cls, fid, note in rows:
                cov = coverage.setdefault(cls, {'skills': set(), 'fidelity': fid, 'notes': set()})
                cov['skills'].add(aid)
                if note:
                    cov['notes'].add(note)
    ids = {}
    for a in abilities:
        n = ids.get(a['id'], 0)
        ids[a['id']] = n + 1
        if n:
            a['id'] += '_%d' % (n + 1)
    return abilities, coverage, report


def referenced(obj, key, out):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k == key and isinstance(v, str):
                out.add(v)
            referenced(v, key, out)
    elif isinstance(obj, list):
        for v in obj:
            referenced(v, key, out)


def helper_types(abilities, opts):
    """Types the skills spawn that the building importer does not make (BW objects of skills)."""
    names, out = set(), {}
    referenced(abilities, 'unit', names)
    px = opts.px_per_cell
    if 'bw_barrier' in names:   # PointsBarrier wall: blocks the lanes, worn down by the walkers that hit it
        out['bw_barrier'] = {'name': 'Barrier', 'health': {'max': 20}, 'move': {'speed': 0, 'radius': 0.6}, 'blocksLanes': True, 'view': {'shape': 'wall'}}
    if 'ufo' in names:          # PositionFlyingTower (agent-bw-routes): range 600 px, aspect, 3.5 shots / s, bullet 300 px / s
        out['ufo'] = {'name': 'UFO', 'structure': {'footprint': [1, 1]}, 'invulnerable': True, 'sight': round(600 / px, 3), 'tags': ['tower'],
                      'weapons': [{'range': round(600 / px, 3), 'rangeAspect': opts.aspect, 'damage': 1, 'cooldown': 0.2857, 'projectileSpeed': round(300 / px, 3)}],
                      'view': {'shape': 'tower', 'scale': 0.8}}
    return out


def strip_comments(text):
    head = []
    for ln in text.split('\n'):
        if ln.lstrip().startswith('//'):
            head.append(ln)
        else:
            break
    return head, json.loads('\n'.join(l for l in text.split('\n') if not l.lstrip().startswith('//')))


def default_loadout(skills_meta, slot_cards, talent, ability_ids):
    """Four cards (first common card allowed in each BW slot) + the hero talent in slot 4."""
    lo = []
    for k in range(4):
        for sid, allowed, rarity in slot_cards:
            if allowed is not None and (int(allowed) >> k) & 1 and sid in ability_ids and sid not in [x['id'] for x in lo]:
                lo.append({'id': sid, 'slot': k})
                break
    if talent in ability_ids:
        lo.append({'id': talent, 'slot': 4})
    return lo


def merge_template(folder, out):
    """Write the abilities into <folder>/rules.json (helper types too) and give map players a default loadout."""
    rp = os.path.join(folder, 'rules.json')
    with open(rp, encoding='utf-8') as f:
        head, rules = strip_comments(f.read())
    rules['playerAbilities'] = out['playerAbilities']
    for k, v in out['types'].items():
        rules.setdefault('types', {}).setdefault(k, v)
    rules.setdefault('loadoutSlots', 5)
    # the skills read / write `morale` (stars = resource levels); the morale core (decay, level bonuses) is the modes
    # agent's: only make sure the resource and its level thresholds exist
    mor_p = os.path.join(folder, 'bw', 'morale.json')
    if 'morale' not in rules.setdefault('resources', {}) and os.path.exists(mor_p):
        with open(mor_p, encoding='utf-8') as f:
            lv = strip_comments(f.read())[1].get('levels', [])
        pts = [x.get('requiredPoints', 0) for x in lv]
        if pts:
            rules['resources']['morale'] = {'start': 0, 'capped': True, 'storage': max(pts), 'levels': [{'at': p} for p in pts]}
    with open(rp, 'w', encoding='utf-8') as f:
        f.write('\n'.join(head + ['// playerAbilities: tools/importers/bunkerwars/skills/convert.py (every BW skill card, talent, campaign / map skill)']) + '\n')
        json.dump(rules, f, ensure_ascii=False, indent=1)
        f.write('\n')
    meta_p = os.path.join(folder, 'bw', 'skills.json')
    heroes_p = os.path.join(folder, 'bw', 'heroes.json')
    cards, talents = [], []
    if os.path.exists(meta_p):
        with open(meta_p, encoding='utf-8') as f:
            meta = strip_comments(f.read())[1].get('skills', [])
        cards = [(x['id'], x.get('allowedSlot'), x.get('rarity')) for x in meta if x.get('kind') == 'card' and x.get('rarity') == 0]
    if os.path.exists(heroes_p):
        with open(heroes_p, encoding='utf-8') as f:
            talents = [slug(h['talent']) for h in strip_comments(f.read())[1].get('heroes', []) if h.get('talent')]
    ids = {a['id'] for a in out['playerAbilities']}
    md = os.path.join(folder, 'maps')
    for name in sorted(os.listdir(md)) if os.path.isdir(md) else []:
        mp = os.path.join(md, name)
        if not name.endswith('.json'):
            continue
        with open(mp, encoding='utf-8') as f:
            mhead, m = strip_comments(f.read())
        for i, pl in enumerate(m.get('players', [])):
            if 'loadout' not in pl:
                pl['loadout'] = default_loadout(None, cards, talents[i % len(talents)] if talents else '', ids)
        with open(mp, 'w', encoding='utf-8') as f:
            if mhead:
                f.write('\n'.join(mhead) + '\n')
            json.dump(m, f, ensure_ascii=False, indent=1)   # as tools/importers/bunkerwars/import.py write_json
            f.write('\n')


def all_modificator_classes(proj):
    out = []
    for name, c in proj.classes.items():
        lin = proj.lineage(name)
        if any(b in lin for b in ('SkillModificatorInfoBase',)) and name not in (
                'SkillModificatorInfoBase', 'SkillPointModificatorInfoBase', 'SkillUnitsModificatorInfoBase', 'SkillPlayersModificatorInfoBase',
                'SkillPlaceHolderModificatorInfoBase'):
            out.append(name)
    return sorted(out)


def write_report(path, proj, coverage, report, abilities):
    rows = []
    for cls in all_modificator_classes(proj):
        cov = coverage.get(cls)
        if cov:
            live = sum(1 for s in cov['skills'] if not s.startswith('legacy_'))
            used = f"{live} + {len(cov['skills']) - live} legacy" if live < len(cov['skills']) else str(live)
            rows.append(f"| {cls} | {cov['fidelity']} | {used} | {'; '.join(sorted(cov['notes']))[:300]} |")
        else:
            fid = 'unused' if cls in MP.H else 'missing'
            rows.append(f'| {cls} | {fid} | 0 | {"mapped, not used by the converted data" if cls in MP.H else "no mapping"} |')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(f'# Bunker Wars skills: conversion coverage\n\n{len(abilities)} abilities.\n\n')
        f.write('| modificator | fidelity | skills (live + legacy) | notes |\n|---|---|---|---|\n')
        f.write('\n'.join(rows) + '\n\n## Skill notes\n\n')
        f.write('\n'.join('- ' + r for r in report) + '\n')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--bw', required=True, help='Bunker Wars client checkout (folder with Client/Assets)')
    ap.add_argument('--out', required=True)
    ap.add_argument('--report')
    ap.add_argument('--px-per-cell', type=float, default=40.0, help='BW world pixels per engine cell (default 40, templates/bunkerwars)')
    ap.add_argument('--energy-resource', default='energy')
    ap.add_argument('--aspect', type=float, default=0.5, help='BW world.aspectRatio: skill ranges are ellipses (default 0.5)')
    ap.add_argument('--template', help='merge the abilities, helper types and default loadouts into this game folder (templates/bunkerwars)')
    ap.add_argument('--morale-star-points', type=float, default=1.0)
    ap.add_argument('--legacy', action='store_true', help='also convert the deleted hero skills (Heroes_DELETE)')
    opts = ap.parse_args(argv)
    ASPECT[0] = opts.aspect
    proj = open_project(os.path.expanduser(opts.bw))
    abilities, coverage, report = convert(proj, opts)
    out = {'playerAbilities': abilities, 'types': helper_types(abilities, opts),
           'bwSkills': {'pxPerCell': opts.px_per_cell, 'energyResource': opts.energy_resource, 'aspect': opts.aspect, 'count': len(abilities)}}
    if opts.template:
        merge_template(opts.template, out)
    with open(opts.out, 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    if opts.report:
        write_report(opts.report, proj, coverage, report, abilities)
    bad = sum(1 for c in coverage.values() if c['fidelity'] == 'missing')
    print(f'{len(abilities)} abilities, {len(coverage)} modificator classes used, {bad} without a mapping')
    return 0


if __name__ == '__main__':
    sys.exit(main())
