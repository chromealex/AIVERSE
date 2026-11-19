"""Standalone regression tests for AIVERSE's terminal build dashboard."""
import argparse
import contextlib
import importlib.util
import io
import json
import pathlib
import tempfile
import unittest
from unittest import mock

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "aiverse-progress.py"
spec = importlib.util.spec_from_file_location("aiverse_progress", SCRIPT)
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)


class DiagnosticsTests(unittest.TestCase):
    def make_dashboard(self, directory):
        args = argparse.Namespace(plain=True, build_dir=directory)
        return monitor.Dashboard(args, directory)

    def test_compiler_cmake_ctest_diagnostics_and_cross_stage_retention(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            d = self.make_dashboard(folder)
            try:
                d.stage_name = "build"
                d.consume("/Users/dev/hello with args.cpp:9:1: note: ignored")
                d.consume("/Users/dev/src/File.cpp:42:17: warning: unused variable [-Wunused-variable]")
                d.consume("CMake Warning at CMakeLists.txt:7 (message):")
                d.consume("source.cpp(22): warning C4996: deprecated")
                d.consume("src/Game.mm:31:2: error: invalid conversion")
                d.consume("CMake Error at CMakeLists.txt:8 (message):")
                d.stage_name = "ctest"
                d.consume("100% tests passed, 0 tests failed out of 2")
                d.consume(" 2/3 Test #77: regression ........***Failed 0.02 sec")
                self.assertEqual(d.warning_count, 3)
                self.assertEqual(d.error_count, 3)
                self.assertEqual(d.issues[0][0], "WARN")
                self.assertIn("File.cpp:42", d.issues[0][1])
                self.assertEqual(d.issues[-1][0], "ERROR")
                for i in range(10):
                    d.consume("header.hpp:%d:1: warning: later warning" % i)
                self.assertIn("CMake Error", "\n".join(d.recent_errors))
                self.assertEqual(d.error_count, 3)
            finally:
                d.close()
            log = (folder / "diagnostics.log").read_text()
            self.assertIn("[build] /Users/dev/src/File.cpp:42:17", log)
            self.assertIn("[ctest]", log)
            self.assertIn("CMake Warning", log)
            self.assertNotIn("note: ignored", log)

    def test_dashboard_pins_recent_error_above_unimportant_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = self.make_dashboard(pathlib.Path(tmp))
            try:
                d.stage_name = "build"
                d.consume("engine/Tile.cpp:99:3: error: bad things happened")
                for i in range(20):
                    d.consume("[ 80%%] boring build message %d" % i)
                d.tty = True  # fake a terminal, avoid changing real TTY modes
                with mock.patch.object(monitor.shutil, "get_terminal_size", return_value=__import__('os').terminal_size((100, 24))):
                    with contextlib.redirect_stdout(io.StringIO()) as rendered:
                        d.draw(force=True)
                screen = rendered.getvalue()
                self.assertIn("WARNINGS / ERRORS", screen)
                self.assertIn("Tile.cpp:99", screen)
                self.assertIn("\033[31m", screen)
                self.assertIn("Recent output:", screen)
                self.assertLess(screen.index("Tile.cpp:99"), screen.index("Recent output:"))
            finally:
                d.tty = False
                d.close()

    def test_short_issue_basename_and_plain_source(self):
        self.assertEqual(monitor.short_issue("/tmp/a/b/test.cpp:14:7: error: bad"), "test.cpp:14: bad")
        self.assertEqual(monitor.short_issue("CMake Error at CMakeLists.txt:8"), "CMake Error at CMakeLists.txt:8")


if __name__ == "__main__":
    unittest.main()
