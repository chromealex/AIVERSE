"""Warcraft III rawcode -> native AIVERSE view model and map entity adapter.

Consumes *user-owned extracted* Warcraft III SLK/model tree and/or embedded map
models. Does not fabricate gameplay stats. No data from the commercial game ships
with AIVERSE. Exact source versions/data files remain available for inspection.
"""
import math
from pathlib import Path
import re
import struct

MAX_INDEX_FILES = 100000
MAX_MODEL_BYTES = 128 * 1024 * 1024
MODEL_EXTS = ('.mdx', '.mdl')
SOURCE_TABLES = {'unit': ('unitui.slk', 'itemdata.slk'),
                 'doodad': ('doodads.slk', 'destructabledata.slk')}
OVERRIDE_TABLES = {'unit': ('w3u', 'w3t'), 'doodad': ('w3d', 'w3b')}
OVERRIDE_MODEL_FIELDS = {'umdl', 'dmdl', 'bfil'}
SKIN_TABLES = {'unit': ('unitskin.txt', 'itemskin.txt'),
               'doodad': ('doodadskin.txt', 'destructableskin.txt')}



def safe_relative(name):
    if not isinstance(name, str) or not name or len(name) > 1024:
        return None
    name = name.replace('\\', '/').strip().strip('"').replace('//', '/')
    if name.startswith('/') or '\x00' in name or ':' in name:
        return None
    parts = name.split('/')
    if any(p in ('.', '..', '') for p in parts):
        return None
    return '/'.join(parts)


def split_sylk(line):
    """SYLK semicolon parser, including ; and doubled quote inside K\"...\"."""
    out, field, in_quote = [], [], False
    i = 0
    while i < len(line):
        c = line[i]
        if c == '"':
            if in_quote and i+1 < len(line) and line[i+1] == '"':
                field.append('"')
                i += 2
                continue
            in_quote = not in_quote
        elif c == ';' and not in_quote:
            out.append(''.join(field))
            field = []
        else:
            field.append(c)
        i += 1
    if in_quote:
        raise ValueError('unterminated SYLK quoted cell')
    out.append(''.join(field))
    return out


def slk_rows(data):
    text = data.decode('utf-8-sig', 'replace')
    grid = {}
    x = y = 0
    for line in text.splitlines():
        if not line.startswith('C;'):
            continue
        k = None
        for field in split_sylk(line)[1:]:
            if field.startswith('X') and field[1:].isdigit():
                x = int(field[1:])
            elif field.startswith('Y') and field[1:].isdigit():
                y = int(field[1:])
            elif field.startswith('K'):
                k = field[1:]
        if k is not None and 0 < x < 4096 and 0 < y < 100000:
            grid.setdefault(y, {})[x] = k
    if not grid:
        return []
    header = {i: value.lower().strip() for i, value in grid.get(1, {}).items()}
    if not header:
        return []
    return [{label: values.get(col, '') for col, label in header.items()}
            for row, values in sorted(grid.items()) if row > 1]


def file_index(root):
    """Build case-insensitive, symlink-free bounded index of extracted source files."""
    if root is None:
        return {}
    root = Path(root).resolve(strict=True)
    if not root.is_dir():
        raise ValueError('game-data is not a folder: %s' % root)
    index = {}
    count = 0
    for path in sorted(root.rglob('*'), key=lambda p: str(p).casefold()):
        if path.is_symlink():
            continue
        if not path.is_file():
            continue
        if any(p.is_symlink() for p in path.relative_to(root).parents if p != Path('.')):
            continue
        relative = path.relative_to(root).as_posix()
        if safe_relative(relative) != relative:
            continue
        index.setdefault(relative.casefold(), path)
        count += 1
        if count > MAX_INDEX_FILES:
            raise ValueError('game-data index too large')
    return index


def _norm_ref(ref):
    ref = safe_relative(ref)
    if not ref:
        return None
    if Path(ref).suffix.lower() not in MODEL_EXTS:
        ref += '.mdx'
    return ref


def _case_find(index, relative):
    if not relative:
        return None
    key = relative.casefold()
    if key in index:
        return index[key]
    if key.endswith('.mdx') and key[:-4] + '.mdl' in index:
        return index[key[:-4] + '.mdl']
    return None


