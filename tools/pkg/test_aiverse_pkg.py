#!/usr/bin/env python3
"""Offline test of tools/pkg/aiverse_pkg.py: a local index (file paths), no network."""
import hashlib
import io
import json
import os
import sys
import tempfile
import zipfile
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import aiverse_pkg as pkg  # noqa: E402


def make_zip(path: Path, manifest: dict, files: dict) -> dict:
    with zipfile.ZipFile(path, 'w') as z:
        z.writestr(pkg.MANIFEST, json.dumps(manifest))
        for name, data in files.items():
            z.writestr(name, data)
    data = path.read_bytes()
    return {'url': str(path), 'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data),
            'minEngine': manifest.get('minEngine', '0.0.1')}


def run(*argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = pkg.main(['--json', *argv])
    text = out.getvalue()
    return code, json.loads(text) if text.strip() else None, err.getvalue()


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        os.environ['AIVERSE_PACKAGES_HOME'] = str(tmp / 'home')
        dist = tmp / 'dist'
        dist.mkdir()
        mats = {'schema': 1, 'id': 'demo-mats', 'kind': 'content', 'category': 'materials', 'title': 'Demo',
                'title-ru': 'Демо', 'license': 'MIT', 'minEngine': '0.0.1',
                'install': {'mount': 'materials/demo', 'mergeJson': [{'from': 'materials.json', 'into': 'materials.json'}]}}
        v1 = make_zip(dist / 'm1.zip', dict(mats, version='1.0.0'),
                      {'materials.json': '{"demo_a": {"r": 1}}', 'textures/a.png': b'png-a'})
        v2 = make_zip(dist / 'm2.zip', dict(mats, version='1.1.0'),
                      {'materials.json': '{"demo_a": {"r": 2}, "demo_b": {}}', 'textures/a.png': b'png-a2'})
        v3 = make_zip(dist / 'm3.zip', dict(mats, version='2.0.0'), {'materials.json': '{}'})
        src = {'schema': 1, 'id': 'demo-src', 'version': '1.0.0', 'kind': 'source', 'category': 'textures',
               'title': 'Src', 'title-ru': 'Исх', 'license': 'MIT'}
        s1 = make_zip(dist / 's1.zip', src, {'raw.png': b'raw'})
        tpl = {'schema': 1, 'id': 'demo-tpl', 'version': '1.0.0', 'kind': 'template', 'category': 'templates',
               'title': 'Template', 'title-ru': 'Шаблон', 'license': 'MIT'}
        t1 = make_zip(dist / 't1.zip', tpl, {'game.json': '// a sample game\n' + json.dumps(
            {'name': 'Tpl', 'url': 'https://x//y', 'packages': {'demo-mats': '^1.0'}}), 'maps/start.json': '{}'})
        tpl2 = dict(tpl, id='demo-tpl2', title='Community arena', provides={'maps': ['Arena']})
        t2 = make_zip(dist / 't2.zip', tpl2, {'game.json': '{"name": "C"}'})
        mdl = {'schema': 1, 'id': 'demo-models', 'version': '1.0.0', 'kind': 'content', 'category': 'models',
               'title': 'Models', 'title-ru': 'Модели', 'license': 'MIT', 'dependencies': {'demo-mats': '^1.0'},
               'install': {'mount': 'models/demo'}}
        md1 = make_zip(dist / 'md1.zip', mdl, {'a.gltf': '{"images": [{"uri": "../../materials/demo/textures/a.png"}]}'})
        bad = make_zip(dist / 'bad.zip', dict(src, id='demo-bad'), {'x': 'y'})
        bad['sha256'] = '0' * 64
        published = [(mats, 'materials', {'1.0.0': v1, '1.1.0': v2, '2.0.0': v3}), (src, 'textures', {'1.0.0': s1}),
                     (tpl, 'templates', {'1.0.0': t1}), (tpl2, 'templates', {'1.0.0': t2}),
                     (mdl, 'models', {'1.0.0': md1}),
                     (dict(src, id='demo-bad'), 'textures', {'1.0.0': bad})]
        cat = tmp / 'catalog'
        index = {'schema': 2, 'packages': []}
        for m, category, versions in published:
            folder = cat / category / m['id']
            folder.mkdir(parents=True)
            (folder / pkg.MANIFEST).write_text(json.dumps(dict(m, version=max(versions, key=pkg.parse_version),
                                                               tags=['pbr'] if m['id'] == 'demo-mats' else [])))
            (folder / 'versions.json').write_text(json.dumps({'id': m['id'], 'versions': versions,
                                                              'latest': max(versions, key=pkg.parse_version)}))
            index['packages'].append({'id': m['id'], 'category': category, 'path': f"{category}/{m['id']}",
                                      **({'official': True} if m['id'] == 'demo-tpl' else {})})
        (cat / 'index.json').write_text(json.dumps(index))
        os.environ['AIVERSE_PACKAGES_INDEX'] = str(cat / 'index.json')

        assert pkg.matches('1.4.0', '^1.2') and not pkg.matches('2.0.0', '^1.2')
        assert pkg.matches('1.2.9', '~1.2.3') and not pkg.matches('1.3.0', '~1.2.3')
        assert pkg.matches('0.2.5', '^0.2') and not pkg.matches('0.3.0', '^0.2')

        code, r, _ = run('search', 'pbr')
        assert code == 0 and [e['id'] for e in r['result']] == ['demo-mats'], r
        code, r, _ = run('search', '--category', 'textures')
        assert {e['id'] for e in r['result']} == {'demo-src', 'demo-bad'}, r

        code, r, _ = run('fetch', 'demo-src')
        assert code == 0 and (Path(r['result']) / 'raw.png').read_bytes() == b'raw'
        code, r, _ = run('fetch', 'demo-bad')
        assert code == 2 and 'sha256' in r['error'], r

        proj = tmp / 'proj'
        proj.mkdir()
        (proj / 'game.json').write_text('{"name": "P"}')
        (proj / 'materials.json').write_text('{"own": {}}')
        code, r, _ = run('add', 'demo-mats@^1.0', '--project', str(proj))
        assert code == 0 and r['result']['version'] == '1.1.0', r
        assert (proj / 'materials/demo/textures/a.png').read_bytes() == b'png-a2'
        assert set(json.loads((proj / 'materials.json').read_text())) == {'own', 'demo_a', 'demo_b'}
        game = json.loads((proj / 'game.json').read_text())
        assert game['packages'] == {'demo-mats': '^1.0'}, game
        lock = json.loads((proj / pkg.LOCK).read_text())
        assert lock['packages']['demo-mats']['version'] == '1.1.0'

        code, r, _ = run('verify', '--project', str(proj))
        assert code == 0 and r['result']['mismatch'] == 0, r
        (proj / 'materials/demo/textures/a.png').write_bytes(b'edited')
        code, r, _ = run('verify', '--project', str(proj))
        assert code == 1 and r['result']['packages'][0]['changed'] == 1, r

        # a fresh clone of the project restores exactly the locked version
        (proj / 'materials/demo/textures/a.png').unlink()
        code, r, _ = run('install', '--project', str(proj))
        assert code == 0 and (proj / 'materials/demo/textures/a.png').read_bytes() == b'png-a2'

        # project ids are never overwritten
        p2 = tmp / 'proj2'
        p2.mkdir()
        (p2 / 'game.json').write_text('{}')
        (p2 / 'materials.json').write_text('{"demo_a": {"mine": true}}')
        code, r, _ = run('add', 'demo-mats@1.0.0', '--project', str(p2))
        assert code == 2 and 'demo_a' in r['error'], r

        code, r, _ = run('add', 'demo-src', '--project', str(proj))
        assert code == 2 and 'fetch' in r['error'], r

        code, r, _ = run('remove', 'demo-mats', '--project', str(proj))
        assert code == 0 and not (proj / 'materials').exists() and not (proj / pkg.LOCK).exists()
        assert json.loads((proj / 'materials.json').read_text()) == {'own': {}}
        assert 'packages' not in json.loads((proj / 'game.json').read_text())

        # a model pack brings the material package it refers to
        p3 = tmp / 'proj3'
        p3.mkdir()
        (p3 / 'game.json').write_text('{}')
        code, r, _ = run('add', 'demo-models', '--project', str(p3))
        assert code == 0 and r['result']['dependencies'][0]['version'] == '1.1.0', r
        assert (p3 / 'models/demo/a.gltf').is_file() and (p3 / 'materials/demo/textures/a.png').is_file()
        assert set(json.loads((p3 / pkg.LOCK).read_text())['packages']) == {'demo-mats', 'demo-models'}
        code, r, _ = run('add', 'demo-models', '--project', str(p3))
        assert code == 0 and 'dependencies' not in r['result'], r

        # templates: official ones for the start screen, every one with --all, search, offline cache
        code, r, _ = run('templates')
        assert code == 0 and [e['id'] for e in r['result']] == ['demo-tpl'] and r['result'][0]['official'], r
        code, r, _ = run('templates', '--all')
        assert [e['id'] for e in r['result']] == ['demo-tpl', 'demo-tpl2'], r
        code, r, _ = run('templates', '--all', 'arena')
        assert [e['id'] for e in r['result']] == ['demo-tpl2'] and r['result'][0]['maps'] == ['Arena'], r
        code, r, _ = run('templates', '--cached')
        assert code == 0 and [e['id'] for e in r['result']] == ['demo-tpl'], r

        code, r, _ = run('create', 'demo-tpl', str(tmp / 'newgame'))
        assert code == 0, r
        ng = tmp / 'newgame'
        assert (ng / 'maps/start.json').is_file() and (ng / 'materials/demo/textures/a.png').is_file()
        g = json.loads((ng / 'game.json').read_text())
        assert g['template'] == 'demo-tpl' and g['url'] == 'https://x//y', g

        # offline: cached packages still resolve, new downloads fail clearly
        os.environ['AIVERSE_PACKAGES_OFFLINE'] = '1'
        os.environ['AIVERSE_PACKAGES_INDEX'] = 'https://invalid.example/index.json'
        code, r, _ = run('fetch', 'demo-src')
        assert code == 0, r
        del os.environ['AIVERSE_PACKAGES_OFFLINE']

        # skills name their packages
        refs = pkg.skill_packages('aiverse-terra-vegetation')
        assert 'vegetation-pbr' in refs and 'terrain-pbr' in refs, refs
    print('aiverse_pkg: ok')


if __name__ == '__main__':
    main()
