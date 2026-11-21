// Explicit wardrobe visibility bake for a closed upper-body outfit.
// Keep skin above a caller-selected bind-world Y and hands; filter indices only.
// This is an outfit-specific mask, never an automatic change to imported people.
const fs=require('node:fs'),path=require('node:path'),{createRequire}=require('node:module');
(async()=>{const [tooling,input,config,out]=process.argv.slice(2);if(!out||fs.existsSync(out))throw Error('Expected tooling input config new-output');const spec=JSON.parse(fs.readFileSync(config));if(spec.version!==1||!Number.isFinite(spec.aboveY)||!Array.isArray(spec.materials))throw Error('Invalid visibility mask');
if(spec.neckBand&&(!Number.isFinite(spec.neckBand.minY)||!Number.isFinite(spec.neckBand.halfWidth)||spec.neckBand.halfWidth<=0))throw Error('Invalid neck band');
const load=createRequire(path.resolve(tooling,'package.json')),{NodeIO}=load('@gltf-transform/core'),io=new NodeIO().registerExtensions(load('@gltf-transform/extensions').ALL_EXTENSIONS),doc=await io.read(input);let removed=0;
const point=(m,v)=>[0,1,2].map(k=>m[k]*v[0]+m[k+4]*v[1]+m[k+8]*v[2]+m[k+12]);const semantic=n=>n.getName().replace(/_\d+$/,'');
for(const node of doc.getRoot().listNodes())if(node.getMesh())for(const p of node.getMesh().listPrimitives())if(spec.materials.includes(p.getMaterial()?.getName())){
 const position=p.getAttribute('POSITION'),joint=p.getAttribute('JOINTS_0'),weight=p.getAttribute('WEIGHTS_0'),skin=node.getSkin(),indices=p.getIndices();if(!indices)throw Error('Indexed skin required');const joints=skin?.listJoints(),inverse=skin?.getInverseBindMatrices();
 const hand=joints?.map(n=>{for(let a=n;a;a=a.getParentNode())if(['hand_l','hand_r','LeftHand','RightHand'].includes(semantic(a)))return true;return false;});
 const neck=joints?.map(n=>{for(let a=n;a;a=a.getParentNode())if(['neck_01','neck_02','head','vivid_face_head'].includes(semantic(a)))return true;return false;});
 const keep=[];for(let i=0;i<position.getCount();i++){const v=position.getElement(i,[]);let world=point(node.getWorldMatrix(),v),handWeight=0,neckWeight=0;if(skin&&joint&&weight){const js=joint.getElement(i,[]),ws=weight.getElement(i,[]),total=ws.reduce((a,b)=>a+b,0);if(total<=0)throw Error('Unweighted skin');world=[0,0,0];for(let k=0;k<4;k++)if(ws[k]){const q=point(joints[js[k]].getWorldMatrix(),point(inverse.getElement(js[k],[]),v));for(let a=0;a<3;a++)world[a]+=q[a]*ws[k]/total;if(hand[js[k]])handWeight+=ws[k]/total;if(neck[js[k]])neckWeight+=ws[k]/total;}}
 keep.push(world[1]>=spec.aboveY||(spec.neckBand&&world[1]>=spec.neckBand.minY&&Math.abs(world[0])<=spec.neckBand.halfWidth&&(!spec.neckBand.requireNeckWeight||neckWeight>.75))||handWeight>.25);}
 const inputIndices=indices.getArray(),filtered=[];for(let i=0;i<inputIndices.length;i+=3){const tri=Array.from(inputIndices.slice(i,i+3));if(tri.every(v=>keep[v]))filtered.push(...tri);else removed++;}
 if(!filtered.length)throw Error('Visibility mask removed entire surface');p.setIndices(doc.createAccessor().setType('SCALAR').setArray(new Uint32Array(filtered)).setBuffer(indices.getBuffer()));
}
if(!removed)throw Error('Mask did not cover any triangles');await io.write(out,doc);console.log('Baked covered-skin triangles:',removed);
})().catch(e=>{console.error(e);process.exitCode=1});
