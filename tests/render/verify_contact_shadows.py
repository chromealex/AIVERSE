#!/usr/bin/env python3
"""Check rendered ContactShadowScene captures, not shader source/formulas.

Usage: python3 tests/render/verify_contact_shadows.py CAPTURE_DIR
Expected PPMs: contact-{new,fixed,gl-new,medium,low,low-fixed}.ppm
"""
from pathlib import Path
import sys


def read(path):
    with path.open('rb') as stream:
        assert stream.readline().strip() == b'P6', path
        width, height = map(int, stream.readline().split())
        assert stream.readline().strip() == b'255', path
        pixels = stream.read()
    assert (width, height) == (960, 640), (path, width, height)
    assert len(pixels) == width * height * 3, path
    return width, pixels


def edge_width(image, position):
    width, pixels = image
    values = []
    for t in range(-20, 21):
        x, y = int(position[0] + t * .535), int(position[1] + t * .845)
        offset = (y * width + x) * 3
        values.append(sum(pixels[offset:offset + 3]) / 3)
    low, high = min(values), max(values)
    assert high - low > 40, 'Missing shadow contrast'
    return sum(low + .1 * (high - low) < v < low + .9 * (high - low) for v in values)


def main():
    folder = Path(sys.argv[1])
    positions = [(320, 421), (380, 384), (450, 340)]
    results = {}
    for name in ('new', 'fixed', 'gl-new', 'medium'):
        results[name] = [edge_width(read(folder / f'contact-{name}.ppm'), p) for p in positions]
        print(name, 'near/middle/far transition pixels:', results[name])
    for name in ('new', 'gl-new', 'medium'):
        near, middle, far = results[name]
        assert 0 < near < middle < far, (name, results[name])
        assert near <= results['fixed'][0] / 2, name
    assert read(folder / 'contact-low.ppm') == read(folder / 'contact-low-fixed.ppm'), 'Low changed'
    print('PASS: contact hardening on Metal/GL, progressive medium/high softness, unchanged low')


if __name__ == '__main__':
    main()
