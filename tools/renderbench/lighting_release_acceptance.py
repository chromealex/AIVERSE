"""Reproducible Release arm64 lighting OFF/ON/OFF run; outputs stay in --output.
Measures submitted-frame CPU/GPU separately from loading and GI convergence.
Window pacing is not FPS; no speedup/production acceptance is inferred.
"""
import argparse
import copy
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--feature', choices=('dynamicGI', 'luma'), default='dynamicGI')
    parser.add_argument('--samples', type=int, default=120)
    args = parser.parse_args()
    if args.samples < 30:
        parser.error('at least 30 measured frames required')
    source = json.loads(args.scene.read_text())
    if args.feature not in source:
        parser.error('scene must configure the selected feature')
    args.output.mkdir(parents=True, exist_ok=True)
    # Resolve the top-level asset path before relocating generated recipes.
    if source.get('model') and not Path(source['model']).is_absolute():
        source['model'] = str((args.scene.parent / source['model']).resolve())
    for model in source.get('models', []):
        if isinstance(model, dict) and model.get('model') and not Path(model['model']).is_absolute():
            model['model'] = str((args.scene.parent / model['model']).resolve())
    results = []
    for label, enabled in (('A1-off', False), ('B-on', True), ('A2-off', False)):
        recipe = copy.deepcopy(source)
        if not enabled:
            recipe.pop(args.feature)
        config, report = args.output / (label + '.json'), args.output / (label + '-report.json')
        config.write_text(json.dumps(recipe, indent=2))
        if report.exists():
            raise RuntimeError('refusing stale/overwritten evidence: ' + str(report))
        command = [str(args.binary.resolve()), '--scene', str(config.resolve()), '--profile', 'hdr',
                   '--camera', '0', '--freeze-time', '0', '--warmup', '30', '--samples', str(args.samples),
                   '--output', str(report.resolve()), '--capture', str((args.output / (label + '.ppm')).resolve())]
        print('Starting', label, flush=True)
        start = time.monotonic()
        with (args.output / (label + '.log')).open('w') as log:
            run = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=600,
                                 env=dict(os.environ))
        if run.returncode or not report.exists():
            raise RuntimeError(label + ': benchmark failed, see log')
        data = json.loads(report.read_text())
        if not data.get('complete') or data.get('error') or data.get('build') != 'Release' or data.get('binaryArchitecture') != 'arm64':
            raise RuntimeError(label + ': incomplete or not Release arm64')
        if not data.get('gpuTimingSupported'):
            raise RuntimeError(label + ': real GPU timestamps unavailable')
        frames = data['frames']
        if len(frames) != args.samples:
            raise RuntimeError(label + ': missing measured frames')
        result = dict(label=label, report=str(report.resolve()), wallSeconds=time.monotonic()-start,
                      hardware=data['hardware'], revision=data['revision'],
                      loadAndUploadMs=data['loadAndUploadMs'],
                      dynamicGiBackend=data.get('dynamicGiBackend', 'off'),
                      dynamicGiFallback=data.get('dynamicGiFallback', ''),
                      dynamicGiTriangles=data.get('dynamicGiTriangles', 0),
                      dynamicGiConvergenceMs=data.get('dynamicGiConvergenceMs', 0),
                      lumaBackend=data.get('lumaBackend', 'off'),
                      lumaSources=data.get('lumaSources', 0), sceneTriangles=data.get('sceneTriangles', 0),
                      width=recipe.get('width'), height=recipe.get('height'))
        for metric in ('cpuEncodeMs', 'cpuFrameMs', 'gpuMs'):
            values = sorted(frame[metric] for frame in frames)
            if not all(isinstance(v, (float, int)) and math.isfinite(v) and v >= 0 for v in values):
                raise RuntimeError(label + ': invalid timing ' + metric)
            result[metric] = dict(mean=statistics.mean(values), p50=values[math.ceil(len(values)*.5)-1],
                                  p95=values[math.ceil(len(values)*.95)-1])
        results.append(result)
        print(json.dumps(result), flush=True)
        summary = dict(feature=args.feature, samples=args.samples, runs=results,
                       limitation='Submitted frames exclude load and convergence-only updates. Window pacing is not FPS; results do not prove production performance.')
        if len(results) == 3:
            summary['baselineGpuDriftPercent'] = 100*(results[2]['gpuMs']['p50']/max(results[0]['gpuMs']['p50'], 1e-9)-1)
        (args.output / 'summary.json').write_text(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
