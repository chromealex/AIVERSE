#!/usr/bin/env python3
"""Verify pinned assets, run sequential independent repetitions, retain raw timing samples."""
import argparse
import hashlib
import json
import os
import platform
import subprocess
from pathlib import Path
from analyze import summarize


def verify(scene):
    config=json.loads(scene.read_text())
    manifest=json.loads((scene.parent/'asset-manifest.json').read_text())
    lock_name=config.get('assetLock','sponza-lock.json')
    if lock_name not in ('sponza-lock.json','sponza-full-lock.json'): raise ValueError('unknown asset lock')
    lock=json.loads(Path(__file__).with_name(lock_name).read_text())
    if manifest['fingerprint']!=lock['fingerprint'] or config['assetFingerprint']!=lock['fingerprint']:
        raise ValueError('asset fingerprint does not match repository lock')
    for entry in lock['files']:
        path=scene.parent/entry['path']
        h=hashlib.sha256()
        with path.open('rb') as f:
            for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
        if path.stat().st_size!=entry['bytes'] or h.hexdigest()!=entry['sha256']:
            raise ValueError(f'modified asset: {path}')
    return config


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary',type=Path);p.add_argument('--scene',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path);p.add_argument('--profile',choices=['baseline','hdr','full','low'],default='baseline')
    p.add_argument('--repeats',type=int,default=3);p.add_argument('--warmup',type=int,default=90);p.add_argument('--samples',type=int,default=240)
    p.add_argument('--camera',type=int);p.add_argument('--capture',action='store_true');a=p.parse_args()
    if not 1<=a.repeats<=20:p.error('repeats must be 1..20')
    scene=a.scene.resolve();verify(scene);a.out.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    for key in ('RTS_HDR','RTS_POSTFX','RTS_EXIT_AFTER_FRAMES','RTS_SCREENSHOT','MTL_DEBUG_LAYER','AIVERSE_HDR','AIVERSE_POSTFX'):
        env.pop(key,None)
    summaries=[]
    for i in range(a.repeats):
        report=a.out/f'{a.profile}-{i+1}.json'
        if report.exists():raise FileExistsError(f'will not overwrite {report}')
        command=[str(a.binary.resolve()),'--scene',str(scene),'--profile',a.profile,'--output',str(report.resolve()),'--samples',str(a.samples),'--warmup',str(a.warmup)]
        if a.camera is not None:command+=['--camera',str(a.camera)]
        if a.capture and i==0:command+=['--capture',str((a.out/'capture.ppm').resolve())]
        with (a.out/f'{a.profile}-{i+1}.log').open('w') as log:
            subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        r=json.loads(report.read_text());r['assetVerification']='sha256';r['hostOS']=platform.platform();r['hostArchitecture']=platform.machine();r['hostId']=hashlib.sha256(platform.node().encode()).hexdigest()
        report.write_text(json.dumps(r,indent=2)+'\n')
        summaries.append(dict(file=str(report),cameras=summarize(r)))
        print(json.dumps(summaries[-1],indent=2),flush=True)
    (a.out/'summary.json').write_text(json.dumps(summaries,indent=2)+'\n')

if __name__=='__main__':main()
