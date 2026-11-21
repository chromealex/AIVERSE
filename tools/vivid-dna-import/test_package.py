import json
from pathlib import Path
import tempfile
import unittest
from package import package

class PackageTests(unittest.TestCase):
    def source(self, root):
        source = root/'decoded.json'
        source.write_text(json.dumps({'version': 1, 'name': 'fixture', 'lod': 0,
          'target': 'source_brow', 'meshes': [{'name': 'head_lod0_mesh',
          'positions': [[0,0,0],[1,0,0],[0,1,0]], 'normals': [[0,0,1]],
          'uv': [[0,0],[1,0],[0,1],[.5,.5]],
          'layouts': [[0,0,0],[1,0,1],[2,0,2],[0,0,3]],
          'faces': [[0,1,2],[3,1,2]], 'deltas': [[0,[0,.1,0]]]}]}))
        return source

    def test_uv_seam_receives_same_source_delta(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            info = package(self.source(root), root/'out', channel='browInnerUp')
            morphs = json.loads((root/'out/morphs.json').read_text())
            deltas = morphs['targets'][0]['deltas']
            self.assertEqual([d['vertex'] for d in deltas], [0,3])
            self.assertEqual(deltas[0]['position'], deltas[1]['position'])
            self.assertAlmostEqual(deltas[0]['position'][1], .32)
            self.assertEqual(info['vertices'], 4)
            gltf = json.loads((root/'out/model.gltf').read_text())
            self.assertEqual(gltf['buffers'][0]['byteLength'], (root/'out/model.bin').stat().st_size)

    def test_rejects_invalid_layout_without_publishing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = self.source(root)
            data = json.loads(source.read_text())
            data['meshes'][0]['layouts'][0][0] = -1
            source.write_text(json.dumps(data))
            with self.assertRaises(ValueError): package(source, root/'out')
            self.assertFalse((root/'out').exists())

    def test_rejects_baked_plus_live_deformation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaises(ValueError):
                package(self.source(root), root/'out', weight=1, channel='browInnerUp')
            self.assertFalse((root/'out').exists())

    def test_refuses_output_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); source = self.source(root)
            (root/'out').mkdir(); (root/'out/sentinel').write_text('preserve')
            with self.assertRaises(FileExistsError): package(source, root/'out')
            self.assertEqual((root/'out/sentinel').read_text(), 'preserve')

if __name__ == '__main__': unittest.main()
