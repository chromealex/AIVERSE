const assert=require('node:assert/strict');
const {createRequire}=require('node:module');
const path=require('node:path');
const {repairMorphNormals}=require('./repair_morph_normals.cjs');
const {Document}=createRequire(path.resolve(process.argv[2],'package.json'))('@gltf-transform/core');
const doc=new Document(),buffer=doc.createBuffer();
const attr=a=>doc.createAccessor().setType('VEC3').setArray(new Float32Array(a)).setBuffer(buffer);
const pos=attr([0,0,0,1,0,0,0,1,0]),normal=attr([0,0,1,0,0,1,0,0,1]);
const deformation=attr([0,0,0,0,0,0,0,0,1]);
const invalid=attr([0,0,-2,0,0,-2,0,0,-2]);
const target=doc.createPrimitiveTarget().setAttribute('POSITION',deformation).setAttribute('NORMAL',invalid);
const other=doc.createPrimitiveTarget().setAttribute('POSITION',deformation).setAttribute('NORMAL',invalid);
const p=doc.createPrimitive().setAttribute('POSITION',pos).setAttribute('NORMAL',normal).addTarget(target).addTarget(other);
const mesh=doc.createMesh().addPrimitive(p).setExtras({targetNames:['eyeBlinkLeft','smile']});
assert.equal(repairMorphNormals(doc,mesh,()=>true,['eyeBlinkLeft']).repaired,1);
const delta=target.getAttribute('NORMAL').getArray();
for(let i=0;i<3;i++) {
 assert.ok(Math.abs(delta[i*3])<1e-6);
 assert.ok(Math.abs(delta[i*3+1]+Math.SQRT1_2)<1e-6);
 assert.ok(Math.abs(delta[i*3+2]+1-Math.SQRT1_2)<1e-6);
}
assert.equal(p.getAttribute('POSITION'),pos);assert.equal(p.getAttribute('NORMAL'),normal);
assert.equal(target.getAttribute('POSITION'),deformation);assert.equal(other.getAttribute('NORMAL'),invalid);
const once=[...delta];repairMorphNormals(doc,mesh,()=>true,['eyeBlinkLeft']);
assert.deepEqual([...target.getAttribute('NORMAL').getArray()],once);
console.log('PASS: folded surface normal, untouched base/position/unselected morph, idempotent repair');
