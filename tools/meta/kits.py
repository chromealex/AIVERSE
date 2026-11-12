#!/usr/bin/env python3
"""Compose only selected reusable meta policies, validate before writing project files."""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LIVE = {'loot':'loot','albums':'albums','jobs':'jobs','research':'research','quests':'quests',
        'events':'events','wars':'wars','async-pvp':'asyncPvp','matchmaking':'matchmaking',
        'ratings':'ratings','referrals':'referrals','ads':'ads','offer-chains':'chains',
        'notifications':'notifications','offline':'offline'}
MODULES = {m['id']:m for m in json.loads((ROOT/'engine/meta/modules.json').read_text())['modules']}
DEPENDENCIES = {key:value['dependencies'] for key,value in MODULES.items()}
DEPENDENCIES.update({'server':['economy'], 'database':['server'], 'client':[], 'ui':['client']})
PRESETS = {'minimal':['economy','progression'], 'mobile':['economy','progression','social','ui'],
           'competitive':['social','tournaments','matchmaking','ratings','ui'],
           'full':list(DEPENDENCIES)}

def resolve(requested):
    result=set()
    def visit(key):
        if key not in DEPENDENCIES:raise ValueError('unknown package: '+key)
        if key in result:return
        result.add(key)
        for dependency in DEPENDENCIES[key]:visit(dependency)
    for key in requested:visit(key)
    return sorted(result)

def definitions(features):
    service=json.loads((ROOT/'examples/meta/service.json').read_text())
    economy=copy.deepcopy(service['economy'])
    if 'progression' not in features:
        for key in ('tracks','daily','energy'):economy.pop(key,None)
    extensions={'initial':service['extensions']['initial']}
    for package in ('clans','items','tournaments'):
        if package in features:extensions[package]=service['extensions'][package]
    if 'social' in features:extensions['friends']=service['extensions']['friends']
    if 'backend' in features:extensions['entitlements']=service['extensions']['entitlements']
    live=json.loads((ROOT/'examples/meta/liveops.json').read_text())
    chosen={LIVE[key]:live[LIVE[key]] for key in features if key in LIVE}
    if chosen:extensions['liveops']=chosen
    screens=['shop','mail']
    if 'progression' in features:screens+=['progress']
    if 'social' in features:screens+=['boards','chat','social']
    if 'items' in features:screens+=['items']
    if 'tournaments' in features:screens+=['events']
    economy.setdefault('metadata',{})['metaScreens']=screens
    return dict(version=1,economy=economy,
                boards=service['boards'] if 'social' in features else {},
                # Reference host has global channels; private/clan channels require membership routing.
                chats={'global':service['chats']['global']} if 'social' in features else {},
                extensions=extensions)

def plan(requested,side="both"):
    from catalog import PACKAGES
    if side not in ('both','client','server'):raise ValueError('unknown side')
    selected=resolve(requested)
    screen_features=list(selected)
    if side=='client':selected=sorted(set(['client']+[key for key in selected if key=='ui']))
    elif side=='server':selected=[key for key in selected if key not in ('client','ui')]
    entries={key:dict(skill=f'skills/{skill}/SKILL.md',source=source) for key,_,skill,source in PACKAGES}
    return dict(version=2,side=side,serverModules=[key for key in selected if key in MODULES] if side!="client" else [],requested=sorted(set(requested)),resolved=selected,
                recipes={key:entries[key] for key in selected},
                screens=definitions(screen_features)['economy']['metadata']['metaScreens'])

