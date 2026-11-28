#!/usr/bin/env python3
"""Install editable Shader Graph presets and their material snapshots into a project."""
import argparse
import json
import os
from pathlib import Path
import tempfile

LIBRARY=Path(__file__).resolve().parents[2]/'examples/shader_graph/material_library'


def install(project, library=LIBRARY):
    project=Path(project).resolve()
    if not (project/'game.json').is_file():
        raise ValueError('project must contain game.json')
    material_file=project/'materials.json'
    shaders=project/'shaders'
    material_dir=project/'content'/'materials'
    for directory in (shaders, material_dir.parent, material_dir):
        if directory.is_symlink() or (directory.exists() and not directory.is_dir()):
            raise ValueError(f'invalid library directory: {directory}')
    if material_file.is_symlink():
        raise ValueError('material index must not be a symlink')
    original=material_file.read_bytes() if material_file.exists() else None
    doc=json.loads(original) if original is not None else {'materials':{}}
    if not isinstance(doc,dict) or not isinstance(doc.get('materials'),dict):
        raise ValueError('materials.json must contain a materials object')
    presets=json.loads((library/'materials.json').read_text())['materials']
    files={}
    for name,material in presets.items():
        if not name or Path(name).name!=name or name in doc['materials']:
            raise ValueError(f'invalid or existing material: {name}')
        graph_name=f'{name}.json'
        graph=json.loads((library/'shaders'/graph_name).read_text())
        if material['shaderGraph']!=graph:
            raise ValueError(f'graph snapshot differs: {name}')
        asset=material_dir/f'{name}.material.json'
        files[shaders/graph_name]=graph
        files[asset]=material
        doc['materials'][name]=asset.relative_to(project).as_posix()
    backup=project/'materials.before-shader-library.json'
    destinations=list(files)
    if original is not None:
        destinations.append(backup)
    for path in destinations:
        if path.exists() or path.is_symlink():
            raise ValueError(f'already exists: {path}')
    shaders.mkdir(exist_ok=True)
    material_dir.mkdir(parents=True,exist_ok=True)
    created=[]
    try:
        if original is not None:
            with backup.open('xb') as stream:
                created.append(backup)
                stream.write(original)
        for path,document in files.items():
            with path.open('x',encoding='utf-8') as stream:
                created.append(path)
                json.dump(document,stream,indent=2)
                stream.write('\n')
        fd,temp=tempfile.mkstemp(prefix='.materials-',suffix='.json',dir=project)
        try:
            with os.fdopen(fd,'w',encoding='utf-8') as stream:
                json.dump(doc,stream,indent=2)
                stream.write('\n')
            os.replace(temp,material_file)
        finally:
            Path(temp).unlink(missing_ok=True)
    except Exception:
        for path in reversed(created):
            path.unlink(missing_ok=True)
        raise
    return list(presets)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--project',required=True,type=Path)
    args=parser.parse_args()
    for name in install(args.project):print(name)
