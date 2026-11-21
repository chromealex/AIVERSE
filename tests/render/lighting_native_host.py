"""Load a real glTF through renderbench, exercise GI selection and compare pixels."""
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

binary, folder, expected_auto = sys.argv[1:]
root = Path(folder)
root.mkdir(parents=True, exist_ok=True)
# Receiver at z=0, side wall at x=2, both visible real scene geometry.
positions = [(-4,-4,0),(4,-4,0),(4,4,0),(-4,4,0),
             (2,-4,0),(2,4,0),(2,4,4),(2,-4,4)]
normals = [(0,0,1)]*4 + [(-1,0,0)]*4
indices = [0,1,2,0,2,3,4,5,6,4,6,7]
blob = b''.join(struct.pack('<3f', *v) for v in positions+normals)
blob += struct.pack('<12H', *indices)
gltf = {"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
        "nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],
        "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"metallicFactor":0,"roughnessFactor":1},"doubleSided":True}],
        "buffers":[{"uri":"data:application/octet-stream;base64,"+base64.b64encode(blob).decode(),"byteLength":len(blob)}],
        "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":96},{"buffer":0,"byteOffset":96,"byteLength":96},{"buffer":0,"byteOffset":192,"byteLength":24}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":8,"type":"VEC3","min":[-4,-4,0],"max":[4,4,4]},
                     {"bufferView":1,"componentType":5126,"count":8,"type":"VEC3"},
                     {"bufferView":2,"componentType":5123,"count":12,"type":"SCALAR"}]}
(root/'scene.gltf').write_text(json.dumps(gltf))
base = {"id":"native-dynamic-gi-acceptance","model":"scene.gltf","width":80,"height":80,
        "sunDirection":[0,1,0],"sunColor":[1,1,1],"sunIntensity":0,"skyColor":[0,0,0],"groundColor":[0,0,0],
        "cameras":[{"name":"receiver","eye":[0,0,10],"target":[0,0,0]}]}
gi = {"origin":[-2,-2,-1],"dimensions":[4,4,4],"spacing":1,"samples":64,"distance":8,
      "sunDirection":[-1,0,0],"sunColor":[1,.5,.25],"sunIntensity":2,"skyColor":[.2,.4,1],"skyIntensity":.3}
values = {}
for mode in ('off', 'cpu', 'auto', 'capacity', 'reject'):
    recipe = dict(base)
    if mode != 'off':
        recipe['dynamicGI'] = dict(gi, backend='gpu' if mode == 'reject' else 'auto' if mode == 'capacity' else mode)
    if mode in ('capacity','reject'):
        recipe['pointLights'] = [dict(position=[0,0,1],color=[0,0,0],radius=1,intensity=0) for _ in range(17)]
    scene = root/(mode+'.json')
    scene.write_text(json.dumps(recipe))
    report, capture = root/(mode+'-report.json'), root/(mode+'.ppm')
    report.unlink(missing_ok=True)
    capture.unlink(missing_ok=True)
    with (root/(mode+'.log')).open('w') as log:
        result = subprocess.run([binary,'--scene',str(scene),'--profile','hdr','--camera','0',
                                 '--warmup','3','--samples','2','--output',str(report),'--capture',str(capture)],
                                env=dict(os.environ,AIVERSE_BENCH_QUICK='1'), stdout=log, stderr=subprocess.STDOUT, timeout=100)
    if (result.returncode and mode != 'reject') or not report.exists():
        raise RuntimeError(f'{mode}: process/report failed; see {root/(mode+".log")}')
    data = json.loads(report.read_text())
    if mode == 'reject':
        assert not data['complete'] and 'capacity' in data['error'], data
        continue
    assert data['complete'] and not data['error'], data
    if mode != 'off':
        expected = 'cpu' if mode in ('cpu','capacity') else expected_auto
        if mode == 'capacity':
            assert 'capacity' in data['dynamicGiFallback'], data
        assert data['dynamicGiBackend'] in ('cpu','gpu'), data
        if expected != 'any':
            assert data['dynamicGiBackend'] == expected, data
        assert data['dynamicGiConvergenceMs'] > 0 and data['dynamicGiTriangles'] == 4, data
    raw = capture.read_bytes().split(b'\n',3)
    assert raw[:3] == [b'P6',b'80 80',b'255'] and len(raw[3]) == 80*80*3
    values[mode] = [sum(raw[3][(y*80+x)*3+c] for y in range(38,42) for x in range(38,42))/16 for c in range(3)]
assert max(values['off']) < 2, values
assert min(values['cpu']) > 8, values
for mode in ('auto','capacity'):
    assert all(abs(a-b) < 4 for a,b in zip(values['cpu'], values[mode])), values
print('Native GI host PASS:',values)
