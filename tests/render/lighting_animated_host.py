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
# A separate animated caster crosses the ray from receiver to (2,0,2).
import copy
caster=copy.deepcopy(gltf)
cp=[(-.5,-2,0),(.5,-2,0),(.5,2,0),(-.5,2,0)]
blob=b''.join(struct.pack('<3f',*v) for v in cp+[(0,0,1)]*4)
blob+=struct.pack('<6H',0,1,2,0,2,3)+struct.pack('<2f',0,1)+struct.pack('<6f',1,0,1,4,0,1)
caster['nodes']=[{'name':'moving-caster','mesh':0,'translation':[1,0,1]}]
caster['buffers']=[{'uri':'data:application/octet-stream;base64,'+base64.b64encode(blob).decode(),'byteLength':len(blob)}]
caster['bufferViews']=[{'buffer':0,'byteOffset':0,'byteLength':48},{'buffer':0,'byteOffset':48,'byteLength':48},
    {'buffer':0,'byteOffset':96,'byteLength':12},{'buffer':0,'byteOffset':108,'byteLength':8},{'buffer':0,'byteOffset':116,'byteLength':24}]
caster['accessors'][0].update(count=4,min=[-.5,-2,0],max=[.5,2,0])
caster['accessors'][1]['count']=4;caster['accessors'][2]['count']=6
caster['accessors'] += [{'bufferView':3,'componentType':5126,'count':2,'type':'SCALAR','min':[0],'max':[1]},
                        {'bufferView':4,'componentType':5126,'count':2,'type':'VEC3'}]
caster['animations']=[{'samplers':[{'input':3,'output':4,'interpolation':'LINEAR'}],
                       'channels':[{'sampler':0,'target':{'node':0,'path':'translation'}}]}]
(root/'caster.gltf').write_text(json.dumps(caster))
base['postfx']={'hdr':'on','effects':[]}
base['models']=[{'path':'caster.gltf','animation':'caster.gltf'}]
base['pointLights']=[dict(position=[2,0,2],color=[1,.5,.25],radius=5,intensity=4)]
values={}
for mode in ('blocked','open','cpu','auto'):
    recipe=copy.deepcopy(base)
    if mode in ('blocked','open'):
        recipe['luma']={'backend':'gpu','quality':'high'}
        if expected_auto!='gpu':continue
    else:
        recipe['dynamicGI']={'backend':mode,'origin':[-2,-2,-1],'dimensions':[4,4,4],
            'spacing':1,'samples':32,'distance':8,'cpuBudgetMs':100}
    scene=root/(mode+'.json');scene.write_text(json.dumps(recipe))
    report,capture=root/(mode+'-report.json'),root/(mode+'.ppm')
    report.unlink(missing_ok=True);capture.unlink(missing_ok=True)
    args=[binary,'--scene',str(scene),'--profile','hdr','--camera','0','--warmup','3','--samples','3',
          '--output',str(report),'--capture',str(capture)]
    if mode in ('blocked','open'):args+=['--freeze-time','0' if mode=='blocked' else '.5']
    with (root/(mode+'.log')).open('w') as log:
        result=subprocess.run(args,env=dict(os.environ,AIVERSE_BENCH_QUICK='1'),stdout=log,stderr=subprocess.STDOUT,timeout=100)
    assert report.exists(),mode
    data=json.loads(report.read_text());assert not result.returncode and data['complete'] and not data['error'],data
    assert data['animatedLightingCasters'] and data['lightingSnapshotTriangles']==6,data
    assert data['lightingGeometryRevision'] >= (2 if mode in ('blocked','open') else 7),data
    if mode in ('cpu','auto') and expected_auto!='any':assert data['dynamicGiBackend']==('cpu' if mode=='cpu' else expected_auto),data
    raw=capture.read_bytes().split(b'\n',3)
    values[mode]=[sum(raw[3][(y*80+x)*3+c] for y in range(38,42) for x in range(38,42))/16 for c in range(3)]
if expected_auto=='gpu':
    # Low-resolution spatial/temporal filtering softens this narrow shadow.
    # Require >90% suppression; the broad-wall GPU fixture checks exact black.
    assert all(a < .1*b for a,b in zip(values['blocked'],values['open'])),values
    assert min(values['open'])>10,values
print('Animated GI/Luma host PASS:',values)
