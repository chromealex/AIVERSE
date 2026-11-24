"""Warcraft III native entity install/SLK fixtures, all synthetic and self-contained."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
IMPORT = ROOT / 'tools/importers/warcraft3'
sys.path.insert(0, str(IMPORT))
from resolver import slk_rows, safe_relative, resolve, skin_rows
from placements import doodads
from formats import strings
from test_warcraft3_stage2 import terrain, units, doo


def slk(rows):
    records = ['ID;PWXL;N;E', 'B;X3;Y%d;D0' % (len(rows)+1)]
    for y, row in enumerate([['unitUIID', 'file', 'name']] + rows, 1):
        for x, text in enumerate(row, 1):
            records.append('C;X%d;Y%d;K"%s"' % (x, y, text.replace('"', '""')))
    records.append('E')
    return ('\n'.join(records) + '\n').encode()


def make_project(root, with_model=True):
    game = root / 'game'
    game.mkdir()
    (game / 'game.json').write_text(json.dumps({'name': 'WC3 test', 'module': 'game',
                                               'rules': 'rules.json', 'maps': []}))
    (game / 'rules.json').write_text(json.dumps({'terrain': {'model': 'heightfield'},
                                                'types': {'existing': {'name': 'existing'}}}))
    source = root / 'Fixture.w3x'
    source.mkdir()
    (source / 'war3map.w3e').write_bytes(terrain(6, 5))
    (source / 'war3mapUnits.doo').write_bytes(units())
    (source / 'war3map.doo').write_bytes(doo())
    if with_model:
        (source / 'MyModels').mkdir()
        (source / 'MyModels/tree.mdx').write_bytes(b'MDLX')
        # Override doodad ATtr model with a map-local path.
        (source / 'war3map.w3d').write_bytes(
            struct.pack('<II', 2, 1) + b'ATtr' + b'\0'*4 + struct.pack('<I', 1) +
            b'dmdl' + struct.pack('<Iii', 3, 0, 0) + b'MyModels/tree.mdx\0' +
            b'ATtr' + struct.pack('<I', 0))
    process = subprocess.run([sys.executable, str(IMPORT / 'import.py'),
                              '--source', str(source), '--out', str(game)],
                             text=True, capture_output=True)
    if process.returncode != 0:
        raise AssertionError(process.stdout + process.stderr)
    extracted = game / 'content/Warcraft3/Fixture'
    return game, extracted


class Stage3Tests(unittest.TestCase):
    def test_one_command_import_assets_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'NewMap.w3x'
            source.mkdir()
            (source / 'war3map.w3e').write_bytes(terrain(6, 5))
            game = root / 'game'
            game.mkdir()
            proc = subprocess.run([sys.executable, str(IMPORT / 'cli.py'),
                                   '--source', str(source), '--project', str(game),
                                   '--assets-only'], text=True, capture_output=True)
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            self.assertEqual(json.loads(proc.stdout)['phase'], 'assets')
            self.assertTrue((game / 'content/Warcraft3/NewMap/studio-map.json').exists())

    def test_reforged_skin_model_reference(self):
        parsed = skin_rows(b'[hfoo]\nfile=Units\\Human\\Footman\\HD.mdx\n'
                           b'[hrif]\nfile=Units\\Human\\Rifleman.mdx\n')
        self.assertEqual(parsed['hfoo'], 'Units/Human/Footman/HD.mdx')
        self.assertEqual(parsed['hrif'], 'Units/Human/Rifleman.mdx')

    def test_wts_inline_braces_and_comments(self):
        value = b'// preface\nSTRING 7\n{\nHello {inside}\n// keep this\n  spaced\n}\n'
        self.assertEqual(strings(value)['strings']['TRIGSTR_007'],
                         'Hello {inside}\n// keep this\n  spaced')
        with self.assertRaisesRegex(ValueError, 'unterminated'):
            strings(value[:-2])

    def test_doodad_3_0_v13_extension(self):
        data = (b'W3do' + struct.pack('<III', 13, 11, 1) + b'ATtr' +
                struct.pack('<i7f', 5, 128, 256, 384, 1.0, 1.0, 1.0, 1.0) +
                b'ATtr' + struct.pack('<I', 0xFFFFFFFF) +
                struct.pack('<BBii', 2, 100, 0, 0) +
                struct.pack('<i4i', 47, 10, 11, 12, 13) + struct.pack('<ii', 0, 0))
        result = doodads(data)
        self.assertEqual(result['version'], 13)
        self.assertEqual(result['doodads'][0]['v13ColorRaw'], 0xFFFFFFFF)
        self.assertEqual(result['doodads'][0]['v13TailRaw'], [10, 11, 12, 13])
        with self.assertRaises(ValueError):
            doodads(data[:-4])

    def test_sylk_with_quotes_and_repeat_columns(self):
        data = slk([['hfoo', 'Units\\Human\\Footman\\Footman.mdx', 'Footman'],
                    ['hrif', '', 'Rifle; man']])
        rows = slk_rows(data)
        self.assertEqual(rows[0]['unituiid'], 'hfoo')
        self.assertEqual(rows[1]['name'], 'Rifle; man')
        self.assertIsNone(safe_relative('../outside.mdx'))
        self.assertIsNone(safe_relative('C:\\abs.mdx'))

    def test_resolve_local_and_game_models(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project, imported = make_project(root)
            gd = root / 'gamedata'
            (gd / 'Units').mkdir(parents=True)
            (gd / 'Units/UnitUI.slk').write_bytes(slk([['hfoo', 'Units\\Human\\Footman\\Footman.mdx', 'Footman']]))
            folder = gd / 'Units/Human/Footman'
            folder.mkdir(parents=True)
            texname = b'Textures\\Footman.blp'
            record = struct.pack('<I', 0) + texname + bytes(260-len(texname)) + struct.pack('<I', 0)
            (folder / 'Footman.mdx').write_bytes(b'MDLX' + b'TEXS' + struct.pack('<I', 268) + record)
            (gd / 'Textures').mkdir()
            (gd / 'Textures/Footman.blp').write_bytes(b'BLP1')
            report = resolve(imported, gd)
            self.assertEqual(report['resolvedCount'], 2)
            self.assertEqual(report['unresolvedCount'], 1)  # random uDNR lacks a model
            self.assertEqual(len(report['types']), 2)
            self.assertEqual(len(report['copyPlan']), 2)
            self.assertIn('wc3_Fixture_unit_68666f6f', report['types'])
            self.assertIn('wc3_Fixture_doodad_41547472', report['types'])
            (gd / 'README').write_text('not imported')
            command = [sys.executable, str(IMPORT / 'install_scene.py'), '--project', str(project),
                       '--imported', str(imported), '--game-data', str(gd)]
            dry = subprocess.run(command + ['--dry-run'], text=True, capture_output=True)
            self.assertEqual(dry.returncode, 0, dry.stdout + dry.stderr)
            self.assertFalse((project / 'maps').exists())
            run = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            scene = json.loads((project / 'maps/warcraft3_Fixture.json').read_text())
            self.assertEqual(len(scene['entities']), 2)
            self.assertEqual(scene['warcraft3']['unresolvedCount'], 1)
            rules = json.loads((project / 'rules.json').read_text())
            self.assertIn('warcraft3_Fixture.json', rules['include'])
            newtypes = json.loads((project / 'warcraft3_Fixture.json').read_text())['types']
            self.assertIn('content/Warcraft3/Fixture/_game/Units/Human/Footman/Footman.mdx',
                          newtypes['wc3_Fixture_unit_68666f6f']['view']['model'])
            self.assertTrue((imported / '_game/Textures/Footman.blp').exists())
            repeat = subprocess.run(command, text=True, capture_output=True)
            self.assertEqual(repeat.returncode, 2)
            self.assertEqual(len(json.loads((project / 'game.json').read_text())['maps']), 1)

    def test_cross_map_type_ids_differ(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project, imported = make_project(root)
            second = imported.parent / 'Second'
            import shutil
            shutil.copytree(imported, second)
            a = resolve(imported, model_map={'ATtr': 'MyModels/tree.mdx'})
            b = resolve(second, model_map={'ATtr': 'MyModels/tree.mdx'})
            self.assertNotEqual(set(a['types']), set(b['types']))

    def test_strict_refuses_unresolved_without_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project, imported = make_project(root, with_model=False)
            proc = subprocess.run([sys.executable, str(IMPORT / 'install_scene.py'),
                                   '--project', str(project), '--imported', str(imported), '--strict'],
                                  text=True, capture_output=True)
            self.assertEqual(proc.returncode, 2)
            self.assertFalse((project / 'maps').exists())
            self.assertEqual(json.loads((project / 'game.json').read_text())['maps'], [])

    def test_manual_model_map_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            project, imported = make_project(root, with_model=False)
            report = resolve(imported, model_map={'hfoo': '../escape.mdx'})
            self.assertEqual(report['resolvedCount'], 0)


if __name__ == '__main__':
    unittest.main()
