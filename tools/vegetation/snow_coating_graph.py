"""Opt-in PBR snow coating over an existing material graph (no new textures).

Static authoring composition: caller owns weather, season and accumulation.
Preserves alpha silhouettes and vertex outputs. Not snow simulation/displacement.
"""
import copy
import json
import math
from pathlib import Path


def snow_coating_graph(base=None, coverage=1.0):
    if not math.isfinite(coverage) or not 0 <= coverage <= 1:
        raise ValueError('snow coverage0..1')
    graph = copy.deepcopy(base) if base is not None else dict(
        version=1, domain='material', minQuality='low', nodes=[], outputs={})
    if graph.get('version') != 1 or graph.get('domain') != 'material':
        raise ValueError('material graph version1 required')
    nodes = graph['nodes']; outputs = graph['outputs']
    memo = {json.dumps([v['op'], v.get('inputs', []),
                       {k: x for k, x in v.items() if k not in ('id', 'op', 'inputs')}],
                       sort_keys=True): v['id'] for v in nodes}
    ids = {v['id'] for v in nodes}

    def n(op, *inputs, **props):
        signature = json.dumps([op, list(inputs), props], sort_keys=True)
        if signature in memo:
            return memo[signature]
        i = len(nodes)
        while 'snow' + str(i) in ids:
            i += 1
        key = 'snow' + str(i); ids.add(key)
        node = dict(id=key, op=op, **props)
        if inputs:
            node['inputs'] = list(inputs)
        nodes.append(node); memo[signature] = key
        return key

    def c(value):
        return n('constant', value=value)

    color = outputs.get('color') or n('input')
    if coverage == 0:
        outputs['color'] = color
        return graph
    normal = n('normal')
    uv = n('multiply', n('swizzle', n('worldPosition'), mask='rbra'), c(6))
    grain = n('gradientNoise', uv)
    facing = n('add', n('green', normal),
               n('multiply', n('subtract', grain, c(.5)), c(.12)))
    mask = n('multiply', n('smoothstep', c(.28), c(.78), facing), c(coverage))
    snow = n('mix', c([.68, .75, .83, 1]), c([.89, .94, 1, 1]), grain)
    outputs['color'] = n('setAlpha', n('mix', color, snow, mask), n('alpha', color))
    outputs['roughness'] = n('mix', outputs.get('roughness') or c(.85), c(.92), mask)
    if 'occlusion' in outputs:
        outputs['occlusion'] = n('mix', outputs['occlusion'], c(1), mask)
    outputs['metallic'] = n('mix', outputs.get('metallic') or c(0), c(0), mask)
    outputs['normal'] = n('normalize3', n('mix', outputs.get('normal') or normal, normal, mask))
    if len(nodes) > 128:
        raise ValueError('snow coating exceeds128 node budget')
    return graph


def build_library(destination):
    destination = Path(destination)
    if destination.exists():
        raise ValueError('output exists; refusing overwrite')
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pkg'))
    from aiverse_pkg import package_dir
    source = package_dir('vegetation-pbr')  # AIVERSE-PACKAGES catalog
    destination.mkdir(parents=True)
    materials = {}
    for name in json.loads((source / 'materials.json').read_text()):
        bark = name.endswith('_bark')
        base = json.loads((source / 'bark_projection' / (name + '.json')).read_text()) if bark else None
        graph = snow_coating_graph(base,1.0 if bark else .65)
        key = name + '_snow'
        materials[key] = dict(base=name + '_projected' if bark else name,
                             description='Opt-in upward-facing PBR snow coating',
                             thinTransmission=0, shaderGraph=graph)
        (destination / (key + '.json')).write_text(json.dumps(graph, indent=2))
    (destination / 'materials.json').write_text(json.dumps(materials, indent=2))
    return {name: len(material['shaderGraph']['nodes']) for name, material in materials.items()}


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build_library(args.out)))
