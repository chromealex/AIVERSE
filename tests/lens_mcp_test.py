"""Exercise the actual stdio server and persistent camera authoring without a GUI."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

server, engine = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix='aiverse-lens-mcp-') as directory:
    root = Path(directory)
    (root / 'game.json').write_text('{"name":"LENS MCP test","rules":"rules.json"}')
    (root / 'rules.json').write_text('{"types":{"unit":{}}}')
    empty = {'version': 1, 'cameras': []}
    camera = {'id': 1, 'camera': {}, 'output': {'kind': 'texture', 'key': 42}}
    scene = {'version': 1, 'cameras': [camera]}
    def tool(name, **arguments):
        return ('tools/call', {'name': name, 'arguments': arguments})
    requests = [('initialize', {}), ('tools/list', {}),
                tool('open_game', game=directory), tool('lens_scene_get'),
                tool('lens_camera_set', camera=camera, expectedScene=empty),
                tool('lens_scene_get'),
                tool('lens_camera_delete', id=1, expectedScene=empty)]
    def run(requests):
        data = ''.join(json.dumps({'jsonrpc': '2.0', 'id': i, 'method': method, 'params': params}) + '\n'
                       for i, (method, params) in enumerate(requests))
        result = subprocess.run([server, '--no-editor', '--engine', engine], input=data,
                                text=True, capture_output=True, timeout=30, check=True)
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(replies) == len(requests), result.stdout
        assert all(reply['id'] == i and 'result' in reply for i, reply in enumerate(replies)), replies
        return [reply['result'] for reply in replies]
    def content(result):
        return json.loads(result['content'][0]['text'])
    replies = run(requests)
    names = {tool['name'] for tool in replies[1]['tools']}
    assert {'lens_scene_get', 'lens_scene_set', 'lens_scene_validate', 'lens_camera_set', 'lens_camera_delete'} <= names
    assert content(replies[2]).get('ok'), replies[2]
    assert content(replies[3])['scene'] == empty
    assert content(replies[4])['ok'], replies[4]
    assert content(replies[5])['scene'] == scene
    assert replies[6]['isError'], 'stale edit must propagate as an MCP error'
    assert json.loads((root/'cameras.json').read_text()) == scene
    reopened = run([tool('open_game', game=directory), tool('lens_scene_get'),
                    tool('lens_camera_delete', id=1, expectedScene=scene)])
    assert content(reopened[1])['scene'] == scene, 'new server did not load saved asset'
    assert content(reopened[2])['ok']
    assert json.loads((root/'cameras.json').read_text()) == empty
print('PASS LENS stdio MCP: discovery, open, write, stale-error, restart, delete')
