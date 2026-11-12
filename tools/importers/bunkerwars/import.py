#!/usr/bin/env python3
"""Bunker Wars importer: turns the Bunker Wars Unity client (Mushroom Wars 2 codebase) into an AIVERSE game folder.

    python3 tools/importers/bunkerwars/import.py --source ~/bunker-wars --out templates/bunkerwars [--json]

Reads (never writes) the Unity project:
  Client/Assets/StreamingAssets/UI.Windows/Cache/Services/GameData.uiws      GD keys (costs, morale, hero energy, skill cards)
  Client/Assets/StreamingAssets/UI.Windows/Cache/Services/Localization.uiws  ru / en strings
  Client/Assets/ME.TexturesGenerator/_DatabaseData/**.asset                  races, buildings, heroes, skills, battle parameters,
                                                                             morale, skins, emotions, camera shakes, destructibles
  Client/Assets/ME.TexturesGenerator/_Common/Maps/Data/**.asset              map data (points, routes, zones, environment, bot)
and writes (PORT_TODO.md next to them is maintained by hand: what is missing for 1:1 and where it lives in BW):
  game.json, rules.json, maps/<id>.json, lang/{ru,en}.json   the playable game (engine keys only)
  bw/*.json, bw/maps/<id>.json                               every BW number the engine has no key for yet, in engine units

Units: BW world units / SCALE = cells (SCALE = 40: the default BW map 2720 x 1520 becomes 68 x 38 cells, unit speed 80 -> 2,
tower ranges 400..550 -> 10..13.75). Map y is BW screen y (the BW world is drawn with aspectRatio 0.5; travel along routes
is measured in these coordinates, so travel times stay exact). Seconds stay seconds. Every value is written as the exact
decimal of the BW float divided by SCALE; the engine reads JSON numbers straight into Q47.16 fixed point.

Python stdlib only (+ tools/importers/lib/unity/unity_yaml.py).
"""
import argparse
import csv
import io
import json
import os
import re
import sys
from decimal import Decimal

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'lib'))
from unity import unity_yaml as uy  # noqa: E402

SCALE = 40                      # BW world units per cell
DEFAULT_MAP = (2720, 1520)      # MultiscreenModule.mapSize
TYPE_HOUSE, TYPE_TOWER, TYPE_SMITHY = 1, 2, 4
POINT_TYPES = {1: 'house', 2: 'tower', 4: 'smithy', 8: 'special1', 16: 'domination', 32: 'kingofthehill', 64: 'cave',
               128: 'graveyard', 256: 'special2', 512: 'special3', 1024: 'special4', 2048: 'special5'}
TYPE_IDS = {'house': 'bunker', 'tower': 'tower', 'smithy': 'armory'}
MODES = {1: 'conquest', 2: 'domination', 4: 'kingOfTheHill', 8: 'timeMod'}
COMPLEXITY = ['easy', 'normal', 'hard', 'expert']   # MapPointsModule.complexityTypes (values[] index)


# ------------------------------------------------------------------ numbers

def dec(v):
    """BW float (as Unity wrote it) -> Decimal of that shortest representation."""
    if isinstance(v, bool):
        return Decimal(int(v))
    if isinstance(v, int):
        return Decimal(v)
    if isinstance(v, float):
        return Decimal(repr(v))
    if isinstance(v, Decimal):
        return v
    return Decimal(str(v))


def num(d):
    """Decimal -> int or float with the same decimal digits (json writes the shortest repr)."""
    d = dec(d)
    if d == d.to_integral_value():
        return int(d)
    return float(str(d.normalize()))


def cells(v):
    return num(dec(v) / SCALE)


def per(v):
    """seconds per event from events per second (exact when the inverse terminates, else 6 digits)."""
    d = dec(v)
    if d == 0:
        return 0
    q = Decimal(1) / d
    return num(q.quantize(Decimal('0.000001')) if len(str(q)) > 12 else q)


# ------------------------------------------------------------------ source

