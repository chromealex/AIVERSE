"""World data of Unity scenes: Terrain components, embedded / built-in meshes, spline roads, gameplay markers.

Extraction keeps only references and small summaries in the intermediate model; terrain samples are
decoded again from the source project when the game is emitted (lib/port/world.py).
"""
import math
from pathlib import Path
from lib.unity import mesh as umesh
from lib.unity import terrain as uterrain
from lib.unity.unity_yaml import get
from lib.port.common import item, ident

CLASS_TERRAIN, CLASS_TERRAIN_COLLIDER, CLASS_MESH = 218, 154, 43
EXPORT_DIR = 'AiverseExport/terrain'


def rel(root, path):
    try:
        return Path(path).resolve().relative_to(root.resolve()).as_posix() if path else ''
    except ValueError:
        return ''


def terrain_summary(model, root, assets, path):
    """Decode a TerrainData asset once per import; record the summary and report what is lossy."""
    cache = model.setdefault('terrains', {})
    if path in cache:
        return cache[path]
    guid = assets.guid_of(str(root / path))
    export = root / EXPORT_DIR / (guid + '.json')
    entry = dict(source=path, guid=guid)
    try:
        t = uterrain.load_export(str(export)) if export.is_file() else uterrain.decode(str(root / path), assets)
        if export.is_file():
            entry['export'] = export.relative_to(root).as_posix()
        s = t.summary()
        for layer in s['layers']:
            for key in ('source', 'diffuse', 'normal'):
                layer[key] = rel(root, layer.get(key))
        for p in s['treePrototypes']:
            p['prefab'] = rel(root, p.get('prefab'))
            p['model'] = prefab_model(root, assets, p['prefab'])
        for p in s['detailPrototypes']:
            p['prefab'], p['texture'] = rel(root, p.get('prefab')), rel(root, p.get('texture'))
        entry.update(s, ok=True)
        item(model, path, 'approx', 'TerrainData decoded: %dx%d heightmap, %d splat layers, %d trees, %d detail layers.'
             % (t.res, t.res, len(t.layers), len(t.trees), len(t.detail_prototypes)), key='terrain')
        for note in t.notes:
            item(model, path, 'missing', 'Terrain: ' + note + '.', key='terrain-note:' + note)
    except (ValueError, OSError, KeyError, IndexError, TypeError) as e:
        entry.update(ok=False, error=str(e))
        item(model, path, 'missing', 'TerrainData could not be decoded (%s); run AiverseTerrainExport.cs in the Unity editor '
             '(tools/importers/unity_project/editor) to write %s/<guid>.json.' % (e, EXPORT_DIR), key='terrain')
    cache[path] = entry
    return entry


def prefab_model(root, assets, prefab_path):
    """First model file referenced by a MeshFilter / SkinnedMeshRenderer in a prefab (tree prototypes)."""
    if not prefab_path:
        return ''
    for d in assets.docs(str(root / prefab_path)):
        if d.class_id in (33, 137):
            p = assets.path(d.data.get('m_Mesh'))
            if p:
                return rel(root, p)
    return ''


def local_mesh(model, prefab, source, comp, rot, scale):
    """Embedded (class 43) or built-in mesh of a MeshFilter as OBJ in model['meshes']; returns the model path."""
    ref = comp.data.get('m_Mesh') or {}
    fid, guid = get(ref, 'fileID'), str(get(ref, 'guid', default='') or '')
    if not fid:
        return None
    try:
        if guid == umesh.BUILTIN_GUID:
            data = umesh.builtin(fid)
            if data is None:
                return None
            kind = 'builtin%d' % fid
        elif not guid and fid in prefab.by_id and prefab.by_id[fid].class_id == CLASS_MESH:
            data = umesh.decode(prefab.by_id[fid].data)
            kind = 'embedded'
        else:
            return None
    except (ValueError, KeyError, TypeError) as e:
        item(model, source, 'missing', 'Embedded mesh not decoded: %s.' % e, key='mesh')
        return None
    text, lo, hi = umesh.to_obj(data, rot, scale, name=prefab.name(get(comp.data, 'm_GameObject', 'fileID')))
    mid = ident(source + '#mesh' + str(comp.file_id))
    model.setdefault('meshes', {})[mid] = text
    item(model, source, 'exact', 'Mesh (%s) converted to OBJ with the world rotation/scale baked in.' % kind, key='mesh')
    return dict(path='_meshes/' + mid + '.obj', bounds=[lo, hi])


