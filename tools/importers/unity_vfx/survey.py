#!/usr/bin/env python3
"""Which effects does a Unity game really use, and how much of them can the importer bring over?

    python3 tools/importers/unity_vfx/survey.py --assets <Unity>/Assets [--root ExtData --root ExtGameplay ...]
        [--exclude ExtData/VFX/Imported] [--convert] [--json] [--out survey.json]

Starts from the roots (default: Scenes, Resources, AddressableAssetsData and the folders named by --root), follows
every GUID reference (`guid:` and Addressables `m_GUID:`) through text assets and counts, over the reached
prefabs, the effect components: ParticleSystem (Shuriken), TrailRenderer, LineRenderer, VisualEffect (VFX Graph),
URP DecalProjector, plus the [SerializeReference] view modules that drive them (`class: X` entries). With
--convert every reached prefab with effects runs through import.py in dry-run mode and .vfx graphs through
vfxgraph.py; the report aggregates what was mapped / approximated / dropped. Writes nothing into the Unity project.
"""
import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unity_scene import Assets, Report  # noqa: E402

TEXT_EXT = {'.prefab', '.asset', '.unity', '.mat', '.vfx', '.controller', '.overridecontroller', '.playable',
            '.spriteatlas', '.vfxoperator', '.vfxblock', '.shadergraph', '.mixer', '.lighting'}
GUID_RE = re.compile(r'(?:guid|m_GUID|m_AssetGUID): ([0-9a-f]{32})')
CLASS_RE = re.compile(r'type: \{class: (\w+), ns: ([\w.]*), asm: ([\w.-]*)\}')
DOC_RE = re.compile(r'^--- !u!(\d+) &-?\d+( stripped)?', re.M)
COMPONENTS = {198: 'ParticleSystem', 96: 'TrailRenderer', 120: 'LineRenderer', 2083052967: 'VisualEffect',
              108: 'Light', 330: 'ParticleSystemForceField'}
# URP DecalProjector has no stable class id (a MonoBehaviour from a package that may be missing): its fields are.
DECAL_FIELDS = ('m_ProjectionDepth', 'm_DrawDistance', 'm_ScaleMode')


def load_index(base, rep, cache):
    if cache and os.path.exists(cache):
        with open(cache, 'r', encoding='utf-8') as f:
            data = json.load(f)
        if data.get('assets') == base:
            a = Assets([], rep)
            a.by_guid = data['byGuid']
            a.guid_by_path = {p: g for g, p in a.by_guid.items()}
            return a
    a = Assets([base], rep)
    if cache:
        with open(cache, 'w', encoding='utf-8') as f:
            json.dump({'assets': base, 'byGuid': a.by_guid}, f)
    return a


class SurveyTextures:
    """Texture stand-in for surveys: names the output without decoding images (decoding is what makes dry runs slow)."""

    def __init__(self, rep):
        self.rep = rep

    def get(self, info):
        if info.builtin_texture or not info.texture_path:
            return 'builtin:soft'
        return self.convert(info.texture_path)

    def convert(self, src, blend='additive', rect=None, label=None):
        return 'fx/textures/%s.png' % os.path.splitext(os.path.basename(label or src))[0]


def reach(assets, roots, excludes):
    """Breadth-first GUID closure from every asset under the roots."""
    seen, order, queue = set(), [], []
    excl = [os.path.abspath(e) + os.sep for e in excludes]

    def blocked(p):
        a = os.path.abspath(p)
        return any(a.startswith(e) for e in excl)

    for r in roots:
        for dp, dns, fns in os.walk(r):
            dns[:] = sorted(dns)
            for fn in sorted(fns):
                p = os.path.join(dp, fn)
                if not fn.endswith('.meta') and not blocked(p) and p not in seen:
                    seen.add(p)
                    queue.append(p)
    refs_by = {}
    while queue:
        p = queue.pop(0)
        order.append(p)
        if os.path.splitext(p)[1].lower() not in TEXT_EXT:
            continue
        try:
            with open(p, 'r', encoding='utf-8', errors='replace') as f:
                txt = f.read()
        except OSError:
            continue
        if not txt.startswith('%YAML'):
            continue
        refs = set(GUID_RE.findall(txt))
        refs_by[p] = refs
        for g in sorted(refs):
            q = assets.by_guid.get(g)
            if q and q not in seen and not os.path.isdir(q):
                seen.add(q)
                queue.append(q)
    return order, refs_by


