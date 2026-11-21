#!/usr/bin/env python3
"""Package official-reader JSON as glTF + VIVID DNA. No RigLogic evaluation.
Geometry is normalized only for the reference Creator camera; exact transform
and source-to-layout expansion are recorded. Material colours are placeholders:
MetaHuman DNA contains neither skin texture maps nor hair/groom assets.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def package(source, destination, yaw=0.0, weight=0.0, channel=None):
    if not math.isfinite(yaw) or not math.isfinite(weight) or not 0 <= weight <= 1:
        raise ValueError('Invalid pose parameters')
    if source.stat().st_size > 256 * 1024 * 1024:
        raise ValueError('Decoded source exceeds budget')
    data = json.loads(source.read_text())
    if data.get('version') != 1 or not data['meshes']:
        raise ValueError('Invalid decoded source')
    meshes = data['meshes']
    head = next((m for m in meshes if m['name'].startswith('head_')), None)
    if not head:
        raise ValueError('No head mesh; explicit framing required for this DNA')
    positions = head['positions']
    minimum = [min(v[a] for v in positions) for a in range(3)]
    maximum = [max(v[a] for v in positions) for a in range(3)]
    center = [(a+b)/2 for a,b in zip(minimum, maximum)]
    extent = maximum[1]-minimum[1]
    if not math.isfinite(extent) or extent <= 0:
        raise ValueError('Degenerate head bounds')
    scale = 3.2/extent
    blob = bytearray()
    doc = {'asset': {'version': '2.0', 'generator': 'VIVID offline MetaHuman DNA adapter'},
           'scene': 0, 'scenes': [{'nodes': [0]}],
           'nodes': [{'name': 'root', 'children': [1],
                      'rotation': [0, math.sin(yaw/2), 0, math.cos(yaw/2)]},
                     {'name': 'head', 'children': []}],
           'meshes': [], 'materials': [], 'buffers': [], 'bufferViews': [], 'accessors': []}
    def accessor(values, kind, component=5126):
        count = len(values)
        if not count:
            raise ValueError('Empty geometry array')
        width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[kind]
        flat = values if width == 1 else [v for row in values for v in row]
        if not all(math.isfinite(v) for v in flat):
            raise ValueError('Nonfinite geometry')
        while len(blob) % 4:
            blob.append(0)
        start = len(blob)
        blob.extend(struct.pack('<'+('I' if component == 5125 else 'f')*len(flat), *flat))
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': start, 'byteLength': len(blob)-start})
        result = {'bufferView': len(doc['bufferViews'])-1, 'componentType': component,
                  'count': count, 'type': kind}
        if kind == 'VEC3':
            result.update(min=[min(v[a] for v in values) for a in range(3)],
                          max=[max(v[a] for v in values) for a in range(3)])
        doc['accessors'].append(result)
        return len(doc['accessors'])-1
    morph_deltas = []
    offset = 0
    for mi, mesh in enumerate(meshes):
        layouts = mesh['layouts']
        if len(layouts)+offset > 1000000:
            raise ValueError('Vertex budget exceeded')
        changes = {int(i): d for i,d in mesh['deltas']}
        if len(changes) != len(mesh['deltas']):
            raise ValueError('Duplicate source morph index')
        pos, normals, uv = [], [], []
        for i,(p,n,t) in enumerate(layouts):
            if not (0 <= p < len(mesh['positions']) and 0 <= n < len(mesh['normals']) and 0 <= t < len(mesh['uv'])):
                raise ValueError('Invalid source layout')
            delta = changes.get(p, [0,0,0])
            pos.append([(mesh['positions'][p][a]-center[a]+weight*delta[a])*scale for a in range(3)])
            normals.append(mesh['normals'][n])
            uv.append([mesh['uv'][t][0], 1-mesh['uv'][t][1]])
            if p in changes:
                morph_deltas.append({'vertex': offset+i, 'position': [d*scale for d in delta]})
        indices = []
        for face in mesh['faces']:
            if len(face) < 3 or any(i < 0 or i >= len(layouts) for i in face):
                raise ValueError('Invalid polygon')
            for k in range(1, len(face)-1):
                indices.extend([face[0], face[k], face[k+1]])
        name = mesh['name'].lower()
        color = [0.56,0.36,0.27,1]
        if any(s in name for s in ['eye', 'teeth', 'saliva']):
            color = [0.78,0.78,0.73,1]
        if any(s in name for s in ['lash', 'shell', 'edge']):
            color = [0.08,0.055,0.04,1]
        doc['materials'].append({'name': mesh['name'], 'pbrMetallicRoughness':
                                {'baseColorFactor': color, 'metallicFactor': 0, 'roughnessFactor': .48}})
        # DNA has no opacity textures. Keep auxiliary geometry in the asset,
        # but hide untextured lashes/tear shells in this diagnostic material.
        if any(part in name for part in ['eyelashes', 'eyeshell', 'eyeedge', 'saliva']):
            doc['materials'][-1]['pbrMetallicRoughness']['baseColorFactor'][3] = 0
            doc['materials'][-1]['alphaMode'] = 'BLEND'
        attrs = {'POSITION': accessor(pos, 'VEC3'), 'NORMAL': accessor(normals, 'VEC3'),
                 'TEXCOORD_0': accessor(uv, 'VEC2')}
        doc['meshes'].append({'name': mesh['name'], 'primitives': [
            {'attributes': attrs, 'indices': accessor(indices, 'SCALAR', 5125), 'material': mi}]})
        doc['nodes'][1]['children'].append(len(doc['nodes']))
        doc['nodes'].append({'name': mesh['name'], 'mesh': mi})
        offset += len(layouts)
    if channel and not data['target']:
        raise ValueError('A semantic route requires an explicitly decoded target')
    doc['buffers'] = [{'uri': 'model.bin', 'byteLength': len(blob)}]
    manifest = {'version': 1, 'model': 'model.gltf', 'dna': 'vivid-dna.json',
                'materials': {m['name']: 'eyelash' for m in meshes if 'eyelashes' in m['name'].lower()}}
    dna = {'version': 1, 'id': 'metahuman-dna-preview',
           'displayName': data['name']+' — MetaHuman DNA geometry',
           'rig': {'rootBone': 'root', 'headBone': 'head'}}
    output = {'model.gltf': doc, 'vivid-dna.json': dna,
              'provenance.json': {'sourceJsonSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
               'name': data['name'], 'lod': data['lod'], 'vertices': offset,
               'sourceCenter': center, 'sourceScale': scale, 'yaw': yaw,
               'bakedTarget': data['target'] if weight else '', 'bakedWeight': weight,
               'rigLogicEvaluated': False, 'materials': 'untextured diagnostic placeholders',
               'limitations': ['no joint-driven expressions', 'no skinning or RigLogic behavior',
                              'no textures or groom in DNA; untextured lashes/tear shells hidden', 'fan-triangulated source polygons']}}
    if channel:
        if weight:
            raise ValueError('Do not combine baked and live target to avoid double deformation')
        manifest.update(morphs='morphs.json', routes=[{'channel': channel, 'target': data['target'], 'gain': 1}])
        output['morphs.json'] = {'version': 1, 'vertices': offset,
                               'targets': [{'name': data['target'], 'deltas': morph_deltas}]}
    output['manifest.json'] = manifest
    destination.mkdir(parents=True, exist_ok=False)
    (destination/'model.bin').write_bytes(blob)
    for name,value in output.items():
        (destination/name).write_text(json.dumps(value, ensure_ascii=False, separators=(',', ':'), allow_nan=False))
    return output['provenance.json']

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('destination', type=Path)
    p.add_argument('--yaw', type=float, default=0)
    p.add_argument('--weight', type=float, default=0)
    p.add_argument('--channel', help='Explicit VIVID semantic channel; no automatic RigLogic mapping')
    a = p.parse_args()
    print(json.dumps(package(a.source, a.destination, a.yaw, a.weight, a.channel), indent=2))
