"""Asset presentation adapters reuse the existing image decoder and VFX importer."""
import sys
import tempfile
from pathlib import Path
from .common import item, safe
# VFX modules retain their standalone imports for compatibility with the existing CLI.
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'unity_vfx'))
import images


def convert_texture(model, writer, source):
    src=safe(Path(model['sourceRoot']),source)
    image,note=images.load_any(str(src))
    if image is None:
        item(model,source,'missing','Texture conversion unavailable: '+str(note),key='texture-format')
        return None
    dest='content/'+source+'.png'
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'image.png'; images.write_png(str(p),image);writer.write(dest,p.read_bytes())
    item(model,source,'approx','Texture converted to RGBA8 PNG; review color profile.',dest,key='texture-format')
    return dest


def sprite(model,writer,entity,scale):
    spec=entity.get('sprite')
    if not isinstance(spec,dict): return None
    src=safe(Path(model['sourceRoot']),spec['file'])
    image,note=images.load_any(str(src))
    if image is None:
        item(model,entity['source'],'missing','Cannot decode sprite: '+str(note),key='sprite-decode'); return None
    rect=spec.get('rect')
    if rect:
        x,y,w,h=(int(rect[k]) for k in ('x','y','width','height'))
        if x<0 or y<0 or w<=0 or h<=0 or x+w>image.w or y+h>image.h:
            item(model,entity['source'],'missing','Sprite rectangle outside texture.',key='sprite-decode'); return None
        image=images.crop(image,x,image.h-y-h,w,h)
    dest='sprites/'+entity['id']+'.png'
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp)/'sprite.png';images.write_png(str(p),image);writer.write(dest,p.read_bytes())
    ppu=spec.get('pixelsPerUnit',100)
    if not isinstance(ppu,(int,float)) or ppu<=0: ppu=100
    item(model,entity['source'],'approx','Sprite cropped; pivot, sorting and animation require review.',dest,key='sprite')
    return dict(sheet=dest,frame=[image.w,image.h],mode='ground',pixelsPerCell=ppu/scale,actions={'idle':[0,1]})