def _model_from_row(row):
    for key in ('file', 'model', 'modelpath'):
        if row.get(key):
            ref = _norm_ref(row[key].split(',')[0].strip())
            if ref:
                return ref
    return None


def skin_rows(data):
    """Parse harmless read-only WC3 profile overrides: [rawcode] and file=path."""
    result = {}
    current = None
    for line in data.decode('utf-8-sig', 'replace').splitlines():
        line = line.strip()
        if not line or line.startswith('//') or line.startswith(';'):
            continue
        if line.startswith('[') and line.endswith(']'):
            raw = line[1:-1].strip()
            current = raw if len(raw) == 4 else None
        elif current and '=' in line:
            name, value = (v.strip() for v in line.split('=', 1))
            if name.lower() in ('file', 'model'):
                result[current] = _norm_ref(value.split(',')[0].strip())
    return result


def _rows_for_kind(game_files, kind):
    rows = {}
    suffixes = SOURCE_TABLES[kind]
    for rel, path in sorted(game_files.items()):
        if path.name.lower() not in suffixes:
            continue
        for row in slk_rows(path.read_bytes()):
            raw = (row.get('unitid') or row.get('doodid') or row.get('destructableid')
                   or row.get('itemid') or row.get('unituiid') or row.get('id') or '').strip()
            if len(raw) == 4 and raw not in rows:
                rows[raw] = {'model': _model_from_row(row),
                             'name': row.get('name', '') or row.get('propernames', ''),
                             'source': rel}
    # Reforged 1.32+ moved unit model paths from UnitUI.slk into UnitSkin.txt.
    for rel, path in sorted(game_files.items()):
        if path.name.lower() not in SKIN_TABLES[kind]:
            continue
        for raw, model in skin_rows(path.read_bytes()).items():
            rows.setdefault(raw, {'model': None, 'name': '', 'source': rel})
            if model:
                rows[raw]['model'] = model
    return rows


def _overrides(decoded, kind):
    aliases = {}
    for ext in OVERRIDE_TABLES[kind]:
        d = decoded.get('objects_' + ext)
        if not isinstance(d, dict):
            continue
        for record in d.get('original', []) + d.get('custom', []):
            raw = record['customId'] if record['customId'] not in ('\x00'*4, '') else record['baseId']
            if len(raw) != 4:
                continue
            entity = aliases.setdefault(raw, {'base': record['baseId'], 'model': None})
            for mod in record.get('modifications', []):
                if mod['fieldId'].lower() in OVERRIDE_MODEL_FIELDS and isinstance(mod['value'], str):
                    entity['model'] = _norm_ref(mod['value'].split(',')[0].strip())
    return aliases


def _embedded_model_index(import_dir):
    result = {}
    for file in import_dir.rglob('*'):
        if not file.is_file() or file.is_symlink():
            continue
        rel = file.relative_to(import_dir).as_posix()
        if not safe_relative(rel):
            continue
        result.setdefault(rel.casefold(), file)
    return result


def _model_candidates(ref, wc3_index, map_index):
    if not ref:
        return None, None
    original = ref.replace('\\', '/')
    candidates = [original]
    if not original.lower().startswith('war3mapimported/'):
        candidates.append('war3mapImported/' + original)
    candidates.append(Path(original).name)
    for candidate in candidates:
        file = _case_find(map_index, candidate)
        if file:
            return file, 'map'
    return _case_find(wc3_index, original), 'game'


def _textures_from_mdx(data):
    if not data.startswith(b'MDLX'):
        return []
    refs = []
    pos = 4
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        pos += 8
        if size > len(data) - pos:
            return refs
        if tag == b'TEXS':
            if size % 268:
                return refs
            for i in range(pos, pos + size, 268):
                name = data[i+4:i+264].split(b'\x00', 1)[0].decode('utf-8', 'replace')
                if name:
                    refs.append(name)
        pos += size
    return refs


def _textures_from_mdl(data):
    text = data.decode('utf-8', 'replace')
    return re.findall(r'\bImage\s+"([^"]+)"', text)


