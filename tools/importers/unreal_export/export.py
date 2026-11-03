"""Run inside Unreal Editor: import export; export.export_project('/absolute/output').

Requires Python Editor Script Plugin. Exports the currently loaded level. All unhandled
assets/properties are recorded, never silently considered converted. Source project is read-only.
"""
import hashlib
import json
import re
from pathlib import Path


def export_project(destination):
    import unreal
    out=Path(destination).expanduser().resolve()
    out.mkdir(parents=True,exist_ok=True)
    if (out/'unreal-export.json').exists(): raise ValueError('choose a fresh export directory')
    data=dict(format=1,version=unreal.SystemLibrary.get_engine_version(),assets=[],entities=[],scenes=[],scripts=[],items=[])
    def identifier(s): return re.sub('[^a-zA-Z0-9_]','_',s.split('.')[-1])[:40]+'_'+hashlib.sha256(s.encode()).hexdigest()[:10]
    def missing(source,reason):
        data['items'].append(dict(id=identifier(source+reason),source=source,status='missing',reason=reason,result=''))
    exported={}
    def asset(obj):
        if obj is None: return ''
        path=obj.get_path_name()
        if path in exported: return exported[path]
        exported[path]=''
        kind=obj.get_class().get_name()
        formats={'StaticMesh':('fbx','model'),'SkeletalMesh':('fbx','model'),'AnimSequence':('fbx','model'),
                 'Texture2D':('png','texture'),'SoundWave':('wav','audio')}
        if kind not in formats:
            missing(path,'No exporter selected for '+kind); return ''
        ext,category=formats[kind]; rel='assets/'+identifier(path)+'.'+ext
        (out/'assets').mkdir(exist_ok=True)
        task=unreal.AssetExportTask()
        task.set_editor_property('object',obj)
        task.set_editor_property('filename',str(out/rel))
        task.set_editor_property('automated',True)
        task.set_editor_property('prompt',False)
        task.set_editor_property('replace_identical',False)
        if unreal.Exporter.run_asset_export_task(task) and (out/rel).is_file():
            data['assets'].append(dict(path=rel,kind=category,source=path))
            exported[path]=rel
            return rel
        missing(path,'Asset export failed: '+str(task.get_editor_property('errors')))
        return ''
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    scene=dict(id='loaded_level',source='Loaded editor level',name='Loaded level',objects=[])
    for actor in sorted(actors,key=lambda a:a.get_path_name()):
        source=actor.get_path_name(); key=identifier(source); loc=actor.get_actor_location(); rot=actor.get_actor_rotation(); scale=actor.get_actor_scale3d()
        # Unreal centimetres/Z-up -> metres/Y-up; +X forward maps to -Z.
        pos=[loc.y/100,loc.z/100,-loc.x/100]
        entity=dict(id=key,source=source,name=actor.get_actor_label(),fields={},models=[],materials=[],components=[],scripts=[],position=pos,scale=[scale.y,scale.z,scale.x])
        defaults=unreal.get_default_object(actor.get_class())
        for name in ('health','max_health','hp','speed','move_speed','damage','attack_damage','range','attack_range','cooldown','fire_rate','cost','build_time'):
            try:
                val=actor.get_editor_property(name)
                if isinstance(val,(int,float,str,bool)): entity['fields'][name]=val
                entity.setdefault('classDefaults',{})[name]=str(defaults.get_editor_property(name))
            except Exception:
                pass  # Unreal raises when a class has no such reflected property.
        for comp in actor.get_components_by_class(unreal.ActorComponent):
            ctype=comp.get_class().get_name(); entity['components'].append(dict(type=ctype,name=comp.get_name()))
            for prop in ('static_mesh','skeletal_mesh','sound'):
                try: obj=comp.get_editor_property(prop)
                except Exception: continue
                rel=asset(obj)
                if rel and prop!='sound': entity['models'].append(rel)
                if rel and prop=='sound': entity['audio']=dict(file=rel,loop=False)
            if 'Landscape' in ctype: missing(source,'Landscape height/layer export requires Unreal landscape tooling; not exported.')
        cls=actor.get_class().get_path_name()
        if cls.startswith('/Game/'):
            missing(source,'Blueprint graph execution is not exported; class '+cls+' and reflected candidate defaults retained.')
        data['entities'].append(entity)
        scene['objects'].append(dict(entity=key,position=pos,rotation=-rot.yaw))
    data['scenes'].append(scene)
    # Referenced project assets, including textures and clips not directly on actor components.
    for path in sorted(unreal.EditorAssetLibrary.list_assets('/Game',recursive=True,include_folder=False)):
        obj=unreal.EditorAssetLibrary.load_asset(path)
        if obj is not None: asset(obj)
    (out/'unreal-export.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return str(out/'unreal-export.json')
