#!/usr/bin/env python3
import pathlib,re,subprocess,json
r=pathlib.Path(__file__).resolve().parent;t=r/'Builds/.temp';e=t/'evidence'
client=t/'client/engine/AiverseGame.app/Contents/MacOS/AiverseGame';server=t/'server-make/meta_server'
cl=(t/'client/engine/CMakeFiles/aiverse_client.dir/link.txt').read_text();sl=(t/'server-make/CMakeFiles/meta_server.dir/link.txt').read_text()
(e/'client-link.txt').write_text(cl);(e/'server-link.txt').write_text(sl)
archives=lambda x: sorted(set(re.findall(r'libaiverse_meta_[a-z_]+\.a',x)))
assert archives(cl)==['libaiverse_meta_client.a'],archives(cl)
expected=['data','economy','live_core','schema','server','social']
assert archives(sl)==sorted('libaiverse_meta_'+x+'.a' for x in expected),archives(sl)
for name,binary in [('client',client),('server',server)]:
 symbols=subprocess.check_output(['nm','-C',str(binary)],text=True);(e/(name+'-symbols.txt')).write_text(symbols)
 forbidden=['aiverse::meta::Service::','aiverse::meta::Economy::','aiverse::meta::Chat::'] if name=='client' else ['aiverse::meta::Client::','modules::registerLoot','modules::registerClans','modules::all(']
 for symbol in forbidden:assert symbol not in symbols,(name,symbol)
assert not list((t/'client/engine/engine/meta').glob('libaiverse_meta_*.a')) or len(list((t/'client/engine/engine/meta').glob('libaiverse_meta_*.a')))==1
files=(e/'pack-files.txt').read_text()
for needle in ['server/','host/','.sqlite','.sql','-token','service.json','SKILL_AUDIT','CMakeLists']:
 assert needle not in files,needle
report={'client_meta_archives':archives(cl),'server_meta_archives':archives(sl),'pack_private_paths':'absent','symbol_checks':'pass','client_binary':str(client),'server_binary':str(server)}
(e/'isolation.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
