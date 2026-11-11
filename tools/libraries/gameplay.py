#!/usr/bin/env python3
"""Config validation, repeatable project edits, content audit, backups and headless gates."""
import argparse,copy,hashlib,json,os,re,shutil,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SPEC=json.loads((ROOT/'libraries/spec/gameplay.json').read_text())

def read_json(path):
    text=Path(path).read_text(encoding='utf-8-sig')
    # Preserve quoted strings while stripping the engine's supported JSON comments.
    text=re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',lambda m:m.group(0) if m.group(0).startswith('"') else ' ',text)
    def pairs(entries):
        out={}
        for key,value in entries:
            if key in out:raise ValueError('duplicate key: '+key)
            out[key]=value
        return out
    return json.loads(text,object_pairs_hook=pairs,parse_constant=lambda value:(_ for _ in ()).throw(ValueError('invalid number '+value)))

def encode(value):return (json.dumps(value,indent=2,ensure_ascii=False,allow_nan=False)+'\n').encode()
def digest(data):return hashlib.sha256(data).hexdigest()
def atomic(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as f:
        name=f.name;f.write(data);f.flush();os.fsync(f.fileno())
    try:os.replace(name,path)
    finally:
        if os.path.exists(name):os.unlink(name)

def settings(document):
    if not isinstance(document,dict) or type(document.get('schemaVersion')) is not int or document['schemaVersion']!=1:raise ValueError('schemaVersion must be 1')
    unknown=set(document)-{'schemaVersion',*SPEC['packages']}
    if unknown:raise ValueError('unknown sections: '+','.join(sorted(unknown)))
    result={}
    for name,package in SPEC['packages'].items():
        values=document.get(name,{})
        if not isinstance(values,dict):raise ValueError(name+': expected object')
        if set(values)-set(package['fields']):raise ValueError(name+': unknown fields')
        out={}
        for key,field in package['fields'].items():
            value=values.get(key,field['default']);kind=bool if field['type']=='boolean' else int
            if type(value) is not kind:raise ValueError(name+'.'+key+': wrong type')
            if kind is int and not field['minimum']<=value<=field['maximum']:raise ValueError(name+'.'+key+': out of range')
            out[key]=value
        for low,high in package.get('relations',[]):
            if out[low]>out[high]:raise ValueError(name+'.'+low+': exceeds '+high)
        result[name]=out
    return result

def recipe(project,name,payload):
    definitions=read_json(ROOT/'libraries/recipes/catalog.json')
    if name not in definitions:raise ValueError('unknown recipe')
    rules_path=project/'rules.json';before=rules_path.read_bytes();rules=read_json(rules_path)
    identifier=payload.get('id','');definition=payload.get('definition')
    if not re.fullmatch(r'[a-z][a-z0-9_]{0,63}',identifier) or not isinstance(definition,dict):raise ValueError('id and definition object required')
    if name=='ability':
        owner=payload.get('owner');types=rules.get('types',{})
        if owner not in types:raise ValueError('unknown owner type')
        items=types[owner].setdefault('abilities',[])
        if any(a.get('id')==identifier for a in items):raise ValueError('ability already exists')
        definition=copy.deepcopy(definition);definition['id']=identifier;items.append(definition)
    else:
        section={'resource':'resources','building':'types','faction':'factions'}[name]
        group=rules.setdefault(section,{})
        if identifier in group:raise ValueError('ID already exists')
        group[identifier]=definition
        if name=='building':
            if 'structure' not in definition:raise ValueError('building requires structure')
            builders=payload.get('builders',[])
            if not builders:raise ValueError('building requires at least one builder')
            for builder in builders:
                if builder not in rules['types']:raise ValueError('unknown builder '+str(builder))
                build_list=rules['types'][builder].setdefault('builds',[])
                if identifier not in build_list:build_list.append(identifier)
    return {'schema':1,'recipe':name,'baseSha256':digest(before),'rules':rules,'checks':definitions[name]['checks']}

def apply_plan(project,plan,binary):
    if plan.get('schema')!=1 or not isinstance(plan.get('rules'),dict):raise ValueError('invalid plan')
    target=project/'rules.json';before=target.read_bytes()
    if digest(before)!=plan.get('baseSha256'):raise ValueError('project changed since planning')
    with tempfile.TemporaryDirectory(prefix='rts-recipe-') as temp:
        staged=Path(temp)/'game';shutil.copytree(project,staged,ignore=shutil.ignore_patterns('Builds','build','.git'))
        (staged/'rules.json').write_bytes(encode(plan['rules']))
        check=subprocess.run([str(binary.resolve()),'--project',str(staged),'--max-seconds','0'],capture_output=True,text=True,timeout=120)
        if check.returncode:raise ValueError('engine validation failed: '+check.stdout+check.stderr)
    if target.read_bytes()!=before:raise ValueError('project changed during validation')
    backup=target.with_name('rules.before-'+digest(before)[:12]+'.json')
    if backup.exists() and backup.read_bytes()!=before:raise ValueError('backup collision')
    atomic(backup,before);atomic(target,encode(plan['rules']))
    return {'applied':True,'backup':str(backup)}

def within(path,root):
    try:path.resolve().relative_to(root.resolve());return True
    except ValueError:return False

def audit(project,config):
    cfg=config['content'];files=[];errors=[];warnings=[];total=0
    for directory in ('models','textures','audio','ui','fonts','lang','fx'):
        folder=project/directory
        if not folder.exists():continue
        for path in sorted(folder.rglob('*')):
            if not path.is_file():continue
            if not cfg['allowExternal'] and not within(path,project):
                errors.append({'path':str(path.relative_to(project)),'reason':'external_path'});continue
            size=path.stat().st_size;total+=size;name=str(path.relative_to(project))
            if size>cfg['maxFileBytes']:errors.append({'path':name,'reason':'file_budget','bytes':size})
            with path.open('rb') as stream:
                head=stream.read(24);stream.seek(0);h=hashlib.sha256()
                while block:=stream.read(1048576):h.update(block)
            if path.suffix.lower()=='.png':
                if len(head)<24 or head[:8]!=b'\x89PNG\r\n\x1a\n' or head[12:16]!=b'IHDR':errors.append({'path':name,'reason':'invalid_png'})
                else:
                    width,height=struct.unpack('>II',head[16:24])
                    if min(width,height)==0 or max(width,height)>cfg['maxTextureSide']:errors.append({'path':name,'reason':'texture_budget','size':[width,height]})
            files.append({'path':name,'bytes':size,'sha256':h.hexdigest()})
    if total>cfg['maxTotalBytes']:errors.append({'reason':'total_budget','bytes':total})
    translations=sorted((project/'lang').glob('*.json'))
    if translations:
        languages={p.stem:read_json(p) for p in translations}
        keys=set().union(*(set(v) for v in languages.values()))
        for lang,values in languages.items():
            missing=sorted(keys-set(values))
            if missing:(errors if cfg['requireMatchingLocales'] else warnings).append({'reason':'missing_localization','language':lang,'keys':missing})
    return {'ok':not errors,'files':files,'bytes':total,'errors':errors,'warnings':warnings}

def snapshot(source,destination,config):
    raw=source.read_bytes();cfg=config['saves']
    if len(raw)>cfg['maxBytes']:raise ValueError('snapshot size limit')
    state=read_json(source)
    if len(encode(state))>cfg['maxBytes']:raise ValueError('canonical snapshot size limit')
    envelope={'format':1,'version':cfg['version'],'sha256':digest(encode(state)),'state':state}
    if destination.exists() and cfg['backups']:
        for n in range(cfg['backups'],1,-1):
            old=destination.with_name(destination.name+'.bak'+str(n-1));new=destination.with_name(destination.name+'.bak'+str(n))
            if old.exists():shutil.copy2(old,new)
        shutil.copy2(destination,destination.with_name(destination.name+'.bak1'))
    atomic(destination,encode(envelope));return {'saved':str(destination)}

def restore(source,destination,config):
    if source.stat().st_size>config['saves']['maxBytes']*2+1024:raise ValueError('snapshot size limit')
    data=read_json(source)
    if data.get('format')!=1 or data.get('version')!=config['saves']['version']:raise ValueError('incompatible snapshot')
    state=encode(data.get('state'))
    if digest(state)!=data.get('sha256'):raise ValueError('snapshot checksum mismatch')
    if len(state)>config['saves']['maxBytes']:raise ValueError('snapshot size limit')
    atomic(destination,state);return {'restored':str(destination)}

def verify(project,binary,config):
    cfg=config['verification'];command=[str(binary.resolve()),'--project',str(project),'--seed',str(cfg['seed']),'--max-seconds',str(cfg['maxSeconds'])]
    if cfg['restoreAt']<cfg['maxSeconds']:
        command+=['--verify-restore-at',str(cfg['restoreAt']),'--verify-restore-ticks',str(cfg['restoreTicks'])]
    else:raise ValueError('restoreAt must precede maxSeconds')
    try:
        r=subprocess.run(command,capture_output=True,text=True,timeout=cfg['timeoutSeconds'])
        text=r.stdout+r.stderr
        restore_ok=all('Restore OK ('+kind+')' in text for kind in ('snapshot','serialized','fresh serialized'))
        winner='Result: winner=' in text and 'winner=none' not in text
        return {'ok':r.returncode==0 and restore_ok and (winner or not cfg['requireWinner']),'exit':r.returncode,'restore':restore_ok,'winner':winner,'log':text}
    except subprocess.TimeoutExpired:return {'ok':False,'reason':'timeout'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('command',choices=['validate','recipe','apply','audit','save','restore','verify','pipeline'])
    p.add_argument('--project',type=Path);p.add_argument('--config',type=Path);p.add_argument('--input',type=Path);p.add_argument('--output',type=Path)
    p.add_argument('--recipe',choices=['resource','building','ability','faction']);p.add_argument('--headless',type=Path)
    p.add_argument('--modelconv',type=Path);p.add_argument('--audiopack',type=Path);p.add_argument('--profile',choices=['desktop','mobile','web'],default='mobile')
    a=p.parse_args()
    required={'recipe':['project','recipe','input'],'apply':['project','input','headless'],'audit':['project'],
              'save':['input','output'],'restore':['input','output'],'verify':['project','headless'],'pipeline':['project']}
    for key in required.get(a.command,[]):
        if getattr(a,key) is None:p.error('--'+key+' is required for '+a.command)
    cfg=settings(read_json(a.config) if a.config else {'schemaVersion':1})
    if a.command=='validate':result={'ok':True,'settings':cfg}
    elif a.command=='recipe':result=recipe(a.project,a.recipe,read_json(a.input))
    elif a.command=='apply':result=apply_plan(a.project,read_json(a.input),a.headless)
    elif a.command=='audit':result=audit(a.project,cfg)
    elif a.command=='save':result=snapshot(a.input,a.output,cfg)
    elif a.command=='restore':result=restore(a.input,a.output,cfg)
    elif a.command=='verify':result=verify(a.project,a.headless,cfg)
    else:
        if not a.project or not (a.project/'game.json').is_file():raise ValueError('project required')
        commands=[]
        if a.modelconv:commands.append([str(a.modelconv.resolve()),'--project',str(a.project)])
        if a.audiopack:
            if not a.output:raise ValueError('--output required for audio pipeline')
            commands.append([str(a.audiopack.resolve()),'--project',str(a.project),'--profile',a.profile,'--out',str(a.output)])
        if not commands:raise ValueError('provide existing modelconv and/or audiopack tool')
        results=[]
        for command in commands:
            r=subprocess.run(command,capture_output=True,text=True,timeout=cfg['verification']['timeoutSeconds']);results.append({'command':command,'exit':r.returncode,'log':r.stdout+r.stderr})
            if r.returncode:break
        result={'ok':all(r['exit']==0 for r in results),'steps':results}
    if a.output and a.command not in ('save','restore','pipeline'):atomic(a.output,encode(result))
    print(json.dumps(result,ensure_ascii=False,indent=2))
    if result.get('ok') is False:raise SystemExit(1)
if __name__=='__main__':
    try:main()
    except (ValueError,OSError,TypeError,AttributeError,subprocess.TimeoutExpired) as e:
        print(json.dumps({'ok':False,'error':str(e)},ensure_ascii=False));raise SystemExit(2)
