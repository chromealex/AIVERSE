#!/usr/bin/env python3
"""Build a separate Sponza candle-instancing/auto-LOD recipe; preserve the reference scene."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',required=True,type=Path)
    p.add_argument('--reinstance',required=True,type=Path)
    p.add_argument('--worker',required=True,type=Path)
    a=p.parse_args();root=a.assets.resolve()
    source=root/'pkg_d_10k_candles/NewSponza_4_Combined_glTF.gltf'
    output=root/'optimized/candles.gltf'
    subprocess.run([str(a.reinstance.resolve()),str(source),str(output),'PointLightInstances'],check=True)
    subprocess.run([sys.executable,str(Path(__file__).parents[1]/'models/autolod.py'),str(output),str(root/'optimized/candles-lod.gltf'),'--worker',str(a.worker.resolve())],check=True)
    files=[]
    for name in ('candles.gltf','candles.bin','candles.audit.json','candles-lod.gltf','candles-lod.lods.bin','candles-lod.lods.json'):
        path=root/'optimized'/name
        files.append({'path':path.relative_to(root).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    fingerprint=hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    (root/'optimized/manifest.json').write_text(json.dumps({'files':files,'fingerprint':fingerprint},indent=2)+'\n')
    for mode,lod in (('instanced',False),('autolod',True)):
        config=json.loads((root/'scene.json').read_text())
        config['id']+='-'+mode
        config['sourceAssetFingerprint']=config.pop('assetFingerprint',None)
        config.pop('assetLock',None)
        config['derivedAssetFingerprint']=fingerprint
        config['assetVerificationNote']='experimental derived workload; source and derived manifests must both be checked'
        matched=0
        for model in config['models']:
            if model['path']==source.relative_to(root).as_posix():
                model['path']='optimized/candles-lod.gltf';matched+=1
        if matched!=1:raise ValueError('expected exactly one candle model in source scene')
        config['modelOptimization']={'lod':lod,'culling':True,'instancing':True}
        (root/f'scene-{mode}.json').write_text(json.dumps(config,indent=2)+'\n')

if __name__=='__main__':main()
