"""Warcraft III stage 2 generated fixtures (no copyrighted files)."""
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / 'tools/importers/warcraft3'
sys.path.insert(0, str(MOD))
import formats
import mapinfo
import placements
import studio


def w3i(version=25, name='TRIGSTR_001'):
    header = struct.pack('<III', version, 2, 1234)
    if version >= 27:
        header += struct.pack('<IIII', 1, 32, 5, 14800)
    header += b'\0'.join(x.encode() for x in (name, 'Test author', 'A description', '2 players')) + b'\0'
    header += struct.pack('<8f4iIII', *([0.0] * 8), *([0] * 4), 16, 24, 0)
    return header + b'L' + b'\0' * 80


def doo(skin=b'ATtr', version=8):
    return (b'W3do' + struct.pack('<III', version, 11, 1) + b'ATtr' +
            struct.pack('<i7f', 2, 128, 256, 384, math.pi/2, 1, 2, 3) +
            skin + struct.pack('<BBii', 2, 100, -1, 1) +
            struct.pack('<i4si', 1, b'I000', 75) + struct.pack('<i', 42) +
            struct.pack('<ii4siii', 0, 1, b'CS01', 1, 2, 3))


def units():
    prefix = (b'W3do' + struct.pack('<III', 8, 11, 2))
    def unit(rawcode, random_flag, extra):
        return (rawcode + struct.pack('<i7f', 1, 512, 640, 128, 270, 1, 1, 1) +
                b'\x01' + b'\0' * 4 + struct.pack('<i', 3) + b'\0' * 2 +
                struct.pack('<iiii', -1, -1, -1, 0) +
                struct.pack('<ifiiii', 0, -1.0, 1, 0, 0, 0) +
                struct.pack('<ii4s', 1, 0, b'I001') +
                struct.pack('<i4sii', 1, b'A000', 1, 2) +
                struct.pack('<i', random_flag) + extra +
                struct.pack('<iii', 4, -1, 101))
    return prefix + unit(b'hfoo', 0, b'') + unit(b'uDNR', 2, struct.pack('<i4si', 1, b'hrif', 100))


def objectdata(extended=False):
    return (struct.pack('<II', 2, 1) + b'hfoo' + b'hf01' +
            struct.pack('<I', 2) + b'unam' + struct.pack('<I', 3) +
            (struct.pack('<ii', 1, 0) if extended else b'') + b'My unit\0' + b'hf01' +
            b'uhpm' + struct.pack('<I', 0) +
            (struct.pack('<ii', 0, 0) if extended else b'') + struct.pack('<i', 750) + b'hf01' +
            struct.pack('<I', 0))


def terrain(width, height):
    header = (b'W3E!' + struct.pack('<I', 11) + b'L' + struct.pack('<II', 0, 1) +
              b'Ldrt' + struct.pack('<I', 1) + b'CLdi' +
              struct.pack('<IIff', width, height, 0.0, 0.0))
    tiles = []
    for y in range(height):
        for x in range(width):
            elevation = 0x2000 + 32 * (x + 10 * y)
            tiles.append(struct.pack('<HHBBB', elevation, 0x2000, 0, 0, 0x02))
    return header + b''.join(tiles)


