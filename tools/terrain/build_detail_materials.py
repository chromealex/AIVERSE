#!/usr/bin/env python3
"""Package shared 1K/4m Terra PBR atlases and native material overrides.

Run bake_landscape_pbr.py --detail first. Output stays in the exported pack;
the installer applies detail-materials.json through existing material APIs.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from terrain_detail_graph import material_graph


def build(root, materials, asset_root, metres=4.0, transparency=.12, sand_layer="sand", secondary_sand=None, rock_layer="rock", secondary_rock=None, height_normals=False):
    if sand_layer not in ("sand","river_sand","pebble_sand"):
        raise ValueError("sand layer: sand|river_sand|pebble_sand")
    if secondary_sand not in (None,"sand","river_sand","pebble_sand"):
        raise ValueError("secondary sand: sand|river_sand|pebble_sand")
    if secondary_sand==sand_layer:
        raise ValueError("secondary sand must differ from primary")
    if rock_layer not in ('rock','granite_rock','schist_rock') or secondary_rock not in (None,'rock','granite_rock','schist_rock'):
        raise ValueError('rock family: rock|granite_rock|schist_rock')
    if secondary_rock==rock_layer or (secondary_rock and not height_normals):
        raise ValueError('distinct secondary rock requires --height-normals')
    layers=['grass',sand_layer]+([secondary_sand] if secondary_sand else [])+[rock_layer]+([secondary_rock] if secondary_rock else [])+['snow']
    columns=3 if len(layers)>4 else 2
    reliefs={e['id']:e['heightMeters'] for e in json.loads((materials.parent/'catalog.json').read_text())['materials']} if height_normals else {}
    height_metres=max(reliefs[name] for name in layers) if height_normals else .04
    size, gutter = 1024, 32
    cell = size + 2 * gutter
    paths = {}
    for channel in (('basecolor','orm') if height_normals else ('basecolor', 'normal', 'orm')):
        atlas = np.zeros((2 * cell, columns * cell, 4 if height_normals and channel=="basecolor" else 3), np.uint8)
        for i, name in enumerate(layers):
            image = Image.open(materials / f'{name}_{channel}_repeat.png').convert('RGB')
            if image.size != (size, size):
                raise ValueError(f'{name}/{channel}: expected native 1024px PBR source')
            data=np.asarray(image)
            if height_normals and channel=='basecolor':
                h=np.asarray(Image.open(materials/f'{name}_height_repeat.png'),dtype=np.float64)/65535.0
                if h.shape!=(size,size):raise ValueError('height shape differs from albedo')
                alpha=np.rint(np.clip(.5+(h-.5)*reliefs[name]/height_metres,0,1)*255).astype(np.uint8)
                data=np.dstack([data,alpha])
            padded = np.pad(data, ((gutter, gutter), (gutter, gutter), (0, 0)), mode='wrap')
            y, x = i // columns * cell, i % columns * cell
            atlas[y:y+cell, x:x+cell] = padded
        filename = 'detail_heightcolor.png' if height_normals and channel=='basecolor' else f'detail_{channel}.png'
        Image.fromarray(atlas).save(root / 'textures' / filename)
        paths[channel] = f'{asset_root}/textures/{filename}'
    overrides = {}
    catalog = json.loads((root / 'catalog.json').read_text())
    for tile in catalog['terrain']:
        stem = Path(tile['file']).stem
        if not (root / 'textures' / f'{stem}_weights.png').is_file():
            raise ValueError('missing continuous weights: run bake_landscape_pbr.py --detail')
        mask_size = Image.open(root / 'textures' / f'{stem}_weights.png').size
        if mask_size[0] != mask_size[1]:
            raise ValueError('weight masks must be square')
        graph = material_graph(f'{asset_root}/textures/{stem}_weights.png', paths, metres, transparency, mask_resolution=mask_size[0], secondary_sand=bool(secondary_sand), secondary_rock=bool(secondary_rock), height_normals=height_normals, height_metres=height_metres)
        overrides[tile['file']] = {'*': {'color': [1, 1, 1, 1], 'vertexColors': False,
                                       'shaderGraph': graph}}
        if height_normals:
            overrides[tile['file']]['*']['normalScale']=0  # graph owns detail normal exactly once
    manifest = dict(version=1, metresPer1024=metres, noiseTransparency=transparency,
                    layers=list(layers), normalMode="world-height-gradient" if height_normals else "normal-atlas", heightMetres=height_metres if height_normals else None, rockVariation="continuous-mask-alpha" if secondary_rock else "single-family", sandVariation="continuous-mask-alpha" if secondary_sand else "single-family", materials=overrides)
    (root / 'detail-materials.json').write_text(json.dumps(manifest))
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--materials', type=Path, help='terrain PBR textures folder (default: terrain-pbr from the package catalog)')
    parser.add_argument('--asset-root', required=True, help='project-relative path of installed pack')
    parser.add_argument('--metres', type=float, default=4)
    parser.add_argument('--noise-transparency', type=float, default=.12)
    parser.add_argument('--sand-layer', choices=['sand','river_sand','pebble_sand'], default='sand')
    parser.add_argument('--secondary-sand', choices=['sand','river_sand','pebble_sand'])
    parser.add_argument('--rock-layer', choices=['rock','granite_rock','schist_rock'], default='rock')
    parser.add_argument('--secondary-rock', choices=['rock','granite_rock','schist_rock'])
    parser.add_argument('--height-normals', action='store_true')
    args = parser.parse_args()
    if args.asset_root.startswith('/') or '..' in args.asset_root or '\\' in args.asset_root:
        parser.error('asset-root must be project relative')
    if args.materials is None:
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pkg'))
        from aiverse_pkg import package_dir
        args.materials = package_dir('terrain-pbr') / 'textures'
    result = build(args.root, args.materials, args.asset_root, args.metres, args.noise_transparency, args.sand_layer, args.secondary_sand, args.rock_layer, args.secondary_rock, args.height_normals)
    print(f"Shared 1K/{args.metres}m PBR: {len(result['materials'])} tiles, {len(result['layers'])} layers")
