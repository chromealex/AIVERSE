#!/usr/bin/env python3
"""Find repository recipes and produce explicit, side-aware library selections (stdlib only)."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT/'skills/catalog.json'

def load_meta():
    spec = importlib.util.spec_from_file_location('meta_catalog', ROOT/'tools/meta/catalog.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

# Search vocabulary is routing metadata, not another copy of skill instructions.
ALIASES = {
 'meta-economy':'shop store offer currency wallet магазин оффер валют кошелек покупк',
 'meta-social':'leaderboard ranking chat лидeрборд лидерборд рейтинг таблица чат',
 'meta-progression':'daily energy battlepass achievement ежеднев энерг достижен боевой пропуск',
 'meta-clans':'clan guild клан гильди', 'meta-items':'equipment instance экипиров предмет',
 'meta-tournaments':'tournament league турнир лиг', 'meta-loot':'chest loot сундук лут',
 'meta-albums':'album collection trade альбом коллекц обмен', 'meta-jobs':'queue crafting очередь крафт',
 'meta-research':'research prestige исследован престиж', 'meta-quests':'quest квест задан',
 'meta-events':'event событ ивент', 'meta-wars':'clanwar войн',
 'meta-async-pvp':'raid revenge рейд месть асинхрон', 'meta-matchmaking':'matchmaking матчмейкинг подбор',
 'meta-ratings':'elo glicko эло глико', 'meta-referrals':'referral promo реферал промокод',
 'meta-ads':'rewarded ads реклам', 'meta-offer-chains':'chain цепочк',
 'meta-notifications':'notification badge уведомлен', 'meta-offline':'offline income оффлайн доход',
 'meta-database':'database sqlite migration бд баз данных миграц',
 'meta-backend':'entitlement subscription подписк прав',
 'meta-client':'client outbox retry клиент повтор', 'meta-server':'server сервер сервак',
 'meta-ui':'screen interface экран интерфейс',
 'aiverse-card-runtime':'cards deck карточ колод', 'aiverse-turn-scheduler':'turn пошаг ход',
 'aiverse-grid-puzzles':'puzzle match3 головолом', 'aiverse-rhythm-timing':'rhythm ритм',
 'aiverse-narrative-graphs':'story narrative сюжет диалог', 'rts-bots-rts':'rts bot ртс бот',
 'aiverse-bots-fps':'fps bot фпс бот', 'aiverse-camera':'camera камер',
 'aiverse-orchestrator':'skill catalog orchestration скилл каталог оркестратор',
 'aiverse-combat-actions':'hitbox hurtbox dodge parry stagger poise lockon хитбокс уклон парир стаггер захват',
}

def generate():
    entries=[]
    packages=json.loads((ROOT/'libraries/catalog.json').read_text(encoding='utf-8'))['packages']
    ids={p['id']:Path(p['skill']).parent.name for p in packages}
    for p in packages:
        entries.append(dict(id=ids[p['id']],package=p['id'],path=p['skill'],domain='gameplay',
            summary=p['summary'],side=p['layer'],runtime=p['runtime'],target=p.get('cmakeTarget'),
            dependencies=[ids[x] for x in p.get('packageDependencies',[])],
            backendDependencies=p.get('linkDependencies',[]),requiresOwner=p.get('requiresOwner'),
            source=p['source']))
    modules={p['id']:p for p in json.loads((ROOT/'engine/meta/modules.json').read_text(encoding='utf-8'))['modules']}
    for key,summary,skill,source in load_meta().PACKAGES:
        definition=modules.get(key)
        deps=['meta-'+d for d in definition['dependencies']] if definition else {'database':['meta-server'],'ui':['meta-client'],'server':['meta-economy']}.get(key,[])
        target=definition['target'] if definition else {'client':'AIVERSE::meta_client','server':'AIVERSE::meta_server'}.get(key)
        entries.append(dict(id=skill,package=key,path=f'skills/{skill}/SKILL.md',domain='meta',summary=summary,
            side='client' if key in ('client','ui') else 'database' if key=='database' else 'server',
            runtime='static' if target else 'assets' if key=='ui' else 'none',target=target,
            dependencies=deps,backendDependencies=[],source=source))
    for name,description in [('meta-game','Choose meta-game recipes'),('meta-compose','Compose a selected meta project'),('aiverse-orchestrator','Find skills and resolve minimal library dependencies')]:
        entries.append(dict(id=name,package=None,path=f'skills/{name}/SKILL.md',domain='workflow',summary=description,
            side='tools',runtime='none',target=None,dependencies=[],backendDependencies=[],source='tools/skills/catalog.py'))
    for entry in entries:entry['keywords']=ALIASES.get(entry['id'],'').split()
    return dict(schema=1,skills=sorted(entries,key=lambda e:e['id']))

def validate(data):
    entries=data['skills']; by_id={e['id']:e for e in entries}
    if len(by_id)!=len(entries):raise ValueError('duplicate skill id')
    actual={p.relative_to(ROOT).as_posix() for p in (ROOT/'skills').glob('*/SKILL.md')}
    registered={e['path'] for e in entries}
    if actual!=registered:raise ValueError('unregistered or missing skills: '+str(sorted(actual^registered)))
    targets=set()
    for e in entries:
        path=ROOT/e['path']
        if path.resolve().parent.parent!=(ROOT/'skills').resolve():raise ValueError('skill path escapes catalog')
        if f"\nname: {e['id']}\n" not in path.read_text(encoding='utf-8'):raise ValueError('frontmatter id: '+e['id'])
        if not (ROOT/e['source']).is_file():raise ValueError('missing source: '+e['source'])
        if e['runtime'] in ('static','header'):
            if not e['target'] or e['target'] in targets:raise ValueError('missing or shared library target: '+e['id'])
            targets.add(e['target'])
            if e['target'] not in path.read_text(encoding='utf-8'):raise ValueError('skill missing library contract: '+e['id'])
            if any(d in ('aiverse::meta','AIVERSE::libraries','AIVERSE::genre') for d in e['backendDependencies']):raise ValueError('aggregate dependency: '+e['id'])
        elif e['target'] is not None:raise ValueError('non-code skill has C++ target')
        for dep in e['dependencies']:
            if dep not in by_id:raise ValueError('unknown dependency: '+dep)
        if e['side']=='client' and any(by_id[d]['side'] in ('server','database') for d in e['dependencies']):raise ValueError('client depends on server')
    closure(by_id,list(by_id))
    return by_id

def closure(entries,roots):
    done=set(); active=set(); order=[]
    def visit(key):
        if key not in entries:raise ValueError('unknown skill: '+key)
        if key in active:raise ValueError('dependency cycle: '+key)
        if key in done:return
        active.add(key)
        for dep in sorted(entries[key]['dependencies']):visit(dep)
        active.remove(key);done.add(key);order.append(key)
    for root in sorted(set(roots)):visit(root)
    return order

def search(entries,query,domain='all'):
    words=re.findall(r'[\w-]+',query.lower())
    results=[]
    for e in entries.values():
        if domain!='all' and e['domain']!=domain:continue
        aliases=e['keywords']; tokens=re.findall(r'[\w-]+',(e['id']+' '+e['summary']).lower())
        score=sum(4 if any(w==a or (len(a)>=4 and w.startswith(a)) for a in aliases) else 1 if w in tokens else 0 for w in words)
        if query in (e['id'],e['package']):score+=20
        if score:results.append((score,e))
    return sorted(results,key=lambda row:(-row[0],row[1]['id']))

def plan(entries,roots,side):
    original=sorted(set(roots)); selected=list(original)
    if side=='client':
        # Policies are server capabilities; a client calls their protocol instead of linking them.
        selected=[key for key in selected if entries[key]['side'] not in ('server','database')]
        if any(entries[key]['domain']=='meta' for key in original):selected.append('meta-client')
    elif side=='server':
        selected=[key for key in selected if entries[key]['side']!='client']
    if any(entries[key]['domain']=='meta' and entries[key]['side']=='server' for key in selected):selected.append('meta-server')
    order=closure(entries,selected)
    choices=[entries[key] for key in order]
    policies=[e['package'] for e in choices if e['domain']=='meta' and e['runtime']=='static' and e['package'] not in ('server','client')]
    gameplay=[e['package'] for e in choices if e['domain']=='gameplay' and e['runtime']=='header']
    commands=[]
    if policies:commands.append('aiverse_add_meta_server(game_meta MODULES '+' '.join(policies)+')')
    if 'meta-client' in order:commands.append('target_link_libraries(game_client PRIVATE AIVERSE::meta_client)')
    if gameplay:commands.append('aiverse_native_use_libraries(aiverse_native_GAME '+' '.join(gameplay)+')')
    return dict(requested=original,side=side,read=[e['path'] for e in choices],
        selection=[{k:e.get(k) for k in ('id','side','runtime','target','dependencies','requiresOwner')} for e in choices],
        cmake=commands,nativeCmake=(["aiverse_native_use_meta(aiverse_native_GAME game_meta)"] if policies else [])+(["aiverse_native_use_meta(aiverse_native_GAME CLIENT)"] if 'meta-client' in order else []),serverModules=policies,clientFeatures=[entries[key]['package'] for key in original if entries[key]['domain']=='meta'] if side=='client' else [])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['generate','check','list','search','plan'])
    parser.add_argument('query',nargs='?',default='')
    parser.add_argument('--skills',help='comma-separated exact skill IDs')
    parser.add_argument('--domain',choices=['all','meta','gameplay','workflow'],default='all')
    parser.add_argument('--side',choices=['both','client','server'],default='both')
    args=parser.parse_args()
    try:
        expected=generate()
        if args.command=='generate':
            validate(expected);CATALOG.write_text(json.dumps(expected,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('Generated skill catalog');return
        data=json.loads(CATALOG.read_text(encoding='utf-8'));entries=validate(data)
        if args.command=='check':
            if data!=expected:raise ValueError('stale skill catalog; run generate')
            print(f'Validated {len(entries)} skills and isolated library mappings');return
        if args.command=='list':result=[{k:e[k] for k in ('id','summary','side','runtime','target')} for e in entries.values() if args.domain=='all' or e['domain']==args.domain]
        elif args.command=='search':result=[dict(score=score,**{k:e[k] for k in ('id','summary','side','path','target')}) for score,e in search(entries,args.query,args.domain)][:12]
        else:
            if args.skills:roots=[x.strip() for x in args.skills.split(',') if x.strip()]
            else:
                hits=search(entries,args.query,args.domain)
                # Explicit aliases select all mentioned capabilities; weak full-text hits are suggestions only.
                roots=[e['id'] for score,e in hits if score>=4]
                if not roots:raise ValueError('no unambiguous match; use search then --skills with exact IDs')
            for key in roots:
                if key not in entries:raise ValueError('unknown skill: '+key)
            result=plan(entries,roots,args.side)
        print(json.dumps(result,ensure_ascii=False,indent=2))
    except (ValueError,OSError,KeyError) as error:parser.error(str(error))
if __name__=='__main__':main()