class Source:
    def __init__(self, root):
        root = os.path.abspath(os.path.expanduser(root))
        for cand in (root, os.path.join(root, 'Client', 'Assets'), os.path.join(root, 'Assets')):
            if os.path.isdir(os.path.join(cand, 'ME.TexturesGenerator', '_DatabaseData')):
                self.assets = cand
                break
        else:
            raise SystemExit('not a Bunker Wars client: no ME.TexturesGenerator/_DatabaseData under ' + root)
        self.db = os.path.join(self.assets, 'ME.TexturesGenerator', '_DatabaseData')
        self.mapdata = os.path.join(self.assets, 'ME.TexturesGenerator', '_Common', 'Maps', 'Data')
        self.warnings = []
        self.scripts = {}       # script guid -> class name
        self.guids = {}         # asset guid -> path
        self.classes = {}       # asset path -> class name
        self.cache = {}
        self._index()
        svc = os.path.join(self.assets, 'StreamingAssets', 'UI.Windows', 'Cache', 'Services')
        self.gd = self._gamedata(os.path.join(svc, 'GameData.uiws'))
        self.loc = self._localization(os.path.join(svc, 'Localization.uiws'))

    def rel(self, path):
        return os.path.relpath(path, os.path.dirname(self.assets)).replace(os.sep, '/')

    @staticmethod
    def _meta_guid(p):
        try:
            with open(p, encoding='utf-8', errors='replace') as f:
                for line in f:
                    if line.startswith('guid:'):
                        return line.split()[1]
        except OSError:
            pass
        return None

    def _index(self):
        for root, dirs, files in os.walk(os.path.join(self.assets, 'MW2Data')):
            dirs[:] = [d for d in dirs if d != '.worktrees']
            for f in files:
                if f.endswith('.cs.meta'):
                    g = self._meta_guid(os.path.join(root, f))
                    if g:
                        self.scripts[g] = f[:-8]
        for base in (self.db, self.mapdata):
            for root, dirs, files in os.walk(base):
                for f in files:
                    if not f.endswith(('.asset.meta', '.prefab.meta')):
                        continue
                    g = self._meta_guid(os.path.join(root, f))
                    if g:
                        self.guids[g] = os.path.join(root, f[:-5])
        for p in self.guids.values():
            if p.endswith('.asset'):
                self.classes[p] = self._script_of(p)

    def _script_of(self, p):
        try:
            with open(p, encoding='utf-8', errors='replace') as f:
                for i, line in enumerate(f):
                    if 'm_Script:' in line:
                        m = re.search(r'guid: (\w+)', line)
                        return self.scripts.get(m.group(1), '?') if m else '?'
                    if i > 30:
                        break
        except OSError:
            pass
        return '?'

    def of_class(self, cls):
        return sorted(p for p, c in self.classes.items() if c == cls)

    def docs(self, path):
        if path not in self.cache:
            with open(path, encoding='utf-8', errors='replace') as f:
                self.cache[path] = uy.parse_documents(f.read())
        return self.cache[path]

    def main(self, path):
        ds = self.docs(path)
        for d in ds:
            if d.file_id == 11400000:
                return d.data
        return ds[0].data if ds else {}

    def ref(self, r):
        """{fileID, guid} -> (path, data of the main object) or (None, None)."""
        if not isinstance(r, dict) or not r.get('guid'):
            return None, None
        p = self.guids.get(r['guid'])
        if not p:
            return None, None
        if p.endswith('.prefab'):
            for d in self.docs(p):
                if d.file_id == r.get('fileID'):
                    return p, d.data
            return p, None
        return p, self.main(p)

    def by_file_id(self, path, fid):
        for d in self.docs(path):
            if d.file_id == fid:
                return d.data
        return None

    # GameData: "key,value" rows; decimals sometimes written with a comma ("37,5")
    def _gamedata(self, p):
        out = {}
        if not os.path.exists(p):
            self.warnings.append('GameData.uiws not found: GD values fall back to the asset defaults')
            return out
        with open(p, encoding='utf-8-sig', errors='replace') as f:
            for row in csv.reader(f):
                if len(row) < 2 or not row[0]:
                    continue
                v = row[1].strip()
                if re.fullmatch(r'-?\d+,\d+', v):
                    v = v.replace(',', '.')
                try:
                    v = int(v) if re.fullmatch(r'-?\d+', v) else Decimal(v)
                except Exception:
                    pass
                out[row[0].strip().lower()] = v
        return out

    def gdv(self, field, default=None):
        """GDInt / GDFloat field ({key, defaultValue}) -> value from GameData or the default."""
        if isinstance(field, dict):
            key = (field.get('key') or '').strip().lower()
            if key and key in self.gd:
                return self.gd[key]
            if 'defaultValue' in field:
                return field['defaultValue']
        return default

    def _localization(self, p):
        out = {}
        if not os.path.exists(p):
            self.warnings.append('Localization.uiws not found: no ru / en strings')
            return out
        with open(p, encoding='utf-8-sig', errors='replace') as f:
            rows = list(csv.reader(f))
        codes = rows[2] if len(rows) > 2 else []
        ru = codes.index('ru') if 'ru' in codes else 1
        en = codes.index('en-US') if 'en-US' in codes else 2
        for r in rows[3:]:
            if len(r) > max(ru, en) and r[0]:
                out[r[0].strip().lower()] = (r[ru].strip(), r[en].strip())
        return out

    def text(self, key):
        k = key.get('key') if isinstance(key, dict) else key
        return self.loc.get((k or '').strip().lower())


# ------------------------------------------------------------------ helpers

def clean(o):
    """Drop Unity noise (resource handles, editor ids) from a parsed object for the sidecar files."""
    noise = {'m_ObjectHideFlags', 'm_CorrespondingSourceObject', 'm_PrefabInstance', 'm_PrefabAsset', 'm_GameObject',
             'm_Enabled', 'm_EditorHideFlags', 'm_Script', 'm_EditorClassIdentifier', 'inDatabase', 'testVariantCheck',
             'testVariant', 'splitTestCheck', 'splitTest', 'versionFrom', 'versionTo', 'resourceIds', 'assetBundleName',
             'resourceSource', 'isAsync', 'isMono', 'loadDependencies', 'controlType', 'objectIndex', 'objectName',
             'directRef', 'formatWithDeclension', 'outputDeclensionWithoutNumber', 'iconsPerPlatform'}
    if isinstance(o, dict):
        return {k: clean(v) for k, v in o.items() if k not in noise}
    if isinstance(o, list):
        return [clean(v) for v in o]
    if isinstance(o, float):
        return num(o)
    return o


def stem_id(name):
    s = re.sub(r'[^0-9a-zA-Z]+', '_', name).strip('_').lower()
    return s or 'map'


