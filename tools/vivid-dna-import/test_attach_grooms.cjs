const assert=require('node:assert/strict');
const path=require('node:path');
const load=require('node:module').createRequire(path.resolve(process.argv[2],'package.json'));
const {Document,NodeIO}=load('@gltf-transform/core');
const {attachRigidGroom}=require('./attach_grooms.cjs');
(async()=>{
 const doc=new Document(),scene=doc.createScene(),root=doc.createNode('root').setScale([2,3,4]);scene.addChild(root);
 const head=doc.createNode('head').setTranslation([1,2,3]),hair=doc.createNode('hair').setTranslation([5,6,7]);root.addChild(head).addChild(hair);
 const mesh=doc.createMesh('brows').setWeights([.3]);hair.setMesh(mesh).setWeights([.2]);
 const before=hair.getWorldMatrix();attachRigidGroom(doc,hair,head);
 const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-5,`${v} != ${b[i]}`));
 near(hair.getWorldMatrix(),before);assert.equal(hair.getMesh(),mesh);assert.deepEqual(hair.getWeights(),[.2]);
 head.setRotation([0,0,Math.SQRT1_2,Math.SQRT1_2]);near(hair.getWorldTranslation(),[-6,18,28]);
 head.setScale([1.5,1,1]);near(hair.getWorldTranslation(),[-6,24,28]);
 const count=doc.getRoot().listNodes().length;assert.equal(attachRigidGroom(doc,hair,head).alreadyAttached,true);assert.equal(doc.getRoot().listNodes().length,count);
 assert.throws(()=>attachRigidGroom(doc,head,hair),/cycle/);
 const skinned=doc.createNode('skinned').setSkin(doc.createSkin());assert.throws(()=>attachRigidGroom(doc,skinned,head),/unskinned/);
 // Persist the hierarchy through actual GLB export/import, without asset files.
 const io=new NodeIO();const reloaded=await io.readBinary(await io.writeBinary(doc));const restored=reloaded.getRoot().listNodes().find(n=>n.getName()==='hair');near(restored.getWorldTranslation(),[-6,24,28]);
 console.log('PASS: bind frame, rotated/scaled head following, morph ownership, idempotence, cycle/skin rejection, GLB roundtrip');
})().catch(e=>{console.error(e);process.exitCode=1;});
