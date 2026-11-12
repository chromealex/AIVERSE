#!/usr/bin/env python3
"""Compare settled cloth with/without sleeping; run without concurrent builds/benchmarks."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--objects', type=int, default=512)
    parser.add_argument('--cells', type=int, default=71)
    parser.add_argument('--warmup', type=int, default=1200)
    parser.add_argument('--frames', type=int, default=120)
    parser.add_argument('--repeats', type=int, default=2)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error('--repeats must be positive')
    runs = []
    with tempfile.TemporaryDirectory(prefix='aiverse-cloth-sleep-') as temp:
        for repeat in range(args.repeats):
            for sleep in (0, 1):
                report = Path(temp) / 'frame.json'
                command = [str(args.binary.resolve()), '--objects', str(args.objects),
                           '--cells', str(args.cells), '--iterations', '4', '--gpu', '1',
                           '--share', '0', '--calm', '1', '--sleep', str(sleep),
                           '--lod', '0', '--auto-lod', '0', '--warmup', str(args.warmup),
                           '--frames', str(args.frames), '--report', str(report)]
                process = subprocess.run(command, env={**os.environ, 'AIVERSE_ADAPTIVE': 'off'},
                                         capture_output=True, text=True, check=True)
                data = json.loads(report.read_text())
                if not data['gpu'] or not data['healthy']:
                    raise RuntimeError('Benchmark needs a healthy compute-capable GPU')
                runs.append({'name': f'gpu-calm-sleep{sleep}-r{repeat+1}',
                             'command': command[1:-2], 'result': data, 'stderr': process.stderr})
                print(f"sleep={sleep}: {data['sleeping_objects']} asleep, "
                      f"GPU frame {data['gpu_frame_ms']['mean']:.3f} ms", flush=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({'description': 'Independent cloth, constant gravity, zero wind, '
                                   'no capsules, floor retained. GPU frame includes rendering.',
                                   'runs': runs}, indent=2) + '\n')


if __name__ == '__main__':
    main()
