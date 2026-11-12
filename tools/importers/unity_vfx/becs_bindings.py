#!/usr/bin/env python3
"""Event-bound effects of an ME.BECS Unity game (Drone Fall) -> an AIVERSE fx binding plan (fx/fx.json, fx_bind).

    python3 tools/importers/unity_vfx/becs_bindings.py --assets <Unity>/Assets [--configs ExtData/Configs]
        [--out plan.json] [--game <project> --import] [--json]

ME.BECS games do not play effects from scripts: entity configs (EntityConfig .asset, [SerializeReference]
components) name them, and views (prefabs with view modules) run them. This tool reads both:

    config component                     engine cue        scope
    UnitEffectOnSpawnComponent           spawned           types.<unit>
    UnitEffectOnDestroyComponent         died              types.<unit>
    UnitEffectOnHitComponent             (target got hit)  reported: the engine "hit" cue is the attacker's
    InstantiateViewComponent (bullet)    trail             weapons.<bullet>  (projectile view: trail + muzzle systems)
    BulletEffectOnDestroy                hit               weapons.<bullet>
    BulletFlightEndEffect                miss              types.<unit> of the shooters (miss has no weapon scope)
    EffectOnBulletDirectionalHit         hit               weapons.<bullet>  (direction kept: not upright)

and the view modules of the bound prefabs:

    ProjectileTrailViewModule            retainAfterRelease false on every emitter -> trail "clear": true
    ProjectileMuzzleViewModule           muzzle systems are in the projectile view: they play where the projectile
                                         starts (convergence to the muzzle is approximated)
    AlignExplosionToWorldUpModule        "upright": true
    ExplosionAshModule, FlameParticleViewModule, Flamethrower..., VfxGraphBurstModule
                                         scripted emission: reported (VFX Graph bursts play once on spawn = the
                                         engine default)

Effect ids go through Resources/ObjectReferenceRegistry (<id>.asset: sourceId -> asset GUID). Effect names are the
sanitized prefab names (what import.py writes for that prefab). Weapon / type ids are the config names: the game
port maps them to its rules ids (`map` in the plan lists every bullet's shooters). --import also runs import.py on
every bound prefab into --game and writes the bindings into its fx/fx.json (existing entries are kept).
"""
import argparse
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from materials import sanitize  # noqa: E402
from unity_scene import Assets, Report  # noqa: E402

REF_RE = re.compile(r'- rid: -?\d+\n\s+type: \{class: (\w+), ns: [\w.]*, asm: [\w.-]*\}\n\s+data:\n((?:\s{8,}.*\n)*)')
SOURCE_RE = re.compile(r'sourceId: (\d+)')
PREFAB_RE = re.compile(r'prefabId: (\d+)')
EFFECT_COMPONENTS = {'UnitEffectOnSpawnComponent', 'UnitEffectOnDestroyComponent', 'UnitEffectOnHitComponent', 'BulletEffectOnDestroy',
                     'EffectOnBulletDirectionalHitComponent', 'BulletFlightEndEffect'}
SCRIPTED = {'ExplosionAshModule': 'emits from a script (organic death pose): imported as its prefab; check the burst',
            'FlameParticleViewModule': 'flame stream emitted by a script along the flamethrower area: needs a gameplay-driven emitter',
            'FlamethrowerViewModule': 'flamethrower view driven by gameplay state',
            'VfxGraphBurstModule': 'VFX Graph reinit on spawn: the engine plays the converted graph once on spawn (same)',
            'StatusEffectViewModule': 'status effect loop toggled by gameplay: bind as a "damaged"-style loop or script it'}


def registry(base, cache=None):
    """ObjectReferenceRegistry: id -> asset GUID (and back)."""
    if cache and os.path.exists(cache):
        with open(cache, encoding='utf-8') as f:
            ids = json.load(f)
    else:
        ids = {}
        root = os.path.join(base, 'Resources', 'ObjectReferenceRegistry')
        for fn in sorted(os.listdir(root)) if os.path.isdir(root) else []:
            if not fn.endswith('.asset'):
                continue
            with open(os.path.join(root, fn), encoding='utf-8', errors='replace') as f:
                txt = f.read(4096)
            m = re.search(r'm_AssetGUID: ([0-9a-f]{32})', txt) or re.search(r'source: \{fileID: -?\d+, guid: ([0-9a-f]{32})', txt)
            if m:
                ids[fn[:-6]] = m.group(1)
        if cache:
            with open(cache, 'w', encoding='utf-8') as f:
                json.dump(ids, f)
    return ids, {g: i for i, g in ids.items()}


def components(txt):
    return [(m.group(1), m.group(2)) for m in REF_RE.finditer(txt)]


