"""Exercise real MCP stdio against isolated RTS/FPS fixtures. All output under --output."""
import argparse
import json
import select
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    with (root / 'server.log').open('w') as log:
        server = subprocess.Popen([str(args.binary.resolve()), '--engine', str(root), '--no-editor', '--port', '0'],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1)
        seq = 0
        def request(method, params):
            nonlocal seq
            seq += 1
            server.stdin.write(json.dumps(dict(jsonrpc='2.0', id=seq, method=method, params=params)) + '\n')
            server.stdin.flush()
            if not select.select([server.stdout], [], [], 30)[0]:
                raise RuntimeError('MCP timeout: ' + method)
            reply = json.loads(server.stdout.readline())
            assert reply.get('id') == seq and 'error' not in reply, reply
            return reply['result']
        def call(name, params=None, error=False):
            result = request('tools/call', dict(name=name, arguments=params or {}))
            assert bool(result.get('isError')) == error, (name, result)
            return json.loads(result['content'][0]['text'])
        try:
            request('initialize', dict(protocolVersion='2025-06-18', capabilities={}, clientInfo=dict(name='entity-test', version='1')))
            required = {'scene_entity_list', 'scene_entity_get', 'scene_component_set', 'scene_light_create',
                        'scene_light_delete', 'scene_render_lighting_get', 'scene_render_lighting_set'}
            assert required <= {tool['name'] for tool in request('tools/list', {})['tools']}
            call('scene_entity_list', error=True)
            for genre in ('rts', 'fps'):
                project = root / 'templates' / genre
                project.mkdir(parents=True, exist_ok=True)
                (project / 'game.json').write_text(json.dumps(dict(name='MCP fixture', module=genre, maps=[dict(file='map.json')])))
                (project / 'rules.json').write_text(json.dumps(dict(fps={}, types=dict(unit=dict(health=dict(max=100))))))
                document = dict(name='Fixture', size=[8, 8]) if genre == 'rts' else dict(name='Fixture', colliders=[], spawns=[[0, 1, 0]], players=[dict(name='P1', team=1)])
                (project / 'map.json').write_text(json.dumps(document))
                call('open_game', dict(game=str(project)))
                first = call('scene_light_create', dict(name='First', position=[2, 3, 4], light=dict(intensity=6, color=[1, .5, .2])))['entityId']
                second = call('scene_light_create', dict(name='Second'))['entityId']
                assert first != second
                item = call('scene_entity_get', dict(entityId=first))
                assert item['stableId'] and {c['id'] for c in item['components']} == {'Transform', 'Light'}
                original = item
                call('scene_component_set', dict(entityId=first, componentId='Transform', patch=dict(position=[1, 2, 3], intensity=9)), error=True)
                call('scene_component_set', dict(entityId=first, componentId='Transform', patch=dict(position=[1, 2, 3], scale=[2, 2, 2])), error=True)
                call('scene_component_set', dict(entityId=first, componentId='Light', patch=dict(radius=-1)), error=True)
                assert call('scene_entity_get', dict(entityId=first)) == original
                call('scene_component_set', dict(entityId=first, componentId='Transform', patch=dict(position=[5, 2, 6])))
                call('scene_component_set', dict(entityId=first, componentId='Light', patch=dict(intensity=8)))
                def intensity():
                    return next(c['data']['intensity'] for c in call('scene_entity_get', dict(entityId=first))['components'] if c['id'] == 'Light')
                assert intensity() == 8
                call('undo'); assert intensity() == 6
                call('redo'); assert intensity() == 8
                call('scene_light_delete', dict(entityId=first))
                call('scene_entity_get', dict(entityId=first), error=True)
                assert call('scene_entity_get', dict(entityId=second))['name'] == 'Second'
                call('scene_render_lighting_set', dict(patch=dict(indirect='auto', direct='luma')))
                settings = call('scene_render_lighting_get')
                call('scene_render_lighting_set', dict(patch=dict(probes=3)), error=True)
                assert call('scene_render_lighting_get') == settings
                call('map_save')
                disk = json.loads((project / 'map.json').read_text())
                assert len(disk['lights']) == 1 and 'light:' + disk['lights'][0]['entityId'] == second
                call('open_game', dict(game=str(project)))
                assert call('scene_entity_get', dict(entityId=second))['name'] == 'Second'
                assert call('scene_render_lighting_get') == settings
                print(genre + ': MCP entity/component/GI/save/reopen PASS', flush=True)
            print('Scene entity MCP PASS', flush=True)
        finally:
            server.stdin.close()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.terminate()
                server.wait(timeout=10)


if __name__ == '__main__':
    main()
