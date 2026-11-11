"""Published package paths remain discoverable across the AIVERSE rename."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('publish', ROOT / 'tools/release/publish_builds.py')
publish = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publish)


class ReleaseIndexTest(unittest.TestCase):
    def test_mixed_old_and_new_packages(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            packages = []
            for brand, prefix, mcp in [('RTSEngine', 'rts', 'rtsmcp'), ('AIVERSE', 'aiverse', 'aiversemcp')]:
                rel = f'linux/{brand}-0.2.0-12345678-linux.zip'
                archive = root / rel
                archive.parent.mkdir(exist_ok=True)
                with zipfile.ZipFile(archive, 'w') as z:
                    for exe in [mcp, prefix + '_editor', prefix + '_headless']:
                        z.writestr(f'{brand}/bin/{exe}', b'\x7fELF' + bytes(14) + b'\x3e\x00' + bytes(44))
                packages.append(rel)
            index = publish.write_index(temp, packages, 'chromealex/rts-engine')
            self.assertEqual(index['schema'], 1)
            for entry, mcp in zip(index['packages'], ['rtsmcp', 'aiversemcp']):
                self.assertEqual(entry['arch'], 'x86_64')
                self.assertEqual(entry['rtsmcp'], f"{entry['root']}/bin/{mcp}")
                with zipfile.ZipFile(root / entry['file']) as z:
                    for field in ['rtsmcp', 'editor', 'headless']:
                        self.assertIn(entry[field], z.namelist())
                self.assertTrue(entry['url'].endswith(entry['file']))
                self.assertEqual(len(entry['sha256']), 64)


if __name__ == '__main__':
    unittest.main()
