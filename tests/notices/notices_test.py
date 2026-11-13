#!/usr/bin/env python3
"""License inventory, failure handling and build-artifact regression tests (no SDKs)."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def run(*args, ok=True):
    p = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if ok and p.returncode:
        raise AssertionError(p.stdout)
    return p


class Notices(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='aiverse notices ')
        self.work = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def generate(self, root=ROOT, extra=None, ok=True):
        args = ['cmake', f'-DAIVERSE_NOTICE_ROOT={root}',
                f'-DAIVERSE_NOTICE_OUTPUT={self.work}/THIRD_PARTY_NOTICES.txt']
        if extra:
            args.append(f'-DAIVERSE_PROJECT_NOTICES={extra}')
        args += ['-P', str(ROOT / 'platform/cmake/GenerateNotices.cmake')]
        return run(*args, ok=ok)

    def test_inventory_and_original_text(self):
        manifest = json.loads((ROOT / 'third_party/notices.json').read_text())
        sources = {e['path'] for e in manifest['entries']}
        for p in (ROOT / 'third_party').rglob('*'):
            if p.is_file() and p.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE', 'OFL')) and p.name != 'notices.json':
                self.assertIn(str(p.relative_to(ROOT)), sources)
        self.assertEqual({p.name for p in (ROOT / 'third_party').iterdir() if p.is_dir()},
                         {Path(p).parts[1] for p in sources if p.startswith('third_party/')})
        self.generate()
        out = self.work / 'THIRD_PARTY_NOTICES.txt'
        result = out.read_text()
        for e in manifest['entries']:
            content = (ROOT / e['path']).read_bytes().replace(b'\r\n', b'\n')
            if 'offset' in e:
                self.assertEqual(hashlib.sha256(content).hexdigest(), e['sha256'])
                content = content[e['offset']:e['offset'] + e['length']]
            self.assertIn(content.decode().strip(), result)
        for author in ['Alex Silaev', 'Loup Vaillant', 'Bruno Levy', 'Nicolas De Carli',
                       'Alexey Tourbin', 'Jackson L. Allan', 'Jutta Degener', 'Markus Schöngart',
                       'Tristan Grimmer', 'David Reid', 'Yuta Mori', 'Tino Reichardt']:
            self.assertIn(author, result)
        before = out.stat().st_mtime_ns
        self.generate()
        self.assertEqual(out.stat().st_mtime_ns, before)

    def test_missing_empty_and_changed_sources_fail(self):
        root = self.work / 'fixture'
        (root / 'third_party').mkdir(parents=True)
        (root / 'third_party/notices.json').write_text(json.dumps({'entries': [{'name': 'test', 'path': 'LICENSE'}]}))
        self.assertNotEqual(self.generate(root, ok=False).returncode, 0)
        (root / 'LICENSE').write_text('')
        self.assertNotEqual(self.generate(root, ok=False).returncode, 0)
        (root / 'LICENSE').write_text('license')
        (root / 'third_party/notices.json').write_text(json.dumps({'entries': [dict(name='test', path='LICENSE', offset=0, length=7, sha256='wrong')]}))
        self.assertIn('Re-audit', self.generate(root, ok=False).stdout)

    def test_project_supplement(self):
        extra = self.work / 'game notice.txt'
        extra.write_text('Asset author; full license supplied by game developer.\n')
        self.generate(extra=extra)
        self.assertIn(extra.read_text(), (self.work / 'THIRD_PARTY_NOTICES.txt').read_text())
        extra.write_text(' ')
        self.assertNotEqual(self.generate(extra=extra, ok=False).returncode, 0)

    def test_native_plugin_and_asset_collection(self):
        exe = self.work / 'notices-test'
        run(os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-I', str(ROOT),
            str(ROOT / 'tests/notices/plugin_test.cpp'), str(ROOT / 'tools/aiversebuild/Plugins.cpp'),
            str(ROOT / 'engine/platform/PluginJson.cpp'), str(ROOT / 'engine/platform/Plugins.cpp'), '-o', str(exe))
        run(str(exe), str(ROOT), str(self.work / 'project'))

    def test_desktop_and_apple_bundle_rebuild(self):
        # Exercise the actual packaging function with a tiny binary, including a
        # license-only update and deletion of the delivered notice between builds.
        for platform in ['linux', 'web'] + (['macos', 'ios'] if os.uname().sysname == 'Darwin' else []):
            source = self.work / platform
            source.mkdir()
            (source / 'third_party').mkdir()
            (source / 'third_party/notices.json').write_text(json.dumps({'entries': [dict(name='fixture', path='LICENSE')]}))
            (source / 'LICENSE').write_text('fixture version one\n')
            (source / 'main.cpp').write_text('int main() {}\n')
            (source / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.22)
project(NoticeFixture LANGUAGES CXX)
set(AIVERSE_PLATFORM {platform})
include("{ROOT}/platform/cmake/ThirdPartyNotices.cmake")
add_executable(game main.cpp)
set_target_properties(game PROPERTIES MACOSX_BUNDLE {'TRUE' if platform in ('macos','ios') else 'FALSE'})
aiverse_package_notices(game)
''')
            build = source / 'build'
            run('cmake', '-S', str(source), '-B', str(build))
            run('cmake', '--build', str(build), '--target', 'game')
            target = build / ('game.app/Contents/Resources/THIRD_PARTY_NOTICES.txt' if platform in ('macos', 'ios') else 'THIRD_PARTY_NOTICES.txt')
            self.assertIn('fixture version one', target.read_text())
            (source / 'LICENSE').write_text('fixture version two\n')
            run('cmake', '--build', str(build), '--target', 'game')
            self.assertIn('fixture version two', target.read_text())
            target.unlink()
            run('cmake', '--build', str(build), '--target', 'game')
            self.assertIn('fixture version two', target.read_text())


if __name__ == '__main__':
    unittest.main()