def apply(project,requested,validator,side="both"):
    project=project.resolve();recipe=plan(requested,side);features=recipe['resolved']
    targets=[project/'meta']+([project/'ui/meta'] if 'ui' in features and side!='server' else [])
    if any(p.exists() or p.is_symlink() for p in targets):raise ValueError('destination exists; edit current definitions instead')
    # Stage all content and validate with the actual C++ implementation before installation.
    project.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.aiverse-meta-',dir=project.parent) as temporary:
        stage=Path(temporary);meta=stage/'meta';meta.mkdir()
        if side!='client':
            config=definitions(features)
            for key in ('economy','boards','chats','extensions'):
                (meta/(key+'.json')).write_text(json.dumps(config[key],ensure_ascii=False,indent=2)+'\n')
            generated=meta/'service.generated.json';generated.write_text(json.dumps(config,indent=2)+'\n')
            subprocess.run([str(validator.resolve()),'service',str(generated)],check=True,capture_output=True,text=True)
            server=meta/'server';server.mkdir()
            selected=recipe['serverModules'] or ['economy']
            (server/'CMakeLists.txt').write_text('aiverse_add_meta_server(game_meta MODULES '+' '.join(selected)+')\n')
            if 'database' in features:shutil.copytree(ROOT/'tools/meta/db',server/'db')
        if side!='server':
            client=meta/'client';client.mkdir()
            (client/'CMakeLists.txt').write_text('add_library(game_meta_client INTERFACE)\ntarget_link_libraries(game_meta_client INTERFACE AIVERSE::meta_client)\n')
            (client/'features.json').write_text(json.dumps({'features':recipe['requested'],'screens':recipe['screens']},indent=2)+'\n')
        (meta/'kit.json').write_text(json.dumps(recipe,indent=2)+'\n')
        context=['# Project meta integration','', 'Use aiverse::meta::Service on the host and aiverse::meta::Client for backend requests.',
                 'Client: AIVERSE::meta_client. Server: add_subdirectory(meta/server), link game_meta.',
                 'Server construction: include game_meta.h; Service(kits::game_meta(), config, state, commit, draw).',
                 'Native OBJECT modules call aiverse_native_use_meta(target CLIENT) or (target game_meta).',
                 'Server JSON/SQL must never be bundled as client assets.',
                 'Keep network/time/profile state outside deterministic simulation callbacks.',
                 'Edit source JSON; compose.py validates and atomically replaces service.generated.json.',
                 'Do not generate another wallet, ranking, chat, receipt or retry implementation.', '', 'Selected recipes:']
        context += [f'- {key}: {value["skill"]}' for key,value in recipe['recipes'].items()]
        context += ['', 'Validate: tools/meta/compose.py --project PROJECT --validator VALIDATOR',
                    'Pin: tools/meta/catalog.py pin --project PROJECT',
                    'Browser screens require the authenticated reference host; they are not RmlUi documents.']
        (meta/'AGENTS.md').write_text('\n'.join(context)+'\n')
        if 'ui' in features and side!='server':
            shutil.copytree(ROOT/'examples/meta/ui',stage/'ui/meta')
        project.mkdir(parents=True,exist_ok=True)
        installed=[]
        try:
            for target in targets:
                if target.exists() or target.is_symlink():raise ValueError('destination appeared during generation')
                target.parent.mkdir(parents=True,exist_ok=True)
                # mkdir claims ownership exclusively; never replace another writer's directory.
                target.mkdir();installed.append(target)
                shutil.copytree(stage/target.relative_to(project),target,dirs_exist_ok=True)
        except Exception:
            for target in reversed(installed):shutil.rmtree(target)
            raise
    return recipe

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('command',choices=['plan','apply'])
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument('--preset',choices=PRESETS)
    group.add_argument('--features',help='comma-separated catalog package IDs')
    p.add_argument('--side',choices=['both','client','server'],default='both')
    p.add_argument('--project',type=Path);p.add_argument('--validator',type=Path)
    a=p.parse_args();requested=PRESETS[a.preset] if a.preset else a.features.split(',')
    try:
        if a.command=='apply':
            if not a.project or not a.validator:p.error('apply requires --project and --validator')
            result=apply(a.project,requested,a.validator,a.side)
        else:result=plan(requested,a.side)
        print(json.dumps(result,ensure_ascii=False,indent=2))
    except (ValueError,OSError,subprocess.CalledProcessError) as error:
        p.error(error.stderr if isinstance(error,subprocess.CalledProcessError) else str(error))
if __name__=='__main__':main()
