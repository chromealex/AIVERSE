// External companion-export preparation, not DNA body reconstruction.
// Usage: node prepare_full_character.cjs tooling-dir source.glb output-dir [yaw]
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {createRequire} = require('node:module');
(async () => {
  const [tooling, source, destination, yawText = '0', materialPath, sourceMaps] = process.argv.slice(2);
  if (!destination) throw Error('Expected tooling-dir source.glb output-dir [yaw]');
  const yaw = Number(yawText);
  if (!Number.isFinite(yaw)) throw Error('Invalid yaw');
  if (fs.existsSync(destination)) throw Error('Output exists');
  const load = createRequire(path.join(path.resolve(tooling), 'package.json'));
  const {NodeIO} = load('@gltf-transform/core');
  const {ALL_EXTENSIONS} = load('@gltf-transform/extensions');
  const draco = load('draco3dgltf');
  const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({
    'draco3d.decoder': await draco.createDecoderModule()
  });
  const doc = await io.read(source);
  for (const ext of doc.getRoot().listExtensionsUsed()) {
    if (ext.extensionName === 'KHR_draco_mesh_compression') ext.dispose();
  }
  const root = doc.getRoot();
  const materialFallbacks = [];
  let bakedMaterials = [];
  if (materialPath) {
    const mapping = JSON.parse(fs.readFileSync(materialPath, 'utf8'));
    if(sourceMaps) {
      bakedMaterials = await require('./bake_materials.cjs').bakeMaterials(doc,mapping,sourceMaps,load('sharp'));
    }
    for (const material of root.listMaterials()) {
      if(sourceMaps) continue;
      const authored = mapping.materials.find(m => m.material_name === material.getName());
      if (authored?.kind !== 'cloth') continue;
      const p = authored.params;
      if (!Array.isArray(p.diffuse_color_1) || p.diffuse_color_1.length !== 4 ||
          !p.diffuse_color_1.every(Number.isFinite) || !Number.isFinite(p.roughness))
        throw Error('Invalid authored cloth parameters');
      // UE cloth exports put a packed region mask in the glTF colour slot.
      // Use its authored uniform colour instead of displaying RGB mask channels.
      // Full two-colour mask/detail shader parity remains separate work.
      material.setBaseColorTexture(null).setBaseColorFactor([...p.diffuse_color_1.slice(0,3),1])
        .setRoughnessFactor(p.roughness).setMetallicFactor(p.metallic ?? 0)
        .setMetallicRoughnessTexture(null);
      materialFallbacks.push(material.getName());
    }
  }
  const nodes = root.listNodes();
  const faceNode = nodes.find(n => n.getMesh()?.getName().match(/(?:^|_)FaceMesh|_FaceMesh/i));
  if (!faceNode?.getSkin()) throw Error('Expected a skinned companion face mesh');
  // Companion exports can carry inverted blink-normal deltas on the lids.
  // Repair only this known smooth face surface and the two blink channels.
  const blinkNormals = require('./repair_morph_normals.cjs').repairMorphNormals(
    doc, faceNode.getMesh(), m => /head_shader/.test(m?.getName() || ''),
    ['eyeBlinkLeft', 'eyeBlinkRight']);
  console.log('Blink normal repair:', blinkNormals);
  const head = faceNode.getSkin().listJoints().find(n => n.getName() === 'head');
  if (!head) throw Error('Face skin has no head bone');
  // Duplicate skeleton names in separate garment rigs must remain distinct.
  nodes.forEach((n,i) => n.setName((n.getName() || 'node') + '_' + i));
  head.setName('vivid_face_head');
  const min = [Infinity,Infinity,Infinity], max = [-Infinity,-Infinity,-Infinity];
  let vertices = 0;
  for (const node of nodes) {
    const mesh = node.getMesh(); if (!mesh) continue;
    const m = node.getWorldMatrix();
    for (const primitive of mesh.listPrimitives()) {
      const position = primitive.getAttribute('POSITION');
      vertices += position.getCount();
      for (let i=0; i<position.getCount(); i++) {
        const v = position.getElement(i, []);
        for (let a=0;a<3;a++) {
          const value = m[a]*v[0]+m[a+4]*v[1]+m[a+8]*v[2]+m[a+12];
          if (!Number.isFinite(value)) throw Error('Nonfinite world geometry');
          min[a]=Math.min(min[a],value);max[a]=Math.max(max[a],value);
        }
      }
    }
  }
  const height = max[1]-min[1];
  if (!(height>0) || vertices>1000000) throw Error('Invalid geometry budget/bounds');
  const scale = 3.8/height;
  const center = min.map((v,i)=>(v+max[i])/2);
  const scene = root.getDefaultScene() || root.listScenes()[0];
  const children = scene.listChildren().slice();
  const frame = doc.createNode('vivid_full_root').setScale([scale,scale,scale])
    .setTranslation([-center[0]*scale,-.65-center[1]*scale,-center[2]*scale]);
  for (const child of children) {scene.removeChild(child);frame.addChild(child);}
  const turn = doc.createNode('vivid_full_view').setRotation([0,Math.sin(yaw/2),0,Math.cos(yaw/2)]).addChild(frame);
  scene.addChild(turn);
  const groomBindings=[];
  for(const node of nodes)if(node.getMesh()&&!node.getSkin()&&/Hair_|Eyebrows_|Beard_|Mustache_/i.test(node.getMesh().getName()))
    groomBindings.push(require('./attach_grooms.cjs').attachRigidGroom(doc,node,head));
  fs.mkdirSync(destination,{recursive:true});
  await io.write(path.join(destination,'model.gltf'),doc);
  const hair = root.listMaterials().filter(m => /hair_|eyebrows_/i.test(m.getName()));
  if (!hair.length) throw Error('Missing authored groom materials');
  const manifest = {version:1,model:'model.gltf',dna:'vivid-dna.json',
    appearance:sourceMaps?'appearance.json':undefined,
    materials:Object.fromEntries(hair.map(m=>[m.getName(),'hair']))};
  const dna={version:1,id:path.basename(source,'.glb')+'-full-companion-export',displayName:path.basename(source,'.glb')+' — full character export',
    groom:{color:[0.03676008,0.02233289,0.01274758],roughness:0.55},
    rig:{rootBone:'vivid_full_root',headBone:'vivid_face_head',jawBone:'unused_jaw',leftEyeBone:'unused_eye_l',rightEyeBone:'unused_eye_r'}};
  if(sourceMaps) {
    const mapping=JSON.parse(fs.readFileSync(materialPath,'utf8'));
    const materials={};
    for(const mat of root.listMaterials()) {
      const original=mat.getName().replace(/__coverage_core$/,'');
      const spec=mapping.materials.find(m=>m.material_name===original);
      if(!spec)continue;
      const role=spec.kind==='skin'?'skin':spec.kind==='hair'?'hair':spec.kind==='cloth'?'cloth':
        spec.face_slot==='eyelashes'?'lashes':spec.params.eye_pole_uv?'eyes':null;
      if(role)materials[mat.getName()]=role;
    }
    fs.writeFileSync(path.join(destination,'appearance.json'),JSON.stringify({version:1,values:{},materials},null,2));
  }
  const provenance={source:path.basename(source),sha256:crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex'),
    sourceKind:'published MetaHuman companion GLB; not body reconstructed from legacy Ada.dna',vertices,
    meshes:root.listMeshes().map(m=>m.getName()),sourceBounds:{min,max},previewScale:scale,previewYaw:yaw,
    skins:root.listSkins().length,images:root.listTextures().length,
    uniformClothFallbacks:materialFallbacks,bakedMaterials,groomBindings,
    limitations:['No full RigLogic evaluation','Existing GLB textures/materials; no original UE shader parity claim']};
  for (const [name,value] of Object.entries({'manifest.json':manifest,'vivid-dna.json':dna,'provenance.json':provenance}))
    fs.writeFileSync(path.join(destination,name),JSON.stringify(value,null,2));
  console.log(JSON.stringify(provenance,null,2));
})().catch(e=>{console.error(e);process.exitCode=1;});
