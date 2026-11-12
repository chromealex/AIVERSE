"""Per-submesh material slot matching: Unity renderer m_Materials[i] -> engine material names of the model.

Unity gives a mesh node one submesh per connected FBX material, in connection order, and fills
submesh i from m_Materials[i]. The engine names primitives after the same FBX materials, so a slot
resolves to `fbx.models[node].materials[i]`. The node is known for MeshFilter/SkinnedMeshRenderer
components (mesh name from the importer's internalIDToNameTable / legacy fileIDToRecycleName, else
the GameObject name); model prefab instances only carry hashed renderer ids, so their slots resolve
by slot index when every mesh agrees on the material there, else by material name. Renderers whose
slots stay ambiguous are reported, never guessed.
"""
import re
from pathlib import Path

from . import fbxinfo

CLASS_MESH = 43


def norm(name):
    s = re.sub(r'\s*\(instance\)$', '', str(name), flags=re.I)
    s = re.sub(r'^(m_|mat_|mtl_|material_)', '', s.lower())
    s = re.sub(r'(_mat|_mtl|_material)$', '', s)
    return re.sub(r'[^a-z0-9]', '', s)


def model_info(root, rel, cache):
    if rel in cache:
        return cache[rel]
    info = None
    if rel.lower().endswith('.fbx'):
        try:
            info = fbxinfo.read_file(str(Path(root) / rel))
        except (OSError, ValueError, IndexError) as e:
            info = dict(error=str(e) or 'unreadable FBX', models=[], materials=[])
    elif rel.lower().endswith('.obj'):
        names = []
        try:
            for line in (Path(root) / rel).read_text(errors='replace').splitlines():
                if line.startswith('usemtl '):
                    n = line[7:].strip()
                    if n not in names:
                        names.append(n)
        except OSError:
            pass
        info = dict(models=[dict(name=Path(rel).stem, materials=names, mesh=True)], materials=names)
    cache[rel] = info
    return info


def mesh_name(meta, file_id):
    """Mesh sub-asset name from a ModelImporter meta (fileIdsGeneration 1 tables); None when hashed."""
    mi = (meta or {}).get('ModelImporter') or {}
    for e in mi.get('internalIDToNameTable') or []:
        if isinstance(e, dict):
            first = e.get('first') or {}
            v = first.get(CLASS_MESH, first.get(str(CLASS_MESH))) if isinstance(first, dict) else None
            if v is not None and str(v) == str(file_id):
                return e.get('second')
    rec = mi.get('fileIDToRecycleName') or {}
    if isinstance(rec, dict):
        v = rec.get(file_id, rec.get(str(file_id)))
        if v:
            return v
    return None


def external_map(meta):
    """ModelImporter externalObjects: FBX material name -> material reference."""
    out = {}
    mi = (meta or {}).get('ModelImporter') or {}
    for e in mi.get('externalObjects') or []:
        if not isinstance(e, dict):
            continue
        first, second = e.get('first') or {}, e.get('second') or {}
        if 'Material' in str(first.get('type', '')) and isinstance(second, dict) and second.get('guid'):
            out[str(first.get('name', ''))] = second
    return out


def resolve(info, renderers):
    """renderers: [dict(node=str|None, slots=[key|None], where=str)] -> (materials{fbx name: key}, assign{node: key}, items)."""
    items, materials, assign = [], {}, {}
    if not info or info.get('error') or not info.get('models'):
        why = (info or {}).get('error') or 'model materials unknown'
        keys = [k for r in renderers for k in r['slots'] if k]
        if keys:
            materials['*'] = keys[0]
            status = 'exact' if len(set(keys)) == 1 else 'missing'
            items.append((status, 'Material slots not matched (%s): every material of the model gets %s.' % (why, keys[0])))
        return materials, assign, items
    meshes = [m for m in info['models'] if m.get('mesh') and m.get('materials')]
    pending = {}   # slot -> [(where, key, candidate names)] of renderers without a known node
    for r in renderers:
        node = None
        if r.get('node'):
            node = next((m for m in meshes if m['name'] == r['node']), None)
        elif len(r['slots']) > 1:
            # a renderer has one slot per submesh: a unique mesh with that many materials is the one
            fits = [m for m in meshes if len(m['materials']) == len(r['slots'])]
            if len(fits) == 1:
                node = fits[0]
        for i, key in enumerate(r['slots']):
            if not key:
                continue
            if node is not None:
                if i >= len(node['materials']):
                    items.append(('approx', '%s: slot %d has no submesh in node %s (Unity draws extra slots over the last submesh); ignored.' % (r['where'], i, node['name'])))
                    continue
                target, how = node['materials'][i], 'node %s slot %d' % (node['name'], i)
            else:
                names = []
                for m in meshes:
                    if i < len(m['materials']) and m['materials'][i] not in names:
                        names.append(m['materials'][i])
                if len(names) == 1:
                    target, how = names[0], 'slot %d' % i
                else:
                    want = norm(r.get('names', {}).get(key, key))
                    named = [n for n in (names or info['materials']) if norm(n) == want]
                    if len(named) == 1:
                        target, how = named[0], 'name'
                    else:
                        pending.setdefault(i, []).append((r['where'], key, names))
                        continue
            prev = materials.get(target)
            if prev and prev != key:
                if node is not None and len(node['materials']) == 1:
                    assign[node['name']] = key
                    items.append(('exact', '%s: node %s gets its own material %s (model material %s is shared).' % (r['where'], node['name'], key, target)))
                else:
                    items.append(('missing', '%s: model material %s already uses %s; %s (%s) not applied.' % (r['where'], target, prev, key, how)))
                continue
            materials[target] = key
            items.append(('exact', '%s: %s -> %s (%s).' % (r['where'], target, key, how)))
    for i, entries in sorted(pending.items()):
        keys = {k for _, k, _ in entries}
        names = entries[0][2]
        free = [n for n in names if n not in materials]
        # as many hashed renderers as candidate meshes, all with the same material: every candidate gets it
        if len(keys) == 1 and len(entries) >= len(names) and free:
            key = keys.pop()
            for n in free:
                materials[n] = key
            items.append(('exact', '%s: slot %d of %d renderers -> %s on %s (same material on every candidate).'
                          % (entries[0][0], i, len(entries), key, ', '.join(free))))
            continue
        for where, key, cands in entries:
            items.append(('missing', '%s: slot %d (%s) is ambiguous between model materials %s; assign it by hand.'
                          % (where, i, key, ', '.join(cands) or 'none')))
    return materials, assign, items
