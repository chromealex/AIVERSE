"""Bake explicit tile sprites into a portable atlas and fixed tile layer."""
import math
import tempfile
from pathlib import Path
from .common import encoded, item, safe
from .presentation import images


def bake(model,writer,scene,width,height,ox,oz,scale):
    tiles=scene.get('tiles',[])
    if not tiles: return None
    unique={}; pictures=[]; fixed=[]
    for tile in tiles:
        key=encoded(tile['sprite'])
        if key not in unique:
            spec=tile['sprite']; picture,note=images.load_any(str(safe(Path(model['sourceRoot']),spec['file'])))
            if picture is None:
                item(model,scene['source'],'missing','Cannot read tile texture: '+str(note),key='tile-image'+spec['file']); continue
            rect=spec.get('rect')
            if rect:
                x,y,w,h=(int(rect[k]) for k in ('x','y','width','height'))
                if x<0 or y<0 or w<=0 or h<=0 or x+w>picture.w or y+h>picture.h: raise ValueError('tile rectangle outside texture')
                picture=images.crop(picture,x,picture.h-y-h,w,h)
            unique[key]=len(pictures)+1; pictures.append(picture)
        x,y=round(tile['x']*scale-ox),round(tile['y']*scale-oz)
        if 0<=x<width and 0<=y<height: fixed.append([x,y,unique[key]])
    if not pictures:return None
    tw,th=pictures[0].w,pictures[0].h
    if any((p.w,p.h)!=(tw,th) for p in pictures):
        item(model,scene['source'],'missing','Tile atlas contains unequal tile sizes; layer not emitted.',key='tile-size');return None
    columns=min(16,len(pictures)+1); rows=math.ceil((len(pictures)+1)/columns)
    if tw*columns>8192 or th*rows>8192: raise ValueError('tile atlas exceeds 8192 pixels')
    atlas=images.Image(tw*columns,th*rows,bytes(tw*columns*th*rows*4))
    for index,pic in enumerate(pictures,1):
        ax,ay=(index%columns)*tw,(index//columns)*th
        for y in range(th):
            start=((ay+y)*atlas.w+ax)*4
            atlas.rgba[start:start+tw*4]=pic.rgba[y*tw*4:(y+1)*tw*4]
    rel='tilesets/'+scene['id']
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'atlas.png';images.write_png(str(p),atlas);writer.write(rel+'.png',p.read_bytes())
    writer.json(rel+'.json',dict(name=scene['name'],atlas=rel+'.png',tileSize=[tw,th],columns=columns,filter='nearest',terrains=[dict(id='imported',char='.',cell='.',tiles=[0])]))
    return dict(tileset=rel+'.json',rows=['.'*width for _ in range(height)],fixed=fixed)
