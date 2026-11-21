#!/usr/bin/env python3
"""Install bundled terrain PBR assets in an existing project without overwriting materials.
Use --default-checkerboard to opt into the neutral default for that project.
"""
from pathlib import Path
import argparse,json,shutil

def install(project,default=False):
    project=Path(project);source=Path(__file__).resolve().parents[2]/'engine/assets/materials/terrain'
    manifest=project/'game.json'
    if not manifest.is_file():raise ValueError('Destination must contain game.json')
    target=project/'materials/terrain'
    if target.exists():raise ValueError('materials/terrain already exists; refusing to overwrite')
    existing=json.loads((project/'materials.json').read_text()) if (project/'materials.json').exists() else {}
    bundled=json.loads((source/'materials.json').read_text())
    collisions=set(existing)&set(bundled)
    if collisions:raise ValueError('Material IDs already exist: '+', '.join(sorted(collisions)))
    game=json.loads(manifest.read_text())
    if default:game['terrain']=json.loads((source/'default-terrain.json').read_text())
    shutil.copytree(source,target)
    (project/'materials.json').write_text(json.dumps(dict(existing,**bundled),indent=2)+'\n')
    if default:manifest.write_text(json.dumps(game,indent=2)+'\n')
    print('Installed',len(bundled),'PBR materials in',project)
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('project',type=Path);ap.add_argument('--default-checkerboard',action='store_true');a=ap.parse_args();install(a.project,a.default_checkerboard)
