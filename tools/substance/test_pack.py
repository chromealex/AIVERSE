import tempfile
import unittest
from pathlib import Path
from PIL import Image
from pack import pack
import json


class PackTest(unittest.TestCase):
    def test_channels_normals_and_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            Image.new('L', (2, 2), 73).save(root / 'rough.png')
            Image.new('RGB', (2, 2), (120, 40, 250)).save(root / 'normal.png')
            output = pack(root, {'roughness': 'rough.png', 'normal': 'normal.png'},
                          root / 'bundle', True)
            with Image.open(output / 'orm.png') as image:
                self.assertEqual(image.getpixel((0, 0)), (255, 73, 0))
            with Image.open(output / 'normal.png') as image:
                self.assertEqual(image.getpixel((0, 0)), (120, 215, 250, 255))
            material = json.loads((output / 'material.json').read_text())
            self.assertEqual(material['roughness'], 1)
            self.assertEqual(material['metallicRoughnessMap'], 'orm.png')
            self.assertNotIn('occlusionMap', material)

    def test_bad_dimensions_publish_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            Image.new('L', (2, 2)).save(root / 'a.png')
            Image.new('L', (3, 3)).save(root / 'b.png')
            with self.assertRaises(ValueError):
                pack(root, {'roughness': 'a.png', 'metallic': 'b.png'}, root / 'out')
            self.assertFalse((root / 'out').exists())


if __name__ == '__main__':
    unittest.main()
