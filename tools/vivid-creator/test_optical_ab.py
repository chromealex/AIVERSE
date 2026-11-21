#!/usr/bin/env python3
"""Stage 7AD deterministic CPU tests for viewport optical A/B reporting."""
import importlib.util
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).with_name("optical_ab.py")
spec = importlib.util.spec_from_file_location("vivid_optical_ab", MODULE)
optical = importlib.util.module_from_spec(spec)
spec.loader.exec_module(optical)


class OpticalAbTests(unittest.TestCase):
    def make_image(self, path, tint=(0, 0, 0), width=640, height=400):
        # P6 with a fixed neutral field; an altered pixel in the captured
        # viewport crop simulates a material change, not a UI text change.
        image = bytearray(bytes((32, 64, 96)) * (width * height))
        for y in range(height // 4, height // 2):
            for x in range(450, 470):
                i = (y * width + x) * 3
                image[i:i + 3] = bytes(tint)
        path.write_bytes(
            f"P6\n{width} {height}\n255\n".encode("ascii") + image
        )

    def test_changes_are_counted_only_in_viewport(self):
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp) / "a.ppm", Path(temp) / "b.ppm"
            self.make_image(a, (0, 0, 0))
            self.make_image(b, (64, 32, 16))
            result = optical.compare(a, b, 24)
            self.assertEqual(result["samples"], (576 - 420) * 300)
            self.assertEqual(result["changed_pixels"], 2000)
            self.assertEqual(result["changed_per_channel"], [2000, 2000, 2000])

    def test_named_region_restricts_comparison(self):
        with tempfile.TemporaryDirectory() as temp:
            a, b = Path(temp) / "a.ppm", Path(temp) / "b.ppm"
            self.make_image(a, (0, 0, 0))
            self.make_image(b, (64, 32, 16))
            roi = optical.parse_roi("0.15,0.2,0.38,0.6")
            result = optical.compare(a, b, 24, roi)
            self.assertEqual(result["viewport_crop"], [443, 110, 479, 230])
            self.assertEqual(result["samples"], 36 * 120)
            self.assertEqual(result["changed_pixels"], 20 * 90)
            self.assertEqual(result["roi_normalized"], [0.15, 0.2, 0.38, 0.6])

    def test_invalid_region_fails_closed(self):
        for value in ("0,0,0,1", "0.4,0.2,0.1,0.5", "nan,0,1,1",
                      "-0.1,0,1,1", "0,0,2,1", "0,0,1"):
            with self.assertRaises(ValueError):
                optical.parse_roi(value)

    def test_equal_images_have_no_changes(self):
        with tempfile.TemporaryDirectory() as temp:
            a = Path(temp) / "a.ppm"
            self.make_image(a, (64, 32, 16))
            self.assertEqual(optical.compare(a, a, 24)["changed_pixels"], 0)

    def test_invalid_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            a = Path(temp) / "a.ppm"
            a.write_bytes(b"P6\n640 400\n255\nshort")
            with self.assertRaises(ValueError):
                optical.read_ppm(a)


if __name__ == "__main__":
    unittest.main()
