#!/usr/bin/env python3
"""Dependency-free smoke checks for the repository-wide Bash runner."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]


class UnifiedRunnerTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="aiverse-runner-")
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        (self.root / "tests").mkdir()
        (self.root / "build" / "fixture").mkdir(parents=True)
        (self.root / "build" / "fixture" / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=Debug\nAIVERSE_BUILD_TESTS:BOOL=ON\n"
        )
        shutil.copy2(SOURCE / "run-tests.sh", self.root / "run-tests.sh")
        (self.root / "tests" / "extra-test-suites.txt").write_text(
            "sample|bash|tests/sample.sh|\n"
        )
        (self.root / "tests" / "sample.sh").write_text(
            "#!/usr/bin/env bash\nprintf 'FAKE_EXTRA_PASSED\\n'\n"
        )
        self.bin_dir = self.root / "fake-bin"
        self.bin_dir.mkdir()
        # Fake-tool tests exercise the portable runner without a real disk.
        self.write_tool("uname", "#!/usr/bin/env bash\necho Linux\n")
        self.write_tool("ctest",
            "#!/usr/bin/env bash\n"
            "printf 'FAKE_CTEST_ARGS:%s\\n' \"$*\"\n"
            "exit \"$FAKE_CTEST_EXIT\"\n")
        self.write_tool("cmake",
            "#!/usr/bin/env bash\n"
            "printf 'FAKE_CMAKE_ARGS:%s\\n' \"$*\"\n"
            "if [[ \"$1\" == '--build' && \"$FAKE_BUILD_FAIL\" == 1 ]]; then\n"
            "  printf 'error: synthetic compiler failure\\n' >&2\n"
            "  exit 9\n"
            "fi\n")

    def write_tool(self, name, text):
        path = self.bin_dir / name
        path.write_text(text)
        path.chmod(0o755)

    def invoke(self, *args, extras=None):
        env = dict(os.environ)
        env["PATH"] = str(self.bin_dir) + os.pathsep + env.get("PATH", "")
        env["FAKE_CTEST_EXIT"] = "0"
        env["FAKE_BUILD_FAIL"] = "0"
        env.update(extras or {})
        return subprocess.run(
            ["bash", str(self.root / "run-tests.sh"), "--build-dir", "build/fixture", *args],
            cwd=self.root, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        )

    def test_local_mac_missing_kingston_stops_before_tools(self):
        self.write_tool("uname", "#!/usr/bin/env bash\necho Darwin\n")
        self.write_tool("mount", "#!/usr/bin/env bash\nexit 0\n")
        result = self.invoke("--quick", extras={"GITHUB_ACTIONS": ""})
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("KINGSTON is not mounted and writable", result.stdout)
        self.assertNotIn("FAKE_CMAKE_ARGS", result.stdout)
        self.assertNotIn("FAKE_CTEST_ARGS", result.stdout)

    def test_bash_syntax(self):
        subprocess.run(["bash", "-n", str(self.root / "run-tests.sh")], check=True)

    def test_quick_skips_extras_but_runs_all_ctest(self):
        result = self.invoke("--quick", "--no-build")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PASS ctest", result.stdout)
        self.assertNotIn("FAKE_EXTRA_PASSED", result.stdout)
        self.assertIn("--no-tests=error", result.stdout)
        self.assertNotIn(" -R ", result.stdout)

    def test_full_runs_registered_extra_suites(self):
        result = self.invoke("--no-build")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PASS ctest", result.stdout)
        self.assertIn("FAKE_EXTRA_PASSED", result.stdout)
        self.assertIn("PASS extra-sample", result.stdout)
        self.assertTrue(list((self.root / "build/fixture/test-reports").glob("*/summary.txt")))

    def test_python_suite_uses_configured_interpreter(self):
        self.write_tool("configured python", "#!/bin/sh\necho CONFIGURED_PYTHON\n")
        (self.root / "build/fixture/test-python.txt").write_text(str(self.bin_dir / "configured python") + "\n")
        (self.root / "tests/extra-test-suites.txt").write_text("sample|python3|tests/sample.sh|\n")
        result = self.invoke("--no-build")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("CONFIGURED_PYTHON", result.stdout)

    def test_ctest_failure_is_reported_without_hiding_extras(self):
        result = self.invoke("--no-build", extras={"FAKE_CTEST_EXIT": "7"})
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("FAIL ctest (exit 7)", result.stdout)
        self.assertIn("PASS extra-sample", result.stdout)
        self.assertIn("Failed: ctest", result.stdout)

    def test_build_failure_stops_before_tests(self):
        result = self.invoke("--quick", extras={"FAKE_BUILD_FAIL": "1"})
        self.assertEqual(result.returncode, 1, result.stdout)
        self.assertIn("error: synthetic compiler failure", result.stdout)
        self.assertIn("Build failed; tests were NOT RUN", result.stdout)
        self.assertNotIn("FAKE_CTEST_ARGS", result.stdout)

    def test_build_keeps_asserts_enabled(self):
        result = self.invoke("--quick")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("CMAKE_BUILD_TYPE=Debug", result.stdout)
        self.assertIn("--config Debug", result.stdout)

    def test_no_build_rejects_disabled_assertions(self):
        (self.root / "build" / "fixture" / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n"
        )
        result = self.invoke("--no-build", "--quick")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("requires a Debug CMake cache", result.stdout)
        self.assertNotIn("FAKE_CTEST_ARGS", result.stdout)

    def test_crlf_registry_passes_no_phantom_args(self):
        (self.root / "tests" / "extra-test-suites.txt").write_bytes(
            b"sample|bash|tests/sample.sh|\r\n"
        )
        result = self.invoke("--no-build")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("FAKE_EXTRA_PASSED", result.stdout)
        self.assertNotIn("Unknown argument", result.stdout)

    def test_crlf_registry_preserves_flags(self):
        (self.root / "tests" / "extra-test-suites.txt").write_bytes(
            b"sample|bash|tests/sample.sh|--full\r\n"
        )
        (self.root / "tests" / "sample.sh").write_text(
            "#!/usr/bin/env bash\n"
            '[[ "$#" == 1 && "$1" == "--full" ]] || exit 7\n'
            "echo FAKE_EXTRA_PASSED\n"
        )
        result = self.invoke("--no-build")
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("PASS extra-sample", result.stdout)

    def test_rejects_unsafe_registry_paths(self):
        (self.root / "tests" / "extra-test-suites.txt").write_text(
            "unsafe|bash|../outside.sh|\n"
        )
        result = self.invoke("--no-build")
        self.assertEqual(result.returncode, 2, result.stdout)
        self.assertIn("Invalid or absent suite script", result.stdout)


if __name__ == "__main__":
    unittest.main()