def road(data, pos, rot, scale):
    """EasyRoads3D-style road component: baked spline points + width (no Unity runtime involved)."""
    pts = data.get('splinePoints')
    width = data.get('roadWidth', get(data, 'rt', 'roadWidth'))
    if not isinstance(pts, list) or len(pts) < 2 or not isinstance(width, (int, float)):
        return None
    from lib.unity.unity_scene import q_rot
    out = []
    for p in pts:
        if not isinstance(p, dict):
            return None
        v = q_rot(rot, (float(p.get('x', 0)) * scale[0], float(p.get('y', 0)) * scale[1], float(p.get('z', 0)) * scale[2]))
        out.append([round(pos[0] + v[0], 4), round(pos[1] + v[1], 4), round(-(pos[2] + v[2]), 4)])
    return dict(name=str(data.get('roadName', '')), width=float(width), points=out,
                closed=bool(data.get('closedTrack', 0)))


def classes(comps, reference):
    """Script class names of a GameObject's MonoBehaviours (namespace-qualified when Unity records it)."""
    out = []
    for c in comps:
        if c.class_id != 114:
            continue
        name = str(c.data.get('m_EditorClassIdentifier') or '').split('::')[-1]
        if not name:
            name = Path(reference(c.data.get('m_Script')) or '').stem
        if name:
            out.append(name)
    return out


def links(prefab, comps, assets, root):
    """One level of references held by MonoBehaviour fields: local components (with their fields and position)
    and project assets (path), so marker rules can read FOWArea points or a pathfinding config."""
    out, refs = {}, {}
    for c in comps:
        if c.class_id != 114:
            continue
        for k, v in c.data.items():
            if k.startswith('m_') or not isinstance(v, dict) or set(v) - {'fileID', 'guid', 'type'}:
                continue
            fid, guid = v.get('fileID'), v.get('guid')
            if guid:
                p = assets.path(v)
                if p and p.endswith('.asset'):
                    refs[k] = rel(root, p)
            elif fid and fid in prefab.by_id:
                d = prefab.by_id[fid]
                if d.class_id == 114:
                    go = get(d.data, 'm_GameObject', 'fileID')
                    tr = prefab.tr_by_go.get(go)
                    wp = prefab.world_of(tr)[0] if tr is not None else (0, 0, 0)
                    out[k] = dict(fields={a: b for a, b in d.data.items() if not a.startswith('m_')},
                                  position=[wp[0], wp[1], -wp[2]])
    return out, refs


def is_terrain_data(path):
    from lib.unity.serialized import is_serialized
    try:
        with open(path, 'rb') as f:
            head = f.read(1 << 16)
    except OSError:
        return False
    return is_serialized(head[:48]) and b'm_Heightmap' in head


def referenced_assets(model):
    """Assets an included subset uses: models, material textures, sprites, audio, terrain layer textures."""
    used = set()
    for e in model['entities']:
        used.update('Assets/' + m.split('Assets/', 1)[1] if 'Assets/' in m else m for m in e.get('models', []))
        for mat in e.get('materials', []):
            used.update(v[len('content/'):] for k, v in mat.items() if isinstance(v, str) and v.startswith('content/'))
        if e.get('sprite'): used.add(e['sprite'].get('file', ''))
        if e.get('audio'): used.add(e['audio'].get('file', ''))
    for t in model.get('terrains', {}).values():
        for layer in t.get('layers', []):
            used.update(x for x in (layer.get('diffuse'), layer.get('normal')) if x)
        used.update(p.get('model') for p in t.get('treePrototypes', []) if p.get('model'))
    deps = {a['path'] for a in model['assets'] if a['kind'] == 'dependency'}
    stems = {u.rsplit('.', 1)[0] for u in used}
    used.update(d for d in deps if d.rsplit('.', 1)[0] in stems)
    return used


def finite(obj):
    """Unity YAML can hold Infinity / NaN (e.g. light ranges); JSON output keeps them as strings."""
    if isinstance(obj, float) and not math.isfinite(obj):
        return str(obj)
    if isinstance(obj, dict):
        for k, v in obj.items():
            obj[k] = finite(v)
    elif isinstance(obj, list):
        for i, v in enumerate(obj):
            obj[i] = finite(v)
    return obj
