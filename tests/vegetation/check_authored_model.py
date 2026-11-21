#!/usr/bin/env python3
"""Audit an authored GLB's actual geometry and referenced PBR maps."""
import argparse,json,struct
from pathlib import Path
import numpy as np
from PIL import Image
ap=argparse.ArgumentParser();ap.add_argument('model',type=Path);ap.add_argument('--max-triangles',type=int,required=True);args=ap.parse_args()
b=args.model.read_bytes();size=struct.unpack_from('<I',b,12)[0];j=json.loads(b[20:20+size]);data=b[28+size:]
def array(i):
 a=j['accessors'][i];v=j['bufferViews'][a['bufferView']];n={'VEC3':3,'VEC2':2,'SCALAR':1}[a['type']]
 return np.frombuffer(data,dtype='<f4' if a['componentType']==5126 else '<u4',count=a['count']*n,offset=v['byteOffset']).reshape(-1,n)
total=0
for m in j['meshes']:
 for p in m['primitives']:
  pos=array(p['attributes']['POSITION']);normal=array(p['attributes']['NORMAL']);uv=array(p['attributes']['TEXCOORD_0']);ix=array(p['indices']).reshape(-1,3)
  assert np.isfinite(pos).all() and np.isfinite(uv).all() and ix.max()<len(pos),'invalid geometry'
  assert np.max(abs(np.linalg.norm(normal,axis=1)-1))<1e-4,'non-unit vertex normals'
  assert np.all(np.linalg.norm(np.cross(pos[ix[:,1]]-pos[ix[:,0]],pos[ix[:,2]]-pos[ix[:,0]]),axis=1)>1e-10),'degenerate triangles'
  total+=len(ix)
assert total<=args.max_triangles,(total,args.max_triangles)
for im in j['images']:assert (args.model.parent/im['uri']).is_file(),im
for material in j['materials']:
 assert material['pbrMetallicRoughness']['metallicFactor']==0
 for key,container in [('normalTexture',material),('metallicRoughnessTexture',material['pbrMetallicRoughness'])]:
  path=args.model.parent/j['images'][j['textures'][container[key]['index']]['source']]['uri'];im=np.asarray(Image.open(path));assert im.ndim==3
  if key=='normalTexture':assert np.max(abs(np.linalg.norm(im[:,:,:3]/255*2-1,axis=2)-1))<.015,'non-unit texture normal'
  else:assert im[:,:,2].max()==0,'nonzero vegetation metalness'
print('PASS:',args.model.name,total,'triangles; finite geometry, unit normals, nondegenerate faces, resolved PBR textures')
