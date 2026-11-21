#!/usr/bin/env python3
"""AIVERSE VIVID Creator material A/B validation of native PPM screenshots.

Compare only the 3D viewport to the RIGHT of the Creator's ImGui DNA
inspector. A change in text labels or unsaved-revision UI is not evidence
of a changed eye/skin/groom shader.
"""
import sys
from pathlib import Path

def ppm(path: Path):
    raw = path.read_bytes()
    header = []
    cursor = 0
    for _ in range(3):
        end = raw.find(b"\n", cursor)
        if end < 0:
            raise ValueError(f"{path}: malformed PPM")
        header.append(raw[cursor:end])
        cursor = end + 1
    if header[0] != b"P6" or header[2] != b"255":
        raise ValueError(f"{path}: only RGB P6/255 supported")
    w, h = [int(x) for x in header[1].split()]
    pixels = raw[cursor:]
    if w < 600 or h < 400 or len(pixels) != w * h * 3:
        raise ValueError(f"{path}: unexpected screenshot dimensions")
    return w, h, pixels

def compare(first: Path, second: Path):
    aw, ah, a = ppm(first)
    bw, bh, b = ppm(second)
    if (aw, ah) != (bw, bh):
        raise ValueError("VIVID A/B screenshots differ in dimensions")
    # The floating DNA inspector occupies the left ~354 logical pixels.
    # Stay well clear of the entire inspector + window decoration.
    left = max(420, aw // 3)
    right = min(aw - 16, aw * 9 // 10)
    changed = 0
    samples = 0
    for y in range(ah // 8, ah * 7 // 8):
        for x in range(left, right):
            i = (y * aw + x) * 3
            difference = (abs(a[i] - b[i]) +
                          abs(a[i + 1] - b[i + 1]) +
                          abs(a[i + 2] - b[i + 2]))
            samples += 1
            if difference > 24:
                changed += 1
    print(f"VIVID human DNA/skin/eyes viewport A/B changed_pixels={changed} "
          f"samples={samples}")
    if changed < 400:
        raise ValueError("DNA preset did not produce a visible 3D material difference")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: compare_ppm.py neutral.ppm warm.ppm")
    try:
        compare(Path(sys.argv[1]), Path(sys.argv[2]))
    except Exception as exc:
        print(f"VIVID human appearance A/B FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
    print("VIVID human appearance A/B PASS")
