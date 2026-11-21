"""Create original procedural watch/chain and a garment variant for an external Ada test package.
No downloaded/licensed character data is included in this script.
"""
import base64,json,math,struct,sys
from pathlib import Path
root=Path(sys.argv[1]); doc=json.loads((root/'model.gltf').read_text()); nodes=doc['nodes']
parents={c:i for i,n in enumerate(nodes) for c in n.get('children',[])}
def mul(a,b):return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def local(n):
 x,y,z,w=n.get('rotation',[0,0,0,1]);s=n.get('scale',[1,1,1]);t=n.get('translation',[0,0,0])
 m=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),t[0]],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),t[1]],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y),t[2]],[0,0,0,1]]
 for r in range(3):
  for c in range(3):m[r][c]*=s[c]
 return m
world={}
def matrix(i):
 if i not in world:world[i]=mul(matrix(parents[i]),local(nodes[i])) if i in parents else local(nodes[i])
 return world[i]
def inverse(a):
 m=[row[:]+[float(i==j) for j in range(4)] for i,row in enumerate(a)]
 for c in range(4):
  pivot=max(range(c,4),key=lambda r:abs(m[r][c]));m[c],m[pivot]=m[pivot],m[c];v=m[c][c];m[c]=[x/v for x in m[c]]
  for r in range(4):
   if r!=c:
    v=m[r][c];m[r]=[x-v*y for x,y in zip(m[r],m[c])]
 return [row[4:] for row in m]
