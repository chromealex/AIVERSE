#!/usr/bin/env python3
"""Bake authored vegetation PBR data. Requires numpy/Pillow. No measured scan claim."""
from pathlib import Path
import sys,json,argparse
import numpy as np
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'terrain'))
from bake_pbr_pack import tile,blur_periodic,png
ROOT=Path(__file__).resolve().parents[2]
SOURCE=Path(__file__).resolve().parent/'pbr_sources'
OUT=ROOT/'engine/assets/materials/vegetation'
N=1024

def bake(names=None):
    out=OUT/'textures';out.mkdir(parents=True,exist_ok=True)
    materials=json.loads((OUT/'materials.json').read_text()) if names and (OUT/'materials.json').exists() else {}
    entries=json.loads((OUT/'catalog.json').read_text())['materials'] if names and (OUT/'catalog.json').exists() else []
    selected=names or ('oak_bark','pine_bark','birch_bark','spruce_bark','oak_leaves','shrub_leaves','spruce_leaves','pine_leaves','birch_leaves','maple_leaves','apple_leaves','willow_leaves')
    entries=[entry for entry in entries if entry['id'] not in selected]
    for name in selected:
        if not name.replace('_','').isalnum() or not (SOURCE/(name+'_basecolor.png')).is_file():raise ValueError('unknown material: '+name)
        leaf=name.endswith('_leaves')
        a=np.asarray(Image.open(SOURCE/(name+'_basecolor.png')).convert('RGBA').resize((N,N),Image.Resampling.LANCZOS),dtype=float)/255
        rgb=a[:,:,:3];alpha=a[:,:,3] if leaf else np.ones((N,N))
        if leaf:
            # A small vein relief is authored from the albedo detail, not a scan.
            lum=rgb@np.array([.2126,.7152,.0722]);h=.5+(lum-blur_periodic(lum,3))*.2
            h=np.where(alpha>.5,h,.5);meters=.8;height_m=.0006
            rough=np.clip(.68+(lum-.3)*.22,.57,.82);ao=np.ones((N,N))
            # Optional data for a future thin-sheet shader; standard PBR does not consume it.
            trans=np.clip((rgb[:,:,1]-rgb[:,:,0])*.8+.35,0,.65)*alpha
        else:
            rgb=tile(rgb);h=tile(np.asarray(Image.open(SOURCE/(name+'_height.png')).convert('L').resize((N,N),Image.Resampling.LANCZOS),dtype=float)/255)
            meters=1.5;height_m={'birch_bark':.004,'spruce_bark':.015,'pine_bark':.028}.get(name,.035);cavity=np.maximum(blur_periodic(h,7)-h,0)
            rough=np.clip(.9+(h-.5)*.08,.78,.97);ao=np.clip(1-cavity,.78,1)
        dx=(np.roll(h,-1,1)-np.roll(h,1,1))*height_m*N/(2*meters)
        dy=(np.roll(h,-1,0)-np.roll(h,1,0))*height_m*N/(2*meters)
        normal=np.stack([-dx,dy,np.ones_like(h)],2);normal/=np.linalg.norm(normal,axis=2,keepdims=True);normal=normal*.5+.5
        for suffix,value in [('basecolor',np.dstack([rgb,alpha]) if leaf else rgb),('normal',normal),('roughness',rough),('ao',ao),('metallic',np.zeros_like(h)),('orm',np.stack([ao,rough,np.zeros_like(h)],2))]:png(out/(name+'_'+suffix+'.png'),value)
        Image.fromarray(np.rint(h*65535).astype('uint16')).save(out/(name+'_height.png'))
        if leaf:
            png(out/(name+'_opacity.png'),alpha);png(out/(name+'_translucency.png'),trans)
        prefix='materials/vegetation/textures/'+name
        materials[name]={'description':'Authored PBR vegetation: '+name,'texture':prefix+'_basecolor.png','normalMap':prefix+'_normal.png','metallicRoughnessMap':prefix+'_orm.png','occlusionMap':prefix+'_ao.png','color':[1,1,1,1],'metallic':0,'roughness':1,'normalScale':1,'vertexColors':False,'team':'none','doubleSided':leaf,'alpha':'mask' if leaf else 'opaque','cutoff':.5}
        if leaf: materials[name]['thinTransmission']=.35
        entries.append({'id':name,'layout':'2x2 sprig atlas' if leaf else 'repeat','normalConvention':'+Y','repeatMeters':meters,'heightMeters':height_m,'optionalTranslucencyMap':prefix+'_translucency.png' if leaf else None})
    (OUT/'materials.json').write_text(json.dumps(materials,indent=2)+'\n')
    (OUT/'catalog.json').write_text(json.dumps({'version':1,'basecolorSpace':'sRGB','dataSpace':'linear','materials':entries},indent=2)+'\n')
    print('Baked',len(entries),'PBR vegetation materials /',sum(9 if m['layout']=='2x2 sprig atlas' else 7 for m in entries),'texture maps')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--only',nargs='+',help='bake these materials and preserve existing pack entries')
    bake(parser.parse_args().only)
