from pathlib import Path
import json, subprocess, shutil, tempfile
r=Path('/private/tmp/aiverse-issue223'); b=Path('/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick')
p=Path(tempfile.mkdtemp(prefix='settings-mcp-',dir=b/'fixtures'))
shutil.copytree(r/'demos/client-command-controls',p,dirs_exist_ok=True)
e=r/'docs/evidence/issues-project-settings-mcp-20261127'; e.mkdir(exist_ok=True)
proc=subprocess.Popen([str(b/'aiversemcp'),'--no-editor','--port','0','--engine',str(r)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
rows=[]
def rpc(method,params):
 i=len(rows)+1; proc.stdin.write(json.dumps({'jsonrpc':'2.0','id':i,'method':method,'params':params})+'\n');proc.stdin.flush()
 while True:
  line=proc.stdout.readline(); assert line,'server ended'; v=json.loads(line)
  if v.get('id')==i:break
 rows.append({'method':method,'params':params,'response':v});return v

def call(name,args={},fail=False):
 v=rpc('tools/call',{'name':name,'arguments':args});res=v.get('result',{})
 if fail:
  assert 'error' in v or res.get('isError'),v
  return v
 assert 'error' not in v and not res.get('isError'),v
 return json.loads(res['content'][0]['text'])
try:
 rpc('initialize',{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'settings-acceptance','version':'1'}})
 listed=rpc('tools/list',{}); assert {'game_info','set_manifest'} <= {x['name'] for x in listed['result']['tools']}
 call('mcp_registry',{})
 call('open_game',{'game':str(p)}); old=call('game_info'); original=(p/'game.json').read_bytes()
 desired=old['manifest']; desired['selectionShortcut']['hotkey']='F8'; desired['ui']['portrait']['background']=[0.2,0.1,0.3,1]; desired['minimap']['bounds']=[5,3,19,13]; desired['acceptanceInteger']=9007199254740993
 call('set_manifest',{'path':'','valueJson':json.dumps(desired),'expectedManifest':old['originalManifest']})
 saved=(p/'game.json').read_bytes(); assert json.loads(saved)==desired
 current=call('game_info');assert current['manifest']==desired
 call('set_manifest',{'path':'selectionShortcut.hotkey','value':'F9','expectedManifest':old['originalManifest']},True)
 for raw in ['{','[]']:
  call('set_manifest',{'path':'','valueJson':raw,'expectedManifest':current['originalManifest']},True)
 assert (p/'game.json').read_bytes()==saved
 status=call('project_history_status'); print('history keys',list(status))
 version=status.get('version');assert isinstance(version,str),status
 call('project_history_step',{'direction':'undo','expectedVersion':version});assert (p/'game.json').read_bytes()==original
 status=call('project_history_status');call('project_history_step',{'direction':'redo','expectedVersion':status['version']});assert (p/'game.json').read_bytes()==saved
 call('open_game',{'game':str(p)});assert call('game_info')['manifest']==desired
 external=saved+b'\n';(p/'game.json').write_bytes(external)
 call('set_manifest',{'path':'selectionShortcut.hotkey','value':'F10'},True);assert (p/'game.json').read_bytes()==external
 proc.stdin.close();assert proc.wait(timeout=15)==0
 (e/'result.json').write_text(json.dumps({'sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'checks':['registry','raw integer preservation','guarded save/read','stale rejection','malformed/root rejection','exact-byte undo/redo','reopen','external file conflict preserves bytes'],'limits':['standalone MCP; native Studio acceptance recorded separately'],'responses':rows},indent=2)+'\n')
 print('PASS 8 MCP acceptance groups; process exit 0; '+str(e))
finally:
 if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
