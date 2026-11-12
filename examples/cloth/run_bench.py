#!/usr/bin/env python3
"""Run sequential warmed cloth workloads; save every sample and machine/build provenance."""
import argparse, hashlib, json, platform, subprocess
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--repeats',type=int,default=3)
p.add_argument('--frames',type=int,default=180)
p.add_argument('--warmup',type=int,default=60)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
def command(args):
    r=subprocess.run(args,capture_output=True,text=True)
    return (r.stdout or r.stderr).strip()
source_files=sorted((root/'engine/cloth').glob('*'))+sorted((root/'examples/cloth').glob('*.cpp'))+sorted((root/'examples/cloth').glob('*.h'))
digest=hashlib.sha256()
for f in source_files:
    if f.is_file():digest.update(str(f.relative_to(root)).encode());digest.update(f.read_bytes())
report={'host':platform.platform(),'machine':platform.machine(),'cpu':command(['sysctl','-n','machdep.cpu.brand_string']) if platform.system()=='Darwin' else platform.processor(),
        'binary':command(['file',str(a.binary)]),'base_commit':command(['git','-C',str(root),'rev-parse','HEAD']),
        'source_sha256':digest.hexdigest(),'method':'fixed 1/60s; all cloths active; 1 capsule and floor per cloth; wind; no render, sleep, culling or self-collision; serial runs', 'runs':[]}
for count,cells,workers in [(100,16,1),(100,16,8),(256,16,8),(512,16,8),(256,24,8),(256,24,16),(100,71,8),(256,71,8)]:
    for repeat in range(a.repeats):
        row=json.loads(subprocess.check_output([str(a.binary),str(count),str(cells),str(workers),str(a.frames),str(a.warmup)],text=True))
        row['repeat']=repeat+1
        if not row['healthy']:raise RuntimeError('unstable cloth')
        report['runs'].append(row)
        print(count,cells,workers,repeat+1,row['simulation_ms'],flush=True)
        a.out.parent.mkdir(parents=True,exist_ok=True)
        a.out.write_text(json.dumps(report,indent=2)+'\n')
