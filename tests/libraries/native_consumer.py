#!/usr/bin/env python3
"""Build an external native module with the actual engine CMake, then relocate its source export."""
import json
import re
import shutil
import subprocess
import tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def run(command):
    result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    if result.returncode:
        print(result.stdout)
        raise RuntimeError('command failed: '+str(command))

with tempfile.TemporaryDirectory(prefix='rts-native-libraries-') as temp:
    tmp=Path(temp); project=tmp/'game'; project.mkdir()
    (project/'game.json').write_text('{}')
    shutil.copytree(ROOT/'examples/native/libraries',project/'native')
    flags=['-DAIVERSE_BUILD_CLIENT=OFF','-DAIVERSE_BUILD_EDITOR=OFF','-DAIVERSE_BUILD_TESTS=OFF',
           '-DAIVERSE_BUILD_HEADLESS=OFF','-DAIVERSE_BUILD_SERVER=OFF','-DAIVERSE_BUILD_TOOLS=OFF']
    def build(engine,content,build_dir):
        run(['cmake','-S',str(engine),'-B',str(build_dir),'-DAIVERSE_CONTENT_DIR='+str(content),*flags])
        run(['cmake','--build',str(build_dir),'--target','aiverse_native_library_demo','--parallel','2'])
    build(ROOT,project,tmp/'build-external')
    print('External game native module compiled with selected per-skill libraries')
    # Use the real source-export directory list, so removing libraries from it breaks this test.
    code=(ROOT/'tools/aiversebuild/main.cpp').read_text()
    body=code.split('void exportSources(',1)[1].split('// Native plugins',1)[0]
    match=re.search(r'for \(const char\* d : \{([^}]+)\}\)',body)
    assert match, 'source export directory list not found'
    exported=tmp/'exported';exported.mkdir()
    for directory in re.findall(r'"([^"]+)"',match.group(1)):
        shutil.copytree(ROOT/directory,exported/directory)
    shutil.copytree(ROOT/'apps/client',exported/'apps/client')
    shutil.copy2(ROOT/'CMakeLists.txt',exported/'CMakeLists.txt')
    shutil.copytree(project/'native',exported/'native')
    # Original project is removed; content/native fallback must use the exported module.
    shutil.rmtree(project)
    content=tmp/'empty-content';content.mkdir();(content/'game.json').write_text('{}')
    build(exported,content,tmp/'build-export')
    print('Relocated source layout compiled without original project/native or engine include paths')
