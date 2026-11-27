import os,subprocess,json
from pathlib import Path
r=Path('/private/tmp/aiverse-issue223');b=Path('/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick');out=b/'issues-client-performance-20261127';out.mkdir(exist_ok=True)
cases=[('selection256',b/'fixtures/selection-boundary',0)]+[('projectile-'+str(i),r/'demos/projectile-segments',i) for i in range(4)]
for name,root,scene in cases:
 env=dict(os.environ,AIVERSE_CONTENT=str(root),AIVERSE_CLIENT_PROFILE='1',AIVERSE_VIEW_PROFILE='1',AIVERSE_FRAME_TRACE='1',AIVERSE_EXIT_AFTER_FRAMES='480',AIVERSE_SHADOWS='high',AIVERSE_BACKGROUND='1',AIVERSE_AUDIO='off',AIVERSE_SEED='7',AIVERSE_SPLASH='off',TMPDIR=str(b/'tmp'))
 log=out/(name+'.log.txt')
 with log.open('w') as f:
  x=subprocess.run(['/usr/bin/time','-l',str(b/'AiverseGame.app/Contents/MacOS/AiverseGame'),'--map',str(scene),'--size','1280x720'],cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 print(name,'exit',x.returncode,flush=True)
 for line in log.read_text().splitlines():
  if '[client-stages]' in line or 'maximum resident' in line:print(line,flush=True)
 assert x.returncode==0
(out/'workloads.json').write_text(json.dumps({'cases':[(n,str(p),i) for n,p,i in cases],'frames':480,'cpu_budget_ms':16.67,'sampled_gpu_budget_ms':16.67,'rss_budget_bytes':536870912},indent=2)+'\n')
