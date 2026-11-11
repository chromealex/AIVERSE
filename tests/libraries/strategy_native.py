#!/usr/bin/env python3
"""Execute six libraries in a game-owned NativeModule, then repeat from relocated sources."""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/libraries'))
from gameplay import read_json
WORK=ROOT/'build/.temp/strategy-native'
WORK.mkdir(parents=True,exist_ok=True)
def run(args):
    result=subprocess.run([str(a) for a in args],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    if result.returncode:
        print(result.stdout)
        raise RuntimeError('command failed: '+' '.join(str(a) for a in args))
    return result.stdout
def project(path):
    if path.exists():shutil.rmtree(path)
    shutil.copytree(ROOT/'templates/skirmish',path)
    shutil.copytree(ROOT/'examples/native/strategy',path/'native')
    rules=read_json(path/'rules.json')
    rules['nativeModules']=[{'id':'strategy_demo','command':32768,'config':{'rate':1}}]
    (path/'rules.json').write_text(json.dumps(rules))
def build(engine,content,dest):
    flags=['-DAIVERSE_BUILD_CLIENT=OFF','-DAIVERSE_BUILD_EDITOR=OFF','-DAIVERSE_BUILD_TESTS=OFF','-DAIVERSE_BUILD_HEADLESS=OFF','-DAIVERSE_BUILD_SERVER=OFF','-DAIVERSE_BUILD_TOOLS=OFF']
    run(['cmake','-S',engine,'-B',dest,'-DCMAKE_BUILD_TYPE=Release','-DAIVERSE_CONTENT_DIR='+str(content),*flags])
    run(['cmake','--build',dest,'--target','aiverse_strategy_native_probe','--parallel','6'])
    binaries=list(dest.rglob('aiverse_strategy_native_probe'))
    assert len(binaries)==1,binaries
    print(run([binaries[0],content]),flush=True)

game=WORK/'original-game';project(game)
build(ROOT,game,WORK/'build-external')
body=(ROOT/'tools/aiversebuild/main.cpp').read_text().split('void exportSources(',1)[1].split('// Native plugins',1)[0]
match=re.search(r'for \(const char\* d : \{([^}]+)\}\)',body);assert match
exported=WORK/'exported'
if exported.exists():shutil.rmtree(exported)
exported.mkdir()
for directory in re.findall(r'"([^"]+)"',match.group(1)):shutil.copytree(ROOT/directory,exported/directory)
shutil.copytree(ROOT/'apps/client',exported/'apps/client')
shutil.copy2(ROOT/'CMakeLists.txt',exported/'CMakeLists.txt')
shutil.copytree(game/'native',exported/'native')
content=WORK/'relocated-game'
if content.exists():shutil.rmtree(content)
shutil.copytree(game,content,ignore=shutil.ignore_patterns('native'))
shutil.rmtree(game)
build(exported,content,WORK/'build-export')
print('Native integration and relocated source-export execution passed.',flush=True)
