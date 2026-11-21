// Explicit offline fallback for imported clothing lacking pose-space correctives.
// Geometry/UV/normal maps unchanged; Jacobi-smooth only the requested material's
// skin weights on welded topology, retaining four normalized influences.
const fs=require('node:fs'),path=require('node:path'),{createRequire}=require('node:module');
function smoothWeights(positions,indices,joints,weights,iterations=4,strength=.65){
 if(!Number.isInteger(iterations)||iterations<1||iterations>16||!Number.isFinite(strength)||strength<0||strength>1)throw Error('Invalid smoothing parameters');
 if(!Array.from(positions).every(Number.isFinite)||!Array.from(joints).every(j=>Number.isInteger(j)&&j>=0&&j<=65535))throw Error('Invalid skin data');
 const count=positions.length/3;if(!Number.isInteger(count)||count<1||joints.length!==count*4||weights.length!==count*4||indices.length%3)throw Error('Invalid skin arrays');
 const groups=[],lookup=new Map(),ids=[];
 for(let i=0;i<count;i++){const key=Array.from(positions.slice(i*3,i*3+3),x=>Math.round(x*1e6)).join(',');let id=lookup.get(key);if(id===undefined){id=groups.length;lookup.set(key,id);groups.push({vertices:[],neighbors:new Set()});}ids.push(id);groups[id].vertices.push(i);}
 for(let i=0;i<indices.length;i+=3){const tri=Array.from(indices.slice(i,i+3),v=>{if(!Number.isInteger(v)||v<0||v>=count)throw Error('Invalid triangle');return ids[v];});for(const a of tri)for(const b of tri)if(a!==b)groups[a].neighbors.add(b);}
 let values=groups.map(g=>{const out=new Map();for(const i of g.vertices)for(let k=0;k<4;k++){const w=weights[i*4+k];if(!Number.isFinite(w)||w<0)throw Error('Invalid weight');if(w)out.set(joints[i*4+k],(out.get(joints[i*4+k])||0)+w/g.vertices.length);}return out;});
 for(let it=0;it<iterations;it++)values=groups.map((g,i)=>{if(!g.neighbors.size)return values[i];const next=new Map();for(const [j,w] of values[i])next.set(j,w*(1-strength));for(const n of g.neighbors)for(const [j,w] of values[n])next.set(j,(next.get(j)||0)+w*strength/g.neighbors.size);return next;});
 const outJ=new Uint16Array(joints.length),outW=new Float32Array(weights.length);
 for(let i=0;i<count;i++){const v=[...values[ids[i]]].sort((a,b)=>b[1]-a[1]||a[0]-b[0]).slice(0,4),sum=v.reduce((a,b)=>a+b[1],0);if(sum<=0)throw Error('Unweighted vertex');for(let k=0;k<v.length;k++){outJ[i*4+k]=v[k][0];outW[i*4+k]=v[k][1]/sum;}}
 return {joints:outJ,weights:outW};
}
module.exports={smoothWeights};
if(require.main===module)(async()=>{const [tooling,input,material,out,iterations="4",strength=".65"]=process.argv.slice(2);if(!out||fs.existsSync(out))throw Error('Expected tooling input material new-output');const load=createRequire(path.resolve(tooling,'package.json')),{NodeIO}=load('@gltf-transform/core'),io=new NodeIO().registerExtensions(load('@gltf-transform/extensions').ALL_EXTENSIONS),doc=await io.read(input);let changed=0;
 for(const mesh of doc.getRoot().listMeshes())for(const p of mesh.listPrimitives())if(p.getMaterial()?.getName()===material){if(p.listTargets().length)throw Error('Cloth morph correctives require explicit transfer');const pos=p.getAttribute('POSITION'),j=p.getAttribute('JOINTS_0'),w=p.getAttribute('WEIGHTS_0');if(!j||!w||!p.getIndices())throw Error('Expected indexed skinned clothing');const positions=new Float32Array(pos.getCount()*3),weights=new Float32Array(w.getCount()*4),joints=new Uint16Array(j.getCount()*4);for(let i=0;i<pos.getCount();i++){positions.set(pos.getElement(i,[]),i*3);weights.set(w.getElement(i,[]),i*4);joints.set(j.getElement(i,[]),i*4);}const result=smoothWeights(positions,p.getIndices().getArray(),joints,weights,Number(iterations),Number(strength));p.setAttribute('JOINTS_0',doc.createAccessor().setType('VEC4').setArray(result.joints).setBuffer(j.getBuffer()));p.setAttribute('WEIGHTS_0',doc.createAccessor().setType('VEC4').setArray(result.weights).setBuffer(w.getBuffer()));changed++;}
 if(!changed)throw Error('Clothing material not found');await io.write(out,doc);console.log('Smoothed clothing primitives:',changed);
})().catch(e=>{console.error(e);process.exitCode=1});
