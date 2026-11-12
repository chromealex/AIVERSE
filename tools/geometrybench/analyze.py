#!/usr/bin/env python3
"""Aggregate paired Bistro runs; retain individual-run medians."""
import json, statistics, sys
from pathlib import Path
root=Path(sys.argv[1]); rows=[]; count=int(sys.argv[2]) if len(sys.argv)>2 else 3
for camera in range(3):
    pair={}; reference=None
    for mode in ('off','on'):
        runs=[json.loads((root/f'bistro-run{run}-cam{camera}-{mode}.json').read_text()) for run in range(1,count+1)]
        for run in runs:
            settings={key:run[key] for key in ('source','sourceTriangles','parts','candidateParts','candidateTriangles','bakedTriangles','eye','target','fovY','device','width','height','maxPixels','keepMasked','bistroOrmMaterials','directXNormalsConverted')}
            if reference is None: reference=settings
            assert settings==reference, 'mismatched paired workload'
            assert len(run['frames'])==120 and run['width']==1280 and run['height']==720
            assert run['camera']==camera and run['proxyEnabled']==(mode=='on')
            assert all(f['gpuMs']>0 for f in run['frames'])
        pair[mode]={'runMedians':{key:[statistics.median(f[key] for f in run['frames']) for run in runs] for key in ('gpuMs','cpuEncodeMs','frameWallMs')},
                    'drawCalls':runs[0]['frames'][0]['drawCalls'], 'visibleInputTriangles':runs[0]['frames'][0]['visibleInputTriangles'],
                    'visibleProxyParts':runs[0]['frames'][0]['visibleProxyParts']}
        pair[mode]['median']={key:statistics.median(value) for key,value in pair[mode]['runMedians'].items()}
        pair[mode]['p95']={key:statistics.quantiles([f[key] for run in runs for f in run['frames']],n=100,method='inclusive')[94] for key in ('gpuMs','cpuEncodeMs')}
    pair['camera']=camera
    pair['deltaPercent']={key:100*(pair['on']['median'][key]/pair['off']['median'][key]-1) for key in pair['off']['median']}
    rows.append(pair)
result={'schema':1,'method':f'{count} paired repeats, 60 warmup + 120 samples each, alternating pair order per batch. Median of run medians; pooled inclusive p95. No uncapped FPS inference.', 'cameras':rows}
(root/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
