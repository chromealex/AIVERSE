#!/usr/bin/env python3
"""Unity materials / Shader Graph / texture settings tools (JSON output).

  cli.py survey PROJECT [--out FILE]      shader families, Shader Graph node coverage, hand-written shaders, texture plans
  cli.py graphs PROJECT --out FILE        every .shadergraph converted (material + particle domain) for aiverse_graphcheck
  cli.py material PROJECT Assets/X.mat    one converted material
The full import (models, slots, materials.json, packaging rules) is tools/importers/port/aiverse_port.py.
"""
import argparse
import collections
import json
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


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=('survey', 'graphs', 'material'))
    ap.add_argument('project', type=Path)
    ap.add_argument('material', nargs='?')
    ap.add_argument('--out', type=Path)
    a = ap.parse_args(argv)
    root = a.project.resolve()
    if a.command == 'survey':
        data = survey(root)
    elif a.command == 'graphs':
        data = graphs(root)
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
