#!/usr/bin/env python3
"""Local authenticated integration; private sessions/SQLite never enter game content."""
import argparse,json,os,pathlib,subprocess,time,urllib.request,sqlite3
p=argparse.ArgumentParser();p.add_argument('--engine',required=True);p.add_argument('--phase',choices=['start','verify','seed'],required=True);args=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1]; tmp=root/'Builds/.temp'; private=tmp/'private';private.mkdir(exist_ok=True);private.chmod(0o700)
engine=pathlib.Path(args.engine);host=engine/'tools/meta/server.py';db=private/'meta.sqlite'
def issue(authority=False):
 cmd=['python3',str(host),'--db',str(db),'user','player-one']
 if authority:cmd+=['--authority']
 token=subprocess.check_output(cmd,text=True).strip();path=private/('authority-token' if authority else 'player-token');path.write_text(token);path.chmod(0o600);return token
def rpc(req,authority=False):
 token=(private/('authority-token' if authority else 'player-token')).read_text().strip()
 request=urllib.request.Request('http://127.0.0.1:18765/rpc',data=json.dumps(req).encode(),headers={'Content-Type':'application/json','Authorization':'Bearer '+token})
 with urllib.request.urlopen(request,timeout=20) as response:return json.load(response)
if args.phase=='start':
 issue();issue(True)
 log=open(tmp/'evidence/server.log','w')
 child=subprocess.Popen(['python3',str(host),'--db',str(db),'serve','--binary',str(tmp/'server-make/meta_server'),'--config',str(root/'server/service.json'),'--port','18765'],stdout=log,stderr=log,start_new_session=True)
 (private/'server.pid').write_text(str(child.pid));time.sleep(1)
 if child.poll() is not None:raise RuntimeError('server startup failed; inspect server.log')
 print('server started')
elif args.phase=='seed':
 # Server-owned local match: never accept a score from the player client.
 match=subprocess.run([str(tmp/'client/engine/aiverse_headless'),'--project',str(tmp/'public-content'),'--seed','3'],capture_output=True,text=True,check=True)
 (tmp/'evidence/match.log').write_text(match.stdout+match.stderr)
 assert 'Result: winner=Blue' in match.stdout, 'Expected mapped player-one (Blue) to win'
 assert rpc(dict(service='profile',action='create',operation='server-create-v1',arguments={}))['code'] in (0,1)
 assert rpc(dict(service='board',action='submit',operation='server-verified-match-v1',arguments={'id':'wins','score':1}),True)['code'] in (0,1)
 print('trusted result recorded')
else:
 result=rpc(dict(service='meta',action='view',operation='',arguments={}))
 (tmp/'evidence/server-view.json').write_text(json.dumps(result,indent=2))
 data=result['data'];assert data['profile']['balances']=={'banner':1,'gold':75},data
 assert len(data['chats']['global'])==1,data
 assert data['boards']['wins']==[{'player':'player-one','rank':1,'score':1}],data
 print('SERVER_VERIFY_PASS wallet=75 banner=1 chat=1 rank=1 score=1')
