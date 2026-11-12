import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('temporal_noise',Path(__file__).with_name('temporal_noise.py'))
noise=importlib.util.module_from_spec(spec);spec.loader.exec_module(noise)

class TemporalNoiseTests(unittest.TestCase):
    def test_frozen_and_alternating_sequences(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            for alternating in (False,True):
                for i in range(16):
                    value=255 if alternating and i%2 else 0
                    (root/f'0-{i}.ppm').write_bytes(b'P6\n16 12\n255\n'+bytes([value])*16*12*3)
                result=noise.measure(root)
                for region in result.values():self.assertEqual(region['meanFrameDifferenceRgb8'],255 if alternating else 0)
    def test_truncated_frame_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'x.ppm';path.write_bytes(b'P6\n16 12\n255\n')
            with self.assertRaises(ValueError):noise.read(path)

if __name__=='__main__':unittest.main()
