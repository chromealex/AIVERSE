import os,subprocess,json,shutil,tempfile
from pathlib import Path
r=Path('/private/tmp/aiverse-issue223');b=Path('/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick');out=b/'issues-material-performance-20261127';p=Path(tempfile.mkdtemp(prefix='material-load-',dir=b/'fixtures'));shutil.copytree(r/'demos/material-effects',p,dirs_exist_ok=True)
env=dict(os.environ,AIVERSE_CONTENT=str(p),AIVERSE_CLIENT_PROFILE='1',AIVERSE_FRAME_TRACE='1',AIVERSE_EXIT_AFTER_FRAMES='720',AIVERSE_SHADOWS='high',AIVERSE_BACKGROUND='1',AIVERSE_AUDIO='off',AIVERSE_SEED='7',AIVERSE_SPLASH='off',TMPDIR=str(b/'tmp'))
for mode in ['linear','display']:
 for name in ['linear','display']:
  f=p/'fx'/(name+'.json');d=json.loads(f.read_text());d['emitters'][0].update(rate=256,life=2,size=0.3,maxParticles=1024,blendSpace=mode);f.write_text(json.dumps(d))
 log=out/('load-'+mode+'.log.txt')
 with log.open('w') as f:
  x=subprocess.run(['/usr/bin/time','-l',str(b/'AiverseGame.app/Contents/MacOS/AiverseGame'),'--map','2','--size','1280x720'],cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 print(mode,'exit',x.returncode,flush=True)
 for line in log.read_text().splitlines():
  if '[client-stages]' in line or 'maximum resident' in line or '[mac-frame]' in line:print(line,flush=True)
 assert x.returncode==0
(out/'load-fixture.json').write_text(json.dumps({'fixture':str(p),'rate_per_emitter':256,'life':2,'emitters':2,'steady_particles_expected':1024,'size':0.3,'cap_per_emitter':1024,'frames':720,'cpu_block_budget_ms':16.67,'gpu_sample_budget_ms':16.67,'rss_budget_bytes':536870912},indent=2)+'\n')
