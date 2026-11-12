#!/usr/bin/env python3
"""Unity materials / Shader Graph / texture settings tools (JSON output).

  cli.py survey PROJECT [--out FILE]      shader families, Shader Graph node coverage, hand-written shaders, texture plans
  cli.py graphs PROJECT --out FILE        every .shadergraph converted (material + particle domain) for aiverse_graphcheck
  cli.py material PROJECT Assets/X.mat    one converted material
  cli.py dryrun PROJECT --out DIR [--scenes]  materials-only import of every prefab (and scene): slot matching,
                                          materials.json, repacked maps, packaging rules, reports (low memory)
The full import (models, slots, materials.json, packaging rules) is tools/importers/port/aiverse_port.py.
"""
import argparse
import collections
import json
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.unity.unity_scene import Assets, Report  # noqa: E402
from unity_materials import shadergraph, textures, urp  # noqa: E402


def context(root):
    assets = Assets([str(root / 'Assets')], Report())
    ctx = urp.Context(root, assets, urp.project_linear(root))
    ctx.graphs = shadergraph.Library(ctx)
    return ctx


def survey(root):
    ctx = context(root)
    families, shaders, statuses = collections.Counter(), collections.Counter(), collections.Counter()
    plans, roles = [], {}
    for mat in sorted((root / 'Assets').rglob('*.mat')):
        docs = ctx.assets.docs(str(mat))
        doc = next((d for d in docs if d.class_id == 21), None)
        if doc is None:
            continue
        res = urp.convert(doc.data, ctx)
        families[res.family] += 1
        shaders[(res.family, res.shader, res.shader_path)] += 1
        statuses[res.status()] += 1
        for rel, role in res.textures:
            roles.setdefault(rel, set()).add(role)
    node_files = collections.Counter()
    graphs = {}
    for g in sorted((root / 'Assets').rglob('*.shadergraph')):
        rel = g.relative_to(root).as_posix()
        tpl = ctx.graphs.template(rel, 'material')
        graphs[rel] = dict(error=tpl.get('error'), nodes=len(tpl['graph']['nodes']) if 'graph' in tpl else 0,
                           unsupported=tpl.get('unsupported', {}), notes=tpl.get('notes', []))
        for t in tpl.get('nodeTypes', []):
            node_files[t] += 1
    for rel, rs in sorted(roles.items()):
        plans.append(textures.plan(rel, ctx.meta(rel), tuple(sorted(rs))))
    compress = collections.Counter((t, p['targets'].get(t, {}).get('compress') or 'none', p['targets'].get(t, {}).get('source'))
                                   for p in plans for t in ('android', 'ios'))
    supported = set(shadergraph.SUPPORTED)
    hand = sorted({k[2] for k in shaders if k[0] == 'custom'})
    return dict(
        materials=sum(families.values()), families=dict(families.most_common()), status=dict(statuses),
        shaders=[dict(family=f, shader=s, path=p, materials=n) for (f, s, p), n in shaders.most_common()],
        shaderGraphs=dict(files=len(graphs), converted=sum(1 for g in graphs.values() if not g['error']),
                          nodeTypes={t: dict(files=n, status='supported' if t in supported else 'replaced' if t in shadergraph.UNSUPPORTED else 'unknown')
                                     for t, n in node_files.most_common()},
                          graphs=graphs),
        handWrittenShaders=dict(used=hand, all=[p.relative_to(root).as_posix() for p in sorted((root / 'Assets').rglob('*.shader'))]),
        textures=dict(count=len(plans), compression={'%s %s (%s)' % k: n for k, n in sorted(compress.items(), key=str)}, plans=plans))


def graphs(root):
    ctx = context(root)
    out = {}
    for g in sorted((root / 'Assets').rglob('*.shadergraph')):
        rel = g.relative_to(root).as_posix()
        for domain in ('material', 'particle'):
            tpl = ctx.graphs.template(rel, domain)
            if 'graph' in tpl and not tpl.get('error'):
                graph = json.loads(json.dumps(tpl['graph']))
                for n in graph['nodes']:
                    n.pop('unity', None)
                    if n.get('texture', '').startswith('{texture:'):
                        n['texture'] = 'textures/placeholder.png'
                out[rel + '#' + domain] = graph
    return {'graphs': out}


