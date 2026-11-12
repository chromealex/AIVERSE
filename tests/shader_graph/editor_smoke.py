#!/usr/bin/env python3
"""Run against a separately launched editor with --window shader_graph --listen PORT.
Creates no project files. Tests actual GPU compilation in all three preview paths.
"""
import argparse
import base64
import json
from pathlib import Path
import socket
import time

parser = argparse.ArgumentParser()
parser.add_argument('--port', type=int, required=True)
parser.add_argument('--screenshots', type=Path)
parser.add_argument('--texture', help='Existing project-relative texture for sampler node checks')
args = parser.parse_args()
connection = socket.create_connection(('127.0.0.1', args.port), timeout=15)
connection.settimeout(30)
stream = connection.makefile('rb')

def call(name, arguments=None):
    connection.sendall((json.dumps({'id': 1, 'method': 'call', 'params': {
        'name': name, 'arguments': arguments or {}}}) + '\n').encode())
    result = json.loads(stream.readline())['result']
    if 'error' in result:
        raise RuntimeError(f'{name}: {result["error"]}')
    return result

def preview(graph):
    call('shader_graph_preview', {'graph': graph})
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        status = call('shader_graph_preview_status')
        if status['ready']:
            if status['diagnostic']:
                raise RuntimeError(status['diagnostic'])
            return
        time.sleep(.05)
    raise RuntimeError('preview did not render')

operations = call('shader_graph_catalog')['nodes']
counts = {}
for domain in ('material', 'postfx', 'particle'):
    count = 0
    for op in operations:
        if domain not in op.get('domains', ('material', 'postfx', 'particle')):
            continue
        if op['id'].startswith('sampleTexture') and not args.texture:
            print(f"SKIP {domain}/{op['id']}: pass --texture", flush=True)
            continue
        graph = {'version': 1, 'domain': domain, 'nodes': [
            {'id': 'seed', 'op': 'constant', 'value': [.2, .4, .6, 1], 'position': [20, 40]},
            {'id': 'test', 'op': op['id'], 'inputs': ['seed'] * op['inputs'],
             'texture': args.texture, 'value': [.1, .2, .3, 1], 'position': [220, 60]}],
             'outputs': {'color': 'test'}}
        print(f"checking {domain}/{op['id']}", flush=True)
        preview(graph)
        count += 1
    if args.screenshots:
        args.screenshots.mkdir(parents=True, exist_ok=True)
        result = call('view_screenshot')
        (args.screenshots / f'{domain}.png').write_bytes(base64.b64decode(result['png_base64']))
    counts[domain] = count
    print(f'{domain}: {count} GPU graphs passed', flush=True)
count = 0
for op in operations:
    if not op.get('vertex', False) or 'material' not in op.get('domains', []):
        continue
    if op['id'].startswith('sampleTexture') and not args.texture:
        continue
    graph = {'version': 1, 'domain': 'material', 'nodes': [
        {'id': 'seed', 'op': 'constant', 'value': [.1, .2, .3, 1]},
        {'id': 'test', 'op': op['id'], 'inputs': ['seed'] * op['inputs'],
         'texture': args.texture, 'value': [.1, .2, .3, 1]}],
         'outputs': {'color': 'seed', 'vertexOffset': 'test'}}
    print(f"checking vertex/{op['id']}", flush=True)
    preview(graph)
    count += 1
counts['vertex'] = count
print(f'GPU graphs passed: {counts}', flush=True)
