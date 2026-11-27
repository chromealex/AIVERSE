from pathlib import Path
import json, subprocess, shutil, tempfile
r=Path('/private/tmp/aiverse-issue223'); b=Path('/Volumes/KINGSTON/AIVERSE-builds/codex-issue223-minimap-atlas-quick')
p=Path(tempfile.mkdtemp(prefix='settings-mcp-',dir=b/'fixtures'))
shutil.copytree(r/'demos/client-command-controls',p,dirs_exist_ok=True)
e=r/'docs/evidence/issues-display-graph-mcp-20261127'; e.mkdir(exist_ok=True)
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
 rpc('initialize',{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'display-graph-acceptance','version':'1'}})
 rpc('tools/list',{});call('mcp_registry',{});call('open_game',{'game':str(p)})
 for version in [1,2]:
  graph={'version':version,'nodes':[{'id':'spawn','type':'Spawn','properties':{'burst':5}},{'id':'init','type':'Initialize','properties':{'life':0.8,'speed':2,'size':0.35}},{'id':'out','type':'SpriteOutput','properties':{'texture':'builtin:soft','blend':'additive','blendSpace':'display'}}],'links':[{'from':'spawn','to':'init'},{'from':'init','to':'out'}],'outputs':['out']}
  name='display_v'+str(version)
  valid=call('fx_graph_validate',{'graph':graph});assert valid['ok'],valid
  assert not valid['gpuEligible'] and 'display-space' in valid['gpuReason'],valid
  assert call('fx_graph_set',{'name':name,'graph':graph})['ok']
  assert call('fx_graph_get',{'name':name})['graph']==graph
  baked=call('fx_graph_bake',{'name':name,'effect':name});assert baked['ok'],baked
  effect=call('fx_get',{'name':name})['json'];assert effect['emitters'][0]['blendSpace']=='display',effect
  raw=(p/'fx/graphs'/ (name+'.vfx.json')).read_bytes()
  invalid=json.loads(json.dumps(graph));invalid['nodes'][2]['properties']['blend']='alpha'
  rejected=call('fx_graph_set',{'name':name,'graph':invalid,'expected':graph});assert not rejected['ok'],rejected
  assert (p/'fx/graphs'/(name+'.vfx.json')).read_bytes()==raw
  call('open_game',{'game':str(p)});assert call('fx_graph_get',{'name':name})['graph']==graph
 proc.stdin.close();assert proc.wait(timeout=15)==0
 (e/'result.json').write_text(json.dumps({'sha':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r,text=True).strip(),'checks':['v1/v2 validate/save/read/bake/reopen retain display space','GPU admission refuses lossy linear fallback','invalid alpha/display graph rejected without replacing saved graph'],'limits':['MCP authoring and compile evidence; GPU pixels retained separately; no graph-canvas interaction claim'],'responses':[x for x in rows if x['method']!='tools/list' and x['params'].get('name')!='mcp_registry']},indent=2)+'\n')
 print('PASS graph v1/v2 display-space authoring, bake, rejection, reopen')
finally:
 if proc.poll() is None:proc.terminate();proc.wait(timeout=10)
