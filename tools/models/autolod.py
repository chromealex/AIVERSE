#!/usr/bin/env python3
"""Offline static and opt-in skinned glTF LOD generation. Requires the aiverse_lod_simplify host worker."""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
from urllib.parse import unquote, quote

COMPONENTS = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}
FORMATS = {5120: 'b', 5121: 'B', 5122: 'h', 5123: 'H', 5125: 'I', 5126: 'f'}


def local_path(root, uri):
    if not uri or ':' in uri or uri.startswith('/'):
        raise ValueError('only relative external buffer/image URIs are supported')
    return (root / unquote(uri)).resolve()


class Asset:
    def __init__(self, path):
        self.path = path.resolve()
        self.doc = json.loads(path.read_text())
        self.buffers = [local_path(path.parent, b['uri']).read_bytes() for b in self.doc['buffers']]

    def accessor(self, index):
        a = self.doc['accessors'][index]
        if 'sparse' in a:
            raise ValueError('sparse accessor requires unpacking before autolod')
        view = self.doc['bufferViews'][a['bufferView']]
        if view.get('extensions'):
            raise ValueError('compressed buffer view requires unpacking before autolod')
        fmt = '<' + FORMATS[a['componentType']] * COMPONENTS[a['type']]
        size = struct.calcsize(fmt)
        stride = view.get('byteStride', size)
        offset = a.get('byteOffset', 0)
        count = a['count']
        start = view.get('byteOffset', 0)
        data = self.buffers[view['buffer']]
        if count <= 0 or stride < size or offset < 0 or start < 0 or start + view['byteLength'] > len(data) or offset + (count-1)*stride + size > view['byteLength']:
            raise ValueError('invalid accessor bounds')
        return a, data, start+offset, stride, size, fmt

    def values(self, index):
        a, data, start, stride, _, fmt = self.accessor(index)
        values = [struct.unpack_from(fmt, data, start+i*stride) for i in range(a['count'])]
        if a.get('normalized'):
            c = a['componentType']
            divisor = {5120:127, 5121:255, 5122:32767, 5123:65535}.get(c)
            if divisor is None:
                raise ValueError('invalid normalized accessor')
            values = [tuple(max(-1.0, v/divisor) for v in row) for row in values]
        return values


