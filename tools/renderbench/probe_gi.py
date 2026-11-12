#!/usr/bin/env python3
"""Create three comparable probe-GI scene recipes without changing the source."""
import argparse
import copy
import json
from pathlib import Path


def variants(source):
    scene = copy.deepcopy(source)
    scene['staticPointLightGrid'] = True
    sun = [float(c) * float(scene.get('sunIntensity', 0)) * (2 / 3)
           for c in scene.get('sunColor', [1, 1, 1])]
    scene['probeGI'] = dict(spacing=3, samples=64, distance=60,
                            sun=sun, sky=[.08, .09, .12])
    for quality in ('off', 'mobile', 'high'):
        result = copy.deepcopy(scene)
        result['probeGI']['quality'] = quality
        yield quality, result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    recipes = [(args.source.with_name(args.source.stem + '-gi-' + quality + '.json'), scene)
               for quality, scene in variants(json.loads(args.source.read_text()))]
    if any(path.exists() for path, _ in recipes):
        parser.error('output recipe already exists')
    for path, scene in recipes:
        with path.open('x') as stream:
            json.dump(scene, stream, indent=2)
            stream.write('\n')
        print(path)
