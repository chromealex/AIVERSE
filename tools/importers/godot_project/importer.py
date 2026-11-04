"""Godot text resource reader. Unsupported variants remain visible in the report."""
import ast
import re
from pathlib import Path
from lib.port.common import inventory, item, new_model, ident, safe, script_index


def value(text):
    text = text.strip()
    if text in ('true','false'): return text=='true'
    if text == 'null': return None
    try: return ast.literal_eval(text)
    except (ValueError,SyntaxError): pass
    m = re.fullmatch(r'(Vector[23]|Rect2|Transform3D|Transform|Transform2D)\(([^()]*)\)',text)
    if m:
        try: return {'variant':m[1],'values':[float(x.strip()) for x in m[2].split(',')]}
        except ValueError: pass
    return {'raw':text}


def sections(text):
    out=[]; current=None
    for line in text.splitlines():
        line=line.strip()
        if not line or line.startswith(';'): continue
        if line.startswith('[') and line.endswith(']'):
            head=line[1:-1]; kind=head.split()[0]
            attrs={k:value(v) for k,v in re.findall(r'(\w+)=("(?:[^"\\]|\\.)*"|[^\s]+)',head)}
            current={'kind':kind,'attrs':attrs,'values':{}}
            out.append(current)
        elif current is not None and '=' in line:
            key,val=line.split('=',1); current['values'][key.strip()]=value(val)
    return out


def scan(root):
    assets=inventory(root)
    text=(root/'project.godot').read_text(encoding='utf-8-sig')
    match=re.search(r'config_version\s*=\s*(\d+)',text)
    version='4' if match and match[1]=='5' else '3' if match and match[1]=='4' else 'unknown'
    return dict(engine='godot',version=version,assets=assets,scenes=[a['path'] for a in assets if a['path'].endswith('.tscn')],
                scripts=[a['path'] for a in assets if a['kind']=='script'],bytes=sum(a['bytes'] for a in assets),ok=True,errors=[])


def extract(root):
    info=scan(root); model=new_model(root,'godot',info['version'])
    model['assets']=[a for a in info['assets'] if a['kind']!='other']
    scripts={a['path']:script_index(model,root,a) for a in model['assets'] if a['kind']=='script'}
    for path in info['scenes'] + [a['path'] for a in info['assets'] if a['path'].endswith('.tres')]:
        blocks=sections(safe(root,path).read_text(encoding='utf-8-sig'))
        refs={str(s['attrs'].get('id')):re.sub(r'^res://', '', s['attrs'].get('path','')) for s in blocks if s['kind']=='ext_resource'}
        def ref(v):
            raw=v.get('raw','') if isinstance(v,dict) else ''
            match=re.fullmatch(r'ExtResource\(\s*"?([^"\s)]+)"?\s*\)',raw)
            return refs.get(match[1],'') if match else ''
        scene=dict(id=ident(path),source=path,name=Path(path).stem,objects=[])
        world={}
        for block in blocks:
            if block['kind'] not in ('node','resource'):
                if block['kind']=='sub_resource': item(model,path,'missing','Embedded resource requires conversion.',key='sub'+str(block['attrs'].get('id')),details=block)
                continue
            a,v=block['attrs'],block['values']; name=a.get('name',Path(path).stem); parent=a.get('parent','')
            source=path+'#'+parent+'/'+name
            pos=[0,0,0]
            tr=v.get('transform',{}); pv=v.get('position',v.get('translation',{}))
            if isinstance(pv,dict) and pv.get('variant') in ('Vector2','Vector3'):
                ns=pv['values']; pos=ns if len(ns)==3 else [ns[0],0,ns[1]]
            elif isinstance(tr,dict) and tr.get('variant') in ('Transform','Transform3D') and len(tr['values'])==12:
                pos=tr['values'][-3:]
            elif isinstance(tr,dict) and tr.get('variant')=='Transform2D' and len(tr['values'])==6:
                pos=[tr['values'][-2],0,tr['values'][-1]]
            parent_pos=world.get(parent,[0,0,0]); pos=[x+y for x,y in zip(pos,parent_pos)]
            node_path=name if not parent else name if parent=='.' else parent+'/'+name
            world[node_path]=pos
            if not parent: world['.']=pos
            script=ref(v.get('script')); mesh=ref(v.get('mesh')) or ref(a.get('instance'))
            fields={k:val for k,val in v.items() if k not in ('position','translation','transform','rotation','scale','script','mesh') and isinstance(val,(int,float))}
            entity=dict(id=ident(source),source=source,name=name,fields=fields,position=pos,models=[mesh] if mesh and Path(mesh).suffix.lower() in ('.glb','.gltf','.obj','.fbx') else [],
                        materials=[],scripts=[script] if script else [],components=[block])
            if script in scripts: scripts[script]['uses'].append(dict(entity=entity['id'],fields=fields))
            kind = a.get('type','')
            if kind.endswith('2D') or (isinstance(pv,dict) and pv.get('variant')=='Vector2'): entity['dimension']='2d'
            elif kind.endswith('3D') or kind=='Spatial': entity['dimension']='3d'
            texture = ref(v.get('texture'))
            if kind in ('Sprite','Sprite2D') and texture:
                entity['sprite'] = dict(file=texture,pixelsPerUnit=1,origin='topLeft')
                rect = v.get('region_rect',{})
                if v.get('region_enabled') and isinstance(rect,dict) and rect.get('variant')=='Rect2' and len(rect['values'])==4:
                    entity['sprite']['rect'] = dict(zip(('x','y','width','height'),rect['values']))
                if v.get('hframes',1)!=1 or v.get('vframes',1)!=1:
                    item(model,source,'missing','Sprite sheet animation frames require action mapping.',key='sprite-frames')
            entity['candidate'] = bool(entity['models'] or fields or script or entity.get('sprite'))
            model['entities'].append(entity)
            scene['objects'].append(dict(entity=entity['id'],position=pos,rotation=0))
            if a.get('type') in ('TileMap','TileMapLayer','GridMap','AnimationPlayer') or a.get('instance') or any(k in v for k in ('rotation','scale','transform')):
                item(model,source,'missing','Node instancing, tile data or transform basis requires review; serialized node retained.',key='node',details=block)
        if path.endswith('.tscn'): model['scenes'].append(scene)
    project=(root/'project.godot').read_text(encoding='utf-8-sig')
    main=re.search(r'run/main_scene\s*=\s*"res://([^"]+)"',project)
    if main: model['scenes'].sort(key=lambda s:(s['source']!=main[1],s['source']))
    return model
