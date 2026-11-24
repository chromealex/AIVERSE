#!/usr/bin/env python3
"""Install the terrain PBR library (catalog package terrain-pbr) into an existing project.

Same as `python3 tools/pkg/aiverse_pkg.py add terrain-pbr --project P`: files go to
<project>/materials/terrain, the material library merges into materials.json (existing ids are
never overwritten), game.json "packages" and aiverse.lock record the version.
Use --default-checkerboard to also switch the project's terrain to the neutral checkerboard.
"""
from pathlib import Path
import argparse
import json
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pkg'))
import aiverse_pkg  # noqa: E402


def install(project, default=False):
    project = Path(project).resolve()
    r = aiverse_pkg.install_into(project, 'terrain-pbr', None, False, lambda s: print(s, file=sys.stderr))
    if default:
        game = json.loads((project / 'game.json').read_text())
        game['terrain'] = json.loads((project / r['mount'] / 'default-terrain.json').read_text())
        (project / 'game.json').write_text(json.dumps(game, indent=2) + '\n')
    print('Installed', r['merged'].get('materials.json', 0), 'PBR materials in', project)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('project', type=Path)
    ap.add_argument('--default-checkerboard', action='store_true')
    a = ap.parse_args()
    try:
        install(a.project, a.default_checkerboard)
    except aiverse_pkg.PkgError as e:
        sys.exit(f'install_pbr_pack: {e}')
