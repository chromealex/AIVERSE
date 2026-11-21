import json
from pathlib import Path
import tempfile
import unittest
from PIL import Image
from pack import pack
from import_bundle import import_bundle


class ImportTest(unittest.TestCase):
    def test_import_preserves_library_and_rejects_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / 'project'
            project.mkdir()
            library = project / 'materials.json'
            library.write_text(json.dumps({'materials': {'old': {'roughness': .3}}, 'extra': 42}))
            Image.new('L', (2, 2), 99).save(root / 'rough.png')
            bundle = pack(root, {'roughness': 'rough.png'}, root / 'bundle')
            material = import_bundle(bundle, project, 'stone')
            self.assertTrue((project / material['metallicRoughnessMap']).exists())
            document = json.loads(library.read_text())
            self.assertEqual(document['extra'], 42)
            self.assertEqual(document['materials']['old']['roughness'], .3)
            before = library.read_bytes()
            with self.assertRaises(ValueError):
                import_bundle(bundle, project, 'stone')
            self.assertEqual(library.read_bytes(), before)
            self.assertFalse((project / '.substance-import.lock').exists())

    def test_escape_rejected_before_project_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / 'project'
            project.mkdir()
            Image.new('RGB', (2, 2)).save(root / 'base.png')
            bundle = pack(root, {'baseColor': 'base.png'}, root / 'bundle')
            (bundle / 'material.json').write_text('{"texture":"../base.png"}')
            with self.assertRaises(ValueError):
                import_bundle(bundle, project, 'bad')
            self.assertEqual(list(project.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
