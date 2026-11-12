#!/usr/bin/env python3
"""Create opt-in daylight calibration or static night bounce beside an existing recipe."""
import argparse
import copy
import json
from pathlib import Path


def refine(source, mode):
    scene = copy.deepcopy(source)
    if mode in ('day', 'day-occlusion', 'day-bounce'):
        scene.update(sunDirection=[-.95, 1, -.12], sunIntensity=9,
                     fogDensity=.0035, fogColor=[.46, .5, .58])
        scene['cameras'][0]['target'][2] = -1.6
        for effect in scene['postfx']['effects']:
            if effect['type'] == 'colorGrade':
                effect.update(saturation=.75, exposure=.2)
        scene['referenceNote'] = 'Manual daylight reference study, not recovered Unreal settings. Foreground remains shaded; indirect lighting still differs.'
        if mode in ('day-occlusion', 'day-bounce'):
            scene['sunDirection'] = [-.492404, .866025, -.086824]
            scene['staticPointLightGrid'] = True
            scene['probeGI'] = dict(quality='mobile', spacing=1.5, samples=128, distance=60,
                                    sun=[6, 5.76, 5.28], sky=[.08, .09, .12])
            scene['referenceNote'] = 'Daylight shadow study: full scene shadow bounds and restored sun texture after SSR required. Approximate reference, not pixel-identical.'
        if mode == 'day-bounce':
            scene['probeGI'].update(quality='high', bounces=3, sky=[30, 25, 18])
            scene['referenceNote'] = 'Warm indirect daylight reference study: three baked sun/sky bounces, 32 sky rays, filtered high-quality probe visibility. Exposure and direct sun unchanged. Artistic sky radiance; not recovered Unreal values.'
    else:
        scene.update(staticPointLightGrid=True, staticPointShadows=True,
                     pointShadowResolution=128,
                     staticPointBounce=dict(samples=16, radius=8, strength=1.5, spacing=2))
        scene['lightingNote'] = 'Static opaque point shadows and one approximate diffuse bounce; no candle assets, dynamic GI or sun bounce.'
    return scene


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--mode', choices=['day', 'day-occlusion', 'day-bounce', 'night'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.source.resolve().parent != args.output.resolve().parent:
        parser.error('output must stay beside the source recipe for relative assets')
    if args.output.exists():
        parser.error('output already exists')
    result = refine(json.loads(args.source.read_text()), args.mode)
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
