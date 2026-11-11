#!/usr/bin/env python3
"""Generate an original articulated spider and a runnable AIVERSE climbing showcase."""
import json, math, re, shutil, struct, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve()
if out.exists(): raise SystemExit('Destination must not exist')
shutil.copytree(root/'templates/showcase',out)
shutil.copytree(root/'engine/assets',out/'engine')
(out/'animations').mkdir(exist_ok=True)
vertices=[];indices=[];views=[];accessors=[];meshes=[];blob=bytearray()
def add(data,component,count,kind,lo=None,hi=None):
    while len(blob)%4: blob.append(0)
    views.append({'buffer':0,'byteOffset':len(blob),'byteLength':len(data)})
    blob.extend(data)
    a={'bufferView':len(views)-1,'componentType':component,'count':count,'type':kind}
    if lo is not None:a.update(min=lo,max=hi)
    accessors.append(a);return len(accessors)-1
def norm(v):
    d=math.sqrt(sum(x*x for x in v));return [x/d for x in v]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def segment(end,width,material):
    axis=norm(end);u=norm(cross(axis,[0,0,1] if abs(axis[2])<.9 else [1,0,0]));v=cross(axis,u)
    corners=[[end[k]*t+width*(u[k]*x+v[k]*y) for k in range(3)] for t in [0,1] for y in [-1,1] for x in [-1,1]]
    faces=[(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)]
    pos=[];normal=[];idx=[]
    for face in faces:
        a,b,c=[corners[j] for j in face[:3]];n=norm(cross([b[k]-a[k] for k in range(3)],[c[k]-a[k] for k in range(3)]));start=len(pos)
        for j in face:pos.append(corners[j]);normal.append(n)
        idx.extend([start,start+1,start+2,start,start+2,start+3])
    lo=[min(p[k] for p in pos) for k in range(3)];hi=[max(p[k] for p in pos) for k in range(3)]
    ap=add(struct.pack('<%sf'%(len(pos)*3),*(x for p in pos for x in p)),5126,len(pos),'VEC3',lo,hi)
    an=add(struct.pack('<%sf'%(len(normal)*3),*(x for p in normal for x in p)),5126,len(normal),'VEC3')
    ai=add(struct.pack('<%sH'%len(idx),*idx),5123,len(idx),'SCALAR')
    meshes.append({'primitives':[{'attributes':{'POSITION':ap,'NORMAL':an},'indices':ai,'material':material}]});return len(meshes)-1
