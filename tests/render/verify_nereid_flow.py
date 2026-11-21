#!/usr/bin/env python3
"""Real native GPU regression: river bend, camera invariance, current advection.
Usage: python3 verify_nereid_flow.py /absolute/renderbench /KINGSTON/output
Run separately for Metal and desktop GL; requires an unlocked native GPU session.
No engine/save mutation and no claim of whole-scene GPU performance.
"""
import base64
import copy
import json
import math
import os
import re
from pathlib import Path
import struct
import subprocess
import sys


def ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        width, height = map(int, line.split())
        assert int(f.readline()) == 255
        data = f.read()
    assert len(data) == width * height * 3
    return width, height, data


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def unit(a):
    length = math.sqrt(dot(a, a))
    return [x/length for x in a]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def pixel(image, camera, point):
    width, height, data = image
    forward = unit(sub(camera['target'], camera['eye']))
    right = unit(cross(forward, [0, 1, 0]))
    up = cross(right, forward)
    relative = sub(point, camera['eye'])
    scale = height / (2*dot(relative, forward)*math.tan(math.radians(55)/2))
    x = int(width/2+scale*dot(relative, right))
    y = int(height/2-scale*dot(relative, up))
    assert 2 <= x < width-2 and 2 <= y < height-2
    return tuple(sorted(data[((y+dy)*width+x+dx)*3+c] for dy in range(-2, 3)
                        for dx in range(-2, 3))[12] for c in range(3))


