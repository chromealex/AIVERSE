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
    if material_file.is_symlink() or shaders.is_symlink():
        raise ValueError('material library destinations must not be symlinks')
    doc=json.loads(material_file.read_text()) if material_file.exists() else {'materials':{}}
    if not isinstance(doc,dict) or not isinstance(doc.get('materials'),dict):
        raise ValueError('materials.json must contain a materials object')
    presets=json.loads((library/'materials.json').read_text())['materials']
    graphs={name:json.loads((library/'shaders'/f'{name}.json').read_text()) for name in presets}
    for name,g in graphs.items():
        if name in doc['materials'] or (shaders/f'{name}.json').exists() or (shaders/f'{name}.json').is_symlink():
            raise ValueError(f'already exists: {name}; existing materials and graphs are never overwritten')
        if presets[name]['shaderGraph']!=g:
            raise ValueError(f'graph snapshot differs: {name}')
    # Check all conflicts before writing; retain an exact backup of an existing library.
    backup=project/'materials.before-shader-library.json'
    if material_file.exists():
        with backup.open('xb') as f:f.write(material_file.read_bytes())
    shaders.mkdir(exist_ok=True)
    for name,g in graphs.items():
        with (shaders/f'{name}.json').open('x') as f:json.dump(g,f,indent=2);f.write('\n')
    doc['materials'].update(presets)
    fd,temp=tempfile.mkstemp(prefix='.materials-',suffix='.json',dir=project)
    try:
        with os.fdopen(fd,'w') as f:json.dump(doc,f,indent=2);f.write('\n')
        os.replace(temp,material_file)
    finally:
        if os.path.exists(temp):os.unlink(temp)
    return list(presets)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--project',required=True,type=Path)
    args=parser.parse_args()
    for name in install(args.project):print(name)
