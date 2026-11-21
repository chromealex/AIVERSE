#!/usr/bin/env python3
"""Contract checks for shipped PBR maps and project installer collision safety."""
from pathlib import Path
import json,sys,tempfile,subprocess
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];pack=ROOT/'engine/assets/materials/terrain'
catalog=json.loads((pack/'catalog.json').read_text());library=json.loads((pack/'materials.json').read_text())
assert catalog['default']=='checkerboard' and {m['id'] for m in catalog['materials']}=={'grass','soil','sand','rock','snow','forest_floor','wet_soil','gravel_soil','snow_crust','checkerboard'}
for item in catalog['materials']:
 name=item['id'];prefix=pack/'textures'/name
 maps={k:np.asarray(Image.open(str(prefix)+'_'+k+'_repeat.png')) for k in ('basecolor','normal','roughness','metallic','ao','orm','height','terrain_color','terrain_normal')}
 assert all(a.shape[:2]==(1024,1024) for a in maps.values()),name
 assert np.max(maps['metallic'])==0 and np.max(maps['orm'][...,2])==0,name
 assert np.array_equal(maps['orm'][...,0],maps['ao']) and np.array_equal(maps['orm'][...,1],maps['roughness']),name
 assert np.array_equal(maps['terrain_color'][...,:3],maps['basecolor']) and np.array_equal(maps['terrain_color'][...,3],maps['ao']),name
 assert np.array_equal(maps['terrain_normal'][...,:3],maps['normal']) and np.array_equal(maps['terrain_normal'][...,3],maps['roughness']),name
 n=maps['normal'].astype(float)/255*2-1
 assert np.max(abs(np.linalg.norm(n,axis=2)-1))<.012 and n[...,2].min()>0,name
 assert maps['height'].dtype in (np.dtype('uint16'),np.dtype('int32'))
 assert maps['roughness'].min()>=127 and maps['ao'].min()>=183,name
 if name!='checkerboard':
  for kind in ('basecolor','height'):
   a=maps[kind].astype(float)
   # Repeat joins adjacent texel centres, not duplicate endpoints. A forced
   # equal row creates a narrow band; wrap gradients must match interior detail.
   for axis in (0,1):
    step=np.abs(np.diff(a,axis=axis)).mean()
    wrap=np.abs(np.take(a,0,axis=axis)-np.take(a,-1,axis=axis)).mean()
    assert wrap<=step*1.6+1,(name,kind,axis,wrap,step)
 m=library['terrain_'+name];assert m['metallic']==0 and not m['vertexColors']
 for k in ('texture','normalMap','metallicRoughnessMap','occlusionMap'):assert (ROOT/'engine/assets'/m[k]).is_file(),m[k]
# Distant repeats must not expose broad directional source illumination.
g=np.asarray(Image.open(pack/'textures/grass_basecolor_repeat.png')).mean(2)/255
fy=np.fft.fftfreq(1024)[:,None];fx=np.fft.fftfreq(1024)[None,:]
low=np.fft.ifft2(np.fft.fft2(g)*np.exp(-2*np.pi**2*64**2*(fx*fx+fy*fy))).real
assert low.std()<.001, 'grass low-frequency contrast repeats as visible bands'
print('PASS:',len(catalog['materials'])*9,'maps, dimensions, dielectric channels, normal lengths, packed parity, tile boundaries, material paths')
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'game.json').write_text('{"name":"fixture","terrain":{"existing":true}}');(p/'materials.json').write_text('{"custom":{"roughness":0.5}}')
 cmd=[sys.executable,str(ROOT/'tools/terrain/install_pbr_pack.py'),d]
 subprocess.run(cmd,check=True,capture_output=True)
 assert json.loads((p/'game.json').read_text())['terrain']=={'existing':True}
 assert 'custom' in json.loads((p/'materials.json').read_text())
 assert subprocess.run(cmd,capture_output=True).returncode!=0
print('PASS: installer preserves existing terrain/custom material; refuses repeated overwrite')
