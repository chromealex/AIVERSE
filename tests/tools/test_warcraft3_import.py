"""Offline Warcraft III importer fixtures; no Blizzard assets or StormLib required."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FORMATS = ROOT / 'tools/importers/warcraft3/formats.py'
PLUGIN = ROOT / 'tools/unpackers/mpq/unpack.py'
IMPORTER = ROOT / 'tools/importers/warcraft3/import.py'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


wc3 = load('wc3_formats', FORMATS)
mpq = load('wc3_mpq', PLUGIN)


def w3e(version, packed, raw_ground=0x2151, raw_water=0x6200):
    return (b'W3E!' + struct.pack('<I', version) + b'L' +
            struct.pack('<II', 0, 1) + b'Ldrt' +
            struct.pack('<I', 1) + b'CLdi' +
            struct.pack('<IIff', 2, 2, -64.0, -64.0) +
            (struct.pack('<HH', raw_ground, raw_water) +
             struct.pack('<B' if version == 11 else '<H', packed) +
             bytes([0x84, 0x13])) * 4)


class Warcraft3Import(unittest.TestCase):
    def test_w3e_classic_fields(self):
        doc = wc3.terrain(w3e(11, 0x56))
        self.assertEqual(doc['width'], 2)
        self.assertEqual(doc['groundTilesets'], ['Ldrt'])
        self.assertEqual(doc['cliffTilesets'], ['CLdi'])
        self.assertEqual(doc['verticesRowMajorBottomLeft'][0],
                         [212.25, 38.4, 0x50, 6, 0x84, 1, 3, 0x6200, 0x50])

    def test_w3e_reforged_v12(self):
        doc = wc3.terrain(w3e(12, (0x50 << 2) | 33))
        self.assertEqual(doc['verticesRowMajorBottomLeft'][0][3], 33)
        self.assertEqual(doc['verticesRowMajorBottomLeft'][0][2], 0x50)
        self.assertEqual(doc['verticesRowMajorBottomLeft'][0][8], 0x140)

    def test_terrain_validation(self):
        with self.assertRaisesRegex(ValueError, 'length mismatch'):
            wc3.terrain(w3e(11, 0x56)[:-1])
        with self.assertRaisesRegex(ValueError, 'unsupported'):
            wc3.terrain(b'W3E!' + struct.pack('<I', 50))

    def test_pathing(self):
        doc = wc3.pathing(b'MP3W' + struct.pack('<III', 0, 2, 2) + b'\x00\x02\x08\x0a')
        self.assertEqual(doc['noWalkCells'], 2)
        self.assertEqual(doc['noBuildCells'], 2)
        with self.assertRaisesRegex(ValueError, 'truncated'):
            wc3.pathing(b'MP3W' + struct.pack('<III', 0, 2, 2) + b'\x00')

    def test_strings_imports(self):
        wts = wc3.strings(b'STRING 4\n{\nHello\n}\nSTRING 15\n{\nWorld\n}\n')
        self.assertEqual(wts['strings']['TRIGSTR_004'], 'Hello')
        imp = wc3.imported(struct.pack('<II', 1, 1) + b'\x0d' + b'Units\\Human\\Unit.mdx\0')
        self.assertEqual(imp['entries'][0]['path'], 'Units\\Human\\Unit.mdx')

    def test_mpq_magic_and_paths(self):
        self.assertFalse(mpq.valid_name('../x'))
        self.assertFalse(mpq.valid_name('C:\\x'))
        self.assertTrue(mpq.valid_name('Units\\Human\\Footman.mdx'))
        with tempfile.TemporaryDirectory() as tmp:
            file = Path(tmp) / 'map.w3x'
            file.write_bytes(b'HM3W' + b'\0' * 508 + b'MPQ\x1a' + b'\0' * 60)
            self.assertTrue(mpq.probe(str(file)))
            file.write_bytes(b'PK\x03\x04' + b'\0' * 540)
            self.assertFalse(mpq.probe(str(file)))

    def test_folder_to_assets_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'Demo.w3x'
            source.mkdir()
            (source / 'war3map.w3e').write_bytes(w3e(11, 0x56))
            (source / 'war3map.wpm').write_bytes(b'MP3W' + struct.pack('<III', 0, 1, 1) + b'\x40')
            (source / 'war3map.wts').write_bytes(b'STRING 1\n{\nDemo\n}\n')
            (source / 'Units').mkdir()
            (source / 'Units' / 'Test.mdx').write_bytes(b'MDLX')
            out = root / 'game'
            proc = subprocess.run([sys.executable, str(IMPORTER), '--source', str(source), '--out', str(out)],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            dest = out / 'content/Warcraft3/Demo'
            manifest = json.loads((dest / 'import.json').read_text())
            self.assertEqual(manifest['fileCount'], 4)
            self.assertTrue((dest / 'Units/Test.mdx').exists())
            self.assertEqual(json.loads((dest / 'terrain.json').read_text())['version'], 11)
            proc = subprocess.run([sys.executable, str(IMPORTER), '--source', str(source), '--out', str(out)],
                                  capture_output=True, text=True)
            self.assertEqual(proc.returncode, 2)
            self.assertIn('will not overwrite', proc.stdout)


if __name__ == '__main__':
    unittest.main()
