#!/usr/bin/env python3
"""Real editor + stdio MCP integration, including Metal/GL preview images.
Run on a desktop host with a display. No external Python packages.
"""
import argparse
import base64
import json
import os
from pathlib import Path
from queue import Queue, Empty
from threading import Thread
import shutil
import socket
import subprocess
import tempfile
import time
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/pkg'))
from fetch_templates import default_dir as templates_dir  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = args.build.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    studio = sys.platform == 'darwin' or os.name == 'nt'
    if studio:
        name = Path('AiverseStudioHTML5.app/Contents/MacOS/AiverseStudioHTML5') if sys.platform == 'darwin' else Path('AiverseStudio.exe')
        directory = build / 'apps/studio-native'
        candidates = [directory / name] + [directory / config / name for config in ('Release', 'Debug', 'RelWithDebInfo')]
        editor = next((p for p in candidates if p.is_file()), None)
        if editor is None:
            parser.error(f'Build aiverse_studio first in {build}')
        mcp_binary = editor.with_name('aiversemcp.exe' if os.name == 'nt' else 'aiversemcp')
    else:
        editor = build / 'aiverse_editor'
        mcp_binary = build / 'aiversemcp'
    with tempfile.TemporaryDirectory(prefix='aiverse-modeling-mcp-', dir=output) as tmp:
        project = Path(tmp) / 'game'
        shutil.copytree(templates_dir() / 'showcase', project)
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        env = dict(os.environ, AIVERSE_AUDIO='off', AIVERSE_LANG='ru', AIVERSE_ENGINE_DIR=str(root),
                   AIVERSE_AGENT_LINK_DIR=str(Path(tmp) / 'agent-link'))
        with (output / 'editor.log').open('w') as log:
            arguments = ['--project', str(project), '--listen', str(port)]
            if not studio:
                arguments.extend(['--engine', str(root), '--window', 'modeling'])
            gui = subprocess.Popen([str(editor), *arguments], env=env, stdout=log, stderr=log)
            mcp = None
            try:
                deadline = time.monotonic() + 40
                while True:
                    if gui.poll() is not None:
                        raise RuntimeError('editor exited before connection; inspect editor.log')
                    try:
                        with socket.create_connection(('127.0.0.1', port), timeout=.5):
                            break
                    except OSError:
                        if time.monotonic() > deadline:
                            raise TimeoutError('editor connection')
                        time.sleep(.2)
                mcp = subprocess.Popen([str(mcp_binary), '--engine', str(root),
                                        '--port', str(port), '--no-editor'], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=log, env=env, text=True, bufsize=1)
                replies = Queue()
                def read_replies():
                    for line in mcp.stdout:
                        replies.put(line)
                    replies.put('')
                Thread(target=read_replies, daemon=True).start()
                serial = 0

                def rpc(method, params):
                    nonlocal serial
                    serial += 1
                    mcp.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': serial,
                                               'method': method, 'params': params}) + '\n')
                    mcp.stdin.flush()
                    try:
                        line = replies.get(timeout=40)
                    except Empty:
                        raise TimeoutError(method) from None
                    if not line:
                        raise RuntimeError(f'MCP exited during {method}')
                    reply = json.loads(line)
                    assert reply['id'] == serial, reply
                    assert 'error' not in reply, reply
                    return reply['result']

                def tool(name, params, expect_error=False, image=None):
                    captured = False
                    result = rpc('tools/call', {'name': name, 'arguments': params})
                    assert bool(result.get('isError')) == expect_error, result
                    text = {}
                    for item in result['content']:
                        if item['type'] == 'text':
                            text = json.loads(item['text'])
                        if item['type'] == 'image' and image:
                            data = base64.b64decode(item['data'])
                            assert data.startswith(b'\x89PNG'), 'invalid preview'
                            (output / image).write_bytes(data)
                            captured = True
                    if image:
                        assert captured, f'{name} returned no rendered image'
                    return text

                rpc('initialize', {'protocolVersion': '2025-06-18', 'capabilities': {},
                                   'clientInfo': {'name': 'modeling-test', 'version': '1'}})
                names = {t['name'] for t in rpc('tools/list', {})['tools']}
                assert {'modeling_create', 'modeling_preview', 'model_info', 'animation_bake'} <= names
                created = tool('modeling_create', {'name': 'robot', 'preset': 'robot'})
                assert created['revision'] == 1
                tool('modeling_apply', {'name': 'robot', 'expectedRevision': 1, 'type': 'footman',
                                       'replaceView': True})
                metadata = tool('modeling_preview', {'name': 'robot', 'expectedRevision': 1,
                                                    'clip': 'Walk', 'time': .25}, image='walk.png')
                assert metadata['revision'] == 1
                tool('modeling_edit', {'name': 'robot', 'expectedRevision': 1, 'commands': [
                    {'op': 'parameters', 'values': {'legLength': 1.0}}]})
                tool('modeling_edit', {'name': 'robot', 'expectedRevision': 1, 'commands': []}, True)
                tool('modeling_preview', {'name': 'robot', 'expectedRevision': 1}, True)
                metadata = tool('animation_preview', {'name': 'robot', 'expectedRevision': 2,
                                                      'clip': 'Death', 'time': .85,
                                                      'skeleton': True}, image='death.png')
                assert metadata['revision'] == 2
                assert (output / 'walk.png').read_bytes() != (output / 'death.png').read_bytes()
                tool('editor_window', {'id': 'animation_authoring', 'open': True})
                tool('view_screenshot', {'window': 'all'}, image='timeline.png')
                graph = {'nodes': [{'id': 'idle', 'kind': 'clip', 'clip': 'Idle'},
                                   {'id': 'walk', 'kind': 'clip', 'clip': 'Walk'},
                                   {'id': 'blend', 'kind': 'blend', 'a': 'idle', 'b': 'walk', 'weight': .7}],
                         'output': 'blend'}
                tool('modeling_graph_set', {'name': 'robot', 'expectedRevision': 2, 'graph': graph})
                sampled = tool('animation_sample', {'name': 'robot', 'graph': graph, 'time': .3})
                assert len(sampled['nodes']) == 34
                tool('animation_preview', {'name': 'robot', 'expectedRevision': 3,
                                           'time': .3}, image='graph-pose.png')
                tool('modeling_graph_publish', {'name': 'robot', 'expectedRevision': 3})
                tool('animation_editor_open', {'name': 'robot'})
                tool('view_screenshot', {'window': 'all'}, image='graph-editor.png')
                print('MCP discovery, create/apply/edit, stale revisions, graph sampling and five real screenshots passed')
            finally:
                if mcp:
                    mcp.terminate()
                    mcp.wait(timeout=10)
                gui.terminate()
                try:
                    gui.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    gui.kill()
                    gui.wait()


if __name__ == '__main__':
    main()