class Planner:
    def __init__(self, base, assets, ids, rep):
        self.base, self.assets, self.ids, self.rep = base, assets, ids, rep
        self.prefabs = {}      # prefab path -> {'effect', 'modules', 'options'}
        self.bindings = []     # {'scope', 'id', 'cue', 'effect', 'options', 'source'}
        self.notes = []
        self.target_hit = {}   # effects played on the unit that was hit -> configs

    def path_of(self, sid):
        g = self.ids.get(str(sid))
        return self.assets.by_guid.get(g) if g else None

    def view_of_config(self, sid):
        """Effect config id -> its view prefab (the config's InstantiateViewComponent)."""
        p = self.path_of(sid)
        if not p:
            return None
        if p.endswith('.prefab'):
            return p
        try:
            with open(p, encoding='utf-8', errors='replace') as f:
                txt = f.read()
        except OSError:
            return None
        for cls, data in components(txt):
            if cls == 'InstantiateViewComponent':
                m = PREFAB_RE.search(data)
                if m:
                    return self.path_of(m.group(1))
        return None

    def effect_for(self, prefab):
        if prefab not in self.prefabs:
            name = sanitize(os.path.splitext(os.path.basename(prefab))[0], 40)
            mods, opts = [], {}
            try:
                with open(prefab, encoding='utf-8', errors='replace') as f:
                    txt = f.read()
            except OSError:
                txt = ''
            for cls, data in components(txt):
                mods.append(cls)
                if cls == 'AlignExplosionToWorldUpModule':
                    opts['upright'] = True
                elif cls == 'ProjectileTrailViewModule':
                    retains = re.findall(r'retainAfterRelease: (\d)', data)
                    if retains and all(r == '0' for r in retains):
                        opts['clear'] = True
                elif cls in SCRIPTED:
                    self.notes.append('%s: %s: %s' % (os.path.relpath(prefab, self.base), cls, SCRIPTED[cls]))
            self.prefabs[prefab] = {'effect': name, 'modules': sorted(set(mods)), 'options': opts}
        return self.prefabs[prefab]

    def bind(self, scope, ident, cue, prefab, source, extra=None):
        e = self.effect_for(prefab)
        opts = {k: v for k, v in e['options'].items() if (k != 'clear' or cue == 'trail') and (k != 'upright' or cue != 'trail')}
        opts.update(extra or {})
        b = {'scope': scope, 'id': ident, 'cue': cue, 'effect': e['effect'], 'prefab': os.path.relpath(prefab, self.base), 'source': source}
        if opts:
            b['options'] = opts
        if b not in self.bindings:
            self.bindings.append(b)

    def scan(self, folders):
        refs = {}   # config id -> configs that mention it (shooters of a bullet)
        configs = []
        for folder in folders:
            for dp, dns, fns in os.walk(os.path.join(self.base, folder)):
                dns.sort()
                for fn in sorted(fns):
                    if fn.endswith('.asset'):
                        configs.append(os.path.join(dp, fn))
        texts = {}
        for p in configs:
            try:
                with open(p, encoding='utf-8', errors='replace') as f:
                    texts[p] = f.read()
            except OSError:
                continue
            for sid in set(SOURCE_RE.findall(texts[p])):
                refs.setdefault(sid, []).append(p)
        guid_to_id = {g: i for i, g in self.ids.items()}
        for p, txt in texts.items():
            comps = components(txt)
            kinds = {c for c, _ in comps}
            if not kinds & {'UnitEffectOnSpawnComponent', 'UnitEffectOnDestroyComponent', 'UnitEffectOnHitComponent',
                            'BulletConfigComponent', 'BulletEffectOnDestroy', 'BulletFlightEndEffect'}:
                continue
            name = sanitize(os.path.splitext(os.path.basename(p))[0], 48)
            rel = os.path.relpath(p, self.base)
            bullet = 'BulletConfigComponent' in kinds
            my_id = guid_to_id.get(self.assets.guid_of(p), '')
            shooters = sorted({sanitize(os.path.splitext(os.path.basename(q))[0], 48) for q in refs.get(my_id, []) if q != p})
            for cls, data in comps:
                m = SOURCE_RE.search(data)
                if cls == 'InstantiateViewComponent' and bullet:
                    pm = PREFAB_RE.search(data)
                    view = self.path_of(pm.group(1)) if pm else None
                    if view:
                        self.bind('weapons', name, 'trail', view, rel)
                    continue
                if not m or cls not in EFFECT_COMPONENTS:
                    continue
                prefab = self.view_of_config(m.group(1))
                if prefab is None:
                    self.notes.append('%s: %s: effect %s has no view prefab in the registry' % (rel, cls, m.group(1)))
                    continue
                if cls == 'UnitEffectOnSpawnComponent':
                    self.bind('types', name, 'spawned', prefab, rel)
                elif cls == 'UnitEffectOnDestroyComponent':
                    self.bind('types', name, 'died', prefab, rel)
                elif cls == 'UnitEffectOnHitComponent':
                    eff = self.effect_for(prefab)['effect']
                    self.target_hit[eff] = self.target_hit.get(eff, 0) + 1
                elif cls in ('BulletEffectOnDestroy', 'EffectOnBulletDirectionalHitComponent'):
                    self.bind('weapons', name, 'hit', prefab, rel)
                elif cls == 'BulletFlightEndEffect':
                    for s in shooters or [name]:
                        self.bind('types', s, 'miss', prefab, rel, {'from': name})
            if bullet:
                self.map_entry(name, shooters)
        return self

    def map_entry(self, bullet, shooters):
        self.notes.append('bullet %s is fired by: %s' % (bullet, ', '.join(shooters) or '(no config refers to it)'))

    def fx_json(self):
        """The bindings as an fx/fx.json fragment (ids = config names)."""
        out = {}
        for b in self.bindings:
            v = b['effect']
            opts = {k: x for k, x in (b.get('options') or {}).items() if k in ('upright', 'clear')}
            if opts:
                v = dict({'effect': b['effect']}, **opts)
            out.setdefault(b['scope'], {}).setdefault(b['id'], {})[b['cue']] = v
        return out