def generate(source, output, worker, min_triangles=128, skinned=False):
    source, output = source.resolve(), output.resolve()
    if source.suffix.lower() != '.gltf' or output.suffix.lower() != '.gltf':
        raise ValueError('input and output must be unpacked .gltf files')
    if source.resolve() == output.resolve():
        raise ValueError('output must differ from source')
    asset = Asset(source)
    doc = copy.deepcopy(asset.doc)
    safe_extensions = {'KHR_lights_punctual', 'KHR_texture_transform', 'KHR_materials_unlit', 'KHR_materials_emissive_strength', 'KHR_materials_ior', 'KHR_materials_specular', 'KHR_materials_clearcoat', 'KHR_materials_transmission', 'KHR_materials_volume', 'KHR_materials_sheen'}
    if set(doc.get('extensionsRequired', [])) - safe_extensions:
        raise ValueError('unpack required glTF extensions before autolod')
    if any(m.get('extras', {}).get('rtsLods') for m in doc['meshes']):
        raise ValueError('asset already has LODs; use its original source')
    output.parent.mkdir(parents=True, exist_ok=True)
    binary_path = output.with_suffix('.lods.bin')
    protected = {source.resolve()}
    # Keep source buffers, textures, animation and LOD0 byte-for-byte; rewrite relative URIs.
    for kind in ('buffers', 'images'):
        for item in doc.get(kind, []):
            if 'uri' in item:
                original = local_path(source.parent, item['uri'])
                protected.add(original)
                item['uri'] = quote(os.path.relpath(original, output.parent), safe='/')
    report_path = output.with_suffix('.lods.json')
    if any(p.resolve() in protected for p in (output, binary_path, report_path)):
        raise ValueError('output collides with source resource')
    binary = bytearray()
    buffer_index = len(doc['buffers'])
    report = {'generator':'meshoptimizer v1.3', 'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(), 'meshes':[]}
    skin_meshes = {n['mesh'] for n in doc.get('nodes', []) if 'mesh' in n and 'skin' in n}

    def append(data, spec):
        binary.extend(b'\0' * (-len(binary) % 4))
        view = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer':buffer_index, 'byteOffset':len(binary), 'byteLength':len(data)})
        binary.extend(data)
        spec = copy.deepcopy(spec)
        spec.pop('byteOffset', None)
        spec['bufferView'] = view
        index = len(doc['accessors'])
        doc['accessors'].append(spec)
        return index

    with tempfile.TemporaryDirectory(prefix='aiverse-lod-') as tmp:
        input_file, result_file = Path(tmp)/'input.bin', Path(tmp)/'result.bin'
        for mi, mesh in enumerate(asset.doc['meshes']):
            summary = {'mesh':mi, 'levels':[]}
            report['meshes'].append(summary)
            has_skin = mi in skin_meshes or any('JOINTS_0' in p['attributes'] for p in mesh['primitives'])
            if mesh.get('weights') or any(p.get('targets') for p in mesh['primitives']) or (has_skin and not skinned):
                summary['skipped'] = 'morph targets unsupported; skinning requires --skinned'
                continue
            prepared = []
            for p in mesh['primitives']:
                if p.get('mode', 4) != 4 or p.get('extensions'):
                    raise ValueError(f'mesh {mi}: only uncompressed triangle primitives supported')
                pos = asset.values(p['attributes']['POSITION'])
                if any(len(v)!=3 or not all(math.isfinite(x) for x in v) for v in pos):
                    raise ValueError('invalid positions')
                indices = [v[0] for v in asset.values(p['indices'])] if 'indices' in p else list(range(len(pos)))
                if not indices or len(indices)%3 or any(not isinstance(v,int) or v<0 or v>=len(pos) for v in indices):
                    raise ValueError('invalid triangle indices')
                columns, weights = [], []
                for key, ai in p['attributes'].items():
                    values = asset.values(ai)
                    if len(values)!=len(pos):
                        raise ValueError('attribute vertex count mismatch')
                    if key == 'POSITION' or key.startswith(('JOINTS_', 'WEIGHTS_')):
                        continue
                    weight = 10.0 if key.startswith('TEXCOORD_') else 1.0
                    columns.append(values)
                    weights.extend([weight]*len(values[0]))
                if has_skin:
                    at = p['attributes']
                    if 'JOINTS_0' not in at or 'WEIGHTS_0' not in at or 'JOINTS_1' in at or 'WEIGHTS_1' in at:
                        raise ValueError('skinned LOD requires exactly four joint influences per vertex')
                    joints = asset.values(at['JOINTS_0']); influence = asset.values(at['WEIGHTS_0'])
                    if len(joints)!=len(pos) or len(influence)!=len(pos):
                        raise ValueError('skin attribute vertex count mismatch')
                    skins = [doc['skins'][n['skin']] for n in doc.get('nodes', []) if n.get('mesh')==mi and 'skin' in n]
                    if not skins: raise ValueError('skin attributes require a skin binding')
                    for js, ws in zip(joints, influence):
                        if len(js)!=4 or len(ws)!=4 or any(not math.isfinite(w) or w<0 for w in ws) or abs(sum(ws)-1)>0.001:
                            raise ValueError('invalid normalized skin weights')
                        if any(not isinstance(j,int) or j<0 or any(j>=len(s['joints']) for s in skins) for j,w in zip(js,ws) if w>0):
                            raise ValueError('skin joint index out of range')
                    # Bone IDs are categorical: never simplify their numeric labels. A dense,
                    # canonical weight field makes slot order irrelevant to the error metric.
                    used=sorted({j for js,ws in zip(joints,influence) for j,w in zip(js,ws) if w>0})
                    dense=[tuple(sum(w for j,w in zip(js,ws) if j==bone) for bone in used) for js,ws in zip(joints,influence)]
                    columns.append(dense); weights.extend([8.0]*len(used))
                    summary['skinMetric']='canonical joint weights (8x); original influences retained'
                if len(weights)>32:
                    raise ValueError('more than 32 simplification attributes; split joint/material regions first')
                attrs = [v for i in range(len(pos)) for column in columns for v in column[i]]
                prepared.append((p,pos,indices,attrs,weights))
            original_count = sum(len(p[2]) for p in prepared)
            summary['sourceTriangles'] = original_count//3
            if original_count//3 < min_triangles:
                summary['skipped'] = 'below minimum triangle count'
                continue
            lods = []
            previous_count = original_count
            previous_pixels = math.inf
            for level, (ratio, limit) in enumerate(((0.5,0.005),(0.25,0.01),(0.125,0.02)), 1):
                results, max_error, count = [], 0.0, 0
                for p,pos,indices,attrs,weights in prepared:
                    # Tiny submeshes (e.g. a wick) retain their exact geometry.
                    result, error = indices, 0.0
                    if len(indices)//3 >= min_triangles:
                        header = struct.pack('<4I2f', len(pos),len(indices),len(weights),0x31444f4c,ratio,limit)
                        with input_file.open('wb') as f:
                            f.write(header)
                            for data, fmt in (([v for row in pos for v in row],'f'),(attrs,'f'),(weights,'f'),(indices,'I')):
                                f.write(struct.pack('<'+str(len(data))+fmt,*data))
                        subprocess.run([str(worker),str(input_file),str(result_file)],check=True)
                        raw=result_file.read_bytes()
                        n,error=struct.unpack_from('<If',raw)
                        result=list(struct.unpack_from('<'+str(n)+'I',raw,8))
                        if n==0 or n%3 or any(i>=len(pos) for i in result):
                            raise ValueError('invalid simplifier result')
                    max_error=max(max_error,error)
                    count+=len(result)
                    results.append((p,pos,result))
                if count > previous_count*0.8:
                    continue
                primitives=[]
                for p,pos,result in results:
                    # Compact every attribute, including tangents/colors, to avoid full vertex fetch on mobile.
                    unique=list(dict.fromkeys(result))
                    remap={v:i for i,v in enumerate(unique)}
                    new=copy.deepcopy(p)
                    new['attributes']={}
                    for key,ai in p['attributes'].items():
                        spec,data,start,stride,size,_=asset.accessor(ai)
                        spec=copy.deepcopy(spec)
                        spec['count']=len(unique)
                        spec.pop('min',None);spec.pop('max',None)
                        if key=='POSITION':
                            spec['min']=[min(pos[v][c] for v in unique) for c in range(3)]
                            spec['max']=[max(pos[v][c] for v in unique) for c in range(3)]
                        packed=b''.join(data[start+v*stride:start+v*stride+size] for v in unique)
                        new['attributes'][key]=append(packed,spec)
                    packed=struct.pack('<'+str(len(result))+'I',*(remap[i] for i in result))
                    new['indices']=append(packed,{'componentType':5125,'count':len(result),'type':'SCALAR'})
                    primitives.append(new)
                pixels=min(256/(2**(level-1)),0.5/max(max_error,1e-9),previous_pixels*0.5)
                lods.append({'mesh':len(doc['meshes']),'maxPixels':pixels})
                doc['meshes'].append({'name':mesh.get('name',str(mi))+f' LOD{level}','primitives':primitives})
                summary['levels'].append({'triangles':count//3,'relativeErrorEstimate':max_error,'maxPixels':pixels})
                previous_count,previous_pixels=count,pixels
            if lods:
                doc['meshes'][mi].setdefault('extras',{})['rtsLods']=lods
    if binary:
        doc['buffers'].append({'uri':quote(binary_path.name),'byteLength':len(binary)})
        binary_path.write_bytes(binary)
    output.write_text(json.dumps(doc,separators=(',',':'))+'\n')
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path)
    p.add_argument('output',type=Path)
    p.add_argument('--worker',required=True,type=Path)
    p.add_argument('--skinned',action='store_true',help='preserve skin attributes using canonical per-joint weight error')
    p.add_argument('--min-triangles',type=int,default=128)
    a=p.parse_args()
    if a.min_triangles<1:
        p.error('--min-triangles must be positive')
    print(json.dumps(generate(a.source,a.output,a.worker.resolve(),a.min_triangles,a.skinned),indent=2))

if __name__=='__main__':
    main()
