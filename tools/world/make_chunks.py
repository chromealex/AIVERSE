#!/usr/bin/env python3
"""Moves the static scenery of a map into a streamed world (docs/large-worlds.md): any genre.

    python3 tools/world/make_chunks.py <project> [--map maps/city.json] [--cell 32] [--radius 2]
                                       [--tags building tree bush prop lamp street_object]

Entities whose type has one of the tags, a `view.model` and never moves (structures, `move.speed` 0) leave the
map's `entities` and become mesh instances in chunk files `<map>.chunks/<x>_<z>.json`; the map gets the streamed
world block (`world` in FPS projects, `streamedWorld` elsewhere: async, shared models, SHA-256 of every file). Structures keep blocking the ground: their footprint cells
become `#` in `terrain.rows`. Other moved objects stop being obstacles. Run it after the map is final; running
it again on a converted map changes nothing.
"""
import argparse
import hashlib
import json
import math
import os
import struct


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def model_files(project, rel):
    """The model and what it loads: external images / buffers of a glTF."""
    out = [rel]
    path = os.path.join(project, rel)
    d = open(path, 'rb').read()
    j = None
    if d[:4] == b'glTF':
        j = json.loads(d[20:20 + struct.unpack_from('<I', d, 12)[0]])
    elif rel.endswith('.gltf'):
        j = json.loads(d)
    if j:
        for item in j.get('images', []) + j.get('buffers', []):
            uri = item.get('uri')
            if uri and not uri.startswith('data:'):
                out.append(os.path.normpath(os.path.join(os.path.dirname(rel), uri)).replace(os.sep, '/'))
    return out


