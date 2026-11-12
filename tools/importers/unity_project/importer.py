"""Unity Force Text project discovery and extraction using the shared VFX parser."""
import math
import re
from pathlib import Path
from .tiles import extract_tiles
from lib.unity.unity_scene import Assets, Prefab, Report
from lib.unity.unity_yaml import get
from lib.port.common import inventory, item, new_model, ident, safe, script_index
from unity_materials.project import Collector, MODEL_EXT

TEXT = {'.unity', '.prefab', '.mat', '.asset'}


def scan(root):
    entries = inventory(root / 'Assets')
    for a in entries:
        a['path'] = 'Assets/' + a['path']
    version_file = root / 'ProjectSettings/ProjectVersion.txt'
    version = version_file.read_text() if version_file.exists() else ''
    match = re.search(r'm_EditorVersion:\s*(\S+)', version)
    settings = root / 'ProjectSettings/EditorSettings.asset'
    settings_text = settings.read_text(errors='replace') if settings.exists() else ''
    mode = re.search(r'm_SerializationMode:\s*(\d+)', settings_text)
    errors = []
    if not mode or mode[1] != '2':
        errors.append('Set Edit > Project Settings > Editor > Asset Serialization > Force Text, then reserialize assets.')
    binary = []
    opaque = []
    for a in entries:
        if Path(a['path']).suffix.lower() in TEXT:
            with safe(root, a['path']).open('rb') as f:
                if not f.read(128).lstrip(b'\xef\xbb\xbf').startswith(b'%YAML'):
                    (opaque if a['path'].endswith('.asset') else binary).append(a['path'])
    if binary:
        errors.append('Non-text serialized assets: ' + ', '.join(binary[:20]) + '. Reserialize these assets with Force Text.')
    return dict(engine='unity', version=match[1] if match else '', serialization='Force Text' if not errors else 'Binary/Mixed/Unknown',
                assets=entries, scenes=[a['path'] for a in entries if a['path'].endswith('.unity')],
                prefabs=[a['path'] for a in entries if a['path'].endswith('.prefab')],
                scripts=[a['path'] for a in entries if a['kind']=='script'], bytes=sum(a['bytes'] for a in entries),
                ok=not errors, errors=errors, opaqueAssets=opaque)