def main():
    binary, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Embedded, portable ground fixture; no dependency on a local showcase.
    vertices = [-30, -2, -15, 50, -2, -15, 50, -2, 55, -30, -2, 55]
    raw = struct.pack('<12f', *vertices)+struct.pack('<6H', 0, 2, 1, 0, 3, 2)
    ground = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
              'nodes': [{'mesh': 0}], 'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1}]}],
              'buffers': [{'byteLength': len(raw), 'uri': 'data:application/octet-stream;base64,'+base64.b64encode(raw).decode()}],
              'bufferViews': [{'buffer': 0, 'byteLength': 48}, {'buffer': 0, 'byteOffset': 48, 'byteLength': 12}],
              'accessors': [{'bufferView': 0, 'componentType': 5126, 'count': 4, 'type': 'VEC3', 'min': [-30, -2, -15], 'max': [50, -2, 55]},
                            {'bufferView': 1, 'componentType': 5123, 'count': 6, 'type': 'SCALAR'}]}
    (output/'ground.gltf').write_text(json.dumps(ground))
    cameras = [{'name': 'A', 'eye': [0, 60, 21], 'target': [0, 0, 20], 'fovYDegrees': 55},
               {'name': 'B', 'eye': [25, 55, 30], 'target': [3, 0, 20], 'fovYDegrees': 55}]
    scene = {'model': str(output/'ground.gltf'), 'width': 960, 'height': 720,
             'near': .1, 'far': 200, 'cameras': cameras, 'sunDirection': [-.6, .8, -.4],
             'sunColor': [1, 1, 1], 'sunIntensity': 1, 'skyColor': [.4, .5, .6],
             'groundColor': [.1, .1, .1], 'fogDensity': 0,
             'nereidWater': {'surfaces': [{'id': 1, 'kind': 'river', 'shape': 'river',
                 'surfaceY': 2, 'bottomY': -2, 'points': [[0, 0, 6], [0, 20, 6], [12, 40, 6]],
                 'flow': [0, 0, -1], 'lod': 'near', 'foamWidth': 0,
                 'style': {'rapidFoamStrength': .5, 'rapidFlowThreshold': .2, 'rapidNoiseScale': .7, 'rapidDetail': 'high'},
                 'grade': {'slopeDegrees': math.degrees(math.atan(.02)), 'downhillHeadingDegrees': 180,
                           'automaticCurrent': True, 'responseSeconds': 5, 'maxSpeed': 5}}]}}
    # Exercise the real showcase's nonzero world coordinates. Rotating world
    # coordinates by a local tangent used to produce origin-amplified foam tears.
    ox, oz = 256, 128
    for i in range(0, len(vertices), 3):
        vertices[i] += ox
        vertices[i+2] += oz
    raw = struct.pack('<12f', *vertices)+struct.pack('<6H', 0, 2, 1, 0, 3, 2)
    ground['buffers'][0]['uri'] = 'data:application/octet-stream;base64,'+base64.b64encode(raw).decode()
    ground['accessors'][0].update(min=[-30+ox, -2, -15+oz], max=[50+ox, -2, 55+oz])
    (output/'ground.gltf').write_text(json.dumps(ground))
    for camera in cameras:
        for key in ['eye', 'target']:
            camera[key][0] += ox
            camera[key][2] += oz
    surface = scene['nereidWater']['surfaces'][0]
    surface['grade']['origin'] = [ox, oz]
    for point in surface['points']:
        point[0] += ox
        point[1] += oz
    (output/'scene.json').write_text(json.dumps(scene))
    images = {}
    for mode in ['flow', 'normal']:
        for camera in [0, 1]:
            for time in [0, 1]:
                name = f'{mode}-c{camera}-t{time}'
                env = dict(os.environ, AIVERSE_BENCH_QUICK='1')
                env.pop('AIVERSE_NEREID_DIAGNOSTIC', None)
                if mode == 'flow':
                    env['AIVERSE_NEREID_DIAGNOSTIC'] = 'flow'
                with (output/(name+'.log')).open('w') as log:
                    result = subprocess.run([str(binary), '--scene', str(output/'scene.json'), '--output', str(output/(name+'.json')),
                        '--capture', str(output/(name+'.ppm')), '--warmup', '2', '--samples', '1', '--camera', str(camera),
                        '--freeze-time', str(time), '--profile', 'low'], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
                report = json.loads((output/(name+'.json')).read_text())
                assert result.returncode == 0 and report.get('complete') and not report.get('error'), name
                text = (output/(name+'.log')).read_text()
                assert 'GL frame error' not in text, name
                assert all(int(value) == 0 for value in re.findall(r'pipelineFailures=(\d+)', text)), name
                images[mode, camera, time] = ppm(output/(name+'.ppm'))
                print(name, 'render PASS', flush=True)
    # Low/LDR diagnostic writes direct RGB = (vx*.1+.5,vz*.1+.5,speed*.2).
    points = [[ox, 2.2, oz+10], [ox+6, 2.6, oz+30]]
    measured = []
    for point in points:
        samples = [pixel(images['flow', camera, time], cameras[camera], point)
                   for camera in [0, 1] for time in [0, 1]]
        assert all(max(abs(a-b) for a, b in zip(samples[0], s)) <= 1 for s in samples), samples
        measured.append(samples[0])
    assert measured[0][0] >= 127 and measured[1][0] <= 119, measured
    assert measured[0][1] < measured[1][1], measured  # tangent turns at the bend
    # Authored current animates actual shaded water, independently of the observer.
    for camera in [0, 1]:
        a, b = images['normal', camera, 0][2], images['normal', camera, 1][2]
        assert sum(abs(x-y) for x, y in zip(a, b)) > 1000, 'water remained static'
    extra = {}
    for variant in ['far', 'manual', 'foam-off']:
        changed = copy.deepcopy(scene)
        surface = changed['nereidWater']['surfaces'][0]
        if variant == 'far':
            surface['lod'] = 'far'
        elif variant == 'foam-off':
            surface['style']['rapidFoamStrength'] = 0
        else:
            surface['grade']['automaticCurrent'] = False
            surface['flow'] = [-2, 0, 1]
        path = output/(variant+'.json')
        path.write_text(json.dumps(changed))
        env = dict(os.environ, AIVERSE_BENCH_QUICK='1', AIVERSE_NEREID_DIAGNOSTIC='flow')
        if variant == 'foam-off':
            env.pop('AIVERSE_NEREID_DIAGNOSTIC', None)
        report_path = output/(variant+'-report.json')
        with (output/(variant+'.log')).open('w') as log:
            result = subprocess.run([str(binary), '--scene', str(path), '--output', str(report_path),
                '--capture', str(output/(variant+'.ppm')), '--warmup', '2', '--samples', '1',
                '--camera', '0', '--freeze-time', '0', '--profile', 'low'], env=env,
                stdout=log, stderr=subprocess.STDOUT, timeout=120)
        report = json.loads(report_path.read_text())
        assert result.returncode == 0 and report.get('complete') and not report.get('error'), variant
        text = (output/(variant+'.log')).read_text()
        assert 'GL frame error' not in text and all(int(v) == 0 for v in re.findall(r'pipelineFailures=(\d+)', text)), variant
        sample = [pixel(ppm(output/(variant+'.ppm')), cameras[0], point) for point in points]
        if variant == 'far':
            assert sample == measured, (variant, sample, measured)
        elif variant == 'manual':
            assert all(max(abs(a-b) for a, b in zip(rgb, [77, 153, 114])) <= 1 for rgb in sample), sample
        else:
            actual = ppm(output/(variant+'.ppm'))[2]
            assert sum(abs(a-b) for a, b in zip(actual, images['normal', 0, 0][2])) > 1000, 'foam toggle did not affect pixels'
        extra[variant] = sample
        print(variant, 'PASS', sample, flush=True)
    (output/'verification.json').write_text(json.dumps({'pass': True, 'binary': str(binary), 'worldFlowRGB': measured, 'extra': extra}, indent=2))
    print('PASS: local bend, camera/time-invariant velocity, animated water', measured)


if __name__ == '__main__':
    main()
