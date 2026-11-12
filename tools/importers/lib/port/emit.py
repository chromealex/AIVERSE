"""Conservative emitter. Hashes protect edits even when the editor preserves unknown markers."""
import copy
import math
import subprocess
import sys
import tempfile
from pathlib import Path
from .validate import validate
from .tiles import bake
from .presentation import convert_texture, sprite
from .common import digest, encoded, ident, item, read_json, safe, is_within


def set_field(obj, path, value):
    tokens = path.split('.')
    node = obj
    for i, key in enumerate(tokens[:-1]):
        if isinstance(node, list):
            while len(node) <= int(key): node.append({})
            node = node[int(key)]
        else:
            node = node.setdefault(key, [] if tokens[i+1].isdigit() else {})
    node[tokens[-1]] = value


class Writer:
    def __init__(self, out, model):
        self.out, self.model = out, model
        self.manifest = out / '.port-state.json'
        self.old = read_json(self.manifest) if self.manifest.exists() else {'files':{}, 'types':{}}
        self.state = copy.deepcopy(self.old)
        self.state['format'] = 1

    def write(self, relative, data):
        p = safe(self.out, relative)
        if p.exists() and digest(p.read_bytes()) != self.old['files'].get(relative):
            item(self.model, relative, 'missing', 'Existing or edited output preserved.', result=relative, key='conflict')
            return False
        p.parent.mkdir(parents=True, exist_ok=True)
        temp = p.with_name(p.name + '.port-tmp')
        temp.write_bytes(data)
        temp.replace(p)
        self.state['files'][relative] = digest(data)
        return True

    def json(self, relative, value):
        return self.write(relative, encoded(value))

    def rules(self, rules):
        path = self.out / 'rules.json'
        if path.exists():
            existing = read_json(path)
            for key, value in rules['types'].items():
                current = existing.get('types',{}).get(key)
                if current is not None and (not current.get('ported') or digest(encoded(current)) != self.old['types'].get(key)):
                    current.pop('ported', None)
                    item(self.model, key, 'missing', 'Edited type preserved.', result='rules.json', key='conflict')
                else:
                    existing.setdefault('types',{})[key] = value
                    self.state['types'][key] = digest(encoded(value))
            rules = existing
            # Rules are merged by type, unlike binary assets and maps.
            self.old['files']['rules.json'] = digest(path.read_bytes())
        else:
            self.state['types'] = {k:digest(encoded(v)) for k,v in rules['types'].items()}
        self.json('rules.json', rules)

    def finish(self):
        safe(self.out, '.port-state.json').write_bytes(encoded(self.state))


