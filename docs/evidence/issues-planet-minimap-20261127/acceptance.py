import os,json,subprocess,tempfile,shutil
from pathlib import Path
r=Path('/private/tmp/aiverse-issue223');b=Path('/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick');p=Path(tempfile.mkdtemp(prefix='planet-minimap-',dir=b/'fixtures'));out=b/'issues-planet-minimap-20261127';out.mkdir(exist_ok=True)
shutil.copytree(r/'examples/projectiles',p,dirs_exist_ok=True)
rules={'victory':{'mode':'survive','seconds':120},'types':{name:{'health':{'max':100},'sight':40,'view':{'shape':'soldier','scale':1,'minimap':True}} for name in ['left','right']}}
m=json.loads((p/'maps/planet.json').read_text());m['entities']=[{'type':name,'owner':1,'at':{'body':'p','face':0,'x':x,'y':8}} for name,x in [('left',5),('right',11)]];(p/'maps/planet.json').write_text(json.dumps(m))
for case,flags in [('both',(True,True)),('right_only',(False,True)),('none',(False,False))]:
 for name,flag in zip(['left','right'],flags):rules['types'][name]['view']['minimap']=flag
 (p/'rules.json').write_text(json.dumps(rules))
 env=dict(os.environ,AIVERSE_CONTENT=str(p),AIVERSE_EXIT_AFTER_FRAMES='180',AIVERSE_BACKGROUND='1',AIVERSE_AUDIO='off',AIVERSE_SEED='7',AIVERSE_SPLASH='off',AIVERSE_MINIMAP_DUMP=str(out/(case+'.ppm')),AIVERSE_CLIENT_PROFILE='1',AIVERSE_FRAME_TRACE='1',TMPDIR=str(b/'tmp'))
 with (out/(case+'.log.txt')).open('w') as f:
  x=subprocess.run(['/usr/bin/time','-l',str(b/'AiverseGame.app/Contents/MacOS/AiverseGame'),'--map','2','--size','1280x720'],cwd=r,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 assert x.returncode==0,(case,x.returncode)
 assert (out/(case+'.ppm')).is_file(),case
 print(case,'exit0 dump', (out/(case+'.ppm')).stat().st_size,flush=True)
(out/'fixture.json').write_text(json.dumps({'path':str(p),'rules':rules,'map':m},indent=2)+'\n')
