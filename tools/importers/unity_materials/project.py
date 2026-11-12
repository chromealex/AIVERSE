"""Materials of an imported Unity project: converts every material a renderer uses, matches slots to the
models' materials and plans their textures. Used by tools/importers/unity_project/importer.py.

The result lives in the neutral model (port.json): `materialLibrary` {key: converted material},
`texturePlans` [textures.plan], and per entity `materialSlots` {model: {materials, assign}}.
"""
import re
from pathlib import Path

from lib.unity.unity_yaml import get
from lib.port.common import item
from . import shadergraph, slots, textures, urp

MODEL_EXT = ('.fbx', '.obj')


def _key(name, guid, fid):
    s = re.sub(r'[^a-z0-9_]+', '_', str(name).lower()).strip('_')[:40] or 'material'
    return '%s_%s' % (s, (str(guid) + str(fid))[-8:])


class Collector:
    def __init__(self, root, assets, model):
        self.root, self.assets, self.model = Path(root), assets, model
        self.ctx = urp.Context(root, assets, urp.project_linear(root))
        self.ctx.graphs = shadergraph.Library(self.ctx)
        self.results, self.keys, self.fbx = {}, {}, {}
        self.renderers = {}   # entity id -> [(model rel, renderer dict)]
        self.entities = {}

    def rel(self, ref):
        return self.ctx.rel(ref)

    def key(self, ref):
        """Library key of the material a reference points at; None for built-in / model-embedded materials."""
        if not isinstance(ref, dict) or not get(ref, 'fileID'):
            return None
        ident = (str(ref.get('guid', '')).lower(), ref.get('fileID'))
        if ident in self.keys:
            return self.keys[ident]
        doc, path = self.assets.object(ref)
        k = None
        if doc is not None and doc.class_id == 21:
            res = urp.convert(doc.data, self.ctx)
            k = _key(res.name, ident[0], ident[1])
            res.source = self.rel(ref)
            self.results[k] = res
        self.keys[ident] = k
        return k

    def name_of(self, key):
        return self.results[key].name if key in self.results else key

    def add_renderer(self, entity, model_rel, node, mat_refs, where, meta=None, mesh_file_id=None, cast=True):
        name = slots.mesh_name(meta, mesh_file_id) if mesh_file_id is not None else None
        r = dict(node=name or node, slots=[self.key(m) for m in mat_refs], where=where, cast=cast)
        for ref, k in zip(mat_refs, r['slots']):
            if k is None and isinstance(ref, dict) and get(ref, 'fileID'):
                p = self.rel(ref)
                if p.lower().endswith(MODEL_EXT):
                    item(self.model, where, 'approx', 'Material embedded in %s: the model file material is used.' % p, key='embedded-material')
        self.entities[entity['id']] = entity
        self.renderers.setdefault(entity['id'], []).append((model_rel, r))
        if model_rel and model_rel not in entity['models']:
            entity['models'].append(model_rel)

    def add_instance(self, entity, model_rel, modifications, where):
        """Model prefab instance: renderer targets are hashed ids, slots match by index or name."""
        per_target = {}
        cast = {}
        for m in modifications or []:
            if not isinstance(m, dict):
                continue
            path = str(m.get('propertyPath', ''))
            tgt = get(m, 'target', 'fileID')
            mm = re.fullmatch(r'm_Materials\.Array\.data\[(\d+)\]', path)
            if mm and isinstance(m.get('objectReference'), dict):
                per_target.setdefault(tgt, {})[int(mm[1])] = m['objectReference']
            elif path == 'm_CastShadows':
                cast[tgt] = str(m.get('value')) != '0'
        self.entities[entity['id']] = entity
        if model_rel not in entity['models']:
            entity['models'].append(model_rel)
        lst = self.renderers.setdefault(entity['id'], [])
        if not per_target:
            lst.append((model_rel, dict(node=None, slots=[], where=where)))
        for tgt, by_index in sorted(per_target.items(), key=lambda kv: str(kv[0])):
            refs = [by_index.get(i) for i in range(max(by_index) + 1)]
            lst.append((model_rel, dict(node=None, slots=[self.key(r) if r else None for r in refs], where='%s renderer %s' % (where, tgt),
                                        cast=cast.get(tgt, True))))

    def finish(self):
        used = set()
        for eid, entries in self.renderers.items():
            entity = self.entities[eid]
            by_model = {}
            for model_rel, r in entries:
                r['names'] = {k: self.name_of(k) for k in r['slots'] if k}
                by_model.setdefault(model_rel, []).append(r)
            out = {}
            for model_rel, rs in by_model.items():
                info = slots.model_info(self.root, model_rel, self.fbx) if model_rel else None
                mats, assign, notes = slots.resolve(info, rs)
                ext = slots.external_map(self.assets.meta(str(self.root / model_rel))) if model_rel else {}
                for fbx_name, ref in ext.items():
                    if fbx_name not in mats and (info and fbx_name in info.get('materials', [])):
                        k = self.key(ref)
                        if k:
                            mats[fbx_name] = k
                            notes.append(('exact', 'Model importer remaps %s -> %s.' % (fbx_name, k)))
                for status, why in notes:
                    item(self.model, entity['source'], status, why)
                if any(r.get('cast') is False for r in rs):
                    item(self.model, entity['source'], 'approx', 'Renderer shadow casting off: set castShadow on its materials.', key='cast')
                out[model_rel] = dict(materials=mats, assign=assign)
                used.update(mats.values())
                used.update(assign.values())
            entity['materialSlots'] = out
            first = next((m for m in out.values() if m['materials'] or m['assign']), None)
            if first:
                k = next(iter(first['materials'].values()), None) or next(iter(first['assign'].values()))
                entity['materials'] = [self.results[k].material] if k in self.results else []
        lib = {}
        roles = {}
        for k in sorted(used):
            res = self.results.get(k)
            if res is None:
                continue
            lib[k] = res.as_json()
            st = res.status()
            for status, why in res.items or [('exact', 'URP material mapped.')]:
                item(self.model, getattr(res, 'source', k) or k, status, '%s (%s): %s' % (res.name, res.shader, why), result='materials.json#' + k)
            if st == 'exact' and not res.items:
                item(self.model, getattr(res, 'source', k) or k, 'exact', '%s (%s) mapped.' % (res.name, res.shader), result='materials.json#' + k, key='mat')
            for rel, role in res.textures:
                roles.setdefault(rel, set()).add(role)
        plans = []
        for rel, rs in sorted(roles.items()):
            meta = self.assets.meta(str(self.root / rel))
            p = textures.plan(rel, meta, tuple(sorted(rs)), size=_image_size(self.root / rel))
            plans.append(p)
            for n in p['notes']:
                item(self.model, rel, 'approx', n, key='texture-settings')
        self.model['materialLibrary'] = lib
        self.model['texturePlans'] = plans
        self.model['shaderGraphs'] = self.ctx.graphs.report


def _image_size(path):
    """(w, h) from PNG / JPEG / TGA headers without decoding; None when unknown."""
    try:
        with open(path, 'rb') as f:
            head = f.read(64 * 1024)
    except OSError:
        return None
    import struct
    if head[:8] == b'\x89PNG\r\n\x1a\n' and len(head) >= 24:
        return struct.unpack('>II', head[16:24])
    if head[:2] == b'\xff\xd8':
        i = 2
        while i + 9 < len(head):
            if head[i] != 0xFF:
                i += 1
                continue
            marker, size = head[i + 1], struct.unpack('>H', head[i + 2:i + 4])[0]
            if marker in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack('>HH', head[i + 5:i + 9])
                return w, h
            i += 2 + size
        return None
    if str(path).lower().endswith('.tga') and len(head) >= 18:
        return struct.unpack('<HH', head[12:16])
    return None
