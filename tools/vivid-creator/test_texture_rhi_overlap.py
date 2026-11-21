#!/usr/bin/env python3
"""Temporary-repository fixtures for the read-only VIVID/Texture audit."""
import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

SOURCE = Path(__file__).with_name("check_texture_rhi_overlap.py")
spec = importlib.util.spec_from_file_location("vivid_texture_overlap", SOURCE)
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


def run(repo, *args):
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        capture_output=True, check=True, text=True
    ).stdout.strip()


class TextureOverlapTests(unittest.TestCase):
    def test_classification_and_deterministic_sets(self):
        a = {"CMakeLists.txt", "engine/render/Model.cpp",
             "engine/character/vivid/HumanGroom.h"}
        b = {"engine/render/Model.cpp", "CMakeLists.txt",
             "engine/gfx/TextureStreaming.h"}
        result = audit.assess(a, b)
        self.assertEqual(result["changed_vivid"], 3)
        self.assertEqual(result["changed_texture"], 3)
        self.assertEqual(result["overlap_count"], 2)
        self.assertEqual(result["critical_overlap"],
                         ["CMakeLists.txt", "engine/render/Model.cpp"])
        self.assertEqual(result["vivid_only"],
                         ["engine/character/vivid/HumanGroom.h"])
        self.assertEqual(result["texture_only"],
                         ["engine/gfx/TextureStreaming.h"])

    def test_merge_base_is_independent_of_current_checkout(self):
        # Two independent branches edit the same shared path and each has
        # one unique file. The script must not switch the checkout or write
        # to the test repository when computing the report.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            run(root, "init", "-q")
            run(root, "config", "user.email", "fixture@example.invalid")
            run(root, "config", "user.name", "Fixture")
            (root / "base.txt").write_text("base\n", encoding="utf-8")
            run(root, "add", "--all")
            run(root, "commit", "-qm", "initial")
            base = run(root, "rev-parse", "HEAD")
            original_branch = run(root, "branch", "--show-current")
            run(root, "switch", "-qc", "vivid")
            (root / "CMakeLists.txt").write_text("vivid\n", encoding="utf-8")
            (root / "vivid.h").write_text("vivid\n", encoding="utf-8")
            run(root, "add", "--all")
            run(root, "commit", "-qm", "vivid change")
            run(root, "switch", "-q", original_branch)
            run(root, "switch", "-qc", "texture")
            (root / "CMakeLists.txt").write_text("texture\n", encoding="utf-8")
            (root / "texture.h").write_text("texture\n", encoding="utf-8")
            run(root, "add", "--all")
            run(root, "commit", "-qm", "texture change")
            before = run(root, "rev-parse", "HEAD")
            report = audit.inventory(root, "vivid", "texture")
            self.assertEqual(report["merge_base_sha"], base)
            self.assertEqual(report["overlap_count"], 1)
            self.assertEqual(report["critical_overlap"], ["CMakeLists.txt"])
            self.assertEqual(report["vivid_only"], ["vivid.h"])
            self.assertEqual(report["texture_only"], ["texture.h"])
            self.assertEqual(run(root, "branch", "--show-current"), "texture")
            self.assertEqual(run(root, "rev-parse", "HEAD"), before)
            self.assertEqual(run(root, "status", "--porcelain"), "")
            self.assertFalse(report["is_merge_proof"])


if __name__ == "__main__":
    unittest.main()
