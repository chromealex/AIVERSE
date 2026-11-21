"""Load a real glTF through renderbench, exercise Luma ownership and compare pixels."""
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
values = {}
for mode in ('off','on','moving','conflict'):
    recipe = dict(base)
    if mode != 'off':
        recipe['luma'] = {'backend':'auto','quality':'high'}
        recipe['pointLights'] = [dict(position=[0,0,1],color=[1,.5,.25],radius=2,intensity=4/128) for _ in range(128)]
    if mode == 'conflict':
        recipe['staticPointLightGrid'] = True
    scene = root/(mode+'.json')
    scene.write_text(json.dumps(recipe))
    report, capture = root/(mode+'-report.json'), root/(mode+'.ppm')
    report.unlink(missing_ok=True);capture.unlink(missing_ok=True)
    args=[binary,'--scene',str(scene),'--profile','hdr','--camera','0','--warmup','3','--samples','5',
          '--output',str(report),'--capture',str(capture)]
    if mode=='moving':args += ['--motion-yaw','0.001']
    with (root/(mode+'.log')).open('w') as log:
        result=subprocess.run(args,env=dict(os.environ,AIVERSE_BENCH_QUICK='1'),stdout=log,stderr=subprocess.STDOUT,timeout=90)
    assert report.exists(), mode
    data=json.loads(report.read_text())
    if mode=='conflict':
        assert not data['complete'] and 'ownership' in data['error'],data
        continue
    assert not result.returncode and data['complete'] and not data['error'],data
    if mode!='off':
        assert data['lumaBackend']==expected_auto,data
        if expected_auto=='gpu':assert data['lumaComposedFrames']>=8 and data['lumaSources']==128,data
        else:assert data['lumaFallback'] and data['lumaComposedFrames']==0,data
    raw=capture.read_bytes().split(b'\n',3)
    assert raw[:3]==[b'P6',b'80 80',b'255'] and len(raw[3])==80*80*3
    values[mode]=[sum(raw[3][(y*80+x)*3+c] for y in range(38,42) for x in range(38,42))/16 for c in range(3)]
assert max(values['off'])<2,values
assert min(values['on'])>10,values
assert all(abs(a-b)<5 for a,b in zip(values['on'],values['moving'])),values
print('Native Luma host PASS:',values)
