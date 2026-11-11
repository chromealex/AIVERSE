#!/usr/bin/env python3
import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/libraries'))
import native_codegen as gen
import project_checks as checks
spec=json.loads((ROOT/'examples/libraries/toolkit/schema.json').read_text())
with tempfile.TemporaryDirectory(prefix='rts-toolkit-') as tmp:
    tmp=Path(tmp);native=tmp/'native';gen.write(spec,native)
    try:gen.write(spec,native)
    except ValueError:pass
    else:raise AssertionError('overwrote module')
    code='''#include "State.h"
#include "Commands.h"
int main(){
 aiverse::game::NativeState raw;if(!generated_demo::initialize(raw))return 1;
 auto state=generated_demo::load(raw);if(!state||state->credits!=100)return 2;
 state->credits=-1;if(generated_demo::store(raw,*state)||raw.values["credits"]!=100)return 3;
 aiverse::game::NativeState old;old.values={{"schema_version",1},{"gold",77}};
 if(!generated_demo::migrate(old)||generated_demo::load(old)->credits!=77)return 4;
 aiverse::game::NativeState bad;bad.values={{"schema_version",1},{"gold",-1}};auto before=bad.values;
 if(generated_demo::migrate(bad)||bad.values!=before)return 5;
 auto cmd=generated_demo::encode(1,generated_demo::build{2});if(!cmd||!generated_demo::decode_build(*cmd))return 6;
 cmd->args.push_back(1);if(generated_demo::decode_build(*cmd))return 7;
 if(generated_demo::encode(1,generated_demo::build{99}))return 8;
 auto finish=generated_demo::encode(1,generated_demo::finish{});if(!finish||!generated_demo::dispatch(*finish,[](auto){return true;}))return 9;
 return 0;
}'''
    (native/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT),'-I'+str(ROOT/'libraries/include'),str(native/'test.cpp'),'-o',str(tmp/'check')],check=True)
    subprocess.run([str(tmp/'check')],check=True)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT),str(native/'Smoke.cpp'),'-o',str(tmp/'smoke')],check=True)
    subprocess.run([str(tmp/'smoke')],check=True)
    subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror','-pedantic','-fsyntax-only','-I'+str(ROOT),str(native/'Module.cpp')],check=True)
    for name in ['raw','class','__bad','a-b']:
        invalid=copy.deepcopy(spec);invalid['fields'][0]['name']=name
        try:gen.generate(invalid)
        except ValueError:pass
        else:raise AssertionError(name)
    invalid=copy.deepcopy(spec);invalid['fields'][0]['default']=True
    try:gen.generate(invalid)
    except ValueError:pass
    else:raise AssertionError('bool accepted as integer')
    document={'types':{'farm':{},'worker':{'builds':['farm','missing']}}}
    rules=[{'source':'/types/*/builds/*','target':'/types/@keys'}]
    report=checks.reference_report(document,rules,tmp);assert not report['ok'] and report['checked']==2
    assert not checks.reference_report({'path':'../escape'},[{'source':'/path','kind':'file'}],tmp)['ok']
    first=[{'tick':0,'modules':{'game':{'gold':1}}},{'tick':1,'modules':{'game':{'gold':2}}}]
    second=copy.deepcopy(first);second[1]['modules']['game']['gold']=3
    result=checks.trace_diff(first,second);assert result['tick']==1 and result['difference']['path']=='/game/gold'
    assert checks.trace_diff(first,first)['ok']
    engine=tmp/'engine';(engine/'libraries').mkdir(parents=True);(engine/'libraries/CMakeLists.txt').write_text('test')
    (engine/'libraries/a.h').write_text('test')
    catalog={'packages':[{'id':'a','version':'1','layer':'tools','source':'libraries/a.h'}]}
    lock={'schema':2,'packages':{'a':{'version':'0','files':{}}}}
    assert checks.upgrade_plan(lock,catalog,engine)['reviewRequired']
    # Build the generated native module via actual engine CMake in an external project.
    project=tmp/'game';project.mkdir();(project/'game.json').write_text('{}');(project/'native').mkdir()
    import shutil
    for file in native.iterdir():shutil.copy2(file,project/'native'/file.name)
    flags=['-DAIVERSE_BUILD_CLIENT=OFF','-DAIVERSE_BUILD_EDITOR=OFF','-DAIVERSE_BUILD_TESTS=OFF','-DAIVERSE_BUILD_TOOLS=OFF','-DAIVERSE_BUILD_HEADLESS=OFF','-DAIVERSE_BUILD_SERVER=OFF']
    subprocess.run(['cmake','-S',str(ROOT),'-B',str(tmp/'build'),'-DAIVERSE_CONTENT_DIR='+str(project),*flags],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['cmake','--build',str(tmp/'build'),'--target','aiverse_native_generated_demo','--parallel','2'],check=True)
print('Generator, typed state/commands, references, trace diff, upgrades and external native build: OK')
