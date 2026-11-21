#!/usr/bin/env python3
"""Validate the fixed 960x640 backlit thin/thick Metal and GL fixture."""
import sys
from pathlib import Path


def pixels(path):
    with Path(path).open("rb") as image:
        assert image.readline() == b"P6\n"
        assert image.readline() == b"960 640\n"
        assert image.readline() == b"255\n"
        data = image.read()
    assert len(data) == 960 * 640 * 3
    return data


def center(data, x):
    values = (data[(y * 960 + xx) * 3 + channel]
              for y in range(300, 341) for xx in range(x - 20, x + 21)
              for channel in range(3))
    return sum(values) / (41 * 41 * 3)


def main(paths):
    assert len(paths) == 4, "Metal off/on and GL off/on required"
    metal_off, metal_on, gl_off, gl_on = map(pixels, paths)
    for name, off, on in (("Metal", metal_off, metal_on),
                          ("GL", gl_off, gl_on)):
        thin = center(on, 350) - center(off, 350)
        thick = center(on, 610) - center(off, 610)
        print(f"{name}: thin center gain={thin:.2f}, thick center gain={thick:.2f}")
        assert thin > 15, f"{name}: thin part did not transmit light"
        assert abs(thick) < 1, f"{name}: thick center incorrectly transmits light"
    for name, metal, gl in (("off", metal_off, gl_off),
                            ("on", metal_on, gl_on)):
        mean = sum(abs(a - b) for a, b in zip(metal, gl)) / len(metal)
        print(f"Metal/GL {name}: mean channel delta={mean:.3f}")
        assert mean < 1, f"Metal/GL {name} mismatch"
    print("SSS backlight fixture PASS")


if __name__ == "__main__":
    main(sys.argv[1:])
