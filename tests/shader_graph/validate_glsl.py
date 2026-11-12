#!/usr/bin/env python3
"""Validate generated node statements in ES 3.00 and desktop GLSL 330 stage contexts.
Uses an editor's compiler, but performs no project mutations. This is syntax validation,
not an OpenGL driver/render test (editor_smoke.py exercises the host GPU backend).
"""
import argparse
import json
from pathlib import Path
import socket
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, required=True)
parser.add_argument('--validator', default='glslangValidator')
args = parser.parse_args()
connection = socket.create_connection(('127.0.0.1', args.port), timeout=15)
stream = connection.makefile('rb')

def call(name, arguments=None):
    connection.sendall((json.dumps({'id': 1, 'method': 'call', 'params': {
        'name': name, 'arguments': arguments or {}}}) + '\n').encode())
    result = json.loads(stream.readline())['result']
    if 'error' in result:
        raise RuntimeError(result['error'])
    return result

ops = call('shader_graph_catalog')['nodes']
count = 0
with tempfile.TemporaryDirectory(prefix='aiverse-glsl-') as directory:
    for domain in ('material', 'postfx', 'particle'):
        for op in ops:
            if domain not in op['domains']:
                continue
            for stage in ('frag', 'vert') if domain == 'material' and op['vertex'] else ('frag',):
                graph = {'version': 1, 'domain': domain, 'nodes': [
                    {'id': 'seed', 'op': 'constant', 'value': [.2, .4, .6, 1]},
                    {'id': 'test', 'op': op['id'], 'inputs': ['seed'] * op['inputs'],
                     'texture': 'textures/test.png', 'value': [.1, .2, .3, 1]}],
                    'outputs': {'color': 'test'} if stage == 'frag' else {'color': 'seed', 'vertexOffset': 'test'}}
                result = call('shader_graph_validate', {'graph': graph})
                statements = result['glsl' if stage == 'frag' else 'vertexGlsl']
                for version in ('300 es', '330 core'):
                    source = '#version ' + version + '\nprecision highp float;\nuniform sampler2D tex0;\n'
                    source += '#define SG_SAMPLE(uv) texture(tex0,uv)\n#define SG_TEXTURE0(uv) texture(tex0,uv)\n#define SG_TEXTURE_LOD0(uv,lod) textureLod(tex0,uv,lod)\n'
                    if stage == 'frag':
                        source += 'out vec4 color;\n'
                    source += 'void main(){vec2 sg_uv=vec2(0.5),sg_screen=sg_uv;vec4 sg_input=vec4(1),sg_color=vec4(1),sg_particle=vec4(0.5,1,0.2,0);float sg_time=1.0;vec3 sg_world=vec3(0),sg_object=vec3(0),sg_normal=vec3(0,1,0),sg_view=vec3(0,1,0);\n'
                    source += statements + ('color=sg_n1;' if stage == 'frag' else 'gl_Position=vec4(sg_n1.xyz,1.0);') + '\n}'
                    path = Path(directory) / ('node.' + stage)
                    path.write_text(source)
                    checked = subprocess.run([args.validator, str(path)], capture_output=True, text=True)
                    if checked.returncode:
                        raise RuntimeError(f'{domain}/{stage}/{op["id"]}/{version}:\n{checked.stdout}\n{checked.stderr}\n{source}')
                    count += 1
print(f'{count} GLSL stage compilations passed (ES 3.00 + desktop 330)')