def dryrun(root, out, scenes=False):
    """Material pipeline over every prefab without the full importer's component capture."""
    from lib.unity.unity_scene import Prefab
    from lib.unity.unity_yaml import get
    from lib.port.common import new_model
    from unity_materials import texops
    from unity_materials.project import Collector, MODEL_EXT
    model = new_model(root, 'unity')
    rep = Report()
    assets = Assets([str(root / 'Assets')], rep)
    col = Collector(root, assets, model)
    files = sorted((root / 'Assets').rglob('*.prefab')) + (sorted((root / 'Assets').rglob('*.unity')) if scenes else [])
    entities = []
    for f in files:
        rel = f.relative_to(root).as_posix()
        try:
            pf = Prefab(str(f), assets, rep)
        except Exception as e:  # noqa: BLE001 - one broken asset must not stop the survey
            model['items'].append(dict(id=rel, source=rel, status='missing', reason='prefab not parsed: %s' % e, result=''))
            continue
        ent = dict(id=rel, source=rel, models=[])
        for go, comps in pf.comp_by_go.items():
            rend = next((c for c in comps if c.class_id in (23, 137)), None)
            mesh = next((c for c in comps if c.class_id in (33, 137)), None)
            if rend is None or mesh is None:
                continue
            mrel = col.rel(mesh.data.get('m_Mesh') or {})
            if not mrel:
                continue
            col.add_renderer(ent, mrel, pf.name(go), rend.data.get('m_Materials') or [], rel + '#' + str(go),
                             meta=assets.meta(str(root / mrel)), mesh_file_id=get(mesh.data, 'm_Mesh', 'fileID'),
                             cast=str(rend.data.get('m_CastShadows', 1)) != '0')
        for inst, _parent in pf.model_instances:
            src = col.rel(inst.data.get('m_SourcePrefab') or {})
            if src.lower().endswith(MODEL_EXT):
                col.add_instance(ent, src, (inst.data.get('m_Modification') or {}).get('m_Modifications'), rel + '#' + str(inst.file_id))
        if ent['models']:
            entities.append(ent)
        for k in [k for k in assets.cache if k.endswith(('.prefab', '.unity'))]:
            del assets.cache[k]
    col.finish()
    out.mkdir(parents=True, exist_ok=True)
    lib = model['materialLibrary']
    (out / 'materials.json').write_text(json.dumps({'materials': {k: v['material'] for k, v in lib.items()}}, indent=1, sort_keys=True))
    derived, failed = {}, []
    for v in lib.values():
        for d in v['derived']:
            derived[d['out']] = d

    def write(relp, data):
        p = out / relp
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    for d in sorted(derived.values(), key=lambda d: d['out']):
        ok, note = texops.run(d, root, write)
        if not ok:
            failed.append(dict(out=d['out'], why=note))
    (out / 'packaging.json').write_text(json.dumps(textures.packaging(model['texturePlans']), indent=1, sort_keys=True))
    slots_out = {e['source']: e.get('materialSlots', {}) for e in entities}
    (out / 'slots.json').write_text(json.dumps(slots_out, indent=1, sort_keys=True))
    items = collections.Counter((i['status'], re.sub(r'[^:]*: ', '', i['reason'], count=1)[:60]) for i in model['items'])
    summary = dict(prefabs=len(files), withModels=len(entities), materials=len(lib), derivedTextures=len(derived), derivedFailed=failed,
                   families=dict(collections.Counter(v['family'] for v in lib.values())),
                   materialStatus=dict(collections.Counter(v['status'] for v in lib.values())),
                   items=dict(collections.Counter(i['status'] for i in model['items'])),
                   topReasons=[dict(status=k[0], reason=k[1], count=n) for k, n in items.most_common(40)],
                   handWrittenShaders=model.get('handWrittenShaders'), shaderGraphs=model.get('shaderGraphs'))
    (out / 'port-materials.json').write_text(json.dumps(dict(summary, materialLibrary=lib, items=model['items'],
                                                             texturePlans=model['texturePlans']), indent=1, sort_keys=True, default=list))
    return dict({k: summary[k] for k in ('prefabs', 'withModels', 'materials', 'derivedTextures', 'families', 'materialStatus', 'items')}, derivedFailed=len(failed))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=('survey', 'graphs', 'material', 'dryrun'))
    ap.add_argument('--scenes', action='store_true')
    ap.add_argument('project', type=Path)
    ap.add_argument('material', nargs='?')
    ap.add_argument('--out', type=Path)
    a = ap.parse_args(argv)
    root = a.project.resolve()
    if a.command == 'survey':
        data = survey(root)
    elif a.command == 'graphs':
        data = graphs(root)
    elif a.command == 'dryrun':
        if not a.out:
            ap.error('dryrun needs --out DIR')
        print(json.dumps(dryrun(root, a.out, a.scenes), indent=2, sort_keys=True))
        return 0
    else:
        ctx = context(root)
        doc = next((d for d in ctx.assets.docs(str(root / a.material)) if d.class_id == 21), None)
        if doc is None:
            print(json.dumps({'error': 'no material in ' + str(a.material)}))
            return 2
        data = urp.convert(doc.data, ctx).as_json()
    text = json.dumps(data, indent=2, sort_keys=True, default=list)
    if a.out:
        a.out.write_text(text + '\n')
        print(json.dumps({'ok': True, 'out': str(a.out)}))
    else:
        print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
