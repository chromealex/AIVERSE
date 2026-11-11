#!/usr/bin/env python3
"""Create the daylight Sponza reference composition without modifying source assets."""
import argparse
import json
from pathlib import Path

def recipe(source):
    scene=json.loads(json.dumps(source))
    scene['id']='sponza-day-reference-v1'
    scene['models']=[entry for entry in scene['models'] if any(name in entry['path'].lower() for name in ('curtains','ivy'))]
    scene.pop('materialGraphs',None)
    scene['staticPointLightGrid']=False
    scene['lightingNote']='Daylight reference study: sun, hemisphere ambient and screen-space GI; no candle lights.'
    scene.update(width=1280,height=720,sunDirection=[-.6,1,.05],sunIntensity=8,
                 sunColor=[1,.96,.88],skyColor=[.32,.34,.4],groundColor=[.14,.1,.06],
                 fogDensity=.0015,fogColor=[.55,.59,.68])
    scene['cameras']=[dict(name='reference',eye=[-11.5,2.1,-1.5],target=[9,6.2,-1],fovYDegrees=49)]
    scene['mainNormalScale']=.45
    scene['postfx']={'hdr':'on','effects':[{'type':'bloom','threshold':2,'intensity':.12},
                                        {'type':'colorGrade','saturation':.8,'exposure':.1,'temperature':.04}]}
    scene['referenceNote']='Manual match to supplied daylight frame; original camera, exposure and GI settings unavailable. Not a pixel-identical recreation.'
    return scene

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.resolve().parent!=args.source.resolve().parent:
        parser.error('output must remain beside the source scene to preserve relative asset paths')
    with args.output.open('x') as f:
        json.dump(recipe(json.loads(args.source.read_text())),f,indent=2);f.write('\n')