def scan_prefab(path):
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        txt = f.read()
    comps = collections.Counter()
    for m in DOC_RE.finditer(txt):
        cid = int(m.group(1))
        if cid in COMPONENTS and not m.group(2):
            comps[COMPONENTS[cid]] += 1
    if all(k in txt for k in DECAL_FIELDS):
        comps['DecalProjector'] += txt.count('m_ProjectionDepth')
    mods = collections.Counter(m.group(1) for m in CLASS_RE.finditer(txt))
    nested = 'm_SourcePrefab:' in txt
    return comps, mods, nested


def categorize(line):
    """'x.prefab: system: module: what' -> 'module: what' with numbers dropped (groups similar findings)."""
    parts = line.split(': ')
    key = ': '.join(parts[-2:]) if len(parts) >= 3 else parts[-1]
    key = re.sub(r'\b[0-9a-f]{32}\b', '<guid>', key)
    key = re.sub(r'-?\d+(\.\d+)?', '#', key)
    return key[:140]


def run(argv=None):
    ap = argparse.ArgumentParser(description='Survey the effects a Unity game uses')
    ap.add_argument('--assets', required=True, help='the Unity project Assets folder')
    ap.add_argument('--root', action='append', default=[], help='extra root folder (relative to --assets); repeatable')
    ap.add_argument('--exclude', action='append', default=[], help='folder not used as a root (still reachable by reference)')
    ap.add_argument('--convert', action='store_true', help='dry-run the importers over the reached effects')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--out', help='also write the JSON report here')
    ap.add_argument('--reuse', help='a previous --out report: skip the reference walk and use its effect list')
    ap.add_argument('--slice', help='i/n: convert only the i-th of n equal parts (large projects; merge with --merge)')
    ap.add_argument('--index-cache', help='JSON file caching the GUID index of --assets (slow on network mounts)')
    ap.add_argument('--merge', nargs='+', help='merge the conversion parts of several --slice reports into --out')
    opts = ap.parse_args(argv)
    base = os.path.abspath(os.path.expanduser(opts.assets))
    if opts.merge:
        return merge(opts)
    rep = Report()
    assets = load_index(base, rep, opts.index_cache)
    if opts.reuse:
        with open(opts.reuse, 'r', encoding='utf-8') as f:
            out = json.load(f)
        per = out['effects']
        vfx = [os.path.join(base, v) for v in out['vfxGraphs']]
        if opts.slice:
            i, n = (int(x) for x in opts.slice.split('/'))
            per = per[len(per) * i // n:len(per) * (i + 1) // n]
            vfx = vfx[len(vfx) * i // n:len(vfx) * (i + 1) // n]
        out['conversion'] = convert_all(base, assets, per, vfx)
        return emit(out, opts)
    roots = [os.path.join(base, r) for r in ['Scenes', 'Resources', 'AddressableAssetsData'] + opts.root]
    roots = [r for r in roots if os.path.isdir(r)]
    excludes = [os.path.join(base, e) for e in opts.exclude]
    order, refs_by = reach(assets, roots, excludes)
    used_prefabs = [p for p in order if p.lower().endswith('.prefab')]
    used_vfx = [p for p in order if p.lower().endswith('.vfx')]
    totals, modules, per = collections.Counter(), collections.Counter(), []
    for p in used_prefabs:
        comps, mods, nested = scan_prefab(p)
        modules.update(mods)
        if comps:
            totals.update(comps)
            totals['prefabs with effects'] += 1
            per.append({'prefab': os.path.relpath(p, base), 'components': dict(comps),
                        'modules': sorted(m for m in mods if m.endswith('Module') or 'Particle' in m or 'Vfx' in m)})
    out = {'assets': base, 'roots': [os.path.relpath(r, base) for r in roots], 'reached': len(order),
           'prefabs': len(used_prefabs), 'vfxGraphs': [os.path.relpath(v, base) for v in used_vfx],
           'totals': dict(totals), 'viewModules': dict(modules.most_common()), 'effects': per}
    if opts.convert:
        out['conversion'] = convert_all(base, assets, per, used_vfx)
    return emit(out, opts)


def merge(opts):
    out = None
    dropped, approx = collections.Counter(), collections.Counter()
    for p in opts.merge:
        with open(p, 'r', encoding='utf-8') as f:
            part = json.load(f)
        c = part.get('conversion') or {}
        if out is None:
            out = part
            out['conversion'] = {'prefabs': 0, 'effects': 0, 'emitters': 0, 'failed': 0, 'errors': [], 'perPrefab': [], 'vfxGraphs': []}
        m = out['conversion']
        for k in ('prefabs', 'effects', 'emitters', 'failed'):
            m[k] += c.get(k, 0)
        for k in ('errors', 'perPrefab', 'vfxGraphs'):
            m[k] += c.get(k, [])
        dropped.update(dict(c.get('dropped', [])))
        approx.update(dict(c.get('approximate', [])))
    out['conversion']['dropped'] = dropped.most_common()
    out['conversion']['approximate'] = approx.most_common()
    return emit(out, opts)


def emit(out, opts):
    totals = out['totals']
    text = json.dumps(out, indent=1, ensure_ascii=False)
    if opts.out:
        with open(opts.out, 'w', encoding='utf-8') as f:
            f.write(text)
    if opts.json:
        print(text)
    else:
        print('reached %d assets, %d prefabs, %d VFX graphs' % (out['reached'], out['prefabs'], len(out['vfxGraphs'])))
        for k, v in sorted(totals.items()):
            print('  %-26s %d' % (k, v))
        if 'conversion' in out:
            c = out['conversion']
            print('conversion: %(effects)d effects / %(emitters)d emitters from %(prefabs)d prefabs; %(failed)d failed' % c)
            for title in ('dropped', 'approximate'):
                print('  top %s:' % title)
                for k, n in c[title][:15]:
                    print('    %4d  %s' % (n, k))
    return 0


def convert_all(base, assets, per, vfx_paths):
    """Dry-run import.py over every prefab with effects (and vfxgraph.py over the graphs): aggregated report."""
    import importlib.util
    import tempfile
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location('unity_vfx_import', os.path.join(here, 'import.py'))
    imp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(imp)
    agg = {'prefabs': 0, 'effects': 0, 'emitters': 0, 'failed': 0, 'errors': [], 'perPrefab': []}
    dropped, approx = collections.Counter(), collections.Counter()
    game = tempfile.mkdtemp(prefix='fxsurvey_')

    class Opts:
        dry_run, scale, ignore_root_rotation, textures, meshes = True, 1.0, False, None, None
        used_names = set()

    for e in per:
        path = os.path.join(base, e['prefab'])
        rep = Report()
        opts = Opts()
        opts.used_names = set()
        opts.textures = SurveyTextures(rep)
        name = imp.sanitize(os.path.splitext(os.path.basename(path))[0], 40)
        agg['prefabs'] += 1
        assets.rep = rep
        try:
            outs = imp.import_prefab(path, assets, opts, rep, name, game, quiet=True)
        except Exception as ex:  # report and continue: one broken prefab must not hide the others
            agg['failed'] += 1
            agg['errors'].append('%s: %s: %s' % (e['prefab'], type(ex).__name__, ex))
            continue
        em = sum(len(x[1]['emitters']) for x in outs)
        agg['effects'] += len(outs)
        agg['emitters'] += em
        dropped.update(categorize(x) for x in rep.dropped)
        approx.update(categorize(x) for x in rep.approx)
        agg['perPrefab'].append({'prefab': e['prefab'], 'effects': len(outs), 'emitters': em,
                                 'approximate': len(rep.approx), 'dropped': len(rep.dropped)})
    if vfx_paths:
        try:
            import vfxgraph
        except ImportError:
            vfxgraph = None
        graphs = []
        for v in vfx_paths:
            if vfxgraph is None:
                graphs.append({'graph': os.path.relpath(v, base), 'status': 'no converter'})
                continue
            rep = Report()
            try:
                g = vfxgraph.convert_file(v, assets, rep, name=os.path.splitext(os.path.basename(v))[0], game_dir=game, dry_run=True)
                graphs.append({'graph': os.path.relpath(v, base), 'emitters': sum(len(x[1]['emitters']) for x in g),
                               'unsupported': len(rep.dropped), 'approximate': len(rep.approx)})
                dropped.update('vfx: ' + categorize(x) for x in rep.dropped)
                approx.update('vfx: ' + categorize(x) for x in rep.approx)
            except Exception as ex:
                graphs.append({'graph': os.path.relpath(v, base), 'error': '%s: %s' % (type(ex).__name__, ex)})
        agg['vfxGraphs'] = graphs
    agg['dropped'] = dropped.most_common()
    agg['approximate'] = approx.most_common()
    return agg


if __name__ == '__main__':
    sys.exit(run())
