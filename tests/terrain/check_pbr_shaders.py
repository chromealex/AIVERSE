#!/usr/bin/env python3
"""Compile actual terrain GLSL permutations (tiers, detail, atlas, HDR)."""
from pathlib import Path
import subprocess,tempfile,shutil,os
root=Path(__file__).resolve().parents[2]
validator=shutil.which('glslangValidator')
if not validator:raise SystemExit('glslangValidator required')
source=r'''
#include "modules/rts/view/RtsTerrainShaders.h"
#include "engine/gfx/ColorSpace.h"
#include <fstream>
int main(){for(int es=0;es<2;++es)for(int tier=0;tier<3;++tier)for(int detail=0;detail<2;++detail)for(int atlas=0;atlas<2;++atlas)for(int hdr=0;hdr<2;++hdr){
std::string name="terrain_"+std::to_string(es)+std::to_string(tier)+std::to_string(detail)+std::to_string(atlas)+std::to_string(hdr);
std::string prefix=es?"#version 300 es\nprecision highp float;\nprecision highp int;\n":"#version 330 core\n";
prefix+=aiverse::gfx::colorShaderPrefix(false,hdr);
prefix+="#define TERRAIN_TIER "+std::to_string(tier)+"\n#define TERRAIN_DETAIL "+std::to_string(detail)+"\n#define TERRAIN_GL_NORMAL_ATLAS "+std::to_string(atlas)+"\n";
std::ofstream(name+".vert")<<prefix<<aiverse::game::shaders::kTerrainVS;
std::ofstream(name+".frag")<<prefix<<aiverse::game::shaders::kTerrainFS;
}}
'''
with tempfile.TemporaryDirectory(prefix='terrain-pbr-shaders-') as d:
 p=Path(d);(p/'generate.cpp').write_text(source)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-I',str(root),str(p/'generate.cpp'),'-o',str(p/'generate')],check=True)
 subprocess.run([str(p/'generate')],cwd=p,check=True)
 vertices=list(p.glob('*.vert'))
 for v in vertices:
  result=subprocess.run([validator,'-l',str(v),str(v.with_suffix('.frag'))],capture_output=True,text=True)
  if result.returncode:raise SystemExit(result.stdout+result.stderr)
 print('PASS:',len(vertices),'GLSL330/GLES300 terrain shader pairs')
