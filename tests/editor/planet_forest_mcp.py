"""End-to-end authored planetary forest MCP contract on an isolated two-body map."""
import argparse
import json
import select
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--engine', required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='aiverse-planet-forest-mcp-') as tmp:
        root = Path(tmp)
        project = root / 'fixture'
        (project / 'maps').mkdir(parents=True)
        def write(path, data):
            path.write_text(json.dumps(data), encoding='utf-8')
        write(project / 'game.json', dict(name='Forest MCP fixture', module='game',
            rules='rules.json', maps=[dict(file='maps/scene.json')]))
        write(project / 'rules.json', dict(types=dict(tree=dict(health=dict(max=100))),
            nativeModules=[dict(id='world_processes', config=dict())]))
        write(project / 'maps/scene.json', dict(players=[dict(name='Blue', team=1)],
            world=dict(start='terra', bodies=[
                dict(id='terra', kind='planet', radius=64, faceSize=8),
                dict(id='moon', kind='moon', radius=32, faceSize=4)]),
            entities=[dict(id='marker', type='tree', owner=0,
                at=dict(body='terra', face=0, x=3, y=4))]))
        with (root / 'server.log').open('w') as log:
            proc = subprocess.Popen([str(args.binary.resolve()), '--engine', str(args.engine.resolve()),
                '--no-editor', '--port', '0'], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=log, text=True, bufsize=1)
            seq = 0
            def request(method, params):
                nonlocal seq
                seq += 1
                proc.stdin.write(json.dumps(dict(jsonrpc='2.0', id=seq, method=method, params=params)) + '\n')
                proc.stdin.flush()
                if not select.select([proc.stdout], [], [], 45)[0]:
                    raise RuntimeError('MCP timeout: ' + method)
                response = json.loads(proc.stdout.readline())
                assert response.get('id') == seq and 'error' not in response, response
                return response['result']
            def call(name, params=None, error=False):
                result = request('tools/call', dict(name=name, arguments=params or {}))
                assert bool(result.get('isError')) == error, (name, result)
                return json.loads(result['content'][0]['text'])
            def read():
                return call('planet_forest_get')
            def edit(name, **params):
                before = read()
                return call(name, dict(mapPath=before['mapPath'], expectedRevision=before['revision'], **params))
            def plants():
                return read()['forest']['plants']
            try:
                request('initialize', dict(protocolVersion='2025-06-18', capabilities={},
                    clientInfo=dict(name='planet-forest-test', version='1')))
                required = {'planet_forest_get', 'planet_forest_set', 'planet_forest_plant',
                    'planet_forest_move', 'planet_forest_remove'}
                assert required <= {t['name'] for t in request('tools/list', {})['tools']}
                call('planet_forest_get', error=True)
                call('open_game', dict(game=str(project)))
                call('map_open', dict(file='maps/scene.json'))
                assert read()['available'] and read()['moduleEnabled']
                configured = edit('planet_forest_set', forest=dict(species=[dict(id=1)], plants=[]),
                                  catalog=dict(species=[dict(id=1, preset='oak')]))
                assert configured['changed'] and configured['override']
                before = read()
                stale = dict(mapPath=before['mapPath'], expectedRevision=before['revision'],
                             species=1, at=dict(body='terra', face=0, x=2, y=3))
                created = call('planet_forest_plant', stale)
                key = created['forest']['plants'][0]['key']
                after = read()
                call('planet_forest_plant', stale, error=True)
                assert read() == after
                moved = edit('planet_forest_move', species=1, key=key,
                             at=dict(body='moon', face=1, x=2, y=1))
                assert moved['changed'] and plants()[0]['at']['body'] == 'moon'
                unchanged = edit('planet_forest_move', species=1, key=key,
                                 at=dict(body='moon', face=1, x=2, y=1))
                assert not unchanged['changed']
                call('undo')
                assert plants()[0]['at']['body'] == 'terra'
                call('redo')
                assert plants()[0]['at']['body'] == 'moon'
                marker = edit('planet_forest_plant', species=1, entity='marker')
                marker_key = next(p['key'] for p in marker['forest']['plants'] if p.get('entity') == 'marker')
                before = read()
                call('planet_forest_move', dict(mapPath=before['mapPath'],
                    expectedRevision=before['revision'], species=1, key=marker_key,
                    at=dict(body='terra', face=1, x=1, y=1)), error=True)
                assert read() == before
                edit('planet_forest_remove', species=1, key=marker_key)
                assert all(p['key'] != marker_key for p in plants())
                assert any(m['id'] == 'marker' for m in read()['markers'])
                call('undo')
                assert any(p['key'] == marker_key for p in plants())
                call('redo')
                assert all(p['key'] != marker_key for p in plants())
                saved = read()['forest']
                call('map_save')
                call('map_open', dict(file='maps/scene.json'))
                assert read()['forest'] == saved
                assert read()['catalog']['species'][0]['preset'] == 'oak'
                print('planet forest MCP authoring/validation/history/save-reopen PASS', flush=True)
            finally:
                proc.stdin.close()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.terminate()
                    proc.wait(timeout=10)


if __name__ == '__main__':
    main()
