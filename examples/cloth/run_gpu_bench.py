#!/usr/bin/env python3
"""CPU / Metal compute comparison with identical rendered geometry and explicit solver budgets."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser()
    p.add_argument('binary', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=120)
    p.add_argument('--warmup', type=int, default=60)
    p.add_argument('--repeats', type=int, default=2)
    p.add_argument('--objects', type=int, default=512)
    a = p.parse_args()
    env = dict(os.environ, AIVERSE_ADAPTIVE='off')
    env.pop('MTL_DEBUG_LAYER', None)
    rows = []
    with tempfile.TemporaryDirectory(prefix='aiverse-cloth-gpu-') as tmp:
        report = Path(tmp) / 'report.json'
        for scenario, share, groups, lod in [('independent', 0, a.objects, 0), ('shared16', 1, 16, 0), ('far16', 1, 16, 1)]:
            for repeat in range(a.repeats):
                for gpu, iterations in [(0, 4), (1, 4), (1, 8)]:
                    cmd = [str(a.binary.resolve()), '--objects', str(a.objects), '--cells', '71', '--workers', '8',
                           '--render-cells', '71', '--frames', str(a.frames), '--warmup', str(a.warmup),
                           '--gpu', str(gpu), '--iterations', str(iterations), '--share', str(share),
                           '--wind-groups', str(groups), '--lod', str(lod), '--auto-lod', '0',
                           '--camera-distance', '200', '--report', str(report)]
                    subprocess.run(cmd, env=env, check=True, capture_output=True, text=True)
                    row = json.loads(report.read_text())
                    if row['gpu'] != bool(gpu) or not row['healthy'] or row['frames'] != a.frames:
                        raise RuntimeError('benchmark fallback or invalid simulation: ' + str(row))
                    row.update(scenario=scenario, repeat=repeat, command=cmd[:-2])
                    rows.append(row)
                    print(scenario, repeat, 'gpu' if gpu else 'cpu', iterations,
                          'frame', row['frame_interval_ms']['mean'], 'GPU', row['gpu_frame_ms']['mean'],
                          'CPU', row['simulation_ms']['mean'], 'stretch', row['max_stretch_ratio'], flush=True)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(rows, indent=2) + '\n')


if __name__ == '__main__':
    main()
