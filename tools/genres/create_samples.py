#!/usr/bin/env python3
"""Rebuild the small, engine-data-only RTS genre examples. No external assets."""
import copy
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
def unit(hp=100,damage=10,speed=3):
    return {'health':{'max':hp},'move':{'speed':speed,'radius':0.3},'sight':12,'weapons':[{'range':4,'damage':damage,'cooldown':0.5}],'view':{'shape':'soldier'}}
def building(hp=500):
    return {'structure':{'footprint':[2,2]},'health':{'max':hp},'sight':12,'view':{'shape':'house'}}
def entity(kind,x,z,owner=1,**extra):return dict(type=kind,owner=owner,pos=[x,z],**extra)
def base():
    return {'victory':{'mode':'survive','seconds':90},'fogOfWar':{'mode':'none'},'resources':{'gold':{'start':100},'wood':{'start':50},'food':{'start':20}},'types':{}}, {'name':'Genre arena','size':[32,24],'players':[{'team':1,'name':'Player'}],'terrain':{'rows':['.'*32 for _ in range(24)]},'entities':[]}
COMMON='''function on_start() state.kills = 0; state.choices = 0; state.boss = false end
on("unit_died", function(e)
  if e.unit_type == "boss" then state.boss = true end
  if e.owner == 2 then state.kills = state.kills + 1; game.set_var("kills", state.kills) end
end)
'''
for genre in ['action-rpg','survivors','survival','colony','city-builder','turn-based','moba','idle','tower-defense']:
    r,m=base();t=r['types'];script=''
    if genre in ['action-rpg','survivors','moba','turn-based','tower-defense']:
        m['players'].append({'team':2,'name':'Opposition','waves':genre not in ['moba','turn-based']})
        t['hero']=unit(600,24);t['hero'].update(hero={'levels':[30,90,180],'perLevel':[{'stat':'damage','add':4}]},inventory={'slots':6},key=True)
        t['enemy']=unit(45,3,2);t['enemy'].update(xp=15,bounty={'gold':5},drops=[{'item':'potion','chance':1}])
        r['items']={'potion':{'cost':{'gold':10},'use':{'heal':100},'charges':1},'boots':{'cost':{'gold':30},'mods':[{'stat':'speed','add':1}],'charges':0,'consumable':False}}
        t['shop']=building();t['shop']['shop']={'sells':['potion','boots'],'radius':8}
        m['entities']=[entity('hero',10,12),entity('shop',7,9)]
        r['commands']={'move_hero':{'label':'Move hero','mapClick':True,'card':False,'args':[{'name':'where','type':'point'}]}}
        script=COMMON+'''function on_command(player, id, args)
  local heroes = game.units{owner=player,type="hero"}
  if #heroes == 0 then return false end
  if id == "move_hero" then game.order(heroes,"move",args.where.x,args.where.z); return true end
'''
        if genre=='survivors':
            for key,hotkey in [('power','1'),('speed','2')]:r['commands'][key]={'label':key.title(),'hotkey':hotkey}
            script+='''  if (id == "power" or id == "speed") and state.choices > 0 then
    game.buff(heroes[1],id == "power" and "damage" or "speed",1,1)
    state.choices = state.choices - 1; game.set_var("choices",state.choices); return true
  end
'''
        script+='  return false\nend\n'
        if genre in ['action-rpg','survivors','tower-defense']:
            m['waves']=[{'time':5,'player':2,'at':[20,12],'spread':2,'spawn':[{'type':'enemy','count':3}],'every':10,'growth':1.2,'repeats':4}]
            if genre in ['action-rpg','survivors']:
                t['boss']=unit(250,8,2);t['boss']['xp']=90;t['boss']['view']['scale']=1.6
                m['waves'].append({'time':45,'player':2,'at':[20,12],'spread':1,'spawn':[{'type':'boss','count':1}]})
            script+='''every(1,function()
  if state.kills >= 12 and state.boss then game.victory(1) end
end)
'''
        if genre=='survivors':script+='''on("level_up",function(e)
  if e.player == 1 then state.choices=state.choices+1; game.set_var("choices",state.choices) end
end)
'''
        if genre=='action-rpg':
            t['hero']['energy']={'max':100,'regen':5,'start':100}
            t['hero']['abilities']=[{'id':'burst','name':'Burst','target':'none','cooldown':4,'energy':10,'effects':[{'type':'damage','amount':30,'radius':5,'affects':'enemies'}]}]
        if genre=='turn-based':
            r['victory']='noUnits';r['turns']={'moveSeconds':3,'attacks':1,'timeLimit':8}
            m['entities']=[entity('hero',10,12),entity('enemy',14,12,2),entity('enemy',15,14,2)]
            r.pop('commands',None);script=''
        if genre=='moba':
            r['victory']='keyUnits';r['waveClock']=5
            t['core']=building(600);t['core'].update(key=True,income={'gold':2},waveSpawn={'unit':'enemy','count':2},produces=['hero'],view={'shape':'hall'})
            t['hero']['key']=False;t['hero']['cost']={'gold':30,'time':5}
            m['entities']=[entity('core',5,12),entity('core',27,12,2),entity('hero',10,12),entity('hero',22,12,2),entity('shop',7,8),entity('shop',25,8,2)]
        if genre=='tower-defense':
            t['core']=building(1000);t['core']['key']=True
            t['tower']=building();t['tower'].update(cost={'gold':20,'time':2},weapons=[{'range':8,'damage':15,'cooldown':0.4}],upgrades=['tower2'],view={'shape':'tower'})
            t['tower2']=copy.deepcopy(t['tower']);t['tower2'].pop('upgrades');t['tower2']['weapons'][0]['damage']=30
            t['builder']=unit(100,0);t['builder'].pop('weapons');t['builder']['builds']=['tower']
            m['entities']=[entity('core',8,12),entity('tower',11,12),entity('builder',9,9)]
            r.pop('commands',None);script=COMMON
    else:
        t['worker']=unit(100,0);t['worker'].pop('weapons');t['worker']['builds']=['farm','home']
        t['worker']['gather']={'wood':{'time':2,'carry':10}}
        t['home']=building();t['home'].update(cost={'wood':10,'time':3},income={'gold':2},supply=10)
        t['farm']=building();t['farm'].update(cost={'wood':10,'time':3},income={'food':2})
        t['tree']=building(100);t['tree'].update(resourceNode={'resource':'wood','amount':200},invulnerable=True,view={'shape':'tree'})
        t['depot']=building();t['depot'].update(dropoff=['wood'],produces=['worker'],view={'shape':'hall'})
        t['worker']['cost']={'food':5,'time':3}
        m['entities']=[entity('depot',6,12),entity('worker',9,12),entity('farm',7,7),entity('home',7,17)]+[entity('tree',15+i*2,6,0) for i in range(5)]
        if genre in ['colony','city-builder']:
            r['storage']='buildings'
            t['depot']['storehouse']={'capacity':2000}
            t['worker']['carrier']={'capacity':5}
            t['farm'].pop('income');t['farm']['recipe']={'out':{'food':2},'time':2,'buffer':3}
            t['home']['recipe']={'in':{'food':1},'out':{'gold':3},'time':3,'buffer':3}
            t['home'].pop('income')
            t['carrier']=unit(100,0);t['carrier'].pop('weapons');t['carrier']['carrier']={'capacity':5}
            for i in range(3):m['entities'].append(entity('carrier',10+i,14))
        if genre=='colony':t['worker']['needs']={'hunger':{'interval':15,'at':'home','time':1,'cost':{'food':1},'starve':1}}
        if genre=='city-builder':
            r['roadNetwork']={'cost':{'wood':1},'speed':1.5,'required':True}
            m['terrain']['rows']=['.'*5+'+'*5+'.'*22 for _ in range(24)]
        if genre=='survival':
            t['worker']['needs']={'hunger':{'interval':10,'at':'home','time':1,'cost':{'food':2},'starve':2}}
            t['campfire']=building();t['campfire'].update(cost={'wood':20,'time':3},income={'gold':1})
            t['worker']['builds'].append('campfire')
        if genre=='idle':
            t['home']['upgrades']=['home2'];t['home2']=copy.deepcopy(t['home']);t['home2'].pop('upgrades');t['home2']['income']={'gold':5};t['home2']['cost']={'gold':30,'time':1}
            t['home2']['upgrades']=['home3'];t['home3']=copy.deepcopy(t['home2']);t['home3'].pop('upgrades');t['home3']['income']={'gold':12};t['home3']['cost']={'gold':100,'time':1}
            m['entities']=[entity('home',12,12)]
        script='''every(1,function()
  if game.resource(1,"gold") >= fx(250) then game.victory(1) end
end)
'''
    if script:r['scripts']=['scripts/game.lua']
    for key,value in t.items():value['name']=key.replace('_',' ').title()
    path=ROOT/'examples/genres/rts'/genre
    write(path/'rules.json',r);write(path/'maps/start.json',m)
    write(path/'game.json',{'id':'com.aiverse.genre.'+genre.replace('-','_'),'name':genre.replace('-',' ').title(),'description':'Minimal genre mechanics sample','version':'0.1.0','module':'rts','genre':genre,'rules':'rules.json','maps':[{'file':'maps/start.json','title':'Genre arena'}],'sourceLanguage':'en','languages':['en'],'camera':{'projection':'ortho','zoom':30,'pitch':65}})
    if script:(path/'scripts').mkdir(exist_ok=True);(path/'scripts/game.lua').write_text(script)
