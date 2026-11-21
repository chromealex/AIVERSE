#!/usr/bin/env python3
from pathlib import Path
import json
import numpy as np
from PIL import Image
root=Path(__file__).resolve().parents[2];pack=root/'engine/assets/materials/vegetation'
cat=json.loads((pack/'catalog.json').read_text());library=json.loads((pack/'materials.json').read_text());count=0
assert {m['id'] for m in cat['materials']}=={'oak_bark','pine_bark','birch_bark','spruce_bark','oak_leaves','shrub_leaves','spruce_leaves','pine_leaves'}
for entry in cat['materials']:
 name=entry['id'];leaf=entry['layout']=='2x2 sprig atlas';keys=['basecolor','normal','roughness','ao','metallic','orm','height']+(['opacity','translucency'] if leaf else [])
 maps={key:np.asarray(Image.open(pack/'textures'/(name+'_'+key+'.png'))) for key in keys};count+=len(keys)
 assert all(a.shape[:2]==(1024,1024) for a in maps.values())
 assert maps['metallic'].max()==0 and maps['orm'][:,:,2].max()==0
 assert np.array_equal(maps['orm'][:,:,0],maps['ao']) and np.array_equal(maps['orm'][:,:,1],maps['roughness'])
 assert np.max(abs(np.linalg.norm(maps['normal']/255*2-1,axis=2)-1))<.015
 if leaf:
  assert np.array_equal(maps['basecolor'][:,:,3],maps['opacity'])
  coverage=np.mean(maps['opacity']>=128);assert .05<coverage<.85,(name,coverage)
  assert library[name]['alpha']=='mask' and library[name]['doubleSided']
 else:
  for key in ['basecolor','height']:
   a=maps[key];assert np.array_equal(a[0],a[-1]) and np.array_equal(a[:,0],a[:,-1])
 for key in ['texture','normalMap','metallicRoughnessMap','occlusionMap']:assert (root/'engine/assets'/library[name][key]).is_file()
print('PASS:',len(cat['materials']),'vegetation materials,',count,'maps; PBR packing, tile edges, normals, cutout coverage and paths')