nodes=[{'name':'body','translation':[0,.5,0],'mesh':segment([0,.15,0],.25,0),'children':[]}];legs=[]
for i in range(8):
    side=1 if i%2 else -1;z=(-.45,-.15,.15,.45)[i//2];hip=len(nodes);knee=hip+1;foot=hip+2
    a=[side*.35,-.1,z*.2];b=[side*.2,-.4,0]
    nodes[0]['children'].append(hip)
    nodes.extend([{'name':f'hip{i}','translation':[side*.25,0,z],'children':[knee],'mesh':segment(a,.035,0)},
                  {'name':f'knee{i}','translation':a,'children':[foot],'mesh':segment(b,.028,1)},
                  {'name':f'foot{i}','translation':b,'mesh':segment([0,.035,0],.045,1)}])
    legs.append({'hip':f'hip{i}','knee':f'knee{i}','foot':f'foot{i}','home':[side*.8,-.5,z*1.2],'pole':[side*.6,.3,z*1.2]})
gltf={'asset':{'version':'2.0','generator':'AIVERSE procedural spider example'},'scene':0,'scenes':[{'nodes':[0]}],'nodes':nodes,'meshes':meshes,
      'materials':[{'pbrMetallicRoughness':{'baseColorFactor':[.12,.16,.22,1],'metallicFactor':.3,'roughnessFactor':.6}},{'pbrMetallicRoughness':{'baseColorFactor':[1,.45,.06,1],'metallicFactor':.1,'roughnessFactor':.7}}],
      'buffers':[{'uri':'spider.bin','byteLength':len(blob)}],'bufferViews':views,'accessors':accessors}
(out/'models/spider.gltf').write_text(json.dumps(gltf));(out/'models/spider.bin').write_bytes(blob)
graph={'model':'models/spider.gltf','parameters':{'velocity':[0,0,.4],'adhesion':1,'reset':0},'nodes':[{'id':'bind','kind':'bind','position':[25,45]},
 {'id':'crawl','kind':'rig','input':'bind','position':[250,45],'units':[{'op':'surfaceLocomotion','bone':'body','velocity':'velocity','adhesion':'adhesion','resetParameter':'reset',
 'clearance':.5,'bodyRadius':.2,'probe':.8,'stepDistance':.18,'stepHeight':.12,'stepDuration':.2,'turnSpeed':4,'maxSpeed':1,'minSupport':5,'maxSwing':2,'world':False,'legs':legs,
 'obstacles':[{'kind':'plane','point':[0,0,0],'normal':[0,1,0]},{'kind':'plane','point':[0,0,2],'normal':[0,0,-1]},{'kind':'plane','point':[0,2.5,0],'normal':[0,-1,0]}]}]}],'output':'crawl'}
(out/'animations/spider.json').write_text(json.dumps(graph,indent=2))
# Remove line comments outside quoted strings, then replace the complete demo view.
p=out/'rules.json'
clean=re.sub(r'("(?:\\.|[^"\\])*")|//[^\n]*',lambda m:m.group(1) or '',p.read_text())
rules=json.loads(clean)
rules['types']['footman']['view']={'model':'models/spider.gltf','programmableAnimation':True,'animationGraph':graph}
p.write_text(json.dumps(rules,indent=2))
print(out)

if '--dynamic-tank' in sys.argv[2:]:
    # A separate scene-backed rig: no hidden demo planes. The small spider starts
    # on the turret and walks while the actual tank translates, pitches and rolls.
    scene_graph=json.loads(json.dumps(graph))
    motor=scene_graph['nodes'][1]['units'][0]
    motor.update(world=True,obstacles=[],followSurface=True,stepDistance=.05,stepHeight=.06)
    scene_graph['parameters']['velocity']=[0,0,0]
    (out/'animations/spider_scene.json').write_text(json.dumps(scene_graph,indent=2))
    rules['types']['footman']['move']={'speed':0,'radius':.05,'layer':'air'}
    rules['types']['footman']['weapons']=[]
    rules['types']['footman']['view']={'model':'models/spider.gltf','height':.195,'center':False,'forward':'+x','flyHeight':0,'offset':[0,.9,-.05625],'programmableAnimation':True,'animationGraph':scene_graph}
    rules['types']['catapult']['weapons']=[]
    rules['types']['catapult']['view']={'model':'models/tank.glb','height':.9,'center':False,'forward':'+x','turnSpeed':0}
    rules['scripts']=['scripts/tank_motion.lua'];rules['viewScripts']=['scripts/view/tank_surface.lua']
    (out/'scripts/view').mkdir(parents=True,exist_ok=True)
    (out/'scripts/tank_motion.lua').write_text('''-- Authoritative movement uses the existing deterministic game API.
function on_tick(tick)
    local phase = tick % 400
    local distance = phase < 200 and phase or 400-phase
    for _, unit in ipairs(game.units{player=1}) do
        if game.type_of(unit) == "catapult" then
            game.set_position(unit, fx(12) + fx(distance,100), fx(12))
        end
    end
end
''')
    (out/'scripts/view/tank_surface.lua').write_text('''-- Tilt/lift are presentation transforms; collision frames follow the drawn object.
local time = 0
view.on_frame(function(dt)
    time = time + dt
    view.set_camera{x=13,z=12,zoom=5,pitch=50,yaw=25}
    for _, unit in ipairs(game.units{player=1}) do
        local kind = game.type_of(unit)
        if kind == "catapult" then
            view.entity_fx(unit, {lift=0.08*math.sin(time*.7), roll=6*math.sin(time*.4), pitch=4*math.sin(time*.3), time=0})
        elseif kind == "footman" then
            view.animation(unit, {velocity={0.05*math.cos(time*.4),0,0.05*math.sin(time*.4)}})
        end
    end
end)
''')
    scene={'name':'Spider on a moving tank','size':[24,24],'players':[{'name':'Blue','team':1},{'name':'Red','team':2}],
           'terrain':{'rows':['.'*24 for _ in range(24)]},
           'entities':[{'type':'townhall','owner':1,'pos':[3,3]},{'type':'townhall','owner':2,'pos':[21,21]},
                       {'type':'catapult','owner':1,'pos':[12,12]},{'type':'footman','owner':1,'pos':[12,12]}]}
    (out/'maps/valley.json').write_text(json.dumps(scene,indent=2));(out/'rules.json').write_text(json.dumps(rules,indent=2))
    print('Dynamic tank scene: animations/spider_scene.json')
