#!/usr/bin/env python3
"""Compare identical-simulation reuse with the independent solver on dense cloths."""
import argparse, hashlib, json, os, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--mode',choices=['cpu','view'],required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--frames',type=int,default=120)
p.add_argument('--warmup',type=int,default=30)
p.add_argument('--repeats',type=int,default=3)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
digest=hashlib.sha256()
for path in sorted((root/'engine/cloth').glob('*'))+sorted((root/'examples/cloth').glob('*.cpp'))+sorted((root/'examples/cloth').glob('*.h')):
    if path.is_file():digest.update(str(path.relative_to(root)).encode());digest.update(path.read_bytes())
report={'source_sha256':digest.hexdigest(),'mode':a.mode,'method':'dense 71x71 cells (5184 simulated particles and 10082 rendered triangles per cloth); 8 workers; 2 substeps x 4 iterations; one local capsule per cloth; same total objects with and without sharing; copy and exact comparisons included in simulation time','runs':[]}
env=os.environ.copy()
for key in ('AIVERSE_SCREENSHOT','AIVERSE_EXIT_AFTER_FRAMES'):env.pop(key,None)
env['AIVERSE_AUDIO']='off'
for count,share,groups in [(256,0,1),(256,1,1),(256,1,16),(256,1,256),(512,1,1),(512,1,16)]:
    for repeat in range(a.repeats):
        if a.mode=='cpu':
            cmd=[str(a.binary),str(count),'71','8',str(a.frames),str(a.warmup),'4',str(share),str(groups)]
            row=json.loads(subprocess.check_output(cmd,text=True,env=env,timeout=300))
        else:
            with tempfile.TemporaryDirectory(prefix='aiverse-cloth-sharing-') as tmp:
                path=Path(tmp)/'report.json'
                cmd=[str(a.binary),'--objects',str(count),'--cells','71','--workers','8','--frames',str(a.frames),'--warmup',str(a.warmup),'--share',str(share),'--wind-groups',str(groups),'--report',str(path)]
                result=subprocess.run(cmd,text=True,capture_output=True,env=env,timeout=300)
                if result.returncode or not path.exists():raise RuntimeError(result.stdout+result.stderr)
                row=json.loads(path.read_text())
        if row['frames']!=a.frames or not row['healthy']:raise RuntimeError('incomplete or unstable run')
        expected=groups if share else count
        if row['simulated_objects']!=expected:raise RuntimeError('unexpected simulation group count')
        row['repeat']=repeat+1;report['runs'].append(row)
        print(count,share,groups,repeat+1,row.get('frame_interval_ms',row['simulation_ms']),flush=True)
        a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2)+'\n')
