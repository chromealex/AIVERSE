#!/usr/bin/env python3
"""STRATA Stage 18.5: deterministic offline RGBA8 / Depth32Float A/B audit.

Input files are raw *hardware capture* bytes, NOT screenshots. This tool never
renders, captures a GPU, or certifies production parity by itself.
"""
import argparse
import json
import math
import struct
from pathlib import Path


def compare_rgba(reference: bytes, candidate: bytes, width: int, height: int,
                 tolerance: int = 0, roi=None):
    required = width * height * 4
    if width <= 0 or height <= 0 or len(reference) != required or len(candidate) != required:
        raise ValueError("invalid RGBA8 dimensions or capture size")
    if tolerance < 0 or tolerance > 255:
        raise ValueError("tolerance must be in [0,255]")
    if roi is None:
        roi = (0, 0, width, height)
    x0, y0, x1, y1 = roi
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError("ROI must be within the capture")
    pixels = changed = max_delta = sum_delta = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            k = (y * width + x) * 4
            delta = max(abs(reference[k+c] - candidate[k+c]) for c in range(4))
            changed += delta > tolerance
            max_delta = max(max_delta, delta)
            sum_delta += delta
            pixels += 1
    return {"pixels": pixels, "changed_pixels": changed, "max_channel_delta": max_delta,
            "mean_max_channel_delta": sum_delta / pixels}


def compare_depth(reference: bytes, candidate: bytes, width: int, height: int,
                  tolerance: float = 0., roi=None):
    required = width * height * 4
    if width <= 0 or height <= 0 or len(reference) != required or len(candidate) != required:
        raise ValueError("invalid Depth32Float dimensions or capture size")
    if not math.isfinite(tolerance) or tolerance < 0:
        raise ValueError("invalid depth tolerance")
    x0, y0, x1, y1 = roi or (0, 0, width, height)
    if not (0 <= x0 < x1 <= width and 0 <= y0 < y1 <= height):
        raise ValueError("ROI must be within the capture")
    changed = pixels = 0
    maximum = 0.
    for y in range(y0, y1):
        for x in range(x0, x1):
            index = (y * width + x) * 4
            a = struct.unpack_from("<f", reference, index)[0]
            b = struct.unpack_from("<f", candidate, index)[0]
            if not (math.isfinite(a) and math.isfinite(b)):
                raise ValueError("nonfinite depth sample")
            delta = abs(a-b)
            changed += delta > tolerance
            maximum = max(maximum, delta)
            pixels += 1
    return {"pixels": pixels, "changed_pixels": changed, "max_depth_delta": maximum}


def self_test():
    a = bytes([10, 20, 30, 255, 80, 90, 100, 255])
    b = bytes([10, 20, 30, 255, 83, 90, 100, 255])
    assert compare_rgba(a, b, 2, 1)["changed_pixels"] == 1
    assert compare_rgba(a, b, 2, 1, tolerance=3)["changed_pixels"] == 0
    assert compare_rgba(a, b, 2, 1, roi=(0, 0, 1, 1))["changed_pixels"] == 0
    try:
        compare_rgba(a, b[:-1], 2, 1)
        raise AssertionError("truncation accepted")
    except ValueError:
        pass
    a = struct.pack("<ff", .25, .75)
    b = struct.pack("<ff", .25, .751)
    assert compare_depth(a, b, 2, 1, tolerance=.0001)["changed_pixels"] == 1
    assert compare_depth(a, b, 2, 1, tolerance=.002)["changed_pixels"] == 0
    try:
        compare_depth(a, struct.pack("<ff", float("nan"), .75), 2, 1)
        raise AssertionError("NaN accepted")
    except ValueError:
        pass


def audit_manifest(manifest_path: Path):
    """Verify the expected per-backend/pass/LOD coverage, fail closed."""
    doc = json.loads(manifest_path.read_text())
    captures = doc.get("captures", [])
    expected = doc.get("required", [])
    if not isinstance(captures, list) or not isinstance(expected, list) or not expected:
        raise ValueError("manifest requires nonempty required and captures arrays")
    observations = {}
    base = manifest_path.parent
    for case in captures:
        key = (case["backend"], case["pass"], case["view"], case["lod"])
        if key in observations:
            raise ValueError("duplicate audit case " + repr(key))
        fmt = case["format"]
        if fmt not in ("rgba8", "depth32"):
            raise ValueError("unsupported capture format")
        width, height = case["width"], case["height"]
        roi = case.get("roi")
        tol = case.get("tolerance", 0)
        reference = (base / case["reference"]).read_bytes()
        candidate = (base / case["candidate"]).read_bytes()
        if fmt == "rgba8":
            if not isinstance(tol, int):
                raise ValueError("rgba tolerance must be integral")
            diff = compare_rgba(reference, candidate, width, height, tol, roi)
        else:
            diff = compare_depth(reference, candidate, width, height, tol, roi)
        diff.update(backend=key[0], render_pass=key[1], view=key[2],
                    lod=key[3], format=fmt, passed=diff["changed_pixels"] == 0)
        observations[key] = diff
    for required in expected:
        key = (required["backend"], required["pass"], required["view"],
               required["lod"])
        if key not in observations:
            raise ValueError("missing required capture " + repr(key))
        wanted_format = required.get("format")
        if wanted_format and observations[key]["format"] != wanted_format:
            raise ValueError("capture format mismatch for " + repr(key))
    return {"passed": all(row["passed"] for row in observations.values()),
            "cases": list(observations.values()), "required_count": len(expected),
            "observed_count": len(observations)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--format", choices=("rgba8", "depth32"), default="rgba8")
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--tolerance", type=float, default=0.)
    parser.add_argument("--roi", type=int, nargs=4, metavar=("X0", "Y0", "X1", "Y1"))
    parser.add_argument("--report", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("PASS: stage18.5 offline comparator self-test")
        return
    if args.manifest:
        result = audit_manifest(args.manifest)
        report = json.dumps(result, indent=2, sort_keys=True)
        if args.report:
            args.report.write_text(report + "\\n")
        print(report)
        if not result["passed"]:
            raise SystemExit(1)
        return
    if not args.reference or not args.candidate or not args.width or not args.height:
        parser.error("reference, candidate, width and height are required")
    reference, candidate = args.reference.read_bytes(), args.candidate.read_bytes()
    if args.format == "rgba8":
        if not args.tolerance.is_integer():
            parser.error("RGBA8 tolerance must be an integer")
        result = compare_rgba(reference, candidate, args.width, args.height,
                              int(args.tolerance), args.roi)
    else:
        result = compare_depth(reference, candidate, args.width, args.height,
                               args.tolerance, args.roi)
    result["format"] = args.format
    result["reference"] = str(args.reference)
    result["candidate"] = str(args.candidate)
    result["passed"] = result["changed_pixels"] == 0
    report = json.dumps(result, indent=2, sort_keys=True)
    if args.report:
        args.report.write_text(report + "\n")
    print(report)
    if not result["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
