"""Versioned intermediate data and stable provenance; no engine dependencies."""
import hashlib
import json
import os
import re
from pathlib import Path

SKIP = {'.git', '.godot', '.import', 'Library', 'Temp', 'Logs', 'obj', 'Binaries', 'Intermediate', 'Saved'}
KINDS = {**dict.fromkeys(('.png', '.tga', '.jpg', '.jpeg', '.psd', '.tif', '.tiff'), 'texture'),
         **dict.fromkeys(('.fbx', '.obj', '.gltf', '.glb', '.dae', '.3ds'), 'model'),
         **dict.fromkeys(('.wav', '.ogg', '.mp3'), 'audio'),
         **dict.fromkeys(('.cs', '.gd'), 'script'), '.bin': 'dependency', '.mtl': 'dependency'}


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def encoded(data):
    return (json.dumps(data, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + '\n').encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def ident(source):
    return re.sub(r'[^a-z0-9_]+', '_', Path(source.split('#')[0]).stem.lower()).strip('_')[:40] + '_' + digest(source.encode())[:10]


def safe(root, relative):
    p = Path(relative)
    if p.is_absolute() or '\\' in relative or '..' in p.parts:
        raise ValueError('unsafe relative path: ' + relative)
    result = (root / p).resolve()
    if not is_within(result, root.resolve()):
        raise ValueError('path escapes project: ' + relative)
    return result


def files(root):
    for directory, dirs, names in os.walk(root, followlinks=False):
        dirs[:] = sorted(d for d in dirs if d not in SKIP and not (Path(directory) / d).is_symlink())
        for name in sorted(names):
            p = Path(directory) / name
            if not p.is_symlink():
                yield p


def item(model, source, status, reason, result='', **extra):
    key = source + ':' + extra.pop('key', reason)
    entry = dict(id=ident(key), source=source, status=status, reason=reason, result=result, **extra)
    model['items'].append(entry)
    return entry


def new_model(root, engine, version=''):
    return dict(format=1, engine=engine, version=version, sourceRoot=str(root.resolve()), name=root.name,
                assets=[], entities=[], scenes=[], scripts=[], items=[])


def inventory(root):
    return [dict(path=p.relative_to(root).as_posix(), kind=KINDS.get(p.suffix.lower(), 'other'), bytes=p.stat().st_size)
            for p in files(root)]


def script_index(model, root, asset):
    text = safe(root, asset['path']).read_text(encoding='utf-8-sig', errors='replace')
    classes = re.findall(r'\b(?:class|class_name)\s+(\w+)', text)
    declarations = re.findall(r'(?m)^.*(?:@export|export\s+var|\bpublic\s+|\[SerializeField\]).*$', text)
    entry = dict(path=asset['path'], classes=classes, declarations=declarations, source=text, uses=[])
    model['scripts'].append(entry)
    item(model, asset['path'], 'missing', 'Script logic requires rules, Lua or a native module; source indexed.', key='script')
    return entry


def is_within(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False
