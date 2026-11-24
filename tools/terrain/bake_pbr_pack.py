#!/usr/bin/env python3
"""Bake the bundled terrain PBR library from authored base-color/height sources.
No inferred metallic surfaces: the entire terrain pack is dielectric.
Requires numpy and Pillow. Sources are authored images, not measured scans.
"""
from pathlib import Path
import json, argparse
import numpy as np
from PIL import Image

MATERIALS={'grass':(2.0,.004,.91),'soil':(2.0,.035,.94),'sand':(2.0,.012,.86),'rock':(2.0,.065,.78),'snow':(2.0,.008,.76),'forest_floor':(2.0,.025,.91),'wet_soil':(2.0,.022,.59),'gravel_soil':(2.0,.045,.85),'snow_crust':(2.0,.006,.66),'river_sand':(4.0,.002,.87),'pebble_sand':(4.0,.012,.82),'granite_rock':(4.0,.04,.83),'schist_rock':(4.0,.035,.90),'checkerboard':(4.0,0,.68)}
SIZE=1024

def tile(a):
    # Periodic-plus-smooth decomposition removes broad boundary discontinuities.
    h,w=a.shape[:2]; v=np.zeros_like(a)
    v[0]=a[-1]-a[0];v[-1]=a[0]-a[-1]
    v[:,0]+=a[:,-1]-a[:,0];v[:,-1]+=a[:,0]-a[:,-1]
    den=2*np.cos(2*np.pi*np.arange(h)/h)[:,None]+2*np.cos(2*np.pi*np.arange(w)/w)[None,:]-4
    den[0,0]=1
    if a.ndim==3:den=den[...,None]
    f=np.fft.fft2(v,axes=(0,1))/den;f[0,0]=0
    p=a-np.fft.ifft2(f,axes=(0,1)).real
    # A single matched endpoint row leaves a visible narrow seam in mips.
    # Blend overlapping half-period copies with zero weight at each copy's
    # boundary. Both value and slope cross the wrap smoothly over a broad area.
    for axis in (0,1):
        n=p.shape[axis]
        weight=np.sin(np.pi*(np.arange(n)+.5)/n)**2
        shape=[1]*p.ndim;shape[axis]=n;weight=weight.reshape(shape)
        p=p*weight+np.roll(p,n//2,axis=axis)*(1-weight)
    return np.clip(p,0,1)

def blur_periodic(a,r):
    freqy=np.fft.fftfreq(a.shape[0])[:,None];freqx=np.fft.fftfreq(a.shape[1])[None,:]
    return np.fft.ifft2(np.fft.fft2(a)*np.exp(-2*np.pi**2*r*r*(freqx*freqx+freqy*freqy))).real

def png(path,a):
    Image.fromarray(np.rint(np.clip(a,0,1)*255).astype('uint8')).save(path,optimize=True)

def catalog_package(pid):
    """Folder of an AIVERSE-PACKAGES catalog package in the shared cache (downloaded when missing)."""
    import sys as _sys
    _sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pkg'))
    from aiverse_pkg import package_dir
    return package_dir(pid)

def bake(root,sources=None):
    sources=Path(sources) if sources else catalog_package('terrain-pbr-sources')
    textures=root/'textures';textures.mkdir(parents=True,exist_ok=True);library={};catalog=[]
    for name,(meters,height_m,rough_mean) in MATERIALS.items():
        if name=='checkerboard':
            yy,xx=np.indices((SIZE,SIZE));c=((xx//128+yy//128)%2)*.23+.24
            base=np.repeat(c[...,None],3,axis=2);height=np.full((SIZE,SIZE),.5)
        else:
            base=np.asarray(Image.open(sources/f'{name}_basecolor.png').convert('RGB').resize((SIZE,SIZE),Image.Resampling.LANCZOS),dtype=float)/255
            height=np.asarray(Image.open(sources/f'{name}_height.png').convert('L').resize((SIZE,SIZE),Image.Resampling.LANCZOS),dtype=float)/255
            base=tile(base);height=tile(height)
            if name=='schist_rock':
                # A broad plate crossed the first overlap cut. A second
                # coherent quarter-period overlap removes its repeated edge
                # in BOTH albedo and height before normal/AO derivation.
                base=tile(np.roll(base,(SIZE//4,SIZE//4),axis=(0,1)))
                height=tile(np.roll(height,(SIZE//4,SIZE//4),axis=(0,1)))
            if name=='grass':
                # Remove source-wide illumination/colour drift before repetition.
                # The blade detail stays; broad dark/light bands must not become
                # a visible 2m lattice when distant mips average that detail.
                low=np.stack([blur_periodic(base[...,c],32) for c in range(3)],axis=2)
                base=tile(np.clip(base-low+base.mean(axis=(0,1)),0,1))
        dx=(np.roll(height,-1,axis=1)-np.roll(height,1,axis=1))*height_m*SIZE/(2*meters)
        dy=(np.roll(height,-1,axis=0)-np.roll(height,1,axis=0))*height_m*SIZE/(2*meters)
        normal=np.stack([-dx,dy,np.ones_like(dx)],axis=2);normal/=np.linalg.norm(normal,axis=2,keepdims=True);normal=normal*.5+.5
        cavity=np.maximum(blur_periodic(height,7)-height,0)
        ao=np.clip(1-cavity*1.2,.72,1)
        rough=np.clip(rough_mean+(blur_periodic(height,2)-.5)*.1+cavity*.12,.5,.98)
        if name=='checkerboard':ao[:]=1;rough[:]=rough_mean
        prefix='materials/terrain/textures/'+name
        for suffix,arr in [('basecolor',base),('normal',normal),('roughness',rough),('ao',ao),('metallic',np.zeros_like(ao)),('orm',np.stack([ao,rough,np.zeros_like(ao)],2)),('terrain_color',np.dstack([base,ao])),('terrain_normal',np.dstack([normal,rough]))]:
            png(textures/f'{name}_{suffix}_repeat.png',arr)
        Image.fromarray(np.rint(height*65535).astype('uint16')).save(textures/f'{name}_height_repeat.png')
        library['terrain_'+name]={'description':f'Bundled terrain PBR: {name}. Dielectric; {meters:g} m repeat.','texture':prefix+'_basecolor_repeat.png','normalMap':prefix+'_normal_repeat.png','metallicRoughnessMap':prefix+'_orm_repeat.png','occlusionMap':prefix+'_ao_repeat.png','color':[1,1,1,1],'roughness':1,'metallic':0,'normalScale':1,'occlusionStrength':1,'vertexColors':False,'team':'none','uvScale':[1,1]}
        catalog.append({'id':name,'repeatMeters':meters,'heightMeters':height_m,'heightMap':prefix+'_height_repeat.png','terrainLayer':{'id':name,'texture':prefix+'_terrain_color_repeat.png','normal':prefix+'_terrain_normal_repeat.png','scale':meters,'tint':[1,1,1]}})
    (root/'materials.json').write_text(json.dumps(library,indent=2)+'\n')
    (root/'catalog.json').write_text(json.dumps({'version':1,'default':'checkerboard','normalConvention':'+Y (OpenGL)','colorSpace':{'basecolor':'sRGB','normal':'linear','orm':'linear','height':'linear'},'materials':catalog},indent=2)+'\n')
    checker=catalog[-1]['terrainLayer']
    config={'pbr':{'enabled':True,'packedMaps':True,'roughness':.68},'layers':[checker],'cells':{k:{'layer':'checkerboard','color':[1,1,1]} for k in ('grass','sand','ramp','rock','blocked','lakebed','shallows','dug')},'splatStrength':1}
    (root/'default-terrain.json').write_text(json.dumps(config,indent=2)+'\n')
    print(f'Baked {len(MATERIALS)} PBR materials / {len(MATERIALS)*9} texture maps')
if __name__=='__main__':
    ap=argparse.ArgumentParser(description='Bake the terrain-pbr package from terrain-pbr-sources.')
    ap.add_argument('--root',type=Path,required=True,help='output: a working copy of the terrain-pbr package (catalog payload)')
    ap.add_argument('--sources',type=Path,help='default: terrain-pbr-sources from the package catalog')
    a=ap.parse_args();bake(a.root,a.sources)