def emit(model, out, scale=1, mapping=None, mode='auto'):
    if mode not in ('auto','2d','3d'): raise ValueError('invalid mode')
    validate(model)
    if not math.isfinite(scale) or scale <= 0: raise ValueError('scale must be finite and > 0')
    model = copy.deepcopy(model)
    neutral = copy.deepcopy(model)
    has2d = any(e.get('sprite') or e.get('dimension') == '2d' for e in model['entities']) or any(s.get('tiles') for s in model['scenes'])
    has3d = any(e.get('models') or e.get('dimension') == '3d' for e in model['entities'])
    is2d = mode == '2d' or (mode == 'auto' and has2d and not has3d)
    if is2d and model['engine'] == 'unity':
        for scene in model['scenes']:
            for o in scene['objects']: o['position'] = [o['position'][0],0,-o['position'][1]]
    root = Path(model['sourceRoot']).resolve()
    out = out.resolve()
    if out == root or is_within(out, root) or is_within(root, out):
        raise ValueError('source and destination must be separate directories')
    for scene in model['scenes']:
        for axis in (0,2):
            values = [o['position'][axis]*scale for o in scene['objects']] + [0]
            if max(values)-min(values)+5 > 2048: raise ValueError('scene exceeds 2048 cells; reduce --scale')
    out.mkdir(parents=True, exist_ok=True)
    model = copy.deepcopy(model)
    writer = Writer(out, model)
    mappings = read_json(Path(__file__).with_name('mapping.json'))
    if mapping: mappings.update(read_json(mapping))
    for key, spec in mappings.items():
        if not isinstance(spec,list) or len(spec)!=2 or not isinstance(spec[0],str) or spec[1] not in ('number','seconds','distance','reciprocal'):
            raise ValueError('invalid mapping for '+str(key))
        if spec[0] not in ('health.max','move.speed','weapons.0.damage','weapons.0.range','weapons.0.cooldown','cost.gold','cost.time'):
            raise ValueError('unsupported mapping target: '+spec[0])
    types = {}
    conversions = {}
    used_materials = set()
    for a in model['assets']:
        src = safe(root, a['path'])
        if a['kind'] == 'script': continue
        dst = 'content/' + a['path']
        if writer.write(dst, src.read_bytes()):
            item(model, a['path'], 'exact', 'Asset copied without conversion.', dst, key='asset')
        if src.suffix.lower() in ('.psd','.tif','.tiff'):
            converted = convert_texture(model, writer, a['path'])
            if converted: conversions[dst] = converted
    for entity in model['entities']:
        if not entity.get('candidate',True): continue
        ty = dict(name=entity['name'], ported=True, view={'shape':'soldier'})
        for key, value in sorted(entity.get('fields',{}).items()):
            if key not in mappings:
                item(model, entity['source'], 'missing', 'Unmapped field: '+key, key='field:'+key, value=value)
                continue
            target, unit = mappings[key]
            if not isinstance(value,(int,float)) or isinstance(value,bool) or not math.isfinite(value) or value < 0:
                item(model, entity['source'], 'missing', 'Non-numeric or negative field: '+key, key='field:'+key, value=value)
                continue
            if unit == 'distance': value *= scale
            if unit == 'reciprocal':
                if not value: continue
                value = 1/value
            set_field(ty, target, value)
            item(model, entity['source'], 'approx', 'Field synonym needs semantic review: '+key, 'rules.json#types.'+entity['id']+'.'+target,
                 key='field:'+key, confidence=0.7, value=value)
        if entity.get('models'):
            ty['view'].update(model='content/'+entity['models'][0], scale=scale * entity.get('modelSettings',{}).get(entity['models'][0],{}).get('meshes',{}).get('globalScale',1), fit=False)
            if len(entity['models']) > 1: item(model, entity['source'], 'missing', 'Multiple meshes require composed model review.', key='meshes')
        slots = (entity.get('materialSlots') or {}).get(entity['models'][0]) if entity.get('models') else None
        if slots and (slots['materials'] or slots['assign']):
            if slots['materials']: ty['view']['materials'] = dict(slots['materials'])
            if slots['assign']: ty['view']['assign'] = dict(slots['assign'])
            used_materials.update(slots['materials'].values()); used_materials.update(slots['assign'].values())
        elif entity.get('materials') and not model.get('materialLibrary'):
            ty['view']['materials'] = {'*':entity['materials'][0]}
        if entity.get('sprite'):
            result = sprite(model, writer, entity, scale)
            if result: ty['sprite'] = result
        if entity.get('rotation'): ty['view']['yaw'] = -entity['rotation']
        if 'health' not in ty:
            ty['health'] = {'max':1}
            item(model, entity['source'], 'approx', 'Placeholder health=1; verify whether this object is gameplay or decor.', key='default-health')
        if entity.get('scale',[1,1,1]) != [1,1,1]:
            item(model, entity['source'], 'missing', 'Non-default transform scale retained in port.json; review model dimensions.', key='scale')
        for mat in ty['view'].get('materials',{}).values():
            if isinstance(mat, dict): retarget_material(mat, conversions)
        types[entity['id']] = ty
    material_report = emit_materials(model, writer, root, used_materials, conversions)
    music = sorted({'content/'+e['audio']['file'] for e in model['entities'] if e.get('audio',{}).get('music') and e['audio'].get('loop') and e['audio'].get('file')})
    if music:
        writer.json('audio.json', {'music': {'match': {'files':music, 'loop':True}}})
        item(model,'AudioSource','approx','Looping camera/manager audio mapped to match playlist.','audio.json',key='music')
    scenes = model['scenes'] or [dict(id='imported', name='Imported scene',source='',objects=[])]
    map_list = []
    for scene in scenes:
        objects = scene['objects']
        xs = [o['position'][0]*scale for o in objects] + [t['x']*scale for t in scene.get('tiles',[])]; zs = [o['position'][2]*scale for o in objects] + [t['y']*scale for t in scene.get('tiles',[])]
        ox, oz = min(xs+[0])-2, min(zs+[0])-2
        width, height = max(16,math.ceil(max(xs+[0])-ox+3)), max(16,math.ceil(max(zs+[0])-oz+3))
        if max(width,height)>2048: raise ValueError('scene exceeds 2048 cells; reduce --scale')
        by_id = {e['id']:e for e in model['entities']}
        ents = [dict(type=o['entity'],owner=by_id[o['entity']].get('owner',0),pos=[round(o['position'][0]*scale-ox,4),round(o['position'][2]*scale-oz,4)]) for o in objects if o['entity'] in types]
        data = dict(name=scene['name'],ported=True,size=[width,height],players=[dict(name='Player '+str(i),team=i) for i in range(1,max([2]+[e['owner'] for e in ents])+1)],entities=ents)
        layer = bake(model,writer,scene,width,height,ox,oz,scale)
        if layer: data['tiles'] = layer
        rel = 'maps/'+scene['id']+'.json'
        writer.json(rel,data)
        map_list.append(dict(file=rel,title=scene['name']))
        item(model,scene['source'],'approx','Scene placed with translated origin; ownership, starts, rotation and victory require review.',rel,key='scene')
    writer.rules(dict(victory='noEntities',resources={'gold':{'name':'Gold','kind':'stock','start':0}},types=types))
    manifest = dict(name=model['name'],id='com.aiverse.'+ident(model['name']),version='0.1.0',module='rts',rules='rules.json',maps=map_list,external=True)
    if is2d: manifest['camera'] = dict(projection='ortho',pitch=90,yaw=0,rotate=False)
    if material_report.get('packaging'): manifest['packaging'] = material_report.pop('packaging')
    writer.json('game.json',manifest)
    writer.json(ident(model['name'])+'.rtsproj',dict(format=1,name=model['name'],manifest='game.json'))
    # VFX runs into a staging folder so repeat imports cannot overwrite edited effects/textures.
    if model['engine']=='unity':
        importer = Path(__file__).resolve().parents[2]/'unity_vfx/import.py'
        for source in sorted({e['effectSource'] for e in model['entities'] if e.get('effectSource')}):
            with tempfile.TemporaryDirectory() as staging:
                proc = subprocess.run([sys.executable,str(importer),'--source',str(safe(root,source)),'--game',staging,'--name',ident(source),'--scale',str(scale),'--json'],capture_output=True,text=True)
                if proc.returncode:
                    item(model,source,'missing','VFX conversion failed: '+proc.stdout[:1000]+proc.stderr[:1000],key='vfx')
                else:
                    report = __import__('json').loads(proc.stdout)
                    for p in sorted(Path(staging).rglob('*')):
                        if p.is_file(): writer.write(p.relative_to(staging).as_posix(),p.read_bytes())
                    item(model,source,'approx','Effects converted; event bindings require semantic review.',key='vfx',details=report)
    writer.write('.rpakignore', b'# Import provenance and source code are editor-only.\nport.json\nport-report.json\nport-report.md\nport-materials.json\n.port-state.json\n*.rtsproj\n')
    writer.json('port.json',neutral)
    report = dict(format=1,engine=model['engine'],sourceRoot=str(root),items=model['items'],scripts=model['scripts'],summary={s:sum(x['status']==s for x in model['items']) for s in ('exact','approx','missing','resolved')})
    context = {e['source']: e for e in model['entities']}
    context.update({s['path']: s for s in model['scripts']})
    for i in report['items']:
        i['fingerprint'] = digest(encoded({'item': i, 'context':context.get(i['source'])}))
    report['counts'] = dict(assets=len(model['assets']),types=len(types),maps=len(map_list),scripts=len(model['scripts']),
                            materials=material_report.get('materials',0),derivedTextures=material_report.get('derived',0))
    old_report = safe(out,'port-report.json')
    if old_report.exists():
        resolved = {i['id']:i for i in read_json(old_report)['items'] if i['status']=='resolved'}
        for i in report['items']:
            old = resolved.get(i['id'])
            if old and old.get('source')==i['source'] and old.get('fingerprint')==i['fingerprint']:
                i.update(status='resolved',resolution=old['resolution'])
    save_report(out,report)
    writer.finish()
    return report


