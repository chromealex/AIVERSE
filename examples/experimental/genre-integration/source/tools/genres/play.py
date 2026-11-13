#!/usr/bin/env python3
"""Launch a reference project with the appropriate engine client (no shell)."""
import argparse
import json
import os
import subprocess
import shutil
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
    catalog=json.loads((ROOT/'examples/genres/index.json').read_text())
    parser=argparse.ArgumentParser()
    parser.add_argument('genre',choices=['list']+[entry['genre'] for entry in catalog])
    parser.add_argument('--build',type=Path,default=ROOT/'build/.temp/host')
    parser.add_argument('--dry-run',action='store_true')
    args=parser.parse_args()
    if args.genre=='list':
        print(json.dumps(catalog,indent=2));return 0
    entry=next(item for item in catalog if item['genre']==args.genre)
    build=args.build.resolve();project=(ROOT/entry['project']).resolve();module=entry['module']
    if module=='rts':
        candidates=['AiverseGame.app/Contents/MacOS/AiverseGame','AiverseGame','AiverseGame.exe','Release/AiverseGame.exe']
        flags=['--map','0'];environment={'AIVERSE_CONTENT':'{staged-content}','AIVERSE_SPLASH':'off'};target='aiverse_client'
    else:
        name='aiverse_fps_client' if module=='fps' else 'aiverse_genre_client'
        folder='fps' if module=='fps' else 'genres'
        candidates=[f'modules/{folder}/{name}',f'modules/{folder}/{name}.exe',f'modules/{folder}/Release/{name}.exe',name,name+'.exe']
        flags=['--project',str(project)];environment={};target=name
    executable=next((build/p for p in candidates if (build/p).is_file()),None)
    if executable is None:
        parser.error(f'client missing; configure its build option and run: cmake --build {build} --target {target}')
    command=[str(executable),*flags]
    if args.dry_run:
        print(json.dumps({'command':command,'environment':environment,'cwd':str(ROOT),'project':str(project),'engineAssets':str(ROOT/'engine/assets') if module=='rts' else None},indent=2));return 0
    if module=='rts':
        temporary=build/'.temp';temporary.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='genre-'+args.genre+'-',dir=temporary) as tmp:
            content=Path(tmp)/'content';shutil.copytree(project,content)
            shutil.copytree(ROOT/'engine/assets',content/'engine',dirs_exist_ok=True)
            environment['AIVERSE_CONTENT']=str(content)
            return subprocess.run(command,cwd=ROOT,env=dict(os.environ,**environment)).returncode
    return subprocess.run(command,cwd=ROOT,env=dict(os.environ,**environment)).returncode
if __name__=='__main__':raise SystemExit(main())