def point(m,p):return [sum(m[r][k]*p[k] for k in range(3))+m[r][3] for r in range(3)]
def add(a,b):return [x+y for x,y in zip(a,b)]
def scale(a,s):return [x*s for x in a]
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def unit(a):return scale(a,1/math.sqrt(sum(x*x for x in a)))
def tube(curve,radius,bone,name,color):
 inv=inverse(matrix(bone));vertices=[];normals=[];indices=[];centers=[curve(i*2*math.pi/96) for i in range(96)]
 for i,c in enumerate(centers):
  tangent=unit([a-b for a,b in zip(centers[(i+1)%96],centers[(i-1)%96])]);u=unit(cross(tangent,[0,0,1]));v=cross(tangent,u)
  for k in range(8):
   n=add(scale(u,math.cos(k*math.pi/4)),scale(v,math.sin(k*math.pi/4)));vertices.extend(point(inv,add(c,scale(n,radius))));normals.extend(unit([sum(inv[r][j]*n[j] for j in range(3)) for r in range(3)]))
   a=i*8+k;b=((i+1)%96)*8+k;c1=i*8+(k+1)%8;d=((i+1)%96)*8+(k+1)%8;indices.extend([a,b,c1,c1,b,d])
 data=bytearray();views=[];accessors=[]
 def accessor(values,fmt,comps,typ):
  offset=len(data);data.extend(struct.pack('<'+fmt*len(values),*values));views.append({'buffer':0,'byteOffset':offset,'byteLength':len(data)-offset});a={'bufferView':len(views)-1,'componentType':5126 if fmt=='f' else 5125,'count':len(values)//comps,'type':typ}
  if typ=='VEC3':a.update(min=[min(values[k::3]) for k in range(3)],max=[max(values[k::3]) for k in range(3)])
  accessors.append(a);return len(accessors)-1
 pos=accessor(vertices,'f',3,'VEC3');normal=accessor(normals,'f',3,'VEC3');idx=accessor(indices,'I',1,'SCALAR')
 model={'asset':{'version':'2.0','generator':'AIVERSE wardrobe test fixture'},'scene':0,'scenes':[{'nodes':[0]}],'nodes':[{'name':name,'mesh':0}],'meshes':[{'name':name,'primitives':[{'attributes':{'POSITION':pos,'NORMAL':normal},'indices':idx,'material':0}]}],'materials':[{'name':name,'pbrMetallicRoughness':{'baseColorFactor':color+[1],'metallicFactor':.9,'roughnessFactor':.28}}],'buffers':[{'uri':'data:application/octet-stream;base64,'+base64.b64encode(data).decode(),'byteLength':len(data)}],'bufferViews':views,'accessors':accessors}
 if name=='SilverWatchBand':
  center=point(matrix(bone),[0,0,0]);center[2]+=.055
  def part(label,verts,inds,color,metal=0):
   a=accessor([q for v in verts for q in point(inv,v)],'f',3,'VEC3');normal=unit([inv[r][2] for r in range(3)]);b=accessor(normal*len(verts),'f',3,'VEC3');c=accessor(inds,'I',1,'SCALAR');material=len(model['materials']);model['materials'].append({'name':label,'pbrMetallicRoughness':{'baseColorFactor':color+[1],'metallicFactor':metal,'roughnessFactor':.35}});model['meshes'][0]['primitives'].append({'attributes':{'POSITION':a,'NORMAL':b},'indices':c,'material':material})
  verts=[center]+[[center[0]+.042*math.cos(k*2*math.pi/32),center[1]+.042*math.sin(k*2*math.pi/32),center[2]] for k in range(32)]
  part('Watch dial',verts,[v for k in range(32) for v in [0,k+1,(k+1)%32+1]],[.015,.025,.04])
  for dx,dy in [(0,.029),(.021,-.008)]:
   part('Watch hands',[[center[0]-.003,center[1],center[2]+.001],[center[0]+.003,center[1],center[2]+.001],[center[0]+dx,center[1]+dy,center[2]+.001]],[0,1,2],[.9,.9,.8])
  model['buffers']=[{'uri':'data:application/octet-stream;base64,'+base64.b64encode(data).decode(),'byteLength':len(data)}]
 (root/(name+'.gltf')).write_text(json.dumps(model))
hand=811;neck=698
w=point(matrix(hand),[0,0,0]);n=point(matrix(neck),[0,0,0]);print('wrist',w,'neck',n)
a=unit([matrix(hand)[r][0] for r in range(3)]);u=unit(cross(a,[0,0,1]));v=cross(a,u)
tube(lambda t:add(w,add(scale(u,.054*math.cos(t)),scale(v,.045*math.sin(t)))),.012,hand,'SilverWatchBand',[.65,.7,.75])
tube(lambda t:[n[0]+.22*math.cos(t),n[1]-.10-.18*math.sin(t),n[2]+.06+.32*math.sin(t)],.009,neck,'GoldChain',[.75,.42,.07])
# Separate source garment retains exact original bind space and rig names.
shirt=json.loads(json.dumps(doc));shirt.pop('animations',None)
for node in shirt['nodes']:
 if node.get('mesh')!=1:node.pop('mesh',None);node.pop('skin',None)
shirt['materials'][1]['pbrMetallicRoughness']['baseColorFactor']=[.22,.42,.8,1]
(root/'BlueShirt.gltf').write_text(json.dumps(shirt))
def item(id,asset,slot,bone='',finish='source',hide=None):return dict(id=id,asset='Ada/'+asset,slot=slot,enabled=True,bone=bone,translation=[0,0,0],rotation=[0,0,0],scale=[1,1,1],finish=finish,hideMeshes=hide or [],masks=[])
recipe={'version':1,'hiddenMeshes':[],'items':[item('shirt','BlueShirt.gltf','top',finish='cloth',hide=[doc['meshes'][1]['name']]),item('watch','SilverWatchBand.gltf','wrist_left',nodes[hand]['name']),item('chain','GoldChain.gltf','neck',nodes[neck]['name'])]}
# UV-space mask fixture: central patch hidden beneath the shirt, white elsewhere.
import zlib
w=64;scan=bytearray()
for y in range(w):
 scan.append(0)
 for x in range(w):scan.extend([0 if 24<=x<40 and 24<=y<40 else 255]*3+[255])
def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
(root/'BodyVisibility.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,w,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b''))
recipe['items'][0]['masks']=[{'material':'f_med_nrw_body_MI_BodySynthesized','texture':'Ada/BodyVisibility.png'}]
(root/'wardrobe-test.json').write_text(json.dumps(recipe,indent=2))
(root/'wardrobe.json').write_text(json.dumps(recipe,indent=2))