class Stage2Tests(unittest.TestCase):
    def test_doodad_specials_and_skin(self):
        d = placements.doodads(doo())
        self.assertEqual(len(d['doodads']), 1)
        self.assertEqual(d['doodads'][0]['skinId'], 'ATtr')
        self.assertEqual(d['doodads'][0]['itemSets'][0][0]['chancePercent'], 75)
        self.assertEqual(d['specialDoodads'][0]['tilePositionZXY'], [1, 2, 3])
        no_skin = doo().replace(b'ATtr' + struct.pack('<BBii', 2, 100, -1, 1),
                                struct.pack('<BBii', 2, 100, -1, 1), 1)
        self.assertFalse(placements.doodads(no_skin)['hasSkinField'])
        with self.assertRaises(ValueError):
            placements.doodads(doo(version=13))

    def test_units_random_and_stats(self):
        d = placements.units(units())
        self.assertEqual(len(d['units']), 2)
        self.assertEqual(d['units'][0]['inventory'][0]['slot'], 0)
        self.assertEqual(d['units'][0]['abilities'][0]['id'], 'A000')
        self.assertEqual(d['units'][1]['randomSelection']['options'][0]['typeId'], 'hrif')
        self.assertEqual(d['units'][1]['owner'], 3)

    def test_doodad_nonfinite_rejected(self):
        b = bytearray(doo())
        struct.pack_into('<f', b, 20+4, math.nan)
        with self.assertRaisesRegex(ValueError, 'non-finite'):
            placements.doodads(bytes(b))

    def test_object_overrides_basic_extended_and_truncation(self):
        for ext, flag in (('w3u', False), ('w3a', True), ('w3d', True), ('w3q', True)):
            d = placements.overrides(objectdata(flag), ext)
            self.assertEqual(d['original'][0]['modifications'][0]['value'], 'My unit')
            self.assertEqual(d['original'][0]['modifications'][1]['value'], 750)
            self.assertEqual(d['extendedFields'], flag)
            with self.assertRaises(ValueError):
                placements.overrides(objectdata(flag)[:-1], ext)

    def test_mapinfo_header_versions(self):
        for version in (18, 25, 27, 28, 31, 33, 39):
            d = mapinfo.header(w3i(version))
            self.assertEqual(d['playableWidth'], 16)
            self.assertEqual(d['tileset'], 'L')
            self.assertEqual(d['gameVersion'] is None, version < 27)
            self.assertGreater(d['unparsedTailBytes'], 0)
        with self.assertRaisesRegex(ValueError, 'unsupported'):
            mapinfo.header(struct.pack('<I', 40))

    def test_studio_map_corners_transpose_and_units(self):
        t = formats.terrain(terrain(6, 5))
        result = studio.mapdoc(t, {'name': 'Map'}, {})
        self.assertEqual(result['size'], [4, 5])
        self.assertEqual(result['terrain']['corners'][0], 0)
        # Adjacent editor X is Warcraft original Y -> 10 tiles -> 80 WC3 height units.
        self.assertEqual(result['terrain']['corners'][1], 10)
        # Adjacent editor Z is Warcraft original X -> 8 WC3 height units.
        self.assertEqual(result['terrain']['corners'][5], 1)
        placements_doc = studio.placements(t, placements.doodads(doo()), placements.units(units()))
        self.assertEqual(len(placements_doc['placements']), 3)
        self.assertEqual(placements_doc['placements'][0]['position'], [2.0, 3.0, 1.0])
        self.assertAlmostEqual(placements_doc['placements'][0]['yawDegrees'], 90, places=3)

    def test_full_import_and_preview(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'TestMap.w3x'
            source.mkdir()
            (source / 'war3map.w3e').write_bytes(terrain(6, 5))
            (source / 'war3map.w3i').write_bytes(w3i())
            (source / 'war3map.wts').write_text('STRING 1\n{\nThis Map\n}\n')
            (source / 'war3map.doo').write_bytes(doo())
            (source / 'war3mapUnits.doo').write_bytes(units())
            (source / 'war3map.w3u').write_bytes(objectdata())
            result = subprocess.run([sys.executable, str(MOD / 'import.py'),
                                    '--source', str(source), '--out', str(root / 'game')],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            dest = root / 'game/content/Warcraft3/TestMap'
            info = json.loads((dest / 'mapinfo.json').read_text())
            self.assertEqual(info['name'], 'This Map')
            output = json.loads((dest / 'studio-map.json').read_text())
            self.assertEqual(output['size'], [4, 5])
            self.assertTrue(output['warcraft3']['previewOnly'])
            summary = json.loads((dest / 'import.json').read_text())
            self.assertFalse(summary['warnings'])
            self.assertEqual(summary['fileCount'], 6)
            self.assertEqual(summary['decoded']['war3map.w3u'], 'objects_w3u.json')

    def test_forward_compat_and_strict(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'New.w3x'
            source.mkdir()
            (source / 'war3map.doo').write_bytes(doo(version=13))
            result = subprocess.run([sys.executable, str(MOD / 'import.py'),
                                    '--source', str(source), '--out', str(root / 'game')],
                                    text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout)
            dest = root / 'game/content/Warcraft3/New'
            self.assertTrue((dest / 'war3map.doo').exists())
            self.assertEqual(len(json.loads((dest / 'import.json').read_text())['warnings']), 1)
            # A separate output project is required because previous import is protected.
            strict = subprocess.run([sys.executable, str(MOD / 'import.py'),
                                     '--source', str(source), '--out', str(root / 'strict'),
                                     '--strict'], capture_output=True, text=True)
            self.assertEqual(strict.returncode, 2)
            self.assertFalse((root / 'strict/content/Warcraft3/New').exists())


if __name__ == '__main__':
    unittest.main()

class StudioInstallTests(unittest.TestCase):
    def test_register_preview_and_reject_repeated_install(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            imported = root / 'content/Warcraft3/Example'
            imported.mkdir(parents=True)
            (root / 'game.json').write_text(json.dumps({'name': 'example', 'maps': []}))
            (imported / 'studio-map.json').write_text(json.dumps({
                'name': 'Custom map', 'size': [4, 4],
                'terrain': {'corners': [0]*25},
                'warcraft3': {'previewOnly': True}}))
            cmd = [sys.executable, str(MOD / 'install_map.py'), '--project', str(root),
                   '--imported', str(imported)]
            first = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stdout)
            self.assertTrue((root / 'maps/warcraft3_Example.json').is_file())
            self.assertEqual(json.loads((root / 'game.json').read_text())['maps'][0]['title'], 'Custom map')
            second = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(second.returncode, 2)
            self.assertEqual(len(json.loads((root / 'game.json').read_text())['maps']), 1)
