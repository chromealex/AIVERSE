"""Build every meta selection separately; inspect linked symbols and default-build archives."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/meta'))
import kits
modules={m['id']:m for m in json.loads((ROOT/'engine/meta/modules.json').read_text())['modules']}
requested=sys.argv[1:] or list(modules)
assert set(requested)<=set(modules), 'unknown policy selection'
nm=shutil.which('nm')
xcode=Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/nm')
if xcode.is_file():nm=str(xcode)
def run(args):
    result=subprocess.run([str(a) for a in args],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if result.returncode:raise RuntimeError(result.stdout)
    return result.stdout

def selected(feature):
    found={'economy'}
    def visit(key):
        if key in found:return
        found.add(key)
        for dep in modules[key]['dependencies']:visit(dep)
    visit(feature);return found

with tempfile.TemporaryDirectory(prefix='aiverse-selection-') as temporary:
    root=Path(temporary);build=root/'build'
    cmake=['cmake_minimum_required(VERSION 3.22)','project(Isolation LANGUAGES CXX)',
           f'add_subdirectory("{ROOT}/engine/meta" meta)',f'add_subdirectory("{ROOT}/libraries" libraries)']
    for feature in modules:
        name=feature.replace('-','_');kit='kit_'+name;exe='check_'+name
        cmake += [f'aiverse_add_meta_server({kit} MODULES {feature})',f'add_executable({exe} EXCLUDE_FROM_ALL {exe}.cpp)',f'target_link_libraries({exe} PRIVATE {kit})']
        config=json.dumps(kits.definitions(kits.resolve([feature])),separators=(',',':'))
        (root/(exe+'.cpp')).write_text(f'''#include "{kit}.h"
#include <stdexcept>
int main() {{
    using namespace aiverse::meta;
    auto chosen=kits::{kit}();
    if(chosen.features.size()!={len(selected(feature))})return 1;
    Service s(chosen,ServiceConfig::parse(aiverse::Json::parse(R"cfg({config})cfg")));
    auto r=s.execute({{"p",false}},{{"profile","create","init",aiverse::Json::object()}},10);
    if(!r.result)return 2;
    auto before=s.save().dump(-1);
    r=s.execute({{"p",false}},{{"not-selected","unknown","blocked",aiverse::Json::object()}},11);
    if(r.result.code!=Code::Unsupported||s.save().dump(-1)!=before)return 3;
    return 0;
}}
''')
    # A real header-only package must not acquire FPS/world/meta through an umbrella.
    cmake+=['add_executable(check_header EXCLUDE_FROM_ALL header.cpp)','target_link_libraries(check_header PRIVATE AIVERSE::lib_melee_combat)']
    (root/'header.cpp').write_text('#include <aiverselib/genre/Melee.h>\nint main(){return 0;}\n')
    (root/'CMakeLists.txt').write_text('\n'.join(cmake)+'\n')
    run(['cmake','-S',root,'-B',build])
    run(['cmake','--build',build,'-j','6'])
    assert not list(build.rglob('*.a')), 'unused optional libraries built by default'
    run(['cmake','--build',build,'--target','check_header','-j','6'])
    assert not list(build.rglob('*.a')), 'header-only selection built runtime archives'
    client_source=root/'client.cpp'
    client_source.write_text('#include <engine/meta/Client.h>\nint main(){aiverse::meta::OfflineBackend b;aiverse::meta::Client c(b);return c.pending();}\n')
    with (root/'CMakeLists.txt').open('a') as out:
        out.write('add_executable(check_client EXCLUDE_FROM_ALL client.cpp)\ntarget_link_libraries(check_client PRIVATE AIVERSE::meta_client)\n')
    run(['cmake','-S',root,'-B',build])
    run(['cmake','--build',build,'--target','check_client','-j','6']);run([build/'check_client'])
    assert {p.name for p in build.rglob('*.a')}=={'libaiverse_meta_data.a','libaiverse_meta_client.a'}
    client_symbols=run([nm,'-C',build/'check_client'])
    assert 'aiverse::meta::Service::' not in client_symbols and 'aiverse::meta::Economy::' not in client_symbols
    # Exercise the selected reference service, not only direct Service construction.
    with (root/'CMakeLists.txt').open('a') as out:
        out.write('aiverse_add_meta_service(selected_rpc MODULES economy social)\n')
    config=root/'service.json'
    config.write_text(json.dumps(kits.definitions(kits.resolve(['economy','social']))))
    run(['cmake','-S',root,'-B',build])
    run(['cmake','--build',build,'--target','selected_rpc','-j','6'])
    with subprocess.Popen([str(build/'selected_rpc'),str(config)],stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE,text=True) as process:
        state=None
        for service,action,operation,args,authority in [
            ('profile','create','init',{},False),
            ('economy','buy','buy',{'id':'sword'},False),
            ('chat','send','chat',{'id':'global','text':'hello'},False),
            ('board','submit','score',{'id':'global','score':25},True),
            ('meta','view','',{},False)]:
            process.stdin.write(json.dumps(dict(state=state,player='p',authority=authority,
                now=100,request=dict(service=service,action=action,operation=operation,arguments=args)))+'\n')
            process.stdin.flush()
            response=json.loads(process.stdout.readline())
            assert response['code'] in (0,1),response
            state=response['state']
        assert response['data']['profile']['balances']['gold']==400
        assert response['data']['chats']['global'][0]['text']=='hello'
        assert response['data']['boards']['global'][0]['score']==25
        process.stdin.close()
        assert process.wait(timeout=10)==0
    symbols=run([nm,'-C',build/'selected_rpc'])
    for forbidden in ['modules::all(', 'modules::loot(', 'modules::clans(', 'Client::', 'Economy::claimDaily(']:
        assert forbidden not in symbols,forbidden
    link=(build/'CMakeFiles/selected_rpc.dir/link.txt').read_text()
    for key in modules:
        archive='libaiverse_meta_'+key.replace('-','_')+'.a'
        assert (archive in link)==(key in {'economy','social'}),(archive,link)
    for feature in requested:
        name=feature.replace('-','_');exe=build/('check_'+name)
        run(['cmake','--build',build,'--target',exe.name,'-j','6']);run([exe])
        symbols=run([nm,'-C',exe])
        expected=selected(feature)
        for key,definition in modules.items():
            symbol='aiverse::meta::modules::'+definition['register']+'('
            assert (symbol in symbols)==(key in expected),(feature,key,'registration leakage')
        assert 'aiverse::meta::modules::all(' not in symbols
        assert 'aiverse::meta::Client::' not in symbols
        assert 'aiverse::meta::Chat::' not in symbols if 'social' not in expected else True
        assert 'aiverse::meta::Economy::claimDaily(' not in symbols if 'progression' not in expected else True
        # CMake link commands prove libraries are absent even when the linker could dead-strip them.
        link=(build/'CMakeFiles'/('check_'+name+'.dir')/'link.txt').read_text()
        for key in modules:
            archive='libaiverse_meta_'+key.replace('-','_')+'.a'
            assert (archive in link)==(key in expected or key=='economy' or (key=='ratings' and feature=='matchmaking')),(feature,archive,link)
        print('Isolated:',feature,flush=True)
print(f'{len(requested)} policy selections verified; unused/default and header-only builds are empty')
