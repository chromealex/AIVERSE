#!/usr/bin/env python3
"""Register a generated Warcraft III heightfield preview with an AIVERSE Studio project.

No game objects are spawned: the preview is terrain-only until rawcode/model
mapping and ECS installation are implemented. Does not alter project rules.
"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile


def run(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project', required=True, type=Path)
    ap.add_argument('--imported', required=True, type=Path,
                    help='content/Warcraft3/<map> produced by import.py')
    args = ap.parse_args(argv)
    try:
        root = args.project.expanduser().resolve(strict=True)
        imported = args.imported.expanduser().resolve(strict=True)
        if not root.is_dir() or root not in imported.parents or imported.parent.name != 'Warcraft3':
            raise ValueError('imported map must be inside project content/Warcraft3')
        if imported.parent.parent != root / 'content':
            raise ValueError('imported map must be directly below content/Warcraft3')
        preview_file = imported / 'studio-map.json'
        game_file = root / 'game.json'
        if not preview_file.is_file() or preview_file.is_symlink() or not game_file.is_file() or game_file.is_symlink():
            raise ValueError('studio-map.json or project game.json is missing')
        game = json.loads(game_file.read_text(encoding='utf-8'))
        preview = json.loads(preview_file.read_text(encoding='utf-8'))
        if not isinstance(game, dict) or not isinstance(game.get('maps', []), list):
            raise ValueError('game.json must be an object with a maps list')
        game.setdefault('maps', [])
        if not isinstance(preview, dict) or not preview.get('warcraft3', {}).get('previewOnly'):
            raise ValueError('not a Warcraft III MapDoc preview')
        dims = preview.get('size')
        if (not isinstance(dims, list) or len(dims) != 2 or
                any(not isinstance(v, int) or not 4 <= v <= 2048 for v in dims)):
            raise ValueError('invalid preview map size')
        if len(preview.get('terrain', {}).get('corners', [])) != (dims[0]+1)*(dims[1]+1):
            raise ValueError('invalid preview corner count')
        slug = re.sub(r'[^A-Za-z0-9_-]', '_', imported.name).strip('_')
        if not slug:
            raise ValueError('invalid map name')
        rel = 'maps/warcraft3_' + slug + '.json'
        dest = root / rel
        if dest.parent.is_symlink():
            raise ValueError('project maps directory must not be a symlink')
        if dest.exists() or any(m.get('file', '').casefold() == rel.casefold() for m in game['maps']
                                if isinstance(m, dict) and isinstance(m.get('file'), str)):
            raise ValueError('map destination already exists or is registered')
        dest.parent.mkdir(parents=True, exist_ok=True)
        title = str(preview.get('name', 'Warcraft III map'))
        game['maps'].append({'file': rel, 'title': title})
        game_text = json.dumps(game, ensure_ascii=False, indent=2) + '\n'
        # Write and validate new manifest before exposing either new file.
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=str(root),
                                         prefix='.wc3-game-', delete=False) as tmp:
            tmp.write(game_text)
            manifest_temp = Path(tmp.name)
        try:
            with dest.open('xb') as writer:  # refuse overwrite even if a file appears after checks
                writer.write(preview_file.read_bytes())
            try:
                os.replace(str(manifest_temp), str(game_file))
            except OSError:
                dest.unlink(missing_ok=True)
                raise
        finally:
            manifest_temp.unlink(missing_ok=True)
        print(json.dumps({'ok': True, 'map': rel, 'title': title,
                          'previewOnly': True}, ensure_ascii=False))
        return 0
    except (ValueError, OSError, json.JSONDecodeError) as ex:
        print(json.dumps({'ok': False, 'error': str(ex)}, ensure_ascii=False))
        return 2


if __name__ == '__main__':
    sys.exit(run())
