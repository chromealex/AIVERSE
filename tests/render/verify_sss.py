#!/usr/bin/env python3
"""Check the opt-in SSS HDR fixture captures from Metal and GL.

Usage: verify_sss.py metal-off.ppm metal-on.ppm gl-off.ppm gl-on.ppm
The runner sets AIVERSE_SSS_TEST=0 / 0.85 and AIVERSE_HDR_CAPTURE.
"""
import sys
from pathlib import Path


def read_ppm(path):
    with Path(path).open("rb") as image:
        assert image.readline() == b"P6\n", path
        width, height = map(int, image.readline().split())
        assert image.readline() == b"255\n", path
        pixels = image.read()
    assert (width, height) == (960, 640), (path, width, height)
    assert len(pixels) == width * height * 3, path
    return pixels


def difference(before, after):
    changed = []
    total = 0
    for offset in range(0, len(before), 3):
        delta = [abs(before[offset + channel] - after[offset + channel])
                 for channel in range(3)]
        total += sum(delta)
        if max(delta) > 3:
            changed.append((offset // 3 % 960, offset // 3 // 960))
    assert changed, "SSS did not change any pixels"
    bounds = (min(x for x, _ in changed), min(y for _, y in changed),
              max(x for x, _ in changed), max(y for _, y in changed))
    return len(changed), total / len(before), bounds


def main(paths):
    assert len(paths) == 4, "expected Metal off/on and GL off/on captures"
    metal_off, metal_on, gl_off, gl_on = map(read_ppm, paths)
    metal = difference(metal_off, metal_on)
    gl = difference(gl_off, gl_on)
    for name, (count, mean, bounds) in (("Metal", metal), ("GL", gl)):
        print(f"{name}: changed={count} mean_channel_delta={mean:.3f} bbox={bounds}")
        assert 5000 < count < 30000, f"{name}: SSS change outside fixture range"
        assert 250 <= bounds[0] and 150 <= bounds[1]
        assert bounds[2] <= 750 and bounds[3] <= 380, (
            f"{name}: SSS changed pixels outside the models/reflection")
    assert abs(metal[0] - gl[0]) < 500, "Metal/GL SSS coverage differs"
    for name, metal_pixels, gl_pixels in (
            ("off", metal_off, gl_off), ("on", metal_on, gl_on)):
        mean = sum(abs(a - b) for a, b in zip(metal_pixels, gl_pixels)) / len(metal_pixels)
        print(f"Metal/GL {name}: mean_channel_delta={mean:.3f}")
        assert mean < 1.0, f"Metal/GL {name} image parity differs"
    print("SSS HDR fixture PASS")


if __name__ == "__main__":
    main(sys.argv[1:])
