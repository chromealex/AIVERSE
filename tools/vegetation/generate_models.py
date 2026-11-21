#!/usr/bin/env python3
"""Offline authored vegetation prototypes, not a replacement for TerraWeave runtime.
Curved hierarchical spines + a smooth implicit woody union; anchored foliage.
EXPERIMENTAL: no native Forest integration or LOD export yet.
Near budgets: 9000 bark triangles/oak, 1400/shrub, plus foliage.
Requires numpy, scipy, scikit-image, fast-simplification, xatlas, Pillow. Units: metres. Deterministic seed.
"""
from pathlib import Path
import argparse,json,struct,math
import numpy as np
from skimage.measure import marching_cubes
from fast_simplification import simplify
from bake_bark_atlas import bake_bark_atlas

def unit(v):
    v=np.asarray(v,float);return v/max(np.linalg.norm(v),1e-12)

def curve(points,t):
    a,b,c,d=points;t=np.asarray(t)
    return (1-t)[...,None]**3*a+3*((1-t)**2*t)[...,None]*b+3*((1-t)*t*t)[...,None]*c+t[...,None]**3*d

class Plant:
    def __init__(self,seed):self.rng=np.random.default_rng(seed);self.spines=[];self.leaves=[]
    def spine(self,points,r0,r1,root=False):
        p=np.asarray(points,float);length=sum(np.linalg.norm(p[i+1]-p[i]) for i in range(3));steps=max(6,int(length/.10));ts=np.linspace(0,1,steps+1)
        xyz=curve(p,ts);r=r1+(r0-r1)*(1-ts)**(1.15 if root else 1.35)
        if root:
            # Shape the UPPER surface, not just the centreline: near the soil
            # it flattens, then sinks below ground before the geometric tip.
            # This avoids the straight triangular fin cut by a ground plane.
            top=max(.12,float(p[0,1])+r0)*(1-ts)**3-.10*ts**4
            xyz[:,1]=top-r
        self.spines.append((xyz,r,root));return p
    def sprigs(self,parent,size,count):
        rng=self.rng
        for j,t in enumerate(np.linspace(.32,.98,count)):
            anchor=curve(parent,t);tangent=unit(curve(parent,min(1,t+.01))-curve(parent,max(0,t-.01)))
            side=unit(np.cross(tangent,[0,1,0]));side*=(-1 if j%2 else 1)
            direction=unit(tangent*.6+side*.65+np.array([0,.38,0]))
            end=anchor+direction*rng.uniform(size*.30,size*.48)
            twig=self.spine([anchor,anchor+tangent*.09,end-direction*.07,end],size*.018,size*.005)
            self.leaves.append((end,direction,size*rng.uniform(.85,1.15),int(rng.integers(4)),rng.uniform(-1.1,1.1)))
    def build(self,kind):
        rng=self.rng
        if kind=='oak':
            trunk=self.spine([[0,-.35,0],[.12,1.8,-.06],[-.18,4.2,.12],[.15,6.4,.18]],.39,.018)
            # Irregular buttress roots curve down into the soil, not straight radial pegs.
            for j in range(6):
                a=j*math.tau/6+rng.uniform(-.28,.28);d=np.array([math.cos(a),0,math.sin(a)]);side=np.array([-d[2],0,d[0]])
                length=rng.uniform(.85,1.65);start=d*.05+[0,rng.uniform(.18,.42),0]
                self.spine([start,d*.30+[0,.08,0],d*(length*.66)+side*rng.uniform(-.18,.18)+[0,-.025,0],d*length+side*.12+[0,-.19,0]],rng.uniform(.17,.24),.012,True)
            for j,t in enumerate(np.linspace(.29,.88,11)):
                start=curve(trunk,t);a=j*2.399+rng.uniform(-.3,.3);out=np.array([math.cos(a),0,math.sin(a)]);length=(1.05+1.9*math.sin(t*math.pi))*rng.uniform(.8,1.1)
                end=start+out*length+[0,rng.uniform(.6,1.4),0]
                main=self.spine([start,start+out*.35+[0,.45,0],end-out*.5+[0,-.20,0],end],.13*(1-t)+.035,.008)
                for k,s in enumerate(np.linspace(.27,.94,6)):
                    a0=curve(main,s);tan=unit(curve(main,min(1,s+.02))-curve(main,s-.02));side=unit(np.cross(tan,[0,1,0]))*(-1 if k%2 else 1)
                    direction=unit(tan*.45+side*.7+[0,.38,0]);reach=rng.uniform(.55,.95)*(1.1-.3*s);e=a0+direction*reach
                    sec=self.spine([a0,a0+tan*.16,e-direction*.2+[0,-.08,0],e],.025*(1-s)+.012,.003)
                    self.sprigs(sec,.48,5)
                self.sprigs(main,.52,5)
        else:
            # Staggered independent basal stems, with alternating side shoots.
            for j in range(7):
                a=j*2.399+rng.uniform(-.3,.3);out=np.array([math.cos(a),0,math.sin(a)]);base=out*rng.uniform(.015,.08)+[0,-.12,0];end=out*rng.uniform(.45,.75)+[0,rng.uniform(.8,1.35),0]
                main=self.spine([base,base+[0,.45,0],end-out*.2+[0,-.2,0],end],.034,.004)
                for k,t in enumerate(np.linspace(.25,.9,5)):
                    start=curve(main,t);tan=unit(curve(main,t+.02)-curve(main,t-.02));side=unit(np.cross(tan,[0,1,0]))*(-1 if k%2 else 1);d=unit(tan*.4+side*.65+[0,.4,0]);e=start+d*rng.uniform(.25,.45)
                    sec=self.spine([start,start+tan*.09,e-d*.09,e],.013,.0025);self.sprigs(sec,.27,4)
                self.sprigs(main,.32,5)
    def wood(self,step,budget=9000):
        pts=np.concatenate([p for p,r,_ in self.spines]);lo=pts.min(0)-.55;hi=pts.max(0)+.55;shape=np.ceil((hi-lo)/step).astype(int)+1
        if np.prod(shape)>60000000:raise ValueError('Voxel budget exceeded')
        field=np.full(tuple(shape),10,dtype=np.float32);segments=[]
        for spine_id,(path,radii,root) in enumerate(self.spines):
            along=float(path[0,1]+.35)
            for a,b,ra,rb in zip(path[:-1],path[1:],radii[:-1],radii[1:]):
                d=b-a;l=np.linalg.norm(d);k=min(.12,max(.008,ra*.55));pad=max(ra,rb)+k+step*2
                low=np.maximum(np.floor((np.minimum(a,b)-pad-lo)/step).astype(int),0);high=np.minimum(np.ceil((np.maximum(a,b)+pad-lo)/step).astype(int)+1,shape)
                axes=[lo[i]+np.arange(low[i],high[i],dtype=np.float32)*step for i in range(3)];x,y,z=np.meshgrid(*axes,indexing='ij',sparse=True)
                t=np.clip(((x-a[0])*d[0]+(y-a[1])*d[1]+(z-a[2])*d[2])/(l*l),0,1)
                dist=np.sqrt((x-a[0]-t*d[0])**2+(y-a[1]-t*d[1])**2+(z-a[2]-t*d[2])**2)-(ra+t*(rb-ra))
                region=tuple(slice(low[i],high[i]) for i in range(3));old=field[region];h=np.maximum(k-np.abs(old-dist),0)/k;field[region]=np.minimum(old,dist)-h*h*k*.25
                segments.append((a,b,ra,rb,along,spine_id));along+=l
        positions,faces,normals,_=marching_cubes(field,0,spacing=(step,step,step),allow_degenerate=False);positions+=lo
        # Surface normals from marching cubes point into an SDF; flip outward.
        normals=-normals
        cross=np.cross(positions[faces[:,1]]-positions[faces[:,0]],positions[faces[:,2]]-positions[faces[:,0]])
        flip=np.einsum('ij,ij->i',cross,normals[faces[:,0]])<0;faces[flip]=faces[flip][:,[0,2,1]]
        if len(faces)>budget:
            positions,faces=simplify(positions,faces,target_count=budget,agg=7)
        # Collapsed tiny twigs can leave zero-area or doubled opposite faces.
        # They have no surface and must not reach tangent/atlas generation.
        cross=np.cross(positions[faces[:,1]]-positions[faces[:,0]],positions[faces[:,2]]-positions[faces[:,0]])
        faces=faces[np.linalg.norm(cross,axis=1)>1e-10]
        _,inverse,counts=np.unique(np.sort(faces,axis=1),axis=0,return_inverse=True,return_counts=True)
        faces=faces[counts[inverse]==1]
        normals=np.zeros_like(positions)
        fallback=np.zeros_like(positions)
        cross=np.cross(positions[faces[:,1]]-positions[faces[:,0]],positions[faces[:,2]]-positions[faces[:,0]])
        for corner in range(3):
            np.add.at(normals,faces[:,corner],cross)
            fallback[faces[:,corner]]=cross
        weak=np.linalg.norm(normals,axis=1)<1e-12
        normals[weak]=fallback[weak]
        normals/=np.maximum(np.linalg.norm(normals,axis=1,keepdims=True),1e-12)
        used,remap=np.unique(faces,return_inverse=True)
        positions=positions[used];normals=normals[used];faces=remap.reshape(-1,3)
        # Bark projection is baked by bake_bark_atlas on a connected chart
        # unwrap, so runtime UVs never interpolate unrelated branch charts.
        return positions,normals,np.zeros((len(positions),2)),faces
    def foliage(self,kind):
        roots=[(.39,.94),(.03,.70),(.42,.95),(.04,.84)] if kind=='oak' else [(.44,.98),(.50,.95),(.47,.98),(.45,.99)]
        pos=[];norm=[];uv=[];faces=[]
        for anchor,direction,size,q,roll in self.leaves:
            v=unit(direction);u0=unit(np.cross(v,[0,0,1]));n0=np.cross(u0,v);u=u0*math.cos(roll)+n0*math.sin(roll);n=np.cross(u,v);ru,rv=roots[q];base=len(pos)
            for lu,lv in [(0,1),(1,1),(1,0),(0,0)]:pos.append(anchor+size*((lu-ru)*u+(rv-lv)*v));norm.append(n);uv.append([q%2*.5+.006+lu*.488,q//2*.5+.006+lv*.488])
            faces.extend([[base,base+1,base+2],[base,base+2,base+3]])
        return np.asarray(pos),np.asarray(norm),np.asarray(uv),np.asarray(faces)

def save_glb(path,parts,kind):
    doc={'asset':{'version':'2.0','generator':'AIVERSE authored vegetation prototype'},'scene':0,'scenes':[{'nodes':[0]}],'nodes':[{'mesh':0}],'meshes':[{'primitives':[]}],'buffers':[],'bufferViews':[],'accessors':[],'materials':[],'images':[],'textures':[],'samplers':[{'magFilter':9729,'minFilter':9987,'wrapS':10497,'wrapT':10497}]};binary=bytearray()
    def accessor(arr,ctype,typ,target):
        arr=np.asarray(arr,dtype='<f4' if ctype==5126 else '<u4');data=arr.tobytes();idx=len(doc['accessors']);view=len(doc['bufferViews']);doc['bufferViews'].append({'buffer':0,'byteOffset':len(binary),'byteLength':len(data),'target':target});binary.extend(data)
        a={'bufferView':view,'componentType':ctype,'count':len(arr),'type':typ}
        if typ=='VEC3':a.update(min=arr.min(0).tolist(),max=arr.max(0).tolist())
        doc['accessors'].append(a);return idx
    for i,(p,n,uv,f) in enumerate(parts):
        doc['meshes'][0]['primitives'].append({'attributes':{'POSITION':accessor(p,5126,'VEC3',34962),'NORMAL':accessor(n,5126,'VEC3',34962),'TEXCOORD_0':accessor(uv,5126,'VEC2',34962)},'indices':accessor(f.flatten(),5125,'SCALAR',34963),'material':i})
        name='oak_bark' if i==0 else ('oak_leaves' if kind=='oak' else 'shrub_leaves')
        def tex(s):
            j=len(doc['images']);uri=(path.stem+'_bark_'+('orm' if s=='ao' else s)+'.png') if i==0 else '../textures/'+name+'_'+s+'.png';doc['images'].append({'uri':uri});doc['textures'].append({'source':j,'sampler':0});return {'index':j}
        doc['materials'].append({'name':name,'pbrMetallicRoughness':{'baseColorTexture':tex('basecolor'),'metallicRoughnessTexture':tex('orm'),'metallicFactor':0,'roughnessFactor':1},'normalTexture':tex('normal'),'occlusionTexture':tex('ao'),'doubleSided':i==1,'alphaMode':'MASK' if i else 'OPAQUE','alphaCutoff':.5})
    doc['buffers']=[{'byteLength':len(binary)}];js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*((-len(js))%4);binary+=b'\0'*((-len(binary))%4)
    path.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(js)+len(binary))+struct.pack('<I4s',len(js),b'JSON')+js+struct.pack('<I4s',len(binary),b'BIN\0')+binary)

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--seed',type=int,default=414);ap.add_argument('--kind',choices=['oak','shrub'],default='oak');ap.add_argument('--bark',choices=['oak_bark','pine_bark','spruce_bark','birch_bark'],default='oak_bark');ap.add_argument('--step',type=float,default=.025);a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    plant=Plant(a.seed);plant.build(a.kind);wood=plant.wood(a.step,9000 if a.kind=='oak' else 1400);leaf=plant.foliage(a.kind);wood=bake_bark_atlas(wood,a.out,a.kind,material=a.bark);save_glb(a.out/(a.kind+'.glb'),[wood,leaf],a.kind)
    print(json.dumps({'kind':a.kind,'seed':a.seed,'spines':len(plant.spines),'sprigs':len(plant.leaves),'woodTriangles':len(wood[3]),'leafTriangles':len(leaf[3])}))
