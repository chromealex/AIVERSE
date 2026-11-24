#!/usr/bin/env python3
"""Tiny hermetic parser tests; no Apple GPU or compiler required."""
import unittest
from verify_metal_ctest import parse_results, evaluate

GOOD = """1/5 Test #112: spectrafx_metal_gpu ............ Passed 0.10 sec
2/5 Test #113: spectrafx_metal_billboard ....... Passed 0.12 sec
3/5 Test #114: spectrafx_metal_rhi ............ Passed 0.45 sec
4/5 Test #115: spectrafx_metal_scene .......... Passed 0.52 sec
5/5 Test #116: spectrafx_metal_allocator ...... Passed 1.20 sec
"""
SKIP = GOOD.replace("Passed 0.45 sec", "***Skipped 0.45 sec")
MISSING = GOOD.splitlines()[:3]
FAIL = GOOD.replace("Passed 0.12 sec", "***Failed 0.12 sec")

class MetalEvidenceTests(unittest.TestCase):
    def test_all_five_native_passes(self):
        self.assertEqual(sum(v == "PASS" for v in parse_results(GOOD).values()), 5)
        self.assertEqual(evaluate(GOOD, strict=True), 0)

    def test_skips_are_not_reported_as_real_pass(self):
        self.assertEqual(parse_results(SKIP)["spectrafx_metal_rhi"], "SKIP")
        self.assertEqual(evaluate(SKIP), 0)  # hosted macOS may have no device
        self.assertEqual(evaluate(SKIP, strict=True), 1)

    def test_missing_test_never_reports_full_acceptance(self):
        self.assertEqual(evaluate("\n".join(MISSING)), 1)

    def test_failed_test_rejects_both_modes(self):
        self.assertEqual(evaluate(FAIL), 1)
        self.assertEqual(evaluate(FAIL, strict=True), 1)

if __name__ == "__main__":
    unittest.main()