def model_stats(project, rel):
    """-> (vertices, indices) by the largest accessors: enough for a reservation estimate."""
    d = open(os.path.join(project, rel), 'rb').read()
    if d[:4] != b'glTF':
        return 20000, 60000
    j = json.loads(d[20:20 + struct.unpack_from('<I', d, 12)[0]])
    verts = idx = 0
    for m in j.get('meshes', []):
        for p in m.get('primitives', []):
            verts += j['accessors'][p['attributes']['POSITION']]['count']
            idx += j['accessors'][p['indices']]['count'] if 'indices' in p else 0
    return verts, idx


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('project')
    ap.add_argument('--map', default='maps/city.json')
    ap.add_argument('--cell', type=int, default=32, help='chunk size in map cells (16..4096)')
    ap.add_argument('--radius', type=int, default=2, help='chunks kept around the camera in every direction (0..4)')
    ap.add_argument('--prune-types', action='store_true', help='drop from rules.json the moved types that no entity of this map uses any more (single-map projects: their models are then not loaded at start)')
    ap.add_argument('--key', help='map key of the block: "world" for FPS projects, "streamedWorld" otherwise (there "world" is the planet system)')
    ap.add_argument('--tags', nargs='*', default=['building', 'tree', 'bush', 'prop', 'lamp', 'street_object'])
    a = ap.parse_args()
    rules = json.load(open(os.path.join(a.project, 'rules.json')))
    mpath = os.path.join(a.project, a.map)
    m = json.load(open(mpath))
    types = rules['types']
    rows = [list(r) for r in m['terrain']['rows']]
    # a 2D tile layer decides the cell kinds: blocked cells need one of its terrains whose cell is '#'
    trows, tblocked = None, None
    if isinstance(m.get('tiles'), dict) and m['tiles'].get('tileset'):
        ts = json.load(open(os.path.join(a.project, m['tiles']['tileset'])))
        tblocked = next((t['char'] for t in ts.get('terrains', []) if t.get('cell') == '#'), None)
        if tblocked is None:
            raise SystemExit("the map's tileset has no terrain with \"cell\": \"#\": buildings moved to chunks would not block the ground")
        trows = [list(r) for r in m['tiles']['rows']]
    chunks, keep, moved = {}, [], 0
    for e in m['entities']:
        t = types.get(e['type'], {})
        view = t.get('view', {})
        static = 'structure' in t or t.get('move', {}).get('speed', 1) == 0
        model = view.get('model')
        if not (static and isinstance(model, str) and set(t.get('tags', [])) & set(a.tags)) or view.get('fit', True) or view.get('center', True):
            keep.append(e)      # only plainly placed models: no fitting, no centring (their transform is known here)
            continue
        x, z = e['pos']
        fwd = view.get('forward', '+z')
        yaw0 = {'+z': 90, '-z': -90, '-x': 180}.get(fwd, 0) + view.get('yaw', 0)      # as RtsLook: forward axis -> +x
        # entity facing: degrees clockwise on screen from north; the view turns +x to the facing: yaw = atan2(-fz, fx)
        f = math.radians(e.get('facing', 90))
        yaw = math.degrees(math.atan2(math.cos(f), math.sin(f))) + yaw0
        scale = view.get('scale', 1)
        off = view.get('offset', [0, 0, 0])
        cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
        wx = x + off[0] * cy + off[2] * sy      # rotateY: (x, z) -> (x cos + z sin, -x sin + z cos)
        wz = z - off[0] * sy + off[2] * cy
        cx, cz = int(x // a.cell), int(z // a.cell)
        chunks.setdefault((cx, cz), []).append({'file': model, 'position': [round(wx - cx * a.cell, 4), off[1], round(wz - cz * a.cell, 4)],
                                                'scale': [scale, scale, scale], 'rotation': [0, round(yaw % 360, 3), 0]})
        if 'structure' in t:
            fw, fh = t['structure']['footprint']
            for zz in range(int(z - fh / 2 + 0.01), int(z - fh / 2 + 0.01) + fh):
                for xx in range(int(x - fw / 2 + 0.01), int(x - fw / 2 + 0.01) + fw):
                    if 0 <= zz < len(rows) and 0 <= xx < len(rows[zz]):
                        rows[zz][xx] = '#'
                        if trows is not None:
                            trows[zz][xx] = tblocked
        moved += 1
    if not moved:
        print(json.dumps({'moved': 0, 'note': 'nothing to move (already converted, or no matching static models)'}))
        return
    cdir = os.path.splitext(a.map)[0] + '.chunks'
    os.makedirs(os.path.join(a.project, cdir), exist_ok=True)
    assets, entries, biggest, deps = {}, [], 0, {}
    for (cx, cz), meshes in sorted(chunks.items()):
        parts = [meshes[i:i + 256] for i in range(0, len(meshes), 256)]
        if len(parts) > 1:
            raise SystemExit('chunk %d,%d has %d objects (limit 256): use a smaller --cell' % (cx, cz, len(meshes)))
        rel = '%s/%d_%d.json' % (cdir, cx, cz)
        with open(os.path.join(a.project, rel), 'w') as f:
            json.dump({'meshes': meshes}, f, separators=(',', ':'))
        assets[rel] = sha(os.path.join(a.project, rel))
        for mesh in meshes:
            if mesh['file'] not in deps:
                deps[mesh['file']] = model_files(a.project, mesh['file'])
            for dep in deps[mesh['file']]:
                if dep not in assets:
                    assets[dep] = sha(os.path.join(a.project, dep))
        reserve = 65536 + 4096 * len({mesh['file'] for mesh in meshes}) + 256 * len(meshes)   # shared models are not charged
        biggest = max(biggest, reserve)
        entries.append({'cell': [cx, 0, cz], 'file': rel, 'reservedBytes': reserve})
    side = 2 * a.radius + 1
    key = a.key or ('world' if json.load(open(os.path.join(a.project, 'game.json'))).get('module') == 'fps' else 'streamedWorld')
    m[key] = {'async': True, 'version': 1, 'cellSize': a.cell, 'shareModels': True, 'meshOverhang': a.cell, 'viewRadius': a.radius,
                  'maxResidentChunks': side * side + 8, 'maxResidentBytes': max(1 << 20, biggest * (side * side + 8)),
                  'maxStagingBytes': 256 << 20, 'maxUploadBytes': 256 << 20, 'maxJobs': 4, 'assets': assets, 'chunks': entries}
    m['entities'] = keep
    m['terrain']['rows'] = [''.join(r) for r in rows]
    if trows is not None:
        m['tiles']['rows'] = [''.join(r) for r in trows]
    with open(mpath, 'w') as f:
        json.dump(m, f, ensure_ascii=False, indent=1)
    pruned = 0
    if a.prune_types:
        left = {e['type'] for e in keep}
        gone = [k for k, t in types.items() if k not in left and set(t.get('tags', [])) & set(a.tags) and isinstance(t.get('view', {}).get('model'), str)]
        for k in gone:
            del types[k]
        pruned = len(gone)
        with open(os.path.join(a.project, 'rules.json'), 'w') as f:
            json.dump(rules, f, ensure_ascii=False, indent=1)
    print(json.dumps({'prunedTypes': pruned, 'moved': moved, 'left': len(keep), 'chunks': len(entries), 'models': len(deps), 'files': len(assets),
                      'largestChunk': max(len(v) for v in chunks.values())}))


if __name__ == '__main__':
    main()
