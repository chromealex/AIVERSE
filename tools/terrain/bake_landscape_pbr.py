#!/usr/bin/env python3
"""Bake continuous PBR layer transitions on native Terra surface exports.
No terrain generation: heights come exclusively from the native ground snapshot.
Requires NumPy/Pillow. Bound memory by processing texture strips.
"""
from pathlib import Path
import argparse,json
import numpy as np
from PIL import Image

def smooth(a,b,x):
 t=np.clip((x-a)/(b-a),0,1);return t*t*(3-2*t)
def sample(a,u,v):
 h,w=a.shape[:2];x=(u*w-.5)%w;y=(v*h-.5)%h
 ix=np.floor(x).astype(int);iy=np.floor(y).astype(int);fx=(x-ix)[...,None];fy=(y-iy)[...,None]
 return ((a[iy,ix]*(1-fx)+a[iy,(ix+1)%w]*fx)*(1-fy)+(a[(iy+1)%h,ix]*(1-fx)+a[(iy+1)%h,(ix+1)%w]*fx)*fy)
def world_noise(x,z,period=24.0):
 # Cosmetic world-space lattice, not a height generator; continuous at tiles.
 u,v=x/period,z/period;ix=np.floor(u);iz=np.floor(v)
 fx=u-ix;fz=v-iz;fx=fx*fx*(3-2*fx);fz=fz*fz*(3-2*fz)
 def hashed(a,b):
  n=a.astype(np.int64)*374761393+b.astype(np.int64)*668265263
  n=(n^(n>>13))*1274126177
  return ((n^(n>>16))&0xffff)/65535.0
 return (hashed(ix,iz)*(1-fx)+hashed(ix+1,iz)*fx)*(1-fz)+(hashed(ix,iz+1)*(1-fx)+hashed(ix+1,iz+1)*fx)*fz

def snow_settings(recipe):
 """Opt-in cosmetic snow cover; never changes native soil or climate authority."""
 values=dict(lowMetres=100.,highMetres=155.,minNormalY=.52,fullNormalY=.88,variationMetres=8.)
 authored=recipe.get('surfaceSnow',{})
 if not isinstance(authored,dict):raise ValueError('surfaceSnow must be an object')
 for key,value in authored.items():
  if key not in values:raise ValueError('unknown surfaceSnow field: '+key)
  if isinstance(value,bool) or not isinstance(value,(int,float)) or not np.isfinite(value):raise ValueError('finite surfaceSnow number required: '+key)
  values[key]=float(value)
 if not -10000<=values['lowMetres']<values['highMetres']<=10000:raise ValueError('ordered surfaceSnow altitude required')
 if not 0<=values['minNormalY']<values['fullNormalY']<=1:raise ValueError('ordered surfaceSnow normal-Y required')
 if not 0<=values['variationMetres']<=1000:raise ValueError('surfaceSnow variation 0..1000m')
 return values

def snow_weight(height,normal_y,variation,settings):
 return smooth(settings['lowMetres'],settings['highMetres'],height+variation*settings['variationMetres'])*smooth(settings['minNormalY'],settings['fullNormalY'],normal_y)

