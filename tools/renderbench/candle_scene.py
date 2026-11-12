#!/usr/bin/env python3
"""Create separate day/night candle-lighting recipes without changing source assets."""
import argparse
import copy
import json
from pathlib import Path


def recipe(source, night):
    scene=copy.deepcopy(source)
    scene['id']+='-candle-night' if night else '-candle-day'
    scene['staticPointLightGrid']=True
    scene['staticLightCellSize']=1.0
    scene['lightingNote']='Static 3D point lights, finite-radius attenuation; no point-light occlusion or bounced GI. Artistic calibration, not matched UE settings.'
    for entry in scene['models']:
        if entry.get('lights'):
            entry.update(lightRadius=1.5,lightIntensityScale=0.4,lightColor=[1.0,0.48,0.16])
            entry.setdefault('materials',{})['Flame_MAT']={'emissive':[1.0,0.65,0.25],'emissiveStrength':12.0}
    if night:
        scene.update(sunIntensity=0.015,sunColor=[0.4,0.5,0.8],skyColor=[0.045,0.055,0.08],groundColor=[0.02,0.018,0.015],clearColor=[0.002,0.003,0.008])
    return scene


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);a=p.parse_args()
    source=json.loads(a.source.read_text())
    for night in (False,True):
        path=a.source.parent/('scene-candle-night.json' if night else 'scene-candle-day.json')
        path.write_text(json.dumps(recipe(source,night),indent=2)+'\n');print(path)
if __name__=='__main__':main()
