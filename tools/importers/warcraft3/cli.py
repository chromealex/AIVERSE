#!/usr/bin/env python3
"""One-command Warcraft III import for a native AIVERSE project.

python3 tools/importers/warcraft3/cli.py --source MyMap.w3x \
    --project /path/to/project --game-data /path/to/extracted/Warcraft3

Imports original data to content/Warcraft3/<map>, then installs model-backed
placements as MapDoc entities and adds the generated rules include. If resources
are missing they are reported and never replaced with misleading placeholders.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys


HERE = Path(__file__).resolve().parent


def _execute(script, args):
    result = subprocess.run([sys.executable, str(HERE / script)] + args,
                            capture_output=True, text=True)
    try:
        info = json.loads(result.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        info = {'ok': False, 'error': result.stdout.strip() or result.stderr.strip()}
    if result.returncode != 0:
        info['ok'] = False
    return info


def run(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--project', type=Path, required=True)
    ap.add_argument('--game-data', type=Path, help='user-owned extracted Warcraft data')
    ap.add_argument('--model-map', type=Path, help='rawcode/model mapping JSON')
    ap.add_argument('--assets-only', action='store_true', help='do not register a scene')
    ap.add_argument('--strict', action='store_true', help='abort on unknown formats or objects')
    args = ap.parse_args(argv)
    project = str(args.project.expanduser().resolve())
    first = _execute('import.py', ['--source', str(args.source.expanduser()), '--out', project] +
                     (['--strict'] if args.strict else []))
    if not first.get('ok'):
        print(json.dumps({'ok': False, 'phase': 'extract', 'detail': first}, ensure_ascii=False))
        return 2
    if args.assets_only:
        print(json.dumps({'ok': True, 'phase': 'assets', 'extract': first}, ensure_ascii=False))
        return 0
    cmd = ['--project', project, '--imported', str(first['target'])]
    if args.game_data:
        cmd.extend(['--game-data', str(args.game_data.expanduser())])
    if args.model_map:
        cmd.extend(['--model-map', str(args.model_map.expanduser())])
    if args.strict:
        cmd.append('--strict')
    second = _execute('install_scene.py', cmd)
    if not second.get('ok'):
        print(json.dumps({'ok': False, 'phase': 'scene', 'detail': second,
                          'assetsPreservedAt': first['target']}, ensure_ascii=False))
        return 2
    print(json.dumps({'ok': True, 'phase': 'scene', 'extract': first,
                      'scene': second}, ensure_ascii=False))
    return 0


if __name__ == '__main__':
    sys.exit(run())
