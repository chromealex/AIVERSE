#!/usr/bin/env python3
"""Emit target-specific AIVERSE ASTC packaging rules for an imported material."""
import argparse
import json
from pathlib import PurePosixPath, Path


def rules_for(material):
    rules = {}
    for field, space, normal in [('texture', 'srgb', False), ('emissiveMap', 'srgb', False),
                                  ('normalMap', 'linear', True),
                                  ('metallicRoughnessMap', 'linear', False),
                                  ('occlusionMap', 'linear', False)]:
        if field not in material:
            continue
        path = material[field]
        if not isinstance(path, str) or not path.startswith('assets/substance/') or any(
                char in path for char in '*?[]\\') or '..' in PurePosixPath(path).parts:
            raise ValueError('Expected an imported project-relative Substance texture path')
        rule = {'match': path, 'compress': 'astc4x4', 'colorSpace': space,
                'normalMap': normal, 'mipmaps': True, 'alpha': True}
        if path in rules and rules[path] != rule:
            raise ValueError('One texture is used with incompatible semantics')
        rules[path] = rule
    if not rules:
        raise ValueError('Material has no texture maps')
    return list(rules.values())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--target', choices=['android', 'ios', 'macos'], required=True)
    args = parser.parse_args()
    try:
        material = json.loads((args.project / 'materials.json').read_text())['materials'][args.name]
        print(json.dumps({'packaging': {'targets': {args.target: {'files': rules_for(material)}}}}, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        parser.exit(1, f'Packaging rules failed: {error}\n')
