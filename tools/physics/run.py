#!/usr/bin/env python3
"""Repeat isolated Jolt workloads; retain raw measurements and check trajectory repeatability."""
import argparse
import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('binary', type=Path)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--repeats', type=int, default=3)
a = p.parse_args()
if not 2 <= a.repeats <= 20:
    p.error('--repeats must be in [2, 20]')
binary = a.binary.resolve()
subprocess.run([str(binary), 'verify', '1', '0'], check=True)
report = {'host': platform.platform(), 'measurement': 'wall-clock PhysicsSystem::Update only', 'jolt': '5.3.0',
          'revision': '0373ec0dd762e4bc2f6acdb08371ee84fa23c6db',
          'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
          'deterministic_build': True, 'warmup_ticks': {'spread': 60, 'pile': 60, 'sleep': 600},
          'measured_ticks': 600, 'tick_hz': 60, 'runs': [], 'summary': []}
a.output.parent.mkdir(parents=True, exist_ok=True)
for scene in ('spread', 'pile', 'sleep'):
    for count in (256, 1024, 4096):
        hashes = set()
        for workers in (0, 3):
            runs = []
            for repeat in range(a.repeats):
                result = subprocess.run([str(binary), scene, str(count), str(workers)], check=True, capture_output=True, text=True)
                row = json.loads(result.stdout)
                row['repeat'] = repeat
                report['runs'].append(row)
                runs.append(row)
                hashes.add(row['trajectory'])
            summary = {'scene': scene, 'bodies': count, 'workers': workers,
                       'median_mean_ms': statistics.median(r['mean_ms'] for r in runs),
                       'median_p95_ms': statistics.median(r['p95_ms'] for r in runs),
                       'median_active': statistics.median(r['mean_active'] for r in runs)}
            report['summary'].append(summary)
            a.output.write_text(json.dumps(report, indent=2) + '\n')
            print(json.dumps(summary), flush=True)
        if len(hashes) != 1:
            raise RuntimeError(f'Trajectory mismatch across repeats/worker counts: {scene}/{count}')
report['trajectory_repeatability'] = 'passed (same binary and host, all measured ticks, both worker counts)'
a.output.write_text(json.dumps(report, indent=2) + '\n')
