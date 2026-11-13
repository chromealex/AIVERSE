#!/usr/bin/env python3
"""Compile/link model LOD fade GLSL 330 and GLES 300 variants with glslangValidator."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
validator = shutil.which('glslangValidator')
if not validator:
    raise SystemExit('glslangValidator is required')
source = r'''
#include "engine/render/ModelShaders.h"
#include "engine/gfx/ColorSpace.h"
#include <fstream>
#include <string>
using namespace aiverse::render::shaders;
int main(){for(int es=0;es<2;++es)for(int shadow=0;shadow<2;++shadow)for(int mask=0;mask<2;++mask)for(int skin=0;skin<4;++skin){
 std::string name="shader_"+std::to_string(es)+std::to_string(shadow)+std::to_string(mask)+std::to_string(skin);
 std::string defs=es?"#version 300 es\nprecision highp float;\nprecision highp int;\n":"#version 330 core\n";
 defs+=aiverse::gfx::colorShaderPrefix(false,false);
 defs+="#define AIVERSE_INST 28\n#define AIVERSE_Q 2\n#define LODFADE 1\n";
 if(mask)defs+="#define ALPHAMASK 1\n";
 if(skin)defs+=skin==1?"#define SKINNED 1\n":skin==2?"#define BAKED 1\n":"#define MORPH 1\n";
 std::ofstream(name+".vert")<<defs<<(shadow?kModelShadowVS:kModelVS);
 std::ofstream(name+".frag")<<defs<<(shadow?kModelShadowFS:kModelFS);
}}
'''
with tempfile.TemporaryDirectory(prefix='model-lod-shaders-') as directory:
    tmp = Path(directory)
    (tmp/'generate.cpp').write_text(source)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(root), str(tmp/'generate.cpp'), '-o', str(tmp/'generate')], check=True)
    subprocess.run([str(tmp/'generate')], cwd=tmp, check=True)
    variants = sorted(tmp.glob('*.vert'))
    for vertex in variants:
        result = subprocess.run([validator, '-l', str(vertex), str(vertex.with_suffix('.frag'))], capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
    print(f'{len(variants)} linked GLSL 330 / GLES 300 model LOD variants passed')