def write_json(path, data, header=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    text = json.dumps(data, ensure_ascii=False, indent=1, default=lambda d: num(d))
    with open(path, 'w', encoding='utf-8') as f:
        if header:
            for line in header.strip().splitlines():
                f.write('// ' + line + '\n')
        f.write(text + '\n')


def compact_json(path, data, header=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        if header:
            f.write('// ' + header + '\n')
        f.write(json.dumps(data, ensure_ascii=False, separators=(',', ':'), default=lambda d: num(d)) + '\n')


# ------------------------------------------------------------------ buildings

PARAM_KEYS = ['fowRange', 'type', 'level', 'buildingTime', 'costGift', 'radius', 'soulsRadius',
              'moraleCatchNeutralPoints', 'moraleCatchEnemyPoints', 'moraleLostPoints', 'moraleUpgradePoints',
              'shieldCoefficient', 'shieldGlobalPercent', 'damageGlobalPercent', 'increaseTime', 'maxValue',
              'firePerSecond', 'fireRange', 'fireMetersPerSecond', 'flyType']


def read_buildings(src):
    """Race -> list of building parameter dicts (BuildingInfo -> BuildingParametersInfo), GD fields resolved."""
    races = {}
    for rp in src.of_class('RaceInfo'):
        race = src.main(rp)
        name = race.get('m_Name', os.path.basename(rp)[:-6])
        out, special = [], []
        for ref in race.get('buildings') or []:
            bp, binfo = src.ref(ref)
            if not binfo:
                continue
            pp, par = src.ref(binfo.get('parameters'))
            if not par:
                continue
            out.append(building_params(src, pp, par))
        for flt in race.get('buildingsFilter') or []:
            for ref in flt.get('buildings') or []:
                bp, binfo = src.ref(ref)
                if not binfo:
                    continue
                pp, par = src.ref(binfo.get('parameters'))
                if par:
                    b = building_params(src, pp, par)
                    b['filter'] = {'modType': flt.get('modType'), 'buildingType': flt.get('buildingType')}
                    special.append(b)
        races[name] = {'clientId': race.get('clientId'), 'raceType': race.get('raceType'), 'title': race.get('title'),
                       'buildings': out, 'special': special, 'path': src.rel(rp)}
    return races


def building_params(src, path, par):
    b = {'name': par.get('m_Name'), 'path': src.rel(path), 'guid': src._meta_guid(path + '.meta')}
    for k in PARAM_KEYS:
        if k in par:
            b[k] = par[k]
    b['cost'] = src.gdv(par.get('cost'), 0)
    b['maxCount'] = src.gdv(par.get('maxCount'), -1)
    b['upgrades'] = [r['guid'] for r in (par.get('upgrades') or []) if isinstance(r, dict) and r.get('guid') in src.guids]
    b['upgrade'] = (par.get('upgrade') or {}).get('guid')
    b['downgrade'] = (par.get('downgrade') or {}).get('guid')
    b['tips'] = [{'value': t.get('value'), 'delta': t.get('delta')} for t in (uy.get(par, 'tips', 'items') or [])]
    # cross-check plain asset floats with the GameData table (runtime reads the asset)
    race = re.sub(r'.*/(Race\d+)/.*', r'\1', path)
    for k in ('increaseTime', 'maxValue', 'shieldCoefficient', 'firePerSecond', 'fireRange', 'buildingTime',
              'shieldGlobalPercent', 'damageGlobalPercent'):
        gk = ('%s|%s|%s' % (race, b['name'], k)).lower()
        if gk in src.gd and k in b and dec(src.gd[gk]) != dec(b[k]):
            src.warnings.append('%s %s: asset %s, GameData %s (asset used)' % (b['name'], k, b[k], src.gd[gk]))
    return b


def numeric_view(b):
    return {k: (num(v) if isinstance(v, (int, float, Decimal)) else v) for k, v in b.items()
            if k not in ('path', 'guid', 'upgrades', 'upgrade', 'downgrade')}


def pick_race(src, races):
    """BW races differ only in looks when their numbers are equal; one set of types then serves all."""
    playable = {k: v for k, v in races.items() if v['buildings']}
    names = sorted(playable)
    if not names:
        raise SystemExit('no races with buildings found')
    base = names[0]
    for n in names[1:]:
        a = [numeric_view(x) for x in playable[base]['buildings']]
        b = [numeric_view(x) for x in playable[n]['buildings']]
        for x, y in zip(a, b):
            for k in ('name', 'tips'):    # tips are menu hints only
                x.pop(k, None)
                y.pop(k, None)
        if a != b:
            src.warnings.append('race %s has other building numbers than %s: only %s is used for the types' % (n, base, base))
    return base


def type_id(b):
    kind = POINT_TYPES.get(b.get('type'), 'type%s' % b.get('type'))
    if kind not in TYPE_IDS:   # special buildings have one level: name them after the asset
        return stem_id(re.sub(r'^Campaign_|PointInfo$', '', b.get('name') or kind))
    return '%s%d' % (TYPE_IDS[kind], int(b.get('level') or 1))


def point_type(kind, level, specials):
    if kind in TYPE_IDS:
        return '%s%d' % (TYPE_IDS[kind], level if kind != 'smithy' else 1)
    return specials.get(kind, kind)


# ------------------------------------------------------------------ rules

def build_rules(src, races, race, battle, heroes, lang):
    blist = races[race]['buildings'] + races[race]['special']
    by_guid = {b['guid']: b for b in blist}
    units = battle.get('units') or {}
    speed = units.get('speed', 80)
    hero = heroes[0] if heroes else {}
    types = {}
    soldier = {
        'name': 'Солдат', 'tags': ['soldier'], 'health': {'max': 1},
        'move': {'speed': cells(speed), 'radius': 0.125},
        'sight': cells((battle.get('fow') or {}).get('unitRadius', 130)),
        'entersGarrison': True, 'clash': True,
        'view': {'shape': 'infantry', 'scale': 0.5}}
    types['soldier'] = soldier
    lang['ru']['type.soldier'], lang['en']['type.soldier'] = 'Солдат', 'Soldier'
    smithies = sorted((b for b in blist if b.get('type') == TYPE_SMITHY), key=lambda b: b.get('level', 0))
    names = {'house': ('HUDScreen|BuildingMenu|Bunker|Description', 'Бункер', 'Bunker'),
             'tower': ('HUDScreen|BuildingMenu|Tower|Description', 'Вышка', 'Tower'),
             'smithy': ('HUDScreen|BuildingMenu|Arsenal|Description', 'Арсенал', 'Armory')}
    for b in blist:
        kind = POINT_TYPES.get(b.get('type'), 'special')
        if kind == 'smithy' and int(b.get('level') or 1) > 1:
            continue        # Smithy2..4 are the table for 2..4 owned armories, not buildings
        tid = type_id(b)
        if tid in types:
            continue
        lvl = int(b.get('level') or 1)
        t = {'name': '', 'tags': [kind if kind != 'house' else 'bunker'], 'level': lvl,
             'structure': {'footprint': [2, 2]}, 'invulnerable': True,
             'sight': cells(b.get('fowRange', 300)),
             'garrison': {'max': int(b.get('maxValue') or 0), 'unit': 'soldier', 'capturable': True,
                          'defense': num(b.get('shieldCoefficient', 1)), 'keep': 0, 'decay': 0}}
        if b.get('increaseTime'):
            t['garrison']['interval'] = num(b['increaseTime'])
        down = by_guid.get(b.get('downgrade'))
        if down:
            t['garrison']['onCapture'] = type_id(down)
        ups = []
        for g in b.get('upgrades') or []:
            u = by_guid.get(g)
            if u and not (u.get('type') == TYPE_SMITHY and int(u.get('level') or 1) > 1):
                ups.append(type_id(u))
        if ups:
            t['upgrades'] = ups
        t['cost'] = {'garrison': int(b.get('cost') or 0), 'time': num(b.get('buildingTime', 0))}
        if kind == 'tower':
            t['weapons'] = [{'range': cells(b.get('fireRange', 0)), 'damage': 1, 'cooldown': per(b.get('firePerSecond', 1)),
                             'projectileSpeed': cells(b.get('fireMetersPerSecond', 300))}]
        if kind == 'smithy' and smithies:
            dmg = [num(s.get('damageGlobalPercent', 1)) for s in smithies]
            shd = [num(s.get('shieldGlobalPercent', 1)) for s in smithies]
            t['playerMods'] = {'max': len(smithies), 'mods': [
                {'tags': ['soldier'], 'stat': 'damage', 'byCount': dmg},
                {'tags': ['bunker', 'tower', 'smithy'], 'stat': 'defense', 'byCount': shd}]}
        t['view'] = {'shape': {'house': 'bunker', 'tower': 'tower', 'smithy': 'barracks'}.get(kind, 'house'),
                     'scale': 1.0}
        key, ru, en = names.get(kind, (None,) + SPECIAL_NAMES.get(tid, (tid, tid)))
        tr = src.text(key) if key else None
        if tr:
            ru, en = tr[0].capitalize(), tr[1].capitalize()
        if kind in names and not (kind == 'smithy'):
            ru, en = '%s %d' % (ru, lvl), '%s %d' % (en, lvl)
        t['name'] = ru
        lang['ru']['type.' + tid], lang['en']['type.' + tid] = ru, en
        types[tid] = t
    # upgrade chains only between types that exist
    for t in types.values():
        if 'upgrades' in t:
            t['upgrades'] = [u for u in t['upgrades'] if u in types]
        g = t.get('garrison')
        if g and g.get('onCapture') not in (None,) and g['onCapture'] not in types:
            g.pop('onCapture')

    rules = {
        'victory': 'noEntities',
        'fogOfWar': {'mode': 'none'},
        'lanes': {'left': 2, 'right': 3, 'spacing': cells((battle.get('units') or {}).get('columnsPadding', 20))},
        'resources': {
            'energy': {'name': 'Энергия', 'start': 0, 'regen': num(hero.get('passiveEnergyPerSecond', 2.5)),
                       'capped': True, 'storage': num(hero.get('maxSkillEnergy', 500)),
                       'gain': {'lost': num(hero.get('energyPerLostUnitAtMorale0', 2.25))}}},
        'types': types,
    }
    lang['ru']['resource.energy'], lang['en']['resource.energy'] = 'Энергия', 'Energy'
    return rules


# ------------------------------------------------------------------ battle parameters, morale

def read_battle(src):
    ps = src.of_class('BattleParametersInfo')
    if not ps:
        raise SystemExit('BattleParametersInfo.asset not found')
    return src.main(ps[0]), ps[0]


def read_morale(src):
    ps = src.of_class('MoraleInfo')
    levels = []
    if ps:
        for i, lv in enumerate(src.main(ps[0]).get('levels') or []):
            levels.append({k: num(src.gdv(v, 0)) for k, v in lv.items()})
    return {'source': src.rel(ps[0]) if ps else None, 'levels': levels}


# ------------------------------------------------------------------ heroes, skills

def read_heroes(src, lang):
    heroes = []
    for p in src.of_class('HeroInfo'):
        if not re.search(r'/Hero\d+/Levels/Hero\d+_Level 1\.asset$', p.replace(os.sep, '/')):
            continue
        h = src.main(p)
        hid = 'hero%d' % int(h.get('clientId') or 0)
        rp, race = src.ref(h.get('race'))
        tp, talent = src.ref(h.get('talent'))
        e = {
            'id': hid, 'clientId': h.get('clientId'), 'source': src.rel(p),
            'race': race.get('m_Name') if race else None,
            'maxSkillEnergy': num(src.gdv(h.get('maxSkillEnergy'), 0)),
            'energyPerUnit': num(src.gdv(h.get('energyPerUnit'), 0)),
            'passiveEnergyPerSecond': num(src.gdv(h.get('passiveEnergyPerSecond'), 0)),
            'talent': talent.get('m_Name') if talent else None,
            'talentClientId': talent.get('clientId') if talent else None,
            'trainingMapId': h.get('trainingMapId'),
            'levels': len([x for x in (h.get('levels') or []) if isinstance(x, dict) and x.get('guid')]) + 1,
        }
        n = src.text('Heroes|Hero%d Caption' % int(h.get('clientId') or 0))
        if n:
            e['name'] = {'ru': n[0], 'en': n[1]}
            lang['ru']['hero.' + hid], lang['en']['hero.' + hid] = n
        heroes.append(e)
    heroes.sort(key=lambda x: x['clientId'] or 0)
    # energy for a lost unit = energyPerUnit * (morale levels - morale index - 1) (Player.OnUnitLost)
    return heroes


def read_skills(src, lang):
    out = []
    for cls, kind in (('HeroEquipSkillHeader', 'card'), ('HeroTalentSkillHeader', 'talent')):
        for p in src.of_class(cls):
            s = src.main(p)
            name = s.get('m_Name') or os.path.basename(p)[:-6]
            e = {'id': stem_id(name), 'kind': kind, 'clientId': s.get('clientId'), 'source': src.rel(p),
                 'allowedSlot': s.get('allowedSlot'), 'rarity': s.get('rarity'), 'levels': []}
            for lv in s.get('levels') or []:
                sp = src.guids.get((lv.get('skill') or {}).get('guid'))
                e['levels'].append({'level': lv.get('level'), 'prefab': src.rel(sp) if sp else None})
            # GameData numbers: SkillsCards|<name>|<param>|lvl<N>
            params = {}
            pre = 'skillscards|%s|' % name.lower()
            for k, v in src.gd.items():
                if k.startswith(pre):
                    rest = k[len(pre):].split('|')
                    if len(rest) == 2 and rest[1].startswith('lvl'):
                        params.setdefault(rest[0], {})[int(rest[1][3:])] = num(v)
            if params:
                e['params'] = {k: [v[i] for i in sorted(v)] for k, v in sorted(params.items())}
            t = src.text('Skills|Headers|%s' % name)
            d = src.text('Skills|Descriptions|%s' % name)
            if t:
                e['name'] = {'ru': t[0], 'en': t[1]}
                lang['ru']['skill.' + e['id']], lang['en']['skill.' + e['id']] = t
            if d:
                e['description'] = {'ru': d[0], 'en': d[1]}
            out.append(e)
    out.sort(key=lambda x: (x['kind'], x['clientId'] if isinstance(x['clientId'], int) else 0, x['id']))
    return out


# ------------------------------------------------------------------ maps

def read_players(src):
    out = {}
    for p in src.of_class('PlayerInfo'):
        d = src.main(p)
        out[src._meta_guid(p + '.meta')] = {'name': d.get('m_Name'), 'neutral': bool(d.get('isNeutral')),
                                             'color': d.get('color'), 'clientId': d.get('clientId')}
    return out


def map_objects(src, data_path):
    """MapData asset: main object + modules by role (points, routes, zones, environment, bot, ...)."""
    docs = src.docs(data_path)
    main = None
    for d in docs:
        if isinstance(d.data, dict) and 'points' in d.data and 'routes' in d.data:
            main = d.data
    mods = {}
    if main:
        for role in ('campaign', 'audio', 'routes', 'points', 'destructibleObjects', 'zones', 'tutorial', 'bot',
                     'environment', 'defaultCustomRules', 'neutralPlayerCustomRules'):
            r = main.get(role)
            if isinstance(r, dict) and r.get('fileID'):
                mods[role] = src.by_file_id(data_path, r['fileID'])
    return main, mods


def to_cell(x, y, w, h):
    return [num((dec(x) + dec(w) / 2) / SCALE), num((dec(h) / 2 - dec(y)) / SCALE)]


def build_map(src, hp, header, players, type_ok, specials, warnings):
    data_ref = header.get('mapData') or {}
    dpath = src.guids.get(data_ref.get('guid')) if isinstance(data_ref, dict) else None
    if not dpath:
        rp = data_ref.get('resourcePath') if isinstance(data_ref, dict) else None
        cand = os.path.join(os.path.dirname(src.assets), rp) if rp else None
        dpath = cand if cand and os.path.exists(cand) else None
    if not dpath:
        warnings.append('%s: map data not found' % os.path.basename(hp))
        return None, None
    main, mods = map_objects(src, dpath)
    pts = uy.get(mods.get('points') or {}, 'list', default=[]) or []
    if not pts:
        pts = uy.get(header, 'points', 'list', default=[]) or []
    w, h = DEFAULT_MAP
    if header.get('useCustomMapSize'):
        cs = header.get('customMapSize') or {}
        if cs.get('x') and cs.get('y'):
            w, h = cs['x'], cs['y']
    # every point inside the map with a margin of 2 cells
    for p in pts:
        pos = p.get('position') or {}
        w = max(dec(w), 2 * (abs(dec(pos.get('x', 0))) + 2 * SCALE))
        h = max(dec(h), 2 * (abs(dec(pos.get('y', 0))) + 2 * SCALE))
    W, H = int((dec(w) / SCALE).to_integral_value(rounding='ROUND_CEILING')), int((dec(h) / SCALE).to_integral_value(rounding='ROUND_CEILING'))
    w, h = dec(W * SCALE), dec(H * SCALE)

    hplayers = header.get('players') or []
    pguid = [((pl.get('player') or {}).get('guid')) for pl in hplayers]
    complexity = 0 if header.get('multiplayer') else 1
    ents, bwpoints = [], []
    for i, p in enumerate(pts):
        st, lvl = p.get('startType'), int(p.get('startLevel') or 1)
        kind = POINT_TYPES.get(st, 'type%s' % st)
        tid = point_type(kind, lvl, specials)
        og = (p.get('startOwner') or {}).get('guid')
        owner = pguid.index(og) + 1 if og in pguid and not players.get(og, {}).get('neutral') else 0
        vals = p.get('values') or []
        v = vals[min(complexity, len(vals) - 1)] if vals else {}
        pos = p.get('position') or {}
        e = {'type': tid, 'owner': owner, 'pos': to_cell(pos.get('x', 0), pos.get('y', 0), w, h),
             'garrison': int(v.get('startValue', 0) or 0), 'id': 'p%d' % i}
        if tid not in type_ok:
            warnings.append('%s: point %d type %s (%s level %d) has no engine type yet: skipped' % (
                os.path.basename(hp), i, tid, kind, lvl))
        else:
            ents.append(e)
        bwpoints.append({'index': i, 'type': kind, 'level': lvl, 'owner': owner, 'pos': e['pos'],
                         'linkedId': p.get('linkedId'), 'caveGroup': p.get('caveGroup'), 'botPriority': p.get('botPriority'),
                         'domination': bool(p.get('isDominationPoint')), 'kingOfTheHill': bool(p.get('isKingOfTheHillPoint')),
                         'values': {COMPLEXITY[k]: clean(x) for k, x in enumerate(vals[:4])},
                         'autostartSkill': uy.get(p, 'autostartSkillResource', 'resourcePath') or None})
    mplayers = []
    for k, pl in enumerate(hplayers):
        mplayers.append({'name': 'Игрок %d' % (k + 1), 'team': int(pl.get('team') or (k + 1))})
    m = {'name': None, 'size': [W, H],
         'players': mplayers,
         'terrain': {'rows': ['.' * W] * H},
         'entities': ents}
    if header.get('fogOfWar'):
        m['fogOfWar'] = {'mode': 'full'}
    # sidecar: everything else, converted to cells
    routes = []
    for r in uy.get(mods.get('routes') or {}, 'list', default=[]) or []:
        poly, slow, flags = [], [], []
        for rp in r.get('points') or []:
            pos = rp.get('position') or {}
            poly.append(to_cell(pos.get('x', 0), pos.get('y', 0), w, h))
            slow.append(num(rp.get('slowdownCoefficient') or 0))
            flags.append(rp.get('zoneFlags') or 0)
        rr = {'from': r.get('pointFromId'), 'to': r.get('pointToId'), 'points': poly}
        if any(slow):
            rr['slowdown'] = slow
        if any(flags):
            rr['zoneFlags'] = flags
        offs = [uy.get(rp, 'offset', 'offsets') or [] for rp in r.get('points') or []]
        if any(any(o) for o in offs):
            rr['offsets'] = [[cells(x) for x in o] for o in offs]
        routes.append(rr)
    zones = []
    for z in uy.get(mods.get('zones') or {}, 'list', default=[]) or []:
        zones.append({'zoneType': z.get('zoneType'), 'slowdown': num(z.get('slowdownValue') or 0),
                      'polygon': [to_cell(q.get('x', 0), q.get('y', 0), w, h) for q in z.get('points') or []]})
    side = {
        'source': {'header': src.rel(hp), 'data': src.rel(dpath)},
        'scale': SCALE, 'bwMapSize': [num(w), num(h)], 'origin': 'cell = ((x + w/2) / scale, (h/2 - y) / scale)',
        'mapId': header.get('mapId'), 'multiplayer': bool(header.get('multiplayer')), 'tutorial': bool(header.get('tutorial')),
        'mapType': header.get('mapType'), 'modes': header.get('modes'),
        'gameMode': [n for b, n in MODES.items() if (header.get('gameMode') or 0) & b],
        'kingOfTheHillScore': {'ffa': header.get('kingOfTheHillFFAScore'), 'coop': header.get('kingOfTheHillCoopScore')},
        'aiAlgorithm': header.get('aiAlgorithm'), 'fogOfWar': bool(header.get('fogOfWar')),
        'suddenDeathDisabled': bool(header.get('suddenDeathDisabled')), 'complexityUsed': COMPLEXITY[complexity],
        'season': clean(header.get('seasonConfig')), 'specialModType': header.get('specialModType'),
        'players': [{'slot': k + 1, 'player': players.get(pguid[k], {}).get('name'), 'team': pl.get('team'),
                     'botOnly': bool(pl.get('botOnly')), 'uiCorner': pl.get('uiCorner'),
                     'aiComplexity': {c: uy.get(pl, c + 'ComplexityInfo', 'aiComplexity') for c in COMPLEXITY}}
                    for k, pl in enumerate(hplayers)],
        'points': bwpoints, 'routes': routes, 'zones': zones,
        'environment': {k: v for k, v in (clean(mods.get('environment')) or {}).items() if k not in ('grass', 'winterGrass')}, 'destructibleObjects': clean(mods.get('destructibleObjects')),
        'bot': clean(mods.get('bot')), 'campaign': clean(mods.get('campaign')),
        'tutorial': clean(mods.get('tutorial')),
    }
    return m, side


# ------------------------------------------------------------------ misc sidecars

def read_simple(src, cls):
    return [dict(clean(src.main(p)), source=src.rel(p)) for p in src.of_class(cls)]


# ------------------------------------------------------------------ run

def run(source, out):
    src = Source(source)
    lang = {'ru': {}, 'en': {}}
    battle, bpath = read_battle(src)
    races = read_buildings(src)
    race = pick_race(src, races)
    heroes = read_heroes(src, lang)
    morale = read_morale(src)
    nlev = len(morale['levels']) or 6
    for hh in heroes:
        hh['energyPerLostUnitAtMorale0'] = num(dec(hh['energyPerUnit']) * (nlev - 0 - 1))
    energy_sets = {(hh['maxSkillEnergy'], hh['energyPerUnit'], hh['passiveEnergyPerSecond']) for hh in heroes}
    if len(energy_sets) > 1:
        src.warnings.append('heroes differ in energy numbers: the rules use %s (per-hero energy needs per-player resources)' %
                            heroes[0]['id'])
    skills = read_skills(src, lang)
    rules = build_rules(src, races, race, battle, heroes, lang)
    types = rules['types']
    specials = {POINT_TYPES.get(b.get('type')): type_id(b) for b in races[race]['special']}
    players = read_players(src)

    # maps
    maps, manifest_maps, skipped = [], [], []
    headers = src.of_class('MapHeader')
    seen = {}
    for hp in headers:
        header = src.main(hp)
        rel = src.rel(hp)
        if '_DELETE' in rel:
            continue
        mid = header.get('mapId')
        variant = os.path.basename(os.path.dirname(hp))
        base = stem_id(str(header.get('m_Name') or os.path.basename(hp)[:-6]).replace('_Header', ''))
        if re.search(r'_st\d+', variant):
            base += '_' + stem_id(variant.split('_')[-1])
        if base in seen:
            base += '_%d' % (len([k for k in seen if k.startswith(base)]) + 1)
        seen[base] = True
        m, side = build_map(src, hp, header, players, types, specials, src.warnings)
        if not m:
            skipped.append(rel)
            continue
        if len(m['players']) < 2 or not m['entities']:
            src.warnings.append('%s: %d players, %d points: not a playable battle map, sidecar only' % (
                base, len(m['players']), len(m['entities'])))
            compact_json(os.path.join(out, 'bw', 'maps', base + '.json'), side, 'Bunker Wars map data the engine has no keys for yet (cells, seconds). Generated by tools/importers/bunkerwars.')
            continue
        tr = src.text(header.get('title'))
        title_ru, title_en = (tr if tr else (str(mid), str(mid)))
        m['name'] = title_ru
        np = len(m['players'])
        lang['ru']['map.%s.title' % base] = '%s (%d игр.)' % (title_ru, np)
        lang['en']['map.%s.title' % base] = '%s (%dP)' % (title_en, np)
        lang['ru']['map.%s.name' % base], lang['en']['map.%s.name' % base] = title_ru, title_en
        for k in range(np):
            lang['ru']['map.%s.player.%d' % (base, k + 1)] = 'Игрок %d' % (k + 1)
            lang['en']['map.%s.player.%d' % (base, k + 1)] = 'Player %d' % (k + 1)
        write_json(os.path.join(out, 'maps', base + '.json'), m)
        compact_json(os.path.join(out, 'bw', 'maps', base + '.json'), side,
                     'Bunker Wars map data the engine has no keys for yet (cells, seconds). Generated by tools/importers/bunkerwars.')
        group = 'multiplayer' if side['multiplayer'] else ('tutorial' if side['tutorial'] or 'utorial' in rel else 'campaign')
        maps.append((group, side['mapId'] or 0, base, '%s (%dP)' % (title_en, np)))
    order = {'multiplayer': 0, 'campaign': 1, 'tutorial': 2}
    maps.sort(key=lambda x: (order[x[0]], x[1], x[2]))
    for g, _, base, title in maps:
        manifest_maps.append({'file': 'maps/%s.json' % base, 'title': title})

    manifest = {
        'description': 'Bunker Wars: бункеры, вышки, арсеналы и герои — числа из клиента Bunker Wars (tools/importers/bunkerwars).',
        'name': 'Bunker Wars', 'id': 'com.rtsengine.bunkerwars', 'version': '0.1.0', 'build': 1, 'module': 'rts',
        'sourceLanguage': 'ru', 'languages': ['ru', 'en'], 'rules': 'rules.json',
        'maps': manifest_maps, 'orientation': 'landscape', 'icon': 'icon.png'}
    write_json(os.path.join(out, 'game.json'), manifest,
               'Game manifest generated by tools/importers/bunkerwars/import.py from the Bunker Wars client. Do not edit by hand.')
    write_json(os.path.join(out, 'rules.json'), rules,
               RULES_HEADER % {'race': race})

    # sidecars
    bw = os.path.join(out, 'bw')
    write_json(os.path.join(bw, 'buildings.json'), {'scale': SCALE, 'usedRace': race, 'races': {
        k: {'clientId': v['clientId'], 'raceType': v['raceType'], 'source': v['path'],
            'buildings': [dict(numeric_view(b), id=type_id(b)) for b in v['buildings']],
            'special': [dict(numeric_view(b), id=type_id(b)) for b in v['special']]} for k, v in sorted(races.items())}},
        'Bunker Wars buildings, every number as in the client (BW units: divide ranges by scale for cells).')
    write_json(os.path.join(bw, 'morale.json'), dict(morale, battle=clean(battle.get('morale'))),
               'Bunker Wars morale levels (MoraleInfo + GameData Morale|LevelN|*).')
    write_json(os.path.join(bw, 'heroes.json'), {'heroes': heroes}, 'Bunker Wars heroes (HeroInfo level 1 + GameData HeroN|*).')
    write_json(os.path.join(bw, 'skills.json'), {'skills': skills},
               'Bunker Wars skill cards and talents: per-level GameData numbers + the level prefab with the modificators.')
    keep = ('manualMorale', 'winter', 'fow', 'weather', 'suddenDeath', 'common', 'world', 'morale', 'units', 'unitsAttack',
            'souls', 'buildings', 'effects')
    bwb = {k: clean(battle.get(k)) for k in keep if k in battle}
    for k in ('killBombArc', 'takeOffCurve', 'arcFlyCurve'):
        (bwb.get('units') or {}).pop(k, None)
    write_json(os.path.join(bw, 'battle.json'), dict(bwb, source=src.rel(bpath)),
               'Bunker Wars BattleParametersInfo (BW units and seconds).')
    write_json(os.path.join(bw, 'skins.json'), {
        'buildings': read_simple(src, 'SkinBuildingInfoHeader'), 'heroes': read_simple(src, 'SkinHeroInfo'),
        'units': read_simple(src, 'SkinInfoHeader')}, 'Bunker Wars skins (headers; art stays in the client).')
    write_json(os.path.join(bw, 'emotions.json'), {'emotions': read_simple(src, 'EmotionInfo')}, 'Bunker Wars emotions.')
    write_json(os.path.join(bw, 'camera_shake.json'), {'patterns': read_simple(src, 'CameraShakeInfo')},
               'Bunker Wars camera shake patterns.')
    write_json(os.path.join(bw, 'destructible_objects.json'), {
        'parameters': read_simple(src, 'DestructibleObjectParametersInfo'),
        'buildings': read_simple(src, 'DestructionObjectBuildingInfo')}, 'Bunker Wars destructible objects.')
    write_json(os.path.join(bw, 'modes.json'), {'winModes': read_simple(src, 'WinModeInfo'),
                                                'battleModes': read_simple(src, 'BattleModeInfo'),
                                                'suddenDeath': read_simple(src, 'SuddenDeathInfo')},
               'Bunker Wars win modes, battle modes and sudden death.')
    write_json(os.path.join(bw, 'races.json'), {'races': [
        {'id': k, 'clientId': v['clientId'], 'raceType': v['raceType'],
         'name': dict(zip(('ru', 'en'), src.text(v['title']))) if src.text(v['title']) else None} for k, v in sorted(races.items())]},
        'Bunker Wars races (looks only when their numbers are equal).')

    # lang
    for code, name in (('ru', 'Русский'), ('en', 'English')):
        table = {'@language': name, 'game.name': 'Bunker Wars'}
        table.update(HOWTO[code])
        table.update(dict(sorted(lang[code].items())))
        write_json(os.path.join(out, 'lang', code + '.json'), table,
                   'bunkerwars — %s. Generated by tools/importers/bunkerwars from the client localization.' % code)
    return {'maps': len(manifest_maps), 'mapsSidecarOnly': len(headers) - len(manifest_maps) - len(skipped),
            'types': len(types), 'heroes': len(heroes), 'skills': len(skills), 'race': race,
            'warnings': list(dict.fromkeys(src.warnings)), 'skipped': skipped}


SPECIAL_NAMES = {'domination': ('Точка доминации', 'Domination point'), 'koh': ('Царь горы', 'King of the Hill'),
                 'cave': ('Пещера', 'Cave'), 'graveyard': ('Кладбище', 'Graveyard'), 'hospital': ('Госпиталь', 'Hospital'),
                 'storage': ('Склад', 'Storage'), 'megatower': ('Мегавышка', 'Mega tower'), 'prison': ('Тюрьма', 'Prison'),
                 'snowbunker': ('Снежный бункер', 'Snow bunker')}

RULES_HEADER = """Bunker Wars rules generated by tools/importers/bunkerwars/import.py from the Bunker Wars client. Do not edit by hand.
Buildings of %(race)s (other races have equal numbers and differ only in looks). Units: BW units / 40 = cells, seconds.
garrison.interval = IncreaseTime, max = MaxValue, defense = ShieldCoefficient, cost = Cost / BuildingTime (GameData),
onCapture = the building's downgrade; towers: range = FireRange, cooldown = 1 / FirePerSecond, projectileSpeed =
FireMetersPerSecond; armories: damage / defense by the number owned = Smithy1..4 DamageGlobalPercent / ShieldGlobalPercent.
Energy = the hero's MaxSkillEnergy / PassiveEnergyPerUnit; a lost unit gives EnergyPerUnit x (morale levels - 1) (morale 0).
Morale, skills, routes, modes and environment are not here yet: see PORT_TODO.md and bw/*.json."""

HOWTO = {
    'ru': {'howto.like': 'Bunker Wars: бункеры, вышки, арсеналы и герои.',
           'howto.goal': 'Захватите все здания противника.',
           'howto.tip': 'Больше четырёх арсеналов не нужно — пятый ничего не добавляет.',
           'howto.step1': 'Бункеры призывают солдат; улучшение оплачивается солдатами внутри: больше места, быстрее призыв, крепче защита.',
           'howto.step2': 'Вышки стреляют по проходящим врагам; перестройка в другое здание стоит 30 солдат.',
           'howto.step3': 'Захваченный бункер или вышка теряет уровень. Встретившиеся отряды сражаются.',
           'howto.step4': 'Арсеналы усиливают атаку солдат и защиту всех зданий.'},
    'en': {'howto.like': 'Bunker Wars: bunkers, towers, armories and heroes.',
           'howto.goal': "Capture every enemy building.",
           'howto.tip': 'More than four armories is pointless — the fifth adds nothing.',
           'howto.step1': 'Bunkers recruit soldiers; an upgrade is paid with the soldiers inside: more room, faster recruiting, better defense.',
           'howto.step2': 'Towers shoot passing enemies; converting a building into another kind costs 30 soldiers.',
           'howto.step3': 'A captured bunker or tower loses a level. Groups that meet on the way fight.',
           'howto.step4': "Armories raise your soldiers' attack and the defense of all your buildings."},
}


def main():
    ap = argparse.ArgumentParser(description='Import Bunker Wars into an AIVERSE game folder')
    ap.add_argument('--source', required=True, help='Bunker Wars repository (or its Client / Client/Assets folder)')
    ap.add_argument('--out', required=True, help='output game folder')
    ap.add_argument('--json', action='store_true', help='one JSON report on stdout (MCP / editor)')
    a = ap.parse_args()
    try:
        rep = run(a.source, os.path.expanduser(a.out))
    except SystemExit as ex:
        if a.json:
            print(json.dumps({'ok': False, 'error': str(ex)}, ensure_ascii=False))
            sys.exit(1)
        raise
    if a.json:
        print(json.dumps(dict({'ok': True}, **rep), ensure_ascii=False))
        return
    print('Bunker Wars -> %s' % a.out)
    print('  maps: %d (+%d sidecar only), types: %d, heroes: %d, skills: %d, race: %s' % (
        rep['maps'], rep['mapsSidecarOnly'], rep['types'], rep['heroes'], rep['skills'], rep['race']))
    for s in rep['skipped']:
        print('  skipped: ' + s)
    for w in rep['warnings']:
        print('  warning: ' + w)


if __name__ == '__main__':
    main()
