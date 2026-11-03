"""Unity serialized Tilemap cells; keep unsupported tile transforms in the report."""
from pathlib import Path
from lib.port.common import item
from lib.unity.unity_yaml import get


def extract_tiles(component, assets, root, model, source, position):
    data=component.data; out=[]
    refs=data.get('m_TileAssetArray',[])
    for entry in data.get('m_Tiles',[]):
        pos=entry.get('first',{}); tile=entry.get('second',{})
        idx=tile.get('m_TileIndex',-1)
        if not isinstance(idx,int) or idx<0 or idx>=len(refs):
            item(model,source,'missing','Tile asset index unresolved.',key='tile'+str(pos));continue
        ref=refs[idx].get('m_Data',refs[idx])
        doc,path=assets.object(ref)
        sprite=get(doc.data,'m_Sprite') if doc else None
        texture=assets.path(sprite)
        if not texture:
            item(model,source,'missing','Tile sprite unresolved or procedural RuleTile.',key='tile'+str(idx));continue
        meta=assets.meta(texture).get('TextureImporter',{})
        sprites=get(meta,'spriteSheet','sprites',default=[]) or []
        sub=next((s for s in sprites if s.get('internalID')==get(sprite,'fileID')),None)
        if meta.get('spriteMode')==2 and not sub:
            item(model,source,'missing','Tile sprite atlas ID unresolved.',key='tile'+str(idx));continue
        spec=dict(file=Path(texture).relative_to(root).as_posix(),rect=sub.get('rect') if sub else None,pixelsPerUnit=meta.get('spritePixelsToUnits',100))
        out.append(dict(x=pos.get('x',0)+position[0],y=-pos.get('y',0)-position[1],sprite=spec))
    if out: item(model,source,'approx','Tile cells and sprite atlas imported; collider, tint, transforms and procedural rules require review.',key='tiles')
    return out