def run(argv=None):
    ap = argparse.ArgumentParser(description='ME.BECS effect bindings -> AIVERSE fx bindings')
    ap.add_argument('--assets', required=True)
    ap.add_argument('--configs', action='append', default=[], help='config folders (relative to --assets); default ExtData/Configs')
    ap.add_argument('--index-cache', help='JSON cache of the GUID index (survey.py --index-cache)')
    ap.add_argument('--registry-cache', help='JSON cache of the ObjectReferenceRegistry')
    ap.add_argument('--out', help='write the plan here')
    ap.add_argument('--game', help='project to import into (with --import)')
    ap.add_argument('--import', dest='do_import', action='store_true', help='import every bound prefab and merge fx/fx.json')
    ap.add_argument('--json', action='store_true')
    opts = ap.parse_args(argv)
    base = os.path.abspath(os.path.expanduser(opts.assets))
    rep = Report()
    import survey
    assets = survey.load_index(base, rep, opts.index_cache)
    ids, _ = registry(base, opts.registry_cache)
    pl = Planner(base, assets, ids, rep).scan(opts.configs or ['ExtData/Configs'])
    plan = {'bindings': pl.bindings, 'fx': pl.fx_json(),
            'effects': [{'effect': v['effect'], 'prefab': os.path.relpath(k, base), 'modules': v['modules'], 'options': v['options']}
                        for k, v in sorted(pl.prefabs.items())],
            'notes': sorted(set(pl.notes)) + ['effect when a unit is hit (%s, %d configs): the engine "hit" cue belongs to the attacker\'s '
                                               'weapon; bind it per weapon or play it from a script' % kv for kv in sorted(pl.target_hit.items())]}
    if opts.do_import and opts.game:
        imp = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'import.py')
        done = []
        for e in plan['effects']:
            r = subprocess.run([sys.executable, imp, '--source', os.path.join(base, e['prefab']), '--game', opts.game,
                                '--name', e['effect'], '--json'], capture_output=True, text=True)
            try:
                res = json.loads(r.stdout)
            except ValueError:
                res = {'warnings': ['import failed: %s' % r.stderr[-300:]]}
            done.append({'effect': e['effect'], 'written': [x['name'] for x in res.get('effects', [])],
                         'dropped': len(res.get('dropped', [])), 'approximate': len(res.get('approximate', []))})
        plan['imported'] = done
        bank_path = os.path.join(opts.game, 'fx', 'fx.json')
        bank = {}
        if os.path.exists(bank_path):
            with open(bank_path, encoding='utf-8') as f:
                bank = json.load(f)
        for scope, items in plan['fx'].items():
            for ident, cues in items.items():
                for cue, v in cues.items():
                    bank.setdefault(scope, {}).setdefault(ident, {}).setdefault(cue, v)
        os.makedirs(os.path.dirname(bank_path), exist_ok=True)
        with open(bank_path, 'w', encoding='utf-8') as f:
            json.dump(bank, f, indent=1)
    text = json.dumps(plan, indent=1, ensure_ascii=False)
    if opts.out:
        with open(opts.out, 'w', encoding='utf-8') as f:
            f.write(text)
    if opts.json:
        print(text)
    else:
        print('%d bindings, %d effects, %d notes' % (len(pl.bindings), len(pl.prefabs), len(plan['notes'])))
        cues = {}
        for b in pl.bindings:
            cues[b['cue']] = cues.get(b['cue'], 0) + 1
        print('  by cue: ' + ', '.join('%s %d' % kv for kv in sorted(cues.items())))
    return 0


if __name__ == '__main__':
    sys.exit(run())