def bake(root,materials,size,part=0,parts=1,detail=False):
 if size<256 or size>4096:raise ValueError('size 256..4096')
 snow=snow_settings(json.loads((root/'recipe.json').read_text()) if (root/'recipe.json').exists() else {})
 ground=json.loads((root/'ground.json').read_text());cat=json.loads((root/'catalog.json').read_text())
 h=np.array(ground['heightCm'],dtype=np.float32).reshape(ground['rows'],ground['columns'])*.01
 dz,dx=np.gradient(h,ground['stepCm']*.01)
 native_shore=ground.get('grassMaskSource')=='terra-nereid-shore-v1'
 fields=[h,dx,dz]
 if native_shore:
  for key in ['sandPermille','wetPermille']:
   field=np.asarray(ground[key],dtype=np.float32)
   if field.size!=h.size or not np.isfinite(field).all() or field.min()<0 or field.max()>1000:raise ValueError('invalid native shore mask: '+key)
   fields.append(field.reshape(h.shape)*.001)
 grid=np.stack(fields,2)
 maps={}
 names=['grass','sand','rock','gravel_soil','soil','snow']
 for n in names:
  maps[n]={k:np.asarray(Image.open(materials/f'{n}_{k}_repeat.png').convert('RGB'),dtype=np.float32)/255 for k in ['basecolor','normal','orm']}
 for index,tile in enumerate(cat['terrain']):
  if index%parts!=part:continue
  result={k:np.empty((size,size,3),np.uint8) for k in ['basecolor','normal','orm']}
  detail_weights=np.empty((size,size,4),np.uint8) if detail else None
  for row in range(0,size,64):
   x=tile['xCm']*.01+(np.arange(size)+.5)*64/size
   z=tile['zCm']*.01+(np.arange(row,min(row+64,size))+.5)*64/size
   X,Z=np.meshgrid(x,z)
   # Interpolate the same native surface in centimetres, without wrapped bounds.
   gx=np.clip((X-ground['originXcm']*.01)/(ground['stepCm']*.01),0,h.shape[1]-1.001)
   gz=np.clip((Z-ground['originZcm']*.01)/(ground['stepCm']*.01),0,h.shape[0]-1.001)
   ix=gx.astype(int);iz=gz.astype(int);fx=(gx-ix)[...,None];fz=(gz-iz)[...,None]
   q=(grid[iz,ix]*(1-fx)+grid[iz,ix+1]*fx)*(1-fz)+(grid[iz+1,ix]*(1-fx)+grid[iz+1,ix+1]*fx)*fz
   H=q[...,0];ny=1/np.sqrt(1+q[...,1]**2+q[...,2]**2)
   variation=world_noise(X,Z)*2-1
   if native_shore:
    # One native mask owns both beach paint and grass exclusion. No infinite
    # water-plane beach appears on low ground far outside a finite river.
    shore=np.clip(q[...,3],0,1);wet=np.clip(q[...,4],0,1)
   else:
    surfaces=cat.get('nereidWater',{}).get('surfaces',[])
    water=surfaces[0] if surfaces else None
    if water:
     grade=water.get('grade',{});origin=grade.get('origin',[0,0]);rise=grade.get('risePerMetre',[0,0])
     if 'slopeDegrees' in grade:
      heading=np.deg2rad(grade.get('downhillHeadingDegrees',0));slope=np.tan(np.deg2rad(grade['slopeDegrees']));rise=[-np.sin(heading)*slope,-np.cos(heading)*slope]
     surface=water.get('surfaceY',0)+(X-origin[0])*rise[0]+(Z-origin[1])*rise[1]
     shore=1-smooth(.35,2.5,H-surface+variation*.20);wet=1-smooth(.05,.45,H-surface)
    else:
     shore=np.zeros_like(H);wet=np.zeros_like(H)
   stone=np.maximum(1-smooth(.55,.91,ny),smooth(18,48,H))
   scree=smooth(.62,.9,ny)*(.3+.20*variation)
   snow_cover=snow_weight(H,ny,variation,snow)
   weights={'grass':(1-shore)*(1-stone),'sand':shore*(1-stone),'rock':stone*(1-scree)*(1-snow_cover),'gravel_soil':stone*scree*(1-snow_cover),'soil':np.zeros_like(H),'snow':stone*snow_cover}
   total=sum(weights.values());weights={k:v/np.maximum(total,1e-6) for k,v in weights.items()}
   if detail:
    # Snow is the implicit fourth weight. Alpha is a grayscale noise underlay.
    data=np.stack([weights['grass']+weights['soil'],weights['sand'],weights['rock']+weights['gravel_soil'],world_noise(X,Z,9.0)],2)
    detail_weights[row:row+H.shape[0]]=np.rint(np.clip(data,0,1)*255).astype('uint8')
   # Rock maps are projected in world space on all three axes to avoid stretch.
   axes=np.stack([np.abs(q[...,1]),np.ones_like(H),np.abs(q[...,2])],2)**4;axes/=axes.sum(2,keepdims=True)
   out={k:np.zeros((*H.shape,3),np.float32) for k in result}
   for n,w in weights.items():
    if w.max()<1e-6:continue
    scale=3 if n in ['grass','sand'] else 7
    for k in result:
     a=maps[n][k];value=sample(a,X/scale,Z/scale)
     if n in ['rock','gravel_soil']:
      value=value*axes[...,1,None]+sample(a,Z/scale,H/scale)*axes[...,0,None]+sample(a,X/scale,H/scale)*axes[...,2,None]
     if k=='basecolor':
      if n=='rock':value=value*.68
      value=value**2.2
     if k=='normal':value=value*2-1
     out[k]+=value*w[...,None]
   out['basecolor']=np.maximum(out['basecolor'],0)**(1/2.2)
   out['basecolor']*=1-.35*wet[...,None]
   out['orm'][...,1]*=1-.25*wet
   out['normal']/=np.maximum(np.linalg.norm(out['normal'],axis=2,keepdims=True),1e-6);out['normal']=out['normal']*.5+.5
   for k in result:result[k][row:row+H.shape[0]]=np.rint(np.clip(out[k],0,1)*255).astype('uint8')
  stem=Path(tile['file']).stem
  for k,a in result.items():Image.fromarray(a[::-1]).save(root/'textures'/f'{stem}_{k}.png')
  Image.fromarray(result['orm'][::-1,:,0]).save(root/'textures'/f'{stem}_ao.png')
  if detail:Image.fromarray(detail_weights[::-1]).save(root/'textures'/f'{stem}_weights.png')
  print(stem,flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--materials',type=Path,default=Path(__file__).resolve().parents[2]/'engine/assets/materials/terrain/textures');p.add_argument('--size',type=int,default=2048);p.add_argument('--part',type=int,default=0);p.add_argument('--parts',type=int,default=1);p.add_argument('--detail',action='store_true');a=p.parse_args();
 if a.parts<1 or not 0<=a.part<a.parts:raise ValueError('invalid partition')
 bake(a.root,a.materials,a.size,a.part,a.parts,a.detail)
