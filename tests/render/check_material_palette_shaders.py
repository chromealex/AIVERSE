#!/usr/bin/env python3
"""Compile/link material palette GLSL 330 and GLES 300 variants with glslangValidator."""
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
int main(){for(int es=0;es<2;++es)for(int q=0;q<3;++q)for(int gi=0;gi<3;++gi)for(int fade=0;fade<2;++fade)for(int inst=0;inst<2;++inst){
 std::string name="shader_"+std::to_string(es)+std::to_string(q)+std::to_string(gi)+std::to_string(fade)+std::to_string(inst);
 std::string defs=es?"#version 300 es\nprecision highp float;\nprecision highp int;\n":"#version 330 core\n";
 defs+=aiverse::gfx::colorShaderPrefix(false,false);
 defs+="#define MATERIAL_PALETTE 1\n#define AIVERSE_GIQ 2\n#define AIVERSE_INST "+std::to_string(inst?28:1)+"\n#define AIVERSE_Q "+std::to_string(q)+"\n";
 if(q)defs+="#define NORMALMAP 1\n";
 if(fade)defs+="#define LODFADE 1\n";
 if(gi)defs+=gi==1?"#define LIGHTMAP 1\n":"#define PROBES 1\n";
 std::ofstream(name+".vert")<<defs<<kModelVS;
 std::ofstream(name+".frag")<<defs<<kModelFS;
}}
'''
with tempfile.TemporaryDirectory(prefix='material-palette-shaders-') as directory:
    tmp = Path(directory)
    (tmp/'generate.cpp').write_text(source)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(root), str(tmp/'generate.cpp'), '-o', str(tmp/'generate')], check=True)
    subprocess.run([str(tmp/'generate')], cwd=tmp, check=True)
    variants = sorted(tmp.glob('*.vert'))
    for vertex in variants:
        result = subprocess.run([validator, '-l', str(vertex), str(vertex.with_suffix('.frag'))], capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(result.stdout + result.stderr)
    print(f'{len(variants)} linked GLSL 330 / GLES 300 material palette variants passed')