def _references_for_game_model(file, game_index):
    if file.stat().st_size > MAX_MODEL_BYTES:
        raise ValueError('model source exceeds safety bound: ' + str(file))
    data = file.read_bytes()
    texrefs = _textures_from_mdx(data) if file.suffix.lower() == '.mdx' else _textures_from_mdl(data)
    out = []
    for name in texrefs:
        relative = safe_relative(name)
        if not relative:
            continue
        candidate = game_index.get(relative.casefold())
        if candidate is None:
            candidate = game_index.get((file.parent / Path(relative).name).name.lower())
        if candidate is not None:
            out.append(candidate)
    return out


def resolve(import_dir, game_data=None, model_map=None):
    """Generate native rules/types, MapDoc entities, copy plan, warnings; no writes."""
    import_dir = Path(import_dir).resolve(strict=True)
    map_index = _embedded_model_index(import_dir)
    game_index = file_index(game_data)
    root = Path(game_data).resolve(strict=True) if game_data is not None else None
    decoded = {}
    for key in ('units', 'doodads', 'placements', 'objects_w3u', 'objects_w3t',
                'objects_w3d', 'objects_w3b'):
        path = import_dir / (key + '.json')
        if path.is_file():
            import json
            decoded[key] = json.loads(path.read_text(encoding='utf-8'))
    records = decoded.get('placements', {}).get('placements', [])
    types = {}
    entities = []
    errors = []
    copies = {}
    model_map = model_map or {}
    if not isinstance(model_map, dict):
        raise ValueError('model-map must be a JSON object')
    for kind in ('unit', 'doodad'):
        tables = _rows_for_kind(game_index, kind)
        overrides = _overrides(decoded, kind)
        items = [p for p in records if p['kind'] == kind]
        for item in items:
            raw = item['sourceId']
            source = overrides.get(raw)
            base = source['base'] if source else raw
            possible = (_norm_ref(model_map[raw]) if raw in model_map else
                        source['model'] if source and source.get('model') else tables.get(base, {}).get('model'))
            model_file, from_where = _model_candidates(possible, game_index, map_index)
            if model_file is None:
                errors.append({'sourceId': raw, 'kind': kind, 'creationId': item['creationId'],
                               'reason': 'model not found' if possible else 'model path unknown',
                               'referencedModel': possible})
                continue
            if from_where == 'map':
                relative = model_file.relative_to(import_dir).as_posix()
                model_ref = 'content/Warcraft3/%s/%s' % (import_dir.name, relative)
            else:
                relative = model_file.relative_to(root).as_posix()
                model_ref = 'content/Warcraft3/%s/_game/%s' % (import_dir.name, relative)
                copies[model_ref] = str(model_file)
                for texture in _references_for_game_model(model_file, game_index):
                    texrel = texture.relative_to(root).as_posix()
                    copies['content/Warcraft3/%s/_game/%s' % (import_dir.name, texrel)] = str(texture)
            type_name = 'wc3_%s_%s_%s' % (import_dir.name, kind, raw.encode('utf-8').hex())
            if type_name not in types:
                types[type_name] = {'name': tables.get(base, {}).get('name') or '%s %s' % (kind, raw),
                                    'tags': ['warcraft3', 'imported'],
                                    'view': {'model': model_ref},
                                    'warcraft3': {'rawcode': raw, 'baseRawcode': base,
                                                 'sourceModel': possible, 'visualOnly': True}}
            px, py, pz = item['position']
            if not all(math.isfinite(v) for v in (px, py, pz)):
                raise ValueError('non-finite placement')
            entities.append({'id': 'wc3_%s_%s' % (kind, item['sourceIndex']),
                             'type': type_name, 'owner': 0,
                             'pos': [px, pz], 'facing': item['yawDegrees'],
                             'warcraft3': {'sourceId': raw, 'creationId': item['creationId'],
                                          'originalOwner': item.get('owner'), 'height': py,
                                          'scale': item['scale'], 'variation': item['variation']}})
    return {'format': 'aiverse.warcraft3.resolved.v1', 'types': types, 'entities': entities,
            'copyPlan': copies, 'unresolved': errors, 'resolvedCount': len(entities),
            'unresolvedCount': len(errors),
            'limitations': ['Visual-only model types; original Warcraft gameplay is not converted',
                            'Unresolved placements are preserved in placements.json, not spawned',
                            'Native MapDoc positions are 2D; original height and scale stored in metadata']}
