#!/usr/bin/env python3
"""Audit native Terra exported tile seams and shared integer height samples."""
import argparse
import json
import math
import struct
from pathlib import Path


def audit(root):
    catalog = json.loads((root / 'catalog.json').read_text())
    ground = json.loads((root / 'ground.json').read_text())
    boundary = {}
    matches = samples = 0
    for tile in catalog['terrain']:
        path = root / tile['file']
        doc = json.loads(path.read_text())
        buffers = [(path.parent / b['uri']).read_bytes() for b in doc['buffers']]

        def vectors(index):
            a = doc['accessors'][index]
            v = doc['bufferViews'][a['bufferView']]
            assert a['type'] == 'VEC3' and a['componentType'] == 5126
            offset = v.get('byteOffset', 0) + a.get('byteOffset', 0)
            stride = v.get('byteStride', 12)
            return [struct.unpack_from('<3f', buffers[v['buffer']], offset + i * stride)
                    for i in range(a['count'])]

        primitive = doc['meshes'][0]['primitives'][0]
        positions = vectors(primitive['attributes']['POSITION'])
        normals = vectors(primitive['attributes']['NORMAL'])
        for (x, y, z), normal in zip(positions, normals):
            assert all(math.isfinite(v) for v in (x, y, z, *normal))
            assert abs(sum(v * v for v in normal) - 1) < .002
            wx, wz = tile['xCm'] + round(x * 100), tile['zCm'] + round(z * 100)
            if x in (0, 64) or z in (0, 64):
                key = wx, wz
                previous = boundary.get(key)
                value = (tile['file'], y, normal)
                if previous and previous[0] != tile['file']:
                    assert abs(previous[1] - y) < .0001, (key, 'height seam')
                    assert max(abs(a - b) for a, b in zip(previous[2], normal)) < .00001, (key, 'normal seam')
                    matches += 1
                boundary[key] = value
            gx, gz = wx - ground['originXcm'], wz - ground['originZcm']
            step = ground['stepCm']
            if gx % step == 0 and gz % step == 0:
                expected = ground['heightCm'][(gz // step) * ground['columns'] + gx // step]
                # cm -> Q16 metres -> float32 metres: account for both
                # known quantization steps, including tall/negative terrain.
                tolerance = 100 / 65536 + abs(y) * 100 * 2 ** -24 + .000001
                assert abs(y * 100 - expected) <= tolerance, (wx, wz, 'geometry/ground disagreement')
                samples += 1
    assert matches > 0 and samples > 0
    print('PASS: %d tiles; %d shared boundary vertices; %d integer ground checks' %
          (len(catalog['terrain']), matches, samples))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    audit(parser.parse_args().root)
