#!/usr/bin/env python3
"""Add a Warcraft III *visual scene* to an existing AIVERSE project.

Native map.entities + native rules.type view.model are used. User-owned model
and dependent BLP files are copied into project content when --game-data is set.
No Warcraft mechanics, water or special cliff topology are fabricated.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from resolver import resolve, safe_relative  # noqa: E402

MAX_COPIED = 512 * 1024 * 1024


def _json(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError('missing/nonregular file: %s' % path)
    return json.loads(path.read_text(encoding='utf-8'))


def _within(root, rel):
    norm = safe_relative(rel)
    if norm != rel:
        raise ValueError('unsafe project relative path: %r' % rel)
    path = root / norm
    if path.is_symlink() or any(parent.is_symlink() for parent in path.parents if parent != root):
        raise ValueError('symlink in project path: %s' % rel)
    return path


def plan(project, imported, game_data=None, model_map=None, strict=False):
    root = Path(project).resolve(strict=True)
    imported = Path(imported).resolve(strict=True)
    if imported.parent != root / 'content' / 'Warcraft3' or not imported.is_dir():
        raise ValueError('imported map must be project/content/Warcraft3/<map>')
    gamepath = _within(root, 'game.json')
    game = _json(gamepath)
    if not isinstance(game, dict) or not isinstance(game.get('maps', []), list):
        raise ValueError('invalid game.json map list')
    rules_rel = game.get('rules', 'rules.json')
    if not isinstance(rules_rel, str):
        raise ValueError('invalid game.json rules path')
    rulespath = _within(root, rules_rel)
    rules = _json(rulespath)
    if not isinstance(rules, dict):
        raise ValueError('invalid native rules file')
    preview = _json(imported / 'studio-map.json')
    if not preview.get('warcraft3', {}).get('previewOnly'):
        raise ValueError('not an imported Warcraft III MapDoc preview')
    resolved = resolve(imported, game_data, model_map)
    if strict and resolved['unresolvedCount']:
        raise ValueError('%d unresolved WC3 objects (see resolver report)' % resolved['unresolvedCount'])
    slug = imported.name
    if not slug.replace('_', '').replace('-', '').isalnum():
        raise ValueError('unsupported map slug: ' + slug)
    map_rel = 'maps/warcraft3_' + slug + '.json'
    # include is resolved relative to the parent rules document
    generated_rules_rel = (str(Path(rules_rel).parent / ('warcraft3_' + slug + '.json')).replace('\\', '/'))
    include_entry = 'warcraft3_' + slug + '.json'
    mapfile = _within(root, map_rel)
    rulesfile = _within(root, generated_rules_rel)
    if mapfile.exists() or rulesfile.exists():
        raise ValueError('refusing to overwrite a generated map or rules file')
    if any(isinstance(m, dict) and m.get('file', '').casefold() == map_rel.casefold()
           for m in game['maps']):
        raise ValueError('map already registered')
    includes = rules.get('include', [])
    if isinstance(includes, str):
        includes = [includes]
    if not isinstance(includes, list) or any(not isinstance(i, str) for i in includes):
        raise ValueError('unsupported rules.include structure')
    if include_entry in includes:
        raise ValueError('rules already include generated WC3 content')
    if any(i in rules.get('types', {}) for i in resolved['types']):
        raise ValueError('generated type ID conflicts with existing rules')
    # Keep game map source metadata and append only known model-backed objects.
    scene = dict(preview)
    scene['entities'] = list(scene.get('entities', [])) + resolved['entities']
    scene['warcraft3'] = dict(scene['warcraft3'])
    scene['warcraft3']['previewOnly'] = not bool(resolved['entities'])
    scene['warcraft3']['resolvedCount'] = resolved['resolvedCount']
    scene['warcraft3']['unresolvedCount'] = resolved['unresolvedCount']
    scene['warcraft3']['rawPlacementFile'] = 'content/Warcraft3/%s/placements.json' % slug
    types = {'types': resolved['types']}
    game = dict(game)
    game['maps'] = list(game['maps']) + [{'file': map_rel, 'title': scene['name']}]
    rules = dict(rules)
    rules['include'] = list(includes) + [include_entry]
    copies = resolved['copyPlan']
    total = 0
    for relative, source in copies.items():
        dest = _within(root, relative)
        if dest.exists():
            raise ValueError('model destination already exists: ' + relative)
        file = Path(source)
        if file.is_symlink() or not file.is_file():
            raise ValueError('invalid source asset: ' + source)
        total += file.stat().st_size
        if total > MAX_COPIED:
            raise ValueError('model copy plan exceeds 512 MiB')
    payloads = {map_rel: scene, generated_rules_rel: types,
                'game.json': game, rules_rel: rules,
                'content/Warcraft3/%s/resolve-report.json' % slug: {
                    k: v for k, v in resolved.items() if k != 'copyPlan'}}
    report = _within(root, 'content/Warcraft3/%s/resolve-report.json' % slug)
    if report.exists():
        raise ValueError('existing resolve report will not be overwritten')
    return payloads, copies, resolved


def install(project, payloads, copies):
    root = Path(project).resolve(strict=True)
    originals = {rel: (root / rel).read_bytes() for rel in ('game.json',)
                 if (root / rel).exists()}
    # Need old rules content for rollback; its location need not be 'rules.json'.
    old_paths = set(payloads).intersection({'game.json'})
    old_paths.update(rel for rel in payloads if rel not in old_paths and (root / rel).exists())
    originals = {rel: (root / rel).read_bytes() for rel in old_paths}
    created = []
    updated = []
    with tempfile.TemporaryDirectory(prefix='.wc3-install-', dir=str(root)) as tmp:
        stage = Path(tmp)
        for rel, doc in payloads.items():
            path = stage / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        for rel, src in copies.items():
            path = stage / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, path)
        try:
            # Non-overwriting writes first. Manifest/rules last.
            newfiles = sorted(set(payloads) - old_paths) + sorted(copies)
            for rel in newfiles:
                target = _within(root, rel)
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as dst:
                    created.append(target)  # cleanup even if copying this file fails
                    with (stage / rel).open('rb') as src:
                        shutil.copyfileobj(src, dst)
            for rel in sorted(old_paths, key=lambda p: p == 'game.json'):
                target = _within(root, rel)
                os.replace(str(stage / rel), str(target))
                updated.append(rel)
        except (OSError, ValueError):
            for rel in reversed(updated):
                (root / rel).write_bytes(originals[rel])
            for path in reversed(created):
                path.unlink(missing_ok=True)
            raise


def run(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project', type=Path, required=True)
    ap.add_argument('--imported', type=Path, required=True)
    ap.add_argument('--game-data', type=Path, help='user-owned extracted Warcraft game data')
    ap.add_argument('--model-map', type=Path, help='optional rawcode -> model path JSON')
    ap.add_argument('--strict', action='store_true', help='fail on any unresolved object')
    ap.add_argument('--dry-run', action='store_true', help='show report without changes')
    args = ap.parse_args(argv)
    try:
        manual = _json(args.model_map) if args.model_map else None
        docs, copies, report = plan(args.project, args.imported, args.game_data, manual, args.strict)
        if not args.dry_run:
            install(args.project, docs, copies)
        print(json.dumps({'ok': True, 'dryRun': args.dry_run,
                          'resolved': report['resolvedCount'],
                          'unresolved': report['unresolved'],
                          'types': list(report['types']),
                          'copiedAssets': len(copies)}, ensure_ascii=False))
        return 0
    except (ValueError, OSError, TypeError, KeyError, json.JSONDecodeError) as ex:
        print(json.dumps({'ok': False, 'error': str(ex)}, ensure_ascii=False))
        return 2


if __name__ == '__main__':
    sys.exit(run())
