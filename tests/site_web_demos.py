"""Verify first deployment and reuse of published binary and mixed-newline assets."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/site'))
from web_demos import copy_web_demos, unversioned_index, versioned_index


class WebDemoPublication(unittest.TestCase):
    def test_verified_local_and_published_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            root = base / 'bundles'
            repo = base / 'pages'
            repo.mkdir()
            (root / 'range').mkdir(parents=True)
            files = {'index.html': b'<html>\r\n', 'bin/game.wasm': b'\x00asm\x00\xff'}
            manifest = {'range': {}}
            for name, contents in files.items():
                path = root / 'range' / name
                path.parent.mkdir(exist_ok=True)
                path.write_bytes(contents)
                manifest['range'][name] = hashlib.sha256(contents).hexdigest()
            copy_web_demos(manifest, repo, root, repo)
            subprocess.run(['git', 'init', '-q'], cwd=repo, check=True)
            subprocess.run(['git', '-c', 'core.autocrlf=true', 'add', '.'], cwd=repo, check=True)
            # An isolated fixture ref can point directly to a tree. No identity or commit required.
            tree = subprocess.check_output(['git', 'write-tree'], cwd=repo, text=True).strip()
            subprocess.run(['git', 'update-ref', 'refs/remotes/origin/gh-pages', tree], cwd=repo, check=True)
            out = base / 'rebuilt'
            copy_web_demos(manifest, out, None, repo)
            for name, data in files.items():
                self.assertEqual((out / 'demos/range' / name).read_bytes(), data)
            (root / 'range/index.html').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                copy_web_demos(manifest, out, root, repo)
            manifest['range']['index.html'] = '0' * 64
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                copy_web_demos(manifest, out, None, repo)
            with self.assertRaisesRegex(ValueError, 'invalid demo asset'):
                copy_web_demos({'range': {'../escape': '0' * 64, 'index.html': '0' * 64}}, out, root, repo)

    def test_index_pins_its_runtime_and_reports_errors(self):
        page = (b'<script>var Module={arguments:[]};'
                b'window.onerror=function(){Module.setStatus("Error: see the browser console")}</script>'
                b'<script async src=index.js></script>')
        files = {'index.html': 'a' * 64, 'index.js': 'b' * 64, 'index.wasm': 'c' * 64}
        text = versioned_index(page, files).decode()
        version = hashlib.sha256(''.join(files[k] for k in sorted(files)).encode()).hexdigest()[:12]
        self.assertIn(f'src=index.js?v={version}>', text)
        self.assertIn(f'var Module={{locateFile:function(p){{return p+"?v={version}"}},arguments:[]', text)
        self.assertIn('Module.setStatus("Error: "+(m||', text)
        self.assertEqual(versioned_index(page, files), versioned_index(page, files))
        self.assertEqual(unversioned_index(versioned_index(page, files)), page)


if __name__ == '__main__':
    unittest.main()
