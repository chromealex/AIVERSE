#!/usr/bin/env python3
"""Sequential real-window runs; measures callback intervals including platform pacing.
Run without AIVERSE_SCREENSHOT / AIVERSE_EXIT_AFTER_FRAMES; captures are separate QA.
"""
import argparse, json, os, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--binary',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--repeats',type=int,default=3)
p.add_argument('--frames',type=int,default=180)
p.add_argument('--warmup',type=int,default=60)
a=p.parse_args()
env=os.environ.copy()
for key in ('AIVERSE_SCREENSHOT','AIVERSE_EXIT_AFTER_FRAMES'):env.pop(key,None)
env['AIVERSE_AUDIO']='off'
report={'method':'real visible 1280x800 window; all cloths simulated and instanced; no culling, sleep, shadows or self-collision; frame intervals include OS/vsync scheduling and GPU backpressure, NOT GPU timestamp timings','runs':[]}
for count,cells,workers in [(100,16,8),(256,16,8),(512,16,8),(256,24,8),(256,24,16),(100,71,8),(256,71,8)]:
    for repeat in range(a.repeats):
        with tempfile.TemporaryDirectory(prefix='aiverse-cloth-frame-') as tmp:
            output=Path(tmp)/'frame.json'
            cmd=[str(a.binary),'--objects',str(count),'--cells',str(cells),'--workers',str(workers),'--frames',str(a.frames),'--warmup',str(a.warmup),'--report',str(output),'--share','0','--lod','0','--auto-lod','0']
            result=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=300)
            if result.returncode or not output.exists():raise RuntimeError(result.stdout+result.stderr)
            row=json.loads(output.read_text())
        if row['frames']!=a.frames or not row['healthy']:raise RuntimeError('incomplete or unstable benchmark')
        row['repeat']=repeat+1;report['runs'].append(row)
        print(count,cells,repeat+1,row['frame_interval_ms'],flush=True)
        a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(report,indent=2)+'\n')
