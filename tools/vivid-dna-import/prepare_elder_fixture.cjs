// Reproducible native skin QA fixture from Patchbay's CC0 MakeHuman old-man.glb.
// No invented normal/roughness maps and no face-atlas smoothing.
// node prepare_elder_fixture.cjs tooling-dir source.glb output-dir
const fs=require('node:fs'),path=require('node:path'),{createRequire}=require('node:module');
(async()=>{
 const [tooling,source,out]=process.argv.slice(2);
 if(!out||fs.existsSync(out))throw Error('Expected tooling source new-output-directory');
 const load=createRequire(path.resolve(tooling,'package.json'));
 const {NodeIO}=load('@gltf-transform/core'),{ALL_EXTENSIONS}=load('@gltf-transform/extensions');
 const {MeshoptDecoder}=await import(require('node:url').pathToFileURL(load.resolve('meshoptimizer')));
 await MeshoptDecoder.ready;
 const io=new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({'meshopt.decoder':MeshoptDecoder});
 const doc=await io.read(source),root=doc.getRoot(),sharp=load('sharp');
 const info=[];
 for(const t of root.listTextures()) {
  const metadata=await sharp(t.getImage()).metadata();
  info.push({name:t.getName(),width:metadata.width,height:metadata.height});
  t.setImage(await sharp(t.getImage()).png().toBuffer()).setMimeType('image/png');
 }
 for(const ext of root.listExtensionsUsed())if(['EXT_meshopt_compression','EXT_texture_webp'].includes(ext.extensionName))ext.dispose();
 // Engine supports64 named morphs. Retain the52 ARKit face channels;
 // remove only unrelated speech visemes, with weights remapped by index.
 for(const mesh of root.listMeshes()) {
  const names=mesh.getExtras().targetNames||[];
  const keep=names.map((name,i)=>({name,i})).filter(x=>!x.name.startsWith('viseme_'));
  if(keep.length===names.length)continue;
  for(const primitive of mesh.listPrimitives()) {
   const targets=primitive.listTargets();
   for(let i=0;i<targets.length;i++)if(!keep.some(x=>x.i===i))primitive.removeTarget(targets[i]);
  }
  mesh.setExtras({...mesh.getExtras(),targetNames:keep.map(x=>x.name)});
  const weights=mesh.getWeights();if(weights.length)mesh.setWeights(keep.map(x=>weights[x.i]||0));
  for(const node of root.listNodes())if(node.getMesh()===mesh){const w=node.getWeights();if(w.length)node.setWeights(keep.map(x=>w[x.i]||0));}
 }
 const head=root.listNodes().find(n=>n.getName()==='Head');
 if(!head)throw Error('Fixture head missing');
 const eye=root.listNodes().find(n=>n.getName()==='LeftEye').getWorldTranslation();
 // Frame actual eyes at the same native portrait center; preserve authored rig.
 const wrapper=doc.createNode('vivid_elder_root').setTranslation([0, .96-eye[1]*2,-eye[2]*2]).setScale([2,2,2]);
 for(const scene of root.listScenes()){
  for(const node of scene.listChildren())wrapper.addChild(node);
  scene.addChild(wrapper);
 }
 fs.mkdirSync(out,{recursive:true});
 await io.write(path.join(out,'model.glb'),doc);
 fs.writeFileSync(path.join(out,'vivid-dna.json'),JSON.stringify({version:1,id:'makehuman-elder-skin-qa',displayName:'MakeHuman older male — skin QA',rig:{rootBone:'vivid_elder_root',headBone:'Head',jawBone:'unused_jaw',leftEyeBone:'unused_left',rightEyeBone:'unused_right'},skin:{melaninColor:[1,1,1],roughness:.62,oiliness:.08,wrinkleStrength:0}},null,2));
 fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify({version:1,model:'model.glb',dna:'vivid-dna.json',authoredHair:true,materials:{'old-man.body':'skin'}},null,2));
 fs.writeFileSync(path.join(out,'fixture-info.json'),JSON.stringify({source,eye,scale:2,textures:info,normalMaps:0,roughnessMaps:0},null,2));
 console.log(JSON.stringify({eye,textures:info}));
})().catch(e=>{console.error(e);process.exitCode=1;});
