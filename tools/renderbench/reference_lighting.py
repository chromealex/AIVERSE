#!/usr/bin/env python3
"""Create daylight and architectural night lighting variants without candle assets."""
import argparse
import copy
import json
from pathlib import Path


def recipe(source, night):
    scene=copy.deepcopy(source)
    scene['models']=[entry for entry in scene['models'] if any(x in entry['path'].lower() for x in ('curtains','ivy'))]
    scene['id']+='-architectural-night' if night else '-architectural-day'
    scene['pointLights']=[]
    scene['staticPointLightGrid']=night
    if night:
        scene.update(sunIntensity=.008,sunColor=[.45,.5,.65],skyColor=[.10,.085,.075],
                     groundColor=[.025,.018,.015],clearColor=[.001,.002,.004],
                     fogDensity=.0005,fogColor=[.08,.06,.05],staticLightCellSize=1)
        scene['postfx']={'hdr':'on','effects':[{'type':'bloom','threshold':1.5,'intensity':.12},
                                            {'type':'colorGrade','exposure':.15,'saturation':.9}]}
        def light(position,radius,intensity,color):
            scene['pointLights'].append(dict(position=position,radius=radius,intensity=intensity,color=color))
        for x in (-6,-2,2,6):
            for z in (-3.6,3.6):light([x,7,z],3.8,10,[1,.48,.16])
        light([17,2.5,0],5,10,[1,.62,.3])
        light([12.5,3.5,0],4.5,4,[1,.58,.28])
        light([8,6.5,0],4.5,5,[1,.5,.18])
        light([7,12,0],7,3,[1,.2,.14])
        light([-6,3.6,-4],3,5,[1,.45,.14])
        light([3,.8,0],7,.4,[1,.18,.06])
    scene['lightingNote']='Architectural reference study, no candle geometry or candle lights. Night uses 14 finite-radius point lights without point-shadow occlusion; not a recreation of Unreal GI.'
    return scene

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=Path)
    args=parser.parse_args();source=json.loads(args.source.read_text())
    targets=[args.source.parent/f'scene-reference-{mode}-lit.json' for mode in ('day','night')]
    if any(p.exists() for p in targets):parser.error('output recipe already exists')
    for night,path in zip((False,True),targets):
        with path.open('x') as f:json.dump(recipe(source,night),f,indent=2);f.write('\n')
        print(path)
