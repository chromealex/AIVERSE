"""Receiver for the versioned JSON produced inside Unreal Editor."""
from lib.port.common import inventory, new_model, item, read_json, safe


def scan(root):
    path=root/'unreal-export.json'
    if not path.exists():
        return dict(engine='unreal',ok=False,errors=['Run tools/importers/unreal_export/export.py inside Unreal Editor; .uasset files cannot be read directly.'],assets=[],scenes=[])
    data=read_json(path)
    if data.get('format')!=1: raise ValueError('unsupported Unreal export format')
    return dict(engine='unreal',version=data.get('version',''),ok=True,errors=[],assets=inventory(root),scenes=[s['name'] for s in data.get('scenes',[])])


def extract(root):
    info=scan(root)
    if not info['ok']: raise ValueError(info['errors'][0])
    data=read_json(root/'unreal-export.json'); model=new_model(root,'unreal',info['version'])
    for key in ('assets','entities','scenes','scripts','items'): model[key]=data.get(key,[])
    for asset in model['assets']: safe(root,asset['path'])
    for entity in model['entities']:
        item(model,entity['source'],'approx','Unreal exported properties require semantic review; Blueprint execution is not translated.',key='blueprint')
    return model
