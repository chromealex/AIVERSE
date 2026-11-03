"""Validate neutral input before touching output files."""
import math
from pathlib import Path
from .common import safe


def validate(model):
    if not isinstance(model,dict) or model.get('format') != 1: raise ValueError('unsupported port.json format')
    root=Path(model['sourceRoot']).resolve()
    for key in ('assets','entities','scenes','scripts','items'):
        if not isinstance(model.get(key),list): raise ValueError(key+' must be an array')
    ids=set()
    for entity in model['entities']:
        key=entity['id']
        if not isinstance(key,str) or not key or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in key):
            raise ValueError('invalid entity id')
        if key in ids: raise ValueError('duplicate entity id: '+key)
        ids.add(key)
        if not isinstance(entity.get('fields',{}),dict): raise ValueError('entity fields must be an object')
        for path in entity.get('models',[]): safe(root,path)
    scenes=set()
    for scene in model['scenes']:
        if scene['id'] in scenes: raise ValueError('duplicate scene id')
        scenes.add(scene['id']); safe(root,'maps/'+scene['id']+'.json')
        for obj in scene['objects']:
            if obj['entity'] not in ids: raise ValueError('unknown scene entity: '+obj['entity'])
            if len(obj['position'])!=3 or any(not isinstance(x,(int,float)) or not math.isfinite(x) for x in obj['position']):
                raise ValueError('invalid scene position')
    for asset in model['assets']:
        if not safe(root,asset['path']).is_file(): raise ValueError('missing source asset: '+asset['path'])