def save_report(out, report):
    report['summary'] = {s:sum(x['status']==s for x in report['items']) for s in ('exact','approx','missing','resolved')}
    safe(out,'port-report.json').write_bytes(encoded(report))
    lines = ['# Project import report','', ' | '.join(f'{k}: {v}' for k,v in report['summary'].items()),'']
    for i in report['items']:
        lines.append(f"- [{i['status']}] {i['source']}: {i['reason']} → {i['result']} (`{i['id']}`)")
        if i.get('resolution'): lines.append('  Resolution: '+str(i['resolution']))
    safe(out,'port-report.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')


def retarget_material(mat, conversions):
    """Point texture keys (and Shader Graph samples) at converted PNGs of PSD/TIFF sources."""
    for key in ('texture','normalMap','emissiveMap','metallicRoughnessMap','occlusionMap','teamMask'):
        if mat.get(key) in conversions: mat[key] = conversions[mat[key]]
    for node in (mat.get('shaderGraph') or {}).get('nodes', []):
        if node.get('texture') in conversions: node['texture'] = conversions[node['texture']]


def emit_materials(model, writer, root, used, conversions):
    """materials.json library, derived (repacked) textures, texture packaging rules, port-materials.json."""
    lib = model.get('materialLibrary') or {}
    if not lib: return {}
    from unity_materials import texops, textures
    out, derived, failed = {}, {}, []
    for key in sorted(used):
        entry = lib.get(key)
        if not entry: continue
        mat = copy.deepcopy(entry['material'])
        retarget_material(mat, conversions)
        mat['description'] = 'Unity %s (%s)' % (entry['name'], entry['shader'])
        out[key] = mat
        for spec in entry.get('derived', []): derived[spec['out']] = spec
    for dst, spec in sorted(derived.items()):
        ok, note = texops.run(spec, root, writer.write)
        if ok: item(model, spec['sources'][0], 'exact', 'Packed map repacked (%s).' % spec['op'], dst, key='derived:'+dst)
        else:
            failed.append(dst)
            item(model, spec['sources'][0], 'missing', 'Packed map not repacked (%s): %s' % (spec['op'], note), dst, key='derived:'+dst)
    if failed:   # drop references to textures that were not written
        for mat in out.values():
            for k in ('metallicRoughnessMap','occlusionMap'):
                if mat.get(k) in failed: del mat[k]
    writer.json('materials.json', {'materials': out})
    plans = model.get('texturePlans') or []
    pack = textures.packaging([dict(p, path=conversions.get('content/'+p['path'], 'content/'+p['path'])[len('content/'):]) for p in plans])
    used_derived = {d for d in derived if d not in failed}
    families = {}
    for e in lib.values(): families[e['family']] = families.get(e['family'], 0) + 1
    writer.json('port-materials.json', dict(format=1, materials={k: lib[k] for k in sorted(used) if k in lib},
                                            families=families, textures=plans, shaderGraphs=model.get('shaderGraphs') or {},
                                            handWrittenShaders=model.get('handWrittenShaders') or {}))
    return dict(materials=len(out), derived=len(used_derived), packaging=pack if (pack.get('files') or pack.get('targets')) else None)
