from pathlib import Path
import json, subprocess, shutil, tempfile
r=Path('/private/tmp/aiverse-issue223'); b=Path('/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick')
p=Path(tempfile.mkdtemp(prefix='settings-mcp-',dir=b/'fixtures'))
shutil.copytree(r/'demos/client-command-controls',p,dirs_exist_ok=True)
e=r/'docs/evidence/issues-material-mcp-current-20261127'; e.mkdir(exist_ok=True)
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
 rpc('initialize',{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'material-acceptance','version':'1'}})
 rpc('tools/list',{});call('mcp_registry',{});call('open_game',{'game':str(p)})
 for mode in ['additive','additive-unweighted']:
  name=mode.replace('-','_')
  assert call('material_set',{'name':name,'material':{'alpha':mode}})['ok']
  assert call('material_get',{'name':name})['material']['alpha']==mode
 saved=(p/'materials.json').read_bytes()
 call('material_set',{'name':'additive','material':{'alpha':'unsupported'}},True)
 assert (p/'materials.json').read_bytes()==saved
 call('open_game',{'game':str(p)})
 for mode in ['additive','additive-unweighted']:
  assert call('material_get',{'name':mode.replace('-','_')})['material']['alpha']==mode
 proc.stdin.close();assert proc.wait(timeout=15)==0
 (e/'result.json').write_text(json.dumps({'sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'checks':['both additive modes actual material_set/get','invalid blend refuses without disk mutation','reopen retains both modes'],'limits':['standalone MCP authoring; rendering and native Studio evidenced separately'],'responses':[x for x in rows if x['method']!='tools/list' and x['params'].get('name')!='mcp_registry']},indent=2)+'\n')
 print('PASS actual material_set/get/reject/reopen for both additive modes')
finally:
 if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