def extract(root):
    info = scan(root)
    if not info['ok']:
        raise ValueError('\n'.join(info['errors']))
    model = new_model(root, 'unity', info['version'])
    model['assets'] = [a for a in info['assets'] if a['kind'] != 'other']
    for path in info['opaqueAssets']:
        item(model, path, 'missing', 'Opaque Unity native asset (baked lighting, navigation or terrain); export from Unity Editor.', key='opaque')
    rep = Report()
    assets = Assets([str(root / 'Assets')], rep)
    mats = Collector(root, assets, model)
    instances = {}
    scripts = {a['path']: script_index(model, root, a) for a in model['assets'] if a['kind']=='script'}
    def reference(ref):
        p = assets.path(ref)
        return Path(p).relative_to(root).as_posix() if p else ''
    for path in info['prefabs'] + info['scenes']:
        prefab = Prefab(str(root / path), assets, rep)
        scene = dict(id=ident(path), source=path, name=Path(path).stem, objects=[], tiles=[])
        for tr, go, pos, rot, scale, hierarchy in prefab.walk():
            comps = prefab.comp_by_go.get(go, [])
            if not comps:
                continue
            source = path + '#' + str(go)
            entity = dict(id=ident(source), source=source, name=prefab.name(go), fields={}, components=[], models=[], materials=[], scripts=[],
                          position=[pos[0], pos[1], -pos[2]], rotation=math.degrees(math.atan2(2*(rot[3]*rot[1]+rot[0]*rot[2]), 1-2*(rot[1]**2+rot[2]**2))), scale=list(scale))
            mesh_ref = None
            for c in comps:
                entity['components'].append(dict(type=c.type, data=c.data))
                if c.class_id == 195:
                    entity['fields']['NavMeshAgent.speed'] = c.data.get('m_Speed', 0)
                elif c.class_id == 114:
                    sp = reference(c.data.get('m_Script'))
                    entity['scripts'].append(sp)
                    fields = {k:v for k,v in c.data.items() if not k.startswith('m_')}
                    entity['fields'].update(fields)
                    if sp in scripts:
                        scripts[sp]['uses'].append(dict(entity=entity['id'], fields=fields))
                if c.class_id in (33, 137):
                    mesh = reference(c.data.get('m_Mesh'))
                    mesh_ref = (mesh, get(c.data, 'm_Mesh', 'fileID')) if mesh else None
                    if mesh:
                        entity['models'].append(mesh)
                        meta = assets.meta(str(root / mesh)).get('ModelImporter', {})
                        entity.setdefault('modelSettings', {})[mesh] = meta
                        clips = get(meta, 'animations', 'clipAnimations', default=[]) or []
                        if clips: item(model, source, 'missing', 'Unity animation frame ranges require clip slicing; metadata retained.', key='clips', details=clips)
                    else: item(model, source, 'missing', 'Mesh reference is built-in or unresolved.', key='mesh')
                if c.class_id in (23, 137):
                    renderer = c
                elif c.class_id == 212:
                    sprite = reference(c.data.get('m_Sprite'))
                    if sprite:
                        meta = assets.meta(str(root / sprite)).get('TextureImporter', {})
                        fid = get(c.data, 'm_Sprite', 'fileID')
                        sprites = get(meta, 'spriteSheet', 'sprites', default=[]) or []
                        sub = next((s for s in sprites if s.get('internalID') == fid), None)
                        entity['sprite'] = dict(file=sprite,pixelsPerUnit=meta.get('spritePixelsToUnits',100),rect=sub.get('rect') if sub else None)
                        if meta.get('spriteMode') == 2 and sub is None:
                            entity.pop('sprite')
                            item(model, source, 'missing', 'Sprite subasset ID not found in texture metadata.', key='sprite')
                elif c.type == 'Tilemap':
                    scene['tiles'] += extract_tiles(c, assets, root, model, source, pos)
                elif c.class_id == 198:
                    entity['effectSource'] = path
                elif c.class_id == 82:
                    entity['audio'] = dict(file=reference(c.data.get('m_audioClip')), loop=bool(c.data.get('Loop', 0)), music=any(x.class_id == 20 for x in comps) or 'manager' in entity['name'].lower())
                elif c.class_id not in (4, 224, 195, 114, 33, 137, 23, 212, 198, 199):
                    item(model, source, 'missing', c.type + ' component requires review; serialized data retained.', key='component'+str(c.file_id))
            renderer = next((c for c in comps if c.class_id in (23, 137)), None)
            if renderer is not None:
                if mesh_ref is None and renderer.class_id == 23:
                    item(model, source, 'missing', 'MeshRenderer without a MeshFilter mesh.', key='mesh')
                elif mesh_ref is not None:
                    mesh, fid = mesh_ref
                    mats.add_renderer(entity, mesh, prefab.name(go), renderer.data.get('m_Materials') or [], source,
                                      meta=assets.meta(str(root / mesh)), mesh_file_id=fid, cast=str(renderer.data.get('m_CastShadows', 1)) != '0')
            owner = entity['fields'].get('owner')
            if isinstance(owner,int) and not isinstance(owner,bool) and -1 <= owner <= 8:
                entity['owner'] = owner
                item(model, source, 'approx', 'Explicit owner field mapped; verify source player numbering.', key='owner')
                del entity['fields']['owner']
            meaningful = any(c.class_id not in (4, 224) for c in comps)
            entity['candidate'] = any(c.class_id in (33,137,212,54,65,135,136,64,95,195,114) for c in comps)
            if meaningful:
                model['entities'].append(entity)
                scene['objects'].append(dict(entity=entity['id'], position=entity['position'], rotation=entity['rotation']))
        by_go = {e['source']: e for e in model['entities'] if e['source'].startswith(path + '#')}
        for inst, parent in prefab.model_instances:
            src = reference(inst.data.get('m_SourcePrefab'))
            if not src.lower().endswith(MODEL_EXT):
                continue
            mod = inst.data.get('m_Modification') or {}
            go = None
            tr = prefab.transforms.get(parent)
            while tr is not None and go is None:   # nearest ancestor that became an entity
                g = get(tr.data, 'm_GameObject', 'fileID')
                go = g if (path + '#' + str(g)) in by_go else None
                tr = prefab.transforms.get(get(tr.data, 'm_Father', 'fileID'))
            source = path + '#' + str(inst.file_id)
            entity = by_go.get(path + '#' + str(go)) if go else None
            # free-standing instances of one model with the same materials share a type
            sig = sorted((str(get(m, 'target', 'fileID')), str(m.get('propertyPath')), str(get(m, 'objectReference', 'guid')), str(get(m, 'objectReference', 'fileID')))
                         for m in mod.get('m_Modifications') or [] if isinstance(m, dict) and str(m.get('propertyPath', '')).startswith('m_Materials'))
            shared = src + '|' + repr(sig)
            if entity is None and shared in instances:
                e = instances[shared]
                pos = _instance_position(prefab, mod, parent)
                scene['objects'].append(dict(entity=e['id'], position=[pos[0], pos[1], -pos[2]], rotation=0))
                continue
            if entity is None:
                pos = _instance_position(prefab, mod, parent)
                entity = dict(id=ident(shared), source=source, name=Path(src).stem, fields={}, components=[], models=[], materials=[], scripts=[],
                              position=[pos[0], pos[1], -pos[2]], rotation=0, scale=[1, 1, 1], candidate=True)
                entity.setdefault('modelSettings', {})[src] = assets.meta(str(root / src)).get('ModelImporter', {})
                model['entities'].append(entity)
                by_go[source] = entity
                instances[shared] = entity
                scene['objects'].append(dict(entity=entity['id'], position=entity['position'], rotation=0))
                item(model, source, 'approx', 'Model prefab instance placed from its own position; rotation and scale require review.', key='model-instance')
            else:
                entity.setdefault('modelSettings', {}).setdefault(src, assets.meta(str(root / src)).get('ModelImporter', {}))
            mats.add_instance(entity, src, mod.get('m_Modifications'), source)
        for reason in prefab.nested_failed:
            if 'model instance' in reason:
                continue
            item(model, path, 'missing', reason)
        if path.endswith('.unity'):
            model['scenes'].append(scene)
    # ScriptableObject values are kept as candidates, even without a GameObject hierarchy.
    for a in info['assets']:
        if not a['path'].endswith('.asset') or a['path'] in info['opaqueAssets']: continue
        for d in assets.docs(str(root / a['path'])):
            if d.class_id == 114 and not get(d.data, 'm_GameObject', 'fileID'):
                source = a['path'] + '#' + str(d.file_id)
                model['entities'].append(dict(id=ident(source), source=source, name=d.data.get('m_Name',Path(a['path']).stem),
                    fields={k:v for k,v in d.data.items() if not k.startswith('m_')}, components=[dict(type=d.type,data=d.data)], models=[], materials=[], scripts=[reference(d.data.get('m_Script'))]))
            elif d.class_id != 114:
                item(model, a['path'], 'missing', d.type + ' asset retained in source; specialized conversion required.')
    for asset in info['assets']:
        suffix=Path(asset['path']).suffix.lower()
        if asset['kind']=='other' and suffix not in ('.meta','.unity','.prefab','.asset','.mat','.shadergraph','.shadersubgraph','.shader','.hlsl','.cginc'):
            item(model,asset['path'],'missing','Unsupported source asset; original remains in source project.',key='unsupported-asset')
    build = root / 'ProjectSettings/EditorBuildSettings.asset'
    if build.exists():
        docs = assets.docs(str(build))
        enabled = [s.get('path') for d in docs for s in d.data.get('m_Scenes',[]) if s.get('enabled')]
        order = {p:i for i,p in enumerate(enabled)}
        model['scenes'].sort(key=lambda s:(order.get(s['source'],len(order)),s['source']))
    for warning in rep.warnings:
        item(model, 'Assets', 'missing', warning)
    mats.finish()
    return model


def _instance_position(prefab, mod, parent):
    """Unity-space position of a model prefab instance: its root m_LocalPosition modifications under the parent."""
    pos = [0.0, 0.0, 0.0]
    for m in mod.get('m_Modifications') or []:
        pp = str(m.get('propertyPath', '')) if isinstance(m, dict) else ''
        if pp.startswith('m_LocalPosition.') and pp[-1] in 'xyz':
            try:
                pos['xyz'.index(pp[-1])] = float(m.get('value'))
            except (TypeError, ValueError):
                pass
    if parent and parent in prefab.transforms:
        ppos = prefab.world_of(prefab.transforms[parent])[0]
        pos = [a + b for a, b in zip(pos, ppos)]
    return pos
