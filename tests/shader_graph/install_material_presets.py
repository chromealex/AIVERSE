"""Verify that installed Shader Graph presets appear as independent project assets."""
import json
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/renderbench'))
from install_material_library import LIBRARY, install


class MaterialPresetInstall(unittest.TestCase):
    def setUp(self):
        temp = TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.project = Path(temp.name)
        (self.project / 'game.json').write_text('{}')
        self.index = self.project / 'materials.json'
        self.original = b'{"materials":{"existing":{"roughness":0.4}},"custom":true}'
        self.index.write_bytes(self.original)

    def test_install_and_retry(self):
        names = install(self.project)
        index = json.loads(self.index.read_text())
        self.assertTrue(index['custom'])
        self.assertEqual(index['materials']['existing'], {'roughness': 0.4})
        source = json.loads((LIBRARY / 'materials.json').read_text())['materials']
        self.assertEqual(set(names), set(source))
        for name, material in source.items():
            relative = index['materials'][name]
            self.assertEqual(relative, f'content/materials/{name}.material.json')
            self.assertEqual(json.loads((self.project / relative).read_text()), material)
            graph = self.project / 'shaders' / f'{name}.json'
            self.assertEqual(json.loads(graph.read_text()), material['shaderGraph'])
        self.assertEqual((self.project / 'materials.before-shader-library.json').read_bytes(), self.original)
        before = self.index.read_bytes()
        with self.assertRaises(ValueError):
            install(self.project)
        self.assertEqual(self.index.read_bytes(), before)

    def test_index_write_failure_can_be_retried(self):
        with patch('install_material_library.os.replace', side_effect=OSError('write failed')):
            with self.assertRaises(OSError):
                install(self.project)
        self.assertEqual(self.index.read_bytes(), self.original)
        self.assertEqual(list((self.project / 'content/materials').iterdir()), [])
        self.assertEqual(list((self.project / 'shaders').iterdir()), [])
        self.assertFalse((self.project / 'materials.before-shader-library.json').exists())
        self.assertIn('sg_basalt', install(self.project))

    def test_existing_asset_is_preserved(self):
        path = self.project / 'content/materials/sg_basalt.material.json'
        path.parent.mkdir(parents=True)
        path.write_text('custom')
        with self.assertRaises(ValueError):
            install(self.project)
        self.assertEqual(path.read_text(), 'custom')
        self.assertEqual(self.index.read_bytes(), self.original)
        self.assertFalse((self.project / 'shaders').exists())


if __name__ == '__main__':
    unittest.main()
