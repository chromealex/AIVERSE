"""Contract tests for Studio CMake and one-pass Mac verification entrypoint."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]

class StudioBuildConfigTests(unittest.TestCase):
    def test_objective_cpp_enabled_by_root_before_targets(self):
        cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
        option='option(AIVERSE_BUILD_STUDIO_HTML5 "Enable opt-in macOS HTML5 Studio" OFF)'
        self.assertEqual(cmake.count(option),1)
        language=cmake.index("enable_language(OBJCXX)",cmake.index(option))
        self.assertLess(cmake.index(option),language)
        self.assertLess(language,cmake.index("add_library(aiverse_engine STATIC"))
        native=(ROOT/"apps/studio-native/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertNotIn("enable_language(OBJCXX)",native)
        self.assertIn("add_executable(aiverse_studio MACOSX_BUNDLE",native)

    def test_mac_build_script_has_valid_shell_syntax(self):
        bash=shutil.which("bash")
        if bash is None: self.skipTest("bash unavailable")
        file=ROOT/"tools/studio/verify_mac.sh"
        completed=subprocess.run([bash,"-n",str(file)],capture_output=True,text=True)
        self.assertEqual(completed.returncode,0,completed.stderr)

    def test_single_studio_ctest_invocation_with_quoted_regex(self):
        text=(ROOT/"tools/studio/verify_mac.sh").read_text(encoding="utf-8")
        self.assertEqual(text.count('ctest --test-dir "$BUILD_DIR"'),1)
        self.assertIn("-R '^studio_'",text)

if __name__=="__main__":
    unittest.main()
