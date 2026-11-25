"""Exercise recording and comparison without running a full simulated match."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


class ChecksumRunner(unittest.TestCase):
    def test_record_and_comparison(self):
        source = Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            shutil.copy(source / 'check.sh', root / 'check.sh')
            info = root / '.github/scripts/game-info.py'
            info.parent.mkdir(parents=True)
            shutil.copy(source.parents[1] / '.github/scripts/game-info.py', info)
            template = root / 'catalog/sample'
            template.mkdir(parents=True)
            (template / 'game.json').write_text('{"module":"game"}')
            child = root / 'aiverse_headless'
            child.write_text('#!/bin/sh\nprintf "%s" "$MATCH_OUTPUT"\nexit "$MATCH_EXIT"\n')
            child.chmod(0o755)
            checksum = '0123456789abcdef'
            good = 'Checksum: ' + checksum + '\r\n'

            def run(output, record=False, code=0):
                env = dict(os.environ, AIVERSE_TEMPLATES_DIR=str(template.parent),
                           MATCH_OUTPUT=output, MATCH_EXIT=str(code))
                return subprocess.run(['sh', 'check.sh', '.', *(['--record'] if record else [])],
                                      cwd=root, env=env, capture_output=True)

            self.assertEqual(run(good, record=True).returncode, 0)
            baseline = root / 'baseline.txt'
            expected = ''.join(f'sample {seed} {checksum}\n' for seed in (1, 2, 3)).encode()
            self.assertEqual(baseline.read_bytes(), expected)
            baseline.write_bytes(expected.replace(b'\n', b'\r\n'))
            self.assertEqual(run(good).returncode, 0)
            self.assertNotEqual(run(good.replace('abcdef', 'abcdee')).returncode, 0)
            previous = baseline.read_bytes()
            for output, code in [('', 0), (good + good, 0), ('Checksum: not-a-hash\n', 0), (good, 1)]:
                with self.subTest(output=output, code=code):
                    self.assertNotEqual(run(output, record=True, code=code).returncode, 0)
                    self.assertEqual(baseline.read_bytes(), previous)


if __name__ == '__main__':
    unittest.main()
