// Author explicit VIVID part LOD chains into glTF mesh extras.aiverseLods.
// Input meshes must already contain authored geometry, compatible skins/morphs.
// node configure_lods.cjs tooling-dir input.glb chains.json new-output.glb
const fs=require('node:fs'),path=require('node:path'),{createRequire}=require('node:module');
function validateChains(spec,meshes){
 if(spec.version!==1||!Array.isArray(spec.chains)||!spec.chains.length)throw Error('Expected version1 chains');
 const byName=new Map();for(const m of meshes){if(byName.has(m.name))throw Error('Ambiguous mesh name');byName.set(m.name,m);}
 const sources=new Set(),targets=new Set();
 for(const chain of spec.chains){
  if(sources.has(chain.source)||!byName.has(chain.source)||!Array.isArray(chain.levels)||!chain.levels.length||chain.levels.length>7)throw Error('Invalid source/levels');
  sources.add(chain.source);let previous=Infinity;const local=new Set();
  for(const level of chain.levels){
   if(!byName.has(level.mesh)||level.mesh===chain.source||local.has(level.mesh)||!Number.isFinite(level.maxPixels)||level.maxPixels<=0||level.maxPixels>=previous)throw Error('Invalid LOD mesh/threshold');
   previous=level.maxPixels;local.add(level.mesh);targets.add(level.mesh);
   const a=byName.get(chain.source),b=byName.get(level.mesh);
   if(a.skinned!==b.skinned||a.skins!==b.skins)throw Error('LOD skin mismatch');
   if(a.morphs.length!==b.morphs.length||a.morphs.some(n=>!b.morphs.includes(n)))throw Error('LOD morph semantics mismatch');
  }
 }
 for(const s of sources)if(targets.has(s))throw Error('Nested/cyclic LOD chain');
 return spec;
}
module.exports={validateChains};
if(require.main===module)(async()=>{
 const [tooling,input,config,output]=process.argv.slice(2);
 if(!output||fs.existsSync(output))throw Error('Expected tooling input config new-output');
 const load=createRequire(path.resolve(tooling,'package.json')),{NodeIO}=load('@gltf-transform/core');
 const io=new NodeIO().registerExtensions(load('@gltf-transform/extensions').ALL_EXTENSIONS),doc=await io.read(input),root=doc.getRoot(),meshes=root.listMeshes();
 const info=meshes.map(m=>{const owners=root.listNodes().filter(n=>n.getMesh()===m);const skins=[...new Set(owners.map(n=>n.getSkin()).filter(Boolean))];return {name:m.getName(),skinned:skins.length>0,skins:skins.map(s=>root.listSkins().indexOf(s)).sort().join(','),morphs:m.getExtras().targetNames||[]};});
 for(const [i,m] of meshes.entries())for(const p of m.listPrimitives()) {
  const count=p.listTargets().length;
  if(count>0 && (info[i].morphs.length!==count||new Set(info[i].morphs).size!==count))throw Error('Explicit unique morph names required');
 }
 const spec=validateChains(JSON.parse(fs.readFileSync(config,'utf8')),info);
 const targets=new Set(spec.chains.flatMap(c=>c.levels.map(l=>l.mesh)));
 for(const c of spec.chains){const source=meshes.find(m=>m.getName()===c.source);source.setExtras({...source.getExtras(),aiverseLods:c.levels.map(l=>({mesh:meshes.findIndex(m=>m.getName()===l.mesh),maxPixels:l.maxPixels}))});}
 // Retain target nodes/skin declarations for importer palette resolution, but
 // detach alternatives from the scene so they are not drawn on top of LOD0.
 for(const n of root.listNodes())if(n.getMesh()&&targets.has(n.getMesh().getName())){
  if(n.listChildren().length)throw Error('LOD target node must not own children');
  for(const scene of root.listScenes())scene.removeChild(n);
  const parent=n.getParentNode();if(parent)parent.removeChild(n);
 }
 await io.write(output,doc);console.log('Wrote '+spec.chains.length+' authored LOD chains');
})().catch(e=>{console.error(e);process.exitCode=1;});
