#!/usr/bin/env python3
"""Run actual genre examples through headless and verify all three restore paths."""
import argparse
import re
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--headless',required=True,type=Path);args=p.parse_args()
for project in sorted(p for p in (ROOT/'examples/genres/rts').iterdir() if p.is_dir()):
    result=subprocess.run([str(args.headless.resolve()),'--project',str(project),'--seed','3','--max-seconds','100','--verify-restore-at','5','--verify-restore-ticks','100'],capture_output=True,text=True,timeout=90)
    out=result.stdout+result.stderr
    assert result.returncode==0,(project.name,out)
    assert '[script error' not in out,(project.name,out)
    assert 'winner=none' not in out and 'Result: winner=' in out,(project.name,out)
    for mode in ['snapshot','serialized','fresh serialized']:assert f'Restore OK ({mode})' in out,(project.name,out)
    events=dict((k,int(v)) for k,v in re.findall(r'(spawned|died|attacks|built|gathered|trained)=(\d+)',out))
    if project.name in ['action-rpg','survivors','moba','turn-based','tower-defense']:assert events['died']>0 and events['attacks']>0,(project.name,out)
    if project.name=='survival':assert events['built']>0 and events['gathered']>0,(project.name,out)
    print(project.name+': match completion and three restore paths passed')
